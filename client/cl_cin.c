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
#ifdef CINEMATICS
#include "client.h"
#include "snd_music.h"
#include "cl_movie.h"

typedef struct
{
	byte	*data;
	int		count;
} cblock_t;

typedef struct
{
	qboolean	restart_sound;
	int		s_rate;
	int		s_width;
	int		s_channels;
	int file_remaining;

	int		width;
	int		height;
	int crop_start;
	int crop_pixels;
	byte	*pic;
	byte	*pic_pending;

	// order 1 huffman stuff
	int		*hnodes1;	// [256][256][2];
	int		numhnodes1[256];

	int		h_used[512];
	int		h_count[512];
} cinematics_t;

cinematics_t	cin;

typedef struct
{
	const char *name;
	int size, start, crop;
} cin_crop_t;

/* Apply the stock movie mattes only to the matching original files. */
static const cin_crop_t cin_crops[] = {
	{ "ntro.cin", 82836235, 727, 30 },
	{ "end.cin", 19311290, 0, 30 },
	{ "rintro.cin", 38434032, 0, 24 },
	{ "rend.cin", 22580919, 0, 24 },
	{ "xin.cin", 13226649, 0, 32 },
	{ "xout.cin", 11194445, 0, 32 }
};

void SCR_CinematicCropForFile (const char *name, int size, int *start, int *crop)
{
	unsigned i;
	*start = *crop = 0;
	for (i = 0; i < sizeof (cin_crops) / sizeof (cin_crops[0]); i++)
	{
		if (!Q_stricmp (name, cin_crops[i].name) && size == cin_crops[i].size)
		{
			*start = cin_crops[i].start;
			*crop = cin_crops[i].crop * 2;
			return;
		}
	}
}

/*
=================================================================

PCX LOADING

=================================================================
*/


/*
==============
SCR_LoadPCX
==============
*/
void SCR_LoadPCX (char *filename, byte **pic, byte **palette, int *width, int *height)
{
	byte *raw, *data, *end;
	pcx_t	*pcx;
	int		x, y;
	int		len;
	int		dataByte, runLength;
	byte	*out, *pix;
	FILE *file;
	qboolean closeHandle;

	*pic = NULL;
	if (palette)
		*palette = NULL;

	len = FS_FOpenFile (filename, &file, HANDLE_DUPE, &closeHandle);
	if (!file)
		return;
	if (len < (int)offsetof (pcx_t, data) + 1 || len > 2 * 1024 * 1024 ||
		(palette && len < (int)offsetof (pcx_t, data) + 1 + 768))
	{
		Com_Printf ("Bad pcx file %s\n", LOG_CLIENT, filename);
		fclose (file);
		return;
	}
	raw = Z_TagMalloc (len, TAGMALLOC_NOT_TAGGED);
	if (fread (raw, 1, len, file) != (size_t)len)
	{
		Com_Printf ("Incomplete pcx file %s\n", LOG_CLIENT, filename);
		fclose (file);
		Z_Free (raw);
		return;
	}
	fclose (file);

	pcx = (pcx_t *)raw;
	data = &pcx->data;
	end = raw + len - (palette ? 768 : 0);

	if (pcx->manufacturer != 0x0a || pcx->version != 5 || pcx->encoding != 1 || pcx->bits_per_pixel != 8 || pcx->xmin != 0 || pcx->ymin != 0 || pcx->xmax >= 640 || pcx->ymax >= 480)
	{
		Com_Printf ("Bad pcx file %s\n", LOG_CLIENT, filename);
		Z_Free (raw);
		return;
	}

	out = Z_TagMalloc ((pcx->ymax + 1) * (pcx->xmax + 1), TAGMALLOC_NOT_TAGGED);
	pix = out;

	for (y=0 ; y<=pcx->ymax ; y++, pix += pcx->xmax+1)
	{
		for (x=0 ; x<=pcx->xmax ; )
		{
			if (data >= end)
				goto malformed;
			dataByte = *data++;

			if((dataByte & 0xC0) == 0xC0)
			{
				runLength = dataByte & 0x3F;
				if (!runLength || data >= end)
					goto malformed;
				dataByte = *data++;
			}
			else
				runLength = 1;

			if (runLength > pcx->xmax + 1 - x)
				goto malformed;
			while(runLength-- > 0)
				pix[x++] = dataByte;
		}
	}
	if (palette)
	{
		*palette = Z_TagMalloc (768, TAGMALLOC_NOT_TAGGED);
		memcpy (*palette, raw + len - 768, 768);
	}
	if (width)
		*width = pcx->xmax + 1;
	if (height)
		*height = pcx->ymax + 1;
	*pic = out;
	Z_Free (raw);
	return;

malformed:
	Com_Printf ("PCX file %s was malformed\n", LOG_CLIENT, filename);
	Z_Free (out);
	Z_Free (raw);
}

//=============================================================

/*
==================
SCR_StopCinematic
==================
*/
void SCR_StopCinematic (void)
{
	CL_MovieStop ();
	cl.cinematictime = 0;	// done
	cin.file_remaining = 0;
	if (cin.pic)
	{
		Z_Free (cin.pic);
		cin.pic = NULL;
	}
	if (cin.pic_pending)
	{
		Z_Free (cin.pic_pending);
		cin.pic_pending = NULL;
	}
	if (cl.cinematicpalette_active)
	{
		if (reflib_active)
			re.CinematicSetPalette (NULL);
		cl.cinematicpalette_active = false;
	}
	if (cl.cinematic_file)
	{
		fclose (cl.cinematic_file);
		cl.cinematic_file = NULL;
	}
	if (cin.hnodes1)
	{
		Z_Free (cin.hnodes1);
		cin.hnodes1 = NULL;
	}

	// switch back down to 11 khz sound if necessary
	if (cin.restart_sound)
	{
		cin.restart_sound = false;
		CL_Snd_Restart_f ();
	}

}

/*
====================
SCR_FinishCinematic

Called when either the cinematic completes, or it is aborted
====================
*/
void SCR_FinishCinematic (void)
{
	// tell the server to advance to the next map / cinematic
	MSG_WriteByte (clc_stringcmd);
	MSG_Print (va ("nextserver %i\n", cl.servercount));
	MSG_EndWriting (&cls.netchan.message);
}

//==========================================================================

/*
==================
SmallestNode1
==================
*/
int	SmallestNode1 (int numhnodes)
{
	int		i;
	int		best, bestnode;

	best = 99999999;
	bestnode = -1;
	for (i=0 ; i<numhnodes ; i++)
	{
		if (cin.h_used[i])
			continue;
		if (!cin.h_count[i])
			continue;
		if (cin.h_count[i] < best)
		{
			best = cin.h_count[i];
			bestnode = i;
		}
	}

	if (bestnode == -1)
		return -1;

	cin.h_used[bestnode] = true;
	return bestnode;
}


/*
==================
Huff1TableInit

Reads the 64k counts table and initializes the node trees
==================
*/
static qboolean SCR_CinematicRead (void *buffer, int size)
{
	if (size < 0 || size > cin.file_remaining ||
		fread (buffer, 1, size, cl.cinematic_file) != (size_t)size)
		return false;
	cin.file_remaining -= size;
	return true;
}

qboolean Huff1TableInit (void)
{
	int		prev;
	int		j;
	int		*node, *nodebase;
	byte	counts[256];
	int		numhnodes;

	cin.hnodes1 = Z_TagMalloc (256 * 256 * 2 * 4, TAGMALLOC_NOT_TAGGED);
	memset (cin.hnodes1, 0, 256*256*2*4);

	for (prev=0 ; prev<256 ; prev++)
	{
		memset (cin.h_count,0,sizeof(cin.h_count));
		memset (cin.h_used,0,sizeof(cin.h_used));

		// read a row of counts
		if (!SCR_CinematicRead (counts, sizeof (counts)))
			return false;
		for (j=0 ; j<256 ; j++)
			cin.h_count[j] = counts[j];

		// build the nodes
		numhnodes = 256;
		nodebase = cin.hnodes1 + prev*256*2;

		while (numhnodes != 511)
		{
			node = nodebase + (numhnodes-256)*2;

			// pick two lowest counts
			node[0] = SmallestNode1 (numhnodes);
			if (node[0] == -1)
				break;	// no more

			node[1] = SmallestNode1 (numhnodes);
			if (node[1] == -1)
			{
				/* A one-symbol tree needs its actual leaf as the root. */
				cin.numhnodes1[prev] = node[0];
				break;
			}

			cin.h_count[numhnodes] = cin.h_count[node[0]] + cin.h_count[node[1]];
			numhnodes++;
		}

		if (numhnodes > 256)
			cin.numhnodes1[prev] = numhnodes - 1;
		else if (node[0] == -1)
			cin.numhnodes1[prev] = -1;
	}
	return true;
}

/*
==================
Huff1Decompress
==================
*/
cblock_t Huff1Decompress (cblock_t in)
{
	cblock_t out;
	unsigned count, i;
	int prev, nodenum, bits_left, inbyte, input_pos;
	int *nodebase;

	out.data = NULL;
	out.count = 0;
	if (!in.data || in.count < 4 || !cin.hnodes1 ||
		cin.width < 1 || cin.height < 1 ||
		cin.width > 640 || cin.height > 480)
		return out;
	count = (unsigned)in.data[0] | ((unsigned)in.data[1] << 8) |
		((unsigned)in.data[2] << 16) | ((unsigned)in.data[3] << 24);
	if (count != (unsigned)(cin.width * cin.height))
		return out;

	out.data = Z_TagMalloc (count, TAGMALLOC_NOT_TAGGED);
	prev = 0;
	bits_left = 0;
	inbyte = 0;
	input_pos = 4;
	for (i = 0; i < count; i++)
	{
		nodenum = cin.numhnodes1[prev];
		nodebase = cin.hnodes1 + prev * 256 * 2;
		while (nodenum >= 256)
		{
			if (nodenum > 510)
				goto malformed;
			if (!bits_left)
			{
				if (input_pos >= in.count)
					goto malformed;
				inbyte = in.data[input_pos++];
				bits_left = 8;
			}
			nodenum = nodebase[(nodenum - 256) * 2 + (inbyte & 1)];
			inbyte >>= 1;
			bits_left--;
		}
		if (nodenum < 0 || nodenum > 255)
			goto malformed;
		out.data[i] = nodenum;
		prev = nodenum;
	}
	out.count = count;
	return out;

malformed:
	Z_Free (out.data);
	out.data = NULL;
	return out;
}

/*
==================
SCR_ReadNextFrame
==================
*/
byte *SCR_ReadNextFrame (void)
{
	int		command;
	byte	samples[22050/14*4];
	byte	compressed[0x20000];
	int		size;
	cblock_t	in, huf1;
	int count, sample_size;
	uint64 start, end;

	// read the next frame
	if (!SCR_CinematicRead (&command, 4))
		return NULL;
	command = LittleLong(command);
	if (command == 2)
		return NULL;	// last frame marker
	if (command != 0 && command != 1)
		return NULL;

	if (command == 1)
	{	// read palette
		if (!SCR_CinematicRead (cl.cinematicpalette, sizeof (cl.cinematicpalette)))
			return NULL;
		cl.cinematicpalette_active=0;	// dubious....  exposes an edge case
	}

	// decompress the next frame
	if (!SCR_CinematicRead (&size, 4))
		return NULL;
	size = LittleLong(size);
	if (size < 4 || size > (int)sizeof (compressed) ||
		!SCR_CinematicRead (compressed, size))
		return NULL;

	// read sound
	if (cin.s_rate)
	{
		start = (uint64)cl.cinematicframe * cin.s_rate / 14;
		end = ((uint64)cl.cinematicframe + 1) * cin.s_rate / 14;
		count = (int)(end - start);
		sample_size = count * cin.s_width * cin.s_channels;
		if (sample_size > (int)sizeof (samples) ||
			!SCR_CinematicRead (samples, sample_size))
			return NULL;
		S_RawSamples (count, cin.s_rate, cin.s_width, cin.s_channels, samples);
	}

	in.data = compressed;
	in.count = size;

	huf1 = Huff1Decompress (in);
	if (!huf1.data)
		return NULL;
	cl.cinematicframe++;
	return huf1.data;
}


/*
==================
SCR_RunCinematic

==================
*/
void SCR_RunCinematic (void)
{
	int		frame;

	if (cl.cinematictime <= 0)
	{
		SCR_StopCinematic ();
		return;
	}

	if (cl.cinematicframe == -1)
		return;		// static image
	if (CL_MovieActive ())
	{
		if (CL_MovieRun (cls.key_dest != key_game || cls.disable_screen))
			return;
		SCR_StopCinematic ();
		SCR_FinishCinematic ();
		cl.cinematictime = 1;
		SCR_BeginLoadingPlaque ();
		cl.cinematictime = 0;
		return;
	}

	if (cls.key_dest != key_game)
	{	// pause if menu or console is up
		cl.cinematictime = cls.realtime - cl.cinematicframe*1000/14;
		return;
	}

	frame = (cls.realtime - cl.cinematictime)*14.0/1000;
	if (frame <= cl.cinematicframe)
		return;
	if (frame > cl.cinematicframe+1)
	{
		Com_Printf ("Dropped frame: %i > %i\n", LOG_CLIENT, frame, cl.cinematicframe+1);
		cl.cinematictime = cls.realtime - cl.cinematicframe*1000/14;
	}
	if (cin.pic)
		Z_Free (cin.pic);
	cin.pic = cin.pic_pending;
	cin.pic_pending = NULL;
	cin.pic_pending = SCR_ReadNextFrame ();
	if (!cin.pic_pending)
	{
		SCR_StopCinematic ();
		SCR_FinishCinematic ();
		cl.cinematictime = 1;	// hack to get the black screen behind loading
		SCR_BeginLoadingPlaque ();
		cl.cinematictime = 0;
		return;
	}
}

/*
==================
SCR_DrawCinematic

Returns true if a cinematic is active, meaning the view rendering
should be skipped
==================
*/
qboolean SCR_DrawCinematic (void)
{
	float scale_w, scale_h, scale;
	int x, y, w, h, crop;

	if (cl.cinematictime <= 0)
	{
		return false;
	}

	if (cls.key_dest == key_menu)
	{	// blank screen and pause if menu is up
		re.CinematicSetPalette(NULL);
		cl.cinematicpalette_active = false;
		return true;
	}

	if (CL_MovieActive ())
	{
		CL_MovieDraw ();
		return true;
	}

	if (!cl.cinematicpalette_active)
	{
		re.CinematicSetPalette ((byte *)cl.cinematicpalette);
		cl.cinematicpalette_active = true;
	}

	if (!cin.pic)
		return true;

	if (cin.width <= 0 || cin.height <= 0 || viddef.width <= 0 || viddef.height <= 0)
		return true;
	crop = cl.cinematicframe >= cin.crop_start ? cin.crop_pixels : 0;
	if (crop >= cin.height)
		crop = 0;
	scale_w = (float)viddef.width / cin.width;
	scale_h = (float)viddef.height / (cin.height - crop);
	scale = scale_w < scale_h ? scale_w : scale_h;
	w = (int)(cin.width * scale + 0.5f);
	h = (int)(cin.height * scale + 0.5f);
	x = (viddef.width - w) / 2;
	y = (viddef.height - h) / 2;
	re.DrawFill (0, 0, viddef.width, viddef.height, 0);
	re.DrawStretchRaw (x, y, w, h,
		cin.width, cin.height, cin.pic);

	return true;
}

/*
==================
SCR_PlayCinematic

==================
*/
void SCR_PlayCinematic (char *arg)
{
	int width, height, file_size;
	byte		*palette;
	char		name[MAX_OSPATH], *dot;
	int			old_khz;
	qboolean	dummy;

	CL_MovieStop ();
	S_MusicBeginCinematic ();
	// make sure CD isn't playing music
#ifdef CD_AUDIO
	CDAudio_Stop();
#endif

	cl.cinematicframe = 0;
	cin.crop_start = cin.crop_pixels = 0;
	dot = strchr (arg, '.');
	if (dot && !strcmp (dot, ".pcx"))
	{	// static pcx image
		Com_sprintf (name, sizeof(name), "pics/%s", arg);
		SCR_LoadPCX (name, &cin.pic, &palette, &cin.width, &cin.height);
		cl.cinematicframe = -1;
		cl.cinematictime = 1;
		SCR_EndLoadingPlaque ();
		cls.state = ca_active;
		if (!cin.pic)
		{
			Com_Printf ("Could not load %s.\n", LOG_CLIENT, name);
			cl.cinematictime = 0;
			SCR_FinishCinematic ();
		}
		else
		{
			memcpy (cl.cinematicpalette, palette, sizeof(cl.cinematicpalette));
			Z_Free (palette);
		}
		return;
	}

	if (CL_MovieStart (arg))
	{
		SCR_EndLoadingPlaque ();
		cls.state = ca_active;
		cl.cinematictime = cls.realtime ? cls.realtime : 1;
		return;
	}

	Com_sprintf (name, sizeof(name), "video/%s", arg);
	file_size = FS_FOpenFile (name, &cl.cinematic_file, HANDLE_DUPE, &dummy);
	if (!cl.cinematic_file)
	{
//		Com_Error (ERR_DROP, "Cinematic %s not found.\n", name);
		SCR_FinishCinematic ();
		cl.cinematictime = 0;	// done
		return;
	}
	SCR_CinematicCropForFile (arg, file_size, &cin.crop_start, &cin.crop_pixels);

	SCR_EndLoadingPlaque ();

	cls.state = ca_active;

	cin.file_remaining = file_size;
	if (!SCR_CinematicRead (&width, 4) ||
		!SCR_CinematicRead (&height, 4) ||
		!SCR_CinematicRead (&cin.s_rate, 4) ||
		!SCR_CinematicRead (&cin.s_width, 4) ||
		!SCR_CinematicRead (&cin.s_channels, 4))
		goto malformed;
	cin.width = LittleLong(width);
	cin.height = LittleLong(height);
	cin.s_rate = LittleLong (cin.s_rate);
	cin.s_width = LittleLong (cin.s_width);
	cin.s_channels = LittleLong (cin.s_channels);
	if (cin.width < 1 || cin.width > 640 ||
		cin.height < 1 || cin.height > 480 ||
		(cin.s_rate && (cin.s_rate < 8000 || cin.s_rate > 22050 || cin.s_width < 1 || cin.s_width > 2 || cin.s_channels < 1 || cin.s_channels > 2)))
		goto malformed;

	if (!Huff1TableInit ())
		goto malformed;

	// switch up to 22 khz sound if necessary
	old_khz = Cvar_IntValue ("s_khz");
	if (cin.s_rate && old_khz != cin.s_rate / 1000)
	{
		cin.restart_sound = true;
		Cvar_SetValue ("s_khz", cin.s_rate/1000);
		CL_Snd_Restart_f ();
		Cvar_SetValue ("s_khz", old_khz);
	}

	cl.cinematicframe = 0;
	cin.pic = SCR_ReadNextFrame ();
	if (!cin.pic)
		goto malformed;
	cl.cinematictime = Sys_Milliseconds ();
	return;

malformed:
	Com_Printf ("Bad cinematic file %s\n", LOG_CLIENT, name);
	SCR_StopCinematic ();
	SCR_FinishCinematic ();
}
#endif
