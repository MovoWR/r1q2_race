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
/* FPS commands adapted from Q2PRO Race frozen commit
 * 5571a17dbb7fb03915e7b403b1f981b7006a1a05, src/client/fps.c.
 * R1Q2's native cvar callback performs all frame timing changes.
 */
#include "../client.h"
#include "jump.h"
#include "jump_settings.h"
#include "jump_layout.h"
#include "jump_editor.h"
#include <limits.h>

cvar_t *cl_jump;
static cvar_t *jump_fps_hold[JUMP_FPS_SLOTS];
static cvar_t *jump_fps_release[JUMP_FPS_SLOTS];
static char jump_fps_names[101][8];
static qboolean jump_commands_registered;

int CL_JumpParseInteger (const char *text)
{
	int value, digit;

	value = 0;
	if (!text || !*text)
		return 0;
	do
	{
		digit = *text++ - '0';
		if (digit < 0 || digit > 9 || value > (INT_MAX - digit) / 10)
			return 0;
		value = value * 10 + digit;
	} while (*text);
	return value;
}

static void CL_JumpSetFps (const char *value)
{
	if (!CL_JumpEnabled ())
		return;
	if (!CL_JumpParseInteger (value))
	{
		Com_Printf ("FPS must be a positive decimal integer\n", LOG_CLIENT);
		return;
	}
	Cvar_Set ("cl_maxfps", value);
}

static void CL_JumpFpsPair_f (void)
{
	if (Cmd_Argc () < 3 || !CL_JumpParseInteger (Cmd_Argv (1)) ||
		!CL_JumpParseInteger (Cmd_Argv (2)))
	{
		Com_Printf ("usage: %s <down_fps> <up_fps>\n", LOG_CLIENT, Cmd_Argv (0));
		return;
	}
	CL_JumpSetFps (Cmd_Argv (Cmd_Argv (0)[0] == '-' ? 2 : 1));
}

static void CL_JumpFpsSlot_f (void)
{
	int slot;
	cvar_t *var;

	slot = CL_JumpParseInteger (Cmd_Argv (1));
	if (slot < 1 || slot > JUMP_FPS_SLOTS)
	{
		Com_Printf ("usage: %s <slot 1-%d>\n", LOG_CLIENT, Cmd_Argv (0), JUMP_FPS_SLOTS);
		return;
	}
	var = Cmd_Argv (0)[0] == '-' ?
		jump_fps_release[slot - 1] :
		jump_fps_hold[slot - 1];
	if (var)
		CL_JumpSetFps (var->string);
	else
	{
		char name[32];
		const char *value;

		Com_sprintf (name, sizeof (name), "fps_%s_%d",
			Cmd_Argv (0)[0] == '-' ? "release" : "hold", slot);
		value = Cvar_VariableString (name);
		if (!*value)
			value = Cvar_VariableString (Cmd_Argv (0)[0] == '-' ?
					"fps_default_release" :
					"fps_default_hold");
		if (!*value)
			value = Cmd_Argv (0)[0] == '-' ? "120" : "30";
		CL_JumpSetFps (value);
	}
}

static void CL_JumpFpsShortcut_f (void)
{
	int fps;
	char value[16];

	fps = CL_JumpParseInteger (Cmd_Argv (0) + 1);
	if (fps >= 20 && fps <= 120)
	{
		Com_sprintf (value, sizeof (value), "%d", fps);
		CL_JumpSetFps (value);
	}
}

static void CL_JumpDoubleJumpWait_f (void)
{
	if (!CL_JumpEnabled ())
		return;
	CL_CommandWaitForSend (atoi (Cmd_Argv (1)) != 0);
	Cbuf_InsertText ("wait\n");
}

static void CL_JumpDoubleJump_f (void)
{
	if (!CL_JumpEnabled ())
		return;
	if (Cmd_Argv (0)[0] == '+' && cl_async && cl_async->intvalue == 1)
		Cbuf_InsertText ("+moveup; _dj_wait 1; -moveup; _dj_wait 0; +moveup\n");
	else
		Cbuf_InsertText (Cmd_Argv (0)[0] == '+' ?
				"+moveup; wait; -moveup; wait; +moveup\n" :
				"-moveup\n");
}

static void CL_JumpDoubleJumpCommands (void)
{
	const char *existing;

	Cmd_AddCommand ("_dj_wait", CL_JumpDoubleJumpWait_f);
	existing = Cmd_CompleteCommandOld ("+dj");
	if (!existing || strcmp (existing, "+dj"))
		Cmd_AddCommand ("+dj", CL_JumpDoubleJump_f);
	existing = Cmd_CompleteCommandOld ("-dj");
	if (!existing || strcmp (existing, "-dj"))
		Cmd_AddCommand ("-dj", CL_JumpDoubleJump_f);
}

void CL_JumpSyncGame (void)
{
	qboolean enabled = CL_JumpEnabled ();

	if (!cl_jump || cl_jump->intvalue == enabled)
		return;
	Cvar_ForceSet ("cl_jump", enabled ? "1" : "0");
	if (!enabled)
	{
		CL_JumpUIClear ();
		CL_JumpClear ();
		CL_JumpClearInput ();
		CL_JumpClearDraw ();
		CL_JumpClearNetwork ();
		CL_JumpClearBeams ();
	}
}

static void CL_JumpSetup_f (void)
{
	Com_Printf (CL_JumpEnabled () ? "Jump tools are enabled automatically for game jump.\n" :
									"Jump tools are available only when game is jump.\n",
		LOG_CLIENT);
}

void CL_JumpRegisterCommands (void)
{
	if (jump_commands_registered)
		return;
	CL_JumpRegisterWidgets ();
	CL_JumpUIMenuRegister ();
	jump_commands_registered = true;
	Cmd_AddCommand ("+fps", CL_JumpFpsPair_f);
	Cmd_AddCommand ("-fps", CL_JumpFpsPair_f);
	Cmd_AddCommand ("+fps_hold", CL_JumpFpsSlot_f);
	Cmd_AddCommand ("-fps_hold", CL_JumpFpsSlot_f);
	Cmd_AddCommand ("jump_setup", CL_JumpSetup_f);
	Cmd_AddCommand ("debugnow", SH_DebugNow_f);
	CL_JumpDoubleJumpCommands ();
}

void CL_JumpInit (void)
{
	int i;
	char name[32];
	const char *hold, *release, *existing;

	CL_JumpConfigInit ();
	SCR_BindRemindersInit ();
	HUD_LayoutInit ();
	HUD_EditorInit ();
	Cvar_Get ("s_mute_player_jump_sounds", "0", CVAR_ARCHIVE);
	/* Compatibility indicator only; the loaded game controls activation. */
	cl_jump = Cvar_Get ("cl_jump", "0", CVAR_NOSET);
	cl_jump->flags = CVAR_NOSET;
	cl_jump->changed = NULL;
	CL_JumpSyncGame ();
	hold = Cvar_VariableString ("fps_default_hold");
	release = Cvar_VariableString ("fps_default_release");
	if (!CL_JumpParseInteger (hold))
		hold = "30";
	if (!CL_JumpParseInteger (release))
		release = "120";
	for (i = 0; i < JUMP_FPS_SLOTS; i++)
	{
		Com_sprintf (name, sizeof (name), "fps_hold_%d", i + 1);
		jump_fps_hold[i] = Cvar_Get (name, hold, CVAR_ARCHIVE);
		Com_sprintf (name, sizeof (name), "fps_release_%d", i + 1);
		jump_fps_release[i] = Cvar_Get (name, release, CVAR_ARCHIVE);
	}
	CL_JumpRegisterCommands ();
	for (i = 20; i <= 120; i++)
	{
		Com_sprintf (jump_fps_names[i - 20], sizeof (jump_fps_names[0]), "f%d", i);
		existing = Cmd_CompleteCommandOld (jump_fps_names[i - 20]);
		if ((!existing || strcmp (existing, jump_fps_names[i - 20])) &&
			!Cvar_FindVar (jump_fps_names[i - 20]))
			Cmd_AddCommand (jump_fps_names[i - 20], CL_JumpFpsShortcut_f);
	}
}
