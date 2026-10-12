/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// snd_mem.c: sound caching

#include "client.h"
#include "snd_loc.h"
#include "snd_ogg.h"
#include <limits.h>

#define MAX_WAV_SAMPLES ((1 << 23) - 1)

static unsigned S_WavU16 (const byte *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static uint32 S_WavU32 (const byte *p)
{
	return (uint32)p[0] | ((uint32)p[1] << 8) |
		((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}

static qboolean S_ParseWAV (const char *name, const byte *wav, int wavlength,
	wavinfo_t *info)
{
	const byte *fmt = NULL, *data = NULL, *cue = NULL, *list = NULL;
	size_t end, pos, fmtlen = 0, datalen = 0, cuelen = 0, listlen = 0;
	unsigned channels, rate, width, align;
	uint32 riffsize;
	const char *error = "invalid RIFF/WAVE header";

	memset (info, 0, sizeof (*info));
	info->loopstart = -1;
	if (!wav || wavlength < 12 || memcmp (wav, "RIFF", 4) ||
		memcmp (wav + 8, "WAVE", 4))
		goto invalid;

	riffsize = S_WavU32 (wav + 4);
	if (riffsize < 4)
		goto invalid;
	/* Old sound packs can overstate RIFF/data lengths. Never read beyond the
	 * actual file; Q2PRO likewise bounds PCM by the bytes available. */
	end = riffsize > (uint32)(wavlength - 8) ? (size_t)wavlength : 8 + (size_t)riffsize;

	for (pos = 12; pos < end;)
	{
		size_t size, payload, next;
		if (end - pos < 8)
		{
			error = "truncated WAV chunk header";
			goto invalid;
		}
		size = S_WavU32 (wav + pos + 4);
		payload = pos + 8;
		if (size > end - payload)
		{
			/* Only a terminal PCM payload may be shortened. Format and
			 * metadata chunks must remain structurally complete. */
			if (memcmp (wav + pos, "data", 4))
			{
				error = "WAV chunk exceeds RIFF bounds";
				goto invalid;
			}
			size = end - payload;
		}
		next = payload + size;
		if ((size & 1) && next < end)
			next++;
		if (!fmt && !memcmp (wav + pos, "fmt ", 4))
		{
			fmt = wav + payload;
			fmtlen = size;
		}
		else if (!data && !memcmp (wav + pos, "data", 4))
		{
			data = wav + payload;
			datalen = size;
		}
		else if (!cue && !memcmp (wav + pos, "cue ", 4))
		{
			cue = wav + payload;
			cuelen = size;
		}
		else if (cue && !list && !memcmp (wav + pos, "LIST", 4))
		{
			list = wav + payload;
			listlen = size;
		}
		pos = next;
	}

	error = "missing or short PCM fmt chunk";
	if (!fmt || fmtlen < 16)
		goto invalid;
	error = "unsupported WAV format";
	if (S_WavU16 (fmt) != 1)
		goto invalid;
	channels = S_WavU16 (fmt + 2);
	rate = S_WavU32 (fmt + 4);
	width = S_WavU16 (fmt + 14);
	if (channels < 1 || channels > 2 || rate < 6000 || rate > 48000 ||
		(width != 8 && width != 16 && width != 24))
		goto invalid;
	width /= 8;
	align = channels * width;

	/* Discard an incomplete final sample frame, as Q2PRO does. */
	datalen -= datalen % align;
	error = "missing WAV data frames";
	if (!data || !datalen)
		goto invalid;
	if (datalen / align > MAX_WAV_SAMPLES)
	{
		error = "too many WAV sample frames";
		goto invalid;
	}
	info->rate = rate;
	info->width = width;
	info->channels = channels;
	info->samples = (int)(datalen / align);
	info->dataofs = (int)(data - wav);

	if (cue && cuelen >= 28 && S_WavU32 (cue) > 0)
	{
		uint32 start = S_WavU32 (cue + 24);
		if (start < (uint32)info->samples)
		{
			info->loopstart = (int)start;
			if (list && listlen >= 24 && !memcmp (list + 20, "mark", 4))
			{
				uint32 length = S_WavU32 (list + 16);
				if (length && length <= (uint32)info->samples - start)
					info->samples = (int)(start + length);
			}
		}
	}
	return true;

invalid:
	Com_Printf ("WAV %s: %s\n", LOG_CLIENT | LOG_WARNING,
		name ? name : "<memory>", error);
	memset (info, 0, sizeof (*info));
	info->loopstart = -1;
	return false;
}

/* WAV samples stay in the FS buffer; Vorbis samples belong to ogg until
 * S_FreeOgg. Both paths expose little-endian PCM to the retained resampler. */
static qboolean S_ParseSound (const char *name, const byte *data, int length,
	wavinfo_t *info, snd_ogg_t *ogg)
{
	char error[128];
	if (!S_IsOggSound (name, data, length))
		return S_ParseWAV (name, data, length, info);
	memset (info, 0, sizeof (*info));
	info->loopstart = -1;
	if (!S_DecodeOgg (data, length, ogg, error, sizeof (error)))
	{
		Com_Printf ("Vorbis %s: %s\n", LOG_CLIENT | LOG_WARNING, name, error);
		return false;
	}
	info->rate = ogg->rate;
	info->width = 2;
	info->channels = ogg->channels;
	info->samples = ogg->frames;
	return true;
}

static byte *S_CopyWAVSamples (const byte *data, int samples, int channels,
	int width, int *outwidth)
{
	size_t count = (size_t)samples * channels;
	size_t bytes = count * (width == 3 ? 2 : width);
	byte *out;
	size_t i;

	if (!count || bytes > INT_MAX)
		return NULL;
	out = Z_TagMalloc ((int)bytes, TAGMALLOC_CLIENT_SOUNDCACHE);
	if (!out)
		return NULL;
	if (width == 3)
	{
		for (i = 0; i < count; i++)
		{
			out[i * 2] = data[i * 3 + 1];
			out[i * 2 + 1] = data[i * 3 + 2];
		}
		*outwidth = 2;
	}
	else
	{
		memcpy (out, data, bytes);
		*outwidth = width;
	}
	return out;
}

//int			cache_full_cycle;

byte *S_Alloc (int size);

/*
================
ResampleSfx
================
*/
void ResampleSfx (sfx_t *sfx, int inrate, int inwidth, byte *data)
{
	int		outcount;
	int inframes;
	int		srcsample;
	float	stepscale;
	int		i;
	int		sample;
	uint32	samplefrac, fracstep;
	sfxcache_t	*sc;
	
	sc = sfx->cache;
	if (!sc)
		return;
	inframes = sc->length;

	stepscale = (float)inrate / dma.speed;	// this is usually 0.5, 1, or 2

	outcount = (int)(sc->length / stepscale);
	
	if (outcount == 0)
	{
		Com_Printf ("ResampleSfx: Invalid sound file '%s' (zero length)\n", LOG_CLIENT|LOG_WARNING, sfx->name);
		//free at next opportunity
		//sfx->registration_sequence = 0;
		Z_Free (sfx->cache);
		sfx->cache = NULL;
		return;
	}

	sc->length = outcount;

	if (sc->loopstart != -1)
	{
		int loop = (int)(sc->loopstart / stepscale);
		sc->loopstart = loop < outcount ? loop : -1;
	}

	sc->speed = dma.speed;
	if (s_loadas8bit->intvalue)
		sc->width = 1;
	else
		sc->width = inwidth;
	sc->stereo = 0;

// resample / decimate to the current source rate

	if (stepscale == 1 && inwidth == 1 && sc->width == 1)
	{
// fast special case
		//Com_DPrintf ("ResampleSfx: special case used.\n");
		for (i=0 ; i<outcount ; i++)
			((signed char *)sc->data)[i]
			= (int)( (unsigned char)(data[i]) - 128);
	}
	else
	{
// general case
		//Com_Printf ("WARNING: ResampleSfx: general case used, step %f in %d sc %d\n", stepscale, inwidth, sc->width);
		samplefrac = 0;
		fracstep = (int)(stepscale*256);
		for (i=0 ; i<outcount ; i++)
		{
			srcsample = samplefrac >> 8;
			samplefrac += fracstep;
			if (inwidth == 2)
			{
				if (srcsample >= inframes)
					srcsample = inframes - 1;
				sample = S_WavU16 (data + srcsample * 2);
				if (sample >= 32768)
					sample -= 65536;
			}
			else
			{
				if (srcsample >= inframes)
					srcsample = inframes - 1;
				sample = ((int)data[srcsample] - 128) * 256;
			}
			if (sc->width == 2)
				((int16 *)sc->data)[i] = sample;
			else
				((signed char *)sc->data)[i] = sample >= 0 ?
					sample / 256 :
					-((255 - sample) / 256);
		}
	}
}

static void S_ResampleStereoSfx (sfx_t *sfx, int inrate, int inwidth,
	const byte *data)
{
	sfxcache_t *sc = sfx->cache;
	int inframes = sc->length;
	float stepscale = (float)inrate / dma.speed;
	int outcount = (int)(inframes / stepscale);
	uint64 samplefrac = 0;
	uint64 fracstep = (uint32)(stepscale * 256);
	int i, channel;

	sc->length = outcount;
	if (sc->loopstart != -1)
	{
		int loop = (int)(sc->loopstart / stepscale);
		sc->loopstart = loop < outcount ? loop : -1;
	}
	sc->speed = dma.speed;
	sc->width = s_loadas8bit->intvalue ? 1 : inwidth;
	sc->stereo = 1;
	for (i = 0; i < outcount; i++, samplefrac += fracstep)
	{
		int source = (int)(samplefrac >> 8);
		if (source >= inframes)
			source = inframes - 1;
		for (channel = 0; channel < 2; channel++)
		{
			int index = source * 2 + channel;
			int sample;
			if (inwidth == 2)
			{
				sample = S_WavU16 (data + index * 2);
				if (sample >= 32768)
					sample -= 65536;
			}
			else
				sample = ((int)data[index] - 128) * 256;
			if (sc->width == 2)
				((int16 *)sc->data)[i * 2 + channel] = (int16)sample;
			else
				((signed char *)sc->data)[i * 2 + channel] =
					(signed char)(sample >= 0 ? sample / 256 :
												-((255 - sample) / 256));
		}
	}
}

//=============================================================================

#ifdef USE_OPENAL

static qboolean S_OpenAL_UploadSound (byte *data, int width, int channels, sfx_t *sfx)
{
	int		size;

	// Calculate buffer size
	size = sfx->samples * width * channels;

	// Set buffer format
	if (width == 2)
	{
		if (channels == 2)
			sfx->format = AL_FORMAT_STEREO16;
		else
			sfx->format = AL_FORMAT_MONO16;
	}
	else
	{
		if (channels == 2)
			sfx->format = AL_FORMAT_STEREO8;
		else
			sfx->format = AL_FORMAT_MONO8;
	}

	// Upload the sound
	qalGetError ();
	qalGenBuffers(1, &sfx->bufferNum);
	if (qalGetError () != AL_NO_ERROR)
		goto failed;
	qalBufferData(sfx->bufferNum, sfx->format, data, size, sfx->rate);
	if (qalGetError () != AL_NO_ERROR)
		goto failed;
	if (sfx->loopstart > 0 && alConfig.bufferiv)
	{
		ALint points[2];
		points[0] = sfx->loopstart;
		points[1] = sfx->samples;
		alConfig.bufferiv (sfx->bufferNum, AL_LOOP_POINTS_SOFT, points);
		if (qalGetError () != AL_NO_ERROR)
		{
			alConfig.bufferiv = NULL;
			Com_Printf ("OpenAL loop points unavailable; looping whole buffers.\n",
				LOG_CLIENT | LOG_WARNING);
		}
	}
	return true;

failed:
	if (sfx->bufferNum)
		qalDeleteBuffers (1, &sfx->bufferNum);
	sfx->bufferNum = 0;
	qalGetError ();
	Com_Printf ("OpenAL sound upload failed: %s\n", LOG_CLIENT | LOG_WARNING, sfx->name);
	return false;
}

static qboolean S_OpenAL_LoadWAV (const char *name, byte **wav, wavInfo_t *info);
qboolean S_OpenAL_LoadSound (sfx_t *sfx)
{
    char		name[MAX_QPATH];
	byte		*data;
	wavInfo_t	info;

	if (sfx->name[0] == '*')
		return false;

	// See if still in memory
	if (sfx->loaded)
		return true;

	// Load it from disk
	if (sfx->name[0] == '#')
		Com_sprintf(name, sizeof(name), "%s", &sfx->name[1]);
	else
		Com_sprintf(name, sizeof(name), "sound/%s", sfx->name);

	if (!S_OpenAL_LoadWAV(name, &data, &info))
	{
		Com_DPrintf ("WARNING: couldn't find sound '%s'\n", name);
		return false;
		//S_CreateDefaultSound(&data, &info);
	}

	// Load it in
	sfx->samples = info.samples;
	sfx->rate = info.rate;
	sfx->loopstart = info.loopstart;

	sfx->loaded = S_OpenAL_UploadSound (data, info.width, info.channels, sfx);

	Z_Free(data);

	return sfx->loaded;
}

#endif

/*
==============
S_LoadSound
==============
*/
sfxcache_t *S_LoadSound (sfx_t *s)
{
    char	namebuffer[MAX_QPATH];
	byte	*data;
	byte *converted = NULL;
	const byte *pcm;
	wavinfo_t	info;
	snd_ogg_t ogg = { 0 };
	int outcount, outwidth, pcmwidth;
	float	stepscale;
	double frames;
	size_t cachebytes;
	sfxcache_t *sc = NULL;
	int		size;
	char	*name;

	if (s->name[0] == '*')
		return NULL;

// see if still in memory
	sc = s->cache;
	if (sc)
		return sc;

//Com_Printf ("S_LoadSound: %x\n", (int)stackbuf);
// load it in
	if (s->truename)
		name = s->truename;
	else
		name = s->name;

	if (name[0] == '#')
		strcpy(namebuffer, &name[1]);
	else
		Com_sprintf (namebuffer, sizeof(namebuffer), "sound/%s", name);

//	Com_Printf ("loading %s\n",namebuffer);

	size = FS_LoadFile (namebuffer, (void **)&data);

	if (!data)
	{
		s->cache = NULL;
		Com_DPrintf ("Couldn't load %s\n", namebuffer);
		return NULL;
	}

	if (!S_ParseSound (namebuffer, data, size, &info, &ogg))
		goto done;
	pcm = ogg.data ? ogg.data : data + info.dataofs;
	pcmwidth = info.width;
	if (pcmwidth == 3)
	{
		converted = S_CopyWAVSamples (pcm, info.samples, info.channels,
			pcmwidth, &pcmwidth);
		if (!converted)
		{
			Com_Printf ("WAV %s: could not convert 24-bit samples\n",
				LOG_CLIENT | LOG_WARNING, s->name);
			goto done;
		}
		pcm = converted;
	}
	if (dma.speed <= 0)
		goto done;
	stepscale = (float)info.rate / dma.speed;
	frames = (double)info.samples / stepscale;
	if (frames < 1 || frames >= (double)INT_MAX - 256)
	{
		Com_Printf ("WAV %s: invalid resampled length\n",
			LOG_CLIENT | LOG_WARNING, s->name);
		goto done;
	}
	outcount = (int)(info.samples / stepscale);
	if (!outcount)
		goto done;
	outwidth = s_loadas8bit->intvalue ? 1 : pcmwidth;
	if ((size_t)outcount > (INT_MAX - sizeof (sfxcache_t)) /
			((size_t)info.channels * outwidth))
	{
		Com_Printf ("WAV %s: resampled cache is too large\n",
			LOG_CLIENT | LOG_WARNING, s->name);
		goto done;
	}
	cachebytes = (size_t)outcount * info.channels * outwidth;
	sc = s->cache = Z_TagMalloc ((int)(cachebytes + sizeof (sfxcache_t)),
		TAGMALLOC_CLIENT_SOUNDCACHE);
	if (!sc)
		goto done;
	sc->length = info.samples;
	sc->loopstart = info.loopstart;
	sc->speed = info.rate;
	sc->width = pcmwidth;
	sc->stereo = info.channels - 1;
	if (info.channels == 2)
		S_ResampleStereoSfx (s, sc->speed, sc->width, pcm);
	else
		ResampleSfx (s, sc->speed, sc->width, (byte *)pcm);
	sc = s->cache;

done:
	if (converted)
		Z_Free (converted);
	S_FreeOgg (&ogg);
	FS_FreeFile (data);
	return sc;
}



/*
===============================================================================

WAV loading

===============================================================================
*/


#ifdef USE_OPENAL
static qboolean S_OpenAL_LoadWAV (const char *name, byte **wav, wavInfo_t *info)
{
	byte *buffer, *out;
	wavinfo_t parsed;
	snd_ogg_t ogg = { 0 };
	int length, width;
	qboolean success = false;
	const byte *pcm;

	*wav = NULL;
	memset (info, 0, sizeof (*info));
	length = FS_LoadFile (name, (void **)&buffer);
	if (!buffer)
		return false;
	if (!S_ParseSound (name, buffer, length, &parsed, &ogg))
		goto done;
	pcm = ogg.data ? ogg.data : buffer + parsed.dataofs;
	out = S_CopyWAVSamples (pcm, parsed.samples,
		parsed.channels, parsed.width, &width);
	if (!out)
	{
		Com_Printf ("Sound %s: could not allocate converted samples\n",
			LOG_CLIENT | LOG_WARNING, name);
		goto done;
	}
	if (width == 2)
	{
		const unsigned host = 1;
		if (*(const byte *)&host != 1)
		{
			size_t i, count = (size_t)parsed.samples * parsed.channels;
			for (i = 0; i < count; i++)
			{
				byte first = out[i * 2];
				out[i * 2] = out[i * 2 + 1];
				out[i * 2 + 1] = first;
			}
		}
	}
	info->rate = parsed.rate;
	info->width = width;
	info->channels = parsed.channels;
	info->samples = parsed.samples;
	info->loopstart = parsed.loopstart;
	*wav = out;
	success = true;
done:
	S_FreeOgg (&ogg);
	FS_FreeFile (buffer);
	return success;
}
#endif

wavinfo_t GetWavinfo (char *name, byte *wav, int wavlength)
{
	wavinfo_t info;
	S_ParseWAV (name, wav, wavlength, &info);
	return info;
}
