/* R1GL fixed-function lava brightness. Original BSP material identity is
 * retained before replacement; the default intensity uses the native path. */
#include "gl_local.h"
#include "gl_jump.h"

static cvar_t *jump_lava_game, *jump_lava_setting;
static msurface_t *jump_lava_surfaces[MAX_MAP_FACES];
static int jump_lava_count;
static float jump_lava_intensity = 1;

qboolean GL_JumpLavaName (const char *name, int length)
{
	int i, j, c;
	static const char lava[] = "lava";
	if (!name || length < 4)
		return false;
	for (i = 0; i + 3 < length && name[i]; i++)
	{
		for (j = 0; j < 4; j++)
		{
			c = (unsigned char)name[i + j];
			if (c >= 'A' && c <= 'Z')
				c += 'a' - 'A';
			if (c != lava[j])
				break;
		}
		if (j == 4)
			return true;
	}
	return false;
}

void GL_JumpLavaReset (void)
{
	jump_lava_game = jump_lava_setting = NULL;
	jump_lava_count = 0;
	jump_lava_intensity = 1;
}

void GL_JumpLavaBegin (void)
{
	float value;
	jump_lava_count = 0;
	jump_lava_intensity = 1;
	if (!jump_lava_game)
		jump_lava_game = ri.Cvar_Get ("game", "", 0);
	if (!jump_lava_setting)
		jump_lava_setting = ri.Cvar_Get ("gl_lava_intensity", "1", 0);
	if (Q_stricmp (jump_lava_game->string, "jump"))
		return;
	value = jump_lava_setting->value;
	if (value != value)
		return;
	if (value < 0.1f)
		value = 0.1f;
	if (value > 5)
		value = 5;
	jump_lava_intensity = value;
}

void GL_JumpLavaSurface (msurface_t *surface)
{
	if (jump_lava_intensity == 1 || jump_lava_count >= MAX_MAP_FACES ||
		!surface || !surface->texinfo || !surface->texinfo->is_lava ||
		(surface->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66)) ||
		(currententity && (currententity->flags & RF_TRANSLUCENT)))
		return;
	/* Material intensity must not recolor an isolated sampled lightmap. */
	if (GL_SurfaceLightmapView (surface))
		return;
	jump_lava_surfaces[jump_lava_count++] = surface;
}

/* Query the actual correction list, including its capacity admission. */
qboolean GL_JumpLavaShowTrisDeferred (const msurface_t *surface)
{
	int i;
	if (!surface || !surface->texinfo || !surface->texinfo->is_lava)
		return false;
	for (i = 0; i < jump_lava_count; i++)
		if (jump_lava_surfaces[i] == surface)
			return true;
	return false;
}

static void GL_JumpLavaGeometry (void)
{
	msurface_t *surface;
	glpoly_t *poly;
	int i, vertex;

	for (i = 0; i < jump_lava_count; i++)
	{
		surface = jump_lava_surfaces[i];
		for (poly = surface->polys; poly; poly = surface->flags & SURF_DRAWTURB ? poly->next : poly->chain)
		{
			qglBegin (GL_TRIANGLE_FAN);
			for (vertex = 0; vertex < poly->numverts; vertex++)
				qglVertex3fv (poly->verts[vertex]);
			qglEnd ();
		}
	}
}

void GL_JumpLavaDraw (void)
{
	float remaining, factor, color;
	qboolean shader;
	msurface_t *surface;
	glpoly_t *poly;
	int i;

	if (!jump_lava_count || jump_lava_intensity == 1)
		return;
	qglPushAttrib (GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
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
	qglDisable (GL_ALPHA_TEST);
	qglDisable (GL_FOG);
	qglDisable (GL_LIGHTING);
	qglEnable (GL_BLEND);
	qglEnable (GL_DEPTH_TEST);
	qglDepthMask (GL_FALSE);
	qglDepthFunc (GL_EQUAL);
	qglColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	shader = GL_BeginSurfaceShader (false, false, 0, 0, 0, 1);
	/* Multiplicative scene correction must not modify the bloom emission target. */
	if (shader && !GL_BloomTargets (false))
	{
		GL_EndSurfaceShader ();
		shader = false;
	}
	remaining = jump_lava_intensity;
	if (remaining < 1)
	{
		qglBlendFunc (GL_DST_COLOR, GL_ZERO);
		qglColor4f (remaining, remaining, remaining, 1);
		GL_JumpLavaGeometry ();
	}
	else
	{
		qglBlendFunc (GL_DST_COLOR, GL_ONE);
		while (remaining > 1)
		{
			factor = remaining > 2 ? 2 : remaining;
			color = factor - 1;
			qglColor4f (color, color, color, 1);
			GL_JumpLavaGeometry ();
			remaining /= factor;
		}
	}
	if (shader)
		GL_EndSurfaceShader ();
	qglPopAttrib ();
	if (GL_ShowTrisEnabled (GL_SHOWTRIS_WORLD))
		for (i = 0; i < jump_lava_count; i++)
		{
			surface = jump_lava_surfaces[i];
			for (poly = surface->polys; poly;
				 poly = surface->flags & SURF_DRAWTURB ? poly->next : poly->chain)
				GL_ShowTrisVertices (GL_SHOWTRIS_WORLD, GL_TRIANGLE_FAN, poly->numverts,
					poly->verts[0], VERTEXSIZE * sizeof (float), 3);
		}
	jump_lava_count = 0;
}
