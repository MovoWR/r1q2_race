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
// gl_warp.c -- sky and water polygons

#include "gl_local.h"
#include <float.h>

extern	model_t	*loadmodel;

char	skyname[MAX_QPATH];
float	skyrotate;
vec3_t	skyaxis;
image_t	*sky_images[6];

msurface_t	*warpface;
static msurface_t *sky_material_faces;
static qboolean sky_cube_requested;

#define	SUBDIVIDE_SIZE	64
//#define	SUBDIVIDE_SIZE	1024

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = FLT_MAX;
	maxs[0] = maxs[1] = maxs[2] = -FLT_MAX;
	v = verts;
	for (i=0 ; i<numverts ; i++)
		for (j=0 ; j<3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;
	float	s, t;
	vec3_t	total;
	float	total_s, total_t;

	if (numverts > 60)
		ri.Sys_Error (ERR_DROP, "SubdividePolygon: numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5f;
		m = SUBDIVIDE_SIZE * (float)floor (m/SUBDIVIDE_SIZE + 0.5f);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		FastVectorCopy (*verts, *v);

		f = b = 0;
		v = verts;

		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (FLOAT_GE_ZERO(dist[j]))
			{
				if (f >= 64)
					ri.Sys_Error (ERR_DROP, "SubdividePolygon: front vertex overflow");
				FastVectorCopy (*v, front[f]);
				f++;
			}
			if (FLOAT_LE_ZERO(dist[j]))
			{
				if (b >= 64)
					ri.Sys_Error (ERR_DROP, "SubdividePolygon: back vertex overflow");
				FastVectorCopy (*v, back[b]);
				b++;
			}
			if (FLOAT_EQ_ZERO(dist[j]) || FLOAT_EQ_ZERO(dist[j+1]))
				continue;
			if (FLOAT_GT_ZERO (dist[j]) != FLOAT_GT_ZERO(dist[j+1]) )
			{
				if (f >= 64 || b >= 64)
					ri.Sys_Error (ERR_DROP, "SubdividePolygon: split vertex overflow");
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	// add a point in the center to help keep warp valid
	poly = Hunk_Alloc (sizeof(glpoly_t) + (numverts-2) * VERTEXSIZE*sizeof(float));
	
	poly->next = warpface->polys;
	poly->numverts = numverts+2;
	poly->chain = NULL;
	poly->flags = 0;

	memset (poly->verts, 0, sizeof(poly->verts));

	warpface->polys = poly;

	VectorClear (total);

	total_s = 0;
	total_t = 0;
	for (i=0 ; i<numverts ; i++, verts+= 3)
	{
		FastVectorCopy (*verts, poly->verts[i+1]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);

		total_s += s;
		total_t += t;
		VectorAdd (total, verts, total);

		poly->verts[i+1][3] = s;
		poly->verts[i+1][4] = t;
	}

	VectorScale (total, (1.0/numverts), poly->verts[0]);
	poly->verts[0][3] = total_s/numverts;
	poly->verts[0][4] = total_t/numverts;

	// copy first vertex to last
	memcpy (poly->verts[i+1], poly->verts[1], sizeof(poly->verts[0]));
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t		verts[64];
	int			numverts;
	int			i;
	int			lindex;
	float		*vec;

	warpface = fa;

	//
	// convert edges back to a normal polygon
	//
	numverts = 0;

	for (i=0 ; i<fa->numedges ; i++)
	{
		if (numverts >= 64)
			ri.Sys_Error (ERR_DROP, "GL_SubdivideSurface: input vertex overflow");
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		FastVectorCopy (*vec, verts[numverts]);
		numverts++;
	}

	SubdividePolygon (numverts, verts[0]);
}

//=========================================================



// speed up sin calculations - Ed
#ifdef WIN32
#pragma warning(disable : 4305)
#endif
float	r_turbsin[] =
{
	#include "warpsin.h"
};
#ifdef WIN32
#pragma warning(default : 4305)
#endif
//#define TURBSCALE (256.0 / (2 * M_PI))
#define TURBSCALE	40.74366543152520595686852575586922507168212759145447757326835836930F

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
/* Subdivided water polygons retain raw BSP texture axes. Donor UVs use the
 * original wall image dimensions, even when the animated upload differs. */
void GL_WaterTexCoord (const msurface_t *fa, const float *v, float *s, float *t)
{
	float scale = (fa->texinfo->flags & SURF_N64_UV) ? 0.5f : 1.0f;
	*s = (v[3] + fa->texinfo->vecs[0][3]) * scale / fa->texinfo->image->width;
	*t = (v[4] + fa->texinfo->vecs[1][3]) * scale / fa->texinfo->image->height;
}

void EmitWaterPolys (msurface_t *fa, image_t *image)
{
	qboolean bloom_shader, gpu_warp = false;
	glpoly_t	*p, *bp;
	float		*v;
	int i, pass;
	float		s, t, os, ot;
	float scroll, scroll_t;
	float uv_scale = (fa->texinfo->flags & SURF_N64_UV) ? 0.5f : 1.0f;
	float		rdt = r_newrefdef.time;

	GL_SurfaceScroll (fa, true, &scroll, &scroll_t);
	bloom_shader = image && GL_BeginWarpMaterialShader (image->has_alpha, GL_MODULATE, 0, 0, 1, image->glowtexnum, fa, fa->texinfo->is_lava && !(fa->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66)) && GL_ImageShaderIntensity (), &gpu_warp);
	if (!bloom_shader)
		bloom_shader = fa->texinfo->is_lava &&
				!(fa->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66)) &&
				GL_ImageShaderIntensity () ?
			GL_BeginLitMaterialShader (true, GL_MODULATE, 0, 0, 1, 0) :
			GL_BeginBloomShader (true, true, false);
	if (gpu_warp && GL_DrawIndexedWaterPolys (fa, false))
	{
		if (bloom_shader)
			GL_EndSurfaceShader ();
		return;
	}
	for (bp=fa->polys ; bp ; bp=bp->next)
	{
		p = bp;

		for (pass = 0; pass < 2; pass++)
		{
			if (pass && !GL_ShowTrisBegin (GL_WorldShowTrisMask ()))
				break;
			qglBegin (GL_TRIANGLE_FAN);
			for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE)
			{
				if (gpu_warp)
					GL_WaterTexCoord (fa, v, &s, &t);
				else
				{
					os = v[3] * uv_scale;
					ot = v[4] * uv_scale;

#if !id386
					s = os + r_turbsin[(int)((ot * 0.125f + r_newrefdef.time) * TURBSCALE) & 255];
#else
					s = os + r_turbsin[Q_ftol (((ot * 0.125f + rdt) * TURBSCALE)) & 255];
#endif
					s = s * (1.0f / 64) + scroll;

#if !id386
					t = ot + r_turbsin[(int)((os * 0.125f + rdt) * TURBSCALE) & 255];
#else
					t = ot + r_turbsin[Q_ftol (((os * 0.125f + rdt) * TURBSCALE)) & 255];
#endif
					t = t * (1.0f / 64) + scroll_t;
				}

				qglTexCoord2f (s, t);
				qglVertex3fv (v);
			}
			qglEnd ();
			if (pass)
				GL_ShowTrisEnd ();
		}
	}
	if (bloom_shader)
		GL_EndSurfaceShader ();
}

/* Keep the animated base coordinates separate from the world-space BSPX
 * lightmap projection. The caller binds both textures and owns GL state. */
void EmitLightmappedWaterPolys (msurface_t *fa, int block_size, qboolean gpu_warp)
{
	glpoly_t *p;
	float *v;
	float s, t, os, ot, scroll, scroll_t, lm_s, lm_t;
	float uv_scale = (fa->texinfo->flags & SURF_N64_UV) ? 0.5f : 1.0f;
	float rdt = r_newrefdef.time;
	int i, pass;

	GL_SurfaceScroll (fa, true, &scroll, &scroll_t);
	if (gpu_warp && GL_DrawIndexedWaterPolys (fa, true))
		return;

	for (p = fa->polys; p; p = p->next)
	{
		for (pass = 0; pass < 2; pass++)
		{
			if (pass && !GL_ShowTrisBegin (GL_WorldShowTrisMask ()))
				break;
			qglBegin (GL_TRIANGLE_FAN);
			for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE)
			{
				if (gpu_warp)
				{
					GL_WaterTexCoord (fa, v, &s, &t);
					qglMTexCoord2fSGIS (GL_TEXTURE0, s, t);
				}
				else
				{
					os = v[3] * uv_scale;
					ot = v[4] * uv_scale;
#if !id386
					s = os + r_turbsin[(int)((ot * 0.125f + rdt) * TURBSCALE) & 255];
					t = ot + r_turbsin[(int)((os * 0.125f + rdt) * TURBSCALE) & 255];
#else
					s = os + r_turbsin[Q_ftol (((ot * 0.125f + rdt) * TURBSCALE)) & 255];
					t = ot + r_turbsin[Q_ftol (((os * 0.125f + rdt) * TURBSCALE)) & 255];
#endif
					qglMTexCoord2fSGIS (GL_TEXTURE0, s * (1.0f / 64) + scroll,
						t * (1.0f / 64) + scroll_t);
				}

				GL_DecoupledLightmapST (fa, v, block_size, &lm_s, &lm_t);
				qglMTexCoord2fSGIS (GL_TEXTURE1, lm_s, lm_t);
				qglVertex3fv (v);
			}
			qglEnd ();
			if (pass)
				GL_ShowTrisEnd ();
		}
	}
}


//===================================================================


vec3_t	skyclip[6] = {
	{1,1,0},
	{1,-1,0},
	{0,-1,1},
	{0,1,1},
	{1,0,1},
	{-1,0,1} 
};
int	c_sky;

// 1 = s, 2 = t, 3 = 2048
int	st_to_vec[6][3] =
{
	{3,-1,2},
	{-3,1,2},

	{1,3,2},
	{-1,-3,2},

	{-2,-1,3},		// 0 degrees yaw, look straight up
	{2,-1,-3}		// look straight down

//	{-1,2,3},
//	{1,2,-3}
};

// s = [0]/[2], t = [1]/[2]
int	vec_to_st[6][3] =
{
	{-2,3,1},
	{2,3,-1},

	{1,3,2},
	{-1,3,-2},

	{-2,-1,3},
	{-2,1,-3}

//	{-1,2,3},
//	{1,2,-3}
};

float	skymins[2][6], skymaxs[2][6];
float	sky_min, sky_max;

void DrawSkyPolygon (int nump, vec3_t vecs)
{
	int		i,j;
	vec3_t	v, av;
	float	s, t, dv;
	int		axis;
	float	*vp;

	c_sky++;
#if 0
glBegin (GL_POLYGON);
for (i=0 ; i<nump ; i++, vecs+=3)
{
	VectorAdd(vecs, r_origin, v);
	qglVertex3fv (v);
}
glEnd();
return;
#endif
	// decide which face it maps to
	//VectorCopy (vec3_origin, v);
	VectorClear (v);
	for (i=0, vp=vecs ; i<nump ; i++, vp+=3)
	{
		VectorAdd (vp, v, v);
	}
	av[0] = (float)fabs(v[0]);
	av[1] = (float)fabs(v[1]);
	av[2] = (float)fabs(v[2]);
	if (av[0] > av[1] && av[0] > av[2])
	{
		if (FLOAT_LT_ZERO(v[0]))
			axis = 1;
		else
			axis = 0;
	}
	else if (av[1] > av[2] && av[1] > av[0])
	{
		if (FLOAT_LT_ZERO(v[1]))
			axis = 3;
		else
			axis = 2;
	}
	else
	{
		if (FLOAT_LT_ZERO(v[2]))
			axis = 5;
		else
			axis = 4;
	}

	// project new texture coords
	for (i=0 ; i<nump ; i++, vecs+=3)
	{
		j = vec_to_st[axis][2];
		if (j > 0)
			dv = vecs[j - 1];
		else
			dv = -vecs[-j - 1];
		if (dv < 0.001)
			continue;	// don't divide by zero
		j = vec_to_st[axis][0];
		if (j < 0)
			s = -vecs[-j -1] / dv;
		else
			s = vecs[j-1] / dv;
		j = vec_to_st[axis][1];
		if (j < 0)
			t = -vecs[-j -1] / dv;
		else
			t = vecs[j-1] / dv;

		if (s < skymins[0][axis])
			skymins[0][axis] = s;
		if (t < skymins[1][axis])
			skymins[1][axis] = t;
		if (s > skymaxs[0][axis])
			skymaxs[0][axis] = s;
		if (t > skymaxs[1][axis])
			skymaxs[1][axis] = t;
	}
}

#define	ON_EPSILON		0.1			// point on plane side epsilon
#define	MAX_CLIP_VERTS	64
void ClipSkyPolygon (int nump, vec3_t vecs, int stage)
{
	float	*norm;
	float	*v;
	qboolean	front, back;
	float	d, e;
	float	dists[MAX_CLIP_VERTS];
	int		sides[MAX_CLIP_VERTS];
	vec3_t	newv[2][MAX_CLIP_VERTS];
	int		newc[2];
	int		i, j;

	if (nump > MAX_CLIP_VERTS-2)
		ri.Sys_Error (ERR_DROP, "ClipSkyPolygon: MAX_CLIP_VERTS");
	if (stage == 6)
	{	// fully clipped, so draw it
		DrawSkyPolygon (nump, vecs);
		return;
	}

	front = back = false;
	norm = skyclip[stage];
	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		d = DotProduct (v, norm);
		if (d > ON_EPSILON)
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if (d < -ON_EPSILON)
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if (!front || !back)
	{	// not clipped
		ClipSkyPolygon (nump, vecs, stage+1);
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	FastVectorCopy (*vecs, *(vecs+(i*3)) );
	newc[0] = newc[1] = 0;

	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			FastVectorCopy (*v, newv[0][newc[0]]);
			newc[0]++;
			break;
		case SIDE_BACK:
			FastVectorCopy (*v, newv[1][newc[1]]);
			newc[1]++;
			break;
		case SIDE_ON:
			FastVectorCopy (*v, newv[0][newc[0]]);
			newc[0]++;
			FastVectorCopy (*v, newv[1][newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i+1]);
		for (j=0 ; j<3 ; j++)
		{
			e = v[j] + d*(v[j+3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	ClipSkyPolygon (newc[0], newv[0][0], stage+1);
	ClipSkyPolygon (newc[1], newv[1][0], stage+1);
}

/*
=================
R_AddSkySurface
=================
*/
static void R_AddGlobalSkySurface (msurface_t *fa)
{
	int			i;
	vec3_t		verts[MAX_CLIP_VERTS];
	glpoly_t	*p;

	// calculate vertex values for sky box
	for (p=fa->polys ; p ; p=p->next)
	{
		if (p->numverts > MAX_CLIP_VERTS - 2)
		{
			for (i = 1; i + 1 < p->numverts; i++)
			{
				VectorSubtract (p->verts[0], r_origin, verts[0]);
				VectorSubtract (p->verts[i], r_origin, verts[1]);
				VectorSubtract (p->verts[i + 1], r_origin, verts[2]);
				ClipSkyPolygon (3, verts[0], 0);
			}
			continue;
		}
		for (i=0 ; i<p->numverts ; i++)
		{
			VectorSubtract (p->verts[i], r_origin, verts[i]);
		}
		ClipSkyPolygon (p->numverts, verts[0], 0);
	}
}

void R_AddSkySurface (msurface_t *fa)
{
	if (sky_cube_requested && fa->texinfo && fa->texinfo->sky_material != SKY_MATERIAL_DEFAULT)
	{
		fa->texturechain = sky_material_faces;
		sky_material_faces = fa;
		return;
	}
	R_AddGlobalSkySurface (fa);
}


/*
==============
R_ClearSkyBox
==============
*/
void R_ClearSkyBox (void)
{
	int		i;
	sky_material_faces = NULL;

	for (i=0 ; i<6 ; i++)
	{
		skymins[0][i] = skymins[1][i] = 9999;
		skymaxs[0][i] = skymaxs[1][i] = -9999;
	}
}

/* Q2PRO texture.c packed layouts, legacy face rotations and sky.c cube
 * coordinates. Copyright (C) 2005-2008, 2018 Andrey Nazarov; GPL-2.0-or-later.
 * One privately owned cubemap follows the glow/bloom reserved texture names.
 */
#define SKY_CUBE_TEXTURE ((GLuint)(TEXNUM_IMAGES + 2 * MAX_GLTEXTURES + 5))
#define SKY_CUBE_MAX_BYTES (64u * 1024u * 1024u)
#define SKY_MATERIAL_MAX_BYTES (128u * 1024u * 1024u)
static cvar_t *gl_cubemaps, *gl_bilerp_skies;
cvar_t *gl_drawsky;
static GLuint sky_cube_texture;
static int sky_cube_size, sky_cube_filter;
static qboolean sky_cube_drawing;
static GLint sky_draw_active, sky_draw_binding;

typedef struct
{
	char name[32];
	int kind;
	GLuint textures[2];
	size_t bytes;
	qboolean failed;
} skymaterial_t;

static skymaterial_t sky_materials[MAX_SKY_MATERIALS];
static int sky_num_materials, sky_material_filter;
static size_t sky_material_bytes;
static const char *sky_cube_suffix[6] = { "rt", "lf", "up", "dn", "bk", "ft" };
static const byte sky_cube_layouts[6][14] = {
	{ 4, 3, 2, 1, 0, 1, 1, 0, 1, 2, 1, 1, 3, 1 },
	{ 3, 4, 2, 1, 0, 1, 1, 0, 1, 2, 1, 1, 1, 3 },
	{ 6, 1, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0 },
	{ 1, 6, 0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5 },
	{ 3, 2, 0, 0, 0, 1, 1, 0, 1, 1, 2, 0, 2, 1 },
	{ 2, 3, 0, 0, 1, 0, 0, 1, 1, 1, 0, 2, 1, 2 }
};
static const GLenum sky_unpack_names[6] = {
	GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS,
	GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST
};

typedef struct
{
	GLint active, binding, buffer, unpack[6];
	GLenum target;
	qboolean saved;
} skyuploadstate_t;

static void GL_DeleteSkyCube (void)
{
	if (sky_cube_texture && qglDeleteTextures)
		qglDeleteTextures (1, &sky_cube_texture);
	sky_cube_texture = 0;
	sky_cube_size = sky_cube_filter = 0;
}

void GL_SkyRegister (void)
{
	if (!gl_cubemaps)
		gl_cubemaps = ri.Cvar_Get ("gl_cubemaps", "0", CVAR_ARCHIVE);
	if (!gl_bilerp_skies)
		gl_bilerp_skies = ri.Cvar_Get ("gl_bilerp_skies", "1", 0);
	if (!gl_drawsky)
		gl_drawsky = ri.Cvar_Get ("gl_drawsky", "1", 0);
}

static int GL_SkyFilter (void)
{
	float value = gl_bilerp_skies ? gl_bilerp_skies->value : 1;
	return value == 0 ? GL_NEAREST : GL_LINEAR;
}

static qboolean GL_SkyRequested (void)
{
	float value;
	GL_SkyRegister ();
	value = gl_cubemaps->value;
	return value >= -FLT_MAX && value <= FLT_MAX && FLOAT_NE_ZERO (value) &&
		qglGetIntegerv && qglGetError && qglIsTexture && qglBindTexture &&
		qglActiveTextureARB && qglBindBuffer && qglPixelStorei &&
		qglTexImage2D && qglGetTexLevelParameteriv && qglTexParameteri &&
		qglDeleteTextures && qglTexCoord3f && GL_SkyShaderReady ();
}

static const byte *GL_SkyLayout (int width, int height)
{
	int i;
	if (width < 1 || height < 1 || width > MAX_TEXTURE_DIMENSIONS || height > MAX_TEXTURE_DIMENSIONS)
		return NULL;
	for (i = 0; i < 6; i++)
	{
		const byte *layout = sky_cube_layouts[i];
		if (width % layout[0] == 0 && height % layout[1] == 0 &&
			width / layout[0] == height / layout[1])
			return layout;
	}
	return NULL;
}

/* Four taps match the retained image resampler, without an atlas-sized copy. */
static qboolean GL_SkyCopyFace (byte *out, int output_size, const byte *pic,
	int width, int height, int startx, int starty, int input_size, int rotation)
{
	int x, y, i, channel, xs[2], ys[2], sx, sy;
	const byte *taps[4];
	if (!out || !pic || input_size < 1 || output_size < 1 || output_size > MAX_TEXTURE_DIMENSIONS ||
		width < 1 || height < 1 || width > MAX_TEXTURE_DIMENSIONS || height > MAX_TEXTURE_DIMENSIONS ||
		input_size > width || input_size > height || startx < 0 || starty < 0 ||
		startx > width - input_size || starty > height - input_size ||
		(size_t)output_size * output_size > SKY_CUBE_MAX_BYTES / 24 || rotation < -1 || rotation > 1)
		return false;
	for (y = 0; y < output_size; y++)
	{
		ys[0] = (int)(((size_t)y * 4 + 1) * input_size / ((size_t)output_size * 4));
		ys[1] = (int)(((size_t)y * 4 + 3) * input_size / ((size_t)output_size * 4));
		for (x = 0; x < output_size; x++, out += 4)
		{
			xs[0] = (int)(((size_t)x * 4 + 1) * input_size / ((size_t)output_size * 4));
			xs[1] = (int)(((size_t)x * 4 + 3) * input_size / ((size_t)output_size * 4));
			for (i = 0; i < 4; i++)
			{
				sx = xs[i & 1];
				sy = ys[i >> 1];
				if (rotation == 1)
				{
					int temp = sx;
					sx = input_size - 1 - sy;
					sy = temp;
				}
				else if (rotation == -1)
				{
					int temp = sx;
					sx = sy;
					sy = input_size - 1 - temp;
				}
				taps[i] = pic + ((size_t)(starty + sy) * width + startx + sx) * 4;
			}
			for (channel = 0; channel < 4; channel++)
				out[channel] = (byte)((taps[0][channel] + taps[1][channel] + taps[2][channel] + taps[3][channel]) >> 2);
		}
	}
	return true;
}

static void GL_SkyRestoreUpload (skyuploadstate_t *state)
{
	int i;
	if (!state->saved)
		return;
	qglActiveTextureARB (GL_TEXTURE0);
	qglBindTexture (state->target, (GLuint)state->binding);
	qglBindBuffer (GL_PIXEL_UNPACK_BUFFER, (GLuint)state->buffer);
	for (i = 0; i < 6; i++)
		qglPixelStorei (sky_unpack_names[i], state->unpack[i]);
	qglActiveTextureARB ((GLenum)state->active);
	state->saved = false;
}

static qboolean GL_SkySaveUploadTarget (skyuploadstate_t *state, GLenum target, GLenum binding)
{
	int i;
	GLint buffer = -1;
	memset (state, 0, sizeof (*state));
	state->target = target;
	if (!GL_ClearErrors ())
		return false;
	qglGetIntegerv (GL_ACTIVE_TEXTURE, &state->active);
	if (qglGetError () != GL_NO_ERROR)
		return false;
	qglActiveTextureARB (GL_TEXTURE0);
	qglGetIntegerv (binding, &state->binding);
	qglGetIntegerv (GL_PIXEL_UNPACK_BUFFER_BINDING, &state->buffer);
	for (i = 0; i < 6; i++)
		qglGetIntegerv (sky_unpack_names[i], &state->unpack[i]);
	if (qglGetError () != GL_NO_ERROR)
	{
		qglActiveTextureARB ((GLenum)state->active);
		return false;
	}
	state->saved = true;
	qglBindBuffer (GL_PIXEL_UNPACK_BUFFER, 0);
	for (i = 0; i < 6; i++)
		qglPixelStorei (sky_unpack_names[i], i == 0 ? 1 : 0);
	qglGetIntegerv (GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
	if (qglGetError () == GL_NO_ERROR && buffer == 0)
		return true;
	GL_SkyRestoreUpload (state);
	return false;
}

static qboolean GL_SkySaveUpload (skyuploadstate_t *state)
{
	return GL_SkySaveUploadTarget (state, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BINDING_CUBE_MAP);
}

static void GL_SkySetParameters (int filter)
{
	qglTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, filter);
	qglTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, filter);
	qglTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	qglTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	qglTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

static int GL_SkyUploadSize (int input_size, int maximum)
{
	int size = input_size;
	if (size < 1 || size > MAX_TEXTURE_DIMENSIONS || maximum < 1)
		return 0;
	while (size > maximum || (size_t)size * size > SKY_CUBE_MAX_BYTES / 24)
		size /= 2;
	return size > 0 ? size : 1;
}

void GL_ClearSkyMaterials (void)
{
	int i;
	for (i = 0; i < sky_num_materials; i++)
	{
		if (sky_materials[i].textures[0] && qglDeleteTextures)
			qglDeleteTextures (sky_materials[i].kind == SKY_MATERIAL_CLASSIC ? 2 : 1,
				sky_materials[i].textures);
	}
	memset (sky_materials, 0, sizeof (sky_materials));
	sky_num_materials = 0;
	sky_material_bytes = 0;
	sky_material_filter = GL_SkyFilter ();
	sky_material_faces = NULL;
}

static qboolean GL_BuildMaterialCube (skymaterial_t *material)
{
	byte *pic = NULL, *buffer = NULL;
	const byte *layout;
	skyuploadstate_t state;
	GLint maximum = 0, bound = 0, w = 0, h = 0;
	int width = 0, height = 0, input_size, output_size, i;
	GLuint texture = 0;
	qboolean claimed = false, success = false;
	memset (&state, 0, sizeof (state));
	if (!qglIsTexture || !GL_ClearErrors ())
		return false;
	qglGetIntegerv (GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maximum);
	if (qglGetError () != GL_NO_ERROR || maximum < 1)
		return false;
	pic = GL_LoadSkyPixels (material->name, &width, &height);
	layout = pic ? GL_SkyLayout (width, height) : NULL;
	if (!layout)
		goto done;
	input_size = width / layout[0];
	output_size = GL_SkyUploadSize (input_size, maximum);
	if (!output_size)
		goto done;
	if ((size_t)output_size * output_size * 24 >
		SKY_MATERIAL_MAX_BYTES - sky_material_bytes)
		goto done;
	buffer = malloc ((size_t)output_size * output_size * 4);
	if (!buffer || !GL_SkySaveUpload (&state))
		goto done;
	texture = SKY_MATERIAL_TEXTURE_FIRST + 2u * (GLuint)(material - sky_materials);
	if (qglIsTexture (texture) || qglGetError () != GL_NO_ERROR)
		goto done;
	qglBindTexture (GL_TEXTURE_CUBE_MAP, texture);
	claimed = true;
	qglGetIntegerv (GL_TEXTURE_BINDING_CUBE_MAP, &bound);
	if (qglGetError () != GL_NO_ERROR || (GLuint)bound != texture)
		goto done;
	GL_SkySetParameters (GL_SkyFilter ());
	if (qglGetError () != GL_NO_ERROR)
		goto done;
	GL_SkyColorPixels (pic, width, height);
	for (i = 0; i < 6; i++)
	{
		if (!GL_SkyCopyFace (buffer, output_size, pic, width, height,
				layout[2 + i * 2] * input_size, layout[3 + i * 2] * input_size,
				input_size, 0))
			goto done;
		qglTexImage2D (GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
			GL_TextureCubeFormat (), output_size, output_size, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, buffer);
		qglGetTexLevelParameteriv (GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_TEXTURE_WIDTH, &w);
		qglGetTexLevelParameteriv (GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_TEXTURE_HEIGHT, &h);
		if (qglGetError () != GL_NO_ERROR || w != output_size || h != output_size)
			goto done;
	}
	success = true;
done:
	GL_SkyRestoreUpload (&state);
	if (success)
	{
		material->textures[0] = texture;
		material->bytes = (size_t)output_size * output_size * 24;
		sky_material_bytes += material->bytes;
	}
	else if (claimed)
		qglDeleteTextures (1, &texture);
	free (pic);
	free (buffer);
	return success;
}

static qboolean GL_BuildClassicSky (skymaterial_t *material)
{
	char pathname[MAX_QPATH];
	byte *pic = NULL;
	skyuploadstate_t state;
	GLint maximum = 0, bound = 0, w = 0, h = 0;
	int width = 0, height = 0, half, i;
	GLuint textures[2] = { 0, 0 };
	qboolean claimed[2] = { false, false }, success = false;
	memset (&state, 0, sizeof (state));
	if (!qglIsTexture || !GL_ClassicSkyShaderReady () ||
		strlen (material->name) + sizeof ("textures/") > sizeof (pathname) ||
		!GL_ClearErrors ())
		return false;
	Com_sprintf (pathname, sizeof (pathname), "textures/%s", material->name);
	pic = GL_LoadSkyPixels (pathname, &width, &height);
	if (!pic || width < 1 || height < 2 || (height & 1) ||
		(size_t)width * height > SKY_CUBE_MAX_BYTES / 4)
		goto done;
	half = height / 2;
	if ((size_t)width * height * 4 > SKY_MATERIAL_MAX_BYTES - sky_material_bytes)
		goto done;
	qglGetIntegerv (GL_MAX_TEXTURE_SIZE, &maximum);
	if (qglGetError () != GL_NO_ERROR || width > maximum || half > maximum)
		goto done;
	if (!GL_SkySaveUploadTarget (&state, GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D))
		goto done;
	textures[0] = SKY_MATERIAL_TEXTURE_FIRST + 2u * (GLuint)(material - sky_materials);
	textures[1] = textures[0] + 1u;
	for (i = 0; i < 2; i++)
		if (qglIsTexture (textures[i]) || qglGetError () != GL_NO_ERROR)
			goto done;
	GL_SkyColorPixels (pic, width, height);
	for (i = 0; i < 2; i++)
	{
		qglBindTexture (GL_TEXTURE_2D, textures[i]);
		claimed[i] = true;
		qglGetIntegerv (GL_TEXTURE_BINDING_2D, &bound);
		if (qglGetError () != GL_NO_ERROR || (GLuint)bound != textures[i])
			goto done;
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_SkyFilter ());
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_SkyFilter ());
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		qglTexImage2D (GL_TEXTURE_2D, 0, GL_TextureCubeFormat (), width, half,
			0, GL_RGBA, GL_UNSIGNED_BYTE,
			pic + (i ? 0 : (size_t)width * half * 4));
		qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
		qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
		if (qglGetError () != GL_NO_ERROR || w != width || h != half)
			goto done;
	}
	success = true;
done:
	GL_SkyRestoreUpload (&state);
	if (success)
	{
		memcpy (material->textures, textures, sizeof (textures));
		material->bytes = (size_t)width * height * 4;
		sky_material_bytes += material->bytes;
	}
	else
		for (i = 0; i < 2; i++)
			if (claimed[i])
				qglDeleteTextures (1, textures + i);
	free (pic);
	return success;
}

static skymaterial_t *GL_FindSkyMaterial (const mtexinfo_t *texinfo)
{
	skymaterial_t *material;
	int i;
	for (i = 0; i < sky_num_materials; i++)
	{
		material = &sky_materials[i];
		if (material->kind == texinfo->sky_material &&
			!strcmp (material->name, texinfo->sky_name))
			return material->failed ? NULL : material;
	}
	if (sky_num_materials == MAX_SKY_MATERIALS)
		return NULL;
	material = &sky_materials[sky_num_materials++];
	material->kind = texinfo->sky_material;
	Q_strncpy (material->name, texinfo->sky_name, sizeof (material->name) - 1);
	if (!(material->kind == SKY_MATERIAL_CUBE ?
				GL_BuildMaterialCube (material) :
				GL_BuildClassicSky (material)))
	{
		material->failed = true;
		return NULL;
	}
	return material;
}

static qboolean GL_BuildSkyCube (void)
{
	char pathname[MAX_QPATH];
	byte *pic = NULL, *buffer = NULL;
	const byte *layout = NULL;
	skyuploadstate_t state;
	GLint maximum = 0, bound = 0, w = 0, h = 0;
	int width = 0, height = 0, input_size, output_size, i, startx, starty, rotation;
	qboolean claimed = false, success = false;
	GLuint texture = SKY_CUBE_TEXTURE;
	memset (&state, 0, sizeof (state));
	if (!GL_SkyRequested () || !GL_ClearErrors ())
		return false;
	qglGetIntegerv (GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maximum);
	if (qglIsTexture (texture) || qglGetError () != GL_NO_ERROR || maximum < 1)
		return false;
	if (strlen (skyname) + sizeof ("sky/") > sizeof (pathname))
		return false;
	Com_sprintf (pathname, sizeof (pathname), "sky/%s", skyname);
	pic = GL_LoadSkyPixels (pathname, &width, &height);
	if (pic)
		layout = GL_SkyLayout (width, height);
	if (!layout)
	{
		free (pic);
		pic = NULL;
		if (strlen (skyname) + sizeof ("env/rt") > sizeof (pathname))
			return false;
		Com_sprintf (pathname, sizeof (pathname), "env/%srt", skyname);
		pic = GL_LoadSkyPixels (pathname, &width, &height);
		if (!pic || width != height || width < 1 || width > MAX_TEXTURE_DIMENSIONS)
			goto done;
	}
	input_size = layout ? width / layout[0] : width;
	output_size = GL_SkyUploadSize (input_size, maximum);
	if (!output_size)
		goto done;
	buffer = malloc ((size_t)output_size * output_size * 4);
	if (!buffer || !GL_SkySaveUpload (&state))
		goto done;
	qglBindTexture (GL_TEXTURE_CUBE_MAP, texture);
	claimed = true;
	qglGetIntegerv (GL_TEXTURE_BINDING_CUBE_MAP, &bound);
	if (qglGetError () != GL_NO_ERROR || (GLuint)bound != texture)
		goto done;
	GL_SkySetParameters (GL_SkyFilter ());
	if (qglGetError () != GL_NO_ERROR)
		goto done;
	GL_SkyColorPixels (pic, width, height);
	for (i = 0; i < 6; i++)
	{
		if (!layout && i)
		{
			free (pic);
			pic = NULL;
			Com_sprintf (pathname, sizeof (pathname), "env/%s%s", skyname, sky_cube_suffix[i]);
			pic = GL_LoadSkyPixels (pathname, &width, &height);
			if (!pic || width != input_size || height != input_size)
				goto done;
			GL_SkyColorPixels (pic, width, height);
		}
		startx = layout ? layout[2 + i * 2] * input_size : 0;
		starty = layout ? layout[3 + i * 2] * input_size : 0;
		rotation = layout ? 0 : i == 2 ? 1 :
			i == 3					   ? -1 :
										 0;
		if (!GL_SkyCopyFace (buffer, output_size, pic, width, height, startx, starty, input_size, rotation))
			goto done;
		qglTexImage2D (GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_TextureCubeFormat (), output_size, output_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
		qglGetTexLevelParameteriv (GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_TEXTURE_WIDTH, &w);
		qglGetTexLevelParameteriv (GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_TEXTURE_HEIGHT, &h);
		if (qglGetError () != GL_NO_ERROR || w != output_size || h != output_size)
			goto done;
	}
	success = true;
done:
	GL_SkyRestoreUpload (&state);
	if (!success && claimed)
		qglDeleteTextures (1, &texture);
	if (success)
	{
		sky_cube_texture = texture;
		sky_cube_size = output_size;
		sky_cube_filter = GL_SkyFilter ();
	}
	free (pic);
	free (buffer);
	return success;
}

void GL_UpdateSky (void)
{
	qboolean requested = GL_SkyRequested ();
	skyuploadstate_t state;
	GLint bound = 0;
	int filter = GL_SkyFilter ();
	if (requested != sky_cube_requested)
	{
		GL_DeleteSkyCube ();
		GL_ClearSkyMaterials ();
		sky_cube_requested = requested;
		if (requested && skyname[0])
			GL_BuildSkyCube ();
	}
	if (filter != sky_material_filter)
		GL_ClearSkyMaterials ();
	if (sky_cube_texture && filter != sky_cube_filter && GL_SkySaveUpload (&state))
	{
		qglBindTexture (GL_TEXTURE_CUBE_MAP, sky_cube_texture);
		qglGetIntegerv (GL_TEXTURE_BINDING_CUBE_MAP, &bound);
		if (qglGetError () == GL_NO_ERROR && (GLuint)bound == sky_cube_texture)
		{
			GL_SkySetParameters (filter);
			if (qglGetError () == GL_NO_ERROR)
				sky_cube_filter = filter;
		}
		GL_SkyRestoreUpload (&state);
	}
}

void GL_ShutdownSky (void)
{
	GL_EndSkyShader ();
	GL_ClearSkyMaterials ();
	GL_DeleteSkyCube ();
	sky_cube_requested = sky_cube_drawing = false;
	gl_cubemaps = gl_bilerp_skies = gl_drawsky = NULL;
}

static qboolean GL_SkyBeginDraw (void)
{
	GLint bound = 0;
	if (!sky_cube_texture || !GL_ClearErrors ())
		return false;
	qglGetIntegerv (GL_ACTIVE_TEXTURE, &sky_draw_active);
	if (qglGetError () != GL_NO_ERROR)
		return false;
	qglActiveTextureARB (GL_TEXTURE0);
	qglGetIntegerv (GL_TEXTURE_BINDING_CUBE_MAP, &sky_draw_binding);
	if (qglGetError () != GL_NO_ERROR)
	{
		qglActiveTextureARB ((GLenum)sky_draw_active);
		return false;
	}
	qglBindTexture (GL_TEXTURE_CUBE_MAP, sky_cube_texture);
	qglGetIntegerv (GL_TEXTURE_BINDING_CUBE_MAP, &bound);
	if (qglGetError () == GL_NO_ERROR && (GLuint)bound == sky_cube_texture && GL_BeginSkyShader ())
	{
		sky_cube_drawing = true;
		return true;
	}
	qglBindTexture (GL_TEXTURE_CUBE_MAP, (GLuint)sky_draw_binding);
	qglActiveTextureARB ((GLenum)sky_draw_active);
	GL_DeleteSkyCube ();
	return false;
}

static void GL_SkyEndDraw (void)
{
	GL_EndSkyShader ();
	qglActiveTextureARB (GL_TEXTURE0);
	qglBindTexture (GL_TEXTURE_CUBE_MAP, (GLuint)sky_draw_binding);
	qglActiveTextureARB ((GLenum)sky_draw_active);
	sky_cube_drawing = false;
}

typedef struct
{
	GLenum texture_unit;
	qboolean bloom_shader;
} blackskystate_t;

static void GL_BeginBlackSky (blackskystate_t *state)
{
	state->texture_unit = gl_state.currenttarget;
	qglPushAttrib (GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT);
	/* Texture attribs restore the server selector; leave array/cache state alone. */
	if (qglActiveTextureARB)
	{
		qglActiveTextureARB (GL_TEXTURE1);
		qglDisable (GL_TEXTURE_2D);
		qglActiveTextureARB (GL_TEXTURE0);
	}
	else if (qglSelectTextureSGIS)
	{
		GL_SelectTexture (GL_TEXTURE1);
		qglDisable (GL_TEXTURE_2D);
		GL_SelectTexture (GL_TEXTURE0);
	}
	qglDisable (GL_TEXTURE_2D);
	qglDisable (GL_BLEND);
	qglDisable (GL_ALPHA_TEST);
	qglDisable (GL_FOG);
	qglDisable (GL_LIGHTING);
	qglColor4f (0, 0, 0, 1);
	state->bloom_shader = GL_BeginBloomShader (false, false, false);
}

static void GL_EndBlackSky (const blackskystate_t *state)
{
	if (state->bloom_shader)
		GL_EndSurfaceShader ();
	qglPopAttrib ();
	if (!qglActiveTextureARB && qglSelectTextureSGIS)
		GL_SelectTexture (state->texture_unit);
}

static void R_DrawBlackSkyFace (const msurface_t *surface)
{
	const glpoly_t *poly;
	int i;
	for (poly = surface->polys; poly; poly = poly->next)
	{
		qglBegin (GL_POLYGON);
		for (i = 0; i < poly->numverts; i++)
			qglVertex3fv (poly->verts[i]);
		qglEnd ();
		GL_ShowTrisVertices (GL_WorldShowTrisMask (), GL_TRIANGLE_FAN, poly->numverts,
			poly->verts[0], VERTEXSIZE * sizeof (float), 3);
	}
}

static qboolean GL_DrawMaterialSkyFace (const msurface_t *surface,
	const skymaterial_t *material, const vec3_t vieworg, const entity_t *entity)
{
	GLenum target = material->kind == SKY_MATERIAL_CUBE ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
	GLenum binding = material->kind == SKY_MATERIAL_CUBE ? GL_TEXTURE_BINDING_CUBE_MAP : GL_TEXTURE_BINDING_2D;
	GLint active = 0, saved0 = 0, saved1 = 0;
	glpoly_t *poly;
	vec3_t direction, rotated, forward, right, up, angles;
	int i;
	qboolean begun;
	if (entity)
	{
		FastVectorCopy (entity->angles, angles);
		AngleVectors (angles, forward, right, up);
	}
	if (!GL_ClearErrors ())
		return false;
	qglGetIntegerv (GL_ACTIVE_TEXTURE, &active);
	if (qglGetError () != GL_NO_ERROR)
		return false;
	qglActiveTextureARB (GL_TEXTURE0);
	qglGetIntegerv (binding, &saved0);
	if (material->kind == SKY_MATERIAL_CLASSIC)
	{
		qglActiveTextureARB (GL_TEXTURE1);
		qglGetIntegerv (GL_TEXTURE_BINDING_2D, &saved1);
	}
	if (qglGetError () != GL_NO_ERROR)
	{
		qglActiveTextureARB ((GLenum)active);
		return false;
	}
	qglActiveTextureARB (GL_TEXTURE0);
	qglBindTexture (target, material->textures[0]);
	if (material->kind == SKY_MATERIAL_CLASSIC)
	{
		qglActiveTextureARB (GL_TEXTURE1);
		qglBindTexture (GL_TEXTURE_2D, material->textures[1]);
	}
	qglActiveTextureARB (GL_TEXTURE0);
	if (qglGetError () != GL_NO_ERROR)
		begun = false;
	else
		begun = material->kind == SKY_MATERIAL_CUBE ?
			GL_BeginSkyShader () :
			GL_BeginClassicSkyShader (r_newrefdef.time);
	if (begun)
	{
		for (poly = surface->polys; poly; poly = poly->next)
		{
			qglBegin (GL_POLYGON);
			for (i = 0; i < poly->numverts; i++)
			{
				VectorSubtract (poly->verts[i], vieworg, direction);
				if (entity)
				{
					rotated[0] = direction[0] * forward[0] - direction[1] * right[0] + direction[2] * up[0];
					rotated[1] = direction[0] * forward[1] - direction[1] * right[1] + direction[2] * up[1];
					rotated[2] = direction[0] * forward[2] - direction[1] * right[2] + direction[2] * up[2];
					FastVectorCopy (rotated, direction);
				}
				if (material->kind == SKY_MATERIAL_CUBE)
				{
					if (skyrotate)
						RotatePointAroundVector (rotated, skyaxis, direction,
							-r_newrefdef.time * skyrotate);
					else
						FastVectorCopy (direction, rotated);
					qglTexCoord3f (rotated[0], rotated[2], rotated[1]);
				}
				else
					qglTexCoord3f (direction[0], direction[1], direction[2] * 3.0f);
				qglVertex3fv (poly->verts[i]);
			}
			qglEnd ();
			GL_ShowTrisVertices (GL_WorldShowTrisMask (), GL_TRIANGLE_FAN, poly->numverts,
				poly->verts[0], VERTEXSIZE * sizeof (float), 3);
		}
		GL_EndSkyShader ();
	}
	if (material->kind == SKY_MATERIAL_CLASSIC)
	{
		qglActiveTextureARB (GL_TEXTURE1);
		qglBindTexture (GL_TEXTURE_2D, (GLuint)saved1);
	}
	qglActiveTextureARB (GL_TEXTURE0);
	qglBindTexture (target, (GLuint)saved0);
	qglActiveTextureARB ((GLenum)active);
	if (material->kind == SKY_MATERIAL_CLASSIC)
		gl_state.currenttextures[0] = gl_state.currenttextures[1] = 0xFFFFFFFF;
	return begun;
}

qboolean R_DrawBrushSkySurface (msurface_t *surface, const vec3_t vieworg)
{
	skymaterial_t fallback, *material = NULL;
	if (gl_drawsky && !gl_drawsky->value)
	{
		blackskystate_t state;
		GL_BeginBlackSky (&state);
		R_DrawBlackSkyFace (surface);
		GL_EndBlackSky (&state);
		return true;
	}
	if (!sky_cube_requested)
		return false;
	if (surface->texinfo->sky_material != SKY_MATERIAL_DEFAULT)
		material = GL_FindSkyMaterial (surface->texinfo);
	if (material && GL_DrawMaterialSkyFace (surface, material, vieworg, currententity))
		return true;
	if (!sky_cube_texture)
		return false;
	memset (&fallback, 0, sizeof (fallback));
	fallback.kind = SKY_MATERIAL_CUBE;
	fallback.textures[0] = sky_cube_texture;
	return GL_DrawMaterialSkyFace (surface, &fallback, vieworg, currententity);
}

qboolean R_SkyBrushModelRequested (void)
{
	return sky_cube_requested || (gl_drawsky && !gl_drawsky->value);
}

static qboolean R_ResolveMaterialSkies (void)
{
	msurface_t *surface, *next;
	skymaterial_t *material;
	qboolean present = false;
	for (surface = sky_material_faces; surface; surface = next)
	{
		next = surface->texturechain;
		material = GL_FindSkyMaterial (surface->texinfo);
		if (!material)
			R_AddGlobalSkySurface (surface);
		else
			present = true;
	}
	return present;
}

static qboolean R_DrawMaterialSkies (qboolean retry)
{
	msurface_t *surface, *next;
	skymaterial_t *material;
	qboolean failed = false;
	for (surface = sky_material_faces; surface; surface = next)
	{
		next = surface->texturechain;
		material = GL_FindSkyMaterial (surface->texinfo);
		if (material && !GL_DrawMaterialSkyFace (surface, material, r_origin, NULL) && !retry)
		{
			R_AddGlobalSkySurface (surface);
			failed = true;
		}
	}
	return failed;
}

void MakeSkyVec (float s, float t, int axis)
{
	vec3_t		v, b;
	int			j, k;

	b[0] = s * (gl_zfar->value / 2);
	b[1] = t * (gl_zfar->value / 2);
	b[2] = (gl_zfar->value / 2);

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			v[j] = -b[-k - 1];
		else
			v[j] = b[k - 1];
	}

	if (sky_cube_drawing)
	{
		qglTexCoord3f (v[0], v[2], v[1]);
		qglVertex3fv (v);
		return;
	}

	// avoid bilerp seam
	s = (s+1)*0.5f;
	t = (t+1)*0.5f;

	if (s < sky_min)
		s = sky_min;
	else if (s > sky_max)
		s = sky_max;
	if (t < sky_min)
		t = sky_min;
	else if (t > sky_max)
		t = sky_max;

	t = 1.0f - t;
	qglTexCoord2f (s, t);
	qglVertex3fv (v);
}

/*
==============
R_DrawSkyBox
==============
*/
int skytexorder[6] = { 0, 2, 1, 3, 4, 5 };

static void R_DrawGlobalSkyBox (qboolean material_skies)
{
	qboolean bloom_shader, cube_shader;
	qboolean saved_depth = true;
	GLint depth_write = GL_TRUE;
	blackskystate_t black_state;
	qboolean black_sky = gl_drawsky && !gl_drawsky->value;
	int		i;

#if 0
qglEnable (GL_BLEND);
GL_TexEnv( GL_MODULATE );
qglColor4f (1,1,1,0.5);
qglDisable (GL_DEPTH_TEST);
#endif
	if (skyrotate)
	{	// check for no sky at all
		for (i=0 ; i<6 ; i++)
			if (skymins[0][i] < skymaxs[0][i]
			&& skymins[1][i] < skymaxs[1][i])
				break;
		if (i == 6)
			return; // no global sky visible
	}
	if (material_skies)
	{
		qglGetIntegerv (GL_DEPTH_WRITEMASK, &depth_write);
		saved_depth = qglGetError () == GL_NO_ERROR;
		if (saved_depth)
			qglDepthMask (GL_FALSE);
	}

qglPushMatrix ();
qglTranslatef (r_origin[0], r_origin[1], r_origin[2]);
qglRotatef (r_newrefdef.time * skyrotate, skyaxis[0], skyaxis[1], skyaxis[2]);

if (black_sky)
	GL_BeginBlackSky (&black_state);
cube_shader = !black_sky && GL_SkyBeginDraw ();
bloom_shader = !black_sky && !cube_shader && GL_BeginBloomShader (true, false, false);
for (i = 0; i < 6; i++)
{
	if (skyrotate)
	{ // hack, forces full sky to draw when rotating
		skymins[0][i] = -1;
		skymins[1][i] = -1;
		skymaxs[0][i] = 1;
		skymaxs[1][i] = 1;
	}

	if (skymins[0][i] >= skymaxs[0][i] || skymins[1][i] >= skymaxs[1][i])
		continue;

	if (!black_sky && !cube_shader)
		GL_Bind (sky_images[skytexorder[i]]->texnum);

	qglBegin (GL_QUADS);
	MakeSkyVec (skymins[0][i], skymins[1][i], i);
	MakeSkyVec (skymins[0][i], skymaxs[1][i], i);
	MakeSkyVec (skymaxs[0][i], skymaxs[1][i], i);
	MakeSkyVec (skymaxs[0][i], skymins[1][i], i);
	qglEnd ();
	if (GL_ShowTrisBegin (GL_WorldShowTrisMask ()))
	{
		/* Match the core backend's quad diagonal: 0,1,3 and 1,2,3. */
		qglBegin (GL_TRIANGLES);
		MakeSkyVec (skymins[0][i], skymins[1][i], i);
		MakeSkyVec (skymins[0][i], skymaxs[1][i], i);
		MakeSkyVec (skymaxs[0][i], skymins[1][i], i);
		MakeSkyVec (skymins[0][i], skymaxs[1][i], i);
		MakeSkyVec (skymaxs[0][i], skymaxs[1][i], i);
		MakeSkyVec (skymaxs[0][i], skymins[1][i], i);
		qglEnd ();
		GL_ShowTrisEnd ();
	}
	}
qglPopMatrix ();
if (cube_shader)
	GL_SkyEndDraw ();
if (bloom_shader)
	GL_EndSurfaceShader ();
if (black_sky)
	GL_EndBlackSky (&black_state);
if (material_skies && saved_depth)
	qglDepthMask ((GLboolean)depth_write);
#if 0
glDisable (GL_BLEND);
glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
glColor4f (1,1,1,0.5);
glEnable (GL_DEPTH_TEST);
#endif
}

void R_DrawSkyBox (void)
{
	qboolean material_skies;
	msurface_t *surface, *next;
	blackskystate_t black_state;
	if (gl_drawsky && !gl_drawsky->value)
	{
		R_DrawGlobalSkyBox (sky_material_faces != NULL);
		if (sky_material_faces)
			GL_BeginBlackSky (&black_state);
		for (surface = sky_material_faces; surface; surface = next)
		{
			next = surface->texturechain;
			R_DrawBlackSkyFace (surface);
		}
		if (sky_material_faces)
			GL_EndBlackSky (&black_state);
		sky_material_faces = NULL;
		return;
	}
	material_skies = R_ResolveMaterialSkies ();
	R_DrawGlobalSkyBox (material_skies);
	if (material_skies && R_DrawMaterialSkies (false))
	{
		/* A draw-time shader failure still exposes the global sky. Repaint the
		 * surviving named faces after the global fallback pass. */
		R_DrawGlobalSkyBox (true);
		R_DrawMaterialSkies (true);
	}
	sky_material_faces = NULL;
}


/*
============
R_SetSky
============
*/
// 3dstudio environment map names
static const char	*suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
void EXPORT R_SetSky (char *name, float rotate, vec3_t axis)
{
	int		i;
	char	pathname[MAX_QPATH];

	Q_strncpy (skyname, name, sizeof(skyname)-1);
	skyrotate = rotate;
	FastVectorCopy (*axis, skyaxis);

	Q_strlwr (skyname);
	GL_DeleteSkyCube ();
	sky_cube_requested = GL_SkyRequested ();
	if (sky_cube_requested)
		GL_BuildSkyCube ();

	for (i=0 ; i<6 ; i++)
	{
		// chop down rotating skies for less memory
		if (FLOAT_NE_ZERO(gl_skymip->value) || skyrotate)
			gl_picmip->value++;

		Com_sprintf (pathname, sizeof(pathname), "env/%s%s.tga", skyname, suf[i]);

		sky_images[i] = GL_FindImage (pathname, skyname, it_sky);
		if (!sky_images[i])
			sky_images[i] = r_notexture;

		if (FLOAT_NE_ZERO(gl_skymip->value) || skyrotate)
		{	// take less memory
			gl_picmip->value--;
			sky_min = 1.0/256;
			sky_max = 255.0/256;
		}
		else	
		{
			sky_min = 1.0/512;
			sky_max = 511.0/512;
		}
	}
}
