/*
** r_gpu.h
**
** ECellWolf (PS3): the 3D view drawn by the RSX instead of the software
** renderer (Display > Renderer). See r_gpu.cpp.
*/

#ifndef __R_GPU_H__
#define __R_GPU_H__

#ifdef I_AM_PS3

#include "wl_def.h"

class FTexture;

extern bool r_gpu_renderer;     // Display > Renderer: GPU (true) or Software
extern bool r_gpu_smooth;       // Display > Smooth textures (GPU only)
extern bool r_gpu_capture;      // the sprite functions hand their sprites to r_gpu.cpp
extern bool r_gpu_nodraw;       // ScalePost draws nothing (the rays only find the visible tiles)

// true: the GPU draws this view (the GPU renderer is on and can work)
bool R_GPU_Active();

// After WallRefresh: walls, doors, floors, ceilings and sky of the view.
// The sprites follow (R_GPU_Add*), then R_GPU_FinishView puts it all on
// the RSX and fills the view window of the screen with the key color.
void R_GPU_BeginView();
void R_GPU_FinishView(byte *vbuf, unsigned vbufPitch);

// ScaleSprite: the screen rectangle (view pixels) the software renderer
// would fill, at the actor's depth (transx).
void R_GPU_AddSprite(FTexture *tex, bool flip, bool bright, double x0, double y0, double x1, double y1, fixed depth);
// Scale3DSprite: a flat sprite in the world, from (x1, y1) to (x2, y2), its
// top and bottom in height units (64 a tile).
void R_GPU_AddFlatSprite(FTexture *tex, bool flip, bool bright, fixed x1, fixed y1, fixed x2, fixed y2, double top, double bottom);
// R_DrawPlayerSprite: the weapon, a screen rectangle with a fixed colormap row.
void R_GPU_AddPlayerSprite(FTexture *tex, int row, double x0, double y0, double x1, double y1);

// A new level (or the renderer changed): textures and the last view go.
void R_GPU_NewLevel();

// While a fizzle fade shows the last view without new frames.
void R_GPU_HoldView(bool on);

#endif

#endif
