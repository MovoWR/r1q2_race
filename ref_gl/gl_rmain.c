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
// r_main.c
#include "gl_local.h"
#include "gl_accounting.h"
#include "gl_jump.h"
#include <float.h>
#ifndef _WIN32
#include <time.h>
#endif
#ifdef _WIN32
void GLimp_InitSwapControl (void);
#endif

#ifndef WIN32
#define __stdcall
#endif

//long (*Q_ftol)(float f);

void R_Clear (void);

viddef_t	vid;

refimport_t		ri;
refimportnew_t	rx;

unsigned int GL_TEXTURE0, GL_TEXTURE1;

model_t		*r_worldmodel;

double		gldepthmin, gldepthmax;

double		vid_scaled_width, vid_scaled_height;

glconfig_t gl_config;
glstate_t  gl_state;

image_t		*r_notexture;		// use for bad textures
image_t		*r_particletexture;	// little dot for particles

entity_t	*currententity;
model_t		*currentmodel;

cplane_t	frustum[4];

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

int			c_brush_polys, c_alias_polys;

float		v_blend[4];			// final blending color

void GL_Strings_f( void );

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float	r_world_matrix[16];
float	r_base_world_matrix[16];

//
// screen size info
//
refdef_t	r_newrefdef;

int		r_viewcluster, r_viewcluster2, r_oldviewcluster, r_oldviewcluster2;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
static cvar_t *gl_draworder;
cvar_t	*r_drawworld;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t *gl_cull_nodes;
cvar_t *gl_cull_models;
cvar_t	*r_lerpmodels;
cvar_t	*r_lefthand;

#ifdef R1Q2_GL_CORE
static cvar_t *gl_profile;
#endif

cvar_t	*r_lightlevel;	// FIXME: This is a HACK to get the client's light level

//cvar_t	*gl_nosubimage;
cvar_t	*gl_allow_software;

cvar_t	*gl_vertex_arrays;
cvar_t *gl_batch_models;
cvar_t *gl_batch_2d;
cvar_t *cl_gunfov, *cl_gunalpha, *r_adjustgunfov;
cvar_t *gl_md5_load, *gl_md5_use, *gl_md5_distance;

cvar_t	*gl_particle_min_size;
cvar_t	*gl_particle_max_size;
cvar_t	*gl_particle_size;
cvar_t	*gl_particle_att_a;
cvar_t	*gl_particle_att_b;
cvar_t	*gl_particle_att_c;
cvar_t *gl_batch_particles;
cvar_t *gl_partscale;
cvar_t *gl_partstyle;
cvar_t *gl_partshape;

#define PARTICLE_BATCH_COUNT 1024
#define PARTICLE_TRIANGLE_SIZE (1.0f + 0.7071067811865475f)
#define PARTICLE_TRIANGLE_SCALE (0.5f / PARTICLE_TRIANGLE_SIZE)
static float particle_batch_vertices[PARTICLE_BATCH_COUNT * 3][9];
static unsigned c_particle_array_draws, c_particle_immediate_draws, c_particle_vertices;

//cvar_t	*gl_ext_swapinterval;
cvar_t	*gl_ext_palettedtexture;
cvar_t	*gl_ext_multitexture;
cvar_t	*gl_ext_pointparameters;
//cvar_t	*gl_ext_compiled_vertex_array;

//r1ch: my extensions
//cvar_t	*gl_ext_generate_mipmap;
cvar_t	*gl_ext_point_sprite;
cvar_t	*gl_ext_texture_filter_anisotropic;
cvar_t	*gl_ext_texture_non_power_of_two;
cvar_t	*gl_ext_max_anisotropy;
cvar_t	*gl_ext_nv_multisample_filter_hint;
cvar_t	*gl_ext_occlusion_query;

cvar_t	*gl_colorbits;
cvar_t	*gl_alphabits;
cvar_t	*gl_depthbits;
cvar_t	*gl_stencilbits;

cvar_t	*gl_ext_multisample;
cvar_t	*gl_ext_samples;

cvar_t	*gl_zfar;
static cvar_t *gl_znear;
cvar_t	*gl_hudscale;

cvar_t	*cl_version;
cvar_t	*gl_r1gl_test;
cvar_t	*gl_doublelight_entities;
cvar_t	*gl_noscrap;
cvar_t	*gl_overbrights;
cvar_t	*gl_linear_mipmaps;

cvar_t	*vid_gamma_pics;

cvar_t *vid_modelist;
cvar_t *vid_mode;
cvar_t *vid_geometry;
cvar_t	*gl_forcewidth;
cvar_t	*gl_forceheight;

cvar_t	*vid_topmost;

//cvar_t	*gl_log;
cvar_t	*gl_bitdepth;
cvar_t	*gl_drawbuffer;
cvar_t  *gl_driver;
cvar_t *gl_lightmap;
cvar_t	*gl_shadows;
cvar_t	*gl_mode;
cvar_t	*gl_dynamic;
cvar_t *gl_lightgrid;
cvar_t *gl_batch_world;
cvar_t *gl_vbo;
//cvar_t  *gl_monolightmap;
cvar_t	*gl_modulate;
cvar_t *gl_modulate_world, *gl_modulate_entities, *gl_brightness;
cvar_t *gl_vertexlight;
cvar_t	*gl_nobind;
cvar_t	*gl_round_down;
cvar_t	*gl_picmip;
cvar_t	*gl_skymip;
cvar_t	*gl_showtris;
static cvar_t *gl_showorigins, *gl_showtearing, *gl_showerrors;
static cvar_t *gl_debug;
cvar_t	*gl_ztrick;
cvar_t	*gl_finish;
cvar_t	*gl_flush;
cvar_t	*gl_clear;
static cvar_t *gl_clearcolor;
static float r_clearcolor[4] = { 0, 0, 0, 1 };
static qboolean r_clearcolor_dirty = true;
cvar_t	*gl_cull;
cvar_t	*gl_polyblend;
cvar_t	*gl_flashblend;
//cvar_t	*gl_playermip;
//cvar_t  *gl_saturatelighting;
cvar_t	*gl_swapinterval;
cvar_t	*gl_texturemode;
cvar_t	*gl_texturealphamode;
cvar_t	*gl_texturesolidmode;
cvar_t	*gl_lockpvs;
cvar_t	*gl_jpg_quality;
cvar_t *gl_screenshot_format;
cvar_t *gl_screenshot_quality;
cvar_t *gl_screenshot_compression;
cvar_t *gl_screenshot_template;
cvar_t *gl_screenshot_async;
cvar_t	*gl_coloredlightmaps;

//cvar_t	*gl_3dlabs_broken;

#if defined WIN32 && defined REF_HARD_LINKED
extern cvar_t *vid_fullscreen, *vid_gamma, *vid_ref;
#else
cvar_t	*vid_fullscreen;
cvar_t	*vid_gamma;
cvar_t	*vid_ref;
#endif
cvar_t *vid_borderless;
cvar_t	*vid_forcedrefresh;
cvar_t	*vid_optimalrefresh;
cvar_t	*vid_nowgl;
cvar_t	*vid_restore_on_switch;

cvar_t	*gl_texture_formats;
cvar_t	*gl_pic_formats;
cvar_t *r_override_textures;
cvar_t *r_texture_formats;
cvar_t *r_texture_overrides;

cvar_t	*gl_dlight_falloff;
cvar_t	*gl_alphaskins;
cvar_t	*gl_defertext;

cvar_t	*gl_pic_scale;

//cvar_t	*con_alpha;

vec4_t	colorWhite = {1,1,1,1};

qboolean load_png_pics = true;
qboolean load_tga_pics = true;
qboolean load_jpg_pics = true;

qboolean load_png_wals = true;
qboolean load_tga_wals = true;
qboolean load_jpg_wals = true;

extern cvar_t		*gl_contrast;

/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullBox (vec3_t mins, vec3_t maxs)
{
	int		i;

	if (FLOAT_NE_ZERO(r_nocull->value))
		return false;

	for (i=0 ; i<4 ; i++)
		if (BOX_ON_PLANE_SIDE(mins, maxs, &frustum[i]) == 2)
			return true;
	return false;
}


void R_RotateForEntity (entity_t *e)
{
    qglTranslatef (e->origin[0],  e->origin[1],  e->origin[2]);

    qglRotatef (e->angles[1],  0, 0, 1);
    qglRotatef (-e->angles[0],  0, 1, 0);
    qglRotatef (-e->angles[2],  1, 0, 0);
}

/*
=============================================================

  SPRITE MODELS

=============================================================
*/

/*
=================
R_DrawSpriteModel

=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	qboolean bloom_shader, blend, alpha_test;
	float alpha = 1.0F;
	vec3_t	point;
	dsprframe_t	*frame;
	float		*up, *right;
	dsprite_t		*psprite;
	image_t *skin;
	int frame_index;

	// don't even bother culling, because it's just a single
	// polygon without a surface cache

	if (!currentmodel || !currentmodel->extradata)
		return;
	psprite = (dsprite_t *)currentmodel->extradata;
	if (psprite->numframes <= 0 || psprite->numframes > MAX_MD2SKINS)
		return;
	frame_index = e->frame < 0 ? 0 : e->frame % psprite->numframes;
	frame = &psprite->frames[frame_index];
	skin = currentmodel->skins[frame_index];
	if (!skin)
		skin = r_notexture;
	if (!skin)
		return;

#if 0
	if (psprite->type == SPR_ORIENTED)
	{	// bullet marks on walls
	vec3_t		v_forward, v_right, v_up;

	AngleVectors (currententity->angles, v_forward, v_right, v_up);
		up = v_up;
		right = v_right;
	}
	else
#endif
	{	// normal sprite
		up = vup;
		right = vright;
	}

	if ( e->flags & RF_TRANSLUCENT )
		alpha = e->alpha;
	blend = alpha != 1.0F || (skin->has_alpha && !skin->paletted);
	alpha_test = alpha == 1.0F && skin->has_alpha && skin->paletted;

	if (blend)
		qglEnable( GL_BLEND );
	else
		qglDisable (GL_BLEND);

	qglColor4f( 1, 1, 1, alpha );

	GL_Bind (skin->texnum);

	GL_TexEnv( GL_MODULATE );

	if (alpha_test)
		qglEnable (GL_ALPHA_TEST);
	else
		qglDisable( GL_ALPHA_TEST );

	bloom_shader = GL_BeginBloomShader (true, true, false);
	qglBegin (GL_QUADS);

	qglTexCoord2f (0, 1);
	VectorMA (e->origin, -frame->origin_y, up, point);
	VectorMA (point, -frame->origin_x, right, point);
	qglVertex3fv (point);

	qglTexCoord2f (0, 0);
	VectorMA (e->origin, frame->height - frame->origin_y, up, point);
	VectorMA (point, -frame->origin_x, right, point);
	qglVertex3fv (point);

	qglTexCoord2f (1, 0);
	VectorMA (e->origin, frame->height - frame->origin_y, up, point);
	VectorMA (point, frame->width - frame->origin_x, right, point);
	qglVertex3fv (point);

	qglTexCoord2f (1, 1);
	VectorMA (e->origin, -frame->origin_y, up, point);
	VectorMA (point, frame->width - frame->origin_x, right, point);
	qglVertex3fv (point);
	
	qglEnd ();
	if (bloom_shader)
		GL_EndSurfaceShader ();

	qglDisable (GL_ALPHA_TEST);
	GL_TexEnv( GL_REPLACE );

	if (blend)
		qglDisable( GL_BLEND );

	qglColor4fv(colorWhite);
}

//==================================================================================

/*
=============
R_DrawNullModel
=============
*/
void R_DrawNullModel (void)
{
	qboolean bloom_shader;
	vec3_t	shadelight;
	int		i;

	if ( currententity->flags & RF_FULLBRIGHT )
		shadelight[0] = shadelight[1] = shadelight[2] = 1.0F;
	else
		R_LightPoint (currententity->origin, shadelight);

    qglPushMatrix ();
	R_RotateForEntity (currententity);

	qglDisable (GL_TEXTURE_2D);
	qglColor3fv (shadelight);

	bloom_shader = GL_BeginBloomShader (false, false, false);
	qglBegin (GL_TRIANGLE_FAN);
	qglVertex3f (0, 0, -16);
	for (i=0 ; i<=4 ; i++)
		qglVertex3f (16*(float)cos(i*M_PI_DIV_2), 16*(float)sin(i*M_PI_DIV_2), 0);
	qglEnd ();

	qglBegin (GL_TRIANGLE_FAN);
	qglVertex3f (0, 0, 16);
	for (i=4 ; i>=0 ; i--)
		qglVertex3f (16*(float)cos(i*M_PI_DIV_2), 16*(float)sin(i*M_PI_DIV_2), 0);
	qglEnd ();
	if (GL_ShowTrisBegin (GL_SHOWTRIS_MESH))
	{
		qglBegin (GL_TRIANGLE_FAN);
		qglVertex3f (0, 0, -16);
		for (i = 0; i <= 4; i++)
			qglVertex3f (16 * (float)cos (i * M_PI_DIV_2), 16 * (float)sin (i * M_PI_DIV_2), 0);
		qglEnd ();

		qglBegin (GL_TRIANGLE_FAN);
		qglVertex3f (0, 0, 16);
		for (i = 4; i >= 0; i--)
			qglVertex3f (16 * (float)cos (i * M_PI_DIV_2), 16 * (float)sin (i * M_PI_DIV_2), 0);
		qglEnd ();
		GL_ShowTrisEnd ();
	}

	qglColor3f (1,1,1);
	if (bloom_shader)
		GL_EndSurfaceShader ();
	qglPopMatrix ();
	qglEnable (GL_TEXTURE_2D);
}

/* Q2PRO 601a8df: red/green/blue 16-unit model axes, excluding weapons/BSP/beams. */
static void R_DrawModelOrigins (void)
{
	entity_t *entity;
	qboolean bloom_shader;
	int i;

	if (!gl_showorigins->intvalue || FLOAT_EQ_ZERO (r_drawentities->value))
		return;

	qglPushAttrib (GL_ENABLE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT);
	qglDisable (GL_TEXTURE_2D);
	qglDisable (GL_BLEND);
	qglDisable (GL_ALPHA_TEST);
	qglEnable (GL_DEPTH_TEST);
	qglDepthMask (1);
	for (i = 0; i < r_newrefdef.num_entities; i++)
	{
		entity = &r_newrefdef.entities[i];
		if ((entity->flags & (RF_BEAM | RF_WEAPONMODEL)) ||
			(entity->model && entity->model->type == mod_brush))
			continue;
		qglPushMatrix ();
		qglTranslatef (entity->origin[0], entity->origin[1], entity->origin[2]);
		/* Match AngleVectors' forward/left/up axes, without legacy mesh corrections. */
		qglRotatef (entity->angles[YAW], 0, 0, 1);
		qglRotatef (entity->angles[PITCH], 0, 1, 0);
		qglRotatef (entity->angles[ROLL], 1, 0, 0);
		bloom_shader = GL_BeginBloomShader (false, false, false);
		qglBegin (GL_LINES);
		qglColor3f (1, 0, 0);
		qglVertex3f (0, 0, 0);
		qglVertex3f (16, 0, 0);
		qglColor3f (0, 1, 0);
		qglVertex3f (0, 0, 0);
		qglVertex3f (0, 16, 0);
		qglColor3f (0, 0, 1);
		qglVertex3f (0, 0, 0);
		qglVertex3f (0, 0, 16);
		qglEnd ();
		if (bloom_shader)
			GL_EndSurfaceShader ();
		qglPopMatrix ();
	}
	qglPopAttrib ();
}

int visibleBits[MAX_ENTITIES];


void R_Occlusion_Results (void)
{
	int		i, visible;
	entity_t	*ent;
	//int		numOccluded = 0;

	// now we read back
	for (i = 0; i < r_newrefdef.num_entities; i++)
	{
		int	available;
		vec3_t md5mins, md5maxs;

		ent = &r_newrefdef.entities[i];

		if (!ent->model || ent->model->type == mod_brush ||
			(!(ent->flags & RF_BEAM) &&
				(ent->model->type == mod_sprite || ent->model->type == mod_empty)))
		{
			visibleBits[i] = 500;
			continue;
		}
		if (!(ent->flags & RF_BEAM) && R_MD5Selected (ent) &&
			((ent->flags & RF_WEAPONMODEL) ||
				!R_MD5WorldBounds (ent, md5mins, md5maxs)))
		{
			/* No query was submitted for this replacement. */
			visibleBits[i] = -1;
			continue;
		}
		if (visibleBits[i] == -1)
		{
			/* First query after a skipped entity has no prior result. */
			visibleBits[i] = 1;
			continue;
		}

		if (visibleBits[i] > 1)
		{
			visibleBits[i]--;
			continue;
		}

		qglGetQueryObjectivARB (gl_config.r1gl_Queries[i], GL_QUERY_RESULT_AVAILABLE_ARB, &available);
		if (!available)
		{
			if (gl_ext_occlusion_query->value == 2.0f)
				i--;
			else
				visibleBits[i] = 25;

			continue;
		}

		// get the object and store it in the occlusion bits for the ent
		qglGetQueryObjectivARB (gl_config.r1gl_Queries[i], GL_QUERY_RESULT, &visible);

		if (!visible)
		{
			//ri.Con_Printf (PRINT_ALL, "Occluded %d, %s\n", i, ent->model->name);
			visibleBits[i] = 0;
		}
		else
			visibleBits[i] = 25;
	}
}

void R_Occlusion_Run (void)
{
	int		i;
	entity_t	*ent;
	qboolean md5_selected;
	float	mins[3];
	float	maxs[3];

	static const byte boxindexes[] =
	{
	0, 1, 2, 3,
	4, 5, 1, 0,
	3, 2, 6, 7,
	5, 4, 7, 6,
	1, 5, 6, 2,
	4, 0, 3, 7
	};

	float	boxverts[24];

	if (!r_newrefdef.num_entities)
		return;

	// disable texturing
	qglDisable (GL_TEXTURE_2D);

	// because we don;t know the orientation of the bbox in advance...
	qglDisable (GL_CULL_FACE);

	// disable framebuffer and depthbuffer writes
	qglColorMask (GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	qglDepthMask (GL_FALSE);

	qglEnableClientState (GL_VERTEX_ARRAY);
	qglVertexPointer (3, GL_FLOAT, 0, boxverts);

	for (i = 0; i < r_newrefdef.num_entities; i++)
	{
		ent = &r_newrefdef.entities[i];

		if (!ent->model || ent->model->type == mod_brush ||
			(!(ent->flags & RF_BEAM) &&
				(ent->model->type == mod_sprite || ent->model->type == mod_empty)))
			continue;

		md5_selected = !(ent->flags & RF_BEAM) && R_MD5Selected (ent);
		if (md5_selected)
		{
			if ((ent->flags & RF_WEAPONMODEL) ||
				!R_MD5WorldBounds (ent, mins, maxs))
			{
				visibleBits[i] = -1;
				continue;
			}
		}

		if (visibleBits[i] > 1)
			continue;

		if (!md5_selected)
		{
			// get mins and maxs points
			VectorAdd (ent->origin, ent->model->mins, mins);
			VectorAdd (ent->origin, ent->model->maxs, maxs);
		}

		// CPU grunt to the rescue!!!
		boxverts[0] = boxverts[9] = boxverts[12] = boxverts[21] = mins[0];
		boxverts[3] = boxverts[6] = boxverts[15] = boxverts[18] = maxs[0];
		boxverts[1] = boxverts[4] = boxverts[13] = boxverts[16] = maxs[1];
		boxverts[7] = boxverts[10] = boxverts[19] = boxverts[22] = mins[1];
		boxverts[2] = boxverts[5] = boxverts[8] = boxverts[11] = maxs[2];
		boxverts[14] = boxverts[17] = boxverts[20] = boxverts[23] = mins[2];

		// begin the occlusion query
		qglBeginQueryARB (GL_SAMPLES_PASSED, gl_config.r1gl_Queries[i]);

		// draw as indexed varray
		qglDrawElements (GL_QUADS, 24, GL_UNSIGNED_BYTE, boxindexes);

		// end the query
		// don't read back immediately so that we give the query time to be ready
		qglEndQueryARB (GL_SAMPLES_PASSED);
	}

	qglDisableClientState (GL_VERTEX_ARRAY);

	// restore basic state
	qglEnable (GL_TEXTURE_2D);
	qglEnable (GL_CULL_FACE);

	// enable framebuffer and depthbuffer writes
	qglColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	qglDepthMask (GL_TRUE);

	// some implementations don't reset the primary colour properly after restoring the colormask
	qglColor4f  (1, 1, 1, 1);
}

/* A no-world view has no final world light sample. Retain the last model
 * lightlevel write in the original two-pass, forward entity order. */
static struct
{
	qboolean active, seen;
	int pass, index, best_pass, best_index;
	float value;
} model_lightlevel;

void R_SetModelLightLevel (float value)
{
	r_lightlevel->value = value;
	if (model_lightlevel.active &&
		(!model_lightlevel.seen || model_lightlevel.pass > model_lightlevel.best_pass ||
			(model_lightlevel.pass == model_lightlevel.best_pass &&
				model_lightlevel.index >= model_lightlevel.best_index)))
	{
		model_lightlevel.seen = true;
		model_lightlevel.best_pass = model_lightlevel.pass;
		model_lightlevel.best_index = model_lightlevel.index;
		model_lightlevel.value = value;
	}
}

static void R_BeginModelLightLevel (void)
{
	model_lightlevel.active = (r_newrefdef.rdflags & RDF_NOWORLDMODEL) != 0;
	model_lightlevel.seen = false;
	model_lightlevel.pass = model_lightlevel.index = -1;
}

static void R_ModelLightLevelEntity (int pass, int index)
{
	if (!model_lightlevel.active)
		return;
	model_lightlevel.pass = pass;
	model_lightlevel.index = index;
}

static void R_EndModelLightLevel (void)
{
	if (model_lightlevel.active && model_lightlevel.seen)
		r_lightlevel->value = model_lightlevel.value;
	model_lightlevel.active = false;
}

static qboolean R_EntityGunTranslucent (const entity_t *e)
{
	return (e->flags & RF_WEAPONMODEL) && cl_gunalpha &&
		cl_gunalpha->value >= 0 && cl_gunalpha->value < 1;
}

static qboolean R_EntityLegacyTranslucent (const entity_t *e)
{
	return (e->flags & RF_TRANSLUCENT) ||
		(gl_alphaskins && FLOAT_NE_ZERO (gl_alphaskins->value) &&
			e->skin && e->skin->has_alpha) ||
		R_EntityGunTranslucent (e);
}

static qboolean R_EntityInFront (const entity_t *e)
{
	/* Material-only translucency has no entity alpha in the R1Q2 refdef. */
	float alpha = (e->flags & RF_TRANSLUCENT) ? e->alpha : 1.0f;
	return (e->flags & RF_WEAPONMODEL) ||
		alpha <= (gl_draworder ? gl_draworder->value : 1.0f);
}

static void R_DrawTranslucentEntities (qboolean front);

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList (void)
{
	int		i;
	qboolean md5_selected, sprite_model, brush_translucent;

	if (FLOAT_EQ_ZERO (r_drawentities->value) || r_newrefdef.num_entities <= 0)
		return;

	if (gl_config.r1gl_QueryBits)
		R_Occlusion_Results ();

	// Inline brush faces must be submitted before the shared alpha-surface pass.
	for (i = r_newrefdef.num_entities - 1; i >= 0; i--)
	{
		currententity = &r_newrefdef.entities[i];
		if ((currententity->flags & RF_BEAM) || !currententity->model ||
			currententity->model->type != mod_brush)
			continue;
		brush_translucent = R_EntityLegacyTranslucent (currententity);
		if (gl_config.r1gl_QueryBits && !visibleBits[i] &&
			!brush_translucent)
			continue;
		currentmodel = currententity->model;
		R_ModelLightLevelEntity (brush_translucent ? 1 : 0, i);
		if (brush_translucent)
			qglDepthMask (0);
		R_DrawBrushModel (currententity);
		if (brush_translucent)
			qglDepthMask (1);
	}

	// Opaque models and non-RF sprites use the donor's reverse bucket order.
	for (i = r_newrefdef.num_entities - 1; i >= 0; i--)
	{
		currententity = &r_newrefdef.entities[i];
		if ((currententity->flags & RF_BEAM) ||
			(currententity->model && (currententity->model->type == mod_brush || currententity->model->type == mod_empty)))
			continue;
		R_ModelLightLevelEntity (0, i);
		sprite_model = currententity->model &&
			currententity->model->type == mod_sprite;
		if (sprite_model)
		{
			if (currententity->flags & RF_TRANSLUCENT)
				continue;
			currentmodel = currententity->model;
			qglDepthMask (0);
			R_DrawSpriteModel (currententity);
			qglDepthMask (1);
			continue;
		}
		if (gl_config.r1gl_QueryBits && !visibleBits[i])
		{
			if (gl_shadows && gl_shadows->value >= 2 && gl_shadows->value <= FLT_MAX)
				R_DrawOccludedModelShadow (currententity);
			continue;
		}

		md5_selected = R_MD5Selected (currententity);
		if (md5_selected)
		{
			if (R_MD5Translucent (currententity))
				continue;
		}
		else if (currententity->model && currententity->model->type == mod_md3)
		{
			if ((currententity->flags & RF_TRANSLUCENT) ||
				R_EntityGunTranslucent (currententity))
				continue;
		}
		else if (R_EntityLegacyTranslucent (currententity))
			continue;

		currentmodel = currententity->model;
		if (!currentmodel)
		{
			R_DrawNullModel ();
			continue;
		}
		if (md5_selected)
		{
			R_DrawMD5ModelPass (currententity, false);
			continue;
		}
		switch (currentmodel->type)
		{
		case mod_alias:
			R_DrawAliasModel (currententity);
			break;
		case mod_md3:
			R_DrawMD3ModelPass (currententity, false);
			break;
		case mod_sprite:
			R_DrawSpriteModel (currententity);
			break;
		default:
			ri.Sys_Error (ERR_DROP, "Bad modeltype %d on %s", currentmodel->type, currentmodel->name);
			break;
		}
	}

	R_DrawTranslucentEntities (false);
}

static void R_DrawTranslucentEntities (qboolean front)
{
	int i;
	qboolean md5_selected, sprite_model;

	if (FLOAT_EQ_ZERO (r_drawentities->value) || r_newrefdef.num_entities <= 0)
		return;
	qglDepthMask (0);
	for (i = r_newrefdef.num_entities - 1; i >= 0; i--)
	{
		currententity = &r_newrefdef.entities[i];
		if ((currententity->flags & RF_BEAM) ||
			(currententity->model && (currententity->model->type == mod_brush || currententity->model->type == mod_empty)))
			continue;
		if (R_EntityInFront (currententity) != front)
			continue;
		sprite_model = currententity->model &&
			currententity->model->type == mod_sprite;
		if (sprite_model)
		{
			if (!(currententity->flags & RF_TRANSLUCENT))
				continue;
		}
		md5_selected = R_MD5Selected (currententity);
		if (md5_selected)
		{
			if (gl_config.r1gl_QueryBits && !visibleBits[i])
				continue;
			if (!R_MD5Translucent (currententity))
				continue;
		}
		else if (currententity->model && currententity->model->type == mod_md3)
		{
			if (!((currententity->flags & RF_TRANSLUCENT) ||
					(FLOAT_NE_ZERO (gl_alphaskins->value) && R_MD3HasAlphaSkin (currententity)) ||
					R_EntityGunTranslucent (currententity)))
				continue;
		}
		else if (!sprite_model && !R_EntityLegacyTranslucent (currententity))
			continue;
		R_ModelLightLevelEntity (1, i);
		currentmodel = currententity->model;
		if (!currentmodel)
		{
			R_DrawNullModel ();
			continue;
		}
		if (md5_selected)
		{
			R_DrawMD5ModelPass (currententity, true);
			continue;
		}
		switch (currentmodel->type)
		{
		case mod_alias:
			R_DrawAliasModel (currententity);
			break;
		case mod_md3:
			R_DrawMD3ModelPass (currententity, true);
			break;
		case mod_sprite:
			R_DrawSpriteModel (currententity);
			break;
		default:
			ri.Sys_Error (ERR_DROP, "Bad modeltype %d on %s", currentmodel->type, currentmodel->name);
			break;
		}
	}
	qglDepthMask (1);
}

static void R_DrawBeamsOnList (void)
{
	int i;
	qboolean old_translucent;

	if (FLOAT_EQ_ZERO (r_drawentities->value) || r_newrefdef.num_entities <= 0)
		return;
	qglDepthMask (0);
	for (i = r_newrefdef.num_entities - 1; i >= 0; i--)
	{
		currententity = &r_newrefdef.entities[i];
		if (!(currententity->flags & RF_BEAM) || !currententity->frame)
			continue;
		old_translucent = R_EntityLegacyTranslucent (currententity);
		if (!old_translucent && gl_config.r1gl_QueryBits && !visibleBits[i])
			continue;
		if (GL_BeamQueueEntity (currententity))
			continue;
		GL_JumpFlushBeams ();
		R_DrawBeam (currententity);
	}
	GL_JumpFlushBeams ();
	qglDepthMask (1);
}

/* Q2PRO-style centered particle triangles, using R1Q2's palette and texture. */
static void GL_BuildParticleTriangle (const particle_t *p, float partscale,
	float vertices[3][9])
{
	vec3_t delta;
	float distance, scale, corner;
	const float *color = d_8to24float[p->color & 255];
	int i, j;

	VectorSubtract (p->origin, r_newrefdef.vieworg, delta);
	distance = DotProduct (delta, vpn);
	scale = (distance > 20 ? 1.0f + distance * 0.004f : 1.0f) * partscale;
	corner = scale * PARTICLE_TRIANGLE_SCALE;
	for (i = 0; i < 3; i++)
	{
		vertices[0][i] = p->origin[i] + (vright[i] - vup[i]) * corner;
		vertices[1][i] = vertices[0][i] + vup[i] * scale;
		vertices[2][i] = vertices[0][i] - vright[i] * scale;
	}
	vertices[0][3] = 0;
	vertices[0][4] = 0;
	vertices[1][3] = 0;
	vertices[1][4] = PARTICLE_TRIANGLE_SIZE;
	vertices[2][3] = PARTICLE_TRIANGLE_SIZE;
	vertices[2][4] = 0;
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
			vertices[i][5 + j] = color[j];
		vertices[i][8] = p->alpha;
	}
}

static qboolean GL_ParticleArraySupport (void)
{
	return gl_vertex_arrays && FLOAT_NE_ZERO (gl_vertex_arrays->value) &&
		qglDrawArrays && qglVertexPointer && qglTexCoordPointer &&
		qglColorPointer && qglEnableClientState && qglDisableClientState &&
		qglPushClientAttrib && qglPopClientAttrib &&
		(!qglBindBuffer || qglGetIntegerv);
}

static void GL_DrawModernParticles (int num_particles, const particle_t particles[])
{
	GLint saved_array = 0, saved_elements = 0, saved_texenv = GL_REPLACE;
	unsigned saved_tmu, saved_texture;
	qboolean arrays, bloom_shader, outlines;
	float partscale, style, vertices[3][9];
	int first, count, i, j;

	if (num_particles <= 0 || !particles)
		return;
	partscale = gl_partscale ? gl_partscale->value : 2.0f;
	if (!(partscale >= -FLT_MAX && partscale <= FLT_MAX))
		partscale = 2.0f;
	if (partscale < 0.25f)
		partscale = 0.25f;
	if (partscale > 8.0f)
		partscale = 8.0f;
	style = gl_partstyle ? gl_partstyle->value : 0;
	arrays = GL_ParticleArraySupport ();

	saved_tmu = gl_state.currenttmu;
	GL_SelectTexture (GL_TEXTURE0);
	saved_texture = gl_state.currenttextures[0];
	if (qglGetTexEnviv)
		qglGetTexEnviv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &saved_texenv);
	qglPushAttrib (GL_CURRENT_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
		GL_DEPTH_BUFFER_BIT);
	GL_Bind (R_ParticleTexture ()->texnum);
	qglDepthMask (GL_FALSE);
	qglEnable (GL_BLEND);
	qglBlendFunc (GL_SRC_ALPHA,
		style != 0 && style >= -FLT_MAX && style <= FLT_MAX ?
			GL_ONE :
			GL_ONE_MINUS_SRC_ALPHA);
	GL_TexEnv (GL_MODULATE);
	bloom_shader = GL_BeginBloomShader (true, true, true);

	if (arrays)
	{
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
		qglVertexPointer (3, GL_FLOAT, sizeof (particle_batch_vertices[0]),
			particle_batch_vertices[0]);
		qglEnableClientState (GL_TEXTURE_COORD_ARRAY);
		qglTexCoordPointer (2, GL_FLOAT, sizeof (particle_batch_vertices[0]),
			&particle_batch_vertices[0][3]);
		qglEnableClientState (GL_COLOR_ARRAY);
		qglColorPointer (4, GL_FLOAT, sizeof (particle_batch_vertices[0]),
			&particle_batch_vertices[0][5]);
		for (first = 0; first < num_particles; first += count)
		{
			count = num_particles - first;
			if (count > PARTICLE_BATCH_COUNT)
				count = PARTICLE_BATCH_COUNT;
			for (i = 0; i < count; i++)
				GL_BuildParticleTriangle (&particles[first + i], partscale,
					&particle_batch_vertices[i * 3]);
			qglDrawArrays (GL_TRIANGLES, 0, count * 3);
			GL_ShowTrisArrays (GL_SHOWTRIS_FX, GL_TRIANGLES, 0, count * 3);
			c_particle_array_draws++;
			c_particle_vertices += count * 3;
		}
		qglPopClientAttrib ();
		if (qglBindBuffer)
		{
			qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
			qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, (GLuint)saved_elements);
		}
	}
	else
	{
		outlines = false;
		do
		{
			qglBegin (GL_TRIANGLES);
			for (i = 0; i < num_particles; i++)
			{
				GL_BuildParticleTriangle (&particles[i], partscale, vertices);
				for (j = 0; j < 3; j++)
				{
					if (!outlines)
						qglColor4fv (&vertices[j][5]);
					qglTexCoord2f (vertices[j][3], vertices[j][4]);
					qglVertex3fv (vertices[j]);
				}
			}
			qglEnd ();
			if (outlines)
			{
				GL_ShowTrisEnd ();
				break;
			}
			outlines = GL_ShowTrisBegin (GL_SHOWTRIS_FX);
		} while (outlines);
		c_particle_immediate_draws++;
		c_particle_vertices += num_particles * 3;
	}
	if (bloom_shader)
		GL_EndSurfaceShader ();
	GL_TexEnv ((GLenum)saved_texenv);
	GL_Bind (saved_texture);
	qglPopAttrib ();
	GL_SelectTexture (saved_tmu ? GL_TEXTURE1 : GL_TEXTURE0);
	GL_CheckForError ();
}

/*
** GL_DrawParticles
**
*/
void GL_DrawParticles( int num_particles, const particle_t particles[])
{
	image_t *texture;
	qboolean bloom_shader, outlines = false;
	GLint wrap_s, wrap_t;
	const particle_t *p;
	int				i;
	vec3_t			up, right;
	float			scale;
	//byte			color[4];
	vec4_t			colorf;
	if (num_particles <= 0 || !particles)
		return;

	texture = R_ParticleTexture ();
	GL_Bind (texture->texnum);
	/* Legacy corner UVs repeat; generated centered masks keep their clamp. */
	qglGetTexParameteriv (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrap_s);
	qglGetTexParameteriv (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrap_t);
	if (!texture->particle_shape)
	{
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}
	qglDepthMask( GL_FALSE );		// no z buffering
	qglEnable( GL_BLEND );
	GL_TexEnv( GL_MODULATE );
	bloom_shader = GL_BeginBloomShader (true, true, true);
	do
	{
		qglBegin (GL_TRIANGLES);

		VectorScale (vup, 1.5f, up);
		VectorScale (vright, 1.5f, right);

		for (p = particles, i = 0; i < num_particles; i++, p++)
		{
			if (texture->particle_shape)
			{
				float vertices[3][9];
				int j;

				GL_BuildParticleTriangle (p, 1.5f, vertices);
				for (j = 0; j < 3; j++)
				{
					if (!outlines)
						qglColor4fv (&vertices[j][5]);
					qglTexCoord2f (vertices[j][3], vertices[j][4]);
					qglVertex3fv (vertices[j]);
				}
				continue;
			}
			// hack a scale up to keep particles from disapearing
			scale = (p->origin[0] - r_origin[0]) * vpn[0] +
				(p->origin[1] - r_origin[1]) * vpn[1] +
				(p->origin[2] - r_origin[2]) * vpn[2];

			if (scale < 20)
				scale = 1;
			else
				scale = 1 + scale * 0.004f;

			//*(int *)color = colortable[p->color];
			//color[3] = (byte)Q_ftol(p->alpha*255);

			FastVectorCopy (d_8to24float[p->color], colorf);
			colorf[3] = p->alpha;

			if (!outlines)
				qglColor4fv (colorf);

			qglTexCoord2f (0.0625f, 0.0625f);
			qglVertex3fv (p->origin);

			qglTexCoord2f (1.0625f, 0.0625f);
			qglVertex3f (p->origin[0] + up[0] * scale,
				p->origin[1] + up[1] * scale,
				p->origin[2] + up[2] * scale);

			qglTexCoord2f (0.0625f, 1.0625f);
			qglVertex3f (p->origin[0] + right[0] * scale,
				p->origin[1] + right[1] * scale,
				p->origin[2] + right[2] * scale);
		}

		qglEnd ();
		if (outlines)
		{
			GL_ShowTrisEnd ();
			break;
		}
		outlines = GL_ShowTrisBegin (GL_SHOWTRIS_FX);
	} while (outlines);
	c_particle_immediate_draws++;
	c_particle_vertices += num_particles * 3;
	if (bloom_shader)
		GL_EndSurfaceShader ();
	qglDisable( GL_BLEND );
	qglColor4fv(colorWhite);
	qglDepthMask( 1 );		// back to normal Z buffering
	GL_TexEnv( GL_REPLACE );
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
}

/*
===============
R_DrawParticles
===============
*/
void R_DrawParticles (void)
{
	qboolean point_shader, additive;
	float style;
	if (r_newrefdef.num_particles <= 0 || !r_newrefdef.particles)
		return;
	if (gl_batch_particles && FLOAT_NE_ZERO (gl_batch_particles->value) &&
		qglPushAttrib && qglPopAttrib)
	{
		GL_DrawModernParticles (r_newrefdef.num_particles, r_newrefdef.particles);
		return;
	}
	style = gl_partstyle ? gl_partstyle->value : 0;
	additive = style != 0 && style >= -FLT_MAX && style <= FLT_MAX &&
		qglPushAttrib && qglPopAttrib;
	if (gl_config.r1gl_GL_ARB_point_sprite && FLOAT_NE_ZERO(gl_ext_point_sprite->value))
	{
		const float quadratic[] =  { 1.0f, 0.0f, 0.0005f };

		GL_Bind (R_ParticleTexture ()->texnum);

		GL_TexEnv( GL_MODULATE );
		qglDepthMask( GL_FALSE );
		//qglDisable( GL_TEXTURE_2D );

		qglEnable( GL_BLEND );
		if (additive)
			qglPushAttrib (GL_COLOR_BUFFER_BIT);
		qglBlendFunc (GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);

		qglPointParameterfvARB( GL_POINT_DISTANCE_ATTENUATION_ARB, quadratic );

		qglPointSize( gl_particle_size->value );

		// The alpha of a point is calculated to allow the fading of points 
		// instead of shrinking them past a defined threshold size. The threshold 
		// is defined by GL_POINT_FADE_THRESHOLD_SIZE_ARB and is not clamped to 
		// the minimum and maximum point sizes.
		qglPointParameterfARB( GL_POINT_FADE_THRESHOLD_SIZE_ARB, gl_particle_max_size->value );

		qglPointParameterfARB( GL_POINT_SIZE_MIN_ARB, gl_particle_min_size->value );
		qglPointParameterfARB( GL_POINT_SIZE_MAX_ARB, gl_particle_max_size->value );

		// Specify point sprite texture coordinate replacement mode for each texture unit
		qglTexEnvf( GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE );

		//
		// Render point sprites...
		//

		qglEnable( GL_POINT_SPRITE_ARB );
		point_shader = GL_BeginPointBloomShader (true);
		qglBegin( GL_POINTS );
		{
			const particle_t *p;
			int i;
			//unsigned char color[4];
			vec4_t	colorf;

			for ( i = 0, p = r_newrefdef.particles; i < r_newrefdef.num_particles; i++, p++ )
			{
				//*(int *)color = d_8to24table[p->color];
				//color[3] = (byte)Q_ftol(p->alpha*255);

				//qglColor4ubv( color );
				
				FastVectorCopy (d_8to24float[p->color], colorf);
				colorf[3] = p->alpha;
				qglColor4fv( colorf );
				
				qglVertex3fv( p->origin );
			}
		}
		qglEnd();
		if (point_shader)
			GL_EndPointBloomShader ();
		if (additive)
			qglPopAttrib ();
		c_particle_immediate_draws++;
		c_particle_vertices += r_newrefdef.num_particles;

		qglDisable( GL_POINT_SPRITE_ARB );
		qglDisable( GL_BLEND );
		qglColor4fv(colorWhite);
		qglDepthMask( GL_TRUE );
		qglEnable( GL_TEXTURE_2D );
		qglDepthMask( 1 );		// back to normal Z buffering
		GL_TexEnv( GL_REPLACE );
	}
	else if ( qglPointParameterfEXT && FLOAT_NE_ZERO(gl_ext_pointparameters->value))
	{
		int i;
		vec4_t			colorf;
		//unsigned char color[4];
		const particle_t *p;

		qglDepthMask( GL_FALSE );
		qglEnable( GL_BLEND );
		if (additive)
		{
			qglPushAttrib (GL_COLOR_BUFFER_BIT);
			qglBlendFunc (GL_SRC_ALPHA, GL_ONE);
		}
		qglDisable( GL_TEXTURE_2D );

		qglPointSize( gl_particle_size->value );

		point_shader = GL_BeginPointBloomShader (false);
		qglBegin( GL_POINTS );

		for ( i = 0, p = r_newrefdef.particles; i < r_newrefdef.num_particles; i++, p++ )
		{
			//*(vec4_t **)&colorf = *(vec4_t *)&d_8to24float[p->color];
			//memcpy (colorf, d_8to24float[p->color], sizeof(colorf));
			FastVectorCopy (d_8to24float[p->color], colorf);
			colorf[3] = p->alpha;

			//*(int *)color = d_8to24table[p->color];
			//color[3] = (byte)Q_ftol (p->alpha * 255);
			//qglColor4ubv( color );
			qglColor4fv( colorf );
			qglVertex3fv( p->origin );
		}

		qglEnd();
		if (point_shader)
			GL_EndPointBloomShader ();
		if (additive)
			qglPopAttrib ();
		c_particle_immediate_draws++;
		c_particle_vertices += r_newrefdef.num_particles;

		qglDisable( GL_BLEND );
		qglColor4fv(colorWhite);
		qglDepthMask( GL_TRUE );
		qglEnable( GL_TEXTURE_2D );

	}
	else
	{
		GL_DrawParticles( r_newrefdef.num_particles, r_newrefdef.particles );
	}
}

/*
============
R_PolyBlend
============
*/
void R_PolyBlend (void)
{
	qboolean shader;

	if (FLOAT_EQ_ZERO(gl_polyblend->value))
		return;

	if (FLOAT_EQ_ZERO(v_blend[3]))
		return;

	qglDisable (GL_ALPHA_TEST);
	qglEnable (GL_BLEND);
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_TEXTURE_2D);

    qglLoadIdentity ();

	// FIXME: get rid of these
    qglRotatef (-90,  1, 0, 0);	    // put Z going up
    qglRotatef (90,  0, 0, 1);	    // put Z going up

	qglColor4fv (v_blend);
	shader = GL_BeginSurfaceShader (false, false, 0, 0, 0, 1);

	qglBegin (GL_QUADS);

	qglVertex3f (10, 100, 100);
	qglVertex3f (10, -100, 100);
	qglVertex3f (10, -100, -100);
	qglVertex3f (10, 100, -100);
	qglEnd ();
	if (shader)
		GL_EndSurfaceShader ();

	qglDisable (GL_BLEND);
	qglEnable (GL_TEXTURE_2D);
	qglEnable (GL_ALPHA_TEST);

	qglColor4fv(colorWhite);
}

//=======================================================================

int SignbitsForPlane (cplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (FLOAT_LT_ZERO(out->normal[j]))
			bits |= 1<<j;
	}
	return bits;
}


void R_SetFrustum (void)
{
	int		i;

#if 0
	/*
	** this code is wrong, since it presume a 90 degree FOV both in the
	** horizontal and vertical plane
	*/
	// front side is visible
	VectorAdd (vpn, vright, frustum[0].normal);
	VectorSubtract (vpn, vright, frustum[1].normal);
	VectorAdd (vpn, vup, frustum[2].normal);
	VectorSubtract (vpn, vup, frustum[3].normal);

	// we theoretically don't need to normalize these vectors, but I do it
	// anyway so that debugging is a little easier
	VectorNormalize( frustum[0].normal );
	VectorNormalize( frustum[1].normal );
	VectorNormalize( frustum[2].normal );
	VectorNormalize( frustum[3].normal );
#else
	// rotate VPN right by FOV_X/2 degrees
	RotatePointAroundVector( frustum[0].normal, vup, vpn, -(90-r_newrefdef.fov_x / 2 ) );
	// rotate VPN left by FOV_X/2 degrees
	RotatePointAroundVector( frustum[1].normal, vup, vpn, 90-r_newrefdef.fov_x / 2 );
	// rotate VPN up by FOV_X/2 degrees
	RotatePointAroundVector( frustum[2].normal, vright, vpn, 90-r_newrefdef.fov_y / 2 );
	// rotate VPN down by FOV_X/2 degrees
	RotatePointAroundVector( frustum[3].normal, vright, vpn, -( 90 - r_newrefdef.fov_y / 2 ) );
#endif

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

//=======================================================================

/* Q2PRO 601a8df: SCR_ParseColor names/indices and #RGB/#RRGGBB/#RRGGBBAA. */
static int R_ClearColorHex (int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static qboolean R_ParseClearColor (const char *text, byte color[4])
{
	static const char *names[8] = {
		"black", "red", "green", "yellow", "blue", "cyan", "magenta", "white"
	};
	static const byte colors[8][3] = {
		{ 0, 0, 0 }, { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 },
		{ 0, 0, 255 }, { 0, 255, 255 }, { 255, 0, 255 }, { 255, 255, 255 }
	};
	int digits[8], count, i, index;
	const char *p;

	if (!text || !*text)
		return false;
	if (*text == '#')
	{
		for (count = 0, p = text + 1; *p; count++, p++)
		{
			if (count == 8 || (digits[count] = R_ClearColorHex (*p)) < 0)
				return false;
		}
		if (count != 3 && count != 6 && count != 8)
			return false;
		color[3] = 255;
		for (i = 0; i < (count == 3 ? 3 : count / 2); i++)
			color[i] = (byte)(count == 3 ? digits[i] * 17 : digits[i * 2] * 16 + digits[i * 2 + 1]);
		return true;
	}
	if (*text >= '0' && *text <= '9')
	{
		index = 0;
		for (p = text; *p; p++)
		{
			if (*p < '0' || *p > '9' || (index = index * 10 + *p - '0') >= 8)
				return false;
		}
	}
	else
	{
		for (index = 0; index < 8; index++)
			if (!strcmp (text, names[index]))
				break;
		if (index == 8)
			return false;
	}
	memcpy (color, colors[index], 3);
	color[3] = 255;
	return true;
}

static void R_UpdateClearColor (void)
{
	byte color[4];
	int i;

	if (!gl_clearcolor->modified)
		return;
	if (!R_ParseClearColor (gl_clearcolor->string, color))
	{
		ri.Con_Printf (PRINT_ALL, "Invalid value '%s' for gl_clearcolor; resetting to black.\n", gl_clearcolor->string);
		ri.Cvar_Set ("gl_clearcolor", "black");
		color[0] = color[1] = color[2] = 0;
		color[3] = 255;
	}
	for (i = 0; i < 4; i++)
		r_clearcolor[i] = color[i] / 255.0f;
	gl_clearcolor->modified = false;
	r_clearcolor_dirty = true;
}

static void R_RestoreClearColor (void)
{
	R_UpdateClearColor ();
	qglClearColor (r_clearcolor[0], r_clearcolor[1], r_clearcolor[2], r_clearcolor[3]);
	r_clearcolor_dirty = false;
}

/* Q2PRO 601a8df: replace the completed frame to expose display tearing. */
static void R_DrawTearing (void)
{
	static unsigned frame;

	if (!gl_showtearing->intvalue)
		return;
	frame++;
	if (frame & 1)
		qglClearColor (1, 1, 1, 1);
	else
		qglClearColor (1, 0, 0, 0);
	qglClear (GL_COLOR_BUFFER_BIT);
	R_RestoreClearColor ();
}

static const char *GL_ErrorString (GLenum error)
{
	switch (error)
	{
	case GL_NO_ERROR:
		return "GL_NO_ERROR";
	case GL_INVALID_ENUM:
		return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE:
		return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION:
		return "GL_INVALID_OPERATION";
	case GL_STACK_OVERFLOW:
		return "GL_STACK_OVERFLOW";
	case GL_STACK_UNDERFLOW:
		return "GL_STACK_UNDERFLOW";
	case GL_OUT_OF_MEMORY:
		return "GL_OUT_OF_MEMORY";
	default:
		return "UNKNOWN ERROR";
	}
}

/* Drain errors even with reporting off; resource fallback checks remain independent. */
static void GL_ShowErrors (const char *function)
{
	GLenum error;

	while ((error = qglGetError ()) != GL_NO_ERROR)
		if (gl_showerrors->intvalue)
			ri.Con_Printf (PRINT_ALL, "%s: %s (0x%x)\n", function,
				GL_ErrorString (error), (unsigned)error);
}

static void GL_ShowFrameErrors (const char *function)
{
	if (gl_showerrors->intvalue > 1)
		GL_ShowErrors (function);
}

/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame (void)
{
	mleaf_t	*leaf;

	r_framecount++;

// build the transformation matrix for the given view angles
	FastVectorCopy (r_newrefdef.vieworg, r_origin);

	AngleVectors (r_newrefdef.viewangles, vpn, vright, vup);

// current viewcluster
	if ( !( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		r_oldviewcluster = r_viewcluster;
		r_oldviewcluster2 = r_viewcluster2;
		leaf = Mod_PointInLeaf (r_origin, r_worldmodel);
		r_viewcluster = r_viewcluster2 = leaf->cluster;

		// check above and below so crossing solid water doesn't draw wrong
		if (!leaf->contents)
		{	// look down a bit
			vec3_t	temp;

			FastVectorCopy (r_origin, temp);
			temp[2] -= 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if ( !(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != r_viewcluster2) )
				r_viewcluster2 = leaf->cluster;
		}
		else
		{	// look up a bit
			vec3_t	temp;

			FastVectorCopy (r_origin, temp);
			temp[2] += 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if ( !(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != r_viewcluster2) )
				r_viewcluster2 = leaf->cluster;
		}
	}

	v_blend[0] = r_newrefdef.blend[0];
	v_blend[1] = r_newrefdef.blend[1];
	v_blend[2] = r_newrefdef.blend[2];
	v_blend[3] = r_newrefdef.blend[3];

	c_brush_polys = 0;
	c_alias_polys = 0;

	// clear out the portion of the screen that the NOWORLDMODEL defines
	/*if ( r_newrefdef.rdflags & RDF_NOWORLDMODEL )
	{
		qglEnable( GL_SCISSOR_TEST );
		qglClearColor( 0.3f, 0.3f, 0.3f, 1 );
		
		qglScissor( r_newrefdef.x, vid.height - r_newrefdef.height - r_newrefdef.y, r_newrefdef.width, r_newrefdef.height );
		qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		
		qglClearColor( 1, 0, 0.5f, 0.5f );

		qglDisable( GL_SCISSOR_TEST );
	}*/
	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
	{
		qglEnable(GL_SCISSOR_TEST);
		qglClearColor(0.3f, 0.3f, 0.3f, 1);
		qglScissor(r_newrefdef.x, vid.height - r_newrefdef.height - r_newrefdef.y, r_newrefdef.width,
			   r_newrefdef.height);
		qglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		R_RestoreClearColor ();
		qglDisable(GL_SCISSOR_TEST);
	}
}

/* Keep the whole map inside the sky box and far plane. Positive gl_zfar
 * values retain the explicit distance override. */
float R_FarClip (void)
{
	int		i;
	double	distance = 0, extent, offset;
	float	mins, maxs, view;

	if (gl_zfar && gl_zfar->value > 0 && gl_zfar->value <= FLT_MAX)
		return gl_zfar->value;
	if (!r_worldmodel || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		return 8192;

	for (i = 0; i < 3; i++)
	{
		mins = r_worldmodel->mins[i];
		maxs = r_worldmodel->maxs[i];
		view = r_newrefdef.vieworg[i];
		if (!(mins >= -FLT_MAX && maxs <= FLT_MAX && mins <= maxs &&
			view >= -FLT_MAX && view <= FLT_MAX))
			return 8192;
		/* The full map span stays constant while the camera is inside it. */
		extent = (double)maxs - mins;
		offset = fabs ((double)view - mins);
		if (offset > extent)
			extent = offset;
		offset = fabs ((double)view - maxs);
		if (offset > extent)
			extent = offset;
		distance += extent * extent;
	}

	/* MakeSkyVec places the sky at half the far distance. */
	distance = 2 * (sqrt (distance) + 64);
	if (distance > FLT_MAX)
		return FLT_MAX;
	return distance > 8192 ? (float)distance : 8192;
}

float R_NearClip (void)
{
	float znear = gl_znear ? gl_znear->value : 2.0f;
	return (znear > 0 && znear <= FLT_MAX) ? znear : 2.0f;
}

void MYgluPerspective( GLdouble fovy, GLdouble aspect,
		     GLdouble zNear, GLdouble zFar )
{
   GLdouble xmin, xmax, ymin, ymax;

   ymax = zNear * tan( fovy * M_PI / 360.0 );
   ymin = -ymax;

   xmin = ymin * aspect;
   xmax = ymax * aspect;

#ifdef STERO_SUPPORT
   xmin += -( 2 * gl_state.camera_separation ) / zNear;
   xmax += -( 2 * gl_state.camera_separation ) / zNear;
#endif

   qglFrustum( xmin, xmax, ymin, ymax, zNear, zFar );
}


/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	float	screenaspect;
//	float	yfov;
	int y;

	/* Keep the origin signed: rounded HUD dimensions can exceed the drawable. */
	y = (int)vid.height - r_newrefdef.y - r_newrefdef.height;
	qglViewport (r_newrefdef.x, y, r_newrefdef.width, r_newrefdef.height);

	//
	// set up projection matrix
	//
    screenaspect = (float)r_newrefdef.width/r_newrefdef.height;
//	yfov = 2*atan((float)r_newrefdef.height/r_newrefdef.width)*180/M_PI;
	qglMatrixMode(GL_PROJECTION);
    qglLoadIdentity ();
	MYgluPerspective (r_newrefdef.fov_y, screenaspect, R_NearClip (), R_FarClip ());

	qglCullFace(GL_FRONT);

	qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity ();

    qglRotatef (-90,  1, 0, 0);	    // put Z going up
    qglRotatef (90,  0, 0, 1);	    // put Z going up
    qglRotatef (-r_newrefdef.viewangles[2],  1, 0, 0);
    qglRotatef (-r_newrefdef.viewangles[0],  0, 1, 0);
    qglRotatef (-r_newrefdef.viewangles[1],  0, 0, 1);
    qglTranslatef (-r_newrefdef.vieworg[0],  -r_newrefdef.vieworg[1],  -r_newrefdef.vieworg[2]);

//	if ( gl_state.camera_separation != 0 && gl_state.stereo_enabled )
//		qglTranslatef ( gl_state.camera_separation, 0, 0 );

	qglGetFloatv (GL_MODELVIEW_MATRIX, r_world_matrix);

	//
	// set drawing parms
	//
	if (FLOAT_NE_ZERO(gl_cull->value))
		qglEnable(GL_CULL_FACE);
	else
		qglDisable(GL_CULL_FACE);

	//qglDisable(GL_BLEND);
	qglDisable(GL_ALPHA_TEST);
	//qglEnable(GL_ALPHA_TEST);
	qglEnable(GL_DEPTH_TEST);
}

/*
=============
R_Clear
=============
*/

float ref_frand(void)
{
	return (((rand()&32767)) * .0000305185094759971922971282082583086642f);
}

void R_Clear (void)
{
	R_UpdateClearColor ();
	if (gl_clear->value != 2 && r_clearcolor_dirty)
		R_RestoreClearColor ();

	if (FLOAT_NE_ZERO(gl_ztrick->value) && r_worldmodel != NULL)
	{
		static int trickframe;

		if (FLOAT_NE_ZERO(gl_clear->value))
		{
			if (gl_clear->value == 2)
			{
				qglClearColor (ref_frand(), ref_frand(), ref_frand(), 1.0);
				r_clearcolor_dirty = true;
				GL_CheckForError ();
			}
			qglClear (GL_COLOR_BUFFER_BIT);
			GL_CheckForError ();
		}

		trickframe++;
		if (trickframe & 1)
		{
			gldepthmin = 0;
			gldepthmax = 0.49999;
			qglDepthFunc (GL_LEQUAL);
			GL_CheckForError ();
		}
		else
		{
			gldepthmin = 1;
			gldepthmax = 0.5;
			qglDepthFunc (GL_GEQUAL);
			GL_CheckForError ();
		}
	}
	else
	{
		if (FLOAT_NE_ZERO(gl_clear->value))
		{
			if (gl_clear->value == 2)
			{
				qglClearColor (ref_frand(), ref_frand(), ref_frand(), 1.0);
				r_clearcolor_dirty = true;
				GL_CheckForError ();
			}

			qglClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			GL_CheckForError ();
		}
		else
		{
			qglClear (GL_DEPTH_BUFFER_BIT);
			GL_CheckForError ();
		}

		gldepthmin = 0;
		gldepthmax = 1;
		qglDepthFunc (GL_LEQUAL);
		GL_CheckForError ();
	}

	qglDepthRange (gldepthmin, gldepthmax);
	GL_CheckForError ();
}

/*void R_Flash( void )
{
	R_PolyBlend ();
}*/

/* Optional GPU view timing. Query results are read only after they are ready. */
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#define R_GPU_PERF_SLOTS 8

typedef struct
{
	void (APIENTRY *GenQueries) (GLsizei, GLuint *);
	void (APIENTRY *DeleteQueries) (GLsizei, const GLuint *);
	void (APIENTRY *BeginQuery) (GLenum, GLuint);
	void (APIENTRY *EndQuery) (GLenum);
	void (APIENTRY *GetQueryiv) (GLenum, GLenum, GLint *);
	void (APIENTRY *GetQueryObjectiv) (GLuint, GLenum, GLint *);
	void (APIENTRY *GetQueryObjectui64v) (GLuint, GLenum, unsigned long long *);
} r_gpu_perf_procs_t;

typedef struct
{
	GLuint id;
	unsigned int window;
	qboolean pending, keep;
} r_gpu_perf_slot_t;

static struct
{
	r_gpu_perf_procs_t proc;
	r_gpu_perf_slot_t slot[R_GPU_PERF_SLOTS];
	qboolean capable, allocated, mode, busy_frame, draining;
	int active_slot, frame_slot;
	unsigned int next_slot, window, attempts, eligible, completed;
	unsigned int unavailable, busy, ineligible, view_calls, drain_frames;
	double sum_ms, max_ms;
} r_gpu_perf;

static qboolean R_GPUPerfExtension (const char *name)
{
	const char *extensions = gl_config.extensions_string, *p;
	size_t length = strlen (name);
	if (!extensions)
		return false;
	for (p = extensions; (p = strstr (p, name)) != NULL; p += length)
		if ((p == extensions || p[-1] == ' ') && (p[length] == ' ' || !p[length]))
			return true;
	return false;
}

static size_t R_GPUPerfProcedure (const char *name)
{
	size_t address = (size_t)qwglGetProcAddress (name);
	if (!address || address == 1 || address == 2 || address == 3 || address == (size_t)-1)
		return 0;
	return address;
}

static void R_GPUPerfShutdown (void)
{
	GLuint ids[R_GPU_PERF_SLOTS];
	unsigned int i;
	if (r_gpu_perf.allocated && r_gpu_perf.active_slot >= 0 &&
		r_gpu_perf.proc.EndQuery)
		r_gpu_perf.proc.EndQuery (GL_TIME_ELAPSED);
	if (r_gpu_perf.allocated && r_gpu_perf.proc.DeleteQueries)
	{
		for (i = 0; i < R_GPU_PERF_SLOTS; i++)
			ids[i] = r_gpu_perf.slot[i].id;
		r_gpu_perf.proc.DeleteQueries (R_GPU_PERF_SLOTS, ids);
	}
	memset (&r_gpu_perf, 0, sizeof (r_gpu_perf));
	r_gpu_perf.active_slot = r_gpu_perf.frame_slot = -1;
}

static void R_GPUPerfInit (void)
{
	int major = 0, minor = 0;
	GLint bits = 0;
	r_gpu_perf_procs_t *p = &r_gpu_perf.proc;
	R_GPUPerfShutdown ();
	if (!qwglGetProcAddress || !gl_config.version_string ||
		sscanf (gl_config.version_string, "%d.%d", &major, &minor) != 2 ||
		(major < 3 || (major == 3 && minor < 2)) ||
		!((major > 3 || (major == 3 && minor >= 3)) ||
			R_GPUPerfExtension ("GL_ARB_timer_query")))
		return;
	p->GenQueries = (void (APIENTRY *) (GLsizei, GLuint *))R_GPUPerfProcedure ("glGenQueries");
	p->DeleteQueries = (void (APIENTRY *) (GLsizei, const GLuint *))R_GPUPerfProcedure ("glDeleteQueries");
	p->BeginQuery = (void (APIENTRY *) (GLenum, GLuint))R_GPUPerfProcedure ("glBeginQuery");
	p->EndQuery = (void (APIENTRY *) (GLenum))R_GPUPerfProcedure ("glEndQuery");
	p->GetQueryiv = (void (APIENTRY *) (GLenum, GLenum, GLint *))R_GPUPerfProcedure ("glGetQueryiv");
	p->GetQueryObjectiv = (void (APIENTRY *) (GLuint, GLenum, GLint *))R_GPUPerfProcedure ("glGetQueryObjectiv");
	p->GetQueryObjectui64v = (void (APIENTRY *) (GLuint, GLenum, unsigned long long *))
		R_GPUPerfProcedure ("glGetQueryObjectui64v");
	if (!p->GenQueries || !p->DeleteQueries || !p->BeginQuery || !p->EndQuery ||
		!p->GetQueryiv || !p->GetQueryObjectiv || !p->GetQueryObjectui64v)
		return;
	p->GetQueryiv (GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
	r_gpu_perf.capable = bits > 0;
}

static qboolean R_GPUPerfAllocate (void)
{
	GLuint ids[R_GPU_PERF_SLOTS];
	unsigned int i;
	if (r_gpu_perf.allocated)
		return true;
	memset (ids, 0, sizeof (ids));
	r_gpu_perf.proc.GenQueries (R_GPU_PERF_SLOTS, ids);
	for (i = 0; i < R_GPU_PERF_SLOTS; i++)
	{
		if (!ids[i])
		{
			r_gpu_perf.proc.DeleteQueries (R_GPU_PERF_SLOTS, ids);
			r_gpu_perf.capable = false;
			return false;
		}
		r_gpu_perf.slot[i].id = ids[i];
	}
	r_gpu_perf.allocated = true;
	return true;
}

static unsigned int R_GPUPerfPending (void)
{
	unsigned int i, pending = 0;
	for (i = 0; i < R_GPU_PERF_SLOTS; i++)
		if (r_gpu_perf.slot[i].pending && r_gpu_perf.slot[i].keep &&
			r_gpu_perf.slot[i].window == r_gpu_perf.window)
			pending++;
	return pending;
}

static void R_GPUPerfReport (unsigned int pending)
{
	ri.Con_Printf (PRINT_ALL,
		"r_speeds %d GPU view: %u/%u completed/eligible, %.3f/%.3f ms avg/max; pending %u, unavailable %u, ring full %u, ineligible %u; %s results after %u drain frames\n",
		r_speeds->value >= 5 ? 5 : 4, r_gpu_perf.completed, r_gpu_perf.eligible,
		r_gpu_perf.completed ? r_gpu_perf.sum_ms / r_gpu_perf.completed : 0.0,
		r_gpu_perf.max_ms, pending, r_gpu_perf.unavailable,
		r_gpu_perf.busy, r_gpu_perf.ineligible,
		pending ? "partial" : "complete", r_gpu_perf.drain_frames);
	r_gpu_perf.window++;
	r_gpu_perf.attempts = r_gpu_perf.eligible = r_gpu_perf.completed = 0;
	r_gpu_perf.unavailable = r_gpu_perf.busy = r_gpu_perf.ineligible = 0;
	r_gpu_perf.drain_frames = 0;
	r_gpu_perf.draining = false;
	r_gpu_perf.sum_ms = r_gpu_perf.max_ms = 0;
}

static void R_GPUPerfBeginFrame (void)
{
	unsigned int i;
	/* Recover an interrupted frame without leaving a timer query active. */
	if (r_gpu_perf.allocated && r_gpu_perf.active_slot >= 0)
	{
		r_gpu_perf.proc.EndQuery (GL_TIME_ELAPSED);
		r_gpu_perf.slot[r_gpu_perf.active_slot].pending = true;
		r_gpu_perf.slot[r_gpu_perf.active_slot].keep = false;
		r_gpu_perf.active_slot = -1;
	}
	if (!r_speeds || r_speeds->value < 4)
	{
		if (r_gpu_perf.mode)
			r_gpu_perf.window++;
		r_gpu_perf.mode = false;
		r_gpu_perf.draining = false;
		return;
	}
	if (!r_gpu_perf.mode)
	{
		r_gpu_perf.mode = true;
		r_gpu_perf.window++;
		r_gpu_perf.attempts = r_gpu_perf.eligible = r_gpu_perf.completed = 0;
		r_gpu_perf.unavailable = r_gpu_perf.busy = r_gpu_perf.ineligible = 0;
		r_gpu_perf.drain_frames = 0;
		r_gpu_perf.sum_ms = r_gpu_perf.max_ms = 0;
	}
	r_gpu_perf.frame_slot = r_gpu_perf.active_slot = -1;
	r_gpu_perf.view_calls = 0;
	r_gpu_perf.busy_frame = false;
	if (r_gpu_perf.allocated)
	{
		for (i = 0; i < R_GPU_PERF_SLOTS; i++)
		{
			GLint available = 0;
			unsigned long long nanoseconds;
			r_gpu_perf_slot_t *slot = &r_gpu_perf.slot[i];
			if (!slot->pending)
				continue;
			r_gpu_perf.proc.GetQueryObjectiv (slot->id, GL_QUERY_RESULT_AVAILABLE, &available);
			if (!available)
				continue;
			if (slot->keep && slot->window == r_gpu_perf.window)
			{
				double ms;
				r_gpu_perf.proc.GetQueryObjectui64v (slot->id, GL_QUERY_RESULT, &nanoseconds);
				ms = (double)nanoseconds / 1000000.0;
				r_gpu_perf.completed++;
				r_gpu_perf.sum_ms += ms;
				if (ms > r_gpu_perf.max_ms)
					r_gpu_perf.max_ms = ms;
			}
			slot->pending = slot->keep = false;
		}
	}
	if (r_gpu_perf.draining)
	{
		unsigned int pending = R_GPUPerfPending ();
		r_gpu_perf.drain_frames++;
		if (!pending || r_gpu_perf.drain_frames >= 120)
			R_GPUPerfReport (pending);
	}
}

static void R_GPUPerfBeginView (void)
{
	unsigned int n;
	if (!r_gpu_perf.mode || r_gpu_perf.draining || !r_speeds || r_speeds->value < 4)
		return;
	r_gpu_perf.view_calls++;
	if (r_gpu_perf.view_calls != 1 || FLOAT_NE_ZERO (r_norefresh->value) ||
		FLOAT_NE_ZERO (gl_finish->value) || !r_gpu_perf.capable ||
		!R_GPUPerfAllocate ())
		return;
	for (n = 0; n < R_GPU_PERF_SLOTS; n++)
	{
		unsigned int i = (r_gpu_perf.next_slot + n) % R_GPU_PERF_SLOTS;
		if (!r_gpu_perf.slot[i].pending)
		{
			r_gpu_perf.active_slot = r_gpu_perf.frame_slot = (int)i;
			r_gpu_perf.slot[i].window = r_gpu_perf.window;
			r_gpu_perf.slot[i].keep = false;
			r_gpu_perf.next_slot = (i + 1) % R_GPU_PERF_SLOTS;
			r_gpu_perf.proc.BeginQuery (GL_TIME_ELAPSED, r_gpu_perf.slot[i].id);
			return;
		}
	}
	r_gpu_perf.busy_frame = true;
}

static void R_GPUPerfEndView (void)
{
	if (r_gpu_perf.active_slot < 0)
		return;
	r_gpu_perf.proc.EndQuery (GL_TIME_ELAPSED);
	r_gpu_perf.slot[r_gpu_perf.active_slot].pending = true;
	r_gpu_perf.active_slot = -1;
}

static void R_GPUPerfEndFrame (qboolean eligible)
{
	if (!r_gpu_perf.mode || !r_speeds || r_speeds->value < 4)
		return;
	if (r_gpu_perf.draining)
		return;
	r_gpu_perf.attempts++;
	if (!eligible || r_gpu_perf.view_calls != 1)
		r_gpu_perf.ineligible++;
	else if (r_gpu_perf.frame_slot >= 0)
	{
		r_gpu_perf.slot[r_gpu_perf.frame_slot].keep = true;
		r_gpu_perf.eligible++;
	}
	else if (r_gpu_perf.busy_frame)
		r_gpu_perf.busy++;
	else
		r_gpu_perf.unavailable++;
	if (r_gpu_perf.attempts < 120)
		return;
	r_gpu_perf.draining = true;
	if (!R_GPUPerfPending ())
		R_GPUPerfReport (0);
}

/* Renderer-only CPU timings. These measure wall time in driver calls, not GPU work. */
#ifdef _WIN32
typedef LARGE_INTEGER r_perf_tick_t;
static LARGE_INTEGER r_perf_frequency;
#else
typedef struct timespec r_perf_tick_t;
#endif

typedef struct
{
	qboolean active, clock_ok, capture, sync, norefresh, swap_ok;
	unsigned int begins, views, swaps;
	r_perf_tick_t frame_start, view_start, view_end, swap_start, swap_end;
	double upload_calls, upload_texels, upload_bytes;
} r_perf_frame_t;

typedef struct
{
	unsigned int attempts, valid, no_view, multi, no_swap, sync, capture, clock;
	double before_sum, view_sum, after_sum, swap_sum;
	double before_max, view_max, after_max, swap_max;
	double upload_calls, upload_texels, upload_bytes;
} r_perf_stats_t;

static r_perf_frame_t r_perf_frame;
static r_perf_stats_t r_perf_stats;
static qboolean r_perf_enabled;

static void R_PerfReset (void)
{
	memset (&r_perf_frame, 0, sizeof (r_perf_frame));
	memset (&r_perf_stats, 0, sizeof (r_perf_stats));
	r_perf_enabled = false;
}

static qboolean R_PerfClockReady (void)
{
#ifdef _WIN32
	LARGE_INTEGER frequency;
	if (r_perf_frequency.QuadPart > 0)
		return true;
	if (!QueryPerformanceFrequency (&frequency) || frequency.QuadPart <= 0)
		return false;
	r_perf_frequency = frequency;
	return true;
#else
	return true;
#endif
}

static qboolean R_PerfNow (r_perf_tick_t *tick)
{
#ifdef _WIN32
	return QueryPerformanceCounter (tick) != 0;
#else
	return clock_gettime (CLOCK_MONOTONIC, tick) == 0;
#endif
}

static double R_PerfElapsedMs (const r_perf_tick_t *start, const r_perf_tick_t *end)
{
#ifdef _WIN32
	if (end->QuadPart < start->QuadPart)
		return -1;
	return (double)(end->QuadPart - start->QuadPart) * 1000.0 /
		(double)r_perf_frequency.QuadPart;
#else
	double elapsed = (double)(end->tv_sec - start->tv_sec) * 1000.0 +
		(double)(end->tv_nsec - start->tv_nsec) / 1000000.0;
	return elapsed >= 0 ? elapsed : -1;
#endif
}

static void R_PerfBeginFrame (void)
{
	if (r_speeds->value < 3)
	{
		if (r_perf_enabled)
			R_PerfReset ();
		return;
	}
	if (!r_perf_enabled)
	{
		memset (&r_perf_stats, 0, sizeof (r_perf_stats));
		r_perf_enabled = true;
	}
	if (r_perf_frame.active)
	{
		r_perf_frame.begins++;
		return;
	}
	memset (&r_perf_frame, 0, sizeof (r_perf_frame));
	r_perf_frame.active = true;
	r_perf_frame.begins = 1;
	r_perf_frame.clock_ok = R_PerfClockReady () &&
		R_PerfNow (&r_perf_frame.frame_start);
}

static void R_PerfBeginView (void)
{
	if (!r_perf_frame.active)
		return;
	r_perf_frame.views++;
	if (r_perf_frame.views != 1)
		return;
	r_perf_frame.norefresh = FLOAT_NE_ZERO (r_norefresh->value);
	r_perf_frame.sync = FLOAT_NE_ZERO (gl_finish->value);
	if (r_perf_frame.clock_ok)
		r_perf_frame.clock_ok = R_PerfNow (&r_perf_frame.view_start);
}

static void R_PerfEndView (void)
{
	if (r_perf_frame.active && r_perf_frame.views == 1 && r_perf_frame.clock_ok)
		r_perf_frame.clock_ok = R_PerfNow (&r_perf_frame.view_end);
}

void R_PerfMarkCapture (void)
{
	if (r_perf_frame.active)
		r_perf_frame.capture = true;
}

void R_PerfRecordUpload (unsigned int texels, unsigned int bytes)
{
	if (!r_perf_frame.active)
		return;
	r_perf_frame.upload_calls += 1;
	r_perf_frame.upload_texels += texels;
	r_perf_frame.upload_bytes += bytes;
}

void R_PerfBeforeSwap (void)
{
	if (!r_perf_frame.active)
		return;
	r_perf_frame.swaps++;
	if (r_perf_frame.clock_ok)
		r_perf_frame.clock_ok = R_PerfNow (&r_perf_frame.swap_start);
}

void R_PerfAfterSwap (qboolean success)
{
	if (!r_perf_frame.active)
		return;
	r_perf_frame.swap_ok = success;
	if (r_perf_frame.clock_ok)
		r_perf_frame.clock_ok = R_PerfNow (&r_perf_frame.swap_end);
}

static void R_PerfEndFrame (void)
{
	double view, before_view, after_view, swap;
	const char *label = r_speeds->value >= 5 ? "r_speeds 5" :
											   (r_speeds->value >= 4 ? "r_speeds 4" : "r_speeds 3");
	if (r_speeds->value < 3)
	{
		if (r_perf_enabled)
			R_PerfReset ();
		return;
	}
	if (!r_perf_frame.active)
		return;
	r_perf_stats.attempts++;
	if (r_perf_frame.begins != 1 || r_perf_frame.views > 1 || r_perf_frame.swaps > 1)
		r_perf_stats.multi++;
	else if (r_perf_frame.views != 1 || r_perf_frame.norefresh)
		r_perf_stats.no_view++;
	else if (r_perf_frame.swaps != 1 || !r_perf_frame.swap_ok)
		r_perf_stats.no_swap++;
	else if (r_perf_frame.sync)
		r_perf_stats.sync++;
	else if (r_perf_frame.capture)
		r_perf_stats.capture++;
	else if (!r_perf_frame.clock_ok)
		r_perf_stats.clock++;
	else
	{
		view = R_PerfElapsedMs (&r_perf_frame.view_start, &r_perf_frame.view_end);
		before_view = R_PerfElapsedMs (&r_perf_frame.frame_start, &r_perf_frame.view_start);
		after_view = R_PerfElapsedMs (&r_perf_frame.view_end, &r_perf_frame.swap_start);
		swap = R_PerfElapsedMs (&r_perf_frame.swap_start, &r_perf_frame.swap_end);
		if (view < 0 || before_view < 0 || after_view < 0 || swap < 0)
			r_perf_stats.clock++;
		else
		{
			r_perf_stats.valid++;
			r_perf_stats.before_sum += before_view;
			r_perf_stats.view_sum += view;
			r_perf_stats.after_sum += after_view;
			r_perf_stats.swap_sum += swap;
			if (before_view > r_perf_stats.before_max)
				r_perf_stats.before_max = before_view;
			if (view > r_perf_stats.view_max)
				r_perf_stats.view_max = view;
			if (after_view > r_perf_stats.after_max)
				r_perf_stats.after_max = after_view;
			if (swap > r_perf_stats.swap_max)
				r_perf_stats.swap_max = swap;
			r_perf_stats.upload_calls += r_perf_frame.upload_calls;
			r_perf_stats.upload_texels += r_perf_frame.upload_texels;
			r_perf_stats.upload_bytes += r_perf_frame.upload_bytes;
		}
	}
	r_perf_frame.active = false;
	if (r_perf_stats.attempts < 120)
		return;
	if (r_perf_stats.valid)
		ri.Con_Printf (PRINT_ALL,
			"%s: %u/%u frames, CPU ms avg/max: pre-view %.3f/%.3f, view %.3f/%.3f, 2D/EndFrame %.3f/%.3f, swap %.3f/%.3f; lightmap glTexSubImage2D %.0f calls, %.0f RGBA texels, %.0f bytes\n",
			label, r_perf_stats.valid, r_perf_stats.attempts,
			r_perf_stats.before_sum / r_perf_stats.valid, r_perf_stats.before_max,
			r_perf_stats.view_sum / r_perf_stats.valid, r_perf_stats.view_max,
			r_perf_stats.after_sum / r_perf_stats.valid, r_perf_stats.after_max,
			r_perf_stats.swap_sum / r_perf_stats.valid, r_perf_stats.swap_max,
			r_perf_stats.upload_calls, r_perf_stats.upload_texels, r_perf_stats.upload_bytes);
	ri.Con_Printf (PRINT_ALL,
		"%s skipped: no view %u, multiple begin/view/swap %u, no swap %u, gl_finish %u, screenshot %u, clock %u\n",
		label, r_perf_stats.no_view, r_perf_stats.multi, r_perf_stats.no_swap,
		r_perf_stats.sync, r_perf_stats.capture, r_perf_stats.clock);
	memset (&r_perf_stats, 0, sizeof (r_perf_stats));
}

/* Q2PRO switchable-only lighting, applied to the renderer's private view. */
static void R_ApplyLightMode (void)
{
	static lightstyle_t styles[MAX_LIGHTSTYLES];
	int i;

	if (gl_dynamic->value != 2)
		return;

	r_newrefdef.num_dlights = 0;
	if (!r_newrefdef.lightstyles)
		return;

	memcpy (styles, r_newrefdef.lightstyles, sizeof (styles));
	/* Animated styles use the base style; map switches start at style 32. */
	for (i = 1; i < 32; i++)
		styles[i] = styles[0];
	r_newrefdef.lightstyles = styles;
}

/* Stencil belongs to the active view target, including the bloom FBO. */
int r_shadow_stencilbits;

static void R_SetupShadowStencil (void)
{
	r_shadow_stencilbits = 0;
	if (!(gl_shadows->value >= 2) || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		return;
	qglGetIntegerv (GL_STENCIL_BITS, &r_shadow_stencilbits);
	if (r_shadow_stencilbits < 1)
		return;
	qglPushAttrib (GL_ENABLE_BIT | GL_STENCIL_BUFFER_BIT);
	qglDisable (GL_SCISSOR_TEST);
	qglStencilMask (~0u);
	qglClearStencil (0);
	qglClear (GL_STENCIL_BUFFER_BIT);
	qglPopAttrib ();
}

/*
================
R_RenderView

r_newrefdef must be set before the first call
================
*/
void R_RenderView (refdef_t *fd)
{
	qboolean bloom_view;
	model_lightlevel.active = false;
	R_ClearAlphaSurfaces ();
	if (FLOAT_NE_ZERO(r_norefresh->value))
		return;

	r_newrefdef = *fd;
	R_BeginModelLightLevel ();
	R_ApplyLightMode ();
	GL_UpdateVertexLighting ();

	if (FLOAT_NE_ZERO(gl_hudscale->value))
	{
		r_newrefdef.width = (int)(r_newrefdef.width * gl_hudscale->value);
		r_newrefdef.height = (int)(r_newrefdef.height * gl_hudscale->value);
		r_newrefdef.x = (int)(r_newrefdef.x * gl_hudscale->value);
		r_newrefdef.y = (int)(r_newrefdef.y * gl_hudscale->value);
	}

	if (!r_worldmodel && !( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) )
		ri.Sys_Error (ERR_DROP, "R_RenderView: NULL worldmodel");

	//if (FLOAT_NE_ZERO(r_speeds->value))
	//{
	c_brush_polys = 0;
	c_alias_polys = 0;
	c_particle_array_draws = c_particle_immediate_draws = c_particle_vertices = 0;
	GL_ResetWorldBatches ();
	GL_ResetAliasBatches ();
	//}

	R_PushDlights ();

	if (FLOAT_NE_ZERO(gl_flush->value))
		qglFlush ();

	if (FLOAT_NE_ZERO(gl_finish->value))
		qglFinish ();

	R_SetupFrame ();

	R_SetFrustum ();

	R_SetupGL ();
	bloom_view = GL_BloomBeginView (&r_newrefdef);
	R_SetupShadowStencil ();

	R_MarkLeaves ();	// done here so we know if we're in water

	if (gl_config.r1gl_QueryBits)
		R_Occlusion_Run ();

	R_DrawWorld ();

	R_DrawEntitiesOnList ();

	R_RenderDlights ();

	R_DrawAlphaSurfaces ();
	R_DrawBeamsOnList ();
	R_DrawParticles ();
	R_DrawTranslucentEntities (true);
	R_EndModelLightLevel ();
	R_DrawModelOrigins ();

	if (bloom_view)
	{
		GL_BloomEndView (&r_newrefdef);
		R_PolyBlend ();
	}
	else
	{
		R_PolyBlend ();
		GL_PostProcessView (&r_newrefdef);
	}

	if (FLOAT_NE_ZERO (r_speeds->value) && r_speeds->value < 3)
	{
		ri.Con_Printf (PRINT_ALL, "%4i wpoly %4i epoly %i tex %i lmaps\n",
			c_brush_polys, 
			c_alias_polys, 
			c_visible_textures, 
			c_visible_lightmaps);
		if (r_speeds->value >= 2)
		{
			ri.Con_Printf (PRINT_ALL,
				"%u world batches, %u vertices, %u indices, %u immediate polys\n",
				c_world_batches, c_world_batch_vertices,
				c_world_batch_indices, c_world_immediate);
			GL_PrintWorldBufferInfo ();
			GL_PrintAliasBatchInfo ();
			ri.Con_Printf (PRINT_ALL,
				"%u particle array draws, %u immediate draws, %u vertices\n",
				c_particle_array_draws, c_particle_immediate_draws,
				c_particle_vertices);
			GL_PrintPostFXInfo ();
			GL_PrintBloomInfo ();
		}
	}
}


void	R_SetGL2D (void)
{
	Draw_Flush ();
	Draw_ResetSpace ();
	// set 2D virtual screen size
	qglViewport (0,0, vid.width, vid.height);
	qglMatrixMode(GL_PROJECTION);
    qglLoadIdentity ();
	//qglOrtho  (0, vid.width, vid.height, 0, -99999, 99999);
	qglOrtho(0, vid_scaled_width, vid_scaled_height, 0, -99999, 99999);
	qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity ();
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_CULL_FACE);
	//GLPROFqglDisable (GL_BLEND);
	qglEnable (GL_ALPHA_TEST);
	qglColor4fv(colorWhite);
}

#ifdef STEREO_SUPPORT
static void GL_DrawColoredStereoLinePair( float r, float g, float b, float y )
{
	qglColor3f( r, g, b );
	qglVertex2f( 0, y );
	qglVertex2f( vid.width, y );
	qglColor3f( 0, 0, 0 );
	qglVertex2f( 0, y + 1 );
	qglVertex2f( vid.width, y + 1 );
}

static void GL_DrawStereoPattern( void )
{
	int i;

	if ( !( gl_config.renderer & GL_RENDERER_INTERGRAPH ) )
		return;

	if ( !gl_state.stereo_enabled )
		return;

	R_SetGL2D();

	qglDrawBuffer( GL_BACK_LEFT );

	for ( i = 0; i < 20; i++ )
	{
		qglBegin( GL_LINES );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 0 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 2 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 4 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 6 );
			GL_DrawColoredStereoLinePair( 0, 1, 0, 8 );
			GL_DrawColoredStereoLinePair( 1, 1, 0, 10);
			GL_DrawColoredStereoLinePair( 1, 1, 0, 12);
			GL_DrawColoredStereoLinePair( 0, 1, 0, 14);
		qglEnd();
		
		GLimp_EndFrame();
	}
}
#endif


/*
====================
R_SetLightLevel

====================
*/
void R_SetLightLevel (void)
{
	vec3_t		shadelight;

	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
		return;

	// save off light value for server to look at (BIG HACK!)

	R_LightPointLegacy (r_newrefdef.vieworg, shadelight);

	// pick the greatest component, which should be the same
	// as the mono value returned by software
	if (shadelight[0] > shadelight[1])
	{
		if (shadelight[0] > shadelight[2])
			r_lightlevel->value = 150*shadelight[0];
		else
			r_lightlevel->value = 150*shadelight[2];
	}
	else
	{
		if (shadelight[1] > shadelight[2])
			r_lightlevel->value = 150*shadelight[1];
		else
			r_lightlevel->value = 150*shadelight[2];
	}

}

/*
@@@@@@@@@@@@@@@@@@@@@
R_RenderFrame

@@@@@@@@@@@@@@@@@@@@@
*/
void EXPORT R_RenderFrame (refdef_t *fd)
{
	Draw_Flush ();
	R_GPUPerfBeginView ();
	R_PerfBeginView ();
	R_RenderView( fd );
	R_PerfEndView ();
	R_GPUPerfEndView ();
	GL_JumpDrawOrigin ();
	R_SetLightLevel ();
	R_SetGL2D ();
	GL_ShowFrameErrors ("R_RenderFrame");
}

void Cmd_HashStats_f (void);
void R_Register( void )
{
	r_lefthand = ri.Cvar_Get( "hand", "0", CVAR_USERINFO | CVAR_ARCHIVE );
	r_norefresh = ri.Cvar_Get ("r_norefresh", "0", 0);
	r_fullbright = ri.Cvar_Get ("r_fullbright", "0", 0);
	r_drawentities = ri.Cvar_Get ("r_drawentities", "1", 0);
	gl_draworder = ri.Cvar_Get ("gl_draworder", "1", 0);
	r_drawworld = ri.Cvar_Get ("r_drawworld", "1", 0);
	r_novis = ri.Cvar_Get ("r_novis", "0", 0);
	r_nocull = ri.Cvar_Get ("r_nocull", "0", 0);
	gl_cull_nodes = ri.Cvar_Get ("gl_cull_nodes", "1", 0);
	gl_cull_models = ri.Cvar_Get ("gl_cull_models", "1", 0);
	r_lerpmodels = ri.Cvar_Get ("r_lerpmodels", "1", 0);
	gl_md5_load = ri.Cvar_Get ("gl_md5_load", "1", CVAR_ARCHIVE);
	gl_md5_use = ri.Cvar_Get ("gl_md5_use", "1", CVAR_ARCHIVE);
	gl_md5_distance = ri.Cvar_Get ("gl_md5_distance", "2048", CVAR_ARCHIVE);
	r_speeds = ri.Cvar_Get ("r_speeds", "0", 0);

	r_lightlevel = ri.Cvar_Get ("r_lightlevel", "0", CVAR_NOSET);

	//gl_nosubimage = ri.Cvar_Get( "gl_nosubimage", "0", 0 );
	gl_allow_software = ri.Cvar_Get( "gl_allow_software", "0", 0 );

	gl_particle_min_size = ri.Cvar_Get( "gl_particle_min_size", "2", CVAR_ARCHIVE );
	gl_particle_max_size = ri.Cvar_Get( "gl_particle_max_size", "40", CVAR_ARCHIVE );
	gl_particle_size = ri.Cvar_Get( "gl_particle_size", "40", CVAR_ARCHIVE );
	gl_particle_att_a = ri.Cvar_Get( "gl_particle_att_a", "0.01", CVAR_ARCHIVE );
	gl_particle_att_b = ri.Cvar_Get( "gl_particle_att_b", "0.0", CVAR_ARCHIVE );
	gl_particle_att_c = ri.Cvar_Get( "gl_particle_att_c", "0.01", CVAR_ARCHIVE );
	gl_batch_particles = ri.Cvar_Get ("gl_batch_particles", "1", CVAR_ARCHIVE);
	gl_partscale = ri.Cvar_Get ("gl_partscale", "2", 0);
	gl_partstyle = ri.Cvar_Get ("gl_partstyle", "0", 0);
	gl_partshape = ri.Cvar_Get ("gl_partshape", "-1", 0);

	gl_modulate = ri.Cvar_Get ("gl_modulate", "1", CVAR_ARCHIVE);
	gl_modulate_world = ri.Cvar_Get ("gl_modulate_world", "1", 0);
	gl_modulate_entities = ri.Cvar_Get ("gl_modulate_entities", "1", 0);
	gl_brightness = ri.Cvar_Get ("gl_brightness", "0", 0);
	gl_vertexlight = ri.Cvar_Get ("gl_vertexlight", "0", CVAR_ARCHIVE);
	//gl_log = ri.Cvar_Get( "gl_log", "0", 0 );
	gl_bitdepth = ri.Cvar_Get( "gl_bitdepth", "0", 0 );
	gl_mode = ri.Cvar_Get( "gl_mode", "3", CVAR_ARCHIVE );
	gl_lightmap = ri.Cvar_Get ("gl_lightmap", "0", 0);
	gl_shadows = ri.Cvar_Get ("gl_shadows", "0", CVAR_ARCHIVE );
	gl_dynamic = ri.Cvar_Get ("gl_dynamic", "1", 0);
	gl_lightgrid = ri.Cvar_Get ("gl_lightgrid", "1", CVAR_ARCHIVE);
	gl_batch_world = ri.Cvar_Get ("gl_batch_world", "1", CVAR_ARCHIVE);
	gl_vbo = ri.Cvar_Get ("gl_vbo", "1", CVAR_ARCHIVE);
	gl_nobind = ri.Cvar_Get ("gl_nobind", "0", 0);
	gl_round_down = ri.Cvar_Get ("gl_round_down", "0", 0);
	gl_picmip = ri.Cvar_Get ("gl_picmip", "0", CVAR_ARCHIVE);
	gl_skymip = ri.Cvar_Get ("gl_skymip", "0", 0);
	gl_showtris = ri.Cvar_Get ("gl_showtris", "0", 0);
	gl_showorigins = ri.Cvar_Get ("gl_showorigins", "0", 0);
	gl_showtearing = ri.Cvar_Get ("gl_showtearing", "0", 0);
	gl_showerrors = ri.Cvar_Get ("gl_showerrors", "1", 0);
	gl_debug = ri.Cvar_Get ("gl_debug", "0", 0);
	if (gl_debug->intvalue < 0)
		ri.Cvar_SetValue ("gl_debug", 0);
	else if (gl_debug->intvalue > 2)
		ri.Cvar_SetValue ("gl_debug", 2);
	gl_ztrick = ri.Cvar_Get ("gl_ztrick", "0", 0);
	gl_finish = ri.Cvar_Get ("gl_finish", "0", CVAR_ARCHIVE);
	gl_flush = ri.Cvar_Get ("gl_flush", "0", CVAR_ARCHIVE);
	gl_clear = ri.Cvar_Get ("gl_clear", "0", 0);
	gl_clearcolor = ri.Cvar_Get ("gl_clearcolor", "black", 0);
	gl_clearcolor->modified = true;
	r_clearcolor_dirty = true;
	gl_cull = ri.Cvar_Get ("gl_cull", "1", 0);
	gl_polyblend = ri.Cvar_Get ("gl_polyblend", "1", 0);
	gl_flashblend = ri.Cvar_Get ("gl_flashblend", "0", 0);
	//gl_playermip = ri.Cvar_Get ("gl_playermip", "0", 0);
	//gl_monolightmap = ri.Cvar_Get( "gl_monolightmap", "0", 0 );
	gl_driver = ri.Cvar_Get( "gl_driver", "opengl32", CVAR_ARCHIVE );
#ifdef R1Q2_GL_CORE
	gl_profile = ri.Cvar_Get ("gl_profile", "compatibility", CVAR_ARCHIVE);
#endif
	gl_texturemode = ri.Cvar_Get( "gl_texturemode", "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE );
	gl_texturealphamode = ri.Cvar_Get( "gl_texturealphamode", "default", CVAR_ARCHIVE );
	gl_texturesolidmode = ri.Cvar_Get( "gl_texturesolidmode", "default", CVAR_ARCHIVE );
	gl_lockpvs = ri.Cvar_Get( "gl_lockpvs", "0", 0 );

	gl_vertex_arrays = ri.Cvar_Get( "gl_vertex_arrays", "1", CVAR_ARCHIVE );
	gl_batch_models = ri.Cvar_Get ("gl_batch_models", "1", CVAR_ARCHIVE);
	GL_ShaderRegister ();
	GL_SkyRegister ();
	GL_PostFXRegister ();
	/* Register before the built-in menus filter unsupported settings. */
	GL_JumpOriginRegister ();
	ri.Cvar_Get ("gl_beamstyle", "1", 0);
	ri.Cvar_Get ("gl_lava_intensity", "1", 0);
	ri.Cvar_Get ("gl_lightmap_precision", "0", CVAR_ARCHIVE);
	gl_batch_2d = ri.Cvar_Get ("gl_batch_2d", "1", CVAR_ARCHIVE);
	cl_gunfov = ri.Cvar_Get ("cl_gunfov", "90", CVAR_ARCHIVE);
	cl_gunalpha = ri.Cvar_Get ("cl_gunalpha", "1", CVAR_ARCHIVE);
	r_adjustgunfov = ri.Cvar_Get ("cl_adjustfov", "1", CVAR_ARCHIVE);

	//gl_ext_swapinterval = ri.Cvar_Get( "gl_ext_swapinterval", "1", CVAR_ARCHIVE );
	//gl_ext_palettedtexture = ri.Cvar_Get( "gl_ext_palettedtexture", "0", CVAR_ARCHIVE );
	gl_ext_multitexture = ri.Cvar_Get( "gl_ext_multitexture", "1", CVAR_ARCHIVE );
	
	//note, pointparams moved to init to handle defaults
	//gl_ext_compiled_vertex_array = ri.Cvar_Get( "gl_ext_compiled_vertex_array", "1", CVAR_ARCHIVE );

	//r1ch: my extensions
	//gl_ext_generate_mipmap = ri.Cvar_Get ("gl_ext_generate_mipmap", "0", 0);
	gl_ext_point_sprite = ri.Cvar_Get ("gl_ext_point_sprite", "0", 0);
	gl_ext_texture_filter_anisotropic = ri.Cvar_Get ("gl_ext_texture_filter_anisotropic", "1", CVAR_ARCHIVE);
	gl_ext_texture_non_power_of_two = ri.Cvar_Get ("gl_ext_texture_non_power_of_two", "1", 0);
	GL_TextureOptionsRegister ();
	gl_ext_occlusion_query = ri.Cvar_Get ("gl_ext_occlusion_query", "0", 0);
	
	gl_ext_nv_multisample_filter_hint = ri.Cvar_Get ("gl_ext_nv_multisample_filter_hint", "fastest", 0);

	gl_colorbits = ri.Cvar_Get ("gl_colorbits", "0", 0);
	gl_stencilbits = ri.Cvar_Get ("gl_stencilbits", "", 0);
	gl_alphabits = ri.Cvar_Get ("gl_alphabits", "", 0);
	gl_depthbits = ri.Cvar_Get ("gl_depthbits", "", 0);

	gl_ext_multisample = ri.Cvar_Get ("gl_ext_multisample", "0", 0);
	gl_ext_samples = ri.Cvar_Get ("gl_ext_samples", "2", 0);
#ifdef _WIN32
	ri.Cvar_Get ("gl_multisamples", "", CVAR_ARCHIVE);
#endif

	gl_znear = ri.Cvar_Get ("gl_znear", "2", 0);
	gl_zfar = ri.Cvar_Get ("gl_zfar", "0", 0);
	gl_hudscale = ri.Cvar_Get ("gl_hudscale", "1", 0);

	cl_version = ri.Cvar_Get ("cl_version", REF_VERSION, CVAR_NOSET); 
	
	gl_r1gl_test = ri.Cvar_Get ("gl_r1gl_test", "0", 0);
	gl_doublelight_entities = ri.Cvar_Get ("gl_doublelight_entities", "1", 0);
	gl_noscrap = ri.Cvar_Get ("gl_noscrap", "1", 0);
	gl_overbrights = ri.Cvar_Get ("gl_overbrights", "0", 0);
	gl_linear_mipmaps = ri.Cvar_Get ("gl_linear_mipmaps", "0", 0);

	vid_forcedrefresh = ri.Cvar_Get ("vid_forcedrefresh", "0", 0);
	vid_optimalrefresh = ri.Cvar_Get ("vid_optimalrefresh", "0", 0);
	vid_gamma_pics = ri.Cvar_Get ("vid_gamma_pics", "0", 0);
	vid_nowgl = ri.Cvar_Get ("vid_nowgl", "0", 0);
	vid_restore_on_switch = ri.Cvar_Get ("vid_flip_on_switch", "0", 0);

	vid_modelist = ri.Cvar_Get ("vid_modelist", NULL, 0);
	vid_mode = ri.Cvar_Get ("vid_mode", "0", CVAR_ARCHIVE);
	vid_geometry = ri.Cvar_Get ("vid_geometry", "", CVAR_ARCHIVE);
	gl_forcewidth = ri.Cvar_Get ("vid_forcewidth", "0", CVAR_ARCHIVE);
	gl_forceheight = ri.Cvar_Get ("vid_forceheight", "0", CVAR_ARCHIVE);
	vid_borderless = ri.Cvar_Get ("vid_borderless", "0", CVAR_ARCHIVE);

	vid_topmost = ri.Cvar_Get ("vid_topmost", "0", 0);

	gl_pic_scale = ri.Cvar_Get ("gl_pic_scale", "1", 0);
	//r1ch end my shit

	gl_drawbuffer = ri.Cvar_Get( "gl_drawbuffer", "GL_BACK", 0 );
	gl_swapinterval = ri.Cvar_Get( "gl_swapinterval", "1", CVAR_ARCHIVE );

	//gl_saturatelighting = ri.Cvar_Get( "gl_saturatelighting", "0", 0 );

	gl_jpg_quality = ri.Cvar_Get ("gl_jpg_quality", "90", 0);
	gl_screenshot_format = ri.Cvar_Get ("gl_screenshot_format", "jpg", CVAR_ARCHIVE);
#ifdef _WIN32
	gl_screenshot_async = ri.Cvar_Get ("gl_screenshot_async", "1", CVAR_ARCHIVE);
#else
	gl_screenshot_async = ri.Cvar_Get ("gl_screenshot_async", "0", CVAR_ARCHIVE);
#endif
	gl_screenshot_quality = ri.Cvar_Get ("gl_screenshot_quality", "90", CVAR_ARCHIVE);
	gl_screenshot_compression = ri.Cvar_Get ("gl_screenshot_compression", "6", CVAR_ARCHIVE);
	gl_screenshot_template = ri.Cvar_Get ("gl_screenshot_template", "quakeXXX", CVAR_ARCHIVE);
	gl_coloredlightmaps = ri.Cvar_Get ("gl_coloredlightmaps", "1", 0);
	usingmodifiedlightmaps = (gl_coloredlightmaps->value != 1.0f);
	GL_LightingRegister ();

	//gl_3dlabs_broken = ri.Cvar_Get( "gl_3dlabs_broken", "1", CVAR_ARCHIVE );

	vid_fullscreen = ri.Cvar_Get( "vid_fullscreen", "0", CVAR_ARCHIVE );
	vid_gamma = ri.Cvar_Get( "vid_gamma", "1.0", CVAR_ARCHIVE );
	vid_ref = ri.Cvar_Get( "vid_ref", "r1gl", CVAR_ARCHIVE );

	gl_texture_formats = ri.Cvar_Get ("gl_texture_formats", "png jpg tga", 0);
	gl_pic_formats = ri.Cvar_Get ("gl_pic_formats", "png jpg tga", 0);
	r_override_textures = ri.Cvar_Get ("r_override_textures", "1", CVAR_ARCHIVE);
	r_texture_formats = ri.Cvar_Get ("r_texture_formats", "png jpg tga", CVAR_ARCHIVE);
	r_texture_overrides = ri.Cvar_Get ("r_texture_overrides", "-1", CVAR_ARCHIVE);

	load_png_wals = strstr (gl_texture_formats->string, "png") ? true : false;
	load_jpg_wals = strstr (gl_texture_formats->string, "jpg") ? true : false;
	load_tga_wals = strstr (gl_texture_formats->string, "tga") ? true : false;

	load_png_pics = strstr (gl_pic_formats->string, "png") ? true : false;
	load_jpg_pics = strstr (gl_pic_formats->string, "jpg") ? true : false;
	load_tga_pics = strstr (gl_pic_formats->string, "tga") ? true : false;

	gl_dlight_falloff = ri.Cvar_Get ("gl_dlight_falloff", "1", 0);
	gl_alphaskins = ri.Cvar_Get ("gl_alphaskins", "0", 0);
	gl_defertext = ri.Cvar_Get ("gl_defertext", "0", 0);
	defer_drawing = (int)gl_defertext->value;

	//con_alpha = ri.Cvar_Get ("con_alpha", "1.0", 0);

	ri.Cmd_AddCommand( "imagelist", GL_ImageList_f );
	ri.Cmd_AddCommand( "screenshot", GL_ScreenShot_f );
	ri.Cmd_AddCommand ("screenshotjpg", GL_ScreenShotJPG_f);
	ri.Cmd_AddCommand ("screenshotpng", GL_ScreenShotPNG_f);
	ri.Cmd_AddCommand ("screenshottga", GL_ScreenShotTGA_f);
	ri.Cmd_AddCommand( "modellist", Mod_Modellist_f );
	ri.Cmd_AddCommand( "gl_strings", GL_Strings_f );
	ri.Cmd_AddCommand( "hash_stats", Cmd_HashStats_f );
	

#ifdef R1GL_RELEASE
	ri.Cmd_AddCommand ("r1gl_version", GL_Version_f);
#endif
}

/*
==================
R_SetMode
==================
*/
int R_SetMode (void)
{
	int err;
	qboolean fullscreen;

	if (!vid_modelist)
		vid_modelist = ri.Cvar_Get ("vid_modelist", "desktop", 0);
	vid_modelist->modified = false;
	vid_mode->modified = false;
	vid_geometry->modified = false;
	fullscreen = FLOAT_EQ_ZERO(vid_fullscreen->value) ? false : true;

	vid_fullscreen->modified = false;
	gl_mode->modified = false;
	vid_borderless->modified = false;
	gl_forcewidth->modified = false;
	gl_forceheight->modified = false;

	if ( ( err = GLimp_SetMode( &vid.width, &vid.height, Q_ftol(gl_mode->value), fullscreen ) ) == VID_ERR_NONE )
	{
		gl_state.prev_mode = Q_ftol(gl_mode->value);
	}
	else
	{
		if ( err & VID_ERR_RETRY_QGL)
		{
			return err;
		}
		else if ( err & VID_ERR_FULLSCREEN_FAILED )
		{
			ri.Cvar_SetValue( "vid_fullscreen", 0);
			vid_fullscreen->modified = false;
			ri.Con_Printf( PRINT_ALL, "ref_gl::R_SetMode() - fullscreen unavailable in this mode\n" );
			if ( ( err = GLimp_SetMode( &vid.width, &vid.height, Q_ftol(gl_mode->value), false ) ) == VID_ERR_NONE )
				return VID_ERR_NONE;
		}
		else if (err & (VID_ERR_FAIL | VID_ERR_INVALID_MODE))
		{
			ri.Con_Printf( PRINT_ALL, "ref_gl::R_SetMode() - invalid mode\n" );
		}

		// Explicit geometry/list modes must not override the safe fallback.
		ri.Cvar_Set ("vid_geometry", "");
		ri.Cvar_SetValue ("vid_mode", 0);
		vid_geometry->modified = false;
		vid_mode->modified = false;

		// Forced dimensions must not override the safe windowed fallback.
		ri.Cvar_SetValue ("gl_mode", (float)gl_state.prev_mode);
		ri.Cvar_SetValue ("vid_forcewidth", 0);
		ri.Cvar_SetValue ("vid_forceheight", 0);
		ri.Cvar_SetValue ("vid_fullscreen", 0);
		gl_forcewidth->modified = false;
		gl_forceheight->modified = false;
		vid_fullscreen->modified = false;
		gl_mode->modified = false;

		// try setting it back to something safe
		if ( ( err = GLimp_SetMode( &vid.width, &vid.height, gl_state.prev_mode, false ) ) != VID_ERR_NONE )
		{
			ri.Con_Printf( PRINT_ALL, "ref_gl::R_SetMode() - could not revert to safe mode\n" );
			return VID_ERR_FAIL;
		}
	}
	return VID_ERR_NONE;
}

static qboolean GL_ProcAddressValid (size_t address)
{
	// wglGetProcAddress can return these sentinels for unavailable functions.
	return address > 3 && address != (size_t)-1;
}

/* Q2PRO 601a8df: optional driver messages, independent of glGetError checks. */
#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT 0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#define GL_DEBUG_SEVERITY_LOW 0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#endif

typedef void (APIENTRY *gl_debug_callback_t) (GLenum, GLenum, GLuint, GLenum,
	GLsizei, const char *, const void *);
static void (APIENTRY *gl_debug_callback) (gl_debug_callback_t, const void *);
static void (APIENTRY *gl_debug_control) (GLenum, GLenum, GLenum, GLsizei,
	const GLuint *, GLboolean);
static qboolean gl_debug_callback_active;

static void APIENTRY GL_DebugMessage (GLenum source, GLenum type, GLuint id,
	GLenum severity, GLsizei length, const char *message, const void *user)
{
	const char *label = "message";
	int level = PRINT_DEVELOPER;
	(void)source;
	(void)type;
	(void)id;
	(void)user;
	if (!message || length <= 0)
		return;
	switch (severity)
	{
	case GL_DEBUG_SEVERITY_HIGH:
		label = "error";
		level = PRINT_ALL;
		break;
	case GL_DEBUG_SEVERITY_MEDIUM:
		label = "warning";
		level = PRINT_ALL;
		break;
	case GL_DEBUG_SEVERITY_LOW:
		level = PRINT_ALL;
		break;
	}
	/* Respect the driver's length and bound the legacy console formatter's input. */
	if (length > 1023)
		length = 1023;
	ri.Con_Printf (level, "GL debug %s: %.*s\n", label, (int)length, message);
}

static void GL_DebugShutdown (void)
{
	if (gl_debug_callback_active)
		gl_debug_callback (NULL, NULL);
	gl_debug_callback_active = false;
	gl_debug_callback = NULL;
	gl_debug_control = NULL;
}

static void GL_DebugInit (void)
{
	int major = 0, minor = 0;
	const char *extensions = gl_config.extensions_string, *p;
	GL_DebugShutdown ();
	gl_debug->modified = false;
#ifdef _WIN32
	if (!qwglGetProcAddress)
		return;
#endif
	if (!gl_config.version_string ||
		sscanf (gl_config.version_string, "%d.%d", &major, &minor) != 2 ||
		major < 1 || minor < 0)
		return;
	if (major < 4 || (major == 4 && minor < 3))
	{
		if (!extensions)
			return;
		for (p = extensions; (p = strstr (p, "GL_KHR_debug")) != NULL; p += 12)
			if ((p == extensions || p[-1] == ' ') && (p[12] == ' ' || !p[12]))
				break;
		if (!p)
			return;
	}
	gl_debug_callback = (void (APIENTRY *) (gl_debug_callback_t, const void *))
		qwglGetProcAddress ("glDebugMessageCallback");
	gl_debug_control = (void (APIENTRY *) (GLenum, GLenum, GLenum, GLsizei, const GLuint *, GLboolean))
		qwglGetProcAddress ("glDebugMessageControl");
	if (!GL_ProcAddressValid ((size_t)gl_debug_callback) ||
		!GL_ProcAddressValid ((size_t)gl_debug_control))
	{
		GL_DebugShutdown ();
		if (gl_debug->intvalue)
			ri.Con_Printf (PRINT_ALL, "OpenGL debug output unavailable: missing entry points.\n");
		return;
	}
	if (!qglIsEnabled (GL_DEBUG_OUTPUT))
		return;
	ri.Con_Printf (PRINT_ALL, "Enabling GL debug output.\n");
	qglEnable (GL_DEBUG_OUTPUT_SYNCHRONOUS);
	if (gl_debug->intvalue < 2)
		gl_debug_control (GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
	gl_debug_callback (GL_DebugMessage, NULL);
	gl_debug_callback_active = true;
}

static void GL_InitMultitexture (void)
{
	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	qglSelectTextureSGIS = NULL;
	qglMTexCoord2fSGIS = NULL;
	qglMTexCoord2fvSGIS = NULL;
	GL_TEXTURE0 = GL_TEXTURE0_ARB;
	GL_TEXTURE1 = GL_TEXTURE1_ARB;

	if (strstr (gl_config.extensions_string, "GL_ARB_multitexture") &&
		FLOAT_NE_ZERO (gl_ext_multitexture->value))
	{
		qglMTexCoord2fSGIS = (void (APIENTRY *) (GLenum, GLfloat, GLfloat))
			qwglGetProcAddress ("glMultiTexCoord2fARB");
		qglMTexCoord2fvSGIS = (void (APIENTRY *) (GLenum, GLfloat *))
			qwglGetProcAddress ("glMultiTexCoord2fvARB");
		qglActiveTextureARB = (void (APIENTRY *) (GLenum))
			qwglGetProcAddress ("glActiveTextureARB");
		qglClientActiveTextureARB = (void (APIENTRY *) (GLenum))
			qwglGetProcAddress ("glClientActiveTextureARB");

		if (GL_ProcAddressValid ((size_t)qglMTexCoord2fSGIS) &&
			GL_ProcAddressValid ((size_t)qglMTexCoord2fvSGIS) &&
			GL_ProcAddressValid ((size_t)qglActiveTextureARB) &&
			GL_ProcAddressValid ((size_t)qglClientActiveTextureARB))
			ri.Con_Printf (PRINT_ALL, "...using GL_ARB_multitexture\n");
		else
		{
			qglMTexCoord2fSGIS = NULL;
			qglMTexCoord2fvSGIS = NULL;
			qglActiveTextureARB = NULL;
			qglClientActiveTextureARB = NULL;
			ri.Con_Printf (PRINT_ALL,
				"...incomplete GL_ARB_multitexture; trying legacy path\n");
		}
	}

	if (!qglActiveTextureARB &&
		strstr (gl_config.extensions_string, "GL_SGIS_multitexture") &&
		FLOAT_NE_ZERO (gl_ext_multitexture->value))
	{
		qglMTexCoord2fSGIS = (void (APIENTRY *) (GLenum, GLfloat, GLfloat))
			qwglGetProcAddress ("glMTexCoord2fSGIS");
		qglMTexCoord2fvSGIS = (void (APIENTRY *) (GLenum, GLfloat *))
			qwglGetProcAddress ("glMTexCoord2fvSGIS");
		qglSelectTextureSGIS = (void (APIENTRY *) (GLenum))
			qwglGetProcAddress ("glSelectTextureSGIS");

		if (GL_ProcAddressValid ((size_t)qglMTexCoord2fSGIS) &&
			GL_ProcAddressValid ((size_t)qglMTexCoord2fvSGIS) &&
			GL_ProcAddressValid ((size_t)qglSelectTextureSGIS))
		{
			GL_TEXTURE0 = GL_TEXTURE0_SGIS;
			GL_TEXTURE1 = GL_TEXTURE1_SGIS;
			ri.Con_Printf (PRINT_ALL, "...using GL_SGIS_multitexture\n");
		}
		else
		{
			qglMTexCoord2fSGIS = NULL;
			qglMTexCoord2fvSGIS = NULL;
			qglSelectTextureSGIS = NULL;
			ri.Con_Printf (PRINT_ALL,
				"...incomplete GL_SGIS_multitexture; using single texture\n");
		}
	}
}

static qboolean GL_BufferProcsAvailable (void)
{
	return GL_ProcAddressValid ((size_t)qglGenBuffers) &&
		GL_ProcAddressValid ((size_t)qglDeleteBuffers) &&
		GL_ProcAddressValid ((size_t)qglBindBuffer) &&
		GL_ProcAddressValid ((size_t)qglBufferData) &&
		GL_ProcAddressValid ((size_t)qglBufferSubData) &&
		GL_ProcAddressValid ((size_t)qglGetBufferParameteriv);
}

static void GL_ClearBufferProcs (void)
{
	qglGenBuffers = NULL;
	qglDeleteBuffers = NULL;
	qglBindBuffer = NULL;
	qglBufferData = NULL;
	qglBufferSubData = NULL;
	qglGetBufferParameteriv = NULL;
}

static void GL_InitBuffers (void)
{
	int major = 0, minor = 0;

	GL_ClearBufferProcs ();
	if (gl_config.version_string)
		sscanf (gl_config.version_string, "%d.%d", &major, &minor);

	if (major > 1 || (major == 1 && minor >= 5))
	{
		qglGenBuffers = (void (APIENTRY *) (GLsizei, GLuint *))
			qwglGetProcAddress ("glGenBuffers");
		qglDeleteBuffers = (void (APIENTRY *) (GLsizei, const GLuint *))
			qwglGetProcAddress ("glDeleteBuffers");
		qglBindBuffer = (void (APIENTRY *) (GLenum, GLuint))
			qwglGetProcAddress ("glBindBuffer");
		qglBufferData = (void (APIENTRY *) (GLenum, GLsizeiptr, const GLvoid *, GLenum))
			qwglGetProcAddress ("glBufferData");
		qglBufferSubData = (void (APIENTRY *) (GLenum, GLintptr, GLsizeiptr, const GLvoid *))
			qwglGetProcAddress ("glBufferSubData");
		qglGetBufferParameteriv = (void (APIENTRY *) (GLenum, GLenum, GLint *))
			qwglGetProcAddress ("glGetBufferParameteriv");
		if (GL_BufferProcsAvailable ())
		{
			ri.Con_Printf (PRINT_ALL, "...using core vertex buffer objects\n");
			return;
		}
	}

	GL_ClearBufferProcs ();
	if (strstr (gl_config.extensions_string, "GL_ARB_vertex_buffer_object"))
	{
		qglGenBuffers = (void (APIENTRY *) (GLsizei, GLuint *))
			qwglGetProcAddress ("glGenBuffersARB");
		qglDeleteBuffers = (void (APIENTRY *) (GLsizei, const GLuint *))
			qwglGetProcAddress ("glDeleteBuffersARB");
		qglBindBuffer = (void (APIENTRY *) (GLenum, GLuint))
			qwglGetProcAddress ("glBindBufferARB");
		qglBufferData = (void (APIENTRY *) (GLenum, GLsizeiptr, const GLvoid *, GLenum))
			qwglGetProcAddress ("glBufferDataARB");
		qglBufferSubData = (void (APIENTRY *) (GLenum, GLintptr, GLsizeiptr, const GLvoid *))
			qwglGetProcAddress ("glBufferSubDataARB");
		qglGetBufferParameteriv = (void (APIENTRY *) (GLenum, GLenum, GLint *))
			qwglGetProcAddress ("glGetBufferParameterivARB");
		if (GL_BufferProcsAvailable ())
		{
			ri.Con_Printf (PRINT_ALL, "...using ARB vertex buffer objects\n");
			return;
		}
	}

	GL_ClearBufferProcs ();
	ri.Con_Printf (PRINT_ALL, "...vertex buffers unavailable; using CPU geometry\n");
}

/*
===============
R_Init
===============
*/
int EXPORT R_Init( void *hinstance, void *hWnd )
{	
	char renderer_buffer[1000];
	char vendor_buffer[1000];
	int		err;
	int		j;
	extern float r_turbsin[256];
	static qboolean turbsin_initialized;

	/* A built-in renderer survives vid_restart, unlike an unloaded DLL. */
	GL_DebugShutdown ();
	R_ClearAlphaSurfaces ();
	R_AccountShutdown ();
	R_GPUPerfShutdown ();
	GL_ShutdownPostFX ();
	GL_ShutdownSky ();
	GL_ShutdownShaders ();
	GL_FreeWorldBuffer (NULL);
	GL_ResetWorldBatches ();

	if (!turbsin_initialized)
	{
		for (j = 0; j < 256; j++)
		{
			r_turbsin[j] *= 0.5;
		}
		turbsin_initialized = true;
	}

#ifdef REF_HARD_LINKED
	memset (&gl_config, 0, sizeof (gl_config));
	memset (&gl_state, 0, sizeof (gl_state));
	r_worldmodel = NULL;
	currentmodel = NULL;
	currententity = NULL;
	r_framecount = r_visframecount = 0;
	r_viewcluster = r_viewcluster2 = r_oldviewcluster = r_oldviewcluster2 = -1;
#endif

	ri.Cmd_ExecuteText (EXEC_NOW, "exec r1gl.cfg\n");

	ri.Con_Printf (PRINT_ALL, "ref_gl version: "REF_VERSION"\n");

	ri.Con_Printf (PRINT_DEVELOPER, "Draw_GetPalette()\n");
	Draw_GetPalette ();

	ri.Con_Printf (PRINT_DEVELOPER, "R_Register()\n");
	R_Register();

	gl_overbrights->modified = false;

retryQGL:

	// initialize our QGL dynamic bindings
	ri.Con_Printf (PRINT_DEVELOPER, "QGL_Init()\n");
	if ( !QGL_Init( gl_driver->string ) )
	{
		QGL_Shutdown();

		ri.Con_Printf (PRINT_ALL, "ref_gl::R_Init() - could not load \"%s\"\n", gl_driver->string );

#ifdef _WIN32
		if (strcmp (gl_driver->string, "opengl32"))
		{
			ri.Con_Printf (PRINT_ALL, "ref_gl::R_Init() - retrying with gl_driver opengl32\n");
			ri.Cvar_Set ("gl_driver", "opengl32");
			goto retryQGL;
		}
#endif
        
		return -1;
	}

	// initialize OS-specific parts of OpenGL
	ri.Con_Printf (PRINT_DEVELOPER, "GLimp_Init()\n");
	if ( !GLimp_Init( hinstance, hWnd ) )
	{
		ri.Con_Printf (PRINT_ALL, "ref_gl::R_Init(): GLimp_Init() failed\n");
		QGL_Shutdown();
		return -1;
	}

	// set our "safe" modes
	gl_state.prev_mode = 3;

	// create the window and set up the context
	ri.Con_Printf (PRINT_DEVELOPER, "R_SetMode()\n");
	err = R_SetMode ();
	if (err != VID_ERR_NONE)
	{
		QGL_Shutdown();
		if (err & VID_ERR_RETRY_QGL)
			goto retryQGL;

        ri.Con_Printf (PRINT_ALL, "ref_gl::R_Init() - could not R_SetMode()\n" );
		return -1;
	}

#ifdef R1Q2_GL_CORE
	if (!GL_CoreInit ())
	{
		GLimp_Shutdown ();
		QGL_Shutdown ();
		ri.Con_Printf (PRINT_ALL, "Core backend failed; restarting with compatibility OpenGL.\n");
		ri.Cvar_Set ("gl_profile", "compatibility");
		goto retryQGL;
	}
	gl_profile->modified = false;
#endif

	ri.Con_Printf (PRINT_DEVELOPER, "Vid_MenuInit()\n");
	ri.Vid_MenuInit();

	/*
	** get our various GL strings
	*/
	gl_config.vendor_string = (const char *) qglGetString (GL_VENDOR);
	ri.Con_Printf (PRINT_ALL, "GL_VENDOR: %s\n", gl_config.vendor_string );
	gl_config.renderer_string = (const char *) qglGetString (GL_RENDERER);
	ri.Con_Printf (PRINT_ALL, "GL_RENDERER: %s\n", gl_config.renderer_string );
	gl_config.version_string = (const char *) qglGetString (GL_VERSION);
	ri.Con_Printf (PRINT_ALL, "GL_VERSION: %s\n", gl_config.version_string );
	gl_config.extensions_string = (const char *) qglGetString (GL_EXTENSIONS);
	GL_DebugInit ();
	qglGetIntegerv (GL_MAX_TEXTURE_SIZE, &gl_config.max_texture_size);
	ri.Con_Printf (PRINT_ALL, "GL_MAX_TEXTURE_SIZE: %d\n", gl_config.max_texture_size);
	//ri.Con_Printf (PRINT_ALL, "GL_EXTENSIONS: %s\n", gl_config.extensions_string );

	Q_strncpy( renderer_buffer, gl_config.renderer_string, sizeof(renderer_buffer)-1);
	Q_strlwr( renderer_buffer );

	Q_strncpy( vendor_buffer, gl_config.vendor_string, sizeof(vendor_buffer)-1);
	Q_strlwr( vendor_buffer );

	if ( strstr( renderer_buffer, "voodoo" ) )
	{
		if ( !strstr( renderer_buffer, "rush" ) )
			gl_config.renderer = GL_RENDERER_VOODOO;
		else
			gl_config.renderer = GL_RENDERER_VOODOO_RUSH;
	}
	else if ( strstr( vendor_buffer, "sgi" ) )
		gl_config.renderer = GL_RENDERER_SGI;
	else if ( strstr( renderer_buffer, "permedia" ) )
		gl_config.renderer = GL_RENDERER_PERMEDIA2;
	else if ( strstr( renderer_buffer, "glint" ) )
		gl_config.renderer = GL_RENDERER_GLINT_MX;
	else if ( strstr( renderer_buffer, "glzicd" ) )
		gl_config.renderer = GL_RENDERER_REALIZM;
	else if ( strstr( renderer_buffer, "gdi" ) )
		gl_config.renderer = GL_RENDERER_MCD;
	else if ( strstr( renderer_buffer, "pcx2" ) )
		gl_config.renderer = GL_RENDERER_PCX2;
	else if ( strstr( renderer_buffer, "verite" ) )
		gl_config.renderer = GL_RENDERER_RENDITION;
	else if ( strstr (vendor_buffer, "ati tech"))
		gl_config.renderer = GL_RENDERER_ATI;
	else if ( strstr (vendor_buffer, "nvidia corp"))
		gl_config.renderer = GL_RENDERER_NV;
	else
		gl_config.renderer = GL_RENDERER_OTHER;

	/*if ( toupper( gl_monolightmap->string[1] ) != 'F' )
	{
		if ( gl_config.renderer == GL_RENDERER_PERMEDIA2 )
		{
			ri.Cvar_Set( "gl_monolightmap", "A" );
			ri.Con_Printf( PRINT_ALL, "...using gl_monolightmap 'a'\n" );
		}
		else if ( gl_config.renderer & GL_RENDERER_POWERVR ) 
		{
			ri.Cvar_Set( "gl_monolightmap", "0" );
		}
		else
		{
			ri.Cvar_Set( "gl_monolightmap", "0" );
		}
	}*/

	// power vr can't have anything stay in the framebuffer, so
	// the screen needs to redraw the tiled background every frame
	if ( gl_config.renderer & GL_RENDERER_POWERVR ) 
	{
		ri.Cvar_Set( "scr_drawall", "1" );
	}
	else
	{
		ri.Cvar_Set( "scr_drawall", "0" );
	}

	// MCD has buffering issues
	if ( gl_config.renderer == GL_RENDERER_MCD )
	{
		ri.Cvar_SetValue( "gl_finish", 1 );
	}

	/*
	** grab extensions
	*/
	if ( strstr( gl_config.extensions_string, "GL_EXT_compiled_vertex_array" ) || 
		 strstr( gl_config.extensions_string, "GL_SGI_compiled_vertex_array" ) )
	{
		ri.Con_Printf( PRINT_ALL, "...enabling GL_EXT_compiled_vertex_array\n" );
		qglLockArraysEXT = ( void (__stdcall *)(int, int) ) qwglGetProcAddress( "glLockArraysEXT" );
		qglUnlockArraysEXT = ( void (__stdcall *)(void) ) qwglGetProcAddress( "glUnlockArraysEXT" );
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
	}

#ifdef _WIN32
	GLimp_InitSwapControl ();
#endif

	if ( strstr( gl_config.extensions_string, "GL_EXT_point_parameters" ) )
	{
		if (gl_config.renderer == GL_RENDERER_ATI)
			gl_ext_pointparameters = ri.Cvar_Get( "gl_ext_pointparameters", "0", CVAR_ARCHIVE );
		else
			gl_ext_pointparameters = ri.Cvar_Get( "gl_ext_pointparameters", "1", CVAR_ARCHIVE );

		if ( FLOAT_NE_ZERO(gl_ext_pointparameters->value) && (gl_config.renderer != GL_RENDERER_ATI || gl_ext_pointparameters->value == 2) )
		{
			qglPointParameterfEXT = ( void (APIENTRY *)( GLenum, GLfloat ) ) qwglGetProcAddress( "glPointParameterfEXT" );
			qglPointParameterfvEXT = ( void (APIENTRY *)( GLenum, const GLfloat * ) ) qwglGetProcAddress( "glPointParameterfvEXT" );
			ri.Con_Printf( PRINT_ALL, "...using GL_EXT_point_parameters\n" );
		}
		else
		{
			ri.Con_Printf( PRINT_ALL, "...ignoring GL_EXT_point_parameters\n" );
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_EXT_point_parameters not found\n" );
	}

	/*if ( !qglColorTableEXT &&
		strstr( gl_config.extensions_string, "GL_EXT_paletted_texture" ) && 
		strstr( gl_config.extensions_string, "GL_EXT_shared_texture_palette" ) )
	{
		if ( gl_ext_palettedtexture->value )
		{
			ri.Con_Printf( PRINT_ALL, "...using GL_EXT_shared_texture_palette\n" );
			qglColorTableEXT = ( void ( APIENTRY * ) ( int, int, int, int, int, const void * ) ) qwglGetProcAddress( "glColorTableEXT" );
		}
		else
		{
			ri.Con_Printf( PRINT_ALL, "...ignoring GL_EXT_shared_texture_palette\n" );
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_EXT_shared_texture_palette not found\n" );
	}*/

	GL_InitMultitexture ();
	GL_InitBuffers ();
	GL_InitShaders ();
	GL_InitPostFX ();
	R_GPUPerfInit ();

	ri.Con_Printf( PRINT_ALL, "Initializing r1gl extensions:\n" );

	/*gl_config.r1gl_GL_SGIS_generate_mipmap = false;
	if ( strstr( gl_config.extensions_string, "GL_SGIS_generate_mipmap" ) ) {
		if ( gl_ext_generate_mipmap->value ) {
			ri.Con_Printf( PRINT_ALL, "...using GL_SGIS_generate_mipmap\n" );
			gl_config.r1gl_GL_SGIS_generate_mipmap = true;
		} else {
			ri.Con_Printf( PRINT_ALL, "...ignoring GL_SGIS_generate_mipmap\n" );		
		}
	} else {
		ri.Con_Printf( PRINT_ALL, "...GL_SGIS_generate_mipmap not found\n" );
	}*/

	gl_config.r1gl_GL_ARB_point_sprite = false;
	if ( strstr( gl_config.extensions_string, "GL_ARB_point_sprite" ) )
	{
		//if ( gl_ext_point_sprite->value ) {
			qglPointParameterfARB = (void (__stdcall *)(GLenum,GLfloat))qwglGetProcAddress("glPointParameterfARB");
			qglPointParameterfvARB = (void (__stdcall *)(GLenum,const GLfloat *))qwglGetProcAddress("glPointParameterfvARB");
			if (!qglPointParameterfARB)
			{
				ri.Con_Printf( PRINT_ALL, "!!! qglGetProcAddress for GL_ARB_point_sprite failed\n" );
			}
			else
			{
				ri.Con_Printf( PRINT_ALL, "...using GL_ARB_point_sprite\n" );
				gl_config.r1gl_GL_ARB_point_sprite = true;
			}
		//} else {
		//	ri.Con_Printf( PRINT_ALL, "...ignoring GL_ARB_point_sprite\n" );		
		//}
	} else {
		ri.Con_Printf( PRINT_ALL, "...GL_ARB_point_sprite not found\n" );
	}

	gl_config.r1gl_GL_EXT_texture_filter_anisotropic = false;
	if (GL_TextureAnisotropySupported ())
	{
		if ( gl_ext_texture_filter_anisotropic->value ) {
			ri.Con_Printf (PRINT_ALL, "...using anisotropic texture filtering\n");
			gl_config.r1gl_GL_EXT_texture_filter_anisotropic = true;
		} else {
			ri.Con_Printf (PRINT_ALL, "...ignoring anisotropic texture filtering\n");
		}
	} else {
		ri.Con_Printf (PRINT_ALL, "...anisotropic texture filtering not available\n");
	}
	gl_ext_texture_filter_anisotropic->modified = false;

	gl_config.r1gl_GL_ARB_texture_non_power_of_two = false;
	if (GL_TextureNPOTSupported ())
	{
		if (FLOAT_NE_ZERO (gl_ext_texture_non_power_of_two->value) ) {
			ri.Con_Printf (PRINT_ALL, "...using non-power-of-two textures\n");
			gl_config.r1gl_GL_ARB_texture_non_power_of_two = true;
		} else {
			ri.Con_Printf (PRINT_ALL, "...ignoring non-power-of-two textures\n");
		}
	}
	else
	{
		ri.Con_Printf (PRINT_ALL, "...non-power-of-two textures not available\n");
	}

	if ( strstr (gl_config.extensions_string, "GL_ARB_occlusion_query"))
	{
		//r1: occlusion queries
		if (FLOAT_NE_ZERO (gl_ext_occlusion_query->value) )
		{
			qglGenQueriesARB			 = (void (__stdcall *)(GLsizei,GLuint *))qwglGetProcAddress ("glGenQueriesARB");
			qglGetQueryivARB			 = (void (__stdcall *)(GLenum,GLenum,GLint *))qwglGetProcAddress ("glGetQueryivARB");
			qglGetQueryObjectivARB		 = (void (__stdcall *)(GLuint,GLenum,GLint *))qwglGetProcAddress ("glGetQueryObjectivARB");
			qglBeginQueryARB			 = (void (__stdcall *)(GLenum,GLuint))qwglGetProcAddress ("glBeginQueryARB");
			qglEndQueryARB				 = (void (__stdcall *)(GLenum))qwglGetProcAddress ("glEndQueryARB");

			qglGetQueryivARB (GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, &gl_config.r1gl_QueryBits);
			ri.Con_Printf (PRINT_ALL, "...using GL_ARB_occlusion_query (%d bits)\n", gl_config.r1gl_QueryBits);
			if (gl_config.r1gl_QueryBits)
				qglGenQueriesARB (MAX_ENTITIES, gl_config.r1gl_Queries);
		}
		else
		{
			ri.Con_Printf (PRINT_ALL, "...ignoring GL_ARB_occlusion_query\n");
			gl_config.r1gl_QueryBits = 0;
		}
	}
	else
	{
		gl_config.r1gl_QueryBits = 0;
		ri.Con_Printf (PRINT_ALL, "...GL_ARB_occlusion_query not found\n");
	}

	ri.Con_Printf( PRINT_ALL, "Initializing r1gl NVIDIA-only extensions:\n" );
	gl_config.r1gl_GL_EXT_nv_multisample_filter_hint = false;
	if ( strstr( gl_config.extensions_string, "GL_NV_multisample_filter_hint" ) ) {
		gl_config.r1gl_GL_EXT_nv_multisample_filter_hint = true;	
		ri.Con_Printf( PRINT_ALL, "...allowing GL_NV_multisample_filter_hint\n" );
	} else {
		ri.Con_Printf( PRINT_ALL, "...GL_NV_multisample_filter_hint not found\n" );
	}

	ri.Con_Printf( PRINT_DEVELOPER, "GL_SetDefaultState()\n" );
	GL_SetDefaultState();
	R_RestoreClearColor ();

	//r1: setup cached screensizes
	vid_scaled_width = vid.width / gl_hudscale->value;
	vid_scaled_height = vid.height / gl_hudscale->value;

	/*
	** draw our stereo patterns
	*/
#if 0 // commented out until H3D pays us the money they owe us
	GL_DrawStereoPattern();
#endif

	ri.Con_Printf( PRINT_DEVELOPER, "GL_InitImages()\n" );
	GL_InitTextureOptions ();
	GL_InitImages ();

	ri.Con_Printf( PRINT_DEVELOPER, "Mod_Init()\n" );
	Mod_Init ();

	ri.Con_Printf( PRINT_DEVELOPER, "R_InitParticleTexture()\n" );
	R_InitParticleTexture ();

	ri.Con_Printf( PRINT_DEVELOPER, "Draw_InitLocal()\n" );
	Draw_InitLocal ();
	R_SetPalette (NULL);

	GL_ShowErrors ("R_Init");

	ri.Con_Printf( PRINT_DEVELOPER, "R_Init() complete.\n" );
	return 0;
}

/*
===============
R_Shutdown
===============
*/
void EXPORT R_Shutdown (void)
{
	GL_DebugShutdown ();
	R_PerfReset ();
	R_GPUPerfShutdown ();
	GL_ScreenshotShutdown ();
	Draw_Reset ();
	GL_FreeWorldBuffer (NULL);
	GL_ResetWorldBatches ();
	GL_FreeStaticLightmapBackings ();
	ri.Cmd_RemoveCommand ("modellist");
	ri.Cmd_RemoveCommand ("screenshot");
	ri.Cmd_RemoveCommand ("screenshotjpg");
	ri.Cmd_RemoveCommand ("screenshotpng");
	ri.Cmd_RemoveCommand ("screenshottga");
	ri.Cmd_RemoveCommand ("imagelist");
	ri.Cmd_RemoveCommand ("gl_strings");
	ri.Cmd_RemoveCommand ("hash_stats");

#ifdef R1GL_RELEASE
	ri.Cmd_RemoveCommand ("r1gl_version");
#endif

	Mod_FreeAll ();

	GL_ShutdownPostFX ();
	GL_ShutdownSky ();
	GL_ShutdownShaders ();
	R_AccountShutdown ();
	GL_ShutdownImages ();
#ifdef R1Q2_GL_CORE
	GL_CoreShutdown ();
#endif

	/*
	** shut down OS specific OpenGL stuff like contexts, etc.
	*/
	GLimp_Shutdown();

	/*
	** shutdown our QGL subsystem
	*/
	QGL_Shutdown();
}

/*
** Physical drawable size and the existing scaled client coordinate space.
** Keep the scaled notification within the retained Vid_NewWindow bounds.
*/
void R_UpdateDrawableSize (int width, int height)
{
	float scale, max_scale;
	int scaled_width, scaled_height;

	if (width < 64 || width >= 0x10000 || height < 48 || height >= 0x10000)
		return;

	Draw_Flush ();
	vid.width = width;
	vid.height = height;
	scale = gl_hudscale->value;
	max_scale = width / 64.0f;
	if (height / 48.0f < max_scale)
		max_scale = height / 48.0f;
	if (!(scale >= 1.0f))
		scale = 1.0f;
	else if (scale > max_scale)
		scale = max_scale;
	if (scale != gl_hudscale->value)
		ri.Cvar_SetValue ("gl_hudscale", scale);
	gl_hudscale->modified = false;

	vid_scaled_width = width / (double)scale;
	vid_scaled_height = height / (double)scale;
	scaled_width = ((int)ceil (vid_scaled_width) + 7) & ~7;
	scaled_height = ((int)ceil (vid_scaled_height) + 1) & ~1;
	if (scaled_width > 0xfff8)
		scaled_width = 0xfff8;
	if (scaled_height > 0xfffe)
		scaled_height = 0xfffe;
	ri.Vid_NewWindow (scaled_width, scaled_height);
}

/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginFrame
@@@@@@@@@@@@@@@@@@@@@
*/
void EXPORT R_BeginFrame( float camera_separation )
{
	R_MD5PerfBeginFrame ();
	R_GPUPerfBeginFrame ();
	R_PerfBeginFrame ();
	R_AccountBeginFrame ();
	GL_ScreenshotPoll ();
	Draw_BeginFrame ();
#ifdef STEREO_SUPPORT
	gl_state.camera_separation = camera_separation;
#endif

	/*
	** change modes if necessary
	*/
	if (
		gl_debug->modified ||
#ifdef R1Q2_GL_CORE
		gl_profile->modified ||
#endif
		gl_mode->modified || vid_fullscreen->modified || vid_borderless->modified ||
		gl_forcewidth->modified || gl_forceheight->modified || vid_mode->modified ||
		vid_geometry->modified || (vid_mode->intvalue && vid_modelist->modified))
	{	// FIXME: only restart if CDS is required
		cvar_t	*ref;

		ref = ri.Cvar_Get ("vid_ref", "r1gl", 0);
		ref->modified = true;
	}

	/*if ( gl_log->modified )
	{
		GLimp_EnableLogging( gl_log->value );
		gl_log->modified = false;
	}

	if ( gl_log->value )
	{
		GLimp_LogNewFrame();
	}*/

	if (gl_ext_nv_multisample_filter_hint->modified)
	{
		gl_ext_nv_multisample_filter_hint->modified = false;

		if (gl_config.r1gl_GL_EXT_nv_multisample_filter_hint)
		{
			if (!strcmp (gl_ext_nv_multisample_filter_hint->string, "nicest"))
				qglHint(GL_MULTISAMPLE_FILTER_HINT_NV, GL_NICEST);
			else
				qglHint(GL_MULTISAMPLE_FILTER_HINT_NV, GL_FASTEST);
		}
	}

	if (gl_contrast->modified)
	{
		if (gl_contrast->value < 0.5f)
			ri.Cvar_SetValue ("gl_contrast", 0.5f);
		else if (gl_contrast->value > 1.5f)
			ri.Cvar_SetValue ("gl_contrast", 1.5f);

		gl_contrast->modified = false;
	}



	/*
	** update 3Dfx gamma -- it is expected that a user will do a vid_restart
	** after tweaking this value
	*/
#if 0
	if ( vid_gamma->modified )
	{
		vid_gamma->modified = false;

		if ( gl_config.renderer & ( GL_RENDERER_VOODOO ) )
		{
			char envbuffer[1024];
			float g;

			g = 2.00 * ( 0.8 - ( vid_gamma->value - 0.5 ) ) + 1.0F;
			Com_sprintf( envbuffer, sizeof(envbuffer), "SSTV2_GAMMA=%f", g );
			putenv( envbuffer );
			Com_sprintf( envbuffer, sizeof(envbuffer), "SST_GAMMA=%f", g );
			putenv( envbuffer );
		}
	}
#endif

#ifdef STEREO_SUPPORT
	GLimp_BeginFrame( camera_separation );
#else
	GLimp_BeginFrame ();
#endif

	if (gl_hudscale->modified)
		R_UpdateDrawableSize (vid.width, vid.height);

	GL_UpdateGamma ();
	GL_UpdateWorldBuffer ();
	GL_UpdateShaderOptions ();
	GL_UpdateLighting ();
	GL_UpdateSky ();
	GL_UpdateTextureOptions ();
	GL_UpdatePostFX ();

	/*
	** go into 2D mode
	*/
	qglViewport (0,0, vid.width, vid.height);
	qglMatrixMode(GL_PROJECTION);
    qglLoadIdentity ();
	//qglOrtho  (0, vid.width, vid.height, 0, -99999, 99999);
	qglOrtho(0, vid_scaled_width, vid_scaled_height, 0, -99999, 99999);
	qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity ();
	//qglDisable (GL_DEPTH_TEST);
	//GLPROFqglDisable (GL_CULL_FACE);
	//GLPROFqglDisable (GL_BLEND);
	//GLPROFqglEnable (GL_ALPHA_TEST);
	qglColor4fv(colorWhite);

	//qglEnable(GL_MULTISAMPLE_ARB);

	/*
	** draw buffer stuff
	*/
	if ( gl_drawbuffer->modified )
	{
		gl_drawbuffer->modified = false;

#ifdef STEREO_SUPPORT
		if ( gl_state.camera_separation == 0 || !gl_state.stereo_enabled )
#endif
		{
			if ( Q_stricmp( gl_drawbuffer->string, "GL_FRONT" ) == 0 )
				qglDrawBuffer( GL_FRONT );
			else
				qglDrawBuffer( GL_BACK );
		}
	}

	/*
	** texturemode stuff
	*/
	if ( gl_texturemode->modified )
	{
		GL_TextureMode( gl_texturemode->string );
		gl_texturemode->modified = false;
	}

	if (gl_ext_texture_filter_anisotropic->modified)
	{
		gl_config.r1gl_GL_EXT_texture_filter_anisotropic = false;
		if (GL_TextureAnisotropySupported ())
		{
			if ( gl_ext_texture_filter_anisotropic->value )
			{
				ri.Con_Printf (PRINT_ALL, "...using anisotropic texture filtering\n");
				gl_config.r1gl_GL_EXT_texture_filter_anisotropic = true;
				GL_UpdateAnisotropy ();
			}
			else
			{
				ri.Con_Printf (PRINT_ALL, "...ignoring anisotropic texture filtering\n");
				GL_UpdateAnisotropy ();
			}
		}
		else
		{
			ri.Con_Printf (PRINT_ALL, "...anisotropic texture filtering not available\n");
		}
		
		gl_ext_texture_filter_anisotropic->modified = false;
	}


#if 0
	if ( gl_texturealphamode->modified )
	{
		GL_TextureAlphaMode( gl_texturealphamode->string );
		gl_texturealphamode->modified = false;
	}

	if ( gl_texturesolidmode->modified )
	{
		GL_TextureSolidMode( gl_texturesolidmode->string );
		gl_texturesolidmode->modified = false;
	}
#endif

	if (gl_texture_formats->modified)
	{
		load_png_wals = strstr (gl_texture_formats->string, "png") ? true : false;
		load_jpg_wals = strstr (gl_texture_formats->string, "jpg") ? true : false;
		load_tga_wals = strstr (gl_texture_formats->string, "tga") ? true : false;
		gl_texture_formats->modified = false;
	}

	if (gl_pic_formats->modified)
	{
		load_png_pics = strstr (gl_pic_formats->string, "png") ? true : false;
		load_jpg_pics = strstr (gl_pic_formats->string, "jpg") ? true : false;
		load_tga_pics = strstr (gl_pic_formats->string, "tga") ? true : false;
		gl_pic_formats->modified = false;
	}

	/*
	** swapinterval stuff
	*/
	GL_UpdateSwapInterval();

	//
	// clear screen if desired
	//
	R_Clear ();
	GL_ShowFrameErrors ("R_BeginFrame");
}

/*
=============
R_SetPalette
=============
*/
unsigned r_rawpalette[256];

void EXPORT R_EndFrame (void)
{
	Draw_Flush ();
	if (defer_drawing)
		Draw_AddText ();
	Draw_Flush ();
	R_DrawTearing ();
	GL_ShowFrameErrors ("R_EndFrame");
	GL_ScreenshotCapture ();
	GLimp_EndFrame ();
	R_AccountEndFrame ();
	R_GPUPerfEndFrame (r_perf_frame.active && r_perf_frame.begins == 1 &&
		r_perf_frame.views == 1 && r_perf_frame.swaps == 1 &&
		r_perf_frame.swap_ok && !r_perf_frame.norefresh &&
		!r_perf_frame.sync && !r_perf_frame.capture && r_perf_frame.clock_ok);
	R_PerfEndFrame ();
	if (gl_defertext->modified)
	{
		gl_defertext->modified = false;
		defer_drawing = (int)gl_defertext->value;
	}
	if (r_speeds->value >= 2 && r_speeds->value < 3)
		Draw_PrintStats ();
}

void EXPORT R_SetPalette (const unsigned char *palette)
{
	int i;
	byte *rp = (byte *)r_rawpalette;
	Draw_Flush ();
	for (i = 0; i < 256; i++)
	{
		if (palette)
		{
			rp[i * 4 + 0] = palette[i * 3 + 0];
			rp[i * 4 + 1] = palette[i * 3 + 1];
			rp[i * 4 + 2] = palette[i * 3 + 2];
		}
		else
			memcpy (rp + i * 4, (byte *)(d_8to24table + i), 3);
		rp[i * 4 + 3] = 255;
	}
	qglClearColor (0, 0, 0, 0);
	qglClear (GL_COLOR_BUFFER_BIT);
	R_RestoreClearColor ();
}

/*
** R_DrawBeam
*/
void R_DrawBeam( entity_t *e )
{
	qboolean bloom_shader;
#define NUM_BEAM_SEGS 6

	int	i;
	float r, g, b;

	vec3_t perpvec;
	vec3_t direction, normalized_direction;
	vec3_t	start_points[NUM_BEAM_SEGS], end_points[NUM_BEAM_SEGS];
	vec3_t oldorigin, origin;

	if (GL_JumpDrawBeam (e) || GL_NativeDrawBeam (e))
		return;

	oldorigin[0] = e->oldorigin[0];
	oldorigin[1] = e->oldorigin[1];
	oldorigin[2] = e->oldorigin[2];

	origin[0] = e->origin[0];
	origin[1] = e->origin[1];
	origin[2] = e->origin[2];

	normalized_direction[0] = direction[0] = oldorigin[0] - origin[0];
	normalized_direction[1] = direction[1] = oldorigin[1] - origin[1];
	normalized_direction[2] = direction[2] = oldorigin[2] - origin[2];

	if ( VectorNormalize( normalized_direction ) == 0 )
		return;

	PerpendicularVector( perpvec, normalized_direction );
	VectorScale( perpvec, e->frame / 2, perpvec );

	for ( i = 0; i < 6; i++ )
	{
		RotatePointAroundVector( start_points[i], normalized_direction, perpvec, (360.0f/NUM_BEAM_SEGS)*i );
		VectorAdd( start_points[i], origin, start_points[i] );
		VectorAdd( start_points[i], direction, end_points[i] );
	}

	qglPushAttrib (GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT |
		GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	qglDisable( GL_TEXTURE_2D );
	qglEnable( GL_BLEND );
	qglDepthMask( GL_FALSE );

	r = (float)(( d_8to24table[e->skinnum & 0xFF] ) & 0xFF);
	g = (float)(( d_8to24table[e->skinnum & 0xFF] >> 8 ) & 0xFF);
	b = (float)(( d_8to24table[e->skinnum & 0xFF] >> 16 ) & 0xFF);

	r *= 1/255.0F;
	g *= 1/255.0F;
	b *= 1/255.0F;

	qglColor4f( r, g, b, e->alpha );

	bloom_shader = GL_BeginBloomShader (false, false, true);
	qglBegin( GL_TRIANGLE_STRIP );
	for ( i = 0; i < NUM_BEAM_SEGS; i++ )
	{
		qglVertex3fv( start_points[i] );
		qglVertex3fv( end_points[i] );
		qglVertex3fv( start_points[(i+1)%NUM_BEAM_SEGS] );
		qglVertex3fv( end_points[(i+1)%NUM_BEAM_SEGS] );
	}
	qglEnd();
	if (GL_ShowTrisBegin (GL_SHOWTRIS_FX))
	{
		qglBegin (GL_TRIANGLE_STRIP);
		for (i = 0; i < NUM_BEAM_SEGS; i++)
		{
			qglVertex3fv (start_points[i]);
			qglVertex3fv (end_points[i]);
			qglVertex3fv (start_points[(i + 1) % NUM_BEAM_SEGS]);
			qglVertex3fv (end_points[(i + 1) % NUM_BEAM_SEGS]);
		}
		qglEnd ();
		GL_ShowTrisEnd ();
	}
	if (bloom_shader)
		GL_EndSurfaceShader ();

	qglPopAttrib ();
}

//===================================================================


void	EXPORT R_BeginRegistration (char *map);
struct model_s	* EXPORT R_RegisterModel (char *name);
struct image_s	* EXPORT R_RegisterSkin (char *name);
void EXPORT R_SetSky (char *name, float rotate, vec3_t axis);
void EXPORT	R_EndRegistration (void);

void EXPORT	R_RenderFrame (refdef_t *fd);

struct image_s	* EXPORT Draw_FindPic (char *name);

void	EXPORT Draw_Pic (int x, int y, char *name);
void	EXPORT Draw_Char (int x, int y, int c);
void	EXPORT Draw_TileClear (int x, int y, int w, int h, char *name);
void	EXPORT Draw_Fill (int x, int y, int w, int h, int c);
void	EXPORT Draw_FadeScreen (void);

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
void EXPORT GetExtraAPI (refimportnew_t rimp )
{
	if (rimp.APIVersion != EXTENDED_API_VERSION)
	{
		ri.Con_Printf (PRINT_ALL, "R1GL: ExtraAPI version number mismatch, expected version %d, got version %d.\n", EXTENDED_API_VERSION, rimp.APIVersion);
		return;
	}

	memcpy (&rx, &rimp, sizeof(rx));
}

refexport_t EXPORT GetRefAPI (refimport_t rimp )
{
	refexport_t	re;

	ri = rimp;

	re.api_version = API_VERSION;

	re.BeginRegistration = R_BeginRegistration;
	re.RegisterModel = R_RegisterModel;
	re.RegisterSkin = R_RegisterSkin;
	re.RegisterPic = Draw_FindPic;
	re.SetSky = R_SetSky;
	re.EndRegistration = R_EndRegistration;

	re.RenderFrame = R_RenderFrame;

	re.DrawGetPicSize = Draw_GetPicSize;
	re.DrawPic = Draw_Pic;
	re.DrawStretchPic = Draw_StretchPic;
	re.DrawChar = Draw_Char;
	re.DrawTileClear = Draw_TileClear;
	re.DrawFill = Draw_Fill;
	re.DrawFadeScreen= Draw_FadeScreen;

	re.DrawStretchRaw = Draw_StretchRaw;

	re.Init = R_Init;
	re.Shutdown = R_Shutdown;

	re.CinematicSetPalette = R_SetPalette;
	re.BeginFrame = R_BeginFrame;
	re.EndFrame = R_EndFrame;

	re.AppActivate = GLimp_AppActivate;

	Swap_Init ();

	return re;
}


#ifndef REF_HARD_LINKED
// this is only here so the functions in q_shared.c and q_shwin.c can link
void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	ri.Sys_Error (ERR_FATAL, "%s", text);
}

void Com_Printf (const char *fmt, int level, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, level);
	vsprintf (text, fmt, argptr);
	va_end (argptr);

	ri.Con_Printf (PRINT_ALL, "%s", text);
}

#endif
