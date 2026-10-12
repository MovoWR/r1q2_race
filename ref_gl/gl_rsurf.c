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
// GL_RSURF.C: surface-related refresh code
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>

#include "gl_local.h"
#include "gl_jump.h"

static vec3_t	modelorg;		// relative to viewpoint

msurface_t	*r_alpha_surfaces;

/* An inline model can be drawn more than once. Its surfaces are shared, so
 * their texturechain links cannot represent separate deferred instances. */
typedef struct
{
	entity_t entity;
	model_t *model;
	vec3_t modelorg;
} alphabrushinstance_t;

static alphabrushinstance_t alpha_brush_instances[MAX_ENTITIES];
static int alpha_brush_count;
static int alpha_world_frame;

void R_ClearAlphaSurfaces (void)
{
	R_EndInlineDlights ();
	r_alpha_surfaces = NULL;
	alpha_brush_count = 0;
	alpha_world_frame = 0;
}

#define LIGHTMAP_BYTES 4
#define CLASSIC_LIGHTMAP_BLOCK 128
#define DECOUPLED_LIGHTMAP_BLOCK 1024
#define MAX_LIGHTMAP_EXTENTS 513
#define MAX_LIGHTMAPS 128
#define LIGHTMAP_ATLAS_BUDGET (64 * 1024 * 1024)

int		c_visible_lightmaps;
int		c_visible_textures;

#define GL_LIGHTMAP_FORMAT GL_RGBA
#ifndef GL_TEXTURE_RED_SIZE
#define GL_TEXTURE_RED_SIZE 0x805C
#define GL_TEXTURE_GREEN_SIZE 0x805D
#define GL_TEXTURE_BLUE_SIZE 0x805E
#endif

typedef struct
{
	int internal_format;
	int upload_type;
	qboolean packed10;
	int	current_lightmap_texture;
	int block_size;
	int max_static_lightmaps;
	qboolean dirty;

	msurface_t	*lightmap_surfaces[MAX_LIGHTMAPS];

	int allocated[DECOUPLED_LIGHTMAP_BLOCK];

	// the lightmap texture data needs to be kept in
	// main memory so texsubimage can update properly
	byte lightmap_buffer[LIGHTMAP_BYTES * DECOUPLED_LIGHTMAP_BLOCK * DECOUPLED_LIGHTMAP_BLOCK];
	byte surface_buffer[LIGHTMAP_BYTES * MAX_LIGHTMAP_EXTENTS * MAX_LIGHTMAP_EXTENTS];
	byte *static_backing[MAX_LIGHTMAPS];
} gllightmapstate_t;

static gllightmapstate_t gl_lms;

/* Vertex-light colors are private renderer geometry; the refdef and wire
 * formats remain unchanged. Keep ordinary lightmaps for an instant fallback. */
static model_t *vertexlight_model;
static qboolean vertexlight_ready;
static float vertexlight_modulate, vertexlight_brightness;
static unsigned vertexlight_sequence;
static float vertexlight_styles[MAX_LIGHTSTYLES][3];
static byte vertexlight_used_styles[MAX_LIGHTSTYLES];
static qboolean vertexlight_disabled_multitexture;
static unsigned vertexlight_refreshed_vertices, vertexlight_vbo_uploaded_bytes;

static qboolean GL_VertexLightRequested (void)
{
	float value = gl_vertexlight ? gl_vertexlight->value : 0;
	return value >= -FLT_MAX && value <= FLT_MAX && FLOAT_NE_ZERO (value);
}

static qboolean GL_VertexLightSurface (const msurface_t *surf)
{
#ifdef SURF_ALPHATEST
	/* A masked face must retain its dedicated clipping draw when available. */
	if (surf && surf->texinfo && (surf->texinfo->flags & SURF_ALPHATEST))
		return false;
#endif
	return GL_VertexLightRequested () && vertexlight_ready &&
		vertexlight_model == r_worldmodel && surf && surf->samples &&
		surf->dlightframe != r_framecount &&
		!(currententity && (currententity->flags & RF_TRANSLUCENT)) &&
		surf->polys && !(surf->flags & (SURF_DRAWTURB | SURF_BSPX_NODRAW)) &&
		!(surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP));
}

static qboolean GL_DecoupledSurfaceLit (const msurface_t *surf);

/* Unlike an allocated fallback-white atlas cell, donor light_m denotes
 * sampled lighting. Vertex-lit and fullbright faces have no such material. */
qboolean GL_SurfaceLightmapView (const msurface_t *surf)
{
	if (!gl_lightmap || !gl_lightmap->intvalue || !surf || !surf->texinfo ||
		!surf->samples || !surf->polys || FLOAT_NE_ZERO (r_fullbright->value) ||
		(surf->flags & SURF_BSPX_NODRAW) || (surf->texinfo->flags & SURF_SKY) ||
		GL_VertexLightSurface (surf) || !gl_lms.block_size ||
		surf->lightmaptexturenum <= 0 ||
		surf->lightmaptexturenum >= gl_lms.current_lightmap_texture)
		return false;
	if (((surf->flags & SURF_DRAWTURB) ||
			(surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))) &&
		!GL_DecoupledSurfaceLit (surf))
		return false;
	return GL_LightmapWhiteTexture () != 0;
}

static qboolean GL_LightmapViewTexture (unsigned texnum)
{
	return gl_lightmap && gl_lightmap->intvalue && texnum &&
		texnum == (unsigned)GL_LightmapWhiteTexture ();
}

static void GL_CopyStaticLightmapRect (msurface_t *surf, const byte *pixels, int stride);

static void GL_RecordLightmapUpload (int width, int height)
{
	size_t texels;

	if (width <= 0 || height <= 0)
		return;
	texels = (size_t)width * (size_t)height;
	if (texels > UINT_MAX / LIGHTMAP_BYTES)
		return;
	R_PerfRecordUpload ((unsigned int)texels,
		(unsigned int)(texels * LIGHTMAP_BYTES));
}

int GL_LightmapBlockSize (void)
{
	return gl_lms.block_size;
}

qboolean GL_LightmapPacked10 (void)
{
	return gl_lms.packed10;
}


static void		LM_UploadBlock( qboolean dynamic );
static qboolean	LM_AllocBlock (int w, int h, int *x, int *y);

extern void R_SetCacheState( msurface_t *surf );
extern void R_BuildLightMap (msurface_t *surf, byte *dest, int stride);
extern void EmitLightmappedWaterPolys (msurface_t *fa, int block_size, qboolean gpu_warp);

static qboolean GL_BeginLightmapShader (qboolean alpha_texture, GLuint glowtexnum,
	const msurface_t *warp, qboolean *gpu_warp, qboolean lightmap_view);

static qboolean GL_WaterBatchDrawing (void);
static void GL_BeginWaterSurfaceBatch (void);
static void GL_EndWaterSurfaceBatch (void);
static void GL_FlushWaterBatch (void);
static void GL_FlushInlineWaterBatch (void);
static qboolean GL_BatchWaterSurface (msurface_t *surf, image_t *image,
	float alpha, qboolean translucent, int frame, float intens);

static qboolean GL_SurfaceAlphaTest (const msurface_t *surf)
{
	return (surf->texinfo->flags & SURF_ALPHATEST) &&
		!(surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66));
}

/* Base texture coordinates are normalized before drawing. Keep N64 motion
 * independent of image dimensions and leave the old flowing rate intact. */
void GL_SurfaceScroll (const msurface_t *surf, qboolean warp, float *s, float *t)
{
	unsigned flags = (unsigned)surf->texinfo->flags;
	float phase;

	*s = *t = 0;
	if (flags & (SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y))
	{
		phase = r_newrefdef.time * 0.78125f;
		if (flags & SURF_N64_SCROLL_FLIP)
			phase = -phase;
		if (flags & SURF_N64_SCROLL_Y)
			*t = phase;
		else
			*s = -phase;
	}
	else if (flags & SURF_FLOWING)
	{
		phase = warp ? r_newrefdef.time * 0.5f : r_newrefdef.time / 40.0f;
		*s = -64 * (phase - (int)phase);
		if (!warp && FLOAT_EQ_ZERO (*s))
			*s = -64.0f;
		if (warp)
			*s *= 1.0f / 64;
	}
}

static qboolean GL_BeginVertexLightShader (qboolean alpha_texture, GLuint glowtexnum)
{
	if (GL_ImageShaderIntensity ())
		return GL_BeginLitMaterialShader (alpha_texture, GL_MODULATE,
			0, 0, 1, glowtexnum);
	return GL_BeginMaterialShader (true, alpha_texture, GL_MODULATE,
		0, 0, 1, glowtexnum);
}

static void GL_RestoreLightmapTextureUnit (void)
{
	if (!vertexlight_disabled_multitexture)
		return;
	GL_EnableMultitexture (true);
	GL_SelectTexture (GL_TEXTURE1);
	if (FLOAT_EQ_ZERO (gl_overbrights->value) || gl_overbrights->modified)
		GL_TexEnv (GL_MODULATE);
	else
	{
		qglTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
		qglTexEnvi (GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);
		qglTexEnvi (GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE);
		qglTexEnvi (GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 2);
		GL_TexEnv (GL_COMBINE_ARB);
	}
	vertexlight_disabled_multitexture = false;
}

/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
image_t *R_TextureAnimation (mtexinfo_t *tex, int frame)
{
	int c;

	if (!tex->next || tex->numframes <= 0)
		return tex->image;
	c = frame % tex->numframes;
	if (c < 0)
		c += tex->numframes;
	while (c && tex->next)
	{
		tex = tex->next;
		c--;
	}
	return tex->image;
}


#if 0
/*
=================
WaterWarpPolyVerts

Mangles the x and y coordinates in a copy of the poly
so that any drawing routine can be water warped
=================
*/
glpoly_t *WaterWarpPolyVerts (glpoly_t *p)
{
	int		i;
	float	*v, *nv;
	static byte	buffer[1024];
	glpoly_t *out;

	out = (glpoly_t *)buffer;

	out->numverts = p->numverts;
	v = p->verts[0];
	nv = out->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE, nv+=VERTEXSIZE)
	{
		nv[0] = v[0] + 4*sin(v[1]*0.05+r_newrefdef.time)*sin(v[2]*0.05+r_newrefdef.time);
		nv[1] = v[1] + 4*sin(v[0]*0.05+r_newrefdef.time)*sin(v[2]*0.05+r_newrefdef.time);

		nv[2] = v[2];
		nv[3] = v[3];
		nv[4] = v[4];
		nv[5] = v[5];
		nv[6] = v[6];
	}

	return out;
}

/*
================
DrawGLWaterPoly

Warp the vertex coordinates
================
*/
void DrawGLWaterPoly (glpoly_t *p)
{
	int		i;
	float	*v;

	p = WaterWarpPolyVerts (p);
	qglBegin (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		qglTexCoord2f (v[3], v[4]);
		qglVertex3fv (v);
	}
	qglEnd ();
	GL_ShowTrisVertices (GL_WorldShowTrisMask (), GL_TRIANGLE_FAN, p->numverts,
		p->verts[0], VERTEXSIZE * sizeof(float), 3);
}
void DrawGLWaterPolyLightmap (glpoly_t *p)
{
	int		i;
	float	*v;

	p = WaterWarpPolyVerts (p);
	qglBegin (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		qglTexCoord2f (v[5], v[6]);
		qglVertex3fv (v);
	}
	qglEnd ();
}
#endif

/* Legacy lightmaps and lava correction are later multiplicative passes.
 * Delay their outlines until those passes have finished. */
static qboolean showtris_defer_world, showtris_defer_lightmap;

int GL_WorldShowTrisMask (void)
{
	return showtris_defer_world || showtris_defer_lightmap ? 0 : GL_SHOWTRIS_WORLD;
}

/*
================
DrawGLPoly
================
*/
void DrawGLPoly (glpoly_t *p, qboolean lit)
{
	qboolean bloom_shader;
	int		i;
	float	*v;

	bloom_shader = lit && GL_ImageShaderIntensity () ?
		GL_BeginLitMaterialShader (true, GL_REPLACE, 0, 0, 1, 0) :
		GL_BeginBloomShader (true, true, false);
	qglBegin (GL_POLYGON);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		qglTexCoord2f (v[3], v[4]);
		qglVertex3fv (v);
	}
	qglEnd ();
	GL_ShowTrisVertices (GL_WorldShowTrisMask (), GL_TRIANGLE_FAN, p->numverts,
		p->verts[0], VERTEXSIZE * sizeof (float), 3);
	if (bloom_shader)
		GL_EndSurfaceShader ();
}

//============
//PGM
/*
================
DrawGLFlowingPoly -- version of DrawGLPoly that handles scrolling texture
================
*/
void DrawGLFlowingPoly (msurface_t *fa, qboolean lit)
{
	qboolean bloom_shader;
	int		i;
	float	*v;
	glpoly_t *p;
	float scroll, scroll_t;

	p = fa->polys;

	GL_SurfaceScroll (fa, false, &scroll, &scroll_t);

	bloom_shader = lit && GL_ImageShaderIntensity () ?
		GL_BeginLitMaterialShader (true, GL_REPLACE, 0, 0, 1, 0) :
		GL_BeginBloomShader (true, true, false);
	qglBegin (GL_POLYGON);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		qglTexCoord2f (v[3] + scroll, v[4] + scroll_t);
		qglVertex3fv (v);
	}
	qglEnd ();
	GL_ShowTrisVertices (GL_WorldShowTrisMask (), GL_TRIANGLE_FAN, p->numverts,
		p->verts[0], VERTEXSIZE * sizeof (float), 3);
	if (bloom_shader)
		GL_EndSurfaceShader ();
}
//PGM
//============

static void R_DrawLightmapOutlines (void)
{
	msurface_t *surf;
	glpoly_t *poly;
	int i;

	if (!GL_ShowTrisEnabled (GL_SHOWTRIS_WORLD))
		return;
	for (i = 0; i < MAX_LIGHTMAPS; i++)
		for (surf = gl_lms.lightmap_surfaces[i]; surf; surf = surf->lightmapchain)
		{
			/* Lava draws its one outline after the brightness correction. */
			if (GL_JumpLavaShowTrisDeferred (surf))
				continue;
			for (poly = surf->polys; poly; poly = poly->chain)
				GL_ShowTrisVertices (GL_SHOWTRIS_WORLD, GL_TRIANGLE_FAN, poly->numverts,
					poly->verts[0], VERTEXSIZE * sizeof (float), 3);
		}
}

/*
** DrawGLPolyChain
*/
void DrawGLPolyChain( glpoly_t *p, float soffset, float toffset )
{
	if ( FLOAT_EQ_ZERO(soffset) && FLOAT_EQ_ZERO(toffset))
	{
		for ( ; p != 0; p = p->chain )
		{
			float *v;
			int j;

			qglBegin (GL_POLYGON);
			v = p->verts[0];
			for (j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
			{
				qglTexCoord2f (v[5], v[6] );
				qglVertex3fv (v);
			}
			qglEnd ();
		}
	}
	else
	{
		for ( ; p != 0; p = p->chain )
		{
			float *v;
			int j;

			qglBegin (GL_POLYGON);
			v = p->verts[0];
			for (j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
			{
				qglTexCoord2f (v[5] - soffset, v[6] - toffset );
				qglVertex3fv (v);
			}
			qglEnd ();
		}
	}
}

int usingmodifiedlightmaps;

/*
** R_BlendLightMaps
**
** This routine takes all the given light mapped surfaces in the world and
** blends them into the framebuffer.
*/
void R_BlendLightmaps (void)
{
	int			i;
	float inverse_block_size;
	msurface_t	*surf, *newdrawsurf = 0;

	// don't bother if we're set to fullbright
	if (FLOAT_NE_ZERO(r_fullbright->value))
		return;

	if (!r_worldmodel->lightdata)
		return;
	if (!gl_lms.block_size)
		return;
	inverse_block_size = 1.0f / gl_lms.block_size;

	/* The base pass already wrote zero emission. Draw this legacy
	 * multiplicative lightmap pass only to scene colour, preserving the
	 * untouched emission attachment and the original blend equation. */
	GL_BloomTargets (false);
	// don't bother writing Z
	qglDepthMask( 0 );


	qglEnable (GL_BLEND);
	qglBlendFunc (GL_ZERO, GL_SRC_COLOR );

	if ( currentmodel == r_worldmodel )
		c_visible_lightmaps = 0;

	/*
	** render static lightmaps first
	*/
	for (i = 1; i < gl_lms.current_lightmap_texture; i++)
	{
		if ( gl_lms.lightmap_surfaces[i] )
		{
			if (currentmodel == r_worldmodel)
				c_visible_lightmaps++;
			GL_Bind( gl_state.lightmap_textures + i);

			for ( surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain )
			{
				if ( surf->polys )
					DrawGLPolyChain( surf->polys, 0, 0 );
			}
		}
	}

	/*
	** render dynamic lightmaps
	*/
	if ( FLOAT_NE_ZERO(gl_dynamic->value) )
	{
		memset( gl_lms.allocated, 0, sizeof(gl_lms.allocated));

		GL_Bind( gl_state.lightmap_textures+0 );

		if (currentmodel == r_worldmodel)
			c_visible_lightmaps++;

		newdrawsurf = gl_lms.lightmap_surfaces[0];

		for ( surf = gl_lms.lightmap_surfaces[0]; surf != 0; surf = surf->lightmapchain )
		{
			int		smax, tmax;
			byte	*base;

			smax = (surf->extents[0]>>4)+1;
			tmax = (surf->extents[1]>>4)+1;

			if ( LM_AllocBlock( smax, tmax, &surf->dlight_s, &surf->dlight_t ) )
			{
				base = gl_lms.lightmap_buffer;
				base += (surf->dlight_t * gl_lms.block_size + surf->dlight_s) * LIGHTMAP_BYTES;

				R_BuildLightMap (surf, base, gl_lms.block_size * LIGHTMAP_BYTES);
			}
			else
			{
				msurface_t *drawsurf;

				// upload what we have so far
				LM_UploadBlock( true );

				// draw all surfaces that use this lightmap
				for ( drawsurf = newdrawsurf; drawsurf != surf; drawsurf = drawsurf->lightmapchain )
				{
					if ( drawsurf->polys )
						DrawGLPolyChain (drawsurf->polys,
							(drawsurf->light_s - drawsurf->dlight_s) * inverse_block_size,
							(drawsurf->light_t - drawsurf->dlight_t) * inverse_block_size);
				}

				newdrawsurf = drawsurf;

				// clear the block
				memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));
				
				// try uploading the block now
				if ( !LM_AllocBlock( smax, tmax, &surf->dlight_s, &surf->dlight_t ) )
				{
					ri.Sys_Error( ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed (dynamic)\n", smax, tmax );
				}

				base = gl_lms.lightmap_buffer;
				base += (surf->dlight_t * gl_lms.block_size + surf->dlight_s) * LIGHTMAP_BYTES;

				R_BuildLightMap (surf, base, gl_lms.block_size * LIGHTMAP_BYTES);
			}
		}

		/*
		** draw remainder of dynamic lightmaps that haven't been uploaded yet
		*/
		if ( newdrawsurf )
			LM_UploadBlock( true );

		for ( surf = newdrawsurf; surf != 0; surf = surf->lightmapchain )
		{
			if ( surf->polys )
				DrawGLPolyChain (surf->polys, (surf->light_s - surf->dlight_s) * inverse_block_size, (surf->light_t - surf->dlight_t) * inverse_block_size);
		}
	}

	/*
	** restore state
	*/
	qglDisable (GL_BLEND);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask( 1 );
}

#define GL_COMBINE_ARB						0x8570
#define GL_RGB_SCALE_ARB					0x8573

/* Sampled BSPX or N64 special faces require a built optional atlas and the
 * material shader to preserve their texture alpha. */
static qboolean GL_DecoupledSurfaceLit (const msurface_t *surf)
{
	return surf->lm_decoupled && surf->samples &&
		!(surf->texinfo->flags & SURF_SKY) &&
		surf->lightmaptexturenum > 0 &&
		surf->lightmaptexturenum < gl_lms.current_lightmap_texture &&
		gl_lms.block_size > 0 && qglMTexCoord2fSGIS &&
		qglMTexCoord2fvSGIS && qglGetTexEnviv && qglGetBooleanv &&
		GL_ImageShaderIntensity () && GL_SurfaceShaderAvailable ();
}

static qboolean GL_SurfaceStylesDirty (const msurface_t *surf, int *map)
{
	for (*map = 0; *map < MAXLIGHTMAPS && surf->styles[*map] != 255; (*map)++)
		if (r_newrefdef.lightstyles[surf->styles[*map]].white != surf->cached_light[*map])
			return true;
	return false;
}

static void GL_RefreshDecoupledSurfaceLightmap (msurface_t *surf)
{
	int map, smax, tmax;
	qboolean dynamic = surf->dlightframe == r_framecount &&
		FLOAT_NE_ZERO (gl_dynamic->value);
	qboolean dirty = dynamic || surf->lighting_sequence != GL_LightingSequence ();

	if (GL_SurfaceStylesDirty (surf, &map))
		dirty = true;
	if (!dirty)
		return;

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	R_BuildLightMap (surf, gl_lms.surface_buffer, smax * LIGHTMAP_BYTES);
	R_SetCacheState (surf);
	GL_MBind (GL_TEXTURE1, gl_state.lightmap_textures + surf->lightmaptexturenum);
	qglTexSubImage2D (GL_TEXTURE_2D, 0, surf->light_s, surf->light_t,
		smax, tmax, GL_LIGHTMAP_FORMAT, gl_lms.upload_type, gl_lms.surface_buffer);
	GL_RecordLightmapUpload (smax, tmax);
	GL_CopyStaticLightmapRect (surf, gl_lms.surface_buffer, smax * LIGHTMAP_BYTES);
	/* Force a clean static upload when the dynamic light leaves this face. */
	if (dynamic)
		surf->lighting_sequence = 0;
}

static qboolean GL_DrawDecoupledSurface (msurface_t *surf, image_t *image,
	float alpha, qboolean translucent)
{
	glpoly_t *p;
	qboolean gpu_warp = false;
	qboolean lightmap_view;
	GLboolean depthmask = GL_TRUE;
	int i;
	float *v, scroll, scroll_t;

	if (!GL_DecoupledSurfaceLit (surf))
		return false;
	lightmap_view = GL_SurfaceLightmapView (surf);
	GL_RefreshDecoupledSurfaceLightmap (surf);
	GL_EnableMultitexture (true);
	GL_MBind (GL_TEXTURE0, lightmap_view ? GL_LightmapWhiteTexture () : image->texnum);
	GL_SelectTexture (GL_TEXTURE0);
	GL_TexEnv (translucent ? GL_MODULATE : GL_REPLACE);
	GL_MBind (GL_TEXTURE1, gl_state.lightmap_textures + surf->lightmaptexturenum);
	if (FLOAT_EQ_ZERO (gl_overbrights->value) || gl_overbrights->modified)
		GL_TexEnv (GL_MODULATE);
	else
	{
		qglTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
		qglTexEnvi (GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);
		qglTexEnvi (GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE);
		qglTexEnvi (GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 2);
		GL_TexEnv (GL_COMBINE_ARB);
	}
	if (!GL_BeginLightmapShader (lightmap_view ? false : image->has_alpha,
			lightmap_view ? 0 : image->glowtexnum,
			(surf->flags & SURF_DRAWTURB) ? surf : NULL, &gpu_warp, lightmap_view))
	{
		GL_EnableMultitexture (false);
		if (translucent)
			GL_TexEnv (GL_MODULATE);
		return false;
	}

	if (translucent)
	{
		qglGetBooleanv (GL_DEPTH_WRITEMASK, &depthmask);
		qglDepthMask (GL_FALSE);
	}
	qglColor4f (1, 1, 1, alpha);
	if (surf->flags & SURF_DRAWTURB)
		EmitLightmappedWaterPolys (surf, gl_lms.block_size, gpu_warp);
	else
	{
		GL_SurfaceScroll (surf, false, &scroll, &scroll_t);
		for (p = surf->polys; p; p = p->next)
		{
			qglBegin (GL_POLYGON);
			for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE)
			{
				qglMTexCoord2fSGIS (GL_TEXTURE0, v[3] + scroll, v[4] + scroll_t);
				qglMTexCoord2fvSGIS (GL_TEXTURE1, &v[5]);
				qglVertex3fv (v);
			}
			qglEnd ();
			GL_ShowTrisVertices (GL_WorldShowTrisMask (), GL_TRIANGLE_FAN, p->numverts,
				p->verts[0], VERTEXSIZE * sizeof (float), 3);
		}
	}
	if (translucent)
		qglDepthMask (depthmask);
	qglColor4fv (colorWhite);
	GL_EndSurfaceShader ();
	GL_EnableMultitexture (false);
	if (translucent)
		GL_TexEnv (GL_MODULATE);
	return true;
}

/*
================
R_RenderBrushPoly
================
*/
static void GL_DrawVertexlitPoly (glpoly_t *p, unsigned texnum, float scroll, float scroll_t,
	qboolean alpha_texture, GLuint glowtexnum)
{
	int i;
	float *v;
	qboolean shader;

	GL_EnableMultitexture (false);
	vertexlight_disabled_multitexture = true;
	GL_MBind (GL_TEXTURE0, texnum);
	GL_SelectTexture (GL_TEXTURE0);
	GL_TexEnv (GL_MODULATE);
	qglPushAttrib (GL_LIGHTING_BIT | GL_CURRENT_BIT);
	qglShadeModel (GL_SMOOTH);
	shader = GL_BeginVertexLightShader (alpha_texture, glowtexnum);
	qglBegin (GL_POLYGON);
	for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE)
	{
		qglColor4ubv ((const byte *)&v[7]);
		qglTexCoord2f (v[3] + scroll, v[4] + scroll_t);
		qglVertex3fv (v);
	}
	qglEnd ();
	GL_ShowTrisVertices (GL_WorldShowTrisMask (), GL_TRIANGLE_FAN, p->numverts,
		p->verts[0], VERTEXSIZE * sizeof (float), 3);
	if (shader)
		GL_EndSurfaceShader ();
	qglPopAttrib ();
	GL_TexEnv (GL_REPLACE);
}

/* A single-texture renderer cannot combine a white base and a lightmap in
 * one pass. In this diagnostic use the built atlas directly, also for
 * translucent owners whose ordinary legacy path omits the later blend. */
static qboolean GL_DrawLegacyLightmapView (msurface_t *surf)
{
	glpoly_t *p;
	int map, smax, tmax;
	qboolean dynamic, dirty, shader;
	float alpha;

	if (qglMTexCoord2fSGIS || !GL_SurfaceLightmapView (surf) || GL_LightingShader ())
		return false;
	dynamic = surf->dlightframe == r_framecount && FLOAT_NE_ZERO (gl_dynamic->value);
	dirty = dynamic || surf->lighting_sequence != GL_LightingSequence ();
	if (GL_SurfaceStylesDirty (surf, &map))
		dirty = true;
	GL_Bind (gl_state.lightmap_textures + surf->lightmaptexturenum);
	if (dirty)
	{
		smax = (surf->extents[0] >> 4) + 1;
		tmax = (surf->extents[1] >> 4) + 1;
		R_BuildLightMap (surf, gl_lms.surface_buffer, smax * LIGHTMAP_BYTES);
		R_SetCacheState (surf);
		qglTexSubImage2D (GL_TEXTURE_2D, 0, surf->light_s, surf->light_t,
			smax, tmax, GL_LIGHTMAP_FORMAT, gl_lms.upload_type, gl_lms.surface_buffer);
		GL_RecordLightmapUpload (smax, tmax);
		GL_CopyStaticLightmapRect (surf, gl_lms.surface_buffer, smax * LIGHTMAP_BYTES);
		if (dynamic)
			surf->lighting_sequence = 0;
	}
	alpha = currententity && (currententity->flags & RF_TRANSLUCENT) ? 0.25f : 1;
	qglPushAttrib (GL_CURRENT_BIT | GL_ENABLE_BIT);
	qglDisable (GL_ALPHA_TEST);
	qglColor4f (1, 1, 1, alpha);
	GL_TexEnv (GL_MODULATE);
	shader = GL_BeginMaterialShader (true, false, GL_MODULATE, 0, 0, 1, 0);
	DrawGLPolyChain (surf->polys, 0, 0);
	for (p = surf->polys; p; p = p->chain)
		GL_ShowTrisVertices (GL_WorldShowTrisMask (), GL_TRIANGLE_FAN, p->numverts,
			p->verts[0], VERTEXSIZE * sizeof (float), 3);
	if (shader)
		GL_EndSurfaceShader ();
	GL_TexEnv (GL_REPLACE);
	qglPopAttrib ();
	return true;
}

void R_RenderBrushPoly (msurface_t *fa)
{
	int			maps;
	image_t		*image;
	qboolean is_dynamic = false;
	qboolean alpha_test = GL_SurfaceAlphaTest (fa);
	qboolean lighting_dirty = fa->lighting_sequence != GL_LightingSequence ();

	GL_SetWorldShowTrisDeferred (GL_ShowTrisEnabled (GL_SHOWTRIS_WORLD) &&
		GL_JumpLavaShowTrisDeferred (fa));
	if (!GL_WaterBatchDrawing ())
		c_brush_polys++;

	image = R_TextureAnimation (fa->texinfo, currententity->frame);
	if (GL_BatchWaterSurface (fa, image, 1.0f, false, 0, gl_state.inverse_intensity))
		return;
	GL_FlushWaterBatch ();
	if (GL_DrawLegacyLightmapView (fa))
		return;
	if (alpha_test)
		qglEnable (GL_ALPHA_TEST);
	if (GL_VertexLightSurface (fa))
	{
		float scroll, scroll_t;
		glpoly_t *p;
		GL_SurfaceScroll (fa, false, &scroll, &scroll_t);
		for (p = fa->polys; p; p = p->next)
			GL_DrawVertexlitPoly (p, image->texnum, scroll, scroll_t,
				image->has_alpha, image->glowtexnum);
		return;
	}

	if (fa->flags & SURF_DRAWTURB)
	{
		if (GL_DrawDecoupledSurface (fa, image, 1.0f, false))
		{
			if (alpha_test)
				qglDisable (GL_ALPHA_TEST);
			return;
		}
		GL_Bind( image->texnum );

		// warp texture, no lightmaps
		GL_TexEnv( GL_MODULATE );
		qglColor4f( gl_state.inverse_intensity, 
			        gl_state.inverse_intensity,
					gl_state.inverse_intensity,
					1.0F );
		EmitWaterPolys (fa, image);
		GL_TexEnv( GL_REPLACE );
		if (alpha_test)
			qglDisable (GL_ALPHA_TEST);

		return;
	}
	else
	{
		GL_Bind( image->texnum );

		/*if (image->detailtexnum) {
			// Activate the second texture ID and bind the fog texture to it
			qglActiveTextureARB(GL_TEXTURE1_ARB);
			qglEnable(GL_TEXTURE_2D);

			// Here we turn on the COMBINE properties and increase our RGB
			// gamma for the detail texture.  2 seems to work just right.
			qglTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
			qglTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 2);

			// Bind the detail texture
			GL_Bind (image->detailtexnum);

			// Now we want to enter the texture matrix.  This will allow us
			// to change the tiling of the detail texture.
			qglMatrixMode(GL_TEXTURE);

			// Reset the current matrix and apply our chosen scale value
			qglLoadIdentity();
			qglScalef((float)1.0, (float)1.0, 1);

			// Leave the texture matrix and set us back in the model view matrix
			qglMatrixMode(GL_MODELVIEW);
		}*/

		GL_TexEnv( GL_REPLACE );
	}

	showtris_defer_lightmap = !alpha_test &&
		!(currententity->flags & RF_TRANSLUCENT);
	//======
	//PGM
	if (fa->texinfo->flags & (SURF_FLOWING | SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y))
		DrawGLFlowingPoly (fa, true);
	else
		DrawGLPoly (fa->polys, true);
	showtris_defer_lightmap = false;
	//PGM
	//======
	if (alpha_test)
	{
		/* A separate lightmap pass has no base alpha to clip its pixels. */
		qglDisable (GL_ALPHA_TEST);
		return;
	}

	/*
	** check for lightmap modification
	*/
	if (GL_SurfaceStylesDirty (fa, &maps))
		goto dynamic;

	// dynamic this frame or dynamic previously
	if ( ( fa->dlightframe == r_framecount ) )
	{
dynamic:
		if ( FLOAT_NE_ZERO (gl_dynamic->value) )
		{
			if (!( fa->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP ) ) )
			{
				is_dynamic = true;
			}
		}
	}

	if (lighting_dirty && !(fa->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)))
		is_dynamic = true;

	if ( is_dynamic )
	{
		if ((lighting_dirty || maps == MAXLIGHTMAPS || fa->styles[maps] >= 32 || fa->styles[maps] == 0) &&
			(fa->dlightframe != r_framecount || FLOAT_EQ_ZERO (gl_dynamic->value)))
		{
			int			smax, tmax;

			smax = (fa->extents[0]>>4)+1;
			tmax = (fa->extents[1]>>4)+1;

			R_BuildLightMap (fa, gl_lms.surface_buffer, smax * LIGHTMAP_BYTES);
			R_SetCacheState( fa );

			GL_Bind( gl_state.lightmap_textures + fa->lightmaptexturenum );

			qglTexSubImage2D (GL_TEXTURE_2D, 0,
				fa->light_s, fa->light_t,
				smax, tmax,
				GL_LIGHTMAP_FORMAT,
				gl_lms.upload_type, gl_lms.surface_buffer);
			GL_RecordLightmapUpload (smax, tmax);
			GL_CopyStaticLightmapRect (fa, gl_lms.surface_buffer,
				smax * LIGHTMAP_BYTES);

			fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
			gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
		}
		else
		{
			fa->lightmapchain = gl_lms.lightmap_surfaces[0];
			gl_lms.lightmap_surfaces[0] = fa;
		}
	}
	else
	{
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
}


/*
================
R_DrawAlphaSurfaces

Draw water surfaces and windows.
The BSP tree is waled front to back, so unwinding the chain
of alpha_surfaces will draw back to front, giving proper ordering.
================
*/
static void GL_DrawAlphaSurface (msurface_t *s, int frame, float intens)
{
	image_t *image = R_TextureAnimation (s->texinfo, frame);
	float alpha;

	GL_SetWorldShowTrisDeferred (false);
	if (!GL_WaterBatchDrawing ())
		c_brush_polys++;
	if (s->texinfo->flags & SURF_TRANS33)
		alpha = 0.33f;
	else if (s->texinfo->flags & SURF_TRANS66)
		alpha = 0.66f;
	else
		alpha = 1.0f;
	if (GL_BatchWaterSurface (s, image, alpha, true, frame, intens))
		return;
	GL_FlushWaterBatch ();
	if (GL_DrawDecoupledSurface (s, image, alpha, true))
		return;
	GL_Bind (image->texnum);
	qglColor4f (intens, intens, intens, alpha);
	if (s->flags & SURF_DRAWTURB)
		EmitWaterPolys (s, image);
	else if (s->texinfo->flags & (SURF_FLOWING | SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y))
		DrawGLFlowingPoly (s, false);
	else
		DrawGLPoly (s->polys, false);
}

void R_DrawAlphaSurfaces (void)
{
	msurface_t *s;
	entity_t *saved_entity, world_entity, rotated;
	model_t *saved_model;
	alphabrushinstance_t *instance;
	float intens, dot;
	int owner, i;

	if (!r_alpha_surfaces && !alpha_brush_count)
		return;

	//
	// go back to the world matrix
	//
	saved_entity = currententity;
	saved_model = currentmodel;
	qglLoadMatrixf (r_world_matrix);

	qglPushAttrib (GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	qglEnable (GL_BLEND);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask (GL_FALSE);
	GL_TexEnv( GL_MODULATE );

	// the textures are prescaled up for a better lighting range,
	// so scale it back down
	intens = gl_state.inverse_intensity;

	/* Inline owners were collected after the world and unwind before it.
	 * Each instance retains its own transform even if models and values match. */
	for (owner = alpha_brush_count; owner > 0;)
	{
		instance = &alpha_brush_instances[--owner];
		currententity = &instance->entity;
		currentmodel = instance->model;
		R_BeginInlineDlights (currententity, currentmodel);
		rotated = instance->entity;
		rotated.angles[0] = -rotated.angles[0];
		rotated.angles[2] = -rotated.angles[2];
		qglPushMatrix ();
		R_RotateForEntity (&rotated);
		GL_BeginWaterSurfaceBatch ();
		for (i = currentmodel->nummodelsurfaces; i > 0;)
		{
			s = &currentmodel->surfaces[currentmodel->firstmodelsurface + --i];
			if ((s->flags & SURF_BSPX_NODRAW) ||
				!(s->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66)))
				continue;
			dot = DotProduct (instance->modelorg, s->plane->normal) - s->plane->dist;
			if (((s->flags & SURF_PLANEBACK) && dot < -BACKFACE_EPSILON) ||
				(!(s->flags & SURF_PLANEBACK) && dot > BACKFACE_EPSILON))
				GL_DrawAlphaSurface (s, instance->entity.frame, intens);
		}
		GL_EndWaterSurfaceBatch ();
		qglPopMatrix ();
		R_EndInlineDlights ();
	}
	memset (&world_entity, 0, sizeof (world_entity));
	world_entity.frame = alpha_world_frame;
	currententity = &world_entity;
	currentmodel = r_worldmodel;
	GL_BeginWaterSurfaceBatch ();
	for (s = r_alpha_surfaces; s; s = s->texturechain)
		GL_DrawAlphaSurface (s, alpha_world_frame, intens);
	GL_EndWaterSurfaceBatch ();

	GL_TexEnv( GL_REPLACE );
	qglColor4fv(colorWhite);
	qglPopAttrib ();

	currententity = saved_entity;
	currentmodel = saved_model;
	R_ClearAlphaSurfaces ();
}

/*
================
DrawTextureChains
================
*/
void DrawTextureChains (void)
{
	int		i;
	msurface_t	*s;
	image_t		*image;

	c_visible_textures = 0;

//	GL_TexEnv( GL_REPLACE );

	if ( !qglSelectTextureSGIS && !qglActiveTextureARB )
	{
		GL_BeginWaterSurfaceBatch ();
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			s = image->texturechain;
			if (!s)
				continue;
			c_visible_textures++;

			for ( ; s ; s=s->texturechain)
				R_RenderBrushPoly (s);

			image->texturechain = NULL;
		}
		GL_EndWaterSurfaceBatch ();
	}
	else
	{
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			if (!image->texturechain)
				continue;
			c_visible_textures++;

			for ( s = image->texturechain; s ; s=s->texturechain)
			{
				if ( !( s->flags & SURF_DRAWTURB ) )
					R_RenderBrushPoly (s);
			}
		}

		GL_EnableMultitexture( false );
		GL_BeginWaterSurfaceBatch ();
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			s = image->texturechain;
			if (!s)
				continue;

			for ( ; s ; s=s->texturechain)
			{
				if ( s->flags & SURF_DRAWTURB )
					R_RenderBrushPoly (s);
			}

			image->texturechain = NULL;
		}
		GL_EndWaterSurfaceBatch ();
		//		GL_EnableMultitexture( true );
	}

	GL_TexEnv( GL_REPLACE );
}

/* Indexed fans and static uploads adapted from Q2PRO
 * src/refresh/tess.c and src/refresh/surf.c.
 * Copyright (C) 2003-2006 Andrey Nazarov; GPL version 2 or later.
 * Preserve visible-face selection and ordered translucent drawing; opaque
 * faces with stable lightmaps may be grouped by material. Flush before
 * lightmap overwrites or draw-state changes.
 */
#define WORLD_BATCH_VERTICES 6144
#define WORLD_BATCH_INDICES (3 * WORLD_BATCH_VERTICES)
#define WORLD_MATERIAL_BUCKETS 1024
/* The paired BSP reader admits at most 65,536 faces; overflow uses traversal submission. */
#define WORLD_SURFACE_QUEUE_CAPACITY 65536

typedef struct
{
	msurface_t *surface;
	int next;
	int next_dirty;
	qboolean lightmap_dirty;
} glqueuedworldsurface_t;

typedef struct
{
	int left, top, right, bottom;
	int first, last;
	unsigned int texels;
} glstaticlightmapdirty_t;

static glqueuedworldsurface_t world_surface_queue[WORLD_SURFACE_QUEUE_CAPACITY];
static int world_material_heads[WORLD_MATERIAL_BUCKETS];
static int world_material_tails[WORLD_MATERIAL_BUCKETS];
static int world_surface_queue_count;
static int world_surface_queue_dirty_count;
static glstaticlightmapdirty_t static_lightmap_dirty[MAX_LIGHTMAPS];

static void GL_ResetWorldSurfaceQueue (void)
{
	int i;
	world_surface_queue_count = 0;
	world_surface_queue_dirty_count = 0;
	for (i = 0; i < WORLD_MATERIAL_BUCKETS; i++)
		world_material_heads[i] = world_material_tails[i] = -1;
	memset (static_lightmap_dirty, 0, sizeof (static_lightmap_dirty));
	for (i = 0; i < MAX_LIGHTMAPS; i++)
		static_lightmap_dirty[i].first = static_lightmap_dirty[i].last = -1;
}

void GL_FreeStaticLightmapBackings (void)
{
	int i;

	R_ClearAlphaSurfaces ();
	for (i = 1; i < MAX_LIGHTMAPS; i++)
	{
		free (gl_lms.static_backing[i]);
		gl_lms.static_backing[i] = NULL;
	}
	GL_ResetWorldSurfaceQueue ();
}

static void GL_CopyStaticLightmapRect (msurface_t *surf, const byte *pixels, int stride)
{
	int atlas = surf->lightmaptexturenum;
	int width = (surf->extents[0] >> 4) + 1;
	int height = (surf->extents[1] >> 4) + 1;
	int y;
	byte *backing;

	if (atlas <= 0 || atlas >= MAX_LIGHTMAPS || !pixels ||
		width < 1 || height < 1 || width > gl_lms.block_size ||
		height > gl_lms.block_size || surf->light_s < 0 || surf->light_t < 0 ||
		surf->light_s > gl_lms.block_size - width ||
		surf->light_t > gl_lms.block_size - height ||
		stride < width * LIGHTMAP_BYTES)
		return;
	backing = gl_lms.static_backing[atlas];
	if (!backing)
		return;
	for (y = 0; y < height; y++)
		memcpy (backing + ((surf->light_t + y) * gl_lms.block_size + surf->light_s) * LIGHTMAP_BYTES,
			pixels + y * stride, width * LIGHTMAP_BYTES);
}

typedef struct
{
	float vertices[WORLD_BATCH_VERTICES][VERTEXSIZE];
	GLuint indices[WORLD_BATCH_INDICES];
	int numverts, numindices;
	unsigned texnum, lmtex, glowtexnum;
	qboolean use_vbo, alpha_texture, vertexlight, lightmap_view;
	qboolean alpha_test;
	float scroll, scroll_t;
} glworldbatch_t;

static glworldbatch_t world_batch;

/* Water waits only inside one draw list and owner transform. Replay the
 * existing material setup at flush; no shader or client arrays stay active. */
static struct
{
	msurface_t *surfaces[WORLD_BATCH_VERTICES / 3];
	GLuint indices[WORLD_BATCH_INDICES];
	int count, numverts, numindices, frame;
	unsigned texnum, glowtexnum, flags;
	int lightmap;
	float alpha, intens;
	entity_t *entity;
	model_t *model;
	qboolean enabled, drawing, submitted, translucent, lightmapped;
	qboolean alpha_texture, alpha_test, lava, lightmap_view, deferred;
} water_batch;

unsigned c_world_batches, c_world_batch_vertices, c_world_batch_indices;
unsigned c_world_immediate;

static unsigned c_world_vbo_batches, c_world_cpu_vertex_bytes;

void GL_ResetWorldBatches (void)
{
	memset (&water_batch, 0, sizeof (water_batch));
	world_batch.numverts = world_batch.numindices = 0;
	world_batch.texnum = world_batch.lmtex = 0;
	world_batch.use_vbo = false;
	world_batch.alpha_test = false;
	world_batch.vertexlight = false;
	world_batch.scroll = world_batch.scroll_t = 0;
	c_world_batches = c_world_batch_vertices = c_world_batch_indices = 0;
	c_world_immediate = 0;
	c_world_vbo_batches = c_world_cpu_vertex_bytes = 0;
}

static qboolean GL_WorldBatchEnabled (void)
{
	return gl_batch_world && FLOAT_NE_ZERO (gl_batch_world->value) &&
		qglActiveTextureARB && qglClientActiveTextureARB &&
		!qglSelectTextureSGIS && qglMTexCoord2fSGIS &&
		qglMTexCoord2fvSGIS && qglDrawElements && qglVertexPointer &&
		qglTexCoordPointer && qglEnableClientState && qglDisableClientState &&
		qglPushClientAttrib && qglPopClientAttrib &&
		(!GL_VertexLightRequested () || qglColorPointer);
}

#define WORLD_BUFFER_MAX_BYTES (64u * 1024u * 1024u)

static struct
{
	GLuint buffer;
	model_t *model;
	unsigned numverts, bytes;
} world_buffer;

static qboolean GL_WorldBufferAvailable (void)
{
	return GL_WorldBatchEnabled () && gl_vbo && FLOAT_NE_ZERO (gl_vbo->value) &&
		qglGenBuffers && qglDeleteBuffers && qglBindBuffer &&
		qglBufferData && qglBufferSubData && qglGetBufferParameteriv &&
		qglGetIntegerv && qglGetError && qglMatrixMode &&
		qglPushMatrix && qglPopMatrix && qglTranslatef;
}

void GL_FreeWorldBuffer (model_t *model)
{
	if (model && model != world_buffer.model)
		return;

	GL_ResetWorldBatches ();
	if (world_buffer.buffer && qglDeleteBuffers)
		qglDeleteBuffers (1, &world_buffer.buffer);
	memset (&world_buffer, 0, sizeof (world_buffer));
}

static qboolean GL_WaterBufferSurface (const msurface_t *surf)
{
	return (surf->flags & SURF_DRAWTURB) &&
		!(surf->flags & SURF_BSPX_NODRAW) && surf->texinfo &&
		(surf->texinfo->flags & SURF_WARP) &&
		!(surf->texinfo->flags & SURF_SKY) &&
		surf->texinfo->image && surf->texinfo->image->width > 0 &&
		surf->texinfo->image->height > 0;
}

void GL_BuildWorldBuffer (model_t *model)
{
	unsigned count = 0, ordinary_count = 0, first = 0, used = 0, uploaded = 0;
	unsigned limit = WORLD_BUFFER_MAX_BYTES / (VERTEXSIZE * sizeof (float));
	GLuint buffer = 0;
	GLint saved_binding = 0, actual_size = 0;
	int i, j;
	msurface_t *surf;
	glpoly_t *p;
	qboolean with_water = true;

	if (world_buffer.buffer && world_buffer.model == model &&
		GL_WorldBufferAvailable ())
		return;

	GL_FreeWorldBuffer (NULL);
	if (!model || model->type != mod_brush || !model->surfaces ||
		model->numsurfaces <= 0)
		return;
	world_buffer.model = model;

retry:
	count = first = used = uploaded = 0;
	buffer = 0;
	// Reset every owned polygon before admitting lightmapped and optional water geometry.
	for (i = 0, surf = model->surfaces; i < model->numsurfaces; i++, surf++)
		for (p = surf->polys; p; p = p->next)
			p->vbo_firstvertex = -1;

	if (!GL_WorldBufferAvailable ())
		return;

	for (i = 0, surf = model->surfaces; i < model->numsurfaces; i++, surf++)
	{
		if (surf->flags & (SURF_DRAWTURB | SURF_BSPX_NODRAW) ||
			surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP))
			continue;
		for (p = surf->polys; p; p = p->next)
		{
			if (p->numverts < 3 || p->numverts > WORLD_BATCH_VERTICES)
				continue;
			if ((unsigned)p->numverts > limit - count)
			{
				ri.Con_Printf (PRINT_ALL,
					"...world vertex buffer exceeds 64 MiB; using CPU geometry\n");
				return;
			}
			count += p->numverts;
		}
	}
	ordinary_count = count;
	/* Ordinary geometry keeps its existing budget. Water uses only spare room
	 * and never changes the retained CPU subdivision polygons. */
	if (with_water)
		for (i = 0, surf = model->surfaces; i < model->numsurfaces; i++, surf++)
		{
			unsigned face_count = 0;
			qboolean admit = true;
			if (!GL_WaterBufferSurface (surf))
				continue;
			for (p = surf->polys; p; p = p->next)
			{
				if (p->numverts < 3 || p->numverts > WORLD_BATCH_VERTICES ||
					(unsigned)p->numverts > limit - face_count)
				{
					admit = false;
					break;
				}
				face_count += p->numverts;
			}
			if (!admit || face_count > limit - count)
				continue;
			for (p = surf->polys; p; p = p->next)
			{
				p->vbo_firstvertex = (int)count;
				count += p->numverts;
			}
		}
	if (!count)
		return;

	// Allocation errors are checked independently of gl_ignore_errors.
	if (!GL_ClearErrors ())
		return;

	qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_binding);
	qglGenBuffers (1, &buffer);
	if (!buffer || qglGetError () != GL_NO_ERROR)
		goto failed;
	qglBindBuffer (GL_ARRAY_BUFFER, buffer);
	if (qglGetError () != GL_NO_ERROR)
		goto failed;
	qglBufferData (GL_ARRAY_BUFFER, (GLsizeiptr)(count * VERTEXSIZE * sizeof (float)),
		NULL, GL_STATIC_DRAW);
	qglGetBufferParameteriv (GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &actual_size);
	if (qglGetError () != GL_NO_ERROR ||
		actual_size != (GLint)(count * VERTEXSIZE * sizeof (float)))
		goto failed;

	// Reuse the bounded draw scratch, avoiding a second full CPU map copy.
	for (i = 0, surf = model->surfaces; i < model->numsurfaces; i++, surf++)
	{
		if (surf->flags & (SURF_DRAWTURB | SURF_BSPX_NODRAW) ||
			surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP))
			continue;
		for (p = surf->polys; p; p = p->next)
		{
			if (p->numverts < 3 || p->numverts > WORLD_BATCH_VERTICES)
				continue;
			if (used + p->numverts > WORLD_BATCH_VERTICES)
			{
				qglBufferSubData (GL_ARRAY_BUFFER,
					(GLintptr)(uploaded * VERTEXSIZE * sizeof (float)),
					(GLsizeiptr)(used * VERTEXSIZE * sizeof (float)), world_batch.vertices);
				if (qglGetError () != GL_NO_ERROR)
					goto failed;
				uploaded += used;
				used = 0;
			}
			p->vbo_firstvertex = first;
			memcpy (world_batch.vertices[used], p->verts,
				p->numverts * sizeof (world_batch.vertices[0]));
			used += p->numverts;
			first += p->numverts;
		}
	}
	/* GPU water starts from donor-normalized base coordinates. Optional
	 * decoupled lightmaps keep their model-space projected atlas coordinates. */
	for (i = 0, surf = model->surfaces; i < model->numsurfaces; i++, surf++)
	{
		if (!GL_WaterBufferSurface (surf))
			continue;
		for (p = surf->polys; p; p = p->next)
		{
			if (p->vbo_firstvertex < 0)
				continue;
			if ((unsigned)p->vbo_firstvertex != first ||
				(unsigned)p->numverts > count - first)
				goto failed;
			if (used + p->numverts > WORLD_BATCH_VERTICES)
			{
				qglBufferSubData (GL_ARRAY_BUFFER,
					(GLintptr)(uploaded * VERTEXSIZE * sizeof (float)),
					(GLsizeiptr)(used * VERTEXSIZE * sizeof (float)), world_batch.vertices);
				if (qglGetError () != GL_NO_ERROR)
					goto failed;
				uploaded += used;
				used = 0;
			}
			for (j = 0; j < p->numverts; j++)
			{
				float *src = p->verts[j];
				float *dst = world_batch.vertices[used + j];
				float lm_s, lm_t;
				memset (dst, 0, sizeof (world_batch.vertices[0]));
				memcpy (dst, src, 3 * sizeof (float));
				GL_WaterTexCoord (surf, src, &dst[3], &dst[4]);
				if (surf->lm_decoupled && surf->samples &&
					surf->lightmaptexturenum && gl_lms.block_size > 0)
				{
					GL_DecoupledLightmapST (surf, src, gl_lms.block_size, &lm_s, &lm_t);
					dst[5] = lm_s;
					dst[6] = lm_t;
				}
				memset (&dst[7], 255, sizeof (float));
			}
			used += p->numverts;
			first += p->numverts;
		}
	}
	if (first != count || uploaded + used != count)
		goto failed;
	if (used)
	{
		qglBufferSubData (GL_ARRAY_BUFFER,
			(GLintptr)(uploaded * VERTEXSIZE * sizeof (float)),
			(GLsizeiptr)(used * VERTEXSIZE * sizeof (float)), world_batch.vertices);
		if (qglGetError () != GL_NO_ERROR)
			goto failed;
	}
	qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_binding);
	world_buffer.buffer = buffer;
	world_buffer.numverts = count;
	world_buffer.bytes = count * VERTEXSIZE * sizeof (float);
	ri.Con_Printf (PRINT_DEVELOPER, "...world vertex buffer: %u bytes, %u vertices\n",
		world_buffer.bytes, world_buffer.numverts);
	return;

failed:
	qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_binding);
	if (buffer)
		qglDeleteBuffers (1, &buffer);
	for (i = 0, surf = model->surfaces; i < model->numsurfaces; i++, surf++)
		for (p = surf->polys; p; p = p->next)
			p->vbo_firstvertex = -1;
	if (with_water && count > ordinary_count)
	{
		with_water = false;
		goto retry;
	}
	ri.Con_Printf (PRINT_ALL, "...world vertex buffer failed; using CPU geometry\n");
}

void GL_UpdateWorldBuffer (void)
{
	if (gl_vbo->modified || gl_batch_world->modified)
	{
		gl_vbo->modified = gl_batch_world->modified = false;
		GL_BuildWorldBuffer (r_worldmodel);
	}
}

void GL_InvalidateVertexLighting (void)
{
	vertexlight_ready = false;
	vertexlight_model = NULL;
}

static void GL_FlushWorldBatch (void);

/* The scratch contains complete interleaved vertices, not isolated color
 * bytes. Bind lazily so style changes that quantize to the same RGBA do not
 * touch the VBO. The caller restores the previous binding after all ranges. */
static qboolean GL_UploadVertexLightRange (unsigned first, unsigned count,
	GLint *saved_binding, qboolean *bound)
{
	unsigned stride = VERTEXSIZE * sizeof (float);
	GLint actual_size = 0;

	if (!count || count > WORLD_BATCH_VERTICES ||
		first >= world_buffer.numverts || count > world_buffer.numverts - first ||
		world_buffer.bytes > WORLD_BUFFER_MAX_BYTES ||
		world_buffer.numverts > WORLD_BUFFER_MAX_BYTES / stride ||
		world_buffer.bytes != world_buffer.numverts * stride)
		return false;
	if (!*bound)
	{
		if (!GL_ClearErrors ())
			return false;
		qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, saved_binding);
		if (qglGetError () != GL_NO_ERROR)
			return false;
		qglBindBuffer (GL_ARRAY_BUFFER, world_buffer.buffer);
		*bound = true;
		if (qglGetError () != GL_NO_ERROR)
			return false;
		qglGetBufferParameteriv (GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &actual_size);
		if (qglGetError () != GL_NO_ERROR || actual_size != (GLint)world_buffer.bytes)
			return false;
	}
	qglBufferSubData (GL_ARRAY_BUFFER, (GLintptr)(first * stride),
		(GLsizeiptr)(count * stride), world_batch.vertices);
	if (qglGetError () != GL_NO_ERROR)
		return false;
	vertexlight_vbo_uploaded_bytes += count * stride;
	return true;
}

/* The current lightstyles arrive in RenderFrame, after BeginFrame's cvar
 * updates. Refresh both CPU polygons and their optional static VBO together. */
void GL_UpdateVertexLighting (void)
{
	model_t *model = r_worldmodel;
	float modulate, brightness;
	unsigned sequence;
	unsigned pending_first = 0, pending_count = 0;
	byte changed_styles[MAX_LIGHTSTYLES] = { 0 };
	GLint saved_binding = 0;
	qboolean full_refresh, dirty, incremental, upload_failed = false, bound = false;
	int i, j;
	vertexlight_refreshed_vertices = vertexlight_vbo_uploaded_bytes = 0;

	if (!GL_VertexLightRequested () || !model || !model->surfaces ||
		!r_newrefdef.lightstyles)
	{
		vertexlight_ready = false;
		return;
	}
	modulate = GL_WorldModulate ();
	brightness = GL_LightingBrightness ();
	sequence = GL_LightingSequence ();
	full_refresh = !vertexlight_ready || vertexlight_model != model ||
		vertexlight_modulate != modulate ||
		vertexlight_brightness != brightness ||
		vertexlight_sequence != sequence;
	dirty = full_refresh;
	if (!full_refresh)
	{
		for (i = 0; i < MAX_LIGHTSTYLES; i++)
		{
			if (!vertexlight_used_styles[i])
				continue;
			for (j = 0; j < 3; j++)
			{
				if (vertexlight_styles[i][j] !=
					r_newrefdef.lightstyles[i].rgb[j])
				{
					changed_styles[i] = 1;
					dirty = true;
					break;
				}
			}
		}
	}
	if (!dirty)
		return;

	/* The same scratch holds queued CPU vertices. Submit those before changing
	 * colors or staging whole interleaved VBO ranges into it. */
	if (world_batch.numindices)
		GL_FlushWorldBatch ();
	vertexlight_ready = false;
	incremental = world_buffer.buffer && world_buffer.model == model &&
		GL_WorldBufferAvailable ();
	if (incremental &&
		(world_buffer.bytes > WORLD_BUFFER_MAX_BYTES ||
			world_buffer.numverts > WORLD_BUFFER_MAX_BYTES / (VERTEXSIZE * sizeof (float)) ||
			world_buffer.bytes != world_buffer.numverts * VERTEXSIZE * sizeof (float)))
		upload_failed = true;
	memset (vertexlight_used_styles, 0, sizeof (vertexlight_used_styles));
	for (i = 0; i < model->numsurfaces; i++)
	{
		msurface_t *surf = &model->surfaces[i];
		glpoly_t *p;
		qboolean refresh = full_refresh;
		if (!surf->samples || !surf->polys ||
			(surf->flags & (SURF_DRAWTURB | SURF_BSPX_NODRAW)) ||
			(surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)))
			continue;
#ifdef SURF_ALPHATEST
		if (surf->texinfo->flags & SURF_ALPHATEST)
			continue;
#endif
		for (j = 0; j < MAXLIGHTMAPS && surf->styles[j] != 255; j++)
		{
			vertexlight_used_styles[surf->styles[j]] = 1;
			if (changed_styles[surf->styles[j]])
				refresh = true;
		}
		if (!refresh)
			continue;
		for (p = surf->polys; p; p = p->next)
		{
			float *v;
			int vertex;
			qboolean changed = false;
			for (vertex = 0, v = p->verts[0]; vertex < p->numverts;
				 vertex++, v += VERTEXSIZE)
			{
				float s, t;
				byte old_rgba[4];
				if (surf->lm_decoupled)
				{
					s = DotProduct (v, surf->lm_axis[0]) + surf->lm_axis[0][3];
					t = DotProduct (v, surf->lm_axis[1]) + surf->lm_axis[1][3];
				}
				else
				{
					s = (DotProduct (v, surf->texinfo->vecs[0]) +
							surf->texinfo->vecs[0][3] - surf->texturemins[0]) /
						16;
					t = (DotProduct (v, surf->texinfo->vecs[1]) +
							surf->texinfo->vecs[1][3] - surf->texturemins[1]) /
						16;
				}
				memcpy (old_rgba, &v[7], sizeof (old_rgba));
				R_SampleSurfaceVertexLight (surf, s, t, (byte *)&v[7]);
				if (memcmp (old_rgba, &v[7], sizeof (old_rgba)))
					changed = true;
				vertexlight_refreshed_vertices++;
			}
			if (!changed || !incremental || upload_failed ||
				p->numverts < 3 || p->numverts > WORLD_BATCH_VERTICES ||
				p->vbo_firstvertex < 0)
				continue;
			if ((unsigned)p->vbo_firstvertex >= world_buffer.numverts ||
				(unsigned)p->numverts > world_buffer.numverts - p->vbo_firstvertex)
			{
				upload_failed = true;
				continue;
			}
			if (pending_count &&
				((unsigned)p->vbo_firstvertex != pending_first + pending_count ||
					(unsigned)p->numverts > WORLD_BATCH_VERTICES - pending_count))
			{
				if (!GL_UploadVertexLightRange (pending_first, pending_count,
						&saved_binding, &bound))
				{
					upload_failed = true;
					continue;
				}
				pending_count = 0;
			}
			if (!pending_count)
				pending_first = p->vbo_firstvertex;
			memcpy (world_batch.vertices[pending_count], p->verts,
				p->numverts * sizeof (world_batch.vertices[0]));
			pending_count += p->numverts;
		}
	}
	if (incremental && !upload_failed && pending_count &&
		!GL_UploadVertexLightRange (pending_first, pending_count,
			&saved_binding, &bound))
		upload_failed = true;
	if (bound)
	{
		qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_binding);
		if (qglGetError () != GL_NO_ERROR)
			upload_failed = true;
	}
	if (upload_failed)
	{
		/* A partially updated buffer is never eligible for a later draw. */
		if (bound && (GLuint)saved_binding == world_buffer.buffer)
		{
			qglBindBuffer (GL_ARRAY_BUFFER, 0);
			qglGetError ();
		}
		GL_FreeWorldBuffer (NULL);
	}
	else if (!GL_WorldBufferAvailable ())
	{
		if (world_buffer.buffer)
			GL_FreeWorldBuffer (NULL);
	}
	else if (!incremental)
	{
		GL_BuildWorldBuffer (model);
		if (world_buffer.buffer)
			vertexlight_vbo_uploaded_bytes = world_buffer.bytes;
	}
	vertexlight_model = model;
	vertexlight_modulate = modulate;
	vertexlight_brightness = brightness;
	vertexlight_sequence = sequence;
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
		for (j = 0; j < 3; j++)
			vertexlight_styles[i][j] = r_newrefdef.lightstyles[i].rgb[j];
	vertexlight_ready = true;
}

void GL_PrintWorldBufferInfo (void)
{
	ri.Con_Printf (PRINT_ALL,
		"world VBO: %u bytes, %u vertices; %u VBO draws, %u CPU vertex bytes; vertexlight %u colors refreshed, %u VBO uploaded bytes\n",
		world_buffer.bytes, world_buffer.numverts,
		c_world_vbo_batches, c_world_cpu_vertex_bytes,
		vertexlight_refreshed_vertices, vertexlight_vbo_uploaded_bytes);
}

static qboolean GL_BeginLightmapShader (qboolean alpha_texture, GLuint glowtexnum,
	const msurface_t *warp, qboolean *gpu_warp, qboolean lightmap_view)
{
	GLint base, lightmap, rgb, alpha, scale = 1;
	unsigned saved_tmu = gl_state.currenttmu;
	if (!GL_SurfaceShaderAvailable () || !qglGetTexEnviv)
	{
		GL_ImageShaderIntensityFailure ();
		return false;
	}
	GL_SelectTexture (GL_TEXTURE0);
	qglGetTexEnviv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &base);
	GL_SelectTexture (GL_TEXTURE1);
	qglGetTexEnviv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &lightmap);
	if (lightmap == GL_COMBINE_ARB)
	{
		qglGetTexEnviv (GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, &rgb);
		qglGetTexEnviv (GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, &alpha);
		qglGetTexEnviv (GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, &scale);
		if (rgb != GL_MODULATE || alpha != GL_MODULATE)
			lightmap = -1;
		else
			lightmap = GL_MODULATE;
	}
	GL_SelectTexture (saved_tmu ? GL_TEXTURE1 : GL_TEXTURE0);
	if (lightmap_view)
		return GL_BeginLightmapViewShader (false, (GLenum)base,
			(GLenum)gl_lms.internal_format, (GLenum)lightmap, (float)scale,
			warp, gpu_warp);
	if (warp)
		return GL_BeginWarpMaterialShader (alpha_texture, (GLenum)base,
			(GLenum)gl_lms.internal_format, (GLenum)lightmap, (float)scale,
			glowtexnum, warp, true, gpu_warp);
	return GL_BeginLitMaterialShader (alpha_texture, (GLenum)base,
		(GLenum)gl_lms.internal_format, (GLenum)lightmap, (float)scale, glowtexnum);
}

static void GL_FlushWorldBatch (void)
{
	GLint saved_array = 0, saved_elements = 0, saved_matrix = GL_MODELVIEW;
	qboolean buffers = qglBindBuffer && qglGetIntegerv;
	qboolean flowing = world_batch.use_vbo &&
		(FLOAT_NE_ZERO (world_batch.scroll) || FLOAT_NE_ZERO (world_batch.scroll_t));
	qboolean shader;

	if (!world_batch.numindices)
		return;

	if (world_batch.vertexlight)
	{
		GL_EnableMultitexture (false);
		vertexlight_disabled_multitexture = true;
	}
	else
		GL_RestoreLightmapTextureUnit ();
	GL_MBind (GL_TEXTURE0, world_batch.texnum);
	if (world_batch.vertexlight)
	{
		GL_SelectTexture (GL_TEXTURE0);
		GL_TexEnv (GL_MODULATE);
		qglPushAttrib (GL_LIGHTING_BIT | GL_CURRENT_BIT);
		qglShadeModel (GL_SMOOTH);
	}
	else
		GL_MBind (GL_TEXTURE1, world_batch.lmtex);
	if (flowing)
	{
		qglGetIntegerv (GL_MATRIX_MODE, &saved_matrix);
		GL_SelectTexture (GL_TEXTURE0);
		qglMatrixMode (GL_TEXTURE);
		qglPushMatrix ();
		qglTranslatef (world_batch.scroll, world_batch.scroll_t, 0);
		qglMatrixMode ((GLenum)saved_matrix);
		GL_SelectTexture (world_batch.vertexlight ? GL_TEXTURE0 : GL_TEXTURE1);
	}

	// Isolate retained alias-model arrays and buffer bindings from this draw.
	qglPushClientAttrib (GL_CLIENT_VERTEX_ARRAY_BIT);
	if (buffers)
	{
		qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_array);
		qglGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &saved_elements);
		qglBindBuffer (GL_ARRAY_BUFFER, world_batch.use_vbo ? world_buffer.buffer : 0);
		qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
	}
	if (world_batch.vertexlight)
	{
		qglEnableClientState (GL_COLOR_ARRAY);
		qglColorPointer (4, GL_UNSIGNED_BYTE, sizeof (world_batch.vertices[0]),
			world_batch.use_vbo ? (const GLvoid *)(7 * sizeof (float)) :
								  &world_batch.vertices[0][7]);
	}
	else
		qglDisableClientState (GL_COLOR_ARRAY);
	qglDisableClientState (GL_NORMAL_ARRAY);
	qglDisableClientState (GL_INDEX_ARRAY);
	qglDisableClientState (GL_EDGE_FLAG_ARRAY);
	qglEnableClientState (GL_VERTEX_ARRAY);
	qglVertexPointer (3, GL_FLOAT, sizeof (world_batch.vertices[0]),
		world_batch.use_vbo ? (const GLvoid *)0 : world_batch.vertices[0]);

	qglClientActiveTextureARB (GL_TEXTURE0);
	qglEnableClientState (GL_TEXTURE_COORD_ARRAY);
	qglTexCoordPointer (2, GL_FLOAT, sizeof (world_batch.vertices[0]),
		world_batch.use_vbo ? (const GLvoid *)(3 * sizeof (float)) : &world_batch.vertices[0][3]);
	qglClientActiveTextureARB (GL_TEXTURE1);
	if (world_batch.vertexlight)
		qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
	else
	{
		qglEnableClientState (GL_TEXTURE_COORD_ARRAY);
		qglTexCoordPointer (2, GL_FLOAT, sizeof (world_batch.vertices[0]),
			world_batch.use_vbo ? (const GLvoid *)(5 * sizeof (float)) : &world_batch.vertices[0][5]);
	}

	if (world_batch.alpha_test)
		qglEnable (GL_ALPHA_TEST);
	shader = world_batch.vertexlight ?
		GL_BeginVertexLightShader (world_batch.alpha_texture, world_batch.glowtexnum) :
		GL_BeginLightmapShader (world_batch.alpha_texture, world_batch.glowtexnum,
			NULL, NULL, world_batch.lightmap_view);
	qglDrawElements (GL_TRIANGLES, world_batch.numindices,
		GL_UNSIGNED_INT, world_batch.indices);
	GL_ShowTrisElements (GL_WorldShowTrisMask (), GL_TRIANGLES, world_batch.numindices,
		GL_UNSIGNED_INT, world_batch.indices);
	if (shader)
		GL_EndSurfaceShader ();
	if (world_batch.alpha_test)
		qglDisable (GL_ALPHA_TEST);
	qglPopClientAttrib ();
	if (world_batch.vertexlight)
	{
		GL_SelectTexture (GL_TEXTURE0);
		GL_TexEnv (GL_REPLACE);
		qglPopAttrib ();
	}
	if (buffers)
	{
		qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
		qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, (GLuint)saved_elements);
	}
	if (flowing)
	{
		GL_SelectTexture (GL_TEXTURE0);
		qglMatrixMode (GL_TEXTURE);
		qglPopMatrix ();
		qglMatrixMode ((GLenum)saved_matrix);
		GL_SelectTexture (world_batch.vertexlight ? GL_TEXTURE0 : GL_TEXTURE1);
	}
	GL_CheckForError ();

	c_world_batches++;
	c_world_batch_vertices += world_batch.numverts;
	c_world_batch_indices += world_batch.numindices;
	if (world_batch.use_vbo)
		c_world_vbo_batches++;
	world_batch.numverts = world_batch.numindices = 0;
}

void GL_SetWorldShowTrisDeferred (qboolean deferred)
{
	if (!GL_ShowTrisEnabled (GL_SHOWTRIS_WORLD))
	{
		showtris_defer_world = false;
		return;
	}
	if (showtris_defer_world != deferred)
	{
		GL_FlushWaterBatch ();
		GL_FlushWorldBatch ();
		showtris_defer_world = deferred;
	}
}

static qboolean GL_WaterSurfaceIndexed (msurface_t *surf, qboolean lightmapped)
{
	glpoly_t *p;
	unsigned first, count;

	if (!surf || !surf->polys || !GL_WaterBufferSurface (surf) ||
		!world_buffer.buffer || world_buffer.model != r_worldmodel ||
		!GL_WorldBufferAvailable () || world_batch.numindices ||
		world_batch.numverts ||
		world_buffer.bytes > WORLD_BUFFER_MAX_BYTES ||
		world_buffer.numverts > WORLD_BUFFER_MAX_BYTES / (VERTEXSIZE * sizeof (float)) ||
		world_buffer.bytes != world_buffer.numverts * VERTEXSIZE * sizeof (float) ||
		(lightmapped && (!surf->lm_decoupled || !surf->samples || !surf->lightmaptexturenum || gl_lms.block_size <= 0)))
		return false;

	/* A whole-face fallback is safe only before the first indexed draw. */
	for (p = surf->polys; p; p = p->next)
	{
		if (p->numverts < 3 || p->numverts > WORLD_BATCH_VERTICES ||
			p->vbo_firstvertex < 0)
			return false;
		first = (unsigned)p->vbo_firstvertex;
		count = (unsigned)p->numverts;
		if (first >= world_buffer.numverts || count > world_buffer.numverts - first)
			return false;
	}
	return true;
}

static qboolean GL_WaterBatchDrawing (void)
{
	return water_batch.drawing;
}

static void GL_FlushWaterBatch (void)
{
	int i;
	if (!water_batch.count || water_batch.drawing)
		return;
	water_batch.drawing = true;
	water_batch.submitted = false;
	for (i = 0; i < water_batch.count; i++)
	{
		if (water_batch.translucent)
			GL_DrawAlphaSurface (water_batch.surfaces[i], water_batch.frame, water_batch.intens);
		else
			R_RenderBrushPoly (water_batch.surfaces[i]);
		if (water_batch.submitted)
			break;
	}
	water_batch.drawing = false;
	water_batch.count = water_batch.numverts = water_batch.numindices = 0;
}

static void GL_FlushInlineWaterBatch (void)
{
	if (!water_batch.count)
		return;
	/* Match the existing single-texture brush-water wrapper, including its
	 * multitexture restore, before the next lightmapped draw or owner exit. */
	GL_EnableMultitexture (false);
	GL_FlushWaterBatch ();
	GL_EnableMultitexture (true);
}

static void GL_BeginWaterSurfaceBatch (void)
{
	GL_FlushWaterBatch ();
	water_batch.enabled = true;
}

static void GL_EndWaterSurfaceBatch (void)
{
	GL_FlushWaterBatch ();
	water_batch.enabled = false;
}

static qboolean GL_BatchWaterSurface (msurface_t *surf, image_t *image,
	float alpha, qboolean translucent, int frame, float intens)
{
	glpoly_t *p;
	int i, map, numverts = 0, numindices = 0;
	unsigned flags;
	GLuint *indices, first;
	qboolean lightmapped, lightmap_view, alpha_test, lava, deferred;

	if (!water_batch.enabled || water_batch.drawing || !(surf->flags & SURF_DRAWTURB))
		return false;
	GL_FlushWorldBatch ();
	lightmapped = GL_DecoupledSurfaceLit (surf);
	if (!GL_WaterSurfaceIndexed (surf, lightmapped))
		return false;
	/* A dirty sampled face retains its immediate update-before-draw path. */
	if (lightmapped)
	{
		if ((surf->dlightframe == r_framecount && FLOAT_NE_ZERO (gl_dynamic->value)) ||
			surf->lighting_sequence != GL_LightingSequence ())
			return false;
		if (GL_SurfaceStylesDirty (surf, &map))
			return false;
	}
	for (p = surf->polys; p; p = p->next)
	{
		if (p->numverts > WORLD_BATCH_VERTICES - numverts ||
			3 * (p->numverts - 2) > WORLD_BATCH_INDICES - numindices)
			return false;
		numverts += p->numverts;
		numindices += 3 * (p->numverts - 2);
	}
	flags = surf->texinfo->flags &
		(SURF_FLOWING | SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y | SURF_N64_SCROLL_FLIP);
	lava = surf->texinfo->is_lava &&
		!(surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66));
	lightmap_view = lightmapped && GL_SurfaceLightmapView (surf);
	alpha_test = !translucent && GL_SurfaceAlphaTest (surf);
	deferred = !translucent && GL_ShowTrisEnabled (GL_SHOWTRIS_WORLD) &&
		GL_JumpLavaShowTrisDeferred (surf);
	if (water_batch.count &&
		(water_batch.numverts + numverts > WORLD_BATCH_VERTICES ||
			water_batch.numindices + numindices > WORLD_BATCH_INDICES ||
			water_batch.texnum != image->texnum || water_batch.glowtexnum != image->glowtexnum ||
			water_batch.alpha_texture != image->has_alpha || water_batch.flags != flags ||
			water_batch.lava != lava || water_batch.lightmapped != lightmapped ||
			water_batch.lightmap != (lightmapped ? surf->lightmaptexturenum : 0) ||
			water_batch.lightmap_view != lightmap_view || water_batch.alpha_test != alpha_test ||
			water_batch.alpha != alpha || water_batch.intens != intens ||
			water_batch.translucent != translucent || water_batch.frame != frame ||
			water_batch.entity != currententity || water_batch.model != currentmodel ||
			water_batch.deferred != deferred))
		GL_FlushWaterBatch ();
	water_batch.texnum = image->texnum;
	water_batch.glowtexnum = image->glowtexnum;
	water_batch.alpha_texture = image->has_alpha;
	water_batch.flags = flags;
	water_batch.lava = lava;
	water_batch.lightmapped = lightmapped;
	water_batch.lightmap = lightmapped ? surf->lightmaptexturenum : 0;
	water_batch.lightmap_view = lightmap_view;
	water_batch.alpha_test = alpha_test;
	water_batch.alpha = alpha;
	water_batch.intens = intens;
	water_batch.translucent = translucent;
	water_batch.frame = frame;
	water_batch.entity = currententity;
	water_batch.model = currentmodel;
	water_batch.deferred = deferred;
	water_batch.surfaces[water_batch.count++] = surf;
	indices = water_batch.indices + water_batch.numindices;
	for (p = surf->polys; p; p = p->next)
	{
		first = (GLuint)p->vbo_firstvertex;
		for (i = 1; i < p->numverts - 1; i++)
		{
			*indices++ = first;
			*indices++ = first + i;
			*indices++ = first + i + 1;
		}
	}
	water_batch.numverts += numverts;
	water_batch.numindices += numindices;
	return true;
}

/* The selected water material owns the program, textures, colour and modelview.
 * Submit only geometry here; never flush an ordinary batch under that program. */
qboolean GL_DrawIndexedWaterPolys (msurface_t *surf, qboolean lightmapped)
{
	glpoly_t *p;
	GLint saved_array = 0, saved_elements = 0, saved_client_tmu = GL_TEXTURE0;
	unsigned first;
	int i, numindices = 0, batch_vertices = 0;
	GLuint *indices;
	qboolean submitted = false;

	if (!GL_WaterSurfaceIndexed (surf, lightmapped))
		return false;

	qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_array);
	qglGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &saved_elements);
	qglGetIntegerv (GL_CLIENT_ACTIVE_TEXTURE, &saved_client_tmu);
	qglPushClientAttrib (GL_CLIENT_VERTEX_ARRAY_BIT);
	qglBindBuffer (GL_ARRAY_BUFFER, world_buffer.buffer);
	qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
	qglDisableClientState (GL_COLOR_ARRAY);
	qglDisableClientState (GL_NORMAL_ARRAY);
	qglDisableClientState (GL_INDEX_ARRAY);
	qglDisableClientState (GL_EDGE_FLAG_ARRAY);
	qglEnableClientState (GL_VERTEX_ARRAY);
	qglVertexPointer (3, GL_FLOAT, VERTEXSIZE * sizeof (float), (const GLvoid *)0);
	qglClientActiveTextureARB (GL_TEXTURE0);
	qglEnableClientState (GL_TEXTURE_COORD_ARRAY);
	qglTexCoordPointer (2, GL_FLOAT, VERTEXSIZE * sizeof (float),
		(const GLvoid *)(3 * sizeof (float)));
	qglClientActiveTextureARB (GL_TEXTURE1);
	if (lightmapped)
	{
		qglEnableClientState (GL_TEXTURE_COORD_ARRAY);
		qglTexCoordPointer (2, GL_FLOAT, VERTEXSIZE * sizeof (float),
			(const GLvoid *)(5 * sizeof (float)));
	}
	else
		qglDisableClientState (GL_TEXTURE_COORD_ARRAY);

	if (water_batch.drawing && surf == water_batch.surfaces[0])
	{
		qglDrawElements (GL_TRIANGLES, water_batch.numindices, GL_UNSIGNED_INT,
			water_batch.indices);
		GL_ShowTrisElements (GL_WorldShowTrisMask (), GL_TRIANGLES, water_batch.numindices,
			GL_UNSIGNED_INT, water_batch.indices);
		c_world_batches++;
		c_world_vbo_batches++;
		c_world_batch_vertices += water_batch.numverts;
		c_world_batch_indices += water_batch.numindices;
		water_batch.submitted = submitted = true;
	}
	else
		for (p = surf->polys; p; p = p->next)
		{
			int poly_indices = 3 * (p->numverts - 2);
			if (numindices + poly_indices > WORLD_BATCH_INDICES)
			{
				qglDrawElements (GL_TRIANGLES, numindices, GL_UNSIGNED_INT,
					world_batch.indices);
				GL_ShowTrisElements (GL_WorldShowTrisMask (), GL_TRIANGLES, numindices,
					GL_UNSIGNED_INT, world_batch.indices);
				submitted = true;
				c_world_batches++;
				c_world_vbo_batches++;
				c_world_batch_vertices += batch_vertices;
				c_world_batch_indices += numindices;
				numindices = batch_vertices = 0;
			}
			indices = world_batch.indices + numindices;
			first = (unsigned)p->vbo_firstvertex;
			for (i = 1; i < p->numverts - 1; i++)
			{
				*indices++ = first;
				*indices++ = first + i;
				*indices++ = first + i + 1;
			}
			numindices += poly_indices;
			batch_vertices += p->numverts;
		}
	if (numindices)
	{
		qglDrawElements (GL_TRIANGLES, numindices, GL_UNSIGNED_INT,
			world_batch.indices);
		GL_ShowTrisElements (GL_WorldShowTrisMask (), GL_TRIANGLES, numindices,
			GL_UNSIGNED_INT, world_batch.indices);
		submitted = true;
		c_world_batches++;
		c_world_vbo_batches++;
		c_world_batch_vertices += batch_vertices;
		c_world_batch_indices += numindices;
	}
	qglPopClientAttrib ();
	qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
	qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, (GLuint)saved_elements);
	qglClientActiveTextureARB ((GLenum)saved_client_tmu);
	GL_CheckForError ();
	return submitted;
}

static void GL_DrawLightmappedPoly (glpoly_t *p, float scroll, float scroll_t,
	qboolean alpha_texture, GLuint glowtexnum, qboolean alpha_test)
{
	int i;
	float *v;

	qboolean shader;
	if (alpha_test)
		qglEnable (GL_ALPHA_TEST);
	shader = GL_BeginLightmapShader (alpha_texture, glowtexnum, NULL, NULL,
		GL_LightmapViewTexture (gl_state.currenttextures[0]));
	c_world_immediate++;
	qglBegin (GL_POLYGON);
	for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE)
	{
		if (FLOAT_NE_ZERO (scroll) || FLOAT_NE_ZERO (scroll_t))
			qglMTexCoord2fSGIS (GL_TEXTURE0, v[3] + scroll, v[4] + scroll_t);
		else
			qglMTexCoord2fvSGIS (GL_TEXTURE0, &v[3]);
		qglMTexCoord2fvSGIS (GL_TEXTURE1, &v[5]);
		qglVertex3fv (v);
	}
	qglEnd ();
	GL_ShowTrisVertices (GL_WorldShowTrisMask (), GL_TRIANGLE_FAN, p->numverts,
		p->verts[0], VERTEXSIZE * sizeof (float), 3);
	if (shader)
		GL_EndSurfaceShader ();
	if (alpha_test)
		qglDisable (GL_ALPHA_TEST);
}

static void GL_BatchLightmappedPoly (glpoly_t *p, unsigned texnum,
	unsigned lmtex, float scroll, float scroll_t, qboolean alpha_texture, GLuint glowtexnum,
	qboolean alpha_test, qboolean vertexlight)
{
	int i, first, numindices;
	qboolean use_vbo;
	qboolean lightmap_view = !vertexlight && GL_LightmapViewTexture (texnum);
	GLuint *indices;

	if (p->numverts < 3)
		return;

	// Oversized accepted polygons keep the existing immediate-mode path.
	if (!GL_WorldBatchEnabled () || p->numverts > WORLD_BATCH_VERTICES)
	{
		GL_FlushWorldBatch ();
		if (vertexlight)
		{
			c_world_immediate++;
			GL_DrawVertexlitPoly (p, texnum, scroll, scroll_t, alpha_texture, glowtexnum);
		}
		else
		{
			GL_RestoreLightmapTextureUnit ();
			GL_MBind (GL_TEXTURE0, texnum);
			GL_MBind (GL_TEXTURE1, lmtex);
			GL_DrawLightmappedPoly (p, scroll, scroll_t, alpha_texture, glowtexnum, alpha_test);
		}
		return;
	}

	use_vbo = world_buffer.buffer && GL_WorldBufferAvailable () &&
		p->vbo_firstvertex >= 0 &&
		(unsigned)p->vbo_firstvertex < world_buffer.numverts &&
		(unsigned)p->numverts <= world_buffer.numverts - p->vbo_firstvertex;
	numindices = 3 * (p->numverts - 2);
	if (world_batch.numverts + p->numverts > WORLD_BATCH_VERTICES ||
		world_batch.numindices + numindices > WORLD_BATCH_INDICES ||
		(world_batch.numindices &&
			(world_batch.texnum != texnum || world_batch.lmtex != lmtex ||
				world_batch.glowtexnum != glowtexnum ||
				world_batch.alpha_texture != alpha_texture ||
				world_batch.alpha_test != alpha_test ||
				world_batch.vertexlight != vertexlight ||
				world_batch.lightmap_view != lightmap_view ||
				world_batch.use_vbo != use_vbo || (use_vbo && (world_batch.scroll != scroll || world_batch.scroll_t != scroll_t)))))
		GL_FlushWorldBatch ();

	world_batch.texnum = texnum;
	world_batch.lmtex = lmtex;
	world_batch.glowtexnum = glowtexnum;
	world_batch.use_vbo = use_vbo;
	world_batch.alpha_texture = alpha_texture;
	world_batch.alpha_test = alpha_test;
	world_batch.vertexlight = vertexlight;
	world_batch.lightmap_view = lightmap_view;
	world_batch.scroll = scroll;
	world_batch.scroll_t = scroll_t;
	first = use_vbo ? p->vbo_firstvertex : world_batch.numverts;
	if (!use_vbo)
	{
		memcpy (world_batch.vertices[first], p->verts,
			p->numverts * sizeof (world_batch.vertices[0]));
		c_world_cpu_vertex_bytes += p->numverts * sizeof (world_batch.vertices[0]);
		if (FLOAT_NE_ZERO (scroll) || FLOAT_NE_ZERO (scroll_t))
		{
			for (i = 0; i < p->numverts; i++)
			{
				world_batch.vertices[first + i][3] += scroll;
				world_batch.vertices[first + i][4] += scroll_t;
			}
		}
	}

	indices = world_batch.indices + world_batch.numindices;
	for (i = 1; i < p->numverts - 1; i++)
	{
		*indices++ = (GLuint)first;
		*indices++ = (GLuint)(first + i);
		*indices++ = (GLuint)(first + i + 1);
	}
	world_batch.numverts += p->numverts;
	world_batch.numindices += numindices;
}

static void
GL_RenderLightmappedPoly (msurface_t *surf);

static qboolean GL_QueuedStaticLightmapUpdate (msurface_t *surf, int map,
	qboolean lighting_dirty)
{
	int atlas = surf->lightmaptexturenum;
	int width = (surf->extents[0] >> 4) + 1;
	int height = (surf->extents[1] >> 4) + 1;

	if (atlas <= 0 || atlas >= MAX_LIGHTMAPS ||
		atlas >= gl_lms.current_lightmap_texture ||
		!gl_lms.static_backing[atlas] ||
		width < 1 || height < 1 || width > MAX_LIGHTMAP_EXTENTS ||
		height > MAX_LIGHTMAP_EXTENTS || width > gl_lms.block_size ||
		height > gl_lms.block_size || surf->light_s < 0 || surf->light_t < 0 ||
		surf->light_s > gl_lms.block_size - width ||
		surf->light_t > gl_lms.block_size - height)
		return false;
	/* Match the existing static branch in GL_RenderLightmappedPoly. */
	return (lighting_dirty ||
		(FLOAT_NE_ZERO (gl_dynamic->value) &&
			(map == MAXLIGHTMAPS || surf->styles[map] >= 32 ||
				surf->styles[map] == 0)));
}

/* Only stable or mirror-backed static lightmaps may wait in the world queue. */
static qboolean GL_QueueWorldSurface (msurface_t *surf)
{
	image_t *image;
	unsigned key, bucket;
	int index, map;
	qboolean lighting_dirty, style_dirty = false, lightmap_dirty = false;
	qboolean vertexlight = GL_VertexLightSurface (surf);

	if (!GL_WorldBatchEnabled () ||
		world_surface_queue_count == WORLD_SURFACE_QUEUE_CAPACITY ||
		(surf->flags & SURF_DRAWTURB) ||
		(surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)) ||
		surf->dlightframe == r_framecount)
		return false;
	lighting_dirty = !vertexlight &&
		surf->lighting_sequence != GL_LightingSequence ();
	map = 0;
	if (!vertexlight)
	{
		style_dirty = GL_SurfaceStylesDirty (surf, &map);
		if (lighting_dirty || style_dirty)
		{
			if (!GL_QueuedStaticLightmapUpdate (surf, map, lighting_dirty))
				return false;
			lightmap_dirty = true;
		}
	}

	image = R_TextureAnimation (surf->texinfo, currententity->frame);
	key = image->texnum * 0x9e3779b1u;
	key ^= (unsigned)(vertexlight ? 0 : surf->lightmaptexturenum) * 0x85ebca6bu;
	key ^= image->glowtexnum * 0xc2b2ae35u;
	if (image->has_alpha)
		key ^= 0x27d4eb2du;
	if (surf->texinfo->flags & SURF_FLOWING)
		key ^= 0x165667b1u;
	if (surf->texinfo->flags & SURF_N64_SCROLL_X)
		key ^= 0xf05a42c3u;
	if (surf->texinfo->flags & SURF_N64_SCROLL_Y)
		key ^= 0x94f25b57u;
	if (surf->texinfo->flags & SURF_N64_SCROLL_FLIP)
		key ^= 0x62bd03e9u;
	if (vertexlight)
		key ^= 0x5b1eec26u;
	if (GL_SurfaceAlphaTest (surf))
		key ^= 0xd3a2646cu;
	key ^= key >> 16;
	bucket = key & (WORLD_MATERIAL_BUCKETS - 1);

	index = world_surface_queue_count++;
	world_surface_queue[index].surface = surf;
	world_surface_queue[index].next = -1;
	world_surface_queue[index].next_dirty = -1;
	world_surface_queue[index].lightmap_dirty = lightmap_dirty;
	if (lightmap_dirty)
		world_surface_queue_dirty_count++;
	if (world_material_tails[bucket] >= 0)
		world_surface_queue[world_material_tails[bucket]].next = index;
	else
		world_material_heads[bucket] = index;
	world_material_tails[bucket] = index;
	return true;
}

static void GL_UploadQueuedStaticLightmaps (void)
{
	int atlas, i, index, y;
	qboolean flushed = false;
	unsigned int block_area = (unsigned)gl_lms.block_size * gl_lms.block_size;
	glstaticlightmapdirty_t *dirty;
	msurface_t *surf;
	byte *backing;
	int width, height;

	if (!world_surface_queue_dirty_count)
		return;

	for (i = 0; i < world_surface_queue_count; i++)
	{
		if (!world_surface_queue[i].lightmap_dirty)
			continue;
		surf = world_surface_queue[i].surface;
		atlas = surf->lightmaptexturenum;
		backing = gl_lms.static_backing[atlas];
		if (!backing)
			continue;
		width = (surf->extents[0] >> 4) + 1;
		height = (surf->extents[1] >> 4) + 1;
		R_BuildLightMap (surf, gl_lms.surface_buffer, width * LIGHTMAP_BYTES);
		GL_CopyStaticLightmapRect (surf, gl_lms.surface_buffer, width * LIGHTMAP_BYTES);
		dirty = &static_lightmap_dirty[atlas];
		if (dirty->first < 0)
		{
			dirty->left = surf->light_s;
			dirty->top = surf->light_t;
			dirty->right = surf->light_s + width;
			dirty->bottom = surf->light_t + height;
			dirty->first = i;
		}
		else
		{
			if (surf->light_s < dirty->left)
				dirty->left = surf->light_s;
			if (surf->light_t < dirty->top)
				dirty->top = surf->light_t;
			if (surf->light_s + width > dirty->right)
				dirty->right = surf->light_s + width;
			if (surf->light_t + height > dirty->bottom)
				dirty->bottom = surf->light_t + height;
			world_surface_queue[dirty->last].next_dirty = i;
		}
		dirty->last = i;
		if ((unsigned)(width * height) > block_area - dirty->texels)
			dirty->texels = block_area;
		else
			dirty->texels += width * height;
	}

	for (atlas = 1; atlas < MAX_LIGHTMAPS; atlas++)
	{
		dirty = &static_lightmap_dirty[atlas];
		if (dirty->first < 0)
			continue;
		if (!flushed)
		{
			GL_FlushWorldBatch ();
			flushed = true;
		}
		backing = gl_lms.static_backing[atlas];
		GL_MBind (GL_TEXTURE1, gl_state.lightmap_textures + atlas);
		width = dirty->right - dirty->left;
		height = dirty->bottom - dirty->top;
		if ((unsigned)(width * height) <= dirty->texels * 2)
		{
			for (y = 0; y < height; y++)
				memcpy (gl_lms.lightmap_buffer + y * width * LIGHTMAP_BYTES,
					backing + ((dirty->top + y) * gl_lms.block_size + dirty->left) * LIGHTMAP_BYTES, width * LIGHTMAP_BYTES);
			qglTexSubImage2D (GL_TEXTURE_2D, 0, dirty->left, dirty->top,
				width, height, GL_LIGHTMAP_FORMAT, gl_lms.upload_type,
				gl_lms.lightmap_buffer);
			GL_RecordLightmapUpload (width, height);
			for (index = dirty->first; index >= 0;
				 index = world_surface_queue[index].next_dirty)
				R_SetCacheState (world_surface_queue[index].surface);
		}
		else
		{
			for (index = dirty->first; index >= 0;
				 index = world_surface_queue[index].next_dirty)
			{
				surf = world_surface_queue[index].surface;
				width = (surf->extents[0] >> 4) + 1;
				height = (surf->extents[1] >> 4) + 1;
				for (y = 0; y < height; y++)
					memcpy (gl_lms.surface_buffer + y * width * LIGHTMAP_BYTES,
						backing + ((surf->light_t + y) * gl_lms.block_size + surf->light_s) * LIGHTMAP_BYTES, width * LIGHTMAP_BYTES);
				qglTexSubImage2D (GL_TEXTURE_2D, 0, surf->light_s, surf->light_t,
					width, height, GL_LIGHTMAP_FORMAT, gl_lms.upload_type,
					gl_lms.surface_buffer);
				GL_RecordLightmapUpload (width, height);
				R_SetCacheState (surf);
			}
		}
	}
}

static void GL_DrawQueuedWorldSurfaces (void)
{
	int bucket, index;
	if (!world_surface_queue_count)
		return;
	GL_UploadQueuedStaticLightmaps ();
	for (bucket = 0; bucket < WORLD_MATERIAL_BUCKETS; bucket++)
		for (index = world_material_heads[bucket]; index >= 0;
			 index = world_surface_queue[index].next)
			GL_RenderLightmappedPoly (world_surface_queue[index].surface);
	GL_ResetWorldSurfaceQueue ();
}

static void GL_RenderLightmappedPoly( msurface_t *surf )
{
	int		map;
	float scroll, scroll_t;
	image_t *image = R_TextureAnimation (surf->texinfo, currententity->frame);
	qboolean is_dynamic = false;
	qboolean lightmap_view;
	qboolean alpha_test = GL_SurfaceAlphaTest (surf);
	qboolean lighting_dirty = surf->lighting_sequence != GL_LightingSequence ();
	unsigned lmtex = surf->lightmaptexturenum;
	glpoly_t *p;

	GL_SetWorldShowTrisDeferred (GL_ShowTrisEnabled (GL_SHOWTRIS_WORLD) &&
		GL_JumpLavaShowTrisDeferred (surf));
	if (GL_VertexLightSurface (surf))
	{
		c_brush_polys++;
		GL_SurfaceScroll (surf, false, &scroll, &scroll_t);
		for (p = surf->polys; p; p = p->chain)
			GL_BatchLightmappedPoly (p, image->texnum, 0, scroll, scroll_t,
				image->has_alpha, image->glowtexnum, false, true);
		return;
	}

	if (GL_SurfaceStylesDirty (surf, &map))
		goto dynamic;

	// dynamic this frame or dynamic previously
	if ( ( surf->dlightframe == r_framecount ) )
	{
dynamic:
		if ( FLOAT_NE_ZERO (gl_dynamic->value) )
		{
			if ( !(surf->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP ) ) )
			{
				is_dynamic = true;
			}
		}
	}

	if (lighting_dirty && !(surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)))
		is_dynamic = true;

	if ( is_dynamic )
	{
		int			smax, tmax;

		// Dynamic atlas rectangles may overwrite previously queued faces.
		GL_FlushWorldBatch ();

		if ((surf->dlightframe != r_framecount || FLOAT_EQ_ZERO (gl_dynamic->value)) &&
			(lighting_dirty || map == MAXLIGHTMAPS || surf->styles[map] >= 32 || surf->styles[map] == 0))
		{
			smax = (surf->extents[0]>>4)+1;
			tmax = (surf->extents[1]>>4)+1;

			R_BuildLightMap (surf, gl_lms.surface_buffer, smax * LIGHTMAP_BYTES);
			R_SetCacheState( surf );

			GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + surf->lightmaptexturenum );

			lmtex = surf->lightmaptexturenum;

			qglTexSubImage2D (GL_TEXTURE_2D, 0,
				surf->light_s, surf->light_t,
				smax, tmax,
				GL_LIGHTMAP_FORMAT,
				gl_lms.upload_type, gl_lms.surface_buffer);
			GL_RecordLightmapUpload (smax, tmax);
			GL_CopyStaticLightmapRect (surf, gl_lms.surface_buffer,
				smax * LIGHTMAP_BYTES);
		}
		else
		{
			smax = (surf->extents[0]>>4)+1;
			tmax = (surf->extents[1]>>4)+1;

			R_BuildLightMap (surf, gl_lms.surface_buffer, smax * LIGHTMAP_BYTES);

			GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + 0 );

			lmtex = 0;

			qglTexSubImage2D (GL_TEXTURE_2D, 0,
				surf->light_s, surf->light_t,
				smax, tmax,
				GL_LIGHTMAP_FORMAT,
				gl_lms.upload_type, gl_lms.surface_buffer);
			GL_RecordLightmapUpload (smax, tmax);
		}
	}

	c_brush_polys++;
	GL_SurfaceScroll (surf, false, &scroll, &scroll_t);

	/* White has no cutouts. Testing final alpha would discard translucent owners. */
	lightmap_view = GL_SurfaceLightmapView (surf);
	for (p = surf->polys; p; p = p->chain)
		GL_BatchLightmappedPoly (p, lightmap_view ? GL_LightmapWhiteTexture () : image->texnum,
			gl_state.lightmap_textures + lmtex, scroll, scroll_t,
			lightmap_view ? false : image->has_alpha,
			lightmap_view ? 0 : image->glowtexnum, lightmap_view ? false : alpha_test, false);
}

/*
=================
R_DrawInlineBModel
=================
*/
void R_DrawInlineBModel (void)
{
	int i;
	cplane_t	*pplane;
	float		dot;
	msurface_t	*psurf;
	alphabrushinstance_t *instance;
	qboolean alpha_queued = false;

	// calculate dynamic lighting for bmodel
	R_BeginInlineDlights (currententity, currentmodel);

	GL_SetWorldShowTrisDeferred (false);
	GL_JumpLavaBegin ();
	GL_BeginWaterSurfaceBatch ();
	psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];

	if ( currententity->flags & RF_TRANSLUCENT )
	{
		qglEnable (GL_BLEND);
		qglColor4f (1,1,1,0.25);
		GL_TexEnv( GL_MODULATE );
	}

	//
	// draw texture
	//
	for (i=0 ; i<currentmodel->nummodelsurfaces ; i++, psurf++)
	{
		if (psurf->flags & SURF_BSPX_NODRAW)
			continue;
		// find which side of the node we are on
		pplane = psurf->plane;

		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

	// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if (!(psurf->flags & SURF_DRAWTURB))
				GL_FlushInlineWaterBatch ();
			if ((psurf->texinfo->flags & SURF_SKY) &&
				!(psurf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)) &&
				!(psurf->flags & SURF_DRAWTURB) &&
				!(currententity->flags & RF_TRANSLUCENT) &&
				(currentmodel->has_bspx || (gl_drawsky && !gl_drawsky->value)) &&
				R_SkyBrushModelRequested ())
			{
				GL_SetWorldShowTrisDeferred (false);
				GL_FlushWorldBatch ();
				if (R_DrawBrushSkySurface (psurf, modelorg))
					continue;
			}
			GL_JumpLavaSurface (psurf);
			if (psurf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66) )
			{ // defer this brush instance without linking its shared faces
				if (!alpha_queued)
				{
					if (alpha_brush_count >= MAX_ENTITIES)
					{
						ri.Sys_Error (ERR_DROP, "R_DrawInlineBModel: alpha owner overflow");
						goto finish;
					}
					instance = &alpha_brush_instances[alpha_brush_count++];
					instance->entity = *currententity;
					instance->model = currentmodel;
					FastVectorCopy (modelorg, instance->modelorg);
					alpha_queued = true;
				}
			}
			else if ( qglMTexCoord2fSGIS && !( psurf->flags & SURF_DRAWTURB ) )
			{
				GL_RenderLightmappedPoly( psurf );
			}
			else
			{
				GL_FlushWorldBatch ();
				GL_EnableMultitexture( false );
				R_RenderBrushPoly( psurf );
				GL_EnableMultitexture( true );
			}
		}
	}

finish:
	GL_FlushInlineWaterBatch ();
	GL_EndWaterSurfaceBatch ();
	GL_FlushWorldBatch ();
	GL_SetWorldShowTrisDeferred (false);

	if ( !(currententity->flags & RF_TRANSLUCENT) )
	{
		if ( !qglMTexCoord2fSGIS )
			R_BlendLightmaps ();
		R_DrawLightmapOutlines ();
		GL_JumpLavaDraw ();
	}
	else
	{
		qglDisable (GL_BLEND);
		qglColor4fv(colorWhite);
		GL_TexEnv( GL_REPLACE );
	}
	R_EndInlineDlights ();
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e)
{
	vec3_t		mins, maxs;
	int			i;
	qboolean	rotated;

	if (currentmodel->nummodelsurfaces == 0)
		return;

	currententity = e;
	gl_state.currenttextures[0] = gl_state.currenttextures[1] = 0xFFFFFFFF;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;
		for (i=0 ; i<3 ; i++)
		{
			mins[i] = e->origin[i] - currentmodel->radius;
			maxs[i] = e->origin[i] + currentmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd (e->origin, currentmodel->mins, mins);
		VectorAdd (e->origin, currentmodel->maxs, maxs);
	}

	if (FLOAT_NE_ZERO (gl_cull_models->value) && R_CullBox (mins, maxs))
		return;

	qglColor3f (1,1,1);
	memset (gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	VectorSubtract (r_newrefdef.vieworg, e->origin, modelorg);
	if (rotated)
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		FastVectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

    qglPushMatrix ();
e->angles[0] = -e->angles[0];	// stupid quake bug
e->angles[2] = -e->angles[2];	// stupid quake bug
	R_RotateForEntity (e);
e->angles[0] = -e->angles[0];	// stupid quake bug
e->angles[2] = -e->angles[2];	// stupid quake bug

	GL_EnableMultitexture( true );
	GL_SelectTexture( GL_TEXTURE0);
	GL_TexEnv( GL_REPLACE );
	GL_SelectTexture( GL_TEXTURE1);
	
	//GL_TexEnv( GL_MODULATE );
	if (FLOAT_EQ_ZERO(gl_overbrights->value) || gl_overbrights->modified)
	{
		GL_TexEnv( GL_MODULATE );
	}
	else
	{
		qglTexEnvi (GL_TEXTURE_ENV,	GL_TEXTURE_ENV_MODE,	GL_COMBINE_ARB);
		qglTexEnvi (GL_TEXTURE_ENV,	GL_COMBINE_RGB_ARB,		GL_MODULATE);
		qglTexEnvi (GL_TEXTURE_ENV,	GL_COMBINE_ALPHA_ARB,	GL_MODULATE);
		qglTexEnvi (GL_TEXTURE_ENV,	GL_RGB_SCALE_ARB,		2);
		GL_TexEnv (GL_COMBINE_ARB);
	}

	R_DrawInlineBModel ();
	GL_EnableMultitexture( false );
	vertexlight_disabled_multitexture = false;

	qglPopMatrix ();
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
================
*/
#define NEW_CULLING_STYLE

static void R_RecursiveWorldNode (mnode_t *node, int planebits)
{
	int			c, side, sidebit;
	cplane_t	*plane;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	float		dot;
	image_t		*image;

	if (node->contents == CONTENTS_SOLID)
		return;		// solid

	if (node->visframe != r_visframecount)
		return;

#ifndef NEW_CULLING_STYLE
	if (FLOAT_NE_ZERO (gl_cull_nodes->value) &&
		R_CullBox (node->minmaxs, node->minmaxs + 3))
		return;
#else
	if (FLOAT_NE_ZERO (gl_cull_nodes->value) && FLOAT_EQ_ZERO (r_nocull->value))
	{
		int	ret;

		if (planebits & 1)
		{
			ret = BOX_ON_PLANE_SIDE (node->minmaxs, node->minmaxs+3, &frustum[0]);
			if (ret == 2)
				return;
			else if (ret == 1)
				planebits &= ~1;
		}

		if (planebits & 2)
		{
			ret = BOX_ON_PLANE_SIDE (node->minmaxs, node->minmaxs+3, &frustum[1]);
			if (ret == 2)
				return;
			else if (ret == 1)
				planebits &= ~2;
		}

		if (planebits & 4)
		{
			ret = BOX_ON_PLANE_SIDE (node->minmaxs, node->minmaxs+3, &frustum[2]);
			if (ret == 2)
				return;
			else if (ret == 1)
				planebits &= ~4;
		}

		if (planebits & 8)
		{
			ret = BOX_ON_PLANE_SIDE (node->minmaxs, node->minmaxs+3, &frustum[3]);
			if (ret == 2)
				return;
			else if (ret == 1)
				planebits &= ~8;
		}
	}
#endif

// if a leaf node, draw stuff
	if (node->contents != -1)
	{
		pleaf = (mleaf_t *)node;

		// check for door connected areas
		if (r_newrefdef.areabits)
		{
			if (! (r_newrefdef.areabits[pleaf->area>>3] & (1<<(pleaf->area&7)) ) )
				return;		// not visible
		}

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}

		return;
	}

// node is just a decision point, so go down the apropriate sides

// find which side of the node we are on
	plane = node->plane;

	switch (plane->type)
	{
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

	if (FLOAT_GE_ZERO (dot))
	{
		side = 0;
		sidebit = 0;
	}
	else
	{
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

// recurse down the children, front side first
	R_RecursiveWorldNode (node->children[side], planebits);

	// draw stuff
	for ( c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c ; c--, surf++)
	{
		if (surf->visframe != r_framecount)
			continue;
		if (surf->flags & SURF_BSPX_NODRAW)
			continue;

		if ( (surf->flags & SURF_PLANEBACK) != sidebit )
			continue;		// wrong side

		if (surf->texinfo->flags & SURF_SKY)
		{	// just adds to visible sky bounds
			R_AddSkySurface (surf);
		}
		else if(surf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66))
		{	// add to the translucent chain
			surf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = surf;
		}
		else
		{
			GL_JumpLavaSurface (surf);
			if ( qglMTexCoord2fSGIS && !( surf->flags & SURF_DRAWTURB ) )
			{
				if (!GL_QueueWorldSurface (surf))
				{
					GL_DrawQueuedWorldSurfaces ();
					GL_RenderLightmappedPoly (surf);
				}
			}
			else
			{
				// the polygon is visible, so add it to the texture
				// sorted chain
				// FIXME: this is a hack for animation
				image = R_TextureAnimation (surf->texinfo, currententity->frame);
				surf->texturechain = image->texturechain;
				image->texturechain = surf;
			}
		}
	}

	// recurse down the back side
	R_RecursiveWorldNode (node->children[!side], planebits);
	/*
	for ( ; c ; c--, surf++)
	{
		if (surf->visframe != r_framecount)
			continue;

		if ( (surf->flags & SURF_PLANEBACK) != sidebit )
			continue;		// wrong side

		if (surf->texinfo->flags & SURF_SKY)
		{	// just adds to visible sky bounds
			R_AddSkySurface (surf);
		}
		else if (surf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66))
		{	// add to the translucent chain
//			surf->texturechain = alpha_surfaces;
//			alpha_surfaces = surf;
		}
		else
		{
			if ( qglMTexCoord2fSGIS && !( surf->flags & SURF_DRAWTURB ) )
			{
				GL_RenderLightmappedPoly( surf );
			}
			else
			{
				// the polygon is visible, so add it to the texture
				// sorted chain
				// FIXME: this is a hack for animation
				image = R_TextureAnimation (surf->texinfo, currententity->frame);
				surf->texturechain = image->texturechain;
				image->texturechain = surf;
			}
		}
	}
*/
}


/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld (void)
{
	entity_t	ent;

	if (FLOAT_EQ_ZERO (r_drawworld->value))
		return;

	if ( r_newrefdef.rdflags & RDF_NOWORLDMODEL )
		return;

	currentmodel = r_worldmodel;

	FastVectorCopy (r_newrefdef.vieworg, modelorg);

	// auto cycle the world frame for texture animation
	memset (&ent, 0, sizeof(ent));
	ent.frame = (int)(r_newrefdef.time*2);
	alpha_world_frame = ent.frame;
	currententity = &ent;
	GL_SetWorldShowTrisDeferred (false);
	GL_JumpLavaBegin ();
	GL_ResetWorldSurfaceQueue ();

	gl_state.currenttextures[0] = gl_state.currenttextures[1] = 0xFFFFFFFF;

	qglColor3f (1,1,1);
	memset (gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));
	R_ClearSkyBox ();

	if ( qglMTexCoord2fSGIS )
	{
		GL_EnableMultitexture( true );

		GL_SelectTexture( GL_TEXTURE0);
		GL_TexEnv( GL_REPLACE );
		GL_SelectTexture( GL_TEXTURE1);

		if (FLOAT_EQ_ZERO(gl_overbrights->value) || gl_overbrights->modified)
		{
			GL_TexEnv( GL_MODULATE );
		}
		else
		{
			qglTexEnvi (GL_TEXTURE_ENV,	GL_TEXTURE_ENV_MODE,	GL_COMBINE_ARB);
			qglTexEnvi (GL_TEXTURE_ENV,	GL_COMBINE_RGB_ARB,		GL_MODULATE);
			qglTexEnvi (GL_TEXTURE_ENV,	GL_COMBINE_ALPHA_ARB,	GL_MODULATE);
			qglTexEnvi (GL_TEXTURE_ENV,	GL_RGB_SCALE_ARB,		2);
			GL_TexEnv (GL_COMBINE_ARB);
		}

		R_RecursiveWorldNode (r_worldmodel->nodes, 15);
		GL_DrawQueuedWorldSurfaces ();
		GL_FlushWorldBatch ();

		GL_EnableMultitexture( false );
		vertexlight_disabled_multitexture = false;
	}
	else
	{
		R_RecursiveWorldNode (r_worldmodel->nodes, 15);
		GL_DrawQueuedWorldSurfaces ();
	}

	/*
	** theoretically nothing should happen in the next two functions
	** if multitexture is enabled
	*/
	DrawTextureChains ();
	R_BlendLightmaps ();
	GL_SetWorldShowTrisDeferred (false);
	R_DrawLightmapOutlines ();
	GL_JumpLavaDraw ();

	R_DrawSkyBox ();

	vertexlight_disabled_multitexture = false;
}


/*
===============
R_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current
cluster
===============
*/
void R_MarkLeaves (void)
{
	byte	*vis;
	byte	fatvis[MAX_MAP_LEAFS/8];
	mnode_t	*node;
	int		i, c;
	mleaf_t	*leaf;
	int		cluster;

	if (r_oldviewcluster == r_viewcluster && r_oldviewcluster2 == r_viewcluster2 && FLOAT_EQ_ZERO(r_novis->value) && r_viewcluster != -1)
		return;

	// development aid to let you run around and see exactly where
	// the pvs ends
	if (FLOAT_NE_ZERO(gl_lockpvs->value))
		return;

	r_visframecount++;
	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	if (FLOAT_NE_ZERO(r_novis->value) || r_viewcluster == -1 || !r_worldmodel->vis)
	{
		// mark everything
		for (i=0 ; i<r_worldmodel->numleafs ; i++)
			r_worldmodel->leafs[i].visframe = r_visframecount;
		for (i=0 ; i<r_worldmodel->numnodes ; i++)
			r_worldmodel->nodes[i].visframe = r_visframecount;
		return;
	}

	vis = Mod_ClusterPVS (r_viewcluster, r_worldmodel);
	// may have to combine two clusters because of solid water boundaries
	if (r_viewcluster2 != r_viewcluster)
	{
		memcpy (fatvis, vis, (r_worldmodel->numleafs+7)/8);
		vis = Mod_ClusterPVS (r_viewcluster2, r_worldmodel);
		c = (r_worldmodel->numleafs+31)/32;
		for (i=0 ; i<c ; i++)
			((int *)fatvis)[i] |= ((int *)vis)[i];
		vis = fatvis;
	}
	
	for (i=0,leaf=r_worldmodel->leafs ; i<r_worldmodel->numleafs ; i++, leaf++)
	{
		cluster = leaf->cluster;
		if (cluster == -1)
			continue;
		if (vis[cluster>>3] & (1<<(cluster&7)))
		{
			node = (mnode_t *)leaf;
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}

#if 0
	for (i=0 ; i<r_worldmodel->vis->numclusters ; i++)
	{
		if (vis[i>>3] & (1<<(i&7)))
		{
			node = (mnode_t *)&r_worldmodel->leafs[i];	// FIXME: cluster
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
#endif
}



/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/

static void LM_UploadBlock( qboolean dynamic )
{
	int texture;
	int height = 0;

	if (!dynamic)
	{
		if (!gl_lms.dirty)
			return;
		if (gl_lms.current_lightmap_texture > gl_lms.max_static_lightmaps)
			ri.Sys_Error (ERR_DROP, "LM_UploadBlock: lightmap atlas budget exceeded");
	}

	if ( dynamic )
	{
		texture = 0;
	}
	else
	{
		texture = gl_lms.current_lightmap_texture;
	}

	GL_Bind( gl_state.lightmap_textures + texture );
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if ( dynamic )
	{
		int i;

		for (i = 0; i < gl_lms.block_size; i++)
		{
			if ( gl_lms.allocated[i] > height )
				height = gl_lms.allocated[i];
		}

		qglTexSubImage2D (GL_TEXTURE_2D,
			0,
			0, 0,
			gl_lms.block_size, height,
			GL_LIGHTMAP_FORMAT,
			gl_lms.upload_type,
			gl_lms.lightmap_buffer);
		GL_RecordLightmapUpload (gl_lms.block_size, height);
	}
	else
	{
		byte *backing;
		size_t bytes = (size_t)gl_lms.block_size * gl_lms.block_size * LIGHTMAP_BYTES;

		qglTexImage2D (GL_TEXTURE_2D,
			0,
			gl_lms.internal_format,
			gl_lms.block_size, gl_lms.block_size,
			0,
			GL_LIGHTMAP_FORMAT,
			gl_lms.upload_type,
			gl_lms.lightmap_buffer);
		/* An unavailable mirror leaves later updates on the original direct path. */
		backing = malloc (bytes);
		if (backing)
			memcpy (backing, gl_lms.lightmap_buffer, bytes);
		free (gl_lms.static_backing[texture]);
		gl_lms.static_backing[texture] = backing;
		gl_lms.current_lightmap_texture++;
		gl_lms.dirty = false;
	}
}

// returns a texture number and the position inside it
static qboolean LM_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;

	best = gl_lms.block_size;

	if (w < 1 || h < 1 || w > gl_lms.block_size || h > gl_lms.block_size)
		return false;
	for (i = 0; i <= gl_lms.block_size - w; i++)
	{
		best2 = 0;

		for (j=0 ; j<w ; j++)
		{
			if (gl_lms.allocated[i+j] >= best)
				break;
			if (gl_lms.allocated[i+j] > best2)
				best2 = gl_lms.allocated[i+j];
		}
		if (j == w)
		{	// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > gl_lms.block_size)
		return false;

	for (i=0 ; i<w ; i++)
		gl_lms.allocated[*x + i] = best + h;

	return true;
}

void GL_DecoupledLightmapST (const msurface_t *surf, const float *v,
	int block_size, float *s, float *t)
{
	*s = DotProduct (v, surf->lm_axis[0]) + surf->lm_axis[0][3];
	*t = DotProduct (v, surf->lm_axis[1]) + surf->lm_axis[1][3];
	if (*s < 0)
		*s = 0;
	if (*t < 0)
		*t = 0;
	if (*s > surf->extents[0] / 16)
		*s = surf->extents[0] / 16;
	if (*t > surf->extents[1] / 16)
		*t = surf->extents[1] / 16;
	*s = (*s + surf->light_s + 0.5f) / block_size;
	*t = (*t + surf->light_t + 0.5f) / block_size;
}

/*
================
GL_BuildPolygonFromSurface
================
*/
void GL_BuildPolygonFromSurface(msurface_t *fa)
{
	int			i, lindex, lnumverts;
	medge_t		*pedges, *r_pedge;
//	int			vertpage;
	float		*vec;
	float		s, t;
	float uv_scale = (fa->texinfo->flags & SURF_N64_UV) ? 0.5f : 1.0f;
	glpoly_t	*poly;
	vec3_t		total;

// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;
	//vertpage = 0;

	VectorClear (total);
	//
	// draw texture
	//
	poly = Hunk_Alloc (sizeof(glpoly_t) + (lnumverts-4) * VERTEXSIZE*sizeof(float));

	poly->chain = NULL;
	poly->vbo_firstvertex = -1;

	poly->next = fa->polys;
	poly->flags = fa->flags;
	poly->numverts = lnumverts;

	fa->polys = poly;

	for (i=0 ; i<lnumverts ; i++)
	{
		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = currentmodel->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = currentmodel->vertexes[r_pedge->v[1]].position;
		}
			
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->image->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->image->height;

		VectorAdd (total, vec, total);
		FastVectorCopy (*vec, poly->verts[i]);
		poly->verts[i][3] = s * uv_scale;
		poly->verts[i][4] = t * uv_scale;

		//
		// lightmap texture coordinates
		//
		if (fa->lm_decoupled)
		{
			GL_DecoupledLightmapST (fa, vec, gl_lms.block_size, &s, &t);
		}
		else
		{
			s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
			s -= fa->texturemins[0];
			s += fa->light_s * 16;
			s += 8;
			s /= gl_lms.block_size * 16;

			t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
			t -= fa->texturemins[1];
			t += fa->light_t * 16;
			t += 8;
			t /= gl_lms.block_size * 16;
		}

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
		memset (&poly->verts[i][7], 255, sizeof (float));
	}

	//poly->numverts = lnumverts;
}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
static qboolean GL_CreateSurfaceLightmapInternal (msurface_t *surf, qboolean optional)
{
	int		smax, tmax;
	byte	*base;

	if (optional)
	{
		/* An unsupported replacement face keeps its legacy base texture. */
		surf->lightmaptexturenum = 0;
		surf->light_s = surf->light_t = 0;
		if (!surf->lm_decoupled || !surf->samples ||
			(surf->flags & SURF_DRAWSKY))
			return false;
	}
	else if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
		return true;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	if (smax < 1 || tmax < 1 || smax > MAX_LIGHTMAP_EXTENTS ||
		tmax > MAX_LIGHTMAP_EXTENTS || smax > gl_lms.block_size ||
		tmax > gl_lms.block_size)
	{
		if (optional)
			return false;
		ri.Sys_Error (ERR_DROP, "GL_CreateSurfaceLightmap: %d x %d exceeds %d atlas",
			smax, tmax, gl_lms.block_size);
	}
	if (gl_lms.current_lightmap_texture > gl_lms.max_static_lightmaps)
	{
		if (optional)
			return false;
		ri.Sys_Error (ERR_DROP, "GL_CreateSurfaceLightmap: lightmap atlas budget exceeded");
	}

	if ( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
	{
		LM_UploadBlock( false );
		if (gl_lms.current_lightmap_texture > gl_lms.max_static_lightmaps)
		{
			if (optional)
			{
				surf->light_s = surf->light_t = 0;
				return false;
			}
			ri.Sys_Error (ERR_DROP, "GL_CreateSurfaceLightmap: lightmap atlas budget exceeded");
		}
		memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));
		if ( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
		{
			if (optional)
			{
				surf->light_s = surf->light_t = 0;
				return false;
			}
			ri.Sys_Error (ERR_DROP, "Consecutive calls to LM_AllocBlock(%d,%d) failed\n", smax, tmax);
		}
	}

	gl_lms.dirty = true;
	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	base = gl_lms.lightmap_buffer;
	base += (surf->light_t * gl_lms.block_size + surf->light_s) * LIGHTMAP_BYTES;

	R_SetCacheState( surf );
	R_BuildLightMap (surf, base, gl_lms.block_size * LIGHTMAP_BYTES);
	return true;
}

void GL_CreateSurfaceLightmap (msurface_t *surf)
{
	GL_CreateSurfaceLightmapInternal (surf, false);
}

qboolean GL_TryCreateSurfaceLightmap (msurface_t *surf)
{
	return GL_CreateSurfaceLightmapInternal (surf, true);
}


/*
==================
GL_BeginBuildingLightmaps

==================
*/
void GL_BeginBuildingLightmaps (qboolean decoupled, int min_side)
{
	static lightstyle_t	lightstyles[MAX_LIGHTSTYLES];
	int				i;
	GLuint textures[MAX_LIGHTMAPS];
	int bytes;
	int bits;
	int major = 0, minor = 0;
	GLint red_bits = 0, green_bits = 0, blue_bits = 0;
	GLenum prior_error;
	float requested_bits;
	float requested_precision;
	qboolean wants_packed10;

	GL_FreeStaticLightmapBackings ();
	GL_UpdateLighting ();
	requested_bits = ri.Cvar_Get ("gl_lightmap_bits", "0", CVAR_ARCHIVE)->value;
	gl_lms.block_size = decoupled ? DECOUPLED_LIGHTMAP_BLOCK : CLASSIC_LIGHTMAP_BLOCK;
	/* Zero keeps the existing classic/decoupled atlas sizes. Explicit values
	 * request a power-of-two atlas side, not texel color precision. Required
	 * bounded faces can grow this request; optional faces retain fallback. */
	if (requested_bits != 0 && requested_bits >= -FLT_MAX && requested_bits <= FLT_MAX)
	{
		bits = requested_bits <= 7 ? 7 : requested_bits >= 10 ? 10 :
																(int)requested_bits;
		gl_lms.block_size = 1 << bits;
	}
	while (gl_lms.block_size < min_side &&
		gl_lms.block_size < DECOUPLED_LIGHTMAP_BLOCK)
		gl_lms.block_size <<= 1;
	while (gl_lms.block_size > gl_config.max_texture_size &&
		gl_lms.block_size > CLASSIC_LIGHTMAP_BLOCK)
		gl_lms.block_size >>= 1;
	if (gl_lms.block_size > gl_config.max_texture_size)
		ri.Sys_Error (ERR_DROP, "GL_BeginBuildingLightmaps: GPU texture limit %d is below %d",
			gl_config.max_texture_size, CLASSIC_LIGHTMAP_BLOCK);
	if (gl_lms.block_size < min_side)
		ri.Sys_Error (ERR_DROP, "GL_BeginBuildingLightmaps: required lightmap side %d cannot fit a power-of-two atlas within GPU limit %d (renderer limit %d)",
			min_side, gl_config.max_texture_size, DECOUPLED_LIGHTMAP_BLOCK);
	bytes = gl_lms.block_size * gl_lms.block_size * LIGHTMAP_BYTES;
	gl_lms.max_static_lightmaps = LIGHTMAP_ATLAS_BUDGET / bytes;
	if (gl_lms.max_static_lightmaps > MAX_LIGHTMAPS - 1)
		gl_lms.max_static_lightmaps = MAX_LIGHTMAPS - 1;
	gl_lms.dirty = false;

	memset( gl_lms.allocated, 0, sizeof(gl_lms.allocated) );
	memset (gl_lms.lightmap_surfaces, 0, sizeof (gl_lms.lightmap_surfaces));
	memset (gl_lms.lightmap_buffer, 0, bytes);

	r_framecount = 1;		// no dlightcache

	GL_EnableMultitexture( true );
	GL_SelectTexture( GL_TEXTURE1);

	/*
	** setup the base lightstyles so the lightmaps won't have to be regenerated
	** the first time they're seen
	*/
	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
	{
		lightstyles[i].rgb[0] = 1;
		lightstyles[i].rgb[1] = 1;
		lightstyles[i].rgb[2] = 1;
		lightstyles[i].white = 3;
	}
	r_newrefdef.lightstyles = lightstyles;

	if (!gl_state.lightmap_textures)
	{
		gl_state.lightmap_textures	= TEXNUM_LIGHTMAPS;
//		gl_state.lightmap_textures	= gl_state.texture_extension_number;
//		gl_state.texture_extension_number = gl_state.lightmap_textures + MAX_LIGHTMAPS;
	}
	/* Texture names are reused across maps. Release old large atlases first. */
	for (i = 0; i < MAX_LIGHTMAPS; i++)
		textures[i] = gl_state.lightmap_textures + i;
	qglDeleteTextures (MAX_LIGHTMAPS, textures);
	for (i = 0; i < 2; i++)
	{
		if (gl_state.currenttextures[i] >= (unsigned)gl_state.lightmap_textures &&
			gl_state.currenttextures[i] < (unsigned)(gl_state.lightmap_textures + MAX_LIGHTMAPS))
			gl_state.currenttextures[i] = 0xFFFFFFFFu;
	}

	gl_lms.current_lightmap_texture = 1;

	/*
	** if mono lightmaps are enabled and we want to use alpha
	** blending (a,1-a) then we're likely running on a 3DLabs
	** Permedia2.  In a perfect world we'd use a GL_ALPHA lightmap
	** in order to conserve space and maximize bandwidth, however 
	** this isn't a perfect world.
	**
	** So we have to use alpha lightmaps, but stored in GL_RGBA format,
	** which means we only get 1/16th the color resolution we should when
	** using alpha lightmaps.  If we find another board that supports
	** only alpha lightmaps but that can at least support the GL_ALPHA
	** format then we should change this code to use real alpha maps.
	*/

	/*if ( toupper( gl_monolightmap->string[0] ) == 'A' )
	{
		gl_lms.internal_format = gl_tex_alpha_format;
	}
	else if ( toupper( gl_monolightmap->string[0] ) == 'C' )
	{
		gl_lms.internal_format = gl_tex_alpha_format;
	}
	else if ( toupper( gl_monolightmap->string[0] ) == 'I' )
	{
		gl_lms.internal_format = GL_INTENSITY8;
	}
	else if ( toupper( gl_monolightmap->string[0] ) == 'L' ) 
	{
		gl_lms.internal_format = GL_LUMINANCE8;
	}
	else
	{
		gl_lms.internal_format = gl_tex_solid_format;
	}*/
	gl_lms.internal_format = gl_tex_solid_format;
	gl_lms.upload_type = GL_UNSIGNED_BYTE;
	gl_lms.packed10 = false;
	requested_precision = ri.Cvar_Get ("gl_lightmap_precision", "0", CVAR_ARCHIVE)->value;
	wants_packed10 = requested_precision >= 1 && requested_precision <= FLT_MAX;
	if (wants_packed10 && qglGetTexLevelParameteriv && qglGetError &&
		gl_config.version_string &&
		sscanf (gl_config.version_string, "%d.%d", &major, &minor) == 2 &&
		(major > 1 || (major == 1 && minor >= 2)))
	{
		/* Do not probe over an earlier error or silently drain the queue. */
		prior_error = qglGetError ();
		if (prior_error == GL_NO_ERROR)
		{
			unsigned int opaque_black = 3u << 30;
			int texel;
			gl_lms.internal_format = GL_RGB10_A2;
			gl_lms.upload_type = GL_UNSIGNED_INT_2_10_10_10_REV;
			gl_lms.packed10 = true;
			/* Empty atlas cells must be opaque when the material shader
			 * classifies this RGBA storage format by its alpha channel. */
			for (texel = 0; texel < gl_lms.block_size * gl_lms.block_size; texel++)
				memcpy (gl_lms.lightmap_buffer + texel * LIGHTMAP_BYTES,
					&opaque_black, LIGHTMAP_BYTES);
		}
		else
			ri.Con_Printf (PRINT_ALL,
				"gl_lightmap_precision: pre-existing GL error 0x%x; skipping format probe.\n",
				(unsigned)prior_error);
	}

	/*
	** initialize the dynamic lightmap texture
	*/
	GL_Bind( gl_state.lightmap_textures + 0 );
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	qglTexImage2D (GL_TEXTURE_2D,
		0,
		gl_lms.internal_format,
		gl_lms.block_size, gl_lms.block_size,
		0,
		GL_LIGHTMAP_FORMAT,
		gl_lms.upload_type,
		gl_lms.lightmap_buffer);
	if (gl_lms.packed10)
	{
		qboolean valid = qglGetError () == GL_NO_ERROR;
		if (valid)
		{
			qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_RED_SIZE, &red_bits);
			qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_GREEN_SIZE, &green_bits);
			qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_BLUE_SIZE, &blue_bits);
			valid = qglGetError () == GL_NO_ERROR &&
				red_bits >= 10 && green_bits >= 10 && blue_bits >= 10;
		}
		if (!valid)
		{
			/* A rejected or downgraded atlas must never receive packed updates. */
			gl_lms.internal_format = gl_tex_solid_format;
			gl_lms.upload_type = GL_UNSIGNED_BYTE;
			gl_lms.packed10 = false;
			memset (gl_lms.lightmap_buffer, 0, bytes);
			qglTexImage2D (GL_TEXTURE_2D, 0, gl_lms.internal_format,
				gl_lms.block_size, gl_lms.block_size, 0, GL_LIGHTMAP_FORMAT,
				gl_lms.upload_type, gl_lms.lightmap_buffer);
		}
	}
	if (wants_packed10 && !gl_lms.packed10)
		ri.Con_Printf (PRINT_ALL, "gl_lightmap_precision: RGB10_A2 unavailable; using legacy lightmaps.\n");
}

/*
=======================
GL_EndBuildingLightmaps
=======================
*/
void GL_EndBuildingLightmaps (void)
{
	LM_UploadBlock( false );
	GL_EnableMultitexture( false );
}
