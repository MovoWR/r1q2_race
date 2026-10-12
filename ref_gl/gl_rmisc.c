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
// r_misc.c

#ifdef WIN32
#include <process.h>
#include <io.h>
#include <share.h>
#endif

#include "gl_local.h"
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#if defined(_WIN32) || defined(__linux__)
void GLimp_SwapInterval (int interval);
#endif
#include <jpeglib.h>
#include <png.h>

qboolean GL_ClearErrors (void)
{
	int i;

	if (!qglGetError)
		return false;
	for (i = 0; i < 16; i++)
		if (qglGetError () == GL_NO_ERROR)
			return true;
	return false;
}

/* gl_showtris overlays reuse the caller's geometry and vertex program. */
static qboolean showtris_active;
static GLboolean showtris_color_mask[4];

qboolean GL_ShowTrisEnabled (int mask)
{
	float value = gl_showtris ? gl_showtris->value : 0;

	/* Reject non-finite/out-of-int-range cvars before the bitmask conversion. */
	return value >= -2147483648.0f && value < 2147483648.0f &&
		((int)value & mask & 15) != 0;
}

qboolean GL_ShowTrisActive (void)
{
	return showtris_active;
}

qboolean GL_ShowTrisBegin (int mask)
{
	GLint active;

	if (showtris_active || !GL_ShowTrisEnabled (mask))
		return false;

	qglPushAttrib (GL_ENABLE_BIT | GL_CURRENT_BIT | GL_POLYGON_BIT |
		GL_DEPTH_BUFFER_BIT | GL_LINE_BIT |
		(qglSelectTextureSGIS ? GL_TEXTURE_BIT : 0));
	qglPushClientAttrib (GL_CLIENT_VERTEX_ARRAY_BIT);
	showtris_active = true;
	/* Do not restore a single DRAW_BUFFER over an active bloom MRT route. */
	qglGetBooleanv (GL_COLOR_WRITEMASK, showtris_color_mask);

	/* Keep gl_ztrick and weapon depth ranges untouched, even when reversed. */
	qglDisable (GL_DEPTH_TEST);
	qglDepthMask (GL_FALSE);
	qglDisable (GL_BLEND);
	qglDisable (GL_ALPHA_TEST);
	qglDisable (GL_STENCIL_TEST);
	qglDisable (GL_FOG);
	qglDisable (GL_LIGHTING);
	qglDisable (GL_COLOR_LOGIC_OP);
	qglDisable (GL_LINE_STIPPLE);
	qglDisable (GL_POLYGON_STIPPLE);
	qglDisable (GL_POLYGON_OFFSET_LINE);
	qglColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	qglPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	qglLineWidth (1);
	qglColor4f (1, 1, 1, 1);

	/* Conventional color arrays can alias a GPU mesh's generic attributes. */
	if (!GL_SetShowTrisShader (true))
		qglDisableClientState (GL_COLOR_ARRAY);

	/* The legacy SGIS selector can differ from the engine's texture cache.
	 * Its texture attribute group restores the actual selection at End. */
	if (qglSelectTextureSGIS)
	{
		qglSelectTextureSGIS (GL_TEXTURE0);
		qglDisable (GL_TEXTURE_2D);
		qglSelectTextureSGIS (GL_TEXTURE1);
		qglDisable (GL_TEXTURE_2D);
	}
	else if (qglActiveTextureARB)
	{
		qglGetIntegerv (GL_ACTIVE_TEXTURE, &active);
		qglActiveTextureARB (GL_TEXTURE0);
		qglDisable (GL_TEXTURE_2D);
		qglActiveTextureARB (GL_TEXTURE1);
		qglDisable (GL_TEXTURE_2D);
		qglActiveTextureARB ((GLenum)active);
	}
	else
		qglDisable (GL_TEXTURE_2D);
	return true;
}

void GL_ShowTrisEnd (void)
{
	if (!showtris_active)
		return;
	GL_SetShowTrisShader (false);
	showtris_active = false;
	qglPopClientAttrib ();
	qglPopAttrib ();
	qglColorMask (showtris_color_mask[0], showtris_color_mask[1],
		showtris_color_mask[2], showtris_color_mask[3]);
}

static qboolean GL_ShowTrisTriangles (GLenum mode)
{
	return mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP ||
		mode == GL_TRIANGLE_FAN || mode == GL_POLYGON;
}

void GL_ShowTrisElements (int mask, GLenum mode, GLsizei count,
	GLenum type, const void *indices)
{
	if (count < 3 || !GL_ShowTrisTriangles (mode) || !GL_ShowTrisBegin (mask))
		return;
	qglDrawElements (mode == GL_POLYGON ? GL_TRIANGLE_FAN : mode, count, type, indices);
	GL_ShowTrisEnd ();
}

void GL_ShowTrisArrays (int mask, GLenum mode, GLint first, GLsizei count)
{
	GLint element = 0;
	GLuint indices[64 * 6];
	GLsizei base, quads, i;

	if (first < 0 || count < 3 || first > INT_MAX - count ||
		(!GL_ShowTrisTriangles (mode) && mode != GL_QUADS) || !GL_ShowTrisBegin (mask))
		return;
	if (mode == GL_QUADS)
	{
		if (qglBindBuffer)
		{
			qglGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &element);
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		/* Same diagonal as the core quad conversion, with bounded stack storage. */
		for (base = 0; base < count / 4; base += quads)
		{
			quads = count / 4 - base;
			if (quads > 64)
				quads = 64;
			for (i = 0; i < quads; i++)
			{
				GLuint v = (GLuint)first + (base + i) * 4;
				indices[i * 6] = v;
				indices[i * 6 + 1] = v + 1;
				indices[i * 6 + 2] = v + 3;
				indices[i * 6 + 3] = v + 1;
				indices[i * 6 + 4] = v + 2;
				indices[i * 6 + 5] = v + 3;
			}
			qglDrawElements (GL_TRIANGLES, quads * 6, GL_UNSIGNED_INT, indices);
		}
		if (qglBindBuffer)
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, (GLuint)element);
	}
	else
		qglDrawArrays (mode == GL_POLYGON ? GL_TRIANGLE_FAN : mode, first, count);
	GL_ShowTrisEnd ();
}

void GL_ShowTrisVertices (int mask, GLenum mode, GLsizei count,
	const void *vertices, GLsizei stride, GLint components)
{
	static const unsigned quad_order[6] = { 0, 1, 3, 1, 2, 3 };
	GLsizei i, j;
	const byte *base = vertices;

	if (!vertices || count < 3 || (components != 2 && components != 3) ||
		(!GL_ShowTrisTriangles (mode) && mode != GL_QUADS))
		return;
	if (!stride)
		stride = components * sizeof (GLfloat);
	if (stride < (GLsizei)(components * sizeof (GLfloat)) || !GL_ShowTrisBegin (mask))
		return;
	qglBegin (mode == GL_QUADS ? GL_TRIANGLES : mode == GL_POLYGON ? GL_TRIANGLE_FAN :
																	 mode);
	for (i = 0; i < (mode == GL_QUADS ? count / 4 : count); i++)
	{
		for (j = 0; j < (mode == GL_QUADS ? 6 : 1); j++)
		{
			const GLfloat *v = (const GLfloat *)(base +
				(size_t)(mode == GL_QUADS ? i * 4 + quad_order[j] : i) * stride);
			if (components == 2)
				qglVertex2fv (v);
			else
				qglVertex3fv (v);
		}
	}
	qglEnd ();
	GL_ShowTrisEnd ();
}

/*
==================
R_InitParticleTexture
==================
*/
byte	dottexture[8][8] =
{
	{0,0,0,0,0,0,0,0},
	{0,0,1,1,0,0,0,0},
	{0,1,1,1,1,0,0,0},
	{0,1,1,1,1,0,0,0},
	{0,0,1,1,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
};

/* Q2PRO's generated masks remain separate from a pack's particle image. */
static image_t *r_particle_shapes[3];

image_t *R_ParticleTexture (void)
{
	float value = gl_partshape ? gl_partshape->value : -1;
	int shape;

	if (!(value >= 0 && value <= FLT_MAX))
		return r_particletexture;
	shape = value >= 2 ? 2 : (int)value;
	return r_particle_shapes[shape] ? r_particle_shapes[shape] : r_particletexture;
}

static void R_InitParticleShapes (void)
{
	static const char *names[3] = {
		"***particle_soft***", "***particle_square***", "***particle_round***"
	};
	byte pixels[16][16][4];
	float x, y, f, maximum;
	int shape, i, j;

	memset (r_particle_shapes, 0, sizeof (r_particle_shapes));
	for (shape = 0; shape < 3; shape++)
	{
		memset (pixels, 0, sizeof (pixels));
		for (i = 0; i < 16; i++)
		{
			for (j = 0; j < 16; j++)
			{
				if (shape == 1)
				{
					if (i < 3 || i > 12 || j < 3 || j > 12)
						continue;
					f = 0.6f;
				}
				else
				{
					x = j - 16 / 2 + 0.5f;
					y = i - 16 / 2 + 0.5f;
					f = sqrtf (x * x + y * y);
					f = 1.0f - f / ((16 - shape) / 2 - 0.5f);
					f *= 1 << shape;
					maximum = 1 - shape * 0.2f;
					if (f < 0)
						f = 0;
					if (f > maximum)
						f = maximum;
				}
				pixels[i][j][0] = pixels[i][j][1] = pixels[i][j][2] = 255;
				pixels[i][j][3] = (byte)(255 * f);
			}
		}
		r_particle_shapes[shape] = GL_LoadPic (names[shape], (byte *)pixels,
			16, 16, it_sprite, 32, false);
	}
}

void R_InitParticleTexture (void)
{
	int		x,y;
	int major = 0, minor = 0;
	GLenum wrap = GL_CLAMP;
	byte	data[8][8][4];

	//
	// particle texture
	//
	for (x=0 ; x<8 ; x++)
	{
		for (y=0 ; y<8 ; y++)
		{
			data[y][x][0] = 255;
			data[y][x][1] = 255;
			data[y][x][2] = 255;
			data[y][x][3] = dottexture[x][y]*255;
		}
	}

	r_particletexture = GL_FindImage ("particle.png", "particle", it_sprite);
	
	if (!r_particletexture)
		r_particletexture = GL_FindImage ("particle.tga", "particle", it_sprite);

	if (!r_particletexture)
		r_particletexture = GL_LoadPic ("***particle***", (byte *)data, 8, 8, it_sprite, 32, false);

	/* Centered particle triangles sample beyond 1; repeating duplicates the dot. */
	if (gl_config.version_string)
		sscanf (gl_config.version_string, "%d.%d", &major, &minor);
	if (major > 1 || (major == 1 && minor >= 2) ||
		(gl_config.extensions_string &&
			(strstr (gl_config.extensions_string, "GL_EXT_texture_edge_clamp") ||
				strstr (gl_config.extensions_string, "GL_SGIS_texture_edge_clamp"))))
		wrap = GL_CLAMP_TO_EDGE;
	GL_Bind (r_particletexture->texnum);
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

	//
	// also use this for bad textures, but without alpha
	//
	for (x=0 ; x<8 ; x++)
	{
		for (y=0 ; y<8 ; y++)
		{
			data[y][x][0] = dottexture[x&3][y&3]*255;
			data[y][x][1] = 0; // dottexture[x&3][y&3]*255;
			data[y][x][2] = 0; //dottexture[x&3][y&3]*255;
			data[y][x][3] = 255;
		}
	}
	r_notexture = GL_LoadPic ("***r_notexture***", (byte *)data, 8, 8, it_wall, 32, true);
	GL_InitLightmapWhiteTexture ();
	R_InitParticleShapes ();
}


/* 
============================================================================== 
 
						SCREEN SHOTS 
 
============================================================================== 
*/ 

/*
============
FS_CreatePath

Creates any directories needed to store the given filename
============
*/
#ifndef REF_HARD_LINKED
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

#endif

/* A single managed encoder owns a captured frame until completion or shutdown. */
#define GL_SCREENSHOT_MAX_BYTES (256u * 1024u * 1024u)

typedef enum
{
	GL_SHOT_JPG = 1,
	GL_SHOT_PNG,
	GL_SHOT_TGA
} gl_shot_format_t;

typedef struct
{
	FILE *file;
	byte *pixels;
	int width, height, rowbytes;
	int param, format, error, success;
	char filename[MAX_OSPATH];
} gl_shot_t;

typedef struct
{
	qboolean pending;
	int format, param;
	char name[65];
} gl_shot_request_t;

static gl_shot_request_t gl_shot_request;

#ifdef _WIN32
static HANDLE gl_shot_thread;
static gl_shot_t *gl_shot_pending;
#endif

static qboolean GL_ShotPath (char *buffer, size_t size, const char *format, ...)
{
	va_list args;
	int length;

	va_start (args, format);
	length = Q_vsnprintf (buffer, size, format, args);
	va_end (args);
	if (size)
		buffer[size - 1] = 0;
	return length >= 0 && (size_t)length < size;
}

static qboolean GL_ShotSafeName (const char *name)
{
	size_t i, length;

	if (!name || !(length = strlen (name)) || length > 64)
		return false;
	for (i = 0; i < length; i++)
		if (!((name[i] >= 'a' && name[i] <= 'z') ||
				(name[i] >= 'A' && name[i] <= 'Z') ||
				(name[i] >= '0' && name[i] <= '9') ||
				name[i] == '_' || name[i] == '-'))
			return false;
	return true;
}

static int GL_ShotTemplate (const char *value, char *prefix, size_t size)
{
	size_t length, end, i;
	int width;

	if (!value)
		return 0;
	length = strlen (value);
	end = length;
	while (end && value[end - 1] == 'X')
		end--;
	width = (int)(length - end);
	if (width < 3 || width > 9 || !end || end >= size || end > 64)
		return 0;
	for (i = 0; i < end; i++)
		if (!((value[i] >= 'a' && value[i] <= 'z') ||
				(value[i] >= 'A' && value[i] <= 'Z') ||
				(value[i] >= '0' && value[i] <= '9') ||
				value[i] == '_' || value[i] == '-'))
			return 0;
	memcpy (prefix, value, end);
	prefix[end] = 0;
	return width;
}

static FILE *GL_ShotOpenExclusive (const char *filename)
{
	FILE *file;
	int descriptor;

#ifdef _WIN32
	descriptor = _sopen (filename, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
		_SH_DENYNO, _S_IREAD | _S_IWRITE);
	if (descriptor < 0)
		return NULL;
	file = _fdopen (descriptor, "wb");
	if (!file)
		_close (descriptor);
#else
	descriptor = open (filename, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (descriptor < 0)
		return NULL;
	file = fdopen (descriptor, "wb");
	if (!file)
		close (descriptor);
#endif
	if (!file)
		remove (filename);
	return file;
}

static qboolean GL_ShotCreateFile (gl_shot_t *shot, const char *name)
{
	char directory[MAX_OSPATH], prefix[65];
	const char *extension;
	int width, count, i;

	extension = shot->format == GL_SHOT_JPG ? ".jpg" :
		shot->format == GL_SHOT_PNG			? ".png" :
											  ".tga";
	if (!GL_ShotPath (directory, sizeof (directory), "%s/scrnshot/", ri.FS_Gamedir ()))
		return false;
	FS_CreatePath (directory);

	if (name && *name)
	{
		if (!GL_ShotSafeName (name) ||
			!GL_ShotPath (shot->filename, sizeof (shot->filename),
				"%s%s%s", directory, name, extension))
			return false;
		shot->file = GL_ShotOpenExclusive (shot->filename);
		return shot->file != NULL;
	}

	width = GL_ShotTemplate (gl_screenshot_template->string, prefix, sizeof (prefix));
	if (!width)
	{
		ri.Con_Printf (PRINT_ALL, "Invalid gl_screenshot_template; using quakeXXX.\n");
		strcpy (prefix, "quake");
		width = 3;
	}
	count = 1;
	for (i = 0; i < width; i++)
		count *= 10;
	for (i = 0; i < count; i++)
	{
		if (!GL_ShotPath (shot->filename, sizeof (shot->filename),
				"%s%s%0*d%s", directory, prefix, width, i, extension))
			return false;
		shot->file = GL_ShotOpenExclusive (shot->filename);
		if (shot->file)
			return true;
		if (errno != EEXIST)
			return false;
	}
	return false;
}

typedef struct
{
	struct jpeg_error_mgr pub;
	jmp_buf jump;
} gl_jpeg_error_t;

static void GL_ShotJpegError (j_common_ptr info)
{
	gl_jpeg_error_t *error = (gl_jpeg_error_t *)info->err;
	longjmp (error->jump, 1);
}

static void GL_ShotJpegOutput (j_common_ptr info)
{
	(void)info;
}

static qboolean GL_ShotSaveJPG (const gl_shot_t *shot)
{
	struct jpeg_compress_struct *info;
	gl_jpeg_error_t error;
	JSAMPROW row;
	int y;

	info = calloc (1, sizeof (*info));
	if (!info)
		return false;
	info->err = jpeg_std_error (&error.pub);
	error.pub.error_exit = GL_ShotJpegError;
	error.pub.output_message = GL_ShotJpegOutput;
	if (setjmp (error.jump))
	{
		if (info->mem)
			jpeg_destroy_compress (info);
		free (info);
		return false;
	}
	jpeg_create_compress (info);
	jpeg_stdio_dest (info, shot->file);
	info->image_width = shot->width;
	info->image_height = shot->height;
	info->input_components = 3;
	info->in_color_space = JCS_RGB;
	jpeg_set_defaults (info);
	jpeg_set_quality (info, shot->param, TRUE);
	jpeg_start_compress (info, TRUE);
	for (y = shot->height - 1; y >= 0; y--)
	{
		row = shot->pixels + (size_t)y * shot->rowbytes;
		jpeg_write_scanlines (info, &row, 1);
	}
	jpeg_finish_compress (info);
	jpeg_destroy_compress (info);
	free (info);
	return true;
}

static void GL_ShotPngError (png_structp info, png_const_charp message)
{
	(void)message;
	longjmp (png_jmpbuf (info), 1);
}

static void GL_ShotPngWarning (png_structp info, png_const_charp message)
{
	(void)info;
	(void)message;
}

static qboolean GL_ShotSavePNG (const gl_shot_t *shot)
{
	png_structp info;
	png_infop header;
	int y;

	info = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL,
		GL_ShotPngError, GL_ShotPngWarning);
	if (!info)
		return false;
	header = png_create_info_struct (info);
	if (!header)
	{
		png_destroy_write_struct (&info, NULL);
		return false;
	}
	if (setjmp (png_jmpbuf (info)))
	{
		png_destroy_write_struct (&info, &header);
		return false;
	}
	png_init_io (info, shot->file);
	png_set_IHDR (info, header, shot->width, shot->height, 8,
		PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
	png_set_compression_level (info, shot->param);
	png_write_info (info, header);
	for (y = shot->height - 1; y >= 0; y--)
		png_write_row (info, shot->pixels + (size_t)y * shot->rowbytes);
	png_write_end (info, header);
	png_destroy_write_struct (&info, &header);
	return true;
}

static qboolean GL_ShotSaveTGA (const gl_shot_t *shot)
{
	byte header[18] = { 0 }, *row, *source;
	int x, y;
	qboolean success = true;

	if (shot->width > 65535 || shot->height > 65535)
		return false;
	header[2] = 2;
	header[12] = shot->width & 255;
	header[13] = (shot->width >> 8) & 255;
	header[14] = shot->height & 255;
	header[15] = (shot->height >> 8) & 255;
	header[16] = 24;
	if (fwrite (header, 1, sizeof (header), shot->file) != sizeof (header))
		return false;
	row = malloc (shot->rowbytes);
	if (!row)
		return false;
	for (y = 0; y < shot->height; y++)
	{
		source = shot->pixels + (size_t)y * shot->rowbytes;
		for (x = 0; x < shot->width; x++)
		{
			row[x * 3] = source[x * 3 + 2];
			row[x * 3 + 1] = source[x * 3 + 1];
			row[x * 3 + 2] = source[x * 3];
		}
		if (fwrite (row, 1, shot->rowbytes, shot->file) != (size_t)shot->rowbytes)
		{
			success = false;
			break;
		}
	}
	free (row);
	return success;
}

static void GL_ShotSave (gl_shot_t *shot)
{
	qboolean success;

	errno = 0;
	if (shot->format == GL_SHOT_JPG)
		success = GL_ShotSaveJPG (shot);
	else if (shot->format == GL_SHOT_PNG)
		success = GL_ShotSavePNG (shot);
	else
		success = GL_ShotSaveTGA (shot);
	if (fclose (shot->file))
		success = false;
	shot->file = NULL;
	shot->success = success;
	shot->error = success ? 0 : errno;
	if (!success)
		remove (shot->filename);
}

static void GL_ShotReport (gl_shot_t *shot)
{
	if (shot->success)
		ri.Con_Printf (PRINT_ALL, "Wrote %s\n", shot->filename);
	else
		ri.Con_Printf (PRINT_ALL, "Couldn't write %s%s%s\n", shot->filename,
			shot->error ? ": " : "", shot->error ? strerror (shot->error) : "");
	free (shot->pixels);
	free (shot);
}

#ifdef _WIN32
static unsigned __stdcall GL_ShotWorker (void *argument)
{
	GL_ShotSave ((gl_shot_t *)argument);
	return 0;
}
#endif

void GL_ScreenshotPoll (void)
{
#ifdef _WIN32
	DWORD result;
	gl_shot_t *shot;

	if (!gl_shot_thread)
		return;
	R_PerfMarkCapture ();
	result = WaitForSingleObject (gl_shot_thread, 0);
	if (result != WAIT_OBJECT_0)
		return;
	CloseHandle (gl_shot_thread);
	gl_shot_thread = NULL;
	shot = gl_shot_pending;
	gl_shot_pending = NULL;
	GL_ShotReport (shot);
#endif
}

void GL_ScreenshotShutdown (void)
{
	if (gl_shot_request.pending)
	{
		gl_shot_request.pending = false;
		ri.Con_Printf (PRINT_ALL, "Screenshot request canceled before frame capture.\n");
	}
#ifdef _WIN32
	if (gl_shot_thread)
	{
		if (WaitForSingleObject (gl_shot_thread, INFINITE) != WAIT_OBJECT_0)
			ri.Sys_Error (ERR_FATAL, "Screenshot worker could not be joined");
		GL_ScreenshotPoll ();
	}
#endif
}

static int GL_ShotFormat (const char *value)
{
	if (!Q_stricmp (value, "jpg") || !Q_stricmp (value, "jpeg"))
		return GL_SHOT_JPG;
	if (!Q_stricmp (value, "png"))
		return GL_SHOT_PNG;
	if (!Q_stricmp (value, "tga"))
		return GL_SHOT_TGA;
	return 0;
}

static int GL_ShotClip (int value, int minimum, int maximum)
{
	if (value < minimum)
		return minimum;
	if (value > maximum)
		return maximum;
	return value;
}

static int GL_ShotQuality (void)
{
	int quality = gl_screenshot_quality->intvalue;
	if (quality == 90 && gl_jpg_quality->intvalue != 90)
		quality = gl_jpg_quality->intvalue;
	return GL_ShotClip (quality, 0, 100);
}

static qboolean GL_ShotNumber (const char *text, int minimum, int maximum, int *out)
{
	char *end;
	long value;

	if (!text || !*text)
		return false;
	errno = 0;
	value = strtol (text, &end, 10);
	if (errno == ERANGE || *end)
		return false;
	*out = value < minimum ? minimum : value > maximum ? maximum :
														 (int)value;
	return true;
}

static void GL_ShotTake (int format, const char *name, int param)
{
	GL_ScreenshotPoll ();
#ifdef _WIN32
	if (gl_shot_thread)
	{
		ri.Con_Printf (PRINT_ALL, "Screenshot encoder busy; try again shortly.\n");
		return;
	}
#endif
	if (gl_shot_request.pending)
	{
		ri.Con_Printf (PRINT_ALL, "Screenshot already queued for this frame.\n");
		return;
	}
	if (name && *name && !GL_ShotSafeName (name))
	{
		ri.Con_Printf (PRINT_ALL, "Invalid screenshot name.\n");
		return;
	}
	gl_shot_request.format = format;
	gl_shot_request.param = param;
	if (name && *name)
		strcpy (gl_shot_request.name, name);
	else
		gl_shot_request.name[0] = 0;
	gl_shot_request.pending = true;
}

/* Capture after the current frame's 2D drawing and before buffer swap. */
void GL_ScreenshotCapture (void)
{
	gl_shot_request_t request;
	gl_shot_t *shot;
	size_t bytes;
	GLint alignment, drawbuffer, readbuffer;
	GLenum error;

	if (!gl_shot_request.pending)
		return;
	R_PerfMarkCapture ();
	request = gl_shot_request;
	gl_shot_request.pending = false;
	if (vid.width == 0 || vid.height == 0 || vid.width > INT_MAX / 3 ||
		vid.height > INT_MAX ||
		(size_t)vid.width * 3 > (size_t)-1 / vid.height ||
		(size_t)vid.width * 3 * vid.height > GL_SCREENSHOT_MAX_BYTES)
	{
		ri.Con_Printf (PRINT_ALL, "Screenshot dimensions are too large.\n");
		return;
	}
	if (request.format == GL_SHOT_TGA && (vid.width > 65535 || vid.height > 65535))
	{
		ri.Con_Printf (PRINT_ALL, "TGA screenshot dimensions are too large.\n");
		return;
	}
	shot = calloc (1, sizeof (*shot));
	if (!shot)
	{
		ri.Con_Printf (PRINT_ALL, "Couldn't allocate screenshot.\n");
		return;
	}
	shot->width = vid.width;
	shot->height = vid.height;
	shot->rowbytes = shot->width * 3;
	shot->format = request.format;
	shot->param = request.param;
	bytes = (size_t)shot->rowbytes * shot->height;
	if (!GL_ShotCreateFile (shot, request.name))
	{
		ri.Con_Printf (PRINT_ALL, "Couldn't create screenshot file.\n");
		free (shot);
		return;
	}
	shot->pixels = malloc (bytes);
	if (!shot->pixels)
	{
		ri.Con_Printf (PRINT_ALL, "Couldn't allocate screenshot pixels.\n");
		fclose (shot->file);
		remove (shot->filename);
		free (shot);
		return;
	}
	Draw_Flush ();
	qglGetIntegerv (GL_PACK_ALIGNMENT, &alignment);
	qglGetIntegerv (GL_DRAW_BUFFER, &drawbuffer);
	qglGetIntegerv (GL_READ_BUFFER, &readbuffer);
	qglPixelStorei (GL_PACK_ALIGNMENT, 1);
	qglGetError ();
	qglReadBuffer (drawbuffer);
	error = qglGetError ();
	if (error == GL_NO_ERROR)
	{
		qglReadPixels (0, 0, shot->width, shot->height, GL_RGB,
			GL_UNSIGNED_BYTE, shot->pixels);
		error = qglGetError ();
	}
	qglReadBuffer (readbuffer);
	qglPixelStorei (GL_PACK_ALIGNMENT, alignment);
	if (error != GL_NO_ERROR)
	{
		ri.Con_Printf (PRINT_ALL, "Couldn't read screenshot pixels (GL error 0x%x).\n", error);
		fclose (shot->file);
		remove (shot->filename);
		free (shot->pixels);
		free (shot);
		return;
	}
#ifdef _WIN32
	if (gl_screenshot_async->intvalue > (request.format == GL_SHOT_JPG ? 1 : 0) &&
		request.format != GL_SHOT_TGA)
	{
		gl_shot_pending = shot;
		gl_shot_thread = (HANDLE)_beginthreadex (NULL, 0, GL_ShotWorker,
			shot, 0, NULL);
		if (gl_shot_thread)
		{
			ri.Con_Printf (PRINT_ALL, "Taking screenshot %s\n", shot->filename);
			return;
		}
		gl_shot_pending = NULL;
		ri.Con_Printf (PRINT_ALL, "Screenshot worker unavailable; saving synchronously.\n");
	}
#else
	if (gl_screenshot_async->intvalue > 0)
		ri.Con_Printf (PRINT_ALL, "Asynchronous screenshots are unavailable on this platform; saving synchronously.\n");
#endif
	GL_ShotSave (shot);
	GL_ShotReport (shot);
}

void GL_ScreenShot_f (void)
{
	int format;

	if (ri.Cmd_Argc () > 2)
	{
		ri.Con_Printf (PRINT_ALL, "Usage: screenshot [jpg|png|tga]\n");
		return;
	}
	format = GL_ShotFormat (ri.Cmd_Argc () > 1 ?
			ri.Cmd_Argv (1) :
			gl_screenshot_format->string);
	if (!format)
	{
		ri.Con_Printf (PRINT_ALL, "Invalid screenshot format; use jpg, png or tga.\n");
		return;
	}
	GL_ShotTake (format, NULL, format == GL_SHOT_JPG ? GL_ShotQuality () : format == GL_SHOT_PNG ? GL_ShotClip (gl_screenshot_compression->intvalue, 0, 9) :
																								   0);
}

void GL_ScreenShotJPG_f (void)
{
	int quality = GL_ShotQuality ();
	if (ri.Cmd_Argc () > 3 || (ri.Cmd_Argc () > 2 && !GL_ShotNumber (ri.Cmd_Argv (2), 0, 100, &quality)))
	{
		ri.Con_Printf (PRINT_ALL, "Usage: screenshotjpg [name] [quality]\n");
		return;
	}
	GL_ShotTake (GL_SHOT_JPG, ri.Cmd_Argc () > 1 ? ri.Cmd_Argv (1) : NULL, quality);
}

void GL_ScreenShotPNG_f (void)
{
	int compression = GL_ShotClip (gl_screenshot_compression->intvalue, 0, 9);
	if (ri.Cmd_Argc () > 3 || (ri.Cmd_Argc () > 2 && !GL_ShotNumber (ri.Cmd_Argv (2), 0, 9, &compression)))
	{
		ri.Con_Printf (PRINT_ALL, "Usage: screenshotpng [name] [compression]\n");
		return;
	}
	GL_ShotTake (GL_SHOT_PNG, ri.Cmd_Argc () > 1 ? ri.Cmd_Argv (1) : NULL, compression);
}

void GL_ScreenShotTGA_f (void)
{
	if (ri.Cmd_Argc () > 2)
	{
		ri.Con_Printf (PRINT_ALL, "Usage: screenshottga [name]\n");
		return;
	}
	GL_ShotTake (GL_SHOT_TGA, ri.Cmd_Argc () > 1 ? ri.Cmd_Argv (1) : NULL, 0);
}

#ifdef _DEBUG
void GL_CheckForError (void)
{
	int error;

	error = qglGetError ();

	if (error != GL_NO_ERROR)
		ri.Sys_Error (ERR_FATAL, "qglGetError: %d", error);
}
#else
void GL_CheckForError (void)
{
}
#endif

/*
** GL_Strings_f
*/
void GL_Strings_f( void )
{
	ri.Con_Printf (PRINT_ALL, "GL_VENDOR: %s\n", gl_config.vendor_string );
	ri.Con_Printf (PRINT_ALL, "GL_RENDERER: %s\n", gl_config.renderer_string );
	ri.Con_Printf (PRINT_ALL, "GL_VERSION: %s\n", gl_config.version_string );
	ri.Con_Printf (PRINT_ALL, "GL_EXTENSIONS: %s\n", gl_config.extensions_string );
}

#ifdef R1GL_RELEASE
void GL_Version_f (void)
{
	char buffer[1024];
	snprintf (buffer, sizeof(buffer)-1, "echo Version: "REF_VERSION"\ncmd say \"I'm using "REF_VERSION" (%s/%s) %s | http://r1gl.r1.cx/\"", gl_config.vendor_string, gl_config.renderer_string,
		gl_config.wglPFD ? va("%dc/%dd/%da/%ds [WGL]", (int)gl_colorbits->value, (int)gl_depthbits->value, (int)gl_alphabits->value, (int)gl_stencilbits->value)
		: va("%dc GL", gl_config.bitDepth));
	ri.Cmd_ExecuteText (EXEC_APPEND, buffer);
}
#endif

/*
** GL_SetDefaultState
*/
void GL_SetDefaultState( void )
{
	qglClearColor (1.0f, 0.0f, 0.5f, 0.5f);
	qglCullFace(GL_FRONT);
	qglEnable(GL_TEXTURE_2D);

	qglEnable(GL_ALPHA_TEST);
	qglAlphaFunc(GL_GREATER, 0.666f);

	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_CULL_FACE);
	qglDisable (GL_BLEND);

	qglColor4fv(colorWhite);

	qglPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	qglShadeModel (GL_FLAT);

	GL_TextureMode( gl_texturemode->string );
	GL_TextureAlphaMode( gl_texturealphamode->string );
	GL_TextureSolidMode( gl_texturesolidmode->string );

	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);

	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GL_TexEnv( GL_REPLACE );

	if ( qglPointParameterfEXT && FLOAT_NE_ZERO(gl_ext_pointparameters->value))
	{
		float attenuations[3];

		attenuations[0] = gl_particle_att_a->value;
		attenuations[1] = gl_particle_att_b->value;
		attenuations[2] = gl_particle_att_c->value;

		qglEnable( GL_POINT_SMOOTH );
		qglPointParameterfEXT( GL_POINT_SIZE_MIN_EXT, gl_particle_min_size->value );
		qglPointParameterfEXT( GL_POINT_SIZE_MAX_EXT, gl_particle_max_size->value );
		qglPointParameterfvEXT( GL_DISTANCE_ATTENUATION_EXT, attenuations );
	}

	gl_swapinterval->modified = true;
	GL_UpdateSwapInterval();
}

void GL_UpdateSwapInterval (void)
{
	if (!gl_swapinterval->modified)
		return;
	gl_swapinterval->modified = false;
#ifdef STEREO_SUPPORT
	if (gl_state.stereo_enabled)
		return;
#endif
#if defined(_WIN32) || defined(__linux__)
	{
		double value = gl_swapinterval->value;
		if (!(value >= -2147483647.0 && value <= 2147483647.0))
		{
			ri.Con_Printf (PRINT_ALL, "Invalid gl_swapinterval value; keeping previous interval.\n");
			return;
		}
		GLimp_SwapInterval ((int)value);
	}
#endif
}
