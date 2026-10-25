/*
** r_gpu.cpp
**
** ECellWolf (PS3): the 3D view on the RSX (ps3/source/ps3_gpu.c), the
** same picture the software renderer of wl_draw.cpp draws:
**
**  - the software ray caster still runs, without drawing (r_gpu_nodraw):
**    it marks the tiles the view sees, and only those (and the ones next
**    to them) go to the RSX;
**  - walls, doors and moving pushwalls are textured quads with the
**    software renderer's texture coordinates, floors and ceilings one
**    quad per tile, the sky screen strips by view angle (wl_parallax.cpp);
**  - sprites are the rectangles ScaleSprite would fill, at the actor's
**    depth; flat sprites (Scale3DSprite) quads in the world; the weapon a
**    screen rectangle;
**  - light is wl_shade.h's: a colormap row from the light level minus
**    the visibility by depth, in the fragment program.
**
** The view window of the 8 bit screen is filled with a key color, the
** engine draws its 2D over it (status bar at full size, messages, PAUSED)
** and ps3_display.c puts the screen over the 3D leaving the key out.
**
** Projection. A world point (x, y in tiles, h in height units: 64 a tile,
** 0 = the floor) in front of the view: depth X and side Y as TransformActor
** (wl_draw.cpp) does it, on the screen
**     sx = viewwidth/2 + scale * Y / X
**     sy = viewheight/2 - viewshift - scale * yaspect * (h + viewz) / (64 * X)
** the same as the walls of ScalePost and CalcHeight (half a pixel over
** each way, see R_GPU_BeginView).
*/

#include "wl_def.h"

#ifdef I_AM_PS3

#include <algorithm>
#include <map>
#include <vector>

#include "actor.h"
#include "gamemap.h"
#include "g_mapinfo.h"
#include "id_ca.h"
#include "r_data/colormaps.h"
#include "textures/textures.h"
#include "wl_agent.h"
#include "wl_draw.h"
#include "wl_game.h"
#include "wl_main.h"
#include "wl_play.h"
#include "wl_shade.h"
#include "r_gpu.h"

#include "ps3_gpu.h"
#include "ps3_platform.h"

extern fixed viewz;
extern int viewshift;
extern short midangle;
extern short focaltx, focalty, viewtx, viewty;

bool r_gpu_renderer = true;
bool r_gpu_smooth = false;
bool r_gpu_capture = false;
bool r_gpu_nodraw = false;

namespace
{

enum { MAT_WORLD, MAT_SCREEN };

struct Batch
{
	gpu_draw_t draw;
	int matrix;
	unsigned int first, count;
};

// The view drawn last: kept to draw it again (PAUSED, fizzle fades).
std::vector<gpu_vert_t> verts;
std::vector<Batch> batches;
float matWorld[16], matScreen[16];

enum TexKind { TEX_WALL, TEX_FLAT, TEX_SPRITE };
typedef std::map<std::pair<FTexture *, int>, gpu_tex_t *> TexCache;
TexCache texCache;

BYTE colormaps[NUMCOLORMAPS*256];
bool colormapsSet = false;

// This view
float lightRow;        // LIGHT2SHADE(level light + extra light), colormap rows
float visibility;      // rows of darkness at a depth of one tile
float maxLightVis;     // the most of it
double viewX, viewY, viewCos, viewSin, viewZ;
double halfWidth, centerY;
double projScale, projYAspect;

// Grouped by texture until the end of BeginView
typedef std::map<gpu_tex_t *, std::vector<gpu_vert_t> > Bucket;
Bucket wallBucket, flatBucket, maskedFlatBucket;

gpu_tex_t *GetTexture(FTexture *tex, TexKind kind)
{
	if(!tex)
		return NULL;

	const std::pair<FTexture *, int> key(tex, kind);
	TexCache::iterator it = texCache.find(key);
	if(it != texCache.end())
		return it->second;

	const int w = tex->GetWidth();
	const int h = tex->GetHeight();
	int tw = w, th = h;

	// A wall's columns repeat every 64 * xScale texels, its rows every
	// 64 * yScale (ScalePost): the texture gets exactly that size.
	if(kind == TEX_WALL)
	{
		const int ww = (int)(64.0*tex->xScale/FRACUNIT + 0.5);
		const int hh = (int)(64.0*tex->yScale/FRACUNIT + 0.5);
		if(ww > 0 && ww <= 4096)
			tw = ww;
		if(hh > 0 && hh <= 4096)
			th = hh;
	}

	gpu_tex_t *gt = NULL;
	const BYTE *pixels = tex->GetPixels();
	if(pixels && w > 0 && h > 0)
	{
		// FTexture pixels go by columns, the RSX's by rows
		std::vector<uint8_t> index(tw*th), mask;
		for(int y = 0; y < th; ++y)
			for(int x = 0; x < tw; ++x)
				index[y*tw + x] = pixels[(x % w)*h + (y % h)];

		const bool masked = kind == TEX_SPRITE || (kind == TEX_FLAT && tex->bMasked);
		if(masked)
		{
			mask.resize(tw*th);
			for(int i = 0; i < tw*th; ++i)
				mask[i] = index[i] != 0;
		}

		gt = GPU_TexCreate(tw, th, &index[0], masked ? &mask[0] : NULL, NULL);
	}

	texCache[key] = gt;
	return gt;
}

gpu_draw_t WorldDraw(gpu_tex_t *tex)
{
	gpu_draw_t d;
	memset(&d, 0, sizeof(d));
	d.tex = tex;
	d.prog = GPU_PROG_WORLD;
	d.blend = GPU_BLEND_NONE;
	d.depth_test = 1;
	d.depth_write = 1;
	d.color_write = 1;
	d.wrap = 1;
	d.stencil = GPU_STENCIL_OFF;
	d.fixed_row = -1.0f;
	d.vis = visibility;
	d.cap = maxLightVis;
	d.ecwolf_light = 1;
	d.minvis = (float)MINZ/FRACUNIT;
	return d;
}

void AddBatch(const gpu_draw_t &d, int matrix, const std::vector<gpu_vert_t> &v)
{
	if(v.size() < 3)
		return;

	Batch b;
	b.draw = d;
	b.matrix = matrix;
	b.first = verts.size();
	b.count = v.size();
	verts.insert(verts.end(), v.begin(), v.end());
	batches.push_back(b);
}

inline gpu_vert_t Vert(double x, double y, double z, double u, double v)
{
	gpu_vert_t r;
	r.x = (float)x;
	r.y = (float)y;
	r.z = (float)z;
	r.w = 1.0f;
	r.u = (float)u;
	r.v = (float)v;
	r.light = lightRow;
	r.maxrow = NUMCOLORMAPS-1;
	r.color = 0xffffffffu;
	return r;
}

// a, b, c, d around the quad: two triangles
void Quad(std::vector<gpu_vert_t> &out, const gpu_vert_t &a, const gpu_vert_t &b, const gpu_vert_t &c, const gpu_vert_t &d)
{
	out.push_back(a);
	out.push_back(b);
	out.push_back(c);
	out.push_back(a);
	out.push_back(c);
	out.push_back(d);
}

// A vertical quad from (x0, y0) to (x1, y1), floor to ceiling, texture
// columns u0 to u1 (texture widths), rows as ScalePost: the bottom row at
// the floor, one texel a height unit at a scale of 1.
void WallQuad(std::vector<gpu_vert_t> &out, double x0, double y0, double u0, double x1, double y1, double u1, double ceiling)
{
	const double vtop = 1.0 - ceiling/64.0;
	Quad(out,
		Vert(x0, y0, ceiling, u0, vtop), Vert(x1, y1, ceiling, u1, vtop),
		Vert(x1, y1, 0, u1, 1.0), Vert(x0, y0, 0, u0, 1.0));
}

// One side of a tile sized box at (bx, by): the texture as HitVertWall and
// HitHorizWall put it (west and south faces left to right with x / y, east
// and north faces mirrored), from the box's corner.
void BoxFace(double bx, double by, MapTile::Side side, FTexture *tex, double ceiling)
{
	gpu_tex_t *gt = GetTexture(tex, TEX_WALL);
	if(!gt)
		return;

	std::vector<gpu_vert_t> &out = wallBucket[gt];
	switch(side)
	{
		case MapTile::West:
			WallQuad(out, bx, by, 0.0, bx, by+1, 1.0, ceiling);
			break;
		case MapTile::East:
			WallQuad(out, bx+1, by, 1.0, bx+1, by+1, 0.0, ceiling);
			break;
		case MapTile::North:
			WallQuad(out, bx, by, 1.0, bx+1, by, 0.0, ceiling);
			break;
		case MapTile::South:
			WallQuad(out, bx, by+1, 0.0, bx+1, by+1, 1.0, ceiling);
			break;
	}
}

bool IsDoor(MapSpot spot)
{
	return spot->tile && (spot->tile->offsetVertical || spot->tile->offsetHorizontal);
}

// A pushwall on its way: the spot it leaves (it draws the block)
bool IsPushSource(MapSpot spot)
{
	if(!spot->tile)
		return false;
	if(spot->pushAmount)
		return true;
	MapSpot next = spot->GetAdjacent(spot->pushDirection);
	return next && next->pushReceptor == spot;
}

// Not a plain solid tile: the sides of the walls next to it can be seen
bool IsOpen(MapSpot spot)
{
	return !spot->tile || IsDoor(spot) || spot->pushReceptor || IsPushSource(spot);
}

MapTile::Side Opposite(int side)
{
	return MapTile::Side((side+2)%4);
}

// The door on the tile's center line, the side the view sees (both sides
// of a door show its texture the same way round, as in the software
// renderer), the slid part left out (CheckSlidePass, SlideTextureOffset).
void Door(MapSpot spot, unsigned int tx, unsigned int ty, double ceiling)
{
	const bool vertical = spot->tile->offsetVertical;
	MapTile::Side side;
	if(vertical)
		side = viewX < tx + 0.5 ? MapTile::West : MapTile::East;
	else
		side = viewY < ty + 0.5 ? MapTile::North : MapTile::South;

	gpu_tex_t *gt = GetTexture(TexMan(spot->texture[side]), TEX_WALL);
	if(!gt)
		return;

	const double a = spot->slideAmount[side]/(double)FRACUNIT;
	double seg[2][3];  // from, to, texture offset
	int segs = 1;
	seg[0][0] = 0.0; seg[0][1] = 1.0; seg[0][2] = 0.0;
	if(a > 0.0)
	{
		switch(spot->slideStyle)
		{
			default:
				seg[0][0] = a; seg[0][2] = -a;
				break;
			case SLIDE_Split:
				seg[0][1] = (1.0-a)/2; seg[0][2] = a/2;
				seg[1][0] = (1.0+a)/2; seg[1][1] = 1.0; seg[1][2] = -a/2;
				segs = 2;
				break;
			case SLIDE_Invert:
				seg[0][1] = 1.0-a; seg[0][2] = a;
				break;
		}
	}

	std::vector<gpu_vert_t> &out = wallBucket[gt];
	for(int i = 0; i < segs; ++i)
	{
		const double from = seg[i][0], to = seg[i][1], off = seg[i][2];
		if(to - from <= 0.0)
			continue;
		if(vertical)
			WallQuad(out, tx+0.5, ty+from, from+off, tx+0.5, ty+to, to+off, ceiling);
		else
			WallQuad(out, tx+from, ty+0.5, from+off, tx+to, ty+0.5, to+off, ceiling);
	}
}

// Floor or ceiling of a tile (R_DrawPlane's texture coordinates)
void Flat(unsigned int tx, unsigned int ty, FTextureID id, double h)
{
	if(!id.isValid())
		return;

	FTexture *tex = TexMan(id);
	gpu_tex_t *gt = GetTexture(tex, TEX_FLAT);
	if(!gt)
		return;

	double su = 1.0, sv = 1.0, ou = 0.0, ov = 0.0;
	const int w = tex->GetWidth(), th = tex->GetHeight();
	if(!(w == 64 && th == 64 && tex->xScale == FRACUNIT && tex->yScale == FRACUNIT))
	{
		// R_DrawPlane's other path: half a texel over and v the other way
		su = 64.0*tex->xScale/FRACUNIT/w;
		sv = -64.0*tex->yScale/FRACUNIT/th;
		ou = -1.0/128;
		ov = 1.0/128;
	}

	std::vector<gpu_vert_t> &out = (tex->bMasked ? maskedFlatBucket : flatBucket)[gt];
	const double x0 = tx, y0 = ty, x1 = tx+1, y1 = ty+1;
	Quad(out,
		Vert(x0, y0, h, (x0+ou)*su, (y0+ov)*sv), Vert(x1, y0, h, (x1+ou)*su, (y0+ov)*sv),
		Vert(x1, y1, h, (x1+ou)*su, (y1+ov)*sv), Vert(x0, y1, h, (x0+ou)*su, (y1+ov)*sv));
}

// wl_parallax.cpp: columns by view angle, rows by screen line, unlit
void Sky()
{
	FTextureID skyid = levelInfo->Sky;
	double scrollSpeed = levelInfo->SkyScrollSpeed;
	int horizonOffset = levelInfo->SkyHorizonOffset;

	if(map->GetHeader().sky.isValid())
	{
		skyid = map->GetHeader().sky;
		horizonOffset = map->GetHeader().skyHorizonOffset;
		scrollSpeed = 0.0;
	}

	if(!skyid.isValid())
		return;

	FTexture * const skysource = TexMan(skyid);
	gpu_tex_t *gt = GetTexture(skysource, TEX_FLAT);
	if(!gt)
		return;

	const int skyheight = skysource->GetScaledHeight();
	int skyhorizon = skyheight;
	if(skyheight >= 100 && skyheight <= 128)
		skyhorizon = 100;
	else if(skyheight > 128 && skyheight <= 200)
		skyhorizon = skyheight-28;
	else if(skyheight > 200)
		skyhorizon = 196;
	skyhorizon -= horizonOffset;

	const angle_t scroll = (angle_t)(int)(scrollSpeed*gamestate.TimeCount*(1<<27)/TICRATE);
	const int skymid = (players[ConsolePlayer].camera->angle + scroll)>>ANGLETOFINESHIFT;
	const int skyscaledheight = ((MAX(100, skyheight)*pspriteyscale)+(FRACUNIT-1))>>FRACBITS;
	const int skyscaledhorizon = (skyhorizon*skyscaledheight+skyheight-1)/skyheight;
	int yshift = skyscaledhorizon - (viewheight>>1) + viewshift;
	if(yshift < 0)
		yshift = skyscaledheight - ((-yshift) % skyscaledheight);

	const int cycle = skysource->GetScaledHeight() < 100 ? FINEANGLES : MIN(FINEANGLES, FINEANGLES*skysource->GetScaledWidth()/1024);
	const double v0 = (double)yshift/skyscaledheight;
	const double v1 = (double)(viewheight + yshift)/skyscaledheight;

	std::vector<gpu_vert_t> out;
	const int step = 4;
	double xprev = 0, uprev = 0;
	for(int x = 0; x <= viewwidth; x += step)
	{
		const int col = MIN(x, viewwidth-1);
		const int curang = pixelangle[col] + skymid;
		const double u = (double)(FINEANGLES - curang - 1)/cycle;
		if(x > 0)
		{
			Quad(out,
				Vert(xprev, 0, 0, uprev, v0), Vert(x, 0, 0, u, v0),
				Vert(x, viewheight, 0, u, v1), Vert(xprev, viewheight, 0, uprev, v1));
		}
		xprev = x;
		uprev = u;
		if(x < viewwidth && x + step > viewwidth)
			x = viewwidth - step;
	}

	gpu_draw_t d = WorldDraw(gt);
	d.depth_test = 0;
	d.depth_write = 0;
	d.fixed_row = 0.0f;
	AddBatch(d, MAT_SCREEN, out);
}

void FlushBucket(Bucket &bucket, const gpu_draw_t &proto)
{
	for(Bucket::iterator it = bucket.begin(); it != bucket.end(); ++it)
	{
		gpu_draw_t d = proto;
		d.tex = it->first;
		AddBatch(d, MAT_WORLD, it->second);
	}
	bucket.clear();
}

void SetupMatrices()
{
	// world: (x, y, h, 1) -> clip; x right, y up, z -w..w, w = depth
	const double n = 1.0/64, f = 512.0;
	const double Xr[4] = { viewCos, -viewSin, 0.0, -viewCos*viewX + viewSin*viewY };
	const double Yr[4] = { viewSin, viewCos, 0.0, -viewSin*viewX - viewCos*viewY };
	const double A = 2.0*projScale/viewwidth;
	const double B = 2.0*halfWidth/viewwidth - 1.0;
	const double C = 2.0*projScale*projYAspect/(64.0*viewheight);
	const double D = 1.0 - 2.0*centerY/viewheight;
	const double za = (f+n)/(f-n), zb = -2.0*f*n/(f-n);

	for(int i = 0; i < 4; ++i)
	{
		matWorld[0*4+i] = (float)(A*Yr[i] + B*Xr[i]);
		matWorld[1*4+i] = (float)(D*Xr[i]);
		matWorld[2*4+i] = (float)(za*Xr[i]);
		matWorld[3*4+i] = (float)Xr[i];
	}
	matWorld[1*4+2] = (float)C;
	matWorld[1*4+3] += (float)(C*viewZ);
	matWorld[2*4+3] += (float)zb;

	// screen: view pixels -> clip
	memset(matScreen, 0, sizeof(matScreen));
	matScreen[0] = 2.0f/viewwidth;
	matScreen[3] = -1.0f;
	matScreen[5] = -2.0f/viewheight;
	matScreen[7] = 1.0f;
	matScreen[15] = 1.0f;
}

// A view point (screen x, y at a depth) back to the world
void Unproject(double sx, double sy, double depth, double &x, double &y, double &h)
{
	const double side = (sx - halfWidth)*depth/projScale;
	h = (centerY - sy)*64.0*depth/(projScale*projYAspect) - viewZ;
	x = viewX + depth*viewCos + side*viewSin;
	y = viewY - depth*viewSin + side*viewCos;
}

void Replay()
{
	int matrix = -1;

	GPU_SetSmooth(r_gpu_smooth);
	for(size_t i = 0; i < batches.size(); ++i)
	{
		const Batch &b = batches[i];
		if(b.matrix != matrix)
		{
			matrix = b.matrix;
			GPU_SetMatrix(matrix == MAT_WORLD ? matWorld : matScreen);
		}
		GPU_DrawTris(&b.draw, &verts[b.first], b.count);
	}
}

} // namespace

bool R_GPU_Active()
{
	return r_gpu_renderer && GPU_Ready() && PS3_Display_Key() >= 0;
}

void R_GPU_BeginView()
{
	verts.clear();
	batches.clear();

	if(!colormapsSet || memcmp(colormaps, NormalLight.Maps, sizeof(colormaps)) != 0)
	{
		memcpy(colormaps, NormalLight.Maps, sizeof(colormaps));
		GPU_SetColormaps(colormaps, NUMCOLORMAPS);
		colormapsSet = true;
	}

	lightRow = (float)LIGHT2SHADE(gLevelLight + r_extralight)/FRACUNIT;
	visibility = (float)((double)r_depthvisibility*heightnumerator/4294967296.0);
	maxLightVis = (float)gLevelMaxLightVis/FRACUNIT;

	viewX = (double)viewx/FRACUNIT;
	viewY = (double)viewy/FRACUNIT;
	viewCos = (double)viewcos/FRACUNIT;
	viewSin = (double)viewsin/FRACUNIT;
	viewZ = (double)viewz/FRACUNIT;
	// half a pixel each way: the edges land on the software renderer's
	// rows and columns most often (measured against it)
	halfWidth = viewwidth/2 + 0.5;
	centerY = (viewheight/2) - viewshift - 0.5;
	projScale = scale;
	projYAspect = (double)yaspect/FRACUNIT;
	SetupMatrices();

	Sky();

	// The tiles the rays went through, and the ones next to them (a floor
	// pixel can be in a tile its column's ray only grazed)
	const unsigned int mw = map->GetHeader().width;
	const unsigned int mh = map->GetHeader().height;
	const double ceiling = map->GetPlane(0).depth;
	std::vector<uint8_t> seen(mw*mh, 0);
	for(unsigned int y = 0; y < mh; ++y)
	{
		for(unsigned int x = 0; x < mw; ++x)
		{
			const bool focal = (x == (unsigned)focaltx && y == (unsigned)focalty) ||
				(x == (unsigned)viewtx && y == (unsigned)viewty);
			if(!focal && !map->GetSpot(x, y, 0)->visible)
				continue;
			for(unsigned int yy = y ? y-1 : 0; yy <= y+1 && yy < mh; ++yy)
				for(unsigned int xx = x ? x-1 : 0; xx <= x+1 && xx < mw; ++xx)
					seen[yy*mw + xx] = 1;
		}
	}

	for(unsigned int y = 0; y < mh; ++y)
	{
		for(unsigned int x = 0; x < mw; ++x)
		{
			MapSpot spot = map->GetSpot(x, y, 0);

			// pushwalls on their way: the whole block, wherever it is
			if(IsPushSource(spot))
			{
				const double p = spot->pushAmount/64.0;
				double bx = x, by = y;
				switch(spot->pushDirection)
				{
					case MapTile::East: bx += p; break;
					case MapTile::West: bx -= p; break;
					case MapTile::South: by += p; break;
					case MapTile::North: by -= p; break;
				}
				for(int side = 0; side < 4; ++side)
					BoxFace(bx, by, MapTile::Side(side), TexMan(spot->texture[side]), ceiling);
			}

			if(!seen[y*mw + x] || !IsOpen(spot))
				continue;

			if(IsDoor(spot))
				Door(spot, x, y, ceiling);

			if(spot->sector)
			{
				Flat(x, y, spot->sector->texture[MapSector::Floor], 0.0);
				Flat(x, y, spot->sector->texture[MapSector::Ceiling], ceiling);
			}

			// the sides of the walls around it facing it
			for(int side = 0; side < 4; ++side)
			{
				MapSpot wall = spot->GetAdjacent(MapTile::Side(side));
				if(!wall || IsOpen(wall))
					continue;

				const MapTile::Side face = Opposite(side);
				FTextureID texid = wall->texture[face];
				// a door frame: the door's own side texture (HitVertWall)
				if(IsDoor(spot))
				{
					const bool eastwest = face == MapTile::East || face == MapTile::West;
					if(eastwest ? (spot->tile->offsetHorizontal && !spot->tile->offsetVertical)
						: (spot->tile->offsetVertical && !spot->tile->offsetHorizontal))
						texid = spot->texture[face];
				}

				BoxFace(wall->GetX(), wall->GetY(), face, TexMan(texid), ceiling);
			}
		}
	}

	// walls first, then the flats (depth tested: hidden ones cost nothing)
	FlushBucket(wallBucket, WorldDraw(NULL));
	gpu_draw_t flat = WorldDraw(NULL);
	flat.minvis = 0.0f;
	FlushBucket(flatBucket, flat);
	FlushBucket(maskedFlatBucket, flat);
}

// The software sprite loops step the texture from the first pixel's
// corner, the RSX samples at pixel centers: half a pixel over, the same
// texels.
const double TEXEL_ALIGN = 0.5;

void R_GPU_AddSprite(FTexture *tex, bool flip, bool bright, double x0, double y0, double x1, double y1, fixed depth)
{
	gpu_tex_t *gt = GetTexture(tex, TEX_SPRITE);
	if(!gt)
		return;

	x0 += TEXEL_ALIGN; x1 += TEXEL_ALIGN;
	y0 += TEXEL_ALIGN; y1 += TEXEL_ALIGN;

	const double X = (double)depth/FRACUNIT;
	double px[4], py[4], ph[4];
	Unproject(x0, y0, X, px[0], py[0], ph[0]);
	Unproject(x1, y0, X, px[1], py[1], ph[1]);
	Unproject(x1, y1, X, px[2], py[2], ph[2]);
	Unproject(x0, y1, X, px[3], py[3], ph[3]);
	const double u0 = flip ? 1.0 : 0.0, u1 = 1.0 - u0;

	std::vector<gpu_vert_t> out;
	Quad(out,
		Vert(px[0], py[0], ph[0], u0, 0.0), Vert(px[1], py[1], ph[1], u1, 0.0),
		Vert(px[2], py[2], ph[2], u1, 1.0), Vert(px[3], py[3], ph[3], u0, 1.0));

	gpu_draw_t d = WorldDraw(gt);
	d.wrap = 0;
	if(bright)
		d.fixed_row = 0.0f;
	AddBatch(d, MAT_WORLD, out);
}

void R_GPU_AddFlatSprite(FTexture *tex, bool flip, bool bright, fixed x1, fixed y1, fixed x2, fixed y2, double top, double bottom)
{
	gpu_tex_t *gt = GetTexture(tex, TEX_SPRITE);
	if(!gt)
		return;

	double ax = (double)x1/FRACUNIT, ay = (double)y1/FRACUNIT;
	double bx = (double)x2/FRACUNIT, by = (double)y2/FRACUNIT;

	// Scale3DSpriter starts the texture at the end on the left of the
	// screen, whichever way the sprite faces
	const double adepth = (ax-viewX)*viewCos - (ay-viewY)*viewSin;
	const double aside = (ax-viewX)*viewSin + (ay-viewY)*viewCos;
	const double bdepth = (bx-viewX)*viewCos - (by-viewY)*viewSin;
	const double bside = (bx-viewX)*viewSin + (by-viewY)*viewCos;
	if(adepth > 0 && bdepth > 0 && bside/bdepth < aside/adepth)
	{
		std::swap(ax, bx);
		std::swap(ay, by);
	}

	const double u0 = flip ? 1.0 : 0.0, u1 = 1.0 - u0;
	std::vector<gpu_vert_t> out;
	Quad(out,
		Vert(ax, ay, top, u0, 0.0), Vert(bx, by, top, u1, 0.0),
		Vert(bx, by, bottom, u1, 1.0), Vert(ax, ay, bottom, u0, 1.0));

	gpu_draw_t d = WorldDraw(gt);
	d.wrap = 0;
	if(bright)
		d.fixed_row = 0.0f;
	AddBatch(d, MAT_WORLD, out);
}

void R_GPU_AddPlayerSprite(FTexture *tex, int row, double x0, double y0, double x1, double y1)
{
	gpu_tex_t *gt = GetTexture(tex, TEX_SPRITE);
	if(!gt)
		return;

	x0 += TEXEL_ALIGN; x1 += TEXEL_ALIGN;
	y0 += TEXEL_ALIGN; y1 += TEXEL_ALIGN;

	std::vector<gpu_vert_t> out;
	Quad(out,
		Vert(x0, y0, 0, 0.0, 0.0), Vert(x1, y0, 0, 1.0, 0.0),
		Vert(x1, y1, 0, 1.0, 1.0), Vert(x0, y1, 0, 0.0, 1.0));

	gpu_draw_t d = WorldDraw(gt);
	d.wrap = 0;
	d.depth_test = 0;
	d.depth_write = 0;
	d.fixed_row = (float)row;
	AddBatch(d, MAT_SCREEN, out);
}

void R_GPU_FinishView(byte *vbuf, unsigned vbufPitch)
{
	const int key = PS3_Display_Key();
	for(int y = 0; y < viewheight; ++y)
		memset(vbuf + y*vbufPitch, key, viewwidth);

	PS3_Display_BeginView(screenWidth, screenHeight, viewscreenx, viewscreeny, viewwidth, viewheight);
	Replay();
	PS3_Display_EndView();
	PS3_Display_SetReplay(Replay);
}

void R_GPU_NewLevel()
{
	PS3_Display_DropView();
	verts.clear();
	batches.clear();

	for(TexCache::iterator it = texCache.begin(); it != texCache.end(); ++it)
	{
		if(it->second)
			GPU_TexFree(it->second);
	}
	texCache.clear();
}

void R_GPU_HoldView(bool on)
{
	PS3_Display_HoldView(on);
}

#endif
