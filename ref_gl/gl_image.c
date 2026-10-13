/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2003-2008 Andrey Nazarov (adapted Q2PRO image rules)

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

#include "gl_local.h"
#include "gl_accounting.h"
#include "gl_hqx.h"
#include <png.h>
#include <jpeglib.h>
#include <jerror.h>
#include <setjmp.h>
#include <float.h>

image_t		gltextures[MAX_GLTEXTURES];
int			numgltextures = 0;
static int glow_refresh_cursor;
static qboolean glow_refresh_available;
static image_t *lightmap_white_image;
//int			base_textureid;		// gltextures[i] = base_textureid+i

static	byte			intensitytable[256];
static	byte			gammatable[256];
static	byte			gammaintensitytable[256];
static unsigned *raw_buffer;
static size_t raw_buffer_size;
static qboolean color_frame_ready;
static cvar_t *vid_hwgamma;
static qboolean gamma_requested;
static qboolean image_shader_intensity, image_color_profile_ready, image_intensity_failed;

cvar_t		*intensity;
cvar_t		*gl_contrast;
cvar_t		*gl_saturation;
cvar_t		*gl_texture_lighting_mode;

unsigned	d_8to24table[256];
vec4_t		d_8to24float[256];

int GL_Upload8 (byte *data, int width, int height, qboolean mipmap, image_t *image);
int GL_Upload32 (unsigned *data, int width, int height, qboolean mipmap, int bpp, image_t *image);

const char	*current_texture_filename;

const int		gl_solid_format = 3;
const int		gl_alpha_format = 4;

int		gl_tex_solid_format = 3;
int		gl_tex_alpha_format = 4;

//int		gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;

int		gl_filter_min = GL_LINEAR_MIPMAP_LINEAR;
int		gl_filter_max = GL_LINEAR;

static void		*scaled_buffer = NULL;
static unsigned *mipmap_buffer = NULL;
static size_t scaled_buffer_size, mipmap_buffer_size;
static GLenum texenv_modes[2] = { 0xFFFFFFFF, 0xFFFFFFFF };

#ifdef RB_IMAGE_CACHE

struct	rbtree *rb;

/*static int _compare(const void *pa, const void *pb, const void *config)
{
	return strcmp ((const char *)pa, (const char *)pb);
}*/

void DestroyImageCache (void)
{
	RBLIST	*rblist;
	const void	*val;

	if ((rblist=rbopenlist(rb)))
	{
		while((val=rbreadlist(rblist)))
			rbdelete (val, rb);
	}

	rbcloselist(rblist);	

	rbdestroy (rb);
}

void EmptyImageCache (void)
{
	RBLIST	*rblist;
	const void	*val;

	if ((rblist=rbopenlist(rb)))
	{
		while((val=rbreadlist(rblist)))
			rbdelete (val, rb);
	}

	rbcloselist(rblist);
}

#endif

/*void GL_SetTexturePalette( unsigned palette[256] )
{
	int i;
	unsigned char temptable[768];

	if ( qglColorTableEXT && gl_ext_palettedtexture->value )
	{
		for ( i = 0; i < 256; i++ )
		{
			temptable[i*3+0] = ( palette[i] >> 0 ) & 0xff;
			temptable[i*3+1] = ( palette[i] >> 8 ) & 0xff;
			temptable[i*3+2] = ( palette[i] >> 16 ) & 0xff;
		}

		qglColorTableEXT( GL_SHARED_TEXTURE_PALETTE_EXT,
						   GL_RGB,
						   256,
						   GL_RGB,
						   GL_UNSIGNED_BYTE,
						   temptable );
	}
}*/

void GL_EnableMultitexture( qboolean enable )
{
	if ( !qglSelectTextureSGIS && !qglActiveTextureARB )
		return;

	GL_SelectTexture( GL_TEXTURE1 );

	if ( enable )
		qglEnable( GL_TEXTURE_2D );
	else
		qglDisable( GL_TEXTURE_2D );	

	GL_CheckForError ();

	GL_TexEnv( GL_REPLACE );

	GL_SelectTexture( GL_TEXTURE0 );
	GL_TexEnv( GL_REPLACE );
}

void GL_SelectTexture( GLenum texture )
{
	unsigned tmu;

	if (!qglSelectTextureSGIS && !qglActiveTextureARB)
		return;

	if ( texture == GL_TEXTURE0 )
	{
		tmu = 0;
	}
	else
	{
		tmu = 1;
	}

	gl_state.currenttmu = tmu;
	gl_state.currenttarget = texture;

	if ( qglSelectTextureSGIS )
	{
		qglSelectTextureSGIS( texture );
		GL_CheckForError ();
	}
	else if ( qglActiveTextureARB )
	{
		qglActiveTextureARB( texture );
		GL_CheckForError ();

		qglClientActiveTextureARB( texture );
		GL_CheckForError ();
	}
}

void GL_TexEnv( GLenum mode )
{
	if (mode != texenv_modes[gl_state.currenttmu])
	{
		qglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode );
		texenv_modes[gl_state.currenttmu] = mode;
		GL_CheckForError ();
	}
}

void GL_Bind (unsigned int texnum)
{
#ifdef _DEBUG
	extern	image_t	*draw_chars;

	if (FLOAT_NE_ZERO(gl_nobind->value))
	{
		if (gl_nobind->value == 2)
		{
			texnum = TEXNUM_SCRAPS;
		}
		else
		{
			if (draw_chars)		// performance evaluation option
				texnum = draw_chars->texnum;
		}
	}
#endif
	if ( gl_state.currenttextures[gl_state.currenttmu] == texnum)
		return;

	gl_state.currenttextures[gl_state.currenttmu] = texnum;

	qglBindTexture (GL_TEXTURE_2D, texnum);
	GL_CheckForError ();
}

void GL_MBind( GLenum target, unsigned int texnum )
{
	if (target != gl_state.currenttarget)
		GL_SelectTexture( target );

	if (target == GL_TEXTURE0)
	{
		if ( gl_state.currenttextures[0] == texnum )
			return;
	}
	else
	{
		if ( gl_state.currenttextures[1] == texnum )
			return;
	}

	GL_Bind( texnum );
	GL_CheckForError ();
}

typedef struct
{
	char *name;
	int	minimize, maximize;
} glmode_t;

glmode_t modes[] = {
	{ "GL_NEAREST", GL_NEAREST, GL_NEAREST },
	{ "GL_LINEAR", GL_LINEAR, GL_LINEAR },
	{ "GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
	{ "GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR },
	{ "MAG_NEAREST", GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST },
	//{"GL_LINEAR_MIPMAP_DETAIL",		GL_LINEAR_MIPMAP_LINEAR,	GL_LINEAR_DETAIL_SGIS},
	//{"GL_LINEAR_MIPMAP_SHARPEN",	GL_LINEAR_MIPMAP_LINEAR,	GL_LINEAR_SHARPEN_SGIS}
};

#define NUM_GL_MODES (sizeof(modes) / sizeof (glmode_t))

typedef struct
{
	char *name;
	int mode;
} gltmode_t;

gltmode_t gl_alpha_modes[] = {
	{"default", 4},
	{"GL_RGBA", GL_RGBA},
	{"GL_RGBA8", GL_RGBA8},
	{"GL_RGB5_A1", GL_RGB5_A1},
	{"GL_RGBA4", GL_RGBA4},
	{"GL_RGBA2", GL_RGBA2},
};

#define NUM_GL_ALPHA_MODES (sizeof(gl_alpha_modes) / sizeof (gltmode_t))

gltmode_t gl_solid_modes[] = {
	{"default", 3},
	{"GL_RGB", GL_RGB},
	{"GL_RGB8", GL_RGB8},
	{"GL_RGB5", GL_RGB5},
	{"GL_RGB4", GL_RGB4},
	{"GL_R3_G3_B2", GL_R3_G3_B2},
#ifdef GL_RGB2_EXT
	{"GL_RGB2", GL_RGB2_EXT},
#endif
};

#define NUM_GL_SOLID_MODES (sizeof(gl_solid_modes) / sizeof (gltmode_t))

/* Q2PRO texture filtering/precision controls. Keep public image/API layouts. */
static cvar_t *gl_anisotropy, *gl_bilerp_pics, *gl_bilerp_skies_2d, *gl_texturebits;
static cvar_t *gl_invert, *gl_gamma_scale_pics, *gl_downsample_skins;
static cvar_t *gl_upscale_pcx, *hqx_y, *hqx_cb, *hqx_cr;
static int texture_upscale;
static qboolean texture_max_level_supported, texture_lod_bias_supported, texture_edge_clamp_supported;
static float texture_saturation = 1;
static qboolean texture_invert, texture_gamma_pics, texture_downsample_skins = true;
static GLfloat texture_max_anisotropy = 1;
static GLint texture_cube_format = GL_RGBA8;
static qboolean texture_anisotropy_supported;
static void GL_RefreshGlowImages (void);

static qboolean GL_TextureExtension (const char *extension)
{
	const char *at, *extensions = gl_config.extensions_string;
	size_t length = strlen (extension);
	if (!extensions)
		return false;
	for (at = extensions; (at = strstr (at, extension)) != NULL; at += length)
		if ((at == extensions || at[-1] == ' ') && (at[length] == 0 || at[length] == ' '))
			return true;
	return false;
}

qboolean GL_TextureAnisotropySupported (void)
{
	int major = 0, minor = 0;
	if (gl_config.version_string)
		sscanf (gl_config.version_string, "%d.%d", &major, &minor);
	return major > 4 || (major == 4 && minor >= 6) ||
		GL_TextureExtension ("GL_EXT_texture_filter_anisotropic") ||
		GL_TextureExtension ("GL_ARB_texture_filter_anisotropic");
}

qboolean GL_TextureNPOTSupported (void)
{
	int major = 0, minor = 0;
	if (gl_config.version_string)
		sscanf (gl_config.version_string, "%d.%d", &major, &minor);
	return major >= 2 || GL_TextureExtension ("GL_ARB_texture_non_power_of_two");
}

static float GL_TextureFiniteValue (const cvar_t *var, float fallback)
{
	float value = var ? var->value : fallback;
	return value >= -FLT_MAX && value <= FLT_MAX ? value : fallback;
}

/* Extra HQ texture levels retain original image dimensions and UI coordinates. */
int GL_ImageMinFilter (const image_t *image, int filter)
{
	if (image && image->upscale_level && texture_max_level_supported)
		return filter == GL_NEAREST ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_NEAREST;
	return filter;
}

static int GL_UpscaleLevel (int width, int height, const image_t *image, int bpp)
{
	int w = 1, h = 1, level = texture_upscale, maximum = gl_config.max_texture_size;
	if (!level || !image || image->glow_companion ||
		(image->type != it_pic && image->type != it_sprite) ||
		(bpp != 8 && image->texnum != TEXNUM_SCRAPS) ||
		width < 1 || height < 1 || width > MAX_TEXTURE_DIMENSIONS || height > MAX_TEXTURE_DIMENSIONS)
		return 0;
	if (maximum > MAX_TEXTURE_DIMENSIONS)
		maximum = MAX_TEXTURE_DIMENSIONS;
	while (w < width)
		w <<= 1;
	while (h < height)
		h <<= 1;
	while (level && (w > (maximum >> level) || h > (maximum >> level)))
		level--;
	return level;
}

static void GL_ResetUpscale (image_t *image)
{
	if (!image || !image->upscale_level)
		return;
	image->upscale_level = 0;
	if (texture_max_level_supported)
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	if (texture_lod_bias_supported)
		qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, 0);
}

static int GL_Upload32Level (unsigned *data, int width, int height, qboolean mipmap, int bpp, image_t *image, int baselevel, qboolean checked);

void GL_TextureOptionsRegister (void)
{
	cvar_t *legacy = ri.Cvar_Get ("gl_ext_max_anisotropy", NULL, 0);
	cvar_t *modern = ri.Cvar_Get ("gl_anisotropy", NULL, 0);
	float value;
	gl_ext_max_anisotropy = ri.Cvar_Get ("gl_ext_max_anisotropy", "2", CVAR_ARCHIVE);
	gl_anisotropy = ri.Cvar_Get ("gl_anisotropy", "1", CVAR_ARCHIVE);
	value = GL_TextureFiniteValue (legacy && !modern ? legacy : gl_anisotropy, 1);
	if (value < 1)
		value = 1;
	ri.Cvar_SetValue ("gl_anisotropy", value);
	ri.Cvar_SetValue ("gl_ext_max_anisotropy", value);
	gl_anisotropy->modified = gl_ext_max_anisotropy->modified = false;
	gl_bilerp_pics = ri.Cvar_Get ("gl_bilerp_pics", "1", 0);
	gl_bilerp_skies_2d = ri.Cvar_Get ("gl_bilerp_skies", "1", 0);
	gl_texturebits = ri.Cvar_Get ("gl_texturebits", "0", 0);
	gl_saturation = ri.Cvar_Get ("gl_saturation", "1", 0);
	gl_invert = ri.Cvar_Get ("gl_invert", "0", 0);
	gl_downsample_skins = ri.Cvar_Get ("gl_downsample_skins", "1", 0);
	gl_upscale_pcx = ri.Cvar_Get ("gl_upscale_pcx", "0", 0);
	hqx_y = ri.Cvar_Get ("hqx_y", "48", 0);
	hqx_cb = ri.Cvar_Get ("hqx_cb", "7", 0);
	hqx_cr = ri.Cvar_Get ("hqx_cr", "6", 0);
	/* The legacy brightness knob seeds an absent modern picture-gamma toggle. */
	legacy = ri.Cvar_Get ("vid_gamma_pics", NULL, 0);
	modern = ri.Cvar_Get ("gl_gamma_scale_pics", NULL, 0);
	gl_gamma_scale_pics = ri.Cvar_Get ("gl_gamma_scale_pics", "0", 0);
	if (legacy && !modern && GL_TextureFiniteValue (legacy, 0) > 0)
		ri.Cvar_SetValue ("gl_gamma_scale_pics", 1);
}

GLfloat GL_TextureAnisotropy (void)
{
	float value = GL_TextureFiniteValue (gl_ext_max_anisotropy, 1);
	if (!gl_config.r1gl_GL_EXT_texture_filter_anisotropic || !texture_anisotropy_supported)
		return 1;
	if (value < 1)
		value = 1;
	if (value > texture_max_anisotropy)
		value = texture_max_anisotropy;
	return value;
}

int GL_PicFilter (const image_t *image)
{
	float value = GL_TextureFiniteValue (gl_bilerp_pics, 1);
	qboolean scrap = image && image->texnum == TEXNUM_SCRAPS;
	return value == 0 || (scrap && value >= 1 && value < 2) ? GL_NEAREST : GL_LINEAR;
}

GLint GL_ImageClampMode (void)
{
	return texture_edge_clamp_supported ? GL_CLAMP_TO_EDGE : GL_CLAMP;
}

void GL_InitTextureOptions (void)
{
	GLfloat maximum = 1;
	float bits = GL_TextureFiniteValue (gl_texturebits, 0);
	int major = 0, minor = 0;
	float upscale = GL_TextureFiniteValue (gl_upscale_pcx, 0);
	if (gl_config.version_string)
		sscanf (gl_config.version_string, "%d.%d", &major, &minor);
	texture_max_level_supported = major > 1 || (major == 1 && minor >= 2);
	texture_lod_bias_supported = major > 1 || (major == 1 && minor >= 4);
	texture_edge_clamp_supported = texture_max_level_supported ||
		GL_TextureExtension ("GL_EXT_texture_edge_clamp") ||
		GL_TextureExtension ("GL_SGIS_texture_edge_clamp");
	texture_upscale = upscale <= 0 ? 0 : upscale >= 2 ? 2 :
														(int)upscale;
	if (texture_upscale)
		GL_InitHQx (GL_TextureFiniteValue (hqx_y, 48), GL_TextureFiniteValue (hqx_cb, 7), GL_TextureFiniteValue (hqx_cr, 6));
	gl_upscale_pcx->modified = hqx_y->modified = hqx_cb->modified = hqx_cr->modified = false;
	texture_anisotropy_supported = GL_TextureAnisotropySupported ();
	texture_max_anisotropy = 1;
	if (texture_anisotropy_supported && qglGetFloatv && qglGetError)
	{
		if (GL_ClearErrors ())
		{
			qglGetFloatv (GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
			if (qglGetError () == GL_NO_ERROR && maximum >= 1 && maximum <= FLT_MAX)
				texture_max_anisotropy = maximum;
		}
	}
	/* Zero retains automatic/explicit legacy formats chosen by default state. */
	if (bits > 16)
	{
		gl_tex_solid_format = GL_RGB8;
		gl_tex_alpha_format = GL_RGBA8;
	}
	else if (bits > 8)
	{
		gl_tex_solid_format = GL_RGB5;
		gl_tex_alpha_format = GL_RGBA4;
	}
	else if (bits > 0)
	{
		gl_tex_solid_format = GL_R3_G3_B2;
		gl_tex_alpha_format = GL_RGBA2;
	}
	texture_cube_format = bits > 16 || bits <= 0 ? GL_RGBA8 : bits > 8 ? GL_RGBA4 :
																		 GL_RGBA2;
	texture_saturation = GL_TextureFiniteValue (gl_saturation, 1);
	if (texture_saturation < 0)
		texture_saturation = 0;
	if (texture_saturation > 1)
		texture_saturation = 1;
	texture_invert = GL_TextureFiniteValue (gl_invert, 0) != 0;
	texture_gamma_pics = GL_TextureFiniteValue (gl_gamma_scale_pics, 0) != 0;
	texture_downsample_skins = GL_TextureFiniteValue (gl_downsample_skins, 1) != 0;
	gl_texturebits->modified = false;
	gl_saturation->modified = gl_invert->modified = gl_gamma_scale_pics->modified = gl_downsample_skins->modified = false;
	vid_gamma_pics->modified = false;
	gl_bilerp_pics->modified = gl_bilerp_skies_2d->modified = false;
}

GLint GL_TextureCubeFormat (void)
{
	return texture_cube_format;
}

static qboolean GL_SetImageParameters (const image_t *image, qboolean mipmap)
{
	int filter;
	if (mipmap)
	{
		if (texture_anisotropy_supported)
			qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, GL_TextureAnisotropy ());
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
	else
	{
		if (image && image->type == it_sky)
			filter = GL_TextureFiniteValue (gl_bilerp_skies_2d, 1) == 0 ? GL_NEAREST : GL_LINEAR;
		else if (image && image->type == it_sprite)
			filter = image->particle_shape == 2 ? GL_NEAREST : GL_LINEAR;
		else
			filter = GL_PicFilter (image);
		if (texture_anisotropy_supported)
			qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1);
		if (image && image->upscale_level && texture_max_level_supported)
			qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, image->upscale_level);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_ImageMinFilter (image, filter));
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	}
	/* Pictures and sprite frames clamp at upload; Draw_TileClear temporarily
	 * enables repetition for its own draw. Wall and skin wrap is unchanged. */
	if (image && (image->type == it_pic || image->type == it_sprite))
	{
		int wrap = GL_ImageClampMode ();
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	}
	return qglGetError () == GL_NO_ERROR;
}

static void GL_UpdateImageParameters (qboolean mipmapped)
{
	GLint binding = 0, bound = 0;
	int i, j;
	Draw_Flush ();
	if (!qglGetIntegerv || !qglGetError || !qglBindTexture)
		return;
	if (!GL_ClearErrors ())
		return;
	qglGetIntegerv (GL_TEXTURE_BINDING_2D, &binding);
	if (qglGetError () != GL_NO_ERROR)
		return;
	for (i = 0; i < numgltextures; i++)
	{
		image_t *image = &gltextures[i];
		qboolean mipmap = image->type == it_wall || image->type == it_skin;
		if (image == lightmap_white_image || !image->registration_sequence || !image->texnum || mipmap != mipmapped)
			continue;
		for (j = 0; j < (image->glowtexnum ? 2 : 1); j++)
		{
			qglBindTexture (GL_TEXTURE_2D, j ? image->glowtexnum : image->texnum);
			qglGetIntegerv (GL_TEXTURE_BINDING_2D, &bound);
			if (qglGetError () != GL_NO_ERROR || (GLuint)bound != (j ? image->glowtexnum : image->texnum) ||
				!GL_SetImageParameters (image, mipmap))
				ri.Con_Printf (PRINT_ALL, "Texture parameter update failed for %s\n", image->name);
		}
	}
	qglBindTexture (GL_TEXTURE_2D, (GLuint)binding);
	if (!mipmapped)
		Draw_InvalidateImageFilters ();
}

void GL_UpdateTextureOptions (void)
{
	float value;
	if (gl_anisotropy->modified || gl_ext_max_anisotropy->modified)
	{
		value = GL_TextureFiniteValue (gl_anisotropy->modified ? gl_anisotropy : gl_ext_max_anisotropy, 1);
		if (value < 1)
			value = 1;
		ri.Cvar_SetValue ("gl_anisotropy", value);
		ri.Cvar_SetValue ("gl_ext_max_anisotropy", value);
		gl_anisotropy->modified = gl_ext_max_anisotropy->modified = false;
		GL_UpdateImageParameters (true);
	}
	if (gl_bilerp_pics->modified || gl_bilerp_skies_2d->modified)
	{
		GL_UpdateImageParameters (false);
		gl_bilerp_pics->modified = gl_bilerp_skies_2d->modified = false;
	}
	if (vid_gamma_pics->modified)
	{
		if (!gl_gamma_scale_pics->modified)
			ri.Cvar_SetValue ("gl_gamma_scale_pics", GL_TextureFiniteValue (vid_gamma_pics, 0) > 0 ? 1.0f : 0.0f);
		vid_gamma_pics->modified = false;
	}
	if (gl_saturation->modified || gl_invert->modified || gl_gamma_scale_pics->modified || gl_downsample_skins->modified ||
		gl_upscale_pcx->modified || hqx_y->modified || hqx_cb->modified || hqx_cr->modified)
	{
		ri.Con_Printf (PRINT_ALL, "Texture color/skin/upscale policy changes apply after vid_restart.\n");
		gl_saturation->modified = gl_invert->modified = gl_gamma_scale_pics->modified = gl_downsample_skins->modified = false;
		gl_upscale_pcx->modified = hqx_y->modified = hqx_cb->modified = hqx_cr->modified = false;
	}
	if (gl_texturebits->modified)
	{
		ri.Con_Printf (PRINT_ALL, "gl_texturebits changes apply after vid_restart.\n");
		gl_texturebits->modified = false;
	}
	GL_RefreshGlowImages ();
}

void GL_UpdateAnisotropy (void)
{
	GL_UpdateImageParameters (true);
}

/*
===============
GL_TextureMode
===============
*/
void GL_TextureMode( char *string )
{
	int		i;

	for (i=0 ; i< NUM_GL_MODES ; i++)
	{
		if ( !Q_stricmp( modes[i].name, string ) )
			break;
	}

	if (i == NUM_GL_MODES)
	{
		ri.Con_Printf (PRINT_ALL, "bad filter name\n");
		return;
	}

	gl_filter_min = modes[i].minimize;
	gl_filter_max = modes[i].maximize;

	GL_UpdateImageParameters (true);
}

/*
===============
GL_TextureAlphaMode
===============
*/
void GL_TextureAlphaMode( char *string )
{
	int		i;

	for (i=0 ; i< NUM_GL_ALPHA_MODES ; i++)
	{
		if ( !Q_stricmp( gl_alpha_modes[i].name, string ) )
			break;
	}

	if (i == NUM_GL_ALPHA_MODES)
	{
		ri.Con_Printf (PRINT_ALL, "bad alpha texture mode name\n");
		return;
	}

	gl_tex_alpha_format = gl_alpha_modes[i].mode;
}

/*
===============
GL_TextureSolidMode
===============
*/
void GL_TextureSolidMode( char *string )
{
	int		i;

	for (i=0 ; i< NUM_GL_SOLID_MODES ; i++)
	{
		if ( !Q_stricmp( gl_solid_modes[i].name, string ) )
			break;
	}

	if (i == NUM_GL_SOLID_MODES)
	{
		ri.Con_Printf (PRINT_ALL, "bad solid texture mode name\n");
		return;
	}

	gl_tex_solid_format = gl_solid_modes[i].mode;
}


/*
=============================================================================

  scrap allocation

  Allocate all the little status bar obejcts into a single texture
  to crutch up inefficient hardware / drivers

=============================================================================
*/

#define	MAX_SCRAPS		1
#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256

int			scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
static unsigned scrap_rgba[BLOCK_WIDTH * BLOCK_HEIGHT];
qboolean	scrap_dirty;

// returns a texture number and the position inside it
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	unsigned int		texnum;

	for (texnum=0 ; texnum<MAX_SCRAPS ; texnum++)
	{
		best = BLOCK_HEIGHT;

		for (i=0 ; i<BLOCK_WIDTH-w ; i++)
		{
			best2 = 0;

			for (j=0 ; j<w ; j++)
			{
				if (scrap_allocated[texnum][i+j] >= best)
					break;
				if (scrap_allocated[texnum][i+j] > best2)
					best2 = scrap_allocated[texnum][i+j];
			}
			if (j == w)
			{	// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i=0 ; i<w ; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	return -1;
//	Sys_Error ("Scrap_AllocBlock: full");
}

int	scrap_uploads;

void Scrap_Upload (void)
{
	image_t image;
	unsigned *pixels;
	int i, result;
	memset (&image, 0, sizeof (image));
	image.type = it_pic;
	image.texnum = TEXNUM_SCRAPS;
	scrap_uploads++;
	Draw_Flush ();
	GL_Bind (TEXNUM_SCRAPS);
	/* Keep the shared atlas raw: gamma and optional HQ passes may modify input. */
	pixels = malloc (sizeof (scrap_rgba));
	if (!pixels)
		return;
	memcpy (pixels, scrap_rgba, sizeof (scrap_rgba));
	result = GL_Upload32 (pixels, BLOCK_WIDTH, BLOCK_HEIGHT, false, 32, &image);
	if (result < 0 && image.upscale_level)
	{
		GL_ResetUpscale (&image);
		memcpy (pixels, scrap_rgba, sizeof (scrap_rgba));
		result = GL_Upload32Level (pixels, BLOCK_WIDTH, BLOCK_HEIGHT, false, 32, &image, 0, true);
		if (qglGetError () != GL_NO_ERROR)
			result = -1;
	}
	free (pixels);
	if (result < 0)
	{
		ri.Con_Printf (PRINT_ALL, "Unable to upload scrap texture\n");
		return;
	}
	for (i = 0; i < numgltextures; i++)
		if (gltextures[i].texnum == TEXNUM_SCRAPS)
			gltextures[i].upscale_level = image.upscale_level;
	Draw_InvalidateImageFilters ();
	scrap_dirty = false;
}

/*
=================================================================

PCX LOADING

=================================================================
*/

/* Source dimensions and scratch sizes are independent of driver upload limits. */
static qboolean R_ImageBytes (int width, int height, int components, size_t *bytes)
{
	if (width < 1 || height < 1 || width > MAX_TEXTURE_DIMENSIONS ||
		height > MAX_TEXTURE_DIMENSIONS || components < 1 || components > 4)
		return false;
	if ((size_t)width > (size_t)-1 / height / components)
		return false;
	*bytes = (size_t)width * height * components;
	return true;
}

static unsigned R_ImageWord (const byte *data)
{
	return data[0] | ((unsigned)data[1] << 8);
}

static qboolean R_ResizeImageBuffer (void **buffer, size_t *capacity, size_t bytes)
{
	void *replacement;

	if (*capacity >= bytes && *buffer)
		return true;
	replacement = realloc (*buffer, bytes);
	if (!replacement)
		return false;
	*buffer = replacement;
	*capacity = bytes;
	return true;
}

static qboolean R_PCXHeader (const byte *data, int length, int *width, int *height)
{
	size_t bytes;

	if (length < 128 || data[0] != 10 || data[1] != 5 ||
		data[2] != 1 || data[3] != 8 || (data[65] != 1 && data[65] != 3))
		return false;
	*width = (int)R_ImageWord (data + 8) - (int)R_ImageWord (data + 4) + 1;
	*height = (int)R_ImageWord (data + 10) - (int)R_ImageWord (data + 6) + 1;
	return R_ImageBytes (*width, *height, 4, &bytes) &&
		R_ImageWord (data + 66) >= (unsigned)*width;
}


qboolean GetPCXInfo (const char *filename, int *width, int *height)
{
	byte *data;
	int length;
	qboolean valid;

	*width = *height = 0;
	length = ri.FS_LoadFile (filename, (void **)&data);
	if (!data)
		return false;
	valid = R_PCXHeader (data, length, width, height);
	ri.FS_FreeFile (data);
	if (!valid)
		*width = *height = 0;
	return valid;
}


static void LoadPCXImage (const char *filename, byte **pic, byte **palette,
	int *width, int *height, int *bits)
{
	byte *data, *pixels = NULL, *scanline = NULL, *pal = NULL;
	const byte *input, *end;
	int length, w, h, planes, stride, scan, x, y, count, value;
	size_t bytes;

	*pic = NULL;
	if (palette)
		*palette = NULL;
	*width = *height = *bits = 0;
	length = ri.FS_LoadFile (filename, (void **)&data);
	if (!data)
		return;
	if (!R_PCXHeader (data, length, &w, &h))
		goto failed;
	planes = data[65];
	stride = R_ImageWord (data + 66);
	scan = stride * planes;
	input = data + 128;
	end = data + length;
	if (planes == 1 && palette)
	{
		if (length < 128 + 768)
			goto failed;
		end -= 768;
		pal = malloc (768);
		if (!pal)
			goto failed;
		memcpy (pal, end, 768);
		if (end > input && end[-1] == 12)
			end--;
	}
	if (!R_ImageBytes (w, h, planes == 1 ? 1 : 4, &bytes))
		goto failed;
	pixels = malloc (bytes);
	scanline = malloc (scan);
	if (!pixels || !scanline)
		goto failed;
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < scan;)
		{
			if (input >= end)
				goto failed;
			value = *input++;
			count = 1;
			if ((value & 0xC0) == 0xC0)
			{
				count = value & 0x3F;
				if (!count || input >= end)
					goto failed;
				value = *input++;
			}
			if (count > scan - x)
				goto failed;
			memset (scanline + x, value, count);
			x += count;
		}
		if (planes == 1)
			memcpy (pixels + y * w, scanline, w);
		else
			for (x = 0; x < w; x++)
			{
				byte *out = pixels + ((size_t)y * w + x) * 4;
				out[0] = scanline[x];
				out[1] = scanline[x + stride];
				out[2] = scanline[x + stride * 2];
				out[3] = 255;
			}
	}
	*pic = pixels;
	*width = w;
	*height = h;
	*bits = planes == 1 ? 8 : 32;
	if (palette)
		*palette = pal;
	free (scanline);
	ri.FS_FreeFile (data);
	return;
failed:
	ri.Con_Printf (PRINT_ALL, "Invalid or unavailable PCX data: %s\n", filename);
	free (pixels);
	free (scanline);
	free (pal);
	ri.FS_FreeFile (data);
}

/*
==============
LoadPCX
==============
*/
void LoadPCX (const char *filename, byte **pic, byte **palette, int *width, int *height)
{
	int bits;

	LoadPCXImage (filename, pic, palette, width, height, &bits);
	/* The legacy palette consumer requires indexed colormap data. */
	if (*pic && bits != 8)
	{
		free (*pic);
		*pic = NULL;
		*width = *height = 0;
		ri.Con_Printf (PRINT_ALL, "Expected indexed PCX: %s\n", filename);
	}
}

typedef struct
{
	byte *Buffer;
	size_t Pos, Length;
	byte *Pixels;
	png_bytep *Rows;
} TPngFileBuffer;

static void PngErrorFunc (png_structp png, png_const_charp message)
{
	ri.Con_Printf (PRINT_ALL, "PNG decode failed: %s\n", message);
	longjmp (png_jmpbuf (png), 1);
}

static void PngWarningFunc (png_structp png, png_const_charp message)
{
	ri.Con_Printf (PRINT_DEVELOPER, "PNG: %s\n", message);
}

void EXPORT PngReadFunc (png_structp png, png_bytep buffer, png_size_t size)
{
	TPngFileBuffer *file = (TPngFileBuffer *)png_get_io_ptr (png);

	if (file->Pos > file->Length || size > file->Length - file->Pos)
		png_error (png, "Unexpected end of image");
	memcpy (buffer, file->Buffer + file->Pos, size);
	file->Pos += size;
}

void LoadPNG (const char *name, byte **pic, int *width, int *height)
{
	TPngFileBuffer *file;
	png_structp png;
	png_infop info;
	png_uint_32 w, h;
	int length, depth, color, alpha;
	unsigned i;
	size_t bytes, rowbytes;

	*pic = NULL;
	*width = *height = 0;
	file = calloc (1, sizeof (*file));
	if (!file)
		return;
	length = ri.FS_LoadFile (name, (void **)&file->Buffer);
	if (!file->Buffer)
	{
		free (file);
		return;
	}
	file->Length = length > 0 ? (size_t)length : 0;
	if (length < 8 || png_sig_cmp (file->Buffer, 0, 8))
	{
		ri.Con_Printf (PRINT_ALL, "Invalid PNG header: %s\n", name);
		goto file_done;
	}
	png = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, PngErrorFunc, PngWarningFunc);
	if (!png)
		goto file_done;
	info = png_create_info_struct (png);
	if (!info)
	{
		png_destroy_read_struct (&png, NULL, NULL);
		goto file_done;
	}
	/* Heap-owned buffers remain well-defined across libpng's longjmp. */
	if (setjmp (png_jmpbuf (png)))
		goto png_done;
#ifdef PNG_SET_USER_LIMITS_SUPPORTED
	png_set_user_limits (png, MAX_TEXTURE_DIMENSIONS, MAX_TEXTURE_DIMENSIONS);
#endif
	png_set_read_fn (png, file, PngReadFunc);
	png_read_info (png, info);
	png_get_IHDR (png, info, &w, &h, &depth, &color, NULL, NULL, NULL);
	if (!R_ImageBytes ((int)w, (int)h, 4, &bytes))
		png_error (png, "Image dimensions exceed the source limit");
	alpha = (color & PNG_COLOR_MASK_ALPHA) || png_get_valid (png, info, PNG_INFO_tRNS);
	if (color == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb (png);
	if (color == PNG_COLOR_TYPE_GRAY && depth < 8)
	{
#if PNG_LIBPNG_VER >= 10400
		png_set_expand_gray_1_2_4_to_8 (png);
#else
		png_set_gray_1_2_4_to_8 (png);
#endif
	}
	if (png_get_valid (png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha (png);
	if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb (png);
	if (depth == 16)
		png_set_strip_16 (png);
	if (!alpha)
		png_set_filler (png, 255, PNG_FILLER_AFTER);
	png_set_interlace_handling (png);
	png_read_update_info (png, info);
	rowbytes = png_get_rowbytes (png, info);
	if (png_get_channels (png, info) != 4 || rowbytes != (size_t)w * 4)
		png_error (png, "Unexpected decoded pixel layout");
	file->Pixels = malloc (bytes);
	file->Rows = malloc ((size_t)h * sizeof (*file->Rows));
	if (!file->Pixels || !file->Rows)
		png_error (png, "Not enough memory for image");
	for (i = 0; i < h; i++)
		file->Rows[i] = file->Pixels + i * rowbytes;
	png_read_image (png, file->Rows);
	png_read_end (png, info);
	*pic = file->Pixels;
	file->Pixels = NULL;
	*width = w;
	*height = h;
png_done:
	png_destroy_read_struct (&png, &info, NULL);
file_done:
	free (file->Pixels);
	free (file->Rows);
	ri.FS_FreeFile (file->Buffer);
	free (file);
}

/*
=========================================================

TARGA LOADING

=========================================================
*/

/* Pixel and orientation rules follow Q2PRO images.c, GPL-2.0-or-later.
 * See the Q2PRO attribution above. */
static void R_TGAPixel (const byte *in, int bytes, byte *out)
{
	switch (bytes)
	{
	case 1:
		out[0] = out[1] = out[2] = in[0];
		out[3] = 255;
		break;
	case 2:
		out[0] = (in[1] & 0x7C) << 1;
		out[1] = (((in[1] & 3) << 3) | (in[0] >> 5)) << 3;
		out[2] = (in[0] & 31) << 3;
		out[3] = 255;
		break;
	case 3:
	case 4:
		out[0] = in[2];
		out[1] = in[1];
		out[2] = in[0];
		out[3] = bytes == 4 ? in[3] : 255;
		break;
	}
}

/*
=============
LoadTGA
=============
*/
void LoadTGA (const char *name, byte **pic, int *width, int *height)
{
	byte *data, *pixels = NULL;
	const byte *input, *end;
	byte palette[256][4], color[4];
	int length, w, h, type, rle, depth, bpp, attributes, interleave;
	int map_start, map_length, map_depth, map_bpp;
	int i, row, base_row, x, y, remaining, count, repeat;
	size_t bytes;

	*pic = NULL;
	*width = *height = 0;
	length = ri.FS_LoadFile (name, (void **)&data);
	if (!data)
		return;
	if (length < 18)
		goto failed;
	type = data[2] & ~8;
	rle = data[2] & 8;
	w = R_ImageWord (data + 12);
	h = R_ImageWord (data + 14);
	depth = data[16];
	attributes = data[17];
	if ((type != 1 && type != 2 && type != 3) ||
		!R_ImageBytes (w, h, 4, &bytes))
		goto failed;
	if (depth != 8 && depth != 15 && depth != 16 && depth != 24 && depth != 32)
		goto failed;
	bpp = (depth + 1) / 8;
	if ((attributes & 0xC0) == 0xC0)
		goto failed;
	interleave = attributes & 0x80 ? 4 : attributes & 0x40 ? 2 :
															 1;
	if (type == 1 && (!data[1] || depth != 8))
		goto failed;
	end = data + length;
	input = data + 18;
	if (data[0] > end - input)
		goto failed;
	input += data[0];
	memset (palette, 0, sizeof (palette));
	if (data[1])
	{
		map_start = R_ImageWord (data + 3);
		map_length = R_ImageWord (data + 5);
		map_depth = data[7];
		if (map_depth != 15 && map_depth != 16 && map_depth != 24 && map_depth != 32)
			goto failed;
		if (map_start + map_length > 256)
			goto failed;
		map_bpp = (map_depth + 1) / 8;
		if ((size_t)map_length * map_bpp > (size_t)(end - input))
			goto failed;
		for (i = 0; i < map_length; i++, input += map_bpp)
			R_TGAPixel (input, map_bpp, palette[map_start + i]);
	}
	pixels = malloc (bytes);
	if (!pixels)
		goto failed;
	remaining = w * h;
	row = base_row = x = 0;
	while (remaining)
	{
		count = 1;
		repeat = 0;
		if (rle)
		{
			if (input >= end)
				goto failed;
			count = 1 + (*input & 0x7F);
			repeat = *input++ & 0x80;
		}
		if (count > remaining)
			goto failed;
		if ((size_t)(repeat ? bpp : count * bpp) > (size_t)(end - input))
			goto failed;
		if (repeat)
		{
			if (type == 1)
				memcpy (color, palette[*input], 4);
			else
				R_TGAPixel (input, bpp, color);
			input += bpp;
		}
		for (i = 0; i < count; i++)
		{
			if (!repeat)
			{
				if (type == 1)
					memcpy (color, palette[*input], 4);
				else
					R_TGAPixel (input, bpp, color);
				input += bpp;
			}
			y = attributes & 0x20 ? row : h - row - 1;
			memcpy (pixels + ((size_t)y * w + (attributes & 0x10 ? w - x - 1 : x)) * 4, color, 4);
			if (++x == w)
			{
				x = 0;
				row += interleave;
				if (row >= h)
					row = ++base_row;
			}
		}
		remaining -= count;
	}
	*pic = pixels;
	*width = w;
	*height = h;
	ri.FS_FreeFile (data);
	return;
failed:
	ri.Con_Printf (PRINT_ALL, "Invalid or unavailable TGA data: %s\n", name);
	free (pixels);
	ri.FS_FreeFile (data);
}

/*
=================================================================

JPEG LOADING
NiceAss: Code from Q2Ice

=================================================================
*/

typedef struct
{
	struct jpeg_error_mgr manager;
	jmp_buf jump;
} RJpegError;

typedef struct
{
	struct jpeg_decompress_struct codec;
	RJpegError error;
	byte *Source, *Pixels, *Row;
} RJpegImage;

static void EXPORT jpg_error_exit (j_common_ptr codec)
{
	RJpegError *error = (RJpegError *)codec->err;
	char message[JMSG_LENGTH_MAX];

	(*codec->err->format_message) (codec, message);
	ri.Con_Printf (PRINT_ALL, "JPEG decode failed: %s\n", message);
	longjmp (error->jump, 1);
}

void EXPORT jpg_null (j_decompress_ptr codec)
{
}

boolean EXPORT jpg_fill_input_buffer (j_decompress_ptr codec)
{
	ERREXIT (codec, JERR_INPUT_EOF);
	return FALSE;
}

void EXPORT jpg_skip_input_data (j_decompress_ptr codec, long bytes)
{
	if (bytes <= 0)
		return;
	if ((size_t)bytes > codec->src->bytes_in_buffer)
		ERREXIT (codec, JERR_INPUT_EOF);
	codec->src->next_input_byte += bytes;
	codec->src->bytes_in_buffer -= bytes;
}

static void R_JpegMemSrc (j_decompress_ptr codec, byte *data, int length)
{
	codec->src = (struct jpeg_source_mgr *)(*codec->mem->alloc_small) ((j_common_ptr)codec, JPOOL_PERMANENT, sizeof (*codec->src));
	codec->src->init_source = jpg_null;
	codec->src->fill_input_buffer = jpg_fill_input_buffer;
	codec->src->skip_input_data = jpg_skip_input_data;
	codec->src->resync_to_restart = jpeg_resync_to_restart;
	codec->src->term_source = jpg_null;
	codec->src->bytes_in_buffer = length;
	codec->src->next_input_byte = data;
}

void LoadJPG (const char *filename, byte **pic, int *width, int *height)
{
	RJpegImage *image;
	int length;
	unsigned x;
	size_t bytes;
	byte *out;

	*pic = NULL;
	*width = *height = 0;
	image = calloc (1, sizeof (*image));
	if (!image)
		return;
	length = ri.FS_LoadFile (filename, (void **)&image->Source);
	if (!image->Source || length < 2)
		goto done;
	image->codec.err = jpeg_std_error (&image->error.manager);
	image->error.manager.error_exit = jpg_error_exit;
	if (setjmp (image->error.jump))
		goto done;
	jpeg_create_decompress (&image->codec);
	R_JpegMemSrc (&image->codec, image->Source, length);
	jpeg_read_header (&image->codec, TRUE);
	if (image->codec.jpeg_color_space != JCS_RGB &&
		image->codec.jpeg_color_space != JCS_YCbCr &&
		image->codec.jpeg_color_space != JCS_GRAYSCALE)
	{
		ri.Con_Printf (PRINT_ALL, "Unsupported JPEG color space: %s\n", filename);
		goto done;
	}
	if (!R_ImageBytes ((int)image->codec.image_width, (int)image->codec.image_height, 4, &bytes))
	{
		ri.Con_Printf (PRINT_ALL, "Invalid JPEG dimensions: %s\n", filename);
		goto done;
	}
	image->codec.out_color_space = JCS_RGB;
	jpeg_start_decompress (&image->codec);
	image->Pixels = malloc (bytes);
	image->Row = malloc ((size_t)image->codec.output_width * 3);
	if (!image->Pixels || !image->Row)
		goto done;
	while (image->codec.output_scanline < image->codec.output_height)
	{
		out = image->Pixels + (size_t)image->codec.output_scanline * image->codec.output_width * 4;
		if (jpeg_read_scanlines (&image->codec, &image->Row, 1) != 1)
			ERREXIT (&image->codec, JERR_INPUT_EOF);
		for (x = 0; x < image->codec.output_width; x++, out += 4)
		{
			memcpy (out, image->Row + x * 3, 3);
			out[3] = 255;
		}
	}
	jpeg_finish_decompress (&image->codec);
	*width = image->codec.output_width;
	*height = image->codec.output_height;
	*pic = image->Pixels;
	image->Pixels = NULL;
done:
	if (image->codec.mem)
		jpeg_destroy_decompress (&image->codec);
	free (image->Pixels);
	free (image->Row);
	if (image->Source)
		ri.FS_FreeFile (image->Source);
	free (image);
}

/*typedef struct _TargaHeader {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;

typedef struct targaHeader_s {
	byte			idLength;
	byte			colorMapType;
	byte			imageType;

	unsigned short	colorMapIndex;
	unsigned short	colorMapLength;
	byte			colorMapSize;

	unsigned short	xOrigin;
	unsigned short	yOrigin;
	unsigned short	width;
	unsigned short	height;

	byte			pixelSize;

	byte			attributes;
} targaHeader_t;*/

/*void LoadTGA (char *name, byte **pic, int *width, int *height)
{
	int				i, columns, rows, row_inc, row, col;
	byte			*buf_p, *buffer, *pixbuf, *targa_rgba;
	int				length, samples, readpixelcount, pixelcount;
	byte			palette[256][4], red, green, blue, alpha;
	qboolean		compressed;
	targaHeader_t	targa_header;

	*pic = NULL;

	//
	// load the file
	//

	length = ri.FS_LoadFile (name, (void **)&buffer);
	if (!buffer || (length <= 0))
		return;

	buf_p = buffer;
	targa_header.idLength = *buf_p++;
	targa_header.colorMapType = *buf_p++;
	targa_header.imageType = *buf_p++;

	targa_header.colorMapIndex = buf_p[0] + buf_p[1] * 256;
	buf_p+=2;
	targa_header.colorMapLength = buf_p[0] + buf_p[1] * 256;
	buf_p+=2;
	targa_header.colorMapSize = *buf_p++;
	targa_header.xOrigin = LittleShort (*((short *)buf_p));
	buf_p+=2;
	targa_header.yOrigin = LittleShort (*((short *)buf_p));
	buf_p+=2;
	targa_header.width = LittleShort (*((short *)buf_p));
	buf_p+=2;
	targa_header.height = LittleShort (*((short *)buf_p));
	buf_p+=2;
	targa_header.pixelSize = *buf_p++;
	targa_header.attributes = *buf_p++;

	if (targa_header.idLength != 0)
		buf_p += targa_header.idLength;  // skip TARGA image comment

	if ((targa_header.imageType == 1) || (targa_header.imageType == 9))
	{
		// uncompressed colormapped image
		if (targa_header.pixelSize != 8)
		{
			ri.FS_FreeFile (buffer);
			ri.Sys_Error (ERR_DROP, "LoadTGA: (%s) Only 8 bit images supported for type 1 and 9\n", name);
		}

		if( targa_header.colorMapLength != 256 )
		{
			ri.FS_FreeFile (buffer);
			ri.Sys_Error (ERR_DROP, "LoadTGA: (%s) Only 8 bit colormaps are supported for type 1 and 9\n", name);
		}

		if( targa_header.colorMapIndex )
		{
			ri.FS_FreeFile (buffer);
			ri.Sys_Error (ERR_DROP, "LoadTGA: (%s) colorMapIndex is not supported for type 1 and 9", name);
		}

		if (targa_header.colorMapSize == 24)
		{
			for (i=0 ; i<targa_header.colorMapLength ; i++)
			{
				palette[i][0] = *buf_p++;
				palette[i][1] = *buf_p++;
				palette[i][2] = *buf_p++;
				palette[i][3] = 255;
			}
		}
		else if (targa_header.colorMapSize == 32)
		{
			for (i=0 ; i<targa_header.colorMapLength ; i++)
			{
				palette[i][0] = *buf_p++;
				palette[i][1] = *buf_p++;
				palette[i][2] = *buf_p++;
				palette[i][3] = *buf_p++;
			}
		}
		else
		{
			ri.FS_FreeFile (buffer);
			ri.Sys_Error (ERR_DROP, "LoadTGA: (%s) Only 24 and 32 bit colormaps are supported for type 1 and 9", name);
		}
	}
	else if ((targa_header.imageType == 2) || (targa_header.imageType == 10))
	{
		// uncompressed or RLE compressed RGB
		if ((targa_header.pixelSize != 32) && (targa_header.pixelSize != 24))
		{
			ri.FS_FreeFile (buffer);
			ri.Sys_Error (ERR_DROP, "LoadTGA: (%s) Only 32 or 24 bit images supported for type 2 and 10", name);
		}
	}
	else if ((targa_header.imageType == 3) || (targa_header.imageType == 11))
	{
		// uncompressed greyscale
		if (targa_header.pixelSize != 8 )
		{
			ri.FS_FreeFile (buffer);
			ri.Sys_Error (ERR_DROP, "LoadTGA: (%s) Only 8 bit images supported for type 3 and 11", name);
		}
	}

	columns = targa_header.width;
	if (width)
		*width = columns;

	rows = targa_header.height;
	if (height)
		*height = rows;

	targa_rgba = malloc (columns * rows * 4);
	*pic = targa_rgba;

	// if bit 5 of attributes isn't set, the image has been stored from bottom to top
	if (targa_header.attributes & 0x20)
	{
		pixbuf = targa_rgba;
		row_inc = 0;
	}
	else
	{
		pixbuf = targa_rgba + (rows - 1) * columns * 4;
		row_inc = -columns * 4 * 2;
	}

	compressed = ((targa_header.imageType == 9) || (targa_header.imageType == 10) || (targa_header.imageType == 11));

	for (row=col=0, samples=3 ; row<rows ; )
	{
		pixelcount = 0x10000;
		readpixelcount = 0x10000;

		if (compressed)
		{
			pixelcount = *buf_p++;
			if (pixelcount & 0x80)	// run-length packet
				readpixelcount = 1;
			pixelcount = 1 + (pixelcount & 0x7f);
		}

		while (pixelcount-- && (row < rows))
		{
			if (readpixelcount-- > 0)
			{
				switch (targa_header.imageType)
				{
					case 1:
					case 9:
						// colormapped image
						blue = *buf_p++;
						red = palette[blue][0];
						green = palette[blue][1];
						alpha = palette[blue][3];
						blue = palette[blue][2];
						if (alpha != 255)
							samples = 4;
						break;
					case 2:
					case 10:
						// 24 or 32 bit image
						blue = *buf_p++;
						green = *buf_p++;
						red = *buf_p++;
						alpha = 255;
						if (targa_header.pixelSize == 32) {
							alpha = *buf_p++;
							if (alpha != 255)
								samples = 4;
						}
						break;
					case 3:
					case 11:
						// greyscale image
						blue = green = red = *buf_p++;
						alpha = 255;
						break;
				}
			}

			*pixbuf++ = red;
			*pixbuf++ = green;
			*pixbuf++ = blue;
			*pixbuf++ = alpha;

			// run spans across rows
			if (++col == columns)
			{
				row++;
				col = 0;
				pixbuf += row_inc;
			}
		}
	}

	ri.FS_FreeFile (buffer);
}*/

/*void LoadTGA (char *name, byte **pic, int *width, int *height)
{
	int		columns, rows, numPixels;
	byte	*pixbuf;
	int		row, column;
	byte	*buf_p;
	byte	*buffer;
	//int		length;
	TargaHeader		targa_header;
	byte			*targa_rgba;
	byte tmp[2];

	*pic = NULL;

	//
	// load the file
	//
	ri.FS_LoadFile (name, (void **)&buffer);
	if (!buffer)
		return;

	buf_p = buffer;

	targa_header.id_length = *buf_p++;
	targa_header.colormap_type = *buf_p++;
	targa_header.image_type = *buf_p++;
	
	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_index = LittleShort ( *((short *)tmp) );
	buf_p+=2;
	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_length = LittleShort ( *((short *)tmp) );
	buf_p+=2;
	targa_header.colormap_size = *buf_p++;
	targa_header.x_origin = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.y_origin = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.width = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.height = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.pixel_size = *buf_p++;
	targa_header.attributes = *buf_p++;

	if (targa_header.image_type!=2 
		&& targa_header.image_type!=10) 
		ri.Sys_Error (ERR_DROP, "LoadTGA: (%s) Only type 2 and 10 targa RGB images supported\n", name);

	if (targa_header.colormap_type !=0 
		|| (targa_header.pixel_size!=32 && targa_header.pixel_size!=24))
		ri.Sys_Error (ERR_DROP, "LoadTGA: (%s) Only 32 or 24 bit images supported (no colormaps)\n", name);

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;

	if (width)
		*width = columns;
	if (height)
		*height = rows;

	targa_rgba = malloc (numPixels*4);
	*pic = targa_rgba;

	if (targa_header.id_length != 0)
		buf_p += targa_header.id_length;  // skip TARGA image comment
	
	if (targa_header.image_type==2) {  // Uncompressed, RGB images
		for(row=rows-1; row>=0; row--) {
			pixbuf = targa_rgba + row*columns*4;
			for(column=0; column<columns; column++) {
				unsigned char red,green,blue,alphabyte;
				switch (targa_header.pixel_size) {
					case 24:
							
							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = 255;
							break;
					case 32:
							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							alphabyte = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = alphabyte;
							break;
				}
			}
		}
	}
	else if (targa_header.image_type==10) {   // Runlength encoded RGB images
		unsigned char red,green,blue,alphabyte,packetHeader,packetSize,j;
		for(row=rows-1; row>=0; row--) {
			pixbuf = targa_rgba + row*columns*4;
			for(column=0; column<columns; ) {
				packetHeader= *buf_p++;
				packetSize = 1 + (packetHeader & 0x7f);
				if (packetHeader & 0x80) {        // run-length packet
					switch (targa_header.pixel_size) {
						case 24:
								blue = *buf_p++;
								green = *buf_p++;
								red = *buf_p++;
								alphabyte = 255;
								break;
						case 32:
								blue = *buf_p++;
								green = *buf_p++;
								red = *buf_p++;
								alphabyte = *buf_p++;
								break;
					}
	
					for(j=0;j<packetSize;j++) {
						*pixbuf++=red;
						*pixbuf++=green;
						*pixbuf++=blue;
						*pixbuf++=alphabyte;
						column++;
						if (column==columns) { // run spans across rows
							column=0;
							if (row>0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row*columns*4;
						}
					}
				}
				else {                            // non run-length packet
					for(j=0;j<packetSize;j++) {
						switch (targa_header.pixel_size) {
							case 24:
									blue = *buf_p++;
									green = *buf_p++;
									red = *buf_p++;
									*pixbuf++ = red;
									*pixbuf++ = green;
									*pixbuf++ = blue;
									*pixbuf++ = 255;
									break;
							case 32:
									blue = *buf_p++;
									green = *buf_p++;
									red = *buf_p++;
									alphabyte = *buf_p++;
									*pixbuf++ = red;
									*pixbuf++ = green;
									*pixbuf++ = blue;
									*pixbuf++ = alphabyte;
									break;
						}
						column++;
						if (column==columns) { // pixel packet run spans across rows
							column=0;
							if (row>0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row*columns*4;
						}						
					}
				}
			}
			breakOut:;
		}
	}

	ri.FS_FreeFile (buffer);
}*/


/*
====================================================================

IMAGE FLOOD FILLING

====================================================================
*/


/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes
=================
*/

typedef struct
{
	short		x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy ) \
{ \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
}

void R_FloodFillSkin( byte *skin, int skinwidth, int skinheight )
{
	byte				fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t			fifo[FLOODFILL_FIFO_SIZE];
	int					inpt = 0, outpt = 0;
	int					filledcolor = -1;
	int					i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte		*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)				FLOODFILL_STEP( -1, -1, 0 );
		if (x < skinwidth - 1)	FLOODFILL_STEP( 1, 1, 0 );
		if (y > 0)				FLOODFILL_STEP( -skinwidth, 0, -1 );
		if (y < skinheight - 1)	FLOODFILL_STEP( skinwidth, 0, 1 );
		skin[x + skinwidth * y] = fdc;
	}
}

//=======================================================

/*
================
GL_ResampleTexture
================
*/
qboolean GL_ResampleTexture (unsigned *in, int inwidth, int inheight,
	unsigned *out, int outwidth, int outheight)
{
	int i, j;
	unsigned *inrow, *inrow2, *offsets, *p1, *p2, frac, fracstep;
	byte *pix1, *pix2, *pix3, *pix4;
	size_t bytes;

	if (!R_ImageBytes (inwidth, inheight, 4, &bytes) ||
		!R_ImageBytes (outwidth, outheight, 4, &bytes))
		return false;
	offsets = malloc ((size_t)outwidth * 2 * sizeof (*offsets));
	if (!offsets)
		return false;
	p1 = offsets;
	p2 = offsets + outwidth;
	fracstep = (unsigned)inwidth * 0x10000 / outwidth;
	frac = fracstep >> 2;
	for (i = 0; i < outwidth; i++, frac += fracstep)
		p1[i] = 4 * (frac >> 16);
	frac = 3 * (fracstep >> 2);
	for (i = 0; i < outwidth; i++, frac += fracstep)
		p2[i] = 4 * (frac >> 16);
	for (i = 0; i < outheight; i++, out += outwidth)
	{
		inrow = in + inwidth * (int)((i + 0.25f) * inheight / outheight);
		inrow2 = in + inwidth * (int)((i + 0.75f) * inheight / outheight);
		for (j = 0; j < outwidth; j++)
		{
			pix1 = (byte *)inrow + p1[j];
			pix2 = (byte *)inrow + p2[j];
			pix3 = (byte *)inrow2 + p1[j];
			pix4 = (byte *)inrow2 + p2[j];
			((byte *)(out + j))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
			((byte *)(out + j))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
			((byte *)(out + j))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
			((byte *)(out + j))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
		}
	}
	free (offsets);
	return true;
}

void GL_ResampleTexture24 (unsigned *in, int inwidth, int inheight,
	unsigned *out, int outwidth, int outheight)
{
	int i;
	byte *src, *dst;
	unsigned *tmp = NULL, *tmp2 = NULL;
	size_t input_bytes, output_bytes;

	if (!R_ImageBytes (inwidth, inheight, 4, &input_bytes) ||
		!R_ImageBytes (outwidth, outheight, 4, &output_bytes))
		goto failed;
	tmp = malloc (input_bytes);
	tmp2 = malloc (output_bytes);
	if (!tmp || !tmp2)
		goto failed;
	src = (byte *)in;
	dst = (byte *)tmp;
	for (i = 0; i < inheight * inwidth; i++, src += 3, dst += 4)
	{
		memcpy (dst, src, 3);
		dst[3] = 255;
	}
	if (!GL_ResampleTexture (tmp, inwidth, inheight, tmp2, outwidth, outheight))
		goto failed;
	src = (byte *)tmp2;
	dst = (byte *)out;
	for (i = 0; i < outheight * outwidth; i++, src += 4, dst += 3)
		memcpy (dst, src, 3);
	free (tmp);
	free (tmp2);
	return;
failed:
	free (tmp);
	free (tmp2);
	ri.Sys_Error (ERR_DROP, "GL_ResampleTexture24: invalid dimensions or out of memory");
}

/*
VectorMix
*/
void VectorMix(const vec3_t v1, const vec3_t v2, float mix, vec3_t out)
{
	int i;

	for(i = 0; i < 3; i++)
		out[i] = v1[i] * (1.0f - mix) + v2[i] * mix;
}

/*
R_FilterTexture

Applies the retained lighting mode's brightness and contrast to RGB.
Material saturation and inversion are applied by GL_ApplyImageColors.
*/
void R_FilterTexture (unsigned *in, int width, int height, imagetype_t type)
{
	vec3_t temp;
	int i, j, c;
	byte *p;
	float max, gamma, brightness, contrast;

	gamma = GL_TextureFiniteValue (type == it_pic ? vid_gamma_pics : vid_gamma, 1);
	if (gamma <= 0)
		gamma = 1;
	brightness = 1.6f / gamma;
	if (brightness > FLT_MAX)
		brightness = FLT_MAX;
	contrast = GL_TextureFiniteValue (gl_contrast, 0.8f);
	if (contrast < 0.5f)
		contrast = 0.5f;
	if (contrast > 1.5f)
		contrast = 1.5f;
	p = (byte *)in;
	c = width * height;

	for (i = 0; i < c; i++, p+= 4)
	{
		VectorScale (p, 1.0f / 255.0f, temp);  // convert to float

		VectorScale (temp, brightness, temp);

		max = 0.0f;  // determine brightest component

		for (j = 0; j < 3; j++)
		{

			if (temp[j] > max)
				max = temp[j];

			if (temp[j] < 0.0f)  // enforcing positive values
				temp[j] = 0.0f;
		}

		if (max > 1.0f)  // clamp without changing hue
			VectorScale(temp, 1.0f / max, temp);

		for (j = 0; j < 3; j++)
		{  
			// apply contrast

			temp[j] -= 0.5f;  // normalize to -0.5 through 0.5

			temp[j] *= contrast; // scale

			temp[j] += 0.5f;

			if (temp[j] > 1.0f)  // clamp
				temp[j] = 1.0f;
			else if (temp[j] < 0)
				temp[j] = 0;
		}

		for (j = 0; j < 3; j++)
		{
			temp[j] *= 255;  // back to byte

			if (temp[j] > 255)  // clamp
				temp[j] = 255;
			else if (temp[j] < 0)
				temp[j] = 0;

			p[j] = (byte)Q_ftol (temp[j]);
		}
	}
}

/*
================
GL_LightScaleTexture

Scale up the pixel values in a texture to increase the
lighting range
================
*/
void GL_LightScaleTexture (unsigned *in, int inwidth, int inheight, qboolean only_gamma)
{
	if (gl_state.hwgamma)
		return;
	if ( only_gamma )
	{
		int		i, c;
		byte	*p;

		p = (byte *)in;

		c = inwidth*inheight;
		for (i=0 ; i<c ; i++, p+=4)
		{
			p[0] = gammatable[p[0]];
			p[1] = gammatable[p[1]];
			p[2] = gammatable[p[2]];
		}
	}
	else
	{
		int		i, c;
		byte	*p;

		p = (byte *)in;

		c = inwidth*inheight;

		for (i=0 ; i<c ; i++, p+=4)
		{
			p[0] = gammaintensitytable[p[0]];
			p[1] = gammaintensitytable[p[1]];
			p[2] = gammaintensitytable[p[2]];
		}
	}
}

void GL_LightScaleTexture24 (unsigned *in, int inwidth, int inheight, qboolean only_gamma)
{
	if (gl_state.hwgamma)
		return;
	if ( only_gamma )
	{
		int		i, c;
		byte	*p;

		p = (byte *)in;

		c = inwidth*inheight;
		for (i=0 ; i<c ; i++, p+=3)
		{
			p[0] = gammatable[p[0]];
			p[1] = gammatable[p[1]];
			p[2] = gammatable[p[2]];
		}
	}
	else
	{
		int		i, c;
		byte	*p;

		p = (byte *)in;

		c = inwidth*inheight;
		for (i=0 ; i<c ; i++, p+=3)
		{
			p[0] = gammaintensitytable[p[0]];
			p[1] = gammaintensitytable[p[1]];
			p[2] = gammaintensitytable[p[2]];
		}
	}
}

/*
================
GL_MipMapLinear

Operates in place, quartering the size of the texture
Proper linear filter
================
*/
static qboolean GL_MipMapLinear (unsigned *in, int width, int height)
{
	static const int weights[4] = { 1, 2, 2, 1 };
	int x, y, channel, dx, dy, sx, sy, total;
	int outwidth = width > 1 ? width >> 1 : 1;
	int outheight = height > 1 ? height >> 1 : 1;
	unsigned *temp;
	byte *out;
	size_t bytes;

	if (!R_ImageBytes (outwidth, outheight, 4, &bytes))
		return false;
	if (r_registering)
	{
		if (!R_ResizeImageBuffer ((void **)&mipmap_buffer, &mipmap_buffer_size, bytes))
			return false;
		temp = mipmap_buffer;
	}
	else
	{
		temp = malloc (bytes);
		if (!temp)
			return false;
	}
	for (y = 0; y < outheight; y++)
		for (x = 0; x < outwidth; x++)
		{
			out = (byte *)(temp + y * outwidth + x);
			for (channel = 0; channel < 4; channel++)
			{
				total = 0;
				for (dy = 0; dy < 4; dy++)
				{
					sy = (y * 2 + dy - 1 + height) % height;
					for (dx = 0; dx < 4; dx++)
					{
						sx = (x * 2 + dx - 1 + width) % width;
						total += weights[dx] * weights[dy] *
							((byte *)(in + sy * width + sx))[channel];
					}
				}
				out[channel] = total / 36;
			}
		}
	memcpy (in, temp, bytes);
	if (!r_registering)
		free (temp);
	return true;
}

/*
================
GL_MipMap

Operates in place, quartering the size of the texture
================
*/
qboolean GL_MipMap (byte *in, int width, int height)
{
	int x, y, channel, x0, x1, y0, y1;
	int outwidth = width > 1 ? width >> 1 : 1;
	int outheight = height > 1 ? height >> 1 : 1;
	byte *out = in;
	size_t bytes;

	if (!R_ImageBytes (width, height, 4, &bytes))
		return false;
	if (FLOAT_NE_ZERO(gl_linear_mipmaps->value))
		return GL_MipMapLinear ((unsigned *)in, width, height);
	for (y = 0; y < outheight; y++)
	{
		y0 = y * 2;
		y1 = height > 1 ? y0 + 1 : y0;
		for (x = 0; x < outwidth; x++, out += 4)
		{
			x0 = x * 2;
			x1 = width > 1 ? x0 + 1 : x0;
			for (channel = 0; channel < 4; channel++)
				out[channel] = (in[(y0 * width + x0) * 4 + channel] +
					in[(y0 * width + x1) * 4 + channel] +
					in[(y1 * width + x0) * 4 + channel] +
					in[(y1 * width + x1) * 4 + channel]) >> 2;
		}
	}
	return true;
}

int		upload_width, upload_height;

/* Color transforms precede resampling/mips; alpha and glow emission stay raw. */
static void GL_ApplyImageColors (unsigned *data, int width, int height, const image_t *image)
{
	int i, channel, count;
	byte *pixel = (byte *)data;
	size_t bytes;
	qboolean wall;
	if (!data || !image || image->glow_companion || image->particle_shape ||
		!R_ImageBytes (width, height, 4, &bytes))
		return;
	wall = image->type == it_wall && !image->turbulent;
	count = width * height;
	if (wall && texture_saturation != 1)
		for (i = 0; i < count; i++)
		{
			byte *p = pixel + i * 4;
			float luminance = p[0] * 0.2126f + p[1] * 0.7152f + p[2] * 0.0722f;
			for (channel = 0; channel < 3; channel++)
			{
				float value = luminance + (p[channel] - luminance) * texture_saturation;
				if (value < 0)
					value = 0;
				if (value > 255)
					value = 255;
				p[channel] = (byte)value;
			}
		}
	if (!gl_state.hwgamma && (image->type == it_wall || image->type == it_skin || texture_gamma_pics))
	{
		if (image_shader_intensity || FLOAT_EQ_ZERO (gl_texture_lighting_mode->value))
			GL_LightScaleTexture (data, width, height, image->type != it_wall && image->type != it_skin);
		else
			R_FilterTexture (data, width, height, image->type);
	}
	if (wall && texture_invert)
		for (i = 0; i < count; i++)
			for (channel = 0; channel < 3; channel++)
				pixel[i * 4 + channel] = 255 - pixel[i * 4 + channel];
}

void GL_SkyColorPixels (byte *pixels, int width, int height)
{
	image_t image;
	memset (&image, 0, sizeof (image));
	image.type = it_sky;
	GL_ApplyImageColors ((unsigned *)pixels, width, height, &image);
}

static int GL_Upload32Level (unsigned *data, int width, int height, qboolean mipmap, int bpp, image_t *image, int baselevel, qboolean checked)
{
	int			samples;
	unsigned	*scaled = NULL;
	int			scaled_width, scaled_height;
	int			i, c;
	//byte		*scan;
	int comp, maximum, picmip;
	qboolean downsample;
	size_t bytes;

	Draw_Flush ();
	if (!R_ImageBytes (width, height, 4, &bytes))
		return -1;
	maximum = gl_config.max_texture_size;
	if (maximum < 1)
		return -1;
	if (maximum > MAX_TEXTURE_DIMENSIONS)
		maximum = MAX_TEXTURE_DIMENSIONS;
	if (!gl_config.r1gl_GL_ARB_texture_non_power_of_two)
	{
		int power;
		for (power = 1; power <= maximum / 2; power <<= 1)
			;
		maximum = power;
	}
	picmip = gl_picmip->value > 0 ? (gl_picmip->value < 13 ? (int)gl_picmip->value : 13) : 0;
	downsample = mipmap && (!image || image->type == it_wall || (image->type == it_skin && texture_downsample_skins));
	GL_ApplyImageColors (data, width, height, image);

	if (gl_config.r1gl_GL_ARB_texture_non_power_of_two)
	{
		scaled_width = width;
		scaled_height = height;
	}
	else
	{
		for (scaled_width = 1 ; scaled_width < width ; scaled_width<<=1)
			;
		if (FLOAT_NE_ZERO (gl_round_down->value) && scaled_width > width && downsample)
			scaled_width >>= 1;
		for (scaled_height = 1 ; scaled_height < height ; scaled_height<<=1)
			;
		if (FLOAT_NE_ZERO (gl_round_down->value) && scaled_height > height && downsample)
			scaled_height >>= 1;
	}

	// let people sample down world textures, and skins when enabled
	if (downsample)
	{
		scaled_width >>= picmip;
		scaled_height >>= picmip;
	}


	if (scaled_width > maximum)
		scaled_width = maximum;
	if (scaled_height > maximum)
		scaled_height = maximum;
	if (scaled_width < 1)
		scaled_width = 1;
	if (scaled_height < 1)
		scaled_height = 1;
	upload_width = scaled_width;
	upload_height = scaled_height;
	if (scaled_width == width && scaled_height == height)
		scaled = data;
	else
	{
		if (!R_ImageBytes (scaled_width, scaled_height, 4, &bytes))
			return -1;
		if (r_registering)
		{
			if (!R_ResizeImageBuffer (&scaled_buffer, &scaled_buffer_size, bytes))
				return -1;
			scaled = scaled_buffer;
		}
		else
		{
			scaled = malloc (bytes);
			if (!scaled)
				return -1;
		}
	}

	// scan the texture for any non-255 alpha
	samples = gl_solid_format;


	c = width * height;
	for (i = 0; i < c; i++)
		if (((byte *)data)[i * 4 + 3] != 255)
		{
			samples = gl_alpha_format;
			break;
		}

	if (samples == gl_solid_format)
	    comp = gl_tex_solid_format;
	else if (samples == gl_alpha_format)
	    comp = gl_tex_alpha_format;
	else {
	    ri.Con_Printf (PRINT_ALL,
			"GL_Upload32: %s: Unknown number of texture components %i\n",
			   current_texture_filename, samples);
	    comp = samples;
	}

	if (scaled_width == width && scaled_height == height)
	{
		if (!mipmap)
		{
			qglTexImage2D (GL_TEXTURE_2D, baselevel, comp, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			if ((checked || (image && (image->glow_companion || image->upscale_level || image->particle_shape || image->texnum == TEXNUM_SCRAPS))) && qglGetError () != GL_NO_ERROR)
				goto failed;
			goto done;
		}
		if (scaled != data)
			memcpy (scaled, data, width * height * sizeof(int));
	}
	else
	{
		if (!GL_ResampleTexture (data, width, height, scaled, scaled_width, scaled_height))
			goto failed;
	}

	//r1ch: hardware/driver mipmap generation
	//0.1.5: removed due to shitty drivers breaking it, thx ati
	/*if (gl_config.r1gl_GL_SGIS_generate_mipmap)
	{
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
		if ((err = qglGetError()) != GL_NO_ERROR) ri.Sys_Error (ERR_FATAL, "glGetError: 0x%x", err);

		qglTexParameteri (GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
		if ((err = qglGetError()) != GL_NO_ERROR) ri.Sys_Error (ERR_FATAL, "glGetError: 0x%x", err);
	}*/

	qglTexImage2D (GL_TEXTURE_2D, baselevel, comp, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
	if ((checked || (image && (image->glow_companion || image->upscale_level || image->particle_shape || image->texnum == TEXNUM_SCRAPS))) && qglGetError () != GL_NO_ERROR)
		goto failed;
	GL_CheckForError ();

	//if (mipmap && !(gl_config.r1gl_GL_SGIS_generate_mipmap))
	if (mipmap)
	{
		int		miplevel;

		//if (scaled_width & 1)
		//	Sys_DebugBreak ();

		miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1)
		{
			if (!GL_MipMap ((byte *)scaled, scaled_width, scaled_height))
				goto failed;

			scaled_width >>= 1;
			scaled_height >>= 1;

			if (scaled_width < 1)
				scaled_width = 1;
			if (scaled_height < 1)
				scaled_height = 1;
			miplevel++;
			qglTexImage2D (GL_TEXTURE_2D, baselevel + miplevel, comp, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
			;
			if ((checked || (image && (image->glow_companion || image->upscale_level || image->particle_shape || image->texnum == TEXNUM_SCRAPS))) && qglGetError () != GL_NO_ERROR)
				goto failed;
			GL_CheckForError ();
		}
	}
done: ;


	if (!GL_SetImageParameters (image, mipmap))
	{
		if (checked || (image && (image->glow_companion || image->upscale_level || image->particle_shape || image->texnum == TEXNUM_SCRAPS)))
			goto failed;
		ri.Con_Printf (PRINT_ALL, "Texture parameter setup failed for %s\n", current_texture_filename);
	}

	if (!r_registering)
	{
		if (scaled && scaled != data)
			free (scaled);
	}

	return (samples == gl_alpha_format) ? true : false;
failed:
	if (!r_registering && scaled && scaled != data)
		free (scaled);
	return -1;
}

static int GL_Upscale32 (unsigned *data, int width, int height, int bpp, image_t *image, int level, qboolean checked)
{
	unsigned *buffer;
	size_t bytes;
	int result = -1;
	float du, dv;

	if (!R_ImageBytes (width << level, height << level, 4, &bytes))
		return -1;
	buffer = malloc (bytes);
	if (!buffer)
		return GL_Upload32Level (data, width, height, false, bpp, image, 0, checked);
	image->upscale_level = level;
	if (!GL_ClearErrors ())
		goto done;
	/* Each HQ pass reads raw pixels; color conversion belongs to its upload. */
	if (level == 2)
	{
		GL_HQ4x (buffer, data, width, height);
		result = GL_Upload32Level (buffer, width * 4, height * 4, false, bpp, image, 0, checked);
		if (result < 0)
			goto done;
	}
	GL_HQ2x (buffer, data, width, height);
	result = GL_Upload32Level (buffer, width * 2, height * 2, false, bpp, image, level - 1, checked);
	if (result < 0)
		goto done;
	result = GL_Upload32Level (data, width, height, false, bpp, image, level, checked);
	if (result >= 0 && texture_lod_bias_supported && (upload_width != width || upload_height != height))
	{
		du = upload_width / (float)width;
		dv = upload_height / (float)height;
		qglTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, (float)(-log (du > dv ? du : dv) / log (2.0)));
		if (qglGetError () != GL_NO_ERROR)
			ri.Con_Printf (PRINT_ALL, "Unable to adjust upscaled texture LOD for %s\n", current_texture_filename);
	}
done:
	free (buffer);
	return result;
}

int GL_Upload32 (unsigned *data, int width, int height, qboolean mipmap, int bpp, image_t *image)
{
	int level = mipmap ? 0 : GL_UpscaleLevel (width, height, image, bpp);
	if (level)
		return GL_Upscale32 (data, width, height, bpp, image, level, false);
	return GL_Upload32Level (data, width, height, mipmap, bpp, image, 0, false);
}

/*
static qboolean IsPowerOf2( int value )
{
	int i = 1;


	while ( 1 )
	{
		if ( value == i )
			return true;
		if ( i > value )
			return false;
		i <<= 1;
	}
}
*/

static void GL_Unpack8 (unsigned *trans, const byte *data, int width, int height)
{
	int i, s, p;
	s = width * height;
	for (i = 0; i < s; i++)
	{
		p = data[i];
		trans[i] = d_8to24table[p];
		if (p == 255)
		{
			if (i >= width && data[i - width] != 255)
				p = data[i - width];
			else if (i < s - width && data[i + width] != 255)
				p = data[i + width];
			else if (i % width > 0 && data[i - 1] != 255)
				p = data[i - 1];
			else if (i % width < width - 1 && data[i + 1] != 255)
				p = data[i + 1];
			else
				p = 0;
			memcpy (&trans[i], &d_8to24table[p], 3);
		}
	}
}

/*
===============
GL_Upload8

Returns has_alpha
===============
*/
int GL_Upload8 (byte *data, int width, int height, qboolean mipmap, image_t *image)
{
	unsigned *trans;
	int result;
	size_t bytes;

	if (!R_ImageBytes (width, height, 4, &bytes))
		return -1;
	trans = malloc (bytes);
	if (!trans)
		return -1;
	GL_Unpack8 (trans, data, width, height);
	result = GL_Upload32 (trans, width, height, mipmap, 8, image);
	if (result < 0 && image && image->upscale_level)
	{
		GL_ResetUpscale (image);
		/* The failed last upload may have colored trans in place. */
		GL_Unpack8 (trans, data, width, height);
		result = GL_Upload32Level (trans, width, height, mipmap, 8, image, 0, false);
		if (qglGetError () != GL_NO_ERROR)
			result = -1;
	}
	free (trans);
	return result;
}


/*
================
GL_LoadPic

This is also used as an entry point for the generated r_notexture
================
*/
image_t *GL_LoadPic (const char *name, byte *pic, int width, int height, imagetype_t type, int bits, qboolean turbulent)
{
	qboolean	mipmap;
	image_t		*image;
	int			i;
	size_t bytes;

	if (!pic || !R_ImageBytes (width, height, bits == 8 ? 1 : 4, &bytes))
		return NULL;

	// find a free image_t
	for (i=0, image=gltextures ; i<numgltextures ; i++,image++)
	{
		if (!image->texnum)
			break;
	}
	if (i == numgltextures)
	{
		if (numgltextures == MAX_GLTEXTURES)
		{
			ri.Con_Printf (PRINT_ALL, "Image registration limit reached: %s\n", name);
			return NULL;
		}
		numgltextures++;
	}
	image = &gltextures[i];

	if (strlen (name) >= sizeof (image->name))
		ri.Sys_Error (ERR_DROP, "Draw_LoadPic: \"%s\" is too long", name);

	strcpy (image->name, name);
	image->registration_sequence = registration_sequence;

	image->width = width;
	image->height = height;
	image->type = type;
	image->paletted = bits == 8;
	image->turbulent = type == it_wall && turbulent;
	image->upscale_level = 0;
	image->particle_shape = 0;
	if (type == it_sprite && bits == 32 && width == 16 && height == 16)
	{
		if (!strcmp (name, "***particle_soft***"))
			image->particle_shape = 1;
		else if (!strcmp (name, "***particle_square***"))
			image->particle_shape = 2;
		else if (!strcmp (name, "***particle_round***"))
			image->particle_shape = 3;
	}
	//image->scrap = false;

	if (type == it_skin && bits == 8)
		R_FloodFillSkin(pic, width, height);

	// Convert indexed pictures before packing so transparent edges stay image-local.
	if (type == it_pic && width < 64 && height < 64 && FLOAT_EQ_ZERO (gl_noscrap->value) &&
		(bits == 8 || bits == 32))
	{
		unsigned *expanded = NULL, *rgba = (unsigned *)pic;
		int x, y, row, temp;
		if (bits == 8)
		{
			expanded = malloc ((size_t)width * height * 4);
			if (!expanded)
				goto nonscrap;
			GL_Unpack8 (expanded, pic, width, height);
			rgba = expanded;
		}
		temp = Scrap_AllocBlock (width, height, &x, &y);
		if (temp == -1)
		{
			free (expanded);
			goto nonscrap;
		}
		for (row = 0; row < height; row++)
			memcpy (&scrap_rgba[(y + row) * BLOCK_WIDTH + x],
				&rgba[row * width], (size_t)width * 4);
		free (expanded);
		scrap_dirty = true;
		image->texnum = TEXNUM_SCRAPS + temp;
		image->has_alpha = true;
		image->upscale_level = GL_UpscaleLevel (BLOCK_WIDTH, BLOCK_HEIGHT, image, 32);
		image->upload_width = width;
		image->upload_height = height;
		image->sl = (x + 0.01f) / (float)BLOCK_WIDTH;
		image->sh = (x + width - 0.01f) / (float)BLOCK_WIDTH;
		image->tl = (y + 0.01f) / (float)BLOCK_WIDTH;
		image->th = (y + height - 0.01f) / (float)BLOCK_WIDTH;
		return image;
	}

nonscrap:
	image->texnum = TEXNUM_IMAGES + (image - gltextures);
	GL_Bind(image->texnum);

	mipmap = image->type == it_wall || image->type == it_skin;

	if (bits == 8)
		image->has_alpha = GL_Upload8 (pic, width, height, mipmap, image);
	else
		image->has_alpha = GL_Upload32 ((unsigned *)pic, width, height, mipmap, bits, image);

	if (image->has_alpha < 0)
	{
		ri.Con_Printf (PRINT_ALL, "Image upload failed: %s\n", name);
		qglDeleteTextures (1, &image->texnum);
		gl_state.currenttextures[gl_state.currenttmu] = 0xFFFFFFFF;
		memset (image, 0, sizeof (*image));
		return NULL;
	}
	image->upload_width = upload_width;
	image->upload_height = upload_height;


	image->sl = 0;
	image->sh = 1;
	image->tl = 0;
	image->th = 1;

	return image;
}

/* Tiling must repeat this picture, not its neighbors in the shared atlas.
 * Reuse its ordinary registry texture name and publish only a complete upload. */
qboolean GL_DetachScrapImage (image_t *image)
{
	image_t standalone;
	unsigned *pixels;
	GLint saved, bound;
	int slot, x, y, width, height, row, level, result = -1;
	size_t bytes;

	if (!image)
		return false;
	if (image->texnum != TEXNUM_SCRAPS)
		return true;
	for (slot = 0; slot < numgltextures; slot++)
		if (image == &gltextures[slot])
			break;
	width = image->upload_width;
	height = image->upload_height;
	if (slot == numgltextures || image->type != it_pic ||
		!image->registration_sequence || width < 1 || width >= 64 ||
		height < 1 || height >= 64 ||
		!(image->sl >= 0 && image->sl < 1 && image->tl >= 0 && image->tl < 1) ||
		!qglGetIntegerv || !qglBindTexture || !qglGetError ||
		!R_ImageBytes (width, height, 4, &bytes))
		return false;
	x = (int)(image->sl * BLOCK_WIDTH);
	y = (int)(image->tl * BLOCK_HEIGHT);
	if (x > BLOCK_WIDTH - width || y > BLOCK_HEIGHT - height)
		return false;
	pixels = malloc (bytes);
	if (!pixels)
		return false;
	for (row = 0; row < height; row++)
		memcpy (&pixels[row * width], &scrap_rgba[(y + row) * BLOCK_WIDTH + x], (size_t)width * 4);

	Draw_Flush ();
	if (!GL_ClearErrors ())
	{
		free (pixels);
		return false;
	}
	qglGetIntegerv (GL_TEXTURE_BINDING_2D, &saved);
	if (qglGetError () != GL_NO_ERROR)
	{
		free (pixels);
		return false;
	}
	standalone = *image;
	standalone.texnum = TEXNUM_IMAGES + slot;
	standalone.upscale_level = 0;
	qglBindTexture (GL_TEXTURE_2D, standalone.texnum);
	qglGetIntegerv (GL_TEXTURE_BINDING_2D, &bound);
	if (qglGetError () == GL_NO_ERROR && (GLuint)bound == standalone.texnum)
	{
		level = GL_UpscaleLevel (width, height, &standalone, image->paletted ? 8 : 32);
		result = level ? GL_Upscale32 (pixels, width, height, 8, &standalone, level, true) :
						 GL_Upload32Level (pixels, width, height, false, 32, &standalone, 0, true);
		if (result < 0 && standalone.upscale_level)
		{
			GL_ResetUpscale (&standalone);
			for (row = 0; row < height; row++)
				memcpy (&pixels[row * width], &scrap_rgba[(y + row) * BLOCK_WIDTH + x], (size_t)width * 4);
			result = GL_Upload32Level (pixels, width, height, false, 32, &standalone, 0, true);
		}
	}
	if (result < 0)
		qglDeleteTextures (1, &standalone.texnum);
	qglBindTexture (GL_TEXTURE_2D, (GLuint)saved);
	qglGetIntegerv (GL_TEXTURE_BINDING_2D, &bound);
	gl_state.currenttextures[gl_state.currenttmu] =
		qglGetError () == GL_NO_ERROR && bound == saved ? (GLuint)saved : 0xFFFFFFFF;
	free (pixels);
	if (result < 0)
		return false;
	standalone.has_alpha = result;
	standalone.upload_width = upload_width;
	standalone.upload_height = upload_height;
	standalone.sl = standalone.tl = 0;
	standalone.sh = standalone.th = 1;
	*image = standalone;
	Draw_InvalidateImageFilters ();
	return true;
}

/* An ordinary registry slot avoids the reserved shader/post-process texture names.
 * Upload directly: even GL_LoadPic's GL_Bind can redirect through gl_nobind.
 * Called only by R_InitParticleTexture after a new context: no pixel-unpack
 * buffer has been bound. Keep this initializer out of live resource reloads. */
void GL_InitLightmapWhiteTexture (void)
{
	static const byte white[4] = { 255, 255, 255, 255 };
	static const GLenum unpack_names[4] = {
		GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS
	};
	GLint binding = 0, actual = 0, unpack[4], width = 0, height = 0;
	GLuint texture;
	image_t *image;
	qboolean uploaded = false;
	int i, slot;

	if (lightmap_white_image)
		return;
	for (slot = 0; slot < numgltextures; slot++)
		if (!gltextures[slot].texnum)
			break;
	if (slot == MAX_GLTEXTURES)
	{
		ri.Con_Printf (PRINT_ALL, "No image slot for lightmap-view white texture.\n");
		return;
	}
	qglGetIntegerv (GL_TEXTURE_BINDING_2D, &binding);
	for (i = 0; i < 4; i++)
		qglGetIntegerv (unpack_names[i], &unpack[i]);
	if (qglGetError () != GL_NO_ERROR || binding < 0)
		return;

	texture = TEXNUM_IMAGES + slot;
	qglBindTexture (GL_TEXTURE_2D, texture);
	qglGetIntegerv (GL_TEXTURE_BINDING_2D, &actual);
	if (qglGetError () == GL_NO_ERROR && actual == (GLint)texture)
	{
		for (i = 0; i < 4; i++)
			qglPixelStorei (unpack_names[i], i ? 0 : 1);
		qglTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
		qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
		uploaded = qglGetError () == GL_NO_ERROR && width == 1 && height == 1;
		for (i = 0; i < 4; i++)
			qglPixelStorei (unpack_names[i], unpack[i]);
	}
	qglBindTexture (GL_TEXTURE_2D, (GLuint)binding);
	qglGetIntegerv (GL_TEXTURE_BINDING_2D, &actual);
	if (qglGetError () != GL_NO_ERROR || actual != binding)
	{
		gl_state.currenttextures[0] = gl_state.currenttextures[1] = 0xFFFFFFFF;
		uploaded = false;
	}
	if (!uploaded)
	{
		qglDeleteTextures (1, &texture);
		ri.Con_Printf (PRINT_ALL, "Lightmap-view white texture unavailable; retaining normal materials.\n");
		return;
	}

	image = &gltextures[slot];
	memset (image, 0, sizeof (*image));
	strcpy (image->name, "***lightmap_white***");
	image->texnum = texture;
	image->registration_sequence = registration_sequence;
	image->type = it_pic; /* Keep it through ordinary map registration cleanup. */
	image->width = image->height = image->upload_width = image->upload_height = 1;
	image->sh = image->th = 1;
	if (slot == numgltextures)
		numgltextures++;
	lightmap_white_image = image;
}

int GL_LightmapWhiteTexture (void)
{
	return lightmap_white_image ? lightmap_white_image->texnum : 0;
}

/*
================
GL_LoadWal
================
*/
image_t *GL_LoadWal (const char *name, imagetype_t type, qboolean turbulent)
{
	miptex_t *mt;
	int width, height, length;
	unsigned offset;
	size_t bytes;
	image_t *image = NULL;

	length = ri.FS_LoadFile (name, (void **)&mt);
	if (!mt)
		return NULL;
	if (length < (int)sizeof (*mt))
		goto failed;
	width = LittleLong (mt->width);
	height = LittleLong (mt->height);
	if (!R_ImageBytes (width, height, 1, &bytes))
		goto failed;
	/* Only base pixels are consumed; unused disk mip levels need not exist. */
	offset = LittleLong (mt->offsets[0]);
	if (offset < sizeof (*mt) || offset > (unsigned)length ||
		bytes > (size_t)length - offset)
		goto failed;
	image = GL_LoadPic (name, (byte *)mt + offset, width, height, type, 8, turbulent);
	ri.FS_FreeFile (mt);
	return image;
failed:
	ri.Con_Printf (PRINT_ALL, "Invalid WAL data: %s\n", name);
	ri.FS_FreeFile (mt);
	return NULL;
}

#define IMAGES_HASH_SIZE	64
static image_t	*images_hash[IMAGES_HASH_SIZE];

unsigned int hashify (const char *S)
{
  unsigned int hash_PeRlHaSh;

  hash_PeRlHaSh = 0;
  
  while (*S)
  {
	  hash_PeRlHaSh = hash_PeRlHaSh * 33 + (*(S++));
  }
  return hash_PeRlHaSh + (hash_PeRlHaSh >> 5);
}

void Cmd_HashStats_f (void)
{
	int hash;
	image_t *imghash;

	for (hash = 0; hash < IMAGES_HASH_SIZE; hash++)
	{
		ri.Con_Printf (PRINT_ALL, "%d: ", hash);
		for(imghash = images_hash[hash]; imghash; imghash = imghash->hash_next)
		{
			ri.Con_Printf (PRINT_ALL, "*");
		}
		ri.Con_Printf (PRINT_ALL, "\n");
	}
}

/* Adapt Q2PRO's format ordering and type-mask semantics without changing the
 * retained engine/renderer tables. Legacy allowlists continue to restrict the
 * eligible modern formats; r_texture_formats determines their order. */
typedef enum
{
	R_IMAGE_PCX,
	R_IMAGE_WAL,
	R_IMAGE_PNG,
	R_IMAGE_JPG,
	R_IMAGE_TGA,
	R_IMAGE_UNKNOWN
} imageformat_t;

static const char *image_extensions[] = { "pcx", "wal", "png", "jpg", "tga" };

static qboolean R_ImageKey (const char *name, char *key, qboolean strip_extension)
{
	char *dot, *slash;
	size_t i, length;

	if (!name || !(length = strlen (name)) || length >= MAX_QPATH)
		return false;
	for (i = 0; i < length; i++)
	{
		char c = name[i];
		if (c == '\\')
			c = '/';
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		key[i] = c;
	}
	key[length] = 0;
	dot = strrchr (key, '.');
	slash = strrchr (key, '/');
	if (strip_extension && dot && (!slash || dot > slash))
		*dot = 0;
	return *key != 0;
}

static int R_ImageFormatOrder (imagetype_t type, imageformat_t *order)
{
	char *cursor = r_texture_formats && r_texture_formats->string ? r_texture_formats->string : "png jpg tga";
	const char *end = cursor + strlen (cursor);
	const char *token, *letter;
	imageformat_t format;
	qboolean enabled[3];
	int count = 0, i, j;

	enabled[0] = type == it_wall ? load_png_wals : load_png_pics;
	enabled[1] = type == it_wall ? load_jpg_wals : load_jpg_pics;
	enabled[2] = type == it_wall ? load_tga_wals : load_tga_pics;
	/* COM_Parse leaves the cursor beyond the terminator for an unclosed quote. */
	while (cursor && cursor < end)
	{
		token = COM_Parse (&cursor);
		for (i = R_IMAGE_PNG; i <= R_IMAGE_TGA; i++)
			if (!Q_stricmp (token, image_extensions[i]))
				break;
		letter = token;
		do
		{
			if (i <= R_IMAGE_TGA)
				format = (imageformat_t)i;
			else
			{
				switch (*letter)
				{
				case 'p':
				case 'P':
					format = R_IMAGE_PNG;
					break;
				case 'j':
				case 'J':
					format = R_IMAGE_JPG;
					break;
				case 't':
				case 'T':
					format = R_IMAGE_TGA;
					break;
				default:
					format = R_IMAGE_UNKNOWN;
					break;
				}
			}
			if (format != R_IMAGE_UNKNOWN && enabled[format - R_IMAGE_PNG])
			{
				for (j = 0; j < count && order[j] != format; j++)
				{
				}
				if (j == count)
					order[count++] = format;
			}
		} while (i > R_IMAGE_TGA && *letter && *++letter);
	}
	return count;
}

static qboolean R_OverrideImage (imagetype_t type, imageformat_t format,
	const char *key, qboolean font)
{
	int mode = r_override_textures ? r_override_textures->intvalue : 1;
	int mask = r_texture_overrides ? r_texture_overrides->intvalue : -1;
	int bit;

	if (mode < 1 || (mode == 1 && format > R_IMAGE_WAL))
		return false;
	/* Donor bits: pic=1, font=2, skin=4, sprite=8, wall=16, sky=32.
	 * R1Q2 stores fonts as it_pic; the private font cache class keeps custom
	 * fonts distinct from ordinary pictures of the same name. */
	switch (type)
	{
	case it_pic:
		bit = font || !strcmp (key, "pics/conchars") ? 2 : 1;
		break;
	case it_skin:
		bit = 4;
		break;
	case it_sprite:
		bit = 8;
		break;
	case it_wall:
		bit = 16;
		break;
	case it_sky:
		bit = 32;
		break;
	default:
		return false;
	}
	return (mask & bit) != 0;
}

static void R_ImageName (const char *base, imageformat_t format, char *name)
{
	size_t length = strlen (base);
	memcpy (name, base, length);
	name[length] = '.';
	memcpy (name + length + 1, image_extensions[format], 4);
}

static image_t *R_LoadImageFormat (const char *filename, imagetype_t type, imageformat_t format, qboolean turbulent)
{
	static char name[MAX_QPATH];
	byte *pixels = NULL, *palette = NULL;
	int width = 0, height = 0, bits = 32;
	image_t *image;
	size_t length = strlen (filename), i;

	if (length >= sizeof (name) || format >= R_IMAGE_UNKNOWN)
		return NULL;
	memcpy (name, filename, length + 1);
	for (i = 0; i < length; i++)
		if (name[i] == '\\')
			name[i] = '/';
	current_texture_filename = name;
	switch (format)
	{
	case R_IMAGE_WAL:
		return GL_LoadWal (name, type, turbulent);
	case R_IMAGE_PCX:
		LoadPCXImage (name, &pixels, &palette, &width, &height, &bits);
		break;
	case R_IMAGE_PNG:
		LoadPNG (name, &pixels, &width, &height);
		break;
	case R_IMAGE_JPG:
		LoadJPG (name, &pixels, &width, &height);
		break;
	case R_IMAGE_TGA:
		LoadTGA (name, &pixels, &width, &height);
		break;
	default:
		return NULL;
	}
	if (!pixels)
	{
		free (palette);
		return NULL;
	}
	image = GL_LoadPic (name, pixels, width, height, type, bits, turbulent);
	free (pixels);
	free (palette);
	return image;
}

image_t	*GL_FindImageBase (const char *basename, imagetype_t type)
{
	char name[MAX_QPATH], key[MAX_QPATH];
	image_t *image;
	unsigned hash;
	size_t length;

	/* Wall callers pass the BSP texture basename, which can contain dots. */
	if (!R_ImageKey (basename, key, type != it_wall))
		return NULL;
	length = strlen (key);
	if (type == it_wall && strncmp (key, "textures/", 9))
	{
		if (length + 9 >= sizeof (name))
			return NULL;
		memcpy (name, "textures/", 9);
		memcpy (name + 9, key, length + 1);
		memcpy (key, name, length + 10);
	}
	hash = hashify (key) % IMAGES_HASH_SIZE;
	for (image = images_hash[hash]; image; image = image->hash_next)
		if (image->type == type && !image->font && !strcmp (image->basename, key))
		{
			image->registration_sequence = registration_sequence;
			return image;
		}
	return NULL;
}

static image_t *GL_FindImageMaterial (const char *name, const char *basename,
	imagetype_t type, qboolean turbulent, qboolean font)
{
	char key[MAX_QPATH], base[MAX_QPATH], candidate[MAX_QPATH], original[MAX_QPATH];
	const char *extension, *slash, *backslash;
	size_t length, index;
	imageformat_t format, order[3], loaded = R_IMAGE_UNKNOWN, fallback;
	image_t *image = NULL, *cached;
	unsigned hash;
	int count, i, original_width = 0, original_height = 0;
	qboolean override;

	/* Cache by full canonical path, rather than caller-specific aliases. */
	(void)basename;
	if (!R_ImageKey (name, key, true))
		return NULL;
	extension = strrchr (name, '.');
	slash = strrchr (name, '/');
	backslash = strrchr (name, '\\');
	if (backslash && (!slash || backslash > slash))
		slash = backslash;
	if (!extension || extension == name || (slash && extension < slash) || !extension[1])
		return NULL;
	length = extension - name;
	if (length + 4 >= MAX_QPATH)
		return NULL;
	memcpy (base, name, length);
	base[length] = 0;
	for (index = 0; index < length; index++)
		if (base[index] == '\\')
			base[index] = '/';
	for (format = R_IMAGE_PCX; format < R_IMAGE_UNKNOWN; format++)
		if (!Q_stricmp (extension + 1, image_extensions[format]))
			break;
	hash = hashify (key) % IMAGES_HASH_SIZE;
	for (cached = images_hash[hash]; cached; cached = cached->hash_next)
		if (cached->type == type && cached->turbulent == turbulent &&
			cached->font == font && !strcmp (cached->basename, key))
		{
			cached->registration_sequence = registration_sequence;
			return cached;
		}
	override = R_OverrideImage (type, format, key, font);
	if (!override && format != R_IMAGE_UNKNOWN)
	{
		image = R_LoadImageFormat (name, type, format, turbulent);
		if (image)
			loaded = format;
	}
	if (!image)
	{
		count = R_ImageFormatOrder (type, order);
		for (i = 0; i < count; i++)
		{
			if (!override && order[i] == format)
				continue;
			R_ImageName (base, order[i], candidate);
			image = R_LoadImageFormat (candidate, type, order[i], turbulent);
			if (image)
			{
				loaded = order[i];
				break;
			}
		}
	}
	fallback = type == it_wall ? R_IMAGE_WAL : R_IMAGE_PCX;
	if (!image && (override || fallback != format))
	{
		if (fallback == format)
			image = R_LoadImageFormat (name, type, fallback, turbulent);
		else
		{
			R_ImageName (base, fallback, candidate);
			image = R_LoadImageFormat (candidate, type, fallback, turbulent);
		}
		if (image)
			loaded = fallback;
	}
	if (!image)
		return NULL;
	if (format <= R_IMAGE_WAL && loaded > R_IMAGE_WAL &&
		(type != it_pic || FLOAT_NE_ZERO (gl_pic_scale->value)))
	{
		/* Preserve the original extension case for loose files on Unix. */
		memcpy (original, base, length);
		memcpy (original + length, extension, strlen (extension) + 1);
		if (format == R_IMAGE_PCX)
			GetPCXInfo (original, &original_width, &original_height);
		else
			GetWalInfo (original, &original_width, &original_height);
		if (original_width > 0 && original_height > 0)
		{
			image->width = original_width;
			image->height = original_height;
		}
	}
	memcpy (image->basename, key, strlen (key) + 1);
	image->font = font;
	image->hash_next = images_hash[hash];
	images_hash[hash] = image;
	GL_LoadGlowImage (image);
	return image;
}

image_t *GL_FindImage (const char *name, const char *basename, imagetype_t type)
{
	return GL_FindImageMaterial (name, basename, type, false, false);
}

image_t *GL_FindFontImage (const char *name)
{
	return GL_FindImageMaterial (name, name, it_pic, false, true);
}

image_t *GL_FindWallImage (const char *name, const char *basename, qboolean turbulent)
{
	return GL_FindImageMaterial (name, basename, it_wall, turbulent != 0, false);
}

/*
===============
R_RegisterSkin
===============
*/
struct image_s * EXPORT R_RegisterSkin (char *name)
{
	return GL_FindImage (name, name, it_skin);
}

/* Q2PRO optional wall/skin companions are private uploads, not registry entries. */
static byte *R_LoadGlowPixels (const char *name, imagetype_t type, imageformat_t format,
	int *width, int *height)
{
	byte *pixels = NULL, *palette = NULL, *rgba;
	miptex_t *wal = NULL;
	int bits = 32, length;
	unsigned offset;
	size_t bytes, count, i;

	switch (format)
	{
	case R_IMAGE_PNG:
		LoadPNG (name, &pixels, width, height);
		break;
	case R_IMAGE_JPG:
		LoadJPG (name, &pixels, width, height);
		break;
	case R_IMAGE_TGA:
		LoadTGA (name, &pixels, width, height);
		break;
	case R_IMAGE_PCX:
		LoadPCXImage (name, &pixels, &palette, width, height, &bits);
		break;
	case R_IMAGE_WAL:
		length = ri.FS_LoadFile (name, (void **)&wal);
		if (!wal || length < (int)sizeof (*wal))
			goto failed;
		*width = LittleLong (wal->width);
		*height = LittleLong (wal->height);
		if (!R_ImageBytes (*width, *height, 1, &bytes))
			goto failed;
		/* Glow decoding also reads only the base level. */
		offset = LittleLong (wal->offsets[0]);
		if (offset < sizeof (*wal) || offset > (unsigned)length ||
			bytes > (size_t)length - offset)
			goto failed;
		pixels = malloc (bytes);
		if (!pixels)
			goto failed;
		memcpy (pixels, (byte *)wal + offset, bytes);
		bits = 8;
		break;
	default:
		return NULL;
	}
	free (palette);
	palette = NULL;
	if (wal)
	{
		ri.FS_FreeFile (wal);
		wal = NULL;
	}
	if (!pixels || !R_ImageBytes (*width, *height, 4, &bytes))
		goto failed;
	count = bytes / 4;
	if (bits == 8)
	{
		if (type == it_skin)
			R_FloodFillSkin (pixels, *width, *height);
		rgba = malloc (bytes);
		if (!rgba)
			goto failed;
		for (i = 0; i < count; i++)
			memcpy (rgba + i * 4, &d_8to24table[pixels[i]], 4);
		free (pixels);
		pixels = rgba;
	}
	else if (bits != 32)
		goto failed;
	if (type == it_skin)
	{
		for (i = 0; i < count; i++)
		{
			float alpha = pixels[i * 4 + 3] / 255.0f;
			pixels[i * 4 + 0] = (byte)(pixels[i * 4 + 0] * alpha);
			pixels[i * 4 + 1] = (byte)(pixels[i * 4 + 1] * alpha);
			pixels[i * 4 + 2] = (byte)(pixels[i * 4 + 2] * alpha);
		}
	}
	return pixels;
failed:
	if (wal)
		ri.FS_FreeFile (wal);
	free (pixels);
	free (palette);
	return NULL;
}

/* Decode sky atlases/faces without publishing a 2D registry entry. */
byte *GL_LoadSkyPixels (const char *basename, int *width, int *height)
{
	imageformat_t order[3];
	char key[MAX_QPATH], candidate[MAX_QPATH];
	const char *saved_filename = current_texture_filename;
	byte *pixels = NULL;
	qboolean override;
	int count, i;
	*width = *height = 0;
	if (!R_ImageKey (basename, key, false) || strlen (key) + 4 >= sizeof (candidate))
		return NULL;
	override = R_OverrideImage (it_sky, R_IMAGE_TGA, key, false);
	current_texture_filename = candidate;
	if (!override)
	{
		R_ImageName (key, R_IMAGE_TGA, candidate);
		pixels = R_LoadGlowPixels (candidate, it_sky, R_IMAGE_TGA, width, height);
	}
	count = R_ImageFormatOrder (it_sky, order);
	for (i = 0; i < count && !pixels; i++)
	{
		if (!override && order[i] == R_IMAGE_TGA)
			continue;
		R_ImageName (key, order[i], candidate);
		pixels = R_LoadGlowPixels (candidate, it_sky, order[i], width, height);
	}
	if (!pixels)
	{
		R_ImageName (key, R_IMAGE_PCX, candidate);
		pixels = R_LoadGlowPixels (candidate, it_sky, R_IMAGE_PCX, width, height);
	}
	current_texture_filename = saved_filename;
	return pixels;
}

void GL_LoadGlowImage (image_t *image)
{
	image_t temporary;
	imageformat_t order[3], format;
	const char *extension, *saved_filename;
	char base[MAX_QPATH], candidate[MAX_QPATH];
	byte *pixels = NULL;
	size_t length;
	int count, i, width = 0, height = 0, result;
	GLint texture_width = 0, texture_height = 0;
	GLuint texture;

	if (!image || image->glowtexnum || (image->type != it_skin && image->type != it_wall) ||
		!GL_GlowmapsAvailable () || !qglIsTexture || !qglGetTexLevelParameteriv)
		return;
	extension = strrchr (image->name, '.');
	if (!extension)
		return;
	length = extension - image->name;
	if (length + sizeof ("_glow.pcx") > sizeof (candidate))
		return;
	memcpy (base, image->name, length);
	memcpy (base + length, "_glow", 6);
	R_ImageName (base, R_IMAGE_PCX, candidate);
	texture = TEXNUM_IMAGES + MAX_GLTEXTURES + 1 + (GLuint)(image - gltextures);
	/* The preceding reserved name belongs to underwater post-processing. */
	if (qglIsTexture (texture))
		return;
	saved_filename = current_texture_filename;
	current_texture_filename = candidate;
	if (!R_OverrideImage (image->type, R_IMAGE_PCX, base, false))
		pixels = R_LoadGlowPixels (candidate, image->type, R_IMAGE_PCX, &width, &height);
	if (!pixels)
	{
		count = R_ImageFormatOrder (image->type, order);
		for (i = 0; i < count && !pixels; i++)
		{
			R_ImageName (base, order[i], candidate);
			pixels = R_LoadGlowPixels (candidate, image->type, order[i], &width, &height);
		}
	}
	if (!pixels)
	{
		format = R_IMAGE_PCX;
		R_ImageName (base, format, candidate);
		pixels = R_LoadGlowPixels (candidate, image->type, format, &width, &height);
	}
	if (pixels)
	{
		memset (&temporary, 0, sizeof (temporary));
		temporary.type = image->type;
		temporary.glow_companion = true;
		GL_Bind (texture);
		result = GL_Upload32 ((unsigned *)pixels, width, height, true, 32, &temporary);
		if (result >= 0)
		{
			qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texture_width);
			qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texture_height);
			if (qglGetError () != GL_NO_ERROR || texture_width != upload_width ||
				texture_height != upload_height)
				result = -1;
		}
		if (result < 0)
			qglDeleteTextures (1, &texture);
		else
			image->glowtexnum = texture;
		GL_Bind (image->texnum);
		free (pixels);
	}
	current_texture_filename = saved_filename;
}

/* A live r_glowmaps enable must visit images cached while the option was off.
 * Limit both slot traversal and file/decode attempts in each frame. */
static void GL_RefreshGlowImages (void)
{
	int scanned = 0, attempted = 0, unit, saved_binding = 0, restored_binding = 0;
	unsigned saved_cache = 0;
	image_t *image;
	qboolean available;

	if (r_glowmaps && r_glowmaps->modified)
	{
		glow_refresh_available = false;
		glow_refresh_cursor = 0;
		r_glowmaps->modified = false;
	}
	available = GL_GlowmapsAvailable ();
	if (!available)
	{
		glow_refresh_available = false;
		glow_refresh_cursor = 0;
		return;
	}
	if (!glow_refresh_available)
	{
		glow_refresh_available = true;
		glow_refresh_cursor = 0;
	}
	if (glow_refresh_cursor >= numgltextures || !qglGetIntegerv ||
		!qglBindTexture || !qglGetError || !qglIsTexture || !qglGetTexLevelParameteriv)
		return;

	unit = gl_state.currenttmu;
	for (; glow_refresh_cursor < numgltextures && scanned < 64 && attempted < 4;
		 glow_refresh_cursor++, scanned++)
	{
		image = &gltextures[glow_refresh_cursor];
		if (!image->registration_sequence || !image->texnum || image->glowtexnum ||
			(image->type != it_wall && image->type != it_skin))
			continue;
		if (!attempted)
		{
			Draw_Flush ();
			qglGetIntegerv (GL_TEXTURE_BINDING_2D, &saved_binding);
			if (qglGetError () != GL_NO_ERROR || saved_binding < 0)
				return;
			saved_cache = gl_state.currenttextures[unit];
		}
		GL_LoadGlowImage (image);
		attempted++;
	}
	if (!attempted)
		return;
	qglBindTexture (GL_TEXTURE_2D, (GLuint)saved_binding);
	qglGetIntegerv (GL_TEXTURE_BINDING_2D, &restored_binding);
	if (qglGetError () == GL_NO_ERROR && restored_binding == saved_binding)
		gl_state.currenttextures[unit] = saved_cache;
	else
	{
		gl_state.currenttextures[unit] = 0xFFFFFFFF;
		ri.Con_Printf (PRINT_ALL, "Glow companion refresh could not restore texture binding.\n");
	}
}

/*
===============
GL_ImageList_f
===============
*/
void	GL_ImageList_f (void)
{
	int		i;
	image_t	*image;
	int		texels;
	int		spr = 0;
	int		ski = 0;
	int		wal = 0;
	int		pic = 0;
	int		mis = 0;
	/*const char *palstrings[2] =
	{
		"RGB",
		"PAL"
	};*/

	ri.Con_Printf (PRINT_ALL, "------------------\n");
	texels = 0;

	for (i=0, image=gltextures ; i<numgltextures ; i++, image++)
	{
		if (!image->texnum)
			continue;
		texels += image->upload_width*image->upload_height;
		switch (image->type)
		{
		case it_skin:
			ski++;
			ri.Con_Printf (PRINT_ALL, "M");
			break;
		case it_sprite:
			spr++;
			ri.Con_Printf (PRINT_ALL, "S");
			break;
		case it_wall:
			wal++;
			ri.Con_Printf (PRINT_ALL, "W");
			break;
		case it_pic:
			pic++;
			ri.Con_Printf (PRINT_ALL, "P");
			break;
		default:
			mis++;
			ri.Con_Printf (PRINT_ALL, " ");
			break;
		}

		ri.Con_Printf (PRINT_ALL,  " %3i x %3i: %s (%d bytes)\n",
			image->upload_width, image->upload_height, image->name, image->upload_width * image->upload_height * sizeof(int));
	}

	ri.Con_Printf (PRINT_ALL, "%d skins (M), %d sprites (S), %d world textures (W), %d pics (P), %d misc.\n", ski, spr, wal, pic, mis);

	/*
	ri.Con_Printf (PRINT_ALL, "ImageCache: %d level 1 hits, %d level 2 hits, %d cache misses. Efficiency = %.2f%%\n",
		l1cachehits,
		l2cachehits,
		cachemisses,
		((float)(l1cachehits+(l2cachehits/4)) / (float)(l1cachehits+l2cachehits+cachemisses) * 100.0));
	*/

	ri.Con_Printf (PRINT_ALL, "Total texel count (not counting mipmaps): %i (%.2f MB)\n", texels, (texels * sizeof(int)) / 1024.0f / 1024.0f);
}

/*
================
GL_FreeUnusedImages

Any image that was not touched on this registration sequence
will be freed.
================
*/
void GL_FreeUnusedImages (void)
{
	int		count;
	int		i;
	int		hash;
	image_t	*image;
	image_t	*hashstart;
	image_t	**prev;

	Draw_Flush ();

	if (scaled_buffer)
	{
		free (scaled_buffer);
		scaled_buffer = NULL;
		scaled_buffer_size = 0;
	}

	if (mipmap_buffer)
	{
		free (mipmap_buffer);
		mipmap_buffer = NULL;
		mipmap_buffer_size = 0;
	}

	// never free r_notexture or particle texture
	r_notexture->registration_sequence = registration_sequence;
	r_particletexture->registration_sequence = registration_sequence;

	count = 0;

	for (i=0, image=gltextures ; i<numgltextures ; i++, image++)
	{
		if (image->particle_shape && image->registration_sequence)
			image->registration_sequence = registration_sequence;
		if (image->registration_sequence == registration_sequence)
			continue;		// used this sequence

		if (!image->registration_sequence)
			continue;		// free image_t slot

		if (image->type == it_pic)
			continue;		// don't free pics

		hash = hashify (image->basename) % IMAGES_HASH_SIZE;
		/*prev = NULL;
		for(hashstart = images_hash[hash]; hashstart; hashstart = hashstart->hash_next)
		{
			if (hashstart == image)
			{
				if (prev)
					prev->hash_next = hashstart->hash_next;
				else
					images_hash[hash] = images_hash[hash]->hash_next;
				break;
			}
			prev = hashstart;
		}*/
		
		prev = &images_hash[hash];
		for (;;)
		{
			hashstart = *prev;
			if (!hashstart)
				break;
			if (hashstart == image)
			{
				*prev = hashstart->hash_next;
				break;
			}
			prev = &hashstart->hash_next;
		}
		// free it

		count++;
		if (image->glowtexnum)
			qglDeleteTextures (1, &image->glowtexnum);
		qglDeleteTextures (1, &image->texnum);
		memset (image, 0, sizeof(*image));
	}

	ri.Con_Printf (PRINT_DEVELOPER, "GL_FreeUnusedImages: freed %d images\n", count);
}


/*
===============
Draw_GetPalette
===============
*/
int Draw_GetPalette (void)
{
	int		i;
	int		r, g, b;
	unsigned	v;
	byte	*pic, *pal;
	int		width, height;

	// get the palette

	LoadPCX ("pics/colormap.pcx", &pic, &pal, &width, &height);
	if (!pal)
		ri.Sys_Error (ERR_FATAL, "R1GL was unable to load the colormap (pics/colormap.pcx).\n\nThis file is required for Quake II to function properly. Please make sure that all files are in the correct directories and that baseq2/pak0.pak is installed and readable (not hidden or system).");

	for (i=0 ; i<256 ; i++)
	{
		r = pal[i*3+0];
		g = pal[i*3+1];
		b = pal[i*3+2];
		
		v = (255<<24) + (r<<0) + (g<<8) + (b<<16);
		d_8to24table[i] = LittleLong(v);

		d_8to24float[i][0] = r / 255.0f;
		d_8to24float[i][1] = g / 255.0f;
		d_8to24float[i][2] = b / 255.0f;
		d_8to24float[i][3] = 0;
	}

	d_8to24table[255] &= LittleLong(0xffffff);	// 255 is transparent

	free (pic);
	free (pal);

	return 0;
}

qboolean GL_ImageShaderIntensity (void)
{
	return image_shader_intensity;
}

qboolean GL_ImageColorProfileReady (void)
{
	return image_color_profile_ready;
}

qboolean GL_HardwareGammaActive (void)
{
	return gl_state.hwgamma && gamma_requested;
}

float GL_ImageIntensityScale (void)
{
	float value;
	if (!image_shader_intensity)
		return 0;
	value = GL_TextureFiniteValue (intensity, 2);
	if (value < 1)
		value = 1;
	if (value > 5)
		value = 5;
	return value;
}

void GL_ImageShaderIntensityFailure (void)
{
	if (!image_shader_intensity || image_intensity_failed)
		return;
	image_intensity_failed = true;
	/* Reload every cached image before returning to baked CPU intensity. */
	ri.Con_Printf (PRINT_ALL, "Material shader failed; restarting with CPU image intensity.\n");
	ri.Cvar_SetValue ("gl_shaders", 0);
	ri.Cvar_Get ("vid_ref", "gl", CVAR_ARCHIVE)->modified = true;
}

static void GL_BuildGammaTable (float g)
{
	int i;
	if (!(g > 0 && g <= FLT_MAX))
		g = 1;
	for (i = 0; i < 256; i++)
	{
		float value = g == 1 ? (float)i : 255.0f * (float)pow (((float)i + 0.5f) / 255.5f, g) + 0.5f;
		if (value < 0)
			value = 0;
		if (value > 255)
			value = 255;
		gammatable[i] = (byte)Q_ftol (value);
		gammaintensitytable[i] = intensitytable[gammatable[i]];
	}
}

static void GL_BuildColorTables (float g)
{
	int i, j;
	float value = GL_TextureFiniteValue (intensity, 2);
	if (value < 1)
		value = 1;
	if (value > 5)
		value = 5;
	ri.Cvar_SetValue ("intensity", value);
	gl_state.inverse_intensity = image_shader_intensity ? 1 : 1 / value;
	for (i = 0; i < 256; i++)
	{
		j = image_shader_intensity ? i : (int)(i * value);
		intensitytable[i] = j > 255 ? 255 : j;
	}
	GL_BuildGammaTable (g);
}

static void GL_InitImageColorTables (float g)
{
	image_color_profile_ready = false;
	image_intensity_failed = false;
	image_shader_intensity = (GL_TextureFiniteValue (gl_texture_lighting_mode, 0) == 0) &&
		GL_ShaderIntensityReady ();
	GL_BuildColorTables (g);
	image_color_profile_ready = true;
}

static void GL_InitGamma (void)
{
	vid_hwgamma = ri.Cvar_Get ("vid_hwgamma", "0", 0);
	gamma_requested = GL_TextureFiniteValue (vid_hwgamma, 0) != 0;
	gl_state.hwgamma = gamma_requested && GLimp_InitGamma (gammatable);
	if (gamma_requested && !gl_state.hwgamma)
	{
		ri.Con_Printf (PRINT_ALL, "Hardware gamma unavailable; using texture gamma.\n");
		ri.Cvar_SetValue ("vid_hwgamma", 0);
		gamma_requested = false;
	}
	vid_hwgamma->modified = vid_gamma->modified = false;
}

void GL_UpdateGamma (void)
{
	if (vid_hwgamma->modified)
	{
		if ((GL_TextureFiniteValue (vid_hwgamma, 0) != 0) != gamma_requested)
			ri.Con_Printf (PRINT_ALL, "vid_hwgamma changes require vid_restart.\n");
		vid_hwgamma->modified = false;
	}
	if (vid_gamma->modified)
	{
		vid_gamma->modified = false;
		if (gl_state.hwgamma)
		{
			float g = GL_TextureFiniteValue (vid_gamma, 1);
			if (FLOAT_NE_ZERO (gl_overbrights->value) ||
				(gl_config.renderer & (GL_RENDERER_VOODOO | GL_RENDERER_VOODOO2)))
				g = 1;
			GL_BuildGammaTable (g);
		}
		else
			ri.Con_Printf (PRINT_ALL, "vid_gamma changes require vid_restart with texture gamma.\n");
	}
	if (gl_state.hwgamma && !GLimp_UpdateGamma (gammatable))
	{
		/* Cached raw textures must be reloaded before selecting CPU gamma. */
		ri.Con_Printf (PRINT_ALL, "Hardware gamma failed; restarting with texture gamma.\n");
		ri.Cvar_SetValue ("vid_hwgamma", 0);
		vid_hwgamma->modified = false;
		gamma_requested = false;
		ri.Cvar_Get ("vid_ref", "gl", CVAR_ARCHIVE)->modified = true;
	}
}

static qboolean GL_UploadRawFrameData (int cols, int rows, int stride,
	const byte *data, size_t data_size, const unsigned *palette)
{
	int width, height, maximum, power, x, y, sx, sy;
	int components = palette ? 1 : 4;
	size_t bytes, row_bytes;
	const byte *pixel;
	byte *output;
	if (!data || !R_ImageBytes (cols, rows, components, &bytes))
		return false;
	row_bytes = (size_t)cols * components;
	/* Check before multiplying so a decoder's padded stride cannot wrap on x86.
	 * The last row does not need trailing padding. Negative strides are normalized
	 * by the decoder, keeping the entire borrowed span explicit at this boundary. */
	if (stride < 0 || (size_t)stride < row_bytes || data_size < row_bytes ||
		(size_t)(rows - 1) > (data_size - row_bytes) / (size_t)stride)
		return false;
	maximum = gl_config.max_texture_size;
	if (maximum < 1)
		return false;
	if (maximum > MAX_TEXTURE_DIMENSIONS)
		maximum = MAX_TEXTURE_DIMENSIONS;
	width = cols;
	height = rows;
	if (!gl_config.r1gl_GL_ARB_texture_non_power_of_two)
	{
		for (power = 1; power <= maximum / 2; power <<= 1)
			;
		maximum = power;
		for (width = 1; width < cols; width <<= 1)
			;
		for (height = 1; height < rows; height <<= 1)
			;
	}
	if (width > maximum)
		width = maximum;
	if (height > maximum)
		height = maximum;
	if (!R_ImageBytes (width, height, 4, &bytes) ||
		!R_ResizeImageBuffer ((void **)&raw_buffer, &raw_buffer_size, bytes))
		return false;
	for (y = 0; y < height; y++)
	{
		sy = ((y * 2 + 1) * rows) / (height * 2);
		for (x = 0; x < width; x++)
		{
			sx = ((x * 2 + 1) * cols) / (width * 2);
			pixel = data + (size_t)sy * stride + (size_t)sx * components;
			if (palette)
				raw_buffer[y * width + x] = palette[*pixel];
			else
			{
				output = (byte *)&raw_buffer[y * width + x];
				output[0] = pixel[0];
				output[1] = pixel[1];
				output[2] = pixel[2];
				output[3] = 255;
			}
		}
	}
	Draw_Flush ();
	GL_Bind (0);
	if (!GL_ClearErrors ())
		return false;
	/* Raw media bypasses texture gamma/intensity, as in Q2PRO. */
	qglTexImage2D (GL_TEXTURE_2D, 0, gl_tex_solid_format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, raw_buffer);
	if (qglGetError () != GL_NO_ERROR)
	{
		ri.Con_Printf (PRINT_ALL, "Could not upload raw cinematic frame.\n");
		return false;
	}
	return true;
}

qboolean GL_UploadRawFrame (int cols, int rows, const byte *data, const unsigned *palette)
{
	size_t bytes;
	color_frame_ready = false;
	if (!palette || !R_ImageBytes (cols, rows, 1, &bytes))
		return false;
	return GL_UploadRawFrameData (cols, rows, cols, data, bytes, palette);
}

qboolean GL_UploadColorFrame (int cols, int rows, int stride,
	const byte *data, unsigned int data_size)
{
	color_frame_ready = false;
	if (gl_state.currenttmu)
		return false;
	color_frame_ready = GL_UploadRawFrameData (cols, rows, stride, data, data_size, NULL);
	return color_frame_ready;
}

qboolean GL_BindColorFrame (void)
{
	if (!color_frame_ready || gl_state.currenttmu)
		return false;
	Draw_Flush ();
	GL_Bind (0);
	return true;
}

/*
===============
GL_InitImages
===============
*/
void	GL_InitImages (void)
{
	float	g = vid_gamma->value;
	lightmap_white_image = NULL;

	registration_sequence = 1;

#ifdef REF_HARD_LINKED
	/* Texture lookups and context state belonged to the unloaded renderer DLL. */
	texenv_modes[0] = texenv_modes[1] = 0xFFFFFFFF;
	numgltextures = 0;
	memset (gltextures, 0, sizeof (gltextures));
	memset (images_hash, 0, sizeof (images_hash));
	memset (scrap_allocated, 0, sizeof (scrap_allocated));
	memset (scrap_rgba, 0, sizeof (scrap_rgba));
	scrap_dirty = false;
#endif

#ifdef RB_IMAGE_CACHE
	rb = rbinit (strcmp);
#endif
	glow_refresh_cursor = 0;
	glow_refresh_available = GL_GlowmapsAvailable ();
	if (r_glowmaps)
		r_glowmaps->modified = false;

	// init intensity conversions
	intensity = ri.Cvar_Get ("intensity", "2", CVAR_ARCHIVE);

	gl_contrast = ri.Cvar_Get ("gl_contrast", "0.8", 0);
	gl_texture_lighting_mode = ri.Cvar_Get ("gl_texture_lighting_mode", "0", 0);

	if (gl_contrast->value < 0.5f)
		ri.Cvar_SetValue ("gl_contrast", 0.5f);
	else if (gl_contrast->value > 1.5f)
		ri.Cvar_SetValue ("gl_contrast", 1.5f);

	if (!(g > 0 && g <= FLT_MAX))
		g = 1;
	if (FLOAT_NE_ZERO(gl_overbrights->value))
		g = 1.0;

	Draw_GetPalette ();

	if ( qglColorTableEXT )
	{
		ri.FS_LoadFile( "pics/16to8.dat", (void *)&gl_state.d_16to8table );
		if ( !gl_state.d_16to8table )
			ri.Sys_Error( ERR_FATAL, "Couldn't load pics/16to8.pcx");
	}

	if ( gl_config.renderer & ( GL_RENDERER_VOODOO | GL_RENDERER_VOODOO2 ) )
	{
		g = 1.0F;
	}

	GL_InitImageColorTables (g);
	GL_InitGamma ();
}

/*
===============
GL_ShutdownImages
===============
*/
void	GL_ShutdownImages (void)
{
	int		i;
	image_t	*image;
	lightmap_white_image = NULL;

	free (raw_buffer);
	raw_buffer = NULL;
	raw_buffer_size = 0;
	color_frame_ready = false;
	image_shader_intensity = image_color_profile_ready = image_intensity_failed = false;

#ifdef RB_IMAGE_CACHE
	DestroyImageCache ();
#endif

	for (i=0, image=gltextures ; i<numgltextures ; i++, image++)
	{
		if (!image->registration_sequence)
			continue;		// free image_t slot
		// free it
		if (image->glowtexnum)
			qglDeleteTextures (1, &image->glowtexnum);
		qglDeleteTextures (1, &image->texnum);
		memset (image, 0, sizeof(*image));
	}

#ifdef REF_HARD_LINKED
	texenv_modes[0] = texenv_modes[1] = 0xFFFFFFFF;
	numgltextures = 0;
	memset (images_hash, 0, sizeof (images_hash));
	free (scaled_buffer);
	free (mipmap_buffer);
	scaled_buffer = NULL;
	mipmap_buffer = NULL;
	scaled_buffer_size = mipmap_buffer_size = 0;
#endif
}

/* Renderer API-call accounting for r_speeds 5. Wrap the already loaded core
 * entry points only while a frame is being measured; ordinary frames keep
 * their original direct QGL calls. These are calls and nominal RGBA8 source
 * bytes, not effective state changes or driver texture memory. */
typedef struct
{
	unsigned long long binds_2d, binds_cube;
	unsigned long long images, empty_images, subimages;
	double image_texels, subimage_texels, rgba_source_bytes;
	unsigned long long enables, disables, blends, depth_masks, color_masks;
} r_account_counts_t;

static r_account_counts_t r_account_frame, r_account_total;
static unsigned r_account_frames;
static qboolean r_account_active;
static void (APIENTRY *r_account_bind) (GLenum, GLuint);
static void (APIENTRY *r_account_image) (GLenum, GLint, GLint, GLsizei, GLsizei,
	GLint, GLenum, GLenum, const GLvoid *);
static void (APIENTRY *r_account_subimage) (GLenum, GLint, GLint, GLint,
	GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
static void (APIENTRY *r_account_enable) (GLenum);
static void (APIENTRY *r_account_disable) (GLenum);
static void (APIENTRY *r_account_blend) (GLenum, GLenum);
static void (APIENTRY *r_account_depth_mask) (GLboolean);
static void (APIENTRY *r_account_color_mask) (GLboolean, GLboolean, GLboolean, GLboolean);

static void APIENTRY R_AccountBind (GLenum target, GLuint texture)
{
	if (target == GL_TEXTURE_2D)
		r_account_frame.binds_2d++;
	else if (target == GL_TEXTURE_CUBE_MAP)
		r_account_frame.binds_cube++;
	r_account_bind (target, texture);
}

static void APIENTRY R_AccountImage (GLenum target, GLint level, GLint internal,
	GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type,
	const GLvoid *pixels)
{
	r_account_frame.images++;
	if (!pixels)
		r_account_frame.empty_images++;
	if (width > 0 && height > 0)
	{
		r_account_frame.image_texels += (double)width * height;
		if (pixels && format == GL_RGBA && type == GL_UNSIGNED_BYTE)
			r_account_frame.rgba_source_bytes += (double)width * height * 4;
	}
	r_account_image (target, level, internal, width, height, border, format, type, pixels);
}

static void APIENTRY R_AccountSubImage (GLenum target, GLint level, GLint x,
	GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
	const GLvoid *pixels)
{
	r_account_frame.subimages++;
	if (width > 0 && height > 0)
	{
		r_account_frame.subimage_texels += (double)width * height;
		if (pixels && format == GL_RGBA && type == GL_UNSIGNED_BYTE)
			r_account_frame.rgba_source_bytes += (double)width * height * 4;
	}
	r_account_subimage (target, level, x, y, width, height, format, type, pixels);
}

static void APIENTRY R_AccountEnable (GLenum cap)
{
	r_account_frame.enables++;
	r_account_enable (cap);
}

static void APIENTRY R_AccountDisable (GLenum cap)
{
	r_account_frame.disables++;
	r_account_disable (cap);
}

static void APIENTRY R_AccountBlend (GLenum source, GLenum destination)
{
	r_account_frame.blends++;
	r_account_blend (source, destination);
}

static void APIENTRY R_AccountDepthMask (GLboolean enabled)
{
	r_account_frame.depth_masks++;
	r_account_depth_mask (enabled);
}

static void APIENTRY R_AccountColorMask (GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
	r_account_frame.color_masks++;
	r_account_color_mask (r, g, b, a);
}

static void R_AccountDetach (void)
{
	if (!r_account_active)
		return;
	if (qglBindTexture == R_AccountBind)
		qglBindTexture = r_account_bind;
	if (qglTexImage2D == R_AccountImage)
		qglTexImage2D = r_account_image;
	if (qglTexSubImage2D == R_AccountSubImage)
		qglTexSubImage2D = r_account_subimage;
	if (qglEnable == R_AccountEnable)
		qglEnable = r_account_enable;
	if (qglDisable == R_AccountDisable)
		qglDisable = r_account_disable;
	if (qglBlendFunc == R_AccountBlend)
		qglBlendFunc = r_account_blend;
	if (qglDepthMask == R_AccountDepthMask)
		qglDepthMask = r_account_depth_mask;
	if (qglColorMask == R_AccountColorMask)
		qglColorMask = r_account_color_mask;
	r_account_active = false;
}

void R_AccountBeginFrame (void)
{
	if (r_account_active)
		R_AccountDetach (); /* discard an unmatched prior BeginFrame */
	if (!r_speeds || r_speeds->value < 5)
	{
		memset (&r_account_total, 0, sizeof (r_account_total));
		r_account_frames = 0;
		return;
	}
	if (!qglBindTexture || !qglTexImage2D || !qglTexSubImage2D ||
		!qglEnable || !qglDisable || !qglBlendFunc || !qglDepthMask || !qglColorMask)
		return;
	memset (&r_account_frame, 0, sizeof (r_account_frame));
	r_account_bind = qglBindTexture;
	r_account_image = qglTexImage2D;
	r_account_subimage = qglTexSubImage2D;
	r_account_enable = qglEnable;
	r_account_disable = qglDisable;
	r_account_blend = qglBlendFunc;
	r_account_depth_mask = qglDepthMask;
	r_account_color_mask = qglColorMask;
	qglBindTexture = R_AccountBind;
	qglTexImage2D = R_AccountImage;
	qglTexSubImage2D = R_AccountSubImage;
	qglEnable = R_AccountEnable;
	qglDisable = R_AccountDisable;
	qglBlendFunc = R_AccountBlend;
	qglDepthMask = R_AccountDepthMask;
	qglColorMask = R_AccountColorMask;
	r_account_active = true;
}

void R_AccountEndFrame (void)
{
	if (!r_account_active)
		return;
	R_AccountDetach ();
	r_account_total.binds_2d += r_account_frame.binds_2d;
	r_account_total.binds_cube += r_account_frame.binds_cube;
	r_account_total.images += r_account_frame.images;
	r_account_total.empty_images += r_account_frame.empty_images;
	r_account_total.subimages += r_account_frame.subimages;
	r_account_total.image_texels += r_account_frame.image_texels;
	r_account_total.subimage_texels += r_account_frame.subimage_texels;
	r_account_total.rgba_source_bytes += r_account_frame.rgba_source_bytes;
	r_account_total.enables += r_account_frame.enables;
	r_account_total.disables += r_account_frame.disables;
	r_account_total.blends += r_account_frame.blends;
	r_account_total.depth_masks += r_account_frame.depth_masks;
	r_account_total.color_masks += r_account_frame.color_masks;
	if (++r_account_frames < 120)
		return;
	ri.Con_Printf (PRINT_ALL,
		"r_speeds 5: %u frames, GL calls: bind 2D %.0f/cube %.0f, TexImage2D %.0f (NULL %.0f, %.0f texels), TexSubImage2D %.0f (%.0f texels), supplied RGBA8 %.0f bytes\n",
		r_account_frames, (double)r_account_total.binds_2d,
		(double)r_account_total.binds_cube, (double)r_account_total.images,
		(double)r_account_total.empty_images, r_account_total.image_texels,
		(double)r_account_total.subimages, r_account_total.subimage_texels,
		r_account_total.rgba_source_bytes);
	ri.Con_Printf (PRINT_ALL,
		"r_speeds 5 state calls: enable %.0f, disable %.0f, blendFunc %.0f, depthMask %.0f, colorMask %.0f\n",
		(double)r_account_total.enables, (double)r_account_total.disables,
		(double)r_account_total.blends, (double)r_account_total.depth_masks,
		(double)r_account_total.color_masks);
	memset (&r_account_total, 0, sizeof (r_account_total));
	r_account_frames = 0;
}

void R_AccountShutdown (void)
{
	R_AccountDetach ();
	memset (&r_account_frame, 0, sizeof (r_account_frame));
	memset (&r_account_total, 0, sizeof (r_account_total));
	r_account_frames = 0;
}
