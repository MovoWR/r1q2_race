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
/* Adapted from frozen Q2PRO Race 5571a17dbb7fb03915e7b403b1f981b7006a1a05. */
#include "jump_menu.h"


extern cvar_t *cl_drawStrafeHelper;

static bool Field_ParseColor (const char *s, color_t *color);
static const char *Menu_StatusText (menuFrameWork_t *menu, char *buffer, size_t size);
static int Menu_StatusHeight (menuFrameWork_t *menu);
static void Keybind_Remove (const char *cmd);
static void Keybind_Update (menuFrameWork_t *menu);
static void BindFields_Update (menuFrameWork_t *menu);
static void BindFields_Remove (const char *cmd);
static void BindSelect2_Remove (menuBindSelect2_t *bs);
static void BindSelect2_Update (menuFrameWork_t *menu);
static void BindSelect2_Push (menuBindSelect2_t *bs);
static void BindSelect2_Init (menuBindSelect2_t *bs);
static void BindFields_Push (menuBindFields_t *bf);
static void Keybind_Push (menuKeybind_t *k);
static void Keybind_Init (menuKeybind_t *k);

void Menu_SetColor (uint32 color)
{
	R_SetColor (color);
	R_SetAltColor (uis.color.alternate.u32);
}

static void Menu_SetNormalColor (void)
{
	Menu_SetColor (uis.color.normal.u32);
}

static bool Menu_IsNetMeterMenu (const menuFrameWork_t *menu)
{
	return menu && menu->name &&
		(!strcmp (menu->name, "jumpnetwork") ||
			!strcmp (menu->name, "jumpnetalerts") ||
			!strcmp (menu->name, "jumpnetthresholds"));
}

static bool Menu_IsStrafePresetMenu (const menuFrameWork_t *menu)
{
	return menu && menu->name && strcmp (menu->name, "shpresets") == 0;
}

/*
===================================================================

ACTION CONTROL

===================================================================
*/

static void Action_Free (menuAction_t *a)
{
	Z_Free (a->generic.name);
	Z_Free (a->generic.status);
	Z_Free (a->cmd);
	Z_Free (a);
}

#define SH_PRESET_SWATCH_COUNT  3
#define SH_PRESET_SWATCH_WIDTH  (CHAR_WIDTH * 2)
#define SH_PRESET_SWATCH_HEIGHT (CHAR_HEIGHT - 2)
#define SH_PRESET_SWATCH_GAP    2
#define SH_PRESET_SWATCH_MARGIN CHAR_WIDTH

static int Action_PresetSwatchesOnlyWidth (void)
{
	return SH_PRESET_SWATCH_COUNT * SH_PRESET_SWATCH_WIDTH +
		(SH_PRESET_SWATCH_COUNT - 1) * SH_PRESET_SWATCH_GAP;
}

static bool Action_PresetColorStrings (menuAction_t *a, const char **accelerating,
	const char **optimal,
	const char **centermarker)
{
	static const char prefix[] = "sh hud preset ";
	const char *name;

	if (!a->cmd || Q_strncasecmp (a->cmd, prefix, sizeof (prefix) - 1) != 0)
	{
		return false;
	}

	name = a->cmd + sizeof (prefix) - 1;
	if (!*name || !Q_stricmp (name, "random"))
	{
		return false;
	}

	return SH_GetPresetColors (name, accelerating, optimal, centermarker);
}

static int Action_PresetSwatchesWidth (menuAction_t *a)
{
	if (!Action_PresetColorStrings (a, NULL, NULL, NULL))
	{
		return 0;
	}

	return SH_PRESET_SWATCH_MARGIN +
		SH_PRESET_SWATCH_COUNT * SH_PRESET_SWATCH_WIDTH +
		(SH_PRESET_SWATCH_COUNT - 1) * SH_PRESET_SWATCH_GAP;
}

static void Action_DrawPresetSwatches (menuAction_t *a)
{
	const char *colors[SH_PRESET_SWATCH_COUNT];
	int x, y, i;

	if (!Action_PresetColorStrings (a, &colors[0], &colors[1], &colors[2]))
	{
		return;
	}

	if (Menu_IsStrafePresetMenu (a->generic.parent))
	{
		x = a->generic.parent->maxs[0] - Action_PresetSwatchesOnlyWidth () -
			SH_PRESET_SWATCH_MARGIN;
	}
	else if ((a->generic.uiFlags & UI_CENTER) == UI_CENTER)
	{
		x = a->generic.x + (int)strlen (a->generic.name) * CHAR_WIDTH / 2 +
			SH_PRESET_SWATCH_MARGIN;
	}
	else
	{
		x = a->generic.x + RCOLUMN_OFFSET + CHAR_WIDTH * 12;
	}
	y = a->generic.y + 1;

	for (i = 0; i < SH_PRESET_SWATCH_COUNT; i++)
	{
		color_t color;

		R_DrawFill8 (x - 1, y - 1, SH_PRESET_SWATCH_WIDTH + 2,
			SH_PRESET_SWATCH_HEIGHT + 2, 7);
		if (Field_ParseColor (colors[i], &color))
		{
			R_DrawFill32 (x, y, SH_PRESET_SWATCH_WIDTH,
				SH_PRESET_SWATCH_HEIGHT, color.u32);
		}
		else
		{
			R_DrawFill32 (x, y, SH_PRESET_SWATCH_WIDTH,
				SH_PRESET_SWATCH_HEIGHT, MakeColor (96, 0, 0, 220));
		}
		x += SH_PRESET_SWATCH_WIDTH + SH_PRESET_SWATCH_GAP;
	}
}

/*
=================
Action_Init
=================
*/
static void Action_Init (menuAction_t *a)
{
	Q_assert (a->generic.name);

	if ((a->generic.uiFlags & UI_CENTER) != UI_CENTER)
	{
		a->generic.x += RCOLUMN_OFFSET;
	}

	a->generic.rect.x = a->generic.x;
	a->generic.rect.y = a->generic.y;
	UI_StringDimensions (&a->generic.rect, a->generic.uiFlags, a->generic.name);
	a->generic.rect.width += Action_PresetSwatchesWidth (a);
}

/*
=================
Action_Draw
=================
*/
static void Action_Draw (menuAction_t *a)
{
	int flags;

	flags = a->generic.uiFlags;

	if (a->generic.flags & QMF_HASFOCUS)
	{
		Menu_SetColor (uis.color.active.u32);
		if ((a->generic.uiFlags & UI_CENTER) == UI_CENTER)
		{
			flags |= UI_ALTCOLOR;
		}
	}

	if (a->generic.flags & QMF_GRAYED)
	{
		Menu_SetColor (uis.color.disabled.u32);
	}
	else if (!(a->generic.flags & QMF_HASFOCUS))
	{
		Menu_SetColor (uis.color.selectable.u32);
	}
	UI_DrawString (a->generic.x, a->generic.y, flags, a->generic.name);
	Action_DrawPresetSwatches (a);
	Menu_SetNormalColor ();
}

/*
===================================================================

STATIC CONTROL

===================================================================
*/

/*
=================
Static_Free
=================
*/
static void Static_Free (menuStatic_t *s)
{
	Z_Free (s->generic.name);
	Z_Free (s->generic.status);
	Z_Free (s);
}

/*
=================
Static_Init
=================
*/
static void Static_Init (menuStatic_t *s)
{
	Q_assert (s->generic.name);

	if (!s->maxChars)
	{
		s->maxChars = MAX_STRING_CHARS;
	}

	s->generic.rect.x = s->generic.x;
	s->generic.rect.y = s->generic.y;

	UI_StringDimensions (&s->generic.rect,
		s->generic.uiFlags, s->generic.name);
}

/*
=================
Static_Draw
=================
*/
static void Static_Draw (menuStatic_t *s)
{
	if (s->generic.flags & QMF_CUSTOM_COLOR)
	{
		Menu_SetColor (s->generic.color.u32);
	}
	UI_DrawString (s->generic.x, s->generic.y, s->generic.uiFlags, s->generic.name);
	if (s->generic.flags & QMF_CUSTOM_COLOR)
	{
		Menu_SetNormalColor ();
	}
}

/*
===================================================================

BITMAP CONTROL

===================================================================
*/

static void Bitmap_Free (menuBitmap_t *b)
{
	Z_Free (b->generic.status);
	Z_Free (b->cmd);
	Z_Free (b);
}

static void Bitmap_Init (menuBitmap_t *b)
{
	b->generic.rect.x = b->generic.x;
	b->generic.rect.y = b->generic.y;
	b->generic.rect.width = b->generic.width;
	b->generic.rect.height = b->generic.height;
}

static void Bitmap_Draw (menuBitmap_t *b)
{
	if (b->generic.flags & QMF_HASFOCUS)
	{
		unsigned frame = (uis.realtime / 100) % NUM_CURSOR_FRAMES;
		R_DrawPic (b->generic.x - CURSOR_OFFSET, b->generic.y, uis.bitmapCursors[frame]);
		R_DrawPic (b->generic.x, b->generic.y, b->pics[1]);
	}
	else
	{
		R_DrawPic (b->generic.x, b->generic.y, b->pics[0]);
	}
}

/*
===================================================================

KEYBIND CONTROL

===================================================================
*/

static void Keybind_Free (menuKeybind_t *k)
{
	Z_Free (k->generic.name);
	Z_Free (k->generic.status);
	Z_Free (k->cmd);
	Z_Free (k->altstatus);
	Z_Free (k);
}

/*
=================
Keybind_Init
=================
*/
static void Keybind_Init (menuKeybind_t *k)
{
	size_t len;

	Q_assert (k->generic.name);

	k->generic.uiFlags &= ~(UI_CENTER | UI_LEFT | UI_RIGHT);

	k->generic.rect.x = k->generic.x + LCOLUMN_OFFSET;
	k->generic.rect.y = k->generic.y;

	UI_StringDimensions (&k->generic.rect,
		k->generic.uiFlags | UI_RIGHT, k->generic.name);

	if (k->altbinding[0])
	{
		len = strlen (k->binding) + 4 + strlen (k->altbinding);
	}
	else if (k->binding[0])
	{
		len = strlen (k->binding);
	}
	else
	{
		len = 3;
	}

	k->generic.rect.width += (RCOLUMN_OFFSET - LCOLUMN_OFFSET) + len * CHAR_WIDTH;
}

/*
=================
Keybind_Draw
=================
*/
static void Keybind_Draw (menuKeybind_t *k)
{
	char string[MAX_STRING_CHARS];
	int flags;

	flags = UI_ALTCOLOR;
	if (k->generic.flags & QMF_HASFOCUS)
	{
		Menu_SetColor (uis.color.active.u32);
	}
	else
	{
		if (k->generic.parent->keywait)
		{
			Menu_SetColor (uis.color.disabled.u32);
		}
	}

	UI_DrawString (k->generic.x + LCOLUMN_OFFSET, k->generic.y,
		k->generic.uiFlags | UI_RIGHT | flags, k->generic.name);

	if (k->altbinding[0])
	{
		Q_concat (string, sizeof (string), k->binding, " or ", k->altbinding);
	}
	else if (k->binding[0])
	{
		Q_strlcpy (string, k->binding, sizeof (string));
	}
	else
	{
		strcpy (string, "???");
	}

	UI_DrawString (k->generic.x + RCOLUMN_OFFSET, k->generic.y,
		k->generic.uiFlags | UI_LEFT, string);

	if ((k->generic.flags & QMF_HASFOCUS) || k->generic.parent->keywait)
	{
		Menu_SetNormalColor ();
	}
}

static void Keybind_Push (menuKeybind_t *k)
{
	int key = Key_EnumBindings (0, k->cmd);

	k->altbinding[0] = 0;
	if (key == -1)
	{
		strcpy (k->binding, "???");
	}
	else
	{
		Q_strlcpy (k->binding, Key_KeynumToLabel (key), sizeof (k->binding));
		key = Key_EnumBindings (key + 1, k->cmd);
		if (key != -1)
		{
			Q_strlcpy (k->altbinding, Key_KeynumToLabel (key), sizeof (k->altbinding));
		}
	}
}

static void Keybind_Pop (menuKeybind_t *k)
{
	Key_WaitKey (NULL, NULL);
}

static void Keybind_Update (menuFrameWork_t *menu)
{
	menuKeybind_t *k;
	int i;

	for (i = 0; i < menu->nitems; i++)
	{
		k = menu->items[i];
		if (k->generic.type == MTYPE_KEYBIND)
		{
			Keybind_Push (k);
			Keybind_Init (k);
		}
	}
}

static void Keybind_Remove (const char *cmd)
{
	int key;

	for (key = 0;; key++)
	{
		key = Key_EnumBindings (key, cmd);
		if (key == -1)
		{
			break;
		}
		Key_SetBinding (key, "");
	}
}

static bool keybind_cb (void *arg, int key)
{
	menuCommon_t *item = arg;
	menuFrameWork_t *menu = item->parent;

	if (key == '`')
	{
		UI_StartSound (QMS_BEEP);
		return false;
	}

	if (key != K_ESCAPE)
	{
		if (item->type == MTYPE_KEYBIND)
		{
			menuKeybind_t *k = (menuKeybind_t *)item;
			if (k->altbinding[0])
				BindFields_Remove (k->cmd);
			Key_SetBinding (key, k->cmd);
			Keybind_Update (menu);
		}
		else if (item->type == MTYPE_BINDFIELDS)
		{
			menuBindFields_t *bf = (menuBindFields_t *)item;
			if (bf->altbinding[0])
				BindFields_Remove (bf->cmd);
			Key_SetBinding (key, bf->cmd);
			BindFields_Update (menu);
		}
		else if (item->type == MTYPE_BINDSELECT2)
		{
			menuBindSelect2_t *bs = (menuBindSelect2_t *)item;
			if (bs->altbinding[0])
				BindSelect2_Remove (bs);
			Key_SetBinding (key, bs->cmd);
			BindSelect2_Update (menu);
		}
	}

	menu->keywait = false;
	menu->status = item->status;
	Key_WaitKey (NULL, NULL);
	UI_StartSound (QMS_OUT);
	return false;
}

static void BindFields_Update (menuFrameWork_t *menu)
{
	int i;
	for (i = 0; i < menu->nitems; i++)
	{
		if (((menuCommon_t *)menu->items[i])->type == MTYPE_BINDFIELDS)
		{
			BindFields_Push (menu->items[i]);
		}
	}
}

static void BindFields_Push (menuBindFields_t *bf)
{
	int key = Key_EnumBindings (0, bf->cmd);
	int i;
	bf->altbinding[0] = 0;
	if (key == -1)
		strcpy (bf->binding, "???");
	else
	{
		Q_strlcpy (bf->binding, Key_KeynumToLabel (key), sizeof (bf->binding));
		key = Key_EnumBindings (key + 1, bf->cmd);
		if (key != -1)
			Q_strlcpy (bf->altbinding, Key_KeynumToLabel (key), sizeof (bf->altbinding));
	}
	for (i = 0; i < bf->numFields; i++)
	{
		IF_Init (&bf->fields[i], bf->fieldWidths[i], bf->fieldWidths[i]);
		if (bf->cvars[i])
			IF_Replace (&bf->fields[i], bf->cvars[i]->string);
	}
	bf->focusPart = 0;
}

static menuSound_t Keybind_DoEnter (menuKeybind_t *k)
{
	menuFrameWork_t *menu = k->generic.parent;

	menu->keywait = true;
	menu->status = k->altstatus;
	Key_WaitKey (keybind_cb, k);
	return QMS_IN;
}

static menuSound_t Keybind_Key (menuKeybind_t *k, int key)
{
	menuFrameWork_t *menu = k->generic.parent;

	if (menu->keywait)
	{
		return QMS_OUT; // never gets there
	}

	if (key == K_BACKSPACE || key == K_DEL)
	{
		Keybind_Remove (k->cmd);
		Keybind_Update (menu);
		return QMS_IN;
	}

	return QMS_NOTHANDLED;
}

/*
===================================================================

SELECT FIELD

===================================================================
*/

static menuSelect_t *g_openSelect; /* only one dropdown open at a time */

static menuSound_t Select_MouseMove (menuSelect_t *s);
static menuSound_t Select2_MouseMove (menuSelect2_t *s);
static menuSound_t BindSelect2_MouseMove (menuBindSelect2_t *s);

static bool SelectAnyIsOpen (void)
{
	if (!g_openSelect)
		return false;
	if (g_openSelect->generic.type == MTYPE_SELECT)
		return g_openSelect->open;
	if (g_openSelect->generic.type == MTYPE_SELECT2)
		return ((menuSelect2_t *)g_openSelect)->open;
	if (g_openSelect->generic.type == MTYPE_BINDSELECT2)
		return ((menuBindSelect2_t *)g_openSelect)->open;
	return false;
}

static void SelectAnyMouseMove (void)
{
	if (!g_openSelect)
		return;
	if (g_openSelect->generic.type == MTYPE_SELECT && g_openSelect->open)
		Select_MouseMove (g_openSelect);
	else if (g_openSelect->generic.type == MTYPE_SELECT2)
		Select2_MouseMove ((menuSelect2_t *)g_openSelect);
	else if (g_openSelect->generic.type == MTYPE_BINDSELECT2)
		BindSelect2_MouseMove ((menuBindSelect2_t *)g_openSelect);
}

static void Select_Push (menuSelect_t *s)
{
	int i, val = s->cvar->intvalue;
	bool numeric = COM_IsFloat (s->cvar->string);

	s->open = false;
	s->hoverIndex = -1;
	s->curvalue = -1;

	for (i = 0; i < s->numItems; i++)
	{
		if (!strcmp (s->itemnames[i], s->cvar->string) ||
			(numeric && COM_IsFloat (s->itemnames[i]) && Q_atoi (s->itemnames[i]) == val))
		{
			s->curvalue = i;
			break;
		}
	}
	if (s->curvalue < 0 && s->numItems > 0)
		s->curvalue = 0;
}

static void Select_Pop (menuSelect_t *s)
{
	if (s->open)
	{
		if (g_openSelect == s)
			g_openSelect = NULL;
		s->open = false;
	}
	if (s->curvalue >= 0 && s->curvalue < s->numItems)
	{
		Cvar_SetByVar (s->cvar, s->itemnames[s->curvalue], FROM_MENU);
	}
}

static void Select_Init (menuSelect_t *s)
{
	int i, maxWidth = 0, len;

	s->generic.uiFlags &= ~(UI_CENTER | UI_LEFT | UI_RIGHT);

	s->generic.rect.x = s->generic.x + LCOLUMN_OFFSET;
	s->generic.rect.y = s->generic.y;

	UI_StringDimensions (&s->generic.rect,
		s->generic.uiFlags | UI_RIGHT, s->generic.name);

	for (i = 0; i < s->numItems; i++)
	{
		len = strlen (s->itemnames[i]);
		if (maxWidth < len)
			maxWidth = len;
	}
	if (maxWidth < 4)
		maxWidth = 4;

	s->dropWidth = maxWidth * CHAR_WIDTH + CHAR_WIDTH * 2;
	s->generic.rect.width += (RCOLUMN_OFFSET - LCOLUMN_OFFSET) +
		maxWidth * CHAR_WIDTH + CHAR_WIDTH * 2;
}

static void Select_CloseOther (void)
{
	if (!g_openSelect)
		return;
	if (g_openSelect->generic.type == MTYPE_SELECT)
	{
		g_openSelect->open = false;
		g_openSelect->hoverIndex = -1;
	}
	else if (g_openSelect->generic.type == MTYPE_SELECT2)
	{
		menuSelect2_t *s2 = (menuSelect2_t *)g_openSelect;
		s2->open = false;
		s2->openIdx = -1;
		s2->hoverIndex = -1;
	}
	else
	{
		menuBindSelect2_t *bs = (menuBindSelect2_t *)g_openSelect;
		bs->open = false;
		bs->openIdx = -1;
		bs->hoverIndex = -1;
	}
	g_openSelect = NULL;
}

static void Select_Draw (menuSelect_t *s)
{
	const char *name;
	int flags = s->generic.uiFlags | UI_RIGHT | UI_ALTCOLOR;
	char buf[256];

	if (s->generic.flags & QMF_HASFOCUS)
	{
		Menu_SetColor (uis.color.active.u32);
	}

	UI_DrawString (s->generic.x + LCOLUMN_OFFSET, s->generic.y,
		flags, s->generic.name);

	if (s->generic.flags & QMF_HASFOCUS)
	{
		Menu_SetColor (uis.color.active.u32);
	}

	if (s->curvalue < 0 || s->curvalue >= s->numItems)
		name = "???";
	else
		name = s->itemnames[s->curvalue];

	Q_snprintf (buf, sizeof (buf), "%s \x1f", name);
	UI_DrawString (s->generic.x + RCOLUMN_OFFSET, s->generic.y,
		s->generic.uiFlags, buf);

	if (s->generic.flags & QMF_HASFOCUS || s->open)
	{
		Menu_SetNormalColor ();
	}
}

static void Select_DrawDropDown (menuSelect_t *s)
{
	int i, x, y;

	if (!s->open)
		return;

	x = s->generic.x + RCOLUMN_OFFSET;
	y = s->generic.y + CHAR_HEIGHT + 2;

	/* Open upward if not enough space below */
	if (y + s->numItems * CHAR_HEIGHT + 4 > uis.height)
		y = s->generic.y - s->numItems * CHAR_HEIGHT - 4;

	s->dropRect.x = x;
	s->dropRect.y = y;
	s->dropRect.width = s->dropWidth;
	s->dropRect.height = s->numItems * CHAR_HEIGHT + 4;

	/* Background */
	R_DrawFill32 (x, y, s->dropWidth, s->dropRect.height,
		MakeColor (0, 0, 0, 200));
	/* Border */
	R_DrawFill32 (x, y, s->dropWidth, 1,
		uis.color.active.u32);
	R_DrawFill32 (x, y + s->dropRect.height - 1, s->dropWidth, 1,
		uis.color.active.u32);
	R_DrawFill32 (x, y, 1, s->dropRect.height,
		uis.color.active.u32);
	R_DrawFill32 (x + s->dropWidth - 1, y, 1, s->dropRect.height,
		uis.color.active.u32);

	/* Items */
	for (i = 0; i < s->numItems; i++)
	{
		int iy = y + 2 + i * CHAR_HEIGHT;
		int ix = x + CHAR_WIDTH;

		s->itemRects[i].x = x;
		s->itemRects[i].y = iy - 1;
		s->itemRects[i].width = s->dropWidth;
		s->itemRects[i].height = CHAR_HEIGHT + 2;

		if (i == s->hoverIndex || i == s->curvalue)
		{
			R_DrawFill32 (x + 1, iy - 1,
				s->dropWidth - 2, CHAR_HEIGHT + 2,
				uis.color.active.u32);
		}

		if (i == s->curvalue)
		{
			Menu_SetColor (uis.color.active.u32);
		}

		UI_DrawString (ix, iy, UI_LEFT | UI_ALTCOLOR, s->itemnames[i]);

		if (i == s->curvalue)
		{
			Menu_SetNormalColor ();
		}
	}
}

static menuSound_t Select_DoEnter (menuSelect_t *s)
{
	if (!s->numItems)
		return QMS_BEEP;

	if (s->open)
	{
		/* Confirm selection */
		if (s->hoverIndex >= 0)
			s->curvalue = s->hoverIndex;
		s->open = false;
		s->hoverIndex = -1;
		if (g_openSelect == s)
			g_openSelect = NULL;
		Cvar_SetByVar (s->cvar, s->itemnames[s->curvalue], FROM_MENU);
		if (s->generic.change)
			s->generic.change (&s->generic);
		return QMS_OUT;
	}

	/* Open dropdown */
	Select_CloseOther ();
	s->open = true;
	s->hoverIndex = s->curvalue;
	g_openSelect = s;
	return QMS_IN;
}

static menuSound_t Select_Key (menuSelect_t *s, int key)
{
	if (s->open)
	{
		switch (key)
		{
		case K_ESCAPE:
			s->open = false;
			s->hoverIndex = -1;
			if (g_openSelect == s)
				g_openSelect = NULL;
			return QMS_OUT;
		case K_UPARROW:
		case K_KP_UPARROW:
			if (s->hoverIndex < 0)
				s->hoverIndex = s->numItems - 1;
			else if (s->hoverIndex > 0)
				s->hoverIndex--;
			return QMS_SILENT;
		case K_DOWNARROW:
		case K_KP_DOWNARROW:
			if (s->hoverIndex < 0)
				s->hoverIndex = 0;
			else if (s->hoverIndex < s->numItems - 1)
				s->hoverIndex++;
			return QMS_SILENT;
		case K_ENTER:
		case K_KP_ENTER:
			return Select_DoEnter (s);
		case K_MOUSE1:
		case K_MOUSE2:
		case K_MOUSE3:
			/* Let Menu_DefaultKey handle mouse clicks */
			return QMS_NOTHANDLED;
		default:
			/* Block all other keys while dropdown is open */
			return QMS_SILENT;
		}
	}

	/* Closed: Left/Right cycle values */
	if (key == K_LEFTARROW || key == K_KP_LEFTARROW)
	{
		if (s->curvalue > 0)
		{
			s->curvalue--;
		}
		else if (s->curvalue == 0)
		{
			s->curvalue = s->numItems - 1;
		}
		Cvar_SetByVar (s->cvar, s->itemnames[s->curvalue], FROM_MENU);
		if (s->generic.change)
			s->generic.change (&s->generic);
		return QMS_MOVE;
	}
	if (key == K_RIGHTARROW || key == K_KP_RIGHTARROW)
	{
		if (s->curvalue < s->numItems - 1)
		{
			s->curvalue++;
		}
		else if (s->curvalue == s->numItems - 1)
		{
			s->curvalue = 0;
		}
		Cvar_SetByVar (s->cvar, s->itemnames[s->curvalue], FROM_MENU);
		if (s->generic.change)
			s->generic.change (&s->generic);
		return QMS_MOVE;
	}

	return QMS_NOTHANDLED;
}

static menuSound_t Select_MouseMove (menuSelect_t *s)
{
	int i;

	if (!s->open)
		return QMS_NOTHANDLED;

	for (i = 0; i < s->numItems; i++)
	{
		if (UI_CursorInRect (&s->itemRects[i]))
		{
			if (s->hoverIndex != i)
			{
				s->hoverIndex = i;
			}
			return QMS_SILENT;
		}
	}

	return QMS_NOTHANDLED;
}

static void Select_Free (menuSelect_t *s)
{
	int i;

	if (g_openSelect == s)
		g_openSelect = NULL;
	Z_Free (s->generic.name);
	Z_Free (s->generic.status);
	for (i = 0; i < s->numItems; i++)
	{
		Z_Free (s->itemnames[i]);
	}
	Z_Free (s->itemnames);
	Z_Free (s->itemRects);
	Z_Free (s);
}

/*
===================================================================

SELECT2 FIELD (two selects side by side)

===================================================================
*/

static void Select2_Push (menuSelect2_t *s)
{
	int side, i, val;
	bool numeric;

	s->open = false;
	s->openIdx = -1;
	s->hoverIndex = -1;

	for (side = 0; side < 2; side++)
	{
		val = s->cvar[side]->intvalue;
		numeric = COM_IsFloat (s->cvar[side]->string);
		s->curvalue[side] = -1;
		for (i = 0; i < s->numItems[side]; i++)
		{
			if (!strcmp (s->itemnames[side][i], s->cvar[side]->string) ||
				(numeric && COM_IsFloat (s->itemnames[side][i]) &&
					Q_atoi (s->itemnames[side][i]) == val))
			{
				s->curvalue[side] = i;
				break;
			}
		}
		if (s->curvalue[side] < 0 && s->numItems[side] > 0)
			s->curvalue[side] = 0;
	}
}

static void Select2_Pop (menuSelect2_t *s)
{
	int side;
	if (s->open)
	{
		if (g_openSelect == (menuSelect_t *)s)
			g_openSelect = NULL;
		s->open = false;
		s->openIdx = -1;
	}
	for (side = 0; side < 2; side++)
	{
		if (s->curvalue[side] >= 0 && s->curvalue[side] < s->numItems[side])
			Cvar_SetByVar (s->cvar[side], s->itemnames[side][s->curvalue[side]], FROM_MENU);
	}
}

static void Select2_Init (menuSelect2_t *s)
{
	int side, i, len, maxW;

	s->generic.uiFlags &= ~(UI_CENTER | UI_LEFT | UI_RIGHT);
	s->generic.rect.x = s->generic.x + LCOLUMN_OFFSET;
	s->generic.rect.y = s->generic.y;

	UI_StringDimensions (&s->generic.rect,
		s->generic.uiFlags | UI_RIGHT, s->generic.name);

	for (side = 0; side < 2; side++)
	{
		maxW = 0;
		for (i = 0; i < s->numItems[side]; i++)
		{
			len = strlen (s->itemnames[side][i]);
			if (maxW < len)
				maxW = len;
		}
		if (maxW < 3)
			maxW = 3;
		s->colWidth[side] = (maxW + 2) * CHAR_WIDTH;
		s->generic.rect.width += CHAR_WIDTH * 2 + s->colWidth[side];
	}
	s->generic.rect.width += RCOLUMN_OFFSET;
}

static void Select2_Draw (menuSelect2_t *s)
{
	int flags = s->generic.uiFlags | UI_RIGHT | UI_ALTCOLOR;
	char buf[64];
	int side, xOff;

	if (s->generic.flags & QMF_HASFOCUS)
	{
		Menu_SetColor (uis.color.active.u32);
	}

	UI_DrawString (s->generic.x + LCOLUMN_OFFSET, s->generic.y,
		flags, s->generic.name);

	if (s->generic.flags & QMF_HASFOCUS)
	{
		Menu_SetColor (uis.color.active.u32);
	}

	xOff = s->generic.x + RCOLUMN_OFFSET;
	for (side = 0; side < 2; side++)
	{
		const char *name;
		if (s->curvalue[side] < 0 || s->curvalue[side] >= s->numItems[side])
			name = "???";
		else
			name = s->itemnames[side][s->curvalue[side]];

		Q_snprintf (buf, sizeof (buf), "%s \x1f", name);
		UI_DrawString (xOff, s->generic.y, UI_LEFT, buf);
		xOff += s->colWidth[side] + CHAR_WIDTH * 2;
	}

	if (s->generic.flags & QMF_HASFOCUS || s->open)
	{
		Menu_SetNormalColor ();
	}
}

static void Select2_DrawDropDown (menuSelect2_t *s)
{
	int i, x, y, side;

	if (!s->open)
		return;

	side = s->openIdx;
	x = s->generic.x + RCOLUMN_OFFSET;
	for (i = 0; i < side; i++)
		x += s->colWidth[i] + CHAR_WIDTH * 2;
	y = s->generic.y + CHAR_HEIGHT + 2;

	if (y + s->numItems[side] * CHAR_HEIGHT + 4 > uis.height)
		y = s->generic.y - s->numItems[side] * CHAR_HEIGHT - 4;

	s->dropRect.x = x;
	s->dropRect.y = y;
	s->dropRect.width = s->colWidth[side] + CHAR_WIDTH * 2;
	s->dropRect.height = s->numItems[side] * CHAR_HEIGHT + 4;

	if (!s->itemRects)
		s->itemRects = Z_Malloc (
			(s->numItems[0] > s->numItems[1] ? s->numItems[0] : s->numItems[1]) * sizeof (vrect_t));

	R_DrawFill32 (x, y, s->dropRect.width, s->dropRect.height,
		MakeColor (0, 0, 0, 200));
	R_DrawFill32 (x, y, s->dropRect.width, 1, uis.color.active.u32);
	R_DrawFill32 (x, y + s->dropRect.height - 1, s->dropRect.width, 1, uis.color.active.u32);
	R_DrawFill32 (x, y, 1, s->dropRect.height, uis.color.active.u32);
	R_DrawFill32 (x + s->dropRect.width - 1, y, 1, s->dropRect.height, uis.color.active.u32);

	for (i = 0; i < s->numItems[side]; i++)
	{
		int iy = y + 2 + i * CHAR_HEIGHT;
		int ix = x + CHAR_WIDTH;

		s->itemRects[i].x = x;
		s->itemRects[i].y = iy - 1;
		s->itemRects[i].width = s->dropRect.width;
		s->itemRects[i].height = CHAR_HEIGHT + 2;

		if (i == s->hoverIndex || i == s->curvalue[side])
		{
			R_DrawFill32 (x + 1, iy - 1,
				s->dropRect.width - 2, CHAR_HEIGHT + 2,
				uis.color.active.u32);
		}

		if (i == s->curvalue[side])
			Menu_SetColor (uis.color.active.u32);

		UI_DrawString (ix, iy, UI_LEFT | UI_ALTCOLOR, s->itemnames[side][i]);

		if (i == s->curvalue[side])
			Menu_SetNormalColor ();
	}
}

static menuSound_t Select2_DoEnter (menuSelect2_t *s)
{
	if (s->open)
	{
		/* Confirm selection */
		int closingSide = s->openIdx;
		if (s->hoverIndex >= 0)
			s->curvalue[closingSide] = s->hoverIndex;
		s->open = false;
		s->openIdx = -1;
		s->hoverIndex = -1;
		if (g_openSelect == (menuSelect_t *)s)
			g_openSelect = NULL;
		Cvar_SetByVar (s->cvar[closingSide],
			s->itemnames[closingSide][s->curvalue[closingSide]], FROM_MENU);
		if (s->generic.change)
			s->generic.change (&s->generic);
		return QMS_OUT;
	}

	/* Open dropdown for side 0 (keyboard Enter); mouse click overrides this */
	Select_CloseOther ();
	s->open = true;
	s->openIdx = 0;
	s->hoverIndex = s->curvalue[0];
	g_openSelect = (menuSelect_t *)s;
	return QMS_IN;
}

static menuSound_t Select2_Key (menuSelect2_t *s, int key)
{
	int side;

	if (s->open)
	{
		side = s->openIdx;
		switch (key)
		{
		case K_ESCAPE:
			s->open = false;
			s->openIdx = -1;
			s->hoverIndex = -1;
			if (g_openSelect == (menuSelect_t *)s)
				g_openSelect = NULL;
			return QMS_OUT;
		case K_UPARROW:
		case K_KP_UPARROW:
			if (s->hoverIndex < 0)
				s->hoverIndex = s->numItems[side] - 1;
			else if (s->hoverIndex > 0)
				s->hoverIndex--;
			return QMS_SILENT;
		case K_DOWNARROW:
		case K_KP_DOWNARROW:
			if (s->hoverIndex < 0)
				s->hoverIndex = 0;
			else if (s->hoverIndex < s->numItems[side] - 1)
				s->hoverIndex++;
			return QMS_SILENT;
		case K_ENTER:
		case K_KP_ENTER:
			return Select2_DoEnter (s);
		case K_LEFTARROW:
		case K_KP_LEFTARROW:
			if (side > 0)
			{
				s->openIdx = 0;
				s->hoverIndex = s->curvalue[0];
			}
			return QMS_SILENT;
		case K_RIGHTARROW:
		case K_KP_RIGHTARROW:
			if (side < 1)
			{
				s->openIdx = 1;
				s->hoverIndex = s->curvalue[1];
			}
			return QMS_SILENT;
		case K_MOUSE1:
		case K_MOUSE2:
		case K_MOUSE3:
			return QMS_NOTHANDLED;
		default:
			return QMS_SILENT;
		}
	}

	/* Closed: Left/Right cycle active side */
	if (key == K_LEFTARROW || key == K_KP_LEFTARROW)
	{
		side = 0;
		if (s->curvalue[side] > 0)
			s->curvalue[side]--;
		else
			s->curvalue[side] = s->numItems[side] - 1;
		Cvar_SetByVar (s->cvar[side], s->itemnames[side][s->curvalue[side]], FROM_MENU);
		return QMS_MOVE;
	}
	if (key == K_RIGHTARROW || key == K_KP_RIGHTARROW)
	{
		side = 1;
		if (s->curvalue[side] < s->numItems[side] - 1)
			s->curvalue[side]++;
		else
			s->curvalue[side] = 0;
		Cvar_SetByVar (s->cvar[side], s->itemnames[side][s->curvalue[side]], FROM_MENU);
		return QMS_MOVE;
	}

	return QMS_NOTHANDLED;
}

static menuSound_t Select2_MouseMove (menuSelect2_t *s)
{
	int i, side;

	if (!s->open)
		return QMS_NOTHANDLED;

	side = s->openIdx;
	for (i = 0; i < s->numItems[side]; i++)
	{
		if (UI_CursorInRect (&s->itemRects[i]))
		{
			if (s->hoverIndex != i)
				s->hoverIndex = i;
			return QMS_SILENT;
		}
	}
	return QMS_NOTHANDLED;
}

static void Select2_Free (menuSelect2_t *s)
{
	int side, i;

	if (g_openSelect == (menuSelect_t *)s)
		g_openSelect = NULL;
	Z_Free (s->generic.name);
	Z_Free (s->generic.status);
	for (side = 0; side < 2; side++)
	{
		for (i = 0; i < s->numItems[side]; i++)
			Z_Free (s->itemnames[side][i]);
		Z_Free (s->itemnames[side]);
	}
	Z_Free (s->itemRects);
	Z_Free (s);
}

/*
===================================================================

BINDSELECT2 FIELD (bind + two selects in one row)

===================================================================
*/

#define BINDSELECT2_INDEX_WIDTH    (CHAR_WIDTH * 2)
#define BINDSELECT2_COL_GAP        (CHAR_WIDTH * 2)
#define BINDSELECT2_VALUE_CHARS    16

static int BindSelect2_TableWidth (const menuBindSelect2_t *bs)
{
	return BINDSELECT2_INDEX_WIDTH + BINDSELECT2_COL_GAP +
		bs->colWidth[0] + BINDSELECT2_COL_GAP +
		bs->colWidth[1] + BINDSELECT2_COL_GAP +
		bs->colWidth[2];
}

static int BindSelect2_TableLeft (const menuBindSelect2_t *bs)
{
	return bs->generic.x - BindSelect2_TableWidth (bs) / 2;
}

static int BindSelect2_KeyX (const menuBindSelect2_t *bs)
{
	return BindSelect2_TableLeft (bs) + BINDSELECT2_INDEX_WIDTH + BINDSELECT2_COL_GAP;
}

static int BindSelect2_SelectX (const menuBindSelect2_t *bs, int side)
{
	int x = BindSelect2_KeyX (bs) + bs->colWidth[0] + BINDSELECT2_COL_GAP;

	if (side > 0)
	{
		x += bs->colWidth[1] + BINDSELECT2_COL_GAP;
	}

	return x;
}

static int BindSelect2_ColumnAt (const menuBindSelect2_t *bs, int mouseX)
{
	int keyX = BindSelect2_KeyX (bs);
	int holdX = BindSelect2_SelectX (bs, 0);

	if (mouseX < keyX + bs->colWidth[0])
	{
		return 0;
	}
	if (mouseX < holdX + bs->colWidth[1] + BINDSELECT2_COL_GAP)
	{
		return 1;
	}
	return 2;
}

static void BindFields_Remove (const char *cmd)
{
	int key;
	while ((key = Key_EnumBindings (0, cmd)) != -1)
		Key_SetBinding (key, "");
}

static void BindSelect2_Remove (menuBindSelect2_t *bs)
{
	BindFields_Remove (bs->cmd);
	if (bs->legacycmd && Q_stricmp (bs->legacycmd, bs->cmd))
	{
		BindFields_Remove (bs->legacycmd);
	}
}

static void BindSelect2_Migrate (menuBindSelect2_t *bs)
{
	int key;

	if (!bs->legacycmd || !Q_stricmp (bs->legacycmd, bs->cmd))
	{
		return;
	}

	while ((key = Key_EnumBindings (0, bs->legacycmd)) != -1)
	{
		Key_SetBinding (key, bs->cmd);
	}
}

static void BindSelect2_Push (menuBindSelect2_t *bs)
{
	int key;
	int side, i;
	BindSelect2_Migrate (bs);
	key = Key_EnumBindings (0, bs->cmd);
	bs->altbinding[0] = 0;
	if (key == -1)
		strcpy (bs->binding, "???");
	else
	{
		Q_strlcpy (bs->binding, Key_KeynumToLabel (key), sizeof (bs->binding));
		key = Key_EnumBindings (key + 1, bs->cmd);
		if (key != -1)
			Q_strlcpy (bs->altbinding, Key_KeynumToLabel (key), sizeof (bs->altbinding));
	}
	bs->open = false;
	bs->openIdx = -1;
	bs->hoverIndex = -1;
	bs->focusPart = 0;
	for (side = 0; side < 2; side++)
	{
		bs->curvalue[side] = -1;
		for (i = 0; i < bs->numItems[side]; i++)
		{
			if (!strcmp (bs->itemnames[side][i], bs->cvar[side]->string))
			{
				bs->curvalue[side] = i;
				break;
			}
		}
	}
}

static void BindSelect2_Update (menuFrameWork_t *menu)
{
	int i;

	for (i = 0; i < menu->nitems; i++)
	{
		if (((menuCommon_t *)menu->items[i])->type == MTYPE_BINDSELECT2)
		{
			BindSelect2_Push (menu->items[i]);
			BindSelect2_Init (menu->items[i]);
		}
	}
}

static void BindSelect2_Pop (menuBindSelect2_t *bs)
{
	Key_WaitKey (NULL, NULL);
	if (bs->open)
	{
		if (g_openSelect == (menuSelect_t *)bs)
			g_openSelect = NULL;
		bs->open = false;
		bs->openIdx = -1;
	}
	/* Deliberate selections are saved immediately; closing preserves custom values. */
}

static void BindSelect2_Init (menuBindSelect2_t *bs)
{
	int side, i, row, len, maxW;
	const menuFrameWork_t *menu = bs->generic.parent;
	bs->generic.uiFlags &= ~(UI_CENTER | UI_LEFT | UI_RIGHT);
	bs->generic.rect.y = bs->generic.y;
	bs->generic.rect.height = CHAR_HEIGHT;
	bs->colWidth[0] = 10 * CHAR_WIDTH;
	for (side = 0; side < 2; side++)
	{
		maxW = side ? 7 : 4;
		/* Keep the table columns aligned when any row has a custom value. */
		for (row = 0; row < menu->nitems; row++)
		{
			const menuCommon_t *item = menu->items[row];
			if (item->type == MTYPE_BINDSELECT2)
			{
				const menuBindSelect2_t *other = (const menuBindSelect2_t *)item;
				len = min ((int)strlen (other->cvar[side]->string), BINDSELECT2_VALUE_CHARS);
				if (maxW < len)
					maxW = len;
			}
		}
		for (i = 0; i < bs->numItems[side]; i++)
		{
			len = strlen (bs->itemnames[side][i]);
			if (maxW < len)
				maxW = len;
		}
		bs->colWidth[side + 1] = (maxW + 2) * CHAR_WIDTH;
	}
	bs->generic.rect.x = BindSelect2_TableLeft (bs);
	bs->generic.rect.width = BindSelect2_TableWidth (bs);
}

static bool BindSelect2_IsFirst (const menuBindSelect2_t *bs)
{
	int i;
	const menuFrameWork_t *menu = bs->generic.parent;

	for (i = 0; i < menu->nitems; i++)
	{
		const menuCommon_t *item = menu->items[i];
		if (item == &bs->generic)
		{
			return true;
		}
		if (item->type == MTYPE_BINDSELECT2)
		{
			return false;
		}
	}

	return false;
}

static void BindSelect2_DrawHeader (menuBindSelect2_t *bs)
{
	int xOff, y;

	if (!BindSelect2_IsFirst (bs))
	{
		return;
	}

	y = bs->generic.y - CHAR_HEIGHT;
	if (y < bs->generic.parent->y1)
	{
		return;
	}

	UI_DrawString (BindSelect2_TableLeft (bs) + BINDSELECT2_INDEX_WIDTH, y,
		UI_RIGHT | UI_ALTCOLOR, "#");

	xOff = BindSelect2_KeyX (bs);
	UI_DrawString (xOff, y, UI_LEFT | UI_ALTCOLOR, "Key");

	xOff = BindSelect2_SelectX (bs, 0);
	UI_DrawString (xOff, y, UI_LEFT | UI_ALTCOLOR, "Hold");

	xOff = BindSelect2_SelectX (bs, 1);
	UI_DrawString (xOff, y, UI_LEFT | UI_ALTCOLOR, "Release");
}

static void BindSelect2_Draw (menuBindSelect2_t *bs)
{
	char string[MAX_STRING_CHARS], buf[64];
	int flags = UI_ALTCOLOR, xOff, side;
	bool hasFocus = (bs->generic.flags & QMF_HASFOCUS) != 0;
	BindSelect2_DrawHeader (bs);
	if (hasFocus)
		Menu_SetColor (uis.color.active.u32);
	else if (bs->generic.parent->keywait)
		Menu_SetColor (uis.color.disabled.u32);
	if (bs->altbinding[0])
		Q_concat (string, sizeof (string), bs->binding, " or ", bs->altbinding);
	else if (bs->binding[0])
		Q_strlcpy (string, bs->binding, sizeof (string));
	else
		strcpy (string, "???");
	xOff = BindSelect2_KeyX (bs);
	if (hasFocus && bs->focusPart == 0)
		R_DrawFill32 (xOff, bs->generic.y - 1, bs->colWidth[0], CHAR_HEIGHT + 2, uis.color.selection.u32);
	UI_DrawString (BindSelect2_TableLeft (bs) + BINDSELECT2_INDEX_WIDTH, bs->generic.y,
		UI_RIGHT | flags, bs->generic.name);
	UI_DrawString (xOff, bs->generic.y, UI_LEFT | ((hasFocus && bs->focusPart == 0) ? 0 : flags), string);
	for (side = 0; side < 2; side++)
	{
		bool sf = hasFocus && bs->focusPart == (side + 1);
		const char *n = bs->cvar[side]->string;
		char value[BINDSELECT2_VALUE_CHARS + 1];
		size_t length = strlen (n);
		Q_strlcpy (value, n, sizeof (value));
		if (length >= sizeof (value))
			memcpy (value + BINDSELECT2_VALUE_CHARS - 3, "...", 3);
		for (char *p = value; *p; p++)
			if ((unsigned char)*p < 32 || (unsigned char)*p == 127)
				*p = ' ';
		xOff = BindSelect2_SelectX (bs, side);
		if (sf)
		{
			Menu_SetColor (uis.color.active.u32);
			R_DrawFill32 (xOff, bs->generic.y - 1, bs->colWidth[side + 1], CHAR_HEIGHT + 2, uis.color.selection.u32);
		}
		Q_snprintf (buf, sizeof (buf), "%s \x1f", value);
		UI_DrawString (xOff, bs->generic.y, UI_LEFT | (sf ? 0 : flags), buf);
		if (sf)
			Menu_SetNormalColor ();
	}
	if (hasFocus || bs->generic.parent->keywait)
		Menu_SetNormalColor ();
}

static void BindSelect2_DrawDropDown (menuBindSelect2_t *bs)
{
	int i, x, y, side;
	if (!bs->open || bs->openIdx < 0)
		return;
	side = bs->openIdx;
	x = BindSelect2_SelectX (bs, side);
	y = bs->generic.y + CHAR_HEIGHT + 2;
	if (y + bs->numItems[side] * CHAR_HEIGHT + 4 > uis.height)
		y = bs->generic.y - bs->numItems[side] * CHAR_HEIGHT - 4;
	bs->dropRect.x = x;
	bs->dropRect.y = y;
	bs->dropRect.width = bs->colWidth[side + 1] + CHAR_WIDTH * 2;
	bs->dropRect.height = bs->numItems[side] * CHAR_HEIGHT + 4;
	R_DrawFill32 (x, y, bs->dropRect.width, bs->dropRect.height, MakeColor (0, 0, 0, 200));
	R_DrawFill32 (x, y, bs->dropRect.width, 1, uis.color.active.u32);
	R_DrawFill32 (x, y + bs->dropRect.height - 1, bs->dropRect.width, 1, uis.color.active.u32);
	R_DrawFill32 (x, y, 1, bs->dropRect.height, uis.color.active.u32);
	R_DrawFill32 (x + bs->dropRect.width - 1, y, 1, bs->dropRect.height, uis.color.active.u32);
	for (i = 0; i < bs->numItems[side]; i++)
	{
		int iy = y + 2 + i * CHAR_HEIGHT;
		bs->itemRects[i].x = x;
		bs->itemRects[i].y = iy - 1;
		bs->itemRects[i].width = bs->dropRect.width;
		bs->itemRects[i].height = CHAR_HEIGHT + 2;
		if (i == bs->hoverIndex || i == bs->curvalue[side])
			R_DrawFill32 (x + 1, iy - 1, bs->dropRect.width - 2, CHAR_HEIGHT + 2, uis.color.active.u32);
		if (i == bs->curvalue[side])
			Menu_SetColor (uis.color.active.u32);
		UI_DrawString (x + CHAR_WIDTH, iy, UI_LEFT | UI_ALTCOLOR, bs->itemnames[side][i]);
		if (i == bs->curvalue[side])
			Menu_SetNormalColor ();
	}
}

static void BindSelect2_Select (menuBindSelect2_t *bs, int side, int index)
{
	if (side < 0 || side >= 2 || index < 0 || index >= bs->numItems[side])
		return;
	bs->curvalue[side] = index;
	Cvar_SetByVar (bs->cvar[side], bs->itemnames[side][index], FROM_MENU);
}

static menuSound_t BindSelect2_DoEnter (menuBindSelect2_t *bs)
{
	menuFrameWork_t *menu = bs->generic.parent;
	if (bs->open)
	{
		int cs = bs->openIdx;
		BindSelect2_Select (bs, cs, bs->hoverIndex);
		bs->open = false;
		bs->openIdx = -1;
		bs->hoverIndex = -1;
		if (g_openSelect == (menuSelect_t *)bs)
			g_openSelect = NULL;
		return QMS_OUT;
	}
	if (bs->focusPart == 0)
	{
		menu->keywait = true;
		menu->status = bs->altstatus;
		Key_WaitKey (keybind_cb, bs);
		return QMS_IN;
	}
	{
		int side = bs->focusPart - 1;
		Select_CloseOther ();
		bs->open = true;
		bs->openIdx = side;
		bs->hoverIndex = bs->curvalue[side];
		g_openSelect = (menuSelect_t *)bs;
	}
	return QMS_IN;
}

static menuSound_t BindSelect2_Key (menuBindSelect2_t *bs, int key)
{
	menuFrameWork_t *menu = bs->generic.parent;
	if (menu->keywait)
		return QMS_OUT;
	if (bs->open)
	{
		int side = bs->openIdx;
		switch (key)
		{
		case K_ESCAPE:
			bs->open = false;
			bs->openIdx = -1;
			bs->hoverIndex = -1;
			if (g_openSelect == (menuSelect_t *)bs)
				g_openSelect = NULL;
			return QMS_OUT;
		case K_UPARROW:
		case K_KP_UPARROW:
			if (bs->hoverIndex > 0)
				bs->hoverIndex--;
			else
				bs->hoverIndex = bs->numItems[side] - 1;
			return QMS_SILENT;
		case K_DOWNARROW:
		case K_KP_DOWNARROW:
			if (bs->hoverIndex < bs->numItems[side] - 1)
				bs->hoverIndex++;
			else
				bs->hoverIndex = 0;
			return QMS_SILENT;
		case K_ENTER:
		case K_KP_ENTER:
			return BindSelect2_DoEnter (bs);
		case K_LEFTARROW:
			if (side > 0)
			{
				bs->openIdx = 0;
				bs->hoverIndex = bs->curvalue[0];
			}
			return QMS_SILENT;
		case K_RIGHTARROW:
			if (side < 1)
			{
				bs->openIdx = 1;
				bs->hoverIndex = bs->curvalue[1];
			}
			return QMS_SILENT;
		case K_MOUSE1:
		case K_MOUSE2:
		case K_MOUSE3:
			return QMS_NOTHANDLED;
		default:
			return QMS_SILENT;
		}
	}
	if (bs->focusPart == 0)
	{
		if (key == K_BACKSPACE || key == K_DEL)
		{
			BindSelect2_Remove (bs);
			BindSelect2_Update (menu);
			return QMS_IN;
		}
	}
	else
	{
		int s = bs->focusPart - 1;
		if (key == K_LEFTARROW)
		{
			BindSelect2_Select (bs, s, bs->curvalue[s] > 0 ? bs->curvalue[s] - 1 : bs->numItems[s] - 1);
			return QMS_MOVE;
		}
		if (key == K_RIGHTARROW)
		{
			BindSelect2_Select (bs, s, bs->curvalue[s] < bs->numItems[s] - 1 ? bs->curvalue[s] + 1 : 0);
			return QMS_MOVE;
		}
	}
	if (key == K_TAB)
	{
		int dir = Key_IsDown (K_SHIFT) ? -1 : 1;
		bs->focusPart = (bs->focusPart + dir + 3) % 3;
		return QMS_SILENT;
	}
	return QMS_NOTHANDLED;
}

static menuSound_t BindSelect2_MouseMove (menuBindSelect2_t *bs)
{
	int i, side;
	if (bs->open)
	{
		side = bs->openIdx;
		for (i = 0; i < bs->numItems[side]; i++)
			if (UI_CursorInRect (&bs->itemRects[i]))
			{
				if (bs->hoverIndex != i)
					bs->hoverIndex = i;
				return QMS_SILENT;
			}
		return QMS_NOTHANDLED;
	}
	/* Track which column the mouse is over when closed */
	{
		int col = BindSelect2_ColumnAt (bs, uis.mouseCoords[0]);
		if (col != bs->focusPart)
			bs->focusPart = col;
	}
	return QMS_NOTHANDLED;
}

static void BindSelect2_Free (menuBindSelect2_t *bs)
{
	int side, i;
	if (g_openSelect == (menuSelect_t *)bs)
		g_openSelect = NULL;
	Z_Free (bs->cmd);
	Z_Free (bs->legacycmd);
	Z_Free (bs->generic.name);
	Z_Free (bs->generic.status);
	Z_Free (bs->altstatus);
	for (side = 0; side < 2; side++)
	{
		for (i = 0; i < bs->numItems[side]; i++)
			Z_Free (bs->itemnames[side][i]);
		Z_Free (bs->itemnames[side]);
	}
	Z_Free (bs->itemRects);
	Z_Free (bs);
}

/*
===================================================================

FIELD CONTROL

===================================================================
*/

static void Field_Push (menuField_t *f)
{
	IF_Init (&f->field, f->width, f->length ? f->length : f->width);
	IF_Replace (&f->field, f->cvar->string);
}

static void Slider_Pop (menuSlider_t *s);
static void Menu_LiveCommit (menuCommon_t *item);

static void Field_Pop (menuField_t *f)
{
	if (f->colorPickerOnly)
	{
		IF_Replace (&f->field, f->cvar->string);
		return;
	}

	Cvar_SetByVar (f->cvar, f->field.text, FROM_MENU);
}

static void Field_Free (menuField_t *f)
{
	Z_Free (f->generic.name);
	Z_Free (f->generic.status);
	Z_Free (f);
}

#define FIELD_COLOR_SWATCH_WIDTH    (CHAR_WIDTH * 3)
#define FIELD_COLOR_PICKER_SWATCH_WIDTH (CHAR_WIDTH * 6)
#define FIELD_COLOR_SWATCH_GAP      CHAR_WIDTH
#define FIELD_COLOR_SWATCH_HEIGHT   (CHAR_HEIGHT + 2)

static int Field_TextInputWidth (const menuField_t *f)
{
	return f->colorPickerOnly ? 0 : f->width * CHAR_WIDTH;
}

static int Field_ColorSwatchWidth (const menuField_t *f)
{
	return f->colorPickerOnly ?
		FIELD_COLOR_PICKER_SWATCH_WIDTH :
		FIELD_COLOR_SWATCH_WIDTH;
}

static int Field_ColorPreviewWidth (const menuField_t *f)
{
	if (!f->colorPreview || !f->generic.name)
	{
		return 0;
	}

	return Field_ColorSwatchWidth (f) +
		(Field_TextInputWidth (f) ? FIELD_COLOR_SWATCH_GAP : 0);
}

static int Field_ClampColorComponent (int value)
{
	if (value < 0)
	{
		return 0;
	}
	if (value > 255)
	{
		return 255;
	}
	return value;
}

static bool Field_ParseColor (const char *s, color_t *color)
{
	int r, g, b, a = 255;
	int components;

	if (!s || !*s)
	{
		return false;
	}

	if (*s == '#' || !strchr (s, ' '))
	{
		return SCR_ParseColor (s, color);
	}

	components = sscanf (s, "%d %d %d %d", &r, &g, &b, &a);
	if (components < 3)
	{
		return false;
	}

	color->u8[0] = Field_ClampColorComponent (r);
	color->u8[1] = Field_ClampColorComponent (g);
	color->u8[2] = Field_ClampColorComponent (b);
	color->u8[3] = Field_ClampColorComponent (a);
	return true;
}

static void Field_DrawColorSwatch (int x, int y, int w, int h,
	uint32 border, const color_t *color,
	bool valid)
{
	const uint32 light = MakeColor (150, 150, 150, 255);
	const uint32 dark = MakeColor (50, 50, 50, 255);

	if (w < 4 || h < 4)
	{
		return;
	}

	if (w >= 24 && h >= 24)
	{
		uint32 shadow_color = UI_MenuPanelShadowColor ();
		if (shadow_color & 0xff000000)
		{
			R_DrawFill32 (x + 2, y + 2, w, h, shadow_color);
		}
	}

	R_DrawFill32 (x, y, w, h, border);
	R_DrawFill32 (x + 1, y + 1, w - 2, h - 2, dark);
	R_DrawFill32 (x + 1, y + 1, (w - 2) / 2, (h - 2) / 2, light);
	R_DrawFill32 (x + 1 + (w - 2) / 2, y + 1 + (h - 2) / 2,
		(w - 2) - (w - 2) / 2, (h - 2) - (h - 2) / 2, light);

	if (valid && color)
	{
		R_DrawFill32 (x + 2, y + 2, w - 4, h - 4, color->u32);
	}
	else
	{
		R_DrawFill32 (x + 2, y + 2, w - 4, h - 4, MakeColor (96, 0, 0, 220));
	}

	if (w >= 24 && h >= 24)
	{
		uint32 border_color = UI_MenuPanelBorderColor ();
		if (border_color & 0xff000000)
		{
			R_DrawFill32 (x, y, w, 1, border_color);		 // Top
			R_DrawFill32 (x, y + h - 1, w, 1, border_color); // Bottom
			R_DrawFill32 (x, y, 1, h, border_color);		 // Left
			R_DrawFill32 (x + w - 1, y, 1, h, border_color); // Right
		}
	}
}

static void Field_DrawColorPreview (menuField_t *f, int x, int y)
{
	color_t color;
	const uint32 border = (f->generic.flags & QMF_HASFOCUS) ?
		uis.color.active.u32 :
		uis.color.normal.u32;
	const char *value = f->colorPickerOnly ? f->cvar->string : f->field.text;
	const bool valid = Field_ParseColor (value, &color);

	Field_DrawColorSwatch (x, y, Field_ColorSwatchWidth (f),
		FIELD_COLOR_SWATCH_HEIGHT, border, &color, valid);
}

/*
===================================================================

COLOR PICKER MENU

===================================================================
*/

#define COLOR_PICKER_SWATCH_HEIGHT  (CHAR_HEIGHT * 4)
#define COLOR_PICKER_ORIGINAL_HEIGHT CHAR_HEIGHT
#define COLOR_PICKER_PALETTE_COUNT  9
#define COLOR_PICKER_PALETTE_SIZE   (CHAR_HEIGHT * 2)
#define COLOR_PICKER_PALETTE_GAP    4

typedef struct
{
	bool initialized;
	menuFrameWork_t menu;
	menuSlider_t red;
	menuSlider_t green;
	menuSlider_t blue;
	menuSlider_t alpha;
	cvar_t *redCvar;
	cvar_t *greenCvar;
	cvar_t *blueCvar;
	cvar_t *alphaCvar;
	cvar_t *target;
	menuField_t *source;
	color_t original;
	bool originalValid;
	char title[64];
	char redName[16];
	char greenName[16];
	char blueName[16];
	char alphaName[16];
} colorPicker_t;

static colorPicker_t colorPicker;

static const uint32 colorPickerPalette[COLOR_PICKER_PALETTE_COUNT] = {
	MakeColor (255, 0, 0, 255),
	MakeColor (255, 160, 0, 255),
	MakeColor (255, 255, 0, 255),
	MakeColor (0, 255, 0, 255),
	MakeColor (0, 200, 255, 255),
	MakeColor (0, 80, 255, 255),
	MakeColor (255, 0, 255, 255),
	MakeColor (255, 255, 255, 255),
	MakeColor (0, 0, 0, 255)
};

static int ColorPicker_Component (menuSlider_t *slider)
{
	return Field_ClampColorComponent (Q_rint (slider->curvalue));
}

static void ColorPicker_CurrentColor (color_t *color)
{
	color->u8[0] = ColorPicker_Component (&colorPicker.red);
	color->u8[1] = ColorPicker_Component (&colorPicker.green);
	color->u8[2] = ColorPicker_Component (&colorPicker.blue);
	color->u8[3] = ColorPicker_Component (&colorPicker.alpha);
}

static void ColorPicker_UpdateNames (void)
{
	Q_snprintf (colorPicker.redName, sizeof (colorPicker.redName),
		"red %3d", ColorPicker_Component (&colorPicker.red));
	Q_snprintf (colorPicker.greenName, sizeof (colorPicker.greenName),
		"green %3d", ColorPicker_Component (&colorPicker.green));
	Q_snprintf (colorPicker.blueName, sizeof (colorPicker.blueName),
		"blue %3d", ColorPicker_Component (&colorPicker.blue));
	Q_snprintf (colorPicker.alphaName, sizeof (colorPicker.alphaName),
		"alpha %3d", ColorPicker_Component (&colorPicker.alpha));
}

static void ColorPicker_CommitTarget (void)
{
	char value[32];

	if (!colorPicker.target)
	{
		return;
	}

	Q_snprintf (value, sizeof (value), "%d %d %d %d",
		ColorPicker_Component (&colorPicker.red),
		ColorPicker_Component (&colorPicker.green),
		ColorPicker_Component (&colorPicker.blue),
		ColorPicker_Component (&colorPicker.alpha));
	if (strcmp (colorPicker.target->string, value))
	{
		Cvar_SetByVar (colorPicker.target, value, FROM_MENU);
	}
}

static void ColorPicker_SetSlider (menuSlider_t *slider, cvar_t *cvar, int value)
{
	value = Field_ClampColorComponent (value);
	slider->curvalue = value;
	slider->modified = false;
	Cvar_SetInteger (cvar, value, FROM_MENU);
}

static void ColorPicker_SetFromColor (const color_t *color, bool keepAlpha)
{
	ColorPicker_SetSlider (&colorPicker.red, colorPicker.redCvar, color->u8[0]);
	ColorPicker_SetSlider (&colorPicker.green, colorPicker.greenCvar, color->u8[1]);
	ColorPicker_SetSlider (&colorPicker.blue, colorPicker.blueCvar, color->u8[2]);
	if (!keepAlpha)
	{
		ColorPicker_SetSlider (&colorPicker.alpha, colorPicker.alphaCvar, color->u8[3]);
	}
}

static void ColorPicker_GetRects (vrect_t *current, vrect_t *original,
	vrect_t palette[COLOR_PICKER_PALETTE_COUNT])
{
	int i;
	int y;
	const int paletteWidth = COLOR_PICKER_PALETTE_COUNT * COLOR_PICKER_PALETTE_SIZE +
		(COLOR_PICKER_PALETTE_COUNT - 1) * COLOR_PICKER_PALETTE_GAP;
	int x = colorPicker.menu.mins[0];
	int width = colorPicker.menu.maxs[0] - colorPicker.menu.mins[0];

	if (width < paletteWidth)
	{
		x = (colorPicker.menu.mins[0] + colorPicker.menu.maxs[0] - paletteWidth) / 2;
		width = paletteWidth;
	}
	if (x < MENU_SPACING)
	{
		x = MENU_SPACING;
	}
	if (x + width > uis.width - MENU_SPACING)
	{
		x = uis.width - MENU_SPACING - width;
	}

	y = colorPicker.menu.maxs[1] + MENU_SPACING;
	if (y + COLOR_PICKER_SWATCH_HEIGHT + COLOR_PICKER_ORIGINAL_HEIGHT +
			COLOR_PICKER_PALETTE_SIZE + MENU_SPACING * 2 >
		uis.height)
	{
		y = colorPicker.menu.mins[1] - COLOR_PICKER_SWATCH_HEIGHT -
			COLOR_PICKER_ORIGINAL_HEIGHT - COLOR_PICKER_PALETTE_SIZE -
			MENU_SPACING * 2;
	}
	if (y < MENU_SPACING)
	{
		y = colorPicker.menu.maxs[1] + MENU_SPACING;
	}

	current->x = x;
	current->y = y;
	current->width = width;
	current->height = COLOR_PICKER_SWATCH_HEIGHT;

	original->x = x;
	original->y = current->y + current->height + COLOR_PICKER_PALETTE_GAP;
	original->width = width;
	original->height = COLOR_PICKER_ORIGINAL_HEIGHT;

	x += (width - paletteWidth) / 2;
	y = original->y + original->height + MENU_SPACING;
	for (i = 0; i < COLOR_PICKER_PALETTE_COUNT; i++)
	{
		palette[i].x = x + i * (COLOR_PICKER_PALETTE_SIZE + COLOR_PICKER_PALETTE_GAP);
		palette[i].y = y;
		palette[i].width = COLOR_PICKER_PALETTE_SIZE;
		palette[i].height = COLOR_PICKER_PALETTE_SIZE;
	}
}

static void ColorPicker_Draw (menuFrameWork_t *menu)
{
	int i;
	color_t current;
	vrect_t currentRect, originalRect;
	vrect_t palette[COLOR_PICKER_PALETTE_COUNT];

	ColorPicker_CommitTarget ();
	ColorPicker_UpdateNames ();
	Menu_Draw (menu);

	ColorPicker_CurrentColor (&current);
	ColorPicker_GetRects (&currentRect, &originalRect, palette);

	Field_DrawColorSwatch (currentRect.x, currentRect.y,
		currentRect.width, currentRect.height,
		uis.color.active.u32, &current, true);
	Field_DrawColorSwatch (originalRect.x, originalRect.y,
		originalRect.width, originalRect.height,
		uis.color.normal.u32, &colorPicker.original,
		colorPicker.originalValid);

	for (i = 0; i < COLOR_PICKER_PALETTE_COUNT; i++)
	{
		color_t color;
		color.u32 = colorPickerPalette[i];
		Field_DrawColorSwatch (palette[i].x, palette[i].y,
			palette[i].width, palette[i].height,
			uis.color.normal.u32, &color, true);
	}
}

static menuSound_t ColorPicker_Keydown (menuFrameWork_t *menu, int key)
{
	int i;
	vrect_t currentRect, originalRect;
	vrect_t palette[COLOR_PICKER_PALETTE_COUNT];

	if (key != K_MOUSE1)
	{
		return QMS_NOTHANDLED;
	}

	ColorPicker_GetRects (&currentRect, &originalRect, palette);
	if (colorPicker.originalValid && UI_CursorInRect (&originalRect))
	{
		ColorPicker_SetFromColor (&colorPicker.original, false);
		ColorPicker_CommitTarget ();
		return QMS_MOVE;
	}

	for (i = 0; i < COLOR_PICKER_PALETTE_COUNT; i++)
	{
		if (UI_CursorInRect (&palette[i]))
		{
			color_t color;
			color.u32 = colorPickerPalette[i];
			ColorPicker_SetFromColor (&color, true);
			ColorPicker_CommitTarget ();
			return QMS_MOVE;
		}
	}

	return QMS_NOTHANDLED;
}

static void ColorPicker_Pop (menuFrameWork_t *menu)
{
	ColorPicker_CommitTarget ();
	if (colorPicker.source && colorPicker.target)
	{
		IF_Replace (&colorPicker.source->field, colorPicker.target->string);
	}
	colorPicker.target = NULL;
	colorPicker.source = NULL;
}

static void ColorPicker_InitSlider (menuSlider_t *slider, const char *name,
	cvar_t *cvar)
{
	memset (slider, 0, sizeof (*slider));
	slider->generic.type = MTYPE_SLIDER;
	slider->generic.name = (char *)name;
	slider->cvar = cvar;
	slider->minvalue = 0.0f;
	slider->maxvalue = 255.0f;
	slider->step = 1.0f;
	Menu_AddItem (&colorPicker.menu, slider);
}

static void ColorPicker_Init (void)
{
	if (colorPicker.initialized)
	{
		return;
	}

	memset (&colorPicker, 0, sizeof (colorPicker));
	colorPicker.initialized = true;
	colorPicker.redCvar = Cvar_Get ("ui_colorpicker_r", "255", CVAR_NOARCHIVE);
	colorPicker.greenCvar = Cvar_Get ("ui_colorpicker_g", "255", CVAR_NOARCHIVE);
	colorPicker.blueCvar = Cvar_Get ("ui_colorpicker_b", "255", CVAR_NOARCHIVE);
	colorPicker.alphaCvar = Cvar_Get ("ui_colorpicker_a", "255", CVAR_NOARCHIVE);

	colorPicker.menu.name = "colorpicker";
	colorPicker.menu.title = colorPicker.title;
	colorPicker.menu.push = Menu_Push;
	colorPicker.menu.pop = ColorPicker_Pop;
	colorPicker.menu.draw = ColorPicker_Draw;
	colorPicker.menu.keydown = ColorPicker_Keydown;
	colorPicker.menu.color.u32 = MakeColor (0, 0, 0, 0);
	colorPicker.menu.compact = true;
	colorPicker.menu.transparent = true;
	colorPicker.menu.live = true;
	colorPicker.menu.halign = MENU_HALIGN_LEFT;

	ColorPicker_InitSlider (&colorPicker.red, colorPicker.redName, colorPicker.redCvar);
	ColorPicker_InitSlider (&colorPicker.green, colorPicker.greenName, colorPicker.greenCvar);
	ColorPicker_InitSlider (&colorPicker.blue, colorPicker.blueName, colorPicker.blueCvar);
	ColorPicker_InitSlider (&colorPicker.alpha, colorPicker.alphaName, colorPicker.alphaCvar);
}

void Menu_FreeColorPicker (void)
{
	if (!colorPicker.initialized)
	{
		return;
	}

	Z_Free (colorPicker.menu.items);
	memset (&colorPicker, 0, sizeof (colorPicker));
}

static void ColorPicker_Open (menuField_t *field)
{
	color_t color;
	const char *name = field->generic.name ? field->generic.name : field->cvar->name;
	const char *value = field->colorPickerOnly ? field->cvar->string : field->field.text;

	ColorPicker_Init ();

	colorPicker.target = field->cvar;
	colorPicker.source = field;
	colorPicker.originalValid = Field_ParseColor (value, &color);
	if (!colorPicker.originalValid)
	{
		colorPicker.originalValid = Field_ParseColor (field->cvar->string, &color);
	}
	if (!colorPicker.originalValid)
	{
		color.u32 = U32_WHITE;
		colorPicker.originalValid = true;
	}
	colorPicker.original = color;

	if (colorPicker.target &&
		(Q_strncasecmp (colorPicker.target->name, "sh_", 3) == 0 ||
			Q_strncasecmp (colorPicker.target->name, "ui_menu_color_", 14) == 0 ||
			Q_strncasecmp (colorPicker.target->name, "race_color", 10) == 0))
	{
		colorPicker.menu.halign = MENU_HALIGN_CENTER;
		colorPicker.menu.color.u32 = MakeColor (63, 109, 160, 112); // #3f6da070
	}
	else
	{
		colorPicker.menu.halign = MENU_HALIGN_LEFT;
		colorPicker.menu.color.u32 = MakeColor (0, 0, 0, 0); // default transparent
	}

	Q_snprintf (colorPicker.title, sizeof (colorPicker.title), "%s color", name);
	ColorPicker_SetFromColor (&color, false);
	ColorPicker_UpdateNames ();
	UI_PushMenu (&colorPicker.menu);
}

static menuSound_t Field_ColorActivate (menuCommon_t *item)
{
	menuField_t *field = (menuField_t *)item;

	if (!field->colorPreview)
	{
		return QMS_NOTHANDLED;
	}

	ColorPicker_Open (field);
	return QMS_IN;
}

/*
=================
Field_Init
=================
*/
static void Field_Init (menuField_t *f)
{
	int w = Field_TextInputWidth (f);
	int preview = Field_ColorPreviewWidth (f);

	f->generic.uiFlags &= ~(UI_CENTER | UI_LEFT | UI_RIGHT);
	if (f->colorPreview)
	{
		f->generic.activate = Field_ColorActivate;
	}

	if (f->generic.name)
	{
		f->generic.rect.x = f->generic.x + LCOLUMN_OFFSET;
		f->generic.rect.y = f->generic.y;
		UI_StringDimensions (&f->generic.rect,
			f->generic.uiFlags | UI_RIGHT, f->generic.name);
		f->generic.rect.width += (RCOLUMN_OFFSET - LCOLUMN_OFFSET) + w + preview;
	}
	else
	{
		f->generic.rect.x = f->generic.x - w / 2;
		f->generic.rect.y = f->generic.y;
		f->generic.rect.width = w;
		f->generic.rect.height = CHAR_HEIGHT;
	}
}

static bool Field_DrawFocusFill (const menuField_t *f)
{
	menuFrameWork_t *menu = f->generic.parent;

	return menu && !menu->compact &&
		(menu->focusStyleSet || UI_MenuStyleFocusFill ());
}

/*
=================
Field_Draw
=================
*/
static void Field_Draw (menuField_t *f)
{
	int flags = f->generic.uiFlags;
	int inputWidth = Field_TextInputWidth (f);
	uint32 color = uis.color.normal.u32;
	bool hasFocus = f->generic.flags & QMF_HASFOCUS;
	bool drawFocusFill = hasFocus && Field_DrawFocusFill (f);

	if (hasFocus)
	{
		flags |= UI_DRAWCURSOR;
		color = uis.color.active.u32;
		Menu_SetColor (color);
	}

	if (f->generic.name)
	{
		UI_DrawString (f->generic.x + LCOLUMN_OFFSET, f->generic.y,
			f->generic.uiFlags | UI_RIGHT | UI_ALTCOLOR, f->generic.name);

		if (!f->colorPickerOnly)
		{
			if (drawFocusFill)
			{
				R_DrawFill32 (f->generic.x + RCOLUMN_OFFSET, f->generic.y - 1,
					f->field.visibleChars * CHAR_WIDTH, CHAR_HEIGHT + 2, color);
			}

			IF_Draw (&f->field, f->generic.x + RCOLUMN_OFFSET, f->generic.y,
				flags, uis.fontHandle);
		}

		if (f->colorPreview)
		{
			Field_DrawColorPreview (
				f,
				f->generic.x + RCOLUMN_OFFSET + inputWidth +
					(inputWidth ? FIELD_COLOR_SWATCH_GAP : 0),
				f->generic.y - 1);
		}
	}
	else
	{
		if (drawFocusFill)
		{
			R_DrawFill32 (f->generic.rect.x, f->generic.rect.y - 1,
				f->generic.rect.width, CHAR_HEIGHT + 2, color);
		}

		IF_Draw (&f->field, f->generic.rect.x, f->generic.rect.y,
			flags, uis.fontHandle);
	}

	if (hasFocus)
	{
		Menu_SetNormalColor ();
	}
}

static bool Field_TestKey (menuField_t *f, int key)
{
	if (f->generic.flags & QMF_NUMBERSONLY)
	{
		return Q_isdigit (key) || key == '+' || key == '-' || key == '.';
	}

	return Q_isprint (key);
}

/*
=================
Field_Key
=================
*/
static int Field_Key (menuField_t *f, int key)
{
	if (f->colorPickerOnly)
	{
		return QMS_NOTHANDLED;
	}

	if (IF_KeyEvent (&f->field, key))
	{
		Menu_LiveCommit (&f->generic);
		return QMS_SILENT;
	}

	if (Field_TestKey (f, key))
	{
		return QMS_SILENT;
	}

	return QMS_NOTHANDLED;
}

/*
=================
Field_Char
=================
*/
static int Field_Char (menuField_t *f, int key)
{
	bool ret;

	if (f->colorPickerOnly)
	{
		return QMS_NOTHANDLED;
	}

	if (!Field_TestKey (f, key))
	{
		return QMS_BEEP;
	}

	ret = IF_CharEvent (&f->field, key);
	Menu_LiveCommit (&f->generic);
	if (f->generic.change)
	{
		f->generic.change (&f->generic);
	}

	return ret ? QMS_SILENT : QMS_NOTHANDLED;
}

/*
===================================================================

SPIN CONTROL

===================================================================
*/

static void SpinControl_Push (menuSpinControl_t *s)
{
	int val = s->cvar->intvalue;

	s->modified = false;

	if (val < 0 || val >= s->numItems)
		s->curvalue = -1;
	else
		s->curvalue = val;
}

static void SpinControl_Pop (menuSpinControl_t *s)
{
	if (s->curvalue >= 0 && s->curvalue < s->numItems)
		Cvar_SetInteger (s->cvar, s->curvalue, FROM_MENU);
}

static void SpinControl_Free (menuSpinControl_t *s)
{
	int i;

	Z_Free (s->generic.name);
	Z_Free (s->generic.status);
	Z_Free (s->cmd);
	for (i = 0; i < s->numItems; i++)
	{
		Z_Free (s->itemnames[i]);
		if (s->itemvalues)
		{
			Z_Free (s->itemvalues[i]);
		}
	}
	Z_Free (s->itemnames);
	if (s->itemvalues)
	{
		Z_Free (s->itemvalues);
	}
	Z_Free (s);
}

static int SpinControl_ValueOffset (const menuSpinControl_t *s)
{
	int width = 11;
	char **name;

	for (name = s->itemnames; *name; name++)
	{
		int length = strlen (*name);
		if (width < length)
			width = length;
	}

	return (width + 2) * CHAR_WIDTH;
}

/*
=================
SpinControl_Init
=================
*/
void SpinControl_Init (menuSpinControl_t *s)
{
	int maxLength, length;
	char **n;

	s->generic.uiFlags &= ~(UI_CENTER | UI_LEFT | UI_RIGHT);

	s->generic.rect.x = s->generic.x + LCOLUMN_OFFSET;
	s->generic.rect.y = s->generic.y;

	UI_StringDimensions (&s->generic.rect,
		s->generic.uiFlags | UI_RIGHT, s->generic.name);

	maxLength = 0;
	s->numItems = 0;
	n = s->itemnames;
	while (*n)
	{
		length = strlen (*n);

		if (maxLength < length)
		{
			maxLength = length;
		}
		s->numItems++;
		n++;
	}

	if (s->generic.flags & QMF_SHOW_VALUE)
	{
		int maxValLen = 0;
		if (s->itemvalues)
		{
			char **v = s->itemvalues;
			while (*v)
			{
				int vlen = strlen (*v);
				if (maxValLen < vlen)
				{
					maxValLen = vlen;
				}
				v++;
			}
		}
		else
		{
			char raw[32];
			Q_snprintf (raw, sizeof (raw), "%d", s->numItems - 1);
			maxValLen = strlen (raw);
		}
		if (s->cvar && maxValLen < (int)strlen (s->cvar->string))
			maxValLen = strlen (s->cvar->string);
		s->generic.rect.width += (RCOLUMN_OFFSET - LCOLUMN_OFFSET) +
			SpinControl_ValueOffset (s) + (maxValLen + 2) * CHAR_WIDTH;
	}
	else
	{
		s->generic.rect.width += (RCOLUMN_OFFSET - LCOLUMN_OFFSET) +
			maxLength * CHAR_WIDTH;
	}
}

/*
=================
SpinControl_DoEnter
=================
*/
static int SpinControl_DoEnter (menuSpinControl_t *s)
{
	if (!s->numItems)
		return QMS_BEEP;

	s->modified = true;
	s->curvalue++;

	if (s->curvalue >= s->numItems)
		s->curvalue = 0;

	if (s->generic.change)
	{
		s->generic.change (&s->generic);
	}

	Menu_LiveCommit (&s->generic);

	return QMS_MOVE;
}

/*
=================
SpinControl_DoSlide
=================
*/
static int SpinControl_DoSlide (menuSpinControl_t *s, int dir)
{
	if (!s->numItems)
		return QMS_BEEP;

	s->modified = true;
	s->curvalue += dir;

	if (s->curvalue < 0)
	{
		s->curvalue = s->numItems - 1;
	}
	else if (s->curvalue >= s->numItems)
	{
		s->curvalue = 0;
	}

	if (s->generic.change)
	{
		s->generic.change (&s->generic);
	}

	Menu_LiveCommit (&s->generic);

	return QMS_MOVE;
}

/*
=================
SpinControl_Draw
=================
*/
static void SpinControl_Draw (menuSpinControl_t *s)
{
	const char *name;
	char value[MAX_QPATH];
	int flags = s->generic.uiFlags | UI_RIGHT | UI_ALTCOLOR;

	if (s->generic.flags & QMF_HASFOCUS)
	{
		Menu_SetColor (uis.color.active.u32);
	}

	UI_DrawString (s->generic.x + LCOLUMN_OFFSET, s->generic.y,
		flags, s->generic.name);

	if (s->generic.flags & QMF_HASFOCUS)
	{
		Menu_SetColor (uis.color.active.u32);
	}

	if (s->curvalue < 0 || s->curvalue >= s->numItems)
		name = "???";
	else
		name = s->itemnames[s->curvalue];

	if (UI_MenuStyleId () == UI_MENU_STYLE_CUSTOM)
	{
		if (s->generic.flags & QMF_HASFOCUS)
		{
			Menu_SetColor (UI_MenuValueActiveColor ());
		}
		else
		{
			Menu_SetColor (UI_MenuValueColor ());
		}
	}


	UI_DrawString (s->generic.x + RCOLUMN_OFFSET, s->generic.y,
		s->generic.uiFlags, name);

	if (s->generic.flags & QMF_SHOW_VALUE)
	{
		char raw[MAX_QPATH];
		const char *val = NULL;

		if (s->generic.type == MTYPE_PAIRS &&
			s->curvalue >= 0 && s->curvalue < s->numItems)
		{
			val = s->itemvalues[s->curvalue];
		}
		else if ((s->generic.type == MTYPE_TOGGLE ||
					 s->generic.type == MTYPE_BITFIELD) &&
			s->curvalue >= 0)
		{
			Q_snprintf (raw, sizeof (raw), "%d",
				s->generic.type == MTYPE_TOGGLE ?
					(s->curvalue ^ s->negate) :
					s->cvar->intvalue);
			val = raw;
		}
		else if (s->cvar)
		{
			val = s->cvar->string;
		}

		if (val)
		{
			Q_snprintf (value, sizeof (value), "[%s]", val);

			UI_DrawString (s->generic.x + RCOLUMN_OFFSET + SpinControl_ValueOffset (s),
				s->generic.y, s->generic.uiFlags, value);
		}
	}

	if (s->generic.flags & QMF_HASFOCUS || UI_MenuStyleId () == UI_MENU_STYLE_CUSTOM)
	{
		Menu_SetNormalColor ();
	}
}

/*
===================================================================

BITFIELD CONTROL

===================================================================
*/

static void BitField_Push (menuSpinControl_t *s)
{
	if (s->cvar->intvalue & s->mask)
	{
		s->curvalue = 1 ^ s->negate;
	}
	else
	{
		s->curvalue = 0 ^ s->negate;
	}
}

static void BitField_Pop (menuSpinControl_t *s)
{
	int val = s->cvar->intvalue;

	if (s->curvalue ^ s->negate)
	{
		val |= s->mask;
	}
	else
	{
		val &= ~s->mask;
	}
	Cvar_SetInteger (s->cvar, val, FROM_MENU);
}

static void BitField_Free (menuSpinControl_t *s)
{
	Z_Free (s->generic.name);
	Z_Free (s->generic.status);
	Z_Free (s);
}

/*
===================================================================

PAIRS CONTROL

===================================================================
*/

static void Pairs_Push (menuSpinControl_t *s)
{
	int i;

	s->modified = false;

	for (i = 0; i < s->numItems; i++)
	{
		if (!Q_stricmp (s->itemvalues[i], s->cvar->string))
		{
			s->curvalue = i;
			return;
		}
	}

	s->curvalue = -1;
}

static void Pairs_Pop (menuSpinControl_t *s)
{
	if (s->curvalue >= 0 && s->curvalue < s->numItems)
		Cvar_SetByVar (s->cvar, s->itemvalues[s->curvalue], FROM_MENU);
}

static void Pairs_Free (menuSpinControl_t *s)
{
	int i;

	Z_Free (s->generic.name);
	Z_Free (s->generic.status);
	for (i = 0; i < s->numItems; i++)
	{
		Z_Free (s->itemnames[i]);
		Z_Free (s->itemvalues[i]);
	}
	Z_Free (s->itemnames);
	Z_Free (s->itemvalues);
	Z_Free (s);
}

/*
===================================================================

STRINGS CONTROL

===================================================================
*/

static void Strings_Push (menuSpinControl_t *s)
{
	int i;

	s->modified = false;

	for (i = 0; i < s->numItems; i++)
	{
		if (!Q_stricmp (s->itemnames[i], s->cvar->string))
		{
			s->curvalue = i;
			return;
		}
	}

	s->curvalue = -1;
}

static void Strings_Pop (menuSpinControl_t *s)
{
	if (s->curvalue >= 0 && s->curvalue < s->numItems)
		Cvar_SetByVar (s->cvar, s->itemnames[s->curvalue], FROM_MENU);
}

static void ActionSelect_Pop (menuSpinControl_t *s)
{
	char escaped[MAX_STRING_CHARS];

	if (!s->modified || s->curvalue < 0 || s->curvalue >= s->numItems)
	{
		return;
	}

	Cvar_SetByVar (s->cvar, s->itemnames[s->curvalue], FROM_MENU);
	Com_EscapeString (escaped, s->itemnames[s->curvalue], sizeof (escaped));
	Cbuf_AddText (va ("%s \"%s\"\n", s->cmd, escaped));
	s->modified = false;
}

/*
===================================================================

TOGGLE CONTROL

===================================================================
*/

static void Toggle_Push (menuSpinControl_t *s)
{
	int val = s->cvar->intvalue;

	s->modified = false;

	if (val == 0 || val == 1)
		s->curvalue = val ^ s->negate;
	else
		s->curvalue = -1;
}

static void Toggle_Pop (menuSpinControl_t *s)
{
	if (s->curvalue == 0 || s->curvalue == 1)
		Cvar_SetInteger (s->cvar, s->curvalue ^ s->negate, FROM_MENU);
}

static void Menu_CommitItem (menuCommon_t *item)
{
	switch (item->type)
	{
	case MTYPE_SLIDER:
		Slider_Pop ((menuSlider_t *)item);
		break;
	case MTYPE_BITFIELD:
		BitField_Pop ((menuSpinControl_t *)item);
		break;
	case MTYPE_PAIRS:
		Pairs_Pop ((menuSpinControl_t *)item);
		break;
	case MTYPE_VALUES:
	case MTYPE_STRINGS:
		Strings_Pop ((menuSpinControl_t *)item);
		break;
	case MTYPE_ACTION_SELECT:
		ActionSelect_Pop ((menuSpinControl_t *)item);
		break;
	case MTYPE_SPINCONTROL:
		SpinControl_Pop ((menuSpinControl_t *)item);
		break;
	case MTYPE_TOGGLE:
		Toggle_Pop ((menuSpinControl_t *)item);
		break;
	case MTYPE_FIELD:
		Field_Pop ((menuField_t *)item);
		break;
	case MTYPE_SELECT:
		Select_Pop ((menuSelect_t *)item);
		break;
	default:
		break;
	}
}

static void Menu_LiveCommit (menuCommon_t *item)
{
	if (item->parent && item->parent->live &&
		!(item->flags & QMF_DEFER_COMMIT))
	{
		Menu_CommitItem (item);
	}
}

/*
===================================================================

LIST CONTROL

===================================================================
*/

/*
=================
MenuList_ValidatePrestep
=================
*/
static void MenuList_ValidatePrestep (menuList_t *l)
{
	if (l->prestep > l->numItems - l->maxItems)
	{
		l->prestep = l->numItems - l->maxItems;
	}
	if (l->prestep < 0)
	{
		l->prestep = 0;
	}
}

static void MenuList_AdjustPrestep (menuList_t *l)
{
	if (l->numItems > l->maxItems && l->curvalue > 0)
	{
		if (l->prestep > l->curvalue)
		{
			l->prestep = l->curvalue;
		}
		else if (l->prestep < l->curvalue - l->maxItems + 1)
		{
			l->prestep = l->curvalue - l->maxItems + 1;
		}
	}
	else
	{
		l->prestep = 0;
	}
}

/*
=================
MenuList_Init
=================
*/
void MenuList_Init (menuList_t *l)
{
	int height;
	int i;

	height = l->generic.height;
	if (l->mlFlags & MLF_HEADER)
	{
		height -= MLIST_SPACING;
	}

	l->maxItems = height / MLIST_SPACING;

	//clamp(l->curvalue, 0, l->numItems - 1);

	MenuList_ValidatePrestep (l);

	l->generic.rect.x = l->generic.x;
	l->generic.rect.y = l->generic.y;

	l->generic.rect.width = 0;
	for (i = 0; i < l->numcolumns; i++)
	{
		l->generic.rect.width += l->columns[i].width;
	}

	if (l->mlFlags & MLF_SCROLLBAR)
	{
		l->generic.rect.width += MLIST_SCROLLBAR_WIDTH;
	}

	l->generic.rect.height = l->generic.height;

	if (l->sortdir && l->sort)
	{
		l->sort (l);
	}
}

/*
=================
MenuList_SetValue
=================
*/
void MenuList_SetValue (menuList_t *l, int value)
{
	if (value > l->numItems - 1)
		value = l->numItems - 1;
	if (value < 0)
		value = 0;

	if (value != l->curvalue)
	{
		l->curvalue = value;
		if (l->generic.change)
		{
			l->generic.change (&l->generic);
		}
	}

	MenuList_AdjustPrestep (l);
}

static menuSound_t MenuList_SetColumn (menuList_t *l, int value)
{
	if (l->sortcol == value)
	{
		l->sortdir = -l->sortdir;
	}
	else
	{
		l->sortcol = value;
		l->sortdir = 1;
	}
	if (l->sort)
	{
		l->sort (l);
		MenuList_AdjustPrestep (l);
	}
	return QMS_SILENT;
}

// finds a visible column by number, with numeration starting at 1
static menuSound_t MenuList_FindColumn (menuList_t *l, int rel)
{
	int i, j;

	if (!l->sortdir)
		return QMS_NOTHANDLED;

	for (i = 0, j = 0; i < l->numcolumns; i++)
	{
		if (!l->columns[i].width)
			continue;

		if (++j == rel)
			return MenuList_SetColumn (l, i);
	}

	return QMS_NOTHANDLED;
}

static menuSound_t MenuList_PrevColumn (menuList_t *l)
{
	int col;

	if (!l->sortdir || !l->numcolumns)
	{
		return QMS_NOTHANDLED;
	}

	col = l->sortcol;
	if (col < 0)
		return MenuList_FindColumn (l, 1);

	do
	{
		if (col == 0)
		{
			col = l->numcolumns - 1;
		}
		else
		{
			col--;
		}
		if (col == l->sortcol)
		{
			return QMS_SILENT;
		}
	} while (!l->columns[col].width);

	return MenuList_SetColumn (l, col);
}

static menuSound_t MenuList_NextColumn (menuList_t *l)
{
	int col;

	if (!l->sortdir || !l->numcolumns)
	{
		return QMS_NOTHANDLED;
	}

	col = l->sortcol;
	if (col < 0)
		return MenuList_FindColumn (l, 1);

	do
	{
		if (col == l->numcolumns - 1)
		{
			col = 0;
		}
		else
		{
			col++;
		}
		if (col == l->sortcol)
		{
			return QMS_SILENT;
		}
	} while (!l->columns[col].width);

	return MenuList_SetColumn (l, col);
}

/*
=================
MenuList_Click
=================
*/
static menuSound_t MenuList_Click (menuList_t *l)
{
	int i, j;
	vrect_t rect;

	if (!l->items)
	{
		return QMS_SILENT;
	}

	// click on scroll bar
	if ((l->mlFlags & MLF_SCROLLBAR) && l->numItems > l->maxItems)
	{
		int x = l->generic.rect.x + l->generic.rect.width - MLIST_SCROLLBAR_WIDTH;
		int y = l->generic.rect.y + MLIST_SPACING;
		int h = l->generic.height;
		int barHeight, pageHeight, prestepHeight;
		float pageFrac, prestepFrac;

		if (l->mlFlags & MLF_HEADER)
		{
			y += MLIST_SPACING;
			h -= MLIST_SPACING;
		}

		barHeight = h - MLIST_SPACING * 2;
		pageFrac = (float)l->maxItems / l->numItems;
		prestepFrac = (float)l->prestep / l->numItems;

		pageHeight = Q_rint (barHeight * pageFrac);
		prestepHeight = Q_rint (barHeight * prestepFrac);

		// click above thumb
		rect.x = x;
		rect.y = y;
		rect.width = MLIST_SCROLLBAR_WIDTH;
		rect.height = prestepHeight;
		if (UI_CursorInRect (&rect))
		{
			l->prestep -= l->maxItems;
			MenuList_ValidatePrestep (l);
			return QMS_MOVE;
		}

		// click on thumb
		rect.y = y + prestepHeight;
		rect.height = pageHeight;
		if (UI_CursorInRect (&rect))
		{
			l->drag_y = uis.mouseCoords[1] - rect.y;
			uis.mouseTracker = &l->generic;
			return QMS_SILENT;
		}

		// click below thumb
		rect.y = y + prestepHeight + pageHeight;
		rect.height = barHeight - prestepHeight - pageHeight;
		if (UI_CursorInRect (&rect))
		{
			l->prestep += l->maxItems;
			MenuList_ValidatePrestep (l);
			return QMS_MOVE;
		}

		// click above scrollbar
		rect.y = y - MLIST_SPACING;
		rect.height = MLIST_SPACING;
		if (UI_CursorInRect (&rect))
		{
			l->prestep--;
			MenuList_ValidatePrestep (l);
			return QMS_MOVE;
		}

		// click below scrollbar
		rect.y = l->generic.rect.y + l->generic.height - MLIST_SPACING;
		rect.height = MLIST_SPACING;
		if (UI_CursorInRect (&rect))
		{
			l->prestep++;
			MenuList_ValidatePrestep (l);
			return QMS_MOVE;
		}
	}

	rect.x = l->generic.rect.x;
	rect.y = l->generic.rect.y;
	rect.width = l->generic.rect.width;
	rect.height = MLIST_SPACING;

	if (l->mlFlags & MLF_SCROLLBAR)
	{
		rect.width -= MLIST_SCROLLBAR_WIDTH;
	}

	// click on header
	if (l->mlFlags & MLF_HEADER)
	{
		if (l->sortdir && UI_CursorInRect (&rect))
		{
			for (j = 0; j < l->numcolumns; j++)
			{
				if (!l->columns[j].width)
				{
					continue;
				}
				rect.width = l->columns[j].width;
				if (UI_CursorInRect (&rect))
				{
					return MenuList_SetColumn (l, j);
				}
				rect.x += rect.width;
			}
			return QMS_SILENT;
		}
		rect.y += MLIST_SPACING;
	}

	// click on item
	j = min (l->numItems, l->prestep + l->maxItems);
	for (i = l->prestep; i < j; i++)
	{
		if (UI_CursorInRect (&rect))
		{
			if (l->curvalue == i && uis.realtime - l->clickTime < DOUBLE_CLICK_DELAY)
			{
				if (l->generic.activate)
				{
					return l->generic.activate (&l->generic);
				}
				return QMS_SILENT;
			}
			l->clickTime = uis.realtime;
			l->curvalue = i;
			if (l->generic.change)
			{
				return l->generic.change (&l->generic);
			}
			return QMS_SILENT;
		}
		rect.y += MLIST_SPACING;
	}

	return QMS_SILENT;
}

/*
=================
MenuList_Key
=================
*/
static menuSound_t MenuList_Key (menuList_t *l, int key)
{
	//int i;

	if (!l->items)
	{
		return QMS_NOTHANDLED;
	}

	if (Key_IsDown (K_ALT) && Q_isdigit (key))
	{
		return MenuList_FindColumn (l, key - '0');
	}


	switch (key)
	{
	case K_LEFTARROW:
	case 'h':
		return MenuList_PrevColumn (l);

	case K_RIGHTARROW:
	case 'l':
		return MenuList_NextColumn (l);

	case K_UPARROW:
	case K_KP_UPARROW:
	case 'k':
		if (l->curvalue < 0)
		{
			goto home;
		}
		if (l->curvalue > 0)
		{
			l->curvalue--;
			if (l->generic.change)
			{
				l->generic.change (&l->generic);
			}
			MenuList_AdjustPrestep (l);
			return QMS_MOVE;
		}
		return QMS_BEEP;

	case K_DOWNARROW:
	case K_KP_DOWNARROW:
	case 'j':
		if (l->curvalue < 0)
		{
			goto home;
		}
		if (l->curvalue < l->numItems - 1)
		{
			l->curvalue++;
			if (l->generic.change)
			{
				l->generic.change (&l->generic);
			}
			MenuList_AdjustPrestep (l);
			return QMS_MOVE;
		}
		return QMS_BEEP;

	case K_HOME:
	case K_KP_HOME:
	home:
		l->prestep = 0;
		l->curvalue = 0;
		if (l->generic.change)
		{
			l->generic.change (&l->generic);
		}
		return QMS_MOVE;

	case K_END:
	case K_KP_END:
		if (!l->numItems)
		{
			goto home;
		}
		if (l->numItems > l->maxItems)
		{
			l->prestep = l->numItems - l->maxItems;
		}
		l->curvalue = l->numItems - 1;
		if (l->generic.change)
		{
			l->generic.change (&l->generic);
		}
		return QMS_MOVE;

	case K_MWHEELUP:
		if (Key_IsDown (K_CTRL))
		{
			l->prestep -= 4;
		}
		else
		{
			l->prestep -= 2;
		}
		MenuList_ValidatePrestep (l);
		return QMS_SILENT;

	case K_MWHEELDOWN:
		if (Key_IsDown (K_CTRL))
		{
			l->prestep += 4;
		}
		else
		{
			l->prestep += 2;
		}
		MenuList_ValidatePrestep (l);
		return QMS_SILENT;

	case K_PGUP:
	case K_KP_PGUP:
		if (l->curvalue < 0)
		{
			goto home;
		}
		if (l->curvalue > 0)
		{
			l->curvalue -= l->maxItems - 1;
			if (l->curvalue < 0)
			{
				l->curvalue = 0;
			}
			if (l->generic.change)
			{
				l->generic.change (&l->generic);
			}
			MenuList_AdjustPrestep (l);
			return QMS_MOVE;
		}
		return QMS_BEEP;

	case K_PGDN:
	case K_KP_PGDN:
		if (l->curvalue < 0)
		{
			goto home;
		}
		if (l->curvalue < l->numItems - 1)
		{
			l->curvalue += l->maxItems - 1;
			if (l->curvalue > l->numItems - 1)
			{
				l->curvalue = l->numItems - 1;
			}
			if (l->generic.change)
			{
				l->generic.change (&l->generic);
			}
			MenuList_AdjustPrestep (l);
			return QMS_MOVE;
		}
		return QMS_BEEP;

	case K_MOUSE1:
		//case K_MOUSE2:
		//case K_MOUSE3:
		return MenuList_Click (l);
	}

	return QMS_NOTHANDLED;
}

static menuSound_t MenuList_MouseMove (menuList_t *l)
{
	int y, h, barHeight;

	if (uis.mouseTracker != &l->generic)
		return QMS_NOTHANDLED;

	y = l->generic.y + MLIST_SPACING;
	h = l->generic.height;

	if (l->mlFlags & MLF_HEADER)
	{
		y += MLIST_SPACING;
		h -= MLIST_SPACING;
	}

	barHeight = h - MLIST_SPACING * 2;
	if (barHeight > 0)
	{
		l->prestep = (uis.mouseCoords[1] - y - l->drag_y) * l->numItems / barHeight;
		MenuList_ValidatePrestep (l);
	}

	return QMS_SILENT;
}

/*
=================
MenuList_DrawString
=================
*/
static void MenuList_DrawString (int x, int y, int flags,
	menuListColumn_t *column,
	const char *string)
{
	clipRect_t rc;

	rc.left = x;
	rc.right = x + column->width - 1;
	rc.top = y + 1;
	rc.bottom = y + CHAR_HEIGHT + 1;

	if ((column->uiFlags & UI_CENTER) == UI_CENTER)
	{
		x += column->width / 2 - 1;
	}
	else if (column->uiFlags & UI_RIGHT)
	{
		x += column->width - MLIST_PRESTEP;
	}
	else
	{
		x += MLIST_PRESTEP;
	}

	R_SetClipRect (&rc);
	UI_DrawString (x, y + 1, column->uiFlags | flags, string);
	R_SetClipRect (NULL);
}

static uint32 MenuList_HeaderColor (void)
{
	return UI_MenuListHeaderColor ();
}

static uint32 MenuList_ScrollbarColor (void)
{
	return UI_MenuScrollbarColor ();
}

/*
=================
MenuList_Draw
=================
*/
static void MenuList_Draw (menuList_t *l)
{
	char *s;
	int x, y, xx, yy;
	int i, j, k;
	int width, height;
	float pageFrac, prestepFrac;
	int barHeight;
	int orig_x, orig_y, orig_width, orig_height;

	x = l->generic.rect.x;
	y = l->generic.rect.y;
	width = l->generic.rect.width;
	height = l->generic.rect.height;

	orig_x = x;
	orig_y = y;
	orig_width = width;
	orig_height = height;

	uint32 shadow_color = UI_MenuPanelShadowColor ();
	if (shadow_color & 0xff000000)
	{
		R_DrawFill32 (orig_x + 3, orig_y + 3, orig_width, orig_height, shadow_color);
	}

	// draw header
	if (l->mlFlags & MLF_HEADER)
	{
		xx = x;
		for (j = 0; j < l->numcolumns; j++)
		{
			int flags = UI_ALTCOLOR;
			uint32 color = MenuList_HeaderColor ();

			if (!l->columns[j].width)
			{
				continue;
			}

			if (l->sortcol == j && l->sortdir)
			{
				flags = 0;
				if (UI_MenuStyleId () == UI_MENU_STYLE_CUSTOM)
				{
					color = UI_MenuSortedHeaderColor ();
				}
				else if (l->generic.flags & QMF_HASFOCUS)
				{
					color = uis.color.focus.u32;
					Menu_SetColor (uis.color.active.u32);
				}
			}
			R_DrawFill32 (xx, y, l->columns[j].width - 1,
				MLIST_SPACING - 1, color);

			if (l->columns[j].name)
			{
				MenuList_DrawString (xx, y, flags,
					&l->columns[j], l->columns[j].name);
			}

			if (l->generic.flags & QMF_HASFOCUS)
			{
				Menu_SetNormalColor ();
			}
			xx += l->columns[j].width;
		}
		y += MLIST_SPACING;
		height -= MLIST_SPACING;
	}

	if (l->mlFlags & MLF_SCROLLBAR)
	{
		barHeight = height - MLIST_SPACING * 2;
		yy = y + MLIST_SPACING;

		// draw scrollbar background
		R_DrawFill32 (x + width - MLIST_SCROLLBAR_WIDTH, yy,
			MLIST_SCROLLBAR_WIDTH - 1, barHeight,
			MenuList_ScrollbarColor ());

		if (l->numItems > l->maxItems)
		{
			pageFrac = (float)l->maxItems / l->numItems;
			prestepFrac = (float)l->prestep / l->numItems;
		}
		else
		{
			pageFrac = 1;
			prestepFrac = 0;
		}

		// draw scrollbar thumb
		R_DrawFill32 (x + width - MLIST_SCROLLBAR_WIDTH,
			yy + Q_rint (barHeight * prestepFrac),
			MLIST_SCROLLBAR_WIDTH - 1,
			Q_rint (barHeight * pageFrac),
			uis.color.selection.u32);
	}

	// draw background
	xx = x;
	for (j = 0; j < l->numcolumns; j++)
	{
		uint32 color = uis.color.background.u32;

		if (!l->columns[j].width)
		{
			continue;
		}


		R_DrawFill32 (xx, y, l->columns[j].width - 1,
			height, color);

		xx += l->columns[j].width;
	}

	yy = y;
	k = min (l->numItems, l->prestep + l->maxItems);
	for (i = l->prestep; i < k; i++)
	{
		// draw selection
		if (!(l->generic.flags & QMF_DISABLED) && i == l->curvalue)
		{
			if (l->generic.flags & QMF_HASFOCUS)
			{
				Menu_SetColor (uis.color.active.u32);
			}
			xx = x;
			for (j = 0; j < l->numcolumns; j++)
			{
				if (!l->columns[j].width)
				{
					continue;
				}
				R_DrawFill32 (xx, yy, l->columns[j].width - 1,
					MLIST_SPACING,
					(l->generic.flags & QMF_HASFOCUS) ? uis.color.selection.u32 : uis.color.focus.u32);
				xx += l->columns[j].width;
			}
		}

		// draw contents
		s = (char *)l->items[i] + l->extrasize;
		if (l->mlFlags & MLF_COLOR)
		{
			Menu_SetColor (*((uint32 *)(s - 4)));
		}

		xx = x;
		for (j = 0; j < l->numcolumns; j++)
		{
			if (!*s)
			{
				break;
			}

			if (l->columns[j].width)
			{
				MenuList_DrawString (xx, yy, 0, &l->columns[j], s);
				xx += l->columns[j].width;
			}
			s += strlen (s) + 1;
		}

		if (!(l->generic.flags & QMF_DISABLED) && i == l->curvalue)
		{
			if (l->generic.flags & QMF_HASFOCUS)
			{
				Menu_SetNormalColor ();
			}
		}

		yy += MLIST_SPACING;
	}

	if (l->mlFlags & MLF_COLOR)
	{
		Menu_SetNormalColor ();
	}

	uint32 border_color = UI_MenuPanelBorderColor ();
	if (border_color & 0xff000000)
	{
		R_DrawFill32 (orig_x, orig_y, orig_width, 1, border_color);					  // Top
		R_DrawFill32 (orig_x, orig_y + orig_height - 1, orig_width, 1, border_color); // Bottom
		R_DrawFill32 (orig_x, orig_y, 1, orig_height, border_color);				  // Left
		R_DrawFill32 (orig_x + orig_width - 1, orig_y, 1, orig_height, border_color); // Right
	}
}

void MenuList_Sort (menuList_t *l, int offset, int (*cmpfunc) (const void *, const void *))
{
	void *n;
	int i;

	if (!l->items)
		return;

	if (offset >= l->numItems)
		return;

	if (l->sortcol < 0 || l->sortcol >= l->numcolumns)
		return;

	if (l->curvalue < 0 || l->curvalue >= l->numItems)
		n = NULL;
	else
		n = l->items[l->curvalue];

	qsort (l->items + offset, l->numItems - offset, sizeof (char *), cmpfunc);

	for (i = 0; i < l->numItems; i++)
	{
		if (l->items[i] == n)
		{
			l->curvalue = i;
			break;
		}
	}
}

/*
===================================================================

SLIDER CONTROL

===================================================================
*/

static menuSound_t Slider_DoSlide (menuSlider_t *s, int dir);

static void Slider_Push (menuSlider_t *s)
{
	s->modified = false;
	s->curvalue = Q_circ_clipf (s->cvar->value, s->minvalue, s->maxvalue);
}

static void Slider_Pop (menuSlider_t *s)
{
	if (s->modified)
	{
		float val = Q_circ_clipf (s->curvalue, s->minvalue, s->maxvalue);
		Cvar_SetValue (s->cvar, val, FROM_MENU);
	}
}

static void Slider_Free (menuSlider_t *s)
{
	Z_Free (s->generic.name);
	Z_Free (s->generic.status);
	Z_Free (s);
}

static void Slider_ValueString (menuSlider_t *s, float value, char *buffer, size_t size);

static void Slider_Init (menuSlider_t *s)
{
	int len = strlen (s->generic.name) * CHAR_WIDTH;

	s->generic.rect.x = s->generic.x + LCOLUMN_OFFSET - len;
	s->generic.rect.y = s->generic.y;

	s->generic.rect.width = (RCOLUMN_OFFSET - LCOLUMN_OFFSET) + len;
	if (s->generic.flags & QMF_SHOW_VALUE)
	{
		char val_str1[32], val_str2[32];
		int val_len1, val_len2, val_len;
		Slider_ValueString (s, s->minvalue, val_str1, sizeof (val_str1));
		Slider_ValueString (s, s->maxvalue, val_str2, sizeof (val_str2));
		val_len1 = strlen (val_str1);
		val_len2 = strlen (val_str2);
		val_len = val_len1 > val_len2 ? val_len1 : val_len2;
		int extra = 13 * CHAR_WIDTH + val_len * CHAR_WIDTH;
		if (s->cvar &&
			(strncmp (s->cvar->name, "sh_histogram_color_", 19) == 0 ||
				strncmp (s->cvar->name, "sh_lagometer_color_", 19) == 0 ||
				strncmp (s->cvar->name, "sh_netgraph_color_", 18) == 0))
		{
			extra = 20 * CHAR_WIDTH;
		}
		s->generic.rect.width += extra;
	}
	else
	{
		s->generic.rect.width += (SLIDER_RANGE + 2) * CHAR_WIDTH;
	}
	s->generic.rect.height = CHAR_HEIGHT;
}

static menuSound_t Slider_Click (menuSlider_t *s)
{
	vrect_t rect;
	float pos;
	int x;

	pos = Q_clipf ((s->curvalue - s->minvalue) / (s->maxvalue - s->minvalue), 0, 1);

	x = CHAR_WIDTH + (SLIDER_RANGE - 1) * CHAR_WIDTH * pos;

	// click left of thumb
	rect.x = s->generic.x + RCOLUMN_OFFSET;
	rect.y = s->generic.y;
	rect.width = x;
	rect.height = CHAR_HEIGHT;
	if (UI_CursorInRect (&rect))
		return Slider_DoSlide (s, -1);

	// click on thumb
	rect.x = s->generic.x + RCOLUMN_OFFSET + x;
	rect.y = s->generic.y;
	rect.width = CHAR_WIDTH;
	rect.height = CHAR_HEIGHT;
	if (UI_CursorInRect (&rect))
	{
		uis.mouseTracker = &s->generic;
		return QMS_SILENT;
	}

	// click right of thumb
	rect.x = s->generic.x + RCOLUMN_OFFSET + x + CHAR_WIDTH;
	rect.y = s->generic.y;
	rect.width = (SLIDER_RANGE + 1) * CHAR_WIDTH - x;
	rect.height = CHAR_HEIGHT;
	if (UI_CursorInRect (&rect))
		return Slider_DoSlide (s, 1);

	return QMS_SILENT;
}

static menuSound_t Slider_MouseMove (menuSlider_t *s)
{
	float pos, value;
	int steps;

	if (uis.mouseTracker != &s->generic)
		return QMS_NOTHANDLED;

	pos = (uis.mouseCoords[0] - (s->generic.x + RCOLUMN_OFFSET + CHAR_WIDTH)) * (1.0f / (SLIDER_RANGE * CHAR_WIDTH));

	value = Q_clipf (pos, 0, 1) * (s->maxvalue - s->minvalue);
	steps = Q_rint (value / s->step);

	s->modified = true;
	s->curvalue = s->minvalue + steps * s->step;
	Menu_LiveCommit (&s->generic);
	return QMS_SILENT;
}

static menuSound_t Slider_Key (menuSlider_t *s, int key)
{
	switch (key)
	{
	case K_END:
		s->modified = true;
		s->curvalue = s->maxvalue;
		Menu_LiveCommit (&s->generic);
		return QMS_MOVE;
	case K_HOME:
		s->modified = true;
		s->curvalue = s->minvalue;
		Menu_LiveCommit (&s->generic);
		return QMS_MOVE;
	case K_MOUSE1:
		return Slider_Click (s);
	}

	return QMS_NOTHANDLED;
}

/*
=================
Slider_DoSlide
=================
*/
static menuSound_t Slider_DoSlide (menuSlider_t *s, int dir)
{
	s->modified = true;
	s->curvalue = Q_circ_clipf (s->curvalue + dir * s->step, s->minvalue, s->maxvalue);

	if (s->generic.change)
	{
		menuSound_t sound = s->generic.change (&s->generic);
		if (sound != QMS_NOTHANDLED)
		{
			Menu_LiveCommit (&s->generic);
			return sound;
		}
	}

	Menu_LiveCommit (&s->generic);

	return QMS_SILENT;
}

static void Slider_ValueString (menuSlider_t *s, float value, char *buffer, size_t size)
{
	char *p;

	Q_snprintf (buffer, size, "%.2f", value);
	p = strchr (buffer, 0);
	while (p > buffer && p[-1] == '0')
	{
		*--p = 0;
	}
	if (p > buffer && p[-1] == '.')
	{
		*--p = 0;
	}
}

/*
=================
Slider_Draw
=================
*/
static void Slider_Draw (menuSlider_t *s)
{
	int i, flags;
	float pos;
	char value[32];

	flags = s->generic.uiFlags & ~(UI_CENTER | UI_LEFT | UI_RIGHT);

	if (s->generic.flags & QMF_HASFOCUS)
	{
		Menu_SetColor (uis.color.active.u32);
	}

	UI_DrawString (s->generic.x + LCOLUMN_OFFSET, s->generic.y,
		flags | UI_RIGHT | UI_ALTCOLOR, s->generic.name);

	if (s->generic.flags & QMF_HASFOCUS)
	{
		Menu_SetNormalColor ();
	}

	pos = Q_clipf ((s->curvalue - s->minvalue) / (s->maxvalue - s->minvalue), 0, 1);

	if (UI_MenuStyleId () == UI_MENU_STYLE_CUSTOM)
	{
		int track_x = s->generic.x + RCOLUMN_OFFSET;
		int track_w = (SLIDER_RANGE + 2) * CHAR_WIDTH;
		int track_h = 4;
		int track_y = s->generic.y + CHAR_HEIGHT / 2 - track_h / 2;

		int thumb_x = s->generic.x + RCOLUMN_OFFSET + CHAR_WIDTH + (SLIDER_RANGE - 1) * CHAR_WIDTH * pos;
		int thumb_w = CHAR_WIDTH;
		int thumb_h = CHAR_HEIGHT;
		int thumb_y = s->generic.y;

		int fill_w = (thumb_x + thumb_w / 2) - track_x;

		R_DrawFill32 (track_x, track_y, track_w, track_h, UI_MenuSliderTrackColor ());

		if (fill_w > 0)
		{
			R_DrawFill32 (track_x, track_y, fill_w, track_h, UI_MenuSliderFillColor ());
		}

		R_DrawFill32 (thumb_x, thumb_y, thumb_w, thumb_h, UI_MenuSliderThumbColor ());

		uint32 border_color = UI_MenuSliderBorderColor ();
		R_DrawFill32 (thumb_x, thumb_y, 1, thumb_h, border_color);				 // Left
		R_DrawFill32 (thumb_x + thumb_w - 1, thumb_y, 1, thumb_h, border_color); // Right
		R_DrawFill32 (thumb_x, thumb_y, thumb_w, 1, border_color);				 // Top
		R_DrawFill32 (thumb_x, thumb_y + thumb_h - 1, thumb_w, 1, border_color); // Bottom
	}
	else
	{
		UI_DrawChar (s->generic.x + RCOLUMN_OFFSET, s->generic.y, flags | UI_LEFT, 128);

		for (i = 0; i < SLIDER_RANGE; i++)
			UI_DrawChar (RCOLUMN_OFFSET + s->generic.x + i * CHAR_WIDTH + CHAR_WIDTH, s->generic.y, flags | UI_LEFT, 129);

		UI_DrawChar (RCOLUMN_OFFSET + s->generic.x + i * CHAR_WIDTH + CHAR_WIDTH, s->generic.y, flags | UI_LEFT, 130);

		UI_DrawChar (CHAR_WIDTH + RCOLUMN_OFFSET + s->generic.x + (SLIDER_RANGE - 1) * CHAR_WIDTH * pos, s->generic.y, flags | UI_LEFT, 131);
	}

	if (s->generic.flags & QMF_SHOW_VALUE)
	{
		Slider_ValueString (s, s->curvalue, value, sizeof (value));
		if (UI_MenuStyleId () == UI_MENU_STYLE_CUSTOM)
		{
			if (s->modified)
			{
				Menu_SetColor (UI_MenuValueChangedColor ());
			}
			else if (s->generic.flags & QMF_HASFOCUS)
			{
				Menu_SetColor (UI_MenuValueActiveColor ());
			}
			else
			{
				Menu_SetColor (UI_MenuValueColor ());
			}
		}
		UI_DrawString (s->generic.x + RCOLUMN_OFFSET + 13 * CHAR_WIDTH,
			s->generic.y, flags | UI_LEFT, value);
		if (UI_MenuStyleId () == UI_MENU_STYLE_CUSTOM)
		{
			Menu_SetNormalColor ();
		}
	}

	if (s->cvar &&
		(strncmp (s->cvar->name, "sh_histogram_color_", 19) == 0 ||
			strncmp (s->cvar->name, "sh_lagometer_color_", 19) == 0 ||
			strncmp (s->cvar->name, "sh_netgraph_color_", 18) == 0))
	{
		int box_x = s->generic.x + RCOLUMN_OFFSET + 13 * CHAR_WIDTH + 5 * CHAR_WIDTH;
		int box_y = s->generic.y + 1;
		int box_w = CHAR_WIDTH * 2;
		int box_h = CHAR_HEIGHT - 2;
		int col = Cvar_ClampInteger (s->cvar, 0, 255);
		R_DrawFill8 (box_x - 1, box_y - 1, box_w + 2, box_h + 2, 7); // border
		R_DrawFill8 (box_x, box_y, box_w, box_h, col);				 // color swatch
	}
}

/*
===================================================================

SEPARATOR CONTROL

===================================================================
*/

/*
=================
Separator_Init
=================
*/
static void Separator_Init (menuSeparator_t *s)
{
	s->generic.rect.x = s->generic.rect.y = 999999;
	s->generic.rect.width = s->generic.rect.height = -999999;
}

/*
=================
Separator_Draw
=================
*/
static void Separator_Draw (menuSeparator_t *s)
{
	if (s->generic.name)
		UI_DrawString (s->generic.x, s->generic.y, UI_RIGHT, s->generic.name);
}

/*
===================================================================

SAVEGAME CONTROL

===================================================================
*/

static void Savegame_Push (menuAction_t *a)
{
	char *info;

	Z_Free (a->generic.name);

	info = SV_GetSaveInfo (a->cmd);
	if (info)
	{
		a->generic.name = info;
		a->generic.flags &= ~QMF_GRAYED;
	}
	else
	{
		a->generic.name = UI_CopyString ("<EMPTY>");
		if (a->generic.type == MTYPE_LOADGAME)
			a->generic.flags |= QMF_GRAYED;
	}

	UI_StringDimensions (&a->generic.rect, a->generic.uiFlags, a->generic.name);
}

/*
===================================================================

MISC

===================================================================
*/

/*
=================
Common_DoEnter
=================
*/
static int Common_DoEnter (menuCommon_t *item)
{
	if (item->activate)
	{
		menuSound_t sound = item->activate (item);
		if (sound != QMS_NOTHANDLED)
		{
			return sound;
		}
	}

	return QMS_IN;
}

/*
=================
Menu_AddItem
=================
*/
void Menu_AddItem (menuFrameWork_t *menu, void *item)
{
	Q_assert (menu->nitems < MAX_MENU_ITEMS);

	if (menu->nitems >= menu->itemCapacity)
	{
		int newCapacity = menu->itemCapacity ?
			menu->itemCapacity + MIN_MENU_ITEMS :
			MIN_MENU_ITEMS;

		if (newCapacity > MAX_MENU_ITEMS)
		{
			newCapacity = MAX_MENU_ITEMS;
		}

		menu->items = menu->items ?
			Z_Realloc (menu->items, newCapacity * sizeof (void *)) :
			UI_Malloc (newCapacity * sizeof (void *));
		menu->itemCapacity = newCapacity;
	}

	menu->items[menu->nitems++] = item;
	((menuCommon_t *)item)->parent = menu;
}

static void UI_ClearBounds (int mins[2], int maxs[2])
{
	mins[0] = mins[1] = 9999;
	maxs[0] = maxs[1] = -9999;
}

static void UI_AddRectToBounds (const vrect_t *rc, int mins[2], int maxs[2])
{
	if (mins[0] > rc->x)
	{
		mins[0] = rc->x;
	}
	if (maxs[0] < rc->x + rc->width)
	{
		maxs[0] = rc->x + rc->width;
	}

	if (mins[1] > rc->y)
	{
		mins[1] = rc->y;
	}
	if (maxs[1] < rc->y + rc->height)
	{
		maxs[1] = rc->y + rc->height;
	}
}

static bool Menu_CalcItemBounds (menuFrameWork_t *menu, int mins[2], int maxs[2])
{
	int i;
	bool hasBounds = false;

	UI_ClearBounds (mins, maxs);

	for (i = 0; i < menu->nitems; i++)
	{
		menuCommon_t *item = menu->items[i];

		if (item->flags & QMF_HIDDEN)
		{
			continue;
		}
		if (item->rect.width <= 0 || item->rect.height <= 0)
		{
			continue;
		}

		UI_AddRectToBounds (&item->rect, mins, maxs);
		hasBounds = true;
	}

	return hasBounds;
}

static int Menu_ItemPadding (const menuCommon_t *item)
{
	if (item->type == MTYPE_BINDSELECT2 && BindSelect2_IsFirst ((const menuBindSelect2_t *)item))
		return CHAR_HEIGHT + 2;
	return 0;
}

static int Menu_ItemHeight (const menuCommon_t *item)
{
	if (item->type == MTYPE_BITMAP)
	{
		return GENERIC_SPACING (item->height);
	}

	return MENU_SPACING + Menu_ItemPadding (item);
}

static int Menu_TitleHeight (const menuFrameWork_t *menu)
{
	int top_pad, item_gap;

	if (!menu->title)
		return 0;
	top_pad = ui_menu_title_top_padding ? ui_menu_title_top_padding->intvalue : 12;
	item_gap = ui_menu_title_item_gap ? ui_menu_title_item_gap->intvalue : 8;
	return max (0, top_pad) + CHAR_HEIGHT + max (MENU_SPACING, item_gap) +
		(menu->videoApply && JUI_VideoTabsAvailable () ? CHAR_HEIGHT + MENU_SPACING * 2 : 0);
}

static const char *const video_tab_labels[] = { "Display", "Graphics", "Advanced" };
static const char *const video_tab_menus[] = { "video", "video_graphics", "video_advanced" };

static int Menu_VideoTabIndex (const menuFrameWork_t *menu)
{
	int i;

	for (i = 0; i < 3; i++)
		if (menu->name && !strcmp (menu->name, video_tab_menus[i]))
			return i;
	return 0;
}

static void Menu_VideoTabRects (const menuFrameWork_t *menu, vrect_t rects[3])
{
	int i, x, total = 0;
	int gap = CHAR_WIDTH / 2;
	int top = ui_menu_title_top_padding ? max (0, ui_menu_title_top_padding->intvalue) : 12;
	int item_gap = ui_menu_title_item_gap ? ui_menu_title_item_gap->intvalue : 8;

	for (i = 0; i < 3; i++)
	{
		rects[i].width = ((int)strlen (video_tab_labels[i]) + 2) * CHAR_WIDTH;
		total += rects[i].width;
	}
	x = (uis.width - total - gap * 2) / 2;
	for (i = 0; i < 3; i++)
	{
		rects[i].x = x;
		rects[i].y = menu->y1 + top + CHAR_HEIGHT + max (MENU_SPACING, item_gap);
		rects[i].height = CHAR_HEIGHT + MENU_SPACING;
		x += rects[i].width + gap;
	}
}

static void Menu_DrawVideoTabs (menuFrameWork_t *menu)
{
	vrect_t rects[3];
	int i, selected;

	if (!menu->videoApply || !JUI_VideoTabsAvailable ())
		return;
	Menu_VideoTabRects (menu, rects);
	selected = Menu_VideoTabIndex (menu);
	for (i = 0; i < 3; i++)
	{
		vrect_t *rect = &rects[i];
		bool active = i == selected;
		R_DrawFill32 (rect->x, rect->y, rect->width, rect->height,
			active ? UI_MenuTabActiveBgColor () : UI_MenuTabInactiveBgColor ());
		Menu_SetColor (active ? UI_MenuTabActiveTextColor () : UI_MenuTabTextColor ());
		UI_DrawString (rect->x + rect->width / 2, rect->y + MENU_SPACING / 2,
			UI_CENTER, video_tab_labels[i]);
		if (active)
			R_DrawFill32 (rect->x, rect->y + rect->height - 2, rect->width, 2,
				menu->videoTabsFocused ? UI_MenuFocusMarkerColor () : UI_MenuTabUnderlineColor ());
	}
	R_ClearColor ();
}

static menuSound_t Menu_VideoSelectTab (int index)
{
	menuFrameWork_t *target = UI_FindMenu (video_tab_menus[index]);

	if (!target)
		return QMS_BEEP;
	Select_CloseOther ();
	UI_PushMenu (target);
	if (uis.activeMenu != target)
		return QMS_BEEP;
	target->videoTabsFocused = true;
	return QMS_MOVE;
}

static menuSound_t Menu_VideoTabsKey (menuFrameWork_t *menu, int key)
{
	menuCommon_t *first = NULL, *last = NULL, *focused;
	vrect_t rects[3];
	int i, index;

	if (!menu->videoApply || !JUI_VideoTabsAvailable ())
		return QMS_NOTHANDLED;
	index = Menu_VideoTabIndex (menu);
	if (key == K_TAB && Key_IsDown (K_CTRL))
		return Menu_VideoSelectTab ((index + (Key_IsDown (K_SHIFT) ? 2 : 1)) % 3);
	if (key == K_MOUSE1)
	{
		Menu_VideoTabRects (menu, rects);
		for (i = 0; i < 3; i++)
			if (UI_CursorInRect (&rects[i]))
				return Menu_VideoSelectTab (i);
		if (uis.mouseCoords[1] >= menu->y1 &&
			uis.mouseCoords[1] < menu->y1 + MENU_SPACING * 2 + Menu_TitleHeight (menu))
			return QMS_SILENT;
		menu->videoTabsFocused = false;
		return QMS_NOTHANDLED;
	}
	for (i = 0; i < menu->nitems; i++)
	{
		menuCommon_t *item = menu->items[i];
		if (!UI_IsItemSelectable (item))
			continue;
		if (!first)
			first = item;
		last = item;
	}
	focused = Menu_ItemAtCursor (menu);
	if (!menu->videoTabsFocused)
	{
		if (((key == K_UPARROW || key == K_KP_UPARROW) && focused == first) ||
			(key == K_TAB && focused == last))
		{
			menu->videoTabsFocused = true;
			return QMS_MOVE;
		}
		return QMS_NOTHANDLED;
	}
	switch (key)
	{
	case K_LEFTARROW:
	case K_KP_LEFTARROW:
		return Menu_VideoSelectTab ((index + 2) % 3);
	case K_RIGHTARROW:
	case K_KP_RIGHTARROW:
		return Menu_VideoSelectTab ((index + 1) % 3);
	case K_DOWNARROW:
	case K_KP_DOWNARROW:
	case K_TAB:
	case K_ENTER:
	case K_KP_ENTER:
		menu->videoTabsFocused = false;
		if (first)
			Menu_SetFocus (first);
		menu->scrollOffset = 0;
		Menu_Layout (menu);
		return QMS_MOVE;
	case K_ESCAPE:
	case K_MOUSE2:
		return QMS_NOTHANDLED;
	default:
		return QMS_SILENT;
	}
}

static int Menu_ScrollTop (const menuFrameWork_t *menu)
{
	int y = menu->y1 + MENU_SPACING * 2 + Menu_TitleHeight (menu);

	if (menu->banner)
		y += GENERIC_SPACING (menu->banner_rc.height) + MENU_SPACING;
	return y;
}

static int Menu_ScrollBottom (const menuFrameWork_t *menu)
{
	return menu->y2 - MENU_SPACING * 2;
}

static int Menu_CountVisibleItems (menuFrameWork_t *menu)
{
	int i, count = 0;

	for (i = 0; i < menu->nitems; i++)
	{
		menuCommon_t *item = menu->items[i];

		if (!(item->flags & QMF_HIDDEN))
		{
			count++;
		}
	}

	return count;
}

static int Menu_VisibleIndex (menuFrameWork_t *menu, const menuCommon_t *target)
{
	int i, index = 0;

	for (i = 0; i < menu->nitems; i++)
	{
		menuCommon_t *item = menu->items[i];

		if (item->flags & QMF_HIDDEN)
		{
			continue;
		}
		if (item == target)
		{
			return index;
		}
		index++;
	}

	return -1;
}

static int Menu_ScrollMax (menuFrameWork_t *menu)
{
	int visibleCount = Menu_CountVisibleItems (menu);

	if (!menu->maxVisible || menu->maxVisible >= visibleCount)
	{
		return 0;
	}

	return visibleCount - menu->maxVisible;
}

static bool Menu_Scroll (menuFrameWork_t *menu, int dir)
{
	int oldOffset = menu->scrollOffset;
	int maxOffset = Menu_ScrollMax (menu);

	if (maxOffset <= 0)
	{
		return false;
	}

	menu->scrollOffset = Q_clip (menu->scrollOffset + dir, 0, maxOffset);
	if (menu->scrollOffset == oldOffset)
	{
		return false;
	}

	if (menu->size)
	{
		Menu_Layout (menu);
	}

	return true;
}

static void Menu_ApplyDefaultFocusStyle (menuFrameWork_t *menu)
{
	if (menu->focusStyleSet)
	{
		return;
	}

	menu->focusBorderWidth = 2;
	menu->focusInset = 0;
	menu->focusHeight = 0;
	menu->focusMenuWidth = false;
}

static void Menu_TranslateHorizontal (menuFrameWork_t *menu, int dx)
{
	int i;

	if (!dx)
	{
		return;
	}

	for (i = 0; i < menu->nitems; i++)
	{
		menuCommon_t *item = menu->items[i];

		item->x += dx;
		item->rect.x += dx;
	}

	menu->banner_rc.x += dx;
	menu->plaque_rc.x += dx;
	menu->logo_rc.x += dx;
}

static void Menu_ClampHorizontal (menuFrameWork_t *menu)
{
	int mins[2], maxs[2];
	int dx = 0;

	if (!Menu_CalcItemBounds (menu, mins, maxs))
	{
		return;
	}

	if (mins[0] < MENU_SPACING)
	{
		dx = MENU_SPACING - mins[0];
	}
	if (maxs[0] + dx > uis.width - MENU_SPACING)
	{
		dx = uis.width - MENU_SPACING - maxs[0];
	}
	if (mins[0] + dx < MENU_SPACING)
	{
		dx = MENU_SPACING - mins[0];
	}

	Menu_TranslateHorizontal (menu, dx);
}

void Menu_Layout (menuFrameWork_t *menu)
{
	void *item;
	int i;

	menu->bar_image_handle = 0;
	menu->bar_image_loaded = false;
	if (ui_menu_bar_image && ui_menu_bar_image->string[0] && Q_stricmp (ui_menu_bar_image->string, "none") != 0)
	{
		menu->bar_image_handle = R_RegisterPic (ui_menu_bar_image->string);
		menu->bar_image_loaded = (menu->bar_image_handle != 0);
	}

	if (!menu->size)
	{
		menu->size = Menu_Size;
	}
	menu->size (menu);

	for (i = 0; i < menu->nitems; i++)
	{
		item = menu->items[i];
		switch (((menuCommon_t *)item)->type)
		{
		case MTYPE_FIELD:
			Field_Init (item);
			break;
		case MTYPE_SLIDER:
			Slider_Init (item);
			break;
		case MTYPE_LIST:
			MenuList_Init (item);
			break;
		case MTYPE_SPINCONTROL:
		case MTYPE_BITFIELD:
		case MTYPE_PAIRS:
		case MTYPE_VALUES:
		case MTYPE_STRINGS:
		case MTYPE_ACTION_SELECT:
		case MTYPE_TOGGLE:
			SpinControl_Init (item);
			break;
		case MTYPE_ACTION:
		case MTYPE_SAVEGAME:
		case MTYPE_LOADGAME:
			Action_Init (item);
			break;
		case MTYPE_SEPARATOR:
			Separator_Init (item);
			break;
		case MTYPE_STATIC:
			Static_Init (item);
			break;
		case MTYPE_KEYBIND:
			Keybind_Init (item);
			break;
		case MTYPE_SELECT:
			Select_Init (item);
			Select_Push (item);
			break;
		case MTYPE_SELECT2:
			Select2_Init (item);
			Select2_Push (item);
			break;
		case MTYPE_BINDSELECT2:
			BindSelect2_Init (item);
			BindSelect2_Push (item);
			break;
		case MTYPE_BITMAP:
			Bitmap_Init (item);
			break;
		default:
			break;
		}
	}

	Menu_ClampHorizontal (menu);

	// calc menu bounding box
	if (!Menu_CalcItemBounds (menu, menu->mins, menu->maxs))
	{
		menu->mins[0] = menu->maxs[0] = uis.width / 2;
		menu->mins[1] = menu->maxs[1] = uis.height / 2;
	}

	// expand
	menu->mins[0] -= MENU_SPACING;
	menu->mins[1] -= MENU_SPACING;
	menu->maxs[0] += MENU_SPACING;
	menu->maxs[1] += MENU_SPACING;

	// clamp
	if (menu->mins[0] < 0)
		menu->mins[0] = 0;
	if (menu->mins[1] < 0)
		menu->mins[1] = 0;
	if (menu->maxs[0] > uis.width)
		menu->maxs[0] = uis.width;
	if (menu->maxs[1] > uis.height)
		menu->maxs[1] = uis.height;
}

static const char *Menu_ShowIfValue (const char *name)
{
	cvar_t *var;

	if (!strcmp (name, "ui_exclusive_fullscreen"))
	{
		cvar_t *vid_borderless = Cvar_WeakGet ("vid_borderless");
		bool borderless = vid_borderless && vid_borderless->intvalue;

		return CL_JumpUIFullscreen () && !borderless ? "1" : "0";
	}

	if (!strcmp (name, "ui_borderless_or_windowed"))
	{
		cvar_t *vid_borderless = Cvar_WeakGet ("vid_borderless");
		bool borderless = vid_borderless && vid_borderless->intvalue;

		return (!CL_JumpUIFullscreen () || borderless) ? "1" : "0";
	}

	if (!strcmp (name, "ui_histogram_custom_width"))
	{
		cvar_t *sh_netmeter = Cvar_WeakGet ("sh_netmeter");
		cvar_t *sh_histogram_width_mode = Cvar_WeakGet ("sh_histogram_width_mode");
		bool histogram = sh_netmeter && sh_netmeter->intvalue == 3;
		bool custom = !sh_histogram_width_mode || sh_histogram_width_mode->intvalue == 0;

		return histogram && custom ? "1" : "0";
	}

	var = Cvar_FindVar (name);
	if (!var)
	{
		return "";
	}

	return var->string;
}

void Menu_UpdateShowIf (menuFrameWork_t *menu)
{
	int i;
	bool layout_changed = false;

	if (!menu)
	{
		return;
	}

	if (menu->name &&
		(strcmp (menu->name, "strafehelper") == 0 || Menu_IsStrafePresetMenu (menu)) &&
		menu->compact && cl_drawStrafeHelper)
	{
		bool should_shift = (cl_drawStrafeHelper->intvalue != 0);
		bool is_shifted = (menu->y1 == 16 - MENU_SPACING);
		if (should_shift != is_shifted)
		{
			layout_changed = true;
		}
	}
	else if (menu->name && strcmp (menu->name, "colorpicker") == 0 && menu->compact &&
		colorPicker.target && Q_strncasecmp (colorPicker.target->name, "sh_color_", 9) == 0 &&
		cl_drawStrafeHelper)
	{
		bool should_shift = (cl_drawStrafeHelper->intvalue != 0);
		bool is_shifted = (menu->y1 == 16 - MENU_SPACING);
		if (should_shift != is_shifted)
		{
			layout_changed = true;
		}
	}

	for (i = 0; i < menu->nitems; i++)
	{
		menuCommon_t *item = menu->items[i];
		if (item->show_if_cvar && item->show_if_value)
		{
			const char *val_str = Menu_ShowIfValue (item->show_if_cvar);
			bool cond_met;
			if (item->show_if_value[0] == '!')
			{
				cond_met = (strcmp (val_str, item->show_if_value + 1) != 0);
			}
			else
			{
				cond_met = (strcmp (val_str, item->show_if_value) == 0);
			}
			int old_flags = item->flags;

			if (cond_met)
			{
				item->flags &= ~QMF_HIDDEN;
			}
			else
			{
				item->flags |= QMF_HIDDEN;
			}

			if ((old_flags & QMF_HIDDEN) != (item->flags & QMF_HIDDEN))
			{
				layout_changed = true;
			}
		}
	}

	if (layout_changed)
	{
		Menu_Layout (menu);
	}

	// Ensure focused item is still selectable.
	menuCommon_t *focused_item = NULL;
	int pos = -1;
	for (i = 0; i < menu->nitems; i++)
	{
		menuCommon_t *item = menu->items[i];
		if (item->flags & QMF_HASFOCUS)
		{
			focused_item = item;
			pos = i;
			break;
		}
	}

	if (focused_item && !UI_IsItemSelectable (focused_item))
	{
		menuCommon_t *new_focus = NULL;
		int cursor = pos;
		do
		{
			cursor++;
			if (cursor >= menu->nitems)
			{
				cursor = 0;
			}
			menuCommon_t *item = menu->items[cursor];
			if (UI_IsItemSelectable (item))
			{
				new_focus = item;
				break;
			}
		} while (cursor != pos);

		if (new_focus)
		{
			Menu_SetFocus (new_focus);
			focused_item->flags &= ~QMF_HASFOCUS;
		}
		else
		{
			focused_item->flags &= ~QMF_HASFOCUS;
			if (menu->status == focused_item->status)
			{
				menu->status = NULL;
			}
		}
	}
}

void Menu_Init (menuFrameWork_t *menu)
{
	void *item;
	int i;
	bool focus = false;

	menu->focusInitialized = false;
	menu->prevFocusedIndex = -1;
	menu->currFocusedIndex = -1;
	menu->focusAnimStartTime = 0;
	menu->y1 = 0;
	menu->y2 = uis.height;
	menu->scrollOffset = 0;
	menu->maxVisible = 0;
	Menu_Layout (menu);

	for (i = 0; i < menu->nitems; i++)
	{
		item = menu->items[i];
		if (((menuCommon_t *)item)->flags & QMF_HASFOCUS)
		{
			if (UI_IsItemSelectable ((menuCommon_t *)item))
			{
				focus = true;
			}
			else
			{
				((menuCommon_t *)item)->flags &= ~QMF_HASFOCUS;
			}
		}
	}

	// set focus to the first selectable item by default
	if (!focus)
	{
		for (i = 0; i < menu->nitems; i++)
		{
			item = menu->items[i];
			if (UI_IsItemSelectable ((menuCommon_t *)item))
			{
				((menuCommon_t *)item)->flags |= QMF_HASFOCUS;
				if (((menuCommon_t *)item)->status)
				{
					menu->status = ((menuCommon_t *)item)->status;
				}
				break;
			}
		}
	}

	for (i = 0; i < menu->nitems; i++)
	{
		item = menu->items[i];
		if (((menuCommon_t *)item)->flags & QMF_HASFOCUS)
		{
			menu->currFocusedIndex = i;
			break;
		}
	}

	Menu_UpdateShowIf (menu);
}

void Menu_Size (menuFrameWork_t *menu)
{
	menuCommon_t *item;
	int x, y, w = 0, h, totalHeight;
	int i, widest = -1;
	int visibleCount = 0;

	// count visible items
	for (i = 0, h = 0; i < menu->nitems; i++)
	{
		item = menu->items[i];
		if (item->flags & QMF_HIDDEN)
		{
			continue;
		}
		visibleCount++;
		if (item->type == MTYPE_BITMAP)
		{
			h += GENERIC_SPACING (item->height);
			if (widest < item->width)
			{
				widest = item->width;
			}
		}
		else
		{
			h += Menu_ItemHeight (item);
		}
	}

	// account for banner
	if (menu->banner)
	{
		h += GENERIC_SPACING (menu->banner_rc.height);
	}

	// compute title area height
	int titleHeight = Menu_TitleHeight (menu);
	int statusHeight = Menu_StatusHeight (menu);

	// set menu top/bottom
	bool shift_top = false;
	if (menu->name &&
		(strcmp (menu->name, "strafehelper") == 0 || Menu_IsStrafePresetMenu (menu)) &&
		cl_drawStrafeHelper && cl_drawStrafeHelper->intvalue)
	{
		shift_top = true;
	}
	else if (menu->name && strcmp (menu->name, "colorpicker") == 0 &&
		colorPicker.target && Q_strncasecmp (colorPicker.target->name, "sh_color_", 9) == 0 &&
		cl_drawStrafeHelper && cl_drawStrafeHelper->intvalue)
	{
		shift_top = true;
	}

	int blockTop = 0;
	if (menu->compact)
	{
		if (shift_top)
		{
			blockTop = 16;
			menu->y1 = 16 - MENU_SPACING;
			menu->y2 = 16 + titleHeight + h + MENU_SPACING;
		}
		else
		{
			blockTop = (uis.height - statusHeight - h - titleHeight) / 2;
			menu->y1 = blockTop - MENU_SPACING;
			menu->y2 = blockTop + titleHeight + h + MENU_SPACING;
		}
	}
	else
	{
		blockTop = (uis.height - statusHeight - h - titleHeight) / 2;
		menu->y1 = 0;
		menu->y2 = uis.height;
	}

	if (Menu_IsStrafePresetMenu (menu) && menu->y2 - menu->y1 > uis.height / 2)
	{
		menu->y2 = menu->y1 + uis.height / 2;
	}

	// set menu horizontal base
	if (widest == -1)
	{
		if (menu->halign == MENU_HALIGN_LEFT)
		{
			x = uis.width / 4;
		}
		else if (menu->halign == MENU_HALIGN_RIGHT)
		{
			x = uis.width * 3 / 4;
		}
		else
		{
			x = uis.width / 2;
		}
	}
	else
	{
		// if menu has bitmaps, it is expected to have plaque and logo
		// align them horizontally to avoid going off screen on small resolution
		w = widest + CURSOR_WIDTH;
		if (menu->plaque_rc.width > menu->logo_rc.width)
		{
			w += menu->plaque_rc.width;
		}
		else
		{
			w += menu->logo_rc.width;
		}
		x = (uis.width + w) / 2 - widest;
	}

	// set menu vertical base
	y = menu->videoApply ? Menu_ScrollTop (menu) : blockTop + titleHeight;

	// banner is horizontally centered and
	// positioned on top of all menu items
	if (menu->banner)
	{
		menu->banner_rc.x = (uis.width - menu->banner_rc.width) / 2;
		menu->banner_rc.y = y;
		y += GENERIC_SPACING (menu->banner_rc.height);
	}
	// save for scroll overflow check
	totalHeight = h - (menu->banner ? GENERIC_SPACING (menu->banner_rc.height) : 0);

	// plaque and logo are vertically centered and
	// positioned to the left of bitmaps and cursor
	h = 0;
	if (menu->plaque)
	{
		h += menu->plaque_rc.height;
	}
	if (menu->logo)
	{
		h += menu->logo_rc.height + 5;
	}

	if (menu->plaque)
	{
		int gap = CURSOR_WIDTH;
		int menu_x = x;
		int menu_y = (uis.height - h) / 2;
		int menu_w = w;
		int menu_h = h;

		switch (menu->plaquePosition)
		{
		default:
		case PLAQUE_LEFT:
			menu->plaque_rc.x = menu_x - gap - menu->plaque_rc.width;
			menu->plaque_rc.y = menu_y;
			break;

		case PLAQUE_RIGHT:
			menu->plaque_rc.x = menu_x + menu_w + gap;
			menu->plaque_rc.y = menu_y;
			break;

		case PLAQUE_TOP:
			menu->plaque_rc.x = menu_x + (menu_w - menu->plaque_rc.width) / 2;
			menu->plaque_rc.y = menu_y - gap - menu->plaque_rc.height;
			break;

		case PLAQUE_BOTTOM:
			menu->plaque_rc.x = menu_x + (menu_w - menu->plaque_rc.width) / 2;
			menu->plaque_rc.y = menu_y + menu_h + gap;
			break;

		case PLAQUE_CENTER:
			menu->plaque_rc.x = menu_x + (menu_w - menu->plaque_rc.width) / 2;
			menu->plaque_rc.y = menu_y + (menu_h - menu->plaque_rc.height) / 2;
			break;

		case PLAQUE_SCREEN_TOPLEFT:
			menu->plaque_rc.x = gap;
			menu->plaque_rc.y = gap;
			break;

		case PLAQUE_SCREEN_TOPRIGHT:
			menu->plaque_rc.x = uis.width - menu->plaque_rc.width - gap;
			menu->plaque_rc.y = gap;
			break;

		case PLAQUE_SCREEN_BOTTOMLEFT:
			menu->plaque_rc.x = gap;
			menu->plaque_rc.y = uis.height - menu->plaque_rc.height - gap;
			break;

		case PLAQUE_SCREEN_BOTTOMRIGHT:
			menu->plaque_rc.x = uis.width - menu->plaque_rc.width - gap;
			menu->plaque_rc.y = uis.height - menu->plaque_rc.height - gap;
			break;
		}
	}

	if (menu->logo)
	{
		menu->logo_rc.x = x - CURSOR_WIDTH - menu->logo_rc.width;
		menu->logo_rc.y = (uis.height + h) / 2 - menu->logo_rc.height;
	}

	// align items
	for (i = 0; i < menu->nitems; i++)
	{
		item = menu->items[i];
		if (item->flags & QMF_HIDDEN)
		{
			continue;
		}
		y += Menu_ItemPadding (item);
		item->x = x;
		item->y = y;
		if (item->type == MTYPE_BITMAP)
		{
			y += GENERIC_SPACING (item->height);
		}
		else
		{
			y += MENU_SPACING;
		}
	}


	// scroll handling - reposition items when they don't fit
	{
		int scrollTop = MENU_SPACING * 2 + titleHeight;
		int scrollBottom = uis.height - statusHeight - MENU_SPACING * 2;
		if (Menu_IsStrafePresetMenu (menu))
		{
			scrollTop += menu->y1;
			scrollBottom = menu->y2 - MENU_SPACING * 2;
		}
		int availHeight = scrollBottom - scrollTop;
		if (menu->banner && menu->banner_rc.height > 0)
			availHeight -= GENERIC_SPACING (menu->banner_rc.height) + MENU_SPACING;

		if (totalHeight > availHeight)
		{
			int maxOffset;

			if (!Menu_IsStrafePresetMenu (menu))
			{
				menu->y1 = 0;
				menu->y2 = uis.height - statusHeight;
			}
			scrollTop = Menu_ScrollTop (menu);
			scrollBottom = Menu_ScrollBottom (menu);

		restartScroll:
			// validate scrollOffset
			if (menu->scrollOffset < 0)
				menu->scrollOffset = 0;
			if (menu->scrollOffset >= visibleCount)
				menu->scrollOffset = 0;

			// compute pre-scroll height (items above scrollOffset)
			int preHeight = 0;
			int visibleIndex = 0;
			for (i = 0; i < menu->nitems && visibleIndex < menu->scrollOffset; i++)
			{
				item = menu->items[i];
				if (item->flags & QMF_HIDDEN)
					continue;
				preHeight += Menu_ItemHeight (item);
				visibleIndex++;
			}

			// reposition banner at top for scrollable menu
			if (menu->banner)
			{
				menu->banner_rc.x = (uis.width - menu->banner_rc.width) / 2;
				menu->banner_rc.y = menu->y1 + MENU_SPACING + titleHeight;
			}

			// base Y: top of items area minus scroll pre-height
			y = scrollTop;
			y -= preHeight;

			// reposition all items with scroll offset
			for (i = 0; i < menu->nitems; i++)
			{
				item = menu->items[i];
				if (item->flags & QMF_HIDDEN)
					continue;
				item->x = x;
				item->y = y + Menu_ItemPadding (item);
				item->rect.y = item->y;
				y += Menu_ItemHeight (item);
			}

			// compute the visible row span; hidden items do not count.
			int visibleY = y;
			int fitCount = 0;
			visibleY = scrollTop;
			visibleIndex = 0;
			for (i = 0; i < menu->nitems; i++)
			{
				item = menu->items[i];
				int itemH;

				if (item->flags & QMF_HIDDEN)
					continue;
				if (visibleIndex++ < menu->scrollOffset)
					continue;

				itemH = Menu_ItemHeight (item);
				if (visibleY + itemH > scrollBottom)
					break;
				fitCount++;
				visibleY += itemH;
			}

			menu->maxVisible = fitCount;
			if (menu->maxVisible < 1)
				menu->maxVisible = 1;

			maxOffset = visibleCount - menu->maxVisible;
			if (maxOffset < 0)
				maxOffset = 0;
			if (menu->scrollOffset > maxOffset)
			{
				menu->scrollOffset = maxOffset;
				goto restartScroll;
			}
		}
		else
		{
			menu->scrollOffset = 0;
			menu->maxVisible = visibleCount;
		}
	}
}

menuCommon_t *Menu_ItemAtCursor (menuFrameWork_t *m)
{
	menuCommon_t *item;
	int i;

	for (i = 0; i < m->nitems; i++)
	{
		item = m->items[i];
		if (item->flags & QMF_HASFOCUS)
		{
			return item;
		}
	}

	return NULL;
}

void Menu_SetFocus (menuCommon_t *focus)
{
	menuFrameWork_t *menu;
	menuCommon_t *item;
	int i;

	focus->parent->videoTabsFocused = false;
	if (focus->flags & QMF_HASFOCUS)
	{
		return;
	}

	menu = focus->parent;

	{
		int prev_idx = -1;
		int curr_idx = -1;
		for (i = 0; i < menu->nitems; i++)
		{
			item = (menuCommon_t *)menu->items[i];
			if (item->flags & QMF_HASFOCUS)
			{
				prev_idx = i;
			}
			if (item == focus)
			{
				curr_idx = i;
			}
		}
		menu->prevFocusedIndex = prev_idx;
		menu->currFocusedIndex = curr_idx;
		menu->focusAnimStartTime = uis.realtime;
	}

	for (i = 0; i < menu->nitems; i++)
	{
		item = (menuCommon_t *)menu->items[i];

		if (item == focus)
		{
			item->flags |= QMF_HASFOCUS;
			if (item->type == MTYPE_BITMAP)
			{
				menu->status = ((menuBitmap_t *)item)->generic.status;
			}
			else if (item->focus)
			{
				item->focus (item, true);
			}
			else if (item->status)
			{
				menu->status = item->status;
			}
		}
		else if (item->flags & QMF_HASFOCUS)
		{
			item->flags &= ~QMF_HASFOCUS;
			if (item->type == MTYPE_BITMAP)
			{
				if (menu->status == ((menuBitmap_t *)item)->generic.status && menu->status != focus->status)
				{
					menu->status = NULL;
				}
			}
			else if (item->focus)
			{
				item->focus (item, false);
			}
			else if (menu->status == item->status && menu->status != focus->status)
			{
				menu->status = NULL;
			}
		}
	}
}

/*
=================
Menu_AdjustCursor

This function takes the given menu, the direction, and attempts
to adjust the menu's cursor so that it's at the next available
slot.
=================
*/
menuSound_t Menu_AdjustCursor (menuFrameWork_t *m, int dir)
{
	menuCommon_t *item;
	int cursor, pos;
	int i;

	if (!m->nitems)
	{
		return QMS_NOTHANDLED;
	}

	pos = 0;
	for (i = 0; i < m->nitems; i++)
	{
		item = (menuCommon_t *)m->items[i];

		if (item->flags & QMF_HASFOCUS)
		{
			pos = i;
			break;
		}
	}

	/*
    ** crawl in the direction indicated until we find a valid spot
    */
	cursor = pos;
	if (dir == 1)
	{
		do
		{
			cursor++;
			if (cursor >= m->nitems)
				cursor = 0;

			item = (menuCommon_t *)m->items[cursor];
			if (UI_IsItemSelectable (item))
				break;
		} while (cursor != pos);
	}
	else
	{
		do
		{
			cursor--;
			if (cursor < 0)
				cursor = m->nitems - 1;

			item = (menuCommon_t *)m->items[cursor];
			if (UI_IsItemSelectable (item))
				break;
		} while (cursor != pos);
	}

	Menu_SetFocus (item);
	// scroll to keep cursor visible
	if (m->maxVisible)
	{
		int visibleIndex = Menu_VisibleIndex (m, item);

		if (visibleIndex < 0)
		{
			return QMS_MOVE;
		}

		if (visibleIndex < m->scrollOffset)
		{
			m->scrollOffset = visibleIndex;
			if (m->size)
				Menu_Layout (m);
		}
		else if (visibleIndex >= m->scrollOffset + m->maxVisible)
		{
			m->scrollOffset = visibleIndex - m->maxVisible + 1;
			if (m->scrollOffset < 0)
				m->scrollOffset = 0;
			if (m->size)
				Menu_Layout (m);
		}
	}

	return QMS_MOVE;
}

static int Menu_WrapStatus (const char *status, int lens[8], const char *ptrs[8])
{
	int linewidth = max (1, uis.width / CHAR_WIDTH);
	int x, l, count;
	const char *txt, *p;

	if (!status || !*status)
		return 0;
	txt = status;
	x = 0;

	count = 0;
	ptrs[0] = txt;

	while (*txt)
	{
		// count word length
		for (p = txt; *p > 32; p++)
			;
		l = p - txt;

		// word wrap
		if ((l < linewidth && x + l > linewidth) || (x == linewidth))
		{
			if (count == 7)
				break;
			lens[count++] = x;
			ptrs[count] = txt;
			x = 0;
		}

		// display character and advance
		txt++;
		x++;
	}

	lens[count++] = x;
	return count;
}

static void Menu_DrawStatus (menuFrameWork_t *menu)
{
	int x, y, l, count;
	char statusBuffer[MAX_STRING_CHARS];
	int lens[8];
	const char *ptrs[8];

	count = Menu_WrapStatus (Menu_StatusText (menu, statusBuffer, sizeof (statusBuffer)), lens, ptrs);
	if (!count)
		return;

	R_DrawFill32 (0, uis.height - count * CHAR_HEIGHT,
		uis.width, count * CHAR_HEIGHT,
		UI_MenuHintBackgroundColor ());
	Menu_SetColor (UI_MenuHintTextColor ());

	for (l = 0; l < count; l++)
	{
		x = (uis.width - lens[l] * CHAR_WIDTH) / 2;
		y = uis.height - (count - l) * CHAR_HEIGHT;
		CL_JumpUIString (x, y, 0, lens[l], ptrs[l], uis.fontHandle);
	}
	Menu_SetNormalColor ();
}

static int Menu_TitleX (menuFrameWork_t *menu)
{
	if (menu->halign == MENU_HALIGN_CENTER)
	{
		return uis.width / 2;
	}

	return (menu->mins[0] + menu->maxs[0]) / 2;
}

static cvar_t *Menu_ItemCvar (const menuCommon_t *item)
{
	if (!item)
	{
		return NULL;
	}

	switch (item->type)
	{
	case MTYPE_FIELD:
		return ((const menuField_t *)item)->cvar;
	case MTYPE_SLIDER:
		return ((const menuSlider_t *)item)->cvar;
	case MTYPE_SPINCONTROL:
	case MTYPE_BITFIELD:
	case MTYPE_PAIRS:
	case MTYPE_VALUES:
	case MTYPE_STRINGS:
	case MTYPE_ACTION_SELECT:
	case MTYPE_TOGGLE:
		return ((const menuSpinControl_t *)item)->cvar;
	default:
		return NULL;
	}
}

static const char *Menu_ItemStatusText (menuFrameWork_t *menu, const menuCommon_t *item,
	const char *status, char *buffer, size_t size)
{
	cvar_t *cvar;

	if (menu && menu->videoApply && item && Menu_ItemCvar (item))
	{
		const char *timing = item->flags & QMF_DEFER_COMMIT ?
			"Choose Apply changes to use this setting; video restarts." :
			"Changes immediately.";
		Q_snprintf (buffer, size, "%s%s%s", status ? status : "",
			status && *status ? " " : "", timing);
		return buffer;
	}

	if (!Menu_IsNetMeterMenu (menu))
	{
		return status;
	}

	cvar = Menu_ItemCvar (item);
	if (!cvar || !Cvar_DefaultString (cvar))
	{
		return status;
	}

	if (status && *status)
	{
		Q_snprintf (buffer, size, "%s Default: %s.", status, Cvar_DefaultString (cvar));
	}
	else
	{
		Q_snprintf (buffer, size, "Default: %s.", Cvar_DefaultString (cvar));
	}

	return buffer;
}

static const char *Menu_StatusText (menuFrameWork_t *menu, char *buffer, size_t size)
{
	if (menu->videoTabsFocused)
		return "Left/Right switches tabs. Down or Enter opens settings. Ctrl+Tab also switches tabs.";
	return Menu_ItemStatusText (menu, Menu_ItemAtCursor (menu), menu->status, buffer, size);
}

static int Menu_StatusHeight (menuFrameWork_t *menu)
{
	int i, count, lines;
	int lens[8];
	const char *ptrs[8], *status;
	char buffer[MAX_STRING_CHARS];

	status = Menu_StatusText (menu, buffer, sizeof (buffer));
	lines = Menu_WrapStatus (status, lens, ptrs);
	for (i = 0; i < menu->nitems; i++)
	{
		const menuCommon_t *item = menu->items[i];
		if (item->flags & QMF_HIDDEN)
			continue;
		status = Menu_ItemStatusText (menu, item, item->status, buffer, sizeof (buffer));
		count = Menu_WrapStatus (status, lens, ptrs);
		if (lines < count)
			lines = count;
	}

	return lines * CHAR_HEIGHT;
}

static void Menu_GetItemContentRect (const menuCommon_t *item, vrect_t *rect)
{
	*rect = item->rect;

	if (item->type == MTYPE_SLIDER)
	{
		const menuSlider_t *s = (const menuSlider_t *)item;
		int label_len = s->generic.name ? strlen (s->generic.name) * CHAR_WIDTH : 0;
		rect->x = s->generic.x + LCOLUMN_OFFSET - label_len;
		rect->y = s->generic.y;
		rect->height = CHAR_HEIGHT;
		if (s->generic.flags & QMF_SHOW_VALUE)
		{
			char value[32];
			Slider_ValueString ((menuSlider_t *)s, s->curvalue, value, sizeof (value));
			int val_len = strlen (value);
			int val_end = 13 * CHAR_WIDTH + val_len * CHAR_WIDTH;
			if (s->cvar &&
				(strncmp (s->cvar->name, "sh_histogram_color_", 19) == 0 ||
					strncmp (s->cvar->name, "sh_lagometer_color_", 19) == 0 ||
					strncmp (s->cvar->name, "sh_netgraph_color_", 18) == 0))
			{
				val_end = 20 * CHAR_WIDTH;
			}
			rect->width = (RCOLUMN_OFFSET - LCOLUMN_OFFSET) + label_len + val_end;
		}
		else
		{
			rect->width = (RCOLUMN_OFFSET - LCOLUMN_OFFSET) + label_len + (SLIDER_RANGE + 2) * CHAR_WIDTH;
		}
	}
	else if (item->type == MTYPE_SPINCONTROL ||
		item->type == MTYPE_BITFIELD ||
		item->type == MTYPE_PAIRS ||
		item->type == MTYPE_VALUES ||
		item->type == MTYPE_STRINGS ||
		item->type == MTYPE_ACTION_SELECT ||
		item->type == MTYPE_TOGGLE)
	{
		const menuSpinControl_t *s = (const menuSpinControl_t *)item;
		int label_len = s->generic.name ? strlen (s->generic.name) * CHAR_WIDTH : 0;
		rect->x = s->generic.x + LCOLUMN_OFFSET - label_len;
		rect->y = s->generic.y;
		rect->height = CHAR_HEIGHT;
		if (s->generic.flags & QMF_SHOW_VALUE)
		{
			const char *val = NULL;
			char raw[MAX_QPATH];
			char val_str[MAX_QPATH] = "";
			if (s->generic.type == MTYPE_PAIRS &&
				s->curvalue >= 0 && s->curvalue < s->numItems && s->itemvalues)
			{
				val = s->itemvalues[s->curvalue];
			}
			else if ((s->generic.type == MTYPE_TOGGLE ||
						 s->generic.type == MTYPE_BITFIELD) &&
				s->curvalue >= 0)
			{
				Q_snprintf (raw, sizeof (raw), "%d",
					s->generic.type == MTYPE_TOGGLE ?
						(s->curvalue ^ s->negate) :
						(s->cvar ? s->cvar->intvalue : 0));
				val = raw;
			}
			else if (s->cvar)
			{
				val = s->cvar->string;
			}
			if (val)
			{
				Q_snprintf (val_str, sizeof (val_str), "[%s]", val);
			}
			int val_len = strlen (val_str);
			rect->width = (RCOLUMN_OFFSET - LCOLUMN_OFFSET) + label_len + SpinControl_ValueOffset (s) + val_len * CHAR_WIDTH;
		}
		else
		{
			const char *name = NULL;
			if (s->itemnames && s->curvalue >= 0 && s->curvalue < s->numItems)
			{
				name = s->itemnames[s->curvalue];
			}
			if (!name)
			{
				name = "???";
			}
			rect->width = (RCOLUMN_OFFSET - LCOLUMN_OFFSET) + label_len + strlen (name) * CHAR_WIDTH;
		}
	}
}

static void Menu_DrawFocusBox (int x, int y, int w, int h, int markerWidth)
{
	if (w <= 0 || h <= 0)
	{
		return;
	}
	uint32 border_color = uis.color.focus_border.u32;
	R_DrawFill32 (x, y, w, 1, border_color);		 // Top
	R_DrawFill32 (x, y + h - 1, w, 1, border_color); // Bottom
	R_DrawFill32 (x, y, 1, h, border_color);		 // Left
	R_DrawFill32 (x + w - 1, y, 1, h, border_color); // Right
}

static void Menu_DrawFocusMarker (menuFrameWork_t *menu, const menuCommon_t *item)
{
	vrect_t row;
	int markerWidth;
	int padding_x = 0;
	int padding_y = 0;
	int base_h;
	float target_x, target_w, target_y, target_h;

	if (item->type == MTYPE_LIST)
	{
		return;
	}

	if (!(item->flags & QMF_HASFOCUS) || !UI_IsItemSelectable (item))
	{
		return;
	}

	Menu_ApplyDefaultFocusStyle (menu);

	if (ui_menu_focus_padding_x)
	{
		padding_x = ui_menu_focus_padding_x->intvalue;
	}
	if (ui_menu_focus_padding_y)
	{
		padding_y = ui_menu_focus_padding_y->intvalue;
	}

	const char *width_style = ui_menu_focus_width ? ui_menu_focus_width->string : "content";


	base_h = menu->focusHeight > 0 ? menu->focusHeight :
									 (item->rect.height > CHAR_HEIGHT ? item->rect.height : CHAR_HEIGHT) + 1;

	target_h = base_h + padding_y * 2;
	if (item->type != MTYPE_BITMAP)
		target_h = min (target_h, MENU_SPACING);
	target_y = item->y + CHAR_HEIGHT / 2.0f - target_h / 2.0f;

	if (target_y < menu->y1)
	{
		target_y = menu->y1;
	}
	if (target_y + target_h > menu->y2)
	{
		target_y = menu->y2 - target_h;
	}

	bool use_content_width = (strcmp (width_style, "content") == 0);

	if (use_content_width)
	{
		vrect_t content_rect;
		Menu_GetItemContentRect (item, &content_rect);
		target_x = content_rect.x - padding_x;
		target_w = content_rect.width + 2 * padding_x;
	}
	else
	{
		target_x = menu->focusMenuWidth ? menu->mins[0] : 0;
		target_w = menu->focusMenuWidth ? menu->maxs[0] - menu->mins[0] : uis.width;
		target_x += menu->focusInset;
		target_w -= menu->focusInset * 2;
	}

	unsigned int current_time = uis.realtime;
	bool use_anim = (ui_menu_anim && ui_menu_anim->intvalue);

	if (!menu->focusInitialized)
	{
		menu->focusCurrentX = target_x;
		menu->focusCurrentWidth = target_w;
		menu->focusCurrentY = target_y;
		menu->focusCurrentHeight = target_h;
		menu->focusTargetX = target_x;
		menu->focusTargetWidth = target_w;
		menu->focusTargetY = target_y;
		menu->focusTargetHeight = target_h;
		menu->focusPrevX = target_x;
		menu->focusPrevWidth = target_w;
		menu->focusPrevY = target_y;
		menu->focusPrevHeight = target_h;
		menu->focusAnimStartTime = 0;
		menu->focusInitialized = true;
	}
	else if (use_anim)
	{
		if (target_x != menu->focusTargetX || target_y != menu->focusTargetY ||
			target_w != menu->focusTargetWidth || target_h != menu->focusTargetHeight)
		{
			menu->focusPrevX = menu->focusCurrentX;
			menu->focusPrevY = menu->focusCurrentY;
			menu->focusPrevWidth = menu->focusCurrentWidth;
			menu->focusPrevHeight = menu->focusCurrentHeight;
			menu->focusTargetX = target_x;
			menu->focusTargetY = target_y;
			menu->focusTargetWidth = target_w;
			menu->focusTargetHeight = target_h;
			menu->focusAnimStartTime = current_time;
		}

		int focus_ms = ui_menu_anim_focus_ms ? ui_menu_anim_focus_ms->intvalue : 90;
		if (focus_ms <= 0)
		{
			menu->focusCurrentX = target_x;
			menu->focusCurrentWidth = target_w;
			menu->focusCurrentY = target_y;
			menu->focusCurrentHeight = target_h;
			menu->focusAnimStartTime = 0;
		}
		else if (menu->focusAnimStartTime > 0)
		{
			float elapsed = (float)(current_time - menu->focusAnimStartTime);
			float t = elapsed / (float)focus_ms;
			if (t >= 1.0f)
			{
				menu->focusCurrentX = target_x;
				menu->focusCurrentWidth = target_w;
				menu->focusCurrentY = target_y;
				menu->focusCurrentHeight = target_h;
				menu->focusAnimStartTime = 0;
			}
			else
			{
				float eased = Menu_Ease01 (t);
				menu->focusCurrentX = menu->focusPrevX + (target_x - menu->focusPrevX) * eased;
				menu->focusCurrentY = menu->focusPrevY + (target_y - menu->focusPrevY) * eased;
				menu->focusCurrentWidth = menu->focusPrevWidth + (target_w - menu->focusPrevWidth) * eased;
				menu->focusCurrentHeight = menu->focusPrevHeight + (target_h - menu->focusPrevHeight) * eased;
			}
		}
		else
		{
			menu->focusCurrentX = target_x;
			menu->focusCurrentWidth = target_w;
			menu->focusCurrentY = target_y;
			menu->focusCurrentHeight = target_h;
		}
	}
	else
	{
		menu->focusCurrentX = target_x;
		menu->focusCurrentWidth = target_w;
		menu->focusCurrentY = target_y;
		menu->focusCurrentHeight = target_h;
	}
	menu->focusLastTime = current_time;

	row.x = (int)menu->focusCurrentX;
	row.width = (int)menu->focusCurrentWidth;
	row.y = (int)menu->focusCurrentY;
	row.height = (int)menu->focusCurrentHeight;
	markerWidth = menu->focusBorderWidth;

	if ((uis.realtime >> 8) & 1)
	{
		int arrow_x = 0;
		int arrow_y = row.y + padding_y;
		int arrow_flags = item->uiFlags | UI_RIGHT;

		if (use_content_width)
		{
			if (item->type == MTYPE_SLIDER)
			{
				const menuSlider_t *s = (const menuSlider_t *)item;
				int label_len = s->generic.name ? strlen (s->generic.name) * CHAR_WIDTH : 0;
				arrow_x = row.x + padding_x + label_len + 24;
			}
			else if (item->type == MTYPE_SPINCONTROL ||
				item->type == MTYPE_BITFIELD ||
				item->type == MTYPE_PAIRS ||
				item->type == MTYPE_VALUES ||
				item->type == MTYPE_STRINGS ||
				item->type == MTYPE_ACTION_SELECT ||
				item->type == MTYPE_TOGGLE)
			{
				const menuSpinControl_t *s = (const menuSpinControl_t *)item;
				int label_len = s->generic.name ? strlen (s->generic.name) * CHAR_WIDTH : 0;
				arrow_x = row.x + padding_x + label_len + 24;
			}
			else if (item->type == MTYPE_KEYBIND)
			{
				const menuKeybind_t *k = (const menuKeybind_t *)item;
				int label_len = k->generic.name ? strlen (k->generic.name) * CHAR_WIDTH : 0;
				arrow_x = row.x + padding_x + label_len + 24;
			}
			else if (item->type == MTYPE_ACTION ||
				item->type == MTYPE_SAVEGAME ||
				item->type == MTYPE_LOADGAME)
			{
				arrow_x = row.x + padding_x - CHAR_WIDTH;
				arrow_flags = (item->uiFlags & ~UI_CENTER) | UI_LEFT;
			}
		}
		else
		{
			if (item->type == MTYPE_SLIDER ||
				item->type == MTYPE_SPINCONTROL ||
				item->type == MTYPE_BITFIELD ||
				item->type == MTYPE_PAIRS ||
				item->type == MTYPE_VALUES ||
				item->type == MTYPE_STRINGS ||
				item->type == MTYPE_ACTION_SELECT ||
				item->type == MTYPE_TOGGLE ||
				item->type == MTYPE_KEYBIND)
			{
				arrow_x = item->x + RCOLUMN_OFFSET / 2;
			}
			else if (item->type == MTYPE_ACTION ||
				item->type == MTYPE_SAVEGAME ||
				item->type == MTYPE_LOADGAME)
			{
				if (item->name && (item->uiFlags & UI_CENTER) == UI_CENTER)
				{
					arrow_x = item->x - strlen (item->name) * CHAR_WIDTH / 2 - CHAR_WIDTH;
					arrow_flags = (item->uiFlags & ~UI_CENTER) | UI_LEFT;
				}
				else
				{
					arrow_x = item->x - RCOLUMN_OFFSET / 2;
				}
			}
		}

		if (arrow_x != 0)
		{
			Menu_SetColor (UI_MenuFocusMarkerColor ());
			UI_DrawChar (arrow_x, arrow_y, arrow_flags, 13);
			R_ClearColor ();
		}
	}

	if (!menu->focusStyleSet && !menu->compact && !UI_MenuStyleFocusFill ())
	{
		return;
	}

	if (row.height <= 0)
	{
		return;
	}

	Menu_DrawFocusBox (row.x, row.y, row.width, row.height, markerWidth);
}

/*
=================
Menu_Draw
=================
*/
void Menu_Draw (menuFrameWork_t *menu)
{
	void *item;
	int i;
	int visibleIndex = 0;

	Menu_UpdateShowIf (menu);

	//
	// 1. Draw full-screen background artwork first (outside the menu strip)
	//
	if (menu->bar_image_loaded && menu->bar_image_handle)
	{
		int img_w = 0, img_h = 0;
		R_GetPicSize (&img_w, &img_h, menu->bar_image_handle);
		if (img_w > 0 && img_h > 0)
		{
			float alpha = ui_menu_bar_image_alpha ? ui_menu_bar_image_alpha->value : 0.20f;
			float final_alpha = alpha;
			if (final_alpha > 1.0f)
				final_alpha = 1.0f;
			if (final_alpha < 0.0f)
				final_alpha = 0.0f;

			if (final_alpha > 0.0f)
			{
				(R_SetColor) (MakeColor (255, 255, 255, (int)(final_alpha * 255)));

				const char *mode = ui_menu_bar_image_mode ? ui_menu_bar_image_mode->string : "screen";
				if (Q_stricmp (mode, "stretch") == 0 || Q_stricmp (mode, "fill") == 0)
				{
					R_DrawStretchPic (0, 0, uis.width, uis.height, menu->bar_image_handle);
				}
				else if (Q_stricmp (mode, "contain") == 0)
				{
					float scale_x = (float)uis.width / (float)img_w;
					float scale_y = (float)uis.height / (float)img_h;
					float scale = (scale_x < scale_y) ? scale_x : scale_y;
					int w = (int)(img_w * scale);
					int h = (int)(img_h * scale);
					int x = (uis.width - w) / 2;
					int y = (uis.height - h) / 2;
					R_DrawStretchPic (x, y, w, h, menu->bar_image_handle);
				}
				else
				{ // cover / screen
					float scale_x = (float)uis.width / (float)img_w;
					float scale_y = (float)uis.height / (float)img_h;
					float scale = (scale_x > scale_y) ? scale_x : scale_y;
					int w = (int)(img_w * scale);
					int h = (int)(img_h * scale);
					int x = (uis.width - w) / 2;
					int y = (uis.height - h) / 2;
					R_DrawStretchPic (x, y, w, h, menu->bar_image_handle);
				}

				R_ClearColor ();
			}
		}
	}

	//
	// Draw menu background strip (translucent overlay for readability)
	//
	if (menu->image)
	{
		R_DrawKeepAspectPic (0, menu->y1, uis.width,
			menu->y2 - menu->y1, menu->image);
	}
	else
	{
		R_DrawFill32 (0, menu->y1, uis.width,
			menu->y2 - menu->y1, UI_MenuBackgroundColor (menu));
	}

	//
	// draw title bar
	//
	if (menu->title)
	{
		int title_top = ui_menu_title_top_padding ? (int)ui_menu_title_top_padding->value : 12;
		if (menu->name && strcmp (menu->name, "servers") == 0)
		{
			title_top = 0;
		}
		int title_y = menu->y1 + title_top;
		Menu_SetColor (uis.color.title.u32);
		UI_DrawString (Menu_TitleX (menu), title_y,
			UI_CENTER, menu->title);
		R_ClearColor ();
	}

	//
	// draw banner, plaque and logo
	//
	Menu_DrawVideoTabs (menu);

	if (menu->banner)
	{
		R_DrawPic (menu->banner_rc.x, menu->banner_rc.y, menu->banner);
	}
	if (menu->plaque)
	{
		R_DrawPic (menu->plaque_rc.x, menu->plaque_rc.y, menu->plaque);
	}
	if (menu->logo)
	{
		R_DrawPic (menu->logo_rc.x, menu->logo_rc.y, menu->logo);
	}

	//
	// draw contents
	//
	Menu_SetNormalColor ();
	for (i = 0; i < menu->nitems; i++)
	{
		item = menu->items[i];
		if (((menuCommon_t *)item)->flags & QMF_HIDDEN)
		{
			continue;
		}
		// skip items scrolled off screen
		if (menu->maxVisible && visibleIndex < menu->scrollOffset)
		{
			visibleIndex++;
			continue;
		}
		if (menu->maxVisible && visibleIndex >= menu->scrollOffset + menu->maxVisible)
		{
			break;
		}

		menu_drawing_item_index = i;
		if (i == menu->currFocusedIndex && !menu->videoTabsFocused)
		{
			Menu_DrawFocusMarker (menu, item);
		}

		switch (((menuCommon_t *)item)->type)
		{
		case MTYPE_FIELD:
			Field_Draw (item);
			break;
		case MTYPE_SLIDER:
			Slider_Draw (item);
			break;
		case MTYPE_LIST:
			MenuList_Draw (item);
			break;
		case MTYPE_SPINCONTROL:
		case MTYPE_BITFIELD:
		case MTYPE_PAIRS:
		case MTYPE_VALUES:
		case MTYPE_STRINGS:
		case MTYPE_ACTION_SELECT:
		case MTYPE_TOGGLE:
			SpinControl_Draw (item);
			break;
		case MTYPE_ACTION:
		case MTYPE_SAVEGAME:
		case MTYPE_LOADGAME:
			Action_Draw (item);
			break;
		case MTYPE_SEPARATOR:
			Separator_Draw (item);
			break;
		case MTYPE_STATIC:
			Static_Draw (item);
			break;
		case MTYPE_KEYBIND:
			Keybind_Draw (item);
			break;
		case MTYPE_SELECT:
			Select_Draw (item);
			break;
		case MTYPE_SELECT2:
			Select2_Draw (item);
			break;
		case MTYPE_BINDSELECT2:
			BindSelect2_Draw (item);
			break;
		case MTYPE_BITMAP:
			Bitmap_Draw (item);
			break;
		default:
			Q_assert (!"unknown item type");
		}

		if (ui_debug->intvalue)
		{
			vrect_t content_rect;
			Menu_GetItemContentRect (item, &content_rect);
			UI_DrawRect8 (&content_rect, 1, 223);
		}

		visibleIndex++;
		menu_drawing_item_index = -1;
	}

	/* Draw open select dropdown on top of all items */
	if (g_openSelect)
	{
		if (g_openSelect->generic.type == MTYPE_SELECT)
			Select_DrawDropDown (g_openSelect);
		else if (g_openSelect->generic.type == MTYPE_SELECT2)
			Select2_DrawDropDown ((menuSelect2_t *)g_openSelect);
		else if (g_openSelect->generic.type == MTYPE_BINDSELECT2)
			BindSelect2_DrawDropDown ((menuBindSelect2_t *)g_openSelect);
	}

	// draw scroll indicators for scrollable menus
	if (menu->maxVisible && menu->maxVisible < Menu_CountVisibleItems (menu))
	{
		if (menu->scrollOffset > 0)
		{
			UI_DrawString (uis.width / 2, Menu_ScrollTop (menu) - CHAR_HEIGHT - 2,
				UI_CENTER | UI_ALTCOLOR, "...");
		}
		if (menu->scrollOffset + menu->maxVisible < Menu_CountVisibleItems (menu))
		{
			UI_DrawString (uis.width / 2, Menu_ScrollBottom (menu) + 2,
				UI_CENTER | UI_ALTCOLOR, "...");
		}
	}
	//
	// draw status bar
	//
	if (menu->status)
	{
		Menu_DrawStatus (menu);
	}
}

menuSound_t Menu_SelectItem (menuFrameWork_t *s)
{
	menuCommon_t *item;

	if (!(item = Menu_ItemAtCursor (s)))
	{
		return QMS_NOTHANDLED;
	}

	switch (item->type)
	{
	//case MTYPE_SLIDER:
	//    return Slider_DoSlide((menuSlider_t *)item, 1);
	case MTYPE_SPINCONTROL:
	case MTYPE_BITFIELD:
	case MTYPE_PAIRS:
	case MTYPE_VALUES:
	case MTYPE_STRINGS:
	case MTYPE_ACTION_SELECT:
	case MTYPE_TOGGLE:
		return SpinControl_DoEnter ((menuSpinControl_t *)item);
	case MTYPE_KEYBIND:
		return Keybind_DoEnter ((menuKeybind_t *)item);
	case MTYPE_SELECT:
		return Select_DoEnter ((menuSelect_t *)item);
	case MTYPE_SELECT2:
		return Select2_DoEnter ((menuSelect2_t *)item);
	case MTYPE_BINDSELECT2:
		return BindSelect2_DoEnter ((menuBindSelect2_t *)item);
	case MTYPE_FIELD:
	case MTYPE_ACTION:
	case MTYPE_LIST:
	case MTYPE_BITMAP:
	case MTYPE_SAVEGAME:
	case MTYPE_LOADGAME:
		return Common_DoEnter (item);
	default:
		return QMS_NOTHANDLED;
	}
}

menuSound_t Menu_SlideItem (menuFrameWork_t *s, int dir)
{
	menuCommon_t *item;

	if (!(item = Menu_ItemAtCursor (s)))
	{
		return QMS_NOTHANDLED;
	}

	switch (item->type)
	{
	case MTYPE_SLIDER:
		return Slider_DoSlide ((menuSlider_t *)item, dir);
	case MTYPE_SPINCONTROL:
	case MTYPE_BITFIELD:
	case MTYPE_PAIRS:
	case MTYPE_VALUES:
	case MTYPE_STRINGS:
	case MTYPE_ACTION_SELECT:
	case MTYPE_TOGGLE:
		return SpinControl_DoSlide ((menuSpinControl_t *)item, dir);
	default:
		return QMS_NOTHANDLED;
	}
}

menuSound_t Menu_KeyEvent (menuCommon_t *item, int key)
{
	if (item->keydown)
	{
		menuSound_t sound = item->keydown (item, key);
		if (sound != QMS_NOTHANDLED)
		{
			return sound;
		}
	}

	switch (item->type)
	{
	case MTYPE_FIELD:
		return Field_Key ((menuField_t *)item, key);
	case MTYPE_LIST:
		return MenuList_Key ((menuList_t *)item, key);
	case MTYPE_SLIDER:
		return Slider_Key ((menuSlider_t *)item, key);
	case MTYPE_KEYBIND:
		return Keybind_Key ((menuKeybind_t *)item, key);
	case MTYPE_SELECT:
		return Select_Key ((menuSelect_t *)item, key);
	case MTYPE_SELECT2:
		return Select2_Key ((menuSelect2_t *)item, key);
	case MTYPE_BINDSELECT2:
		return BindSelect2_Key ((menuBindSelect2_t *)item, key);
	default:
		return QMS_NOTHANDLED;
	}
}

menuSound_t Menu_CharEvent (menuCommon_t *item, int key)
{
	switch (item->type)
	{
	case MTYPE_FIELD:
		return Field_Char ((menuField_t *)item, key);
	case MTYPE_SELECT:
		return QMS_NOTHANDLED;
	default:
		return QMS_NOTHANDLED;
	}
}

bool Menu_HandleOpenSelect (void)
{
	if (!SelectAnyIsOpen ())
		return false;
	SelectAnyMouseMove ();
	return true;
}

menuSound_t Menu_MouseMove (menuCommon_t *item)
{
	switch (item->type)
	{
	case MTYPE_LIST:
		return MenuList_MouseMove ((menuList_t *)item);
	case MTYPE_SLIDER:
		return Slider_MouseMove ((menuSlider_t *)item);
	case MTYPE_SELECT:
		return Select_MouseMove ((menuSelect_t *)item);
	default:
		return QMS_NOTHANDLED;
	}
}

static menuSound_t Menu_DefaultKey (menuFrameWork_t *m, int key)
{
	menuCommon_t *item;

	switch (key)
	{
	case K_ESCAPE:
	case K_MOUSE2:
		/* Close open dropdown instead of popping menu */
		if (SelectAnyIsOpen ())
		{
			if (g_openSelect->generic.type == MTYPE_SELECT)
			{
				g_openSelect->open = false;
				g_openSelect->hoverIndex = -1;
			}
			else if (g_openSelect->generic.type == MTYPE_SELECT2)
			{
				menuSelect2_t *s2 = (menuSelect2_t *)g_openSelect;
				s2->open = false;
				s2->openIdx = -1;
				s2->hoverIndex = -1;
			}
			else
			{
				menuBindSelect2_t *bs = (menuBindSelect2_t *)g_openSelect;
				bs->open = false;
				bs->openIdx = -1;
				bs->hoverIndex = -1;
			}
			g_openSelect = NULL;
			return QMS_OUT;
		}
		UI_PopMenu ();
		return QMS_OUT;

	case K_KP_UPARROW:
	case K_UPARROW:
	case 'k':
	case K_KP_DOWNARROW:
	case K_DOWNARROW:
		if (SelectAnyIsOpen ())
		{
			menuSound_t snd;
			if (g_openSelect->generic.type == MTYPE_SELECT)
				snd = Select_Key (g_openSelect, key);
			else if (g_openSelect->generic.type == MTYPE_SELECT2)
				snd = Select2_Key ((menuSelect2_t *)g_openSelect, key);
			else
				snd = BindSelect2_Key ((menuBindSelect2_t *)g_openSelect, key);
			if (snd != QMS_NOTHANDLED)
				return snd;
		}
		if (key == K_KP_UPARROW || key == K_UPARROW || key == 'k')
			return Menu_AdjustCursor (m, -1);
		return Menu_AdjustCursor (m, 1);

	case K_TAB:
	case 'j':
		if (SelectAnyIsOpen ())
			return QMS_SILENT;
		return Menu_AdjustCursor (m, 1);
	case K_MWHEELDOWN:
		if (Menu_Scroll (m, 1))
		{
			return QMS_SILENT;
		}
		return Menu_SlideItem (m, -1);

	case K_MWHEELUP:
		if (Menu_Scroll (m, -1))
		{
			return QMS_SILENT;
		}
		return Menu_SlideItem (m, 1);

	case K_KP_LEFTARROW:
	case K_LEFTARROW:

	case 'h':
		return Menu_SlideItem (m, -1);

	case K_KP_RIGHTARROW:
	case K_RIGHTARROW:

	case 'l':
		return Menu_SlideItem (m, 1);

	case K_MOUSE1:
	//case K_MOUSE2:
	case K_MOUSE3:
		/* Handle open select dropdown clicks */
		if (SelectAnyIsOpen ())
		{
			if (g_openSelect->generic.type == MTYPE_SELECT)
			{
				int i;
				for (i = 0; i < g_openSelect->numItems; i++)
				{
					if (UI_CursorInRect (&g_openSelect->itemRects[i]))
					{
						g_openSelect->curvalue = g_openSelect->hoverIndex;
						g_openSelect->open = false;
						g_openSelect->hoverIndex = -1;
						Cvar_SetByVar (g_openSelect->cvar,
							g_openSelect->itemnames[g_openSelect->curvalue], FROM_MENU);
						if (g_openSelect->generic.change)
							g_openSelect->generic.change (&g_openSelect->generic);
						g_openSelect = NULL;
						return QMS_OUT;
					}
				}
				g_openSelect->open = false;
				g_openSelect->hoverIndex = -1;
				g_openSelect = NULL;
				return QMS_OUT;
			}
			else if (g_openSelect->generic.type == MTYPE_SELECT2)
			{
				menuSelect2_t *s2 = (menuSelect2_t *)g_openSelect;
				int i, side = s2->openIdx;
				for (i = 0; i < s2->numItems[side]; i++)
				{
					if (UI_CursorInRect (&s2->itemRects[i]))
					{
						if (s2->hoverIndex >= 0)
							s2->curvalue[side] = s2->hoverIndex;
						s2->open = false;
						s2->openIdx = -1;
						s2->hoverIndex = -1;
						Cvar_SetByVar (s2->cvar[side],
							s2->itemnames[side][s2->curvalue[side]], FROM_MENU);
						g_openSelect = NULL;
						return QMS_OUT;
					}
				}
				s2->open = false;
				s2->openIdx = -1;
				s2->hoverIndex = -1;
				g_openSelect = NULL;
				return QMS_OUT;
			}
			else
			{
				menuBindSelect2_t *bs = (menuBindSelect2_t *)g_openSelect;
				int i, side = bs->openIdx;
				for (i = 0; i < bs->numItems[side]; i++)
				{
					if (UI_CursorInRect (&bs->itemRects[i]))
					{
						BindSelect2_Select (bs, side, i);
						bs->open = false;
						bs->openIdx = -1;
						bs->hoverIndex = -1;
						g_openSelect = NULL;
						return QMS_OUT;
					}
				}
				bs->open = false;
				bs->openIdx = -1;
				bs->hoverIndex = -1;
				g_openSelect = NULL;
				return QMS_OUT;
			}
		}

		item = Menu_HitTest (m);
		if (!item)
		{
			return QMS_NOTHANDLED;
		}

		/* Allow clicking select/select2/bindselect2 to open dropdown without prior focus */
		if (item->type == MTYPE_SELECT || item->type == MTYPE_SELECT2 ||
			item->type == MTYPE_BINDSELECT2)
		{
			if (item->type == MTYPE_SELECT)
				return Select_DoEnter ((menuSelect_t *)item);
			else if (item->type == MTYPE_SELECT2)
			{
				/* Open the side the mouse is over */
				menuSelect2_t *s2 = (menuSelect2_t *)item;
				int xOff = s2->generic.x + RCOLUMN_OFFSET;
				int side = (uis.mouseCoords[0] > xOff + s2->colWidth[0] + CHAR_WIDTH * 2) ? 1 : 0;
				Select_CloseOther ();
				s2->open = true;
				s2->openIdx = side;
				s2->hoverIndex = s2->curvalue[side];
				g_openSelect = (menuSelect_t *)s2;
				return QMS_IN;
			}
			else
			{
				/* bindselect2: open the column the mouse is over */
				menuBindSelect2_t *bs = (menuBindSelect2_t *)item;
				int col = BindSelect2_ColumnAt (bs, uis.mouseCoords[0]);
				if (col == 0)
				{
					/* key bind mode */
					bs->generic.parent->keywait = true;
					bs->generic.parent->status = bs->altstatus;
					Key_WaitKey (keybind_cb, bs);
					return QMS_IN;
				}
				int side = col - 1;
				Select_CloseOther ();
				bs->open = true;
				bs->openIdx = side;
				bs->hoverIndex = bs->curvalue[side];
				bs->focusPart = col;
				g_openSelect = (menuSelect_t *)bs;
				return QMS_IN;
			}
		}

		if (!(item->flags & QMF_HASFOCUS))
		{
			return QMS_NOTHANDLED;
		}
		// fall through

	case K_KP_ENTER:
	case K_ENTER:
		return Menu_SelectItem (m);
	}

	return QMS_NOTHANDLED;
}

menuSound_t Menu_Keydown (menuFrameWork_t *menu, int key)
{
	menuCommon_t *item;
	menuSound_t sound;

	Menu_UpdateShowIf (menu);

	sound = Menu_VideoTabsKey (menu, key);
	if (sound != QMS_NOTHANDLED)
		return sound;

	if (menu->keywait)
	{
	}

	if (menu->keydown)
	{
		sound = menu->keydown (menu, key);
		if (sound != QMS_NOTHANDLED)
		{
			Menu_UpdateShowIf (menu);
			return sound;
		}
	}

	item = Menu_ItemAtCursor (menu);
	if (item)
	{
		sound = Menu_KeyEvent (item, key);
		if (sound != QMS_NOTHANDLED)
		{
			Menu_UpdateShowIf (menu);
			return sound;
		}
	}

	sound = Menu_DefaultKey (menu, key);
	Menu_UpdateShowIf (menu);
	return sound;
}

menuCommon_t *Menu_HitTest (menuFrameWork_t *menu)
{
	int i;
	int visibleIndex = 0;
	menuCommon_t *item;

	if (menu->keywait)
	{
		return NULL;
	}

	for (i = 0; i < menu->nitems; i++)
	{
		item = menu->items[i];
		if (item->flags & QMF_HIDDEN)
		{
			continue;
		}

		/* Scrolled-off video rows must not receive clicks behind the fixed tabs. */
		if (menu->videoApply && menu->maxVisible)
		{
			int index = visibleIndex++;
			if (index < menu->scrollOffset || index >= menu->scrollOffset + menu->maxVisible)
				continue;
		}

		if (UI_CursorInRect (&item->rect))
		{
			return item;
		}
	}

	return NULL;
}

static void Menu_LoadItem (void *item)
{
	switch (((menuCommon_t *)item)->type)
	{
	case MTYPE_SLIDER:
		Slider_Push (item);
		break;
	case MTYPE_BITFIELD:
		BitField_Push (item);
		((menuSpinControl_t *)item)->modified = false;
		break;
	case MTYPE_PAIRS:
		Pairs_Push (item);
		break;
	case MTYPE_VALUES:
	case MTYPE_STRINGS:
	case MTYPE_ACTION_SELECT:
		Strings_Push (item);
		break;
	case MTYPE_SPINCONTROL:
		SpinControl_Push (item);
		break;
	case MTYPE_TOGGLE:
		Toggle_Push (item);
		break;
	case MTYPE_KEYBIND:
		Keybind_Push (item);
		break;
	case MTYPE_SELECT:
		Select_Push (item);
		break;
	case MTYPE_SELECT2:
		Select2_Push (item);
		break;
	case MTYPE_BINDSELECT2:
		BindSelect2_Push (item);
		break;
	case MTYPE_FIELD:
		Field_Push (item);
		break;
	case MTYPE_SAVEGAME:
	case MTYPE_LOADGAME:
		Savegame_Push (item);
		break;
	default:
		break;
	}
}

static bool Menu_VideoItemEdited (const menuCommon_t *item)
{
	switch (item->type)
	{
	case MTYPE_SLIDER:
		return ((const menuSlider_t *)item)->modified;
	case MTYPE_BITFIELD:
	case MTYPE_PAIRS:
	case MTYPE_VALUES:
	case MTYPE_STRINGS:
	case MTYPE_ACTION_SELECT:
	case MTYPE_SPINCONTROL:
	case MTYPE_TOGGLE:
		return ((const menuSpinControl_t *)item)->modified;
	default:
		return false;
	}
}

/* The native display fields have no cvars; prepare only pending renderer controls. */
void Menu_VideoPrepare (menuFrameWork_t *menu)
{
	int i;

	for (i = 0; i < menu->nitems; i++)
	{
		menuCommon_t *item = menu->items[i];
		if (!(item->flags & QMF_DEFER_COMMIT) || !Menu_ItemCvar (item))
			continue;
		if (!menu->videoPendingInitialized || !Menu_VideoItemEdited (item))
			Menu_LoadItem (item);
	}
	menu->videoPendingInitialized = true;
}

bool Menu_Push (menuFrameWork_t *menu)
{
	int i;

	if (menu->videoApply)
		Menu_VideoPrepare (menu);
	for (i = 0; i < menu->nitems; i++)
	{
		menuCommon_t *item = menu->items[i];
		if (menu->videoApply && (item->flags & QMF_DEFER_COMMIT))
			continue;
		Menu_LoadItem (item);
	}
	return true;
}

static bool Item_WasModified (const menuCommon_t *item)
{
	switch (item->type)
	{
	case MTYPE_SLIDER:
	{
		const menuSlider_t *s = (const menuSlider_t *)item;
		return s->cvar && fabsf (s->curvalue - s->cvar->value) > 0.0001f;
	}
	case MTYPE_VALUES:
	case MTYPE_PAIRS:
	case MTYPE_STRINGS:
	case MTYPE_SPINCONTROL:
	case MTYPE_ACTION_SELECT:
	{
		const menuSpinControl_t *s = (const menuSpinControl_t *)item;
		if (!s->cvar || s->curvalue < 0 || s->curvalue >= s->numItems)
			return false;
		if (s->itemvalues)
			return strcmp (s->itemvalues[s->curvalue], s->cvar->string) != 0;
		return s->curvalue != s->cvar->intvalue;
	}
	case MTYPE_TOGGLE:
	{
		const menuSpinControl_t *s = (const menuSpinControl_t *)item;
		if (!s->cvar || s->curvalue < 0 || s->curvalue > 1)
			return false;
		return (s->curvalue ^ s->negate) != s->cvar->intvalue;
	}
	case MTYPE_BITFIELD:
	{
		const menuSpinControl_t *s = (const menuSpinControl_t *)item;
		int newval;
		if (!s->cvar)
			return false;
		newval = s->cvar->intvalue;
		if (s->curvalue ^ s->negate)
			newval |= s->mask;
		else
			newval &= ~s->mask;
		return newval != s->cvar->intvalue;
	}
	case MTYPE_FIELD:
	{
		const menuField_t *f = (const menuField_t *)item;
		return f->cvar && strcmp (f->field.text, f->cvar->string) != 0;
	}
	case MTYPE_SELECT:
	{
		const menuSelect_t *s = (const menuSelect_t *)item;
		if (!s->cvar || s->curvalue < 0 || s->curvalue >= s->numItems)
			return false;
		return strcmp (s->itemnames[s->curvalue], s->cvar->string) != 0;
	}
	case MTYPE_SELECT2:
	{
		const menuSelect2_t *s = (const menuSelect2_t *)item;
		int i;
		for (i = 0; i < 2; i++)
		{
			if (s->cvar[i] && s->curvalue[i] >= 0 && s->curvalue[i] < s->numItems[i])
				if (strcmp (s->itemnames[i][s->curvalue[i]], s->cvar[i]->string))
					return true;
		}
		return false;
	}
	default:
		return false;
	}
}

qboolean Menu_VideoPending (void)
{
	menuFrameWork_t *menu;
	int i;

	LIST_FOR_EACH (menuFrameWork_t, menu, &ui_menus, entry)
	{
		if (!menu->videoApply || !menu->videoPendingInitialized)
			continue;
		for (i = 0; i < menu->nitems; i++)
		{
			menuCommon_t *item = menu->items[i];
			if ((item->flags & QMF_DEFER_COMMIT) && Menu_VideoItemEdited (item) &&
				Item_WasModified (item))
				return true;
		}
	}
	return false;
}

void Menu_VideoDiscard (void)
{
	menuFrameWork_t *menu;
	int i;

	LIST_FOR_EACH (menuFrameWork_t, menu, &ui_menus, entry)
	{
		if (!menu->videoApply || !menu->videoPendingInitialized)
			continue;
		for (i = 0; i < menu->nitems; i++)
		{
			menuCommon_t *item = menu->items[i];
			if ((item->flags & QMF_DEFER_COMMIT) && Menu_ItemCvar (item))
				Menu_LoadItem (item);
		}
		Menu_UpdateShowIf (menu);
	}
}

qboolean Menu_VideoApply (void)
{
	menuFrameWork_t *menu;
	int i;
	qboolean changed = false;

	LIST_FOR_EACH (menuFrameWork_t, menu, &ui_menus, entry)
	{
		if (!menu->videoApply || !menu->videoPendingInitialized)
			continue;
		for (i = 0; i < menu->nitems; i++)
		{
			menuCommon_t *item = menu->items[i];
			if ((item->flags & QMF_DEFER_COMMIT) && Menu_VideoItemEdited (item) &&
				Item_WasModified (item))
			{
				Menu_CommitItem (item);
				changed = true;
			}
		}
	}
	Menu_VideoDiscard ();
	if (changed)
		Cbuf_AddText ("vid_restart force\n");
	return true;
}

/* Back retains drafts inside video setup; leaving the entire area discards them. */
void Menu_VideoClose (void)
{
	menuFrameWork_t *menu;
	int i;

	for (i = 0; i < uis.menuDepth; i++)
	{
		if (uis.layers[i] && uis.layers[i]->videoApply)
			return;
	}
	Menu_VideoDiscard ();
	LIST_FOR_EACH (menuFrameWork_t, menu, &ui_menus, entry)
	{
		menu->videoPendingInitialized = false;
		menu->videoTabsFocused = false;
	}
}

void Menu_Pop (menuFrameWork_t *menu)
{
	void *item;
	int i;
	bool need_restart = false;

	if (menu->videoApply)
	{
		Select_CloseOther ();
		return;
	}

	for (i = 0; i < menu->nitems; i++)
	{
		item = menu->items[i];

		if (((menuCommon_t *)item)->flags & QMF_DEFER_COMMIT &&
			Item_WasModified ((menuCommon_t *)item))
			need_restart = true;

		switch (((menuCommon_t *)item)->type)
		{
		case MTYPE_SLIDER:
		case MTYPE_BITFIELD:
		case MTYPE_PAIRS:
		case MTYPE_STRINGS:
		case MTYPE_ACTION_SELECT:
		case MTYPE_SPINCONTROL:
		case MTYPE_TOGGLE:
		case MTYPE_FIELD:
			Menu_CommitItem (item);
			break;
		case MTYPE_KEYBIND:
			Keybind_Pop (item);
			break;
		case MTYPE_SELECT:
			Select_Pop (item);
			break;
		case MTYPE_SELECT2:
			Select2_Pop (item);
			break;
		case MTYPE_BINDSELECT2:
			BindSelect2_Pop (item);
			break;
		default:
			break;
		}
	}

	if (need_restart)
		Cbuf_AddText ("vid_restart force\n");
}

void Menu_Free (menuFrameWork_t *menu)
{
	void *item;
	int i;

	for (i = 0; i < menu->nitems; i++)
	{
		item = menu->items[i];

		Z_Free (((menuCommon_t *)item)->show_if_cvar);
		Z_Free (((menuCommon_t *)item)->show_if_value);

		switch (((menuCommon_t *)item)->type)
		{
		case MTYPE_ACTION:
		case MTYPE_SAVEGAME:
		case MTYPE_LOADGAME:
			Action_Free (item);
			break;
		case MTYPE_STATIC:
			Static_Free (item);
			break;
		case MTYPE_SLIDER:
			Slider_Free (item);
			break;
		case MTYPE_BITFIELD:
		case MTYPE_TOGGLE:
			BitField_Free (item);
			break;
		case MTYPE_PAIRS:
			Pairs_Free (item);
			break;
		case MTYPE_SPINCONTROL:
		case MTYPE_STRINGS:
		case MTYPE_ACTION_SELECT:
			SpinControl_Free (item);
			break;
		case MTYPE_KEYBIND:
			Keybind_Free (item);
			break;
		case MTYPE_SELECT:
			Select_Free (item);
			break;
		case MTYPE_SELECT2:
			Select2_Free (item);
			break;
		case MTYPE_BINDSELECT2:
			BindSelect2_Free (item);
			break;
		case MTYPE_FIELD:
			Field_Free (item);
			break;
		case MTYPE_SEPARATOR:
			Z_Free (item);
			break;
		case MTYPE_BITMAP:
			Bitmap_Free (item);
			break;
		default:
			break;
		}
	}

	Z_Free (menu->items);
	Z_Free (menu->title);
	Z_Free (menu->name);
	Z_Free (menu);
}
