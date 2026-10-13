/* Private native display menu. Draft controls do not write engine cvars;
 * the display controller owns application, confirmation and recovery. */
#include "jump_menu.h"
#include "jump_display.h"

typedef struct
{
	menuFrameWork_t menu;
	menuSpinControl_t kind, monitor, resolution, window_size, frame[3], vsync;
	menuField_t width, height;
	menuSlider_t brightness;
	menuStatic_t active_mode, active_size, notices[2];
	menuAction_t actions[4];
	jump_display_settings_t draft, baseline;
	jump_display_monitor_t monitors[JUMP_DISPLAY_MAX_MONITORS];
	jump_display_mode_t modes[JUMP_DISPLAY_MAX_MODES];
	char monitor_text[JUMP_DISPLAY_MAX_MONITORS][128];
	char *monitor_names[JUMP_DISPLAY_MAX_MONITORS + 1];
	char mode_text[JUMP_DISPLAY_MAX_MODES + 1][64];
	char *mode_names[JUMP_DISPLAY_MAX_MODES + 2];
	char status[256], error[160], current_vsync[64];
	char active_text[2][80];
	char *vsync_names[5];
	int vsync_values[4], monitor_count, mode_count, vsync_count;
	qboolean active;
} jump_video_menu_t;

static jump_video_menu_t video_menu;
static char *display_kinds[] = { "Windowed", "Borderless fullscreen", "Exclusive fullscreen", NULL };
static char *display_boolean[] = { "No", "Yes", NULL };
static const int window_sizes[][2] = {
	{ 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1280, 960 },
	{ 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 }
};
static char *window_size_names[] = {
	"640x480", "800x600", "1024x768", "1280x720", "1280x960",
	"1600x900", "1920x1080", "2560x1440", "3840x2160", "Custom", NULL
};

enum
{
	JUI_VIDEO_CUSTOM_SIZE = sizeof (window_sizes) / sizeof (window_sizes[0])
};

static const int frame_bits[] = { JUMP_DISPLAY_WINDOW_BORDERLESS, JUMP_DISPLAY_WINDOW_NOTITLE, JUMP_DISPLAY_WINDOW_NORESIZE };

static qboolean JUI_VideoState (void);
static qboolean JUI_VideoPush (menuFrameWork_t *menu);

static void JUI_VideoFields (void)
{
	char text[32];
	int i;

	Com_sprintf (text, sizeof (text), "%d", video_menu.draft.window.width);
	IF_Replace (&video_menu.width.field, text);
	Com_sprintf (text, sizeof (text), "%d", video_menu.draft.window.height);
	IF_Replace (&video_menu.height.field, text);
	video_menu.window_size.curvalue = JUI_VIDEO_CUSTOM_SIZE;
	for (i = 0; i < JUI_VIDEO_CUSTOM_SIZE; i++)
		if (video_menu.draft.window.width == window_sizes[i][0] && video_menu.draft.window.height == window_sizes[i][1])
		{
			video_menu.window_size.curvalue = i;
			break;
		}
}

static void JUI_VideoModes (void)
{
	int i, selected;
	jump_display_mode_t *mode;

	video_menu.mode_count = CL_JumpDisplayModes (video_menu.draft.display, video_menu.modes, JUMP_DISPLAY_MAX_MODES);
	strcpy (video_menu.mode_text[0], "Desktop / automatic");
	video_menu.mode_names[0] = video_menu.mode_text[0];
	selected = 0;
	for (i = 0; i < video_menu.mode_count; i++)
	{
		mode = video_menu.modes + i;
		Com_sprintf (video_menu.mode_text[i + 1], sizeof (video_menu.mode_text[0]), "%dx%d  %d Hz  %d-bit",
			mode->width, mode->height, mode->refresh, mode->depth);
		video_menu.mode_names[i + 1] = video_menu.mode_text[i + 1];
		if (mode->width == video_menu.draft.width && mode->height == video_menu.draft.height &&
			mode->refresh == video_menu.draft.refresh && mode->depth == video_menu.draft.depth)
			selected = i + 1;
	}
	video_menu.mode_names[video_menu.mode_count + 1] = NULL;
	video_menu.resolution.itemnames = video_menu.mode_names;
	video_menu.resolution.curvalue = selected;
}

static void JUI_VideoRead (const jump_display_snapshot_t *snapshot)
{
	int i, selected;

	video_menu.draft = snapshot->current;
	video_menu.kind.curvalue = video_menu.draft.mode;
	video_menu.monitor_count = CL_JumpDisplayMonitors (video_menu.monitors, JUMP_DISPLAY_MAX_MONITORS);
	selected = 0;
	for (i = 0; i < video_menu.monitor_count; i++)
	{
		Com_sprintf (video_menu.monitor_text[i], sizeof (video_menu.monitor_text[0]), "%s%s",
			video_menu.monitors[i].name, video_menu.monitors[i].primary ? " (primary)" : "");
		video_menu.monitor_names[i] = video_menu.monitor_text[i];
		if (!strcmp (video_menu.draft.display, video_menu.monitors[i].id))
			selected = i;
		else if (!video_menu.draft.display[0] && video_menu.monitors[i].primary)
			selected = i;
	}
	if (!video_menu.monitor_count)
	{
		strcpy (video_menu.monitor_text[0], "Unavailable");
		video_menu.monitor_names[0] = video_menu.monitor_text[0];
		video_menu.monitor_names[1] = NULL;
	}
	else
		video_menu.monitor_names[video_menu.monitor_count] = NULL;
	video_menu.monitor.curvalue = selected;
	video_menu.monitor.itemnames = video_menu.monitor_names;
	if (video_menu.monitor_count && !video_menu.draft.display[0])
		Q_strlcpy (video_menu.draft.display, video_menu.monitors[selected].id, sizeof (video_menu.draft.display));
	JUI_VideoModes ();
	IF_Init (&video_menu.width.field, 8, 8);
	IF_Init (&video_menu.height.field, 8, 8);
	JUI_VideoFields ();
	for (i = 0; i < 3; i++)
		video_menu.frame[i].curvalue = (video_menu.draft.window_flags & frame_bits[i]) != 0;
	video_menu.vsync_count = 0;
	selected = -1;
	if (snapshot->capabilities & JUMP_DISPLAY_CAP_VSYNC)
	{
		video_menu.vsync_names[0] = "Off";
		video_menu.vsync_values[0] = 0;
		video_menu.vsync_names[1] = "On";
		video_menu.vsync_values[1] = 1;
		video_menu.vsync_count = 2;
		if (snapshot->capabilities & JUMP_DISPLAY_CAP_ADAPTIVE)
		{
			video_menu.vsync_names[2] = "Adaptive";
			video_menu.vsync_values[2] = -1;
			video_menu.vsync_count++;
		}
	}
	for (i = 0; i < video_menu.vsync_count; i++)
		if (video_menu.vsync_values[i] == video_menu.draft.vsync)
			selected = i;
	if (selected < 0)
	{
		selected = video_menu.vsync_count++;
		Com_sprintf (video_menu.current_vsync, sizeof (video_menu.current_vsync), "Current (%d)%s",
			video_menu.draft.vsync, (snapshot->capabilities & JUMP_DISPLAY_CAP_VSYNC) ? "" : " - unavailable");
		video_menu.vsync_names[selected] = video_menu.current_vsync;
		video_menu.vsync_values[selected] = video_menu.draft.vsync;
	}
	video_menu.vsync_names[video_menu.vsync_count] = NULL;
	video_menu.vsync.itemnames = video_menu.vsync_names;
	video_menu.vsync.curvalue = selected;
	video_menu.baseline = video_menu.draft;
}

static menuSound_t JUI_VideoChange (menuCommon_t *item)
{
	jump_display_monitor_t *monitor;
	jump_display_mode_t *mode;
	int i;

	if (item == &video_menu.kind.generic)
		video_menu.draft.mode = video_menu.kind.curvalue;
	else if (item == &video_menu.monitor.generic)
	{
		i = video_menu.monitor.curvalue;
		if (i < 0 || i >= video_menu.monitor_count)
			return QMS_BEEP;
		monitor = video_menu.monitors + i;
		Q_strlcpy (video_menu.draft.display, monitor->id, sizeof (video_menu.draft.display));
		video_menu.draft.window.x = monitor->desktop.x + (monitor->desktop.width - video_menu.draft.window.width) / 2;
		video_menu.draft.window.y = monitor->desktop.y + (monitor->desktop.height - video_menu.draft.window.height) / 2;
		video_menu.draft.width = video_menu.draft.height = video_menu.draft.refresh = video_menu.draft.depth = 0;
		JUI_VideoModes ();
	}
	else if (item == &video_menu.resolution.generic)
	{
		i = video_menu.resolution.curvalue;
		if (i < 0 || i > video_menu.mode_count)
			return QMS_BEEP;
		video_menu.draft.width = video_menu.draft.height = video_menu.draft.refresh = video_menu.draft.depth = 0;
		if (i)
		{
			mode = video_menu.modes + i - 1;
			video_menu.draft.width = mode->width;
			video_menu.draft.height = mode->height;
			video_menu.draft.refresh = mode->refresh;
			video_menu.draft.depth = mode->depth;
		}
	}
	else if (item == &video_menu.window_size.generic)
	{
		i = video_menu.window_size.curvalue;
		if (i < 0 || i > JUI_VIDEO_CUSTOM_SIZE)
			return QMS_BEEP;
		if (i < JUI_VIDEO_CUSTOM_SIZE)
		{
			video_menu.draft.window.width = window_sizes[i][0];
			video_menu.draft.window.height = window_sizes[i][1];
			JUI_VideoFields ();
		}
	}
	else if (item == &video_menu.vsync.generic)
	{
		i = video_menu.vsync.curvalue;
		if (i >= 0 && i < video_menu.vsync_count)
			video_menu.draft.vsync = video_menu.vsync_values[i];
	}
	else
	{
		for (i = 0; i < 3; i++)
			if (item == &video_menu.frame[i].generic)
			{
				video_menu.draft.window_flags &= ~frame_bits[i];
				if (video_menu.frame[i].curvalue)
					video_menu.draft.window_flags |= frame_bits[i];
			}
	}
	video_menu.error[0] = 0;
	JUI_VideoState ();
	Menu_Init (&video_menu.menu);
	JUI_VideoState ();
	return QMS_MOVE;
}

static qboolean JUI_VideoDimension (const char *text, int minimum, int *out)
{
	char *end;
	long value;

	if (!CL_JumpUIIsUint (text))
		return false;
	errno = 0;
	value = strtol (text, &end, 10);
	if (errno || *end || value < minimum || value > 8192)
		return false;
	*out = (int)value;
	return true;
}

static qboolean JUI_VideoDisplayChanged (const jump_display_settings_t *draft, const jump_display_settings_t *current)
{
	const char *display = current->display;
	int i, selected;

	if (!display[0] && video_menu.monitor_count)
	{
		selected = 0;
		for (i = 0; i < video_menu.monitor_count; i++)
			if (video_menu.monitors[i].primary)
				selected = i;
		display = video_menu.monitors[selected].id;
	}
	if (draft->mode != current->mode || strcmp (draft->display, display) || draft->vsync != current->vsync)
		return true;
	if (draft->mode == JUMP_DISPLAY_WINDOWED)
		return draft->window.x != current->window.x || draft->window.y != current->window.y ||
			draft->window.width != current->window.width || draft->window.height != current->window.height ||
			draft->window_flags != current->window_flags;
	return draft->mode == JUMP_DISPLAY_EXCLUSIVE &&
		(draft->width != current->width || draft->height != current->height ||
			draft->refresh != current->refresh || draft->depth != current->depth);
}

qboolean JUI_VideoApplyChanges (void)
{
	jump_display_snapshot_t snapshot;
	jump_display_settings_t geometry;
	qboolean ok;
	int vsync, width, height;

	if (!video_menu.menu.name)
		return Menu_VideoApply ();
	if (!video_menu.menu.videoPendingInitialized)
		JUI_VideoPush (&video_menu.menu);
	CL_JumpDisplaySnapshot (&snapshot);
	if (snapshot.active || snapshot.applying || snapshot.committing)
		return false;
	video_menu.error[0] = 0;
	if (snapshot.available && video_menu.draft.mode == JUMP_DISPLAY_WINDOWED)
	{
		if (!JUI_VideoDimension (video_menu.width.field.text, 320, &width) ||
			!JUI_VideoDimension (video_menu.height.field.text, 240, &height))
		{
			UI_PushMenu (&video_menu.menu);
			Q_strlcpy (video_menu.error, "Window size must be 320..8192 by 240..8192.", sizeof (video_menu.error));
			JUI_VideoState ();
			return false;
		}
		video_menu.draft.window.width = width;
		video_menu.draft.window.height = height;
	}
	/* External window moves and resizes are not pending menu edits. */
	if (!snapshot.available || !JUI_VideoDisplayChanged (&video_menu.draft, &video_menu.baseline))
		return Menu_VideoApply ();
	geometry = video_menu.draft;
	geometry.vsync = video_menu.baseline.vsync;
	if (!JUI_VideoDisplayChanged (&geometry, &video_menu.baseline))
	{
		/* A VSync-only edit keeps the current window geometry. */
		vsync = video_menu.draft.vsync;
		video_menu.draft = snapshot.current;
		video_menu.draft.vsync = vsync;
		if (!video_menu.draft.display[0])
			Q_strlcpy (video_menu.draft.display, video_menu.baseline.display, sizeof (video_menu.draft.display));
	}
	if (!JUI_VideoDisplayChanged (&video_menu.draft, &snapshot.current))
	{
		JUI_VideoRead (&snapshot);
		return Menu_VideoApply ();
	}
	UI_PushMenu (&video_menu.menu);
	ok = CL_JumpDisplayApply (&video_menu.draft);
	CL_JumpDisplaySnapshot (&snapshot);
	if (ok && !snapshot.active)
		ok = Menu_VideoApply ();
	if (!snapshot.active && snapshot.available)
		JUI_VideoRead (&snapshot);
	JUI_VideoState ();
	Menu_Init (&video_menu.menu);
	JUI_VideoState ();
	return ok;
}

void JUI_VideoDiscardChanges (void)
{
	jump_display_snapshot_t snapshot;

	CL_JumpDisplayRevert ();
	Menu_VideoDiscard ();
	if (!video_menu.menu.name)
		return;
	video_menu.error[0] = 0;
	CL_JumpDisplaySnapshot (&snapshot);
	JUI_VideoRead (&snapshot);
	JUI_VideoState ();
	if (uis.activeMenu == &video_menu.menu)
	{
		Menu_Init (&video_menu.menu);
		JUI_VideoState ();
	}
}

static menuSound_t JUI_VideoAction (menuCommon_t *item)
{
	jump_display_snapshot_t snapshot;
	qboolean ok = true;

	switch (item->id)
	{
	case 0:
		return JUI_VideoApplyChanges () ? QMS_IN : QMS_BEEP;
	case 1:
		ok = CL_JumpDisplayConfirm ();
		if (ok)
			ok = Menu_VideoApply ();
		break;
	case 2:
		CL_JumpDisplayRevert ();
		break;
	case 3:
		JUI_VideoDiscardChanges ();
		return QMS_OUT;
	}
	CL_JumpDisplaySnapshot (&snapshot);
	if (!snapshot.active && snapshot.available)
		JUI_VideoRead (&snapshot);
	JUI_VideoState ();
	Menu_Init (&video_menu.menu);
	JUI_VideoState ();
	return ok ? QMS_IN : QMS_BEEP;
}

static void JUI_VideoEnabled (menuCommon_t *item, qboolean enabled)
{
	if (enabled)
		item->flags &= ~QMF_GRAYED;
	else
		item->flags |= QMF_GRAYED;
}

static qboolean JUI_VideoVisible (menuCommon_t *item, qboolean visible)
{
	qboolean hidden = (item->flags & QMF_HIDDEN) != 0;
	if (visible)
		item->flags &= ~QMF_HIDDEN;
	else
		item->flags |= QMF_HIDDEN;
	return hidden == visible;
}

static qboolean JUI_VideoState (void)
{
	jump_display_snapshot_t snapshot;
	menuCommon_t *focused;
	qboolean edit, windowed, custom, framed, changed;
	char text[80];
	const char *help;
	int i;

	CL_JumpDisplaySnapshot (&snapshot);
	edit = snapshot.available && !snapshot.active && !snapshot.applying && !snapshot.committing;
	if (snapshot.active || snapshot.applying || snapshot.committing)
		video_menu.menu.videoTabsFocused = false;
	windowed = video_menu.draft.mode == JUMP_DISPLAY_WINDOWED;
	custom = windowed && video_menu.window_size.curvalue == JUI_VIDEO_CUSTOM_SIZE;
	framed = windowed && !(video_menu.draft.window_flags & JUMP_DISPLAY_WINDOW_BORDERLESS);
	changed = false;
	changed |= JUI_VideoVisible (&video_menu.kind.generic, !snapshot.active);
	changed |= JUI_VideoVisible (&video_menu.monitor.generic, !snapshot.active);
	changed |= JUI_VideoVisible (&video_menu.resolution.generic, !snapshot.active && video_menu.draft.mode == JUMP_DISPLAY_EXCLUSIVE);
	changed |= JUI_VideoVisible (&video_menu.window_size.generic, !snapshot.active && windowed);
	changed |= JUI_VideoVisible (&video_menu.width.generic, !snapshot.active && custom);
	changed |= JUI_VideoVisible (&video_menu.height.generic, !snapshot.active && custom);
	changed |= JUI_VideoVisible (&video_menu.frame[0].generic, !snapshot.active && windowed);
	changed |= JUI_VideoVisible (&video_menu.frame[1].generic, !snapshot.active && framed);
	changed |= JUI_VideoVisible (&video_menu.frame[2].generic, !snapshot.active && framed);
	changed |= JUI_VideoVisible (&video_menu.vsync.generic, !snapshot.active);
	changed |= JUI_VideoVisible (&video_menu.brightness.generic, !snapshot.active);
	changed |= JUI_VideoVisible (&video_menu.actions[0].generic, !snapshot.active);
	changed |= JUI_VideoVisible (&video_menu.actions[1].generic, snapshot.active);
	changed |= JUI_VideoVisible (&video_menu.actions[2].generic, snapshot.active);
	changed |= JUI_VideoVisible (&video_menu.actions[3].generic, !snapshot.active);
	for (i = 0; i < 2; i++)
		changed |= JUI_VideoVisible (&video_menu.notices[i].generic, snapshot.active);
	changed |= JUI_VideoVisible (&video_menu.active_size.generic, snapshot.available);
	Com_sprintf (text, sizeof (text), "Active mode: %s", snapshot.available && snapshot.current.mode >= JUMP_DISPLAY_WINDOWED && snapshot.current.mode <= JUMP_DISPLAY_EXCLUSIVE ? display_kinds[snapshot.current.mode] : "unavailable");
	if (strcmp (text, video_menu.active_text[0]))
	{
		Q_strlcpy (video_menu.active_text[0], text, sizeof (video_menu.active_text[0]));
		changed = true;
	}
	if (snapshot.current.mode == JUMP_DISPLAY_WINDOWED)
		Com_sprintf (text, sizeof (text), "Active size: %dx%d", snapshot.current.window.width, snapshot.current.window.height);
	else if (snapshot.current.refresh > 0)
		Com_sprintf (text, sizeof (text), "Active size: %dx%d @ %d Hz", snapshot.current.width, snapshot.current.height, snapshot.current.refresh);
	else
		Com_sprintf (text, sizeof (text), "Active size: %dx%d", snapshot.current.width, snapshot.current.height);
	if (strcmp (text, video_menu.active_text[1]))
	{
		Q_strlcpy (video_menu.active_text[1], text, sizeof (video_menu.active_text[1]));
		changed = true;
	}
	JUI_VideoEnabled (&video_menu.kind.generic, edit);
	JUI_VideoEnabled (&video_menu.monitor.generic, edit && video_menu.monitor_count > 0);
	JUI_VideoEnabled (&video_menu.resolution.generic, edit && video_menu.draft.mode != JUMP_DISPLAY_BORDERLESS);
	JUI_VideoEnabled (&video_menu.window_size.generic, edit && windowed);
	JUI_VideoEnabled (&video_menu.width.generic, edit && custom);
	JUI_VideoEnabled (&video_menu.height.generic, edit && custom);
	for (i = 0; i < 3; i++)
		JUI_VideoEnabled (&video_menu.frame[i].generic, edit && video_menu.draft.mode == JUMP_DISPLAY_WINDOWED);
	JUI_VideoEnabled (&video_menu.vsync.generic, edit && (snapshot.capabilities & JUMP_DISPLAY_CAP_VSYNC));
	JUI_VideoEnabled (&video_menu.brightness.generic, !snapshot.active && !snapshot.applying && !snapshot.committing);
	JUI_VideoEnabled (&video_menu.actions[0].generic, !snapshot.active && !snapshot.applying && !snapshot.committing && ((snapshot.available && video_menu.monitor_count > 0) || Menu_VideoPending ()));
	JUI_VideoEnabled (&video_menu.actions[1].generic, snapshot.active && !snapshot.applying && !snapshot.committing);
	JUI_VideoEnabled (&video_menu.actions[2].generic, snapshot.active && !snapshot.applying && !snapshot.committing);
	JUI_VideoEnabled (&video_menu.actions[3].generic, !snapshot.active && !snapshot.applying && !snapshot.committing);
	if (snapshot.active)
		Com_sprintf (video_menu.status, sizeof (video_menu.status), "Keep settings within %d seconds; Alt+Tab or Back cancels.", snapshot.seconds);
	else
	{
		focused = Menu_ItemAtCursor (&video_menu.menu);
		help = focused && focused->status ? focused->status : "Choose settings, then Apply changes.";
		if (focused == &video_menu.kind.generic)
			help = windowed ? "Set the window size below." : video_menu.draft.mode == JUMP_DISPLAY_BORDERLESS ? "Use desktop resolution and refresh." :
																															 "Switch display resolution and refresh.";
		if (!snapshot.available)
			help = "Display controls unavailable; other video settings still work.";
		Com_sprintf (video_menu.status, sizeof (video_menu.status), "%s%s%s", video_menu.error[0] ? video_menu.error : snapshot.message,
			video_menu.error[0] || snapshot.message[0] ? " " : "", help);
	}
	video_menu.menu.status = video_menu.status;
	return changed;
}

qboolean JUI_VideoTabsAvailable (void)
{
	jump_display_snapshot_t snapshot;

	CL_JumpDisplaySnapshot (&snapshot);
	return !snapshot.active && !snapshot.applying && !snapshot.committing;
}

static qboolean JUI_VideoPush (menuFrameWork_t *menu)
{
	jump_display_snapshot_t snapshot;

	video_menu.error[0] = 0;
	CL_JumpDisplayInit ();
	CL_JumpDisplaySnapshot (&snapshot);
	if (!menu->videoPendingInitialized)
		JUI_VideoRead (&snapshot);
	Menu_VideoPrepare (menu);
	video_menu.active = snapshot.active;
	JUI_VideoState ();
	return true;
}

static void JUI_VideoPop (menuFrameWork_t *menu)
{
	CL_JumpDisplayClose ();
}

static void JUI_VideoFree (menuFrameWork_t *menu)
{
	CL_JumpDisplayClose ();
	free (menu->items);
	memset (&video_menu, 0, sizeof (video_menu));
}

void JUI_VideoFrame (void)
{
	jump_display_snapshot_t snapshot;

	if (!video_menu.menu.name)
		return;
	if (!UI_IsMenuActive ("video") && !CL_JumpDisplayActive () && !video_menu.active)
		return;
	CL_JumpDisplaySnapshot (&snapshot);
	if (video_menu.active && !snapshot.active && snapshot.available)
		JUI_VideoRead (&snapshot);
	video_menu.active = snapshot.active;
	if (JUI_VideoState () && uis.activeMenu == &video_menu.menu)
	{
		Menu_Init (&video_menu.menu);
		JUI_VideoState ();
	}
}

void JUI_Menu_Video (void)
{
	menuSpinControl_t *spins[8];
	static const char *names[] = { "Apply changes", "Keep settings", "Revert", "Discard changes" };
	static const char *help[] = { "Choose a display mode.", "Choose the game monitor.",
		"Resolution and refresh for exclusive fullscreen.",
		"Window size in pixels; Custom sets width and height.",
		"Remove the window frame and title bar.",
		"Hide the title bar; keep the frame.", "Prevent resizing by dragging window edges.",
		"Prevent tearing; Adaptive allows tearing below refresh rate." };
	static const char *action_help[] = { "Apply changes; confirm new display modes within 15 seconds.",
		"Keep this display and apply pending video settings.", "Restore the previous display settings.",
		"Discard pending changes; live changes remain." };
	int i;

	video_menu.menu.name = "video";
	video_menu.menu.title = "Video - Display";
	video_menu.menu.videoApply = true;
	video_menu.menu.transparent = true;
	video_menu.menu.push = JUI_VideoPush;
	video_menu.menu.pop = JUI_VideoPop;
	video_menu.menu.free = JUI_VideoFree;
	video_menu.kind.generic.name = "Display mode";
	video_menu.kind.itemnames = display_kinds;
	video_menu.kind.generic.flags = QMF_HASFOCUS;
	video_menu.monitor.generic.name = "Monitor";
	video_menu.resolution.generic.name = "Resolution / refresh";
	video_menu.window_size.generic.name = "Window size";
	video_menu.window_size.itemnames = window_size_names;
	video_menu.frame[0].generic.name = "Remove window border";
	video_menu.frame[1].generic.name = "Hide title bar";
	video_menu.frame[2].generic.name = "Fixed window size";
	video_menu.vsync.generic.name = "VSync";
	video_menu.active_mode.generic.type = video_menu.active_size.generic.type = MTYPE_STATIC;
	video_menu.active_mode.generic.uiFlags = video_menu.active_size.generic.uiFlags = UI_CENTER | UI_ALTCOLOR;
	video_menu.active_mode.generic.name = video_menu.active_text[0];
	video_menu.active_size.generic.name = video_menu.active_text[1];
	Menu_AddItem (&video_menu.menu, &video_menu.active_mode);
	Menu_AddItem (&video_menu.menu, &video_menu.active_size);
	video_menu.width.generic.type = video_menu.height.generic.type = MTYPE_FIELD;
	video_menu.width.generic.name = "Window width";
	video_menu.height.generic.name = "Window height";
	video_menu.width.generic.status = "Width in pixels (320-8192).";
	video_menu.height.generic.status = "Height in pixels (240-8192).";
	video_menu.width.width = video_menu.height.width = 8;
	video_menu.width.length = video_menu.height.length = 8;
	spins[0] = &video_menu.kind;
	spins[1] = &video_menu.monitor;
	spins[2] = &video_menu.resolution;
	spins[3] = &video_menu.window_size;
	spins[4] = &video_menu.frame[0];
	spins[5] = &video_menu.frame[1];
	spins[6] = &video_menu.frame[2];
	spins[7] = &video_menu.vsync;
	for (i = 0; i < 8; i++)
	{
		spins[i]->generic.type = MTYPE_SPINCONTROL;
		spins[i]->generic.change = JUI_VideoChange;
		spins[i]->generic.status = (char *)help[i];
		if (i >= 4 && i <= 6)
			spins[i]->itemnames = display_boolean;
		if (i == 4)
		{
			Menu_AddItem (&video_menu.menu, &video_menu.width);
			Menu_AddItem (&video_menu.menu, &video_menu.height);
		}
		Menu_AddItem (&video_menu.menu, spins[i]);
	}
	video_menu.brightness.generic.type = MTYPE_SLIDER;
	video_menu.brightness.generic.name = "Brightness";
	video_menu.brightness.generic.status = "Move right to brighten the image.";
	video_menu.brightness.generic.flags = QMF_DEFER_COMMIT | QMF_SHOW_VALUE;
	video_menu.brightness.cvar = Cvar_WeakGet ("vid_gamma");
	video_menu.brightness.minvalue = 1.3f;
	video_menu.brightness.maxvalue = 0.3f;
	video_menu.brightness.step = -0.1f;
	Menu_AddItem (&video_menu.menu, &video_menu.brightness);
	for (i = 0; i < 2; i++)
	{
		video_menu.notices[i].generic.type = MTYPE_STATIC;
		video_menu.notices[i].generic.uiFlags = UI_CENTER | UI_ALTCOLOR;
		video_menu.notices[i].generic.name = i ? "Alt+Tab or Back will cancel." : "Choose Keep settings to save.";
		Menu_AddItem (&video_menu.menu, &video_menu.notices[i]);
	}
	for (i = 0; i < 4; i++)
	{
		video_menu.actions[i].generic.type = MTYPE_ACTION;
		video_menu.actions[i].generic.name = (char *)names[i];
		video_menu.actions[i].generic.status = (char *)action_help[i];
		video_menu.actions[i].generic.id = i;
		video_menu.actions[i].generic.activate = JUI_VideoAction;
		Menu_AddItem (&video_menu.menu, &video_menu.actions[i]);
	}
	List_Append (&ui_menus, &video_menu.menu.entry);
}
