/* Q2PRO Race beam geometry and procedural texture, adapted to R1GL.
 * Frozen donors: 5571a17dbb7fb03915e7b403b1f981b7006a1a05 (Race) and
 * 601a8df8433b0c50dbbe37c0716c3793fff140a7 (ordinary beams),
 * src/refresh/tess.c and texture.c. Copyright (C) 2005-2008 Andrey Nazarov.
 * GNU GPL version 2 or later; see COPYING. */
#include "gl_local.h"
#include "gl_jump.h"
#include <float.h>

static const void *jump_beam_entities;
static refbeam_t jump_beams[MAX_ENTITIES];
static int jump_beam_count;
static GLuint jump_beam_texture;
static cvar_t *jump_beam_style, *jump_beam_game;

#define JUMP_BEAM_VERTICES 48
static float jump_beam_vertices[MAX_ENTITIES * JUMP_BEAM_VERTICES][9];
static int jump_beam_vertex_count, jump_beam_batch_style;

static int GL_BeamStyle (void)
{
	if (!jump_beam_style)
		jump_beam_style = ri.Cvar_Get ("gl_beamstyle", "0", 0);
	return jump_beam_style->intvalue;
}

void EXPORT GL_JumpSetBeams (const void *entities, const refbeam_t *beams, int count)
{
	/* The array and its metadata must always describe the same scene. */
	jump_beam_vertex_count = 0;
	jump_beam_entities = NULL;
	jump_beam_count = 0;
	if (!entities || !beams || count <= 0 || count > MAX_ENTITIES)
		return;
	memcpy (jump_beams, beams, count * sizeof (*beams));
	jump_beam_entities = entities;
	jump_beam_count = count;
}

void GL_JumpResetBeams (void)
{
	jump_beam_vertex_count = 0;
	if (jump_beam_texture && qglDeleteTextures)
		qglDeleteTextures (1, &jump_beam_texture);
	jump_beam_texture = 0;
	jump_beam_style = jump_beam_game = NULL;
	GL_JumpSetBeams (NULL, NULL, 0);
}

static qboolean GL_JumpBeamTexture (void)
{
	byte pixels[16 * 16 * 4];
	float alpha;
	int i, j, index;
	GLint bound = 0;
	GLuint texture = JUMP_BEAM_TEXTURE;

	if (!jump_beam_texture)
	{
		if (!qglIsTexture || !qglGetIntegerv || !qglGetError ||
			!GL_ClearErrors () || qglIsTexture (texture) ||
			qglGetError () != GL_NO_ERROR)
			return false;
		qglBindTexture (GL_TEXTURE_2D, texture);
		qglGetIntegerv (GL_TEXTURE_BINDING_2D, &bound);
		if (qglGetError () != GL_NO_ERROR || (GLuint)bound != texture)
			goto failed;
		for (i = 0; i < 16; i++)
			for (j = 0; j < 16; j++)
			{
				alpha = 1.0f - (abs (j - 8) - 0.5f) / 5.5f;
				if (alpha < 0)
					alpha = 0;
				if (alpha > 1)
					alpha = 1;
				index = (i * 16 + j) * 4;
				pixels[index] = pixels[index + 1] = pixels[index + 2] = 255;
				pixels[index + 3] = (byte)(255 * alpha);
			}
		qglTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		if (qglGetError () != GL_NO_ERROR)
			goto failed;
		jump_beam_texture = texture;
	}
	else
		qglBindTexture (GL_TEXTURE_2D, jump_beam_texture);
	qglTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	return true;
failed:
	qglDeleteTextures (1, &texture);
	return false;
}

static qboolean GL_JumpBeamInfo (entity_t *entity, int *index, int *style)
{
	size_t address, base, offset;

	if (!entity || !jump_beam_count ||
		jump_beam_entities != r_newrefdef.entities ||
		jump_beam_count > r_newrefdef.num_entities)
		return false;
	/* Only elements of the published refdef array may use the parallel metadata. */
	address = (size_t)entity;
	base = (size_t)r_newrefdef.entities;
	if (address < base)
		return false;
	offset = address - base;
	if (offset % sizeof (*entity) || offset / sizeof (*entity) >= (size_t)jump_beam_count)
		return false;
	*index = (int)(offset / sizeof (*entity));
	if (!jump_beams[*index].active)
		return false;
	if (!jump_beam_game)
		jump_beam_game = ri.Cvar_Get ("game", "", 0);
	if (Q_stricmp (jump_beam_game->string, "jump"))
		return false;
	*style = GL_BeamStyle ();
	if (*style < 0 || *style > 4)
		*style = 0;
	return true;
}

static void GL_JumpBeamVertex (float dst[9], const vec3_t point,
	float s, float t, const float rgba[4])
{
	int i;
	for (i = 0; i < 3; i++)
		dst[i] = point[i];
	dst[3] = s;
	dst[4] = t;
	for (i = 0; i < 4; i++)
		dst[i + 5] = rgba[i];
}

static void GL_JumpBeamPoint (float dst[9], const vec3_t point,
	const vec3_t offset, float scale, float s, float t, const float rgba[4])
{
	vec3_t vertex;
	VectorMA (point, scale, offset, vertex);
	GL_JumpBeamVertex (dst, vertex, s, t, rgba);
}

/* The direct and queued paths submit these identical four-vertex quads. */
static int GL_JumpBuildBeam (const entity_t *entity, unsigned int color,
	int style, float vertices[][9])
{
	vec3_t direction, right, view, points[12], vertex;
	float rgba[4], width, distance, factor, startwidth, endwidth;
	int i, next, axis, count = 0;

	width = (float)abs ((short)entity->frame) * (style == 1 ? 0.5f : 1.2f);
	if (!(width > 0) || !(entity->alpha > 0 && entity->alpha <= FLT_MAX))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (!(fabs (entity->origin[axis]) <= FLT_MAX) ||
			!(fabs (entity->oldorigin[axis]) <= FLT_MAX))
			return 0;
	VectorSubtract (entity->oldorigin, entity->origin, direction);
	if (style == 1)
	{
		distance = VectorNormalize (direction);
		if (!(distance >= 0.1f && distance <= FLT_MAX))
			return 0;
		PerpendicularVector (right, direction);
		VectorScale (right, width, right);
		for (i = 0; i < 12; i++)
			RotatePointAroundVector (points[i], direction, right, i * 30.0f);
	}
	else
	{
		VectorSubtract (r_newrefdef.vieworg, entity->origin, view);
		CrossProduct (direction, view, right);
		distance = VectorNormalize (right);
		if (!(distance >= 0.1f && distance <= FLT_MAX))
			return 0;
	}
	for (i = 0; i < 4; i++)
		rgba[i] = ((color >> (i * 8)) & 255) / 255.0f;
	/* The frozen renderer quantizes the faded alpha to a byte. */
	rgba[3] = (byte)(((color >> 24) & 255) *
				  (entity->alpha > 1 ? 1 : entity->alpha)) /
		255.0f;
	if (style == 2 || style == 4)
	{
		VectorSubtract (r_newrefdef.vieworg, entity->origin, view);
		distance = VectorLength (view);
		factor = (distance - 128.0f) / 896.0f;
		if (!(factor >= 0))
			factor = 0;
		if (factor > 1)
			factor = 1;
		rgba[0] = (byte)(255 * factor) / 255.0f;
		rgba[1] = (byte)(255 * (1 - factor)) / 255.0f;
		rgba[2] = 0;
	}
	if (style == 1)
	{
		for (i = 0; i < 12; i++)
		{
			next = (i + 1) % 12;
			VectorAdd (entity->origin, points[i], vertex);
			GL_JumpBeamVertex (vertices[count++], vertex, 0, 0, rgba);
			VectorAdd (entity->oldorigin, points[i], vertex);
			GL_JumpBeamVertex (vertices[count++], vertex, 0, 0, rgba);
			VectorAdd (entity->oldorigin, points[next], vertex);
			GL_JumpBeamVertex (vertices[count++], vertex, 0, 0, rgba);
			VectorAdd (entity->origin, points[next], vertex);
			GL_JumpBeamVertex (vertices[count++], vertex, 0, 0, rgba);
		}
	}
	else
	{
		startwidth = style == 3 || style == 4 ? 0 : width;
		endwidth = style == 3 || style == 4 ? width * 2 : width;
		GL_JumpBeamPoint (vertices[count++], entity->origin, right,
			startwidth, 0, 0, rgba);
		GL_JumpBeamPoint (vertices[count++], entity->origin, right,
			-startwidth, 1, 0, rgba);
		GL_JumpBeamPoint (vertices[count++], entity->oldorigin, right,
			-endwidth, 1, 1, rgba);
		GL_JumpBeamPoint (vertices[count++], entity->oldorigin, right,
			endwidth, 0, 1, rgba);
	}
	return count;
}

static qboolean GL_JumpBeamArraySupport (void)
{
	return gl_vertex_arrays && FLOAT_NE_ZERO (gl_vertex_arrays->value) &&
		qglDrawArrays && qglVertexPointer && qglTexCoordPointer &&
		qglColorPointer && qglEnableClientState && qglDisableClientState &&
		qglPushClientAttrib && qglPopClientAttrib &&
		(!(qglBindBuffer || qglClientActiveTextureARB) || qglGetIntegerv);
}

static void GL_JumpSubmitBeams (const float vertices[][9], int count,
	int style, qboolean allow_arrays)
{
	GLint saved_array = 0, saved_elements = 0, saved_client_tmu = GL_TEXTURE0;
	unsigned saved_tmu;
	qboolean arrays, bloom_shader;
	int i;

	if (count <= 0)
		return;
	arrays = allow_arrays && GL_JumpBeamArraySupport ();
	saved_tmu = gl_state.currenttmu;
	qglPushAttrib (GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT |
		GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
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
	qglDisable (GL_ALPHA_TEST);
	qglDisable (GL_CULL_FACE);
	qglEnable (GL_BLEND);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask (GL_FALSE);
	if (style)
		qglDisable (GL_TEXTURE_2D);
	else
	{
		qglEnable (GL_TEXTURE_2D);
		if (!GL_JumpBeamTexture ())
		{
			style = 1;
			qglDisable (GL_TEXTURE_2D);
		}
	}
	qglColor4fv (&vertices[0][5]);
	bloom_shader = GL_BeginBloomShader (!style, true, true);
	if (arrays)
	{
		if (qglClientActiveTextureARB)
			qglGetIntegerv (GL_CLIENT_ACTIVE_TEXTURE, &saved_client_tmu);
		qglPushClientAttrib (GL_CLIENT_VERTEX_ARRAY_BIT);
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
		if (qglClientActiveTextureARB)
		{
			qglClientActiveTextureARB (GL_TEXTURE1);
			qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
			qglClientActiveTextureARB (GL_TEXTURE0);
		}
		qglEnableClientState (GL_VERTEX_ARRAY);
		qglVertexPointer (3, GL_FLOAT, sizeof (vertices[0]), vertices[0]);
		qglEnableClientState (GL_COLOR_ARRAY);
		qglColorPointer (4, GL_FLOAT, sizeof (vertices[0]), &vertices[0][5]);
		if (!style)
		{
			qglEnableClientState (GL_TEXTURE_COORD_ARRAY);
			qglTexCoordPointer (2, GL_FLOAT, sizeof (vertices[0]), &vertices[0][3]);
		}
		else
			qglDisableClientState (GL_TEXTURE_COORD_ARRAY);
		qglDrawArrays (GL_QUADS, 0, count);
		GL_ShowTrisArrays (GL_SHOWTRIS_FX, GL_QUADS, 0, count);
		qglPopClientAttrib ();
		if (qglBindBuffer)
		{
			qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, (GLuint)saved_elements);
		}
		if (qglClientActiveTextureARB)
			qglClientActiveTextureARB ((GLenum)saved_client_tmu);
	}
	else
	{
		qglBegin (GL_QUADS);
		for (i = 0; i < count; i++)
		{
			/* All vertices of one beam share a colour. */
			if (i % (style == 1 ? JUMP_BEAM_VERTICES : 4) == 0)
				qglColor4fv (&vertices[i][5]);
			if (!style)
				qglTexCoord2f (vertices[i][3], vertices[i][4]);
			qglVertex3fv (vertices[i]);
		}
		qglEnd ();
		GL_ShowTrisVertices (GL_SHOWTRIS_FX, GL_QUADS, count,
			vertices[0], sizeof (vertices[0]), 3);
	}
	if (bloom_shader)
		GL_EndSurfaceShader ();
	qglPopAttrib ();
	if (qglSelectTextureSGIS)
		qglSelectTextureSGIS (saved_tmu ? GL_TEXTURE1 : GL_TEXTURE0);
	else if (qglActiveTextureARB)
		qglActiveTextureARB (saved_tmu ? GL_TEXTURE1 : GL_TEXTURE0);
}

void GL_JumpFlushBeams (void)
{
	if (!jump_beam_vertex_count)
		return;
	GL_JumpSubmitBeams (jump_beam_vertices, jump_beam_vertex_count,
		jump_beam_batch_style, true);
	jump_beam_vertex_count = 0;
}

static void GL_BeamQueueBuilt (const float vertices[][9], int count, int style)
{
	if (!count)
		return;
	if (jump_beam_vertex_count &&
		(jump_beam_batch_style != style ||
			jump_beam_vertex_count + count > MAX_ENTITIES * JUMP_BEAM_VERTICES))
		GL_JumpFlushBeams ();
	memcpy (&jump_beam_vertices[jump_beam_vertex_count], vertices,
		count * sizeof (vertices[0]));
	jump_beam_vertex_count += count;
	jump_beam_batch_style = style;
}

qboolean GL_JumpQueueBeam (entity_t *entity)
{
	float vertices[JUMP_BEAM_VERTICES][9];
	int index, style, count;

	if (!GL_JumpBeamInfo (entity, &index, &style))
		return false;
	count = GL_JumpBuildBeam (entity, jump_beams[index].rgba, style, vertices);
	GL_BeamQueueBuilt (vertices, count, style);
	return true;
}

/* An ordinary palette beam uses the same Q2PRO shapes, but not Jump's
 * heatmap/arrow styles or its parallel per-line colour metadata. */
static int GL_NativeBuildBeam (entity_t *entity, int style,
	float vertices[][9])
{
	unsigned int color = d_8to24table[entity->skinnum & 255] | 0xff000000u;
	return GL_JumpBuildBeam (entity, color, style ? 1 : 0, vertices);
}

qboolean GL_BeamQueueEntity (entity_t *entity)
{
	float vertices[JUMP_BEAM_VERTICES][9];
	int style, count;

	if (!entity || !(entity->flags & RF_BEAM))
		return false;
	if (GL_JumpQueueBeam (entity))
		return true;
	style = GL_BeamStyle ();
	if (style == -1)
		return false; /* Explicit legacy six-sided beam. */
	style = style ? 1 : 0;
	count = GL_NativeBuildBeam (entity, style, vertices);
	GL_BeamQueueBuilt (vertices, count, style);
	return true;
}

qboolean GL_NativeDrawBeam (entity_t *entity)
{
	float vertices[JUMP_BEAM_VERTICES][9];
	int index, style, count;

	if (!entity || !(entity->flags & RF_BEAM) ||
		GL_JumpBeamInfo (entity, &index, &style))
		return false;
	style = GL_BeamStyle ();
	if (style == -1)
		return false;
	GL_JumpFlushBeams ();
	style = style ? 1 : 0;
	count = GL_NativeBuildBeam (entity, style, vertices);
	if (count)
		GL_JumpSubmitBeams (vertices, count, style, false);
	return true;
}

qboolean GL_JumpDrawBeam (entity_t *entity)
{
	float vertices[JUMP_BEAM_VERTICES][9];
	int index, style, count;

	/* A direct draw may follow queued beams inside the same translucent pass. */
	GL_JumpFlushBeams ();
	if (!GL_JumpBeamInfo (entity, &index, &style))
		return false;
	count = GL_JumpBuildBeam (entity, jump_beams[index].rgba, style, vertices);
	if (count)
		GL_JumpSubmitBeams (vertices, count, style, false);
	return true;
}
