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

#if defined(__linux__) && !defined(_LARGEFILE64_SOURCE)
#define _LARGEFILE64_SOURCE
#endif

#include "qcommon.h"
#ifndef DEDICATED_ONLY
extern void Con_SaveHistory (void);
extern void Con_LoadHistory (void);
#endif
#include "redblack.h"
#include "unzip.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif
/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

//
// in memory
//

typedef struct
{
	union filehandle_type
	{
		FILE			*handle;
		unzFile zhandle;
	} h;
	uint32			length;
	uint32			refcount;
} fshandle_t;

typedef struct
{
	char name[MAX_QPATH];
	uint64 filepos;
	uint32			filelen;
} packfile_t;

typedef struct
{
	uint64 offset;
} zpackfile_t;

typedef enum
{
	PAK_QUAKE,
	PAK_ZIP,
} packtype_t;

typedef struct pack_s
{
	char			filename[MAX_OSPATH];
	union packhandle_type
	{
		FILE			*handle;
		unzFile zhandle;
	} h;
	int				numfiles;
	packfile_t *files;
	struct rbtree	*rb;
	packtype_t		type;
} pack_t;

char	fs_gamedir[MAX_OSPATH];
cvar_t	*fs_basedir;
cvar_t	*fs_gamedirvar;
cvar_t	*fs_cache;
cvar_t	*fs_noextern;

typedef struct filelink_s
{
	struct filelink_s	*next;
	char	*from;
	int		fromlength;
	char	*to;
} filelink_t;

static filelink_t	*fs_links;

typedef struct searchpath_s
{
	char	filename[MAX_OSPATH];
	pack_t	*pack;		// only one of filename / pack will be used
	struct searchpath_s *next;
} searchpath_t;

static searchpath_t	*fs_searchpaths;
static searchpath_t	*fs_base_searchpaths;	// without gamedirs

static const char *current_filename;

/*

All of Quake's data access is through a hierchal file system, but the contents of
the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all game
directories.  The sys_* files pass this to host_init in quakeparms_t->basedir.  This
can be overridden with the "-basedir" command line parm to allow code debugging in a
different directory.  The base directory is only used during filesystem initialization.

The "game directory" is the first tree on the search path and directory that all
generated files (savegames, screenshots, demos, config files) will be saved to. 
This can be overridden with the "-game" command line parameter.  The game directory
can never be changed while quake is executing.  This is a precacution against having a
malicious server instruct clients to write files over areas they shouldn't.

*/


/*
================
FS_filelength
================
*/
static int FS_filelength (FILE *f)
{
//	int		pos;
	int		end;

	//pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	rewind (f);
	//fseek (f, pos, SEEK_SET);

	return end;
}

/*
============
FS_CreatePath

Creates any directories needed to store the given filename
============
*/
void	FS_CreatePath (char *path)
{
	char	*ofs;

	for (ofs = path+1 ; *ofs ; ofs++)
	{
		if (*ofs == '/')
		{	// create the directory
			*ofs = 0;
			Sys_Mkdir (path);
			*ofs = '/';
		}
	}
}


/*
==============
FS_FCloseFile

For some reason, other dll's can't just call fclose()
on files returned by FS_FOpenFile...
==============
*/
void EXPORT FS_FCloseFile (FILE *f)
{
	fclose (f);
}


// RAFAEL
/*
	Developer_searchpath
*/
int	Developer_searchpath (void)
{
	searchpath_t	*search;
	

	for (search = fs_searchpaths ; search ; search = search->next)
	{
		if (strstr (search->filename, "xatrix"))
			return 1;

		if (strstr (search->filename, "rogue"))
			return 2;
/*
		start = strchr (search->filename, ch);

		if (start == NULL)
			continue;

		if (strcmp (start ,"xatrix") == 0)
			return (1);
*/
	}
	return (0);

}

#define BTREE_SEARCH 1
//#define HASH_CACHE 1
//#define MAGIC_BTREE 1

#if HASH_CACHE || MAGIC_BTREE
uint32 hashify (char *S)
{
  uint32 hash_PeRlHaSh = 0;
  char c;
  while (*S) {
	  c = fast_tolower(*S++);
	  hash_PeRlHaSh = hash_PeRlHaSh * 33 + (c);
  }
  return hash_PeRlHaSh + (hash_PeRlHaSh >> 5);
}
#endif

typedef struct fscache_s fscache_t;

struct fscache_s
{
	char		filename[MAX_QPATH];
	char		filepath[MAX_OSPATH];
	uint32		filelen;
	uint64 fileseek;
	pack_t		*pak;
#ifdef HASH_CACHE
	fscache_t	*next;
	uint32		hash;
#endif
};

static struct rbtree *rb;

#ifdef MAGIC_BTREE
static int _compare(const void *pa, const void *pb)
{
	if ((int *)pa > (int *)pb)
		return 1;
	else if ((int *)pa < (int *)pb)
		return -1;
	return 0;
}
#endif

void FS_InitCache (void)
{
#ifdef MAGIC_BTREE
	rb = rbinit (_compare, 1);
#else
#ifdef LINUX
	rb = rbinit ((int (EXPORT *)(const void *, const void *))strcmp, 0);
#else
	rb = rbinit ((int (EXPORT *)(const void *, const void *))Q_stricmp, 0);
#endif
#endif

	if (!rb)
		Com_Error (ERR_FATAL, "FS_InitCache: rbinit failed"); 
}

#ifdef HASH_CACHE
static fscache_t fscache;
#endif

static void RB_Purge (const struct rbtree *r)
{
	RBLIST *rblist;
	const void *val;
	void *data;
	void *ptr;

	if ((rblist=rbopenlist(rb)))
	{
		while((val=rbreadlist(rblist)))
		{
			data = rbfind (val, rb);
			ptr = *(void **)data;
			rbdelete (val, rb);

			if (ptr)
				Z_Free (ptr);
		}
	}

	rbcloselist(rblist);
	rbdestroy (rb);

	FS_InitCache();
}

void FS_FlushCache (void)
{
#ifdef HASH_CACHE
	fscache_t *last = NULL, *temp;

	temp = &fscache;
	temp = temp->next;

	if (temp) {
		while (temp->next) {
			last = temp->next;
			Z_Free (temp);
			temp = last;
		}

		fscache.next = NULL;

		Z_Free (temp);
	}

	memset (&fscache, 0, sizeof(fscache));
#endif
	RB_Purge (rb);
}

static void FS_Stats_f (void)
{
#if BTREE_SEARCH || MAGIC_BTREE
	int			i;
	int			j;
	int			k;
	const void	*val;
#endif
#ifdef HASH_CACHE
	fscache_t *temp;
#endif

#if BTREE_SEARCH || MAGIC_BTREE
	i = 0;
	j = 1;
	k = 1;

	for(val=rblookup(RB_LUFIRST, NULL, rb); val!=NULL; val=rblookup(RB_LUNEXT, val, rb))
		i++;

	while (j < i)
	{
		k++;
		j <<= 1;
	}
	Com_Printf ("%d entries in binary search tree cache (est. height %d).\n", LOG_GENERAL, i, k);
#endif

#ifdef HASH_CACHE
	temp = &fscache;
	temp = temp->next;

	i = 0;

	if (temp)
	{
		while (temp->next)
		{
			i++;
			temp = temp->next;
		}
	}

	Com_Printf ("%d entries in linked list hash cache.\n", LOG_GENERAL, i);
#endif
}

#if BTREE_SEARCH
static void FS_AddToCache (const char *path, uint32 filelen, uint64 fileseek, const char *filename, pack_t *pak)
{
	void		**newitem;
	fscache_t	*cache;

	if (!q2_initialized)
		return;

	cache = Z_TagMalloc (sizeof(fscache_t), TAGMALLOC_FSCACHE);
	cache->filelen = filelen;
	cache->fileseek = fileseek;
	cache->pak = pak;

	if (path)
		strncpy (cache->filepath, path, sizeof(cache->filepath)-1);
	else
		cache->filepath[0] = 0;

	strncpy (cache->filename, filename, sizeof(cache->filename)-1);

	newitem = rbsearch (cache->filename, rb);
	*newitem = cache;
}
#endif

#if MAGIC_BTREE

typedef struct magic_s
{
	struct magic_s	*next;
	fscache_t		*entry;
} magic_t;

static void FS_AddToCache (char *path, uint32 filelen, uint64 fileseek, char *filename, uint32 hash)
{
	void		**newitem;
	fscache_t	*cache;
	magic_t		*magic;

	cache = Z_TagMalloc (sizeof(fscache_t), TAGMALLOC_FSCACHE);
	cache->filelen = filelen;
	cache->fileseek = fileseek;

	if (path)
		strncpy (cache->filepath, path, sizeof(cache->filepath)-1);
	else
		cache->filepath[0] = 0;

	strncpy (cache->filename, filename, sizeof(cache->filename)-1);

	newitem = rbfind ((void *)hash, rb);
	if (newitem)
	{
		magic = *(magic_t **)newitem;
		while (magic->next)
			magic = magic->next;
		magic->next = Z_TagMalloc (sizeof(magic_t), TAGMALLOC_FSCACHE);
		magic = magic->next;
		magic->entry = cache;
		magic->next = NULL;
	}
	else
	{
		newitem = rbsearch ((void *)hash, rb);
		magic = Z_TagMalloc (sizeof(magic_t), TAGMALLOC_FSCACHE);
		magic->entry = cache;
		magic->next = NULL;
		*newitem = magic;
	}
}
#endif

#if HASH_CACHE
static void FS_AddToCache (uint32 hash, char *path, uint32 filelen, uint64 fileseek, fscache_t *cache, char *filename)
{
	cache->next = Z_TagMalloc (sizeof(fscache_t), TAGMALLOC_FSCACHE);
	cache = cache->next;
	cache->filelen = filelen;
	cache->hash = hash;
	cache->next = NULL;
	cache->fileseek = fileseek;

	if (path)
		strncpy (cache->filepath, path, sizeof(cache->filepath)-1);
	else
		cache->filepath[0] = 0;

	strncpy (cache->filename, filename, sizeof(cache->filename)-1);
}
#endif

void FS_WhereIs_f (void)
{
	char			*filename;
	searchpath_t	*search;
	pack_t			*pak;
	filelink_t		*link;
	char			netpath[MAX_OSPATH];
	char			lowered[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("Purpose: Find where a file is being loaded from on the filesystem.\n"
					"Syntax : whereis <path>\n"
					"Example: whereis maps/q2dm1.bsp\n", LOG_GENERAL);
		return;
	}

	filename = Cmd_Argv(1);

	// check for links firstal
	if (!fs_noextern->intvalue)
	{
		for (link = fs_links ; link ; link=link->next)
		{
			if (!strncmp (filename, link->from, link->fromlength))
			{
				int	len;
				Com_sprintf (netpath, sizeof(netpath), "%s%s",link->to, filename+link->fromlength);
				len = Sys_FileLength (netpath);
				if (len != -1)
				{
					Com_Printf ("%s is found on disk as %s (using linkpath), %d bytes.\n", LOG_GENERAL, Cmd_Argv(1), netpath, len);
				}
				else
				{
					Com_Printf ("%s is not found.\n", LOG_GENERAL, Cmd_Argv(1));
				}
				return;
			}
		}
	}

	Q_strncpy (lowered, filename, sizeof(lowered)-1);
	fast_strlwr (lowered);

	for (search = fs_searchpaths ; search ; search = search->next)
	{
		// is the element a pak file?
		if (search->pack)
		{
			packfile_t	*entry;

			//r1: optimized btree search
			pak = search->pack;

			entry = rbfind (lowered, pak->rb);

			if (entry)
			{
				entry = *(packfile_t **)entry;
				Com_Printf ("%s is found in pakfile %s as %s, %d bytes.\n", LOG_GENERAL, Cmd_Argv(1), pak->filename, entry->name, entry->filelen);
				return;
			}
		}
		else if (!fs_noextern->intvalue)
		{
			int filelen;
			// check a file in the directory tree
			
			Com_sprintf (netpath, sizeof(netpath), "%s/%s",search->filename, filename);

			filelen = Sys_FileLength (netpath);
			if (filelen == -1)
				continue;

			Com_Printf ("%s is found on disk as %s, %d bytes.\n", LOG_GENERAL, Cmd_Argv(1), netpath, filelen);
			return;
		}
	}

	Com_Printf ("%s is not found.\n", LOG_GENERAL, Cmd_Argv(1));
}

/*
===========
FS_FOpenFile

Finds the file in the search path.
returns filesize and an open FILE *
Used for streaming data out of either a pak file or
a seperate file.
===========
*/

/* Wide offsets remain private: public asset lengths and FILE streams retain
 * their existing API. Older CRTs reject unsupported positions explicitly. */
static int FS_SeekPackFile (FILE *stream, uint64 offset, int origin)
{
	if (offset > ((uint64)-1 >> 1))
		return -1;
#if defined(_WIN32) && (!defined(_MSC_VER) || _MSC_VER >= 1400)
	return _fseeki64 (stream, (int64)offset, origin);
#elif defined(_WIN32)
	if (offset > LONG_MAX)
		return -1;
	return fseek (stream, (long)offset, origin);
#elif defined(__linux__)
	return fseeko64 (stream, (off64_t)offset, origin);
#else
	if ((uint64)(off_t)offset != offset || (off_t)offset < 0)
		return -1;
	return fseeko (stream, (off_t)offset, origin);
#endif
}

static int64 FS_PackFileSize (FILE *stream)
{
	int64 size;
	if (FS_SeekPackFile (stream, 0, SEEK_END))
		return -1;
#if defined(_WIN32) && (!defined(_MSC_VER) || _MSC_VER >= 1400)
	size = _ftelli64 (stream);
#elif defined(_WIN32)
	size = ftell (stream);
#elif defined(__linux__)
	size = ftello64 (stream);
#else
	size = ftello (stream);
#endif
	return size < 0 || FS_SeekPackFile (stream, 0, SEEK_SET) ? -1 : size;
}

#ifndef NO_ZLIB
static FILE *FS_TempFile (void)
{
#ifdef _WIN32
	char directory[MAX_PATH], filename[MAX_PATH];
	DWORD length;
	HANDLE handle;
	int descriptor;
	FILE *stream;

	/* The Microsoft CRT tmpfile uses the drive root, which ordinary accounts
	 * may not write. Keep ZIP staging in the user's system temp directory. */
	length = GetTempPathA (sizeof (directory), directory);
	if (!length || length >= sizeof (directory) ||
		!GetTempFileNameA (directory, "r1q", 0, filename))
		return NULL;
	handle = CreateFileA (filename, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		DeleteFileA (filename);
		return NULL;
	}
	descriptor = _open_osfhandle ((INT_PTR)handle, _O_BINARY | _O_RDWR);
	if (descriptor == -1)
	{
		CloseHandle (handle);
		return NULL;
	}
	stream = _fdopen (descriptor, "w+b");
	if (!stream)
		_close (descriptor);
	return stream;
#else
	return tmpfile ();
#endif
}
#endif

/* The public file API returns seekable FILE streams. ZIP entries are validated
 * into temporary streams so cinematics and independent HANDLE_DUPE readers keep
 * that interface without retaining a whole decompressed asset in memory. */
static qboolean FS_OpenPackFile (pack_t *pak, uint64 offset, uint32 length,
	FILE **file, handlestyle_t openHandle, qboolean *closeHandle)
{
	FILE *stream;

	if (openHandle == HANDLE_NONE)
		return true;
#ifndef NO_ZLIB
	if (pak->type == PAK_ZIP)
	{
		byte data[16384];
		uint32 total = 0;
		int count, result;
		unz_file_info64 info;
		qboolean valid = true;

		if (unzSetOffset64 (pak->h.zhandle, offset) != UNZ_OK ||
			unzGetCurrentFileInfo64 (pak->h.zhandle, &info, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK ||
			info.uncompressed_size != length ||
			unzOpenCurrentFile (pak->h.zhandle) != UNZ_OK)
			goto zip_failed;
		stream = FS_TempFile ();
		if (!stream)
		{
			unzCloseCurrentFile (pak->h.zhandle);
			goto zip_failed;
		}
		while ((count = unzReadCurrentFile (pak->h.zhandle, data, sizeof (data))) > 0)
		{
			if ((uint32)count > length - total ||
				fwrite (data, 1, count, stream) != (size_t)count)
			{
				valid = false;
				break;
			}
			total += count;
		}
		result = unzCloseCurrentFile (pak->h.zhandle);
		if (!valid || count < 0 || total != length || result != UNZ_OK ||
			fflush (stream) || fseek (stream, 0, SEEK_SET))
		{
			fclose (stream);
			goto zip_failed;
		}
		*file = stream;
		*closeHandle = true;
		return true;
	zip_failed:
		Com_Printf ("WARNING: Could not read validated ZIP entry in %s\n",
			LOG_WARNING | LOG_GENERAL, pak->filename);
		return false;
	}
#endif
	stream = openHandle == HANDLE_DUPE ? fopen (pak->filename, "rb") : pak->h.handle;
	if (!stream || FS_SeekPackFile (stream, offset, SEEK_SET))
	{
		if (stream && openHandle == HANDLE_DUPE)
			fclose (stream);
		Com_Printf ("WARNING: Could not open pack entry in %s\n",
			LOG_WARNING | LOG_GENERAL, pak->filename);
		return false;
	}
	*file = stream;
	*closeHandle = openHandle == HANDLE_DUPE;
	return true;
}

int EXPORT FS_FOpenFile (const char *filename, FILE **file, handlestyle_t openHandle, qboolean *closeHandle)
{
	fscache_t		*cache;
	searchpath_t	*search;
	pack_t			*pak;
	filelink_t		*link;
	char			netpath[MAX_OSPATH];
	char			lowered[MAX_QPATH];

	*file = NULL;
	*closeHandle = false;
	if (!filename || !filename[0] || strlen (filename) >= sizeof (lowered))
		return -1;

	// check for links firstal
	if (!fs_noextern->intvalue)
	{
		for (link = fs_links ; link ; link=link->next)
		{
			if (!strncmp (filename, link->from, link->fromlength))
			{
				Com_sprintf (netpath, sizeof(netpath), "%s%s",link->to, filename+link->fromlength);
				if (openHandle != HANDLE_NONE)
				{
					*file = fopen (netpath, "rb");
					if (*file)
					{	
						Com_DPrintf ("link file: %s\n",netpath);
						*closeHandle = true;
						return FS_filelength (*file);
					}
					return -1;
				}
				else
				{
					return Sys_FileLength (netpath);
				}
			}
		}
	}

#ifdef BTREE_SEARCH
	cache = rbfind (filename, rb);
	if (cache)
	{
		cache = *(fscache_t **)cache;
		if (cache->filepath[0] == 0)
		{
			*file = NULL;
			return -1;
		}
#ifdef _DEBUG
		Com_DPrintf ("File '%s' found in cache: %s\n", filename, cache->filepath);
#endif
		if (openHandle != HANDLE_NONE)
		{
			if (cache->pak)
				return FS_OpenPackFile (cache->pak, cache->fileseek, cache->filelen,
						   file, openHandle, closeHandle) ?
					(int)cache->filelen :
					-1;
			else
			{
				*file = fopen (cache->filepath, "rb");
				if (!*file)
				{
					Com_Printf ("WARNING: Cached file '%s' failed to open! Did you delete it?\n", LOG_WARNING|LOG_GENERAL, cache->filepath);
					rbdelete (filename, rb);
					return -1;
				}
					//Com_Error (ERR_FATAL, "Couldn't open %s (cached)", cache->filepath);	

				*closeHandle = true;
			}
			if (cache->fileseek && FS_SeekPackFile (*file, cache->fileseek, SEEK_SET))
				Com_Error (ERR_FATAL, "Couldn't seek in %s (cached)", cache->filepath);
		}
		return cache->filelen;
	}

#elif HASH_CACHE
	hash = hashify (filename);

	cache = &fscache;

	while (cache->next)
	{ 
		cache = cache->next;
		if (cache->hash == hash && !Q_stricmp (cache->filename, filename))
		{
			Com_Printf (" (cached) ", LOG_GENERAL);
			if (cache->filepath[0] == 0)
			{
				*file = NULL;
				return -1;
			}
			*file = fopen (cache->filepath, "rb");
			if (!*file)
				Com_Error (ERR_FATAL, "Couldn't open %s", cache->filepath);
			FS_SeekPackFile (*file, cache->fileseek, SEEK_SET);
			return cache->filelen;
		}
	}
#elif MAGIC_BTREE
	{
		magic_t *magic;

		hash = hashify (filename);

		magic = rbfind ((void *)hash, rb);
		if (magic)
		{
			magic = *(magic_t **)magic;

			do
			{
				cache = magic->entry;
				if (!Q_stricmp (cache->filename, filename))
				{
					if (cache->filepath[0] == 0)
					{
						*file = NULL;
						return -1;
					}
					*file = fopen (cache->filepath, "rb");
					if (!*file)
						Com_Error (ERR_FATAL, "Couldn't open %s", cache->filepath);
					FS_SeekPackFile (*file, cache->fileseek, SEEK_SET);
					return cache->filelen;
				}

				magic = magic->next;
			} while (magic);
		}
	}
#endif

#ifdef _DEBUG
	Com_DPrintf ("File '%s' not found in cache, searching fs_searchpaths\n", filename);
#endif

	Q_strncpy (lowered, filename, sizeof(lowered)-1);
	fast_strlwr (lowered);

	for (search = fs_searchpaths ; search ; search = search->next)
	{
		// is the element a pak file?
		if (search->pack)
		{
//			char		*lower;
			packfile_t	*entry;

			//r1: optimized btree search
			pak = search->pack;

			entry = rbfind (lowered, pak->rb);
			if (entry)
			{
				entry = *(packfile_t **)entry;
				if (!FS_OpenPackFile (pak, entry->filepos, entry->filelen,
						file, openHandle, closeHandle))
					return -1;
				if (fs_cache->intvalue & 1)
					FS_AddToCache (pak->filename, entry->filelen, entry->filepos, filename, pak);
				return entry->filelen;
			}
		}
		else if (!fs_noextern->intvalue)
		{
			struct stat	statInfo;
			int filelen;
			// check a file in the directory tree
			
			Com_sprintf (netpath, sizeof(netpath), "%s/%s",search->filename, filename);

			if (openHandle == HANDLE_NONE)
			{
				filelen = Sys_FileLength (netpath);
				if (filelen == -1)
					continue;
				
				if (fs_cache->intvalue & 4)
					FS_AddToCache (netpath, filelen, 0, filename, NULL);

				return filelen;
			}

			//fix for moronic implementations that allow fopen (FILE OPEN) to open a directory
			//(this means you linux and krew)
			if (stat (netpath, &statInfo))
				continue;

			if (statInfo.st_mode & S_IFDIR)
			{
				Com_Printf ("WARNING: Tried to open a directory as a file: %s\n", LOG_WARNING, netpath);
				continue;
			}

			*file = fopen (netpath, "rb");

			if (!*file)
				continue;

			*closeHandle = true;
			
			Com_DPrintf ("FindFile: %s\n",netpath);

			filelen = FS_filelength (*file);
			if (fs_cache->intvalue & 4)
			{
#if BTREE_SEARCH
				FS_AddToCache (netpath, filelen, 0, filename, NULL);
#elif HASH_CACHE
				FS_AddToCache (hash, netpath, filelen, 0, cache, filename);
#elif MAGIC_BTREE
				FS_AddToCache (netpath, filelen, 0, filename, hash);
#endif
			}
			return filelen;
		}
		
	}
	
	Com_DPrintf ("FindFile: can't find %s\n", filename);

	if (fs_cache->intvalue & 2)
	{
#if BTREE_SEARCH
		FS_AddToCache (NULL, 0, 0, filename, NULL);
#elif HASH_CACHE
		FS_AddToCache (hash, NULL, 0, 0, cache, filename);
#elif MAGIC_BTREE
		FS_AddToCache (NULL, 0, 0, filename, hash);
#endif
	}
	
	*file = NULL;
	return -1;
}

/*
=================
FS_ReadFile

Properly handles partial reads
=================
*/
#ifdef CD_AUDIO
#include "../client/cdaudio.h"
#endif

#define	MAX_READ	0x40000		// read in blocks of 64k
								// r1: bumped to 256k.
void EXPORT FS_Read (void *buffer, int len, FILE *f)
{
	int		block, remaining;
	size_t	read;
	byte	*buf;
#ifdef CD_AUDIO
	int		tries = 0;
#endif

	Q_assert (len != 0);

	buf = (byte *)buffer;

	// read in chunks for progress bar
	remaining = len;

	while (remaining)
	{
		block = remaining;
		if (block > MAX_READ)
			block = MAX_READ;

		read = fread (buf, block, 1, f);
		if (read == 0)
		{
			// we might have been trying to read from a CD
#ifdef CD_AUDIO
			if (!tries)
			{
				tries = 1;
				CDAudio_Stop();
			}
			else
#endif
			{
				if (ferror (f))
				{
					Com_Error (ERR_FATAL, "FS_Read(%d): Read error on '%s'. Did you forget to empty the filesystem cache after modifying a file?", len, current_filename);
				}
				else
				{
					Com_Printf ("WARNING: Incomplete read of %d bytes from '%s'. Did you forget to empty the filesystem cache after modifying a file?\n", LOG_WARNING, len, current_filename);
					return;
				}
			}
		}

		// do some progress bar thing here...

		remaining -= block;
		buf += block;
	}
}

/*
============
FS_LoadFile

Filename are reletive to the quake search path
a null buffer will just return the file length without loading
============
*/
int EXPORT FS_LoadFile (const char *path, void /*@out@*/ /*@null@*/**buffer)
{
	FILE		*h;
	byte		*buf;
	int			len;
	qboolean	closeHandle;
	// look for it in the filesystem or pack files
	//START_PERFORMANCE_TIMER;
	//Com_Printf ("%s... ", path);
	len = FS_FOpenFile (path, &h, buffer ? HANDLE_OPEN : HANDLE_NONE, &closeHandle);
	//STOP_PERFORMANCE_TIMER;

	//Com_Printf ("TOTAL SO FAR: %.5f\n", totalTime);

	if (len == -1)
	{
		if (buffer)
			*buffer = NULL;
		return -1;
	}
	
	if (!buffer)
		return len;

	if (!len)
	{
		if (closeHandle)
			fclose (h);
		Com_Printf ("WARNING: 0 byte file: %s\n", LOG_GENERAL|LOG_WARNING, path);
		*buffer = CopyString ("", TAGMALLOC_FSLOADFILE);
		return 0;
	}

	buf = Z_TagMalloc(len, TAGMALLOC_FSLOADFILE);
	*buffer = buf;
	current_filename = path;
	FS_Read (buf, len, h);
	current_filename = "unknown";

	if (closeHandle)
		fclose (h);

	return len;
}


/*
=============
FS_FreeFile
=============
*/
void EXPORT FS_FreeFile (void *buffer)
{
	Z_Free (buffer);
}

/*
=================
FS_LoadPackFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
/* Classic PAK records stay 64 bytes even though the private index accepts the
 * full existing virtual path limit used by ZIP entries. */
typedef struct
{
	char name[56];
	uint32 filepos, filelen;
} packdiskfile_t;

#define MAX_PACK_ENTRIES (1U << 20)

static qboolean FS_PackName (char *output, const char *name, size_t length)
{
	size_t start = 0, used = 0, end, part;

	if (!length || length >= MAX_QPATH || name[0] == '/' || name[0] == '\\')
		return false;
	while (start < length)
	{
		for (end = start; end < length && name[end] != '/' && name[end] != '\\'; end++)
			if ((unsigned char)name[end] < 32 || name[end] == ':' || name[end] == 0)
				return false;
		part = end - start;
		if (part == 2 && name[start] == '.' && name[start + 1] == '.')
			return false;
		if (part && !(part == 1 && name[start] == '.'))
		{
			if (used)
				output[used++] = '/';
			memcpy (output + used, name + start, part);
			used += part;
		}
		start = end + 1;
	}
	output[used] = 0;
	fast_strlwr (output);
	return used != 0;
}

static void FS_FreePack (pack_t *pack)
{
	if (!pack)
		return;
#ifndef NO_ZLIB
	if (pack->type == PAK_ZIP)
	{
		if (pack->h.zhandle)
			unzClose (pack->h.zhandle);
	}
	else
#endif
		if (pack->h.handle)
		fclose (pack->h.handle);
	if (pack->rb)
		rbdestroy (pack->rb);
	if (pack->files)
		Z_Free (pack->files);
	Z_Free (pack);
}

static pack_t /*@null@*/ *FS_LoadPackFile (const char *packfile, const char *ext)
{
	pack_t *pack;
	packfile_t *entry;
	void **item;
	uint32 count, i;

	if (strlen (packfile) >= MAX_OSPATH)
		return NULL;
	pack = Z_TagMalloc (sizeof (*pack), TAGMALLOC_FSLOADPAK);
	memset (pack, 0, sizeof (*pack));
	Q_strncpy (pack->filename, packfile, sizeof (pack->filename) - 1);
	if (!Q_stricmp (ext, "pak"))
	{
		dpackheader_t header;
		packdiskfile_t disk;
		int64 size;
		uint32 offset, bytes, position, length;
		size_t namelen;

		pack->type = PAK_QUAKE;
		pack->h.handle = fopen (packfile, "rb");
		if (!pack->h.handle)
			goto failed;
		if ((size = FS_PackFileSize (pack->h.handle)) < 0 ||
			fread (&header, 1, sizeof (header), pack->h.handle) != sizeof (header) ||
			LittleLong (header.ident) != IDPAKHEADER)
			goto failed;
		offset = LittleLong (header.dirofs);
		bytes = LittleLong (header.dirlen);
		if (offset < sizeof (header) || offset > (uint64)size || bytes > (uint64)size - offset ||
			bytes % sizeof (disk) || !(count = bytes / sizeof (disk)) || count > MAX_PACK_ENTRIES)
			goto failed;
		if (count > MAX_FILES_IN_PACK)
			Com_Printf ("WARNING: Pack %s has %u entries; old clients may reject it\n",
				LOG_WARNING | LOG_GENERAL, packfile, count);
		pack->files = Z_TagMalloc (count * sizeof (*pack->files), TAGMALLOC_FSLOADPAK);
		pack->rb = rbinit ((int (EXPORT *) (const void *, const void *))strcmp, count);
		if (FS_SeekPackFile (pack->h.handle, offset, SEEK_SET))
			goto failed;
		for (i = 0; i < count; i++)
		{
			if (fread (&disk, 1, sizeof (disk), pack->h.handle) != sizeof (disk))
				goto failed;
			position = LittleLong (disk.filepos);
			length = LittleLong (disk.filelen);
			if (position > (uint64)size || length > INT_MAX || length > (uint64)size - position)
				goto failed;
			for (namelen = 0; namelen < sizeof (disk.name) && disk.name[namelen]; namelen++)
			{
			}
			entry = &pack->files[pack->numfiles];
			if (!FS_PackName (entry->name, disk.name, namelen))
			{
				Com_Printf ("WARNING: Rejected invalid entry name in %s\n", LOG_WARNING | LOG_GENERAL, packfile);
				continue;
			}
			entry->filepos = position;
			entry->filelen = length;
			item = rbsearch (entry->name, pack->rb);
			*item = entry;
			pack->numfiles++;
		}
	}
#ifndef NO_ZLIB
	else if (!Q_stricmp (ext, "pkz"))
	{
		unz_global_info64 global;
		unz_file_info64 info;
		char name[MAX_QPATH];
		FILE *probe;
		int64 size;

		pack->type = PAK_ZIP;
		probe = fopen (packfile, "rb");
		if (!probe)
			goto failed;
		size = FS_PackFileSize (probe);
		fclose (probe);
		if (size < 0)
			goto failed;
		pack->h.zhandle = unzOpen64 (packfile);
		if (!pack->h.zhandle || unzGetGlobalInfo64 (pack->h.zhandle, &global) != UNZ_OK ||
			!global.number_entry || global.number_entry > MAX_PACK_ENTRIES ||
			unzGoToFirstFile (pack->h.zhandle) != UNZ_OK)
			goto failed;
		count = (uint32)global.number_entry;
		pack->files = Z_TagMalloc (count * sizeof (*pack->files), TAGMALLOC_FSLOADPAK);
		pack->rb = rbinit ((int (EXPORT *) (const void *, const void *))strcmp, count);
		for (i = 0; i < count; i++)
		{
			memset (name, 0, sizeof (name));
			if (unzGetCurrentFileInfo64 (pack->h.zhandle, &info, name, sizeof (name), NULL, 0, NULL, 0) != UNZ_OK)
				goto failed;
			entry = &pack->files[pack->numfiles];
			if (!info.size_filename || info.size_filename >= sizeof (name) ||
				strlen (name) != info.size_filename ||
				!FS_PackName (entry->name, name, info.size_filename))
				Com_Printf ("WARNING: Rejected invalid ZIP entry name in %s\n", LOG_WARNING | LOG_GENERAL, packfile);
			else if (name[info.size_filename - 1] == '/' || (info.external_fa & 16))
				; /* Directory records do not provide assets. */
			else if ((info.flag & (1 | 64 | 8192)) ||
				(info.compression_method != 0 && info.compression_method != Z_DEFLATED) ||
				info.uncompressed_size > INT_MAX ||
				(!info.compression_method && info.compressed_size != info.uncompressed_size))
				Com_Printf ("WARNING: Rejected unsupported ZIP entry %s in %s\n",
					LOG_WARNING | LOG_GENERAL, entry->name, packfile);
			else
			{
				entry->filepos = unzGetOffset64 (pack->h.zhandle);
				entry->filelen = (uint32)info.uncompressed_size;
				if (!entry->filepos || entry->filepos > (uint64)size)
					goto failed;
				item = rbsearch (entry->name, pack->rb);
				*item = entry;
				pack->numfiles++;
			}
			if (unzGoToNextFile (pack->h.zhandle) !=
				(i + 1 < count ? UNZ_OK : UNZ_END_OF_LIST_OF_FILE))
				goto failed;
		}
	}
#endif
	else
		goto failed;
	if (!pack->numfiles)
		goto failed;
	Com_Printf ("Added packfile %s (%i files)\n", LOG_GENERAL, packfile, pack->numfiles);
	return pack;
failed:
	Com_Printf ("WARNING: Could not mount packfile %s\n", LOG_WARNING | LOG_GENERAL, packfile);
	FS_FreePack (pack);
	return NULL;
}

static const char *FS_PackBaseName (const char *path)
{
	const char *base = path;
	while (*path)
	{
		if (*path == '/' || *path == '\\')
			base = path + 1;
		path++;
	}
	return base;
}

/* Adapted from Q2PRO (Andrey Nazarov): numeric pak prefixes precede other names;
 * the remainder breaks numeric ties, including mixed .pak/.pkz extensions. */
static int EXPORT pakcmp (const void *a, const void *b)
{
	const char *first = FS_PackBaseName (*(const char *const *)a);
	const char *second = FS_PackBaseName (*(const char *const *)b);
	qboolean firstpak = !Q_strncasecmp (first, "pak", 3);
	qboolean secondpak = !Q_strncasecmp (second, "pak", 3);
	unsigned long n1, n2;
	char *end1, *end2;

	if (firstpak && secondpak)
	{
		n1 = strtoul (first + 3, &end1, 10);
		n2 = strtoul (second + 3, &end2, 10);
		if (n1 != n2)
			return n1 < n2 ? -1 : 1;
		return Q_stricmp (end1, end2);
	}
	if (firstpak != secondpak)
		return firstpak ? -1 : 1;
	return Q_stricmp (first, second);
}

static void FS_LoadPaks (const char *dir)
{
	char pattern[MAX_OSPATH], **names = NULL, **grown, *found;
	const char *ext;
	size_t count = 0, capacity = 0, i, length;
	pack_t *pak;
	searchpath_t *search;

	Com_sprintf (pattern, sizeof (pattern), "%s/*", dir);
	found = Sys_FindFirst (pattern, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM);
	while (found)
	{
		length = strlen (found);
		ext = strrchr (found, '.');
		if (length < MAX_OSPATH && ext && (!Q_stricmp (ext, ".pak")
#ifndef NO_ZLIB
											  || !Q_stricmp (ext, ".pkz")
#endif
												  ))
		{
			if (count == capacity)
			{
				if (capacity >= MAX_PACK_ENTRIES)
					goto failed;
				capacity = capacity ? capacity * 2 : 16;
				if (capacity > MAX_PACK_ENTRIES)
					capacity = MAX_PACK_ENTRIES;
				grown = realloc (names, capacity * sizeof (*names));
				if (!grown)
					goto failed;
				names = grown;
			}
			names[count] = malloc (length + 1);
			if (!names[count])
				goto failed;
			memcpy (names[count++], found, length + 1);
		}
		found = Sys_FindNext (0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM);
	}
	Sys_FindClose ();
	if (count)
		qsort (names, count, sizeof (*names), pakcmp);
	for (i = 0; i < count; i++)
	{
		ext = strrchr (names[i], '.') + 1;
		pak = FS_LoadPackFile (names[i], ext);
		if (pak)
		{
			search = Z_TagMalloc (sizeof (*search), TAGMALLOC_SEARCHPATH);
			search->pack = pak;
			search->filename[0] = 0;
			search->next = fs_searchpaths;
			fs_searchpaths = search;
		}
		free (names[i]);
	}
	free (names);
	return;
failed:
	Sys_FindClose ();
	for (i = 0; i < count; i++)
		free (names[i]);
	free (names);
	Com_Printf ("WARNING: Could not enumerate packs in %s\n", LOG_WARNING | LOG_GENERAL, dir);
}

/*
================
FS_AddGameDirectory

Sets fs_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ... 
================
*/
static void FS_AddGameDirectory (const char *dir)
{
	searchpath_t	*search;

	Q_strncpy (fs_gamedir, dir, sizeof(fs_gamedir)-1);

	Com_DPrintf ("FS_AddGameDirectory: Added '%s'\n", dir);

	//
	// add the directory to the search path
	//
	search = Z_TagMalloc (sizeof(searchpath_t), TAGMALLOC_SEARCHPATH);
	strcpy (search->filename, fs_gamedir);
	search->pack = NULL;
	search->next = fs_searchpaths;
	fs_searchpaths = search;

	FS_LoadPaks (dir);
}

/*
============
FS_Gamedir

Called to find where to write a file (demos, savegames, etc)
============
*/
char * EXPORT FS_Gamedir (void)
{
	if (fs_gamedir[0])
		return fs_gamedir;
	else
		return BASEDIRNAME;
}

/*
============
FS_ExistsInGameDir

See if a file exists in the mod directory/paks (ignores baseq2)
============
*/
qboolean FS_ExistsInGameDir (char *filename)
{
	size_t			len;
	char			*gamedir;
	char			lowered[MAX_QPATH];
	searchpath_t	*search;
	pack_t			*pak;

	Q_strncpy (lowered, filename, sizeof(lowered)-1);
	fast_strlwr (lowered);

	gamedir = FS_Gamedir();
	len = strlen(gamedir);

	for (search = fs_searchpaths ; search ; search = search->next)
	{
		// is the element a pak file?
		if (search->pack)
		{
			packfile_t	*entry;

			//r1: optimized btree search
			pak = search->pack;

			if (strncmp (pak->filename, gamedir, len))
				continue;

			entry = rbfind (lowered, pak->rb);

			if (entry)
				return true;
		}
		else
		{
			char	netpath[MAX_OSPATH];

			if (strncmp (search->filename, gamedir, len))
				continue;

			Com_sprintf (netpath, sizeof(netpath), "%s/%s",search->filename, filename);

			if (Sys_FileLength (netpath) != -1)
				return true;
		}
	}

	return false;
}

/*
=============
FS_ExecConfig
=============
*/
void FS_ExecConfig (const char *filename)
{
	const char	*dir;
	char		name [MAX_QPATH];

	dir = Cvar_VariableString("gamedir");
	if (dir[0])
		Com_sprintf(name, sizeof(name), "%s/%s/%s", fs_basedir->string, dir, filename); 
	else
		Com_sprintf(name, sizeof(name), "%s/%s/%s", fs_basedir->string, BASEDIRNAME, filename); 
	if (Sys_FindFirst(name, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM))
		Cbuf_AddText (va ("exec %s\n", filename));
	Sys_FindClose();
}

void FS_ReloadPAKs (void)
{
	const char		*dir;
	searchpath_t	*next;

	FS_FlushCache ();

	//
	// free up any current game dir info
	//
	while (fs_searchpaths != fs_base_searchpaths)
	{
		FS_FreePack (fs_searchpaths->pack);
		next = fs_searchpaths->next;
		Z_Free (fs_searchpaths);
		fs_searchpaths = next;
	}

	dir = Cvar_VariableString ("gamedir");

	if (dir[0] && strcmp(dir, BASEDIRNAME))
		FS_AddGameDirectory (va("%s/%s", fs_basedir->string, dir) );
}

/*
================
FS_SetGamedir

Sets the gamedir and path to a different directory.
================
*/
void FS_SetGamedir (const char *dir)
{
	searchpath_t	*next;

	if (strstr(dir, "..") || strchr(dir, '/')
		|| strchr(dir, '\\') || strchr(dir, ':') )
	{
		Com_Printf ("Gamedir '%s' should be a single filename, not a path\n", LOG_GENERAL, dir);
		return;
	}

#ifndef DEDICATED_ONLY
	Con_SaveHistory ();
#endif

	//
	// free up any current game dir info
	//
	while (fs_searchpaths != fs_base_searchpaths)
	{
		FS_FreePack (fs_searchpaths->pack);
		next = fs_searchpaths->next;
		Z_Free (fs_searchpaths);
		fs_searchpaths = next;
	}

	//
	// flush all data, so it will be forced to reload
	//

	FS_FlushCache();

#ifndef DEDICATED_ONLY
#ifndef NO_SERVER
	if (!dedicated->intvalue)
	{
#endif
		//Cbuf_AddText ("vid_restart\nsnd_restart\n");
		Cmd_ExecuteString ("vid_restart");
		Cmd_ExecuteString ("snd_restart");
#ifndef NO_SERVER
	}
#endif
#endif

	if (!strcmp(dir,BASEDIRNAME) || (*dir == 0))
	{
		Com_sprintf (fs_gamedir, sizeof(fs_gamedir), "%s/%s", fs_basedir->string, BASEDIRNAME);
		Cvar_FullSet ("gamedir", "", CVAR_SERVERINFO|CVAR_NOSET);
		Cvar_FullSet ("game", "", CVAR_LATCH|CVAR_SERVERINFO);
	}
	else
	{
		Com_sprintf (fs_gamedir, sizeof(fs_gamedir), "%s/%s", fs_basedir->string, dir);
		Cvar_FullSet ("gamedir", dir, CVAR_SERVERINFO|CVAR_NOSET);
		FS_AddGameDirectory (va("%s/%s", fs_basedir->string, dir) );
	}
#ifndef DEDICATED_ONLY
	Con_LoadHistory ();
#endif
}


/*
================
FS_Link_f

Creates a filelink_t
================
*/
static void FS_Link_f (void)
{
	char		*to;
	filelink_t	*l, **prev;

	if (Cmd_Argc() != 3)
	{
		Com_Printf ("Purpose: Create a link from one file system path to another.\n"
					"Syntax : link <from> <to>\n"
					"Example: link ./aq2/maps ./action/maps\n", LOG_GENERAL);
		return;
	}

	//r1: validate destination. prevents people from reading outside the q2 dir
	//if rcon pass is compromised for example, rcon link ./baseq2/maps/lol.bsp /etc/passwd, download maps/lol.bsp
	to = Cmd_Argv(2);

	if (to[0])
	{
		if (strstr (to, "..") || strchr (to, '\\') || *to != '.')
		{
			Com_Printf ("Bad destination path.\n", LOG_GENERAL);
			return;
		}
	}

	// see if the link already exists
	prev = &fs_links;
	for (l=fs_links ; l ; l=l->next)
	{
		if (!strcmp (l->from, Cmd_Argv(1)))
		{
			Z_Free (l->to);
			if (!*to)
			{	// delete it
				*prev = l->next;
				Z_Free (l->from);
				Z_Free (l);
				return;
			}
			l->to = CopyString (to, TAGMALLOC_LINK);
			return;
		}
		prev = &l->next;
	}

	// create a new link
	l = Z_TagMalloc(sizeof(*l), TAGMALLOC_LINK);
	l->next = fs_links;
	fs_links = l;
	l->from = CopyString(Cmd_Argv(1), TAGMALLOC_LINK);
	l->fromlength = (int)strlen(l->from);
	l->to = CopyString(Cmd_Argv(2), TAGMALLOC_LINK);
}

/*
** FS_ListFiles
*/
char /*@null@*/ **FS_ListFiles( char *findname, int *numfiles, uint32 musthave, uint32 canthave )
{
	char *s;
	int nfiles = 0;
	char **list = 0;

	s = Sys_FindFirst( findname, musthave, canthave );
	while ( s )
	{
		if ( s[strlen(s)-1] != '.' )
			nfiles++;
		s = Sys_FindNext( musthave, canthave );
	}
	Sys_FindClose ();

	if ( !nfiles )
		return NULL;

	nfiles++; // add space for a guard
	*numfiles = nfiles;

	list = malloc( sizeof( char * ) * nfiles );
	memset( list, 0, sizeof( char * ) * nfiles );

	s = Sys_FindFirst( findname, musthave, canthave );
	nfiles = 0;
	while ( s )
	{
		if ( s[strlen(s)-1] != '.' )
		{
			list[nfiles] = strdup( s );
#ifdef _WIN32
			Q_strlwr( list[nfiles] );
#endif
			nfiles++;
		}
		s = Sys_FindNext( musthave, canthave );
	}
	Sys_FindClose ();

	return list;
}

/*
** FS_Dir_f
*/
static void FS_Dir_f( void )
{
	char	*path = NULL;
	char	findname[1024];
	char	wildcard[1024] = "*.*";
	char	**dirnames;
	int		ndirs;

	if ( Cmd_Argc() != 1 )
	{
		//r1: another overflow was here!
		Q_strncpy( wildcard, Cmd_Argv( 1 ), sizeof(wildcard)-1);
	}

	while ( ( path = FS_NextPath( path ) ) != NULL )
	{
		char *tmp = findname;

		Com_sprintf( findname, sizeof(findname), "%s/%s", path, wildcard );

		while ( *tmp != 0 )
		{
			if ( *tmp == '\\' ) 
				*tmp = '/';
			tmp++;
		}
		Com_Printf( "Directory of %s\n", LOG_GENERAL, findname );
		Com_Printf( "----\n", LOG_GENERAL );

		if ( ( dirnames = FS_ListFiles( findname, &ndirs, 0, 0 ) ) != 0 )
		{
			int i;

			for ( i = 0; i < ndirs-1; i++ )
			{
				if ( strrchr( dirnames[i], '/' ) )
					Com_Printf( "%s\n", LOG_GENERAL, strrchr( dirnames[i], '/' ) + 1 );
				else
					Com_Printf( "%s\n", LOG_GENERAL, dirnames[i] );

				free( dirnames[i] );
			}
			free( dirnames );
		}
		Com_Printf( "\n", LOG_GENERAL );
	};
}

/*
============
FS_Path_f

============
*/
static void FS_Path_f (void)
{
	searchpath_t	*s;
	filelink_t		*l;

	Com_Printf ("Current search path:\n", LOG_GENERAL);
	for (s=fs_searchpaths ; s ; s=s->next)
	{
		if (s == fs_base_searchpaths)
			Com_Printf ("----------\n", LOG_GENERAL);
		if (s->pack)
			Com_Printf ("%s (%i files)\n", LOG_GENERAL, s->pack->filename, s->pack->numfiles);
		else
			Com_Printf ("%s\n", LOG_GENERAL, s->filename);
	}

	Com_Printf ("\nLinks:\n", LOG_GENERAL);
	for (l=fs_links ; l ; l=l->next)
		Com_Printf ("%s : %s\n", LOG_GENERAL, l->from, l->to);
}

/*
================
FS_NextPath

Allows enumerating all of the directories in the search path
================
*/
char /*@null@*/ *FS_NextPath (const char *prevpath)
{
	searchpath_t	*s;
	char			*prev;

	if (!prevpath)
		return fs_gamedir;

	prev = fs_gamedir;
	for (s=fs_searchpaths ; s ; s=s->next)
	{
		if (s->pack)
			continue;
		if (prevpath == prev)
			return s->filename;
		prev = s->filename;
	}

	return NULL;
}

/*
================
FS_InitFilesystem
================
*/
void FS_InitFilesystem (void)
{
	current_filename = "unknown";

	Cmd_AddCommand ("path", FS_Path_f);
	Cmd_AddCommand ("link", FS_Link_f);
	Cmd_AddCommand ("dir", FS_Dir_f );

	//r1: search for a file
	Cmd_AddCommand ("whereis", FS_WhereIs_f);

	//r1: allow manual cache flushing
	Cmd_AddCommand ("fsflushcache", FS_FlushCache);

	//r1: fs stats
	Cmd_AddCommand ("fs_stats", FS_Stats_f);

	//r1: binary tree filesystem cache
	FS_InitCache ();

	//r1: init fs cache
	//FS_FlushCache ();

	//
	// basedir <path>
	// allows the game to run from outside the data tree
	//
	fs_basedir = Cvar_Get ("basedir", ".", CVAR_NOSET);
	fs_cache = Cvar_Get ("fs_cache", "7", 0);
	fs_noextern = Cvar_Get ("fs_noextern", "0", 0);

	//
	// start up with baseq2 by default
	//
	FS_AddGameDirectory (va("%s/"BASEDIRNAME, fs_basedir->string) );

	// any set gamedirs will be freed up to here
	fs_base_searchpaths = fs_searchpaths;

	// check for game override
	fs_gamedirvar = Cvar_Get ("game", "", CVAR_LATCH|CVAR_SERVERINFO);
	if (fs_gamedirvar->string[0])
		FS_SetGamedir (fs_gamedirvar->string);
}


#ifndef DEDICATED_ONLY
/* Private metadata reads never materialize an entire compressed demo. Native
 * playback still performs its normal complete archive validation. */
static int FS_JumpLoosePrefix (const char *path, void *buffer, int capacity, int *size)
{
	FILE *file;
	struct stat info;
	int count;

	if (stat (path, &info) || (info.st_mode & S_IFMT) != S_IFREG || info.st_size < 0 || info.st_size > INT_MAX)
		return -1;
	file = fopen (path, "rb");
	if (!file)
		return -1;
	*size = (int)info.st_size;
	count = *size < capacity ? *size : capacity;
	if (fread (buffer, 1, count, file) != (size_t)count)
		count = -1;
	fclose (file);
	return count;
}

int FS_JumpReadPrefix (const char *filename, void *buffer, int capacity, int *size)
{
	searchpath_t *search;
	filelink_t *link;
	packfile_t *entry;
	char path[MAX_OSPATH], lowered[MAX_QPATH];
	FILE *file;
	int count;

	if (!size)
		return -1;
	*size = -1;
	if (!filename || !*filename || strlen (filename) >= sizeof (lowered) ||
		filename[0] == '/' || strchr (filename, '\\') || strchr (filename, ':') ||
		strstr (filename, "..") || !buffer || capacity < 1 || capacity > MAX_MSGLEN + 4)
		return -1;
	if (!fs_noextern->intvalue)
	{
		for (link = fs_links; link; link = link->next)
		{
			if (!strncmp (filename, link->from, link->fromlength))
			{
				Com_sprintf (path, sizeof (path), "%s%s", link->to, filename + link->fromlength);
				return FS_JumpLoosePrefix (path, buffer, capacity, size);
			}
		}
	}
	strcpy (lowered, filename);
	fast_strlwr (lowered);
	for (search = fs_searchpaths; search; search = search->next)
	{
		if (!search->pack)
		{
			if (fs_noextern->intvalue)
				continue;
			Com_sprintf (path, sizeof (path), "%s/%s", search->filename, filename);
			count = FS_JumpLoosePrefix (path, buffer, capacity, size);
			if (count >= 0)
				return count;
			continue;
		}
		entry = rbfind (lowered, search->pack->rb);
		if (!entry)
			continue;
		entry = *(packfile_t **)entry;
		if (entry->filelen > INT_MAX)
			return -1;
		*size = (int)entry->filelen;
		count = *size < capacity ? *size : capacity;
#ifndef NO_ZLIB
		if (search->pack->type == PAK_ZIP)
		{
			unzFile zip = search->pack->h.zhandle;
			unz_file_info64 info;
			uint64 previous = unzGetOffset64 (zip);
			int result, closeResult;

			result = -1;
			if (unzSetOffset64 (zip, entry->filepos) == UNZ_OK &&
				unzGetCurrentFileInfo64 (zip, &info, NULL, 0, NULL, 0, NULL, 0) == UNZ_OK &&
				info.uncompressed_size == entry->filelen && unzOpenCurrentFile (zip) == UNZ_OK)
			{
				result = unzReadCurrentFile (zip, buffer, count);
				closeResult = unzCloseCurrentFile (zip);
				if (result != count || (count == *size && closeResult != UNZ_OK))
					result = -1;
			}
			unzSetOffset64 (zip, previous);
			return result;
		}
#endif
		if (search->pack->type != PAK_QUAKE)
			return -1;
		file = fopen (search->pack->filename, "rb");
		if (!file)
			return -1;
		if (FS_SeekPackFile (file, entry->filepos, SEEK_SET) || fread (buffer, 1, count, file) != (size_t)count)
			count = -1;
		fclose (file);
		return count;
	}
	return -1;
}

/* Private client browser enumeration. Search archives and loose game paths in
 * native priority order, collapse duplicates, and keep ownership with malloc. */
static void FS_JumpListAdd (char **list, int *count, const char *name, size_t length)
{
	int i;

	if (!length || length >= MAX_QPATH || *count >= 8192)
		return;
	for (i = 0; i < *count; i++)
		if (strlen (list[i]) == length && !Q_strncasecmp (list[i], name, (int)length))
			return;
	list[*count] = malloc (length + 1);
	if (!list[*count])
		return;
	memcpy (list[*count], name, length);
	list[(*count)++][length] = 0;
}

char **FS_JumpList (const char *directory, const char *suffix, qboolean dirs, int *count)
{
	searchpath_t *path;
	char **list;
	char prefix[MAX_QPATH], find[MAX_OSPATH];
	size_t length, slength;
	int i;

	*count = 0;
	if (!directory || strlen (directory) >= sizeof (prefix) - 2 ||
		directory[0] == '/' || strchr (directory, '\\') || strchr (directory, ':') || strstr (directory, ".."))
		return NULL;
	Com_sprintf (prefix, sizeof (prefix), "%s%s", directory, *directory ? "/" : "");
	length = strlen (prefix);
	slength = suffix ? strlen (suffix) : 0;
	list = calloc (8193, sizeof (*list));
	if (!list)
		return NULL;
	for (path = fs_searchpaths; path; path = path->next)
	{
		if (path->pack)
		{
			for (i = 0; i < path->pack->numfiles; i++)
			{
				const char *name = path->pack->files[i].name, *slash;
				size_t n;

				if (Q_strncasecmp (name, prefix, (int)length))
					continue;
				name += length;
				slash = strchr (name, '/');
				n = strlen (name);
				if (dirs)
				{
					if (slash)
						FS_JumpListAdd (list, count, name, (size_t)(slash - name));
				}
				else if (!slash && n >= slength && (!slength || !Q_stricmp (name + n - slength, suffix)))
					FS_JumpListAdd (list, count, name, n);
			}
		}
		else if (!fs_noextern->intvalue)
		{
			char *found;
			unsigned must = dirs ? SFF_SUBDIR : 0, cant = dirs ? 0 : SFF_SUBDIR;

			Com_sprintf (find, sizeof (find), "%s/%s%s*", path->filename, directory, *directory ? "/" : "");
			found = Sys_FindFirst (find, must, cant);
			while (found)
			{
				const char *name = strrchr (found, '/'), *back = strrchr (found, '\\');
				size_t n;

				if (back && (!name || back > name))
					name = back;
				name = name ? name + 1 : found;
				n = strlen (name);
				if (strcmp (name, ".") && strcmp (name, "..") &&
					(dirs || (n >= slength && (!slength || !Q_stricmp (name + n - slength, suffix)))))
					FS_JumpListAdd (list, count, name, n);
				found = Sys_FindNext (must, cant);
			}
			Sys_FindClose ();
		}
	}
	if (!*count)
	{
		free (list);
		return NULL;
	}
	return list;
}
#endif
