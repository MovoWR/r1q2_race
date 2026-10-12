/* Temporary racing lasers adapted from Q2PRO Race src/client/laser.c
 * (frozen 5571a17dbb7fb03915e7b403b1f981b7006a1a05).
 * Copyright (C) 1997-2001 Id Software, Inc.
 * GNU GPL version 2 or later; see COPYING. */
#include "jump_compat.h"
#include "jump_beams.h"
#include "strafe_helper_customization.h"

#define JUMP_LASERS 256

typedef struct
{
	entity_t entity;
	unsigned int starttime;
	int lifetime;
	unsigned int rgba;
} jump_laser_t;

typedef struct
{
	entity_t entity;
	refbeam_t beam;
} jump_scene_entity_t;

static jump_laser_t jump_lasers[JUMP_LASERS];
static refbeam_t jump_scene_beams[MAX_ENTITIES];
static jump_scene_entity_t jump_sorted_entities[MAX_ENTITIES];
static int jump_scene_count;
const refpresent3_t *scr_beam_presentation;
extern int r_numentities;
extern int EXPORT entitycmpfnc (const entity_t *a, const entity_t *b);

void SCR_SetBeamPresentation (const refpresent3_t *api)
{
	scr_beam_presentation = NULL;
	if (api && api->version == REF_PRESENT_VERSION3 && api->size >= sizeof (*api) && api->SetBeams)
		scr_beam_presentation = api;
	CL_JumpClearBeams ();
}

static qboolean CL_JumpRaceBeams (void)
{
	return CL_JumpEnabled () && scr_beam_presentation;
}

void CL_JumpSceneClear (void)
{
	memset (jump_scene_beams, 0, sizeof (jump_scene_beams));
	jump_scene_count = 0;
}

void CL_JumpClearBeams (void)
{
	memset (jump_lasers, 0, sizeof (jump_lasers));
	CL_JumpSceneClear ();
	if (scr_beam_presentation)
		scr_beam_presentation->SetBeams (NULL, NULL, 0);
}

void CL_JumpSceneEntity (int index)
{
	if (index >= 0 && index < MAX_ENTITIES)
		memset (&jump_scene_beams[index], 0, sizeof (jump_scene_beams[index]));
}

static float CL_JumpBeamClamp (float value, float low, float high)
{
	if (!(value >= low))
		return low;
	return value > high ? high : value;
}

qboolean CL_JumpParseLaser (vec3_t start, vec3_t end)
{
	jump_laser_t *laser;
	color_t color;
	unsigned int now;
	int i, axis;

	if (!CL_JumpRaceBeams ())
		return false;
	for (axis = 0; axis < 3; axis++)
		if (!(fabs (start[axis]) <= FLT_MAX) || !(fabs (end[axis]) <= FLT_MAX))
			return true;
	now = (unsigned int)cl.time;
	for (i = 0; i < JUMP_LASERS; i++)
	{
		laser = &jump_lasers[i];
		if (now - laser->starttime < (unsigned int)laser->lifetime)
			continue;
		memset (laser, 0, sizeof (*laser));
		laser->starttime = now;
		laser->lifetime = (int)CL_JumpBeamClamp (cl_race_life->value, 0, 5000);
		laser->entity.frame = (int)CL_JumpBeamClamp (cl_race_width->value, 0, 20);
		laser->entity.flags = RF_TRANSLUCENT | RF_BEAM;
		VectorCopy (start, laser->entity.origin);
		VectorCopy (end, laser->entity.oldorigin);
		/* Public skinnum remains valid even if the optional renderer goes away. */
		laser->entity.skinnum = 0;
		if (!shc_ParseColorCvar (cl_race_color->string, NULL, &color))
			color.u32 = 0xff0000ffu;
		laser->rgba = color.u32;
		return true;
	}
	return true;
}

void CL_JumpAddLasers (void)
{
	jump_laser_t *laser;
	unsigned int age;
	float alpha;
	int i, index;

	if (!CL_JumpRaceBeams ())
	{
		CL_JumpClearBeams ();
		return;
	}
	alpha = CL_JumpBeamClamp (cl_race_alpha->value, 0, 1);
	if (!alpha)
		return;
	for (i = 0; i < JUMP_LASERS; i++)
	{
		laser = &jump_lasers[i];
		age = (unsigned int)cl.time - laser->starttime;
		if (!laser->lifetime || age >= (unsigned int)laser->lifetime || !laser->entity.frame)
			continue;
		laser->entity.alpha = (1.0f - (float)age / laser->lifetime) * alpha;
		index = r_numentities;
		V_AddEntity (&laser->entity);
		if (index < MAX_ENTITIES && r_numentities == index + 1)
		{
			jump_scene_beams[index].active = true;
			jump_scene_beams[index].rgba = laser->rgba;
		}
	}
}

static int EXPORT CL_JumpEntityCompare (const void *a, const void *b)
{
	return entitycmpfnc (&((const jump_scene_entity_t *)a)->entity,
		&((const jump_scene_entity_t *)b)->entity);
}

void CL_JumpSortScene (entity_t *entities, int count)
{
	int i;
	qboolean paired = false;

	if (!entities || count < 0 || count > MAX_ENTITIES)
		return;
	jump_scene_count = count;
	for (i = 0; i < count; i++)
		if (jump_scene_beams[i].active)
			paired = true;
	if (!paired)
	{
		qsort (entities, count, sizeof (*entities), (int (EXPORT *) (const void *, const void *))entitycmpfnc);
		return;
	}
	for (i = 0; i < count; i++)
	{
		jump_sorted_entities[i].entity = entities[i];
		jump_sorted_entities[i].beam = jump_scene_beams[i];
	}
	qsort (jump_sorted_entities, count, sizeof (*jump_sorted_entities), CL_JumpEntityCompare);
	for (i = 0; i < count; i++)
	{
		entities[i] = jump_sorted_entities[i].entity;
		jump_scene_beams[i] = jump_sorted_entities[i].beam;
	}
}

void CL_JumpPublishScene (const entity_t *entities, int count)
{
	if (!scr_beam_presentation)
		return;
	if (!CL_JumpRaceBeams () || count != jump_scene_count || count < 0 || count > MAX_ENTITIES)
		scr_beam_presentation->SetBeams (NULL, NULL, 0);
	else
		scr_beam_presentation->SetBeams (entities, jump_scene_beams, count);
}
