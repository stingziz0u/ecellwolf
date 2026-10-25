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

void
PS3_Display_SetPalette(const uint8_t *rgb)
{
	if (GPU_Ready())
	{
		GPU_SetPalette(rgb);
	}
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

	/* the whole TV: the engine draws for the screen's aspect itself
	   (Display > Aspect) */
	GPU_SetScreen(w, h, GPU_Width(), GPU_Height());
	GPU_BeginFrame();
	GPU_DrawScreen(src, -1, 0, 0, 0, 0);
	GPU_EndFrame();

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
