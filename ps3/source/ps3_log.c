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
 * The log (1.1). On the PS3 there is no console: a failure looks like a
 * black screen and a trip back to the XMB, and the log is the only
 * witness. stdout and stderr go to USRDIR/ecwolf_log.txt, so everything
 * ECWolf prints is in it, line by line (line buffered: after a crash, the
 * last line is the one that says where it died). The previous run is kept
 * as ecwolf_log.old.txt.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "ps3_platform.h"

#define LOG_PATH     PS3_USRDIR "/ecwolf_log.txt"
#define LOG_OLD_PATH PS3_USRDIR "/ecwolf_log.old.txt"

static int log_open;

void
PS3_Log_Open(void)
{
#ifdef __PPU__
	if (log_open)
	{
		return;
	}

	remove(LOG_OLD_PATH);
	rename(LOG_PATH, LOG_OLD_PATH);

	if (freopen(LOG_PATH, "w", stdout))
	{
		setvbuf(stdout, NULL, _IOLBF, 0);
		freopen(LOG_PATH, "a", stderr);
		setvbuf(stderr, NULL, _IOLBF, 0);
		log_open = 1;
	}
#else
	log_open = 1;   /* the host harness: the terminal */
#endif
}

static void
LogV(const char *fmt, va_list ap)
{
	vfprintf(stdout, fmt, ap);
	fputc('\n', stdout);
	fflush(stdout);
}

void
PS3_Log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	LogV(fmt, ap);
	va_end(ap);
}

void
PS3_GPU_Log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	LogV(fmt, ap);
	va_end(ap);
}
