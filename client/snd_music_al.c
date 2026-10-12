/*
 * Private bounded OpenAL queue for disk music.
 * Copyright (C) 2026 R1Q2 contributors
 * Distributed under the GNU General Public License, version 2 or later.
 */
#include "snd_music_al.h"

#ifdef USE_OPENAL
#ifdef _WIN32
#include <windows.h>
#endif
#include "snd_openal.h"
#include "snd_music.h"

#define MUSIC_AL_BUFFERS 4
#define MUSIC_AL_FRAMES 1024
#define MUSIC_AL_RATE 44100

static ALuint music_al_source;
static ALuint music_al_buffers[MUSIC_AL_BUFFERS];
static int music_al_used[MUSIC_AL_BUFFERS];
static int music_al_queued;
static unsigned music_al_generation;

void S_MusicALShutdown (void)
{
	int i, owned;

	owned = music_al_source != 0;
	if (music_al_source)
	{
		qalSourceStop (music_al_source);
		qalSourcei (music_al_source, AL_BUFFER, 0);
		qalDeleteSources (1, &music_al_source);
		music_al_source = 0;
	}
	for (i = 0; i < MUSIC_AL_BUFFERS; i++)
	{
		if (music_al_buffers[i])
		{
			owned = 1;
			qalDeleteBuffers (1, &music_al_buffers[i]);
			music_al_buffers[i] = 0;
		}
		music_al_used[i] = 0;
	}
	music_al_queued = 0;
	/* Do not leave this optional queue's errors for the effect mixer. */
	if (owned)
		qalGetError ();
}

static void S_MusicALFailed (int error)
{
	Com_Printf ("OpenAL music disabled (error %d).\n", LOG_CLIENT | LOG_WARNING, error);
	S_MusicALShutdown ();
}

static int S_MusicALCheck (void)
{
	ALenum error;

	error = qalGetError ();
	if (error == AL_NO_ERROR)
		return 1;
	S_MusicALFailed (error);
	return 0;
}

static int S_MusicALReset (unsigned generation)
{
	int i;

	qalSourceStop (music_al_source);
	qalSourcei (music_al_source, AL_BUFFER, 0);
	if (!S_MusicALCheck ())
		return 0;
	for (i = 0; i < MUSIC_AL_BUFFERS; i++)
		music_al_used[i] = 0;
	music_al_queued = 0;
	music_al_generation = generation;
	return 1;
}

int S_MusicALInit (void)
{
	int i;

	if (music_al_source)
		return 1;
	qalGetError ();
	qalGenSources (1, &music_al_source);
	if (!S_MusicALCheck ())
		return 0;
	for (i = 0; i < MUSIC_AL_BUFFERS; i++)
	{
		qalGenBuffers (1, &music_al_buffers[i]);
		if (!S_MusicALCheck ())
			return 0;
	}
	S_OpenAL_SpatializeSource (music_al_source, AL_FALSE);
	qalSourcei (music_al_source, AL_SOURCE_RELATIVE, AL_TRUE);
	qalSourcei (music_al_source, AL_LOOPING, AL_FALSE);
	qalSource3f (music_al_source, AL_POSITION, 0, 0, 0);
	qalSource3f (music_al_source, AL_VELOCITY, 0, 0, 0);
	qalSourcef (music_al_source, AL_ROLLOFF_FACTOR, 0);
	if (!S_MusicALCheck ())
		return 0;
	music_al_generation = S_MusicGeneration ();
	return 1;
}

void S_MusicALUpdate (void)
{
	unsigned char pcm[MUSIC_AL_FRAMES * 4];
	ALshort native[MUSIC_AL_FRAMES * 2];
	ALuint buffer;
	ALint state, processed;
	unsigned generation;
	int i, j, count, sample;

	if (!music_al_source)
		return;
	qalGetError ();
	generation = S_MusicGeneration ();
	if (generation != music_al_generation && !S_MusicALReset (generation))
		return;
	qalSourcef (music_al_source, AL_GAIN, S_MusicVolume ());
	qalGetSourcei (music_al_source, AL_SOURCE_STATE, &state);
	if (!S_MusicALCheck ())
		return;
	if (S_MusicPaused ())
	{
		if (state == AL_PLAYING)
		{
			qalSourcePause (music_al_source);
			S_MusicALCheck ();
		}
		return;
	}
	qalGetSourcei (music_al_source, AL_BUFFERS_PROCESSED, &processed);
	if (!S_MusicALCheck ())
		return;
	if (processed < 0 || processed > music_al_queued)
	{
		S_MusicALFailed (AL_INVALID_VALUE);
		return;
	}
	for (i = 0; i < processed; i++)
	{
		qalSourceUnqueueBuffers (music_al_source, 1, &buffer);
		if (!S_MusicALCheck ())
			return;
		for (j = 0; j < MUSIC_AL_BUFFERS; j++)
			if (music_al_used[j] && music_al_buffers[j] == buffer)
				break;
		if (j == MUSIC_AL_BUFFERS)
		{
			S_MusicALFailed (AL_INVALID_VALUE);
			return;
		}
		music_al_used[j] = 0;
		music_al_queued--;
	}
	for (i = 0; i < MUSIC_AL_BUFFERS; i++)
	{
		if (music_al_used[i])
			continue;
		count = S_MusicRead (pcm, MUSIC_AL_FRAMES, MUSIC_AL_RATE);
		/* A fatal read can stop the controller after producing a partial block. */
		generation = S_MusicGeneration ();
		if (generation != music_al_generation)
		{
			S_MusicALReset (generation);
			return;
		}
		if (count <= 0)
			break;
		if (count > MUSIC_AL_FRAMES)
		{
			S_MusicALFailed (AL_INVALID_VALUE);
			return;
		}
		/* AL takes native signed shorts; the controller always supplies S16LE. */
		for (j = 0; j < count * 2; j++)
		{
			sample = pcm[j * 2] | (pcm[j * 2 + 1] << 8);
			if (sample >= 32768)
				sample -= 65536;
			native[j] = (ALshort)sample;
		}
		qalBufferData (music_al_buffers[i], AL_FORMAT_STEREO16, native,
			count * 4, MUSIC_AL_RATE);
		if (!S_MusicALCheck ())
			return;
		qalSourceQueueBuffers (music_al_source, 1, &music_al_buffers[i]);
		if (!S_MusicALCheck ())
			return;
		music_al_used[i] = 1;
		music_al_queued++;
	}
	qalGetSourcei (music_al_source, AL_SOURCE_STATE, &state);
	if (!S_MusicALCheck ())
		return;
	if (music_al_queued && state != AL_PLAYING)
	{
		qalSourcePlay (music_al_source);
		S_MusicALCheck ();
	}
}

#else

int S_MusicALInit (void)
{
	return 0;
}

void S_MusicALShutdown (void)
{
}

void S_MusicALUpdate (void)
{
}

#endif
