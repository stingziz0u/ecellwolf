/*
 * Copyright (C) 2026 the ECellWolf contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * The engine's screen on the TV (1.1): sdlvideo.cpp hands over its 8 bit
 * frame buffer and the palette (after gamma and flashes), and the RSX
 * (ps3_gpu.c) draws it through the palette, scaled to the TV -- no
 * conversion to 32 bits on the CPU, no SDL texture upload. As in 1.0 the
 * picture fills the TV, with a 5 % margin on each side (screen fit 90 %)
 * for TVs that crop the edges.
 *
 * =======================================================================
 */

#include <stdlib.h>
#include <string.h>

#ifdef __PPU__
#include <sysutil/sysutil.h>
#endif

#include "ps3_gpu.h"
#include "ps3_platform.h"

#define SCREEN_FIT 90

static uint8_t *packed;      /* the frame without the canvas' row padding */
static int packed_size;

/* The GPU renderer's 3D view (r_gpu.cpp): drawn into its place on the TV
   first, then the 8 bit screen goes over it with the view window's key
   color pixels left out. */
static int frame_open;
static int view_drawn, last_view;
static int view_x, view_y, view_w, view_h;
static int key = -1;
static int key_chosen;
static void (*replay)(void);
static int hold;

void
PS3_Display_Init(void)
{
	if (GPU_Init() != 0)
	{
		PS3_Log("[display] the RSX did not start");
		return;
	}

	GPU_SetFit(SCREEN_FIT);
	GPU_SetFilter2D(1);
}

void
PS3_Display_Shutdown(void)
{
	GPU_Shutdown();
	free(packed);
	packed = NULL;
	packed_size = 0;
	frame_open = 0;
}

void
PS3_Display_Size(int *w, int *h)
{
	if (!GPU_Ready())
	{
		PS3_Display_Init();
	}

	*w = GPU_Ready() ? GPU_Width() : 1280;
	*h = GPU_Ready() ? GPU_Height() : 720;
}

/* An index whose color another one repeats exactly: the key. Chosen once,
   from the first palette (flashes blend every color alike, so it stays a
   duplicate). */
static void
ChooseKey(const uint8_t *rgb)
{
	int i, j;

	key_chosen = 1;

	for (i = 255; i > 0 && key < 0; i--)
	{
		for (j = 0; j < 256; j++)
		{
			if (j != i && !memcmp(rgb + i * 3, rgb + j * 3, 3))
			{
				key = i;
				break;
			}
		}
	}

	if (key >= 0)
		PS3_Log("[display] key color index %d", key);
	else
		PS3_Log("[display] no repeated color in the palette: no GPU renderer");
}

void
PS3_Display_SetPalette(const uint8_t *rgb)
{
	if (!key_chosen)
	{
		ChooseKey(rgb);
	}

	if (GPU_Ready())
	{
		GPU_SetPalette(rgb);
	}
}

int
PS3_Display_Key(void)
{
	return key;
}

static void
OpenFrame(int w, int h)
{
	if (!frame_open)
	{
		/* the whole TV: the engine draws for the screen's aspect itself
		   (Display > Aspect) */
		GPU_SetScreen(w, h, GPU_Width(), GPU_Height());
		GPU_BeginFrame();
		frame_open = 1;
	}
}

void
PS3_Display_BeginView(int sw, int sh, int x, int y, int w, int h)
{
	OpenFrame(sw, sh);
	view_x = x;
	view_y = y;
	view_w = w;
	view_h = h;
	GPU_Begin3D(x, y, w, h);
}

void
PS3_Display_EndView(void)
{
	GPU_End3D();
	view_drawn = 1;
}

void
PS3_Display_SetReplay(void (*fn)(void))
{
	replay = fn;
}

void
PS3_Display_DropView(void)
{
	last_view = 0;
}

void
PS3_Display_HoldView(int on)
{
	hold = on;
}

/* Is the window still the last view under some 2D (PAUSED, a message)?
   Not after a whole new screen (a menu, the intermission): the key color
   is one that repeats another, so at most a few of its pixels there. */
static int
ViewStillThere(const uint8_t *src, int w, int h)
{
	int x, y, n = 0, need;

	if (hold)
	{
		return 1;
	}

	need = view_w * view_h / 64;

	for (y = view_y; y < view_y + view_h && y < h; y++)
	{
		const uint8_t *row = src + y * w;

		for (x = view_x; x < view_x + view_w && x < w; x++)
		{
			n += row[x] == key;
		}

		if (n >= need)
		{
			return 1;
		}
	}

	return 0;
}

void
PS3_Display_Present(const uint8_t *pixels, int pitch, int w, int h)
{
	const uint8_t *src = pixels;

	if (!GPU_Ready() || !pixels || w <= 0 || h <= 0)
	{
		return;
	}

	if (pitch != w)
	{
		int y;

		if (packed_size < w * h)
		{
			free(packed);
			packed = (uint8_t *)malloc(w * h);
			packed_size = packed ? w * h : 0;
			if (!packed)
			{
				return;
			}
		}

		for (y = 0; y < h; y++)
		{
			memcpy(packed + y * w, pixels + y * pitch, w);
		}

		src = packed;
	}

	OpenFrame(w, h);

	/* a frame without a new 3D view (paused, a message over the game):
	   the last one again, under the same key pixels */
	if (!view_drawn && last_view && replay && ViewStillThere(src, w, h))
	{
		GPU_Begin3D(view_x, view_y, view_w, view_h);
		replay();
		GPU_End3D();
		view_drawn = 1;
	}

	GPU_DrawScreen(src, view_drawn ? key : -1, view_x, view_y, view_w, view_h);
	GPU_EndFrame();
	frame_open = 0;
	last_view = view_drawn;
	view_drawn = 0;

#ifndef __PPU__
	{
		/* the host harness: screenshots */
		extern void PS3_Host_Frame(void);

		PS3_Host_Frame();
	}
#endif
}

void
PS3_Pump(void)
{
#ifdef __PPU__
	sysUtilCheckCallback();
#endif
}
