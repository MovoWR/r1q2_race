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
// gl_mesh.c: triangle model functions

#include "gl_local.h"
#include <float.h>
#include <time.h>

/*
=============================================================

  ALIAS MODELS

=============================================================
*/

#define NUMVERTEXNORMALS	162

#ifdef WIN32
#pragma warning(disable : 4305)
#endif

float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

#ifdef WIN32
#pragma warning(default : 4305)
#endif

static	vec4_t	s_lerped[MAX_VERTS];
//static	vec3_t	lerped[MAX_VERTS];

vec3_t	shadevector;
float	shadelight[3];
extern vec3_t lightspot;

/* MD2 normal indices are validated by the model reader. */

/* Bounded indexed alias submission follows Q2PRO src/refresh/mesh.c.
 * Copyright (C) 2003-2006 Andrey Nazarov; GPL version 2 or later.
 * CPU interpolation and entity/skin/light state remain owned by R1Q2.
 */
static float alias_vertices[ALIAS_MESH_MAX_VERTICES][9]; // xyz st rgba
static unsigned c_alias_batches, c_alias_vertices, c_alias_indices;
static unsigned c_alias_shadow_batches, c_alias_cel_draws, c_alias_immediate;
static qboolean alias_gpu_lerped;
static maliasmesh_t *alias_gpu_mesh;
static dtrivertx_t *alias_gpu_verts, *alias_gpu_oldverts;
static vec3_t alias_gpu_move, alias_gpu_front, alias_gpu_back;

/* A translucent gun needs front depth before its blended color pass. */
static qboolean GL_WeaponDepthPrefill (float alpha)
{
	return (currententity->flags & RF_WEAPONMODEL) &&
		!(currententity->flags & RF_FULLBRIGHT) && alpha > 0 && alpha < 1;
}

static void GL_BeginWeaponDepthPrefill (void)
{
	qglPushAttrib (GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	qglEnable (GL_DEPTH_TEST);
	qglDepthMask (GL_TRUE);
	qglDisable (GL_BLEND);
	qglDisable (GL_ALPHA_TEST);
	qglDisable (GL_TEXTURE_2D);
	qglColorMask (GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
}

static void GL_EndWeaponDepthPrefill (void)
{
	qglPopAttrib ();
}

static void GL_DrawAliasPrefillCommands (dmdl_t *header)
{
	int *order = (int *)((byte *)header + header->ofs_glcmds);
	int count, index;

	GL_BeginWeaponDepthPrefill ();
	for (;;)
	{
		count = *order++;
		if (!count)
			break;
		if (count < 0)
		{
			count = -count;
			qglBegin (GL_TRIANGLE_FAN);
		}
		else
			qglBegin (GL_TRIANGLE_STRIP);
		do
		{
			index = order[2];
			order += 3;
			qglVertex3fv (s_lerped[index]);
		} while (--count);
		qglEnd ();
		c_alias_immediate++;
	}
	GL_EndWeaponDepthPrefill ();
}

/* Q2PRO height fade; MD5 replacements use the base model's frame radii. */
static float R_ShadowAlpha (entity_t *e, float backlerp)
{
	const cplane_t *plane;
	float current_radius, old_radius;
	double radius, distance;
	int frame, oldframe;

	if (!gl_shadows || !(gl_shadows->value >= 2))
		return 0.5f;
	if (!currentmodel || !currentmodel->shadow_radii || currentmodel->numframes < 1)
		return 0;
	frame = e->frame;
	oldframe = e->oldframe;
	if (frame < 0 || frame >= currentmodel->numframes)
		frame = 0;
	if (oldframe < 0 || oldframe >= currentmodel->numframes)
		oldframe = 0;
	if (!(backlerp >= 0))
		backlerp = 0;
	if (backlerp > 1)
		backlerp = 1;
	current_radius = currentmodel->shadow_radii[frame];
	old_radius = currentmodel->shadow_radii[oldframe];
	if ((backlerp < 1 && !(current_radius >= 0 && current_radius <= FLT_MAX)) ||
		(backlerp > 0 && !(old_radius >= 0 && old_radius <= FLT_MAX)))
		return 0;
	/* Do not evaluate an invalid radius belonging to an unused frame. */
	radius = backlerp < 1 ? current_radius * (1.0 - backlerp) : 0;
	if (backlerp > 0)
		radius += old_radius * (double)backlerp;
	if (!(radius > 0) || !R_ShadowPoint (e->origin))
		return 0;
	plane = R_GetShadowPlane ();
	if (!plane || !(plane->normal[2] >= 0.5f))
		return 0;
	distance = (double)e->origin[2] - lightspot[2] - radius;
	if (!(distance <= radius * 4))
		return 0;
	return distance > 0 ? (float)(0.5 - distance / (radius * 8)) : 0.5f;
}

/* Project world-space geometry vertically onto the current ground plane. */
static void GL_BeginShadow (entity_t *e)
{
	qglPushMatrix ();
	if (gl_shadows->value >= 2)
	{
		const cplane_t *plane = R_GetShadowPlane ();
		float projection[16] = { 1, 0, 0, 0, 0, 1, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 1 };
		entity_t rotated = *e;
		float offset = gldepthmin > gldepthmax ? 1.0f : -1.0f;

		qglPushAttrib (GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
			GL_POLYGON_BIT | GL_STENCIL_BUFFER_BIT | GL_CURRENT_BIT);
		qglDepthMask (GL_FALSE);
		qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		if (r_shadow_stencilbits > 0)
		{
			qglEnable (GL_STENCIL_TEST);
			qglStencilMask (~0u);
			qglStencilFunc (GL_EQUAL, 0, ~0u);
			qglStencilOp (GL_KEEP, GL_KEEP, GL_INCR);
		}
		else
			qglDisable (GL_STENCIL_TEST);
		qglEnable (GL_POLYGON_OFFSET_FILL);
		qglPolygonOffset (offset, offset * 2);
		projection[2] = -plane->normal[0] / plane->normal[2];
		projection[6] = -plane->normal[1] / plane->normal[2];
		projection[14] = plane->dist / plane->normal[2];
		qglMultMatrixf (projection);
		/* Match the filled model without changing the caller's entity. */
		rotated.angles[PITCH] = -rotated.angles[PITCH];
		R_RotateForEntity (&rotated);
	}
	else
		R_RotateForEntity (e);
}

static void GL_EndShadow (void)
{
	qglPopMatrix ();
	if (gl_shadows->value >= 2)
		qglPopAttrib ();
}

static float GL_AliasNormalShade (unsigned index)
{
	float dot = DotProduct (r_avertexnormals[index], shadevector);
	if (dot < 0)
		dot *= 0.3f;
	return dot + 1.0f;
}

static float GL_AliasDotShade (unsigned oldindex, unsigned newindex,
	float backlerp)
{
	if (gl_dotshading && FLOAT_EQ_ZERO (gl_dotshading->value))
		return 1.0f;
	if (backlerp == 0 || oldindex == newindex)
		return GL_AliasNormalShade (newindex);
	return GL_AliasNormalShade (oldindex) * backlerp +
		GL_AliasNormalShade (newindex) * (1.0f - backlerp);
}

static image_t *GL_AliasSkin (void)
{
	image_t *skin = currententity->skin;
	if (!skin)
	{
		if (currententity->skinnum >= MAX_MD2SKINS || currententity->skinnum < 0)
			skin = currentmodel->skins[0];
		else
		{
			skin = currentmodel->skins[currententity->skinnum];
			if (!skin)
				skin = currentmodel->skins[0];
		}
	}
	return skin ? skin : r_notexture;
}

/* Q2PRO's optional backface-line outline fades out by 700 world units. */
static float GL_AliasCelScale (float alpha)
{
	vec3_t delta;
	float width = gl_celshading ? gl_celshading->value : 0;
	float scale;
	if (!(width > 0) || width > FLT_MAX || !(alpha >= 1.0f) ||
		(currententity->flags & (RF_TRANSLUCENT | RF_SHELL_MASK)) ||
		!qglPolygonMode || !qglLineWidth || !qglPushAttrib || !qglPopAttrib ||
		!qglCullFace || !qglBlendFunc)
		return 0;
	if (gl_alphaskins && FLOAT_NE_ZERO (gl_alphaskins->value) &&
		GL_AliasSkin ()->has_alpha)
		return 0;
	VectorSubtract (currententity->origin, r_newrefdef.vieworg, delta);
	scale = 1.0f - VectorLength (delta) / 700.0f;
	return scale >= 0.01f ? scale : 0;
}

void GL_ClearAliasGPUFrame (maliasmesh_t *mesh)
{
	if (!mesh || alias_gpu_mesh == mesh)
	{
		alias_gpu_lerped = false;
		alias_gpu_mesh = NULL;
	}
}

void GL_ResetAliasBatches (void)
{
	c_alias_batches = c_alias_vertices = c_alias_indices = 0;
	c_alias_shadow_batches = c_alias_cel_draws = c_alias_immediate = 0;
	alias_gpu_lerped = false;
	GL_ResetShaderStats ();
}

void GL_PrintAliasBatchInfo (void)
{
	ri.Con_Printf (PRINT_ALL,
		"%u alias draws (%u shadows, %u cel), %u vertices, %u indices, %u strip/fan draws, %u cache bytes\n",
		c_alias_batches, c_alias_shadow_batches, c_alias_cel_draws, c_alias_vertices,
		c_alias_indices, c_alias_immediate, c_alias_mesh_bytes);
	GL_PrintShaderInfo ();
}

static qboolean GL_DrawAliasIndexed (dtrivertx_t *verts,
	dtrivertx_t *oldverts, float backlerp, float alpha,
	qboolean shadow, float lheight)
{
	maliasmesh_t *mesh = currentmodel->aliasmesh;
	GLint saved_array = 0, saved_elements = 0;
	qboolean buffers = qglBindBuffer != NULL;
	qboolean shell = currententity->flags & RF_SHELL_MASK;
	unsigned i;
	float *out, l;
	int index;

	if (!mesh || !gl_batch_models || FLOAT_EQ_ZERO (gl_batch_models->value) ||
		!gl_vertex_arrays || FLOAT_EQ_ZERO (gl_vertex_arrays->value) ||
		!qglDrawElements || !qglVertexPointer || !qglTexCoordPointer ||
		!qglColorPointer || !qglEnableClientState || !qglDisableClientState ||
		!qglPushClientAttrib || !qglPopClientAttrib || !qglPushAttrib ||
		!qglPopAttrib || !qglGetIntegerv)
		return false;

	for (i = 0; i < mesh->numverts; i++)
	{
		index = mesh->vertices[i].index_xyz;
		out = alias_vertices[i];
		memcpy (out, s_lerped[index], sizeof (vec3_t));
		if (shadow && !(gl_shadows->value >= 2))
		{
			out[0] -= shadevector[0] * (out[2] + lheight);
			out[1] -= shadevector[1] * (out[2] + lheight);
			out[2] = -lheight + 1.0f;
		}
		else if (!shadow && !shell)
		{
			out[3] = mesh->vertices[i].st[0];
			out[4] = mesh->vertices[i].st[1];
			l = GL_AliasDotShade (oldverts[index].lightnormalindex,
				verts[index].lightnormalindex, backlerp);
			out[5] = l * shadelight[0];
			out[6] = l * shadelight[1];
			out[7] = l * shadelight[2];
			out[8] = alpha;
		}
	}

	qglPushAttrib (GL_CURRENT_BIT);
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
	qglVertexPointer (3, GL_FLOAT, sizeof (alias_vertices[0]), alias_vertices[0]);
	if (shell || shadow)
	{
		qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
		qglDisableClientState (GL_COLOR_ARRAY);
		if (!shadow)
			qglColor4f (shadelight[0], shadelight[1], shadelight[2], alpha);
	}
	else
	{
		qglEnableClientState (GL_TEXTURE_COORD_ARRAY);
		qglTexCoordPointer (2, GL_FLOAT, sizeof (alias_vertices[0]), &alias_vertices[0][3]);
		qglEnableClientState (GL_COLOR_ARRAY);
		qglColorPointer (4, GL_FLOAT, sizeof (alias_vertices[0]), &alias_vertices[0][5]);
	}
	if (!shadow && GL_WeaponDepthPrefill (alpha))
	{
		GL_BeginWeaponDepthPrefill ();
		qglDrawElements (GL_TRIANGLES, mesh->numindices, GL_UNSIGNED_INT, mesh->indices);
		GL_EndWeaponDepthPrefill ();
		c_alias_batches++;
		c_alias_vertices += mesh->numverts;
		c_alias_indices += mesh->numindices;
	}
	qglDrawElements (GL_TRIANGLES, mesh->numindices, GL_UNSIGNED_INT, mesh->indices);
	qglPopClientAttrib ();
	if (buffers)
	{
		qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
		qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, (GLuint)saved_elements);
	}
	qglPopAttrib ();
	GL_CheckForError ();

	c_alias_batches++;
	c_alias_vertices += mesh->numverts;
	c_alias_indices += mesh->numindices;
	if (shadow)
		c_alias_shadow_batches++;
	return true;
}

static void GL_DrawAliasCelCPU (dmdl_t *header, float celscale)
{
	maliasmesh_t *mesh = currentmodel->aliasmesh;
	unsigned saved_tmu, i;
	GLint saved_array = 0, saved_elements = 0;
	qboolean indexed, bloom_shader;
	float width;
	int *order, count;

	if (celscale <= 0)
		return;
	width = gl_celshading->value;
	if (width > 10)
		width = 10;
	indexed = mesh && gl_batch_models && FLOAT_NE_ZERO (gl_batch_models->value) &&
		gl_vertex_arrays && FLOAT_NE_ZERO (gl_vertex_arrays->value) &&
		qglDrawElements && qglVertexPointer && qglEnableClientState &&
		qglDisableClientState && qglPushClientAttrib && qglPopClientAttrib &&
		(!qglBindBuffer || qglGetIntegerv);

	saved_tmu = gl_state.currenttmu;
	GL_SelectTexture (GL_TEXTURE0);
	qglPushAttrib (GL_CURRENT_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
		GL_POLYGON_BIT | GL_LINE_BIT);
	qglDisable (GL_TEXTURE_2D);
	qglEnable (GL_BLEND);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglEnable (GL_CULL_FACE);
	qglCullFace ((currententity->flags & RF_WEAPONMODEL) &&
				r_lefthand && r_lefthand->value == 1.0f ?
			GL_FRONT :
			GL_BACK);
	qglPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	qglLineWidth (width * celscale);
	qglColor4f (0, 0, 0, celscale);
	bloom_shader = GL_BeginBloomShader (false, false, false);

	if (indexed)
	{
		for (i = 0; i < mesh->numverts; i++)
			memcpy (alias_vertices[i], s_lerped[mesh->vertices[i].index_xyz],
				sizeof (vec3_t));
		qglPushClientAttrib (GL_CLIENT_VERTEX_ARRAY_BIT);
		if (qglBindBuffer)
		{
			qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_array);
			qglGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &saved_elements);
			qglBindBuffer (GL_ARRAY_BUFFER, 0);
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		qglDisableClientState (GL_COLOR_ARRAY);
		qglDisableClientState (GL_NORMAL_ARRAY);
		qglDisableClientState (GL_INDEX_ARRAY);
		qglDisableClientState (GL_EDGE_FLAG_ARRAY);
		if (qglClientActiveTextureARB)
		{
			qglClientActiveTextureARB (GL_TEXTURE1);
			qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
			qglClientActiveTextureARB (GL_TEXTURE0);
		}
		qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
		qglEnableClientState (GL_VERTEX_ARRAY);
		qglVertexPointer (3, GL_FLOAT, sizeof (alias_vertices[0]), alias_vertices[0]);
		qglDrawElements (GL_TRIANGLES, mesh->numindices, GL_UNSIGNED_INT, mesh->indices);
		qglPopClientAttrib ();
		if (qglBindBuffer)
		{
			qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, (GLuint)saved_elements);
		}
		c_alias_batches++;
		c_alias_vertices += mesh->numverts;
		c_alias_indices += mesh->numindices;
		c_alias_cel_draws++;
	}
	else
	{
		order = (int *)((byte *)header + header->ofs_glcmds);
		for (;;)
		{
			count = *order++;
			if (!count)
				break;
			if (count < 0)
			{
				count = -count;
				qglBegin (GL_TRIANGLE_FAN);
			}
			else
				qglBegin (GL_TRIANGLE_STRIP);
			c_alias_immediate++;
			c_alias_cel_draws++;
			do
			{
				qglVertex3fv (s_lerped[order[2]]);
				order += 3;
			} while (--count);
			qglEnd ();
		}
	}
	if (bloom_shader)
		GL_EndSurfaceShader ();
	qglPopAttrib ();
	GL_SelectTexture (saved_tmu ? GL_TEXTURE1 : GL_TEXTURE0);
	GL_CheckForError ();
}


void GL_LerpVerts( int nverts, dtrivertx_t *v, dtrivertx_t *ov, dtrivertx_t *verts, float *lerp, float move[3], float frontv[3], float backv[3] )
{
	int i;

	//PMM -- added RF_SHELL_DOUBLE, RF_SHELL_HALF_DAM
	if (currententity->flags & RF_SHELL_MASK)
	{
		for (i=0 ; i < nverts; i++, v++, ov++, lerp+=4 )
		{
			float *normal = r_avertexnormals[verts[i].lightnormalindex];

			lerp[0] = move[0] + ov->v[0]*backv[0] + v->v[0]*frontv[0] + normal[0] * POWERSUIT_SCALE;
			lerp[1] = move[1] + ov->v[1]*backv[1] + v->v[1]*frontv[1] + normal[1] * POWERSUIT_SCALE;
			lerp[2] = move[2] + ov->v[2]*backv[2] + v->v[2]*frontv[2] + normal[2] * POWERSUIT_SCALE; 
		}
	}
	else
	{
		for (i=0 ; i < nverts; i++, v++, ov++, lerp+=4)
		{
			lerp[0] = move[0] + ov->v[0]*backv[0] + v->v[0]*frontv[0];
			lerp[1] = move[1] + ov->v[1]*backv[1] + v->v[1]*frontv[1];
			lerp[2] = move[2] + ov->v[2]*backv[2] + v->v[2]*frontv[2];
		}
	}

}

/*
=============
GL_DrawAliasFrameLerp

interpolates between two frames and origins
FIXME: batch lerp all vertexes
=============
*/
/* Q2PRO weapon projection, applied only to this draw's private matrix. */
static float R_GunFov (void)
{
	float fov = cl_gunfov ? cl_gunfov->value : 0;
	float tangent;
	if (!(fov > 0) || r_newrefdef.width < 1 || r_newrefdef.height < 1)
		return r_newrefdef.fov_y;
	if (fov < 30)
		fov = 30;
	if (fov > 160)
		fov = 160;
	tangent = (float)tan (fov * M_PI / 360);
	if (r_adjustgunfov && FLOAT_NE_ZERO (r_adjustgunfov->value))
		tangent *= 0.75f;
	else
		tangent *= (float)r_newrefdef.height / r_newrefdef.width;
	return (float)(atan (tangent) * 360 / M_PI);
}

static float R_GunAlpha (void)
{
	float alpha = cl_gunalpha ? cl_gunalpha->value : 1;
	if (!(alpha >= 0))
		return 1;
	if (alpha > 1)
		return 1;
	return alpha;
}

/* Replay the validated command topology using the already interpolated pose. */
static void GL_ShowAliasTris (const dmdl_t *header)
{
	const int *order;
	int count;

	if (!GL_ShowTrisBegin (GL_SHOWTRIS_MESH))
		return;
	order = (const int *)((const byte *)header + header->ofs_glcmds);
	for (;;)
	{
		count = *order++;
		if (!count)
			break;
		if (count < 0)
		{
			count = -count;
			qglBegin (GL_TRIANGLE_FAN);
		}
		else
			qglBegin (GL_TRIANGLE_STRIP);
		do
		{
			qglVertex3fv (s_lerped[order[2]]);
			order += 3;
		} while (--count);
		qglEnd ();
	}
	GL_ShowTrisEnd ();
}

void GL_DrawAliasFrameLerp (dmdl_t *paliashdr, float backlerp, GLuint glowtexnum)
{
	float 	l;
	daliasframe_t	*frame, *oldframe;
	dtrivertx_t	*v, *ov, *verts;
	int		*order;
	int		count;
	float	frontlerp;
	float	alpha;
	float celscale;
	vec3_t	move, delta, vectors[3];
	vec3_t	frontv, backv;
	int		i;
	int		index_xyz;
	float	*lerp;

	qboolean material_shader, shell;
	alias_gpu_lerped = false;
	shell = currententity->flags & RF_SHELL_MASK;
	if (shell)
		glowtexnum = 0;
	frame = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->frame * paliashdr->framesize);
	verts = v = frame->verts;

	oldframe = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->oldframe * paliashdr->framesize);
	ov = oldframe->verts;

	order = (int *)((byte *)paliashdr + paliashdr->ofs_glcmds);

//	glTranslatef (frame->translate[0], frame->translate[1], frame->translate[2]);
//	glScalef (frame->scale[0], frame->scale[1], frame->scale[2]);

	if (currententity->flags & RF_TRANSLUCENT)
		alpha = currententity->alpha;
	else
		alpha = 1.0f;
	if (currententity->flags & RF_WEAPONMODEL)
		alpha *= R_GunAlpha ();
	celscale = GL_AliasCelScale (alpha);

	// PMM - added double shell
	if (currententity->flags & RF_SHELL_MASK)
		qglDisable( GL_TEXTURE_2D );

	frontlerp = 1.0f - backlerp;

	// move should be the delta back to the previous frame * backlerp
	VectorSubtract (currententity->oldorigin, currententity->origin, delta);
	AngleVectors (currententity->angles, vectors[0], vectors[1], vectors[2]);

	move[0] = DotProduct (delta, vectors[0]);	// forward
	move[1] = -DotProduct (delta, vectors[1]);	// left
	move[2] = DotProduct (delta, vectors[2]);	// up

	VectorAdd (move, oldframe->translate, move);

	move[0] = backlerp*move[0] + frontlerp*frame->translate[0];
	move[1] = backlerp*move[1] + frontlerp*frame->translate[1];
	move[2] = backlerp*move[2] + frontlerp*frame->translate[2];

	frontv[0] = frontlerp*frame->scale[0];
	backv[0] = backlerp*oldframe->scale[0];
	frontv[1] = frontlerp*frame->scale[1];
	backv[1] = backlerp*oldframe->scale[1];
	frontv[2] = frontlerp*frame->scale[2];
	backv[2] = backlerp*oldframe->scale[2];

	if (GL_DrawAliasGPU (paliashdr, verts, ov, move, frontv, backv,
			alpha, celscale, false, 0, glowtexnum))
	{
		alias_gpu_lerped = true;
		alias_gpu_mesh = currentmodel->aliasmesh;
		alias_gpu_verts = verts;
		alias_gpu_oldverts = ov;
		memcpy (alias_gpu_move, move, sizeof (vec3_t));
		memcpy (alias_gpu_front, frontv, sizeof (vec3_t));
		memcpy (alias_gpu_back, backv, sizeof (vec3_t));
		if (currententity->flags & RF_SHELL_MASK)
			qglEnable (GL_TEXTURE_2D);
		return;
	}

	lerp = s_lerped[0];

	GL_LerpVerts( paliashdr->num_xyz, v, ov, verts, lerp, move, frontv, backv );

	material_shader = false;
	if (!shell && GL_ImageShaderIntensity ())
		material_shader = GL_BeginLitMaterialShader (true, GL_MODULATE, 0, 0, 1, glowtexnum);
	else if (GL_BloomActive () && !glowtexnum)
		material_shader = GL_BeginBloomShader (!shell, true, shell);
	else if (glowtexnum && GL_GlowmapsAvailable ())
		material_shader = GL_BeginMaterialShader (true, true, GL_MODULATE, 0, 0, 1, glowtexnum);
	else if (GL_BloomActive ())
		material_shader = GL_BeginBloomShader (true, true, false);
	if (GL_DrawAliasIndexed (verts, ov, backlerp, alpha, false, 0))
	{
		if (material_shader)
			GL_EndSurfaceShader ();
		GL_DrawAliasCelCPU (paliashdr, celscale);
		GL_ShowAliasTris (paliashdr);
		if (currententity->flags & RF_SHELL_MASK)
			qglEnable (GL_TEXTURE_2D);
		return;
	}
	if (GL_WeaponDepthPrefill (alpha))
		GL_DrawAliasPrefillCommands (paliashdr);

	//TODO: use this somehow
	/*qglEnableClientState (GL_VERTEX_ARRAY);
	qglEnableClientState (GL_NORMAL_ARRAY);
	qglEnableClientState (GL_TEXTURE_COORD_ARRAY);

	qglVertexPointer (3, GL_FLOAT, 0, inVertexArray);
	qglNormalPointer (GL_FLOAT, 12, inNormalsArray);
	qglTexCoordPointer (2, GL_FLOAT, 0, inCoordArray);

	qglDrawElements (GL_TRIANGLES, inNumIndexes, GL_UNSIGNED_INT, inIndexArray);*/

	if (FLOAT_NE_ZERO(gl_vertex_arrays->value))
	{
		float colorArray[MAX_VERTS*4];

		qglEnableClientState( GL_VERTEX_ARRAY );
		GL_CheckForError ();

		qglVertexPointer( 3, GL_FLOAT, 16, s_lerped );	// padded for SIMD
		GL_CheckForError ();

//		if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE ) )
		// PMM - added double damage shell
		if (currententity->flags & RF_SHELL_MASK)
		{
			qglColor4f( shadelight[0], shadelight[1], shadelight[2], alpha );
			GL_CheckForError ();
		}
		else
		{
			qglEnableClientState( GL_COLOR_ARRAY );
			GL_CheckForError ();

			qglColorPointer( 4, GL_FLOAT, 0, colorArray );
			GL_CheckForError ();

			//
			// pre light everything
			//
			for ( i = 0; i < paliashdr->num_xyz; i++ )
			{
				float l = GL_AliasDotShade (ov[i].lightnormalindex,
					verts[i].lightnormalindex, backlerp);

				colorArray[i*4+0] = l * shadelight[0];
				colorArray[i*4+1] = l * shadelight[1];
				colorArray[i*4+2] = l * shadelight[2];
				colorArray[i*4+3] = alpha;
				//qglColor4f (l* shadelight[0], l*shadelight[1], l*shadelight[2], alpha);
			}
		}

		if ( qglLockArraysEXT != 0 )
		{
			qglLockArraysEXT( 0, paliashdr->num_xyz );
			GL_CheckForError ();
		}

		for (;;)
		{
			// get the vertex count and primitive type
			count = *order++;
			if (!count)
				break;		// done
			c_alias_immediate++;
			if (count < 0)
			{
				count = -count;
				qglBegin (GL_TRIANGLE_FAN);
			}
			else
			{
				qglBegin (GL_TRIANGLE_STRIP);
			}

			// PMM - added double damage shell
			if (currententity->flags & RF_SHELL_MASK)
			{
				do
				{
					index_xyz = order[2];
					order += 3;

					qglVertex3fv( s_lerped[index_xyz] );

				} while (--count);
			}
			else
			{
				do
				{
					// texture coordinates come from the draw list
					qglTexCoord2f (((float *)order)[0], ((float *)order)[1]);
					index_xyz = order[2];
					order += 3;
					qglArrayElement( index_xyz );

				} while (--count);
			}
			qglEnd ();
			GL_CheckForError ();
		}

		if ( qglUnlockArraysEXT != 0 )
		{
			qglUnlockArraysEXT();
			GL_CheckForError ();
		}
	}
	else
	{
		for (;;)
		{
			// get the vertex count and primitive type
			count = *order++;
			if (!count)
				break;		// done
			c_alias_immediate++;
			if (count < 0)
			{
				count = -count;
				qglBegin (GL_TRIANGLE_FAN);
			}
			else
			{
				qglBegin (GL_TRIANGLE_STRIP);
			}

			if (currententity->flags & RF_SHELL_MASK)
			{
				do
				{
					index_xyz = order[2];
					order += 3;

					qglColor4f( shadelight[0], shadelight[1], shadelight[2], alpha);
					qglVertex3fv (s_lerped[index_xyz]);

				} while (--count);
			}
			else
			{
				do
				{
					// texture coordinates come from the draw list
					qglTexCoord2f (((float *)order)[0], ((float *)order)[1]);
					index_xyz = order[2];
					order += 3;

					// normals and vertexes come from the frame list
					l = GL_AliasDotShade (ov[index_xyz].lightnormalindex,
						verts[index_xyz].lightnormalindex, backlerp);

					qglColor4f (l* shadelight[0], l*shadelight[1], l*shadelight[2], alpha);
					qglVertex3fv (s_lerped[index_xyz]);
				} while (--count);
			}

			qglEnd ();
			GL_CheckForError ();
		}
	}

	if (material_shader)
		GL_EndSurfaceShader ();
	GL_DrawAliasCelCPU (paliashdr, celscale);
	GL_ShowAliasTris (paliashdr);

	//	if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE ) )
	// PMM - added double damage shell
	if (currententity->flags & RF_SHELL_MASK)
	{
		qglEnable( GL_TEXTURE_2D );
		GL_CheckForError ();
	}
}


#if 1
/*
=============
GL_DrawAliasShadow
=============
*/

void GL_DrawAliasShadow (dmdl_t *paliashdr)
{
	qboolean bloom_shader;
	//dtrivertx_t	*verts;
	int		*order;
	vec3_t	point;
	float	height, lheight;
	int		count;
	//daliasframe_t	*frame;

	lheight = currententity->origin[2] - lightspot[2];

	//frame = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
	//	+ currententity->frame * paliashdr->framesize);
	//verts = frame->verts;

	//height = 0;

	order = (int *)((byte *)paliashdr + paliashdr->ofs_glcmds);

	height = -lheight + 1.0f;
	if (alias_gpu_lerped && alias_gpu_mesh == currentmodel->aliasmesh)
	{
		if (GL_DrawAliasGPU (paliashdr, alias_gpu_verts, alias_gpu_oldverts,
				alias_gpu_move, alias_gpu_front, alias_gpu_back, 0, 0, true, lheight, 0))
			return;
		GL_LerpVerts (paliashdr->num_xyz, alias_gpu_verts, alias_gpu_oldverts,
			alias_gpu_verts, s_lerped[0], alias_gpu_move, alias_gpu_front, alias_gpu_back);
	}
	bloom_shader = GL_BeginBloomShader (false, false, false);
	if (GL_DrawAliasIndexed (NULL, NULL, 0, 0, true, lheight))
	{
		if (bloom_shader)
			GL_EndSurfaceShader ();
		return;
	}

	for (;;)
	{
		// get the vertex count and primitive type
		count = *order++;
		if (!count)
			break;		// done
		c_alias_immediate++;
		if (count < 0)
		{
			count = -count;
			qglBegin (GL_TRIANGLE_FAN);
		}
		else
			qglBegin (GL_TRIANGLE_STRIP);

		do
		{
			// normals and vertexes come from the frame list
/*
			point[0] = verts[order[2]].v[0] * frame->scale[0] + frame->translate[0];
			point[1] = verts[order[2]].v[1] * frame->scale[1] + frame->translate[1];
			point[2] = verts[order[2]].v[2] * frame->scale[2] + frame->translate[2];
*/

			memcpy( point, s_lerped[order[2]], sizeof( point )  );

			if (!(gl_shadows->value >= 2))
			{
				point[0] -= shadevector[0] * (point[2] + lheight);
				point[1] -= shadevector[1] * (point[2] + lheight);
				point[2] = height;
			}
//			height -= 0.001;
			qglVertex3fv (point);

			order += 3;

//			verts++;

		} while (--count);

		qglEnd ();
	}
	if (bloom_shader)
		GL_EndSurfaceShader ();
}

#endif

/*
** R_CullAliasModel
*/
static qboolean R_CullAliasModel( vec3_t bbox[8], entity_t *e )
{
	int i;
	vec3_t		mins, maxs;
	dmdl_t		*paliashdr;
	vec3_t		vectors[3];
	vec3_t		thismins, oldmins, thismaxs, oldmaxs;
	daliasframe_t *pframe, *poldframe;
	vec3_t angles;

	paliashdr = (dmdl_t *)currentmodel->extradata;

	pframe = ( daliasframe_t * ) ( ( byte * ) paliashdr + 
		                              paliashdr->ofs_frames +
									  e->frame * paliashdr->framesize);

	poldframe = ( daliasframe_t * ) ( ( byte * ) paliashdr + 
		                              paliashdr->ofs_frames +
									  e->oldframe * paliashdr->framesize);

	/*
	** compute axially aligned mins and maxs
	*/
	if ( pframe == poldframe )
	{
		for ( i = 0; i < 3; i++ )
		{
			mins[i] = pframe->translate[i];
			maxs[i] = mins[i] + pframe->scale[i]*255;
		}
	}
	else
	{
		for ( i = 0; i < 3; i++ )
		{
			thismins[i] = pframe->translate[i];
			thismaxs[i] = thismins[i] + pframe->scale[i]*255;

			oldmins[i]  = poldframe->translate[i];
			oldmaxs[i]  = oldmins[i] + poldframe->scale[i]*255;

			if ( thismins[i] < oldmins[i] )
				mins[i] = thismins[i];
			else
				mins[i] = oldmins[i];

			if ( thismaxs[i] > oldmaxs[i] )
				maxs[i] = thismaxs[i];
			else
				maxs[i] = oldmaxs[i];
		}
	}

	/*
	** compute a full bounding box
	*/
	for ( i = 0; i < 8; i++ )
	{
		vec3_t   tmp;

		if ( i & 1 )
			tmp[0] = mins[0];
		else
			tmp[0] = maxs[0];

		if ( i & 2 )
			tmp[1] = mins[1];
		else
			tmp[1] = maxs[1];

		if ( i & 4 )
			tmp[2] = mins[2];
		else
			tmp[2] = maxs[2];

		FastVectorCopy( tmp, *bbox[i] );
	}

	/*
	** rotate the bounding box
	*/
	FastVectorCopy( e->angles, angles );
	angles[YAW] = -angles[YAW];
	AngleVectors( angles, vectors[0], vectors[1], vectors[2] );

	for ( i = 0; i < 8; i++ )
	{
		vec3_t tmp;

		FastVectorCopy( *bbox[i], tmp );

		bbox[i][0] = DotProduct( vectors[0], tmp );
		bbox[i][1] = -DotProduct( vectors[1], tmp );
		bbox[i][2] = DotProduct( vectors[2], tmp );

		VectorAdd( e->origin, bbox[i], bbox[i] );
	}

	{
		int p, f, aggregatemask = ~0;

		for ( p = 0; p < 8; p++ )
		{
			int mask = 0;

			for ( f = 0; f < 4; f++ )
			{
				float dp;
				
				dp = DotProduct( frustum[f].normal, bbox[p] ) - frustum[f].dist;

				if (FLOAT_LT_ZERO (dp))
				{
					mask |= ( 1 << f );
				}
			}

			aggregatemask &= mask;
		}

		if ( aggregatemask )
		{
			return true;
		}

		return false;
	}
}

/*
=================
R_DrawAliasModel

=================
*/
extern void MYgluPerspective( GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar );
void R_DrawAliasModel (entity_t *e)
{
	int			i;
	dmdl_t		*paliashdr;
	float an, shadow_alpha;
	vec3_t		bbox[8];
	image_t		*skin;

	paliashdr = (dmdl_t *)currentmodel->extradata;

	//r1: always test, even for weapon models
	if ( ( e->frame >= paliashdr->num_frames ) || ( e->frame < 0 ) )
	{
		ri.Con_Printf (PRINT_DEVELOPER, "R_DrawAliasModel %s: no such frame %d\n", 
			currentmodel->name, e->frame);
		e->frame = 0;
	}

	if ( ( e->oldframe >= paliashdr->num_frames ) || ( e->oldframe < 0 ) )
	{
		ri.Con_Printf (PRINT_DEVELOPER, "R_DrawAliasModel %s: no such oldframe %d\n", 
			currentmodel->name, e->oldframe);
		e->oldframe = 0;
	}

	if ( !( e->flags & RF_WEAPONMODEL ) )
	{
		if (FLOAT_NE_ZERO (gl_cull_models->value) && FLOAT_EQ_ZERO (r_nocull->value) &&
			R_CullAliasModel (bbox, e))
		{
			R_DrawCulledModelShadow (e);
			return;
		}
	}

	if ( e->flags & RF_WEAPONMODEL )
	{
		if ( r_lefthand->value == 2 )
			return;
	}

	//
	// get lighting information
	//
	// PMM - rewrote, reordered to handle new shells & mixing
	// PMM - 3.20 code .. replaced with original way of doing it to keep mod authors happy
	//
	if (currententity->flags & RF_SHELL_MASK)
	{
		VectorClear (shadelight);
		if (currententity->flags & RF_SHELL_HALF_DAM)
		{
				shadelight[0] = 0.56f;
				shadelight[1] = 0.59f;
				shadelight[2] = 0.45f;
		}
		if ( currententity->flags & RF_SHELL_DOUBLE )
		{
			shadelight[0] = 0.9f;
			shadelight[1] = 0.7f;
		}
		if ( currententity->flags & RF_SHELL_RED )
			shadelight[0] = 1.0f;
		if ( currententity->flags & RF_SHELL_GREEN )
			shadelight[1] = 1.0f;
		if ( currententity->flags & RF_SHELL_BLUE )
			shadelight[2] = 1.0f;
	}
/*
		// PMM -special case for godmode
		if ( (currententity->flags & RF_SHELL_RED) &&
			(currententity->flags & RF_SHELL_BLUE) &&
			(currententity->flags & RF_SHELL_GREEN) )
		{
			for (i=0 ; i<3 ; i++)
				shadelight[i] = 1.0;
		}
		else if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE ) )
		{
			VectorClear (shadelight);

			if ( currententity->flags & RF_SHELL_RED )
			{
				shadelight[0] = 1.0;
				if (currententity->flags & (RF_SHELL_BLUE|RF_SHELL_DOUBLE) )
					shadelight[2] = 1.0;
			}
			else if ( currententity->flags & RF_SHELL_BLUE )
			{
				if ( currententity->flags & RF_SHELL_DOUBLE )
				{
					shadelight[1] = 1.0;
					shadelight[2] = 1.0;
				}
				else
				{
					shadelight[2] = 1.0;
				}
			}
			else if ( currententity->flags & RF_SHELL_DOUBLE )
			{
				shadelight[0] = 0.9;
				shadelight[1] = 0.7;
			}
		}
		else if ( currententity->flags & ( RF_SHELL_HALF_DAM | RF_SHELL_GREEN ) )
		{
			VectorClear (shadelight);
			// PMM - new colors
			if ( currententity->flags & RF_SHELL_HALF_DAM )
			{
				shadelight[0] = 0.56;
				shadelight[1] = 0.59;
				shadelight[2] = 0.45;
			}
			if ( currententity->flags & RF_SHELL_GREEN )
			{
				shadelight[1] = 1.0;
			}
		}
	}
			//PMM - ok, now flatten these down to range from 0 to 1.0.
	//		max_shell_val = max(shadelight[0], max(shadelight[1], shadelight[2]));
	//		if (max_shell_val > 0)
	//		{
	//			for (i=0; i<3; i++)
	//			{
	//				shadelight[i] = shadelight[i] / max_shell_val;
	//			}
	//		}
	// pmm
*/
	else if ( currententity->flags & RF_FULLBRIGHT )
	{
		shadelight[0] = 1.0f;
		shadelight[1] = 1.0f;
		shadelight[2] = 1.0f;
	}
	else
	{
		R_LightPoint (currententity->origin, shadelight);

		// player lighting hack for communication back to server
		// big hack!
		if ( currententity->flags & RF_WEAPONMODEL )
		{
			// pick the greatest component, which should be the same
			// as the mono value returned by software
			if (shadelight[0] > shadelight[1])
			{
				if (shadelight[0] > shadelight[2])
					R_SetModelLightLevel (150.0f * shadelight[0]);
				else
					R_SetModelLightLevel (150.0f * shadelight[2]);
			}
			else
			{
				if (shadelight[1] > shadelight[2])
					R_SetModelLightLevel (150.0f * shadelight[1]);
				else
					R_SetModelLightLevel (150.0f * shadelight[2]);
			}

		}
		
		/*if ( gl_monolightmap->string[0] != '0' )
		{
			float s = shadelight[0];

			if ( s < shadelight[1] )
				s = shadelight[1];
			if ( s < shadelight[2] )
				s = shadelight[2];

			shadelight[0] = s;
			shadelight[1] = s;
			shadelight[2] = s;
		}*/
	}

	if ( currententity->flags & RF_MINLIGHT )
	{
		for (i=0 ; i<3 ; i++)
			if (shadelight[i] > 0.1f)
				break;
		if (i == 3)
		{
			shadelight[0] = 0.1f;
			shadelight[1] = 0.1f;
			shadelight[2] = 0.1f;
		}
	}

	if ( currententity->flags & RF_GLOW )
	{	// bonus items will pulse with time
		float	scale;
		float	min;

		scale = 0.1f * (float)sin(r_newrefdef.time*7);
		for (i=0 ; i<3 ; i++)
		{
			min = shadelight[i] * 0.8f;
			shadelight[i] += scale;
			if (shadelight[i] < min)
				shadelight[i] = min;
		}
	}

// =================
// PGM	ir goggles color override
	if ( r_newrefdef.rdflags & RDF_IRGOGGLES && currententity->flags & RF_IR_VISIBLE)
	{
		shadelight[0] = 1.0f;
		shadelight[1] = 0.0f;
		shadelight[2] = 0.0f;
	}
// PGM	
// =================

	an = currententity->angles[1]/180*M_PI;
	shadevector[0] = (float)cos(-an);
	shadevector[1] = (float)sin(-an);
	shadevector[2] = 1;
	VectorNormalize (shadevector);

	//
	// locate the proper data
	//

	c_alias_polys += paliashdr->num_tris;

	//
	// draw all the triangles
	//
	if (currententity->flags & RF_DEPTHHACK) // hack the depth range to prevent view model from poking into walls
		qglDepthRange (gldepthmin, gldepthmin + 0.3*(gldepthmax-gldepthmin));

	if ((currententity->flags & RF_WEAPONMODEL) &&
		(r_lefthand->value == 1.0F || (cl_gunfov && cl_gunfov->value > 0)))
	{
		qglMatrixMode( GL_PROJECTION );
		qglPushMatrix();
		qglLoadIdentity();
		if (r_lefthand->value == 1.0F)
			qglScalef (-1, 1, 1);
		MYgluPerspective (R_GunFov (), (float)r_newrefdef.width / r_newrefdef.height, R_NearClip (), gl_zfar->value);
		qglMatrixMode( GL_MODELVIEW );

		if (r_lefthand->value == 1.0F)
			qglCullFace (GL_BACK);
	}

    qglPushMatrix ();

	e->angles[PITCH] = -e->angles[PITCH];	// sigh.
	R_RotateForEntity (e);
	e->angles[PITCH] = -e->angles[PITCH];	// sigh.

	// Resolve the same skin used for cel eligibility and the filled model.
	skin = GL_AliasSkin ();


	GL_Bind(skin->texnum);

	// draw it

	qglShadeModel (GL_SMOOTH);

	GL_TexEnv( GL_MODULATE );
	if (currententity->flags & RF_TRANSLUCENT || (skin->has_alpha && FLOAT_NE_ZERO (gl_alphaskins->value)) ||
		((currententity->flags & RF_WEAPONMODEL) && R_GunAlpha () < 1))
	{
		qglEnable (GL_BLEND);
	}


	/*if ( (currententity->frame >= paliashdr->num_frames) 
		|| (currententity->frame < 0) )
	{
		ri.Con_Printf (PRINT_ALL, "R_DrawAliasModel %s: no such frame %d\n",
			currentmodel->name, currententity->frame);
		currententity->frame = 0;
		currententity->oldframe = 0;
	}

	if ( (currententity->oldframe >= paliashdr->num_frames)
		|| (currententity->oldframe < 0))
	{
		ri.Con_Printf (PRINT_ALL, "R_DrawAliasModel %s: no such oldframe %d\n",
			currentmodel->name, currententity->oldframe);
		currententity->frame = 0;
		currententity->oldframe = 0;
	}*/

	if ( FLOAT_EQ_ZERO(r_lerpmodels->value) )
		currententity->backlerp = 0;

	GL_DrawAliasFrameLerp (paliashdr, currententity->backlerp, skin->glowtexnum);

	GL_TexEnv( GL_REPLACE );
	qglShadeModel (GL_FLAT);

	qglPopMatrix ();

	if ((currententity->flags & RF_WEAPONMODEL) &&
		(r_lefthand->value == 1.0F || (cl_gunfov && cl_gunfov->value > 0)))
	{
		qglMatrixMode( GL_PROJECTION );
		qglPopMatrix();
		qglMatrixMode( GL_MODELVIEW );
		if (r_lefthand->value == 1.0F)
			qglCullFace (GL_FRONT);
	}

	if (currententity->flags & RF_TRANSLUCENT || (skin->has_alpha && FLOAT_NE_ZERO (gl_alphaskins->value)) ||
		((currententity->flags & RF_WEAPONMODEL) && R_GunAlpha () < 1))
	{
		qglDisable (GL_BLEND);
	}

	if (currententity->flags & RF_DEPTHHACK)
		qglDepthRange (gldepthmin, gldepthmax);

#if 1
	if (FLOAT_NE_ZERO (gl_shadows->value) && !(currententity->flags & (RF_TRANSLUCENT | RF_WEAPONMODEL)) &&
		(shadow_alpha = R_ShadowAlpha (e, currententity->backlerp)) > 0)
	{
		GL_BeginShadow (e);
		qglDisable (GL_TEXTURE_2D);
		qglEnable (GL_BLEND);
		qglColor4f (0, 0, 0, shadow_alpha);
		GL_DrawAliasShadow (paliashdr);
		qglEnable (GL_TEXTURE_2D);
		qglDisable (GL_BLEND);
		GL_EndShadow ();
	}
#endif
	qglColor4fv(colorWhite);
}

/* MD3 models use private, validated mesh data; never interpret it as dmdl_t. */
static float md3_vertices[MD3_MAX_VERTICES][9]; /* xyz, st, rgba */
static float md3_sin[256], md3_cos[256];
static qboolean md3_normal_table_ready;

static image_t *GL_MD3Skin (const mmd3mesh_t *mesh, const entity_t *e)
{
	image_t *skin;
	int i;

	if (e->skin)
		return e->skin;
	if (mesh->skins && mesh->numskins > 0)
	{
		i = e->skinnum;
		if (i < 0 || i >= mesh->numskins || !mesh->skins[i])
			i = 0;
		skin = mesh->skins[i];
		if (skin)
			return skin;
		for (i = 0; i < mesh->numskins; i++)
			if (mesh->skins[i])
				return mesh->skins[i];
	}
	return r_notexture;
}

qboolean R_MD3HasAlphaSkin (entity_t *e)
{
	mmd3model_t *md3;
	int i;

	if (!e->model || e->model->type != mod_md3 || !e->model->md3)
		return false;
	/* Powerup shells discard the skin texture and render as opaque geometry. */
	if (e->flags & RF_SHELL_MASK)
		return false;
	md3 = e->model->md3;
	for (i = 0; i < md3->nummeshes; i++)
		if (GL_MD3Skin (&md3->meshes[i], e)->has_alpha)
			return true;
	return false;
}

static qboolean GL_MD3MeshTranslucent (const mmd3mesh_t *mesh,
	const entity_t *e)
{
	if (e->flags & RF_TRANSLUCENT)
		return true;
	if ((e->flags & RF_WEAPONMODEL) && R_GunAlpha () < 1)
		return true;
	if (e->flags & RF_SHELL_MASK)
		return false;
	return gl_alphaskins && FLOAT_NE_ZERO (gl_alphaskins->value) &&
		GL_MD3Skin (mesh, e)->has_alpha;
}

static void GL_MD3Normal (const mmd3vertex_t *v, vec3_t normal)
{
	int i;
	unsigned lat = v->normal[0], lng = v->normal[1];

	if (!md3_normal_table_ready)
	{
		for (i = 0; i < 256; i++)
		{
			float angle = i * (float)(2 * M_PI / 255.0);
			md3_sin[i] = (float)sin (angle);
		}
		for (i = 0; i < 256; i++)
			md3_cos[i] = md3_sin[(i + 64) & 255];
		md3_normal_table_ready = true;
	}

	/* MD3 stores latitude and longitude as two bytes, in that order. */
	normal[0] = md3_sin[lat] * md3_cos[lng];
	normal[1] = md3_sin[lat] * md3_sin[lng];
	normal[2] = md3_cos[lat];
}

static float GL_MD3Shade (const vec3_t normal)
{
	float dot;

	if (gl_dotshading && FLOAT_EQ_ZERO (gl_dotshading->value))
		return 1.0f;
	dot = DotProduct (normal, shadevector);
	if (dot < 0)
		dot *= 0.3f;
	return dot + 1.0f;
}

/* Match the active MD2 lighting rules without changing its draw path. */
static void GL_MD3SetLighting (void)
{
	int i;
	float an;

	if (currententity->flags & RF_SHELL_MASK)
	{
		VectorClear (shadelight);
		if (currententity->flags & RF_SHELL_HALF_DAM)
		{
			shadelight[0] = 0.56f;
			shadelight[1] = 0.59f;
			shadelight[2] = 0.45f;
		}
		if (currententity->flags & RF_SHELL_DOUBLE)
		{
			shadelight[0] = 0.9f;
			shadelight[1] = 0.7f;
		}
		if (currententity->flags & RF_SHELL_RED)
			shadelight[0] = 1.0f;
		if (currententity->flags & RF_SHELL_GREEN)
			shadelight[1] = 1.0f;
		if (currententity->flags & RF_SHELL_BLUE)
			shadelight[2] = 1.0f;
	}
	else if (currententity->flags & RF_FULLBRIGHT)
		shadelight[0] = shadelight[1] = shadelight[2] = 1.0f;
	else
	{
		R_LightPoint (currententity->origin, shadelight);
		if (currententity->flags & RF_WEAPONMODEL)
		{
			float maxlight = shadelight[0];
			if (shadelight[1] > maxlight)
				maxlight = shadelight[1];
			if (shadelight[2] > maxlight)
				maxlight = shadelight[2];
			R_SetModelLightLevel (150.0f * maxlight);
		}
	}
	if (currententity->flags & RF_MINLIGHT)
	{
		for (i = 0; i < 3 && shadelight[i] <= 0.1f; i++)
			;
		if (i == 3)
			shadelight[0] = shadelight[1] = shadelight[2] = 0.1f;
	}
	if (currententity->flags & RF_GLOW)
	{
		float scale = 0.1f * (float)sin (r_newrefdef.time * 7);
		for (i = 0; i < 3; i++)
		{
			float min = shadelight[i] * 0.8f;
			shadelight[i] += scale;
			if (shadelight[i] < min)
				shadelight[i] = min;
		}
	}
	if ((r_newrefdef.rdflags & RDF_IRGOGGLES) &&
		(currententity->flags & RF_IR_VISIBLE))
	{
		shadelight[0] = 1.0f;
		shadelight[1] = shadelight[2] = 0.0f;
	}
	an = currententity->angles[YAW] / 180 * M_PI;
	shadevector[0] = (float)cos (-an);
	shadevector[1] = (float)sin (-an);
	shadevector[2] = 1.0f;
	VectorNormalize (shadevector);
}

static qboolean R_CullMD3Model (const mmd3model_t *md3, const entity_t *e)
{
	vec3_t mins, maxs, worldmins, worldmaxs, angles, vectors[3];
	int i, j;

	if (FLOAT_EQ_ZERO (gl_cull_models->value) || FLOAT_NE_ZERO (r_nocull->value))
		return false;
	for (j = 0; j < 3; j++)
	{
		mins[j] = md3->frames[e->frame].mins[j];
		maxs[j] = md3->frames[e->frame].maxs[j];
		if (md3->frames[e->oldframe].mins[j] < mins[j])
			mins[j] = md3->frames[e->oldframe].mins[j];
		if (md3->frames[e->oldframe].maxs[j] > maxs[j])
			maxs[j] = md3->frames[e->oldframe].maxs[j];
		if (e->flags & RF_SHELL_MASK)
		{
			mins[j] -= POWERSUIT_SCALE;
			maxs[j] += POWERSUIT_SCALE;
		}
		worldmins[j] = 1e30f;
		worldmaxs[j] = -1e30f;
	}
	VectorCopy (e->angles, angles);
	angles[YAW] = -angles[YAW];
	AngleVectors (angles, vectors[0], vectors[1], vectors[2]);
	for (i = 0; i < 8; i++)
	{
		vec3_t corner, world;
		corner[0] = (i & 1) ? mins[0] : maxs[0];
		corner[1] = (i & 2) ? mins[1] : maxs[1];
		corner[2] = (i & 4) ? mins[2] : maxs[2];
		world[0] = DotProduct (vectors[0], corner) + e->origin[0];
		world[1] = -DotProduct (vectors[1], corner) + e->origin[1];
		world[2] = DotProduct (vectors[2], corner) + e->origin[2];
		for (j = 0; j < 3; j++)
		{
			if (world[j] < worldmins[j])
				worldmins[j] = world[j];
			if (world[j] > worldmaxs[j])
				worldmaxs[j] = world[j];
		}
	}
	/* Interpolation can place the mesh between both entity origins. */
	for (j = 0; j < 3; j++)
	{
		float origin_delta = e->oldorigin[j] - e->origin[j];
		if (origin_delta < 0)
			worldmins[j] += origin_delta;
		else
			worldmaxs[j] += origin_delta;
	}
	return R_CullBox (worldmins, worldmaxs);
}

static qboolean GL_MD3ArraySupport (void)
{
	return gl_vertex_arrays && FLOAT_NE_ZERO (gl_vertex_arrays->value) &&
		qglDrawElements && qglVertexPointer && qglTexCoordPointer &&
		qglColorPointer && qglEnableClientState && qglDisableClientState &&
		qglPushClientAttrib && qglPopClientAttrib &&
		(!(qglBindBuffer || qglClientActiveTextureARB) || qglGetIntegerv);
}

static void GL_MD3Submit (const mmd3mesh_t *mesh, qboolean textured)
{
	int i;
	GLint saved_array = 0, saved_elements = 0, saved_client_tmu = GL_TEXTURE0;
	qboolean arrays = GL_MD3ArraySupport ();

	if (arrays)
	{
		qglPushClientAttrib (GL_CLIENT_VERTEX_ARRAY_BIT);
		if (qglClientActiveTextureARB)
		{
			qglGetIntegerv (GL_CLIENT_ACTIVE_TEXTURE, &saved_client_tmu);
			qglClientActiveTextureARB (GL_TEXTURE1);
			qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
			qglClientActiveTextureARB (GL_TEXTURE0);
		}
		if (qglBindBuffer)
		{
			qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_array);
			qglGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &saved_elements);
			qglBindBuffer (GL_ARRAY_BUFFER, 0);
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		qglDisableClientState (GL_NORMAL_ARRAY);
		qglDisableClientState (GL_INDEX_ARRAY);
		qglDisableClientState (GL_EDGE_FLAG_ARRAY);
		qglEnableClientState (GL_VERTEX_ARRAY);
		qglVertexPointer (3, GL_FLOAT, sizeof (md3_vertices[0]), md3_vertices[0]);
		if (textured)
		{
			qglEnableClientState (GL_TEXTURE_COORD_ARRAY);
			qglTexCoordPointer (2, GL_FLOAT, sizeof (md3_vertices[0]),
				&md3_vertices[0][3]);
		}
		else
			qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
		qglEnableClientState (GL_COLOR_ARRAY);
		qglColorPointer (4, GL_FLOAT, sizeof (md3_vertices[0]),
			&md3_vertices[0][5]);
		qglDrawElements (GL_TRIANGLES, mesh->numtris * 3,
			GL_UNSIGNED_INT, mesh->indices);
		qglPopClientAttrib ();
		if (qglBindBuffer)
		{
			qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, (GLuint)saved_elements);
		}
		if (qglClientActiveTextureARB)
			qglClientActiveTextureARB ((GLenum)saved_client_tmu);
		c_alias_batches++;
		c_alias_vertices += mesh->numverts;
		c_alias_indices += mesh->numtris * 3;
	}
	else
	{
		qglBegin (GL_TRIANGLES);
		for (i = 0; i < mesh->numtris * 3; i++)
		{
			unsigned index = mesh->indices[i];
			if (index >= (unsigned)mesh->numverts)
				continue; /* Parser checks this; retain a safe fallback. */
			if (textured)
				qglTexCoord2fv (&md3_vertices[index][3]);
			qglColor4fv (&md3_vertices[index][5]);
			qglVertex3fv (md3_vertices[index]);
		}
		qglEnd ();
		c_alias_immediate++;
	}
}

static void GL_MD3BuildMesh (const mmd3mesh_t *mesh, const entity_t *e,
	float backlerp, const vec3_t move, float alpha, qboolean shell)
{
	float frontlerp = 1.0f - backlerp;
	int i, j;

	for (i = 0; i < mesh->numverts; i++)
	{
		const mmd3vertex_t *v = &mesh->vertices[e->frame * mesh->numverts + i];
		const mmd3vertex_t *ov = &mesh->vertices[e->oldframe * mesh->numverts + i];
		vec3_t normal, oldnormal;
		float light;

		GL_MD3Normal (v, normal);
		light = shell ? 1.0f : GL_MD3Shade (normal) * frontlerp;
		if (backlerp > 0)
		{
			GL_MD3Normal (ov, oldnormal);
			if (!shell)
				light += GL_MD3Shade (oldnormal) * backlerp;
			for (j = 0; j < 3; j++)
				normal[j] = normal[j] * frontlerp + oldnormal[j] * backlerp;
			if (shell)
				VectorNormalize (normal);
		}
		for (j = 0; j < 3; j++)
			md3_vertices[i][j] = move[j] +
				(v->xyz[j] * frontlerp + ov->xyz[j] * backlerp) / 64.0f +
				(shell ? normal[j] * POWERSUIT_SCALE : 0.0f);
		md3_vertices[i][3] = mesh->st[i][0];
		md3_vertices[i][4] = mesh->st[i][1];
		md3_vertices[i][5] = shadelight[0] * light;
		md3_vertices[i][6] = shadelight[1] * light;
		md3_vertices[i][7] = shadelight[2] * light;
		md3_vertices[i][8] = alpha;
	}
}

static float GL_MD3CelScale (image_t *skin, float alpha)
{
	vec3_t delta;
	float width = gl_celshading ? gl_celshading->value : 0;
	float scale;

	if (!(width > 0) || width > FLT_MAX || !(alpha >= 1.0f) ||
		(currententity->flags & (RF_TRANSLUCENT | RF_SHELL_MASK)) ||
		!qglPolygonMode || !qglLineWidth || !qglPushAttrib || !qglPopAttrib ||
		!qglCullFace || !qglBlendFunc ||
		(gl_alphaskins && FLOAT_NE_ZERO (gl_alphaskins->value) && skin->has_alpha))
		return 0;
	VectorSubtract (currententity->origin, r_newrefdef.vieworg, delta);
	scale = 1.0f - VectorLength (delta) / 700.0f;
	return scale >= 0.01f ? scale : 0;
}

static void GL_ShowMD3Tris (const mmd3mesh_t *mesh)
{
	int i;

	if (!GL_ShowTrisBegin (GL_SHOWTRIS_MESH))
		return;
	qglBegin (GL_TRIANGLES);
	for (i = 0; i < mesh->numtris * 3; i++)
	{
		unsigned index = mesh->indices[i];
		if (index < (unsigned)mesh->numverts)
			qglVertex3fv (md3_vertices[index]);
	}
	qglEnd ();
	GL_ShowTrisEnd ();
}

static void GL_MD3DrawCel (const mmd3mesh_t *mesh, float scale)
{
	float width = gl_celshading->value;
	qboolean bloom_shader;
	int i;

	if (scale <= 0)
		return;
	if (width > 10)
		width = 10;
	qglPushAttrib (GL_CURRENT_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
		GL_POLYGON_BIT | GL_LINE_BIT);
	qglDisable (GL_TEXTURE_2D);
	qglEnable (GL_BLEND);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglEnable (GL_CULL_FACE);
	qglCullFace ((currententity->flags & RF_WEAPONMODEL) &&
				r_lefthand && r_lefthand->value == 1.0f ?
			GL_FRONT :
			GL_BACK);
	qglPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	qglLineWidth (width * scale);
	for (i = 0; i < mesh->numverts; i++)
	{
		md3_vertices[i][5] = md3_vertices[i][6] = md3_vertices[i][7] = 0;
		md3_vertices[i][8] = scale;
	}
	bloom_shader = GL_BeginBloomShader (false, false, false);
	GL_MD3Submit (mesh, false);
	if (bloom_shader)
		GL_EndSurfaceShader ();
	qglPopAttrib ();
	c_alias_cel_draws++;
}

static void GL_MD3DrawShadow (const mmd3model_t *md3, const entity_t *e,
	float backlerp, const vec3_t move, float alpha)
{
	float lheight;
	qboolean bloom_shader;
	int i, j;

	if (!(alpha > 0))
		return;
	lheight = e->origin[2] - lightspot[2];
	GL_BeginShadow ((entity_t *)e);
	qglDisable (GL_TEXTURE_2D);
	qglEnable (GL_BLEND);
	qglColor4f (0, 0, 0, alpha);
	bloom_shader = GL_BeginBloomShader (false, false, false);
	for (i = 0; i < md3->nummeshes; i++)
	{
		const mmd3mesh_t *mesh = &md3->meshes[i];
		if (mesh->numverts < 1 || mesh->numverts > MD3_MAX_VERTICES ||
			mesh->numtris < 1 || !mesh->vertices || !mesh->indices || !mesh->st)
			continue;
		GL_MD3BuildMesh (mesh, e, backlerp, move, alpha, false);
		for (j = 0; j < mesh->numverts; j++)
		{
			if (!(gl_shadows->value >= 2))
			{
				md3_vertices[j][0] -= shadevector[0] * (md3_vertices[j][2] + lheight);
				md3_vertices[j][1] -= shadevector[1] * (md3_vertices[j][2] + lheight);
				md3_vertices[j][2] = -lheight + 1.0f;
			}
			md3_vertices[j][5] = md3_vertices[j][6] = md3_vertices[j][7] = 0;
		}
		GL_MD3Submit (mesh, false);
		c_alias_shadow_batches++;
	}
	if (bloom_shader)
		GL_EndSurfaceShader ();
	GL_EndShadow ();
}

static void GL_MD3DrawModel (entity_t *e, int pass)
{
	mmd3model_t *md3 = currentmodel->md3;
	vec3_t delta, vectors[3], move;
	float backlerp, frontlerp, alpha;
	qboolean shell, projection, has_opaque = false, has_translucent = false;
	unsigned saved_tmu;
	int i;

	if (!md3 || md3->numframes < 1 || !md3->frames || !md3->meshes)
		return;
	for (i = 0; i < md3->nummeshes; i++)
	{
		const mmd3mesh_t *mesh = &md3->meshes[i];
		if (mesh->numverts < 1 || mesh->numverts > MD3_MAX_VERTICES ||
			mesh->numtris < 1 || !mesh->vertices || !mesh->indices || !mesh->st)
			continue;
		if (GL_MD3MeshTranslucent (mesh, e))
			has_translucent = true;
		else
			has_opaque = true;
	}
	if ((pass == 0 && !has_opaque) || (pass == 1 && !has_translucent) ||
		(!has_opaque && !has_translucent))
		return;
	if (e->frame < 0 || e->frame >= md3->numframes)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "R_DrawMD3Model %s: no such frame %d\n",
			currentmodel->name, e->frame);
		e->frame = 0;
	}
	if (e->oldframe < 0 || e->oldframe >= md3->numframes)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "R_DrawMD3Model %s: no such oldframe %d\n",
			currentmodel->name, e->oldframe);
		e->oldframe = 0;
	}
	if (!(e->flags & RF_WEAPONMODEL) && R_CullMD3Model (md3, e))
	{
		if (pass < 0 || pass == 0 || !has_opaque)
			R_DrawCulledModelShadow (e);
		return;
	}
	if ((e->flags & RF_WEAPONMODEL) && r_lefthand->value == 2)
		return;

	GL_MD3SetLighting ();
	shell = (e->flags & RF_SHELL_MASK) != 0;
	alpha = (e->flags & RF_TRANSLUCENT) ? e->alpha : 1.0f;
	if (e->flags & RF_WEAPONMODEL)
		alpha *= R_GunAlpha ();
	backlerp = (r_lerpmodels && FLOAT_NE_ZERO (r_lerpmodels->value)) ? e->backlerp : 0;
	if (!(backlerp >= 0))
		backlerp = 0;
	if (backlerp > 1)
		backlerp = 1;
	frontlerp = 1.0f - backlerp;
	VectorSubtract (e->oldorigin, e->origin, delta);
	AngleVectors (e->angles, vectors[0], vectors[1], vectors[2]);
	move[0] = DotProduct (delta, vectors[0]);
	move[1] = -DotProduct (delta, vectors[1]);
	move[2] = DotProduct (delta, vectors[2]);
	for (i = 0; i < 3; i++)
		move[i] = backlerp * (move[i] + md3->frames[e->oldframe].translate[i]) +
			frontlerp * md3->frames[e->frame].translate[i];

	saved_tmu = gl_state.currenttmu;
	GL_SelectTexture (GL_TEXTURE0);
	qglPushAttrib (GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT |
		GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT | GL_LINE_BIT);
	if (e->flags & RF_DEPTHHACK)
		qglDepthRange (gldepthmin, gldepthmin + 0.3 * (gldepthmax - gldepthmin));
	projection = (e->flags & RF_WEAPONMODEL) &&
		(r_lefthand->value == 1.0f || (cl_gunfov && cl_gunfov->value > 0));
	if (projection)
	{
		qglMatrixMode (GL_PROJECTION);
		qglPushMatrix ();
		qglLoadIdentity ();
		if (r_lefthand->value == 1.0f)
			qglScalef (-1, 1, 1);
		MYgluPerspective (R_GunFov (),
			(float)r_newrefdef.width / (r_newrefdef.height ? r_newrefdef.height : 1),
			R_NearClip (), gl_zfar->value);
		qglMatrixMode (GL_MODELVIEW);
		if (r_lefthand->value == 1.0f)
			qglCullFace (GL_BACK);
	}
	qglPushMatrix ();
	e->angles[PITCH] = -e->angles[PITCH];
	R_RotateForEntity (e);
	e->angles[PITCH] = -e->angles[PITCH];
	qglShadeModel (GL_SMOOTH);
	GL_TexEnv (GL_MODULATE);

	for (i = 0; i < md3->nummeshes; i++)
	{
		mmd3mesh_t *mesh = &md3->meshes[i];
		image_t *skin;
		qboolean material_shader = false, gpu_drawn = false;
		float celscale;

		if (mesh->numverts < 1 || mesh->numverts > MD3_MAX_VERTICES ||
			mesh->numtris < 1 || !mesh->vertices || !mesh->indices || !mesh->st)
			continue;
		if (pass >= 0 && GL_MD3MeshTranslucent (mesh, e) != (pass == 1))
			continue;
		skin = GL_MD3Skin (mesh, e);
		if (shell)
			qglDisable (GL_TEXTURE_2D);
		else
		{
			qglEnable (GL_TEXTURE_2D);
			GL_Bind (skin->texnum);
		}
		if (GL_MD3MeshTranslucent (mesh, e))
			qglEnable (GL_BLEND);
		else
			qglDisable (GL_BLEND);
		c_alias_polys += mesh->numtris;
		if (!shell)
			gpu_drawn = GL_DrawMD3GPU (mesh, move, backlerp, alpha,
				skin->glowtexnum);
		if (!gpu_drawn)
		{
			GL_MD3BuildMesh (mesh, e, backlerp, move, alpha, shell);
			if (GL_WeaponDepthPrefill (alpha))
			{
				GL_BeginWeaponDepthPrefill ();
				GL_MD3Submit (mesh, false);
				GL_EndWeaponDepthPrefill ();
			}
			if (!shell && GL_ImageShaderIntensity ())
				material_shader = GL_BeginLitMaterialShader (true, GL_MODULATE,
					0, 0, 1, skin->glowtexnum);
			else if (GL_BloomActive () && (!skin->glowtexnum || shell))
				material_shader = GL_BeginBloomShader (!shell, true, shell);
			else if (!shell && skin->glowtexnum && GL_GlowmapsAvailable ())
				material_shader = GL_BeginMaterialShader (true, true, GL_MODULATE,
					0, 0, 1, skin->glowtexnum);
			else if (GL_BloomActive ())
				material_shader = GL_BeginBloomShader (true, true, false);
			GL_MD3Submit (mesh, !shell);
			if (material_shader)
				GL_EndSurfaceShader ();
		}
		celscale = GL_MD3CelScale (skin, alpha);
		if (gpu_drawn && celscale > 0)
			GL_MD3BuildMesh (mesh, e, backlerp, move, alpha, false);
		GL_MD3DrawCel (mesh, celscale);
		if (gpu_drawn)
		{
			/* Keep the GPU pose and draw only its outline after the cel pass. */
			if (GL_ShowTrisBegin (GL_SHOWTRIS_MESH))
			{
				GL_DrawMD3GPU (mesh, move, backlerp, alpha,
					skin->glowtexnum);
				GL_ShowTrisEnd ();
			}
		}
		else
			GL_ShowMD3Tris (mesh);
	}
	GL_TexEnv (GL_REPLACE);
	qglShadeModel (GL_FLAT);
	qglPopMatrix ();
	if (projection)
	{
		qglMatrixMode (GL_PROJECTION);
		qglPopMatrix ();
		qglMatrixMode (GL_MODELVIEW);
	}
	if (e->flags & RF_DEPTHHACK)
		qglDepthRange (gldepthmin, gldepthmax);
	if ((pass < 0 || pass == 0 || !has_opaque) &&
		FLOAT_NE_ZERO (gl_shadows->value) &&
		!(e->flags & (RF_TRANSLUCENT | RF_WEAPONMODEL)))
		GL_MD3DrawShadow (md3, e, backlerp, move, R_ShadowAlpha (e, backlerp));
	qglPopAttrib ();
	GL_SelectTexture (saved_tmu ? GL_TEXTURE1 : GL_TEXTURE0);
	GL_CheckForError ();
}

void R_DrawMD3Model (entity_t *e)
{
	GL_MD3DrawModel (e, -1);
}

void R_DrawMD3ModelPass (entity_t *e, qboolean translucent)
{
	GL_MD3DrawModel (e, translucent ? 1 : 0);
}

/* Opt-in MD5 CPU wall timing. The renderer frame counter groups MD5-active
 * views without adding a public API or synchronizing with the GPU. */
#define MD5_CPU_TIMING 1
#ifdef _WIN32
typedef LARGE_INTEGER md5_perf_tick_t;
static LARGE_INTEGER md5_perf_frequency;
#else
typedef struct timespec md5_perf_tick_t;
#endif

typedef struct
{
	int frame_id;
	qboolean active, bad_clock;
	double draws, builds, shadow_builds, vertices, weights;
	double pose_ms, skin_ms, shadow_ms, pass_ms;
} md5_perf_frame_t;

typedef struct
{
	unsigned int attempts, valid, bad_clock;
	double draws, builds, shadow_builds, vertices, weights;
	double pose_sum, skin_sum, shadow_sum, pass_sum;
	double pose_max, skin_max, pass_max;
} md5_perf_stats_t;

static md5_perf_frame_t md5_perf_frame;
static md5_perf_stats_t md5_perf_stats;
static qboolean md5_perf_enabled, md5_perf_pass_active;

static void MD5_PerfReset (void)
{
	memset (&md5_perf_frame, 0, sizeof (md5_perf_frame));
	memset (&md5_perf_stats, 0, sizeof (md5_perf_stats));
	md5_perf_enabled = md5_perf_pass_active = false;
}

void R_MD5PerfBeginFrame (void)
{
	/* Observe mode changes even when this frame draws no MD5 model. */
	if (md5_perf_enabled && (!r_speeds || r_speeds->value < 3))
		MD5_PerfReset ();
}

static qboolean MD5_PerfNow (md5_perf_tick_t *tick)
{
#ifdef _WIN32
	LARGE_INTEGER frequency;
	if (md5_perf_frequency.QuadPart <= 0)
	{
		if (!QueryPerformanceFrequency (&frequency) || frequency.QuadPart <= 0)
			return false;
		md5_perf_frequency = frequency;
	}
	return QueryPerformanceCounter (tick) != 0;
#else
	return clock_gettime (CLOCK_MONOTONIC, tick) == 0;
#endif
}

static double MD5_PerfElapsedMs (const md5_perf_tick_t *start,
	const md5_perf_tick_t *end)
{
#ifdef _WIN32
	if (end->QuadPart < start->QuadPart)
		return -1;
	return (double)(end->QuadPart - start->QuadPart) * 1000.0 /
		(double)md5_perf_frequency.QuadPart;
#else
	double elapsed = (double)(end->tv_sec - start->tv_sec) * 1000.0 +
		(double)(end->tv_nsec - start->tv_nsec) / 1000000.0;
	return elapsed >= 0 ? elapsed : -1;
#endif
}

static void MD5_PerfCommitFrame (void)
{
	md5_perf_frame_t *f = &md5_perf_frame;
	md5_perf_stats_t *s = &md5_perf_stats;
	if (!f->active)
		return;
	s->attempts++;
	if (f->bad_clock)
		s->bad_clock++;
	else
	{
		s->valid++;
		s->draws += f->draws;
		s->builds += f->builds;
		s->shadow_builds += f->shadow_builds;
		s->vertices += f->vertices;
		s->weights += f->weights;
		s->pose_sum += f->pose_ms;
		s->skin_sum += f->skin_ms;
		s->shadow_sum += f->shadow_ms;
		s->pass_sum += f->pass_ms;
		if (f->pose_ms > s->pose_max)
			s->pose_max = f->pose_ms;
		if (f->skin_ms > s->skin_max)
			s->skin_max = f->skin_ms;
		if (f->pass_ms > s->pass_max)
			s->pass_max = f->pass_ms;
	}
	if (s->attempts < 120)
		return;
	if (s->valid)
		ri.Con_Printf (PRINT_ALL,
			"MD5 CPU: %u/%u active views, %.0f draws, %.0f builds (%.0f shadow), %.0f vertices, %.0f weights; CPU wall ms/view avg/max: pose %.3f/%.3f, skin %.3f/%.3f (shadow %.3f avg), full pass %.3f/%.3f\n",
			s->valid, s->attempts, s->draws, s->builds,
			s->shadow_builds, s->vertices, s->weights,
			s->pose_sum / s->valid, s->pose_max,
			s->skin_sum / s->valid, s->skin_max,
			s->shadow_sum / s->valid,
			s->pass_sum / s->valid, s->pass_max);
	if (s->bad_clock)
		ri.Con_Printf (PRINT_ALL,
			"MD5 CPU skipped: clock %u\n", s->bad_clock);
	memset (s, 0, sizeof (*s));
}

static qboolean MD5_PerfBeginPass (md5_perf_tick_t *start)
{
	if (!r_speeds || !(r_speeds->value >= 3))
	{
		if (md5_perf_enabled)
			MD5_PerfReset ();
		return false;
	}
	md5_perf_enabled = true;
	if (!md5_perf_frame.active || md5_perf_frame.frame_id != r_framecount)
	{
		if (md5_perf_frame.active && r_framecount <= md5_perf_frame.frame_id)
			MD5_PerfReset (); /* A renderer restart reset the frame number. */
		else
			MD5_PerfCommitFrame ();
		memset (&md5_perf_frame, 0, sizeof (md5_perf_frame));
		md5_perf_frame.active = true;
		md5_perf_frame.frame_id = r_framecount;
		md5_perf_enabled = true;
	}
	if (md5_perf_frame.bad_clock)
		return false;
	md5_perf_frame.draws++;
	if (!MD5_PerfNow (start))
	{
		md5_perf_frame.bad_clock = true;
		return false;
	}
	md5_perf_pass_active = true;
	return true;
}

static qboolean MD5_PerfStart (md5_perf_tick_t *start)
{
	if (!md5_perf_pass_active || md5_perf_frame.bad_clock)
		return false;
	if (MD5_PerfNow (start))
		return true;
	md5_perf_frame.bad_clock = true;
	return false;
}

static double MD5_PerfStop (const md5_perf_tick_t *start, qboolean started)
{
	md5_perf_tick_t end;
	double elapsed;
	if (!started || md5_perf_frame.bad_clock)
		return -1;
	if (!MD5_PerfNow (&end))
	{
		md5_perf_frame.bad_clock = true;
		return -1;
	}
	elapsed = MD5_PerfElapsedMs (start, &end);
	if (elapsed < 0)
		md5_perf_frame.bad_clock = true;
	return elapsed;
}

static void MD5_PerfEndPass (const md5_perf_tick_t *start)
{
	double elapsed = MD5_PerfStop (start, md5_perf_pass_active);
	if (elapsed >= 0)
		md5_perf_frame.pass_ms += elapsed;
	md5_perf_pass_active = false;
}

/* Optional MD5 skeletal replacements use validated, private model data. */
static mmd5joint_t md5_pose[MD5_MAX_JOINTS];
static float md5_vertices[MD5_MAX_VERTICES][9]; /* xyz, st, rgba */

qboolean R_MD5Selected (const entity_t *e)
{
	const mmd5model_t *md5;
	double dx, dy, dz, distance, limit;

	if (!e || !e->model || (e->model->type != mod_alias && e->model->type != mod_md3) || !gl_md5_use ||
		!(gl_md5_use->value >= -FLT_MAX && gl_md5_use->value <= FLT_MAX) ||
		FLOAT_EQ_ZERO (gl_md5_use->value))
		return false;
	md5 = e->model->md5;
	if (!md5 || md5->numframes < 1 || md5->numframes > MD5_MAX_FRAMES ||
		md5->numjoints < 1 || md5->numjoints > MD5_MAX_JOINTS ||
		md5->nummeshes < 1 || md5->nummeshes > MD5_MAX_MESHES ||
		!md5->frames || !md5->meshes)
		return false;
	/* R1Q2 has no public RF_NO_LOD flag. View models stay at full detail. */
	if (e->flags & RF_WEAPONMODEL)
		return true;
	limit = gl_md5_distance ? gl_md5_distance->value : 2048.0;
	if (!(limit >= -FLT_MAX && limit <= FLT_MAX))
		return false;
	if (limit <= 0)
		return true;
	dx = (double)e->origin[0] - r_newrefdef.vieworg[0];
	dy = (double)e->origin[1] - r_newrefdef.vieworg[1];
	dz = (double)e->origin[2] - r_newrefdef.vieworg[2];
	distance = dx * dx + dy * dy + dz * dz;
	return distance <= limit * limit;
}

qboolean R_MD5WorldBounds (const entity_t *e, vec3_t mins, vec3_t maxs)
{
	const mmd5model_t *md5;
	const mmd5framebound_t *newbound, *oldbound;
	double position, radius;
	int frame, oldframe, i;

	if (!e || !e->model || !(md5 = e->model->md5) ||
		!md5->bounds || md5->numframes < 1 || md5->numframes > MD5_MAX_FRAMES ||
		e->model->numframes < 1)
		return false;
	/* Match the base-frame clamp and MD5 modulo used by the draw path. */
	frame = (e->frame < 0 || e->frame >= e->model->numframes) ?
		0 :
		e->frame;
	oldframe = (e->oldframe < 0 ||
				   e->oldframe >= e->model->numframes) ?
		0 :
		e->oldframe;
	newbound = &md5->bounds[frame % md5->numframes];
	oldbound = &md5->bounds[oldframe % md5->numframes];
	if (!(newbound->position_radius >= 0 &&
			newbound->position_radius <= FLT_MAX &&
			newbound->scaled_weight_radius >= 0 &&
			newbound->scaled_weight_radius <= FLT_MAX &&
			oldbound->position_radius >= 0 &&
			oldbound->position_radius <= FLT_MAX))
		return false;
	position = newbound->position_radius > oldbound->position_radius ?
		newbound->position_radius :
		oldbound->position_radius;
	radius = 1.02 * (position + newbound->scaled_weight_radius) + 1.0;
	if (e->flags & RF_SHELL_MASK)
		radius += POWERSUIT_SCALE;
	if (!(radius >= 0 && radius <= FLT_MAX))
		return false;
	for (i = 0; i < 3; i++)
	{
		double low, high;
		if (!(e->origin[i] >= -FLT_MAX && e->origin[i] <= FLT_MAX &&
				e->oldorigin[i] >= -FLT_MAX &&
				e->oldorigin[i] <= FLT_MAX))
			return false;
		low = e->origin[i] < e->oldorigin[i] ?
			e->origin[i] :
			e->oldorigin[i];
		high = e->origin[i] > e->oldorigin[i] ?
			e->origin[i] :
			e->oldorigin[i];
		low -= radius;
		high += radius;
		if (!(low >= -FLT_MAX && high <= FLT_MAX))
			return false;
		mins[i] = (float)low;
		maxs[i] = (float)high;
	}
	return true;
}

static image_t *GL_MD5Skin (const mmd5model_t *md5, const entity_t *e)
{
	image_t *skin;
	int i;

	if (e->skin)
		return e->skin;
	if (md5->skins && md5->numskins > 0)
	{
		i = e->skinnum;
		if (i < 0 || i >= md5->numskins || !md5->skins[i])
			i = 0;
		skin = md5->skins[i];
		if (skin)
			return skin;
		for (i = 0; i < md5->numskins; i++)
			if (md5->skins[i])
				return md5->skins[i];
	}
	return r_notexture;
}

qboolean R_MD5HasAlphaSkin (const entity_t *e)
{
	if (!e || !e->model || !e->model->md5)
		return false;
	/* Shell geometry is opaque and does not sample its skin. */
	if (e->flags & RF_SHELL_MASK)
		return false;
	return GL_MD5Skin (e->model->md5, e)->has_alpha;
}

qboolean R_MD5Translucent (const entity_t *e)
{
	if (e->flags & RF_TRANSLUCENT)
		return true;
	if ((e->flags & RF_WEAPONMODEL) && R_GunAlpha () < 1)
		return true;
	return gl_alphaskins && FLOAT_NE_ZERO (gl_alphaskins->value) &&
		R_MD5HasAlphaSkin (e);
}

static void GL_MD5NormalizeQuat (float q[4])
{
	float length = (float)sqrt (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	int i;
	if (!(length > 0) || length > FLT_MAX)
	{
		q[0] = q[1] = q[2] = 0;
		q[3] = 1;
		return;
	}
	for (i = 0; i < 4; i++)
		q[i] /= length;
}

static void GL_MD5Slerp (const float oldq[4], const float newq[4],
	float frontlerp, float out[4])
{
	float target[4], dot = 0;
	float oldweight, newweight;
	int i;
	for (i = 0; i < 4; i++)
	{
		target[i] = newq[i];
		dot += oldq[i] * target[i];
	}
	if (dot < 0)
	{
		for (i = 0; i < 4; i++)
			target[i] = -target[i];
		dot = -dot;
	}
	if (dot > 0.9995f)
	{
		oldweight = 1.0f - frontlerp;
		newweight = frontlerp;
	}
	else
	{
		float theta, denominator;
		if (dot > 1.0f)
			dot = 1.0f;
		theta = (float)acos (dot);
		denominator = (float)sin (theta);
		oldweight = (float)sin ((1.0f - frontlerp) * theta) / denominator;
		newweight = (float)sin (frontlerp * theta) / denominator;
	}
	for (i = 0; i < 4; i++)
		out[i] = oldq[i] * oldweight + target[i] * newweight;
	GL_MD5NormalizeQuat (out);
}

static void GL_MD5Rotate (const float q[4], const float v[3], vec3_t out)
{
	vec3_t cross, cross2;
	cross[0] = q[1] * v[2] - q[2] * v[1];
	cross[1] = q[2] * v[0] - q[0] * v[2];
	cross[2] = q[0] * v[1] - q[1] * v[0];
	cross2[0] = q[1] * cross[2] - q[2] * cross[1];
	cross2[1] = q[2] * cross[0] - q[0] * cross[2];
	cross2[2] = q[0] * cross[1] - q[1] * cross[0];
	out[0] = v[0] + 2.0f * (q[3] * cross[0] + cross2[0]);
	out[1] = v[1] + 2.0f * (q[3] * cross[1] + cross2[1]);
	out[2] = v[2] + 2.0f * (q[3] * cross[2] + cross2[2]);
}

static void GL_MD5BuildPose (const mmd5model_t *md5, int frame,
	int oldframe, float backlerp)
{
	const mmd5joint_t *newpose = &md5->frames[(frame % md5->numframes) * md5->numjoints];
	const mmd5joint_t *oldpose = &md5->frames[(oldframe % md5->numframes) * md5->numjoints];
	float frontlerp = 1.0f - backlerp;
	int i, j;

	if (backlerp == 0 || frame % md5->numframes == oldframe % md5->numframes)
	{
		memcpy (md5_pose, newpose, md5->numjoints * sizeof (md5_pose[0]));
		return;
	}
	for (i = 0; i < md5->numjoints; i++)
	{
		for (j = 0; j < 3; j++)
			md5_pose[i].pos[j] = oldpose[i].pos[j] * backlerp +
				newpose[i].pos[j] * frontlerp;
		md5_pose[i].scale = newpose[i].scale;
		GL_MD5Slerp (oldpose[i].orient, newpose[i].orient,
			frontlerp, md5_pose[i].orient);
	}
}

static void GL_MD5BuildPoseMeasured (const mmd5model_t *md5, int frame,
	int oldframe, float backlerp)
{
#ifdef MD5_CPU_TIMING
	md5_perf_tick_t start;
	qboolean timed = MD5_PerfStart (&start);
	double elapsed;
#endif
	GL_MD5BuildPose (md5, frame, oldframe, backlerp);
#ifdef MD5_CPU_TIMING
	elapsed = MD5_PerfStop (&start, timed);
	if (elapsed >= 0)
		md5_perf_frame.pose_ms += elapsed;
#endif
}

static qboolean GL_MD5BuildMesh (const mmd5model_t *md5,
	const mmd5mesh_t *mesh, const vec3_t move, float alpha, qboolean shell)
{
	int i, j, k;
	qboolean need_normal = shell || !gl_dotshading ||
		FLOAT_NE_ZERO (gl_dotshading->value);

	if (mesh->numverts < 1 || mesh->numverts > MD5_MAX_VERTICES ||
		mesh->numweights < 1 || mesh->numweights > MD5_MAX_WEIGHTS ||
		!mesh->vertices || !mesh->weights)
		return false;
	for (i = 0; i < mesh->numverts; i++)
	{
		const mmd5vertex_t *vertex = &mesh->vertices[i];
		vec3_t point, normal, rotated, rotated_normal;
		float light;

		if (!vertex->count || (unsigned)vertex->start + vertex->count > (unsigned)mesh->numweights)
			return false;
		VectorCopy (move, point);
		if (need_normal)
			VectorClear (normal);
		for (j = 0; j < vertex->count; j++)
		{
			const mmd5weight_t *weight = &mesh->weights[vertex->start + j];
			const mmd5joint_t *joint;
			if (weight->joint >= md5->numjoints)
				return false;
			joint = &md5_pose[weight->joint];
			GL_MD5Rotate (joint->orient, weight->pos, rotated);
			if (need_normal)
				GL_MD5Rotate (joint->orient, vertex->normal, rotated_normal);
			for (k = 0; k < 3; k++)
			{
				point[k] += weight->bias *
					(joint->pos[k] + joint->scale * rotated[k]);
				if (need_normal)
					normal[k] += weight->bias * rotated_normal[k];
			}
		}
		if (shell)
		{
			VectorNormalize (normal);
			for (k = 0; k < 3; k++)
				point[k] += normal[k] * POWERSUIT_SCALE;
		}
		light = shell || !need_normal ? 1.0f : GL_MD3Shade (normal);
		for (k = 0; k < 3; k++)
		{
			md5_vertices[i][k] = point[k];
			md5_vertices[i][5 + k] = shadelight[k] * light;
		}
		md5_vertices[i][3] = vertex->st[0];
		md5_vertices[i][4] = vertex->st[1];
		md5_vertices[i][8] = alpha;
	}
	return true;
}

static qboolean GL_MD5BuildMeshMeasured (const mmd5model_t *md5,
	const mmd5mesh_t *mesh, const vec3_t move, float alpha,
	qboolean shell, qboolean shadow)
{
	qboolean built;
#ifdef MD5_CPU_TIMING
	md5_perf_tick_t start;
	qboolean timed = MD5_PerfStart (&start);
	double elapsed;
#else
	(void)shadow;
#endif
	built = GL_MD5BuildMesh (md5, mesh, move, alpha, shell);
#ifdef MD5_CPU_TIMING
	elapsed = MD5_PerfStop (&start, timed);
	if (elapsed >= 0)
	{
		md5_perf_frame.skin_ms += elapsed;
		if (shadow)
			md5_perf_frame.shadow_ms += elapsed;
	}
	if (built && md5_perf_pass_active && !md5_perf_frame.bad_clock)
	{
		md5_perf_frame.builds++;
		md5_perf_frame.shadow_builds += shadow ? 1 : 0;
		md5_perf_frame.vertices += mesh->numverts;
		md5_perf_frame.weights += mesh->numweights;
	}
#endif
	return built;
}

static void GL_MD5Submit (const mmd5mesh_t *mesh, qboolean textured)
{
	GLint saved_array = 0, saved_elements = 0, saved_client_tmu = GL_TEXTURE0;
	qboolean arrays = GL_MD3ArraySupport ();
	int i;

	if (arrays)
	{
		qglPushClientAttrib (GL_CLIENT_VERTEX_ARRAY_BIT);
		if (qglClientActiveTextureARB)
		{
			qglGetIntegerv (GL_CLIENT_ACTIVE_TEXTURE, &saved_client_tmu);
			qglClientActiveTextureARB (GL_TEXTURE1);
			qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
			qglClientActiveTextureARB (GL_TEXTURE0);
		}
		if (qglBindBuffer)
		{
			qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_array);
			qglGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &saved_elements);
			qglBindBuffer (GL_ARRAY_BUFFER, 0);
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		qglDisableClientState (GL_NORMAL_ARRAY);
		qglDisableClientState (GL_INDEX_ARRAY);
		qglDisableClientState (GL_EDGE_FLAG_ARRAY);
		qglEnableClientState (GL_VERTEX_ARRAY);
		qglVertexPointer (3, GL_FLOAT, sizeof (md5_vertices[0]), md5_vertices[0]);
		if (textured)
		{
			qglEnableClientState (GL_TEXTURE_COORD_ARRAY);
			qglTexCoordPointer (2, GL_FLOAT, sizeof (md5_vertices[0]),
				&md5_vertices[0][3]);
		}
		else
			qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
		qglEnableClientState (GL_COLOR_ARRAY);
		qglColorPointer (4, GL_FLOAT, sizeof (md5_vertices[0]),
			&md5_vertices[0][5]);
		qglDrawElements (GL_TRIANGLES, mesh->numtris * 3,
			GL_UNSIGNED_SHORT, mesh->indices);
		qglPopClientAttrib ();
		if (qglBindBuffer)
		{
			qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, (GLuint)saved_elements);
		}
		if (qglClientActiveTextureARB)
			qglClientActiveTextureARB ((GLenum)saved_client_tmu);
		c_alias_batches++;
		c_alias_vertices += mesh->numverts;
		c_alias_indices += mesh->numtris * 3;
	}
	else
	{
		qglBegin (GL_TRIANGLES);
		for (i = 0; i < mesh->numtris * 3; i++)
		{
			unsigned index = mesh->indices[i];
			if (index >= (unsigned)mesh->numverts)
				continue; /* The optional loader validates all indices. */
			if (textured)
				qglTexCoord2fv (&md5_vertices[index][3]);
			qglColor4fv (&md5_vertices[index][5]);
			qglVertex3fv (md5_vertices[index]);
		}
		qglEnd ();
		c_alias_immediate++;
	}
}

static void GL_ShowMD5Tris (const mmd5mesh_t *mesh)
{
	int i;

	if (!GL_ShowTrisBegin (GL_SHOWTRIS_MESH))
		return;
	qglBegin (GL_TRIANGLES);
	for (i = 0; i < mesh->numtris * 3; i++)
	{
		unsigned index = mesh->indices[i];
		if (index < (unsigned)mesh->numverts)
			qglVertex3fv (md5_vertices[index]);
	}
	qglEnd ();
	GL_ShowTrisEnd ();
}

static void GL_MD5DrawCel (const mmd5mesh_t *mesh, float scale)
{
	float width;
	qboolean bloom_shader;
	int i;

	if (scale <= 0)
		return;
	width = gl_celshading->value;
	if (width > 10)
		width = 10;
	qglPushAttrib (GL_CURRENT_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
		GL_POLYGON_BIT | GL_LINE_BIT);
	qglDisable (GL_TEXTURE_2D);
	qglEnable (GL_BLEND);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglEnable (GL_CULL_FACE);
	qglCullFace ((currententity->flags & RF_WEAPONMODEL) &&
				r_lefthand && r_lefthand->value == 1.0f ?
			GL_FRONT :
			GL_BACK);
	qglPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	qglLineWidth (width * scale);
	for (i = 0; i < mesh->numverts; i++)
	{
		md5_vertices[i][5] = md5_vertices[i][6] = md5_vertices[i][7] = 0;
		md5_vertices[i][8] = scale;
	}
	bloom_shader = GL_BeginBloomShader (false, false, false);
	GL_MD5Submit (mesh, false);
	if (bloom_shader)
		GL_EndSurfaceShader ();
	qglPopAttrib ();
	c_alias_cel_draws++;
}

static void GL_MD5DrawShadow (const mmd5model_t *md5, entity_t *e,
	float backlerp, const vec3_t move, float alpha)
{
	vec3_t shadowlight;
	float lheight;
	qboolean bloom_shader;
	int i, j;

	if (!(alpha > 0))
		return;
	/* Mode 2 already traced a current ground hit for the fade. */
	if (!(gl_shadows->value >= 2))
		R_LightPoint (e->origin, shadowlight);
	lheight = e->origin[2] - lightspot[2];
	GL_BeginShadow ((entity_t *)e);
	qglDisable (GL_TEXTURE_2D);
	qglEnable (GL_BLEND);
	qglColor4f (0, 0, 0, alpha);
	bloom_shader = GL_BeginBloomShader (false, false, false);
	for (i = 0; i < md5->nummeshes; i++)
	{
		const mmd5mesh_t *mesh = &md5->meshes[i];
		if (mesh->numverts < 1 || mesh->numverts > MD5_MAX_VERTICES ||
			mesh->numtris < 1 || mesh->numtris > MD5_MAX_TRIANGLES ||
			!mesh->vertices || !mesh->indices || !mesh->weights ||
			!GL_MD5BuildMeshMeasured (md5, mesh, move, alpha,
				false, true))
			continue;
		for (j = 0; j < mesh->numverts; j++)
		{
			if (!(gl_shadows->value >= 2))
			{
				md5_vertices[j][0] -= shadevector[0] * (md5_vertices[j][2] + lheight);
				md5_vertices[j][1] -= shadevector[1] * (md5_vertices[j][2] + lheight);
				md5_vertices[j][2] = -lheight + 1.0f;
			}
			md5_vertices[j][5] = md5_vertices[j][6] = md5_vertices[j][7] = 0;
		}
		GL_MD5Submit (mesh, false);
		c_alias_shadow_batches++;
	}
	if (bloom_shader)
		GL_EndSurfaceShader ();
	GL_EndShadow ();
}

void R_DrawMD5ModelPass (entity_t *e, qboolean translucent)
{
	const mmd5model_t *md5;
	image_t *skin;
	vec3_t delta, vectors[3], move, worldmins, worldmaxs;
	float backlerp, alpha;
	qboolean shell, projection, gpu_pose_ready;
	unsigned saved_tmu;
	int frame, oldframe, i;
#ifdef MD5_CPU_TIMING
	md5_perf_tick_t md5_pass_start;
	qboolean md5_timed;
#endif

	if (!currentmodel || !R_MD5Selected (e) ||
		R_MD5Translucent (e) != translucent)
		return;
	md5 = currentmodel->md5;
	if (!md5 || currentmodel->numframes < 1)
		return;
	frame = e->frame;
	oldframe = e->oldframe;
	if (frame < 0 || frame >= currentmodel->numframes)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "R_DrawMD5Model %s: no such frame %d\n",
			currentmodel->name, frame);
		frame = 0;
	}
	if (oldframe < 0 || oldframe >= currentmodel->numframes)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "R_DrawMD5Model %s: no such oldframe %d\n",
			currentmodel->name, oldframe);
		oldframe = 0;
	}
	if ((e->flags & RF_WEAPONMODEL) && r_lefthand->value == 2)
		return;
	if (!(e->flags & RF_WEAPONMODEL) && FLOAT_NE_ZERO (gl_cull_models->value) &&
		R_MD5WorldBounds (e, worldmins, worldmaxs) &&
		R_CullBox (worldmins, worldmaxs))
	{
		R_DrawCulledModelShadow (e);
		return;
	}
#ifdef MD5_CPU_TIMING
	md5_timed = MD5_PerfBeginPass (&md5_pass_start);
#endif
	GL_MD3SetLighting ();
	skin = GL_MD5Skin (md5, e);
	shell = (e->flags & RF_SHELL_MASK) != 0;
	alpha = (e->flags & RF_TRANSLUCENT) ? e->alpha : 1.0f;
	if (e->flags & RF_WEAPONMODEL)
		alpha *= R_GunAlpha ();
	backlerp = (r_lerpmodels && FLOAT_NE_ZERO (r_lerpmodels->value)) ?
		e->backlerp :
		0;
	if (!(backlerp >= 0))
		backlerp = 0;
	if (backlerp > 1)
		backlerp = 1;
	GL_MD5BuildPoseMeasured (md5, frame, oldframe, backlerp);
	VectorSubtract (e->oldorigin, e->origin, delta);
	AngleVectors (e->angles, vectors[0], vectors[1], vectors[2]);
	move[0] = backlerp * DotProduct (delta, vectors[0]);
	move[1] = -backlerp * DotProduct (delta, vectors[1]);
	move[2] = backlerp * DotProduct (delta, vectors[2]);
	gpu_pose_ready = !shell && GL_BeginMD5GPUPass (md5_pose, md5->numjoints);

	saved_tmu = gl_state.currenttmu;
	GL_SelectTexture (GL_TEXTURE0);
	qglPushAttrib (GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT |
		GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT | GL_LINE_BIT);
	if (e->flags & RF_DEPTHHACK)
		qglDepthRange (gldepthmin, gldepthmin + 0.3 * (gldepthmax - gldepthmin));
	projection = (e->flags & RF_WEAPONMODEL) &&
		(r_lefthand->value == 1.0f || (cl_gunfov && cl_gunfov->value > 0));
	if (projection)
	{
		qglMatrixMode (GL_PROJECTION);
		qglPushMatrix ();
		qglLoadIdentity ();
		if (r_lefthand->value == 1.0f)
			qglScalef (-1, 1, 1);
		MYgluPerspective (R_GunFov (),
			(float)r_newrefdef.width / (r_newrefdef.height ? r_newrefdef.height : 1),
			R_NearClip (), gl_zfar->value);
		qglMatrixMode (GL_MODELVIEW);
		if (r_lefthand->value == 1.0f)
			qglCullFace (GL_BACK);
	}
	qglPushMatrix ();
	e->angles[PITCH] = -e->angles[PITCH];
	R_RotateForEntity (e);
	e->angles[PITCH] = -e->angles[PITCH];
	qglShadeModel (GL_SMOOTH);
	GL_TexEnv (GL_MODULATE);
	if (shell)
		qglDisable (GL_TEXTURE_2D);
	else
	{
		qglEnable (GL_TEXTURE_2D);
		GL_Bind (skin->texnum);
	}
	if (translucent)
		qglEnable (GL_BLEND);
	else
		qglDisable (GL_BLEND);
	for (i = 0; i < md5->nummeshes; i++)
	{
		mmd5mesh_t *mesh = &md5->meshes[i];
		qboolean material_shader = false;
		qboolean gpu_drawn = false;
		float celscale;

		if (mesh->numverts < 1 || mesh->numverts > MD5_MAX_VERTICES ||
			mesh->numtris < 1 || mesh->numtris > MD5_MAX_TRIANGLES ||
			!mesh->vertices || !mesh->indices || !mesh->weights)
			continue;
		if (gpu_pose_ready)
			gpu_drawn = GL_DrawMD5GPU (mesh, move, alpha,
				skin->glowtexnum);
		if (!gpu_drawn && !GL_MD5BuildMeshMeasured (md5, mesh, move, alpha, shell, false))
			continue;
		c_alias_polys += mesh->numtris;
		if (!gpu_drawn)
		{
			if (GL_WeaponDepthPrefill (alpha))
			{
				GL_BeginWeaponDepthPrefill ();
				GL_MD5Submit (mesh, false);
				GL_EndWeaponDepthPrefill ();
			}
			if (!shell && GL_ImageShaderIntensity ())
				material_shader = GL_BeginLitMaterialShader (true, GL_MODULATE,
					0, 0, 1, skin->glowtexnum);
			else if (GL_BloomActive () && (!skin->glowtexnum || shell))
				material_shader = GL_BeginBloomShader (!shell, true, shell);
			else if (!shell && skin->glowtexnum && GL_GlowmapsAvailable ())
				material_shader = GL_BeginMaterialShader (true, true, GL_MODULATE,
					0, 0, 1, skin->glowtexnum);
			else if (GL_BloomActive ())
				material_shader = GL_BeginBloomShader (true, true, false);
			GL_MD5Submit (mesh, !shell);
			if (material_shader)
				GL_EndSurfaceShader ();
		}
		celscale = GL_MD3CelScale (skin, alpha);
		if (gpu_drawn && celscale > 0 &&
			!GL_MD5BuildMeshMeasured (md5, mesh, move, alpha, false, false))
			continue;
		GL_MD5DrawCel (mesh, celscale);
		if (gpu_drawn)
		{
			/* Keep the GPU pose and draw only its outline after the cel pass. */
			if (GL_ShowTrisBegin (GL_SHOWTRIS_MESH))
			{
				GL_DrawMD5GPU (mesh, move, alpha,
					skin->glowtexnum);
				GL_ShowTrisEnd ();
			}
		}
		else
			GL_ShowMD5Tris (mesh);
	}
	GL_EndMD5GPUPass ();
	GL_TexEnv (GL_REPLACE);
	qglShadeModel (GL_FLAT);
	qglPopMatrix ();
	if (projection)
	{
		qglMatrixMode (GL_PROJECTION);
		qglPopMatrix ();
		qglMatrixMode (GL_MODELVIEW);
	}
	if (e->flags & RF_DEPTHHACK)
		qglDepthRange (gldepthmin, gldepthmax);
	if (gl_shadows && FLOAT_NE_ZERO (gl_shadows->value) &&
		!(e->flags & (RF_TRANSLUCENT | RF_WEAPONMODEL)))
		GL_MD5DrawShadow (md5, e, backlerp, move, R_ShadowAlpha (e, backlerp));
	qglPopAttrib ();
	GL_SelectTexture (saved_tmu ? GL_TEXTURE1 : GL_TEXTURE0);
	GL_CheckForError ();
#ifdef MD5_CPU_TIMING
	if (md5_timed)
		MD5_PerfEndPass (&md5_pass_start);
#endif
}

/* The vertical projection depends only on world X/Y. Test the maximum signed
 * distance over that rectangle, not the visibility of the unprojected model. */
static qboolean R_CullProjectedShadow (const double mins[2], const double maxs[2])
{
	const cplane_t *plane = R_GetShadowPlane ();
	double x, y, distance;
	int i;

	if (FLOAT_EQ_ZERO (gl_cull_models->value) || FLOAT_NE_ZERO (r_nocull->value) || !plane)
		return false;
	for (i = 0; i < 2; i++)
		if (!(mins[i] >= -DBL_MAX && maxs[i] <= DBL_MAX && mins[i] <= maxs[i]))
			return false;
	for (i = 0; i < 4; i++)
	{
		x = frustum[i].normal[0] - (double)frustum[i].normal[2] * plane->normal[0] / plane->normal[2];
		y = frustum[i].normal[1] - (double)frustum[i].normal[2] * plane->normal[1] / plane->normal[2];
		distance = x * (x >= 0 ? maxs[0] : mins[0]) +
			y * (y >= 0 ? maxs[1] : mins[1]) +
			(double)frustum[i].normal[2] * plane->dist / plane->normal[2] -
			frustum[i].dist;
		if (distance < 0)
			return true;
	}
	return false;
}

/* A culled model can still cast a visible shadow. Keep this independent of
 * filled-model lighting, draw state and any interpolation left by another entity. */
void R_DrawCulledModelShadow (entity_t *e)
{
	entity_t shadow, *saved_entity;
	model_t *saved_model;
	const mmd5model_t *md5 = NULL;
	const mmd3model_t *md3 = NULL;
	dmdl_t *md2 = NULL;
	daliasframe_t *frame = NULL, *oldframe = NULL;
	vec3_t delta, axes[3], move, frontv, backv, worldmins, worldmaxs;
	vec3_t saved_light, saved_direction, saved_spot;
	double mins[2], maxs[2], radius, move_length;
	float backlerp, frontlerp, alpha;
	unsigned saved_tmu;
	int i, frames;
	qboolean bounds = false;
#ifdef MD5_CPU_TIMING
	md5_perf_tick_t md5_pass_start;
	qboolean md5_timed = false;
#endif

	if (!gl_shadows || !(gl_shadows->value >= 2) || !e || !e->model ||
		(e->flags & (RF_TRANSLUCENT | RF_WEAPONMODEL | RF_BEAM)) ||
		(e->model->type != mod_alias && e->model->type != mod_md3))
		return;
	alias_gpu_lerped = false;
	alias_gpu_mesh = NULL;
	if (R_MD5Selected (e))
	{
		md5 = e->model->md5;
		frames = e->model->numframes;
	}
	else if (e->model->type == mod_md3)
	{
		md3 = e->model->md3;
		if (!md3 || !md3->frames || !md3->meshes)
			return;
		frames = md3->numframes;
	}
	else
	{
		md2 = e->model->extradata;
		if (!md2)
			return;
		frames = md2->num_frames;
	}
	if (frames < 1)
		return;
	shadow = *e;
	if (shadow.frame < 0 || shadow.frame >= frames)
		shadow.frame = 0;
	if (shadow.oldframe < 0 || shadow.oldframe >= frames)
		shadow.oldframe = 0;
	backlerp = (r_lerpmodels && FLOAT_NE_ZERO (r_lerpmodels->value)) ? e->backlerp : 0;
	/* Match each format's filled draw, including legacy MD2 extrapolation. */
	if (!md2)
	{
		if (!(backlerp >= 0))
			backlerp = 0;
		if (backlerp > 1)
			backlerp = 1;
	}
	shadow.backlerp = backlerp;
	frontlerp = 1.0f - backlerp;
	saved_entity = currententity;
	saved_model = currentmodel;
	VectorCopy (shadelight, saved_light);
	VectorCopy (shadevector, saved_direction);
	VectorCopy (lightspot, saved_spot);
	currententity = &shadow;
	currentmodel = shadow.model;
#ifdef MD5_CPU_TIMING
	if (md5)
		md5_timed = MD5_PerfBeginPass (&md5_pass_start);
#endif
	alpha = R_ShadowAlpha (&shadow, backlerp);
	if (!(alpha > 0))
		goto done;

	VectorSubtract (shadow.oldorigin, shadow.origin, delta);
	AngleVectors (shadow.angles, axes[0], axes[1], axes[2]);
	move[0] = DotProduct (delta, axes[0]);
	move[1] = -DotProduct (delta, axes[1]);
	move[2] = DotProduct (delta, axes[2]);
	if (md2)
	{
		frame = (daliasframe_t *)((byte *)md2 + md2->ofs_frames +
			shadow.frame * md2->framesize);
		oldframe = (daliasframe_t *)((byte *)md2 + md2->ofs_frames +
			shadow.oldframe * md2->framesize);
		for (i = 0; i < 3; i++)
		{
			move[i] = backlerp * (move[i] + oldframe->translate[i]) +
				frontlerp * frame->translate[i];
			frontv[i] = frontlerp * frame->scale[i];
			backv[i] = backlerp * oldframe->scale[i];
		}
	}
	else if (md3)
	{
		for (i = 0; i < 3; i++)
			move[i] = backlerp * (move[i] + md3->frames[shadow.oldframe].translate[i]) +
				frontlerp * md3->frames[shadow.frame].translate[i];
	}
	else
	{
		for (i = 0; i < 3; i++)
			move[i] *= backlerp;
		if (R_MD5WorldBounds (&shadow, worldmins, worldmaxs))
		{
			for (i = 0; i < 2; i++)
			{
				mins[i] = (double)worldmins[i] - 1;
				maxs[i] = (double)worldmaxs[i] + 1;
			}
			bounds = true;
		}
	}
	if (!md5 && shadow.frame < currentmodel->numframes &&
		shadow.oldframe < currentmodel->numframes &&
		(frontlerp == 0 || (currentmodel->shadow_radii[shadow.frame] >= 0 && currentmodel->shadow_radii[shadow.frame] <= FLT_MAX)) &&
		(backlerp == 0 || (currentmodel->shadow_radii[shadow.oldframe] >= 0 && currentmodel->shadow_radii[shadow.oldframe] <= FLT_MAX)))
	{
		/* Cached radii exclude frame translation; move contains it and the
		 * interpolated oldorigin offset. Rotation preserves this bound. */
		radius = frontlerp != 0 ? currentmodel->shadow_radii[shadow.frame] * fabs ((double)frontlerp) : 0;
		if (backlerp != 0)
			radius += currentmodel->shadow_radii[shadow.oldframe] * fabs ((double)backlerp);
		move_length = 0;
		for (i = 0; i < 3; i++)
			move_length += (double)move[i] * move[i];
		radius += sqrt (move_length) + 1;
		if (md2 && (shadow.flags & RF_SHELL_MASK))
			radius += POWERSUIT_SCALE;
		if (radius >= 0 && radius <= DBL_MAX)
		{
			for (i = 0; i < 2; i++)
			{
				mins[i] = shadow.origin[i] - radius;
				maxs[i] = shadow.origin[i] + radius;
			}
			bounds = true;
		}
	}
	if (bounds && R_CullProjectedShadow (mins, maxs))
		goto done;

	VectorClear (shadelight);
	VectorClear (shadevector);
	saved_tmu = gl_state.currenttmu;
	GL_SelectTexture (GL_TEXTURE0);
	if (md2)
	{
		/* Seed all interpolation inputs; the GPU path may defer CPU lerping. */
		alias_gpu_lerped = true;
		alias_gpu_mesh = currentmodel->aliasmesh;
		alias_gpu_verts = frame->verts;
		alias_gpu_oldverts = oldframe->verts;
		VectorCopy (move, alias_gpu_move);
		VectorCopy (frontv, alias_gpu_front);
		VectorCopy (backv, alias_gpu_back);
		GL_BeginShadow (&shadow);
		qglDisable (GL_TEXTURE_2D);
		qglEnable (GL_BLEND);
		qglColor4f (0, 0, 0, alpha);
		GL_DrawAliasShadow (md2);
		GL_EndShadow ();
		alias_gpu_lerped = false;
		alias_gpu_mesh = NULL;
	}
	else if (md3)
		GL_MD3DrawShadow (md3, &shadow, backlerp, move, alpha);
	else
	{
		GL_MD5BuildPoseMeasured (md5, shadow.frame, shadow.oldframe, backlerp);
		GL_MD5DrawShadow (md5, &shadow, backlerp, move, alpha);
	}
	GL_SelectTexture (saved_tmu ? GL_TEXTURE1 : GL_TEXTURE0);
done:
#ifdef MD5_CPU_TIMING
	if (md5_timed)
		MD5_PerfEndPass (&md5_pass_start);
#endif
	alias_gpu_lerped = false;
	alias_gpu_mesh = NULL;
	currententity = saved_entity;
	currentmodel = saved_model;
	VectorCopy (saved_light, shadelight);
	VectorCopy (saved_direction, shadevector);
	VectorCopy (saved_spot, lightspot);
}

/* Preserve the original translucent pass: it ignores model occlusion for MD2
 * and MD3. Only cast here when that later pass will not already cast a shadow. */
void R_DrawOccludedModelShadow (entity_t *e)
{
	const mmd3model_t *md3;
	int i;

	if (!e || !e->model || (e->flags & (RF_TRANSLUCENT | RF_WEAPONMODEL | RF_BEAM)))
		return;
	if (!R_MD5Selected (e))
	{
		if (e->model->type == mod_alias && gl_alphaskins &&
			FLOAT_NE_ZERO (gl_alphaskins->value) && e->skin && e->skin->has_alpha)
			return;
		if (e->model->type == mod_md3)
		{
			md3 = e->model->md3;
			if (!md3 || !md3->meshes)
				return;
			for (i = 0; i < md3->nummeshes; i++)
			{
				const mmd3mesh_t *mesh = &md3->meshes[i];
				if (mesh->numverts < 1 || mesh->numverts > MD3_MAX_VERTICES ||
					mesh->numtris < 1 || !mesh->vertices || !mesh->indices || !mesh->st)
					continue;
				if (!GL_MD3MeshTranslucent (mesh, e))
					break;
			}
			if (i == md3->nummeshes)
				return;
		}
	}
	R_DrawCulledModelShadow (e);
}
