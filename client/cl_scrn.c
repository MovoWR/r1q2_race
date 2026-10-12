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
// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

/*

  full screen console
  put up loading plaque
  blanked background with loading plaque
  blanked background with menu
  cinematics
  full screen image for quit and victory

  end of unit intermissions

  */

#include "client.h"
#include "jump/jump.h"
#include "jump/jump_layout.h"
#include "ref_present.h"

const refpresent2_t *scr_rich_presentation;
const refpresent5_t *scr_color_presentation;

void SCR_SetColorPresentation (const refpresent5_t *api)
{
	scr_color_presentation = NULL;
	if (api && api->version == REF_PRESENT_VERSION5 && api->size >= (int)sizeof (*api) &&
		api->UpdateColorFrame && api->DrawColorFrame)
		scr_color_presentation = api;
}

void SCR_SetRichPresentation (const refpresent2_t *api)
{
	scr_rich_presentation = NULL;
	if (api && api->version == REF_PRESENT_VERSION2 && api->size >= sizeof (*api) &&
		api->Begin && api->End && api->Glyph && api->Pic && api->Fill && api->Clip && api->Color8)
		scr_rich_presentation = api;
}

vrect_t scr_vrect; // position of render window on screen
const refpresent_t *scr_presentation;
static cvar_t *scr_scale, *con_scale, *scr_font, *con_font;
static float scr_ui_factor = 1;

void SCR_SetPresentation (const refpresent_t *api)
{
	scr_presentation = NULL;
	scr_ui_factor = 1;
	if (api && api->version == REF_PRESENT_VERSION && api->size >= sizeof (*api) &&
		api->GetSize && api->SetSpace && api->DrawColorPic)
		scr_presentation = api;
	else if (api)
		Com_Printf ("Unsupported presentation API; using global HUD scale.\n", LOG_CLIENT);
	Con_CheckResize ();
}

int SCR_ConsoleWidth (void)
{
	int width = viddef.width, height;
	if (scr_presentation && con_scale)
		scr_presentation->GetSize (con_scale->value, &width, &height);
	return width;
}

void SCR_BeginPresentation (present_scope_t *saved, int space)
{
	cvar_t *scale, *font;
	int width, height, unused_width, unused_height;
	float base, selected;

	saved->width = viddef.width;
	saved->height = viddef.height;
	saved->x = scr_vrect.x;
	saved->y = scr_vrect.y;
	saved->vwidth = scr_vrect.width;
	saved->vheight = scr_vrect.height;
	saved->factor = scr_ui_factor;
	if (!scr_presentation)
		return;
	scale = space == PRESENT_CONSOLE ? con_scale : scr_scale;
	font = space == PRESENT_CONSOLE ? con_font : scr_font;
	if (!scale || !font)
		return;
	base = scr_presentation->GetSize (-1, &unused_width, &unused_height);
	selected = scr_presentation->GetSize (scale->value, &width, &height);
	if (!(base >= 1 && base <= 1024 && selected >= 1 && selected <= 1024) ||
		width < 64 || width > 0xfff8 || height < 48 || height > 0xfffe)
		return;
	scr_ui_factor = selected / base;
	viddef.width = width;
	viddef.height = height;
	scr_vrect.x = (int)(saved->x / scr_ui_factor);
	scr_vrect.y = (int)(saved->y / scr_ui_factor);
	scr_vrect.width = (int)(saved->vwidth / scr_ui_factor);
	scr_vrect.height = (int)(saved->vheight / scr_ui_factor);
	scr_presentation->SetSpace (space, scale->value, font->string);
}

void SCR_EndPresentation (const present_scope_t *saved)
{
	viddef.width = saved->width;
	viddef.height = saved->height;
	scr_vrect.x = saved->x;
	scr_vrect.y = saved->y;
	scr_vrect.width = saved->vwidth;
	scr_vrect.height = saved->vheight;
	scr_ui_factor = saved->factor;
	if (scr_presentation)
		scr_presentation->SetSpace (PRESENT_BASE, -1, NULL);
}

static void SCR_ScaleChanged (cvar_t *self, char *old, char *value)
{
	float scale = self->value;
	if (!(scale >= -1 && scale <= 10) || (scale < 0 && scale != -1))
	{
		Com_Printf ("%s must be -1 (global), 0 (automatic), or 1..10.\n", LOG_CLIENT, self->name);
		Cvar_SetValue (self->name, 0);
		return;
	}
	if (scale > 0 && scale < 1)
	{
		Cvar_SetValue (self->name, 1);
		return;
	}
	Con_CheckResize ();
}


float		scr_con_current;	// aproaches scr_conlines at scr_conspeed
float		scr_conlines;		// 0.0 to 1.0 lines of console to display

qboolean	scr_initialized;		// ready to draw

int			scr_draw_loading;



cvar_t		*scr_viewsize;
cvar_t		*scr_conspeed;
cvar_t		*scr_conheight;
cvar_t		*scr_centertime;
cvar_t		*scr_showturtle;
cvar_t		*scr_showpause;
//cvar_t		*scr_printspeed;

cvar_t		*scr_netgraph;
cvar_t		*scr_timegraph;
cvar_t		*scr_sizegraph;
cvar_t		*scr_debuggraph;
cvar_t		*scr_graphheight;
cvar_t		*scr_graphscale;
cvar_t		*scr_graphshift;
cvar_t		*scr_drawall;
cvar_t		*scr_chathud;
cvar_t		*scr_chathud_lines;
cvar_t		*scr_chathud_ignore_duplicates;
cvar_t		*scr_chathud_colored;
cvar_t		*scr_chathud_x;
cvar_t		*scr_chathud_y;
cvar_t		*scr_chathud_highlight;
cvar_t		*scr_chathud_highlight_char;

typedef struct
{
	int		x1, y1, x2, y2;
} dirty_t;

dirty_t		scr_dirty, scr_old_dirty[2];

char		crosshair_pic[8];
int			crosshair_width, crosshair_height;

void SCR_TimeRefresh_f (void);
void SCR_Loading_f (void);


/*
===============================================================================

BAR GRAPHS

===============================================================================
*/

/*
==============
CL_AddNetgraph

A new packet was just parsed
==============
*/
void CL_AddNetgraph (void)
{
	int		i;
	int		in;
	int		ping;

	// if using the debuggraph for something else, don't
	// add the net lines
	if (scr_debuggraph->intvalue || scr_timegraph->intvalue || scr_sizegraph->intvalue)
		return;

	for (i=0 ; i<cls.netchan.dropped ; i++)
		SCR_DebugGraph (30, 0x40);

	for (i=0 ; i<cl.surpressCount ; i++)
		SCR_DebugGraph (30, 0xdf);

	// see what the latency was on this packet
	in = cls.netchan.incoming_acknowledged & (CMD_BACKUP-1);
	ping = cls.realtime - cl.cmd_time[in];
	ping /= 30;
	if (ping > 30)
		ping = 30;
	SCR_DebugGraph ((float)ping, 0xd0);
}

void CL_AddSizegraph (void)
{
	int		ping;
	int		color;

	// if using the debuggraph for something else, don't
	// add the net lines
	if (scr_debuggraph->intvalue || scr_timegraph->intvalue || scr_netgraph->intvalue)
		return;
	
	for (ping=0 ; ping<cl.surpressCount ; ping++)
		SCR_DebugGraph (30, 0);

	for (ping=0 ; ping<cls.netchan.dropped ; ping++)
		SCR_DebugGraph (30, 111);

	ping = net_message.cursize;

	if (ping < 200)
		color = 61;
	else if (ping < 500)
		color = 59;
	else if (ping < 800)
		color = 57;
	else if (ping < 1200)
		color = 224;
	else
		color = 242;

	ping /= 40;
	if (ping > 30)
		ping = 30;

	SCR_DebugGraph ((float)ping, color);
}

typedef struct
{
	float	value;
	int		color;
} graphsamp_t;

#define	DEBUGGRAPH_SAMPLES	2048
#define	DEBUGGRAPH_MASK		2047

static	int			current_graph_value;
static	graphsamp_t	values[DEBUGGRAPH_SAMPLES];

/*
==============
SCR_DebugGraph
==============
*/
void EXPORT SCR_DebugGraph (float value, int color)
{
	values[current_graph_value&DEBUGGRAPH_MASK].value = value;
	values[current_graph_value&DEBUGGRAPH_MASK].color = color;
	current_graph_value++;
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph (void)
{
	int		a, x, y, w, i, h;
	float	v;
	int		color;

	//
	// draw the graph
	//
	w = scr_vrect.width;

	x = scr_vrect.x;
	y = scr_vrect.y+scr_vrect.height;
	//re.DrawFill (x, y-scr_graphheight->value, w, scr_graphheight->value, 8);

	for (a=0 ; a<w ; a++)
	{
		i = (current_graph_value-1-a+DEBUGGRAPH_SAMPLES) & DEBUGGRAPH_MASK;
		v = values[i].value;
		color = values[i].color;
		v = v*scr_graphscale->value + scr_graphshift->value;
		
		if (FLOAT_LT_ZERO(v))
			v += scr_graphheight->value * (1+(int)(-v/scr_graphheight->value));
		h = (int)v % scr_graphheight->intvalue;
		re.DrawFill (x+w-1-a, y - h, 1,	h, color);
	}
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char		scr_centerstring[1024];
//float		scr_centertime_start;	// for slow victory printing
uint32		scr_centertime_off;
int			scr_center_lines;
int			scr_erase_center;

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint (char *str)
{
	int			len;
	char		*s;
	qboolean	itsTheSameAsBefore;

	if (!strcmp (scr_centerstring, str))
		itsTheSameAsBefore = true;
	else
		itsTheSameAsBefore = false;

	strncpy (scr_centerstring, str, sizeof(scr_centerstring)-1);
	scr_centertime_off = (cls.realtime + (int)scr_centertime->value * 1000);
	//scr_centertime_start = cl.time;

	// count the number of lines for centering
	scr_center_lines = 1;
	s = str;
	while (*s)
	{
		if (*s == '\n')
			scr_center_lines++;
		s++;
	}

	// echo it to the console

	//r1: less spam plz
	if (!itsTheSameAsBefore)
	{
		/*s = str;
		while (*s) {
			if (*s == '\n')
				*s = ' ';
			s++;
		}*/
		Com_Printf ("%s\n", LOG_CLIENT, str);
	}

	//strip newline for trigger match
	len = strlen(str);
	if (str[len-1] == '\n')
		str[len-1] = '\0';

	Cmd_ExecTrigger (str); //Triggers

	/*s = str;
	do	
	{
	// scan the width of the line
		i = 0;
		for (l=0 ; l<40 ; l++)
			if (s[l] == '\n')
				continue;
			if (!s[l])
				break;
		//for (i=0 ; i<(40-l)/2 ; i++)
		//	line[i] = ' ';

		for (j=0 ; j<l ; j++)
		{
			line[i++] = s[j];
		}

		line[i] = '\n';
		line[i+1] = 0;

		Com_Printf ("%s", line);

		while (*s && *s != '\n')
			s++;

		if (!*s)
			break;
		s++;		// skip the \n
	} while (1);
	//Com_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
	//Con_ClearNotify ();*/
}


void SCR_DrawCenterString (void)
{
	char	*start;
	int		l;
	int		j;
	int		x, y;
	int		remaining;

// the finale prints the characters one at a time
	remaining = 9999;

	scr_erase_center = 0;
	start = scr_centerstring;

	if (scr_center_lines <= 4)
		y = (int)(viddef.height*0.35f);
	else
		y = 48;

	for (;;)	
	{
	// scan the width of the line
		for (l=0 ; l<40 ; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (viddef.width - l*8)/2;
		SCR_AddDirtyPoint (x, y);
		for (j=0 ; j<l ; j++, x+=8)
		{
			re.DrawChar (x, y, start[j]);	
			if (!remaining--)
				return;
		}
		SCR_AddDirtyPoint (x, y+8);
			
		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++;		// skip the \n
	};
}

void SCR_CheckDrawCenterString (void)
{
	if (scr_centertime_off <= cls.realtime)
		return;

	SCR_DrawCenterString ();
}

//=============================================================================

/*
=================
SCR_CalcVrect

Sets scr_vrect, the coordinates of the rendered window
=================
*/
static void SCR_CalcVrect (void)
{
	int		size;

	size = scr_viewsize->intvalue;

	scr_vrect.width = viddef.width*size/100;
	scr_vrect.width &= ~7;

	scr_vrect.height = viddef.height*size/100;
	scr_vrect.height &= ~1;

	scr_vrect.x = (viddef.width - scr_vrect.width)/2;
	scr_vrect.y = (viddef.height - scr_vrect.height)/2;
}


/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f (void)
{
	Cvar_SetValue ("viewsize", (float)scr_viewsize->intvalue+10);
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f (void)
{
	Cvar_SetValue ("viewsize", (float)scr_viewsize->intvalue-10);
}

/*
=================
SCR_Sky_f

Set a specific sky and rotation speed
=================
*/
void SCR_Sky_f (void)
{
	float	rotate;
	vec3_t	axis;

	if (Cmd_Argc() < 2)
	{
		Com_Printf ("Usage: sky <basename> <rotate> <axis x y z>\n", LOG_CLIENT);
		return;
	}

	if (Cmd_Argc() > 2)
		rotate = (float)atof(Cmd_Argv(2));
	else
		rotate = 0;

	if (Cmd_Argc() == 6)
	{
		axis[0] = (float)atof(Cmd_Argv(3));
		axis[1] = (float)atof(Cmd_Argv(4));
		axis[2] = (float)atof(Cmd_Argv(5));
	}
	else
	{
		axis[0] = 0;
		axis[1] = 0;
		axis[2] = 1;
	}

	re.SetSky (Cmd_Argv(1), rotate, axis);
}

//============================================================================


#define CHATHUD_MAX_LINES 32

static char		chathud_messages[CHATHUD_MAX_LINES][512];
static unsigned	chathud_index = 0;

void SCR_ClearChatMessages (void)
{
	int	i;

	for (i = 0; i < CHATHUD_MAX_LINES; i++)
		chathud_messages[i][0] = 0;

	chathud_index = 0;
}

void SCR_Chathud_Changed (cvar_t *self, char *old, char *newValue)
{
	if (self->intvalue >= CHATHUD_MAX_LINES)
		Cvar_Set (self->name, "32");
	else if (self->intvalue < 1)
		Cvar_Set (self->name, "1");

	SCR_ClearChatMessages ();
}

void SCR_DrawChatHud (void)
{
	int		x, v;
	int		i, j;

	if (!scr_chathud_y->intvalue)
		v = cl.refdef.height - (8 * (scr_chathud_lines->intvalue + 4));
	else
		v = scr_chathud_y->intvalue;

	for (i = chathud_index, j = 0; j <  scr_chathud_lines->intvalue; i++, j++)
	{
		for (x = 0; chathud_messages[i % scr_chathud_lines->intvalue][x] ; x++)
			re.DrawChar ( (x+scr_chathud_x->intvalue)<<3, v, chathud_messages[i % scr_chathud_lines->intvalue][x]);

		v += 8;
	}
}

void SCR_AddChatMessage (const char *chat)
{
	unsigned	i, index;
	size_t		j;
	size_t		length;
	char		*p;
	char		tempchat[512];

	index = chathud_index % scr_chathud_lines->intvalue;

	Q_strncpy (tempchat, chat, sizeof(tempchat)-1);

	p = strchr (tempchat, '\n');
	if (p)
		p[0] = 0;
	
	if (scr_chathud_colored->intvalue)
	{
		for (j = 0; tempchat[j]; j++)
			tempchat[j] |= 128;
	}

	if (scr_chathud_highlight->intvalue)
	{
		char	player_name[MAX_QPATH];
		int			best;
		size_t		bestlen;
		ptrdiff_t	bestoffset = 0;

		best = -1;
		bestlen = 0;

		for (i = 0; i < cl.maxclients; i++)
		{
			if (cl.configstrings[CS_PLAYERSKINS + i][0])
			{
				char *separator, *foundname;
				
				separator = strchr (cl.configstrings[CS_PLAYERSKINS + i], '\\');
				if (!separator)
					continue;
				
				Q_strncpy (player_name, cl.configstrings[CS_PLAYERSKINS + i], (separator - cl.configstrings[CS_PLAYERSKINS + i]) >= sizeof(player_name)-1 ? sizeof(player_name) - 1 : (separator - cl.configstrings[CS_PLAYERSKINS + i]));

				foundname = strstr (tempchat, player_name);
				if (foundname != NULL)
				{
					size_t	len;

					len = strlen (player_name);

					if (len > bestlen)
					{
						best = i;
						bestlen = len;
						bestoffset = (foundname - tempchat);
					}
				}
			}
		}

		if (best != -1)
		{
			ptrdiff_t start;

			length = bestlen;

			if (scr_chathud_highlight->intvalue & 4)
				length++;

			if (!(scr_chathud_highlight->intvalue & 2))
			{
				length += bestoffset;
				start = 0;
			}
			else
			{
				start = bestoffset;
			}

			if (scr_chathud_highlight_char->string[0])
			{
				char *p = strchr (tempchat + start + length, scr_chathud_highlight_char->string[0]);
				if (p)
					length += p - (tempchat + start + length); 
			}

			if (scr_chathud_colored->intvalue)
			{
				for (j = start; j < start + length; j++)
					tempchat[j] &= ~128;
			}
			else
			{
				for (j = start; j < start + length; j++)
					tempchat[j] |= 128;
			}
		}
	}

	//unfortunate way of doing this but necessary
	if (scr_chathud_ignore_duplicates->intvalue && chathud_index)
	{
		for (i = 1; i <= scr_chathud_ignore_duplicates->intvalue; i++)
		{
			if (!strcmp (tempchat, chathud_messages[(chathud_index - i) % scr_chathud_lines->intvalue]))
				return;
			if (chathud_index == i)
				break;
		}
	}

	strncpy (chathud_messages[chathud_index % scr_chathud_lines->intvalue], tempchat, sizeof(chathud_messages[0])-1);
	chathud_index++;
}

void SCR_Conheight_Changed (cvar_t *self, char *old, char *newValue)
{
	if (self->value > 1.0f)
	{
		//Com_Printf ("scr_conheight ranges from 0 to 1\n", LOG_CLIENT);
		Cvar_ForceSet ("scr_conheight", "1");
	}
	else if (FLOAT_LT_ZERO(self->value))
	{
		//Com_Printf ("scr_conheight ranges from 0 to 1\n", LOG_CLIENT);
		Cvar_ForceSet ("scr_conheight", "0");
	}
	if (Cvar_FindVar ("con_height") && Cvar_VariableValue ("con_height") != self->value)
		Cvar_ForceSet ("con_height", self->string);
}

static void _viewsize_changed (cvar_t *self, char *oldValue, char *newValue)
{
	// bound viewsize
	if (self->intvalue < 40)
		Cvar_Set (self->name, "40");
	else if (self->intvalue > 100)
		Cvar_Set (self->name, "100");
}

/*
==================
SCR_Init
==================
*/
void SCR_Init (void)
{
	scr_scale = Cvar_Get ("scr_scale", "0", CVAR_ARCHIVE);
	con_scale = Cvar_Get ("con_scale", "0", CVAR_ARCHIVE);
	scr_font = Cvar_Get ("scr_font", "conchars", CVAR_ARCHIVE);
	con_font = Cvar_Get ("con_font", "conchars", CVAR_ARCHIVE);
	scr_scale->changed = con_scale->changed = SCR_ScaleChanged;
	SCR_ScaleChanged (scr_scale, scr_scale->string, scr_scale->string);
	SCR_ScaleChanged (con_scale, con_scale->string, con_scale->string);
	scr_viewsize = Cvar_Get ("viewsize", "100", CVAR_ARCHIVE);
	scr_conspeed = Cvar_Get ("scr_conspeed", "3", 0);
	scr_conheight = Cvar_Get ("scr_conheight", "0.5", 0);
	scr_showturtle = Cvar_Get ("scr_showturtle", "1", 0);
	scr_showpause = Cvar_Get ("scr_showpause", "1", 0);
	scr_centertime = Cvar_Get ("scr_centertime", "2.5", 0);
	//scr_printspeed = Cvar_Get ("scr_printspeed", "8", 0);
	scr_netgraph = Cvar_Get ("netgraph", "0", 0);
	scr_timegraph = Cvar_Get ("timegraph", "0", 0);
	scr_sizegraph = Cvar_Get ("sizegraph", "0", 0);
	scr_debuggraph = Cvar_Get ("debuggraph", "0", 0);
	scr_graphheight = Cvar_Get ("graphheight", "32", 0);
	scr_graphscale = Cvar_Get ("graphscale", "1", 0);
	scr_graphshift = Cvar_Get ("graphshift", "0", 0);
	scr_drawall = Cvar_Get ("scr_drawall", "0", 0);

	scr_chathud = Cvar_Get ("scr_chathud", "0", 0);
	scr_chathud_lines = Cvar_Get ("scr_chathud_lines", "4", 0);
	scr_chathud_colored = Cvar_Get ("scr_chathud_colored", "0", 0);
	scr_chathud_ignore_duplicates = Cvar_Get ("scr_chathud_ignore_duplicates", "1", 0);
	scr_chathud_x = Cvar_Get ("scr_chathud_x", "0", 0);
	scr_chathud_y = Cvar_Get ("scr_chathud_y", "0", 0);
	scr_chathud_highlight = Cvar_Get ("scr_chathud_highlight", "0", 0);
	scr_chathud_highlight_char = Cvar_Get ("scr_chathud_highlight_char", " ", 0);

	scr_chathud_lines->changed = SCR_Chathud_Changed;
	scr_chathud_lines->changed (scr_chathud_lines, scr_chathud_lines->string, scr_chathud_lines->string);

	scr_conheight->changed = SCR_Conheight_Changed;
	scr_conheight->changed (scr_conheight, scr_conheight->string, scr_conheight->string);

	scr_viewsize->changed = _viewsize_changed;
	scr_viewsize->changed (scr_viewsize, scr_viewsize->string, scr_viewsize->string);

//
// register our commands
//
	Cmd_AddCommand ("timerefresh",SCR_TimeRefresh_f);
	Cmd_AddCommand ("loading",SCR_Loading_f);
	Cmd_AddCommand ("sizeup",SCR_SizeUp_f);
	Cmd_AddCommand ("sizedown",SCR_SizeDown_f);
	Cmd_AddCommand ("sky",SCR_Sky_f);
	Cmd_AddCommand ("clearchathud", SCR_ClearChatMessages);

	scr_initialized = true;
}


/*
==============
SCR_DrawNet
==============
*/
void SCR_DrawNet (void)
{
	if (cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged 
		< CMD_BACKUP-1)
		return;

	re.DrawPic (scr_vrect.x+64, scr_vrect.y, "net");
}

/*
==============
SCR_DrawPause
==============
*/
void SCR_DrawPause (void)
{
	int		w, h;

	if (!scr_showpause->intvalue)		// turn off for screenshots
		return;

	if (!cl_paused->intvalue)
		return;

	re.DrawGetPicSize (&w, &h, "pause");
	re.DrawPic ((viddef.width-w)/2, viddef.height/2 + 8, "pause");
}

/*
==============
SCR_DrawLoading
==============
*/
void SCR_DrawLoading (void)
{
	int		w, h;
		
	if (!scr_draw_loading)
		return;

	scr_draw_loading = 0;
	re.DrawGetPicSize (&w, &h, "loading");
	re.DrawPic ((viddef.width-w)/2, (viddef.height-h)/2, "loading");
}

//=============================================================================

/*
==================
SCR_RunConsole

Scroll it up or down
==================
*/
void SCR_RunConsole (void)
{
// decide on the height of the console
	if (cls.key_dest == key_console)
		//scr_conlines = 1;//0.5;		// half screen
		scr_conlines = scr_conheight->value;// / 100.0;
	else
		scr_conlines = 0;				// none visible
	
	if (scr_conlines < scr_con_current)
	{
		scr_con_current -= scr_conspeed->value*cls.frametime;
		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;

	}
	else if (scr_conlines > scr_con_current)
	{
		scr_con_current += scr_conspeed->value*cls.frametime;
		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
	}

}

/*
==================
SCR_DrawConsole
==================
*/
void SCR_DrawConsole (void)
{
	//Con_CheckResize ();
	
	if (cls.state <= ca_connecting)
	{	// forced full screen console
		Con_DrawConsole (1.0);
		return;
	}

	if (cls.state != ca_active || !cl.refresh_prepped)
	{	// connected, but can't render
		Con_DrawConsole (0.5);
		re.DrawFill (0, viddef.height/2, viddef.width, viddef.height/2, 0);
		return;
	}

	if (FLOAT_NE_ZERO(scr_con_current))
	{
		Con_DrawConsole (scr_con_current);
	}
	else
	{
		if (cls.key_dest == key_game || cls.key_dest == key_message)
		{
			Con_DrawNotify ();	// only draw notify in game
		}
	}
}

//=============================================================================

/*
================
SCR_BeginLoadingPlaque
================
*/
void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds ();
	cl.sound_prepped = false;		// don't play ambients
#ifdef CD_AUDIO
	CDAudio_Stop ();
#endif
	if (cls.disable_screen)
		return;
	if (developer->intvalue)
		return;
	if (cls.state == ca_disconnected)
		return;	// if at console, don't bring up the plaque
	if (cls.key_dest == key_console)
		return;
#ifdef CINEMATICS
	if (cl.cinematictime > 0)
		scr_draw_loading = 2;	// clear to balack first
	else
#endif
		scr_draw_loading = 1;
	SCR_UpdateScreen ();
	cls.disable_screen = Sys_Milliseconds ();
	cls.disable_servercount = cl.servercount;
}

/*
================
SCR_EndLoadingPlaque
================
*/
void SCR_EndLoadingPlaque (void)
{
	cls.disable_screen = 0;
	Con_ClearNotify ();
}

/*
================
SCR_Loading_f
================
*/
void SCR_Loading_f (void)
{
	SCR_BeginLoadingPlaque ();
}

/*
================
SCR_TimeRefresh_f
================
*/
int EXPORT entitycmpfnc( const entity_t *a, const entity_t *b )
{
	/*
	** all other models are sorted by model then skin
	*/
	if ( a->model == b->model )
	{
		return (int)( ( ptrdiff_t ) a->skin - ( ptrdiff_t ) b->skin );
	}
	else
	{
		return (int)( ( ptrdiff_t ) a->model - ( ptrdiff_t ) b->model );
	}
}

void SCR_TimeRefresh_f (void)
{
	int				i;
	unsigned int	start, stop;
	float			time;

	if ( cls.state != ca_active )
		return;

	start = Sys_Milliseconds ();

	if (Cmd_Argc() == 2)
	{	// run without page flipping
		re.BeginFrame( 0 );
		CL_JumpLayoutPrepare ();
		HUD_LayoutFrame ();
		for (i=0 ; i<128 ; i++)
		{
			cl.refdef.viewangles[1] = i/128.0f*360.0f;
			re.RenderFrame (&cl.refdef);
		}
		re.EndFrame();
	}
	else
	{
		for (i=0 ; i<128 ; i++)
		{
			cl.refdef.viewangles[1] = i/128.0f*360.0f;

			re.BeginFrame( 0 );
			CL_JumpLayoutPrepare ();
			HUD_LayoutFrame ();
			re.RenderFrame (&cl.refdef);
			re.EndFrame();
		}
	}

	stop = Sys_Milliseconds ();
	time = (unsigned)(stop-start)/1000.0f;

	if (time > 0)
		Com_Printf ("%f seconds (%f fps)\n", LOG_CLIENT, time, 128 / time);
	else
		Com_Printf ("%f seconds (fps unavailable)\n", LOG_CLIENT, time);
}

/*
=================
SCR_AddDirtyPoint
=================
*/
void SCR_AddDirtyPoint (int x, int y)
{
	if (scr_ui_factor != 1)
	{
		int width, height, extra = (int)ceil (scr_ui_factor);
		double px = x * (double)scr_ui_factor, py = y * (double)scr_ui_factor;
		scr_presentation->GetSize (-1, &width, &height);
		x = px < 0 ? 0 : px >= width ? width - 1 :
									   (int)floor (px);
		y = py < 0 ? 0 : py >= height ? height - 1 :
										(int)floor (py);
		/* Include the full scaled logical pixel, clipped to the base screen. */
		if (x + extra > scr_dirty.x2)
			scr_dirty.x2 = x + extra < width ? x + extra : width - 1;
		if (y + extra > scr_dirty.y2)
			scr_dirty.y2 = y + extra < height ? y + extra : height - 1;
	}
	if (x < scr_dirty.x1)
		scr_dirty.x1 = x;
	if (x > scr_dirty.x2)
		scr_dirty.x2 = x;
	if (y < scr_dirty.y1)
		scr_dirty.y1 = y;
	if (y > scr_dirty.y2)
		scr_dirty.y2 = y;
}

void SCR_DirtyScreen (void)
{
	SCR_AddDirtyPoint (0, 0);
	SCR_AddDirtyPoint (viddef.width-1, viddef.height-1);
}

/*
==============
SCR_TileClear

Clear any parts of the tiled background that were drawn on last frame
==============
*/
void SCR_TileClear (void)
{
	int		i;
	int		top, bottom, left, right;
	dirty_t	clear;

	if (scr_viewsize->intvalue == 100)
		return;		// full screen rendering

	if (scr_drawall->intvalue)
		SCR_DirtyScreen ();	// for power vr or broken page flippers...

	if (scr_con_current == 1.0f)
		return;		// full screen console

#ifdef CINEMATICS
	if (cl.cinematictime > 0)
		return;		// full screen cinematic
#endif

	// erase rect will be the union of the past three frames
	// so tripple buffering works properly
	clear = scr_dirty;
	for (i=0 ; i<2 ; i++)
	{
		if (scr_old_dirty[i].x1 < clear.x1)
			clear.x1 = scr_old_dirty[i].x1;
		if (scr_old_dirty[i].x2 > clear.x2)
			clear.x2 = scr_old_dirty[i].x2;
		if (scr_old_dirty[i].y1 < clear.y1)
			clear.y1 = scr_old_dirty[i].y1;
		if (scr_old_dirty[i].y2 > clear.y2)
			clear.y2 = scr_old_dirty[i].y2;
	}

	scr_old_dirty[1] = scr_old_dirty[0];
	scr_old_dirty[0] = scr_dirty;

	scr_dirty.x1 = 9999;
	scr_dirty.x2 = -9999;
	scr_dirty.y1 = 9999;
	scr_dirty.y2 = -9999;

	// don't bother with anything convered by the console
	top = (int)(scr_con_current*viddef.height);
	if (top >= clear.y1)
		clear.y1 = top;

	if (clear.y2 <= clear.y1)
		return;		// nothing disturbed

	top = scr_vrect.y;
	bottom = top + scr_vrect.height-1;
	left = scr_vrect.x;
	right = left + scr_vrect.width-1;

	if (clear.y1 < top)
	{	// clear above view screen
		i = clear.y2 < top-1 ? clear.y2 : top-1;
		re.DrawTileClear (clear.x1 , clear.y1,
			clear.x2 - clear.x1 + 1, i - clear.y1+1, "backtile");
		clear.y1 = top;
	}

	if (clear.y2 > bottom)
	{	// clear below view screen
		i = clear.y1 > bottom+1 ? clear.y1 : bottom+1;
		re.DrawTileClear (clear.x1, i,
			clear.x2-clear.x1+1, clear.y2-i+1, "backtile");
		clear.y2 = bottom;
	}

	if (clear.x1 < left)
	{	// clear left of view screen
		i = clear.x2 < left-1 ? clear.x2 : left-1;
		re.DrawTileClear (clear.x1, clear.y1,
			i-clear.x1+1, clear.y2 - clear.y1 + 1, "backtile");
		clear.x1 = left;
	}

	if (clear.x2 > right)
	{	// clear left of view screen
		i = clear.x1 > right+1 ? clear.x1 : right+1;
		re.DrawTileClear (i, clear.y1,
			clear.x2-i+1, clear.y2 - clear.y1 + 1, "backtile");
		clear.x2 = right;
	}

}


//===============================================================


#define STAT_MINUS		10	// num frame for '-' stats digit
static char		*sb_nums[2][11] = 
{
	{
			"num_0", "num_1", "num_2", "num_3",	"num_4", "num_5",
			"num_6", "num_7", "num_8", "num_9", "num_minus"
	},
	{
		"anum_0", "anum_1", "anum_2", "anum_3", "anum_4", "anum_5",
		"anum_6", "anum_7", "anum_8", "anum_9", "anum_minus"
	}
};

#define	ICON_WIDTH	24
#define	ICON_HEIGHT	24
#define	CHAR_WIDTH	16
#define	ICON_SPACE	8



/*
================
SizeHUDString

Allow embedded \n in the string
================
*/
void SizeHUDString (char *string, int *w, int *h)
{
	int		lines, width, current;

	lines = 1;
	width = 0;

	current = 0;
	while (*string)
	{
		if (*string == '\n')
		{
			lines++;
			current = 0;
		}
		else
		{
			current++;
			if (current > width)
				width = current;
		}
		string++;
	}

	*w = width * 8;
	*h = lines * 8;
}

void DrawHUDString (const char *string, int x, int y, int centerwidth, int xor)
{
	int		margin;
	char	line[1024];
	int		width;
	int		i;

	margin = x;

	while (*string)
	{
		// scan out one line of text from the string
		width = 0;
		while (*string && *string != '\n')
			line[width++] = *string++;
		line[width] = 0;

		if (centerwidth)
			x = margin + (centerwidth - width*8)/2;
		else
			x = margin;
		for (i=0 ; i<width ; i++)
		{
			re.DrawChar (x, y, line[i]^xor);
			x += 8;
		}
		if (*string)
		{
			string++;	// skip the \n
			x = margin;
			y += 8;
		}
	}
}


/*
==============
SCR_DrawField
==============
*/
void SCR_DrawField (int x, int y, int color, int width, int value)
{
	char	num[16], *ptr;
	int		l;
	int		frame;

	if (width < 1)
		return;

	// draw number string
	if (width > 5)
		width = 5;

	SCR_AddDirtyPoint (x, y);
	SCR_AddDirtyPoint (x+width*CHAR_WIDTH+2, y+23);

	Com_sprintf (num, sizeof(num), "%i", value);
	l = (int)strlen(num);
	if (l > width)
		l = width;
	x += 2 + CHAR_WIDTH*(width - l);

	ptr = num;
	while (ptr[0] && l)
	{
		if (ptr[0] == '-')
			frame = STAT_MINUS;
		else
			frame = ptr[0] -'0';

		re.DrawPic (x, y, sb_nums[color][frame]);
		x += CHAR_WIDTH;
		ptr++;
		l--;
	}
}


/*
===============
SCR_TouchPics

Allows rendering code to cache all needed sbar graphics
===============
*/
void SCR_TouchPics (void)
{
	int		i, j;

	for (i=0 ; i<2 ; i++)
		for (j=0 ; j<11 ; j++)
			re.RegisterPic (sb_nums[i][j]);

	if (crosshair->intvalue)
	{
		if (crosshair->intvalue < 0)
			Cvar_Set ("crosshair", "0");

		Com_sprintf (crosshair_pic, sizeof(crosshair_pic), "ch%i", crosshair->intvalue);
		re.DrawGetPicSize (&crosshair_width, &crosshair_height, crosshair_pic);
		if (!crosshair_width)
			crosshair_pic[0] = 0;
	}
}

/*
================
SCR_ExecuteLayoutString 

================
*/
void SCR_ExecuteLayoutString (char *s)
{
	int				x, y;
	int				value;
	const char		*token;
	int				width;
	int				index;
	clientinfo_t	*ci;
	char *layout = s;
	qboolean grouped;
	int group = -1, next_group;

	if (cls.state != ca_active || !cl.refresh_prepped)
		return;

	if (!s[0])
		return;

	x = 0;
	y = 0;
	width = 3;
	grouped = CL_JumpEnabled () && s == cl.configstrings[CS_STATUSBAR] && HUD_LayoutMatch (s);
	if (grouped)
		CL_JumpLayoutNativeBegin (-1);

	while (s)
	{
		if (grouped)
		{
			next_group = HUD_LayoutToken (layout, s);
			if (next_group != group)
			{
				HUD_LayoutBegin (next_group);
				group = next_group;
			}
		}
		token = COM_Parse (&s);

		//r1: rewrote how this is parsed to minimize use of redundant strcmps
		//note this only works since token is defined as char[512]
		switch (token[0])
		{
			case 'x':
				if (token[1] == 'v')
				{
					token = COM_Parse (&s);
					x = viddef.width/2 - 160 + atoi(token);
					continue;
				}
				else if (token[1] == 'r')
				{
					token = COM_Parse (&s);
					x = viddef.width + atoi(token);
					continue;
				}
				else if (token[1] == 'l')
				{
					token = COM_Parse (&s);
					x = atoi(token);
					continue;
				}
				break;

			case 'y':
				if (token[1] == 'v')
				{
					token = COM_Parse (&s);
					y = viddef.height/2 - 120 + atoi(token);
					continue;
				}
				else if (token[1] == 'b')
				{
					token = COM_Parse (&s);
					y = viddef.height + atoi(token);
					continue;
				}
				else if (token[1] == 't')
				{
					token = COM_Parse (&s);
					y = atoi(token);
					continue;
				}
				break;
			case 0:
				break;
			default:
				if (token[0] == 'p' && token[1] == 'i' && token[2] == 'c')
				{	// draw a pic from a stat number
					if (token[3] == 'n' && token[4] == 0)
					{	// draw a pic from a name
						token = COM_Parse (&s);
						SCR_AddDirtyPoint (x, y);
						SCR_AddDirtyPoint (x+23, y+23);
						re.DrawPic (x, y, (char *)token);
					}
					else
					{
						token = COM_Parse (&s);

						index = atoi(token);

						if (index < 0 || index >= MAX_STATS)
							Com_Error (ERR_DROP, "Bad stats index %d in block 'pic' whilst parsing layout string", index);

						value = cl.frame.playerstate.stats[index];

						if (value >= MAX_IMAGES)
							Com_Error (ERR_DROP, "Bad picture index %d in block 'pic' whilst parsing layout string", value);

						if (cl.configstrings[CS_IMAGES+value][0])
						{
							SCR_AddDirtyPoint (x, y);
							SCR_AddDirtyPoint (x+23, y+23);
							re.DrawPic (x, y, cl.configstrings[CS_IMAGES+value]);
						}
					}
					continue;
				}
				else if (token[0] == 'n' && token[1] == 'u' && token[2] == 'm')
				{	
					// draw a number
					token = COM_Parse (&s);
					width = atoi(token);
					token = COM_Parse (&s);

					index = atoi(token);

					if (index < 0 || index >= MAX_STATS)
						Com_Error (ERR_DROP, "Bad stats index %d in block 'num' whilst parsing layout string", index);

					value = cl.frame.playerstate.stats[index];
					SCR_DrawField (x, y, 0, width, value);
					continue;
				}
				else  if (token[1] == 'n' && token[2] == 'u' && token[3] == 'm')
				{
					int		color;

					switch (token[0])
					{
					case 'a':
						width = 3;
						value = cl.frame.playerstate.stats[STAT_AMMO];
						if (value > 5)
							color = 0;	// green
						else if (value >= 0)
							color = (cl.frame.serverframe>>2) & 1;		// flash
						else
							continue;	// negative number = don't show

						if (cl.frame.playerstate.stats[STAT_FLASHES] & 4)
							re.DrawPic (x, y, "field_3");

						SCR_DrawField (x, y, color, width, value);
						continue;

					case 'h':
						// health number
						width = 3;
						value = cl.frame.playerstate.stats[STAT_HEALTH];
						if (value > 25)
							color = 0;	// green
						else if (value > 0)
							color = (cl.frame.serverframe>>2) & 1;		// flash
						else
							color = 1;

						if (cl.frame.playerstate.stats[STAT_FLASHES] & 1)
							re.DrawPic (x, y, "field_3");

						SCR_DrawField (x, y, color, width, value);
						continue;

					case 'r':
						width = 3;
						value = cl.frame.playerstate.stats[STAT_ARMOR];
						if (value < 1)
							continue;

						color = 0;	// green

						if (cl.frame.playerstate.stats[STAT_FLASHES] & 2)
							re.DrawPic (x, y, "field_3");

						SCR_DrawField (x, y, color, width, value);
						continue;
					}
				}
				else if (!strcmp(token, "stat_string"))
				{
					token = COM_Parse (&s);
					index = atoi(token);

					if (index < 0 || index >= MAX_STATS)
						Com_Error (ERR_DROP, "Bad stats index %d in block 'stat_string' whilst parsing layout string", index);

					index = cl.frame.playerstate.stats[index];

					if (index < 0 || index >= MAX_CONFIGSTRINGS)
						Com_Error (ERR_DROP, "Bad stat_string index %d whilst parsing layout string", index);

					DrawString (x, y, cl.configstrings[index]);
					continue;
				}

				if (!strncmp(token, "cstring", 6))
				{
					if (token[7] == '2' && token[8] == 0)
					{
						token = COM_Parse (&s);
						DrawHUDString (token, x, y, 320,0x80);
						continue;
					}
					else
					{
						token = COM_Parse (&s);
						DrawHUDString (token, x, y, 320, 0);
						continue;
					}
				}
				else if (!strncmp(token, "string", 5))
				{
					if (token[6] == '2' && token[7] == 0)
					{
						token = COM_Parse (&s);
						DrawAltString (x, y, token);
						continue;
					}
					else
					{
						token = COM_Parse (&s);
						DrawString (x, y, token);
						continue;
					}
				}
				else if (token[0] == 'i' && token[1] == 'f')
				{	// draw a number
					token = COM_Parse (&s);
					index = atoi(token);

					if (index < 0 || index >= MAX_STATS)
						Com_Error (ERR_DROP, "Bad stats index %d in block 'if' whilst parsing layout string", index);

					value = cl.frame.playerstate.stats[index];
					if (!value)
					{	// skip to endif
						/*while (s && strcmp(token, "endif") )
						{
							token = COM_Parse (&s);
						}*/
						//hack for speed
						s = strstr (s, " endif");
						if (s)
							s += 6;
					}

					continue;
				}
				else if (!strcmp(token, "client"))
				{	// draw a deathmatch client block
					int		score, ping, time;

					token = COM_Parse (&s);
					x = viddef.width/2 - 160 + atoi(token);
					token = COM_Parse (&s);
					y = viddef.height/2 - 120 + atoi(token);
					SCR_AddDirtyPoint (x, y);
					SCR_AddDirtyPoint (x+159, y+31);

					token = COM_Parse (&s);
					value = atoi(token);

					if (value >= cl.maxclients || value < 0)
						Com_Error (ERR_DROP, "Bad client index %d in block 'client' whilst parsing layout string", value);

					ci = &cl.clientinfo[value];

					token = COM_Parse (&s);
					score = atoi(token);

					token = COM_Parse (&s);
					ping = atoi(token);

					token = COM_Parse (&s);
					time = atoi(token);

					DrawAltString (x+32, y, ci->name);
					DrawString (x+32, y+8,  "Score: ");
					DrawAltString (x+32+7*8, y+8,  va("%i", score));

					DrawString (x+32, y+16, va("Ping:  %ims", ping));
					DrawString (x+32, y+24, va("Time:  %i", time));

					if (!ci->icon)
						ci = &cl.baseclientinfo;
					re.DrawPic (x, y, ci->iconname);
					continue;
				}
				else if (!strcmp(token, "ctf"))
				{	// draw a ctf client block
					int		score, ping;
					char	block[80];

					token = COM_Parse (&s);
					x = viddef.width/2 - 160 + atoi(token);
					token = COM_Parse (&s);
					y = viddef.height/2 - 120 + atoi(token);
					SCR_AddDirtyPoint (x, y);
					SCR_AddDirtyPoint (x+159, y+31);

					token = COM_Parse (&s);

					value = atoi(token);
					
					if (value >= cl.maxclients || value < 0)
						Com_Error (ERR_DROP, "Bad client index %d in block 'ctf' whilst parsing layout string", value);

					ci = &cl.clientinfo[value];

					token = COM_Parse (&s);
					score = atoi(token);

					token = COM_Parse (&s);
					ping = atoi(token);
					if (ping > 999)
						ping = 999;

					sprintf(block, "%3d %3d %-12.12s", score, ping, ci->name);

					if (value == cl.playernum)
						DrawAltString (x, y, block);
					else
						DrawString (x, y, block);
					continue;
				}
		}
	}
	if (grouped)
		CL_JumpLayoutNativeEnd ();
}


/*
================
SCR_DrawStats

The status bar is a small layout program that
is based on the stats array
================
*/
void SCR_DrawStats (void)
{
	SCR_ExecuteLayoutString (cl.configstrings[CS_STATUSBAR]);
}


/*
================
SCR_DrawLayout

================
*/
#define	STAT_LAYOUTS		13

void SCR_DrawLayout (void)
{
	if (!cl.frame.playerstate.stats[STAT_LAYOUTS])
		return;
	SCR_ExecuteLayoutString (cl.layout);
}

//=======================================================

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen (void)
{
	present_scope_t hud;
#ifdef CL_STEREO_SUPPORT
	int i;
	int numframes;
	float separation[2] = { 0, 0 };
#endif

	// if the screen is disabled (loading plaque is up, or vid mode changing)
	// do nothing at all
	if (cls.disable_screen)
	{
		if ((unsigned)(Sys_Milliseconds() - cls.disable_screen) > 120000)
		{
			cls.disable_screen = 0;
			Com_Printf ("Loading plaque timed out.\n", LOG_CLIENT);
		}
		return;
	}

	if (!scr_initialized || !con.initialized)
		return;				// not initialized yet

	/*
	** range check cl_camera_separation so we don't inadvertently fry someone's
	** brain
	*/
#ifdef CL_STEREO_SUPPORT
	if ( cl_stereo_separation->value > 1.0f )
		Cvar_SetValue( "cl_stereo_separation", 1.0 );
	else if (FLOAT_LT_ZERO(cl_stereo_separation->value))
		Cvar_SetValue( "cl_stereo_separation", 0.0 );

	if ( cl_stereo->intvalue )
	{
		numframes = 2;
		separation[0] = -cl_stereo_separation->value / 2;
		separation[1] =  cl_stereo_separation->value / 2;
	}		
	else
	{
		separation[0] = 0;
		separation[1] = 0;
		numframes = 1;
	}
#endif

#ifdef CL_STEREO_SUPPORT
	for ( i = 0; i < numframes; i++ )
	{
		re.BeginFrame( separation[i] );
#else
		re.BeginFrame( 0 );
#endif
		CL_JumpLayoutPrepare ();
		HUD_LayoutFrame ();

		//r1: only update console during load
		if (!cl.refresh_prepped)
		{
			if (cls.key_dest != key_menu)
				SCR_DrawConsole ();
			M_Draw ();
			re.EndFrame();
			return;
		}

		if (scr_draw_loading == 2)
		{	//  loading plaque over black screen
			int		w, h;

			re.CinematicSetPalette(NULL);
			scr_draw_loading = 0;
			re.DrawGetPicSize (&w, &h, "loading");
			re.DrawPic ((viddef.width-w)/2, (viddef.height-h)/2, "loading");
//			re.EndFrame();
//			return;
		} 
		// if a cinematic is supposed to be running, handle menus
		// and console specially
#ifdef CINEMATICS
		else if (cl.cinematictime > 0)
		{
			if (cls.key_dest == key_menu)
			{
				if (cl.cinematicpalette_active)
				{
					re.CinematicSetPalette(NULL);
					cl.cinematicpalette_active = false;
				}
				M_Draw ();
//				re.EndFrame();
//				return;
			}
			else if (cls.key_dest == key_console)
			{
				if (cl.cinematicpalette_active)
				{
					re.CinematicSetPalette(NULL);
					cl.cinematicpalette_active = false;
				}
				SCR_DrawConsole ();
//				re.EndFrame();
//				return;
			}
			else
			{
				SCR_DrawCinematic();
//				re.EndFrame();
//				return;
			}
		}
#endif
		else 
		{

#ifdef CINEMATICS
			// make sure the game palette is active
			if (cl.cinematicpalette_active)
			{
				re.CinematicSetPalette(NULL);
				cl.cinematicpalette_active = false;
			}
#endif
			// do 3D refresh drawing, and then update the screen
			SCR_CalcVrect ();

			// clear any dirty part of the background
			SCR_TileClear ();

#ifdef CL_STEREO_SUPPORT
			V_RenderView ( separation[i] );
#else
			V_RenderView ();
#endif

			if (scr_timegraph->intvalue)
				SCR_DebugGraph (cls.frametime*300, (int)(cls.frametime*300));

			if (scr_debuggraph->intvalue || scr_timegraph->intvalue || scr_netgraph->intvalue || scr_sizegraph->intvalue)
			{
				CL_JumpLayoutNativeBegin (HL_DEBUGGRAPH);
				SCR_DrawDebugGraph ();
				CL_JumpLayoutNativeEnd ();
			}

			SCR_BeginPresentation (&hud, PRESENT_HUD);
			SCR_DrawStats ();
			if (cl.frame.playerstate.stats[STAT_LAYOUTS] & 1)
			{
				if (CL_JumpLayoutCustomized (HL_SCOREBOARD))
					CL_JumpLayoutNativeBegin (HL_SCOREBOARD);
				SCR_DrawLayout ();
				CL_JumpLayoutNativeEnd ();
			}
			if (cl.frame.playerstate.stats[STAT_LAYOUTS] & 2)
			{
				CL_JumpLayoutNativeBegin (HL_INVENTORY);
				CL_DrawInventory ();
				CL_JumpLayoutNativeEnd ();
			}

			CL_JumpLayoutNativeBegin (HL_NETICON);
			SCR_DrawNet ();
			CL_JumpLayoutNativeEnd ();
			CL_JumpLayoutNativeBegin (HL_CENTER);
			SCR_CheckDrawCenterString ();
			CL_JumpLayoutNativeEnd ();

			CL_JumpLayoutNativeBegin (HL_PAUSE);
			SCR_DrawPause ();
			CL_JumpLayoutNativeEnd ();
			SCR_EndPresentation (&hud);
			CL_JumpDrawHUD ();

			SCR_DrawConsole ();

			if (scr_chathud->intvalue)
			{
				SCR_BeginPresentation (&hud, PRESENT_HUD);
				CL_JumpLayoutNativeBegin (HL_CHAT);
				SCR_DrawChatHud ();
				CL_JumpLayoutNativeEnd ();
				SCR_EndPresentation (&hud);
			}

			M_Draw ();

			SCR_DrawLoading ();
		}
#ifdef CL_STEREO_SUPPORT
	}
#endif
	re.EndFrame();
}
