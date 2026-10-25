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
 * The game picker shown before the engine starts (1.1): one package for
 * Wolfenstein 3D and Spear of Destiny, instead of one each.
 *
 * It lists the game data sets in USRDIR (and in the folder of 1.0's
 * Spear of Destiny package, ECWOLFSOD, so an existing install keeps
 * working) by their files' extension -- WL6, WL1, SOD, SD2, SD3, SDM,
 * N3D -- and, on a MODS tab, every folder in USRDIR/mods/ (its .pk3,
 * .zip, .wad files loaded with --file; an optional mod.txt gives the name
 * shown, "name = ...", and the game it needs, "data = wl6"). The choice
 * becomes ECWolf's command line (--data <extension>). With one thing to
 * play the menu is skipped.
 *
 * Quit from the game's menu comes back here, with a fresh process (the
 * marker USRDIR/ps3_return_to_launcher makes the menu show even with one
 * entry; CIRCLE then goes to the XMB): RELOAD.SELF, the same program as a
 * plain SELF, since sysProcessExitSpawn2 cannot start the NPDRM
 * EBOOT.BIN. "Quit Game" from the XMB quits for good.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

#ifdef __PPU__
#include <sys/process.h>
#endif

#include "ps3_gpu.h"
#include "ps3_launcher.h"
#include "ps3_platform.h"

#define LEGACY_SOD_DIR "/dev_hdd0/game/ECWOLFSOD/USRDIR"
#define MODS_DIR       PS3_USRDIR "/mods"
#define RETURN_MARKER  PS3_USRDIR "/ps3_return_to_launcher"
#define RELOAD_SELF    PS3_USRDIR "/RELOAD.SELF"

volatile int ps3_launcher_active;
volatile int ps3_launcher_quit_requested;

#define LAUNCHER_W   640
#define LAUNCHER_H   400
#define MAX_ENTRIES  48
#define MAX_NAME     48
#define MAX_FILES    8
#define MAX_PATH_LEN 256
#define MAX_ARGS     (4 + 2 * MAX_FILES)
#define VISIBLE_ROWS 9
#define GLYPH_W      8
#define GLYPH_H      8
#define FONT_FIRST   32
#define FONT_LAST    95

#define COL_BG      0xff101018
#define COL_TEXT    0xffc8c8c0
#define COL_DIM     0xff707068
#define COL_SEL_BG  0xff803018
#define COL_SEL_TX  0xffffffff
#define COL_TITLE   0xffd08040

/* 8x8 glyphs for ASCII 32..95 (printable, uppercase only -- lowercase
   is folded to uppercase at draw time), CrispyCell's launcher font */
static const unsigned char launcher_font[FONT_LAST - FONT_FIRST + 1][8] =
{
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // 'space'
    { 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00 },  // '!'
    { 0x36, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '"'
    { 0x36, 0x36, 0xfe, 0x36, 0xfe, 0x36, 0x36, 0x00 },  // '#'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '$'
    { 0xe2, 0xd2, 0xe4, 0x08, 0x3b, 0x2b, 0x3b, 0x00 },  // '%'
    { 0x38, 0x6c, 0x38, 0x76, 0xdc, 0xcc, 0x3d, 0x00 },  // '&'
    { 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '''
    { 0x0c, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0c, 0x00 },  // '('
    { 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x18, 0x30, 0x00 },  // ')'
    { 0x00, 0x2a, 0x1c, 0x3e, 0x1c, 0x2a, 0x00, 0x00 },  // '*'
    { 0x00, 0x18, 0x18, 0x7e, 0x18, 0x18, 0x00, 0x00 },  // '+'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30 },  // ','
    { 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00 },  // '-'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00 },  // '.'
    { 0x02, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x40, 0x00 },  // '/'
    { 0x3c, 0x66, 0x6e, 0x7e, 0x76, 0x66, 0x3c, 0x00 },  // '0'
    { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00 },  // '1'
    { 0x3c, 0x66, 0x06, 0x0c, 0x18, 0x30, 0x7e, 0x00 },  // '2'
    { 0x7e, 0x0c, 0x18, 0x0c, 0x06, 0x66, 0x3c, 0x00 },  // '3'
    { 0x0e, 0x1e, 0x36, 0x66, 0x7f, 0x06, 0x0f, 0x00 },  // '4'
    { 0x7e, 0x60, 0x7c, 0x06, 0x06, 0x66, 0x3c, 0x00 },  // '5'
    { 0x1c, 0x30, 0x60, 0x7c, 0x66, 0x66, 0x3c, 0x00 },  // '6'
    { 0x7e, 0x66, 0x0c, 0x18, 0x18, 0x18, 0x18, 0x00 },  // '7'
    { 0x3c, 0x66, 0x66, 0x3c, 0x66, 0x66, 0x3c, 0x00 },  // '8'
    { 0x3c, 0x66, 0x66, 0x3e, 0x06, 0x0c, 0x38, 0x00 },  // '9'
    { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00 },  // ':'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // ';'
    { 0x0e, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0e, 0x00 },  // '<'
    { 0x00, 0x00, 0x7e, 0x00, 0x7e, 0x00, 0x00, 0x00 },  // '='
    { 0x70, 0x18, 0x0c, 0x06, 0x0c, 0x18, 0x70, 0x00 },  // '>'
    { 0x3c, 0x66, 0x0c, 0x18, 0x18, 0x00, 0x18, 0x00 },  // '?'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '@'
    { 0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x00 },  // 'A'
    { 0x7c, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x7c, 0x00 },  // 'B'
    { 0x3c, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3c, 0x00 },  // 'C'
    { 0x78, 0x6c, 0x66, 0x66, 0x66, 0x6c, 0x78, 0x00 },  // 'D'
    { 0x7f, 0x63, 0x68, 0x78, 0x68, 0x63, 0x7f, 0x00 },  // 'E'
    { 0x7f, 0x63, 0x68, 0x78, 0x68, 0x60, 0x78, 0x00 },  // 'F'
    { 0x3c, 0x66, 0x60, 0x60, 0x67, 0x66, 0x3c, 0x00 },  // 'G'
    { 0x66, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x66, 0x00 },  // 'H'
    { 0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00 },  // 'I'
    { 0x07, 0x03, 0x03, 0x03, 0x63, 0x63, 0x3e, 0x00 },  // 'J'
    { 0x63, 0x66, 0x6c, 0x78, 0x6c, 0x66, 0x63, 0x00 },  // 'K'
    { 0x78, 0x60, 0x60, 0x60, 0x63, 0x63, 0x7f, 0x00 },  // 'L'
    { 0x63, 0x77, 0x7f, 0x6b, 0x63, 0x63, 0x63, 0x00 },  // 'M'
    { 0x63, 0x73, 0x7b, 0x6f, 0x67, 0x63, 0x63, 0x00 },  // 'N'
    { 0x3e, 0x63, 0x63, 0x63, 0x63, 0x63, 0x3e, 0x00 },  // 'O'
    { 0x7e, 0x63, 0x63, 0x7e, 0x60, 0x60, 0x78, 0x00 },  // 'P'
    { 0x3e, 0x63, 0x63, 0x63, 0x6b, 0x66, 0x3b, 0x00 },  // 'Q'
    { 0x7e, 0x63, 0x63, 0x7e, 0x6c, 0x66, 0x63, 0x00 },  // 'R'
    { 0x3e, 0x63, 0x70, 0x3e, 0x0e, 0x66, 0x3c, 0x00 },  // 'S'
    { 0x7f, 0x5a, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00 },  // 'T'
    { 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x3e, 0x00 },  // 'U'
    { 0x63, 0x63, 0x63, 0x63, 0x63, 0x2c, 0x18, 0x00 },  // 'V'
    { 0x63, 0x63, 0x63, 0x6b, 0x7f, 0x77, 0x63, 0x00 },  // 'W'
    { 0x63, 0x63, 0x2c, 0x18, 0x2c, 0x63, 0x63, 0x00 },  // 'X'
    { 0x63, 0x63, 0x63, 0x3e, 0x0c, 0x18, 0x3c, 0x00 },  // 'Y'
    { 0x7f, 0x63, 0x0c, 0x18, 0x30, 0x63, 0x7f, 0x00 },  // 'Z'
    { 0x3c, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3c, 0x00 },  // '['
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '\'
    { 0x3c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x3c, 0x00 },  // ']'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '^'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff },  // '_'
};

/* The data sets, as ECWolf's iwadinfo.txt names them */
static const struct
{
	const char *ext;
	const char *label;
	const char *needs;      /* another set it plays on top of */
} known_sets[] =
{
	{ "WL6", "Wolfenstein 3D", NULL },
	{ "WL1", "Wolfenstein 3D (shareware)", NULL },
	{ "SOD", "Spear of Destiny", NULL },
	{ "SD2", "Spear: Return to Danger", "SOD" },
	{ "SD3", "Spear: Ultimate Challenge", "SOD" },
	{ "SDM", "Spear of Destiny (demo)", NULL },
	{ "N3D", "Super 3-D Noah's Ark", NULL },
};
#define NUM_SETS ((int)(sizeof(known_sets) / sizeof(known_sets[0])))

typedef struct
{
	char label[MAX_NAME];
	char note[MAX_NAME];            /* small, at the right */
	char data[8];                   /* --data */
	char files[MAX_FILES][MAX_PATH_LEN];
	int num_files;
	int is_mod;
} entry_t;

static entry_t entries[MAX_ENTRIES];
static int num_entries;
static int set_found[NUM_SETS];

static char arg_buf[MAX_ARGS][MAX_PATH_LEN];
static char *arg_ptrs[MAX_ARGS + 1];
static int arg_count;

static uint32_t fb[LAUNCHER_W * LAUNCHER_H];

/*
 * Files
 */

static int
FileExists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

/* a VSWAP.<ext> in the folder, in either case */
static int
HasSet(const char *dir, const char *ext)
{
	char want[16];
	DIR *d = opendir(dir);
	struct dirent *de;
	int found = 0;

	if (!d)
	{
		return 0;
	}

	snprintf(want, sizeof(want), "VSWAP.%s", ext);

	while (!found && (de = readdir(d)) != NULL)
	{
		found = !strcasecmp(de->d_name, want);
	}

	closedir(d);
	return found;
}

/*
 * The command line
 */

static void
AddArg(const char *arg)
{
	if (arg_count >= MAX_ARGS)
	{
		return;
	}

	snprintf(arg_buf[arg_count], MAX_PATH_LEN, "%s", arg);
	arg_ptrs[arg_count] = arg_buf[arg_count];
	arg_count++;
	arg_ptrs[arg_count] = NULL;
}

static void
ArgsForEntry(const entry_t *e, const char *argv0)
{
	int i;

	arg_count = 0;
	AddArg(argv0 ? argv0 : "ecwolf");

	if (e->data[0])
	{
		AddArg("--data");
		AddArg(e->data);
	}

	for (i = 0; i < e->num_files; i++)
	{
		AddArg("--file");
		AddArg(e->files[i]);
	}
}

/*
 * Drawing
 */

static void
FillRect(int x, int y, int w, int h, uint32_t colour)
{
	int px, py;

	for (py = y; py < y + h; py++)
	{
		if (py < 0 || py >= LAUNCHER_H)
		{
			continue;
		}
		for (px = x; px < x + w; px++)
		{
			if (px >= 0 && px < LAUNCHER_W)
			{
				fb[py * LAUNCHER_W + px] = colour;
			}
		}
	}
}

static void
DrawChar(int x, int y, char c, uint32_t colour, int scale)
{
	const unsigned char *glyph;
	int row, col;

	if (c >= 'a' && c <= 'z')
	{
		c = c - 'a' + 'A';
	}
	if (c < FONT_FIRST || c > FONT_LAST)
	{
		c = '?';
	}

	glyph = launcher_font[c - FONT_FIRST];

	for (row = 0; row < GLYPH_H; row++)
	{
		for (col = 0; col < GLYPH_W; col++)
		{
			if (glyph[row] & (0x80 >> col))
			{
				FillRect(x + col * scale, y + row * scale, scale, scale, colour);
			}
		}
	}
}

static void
DrawText(int x, int y, const char *s, uint32_t colour, int scale)
{
	for (; *s; s++, x += GLYPH_W * scale)
	{
		DrawChar(x, y, *s, colour, scale);
	}
}

static int
TextWidth(const char *s, int scale)
{
	return (int)strlen(s) * GLYPH_W * scale;
}

/*
 * What is installed
 */

static entry_t *
NewEntry(const char *label, int is_mod)
{
	entry_t *e;

	if (num_entries >= MAX_ENTRIES)
	{
		return NULL;
	}

	e = &entries[num_entries++];
	memset(e, 0, sizeof(*e));
	snprintf(e->label, sizeof(e->label), "%.*s", MAX_NAME - 1, label);
	e->is_mod = is_mod;
	return e;
}

static void
ScanSets(void)
{
	int i;

	for (i = 0; i < NUM_SETS; i++)
	{
		set_found[i] = HasSet(PS3_USRDIR, known_sets[i].ext) ||
			HasSet(LEGACY_SOD_DIR, known_sets[i].ext);
	}

	for (i = 0; i < NUM_SETS; i++)
	{
		entry_t *e;
		int j, ok = set_found[i];

		/* the Spear missions need the Spear of Destiny's files */
		for (j = 0; ok && known_sets[i].needs && j < NUM_SETS; j++)
		{
			if (!strcmp(known_sets[j].ext, known_sets[i].needs))
			{
				ok = set_found[j];
			}
		}

		if (!ok || !(e = NewEntry(known_sets[i].label, 0)))
		{
			continue;
		}

		snprintf(e->data, sizeof(e->data), "%s", known_sets[i].ext);
	}
}

static char *
Trim(char *s)
{
	char *end;

	while (*s == ' ' || *s == '\t')
	{
		s++;
	}

	end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
	{
		*--end = '\0';
	}

	return s;
}

static int
CompareNames(const void *a, const void *b)
{
	return strcasecmp((const char *)a, (const char *)b);
}

static int
IsModFile(const char *name)
{
	const char *dot = strrchr(name, '.');

	return dot && (!strcasecmp(dot, ".pk3") || !strcasecmp(dot, ".zip") ||
		!strcasecmp(dot, ".wad") || !strcasecmp(dot, ".pk7"));
}

/* USRDIR/mods/<folder>/: its files, in alphabetical order, on --file */
static void
AddMod(const char *folder)
{
	char dir[MAX_PATH_LEN], path[MAX_PATH_LEN], line[128];
	char names[MAX_FILES][MAX_NAME];
	int n = 0, i;
	entry_t *e;
	DIR *d;
	struct dirent *de;
	FILE *f;

	snprintf(dir, sizeof(dir), "%s/%.*s", MODS_DIR, MAX_NAME, folder);

	if (!(d = opendir(dir)))
	{
		return;
	}

	while ((de = readdir(d)) != NULL && n < MAX_FILES)
	{
		if (IsModFile(de->d_name) && strlen(de->d_name) < MAX_NAME)
		{
			snprintf(names[n++], MAX_NAME, "%.*s", MAX_NAME - 1, de->d_name);
		}
	}

	closedir(d);

	if (n == 0 || !(e = NewEntry(folder, 1)))
	{
		return;
	}

	qsort(names, n, MAX_NAME, CompareNames);

	for (i = 0; i < n; i++)
	{
		snprintf(e->files[i], MAX_PATH_LEN, "%s/%s", dir, names[i]);
	}
	e->num_files = n;

	/* mod.txt: name, data */
	snprintf(path, sizeof(path), "%s/mod.txt", dir);
	if ((f = fopen(path, "r")) != NULL)
	{
		while (fgets(line, sizeof(line), f))
		{
			char *eq = strchr(line, '='), *key, *value;

			if (!eq)
			{
				continue;
			}

			*eq = '\0';
			key = Trim(line);
			value = Trim(eq + 1);

			if (!strcasecmp(key, "name") && *value)
			{
				snprintf(e->label, sizeof(e->label), "%.*s", MAX_NAME - 1, value);
			}
			else if (!strcasecmp(key, "data") && *value)
			{
				for (i = 0; value[i] && i < 7; i++)
				{
					e->data[i] = (char)(value[i] >= 'a' && value[i] <= 'z' ? value[i] - 32 : value[i]);
				}
				e->data[i] = '\0';
			}
		}

		fclose(f);
	}

	/* without one, the first game installed */
	if (!e->data[0])
	{
		for (i = 0; i < NUM_SETS; i++)
		{
			if (set_found[i])
			{
				snprintf(e->data, sizeof(e->data), "%s", known_sets[i].ext);
				break;
			}
		}
	}

	snprintf(e->note, sizeof(e->note), "%s", e->data);
}

static void
ScanMods(void)
{
	char names[MAX_ENTRIES][MAX_NAME];
	int n = 0, i;
	DIR *d = opendir(MODS_DIR);
	struct dirent *de;

	if (!d)
	{
		return;
	}

	while ((de = readdir(d)) != NULL && n < MAX_ENTRIES)
	{
		if (de->d_name[0] != '.' && strlen(de->d_name) < MAX_NAME)
		{
			snprintf(names[n++], MAX_NAME, "%.*s", MAX_NAME - 1, de->d_name);
		}
	}

	closedir(d);
	qsort(names, n, MAX_NAME, CompareNames);

	for (i = 0; i < n; i++)
	{
		AddMod(names[i]);
	}
}

/*
 * Input
 */

/* the pad's two button bytes, as ps3_pad.c keeps them */
#define PAD_LEFT   0x8000
#define PAD_DOWN   0x4000
#define PAD_RIGHT  0x2000
#define PAD_UP     0x1000
#define PAD_START  0x0800
#define PAD_CROSS  0x0040
#define PAD_CIRCLE 0x0020
#define PAD_R1     0x0008
#define PAD_L1     0x0004

/*
 * The menu
 */

#define TAB_GAMES 0
#define TAB_MODS  1

static int tab_items[2][MAX_ENTRIES];
static int tab_count[2];

static void
BuildTabs(void)
{
	int i;

	tab_count[TAB_GAMES] = tab_count[TAB_MODS] = 0;

	for (i = 0; i < num_entries; i++)
	{
		int t = entries[i].is_mod ? TAB_MODS : TAB_GAMES;
		tab_items[t][tab_count[t]++] = i;
	}
}

static void
DrawMenu(int tab, int selected, int top)
{
	const char *title = "ECELLWOLF";
	const int tabs = tab_count[TAB_MODS] > 0;
	const char *hint = tabs ? "D-PAD CHOOSE  L1/R1 GAMES/MODS  CROSS PLAY  CIRCLE EXIT"
	                        : "D-PAD TO CHOOSE    CROSS TO PLAY    CIRCLE TO EXIT";
	const int count = tab_count[tab];
	const int row_h = 26;
	char line[64];
	int i;

	FillRect(0, 0, LAUNCHER_W, LAUNCHER_H, COL_BG);
	DrawText((LAUNCHER_W - TextWidth(title, 3)) / 2, 34, title, COL_TITLE, 3);

	if (tabs)
	{
		static const char *const names[2] = { "GAMES", "MODS" };
		int x = LAUNCHER_W / 2 - TextWidth("GAMES", 2) - 24;
		int t;

		for (t = 0; t < 2; t++)
		{
			int w = TextWidth(names[t], 2);

			DrawText(x, 70, names[t], t == tab ? COL_TITLE : COL_DIM, 2);
			if (t == tab)
			{
				FillRect(x, 88, w, 2, COL_TITLE);
			}
			x += w + 48;
		}
	}

	for (i = 0; i < VISIBLE_ROWS && top + i < count; i++)
	{
		const entry_t *e = &entries[tab_items[tab][top + i]];
		int y = 100 + i * row_h;
		uint32_t colour = COL_TEXT;

		if (top + i == selected)
		{
			FillRect(26, y - 4, LAUNCHER_W - 52, row_h - 4, COL_SEL_BG);
			colour = COL_SEL_TX;
		}

		DrawText(44, y, e->label, colour, 2);

		if (e->note[0])
		{
			int lw = TextWidth(e->label, 2), bw = TextWidth(e->note, 1);

			if (44 + lw + 16 + bw <= LAUNCHER_W - 40)
			{
				DrawText(LAUNCHER_W - 40 - bw, y + 4, e->note,
				         top + i == selected ? COL_SEL_TX : COL_DIM, 1);
			}
		}
	}

	if (count > VISIBLE_ROWS)
	{
		snprintf(line, sizeof(line), "%d / %d", selected + 1, count);
		DrawText(LAUNCHER_W - TextWidth(line, 1) - 24, LAUNCHER_H - 26, line, COL_DIM, 1);
	}

	DrawText(24, LAUNCHER_H - 26, hint, COL_DIM, 1);

	GPU_SetScreen(LAUNCHER_W, LAUNCHER_H, 16, 10);
	GPU_BeginFrame();
	GPU_DrawScreen32(fb);
	GPU_EndFrame();

#ifndef __PPU__
	{
		/* the host harness: screenshots */
		extern void PS3_Host_Frame(void);

		PS3_Host_Frame();
	}
#endif
}

/* An index into entries[], or -1 to leave for the XMB */
static int
RunMenu(void)
{
	int tab = tab_count[TAB_GAMES] > 0 ? TAB_GAMES : TAB_MODS;
	int selected[2] = { 0, 0 }, top[2] = { 0, 0 };
	/* as if everything were held: the button that started the game from
	   the XMB must not pick an entry by itself */
	unsigned last = ~0u;

	for (;;)
	{
		PS3_PadState pad;
		unsigned now, pressed;
		int count;

		PS3_Pad_Read(&pad);
		now = pad.buttons;
		pressed = now & ~last;
		last = now;

		PS3_Pump();   /* PS button, "Quit Game" from the XMB */

		if (ps3_launcher_quit_requested || (pressed & PAD_CIRCLE))
		{
			return -1;
		}

		if ((pressed & (PAD_L1 | PAD_R1 | PAD_LEFT | PAD_RIGHT))
		    && tab_count[TAB_GAMES] > 0 && tab_count[TAB_MODS] > 0)
		{
			tab = (pressed & (PAD_L1 | PAD_LEFT)) ? TAB_GAMES : TAB_MODS;
		}

		count = tab_count[tab];

		if (pressed & PAD_UP)
		{
			selected[tab] = (selected[tab] + count - 1) % count;
		}
		if (pressed & PAD_DOWN)
		{
			selected[tab] = (selected[tab] + 1) % count;
		}
		if (pressed & (PAD_CROSS | PAD_START))
		{
			return tab_items[tab][selected[tab]];
		}

		if (selected[tab] < top[tab])
		{
			top[tab] = selected[tab];
		}
		else if (selected[tab] >= top[tab] + VISIBLE_ROWS)
		{
			top[tab] = selected[tab] - VISIBLE_ROWS + 1;
		}

		DrawMenu(tab, selected[tab], top[tab]);
	}
}

/*
 * Entry points
 */

void
PS3_Launcher_Run(int *argc, char ***argv)
{
	const char *argv0 = (*argc > 0 && (*argv)[0]) ? (*argv)[0] : "ecwolf";
	int returning, chosen = 0, i;

	/* a command line given (the host harness): as it is */
	if (*argc > 1)
	{
		return;
	}

	returning = FileExists(RETURN_MARKER);
	if (returning)
	{
		remove(RETURN_MARKER);
		PS3_Log("Launcher: back from Quit");
	}

	num_entries = 0;
	ScanSets();
	ScanMods();
	BuildTabs();

	if (num_entries == 0)
	{
		/* ECWolf's own error says what is missing */
		PS3_Log("Launcher: no game data found");
		return;
	}

	if (num_entries > 1 || returning)
	{
		PS3_Display_Init();

		if (!GPU_Ready())
		{
			PS3_Log("Launcher: no video, playing the first entry");
		}
		else
		{
			PS3_Pad_Init();

			ps3_launcher_active = 1;
			chosen = RunMenu();
			ps3_launcher_active = 0;

			if (chosen < 0)
			{
				/* only the RSX is up: let go of it, or the console goes
				   down with us */
				PS3_Log("Launcher: exit to the XMB");
				PS3_Display_Shutdown();
				exit(0);
			}
		}
	}

	ArgsForEntry(&entries[chosen], argv0);

	PS3_Log("Launcher: '%s'", entries[chosen].label);
	for (i = 1; i < arg_count; i++)
	{
		PS3_Log("Launcher:   %s", arg_ptrs[i]);
	}

	*argc = arg_count;
	*argv = arg_ptrs;
}

void
PS3_Launcher_Relaunch(void)
{
#ifdef __PPU__
	FILE *marker;

	if (!FileExists(RELOAD_SELF))
	{
		PS3_Log("Launcher: %s missing, exiting instead", RELOAD_SELF);
		return;
	}

	marker = fopen(RETURN_MARKER, "w");
	if (!marker)
	{
		PS3_Log("Launcher: cannot write %s, exiting instead", RETURN_MARKER);
		return;
	}
	fclose(marker);

	PS3_Log("Launcher: Quit, back to the launcher");
	sysProcessExitSpawn2(RELOAD_SELF, NULL, NULL, NULL, 0, 1001,
	                     SYS_PROCESS_SPAWN_STACK_SIZE_1M);

	PS3_Log("Launcher: sysProcessExitSpawn2 failed, exiting instead");
	remove(RETURN_MARKER);
#endif
}
