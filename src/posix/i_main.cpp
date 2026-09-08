/*
** i_main.cpp
**
**---------------------------------------------------------------------------
** Copyright 2021 Braden Obrzut
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
**
*/

#include <cstdlib>

#ifndef NO_GTK
#include <gtk/gtk.h>
bool GtkAvailable;
#endif

#include <stdio.h>

#include <sysutil/sysutil.h>
// PS3: la pila por defecto del proceso principal es chica, y ECWolf (C++,
// con llamadas anidadas profundas en el raycasting) la puede desbordar --
// una de las causas de cuelgue que documenta la guia de PSL1GHT. Pedimos
// 1MB, mismo enfoque que en TyrQuake (donde el hilo del juego tenia 2MB).
#include <sys/process.h>
SYS_PROCESS_PARAM(1001, 1024*1024) // 8MB: con 1MB cualquier agregado de codigo rompia el arranque (pila al limite)

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdlib.h>

// Mismo arreglo que TyrQuake: sin registrar este callback, salir con el
// boton PS mata el proceso de golpe sin liberar la RSX, y la consola se
// reinicia sola. Con esto, el sistema nos avisa y salimos ordenadamente.
volatile int PS3_ExitRequested = 0;
static void PS3_SysutilCallback(u64 status, u64 param, void *userdata)
{
	(void)param; (void)userdata;
	if (status == SYSUTIL_EXIT_GAME)
	{
		// El callback corre en medio del frame: apagar SDL aca es
		// peligroso. Solo marcamos, y el cierre real pasa en un punto
		// seguro (ver PS3_ExitRequested en sdlvideo.cpp).
		PS3_ExitRequested = 1;
	}
}

int main(int argc, char *argv[])
{
	sysUtilRegisterCallback(3, PS3_SysutilCallback, NULL); // ranura 3: SDL2 usa la 0 para su propio callback y pisaba el nuestro
	// Set LC_NUMERIC environment variable in case some library decides to
	// clear the setlocale call at least this will be correct.
	// Note that the LANG environment variable is overridden by LC_*
	remove("/dev_hdd0/game/ECWOLF001/USRDIR/ecwolf_log.txt");
	setenv("LC_NUMERIC", "C", 1);

#ifndef NO_GTK
#endif

	extern int WL_Main(int argc, char *argv[]);
	return WL_Main(argc, argv);
}
