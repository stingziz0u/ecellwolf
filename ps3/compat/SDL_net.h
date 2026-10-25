/*
 * Copyright (C) 2026 the ECellWolf contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * The part of SDL_net ECWolf's multiplayer (wl_net.cpp) uses, without a
 * network: opening a socket always fails, so the game stays single
 * player (1.0 had no multiplayer on the PS3 either).
 */

#ifndef PS3_SDL_NET_SHIM_H
#define PS3_SDL_NET_SHIM_H

#include "SDL.h"

typedef struct
{
	Uint32 host;
	Uint16 port;
} IPaddress;

typedef struct
{
	int channel;
	Uint8 *data;
	int len;
	int maxlen;
	int status;
	IPaddress address;
} UDPpacket;

typedef struct _UDPsocket *UDPsocket;

static inline int SDLNet_Init(void) { return -1; }
static inline void SDLNet_Quit(void) {}
static inline const char *SDLNet_GetError(void) { return "no network on the PS3"; }
static inline int SDLNet_ResolveHost(IPaddress *address, const char *host, Uint16 port)
{
	(void)host;
	address->host = 0;
	address->port = port;
	return -1;
}
static inline UDPsocket SDLNet_UDP_Open(Uint16 port) { (void)port; return (UDPsocket)0; }
static inline void SDLNet_UDP_Close(UDPsocket sock) { (void)sock; }
static inline int SDLNet_UDP_Send(UDPsocket sock, int channel, UDPpacket *packet)
{
	(void)sock; (void)channel; (void)packet;
	return 0;
}
static inline int SDLNet_UDP_Recv(UDPsocket sock, UDPpacket *packet)
{
	(void)sock; (void)packet;
	return 0;
}
static inline UDPpacket *SDLNet_AllocPacket(int size)
{
	UDPpacket *p = (UDPpacket *)calloc(1, sizeof(UDPpacket) + size);

	if (p) {
		p->data = (Uint8 *)(p + 1);
		p->maxlen = size;
	}
	return p;
}
static inline void SDLNet_FreePacket(UDPpacket *packet) { free(packet); }

#endif
