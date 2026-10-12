/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2003-2006 Andrey Nazarov

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

// draw.c

#include "gl_local.h"
#include <limits.h>
#include "gl_jump.h"
#include "../client/ref_present.h"
#ifdef _WIN32
#include "../win32/glw_display.h"
#elif defined(LINUX)
#include "../client/ref_display.h"
const refdisplay_t *SDL2_GetDisplayAPI (void);
#endif

image_t		*draw_chars;

extern	qboolean	scrap_dirty;
void Scrap_Upload (void);

#define	MAX_DRAWCHARS	16384

typedef struct drawchars_s
{
	int		x;
	int		y;
	int		num;
	int shadow;
	float factor;
	image_t *font;
} drawchars_t;

int			defer_drawing;
int			drawcharsindex;
drawchars_t	drawchars[MAX_DRAWCHARS];

/* Q2PRO-style bounded quad submission; retained R1GL state and draw APIs. */
#define DRAW_BATCH_QUADS 1024
#define DRAW_BATCH_TEXTURE 1
#define DRAW_BATCH_ALPHA 2
#define DRAW_BATCH_NOALPHA 4
#define DRAW_BATCH_BLEND 8
#define DRAW_BATCH_HASALPHA 16
#define DRAW_BATCH_REPEAT 32

typedef struct
{
	float vertices[DRAW_BATCH_QUADS * 4][8];
	GLushort indices[DRAW_BATCH_QUADS * 6];
	unsigned quads, texture, flags;
	GLint texenv;
	qboolean alpha_test, blend, textured;
	float color[4];
} drawbatch_t;

static drawbatch_t draw_batch;
static unsigned c_draw_batches, c_draw_quads, c_draw_immediate;
static float draw_factor = 1;
static void EXPORT Draw_RichEnd (void);

static struct
{
	qboolean active;
	float factor;
	image_t *font;
	int fontslot;
	unsigned textures[2], tmu, target;
} draw_rich;

static image_t *draw_font;
static int draw_fontslot = 2;
static void Draw_FontFilter (image_t *font, int slot);
static void Draw_PicSampling (image_t *image);
static cvar_t *gl_fontshadow, *gl_bilerp_chars;
static image_t *draw_fonts[2];
static image_t *draw_rich_font;
static char draw_fontnames[2][MAX_QPATH];
static int draw_fontfilters[4];

static void Draw_Vertex (double x, double y)
{
	qglVertex2f ((float)(x * draw_factor), (float)(y * draw_factor));
}

/* Match Draw_Vertex's presentation scale for immediate picture outlines. */
static void Draw_ShowTrisQuad (double x, double y, double w, double h)
{
	float vertices[4][2];
	if (!GL_ShowTrisEnabled (GL_SHOWTRIS_PIC))
		return;
	vertices[0][0] = vertices[3][0] = (float)(x * draw_factor);
	vertices[1][0] = vertices[2][0] = (float)((x + w) * draw_factor);
	vertices[0][1] = vertices[1][1] = (float)(y * draw_factor);
	vertices[2][1] = vertices[3][1] = (float)((y + h) * draw_factor);
	GL_ShowTrisVertices (GL_SHOWTRIS_PIC, GL_QUADS, 4,
		vertices[0], sizeof (vertices[0]), 2);
}

void Draw_ResetSpace (void)
{
	draw_factor = 1;
	draw_font = draw_chars;
	draw_fontslot = 2;
}

static qboolean Draw_ArraySupport (void)
{
	return qglDrawElements && qglVertexPointer && qglTexCoordPointer &&
		qglColorPointer && qglEnableClientState && qglDisableClientState &&
		qglPushClientAttrib && qglPopClientAttrib && qglPushAttrib &&
		qglPopAttrib && qglGetIntegerv && qglGetFloatv && qglGetTexEnviv &&
		qglIsEnabled;
}

static qboolean Draw_CanBatch (void)
{
#ifdef _DEBUG
	if (FLOAT_NE_ZERO (gl_nobind->value))
		return false;
#endif
	return gl_batch_2d && FLOAT_NE_ZERO (gl_batch_2d->value) &&
		gl_vertex_arrays && FLOAT_NE_ZERO (gl_vertex_arrays->value) &&
		!gl_state.currenttmu && Draw_ArraySupport ();
}

void Draw_Flush (void)
{
	GLint saved_array = 0, saved_elements = 0;
	qboolean arrays, buffers, shader;
	unsigned i;
	float *v;

	if (!draw_batch.quads)
		return;

	arrays = Draw_ArraySupport ();
	buffers = arrays && qglBindBuffer != NULL;
	qglPushAttrib (GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT);
	if (qglSelectTextureSGIS)
		qglSelectTextureSGIS (GL_TEXTURE0);
	else if (qglActiveTextureARB)
		qglActiveTextureARB (GL_TEXTURE0);
	if (draw_batch.textured)
	{
		qglEnable (GL_TEXTURE_2D);
		qglBindTexture (GL_TEXTURE_2D, draw_batch.texture);
		qglTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, draw_batch.texenv);
		if (draw_batch.flags & DRAW_BATCH_REPEAT)
		{
			qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		}
	}
	else
		qglDisable (GL_TEXTURE_2D);
	if (draw_batch.alpha_test)
		qglEnable (GL_ALPHA_TEST);
	else
		qglDisable (GL_ALPHA_TEST);
	if (draw_batch.blend)
		qglEnable (GL_BLEND);
	else
		qglDisable (GL_BLEND);

	shader = GL_BeginSurfaceShader (draw_batch.textured,
		!!(draw_batch.flags & DRAW_BATCH_HASALPHA), (GLenum)draw_batch.texenv, 0, 0, 1);
	if (arrays)
	{
		qglPushClientAttrib (GL_CLIENT_VERTEX_ARRAY_BIT);
		if (buffers)
		{
			qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_array);
			qglGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &saved_elements);
			qglBindBuffer (GL_ARRAY_BUFFER, 0);
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		qglDisableClientState (GL_NORMAL_ARRAY);
		qglDisableClientState (GL_INDEX_ARRAY);
		qglDisableClientState (GL_EDGE_FLAG_ARRAY);
		if (qglClientActiveTextureARB)
		{
			qglClientActiveTextureARB (GL_TEXTURE1);
			qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
			qglClientActiveTextureARB (GL_TEXTURE0);
		}
		qglEnableClientState (GL_VERTEX_ARRAY);
		qglVertexPointer (2, GL_FLOAT, sizeof (draw_batch.vertices[0]), draw_batch.vertices[0]);
		qglEnableClientState (GL_COLOR_ARRAY);
		qglColorPointer (4, GL_FLOAT, sizeof (draw_batch.vertices[0]), &draw_batch.vertices[0][4]);
		if (draw_batch.flags & DRAW_BATCH_TEXTURE)
		{
			qglEnableClientState (GL_TEXTURE_COORD_ARRAY);
			qglTexCoordPointer (2, GL_FLOAT, sizeof (draw_batch.vertices[0]), &draw_batch.vertices[0][2]);
		}
		else
			qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
		qglDrawElements (GL_TRIANGLES, draw_batch.quads * 6, GL_UNSIGNED_SHORT, draw_batch.indices);
		GL_ShowTrisElements (GL_SHOWTRIS_PIC, GL_TRIANGLES,
			draw_batch.quads * 6, GL_UNSIGNED_SHORT, draw_batch.indices);
		qglPopClientAttrib ();
		if (buffers)
		{
			qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, (GLuint)saved_elements);
		}
		c_draw_batches++;
	}
	else
	{
		qglBegin (GL_QUADS);
		for (i = 0; i < draw_batch.quads * 4; i++)
		{
			v = draw_batch.vertices[i];
			qglColor4fv (v + 4);
			if (draw_batch.flags & DRAW_BATCH_TEXTURE)
				qglTexCoord2f (v[2], v[3]);
			qglVertex2f (v[0], v[1]);
		}
		qglEnd ();
		GL_ShowTrisVertices (GL_SHOWTRIS_PIC, GL_QUADS, draw_batch.quads * 4,
			draw_batch.vertices[0], sizeof (draw_batch.vertices[0]), 2);
		c_draw_immediate++;
	}
	if (shader)
		GL_EndSurfaceShader ();
	if (draw_batch.textured && (draw_batch.flags & DRAW_BATCH_REPEAT))
	{
		if (qglSelectTextureSGIS)
			qglSelectTextureSGIS (GL_TEXTURE0);
		else if (qglActiveTextureARB)
			qglActiveTextureARB (GL_TEXTURE0);
		qglBindTexture (GL_TEXTURE_2D, draw_batch.texture);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_ImageClampMode ());
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_ImageClampMode ());
	}
	c_draw_quads += draw_batch.quads;
	draw_batch.quads = 0;
	qglPopAttrib ();
	/* Keep the engine's texture cache unchanged after raw temporary binds. */
	if (qglSelectTextureSGIS)
		qglSelectTextureSGIS (gl_state.currenttmu ? GL_TEXTURE1 : GL_TEXTURE0);
	else if (qglActiveTextureARB)
		qglActiveTextureARB (gl_state.currenttmu ? GL_TEXTURE1 : GL_TEXTURE0);
	GL_CheckForError ();
}

static qboolean Draw_BatchQuad (double x, double y, double w, double h,
	float sl, float tl, float sh, float th, unsigned texture,
	unsigned flags, const float *color)
{
	unsigned first, i;
	float *v;
	GLushort *indices;

	if (!Draw_CanBatch ())
	{
		Draw_Flush ();
		return false;
	}
	if (draw_batch.quads && (draw_batch.texture != texture || draw_batch.flags != flags || draw_batch.quads == DRAW_BATCH_QUADS))
		Draw_Flush ();
	if (!draw_batch.quads)
	{
		draw_batch.texture = texture;
		draw_batch.flags = flags;
		draw_batch.textured = (flags & DRAW_BATCH_TEXTURE) && qglIsEnabled (GL_TEXTURE_2D);
		draw_batch.alpha_test = !(flags & (DRAW_BATCH_ALPHA | DRAW_BATCH_NOALPHA)) &&
			qglIsEnabled (GL_ALPHA_TEST);
		draw_batch.blend = (flags & (DRAW_BATCH_ALPHA | DRAW_BATCH_BLEND)) ||
			qglIsEnabled (GL_BLEND);
		if (flags & DRAW_BATCH_ALPHA)
			draw_batch.texenv = GL_MODULATE;
		else
			qglGetTexEnviv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &draw_batch.texenv);
		qglGetFloatv (GL_CURRENT_COLOR, draw_batch.color);
	}
	if (!color)
		color = draw_batch.color;
	first = draw_batch.quads * 4;
	for (i = 0; i < 4; i++)
	{
		v = draw_batch.vertices[first + i];
		v[0] = (float)((x + ((i == 1 || i == 2) ? w : 0)) * draw_factor);
		v[1] = (float)((y + ((i >= 2) ? h : 0)) * draw_factor);
		v[2] = (i == 1 || i == 2) ? sh : sl;
		v[3] = (i >= 2) ? th : tl;
		memcpy (v + 4, color, sizeof (draw_batch.color));
	}
	indices = draw_batch.indices + draw_batch.quads * 6;
	indices[0] = (GLushort)first;
	indices[1] = (GLushort)(first + 1);
	indices[2] = (GLushort)(first + 2);
	indices[3] = (GLushort)first;
	indices[4] = (GLushort)(first + 2);
	indices[5] = (GLushort)(first + 3);
	draw_batch.quads++;
	return true;
}

static void Draw_PicStateEnd (qboolean alpha, qboolean opaque_workaround)
{
	if (alpha)
	{
		GL_TexEnv (GL_REPLACE);
		qglEnable (GL_ALPHA_TEST);
		qglDisable (GL_BLEND);
	}
	else if (opaque_workaround)
		qglEnable (GL_ALPHA_TEST);
}

static qboolean Draw_BatchPic (image_t *image, int x, int y, int w, int h,
	qboolean tile)
{
	qboolean workaround = ((gl_config.renderer == GL_RENDERER_MCD) ||
							  (gl_config.renderer & GL_RENDERER_RENDITION)) &&
		!image->has_alpha;
	unsigned flags = DRAW_BATCH_TEXTURE | (image->has_alpha ? DRAW_BATCH_HASALPHA : 0);
	float sl, tl, sh, th;

	Draw_PicSampling (image);
	if (tile && image->type == it_pic)
		flags |= DRAW_BATCH_REPEAT;
	if (!tile && image->has_alpha)
		flags |= DRAW_BATCH_ALPHA;
	else if (workaround)
		flags |= DRAW_BATCH_NOALPHA;
	if (tile)
	{
		sl = x / 64.0f;
		tl = y / 64.0f;
		sh = ((float)x + w) / 64.0f;
		th = ((float)y + h) / 64.0f;
	}
	else
	{
		sl = image->sl;
		tl = image->tl;
		sh = image->sh;
		th = image->th;
	}
	if (!Draw_BatchQuad (x, y, w, h, sl, tl, sh, th, image->texnum, flags, NULL))
		return false;
	Draw_PicStateEnd (!tile && image->has_alpha, workaround);
	return true;
}

void Draw_Reset (void)
{
	GL_JumpReset ();
	Draw_RichEnd ();
	draw_batch.quads = 0;
	drawcharsindex = 0;
	draw_chars = draw_font = NULL;
	draw_rich_font = NULL;
	draw_factor = 1;
	memset (draw_fonts, 0, sizeof (draw_fonts));
	memset (draw_fontnames, 0, sizeof (draw_fontnames));
	memset (draw_fontfilters, -1, sizeof (draw_fontfilters));
	c_draw_batches = c_draw_quads = c_draw_immediate = 0;
}

void Draw_BeginFrame (void)
{
	Draw_RichEnd ();
	Draw_Flush ();
	Draw_ResetSpace ();
	c_draw_batches = c_draw_quads = c_draw_immediate = 0;
}

void Draw_PrintStats (void)
{
	ri.Con_Printf (PRINT_ALL, "%u 2D batches, %u quads, %u immediate calls\n",
		c_draw_batches, c_draw_quads, c_draw_immediate);
}

/*
===============
Draw_InitLocal
===============
*/
void Draw_InitLocal (void)
{
	Draw_Reset ();
	gl_fontshadow = ri.Cvar_Get ("gl_fontshadow", "0", CVAR_ARCHIVE);
	gl_bilerp_chars = ri.Cvar_Get ("gl_bilerp_chars", "0", CVAR_ARCHIVE);
	// load console characters (don't bilerp characters)
	draw_chars = GL_FindImage ("pics/conchars.pcx", "pics/conchars.pcx", it_pic);
	if (!draw_chars)
		ri.Sys_Error (ERR_FATAL, "R1GL: Couldn't load conchars.pcx\n\nEither you aren't running Quake 2 from the correct directory or you are missing important files.");
	GL_Bind( draw_chars->texnum );
	qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLfloat)(gl_bilerp_chars->value ? GL_LINEAR : GL_NEAREST));
	qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLfloat)(gl_bilerp_chars->value ? GL_LINEAR : GL_NEAREST));
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_ImageClampMode ());
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_ImageClampMode ());
	draw_fontfilters[2] = gl_bilerp_chars->value != 0;
	Draw_ResetSpace ();
}

static float conchars_texoffset[16] =
{
	0, 0.0625, 0.125, 0.1875, 0.25, 0.3125, 0.375, 0.4375, 0.5, 0.5625, 0.625, 0.6875, 0.75, 0.8125, 0.875, 0.9375
};

static float conchars_texlimits[16] =
{
	0.0625, 0.125, 0.1875, 0.25, 0.3125, 0.375, 0.4375, 0.5, 0.5625, 0.625, 0.6875, 0.75, 0.8125, 0.875, 0.9375, 1
};

static void Draw_Glyph (int x, int y, int num);

static void Draw_StyledQuad (double x, double y, double w, double h,
	float sl, float tl, float sh, float th, image_t *image, const float *rgba)
{
	if (Draw_BatchQuad (x, y, w, h, sl, tl, sh, th, image->texnum,
			DRAW_BATCH_TEXTURE | DRAW_BATCH_ALPHA | (image->has_alpha ? DRAW_BATCH_HASALPHA : 0), rgba))
		return;
	Draw_Flush ();
	qglPushAttrib (GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT);
	qglEnable (GL_TEXTURE_2D);
	qglDisable (GL_ALPHA_TEST);
	qglEnable (GL_BLEND);
	qglBindTexture (GL_TEXTURE_2D, image->texnum);
	qglTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	qglColor4fv (rgba);
	qglBegin (GL_QUADS);
	qglTexCoord2f (sl, tl);
	Draw_Vertex (x, y);
	qglTexCoord2f (sh, tl);
	Draw_Vertex (x + w, y);
	qglTexCoord2f (sh, th);
	Draw_Vertex (x + w, y + h);
	qglTexCoord2f (sl, th);
	Draw_Vertex (x, y + h);
	qglEnd ();
	Draw_ShowTrisQuad (x, y, w, h);
	qglPopAttrib ();
	c_draw_immediate++;
	c_draw_quads++;
}

static void Draw_Shadow (int x, int y, int num, int shadow)
{
	float black[4];
	int i, row = num >> 4, col = num & 15;
	if (!shadow || num == 0x83)
		return;
	qglGetFloatv (GL_CURRENT_COLOR, black);
	black[0] = black[1] = black[2] = 0;
	for (i = 1; i <= shadow; i++)
		Draw_StyledQuad (x + i, y + i, 8, 8, conchars_texoffset[col],
			conchars_texoffset[row], conchars_texlimits[col],
			conchars_texlimits[row], draw_font, black);
}

void Draw_AddText (void)
{
	int i, x, y, num, row, col, first, last;
	float frow, fcol, frowbottom, fcolbottom;
	float saved_factor = draw_factor;
	image_t *saved_font = draw_font;

	if (!drawcharsindex)
		return;
	Draw_Flush ();
	for (first = 0; first < drawcharsindex; first = last)
	{
		draw_font = drawchars[first].font;
		draw_factor = drawchars[first].factor;
		for (last = first + 1; last < drawcharsindex; last++)
			if (drawchars[last].font != draw_font || drawchars[last].factor != draw_factor ||
				drawchars[last].shadow != drawchars[first].shadow)
				break;
		if (Draw_CanBatch () || drawchars[first].shadow)
		{
			for (i = first; i < last; i++)
			{
				Draw_Shadow (drawchars[i].x, drawchars[i].y, drawchars[i].num, drawchars[i].shadow);
				Draw_Glyph (drawchars[i].x, drawchars[i].y, drawchars[i].num);
			}
			Draw_Flush ();
			continue;
		}
		if (draw_font->has_alpha)
		{
			qglDisable (GL_ALPHA_TEST);
			GL_CheckForError ();

			qglEnable (GL_BLEND);
			GL_CheckForError ();

			GL_TexEnv (GL_MODULATE);
		}

		GL_Bind (draw_font->texnum);
		c_draw_immediate++;
		c_draw_quads += last - first;
		qglBegin (GL_QUADS);

		for (i = first; i < last; i++)
		{
			num = drawchars[i].num;
			x = drawchars[i].x;
			y = drawchars[i].y;

			row = num >> 4;
			col = num & 15;

			frow = conchars_texoffset[row];
			fcol = conchars_texoffset[col];

			frowbottom = conchars_texlimits[row];
			fcolbottom = conchars_texlimits[col];

			qglTexCoord2f (fcol, frow);
			Draw_Vertex (x, y);
			qglTexCoord2f (fcolbottom, frow);
			Draw_Vertex (x + 8, y);
			qglTexCoord2f (fcolbottom, frowbottom);
			Draw_Vertex (x + 8, y + 8);
			qglTexCoord2f (fcol, frowbottom);
			Draw_Vertex (x, y + 8);
		}

		qglEnd ();
		if (GL_ShowTrisEnabled (GL_SHOWTRIS_PIC))
			for (i = first; i < last; i++)
				Draw_ShowTrisQuad (drawchars[i].x, drawchars[i].y, 8, 8);
		GL_CheckForError ();

		if (draw_font->has_alpha)
		{
			GL_TexEnv (GL_REPLACE);

			qglEnable (GL_ALPHA_TEST);
			GL_CheckForError ();

			qglDisable (GL_BLEND);
			GL_CheckForError ();
		}
	}
	draw_factor = saved_factor;
	draw_font = saved_font;
	drawcharsindex = 0;
}

/*
================
Draw_Char

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
static void Draw_Glyph (int x, int y, int num)
{
	int				row, col;
	float			frow, fcol, frowbottom, fcolbottom;

	num &= 0xFF;

	if ( (num&127) == 32 )
		return;		// space

	//if (y <= -8)
	//	return;			// totally off screen

	row = num>>4;
	col = num&15;

	frow = conchars_texoffset[row];
	fcol = conchars_texoffset[col];

	frowbottom = conchars_texlimits[row];
	fcolbottom = conchars_texlimits[col];

	if (Draw_BatchQuad (x, y, 8, 8, fcol, frow, fcolbottom, frowbottom,
			draw_font->texnum, DRAW_BATCH_TEXTURE | (draw_font->has_alpha ? DRAW_BATCH_ALPHA | DRAW_BATCH_HASALPHA : 0), NULL))
	{
		Draw_PicStateEnd (draw_font->has_alpha, false);
		return;
	}
	c_draw_immediate++;
	c_draw_quads++;
	GL_Bind (draw_font->texnum);

	if (draw_font->has_alpha)
	{
		qglDisable(GL_ALPHA_TEST);
		GL_CheckForError ();

		qglEnable(GL_BLEND);
		GL_CheckForError ();

		GL_TexEnv(GL_MODULATE);
	}

	qglBegin (GL_QUADS);
	qglTexCoord2f (fcol, frow);
	Draw_Vertex (x, y);
	qglTexCoord2f (fcolbottom, frow);
	Draw_Vertex (x + 8, y);
	qglTexCoord2f (fcolbottom, frowbottom);
	Draw_Vertex (x + 8, y + 8);
	qglTexCoord2f (fcol, frowbottom);
	Draw_Vertex (x, y + 8);
	qglEnd ();
	Draw_ShowTrisQuad (x, y, 8, 8);
	GL_CheckForError ();

	if (draw_font->has_alpha)
	{
		GL_TexEnv (GL_REPLACE);
		
		qglEnable(GL_ALPHA_TEST);
		GL_CheckForError ();

		qglDisable(GL_BLEND);
		GL_CheckForError ();
	}
}

void EXPORT Draw_Char (int x, int y, int num)
{
	float value = gl_fontshadow ? gl_fontshadow->value : 0;
	int shadow = value >= 2 ? 2 : value >= 1 ? 1 :
											   0;
	num &= 255;
	if ((num & 127) == 32)
		return;
	if (shadow < 0)
		shadow = 0;
	if (shadow > 2)
		shadow = 2;
	if (!draw_font)
		draw_font = draw_chars;
	if (gl_bilerp_chars)
		Draw_FontFilter (draw_font, draw_fontslot);
	if (defer_drawing)
	{
		drawchars[drawcharsindex].x = x;
		drawchars[drawcharsindex].y = y;
		drawchars[drawcharsindex].num = num;
		drawchars[drawcharsindex].shadow = shadow;
		drawchars[drawcharsindex].factor = draw_factor;
		drawchars[drawcharsindex].font = draw_font;
		if (++drawcharsindex == MAX_DRAWCHARS)
			ri.Sys_Error (ERR_FATAL, "drawcharsindex == MAX_DRAWCHARS");
		return;
	}
	Draw_Shadow (x, y, num, shadow);
	Draw_Glyph (x, y, num);
}

/*
=============
Draw_FindPic
=============
*/
image_t	* EXPORT Draw_FindPic (char *name)
{
	image_t *gl;
	char	fullname[MAX_QPATH];

	fast_strlwr (name);

	if (name[0] != '/' && name[0] != '\\')
	{
		Com_sprintf (fullname, sizeof(fullname), "pics/%s.pcx", name);
		gl = GL_FindImage (fullname, name, it_pic);
	}
	else
		gl = GL_FindImage (name+1, name+1, it_pic);

	return gl;
}

static image_t *Draw_FindFont (char *name)
{
	char fullname[MAX_QPATH];

	fast_strlwr (name);
	if (!strcmp (name, "conchars"))
		return draw_chars;
	if (name[0] != '/' && name[0] != '\\')
	{
		Com_sprintf (fullname, sizeof (fullname), "pics/%s.pcx", name);
		return GL_FindFontImage (fullname);
	}
	return GL_FindFontImage (name + 1);
}

/*
=============
Draw_GetPicSize
=============
*/
void EXPORT Draw_GetPicSize (int *w, int *h, char *pic)
{
	image_t *gl;

	gl = Draw_FindPic (pic);
	if (!gl)
	{
		*w = *h = -1;
		return;
	}

	*w = gl->width;
	*h = gl->height;
}

/* Scale/font/colored-picture extension; no additions to refexport_t. */
static float EXPORT Draw_PresentationSize (float scale, int *width, int *height)
{
	float limit = vid.width / 64.0f;
	if (vid.height / 48.0f < limit)
		limit = vid.height / 48.0f;
	if (limit > 10)
		limit = 10;
	if (scale == -1)
		scale = gl_hudscale->value;
	else if (scale == 0)
	{
#ifdef WIN32
		int dpi_scale = GLimp_GetDpiScale ();
#endif
		/* Q2PRO drawable thresholds with the native per-window DPI minimum. */
		if (vid.height < vid.width)
			scale = vid.height >= 2160 ? 4.0f : vid.height >= 1080 ? 2.0f :
																	 1.0f;
		else
			scale = vid.width >= 3840 ? 4.0f : vid.width >= 1920 ? 2.0f :
																   1.0f;
#ifdef WIN32
		if (dpi_scale > scale)
			scale = (float)dpi_scale;
#endif
	}
	if (!(scale >= 1))
		scale = 1;
	if (scale > limit && scale != gl_hudscale->value)
		scale = limit;
	if (!(scale >= 1))
		scale = 1;
	*width = ((int)ceil (vid.width / (double)scale) + 7) & ~7;
	*height = ((int)ceil (vid.height / (double)scale) + 1) & ~1;
	return scale;
}

void Draw_InvalidateImageFilters (void)
{
	memset (draw_fontfilters, -1, sizeof (draw_fontfilters));
}

/* A registered picture may also be a font atlas. -2 records picture sampling;
 * font draws recover their own control at the next text submission. */
static void Draw_PicSampling (image_t *image)
{
	int i, filter;
	qboolean shared = image == draw_chars || image == draw_rich_font;
	for (i = 0; i < 2; i++)
		if (image == draw_fonts[i])
			shared = true;
	if (!shared)
		return;
	for (i = 0; i < 2; i++)
		if (image == draw_fonts[i] && draw_fontfilters[i] != -2)
			break;
	if (i == 2 && (image != draw_chars || draw_fontfilters[2] == -2) &&
		(image != draw_rich_font || draw_fontfilters[3] == -2))
		return;
	Draw_Flush ();
	filter = GL_PicFilter (image);
	GL_Bind (image->texnum);
	qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLfloat)GL_ImageMinFilter (image, filter));
	qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLfloat)filter);
	for (i = 0; i < 2; i++)
		if (image == draw_fonts[i])
			draw_fontfilters[i] = -2;
	if (image == draw_chars)
		draw_fontfilters[2] = -2;
	if (image == draw_rich_font)
		draw_fontfilters[3] = -2;
}

static void Draw_FontFilter (image_t *font, int slot)
{
	int i, filter = gl_bilerp_chars && gl_bilerp_chars->value != 0;
	if (draw_fontfilters[slot] == filter)
		return;
	Draw_Flush ();
	GL_Bind (font->texnum);
	qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLfloat)GL_ImageMinFilter (font, filter ? GL_LINEAR : GL_NEAREST));
	qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLfloat)(filter ? GL_LINEAR : GL_NEAREST));
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_ImageClampMode ());
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_ImageClampMode ());
	/* One image may serve both fonts and the base console atlas. */
	for (i = 0; i < 2; i++)
		if (draw_fonts[i] == font)
			draw_fontfilters[i] = filter;
	if (draw_chars == font)
		draw_fontfilters[2] = filter;
	if (draw_rich_font == font)
		draw_fontfilters[3] = filter;
	draw_fontfilters[slot] = filter;
}

static void EXPORT Draw_PresentationSpace (int space, float scale, const char *font)
{
	char name[MAX_QPATH];
	int width, height, slot;
	image_t *image;
	if (space != PRESENT_HUD && space != PRESENT_CONSOLE)
	{
		Draw_ResetSpace ();
		if (draw_chars)
			Draw_FontFilter (draw_chars, 2);
		return;
	}
	draw_factor = Draw_PresentationSize (scale, &width, &height) / gl_hudscale->value;
	slot = space - 1;
	if (!font || !font[0])
		font = "conchars";
	Com_sprintf (name, sizeof (name), "%s", font);
	if (!draw_fonts[slot] || strcmp (name, draw_fontnames[slot]))
	{
		image = strlen (font) < sizeof (name) ? Draw_FindFont (name) : NULL;
		if (!image || image->width < 128 || image->height < 128 ||
			(image->width & 15) || (image->height & 15))
		{
			ri.Con_Printf (PRINT_ALL, "Font %s is missing or not a 16x16 atlas; using conchars.\n", font);
			image = draw_chars;
		}
		draw_fonts[slot] = image;
		Com_sprintf (draw_fontnames[slot], sizeof (draw_fontnames[slot]), "%s", font);
		draw_fontfilters[slot] = -1;
	}
	draw_font = draw_fonts[slot];
	draw_fontslot = slot;
	Draw_FontFilter (draw_font, slot);
}

static void EXPORT Draw_PresentationPic (int x, int y, int w, int h,
	const char *pic, const float *rgba)
{
	char name[MAX_QPATH];
	image_t *image;
	float color[4];
	int i;
	if (!pic || !rgba || strlen (pic) >= sizeof (name))
		return;
	Com_sprintf (name, sizeof (name), "%s", pic);
	image = Draw_FindPic (name);
	if (!image)
		return;
	if (scrap_dirty)
		Scrap_Upload ();
	for (i = 0; i < 4; i++)
	{
		color[i] = rgba[i];
		if (!(color[i] >= 0))
			color[i] = 0;
		if (color[i] > 1)
			color[i] = 1;
	}
	Draw_PicSampling (image);
	Draw_StyledQuad (x, y, w, h, image->sl, image->tl, image->sh, image->th, image, color);
}

/* Rich HUD drawing is isolated from the global/console presentation spaces. */
static qboolean Draw_RichRect (float x, float y, float w, float h)
{
	return x >= -1048576 && x <= 1048576 && y >= -1048576 && y <= 1048576 &&
		w > 0 && w <= 65536 && h > 0 && h <= 65536;
}

static qboolean Draw_RichColor (const float *rgba, float *color)
{
	int i;
	if (!rgba)
		return false;
	for (i = 0; i < 4; i++)
	{
		color[i] = rgba[i];
		if (!(color[i] >= 0))
			color[i] = 0;
		if (color[i] > 1)
			color[i] = 1;
	}
	return true;
}

static void EXPORT Draw_RichBegin (void)
{
	if (draw_rich.active || !draw_chars || !vid.width || !vid.height)
		return;
	Draw_AddText ();
	Draw_Flush ();
	draw_rich.factor = draw_factor;
	draw_rich.font = draw_font;
	draw_rich.fontslot = draw_fontslot;
	memcpy (draw_rich.textures, gl_state.currenttextures, sizeof (draw_rich.textures));
	draw_rich.tmu = gl_state.currenttmu;
	draw_rich.target = gl_state.currenttarget;
	qglPushAttrib (GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT |
		GL_SCISSOR_BIT | GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT);
	qglMatrixMode (GL_PROJECTION);
	qglPushMatrix ();
	qglLoadIdentity ();
	qglOrtho (0, vid.width, vid.height, 0, -99999, 99999);
	qglMatrixMode (GL_MODELVIEW);
	qglPushMatrix ();
	qglLoadIdentity ();
	qglViewport (0, 0, vid.width, vid.height);
	if (qglSelectTextureSGIS)
		qglSelectTextureSGIS (GL_TEXTURE1);
	else if (qglActiveTextureARB)
		qglActiveTextureARB (GL_TEXTURE1);
	if (qglSelectTextureSGIS || qglActiveTextureARB)
		qglDisable (GL_TEXTURE_2D);
	if (qglSelectTextureSGIS)
		qglSelectTextureSGIS (GL_TEXTURE0);
	else if (qglActiveTextureARB)
		qglActiveTextureARB (GL_TEXTURE0);
	gl_state.currenttmu = 0;
	gl_state.currenttarget = GL_TEXTURE0;
	qglEnable (GL_TEXTURE_2D);
	qglDisable (GL_SCISSOR_TEST);
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_CULL_FACE);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	draw_factor = 1;
	draw_rich.active = true;
}

static void EXPORT Draw_RichEnd (void)
{
	if (!draw_rich.active)
		return;
	Draw_Flush ();
	qglMatrixMode (GL_MODELVIEW);
	qglPopMatrix ();
	qglMatrixMode (GL_PROJECTION);
	qglPopMatrix ();
	qglPopAttrib ();
	memcpy (gl_state.currenttextures, draw_rich.textures, sizeof (draw_rich.textures));
	gl_state.currenttmu = draw_rich.tmu;
	gl_state.currenttarget = draw_rich.target;
	draw_factor = draw_rich.factor;
	draw_font = draw_rich.font;
	draw_fontslot = draw_rich.fontslot;
	draw_rich.active = false;
	draw_rich_font = NULL;
	draw_fontfilters[3] = -1;
}

static void EXPORT Draw_RichClip (int x, int y, int w, int h)
{
	double left, top, right, bottom;
	if (!draw_rich.active)
		return;
	Draw_Flush ();
	if (w <= 0 || h <= 0)
	{
		qglDisable (GL_SCISSOR_TEST);
		return;
	}
	left = x < 0 ? 0 : x > vid.width ? vid.width :
									   x;
	top = y < 0 ? 0 : y > vid.height ? vid.height :
									   y;
	right = (double)x + w;
	bottom = (double)y + h;
	if (right > vid.width)
		right = vid.width;
	if (bottom > vid.height)
		bottom = vid.height;
	if (right < left)
		right = left;
	if (bottom < top)
		bottom = top;
	qglEnable (GL_SCISSOR_TEST);
	qglScissor ((int)left, vid.height - (int)bottom, (int)(right - left), (int)(bottom - top));
}

static void EXPORT Draw_RichGlyph (float x, float y, float w, float h, int c,
	const char *font, const float *rgba)
{
	char name[MAX_QPATH];
	image_t *image;
	float color[4];
	int row, col;
	if (!draw_rich.active || !Draw_RichRect (x, y, w, h) || !Draw_RichColor (rgba, color))
		return;
	c &= 255;
	if ((c & 127) == 32)
		return;
	image = draw_chars;
	if (font && *font && strlen (font) < sizeof (name))
	{
		Com_sprintf (name, sizeof (name), "%s", font);
		image = Draw_FindFont (name);
		if (!image || image->width < 128 || image->height < 128 || (image->width & 15) || (image->height & 15))
			image = draw_chars;
	}
	if (scrap_dirty)
		Scrap_Upload ();
	if (image != draw_rich_font)
	{
		draw_rich_font = image;
		draw_fontfilters[3] = -1;
	}
	Draw_FontFilter (image, 3);
	row = c >> 4;
	col = c & 15;
	Draw_StyledQuad (x, y, w, h, conchars_texoffset[col], conchars_texoffset[row],
		conchars_texlimits[col], conchars_texlimits[row], image, color);
}

static void EXPORT Draw_RichPic (float x, float y, float w, float h,
	const char *pic, const float *rgba)
{
	char name[MAX_QPATH];
	image_t *image;
	float color[4];
	if (!draw_rich.active || !Draw_RichRect (x, y, w, h) || !Draw_RichColor (rgba, color) ||
		!pic || strlen (pic) >= sizeof (name))
		return;
	Com_sprintf (name, sizeof (name), "%s", pic);
	image = Draw_FindPic (name);
	if (!image)
		return;
	if (scrap_dirty)
		Scrap_Upload ();
	Draw_PicSampling (image);
	Draw_StyledQuad (x, y, w, h, image->sl, image->tl, image->sh, image->th, image, color);
}

static void EXPORT Draw_RichFill (float x, float y, float w, float h, const float *rgba)
{
	float color[4];
	if (!draw_rich.active || !Draw_RichRect (x, y, w, h) || !Draw_RichColor (rgba, color))
		return;
	if (Draw_BatchQuad (x, y, w, h, 0, 0, 0, 0, 0,
			DRAW_BATCH_NOALPHA | DRAW_BATCH_BLEND, color))
		return;
	qglPushAttrib (GL_ENABLE_BIT | GL_CURRENT_BIT);
	qglDisable (GL_TEXTURE_2D);
	qglDisable (GL_ALPHA_TEST);
	qglEnable (GL_BLEND);
	qglColor4fv (color);
	qglBegin (GL_QUADS);
	Draw_Vertex (x, y);
	Draw_Vertex (x + w, y);
	Draw_Vertex (x + w, y + h);
	Draw_Vertex (x, y + h);
	qglEnd ();
	Draw_ShowTrisQuad (x, y, w, h);
	qglPopAttrib ();
	c_draw_immediate++;
	c_draw_quads++;
}

static unsigned int EXPORT Draw_RichColor8 (int index)
{
	return index >= 0 && index <= 255 ? d_8to24table[index] | 0xff000000u : 0xffffffffu;
}

const refpresent_t *EXPORT GetPresentationAPI (int version)
{
	static const refpresent_t api = {
		REF_PRESENT_VERSION, sizeof (refpresent_t),
		Draw_PresentationSize, Draw_PresentationSpace, Draw_PresentationPic
	};
	static const refpresent2_t rich_api = {
		REF_PRESENT_VERSION2, sizeof (refpresent2_t),
		Draw_RichBegin, Draw_RichEnd, Draw_RichGlyph, Draw_RichPic, Draw_RichFill, Draw_RichClip, Draw_RichColor8
	};
	static const refpresent3_t beam_api = {
		REF_PRESENT_VERSION3, sizeof (refpresent3_t), GL_JumpSetBeams
	};
	static const refpresent5_t color_api = {
		REF_PRESENT_VERSION5, sizeof (refpresent5_t), Draw_UpdateColorFrame, Draw_ColorFrame
	};
#ifdef _WIN32
	if (version == REF_DISPLAY_VERSION)
		return (const refpresent_t *)GLW_GetDisplayAPI ();
#elif defined(LINUX)
	if (version == REF_DISPLAY_VERSION)
		return (const refpresent_t *)SDL2_GetDisplayAPI ();
#endif
	if (version == REF_PRESENT_VERSION5)
		return (const refpresent_t *)&color_api;
	if (version == REF_PRESENT_VERSION3)
		return (const refpresent_t *)&beam_api;
	if (version == REF_PRESENT_VERSION2)
		return (const refpresent_t *)&rich_api;
	return version == REF_PRESENT_VERSION ? &api : NULL;
}

/*
=============
Draw_StretchPic
=============
*/
void EXPORT Draw_StretchPic (int x, int y, int w, int h, char *pic)
{
	image_t *gl;

	gl = Draw_FindPic (pic);
	if (!gl)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "Can't find pic: %s\n", pic);
		gl = r_notexture;
	}

	if (scrap_dirty)
		Scrap_Upload ();

	if (Draw_BatchPic (gl, x, y, w, h, false))
		return;
	c_draw_immediate++;
	c_draw_quads++;

	if ( ( ( gl_config.renderer == GL_RENDERER_MCD ) || ( gl_config.renderer & GL_RENDERER_RENDITION ) ) && !gl->has_alpha)
	{
		qglDisable (GL_ALPHA_TEST);
		GL_CheckForError ();
	}

	if (gl->has_alpha)
	{
		qglDisable(GL_ALPHA_TEST);
		GL_CheckForError ();

		qglEnable(GL_BLEND);
		GL_CheckForError ();

		GL_TexEnv(GL_MODULATE);
	}

	GL_Bind (gl->texnum);
	qglBegin (GL_QUADS);
	qglTexCoord2f (gl->sl, gl->tl);
	Draw_Vertex (x, y);
	qglTexCoord2f (gl->sh, gl->tl);
	Draw_Vertex (x + w, y);
	qglTexCoord2f (gl->sh, gl->th);
	Draw_Vertex (x + w, y + h);
	qglTexCoord2f (gl->sl, gl->th);
	Draw_Vertex (x, y + h);
	qglEnd ();
	Draw_ShowTrisQuad (x, y, w, h);

	GL_CheckForError ();

	if (gl->has_alpha)
	{
		GL_TexEnv (GL_REPLACE);
		
		qglEnable(GL_ALPHA_TEST);
		GL_CheckForError ();

		qglDisable(GL_BLEND);
		GL_CheckForError ();
	}

	if ( ( ( gl_config.renderer == GL_RENDERER_MCD ) || ( gl_config.renderer & GL_RENDERER_RENDITION ) ) && !gl->has_alpha)
	{
		qglEnable (GL_ALPHA_TEST);
		GL_CheckForError ();
	}
}


/*
=============
Draw_Pic
=============
*/
void EXPORT Draw_Pic (int x, int y, char *pic)
{
	image_t *gl;

	gl = Draw_FindPic (pic);

	if (!gl)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "Can't find pic: %s\n", pic);
		gl = r_notexture;
	}

	if (scrap_dirty)
		Scrap_Upload ();

	if (Draw_BatchPic (gl, x, y, gl->width, gl->height, false))
		return;
	c_draw_immediate++;
	c_draw_quads++;

	if ( ( ( gl_config.renderer == GL_RENDERER_MCD ) || ( gl_config.renderer & GL_RENDERER_RENDITION ) ) && !gl->has_alpha)
	{
		qglDisable (GL_ALPHA_TEST);
		GL_CheckForError ();
	}

	if (gl->has_alpha)
	{
		qglDisable(GL_ALPHA_TEST);
		GL_CheckForError ();

		qglEnable(GL_BLEND);
		GL_CheckForError ();

		GL_TexEnv(GL_MODULATE);
	}

	GL_Bind (gl->texnum);

	qglBegin (GL_QUADS);
	qglTexCoord2f (gl->sl, gl->tl);
	Draw_Vertex (x, y);
	qglTexCoord2f (gl->sh, gl->tl);
	Draw_Vertex (x + gl->width, y);
	qglTexCoord2f (gl->sh, gl->th);
	Draw_Vertex (x + gl->width, y + gl->height);
	qglTexCoord2f (gl->sl, gl->th);
	Draw_Vertex (x, y + gl->height);
	qglEnd ();
	Draw_ShowTrisQuad (x, y, gl->width, gl->height);

	GL_CheckForError ();

	if (gl->has_alpha)
	{
		GL_TexEnv (GL_REPLACE);
		qglEnable(GL_ALPHA_TEST);
		GL_CheckForError ();
		qglDisable(GL_BLEND);
		GL_CheckForError ();
	}

	if ( ( ( gl_config.renderer == GL_RENDERER_MCD ) || ( gl_config.renderer & GL_RENDERER_RENDITION ) )  && !gl->has_alpha)
	{
		qglEnable (GL_ALPHA_TEST);
		GL_CheckForError ();
	}
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void EXPORT Draw_TileClear (int x, int y, int w, int h, char *pic)
{
	image_t	*image;

	image = Draw_FindPic (pic);

	if (!image)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "Can't find pic: %s\n", pic);
		image = r_notexture;
	}

	/* Repeating UVs need this picture's own texture, not the shared atlas. */
	if (!GL_DetachScrapImage (image))
		return;
	if (Draw_BatchPic (image, x, y, w, h, true))
		return;
	c_draw_immediate++;
	c_draw_quads++;

	if ( ( ( gl_config.renderer == GL_RENDERER_MCD ) || ( gl_config.renderer & GL_RENDERER_RENDITION ) )  && !image->has_alpha)
	{
		qglDisable (GL_ALPHA_TEST);
		GL_CheckForError ();
	}

	GL_Bind (image->texnum);
	if (image->type == it_pic)
	{
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}
	qglBegin (GL_QUADS);
	qglTexCoord2f (x/64.0f, y/64.0f);
	Draw_Vertex (x, y);
	qglTexCoord2f ( (x+w)/64.0f, y/64.0f);
	Draw_Vertex (x + w, y);
	qglTexCoord2f ( (x+w)/64.0f, (y+h)/64.0f);
	Draw_Vertex (x + w, y + h);
	qglTexCoord2f ( x/64.0f, (y+h)/64.0f );
	Draw_Vertex (x, y + h);
	qglEnd ();
	Draw_ShowTrisQuad (x, y, w, h);
	if (image->type == it_pic)
	{
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_ImageClampMode ());
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_ImageClampMode ());
	}
	GL_CheckForError ();

	if ( ( ( gl_config.renderer == GL_RENDERER_MCD ) || ( gl_config.renderer & GL_RENDERER_RENDITION ) )  && !image->has_alpha)
	{
		qglEnable (GL_ALPHA_TEST);
		GL_CheckForError ();
	}
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void EXPORT Draw_Fill (int x, int y, int w, int h, int c)
{
	union
	{
		unsigned	c;
		byte		v[4];
	} color;

	if ( (unsigned)c > 255)
		ri.Sys_Error (ERR_FATAL, "Draw_Fill: bad color");

	{
		float rgba[4];
		color.c = d_8to24table[c];
		rgba[0] = color.v[0] / 255.0f;
		rgba[1] = color.v[1] / 255.0f;
		rgba[2] = color.v[2] / 255.0f;
		rgba[3] = 1;
		if (Draw_BatchQuad (x, y, w, h, 0, 0, 0, 0, 0, 0, rgba))
		{
			qglColor3f (1, 1, 1);
			qglEnable (GL_TEXTURE_2D);
			return;
		}
	}
	c_draw_immediate++;
	c_draw_quads++;

	qglDisable (GL_TEXTURE_2D);
	GL_CheckForError ();

	color.c = d_8to24table[c];
	qglColor3f (color.v[0]/255.0f,
		color.v[1]/255.0f,
		color.v[2]/255.0f);

	qglBegin (GL_QUADS);

	Draw_Vertex (x, y);
	Draw_Vertex (x + w, y);
	Draw_Vertex (x + w, y + h);
	Draw_Vertex (x, y + h);

	qglEnd ();
	Draw_ShowTrisQuad (x, y, w, h);
	GL_CheckForError ();

	qglColor3f (1,1,1);
	GL_CheckForError ();

	qglEnable (GL_TEXTURE_2D);
	GL_CheckForError ();
}

//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void EXPORT Draw_FadeScreen (void)
{
	static const float fade_color[4] = { 0, 0, 0, 0.8f };

	if (Draw_BatchQuad (0, 0, vid.width, vid.height, 0, 0, 0, 0,
			0, DRAW_BATCH_BLEND, fade_color))
	{
		qglColor4fv (colorWhite);
		qglEnable (GL_TEXTURE_2D);
		qglDisable (GL_BLEND);
		return;
	}
	c_draw_immediate++;
	c_draw_quads++;

	qglEnable (GL_BLEND);
	GL_CheckForError ();

	qglDisable (GL_TEXTURE_2D);
	GL_CheckForError ();

	qglColor4f (0, 0, 0, 0.8f);
	GL_CheckForError ();

	qglBegin (GL_QUADS);

	Draw_Vertex (0, 0);
	Draw_Vertex (vid.width, 0);
	Draw_Vertex (vid.width, vid.height);
	Draw_Vertex (0, vid.height);

	qglEnd ();
	Draw_ShowTrisQuad (0, 0, vid.width, vid.height);
	GL_CheckForError ();

	qglColor4fv(colorWhite);
	GL_CheckForError ();

	qglEnable (GL_TEXTURE_2D);
	GL_CheckForError ();

	qglDisable (GL_BLEND);
	GL_CheckForError ();
}


//====================================================================


/*
=============
Draw_StretchRaw
=============
*/
extern unsigned	r_rawpalette[256];

static void Draw_RawQuad (int x, int y, int w, int h)
{
	qboolean shader = false;
	GLint texenv = GL_REPLACE;

	c_draw_immediate++;
	c_draw_quads++;
	qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLfloat)GL_PicFilter (NULL));
	GL_CheckForError ();

	qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLfloat)GL_PicFilter (NULL));
	GL_CheckForError ();

	if ( ( gl_config.renderer == GL_RENDERER_MCD ) || ( gl_config.renderer & GL_RENDERER_RENDITION ) ) 
	{
		qglDisable (GL_ALPHA_TEST);
		GL_CheckForError ();
	}
	/* The shared surface shader samples unit zero; keep other texture units native. */
	if (!gl_state.currenttmu && qglIsEnabled && qglGetTexEnviv)
	{
		qglGetTexEnviv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &texenv);
		if (qglGetError () == GL_NO_ERROR)
			shader = GL_BeginSurfaceShader (qglIsEnabled (GL_TEXTURE_2D), true,
				(GLenum)texenv, 0, 0, 1);
	}

	qglBegin (GL_QUADS);
	qglTexCoord2f (0, 0);
	Draw_Vertex (x, y);
	qglTexCoord2f (1, 0);
	Draw_Vertex (x + w, y);
	qglTexCoord2f (1, 1);
	Draw_Vertex (x + w, y + h);
	qglTexCoord2f (0, 1);
	Draw_Vertex (x, y + h);
	qglEnd ();
	Draw_ShowTrisQuad (x, y, w, h);
	if (shader)
		GL_EndSurfaceShader ();
	GL_CheckForError ();

	if ( ( gl_config.renderer == GL_RENDERER_MCD ) || ( gl_config.renderer & GL_RENDERER_RENDITION ) ) 
	{
		qglEnable (GL_ALPHA_TEST);
		GL_CheckForError ();
	}
}

void EXPORT Draw_StretchRaw (int x, int y, int w, int h, int cols, int rows, byte *data)
{
	if (w <= 0 || h <= 0 || !GL_UploadRawFrame (cols, rows, data, r_rawpalette))
		return;
	Draw_RawQuad (x, y, w, h);
}

int EXPORT Draw_UpdateColorFrame (int cols, int rows, int stride,
	const unsigned char *rgba, unsigned int data_size)
{
	return GL_UploadColorFrame (cols, rows, stride, rgba, data_size);
}

int EXPORT Draw_ColorFrame (int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0 || x > INT_MAX - w || y > INT_MAX - h ||
		!GL_BindColorFrame ())
		return false;
	qglPushAttrib (GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
	qglEnable (GL_TEXTURE_2D);
	qglDisable (GL_BLEND);
	qglDisable (GL_ALPHA_TEST);
	qglColor4f (1, 1, 1, 1);
	qglTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	Draw_RawQuad (x, y, w, h);
	qglPopAttrib ();
	return true;
}
