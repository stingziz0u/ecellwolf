/*
 * Copyright (C) 2026 the ECellWolf contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * The DualShock 3 (1.1): the first pad connected, read once per
 * SDL_JoystickUpdate. Its two button bytes are kept as one 16 bit value,
 * high bit first, which is the SDL button numbering of 1.0 (SDL.h).
 * When the pad sleeps or is switched off, everything reads released and
 * centered until it comes back.
 *
 * The host harness has no pad: ps3_host.c defines these from a script.
 */

#ifdef __PPU__

#include <string.h>
#include <io/pad.h>

#include "ps3_platform.h"

static int pad_init;
static PS3_PadState last;   /* ioPadGetData gives nothing when nothing changed */

void
PS3_Pad_Init(void)
{
	if (!pad_init)
	{
		ioPadInit(MAX_PADS);
		pad_init = 1;
	}
}

void
PS3_Pad_Read(PS3_PadState *state)
{
	padInfo info;
	padData data;
	int i;

	memset(state, 0, sizeof(*state));
	memset(state->axis, 128, sizeof(state->axis));

	if (!pad_init || ioPadGetInfo(&info) != 0)
	{
		return;
	}

	for (i = 0; i < MAX_PADS; i++)
	{
		if (!info.status[i])
		{
			continue;
		}

		memset(&data, 0, sizeof(data));
		if (ioPadGetData(i, &data) == 0 && data.len >= 8)
		{
			last.buttons = (uint16_t)(((data.button[2] & 0xff) << 8) | (data.button[3] & 0xff));
			last.axis[0] = (uint8_t)data.button[6];   /* left X */
			last.axis[1] = (uint8_t)data.button[7];   /* left Y */
			last.axis[2] = (uint8_t)data.button[4];   /* right X */
			last.axis[3] = (uint8_t)data.button[5];   /* right Y */
		}

		*state = last;
		return;
	}

	/* no pad: everything released */
	memset(&last, 0, sizeof(last));
	memset(last.axis, 128, sizeof(last.axis));
}

#endif
