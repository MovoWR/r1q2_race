/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2003-2006, 2008, 2018 Andrey Nazarov

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
// r_light.c

#include "gl_local.h"
#include <float.h>
#include <limits.h>

#define MAX_LIGHTMAP_EXTENTS 513

/* Q2PRO surf.c lightmap adjustment, retained renderer-private state. */
static unsigned lighting_sequence = 1;
static qboolean lighting_ready, lighting_shader, lighting_switchable;
static float lighting_modulate, lighting_brightness, lighting_colored;

static float GL_LightingValue (const cvar_t *var, float fallback, float low, float high)
{
	float value = var ? var->value : fallback;
	if (!(value >= -FLT_MAX && value <= FLT_MAX))
		value = fallback;
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

qboolean GL_LightingShader (void)
{
	/* This registered profile already admits every opaque material path. */
	return GL_ImageShaderIntensity ();
}

float GL_WorldModulate (void)
{
	return GL_LightingValue (gl_modulate, 1, 0, 1000000) *
		GL_LightingValue (gl_modulate_world, 1, 0, 1000000);
}

static float GL_EntityModulate (void)
{
	return GL_LightingValue (gl_modulate, 1, 0, 1000000) *
		GL_LightingValue (gl_modulate_entities, 1, 0, 1000000);
}

float GL_LightingBrightness (void)
{
	return GL_LightingValue (gl_brightness, 0, -1, 1);
}

static float GL_LightingColorScale (void)
{
	return GL_LightingValue (gl_coloredlightmaps, 1, 0, 1);
}

unsigned GL_LightingSequence (void)
{
	return lighting_sequence;
}

void GL_LightingRegister (void)
{
	lighting_sequence = 1;
	lighting_ready = false;
}

void GL_UpdateLighting (void)
{
	float modulate = GL_WorldModulate ();
	float brightness = GL_LightingBrightness ();
	float colored = GL_LightingColorScale ();
	qboolean shader = GL_LightingShader ();
	qboolean switchable = gl_dynamic->value == 2;

	if (lighting_ready && (switchable != lighting_switchable || colored != lighting_colored || shader != lighting_shader || (!shader && (modulate != lighting_modulate || brightness != lighting_brightness))))
	{
		/* Never wrap onto a generation still held by a hidden cached face. */
		if (lighting_sequence == UINT_MAX)
		{
			ri.Cvar_Get ("vid_ref", "gl", CVAR_ARCHIVE)->modified = true;
			return;
		}
		lighting_sequence++;
	}
	lighting_modulate = modulate;
	lighting_brightness = brightness;
	lighting_colored = colored;
	lighting_shader = shader;
	lighting_switchable = switchable;
	lighting_ready = true;
	usingmodifiedlightmaps = colored != 1;
	if (gl_coloredlightmaps->modified && gl_coloredlightmaps->value != colored)
		ri.Cvar_SetValue ("gl_coloredlightmaps", colored);
	gl_coloredlightmaps->modified = false;
}

static void GL_AdjustLighting (float *out, const float *in, float add,
	float modulate, float colored)
{
	float r = (in[0] + add) * modulate;
	float g = (in[1] + add) * modulate;
	float b = (in[2] + add) * modulate;
	float max, y;

	if (r < 0)
		r = 0;
	if (g < 0)
		g = 0;
	if (b < 0)
		b = 0;
	max = r > g ? r : g;
	if (b > max)
		max = b;
	if (max > 255)
	{
		y = 255 / max;
		r *= y;
		g *= y;
		b *= y;
	}
	if (colored != 1)
	{
		y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
		r = y + (r - y) * colored;
		g = y + (g - y) * colored;
		b = y + (b - y) * colored;
	}
	out[0] = r;
	out[1] = g;
	out[2] = b;
}

/* Match a face's lightmap samples, not R_LightPoint's downward world trace.
 * The latter can select a different surface (or a lightgrid) at a vertex. */
void R_SampleSurfaceVertexLight (const msurface_t *surf, float s, float t,
	byte color[4])
{
	vec3_t sampled = { 0, 0, 0 }, adjusted;
	int width, height, x, y, x1, y1, map;
	float fx, fy, w00, w10, w01, w11;
	const byte *samples;
	size_t size;

	color[0] = color[1] = color[2] = color[3] = 255;
	if (!surf || !surf->samples || !r_newrefdef.lightstyles)
		return;
	width = (surf->extents[0] >> 4) + 1;
	height = (surf->extents[1] >> 4) + 1;
	if (width < 1 || height < 1 || width > MAX_LIGHTMAP_EXTENTS ||
		height > MAX_LIGHTMAP_EXTENTS || !(s >= -FLT_MAX && s <= FLT_MAX) ||
		!(t >= -FLT_MAX && t <= FLT_MAX))
		return;
	if (s < 0)
		s = 0;
	if (t < 0)
		t = 0;
	if (s > width - 1)
		s = (float)(width - 1);
	if (t > height - 1)
		t = (float)(height - 1);
	x = (int)s;
	y = (int)t;
	x1 = x + (x + 1 < width);
	y1 = y + (y + 1 < height);
	fx = s - x;
	fy = t - y;
	w00 = (1 - fx) * (1 - fy);
	w10 = fx * (1 - fy);
	w01 = (1 - fx) * fy;
	w11 = fx * fy;
	size = (size_t)width * height * 3;
	for (map = 0, samples = surf->samples;
		 map < MAXLIGHTMAPS && surf->styles[map] != 255;
		 map++, samples += size)
	{
		const lightstyle_t *style = &r_newrefdef.lightstyles[surf->styles[map]];
		const byte *p00 = samples + 3 * (y * width + x);
		const byte *p10 = samples + 3 * (y * width + x1);
		const byte *p01 = samples + 3 * (y1 * width + x);
		const byte *p11 = samples + 3 * (y1 * width + x1);
		int channel;
		for (channel = 0; channel < 3; channel++)
			sampled[channel] += style->rgb[channel] *
				(w00 * p00[channel] + w10 * p10[channel] +
					w01 * p01[channel] + w11 * p11[channel]);
	}
	/* Vertex colors are consumed by both fixed-function and shader paths, so
	 * apply these controls here exactly once even when a shader is available. */
	GL_AdjustLighting (adjusted, sampled, 255 * GL_LightingBrightness (),
		GL_WorldModulate (), GL_LightingColorScale ());
	color[0] = (byte)adjusted[0];
	color[1] = (byte)adjusted[1];
	color[2] = (byte)adjusted[2];
}


int	r_dlightframecount;

#define	DLIGHT_CUTOFF	64

/* Inline BSP faces are shared by every instance, but each lightmap build
 * needs light origins in the current instance's local coordinates. */
static dlight_t inline_dlights[MAX_DLIGHTS];
static int inline_dlight_count;
static qboolean inline_dlights_active;

/*
=============================================================================

DYNAMIC LIGHTS BLEND RENDERING

=============================================================================
*/

void R_RenderDlight (dlight_t *light)
{
	int		i, j;
	float	a;
	vec3_t	v;
	float	rad;

	rad = light->intensity * 0.35f;

	VectorSubtract (light->origin, r_origin, v);
#if 0
	// FIXME?
	if (VectorLength (v) < rad)
	{	// view is inside the dlight
		V_AddBlend (light->color[0], light->color[1], light->color[2], light->intensity * 0.0003, v_blend);
		return;
	}
#endif

	qglBegin (GL_TRIANGLE_FAN);
	qglColor3f (light->color[0]*0.2f, light->color[1]*0.2f, light->color[2]*0.2f);
	for (i=0 ; i<3 ; i++)
		v[i] = light->origin[i] - vpn[i]*rad;
	qglVertex3fv (v);
	qglColor3f (0,0,0);
	for (i=16 ; i>=0 ; i--)
	{
		a = i/16.0f * M_PI*2;
		for (j=0 ; j<3 ; j++)
			v[j] = light->origin[j] + vright[j]*(float)cos(a)*rad
				+ vup[j]*(float)sin(a)*rad;
		qglVertex3fv (v);
	}
	qglEnd ();
}

/*
=============
R_RenderDlights
=============
*/
void R_RenderDlights (void)
{
	qboolean bloom_shader;
	int		i;
	dlight_t	*l;

	if (FLOAT_EQ_ZERO(gl_flashblend->value))
		return;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
	qglDepthMask (0);
	qglDisable (GL_TEXTURE_2D);
	qglShadeModel (GL_SMOOTH);
	qglEnable (GL_BLEND);
	qglBlendFunc (GL_ONE, GL_ONE);

	bloom_shader = GL_BeginBloomShader (false, false, true);
	l = r_newrefdef.dlights;
	for (i=0 ; i<r_newrefdef.num_dlights ; i++, l++)
		R_RenderDlight (l);

	qglColor3f (1,1,1);
	if (bloom_shader)
		GL_EndSurfaceShader ();
	qglDisable (GL_BLEND);
	qglEnable (GL_TEXTURE_2D);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask (1);
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
	cplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int			i;
	
	if (node->contents != -1)
		return;

	splitplane = node->plane;
	dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;
	
	if (dist > light->intensity-DLIGHT_CUTOFF)
	{
		R_MarkLights (light, bit, node->children[0]);
		return;
	}
	if (dist < -light->intensity+DLIGHT_CUTOFF)
	{
		R_MarkLights (light, bit, node->children[1]);
		return;
	}
		
// mark the polygons
	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->flags & SURF_BSPX_NODRAW)
			continue;

		/*dist = DotProduct (light->origin, surf->plane->normal) - surf->plane->dist;	//Discoloda
		if (dist >= 0)									//Discoloda
			sidebit = 0;								//Discoloda
		else										//Discoloda
			sidebit = SURF_PLANEBACK;						//Discoloda

		if ( (surf->flags & SURF_PLANEBACK) != sidebit )				//Discoloda
			continue;								//Discoloda*/

		if (surf->dlightframe != r_dlightframecount)
		{
			surf->dlightbits = 0;
			surf->dlightframe = r_dlightframecount;
		}
		surf->dlightbits |= bit;
	}

	R_MarkLights (light, bit, node->children[0]);
	R_MarkLights (light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (void)
{
	int		i;
	dlight_t	*l;

	if (FLOAT_NE_ZERO(gl_flashblend->value))
		return;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
	l = r_newrefdef.dlights;
	for (i = 0; i < r_newrefdef.num_dlights && i < MAX_DLIGHTS; i++, l++)
		R_MarkLights (l, (int)(1u << i), r_worldmodel->nodes);
}

void R_EndInlineDlights (void)
{
	inline_dlight_count = 0;
	inline_dlights_active = false;
}

void R_BeginInlineDlights (entity_t *entity, model_t *model)
{
	vec3_t relative, forward, right, up;
	dlight_t *light;
	int i, count, first;
	qboolean rotated;

	R_EndInlineDlights ();
	if (!entity || !model || !model->surfaces ||
		model->firstmodelsurface < 0 || model->nummodelsurfaces < 0 ||
		model->firstmodelsurface > model->numsurfaces ||
		model->nummodelsurfaces > model->numsurfaces - model->firstmodelsurface)
		return;

	/* A previous instance may have marked these same faces this frame.
	 * Preserve their style and lightmap caches for static restoration. */
	first = model->firstmodelsurface;
	for (i = 0; i < model->nummodelsurfaces; i++)
	{
		model->surfaces[first + i].dlightframe = -1;
		model->surfaces[first + i].dlightbits = 0;
	}
	inline_dlights_active = true;
	count = r_newrefdef.num_dlights;
	if (count <= 0 || !r_newrefdef.dlights || FLOAT_NE_ZERO (gl_flashblend->value))
		return;
	if (count > MAX_DLIGHTS)
		count = MAX_DLIGHTS;
	inline_dlight_count = count;
	rotated = entity->angles[0] || entity->angles[1] || entity->angles[2];
	if (rotated)
		AngleVectors (entity->angles, forward, right, up);
	for (i = 0; i < count; i++)
	{
		light = &inline_dlights[i];
		*light = r_newrefdef.dlights[i];
		VectorSubtract (light->origin, entity->origin, relative);
		if (rotated)
		{
			light->origin[0] = DotProduct (relative, forward);
			light->origin[1] = -DotProduct (relative, right);
			light->origin[2] = DotProduct (relative, up);
		}
		else
			FastVectorCopy (relative, light->origin);
	}

	if (!r_worldmodel || !model->nodes || model->firstnode < 0 ||
		model->firstnode >= model->numnodes)
		return;
	r_dlightframecount = r_framecount;
	for (i = 0; i < count; i++)
		R_MarkLights (&inline_dlights[i], (int)(1u << i),
			model->nodes + model->firstnode);
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

vec3_t			pointcolor;
cplane_t		*lightplane;		// used as shadow plane
vec3_t			lightspot;

int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	int			side;
	cplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	int			s, t, ds, dt;
	int			i;
	mtexinfo_t	*tex;
	byte		*lightmap;
	int			maps;
	int			r;
	float lm_s, lm_t;

	if (node->contents != -1)
		return -1;		// didn't hit anything
	
// calculate mid point

// FIXME: optimize for axial
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = FLOAT_LT_ZERO(front);
	
	if ((FLOAT_LT_ZERO (back)) == side)
		return RecursiveLightPoint (node->children[side], start, end);
	
	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;
	
// go down front side	
	r = RecursiveLightPoint (node->children[side], start, mid);
	if (r >= 0)
		return r;		// hit something
		
	if ((FLOAT_LT_ZERO (back)) == side )
		return -1;		// didn't hit anuthing
		
// check for impact on this node
	FastVectorCopy (mid, lightspot);
	lightplane = plane;

	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->flags & (SURF_DRAWTURB | SURF_DRAWSKY | SURF_BSPX_NODRAW))
			continue;	// no lightmaps

		tex = surf->texinfo;

		if (surf->lm_decoupled)
		{
			lm_s = DotProduct (mid, surf->lm_axis[0]) + surf->lm_axis[0][3];
			lm_t = DotProduct (mid, surf->lm_axis[1]) + surf->lm_axis[1][3];
			if (!(lm_s >= 0 && lm_s <= surf->extents[0] / 16 &&
					lm_t >= 0 && lm_t <= surf->extents[1] / 16))
				continue;
			ds = (int)lm_s;
			dt = (int)lm_t;
		}
		else
		{
			s = (int)(DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3]);
			t = (int)(DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3]);
			if (s < surf->texturemins[0] || t < surf->texturemins[1])
				continue;
			ds = s - surf->texturemins[0];
			dt = t - surf->texturemins[1];
			if (ds > surf->extents[0] || dt > surf->extents[1])
				continue;
			ds >>= 4;
			dt >>= 4;
		}

		if (!surf->samples)
		{
			VectorClear (pointcolor);
			return 0;
		}

		lightmap = surf->samples;
		VectorClear (pointcolor);

		if (lightmap)
		{
			vec3_t scale;

			lightmap += 3*(dt * ((surf->extents[0]>>4)+1) + ds);

			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
					maps++)
			{
				scale[0] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[0];
				scale[1] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[1];
				scale[2] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[2];

				pointcolor[0] += lightmap[0] * scale[0] * 0.003921568627450980392156862745098f;
				pointcolor[1] += lightmap[1] * scale[1] * 0.003921568627450980392156862745098f;
				pointcolor[2] += lightmap[2] * scale[2] * 0.003921568627450980392156862745098f;
				lightmap += 3*((surf->extents[0]>>4)+1) *
						((surf->extents[1]>>4)+1);
			}
		}
		
		return 1;
	}

// go down back side
	return RecursiveLightPoint (node->children[!side], mid, end);
}

/* Shadow lightmap tracing follows Q2PRO common/bsp.c and refresh/world.c
 * (Copyright (C) 2008 Andrey Nazarov; GPL version 2 or later). Modern shadows
 * and entity lighting share the query without sharing their result state. */
typedef struct
{
	mnode_t *node;
	double first, last;
} shadow_trace_entry_t;

typedef struct
{
	cplane_t plane;
	double fraction;
	const msurface_t *surface;
	double st[2];
} shadow_trace_hit_t;

/* Valid IBSP trees may be deeper than the QBSP depth limit. */
static shadow_trace_entry_t shadow_trace_stack[MAX_MAP_NODES];
static cplane_t shadow_plane;
static qboolean shadow_plane_valid;

const cplane_t *R_GetShadowPlane (void)
{
	return shadow_plane_valid ? &shadow_plane : NULL;
}

static qboolean R_ShadowSurface (const msurface_t *surf, const double point[3],
	double st[2], qboolean for_shadow)
{
	double coordinate, normal_length = 0;
	int axis, i;

	if (!surf->samples || !surf->texinfo || !surf->plane ||
		(surf->flags & (SURF_DRAWSKY | SURF_BSPX_NODRAW)) ||
		(surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66)))
		return false;
	/* Plain BSP NODRAW is ignored by the loader; only its private BSPX flag
	 * excludes a receiver. Shader-lit decoupled liquids may light entities,
	 * while their warped geometry remains unsuitable for ground shadows. */
	if (((surf->flags & SURF_DRAWTURB) || (surf->texinfo->flags & SURF_WARP)) &&
		(for_shadow || !surf->lm_decoupled || !GL_LightingShader () ||
			!GL_SurfaceShaderAvailable ()))
		return false;
	for (i = 0; i < 3; i++)
	{
		if (!(surf->plane->normal[i] >= -FLT_MAX && surf->plane->normal[i] <= FLT_MAX))
			return false;
		normal_length += (double)surf->plane->normal[i] * surf->plane->normal[i];
	}
	if (!(normal_length > 0) ||
		!(surf->plane->dist >= -FLT_MAX && surf->plane->dist <= FLT_MAX))
		return false;
	for (axis = 0; axis < 2; axis++)
	{
		const float *vector = surf->lm_decoupled ?
			surf->lm_axis[axis] :
			surf->texinfo->vecs[axis];
		coordinate = vector[3];
		for (i = 0; i < 3; i++)
			coordinate += point[i] * vector[i];
		if (!surf->lm_decoupled)
			coordinate -= surf->texturemins[axis];
		if (!(coordinate >= 0 && coordinate <= (surf->lm_decoupled ? surf->extents[axis] / 16.0 : surf->extents[axis])))
			return false;
		st[axis] = surf->lm_decoupled ? coordinate : coordinate / 16.0;
	}
	return true;
}

static qboolean R_ShadowTrace (const model_t *model, const double start[3],
	const double direction[3], shadow_trace_hit_t *hit, qboolean for_shadow)
{
	shadow_trace_entry_t entry;
	mnode_t *node;
	const msurface_t *surf;
	double first_distance, last_distance, fraction, point[3], st[2];
	int depth = 0, visited = 0, side, i, axis, index;
	qboolean found = false;

	if (!model->nodes || !model->surfaces || model->numnodes < 1 ||
		model->numnodes > MAX_MAP_NODES || model->firstnode < 0 ||
		model->firstnode >= model->numnodes || model->firstmodelsurface < 0 ||
		model->nummodelsurfaces < 1 || model->firstmodelsurface > model->numsurfaces ||
		model->nummodelsurfaces > model->numsurfaces - model->firstmodelsurface)
		return false;
	entry.node = model->nodes + model->firstnode;
	entry.first = 0;
	entry.last = hit->fraction;
	for (;;)
	{
		node = entry.node;
		if (entry.last > hit->fraction)
			entry.last = hit->fraction;
		if (node && node->contents == -1 && entry.first <= entry.last)
		{
			if (++visited > model->numnodes || !node->plane)
				return false;
			first_distance = last_distance = -node->plane->dist;
			for (axis = 0; axis < 3; axis++)
			{
				first_distance += (start[axis] + direction[axis] * entry.first) * node->plane->normal[axis];
				last_distance += (start[axis] + direction[axis] * entry.last) * node->plane->normal[axis];
			}
			side = first_distance < 0;
			if ((last_distance < 0) == side)
			{
				entry.node = node->children[side];
				continue;
			}
			fraction = entry.first + (entry.last - entry.first) * first_distance / (first_distance - last_distance);
			if (!(fraction >= entry.first && fraction <= entry.last))
				return false;
			for (axis = 0; axis < 3; axis++)
				point[axis] = start[axis] + direction[axis] * fraction;
			for (i = 0; i < node->numsurfaces; i++)
			{
				index = node->firstsurface + i;
				if (index < model->firstmodelsurface ||
					index - model->firstmodelsurface >= model->nummodelsurfaces)
					continue;
				surf = &model->surfaces[index];
				if (fraction >= hit->fraction || !R_ShadowSurface (surf, point, st, for_shadow))
					continue;
				hit->plane = *surf->plane;
				if (surf->flags & SURF_PLANEBACK)
				{
					for (axis = 0; axis < 3; axis++)
						hit->plane.normal[axis] = -hit->plane.normal[axis];
					hit->plane.dist = -hit->plane.dist;
				}
				hit->fraction = fraction;
				hit->surface = surf;
				hit->st[0] = st[0];
				hit->st[1] = st[1];
				found = true;
				break;
			}
			if (fraction < hit->fraction)
			{
				if (depth == MAX_MAP_NODES)
					return false;
				shadow_trace_stack[depth].node = node->children[side ^ 1];
				shadow_trace_stack[depth].first = fraction;
				shadow_trace_stack[depth++].last = entry.last;
			}
			entry.node = node->children[side];
			entry.last = fraction;
			continue;
		}
		if (!depth)
			break;
		entry = shadow_trace_stack[--depth];
	}
	return found;
}

static qboolean R_LightPointTrace (vec3_t p, shadow_trace_hit_t *result,
	qboolean for_shadow)
{
	shadow_trace_hit_t hit, candidate;
	const entity_t *entity;
	const model_t *model;
	vec3_t axes[3], angles;
	double start[3], direction[3], normal[3], distance, length;
	int count, i, j, axis;
	qboolean found, rotated, valid_bounds;

	if (!r_worldmodel || !r_worldmodel->nodes || !r_worldmodel->lightdata ||
		!r_newrefdef.lightstyles || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		if (!(p[axis] >= -FLT_MAX && p[axis] <= FLT_MAX))
			return false;
		start[axis] = p[axis];
		direction[axis] = axis == 2 ? -8192 : 0;
	}
	hit.fraction = 1;
	found = R_ShadowTrace (r_worldmodel, start, direction, &hit, for_shadow);
	if (!found)
		hit.fraction = 1;
	count = r_newrefdef.entities ? r_newrefdef.num_entities : 0;
	if (count > MAX_ENTITIES)
		count = MAX_ENTITIES;
	for (i = 0; i < count; i++)
	{
		entity = &r_newrefdef.entities[i];
		model = entity->model;
		if (!model || model == r_worldmodel || model->type != mod_brush ||
			(for_shadow && (entity->flags & RF_TRANSLUCENT)) ||
			(!for_shadow && (entity->flags & RF_BEAM)))
			continue;
		for (axis = 0; axis < 3; axis++)
			if (!(entity->origin[axis] >= -FLT_MAX && entity->origin[axis] <= FLT_MAX) ||
				!(entity->angles[axis] >= -FLT_MAX && entity->angles[axis] <= FLT_MAX))
				break;
		if (axis != 3)
			continue;
		/* Donor XY broad phase: a downward ray cannot enter distant brushes. */
		rotated = entity->angles[0] != 0 || entity->angles[1] != 0 || entity->angles[2] != 0;
		if (rotated)
		{
			if (model->radius >= 0 && model->radius <= FLT_MAX &&
				(fabs ((double)p[0] - entity->origin[0]) > model->radius ||
					fabs ((double)p[1] - entity->origin[1]) > model->radius))
				continue;
		}
		else
		{
			valid_bounds = true;
			for (axis = 0; axis < 2; axis++)
				if (!(model->mins[axis] >= -FLT_MAX && model->maxs[axis] <= FLT_MAX &&
						model->mins[axis] <= model->maxs[axis]))
					valid_bounds = false;
			if (valid_bounds &&
				((double)p[0] < (double)model->mins[0] + entity->origin[0] ||
					(double)p[0] > (double)model->maxs[0] + entity->origin[0] ||
					(double)p[1] < (double)model->mins[1] + entity->origin[1] ||
					(double)p[1] > (double)model->maxs[1] + entity->origin[1]))
				continue;
		}
		VectorCopy (entity->angles, angles);
		AngleVectors (angles, axes[0], axes[1], axes[2]);
		for (axis = 0; axis < 3; axis++)
			axes[1][axis] = -axes[1][axis];
		for (j = 0; j < 3; j++)
		{
			start[j] = 0;
			for (axis = 0; axis < 3; axis++)
				start[j] += ((double)p[axis] - entity->origin[axis]) * axes[j][axis];
			direction[j] = -8192.0 * axes[j][2];
		}
		candidate.fraction = hit.fraction;
		if (!R_ShadowTrace (model, start, direction, &candidate, for_shadow))
			continue;
		for (axis = 0; axis < 3; axis++)
		{
			normal[axis] = 0;
			for (j = 0; j < 3; j++)
				normal[axis] += candidate.plane.normal[j] * (double)axes[j][axis];
		}
		distance = candidate.plane.dist;
		length = 0;
		for (axis = 0; axis < 3; axis++)
		{
			distance += normal[axis] * entity->origin[axis];
			length += normal[axis] * normal[axis];
		}
		length = sqrt (length);
		if (!(length > 0 && length <= DBL_MAX))
			continue;
		distance /= length;
		if (!(distance >= -FLT_MAX && distance <= FLT_MAX))
			continue;
		for (axis = 0; axis < 3; axis++)
			candidate.plane.normal[axis] = (float)(normal[axis] / length);
		candidate.plane.dist = (float)distance;
		hit = candidate;
		found = true;
	}
	if (!found)
		return false;
	length = 0;
	for (axis = 0; axis < 3; axis++)
		length += (double)hit.plane.normal[axis] * hit.plane.normal[axis];
	length = sqrt (length);
	if (!(length > 0 && length <= DBL_MAX))
		return false;
	hit.plane.type = 3;
	hit.plane.signbits = 0;
	hit.plane.pad[0] = hit.plane.pad[1] = 0;
	for (axis = 0; axis < 3; axis++)
	{
		hit.plane.normal[axis] = (float)(hit.plane.normal[axis] / length);
		if (hit.plane.normal[axis] < 0)
			hit.plane.signbits |= 1 << axis;
	}
	distance = hit.plane.dist / length;
	if (!(distance >= -FLT_MAX && distance <= FLT_MAX))
		return false;
	hit.plane.dist = (float)distance;
	*result = hit;
	return true;
}

qboolean R_ShadowPoint (vec3_t p)
{
	shadow_trace_hit_t hit;

	shadow_plane_valid = false;
	if (!R_LightPointTrace (p, &hit, true))
		return false;
	shadow_plane = hit.plane;
	VectorCopy (p, lightspot);
	lightspot[2] = (float)((double)p[2] - 8192.0 * hit.fraction);
	shadow_plane_valid = true;
	return true;
}

/* Neutral Q2PRO bilinear samples. Apply entity color controls only after
 * grid/surface selection, and clamp edge neighbors before forming pointers. */
static qboolean R_SampleLightPoint (const shadow_trace_hit_t *hit, vec3_t color)
{
	const msurface_t *surf = hit->surface;
	const byte *samples, *p00, *p10, *p01, *p11;
	vec3_t sampled = { 0, 0, 0 };
	double s = hit->st[0], t = hit->st[1];
	float fx, fy, w00, w10, w01, w11;
	size_t size;
	int width, height, x, y, x1, y1, map, channel;

	if (!surf || !surf->samples || !r_newrefdef.lightstyles ||
		surf->extents[0] < 0 || surf->extents[1] < 0 ||
		!(s >= -FLT_MAX && s <= FLT_MAX) ||
		!(t >= -FLT_MAX && t <= FLT_MAX))
		return false;
	width = surf->extents[0] / 16 + 1;
	height = surf->extents[1] / 16 + 1;
	if (width > MAX_LIGHTMAP_EXTENTS || height > MAX_LIGHTMAP_EXTENTS)
		return false;
	if (s < 0)
		s = 0;
	if (t < 0)
		t = 0;
	if (s > width - 1)
		s = width - 1;
	if (t > height - 1)
		t = height - 1;
	x = (int)s;
	y = (int)t;
	x1 = x + (x + 1 < width);
	y1 = y + (y + 1 < height);
	fx = (float)(s - x);
	fy = (float)(t - y);
	w00 = (1 - fx) * (1 - fy);
	w10 = fx * (1 - fy);
	w01 = (1 - fx) * fy;
	w11 = fx * fy;
	size = (size_t)width * height * 3;
	for (map = 0, samples = surf->samples;
		 map < MAXLIGHTMAPS && surf->styles[map] != 255;
		 map++, samples += size)
	{
		const lightstyle_t *style = &r_newrefdef.lightstyles[surf->styles[map]];
		p00 = samples + 3 * (y * width + x);
		p10 = samples + 3 * (y * width + x1);
		p01 = samples + 3 * (y1 * width + x);
		p11 = samples + 3 * (y1 * width + x1);
		for (channel = 0; channel < 3; channel++)
			sampled[channel] += style->rgb[channel] *
				(w00 * p00[channel] + w10 * p10[channel] +
					w01 * p01[channel] + w11 * p11[channel]);
	}
	VectorScale (sampled, 1.0f / 255.0f, color);
	return true;
}

/* Q2PRO LIGHTGRID_OCTREE samples light for entities; the BSP surface query
 * supplies only the fallback. Legacy shadows keep their original trace. */
static qboolean GL_LightGridPoint (const rlightgrid_t *grid, vec3_t p, vec3_t color)
{
	uint32 base[3], corner[3];
	vec3_t samples[8], average;
	float frac[3], weight[2][3], factor;
	double coord;
	const rlightgrid_sample_t *sample;
	unsigned mask = 0, count = 0, j;
	int i, axis;

	if (!grid || !grid->numleafs || !gl_lightgrid || !gl_lightgrid->intvalue)
		return false;

	for (axis = 0; axis < 3; axis++)
	{
		coord = ((double)p[axis] - grid->mins[axis]) * grid->scale[axis];
		/* Also rejects NaNs and infinities before the unsigned conversion. */
		if (!(coord >= 0 && coord < (double)grid->size[axis]))
			return false;
		base[axis] = (uint32)coord;
		frac[axis] = (float)(coord - base[axis]);
		weight[0][axis] = 1.0f - frac[axis];
		weight[1][axis] = frac[axis];
	}

	VectorClear (average);
	for (i = 0; i < 8; i++)
	{
		for (axis = 0; axis < 3; axis++)
			corner[axis] = base[axis] + ((i >> axis) & 1);
		if (corner[0] >= grid->size[0] || corner[1] >= grid->size[1] ||
			corner[2] >= grid->size[2])
			continue;

		sample = Mod_LookupLightgrid (grid, corner);
		if (!sample)
			continue;
		VectorClear (samples[i]);
		for (j = 0; j < grid->numstyles && sample[j].style != 255; j++)
		{
			const lightstyle_t *style =
				&r_newrefdef.lightstyles[sample[j].style];
			/* R1Q2's white is the sum of rgb (normally 3), unlike the
			 * Q2PRO scalar. Use the per-channel factors used by surfaces. */
			samples[i][0] += style->rgb[0] * sample[j].rgb[0];
			samples[i][1] += style->rgb[1] * sample[j].rgb[1];
			samples[i][2] += style->rgb[2] * sample[j].rgb[2];
		}
		if (!j)
			continue;
		mask |= 1u << i;
		VectorAdd (average, samples[i], average);
		count++;
	}
	if (!count)
		return false;

	VectorScale (average, 1.0f / count, average);
	for (i = 0; i < 8; i++)
		if (!(mask & (1u << i)))
			VectorCopy (average, samples[i]);

	VectorClear (color);
	for (i = 0; i < 8; i++)
	{
		factor = weight[i & 1][0] * weight[(i >> 1) & 1][1] *
			weight[(i >> 2) & 1][2];
		VectorMA (color, factor, samples[i], color);
	}
	/* R_LightPoint adjusts normalized surface and grid samples together. */
	VectorScale (color, 1.0f / 255.0f, color);
	return true;
}

/*
===============
R_LightPoint
===============
*/
static void R_LightPointMode (vec3_t p, vec3_t color, qboolean modern)
{
	vec3_t		end;
	int lnum, axis, r;
	shadow_trace_hit_t hit;
	dlight_t	*dl;
	//float		light;
	vec3_t		dist;
	float add, modulate = GL_EntityModulate ();
	qboolean grid_sampled, valid_point;

	/* Preview views retain their original lighting and weapon lightlevel. */
	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
		modern = false;
	if (!r_worldmodel || !r_worldmodel->lightdata ||
		!r_newrefdef.lightstyles)
	{
		color[0] = color[1] = color[2] = 1.0f;
		return;
	}

	grid_sampled = GL_LightGridPoint (r_worldmodel->lightgrid, p, color);
	if (!modern)
	{
		/* R_SetLightLevel feeds outgoing usercmds: keep the nearest-texel
		 * legacy surface fallback. Light grids keep precedence in both modes. */
		r = -1;
		if (!grid_sampled || (gl_shadows && FLOAT_NE_ZERO (gl_shadows->value)))
		{
			end[0] = p[0];
			end[1] = p[1];
			end[2] = p[2] - 2048;
			r = RecursiveLightPoint (r_worldmodel->nodes, p, end);
		}
		if (!grid_sampled && r == -1)
			VectorClear (color);
		else if (!grid_sampled)
			FastVectorCopy (pointcolor, *color);
	}
	else
	{
		if (!grid_sampled && (!R_LightPointTrace (p, &hit, false) || !R_SampleLightPoint (&hit, color)))
			VectorClear (color);

		/* Fixed shadows consume the original lightspot/lightplane state;
		 * modern entity sampling never commits either shadow result. */
		valid_point = true;
		for (axis = 0; axis < 3; axis++)
			if (!(p[axis] >= -FLT_MAX && p[axis] <= FLT_MAX))
				valid_point = false;
		if (valid_point && r_worldmodel->nodes && gl_shadows &&
			FLOAT_NE_ZERO (gl_shadows->value) && !(gl_shadows->value >= 2))
		{
			end[0] = p[0];
			end[1] = p[1];
			end[2] = p[2] - 2048;
			RecursiveLightPoint (r_worldmodel->nodes, p, end);
		}
	}

	/* Sample colors are neutral; adjust static entity lighting once. */
	VectorScale (color, 255, color);
	GL_AdjustLighting (color, color, 255 * GL_LightingBrightness (),
		modulate, GL_LightingColorScale ());
	VectorScale (color, (1.0f / 255), color);

	//
	// add dynamic lights
	//
	//light = 0;
	if (FLOAT_NE_ZERO (gl_dynamic->value))
	{
		dl = r_newrefdef.dlights;
		for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++, dl++)
		{
			VectorSubtract (p,
				dl->origin,
				dist);
			add = dl->intensity - DLIGHT_CUTOFF - VectorLength (dist);
			add *= (1.0f / 255);
			if (FLOAT_GT_ZERO(add))
			{
				VectorMA (color, add, dl->color, color);
			}
		}
	}

	if (FLOAT_NE_ZERO(gl_doublelight_entities->value))
		VectorScale (color, modulate, color);
}

void R_LightPoint (vec3_t p, vec3_t color)
{
	R_LightPointMode (p, color, true);
}

void R_LightPointLegacy (vec3_t p, vec3_t color)
{
	R_LightPointMode (p, color, false);
}


//===================================================================

#define BLOCKLIGHT_SIZE 3

#ifdef WIN32
__declspec (align (16)) static float s_blocklights[MAX_LIGHTMAP_EXTENTS * MAX_LIGHTMAP_EXTENTS * BLOCKLIGHT_SIZE];
#else
static float s_blocklights[MAX_LIGHTMAP_EXTENTS * MAX_LIGHTMAP_EXTENTS * BLOCKLIGHT_SIZE];
#endif

#define INTEGER_DLIGHTS		1

/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights (msurface_t *surf)
{
	int lnum, s, t, smax, tmax, axis, component;
	float dist, rad, minlight, falloff, local[2], sd, td, frac;
	double normal2, projection, tangent[2][3], gram00, gram01, gram11;
	double determinant, inverse00 = 0, inverse01 = 0, inverse11 = 0, world2;
	vec3_t impact;
	mtexinfo_t *tex = surf->texinfo;
	const dlight_t *dl;
	int count = inline_dlights_active ? inline_dlight_count : r_newrefdef.num_dlights;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	if (surf->lm_decoupled)
	{
		normal2 = 0;
		for (component = 0; component < 3; component++)
			normal2 += (double)surf->plane->normal[component] *
				surf->plane->normal[component];
		if (normal2 <= 0)
			return;
		for (axis = 0; axis < 2; axis++)
		{
			projection = 0;
			for (component = 0; component < 3; component++)
				projection += (double)surf->lm_axis[axis][component] *
					surf->plane->normal[component];
			projection /= normal2;
			for (component = 0; component < 3; component++)
				tangent[axis][component] = surf->lm_axis[axis][component] -
					projection * surf->plane->normal[component];
		}
		gram00 = gram01 = gram11 = 0;
		for (component = 0; component < 3; component++)
		{
			gram00 += tangent[0][component] * tangent[0][component];
			gram01 += tangent[0][component] * tangent[1][component];
			gram11 += tangent[1][component] * tangent[1][component];
		}
		determinant = gram00 * gram11 - gram01 * gram01;
		if (determinant <= 0)
			return;
		/* Invert the tangent-axis Gram matrix so a lightmap texel delta
		 * has a world-space distance even when BSPX axes are stretched. */
		inverse00 = gram11 / determinant;
		inverse01 = -gram01 / determinant;
		inverse11 = gram00 / determinant;
	}
	for (lnum = 0; lnum < count && lnum < MAX_DLIGHTS; lnum++)
	{
		if (!((unsigned)surf->dlightbits & (1u << lnum)))
			continue;

		dl = inline_dlights_active ? &inline_dlights[lnum] :
									 &r_newrefdef.dlights[lnum];
		dist = DotProduct (dl->origin, surf->plane->normal) - surf->plane->dist;
		rad = dl->intensity - fabsf (dist);
		if (rad < DLIGHT_CUTOFF)
			continue;

		if (FLOAT_NE_ZERO (gl_dlight_falloff->value))
		{
			minlight = rad - DLIGHT_CUTOFF * 0.8f;
			falloff = rad / minlight;
		}
		else
		{
			minlight = rad - DLIGHT_CUTOFF;
			falloff = 1;
		}

		impact[0] = dl->origin[0] - surf->plane->normal[0] * dist;
		impact[1] = dl->origin[1] - surf->plane->normal[1] * dist;
		impact[2] = dl->origin[2] - surf->plane->normal[2] * dist;
		if (surf->lm_decoupled)
		{
			local[0] = DotProduct (impact, surf->lm_axis[0]) + surf->lm_axis[0][3];
			local[1] = DotProduct (impact, surf->lm_axis[1]) + surf->lm_axis[1][3];
		}
		else
		{
			local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3] - surf->texturemins[0];
			local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3] - surf->texturemins[1];
		}

		for (t = 0; t < tmax; t++)
		{
			td = surf->lm_decoupled ? t - local[1] : fabsf (local[1] - t * 16.0f);
			for (s = 0; s < smax; s++)
			{
				if (surf->lm_decoupled)
				{
					sd = s - local[0];
					world2 = inverse00 * sd * sd + 2 * inverse01 * sd * td +
						inverse11 * td * td;
					dist = (float)sqrt (world2 > 0 ? world2 : 0);
				}
				else
				{
					sd = fabsf (local[0] - s * 16.0f);
					dist = sd > td ? sd + td * 0.5f : td + sd * 0.5f;
				}
				if (dist < minlight)
				{
					frac = rad - dist * falloff;
					s_blocklights[(t * smax + s) * 3 + 0] += frac * dl->color[0];
					s_blocklights[(t * smax + s) * 3 + 1] += frac * dl->color[1];
					s_blocklights[(t * smax + s) * 3 + 2] += frac * dl->color[2];
				}
			}
		}
	}
}

/*
** R_SetCacheState
*/
void R_SetCacheState( msurface_t *surf )
{
	int maps;

	surf->lighting_sequence = GL_LightingSequence ();

	for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
		 maps++)
	{
		surf->cached_light[maps] = r_newrefdef.lightstyles[surf->styles[maps]].white;
	}
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the floating format in blocklights
===============
*/
extern qboolean GL_LightmapPacked10 (void);

void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)
{
	int			smax, tmax;
	//int			r, g, b, a, max;
	vec3_t color;
	float add, modulate, colored;
	int			i, j, size;
	byte		*lightmap;
	float		scale[4];
	int			nummaps;
	float		*bl;
	qboolean packed10;

	/* Replacement lightmaps can light liquids and translucent faces, but the
	 * classic unsampled versions must keep their texture-only path. */
	if ((surf->texinfo->flags & SURF_SKY) ||
		((surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)) &&
			(!surf->lm_decoupled || !surf->samples)))
		ri.Sys_Error (ERR_DROP, "R_BuildLightMap called for non-lit surface");

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;

	if (smax < 1 || tmax < 1 || smax > MAX_LIGHTMAP_EXTENTS ||
		tmax > MAX_LIGHTMAP_EXTENTS || stride < smax * 4)
		ri.Sys_Error (ERR_DROP, "R_BuildLightMap: invalid dimensions %d x %d", smax, tmax);
	size = smax*tmax;

	if (size > sizeof (s_blocklights) / sizeof (s_blocklights[0]) / BLOCKLIGHT_SIZE)
		ri.Sys_Error (ERR_DROP, "R_BuildLightMap: Bad s_blocklights size %d", size);

// set to full bright if no light data
	if (!surf->samples)
	{
//		int maps;

		for (i=0 ; i<size*BLOCKLIGHT_SIZE ; i++)
			s_blocklights[i] = 255;
		/*for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			//style = &r_newrefdef.lightstyles[surf->styles[maps]];
		}*/
		goto store;
	}

	// count the # of maps
	for ( nummaps = 0 ; nummaps < MAXLIGHTMAPS && surf->styles[nummaps] != 255 ;
		 nummaps++)
		;

	lightmap = surf->samples;

	// add all the lightmaps
	if ( nummaps == 1 )
	{
		int maps;

		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			bl = s_blocklights;

			scale[0] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[0];
			scale[1] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[1];
			scale[2] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[2];

			if ( scale[0] == 1.0F &&
				 scale[1] == 1.0F &&
				 scale[2] == 1.0F )
			{
				for (i=0 ; i<size; i++, bl+=3)
				{
					bl[0] = lightmap[i*3+0];
					bl[1] = lightmap[i*3+1];
					bl[2] = lightmap[i*3+2];
				}
			}
			else
			{
				for (i=0 ; i<size; i++, bl+=3)
				{
					bl[0] = lightmap[i*3+0] * scale[0];
					bl[1] = lightmap[i*3+1] * scale[1];
					bl[2] = lightmap[i*3+2] * scale[2];
				}
			}
			lightmap += size*3;		// skip to next lightmap
		}
	}
	else
	{
		int maps;

		memset( s_blocklights, 0, sizeof( s_blocklights[0] ) * size * BLOCKLIGHT_SIZE );

		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			bl = s_blocklights;

			scale[0] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[0];
			scale[1] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[1];
			scale[2] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[2];

			if ( scale[0] == 1.0F &&
				 scale[1] == 1.0F &&
				 scale[2] == 1.0F )
			{
				for (i=0 ; i<size ; i++, bl+=3 )
				{
					bl[0] += lightmap[i*3+0];
					bl[1] += lightmap[i*3+1];
					bl[2] += lightmap[i*3+2];
				}
			}
			else
			{
				for (i=0 ; i<size ; i++, bl+=3)
				{
					bl[0] += lightmap[i*3+0] * scale[0];
					bl[1] += lightmap[i*3+1] * scale[1];
					bl[2] += lightmap[i*3+2] * scale[2];
				}
			}
			lightmap += size*3;		// skip to next lightmap
		}
	}

// add all the dynamic lights
	if (surf->dlightframe == r_framecount && FLOAT_NE_ZERO (gl_dynamic->value))
		R_AddDynamicLights (surf);

// put into texture format
store:
	stride -= (smax<<2);
	bl = s_blocklights;
	packed10 = GL_LightmapPacked10 ();

	add = GL_LightingShader () ? 0 : 255 * GL_LightingBrightness ();
	modulate = GL_LightingShader () ? 1 : GL_WorldModulate ();
	colored = GL_LightingColorScale ();

	for (i=0 ; i<tmax ; i++, dest += stride)
	{
		for (j=0 ; j<smax ; j++)
		{
			GL_AdjustLighting (color, bl, add, modulate, colored);
			if (packed10)
			{
				/* GL_RGBA / GL_UNSIGNED_INT_2_10_10_10_REV: red is low. */
				unsigned int packed =
					(unsigned int)(color[0] * (1023.0f / 255.0f) + 0.5f) |
					((unsigned int)(color[1] * (1023.0f / 255.0f) + 0.5f) << 10) |
					((unsigned int)(color[2] * (1023.0f / 255.0f) + 0.5f) << 20) |
					(3u << 30);
				memcpy (dest, &packed, sizeof (packed));
			}
			else
			{
				dest[0] = (byte)color[0];
				dest[1] = (byte)color[1];
				dest[2] = (byte)color[2];
				dest[3] = 255;
			}
			bl += BLOCKLIGHT_SIZE;
			dest += 4;
		}
	}
}
