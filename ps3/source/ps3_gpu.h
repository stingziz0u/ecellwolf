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
 * ps3_gpu: the RSX for 8 bit engines (Doom, Heretic, Hexen, Strife,
 * Wolfenstein 3D, Rise of the Triad). The same file in the three ports.
 *
 * Owns the display: framebuffers, depth buffer, flips. A frame is
 *
 *     GPU_BeginFrame();
 *     GPU_Begin3D(view window); ...GPU_DrawTris...; GPU_End3D();   (GPU renderer)
 *     GPU_DrawScreen(the engine's 8 bit screen);
 *     GPU_EndFrame();
 *
 * The engine keeps drawing its 2D (status bar, menus, messages, automap)
 * into its own 8 bit screen as always. With the GPU renderer, the view
 * window of that screen is filled with a "key" color index before the
 * 2D is drawn over it, and GPU_DrawScreen leaves the key pixels inside
 * the window transparent: the 3D picture the RSX drew shows through.
 * With the software renderer the screen goes up whole: same path.
 *
 * World textures are palette indexes. The colormaps of the engine (light
 * levels, invulnerability...) go through the current palette into one
 * table the fragment programs read, so light and palette effects (damage
 * flash, gamma) look like the software renderer's.
 *
 * ps3_gpu_host.c implements this same interface with a small software
 * rasterizer, for the host test harness (screenshots without a PS3).
 *
 * =======================================================================
 */

#ifndef PS3_GPU_H
#define PS3_GPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the vertex: ps3gl's layout (36 bytes) */
typedef struct
{
	float x, y, z, w;     /* w: always 1 */
	float u, v;           /* texture coordinates, 0..1 = the texture */
	float light;          /* light of the surface, see gpu_draw_t */
	float maxrow;         /* the darkest colormap row light can reach */
	uint32_t color;       /* 0xRRGGBBAA (bytes R, G, B, A in memory) */
} gpu_vert_t;

#define GPU_RGBA(r, g, b, a) \
	(((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

typedef struct gpu_tex_s gpu_tex_t;

enum
{
	GPU_BLEND_NONE,
	GPU_BLEND_ALPHA,      /* src * a + dst * (1 - a) */
	GPU_BLEND_ADD         /* src * a + dst */
};

enum
{
	GPU_STENCIL_OFF,
	GPU_STENCIL_WRITE,    /* where it draws, stencil = 1 */
	GPU_STENCIL_TEST      /* draws only where stencil == 1 */
};

enum
{
	GPU_PROG_WORLD,       /* palette textures through the colormaps */
	GPU_PROG_COLOR        /* vertex color only (tex ignored) */
};

typedef struct
{
	gpu_tex_t *tex;
	int prog;             /* GPU_PROG_* */
	int blend;            /* GPU_BLEND_* */
	int depth_test;       /* LEQUAL */
	int depth_write;
	int color_write;      /* 0: depth only (sky surfaces) */
	int wrap;             /* texture coordinates repeat */
	int stencil;          /* GPU_STENCIL_* (the sky) */

	/* Light: colormap row = light - floor(min(vis / depth, cap)), clamped
	   to 0..maxrow; light and maxrow are per vertex, depth is the distance
	   along the view axis in world units. fixed_row >= 0 replaces it all. */
	float fixed_row;
	float vis;
	float cap;
	float zstep;          /* > 0: depth rounded up to steps of this first */
	/* ECWolf (wl_shade.h): row = floor(light - min(max(vis / depth, minvis),
	   cap)), the rounding after the subtraction */
	int ecwolf_light;
	float minvis;
} gpu_draw_t;

/* ---- display ---- */

int  GPU_Init(void);              /* 0 ok; idempotent */
void GPU_Shutdown(void);
int  GPU_Ready(void);
void GPU_Finish(void);            /* waits until the RSX is idle */
int  GPU_Width(void);             /* TV mode */
int  GPU_Height(void);

/* The engine's screen: w x h pixels shown with an aspect of aw:ah (Doom
   320x200 -> 4:3). It goes in the middle of the TV inside the screen fit
   area (percent, 50..100). */
void GPU_SetScreen(int w, int h, int aw, int ah);
void GPU_SetFit(int percent);
void GPU_SetFilter2D(int linear); /* the 2D screen: nearest or bilinear */
void GPU_SetSmooth(int on);       /* world textures filtered */
void GPU_SetLock30(int on);       /* at most 30 flips per second */

/* ---- palette ---- */

/* 768 bytes, already through the engine's gamma */
void GPU_SetPalette(const uint8_t *rgb);
/* rows x 256 palette indexes (rows <= 64): the engine's colormaps */
void GPU_SetColormaps(const uint8_t *maps, int rows);

/* ---- frame ---- */

void GPU_BeginFrame(void);
void GPU_EndFrame(void);          /* flip */

/* The engine's screen. key >= 0: pixels of that index inside the
   rectangle (screen pixels) are left out. */
void GPU_DrawScreen(const uint8_t *pixels, int key, int kx, int ky, int kw, int kh);

/* A 32 bit image (0xAARRGGBB, w x h = the screen set by GPU_SetScreen)
   instead: the launchers, error screens. */
void GPU_DrawScreen32(const uint32_t *argb);

/* Flat color over the whole picture (palette flashes in smooth mode). */
void GPU_Tint(uint32_t rgba);

/* ---- 3D ---- */

/* The view window, in the engine's screen pixels. Clears depth and
   stencil there. */
void GPU_Begin3D(int x, int y, int w, int h);
/* row-major, clip = m * (x, y, z, 1); clip y up, z -w..w */
void GPU_SetMatrix(const float *m);
void GPU_DrawTris(const gpu_draw_t *d, const gpu_vert_t *v, int n);
void GPU_End3D(void);

/* index: w x h palette indexes; mask: NULL (opaque) or w x h bytes,
   0 = hole; bright: NULL or w x h bytes, 1 = always full bright (Crispy's
   brightmaps). NULL if out of memory. */
gpu_tex_t *GPU_TexCreate(int w, int h, const uint8_t *index, const uint8_t *mask,
		const uint8_t *bright);
void GPU_TexFree(gpu_tex_t *t);   /* deferred: frames in flight may use it */
int  GPU_TexWidth(const gpu_tex_t *t);
int  GPU_TexHeight(const gpu_tex_t *t);
/* bytes of texture memory in use / available */
unsigned GPU_TexMemUsed(void);
unsigned GPU_TexMemTotal(void);

/* fps and timings in the log every 10 seconds */
void GPU_SetPerfLog(int on);

/* the engine's log */
extern void PS3_GPU_Log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
