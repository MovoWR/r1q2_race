/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
/* Observational acceleration/angle model adapted from frozen Q2PRO Race
 * src/jump/strafe_helper.c and strafe_efficiency.c (5571a17dbb7fb03915e7b403b1f981b7006a1a05).
 * The gain model derives from q2re-jump's GPLv2 jump_logic.cpp.
 */
#include "../client.h"
#include "jump.h"
#include "strafe_efficiency.h"
#include <float.h>

jump_sample_t cl_jump_sample;
static jump_sample_t jump_pending;
static qboolean jump_capturing;
static float jump_previous_side;

static float CL_JumpClamp (float value, float low, float high)
{
	return value < low ? low : value > high ? high :
											  value;
}

static qboolean CL_JumpFinite (float value)
{
	return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

static float CL_JumpCross (const vec3_t a, const vec3_t b)
{
	return a[0] * b[1] - a[1] * b[0];
}

static float CL_JumpAngle (const vec3_t a, const vec3_t b)
{
	return (float)atan2 (CL_JumpCross (a, b), a[0] * b[0] + a[1] * b[1]);
}

static float CL_JumpSign (float value)
{
	return value < 0 ? -1.0f : value > 0 ? 1.0f :
										   0.0f;
}

static void CL_JumpEfficiency (const vec3_t velocity, const vec3_t wishdir,
	float target, float budget)
{
	StrafeEfficiency result;
	result = StrafeEfficiency_Calculate (velocity, wishdir, target, budget);
	jump_pending.gain = result.gain;
	jump_pending.maximum_gain = result.maximum_gain;
	jump_pending.efficiency = result.efficiency;
	jump_pending.efficiency_valid = result.valid;
}

void CL_JumpClear (void)
{
	memset (&cl_jump_sample, 0, sizeof (cl_jump_sample));
	memset (&jump_pending, 0, sizeof (jump_pending));
	jump_capturing = false;
	jump_previous_side = 0;
}

void CL_JumpResetPrediction (void)
{
	memset (&cl_jump_sample, 0, sizeof (cl_jump_sample));
	jump_capturing = false;
}

int CL_JumpLastCommand (int acknowledged, int current, qboolean asynchronous)
{
	int sequence, last, end;

	if (current - acknowledged >= CMD_BACKUP || current <= acknowledged)
		return -1;
	last = -1;
	end = asynchronous ? current : current - 1;
	for (sequence = acknowledged + 1; sequence <= end; sequence++)
	{
		if (cl.cmds[sequence & (CMD_BACKUP - 1)].msec)
			last = sequence;
	}
	return last;
}

void CL_JumpBeginCommand (qboolean capture)
{
	jump_capturing = capture && CL_JumpEnabled () &&
		cls.state == ca_active && !cl.attractloop &&
		cl.frame.playerstate.pmove.pm_type == PM_NORMAL;
	if (jump_capturing)
		memset (&jump_pending, 0, sizeof (jump_pending));
}

void CL_JumpEndCommand (const pmove_new_t *pm)
{
	int i;

	if (!jump_capturing)
		return;
	jump_capturing = false;
	jump_pending.prediction_valid = true;
	jump_pending.command_msec = pm->cmd.msec;
	VectorSet (jump_pending.localmove, pm->cmd.forwardmove, pm->cmd.sidemove, pm->cmd.upmove);
	for (i = 0; i < 3; i++)
	{
		jump_pending.velocity[i] = pm->s.velocity[i] * 0.125f;
		jump_pending.origin[i] = pm->s.origin[i] * 0.125f;
	}
	cl_jump_sample = jump_pending;
	if (jump_pending.valid)
		jump_previous_side = jump_pending.side;
}

void CL_JumpAcceleration (const vec3_t forward, const vec3_t velocity,
	const vec3_t wishdir, float wishspeed, float target, float accel,
	float frametime, qboolean on_ground, int waterlevel, qboolean ladder)
{
	float speed, length, angle, side, scale, squared, optimal, loss, cross;
	int i;

	if (!jump_capturing)
		return;
	for (i = 0; i < 3; i++)
	{
		if (!CL_JumpFinite (forward[i]) || !CL_JumpFinite (velocity[i]) || !CL_JumpFinite (wishdir[i]))
			return;
	}
	if (!CL_JumpFinite (wishspeed) || !CL_JumpFinite (target) ||
		!CL_JumpFinite (accel) || !CL_JumpFinite (frametime))
		return;
	VectorCopy (forward, jump_pending.forward);
	VectorCopy (wishdir, jump_pending.wishdir);
	jump_pending.wishspeed = wishspeed;
	jump_pending.target = target;
	jump_pending.budget = accel * frametime * wishspeed;
	jump_pending.frametime = frametime;
	jump_pending.on_ground = on_ground;
	jump_pending.waterlevel = waterlevel;
	jump_pending.currentspeed = DotProduct (velocity, wishdir);
	jump_pending.addspeed = target - jump_pending.currentspeed;
	jump_pending.forward_wish_angle = CL_JumpAngle (wishdir, forward);
	/* Efficiency intentionally covers dry airborne movement only. */
	if (!on_ground && !waterlevel && !ladder)
		CL_JumpEfficiency (velocity, wishdir, target, jump_pending.budget);

	speed = (float)sqrt ((double)velocity[0] * velocity[0] + (double)velocity[1] * velocity[1]);
	length = (float)sqrt ((double)wishdir[0] * wishdir[0] + (double)wishdir[1] * wishdir[1]);
	target -= velocity[2] * wishdir[2];
	cross = CL_JumpCross (wishdir, forward);
	if (speed <= 0.0001f || length <= 0.0001f || target <= 0 || jump_pending.budget <= 0 ||
		(fabs (cross) <= 0.0001f * length * sqrt ((double)forward[0] * forward[0] + (double)forward[1] * forward[1]) &&
			wishdir[0] * forward[0] + wishdir[1] * forward[1] > 0))
		return;
	side = CL_JumpSign (CL_JumpCross (wishdir, velocity));
	if (!side)
		side = jump_previous_side ? jump_previous_side : CL_JumpSign (cross);
	if (!side)
		side = 1;
	squared = CL_JumpClamp (length * length, 0, 1);
	scale = speed * length;
	optimal = target * (1 - squared) / (2 - squared);
	if (target - jump_pending.budget > optimal)
		optimal = target - jump_pending.budget;
	angle = CL_JumpAngle (wishdir, forward);
	jump_pending.optimal = side * (float)acos (CL_JumpClamp (optimal / scale, -1, 1)) - angle;
	jump_pending.minimum = side * (float)acos (CL_JumpClamp (target / scale, -1, 1)) - angle;
	loss = jump_pending.budget * 0.5f;
	if (target / (2 - squared) < loss)
		loss = target / (2 - squared);
	jump_pending.maximum = side * (float)acos (CL_JumpClamp (-loss * length / speed, -1, 1)) - angle;
	jump_pending.current = CL_JumpAngle (forward, velocity);
	while (jump_pending.current - jump_pending.optimal > M_PI)
		jump_pending.current -= 2 * M_PI;
	while (jump_pending.current - jump_pending.optimal < -M_PI)
		jump_pending.current += 2 * M_PI;
	jump_pending.difference = jump_pending.current - jump_pending.optimal;
	jump_pending.velocity_norm = speed;
	jump_pending.side = side;
	jump_pending.valid = true;
}
