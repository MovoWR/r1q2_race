/* Context-preserving Win32 display trials for R1Q2.
 * Q2PRO display/settings semantics: id Software / Q2PRO contributors, GPL 2+.
 * Native integration retains R1GL's HWND, DC and GL context throughout a trial.
 */
#include <windows.h>
#include "../ref_gl/gl_local.h"
#include "glw_win.h"
#include "glw_display.h"
#include "../client/vid_config.h"

static BOOL (WINAPI *display_devices) (LPCSTR, DWORD, PDISPLAY_DEVICEA, DWORD);
static BOOL (WINAPI *display_settings) (LPCSTR, DWORD, DEVMODEA *, DWORD);
static LONG (WINAPI *display_change) (LPCSTR, DEVMODEA *, HWND, DWORD, LPVOID);
static HMONITOR (WINAPI *display_monitor) (LPCRECT, DWORD);
static BOOL (WINAPI *display_monitor_info) (HMONITOR, LPMONITORINFO);
static qboolean display_initialized;

static struct
{
	qboolean managed, suspended, changing;
	char owner[32];
	DEVMODEA desktop, exclusive;
	jump_display_rect_t window;
	int window_flags;

	struct
	{
		qboolean valid, managed, suspended;
		HWND hwnd;
		HDC dc;
		HGLRC context;
		DWORD style, exstyle;
		RECT outer;
		WINDOWPLACEMENT placement;
		jump_display_settings_t settings;
		char owner[32];
		DEVMODEA desktop, exclusive;
	} checkpoint;

	char message[160];
} display_state;

static void GLW_DisplayMessage (const char *text)
{
	Com_sprintf (display_state.message, sizeof (display_state.message), "%s", text);
}

static qboolean GLW_DisplayLoad (void)
{
	HMODULE user;
	if (!display_initialized)
	{
		display_initialized = true;
		user = GetModuleHandleA ("user32.dll");
		if (user)
		{
			display_devices = (BOOL (WINAPI *) (LPCSTR, DWORD, PDISPLAY_DEVICEA, DWORD))GetProcAddress (user, "EnumDisplayDevicesA");
			display_settings = (BOOL (WINAPI *) (LPCSTR, DWORD, DEVMODEA *, DWORD))GetProcAddress (user, "EnumDisplaySettingsExA");
			display_change = (LONG (WINAPI *) (LPCSTR, DEVMODEA *, HWND, DWORD, LPVOID))GetProcAddress (user, "ChangeDisplaySettingsExA");
			display_monitor = (HMONITOR (WINAPI *) (LPCRECT, DWORD))GetProcAddress (user, "MonitorFromRect");
			display_monitor_info = (BOOL (WINAPI *) (HMONITOR, LPMONITORINFO))GetProcAddress (user, "GetMonitorInfoA");
		}
	}
	return display_devices && display_settings && display_change;
}

static qboolean EXPORT GLW_DisplayAvailable (void)
{
	return GLW_DisplayLoad () && glw_state.hWnd && glw_state.hDC && glw_state.hGLRC && !glw_state.minidriver;
}

static qboolean GLW_DisplayQuery (const char *id, DWORD index, DEVMODEA *mode)
{
	memset (mode, 0, sizeof (*mode));
	mode->dmSize = sizeof (*mode);
	return display_settings (id, index, mode, 0);
}

static qboolean GLW_DisplayModeValid (const DEVMODEA *mode)
{
	return mode->dmPelsWidth >= 320 && mode->dmPelsWidth <= 8192 &&
		mode->dmPelsHeight >= 240 && mode->dmPelsHeight <= 8192 &&
		mode->dmDisplayFrequency >= 2 && mode->dmDisplayFrequency <= 1000 &&
		(mode->dmBitsPerPel == 16 || mode->dmBitsPerPel == 24 || mode->dmBitsPerPel == 32);
}

static int EXPORT GLW_DisplayMonitors (jump_display_monitor_t *out, int capacity)
{
	DISPLAY_DEVICEA device;
	DEVMODEA mode;
	int i, count;
	if (!out || capacity <= 0 || !GLW_DisplayLoad ())
		return 0;
	if (capacity > JUMP_DISPLAY_MAX_MONITORS)
		capacity = JUMP_DISPLAY_MAX_MONITORS;
	count = 0;
	for (i = 0; i < 128 && count < capacity; i++)
	{
		memset (&device, 0, sizeof (device));
		device.cb = sizeof (device);
		if (!display_devices (NULL, (DWORD)i, &device, 0))
			break;
		if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) ||
			(device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) ||
			!memchr (device.DeviceName, 0, sizeof (device.DeviceName)) || !device.DeviceName[0] ||
			!GLW_DisplayQuery (device.DeviceName, ENUM_CURRENT_SETTINGS, &mode) || !GLW_DisplayModeValid (&mode) ||
			mode.dmPosition.x < -32767 || mode.dmPosition.x > 32767 || mode.dmPosition.y < -32767 || mode.dmPosition.y > 32767)
			continue;
		memset (&out[count], 0, sizeof (out[count]));
		Com_sprintf (out[count].id, sizeof (out[count].id), "%s", device.DeviceName);
		device.DeviceString[sizeof (device.DeviceString) - 1] = 0;
		Com_sprintf (out[count].name, sizeof (out[count].name), "%s", device.DeviceString);
		out[count].desktop.x = mode.dmPosition.x;
		out[count].desktop.y = mode.dmPosition.y;
		out[count].desktop.width = mode.dmPelsWidth;
		out[count].desktop.height = mode.dmPelsHeight;
		out[count].refresh = mode.dmDisplayFrequency;
		out[count].depth = mode.dmBitsPerPel;
		out[count].primary = !!(device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE);
		count++;
	}
	return count;
}

static qboolean GLW_DisplayFind (const char *id, jump_display_monitor_t *out)
{
	jump_display_monitor_t monitors[JUMP_DISPLAY_MAX_MONITORS];
	int i, count, index;
	count = GLW_DisplayMonitors (monitors, JUMP_DISPLAY_MAX_MONITORS);
	if (!count)
		return false;
	index = 0;
	for (i = 0; i < count; i++)
	{
		if (id && id[0] && !strcmp (id, monitors[i].id))
		{
			*out = monitors[i];
			return true;
		}
		if (monitors[i].primary)
			index = i;
	}
	if (id && id[0])
		return false;
	*out = monitors[index];
	return true;
}

static int GLW_DisplayCompare (const void *a, const void *b)
{
	const jump_display_mode_t *first = a, *second = b;
	if (first->width != second->width)
		return first->width < second->width ? -1 : 1;
	if (first->height != second->height)
		return first->height < second->height ? -1 : 1;
	if (first->refresh != second->refresh)
		return first->refresh < second->refresh ? -1 : 1;
	return first->depth < second->depth ? -1 : first->depth != second->depth;
}

static int EXPORT GLW_DisplayModes (const char *id, jump_display_mode_t *out, int capacity)
{
	jump_display_monitor_t monitor;
	jump_display_mode_t next;
	DEVMODEA mode;
	int i, n, count;
	if (!out || capacity <= 0 || !GLW_DisplayFind (id, &monitor))
		return 0;
	if (capacity > JUMP_DISPLAY_MAX_MODES)
		capacity = JUMP_DISPLAY_MAX_MODES;
	count = 0;
	for (i = 0; i < JUMP_DISPLAY_MAX_MODES && count < capacity; i++)
	{
		if (!GLW_DisplayQuery (monitor.id, (DWORD)i, &mode))
			break;
		if (!GLW_DisplayModeValid (&mode) || mode.dmBitsPerPel != (DWORD)monitor.depth)
			continue;
		next.width = mode.dmPelsWidth;
		next.height = mode.dmPelsHeight;
		next.refresh = mode.dmDisplayFrequency;
		next.depth = mode.dmBitsPerPel;
		for (n = 0; n < count; n++)
			if (!GLW_DisplayCompare (&out[n], &next))
				break;
		if (n == count)
			out[count++] = next;
	}
	qsort (out, count, sizeof (*out), GLW_DisplayCompare);
	return count;
}

static qboolean GLW_DisplayClient (jump_display_rect_t *window)
{
	RECT rect;
	POINT origin;
	if (!GetClientRect (glw_state.hWnd, &rect))
		return false;
	origin.x = origin.y = 0;
	if (!ClientToScreen (glw_state.hWnd, &origin))
		return false;
	window->x = origin.x;
	window->y = origin.y;
	window->width = rect.right - rect.left;
	window->height = rect.bottom - rect.top;
	return window->width >= 64 && window->width < 65536 && window->height >= 48 && window->height < 65536;
}

static qboolean EXPORT GLW_DisplayRead (jump_display_settings_t *out, unsigned int *capabilities)
{
	jump_display_monitor_t monitors[JUMP_DISPLAY_MAX_MONITORS], monitor;
	jump_display_rect_t window;
	vid_geometry_t geometry;
	int count, i, index, best_area, area, left, right, top, bottom;
	DWORD style;
	if (!out || !capabilities || !GLW_DisplayAvailable () || !GLW_DisplayClient (&window))
		return false;
	count = GLW_DisplayMonitors (monitors, JUMP_DISPLAY_MAX_MONITORS);
	if (!count)
		return false;
	index = 0;
	best_area = 0;
	for (i = 0; i < count; i++)
		if (monitors[i].primary)
			index = i;
	for (i = 0; i < count; i++)
	{
		if (display_state.owner[0] && !strcmp (display_state.owner, monitors[i].id))
		{
			index = i;
			break;
		}
		left = max (window.x, monitors[i].desktop.x);
		top = max (window.y, monitors[i].desktop.y);
		right = min (window.x + window.width, monitors[i].desktop.x + monitors[i].desktop.width);
		bottom = min (window.y + window.height, monitors[i].desktop.y + monitors[i].desktop.height);
		area = right > left && bottom > top ? (right - left) * (bottom - top) : 0;
		if (area > best_area)
		{
			best_area = area;
			index = i;
		}
	}
	monitor = monitors[index];
	memset (out, 0, sizeof (*out));
	Com_sprintf (out->display, sizeof (out->display), "%s", monitor.id);
	out->mode = gl_state.fullscreen ? JUMP_DISPLAY_EXCLUSIVE :
		GLimp_DisplayWindowed ()	? JUMP_DISPLAY_WINDOWED :
									  JUMP_DISPLAY_BORDERLESS;
	out->width = monitor.desktop.width;
	out->height = monitor.desktop.height;
	out->refresh = monitor.refresh;
	out->depth = monitor.depth;
	out->vsync = GLimp_DisplaySwapRead ();
	*capabilities = GLimp_DisplayCapabilities ();
	style = (DWORD)GetWindowLong (glw_state.hWnd, GWL_STYLE);
	out->window_flags = display_state.managed ? display_state.window_flags :
												(!(style & (WS_BORDER | WS_THICKFRAME)) ? JUMP_DISPLAY_WINDOW_BORDERLESS : 0) |
			((style & WS_CAPTION) != WS_CAPTION ? JUMP_DISPLAY_WINDOW_NOTITLE : 0) |
			(!(style & WS_THICKFRAME) ? JUMP_DISPLAY_WINDOW_NORESIZE : 0);
	if (out->mode == JUMP_DISPLAY_WINDOWED)
		out->window = window;
	else if (display_state.window.width >= 320)
		out->window = display_state.window;
	else if (VID_ParseGeometry (ri.Cvar_Get ("vid_geometry", "", CVAR_ARCHIVE)->string, &geometry))
	{
		out->window.x = geometry.x;
		out->window.y = geometry.y;
		out->window.width = geometry.width;
		out->window.height = geometry.height;
	}
	else
	{
		out->window.x = monitor.desktop.x;
		out->window.y = monitor.desktop.y;
		out->window.width = 640;
		out->window.height = 480;
	}
	return true;
}

static qboolean GLW_DisplayAttached (const char *id)
{
	DISPLAY_DEVICEA device;
	int i;
	for (i = 0; i < 128; i++)
	{
		memset (&device, 0, sizeof (device));
		device.cb = sizeof (device);
		if (!display_devices (NULL, (DWORD)i, &device, 0))
			break;
		if ((device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
			!(device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) &&
			memchr (device.DeviceName, 0, sizeof (device.DeviceName)) && !strcmp (id, device.DeviceName))
			return true;
	}
	return false;
}

static qboolean GLW_DisplayRestoreOwner (void)
{
	if (!display_state.owner[0])
		return true;
	if (GLW_DisplayAttached (display_state.owner) &&
		display_change (display_state.owner, &display_state.desktop, NULL, 0, NULL) != DISP_CHANGE_SUCCESSFUL)
		return false;
	display_state.owner[0] = 0;
	display_state.suspended = false;
	return true;
}

static qboolean EXPORT GLW_DisplayBegin (void)
{
	jump_display_settings_t current;
	unsigned int capabilities;
	if (display_state.checkpoint.valid || !GLW_DisplayRead (&current, &capabilities))
		return false;
	memset (&display_state.checkpoint, 0, sizeof (display_state.checkpoint));
	display_state.checkpoint.placement.length = sizeof (WINDOWPLACEMENT);
	if (!GetWindowRect (glw_state.hWnd, &display_state.checkpoint.outer) ||
		!GetWindowPlacement (glw_state.hWnd, &display_state.checkpoint.placement))
	{
		GLW_DisplayMessage ("Could not capture the native window.");
		return false;
	}
	/* Adopt a primary exclusive mode created by the original GLW path. */
	if (!display_state.managed && current.mode == JUMP_DISPLAY_EXCLUSIVE)
	{
		if (!GLimp_DisplayLegacyDesktop (&display_state.desktop) ||
			!GLW_DisplayQuery (current.display, ENUM_CURRENT_SETTINGS, &display_state.exclusive))
			return false;
		Com_sprintf (display_state.owner, sizeof (display_state.owner), "%s", current.display);
	}
	display_state.checkpoint.hwnd = glw_state.hWnd;
	display_state.checkpoint.dc = glw_state.hDC;
	display_state.checkpoint.context = glw_state.hGLRC;
	display_state.checkpoint.settings = current;
	display_state.checkpoint.style = (DWORD)GetWindowLong (glw_state.hWnd, GWL_STYLE);
	display_state.checkpoint.exstyle = (DWORD)GetWindowLong (glw_state.hWnd, GWL_EXSTYLE);
	display_state.checkpoint.managed = display_state.managed;
	display_state.checkpoint.suspended = display_state.suspended;
	memcpy (display_state.checkpoint.owner, display_state.owner, sizeof (display_state.owner));
	display_state.checkpoint.desktop = display_state.desktop;
	display_state.checkpoint.exclusive = display_state.exclusive;
	display_state.checkpoint.valid = true;
	display_state.managed = true;
	display_state.window = current.window;
	display_state.window_flags = current.window_flags;
	GLW_DisplayMessage ("");
	return true;
}

static qboolean GLW_DisplayStyle (DWORD style, DWORD exstyle)
{
	LONG previous;
	SetLastError (0);
	previous = SetWindowLong (glw_state.hWnd, GWL_STYLE, (LONG)style);
	if (!previous && GetLastError ())
		return false;
	SetLastError (0);
	previous = SetWindowLong (glw_state.hWnd, GWL_EXSTYLE, (LONG)exstyle);
	return previous || !GetLastError ();
}

static DWORD GLW_DisplayWindowStyle (int flags)
{
	DWORD style = WS_VISIBLE | WS_SYSMENU | WS_MINIMIZEBOX;
	if (flags & JUMP_DISPLAY_WINDOW_BORDERLESS)
		return style | WS_POPUP;
	style |= WS_OVERLAPPED | WS_BORDER;
	if (!(flags & JUMP_DISPLAY_WINDOW_NOTITLE))
		style |= WS_CAPTION;
	if (!(flags & JUMP_DISPLAY_WINDOW_NORESIZE))
		style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
	return style;
}

static qboolean GLW_DisplaySelectMode (const jump_display_settings_t *s, const jump_display_monitor_t *monitor, DEVMODEA *selected)
{
	DEVMODEA mode;
	int i, width, height, depth;
	qboolean found;
	width = s->width ? s->width : monitor->desktop.width;
	height = s->height ? s->height : monitor->desktop.height;
	depth = s->depth ? s->depth : monitor->depth;
	found = false;
	for (i = 0; i < JUMP_DISPLAY_MAX_MODES; i++)
	{
		if (!GLW_DisplayQuery (monitor->id, (DWORD)i, &mode))
			break;
		if (!GLW_DisplayModeValid (&mode) || mode.dmPelsWidth != (DWORD)width ||
			mode.dmPelsHeight != (DWORD)height || mode.dmBitsPerPel != (DWORD)depth ||
			(s->refresh && mode.dmDisplayFrequency != (DWORD)s->refresh))
			continue;
		if (!found || mode.dmDisplayFrequency == (DWORD)monitor->refresh)
			*selected = mode;
		found = true;
		if (s->refresh || mode.dmDisplayFrequency == (DWORD)monitor->refresh)
			break;
	}
	if (found)
	{
		selected->dmDriverExtra = 0;
		selected->dmPosition.x = monitor->desktop.x;
		selected->dmPosition.y = monitor->desktop.y;
		selected->dmFields |= DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
	}
	return found;
}

static void GLW_DisplayWorkArea (const jump_display_monitor_t *monitor, RECT *work)
{
	HMONITOR handle;
	MONITORINFO info;
	work->left = monitor->desktop.x;
	work->top = monitor->desktop.y;
	work->right = work->left + monitor->desktop.width;
	work->bottom = work->top + monitor->desktop.height;
	if (!display_monitor || !display_monitor_info)
		return;
	handle = display_monitor (work, MONITOR_DEFAULTTONEAREST);
	memset (&info, 0, sizeof (info));
	info.cbSize = sizeof (info);
	if (handle && display_monitor_info (handle, &info))
		*work = info.rcWork;
}

static qboolean GLW_DisplayValidRequest (const jump_display_settings_t *s)
{
	if (!s || !memchr (s->display, 0, sizeof (s->display)) ||
		s->mode < JUMP_DISPLAY_WINDOWED || s->mode > JUMP_DISPLAY_EXCLUSIVE ||
		(s->window_flags & ~JUMP_DISPLAY_WINDOW_FLAGS) || s->refresh < 0 || s->refresh == 1 || s->refresh > 1000 ||
		(s->depth && s->depth != 16 && s->depth != 24 && s->depth != 32))
		return false;
	if (s->mode == JUMP_DISPLAY_WINDOWED)
		return s->window.width >= 320 && s->window.width <= 8192 && s->window.height >= 240 && s->window.height <= 8192 &&
			s->window.x >= -32767 && s->window.x <= 32767 && s->window.y >= -32767 && s->window.y <= 32767;
	return s->mode == JUMP_DISPLAY_BORDERLESS || ((!s->width || (s->width >= 320 && s->width <= 8192)) && (!s->height || (s->height >= 240 && s->height <= 8192)));
}

static qboolean GLW_DisplaySameMode (const jump_display_settings_t *s, const jump_display_settings_t *actual)
{
	if (s->mode != actual->mode || strcmp (s->display, actual->display))
		return false;
	if (s->mode == JUMP_DISPLAY_WINDOWED)
		return s->window_flags == actual->window_flags && !memcmp (&s->window, &actual->window, sizeof (s->window));
	/* Zero exclusive fields request the desktop/default, not the current temporary mode. */
	return s->mode == JUMP_DISPLAY_BORDERLESS || (s->width > 0 && s->height > 0 && s->refresh > 0 && s->depth > 0 && s->width == actual->width && s->height == actual->height && s->refresh == actual->refresh && s->depth == actual->depth);
}

static qboolean EXPORT GLW_DisplayApply (const jump_display_settings_t *s, jump_display_settings_t *actual)
{
	jump_display_monitor_t monitor;
	jump_display_rect_t window;
	DEVMODEA mode;
	RECT frame, work;
	DWORD style, exstyle;
	cvar_t *topmost;
	unsigned int capabilities;
	int x, y, width, height, attempt;
	if (!GLW_DisplayValidRequest (s) || !actual || !display_state.checkpoint.valid || !GLW_DisplayAvailable () ||
		display_state.checkpoint.hwnd != glw_state.hWnd || display_state.checkpoint.dc != glw_state.hDC ||
		display_state.checkpoint.context != glw_state.hGLRC || !GLW_DisplayFind (s->display, &monitor))
		return false;
	/* VSync-only changes keep native mode/style/geometry completely untouched. */
	if (GLW_DisplayRead (actual, &capabilities) && GLW_DisplaySameMode (s, actual))
	{
		if (s->vsync != actual->vsync && !GLimp_DisplaySwapSet (s->vsync))
			return false;
		return GLW_DisplayRead (actual, &capabilities);
	}
	if (!GLW_DisplayRestoreOwner () || !GLW_DisplayFind (s->display, &monitor))
		return false;
	if (s->mode == JUMP_DISPLAY_EXCLUSIVE)
	{
		if (!GLW_DisplaySelectMode (s, &monitor, &mode) ||
			!GLW_DisplayQuery (monitor.id, ENUM_CURRENT_SETTINGS, &display_state.desktop) ||
			display_change (monitor.id, &mode, NULL, CDS_TEST, NULL) != DISP_CHANGE_SUCCESSFUL)
			return false;
		/* Track ownership before attempting a driver call that can partially change. */
		Com_sprintf (display_state.owner, sizeof (display_state.owner), "%s", monitor.id);
		display_state.exclusive = mode;
		if (display_change (monitor.id, &mode, NULL, CDS_FULLSCREEN, NULL) != DISP_CHANGE_SUCCESSFUL ||
			!GLW_DisplayFind (monitor.id, &monitor))
			return false;
	}
	display_state.window_flags = s->window_flags;
	GLimp_DisplayWindowState (s->mode);
	exstyle = (DWORD)GetWindowLong (glw_state.hWnd, GWL_EXSTYLE) & ~WS_EX_TOPMOST;
	topmost = ri.Cvar_Get ("vid_topmost", "0", 0);
	if (s->mode != JUMP_DISPLAY_WINDOWED && topmost->intvalue)
		exstyle |= WS_EX_TOPMOST;
	style = s->mode == JUMP_DISPLAY_WINDOWED ? GLW_DisplayWindowStyle (s->window_flags) : WS_POPUP | WS_VISIBLE;
	if (!GLW_DisplayStyle (style, exstyle))
		return false;
	ShowWindow (glw_state.hWnd, SW_RESTORE);
	if (s->mode == JUMP_DISPLAY_WINDOWED)
	{
		window = s->window;
		GLW_DisplayWorkArea (&monitor, &work);
		for (attempt = 0; attempt < 2; attempt++)
		{
			frame.left = frame.top = 0;
			frame.right = window.width;
			frame.bottom = window.height;
			if (!GLimp_DisplayAdjust (&frame, style, exstyle))
				return false;
			x = window.x + frame.left;
			y = window.y + frame.top;
			width = frame.right - frame.left;
			height = frame.bottom - frame.top;
			/* Keep the title bar reachable, including when the client fills the work area. */
			x = max ((int)work.left, min (x, (int)work.right - width));
			y = max ((int)work.top, min (y, (int)work.bottom - height));
			if (!SetWindowPos (glw_state.hWnd, exstyle & WS_EX_TOPMOST ? HWND_TOPMOST : HWND_NOTOPMOST,
					x, y, width, height, SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW))
				return false;
			/* The first move may change DPI; the second uses that window's DPI. */
		}
	}
	else if (!SetWindowPos (glw_state.hWnd, exstyle & WS_EX_TOPMOST ? HWND_TOPMOST : HWND_NOTOPMOST,
				 monitor.desktop.x, monitor.desktop.y, monitor.desktop.width, monitor.desktop.height,
				 SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW))
		return false;
	if (s->vsync != GLimp_DisplaySwapRead () && !GLimp_DisplaySwapSet (s->vsync))
		return false;
	if (!GLimp_DisplayDrawable () || !GLW_DisplayRead (actual, &capabilities))
		return false;
	if (s->mode == JUMP_DISPLAY_WINDOWED)
	{
		if (actual->window.width != s->window.width || actual->window.height != s->window.height)
			return false;
		display_state.window = actual->window;
	}
	return true;
}

static qboolean GLW_DisplayFallback (int vsync)
{
	jump_display_monitor_t monitor;
	jump_display_settings_t fallback, actual;
	if (!GLW_DisplayFind (NULL, &monitor))
		return false;
	memset (&fallback, 0, sizeof (fallback));
	Com_sprintf (fallback.display, sizeof (fallback.display), "%s", monitor.id);
	fallback.mode = JUMP_DISPLAY_WINDOWED;
	fallback.window.x = monitor.desktop.x;
	fallback.window.y = monitor.desktop.y;
	fallback.window.width = 640;
	fallback.window.height = 480;
	fallback.vsync = GLimp_DisplaySwapRead ();
	if (!GLW_DisplayApply (&fallback, &actual))
		return false;
	if (vsync != actual.vsync && !GLimp_DisplaySwapSet (vsync))
		GLW_DisplayMessage ("Restored a usable window; previous VSync could not be restored.");
	else
		GLW_DisplayMessage ("Previous display unavailable. Restored a window on an available monitor.");
	return true;
}

static qboolean EXPORT GLW_DisplayEnd (qboolean keep)
{
	jump_display_monitor_t monitor;
	qboolean restored;
	if (!display_state.checkpoint.valid)
		return false;
	if (display_state.checkpoint.hwnd != glw_state.hWnd || display_state.checkpoint.dc != glw_state.hDC ||
		display_state.checkpoint.context != glw_state.hGLRC)
	{
		display_state.checkpoint.valid = false;
		GLW_DisplayMessage ("The renderer changed before display recovery.");
		return false;
	}
	if (keep)
	{
		display_state.checkpoint.valid = false;
		GLW_DisplayMessage ("Display settings kept.");
		return true;
	}
	display_state.changing = true;
	restored = GLW_DisplayRestoreOwner () && GLW_DisplayFind (display_state.checkpoint.settings.display, &monitor);
	if (restored && display_state.checkpoint.owner[0])
	{
		memcpy (display_state.owner, display_state.checkpoint.owner, sizeof (display_state.owner));
		display_state.desktop = display_state.checkpoint.desktop;
		display_state.exclusive = display_state.checkpoint.exclusive;
		restored = display_change (display_state.owner, &display_state.exclusive, NULL, CDS_FULLSCREEN, NULL) == DISP_CHANGE_SUCCESSFUL;
	}
	display_state.window_flags = display_state.checkpoint.settings.window_flags;
	GLimp_DisplayWindowState (display_state.checkpoint.settings.mode);
	if (restored)
		restored = GLW_DisplayStyle (display_state.checkpoint.style, display_state.checkpoint.exstyle) &&
			SetWindowPos (glw_state.hWnd, display_state.checkpoint.exstyle & WS_EX_TOPMOST ? HWND_TOPMOST : HWND_NOTOPMOST,
				display_state.checkpoint.outer.left, display_state.checkpoint.outer.top,
				display_state.checkpoint.outer.right - display_state.checkpoint.outer.left,
				display_state.checkpoint.outer.bottom - display_state.checkpoint.outer.top,
				SWP_NOACTIVATE | SWP_FRAMECHANGED) &&
			SetWindowPlacement (glw_state.hWnd, &display_state.checkpoint.placement) && GLimp_DisplayDrawable ();
	if (restored)
	{
		display_state.window = display_state.checkpoint.settings.window;
		display_state.managed = display_state.checkpoint.managed;
		display_state.suspended = display_state.checkpoint.suspended;
		if (display_state.checkpoint.settings.vsync != GLimp_DisplaySwapRead () &&
			!GLimp_DisplaySwapSet (display_state.checkpoint.settings.vsync))
			GLW_DisplayMessage ("Previous window restored; previous VSync could not be restored.");
		else
			GLW_DisplayMessage ("Previous display settings restored.");
	}
	else
	{
		restored = GLW_DisplayFallback (display_state.checkpoint.settings.vsync);
		if (!restored)
			GLW_DisplayMessage ("Could not restore the display. Reconnect a monitor and retry.");
	}
	display_state.checkpoint.valid = display_state.changing = false;
	return restored;
}

static const char *EXPORT GLW_DisplayLastMessage (void)
{
	return display_state.message;
}

const refdisplay_t *GLW_GetDisplayAPI (void)
{
	static const refdisplay_t api = { REF_DISPLAY_VERSION, sizeof (refdisplay_t),
		GLW_DisplayAvailable, GLW_DisplayMonitors, GLW_DisplayModes, GLW_DisplayRead,
		GLW_DisplayBegin, GLW_DisplayApply, GLW_DisplayEnd, GLW_DisplayLastMessage };
	return &api;
}

qboolean GLW_DisplayBusy (void)
{
	return display_state.checkpoint.valid || display_state.changing;
}

int GLW_DisplayWindowFlags (void)
{
	return display_state.managed ? display_state.window_flags : 0;
}

qboolean GLW_DisplayConfigured (void)
{
	cvar_t *id, *borderless, *notitle, *noresize;
	id = ri.Cvar_Get ("vid_display", "", CVAR_ARCHIVE);
	borderless = ri.Cvar_Get ("win_borderless", "0", CVAR_ARCHIVE);
	notitle = ri.Cvar_Get ("win_notitle", "0", CVAR_ARCHIVE);
	noresize = ri.Cvar_Get ("win_noresize", "0", CVAR_ARCHIVE);
	return GLW_DisplayLoad () && (id->string[0] || borderless->intvalue || notitle->intvalue || noresize->intvalue);
}

qboolean GLW_DisplayStart (qboolean fullscreen, qboolean borderless, int width, int height, int refresh, int depth)
{
	jump_display_settings_t settings, actual;
	jump_display_monitor_t monitor;
	unsigned int capabilities;
	qboolean applied;
	const char *id;
	id = ri.Cvar_Get ("vid_display", "", CVAR_ARCHIVE)->string;
	if (!GLW_DisplayFind (id, &monitor) && !GLW_DisplayFind (NULL, &monitor))
		return false;
	if (!GLW_DisplayRead (&settings, &capabilities) || !GLW_DisplayBegin ())
		return false;
	Com_sprintf (settings.display, sizeof (settings.display), "%s", monitor.id);
	settings.mode = !fullscreen ? JUMP_DISPLAY_WINDOWED : borderless ? JUMP_DISPLAY_BORDERLESS :
																	   JUMP_DISPLAY_EXCLUSIVE;
	settings.width = width;
	settings.height = height;
	settings.refresh = refresh;
	settings.depth = depth;
	settings.window_flags = (ri.Cvar_Get ("win_borderless", "0", CVAR_ARCHIVE)->intvalue ? JUMP_DISPLAY_WINDOW_BORDERLESS : 0) |
		(ri.Cvar_Get ("win_notitle", "0", CVAR_ARCHIVE)->intvalue ? JUMP_DISPLAY_WINDOW_NOTITLE : 0) |
		(ri.Cvar_Get ("win_noresize", "0", CVAR_ARCHIVE)->intvalue ? JUMP_DISPLAY_WINDOW_NORESIZE : 0);
	applied = GLW_DisplayApply (&settings, &actual);
	if (!applied)
	{
		GLW_DisplayEnd (false);
		ri.Con_Printf (PRINT_ALL, "Saved display profile failed; requesting native safe-mode recovery.\n");
		return false;
	}
	return GLW_DisplayEnd (true);
}

void GLW_DisplayShutdown (void)
{
	if (display_state.checkpoint.valid)
		GLW_DisplayEnd (false);
	if (display_state.managed || display_state.owner[0])
	{
		if (!GLW_DisplayRestoreOwner ())
			ri.Con_Printf (PRINT_ALL, "Could not restore the owned display mode on shutdown.\n");
		gl_state.fullscreen = false;
	}
	memset (&display_state, 0, sizeof (display_state));
}

qboolean GLW_DisplayActivate (qboolean active)
{
	cvar_t *restore;
	if (!display_state.managed)
		return false;
	if (active)
	{
		if (display_state.suspended && display_state.owner[0])
		{
			if (display_change (display_state.owner, &display_state.exclusive, NULL, CDS_FULLSCREEN, NULL) != DISP_CHANGE_SUCCESSFUL)
			{
				ri.Con_Printf (PRINT_ALL, "Could not resume the exclusive display mode.\n");
				return true;
			}
			display_state.suspended = false;
		}
		SetForegroundWindow (glw_state.hWnd);
		ShowWindow (glw_state.hWnd, SW_RESTORE);
	}
	else if (display_state.owner[0])
	{
		restore = ri.Cvar_Get ("vid_restore_on_switch", "1", 0);
		if (restore->intvalue && !display_state.suspended &&
			display_change (display_state.owner, &display_state.desktop, NULL, 0, NULL) == DISP_CHANGE_SUCCESSFUL)
			display_state.suspended = true;
		ShowWindow (glw_state.hWnd, SW_MINIMIZE);
	}
	return true;
}
