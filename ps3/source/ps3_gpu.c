/*
 * Copyright (C) 2026 the CrispyCell / ROTT-PS3 / ECellWolf contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * ps3_gpu: the RSX for 8 bit engines. See ps3_gpu.h.
 *
 * Put together from code proven on hardware:
 *  - video init, framebuffers, flip handler, flip sequence and the 30 fps
 *    pacing: TyrQuakeCell / Quake2PS3 ps3_video.c;
 *  - drawing (state, textures, vertex ring, fragment programs in RSX
 *    memory): Doom64-PS3 ps3_rsx.c and TyrQuakeCell's present pass;
 *  - vertex ring in the IO buffer (main memory): ioRTWC-PS3 (3.107).
 *
 * Rules learned the hard way (handoff notes):
 *  - rsxInit twice leaks the previous context: init is idempotent.
 *  - Shutdown: gcmSetFlipHandler(NULL) FIRST, then free.
 *  - rsxFinish with a new reference value every time (3.61).
 *  - Vertex blocks start 16 byte aligned (here 128: their own cache line,
 *    3.142), or the vertex fetch hangs the console (3.33).
 *  - CPU writes to RSX memory: flushed out of the data cache (dcbst) and
 *    the RSX's texture and vertex caches invalidated before use (3.139,
 *    3.141, 3.142).
 *  - A texture is freed only frames after its last use (3.55).
 *  - The clear honors the viewport: full screen viewport first (3.62).
 *  - rsxMemalign keeps its bookkeeping in RSX memory (3.145): textures
 *    come from one big block managed here, in main memory.
 *  - Never read RSX memory from the CPU.
 *  - Built at -O1 -fno-inline: PSL1GHT's syscall stubs don't survive
 *    aggressive inlining (4.2).
 *
 * =======================================================================
 */

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ppu-types.h>
#include <lv2/systime.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <sysutil/video.h>

#include "ps3_gpu.h"
#include "ps3_gpu_shaders.h"

#define FB_COUNT        3
#define FB_ALIGN        64
#define RSX_CB_SIZE     (1 * 1024 * 1024)            /* command buffer */
#define RING_SEGMENTS   FB_COUNT
#define RING_SEG_SIZE   (8 * 1024 * 1024)            /* vertices per frame */
#define RSX_IO_SIZE     (RSX_CB_SIZE + RING_SEGMENTS * RING_SEG_SIZE + 3 * 1024 * 1024)

#define SCREEN_MAX_W    1280                         /* the engine's screen */
#define SCREEN_MAX_H    960
#define LUT_ROWS        64

#define TEX_REMAP       0x00AAE4                     /* identity (ps3gl) */
#define FREE_DELAY      (FB_COUNT + 2)               /* frames */

static gcmContextData *ctx;
static void *io_buffer;
static int ready;

static u32 disp_w = 1280, disp_h = 720, disp_pitch;

static u32 *fb[FB_COUNT];
static u32 fb_offset[FB_COUNT];
static u32 *depth;
static u32 depth_offset;
static int cur;

static volatile u32 flip_queued, flip_completed;
static volatile u32 vblank_count;
static u32 last_flip_vblank;
static int lock30;
static u32 frame_no;
static int in_frame;

/* the engine's screen and where it goes on the TV */
static int scr_w = 320, scr_h = 200, scr_aw = 4, scr_ah = 3;
static int fit_percent = 90;
static int filter2d;
static int smooth;
static float pic_x, pic_y, pic_w, pic_h;

/* vertex ring, in the IO buffer */
static u8 *ring;
static u32 ring_offset;
static u32 ring_head;
static int ring_warned;

/* the engine's screen as a 32 bit image, one per framebuffer (RSX memory) */
static u32 *scr_img[FB_COUNT];
static u32 scr_img_offset[FB_COUNT];

/* colormap table, one per framebuffer (RSX memory) */
static u32 *lut_img[FB_COUNT];
static u32 lut_img_offset[FB_COUNT];
static u32 lut_version, lut_img_version[FB_COUNT];
static u8 palette[768];
static u8 colormaps[LUT_ROWS * 256];
static int colormap_rows;
static u32 pal32[256];

/* the matrix the vertex program has: GPU_DrawTris clips with it */
static float cur_mvp[16];

/* GPU_DrawTris' triangles after clipping */
static gpu_vert_t *clip_buf;
static int clip_cap;

/* programs */
static rsxVertexProgram *vp;
static void *vp_ucode;
static rsxProgramConst *vp_mvp, *vp_params, *vp_texinfo, *vp_params2;

enum { FP_WORLD, FP_SMOOTH, FP_RGBA, FP_COLOR, FP_COUNT };

static rsxFragmentProgram *fp[FP_COUNT];
static u32 fp_offset[FP_COUNT];
static void *fp_ucode[FP_COUNT];

/* the state the RSX has, valid inside a frame */
static int st_valid;
static int st_fp;
static gpu_tex_t *st_tex;
static int st_blend, st_dtest, st_dwrite, st_cwrite, st_stencil;
static float st_params[4], st_texinfo[4], st_params2[4];
static int st_params_ok, st_texinfo_ok, st_params2_ok;
static int lut_bound;

static int perf_log;
static u64 prof_wait_us, prof_lock_us;
static u32 prof_draws, prof_verts;

/* ================================================================ */
/* Cache helpers                                                     */
/* ================================================================ */

/* Pushes CPU writes in [p, p + bytes) out of the data cache: RSX memory
   mapped to the CPU is not coherent for writes (3.139). */
static void
FlushRange(const void *p, u32 bytes)
{
	uintptr_t a = (uintptr_t)p & ~(uintptr_t)127;
	uintptr_t end = (uintptr_t)p + bytes;

	for (; a < end; a += 128)
	{
		__asm__ volatile("dcbst 0,%0" : : "r"(a) : "memory");
	}

	__asm__ volatile("sync" ::: "memory");
}

/* ================================================================ */
/* Texture memory: one block, bookkeeping in main memory (3.145)     */
/* ================================================================ */

typedef struct vblock_s
{
	u32 off, size;
	int used;
	struct vblock_s *next;
} vblock_t;

static u8 *vram_base;
static u32 vram_base_offset;
static u32 vram_size, vram_used;
static vblock_t *vram_blocks;

static int
VramInit(void)
{
	static const u32 sizes[] = { 160, 128, 96, 64, 48, 32, 16 };
	unsigned i;

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
	{
		vram_base = (u8 *)rsxMemalign(128, sizes[i] << 20);

		if (vram_base)
		{
			vram_size = sizes[i] << 20;
			break;
		}
	}

	if (!vram_base)
	{
		PS3_GPU_Log("[gpu] FATAL: no RSX memory for textures");
		return -1;
	}

	rsxAddressToOffset(vram_base, &vram_base_offset);
	vram_blocks = (vblock_t *)calloc(1, sizeof(vblock_t));
	vram_blocks->size = vram_size;
	vram_used = 0;

	PS3_GPU_Log("[gpu] texture memory: %u MB", (unsigned)(vram_size >> 20));
	return 0;
}

static void *
VramAlloc(u32 bytes, u32 *offset)
{
	vblock_t *b, *best = NULL;

	bytes = (bytes + 127) & ~127u;

	for (b = vram_blocks; b; b = b->next)
	{
		if (!b->used && b->size >= bytes && (!best || b->size < best->size))
		{
			best = b;
		}
	}

	if (!best)
	{
		return NULL;
	}

	if (best->size > bytes)
	{
		vblock_t *rest = (vblock_t *)malloc(sizeof(vblock_t));

		if (!rest)
		{
			return NULL;
		}

		rest->off = best->off + bytes;
		rest->size = best->size - bytes;
		rest->used = 0;
		rest->next = best->next;
		best->next = rest;
		best->size = bytes;
	}

	best->used = 1;
	vram_used += best->size;
	*offset = vram_base_offset + best->off;

	return vram_base + best->off;
}

static void
VramFree(void *p)
{
	u32 off = (u32)((u8 *)p - vram_base);
	vblock_t *b, *prev = NULL;

	for (b = vram_blocks; b; prev = b, b = b->next)
	{
		if (b->off == off && b->used)
		{
			break;
		}
	}

	if (!b)
	{
		return;
	}

	b->used = 0;
	vram_used -= b->size;

	/* merge with the next and the previous free blocks */
	if (b->next && !b->next->used)
	{
		vblock_t *n = b->next;

		b->size += n->size;
		b->next = n->next;
		free(n);
	}

	if (prev && !prev->used)
	{
		prev->size += b->size;
		prev->next = b->next;
		free(b);
	}
}

/* ================================================================ */
/* Textures                                                          */
/* ================================================================ */

struct gpu_tex_s
{
	void *data;
	u32 offset;
	int w, h;
	int swz;
	gcmTexture gt;
	u32 last_frame;       /* last frame that drew with it */
	struct gpu_tex_s *next_free;
	u32 free_due;
};

static gpu_tex_t *pending_free;

static u32
SwzOffset(u32 x, u32 y, u32 lw, u32 lh)
{
	u32 off = 0, bit = 0;

	while (lw || lh)
	{
		if (lw)
		{
			off |= (x & 1u) << bit;
			x >>= 1;
			bit++;
			lw--;
		}

		if (lh)
		{
			off |= (y & 1u) << bit;
			y >>= 1;
			bit++;
			lh--;
		}
	}

	return off;
}

static u32
Log2(u32 v)
{
	u32 r = 0;

	while (v > 1)
	{
		v >>= 1;
		r++;
	}

	return r;
}

gpu_tex_t *
GPU_TexCreate(int w, int h, const uint8_t *index, const uint8_t *mask,
		const uint8_t *bright)
{
	gpu_tex_t *t;
	u32 pitch, bytes, x, y;
	u32 *dst;
	static int warned;

	if (!ready || w <= 0 || h <= 0 || w > 4096 || h > 4096)
	{
		return NULL;
	}

	t = (gpu_tex_t *)calloc(1, sizeof(*t));

	if (!t)
	{
		return NULL;
	}

	t->w = w;
	t->h = h;
	t->swz = ((w & (w - 1)) == 0 && (h & (h - 1)) == 0);
	pitch = t->swz ? (u32)w * 4 : (((u32)w * 4 + 63) & ~63u);
	bytes = pitch * (u32)h;
	t->data = VramAlloc(bytes, &t->offset);

	if (!t->data)
	{
		if (!warned)
		{
			PS3_GPU_Log("[gpu] out of texture memory (%u of %u KB in use)",
					(unsigned)(vram_used >> 10), (unsigned)(vram_size >> 10));
			warned = 1;
		}

		free(t);
		return NULL;
	}

	/* A8R8G8B8: A = coverage, R = palette index, G = full bright */
	dst = (u32 *)t->data;

	if (t->swz)
	{
		u32 lw = Log2((u32)w), lh = Log2((u32)h);

		for (y = 0; y < (u32)h; y++)
		{
			for (x = 0; x < (u32)w; x++)
			{
				u32 i = y * (u32)w + x;
				u32 a = (!mask || mask[i]) ? 0xff000000u : 0;
				u32 g = (bright && bright[i]) ? 0xff00u : 0;

				dst[SwzOffset(x, y, lw, lh)] = a | ((u32)index[i] << 16) | g;
			}
		}
	}
	else
	{
		for (y = 0; y < (u32)h; y++)
		{
			u32 *row = (u32 *)((u8 *)t->data + y * pitch);

			for (x = 0; x < (u32)w; x++)
			{
				u32 i = y * (u32)w + x;
				u32 a = (!mask || mask[i]) ? 0xff000000u : 0;
				u32 g = (bright && bright[i]) ? 0xff00u : 0;

				row[x] = a | ((u32)index[i] << 16) | g;
			}
		}
	}

	FlushRange(t->data, bytes);

	memset(&t->gt, 0, sizeof(t->gt));
	t->gt.format = GCM_TEXTURE_FORMAT_A8R8G8B8 |
		(t->swz ? GCM_TEXTURE_FORMAT_SWZ : GCM_TEXTURE_FORMAT_LIN);
	t->gt.mipmap = 1;
	t->gt.dimension = GCM_TEXTURE_DIMS_2D;
	t->gt.cubemap = GCM_FALSE;
	t->gt.remap = TEX_REMAP;
	t->gt.width = (u16)w;
	t->gt.height = (u16)h;
	t->gt.depth = 1;
	t->gt.location = GCM_LOCATION_RSX;
	t->gt.pitch = t->swz ? 0 : pitch;
	t->gt.offset = t->offset;

	return t;
}

void
GPU_TexFree(gpu_tex_t *t)
{
	if (!t)
	{
		return;
	}

	if (st_tex == t)
	{
		st_tex = NULL;
		st_valid = 0;
	}

	/* the RSX may still draw frames that use it */
	t->free_due = frame_no + FREE_DELAY;
	t->next_free = pending_free;
	pending_free = t;
}

static void
ProcessFrees(int all)
{
	gpu_tex_t **pp = &pending_free;

	while (*pp)
	{
		gpu_tex_t *t = *pp;

		if (all || (int)(frame_no - t->free_due) >= 0)
		{
			*pp = t->next_free;
			VramFree(t->data);
			free(t);
		}
		else
		{
			pp = &t->next_free;
		}
	}
}

int
GPU_TexWidth(const gpu_tex_t *t)
{
	return t ? t->w : 0;
}

int
GPU_TexHeight(const gpu_tex_t *t)
{
	return t ? t->h : 0;
}

unsigned
GPU_TexMemUsed(void)
{
	return vram_used;
}

unsigned
GPU_TexMemTotal(void)
{
	return vram_size;
}

/* ================================================================ */
/* Display                                                           */
/* ================================================================ */

static void
FlipHandler(const u32 head)
{
	(void)head;
	flip_completed++;
}

static void
VBlankHandler(const u32 head)
{
	(void)head;
	vblank_count++;
}

/* Blocks while both framebuffers that are not being built are still in
   flight. Afterwards the one at 'cur' (and everything of its slot: the
   ring segment, the screen image, the colormap table) is free. */
static void
WaitFlips(void)
{
	int waited = 0;

	while ((int)(flip_queued - flip_completed) > FB_COUNT - 2)
	{
		usleep(100);

		if (++waited > 20000)
		{
			PS3_GPU_Log("[gpu] WARNING: flip fence timed out, force-sync");
			flip_completed = flip_queued;
			break;
		}
	}
}

void
GPU_Finish(void)
{
	static u32 ref = 0x1000;
	gcmControlRegister volatile *ctrl;
	int waited = 0;

	if (!ctx)
	{
		return;
	}

	ref++;

	if (ref == 0 || ref == 0xffffffffu)
	{
		ref = 0x1000;
	}

	/* rsxFinish(ctx, ref), with a time limit: on the way out after an
	   error the RSX may be the thing that's stuck (3.61) */
	rsxSetReferenceCommand(ctx, ref);
	rsxFlushBuffer(ctx);
	ctrl = gcmGetControlRegister();

	while (ctrl->ref != ref)
	{
		usleep(30);

		if (++waited > 100000)
		{
			PS3_GPU_Log("[gpu] WARNING: the RSX didn't finish in time (ref %u)", (unsigned)ref);
			break;
		}
	}
}

int
GPU_Ready(void)
{
	return ready;
}

int
GPU_Width(void)
{
	return (int)disp_w;
}

int
GPU_Height(void)
{
	return (int)disp_h;
}

static int
LoadPrograms(void)
{
	static const unsigned char *const fp_data[FP_COUNT] = {
		r3d_fp_world_fpo, r3d_fp_smooth_fpo, r3d_fp_rgba_fpo, r3d_fp_color_fpo
	};
	u32 size;
	void *ucode;
	int i;

	vp = (rsxVertexProgram *)r3d_vp_vpo;
	rsxVertexProgramGetUCode(vp, &vp_ucode, &size);
	vp_mvp = rsxVertexProgramGetConst(vp, "mvp");
	vp_params = rsxVertexProgramGetConst(vp, "params");
	vp_texinfo = rsxVertexProgramGetConst(vp, "texinfo");
	vp_params2 = rsxVertexProgramGetConst(vp, "params2");

	if (!vp_ucode || !vp_mvp || !vp_params || !vp_texinfo || !vp_params2)
	{
		PS3_GPU_Log("[gpu] FATAL: vertex program constants missing");
		return -1;
	}

	/* the fragment programs' microcode must be in RSX memory */
	for (i = 0; i < FP_COUNT; i++)
	{
		fp[i] = (rsxFragmentProgram *)fp_data[i];
		rsxFragmentProgramGetUCode(fp[i], &ucode, &size);
		fp_ucode[i] = rsxMemalign(64, size);

		if (!fp_ucode[i])
		{
			PS3_GPU_Log("[gpu] FATAL: no RSX memory for a fragment program");
			return -1;
		}

		memcpy(fp_ucode[i], ucode, size);
		FlushRange(fp_ucode[i], size);
		rsxAddressToOffset(fp_ucode[i], &fp_offset[i]);
	}

	return 0;
}

int
GPU_Init(void)
{
	s32 ret;
	s32 vid_res = VIDEO_RESOLUTION_720;
	u8 vid_aspect = VIDEO_ASPECT_16_9;
	videoResolution res;
	videoConfiguration vconfig;
	videoState state;
	u32 scr_pitch = SCREEN_MAX_W * 4;
	int i, waited;

	if (ready)
	{
		return 0;
	}

	PS3_GPU_Log("[gpu] init");

	io_buffer = memalign(1024 * 1024, RSX_IO_SIZE);

	if (!io_buffer)
	{
		PS3_GPU_Log("[gpu] FATAL: memalign failed for the %d MB IO buffer", RSX_IO_SIZE >> 20);
		return -1;
	}

	ret = rsxInit(&ctx, RSX_CB_SIZE, RSX_IO_SIZE, io_buffer);

	if (ret != 0 || !ctx)
	{
		PS3_GPU_Log("[gpu] FATAL: rsxInit failed (%d)", (int)ret);
		return -1;
	}

	/* 720p if the TV takes it; otherwise whatever the TV is set to */
	if (!videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720,
				VIDEO_ASPECT_16_9, 0))
	{
		videoGetState(0, 0, &state);
		vid_res = state.displayMode.resolution;
		vid_aspect = state.displayMode.aspect;
		PS3_GPU_Log("[gpu] 720p not available, using the TV's mode (%d)", (int)vid_res);
	}

	if (videoGetResolution(vid_res, &res) != 0)
	{
		PS3_GPU_Log("[gpu] FATAL: videoGetResolution failed");
		return -1;
	}

	disp_w = res.width;
	disp_h = res.height;
	disp_pitch = disp_w * 4;

	memset(&vconfig, 0, sizeof(vconfig));
	vconfig.resolution = vid_res;
	vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
	vconfig.pitch = disp_pitch;
	vconfig.aspect = vid_aspect;

	if (videoConfigure(0, &vconfig, NULL, 0) != 0)
	{
		PS3_GPU_Log("[gpu] FATAL: videoConfigure failed");
		return -1;
	}

	waited = 0;

	do
	{
		usleep(10000);

		if (videoGetState(0, 0, &state) != 0)
		{
			break;
		}
	}
	while (state.state == 3 && ++waited < 300);

	gcmSetFlipMode(GCM_FLIP_VSYNC);

	for (i = 0; i < FB_COUNT; i++)
	{
		fb[i] = (u32 *)rsxMemalign(FB_ALIGN, disp_pitch * disp_h);
		scr_img[i] = (u32 *)rsxMemalign(FB_ALIGN, scr_pitch * SCREEN_MAX_H);
		lut_img[i] = (u32 *)rsxMemalign(FB_ALIGN, 256 * 4 * LUT_ROWS);

		if (!fb[i] || !scr_img[i] || !lut_img[i])
		{
			PS3_GPU_Log("[gpu] FATAL: rsxMemalign failed for framebuffer %d", i);
			return -1;
		}

		memset(fb[i], 0, disp_pitch * disp_h);
		FlushRange(fb[i], disp_pitch * disp_h);
		rsxAddressToOffset(fb[i], &fb_offset[i]);
		rsxAddressToOffset(scr_img[i], &scr_img_offset[i]);
		rsxAddressToOffset(lut_img[i], &lut_img_offset[i]);
		gcmSetDisplayBuffer(i, fb_offset[i], disp_pitch, disp_w, disp_h);
		lut_img_version[i] = 0xffffffffu;
	}

	depth = (u32 *)rsxMemalign(FB_ALIGN, disp_pitch * disp_h);

	if (!depth)
	{
		PS3_GPU_Log("[gpu] FATAL: rsxMemalign failed for the depth buffer");
		return -1;
	}

	rsxAddressToOffset(depth, &depth_offset);

	/* the vertex ring: the IO buffer after the command buffer (main
	   memory the RSX reads, 3.107) */
	ring = (u8 *)io_buffer + RSX_CB_SIZE;

	if (rsxAddressToOffset(ring, &ring_offset) != 0)
	{
		PS3_GPU_Log("[gpu] FATAL: the vertex ring has no RSX offset");
		return -1;
	}

	if (LoadPrograms() != 0 || VramInit() != 0)
	{
		return -1;
	}

	cur = 0;
	flip_queued = 0;
	flip_completed = 0;
	lut_version = 1;

	gcmSetFlipHandler(FlipHandler);
	gcmSetVBlankHandler(VBlankHandler);

	ready = 1;
	GPU_SetScreen(scr_w, scr_h, scr_aw, scr_ah);
	PS3_GPU_Log("[gpu] ready: %ux%u, %d framebuffers, vertex ring %d x %d MB",
			(unsigned)disp_w, (unsigned)disp_h, FB_COUNT, RING_SEGMENTS, RING_SEG_SIZE >> 20);

	return 0;
}

void
GPU_Shutdown(void)
{
	int i;

	if (!ready)
	{
		return;
	}

	PS3_GPU_Log("[gpu] shutdown");

	/* ORDER MATTERS: unhook the handlers first */
	gcmSetFlipHandler(NULL);
	gcmSetVBlankHandler(NULL);
	GPU_Finish();

	ProcessFrees(1);

	for (i = 0; i < FB_COUNT; i++)
	{
		if (fb[i]) rsxFree(fb[i]);
		if (scr_img[i]) rsxFree(scr_img[i]);
		if (lut_img[i]) rsxFree(lut_img[i]);
		fb[i] = scr_img[i] = lut_img[i] = NULL;
	}

	if (depth)
	{
		rsxFree(depth);
		depth = NULL;
	}

	for (i = 0; i < FP_COUNT; i++)
	{
		if (fp_ucode[i])
		{
			rsxFree(fp_ucode[i]);
			fp_ucode[i] = NULL;
		}
	}

	if (vram_base)
	{
		rsxFree(vram_base);
		vram_base = NULL;
	}

	while (vram_blocks)
	{
		vblock_t *n = vram_blocks->next;

		free(vram_blocks);
		vram_blocks = n;
	}

	ready = 0;
}

/* ================================================================ */
/* Settings                                                          */
/* ================================================================ */

static void
PlacePicture(void)
{
	float avail_w = disp_w * fit_percent / 100.0f;
	float avail_h = disp_h * fit_percent / 100.0f;
	float w, h;

	/* the engine's aspect, in the middle of the screen fit area */
	h = avail_h;
	w = h * scr_aw / scr_ah;

	if (w > avail_w)
	{
		w = avail_w;
		h = w * scr_ah / scr_aw;
	}

	/* whole pixels: the 2D screen and the 3D view must line up */
	pic_w = (float)(int)(w + 0.5f);
	pic_h = (float)(int)(h + 0.5f);
	pic_x = (float)(int)((disp_w - pic_w) / 2);
	pic_y = (float)(int)((disp_h - pic_h) / 2);
}

void
GPU_SetScreen(int w, int h, int aw, int ah)
{
	if (w < 1 || h < 1 || w > SCREEN_MAX_W || h > SCREEN_MAX_H || aw < 1 || ah < 1)
	{
		PS3_GPU_Log("[gpu] bad screen %dx%d (%d:%d)", w, h, aw, ah);
		return;
	}

	scr_w = w;
	scr_h = h;
	scr_aw = aw;
	scr_ah = ah;
	PlacePicture();
}

void
GPU_SetFit(int percent)
{
	if (percent < 50) percent = 50;
	if (percent > 100) percent = 100;

	fit_percent = percent;
	PlacePicture();
}

void
GPU_SetFilter2D(int linear)
{
	filter2d = linear ? 1 : 0;
}

void
GPU_SetSmooth(int on)
{
	smooth = on ? 1 : 0;
}

void
GPU_SetLock30(int on)
{
	lock30 = on ? 1 : 0;
}

void
GPU_SetPerfLog(int on)
{
	perf_log = on ? 1 : 0;
}

/* ================================================================ */
/* Palette and colormaps                                             */
/* ================================================================ */

static void
Rebuild32(void)
{
	int i;

	for (i = 0; i < 256; i++)
	{
		pal32[i] = 0xff000000u | ((u32)palette[i * 3] << 16) |
			((u32)palette[i * 3 + 1] << 8) | palette[i * 3 + 2];
	}
}

void
GPU_SetPalette(const uint8_t *rgb)
{
	if (memcmp(palette, rgb, 768) != 0)
	{
		memcpy(palette, rgb, 768);
		Rebuild32();
		lut_version++;
	}
}

void
GPU_SetColormaps(const uint8_t *maps, int rows)
{
	if (rows > LUT_ROWS)
	{
		rows = LUT_ROWS;
	}

	memcpy(colormaps, maps, (size_t)rows * 256);
	colormap_rows = rows;
	lut_version++;
}

/* this frame's colormap table, rebuilt if the palette or the colormaps
   changed since it was last written */
static void
UpdateLut(void)
{
	u32 *dst = lut_img[cur];
	int r, i;

	if (lut_img_version[cur] == lut_version)
	{
		return;
	}

	for (r = 0; r < LUT_ROWS; r++)
	{
		const u8 *map = colormaps + (r < colormap_rows ? r : (colormap_rows ? colormap_rows - 1 : 0)) * 256;

		for (i = 0; i < 256; i++)
		{
			dst[r * 256 + i] = colormap_rows ? pal32[map[i]] : pal32[i];
		}
	}

	FlushRange(dst, 256 * 4 * LUT_ROWS);
	lut_img_version[cur] = lut_version;
}

/* ================================================================ */
/* Drawing                                                           */
/* ================================================================ */

/*
 * Clipping
 *
 * The RSX doesn't cut triangles at the edges of the view: one with a
 * corner behind the eye, or very far off the side of the screen, came out
 * wrong or not at all (walls and floors that pass by the player vanished
 * at some angles, the rooms behind them showing). Every triangle is cut
 * here first, in clip space, against the near plane and against sides at
 * twice the view's size; the RSX only has the rest of the way to the
 * screen's edges to do.
 */

#define CLIP_GUARD 2.0f
#define CLIP_PLANES 5

typedef struct
{
	float c[4];           /* clip position */
	gpu_vert_t v;
} clipvert_t;

static float
ClipDist(const float *c, int plane)
{
	switch (plane)
	{
		case 0: return c[2] + c[3];               /* near: z >= -w */
		case 1: return CLIP_GUARD * c[3] + c[0];
		case 2: return CLIP_GUARD * c[3] - c[0];
		case 3: return CLIP_GUARD * c[3] + c[1];
		default: return CLIP_GUARD * c[3] - c[1];
	}
}

static int
OutCode(const float *c)
{
	int plane, code = 0;

	for (plane = 0; plane < CLIP_PLANES; plane++)
	{
		if (ClipDist(c, plane) < 0.0f)
		{
			code |= 1 << plane;
		}
	}

	return code;
}

static void
ToClip(const gpu_vert_t *v, float *c)
{
	const float *m = cur_mvp;
	int r;

	for (r = 0; r < 4; r++)
	{
		c[r] = m[r * 4] * v->x + m[r * 4 + 1] * v->y + m[r * 4 + 2] * v->z + m[r * 4 + 3] * v->w;
	}
}

static u32
LerpByte(u32 a, u32 b, int shift, float t)
{
	float x = (float)((a >> shift) & 0xff), y = (float)((b >> shift) & 0xff);

	return ((u32)(x + (y - x) * t + 0.5f) & 0xff) << shift;
}

static void
LerpVert(clipvert_t *o, const clipvert_t *a, const clipvert_t *b, float t)
{
	int i;

	for (i = 0; i < 4; i++)
	{
		o->c[i] = a->c[i] + (b->c[i] - a->c[i]) * t;
	}

#define L(f) o->v.f = a->v.f + (b->v.f - a->v.f) * t
	L(x); L(y); L(z); L(w);
	L(u); L(v);
	L(light); L(maxrow);
#undef L
	o->v.color = LerpByte(a->v.color, b->v.color, 24, t) |
		LerpByte(a->v.color, b->v.color, 16, t) |
		LerpByte(a->v.color, b->v.color, 8, t) |
		LerpByte(a->v.color, b->v.color, 0, t);
}

/* One triangle, cut against the planes its corners are outside of; the
   pieces go to out (up to 6 triangles). The number of vertices written. */
static int
ClipTriangle(const gpu_vert_t *tri, gpu_vert_t *out)
{
	clipvert_t buf[2][CLIP_PLANES + 3];
	clipvert_t *in = buf[0], *res = buf[1], *tmp;
	int n = 3, codes = 0, all = ~0, plane, i;

	for (i = 0; i < 3; i++)
	{
		int code;

		in[i].v = tri[i];
		ToClip(&tri[i], in[i].c);
		code = OutCode(in[i].c);
		codes |= code;
		all &= code;
	}

	if (!codes)
	{
		memcpy(out, tri, 3 * sizeof(gpu_vert_t));
		return 3;
	}

	if (all)
	{
		return 0;   /* wholly outside one plane */
	}

	for (plane = 0; plane < CLIP_PLANES && n >= 3; plane++)
	{
		int m = 0;

		if (!(codes & (1 << plane)))
		{
			continue;
		}

		for (i = 0; i < n; i++)
		{
			const clipvert_t *a = &in[i], *b = &in[(i + 1) % n];
			float da = ClipDist(a->c, plane), db = ClipDist(b->c, plane);

			if (da >= 0.0f)
			{
				res[m++] = *a;
			}

			if ((da >= 0.0f) != (db >= 0.0f))
			{
				LerpVert(&res[m++], a, b, da / (da - db));
			}
		}

		tmp = in;
		in = res;
		res = tmp;
		n = m;
	}

	for (i = 1; i + 1 < n; i++)
	{
		out[(i - 1) * 3] = in[0].v;
		out[(i - 1) * 3 + 1] = in[i].v;
		out[(i - 1) * 3 + 2] = in[i + 1].v;
	}

	return n >= 3 ? (n - 2) * 3 : 0;
}

/* The triangles of v, clipped, in clip_buf; how many vertices */
static int
ClipTriangles(const gpu_vert_t *v, int n)
{
	int t, out = 0;

	if (clip_cap < n * 6)
	{
		gpu_vert_t *b = (gpu_vert_t *)realloc(clip_buf, (size_t)n * 6 * sizeof(gpu_vert_t));

		if (!b)
		{
			return 0;
		}

		clip_buf = b;
		clip_cap = n * 6;
	}

	for (t = 0; t + 2 < n; t += 3)
	{
		out += ClipTriangle(v + t, clip_buf + out);
	}

	return out;
}

static gpu_vert_t *
RingAlloc(int n, u32 *offset)
{
	u32 bytes = (u32)n * (u32)sizeof(gpu_vert_t);
	u32 seg = (u32)cur * RING_SEG_SIZE;

	if (ring_head + bytes > RING_SEG_SIZE)
	{
		if (!ring_warned)
		{
			PS3_GPU_Log("[gpu] WARNING: vertex ring full, triangles dropped");
			ring_warned = 1;
		}

		return NULL;
	}

	*offset = ring_offset + seg + ring_head;

	{
		gpu_vert_t *v = (gpu_vert_t *)(ring + seg + ring_head);

		/* every block on its own 128 byte line (3.33, 3.142) */
		ring_head = (ring_head + bytes + 127u) & ~127u;
		return v;
	}
}

static void
BindVertices(u32 off)
{
	rsxInvalidateVertexCache(ctx);
	rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0, off + 0,
			sizeof(gpu_vert_t), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_CELL);
	rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0, off + 16,
			sizeof(gpu_vert_t), 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_CELL);
	rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX1, 0, off + 24,
			sizeof(gpu_vert_t), 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_CELL);
	rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0, off + 32,
			sizeof(gpu_vert_t), 4, GCM_VERTEX_DATA_TYPE_U8, GCM_LOCATION_CELL);
}

static void
SetViewportRect(float x, float y, float w, float h)
{
	float scale[4], offset[4];
	int ix = (int)x, iy = (int)y;
	int ix2 = (int)(x + w + 0.999f), iy2 = (int)(y + h + 0.999f);

	if (ix < 0) ix = 0;
	if (iy < 0) iy = 0;
	if (ix2 > (int)disp_w) ix2 = (int)disp_w;
	if (iy2 > (int)disp_h) iy2 = (int)disp_h;

	scale[0] = w * 0.5f;
	scale[1] = h * -0.5f;
	scale[2] = 0.5f;
	scale[3] = 0.0f;
	offset[0] = x + w * 0.5f;
	offset[1] = y + h * 0.5f;
	offset[2] = 0.5f;
	offset[3] = 0.0f;

	rsxSetViewport(ctx, (u16)ix, (u16)iy, (u16)(ix2 - ix), (u16)(iy2 - iy),
			0.0f, 1.0f, scale, offset);
	rsxSetViewportClip(ctx, 0, disp_w, disp_h);
	rsxSetScissor(ctx, (u16)ix, (u16)iy, (u16)(ix2 - ix), (u16)(iy2 - iy));
}

static void
SetFP(int which)
{
	if (st_valid && st_fp == which)
	{
		return;
	}

	rsxLoadFragmentProgramLocation(ctx, fp[which], fp_offset[which], GCM_LOCATION_RSX);
	st_fp = which;
}

static void
BindTexture(int unit, const gcmTexture *gt, int linear)
{
	u32 f = linear ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST;

	/* texture caches invalidated on every bind (3.141) */
	rsxInvalidateTextureCache(ctx, GCM_INVALIDATE_TEXTURE);
	rsxLoadTexture(ctx, unit, gt);
	rsxTextureControl(ctx, unit, GCM_TRUE, 0, 0, GCM_TEXTURE_MAX_ANISO_1);
	rsxTextureFilter(ctx, unit, 0, f, f, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
	rsxTextureWrapMode(ctx, unit, GCM_TEXTURE_CLAMP_TO_EDGE, GCM_TEXTURE_CLAMP_TO_EDGE,
			GCM_TEXTURE_CLAMP_TO_EDGE, GCM_TEXTURE_UNSIGNED_REMAP_NORMAL,
			GCM_TEXTURE_ZFUNC_NEVER, 0);
}

static void
BindLut(void)
{
	gcmTexture gt;

	if (lut_bound)
	{
		return;
	}

	UpdateLut();

	memset(&gt, 0, sizeof(gt));
	gt.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
	gt.mipmap = 1;
	gt.dimension = GCM_TEXTURE_DIMS_2D;
	gt.cubemap = GCM_FALSE;
	gt.remap = TEX_REMAP;
	gt.width = 256;
	gt.height = LUT_ROWS;
	gt.depth = 1;
	gt.location = GCM_LOCATION_RSX;
	gt.pitch = 256 * 4;
	gt.offset = lut_img_offset[cur];

	BindTexture(1, &gt, 0);
	lut_bound = 1;
}

static void
SetBlend(int blend)
{
	if (st_valid && st_blend == blend)
	{
		return;
	}

	if (blend == GPU_BLEND_NONE)
	{
		rsxSetBlendEnable(ctx, GCM_FALSE);
	}
	else
	{
		u32 dst = blend == GPU_BLEND_ADD ? GCM_ONE : GCM_ONE_MINUS_SRC_ALPHA;

		rsxSetBlendEnable(ctx, GCM_TRUE);
		rsxSetBlendFunc(ctx, GCM_SRC_ALPHA, dst, GCM_SRC_ALPHA, dst);
		rsxSetBlendEquation(ctx, GCM_FUNC_ADD, GCM_FUNC_ADD);
	}

	st_blend = blend;
}

static void
SetDepth(int test, int write)
{
	if (!st_valid || st_dtest != test)
	{
		rsxSetDepthTestEnable(ctx, test ? GCM_TRUE : GCM_FALSE);

		if (test)
		{
			rsxSetDepthFunc(ctx, GCM_LEQUAL);
		}

		st_dtest = test;
	}

	if (!st_valid || st_dwrite != write)
	{
		rsxSetDepthWriteEnable(ctx, write ? GCM_TRUE : GCM_FALSE);
		st_dwrite = write;
	}
}

static void
SetColorWrite(int on)
{
	if (st_valid && st_cwrite == on)
	{
		return;
	}

	rsxSetColorMask(ctx, on ? (GCM_COLOR_MASK_R | GCM_COLOR_MASK_G |
				GCM_COLOR_MASK_B | GCM_COLOR_MASK_A) : 0);
	st_cwrite = on;
}

static void
SetStencil(int mode)
{
	if (st_valid && st_stencil == mode)
	{
		return;
	}

	if (mode == GPU_STENCIL_OFF)
	{
		rsxSetStencilTestEnable(ctx, GCM_FALSE);
	}
	else if (mode == GPU_STENCIL_WRITE)
	{
		rsxSetStencilTestEnable(ctx, GCM_TRUE);
		rsxSetStencilMask(ctx, 0xff);
		rsxSetStencilFunc(ctx, GCM_ALWAYS, 1, 0xff);
		rsxSetStencilOp(ctx, GCM_KEEP, GCM_KEEP, GCM_REPLACE);
	}
	else
	{
		rsxSetStencilTestEnable(ctx, GCM_TRUE);
		rsxSetStencilMask(ctx, 0);
		rsxSetStencilFunc(ctx, GCM_EQUAL, 1, 0xff);
		rsxSetStencilOp(ctx, GCM_KEEP, GCM_KEEP, GCM_KEEP);
	}

	st_stencil = mode;
}

static void
SetMatrixRaw(const float *m)
{
	float t[16] __attribute__((aligned(16)));

	memcpy(t, m, sizeof(t));
	memcpy(cur_mvp, m, sizeof(cur_mvp));
	rsxSetVertexProgramParameter(ctx, vp, vp_mvp, t);
}

static void
SetVec(rsxProgramConst *c, float *cache, int *ok, const float *v)
{
	float t[4] __attribute__((aligned(16)));

	if (*ok && memcmp(cache, v, sizeof(float) * 4) == 0)
	{
		return;
	}

	*ok = 1;
	memcpy(cache, v, sizeof(float) * 4);
	memcpy(t, v, sizeof(t));
	rsxSetVertexProgramParameter(ctx, vp, c, t);
}

void
GPU_BeginFrame(void)
{
	gcmSurface sf;
	int i;
	u64 t0;

	if (!ready)
	{
		return;
	}

	t0 = sysGetSystemTime();
	WaitFlips();
	prof_wait_us += sysGetSystemTime() - t0;

	frame_no++;
	ProcessFrees(0);
	ring_head = 0;
	in_frame = 1;
	st_valid = 0;
	lut_bound = 0;
	st_tex = NULL;

	memset(&sf, 0, sizeof(sf));
	sf.colorFormat = GCM_SURFACE_A8R8G8B8;
	sf.colorTarget = GCM_SURFACE_TARGET_0;
	sf.colorLocation[0] = GCM_LOCATION_RSX;
	sf.colorOffset[0] = fb_offset[cur];
	sf.colorPitch[0] = disp_pitch;

	for (i = 1; i < 4; i++)
	{
		sf.colorLocation[i] = GCM_LOCATION_RSX;
		sf.colorOffset[i] = fb_offset[cur];
		sf.colorPitch[i] = 64;
	}

	sf.depthFormat = GCM_SURFACE_ZETA_Z24S8;
	sf.depthLocation = GCM_LOCATION_RSX;
	sf.depthOffset = depth_offset;
	sf.depthPitch = disp_pitch;
	sf.type = GCM_SURFACE_TYPE_LINEAR;
	sf.antiAlias = GCM_SURFACE_CENTER_1;
	sf.width = (u16)disp_w;
	sf.height = (u16)disp_h;
	sf.x = 0;
	sf.y = 0;

	rsxSetSurface(ctx, &sf);

	/* the whole screen: black, depth at the far end (the clear honors the
	   viewport, 3.62) */
	SetViewportRect(0.0f, 0.0f, (float)disp_w, (float)disp_h);
	rsxSetColorMask(ctx, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G |
			GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
	rsxSetClearColor(ctx, 0xff000000u);
	rsxSetClearDepthStencil(ctx, 0xffffff00u);
	rsxClearSurface(ctx, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A |
			GCM_CLEAR_Z | GCM_CLEAR_S);

	/* plain state, all of it sent again every frame (3.141) */
	rsxSetCullFaceEnable(ctx, GCM_FALSE);
	rsxSetShadeModel(ctx, GCM_SHADE_MODEL_SMOOTH);
	rsxSetAlphaTestEnable(ctx, GCM_FALSE);
	rsxSetStencilTestEnable(ctx, GCM_FALSE);
	rsxSetPolygonOffsetFillEnable(ctx, GCM_FALSE);
	rsxSetBlendEnable(ctx, GCM_FALSE);
	rsxSetDepthTestEnable(ctx, GCM_FALSE);
	rsxSetDepthWriteEnable(ctx, GCM_FALSE);
	rsxLoadVertexProgram(ctx, vp, vp_ucode);
	rsxTextureControl(ctx, 0, GCM_FALSE, 0, 0, 0);
	rsxTextureControl(ctx, 1, GCM_FALSE, 0, 0, 0);

	st_blend = GPU_BLEND_NONE;
	st_dtest = 0;
	st_dwrite = 0;
	st_cwrite = 1;
	st_stencil = GPU_STENCIL_OFF;
	st_fp = -1;
	st_params_ok = 0;
	st_texinfo_ok = 0;
	st_params2_ok = 0;
	SetFP(FP_COLOR);
	st_valid = 1;
}

void
GPU_Begin3D(int x, int y, int w, int h)
{
	float sx, sy;

	if (!ready || !in_frame)
	{
		return;
	}

	sx = pic_w / scr_w;
	sy = pic_h / scr_h;

	SetViewportRect(pic_x + x * sx, pic_y + y * sy, w * sx, h * sy);

	/* depth and stencil of the window start clear (the clear honors the
	   viewport and the scissor, 3.62) */
	SetDepth(1, 1);
	rsxSetClearDepthStencil(ctx, 0xffffff00u);
	rsxClearSurface(ctx, GCM_CLEAR_Z | GCM_CLEAR_S);
}

void
GPU_SetMatrix(const float *m)
{
	if (!ready || !in_frame)
	{
		return;
	}

	SetMatrixRaw(m);
}

void
GPU_DrawTris(const gpu_draw_t *d, const gpu_vert_t *v, int n)
{
	gpu_vert_t *dst;
	u32 off;
	float params[4], texinfo[4], params2[4];

	if (!ready || !in_frame || n < 3)
	{
		return;
	}

	{
		int clipped = ClipTriangles(v, n);

		if (clipped < 3)
		{
			return;
		}

		dst = RingAlloc(clipped, &off);

		if (!dst)
		{
			return;
		}

		memcpy(dst, clip_buf, (size_t)clipped * sizeof(gpu_vert_t));
		n = clipped;
	}

	SetBlend(d->blend);
	SetDepth(d->depth_test, d->depth_write);
	SetColorWrite(d->color_write);
	SetStencil(d->stencil);

	if (d->prog == GPU_PROG_WORLD && d->tex)
	{
		SetFP(smooth ? FP_SMOOTH : FP_WORLD);
		BindLut();

		if (st_tex != d->tex)
		{
			BindTexture(0, &d->tex->gt, 0);
			st_tex = d->tex;
		}

		d->tex->last_frame = frame_no;

		params[0] = d->fixed_row;
		params[1] = d->vis;
		params[2] = d->cap;
		params[3] = d->wrap ? 1.0f : 0.0f;
		texinfo[0] = (float)d->tex->w;
		texinfo[1] = (float)d->tex->h;
		texinfo[2] = 1.0f / d->tex->w;
		texinfo[3] = 1.0f / d->tex->h;
		params2[0] = d->zstep;
		params2[1] = d->ecwolf_light ? 1.0f : 0.0f;
		params2[2] = d->minvis;
		params2[3] = 0.0f;
		SetVec(vp_params, st_params, &st_params_ok, params);
		SetVec(vp_texinfo, st_texinfo, &st_texinfo_ok, texinfo);
		SetVec(vp_params2, st_params2, &st_params2_ok, params2);
	}
	else
	{
		SetFP(FP_COLOR);
	}

	BindVertices(off);
	rsxDrawVertexArray(ctx, GCM_TYPE_TRIANGLES, 0, (u32)n);

	prof_draws++;
	prof_verts += (u32)n;
}

void
GPU_End3D(void)
{
	if (!ready || !in_frame)
	{
		return;
	}

	SetDepth(0, 0);
	SetColorWrite(1);
	SetStencil(GPU_STENCIL_OFF);
	SetBlend(GPU_BLEND_NONE);
}

static void
ScreenQuad(int prog, u32 color, float u1, float v1)
{
	static const float identity[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};
	static const float corner[6][4] = {
		/* x, y, u, v: two triangles */
		{-1.0f, 1.0f, 0.0f, 0.0f},
		{1.0f, 1.0f, 1.0f, 0.0f},
		{1.0f, -1.0f, 1.0f, 1.0f},
		{-1.0f, 1.0f, 0.0f, 0.0f},
		{1.0f, -1.0f, 1.0f, 1.0f},
		{-1.0f, -1.0f, 0.0f, 1.0f},
	};
	gpu_vert_t *v;
	u32 off;
	int i;

	v = RingAlloc(6, &off);

	if (!v)
	{
		return;
	}

	for (i = 0; i < 6; i++)
	{
		v[i].x = corner[i][0];
		v[i].y = corner[i][1];
		v[i].z = 0.0f;
		v[i].w = 1.0f;
		v[i].u = corner[i][2] * u1;
		v[i].v = corner[i][3] * v1;
		v[i].light = 0.0f;
		v[i].maxrow = 0.0f;
		v[i].color = color;
	}

	SetViewportRect(pic_x, pic_y, pic_w, pic_h);
	SetMatrixRaw(identity);
	SetDepth(0, 0);
	SetColorWrite(1);
	SetStencil(GPU_STENCIL_OFF);
	SetFP(prog);
	BindVertices(off);
	rsxDrawVertexArray(ctx, GCM_TYPE_TRIANGLES, 0, 6);
}

void
GPU_DrawScreen(const uint8_t *pixels, int key, int kx, int ky, int kw, int kh)
{
	u32 pitch = SCREEN_MAX_W * 4;
	u32 *img;
	gcmTexture gt;
	int x, y;

	if (!ready || !in_frame)
	{
		return;
	}

	img = scr_img[cur];

	/* through the palette; key pixels inside the view window stay out */
	for (y = 0; y < scr_h; y++)
	{
		const u8 *s = pixels + y * scr_w;
		u32 *d = (u32 *)((u8 *)img + y * pitch);

		if (key >= 0 && y >= ky && y < ky + kh)
		{
			for (x = 0; x < scr_w; x++)
			{
				u32 c = pal32[s[x]];

				if (s[x] == key && x >= kx && x < kx + kw)
				{
					c = 0;
				}

				d[x] = c;
			}
		}
		else
		{
			for (x = 0; x < scr_w; x++)
			{
				d[x] = pal32[s[x]];
			}
		}

		FlushRange(d, (u32)scr_w * 4);
	}

	memset(&gt, 0, sizeof(gt));
	gt.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
	gt.mipmap = 1;
	gt.dimension = GCM_TEXTURE_DIMS_2D;
	gt.cubemap = GCM_FALSE;
	gt.remap = TEX_REMAP;
	gt.width = (u16)scr_w;
	gt.height = (u16)scr_h;
	gt.depth = 1;
	gt.location = GCM_LOCATION_RSX;
	gt.pitch = pitch;
	gt.offset = scr_img_offset[cur];

	BindTexture(0, &gt, filter2d && key < 0);
	st_tex = NULL;
	SetBlend(GPU_BLEND_NONE);
	ScreenQuad(FP_RGBA, 0xffffffffu, 1.0f, 1.0f);
}

void
GPU_DrawScreen32(const uint32_t *argb)
{
	u32 pitch = SCREEN_MAX_W * 4;
	u32 *img;
	gcmTexture gt;
	int y;

	if (!ready || !in_frame)
	{
		return;
	}

	img = scr_img[cur];

	for (y = 0; y < scr_h; y++)
	{
		u32 *d = (u32 *)((u8 *)img + y * pitch);

		memcpy(d, argb + y * scr_w, (size_t)scr_w * 4);
		FlushRange(d, (u32)scr_w * 4);
	}

	memset(&gt, 0, sizeof(gt));
	gt.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
	gt.mipmap = 1;
	gt.dimension = GCM_TEXTURE_DIMS_2D;
	gt.cubemap = GCM_FALSE;
	gt.remap = TEX_REMAP;
	gt.width = (u16)scr_w;
	gt.height = (u16)scr_h;
	gt.depth = 1;
	gt.location = GCM_LOCATION_RSX;
	gt.pitch = pitch;
	gt.offset = scr_img_offset[cur];

	BindTexture(0, &gt, filter2d);
	st_tex = NULL;
	SetBlend(GPU_BLEND_NONE);
	ScreenQuad(FP_RGBA, 0xffffffffu, 1.0f, 1.0f);
}

void
GPU_Tint(uint32_t rgba)
{
	if (!ready || !in_frame || (rgba & 0xff) == 0)
	{
		return;
	}

	SetBlend(GPU_BLEND_ALPHA);
	ScreenQuad(FP_COLOR, rgba, 1.0f, 1.0f);
	SetBlend(GPU_BLEND_NONE);
}

void
GPU_EndFrame(void)
{
	static int frames;
	u64 t0;

	if (!ready || !in_frame)
	{
		return;
	}

	in_frame = 0;

	/* the vertex ring and the images were written by the CPU */
	__asm__ volatile("sync" ::: "memory");

	/* Locked 30: flip every second vblank. The flip below happens on the
	   vblank after it is queued, so queue it once 2 have gone by since the
	   previous one. */
	if (lock30)
	{
		int waited = 0;

		t0 = sysGetSystemTime();

		while ((u32)(vblank_count - last_flip_vblank) < 2 && ++waited < 1000)
		{
			usleep(100);
		}

		prof_lock_us += sysGetSystemTime() - t0;
	}

	last_flip_vblank = vblank_count;

	/* TyrQuakeCell's sequence: flip, flush, wait-flip */
	gcmSetFlip(ctx, cur);
	rsxFlushBuffer(ctx);
	gcmSetWaitFlip(ctx);

	flip_queued++;
	cur = (cur + 1) % FB_COUNT;

	if (frames < 3)
	{
		PS3_GPU_Log("[gpu] frame %d: screen %dx%d -> %.0fx%.0f at %.0f,%.0f", frames,
				scr_w, scr_h, pic_w, pic_h, pic_x, pic_y);
	}

	frames++;

	/* frame rate in the log every 10 seconds (not more often: every
	   write to the HDD flashes the activity icon) */
	{
		static u64 window_start;
		static int window_frames;
		u64 now = sysGetSystemTime();

		if (window_start == 0)
		{
			window_start = now;
		}

		window_frames++;

		if (now - window_start >= 10000000ull)
		{
			if (perf_log)
			{
				double ms = (double)(now - window_start) / 1000.0 / window_frames;

				PS3_GPU_Log("[gpu] %.1f fps%s: frame %.1f ms, wait %.1f, lock %.1f; "
						"%u draws, %u verts per frame; textures %u KB",
						1000.0 / ms, lock30 ? " (locked 30)" : "", ms,
						prof_wait_us / 1000.0 / window_frames,
						prof_lock_us / 1000.0 / window_frames,
						prof_draws / window_frames, prof_verts / window_frames,
						(unsigned)(vram_used >> 10));
			}

			window_start = now;
			window_frames = 0;
			prof_wait_us = prof_lock_us = 0;
			prof_draws = prof_verts = 0;
		}
	}
}
