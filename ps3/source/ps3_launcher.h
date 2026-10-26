/*
 * Copyright (C) 2026 the ECellWolf contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * The game picker shown before the engine starts (ps3_launcher.c).
 */

#ifndef PS3_LAUNCHER_H
#define PS3_LAUNCHER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Before WL_Main: picks the game (the menu, or straight through when
   there is one thing to play) and gives ECWolf's command line for it. */
void PS3_Launcher_Run(int *argc, char ***argv);

/* While the menu is up the XMB's "Quit Game" only asks it to leave. */
extern volatile int ps3_launcher_active;
extern volatile int ps3_launcher_quit_requested;

/* Quit from the game's menu (error NULL) or an engine error, after the
   engine shut down: back to the launcher (a fresh copy of the program),
   which shows the error. Returns only when the game must really exit. */
void PS3_Launcher_Relaunch(const char *error);

#ifdef __cplusplus
}
#endif

#endif
