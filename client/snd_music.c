/*
 * File music policy adapted from Q2PRO src/client/sound/ogg.c,
 * revision 601a8df8433b0c50dbbe37c0716c3793fff140a7.
 * Copyright (C) 2022 Andrey Nazarov
 * GPL version 2 or (at your option) later. WITHOUT ANY WARRANTY. See COPYING.
 */
#ifndef _WIN32
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#endif
#include "client.h"
#include "snd_music.h"
#include "snd_stream.h"
#include <limits.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#define MUSIC_MAX_TRACKS 4096
#define MUSIC_MAX_DIRS 4096
#define MUSIC_MAX_DEPTH 16
#define MUSIC_MAX_ENTRIES 65536

typedef struct music_track_s
{
	char name[MAX_OSPATH];
	char path[MAX_OSPATH];
	int search;
} music_track_t;

typedef struct music_dir_s
{
	char path[MAX_OSPATH];
	struct music_dir_s *next;
} music_dir_t;

static const char *music_extensions[] = { ".flac", ".opus", ".ogg", ".mp3", ".wav" };
static music_track_t *music_tracks;
static int music_order[MUSIC_MAX_TRACKS];
static int music_count, music_index, music_dirs;
static int music_entries;
static unsigned music_random = 0x6d2b79f5U;
static int music_seeded;
static unsigned music_generation;
static cvar_t *ogg_enable, *ogg_volume, *ogg_shuffle, *ogg_menu_track, *ogg_remap_tracks;
static FILE *music_file;
static snd_stream_t *music_stream;
static char music_path[MAX_OSPATH], music_gamedir[MAX_OSPATH];
static char music_auto[MAX_OSPATH], music_observed[MAX_OSPATH];
static int music_rate, music_initialized, music_manual, music_paused;
static int music_connected, music_cinematic;
static int music_suspended;

static int S_MusicRegularFile (const char *path)
{
#ifdef _WIN32
	struct _stat64 info;
	return !_stat64 (path, &info) && (info.st_mode & _S_IFMT) == _S_IFREG;
#else
	struct stat info;
	return !stat (path, &info) && S_ISREG (info.st_mode);
#endif
}

static FILE *S_MusicOpenFile (const char *path)
{
#ifdef _WIN32
	if (!S_MusicRegularFile (path))
		return NULL;
	return fopen (path, "rb");
#else
	int fd;
	struct stat info;
	FILE *file;

	/* A replaced music entry must not block the sound thread on a FIFO. */
	fd = open (path, O_RDONLY | O_NONBLOCK | O_NOCTTY);
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
	return file;
#endif
}

static void S_MusicCopy (char *out, const char *in, size_t size)
{
	strncpy (out, in, size - 1);
	out[size - 1] = 0;
}

static const char *S_MusicExtension (const char *path)
{
	const char *ext;
	int i;

	ext = strrchr (path, '.');
	if (ext)
		for (i = 0; i < 5; i++)
			if (!Q_stricmp (ext, music_extensions[i]))
				return music_extensions[i];
	return NULL;
}

/* Track names are relative to music/, never paths passed in by a server. */
static int S_MusicNormalize (const char *name, char *out)
{
	int length, part, n;
	const char *ext;

	length = 0;
	if (!name || *name == '/' || *name == '\\')
		return 0;
	while (*name)
	{
		while (*name == '/' || *name == '\\')
			name++;
		part = length;
		while (*name && *name != '/' && *name != '\\')
		{
			if ((unsigned char)*name < 32 || *name == ':' || length >= MAX_OSPATH - 2)
				return 0;
			out[length++] = *name++;
		}
		n = length - part;
		if (n == 2 && out[part] == '.' && out[part + 1] == '.')
			return 0;
		if (n == 1 && out[part] == '.')
			length = part;
		else if (n)
			out[length++] = '/';
	}
	if (length && out[length - 1] == '/')
		length--;
	out[length] = 0;
	ext = S_MusicExtension (out);
	if (ext)
		out[length - strlen (ext)] = 0;
	return out[0] != 0;
}

static int S_MusicFind (const char *name)
{
	int i;
	for (i = 0; i < music_count; i++)
		if (!Q_stricmp (name, music_tracks[i].name))
			return i;
	return -1;
}

static void S_MusicAddFile (const char *path, size_t prefix, int search)
{
	char name[MAX_OSPATH];
	const char *ext;
	int i;

	if (strlen (path) >= MAX_OSPATH || strlen (path) <= prefix)
		return;
	ext = S_MusicExtension (path);
	if (!ext || !S_StreamFormatAvailable (ext) || !S_MusicNormalize (path + prefix, name) ||
		!S_MusicRegularFile (path))
		return;
	i = S_MusicFind (name);
	if (i >= 0)
	{
		/* Search-path priority first, deterministic extension choice second. */
		if (music_tracks[i].search != search || Q_stricmp (path, music_tracks[i].path) >= 0)
			return;
	}
	else
	{
		if (music_count == MUSIC_MAX_TRACKS)
			return;
		i = music_count++;
	}
	S_MusicCopy (music_tracks[i].path, path, MAX_OSPATH);
	S_MusicCopy (music_tracks[i].name, name, MAX_OSPATH);
	music_tracks[i].search = search;
}

static void S_MusicScan (const char *path, size_t prefix, int search, int depth)
{
	char pattern[MAX_OSPATH];
	char *found;
	music_dir_t *dirs, *dir;

	if (depth > MUSIC_MAX_DEPTH || music_dirs++ >= MUSIC_MAX_DIRS ||
		strlen (path) + 3 > sizeof (pattern))
		return;
	Com_sprintf (pattern, sizeof (pattern), "%s/*", path);
	found = Sys_FindFirst (pattern, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM);
	while (found && music_entries++ < MUSIC_MAX_ENTRIES)
	{
		S_MusicAddFile (found, prefix, search);
		found = Sys_FindNext (0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM);
	}
	Sys_FindClose ();
	if (depth == MUSIC_MAX_DEPTH)
		return;

	/* Sys_Find has global state. Finish enumeration before descending. */
	dirs = NULL;
	found = Sys_FindFirst (pattern, SFF_SUBDIR, SFF_HIDDEN | SFF_SYSTEM);
	while (found && music_entries++ < MUSIC_MAX_ENTRIES)
	{
		const char *base;
		base = strrchr (found, '/');
		base = base ? base + 1 : found;
		if (strcmp (base, ".") && strcmp (base, "..") && strlen (found) < MAX_OSPATH &&
			music_dirs < MUSIC_MAX_DIRS)
		{
			dir = malloc (sizeof (*dir));
			if (!dir)
				break;
			S_MusicCopy (dir->path, found, sizeof (dir->path));
			dir->next = dirs;
			dirs = dir;
			music_dirs++;
		}
		found = Sys_FindNext (SFF_SUBDIR, SFF_HIDDEN | SFF_SYSTEM);
	}
	Sys_FindClose ();
	while (dirs)
	{
		dir = dirs;
		dirs = dir->next;
		S_MusicScan (dir->path, prefix, search, depth + 1);
		free (dir);
	}
}

static void S_MusicLoadList (void)
{
	const char *path;
	char root[MAX_OSPATH], previous[MAX_OSPATH];
	int i, search, available;

	free (music_tracks);
	music_tracks = NULL;
	music_count = music_index = music_dirs = music_entries = 0;
	available = 0;
	for (i = 0; i < 5; i++)
		available |= S_StreamFormatAvailable (music_extensions[i]);
	if (!available)
		return;
	music_tracks = calloc (MUSIC_MAX_TRACKS, sizeof (*music_tracks));
	if (!music_tracks)
		return;
	path = NULL;
	previous[0] = 0;
	search = 0;
	while ((path = FS_NextPath (path)) != NULL)
	{
		/* FS_NextPath yields the active game directory twice. */
		if (!strcmp (path, previous))
			continue;
		S_MusicCopy (previous, path, sizeof (previous));
		if (strlen (path) + 7 < sizeof (root))
		{
			Com_sprintf (root, sizeof (root), "%s/music", path);
			S_MusicScan (root, strlen (root) + 1, search, 0);
		}
		search++;
	}
	/* GOG-style base-directory music is a nonrecursive fallback. */
	path = Cvar_VariableString ("basedir");
	if (*path && strlen (path) + 7 < sizeof (root))
	{
		Com_sprintf (root, sizeof (root), "%s/music", path);
		S_MusicScan (root, strlen (root) + 1, search, MUSIC_MAX_DEPTH);
	}
	for (i = 0; i < music_count; i++)
		music_order[i] = i;
}

static int S_MusicLookup (const char *name)
{
	const char *s;
	char normalized[MAX_OSPATH];
	int number, digit, i;
	static const int remap[] = { 9, 13, 14, 7, 16, 2, 15, 3, 4, 18 };

	number = 0;
	for (s = name; *s >= '0' && *s <= '9'; s++)
	{
		digit = *s - '0';
		if (number > (INT_MAX - digit) / 10)
			return -1;
		number = number * 10 + digit;
	}
	if (s != name && !*s)
	{
		if (number <= 0)
			return -1;
		if (ogg_remap_tracks->intvalue && number >= 2 && number <= 11)
		{
			if (!Q_stricmp (cl.gamedir, "rogue"))
				number += 10;
			else if (!Q_stricmp (cl.gamedir, "xatrix"))
				number = remap[number - 2];
		}
		Com_sprintf (normalized, sizeof (normalized), "track%02d", number);
		i = S_MusicFind (normalized);
		if (i >= 0)
			return i;
		Com_sprintf (normalized, sizeof (normalized), "%02d", number);
		return S_MusicFind (normalized);
	}
	if (!S_MusicNormalize (name, normalized))
		return -1;
	return S_MusicFind (normalized);
}

static void S_MusicClose (void)
{
	S_StreamClose (&music_stream);
	if (music_file)
		fclose (music_file);
	music_file = NULL;
	music_rate = 0;
}

void S_MusicStop (void)
{
	S_MusicClose ();
	music_path[0] = music_auto[0] = 0;
	music_manual = music_paused = 0;
	music_generation++;
}

static const char *S_MusicAutoName (void)
{
	return cls.state >= ca_connected ? cl.configstrings[CS_CDTRACK] : ogg_menu_track->string;
}

static void S_MusicShuffle (void)
{
	int i, j, tmp;
	for (i = music_count - 1; i > 0; i--)
	{
		/* Private state: playlist changes must not perturb engine/game random. */
		music_random ^= music_random << 13;
		music_random ^= music_random >> 17;
		music_random ^= music_random << 5;
		j = music_random % (i + 1);
		tmp = music_order[i];
		music_order[i] = music_order[j];
		music_order[j] = tmp;
	}
}

static void S_MusicSelectAuto (void)
{
	const char *name;
	int i;

	name = S_MusicAutoName ();
	music_path[0] = 0;
	if (!*name || !strcmp (name, "0") || !ogg_enable->intvalue || cl.cinematictime || music_suspended)
		return;
	S_MusicCopy (music_auto, name, sizeof (music_auto));
	if (ogg_shuffle->intvalue && music_count)
	{
		if (!music_index)
			S_MusicShuffle ();
		i = music_order[music_index];
		music_index = (music_index + 1) % music_count;
	}
	else
		i = S_MusicLookup (name);
	if (i >= 0)
		S_MusicCopy (music_path, music_tracks[i].path, sizeof (music_path));
}

void S_MusicPlay (void)
{
	const char *name;
	if (!music_initialized || cl.cinematictime || music_suspended || music_manual)
		return;
	name = S_MusicAutoName ();
	if (strlen (name) >= sizeof (music_auto))
	{
		S_MusicStop ();
		return;
	}
	if (*name && !Q_stricmp (music_auto, name))
		return;
	S_MusicStop ();
	S_MusicSelectAuto ();
}

void S_MusicBeginCinematic (void)
{
	S_MusicStop ();
	music_suspended = 1;
}

void S_MusicResume (void)
{
	music_suspended = 0;
	S_MusicPlay ();
}

float S_MusicVolume (void)
{
	float volume;
	volume = ogg_volume ? ogg_volume->value : 0;
	if (!(volume >= 0))
		return 0;
	return volume > 1 ? 1 : volume;
}

int S_MusicPaused (void)
{
	return !music_initialized || music_paused || music_suspended ||
		cls.disable_screen || cl.cinematictime;
}

unsigned S_MusicGeneration (void)
{
	return music_generation;
}

int S_MusicRead (unsigned char *pcm, int frames, int rate)
{
	char error[256];
	int total, count, transitions, failures;

	if (!pcm || frames <= 0 || frames > S_STREAM_MAX_READ_FRAMES || rate < 1 ||
		rate > 192000 || S_MusicPaused ())
		return 0;
	total = transitions = failures = 0;
	while (total < frames && *music_path)
	{
		if (music_stream && rate != music_rate)
			S_MusicClose ();
		if (!music_stream)
		{
			music_file = S_MusicOpenFile (music_path);
			if (music_file)
				music_stream = S_StreamOpen (music_file, S_MusicExtension (music_path),
					rate, error, sizeof (error));
			else
				S_MusicCopy (error, "could not open file", sizeof (error));
			if (!music_stream)
			{
				Com_Printf ("Music %s: %s\n", LOG_CLIENT | LOG_WARNING, music_path, error);
				if (!music_manual && ogg_shuffle->intvalue && ++failures < music_count)
				{
					S_MusicClose ();
					S_MusicSelectAuto ();
					continue;
				}
				S_MusicStop ();
				break;
			}
			music_rate = rate;
		}
		count = S_StreamRead (music_stream, pcm + total * 4, frames - total, error, sizeof (error));
		if (count < 0)
		{
			Com_Printf ("Music %s: %s\n", LOG_CLIENT | LOG_WARNING, music_path, error);
			if (!music_manual && ogg_shuffle->intvalue && ++failures < music_count)
			{
				S_MusicClose ();
				S_MusicSelectAuto ();
				continue;
			}
			S_MusicStop ();
			break;
		}
		if (count)
		{
			total += count;
			continue;
		}
		S_MusicClose ();
		music_manual = 0;
		S_MusicSelectAuto ();
		/* Empty/tiny tracks must never spin indefinitely in an audio frame. */
		if (++transitions == 2)
		{
			if (!total)
				S_MusicStop ();
			break;
		}
	}
	return total;
}

static void S_MusicCommand_f (void)
{
	const char *cmd;
	int i;
	cmd = Cmd_Argv (1);
	if (!strcmp (cmd, "stop"))
		S_MusicStop ();
	else if (!strcmp (cmd, "info"))
		Com_Printf ("Music: %s%s (%d tracks)\n", LOG_CLIENT,
			*music_path ? music_path : "stopped", music_paused ? " [paused]" : "", music_count);
	else if (!strcmp (cmd, "pause") || !strcmp (cmd, "toggle"))
	{
		if (*music_path)
			music_paused = Cmd_Argc () > 2 ? atoi (Cmd_Argv (2)) != 0 : !music_paused;
	}
	else if (!strcmp (cmd, "play") || !strcmp (cmd, "next"))
	{
		if (!music_initialized || cl.cinematictime || music_suspended)
		{
			Com_Printf ("Music needs an active sound system outside cinematics.\n", LOG_CLIENT);
			return;
		}
		if (!strcmp (cmd, "next"))
		{
			S_MusicStop ();
			S_MusicPlay ();
			return;
		}
		i = Cmd_Argc () >= 3 ? S_MusicLookup (Cmd_Argv (2)) : -1;
		if (i < 0)
		{
			Com_Printf ("No such music track: %s\n", LOG_CLIENT, Cmd_Argv (2));
			return;
		}
		if (!strcmp (Cmd_Argv (3), "soft"))
			S_MusicClose ();
		else
			S_MusicStop ();
		S_MusicCopy (music_path, music_tracks[i].path, sizeof (music_path));
		music_manual = 1;
	}
	else
		Com_Printf ("Usage: ogg <info|play <track> [soft]|stop|next|pause [0|1]>\n", LOG_CLIENT);
}

void S_MusicGameDirChanged (void)
{
	if (!music_initialized)
		return;
	S_MusicStop ();
	S_MusicCopy (music_gamedir, FS_Gamedir (), sizeof (music_gamedir));
	S_MusicLoadList ();
	S_MusicPlay ();
}

void S_MusicUpdate (void)
{
	const char *name;
	int connected, cinematic, select;
	if (!music_initialized)
		return;
	if (strcmp (music_gamedir, FS_Gamedir ()))
		S_MusicGameDirChanged ();
	connected = cls.state >= ca_connected;
	cinematic = cl.cinematictime != 0;
	name = S_MusicAutoName ();
	/* Observe even invalid overlong cvars without retrying them every frame. */
	select = strncmp (music_observed, name, sizeof (music_observed) - 1) != 0;
	if (cinematic && !music_cinematic)
		S_MusicBeginCinematic ();
	if (connected != music_connected)
	{
		S_MusicStop ();
		if (!connected)
			music_suspended = 0;
		select = 1;
	}
	S_MusicCopy (music_observed, name, sizeof (music_observed));
	music_connected = connected;
	music_cinematic = cinematic;
	if (ogg_enable->modified)
	{
		ogg_enable->modified = false;
		if (!music_manual && !cinematic)
		{
			music_paused = !ogg_enable->intvalue;
			if (ogg_enable->intvalue && !*music_path)
				select = 1;
		}
	}
	if (ogg_remap_tracks->modified || ogg_menu_track->modified)
	{
		if ((connected && ogg_remap_tracks->modified) || (!connected && ogg_menu_track->modified))
		{
			music_auto[0] = 0;
			select = 1;
		}
		ogg_remap_tracks->modified = ogg_menu_track->modified = false;
	}
	if (select)
		S_MusicPlay ();
	if (ogg_volume->modified)
	{
		if (!(ogg_volume->value >= 0 && ogg_volume->value <= 1))
			Cvar_SetValue ("ogg_volume", S_MusicVolume ());
		ogg_volume->modified = false;
	}
}

void S_MusicInit (void)
{
	if (!music_seeded)
	{
		music_random ^= (unsigned)Sys_Milliseconds ();
		if (!music_random)
			music_random = 1;
		music_seeded = 1;
	}
	ogg_enable = Cvar_Get ("ogg_enable", "1", 0);
	ogg_volume = Cvar_Get ("ogg_volume", "1", 0);
	ogg_shuffle = Cvar_Get ("ogg_shuffle", "0", 0);
	ogg_menu_track = Cvar_Get ("ogg_menu_track", "0", 0);
	ogg_remap_tracks = Cvar_Get ("ogg_remap_tracks", "1", 0);
	Cmd_AddCommand ("ogg", S_MusicCommand_f);
	music_initialized = 1;
	music_connected = cls.state >= ca_connected;
	music_cinematic = cl.cinematictime != 0;
	S_MusicCopy (music_observed, S_MusicAutoName (), sizeof (music_observed));
	S_MusicGameDirChanged ();
}

void S_MusicShutdown (void)
{
	S_MusicStop ();
	free (music_tracks);
	music_tracks = NULL;
	music_count = music_index = music_initialized = 0;
	Cmd_RemoveCommand ("ogg");
}
