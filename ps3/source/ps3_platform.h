/*
 * Copyright (C) 2026 the ECellWolf contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * What the PS3 layer's files (ps3/source) share (1.1).
 */

#ifndef PS3_PLATFORM_H
#define PS3_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PS3_USRDIR
#define PS3_USRDIR "/dev_hdd0/game/ECWOLF001/USRDIR"
#endif

/* ps3_log.c: USRDIR/ecwolf_log.txt, the engine's printf output included
   (stdout and stderr go there), the previous run kept as
   ecwolf_log.old.txt */
void PS3_Log_Open(void);
void PS3_Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* the XMB's callbacks (sysUtilCheckCallback) */
void PS3_Pump(void);

/* ps3_pad.c: the first DualShock 3 connected */
typedef struct
{
	uint16_t buttons;    /* high bit: left, ... low bit: L2 (SDL.h) */
	uint8_t axis[4];     /* left X, left Y, right X, right Y; 128 centered */
} PS3_PadState;

void PS3_Pad_Init(void);
void PS3_Pad_Read(PS3_PadState *state);

/* ps3_display.c: the engine's 8 bit screen on the TV, through the RSX */

/* The share of the TV the picture may use, in percent, for TVs that crop
   the edges (overscan): Display Options > TV Screen Fit, saved as
   Vid_PS3ScreenFit. 90 is what 1.0 had; 100 fills a monitor. */
#define PS3_SCREEN_FIT_MIN     70
#define PS3_SCREEN_FIT_MAX     100
#define PS3_SCREEN_FIT_STEP    2
#define PS3_SCREEN_FIT_DEFAULT 90

extern int ps3_screen_fit;
void PS3_Display_SetFit(int percent);
void PS3_Display_Init(void);
void PS3_Display_Shutdown(void);
void PS3_Display_Size(int *w, int *h);
void PS3_Display_SetPalette(const uint8_t *rgb);   /* 256 x r, g, b */
void PS3_Display_Present(const uint8_t *pixels, int pitch, int w, int h);

/* The GPU renderer's 3D view (r_gpu.cpp): BeginView opens the frame and
   the view window (in the sw x sh screen), the 3D goes in between;
   Present then puts the screen over it, leaving out the window's pixels
   of the key color (-1: none, no GPU renderer). A frame presented with
   no new view gets the last one again from the replay function, until
   DropView. */
int  PS3_Display_Key(void);
void PS3_Display_BeginView(int sw, int sh, int x, int y, int w, int h);
void PS3_Display_EndView(void);
void PS3_Display_SetReplay(void (*fn)(void));
void PS3_Display_DropView(void);
/* while on, a frame with no new view always gets the last one (fizzle
   fades); otherwise only while the key color still fills a good part
   of the window (not under a whole new screen) */
void PS3_Display_HoldView(int on);

/* ps3_gpu.c needs this one */
void PS3_GPU_Log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
