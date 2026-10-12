/* Replacement movie policy adapted from Q2PRO src/client/cin.c,
 * revision 601a8df8433b0c50dbbe37c0716c3793fff140a7.
 * Copyright (C) 2023 Andrey Nazarov
 * GPL version 2 or (at your option) later. WITHOUT WARRANTY. See COPYING. */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#endif
#include "client.h"
#include "cl_movie.h"
#include "cl_media.h"
#include "ref_present.h"
#include "snd_movie_al.h"
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef CINEMATICS
#define MOVIE_AUDIO_BYTES (2 * 1024 * 1024)
#define MOVIE_AUDIO_BLOCKS 2048
#define MOVIE_STEPS 128

typedef struct
{
	unsigned char *data;
	size_t size;
	int width, height, stride, number;
	double time, duration;
} movie_pic_t;

typedef struct movie_audio_s
{
	struct movie_audio_s *next;
	double time, end;
	int samples;
	unsigned char data[1];
} movie_audio_t;

static struct
{
	cl_media_t *decoder;
	FILE *file;
	movie_pic_t current, pending;
	movie_audio_t *audio, *audio_tail;
	size_t audio_bytes;
	int audio_blocks, rate, paused, failed, video_eof, audio_eof, dirty;
	int frames, crop_start, crop_pixels;
	unsigned clock;
	double time, video_end, audio_end;
} movie;

static unsigned movie_generation;

static void CL_MovieFail (const char *message)
{
	if (!movie.failed)
	{
		Com_Printf ("Movie: %s\n", LOG_CLIENT | LOG_WARNING, message);
		movie.failed = 1;
		movie_generation++;
	}
}

void CL_MovieStop (void)
{
	movie_audio_t *next;
	if (movie.decoder || movie.file || movie.current.data || movie.audio)
		movie_generation++;
	CL_MediaClose (&movie.decoder);
	if (movie.file)
		fclose (movie.file);
	free (movie.current.data);
	free (movie.pending.data);
	while (movie.audio)
	{
		next = movie.audio->next;
		free (movie.audio);
		movie.audio = next;
	}
	memset (&movie, 0, sizeof (movie));
}

int CL_MovieActive (void)
{
	return movie.decoder != NULL;
}

unsigned CL_MovieGeneration (void)
{
	return movie_generation;
}

static void CL_MovieSetPaused (int paused)
{
	if (movie.paused != paused)
	{
		movie_generation++;
		movie.paused = paused;
		movie.clock = (unsigned)cls.realtime;
	}
}

int CL_MoviePaused (void)
{
	if (!movie.decoder || movie.failed)
		return 1;
	/* Sound runs before the cinematic update. Rebase a resumed stream before
	 * the backend reads its generation or pulls the first resumed samples. */
	CL_MovieSetPaused (cls.key_dest != key_game || cls.disable_screen);
	return movie.paused;
}

static int CL_MovieName (const char *name, char *out, size_t size)
{
	size_t i, start = 0, length;
	char *dot, *slash;
	if (!name || !(length = strlen (name)) || length >= size)
		return 0;
	for (i = 0; i <= length; i++)
	{
		int c = (unsigned char)name[i];
		if (c == '\\')
			c = '/';
		if (c && (c < 32 || c == 127 || c == ':'))
			return 0;
		out[i] = (char)c;
		if (!c || c == '/')
		{
			if (i == start || (i - start == 1 && out[start] == '.') ||
				(i - start == 2 && out[start] == '.' && out[start + 1] == '.'))
				return 0;
			start = i + 1;
		}
	}
	dot = strrchr (out, '.');
	slash = strrchr (out, '/');
	if (dot && (!slash || dot > slash))
		*dot = 0;
	return *out && (!slash || slash[1]);
}

static FILE *CL_MovieFile (const char *path, int *size)
{
	FILE *file;
#ifdef _WIN32
	struct _stat64 info;
	if (_stat64 (path, &info) || (info.st_mode & _S_IFMT) != _S_IFREG)
		return NULL;
	file = fopen (path, "rb");
#else
	struct stat info;
	int fd = open (path, O_RDONLY | O_NONBLOCK | O_NOCTTY);
	if (fd < 0)
		return NULL;
	if (fstat (fd, &info) || !S_ISREG (info.st_mode))
	{
		close (fd);
		return NULL;
	}
	file = fdopen (fd, "rb");
	if (!file)
		close (fd);
#endif
	*size = info.st_size > 0 && info.st_size <= INT_MAX ? (int)info.st_size : 0;
	return file;
}

int CL_MovieStart (const char *name)
{
	static const char *extensions[] = { ".ogv", ".mkv", ".mp4", ".cin" };
	char normalized[MAX_OSPATH], full[MAX_OSPATH], error[160], classic[MAX_OSPATH];
	const char *path = NULL;
	int i, size, available = 0, paths = 0;
	CL_MovieStop ();
	if (!scr_color_presentation || !scr_color_presentation->UpdateColorFrame ||
		!scr_color_presentation->DrawColorFrame || !CL_MovieName (name, normalized, sizeof (normalized)))
		return 0;
	for (i = 0; i < 4; i++)
		if (CL_MediaFormatAvailable (extensions[i]))
			available |= 1 << i;
	if (!available)
		return 0;
	while ((path = FS_NextPath (path)) != NULL && paths++ < 256)
	{
		for (i = 0; i < 4; i++)
		{
			if (!(available & (1 << i)) || strlen (path) + strlen (normalized) + 12 >= sizeof (full))
				continue;
			Com_sprintf (full, sizeof (full), "%s/video/%s%s", path, normalized, extensions[i]);
			movie.file = CL_MovieFile (full, &size);
			if (!movie.file)
				continue;
			movie.rate = S_MovieRate ();
			if (movie.rate < 1 || movie.rate > 192000)
				movie.rate = 44100;
			movie.decoder = CL_MediaOpen (movie.file, extensions[i], movie.rate, error, sizeof (error));
			if (!movie.decoder)
			{
				Com_Printf ("Movie %s: %s\n", LOG_CLIENT | LOG_WARNING, full, error);
				CL_MovieStop ();
				continue;
			}
			if (i == 3 && strlen (normalized) + 5 < sizeof (classic))
			{
				Com_sprintf (classic, sizeof (classic), "%s.cin", normalized);
				SCR_CinematicCropForFile (classic, size, &movie.crop_start, &movie.crop_pixels);
			}
			movie.clock = (unsigned)cls.realtime;
			movie_generation++;
			if (!CL_MovieRun (0))
			{
				CL_MovieStop ();
				continue;
			}
			return 1;
		}
	}
	return 0;
}

static void CL_MovieTrimAudio (void)
{
	movie_audio_t **link = &movie.audio, *block;
	while ((block = *link) != NULL)
	{
		if (block->end > movie.time - 0.25)
		{
			link = &block->next;
			continue;
		}
		*link = block->next;
		movie.audio_bytes -= block->samples * 4;
		movie.audio_blocks--;
		free (block);
	}
	movie.audio_tail = movie.audio;
	if (movie.audio_tail)
		while (movie.audio_tail->next)
			movie.audio_tail = movie.audio_tail->next;
}

static void CL_MovieFillAudio (double until, int *budget)
{
	cl_media_frame_t frame;
	movie_audio_t *block;
	char error[160];
	int ret;
	CL_MovieTrimAudio ();
	while (!movie.failed && !movie.audio_eof && movie.audio_end <= until && (*budget)-- > 0)
	{
		/* Reserve one maximum decoder block before advancing it. */
		if (movie.audio_blocks == MOVIE_AUDIO_BLOCKS || movie.audio_bytes > MOVIE_AUDIO_BYTES - 4096 * 4)
			return;
		ret = CL_MediaNextStream (movie.decoder, CL_MEDIA_AUDIO, &frame, error, sizeof (error));
		if (ret == CL_MEDIA_AGAIN)
			return;
		if (ret == CL_MEDIA_EOF)
		{
			movie.audio_eof = 1;
			return;
		}
		if (ret != CL_MEDIA_EVENT)
		{
			CL_MovieFail (error);
			return;
		}
		if (frame.type != CL_MEDIA_AUDIO || !frame.data || frame.samples < 1 || frame.samples > 4096 ||
			frame.rate != movie.rate || frame.size < (size_t)frame.samples * 4 ||
			!(frame.time >= 0 && frame.time < 1.0e12))
		{
			CL_MovieFail ("Invalid audio frame");
			return;
		}
		if (frame.time + (double)frame.samples / movie.rate > movie.audio_end)
			movie.audio_end = frame.time + (double)frame.samples / movie.rate;
		if (frame.time + (double)frame.samples / movie.rate <= movie.time - 0.25)
			continue;
		block = malloc (sizeof (*block) + (size_t)frame.samples * 4);
		if (!block)
		{
			CL_MovieFail ("Audio queue allocation failed");
			return;
		}
		block->next = NULL;
		block->time = frame.time;
		block->end = frame.time + (double)frame.samples / movie.rate;
		block->samples = frame.samples;
		memcpy (block->data, frame.data, frame.samples * 4);
		if (movie.audio_tail)
			movie.audio_tail->next = block;
		else
			movie.audio = block;
		movie.audio_tail = block;
		movie.audio_blocks++;
		movie.audio_bytes += frame.samples * 4;
	}
}

int CL_MovieReadAudio (unsigned char *pcm, int frames, int rate, double lead)
{
	movie_audio_t *block;
	double start, time, offset;
	int i, sample, budget = MOVIE_STEPS;
	if (!pcm || frames < 1 || frames > 8192 || rate < 1 || rate > 192000 ||
		!(lead >= 0 && lead <= 10) || CL_MoviePaused ())
		return 0;
	/* Sound updates precede SCR_RunCinematic in the retained client loop. */
	start = movie.time + (double)((unsigned)cls.realtime - movie.clock) / 1000 + lead;
	CL_MovieFillAudio (start + (double)frames / rate, &budget);
	if (movie.failed)
		return 0;
	memset (pcm, 0, frames * 4);
	/* Timestamp addressing preserves gaps and prevents late audio from dragging
	 * the video clock. Retained history allows pause to invalidate mix-ahead. */
	for (block = movie.audio; block; block = block->next)
	{
		if (block->end <= start || block->time >= start + (double)frames / rate)
			continue;
		offset = (block->time - start) * rate;
		i = offset <= 0 ? 0 : (offset >= frames ? frames : (int)offset);
		for (; i < frames; i++)
		{
			time = start + (double)i / rate;
			offset = (time - block->time) * movie.rate;
			/* x87 may keep division in extended precision while PTS is a
			 * stored double. Compare in sample units at exact block edges. */
			if (offset >= block->samples - 0.00001)
				break;
			if (offset < -0.00001)
				continue;
			sample = (int)(offset + 0.00001);
			if (sample >= 0 && sample < block->samples)
				memcpy (pcm + i * 4, block->data + sample * 4, 4);
		}
	}
	return frames;
}

static int CL_MovieNextPicture (void)
{
	cl_media_frame_t frame;
	movie_pic_t *pic = &movie.pending;
	char error[160];
	int ret;
	ret = CL_MediaNextStream (movie.decoder, CL_MEDIA_VIDEO, &frame, error, sizeof (error));
	if (ret == CL_MEDIA_AGAIN)
		return 0;
	if (ret == CL_MEDIA_EOF)
	{
		movie.video_eof = 1;
		return 0;
	}
	if (ret != CL_MEDIA_EVENT)
	{
		CL_MovieFail (error);
		return 0;
	}
	if (frame.type != CL_MEDIA_VIDEO || !frame.data || frame.width < 1 || frame.width > 8192 ||
		frame.height < 1 || frame.height > 8192 || frame.stride < frame.width * 4 ||
		frame.stride > 8192 * 4 || frame.size != (size_t)frame.stride * frame.height ||
		!(frame.time >= 0 && frame.time < 1.0e12) || !(frame.duration > 0 && frame.duration <= 3600))
	{
		CL_MovieFail ("Invalid video frame");
		return 0;
	}
	pic->data = malloc (frame.size);
	if (!pic->data)
	{
		CL_MovieFail ("Video frame allocation failed");
		return 0;
	}
	memcpy (pic->data, frame.data, frame.size);
	pic->size = frame.size;
	pic->width = frame.width;
	pic->height = frame.height;
	pic->stride = frame.stride;
	pic->time = frame.time;
	pic->duration = frame.duration;
	pic->number = movie.frames++;
	if (pic->time + pic->duration > movie.video_end)
		movie.video_end = pic->time + pic->duration;
	return 1;
}

int CL_MovieRun (int paused)
{
	unsigned now = (unsigned)cls.realtime;
	int budget = MOVIE_STEPS;
	if (!movie.decoder || movie.failed)
		return 0;
	paused = paused != 0;
	CL_MovieSetPaused (paused);
	if (!paused)
		movie.time += (double)(now - movie.clock) / 1000;
	movie.clock = now;
	if (paused)
		return 1;
	while (!movie.video_eof && !movie.failed && budget-- > 0)
	{
		if (!movie.pending.data && !CL_MovieNextPicture ())
			break;
		if (movie.pending.time > movie.time)
			break;
		free (movie.current.data);
		movie.current = movie.pending;
		memset (&movie.pending, 0, sizeof (movie.pending));
		movie.dirty = 1;
	}
	budget = MOVIE_STEPS;
	CL_MovieFillAudio (movie.time, &budget);
	return !movie.failed && !(movie.video_eof && movie.audio_eof && movie.time >= movie.video_end && movie.time >= movie.audio_end);
}

void CL_MovieDraw (void)
{
	movie_pic_t *pic = &movie.current;
	float sx, sy, scale;
	int crop, w, h, x, y;
	if (!movie.decoder || movie.failed || viddef.width < 1 || viddef.height < 1)
		return;
	re.DrawFill (0, 0, viddef.width, viddef.height, 0);
	if (!pic->data)
		return;
	if (!scr_color_presentation)
	{
		CL_MovieFail ("Color presentation unavailable after restart");
		return;
	}
	crop = pic->number >= movie.crop_start ? movie.crop_pixels : 0;
	if (crop >= pic->height)
		crop = 0;
	sx = (float)viddef.width / pic->width;
	sy = (float)viddef.height / (pic->height - crop);
	scale = sx < sy ? sx : sy;
	w = (int)(pic->width * scale + 0.5f);
	h = (int)(pic->height * scale + 0.5f);
	x = (viddef.width - w) / 2;
	y = (viddef.height - h) / 2;
	if (movie.dirty)
	{
		if (!scr_color_presentation->UpdateColorFrame (pic->width, pic->height, pic->stride,
				pic->data, (unsigned)pic->size))
		{
			CL_MovieFail ("Color frame upload failed");
			return;
		}
		movie.dirty = 0;
	}
	if (!scr_color_presentation->DrawColorFrame (x, y, w, h))
	{
		if (!scr_color_presentation->UpdateColorFrame (pic->width, pic->height, pic->stride,
				pic->data, (unsigned)pic->size) ||
			!scr_color_presentation->DrawColorFrame (x, y, w, h))
			CL_MovieFail ("Color frame presentation failed");
	}
}

#else
int CL_MovieStart (const char *name)
{
	(void)name;
	return 0;
}

void CL_MovieStop (void) {}

int CL_MovieActive (void)
{
	return 0;
}

int CL_MovieRun (int paused)
{
	(void)paused;
	return 0;
}

void CL_MovieDraw (void) {}

int CL_MovieReadAudio (unsigned char *pcm, int frames, int rate, double lead)
{
	(void)pcm;
	(void)frames;
	(void)rate;
	(void)lead;
	return 0;
}

int CL_MoviePaused (void)
{
	return 1;
}

unsigned CL_MovieGeneration (void)
{
	return 0;
}
#endif
