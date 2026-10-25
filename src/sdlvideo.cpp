
// HEADER FILES ------------------------------------------------------------

#include "wl_def.h"
#include "c_cvars.h"

#include "templates.h"
//#include "i_system.h"
//#include "i_video.h"
#include "v_video.h"
#include "v_pfx.h"
//#include "stats.h"
#include "v_palette.h"
#include "sdlvideo.h"
//#include "r_swrenderer.h"
#include "thingdef/thingdef.h"
#include "wl_main.h"
#include "version.h"

#include <SDL.h>

IVideo *Video = NULL;

#include <stdio.h>
#include <lv2/systime.h>
#include <sysutil/sysutil.h>
#ifdef I_AM_PS3
#include "ps3_platform.h" // PS3 1.1: ps3_display.c
#endif
extern "C" {
}

extern float screenGamma;

DFrameBuffer *I_SetMode (int &width, int &height, DFrameBuffer *old)
{
	bool fs = false;
	switch (Video->GetDisplayType ())
	{
	case DISPLAY_WindowOnly:
		fs = false;
		break;
	case DISPLAY_FullscreenOnly:
		fs = true;
		break;
	case DISPLAY_Both:
		fs = vid_fullscreen;
		break;
	}
	DFrameBuffer *res = Video->CreateFrameBuffer (width, height, fs, old);

	/* Right now, CreateFrameBuffer cannot return NULL
	if (res == NULL)
	{
		I_FatalError ("Mode %dx%d is unavailable\n", width, height);
	}
	*/
	return res;
}

bool I_CheckResolution (int width, int height, int bits)
{
	int twidth, theight;

	Video->StartModeIterator (bits, screen ? screen->IsFullscreen() : vid_fullscreen);
	while (Video->NextMode (&twidth, &theight, NULL))
	{
		if (width == twidth && height == theight)
			return true;
	}
	return false;
}

void I_ClosestResolution (int *width, int *height, int bits)
{
	int twidth, theight;
	int cwidth = 0, cheight = 0;
	int iteration;
	DWORD closest = 0xFFFFFFFFu;

	for (iteration = 0; iteration < 2; iteration++)
	{
		Video->StartModeIterator (bits, screen ? screen->IsFullscreen() : vid_fullscreen);
		while (Video->NextMode (&twidth, &theight, NULL))
		{
			if (twidth == *width && theight == *height)
				return;

			if (iteration == 0 && (twidth < *width || theight < *height))
				continue;

			DWORD dist = (twidth - *width) * (twidth - *width)
				+ (theight - *height) * (theight - *height);

			if (dist < closest)
			{
				closest = dist;
				cwidth = twidth;
				cheight = theight;
			}
		}
		if (closest != 0xFFFFFFFFu)
		{
			*width = cwidth;
			*height = cheight;
			return;
		}
	}
}

//
// V_SetResolution
//
bool V_DoModeSetup (int width, int height, int bits)
{
	DFrameBuffer *buff = I_SetMode (width, height, screen);
	int cx1, cx2;

	if (buff == NULL)
	{
		return false;
	}

	screen = buff;
	GC::WriteBarrier(screen);
	screen->SetGamma (screenGamma);

	// Load fonts now so they can be packed into textures straight away,
	// if D3DFB is being used for the display.
	//FFont::StaticPreloadFonts();

	V_CalcCleanFacs(320, 200, width, height, &CleanXfac, &CleanYfac, &cx1, &cx2);

	CleanWidth = width / CleanXfac;
	CleanHeight = height / CleanYfac;
	assert(CleanWidth >= 320);
	assert(CleanHeight >= 200);

	if (width < 800 || width >= 960)
	{
		if (cx1 < cx2)
		{
			// Special case in which we don't need to scale down.
			CleanXfac_1 = 
			CleanYfac_1 = cx1;
		}
		else
		{
			CleanXfac_1 = MAX(CleanXfac - 1, 1);
			CleanYfac_1 = MAX(CleanYfac - 1, 1);
			// On larger screens this is not enough so make sure it's at most 3/4 of the screen's width
			while (CleanXfac_1 * 320 > screen->GetWidth()*3/4 && CleanXfac_1 > 2)
			{
				CleanXfac_1--;
				CleanYfac_1--;
			}
		}
		CleanWidth_1 = width / CleanXfac_1;
		CleanHeight_1 = height / CleanYfac_1;
	}
	else // if the width is between 800 and 960 the ratio between the screensize and CleanXFac-1 becomes too large.
	{
		CleanXfac_1 = CleanXfac;
		CleanYfac_1 = CleanYfac;
		CleanWidth_1 = CleanWidth;
		CleanHeight_1 = CleanHeight;
	}


	DisplayWidth = width;
	DisplayHeight = height;
	DisplayBits = bits;
	//R_OldBlend = ~0;
	//Renderer->OnModeSet();
	//M_RefreshModesList ();
	return true;
}

bool IVideo::SetResolution (int width, int height, int bits)
{
	int oldwidth, oldheight;
	int oldbits;

	if (screen)
	{
		oldwidth = SCREENWIDTH;
		oldheight = SCREENHEIGHT;
		oldbits = DisplayBits;
	}
	else
	{ // Harmless if screen wasn't allocated
		oldwidth = width;
		oldheight = height;
		oldbits = bits;
	}

	I_ClosestResolution (&width, &height, bits);
	if (!I_CheckResolution (width, height, bits))
	{ // Try specified resolution
		if (!I_CheckResolution (oldwidth, oldheight, oldbits))
		{ // Try previous resolution (if any)
	   		return false;
		}
		else
		{
			width = oldwidth;
			height = oldheight;
			bits = oldbits;
		}
	}
	return V_DoModeSetup (width, height, bits);
}

void IVideo::DumpAdapters ()
{
	Printf("Multi-monitor support unavailable.\n");
}

void I_ShutdownGraphics ()
{
	if (screen)
	{
		DFrameBuffer *s = screen;
		screen = NULL;
		s->ObjectFlags |= OF_YesReallyDelete;
		delete s;
	}
	if (Video)
		delete Video, Video = NULL;

	SDL_QuitSubSystem (SDL_INIT_VIDEO);
}

void I_InitGraphics ()
{
	if(Video)
		return;

	if (SDL_InitSubSystem (SDL_INIT_VIDEO) < 0)
	{
		I_FatalError ("Could not initialize SDL video:\n%s\n", SDL_GetError());
		return;
	}

#ifdef __ANDROID__
	extern void Android_InitGraphics();
	Android_InitGraphics();
#endif

	Video = new SDLVideo (0);
	if (Video == NULL)
		I_FatalError ("Failed to initialize display");

//	Video->SetWindowedScale (vid_winscale);
}

// MACROS ------------------------------------------------------------------

// TYPES -------------------------------------------------------------------

class SDLFB : public DFrameBuffer
{
	DECLARE_CLASS(SDLFB, DFrameBuffer)
public:
#if SDL_VERSION_ATLEAST(2,0,0)
	SDLFB (int width, int height, bool fullscreen, SDL_Window *oldwin);
#else
	SDLFB (int width, int height, bool fullscreen);
#endif
	~SDLFB ();

	bool Lock (bool buffer);
	void Unlock ();
	bool Relock ();
	void ForceBuffering (bool force);
	bool IsValid ();
	void Update ();
	PalEntry *GetPalette ();
	void GetFlashedPalette (PalEntry pal[256]);
	void UpdatePalette ();
	bool SetGamma (float gamma);
	bool SetFlash (PalEntry rgb, int amount);
	void GetFlash (PalEntry &rgb, int &amount);
	void SetFullscreen (bool fullscreen);
	int GetPageCount ();
	bool IsFullscreen ();

	void PaletteChanged () { }
	int QueryNewPalette () { return 0; }
	bool Is8BitMode() { return true; }

	friend class SDLVideo;

	virtual void SetVSync (bool vsync);
	virtual void ScaleCoordsFromWindow(SWORD &x, SWORD &y);

private:
	PalEntry SourcePalette[256];
	BYTE GammaTable[3][256];
	PalEntry Flash;
	int FlashAmount;
	float Gamma;
	bool UpdatePending;

#if SDL_VERSION_ATLEAST(2,0,0)
	SDL_Window *Screen;
	SDL_Renderer *Renderer;
	union
	{
		SDL_Texture *Texture;
		SDL_Surface *Surface;
	};
#else
	SDL_Surface *Screen;
#endif

	bool UsingRenderer;
	bool NeedPalUpdate;
	bool NeedGammaUpdate;
	bool NotPaletted;

	void UpdateColors ();
	void ResetSDLRenderer ();

	SDLFB () {}
};
IMPLEMENT_INTERNAL_CLASS(SDLFB)

struct MiniModeInfo
{
	WORD Width, Height;
};

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern IVideo *Video;
//extern SDL_Surface *cursorSurface;
//extern SDL_Rect cursorBlit;
//extern bool GUICapture;

#define vid_maxfps 70
#define cl_capfps false
//EXTERN_CVAR (Float, Gamma)
//EXTERN_CVAR (Int, vid_maxfps)
//EXTERN_CVAR (Bool, cl_capfps)

// PUBLIC DATA DEFINITIONS -------------------------------------------------

extern unsigned screenBits;
#define vid_displaybits screenBits

#if 0
CVAR (Int, vid_adapter, 0, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

CVAR (Int, vid_displaybits, 32, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

CVAR (Bool, vid_forcesurface, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

CUSTOM_CVAR (Float, rgamma, 1.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (screen != NULL)
	{
		screen->SetGamma (Gamma);
	}
}
CUSTOM_CVAR (Float, ggamma, 1.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (screen != NULL)
	{
		screen->SetGamma (Gamma);
	}
}
CUSTOM_CVAR (Float, bgamma, 1.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (screen != NULL)
	{
		screen->SetGamma (Gamma);
	}
}
#else
#define vid_adapter 0
#define rgamma 1.f
#define ggamma 1.f
#define bgamma 1.f
#define vid_forcesurface 0
#endif

// PRIVATE DATA DEFINITIONS ------------------------------------------------

// Dummy screen sizes to pass when windowed
static MiniModeInfo WinModes[] =
{
	{ 320, 200 },
	{ 320, 240 },
	{ 400, 225 },	// 16:9
	{ 400, 300 },
	{ 480, 270 },	// 16:9
	{ 480, 360 },
	{ 512, 288 },	// 16:9
	{ 512, 384 },
	{ 640, 270 },   // 64:27 (~21:9)
	{ 640, 360 },	// 16:9
	{ 640, 400 },
	{ 640, 480 },
	{ 720, 480 },	// 16:10
	{ 720, 540 },
	{ 800, 450 },	// 16:9
	{ 800, 480 },
	{ 800, 500 },	// 16:10
	{ 800, 600 },
	{ 848, 480 },	// 16:9
	{ 960, 600 },	// 16:10
	{ 960, 720 },
	{ 1024, 288 },  // 32:9
	{ 1024, 576 },	// 16:9
	{ 1024, 600 },	// 17:10
	{ 1024, 640 },	// 16:10
	{ 1024, 768 },
	{ 1088, 612 },	// 16:9
	{ 1152, 648 },	// 16:9
	{ 1152, 720 },	// 16:10
	{ 1152, 864 },
	{ 1280, 360 },  // 32:9
	{ 1280, 540 },  // 64:27 (~21:9)
	{ 1280, 720 },	// 16:9
	{ 1280, 854 },
	{ 1280, 800 },	// 16:10
	{ 1280, 960 },
	{ 1280, 1024 },	// 5:4
	{ 1360, 768 },	// 16:9
	{ 1366, 768 },
	{ 1400, 787 },	// 16:9
	{ 1400, 875 },	// 16:10
	{ 1400, 1050 },
	{ 1440, 900 },
	{ 1440, 960 },
	{ 1440, 1080 },
	{ 1600, 900 },	// 16:9
	{ 1600, 1000 },	// 16:10
	{ 1600, 1200 },
	{ 1680, 1050 },	// 16:10
	{ 1720, 720 },  // 43:18 (~21:9)
	{ 1920, 540 },  // 32:9
	{ 1920, 1080 },
	{ 1920, 1200 },
	{ 2048, 864 }, // 64:27 (~21:9)
	{ 2048, 1536 },
	{ 2240, 1400 }, // 16:10
	{ 2304, 1440 }, // 16:10
	{ 2560, 720 },  // 32:9
	{ 2560, 1080 }, // 64:27 (~21:9)
	{ 2560, 1440 },
	{ 2560, 1600 },
	{ 2560, 2048 },
	{ 2880, 1800 },
	{ 3072, 1920 }, // 16:10
	{ 3200, 1800 },
	{ 3440, 1440 }, // 43:18 (~21:9)
	{ 3840, 1080 }, // 32:9
	{ 3840, 2160 },
	{ 3840, 2400 },
	{ 4096, 2160 },
	{ 4096, 2304 }, // 16:9
	{ 4480, 2520 }, // 16:9
	{ 5120, 1440 }, // 32:9
	{ 5120, 2880 }
};

//static cycle_t BlitCycles;
//static cycle_t SDLFlipCycles;

// CODE --------------------------------------------------------------------

void ScaleWithAspect (int &w, int &h, int Width, int Height)
{
	int resRatio = CheckRatio (Width, Height);
	int screenRatio;
	CheckRatio (w, h, &screenRatio);
	if (resRatio == screenRatio)
		return;

	double yratio;
	switch(resRatio)
	{
		case ASPECT_NONE: yratio = 4./3.; break;
		case ASPECT_16_9: yratio = 16./9.; break;
		case ASPECT_16_10: yratio = 16./10.; break;
		case ASPECT_17_10: yratio = 17./10.; break;
		case ASPECT_5_4: yratio = 5./4.; break;
		case ASPECT_4_3: yratio = 4./3.; break;
		case ASPECT_64_27: yratio = 64./27.; break;
		case ASPECT_32_9: yratio = 32./9.; break;
		default: return;
	}
	double y = w/yratio;
	if (y > h)
		w = (int)(h*yratio);
	else
		h = (int)(y);
}

SDLVideo::SDLVideo (int parm)
{
	IteratorBits = 0;
	IteratorFS = false;
}

SDLVideo::~SDLVideo ()
{
}

void SDLVideo::StartModeIterator (int bits, bool fs)
{
	IteratorMode = 0;
	IteratorBits = bits;
	IteratorFS = fs;
}

bool SDLVideo::NextMode (int *width, int *height, bool *letterbox)
{
	if (IteratorBits != 8)
		return false;

#if !SDL_VERSION_ATLEAST(2,0,0)
	if (!IteratorFS)
	{
#endif
		if ((unsigned)IteratorMode < sizeof(WinModes)/sizeof(WinModes[0]))
		{
			*width = WinModes[IteratorMode].Width;
			*height = WinModes[IteratorMode].Height;
			++IteratorMode;
			return true;
		}
#if !SDL_VERSION_ATLEAST(2,0,0)
	}
	else
	{
		SDL_Rect **modes = SDL_ListModes (NULL, SDL_FULLSCREEN|SDL_HWSURFACE);
		if (modes != NULL && modes[IteratorMode] != NULL)
		{
			*width = modes[IteratorMode]->w;
			*height = modes[IteratorMode]->h;
			++IteratorMode;
			return true;
		}
	}
#endif
	return false;
}

DFrameBuffer *SDLVideo::CreateFrameBuffer (int width, int height, bool fullscreen, DFrameBuffer *old)
{
	static int retry = 0;
	static int owidth, oheight;
	
	PalEntry flashColor;
	int flashAmount;

#if SDL_VERSION_ATLEAST(2,0,0)
	SDL_Window *oldwin = NULL;
#endif

#if __ANDROID__
	// Always fullscreen in Android
	fullscreen = true;
#endif

	if (old != NULL)
	{ // Reuse the old framebuffer if its attributes are the same
		SDLFB *fb = static_cast<SDLFB *> (old);
		if (fb->Width == width &&
			fb->Height == height)
		{
#if SDL_VERSION_ATLEAST(2,0,0)
			bool fsnow = (SDL_GetWindowFlags (fb->Screen) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
	
			if (fsnow != fullscreen)
			{
				fb->SetFullscreen (fullscreen);
			}
			return old;
#else
			bool fsnow = (fb->Screen->flags & SDL_FULLSCREEN) != 0;
	
			if (fsnow == fullscreen)
				return old;
			if (fsnow != fullscreen)
			{
				if(SDL_WM_ToggleFullScreen (fb->Screen))
					return old;
			}
#endif
		}

#if SDL_VERSION_ATLEAST(2,0,0)
		oldwin = fb->Screen;
		fb->Screen = NULL;
#endif

		old->GetFlash (flashColor, flashAmount);
		old->ObjectFlags |= OF_YesReallyDelete;
		if (screen == old) screen = NULL;
		delete old;
	}
	else
	{
		flashColor = 0;
		flashAmount = 0;
	}

#if SDL_VERSION_ATLEAST(2,0,0)
	SDLFB *fb = new SDLFB (width, height, fullscreen, oldwin);
#else
	SDLFB *fb = new SDLFB (width, height, fullscreen);
#endif
	
	// If we could not create the framebuffer, try again with slightly
	// different parameters in this order:
	// 1. Try with the closest size
	// 2. Try in the opposite screen mode with the original size
	// 3. Try in the opposite screen mode with the closest size
	// This is a somewhat confusing mass of recursion here.

	while (fb == NULL || !fb->IsValid ())
	{
		if (fb != NULL)
		{
			delete fb;
		}

		switch (retry)
		{
		case 0:
			owidth = width;
			oheight = height;
		case 2:
			// Try a different resolution. Hopefully that will work.
			I_ClosestResolution (&width, &height, 8);
			break;

		case 1:
			// Try changing fullscreen mode. Maybe that will work.
			width = owidth;
			height = oheight;
			fullscreen = !fullscreen;
			break;

		default:
			// I give up!
			I_FatalError ("Could not create new screen (%d x %d)", owidth, oheight);
		}

		++retry;
		fb = static_cast<SDLFB *>(CreateFrameBuffer (width, height, fullscreen, NULL));
	}
	retry = 0;

	fb->SetFlash (flashColor, flashAmount);

	return fb;
}

void SDLVideo::SetWindowedScale (float scale)
{
}

// FrameBuffer implementation -----------------------------------------------

#if SDL_VERSION_ATLEAST(2,0,0)
SDLFB::SDLFB (int width, int height, bool fullscreen, SDL_Window *oldwin)
#else
SDLFB::SDLFB (int width, int height, bool fullscreen)
#endif
	: DFrameBuffer (width, height)
{
	int i;
	
	NeedPalUpdate = false;
	NeedGammaUpdate = false;
	UpdatePending = false;
	NotPaletted = false;
	FlashAmount = 0;

#if SDL_VERSION_ATLEAST(2,0,0)
	Renderer = NULL;
	Texture = NULL;

	if (oldwin)
	{
		// In some cases (Mac OS X fullscreen) SDL2 doesn't like having multiple windows which
		// appears to inevitably happen while compositor animations are running. So lets try
		// to reuse the existing window.
		Screen = oldwin;
		SDL_SetWindowSize (Screen, width, height);
		SetFullscreen (fullscreen);
	}
	else
	{
		// PS3: la ventana SIEMPRE a 1280x720 (resolucion real de la
		// consola), aunque el juego dibuje internamente mas chico
		// (width x height). SDL_RenderCopy estira la textura chica a
		// toda la ventana, asi que el escalado sale gratis y la imagen
		// ocupa la pantalla completa.
		// PS3: preguntamos al sistema que resolucion real tiene la
		// pantalla, en vez de asumir 720p.
		int winw = 1280, winh = 720;
		{
			SDL_DisplayMode dm;
			char b[128];
			if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
			{
				if (dm.w > 0 && dm.h > 0) { winw = dm.w; winh = dm.h; }
			}
			else
			{
			}
		}
		Screen = SDL_CreateWindow (GetGameCaption(),
			SDL_WINDOWPOS_UNDEFINED_DISPLAY(vid_adapter), SDL_WINDOWPOS_UNDEFINED_DISPLAY(vid_adapter),
			winw, winh, (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));

		if (Screen == NULL)
			return;
	}

#ifdef _WIN32
	extern void ForceSDLFocus(SDL_Window *win);
	ForceSDLFocus(Screen);
#endif

	ResetSDLRenderer ();

#ifdef __ANDROID__
	extern void PostSDLCreateRenderer(SDL_Window *);
	PostSDLCreateRenderer(Screen);
#endif

	for (i = 0; i < 256; i++)
	{
		GammaTable[0][i] = GammaTable[1][i] = GammaTable[2][i] = i;
	}
#else
	SDL_WM_SetCaption(GetGameCaption(), NULL);

	if(vid_displaybits == static_cast<unsigned>(-1))
	{
		const SDL_VideoInfo *vidInfo = SDL_GetVideoInfo();
		screenBits = vidInfo->vfmt->BitsPerPixel;
	}

	Screen = SDL_SetVideoMode (width, height, vid_displaybits,
		SDL_HWSURFACE|SDL_HWPALETTE|SDL_DOUBLEBUF|SDL_ANYFORMAT|
		(fullscreen ? SDL_FULLSCREEN : 0));

	if (Screen == NULL)
		return;

	for (i = 0; i < 256; i++)
	{
		GammaTable[0][i] = GammaTable[1][i] = GammaTable[2][i] = i;
	}
	if (Screen->format->palette == NULL)
	{
		NotPaletted = true;
		GPfx.SetFormat (Screen->format->BitsPerPixel,
			Screen->format->Rmask,
			Screen->format->Gmask,
			Screen->format->Bmask);
	}
#endif

	memcpy (SourcePalette, GPalette.BaseColors, sizeof(PalEntry)*256);
	UpdateColors ();

#ifdef __APPLE__
	SetVSync (vid_vsync);
#endif
}

SDLFB::~SDLFB ()
{
#if SDL_VERSION_ATLEAST(2,0,0)
	if (Renderer)
	{
		if (Texture)
			SDL_DestroyTexture (Texture);
		SDL_DestroyRenderer (Renderer);
	}

	if(Screen)
	{
		SDL_DestroyWindow (Screen);
	}
#endif
#ifdef I_AM_PS3
	// PS3 1.1: the RSX must be let go of before the process ends, or the
	// console goes down with it
	PS3_Display_Shutdown ();
#endif
}

bool SDLFB::IsValid ()
{
	return DFrameBuffer::IsValid() && Screen != NULL;
}

int SDLFB::GetPageCount ()
{
	return 1;
}

bool SDLFB::Lock (bool buffered)
{
	return DSimpleCanvas::Lock ();
}

bool SDLFB::Relock ()
{
	return DSimpleCanvas::Lock ();
}

void SDLFB::Unlock ()
{
	if (UpdatePending && LockCount == 1)
	{
		Update ();
	}
	else if (--LockCount <= 0)
	{
		Buffer = NULL;
		LockCount = 0;
	}
}

void SDLFB::Update ()
{
	if (LockCount != 1)
	{
		if (LockCount > 0)
		{
			UpdatePending = true;
			--LockCount;
		}
		return;
	}

	sysUtilCheckCallback();
	{
		// Cierre limpio pedido desde el XMB: lo hacemos aca, al principio
		// del frame, no dentro del callback.
		extern volatile int PS3_ExitRequested;
		if (PS3_ExitRequested)
		{
			SDL_Quit();
			exit(0);
		}
	}
	//  PS3: consultar el sysutil en CADA cuadro, no solo en IN_ProcessEvents (que no corre en menus bloqueados)
	Uint32 t_update_start = SDL_GetTicks();
	DrawRateStuff ();

	if (NeedGammaUpdate)
	{
		bool Windowed = false;
		NeedGammaUpdate = false;
		CalcGamma ((Windowed || rgamma == 0.f) ? Gamma : (Gamma * rgamma), GammaTable[0]);
		CalcGamma ((Windowed || ggamma == 0.f) ? Gamma : (Gamma * ggamma), GammaTable[1]);
		CalcGamma ((Windowed || bgamma == 0.f) ? Gamma : (Gamma * bgamma), GammaTable[2]);
		NeedPalUpdate = true;
	}

	if (NeedPalUpdate)
	{
		NeedPalUpdate = false;
		UpdateColors ();
	}

#if 0
#ifndef __APPLE__
	if(vid_maxfps && !cl_capfps)
	{
		SEMAPHORE_WAIT(FPSLimitSemaphore)
	}
#endif
#endif

	Buffer = NULL;
	LockCount = 0;
	UpdatePending = false;

	//BlitCycles.Reset();
	//SDLFlipCycles.Reset();
	//BlitCycles.Clock();

#ifdef I_AM_PS3
	// PS3 1.1: the 8 bit screen goes to the RSX as it is, drawn through
	// the palette there (ps3/source/ps3_display.c): no conversion to 32
	// bits on the CPU, no texture upload.
	PS3_Display_Present(MemBuffer, Pitch, Width, Height);
#elif SDL_VERSION_ATLEAST(2,0,0)
	void *pixels;
	int pitch;
	// PS3: las texturas viven en memoria de la GPU (rsxMemalign), donde
	// escribir con el CPU es lentisimo (sin cache). Armamos el cuadro en
	// RAM normal y lo subimos de una sola vez con SDL_UpdateTexture.
	static void *staging = NULL;
	static int staging_pitch = 0;
	static int staging_h = 0;
	if (UsingRenderer)
	{
		int need_pitch = Width * 4;
		if (!staging || staging_pitch != need_pitch || staging_h != Height)
		{
			if (staging) SDL_free(staging);
			staging = SDL_malloc(need_pitch * Height);
			staging_pitch = need_pitch;
			staging_h = Height;
		}
		if (!staging) return;
		pixels = staging;
		pitch = staging_pitch;
	}
	else
	{
		if (SDL_LockSurface (Surface))
			return;

		pixels = Surface->pixels;
		pitch = Surface->pitch;
	}

	Uint32 t_convert = SDL_GetTicks();
	if (NotPaletted)
	{
		GPfx.Convert (MemBuffer, Pitch,
			pixels, pitch, Width, Height,
			FRACUNIT, FRACUNIT, 0, 0);
	}
	else
	{
		if (pitch == Pitch)
		{
			memcpy (pixels, MemBuffer, Width*Height);
		}
		else
		{
			for (int y = 0; y < Height; ++y)
			{
				memcpy ((BYTE *)pixels+y*pitch, MemBuffer+y*Pitch, Width);
			}
		}
	}

	{
		// Contador de FPS, mismo enfoque que en TyrQuake: promedio real
		// sobre ventanas de 1 segundo, al log.
		static Uint32 fps_t0 = 0;
		static int fps_frames = 0;
		Uint32 now = SDL_GetTicks();
		if (fps_t0 == 0) fps_t0 = now;
		fps_frames++;
		if (now - fps_t0 >= 1000)
		{
			char b[96];
			{
				// Comparar el reloj de SDL contra el reloj real de la PS3:
				// si SDL corre mas rapido, el juego entero se acelera.
				static u64 ps3_t0 = 0;
				static Uint32 sdl_t0 = 0;
				u64 ps3_now = sysGetSystemTime();
				if (ps3_t0 == 0) { ps3_t0 = ps3_now; sdl_t0 = now; }
				else {
					char tb[160];
					snprintf(tb, sizeof(tb), "CLOCK: SDL=%u ms  PS3real=%llu ms  ratio=%.3f",
						(unsigned)(now - sdl_t0),
						(unsigned long long)((ps3_now - ps3_t0)/1000),
						(double)(now - sdl_t0) / ((ps3_now - ps3_t0)/1000.0));
				}
			}
			snprintf(b, sizeof(b), "FPS: %.1f (%d cuadros en %u ms)",
				fps_frames * 1000.0 / (now - fps_t0), fps_frames, (unsigned)(now - fps_t0));
			fps_t0 = now;
			fps_frames = 0;
		}
	}

	if (UsingRenderer)
	{
		Uint32 t_conv_done = SDL_GetTicks();
		SDL_UpdateTexture (Texture, NULL, staging, staging_pitch);
		Uint32 t_upload = SDL_GetTicks();

		//SDLFlipCycles.Clock();
		SDL_RenderClear(Renderer);
		{
			// Overscan ~10%: muchas TVs recortan los bordes, asi que
			// dibujamos la imagen con un 5% de margen por lado.
			int ww, wh;
			SDL_GetWindowSize(Screen, &ww, &wh);
			SDL_Rect dst;
			dst.x = ww * 5 / 100;
			dst.y = wh * 5 / 100;
			dst.w = ww - dst.x * 2;
			dst.h = wh - dst.y * 2;
			SDL_RenderCopy(Renderer, Texture, NULL, &dst);
		}

#ifdef __ANDROID__
		// Hack control overlay in
		extern void frameControls();
		frameControls();
#endif

		SDL_RenderPresent(Renderer);
		{
			static Uint32 acc_conv=0, acc_upl=0, acc_pres=0, acc_tot=0, n=0, last=0;
			Uint32 tend = SDL_GetTicks();
			acc_conv += t_conv_done - t_convert;
			acc_upl  += t_upload - t_conv_done;
			acc_pres += tend - t_upload;
			acc_tot  += tend - t_update_start;
			n++;
			if (tend - last >= 1000)
			{
				char b[160];
				snprintf(b, sizeof(b), "PERF: %u cuadros | convert=%ums upload=%ums present=%ums TOTAL_update=%ums",
					n, acc_conv/n, acc_upl/n, acc_pres/n, acc_tot/n);
				acc_conv=acc_upl=acc_pres=acc_tot=n=0; last=tend;
			}
		}
		//SDLFlipCycles.Unclock();
	}
	else
	{
		SDL_UnlockSurface (Surface);

		//SDLFlipCycles.Clock();
		SDL_UpdateWindowSurface (Screen);
		//SDLFlipCycles.Unclock();
	}

#else
	if (SDL_LockSurface (Screen) == -1)
		return;

	if (NotPaletted)
	{
		GPfx.Convert (MemBuffer, Pitch,
			Screen->pixels, Screen->pitch, Width, Height,
			FRACUNIT, FRACUNIT, 0, 0);
	}
	else
	{
		if (Screen->pitch == Pitch)
		{
			memcpy (Screen->pixels, MemBuffer, Width*Height);
		}
		else
		{
			for (int y = 0; y < Height; ++y)
			{
				memcpy ((BYTE *)Screen->pixels+y*Screen->pitch, MemBuffer+y*Pitch, Width);
			}
		}
	}
	
	SDL_UnlockSurface (Screen);

#if 0
	if (cursorSurface != NULL && GUICapture)
	{
		// SDL requires us to draw a surface to get true color cursors.
		SDL_BlitSurface(cursorSurface, NULL, Screen, &cursorBlit);
	}
#endif

	//SDLFlipCycles.Clock();
	SDL_Flip (Screen);
	//SDLFlipCycles.Unclock();
#endif

	//BlitCycles.Unclock();
}

void SDLFB::UpdateColors ()
{
	if (NotPaletted)
	{
		PalEntry palette[256];
		
		for (int i = 0; i < 256; ++i)
		{
			palette[i].r = GammaTable[0][SourcePalette[i].r];
			palette[i].g = GammaTable[1][SourcePalette[i].g];
			palette[i].b = GammaTable[2][SourcePalette[i].b];
		}
		if (FlashAmount)
		{
			DoBlending (palette, palette,
				256, GammaTable[0][Flash.r], GammaTable[1][Flash.g], GammaTable[2][Flash.b],
				FlashAmount);
		}
		GPfx.SetPalette (palette);
#ifdef I_AM_PS3
		{
			// PS3 1.1: the palette the RSX draws the screen with
			uint8_t rgb[768];

			for (int i = 0; i < 256; ++i)
			{
				rgb[i*3] = palette[i].r;
				rgb[i*3+1] = palette[i].g;
				rgb[i*3+2] = palette[i].b;
			}
			PS3_Display_SetPalette (rgb);
		}
#endif
	}
	else
	{
		SDL_Color colors[256];
		
		for (int i = 0; i < 256; ++i)
		{
			colors[i].r = GammaTable[0][SourcePalette[i].r];
			colors[i].g = GammaTable[1][SourcePalette[i].g];
			colors[i].b = GammaTable[2][SourcePalette[i].b];
		}
		if (FlashAmount)
		{
			DoBlending ((PalEntry *)colors, (PalEntry *)colors,
				256, GammaTable[2][Flash.b], GammaTable[1][Flash.g], GammaTable[0][Flash.r],
				FlashAmount);
		}
#if SDL_VERSION_ATLEAST(2,0,0)
		SDL_SetPaletteColors (Surface->format->palette, colors, 0, 256);
#else
		SDL_SetPalette (Screen, SDL_LOGPAL|SDL_PHYSPAL, colors, 0, 256);
#endif
	}
}

PalEntry *SDLFB::GetPalette ()
{
	return SourcePalette;
}

void SDLFB::UpdatePalette ()
{
	NeedPalUpdate = true;
}

bool SDLFB::SetGamma (float gamma)
{
	Gamma = gamma;
	NeedGammaUpdate = true;
	return true;
}

bool SDLFB::SetFlash (PalEntry rgb, int amount)
{
	Flash = rgb;
	FlashAmount = amount;
	NeedPalUpdate = true;
	return true;
}

void SDLFB::GetFlash (PalEntry &rgb, int &amount)
{
	rgb = Flash;
	amount = FlashAmount;
}

// Q: Should I gamma adjust the returned palette?
void SDLFB::GetFlashedPalette (PalEntry pal[256])
{
	memcpy (pal, SourcePalette, 256*sizeof(PalEntry));
	if (FlashAmount)
	{
		DoBlending (pal, pal, 256, Flash.r, Flash.g, Flash.b, FlashAmount);
	}
}

void SDLFB::SetFullscreen (bool fullscreen)
{
#ifdef __ANDROID__
	fullscreen = true;
#endif

#if SDL_VERSION_ATLEAST(2,0,0)
	if (IsFullscreen() == fullscreen)
		return;

	SDL_SetWindowFullscreen (Screen, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	if (!fullscreen)
	{
		// Restore proper window size
		SDL_SetWindowSize (Screen, Width, Height);
	}

	ResetSDLRenderer ();
#endif
}

bool SDLFB::IsFullscreen ()
{
#if SDL_VERSION_ATLEAST(2,0,0)
	return (SDL_GetWindowFlags (Screen) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
#else
	return (Screen->flags & SDL_FULLSCREEN) != 0;
#endif
}

void SDLFB::ResetSDLRenderer ()
{
#if SDL_VERSION_ATLEAST(2,0,0)
	if (Renderer)
	{
		if (Texture)
			SDL_DestroyTexture (Texture);
		SDL_DestroyRenderer (Renderer);
	}

	UsingRenderer = true; // PS3: probando el renderer para ver POR QUE falla
	if (UsingRenderer)
	{
		// PS3: el driver acelerado (RSX) NO anuncia TARGETTEXTURE en su
		// info estatica, aunque despues si la habilita al crearse. Si se
		// la pedimos, SDL descarta el driver antes de probarlo y caemos a
		// software. Sin ese flag, el driver por RSX si entra.
		Renderer = SDL_CreateRenderer (Screen, -1, SDL_RENDERER_ACCELERATED|
							(vid_vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
		if (!Renderer)
		{
			Renderer = SDL_CreateRenderer (Screen, -1, SDL_RENDERER_SOFTWARE);
			if (!Renderer)
			{
				UsingRenderer = false;
				Surface = SDL_GetWindowSurface (Screen);
				return;
			}
		}

		SDL_SetRenderDrawColor(Renderer, 0, 0, 0, 255);

		Uint32 fmt;
		switch(vid_displaybits)
		{
			default: fmt = SDL_PIXELFORMAT_ARGB8888; break;
			case 30: fmt = SDL_PIXELFORMAT_ARGB2101010; break;
			case 24: fmt = SDL_PIXELFORMAT_RGB888; break;
			case 16: fmt = SDL_PIXELFORMAT_RGB565; break;
			case 15: fmt = SDL_PIXELFORMAT_ARGB1555; break;
		}
		Texture = SDL_CreateTexture (Renderer, fmt, SDL_TEXTUREACCESS_STREAMING, Width, Height);

		{
			NotPaletted = true;

			Uint32 format;
			SDL_QueryTexture(Texture, &format, NULL, NULL, NULL);

			Uint32 Rmask, Gmask, Bmask, Amask;
			int bpp;
			SDL_PixelFormatEnumToMasks(format, &bpp, &Rmask, &Gmask, &Bmask, &Amask);
			GPfx.SetFormat (bpp, Rmask, Gmask, Bmask);
		}
	}
	else
	{
		Surface = SDL_GetWindowSurface (Screen);

		if (Surface->format->palette == NULL)
		{
			NotPaletted = true;
			GPfx.SetFormat (Surface->format->BitsPerPixel, Surface->format->Rmask, Surface->format->Gmask, Surface->format->Bmask);
		}
		else
			NotPaletted = false;
	}

	// In fullscreen, set logical size according to animorphic ratio.
	// Windowed modes are rendered to fill the window (usually 1:1)
	if (IsFullscreen ())
	{
		int w, h;
		SDL_GetWindowSize (Screen, &w, &h);
		ScaleWithAspect (w, h, Width, Height);
		SDL_RenderSetLogicalSize (Renderer, w, h);
	}
#endif
}

void SDLFB::SetVSync (bool vsync)
{
#if SDL_VERSION_ATLEAST(2,0,0)
	ResetSDLRenderer ();
#endif
}

void SDLFB::ScaleCoordsFromWindow(SWORD &x, SWORD &y)
{
#if SDL_VERSION_ATLEAST(2,0,0)
	int w, h;
	SDL_GetWindowSize (Screen, &w, &h);

	// Detect if we're doing scaling in the Window and adjust the mouse
	// coordinates accordingly. This could be more efficent, but I
	// don't think performance is an issue in the menus.
	if(IsFullscreen())
	{
		int realw = w, realh = h;
		ScaleWithAspect (realw, realh, SCREENWIDTH, SCREENHEIGHT);
		if (realw != SCREENWIDTH || realh != SCREENHEIGHT)
		{
			double xratio = (double)SCREENWIDTH/realw;
			double yratio = (double)SCREENHEIGHT/realh;
			if (realw < w)
			{
				x = (SWORD)((x - (w - realw)/2)*xratio);
				y = (SWORD)(y*yratio);
			}
			else
			{
				y = (SWORD)((y - (h - realh)/2)*yratio);
				x = (SWORD)(x*xratio);
			}
		}
	}
	else
	{
		x = (SWORD)(x*Width/w);
		y = (SWORD)(y*Height/h);
	}
#endif
}

#if 0
ADD_STAT (blit)
{
	FString out;
	out.Format ("blit=%04.1f ms  flip=%04.1f ms",
		BlitCycles.Time() * 1e-3, SDLFlipCycles.TimeMS());
	return out;
}
#endif
