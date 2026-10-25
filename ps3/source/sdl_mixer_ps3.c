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
 * The SDL_mixer of ps3/compat/SDL_mixer.h (1.1, instead of SDL2_mixer),
 * from ROTT-PS3's: sound effects and ECWolf's music mixed by one thread
 * into the PS3's audio port.
 *
 *  - ONE port, 48000 Hz stereo float, 16 blocks of 256 frames; a thread
 *    woken by the port's notify queue keeps the blocks up to AUDIO_AHEAD
 *    in front of the one being played filled (handoff notes 3.5-3.8,
 *    3.40, 3.41).
 *  - Sound effects: WAV, 16 bit (ECWolf resamples the VSWAP sounds to
 *    48 kHz when it loads them) or 8 bit (Mac sounds), resampled while
 *    mixing with a 64 bit position.
 *  - Channel groups and reserved channels as SDL_mixer has them: ECWolf
 *    plays the player's and the boss' weapons on channels 0 and 1, and
 *    the rest in group 1.
 *  - Music: ECWolf's IMF songs and the PC speaker are mixed in by its
 *    Mix_SetPostMix callback, on the emulated OPL. Music files
 *    (Mix_LoadMUS_RW: Ogg, MIDI...) are not played: as 1.0, the
 *    music is the AdLib one.
 *  - Lock order: ECWolf calls in here with its own audioMutex held, and
 *    its post mix callback takes that mutex. So the thread calls the
 *    callback (and the channel finished one) with the mixer's lock
 *    released, or the two threads would wait for each other forever.
 *
 * The host harness builds this file without __PPU__: the "port" is then
 * a thread that renders at real-time pace, optionally into a WAV file
 * (ECW_HOST_WAV).
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL_mixer.h"
#include "ps3_platform.h"

#define AUDIO_RATE          48000
#define AUDIO_CHANNELS      2
#define AUDIO_BLOCK_FRAMES  256
#define AUDIO_AHEAD         4       /* blocks, ~21 ms */
#define MAX_MIX_CHANNELS    32

#ifdef __PPU__
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/event_queue.h>
#include <audio/audio.h>

static sys_mutex_t mix_mutex;
#define MIX_LOCK()   sysMutexLock(mix_mutex, 0)
#define MIX_UNLOCK() sysMutexUnlock(mix_mutex)
#else
#include <pthread.h>
#include <time.h>

static pthread_mutex_t mix_mutex = PTHREAD_MUTEX_INITIALIZER;
#define MIX_LOCK()   pthread_mutex_lock(&mix_mutex)
#define MIX_UNLOCK() pthread_mutex_unlock(&mix_mutex)
#endif

/* ================================================================ */
/* State                                                              */
/* ================================================================ */

typedef struct
{
	Mix_Chunk *chunk;
	int playing;
	int finished;          /* stopped by the mixer: tell ECWolf */
	uint64_t pos;          /* 32.16 fixed point, in the chunk's samples */
	uint64_t step;
	int left, right;       /* panning 0..255 */
	int volume;            /* 0..128 */
	int tag;               /* group, -1 none */
	unsigned start;        /* when it started, for Mix_GroupOldest */
} mixchannel_t;

struct _Mix_Music
{
	int unused;
};

static int audio_open;
static volatile int audio_quit;
static int num_channels = MIX_CHANNELS;
static int reserved;
static mixchannel_t channels[MAX_MIX_CHANNELS];
static unsigned play_count;

static void (*postmix)(void *udata, Uint8 *stream, int len);
static void *postmix_arg;
static void (*channel_finished)(int channel);

static unsigned audio_late;         /* times the thread fell behind */

/* ================================================================ */
/* Mixing                                                             */
/* ================================================================ */

static int32_t mixbuf[AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS];
static int16_t outbuf[AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS];

/* under the lock */
static void
MixChannels(void)
{
	int c, i;

	for (c = 0; c < num_channels; c++)
	{
		mixchannel_t *ch = &channels[c];
		const int16_t *samples;
		uint64_t len, pos, step;
		int32_t vl, vr;
		int32_t *out = mixbuf;

		if (!ch->playing || !ch->chunk)
		{
			continue;
		}

		samples = (const int16_t *)ch->chunk->abuf;
		len = (uint64_t)(ch->chunk->alen / 2) << 16;
		pos = ch->pos;
		step = ch->step;

		/* 0..255 pan x 0..128 volume -> 0..(1<<15) */
		vl = (int32_t)((ch->left * ch->volume) );
		vr = (int32_t)((ch->right * ch->volume) );

		for (i = 0; i < AUDIO_BLOCK_FRAMES; i++)
		{
			int32_t s;

			if (pos >= len)
			{
				ch->playing = 0;
				ch->finished = 1;
				break;
			}

			s = samples[pos >> 16];
			out[0] += (s * vl) >> 15;
			out[1] += (s * vr) >> 15;
			out += 2;
			pos += step;
		}

		ch->pos = pos;
	}
}

/* One block of 256 stereo frames, as floats (the port's format). */
static void
MixBlock(float *dst)
{
	int finished[MAX_MIX_CHANNELS];
	int nfinished = 0;
	int i, c;

	memset(mixbuf, 0, sizeof(mixbuf));

	MIX_LOCK();
	MixChannels();

	for (c = 0; c < num_channels; c++)
	{
		if (channels[c].finished)
		{
			channels[c].finished = 0;
			finished[nfinished++] = c;
		}
	}
	MIX_UNLOCK();

	for (i = 0; i < AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS; i++)
	{
		int32_t s = mixbuf[i];

		outbuf[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
	}

	/* without the mixer's lock (see the top of the file) */
	for (c = 0; c < nfinished; c++)
	{
		if (channel_finished)
		{
			channel_finished(finished[c]);
		}
	}

	if (postmix)
	{
		postmix(postmix_arg, (Uint8 *)outbuf, (int)sizeof(outbuf));
	}

	for (i = 0; i < AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS; i++)
	{
		dst[i] = (float)outbuf[i] * (1.0f / 32768.0f);
	}
}

/* ================================================================ */
/* The port                                                           */
/* ================================================================ */

#ifdef __PPU__

static u32 audio_port;
static audioPortConfig audio_cfg;
static sys_event_queue_t audio_queue;
static sys_ipc_key_t audio_key;
static sys_ppu_thread_t audio_tid;
static int mutex_ok;

static float *
AudioBlock(u32 index)
{
	return (float *)(u64)audio_cfg.audioDataStart +
		(u64)index * AUDIO_BLOCK_FRAMES * audio_cfg.channelCount;
}

static void
AudioThread(void *arg)
{
	u32 blocks = (u32)audio_cfg.numBlocks;
	u32 next = 1;   /* next block to fill */
	sys_event_t ev;

	(void)arg;

	while (!audio_quit)
	{
		u32 playing, dist;

		/* one event per block played; also fill on timeout (notes 3.41) */
		sysEventQueueReceive(audio_queue, &ev, 20 * 1000);

		if (audio_quit)
		{
			break;
		}

		__asm__ volatile("lwsync" ::: "memory");

		playing = (u32)(*(volatile u64 *)(u64)audio_cfg.readIndex % blocks);
		dist = (next + blocks - playing) % blocks;

		if (dist == 0 || dist > AUDIO_AHEAD + 1)
		{
			/* the port caught up with us: start again right after it */
			if (dist == 0)
			{
				audio_late++;
			}

			next = (playing + 1) % blocks;
			dist = 1;
		}

		while (dist <= AUDIO_AHEAD)
		{
			MixBlock(AudioBlock(next));
			next = (next + 1) % blocks;
			dist++;
		}
	}

	sysThreadExit(0);
}

static int
PortOpen(void)
{
	audioPortParam param;
	sys_mutex_attr_t attr;
	s32 r;

	if (!mutex_ok)
	{
		memset(&attr, 0, sizeof(attr));
		attr.attr_protocol = SYS_MUTEX_PROTOCOL_PRIO;
		attr.attr_recursive = SYS_MUTEX_ATTR_NOT_RECURSIVE;
		attr.attr_pshared = SYS_MUTEX_ATTR_NOT_PSHARED;
		attr.attr_adaptive = SYS_MUTEX_ATTR_NOT_ADAPTIVE;
		strcpy(attr.name, "ecwmix");

		if (sysMutexCreate(&mix_mutex, &attr) != 0)
		{
			PS3_Log("[audio] sysMutexCreate failed");
			return -1;
		}

		/* never destroyed: the thread may hold it at shutdown (notes 3.8) */
		mutex_ok = 1;
	}

	r = audioInit();

	if (r != 0)
	{
		PS3_Log("[audio] audioInit failed (%d)", (int)r);
		return -1;
	}

	memset(&param, 0, sizeof(param));
	param.numChannels = AUDIO_PORT_2CH;
	param.numBlocks = AUDIO_BLOCK_16;
	param.attrib = 0;
	param.level = 1.0f;

	r = audioPortOpen(&param, &audio_port);

	if (r != 0)
	{
		PS3_Log("[audio] audioPortOpen failed (%d)", (int)r);
		audioQuit();
		return -1;
	}

	audioGetPortConfig(audio_port, &audio_cfg);
	audioCreateNotifyEventQueue(&audio_queue, &audio_key);
	audioSetNotifyEventQueue(audio_key);
	sysEventQueueDrain(audio_queue);
	memset((void *)(u64)audio_cfg.audioDataStart, 0, audio_cfg.portSize);

	audio_quit = 0;
	audio_late = 0;

	audioPortStart(audio_port);

	/* priority 500: runs before the game thread (1000) */
	if (sysThreadCreate(&audio_tid, AudioThread, NULL, 500, 64 * 1024,
				THREAD_JOINABLE, "ecwaudio") != 0)
	{
		PS3_Log("[audio] sysThreadCreate failed");
		audioPortStop(audio_port);
		audioRemoveNotifyEventQueue(audio_key);
		audioPortClose(audio_port);
		sysEventQueueDestroy(audio_queue, 0);
		audioQuit();
		return -1;
	}

	PS3_Log("[audio] port %u open: %u blocks, %u channels, %d Hz",
			(unsigned)audio_port, (unsigned)audio_cfg.numBlocks,
			(unsigned)audio_cfg.channelCount, AUDIO_RATE);

	return 0;
}

static void
PortClose(void)
{
	u64 rv;

	audio_quit = 1;
	sysThreadJoin(audio_tid, &rv);

	audioPortStop(audio_port);
	audioRemoveNotifyEventQueue(audio_key);
	audioPortClose(audio_port);
	sysEventQueueDestroy(audio_queue, 0);
	audioQuit();
}

#else /* host harness */

static pthread_t audio_thread;
static FILE *wav_out;
static unsigned long wav_frames;

static void
WavHeader(FILE *f, unsigned long frames)
{
	unsigned long data = frames * 4;
	unsigned char h[44] = {
		'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E',
		'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
		0x80, 0xbb, 0, 0, 0, 0xee, 2, 0, 4, 0, 16, 0,
		'd', 'a', 't', 'a', 0, 0, 0, 0
	};

	h[4] = (unsigned char)((data + 36) & 0xff);
	h[5] = (unsigned char)(((data + 36) >> 8) & 0xff);
	h[6] = (unsigned char)(((data + 36) >> 16) & 0xff);
	h[7] = (unsigned char)(((data + 36) >> 24) & 0xff);
	h[40] = (unsigned char)(data & 0xff);
	h[41] = (unsigned char)((data >> 8) & 0xff);
	h[42] = (unsigned char)((data >> 16) & 0xff);
	h[43] = (unsigned char)((data >> 24) & 0xff);
	fseek(f, 0, SEEK_SET);
	fwrite(h, 1, 44, f);
}

static void *
HostAudioThread(void *arg)
{
	float block[AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS];
	struct timespec ts = {0, 5333333};   /* 256 frames at 48 kHz */

	(void)arg;

	while (!audio_quit)
	{
		MixBlock(block);

		if (wav_out)
		{
			int i;

			for (i = 0; i < AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS; i++)
			{
				int v = (int)(block[i] * 32767.0f);

				fputc(v & 0xff, wav_out);
				fputc((v >> 8) & 0xff, wav_out);
			}

			wav_frames += AUDIO_BLOCK_FRAMES;
		}

		nanosleep(&ts, NULL);
	}

	return NULL;
}

static int
PortOpen(void)
{
	const char *wav = getenv("ECW_HOST_WAV");

	if (wav)
	{
		wav_out = fopen(wav, "wb");

		if (wav_out)
		{
			WavHeader(wav_out, 0);
		}
	}

	audio_quit = 0;

	if (pthread_create(&audio_thread, NULL, HostAudioThread, NULL) != 0)
	{
		return -1;
	}

	PS3_Log("[audio] host audio thread started%s", wav_out ? " (recording)" : "");

	return 0;
}

static void
PortClose(void)
{
	audio_quit = 1;
	pthread_join(audio_thread, NULL);

	if (wav_out)
	{
		WavHeader(wav_out, wav_frames);
		fclose(wav_out);
		wav_out = NULL;
	}
}

#endif

/* ================================================================ */
/* Device                                                             */
/* ================================================================ */

int
Mix_OpenAudio(int frequency, Uint16 format, int nchannels, int chunksize)
{
	(void)frequency;
	(void)format;
	(void)nchannels;
	(void)chunksize;

	if (audio_open)
	{
		return 0;
	}

	if (PortOpen() != 0)
	{
		return -1;
	}

	audio_open = 1;
	return 0;
}

int
Mix_QuerySpec(int *frequency, Uint16 *format, int *nchannels)
{
	if (!audio_open)
	{
		return 0;
	}

	if (frequency) *frequency = AUDIO_RATE;
	if (format) *format = AUDIO_S16;   /* what ECWolf asks for: no conversion */
	if (nchannels) *nchannels = AUDIO_CHANNELS;

	return 1;
}

void
Mix_CloseAudio(void)
{
	if (!audio_open)
	{
		return;
	}

	PortClose();
	audio_open = 0;

	MIX_LOCK();
	for (int c = 0; c < MAX_MIX_CHANNELS; c++)
	{
		channels[c].playing = 0;
		channels[c].chunk = NULL;
	}
	MIX_UNLOCK();

	PS3_Log("[audio] closed (the mixer fell behind %u times)", audio_late);
}

const char *
Mix_GetError(void)
{
	return "PS3 mixer";
}

int
Mix_AllocateChannels(int numchans)
{
	if (numchans < 0)
	{
		return num_channels;
	}

	if (numchans > MAX_MIX_CHANNELS)
	{
		numchans = MAX_MIX_CHANNELS;
	}

	MIX_LOCK();
	num_channels = numchans;
	MIX_UNLOCK();

	return num_channels;
}

int
Mix_ReserveChannels(int num)
{
	reserved = num < 0 ? 0 : num > num_channels ? num_channels : num;
	return reserved;
}

int
Mix_GroupChannels(int from, int to, int tag)
{
	int c, n = 0;

	MIX_LOCK();
	for (c = from; c <= to && c < MAX_MIX_CHANNELS; c++)
	{
		if (c >= 0)
		{
			channels[c].tag = tag;
			n++;
		}
	}
	MIX_UNLOCK();

	return n;
}

int
Mix_GroupAvailable(int tag)
{
	int c, found = -1;

	MIX_LOCK();
	for (c = 0; c < num_channels; c++)
	{
		if ((tag == -1 || channels[c].tag == tag) && !channels[c].playing)
		{
			found = c;
			break;
		}
	}
	MIX_UNLOCK();

	return found;
}

int
Mix_GroupOldest(int tag)
{
	int c, found = -1;
	unsigned oldest = 0;

	MIX_LOCK();
	for (c = 0; c < num_channels; c++)
	{
		if ((tag == -1 || channels[c].tag == tag) && channels[c].playing &&
				(found < 0 || play_count - channels[c].start > oldest))
		{
			found = c;
			oldest = play_count - channels[c].start;
		}
	}
	MIX_UNLOCK();

	return found;
}

/* ================================================================ */
/* Sound files                                                        */
/* ================================================================ */

static unsigned
LE16(const Uint8 *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned
LE24(const Uint8 *p)
{
	return LE16(p) | ((unsigned)p[2] << 16);
}

static unsigned
LE32(const Uint8 *p)
{
	return LE24(p) | ((unsigned)p[3] << 24);
}

/* Appends 'count' samples (8 bit unsigned or 16 bit little-endian signed,
   'chans' interleaved channels mixed down to one) to a growing buffer. */
typedef struct
{
	int16_t *s;
	size_t n, cap;
	int rate;
} pcmbuf_t;

static int
PcmAppend(pcmbuf_t *b, const Uint8 *data, size_t bytes, int bits, int chans)
{
	size_t frame = (size_t)(bits / 8) * (size_t)chans;
	size_t count, i;

	if (frame == 0)
	{
		return -1;
	}

	count = bytes / frame;

	if (b->n + count > b->cap)
	{
		size_t cap = (b->n + count) * 2 + 1024;
		int16_t *s = realloc(b->s, cap * sizeof(int16_t));

		if (!s)
		{
			return -1;
		}

		b->s = s;
		b->cap = cap;
	}

	for (i = 0; i < count; i++)
	{
		int c, sum = 0;

		for (c = 0; c < chans; c++)
		{
			const Uint8 *p = data + i * frame + (size_t)c * (size_t)(bits / 8);

			sum += (bits == 8) ? ((int)p[0] - 128) << 8 : (int16_t)LE16(p);
		}

		b->s[b->n++] = (int16_t)(sum / chans);
	}

	return 0;
}

static int
DecodeWav(const Uint8 *d, size_t size, pcmbuf_t *out)
{
	size_t pos = 12;
	int chans = 0, bits = 0, fmt_ok = 0;

	while (pos + 8 <= size)
	{
		unsigned id_len = LE32(d + pos + 4);
		const Uint8 *body = d + pos + 8;
		size_t len = id_len;

		if (len > size - pos - 8)
		{
			len = size - pos - 8;
		}

		if (!memcmp(d + pos, "fmt ", 4) && len >= 16)
		{
			if (LE16(body) != 1)
			{
				PS3_Log("[audio] WAV: compressed format %u not supported", LE16(body));
				return -1;
			}

			chans = (int)LE16(body + 2);
			out->rate = (int)LE32(body + 4);
			bits = (int)LE16(body + 14);
			fmt_ok = (chans >= 1 && chans <= 2 && (bits == 8 || bits == 16) &&
					out->rate > 0);
		}
		else if (!memcmp(d + pos, "data", 4))
		{
			if (!fmt_ok)
			{
				return -1;
			}

			return PcmAppend(out, body, len, bits, chans);
		}

		pos += 8 + id_len + (id_len & 1);
	}

	return -1;
}

Mix_Chunk *
Mix_LoadWAV_RW(SDL_RWops *src, int freesrc)
{
	pcmbuf_t pcm;
	Mix_Chunk *chunk = NULL;
	Uint8 *data = NULL;
	Sint64 size;
	int r = -1;

	if (!src)
	{
		return NULL;
	}

	memset(&pcm, 0, sizeof(pcm));

	/* the whole file in memory (ECWolf's Mac sounds come through read
	   callbacks, everything else from memory) */
	size = SDL_RWsize(src);
	if (size > 0 && size < 64 * 1024 * 1024)
	{
		data = (Uint8 *)malloc((size_t)size);
		SDL_RWseek(src, 0, RW_SEEK_SET);
		if (data && SDL_RWread(src, data, 1, (size_t)size) != (size_t)size)
		{
			free(data);
			data = NULL;
		}
	}

	if (data && size >= 44 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WAVE", 4))
	{
		r = DecodeWav(data, (size_t)size, &pcm);
	}

	if (r == 0 && pcm.n > 0 && pcm.rate > 0)
	{
		chunk = (Mix_Chunk *)calloc(1, sizeof(*chunk));

		if (chunk)
		{
			chunk->allocated = 1;
			chunk->abuf = (Uint8 *)pcm.s;
			chunk->alen = (Uint32)(pcm.n * 2);
			chunk->volume = MIX_MAX_VOLUME;
			chunk->rate = pcm.rate;
			pcm.s = NULL;
		}
	}

	free(pcm.s);
	free(data);

	if (freesrc)
	{
		SDL_RWclose(src);
	}

	return chunk;
}

void
Mix_FreeChunk(Mix_Chunk *chunk)
{
	int c;

	if (!chunk)
	{
		return;
	}

	MIX_LOCK();
	for (c = 0; c < MAX_MIX_CHANNELS; c++)
	{
		if (channels[c].chunk == chunk)
		{
			channels[c].playing = 0;
			channels[c].chunk = NULL;
		}
	}
	MIX_UNLOCK();

	free(chunk->abuf);
	free(chunk);
}

/* ================================================================ */
/* Channels                                                           */
/* ================================================================ */

static void
ChannelDefaults(mixchannel_t *ch)
{
	ch->volume = MIX_MAX_VOLUME;
	ch->left = ch->right = 255;
	ch->tag = -1;
}

int
Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks)
{
	(void)loops;   /* ECWolf never loops a sound through the mixer */
	(void)ticks;

	if (!chunk || !audio_open)
	{
		return -1;
	}

	MIX_LOCK();

	if (channel < 0)
	{
		for (channel = reserved; channel < num_channels; channel++)
		{
			if (!channels[channel].playing)
			{
				break;
			}
		}
	}

	if (channel < 0 || channel >= num_channels)
	{
		MIX_UNLOCK();
		return -1;
	}

	channels[channel].chunk = chunk;
	channels[channel].pos = 0;
	channels[channel].step = ((uint64_t)chunk->rate << 16) / AUDIO_RATE;
	channels[channel].playing = 1;
	channels[channel].finished = 0;
	channels[channel].start = play_count++;

	MIX_UNLOCK();

	return channel;
}

int
Mix_HaltChannel(int channel)
{
	int stopped[MAX_MIX_CHANNELS];
	int n = 0, c;

	MIX_LOCK();
	for (c = 0; c < num_channels; c++)
	{
		if ((channel < 0 || c == channel) && channels[c].playing)
		{
			channels[c].playing = 0;
			channels[c].finished = 0;
			stopped[n++] = c;
		}
	}
	MIX_UNLOCK();

	/* as SDL_mixer: the finished callback for every channel halted */
	for (c = 0; c < n; c++)
	{
		if (channel_finished)
		{
			channel_finished(stopped[c]);
		}
	}

	return 0;
}

int
Mix_Playing(int channel)
{
	int c, n = 0;

	MIX_LOCK();
	for (c = 0; c < num_channels; c++)
	{
		if ((channel < 0 || c == channel) && channels[c].playing)
		{
			n++;
		}
	}
	MIX_UNLOCK();

	return n;
}

int
Mix_SetPanning(int channel, Uint8 left, Uint8 right)
{
	if (channel < 0 || channel >= MAX_MIX_CHANNELS)
	{
		return 0;
	}

	MIX_LOCK();
	channels[channel].left = left;
	channels[channel].right = right;
	MIX_UNLOCK();

	return 1;
}

int
Mix_Volume(int channel, int volume)
{
	int c, prev;

	if (channel >= MAX_MIX_CHANNELS)
	{
		return 0;
	}

	if (channel < 0)
	{
		int sum = 0;

		for (c = 0; c < num_channels; c++)
		{
			sum += Mix_Volume(c, volume);
		}
		return num_channels ? sum / num_channels : 0;
	}

	prev = channels[channel].volume;

	if (volume >= 0)
	{
		MIX_LOCK();
		channels[channel].volume = volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME : volume;
		MIX_UNLOCK();
	}

	return prev;
}

void
Mix_ChannelFinished(void (*cb)(int channel))
{
	MIX_LOCK();
	channel_finished = cb;
	MIX_UNLOCK();
}

void
Mix_SetPostMix(void (*mix_func)(void *udata, Uint8 *stream, int len), void *arg)
{
	MIX_LOCK();
	postmix = mix_func;
	postmix_arg = arg;
	MIX_UNLOCK();
}

/* ================================================================ */
/* Music files: not played (the IMF music is the post mix)            */
/* ================================================================ */

Mix_Music *
Mix_LoadMUS_RW(SDL_RWops *src, int freesrc)
{
	if (src && freesrc)
	{
		SDL_RWclose(src);
	}

	return NULL;
}

void
Mix_FreeMusic(Mix_Music *music)
{
	(void)music;
}

int
Mix_PlayMusic(Mix_Music *music, int loops)
{
	(void)loops;
	return music ? 0 : -1;
}

int
Mix_HaltMusic(void)
{
	return 0;
}

void
Mix_PauseMusic(void)
{
}

void
Mix_ResumeMusic(void)
{
}

int
Mix_PausedMusic(void)
{
	return 0;
}

int
Mix_PlayingMusic(void)
{
	return 0;
}

int
Mix_VolumeMusic(int volume)
{
	(void)volume;
	return MIX_MAX_VOLUME;
}

double
Mix_GetMusicPosition(Mix_Music *music)
{
	(void)music;
	return 0.0;
}

int
Mix_SetMusicPosition(double position)
{
	(void)position;
	return 0;
}

void
Mix_HookMusicFinished(void (*music_finished)(void))
{
	(void)music_finished;
}

int
Mix_SetSoundFonts(const char *paths)
{
	(void)paths;
	return 0;
}

/* SDL's defaults for every channel, before anything plays */
__attribute__((constructor)) static void
MixerDefaults(void)
{
	int c;

	for (c = 0; c < MAX_MIX_CHANNELS; c++)
	{
		ChannelDefaults(&channels[c]);
	}
}
