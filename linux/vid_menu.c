#include "../client/client.h"
#include "../client/vid_mode_menu.h"
#include "../client/qmenu.h"

#define REF_SOFT	0
#define REF_SOFTX11	1
#define REF_OPENGL	2

extern cvar_t *vid_ref;
extern cvar_t *vid_fullscreen;
extern cvar_t *vid_gamma;
extern cvar_t *scr_viewsize;

static cvar_t *gl_mode;
static cvar_t *gl_driver;
static cvar_t *gl_picmip;
static cvar_t *gl_ext_palettedtexture;

static cvar_t *sw_mode;
static cvar_t *sw_stipplealpha;

static cvar_t *_windowed_mouse;

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
static menulist_s  		s_windowed_mouse;
static menuaction_s		s_apply_action[2];
static menuaction_s		s_defaults_action[2];

static void DriverCallback( void *unused )
{
	s_ref_list[!s_current_menu_index].curvalue = s_ref_list[s_current_menu_index].curvalue;

	if ( s_ref_list[s_current_menu_index].curvalue < 2 )
	{
		s_current_menu = &s_software_menu;
		s_current_menu_index = 0;
	}
	else
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

	if ( s_current_menu_index == 0)
		s_brightness_slider[1].curvalue = s_brightness_slider[0].curvalue;
	else
		s_brightness_slider[0].curvalue = s_brightness_slider[1].curvalue;

	if ( Q_stricmp( vid_ref->string, "soft" ) == 0 ||
		 Q_stricmp( vid_ref->string, "softx" ) == 0 )
	{
		float gamma = ( 0.8 - ( slider->curvalue/10.0 - 0.5 ) ) + 0.5;

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
	int mode;
	int width, height;
	vid_geometry_t geometry;

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
	gamma = ( 0.8 - ( s_brightness_slider[s_current_menu_index].curvalue/10.0 - 0.5 ) ) + 0.5;

	Cvar_SetValue( "vid_gamma", gamma );
	Cvar_SetValue( "sw_stipplealpha", s_stipple_box.curvalue );
	Cvar_SetValue( "gl_picmip", 3 - s_tq_slider.curvalue );
	Cvar_SetValue( "vid_fullscreen", s_fs_box[s_current_menu_index].curvalue );
	Cvar_SetValue( "gl_ext_palettedtexture", s_paletted_texture_box.curvalue );
	Cvar_SetValue( "sw_mode", s_mode_list[SOFTWARE_MENU].curvalue );
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
	Cvar_SetValue( "_windowed_mouse", s_windowed_mouse.curvalue);

	switch ( s_ref_list[s_current_menu_index].curvalue )
	{
	case REF_SOFT:
		Cvar_Set( "vid_ref", "soft" );
		break;
	case REF_SOFTX11:
		Cvar_Set( "vid_ref", "softx" );
		break;
	case REF_OPENGL:
		Cvar_Set( "vid_ref", "gl" );
		Cvar_Set( "gl_driver", "opengl32" );
		break;
	}

#if 0
	/*
	** update appropriate stuff if we're running OpenGL and gamma
	** has been modified
	*/
	if ( strstr( vid_ref->string, "gl" ))
	{
		if ( vid_gamma->modified )
		{
			vid_ref->modified = true;
			if ( Q_stricmp( gl_driver->string, "3dfxgl" ) == 0 )
			{
				char envbuffer[1024];
				float g;

				vid_ref->modified = true;

				g = 2.00 * ( 0.8 - ( vid_gamma->value - 0.5 ) ) + 1.0F;
				Com_sprintf( envbuffer, sizeof(envbuffer), "SST_GAMMA=%f", g );
				putenv( envbuffer );

				vid_gamma->modified = false;
			}
		}
	}
#endif

	M_ForceMenuOff();
}

/*
** VID_MenuInit
*/
void VID_MenuInit( void )
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
		"[1280 1024]",
		"[1600 1200]",
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
		"[1280 1024]",
		"[1600 1200]",
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
	static const char *refs[] =
	{
		"[software      ]",
		"[software X11  ]",
		"[default OpenGL]",
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
		gl_ext_palettedtexture = Cvar_Get( "gl_ext_palettedtexture", "1", CVAR_ARCHIVE );

	if ( !sw_stipplealpha )
		sw_stipplealpha = Cvar_Get( "sw_stipplealpha", "0", CVAR_ARCHIVE );

	if ( !_windowed_mouse)
        _windowed_mouse = Cvar_Get( "_windowed_mouse", "0", CVAR_ARCHIVE );

	s_mode_list[SOFTWARE_MENU].curvalue = sw_mode->value;
	if (s_mode_list[SOFTWARE_MENU].curvalue < 0 || s_mode_list[SOFTWARE_MENU].curvalue > 9)
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
	s_custom_width.generic.statusbar = s_custom_height.generic.statusbar = "Used by the custom video mode";

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
		s_native_mode.generic.statusbar = "Overrides legacy resolution; geometry takes priority in a window";
		s_native_mode.itemnames = s_native_names.names;
		s_native_mode.curvalue = Cvar_Get ("vid_mode", "0", CVAR_ARCHIVE)->intvalue;
		if (s_native_mode.curvalue < 0 || s_native_mode.curvalue > s_native_names.count)
			s_native_mode.curvalue = 0;
	}
	s_geometry.generic.type = MTYPE_FIELD;
	s_geometry.generic.name = "window geometry";
	s_geometry.generic.y = 140;
	s_geometry.generic.statusbar = "Optional WIDTHxHEIGHT[+X+Y]; fullscreen ignores this";
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

	if ( strcmp( vid_ref->string, "soft" ) == 0)
	{
		s_current_menu_index = SOFTWARE_MENU;
		s_ref_list[0].curvalue = s_ref_list[1].curvalue = REF_SOFT;
	}
	else if (strcmp( vid_ref->string, "softx" ) == 0 ) 
	{
		s_current_menu_index = SOFTWARE_MENU;
		s_ref_list[0].curvalue = s_ref_list[1].curvalue = REF_SOFTX11;
	}
	else if ( strstr( vid_ref->string, "gl" ))
	{
		s_current_menu_index = OPENGL_MENU;
		s_ref_list[s_current_menu_index].curvalue = REF_OPENGL;
#if 0
		if ( strcmp( gl_driver->string, "3dfxgl" ) == 0 )
			s_ref_list[s_current_menu_index].curvalue = REF_3DFX;
		else if ( strcmp( gl_driver->string, "pvrgl" ) == 0 )
			s_ref_list[s_current_menu_index].curvalue = REF_POWERVR;
		else if ( strcmp( gl_driver->string, "opengl32" ) == 0 )
			s_ref_list[s_current_menu_index].curvalue = REF_OPENGL;
		else
			s_ref_list[s_current_menu_index].curvalue = REF_VERITE;
#endif
	}

	s_software_menu.x = viddef.width * 0.50;
	s_software_menu.nitems = 0;
	s_opengl_menu.x = viddef.width * 0.50;
	s_opengl_menu.nitems = 0;

	for ( i = 0; i < 2; i++ )
	{
		s_ref_list[i].generic.type = MTYPE_SPINCONTROL;
		s_ref_list[i].generic.name = "driver";
		s_ref_list[i].generic.x = 0;
		s_ref_list[i].generic.y = 0;
		s_ref_list[i].generic.callback = DriverCallback;
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
		s_brightness_slider[i].curvalue = ( 1.3 - vid_gamma->value + 0.5 ) * 10;

		s_fs_box[i].generic.type = MTYPE_SPINCONTROL;
		s_fs_box[i].generic.x	= 0;
		s_fs_box[i].generic.y	= 40;
		s_fs_box[i].generic.name	= "fullscreen";
		s_fs_box[i].itemnames = yesno_names;
		s_fs_box[i].curvalue = vid_fullscreen->value;

		s_defaults_action[i].generic.type = MTYPE_ACTION;
		s_defaults_action[i].generic.name = "reset to default";
		s_defaults_action[i].generic.x    = 0;
		s_defaults_action[i].generic.y = i == OPENGL_MENU ? 160 : 90;
		s_defaults_action[i].generic.callback = ResetDefaults;

		s_apply_action[i].generic.type = MTYPE_ACTION;
		s_apply_action[i].generic.name = "apply";
		s_apply_action[i].generic.x    = 0;
		s_apply_action[i].generic.y = i == OPENGL_MENU ? 170 : 100;
		s_apply_action[i].generic.callback = ApplyChanges;
	}

	s_stipple_box.generic.type = MTYPE_SPINCONTROL;
	s_stipple_box.generic.x	= 0;
	s_stipple_box.generic.y	= 60;
	s_stipple_box.generic.name	= "stipple alpha";
	s_stipple_box.curvalue = sw_stipplealpha->value;
	s_stipple_box.itemnames = yesno_names;

	s_windowed_mouse.generic.type = MTYPE_SPINCONTROL;
	s_windowed_mouse.generic.x  = 0;
	s_windowed_mouse.generic.y  = 72;
	s_windowed_mouse.generic.name   = "windowed mouse";
	s_windowed_mouse.curvalue = _windowed_mouse->value;
	s_windowed_mouse.itemnames = yesno_names;

	s_tq_slider.generic.type	= MTYPE_SLIDER;
	s_tq_slider.generic.x		= 0;
	s_tq_slider.generic.y		= 60;
	s_tq_slider.generic.name	= "texture quality";
	s_tq_slider.minvalue = 0;
	s_tq_slider.maxvalue = 3;
	s_tq_slider.curvalue = 3-gl_picmip->value;

	s_paletted_texture_box.generic.type = MTYPE_SPINCONTROL;
	s_paletted_texture_box.generic.x	= 0;
	s_paletted_texture_box.generic.y	= 70;
	s_paletted_texture_box.generic.name	= "8-bit textures";
	s_paletted_texture_box.itemnames = yesno_names;
	s_paletted_texture_box.curvalue = gl_ext_palettedtexture->value;

	Menu_AddItem( &s_software_menu, ( void * ) &s_ref_list[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_mode_list[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_screensize_slider[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_brightness_slider[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_fs_box[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_stipple_box );
	Menu_AddItem( &s_software_menu, ( void * ) &s_windowed_mouse );

	Menu_AddItem( &s_opengl_menu, ( void * ) &s_ref_list[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_mode_list[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_screensize_slider[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_brightness_slider[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_fs_box[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_tq_slider );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_paletted_texture_box );

	Menu_AddItem (&s_opengl_menu, &s_custom_width);
	Menu_AddItem (&s_opengl_menu, &s_custom_height);
	Menu_AddItem (&s_opengl_menu, &s_adjustfov_box);
	Menu_AddItem (&s_opengl_menu, &s_borderless_box);
	if (s_native_mode.itemnames)
		Menu_AddItem (&s_opengl_menu, &s_native_mode);
	Menu_AddItem (&s_opengl_menu, &s_geometry);

	Menu_AddItem( &s_software_menu, ( void * ) &s_defaults_action[SOFTWARE_MENU] );
	Menu_AddItem( &s_software_menu, ( void * ) &s_apply_action[SOFTWARE_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_defaults_action[OPENGL_MENU] );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_apply_action[OPENGL_MENU] );

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
	extern void M_PopMenu( void );

	menuframework_s *m = s_current_menu;
	static const char *sound = "misc/menu1.wav";

	if (s_current_menu_index == OPENGL_MENU)
	{
		menucommon_s *item = Menu_ItemAtCursor (m);
		if (item && item->type == MTYPE_FIELD && Field_Key ((menufield_s *)item, key))
			return NULL;
	}

	switch ( key )
	{
	case K_ESCAPE:
		M_PopMenu();
		return NULL;
	case K_UPARROW:
		m->cursor--;
		Menu_AdjustCursor( m, -1 );
		break;
	case K_DOWNARROW:
		m->cursor++;
		Menu_AdjustCursor( m, 1 );
		break;
	case K_LEFTARROW:
		Menu_SlideItem( m, -1 );
		break;
	case K_RIGHTARROW:
		Menu_SlideItem( m, 1 );
		break;
	case K_ENTER:
		Menu_SelectItem( m );
		break;
	}

	return sound;
}


