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
// console.c

#include "client.h"
#include "jump/jump_layout.h"
#include "ref_present.h"
#include "con_prompt.h"
#include <time.h>

console_t	con;

cvar_t		*con_notifytime;
static cvar_t *con_notifylines, *con_clock, *con_alpha, *con_background;
static cvar_t *con_scroll, *con_history, *con_timestamps, *con_timestampsformat;
static cvar_t *con_timestampscolor, *con_auto_chat, *con_height;
static con_prompt_t console_prompt, message_prompt;
static qboolean console_interactive, console_remote, console_height_sync;
static netadr_t console_remote_address;
static char console_remote_password[128];
extern char key_lines[32][MAXCMDLINE];
extern int edit_line, key_linepos;
extern qboolean keydown[256];
extern float scr_con_current;
extern void R_SetColor (unsigned int color);
extern void R_ClearColor (void);
extern void R_SetAlpha (float alpha);
extern qboolean Key_GetOverstrikeMode (void);
extern void Key_ConsoleCompleteLegacy (void);

static void Con_Clear_f (void);
extern qboolean CL_JumpParseColor (const char *text, unsigned int *color);

void Con_SaveHistory (void)
{
	char path[MAX_OSPATH];
	if (!con.initialized)
		return;
	Com_sprintf (path, sizeof (path), "%s/.conhistory", FS_Gamedir ());
	Con_PromptSave (&console_prompt, path, con_history->intvalue);
}

void Con_LoadHistory (void)
{
	char path[MAX_OSPATH];
	if (!con.initialized)
		return;
	memset (&console_prompt, 0, sizeof (console_prompt));
	console_remote = false;
	memset (console_remote_password, 0, sizeof (console_remote_password));
	if (con_history->intvalue < 1)
		return;
	Com_sprintf (path, sizeof (path), "%s/.conhistory", FS_Gamedir ());
	if (!Con_PromptLoad (&console_prompt, path))
	{
		Com_sprintf (path, sizeof (path), "%s/%s/.conhistory", Cvar_VariableString ("basedir"), BASEDIRNAME);
		Con_PromptLoad (&console_prompt, path);
	}
}

void Con_Shutdown (void)
{
	Con_SaveHistory ();
	memset (&console_prompt, 0, sizeof (console_prompt));
	memset (&message_prompt, 0, sizeof (message_prompt));
	memset (console_remote_password, 0, sizeof (console_remote_password));
	con.initialized = false;
}

void Con_ConnectionEstablished (void)
{
	if (!console_interactive && cls.key_dest == key_console)
		cls.key_dest = key_game;
}

static void Con_RemoteMode_f (void)
{
	if (Cmd_Argc () != 3)
	{
		Com_Printf ("Usage: remotemode <address> <password> (Ctrl+D returns to local mode)\n", LOG_CLIENT);
		return;
	}
	if (strlen (Cmd_Argv (2)) >= sizeof (console_remote_password))
	{
		Com_Printf ("Remote console password is too long.\n", LOG_CLIENT);
		return;
	}
	if (!NET_StringToAdr (Cmd_Argv (1), &console_remote_address))
	{
		Com_Printf ("Invalid remote console address.\n", LOG_CLIENT);
		return;
	}
	if (!console_remote_address.port)
		console_remote_address.port = ShortSwap (PORT_SERVER);
	Q_strncpy (console_remote_password, Cmd_Argv (2), sizeof (console_remote_password) - 1);
	console_remote = console_interactive = true;
	M_ForceMenuOff ();
	cls.key_dest = key_console;
}

static void Con_HeightChanged (cvar_t *self, char *old, char *value)
{
	float height = self->value;
	(void)old;
	(void)value;
	if (console_height_sync)
		return;
	console_height_sync = true;
	if (!(height >= .1f))
		height = .1f;
	if (height > 1)
		height = 1;
	Cvar_ForceSet ("con_height", va ("%g", height));
	Cvar_ForceSet ("scr_conheight", va ("%g", height));
	console_height_sync = false;
}

static void Con_Scroll (int amount)
{
	int top = con.current - con.totallines + 1;
	con.display += amount;
	if (con.display < top)
		con.display = top;
	if (con.display > con.current)
		con.display = con.current;
}

static qboolean Con_Contains (const char *line, const char *find)
{
	size_t length = strlen (find);
	for (; *line; line++)
		if (!Q_strncasecmp (line, find, (int)length))
			return true;
	return false;
}

static void Con_Search (int direction)
{
	int row, x, top = con.current - con.totallines + 1;
	char line[CON_MAXWIDTH + 1];
	if (!console_prompt.field.text[0])
		return;
	for (row = con.display + direction; row >= top && row <= con.current; row += direction)
	{
		int skip = con.timestamp_length[row % con.totallines];
		for (x = skip; x < con.linewidth; x++)
			line[x - skip] = con.text[(row % con.totallines) * con.linewidth + x] & 127;
		line[con.linewidth - skip] = 0;
		if (Con_Contains (line, console_prompt.field.text))
		{
			con.display = row;
			break;
		}
	}
}

static qboolean Con_RemoteSetup (const char *text)
{
	qboolean quoted = false, start = true;
	for (; *text; text++)
	{
		if (start)
		{
			if ((unsigned char)*text <= ' ' || *text == '/' || *text == '\\')
				continue;
			if (!Q_strncasecmp (text, "remotemode", 10) && (!text[10] || (unsigned char)text[10] <= ' '))
				return true;
			start = false;
		}
		if (*text == '"')
			quoted = !quoted;
		else if (!quoted && *text == ';')
			start = true;
	}
	return false;
}

static void Con_Action (void)
{
	char text[MAXCMDLINE];
	const char *command = console_prompt.field.text;
	int slash = *command == '/' || *command == '\\';
	console_interactive = true;
	/* Do not persist or echo a remote-console password. */
	if (Con_RemoteSetup (command + slash))
	{
		Q_strncpy (text, command, sizeof (text) - 1);
		Con_FieldReplace (&console_prompt.field, "");
		Con_PromptClearState (&console_prompt);
		Cbuf_AddText (text + slash);
		Cbuf_AddText ("\n");
		return;
	}
	if (!Con_PromptAction (&console_prompt, text))
	{
		Com_Printf ("]\n", LOG_CLIENT);
		return;
	}
	slash = *text == '/' || *text == '\\';
	if (console_remote)
		CL_SendConsoleRcon (&console_remote_address, console_remote_password, text + slash);
	else if (!slash && cls.state == ca_active && (con_auto_chat->intvalue == 1 || con_auto_chat->intvalue == 2))
		CL_SendConsoleChat (con_auto_chat->intvalue == 2 ? "say_team" : "say", text);
	else
	{
		Cbuf_AddText (text + slash);
		Cbuf_AddText ("\n");
	}
	Com_Printf ("]%s\n", LOG_CLIENT, text);
	if (cls.state == ca_disconnected)
		SCR_UpdateScreen ();
}

static void Con_Paste (con_prompt_t *prompt, qboolean commands)
{
	char *clipboard = Sys_GetClipboardData (), *s;
	if (!clipboard)
		return;
	Con_PromptClearState (prompt);
	for (s = clipboard; *s; s++)
	{
		unsigned char c = (unsigned char)*s;
		if (c == '\n' && commands)
		{
			if (s[1])
				Con_Action ();
		}
		else if (c == '\r' || c == '\t' || c == '\n')
			Con_FieldChar (&prompt->field, ' ');
		else
			Con_FieldChar (&prompt->field, c >= 32 && c < 127 ? c : '?');
	}
	free (clipboard);
}

static int Con_Keypad (int key)
{
	switch (key)
	{
	case K_KP_SLASH:
		return '/';
	case K_KP_MINUS:
		return '-';
	case K_KP_PLUS:
		return '+';
	case K_KP_HOME:
		return '7';
	case K_KP_UPARROW:
		return '8';
	case K_KP_PGUP:
		return '9';
	case K_KP_LEFTARROW:
		return '4';
	case K_KP_5:
		return '5';
	case K_KP_RIGHTARROW:
		return '6';
	case K_KP_END:
		return '1';
	case K_KP_DOWNARROW:
		return '2';
	case K_KP_PGDN:
		return '3';
	case K_KP_INS:
		return '0';
	case K_KP_DEL:
		return '.';
	default:
		return key;
	}
}

static qboolean Con_PromptKey (con_prompt_t *prompt, int key, qboolean commands)
{
	qboolean ctrl = keydown[K_CTRL];
	if (ctrl && key >= 'A' && key <= 'Z')
		key += 'a' - 'A';
	if ((ctrl && key == 'v') || ((key == K_INS || key == K_KP_INS) && keydown[K_SHIFT]) || key == K_MOUSE3)
		Con_Paste (prompt, commands);
	else if (ctrl && (key == 'r' || key == 's'))
		Con_PromptHistory (prompt, key == 'r' ? -1 : 1, true);
	else if (key == K_UPARROW || (ctrl && key == 'p'))
		Con_PromptHistory (prompt, -1, false);
	else if (key == K_DOWNARROW || (ctrl && key == 'n'))
		Con_PromptHistory (prompt, 1, false);
	else if (key == K_TAB)
	{
		if (!commands)
			return false;
		if (commands && Cvar_VariableValue ("cl_cmdcomplete") < 3)
		{
			Q_strncpy (key_lines[edit_line] + 1, prompt->field.text, MAXCMDLINE - 2);
			key_linepos = prompt->field.cursor + 1;
			Key_ConsoleCompleteLegacy ();
			Con_FieldReplace (&prompt->field, key_lines[edit_line] + 1);
		}
		else
			Con_PromptComplete (prompt, con.linewidth);
	}
	else
	{
		key = Con_Keypad (key);
		if (!Con_FieldKey (&prompt->field, key))
		{
			if (ctrl || keydown[K_ALT] || key < 32 || key >= 127)
				return false;
			Con_FieldChar (&prompt->field, key);
		}
		Con_PromptClearState (prompt);
	}
	return true;
}

void Con_KeyEvent (int key)
{
	qboolean ctrl = keydown[K_CTRL];
	if (ctrl && key >= 'A' && key <= 'Z')
		key += 'a' - 'A';
	if (ctrl && key == 'l')
	{
		Con_Clear_f ();
		return;
	}
	if (ctrl && key == 'd')
	{
		console_remote = false;
		memset (console_remote_password, 0, sizeof (console_remote_password));
		return;
	}
	if (ctrl && (key == K_UPARROW || key == K_DOWNARROW))
	{
		Con_Search (key == K_UPARROW ? -1 : 1);
		return;
	}
	if (key == K_PGUP || key == K_MWHEELUP)
	{
		Con_Scroll (ctrl ? -6 : -2);
		return;
	}
	if (key == K_PGDN || key == K_MWHEELDOWN)
	{
		Con_Scroll (ctrl ? 6 : 2);
		return;
	}
	if (ctrl && key == K_HOME)
	{
		con.display = con.current - con.totallines + 1;
		return;
	}
	if (ctrl && key == K_END)
	{
		con.display = con.current;
		return;
	}
	if (key == K_ENTER || key == K_KP_ENTER)
		Con_Action ();
	else if (!Con_PromptKey (&console_prompt, key, true))
		return;
	console_interactive = true;
	if (con_scroll->intvalue & 1)
		con.display = con.current;
}

void Con_MessageKeyEvent (int key)
{
	char text[MAXCMDLINE];
	if (key == K_ESCAPE)
	{
		Con_FieldReplace (&message_prompt.field, "");
		cls.key_dest = key_game;
		return;
	}
	if (key == K_ENTER || key == K_KP_ENTER)
	{
		if (Con_PromptAction (&message_prompt, text))
			CL_SendConsoleChat (chat_mode == CHAT_MODE_TEAM ? "say_team" : chat_mode == CHAT_MODE_CUSTOM ? chat_custom_cmd :
																										   "say",
				text);
		cls.key_dest = key_game;
	}
	else if (keydown[K_CTRL] && (key == 'l' || key == 'L'))
	{
		Con_FieldReplace (&message_prompt.field, "");
		Con_PromptClearState (&message_prompt);
	}
	else
		Con_PromptKey (&message_prompt, key, false);
}

extern	char	key_lines[32][MAXCMDLINE];
extern	int		edit_line;
extern	int		key_linepos;
		

void DrawString (int x, int y, const char *s)
{
	//r1: don't draw if obscured
	if (viddef.height * scr_conlines > y)
		return;

	while (*s)
	{
		re.DrawChar (x, y, *s);
		x+=8;
		s++;
	}
}

void DrawAltString (int x, int y, const char *s)
{
	//r1: don't draw if obscured
	if (viddef.height * scr_conlines > y)
		return;

	while (*s)
	{
		re.DrawChar (x, y, *s ^ 0x80);
		x+=8;
		s++;
	}
}


void Key_ClearTyping (void)
{
	Con_FieldReplace (&console_prompt.field, "");
	Con_PromptClearState (&console_prompt);
	key_lines[edit_line][1] = 0;	// clear any typing
	key_linepos = 1;
}

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f (void)
{
	console_interactive = true;
	Key_ClearTyping ();
	Con_ClearNotify ();
	SCR_EndLoadingPlaque ();	// get rid of loading plaque

	//r1: fucking annoying.
	/*if (cl.attractloop)
	{
		cl.attractloop = false;
		Cmd_ExecuteString ("disconnect\n");
		return;
	}*/

	if (cls.state == ca_disconnected)
	{	// start the demo loop again

		//r1: fucking annoying.
		//Cbuf_AddText ("d1\n");
		cls.key_dest = key_console;
		return;
	}

	if (cls.key_dest == key_console)
	{
		M_ForceMenuOff ();
		//if (chat_bufferlen)
		//	cls.key_dest = key_message;
		//Cvar_Set ("paused", "0");
	}
	else
	{
		M_ForceMenuOff ();
		cls.key_dest = key_console;	

		/*if (Cvar_VariableValue ("maxclients") == 1 
			&& Com_ServerState ())
			Cvar_Set ("paused", "1");*/
	}
}

/*
================
Con_ToggleChat_f
================
*/
void Con_ToggleChat_f (void)
{
	console_interactive = true;
	Key_ClearTyping ();

	if (cls.key_dest == key_console)
	{
		if (cls.state == ca_active)
		{
			M_ForceMenuOff ();
			cls.key_dest = key_game;
		}
	}
	else
		cls.key_dest = key_console;
	
	Con_ClearNotify ();
}

/*
================
Con_Clear_f
================
*/
static void Con_Clear_f (void)
{
	memset (con.text, ' ', sizeof(con.text));
	memset (con.timestamp_length, 0, sizeof (con.timestamp_length));
	con.x = 0;
	con.display = con.current;
	Con_ClearNotify ();
}

						
/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
static void Con_Dump_f (void)
{
	int		l, x;
	char	*line;
	FILE	*f;
	char	buffer[1024];
	char	name[MAX_OSPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("usage: condump <filename>\n", LOG_CLIENT);
		return;
	}

	//r1: consolidate condumps to their own dir
	Com_sprintf (name, sizeof(name), "%s/condumps/%s.txt", FS_Gamedir(), Cmd_Argv(1));

	if (strstr (Cmd_Argv(1), "..") || strchr (Cmd_Argv(1), '/') || strchr (Cmd_Argv(1), '\\') )
	{
		Com_Printf ("Illegal filename.\n", LOG_CLIENT);
		return;
	}

	FS_CreatePath (name);
	f = fopen (name, "w");
	if (!f)
	{
		Com_Printf ("ERROR: couldn't open.\n", LOG_CLIENT);
		return;
	}

	//r1: save some basic infos
	if (cls.state == ca_active) {
		time_t timet;
		time(&timet);
		fprintf (f, "%s on server %s, map %s.\n-----------------\n", ctime (&timet), cls.servername, cl.configstrings[CS_MODELS+1] + 5);
	}

	// skip empty lines
	for (l = con.current - con.totallines + 1 ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		for (x=0 ; x<con.linewidth ; x++)
			if (line[x] != ' ')
				break;
		if (x != con.linewidth)
			break;
	}

	// write the remaining lines
	buffer[con.linewidth] = 0;
	for ( ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		strncpy (buffer, line, con.linewidth);
		for (x=con.linewidth-1 ; x>=0 ; x--)
		{
			if (buffer[x] == ' ')
				buffer[x] = 0;
			else
				break;
		}
		for (x=0; buffer[x]; x++)
			buffer[x] &= 0x7f;

		fprintf (f, "%s\n", buffer);
	}

	Com_Printf ("Dumped console text to %s.\n", LOG_CLIENT, name);

	fclose (f);
}

						
/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify (void)
{
	/*int		i;
	
	for (i=0 ; i<NUM_CON_TIMES ; i++)
		con.times[i] = 0;*/
	memset (&con.times, 0, sizeof (con.times));
}

						
/*
================
Con_MessageMode_f
================
*/
static void Con_MessageMode_f (void)
{
	if (cls.state != ca_active || cl.attractloop)
		return;
	chat_mode = CHAT_MODE_PUBLIC;
	Con_FieldReplace (&message_prompt.field, Cmd_Argc () > 1 ? Cmd_Args2 (1) : "");
	Con_PromptClearState (&message_prompt);
	M_ForceMenuOff ();
	cls.key_dest = key_message;
	scr_con_current = scr_conlines = 0;
}

/*
================
Con_MessageMode2_f
================
*/
static void Con_MessageMode2_f (void)
{
	if (cls.state != ca_active || cl.attractloop)
		return;
	chat_mode = CHAT_MODE_TEAM;
	Con_FieldReplace (&message_prompt.field, Cmd_Argc () > 1 ? Cmd_Args2 (1) : "");
	Con_PromptClearState (&message_prompt);
	M_ForceMenuOff ();
	cls.key_dest = key_message;
	scr_con_current = scr_conlines = 0;
}

void Con_OpenChatPrompt (const char *command, const char *text)
{
	if (cls.state != ca_active || cl.attractloop)
		return;
	Q_strncpy (chat_custom_cmd, command, sizeof (chat_custom_cmd) - 1);
	Com_sprintf (chat_custom_prompt, sizeof (chat_custom_prompt), "%s: ", chat_custom_cmd);
	chat_mode = CHAT_MODE_CUSTOM;
	Con_FieldReplace (&message_prompt.field, text);
	Con_PromptClearState (&message_prompt);
	M_ForceMenuOff ();
	cls.key_dest = key_message;
	scr_con_current = scr_conlines = 0;
}

static void Con_MessageModex_f (void)
{
	if (cls.state != ca_active || cl.attractloop)
		return;
	if (Cmd_Argc () < 2)
	{
		Com_Printf ("Usage: messagemodex prompt [default]\n", LOG_CLIENT);
		return;
	}
	Con_OpenChatPrompt (Cmd_Argv (1), Cmd_Argc () > 2 ? Cmd_Args2 (2) : "");
}

static void Con_Resize (int width)
{
	char	tbuf[CON_TEXTSIZE];
	unsigned char stamps[CON_MAXLINES];
	int		i, j, oldwidth, oldtotallines, numlines, numchars;

	oldwidth = con.linewidth;
	con.linewidth = width;
	oldtotallines = con.totallines;
	con.totallines = CON_TEXTSIZE / con.linewidth;
	numlines = oldtotallines;

	if (con.totallines < numlines)
		numlines = con.totallines;

	numchars = oldwidth;

	if (con.linewidth < numchars)
		numchars = con.linewidth;

	memcpy (tbuf, con.text, CON_TEXTSIZE);
	memcpy (stamps, con.timestamp_length, sizeof (stamps));
	memset (con.timestamp_length, 0, sizeof (con.timestamp_length));
	memset (con.text, ' ', CON_TEXTSIZE);

	for (i=0 ; i<numlines ; i++)
	{
		con.timestamp_length[con.totallines - 1 - i] = stamps[(con.current - i + oldtotallines) % oldtotallines];
		if (con.timestamp_length[con.totallines - 1 - i] > numchars)
			con.timestamp_length[con.totallines - 1 - i] = (unsigned char)numchars;
		for (j=0 ; j<numchars ; j++)
		{
			con.text[(con.totallines - 1 - i) * con.linewidth + j] =
					tbuf[((con.current - i + oldtotallines) %
							oldtotallines) * oldwidth + j];
		}
	}
}
/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int		width;

	width = (SCR_ConsoleWidth () >> 3) - 2;
	if (width > CON_MAXWIDTH)
		width = CON_MAXWIDTH;
	if (width > 0 && width < CON_MINWIDTH)
		width = CON_MINWIDTH;

	if (width == con.linewidth)
		return;

	if (width < 1)			// video hasn't been initialized yet
	{
		width = 38;
		con.linewidth = width;
		con.totallines = sizeof(con.text) / con.linewidth;
		memset (con.text, ' ', sizeof(con.text));
	}
	else
	{
		Con_Resize (width);
		Con_ClearNotify ();
	}

	con.current = con.totallines - 1;
	con.display = con.current;
	con.x = 0;
}


/*
================
Con_Init
================
*/
void Con_Init (void)
{
	con.linewidth = -1;

	Con_CheckResize ();
	
	Com_Printf ("Console initialized.\n", LOG_CLIENT);

	cls.key_dest = key_console;

//
// register our commands
//
	con_notifytime = Cvar_Get ("con_notifytime", "3", 0);
	con_notifylines = Cvar_Get ("con_notifylines", "4", 0);
	con_clock = Cvar_Get ("con_clock", "0", 0);
	con_alpha = Cvar_Get ("con_alpha", "1", 0);
	con_background = Cvar_Get ("con_background", "conback", 0);
	con_scroll = Cvar_Get ("con_scroll", "0", 0);
	con_history = Cvar_Get ("con_history", "128", 0);
	con_timestamps = Cvar_Get ("con_timestamps", "0", 0);
	con_timestampsformat = Cvar_Get ("con_timestampsformat", "%H:%M:%S ", 0);
	con_timestampscolor = Cvar_Get ("con_timestampscolor", "#aaa", 0);
	con_auto_chat = Cvar_Get ("con_auto_chat", "0", 0);
	con_height = Cvar_Get ("con_height", Cvar_Get ("scr_conheight", "0.5", CVAR_ARCHIVE)->string, 0);
	con_height->changed = Con_HeightChanged;
	Con_HeightChanged (con_height, NULL, NULL);
	Cvar_Get ("com_completion_mode", "1", 0);
	Cvar_Get ("com_completion_treshold", "50", 0);

	Cmd_AddCommand ("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand ("togglechat", Con_ToggleChat_f);
	Cmd_AddCommand ("messagemode", Con_MessageMode_f);
	Cmd_AddCommand ("messagemode2", Con_MessageMode2_f);
	Cmd_AddCommand ("messagemodex", Con_MessageModex_f);
	Cmd_AddCommand ("clear", Con_Clear_f);
	Cmd_AddCommand ("condump", Con_Dump_f);
	Cmd_AddCommand ("remotemode", Con_RemoteMode_f);
	con.initialized = true;
	Con_LoadHistory ();
}


/*
===============
Con_Linefeed
===============
*/
static void Con_Linefeed (void)
{
	con.x = 0;
	if (con.display == con.current)
		con.display++;
	con.current++;
	memset (&con.text[(con.current%con.totallines)*con.linewidth]
	, ' ', con.linewidth);
	con.timestamp_length[con.current % con.totallines] = 0;
	if (con_timestamps && con_timestamps->intvalue)
	{
		char stamp[128];
		time_t now = time (NULL);
		struct tm *local = localtime (&now);
		size_t length = local ? strftime (stamp, sizeof (stamp), con_timestampsformat->string, local) : 0;
		if (length > (size_t)(con.linewidth / 2))
			length = con.linewidth / 2;
		memcpy (con.text + (con.current % con.totallines) * con.linewidth, stamp, length);
		con.timestamp_length[con.current % con.totallines] = (unsigned char)length;
		con.x = (int)length;
	}
	if (con_scroll && (con_scroll->intvalue & 2))
		con.display = con.current;
}

/*
================
Con_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void Con_Print (const char *txt)
{
	int		y;
	int		c, l;
	static int	cr;
	int		mask;

	if (!con.initialized)
		return;

	if (txt[0] == 1 || txt[0] == 2)
	{
		mask = 128;		// go to colored text
		txt++;
	}
	else
		mask = 0;


	while ( (c = *txt) )
	{
	// count word length
		for (l=0 ; l< con.linewidth ; l++)
			if ( txt[l] <= ' ')
				break;

	// word wrap
		if (l != con.linewidth && (con.x + l > con.linewidth) )
			con.x = 0;

		txt++;

		if (cr)
		{
			con.current--;
			cr = false;
		}

		
		if (!con.x)
		{
			Con_Linefeed ();
		// mark time for transparent overlay
			if (con.current >= 0)
				con.times[con.current % NUM_CON_TIMES] = cls.realtime;
		}

		switch (c)
		{
		case '\n':
			con.x = 0;
			break;

		case '\r':
			con.x = 0;
			cr = 1;
			break;

		default:	// display character and advance
			y = con.current % con.totallines;
			con.text[y*con.linewidth+con.x] = c | mask | con.ormask;
			con.x++;
			if (con.x >= con.linewidth)
				con.x = 0;
			break;
		}
		
	}
}


/*
==============
Con_CenteredPrint
==============
*/
/*void Con_CenteredPrint (char *text)
{
	int		l;
	char	buffer[1024];

	l = strlen(text);
	l = (con.linewidth-l)/2;
	if (l < 0)
		l = 0;
	memset (buffer, ' ', l);
	strcpy (buffer+l, text);
	strcat (buffer, "\n");
	Con_Print (buffer);
}*/

/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

The input line scrolls horizontally if typing goes beyond the right edge
================
*/
static void Con_DrawField (const con_field_t *field, int x, int y, int width)
{
	int i, offset = 0, cursor = field->cursor;
	if (width < 1)
		return;
	if (cursor >= width)
	{
		offset = cursor - width + 1;
		cursor = width - 1;
	}
	for (i = 0; i < width && field->text[offset + i]; i++)
		re.DrawChar (x + i * 8, y, (unsigned char)field->text[offset + i]);
	if (cls.realtime & 256)
		re.DrawChar (x + cursor * 8, y, Key_GetOverstrikeMode () ? 11 : '_');
}

static void Con_DrawLine (int row, int y, float alpha, qboolean notify, qboolean rich)
{
	int x, skip = notify ? con.timestamp_length[row % con.totallines] : 0;
	unsigned int color = 0xffaaaaaa;
	const char *line = con.text + (row % con.totallines) * con.linewidth;
	if (rich)
	{
		CL_JumpParseColor (con_timestampscolor->string, &color);
		R_ClearColor ();
		R_SetAlpha (alpha);
	}
	for (x = skip; x < con.linewidth; x++)
	{
		if (rich && !notify && x == 0)
		{
			R_SetColor (color);
			R_SetAlpha (alpha);
		}
		if (rich && x == con.timestamp_length[row % con.totallines])
		{
			R_ClearColor ();
			R_SetAlpha (alpha);
		}
		re.DrawChar ((x - skip + 1) * 8, y, (unsigned char)line[x]);
	}
	if (rich)
		R_ClearColor ();
}

static void Con_DrawInput (void)
{
	if (cls.key_dest == key_menu || (cls.key_dest != key_console && cls.state == ca_active))
		return;
	re.DrawChar (8, con.vislines - 22, console_remote ? '>' : ']');
	Con_DrawField (&console_prompt.field, 16, con.vislines - 22, con.linewidth - 1);
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify (void)
{
	int i, v = 0, lines = con_notifylines->intvalue;
	qboolean rich;
	present_scope_t space;
	float duration = con_notifytime->value * 1000;
	if (lines < 0)
		lines = 0;
	if (lines > NUM_CON_TIMES)
		lines = NUM_CON_TIMES;
	Con_CheckResize ();
	SCR_BeginPresentation (&space, PRESENT_CONSOLE);
	rich = CL_ConsoleLayoutNativeBegin (HL_NOTIFY);
	for (i = con.current - lines + 1; i <= con.current; i++)
	{
		unsigned time;
		float remaining, alpha = 1;
		if (i < 0 || !(time = con.times[i % NUM_CON_TIMES]))
			continue;
		remaining = duration - (unsigned)(cls.realtime - time);
		if (!(remaining > 0))
			continue;
		if (!v && i == con.current && remaining < 300)
			alpha = remaining / 300;
		Con_DrawLine (i, v, alpha, true, rich);
		v += 8;
	}
	if (rich)
		CL_JumpLayoutNativeEnd ();
	if (cls.key_dest == key_message)
	{
		const char *label = chat_mode == CHAT_MODE_TEAM ? "say_team: " : chat_mode == CHAT_MODE_CUSTOM ? chat_custom_prompt :
																										 "say: ";
		int x, skip = (int)strlen (label) + 1;
		rich = CL_ConsoleLayoutNativeBegin (HL_MESSAGE);
		for (x = 0; label[x]; x++)
			re.DrawChar ((x + 1) * 8, v, label[x]);
		Con_DrawField (&message_prompt.field, skip * 8, v, (viddef.width / 8) - skip - 1);
		if (rich)
			CL_JumpLayoutNativeEnd ();
		v += 8;
	}
	if (v)
	{
		SCR_AddDirtyPoint (0, 0);
		SCR_AddDirtyPoint (viddef.width - 1, v);
	}
	SCR_EndPresentation (&space);
}

/*
================
Con_DrawConsole

Draws the console with the solid background
================
*/
void Con_DrawConsole (float frac)
{
	int				i, j, x, y, n, len, offset;
	int				rows;
	char			*text;
	int				row;
	int				lines;
	char			version[96];
	char			dlbar[1024];

	time_t			t;
	struct tm		*today;
	present_scope_t space;
	qboolean rich;

	Con_CheckResize ();
	SCR_BeginPresentation (&space, PRESENT_CONSOLE);
	lines = (int)(viddef.height * frac);

	if (lines <= 0)
	{
		SCR_EndPresentation (&space);
		return;
	}

	if (lines > viddef.height)
		lines = viddef.height;

// draw the background
	if (scr_presentation)
	{
		float alpha = cls.state < ca_active || cls.key_dest == key_menu ? 1 : con_alpha->value;
		float rgba[4] = { 1, 1, 1, 1 };
		if (!(alpha >= 0))
			alpha = 0;
		if (alpha > 1)
			alpha = 1;
		rgba[3] = alpha;
		if (alpha > 0)
			scr_presentation->DrawColorPic (0, lines - viddef.height, viddef.width, viddef.height, con_background->string, rgba);
	}
	else
		re.DrawStretchPic (0, lines - viddef.height, viddef.width, viddef.height, con_background->string);
	rich = CL_ConsoleLayoutNativeBegin (-1);
	SCR_AddDirtyPoint (0,0);
	SCR_AddDirtyPoint (viddef.width-1,lines-1);

	len = (int)strlen (console_prompt.field.text);

	i = Com_sprintf (version, sizeof(version), "r1q2 race " RACE_VERSION RACE_MODIFIED);

	if (len >= (viddef.width * 0.125f) - (i+2))
		offset = 20;
	else
		offset = 0;

	for (x=i-1; x>=0 ; x--)
		re.DrawChar (viddef.width-2-(i*8)+x*8, lines-12-offset, 128 + version[x] );

	t = time (NULL);
	today = localtime(&t);

	i = con_clock->intvalue && today ? (int)strftime (version, sizeof (version), "%H:%M:%S", today) : 0;
	for (x=0 ; x<i ; x++)
		re.DrawChar (viddef.width-66+x*8, lines-22-offset, 128 + version[x] );

// draw the text
	con.vislines = lines;
	
#if 0
	rows = (lines-8)>>3;		// rows of text to draw

	y = lines - 24;
#else
	rows = (lines-22)>>3;		// rows of text to draw

	y = lines - 30;
#endif

// draw from the bottom up
	if (con.display != con.current)
	{
	// draw arrows to show the buffer is backscrolled
		for (x=0 ; x<con.linewidth ; x+=4)
			re.DrawChar ( (x+1)<<3, y, '^');
	
		y -= 8;
		rows--;
	}
	
	row = con.display;
	for (i=0 ; i<rows ; i++, y-=8, row--)
	{
		if (row < 0)
			break;
		if (con.current - row >= con.totallines)
			break;		// past scrollback wrap point
			
		text = con.text + (row % con.totallines)*con.linewidth;

		Con_DrawLine (row, y, 1, false, rich);
	}

//ZOID
	// draw the download bar
	// figure out width
	if (cls.downloadname[0] && (cls.download || cls.downloadposition))
	{
		if ((text = strrchr(cls.downloadname, '/')) != NULL)
			text++;
		else
			text = cls.downloadname;

		x = con.linewidth - ((con.linewidth * 7) / 40);
		y = x - (int)strlen(text) - 20;
		i = con.linewidth/3;
		if (strlen(text) > i) {
			y = x - i - 11;
			Q_strncpy(dlbar, text, i);
			strcat(dlbar, "...");
		} else
			strcpy(dlbar, text);
		strcat(dlbar, ": ");
		i = (int)strlen(dlbar);
		dlbar[i++] = '\x80';
		// where's the dot go?
		if (cls.downloadpercent == 0)
			n = 0;
		else
			n = y * cls.downloadpercent / 100;
			
		for (j = 0; j < y; j++)
			if (j == n)
				dlbar[i++] = '\x83';
			else
				dlbar[i++] = '\x81';
		dlbar[i++] = '\x82';

		if (cls.download)
			cls.downloadposition = ftell (cls.download);

		sprintf (dlbar + i, " %02d%% (%.02f KB)", cls.downloadpercent, (float)cls.downloadposition / 1024.0);

		j = (int)strlen(dlbar);

		// draw it
		y = con.vislines-12;
		for (i = 0; i < j; i++)
			re.DrawChar ( (i+1)<<3, y, dlbar[i]);
	}
//ZOID

// draw the input prompt, user text, and cursor if desired
	Con_DrawInput ();
	if (rich)
		CL_JumpLayoutNativeEnd ();
	SCR_EndPresentation (&space);
}


