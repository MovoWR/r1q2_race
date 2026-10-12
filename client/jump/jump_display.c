/* Display trials adapted from Q2PRO's committed client/display.c controller.
 * Copyright (C) id Software / Q2PRO contributors.
 * GNU General Public License, version 2 or later.
 * Native checkpoints retain the HWND/DC/GL context; no cvar changes until Keep.
 */
#include "jump_display.h"
#include "../ref_display.h"
#include "../vid_config.h"

static const refdisplay_t *display_api;

static struct
{
	qboolean initialized, active, applying, committing, focused;
	unsigned int started, checked, missing_polls;
	jump_display_settings_t previous, requested;
	char kept_display[32];
	char message[160];
} display_trial;

static void DisplayMessage (const char *text)
{
	Com_sprintf (display_trial.message, sizeof (display_trial.message), "%s", text ? text : "");
}

void CL_JumpDisplayInit (void)
{
	if (display_trial.initialized)
		return;
	display_trial.initialized = true;
	display_trial.focused = true;
	Cvar_Get ("vid_display", "", CVAR_ARCHIVE);
	Cvar_Get ("win_borderless", "0", CVAR_ARCHIVE);
	Cvar_Get ("win_notitle", "0", CVAR_ARCHIVE);
	Cvar_Get ("win_noresize", "0", CVAR_ARCHIVE);
	Cvar_Get ("_vid_fullscreen_borderless", Cvar_VariableValue ("vid_borderless") ? "1" : "0", CVAR_ARCHIVE);
	Cvar_Get ("_vid_fullscreen_mode", "1", CVAR_ARCHIVE);
}

qboolean CL_JumpDisplayAvailable (void)
{
	CL_JumpDisplayInit ();
	return display_api && display_api->Available ();
}

qboolean CL_JumpDisplayBusy (void)
{
	return display_trial.active || display_trial.applying || display_trial.committing;
}

qboolean CL_JumpDisplayActive (void)
{
	return display_trial.active;
}

int CL_JumpDisplayMonitors (jump_display_monitor_t *out, int capacity)
{
	int count;
	if (!out || capacity <= 0 || !CL_JumpDisplayAvailable ())
		return 0;
	if (capacity > JUMP_DISPLAY_MAX_MONITORS)
		capacity = JUMP_DISPLAY_MAX_MONITORS;
	count = display_api->Monitors (out, capacity);
	return count < 0 ? 0 : count > capacity ? capacity :
											  count;
}

int CL_JumpDisplayModes (const char *display, jump_display_mode_t *out, int capacity)
{
	int count, length;
	if (!display)
		return 0;
	for (length = 0; length < 32 && display[length]; length++)
		;
	if (length == 32 || !out || capacity <= 0 || !CL_JumpDisplayAvailable ())
		return 0;
	if (capacity > JUMP_DISPLAY_MAX_MODES)
		capacity = JUMP_DISPLAY_MAX_MODES;
	count = display_api->Modes (display, out, capacity);
	return count < 0 ? 0 : count > capacity ? capacity :
											  count;
}

static qboolean DisplayExists (const char *id)
{
	jump_display_monitor_t monitors[JUMP_DISPLAY_MAX_MONITORS];
	int count, i;
	count = CL_JumpDisplayMonitors (monitors, JUMP_DISPLAY_MAX_MONITORS);
	for (i = 0; i < count; i++)
		if (!strcmp (id, monitors[i].id))
			return true;
	return false;
}

static int DisplaySeconds (void)
{
	unsigned int elapsed;
	elapsed = (unsigned int)Sys_Milliseconds () - display_trial.started;
	return !display_trial.active || elapsed >= JUMP_DISPLAY_CONFIRM_MSEC ? 0 :
																		   (int)((JUMP_DISPLAY_CONFIRM_MSEC - elapsed + 999u) / 1000u);
}

void CL_JumpDisplaySnapshot (jump_display_snapshot_t *out)
{
	if (!out)
		return;
	memset (out, 0, sizeof (*out));
	out->available = CL_JumpDisplayAvailable ();
	if (out->available && !display_api->Read (&out->current, &out->capabilities))
		out->available = false;
	out->active = display_trial.active;
	out->applying = display_trial.applying;
	out->committing = display_trial.committing;
	out->seconds = DisplaySeconds ();
	out->previous = display_trial.previous;
	out->requested = display_trial.requested;
	Com_sprintf (out->message, sizeof (out->message), "%s", display_trial.message);
}

static qboolean DisplayValid (const jump_display_settings_t *s)
{
	if (!s || !memchr (s->display, 0, sizeof (s->display)) ||
		s->mode < JUMP_DISPLAY_WINDOWED || s->mode > JUMP_DISPLAY_EXCLUSIVE ||
		(s->window_flags & ~JUMP_DISPLAY_WINDOW_FLAGS) ||
		s->refresh < 0 || s->refresh == 1 || s->refresh > 1000 ||
		(s->depth && s->depth != 16 && s->depth != 24 && s->depth != 32))
		return false;
	if (s->mode == JUMP_DISPLAY_WINDOWED)
		return s->window.width >= 320 && s->window.width <= 8192 &&
			s->window.height >= 240 && s->window.height <= 8192 &&
			s->window.x >= -32767 && s->window.x <= 32767 &&
			s->window.y >= -32767 && s->window.y <= 32767;
	return s->mode == JUMP_DISPLAY_BORDERLESS ||
		((!s->width || (s->width >= 320 && s->width <= 8192)) &&
			(!s->height || (s->height >= 240 && s->height <= 8192)));
}

static qboolean DisplayEqual (const jump_display_settings_t *a, const jump_display_settings_t *b)
{
	if (a->mode != b->mode || strcmp (a->display, b->display) || a->vsync != b->vsync)
		return false;
	if (a->mode == JUMP_DISPLAY_WINDOWED)
		return !memcmp (&a->window, &b->window, sizeof (a->window)) && a->window_flags == b->window_flags;
	return a->mode == JUMP_DISPLAY_BORDERLESS ||
		(a->width == b->width && a->height == b->height && a->refresh == b->refresh &&
			a->depth == b->depth);
}

static qboolean DisplayMatches (const jump_display_settings_t *s, const jump_display_settings_t *actual)
{
	if (s->mode != actual->mode || strcmp (s->display, actual->display) || s->vsync != actual->vsync)
		return false;
	if (s->mode == JUMP_DISPLAY_WINDOWED)
		return s->window_flags == actual->window_flags;
	return s->mode == JUMP_DISPLAY_BORDERLESS ||
		((!s->width || s->width == actual->width) && (!s->height || s->height == actual->height) &&
			(!s->refresh || s->refresh == actual->refresh) && (!s->depth || s->depth == actual->depth));
}

void CL_JumpDisplayRevert (void)
{
	qboolean restored;
	if (!display_trial.active || display_trial.applying || display_trial.committing)
		return;
	display_trial.applying = true;
	restored = display_api && display_api->End (false);
	DisplayMessage (display_api ? display_api->Message () : NULL);
	if (!display_trial.message[0])
		DisplayMessage (restored ? "Previous display settings restored." : "Could not restore the display. Reconnect a monitor and retry.");
	display_trial.active = display_trial.applying = false;
}

static qboolean DisplayApply (const jump_display_settings_t *settings, qboolean force)
{
	jump_display_settings_t requested, actual, compare;
	jump_display_monitor_t monitors[JUMP_DISPLAY_MAX_MONITORS];
	unsigned int capabilities;
	int count, i, index;
	qboolean applied;
	if (!DisplayValid (settings) || CL_JumpDisplayBusy () || !CL_JumpDisplayAvailable ())
		return false;
	if (!display_api->Read (&display_trial.previous, &capabilities))
		return false;
	requested = *settings;
	if (!requested.display[0])
	{
		count = CL_JumpDisplayMonitors (monitors, JUMP_DISPLAY_MAX_MONITORS);
		if (!count)
			return false;
		index = 0;
		for (i = 0; i < count; i++)
			if (monitors[i].primary)
				index = i;
		Com_sprintf (requested.display, sizeof (requested.display), "%s", monitors[index].id);
	}
	if (!DisplayExists (requested.display))
	{
		DisplayMessage ("The selected display is unavailable.");
		return false;
	}
	if (!(capabilities & JUMP_DISPLAY_CAP_VSYNC) && requested.vsync != display_trial.previous.vsync)
	{
		DisplayMessage ("This renderer cannot change VSync.");
		return false;
	}
	if (requested.vsync < 0 && !(capabilities & JUMP_DISPLAY_CAP_ADAPTIVE))
		requested.vsync = requested.vsync == (-2147483647 - 1) ? 2147483647 : -requested.vsync;
	if (!force && DisplayEqual (&requested, &display_trial.previous))
		return true;
	display_trial.active = display_trial.applying = true;
	DisplayMessage ("");
	if (!display_api->Begin ())
	{
		DisplayMessage (display_api->Message ());
		display_trial.active = display_trial.applying = false;
		return false;
	}
	applied = display_api->Apply (&requested, &actual);
	if (applied)
		applied = display_api->Read (&actual, &capabilities) && DisplayMatches (&requested, &actual);
	display_trial.applying = false;
	if (!applied || !display_trial.focused)
	{
		CL_JumpDisplayRevert ();
		if (!applied)
		{
			char restored[160];
			Com_sprintf (restored, sizeof (restored), "%s", display_trial.message);
			Com_sprintf (display_trial.message, sizeof (display_trial.message), "Requested settings failed. %s", restored);
		}
		return false;
	}
	display_trial.requested = actual;
	if (actual.mode != JUMP_DISPLAY_WINDOWED && display_trial.previous.mode == JUMP_DISPLAY_WINDOWED)
		display_trial.requested.window = display_trial.previous.window;
	display_trial.started = display_trial.checked = (unsigned int)Sys_Milliseconds ();
	compare = display_trial.previous;
	compare.vsync = requested.vsync;
	/* VSync alone cannot hide the display, so it commits immediately. */
	if (DisplayEqual (&requested, &compare))
		return CL_JumpDisplayConfirm ();
	return true;
}

qboolean CL_JumpDisplayApply (const jump_display_settings_t *settings)
{
	return DisplayApply (settings, false);
}

static void DisplaySet (const char *name, const char *value)
{
	cvar_t *var = Cvar_ForceSet (name, value);
	if (var)
	{
		var->flags |= CVAR_ARCHIVE;
		var->modified = false; /* Already applied to the current window/context. */
	}
}

static void DisplaySetInteger (const char *name, int value)
{
	char text[32];
	Com_sprintf (text, sizeof (text), "%d", value);
	DisplaySet (name, text);
}

static int DisplayFullscreenIndex (const jump_display_settings_t *s, char **expanded)
{
	const char *list, *p, *start;
	char token[64];
	size_t length, list_length;
	int index, found;
	vid_displaymode_t mode;
	const char *parsed;
	*expanded = NULL;
	list = Cvar_VariableString ("vid_modelist");
	list_length = strlen (list);
	if (list_length > 131072)
		return 0;
	Com_sprintf (token, sizeof (token), "%dx%d@%d:%d", s->width, s->height, s->refresh, s->depth);
	length = strlen (token);
	index = 1;
	found = 0;
	for (p = list; *p && index <= VID_MAX_DISPLAY_MODES + 1; index++)
	{
		p = VID_SkipSpace (p);
		if (!*p)
			break;
		start = p;
		parsed = p;
		if (!VID_ParseDisplayMode (&parsed, &mode))
			return 0;
		while (*p && VID_SkipSpace (p) == p)
			p++;
		if ((size_t)(p - start) == length && !strncmp (start, token, length))
			found = index;
	}
	if (found)
		return found;
	if (index > VID_MAX_DISPLAY_MODES + 1)
		return 0;
	*expanded = malloc (list_length + length + 2);
	if (!*expanded)
		return 0;
	Com_sprintf (*expanded, (int)(list_length + length + 2), "%s%s%s", list, list_length ? " " : "", token);
	return index;
}

qboolean CL_JumpDisplayConfirm (void)
{
	jump_display_settings_t actual, settings;
	unsigned int capabilities;
	char *expanded, geometry[64];
	int index;
	if (!display_trial.active || display_trial.applying || display_trial.committing || !display_api)
		return false;
	if (!DisplaySeconds () || !DisplayExists (display_trial.requested.display) ||
		!display_api->Read (&actual, &capabilities) || !DisplayMatches (&display_trial.requested, &actual))
	{
		CL_JumpDisplayRevert ();
		return false;
	}
	settings = display_trial.requested;
	if (settings.mode == JUMP_DISPLAY_WINDOWED)
		settings.window = actual.window;
	index = 0;
	expanded = NULL;
	if (settings.mode == JUMP_DISPLAY_EXCLUSIVE && !(index = DisplayFullscreenIndex (&settings, &expanded)))
	{
		CL_JumpDisplayRevert ();
		{
			char restored[160];
			Com_sprintf (restored, sizeof (restored), "%s", display_trial.message);
			Com_sprintf (display_trial.message, sizeof (display_trial.message), "Could not preserve fullscreen mode list. %s", restored);
		}
		return false;
	}
	display_trial.committing = true;
	if (!display_api->End (true))
	{
		free (expanded);
		display_trial.committing = false;
		CL_JumpDisplayRevert ();
		return false;
	}
	DisplaySet ("vid_display", settings.display);
	DisplaySetInteger ("gl_swapinterval", settings.vsync);
	DisplaySetInteger ("vid_fullscreen", settings.mode != JUMP_DISPLAY_WINDOWED);
	DisplaySetInteger ("vid_borderless", settings.mode == JUMP_DISPLAY_BORDERLESS);
	DisplaySetInteger ("win_borderless", !!(settings.window_flags & JUMP_DISPLAY_WINDOW_BORDERLESS));
	DisplaySetInteger ("win_notitle", !!(settings.window_flags & JUMP_DISPLAY_WINDOW_NOTITLE));
	DisplaySetInteger ("win_noresize", !!(settings.window_flags & JUMP_DISPLAY_WINDOW_NORESIZE));
	if (settings.window.width >= 320 && settings.window.width <= 8192 &&
		settings.window.height >= 240 && settings.window.height <= 8192)
	{
		Com_sprintf (geometry, sizeof (geometry), "%dx%d%+d%+d", settings.window.width,
			settings.window.height, settings.window.x, settings.window.y);
		DisplaySet ("vid_geometry", geometry);
	}
	if (settings.mode == JUMP_DISPLAY_WINDOWED)
	{
		DisplaySetInteger ("vid_mode", 0);
		if (display_trial.previous.mode != JUMP_DISPLAY_WINDOWED)
			DisplaySetInteger ("_vid_fullscreen_borderless", display_trial.previous.mode == JUMP_DISPLAY_BORDERLESS);
	}
	else
	{
		DisplaySetInteger ("_vid_fullscreen_borderless", settings.mode == JUMP_DISPLAY_BORDERLESS);
		if (settings.mode == JUMP_DISPLAY_EXCLUSIVE)
		{
			if (expanded)
				DisplaySet ("vid_modelist", expanded);
			else
				Cvar_Get ("vid_modelist", NULL, 0)->flags |= CVAR_ARCHIVE;
			DisplaySetInteger ("vid_mode", index);
			DisplaySetInteger ("_vid_fullscreen_mode", index);
		}
	}
	free (expanded);
	display_trial.active = display_trial.committing = false;
	Com_sprintf (display_trial.kept_display, sizeof (display_trial.kept_display), "%s", settings.display);
	display_trial.checked = (unsigned int)Sys_Milliseconds ();
	display_trial.missing_polls = 0;
	DisplayMessage ("Display settings kept.");
	return true;
}

void CL_JumpDisplayFrame (void)
{
	jump_display_monitor_t monitors[JUMP_DISPLAY_MAX_MONITORS];
	jump_display_settings_t fallback, actual;
	unsigned int capabilities;
	int count, i, index;
	unsigned int now;
	if (display_trial.applying || display_trial.committing || !CL_JumpDisplayAvailable ())
		return;
	now = (unsigned int)Sys_Milliseconds ();
	if (display_trial.active)
	{
		if (!DisplaySeconds ())
			CL_JumpDisplayRevert ();
		else if (now - display_trial.checked >= 250u)
		{
			display_trial.checked = now;
			if (!DisplayExists (display_trial.requested.display))
				CL_JumpDisplayRevert ();
		}
		return;
	}
	if (!display_trial.kept_display[0] || now - display_trial.checked < 250u)
		return;
	display_trial.checked = now;
	count = CL_JumpDisplayMonitors (monitors, JUMP_DISPLAY_MAX_MONITORS);
	if (!count)
	{
		display_trial.missing_polls = 0;
		return;
	}
	index = 0;
	for (i = 0; i < count; i++)
	{
		if (!strcmp (display_trial.kept_display, monitors[i].id))
		{
			display_trial.missing_polls = 0;
			return;
		}
		if (monitors[i].primary)
			index = i;
	}
	if (++display_trial.missing_polls < 2)
		return;
	if (!display_api->Read (&actual, &capabilities))
		return;
	memset (&fallback, 0, sizeof (fallback));
	Com_sprintf (fallback.display, sizeof (fallback.display), "%s", monitors[index].id);
	fallback.mode = JUMP_DISPLAY_WINDOWED;
	fallback.window.x = monitors[index].desktop.x;
	fallback.window.y = monitors[index].desktop.y;
	fallback.window.width = 640;
	fallback.window.height = 480;
	fallback.vsync = actual.vsync;
	if (DisplayApply (&fallback, true) && (!display_trial.active || CL_JumpDisplayConfirm ()))
	{
		if (display_trial.previous.mode == JUMP_DISPLAY_EXCLUSIVE)
			DisplaySetInteger ("_vid_fullscreen_mode", 1);
		DisplayMessage ("Saved display disappeared. Restored a window on an available monitor.");
	}
}

void CL_JumpDisplayFocus (qboolean active)
{
	CL_JumpDisplayInit ();
	display_trial.focused = active;
	if (!active)
		CL_JumpDisplayRevert ();
}

void CL_JumpDisplayClose (void)
{
	CL_JumpDisplayRevert ();
}

void CL_JumpDisplaySetAPI (const refdisplay_t *api)
{
	if (api == display_api)
		return;
	CL_JumpDisplayClose ();
	display_api = api && api->version == REF_DISPLAY_VERSION && api->size >= (int)sizeof (*api) &&
			api->Available && api->Monitors && api->Modes && api->Read && api->Begin && api->Apply && api->End && api->Message ?
		api :
		NULL;
	CL_JumpDisplayInit ();
	Com_sprintf (display_trial.kept_display, sizeof (display_trial.kept_display), "%s",
		display_api ? Cvar_VariableString ("vid_display") : "");
	display_trial.checked = (unsigned int)Sys_Milliseconds ();
	display_trial.missing_polls = 0;
}

qboolean CL_JumpDisplayToggle (void)
{
	jump_display_settings_t settings;
	vid_displaymode_t selected;
	unsigned int capabilities;
	if (display_trial.active)
	{
		CL_JumpDisplayRevert ();
		Com_Printf ("ALT+Enter: %s\n", LOG_CLIENT, display_trial.message);
		return true;
	}
	if (!CL_JumpDisplayAvailable ())
		return false;
	if (!display_api->Read (&settings, &capabilities))
	{
		DisplayMessage ("Could not read current display settings.");
		Com_Printf ("ALT+Enter: %s\n", LOG_CLIENT, display_trial.message);
		return true;
	}
	if (settings.mode != JUMP_DISPLAY_WINDOWED)
		settings.mode = JUMP_DISPLAY_WINDOWED;
	else
	{
		settings.mode = Cvar_VariableValue ("_vid_fullscreen_borderless") ? JUMP_DISPLAY_BORDERLESS : JUMP_DISPLAY_EXCLUSIVE;
		if (VID_GetModeFromList (Cvar_VariableString ("vid_modelist"),
				(int)Cvar_VariableValue ("_vid_fullscreen_mode"), &selected) &&
			!selected.desktop)
		{
			settings.width = selected.width;
			settings.height = selected.height;
			settings.refresh = selected.frequency;
			settings.depth = selected.depth;
		}
		else
			settings.width = settings.height = settings.refresh = settings.depth = 0;
	}
	if (CL_JumpDisplayApply (&settings) &&
		(!display_trial.active || CL_JumpDisplayConfirm ()))
		Com_Printf ("ALT+Enter: %s.\n", LOG_CLIENT,
			settings.mode == JUMP_DISPLAY_WINDOWED		 ? "windowed" :
				settings.mode == JUMP_DISPLAY_BORDERLESS ? "borderless fullscreen" :
														   "exclusive fullscreen");
	else
		Com_Printf ("ALT+Enter: %s\n", LOG_CLIENT,
			display_trial.message[0] ? display_trial.message : "Could not change display settings.");
	return true;
}
