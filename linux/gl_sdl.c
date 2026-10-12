/* $Id: gl_sdl.c,v 1.3 2007/05/12 20:09:36 r1ch Exp $
 *
 * all os-specific SDL refresher code
 *
 * Copyright (c) 2002 The QuakeForge Project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
** RW_SDL.C
**
** This file contains ALL Linux specific stuff having to do with the
** software refresh.  When a port is being made the following functions
** must be implemented by the port:
**
** GLimp_EndFrame
** GLimp_Init
** GLimp_InitGraphics
** GLimp_Shutdown
** GLimp_SwitchFullscreen
*/

#include "../race_version.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <string.h>
#include <sys/mman.h>

#include <SDL.h>

#if SDL_MAJOR_VERSION >= 2
#define SDLK_KP9 SDLK_KP_9
#define SDLK_KP3 SDLK_KP_3
#define SDLK_KP7 SDLK_KP_7
#define SDLK_KP1 SDLK_KP_1
#define SDLK_KP4 SDLK_KP_4
#define SDLK_KP6 SDLK_KP_6
#define SDLK_KP2 SDLK_KP_2
#define SDLK_KP8 SDLK_KP_8
#define SDLK_KP5 SDLK_KP_5
#define SDLK_KP0 SDLK_KP_0
#define SDLK_LMETA SDLK_LGUI
#define SDLK_RMETA SDLK_RGUI
#define SDLK_WORLD_7 0x1fffffff
#define SDL_JoystickName SDL_JoystickNameForIndex
#define SDL_JoystickOpened(index) 0
#define SDL_GetGammaRamp(r, g, b) SDL_GetWindowGammaRamp(sdl_window, r, g, b)
#define SDL_SetGammaRamp(r, g, b) SDL_SetWindowGammaRamp(sdl_window, r, g, b)
#define SDL_APPACTIVE SDL_WINDOW_SHOWN
#define SDL_APPINPUTFOCUS SDL_WINDOW_INPUT_FOCUS
#define SDL_GetAppState() SDL_GetWindowFlags(sdl_window)
#endif

#include <GL/gl.h>

#include "../client/client.h"
#include "../ref_gl/gl_local.h"
#include "../client/vid_config.h"
#if SDL_MAJOR_VERSION >= 2
#include "../client/ref_display.h"
#endif
#include "../linux/glw.h"

#include "../client/keys.h"
#include "../linux/rw_linux.h"

/*****************************************************************************/

#define JOY_AXIS_X			0
#define JOY_AXIS_Y			1
#define JOY_AXIS_Z			2
#define JOY_AXIS_R			3
#define JOY_AXIS_U			4
#define JOY_AXIS_V			5

static qboolean                 X11_active = false;

#if SDL_MAJOR_VERSION >= 2
static SDL_Window *sdl_window;
static SDL_GLContext sdl_context;
static int sdl_display_index;
static int sdl_refresh, sdl_depth;
static int sdl_window_x, sdl_window_y;
static qboolean sdl_window_positioned;
static qboolean sdl_recovering;
static qboolean sdl_trial_active, sdl_geometry_suppressed;
static int sdl_window_flags;
static jump_display_rect_t sdl_normal_window;
static int SDL2_DisplayIndex (void);
static int SDL2_ParseDisplayId (const char *id);
static qboolean SDL2_DisplayToken (int display, char *token, size_t capacity);
#else
static SDL_Surface *surface;
#endif
extern cvar_t *vid_borderless;

/* Keep the modern option and legacy fallback consistent with WGL. */
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

/* The renderer normalizes gl_debug before creating a context. */
static qboolean GLimp_DebugRequested (void)
{
	return ri.Cvar_Get ("gl_debug", "0", 0)->intvalue > 0;
}

static qboolean GLimp_SetSampleAttributes (int samples)
{
	int buffers_result, samples_result;

	/* SDL attributes survive failed creation attempts and video restarts. */
	buffers_result = SDL_GL_SetAttribute (SDL_GL_MULTISAMPLEBUFFERS, samples ? 1 : 0);
	samples_result = SDL_GL_SetAttribute (SDL_GL_MULTISAMPLESAMPLES, samples);
	if (buffers_result < 0 || samples_result < 0)
	{
		ri.Con_Printf (PRINT_ALL, "SDL MSAA attributes failed: %s\n", SDL_GetError ());
		return false;
	}
	return true;
}

static void GLimp_ReportSamples (int requested)
{
	int buffers = 0, samples = 0;

	if (SDL_GL_GetAttribute (SDL_GL_MULTISAMPLEBUFFERS, &buffers) < 0 ||
		SDL_GL_GetAttribute (SDL_GL_MULTISAMPLESAMPLES, &samples) < 0 ||
		buffers < 0 || samples < 0)
		ri.Con_Printf (PRINT_ALL, "SDL MSAA: requested %d samples; actual count unavailable.\n", requested);
	else
		ri.Con_Printf (PRINT_ALL, "SDL MSAA: requested %d, actual %d samples.\n", requested, buffers ? samples : 0);
}

#ifdef R1Q2_GL_CORE
static qboolean GLimp_CoreProfileRequested (void)
{
	cvar_t *profile = ri.Cvar_Get ("gl_profile", "compatibility", CVAR_ARCHIVE);
	return !Q_stricmp (profile->string, "core");
}
#endif

struct
{
	int key;
	int down;
} keyq[64];
int keyq_head=0;
int keyq_tail=0;

int config_notify=0;
int config_notify_width;
int config_notify_height;

glwstate_t glw_state;
						      
// Console variables that we need to access from this module

/*****************************************************************************/
/* MOUSE                                                                     */
/*****************************************************************************/

// this is inside the renderer shared lib, so these are called from vid_so

static qboolean        mouse_avail;
static int     mouse_buttonstate;
static int     mouse_oldbuttonstate;
static int   mouse_x, mouse_y;
static int	old_mouse_x, old_mouse_y;
static int		mx, my;
static float old_windowed_mouse;
static qboolean mouse_active;

static cvar_t	*_windowed_mouse;
static cvar_t	*m_filter;
static cvar_t	*in_mouse;

static qboolean	mlooking;

/* stencilbuffer shadows */
qboolean have_stencil = false;

// state struct passed in Init
static in_state_t	*in_state;

cvar_t *sensitivity;
static cvar_t *my_lookstrafe;
cvar_t *m_side;
cvar_t *m_yaw;
cvar_t *m_pitch;
cvar_t *m_forward;
static cvar_t *my_freelook;

#ifdef HAVE_JOYSTICK
/************************
 * Joystick
 ************************/
static cvar_t   *in_joystick;
static cvar_t   *j_invert_y;
static qboolean joystick_avail;
static SDL_Joystick *joy;
static int joy_oldbuttonstate;
static int joy_numbuttons;
static int jx, jy, jt;
static int lr_axis, ud_axis, throttle_axis;
#endif /* HAVE_JOYSTICK */

static void Force_CenterView_f (void)
{
	in_state->viewangles[PITCH] = 0;
}

static void RW_IN_MLookDown (void) 
{ 
	mlooking = true; 
}

static void RW_IN_MLookUp (void) 
{
	mlooking = false;
	in_state->IN_CenterView_fp ();
}

void EXPORT RW_IN_Init(in_state_t *in_state_p)
{
	in_state = in_state_p;

	// mouse variables
	_windowed_mouse = ri.Cvar_Get ("_windowed_mouse", "0", CVAR_ARCHIVE);
	m_filter = ri.Cvar_Get ("m_filter", "0", 0);
	in_mouse = ri.Cvar_Get ("in_mouse", "1", CVAR_ARCHIVE);
#ifdef HAVE_JOYSTICK
	in_joystick = ri.Cvar_Get("in_joystick", "0", CVAR_ARCHIVE);
	j_invert_y = ri.Cvar_Get("j_invert_y", "1", 0);
	lr_axis = (int) ri.Cvar_Get("j_lr_axis", "0", CVAR_ARCHIVE)->value;
	ud_axis = (int) ri.Cvar_Get("j_ud_axis", "1", CVAR_ARCHIVE)->value;
	throttle_axis = (int) ri.Cvar_Get("j_throttle", "3", CVAR_ARCHIVE)->value;
#endif
	my_freelook = ri.Cvar_Get( "freelook", "0", 0);
	my_lookstrafe = ri.Cvar_Get ("lookstrafe", "0", 0);
	
	sensitivity = ri.Cvar_Get ("sensitivity", "3", 0);
	m_pitch = ri.Cvar_Get ("m_pitch", "0.022", 0);
	m_yaw = ri.Cvar_Get ("m_yaw", "0.022", 0);
	m_forward = ri.Cvar_Get ("m_forward", "1", 0);
	m_side = ri.Cvar_Get ("m_side", "0.8", 0);

	ri.Cmd_AddCommand ("+mlook", RW_IN_MLookDown);
	ri.Cmd_AddCommand ("-mlook", RW_IN_MLookUp);

	ri.Cmd_AddCommand ("force_centerview", Force_CenterView_f);

	mouse_x = mouse_y = 0.0;
	mouse_avail = true;
}

void EXPORT RW_IN_Shutdown(void) {
    if (mouse_avail) {
	mouse_avail = false;

	ri.Cmd_RemoveCommand ("+mlook");
	ri.Cmd_RemoveCommand ("-mlook");

	ri.Cmd_RemoveCommand ("force_centerview");
    }

#ifdef HAVE_JOYSTICK
    if (joy) {
	SDL_JoystickClose(joy);
	joy = NULL;
    }
#endif
}

/*
===========
IN_Commands
===========
*/
void EXPORT RW_IN_Commands (void)
{
    int i;
#ifdef HAVE_JOYSTICK
    int key_index;
#endif
   
    if (mouse_avail) {
	for (i = 0; i < 3; i++) {
	    if ( (mouse_buttonstate & (1<<i)) && !(mouse_oldbuttonstate & (1<<i)) )
		in_state->Key_Event_fp (K_MOUSE1 + i, true);
	    
	    if ( !(mouse_buttonstate & (1<<i)) && (mouse_oldbuttonstate & (1<<i)) )
		in_state->Key_Event_fp (K_MOUSE1 + i, false);
	}
	/* can't put in loop because K_MOUSE4 doesn't come after K_MOUSE3 */
	if ((mouse_buttonstate & (1<<3)) && !(mouse_oldbuttonstate & (1<<3)))
	    in_state->Key_Event_fp(K_MOUSE4, true);
	if (!(mouse_buttonstate * (1<<3)) && (mouse_oldbuttonstate & (1<<3)))
	    in_state->Key_Event_fp(K_MOUSE4, false);
	
	if ((mouse_buttonstate & (1<<4)) && !(mouse_oldbuttonstate & (1<<4)))
	    in_state->Key_Event_fp(K_MOUSE5, true);
	if (!(mouse_buttonstate * (1<<4)) && (mouse_oldbuttonstate & (1<<4)))
	    in_state->Key_Event_fp(K_MOUSE5, false);
	
	mouse_oldbuttonstate = mouse_buttonstate;
    }
#ifdef HAVE_JOYSTICK
    if (joystick_avail && joy) {
	for (i = 0; i < joy_numbuttons; i++) {
	    if (SDL_JoystickGetButton(joy, i) && joy_oldbuttonstate != i) {
		key_index = (i < 4) ? K_JOY1 : K_AUX1;
		in_state->Key_Event_fp(key_index + i, true);
		joy_oldbuttonstate = i;
	    }
	    if (!SDL_JoystickGetButton(joy, i) && joy_oldbuttonstate != i) {
		key_index = (i < 4) ? K_JOY1 : K_AUX1;
		in_state->Key_Event_fp(key_index + i, false);
		joy_oldbuttonstate = i;
	    }
	}
    }
#endif
}

/*
===========
IN_Move
===========
*/
void EXPORT RW_IN_Move (usercmd_t *cmd)
{
  /*** FIXME 
   *   You can accelerate while in the air, this doesn't
   *   make physical sense.  Try falling off something and then moving
   *   forward.
   ***/
  
  if (mouse_avail) {
    if (m_filter->value)
      {
	mouse_x = (mx + old_mouse_x) * 0.5;
	mouse_y = (my + old_mouse_y) * 0.5;
      } else {
	mouse_x = mx;
	mouse_y = my;
      }
    
    old_mouse_x = mx;
    old_mouse_y = my;
    
    if (mouse_x || mouse_y) {    
      mouse_x *= sensitivity->value;
      mouse_y *= sensitivity->value;
      
      /* add mouse X/Y movement to cmd */
      if ( (*in_state->in_strafe_state & 1) || 
	   (my_lookstrafe->value && mlooking ))
	cmd->sidemove += m_side->value * mouse_x;
      else
	in_state->viewangles[YAW] -= m_yaw->value * mouse_x;
      
      
      if ( (mlooking || my_freelook->value) && 
	   !(*in_state->in_strafe_state & 1))
	{
	  in_state->viewangles[PITCH] += m_pitch->value * mouse_y;
	}
      else
	{
	  cmd->forwardmove -= m_forward->value * mouse_y;
	}
      mx = my = 0;
    }
  }
#ifdef HAVE_JOYSTICK
  if (joystick_avail && joy) {
      /* add joy X/Y movement to cmd */
    if ( (*in_state->in_strafe_state & 1) || 
	 (my_lookstrafe->value && mlooking ))
      cmd->sidemove += m_side->value * (jx/100);
    else
      in_state->viewangles[YAW] -= m_yaw->value * (jx/100);

    if ((mlooking || my_freelook->value) && !(*in_state->in_strafe_state & 1)) {
	if (j_invert_y)
	    in_state->viewangles[PITCH] -= m_pitch->value * (jy/100);
	else
	    in_state->viewangles[PITCH] += m_pitch->value * (jy/100);
	cmd->forwardmove -= m_forward->value * (jt/100);
    } else {
	cmd->forwardmove -= m_forward->value * (jy/100);
    }
    jt = jx = jy = 0;
  }
#endif
}

/* Preview the collected sample without consuming motion or filter history. */
void EXPORT RW_IN_PreviewMove (usercmd_t *cmd)
{
	int saved_mx = mx, saved_my = my;
	int saved_x = mouse_x, saved_y = mouse_y;
	int saved_old_x = old_mouse_x, saved_old_y = old_mouse_y;
#ifdef HAVE_JOYSTICK
	int saved_jx = jx, saved_jy = jy, saved_jt = jt;
#endif
	RW_IN_Move (cmd);
	mx = saved_mx;
	my = saved_my;
	mouse_x = saved_x;
	mouse_y = saved_y;
	old_mouse_x = saved_old_x;
	old_mouse_y = saved_old_y;
#ifdef HAVE_JOYSTICK
	jx = saved_jx;
	jy = saved_jy;
	jt = saved_jt;
#endif
}

void IN_DeactivateMouse( void ) 
{ 
	if (!mouse_avail)
		return;

	if (mouse_active) {
		/* uninstall_grabs(); */
#if SDL_MAJOR_VERSION >= 2
		if (sdl_window)
			SDL_SetWindowGrab (sdl_window, SDL_FALSE);
		SDL_SetRelativeMouseMode (SDL_FALSE);
#endif
		mouse_active = false;
	}
}

static void IN_ActivateMouse( void ) 
{
	if (!mouse_avail)
		return;

	if (!mouse_active) {
		mx = my = 0; // don't spazz
		/* install_grabs(); */
#if SDL_MAJOR_VERSION >= 2
		if (sdl_window && _windowed_mouse && _windowed_mouse->value)
		{
			SDL_SetWindowGrab (sdl_window, SDL_TRUE);
			SDL_SetRelativeMouseMode (SDL_TRUE);
		}
#endif
		mouse_active = true;
	}
}

void EXPORT RW_IN_Frame (void)
{
}

void EXPORT RW_IN_Activate(qboolean active)
{
    /*	if (active || vidmode_active) */
    	if (active)
		IN_ActivateMouse();
	else
		IN_DeactivateMouse();
}

/*****************************************************************************/

#if 0 /* SDL parachute should catch everything... */
// ========================================================================
// Tragic death handler
// ========================================================================

void TragicDeath(int signal_num)
{
	/* SDL_Quit(); */
	Sys_Error("This death brought to you by the number %d\n", signal_num);
}
#endif

int XLateKey(unsigned int keysym)
{
	int key;
	
	key = 0;
	switch(keysym) {
		case SDLK_KP9:			key = K_KP_PGUP; break;
		case SDLK_PAGEUP:		key = K_PGUP; break;
		
		case SDLK_KP3:			key = K_KP_PGDN; break;
		case SDLK_PAGEDOWN:		key = K_PGDN; break;
		
		case SDLK_KP7:			key = K_KP_HOME; break;
		case SDLK_HOME:			key = K_HOME; break;
		
		case SDLK_KP1:			key = K_KP_END; break;
		case SDLK_END:			key = K_END; break;
		
		case SDLK_KP4:			key = K_KP_LEFTARROW; break;
		case SDLK_LEFT:			key = K_LEFTARROW; break;
		
		case SDLK_KP6:			key = K_KP_RIGHTARROW; break;
		case SDLK_RIGHT:		key = K_RIGHTARROW; break;
		
		case SDLK_KP2:			key = K_KP_DOWNARROW; break;
		case SDLK_DOWN:			key = K_DOWNARROW; break;
		
		case SDLK_KP8:			key = K_KP_UPARROW; break;
		case SDLK_UP:			key = K_UPARROW; break;
		
		case SDLK_ESCAPE:		key = K_ESCAPE; break;
		
		case SDLK_KP_ENTER:		key = K_KP_ENTER; break;
		case SDLK_RETURN:		key = K_ENTER; break;
		
		case SDLK_TAB:			key = K_TAB; break;
		
		case SDLK_F1:			key = K_F1; break;
		case SDLK_F2:			key = K_F2; break;
		case SDLK_F3:			key = K_F3; break;
		case SDLK_F4:			key = K_F4; break;
		case SDLK_F5:			key = K_F5; break;
		case SDLK_F6:			key = K_F6; break;
		case SDLK_F7:			key = K_F7; break;
		case SDLK_F8:			key = K_F8; break;
		case SDLK_F9:			key = K_F9; break;
		case SDLK_F10:			key = K_F10; break;
		case SDLK_F11:			key = K_F11; break;
		case SDLK_F12:			key = K_F12; break;
		
		case SDLK_BACKSPACE:		key = K_BACKSPACE; break;
		
		case SDLK_KP_PERIOD:		key = K_KP_DEL; break;
		case SDLK_DELETE:		key = K_DEL; break;
		
		case SDLK_PAUSE:		key = K_PAUSE; break;
		
		case SDLK_LSHIFT:
		case SDLK_RSHIFT:		key = K_SHIFT; break;
		
		case SDLK_LCTRL:
		case SDLK_RCTRL:		key = K_CTRL; break;
		
		case SDLK_LMETA:
		case SDLK_RMETA:
		case SDLK_LALT:
		case SDLK_RALT:			key = K_ALT; break;

		case SDLK_KP5:			key = K_KP_5; break;
		
		case SDLK_INSERT:		key = K_INS; break;
		case SDLK_KP0:			key = K_KP_INS; break;
		
		case SDLK_KP_MULTIPLY:		key = '*'; break;
		case SDLK_KP_PLUS:		key = K_KP_PLUS; break;
		case SDLK_KP_MINUS:		key = K_KP_MINUS; break;
		case SDLK_KP_DIVIDE:		key = K_KP_SLASH; break;
		
		/* suggestions on how to handle this better would be appreciated */
		case SDLK_WORLD_7:		key = '`'; break;
		
		default: /* assuming that the other sdl keys are mapped to ascii */
			if (keysym < 128)
				key = keysym;
			break;
	}

	return key;		
}

#if SDL_MAJOR_VERSION >= 2
static unsigned char KeyStates[SDL_NUM_SCANCODES];
#else
static unsigned char KeyStates[SDLK_LAST];
#endif

#if SDL_MAJOR_VERSION >= 2
static void SDL2_WindowChanged (void)
{
	int width, height, x, y;
	char geometry[64];

	if (!sdl_window || !sdl_context)
		return;
	SDL_GL_GetDrawableSize (sdl_window, &width, &height);
	if (width >= 64 && width < 65536 && height >= 48 && height < 65536 &&
		(width != (int)vid.width || height != (int)vid.height))
		R_UpdateDrawableSize (width, height);
	if (sdl_recovering || sdl_trial_active || sdl_geometry_suppressed)
		return;
	if (SDL_GetWindowFlags (sdl_window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED))
		return;
	if (vid_geometry->modified || vid_mode->modified || vid_fullscreen->modified || vid_borderless->modified)
		return;
	SDL_GetWindowSize (sdl_window, &width, &height);
	SDL_GetWindowPosition (sdl_window, &x, &y);
	if (width < 320 || width > 8192 || height < 240 || height > 8192 ||
		x < -32767 || x > 32767 || y < -32767 || y > 32767)
		return;
	Com_sprintf (geometry, sizeof (geometry), "%dx%d%+d%+d", width, height, x, y);
	if (strcmp (geometry, vid_geometry->string))
	{
		ri.Cvar_Set ("vid_geometry", geometry);
		vid_geometry->modified = false; /* The existing window already has this geometry. */
	}
	sdl_normal_window.x = x;
	sdl_normal_window.y = y;
	sdl_normal_window.width = width;
	sdl_normal_window.height = height;
}

static void SDL2_RecoverDisplay (void)
{
	int count = SDL_GetNumVideoDisplays ();

	if (!sdl_window || !sdl_context || count < 1)
	{
		ri.Con_Printf (PRINT_ALL, "SDL display lost; no available monitor for recovery.\n");
		return;
	}
	sdl_recovering = true;
	if (SDL_SetWindowFullscreen (sdl_window, 0) < 0)
	{
		ri.Con_Printf (PRINT_ALL, "SDL display recovery failed: %s\n", SDL_GetError ());
		sdl_recovering = false;
		return;
	}
	SDL_SetWindowSize (sdl_window, 640, 480);
	SDL_SetWindowPosition (sdl_window, SDL_WINDOWPOS_CENTERED_DISPLAY (0),
		SDL_WINDOWPOS_CENTERED_DISPLAY (0));
	sdl_display_index = SDL_GetWindowDisplayIndex (sdl_window);
	SDL2_WindowChanged ();
	ri.Con_Printf (PRINT_ALL, "SDL display disconnected; restored a window on an available monitor.\n");
}

void GetEvent (SDL_Event *event)
{
	unsigned int key;
	int scancode;

	switch (event->type)
	{
	case SDL_MOUSEWHEEL:
		if (event->wheel.y)
		{
			key = event->wheel.y > 0 ? K_MWHEELUP : K_MWHEELDOWN;
			keyq[keyq_head].key = key;
			keyq[keyq_head].down = true;
			keyq_head = (keyq_head + 1) & 63;
			keyq[keyq_head].key = key;
			keyq[keyq_head].down = false;
			keyq_head = (keyq_head + 1) & 63;
		}
		break;
#ifdef HAVE_JOYSTICK
	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP:
		key = (event->jbutton.button < 4 ? K_JOY1 : K_AUX1) + event->jbutton.button;
		keyq[keyq_head].key = key;
		keyq[keyq_head].down = event->type == SDL_JOYBUTTONDOWN;
		keyq_head = (keyq_head + 1) & 63;
		break;
#endif
	case SDL_WINDOWEVENT:
		if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
			event->window.event == SDL_WINDOWEVENT_MOVED)
			SDL2_WindowChanged ();
		else if (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
			GLimp_GammaActivate (true);
		else if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
			GLimp_GammaActivate (false);
		break;
	case SDL_DISPLAYEVENT:
		if (event->display.event == SDL_DISPLAYEVENT_DISCONNECTED &&
			(event->display.display == sdl_display_index ||
				sdl_display_index >= SDL_GetNumVideoDisplays ()))
			SDL2_RecoverDisplay ();
		break;
	case SDL_KEYDOWN:
		scancode = event->key.keysym.scancode;
		if ((event->key.keysym.mod & KMOD_ALT) && event->key.keysym.sym == SDLK_RETURN)
		{
			qboolean fullscreen = (SDL_GetWindowFlags (sdl_window) & SDL_WINDOW_FULLSCREEN) != 0;
			Uint32 flags = fullscreen ? 0 :
										(vid_borderless->intvalue ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN);
			if (SDL_SetWindowFullscreen (sdl_window, flags) == 0)
			{
				ri.Cvar_SetValue ("vid_fullscreen", fullscreen ? 0 : 1);
				vid_fullscreen->modified = false;
				SDL2_WindowChanged ();
			}
			break;
		}
		if ((event->key.keysym.mod & KMOD_CTRL) && event->key.keysym.sym == SDLK_g)
		{
			ri.Cvar_SetValue ("_windowed_mouse", SDL_GetWindowGrab (sdl_window) ? 0 : 1);
			break;
		}
		if (scancode >= 0 && scancode < SDL_NUM_SCANCODES)
			KeyStates[scancode] = 1;
		key = XLateKey (event->key.keysym.sym);
		if (key)
		{
			keyq[keyq_head].key = key;
			keyq[keyq_head].down = true;
			keyq_head = (keyq_head + 1) & 63;
		}
		break;
	case SDL_KEYUP:
		scancode = event->key.keysym.scancode;
		if (scancode >= 0 && scancode < SDL_NUM_SCANCODES && KeyStates[scancode])
		{
			KeyStates[scancode] = 0;
			key = XLateKey (event->key.keysym.sym);
			if (key)
			{
				keyq[keyq_head].key = key;
				keyq[keyq_head].down = false;
				keyq_head = (keyq_head + 1) & 63;
			}
		}
		break;
	case SDL_QUIT:
		ri.Cmd_ExecuteText (EXEC_NOW, "quit");
		break;
	}
}
#else
void GetEvent(SDL_Event *event)
{
	unsigned int key;
	
	switch(event->type) {
	case SDL_MOUSEBUTTONDOWN:
		if (event->button.button == 4) {
			keyq[keyq_head].key = K_MWHEELUP;
			keyq[keyq_head].down = true;
			keyq_head = (keyq_head + 1) & 63;
			keyq[keyq_head].key = K_MWHEELUP;
			keyq[keyq_head].down = false;
			keyq_head = (keyq_head + 1) & 63;
		} else if (event->button.button == 5) {
			keyq[keyq_head].key = K_MWHEELDOWN;
			keyq[keyq_head].down = true;
			keyq_head = (keyq_head + 1) & 63;
			keyq[keyq_head].key = K_MWHEELDOWN;
			keyq[keyq_head].down = false;
			keyq_head = (keyq_head + 1) & 63;
		}
		break;
	case SDL_MOUSEBUTTONUP:
		break;
#ifdef HAVE_JOYSTICK
	case SDL_JOYBUTTONDOWN:
	  keyq[keyq_head].key = 
	    ((((SDL_JoyButtonEvent*)event)->button < 4)?K_JOY1:K_AUX1)+
	    ((SDL_JoyButtonEvent*)event)->button;
	  keyq[keyq_head].down = true;
	  keyq_head = (keyq_head+1)&63;
	  break;
	case SDL_JOYBUTTONUP:
	  keyq[keyq_head].key = 
	    ((((SDL_JoyButtonEvent*)event)->button < 4)?K_JOY1:K_AUX1)+
	    ((SDL_JoyButtonEvent*)event)->button;
	  keyq[keyq_head].down = false;
	  keyq_head = (keyq_head+1)&63;
	  break;
#endif
	case SDL_KEYDOWN:
		if ( (KeyStates[SDLK_LALT] || KeyStates[SDLK_RALT]) &&
			(event->key.keysym.sym == SDLK_RETURN) ) {
			cvar_t	*fullscreen;

			SDL_WM_ToggleFullScreen(surface);

			if (surface->flags & SDL_FULLSCREEN) {
				ri.Cvar_SetValue( "vid_fullscreen", 1 );
			} else {
				ri.Cvar_SetValue( "vid_fullscreen", 0 );
			}

			fullscreen = ri.Cvar_Get( "vid_fullscreen", "0", 0 );
			fullscreen->modified = false;	// we just changed it with SDL.

			break; /* ignore this key */
		}
		
		if ( (KeyStates[SDLK_LCTRL] || KeyStates[SDLK_RCTRL]) &&
			(event->key.keysym.sym == SDLK_g) ) {
			SDL_GrabMode gm = SDL_WM_GrabInput(SDL_GRAB_QUERY);
			/*	
			SDL_WM_GrabInput((gm == SDL_GRAB_ON) ? SDL_GRAB_OFF : SDL_GRAB_ON);
			gm = SDL_WM_GrabInput(SDL_GRAB_QUERY);
			*/	
			ri.Cvar_SetValue( "_windowed_mouse", (gm == SDL_GRAB_ON) ? /*1*/ 0 : /*0*/ 1 );
			
			break; /* ignore this key */
		}

		KeyStates[event->key.keysym.sym] = 1;
		
		key = XLateKey(event->key.keysym.sym);
		if (key) {
			keyq[keyq_head].key = key;
			keyq[keyq_head].down = true;
			keyq_head = (keyq_head + 1) & 63;
		}
		break;
	case SDL_KEYUP:
		if (KeyStates[event->key.keysym.sym]) {
			KeyStates[event->key.keysym.sym] = 0;
		
			key = XLateKey(event->key.keysym.sym);
			if (key) {
				keyq[keyq_head].key = key;
				keyq[keyq_head].down = false;
				keyq_head = (keyq_head + 1) & 63;
			}
		}
		break;
	case SDL_QUIT:
		ri.Cmd_ExecuteText(EXEC_NOW, "quit");
		break;
	}

}
#endif

void init_joystick() {
#ifdef HAVE_JOYSTICK
    int num_joysticks, i;
    joy = NULL;

    if (!(SDL_INIT_JOYSTICK&SDL_WasInit(SDL_INIT_JOYSTICK))) {
	ri.Con_Printf(PRINT_ALL, "SDL Joystick not initialized, trying to init...\n");
	SDL_Init(SDL_INIT_JOYSTICK);
    }
    if (in_joystick) {
	ri.Con_Printf(PRINT_ALL, "Trying to start-up joystick...\n");
	if ((num_joysticks=SDL_NumJoysticks())) {
	    for(i=0;i<num_joysticks;i++) {
		ri.Con_Printf(PRINT_ALL, "Trying joystick [%s]\n", 
			      SDL_JoystickName(i));
		if (!SDL_JoystickOpened(i)) {
		    joy = SDL_JoystickOpen(i);
		    if (joy) {
			ri.Con_Printf(PRINT_ALL, "Joytick activated.\n");
			joystick_avail = true;
			joy_numbuttons = SDL_JoystickNumButtons(joy);
			break;
		    }
		}
	    }
	    if (!joy) {
		ri.Con_Printf(PRINT_ALL, "Failed to open any joysticks\n");
		joystick_avail = false;
	    }
	}
	else {
	    ri.Con_Printf(PRINT_ALL, "No joysticks available\n");
	    joystick_avail = false;
	}
    }
    else {
	ri.Con_Printf(PRINT_ALL, "Joystick Inactive\n");
	joystick_avail = false;
    }
#endif
}

void InitJoystick() {
#ifdef HAVE_JOYSTICK
  int num_joysticks, i;
  joy = NULL;

  if (!(SDL_INIT_JOYSTICK&SDL_WasInit(SDL_INIT_JOYSTICK))) {
    ri.Con_Printf(PRINT_ALL, "SDL Joystick not initialized, trying to init...\n");
    SDL_Init(SDL_INIT_JOYSTICK);
  }
  if (in_joystick) {
    ri.Con_Printf(PRINT_ALL, "Trying to start-up joystick...\n");
    if ((num_joysticks=SDL_NumJoysticks())) {
      for(i=0;i<num_joysticks;i++) {
	ri.Con_Printf(PRINT_ALL, "Trying joystick [%s]\n", 
		      SDL_JoystickName(i));
	if (!SDL_JoystickOpened(i)) {
	  joy = SDL_JoystickOpen(0);
	  if (joy) {
	    ri.Con_Printf(PRINT_ALL, "Joytick activated.\n");
	    joystick_avail = true;
	    joy_numbuttons = SDL_JoystickNumButtons(joy);
	    break;
	  }
	}
      }
      if (!joy) {
	ri.Con_Printf(PRINT_ALL, "Failed to open any joysticks\n");
	joystick_avail = false;
      }
    }
    else {
      ri.Con_Printf(PRINT_ALL, "No joysticks available\n");
      joystick_avail = false;
    }
  }
  else {
    ri.Con_Printf(PRINT_ALL, "Joystick Inactive\n");
    joystick_avail = false;
  }
#endif
}

/*****************************************************************************/

/*
** SWimp_Init
**
** This routine is responsible for initializing the implementation
** specific stuff in a software rendering subsystem.
*/
int SWimp_Init( void *hInstance, void *wndProc )
{
#if SDL_MAJOR_VERSION >= 2
	if (!(SDL_WasInit (SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
	{
		if (SDL_InitSubSystem (SDL_INIT_VIDEO) < 0)
		{
			Sys_Error ("SDL video initialization failed: %s\n", SDL_GetError ());
			return false;
		}
	}
#else
	if (SDL_WasInit(SDL_INIT_AUDIO|SDL_INIT_CDROM|SDL_INIT_VIDEO) == 0) {
		if (SDL_Init(SDL_INIT_VIDEO) < 0) {
			Sys_Error("SDL Init failed: %s\n", SDL_GetError());
			return false;
		}
	} else if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
		if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
			Sys_Error("SDL Init failed: %s\n", SDL_GetError());
			return false;
		}
	}

	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
#endif

// catch signals so i can turn on auto-repeat
#if 0
 	{
		struct sigaction sa;
		sigaction(SIGINT, 0, &sa);
		sa.sa_handler = TragicDeath;
		sigaction(SIGINT, &sa, 0);
		sigaction(SIGTERM, &sa, 0);
	}
#endif
#ifdef HAVE_JOYSTICK
	init_joystick();
#endif
	return true;
}

void *GLimp_GetProcAddress(const char *func)
{
	return SDL_GL_GetProcAddress(func);
}

static void GLimp_InitModeList (void)
{
#if SDL_MAJOR_VERSION >= 2
	SDL_DisplayMode mode, desktop;
	vid_displaymode_t *modes;
	char *list, *out;
	int i, j, count, available, depth, display;

	if (vid_modelist)
		return;
	display = SDL2_DisplayIndex ();
	available = display >= 0 ? SDL_GetNumDisplayModes (display) : -1;
	if (available <= 0 || SDL_GetDesktopDisplayMode (display, &desktop) < 0)
	{
		vid_modelist = ri.Cvar_Get ("vid_modelist", "desktop 640x480 1280x720 1920x1080", 0);
		return;
	}
	modes = malloc (VID_MAX_DISPLAY_MODES * sizeof (*modes));
	if (!modes)
	{
		vid_modelist = ri.Cvar_Get ("vid_modelist", "desktop", 0);
		return;
	}
	count = 0;
	depth = SDL_BITSPERPIXEL (desktop.format);
	for (i = 0; i < available && count < VID_MAX_DISPLAY_MODES; i++)
	{
		if (SDL_GetDisplayMode (display, i, &mode) < 0 || mode.w < 320 || mode.w > 8192 ||
			mode.h < 240 || mode.h > 8192 || mode.refresh_rate < 0 || mode.refresh_rate > 1000 ||
			SDL_BITSPERPIXEL (mode.format) != depth)
			continue;
		for (j = 0; j < count; j++)
			if (modes[j].width == mode.w && modes[j].height == mode.h &&
				modes[j].frequency == mode.refresh_rate)
				break;
		if (j != count)
			continue;
		memset (&modes[count], 0, sizeof (modes[count]));
		modes[count].width = mode.w;
		modes[count].height = mode.h;
		modes[count++].frequency = mode.refresh_rate;
	}
	qsort (modes, count, sizeof (*modes), VID_CompareDisplayModes);
	list = malloc (8 + count * 32);
	if (!list)
		vid_modelist = ri.Cvar_Get ("vid_modelist", "desktop", 0);
	else
	{
		strcpy (list, "desktop");
		out = list + 7;
		for (i = 0; i < count; i++)
			out += sprintf (out, " %dx%d@%d:%d", modes[i].width, modes[i].height,
				modes[i].frequency, depth);
		vid_modelist = ri.Cvar_Get ("vid_modelist", list, 0);
		free (list);
	}
	free (modes);
#else
	SDL_Rect **available;
	vid_displaymode_t *modes;
	char *list, *out;
	int i, j, count;

	if (vid_modelist)
		return;
	available = SDL_ListModes (NULL, SDL_OPENGL | SDL_FULLSCREEN);
	if (!available || available == (SDL_Rect **)-1)
	{
		vid_modelist = ri.Cvar_Get ("vid_modelist", "desktop 640x480 1280x720 1920x1080", 0);
		return;
	}
	modes = malloc (VID_MAX_DISPLAY_MODES * sizeof (*modes));
	if (!modes)
	{
		vid_modelist = ri.Cvar_Get ("vid_modelist", "desktop", 0);
		return;
	}
	count = 0;
	for (i = 0; i < VID_MAX_DISPLAY_MODES && available[i]; i++)
	{
		if (available[i]->w < 320 || available[i]->w > 8192 ||
			available[i]->h < 240 || available[i]->h > 8192)
			continue;
		for (j = 0; j < count; j++)
			if (modes[j].width == available[i]->w && modes[j].height == available[i]->h)
				break;
		if (j != count)
			continue;
		memset (&modes[count], 0, sizeof (modes[count]));
		modes[count].width = available[i]->w;
		modes[count++].height = available[i]->h;
	}
	qsort (modes, count, sizeof (*modes), VID_CompareDisplayModes);
	list = malloc (8 + count * 32);
	if (!list)
		vid_modelist = ri.Cvar_Get ("vid_modelist", "desktop", 0);
	else
	{
		strcpy (list, "desktop");
		out = list + 7;
		for (i = 0; i < count; i++)
			out += sprintf (out, " %dx%d", modes[i].width, modes[i].height);
		vid_modelist = ri.Cvar_Get ("vid_modelist", list, 0);
		free (list);
	}
	free (modes);
#endif
}

int GLimp_Init( void *hInstance, void *wndProc )
{
	if (!SWimp_Init (hInstance, wndProc))
		return false;
	GLimp_InitModeList ();
	return true;
}

static void SetSDLIcon(void) {
#include "q2icon.xbm"
    SDL_Surface * icon;
    SDL_Color color;
    Uint8 * ptr;
    int i, mask;

	icon = SDL_CreateRGBSurface (0, q2icon_width, q2icon_height, 8,
		0, 0, 0, 0);
	if (icon == NULL)
	return; /* oh well... */
#if SDL_MAJOR_VERSION >= 2
	SDL_SetColorKey (icon, SDL_TRUE, 0);
#else
	SDL_SetColorKey(icon, SDL_SRCCOLORKEY, 0);
#endif

	color.r = 255;
    color.g = 255;
    color.b = 255;
#if SDL_MAJOR_VERSION >= 2
	color.a = 255;
	SDL_SetPaletteColors (icon->format->palette, &color, 0, 1);
#else
	SDL_SetColors(icon, &color, 0, 1); /* just in case */
#endif
	color.r = 0;
    color.g = 16;
    color.b = 0;
#if SDL_MAJOR_VERSION >= 2
	SDL_SetPaletteColors (icon->format->palette, &color, 1, 1);
#else
	SDL_SetColors(icon, &color, 1, 1);
#endif

	ptr = (Uint8 *)icon->pixels;
    for (i = 0; i < sizeof(q2icon_bits); i++) {
	for (mask = 1; mask != 0x100; mask <<= 1) {
	    *ptr = (q2icon_bits[i] & mask) ? 1 : 0;
	    ptr++;
	}               
    }

#if SDL_MAJOR_VERSION >= 2
	SDL_SetWindowIcon (sdl_window, icon);
#else
	SDL_WM_SetIcon(icon, NULL);
#endif
	SDL_FreeSurface(icon);
}

/*
** SWimp_InitGraphics
**
** This initializes the software refresh's implementation specific
** graphics subsystem.  In the case of Windows it creates DIB or
** DDRAW surfaces.
**
** The necessary width and height parameters are grabbed from
** vid.width and vid.height.
*/
#if SDL_MAJOR_VERSION >= 2
static int SDL2_DisplayIndex (void)
{
	const char *name = ri.Cvar_Get ("vid_display", "", CVAR_ARCHIVE)->string;
	int index;
	int count = SDL_GetNumVideoDisplays ();

	if (count < 1)
		return -1;
	if (!name[0])
	{
		index = sdl_window ? SDL_GetWindowDisplayIndex (sdl_window) : 0;
		return index >= 0 && index < count ? index : 0;
	}
	index = SDL2_ParseDisplayId (name);
	if (index < 0)
	{
		ri.Con_Printf (PRINT_ALL, "SDL display %s unavailable; using display 0.\n", name);
		return 0;
	}
	return index;
}

static qboolean SDL2_FullscreenMode (int display, int width, int height, int refresh, int depth,
	SDL_DisplayMode *chosen)
{
	SDL_DisplayMode mode, desktop;
	int i, count, best = -1, score, best_score = -1;

	count = SDL_GetNumDisplayModes (display);
	if (count <= 0 || SDL_GetDesktopDisplayMode (display, &desktop) < 0)
		return false;
	for (i = 0; i < count; i++)
	{
		if (SDL_GetDisplayMode (display, i, &mode) < 0 || mode.w != width || mode.h != height ||
			(refresh && mode.refresh_rate != refresh) ||
			(depth && SDL_BITSPERPIXEL (mode.format) != depth))
			continue;
		score = (mode.refresh_rate == desktop.refresh_rate ? 2 : 0) +
			(SDL_BITSPERPIXEL (mode.format) == SDL_BITSPERPIXEL (desktop.format) ? 1 : 0);
		if (best < 0 || score > best_score)
		{
			*chosen = mode;
			best = i;
			best_score = score;
		}
	}
	return best >= 0;
}

static qboolean SDL2_CreateGLWindow (int display)
{
#ifdef R1Q2_GL_CORE
	static const int versions[3][2] = { { 4, 3 }, { 3, 3 }, { 2, 1 } };
	qboolean requested = GLimp_CoreProfileRequested ();
	int actual_major, actual_minor, actual_profile;
#endif
	int first = 2, i;
	int samples = GLimp_RequestedSamples ();
	int attempt_samples;
	qboolean debug = GLimp_DebugRequested (), attempt_debug;

#ifdef R1Q2_GL_CORE
	first = requested ? 0 : 2;
#endif
	for (i = first; i < 3; i++)
	{
#ifdef R1Q2_GL_CORE
		if (SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, versions[i][0]) < 0 ||
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, versions[i][1]) < 0 ||
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, i < 2 ? SDL_GL_CONTEXT_PROFILE_CORE : 0) < 0)
		{
			ri.Con_Printf (PRINT_ALL, "SDL OpenGL %d.%d %s attributes failed: %s\n",
				versions[i][0], versions[i][1], i < 2 ? "core" : "compatibility", SDL_GetError ());
			continue;
		}
#endif
		attempt_samples = samples;
		attempt_debug = debug;
		for (;;)
		{
			if (SDL_GL_SetAttribute (SDL_GL_CONTEXT_FLAGS,
					attempt_debug ? SDL_GL_CONTEXT_DEBUG_FLAG : 0) < 0)
				ri.Con_Printf (PRINT_ALL, "SDL OpenGL context flags failed: %s\n", SDL_GetError ());
			else if (GLimp_SetSampleAttributes (attempt_samples))
			{
				sdl_window = SDL_CreateWindow ("R1Q2 Race - " RACE_VERSION RACE_MODIFIED,
					sdl_window_positioned ? sdl_window_x : SDL_WINDOWPOS_CENTERED_DISPLAY (display),
					sdl_window_positioned ? sdl_window_y : SDL_WINDOWPOS_CENTERED_DISPLAY (display),
					vid.width, vid.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
				if (sdl_window)
				{
					sdl_context = SDL_GL_CreateContext (sdl_window);
					if (sdl_context && SDL_GL_MakeCurrent (sdl_window, sdl_context) == 0)
					{
#ifdef R1Q2_GL_CORE
						if (i == 2)
						{
							if (requested)
								ri.Con_Printf (PRINT_ALL, "OpenGL core profile unavailable; using SDL compatibility context.\n");
							GLimp_ReportSamples (samples);
							goto success;
						}
						actual_major = actual_minor = actual_profile = 0;
						if (SDL_GL_GetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, &actual_major) == 0 &&
							SDL_GL_GetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, &actual_minor) == 0 &&
							SDL_GL_GetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, &actual_profile) == 0 &&
							(actual_profile & SDL_GL_CONTEXT_PROFILE_CORE) &&
							(actual_major > versions[i][0] ||
								(actual_major == versions[i][0] && actual_minor >= versions[i][1])))
						{
							ri.Con_Printf (PRINT_ALL, "Using SDL OpenGL %d.%d core profile.\n", actual_major, actual_minor);
							GLimp_ReportSamples (samples);
							goto success;
						}
#else
						GLimp_ReportSamples (samples);
						goto success;
#endif
					}
					ri.Con_Printf (PRINT_ALL, "SDL GL context creation/activation failed: %s\n", SDL_GetError ());
					if (sdl_context)
						SDL_GL_DeleteContext (sdl_context);
					sdl_context = NULL;
					SDL_DestroyWindow (sdl_window);
					sdl_window = NULL;
				}
				else
					ri.Con_Printf (PRINT_ALL, "SDL window creation failed: %s\n", SDL_GetError ());
			}
			if (attempt_debug)
			{
				ri.Con_Printf (PRINT_ALL, "Retrying SDL OpenGL without a debug context.\n");
				attempt_debug = false;
				continue;
			}
			if (!attempt_samples)
				break;
			ri.Con_Printf (PRINT_ALL, "Retrying SDL OpenGL without MSAA.\n");
			attempt_samples = 0;
			attempt_debug = debug;
		}
	}
	if (SDL_GL_SetAttribute (SDL_GL_CONTEXT_FLAGS, 0) < 0)
		ri.Con_Printf (PRINT_ALL, "Unable to clear SDL OpenGL context flags: %s\n", SDL_GetError ());
	return false;
success:
	/* Pending creation attributes must not leak into a later video restart. */
	if (SDL_GL_SetAttribute (SDL_GL_CONTEXT_FLAGS, 0) < 0)
		ri.Con_Printf (PRINT_ALL, "Unable to clear SDL OpenGL context flags: %s\n", SDL_GetError ());
	return true;
}

static qboolean GLimp_InitGraphics (qboolean fullscreen, qboolean borderless)
{
	SDL_DisplayMode mode;
	SDL_DisplayMode old_mode;
	vid_geometry_t saved_geometry;
	Uint32 old_flags, target;
	int old_x = 0, old_y = 0, old_w = 0, old_h = 0;
	int old_window_flags = sdl_window_flags;
	jump_display_rect_t old_normal_window = sdl_normal_window;
	int drawable_w, drawable_h, display;
	qboolean created = false;
	qboolean have_old_mode = false;
	float scale;

	display = SDL2_DisplayIndex ();
	if (display < 0)
		return false;
	if (fullscreen && !SDL2_FullscreenMode (display, vid.width, vid.height, sdl_refresh, sdl_depth, &mode))
	{
		ri.Con_Printf (PRINT_ALL, "SDL fullscreen mode %ux%u@%d:%d unavailable.\n",
			vid.width, vid.height, sdl_refresh, sdl_depth);
		return false;
	}
	if (sdl_window)
	{
		old_flags = SDL_GetWindowFlags (sdl_window);
		SDL_GetWindowPosition (sdl_window, &old_x, &old_y);
		SDL_GetWindowSize (sdl_window, &old_w, &old_h);
		have_old_mode = SDL_GetWindowDisplayMode (sdl_window, &old_mode) == 0;
		if (!(old_flags & SDL_WINDOW_FULLSCREEN))
		{
			sdl_normal_window.x = old_x;
			sdl_normal_window.y = old_y;
			sdl_normal_window.width = old_w;
			sdl_normal_window.height = old_h;
		}
	}
	else
	{
		old_flags = 0;
		SDL_GL_SetAttribute (SDL_GL_RED_SIZE, 5);
		SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 5);
		SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE, 5);
		SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 16);
		SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);
		if (!SDL2_CreateGLWindow (display))
			return false;
		SetSDLIcon ();
		if (VID_ParseGeometry (vid_geometry->string, &saved_geometry))
		{
			sdl_normal_window.x = saved_geometry.x;
			sdl_normal_window.y = saved_geometry.y;
			sdl_normal_window.width = saved_geometry.width;
			sdl_normal_window.height = saved_geometry.height;
		}
		else
		{
			SDL_GetWindowPosition (sdl_window, &sdl_normal_window.x, &sdl_normal_window.y);
			sdl_normal_window.width = 640;
			sdl_normal_window.height = 480;
		}
		created = true;
	}
	sdl_window_flags = (ri.Cvar_Get ("win_borderless", "0", CVAR_ARCHIVE)->intvalue ?
							   JUMP_DISPLAY_WINDOW_BORDERLESS :
							   0) |
		(ri.Cvar_Get ("win_notitle", "0", CVAR_ARCHIVE)->intvalue ? JUMP_DISPLAY_WINDOW_NOTITLE : 0) |
		(ri.Cvar_Get ("win_noresize", "0", CVAR_ARCHIVE)->intvalue ? JUMP_DISPLAY_WINDOW_NORESIZE : 0);
	if (SDL_SetWindowFullscreen (sdl_window, 0) < 0)
		goto failed;
	SDL_SetWindowSize (sdl_window, vid.width, vid.height);
	SDL_SetWindowPosition (sdl_window,
		sdl_window_positioned ? sdl_window_x : SDL_WINDOWPOS_CENTERED_DISPLAY (display),
		sdl_window_positioned ? sdl_window_y : SDL_WINDOWPOS_CENTERED_DISPLAY (display));
	SDL_SetWindowBordered (sdl_window, sdl_window_flags & (JUMP_DISPLAY_WINDOW_BORDERLESS | JUMP_DISPLAY_WINDOW_NOTITLE) ? SDL_FALSE : SDL_TRUE);
	SDL_SetWindowResizable (sdl_window, sdl_window_flags & JUMP_DISPLAY_WINDOW_NORESIZE ? SDL_FALSE : SDL_TRUE);
	if (fullscreen && SDL_SetWindowDisplayMode (sdl_window, &mode) < 0)
		goto failed;
	target = borderless ? SDL_WINDOW_FULLSCREEN_DESKTOP : fullscreen ? SDL_WINDOW_FULLSCREEN :
																	   0;
	if (SDL_SetWindowFullscreen (sdl_window, target) < 0)
		goto failed;
	SDL_GL_GetDrawableSize (sdl_window, &drawable_w, &drawable_h);
	if (drawable_w < 64 || drawable_w >= 65536 || drawable_h < 48 || drawable_h >= 65536)
		goto failed;
	vid.width = drawable_w;
	vid.height = drawable_h;
	sdl_display_index = SDL_GetWindowDisplayIndex (sdl_window);
	if (!fullscreen && !borderless)
	{
		SDL_GetWindowPosition (sdl_window, &sdl_normal_window.x, &sdl_normal_window.y);
		SDL_GetWindowSize (sdl_window, &sdl_normal_window.width, &sdl_normal_window.height);
	}
	scale = gl_hudscale->value;
	if (!(scale >= 1.0f))
		scale = 1.0f;
	ri.Vid_NewWindow (((int)ceilf (vid.width / scale) + 7) & ~7,
		((int)ceilf (vid.height / scale) + 1) & ~1);
	SDL_ShowCursor (SDL_DISABLE);
	X11_active = true;
	sdl_recovering = false;
	return true;

failed:
	ri.Con_Printf (PRINT_ALL, "SDL display change failed: %s\n", SDL_GetError ());
	if (created)
	{
		SDL_GL_DeleteContext (sdl_context);
		sdl_context = NULL;
		SDL_DestroyWindow (sdl_window);
		sdl_window = NULL;
	}
	else
	{
		SDL_SetWindowFullscreen (sdl_window, 0);
		if (have_old_mode)
			SDL_SetWindowDisplayMode (sdl_window, &old_mode);
		SDL_SetWindowSize (sdl_window, old_w, old_h);
		SDL_SetWindowPosition (sdl_window, old_x, old_y);
		SDL_SetWindowBordered (sdl_window, old_flags & SDL_WINDOW_BORDERLESS ? SDL_FALSE : SDL_TRUE);
		SDL_SetWindowResizable (sdl_window, old_flags & SDL_WINDOW_RESIZABLE ? SDL_TRUE : SDL_FALSE);
		SDL_SetWindowFullscreen (sdl_window,
			(old_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP ?
				SDL_WINDOW_FULLSCREEN_DESKTOP :
				old_flags & SDL_WINDOW_FULLSCREEN ? SDL_WINDOW_FULLSCREEN :
													0);
	}
	sdl_window_flags = old_window_flags;
	sdl_normal_window = old_normal_window;
	return false;
}
#else
static qboolean GLimp_InitGraphics (qboolean fullscreen, qboolean borderless)
{
	int flags;
	int width, height;
	int samples, attempt_samples;

	if (GLimp_DebugRequested ())
		ri.Con_Printf (PRINT_ALL, "OpenGL debug contexts are unsupported by SDL 1.2; using compatibility.\n");

#ifdef R1Q2_GL_CORE
	if (GLimp_CoreProfileRequested ())
		ri.Con_Printf (PRINT_ALL, "OpenGL core profile is unsupported by SDL 1.2; using compatibility.\n");
#endif

	/* Just toggle fullscreen if that's all that has been changed */
	if (surface && (surface->w == vid.width) && (surface->h == vid.height) &&
		!!(surface->flags & SDL_NOFRAME) == borderless)
	{
		int isfullscreen = (surface->flags & SDL_FULLSCREEN) ? 1 : 0;
		if (fullscreen != isfullscreen)
			SDL_WM_ToggleFullScreen(surface);

		isfullscreen = (surface->flags & SDL_FULLSCREEN) ? 1 : 0;
		if (fullscreen == isfullscreen)
			return true;
	}

	srandom(getpid());

	// free resources in use
	if (surface)
		SDL_FreeSurface(surface);
	surface = NULL;

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	
	flags = SDL_OPENGL;
	if (fullscreen)
		flags |= SDL_FULLSCREEN;
	if (borderless)
		flags |= SDL_NOFRAME;

	SetSDLIcon(); /* currently uses q2icon.xbm data */

	samples = GLimp_RequestedSamples ();
	attempt_samples = samples;
	for (;;)
	{
		if (GLimp_SetSampleAttributes (attempt_samples))
		{
			surface = SDL_SetVideoMode (vid.width, vid.height, 0, flags);
			if (surface)
			{
				GLimp_ReportSamples (samples);
				break;
			}
			ri.Con_Printf (PRINT_ALL, "(SDLGL) SDL SetVideoMode failed: %s\n", SDL_GetError ());
		}
		if (!attempt_samples)
			return false;
		ri.Con_Printf (PRINT_ALL, "Retrying SDL OpenGL without MSAA.\n");
		attempt_samples = 0;
	}

	SDL_WM_SetCaption("R1Q2 Race - " RACE_VERSION RACE_MODIFIED, "R1Q2 Race");

	width = (int)ceilf ((float)vid.width / gl_hudscale->value);
	height = (int)ceilf ((float)vid.height / gl_hudscale->value);
	width = (width + 7) & ~7;
	height = (height + 1) & ~1;
	ri.Vid_NewWindow (width, height);

	SDL_ShowCursor(0);

	X11_active = true;

	

	return true;
}
#endif

#if SDL_MAJOR_VERSION >= 2
static struct
{
	qboolean active;
	jump_display_settings_t previous;
	char message[160];
} sdl_trial;

static qboolean sdl_swap_probed;
static unsigned int sdl_swap_capabilities;

static qboolean EXPORT SDL2_DisplayAvailable (void)
{
	return sdl_window && sdl_context && SDL_GetNumVideoDisplays () > 0;
}

static int EXPORT SDL2_DisplayMonitors (jump_display_monitor_t *out, int capacity)
{
	SDL_DisplayMode mode;
	SDL_Rect bounds;
	const char *name;
	int i, count;

	if (!out || capacity <= 0)
		return 0;
	count = SDL_GetNumVideoDisplays ();
	if (count < 0)
		return 0;
	if (count > capacity)
		count = capacity;
	if (count > JUMP_DISPLAY_MAX_MONITORS)
		count = JUMP_DISPLAY_MAX_MONITORS;
	for (i = 0; i < count; i++)
	{
		if (SDL_GetDisplayBounds (i, &bounds) < 0 || SDL_GetDesktopDisplayMode (i, &mode) < 0)
			break;
		memset (&out[i], 0, sizeof (out[i]));
		if (!SDL2_DisplayToken (i, out[i].id, sizeof (out[i].id)))
			break;
		name = SDL_GetDisplayName (i);
		Com_sprintf (out[i].name, sizeof (out[i].name), "%s", name ? name : "Display");
		out[i].desktop.x = bounds.x;
		out[i].desktop.y = bounds.y;
		out[i].desktop.width = bounds.w;
		out[i].desktop.height = bounds.h;
		out[i].refresh = mode.refresh_rate;
		out[i].depth = SDL_BITSPERPIXEL (mode.format);
		out[i].primary = i == 0;
	}
	return i;
}

static int SDL2_ModeCompare (const void *left, const void *right)
{
	const jump_display_mode_t *a = left, *b = right;
	if (a->width != b->width)
		return a->width < b->width ? -1 : 1;
	if (a->height != b->height)
		return a->height < b->height ? -1 : 1;
	if (a->refresh != b->refresh)
		return a->refresh < b->refresh ? -1 : 1;
	return a->depth == b->depth ? 0 : a->depth < b->depth ? -1 :
															1;
}

static qboolean SDL2_DisplayToken (int display, char *token, size_t capacity)
{
	SDL_DisplayMode mode;
	SDL_Rect bounds;
	const char *name;
	char identity[256];
	const unsigned char *bytes;
	unsigned int hash = 2166136261u;

	if (SDL_GetDisplayBounds (display, &bounds) < 0 ||
		SDL_GetDesktopDisplayMode (display, &mode) < 0)
		return false;
	name = SDL_GetDisplayName (display);
	/* Bounds dimensions can change in exclusive fullscreen; the origin identifies
	 * otherwise identical panels without invalidating the active profile. */
	Com_sprintf (identity, sizeof (identity), "%s|%d,%d|%d,%d,%d,%u",
		name ? name : "", bounds.x, bounds.y,
		mode.w, mode.h, mode.refresh_rate, mode.format);
	for (bytes = (const unsigned char *)identity; *bytes; bytes++)
		hash = (hash ^ *bytes) * 16777619u;
	Com_sprintf (token, (int)capacity, "sdl-%08x", hash);
	return true;
}

static int SDL2_ParseDisplayId (const char *id)
{
	char *end;
	long number;
	char token[32];
	int i, found = -1;

	if (!id || !*id)
		return 0;
	if (!strncmp (id, "sdl-", 4))
	{
		for (i = 0; i < SDL_GetNumVideoDisplays (); i++)
			if (SDL2_DisplayToken (i, token, sizeof (token)) && !strcmp (id, token))
			{
				if (found >= 0)
					return -1; /* Ambiguous identity; never select a different panel. */
				found = i;
			}
		return found;
	}
	number = strtol (id, &end, 10);
	return end == id || *end || number < 0 || number >= SDL_GetNumVideoDisplays () ? -1 : (int)number;
}

static int EXPORT SDL2_DisplayModes (const char *id, jump_display_mode_t *out, int capacity)
{
	SDL_DisplayMode mode;
	jump_display_mode_t next;
	int display, available, i, j, count = 0;

	display = SDL2_ParseDisplayId (id);
	if (!out || capacity <= 0 || display < 0)
		return 0;
	if (capacity > JUMP_DISPLAY_MAX_MODES)
		capacity = JUMP_DISPLAY_MAX_MODES;
	available = SDL_GetNumDisplayModes (display);
	for (i = 0; i < available && count < capacity; i++)
	{
		if (SDL_GetDisplayMode (display, i, &mode) < 0 || mode.w < 320 || mode.w > 8192 ||
			mode.h < 240 || mode.h > 8192 || mode.refresh_rate < 2 || mode.refresh_rate > 1000)
			continue;
		next.width = mode.w;
		next.height = mode.h;
		next.refresh = mode.refresh_rate;
		next.depth = SDL_BITSPERPIXEL (mode.format);
		if (next.depth != 16 && next.depth != 24 && next.depth != 32)
			continue;
		for (j = 0; j < count; j++)
			if (!SDL2_ModeCompare (&out[j], &next))
				break;
		if (j == count)
			out[count++] = next;
	}
	qsort (out, count, sizeof (*out), SDL2_ModeCompare);
	return count;
}

static void SDL2_ProbeSwap (void)
{
	int interval;
	if (sdl_swap_probed || !sdl_context)
		return;
	sdl_swap_probed = true;
	interval = SDL_GL_GetSwapInterval ();
	if (SDL_GL_SetSwapInterval (interval) < 0)
		return;
	sdl_swap_capabilities = JUMP_DISPLAY_CAP_VSYNC;
	if (SDL_GL_SetSwapInterval (-1) == 0)
	{
		if (SDL_GL_SetSwapInterval (interval) == 0)
			sdl_swap_capabilities |= JUMP_DISPLAY_CAP_ADAPTIVE;
		else
			SDL_GL_SetSwapInterval (0);
	}
}

static qboolean EXPORT SDL2_DisplayRead (jump_display_settings_t *out, unsigned int *capabilities)
{
	SDL_DisplayMode mode;
	Uint32 flags;
	int display;

	if (!out || !capabilities || !SDL2_DisplayAvailable ())
		return false;
	display = SDL_GetWindowDisplayIndex (sdl_window);
	if (display < 0 || display >= SDL_GetNumVideoDisplays ())
		return false;
	memset (out, 0, sizeof (*out));
	if (!SDL2_DisplayToken (display, out->display, sizeof (out->display)))
		return false;
	flags = SDL_GetWindowFlags (sdl_window);
	out->mode = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP ?
		JUMP_DISPLAY_BORDERLESS :
		flags & SDL_WINDOW_FULLSCREEN ? JUMP_DISPLAY_EXCLUSIVE :
										JUMP_DISPLAY_WINDOWED;
	out->window = sdl_normal_window;
	if (out->mode == JUMP_DISPLAY_WINDOWED)
	{
		SDL_GetWindowPosition (sdl_window, &out->window.x, &out->window.y);
		SDL_GetWindowSize (sdl_window, &out->window.width, &out->window.height);
	}
	else if (out->mode == JUMP_DISPLAY_EXCLUSIVE)
	{
		if (SDL_GetCurrentDisplayMode (display, &mode) < 0)
			return false;
		out->width = mode.w;
		out->height = mode.h;
		out->refresh = mode.refresh_rate;
		out->depth = SDL_BITSPERPIXEL (mode.format);
	}
	out->window_flags = sdl_window_flags;
	SDL2_ProbeSwap ();
	out->vsync = SDL_GL_GetSwapInterval ();
	*capabilities = sdl_swap_capabilities;
	return true;
}

static qboolean SDL2_ApplySettings (const jump_display_settings_t *settings)
{
	SDL_DisplayMode mode, desktop;
	int display, width, height;
	Uint32 flags;

	if (!settings || !SDL2_DisplayAvailable () ||
		(display = SDL2_ParseDisplayId (settings->display)) < 0 ||
		settings->mode < JUMP_DISPLAY_WINDOWED || settings->mode > JUMP_DISPLAY_EXCLUSIVE ||
		(settings->window_flags & ~JUMP_DISPLAY_WINDOW_FLAGS) ||
		SDL_GetDesktopDisplayMode (display, &desktop) < 0)
		return false;
	if (settings->mode == JUMP_DISPLAY_WINDOWED)
	{
		if (settings->window.width < 320 || settings->window.width > 8192 ||
			settings->window.height < 240 || settings->window.height > 8192)
			return false;
		width = settings->window.width;
		height = settings->window.height;
	}
	else if (settings->mode == JUMP_DISPLAY_EXCLUSIVE)
	{
		width = settings->width ? settings->width : desktop.w;
		height = settings->height ? settings->height : desktop.h;
		if (!SDL2_FullscreenMode (display, width, height, settings->refresh, settings->depth, &mode))
			return false;
	}
	else
	{
		width = desktop.w;
		height = desktop.h;
	}
	if (!(SDL_GetWindowFlags (sdl_window) & SDL_WINDOW_FULLSCREEN))
	{
		SDL_GetWindowPosition (sdl_window, &sdl_normal_window.x, &sdl_normal_window.y);
		SDL_GetWindowSize (sdl_window, &sdl_normal_window.width, &sdl_normal_window.height);
	}
	sdl_geometry_suppressed = true;
	if (SDL_SetWindowFullscreen (sdl_window, 0) < 0)
		return false;
	SDL_SetWindowSize (sdl_window, width, height);
	SDL_SetWindowPosition (sdl_window,
		settings->mode == JUMP_DISPLAY_WINDOWED ? settings->window.x : SDL_WINDOWPOS_CENTERED_DISPLAY (display),
		settings->mode == JUMP_DISPLAY_WINDOWED ? settings->window.y : SDL_WINDOWPOS_CENTERED_DISPLAY (display));
	sdl_window_flags = settings->window_flags;
	SDL_SetWindowBordered (sdl_window, sdl_window_flags & (JUMP_DISPLAY_WINDOW_BORDERLESS | JUMP_DISPLAY_WINDOW_NOTITLE) ? SDL_FALSE : SDL_TRUE);
	SDL_SetWindowResizable (sdl_window, sdl_window_flags & JUMP_DISPLAY_WINDOW_NORESIZE ? SDL_FALSE : SDL_TRUE);
	if (settings->mode == JUMP_DISPLAY_EXCLUSIVE && SDL_SetWindowDisplayMode (sdl_window, &mode) < 0)
		return false;
	flags = settings->mode == JUMP_DISPLAY_BORDERLESS ? SDL_WINDOW_FULLSCREEN_DESKTOP :
		settings->mode == JUMP_DISPLAY_EXCLUSIVE	  ? SDL_WINDOW_FULLSCREEN :
														0;
	if (SDL_SetWindowFullscreen (sdl_window, flags) < 0 ||
		(settings->vsync != SDL_GL_GetSwapInterval () && SDL_GL_SetSwapInterval (settings->vsync) < 0) ||
		SDL_GL_GetCurrentContext () != sdl_context)
		return false;
	if (settings->mode == JUMP_DISPLAY_WINDOWED)
		sdl_normal_window = settings->window;
	sdl_display_index = SDL_GetWindowDisplayIndex (sdl_window);
	SDL2_WindowChanged ();
	sdl_recovering = false;
	return true;
}

static qboolean EXPORT SDL2_DisplayBegin (void)
{
	unsigned int capabilities;
	if (sdl_trial.active || !SDL2_DisplayRead (&sdl_trial.previous, &capabilities))
		return false;
	sdl_trial.active = sdl_trial_active = true;
	sdl_trial.message[0] = 0;
	return true;
}

static qboolean EXPORT SDL2_DisplayApply (const jump_display_settings_t *request, jump_display_settings_t *actual)
{
	unsigned int capabilities;
	if (!sdl_trial.active || !request || !actual || !SDL2_ApplySettings (request) ||
		!SDL2_DisplayRead (actual, &capabilities))
	{
		Com_sprintf (sdl_trial.message, sizeof (sdl_trial.message), "SDL display change failed: %s", SDL_GetError ());
		return false;
	}
	return true;
}

static qboolean EXPORT SDL2_DisplayEnd (qboolean keep)
{
	jump_display_settings_t fallback;
	SDL_Rect bounds;
	int restored;
	if (!sdl_trial.active)
		return false;
	if (keep)
	{
		sdl_trial.active = sdl_trial_active = false;
		sdl_geometry_suppressed = true;
		Com_sprintf (sdl_trial.message, sizeof (sdl_trial.message), "Display settings kept.");
		return true;
	}
	restored = SDL2_ApplySettings (&sdl_trial.previous);
	if (!restored)
	{
		memset (&fallback, 0, sizeof (fallback));
		fallback.mode = JUMP_DISPLAY_WINDOWED;
		Com_sprintf (fallback.display, sizeof (fallback.display), "0");
		if (SDL_GetDisplayBounds (0, &bounds) == 0)
		{
			fallback.window.x = bounds.x;
			fallback.window.y = bounds.y;
		}
		fallback.window.width = 640;
		fallback.window.height = 480;
		fallback.vsync = SDL_GL_GetSwapInterval ();
		restored = SDL2_ApplySettings (&fallback);
		Com_sprintf (sdl_trial.message, sizeof (sdl_trial.message), restored ? "Previous display unavailable. Restored a window on an available monitor." : "SDL display recovery failed: %s", SDL_GetError ());
	}
	else
		Com_sprintf (sdl_trial.message, sizeof (sdl_trial.message), "Previous display settings restored.");
	sdl_trial.active = sdl_trial_active = false;
	sdl_geometry_suppressed = true;
	return restored;
}

static const char *EXPORT SDL2_DisplayMessage (void)
{
	return sdl_trial.message;
}

const refdisplay_t *SDL2_GetDisplayAPI (void)
{
	static const refdisplay_t api = {
		REF_DISPLAY_VERSION, sizeof (refdisplay_t), SDL2_DisplayAvailable,
		SDL2_DisplayMonitors, SDL2_DisplayModes, SDL2_DisplayRead,
		SDL2_DisplayBegin, SDL2_DisplayApply, SDL2_DisplayEnd, SDL2_DisplayMessage
	};
	return &api;
}
#endif

void GLimp_BeginFrame(void)
{
}

/*
** GLimp_EndFrame
**
** This does an implementation specific copy from the backbuffer to the
** front buffer.  In the Win32 case it uses BitBlt or BltFast depending
** on whether we're using DIB sections/GDI or DDRAW.
*/

void GLimp_EndFrame (void)
{
	R_PerfBeforeSwap ();
#if SDL_MAJOR_VERSION >= 2
	SDL_GL_SwapWindow (sdl_window);
#else
	SDL_GL_SwapBuffers();
#endif
	R_PerfAfterSwap (true);
}

#if SDL_MAJOR_VERSION >= 2
void GLimp_SwapInterval (int interval)
{
	if (!sdl_context)
		return;
	if (SDL_GL_SetSwapInterval (interval) < 0)
	{
		if (interval < 0 && SDL_GL_SetSwapInterval (-interval) == 0)
			ri.Con_Printf (PRINT_ALL, "SDL adaptive VSync unavailable; using interval %d.\n", -interval);
		else
			ri.Con_Printf (PRINT_ALL, "SDL swap interval %d unavailable: %s\n", interval, SDL_GetError ());
	}
}
#endif

/*
** GLimp_SetMode
*/
int GLimp_SetMode( unsigned int *pwidth, unsigned int *pheight, int mode, qboolean fullscreen )
{
#if SDL_MAJOR_VERSION >= 2
	SDL_DisplayMode desktop;
#else
	const SDL_VideoInfo *info;
#endif
	qboolean borderless, explicit_size;
	vid_geometry_t geometry;
	vid_displaymode_t selected;
	unsigned int width, height;

	ri.Con_Printf (PRINT_ALL, "setting mode %d:", mode );

	borderless = fullscreen && FLOAT_NE_ZERO (vid_borderless->value);
#if SDL_MAJOR_VERSION >= 2
	sdl_refresh = sdl_depth = 0;
	sdl_window_positioned = false;
#endif
	if (borderless)
		mode = -2;
	explicit_size = false;
	if (!borderless && !fullscreen && vid_geometry->string[0])
	{
		if (!VID_ParseGeometry (vid_geometry->string, &geometry))
			return VID_ERR_INVALID_MODE;
#if SDL_MAJOR_VERSION >= 2
		sdl_window_x = geometry.x;
		sdl_window_y = geometry.y;
		sdl_window_positioned = strpbrk (strchr (vid_geometry->string, 'x') + 1, "+-") != NULL;
#else
		if (geometry.x || geometry.y)
		{
			ri.Con_Printf (PRINT_ALL, " SDL geometry positioning is not supported\n");
			return VID_ERR_INVALID_MODE;
		}
#endif
		width = geometry.width;
		height = geometry.height;
		explicit_size = true;
	}
	else if (!borderless && FLOAT_NE_ZERO (vid_mode->value))
	{
		if (!(vid_mode->value >= 1 && vid_mode->value <= VID_MAX_DISPLAY_MODES + 1) ||
			vid_mode->value != vid_mode->intvalue ||
			!VID_GetModeFromList (vid_modelist->string, vid_mode->intvalue, &selected))
			return VID_ERR_INVALID_MODE;
#if SDL_MAJOR_VERSION >= 2
		if (fullscreen)
		{
			sdl_refresh = selected.frequency;
			sdl_depth = selected.depth;
		}
#else
		if (fullscreen && selected.frequency)
		{
			ri.Con_Printf (PRINT_ALL, " SDL explicit refresh selection is not supported\n");
			return VID_ERR_INVALID_MODE;
		}
#endif
#if SDL_MAJOR_VERSION < 2
		if (selected.depth)
		{
			ri.Con_Printf (PRINT_ALL, " SDL explicit color depth selection is not supported\n");
			return VID_ERR_INVALID_MODE;
		}
#endif
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
	else if (mode == -2)
	{
#if SDL_MAJOR_VERSION >= 2
		int display = SDL2_DisplayIndex ();
		if (display < 0 || SDL_GetDesktopDisplayMode (display, &desktop) < 0 ||
			desktop.w < 64 || desktop.h < 48)
			return VID_ERR_INVALID_MODE;
		width = desktop.w;
		height = desktop.h;
#else
		info = SDL_GetVideoInfo ();
#if SDL_VERSION_ATLEAST(1, 2, 10)
		if (!info || info->current_w < 64 || info->current_h < 48)
			return VID_ERR_INVALID_MODE;
		width = info->current_w;
		height = info->current_h;
#else
		ri.Con_Printf (PRINT_ALL, " desktop mode requires SDL 1.2.10 or later\n");
		return VID_ERR_INVALID_MODE;
#endif
#endif
	}
	else if (mode == -1)
	{
		width = 0;
		height = 0;
	}
	else if (!ri.Vid_GetModeInfo (&width, &height, mode))
	{
		ri.Con_Printf( PRINT_ALL, " invalid mode\n" );
		return VID_ERR_INVALID_MODE;
	}

	if (!explicit_size && mode != -2)
	{
		if (FLOAT_NE_ZERO (gl_forcewidth->value))
		{
			if (!(gl_forcewidth->value >= 64 && gl_forcewidth->value < 0x10000))
				return VID_ERR_INVALID_MODE;
			width = (unsigned int)gl_forcewidth->value;
		}
		if (FLOAT_NE_ZERO (gl_forceheight->value))
		{
			if (!(gl_forceheight->value >= 48 && gl_forceheight->value < 0x10000))
				return VID_ERR_INVALID_MODE;
			height = (unsigned int)gl_forceheight->value;
		}
	}
	if (width < 64 || width >= 0x10000 || height < 48 || height >= 0x10000)
		return VID_ERR_INVALID_MODE;
	*pwidth = width;
	*pheight = height;

	ri.Con_Printf( PRINT_ALL, " %d %d\n", *pwidth, *pheight);

	if (!GLimp_InitGraphics (fullscreen && !borderless, borderless))
	{
		// failed to set a valid mode in windowed mode
		return fullscreen && !borderless ? VID_ERR_FULLSCREEN_FAILED : VID_ERR_INVALID_MODE;
	}

	return VID_ERR_NONE;
}


/*
** SWimp_Shutdown
**
** System specific graphics subsystem shutdown routine.  Destroys
** DIBs or DDRAW surfaces as appropriate.
*/

void SWimp_Shutdown( void )
{
#if SDL_MAJOR_VERSION >= 2
	if (sdl_context)
		SDL_GL_DeleteContext (sdl_context);
	sdl_context = NULL;
	if (sdl_window)
		SDL_DestroyWindow (sdl_window);
	sdl_window = NULL;
	SDL_QuitSubSystem (SDL_INIT_VIDEO);
#else
	if (surface)
		SDL_FreeSurface(surface);
	surface = NULL;
	
	if (SDL_WasInit(SDL_INIT_EVERYTHING) == SDL_INIT_VIDEO)
		SDL_Quit();
	else
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
#endif

	X11_active = false;
}

/* Hardware gamma uses the retained SDL 1.2 API. */
static Uint16 sdl_gamma_original[3][256], sdl_gamma_custom[3][256];
static byte sdl_gamma_table[256];
static qboolean sdl_gamma_saved, sdl_gamma_failed, sdl_gamma_active;

void GLimp_ShutdownGamma (void)
{
	if (sdl_gamma_saved && SDL_SetGammaRamp (sdl_gamma_original[0], sdl_gamma_original[1], sdl_gamma_original[2]) < 0)
		ri.Con_Printf (PRINT_ALL, "Could not restore the saved SDL gamma ramp.\n");
	sdl_gamma_saved = sdl_gamma_failed = false;
}

void GLimp_GammaActivate (qboolean active)
{
	if (active == sdl_gamma_active)
		return;
	sdl_gamma_active = active;
	if (sdl_gamma_saved && SDL_SetGammaRamp (active ? sdl_gamma_custom[0] : sdl_gamma_original[0], active ? sdl_gamma_custom[1] : sdl_gamma_original[1], active ? sdl_gamma_custom[2] : sdl_gamma_original[2]) < 0)
		sdl_gamma_failed = true;
}

qboolean GLimp_UpdateGamma (const byte *table)
{
	int channel, i;
	qboolean changed;
	if (!table || !sdl_gamma_saved || sdl_gamma_failed)
		return false;
	changed = memcmp (sdl_gamma_table, table, 256) != 0;
	if (changed)
	{
		memcpy (sdl_gamma_table, table, 256);
		for (channel = 0; channel < 3; channel++)
			for (i = 0; i < 256; i++)
				sdl_gamma_custom[channel][i] = (Uint16)(table[i] << 8);
		if (sdl_gamma_active && SDL_SetGammaRamp (sdl_gamma_custom[0], sdl_gamma_custom[1], sdl_gamma_custom[2]) < 0)
		{
			SDL_SetGammaRamp (sdl_gamma_original[0], sdl_gamma_original[1], sdl_gamma_original[2]);
			sdl_gamma_failed = true;
			return false;
		}
	}
	GLimp_GammaActivate ((SDL_GetAppState () & (SDL_APPACTIVE | SDL_APPINPUTFOCUS)) == (SDL_APPACTIVE | SDL_APPINPUTFOCUS));
	return !sdl_gamma_failed;
}

qboolean GLimp_InitGamma (const byte *table)
{
	int channel, i;
	GLimp_ShutdownGamma ();
#if SDL_MAJOR_VERSION >= 2
	if (!sdl_window || !table || SDL_GetGammaRamp (sdl_gamma_original[0], sdl_gamma_original[1], sdl_gamma_original[2]) < 0)
		return false;
#else
	if (!surface || !table || SDL_GetGammaRamp (sdl_gamma_original[0], sdl_gamma_original[1], sdl_gamma_original[2]) < 0)
		return false;
#endif
	sdl_gamma_saved = true;
	memcpy (sdl_gamma_table, table, 256);
	for (channel = 0; channel < 3; channel++)
		for (i = 0; i < 256; i++)
			sdl_gamma_custom[channel][i] = (Uint16)(table[i] << 8);
	sdl_gamma_active = (SDL_GetAppState () & (SDL_APPACTIVE | SDL_APPINPUTFOCUS)) == (SDL_APPACTIVE | SDL_APPINPUTFOCUS);
	if (SDL_SetGammaRamp (sdl_gamma_active ? sdl_gamma_custom[0] : sdl_gamma_original[0],
			sdl_gamma_active ? sdl_gamma_custom[1] : sdl_gamma_original[1], sdl_gamma_active ? sdl_gamma_custom[2] : sdl_gamma_original[2]) == 0)
		return true;
	GLimp_ShutdownGamma ();
	return false;
}

void GLimp_Shutdown( void )
{
	GLimp_ShutdownGamma ();
	SWimp_Shutdown();
}

/*
** GLimp_AppActivate
*/
void GLimp_AppActivate( qboolean active )
{
	GLimp_GammaActivate (active);
}

//===============================================================================

/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{

	int r;
	unsigned long addr;
	int psize = getpagesize();

	addr = (startaddr & ~(psize-1)) - psize;

//	fprintf(stderr, "writable code %lx(%lx)-%lx, length=%lx\n", startaddr,
//			addr, startaddr+length, length);

	r = mprotect((char*)addr, length + startaddr - addr + psize, 7);

	if (r < 0)
    		Sys_Error("Protection change failed\n");

}

/*****************************************************************************/
/* KEYBOARD                                                                  */
/*****************************************************************************/

Key_Event_fp_t Key_Event_fp;

void EXPORT KBD_Init(Key_Event_fp_t fp)
{
	Key_Event_fp = fp;
}

void EXPORT KBD_Update(void)
{
	SDL_Event event;
	static int KBD_Update_Flag;
	
	if (KBD_Update_Flag == 1)
		return;
	
	KBD_Update_Flag = 1;
	
// get events from x server
	if (X11_active)
	{
		int bstate;
		
		while (SDL_PollEvent(&event))
			GetEvent(&event);
#if SDL_MAJOR_VERSION >= 2
		sdl_geometry_suppressed = false;
#endif

		if (!mx && !my)
			SDL_GetRelativeMouseState (&mx, &my);
	
#ifdef HAVE_JOYSTICK
	if (joystick_avail && joy) {
	  jx = SDL_JoystickGetAxis(joy, lr_axis);
	  jy = SDL_JoystickGetAxis(joy, ud_axis);
	  jt = SDL_JoystickGetAxis(joy, throttle_axis);
	}
#endif	
	mouse_buttonstate = 0;
	bstate = SDL_GetMouseState(NULL, NULL);
	if (SDL_BUTTON(1) & bstate)
		mouse_buttonstate |= (1 << 0);
	if (SDL_BUTTON(3) & bstate) /* quake2 has the right button be mouse2 */
		mouse_buttonstate |= (1 << 1);
	if (SDL_BUTTON(2) & bstate) /* quake2 has the middle button be mouse3 */
		mouse_buttonstate |= (1 << 2);
	if (SDL_BUTTON(6) & bstate)
		mouse_buttonstate |= (1 << 3);

	if (SDL_BUTTON(7) & bstate)
		mouse_buttonstate |= (1 << 4);

	if (old_windowed_mouse != _windowed_mouse->value)
	{
		old_windowed_mouse = _windowed_mouse->value;

#if SDL_MAJOR_VERSION >= 2
		SDL_SetWindowGrab (sdl_window, mouse_active && _windowed_mouse->value ? SDL_TRUE : SDL_FALSE);
		SDL_SetRelativeMouseMode (mouse_active && _windowed_mouse->value ? SDL_TRUE : SDL_FALSE);
#else
		if (!_windowed_mouse->value)
		{
			/* ungrab the pointer */
			SDL_WM_GrabInput (SDL_GRAB_OFF);
		}
		else
		{
			/* grab the pointer */
			SDL_WM_GrabInput (SDL_GRAB_ON);
		}
#endif
	}
	while (keyq_head != keyq_tail)
	{
		Key_Event_fp (keyq[keyq_tail].key, keyq[keyq_tail].down);
		keyq_tail = (keyq_tail + 1) & 63;
	}
	}
	
	KBD_Update_Flag = 0;
}

void EXPORT KBD_Close(void)
{
	keyq_head = 0;
	keyq_tail = 0;
	
	memset(keyq, 0, sizeof(keyq));
}
