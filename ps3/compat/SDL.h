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
 * A tiny "SDL 2" for the PS3 (1.1). It is NOT SDL: it declares only what
 * ECWolf uses, and ps3/source/sdl_ps3.c implements it: the DualShock 3 as
 * a joystick (the 16 buttons and 4 axes the SDL2_PSL1GHT port of 1.0 had,
 * in the same order, so bindings and the menus' button numbers still
 * hold), the clock, mutexes, memory "files", and the audio helpers
 * id_sd.cpp uses. SDL_mixer is ps3/source/sdl_mixer_ps3.c. Video doesn't
 * go through here: sdlvideo.cpp hands the 8 bit screen and its palette
 * to the RSX itself (ps3_display.c). Types, constants and prototypes
 * follow SDL 2.30, so the engine builds with almost no changes.
 *
 * ps3/compat comes first in the include path (ps3/Makefile), so the
 * engine's #include "SDL.h" lands here (SDL2/SDL.h too).
 *
 * =======================================================================
 */

#ifndef PS3_SDL_SHIM_H
#define PS3_SDL_SHIM_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- version -------------------------------------------------------- */

#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 30
#define SDL_PATCHLEVEL    0

#define SDL_VERSIONNUM(X, Y, Z) ((X)*1000 + (Y)*100 + (Z))
#define SDL_COMPILEDVERSION SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL)
#define SDL_VERSION_ATLEAST(X, Y, Z) (SDL_COMPILEDVERSION >= SDL_VERSIONNUM(X, Y, Z))

typedef struct SDL_version
{
	unsigned char major, minor, patch;
} SDL_version;

#define SDL_VERSION(x) \
	do { (x)->major = SDL_MAJOR_VERSION; (x)->minor = SDL_MINOR_VERSION; \
	     (x)->patch = SDL_PATCHLEVEL; } while (0)

void SDL_GetVersion(SDL_version *ver);

/* ---- basic types ---------------------------------------------------- */

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint64_t Uint64;
typedef int64_t Sint64;

typedef enum
{
	SDL_FALSE = 0,
	SDL_TRUE = 1
} SDL_bool;

#define SDL_malloc malloc
#define SDL_free free
#define SDL_memset memset
#define SDL_memcpy memcpy
#define SDL_vsnprintf vsnprintf

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#if defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define SDL_BYTEORDER SDL_BIG_ENDIAN
#else
#define SDL_BYTEORDER SDL_LIL_ENDIAN
#endif

static inline Uint16 SDL_Swap16(Uint16 x) { return (Uint16)((x << 8) | (x >> 8)); }
static inline Uint32 SDL_Swap32(Uint32 x) { return __builtin_bswap32(x); }
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define SDL_SwapLE16(x) SDL_Swap16(x)
#define SDL_SwapLE32(x) SDL_Swap32(x)
#define SDL_SwapBE16(x) (x)
#define SDL_SwapBE32(x) (x)
#else
#define SDL_SwapLE16(x) (x)
#define SDL_SwapLE32(x) (x)
#define SDL_SwapBE16(x) SDL_Swap16(x)
#define SDL_SwapBE32(x) SDL_Swap32(x)
#endif

/* ---- init ----------------------------------------------------------- */

#define SDL_INIT_TIMER          0x00000001u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_JOYSTICK       0x00000200u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_EVENTS         0x00004000u

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED 1
#endif

int SDL_Init(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
Uint32 SDL_WasInit(Uint32 flags);
void SDL_QuitSubSystem(Uint32 flags);
void SDL_Quit(void);

const char *SDL_GetError(void);

#define SDL_HINT_RENDER_DRIVER "SDL_RENDER_DRIVER"
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_ACCELEROMETER_AS_JOYSTICK "SDL_ACCELEROMETER_AS_JOYSTICK"
SDL_bool SDL_SetHint(const char *name, const char *value);

/* ---- time ----------------------------------------------------------- */

Uint32 SDL_GetTicks(void);
Uint64 SDL_GetTicks64(void);
void SDL_Delay(Uint32 ms);

/* ---- threads -------------------------------------------------------- */

typedef struct SDL_mutex SDL_mutex;
SDL_mutex *SDL_CreateMutex(void);
int SDL_LockMutex(SDL_mutex *mutex);
int SDL_UnlockMutex(SDL_mutex *mutex);
void SDL_DestroyMutex(SDL_mutex *mutex);

/* ---- video (only the types: sdlvideo.cpp's PS3 path doesn't use it) - */

typedef struct SDL_Color
{
	Uint8 r;
	Uint8 g;
	Uint8 b;
	Uint8 a;
} SDL_Color;

typedef struct SDL_Palette
{
	int ncolors;
	SDL_Color *colors;
	Uint32 version;
	int refcount;
} SDL_Palette;

typedef struct SDL_PixelFormat
{
	Uint32 format;
	SDL_Palette *palette;
	Uint8 BitsPerPixel;
	Uint8 BytesPerPixel;
	Uint32 Rmask, Gmask, Bmask, Amask;
} SDL_PixelFormat;

typedef struct SDL_Rect
{
	int x, y;
	int w, h;
} SDL_Rect;

typedef struct SDL_Surface
{
	Uint32 flags;
	SDL_PixelFormat *format;
	int w, h;
	int pitch;
	void *pixels;
} SDL_Surface;

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

typedef struct SDL_DisplayMode
{
	Uint32 format;
	int w, h;
	int refresh_rate;
	void *driverdata;
} SDL_DisplayMode;

#define SDL_PIXELFORMAT_INDEX8      0x13000801u
#define SDL_PIXELFORMAT_RGB565      0x15151002u
#define SDL_PIXELFORMAT_ARGB1555    0x15331002u
#define SDL_PIXELFORMAT_RGB888      0x16161804u
#define SDL_PIXELFORMAT_ARGB8888    0x16362004u
#define SDL_PIXELFORMAT_ARGB2101010 0x16372004u

#define SDL_WINDOWPOS_UNDEFINED_DISPLAY(X) (0x1FFF0000u | (X))
#define SDL_WINDOWPOS_CENTERED        0x2FFF0000u
#define SDL_WINDOW_FULLSCREEN         0x00000001u
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000u)

#define SDL_RENDERER_SOFTWARE       0x00000001u
#define SDL_RENDERER_ACCELERATED    0x00000002u
#define SDL_RENDERER_PRESENTVSYNC   0x00000004u
#define SDL_TEXTUREACCESS_STREAMING 1

int SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode);

/* A window, a renderer and a texture exist so sdlvideo.cpp's frame
   buffer is valid; its PS3 path (I_AM_PS3) presents through
   ps3_display.c and never draws with them. */
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window *window);
Uint32 SDL_GetWindowFlags(SDL_Window *window);
void SDL_GetWindowSize(SDL_Window *window, int *w, int *h);
void SDL_SetWindowSize(SDL_Window *window, int w, int h);
int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags);
SDL_Surface *SDL_GetWindowSurface(SDL_Window *window);
int SDL_UpdateWindowSurface(SDL_Window *window);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer *renderer);
int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h);
int SDL_RenderClear(SDL_Renderer *renderer);
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
		const SDL_Rect *srcrect, const SDL_Rect *dstrect);
void SDL_RenderPresent(SDL_Renderer *renderer);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h);
int SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access, int *w, int *h);
void SDL_DestroyTexture(SDL_Texture *texture);
int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);
int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int firstcolor, int ncolors);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch);
void SDL_UnlockTexture(SDL_Texture *texture);
SDL_bool SDL_PixelFormatEnumToMasks(Uint32 format, int *bpp, Uint32 *Rmask,
		Uint32 *Gmask, Uint32 *Bmask, Uint32 *Amask);

/* ---- keyboard ------------------------------------------------------- */

#include "ps3_sdl_keys.h"

SDL_Keymod SDL_GetModState(void);
void SDL_SetModState(SDL_Keymod modstate);

/* ---- mouse (none) --------------------------------------------------- */

#define SDL_BUTTON(X)       (1u << ((X) - 1))
#define SDL_BUTTON_LEFT     1
#define SDL_BUTTON_MIDDLE   2
#define SDL_BUTTON_RIGHT    3
#define SDL_BUTTON_WHEELUP   4
#define SDL_BUTTON_WHEELDOWN 5

Uint32 SDL_GetMouseState(int *x, int *y);
Uint32 SDL_GetRelativeMouseState(int *x, int *y);
int SDL_SetRelativeMouseMode(SDL_bool enabled);
void SDL_WarpMouseInWindow(SDL_Window *window, int x, int y);
int SDL_ShowCursor(int toggle);

/* ---- joystick: the DualShock 3 ---------------------------------------
 *
 * Buttons 0-15 are the bits of the pad's two button bytes, high bit
 * first: 0 left, 1 down, 2 right, 3 up, 4 start, 5 R3, 6 L3, 7 select,
 * 8 square, 9 cross, 10 circle, 11 triangle, 12 R1, 13 L1, 14 R2, 15 L2
 * (as 1.0's SDL2_PSL1GHT numbered them). Axes 0-3: left X, left Y,
 * right X, right Y. No hats. */

typedef struct _SDL_Joystick SDL_Joystick;

#define SDL_JOYSTICK_AXIS_MAX 32767
#define SDL_QUERY   -1
#define SDL_IGNORE   0
#define SDL_DISABLE  0
#define SDL_ENABLE   1

#define SDL_HAT_CENTERED 0x00
#define SDL_HAT_UP       0x01
#define SDL_HAT_RIGHT    0x02
#define SDL_HAT_DOWN     0x04
#define SDL_HAT_LEFT     0x08

int SDL_NumJoysticks(void);
const char *SDL_JoystickNameForIndex(int device_index);
SDL_Joystick *SDL_JoystickOpen(int device_index);
void SDL_JoystickClose(SDL_Joystick *joystick);
SDL_bool SDL_JoystickGetAttached(SDL_Joystick *joystick);
int SDL_JoystickEventState(int state);
void SDL_JoystickUpdate(void);
int SDL_JoystickNumAxes(SDL_Joystick *joystick);
int SDL_JoystickNumButtons(SDL_Joystick *joystick);
int SDL_JoystickNumHats(SDL_Joystick *joystick);
Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis);
Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button);
Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat);

/* the game controller API: declared, never available (id_in.cpp uses the
   raw joystick on the PS3) */
typedef struct _SDL_GameController SDL_GameController;

typedef enum
{
	SDL_CONTROLLER_AXIS_INVALID = -1,
	SDL_CONTROLLER_AXIS_LEFTX,
	SDL_CONTROLLER_AXIS_LEFTY,
	SDL_CONTROLLER_AXIS_RIGHTX,
	SDL_CONTROLLER_AXIS_RIGHTY,
	SDL_CONTROLLER_AXIS_TRIGGERLEFT,
	SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
	SDL_CONTROLLER_AXIS_MAX
} SDL_GameControllerAxis;

typedef enum
{
	SDL_CONTROLLER_BUTTON_INVALID = -1,
	SDL_CONTROLLER_BUTTON_A,
	SDL_CONTROLLER_BUTTON_B,
	SDL_CONTROLLER_BUTTON_X,
	SDL_CONTROLLER_BUTTON_Y,
	SDL_CONTROLLER_BUTTON_BACK,
	SDL_CONTROLLER_BUTTON_GUIDE,
	SDL_CONTROLLER_BUTTON_START,
	SDL_CONTROLLER_BUTTON_LEFTSTICK,
	SDL_CONTROLLER_BUTTON_RIGHTSTICK,
	SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
	SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
	SDL_CONTROLLER_BUTTON_DPAD_UP,
	SDL_CONTROLLER_BUTTON_DPAD_DOWN,
	SDL_CONTROLLER_BUTTON_DPAD_LEFT,
	SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
	SDL_CONTROLLER_BUTTON_MAX
} SDL_GameControllerButton;

SDL_bool SDL_IsGameController(int joystick_index);
SDL_GameController *SDL_GameControllerOpen(int joystick_index);
void SDL_GameControllerClose(SDL_GameController *gamecontroller);
const char *SDL_GameControllerName(SDL_GameController *gamecontroller);
int SDL_GameControllerEventState(int state);
void SDL_GameControllerUpdate(void);
Sint16 SDL_GameControllerGetAxis(SDL_GameController *gamecontroller, SDL_GameControllerAxis axis);
Uint8 SDL_GameControllerGetButton(SDL_GameController *gamecontroller, SDL_GameControllerButton button);
const char *SDL_GameControllerGetStringForButton(SDL_GameControllerButton button);

/* ---- events --------------------------------------------------------- */

#define SDL_RELEASED 0
#define SDL_PRESSED  1

typedef enum
{
	SDL_FIRSTEVENT = 0,
	SDL_QUIT = 0x100,
	SDL_WINDOWEVENT = 0x200,
	SDL_KEYDOWN = 0x300,
	SDL_KEYUP,
	SDL_TEXTEDITING,
	SDL_TEXTINPUT,
	SDL_MOUSEMOTION = 0x400,
	SDL_MOUSEBUTTONDOWN,
	SDL_MOUSEBUTTONUP,
	SDL_MOUSEWHEEL,
	SDL_JOYAXISMOTION = 0x600,
	SDL_JOYBALLMOTION,
	SDL_JOYHATMOTION,
	SDL_JOYBUTTONDOWN,
	SDL_JOYBUTTONUP,
	SDL_FINGERDOWN = 0x700,
	SDL_FINGERUP,
	SDL_FINGERMOTION
} SDL_EventType;

typedef enum
{
	SDL_WINDOWEVENT_NONE,
	SDL_WINDOWEVENT_SHOWN,
	SDL_WINDOWEVENT_HIDDEN,
	SDL_WINDOWEVENT_EXPOSED,
	SDL_WINDOWEVENT_MOVED,
	SDL_WINDOWEVENT_RESIZED,
	SDL_WINDOWEVENT_SIZE_CHANGED,
	SDL_WINDOWEVENT_MINIMIZED,
	SDL_WINDOWEVENT_MAXIMIZED,
	SDL_WINDOWEVENT_RESTORED,
	SDL_WINDOWEVENT_ENTER,
	SDL_WINDOWEVENT_LEAVE,
	SDL_WINDOWEVENT_FOCUS_GAINED,
	SDL_WINDOWEVENT_FOCUS_LOST
} SDL_WindowEventID;

typedef struct SDL_Keysym
{
	SDL_Scancode scancode;
	SDL_Keycode sym;
	Uint16 mod;
	Uint32 unused;
} SDL_Keysym;

typedef struct SDL_KeyboardEvent
{
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	Uint8 state;
	Uint8 repeat;
	Uint8 padding2;
	Uint8 padding3;
	SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_TextInputEvent
{
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	char text[32];
} SDL_TextInputEvent;

typedef struct SDL_WindowEvent
{
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	Uint8 event;
	Uint8 padding1, padding2, padding3;
	Sint32 data1, data2;
} SDL_WindowEvent;

typedef struct SDL_MouseMotionEvent
{
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	Uint32 which;
	Uint32 state;
	Sint32 x, y;
	Sint32 xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent
{
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	Uint32 which;
	Uint8 button;
	Uint8 state;
	Uint8 clicks;
	Uint8 padding1;
	Sint32 x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseWheelEvent
{
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	Uint32 which;
	Sint32 x, y;
	Uint32 direction;
} SDL_MouseWheelEvent;

typedef struct SDL_JoyButtonEvent
{
	Uint32 type;
	Uint32 timestamp;
	Sint32 which;
	Uint8 button;
	Uint8 state;
	Uint8 padding1, padding2;
} SDL_JoyButtonEvent;

typedef struct SDL_TouchFingerEvent
{
	Uint32 type;
	Uint32 timestamp;
	Sint64 touchId;
	Sint64 fingerId;
	float x, y, dx, dy, pressure;
} SDL_TouchFingerEvent;

typedef union SDL_Event
{
	Uint32 type;
	SDL_KeyboardEvent key;
	SDL_TextInputEvent text;
	SDL_WindowEvent window;
	SDL_MouseMotionEvent motion;
	SDL_MouseButtonEvent button;
	SDL_MouseWheelEvent wheel;
	SDL_JoyButtonEvent jbutton;
	SDL_TouchFingerEvent tfinger;
	Uint8 padding[56];
} SDL_Event;

int SDL_PollEvent(SDL_Event *event);
int SDL_WaitEventTimeout(SDL_Event *event, int timeout);
int SDL_WaitEvent(SDL_Event *event);
int SDL_PushEvent(SDL_Event *event);
Uint8 SDL_EventState(Uint32 type, int state);

/* ---- memory "files" ------------------------------------------------- */

#define RW_SEEK_SET 0
#define RW_SEEK_CUR 1
#define RW_SEEK_END 2

typedef struct SDL_RWops
{
	Sint64 (*size)(struct SDL_RWops *context);
	Sint64 (*seek)(struct SDL_RWops *context, Sint64 offset, int whence);
	size_t (*read)(struct SDL_RWops *context, void *ptr, size_t size, size_t maxnum);
	size_t (*write)(struct SDL_RWops *context, const void *ptr, size_t size, size_t num);
	int (*close)(struct SDL_RWops *context);
	Uint32 type;
	union
	{
		struct
		{
			Uint8 *base;
			Uint8 *here;
			Uint8 *stop;
		} mem;
		struct
		{
			void *data1;
			void *data2;
		} unknown;
	} hidden;
} SDL_RWops;

SDL_RWops *SDL_AllocRW(void);
void SDL_FreeRW(SDL_RWops *area);
SDL_RWops *SDL_RWFromMem(void *mem, int size);
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);
Sint64 SDL_RWsize(SDL_RWops *context);
Sint64 SDL_RWseek(SDL_RWops *context, Sint64 offset, int whence);
size_t SDL_RWread(SDL_RWops *context, void *ptr, size_t size, size_t maxnum);
int SDL_RWclose(SDL_RWops *context);

/* ---- audio ---------------------------------------------------------- */

typedef Uint16 SDL_AudioFormat;

#define AUDIO_U8     0x0008
#define AUDIO_S8     0x8008
#define AUDIO_S16LSB 0x8010
#define AUDIO_S16MSB 0x9010
#define AUDIO_S16    AUDIO_S16LSB
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define AUDIO_S16SYS AUDIO_S16LSB
#else
#define AUDIO_S16SYS AUDIO_S16MSB
#endif

#define SDL_MIX_MAXVOLUME 128

/* id_sd.cpp builds one to turn its 16 bit stereo into what the mixer
   opened; the mixer opens 16 bit stereo, so none is needed (needed 0) */
typedef struct SDL_AudioCVT
{
	int needed;
	SDL_AudioFormat src_format;
	SDL_AudioFormat dst_format;
	double rate_incr;
	Uint8 *buf;
	int len;
	int len_cvt;
	int len_mult;
	double len_ratio;
} SDL_AudioCVT;

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, SDL_AudioFormat src_format,
		Uint8 src_channels, int src_rate, SDL_AudioFormat dst_format,
		Uint8 dst_channels, int dst_rate);
int SDL_ConvertAudio(SDL_AudioCVT *cvt);
void SDL_MixAudioFormat(Uint8 *dst, const Uint8 *src, SDL_AudioFormat format,
		Uint32 len, int volume);

#ifdef __cplusplus
}
#endif

#endif
