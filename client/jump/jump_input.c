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
/* Held-key display observes state bits without consuming input edges. */
#include "../client.h"
#include "jump.h"
#include "jump_settings.h"

extern kbutton_t in_forward, in_back, in_moveleft, in_moveright, in_strafe;
extern kbutton_t in_left, in_right, in_up, in_down;

void CL_JumpClearInput (void)
{
	if (cl_input_keys)
		Cvar_Set ("cl_input_keys", "");
}

void CL_JumpUpdateInput (void)
{
	char text[32], *out;
	qboolean forward, left, back, right, jump, crouch;

	if (!cl_input_keys)
		return;
	if (!CL_JumpEnabled () || cls.state != ca_active)
	{
		Cvar_Set ("cl_input_keys", "");
		return;
	}
	forward = left = back = right = false;
	if (cl.attractloop || cl.frame.playerstate.pmove.pm_type == PM_SPECTATOR)
	{
		jump = (cl.frame.playerstate.pmove.pm_flags & PMF_JUMP_HELD) != 0;
		crouch = (cl.frame.playerstate.pmove.pm_flags & PMF_DUCKED) != 0;
	}
	else
	{
		forward = (in_forward.state & 1) != 0;
		back = (in_back.state & 1) != 0;
		left = (in_moveleft.state & 1) || ((in_strafe.state & 1) && (in_left.state & 1));
		right = (in_moveright.state & 1) || ((in_strafe.state & 1) && (in_right.state & 1));
		jump = (in_up.state & 1) != 0;
		crouch = (in_down.state & 1) != 0;
	}
	out = text;
#define JUMP_INPUT_LETTER(held, letter) if (held) { *out++ = '['; *out++ = letter; *out++ = ']'; }
	JUMP_INPUT_LETTER (forward, 'W');
	JUMP_INPUT_LETTER (left, 'A');
	JUMP_INPUT_LETTER (back, 'S');
	JUMP_INPUT_LETTER (right, 'D');
	if (out != text && (jump || crouch))
		*out++ = ' ';
	JUMP_INPUT_LETTER (jump, 'J');
	JUMP_INPUT_LETTER (crouch, 'C');
#undef JUMP_INPUT_LETTER
	*out = 0;
	Cvar_Set ("cl_input_keys", text);
}

qboolean CL_JumpMuteSound (const char *name, int entnum)
{
	return CL_JumpEnabled () && name &&
		Cvar_IntValue ("s_mute_player_jump_sounds") && entnum > 0 && entnum <= MAX_CLIENTS &&
		!Q_stricmp (name, "*jump1.wav");
}
