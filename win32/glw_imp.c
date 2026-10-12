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
/*
** GLW_IMP.C
**
** This file contains ALL Win32 specific stuff having to do with the
** OpenGL refresh.  When a port is being made the following functions
** must be implemented by the port:
**
** GLimp_EndFrame
** GLimp_Init
** GLimp_Shutdown
** GLimp_SwitchFullscreen
**
*/
#include <float.h>
#include <windows.h>
#include "../ref_gl/gl_local.h"
#include "glw_win.h"
#include "winquake.h"
#include "../client/vid_config.h"
#include "wglext.h"
#include "glw_display.h"

//static qboolean GLimp_SwitchFullscreen( int width, int height );
qboolean GLimp_InitGL (void);
static void GLimp_ResetExtensions (void);

glwstate_t glw_state;

extern cvar_t *vid_fullscreen;
extern cvar_t *vid_borderless;
extern cvar_t *vid_ref;
extern cvar_t *vid_forcedrefresh;
extern cvar_t *vid_optimalrefresh;
extern cvar_t *vid_nowgl;

DEVMODE		originalDesktopMode;
DEVMODE		fullScreenMode;

qboolean	usingDesktopSettings;

static qboolean VerifyDriver( void )
{
	char buffer[1024];

	Q_strncpy( buffer, qglGetString( GL_RENDERER ), sizeof(buffer)-1);
	strlwr( buffer );
	if ( strcmp( buffer, "gdi generic" ) == 0 )
	{
		if ( !glw_state.mcd_accelerated )
			return false;
	}
	return true;
}

#ifndef REF_HARD_LINKED
BOOL WINAPI DllMain(HINSTANCE hDll,DWORD dwReason,LPVOID lpReserved)
{
	switch (dwReason)
	{
		case DLL_PROCESS_ATTACH:
			DisableThreadLibraryCalls (hDll);
			glw_state.hModule = hDll;
			break;
	}

	return TRUE;
}

#endif

/*
** VID_CreateWindow
*/
#define	WINDOW_CLASS_NAME	"Quake 2"
char	OPENGL_CLASS[32];

static qboolean		window_class_registered = false;


static qboolean glw_windowed;
static qboolean glw_geometry_changed;
static qboolean glw_window_ready;

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

/* Optional native display APIs; no additions to the client/renderer ABI. */
typedef struct
{
	DWORD size;
	RECT monitor, work;
	DWORD flags;
} glwmonitor_t;

static UINT (WINAPI *glw_get_window_dpi) (HWND);
static BOOL (WINAPI *glw_adjust_rect_dpi) (RECT *, DWORD, BOOL, DWORD, UINT);
static HANDLE (WINAPI *glw_monitor_from_rect) (const RECT *, DWORD);
static BOOL (WINAPI *glw_get_monitor_info) (HANDLE, glwmonitor_t *);

static void GLimp_InitDisplayAPI (void)
{
	HMODULE user32 = GetModuleHandle ("user32.dll");

	glw_get_window_dpi = NULL;
	glw_adjust_rect_dpi = NULL;
	glw_monitor_from_rect = NULL;
	glw_get_monitor_info = NULL;
	if (!user32)
		return;
	glw_get_window_dpi = (UINT (WINAPI *) (HWND))GetProcAddress (user32, "GetDpiForWindow");
	glw_adjust_rect_dpi = (BOOL (WINAPI *) (RECT *, DWORD, BOOL, DWORD, UINT))GetProcAddress (user32, "AdjustWindowRectExForDpi");
	glw_monitor_from_rect = (HANDLE (WINAPI *) (const RECT *, DWORD))GetProcAddress (user32, "MonitorFromRect");
	glw_get_monitor_info = (BOOL (WINAPI *) (HANDLE, glwmonitor_t *))GetProcAddress (user32, "GetMonitorInfoA");
}

static UINT GLimp_WindowDpi (HWND window)
{
	UINT dpi = glw_get_window_dpi && window ? glw_get_window_dpi (window) : 0;
	if (dpi < 96)
		return 96;
	if (dpi > 960)
		return 960;
	return dpi;
}

int GLimp_GetDpiScale (void)
{
	/* Match Q2PRO's nearest integral DPI scale, bounded to 1..10. */
	return (GLimp_WindowDpi (glw_state.hWnd) + 48) / 96;
}

static BOOL GLimp_AdjustWindowRect (RECT *rect, DWORD style, DWORD exstyle, UINT dpi)
{
	if (glw_adjust_rect_dpi)
		return glw_adjust_rect_dpi (rect, style, FALSE, exstyle, dpi);
	return AdjustWindowRect (rect, style, FALSE);
}

static void GLimp_ClampWindowPosition (int *x, int *y, int width, int height)
{
	RECT rect;
	glwmonitor_t info;
	int max_x, max_y;

	if (!glw_monitor_from_rect || !glw_get_monitor_info || width < 1 || height < 1 ||
		*x > 0x7fffffff - width || *y > 0x7fffffff - height)
		return;
	rect.left = *x;
	rect.top = *y;
	rect.right = *x + width;
	rect.bottom = *y + height;
	memset (&info, 0, sizeof (info));
	info.size = sizeof (info);
	if (!glw_get_monitor_info (glw_monitor_from_rect (&rect, 2), &info) ||
		info.work.right <= info.work.left || info.work.bottom <= info.work.top)
		return;
	/* Oversized windows keep their pixel size and align to the work area's origin. */
	max_x = (uint32)info.work.right - (uint32)info.work.left <= (uint32)width ?
		info.work.left :
		info.work.right - width;
	max_y = (uint32)info.work.bottom - (uint32)info.work.top <= (uint32)height ?
		info.work.top :
		info.work.bottom - height;
	if (*x < info.work.left)
		*x = info.work.left;
	if (*x > max_x)
		*x = max_x;
	if (*y < info.work.top)
		*y = info.work.top;
	if (*y > max_y)
		*y = max_y;
}

static void GLimp_FitWindow (int width, int height, DWORD style, DWORD exstyle, int *x, int *y)
{
	RECT rect;
	UINT dpi;
	int i, outer_width, outer_height;
	vid_geometry_t geometry;

	if (!glw_get_window_dpi || !glw_adjust_rect_dpi)
		return;
	/* Creation chooses the monitor. Use its DPI to retain the requested client pixels. */
	for (i = 0; i < 2; i++)
	{
		dpi = GLimp_WindowDpi (glw_state.hWnd);
		rect.left = rect.top = 0;
		rect.right = width;
		rect.bottom = height;
		if (!GLimp_AdjustWindowRect (&rect, style, exstyle, dpi))
			return;
		outer_width = rect.right - rect.left;
		outer_height = rect.bottom - rect.top;
		if (glw_windowed)
		{
			if (VID_ParseGeometry (vid_geometry->string, &geometry))
			{
				*x = geometry.x + rect.left;
				*y = geometry.y + rect.top;
			}
			GLimp_ClampWindowPosition (x, y, outer_width, outer_height);
		}
		if (!SetWindowPos (glw_state.hWnd, NULL, *x, *y, outer_width, outer_height,
				SWP_NOZORDER | SWP_NOACTIVATE))
			return;
		if (GLimp_WindowDpi (glw_state.hWnd) == dpi)
			break;
	}
}

/* Private display bridge; native transactions keep the existing context alive. */
qboolean GLimp_DisplayWindowed (void)
{
	return glw_windowed;
}

qboolean GLimp_DisplayAdjust (RECT *rect, DWORD style, DWORD exstyle)
{
	return GLimp_AdjustWindowRect (rect, style, exstyle, GLimp_WindowDpi (glw_state.hWnd));
}

void GLimp_DisplayWindowState (jump_display_kind_t mode)
{
	glw_windowed = mode == JUMP_DISPLAY_WINDOWED;
	gl_state.fullscreen = mode == JUMP_DISPLAY_EXCLUSIVE;
	usingDesktopSettings = false;
	glw_geometry_changed = true;
}

qboolean GLimp_DisplayDrawable (void)
{
	RECT rect;
	int width, height;
	if (!GetClientRect (glw_state.hWnd, &rect))
		return false;
	width = rect.right - rect.left;
	height = rect.bottom - rect.top;
	if (width < 64 || width >= 65536 || height < 48 || height >= 65536)
		return false;
	R_UpdateDrawableSize (width, height);
	glw_geometry_changed = false;
	return true;
}

qboolean GLimp_DisplayLegacyDesktop (DEVMODEA *desktop)
{
	if (!gl_state.fullscreen || originalDesktopMode.dmPelsWidth < 64 || originalDesktopMode.dmPelsHeight < 48)
		return false;
	*desktop = originalDesktopMode;
	desktop->dmSize = sizeof (*desktop);
	desktop->dmDriverExtra = 0;
	return true;
}

static void GLimp_SaveWindowGeometry (void);

/*
** Keep geometry notifications private to this renderer. Input messages retain
** the client window procedure supplied through Init.
*/
/* Q2PRO hardware-gamma mapping, with private device ownership. */
static HDC glw_gamma_dc;
static qboolean glw_gamma_owns_dc, glw_gamma_saved, glw_gamma_failed;
static qboolean glw_gamma_active = true;
static int glw_gamma_dirty = 3;
static WORD glw_gamma_original[3][256], glw_gamma_custom[3][256];
static byte glw_gamma_table[256];
static char glw_gamma_device[32];

static qboolean GLimp_GammaDevice (char *device)
{
	struct
	{
		DWORD size;
		RECT monitor, work;
		DWORD flags;
		char device[32];
	} info;

	RECT rect;
	HANDLE monitor;
	memset (device, 0, 32);
	if (!glw_monitor_from_rect || !glw_get_monitor_info)
		return true;
	if (!glw_state.hWnd || !GetWindowRect (glw_state.hWnd, &rect))
		return false;
	monitor = glw_monitor_from_rect (&rect, 2);
	memset (&info, 0, sizeof (info));
	info.size = sizeof (info);
	if (!monitor || !glw_get_monitor_info (monitor, (glwmonitor_t *)&info) ||
		!info.device[0] || !memchr (info.device, 0, sizeof (info.device)))
		return false;
	memcpy (device, info.device, sizeof (info.device));
	return true;
}

void GLimp_ShutdownGamma (void)
{
	if (glw_gamma_saved && !SetDeviceGammaRamp (glw_gamma_dc, glw_gamma_original))
		ri.Con_Printf (PRINT_ALL, "Could not restore the saved display gamma ramp.\n");
	if (glw_gamma_dc && glw_gamma_owns_dc)
		DeleteDC (glw_gamma_dc);
	glw_gamma_dc = NULL;
	glw_gamma_saved = glw_gamma_owns_dc = glw_gamma_failed = false;
	glw_gamma_device[0] = 0;
	glw_gamma_dirty = 3;
}

qboolean GLimp_UpdateGamma (const byte *table)
{
	char device[32];
	int i, channel;
	qboolean changed;
	if (!table || glw_gamma_failed)
		return false;
	changed = memcmp (glw_gamma_table, table, 256) != 0;
	if (!glw_gamma_dirty && !changed && glw_gamma_saved)
		return true;
	if (!GLimp_GammaDevice (device))
		return false;
	if (glw_gamma_saved && strcmp (device, glw_gamma_device))
	{
		if (!SetDeviceGammaRamp (glw_gamma_dc, glw_gamma_original))
		{
			glw_gamma_failed = true;
			return false;
		}
		glw_gamma_saved = false;
		GLimp_ShutdownGamma ();
	}
	if (!glw_gamma_saved)
	{
		glw_gamma_dc = device[0] ? CreateDCA (device, device, NULL, NULL) : glw_state.hDC;
		glw_gamma_owns_dc = device[0] != 0;
		if (!glw_gamma_dc || !GetDeviceGammaRamp (glw_gamma_dc, glw_gamma_original))
		{
			GLimp_ShutdownGamma ();
			return false;
		}
		glw_gamma_saved = true;
		memcpy (glw_gamma_device, device, sizeof (device));
	}
	if (!changed && !(glw_gamma_dirty & 2))
	{
		glw_gamma_dirty = 0;
		return true;
	}
	memmove (glw_gamma_table, table, 256);
	for (channel = 0; channel < 3; channel++)
		for (i = 0; i < 256; i++)
			glw_gamma_custom[channel][i] = (WORD)(table[i] << 8);
	if (!SetDeviceGammaRamp (glw_gamma_dc, glw_gamma_active ? glw_gamma_custom : glw_gamma_original))
	{
		SetDeviceGammaRamp (glw_gamma_dc, glw_gamma_original);
		glw_gamma_failed = true;
		return false;
	}
	glw_gamma_dirty = false;
	return true;
}

qboolean GLimp_InitGamma (const byte *table)
{
	GLimp_ShutdownGamma ();
	glw_gamma_active = GetForegroundWindow () == glw_state.hWnd && !IsIconic (glw_state.hWnd);
	if (GLimp_UpdateGamma (table))
		return true;
	GLimp_ShutdownGamma ();
	return false;
}

void GLimp_GammaActivate (qboolean active)
{
	if (active && (GetForegroundWindow () != glw_state.hWnd || IsIconic (glw_state.hWnd)))
		active = false;
	glw_gamma_active = active;
	if (!glw_gamma_saved)
		return;
	glw_gamma_dirty = 3;
	if (active)
		GLimp_UpdateGamma (glw_gamma_table);
	else if (!SetDeviceGammaRamp (glw_gamma_dc, glw_gamma_original))
		glw_gamma_failed = true;
}

static LRESULT CALLBACK GLimp_WndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT result;
	if (uMsg == WM_MOVE || uMsg == WM_SIZE || uMsg == WM_DPICHANGED)
		glw_gamma_dirty |= 1;
	if (uMsg == WM_DISPLAYCHANGE)
		glw_gamma_dirty |= 3;
	qboolean geometry_changed = false;

	if (uMsg == WM_WINDOWPOSCHANGED)
	{
		WINDOWPOS *position = (WINDOWPOS *)lParam;

		if ((position->flags & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE) ||
			(position->flags & SWP_FRAMECHANGED))
		{
			glw_geometry_changed = true;
			geometry_changed = true;
		}
	}
	else if (uMsg == WM_DPICHANGED)
	{
		glw_geometry_changed = true;
		return DefWindowProc (hWnd, uMsg, wParam, lParam);
	}
	else if (uMsg == WM_SIZE)
		return 0; /* Maximizing a resizable window does not select fullscreen. */
	else if (uMsg == WM_MOVE && (!glw_windowed || IsZoomed (hWnd) || IsIconic (hWnd)))
		return DefWindowProc (hWnd, uMsg, wParam, lParam);
	else if (uMsg == WM_STYLECHANGING && wParam == (WPARAM)GWL_STYLE && glw_windowed)
	{
		STYLESTRUCT *style = (STYLESTRUCT *)lParam;

		/* Restore the resize frame when the legacy hidden title bar returns. */
		if ((style->styleNew & WS_CAPTION) == WS_CAPTION &&
			!(GLW_DisplayWindowFlags () & (JUMP_DISPLAY_WINDOW_NORESIZE | JUMP_DISPLAY_WINDOW_BORDERLESS)))
			style->styleNew |= WS_THICKFRAME;
	}
	else if (uMsg == WM_GETMINMAXINFO && glw_windowed)
	{
		MINMAXINFO *limits = (MINMAXINFO *)lParam;
		RECT min_rect, max_rect;
		DWORD style;
		UINT dpi;

		style = (DWORD)GetWindowLong (hWnd, GWL_STYLE);
		dpi = GLimp_WindowDpi (hWnd);
		min_rect.left = min_rect.top = max_rect.left = max_rect.top = 0;
		min_rect.right = (320 * dpi) / 96;
		min_rect.bottom = (240 * dpi) / 96;
		max_rect.right = max_rect.bottom = 8192;
		if (GLimp_AdjustWindowRect (&min_rect, style, 0, dpi) &&
			GLimp_AdjustWindowRect (&max_rect, style, 0, dpi))
		{
			limits->ptMinTrackSize.x = min_rect.right - min_rect.left;
			limits->ptMinTrackSize.y = min_rect.bottom - min_rect.top;
			limits->ptMaxTrackSize.x = max_rect.right - max_rect.left;
			limits->ptMaxTrackSize.y = max_rect.bottom - max_rect.top;
		}
		return 0;
	}

	result = CallWindowProc ((WNDPROC)glw_state.wndproc, hWnd, uMsg, wParam, lParam);
	if (geometry_changed && glw_window_ready && hWnd == glw_state.hWnd)
		GLimp_SaveWindowGeometry ();
	return result;
}

/*
** Capture normal geometry before a quit can write the archived settings.
** Physical and scaled drawable changes remain deferred to a frame boundary.
*/
static void GLimp_SaveWindowGeometry (void)
{
	RECT rect;
	POINT origin;
	int width, height;
	cvar_t *ref;
	char geometry[64];

	if (GLW_DisplayBusy () || !glw_window_ready || !glw_windowed || !glw_state.hWnd ||
		IsIconic (glw_state.hWnd) || IsZoomed (glw_state.hWnd) ||
		((GetWindowLong (glw_state.hWnd, GWL_STYLE) & WS_CAPTION) != WS_CAPTION && !GLW_DisplayWindowFlags ()))
		return;

	/* A queued configuration change owns the next mode, not this old window. */
	ref = ri.Cvar_Get ("vid_ref", NULL, 0);
	if ((ref && ref->modified) || vid_geometry->modified || gl_mode->modified ||
		vid_fullscreen->modified || vid_borderless->modified ||
		gl_forcewidth->modified || gl_forceheight->modified ||
		vid_mode->modified || (vid_mode->intvalue && vid_modelist->modified))
		return;
	if (!GetClientRect (glw_state.hWnd, &rect))
		return;
	width = rect.right - rect.left;
	height = rect.bottom - rect.top;
	if (width < 320 || width > 8192 || height < 240 || height > 8192)
		return;

	origin.x = origin.y = 0;
	if (!ClientToScreen (glw_state.hWnd, &origin) ||
		origin.x < -32767 || origin.x > 32767 ||
		origin.y < -32767 || origin.y > 32767)
		return;

	Com_sprintf (geometry, sizeof (geometry), "%dx%d%+d%+d",
		width, height, (int)origin.x, (int)origin.y);
	ri.Cvar_Set ("vid_geometry", geometry);
	vid_geometry->modified = false; /* The existing window already has this geometry. */
}

/*
** Apply a coalesced move/resize at a frame boundary without replacing the GL
** context or assets. Retry geometry capture after transient native query failure.
*/
static void GLimp_UpdateWindowGeometry (void)
{
	RECT rect;
	int width, height;

	if (!glw_geometry_changed || !glw_windowed || !glw_state.hWnd ||
		IsIconic (glw_state.hWnd))
		return;
	if (!GetClientRect (glw_state.hWnd, &rect))
		return;

	glw_geometry_changed = false;
	width = rect.right - rect.left;
	height = rect.bottom - rect.top;
	if (width < 64 || width >= 0x10000 || height < 48 || height >= 0x10000)
		return;

	R_UpdateDrawableSize (width, height);
	GLimp_SaveWindowGeometry ();
}

qboolean init_regular (void);
int VID_CreateWindow( int width, int height, qboolean fullscreen )
{
	WNDCLASS		wc;
	RECT			r;
	cvar_t			*vid_xpos, *vid_ypos;
	int				stylebits;
	int				x, y, w, h;
	int				exstyle;
	int				error;
	vid_geometry_t geometry;

	glw_windowed = !fullscreen;
	glw_geometry_changed = false;
	glw_window_ready = false;

	if (!window_class_registered)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "window class is not registered\n");
		if (GetClassInfo (glw_state.hInstance, WINDOW_CLASS_NAME, &wc))
		{
			ri.Con_Printf (PRINT_DEVELOPER, "q2 window class already exists, unregistering: ");
			UnregisterClass (WINDOW_CLASS_NAME, wc.hInstance);
			ri.Con_Printf (PRINT_DEVELOPER, "ok\n");
		}

		/* Register the frame class */
		wc.style         = 0;
		wc.lpfnWndProc = GLimp_WndProc;
		wc.cbClsExtra    = 0;
		wc.cbWndExtra    = 0;
		wc.hInstance     = glw_state.hInstance;
		wc.hIcon         = 0;
		wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
		wc.hbrBackground = (HBRUSH)COLOR_GRAYTEXT;
		wc.lpszMenuName  = 0;
		wc.lpszClassName = WINDOW_CLASS_NAME;

		ri.Con_Printf (PRINT_DEVELOPER, "registering q2 window class: ");
		if (!RegisterClass (&wc))
		{
			char	*noglMsg = "";
			char	*msg;

			ri.Con_Printf (PRINT_DEVELOPER, "failed\n");

			FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&msg, 0, NULL);

			if (Q_strncasecmp (gl_driver->string, "opengl32", 8))
				noglMsg = va ("\r\n\r\nYou may be getting this message because your gl_driver is set to '%s'. Try adding 'set gl_driver \"opengl32\"' to your baseq2/r1gl.cfg if problems persist.", gl_driver->string);

			ri.Sys_Error (ERR_FATAL, "R1GL: Couldn't register window class: %s\r\n\r\nPlease make sure you have installed the latest drivers for your video card.%s", msg, noglMsg);
		}
		else
			ri.Con_Printf (PRINT_DEVELOPER, "ok\n");

		window_class_registered = true;
	}

	if (fullscreen)
	{
		exstyle = (FLOAT_NE_ZERO(vid_topmost->value) ? WS_EX_TOPMOST : 0);
		stylebits = WS_POPUP|WS_VISIBLE;
	}
	else
	{
		exstyle = 0;
		stylebits = WINDOW_STYLE | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
	}

	r.left = 0;
	r.top = 0;
	r.right  = width;
	r.bottom = height;

	AdjustWindowRect (&r, stylebits, FALSE);

	w = r.right - r.left;
	h = r.bottom - r.top;

	if (fullscreen)
	{
		x = 0;
		y = 0;
	}
	else
	{
		vid_xpos = ri.Cvar_Get ("vid_xpos", "0", 0);
		vid_ypos = ri.Cvar_Get ("vid_ypos", "0", 0);
		x = Q_ftol(vid_xpos->value);
		y = Q_ftol(vid_ypos->value);
		if (VID_ParseGeometry (vid_geometry->string, &geometry))
		{
			x = geometry.x + r.left;
			y = geometry.y + r.top;
		}
	}
	if (!fullscreen)
		GLimp_ClampWindowPosition (&x, &y, w, h);

	glw_state.hWnd = CreateWindowEx (
		exstyle,
		WINDOW_CLASS_NAME,
		"r1q2_race",
		stylebits,
		x, y, w, h,
		NULL,
		NULL,
		glw_state.hInstance,
		NULL);

	if (!glw_state.hWnd)
		ri.Sys_Error (ERR_FATAL, "Couldn't create window: %d", GetLastError());
	GLimp_FitWindow (width, height, stylebits, exstyle, &x, &y);

	ShowWindow( glw_state.hWnd, SW_SHOW );
	UpdateWindow( glw_state.hWnd );

	// init all the gl stuff for the window
	error = GLimp_InitGL ();

	if (error != VID_ERR_NONE)
	{
		if (error & VID_ERR_RETRY_QGL)
			return error;

		error = init_regular ();
		if (error != VID_ERR_NONE)
		{
			ri.Con_Printf( PRINT_ALL, "VID_CreateWindow() - GLimp_InitGL failed\n");
			return error;
		}
	}

	SetForegroundWindow( glw_state.hWnd );
	SetFocus( glw_state.hWnd );

	if (GetClientRect (glw_state.hWnd, &r) && r.right - r.left >= 64 &&
		r.right - r.left < 0x10000 && r.bottom - r.top >= 48 && r.bottom - r.top < 0x10000)
	{
		width = r.right - r.left;
		height = r.bottom - r.top;
	}
	R_UpdateDrawableSize (width, height);
	glw_geometry_changed = false;
	glw_window_ready = true;

	return VID_ERR_NONE;
}


/*
** GLimp_SetMode
*/
int GLimp_SetMode( unsigned int *pwidth, unsigned int *pheight, int mode, qboolean fullscreen )
{
	int	error;
	int width, height;
	qboolean borderless, explicit_size;
	vid_geometry_t geometry;
	vid_displaymode_t selected;
	const char *win_fs[] = { "W", "FS" };

	ri.Con_Printf( PRINT_ALL, "Initializing OpenGL display\n");

	borderless = fullscreen && FLOAT_NE_ZERO (vid_borderless->value);
	if (borderless)
		mode = -2;

	explicit_size = false;
	memset (&selected, 0, sizeof (selected));
	if (!borderless && !fullscreen && vid_geometry->string[0])
	{
		if (!VID_ParseGeometry (vid_geometry->string, &geometry))
		{
			ri.Con_Printf (PRINT_ALL, " invalid vid_geometry\n");
			return VID_ERR_INVALID_MODE;
		}
		width = geometry.width;
		height = geometry.height;
		explicit_size = true;
	}
	else if (!borderless && FLOAT_NE_ZERO (vid_mode->value))
	{
		if (!(vid_mode->value >= 1 && vid_mode->value <= VID_MAX_DISPLAY_MODES + 1) ||
			vid_mode->value != vid_mode->intvalue ||
			!VID_GetModeFromList (vid_modelist->string, vid_mode->intvalue, &selected))
		{
			ri.Con_Printf (PRINT_ALL, " invalid vid_mode/vid_modelist\n");
			return VID_ERR_INVALID_MODE;
		}
		if (selected.desktop)
			mode = -2;
		else
		{
			width = selected.width;
			height = selected.height;
			explicit_size = true;
		}
	}

	if (explicit_size)
	{
		/* Geometry/list dimensions take precedence over legacy forced sizes. */
	}
	else if (mode == -1)
	{
		ri.Con_Printf (PRINT_ALL, "...ignoring gl_mode, using forced width / height:");
		width = 0;
		height = 0;
	}
	else
	{
		ri.Con_Printf (PRINT_ALL, "...setting gl_mode %d:", mode );

		if ( !ri.Vid_GetModeInfo( &width, &height, mode ) )
		{
			ri.Con_Printf( PRINT_ALL, " invalid gl_mode\n" );
			return VID_ERR_INVALID_MODE;
		}
	}

	if (!explicit_size && mode != -2)
	{
		if (FLOAT_NE_ZERO (gl_forcewidth->value))
		{
			if (!(gl_forcewidth->value >= 64 && gl_forcewidth->value < 0x10000))
				return VID_ERR_INVALID_MODE;
			width = (int)gl_forcewidth->value;
		}
		if (FLOAT_NE_ZERO (gl_forceheight->value))
		{
			if (!(gl_forceheight->value >= 48 && gl_forceheight->value < 0x10000))
				return VID_ERR_INVALID_MODE;
			height = (int)gl_forceheight->value;
		}
	}
	if (width < 64 || width >= 0x10000 || height < 48 || height >= 0x10000)
		return VID_ERR_INVALID_MODE;

	ri.Con_Printf( PRINT_ALL, " %d %d %s\n", width, height, win_fs[fullscreen] );

	// destroy the existing window
	if (glw_state.hWnd)
	{
		GLimp_Shutdown ();
	}

	/* Saved private profiles use the selected monitor without first changing the primary. */
	if (GLW_DisplayConfigured ())
	{
		RECT actual;
		gl_state.fullscreen = false;
		error = VID_CreateWindow (width, height, false);
		if (error != VID_ERR_NONE)
			return error;
		if (!GLW_DisplayStart (fullscreen, borderless, fullscreen && mode == -2 ? 0 : width,
				fullscreen && mode == -2 ? 0 : height, selected.frequency, selected.depth) ||
			!GetClientRect (glw_state.hWnd, &actual))
			return VID_ERR_INVALID_MODE;
		*pwidth = actual.right - actual.left;
		*pheight = actual.bottom - actual.top;
		return VID_ERR_NONE;
	}

	if (borderless)
	{
		ri.Con_Printf (PRINT_ALL, "...setting borderless desktop fullscreen\n");
		*pwidth = width;
		*pheight = height;
		gl_state.fullscreen = false; /* No exclusive display-mode change. */
		return VID_CreateWindow (width, height, true);
	}

	// do a CDS if needed
	if ( fullscreen )
	{
		DEVMODE dm;

		int		index = 0;
		int		bestFrequency = 0;
		DEVMODE	settings;

		EnumDisplaySettings (NULL, ENUM_CURRENT_SETTINGS, &originalDesktopMode);

		ri.Con_Printf( PRINT_ALL, "...attempting fullscreen\n" );

		memset( &dm, 0, sizeof( dm ) );

		dm.dmSize = sizeof( dm );

		dm.dmPelsWidth  = width;
		dm.dmPelsHeight = height;
		dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT;

		if (selected.depth)
		{
			dm.dmBitsPerPel = selected.depth;
			dm.dmFields |= DM_BITSPERPEL;
		}
		else if (FLOAT_NE_ZERO (gl_bitdepth->value))
		{
			dm.dmBitsPerPel = Q_ftol(gl_bitdepth->value);
			dm.dmFields |= DM_BITSPERPEL;
			ri.Con_Printf( PRINT_ALL, "...using gl_bitdepth of %d\n", ( int ) gl_bitdepth->value );
		}
		else
		{
			ri.Con_Printf( PRINT_ALL, "...using desktop display depth of %d\n", originalDesktopMode.dmBitsPerPel );

			//r1: be explicit about this just in case
			dm.dmFields |= DM_BITSPERPEL;
			dm.dmBitsPerPel = originalDesktopMode.dmBitsPerPel;
		}

		memset (&settings, 0, sizeof(settings));
		settings.dmSize = sizeof(dm);

		for (index = 0; index < VID_MAX_DISPLAY_MODES &&
			 EnumDisplaySettings (NULL, index, &settings);
			 index++)
		{
			if (!(settings.dmFields & (DM_BITSPERPEL|DM_DISPLAYFREQUENCY|DM_PELSWIDTH|DM_PELSHEIGHT)))
				continue;

			if (settings.dmBitsPerPel == dm.dmBitsPerPel &&
				settings.dmPelsWidth == dm.dmPelsWidth &&
				settings.dmPelsHeight == dm.dmPelsHeight)
			{
				if (settings.dmDisplayFrequency > bestFrequency)
					bestFrequency = settings.dmDisplayFrequency;
			}
		}

		//r1: if running q2 at desktop res, inherit desktop refresh rate
		if (originalDesktopMode.dmFields & (DM_BITSPERPEL|DM_DISPLAYFREQUENCY|DM_PELSWIDTH|DM_PELSHEIGHT) &&
			originalDesktopMode.dmPelsWidth == dm.dmPelsWidth &&
			originalDesktopMode.dmPelsHeight == dm.dmPelsHeight &&
			originalDesktopMode.dmBitsPerPel == dm.dmBitsPerPel)
		{
			dm.dmFields |= DM_DISPLAYFREQUENCY;
			dm.dmDisplayFrequency = originalDesktopMode.dmDisplayFrequency;
		}

		// An explicit list refresh takes precedence over the legacy refresh policy.
		if (selected.frequency)
		{
			dm.dmFields |= DM_DISPLAYFREQUENCY;
			dm.dmDisplayFrequency = selected.frequency;
		}

		//r1: allow refresh overriding
		if (!selected.frequency && (originalDesktopMode.dmFields & DM_DISPLAYFREQUENCY))
		{
			if (FLOAT_NE_ZERO (vid_optimalrefresh->value))
			{
				dm.dmFields |= DM_DISPLAYFREQUENCY;
				dm.dmDisplayFrequency = bestFrequency;
			}
			else if (FLOAT_NE_ZERO(vid_forcedrefresh->value))
			{
				dm.dmFields |= DM_DISPLAYFREQUENCY;
				dm.dmDisplayFrequency = Q_ftol(vid_forcedrefresh->value);
			}
		}
		else
			ri.Con_Printf (PRINT_ALL, "...ignoring frequencies, no driver support\n");

		ri.Con_Printf( PRINT_ALL, "...calling CDS: " );
		if ( ChangeDisplaySettings( &dm, CDS_FULLSCREEN ) == DISP_CHANGE_SUCCESSFUL )
		{
			*pwidth = width;
			*pheight = height;

			gl_state.fullscreen = true;

			ri.Con_Printf( PRINT_ALL, "ok\n" );

			error = VID_CreateWindow (width, height, true);
			if (error != VID_ERR_NONE)
				return error;

			ri.Con_Printf (PRINT_DEVELOPER, "Checking fullscreen frequencies: \n");
			EnumDisplaySettings (NULL, ENUM_CURRENT_SETTINGS, &fullScreenMode);

			if (originalDesktopMode.dmFields & DM_DISPLAYFREQUENCY)
			{
				if (fullScreenMode.dmDisplayFrequency < bestFrequency)
					ri.Con_Printf (PRINT_ALL, "\2NOTE: You are currently using a refresh rate of %d Hz. Your monitor claims to support up to %d Hz at %dx%d. Consider increasing your refresh rate for better performance by setting vid_optimalrefresh 1\n", fullScreenMode.dmDisplayFrequency, bestFrequency, fullScreenMode.dmPelsWidth, fullScreenMode.dmPelsHeight);
			}
			ri.Con_Printf (PRINT_DEVELOPER, "ok\n");

			return VID_ERR_NONE;
		}
		else
		{
			*pwidth = width;
			*pheight = height;

			ri.Con_Printf( PRINT_ALL, "failed\n" );

			/* A selected native mode must not silently double its display width. */
			if (FLOAT_NE_ZERO (vid_mode->value))
			{
				ChangeDisplaySettings (NULL, 0);
				gl_state.fullscreen = false;
				return VID_ERR_FULLSCREEN_FAILED;
			}

			ri.Con_Printf( PRINT_ALL, "...calling CDS assuming dual monitors:" );

			dm.dmPelsWidth = width * 2;
			
			//should already still be set
			/*
			dm.dmPelsHeight = height;
			dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

			if ( FLOAT_NE_ZERO(gl_bitdepth->value) )
			{
				dm.dmBitsPerPel = Q_ftol(gl_bitdepth->value);
				dm.dmFields |= DM_BITSPERPEL;
			}*/

			/*
			** our first CDS failed, so maybe we're running on some weird dual monitor
			** system 
			*/
			if ( ChangeDisplaySettings( &dm, CDS_FULLSCREEN ) != DISP_CHANGE_SUCCESSFUL )
			{
				ri.Con_Printf( PRINT_ALL, " failed\n" );

				ri.Con_Printf( PRINT_ALL, "...setting windowed mode\n" );

				ChangeDisplaySettings( 0, 0 );

				*pwidth = width;
				*pheight = height;
				gl_state.fullscreen = false;
				return VID_ERR_FULLSCREEN_FAILED;
			}
			else
			{
				ri.Con_Printf( PRINT_ALL, " ok\n" );
				error = VID_CreateWindow (width, height, true);
				if (error != VID_ERR_NONE)
					return error;

				EnumDisplaySettings (NULL, ENUM_CURRENT_SETTINGS, &fullScreenMode);
				gl_state.fullscreen = true;
				return VID_ERR_NONE;
			}
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...setting windowed mode\n" );

		ChangeDisplaySettings( 0, 0 );

		*pwidth = width;
		*pheight = height;
		gl_state.fullscreen = false;
		error = VID_CreateWindow (width, height, false);
		if (error != VID_ERR_NONE)
			return error;
	}

	return VID_ERR_NONE;
}

/*
** GLimp_Shutdown
**
** This routine does all OS specific shutdown procedures for the OpenGL
** subsystem.  Under OpenGL this means NULLing out the current DC and
** HGLRC, deleting the rendering context, and releasing the DC acquired
** for the window.  The state structure is also nulled out.
**
*/
void GLimp_Shutdown( void )
{
	WNDCLASS		wc;

	GLimp_ShutdownGamma ();
	GLW_DisplayShutdown ();
	glw_window_ready = false;

#ifdef USE_MSGLOG
  if(hMsgLog) FreeLibrary(hMsgLog);
#endif

	if ( qwglMakeCurrent && !qwglMakeCurrent( NULL, NULL ) )
		ri.Con_Printf( PRINT_ALL, "ref_gl::R_Shutdown() - wglMakeCurrent failed\n");
	if ( glw_state.hGLRC )
	{
		if (  qwglDeleteContext && !qwglDeleteContext( glw_state.hGLRC ) )
			ri.Con_Printf( PRINT_ALL, "ref_gl::R_Shutdown() - wglDeleteContext failed\n");
		glw_state.hGLRC = NULL;
	}
	if (glw_state.hDC)
	{
		if ( !ReleaseDC( glw_state.hWnd, glw_state.hDC ) )
			ri.Con_Printf( PRINT_ALL, "ref_gl::R_Shutdown() - ReleaseDC failed\n" );
		glw_state.hDC   = NULL;
	}

	if (glw_state.hWnd)
	{
		ShowWindow (glw_state.hWnd, SW_HIDE);
		DestroyWindow (	glw_state.hWnd );
		glw_state.hWnd = NULL;
	}

	if ( glw_state.log_fp )
	{
		fclose( glw_state.log_fp );
		glw_state.log_fp = 0;
	}

	if (GetClassInfo (glw_state.hInstance, WINDOW_CLASS_NAME, &wc))
	{
		ri.Con_Printf (PRINT_DEVELOPER, "unregistering q2 window class: ");
		UnregisterClass (WINDOW_CLASS_NAME, wc.hInstance);
		ri.Con_Printf (PRINT_DEVELOPER, "ok\n");
	}

	if (GetClassInfo (glw_state.hInstance, OPENGL_CLASS, &wc))
	{
		ri.Con_Printf (PRINT_DEVELOPER, "unregistering opengl class: ");
		UnregisterClass (OPENGL_CLASS, wc.hInstance);
		ri.Con_Printf (PRINT_DEVELOPER, "ok\n");
	}

	window_class_registered = false;
	glw_windowed = false;
	glw_geometry_changed = false;
	GLimp_ResetExtensions ();

	if ( gl_state.fullscreen )
	{
		ChangeDisplaySettings( 0, 0 );
		gl_state.fullscreen = false;
	}
}


/*
** GLimp_Init
**
** This routine is responsible for initializing the OS specific portions
** of OpenGL.  Under Win32 this means dealing with the pixelformats and
** doing the wgl interface stuff.
*/
qboolean GLimp_Init( void *hinstance, void *wndproc )
{
#define OSR2_BUILD_NUMBER 1111

	OSVERSIONINFO	vinfo;

	GLimp_InitDisplayAPI ();
	if (!OPENGL_CLASS[0])
		Com_sprintf (OPENGL_CLASS, sizeof(OPENGL_CLASS), "R1GLOpenGLPFD-%u", GetTickCount());

#ifndef _M_AMD64
	//_controlfp( _PC_24, _MCW_PC );
#endif

	vinfo.dwOSVersionInfoSize = sizeof(vinfo);

	glw_state.allowdisplaydepthchange = false;

	if ( GetVersionEx( &vinfo) )
	{
		if ( vinfo.dwMajorVersion > 4 )
		{
			glw_state.allowdisplaydepthchange = true;
		}
		else if ( vinfo.dwMajorVersion == 4 )
		{
			if ( vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT )
			{
				glw_state.allowdisplaydepthchange = true;
			}
			else if ( vinfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS )
			{
				if ( LOWORD( vinfo.dwBuildNumber ) >= OSR2_BUILD_NUMBER )
				{
					glw_state.allowdisplaydepthchange = true;
				}
			}
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "GLimp_Init() - GetVersionEx failed\n" );
		return false;
	}

	glw_state.hInstance = ( HINSTANCE ) hinstance;
#ifdef REF_HARD_LINKED
	glw_state.hModule = (HINSTANCE)hinstance;
#endif
	glw_state.wndproc = wndproc;

	return true;
}

// Define entry points
#define WGL_SAMPLE_BUFFERS_ARB             0x2041
#define WGL_SAMPLES_ARB                    0x2042

// WGL_ARB_extensions_string
PFNWGLGETEXTENSIONSSTRINGARBPROC     wglGetExtensionsStringARB;

PFNWGLGETEXTENSIONSSTRINGEXTPROC     wglGetExtensionsStringEXT;

// WGL_ARB_pixel_format
PFNWGLGETPIXELFORMATATTRIBIVARBPROC  wglGetPixelFormatAttribivARB;
PFNWGLGETPIXELFORMATATTRIBFVARBPROC  wglGetPixelFormatAttribfvARB;
PFNWGLCHOOSEPIXELFORMATARBPROC       wglChoosePixelFormatARB;

static qboolean glw_swap_tear;
static int glw_swap_interval = 1;
static int glw_samples;

static qboolean GLimp_ValidProc (const void *proc)
{
	return proc && (size_t)proc > 3 && (size_t)proc != (size_t)-1;
}

#ifdef R1Q2_GL_CORE
static qboolean GLimp_ContextIsCore (void)
{
	const char *version = (const char *)qglGetString (GL_VERSION);
	int major = 0, minor = 0;
	GLint profile = 0;

	if (!version || sscanf (version, "%d.%d", &major, &minor) != 2 ||
		major < 3 || (major == 3 && minor < 2))
		return false;
	qglGetIntegerv (0x9126 /* GL_CONTEXT_PROFILE_MASK */, &profile);
	return (profile & 1 /* GL_CONTEXT_CORE_PROFILE_BIT */) != 0;
}

#endif

int _wglExtensionSupported (const char *extension);

/* Keep the selected pixel format and bootstrap context until the trial works. */
static qboolean GLimp_TryCoreContext (void)
{
	HGLRC (WINAPI * create_context)
	(HDC, HGLRC, const int *);
	HGLRC previous = glw_state.hGLRC, context;
	qboolean core = false;
	qboolean debug = ri.Cvar_Get ("gl_debug", "0", 0)->intvalue > 0;
	static const int versions[2][2] = { { 4, 3 }, { 3, 3 } };
	int i, trial, count, attributes[9];

#ifdef R1Q2_GL_CORE
	core = !Q_stricmp (ri.Cvar_Get ("gl_profile", "compatibility", CVAR_ARCHIVE)->string, "core");
#endif
	if (!core && !debug)
		return true;
	wglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)qwglGetProcAddress ("wglGetExtensionsStringARB");
	wglGetExtensionsStringEXT = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)qwglGetProcAddress ("wglGetExtensionsStringEXT");
	if (!GLimp_ValidProc (wglGetExtensionsStringARB))
		wglGetExtensionsStringARB = NULL;
	if (!GLimp_ValidProc (wglGetExtensionsStringEXT))
		wglGetExtensionsStringEXT = NULL;
	if (!_wglExtensionSupported ("WGL_ARB_create_context"))
	{
		ri.Con_Printf (PRINT_ALL, "WGL_ARB_create_context unavailable; retaining compatibility without a debug request.\n");
		return true;
	}
	create_context = (HGLRC (WINAPI *) (HDC, HGLRC, const int *))
		qwglGetProcAddress ("wglCreateContextAttribsARB");
	if (!GLimp_ValidProc (create_context))
	{
		ri.Con_Printf (PRINT_ALL, "wglCreateContextAttribsARB is missing; retaining compatibility without a debug request.\n");
		return true;
	}
	for (i = 0; i < (core ? 2 : 1); i++)
	{
		/* Retry the same profile and pixel format without debug before falling back. */
		for (trial = debug ? 1 : 0; trial >= 0; trial--)
		{
			count = 0;
			if (core)
			{
				attributes[count++] = 0x2091; /* WGL_CONTEXT_MAJOR_VERSION_ARB */
				attributes[count++] = versions[i][0];
				attributes[count++] = 0x2092; /* WGL_CONTEXT_MINOR_VERSION_ARB */
				attributes[count++] = versions[i][1];
				attributes[count++] = 0x9126; /* WGL_CONTEXT_PROFILE_MASK_ARB */
				attributes[count++] = 1;	  /* WGL_CONTEXT_CORE_PROFILE_BIT_ARB */
			}
			if (trial)
			{
				attributes[count++] = 0x2094; /* WGL_CONTEXT_FLAGS_ARB */
				attributes[count++] = 1;	  /* WGL_CONTEXT_DEBUG_BIT_ARB */
			}
			attributes[count] = 0;
			context = create_context (glw_state.hDC, NULL, attributes);
			if (!context)
			{
				ri.Con_Printf (PRINT_ALL, "OpenGL %s context creation failed%s (%lu).\n",
					core ? "core" : "compatibility", trial ? " with debug requested" : "", GetLastError ());
				continue;
			}
			if (qwglMakeCurrent (glw_state.hDC, context)
#ifdef R1Q2_GL_CORE
				&& (!core || GLimp_ContextIsCore ())
#endif
			)
			{
				glw_state.hGLRC = context;
				qwglDeleteContext (previous);
				if (core)
					ri.Con_Printf (PRINT_ALL, "Using OpenGL %d.%d core profile.\n", versions[i][0], versions[i][1]);
				return true;
			}
			ri.Con_Printf (PRINT_ALL, "OpenGL context activation failed%s; restoring compatibility.\n",
				trial ? " with debug requested" : "");
			if (!qwglMakeCurrent (glw_state.hDC, previous))
			{
				qwglMakeCurrent (NULL, NULL);
				qwglDeleteContext (context);
				ri.Con_Printf (PRINT_ALL, "Compatibility context restoration failed.\n");
				return false;
			}
			qwglDeleteContext (context);
		}
	}
	ri.Con_Printf (PRINT_ALL, "Requested OpenGL context unavailable; retaining compatibility.\n");
	return true;
}

#ifdef R1Q2_GL_CORE
static int GLimp_CoreExtensionSupported (const char *extension)
{
	const GLubyte *(APIENTRY * get_string) (GLenum, GLuint);
	GLint count = 0;
	int i;

	if (!extension || !*extension || strchr (extension, ' '))
		return 0;
	get_string = (const GLubyte *(APIENTRY *)(GLenum, GLuint))qwglGetProcAddress ("glGetStringi");
	if (!GLimp_ValidProc (get_string))
		return 0;
	qglGetIntegerv (0x821D /* GL_NUM_EXTENSIONS */, &count);
	if (count < 0 || count > 65536)
		return 0;
	for (i = 0; i < count; i++)
	{
		const char *name = (const char *)get_string (GL_EXTENSIONS, i);
		if (name && !strcmp (name, extension))
			return 1;
	}
	return 0;
}
#endif

static void GLimp_ResetExtensions (void)
{
	wglGetExtensionsStringARB = NULL;
	wglGetExtensionsStringEXT = NULL;
	wglGetPixelFormatAttribivARB = NULL;
	wglGetPixelFormatAttribfvARB = NULL;
	wglChoosePixelFormatARB = NULL;
	qwglSwapIntervalEXT = NULL;
	glw_swap_tear = false;
	glw_swap_interval = 1;
	glw_samples = 0;
}

qboolean _is_multisample;

// Determine if an OpenGL extension is supported.
int _glExtensionSupported(const char *extension)
{
#ifdef REF_HARD_LINKED
	const GLubyte *extensions = NULL;
#else
	static const GLubyte *extensions = NULL;
#endif
	const GLubyte *start;
    GLubyte *where, *terminator;

#ifdef R1Q2_GL_CORE
	if (GLimp_ContextIsCore ())
		return GLimp_CoreExtensionSupported (extension);
#endif
	extensions = qglGetString(GL_EXTENSIONS);

    // Extension names should not have spaces.
    where = (GLubyte *) strchr(extension, ' ');
    if (where || *extension == '\0')
        return 0;

    // It takes a bit of care to be fool-proof about parsing the
    // OpenGL extensions string.  Don't be fooled by sub-strings,
    // etc.
    start = extensions;
    for (;;)
    {
        where = (GLubyte *) strstr((const char *) start, extension);
        if (!where)
            break;
        terminator = where + strlen(extension);
        if (where == start || *(where - 1) == ' ')
            if (*terminator == ' ' || *terminator == '\0')
                return 1;

        start = terminator;
    }
    return 0;
}

// Determine if an OpenGL WGL extension is supported.
//
// NOTE:  This routine uses wglGetProcAddress so this routine REQUIRES
// that the calling thread is bound to a hardware-accelerated OpenGL
// rendering context.
int _wglExtensionSupported (const char *extension)
{
	const char *extensions = NULL, *start, *where, *end;

	if (!extension || !*extension || strchr (extension, ' '))
		return 0;
	if (wglGetExtensionsStringARB && qwglGetCurrentDC)
		extensions = wglGetExtensionsStringARB (qwglGetCurrentDC ());
	if ((!extensions || !*extensions) && wglGetExtensionsStringEXT)
		extensions = wglGetExtensionsStringEXT ();
#ifdef R1Q2_GL_CORE
	if ((!extensions || !*extensions) && GLimp_ContextIsCore ())
		return 0;
#endif
	if (!extensions || !*extensions)
		extensions = (const char *)qglGetString (GL_EXTENSIONS);
	if (!extensions)
		return 0;
	for (start = extensions; (where = strstr (start, extension)) != NULL; start = end)
	{
		end = where + strlen (extension);
		if ((where == extensions || where[-1] == ' ') && (!*end || *end == ' '))
			return 1;
	}
	return 0;
}

static int GLimp_RequestedSamples (void)
{
	cvar_t *samples = ri.Cvar_Get ("gl_multisamples", "", CVAR_ARCHIVE);
	double value;

	if (*samples->string)
		value = samples->value;
	else
		value = FLOAT_NE_ZERO (gl_ext_multisample->value) ? gl_ext_samples->value : 0;
	if (!(value >= 0) || value > FLT_MAX)
	{
		ri.Con_Printf (PRINT_ALL, "Invalid multisample count; disabling MSAA.\n");
		return 0;
	}
	if (value > 32)
		value = 32;
	return value < 2 ? 0 : (int)value;
}

qboolean init_extensions (void)
{
	glw_samples = GLimp_RequestedSamples ();
	_is_multisample = glw_samples && _wglExtensionSupported ("WGL_ARB_multisample");
	if (glw_samples && !_is_multisample)
		ri.Con_Printf (PRINT_ALL, "WGL_ARB_multisample not found; MSAA disabled.\n");
	if (!_wglExtensionSupported ("WGL_ARB_pixel_format"))
		return false;
	wglGetPixelFormatAttribivARB = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)qwglGetProcAddress ("wglGetPixelFormatAttribivARB");
	wglGetPixelFormatAttribfvARB = (PFNWGLGETPIXELFORMATATTRIBFVARBPROC)qwglGetProcAddress ("wglGetPixelFormatAttribfvARB");
	wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)qwglGetProcAddress ("wglChoosePixelFormatARB");
	if (!GLimp_ValidProc (wglGetPixelFormatAttribivARB) || !GLimp_ValidProc (wglChoosePixelFormatARB))
	{
		wglGetPixelFormatAttribivARB = NULL;
		wglGetPixelFormatAttribfvARB = NULL;
		wglChoosePixelFormatARB = NULL;
		return false;
	}
	if (!GLimp_ValidProc (wglGetPixelFormatAttribfvARB))
		wglGetPixelFormatAttribfvARB = NULL;
	return true;
}

void GLimp_InitSwapControl (void)
{
	qwglSwapIntervalEXT = NULL;
	glw_swap_tear = false;
	wglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)qwglGetProcAddress ("wglGetExtensionsStringARB");
	wglGetExtensionsStringEXT = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)qwglGetProcAddress ("wglGetExtensionsStringEXT");
	if (!GLimp_ValidProc (wglGetExtensionsStringARB))
		wglGetExtensionsStringARB = NULL;
	if (!GLimp_ValidProc (wglGetExtensionsStringEXT))
		wglGetExtensionsStringEXT = NULL;
	if (_wglExtensionSupported ("WGL_EXT_swap_control"))
		qwglSwapIntervalEXT = (BOOL (WINAPI *) (int))qwglGetProcAddress ("wglSwapIntervalEXT");
	if (!GLimp_ValidProc (qwglSwapIntervalEXT))
	{
		qwglSwapIntervalEXT = NULL;
		ri.Con_Printf (PRINT_ALL, "WGL_EXT_swap_control not found.\n");
		return;
	}
	glw_swap_tear = _wglExtensionSupported ("WGL_EXT_swap_control_tear");
}

qboolean GLimp_TrySwapInterval (int interval)
{
	if (!qwglSwapIntervalEXT)
		return false;
	if (interval < 0 && !glw_swap_tear)
	{
		ri.Con_Printf (PRINT_ALL, "Negative swap interval is unsupported; using positive interval.\n");
		interval = interval == (-2147483647 - 1) ? 2147483647 : -interval;
	}
	if (!qwglSwapIntervalEXT (interval))
	{
		ri.Con_Printf (PRINT_ALL, "wglSwapIntervalEXT(%d) failed.\n", interval);
		return false;
	}
	glw_swap_interval = interval;
	return true;
}

void GLimp_SwapInterval (int interval)
{
	GLimp_TrySwapInterval (interval);
}

unsigned int GLimp_DisplayCapabilities (void)
{
#ifdef STEREO_SUPPORT
	if (gl_state.stereo_enabled)
		return 0;
#endif
	return !qwglSwapIntervalEXT ? 0 : JUMP_DISPLAY_CAP_VSYNC | (glw_swap_tear ? JUMP_DISPLAY_CAP_ADAPTIVE : 0);
}

int GLimp_DisplaySwapRead (void)
{
	int (WINAPI * get_interval) (void);
	if (qwglSwapIntervalEXT && qwglGetProcAddress)
	{
		get_interval = (int (WINAPI *) (void))qwglGetProcAddress ("wglGetSwapIntervalEXT");
		if (GLimp_ValidProc (get_interval))
			return get_interval ();
	}
	return glw_swap_interval;
}

qboolean GLimp_DisplaySwapSet (int interval)
{
#ifdef STEREO_SUPPORT
	if (gl_state.stereo_enabled)
		return false;
#endif
	return GLimp_TrySwapInterval (interval);
}


// don't do anything
static LRESULT CALLBACK StupidOpenGLProc(HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
  return DefWindowProc(hWnd,msg,wParam,lParam);
}

// Registers the window classes
BOOL RegisterOpenGLWindow(HINSTANCE hInst)
{
	WNDCLASSEX wcex;

	// Initialize our Window class
	wcex.cbSize = sizeof(wcex);

	if (GetClassInfoEx (hInst, OPENGL_CLASS, &wcex))
	{
		ri.Con_Printf (PRINT_DEVELOPER, "opengl class already exists, unregistering: ");
		UnregisterClass (OPENGL_CLASS, wcex.hInstance);
		ri.Con_Printf (PRINT_DEVELOPER, "ok\n");
	}

	// register main one
	ZeroMemory(&wcex,sizeof(wcex));

	// now the stupid one
	wcex.cbSize			= sizeof(wcex);
	wcex.style			= CS_OWNDC;
	wcex.cbWndExtra		= 0; /* space for our pointer */
	//wcex.lpfnWndProc	= StupidOpenGLProc;
	wcex.lpfnWndProc	= DefWindowProc;
	wcex.hbrBackground	= NULL;
	wcex.hInstance		= hInst;
	wcex.hCursor		= LoadCursor(NULL,IDC_ARROW);
	wcex.lpszClassName	= OPENGL_CLASS;

	ri.Con_Printf (PRINT_DEVELOPER, "registering opengl window class: ");
	if (!RegisterClassEx (&wcex))
	{
		ri.Con_Printf (PRINT_DEVELOPER, "failed\n");
		ri.Sys_Error (ERR_FATAL, "R1GL: Unable to register OpenGL window (%d).\r\n\r\nTry adding 'set vid_nowgl 1' to your baseq2/r1gl.cfg", GetLastError());
		return FALSE;
	}

	ri.Con_Printf (PRINT_DEVELOPER, "ok\n");

	return TRUE;
}

qboolean init_regular (void)
{
    PIXELFORMATDESCRIPTOR pfd = 
	{
		sizeof(PIXELFORMATDESCRIPTOR),	// size of this pfd
		1,								// version number
		PFD_DRAW_TO_WINDOW |			// support window
		PFD_SUPPORT_OPENGL |			// support OpenGL
		PFD_DOUBLEBUFFER,				// double buffered
		PFD_TYPE_RGBA,					// RGBA type
		24,								// 24-bit color depth
		0, 0, 0, 0, 0, 0,				// color bits ignored
		0,								// no alpha buffer
		0,								// shift bit ignored
		0,								// no accumulation buffer
		0, 0, 0, 0, 					// accum bits ignored
		32,								// 32-bit z-buffer	
		0,								// no stencil buffer
		0,								// no auxiliary buffer
		PFD_MAIN_PLANE,					// main layer
		0,								// reserved
		0, 0, 0							// layer masks ignored
    };
    int  pixelformat;

#ifdef STEREO_SUPPORT
	cvar_t *stereo;
	
	stereo = ri.Cvar_Get( "cl_stereo", "0", 0 );

	/*
	** set PFD_STEREO if necessary
	*/
	if ( FLOAT_NE_ZERO(stereo->value))
	{
		ri.Con_Printf( PRINT_ALL, "...attempting to use stereo\n" );
		pfd.dwFlags |= PFD_STEREO;
		gl_state.stereo_enabled = true;
	}
	else
	{
		gl_state.stereo_enabled = false;
	}
#endif

	/*
	** figure out if we're running on a minidriver or not
	*/
	if ( strstr( gl_driver->string, "opengl32" ) != 0 )
		glw_state.minidriver = false;
	else
		glw_state.minidriver = true;

	/*
	** Get a DC for the specified window
	*/
	if ( glw_state.hDC != NULL )
		ri.Con_Printf( PRINT_ALL, "GLimp_Init() - non-NULL DC exists\n" );

    if ( ( glw_state.hDC = GetDC( glw_state.hWnd ) ) == NULL )
	{
		ri.Con_Printf( PRINT_ALL, "GLimp_Init() - GetDC failed\n" );
		return VID_ERR_FAIL;
	}

	if ( glw_state.minidriver )
	{
		if ( (pixelformat = qwglChoosePixelFormat( glw_state.hDC, &pfd)) == 0 )
		{
			ri.Con_Printf (PRINT_ALL, "GLimp_Init() - qwglChoosePixelFormat failed\n");
			return VID_ERR_FAIL;
		}
		if ( qwglSetPixelFormat( glw_state.hDC, pixelformat, &pfd) == FALSE )
		{
			ri.Con_Printf (PRINT_ALL, "GLimp_Init() - qwglSetPixelFormat failed\n");
			return VID_ERR_FAIL;
		}
		qwglDescribePixelFormat( glw_state.hDC, pixelformat, sizeof( pfd ), &pfd );
	}
	else
	{
		if ( ( pixelformat = ChoosePixelFormat( glw_state.hDC, &pfd)) == 0 )
		{
			ri.Con_Printf (PRINT_ALL, "GLimp_Init() - ChoosePixelFormat failed\n");
			return VID_ERR_FAIL;
		}
		if ( SetPixelFormat( glw_state.hDC, pixelformat, &pfd) == FALSE )
		{
			ri.Con_Printf (PRINT_ALL, "GLimp_Init() - SetPixelFormat failed\n");
			return VID_ERR_FAIL;
		}
		DescribePixelFormat( glw_state.hDC, pixelformat, sizeof( pfd ), &pfd );

		if ( !( pfd.dwFlags & PFD_GENERIC_ACCELERATED ) )
		{
			extern cvar_t *gl_allow_software;

			if ( FLOAT_NE_ZERO(gl_allow_software->value) )
				glw_state.mcd_accelerated = true;
			else
				glw_state.mcd_accelerated = false;
		}
		else
		{
			glw_state.mcd_accelerated = true;
		}
	}

#ifdef STEREO_SUPPORT
	/*
	** report if stereo is desired but unavailable
	*/
	if ( !( pfd.dwFlags & PFD_STEREO ) && ( FLOAT_NE_ZERO(stereo->value)) ) 
	{
		ri.Con_Printf( PRINT_ALL, "...failed to select stereo pixel format\n" );
		ri.Cvar_SetValue( "cl_stereo", 0 );
		gl_state.stereo_enabled = false;
	}
#endif

	/*
	** startup the OpenGL subsystem by creating a context and making
	** it current
	*/
	if ( ( glw_state.hGLRC = qwglCreateContext( glw_state.hDC ) ) == 0 )
	{
		ri.Con_Printf (PRINT_ALL, "GLimp_Init() - qwglCreateContext failed\n");

		goto fail;
	}

    if ( !qwglMakeCurrent( glw_state.hDC, glw_state.hGLRC ) )
	{
		ri.Con_Printf (PRINT_ALL, "GLimp_Init() - qwglMakeCurrent failed\n");

		goto fail;
	}

	if (!GLimp_TryCoreContext ())
		goto fail;

	if ( !VerifyDriver() )
	{
		const char *extraMsg;

		ri.Con_Printf( PRINT_ALL, "GLimp_Init() - no hardware acceleration detected\n" );

		if (strcmp (gl_driver->string, "opengl32"))
		{
			ri.Con_Printf( PRINT_ALL, "GLimp_Init() - trying again with gl_driver 'opengl32'\n" );
			ri.Cvar_Set ("gl_driver", "opengl32");

			if ( glw_state.hGLRC )
			{
				qwglDeleteContext( glw_state.hGLRC );
				glw_state.hGLRC = NULL;
			}

			if ( glw_state.hDC )
			{
				ReleaseDC( glw_state.hWnd, glw_state.hDC );
				glw_state.hDC = NULL;
			}

			return VID_ERR_RETRY_QGL;
		}

		if (GetFileAttributes("opengl32.dll") != -1)
			extraMsg = "\r\n\r\nYou may also be getting this problem due to an invalid opengl32.dll file in your Quake II directory. Delete it and try running R1GL again.";
		else
			extraMsg = "";

		ri.Sys_Error (ERR_FATAL, "R1GL could not setup a hardware accelerated OpenGL window.\r\n\r\nPlease check you have installed the latest drivers for your video card and that it supports OpenGL.%s", extraMsg);
		goto fail;
	}

	/*
	** print out PFD specifics 
	*/
	ri.Con_Printf( PRINT_ALL, "GL PFD: color(%d-bits) Z(%d-bit)\n", ( int ) pfd.cColorBits, ( int ) pfd.cDepthBits );

	return VID_ERR_NONE;

fail:
	if ( glw_state.hGLRC )
	{
		qwglDeleteContext( glw_state.hGLRC );
		glw_state.hGLRC = NULL;
	}

	if ( glw_state.hDC )
	{
		ReleaseDC( glw_state.hWnd, glw_state.hDC );
		glw_state.hDC = NULL;
	}
	return VID_ERR_FAIL;
}
/*
qboolean init_regular (void)
{
    PIXELFORMATDESCRIPTOR pfd = 
	{
		sizeof(PIXELFORMATDESCRIPTOR),	// size of this pfd
		1,								// version number
		PFD_DRAW_TO_WINDOW |			// support window
		PFD_SUPPORT_OPENGL |			// support OpenGL
		PFD_GENERIC_ACCELERATED |		// accelerated
		PFD_DOUBLEBUFFER,				// double buffered
		PFD_TYPE_RGBA,					// RGBA type
		24,								// 24-bit color depth
		0, 0, 0, 0, 0, 0,				// color bits ignored
		0,								// no alpha buffer
		0,								// shift bit ignored
		0,								// no accumulation buffer
		0, 0, 0, 0, 					// accum bits ignored
		32,								// 32-bit z-buffer	
		0,								// no stencil buffer
		0,								// no auxiliary buffer
		PFD_MAIN_PLANE,					// main layer
		0,								// reserved
		0, 0, 0							// layer masks ignored
    };

    int  pixelformat;

	if ( ( glw_state.hDC = GetDC( glw_state.hWnd ) ) == NULL )
	{
		ri.Con_Printf( PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº GetDC failed\n" );
		return false;
	}

	if ( ( pixelformat = ChoosePixelFormat( glw_state.hDC, &pfd)) == 0 )
	{
		ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº ChoosePixelFormat failed\n");
		return false;
	}

	if ( SetPixelFormat( glw_state.hDC, pixelformat, &pfd) == FALSE )
	{
		ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº SetPixelFormat failed\n");
		return false;
	}

	DescribePixelFormat( glw_state.hDC, pixelformat, sizeof( pfd ), &pfd );

	if ( !( pfd.dwFlags & PFD_GENERIC_ACCELERATED ) )
	{
		extern cvar_t *gl_allow_software;

		if ( gl_allow_software->value )
			glw_state.mcd_accelerated = true;
		else
			glw_state.mcd_accelerated = false;
	}
	else
	{
		glw_state.mcd_accelerated = true;
	}

#ifdef STEREO_SUPPORT
	if ( !( pfd.dwFlags & PFD_STEREO ) && ( stereo->value != 0 ) ) 
	{
		ri.Con_Printf( PRINT_ALL, "...failed to select stereo pixel format\n" );
		ri.Cvar_SetValue( "cl_stereo", 0 );
		gl_state.stereo_enabled = false;
	}
#endif

	if ( ( glw_state.hGLRC = qwglCreateContext( glw_state.hDC ) ) == 0 )
	{
		ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº qwglCreateContext failed\n");

		goto fail;
	}

	if ( !qwglMakeCurrent( glw_state.hDC, glw_state.hGLRC ) )
	{
		ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº qwglMakeCurrent failed\n");

		goto fail;
	}

	return true;

fail:
	if ( glw_state.hGLRC )
	{
		qwglDeleteContext( glw_state.hGLRC );
		glw_state.hGLRC = NULL;
	}

	if ( glw_state.hDC )
	{
		ReleaseDC( glw_state.hWnd, glw_state.hDC );
		glw_state.hDC = NULL;
	}
	return false;
}*/

qboolean GLimp_InitGL (void)
{
	HGLRC	hGLRC;
	HWND	temphwnd;
	HDC		hDC;

    /*PIXELFORMATDESCRIPTOR pfd = 
	{
		sizeof(PIXELFORMATDESCRIPTOR),	// size of this pfd
		1,								// version number
		PFD_DRAW_TO_WINDOW |			// support window
		PFD_SUPPORT_OPENGL |			// support OpenGL
		PFD_GENERIC_ACCELERATED |		// accelerated
		PFD_DOUBLEBUFFER,				// double buffered
		PFD_TYPE_RGBA,					// RGBA type
		24,								// 24-bit color depth
		0, 0, 0, 0, 0, 0,				// color bits ignored
		0,								// no alpha buffer
		0,								// shift bit ignored
		0,								// no accumulation buffer
		0, 0, 0, 0, 					// accum bits ignored
		32,								// 32-bit z-buffer	
		0,								// no stencil buffer
		0,								// no auxiliary buffer
		PFD_MAIN_PLANE,					// main layer
		0,								// reserved
		0, 0, 0							// layer masks ignored
    };

    int  pixelformat;*/
#ifdef STEREO_SUPPORT
	cvar_t *stereo;
#endif

#ifdef STEREO_SUPPORT
	stereo = ri.Cvar_Get( "cl_stereo", "0", 0 );

	/*
	** set PFD_STEREO if necessary
	*/
	if ( FLOAT_NE_ZERO(stereo->value))
	{
		ri.Con_Printf( PRINT_ALL, "...attempting to use stereo\n" );
		pfd.dwFlags |= PFD_STEREO;
		gl_state.stereo_enabled = true;
	}
	else
	{
		gl_state.stereo_enabled = false;
	}
#endif

	if (FLOAT_NE_ZERO(vid_nowgl->value))
		return init_regular ();

	glw_state.minidriver = false;

	/*
	** Get a DC for the specified window
	*/
	if ( glw_state.hDC != NULL )
		ri.Con_Printf( PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº non-NULL DC exists\n" );

	{
		WORD ramps[3][256];
		DEVMODE dm;
		HDC hdDesk;
		memset( &dm, 0, sizeof( dm ) );
		hdDesk = GetDC( GetDesktopWindow() );

		dm.dmSize = sizeof( dm );
		dm.dmBitsPerPel = GetDeviceCaps( hdDesk, BITSPIXEL );
		dm.dmFields     = DM_BITSPERPEL;

		if (GetDeviceGammaRamp (hdDesk, ramps))
		{
			if (ramps[0][0] > 0 || ramps[1][0] > 0 || ramps[2][0] > 0)
				ri.Cvar_Get ("vid_hwgamma_status", va ("GDGR: %d-%d-%d", ramps[0][0], ramps[1][0], ramps[2][0]), CVAR_NOSET);
		}

		ReleaseDC( GetDesktopWindow(), hdDesk );

		if (FLOAT_LE_ZERO(gl_colorbits->value)) 
			ri.Cvar_Set ("gl_colorbits", dm.dmBitsPerPel == 32 ? "24" : "16");

		if (FLOAT_LE_ZERO(gl_depthbits->value))
			ri.Cvar_Set ("gl_depthbits", dm.dmBitsPerPel == 32 ? "24" : "16");

		if (!*gl_alphabits->string)
			ri.Cvar_Set ("gl_alphabits", dm.dmBitsPerPel == 32 ? "8" : "0");

		if (!*gl_stencilbits->string)
			ri.Cvar_Set ("gl_stencilbits", dm.dmBitsPerPel == 32 ? "8" : "0");
	}

	if (gl_colorbits->value < 24)
	{
		if (FLOAT_NE_ZERO(gl_alphabits->value))
		{
			ri.Con_Printf (PRINT_ALL, "GLimp_InitGL() - disabling gl_alphabits with colorbits %d\n", (int)gl_colorbits->value);
			ri.Cvar_Set ("gl_alphabits", "0");
		}
		if (FLOAT_NE_ZERO(gl_stencilbits->value))
		{
			ri.Con_Printf (PRINT_ALL, "GLimp_InitGL() - disabling gl_stencilbits with colorbits %d\n", (int)gl_colorbits->value);
			ri.Cvar_Set ("gl_stencilbits", "0");
		}
	}

	RegisterOpenGLWindow (glw_state.hInstance);

	{
		int     iAttributes[30];
		float   fAttributes[] = {0, 0};
		int     iResults[30];
		//int     nPFD = 0;
		int     pixelFormat;
		unsigned int numFormats;
		int		status;

		PIXELFORMATDESCRIPTOR temppfd = { 
			sizeof(PIXELFORMATDESCRIPTOR),   // size of this pfd 
			1,                     // version number 
			PFD_DRAW_TO_WINDOW |   // support window 
			PFD_GENERIC_ACCELERATED | // accelerated
			PFD_SUPPORT_OPENGL |   // support OpenGL 
			PFD_DOUBLEBUFFER,      // double buffered 
			PFD_TYPE_RGBA,         // RGBA type 
			(byte)Q_ftol(gl_colorbits->value),// desktop color depth 
			0, 0, 0, 0, 0, 0,      // color bits ignored 
			(byte)Q_ftol(gl_alphabits->value), // alpha buffer 
			0,                     // shift bit ignored 
			0,                     // no accumulation buffer 
			0, 0, 0, 0,            // accum bits ignored 
			(byte)Q_ftol(gl_depthbits->value), // z-buffer 
			(byte)Q_ftol(gl_stencilbits->value), // no stencil buffer 
			0,                     // no auxiliary buffer 
			PFD_MAIN_PLANE,        // main layer 
			0,                     // reserved 
			0, 0, 0                // layer masks ignored 
		};

		temphwnd =
			CreateWindowEx (
				0L,OPENGL_CLASS, "R1GL OpenGL PFD Detection Window",
				WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 1, 1,
				glw_state.hWnd, 0, glw_state.hInstance, NULL
			);

		if (!temphwnd)
			ri.Sys_Error (ERR_FATAL, "R1GL: Couldn't create OpenGL PFD Detection Window (%d).\r\n\r\nTry 'set vid_nowgl 1' in your baseq2/r1gl.cfg.", GetLastError());

		hDC = GetDC (temphwnd);

		hGLRC = NULL;

		// Set up OpenGL
		pixelFormat = ChoosePixelFormat(hDC, &temppfd);

		if (!pixelFormat)
		{
			ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº ChoosePixelFormat (%dc/%dd/%da/%ds) failed. Error %.8x.\n", (int)gl_colorbits->value, (int)gl_depthbits->value, (int)gl_alphabits->value, (int)gl_stencilbits->value, GetLastError());
			goto fail2;
		}

		if (SetPixelFormat(hDC, pixelFormat, &temppfd) == FALSE)
		{
			ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº SetPixelFormat (%d) failed. Error %.8x.\n", pixelFormat, GetLastError());
			goto fail2;
		}

		// Create a rendering context
		hGLRC = qwglCreateContext(hDC);
		if (!hGLRC)
		{
			ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº qwglCreateContext failed\n");
			goto fail2;
		}

		// Make the rendering context current
		if (!(qwglMakeCurrent(hDC, hGLRC)))
		{
			ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº qwglMakeCurrent failed\n");
			goto fail2;
		}

		{
			const char *s;
			s = qglGetString( GL_RENDERER );

			if (strcmp (s, "GDI Generic") == 0)
			{
				ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº no hardware accelerated pixelformats matching your current settings (try editing gl_colorbits/gl_alphabits/gl_depthbits/gl_stencilbits)\n");

				// make no rendering context current
				qwglMakeCurrent(NULL, NULL);

				// Destroy the rendering context...
				qwglDeleteContext(hGLRC);
				hGLRC = NULL;
				ReleaseDC (temphwnd, hDC);
				DestroyWindow (temphwnd);
				temphwnd = NULL;

				if (strcmp (gl_driver->string, "opengl32"))
				{
					ri.Con_Printf (PRINT_ALL, "Retrying with gl_driver opengl32\n");
					ri.Cvar_Set ("gl_driver", "opengl32");
					return VID_ERR_RETRY_QGL;
				}
				
				return VID_ERR_FAIL;
			}
			ri.Cvar_Get ("vid_renderer", s, CVAR_NOSET);
			ri.Con_Printf (PRINT_ALL, "Getting capabilities of '%s'\n", s);
		}

		wglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)qwglGetProcAddress("wglGetExtensionsStringARB");
		wglGetExtensionsStringEXT = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)qwglGetProcAddress("wglGetExtensionsStringEXT");

		if (!GLimp_ValidProc (wglGetExtensionsStringARB))
			wglGetExtensionsStringARB = NULL;
		if (!GLimp_ValidProc (wglGetExtensionsStringEXT))
			wglGetExtensionsStringEXT = NULL;
		if (!wglGetExtensionsStringARB && !wglGetExtensionsStringEXT)
		{
			ri.Con_Printf (PRINT_ALL, "GLimp_InitGL() - qwglGetProcAddress (wglGetExtensionsString) failed, falling back to regular PFD\n");
			goto fail2;
			
		}

		if (!(init_extensions ()))
		{
			ri.Con_Printf (PRINT_ALL, "GLimp_InitGL() - init_extensions () failed, falling back to regular PFD\n");
			goto fail2;
		}

		// make no rendering context current
		qwglMakeCurrent(NULL, NULL);

		// Destroy the rendering context...
		qwglDeleteContext(hGLRC);
		hGLRC = NULL;

		// Get the number of pixel format available
		iAttributes[0] = WGL_NUMBER_PIXEL_FORMATS_ARB;

		if (wglGetPixelFormatAttribivARB(hDC, 0, 0, 1, iAttributes, iResults) == GL_FALSE)
		{
			ri.Con_Printf (PRINT_ALL, "GLimp_InitGL() - wglGetPixelFormatAttribivARB failed, falling back to regular PFD\n");
			goto fail2;
		}

		//nPFD = iResults[0];

		// Choose a Pixel Format Descriptor (PFD) with multisampling support.
		iAttributes[0] = WGL_DOUBLE_BUFFER_ARB;
		iAttributes[1] = TRUE;

		iAttributes[2] = WGL_COLOR_BITS_ARB;
		iAttributes[3] = (int)gl_colorbits->value;

		iAttributes[4] = WGL_DEPTH_BITS_ARB;
		iAttributes[5] = (int)gl_depthbits->value;

		iAttributes[6] = WGL_ALPHA_BITS_ARB;
		iAttributes[7] = (int)gl_alphabits->value;

		iAttributes[8] = WGL_STENCIL_BITS_ARB;
		iAttributes[9] = (int)gl_stencilbits->value;

		iAttributes[10] = WGL_DRAW_TO_WINDOW_ARB;
		iAttributes[11] = TRUE;
		iAttributes[12] = WGL_SUPPORT_OPENGL_ARB;
		iAttributes[13] = TRUE;
		iAttributes[14] = WGL_PIXEL_TYPE_ARB;
		iAttributes[15] = WGL_TYPE_RGBA_ARB;
		iAttributes[16] = _is_multisample ? WGL_SAMPLE_BUFFERS_ARB : 0;
		iAttributes[17] = _is_multisample ? TRUE : 0;
		iAttributes[18] = _is_multisample ? WGL_SAMPLES_ARB : 0;
		iAttributes[19] = _is_multisample ? glw_samples : 0;
		iAttributes[20] = 0;
		iAttributes[21] = 0;

		// First attempt...
		status = wglChoosePixelFormatARB(hDC, iAttributes, fAttributes, 1, &pixelFormat, &numFormats);
		// Failure happens not only when the function fails, but also when no matching pixel format has been found
		if (status == FALSE || numFormats == 0)
		{
			ri.Con_Printf (PRINT_ALL, "GLimp_InitGL() - wglChoosePixelFormatARB failed, falling back to regular PFD\n");
			goto fail2;
		}
		else
		{
			// Fill the list of attributes we are interested in
			iAttributes[0 ] = WGL_PIXEL_TYPE_ARB;
			iAttributes[1 ] = WGL_COLOR_BITS_ARB;
			iAttributes[2 ] = WGL_RED_BITS_ARB;
			iAttributes[3 ] = WGL_GREEN_BITS_ARB;
			iAttributes[4 ] = WGL_BLUE_BITS_ARB;
			iAttributes[5 ] = WGL_ALPHA_BITS_ARB;
			iAttributes[6 ] = WGL_DEPTH_BITS_ARB;
			iAttributes[7 ] = WGL_STENCIL_BITS_ARB;

			// Since WGL_ARB_multisample and WGL_pbuffer are extensions, we must check if
			// those extensions are supported before passing the corresponding enums
			// to the driver. This could cause an error if they are not supported.
			iAttributes[8 ] = _is_multisample ? WGL_SAMPLE_BUFFERS_ARB : WGL_PIXEL_TYPE_ARB;
			iAttributes[9 ] = _is_multisample ? WGL_SAMPLES_ARB : WGL_PIXEL_TYPE_ARB;
			iAttributes[12] = WGL_PIXEL_TYPE_ARB;
			iAttributes[10] = WGL_DRAW_TO_WINDOW_ARB;
			iAttributes[11] = WGL_DRAW_TO_BITMAP_ARB;
			iAttributes[13] = WGL_DOUBLE_BUFFER_ARB;
			iAttributes[14] = WGL_STEREO_ARB;
			iAttributes[15] = WGL_ACCELERATION_ARB;
			iAttributes[16] = WGL_NEED_PALETTE_ARB;
			iAttributes[17] = WGL_NEED_SYSTEM_PALETTE_ARB;
			iAttributes[18] = WGL_SWAP_LAYER_BUFFERS_ARB;
			iAttributes[19] = WGL_SWAP_METHOD_ARB;
			iAttributes[20] = WGL_NUMBER_OVERLAYS_ARB;
			iAttributes[21] = WGL_NUMBER_UNDERLAYS_ARB;
			iAttributes[22] = WGL_TRANSPARENT_ARB;
			iAttributes[23] = WGL_SUPPORT_GDI_ARB;
			iAttributes[24] = WGL_SUPPORT_OPENGL_ARB;

			if (wglGetPixelFormatAttribivARB(hDC, pixelFormat, 0, 25, iAttributes, iResults) == GL_FALSE)
			{
				ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº wglGetPixelFormatAttribivARB failed\n");
				goto fail2;
			}

			ri.Con_Printf ( PRINT_ALL ,"R1GL PFD: %d matching formats, chose %d\n  color(%d-bits (red:%d, green:%d, blue:%d)), z(%d-bits), alpha(%d-bits), stencil(%d-bits)\n",
				numFormats, pixelFormat, iResults[1], iResults[2], iResults[3], iResults[4], iResults[6], iResults[5], iResults[7]);

			if (_is_multisample)
			{
				if (iResults[8])
					ri.Con_Printf ( PRINT_ALL ,"  using multisampling (FSAA), %d samples per pixel\n", iResults[9]);
				else
					ri.Con_Printf ( PRINT_ALL ,"  multisampling (FSAA) setup FAILED.\n");
			}

			if (iResults[15] != WGL_FULL_ACCELERATION_ARB)
			{
				ri.Con_Printf ( PRINT_ALL, "********** WARNING **********\npixelformat %d is NOT hardware accelerated!\n*****************************\n", pixelFormat);
			}

			ReleaseDC (temphwnd, hDC);
			DestroyWindow (temphwnd);
			temphwnd = NULL;

			ri.Con_Printf (PRINT_DEVELOPER, "unregistering opengl window class: ");
			UnregisterClass (OPENGL_CLASS, glw_state.hInstance);
			ri.Con_Printf (PRINT_DEVELOPER, "ok\n");

			//glw_state.hDC = GetDC (glw_state.hWnd);
			if ( ( glw_state.hDC = GetDC( glw_state.hWnd ) ) == NULL )
			{
				ri.Con_Printf( PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº GetDC failed\n" );
				return VID_ERR_FAIL;
			}

			if (!DescribePixelFormat (glw_state.hDC, pixelFormat, sizeof (temppfd), &temppfd) ||
				!SetPixelFormat (glw_state.hDC, pixelFormat, &temppfd))
			{
				ri.Con_Printf (PRINT_ALL, "Cannot apply selected OpenGL pixel format.\n");
				goto fail;
			}

			/*
			** startup the OpenGL subsystem by creating a context and making
			** it current
			*/
			if ( ( glw_state.hGLRC = qwglCreateContext( glw_state.hDC ) ) == 0 )
			{
				ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº qwglCreateContext failed (%d)\n", GetLastError());
				goto fail;
			}

			if ( !qwglMakeCurrent( glw_state.hDC, glw_state.hGLRC ) )
			{
				ri.Con_Printf (PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº qwglMakeCurrent failed\n");
				goto fail;
			}

			if (!GLimp_TryCoreContext ())
				goto fail;
			if (_is_multisample && iResults[8])
			{
				qglEnable (GL_MULTISAMPLE_ARB);
				if (_glExtensionSupported ("GL_NV_multisample_filter_hint"))
					qglHint (GL_MULTISAMPLE_FILTER_HINT_NV,
						!strcmp (gl_ext_nv_multisample_filter_hint->string, "nicest") ? GL_NICEST : GL_FASTEST);
			}
			gl_config.bitDepth = iResults[1];		
			gl_config.wglPFD = true;
		}
	/*} else {

		if ( !VerifyDriver() )
		{
			ri.Con_Printf( PRINT_ALL, "ÇÌéíðßÉîéôÇÌ¨© Åòòïòº no hardware acceleration detected\n" );
			goto fail;
		}

		ri.Con_Printf( PRINT_ALL, "GL PFD: pf(%d) color(%d-bits) Z(%d-bit)\n", pixelformat, ( int ) pfd.cColorBits, ( int ) pfd.cDepthBits );
		gl_config.bitDepth = pfd.cColorBits;*/
	}

	return VID_ERR_NONE;

fail2:
	// make no rendering context current
	qwglMakeCurrent (NULL, NULL);

	// Destroy the rendering context...
	if (hGLRC)
	{
		qwglDeleteContext( hGLRC);
		hGLRC = NULL;
	}

	ReleaseDC (temphwnd, hDC);
	DestroyWindow (temphwnd);

	ri.Con_Printf (PRINT_DEVELOPER, "unregistering opengl window class: ");
	UnregisterClass (OPENGL_CLASS, glw_state.hInstance);
	ri.Con_Printf (PRINT_DEVELOPER, "ok\n");
	return VID_ERR_FAIL;

fail:
	if ( glw_state.hGLRC )
	{
		qwglDeleteContext( glw_state.hGLRC );
		glw_state.hGLRC = NULL;
	}

	if ( glw_state.hDC )
	{
		ReleaseDC( glw_state.hWnd, glw_state.hDC );
		glw_state.hDC = NULL;
	}
	return VID_ERR_FAIL;
}

/*
** GLimp_BeginFrame
*/
#ifdef STEREO_SUPPORT
void GLimp_BeginFrame( float camera_separation )
#else
void GLimp_BeginFrame( void )
#endif
{
	GLimp_UpdateWindowGeometry ();

#if 0
	if ( gl_bitdepth->modified )
	{
		if ( FLOAT_NE_ZERO(gl_bitdepth->value) && !glw_state.allowdisplaydepthchange )
		{
			ri.Cvar_SetValue( "gl_bitdepth", 0 );
			ri.Con_Printf( PRINT_ALL, "gl_bitdepth requires Win95 OSR2.x or WinNT 4.x\n" );
		}
		gl_bitdepth->modified = false;
	}
#endif

#ifdef STEREO_SUPPORT
	if ( camera_separation < 0 && gl_state.stereo_enabled )
	{
		qglDrawBuffer( GL_BACK_LEFT );
	}
	else if (FLOAT_GT_ZERO (camera_separation) && gl_state.stereo_enabled )
	{
		qglDrawBuffer( GL_BACK_RIGHT );
	}
	else
#endif
	{
		qglDrawBuffer( GL_BACK );
	}
}

/*
** GLimp_EndFrame
** 
** Responsible for doing a swapbuffers and possibly for other stuff
** as yet to be determined.  Probably better not to make this a GLimp
** function and instead do a call to GLimp_SwapBuffers.
*/

void Draw_AddText (void);
void EXPORT GLimp_EndFrame (void)
{
	static int iDrawBuffer = 0;

	if (defer_drawing)
		Draw_AddText();

	if (gl_drawbuffer->modified)
	{
		gl_drawbuffer->modified = false;
		iDrawBuffer = stricmp( gl_drawbuffer->string, "GL_BACK" );
	}

	if (gl_defertext->modified)
	{
		gl_defertext->modified = false;
		defer_drawing = (int)gl_defertext->value;
	}

	if (iDrawBuffer == 0)
	{
		if (gl_config.wglPFD)
		{
			int swapped;
			R_PerfBeforeSwap ();
			swapped = qwglSwapBuffers (glw_state.hDC);
			if (!swapped)
			{
				int err = GetLastError();
				R_PerfAfterSwap (false);
				if (!IsIconic (glw_state.hWnd))
					ri.Sys_Error( ERR_FATAL, "GLimp_EndFrame() - SwapBuffers() failed: %d\n", err);
			}
			else
				R_PerfAfterSwap (true);
		}
		else
		{
			int swapped;
			R_PerfBeforeSwap ();
			swapped = SwapBuffers (glw_state.hDC);
			if (!swapped)
			{
				int err = GetLastError();
				R_PerfAfterSwap (false);
				if (!IsIconic (glw_state.hWnd))
					ri.Sys_Error( ERR_FATAL, "GLimp_EndFrame() - SwapBuffers() failed: %d\n", err);
			}
			else
				R_PerfAfterSwap (true);
		}
	}
}

void RestoreDesktopSettings (void)
{
	if (ChangeDisplaySettings( &originalDesktopMode, 0 ) != DISP_CHANGE_SUCCESSFUL )
	{
		ri.Sys_Error (ERR_FATAL, "Couldn't restore desktop display settings");
	}
}

void RestoreQ2Settings (void)
{
	if (ChangeDisplaySettings( &fullScreenMode, CDS_FULLSCREEN ) != DISP_CHANGE_SUCCESSFUL )
	{
		ri.Sys_Error (ERR_FATAL, "Couldn't restore Quake 2 display settings");
	}
}

/*
** GLimp_AppActivate
*/
int R_SetMode (void);
void EXPORT GLimp_AppActivate( qboolean active )
{
	if (!active)
		GLimp_GammaActivate (false);
	if (GLW_DisplayActivate (active))
	{
		if (active)
			GLimp_GammaActivate (true);
		return;
	}
	if ( active )
	{
		if (IsIconic (glw_state.hWnd))
			return;

		if (gl_state.fullscreen)
		{
			if (usingDesktopSettings)
			{
				RestoreQ2Settings ();
				usingDesktopSettings = false;
			}
			else
			{
				static qboolean warned = false;
				if (!warned)
				{
					ri.Con_Printf (PRINT_ALL, "\2NOTE: Set vid_flip_on_switch 1 if you would like R1GL to restore your desktop resolution when switching to another application.\n");
					warned = true;
				}
			}
		}

		SetForegroundWindow( glw_state.hWnd );
		ShowWindow( glw_state.hWnd, SW_RESTORE );
		GLimp_GammaActivate (true);
	}
	else
	{
		if (gl_state.fullscreen)
		{
			ShowWindow( glw_state.hWnd, SW_MINIMIZE );
			if (FLOAT_NE_ZERO (vid_restore_on_switch->value) && !usingDesktopSettings)
			{
				RestoreDesktopSettings();
				usingDesktopSettings = true;
			}
		}
	}
}
