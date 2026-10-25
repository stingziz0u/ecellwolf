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
 * The part of SDL_mixer 2 ECWolf uses, on the PS3's audio port
 * (ps3/source/sdl_mixer_ps3.c, from ROTT-PS3's). Always 48000 Hz, 16 bit
 * native endian, stereo. Music files (Mix_LoadMUS_RW) are not played:
 * ECWolf's music is the IMF songs on the emulated OPL, mixed in by its
 * Mix_SetPostMix callback.
 *
 * =======================================================================
 */

#ifndef PS3_SDL_MIXER_SHIM_H
#define PS3_SDL_MIXER_SHIM_H

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_MIXER_MAJOR_VERSION 2
#define SDL_MIXER_MINOR_VERSION 8
#define SDL_MIXER_PATCHLEVEL    0
#define SDL_MIXER_VERSION_ATLEAST(X, Y, Z) \
	(SDL_VERSIONNUM(SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_PATCHLEVEL) >= SDL_VERSIONNUM(X, Y, Z))

#define MIX_CHANNELS          8
#define MIX_DEFAULT_FREQUENCY 48000
#define MIX_DEFAULT_FORMAT    AUDIO_S16SYS
#define MIX_MAX_VOLUME        SDL_MIX_MAXVOLUME

typedef struct Mix_Chunk
{
	int allocated;
	Uint8 *abuf;            /* 16 bit mono samples at 'rate' */
	Uint32 alen;            /* bytes */
	Uint8 volume;
	int rate;               /* PS3: the samples' own rate */
} Mix_Chunk;

typedef struct _Mix_Music Mix_Music;

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize);
int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels);
void Mix_CloseAudio(void);
const char *Mix_GetError(void);

int Mix_AllocateChannels(int numchans);
int Mix_ReserveChannels(int num);
int Mix_GroupChannels(int from, int to, int tag);
int Mix_GroupAvailable(int tag);
int Mix_GroupOldest(int tag);

Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc);
void Mix_FreeChunk(Mix_Chunk *chunk);

int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks);
#define Mix_PlayChannel(channel, chunk, loops) Mix_PlayChannelTimed(channel, chunk, loops, -1)
int Mix_HaltChannel(int channel);
int Mix_Playing(int channel);
int Mix_SetPanning(int channel, Uint8 left, Uint8 right);
int Mix_Volume(int channel, int volume);
void Mix_ChannelFinished(void (*channel_finished)(int channel));

/* called by the audio thread with each block, after the sound effects
   (16 bit stereo); ECWolf's OPL music and PC speaker */
void Mix_SetPostMix(void (*mix_func)(void *udata, Uint8 *stream, int len), void *arg);

Mix_Music *Mix_LoadMUS_RW(SDL_RWops *src, int freesrc);
void Mix_FreeMusic(Mix_Music *music);
int Mix_PlayMusic(Mix_Music *music, int loops);
int Mix_HaltMusic(void);
void Mix_PauseMusic(void);
void Mix_ResumeMusic(void);
int Mix_PausedMusic(void);
int Mix_PlayingMusic(void);
int Mix_VolumeMusic(int volume);
double Mix_GetMusicPosition(Mix_Music *music);
int Mix_SetMusicPosition(double position);
void Mix_HookMusicFinished(void (*music_finished)(void));
int Mix_SetSoundFonts(const char *paths);

#ifdef __cplusplus
}
#endif

#endif
