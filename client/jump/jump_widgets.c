/* Dynamic draw objects and speed-color behavior adapted from Q2PRO Race
 * src/client/screen.c and main.c, frozen 5571a17dbb7fb03915e7b403b1f981b7006a1a05.
 * Copyright (C) 2003-2006 Andrey Nazarov. GPL version 2 or later; see COPYING. */
#include "jump_compat.h"
#include "strafe_helper_customization.h"
#include <limits.h>

typedef struct
{
	char name[MAX_QPATH];
	int x, y, flags, macro;
	uint32 color;
	qboolean dynamic, fixed_color;
} jump_widget_t;

static jump_widget_t jump_widgets[128];
static int jump_widget_count;
static float jump_previous_speed[2];
static qboolean jump_widget_commands;
static const char *jump_macro_names[] = {
	"cl_mapname", "cl_server", "cl_timer", "cl_demopos", "cl_pps", "cl_ping", "cl_lag",
	"cl_health", "cl_ammo", "cl_armor", "cl_weaponmodel", "cl_numentities", "cl_surface",
	"r_mfps", "r_fps", "cl_mfps", "cl_mmps", "cl_fps", "cl_rups", "cl_ups",
	"cl_playerpos_z", "cl_playerpos_y", "cl_playerpos_x", "loc_here", "loc_there"
};
extern const char *CL_Get_Loc_Here (void);
extern const char *CL_Get_Loc_There (void);

static int CL_JumpMacroIndex (const char *name)
{
	unsigned int i;
	for (i = 0; i < q_countof (jump_macro_names); i++)
		if (!strcmp (name, jump_macro_names[i]))
			return (int)i;
	return -1;
}

static float CL_JumpWidgetSpeed (qboolean full)
{
	vec3_t velocity;
	float speed;
	if (!cl.frame.valid)
		return 0;
	if (cl_jump_sample.prediction_valid && cl_predict->intvalue && !cl.attractloop &&
		!(cl.frame.playerstate.pmove.pm_flags & PMF_NO_PREDICTION))
		VectorCopy (cl_jump_sample.velocity, velocity);
	else
		VectorScale (cl.frame.playerstate.pmove.velocity, .125f, velocity);
	if (!full)
		velocity[2] = 0;
	speed = VectorLength (velocity);
	return speed >= 0 && speed <= FLT_MAX ? speed : 0;
}

static uint32 CL_JumpWidgetSpeedColor (qboolean full)
{
	float current = CL_JumpWidgetSpeed (full);
	float previous = jump_previous_speed[full != 0];
	float epsilon = full ? .01f : 0;
	uint32 color = U32_WHITE;
	if (current > previous + epsilon)
		color = U32_GREEN;
	else if (current < previous - epsilon)
		color = U32_RED;
	jump_previous_speed[full != 0] = current;
	return color;
}

void CL_JumpClearWidgets (void)
{
	jump_previous_speed[0] = jump_previous_speed[1] = 0;
}

static void CL_JumpSurfaceMacro (char *out, int size)
{
	trace_t trace, inline_trace;
	entity_state_t *entity;
	cmodel_t *model;
	vec3_t end;
	int i;
	if (cls.state != ca_active || !cl.frame.valid)
	{
		if (size)
			*out = 0;
		return;
	}
	VectorMA (cl.refdef.vieworg, 8192, cl.v_forward, end);
	trace = CM_BoxTrace (cl.refdef.vieworg, end, vec3_origin, vec3_origin, 0, MASK_SOLID | MASK_WATER);
	/* Encoded player boxes have CONTENTS_MONSTER and cannot match this mask. */
	for (i = 0; i < cl.frame.num_entities && !trace.allsolid; i++)
	{
		entity = &cl_parse_entities[(cl.frame.parse_entities + i) & (MAX_PARSE_ENTITIES - 1)];
		if (entity->solid != 31 || entity->number == cl.playernum + 1 ||
			entity->modelindex <= 0 || entity->modelindex >= MAX_MODELS)
			continue;
		model = cl.model_clip[entity->modelindex];
		if (!model)
			continue;
		inline_trace = CM_TransformedBoxTrace (cl.refdef.vieworg, end, vec3_origin, vec3_origin,
			model->headnode, MASK_SOLID | MASK_WATER, entity->origin, entity->angles);
		if (inline_trace.allsolid || inline_trace.startsolid || inline_trace.fraction < trace.fraction)
		{
			if (trace.startsolid)
				inline_trace.startsolid = true;
			trace = inline_trace;
		}
	}
	if (trace.surface)
		Com_sprintf (out, size, "%s %#x", trace.surface->name, trace.surface->flags);
	else if (size)
		*out = 0;
}

static void CL_JumpMacroValue (int index, char *out, int size)
{
	const char *text;
	char map[MAX_QPATH], *extension;
	int seconds, minutes, hours, model, axis;
	float speed;

	if (size <= 0)
		return;
	*out = 0;
	switch (index)
	{
	case 0:
		text = cl.configstrings[CS_MODELS + 1];
		if (!strncmp (text, "maps/", 5))
			text += 5;
		Q_strlcpy (map, text, sizeof (map));
		extension = strrchr (map, '.');
		if (extension)
			*extension = 0;
		Q_strlcpy (out, map, size);
		break;
	case 1:
		Q_strlcpy (out, cls.servername, size);
		break;
	case 2:
		seconds = cl.time > 0 ? cl.time / 1000 : 0;
		minutes = seconds / 60;
		seconds %= 60;
		hours = minutes / 60;
		minutes %= 60;
		if (hours)
			Com_sprintf (out, size, "%i:%i:%02i", hours, minutes, seconds);
		else
			Com_sprintf (out, size, "%i:%02i", minutes, seconds);
		break;
	case 3:
		CL_DemoPos_m (out, size);
		break;
	case 4:
		CL_Pps_m (out, size);
		break;
	case 5:
		CL_Ping_m (out, size);
		break;
	case 6:
		CL_Lag_m (out, size);
		break;
	case 7:
		Com_sprintf (out, size, "%i", cl.frame.playerstate.stats[STAT_HEALTH]);
		break;
	case 8:
		Com_sprintf (out, size, "%i", cl.frame.playerstate.stats[STAT_AMMO]);
		break;
	case 9:
		Com_sprintf (out, size, "%i", cl.frame.playerstate.stats[STAT_ARMOR]);
		break;
	case 10:
		model = cl.frame.playerstate.gunindex;
		if (model >= 0 && model < MAX_MODELS)
			Q_strlcpy (out, cl.configstrings[CS_MODELS + model], size);
		break;
	case 11:
		Com_sprintf (out, size, "%i", cl.frame.num_entities);
		break;
	case 12:
		CL_JumpSurfaceMacro (out, size);
		break;
	case 13:
		R_Mfps_m (out, size);
		break;
	case 14:
		R_Fps_m (out, size);
		break;
	case 15:
		CL_Mfps_m (out, size);
		break;
	case 16:
		CL_Mmps_m (out, size);
		break;
	case 17:
		CL_Mps_m (out, size);
		break;
	case 18:
	case 19:
		speed = CL_JumpWidgetSpeed (index == 18);
		if (cl.frame.playerstate.pmove.pm_type == PM_NORMAL || speed != 0)
			Com_sprintf (out, size, "%.f", speed);
		break;
	case 20:
	case 21:
	case 22:
		axis = 22 - index;
		Com_sprintf (out, size, "%.0f", cl.frame.playerstate.pmove.origin[axis] * .125f);
		break;
	case 23:
		Q_strlcpy (out, CL_Get_Loc_Here (), size);
		break;
	case 24:
		Q_strlcpy (out, CL_Get_Loc_There (), size);
		break;
	}
}

qboolean CL_JumpMacro (const char *name, char *out, int size)
{
	int index;
	if (!CL_JumpEnabled () || !name || !out || size <= 0)
		return false;
	index = CL_JumpMacroIndex (name);
	if (index < 0)
		return false;
	CL_JumpMacroValue (index, out, size);
	return true;
}

static int CL_JumpHexDigit (int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

qboolean CL_JumpParseColor (const char *text, uint32 *packed)
{
	static const char *names[] = { "black", "red", "green", "yellow", "blue", "cyan", "magenta", "white" };
	color_t color;
	int count, i, high, low;
	if (!text || !packed)
		return false;
	if (*text == '#')
	{
		text++;
		count = (int)strlen (text);
		if (count != 3 && count != 6 && count != 8)
			return false;
		color.u32 = U32_WHITE;
		for (i = 0; i < (count == 3 ? 3 : count / 2); i++)
		{
			high = CL_JumpHexDigit (text[count == 3 ? i : i * 2]);
			low = count == 3 ? high : CL_JumpHexDigit (text[i * 2 + 1]);
			if (high < 0 || low < 0)
				return false;
			color.u8[i] = (byte)((high << 4) | low);
		}
		*packed = color.u32;
		return true;
	}
	if (shc_ParseColorCvar (text, NULL, &color))
	{
		*packed = color.u32;
		return true;
	}
	for (i = 0; i < 8; i++)
		if (!strcmp (names[i], text) || (text[0] == '0' + i && !text[1]))
		{
			*packed = MakeColor ((i & 1) ? 255 : 0, (i & 2) ? 255 : 0, (i & 4) ? 255 : 0, 255);
			return true;
		}
	return false;
}

static qboolean CL_JumpWidgetCoordinate (const char *text, int *out)
{
	char *end;
	long value;
	if (!text || !*text)
		return false;
	value = strtol (text, &end, 10);
	if (*end || value < -16000 || value > 16000)
		return false;
	*out = (int)value;
	return true;
}

static void CL_JumpDraw_f (void)
{
	jump_widget_t widget;
	const char *color;
	int i;

	if (!CL_JumpEnabled ())
		return;
	if (Cmd_Argc () == 1)
	{
		for (i = 0; i < jump_widget_count; i++)
			Com_Printf ("%-15s %4d %4d\n", LOG_CLIENT, jump_widgets[i].name, jump_widgets[i].x, jump_widgets[i].y);
		return;
	}
	if (Cmd_Argc () < 4 || !*Cmd_Argv (1) || strlen (Cmd_Argv (1)) >= MAX_QPATH ||
		!CL_JumpWidgetCoordinate (Cmd_Argv (2), &widget.x) || !CL_JumpWidgetCoordinate (Cmd_Argv (3), &widget.y))
	{
		Com_Printf ("usage: draw <name> <x> <y> [color|alt|none|dynamic]\n", LOG_CLIENT);
		return;
	}
	Q_strlcpy (widget.name, Cmd_Argv (1), sizeof (widget.name));
	widget.macro = CL_JumpMacroIndex (widget.name);
	widget.flags = widget.x < 0 ? UI_RIGHT : 0;
	widget.color = U32_WHITE;
	widget.dynamic = widget.fixed_color = false;
	color = Cmd_Argc () > 4 ? Cmd_Argv (4) : "none";
	if (!strcmp (color, "alt"))
		widget.flags |= UI_ALTCOLOR;
	else if (!strcmp (color, "dynamic"))
	{
		if (widget.macro != 18 && widget.macro != 19)
		{
			Com_Printf ("'%s' does not support dynamic colors\n", LOG_CLIENT, widget.name);
			return;
		}
		widget.dynamic = true;
	}
	else if (strcmp (color, "none") && !CL_JumpParseColor (color, &widget.color))
	{
		Com_Printf ("Unknown color '%s'\n", LOG_CLIENT, color);
		return;
	}
	widget.fixed_color = strcmp (color, "none") && strcmp (color, "alt") && !widget.dynamic;
	for (i = 0; i < jump_widget_count; i++)
		if (!strcmp (jump_widgets[i].name, widget.name))
			break;
	if (i == q_countof (jump_widgets))
	{
		Com_Printf ("Too many draw strings\n", LOG_CLIENT);
		return;
	}
	jump_widgets[i] = widget;
	if (i == jump_widget_count)
		jump_widget_count++;
}

static void CL_JumpUndraw_f (void)
{
	int i;
	if (!CL_JumpEnabled ())
		return;
	if (Cmd_Argc () != 2)
	{
		Com_Printf ("usage: undraw <name|all>\n", LOG_CLIENT);
		return;
	}
	if (!strcmp (Cmd_Argv (1), "all"))
	{
		jump_widget_count = 0;
		return;
	}
	for (i = 0; i < jump_widget_count; i++)
		if (!strcmp (jump_widgets[i].name, Cmd_Argv (1)))
		{
			memmove (jump_widgets + i, jump_widgets + i + 1, (--jump_widget_count - i) * sizeof (*jump_widgets));
			return;
		}
	Com_Printf ("Draw string '%s' not found\n", LOG_CLIENT, Cmd_Argv (1));
}

static void CL_JumpWidgetCommand (const char *name, xcommand_t handler)
{
	const char *existing = Cmd_CompleteCommandOld (name);
	if ((!existing || strcmp (existing, name)) && !Cvar_FindVar (name))
		Cmd_AddCommand (name, handler);
}

void CL_JumpRegisterWidgets (void)
{
	if (jump_widget_commands)
		return;
	jump_widget_commands = true;
	CL_JumpWidgetCommand ("draw", CL_JumpDraw_f);
	CL_JumpWidgetCommand ("undraw", CL_JumpUndraw_f);
}

void CL_JumpDrawWidgets (void)
{
	jump_widget_t *widget;
	char buffer[MAX_QPATH];
	const char *text;
	int i, x, y, id;
	float alpha = Cvar_ClampValue (jump_hud_alpha, 0, 1);

	if (!CL_JumpEnabled ())
		return;
	for (i = 0; i < jump_widget_count; i++)
	{
		widget = &jump_widgets[i];
		x = widget->x < 0 ? widget->x + jump_screen.hud_width + 1 : widget->x;
		y = widget->y < 0 ? widget->y + jump_screen.hud_height - CHAR_HEIGHT + 1 : widget->y;
		id = HUD_LayoutObject (widget->name);
		HUD_LayoutSetScaleAnchor (id, x, y);
		HUD_LayoutBegin (id);
		R_ClearColor ();
		R_SetAlpha (alpha);
		if (widget->dynamic)
			R_SetColor (CL_JumpWidgetSpeedColor (widget->macro == 18));
		else if (widget->fixed_color)
			R_SetColor (widget->color);
		if (widget->macro >= 0)
		{
			CL_JumpMacroValue (widget->macro, buffer, sizeof (buffer));
			text = buffer;
		}
		else
			text = Cvar_VariableString (widget->name);
		SCR_DrawString (x, y, widget->flags, text);
		HUD_LayoutEnd ();
	}
	R_ClearColor ();
	R_SetAlpha (alpha);
}
