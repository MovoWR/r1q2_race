/*
 * Private bounded OpenAL queue for cinematic audio.
 * Copyright (C) 2026 R1Q2 contributors
 * Distributed under the GNU General Public License, version 2 or later.
 */
#include "snd_movie_al.h"

#ifdef USE_OPENAL
#ifdef _WIN32
#include <windows.h>
#endif
#include "snd_openal.h"
#include "cl_movie.h"

#ifndef AL_SAMPLE_OFFSET
#define AL_SAMPLE_OFFSET 0x1025
#endif
#define MOVIE_AL_BUFFERS 4
#define MOVIE_AL_FRAMES 1024
#define MOVIE_AL_RATE 44100

static ALuint movie_al_source;
static ALuint movie_al_buffers[MOVIE_AL_BUFFERS];
static int movie_al_frames[MOVIE_AL_BUFFERS];
static int movie_al_queued;
static unsigned movie_al_generation;

void S_MovieALShutdown (void)
{
	int i, owned;

	owned = movie_al_source != 0;
	if (movie_al_source)
	{
		qalSourceStop (movie_al_source);
		qalSourcei (movie_al_source, AL_BUFFER, 0);
		qalDeleteSources (1, &movie_al_source);
		movie_al_source = 0;
	}
	for (i = 0; i < MOVIE_AL_BUFFERS; i++)
	{
		if (movie_al_buffers[i])
		{
			owned = 1;
			qalDeleteBuffers (1, &movie_al_buffers[i]);
			movie_al_buffers[i] = 0;
		}
		movie_al_frames[i] = 0;
	}
	movie_al_queued = 0;
	if (owned)
		qalGetError ();
}

static void S_MovieALFailed (int error)
{
	Com_Printf ("OpenAL movie audio disabled (error %d).\n", LOG_CLIENT | LOG_WARNING, error);
	S_MovieALShutdown ();
}

static int S_MovieALCheck (void)
{
	ALenum error;

	error = qalGetError ();
	if (error == AL_NO_ERROR)
		return 1;
	S_MovieALFailed (error);
	return 0;
}

static int S_MovieALReset (unsigned generation)
{
	int i;

	qalSourceStop (movie_al_source);
	qalSourcei (movie_al_source, AL_BUFFER, 0);
	if (!S_MovieALCheck ())
		return 0;
	for (i = 0; i < MOVIE_AL_BUFFERS; i++)
		movie_al_frames[i] = 0;
	movie_al_queued = 0;
	movie_al_generation = generation;
	return 1;
}

int S_MovieALInit (void)
{
	int i;

	if (movie_al_source)
		return 1;
	qalGetError ();
	qalGenSources (1, &movie_al_source);
	if (!S_MovieALCheck ())
		return 0;
	for (i = 0; i < MOVIE_AL_BUFFERS; i++)
	{
		qalGenBuffers (1, &movie_al_buffers[i]);
		if (!S_MovieALCheck ())
			return 0;
	}
	S_OpenAL_SpatializeSource (movie_al_source, AL_FALSE);
	qalSourcei (movie_al_source, AL_SOURCE_RELATIVE, AL_TRUE);
	qalSourcei (movie_al_source, AL_LOOPING, AL_FALSE);
	qalSource3f (movie_al_source, AL_POSITION, 0, 0, 0);
	qalSource3f (movie_al_source, AL_VELOCITY, 0, 0, 0);
	qalSourcef (movie_al_source, AL_ROLLOFF_FACTOR, 0);
	/* The listener already applies the master volume. */
	qalSourcef (movie_al_source, AL_GAIN, 1);
	if (!S_MovieALCheck ())
		return 0;
	movie_al_generation = CL_MovieGeneration ();
	return 1;
}

static int S_MovieALLead (double *lead)
{
	ALuint buffer;
	ALint processed, offset;
	int i, j, frames;

	qalGetSourcei (movie_al_source, AL_BUFFERS_PROCESSED, &processed);
	if (!S_MovieALCheck ())
		return 0;
	if (processed < 0 || processed > movie_al_queued)
	{
		S_MovieALFailed (AL_INVALID_VALUE);
		return 0;
	}
	for (i = 0; i < processed; i++)
	{
		qalSourceUnqueueBuffers (movie_al_source, 1, &buffer);
		if (!S_MovieALCheck ())
			return 0;
		for (j = 0; j < MOVIE_AL_BUFFERS; j++)
			if (movie_al_frames[j] && movie_al_buffers[j] == buffer)
				break;
		if (j == MOVIE_AL_BUFFERS)
		{
			S_MovieALFailed (AL_INVALID_VALUE);
			return 0;
		}
		movie_al_frames[j] = 0;
		movie_al_queued--;
	}
	frames = 0;
	for (i = 0; i < MOVIE_AL_BUFFERS; i++)
		frames += movie_al_frames[i];
	qalGetSourcei (movie_al_source, AL_SAMPLE_OFFSET, &offset);
	if (!S_MovieALCheck ())
		return 0;
	if (offset < 0 || offset > frames)
	{
		S_MovieALFailed (AL_INVALID_VALUE);
		return 0;
	}
	*lead = (double)(frames - offset) / MOVIE_AL_RATE;
	return 1;
}

void S_MovieALUpdate (void)
{
	unsigned char pcm[MOVIE_AL_FRAMES * 4];
	ALshort native[MOVIE_AL_FRAMES * 2];
	ALint state, initial_state;
	unsigned generation;
	double lead;
	int i, j, count, sample, paused;

	if (!movie_al_source)
		return;
	qalGetError ();
	paused = CL_MoviePaused ();
	generation = CL_MovieGeneration ();
	if (generation != movie_al_generation && !S_MovieALReset (generation))
		return;
	qalGetSourcei (movie_al_source, AL_SOURCE_STATE, &initial_state);
	if (!S_MovieALCheck ())
		return;
	if (paused)
	{
		if (initial_state == AL_PLAYING)
		{
			qalSourcePause (movie_al_source);
			S_MovieALCheck ();
		}
		return;
	}
	/* A stopped source has no audible lead, even if buffers remain attached. */
	if (initial_state == AL_STOPPED && movie_al_queued && !S_MovieALReset (generation))
		return;
	for (i = 0; i < MOVIE_AL_BUFFERS; i++)
	{
		/* Sample after retiring processed buffers; account for partial buffers. */
		if (!S_MovieALLead (&lead))
			return;
		if (movie_al_frames[i])
			continue;
		count = CL_MovieReadAudio (pcm, MOVIE_AL_FRAMES, MOVIE_AL_RATE, lead);
		generation = CL_MovieGeneration ();
		if (generation != movie_al_generation)
		{
			S_MovieALReset (generation);
			return;
		}
		if (count < 0 || count > MOVIE_AL_FRAMES)
		{
			S_MovieALFailed (AL_INVALID_VALUE);
			return;
		}
		if (!count)
			break;
		for (j = 0; j < count * 2; j++)
		{
			sample = pcm[j * 2] | (pcm[j * 2 + 1] << 8);
			if (sample >= 32768)
				sample -= 65536;
			native[j] = (ALshort)sample;
		}
		qalBufferData (movie_al_buffers[i], AL_FORMAT_STEREO16, native,
			count * 4, MOVIE_AL_RATE);
		if (!S_MovieALCheck ())
			return;
		qalSourceQueueBuffers (movie_al_source, 1, &movie_al_buffers[i]);
		if (!S_MovieALCheck ())
			return;
		movie_al_frames[i] = count;
		movie_al_queued++;
	}
	qalGetSourcei (movie_al_source, AL_SOURCE_STATE, &state);
	if (!S_MovieALCheck ())
		return;
	/* Refill can itself outlast the old queue. Rebase on the next update. */
	if (state == AL_STOPPED && initial_state == AL_PLAYING)
	{
		S_MovieALReset (generation);
		return;
	}
	if (movie_al_queued && state != AL_PLAYING)
	{
		qalSourcePlay (movie_al_source);
		S_MovieALCheck ();
	}
}

#else

int S_MovieALInit (void)
{
	return 0;
}

void S_MovieALShutdown (void)
{
}

void S_MovieALUpdate (void)
{
}

#endif
