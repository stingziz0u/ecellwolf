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
 * The "SDL 2" of ps3/compat/SDL.h (1.1, instead of SDL2_PSL1GHT): the
 * clock, mutexes, the DualShock 3 as a joystick, memory files and the
 * audio helpers. Video and sound are elsewhere (ps3_display.c,
 * sdl_mixer_ps3.c).
 *
 * The host harness builds this file without __PPU__: the pad is then
 * the script in ECW_HOST_PAD (ps3_host.c).
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "SDL.h"
#include "ps3_platform.h"

#ifdef __PPU__
#include <sys/systime.h>
#include <sys/mutex.h>
#include <io/pad.h>
#else
#include <pthread.h>
#include <time.h>
#endif

/* ================================================================ */
/* Init, errors, version                                              */
/* ================================================================ */

static Uint32 subsystems;

void
SDL_GetVersion(SDL_version *ver)
{
	SDL_VERSION(ver);
}

int
SDL_Init(Uint32 flags)
{
	return SDL_InitSubSystem(flags);
}

int
SDL_InitSubSystem(Uint32 flags)
{
	if ((flags & SDL_INIT_JOYSTICK) && !(subsystems & SDL_INIT_JOYSTICK))
	{
		PS3_Pad_Init();
	}

	subsystems |= flags;
	return 0;
}

Uint32
SDL_WasInit(Uint32 flags)
{
	return subsystems & (flags ? flags : ~0u);
}

void
SDL_QuitSubSystem(Uint32 flags)
{
	subsystems &= ~flags;
}

/* the XMB's "Quit Game" ends here (sdlvideo.cpp): let go of the audio
   port and the RSX, or the console goes down with the process */
void
SDL_Quit(void)
{
	extern void Mix_CloseAudio(void);

	Mix_CloseAudio();
	PS3_Display_Shutdown();
	subsystems = 0;
}

const char *
SDL_GetError(void)
{
	return "PS3";
}

SDL_bool
SDL_SetHint(const char *name, const char *value)
{
	(void)name;
	(void)value;
	return SDL_TRUE;
}

/* ================================================================ */
/* Time                                                               */
/* ================================================================ */

static Uint64
NowMicro(void)
{
#ifdef __PPU__
	return sysGetSystemTime();
#else
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (Uint64)ts.tv_sec * 1000000ull + (Uint64)ts.tv_nsec / 1000ull;
#endif
}

static Uint64 time_base;

Uint64
SDL_GetTicks64(void)
{
	Uint64 now = NowMicro();

	if (time_base == 0)
	{
		time_base = now;
	}

	return (now - time_base) / 1000ull;
}

Uint32
SDL_GetTicks(void)
{
	return (Uint32)SDL_GetTicks64();
}

void
SDL_Delay(Uint32 ms)
{
	usleep(ms * 1000);
}

/* ================================================================ */
/* Mutexes (recursive, as SDL's)                                      */
/* ================================================================ */

struct SDL_mutex
{
#ifdef __PPU__
	sys_mutex_t id;
#else
	pthread_mutex_t id;
#endif
};

SDL_mutex *
SDL_CreateMutex(void)
{
	SDL_mutex *m = (SDL_mutex *)calloc(1, sizeof(*m));
#ifdef __PPU__
	sys_mutex_attr_t attr;

	if (!m)
	{
		return NULL;
	}

	memset(&attr, 0, sizeof(attr));
	attr.attr_protocol = SYS_MUTEX_PROTOCOL_PRIO;
	attr.attr_recursive = SYS_MUTEX_ATTR_RECURSIVE;
	attr.attr_pshared = SYS_MUTEX_ATTR_NOT_PSHARED;
	attr.attr_adaptive = SYS_MUTEX_ATTR_NOT_ADAPTIVE;
	strcpy(attr.name, "sdlmtx");

	if (sysMutexCreate(&m->id, &attr) != 0)
	{
		PS3_Log("[sdl] sysMutexCreate failed");
		free(m);
		return NULL;
	}
#else
	pthread_mutexattr_t attr;

	if (!m)
	{
		return NULL;
	}

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m->id, &attr);
	pthread_mutexattr_destroy(&attr);
#endif
	return m;
}

int
SDL_LockMutex(SDL_mutex *m)
{
	if (!m)
	{
		return -1;
	}
#ifdef __PPU__
	return sysMutexLock(m->id, 0) == 0 ? 0 : -1;
#else
	return pthread_mutex_lock(&m->id);
#endif
}

int
SDL_UnlockMutex(SDL_mutex *m)
{
	if (!m)
	{
		return -1;
	}
#ifdef __PPU__
	return sysMutexUnlock(m->id) == 0 ? 0 : -1;
#else
	return pthread_mutex_unlock(&m->id);
#endif
}

void
SDL_DestroyMutex(SDL_mutex *m)
{
	if (!m)
	{
		return;
	}
#ifdef __PPU__
	sysMutexDestroy(m->id);
#else
	pthread_mutex_destroy(&m->id);
#endif
	free(m);
}

/* ================================================================ */
/* Video stubs (see SDL.h)                                            */
/* ================================================================ */

struct SDL_Window
{
	int w, h;
	Uint32 flags;
};

struct SDL_Renderer
{
	int unused;
};

struct SDL_Texture
{
	Uint32 format;
	int w, h;
};

int
SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode)
{
	(void)displayIndex;
	memset(mode, 0, sizeof(*mode));
	mode->format = SDL_PIXELFORMAT_ARGB8888;
	PS3_Display_Size(&mode->w, &mode->h);
	mode->refresh_rate = 60;
	return 0;
}

SDL_Window *
SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
	SDL_Window *win = (SDL_Window *)calloc(1, sizeof(*win));

	(void)title;
	(void)x;
	(void)y;

	if (win)
	{
		win->w = w;
		win->h = h;
		win->flags = flags | SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	PS3_Display_Init();
	return win;
}

void
SDL_DestroyWindow(SDL_Window *window)
{
	free(window);
}

Uint32
SDL_GetWindowFlags(SDL_Window *window)
{
	return window ? window->flags : 0;
}

void
SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
	if (w)
	{
		*w = window ? window->w : 0;
	}
	if (h)
	{
		*h = window ? window->h : 0;
	}
}

void
SDL_SetWindowSize(SDL_Window *window, int w, int h)
{
	(void)window;
	(void)w;
	(void)h;
}

int
SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags)
{
	(void)window;
	(void)flags;
	return 0;
}

SDL_Surface *
SDL_GetWindowSurface(SDL_Window *window)
{
	(void)window;
	return NULL;
}

int
SDL_UpdateWindowSurface(SDL_Window *window)
{
	(void)window;
	return 0;
}

SDL_Renderer *
SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
	static SDL_Renderer renderer;

	(void)window;
	(void)index;
	(void)flags;
	return &renderer;
}

void
SDL_DestroyRenderer(SDL_Renderer *renderer)
{
	(void)renderer;
}

int
SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	(void)renderer;
	(void)r;
	(void)g;
	(void)b;
	(void)a;
	return 0;
}

int
SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h)
{
	(void)renderer;
	(void)w;
	(void)h;
	return 0;
}

int
SDL_RenderClear(SDL_Renderer *renderer)
{
	(void)renderer;
	return 0;
}

int
SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
		const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
	(void)renderer;
	(void)texture;
	(void)srcrect;
	(void)dstrect;
	return 0;
}

void
SDL_RenderPresent(SDL_Renderer *renderer)
{
	(void)renderer;
}

SDL_Texture *
SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
	SDL_Texture *tex = (SDL_Texture *)calloc(1, sizeof(*tex));

	(void)renderer;
	(void)access;

	if (tex)
	{
		tex->format = format;
		tex->w = w;
		tex->h = h;
	}

	return tex;
}

int
SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access, int *w, int *h)
{
	if (!texture)
	{
		return -1;
	}
	if (format)
	{
		*format = texture->format;
	}
	if (access)
	{
		*access = SDL_TEXTUREACCESS_STREAMING;
	}
	if (w)
	{
		*w = texture->w;
	}
	if (h)
	{
		*h = texture->h;
	}
	return 0;
}

void
SDL_DestroyTexture(SDL_Texture *texture)
{
	free(texture);
}

int
SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
	(void)texture;
	(void)rect;
	(void)pixels;
	(void)pitch;
	return 0;
}

int
SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch)
{
	(void)texture;
	(void)rect;
	(void)pixels;
	(void)pitch;
	return -1;
}

void
SDL_UnlockTexture(SDL_Texture *texture)
{
	(void)texture;
}

int
SDL_LockSurface(SDL_Surface *surface)
{
	return surface ? 0 : -1;
}

void
SDL_UnlockSurface(SDL_Surface *surface)
{
	(void)surface;
}

int
SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int firstcolor, int ncolors)
{
	(void)palette;
	(void)colors;
	(void)firstcolor;
	(void)ncolors;
	return 0;
}

SDL_bool
SDL_PixelFormatEnumToMasks(Uint32 format, int *bpp, Uint32 *Rmask,
		Uint32 *Gmask, Uint32 *Bmask, Uint32 *Amask)
{
	*bpp = 32;
	*Amask = 0xff000000u;
	*Rmask = 0x00ff0000u;
	*Gmask = 0x0000ff00u;
	*Bmask = 0x000000ffu;

	if (format == SDL_PIXELFORMAT_INDEX8)
	{
		*bpp = 8;
		*Amask = *Rmask = *Gmask = *Bmask = 0;
	}

	return SDL_TRUE;
}

/* ================================================================ */
/* Keyboard and mouse: none                                           */
/* ================================================================ */

static SDL_Keymod mod_state;

SDL_Keymod
SDL_GetModState(void)
{
	return mod_state;
}

void
SDL_SetModState(SDL_Keymod modstate)
{
	mod_state = modstate;
}

Uint32
SDL_GetMouseState(int *x, int *y)
{
	if (x)
	{
		*x = 0;
	}
	if (y)
	{
		*y = 0;
	}
	return 0;
}

Uint32
SDL_GetRelativeMouseState(int *x, int *y)
{
	return SDL_GetMouseState(x, y);
}

int
SDL_SetRelativeMouseMode(SDL_bool enabled)
{
	(void)enabled;
	return 0;
}

void
SDL_WarpMouseInWindow(SDL_Window *window, int x, int y)
{
	(void)window;
	(void)x;
	(void)y;
}

int
SDL_ShowCursor(int toggle)
{
	(void)toggle;
	return 0;
}

/* ================================================================ */
/* The DualShock 3                                                    */
/* ================================================================ */

struct _SDL_Joystick
{
	int index;
};

static SDL_Joystick joystick;
static PS3_PadState pad;

int
SDL_NumJoysticks(void)
{
	return 1;   /* always one: a pad switched off comes back the same */
}

const char *
SDL_JoystickNameForIndex(int device_index)
{
	(void)device_index;
	return "DualShock 3";
}

SDL_Joystick *
SDL_JoystickOpen(int device_index)
{
	if (device_index != 0)
	{
		return NULL;
	}

	PS3_Pad_Init();
	return &joystick;
}

void
SDL_JoystickClose(SDL_Joystick *j)
{
	(void)j;
}

/* id_in.cpp reopens the joystick when it's detached; ours never is */
SDL_bool
SDL_JoystickGetAttached(SDL_Joystick *j)
{
	return j ? SDL_TRUE : SDL_FALSE;
}

int
SDL_JoystickEventState(int state)
{
	(void)state;
	return SDL_IGNORE;
}

void
SDL_JoystickUpdate(void)
{
	PS3_Pad_Read(&pad);
}

int
SDL_JoystickNumAxes(SDL_Joystick *j)
{
	(void)j;
	return 4;
}

int
SDL_JoystickNumButtons(SDL_Joystick *j)
{
	(void)j;
	return 16;
}

int
SDL_JoystickNumHats(SDL_Joystick *j)
{
	(void)j;
	return 0;
}

Sint16
SDL_JoystickGetAxis(SDL_Joystick *j, int axis)
{
	int v;

	(void)j;

	if (axis < 0 || axis >= 4)
	{
		return 0;
	}

	/* 0..255, 128 centered; as SDL2_PSL1GHT scaled it: 0 -> -32768,
	   255 -> 32767 */
	v = (((int)pad.axis[axis] - 0x80) << 8) | pad.axis[axis];
	return (Sint16)v;
}

Uint8
SDL_JoystickGetButton(SDL_Joystick *j, int button)
{
	(void)j;

	if (button < 0 || button >= 16)
	{
		return 0;
	}

	return (pad.buttons >> (15 - button)) & 1;
}

Uint8
SDL_JoystickGetHat(SDL_Joystick *j, int hat)
{
	(void)j;
	(void)hat;
	return SDL_HAT_CENTERED;
}

SDL_bool
SDL_IsGameController(int joystick_index)
{
	(void)joystick_index;
	return SDL_FALSE;
}

SDL_GameController *
SDL_GameControllerOpen(int joystick_index)
{
	(void)joystick_index;
	return NULL;
}

void
SDL_GameControllerClose(SDL_GameController *gamecontroller)
{
	(void)gamecontroller;
}

const char *
SDL_GameControllerName(SDL_GameController *gamecontroller)
{
	(void)gamecontroller;
	return "";
}

int
SDL_GameControllerEventState(int state)
{
	(void)state;
	return SDL_IGNORE;
}

void
SDL_GameControllerUpdate(void)
{
}

Sint16
SDL_GameControllerGetAxis(SDL_GameController *gamecontroller, SDL_GameControllerAxis axis)
{
	(void)gamecontroller;
	(void)axis;
	return 0;
}

Uint8
SDL_GameControllerGetButton(SDL_GameController *gamecontroller, SDL_GameControllerButton button)
{
	(void)gamecontroller;
	(void)button;
	return 0;
}

const char *
SDL_GameControllerGetStringForButton(SDL_GameControllerButton button)
{
	(void)button;
	return "";
}

/* ================================================================ */
/* Events: none come from the pad (id_in.cpp polls the joystick)      */
/* ================================================================ */

#define EVENT_QUEUE 64

static SDL_Event queue[EVENT_QUEUE];
static int queue_head, queue_tail;

int
SDL_PushEvent(SDL_Event *event)
{
	int next = (queue_tail + 1) % EVENT_QUEUE;

	if (next == queue_head)
	{
		return 0;
	}

	queue[queue_tail] = *event;
	queue_tail = next;
	return 1;
}

int
SDL_PollEvent(SDL_Event *event)
{
	PS3_Pump();

	if (queue_head == queue_tail)
	{
		return 0;
	}

	if (event)
	{
		*event = queue[queue_head];
		queue_head = (queue_head + 1) % EVENT_QUEUE;
	}

	return 1;
}

int
SDL_WaitEventTimeout(SDL_Event *event, int timeout)
{
	if (SDL_PollEvent(event))
	{
		return 1;
	}

	SDL_Delay(timeout > 0 ? (Uint32)timeout : 1);
	return SDL_PollEvent(event);
}

int
SDL_WaitEvent(SDL_Event *event)
{
	while (!SDL_WaitEventTimeout(event, 10))
	{
	}

	return 1;
}

Uint8
SDL_EventState(Uint32 type, int state)
{
	(void)type;
	(void)state;
	return SDL_IGNORE;
}

/* ================================================================ */
/* Memory files                                                       */
/* ================================================================ */

static Sint64
MemSize(SDL_RWops *rw)
{
	return rw->hidden.mem.stop - rw->hidden.mem.base;
}

static Sint64
MemSeek(SDL_RWops *rw, Sint64 offset, int whence)
{
	Uint8 *p;

	switch (whence)
	{
	case RW_SEEK_SET:
		p = rw->hidden.mem.base + offset;
		break;
	case RW_SEEK_CUR:
		p = rw->hidden.mem.here + offset;
		break;
	case RW_SEEK_END:
		p = rw->hidden.mem.stop + offset;
		break;
	default:
		return -1;
	}

	if (p < rw->hidden.mem.base)
	{
		p = rw->hidden.mem.base;
	}
	if (p > rw->hidden.mem.stop)
	{
		p = rw->hidden.mem.stop;
	}

	rw->hidden.mem.here = p;
	return p - rw->hidden.mem.base;
}

static size_t
MemRead(SDL_RWops *rw, void *ptr, size_t size, size_t maxnum)
{
	size_t total = size * maxnum;
	size_t avail = rw->hidden.mem.stop - rw->hidden.mem.here;

	if (size == 0)
	{
		return 0;
	}

	if (total > avail)
	{
		total = avail - avail % size;
	}

	memcpy(ptr, rw->hidden.mem.here, total);
	rw->hidden.mem.here += total;
	return total / size;
}

static int
MemClose(SDL_RWops *rw)
{
	SDL_FreeRW(rw);
	return 0;
}

SDL_RWops *
SDL_AllocRW(void)
{
	return (SDL_RWops *)calloc(1, sizeof(SDL_RWops));
}

void
SDL_FreeRW(SDL_RWops *area)
{
	free(area);
}

SDL_RWops *
SDL_RWFromConstMem(const void *mem, int size)
{
	SDL_RWops *rw = SDL_AllocRW();

	if (!rw)
	{
		return NULL;
	}

	rw->size = MemSize;
	rw->seek = MemSeek;
	rw->read = MemRead;
	rw->close = MemClose;
	rw->hidden.mem.base = (Uint8 *)mem;
	rw->hidden.mem.here = (Uint8 *)mem;
	rw->hidden.mem.stop = (Uint8 *)mem + size;
	return rw;
}

SDL_RWops *
SDL_RWFromMem(void *mem, int size)
{
	return SDL_RWFromConstMem(mem, size);
}

Sint64
SDL_RWsize(SDL_RWops *rw)
{
	Sint64 pos, size;

	if (rw->size)
	{
		return rw->size(rw);
	}

	/* ECWolf's Mac sounds have no size function: by seeking */
	pos = rw->seek(rw, 0, RW_SEEK_CUR);
	size = rw->seek(rw, 0, RW_SEEK_END);
	rw->seek(rw, pos, RW_SEEK_SET);
	return size;
}

Sint64
SDL_RWseek(SDL_RWops *rw, Sint64 offset, int whence)
{
	return rw->seek(rw, offset, whence);
}

size_t
SDL_RWread(SDL_RWops *rw, void *ptr, size_t size, size_t maxnum)
{
	return rw->read(rw, ptr, size, maxnum);
}

int
SDL_RWclose(SDL_RWops *rw)
{
	return rw->close ? rw->close(rw) : 0;
}

/* ================================================================ */
/* Audio helpers: 16 bit stereo, little-endian or native               */
/* ================================================================ */

int
SDL_BuildAudioCVT(SDL_AudioCVT *cvt, SDL_AudioFormat src_format,
		Uint8 src_channels, int src_rate, SDL_AudioFormat dst_format,
		Uint8 dst_channels, int dst_rate)
{
	(void)src_channels;
	(void)src_rate;
	(void)dst_channels;
	(void)dst_rate;

	memset(cvt, 0, sizeof(*cvt));
	cvt->src_format = src_format;
	cvt->dst_format = dst_format;
	cvt->len_mult = 1;
	cvt->len_ratio = 1.0;
	cvt->rate_incr = 1.0;

	/* only the byte order can differ (16 bit stereo at the mixer's rate on
	   both ends): ECWolf's AUDIO_S16 (little-endian) to the mixer's
	   AUDIO_S16SYS, big-endian on the PS3 */
	cvt->needed = src_format != dst_format;
	return cvt->needed;
}

int
SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
	cvt->len_cvt = cvt->len;

	if (cvt->needed && cvt->buf)
	{
		Uint8 *p = cvt->buf;
		int i;

		for (i = 0; i + 1 < cvt->len; i += 2)
		{
			Uint8 t = p[i];

			p[i] = p[i + 1];
			p[i + 1] = t;
		}
	}

	return 0;
}

void
SDL_MixAudioFormat(Uint8 *dst, const Uint8 *src, SDL_AudioFormat format,
		Uint32 len, int volume)
{
	Sint16 *d = (Sint16 *)dst;
	const Sint16 *s = (const Sint16 *)src;
	Uint32 i;

	(void)format;

	for (i = 0; i < len / 2; i++)
	{
		int v = d[i] + s[i] * volume / SDL_MIX_MAXVOLUME;

		d[i] = (Sint16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
	}
}
