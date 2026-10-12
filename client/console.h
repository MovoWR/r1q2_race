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

//
// console
//

#define NUM_CON_TIMES 16
#define CON_MINWIDTH 10
#define CON_MAXWIDTH 1023
#define CON_MAXLINES (CON_TEXTSIZE / CON_MINWIDTH)

#define		CON_TEXTSIZE	65536
typedef struct
{
	qboolean	initialized;

	char		text[CON_TEXTSIZE];
	int			current;		// line where next message will be printed
	int			x;				// offset in current line for next print
	int			display;		// bottom of console displays this line

	int			ormask;			// high bit mask for colored characters

	int 		linewidth;		// characters across screen
	int			totallines;		// total lines in console scrollback

	int			vislines;

	unsigned	times[NUM_CON_TIMES];	// cls.realtime time the line was generated
	unsigned char timestamp_length[CON_MAXLINES];
	// for transparent notify lines
} console_t;

extern	console_t	con;

void Con_DrawCharacter (int cx, int line, int num);

void Con_CheckResize (void);
void Con_Init (void);
void Con_DrawConsole (float frac);
void Con_Print (const char *txt);
//void Con_CenteredPrint (const char *text);
void Con_DrawNotify (void);
void Con_ClearNotify (void);
void Con_ToggleConsole_f (void);
void Con_KeyEvent (int key);
void Con_MessageKeyEvent (int key);
void Con_OpenChatPrompt (const char *command, const char *text);
void Con_SaveHistory (void);
void Con_LoadHistory (void);
void Con_Shutdown (void);
void Con_ConnectionEstablished (void);
void CL_SendConsoleRcon (const netadr_t *address, const char *password, const char *command);
void CL_SendConsoleChat (const char *command, const char *text);
