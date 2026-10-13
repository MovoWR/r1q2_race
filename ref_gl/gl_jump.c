/* World-origin axes adapted from the frozen Q2PRO Race renderer.
 * Original behavior: RGB world axes, visible through walls, no drift mode. */
#include "gl_local.h"
#include "gl_jump.h"
#include <float.h>

static cvar_t *jump_game, *jump_origin_draw, *jump_origin_size, *jump_origin_width;

void GL_JumpReset (void)
{
	GL_JumpResetBeams ();
	GL_JumpLavaReset ();
	jump_game = jump_origin_draw = jump_origin_size = jump_origin_width = NULL;
}

void GL_JumpOriginRegister (void)
{
	if (jump_game)
		return;
	jump_game = ri.Cvar_Get ("game", "", 0);
	jump_origin_draw = ri.Cvar_Get ("cl_drawworldorigin", "0", 0);
	jump_origin_size = ri.Cvar_Get ("cl_worldorigin_size", "4096", 0);
	jump_origin_width = ri.Cvar_Get ("cl_worldorigin_linewidth", "1", 0);
}

void GL_JumpDrawOrigin (void)
{
	float size, width;
	int axis;
	vec3_t point;

	GL_JumpOriginRegister ();
	if (Q_stricmp (jump_game->string, "jump") || !jump_origin_draw->intvalue ||
		(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		return;
	size = jump_origin_size->value;
	if (!(size > 0 && size <= FLT_MAX))
		return;
	width = jump_origin_width->value;
	if (!(width >= 1))
		width = 1;
	if (width > 10)
		width = 10;
	qglPushAttrib (GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT |
		GL_LINE_BIT | GL_DEPTH_BUFFER_BIT | GL_TRANSFORM_BIT);
	qglMatrixMode (GL_MODELVIEW);
	qglPushMatrix ();
	qglLoadMatrixf (r_world_matrix);
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
	qglDisable (GL_TEXTURE_2D);
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_BLEND);
	qglDisable (GL_ALPHA_TEST);
	qglDepthMask (GL_FALSE);
	qglLineWidth (width);
	qglBegin (GL_LINES);
	for (axis = 0; axis < 3; axis++)
	{
		qglColor4f (axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f,
			axis == 2 ? 1.0f : 0.0f, 1);
		VectorClear (point);
		point[axis] = -size;
		qglVertex3fv (point);
		point[axis] = size;
		qglVertex3fv (point);
	}
	qglEnd ();
	qglPopMatrix ();
	qglPopAttrib ();
}
