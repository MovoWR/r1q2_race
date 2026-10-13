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
#include "../client/client.h"
#include "../client/vid_mode_menu.h"
#include "../client/qmenu.h"

enum
{
	REF_SOFT,
	REF_OPENGL,
	REF_3DFX,
	REF_POWERVR,
	REF_R1GL,
	REF_NCGL
};

extern cvar_t *vid_ref;
extern cvar_t *vid_gamma;
#ifdef REF_HARD_LINKED
qboolean GL_ImageColorProfileReady (void);
qboolean GL_HardwareGammaActive (void);
#endif
extern cvar_t *scr_viewsize;

static cvar_t *gl_mode;
static cvar_t *gl_driver;
static cvar_t *gl_picmip;
static cvar_t *gl_ext_palettedtexture;
static cvar_t *gl_finish;

static cvar_t *sw_mode;
static cvar_t *sw_stipplealpha;

extern void M_ForceMenuOff( void );

/*
====================================================================

MENU INTERACTION

====================================================================
*/
#define SOFTWARE_MENU 0
#define OPENGL_MENU   1

static menuframework_s  s_software_menu;
static menuframework_s	s_opengl_menu;
static menuframework_s *s_current_menu;
static int				s_current_menu_index;

static menulist_s		s_mode_list[2];
static int s_gl_mode_count, s_initial_gl_mode;
static menufield_s s_custom_width, s_custom_height;
static menulist_s s_adjustfov_box, s_borderless_box;
static menulist_s s_native_mode;
static menufield_s s_geometry;
static vid_modemenu_t s_native_names;
static menulist_s		s_ref_list[2];
static menuslider_s		s_tq_slider;
static menuslider_s		s_screensize_slider[2];
static menuslider_s		s_brightness_slider[2];
static menulist_s  		s_fs_box[2];
static menulist_s  		s_stipple_box;
static menulist_s  		s_paletted_texture_box;
static menulist_s  		s_finish_box;
static menuaction_s		s_cancel_action[2];
static menuaction_s		s_defaults_action[2];

static void VID_DriverCallback (void *unused)
{
	s_ref_list[!s_current_menu_index].curvalue = s_ref_list[s_current_menu_index].curvalue;

#ifndef REF_HARD_LINKED
	if ( s_ref_list[s_current_menu_index].curvalue == 0 )
	{
		s_current_menu = &s_software_menu;
		s_current_menu_index = 0;
	}
	else
#endif
	{
		s_current_menu = &s_opengl_menu;
		s_current_menu_index = 1;
	}
}

static void ResolutionCallback (void *unused)
{
	if (s_current_menu_index == OPENGL_MENU)
	{
		s_native_mode.curvalue = 0;
		s_geometry.buffer[0] = 0;
	}
}

static void NativeModeCallback (void *unused)
{
	s_geometry.buffer[0] = 0;
}

static void ScreenSizeCallback( void *s )
{
	menuslider_s *slider = ( menuslider_s * ) s;

	Cvar_SetValue( "viewsize", slider->curvalue * 10 );
}

static void BrightnessCallback( void *s )
{
	menuslider_s *slider = ( menuslider_s * ) s;

	if ( s_current_menu_index == SOFTWARE_MENU )
		s_brightness_slider[1].curvalue = s_brightness_slider[0].curvalue;
	else
		s_brightness_slider[0].curvalue = s_brightness_slider[1].curvalue;

	if ( Q_stricmp( vid_ref->string, "soft" ) == 0 )
	{
		float gamma = ( 0.8f - ( slider->curvalue/10.0f - 0.5f ) ) + 0.5f;

		Cvar_SetValue( "vid_gamma", gamma );
	}
}

static void ResetDefaults( void *unused )
{
	VID_MenuInit();
}

static void ApplyChanges( void *unused )
{
	float gamma;
	float old_picmip = gl_picmip->value;
	float old_palettedtexture = gl_ext_palettedtexture->value;
	qboolean gamma_restart = true;
	int mode;
	int width, height;
	vid_geometry_t geometry;

#ifdef REF_HARD_LINKED
	if (GL_ImageColorProfileReady () && GL_HardwareGammaActive ())
		gamma_restart = false;
#endif

	mode = s_mode_list[OPENGL_MENU].curvalue;
	if (mode >= s_gl_mode_count)
		mode = mode == s_gl_mode_count ? -2 : -1;
	if (s_current_menu_index == OPENGL_MENU && s_geometry.buffer[0] &&
		!VID_ParseGeometry (s_geometry.buffer, &geometry))
	{
		Menu_SetStatusBar (&s_opengl_menu, "Geometry: WIDTHxHEIGHT[+X+Y], dimensions 320x240 through 8192x8192");
		return;
	}
	width = atoi (s_custom_width.buffer);
	height = atoi (s_custom_height.buffer);
	if (s_current_menu_index == OPENGL_MENU && !s_native_mode.curvalue &&
		!s_geometry.buffer[0] && mode == -1 &&
		(strspn (s_custom_width.buffer, "0123456789") != strlen (s_custom_width.buffer) ||
			strspn (s_custom_height.buffer, "0123456789") != strlen (s_custom_height.buffer) ||
			width < 64 || width >= 0x10000 || height < 48 || height >= 0x10000))
	{
		Menu_SetStatusBar (&s_opengl_menu, "Custom size: width 64-65535, height 48-65535");
		return;
	}

	/*
	** make values consistent
	*/
	s_fs_box[!s_current_menu_index].curvalue = s_fs_box[s_current_menu_index].curvalue;
	s_brightness_slider[!s_current_menu_index].curvalue = s_brightness_slider[s_current_menu_index].curvalue;
	s_ref_list[!s_current_menu_index].curvalue = s_ref_list[s_current_menu_index].curvalue;

	/*
	** invert sense so greater = brighter, and scale to a range of 0.5 to 1.3
	*/
	gamma = ( 0.8f - ( s_brightness_slider[s_current_menu_index].curvalue/10.0f - 0.5f ) ) + 0.5f;

	Cvar_SetValue( "vid_gamma", gamma );
	Cvar_SetValue( "sw_stipplealpha", (float)s_stipple_box.curvalue );
	Cvar_SetValue( "gl_picmip", 3 - (float)s_tq_slider.curvalue );
	Cvar_SetValue( "vid_fullscreen", (float)s_fs_box[s_current_menu_index].curvalue );
	Cvar_SetValue( "gl_ext_palettedtexture", (float)s_paletted_texture_box.curvalue );
	Cvar_SetValue( "gl_finish", (float)s_finish_box.curvalue );
	Cvar_SetValue( "sw_mode", (float)s_mode_list[SOFTWARE_MENU].curvalue );
	if (s_current_menu_index == OPENGL_MENU)
	{
		if (mode == -1)
		{
			Cvar_Set ("vid_forcewidth", s_custom_width.buffer);
			Cvar_Set ("vid_forceheight", s_custom_height.buffer);
		}
		else if (s_mode_list[OPENGL_MENU].curvalue != s_initial_gl_mode)
		{
			Cvar_SetValue ("vid_forcewidth", 0);
			Cvar_SetValue ("vid_forceheight", 0);
		}
		Cvar_SetValue ("vid_mode", (float)s_native_mode.curvalue);
		Cvar_Set ("vid_geometry", s_geometry.buffer);
		Cvar_SetValue ("gl_mode", (float)mode);
		Cvar_SetValue ("cl_adjustfov", (float)s_adjustfov_box.curvalue);
		Cvar_SetValue ("vid_borderless", (float)s_borderless_box.curvalue);
	}

#ifdef REF_HARD_LINKED
	Cvar_Set ("vid_ref", "r1gl");
	Cvar_Set ("gl_driver", "opengl32");
#else
	switch ( s_ref_list[s_current_menu_index].curvalue )
	{
		case REF_SOFT:
			Cvar_Set( "vid_ref", "soft" );
			break;
		case REF_OPENGL:
			Cvar_Set( "vid_ref", "gl" );
			Cvar_Set( "gl_driver", "opengl32" );
			break;
		case REF_3DFX:
			Cvar_Set( "vid_ref", "gl" );
			Cvar_Set( "gl_driver", "3dfxgl" );
			break;
		case REF_POWERVR:
			Cvar_Set( "vid_ref", "gl" );
			Cvar_Set( "gl_driver", "pvrgl" );
			break;
		/*case REF_VERITE:
			Cvar_Set( "vid_ref", "gl" );
			Cvar_Set( "gl_driver", "veritegl" );
			break;*/
		case REF_R1GL:
			Cvar_Set ( "vid_ref", "r1gl" );
			Cvar_Set ( "gl_driver", "opengl32" );
			break;
		case REF_NCGL:
			Cvar_Set ( "vid_ref", "ncgl" );
			Cvar_Set ( "gl_driver", "opengl32" );
			break;
	}
#endif

	/*
	** update appropriate stuff if we're running OpenGL and gamma
	** has been modified
	*/
	if (strstr( vid_ref->string, "gl" ))
	{
		if (vid_gamma->modified && gamma_restart)
		{
			vid_ref->modified = true;
			if ( Q_stricmp( gl_driver->string, "3dfxgl" ) == 0 )
			{
				char envbuffer[1024];
				float g;

				vid_ref->modified = true;

				g = 2.00f * ( 0.8f - ( vid_gamma->value - 0.5f ) ) + 1.0F;
				Com_sprintf( envbuffer, sizeof(envbuffer), "SSTV2_GAMMA=%f", g );
				putenv( envbuffer );
				Com_sprintf( envbuffer, sizeof(envbuffer), "SST_GAMMA=%f", g );
				putenv( envbuffer );
				vid_gamma->modified = false;
			}
		}

		if (gl_driver->modified || gl_picmip->value != old_picmip ||
			gl_ext_palettedtexture->value != old_palettedtexture)
			vid_ref->modified = true;
	}

	M_ForceMenuOff();
}

extern void M_PopMenu( void );
static void CancelChanges( void *unused )
{
	M_PopMenu();
}

/*
** VID_MenuInit
*/
void EXPORT VID_MenuInit( void )
{
	static const char *resolutions[] = 
	{
		"[320 240  ]",
		"[400 300  ]",
		"[512 384  ]",
		"[640 480  ]",
		"[800 600  ]",
		"[960 720  ]",
		"[1024 768 ]",
		"[1152 864 ]",
		"[1280 960 ]",
		"[1600 1200]",
		"[2048 1536]",
		"[1280 1024]",
		"[1440 900 ]",
		"[1680 1050]",
		"[2560 1920]",
		0
	};

	static const char *gl_resolutions[] = {
		"[320 240]",
		"[400 300]",
		"[512 384]",
		"[640 480]",
		"[800 600]",
		"[960 720]",
		"[1024 768]",
		"[1152 864]",
		"[1280 960]",
		"[1600 1200]",
		"[2048 1536]",
		"[1280 1024]",
		"[1440 900]",
		"[1680 1050]",
		"[2560 1920]",
		"[1280 720]",
		"[1366 768]",
		"[1600 900]",
		"[1920 1080]",
		"[1920 1200]",
		"[2560 1080]",
		"[2560 1440]",
		"[2560 1600]",
		"[3440 1440]",
		"[3840 1600]",
		"[3840 2160]",
		"[5120 1440]",
		"[5120 2160]",
		"[7680 4320]",
		"[desktop]",
		"[custom]",
		0
	};
	static const char *refs[] = {
#ifdef REF_HARD_LINKED
		"[R1GL          ]",
#else
		"[software      ]",
		"[default OpenGL]",
		"[3Dfx OpenGL   ]",
		"[PowerVR OpenGL]",
		"[R1GL          ]",
		"[NoCheat OpenGL]",
//		"[Rendition OpenGL]",
#endif
		0
	};
	static const char *yesno_names[] =
	{
		"no",
		"yes",
		0
	};
	int i;
	float custom_width, custom_height;

	if ( !gl_driver )
		gl_driver = Cvar_Get( "gl_driver", "opengl32", 0 );

	if ( !gl_picmip )
		gl_picmip = Cvar_Get( "gl_picmip", "0", 0 );

	if ( !gl_mode )
		gl_mode = Cvar_Get( "gl_mode", "3", 0 );

	if ( !sw_mode )
		sw_mode = Cvar_Get( "sw_mode", "0", 0 );

	if ( !gl_ext_palettedtexture )
		gl_ext_palettedtexture = Cvar_Get( "gl_ext_palettedtexture", "0", CVAR_ARCHIVE );

	if ( !gl_finish )
		gl_finish = Cvar_Get( "gl_finish", "0", CVAR_ARCHIVE );

	if ( !sw_stipplealpha )
		sw_stipplealpha = Cvar_Get( "sw_stipplealpha", "0", CVAR_ARCHIVE );

	s_mode_list[SOFTWARE_MENU].curvalue = sw_mode->intvalue;
	if (s_mode_list[SOFTWARE_MENU].curvalue > 14)
		s_mode_list[SOFTWARE_MENU].curvalue = 14;
	else if (s_mode_list[SOFTWARE_MENU].curvalue < 0)
		s_mode_list[SOFTWARE_MENU].curvalue = 0;

	s_gl_mode_count = sizeof (gl_resolutions) / sizeof (gl_resolutions[0]) - 3;
	if (gl_mode->intvalue == -2)
		s_mode_list[OPENGL_MENU].curvalue = s_gl_mode_count;
	else if (gl_mode->intvalue == -1)
		s_mode_list[OPENGL_MENU].curvalue = s_gl_mode_count + 1;
	else if (gl_mode->intvalue >= 0 && gl_mode->intvalue < s_gl_mode_count)
		s_mode_list[OPENGL_MENU].curvalue = gl_mode->intvalue;
	else
		s_mode_list[OPENGL_MENU].curvalue = 3;
	s_initial_gl_mode = s_mode_list[OPENGL_MENU].curvalue;

	memset (&s_custom_width, 0, sizeof (s_custom_width));
	memset (&s_custom_height, 0, sizeof (s_custom_height));
	s_custom_width.generic.type = s_custom_height.generic.type = MTYPE_FIELD;
	s_custom_width.generic.name = "custom width";
	s_custom_height.generic.name = "custom height";
	s_custom_width.generic.y = 90;
	s_custom_height.generic.y = 100;
	s_custom_width.generic.flags = s_custom_height.generic.flags = QMF_NUMBERSONLY;
	s_custom_width.length = s_custom_height.length = 5;
	s_custom_width.visible_length = s_custom_height.visible_length = 5;
	custom_width = Cvar_VariableValue ("vid_forcewidth");
	custom_height = Cvar_VariableValue ("vid_forceheight");
	Com_sprintf (s_custom_width.buffer, sizeof (s_custom_width.buffer), "%d",
		custom_width >= 64 && custom_width < 0x10000 ? (int)custom_width : 1920);
	Com_sprintf (s_custom_height.buffer, sizeof (s_custom_height.buffer), "%d",
		custom_height >= 48 && custom_height < 0x10000 ? (int)custom_height : 1080);
	s_custom_width.cursor = strlen (s_custom_width.buffer);
	s_custom_height.cursor = strlen (s_custom_height.buffer);
	s_custom_width.generic.statusbar = s_custom_height.generic.statusbar = "Size for Custom mode.";

	s_adjustfov_box.generic.type = MTYPE_SPINCONTROL;
	s_adjustfov_box.generic.name = "widescreen FOV";
	s_adjustfov_box.generic.y = 110;
	s_adjustfov_box.itemnames = yesno_names;
	s_adjustfov_box.curvalue = Cvar_Get ("cl_adjustfov", "1", CVAR_ARCHIVE)->intvalue != 0;
	s_borderless_box.generic.type = MTYPE_SPINCONTROL;
	s_borderless_box.generic.name = "borderless fullscreen";
	s_borderless_box.generic.y = 120;
	s_borderless_box.itemnames = yesno_names;
	s_borderless_box.curvalue = Cvar_Get ("vid_borderless", "0", CVAR_ARCHIVE)->intvalue != 0;
	memset (&s_native_mode, 0, sizeof (s_native_mode));
	memset (&s_geometry, 0, sizeof (s_geometry));
	if (VID_BuildModeMenu (&s_native_names, Cvar_Get ("vid_modelist", "desktop", 0)->string))
	{
		s_native_mode.generic.type = MTYPE_SPINCONTROL;
		s_native_mode.generic.name = "detected mode";
		s_native_mode.generic.y = 130;
		s_native_mode.generic.callback = NativeModeCallback;
		s_native_mode.generic.statusbar = "Overrides resolution; window geometry takes priority.";
		s_native_mode.itemnames = s_native_names.names;
		s_native_mode.curvalue = Cvar_Get ("vid_mode", "0", CVAR_ARCHIVE)->intvalue;
		if (s_native_mode.curvalue < 0 || s_native_mode.curvalue > s_native_names.count)
			s_native_mode.curvalue = 0;
	}
	s_geometry.generic.type = MTYPE_FIELD;
	s_geometry.generic.name = "window geometry";
	s_geometry.generic.y = 140;
	s_geometry.generic.statusbar = "Window only: WIDTHxHEIGHT[+X+Y].";
	s_geometry.length = sizeof (s_geometry.buffer) - 1;
	s_geometry.visible_length = 24;
	Q_strncpy (s_geometry.buffer, Cvar_Get ("vid_geometry", "", CVAR_ARCHIVE)->string, sizeof (s_geometry.buffer) - 1);
	s_geometry.buffer[sizeof (s_geometry.buffer) - 1] = 0;
	s_geometry.cursor = strlen (s_geometry.buffer);
	s_opengl_menu.statusbar = NULL;

	if ( !scr_viewsize )
		scr_viewsize = Cvar_Get ("viewsize", "100", CVAR_ARCHIVE);

	s_screensize_slider[SOFTWARE_MENU].curvalue = scr_viewsize->value/10;
	s_screensize_slider[OPENGL_MENU].curvalue = scr_viewsize->value/10;

#ifdef REF_HARD_LINKED
	s_current_menu_index = OPENGL_MENU;
	s_ref_list[0].curvalue = s_ref_list[1].curvalue = 0;
#else
	if ( strcmp( vid_ref->string, "soft" ) == 0 )
	{
		s_current_menu_index = SOFTWARE_MENU;
		s_ref_list[0].curvalue = s_ref_list[1].curvalue = REF_SOFT;
	}
	else
	{
		s_current_menu_index = OPENGL_MENU;
		if ( strcmp( gl_driver->string, "3dfxgl" ) == 0 )
			s_ref_list[s_current_menu_index].curvalue = REF_3DFX;
		else if ( strcmp( gl_driver->string, "pvrgl" ) == 0 )
			s_ref_list[s_current_menu_index].curvalue = REF_POWERVR;
		else if ( strcmp( gl_driver->string, "opengl32" ) == 0 )
		{
			if ( strcmp( vid_ref->string, "r1gl" ) == 0 )
				s_ref_list[s_current_menu_index].curvalue = REF_R1GL;
			else if ( strcmp( vid_ref->string, "ncgl" ) == 0 )
				s_ref_list[s_current_menu_index].curvalue = REF_NCGL;
			else
				s_ref_list[s_current_menu_index].curvalue = REF_OPENGL;
		}
		else
		{
			s_ref_list[s_current_menu_index].curvalue = REF_OPENGL;
		}
	}
#endif

	s_software_menu.x = (int)(viddef.width * 0.50f);
	s_software_menu.nitems = 0;
	s_opengl_menu.x = (int)(viddef.width * 0.50f);
	s_opengl_menu.nitems = 0;

	for ( i = 0; i < 2; i++ )
	{
		s_ref_list[i].generic.type = MTYPE_SPINCONTROL;
		s_ref_list[i].generic.name = "driver";
		s_ref_list[i].generic.x = 0;
		s_ref_list[i].generic.y = 0;
		s_ref_list[i].generic.callback = VID_DriverCallback;
		s_ref_list[i].itemnames = refs;

		s_mode_list[i].generic.type = MTYPE_SPINCONTROL;
		s_mode_list[i].generic.name = "video mode";
		s_mode_list[i].generic.x = 0;
		s_mode_list[i].generic.y = 10;
		s_mode_list[i].generic.callback = ResolutionCallback;
		s_mode_list[i].itemnames = i == OPENGL_MENU ? gl_resolutions : resolutions;

		s_screensize_slider[i].generic.type	= MTYPE_SLIDER;
		s_screensize_slider[i].generic.x		= 0;
		s_screensize_slider[i].generic.y		= 20;
		s_screensize_slider[i].generic.name	= "screen size";
		s_screensize_slider[i].minvalue = 3;
		s_screensize_slider[i].maxvalue = 12;
		s_screensize_slider[i].generic.callback = ScreenSizeCallback;

		s_brightness_slider[i].generic.type	= MTYPE_SLIDER;
		s_brightness_slider[i].generic.x	= 0;
		s_brightness_slider[i].generic.y	= 30;
		s_brightness_slider[i].generic.name	= "brightness";
		s_brightness_slider[i].generic.callback = BrightnessCallback;
		s_brightness_slider[i].minvalue = 5;
		s_brightness_slider[i].maxvalue = 13;
		s_brightness_slider[i].curvalue = ( 1.3f - vid_gamma->value + 0.5f ) * 10;

		s_fs_box[i].generic.type = MTYPE_SPINCONTROL;
		s_fs_box[i].generic.x	= 0;
		s_fs_box[i].generic.y	= 40;
		s_fs_box[i].generic.name	= "fullscreen";
		s_fs_box[i].itemnames = yesno_names;
		s_fs_box[i].curvalue = vid_fullscreen->intvalue ? 1 : 0;

		s_defaults_action[i].generic.type = MTYPE_ACTION;
		s_defaults_action[i].generic.name = "reset to defaults";
		s_defaults_action[i].generic.x    = 0;
		s_defaults_action[i].generic.y = i == OPENGL_MENU ? 160 : 90;
		s_defaults_action[i].generic.callback = ResetDefaults;

		s_cancel_action[i].generic.type = MTYPE_ACTION;
		s_cancel_action[i].generic.name = "cancel";
		s_cancel_action[i].generic.x    = 0;
		s_cancel_action[i].generic.y = i == OPENGL_MENU ? 170 : 100;
		s_cancel_action[i].generic.callback = CancelChanges;
	}

	s_stipple_box.generic.type = MTYPE_SPINCONTROL;
	s_stipple_box.generic.x	= 0;
	s_stipple_box.generic.y	= 60;
	s_stipple_box.generic.name	= "stipple alpha";
	s_stipple_box.curvalue = sw_stipplealpha->intvalue ? 1 : 0;
	s_stipple_box.itemnames = yesno_names;

	s_tq_slider.generic.type	= MTYPE_SLIDER;
	s_tq_slider.generic.x		= 0;
	s_tq_slider.generic.y		= 60;
	s_tq_slider.generic.name	= "texture quality";
	s_tq_slider.minvalue = 0;
	s_tq_slider.maxvalue = 3;
	s_tq_slider.curvalue = 3-gl_picmip->value;
	if (s_tq_slider.curvalue < 0)
		s_tq_slider.curvalue = 0;

	s_paletted_texture_box.generic.type = MTYPE_SPINCONTROL;
	s_paletted_texture_box.generic.x	= 0;
	s_paletted_texture_box.generic.y	= 70;
	s_paletted_texture_box.generic.name	= "8-bit textures";
	s_paletted_texture_box.itemnames = yesno_names;
	s_paletted_texture_box.curvalue = gl_ext_palettedtexture->intvalue ? 1 : 0;

	s_finish_box.generic.type = MTYPE_SPINCONTROL;
	s_finish_box.generic.x	= 0;
	s_finish_box.generic.y	= 80;
	s_finish_box.generic.name	= "sync every frame";
	s_finish_box.curvalue = gl_finish->intvalue ? 1 : 0;
	s_finish_box.itemnames = yesno_names;

	Menu_AddItem( &s_software_menu, ( void * ) &s_ref_list[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_mode_list[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_screensize_slider[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_brightness_slider[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_fs_box[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_stipple_box );

	Menu_AddItem( &s_opengl_menu, ( void * ) &s_ref_list[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_mode_list[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_screensize_slider[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_brightness_slider[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_fs_box[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_tq_slider );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_paletted_texture_box );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_finish_box );

	Menu_AddItem (&s_opengl_menu, &s_custom_width);
	Menu_AddItem (&s_opengl_menu, &s_custom_height);
	Menu_AddItem (&s_opengl_menu, &s_adjustfov_box);
	Menu_AddItem (&s_opengl_menu, &s_borderless_box);
	if (s_native_mode.itemnames)
		Menu_AddItem (&s_opengl_menu, &s_native_mode);
	Menu_AddItem (&s_opengl_menu, &s_geometry);

	Menu_AddItem( &s_software_menu, ( void * ) &s_defaults_action[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_cancel_action[SOFTWARE_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_defaults_action[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_cancel_action[OPENGL_MENU] );

	Menu_Center( &s_software_menu );
	Menu_Center( &s_opengl_menu );
	s_opengl_menu.x -= 8;
	s_software_menu.x -= 8;
}

/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw (void)
{
	int w, h;

	if ( s_current_menu_index == 0 )
		s_current_menu = &s_software_menu;
	else
		s_current_menu = &s_opengl_menu;

	/*
	** draw the banner
	*/
	re.DrawGetPicSize( &w, &h, "m_banner_video" );
	re.DrawPic( viddef.width / 2 - w / 2, viddef.height /2 - 110, "m_banner_video" );

	/*
	** move cursor to a reasonable starting position
	*/
	Menu_AdjustCursor( s_current_menu, 1 );

	/*
	** draw the menu
	*/
	Menu_Draw( s_current_menu );
}

/*
================
VID_MenuKey
================
*/
const char *VID_MenuKey( int key )
{
	menuframework_s *m = s_current_menu;

	if (!m)
		Com_Error (ERR_DROP, "VID_MenuKey: NULL s_current_menu");

	if (s_current_menu_index == OPENGL_MENU)
	{
		menucommon_s *item = Menu_ItemAtCursor (m);
		if (item && item->type == MTYPE_FIELD && Field_Key ((menufield_s *)item, key))
			return NULL;
	}

	switch ( key )
	{
	case K_ESCAPE:
		ApplyChanges( 0 );
		return NULL;
	case K_KP_UPARROW:
	case K_UPARROW:
		m->cursor--;
		Menu_AdjustCursor( m, -1 );
		break;
	case K_KP_DOWNARROW:
	case K_DOWNARROW:
		m->cursor++;
		Menu_AdjustCursor( m, 1 );
		break;
	case K_KP_LEFTARROW:
	case K_LEFTARROW:
		Menu_SlideItem( m, -1 );
		break;
	case K_KP_RIGHTARROW:
	case K_RIGHTARROW:
		Menu_SlideItem( m, 1 );
		break;
	case K_KP_ENTER:
	case K_ENTER:
		if ( !Menu_SelectItem( m ) )
			ApplyChanges( NULL );
		break;
	}

	return "misc/menu1.wav";
}


