/* Bounded editor inputs and native menu bridge; no public engine ABI changes. */
#include "jump_ui_compat.h"
#ifdef _WIN32
#include <windows.h>
#endif
extern int keydown[256];
uiStatic_t uis;
static qboolean jump_window_active = true;

qboolean CL_JumpWindowActive (void)
{
	return jump_window_active;
}

void CL_JumpEditorFocus (qboolean active)
{
	jump_window_active = active;
	if (!active)
	{
		CL_JumpEditorReleaseInputs ();
		CL_JumpUIReleaseInputs ();
	}
}

int Key_IsDown (int key)
{
	return key >= 0 && key < 256 ? keydown[key] : 0;
}

int Key_GetDest (void)
{
	return cls.key_dest;
}

const char *Q_stristr (const char *text, const char *needle)
{
	size_t length = strlen (needle);
	for (; *text; text++)
		if (!Q_strncasecmp (text, needle, (int)length))
			return text;
	return length ? NULL : text;
}

extern void JUI_Menu_Init (menuFrameWork_t *menu);
extern void Menu_VideoClose (void);
extern qboolean JUI_VideoTabsAvailable (void);
extern void UI_Draw (unsigned realtime);
extern void UI_KeyEvent (int key, qboolean down);

static void CL_JumpMenuDraw (void)
{
	if (HUD_EditorActive ())
		CL_JumpEditorDraw ();
	else
		UI_Draw ((unsigned)cls.realtime);
}

static const char *CL_JumpMenuKey (int key)
{
	if (HUD_EditorActive ())
		HUD_EditorKey (key, true);
	else
		UI_KeyEvent (key, true);
	return NULL;
}

void UI_DrawString (int x, int y, int flags, const char *text)
{
	R_SetFont (uis.fontHandle);
	SCR_DrawString (x, y, flags, text);
	R_SetFont (NULL);
}

void UI_PushMenu (menuFrameWork_t *menu)
{
	int i;
	if (!menu)
		return;
	if (uis.menuDepth > 0 && uis.activeMenu && uis.activeMenu != menu &&
		uis.activeMenu->videoApply && menu->videoApply)
	{
		if (!JUI_VideoTabsAvailable ())
			return;
		if (menu->push && !menu->push (menu))
			return;
		CL_JumpUIReleaseInputs ();
		uis.mouseTracker = NULL;
		/* Keep one video frame in place so switching tabs retains all drafts. */
		if (uis.activeMenu->pop)
			uis.activeMenu->pop (uis.activeMenu);
		uis.layers[uis.menuDepth - 1] = menu;
	}
	else
	{
		for (i = 0; i < uis.menuDepth; i++)
			if (uis.layers[i] == menu)
				break;
		if (i == uis.menuDepth)
		{
			if (uis.menuDepth >= MAX_MENU_DEPTH)
				return;
			if (menu->push && !menu->push (menu))
				return;
			uis.layers[uis.menuDepth++] = menu;
		}
		else
			while (uis.menuDepth > i + 1)
				UI_PopMenu ();
	}
	if (!uis.activeMenu)
		M_JumpPushMenu (CL_JumpMenuDraw, CL_JumpMenuKey);
	uis.activeMenu = menu;
	if (menu->nitems)
		JUI_Menu_Init (menu);
	else if (menu->size)
		menu->size (menu);
	menu->openTime = (unsigned)cls.realtime;
	menu->closeTime = 0;
	uis.realtime = (unsigned)cls.realtime;
	uis.entersound = true;
	if (menu->expose)
		menu->expose (menu);
}

void UI_PopMenu (void)
{
	menuFrameWork_t *menu = uis.activeMenu;
	if (!menu)
		return;
	CL_JumpUIReleaseInputs ();
	if (menu->pop)
		menu->pop (menu);
	if (uis.menuDepth > 0)
		uis.layers[--uis.menuDepth] = NULL;
	uis.mouseTracker = NULL;
	uis.activeMenu = uis.menuDepth ? uis.layers[uis.menuDepth - 1] : NULL;
	Menu_VideoClose ();
	if (!uis.activeMenu)
		M_JumpPopMenu ();
}

qhandle_t R_RegisterPic (const char *name)
{
	static char names[4096][MAX_QPATH * 2];
	static int count;
	int i;
	if (!name || !*name || strlen (name) >= sizeof (names[0]) || !re.RegisterPic ((char *)name))
		return NULL;
	for (i = 0; i < count; i++)
		if (!strcmp (names[i], name))
			return names[i];
	if (count == (int)q_countof (names))
		return NULL;
	Q_strlcpy (names[count], name, sizeof (names[0]));
	return names[count++];
}

qboolean R_GetPicSize (int *width, int *height, qhandle_t pic)
{
	int w = -1, h = -1;
	if (pic)
		re.DrawGetPicSize (&w, &h, (char *)pic);
	if (width)
		*width = w;
	if (height)
		*height = h;
	return w > 0 && h > 0;
}

static qboolean jump_overstrike;

qboolean Key_GetOverstrikeMode (void)
{
	return jump_overstrike;
}

void Key_SetOverstrikeMode (qboolean state)
{
	jump_overstrike = state;
}

void CL_JumpFieldCopy (const char *text)
{
#ifdef _WIN32
	HGLOBAL memory;
	char *bytes;
	size_t length = strlen (text) + 1;
	if (!OpenClipboard (NULL))
		return;
	memory = GlobalAlloc (GMEM_MOVEABLE, length);
	if (!memory)
	{
		CloseClipboard ();
		return;
	}
	bytes = GlobalLock (memory);
	if (!bytes)
	{
		GlobalFree (memory);
		CloseClipboard ();
		return;
	}
	memcpy (bytes, text, length);
	GlobalUnlock (memory);
	EmptyClipboard ();
	if (!SetClipboardData (CF_TEXT, memory))
		GlobalFree (memory);
	CloseClipboard ();
#else
	(void)text;
#endif
}
