#define UI_C_IMPLEMENTATION
/*
Copyright (C) 2003-2006 Andrey Nazarov

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


LIST_DECL (ui_menus);

cvar_t *ui_debug;
cvar_t *cl_menu_cursor;
static cvar_t *ui_open;
static cvar_t *ui_scale;
static cvar_t *ui_draw_layers;

cvar_t *ui_menu_focus_width;
cvar_t *ui_menu_focus_padding_x;
cvar_t *ui_menu_focus_padding_y;
cvar_t *ui_menu_density;

cvar_t *ui_menu_bar_image;
cvar_t *ui_menu_bar_image_alpha;
cvar_t *ui_menu_bar_image_mode;

cvar_t *ui_menu_anim;
cvar_t *ui_menu_anim_focus_ms;
cvar_t *ui_menu_title_top_padding;
cvar_t *ui_menu_title_item_gap;

menuFrameWork_t *ui_drawing_menu = NULL;
int menu_drawing_item_index = -1;

typedef enum
{
	UI_MENU_COLOR_BACKGROUND,
	UI_MENU_COLOR_TITLE,
	UI_MENU_COLOR_NORMAL,
	UI_MENU_COLOR_SELECTABLE,
	UI_MENU_COLOR_ALTERNATE,
	UI_MENU_COLOR_ACTIVE,
	UI_MENU_COLOR_SELECTION,
	UI_MENU_COLOR_FOCUS,
	UI_MENU_COLOR_FOCUS_BORDER,
	UI_MENU_COLOR_DISABLED,
	UI_MENU_COLOR_LIST_HEADER,
	UI_MENU_COLOR_SCROLLBAR,
	UI_MENU_COLOR_HINT_BACKGROUND,
	UI_MENU_COLOR_HINT_TEXT,
	UI_MENU_COLOR_FOCUS_MARKER,
	UI_MENU_COLOR_VALUE,
	UI_MENU_COLOR_VALUE_ACTIVE,
	UI_MENU_COLOR_VALUE_CHANGED,
	UI_MENU_COLOR_SLIDER_TRACK,
	UI_MENU_COLOR_SLIDER_FILL,
	UI_MENU_COLOR_SLIDER_THUMB,
	UI_MENU_COLOR_SLIDER_BORDER,
	UI_MENU_COLOR_SORTED_HEADER,
	UI_MENU_COLOR_TAB_TEXT,
	UI_MENU_COLOR_TAB_ACTIVE_TEXT,
	UI_MENU_COLOR_TAB_ACTIVE_BG,
	UI_MENU_COLOR_TAB_INACTIVE_BG,
	UI_MENU_COLOR_PANEL_BORDER,
	UI_MENU_COLOR_PANEL_SHADOW,
	UI_MENU_COLOR_TAB_UNDERLINE,
	UI_MENU_COLOR_COUNT
} uiMenuColorId_t;

typedef struct
{
	const char *cvarName;
	const char *defaultValue;
} uiMenuCustomColor_t;

static cvar_t *ui_menu_custom_colors[UI_MENU_COLOR_COUNT];

static const uiColorStyle_t ui_slateColorStyle = {
	{ .u32 = MakeColor (28, 31, 34, 210) },
	{ .u32 = MakeColor (214, 218, 224, 255) },
	{ .u32 = MakeColor (232, 235, 240, 255) },
	{ .u32 = MakeColor (232, 235, 240, 255) },
	{ .u32 = MakeColor (255, 255, 255, 255) },
	{ .u32 = MakeColor (139, 150, 163, 255) },
	{ .u32 = MakeColor (95, 102, 112, 220) },
	{ .u32 = MakeColor (80, 84, 90, 150) },
	{ .u32 = MakeColor (180, 184, 190, 220) },
	{ .u32 = MakeColor (112, 116, 122, 255) }
};

static const uint32 ui_slateListHeaderColor = MakeColor (62, 68, 76, 255);
static const uint32 ui_slateScrollbarColor = MakeColor (50, 55, 62, 255);

static const uiMenuCustomColor_t ui_menu_custom_color_defs[UI_MENU_COLOR_COUNT] = {
	[UI_MENU_COLOR_BACKGROUND] = { "ui_menu_color_panel_bg", "8 12 18 160" },
	[UI_MENU_COLOR_TITLE] = { "ui_menu_color_title_text", "225 112 124 255" },
	[UI_MENU_COLOR_NORMAL] = { "ui_menu_color_text", "210 216 224 255" },
	[UI_MENU_COLOR_SELECTABLE] = { "ui_menu_color_item_text", "210 216 224 255" },
	[UI_MENU_COLOR_ALTERNATE] = { "ui_menu_color_label_text", "160 168 178 255" },
	[UI_MENU_COLOR_ACTIVE] = { "ui_menu_color_focus_text", "255 255 255 255" },
	[UI_MENU_COLOR_SELECTION] = { "ui_menu_color_selection_bg", "44 78 112 200" },
	[UI_MENU_COLOR_FOCUS] = { "ui_menu_color_focus_bg", "0 0 0 0" },
	[UI_MENU_COLOR_FOCUS_BORDER] = { "ui_menu_color_focus_edge", "90 130 170 100" },
	[UI_MENU_COLOR_DISABLED] = { "ui_menu_color_disabled_text", "86 90 98 255" },
	[UI_MENU_COLOR_LIST_HEADER] = { "ui_menu_color_list_header_bg", "82 40 50 235" },
	[UI_MENU_COLOR_SCROLLBAR] = { "ui_menu_color_scrollbar_track", "26 36 48 255" },
	[UI_MENU_COLOR_HINT_BACKGROUND] = { "ui_menu_color_hint_bg", "5 7 10 235" },
	[UI_MENU_COLOR_HINT_TEXT] = { "ui_menu_color_hint_text", "185 195 205 255" },

	[UI_MENU_COLOR_FOCUS_MARKER] = { "ui_menu_color_focus_marker", "170 70 83 220" },
	[UI_MENU_COLOR_VALUE] = { "ui_menu_color_value_text", "220 224 230 255" },
	[UI_MENU_COLOR_VALUE_ACTIVE] = { "ui_menu_color_value_active_text", "255 255 255 255" },
	[UI_MENU_COLOR_VALUE_CHANGED] = { "ui_menu_color_value_changed_text", "225 112 124 255" },
	[UI_MENU_COLOR_SLIDER_TRACK] = { "ui_menu_color_slider_track", "26 36 48 255" },
	[UI_MENU_COLOR_SLIDER_FILL] = { "ui_menu_color_slider_fill", "48 86 124 220" },
	[UI_MENU_COLOR_SLIDER_THUMB] = { "ui_menu_color_slider_thumb", "160 168 178 255" },
	[UI_MENU_COLOR_SLIDER_BORDER] = { "ui_menu_color_slider_border", "95 143 190 180" },
	[UI_MENU_COLOR_SORTED_HEADER] = { "ui_menu_color_sorted_header_bg", "95 45 56 235" },
	[UI_MENU_COLOR_TAB_TEXT] = { "ui_menu_color_tab_text", "185 195 205 255" },
	[UI_MENU_COLOR_TAB_ACTIVE_TEXT] = { "ui_menu_color_tab_active_text", "255 255 255 255" },
	[UI_MENU_COLOR_TAB_ACTIVE_BG] = { "ui_menu_color_tab_active_bg", "44 78 112 200" },
	[UI_MENU_COLOR_TAB_INACTIVE_BG] = { "ui_menu_color_tab_inactive_bg", "8 12 18 180" },
	[UI_MENU_COLOR_PANEL_BORDER] = { "ui_menu_color_panel_border", "44 58 72 160" },
	[UI_MENU_COLOR_PANEL_SHADOW] = { "ui_menu_color_panel_shadow", "0 0 0 120" },
	[UI_MENU_COLOR_TAB_UNDERLINE] = { "ui_menu_color_tab_underline", "225 112 124 220" }
};

// ===========================================================================

/*
=================
UI_Resize
=================
*/
static void UI_Resize (void)
{
	int i;

	uis.scale = R_ClampScale (ui_scale);
	uis.width = Q_rint (CL_JumpViewportWidth () * uis.scale);
	uis.height = Q_rint (CL_JumpViewportHeight () * uis.scale);

	for (i = 0; i < uis.menuDepth; i++)
	{
		Menu_Init (uis.layers[i]);
	}

	//CL_WarpMouse(0, 0);
}

/*
=================
UI_ForceMenuOff
=================
*/
void UI_ForceMenuOff (void)
{
	menuFrameWork_t *menu;
	qboolean owned = uis.activeMenu != NULL || uis.menuDepth > 0;
	int i;

	CL_JumpUIReleaseInputs ();
	for (i = uis.menuDepth; i > 0; i--)
	{
		menu = uis.layers[i - 1];
		if (menu->pop)
		{
			menu->pop (menu);
		}
		uis.layers[i - 1] = NULL;
	}
	uis.menuDepth = 0;
	uis.activeMenu = NULL;
	uis.mouseTracker = NULL;
	uis.transparent = false;
	Menu_VideoClose ();
	if (owned)
		M_JumpPopMenu ();
}

/*
=================
UI_IsTransparent
=================
*/
bool UI_IsTransparent (void)
{
	if (Key_GetDest () != key_menu)
	{
		return true;
	}

	if (!uis.activeMenu)
	{
		return true;
	}

	return uis.activeMenu->transparent;
}

bool UI_IsLive (void)
{
	if (Key_GetDest () != key_menu)
	{
		return false;
	}

	if (!uis.activeMenu)
	{
		return false;
	}

	return uis.activeMenu->live;
}

bool UI_IsMenuActive (const char *name)
{
	int i;

	if (Key_GetDest () != key_menu)
	{
		return false;
	}

	if (!name || !*name)
	{
		return false;
	}

	for (i = 0; i < uis.menuDepth; i++)
	{
		if (uis.layers[i] && uis.layers[i]->name &&
			!strcmp (uis.layers[i]->name, name))
		{
			return true;
		}
	}

	return false;
}

menuFrameWork_t *UI_FindMenu (const char *name)
{
	menuFrameWork_t *menu;
	const char *alias = NULL;

	LIST_FOR_EACH (menuFrameWork_t, menu, &ui_menus, entry)
	{
		if (!strcmp (menu->name, name))
		{
			return menu;
		}
	}

	if (ui_builtin_menu_active &&
		(!strcmp (name, "video_color") || !strcmp (name, "video_models") ||
			!strcmp (name, "video_materials")))
		alias = "video_graphics";
	if (alias)
	{
		LIST_FOR_EACH (menuFrameWork_t, menu, &ui_menus, entry)
		{
			if (!strcmp (menu->name, alias))
				return menu;
		}
	}

	return NULL;
}

/*
=================
UI_OpenMenu
=================
*/
void UI_OpenMenu (uiMenu_t type)
{
	menuFrameWork_t *menu = NULL;

	if (!uis.initialized)
	{
		return;
	}

	// close any existing menus
	UI_ForceMenuOff ();

	switch (type)
	{
	case UIMENU_DEFAULT:
		if (ui_open->intvalue)
		{
			menu = UI_FindMenu ("main");
		}
		break;
	case UIMENU_MAIN:
		menu = UI_FindMenu ("main");
		break;
	case UIMENU_GAME:
		menu = UI_FindMenu ("game");
		if (!menu)
		{
			menu = UI_FindMenu ("main");
		}
		break;
	case UIMENU_NONE:
		break;
	default:
		Q_assert (!"bad menu");
	}

	UI_PushMenu (menu);
}

//=============================================================================

/*
=================
UI_FormatColumns
=================
*/
void *UI_FormatColumns (int extrasize, ...)
{
	va_list argptr;
	char *buffer, *p;
	int i, j;
	size_t total = 0;
	char *strings[MAX_COLUMNS];
	size_t lengths[MAX_COLUMNS];

	va_start (argptr, extrasize);
	for (i = 0; i < MAX_COLUMNS; i++)
	{
		if ((p = va_arg (argptr, char *)) == NULL)
		{
			break;
		}
		strings[i] = p;
		total += lengths[i] = strlen (p) + 1;
	}
	va_end (argptr);

	buffer = UI_Malloc (extrasize + total + 1);
	p = buffer + extrasize;
	for (j = 0; j < i; j++)
	{
		memcpy (p, strings[j], lengths[j]);
		p += lengths[j];
	}
	*p = 0;

	return buffer;
}

char *UI_GetColumn (char *s, int n)
{
	int i;

	for (i = 0; i < n && *s; i++)
	{
		s += strlen (s) + 1;
	}

	return s;
}

/*
=================
UI_CursorInRect
=================
*/
bool UI_CursorInRect (const vrect_t *rect)
{
	if (uis.mouseCoords[0] < rect->x)
	{
		return false;
	}
	if (uis.mouseCoords[0] >= rect->x + rect->width)
	{
		return false;
	}
	if (uis.mouseCoords[1] < rect->y)
	{
		return false;
	}
	if (uis.mouseCoords[1] >= rect->y + rect->height)
	{
		return false;
	}
	return true;
}

void UI_DrawChar (int x, int y, int flags, int ch)
{
	R_DrawChar (x, y, flags, ch, uis.fontHandle);
}

void UI_StringDimensions (vrect_t *rc, int flags, const char *string)
{
	rc->height = CHAR_HEIGHT;
	rc->width = CHAR_WIDTH * strlen (string);

	if ((flags & UI_CENTER) == UI_CENTER)
	{
		rc->x -= rc->width / 2;
	}
	else if (flags & UI_RIGHT)
	{
		rc->x -= rc->width;
	}
}

void UI_DrawRect8 (const vrect_t *rc, int border, int c)
{
	R_DrawFill8 (rc->x, rc->y, border, rc->height, c);											  // left
	R_DrawFill8 (rc->x + rc->width - border, rc->y, border, rc->height, c);						  // right
	R_DrawFill8 (rc->x + border, rc->y, rc->width - border * 2, border, c);						  // top
	R_DrawFill8 (rc->x + border, rc->y + rc->height - border, rc->width - border * 2, border, c); // bottom
}

//=============================================================================
/* Menu Subsystem */

/*
=================
UI_DoHitTest
=================
*/
bool UI_DoHitTest (void)
{
	menuCommon_t *item;

	if (!uis.activeMenu)
	{
		return false;
	}

	Menu_UpdateShowIf (uis.activeMenu);

	/* Always update open select dropdown hover state */
	if (Menu_HandleOpenSelect ())
	{
		/* Dropdown is open: don't change menu focus */
		return false;
	}

	if (uis.mouseTracker)
	{
		item = uis.mouseTracker;
	}
	else
	{
		if (!(item = Menu_HitTest (uis.activeMenu)))
		{
			return false;
		}
	}

	if (!UI_IsItemSelectable (item))
	{
		return false;
	}

	Menu_MouseMove (item);

	if (item->flags & QMF_HASFOCUS)
	{
		return false;
	}

	Menu_SetFocus (item);

	return true;
}

/*
=================
UI_MouseEvent
=================
*/
void UI_MouseEvent (int x, int y)
{
	x = Q_clip (x, 0, CL_JumpViewportWidth () - 1);
	y = Q_clip (y, 0, CL_JumpViewportHeight () - 1);

	uis.mouseCoords[0] = Q_rint (x * uis.scale);
	uis.mouseCoords[1] = Q_rint (y * uis.scale);

	if (HUD_EditorActive ())
	{
		HUD_EditorMouse (x, y);
		return;
	}

	UI_DoHitTest ();
}

static bool UI_ShouldDrawCursor (void)
{
	return cl_menu_cursor && UI_GetCursorTextureName (cl_menu_cursor->string) != NULL;
}

static int UI_ClampColorComponent (int value)
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

static bool UI_ParseMenuColor (const char *s, color_t *color)
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

	color->u8[0] = UI_ClampColorComponent (r);
	color->u8[1] = UI_ClampColorComponent (g);
	color->u8[2] = UI_ClampColorComponent (b);
	color->u8[3] = UI_ClampColorComponent (a);
	return true;
}

uiMenuStyleId_t UI_MenuStyleId (void)
{
	return UI_MENU_STYLE_CUSTOM;
}

static void UI_ApplyCustomColor (uiMenuColorId_t id, color_t *color)
{
	color_t parsed;
	cvar_t *cvar = ui_menu_custom_colors[id];

	if (!cvar)
	{
		return;
	}

	if (UI_ParseMenuColor (cvar->string, &parsed))
	{
		*color = parsed;
	}
}

static void UI_ApplyCustomColor32 (uiMenuColorId_t id, uint32 *color)
{
	color_t parsed;
	cvar_t *cvar = ui_menu_custom_colors[id];

	if (!cvar)
	{
		return;
	}

	if (UI_ParseMenuColor (cvar->string, &parsed))
	{
		*color = parsed.u32;
	}
}

static void UI_ApplyMenuStyle (void)
{
	uis.color = ui_slateColorStyle;
	uis.listHeaderColor = ui_slateListHeaderColor;
	uis.scrollbarColor = ui_slateScrollbarColor;
	uis.hintBackgroundColor = MakeColor (18, 20, 23, 235);
	uis.hintTextColor = uis.color.normal.u32;

	uis.focusMarkerColor = MakeColor (180, 184, 190, 220);
	uis.valueColor = MakeColor (232, 235, 240, 255);
	uis.valueActiveColor = MakeColor (255, 255, 255, 255);
	uis.valueChangedColor = MakeColor (225, 112, 124, 255);
	uis.sliderTrackColor = MakeColor (50, 55, 62, 255);
	uis.sliderFillColor = MakeColor (95, 102, 112, 220);
	uis.sliderThumbColor = MakeColor (255, 255, 255, 255);
	uis.sliderBorderColor = MakeColor (180, 184, 190, 220);
	uis.sortedHeaderColor = ui_slateListHeaderColor;
	uis.tabTextColor = MakeColor (232, 235, 240, 255);
	uis.tabActiveTextColor = MakeColor (255, 255, 255, 255);
	uis.tabActiveBgColor = MakeColor (95, 102, 112, 220);
	uis.tabInactiveBgColor = MakeColor (28, 31, 34, 210);


	uis.panelBorderColor = MakeColor (44, 58, 72, 160);
	uis.panelShadowColor = MakeColor (0, 0, 0, 120);

	uis.tabUnderlineColor = MakeColor (225, 112, 124, 220);

	UI_ApplyCustomColor (UI_MENU_COLOR_BACKGROUND, &uis.color.background);
	UI_ApplyCustomColor (UI_MENU_COLOR_TITLE, &uis.color.title);
	UI_ApplyCustomColor (UI_MENU_COLOR_NORMAL, &uis.color.normal);
	UI_ApplyCustomColor (UI_MENU_COLOR_SELECTABLE, &uis.color.selectable);
	UI_ApplyCustomColor (UI_MENU_COLOR_ALTERNATE, &uis.color.alternate);
	UI_ApplyCustomColor (UI_MENU_COLOR_ACTIVE, &uis.color.active);
	UI_ApplyCustomColor (UI_MENU_COLOR_SELECTION, &uis.color.selection);
	UI_ApplyCustomColor (UI_MENU_COLOR_FOCUS, &uis.color.focus);
	UI_ApplyCustomColor (UI_MENU_COLOR_FOCUS_BORDER, &uis.color.focus_border);
	UI_ApplyCustomColor (UI_MENU_COLOR_DISABLED, &uis.color.disabled);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_LIST_HEADER, &uis.listHeaderColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_SCROLLBAR, &uis.scrollbarColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_HINT_BACKGROUND, &uis.hintBackgroundColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_HINT_TEXT, &uis.hintTextColor);

	UI_ApplyCustomColor32 (UI_MENU_COLOR_FOCUS_MARKER, &uis.focusMarkerColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_VALUE, &uis.valueColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_VALUE_ACTIVE, &uis.valueActiveColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_VALUE_CHANGED, &uis.valueChangedColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_SLIDER_TRACK, &uis.sliderTrackColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_SLIDER_FILL, &uis.sliderFillColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_SLIDER_THUMB, &uis.sliderThumbColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_SLIDER_BORDER, &uis.sliderBorderColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_SORTED_HEADER, &uis.sortedHeaderColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_TAB_TEXT, &uis.tabTextColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_TAB_ACTIVE_TEXT, &uis.tabActiveTextColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_TAB_ACTIVE_BG, &uis.tabActiveBgColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_TAB_INACTIVE_BG, &uis.tabInactiveBgColor);


	UI_ApplyCustomColor32 (UI_MENU_COLOR_PANEL_BORDER, &uis.panelBorderColor);
	UI_ApplyCustomColor32 (UI_MENU_COLOR_PANEL_SHADOW, &uis.panelShadowColor);

	UI_ApplyCustomColor32 (UI_MENU_COLOR_TAB_UNDERLINE, &uis.tabUnderlineColor);

	uis.styleFocusFill = true;
	uis.styleMenuBackground = true;
}

uint32 UI_MenuBackgroundColor (const menuFrameWork_t *menu)
{
	return uis.styleMenuBackground ? uis.color.background.u32 : menu->color.u32;
}

uint32 UI_MenuListHeaderColor (void)
{
	return uis.listHeaderColor;
}

uint32 UI_MenuScrollbarColor (void)
{
	return uis.scrollbarColor;
}

uint32 UI_MenuHintBackgroundColor (void)
{
	return uis.hintBackgroundColor;
}

uint32 UI_MenuHintTextColor (void)
{
	return uis.hintTextColor;
}

uint32 UI_MenuFocusMarkerColor (void)
{
	return uis.focusMarkerColor;
}

uint32 UI_MenuValueColor (void)
{
	return uis.valueColor;
}

uint32 UI_MenuValueActiveColor (void)
{
	return uis.valueActiveColor;
}

uint32 UI_MenuValueChangedColor (void)
{
	return uis.valueChangedColor;
}

uint32 UI_MenuSliderTrackColor (void)
{
	return uis.sliderTrackColor;
}

uint32 UI_MenuSliderFillColor (void)
{
	return uis.sliderFillColor;
}

uint32 UI_MenuSliderThumbColor (void)
{
	return uis.sliderThumbColor;
}

uint32 UI_MenuSliderBorderColor (void)
{
	return uis.sliderBorderColor;
}

uint32 UI_MenuSortedHeaderColor (void)
{
	return uis.sortedHeaderColor;
}

uint32 UI_MenuTabTextColor (void)
{
	return uis.tabTextColor;
}

uint32 UI_MenuTabActiveTextColor (void)
{
	return uis.tabActiveTextColor;
}

uint32 UI_MenuTabActiveBgColor (void)
{
	return uis.tabActiveBgColor;
}

uint32 UI_MenuTabInactiveBgColor (void)
{
	return uis.tabInactiveBgColor;
}

bool UI_MenuStyleFocusFill (void)
{
	return uis.styleFocusFill;
}

int UI_MenuSpacing (void)
{
	const char *density;
	char *end;
	long offset;
	int base = GENERIC_SPACING (CHAR_HEIGHT);

	if (!ui_menu_density || !ui_menu_density->string || !ui_menu_density->string[0])
	{
		return base;
	}

	density = ui_menu_density->string;

	if (Q_stricmp (density, "compact") == 0 ||
		Q_stricmp (density, "dense") == 0 ||
		Q_stricmp (density, "small") == 0)
	{
		return base;
	}

	if (Q_stricmp (density, "normal") == 0 ||
		Q_stricmp (density, "default") == 0 ||
		Q_stricmp (density, "medium") == 0)
	{
		return base;
	}

	if (Q_stricmp (density, "spacious") == 0 ||
		Q_stricmp (density, "large") == 0 ||
		Q_stricmp (density, "big") == 0)
	{
		return base + 8;
	}

	offset = strtol (density, &end, 10);

	/*
     * Only accept real numeric values.
     * Prevents "abc" from behaving like 0.
     */
	if (end == density || *end != '\0')
	{
		return base;
	}

	/*
     * Clamp custom spacing offset.
     * With base 10:
     * Negative offsets keep the minimum 10-pixel row height.
     * 16 gives spacing 26
     */
	if (offset < -4)
	{
		offset = -4;
	}
	else if (offset > 16)
	{
		offset = 16;
	}

	return max (base, base + (int)offset);
}

uint32 UI_MenuPanelBorderColor (void)
{
	return uis.panelBorderColor;
}

uint32 UI_MenuPanelShadowColor (void)
{
	return uis.panelShadowColor;
}

uint32 UI_MenuTabUnderlineColor (void)
{
	return uis.tabUnderlineColor;
}

static void UI_ResetMenuColors_f (void)
{
	int i;

	for (i = 0; i < UI_MENU_COLOR_COUNT; i++)
	{
		if (ui_menu_custom_colors[i])
		{
			Cvar_SetByVar (ui_menu_custom_colors[i],
				Cvar_DefaultString (ui_menu_custom_colors[i]),
				FROM_MENU);
		}
	}

	Com_Printf ("Menu colors reset to defaults.\n");
}

static const char *ui_netmeter_cvars[] = {
	"hud_network_scale",
	"sh_netmeter",
	"sh_netmeter_adaptive",
	"sh_netmeter_min_ms",
	"sh_netmeter_max_ms",
	"sh_lagometer_x",
	"sh_lagometer_y",
	"sh_lagometer_alpha",
	"sh_lagometer_bad_alpha",
	"sh_lagometer_color_normal",
	"sh_lagometer_color_spike",
	"sh_lagometer_color_jitter",
	"sh_lagometer_color_loss_s2c",
	"sh_lagometer_color_loss_c2s",
	"sh_netgraph_y",
	"sh_netgraph_height",
	"sh_netgraph_alpha",
	"sh_netgraph_color_normal",
	"sh_netgraph_color_spike",
	"sh_netgraph_color_jitter",
	"sh_netgraph_color_loss_s2c",
	"sh_netgraph_color_loss_c2s",
	"sh_histogram_x",
	"sh_histogram_y",
	"sh_histogram_width_mode",
	"sh_histogram_width",
	"sh_histogram_height",
	"sh_histogram_fill_mode",
	"sh_histogram_spacing_mode",
	"sh_histogram_bg_alpha",
	"sh_histogram_color_bg",
	"sh_histogram_color_normal",
	"sh_histogram_color_spike",
	"sh_histogram_color_jitter",
	"sh_histogram_color_loss_s2c",
	"sh_histogram_color_loss_c2s",
	"sh_histogram_alpha",
	"sh_histogram_bad_alpha",
	"sh_histogram_history_ms",
	"sh_histogram_ping",
	"sh_netalert",
	"sh_netalert_x",
	"sh_netalert_y",
	"sh_netalert_color",
	"sh_netalert_alpha",
	"sh_netalert_duration_ms",
	"sh_netalert_loss",
	"sh_netalert_jitter",
	"sh_netalert_spike",
	"sh_netwarn_ping_adaptive",
	"sh_netwarn_spike_ms",
	"sh_netwarn_spike_pct",
	"sh_netwarn_jitter_ms"
};

static void UI_ResetNetMeterSettings_f (void)
{
	size_t i;
	int reset = 0;

	for (i = 0; i < q_countof (ui_netmeter_cvars); i++)
	{
		cvar_t *var = Cvar_FindVar (ui_netmeter_cvars[i]);
		if (!var || !Cvar_DefaultString (var))
		{
			continue;
		}

		Cvar_SetByVar (var, Cvar_DefaultString (var), FROM_MENU);
		reset++;
	}

	Com_Printf ("Network meter settings reset to defaults (%d cvars).\n", reset);
}

/*
=================
UI_Draw
=================
*/
void UI_Draw (unsigned realtime)
{
	int i;
	static char prev_focus_width[32] = "";
	static char prev_density[32] = "";
	static float prev_scale = -1.0f;
	bool cvar_changed = false;

	uis.realtime = realtime;

	if (Key_GetDest () != key_menu)
	{
		return;
	}

	if (!uis.activeMenu)
	{
		return;
	}

	if (ui_menu_focus_width && strcmp (ui_menu_focus_width->string, prev_focus_width) != 0)
	{
		Q_strlcpy (prev_focus_width, ui_menu_focus_width->string, sizeof (prev_focus_width));
		cvar_changed = true;
	}
	if (ui_menu_density && strcmp (ui_menu_density->string, prev_density) != 0)
	{
		Q_strlcpy (prev_density, ui_menu_density->string, sizeof (prev_density));
		cvar_changed = true;
	}
	if (ui_scale && ui_scale->value != prev_scale)
	{
		prev_scale = ui_scale->value;
		cvar_changed = true;
	}
	if (cvar_changed)
	{
		for (i = 0; i < uis.menuDepth; i++)
		{
			if (uis.layers[i])
			{
				uis.layers[i]->focusInitialized = false;
				Menu_Layout (uis.layers[i]);
			}
		}
	}

	if (!scr_rich_presentation)
		return;
	if (uis.width != Q_rint (CL_JumpViewportWidth () * R_ClampScale (ui_scale)) ||
		uis.height != Q_rint (CL_JumpViewportHeight () * R_ClampScale (ui_scale)) ||
		uis.scale != R_ClampScale (ui_scale))
		UI_Resize ();
	scr_rich_presentation->Begin ();
	UI_ApplyMenuStyle ();

	R_ClearColor ();
	R_SetScale (uis.scale);

	/* Compact menus must also erase pixels outside their panel when no world draws. */
	if (cls.state != ca_active || !cl.refresh_prepped)
		(R_DrawFill32) (0, 0, uis.width, uis.height, 0xff000000);

	if (!ui_draw_layers->intvalue)
	{
		// draw top menu
		if (uis.activeMenu->draw)
		{
			uis.activeMenu->draw (uis.activeMenu);
		}
		else
		{
			Menu_Draw (uis.activeMenu);
		}
	}
	else
	{
		// draw all layers
		for (i = 0; i < uis.menuDepth; i++)
		{
			if (uis.layers[i]->draw)
			{
				uis.layers[i]->draw (uis.layers[i]);
			}
			else
			{
				Menu_Draw (uis.layers[i]);
			}
		}
	}


	// Cursor rendering must not inherit text tint or menu transition alpha.
	if (UI_ShouldDrawCursor ())
	{
		R_ClearColor ();
		if (uis.cursorHandle)
		{
			R_DrawPic (uis.mouseCoords[0] - uis.cursorWidth / 2,
				uis.mouseCoords[1] - uis.cursorHeight / 2, uis.cursorHandle);
		}
		else
		{
			int x = uis.mouseCoords[0], y = uis.mouseCoords[1];
			(R_DrawFill32) (x - 5, y - 2, 11, 5, 0xff000000);
			(R_DrawFill32) (x - 2, y - 5, 5, 11, 0xff000000);
			(R_DrawFill32) (x - 4, y, 9, 1, 0xffffffff);
			(R_DrawFill32) (x, y - 4, 1, 9, 0xffffffff);
		}
	}

	if (ui_debug->intvalue)
	{
		UI_DrawString (uis.width - 4, 4, UI_RIGHT,
			va ("%3i %3i", uis.mouseCoords[0], uis.mouseCoords[1]));
	}

	// delay playing the enter sound until after the
	// menu has been drawn, to avoid delay while
	// caching images
	if (uis.entersound)
	{
		uis.entersound = false;
		S_StartLocalSound ("misc/menu1.wav");
	}

	R_ClearColor ();
	R_SetScale (1.0f);
	R_SetClipRect (NULL);
	R_SetFont (NULL);
	scr_rich_presentation->End ();
}

void UI_StartSound (menuSound_t sound)
{
	switch (sound)
	{
	case QMS_IN:
		S_StartLocalSound ("misc/menu1.wav");
		break;
	case QMS_MOVE:
		S_StartLocalSound ("misc/menu2.wav");
		break;
	case QMS_OUT:
		S_StartLocalSound ("misc/menu3.wav");
		break;
	case QMS_BEEP:
		S_StartLocalSound ("misc/talk1.wav");
		break;
	default:
		break;
	}
}

/*
=================
UI_KeyEvent
=================
*/
void UI_KeyEvent (int key, bool down)
{
	menuSound_t sound;

	if (!uis.activeMenu)
	{
		return;
	}

	if (HUD_EditorKey (key, down))
		return;

	if (!down)
	{
		if (key == K_MOUSE1)
		{
			uis.mouseTracker = NULL;
		}
		return;
	}

	sound = Menu_Keydown (uis.activeMenu, key);

	UI_StartSound (sound);
}

/*
=================
UI_CharEvent
=================
*/
void UI_CharEvent (int key)
{
	menuCommon_t *item;
	menuSound_t sound;

	if (!uis.activeMenu)
	{
		return;
	}

	if (uis.activeMenu->videoTabsFocused)
		return;

	if (HUD_EditorChar (key))
		return;

	Menu_UpdateShowIf (uis.activeMenu);

	if ((item = Menu_ItemAtCursor (uis.activeMenu)) == NULL ||
		(sound = Menu_CharEvent (item, key)) == QMS_NOTHANDLED)
	{
		Menu_UpdateShowIf (uis.activeMenu);
		return;
	}

	UI_StartSound (sound);
	Menu_UpdateShowIf (uis.activeMenu);
}

static void UI_Menu_g (genctx_t *ctx)
{
	menuFrameWork_t *menu;

	LIST_FOR_EACH (menuFrameWork_t, menu, &ui_menus, entry)
	Prompt_AddMatch (ctx, menu->name);
}

static void UI_PushMenu_c (genctx_t *ctx, int argnum)
{
	if (argnum == 1)
	{
		UI_Menu_g (ctx);
	}
}

static void UI_PushMenu_f (void)
{
	menuFrameWork_t *menu;
	char *s;

	if (!scr_rich_presentation || !CL_JumpEnabled ())
		return;
	CL_JumpUITouch ();
	CL_JumpUICaptureArgs ();
	if (Cmd_Argc () < 2)
	{
		Com_Printf ("Usage: %s <menu>\n", Cmd_Argv (0));
		return;
	}
	s = Cmd_Argv (1);
	menu = UI_FindMenu (s);
	if (menu)
	{
		UI_PushMenu (menu);
	}
	else
	{
		Com_Printf ("No such menu: %s\n", s);
	}
}

static void UI_PopMenu_f (void)
{
	if (uis.activeMenu)
	{
		UI_PopMenu ();
	}
}

static void UI_ApplyVideo_f (void)
{
	if (!JUI_VideoApplyChanges ())
		UI_StartSound (QMS_BEEP);
}

static const cmdreg_t c_ui[] = {
	{ "forcemenuoff", UI_ForceMenuOff },
	{ "pushmenu", UI_PushMenu_f, UI_PushMenu_c },
	{ "popmenu", UI_PopMenu_f },
	{ "ui_apply_video", UI_ApplyVideo_f },
	{ "ui_discard_video", JUI_VideoDiscardChanges },
	{ "ui_reset_menu_colors", UI_ResetMenuColors_f },
	{ "ui_reset_netmeter_settings", UI_ResetNetMeterSettings_f },

	{ NULL, NULL }
};

static void ui_scale_changed (cvar_t *self, char *oldValue, char *newValue)
{
	UI_Resize ();
}

void UI_ModeChanged (void)
{
	ui_scale = Cvar_Get ("ui_scale", "0", 0);
	ui_scale->changed = ui_scale_changed;
	UI_Resize ();
}

static void UI_FreeMenus (void)
{
	menuFrameWork_t *menu, *next;

	LIST_FOR_EACH_SAFE (menuFrameWork_t, menu, next, &ui_menus, entry)
	{
		if (menu->free)
		{
			menu->free (menu);
		}
	}
	List_Init (&ui_menus);
}

const char *UI_GetCursorTextureName (const char *name)
{
	if (!name || !*name || Q_strcasecmp (name, "none") == 0 || strcmp (name, "0") == 0)
	{
		return NULL;
	}
	if (Q_strcasecmp (name, "cross") == 0 || strcmp (name, "1") == 0 || Q_strcasecmp (name, "ch1") == 0)
	{
		return "ch1";
	}
	if (Q_strcasecmp (name, "dot") == 0 || strcmp (name, "2") == 0 || Q_strcasecmp (name, "ch2") == 0)
	{
		return "ch2";
	}
	if (Q_strcasecmp (name, "angle") == 0 || strcmp (name, "3") == 0 || Q_strcasecmp (name, "ch3") == 0)
	{
		return "ch3";
	}
	if (strcmp (name, "4") == 0 || Q_strcasecmp (name, "ch4") == 0)
	{
		return "ch4";
	}
	if (strcmp (name, "5") == 0 || Q_strcasecmp (name, "ch5") == 0)
	{
		return "ch5";
	}
	return name;
}

void UI_SetCursor (const char *name)
{
	const char *tex_name = UI_GetCursorTextureName (name);
	if (!tex_name)
	{
		uis.cursorHandle = 0;
		uis.cursorWidth = 0;
		uis.cursorHeight = 0;
		return;
	}

	qhandle_t handle = R_RegisterPic (tex_name);
	if (handle)
	{
		uis.cursorHandle = handle;
		R_GetPicSize (&uis.cursorWidth, &uis.cursorHeight, uis.cursorHandle);
	}
	else
	{
		uis.cursorHandle = 0;
		uis.cursorWidth = 0;
		uis.cursorHeight = 0;
	}
}

static void cl_menu_cursor_changed (cvar_t *self, char *oldValue, char *newValue)
{
	if (Q_strcasecmp (self->string, "none") == 0 || strcmp (self->string, "0") == 0)
	{
		self->intvalue = 0;
	}
	else if (Q_strcasecmp (self->string, "cross") == 0 || strcmp (self->string, "1") == 0 || Q_strcasecmp (self->string, "ch1") == 0)
	{
		self->intvalue = 1;
	}
	else if (Q_strcasecmp (self->string, "dot") == 0 || strcmp (self->string, "2") == 0 || Q_strcasecmp (self->string, "ch2") == 0)
	{
		self->intvalue = 2;
	}
	else if (Q_strcasecmp (self->string, "angle") == 0 || strcmp (self->string, "3") == 0 || Q_strcasecmp (self->string, "ch3") == 0)
	{
		self->intvalue = 3;
	}
	else if (strcmp (self->string, "4") == 0 || Q_strcasecmp (self->string, "ch4") == 0)
	{
		self->intvalue = 4;
	}
	else if (strcmp (self->string, "5") == 0 || Q_strcasecmp (self->string, "ch5") == 0)
	{
		self->intvalue = 5;
	}
	else
	{
		self->intvalue = -1;
	}

	UI_SetCursor (self->string);
}

static const char *UI_NormalizeMenuFocusWidth (const char *value)
{
	if (!value || !value[0])
	{
		return "content";
	}

	if (!Q_strcasecmp (value, "full"))
	{
		return "full";
	}

	return "content";
}

static void ui_menu_focus_width_changed (cvar_t *self, char *oldValue, char *newValue)
{
	const char *value = UI_NormalizeMenuFocusWidth (self->string);

	if (strcmp (self->string, value) != 0)
	{
		Cvar_SetByVar (self, value, FROM_CODE);
	}
}

static void UI_MenuFocusWidth_g (genctx_t *ctx)
{
	Prompt_AddMatch (ctx, "full");
	Prompt_AddMatch (ctx, "content");
}

/*
=================
CL_JumpUIInit
=================
*/
void CL_JumpUIInit (void)
{
	int i;

	Cmd_Register (c_ui);

	ui_debug = Cvar_Get ("ui_debug", "0", 0);
	ui_open = Cvar_Get ("ui_open", "0", 0);
	ui_draw_layers = Cvar_Get ("ui_draw_layers", "0", 0);

	for (i = 0; i < UI_MENU_COLOR_COUNT; i++)
	{
		ui_menu_custom_colors[i] =
			Cvar_Get (ui_menu_custom_color_defs[i].cvarName,
				ui_menu_custom_color_defs[i].defaultValue,
				CVAR_ARCHIVE);
	}

	ui_menu_focus_width = Cvar_Get ("ui_menu_focus_width", "full", CVAR_ARCHIVE);
	ui_menu_focus_width->changed = ui_menu_focus_width_changed;
	ui_menu_focus_width_changed (ui_menu_focus_width, NULL, NULL);

	ui_menu_focus_padding_x = Cvar_Get ("ui_menu_focus_padding_x", "14", CVAR_ARCHIVE);
	ui_menu_focus_padding_y = Cvar_Get ("ui_menu_focus_padding_y", "3", CVAR_ARCHIVE);
	ui_menu_density = Cvar_Get ("ui_menu_density", "2", CVAR_ARCHIVE);

	ui_menu_bar_image = Cvar_Get ("ui_menu_bar_image", "q2jump_background", CVAR_ARCHIVE);
	ui_menu_bar_image_alpha = Cvar_Get ("ui_menu_bar_image_alpha", "0.5", CVAR_ARCHIVE);
	ui_menu_bar_image_mode = Cvar_Get ("ui_menu_bar_image_mode", "screen", CVAR_ARCHIVE);

	ui_menu_anim = Cvar_Get ("ui_menu_anim", "1", CVAR_ARCHIVE);
	ui_menu_anim_focus_ms = Cvar_Get ("ui_menu_anim_focus_ms", "70", CVAR_ARCHIVE);
	ui_menu_title_top_padding = Cvar_Get ("ui_menu_title_top_padding", "12", CVAR_ARCHIVE);
	ui_menu_title_item_gap = Cvar_Get ("ui_menu_title_item_gap", "0", CVAR_ARCHIVE);

	cl_menu_cursor = Cvar_Get ("cl_menu_cursor", "ch1", CVAR_ARCHIVE);
	cl_menu_cursor->changed = cl_menu_cursor_changed;

	UI_ModeChanged ();

	uis.fontHandle = R_RegisterFont ("conchars");
	cl_menu_cursor_changed (cl_menu_cursor, NULL, NULL);

	{
		int i;
		for (i = 0; i < NUM_CURSOR_FRAMES; i++)
		{
			uis.bitmapCursors[i] = R_RegisterPic (va ("m_cursor%d", i));
		}
	}

	uis.color.background.u32 = MakeColor (0, 0, 0, 255);
	uis.color.title.u32 = MakeColor (15, 128, 235, 255);
	uis.color.normal.u32 = MakeColor (15, 128, 235, 255);
	uis.color.selectable.u32 = MakeColor (15, 128, 235, 255);
	uis.color.alternate.u32 = MakeColor (255, 255, 255, 255);
	uis.color.active.u32 = MakeColor (15, 128, 235, 255);
	uis.color.selection.u32 = MakeColor (15, 128, 235, 255);
	uis.color.focus.u32 = MakeColor (80, 80, 80, 110);
	uis.color.focus_border.u32 = MakeColor (180, 180, 180, 160);
	uis.color.disabled.u32 = MakeColor (127, 127, 127, 255);
	uis.listHeaderColor = uis.color.normal.u32;
	uis.scrollbarColor = uis.color.normal.u32;
	uis.hintBackgroundColor = MakeColor (0, 0, 255, 255);
	uis.hintTextColor = MakeColor (255, 255, 255, 255);
	uis.focusMarkerColor = MakeColor (180, 180, 180, 160);
	uis.valueColor = MakeColor (15, 128, 235, 255);
	uis.valueActiveColor = MakeColor (15, 128, 235, 255);
	uis.valueChangedColor = MakeColor (225, 112, 124, 255);
	uis.sliderTrackColor = MakeColor (80, 80, 80, 110);
	uis.sliderFillColor = MakeColor (15, 128, 235, 255);
	uis.sliderThumbColor = MakeColor (255, 255, 255, 255);
	uis.sliderBorderColor = MakeColor (180, 180, 180, 160);
	uis.sortedHeaderColor = uis.color.normal.u32;
	uis.tabTextColor = uis.color.normal.u32;
	uis.tabActiveTextColor = uis.color.alternate.u32;
	uis.tabActiveBgColor = uis.color.focus.u32;
	uis.tabInactiveBgColor = uis.color.background.u32;
	uis.styleFocusFill = false;
	uis.styleMenuBackground = false;

	strcpy (uis.weaponModel, "w_railgun.md2");

	// load custom menus
	UI_LoadScript ();

	// load built-in menus
	JUI_Menu_PlayerConfig ();
	JUI_Menu_Servers ();
	JUI_Menu_Demos ();
	JUI_Menu_Video ();


	Com_DPrintf ("Registered %d menus.\n", List_Count (&ui_menus));

	uis.initialized = true;
}

/*
=================
CL_JumpUIShutdown
=================
*/
void CL_JumpUIShutdown (void)
{
	if (!uis.initialized)
	{
		return;
	}

	UI_ForceMenuOff ();


	ui_scale->changed = NULL;

	PlayerModel_Free ();

	UI_FreeMenus ();
	CL_JumpBrowserUIFree ();
	Menu_FreeColorPicker ();

	Cmd_Deregister (c_ui);

	memset (&uis, 0, sizeof (uis));

	Z_LeakTest (TAG_UI);
}

/*
=================
Rendering wrappers for animation fades and atmosphere overlay
=================
*/

float Menu_Ease01 (float t)
{
	if (t < 0.0f)
		return 0.0f;
	if (t > 1.0f)
		return 1.0f;
	return t * t * (3.0f - 2.0f * t); // smoothstep
}

static uint32 Menu_LerpColor (uint32 from_color, uint32 to_color, float t)
{
	color_t c1, c2, c3;
	c1.u32 = from_color;
	c2.u32 = to_color;
	c3.u8[0] = (byte)(c1.u8[0] + (c2.u8[0] - c1.u8[0]) * t);
	c3.u8[1] = (byte)(c1.u8[1] + (c2.u8[1] - c1.u8[1]) * t);
	c3.u8[2] = (byte)(c1.u8[2] + (c2.u8[2] - c1.u8[2]) * t);
	c3.u8[3] = (byte)(c1.u8[3] + (c2.u8[3] - c1.u8[3]) * t);
	return c3.u32;
}

static uint32 UI_InterpolateColor (uint32 color)
{
	if (!ui_menu_anim || !ui_menu_anim->intvalue)
	{
		return color;
	}
	if (!ui_drawing_menu || menu_drawing_item_index == -1)
	{
		return color;
	}

	int focus_ms = ui_menu_anim_focus_ms ? ui_menu_anim_focus_ms->intvalue : 90;
	if (focus_ms <= 0 || ui_drawing_menu->focusAnimStartTime == 0)
	{
		return color;
	}

	float elapsed = (float)(uis.realtime - ui_drawing_menu->focusAnimStartTime);
	float focus_t = elapsed / (float)focus_ms;
	if (focus_t >= 1.0f)
	{
		return color;
	}
	if (focus_t < 0.0f)
	{
		focus_t = 0.0f;
	}

	float eased = Menu_Ease01 (focus_t);

	if (menu_drawing_item_index == ui_drawing_menu->currFocusedIndex)
	{
		// Gaining focus: transition from selectable/normal to active color
		if (color == uis.color.active.u32)
		{
			return Menu_LerpColor (uis.color.selectable.u32, uis.color.active.u32, eased);
		}
	}
	else if (menu_drawing_item_index == ui_drawing_menu->prevFocusedIndex)
	{
		// Losing focus: transition from active to selectable/normal color
		if (color == uis.color.selectable.u32 || color == uis.color.normal.u32)
		{
			return Menu_LerpColor (uis.color.active.u32, color, eased);
		}
	}

	return color;
}

static uint32 UI_ProcessColor (uint32 color)
{
	return UI_InterpolateColor (color);
}

void UI_SetColor_Wrapper (uint32 color)
{
	(R_SetColor) (UI_ProcessColor (color));
}

void UI_SetAltColor_Wrapper (uint32 color)
{
	(R_SetAltColor) (UI_ProcessColor (color));
}

void UI_ClearColor_Wrapper (void)
{
	(R_ClearColor) ();
}

void UI_DrawFill32_Wrapper (int x, int y, int w, int h, uint32 color)
{
	(R_DrawFill32) (x, y, w, h, UI_ProcessColor (color));
}

void CL_JumpUIRegister (void)
{
	Cmd_Register (c_ui);
}
