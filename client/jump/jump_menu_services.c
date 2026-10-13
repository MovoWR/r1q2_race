/* Private native services for the frozen menu/widget/script algorithms. */
#define UI_C_IMPLEMENTATION
#include "jump_menu.h"
#include "jump_sh_commands.h"
#include "jump_servers.h"
#include "jump_demos.h"
#include "jump_display.h"
#undef Com_Printf
#undef Cvar_SetValue
#undef Cmd_Argc
#undef Cmd_Argv
#undef Cmd_ArgsFrom
#undef Cmd_TokenizeString
extern char *keybindings[256];

static char cmd_storage[65536], cmd_args[65536], cmd_null_string[1];
static char *cmd_argv[4096];
static int cmd_argc;
int cmd_optind;
char *cmd_optarg, *cmd_optopt;
static qboolean (*jump_wait_key) (void *, int);
static void *jump_wait_arg;
static unsigned jump_registration = 1;
static qboolean jump_ui_loaded;
static char jump_ui_game[MAX_QPATH];

void CL_JumpUIPrintf (const char *fmt, ...)
{
	char text[8192];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (text, sizeof (text), fmt, ap);
	va_end (ap);
	text[sizeof (text) - 1] = 0;
	Com_Printf ("%s", LOG_CLIENT, text);
}

void *CL_JumpUIAlloc (size_t size)
{
	void *ptr;
	if (size > 16 * 1024 * 1024)
		Com_Error (ERR_DROP, "Jump menu allocation exceeds limit");
	ptr = calloc (1, size ? size : 1);
	if (!ptr)
		Com_Error (ERR_DROP, "Jump menu allocation failed");
	return ptr;
}

void *CL_JumpUIRealloc (void *ptr, size_t size)
{
	void *out;
	if (size > 16 * 1024 * 1024)
		Com_Error (ERR_DROP, "Jump menu allocation exceeds limit");
	out = realloc (ptr, size ? size : 1);
	if (!out)
		Com_Error (ERR_DROP, "Jump menu allocation failed");
	return out;
}

char *CL_JumpUICopy (const char *text)
{
	size_t size;
	char *out;
	if (!text)
		return NULL;
	size = strlen (text) + 1;
	out = CL_JumpUIAlloc (size);
	memcpy (out, text, size);
	return out;
}

size_t CL_JumpUIStrlcat (char *out, const char *text, size_t size)
{
	size_t a = strlen (out), b = strlen (text);
	if (a < size)
		Q_strlcpy (out + a, text, size - a);
	return a + b;
}

size_t CL_JumpUIConcat (char *out, size_t size, ...)
{
	const char *part;
	size_t length = 0;
	va_list ap;
	if (size)
		*out = 0;
	va_start (ap, size);
	while ((part = va_arg (ap, const char *)) != NULL)
	{
		if (length < size)
			Q_strlcpy (out + length, part, size - length);
		length += strlen (part);
	}
	va_end (ap);
	return length;
}

qboolean CL_JumpUIIsFloat (const char *s)
{
	char *end;
	double value;
	if (!s || !*s)
		return false;
	errno = 0;
	value = strtod (s, &end);
	return *end == 0 && !errno && value == value && value >= -FLT_MAX && value <= FLT_MAX;
}

qboolean CL_JumpUIIsUint (const char *s)
{
	if (!s || !*s)
		return false;
	for (; *s; s++)
		if (!isdigit ((unsigned char)*s))
			return false;
	return true;
}

float Q_circ_clipf (float value, float low, float high)
{
	/* Menu ranges may run backwards; endpoints still clamp without wrapping. */
	if (!(value >= -FLT_MAX && value <= FLT_MAX))
		return low;
	return low > high ? CLAMP (value, high, low) : CLAMP (value, low, high);
}

size_t CL_JumpUIEscape (char *out, const char *s, size_t size)
{
	size_t n = 0;
	if (!size)
		return 0;
	for (; *s && n + 1 < size; s++)
	{
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\' || c == '$')
		{
			if (n + 2 >= size)
				break;
			out[n++] = '\\';
		}
		if (c >= 32 && c != 127)
			out[n++] = c;
	}
	out[n] = 0;
	return n;
}

void CL_JumpUICompress (char *s)
{
	char *r = s, *w = s;
	int quoted = 0;
	while (*r)
	{
		if (!quoted && r[0] == '/' && r[1] == '/')
		{
			while (*r && *r != '\n')
				r++;
			continue;
		}
		if (!quoted && r[0] == '/' && r[1] == '*')
		{
			r += 2;
			while (*r && !(r[0] == '*' && r[1] == '/'))
			{
				if (*r == '\n')
					*w++ = '\n';
				r++;
			}
			if (*r)
				r += 2;
			*w++ = ' ';
			continue;
		}
		if (r[0] == '\\' && r[1] == '\n')
		{
			r += 2;
			*w++ = ' ';
			continue;
		}
		if (*r == '"' && (r == s || r[-1] != '\\'))
			quoted = !quoted;
		if (*r != '\r')
			*w++ = *r;
		r++;
	}
	*w = 0;
}

void CL_JumpUISet (cvar_t *var, const char *value, int from)
{
	int oldvalue;
	if (!var || !value)
		return;
	oldvalue = var->intvalue;
	if (from == FROM_CODE && !strncmp (var->name, "ui_", 3))
		Cvar_ForceSet (var->name, value);
	else
	{
		/* These native extension switches are separate from their numeric controls. */
		if (!strcmp (var->name, "gl_ext_samples") && CL_JumpUIIsFloat (value))
			Cvar_Set ("gl_ext_multisample", atof (value) > 0 ? "1" : "0");
		if (!strcmp (var->name, "gl_ext_max_anisotropy") && CL_JumpUIIsFloat (value))
			Cvar_Set ("gl_ext_texture_filter_anisotropic", atof (value) > 1 ? "1" : "0");
		Cvar_Set (var->name, value);
		if (from == FROM_MENU && !strcmp (var->name, "s_initsound") && var->intvalue != oldvalue)
			Cbuf_AddText ("snd_restart\n");
	}
}

void CL_JumpUISetValue (cvar_t *var, float value, int from)
{
	char text[64];
	if (!(value >= -FLT_MAX && value <= FLT_MAX))
		return;
	Com_sprintf (text, sizeof (text), "%g", value);
	CL_JumpUISet (var, text, from);
}

void CL_JumpUISetNumberByName (const char *name, float value)
{
	Cvar_SetValue (name, value);
}

uint32 CL_JumpUIRandom (uint32 bound)
{
	unsigned value = (unsigned)rand ();
	return bound ? value % bound : 0;
}

int CL_JumpUIArgc (void)
{
	return cmd_argc;
}

char *CL_JumpUIArgv (int index)
{
	return index >= 0 && index < cmd_argc ? cmd_argv[index] : cmd_null_string;
}

char *CL_JumpUIArgsFrom (int index)
{
	int i;
	cmd_args[0] = 0;
	for (i = index; i < cmd_argc; i++)
	{
		if (i > index)
			CL_JumpUIStrlcat (cmd_args, " ", sizeof (cmd_args));
		CL_JumpUIStrlcat (cmd_args, cmd_argv[i], sizeof (cmd_args));
	}
	return cmd_args;
}

void CL_JumpUIRemoveArg (int index)
{
	if (index < 0 || index >= cmd_argc)
		return;
	memmove (cmd_argv + index, cmd_argv + index + 1, (cmd_argc - index - 1) * sizeof (cmd_argv[0]));
	cmd_argc--;
}

char *CL_JumpUIParse (const char **data)
{
	static char token[4096];
	const char *s = *data;
	size_t n = 0;
	int quoted;
	if (!s)
	{
		token[0] = 0;
		return token;
	}
	while (*s && (unsigned char)*s <= 32)
		s++;
	if (!*s)
	{
		*data = NULL;
		token[0] = 0;
		return token;
	}
	quoted = *s == '"';
	if (quoted)
		s++;
	while (*s && (quoted ? *s != '"' : (unsigned char)*s > 32))
	{
		if (n + 1 < sizeof (token))
			token[n++] = *s;
		s++;
	}
	if (quoted && *s == '"')
		s++;
	token[n] = 0;
	*data = s;
	return token;
}

void CL_JumpUITokenize (char *text, qboolean expand)
{
	const char *r = text;
	char *w = cmd_storage, *limit = cmd_storage + sizeof (cmd_storage) - 1;
	int quoted;
	cmd_argc = 0;
	cmd_optind = 1;
	cmd_optarg = cmd_optopt = cmd_null_string;
	if (expand)
	{
		char *expanded = CL_JumpUIExpand (text);
		if (!expanded)
			return;
		r = expanded;
	}
	while (*r && cmd_argc < (int)q_countof (cmd_argv))
	{
		while (*r && (unsigned char)*r <= 32)
			r++;
		if (!*r)
			break;
		cmd_argv[cmd_argc++] = w;
		quoted = *r == '"';
		if (quoted)
			r++;
		while (*r && (quoted ? *r != '"' : (unsigned char)*r > 32))
		{
			if (w >= limit)
			{
				cmd_argc = 0;
				cmd_storage[0] = 0;
				return;
			}
			if (*r == '\\' && (r[1] == '"' || r[1] == '\\' || r[1] == '$'))
				r++;
			*w++ = *r++;
		}
		if (quoted && *r == '"')
			r++;
		if (w >= limit)
		{
			cmd_argc = 0;
			return;
		}
		*w++ = 0;
	}
	if (*r)
		cmd_argc = 0;
}

void CL_JumpUICaptureArgs (void)
{
	int i;
	char *w = cmd_storage;
	cmd_argc = 0;
	cmd_optind = 1;
	for (i = 0; i < Cmd_Argc () && i < (int)q_countof (cmd_argv); i++)
	{
		const char *s = Cmd_Argv (i);
		size_t n = strlen (s) + 1;
		if (n > (size_t)(cmd_storage + sizeof (cmd_storage) - w))
		{
			cmd_argc = 0;
			break;
		}
		cmd_argv[cmd_argc++] = w;
		memcpy (w, s, n);
		w += n;
	}
}

/* Frozen macro callbacks have no context parameter, so each name keeps a
 * small private thunk. Samples are evaluated when the parser requests them. */
#define JUMP_MENU_MACRO(name) \
static size_t CL_JumpUIMacro_##name(char *out,size_t size) \
{ \
 if(!size)return 0; \
 if(!CL_JumpMacro(#name,out,(int)min(size,(size_t)INT_MAX)))out[0]=0; \
 return strlen(out); \
}
JUMP_MENU_MACRO (cl_mapname)
JUMP_MENU_MACRO (cl_server)
JUMP_MENU_MACRO (cl_timer)
JUMP_MENU_MACRO (cl_demopos)
JUMP_MENU_MACRO (cl_pps)
JUMP_MENU_MACRO (cl_ping)
JUMP_MENU_MACRO (cl_lag)
JUMP_MENU_MACRO (cl_health)
JUMP_MENU_MACRO (cl_ammo)
JUMP_MENU_MACRO (cl_armor)
JUMP_MENU_MACRO (cl_weaponmodel)
JUMP_MENU_MACRO (cl_numentities)
JUMP_MENU_MACRO (cl_surface)
JUMP_MENU_MACRO (r_mfps)
JUMP_MENU_MACRO (r_fps)
JUMP_MENU_MACRO (cl_mfps)
JUMP_MENU_MACRO (cl_mmps)
JUMP_MENU_MACRO (cl_fps)
JUMP_MENU_MACRO (cl_rups)
JUMP_MENU_MACRO (cl_ups)
JUMP_MENU_MACRO (cl_playerpos_z)
JUMP_MENU_MACRO (cl_playerpos_y)
JUMP_MENU_MACRO (cl_playerpos_x)
JUMP_MENU_MACRO (loc_here)
JUMP_MENU_MACRO (loc_there)
#undef JUMP_MENU_MACRO

cmd_macro_t *CL_JumpUIFindMacro (const char *name)
{
	static const char *names[] = { "cl_mapname", "cl_server", "cl_timer", "cl_demopos", "cl_pps", "cl_ping", "cl_lag", "cl_health", "cl_ammo", "cl_armor", "cl_weaponmodel", "cl_numentities", "cl_surface", "r_mfps", "r_fps", "cl_mfps", "cl_mmps", "cl_fps", "cl_rups", "cl_ups", "cl_playerpos_z", "cl_playerpos_y", "cl_playerpos_x", "loc_here", "loc_there" };
	static cmd_macro_t macros[] = { { CL_JumpUIMacro_cl_mapname }, { CL_JumpUIMacro_cl_server }, { CL_JumpUIMacro_cl_timer }, { CL_JumpUIMacro_cl_demopos }, { CL_JumpUIMacro_cl_pps }, { CL_JumpUIMacro_cl_ping }, { CL_JumpUIMacro_cl_lag }, { CL_JumpUIMacro_cl_health }, { CL_JumpUIMacro_cl_ammo }, { CL_JumpUIMacro_cl_armor }, { CL_JumpUIMacro_cl_weaponmodel }, { CL_JumpUIMacro_cl_numentities }, { CL_JumpUIMacro_cl_surface }, { CL_JumpUIMacro_r_mfps }, { CL_JumpUIMacro_r_fps }, { CL_JumpUIMacro_cl_mfps }, { CL_JumpUIMacro_cl_mmps }, { CL_JumpUIMacro_cl_fps }, { CL_JumpUIMacro_cl_rups }, { CL_JumpUIMacro_cl_ups }, { CL_JumpUIMacro_cl_playerpos_z }, { CL_JumpUIMacro_cl_playerpos_y }, { CL_JumpUIMacro_cl_playerpos_x }, { CL_JumpUIMacro_loc_here }, { CL_JumpUIMacro_loc_there } };
	unsigned i;
	if (!name)
		return NULL;
	for (i = 0; i < q_countof (names); i++)
		if (!strcmp (name, names[i]))
			return macros + i;
	return NULL;
}

void CL_JumpUIRegisterCommands (const cmdreg_t *commands)
{
	for (; commands->name; commands++)
	{
		const char *existing = Cmd_CompleteCommandOld (commands->name);
		if ((!existing || strcmp (existing, commands->name)) && !Cvar_FindVar (commands->name))
			Cmd_AddCommand (commands->name, commands->function);
	}
}

void CL_JumpUIDeregisterCommands (const cmdreg_t *commands)
{
	(void)commands; /* Native command ownership persists until shutdown. */
}

void CL_JumpUIWaitKey (qboolean (*callback) (void *, int), void *arg)
{
	jump_wait_key = callback;
	jump_wait_arg = arg;
}

int CL_JumpUIEnumBindings (int start, const char *command)
{
	int i;
	for (i = max (0, start); i < 256; i++)
		if (keybindings[i] && command && !Q_stricmp (keybindings[i], command))
			return i;
	return -1;
}

void R_DrawPic (int x, int y, qhandle_t pic)
{
	int w, h;
	if (R_GetPicSize (&w, &h, pic))
		R_DrawStretchPic (x, y, w, h, pic);
}

void R_DrawKeepAspectPic (int x, int y, int w, int h, qhandle_t pic)
{
	int pw, ph;
	float scale;
	if (w <= 0 || h <= 0 || !R_GetPicSize (&pw, &ph, pic))
		return;
	scale = min ((float)w / pw, (float)h / ph);
	R_DrawStretchPic (x + (w - pw * scale) / 2, y + (h - ph * scale) / 2, pw * scale, ph * scale, pic);
}

int CL_JumpUIString (int x, int y, int flags, int maxchars, const char *text, qhandle_t font)
{
	int result;
	R_SetFont (font);
	result = SCR_DrawStringEx (x, y, flags, maxchars, text, 0);
	R_SetFont (NULL);
	return result;
}

void R_DrawChar (int x, int y, int flags, int ch, qhandle_t font)
{
	char text[2];
	text[0] = (char)ch;
	text[1] = 0;
	CL_JumpUIString (x, y, flags, 1, text, font);
}

qboolean SCR_ParseColor (const char *text, color_t *color)
{
	return color && CL_JumpParseColor (text, &color->u32);
}

float CL_JumpUIScale (cvar_t *var)
{
	float scale = var ? var->value : 0;
	if (!(scale >= .25f && scale <= 4))
		scale = min (CL_JumpViewportWidth () / 640.0f, CL_JumpViewportHeight () / 480.0f);
	return 1.0f / CLAMP (scale, .25f, 4);
}

qboolean CL_JumpUIFullscreen (void)
{
	return Cvar_VariableValue ("vid_fullscreen") != 0;
}

unsigned CL_JumpUIRegistrationSequence (void)
{
	return jump_registration;
}

float V_CalcFov (float fov, int width, int height)
{
	return width > 0 && height > 0 && fov >= 1 && fov <= 179 ? (float)(atan (height / (width / tan (fov * M_PI / 360))) * 360 / M_PI) : 90;
}

void CL_JumpUIRenderFrame (refdef_t *refdef)
{
	refdef_t copy;
	int w, h;
	float base;
	if (!refdef || !scr_rich_presentation || !scr_presentation || !re.RenderFrame)
		return;
	copy = *refdef;
	base = scr_presentation->GetSize (-1, &w, &h);
	if (!(base >= 1 && base <= 1024))
		return;
	copy.x = (int)(copy.x / base);
	copy.y = (int)(copy.y / base);
	copy.width = max (1, (int)(copy.width / base));
	copy.height = max (1, (int)(copy.height / base));
	scr_rich_presentation->End ();
	re.RenderFrame (&copy);
	scr_rich_presentation->Begin ();
}

char *SV_GetSaveInfo (const char *slot)
{
	static char info[33];
	char path[MAX_QPATH];
	void *data = NULL;
	int length;
	info[0] = 0;
	if (!slot || strchr (slot, '/') || strchr (slot, '\\') || strstr (slot, ".."))
		return NULL;
	Com_sprintf (path, sizeof (path), "save/%s/server.ssv", slot);
	length = FS_LoadFile (path, &data);
	if (length >= 32 && data)
	{
		memcpy (info, data, 32);
		info[32] = 0;
	}
	if (data)
		FS_FreeFile (data);
	return info[0] ? info : NULL;
}

static void CL_JumpSHCommand (void)
{
	if (!CL_JumpEnabled ())
	{
		Com_Printf ("Jump tools are available only when game is jump.\n", LOG_CLIENT);
		return;
	}
	CL_JumpUICaptureArgs ();
	SH_Cmd_f ();
}

static void CL_JumpMenuReload (void)
{
	if (!scr_rich_presentation || !CL_JumpEnabled ())
		return;
	if (jump_ui_loaded)
		CL_JumpUIShutdown ();
	jump_ui_loaded = false;
	CL_JumpUITouch ();
}

void CL_JumpUITouch (void)
{
	if (!CL_JumpEnabled ())
		return;
	if (jump_ui_loaded && uis.initialized && !strcmp (jump_ui_game, Cvar_VariableString ("game")))
		return;
	if (uis.initialized)
		CL_JumpUIShutdown ();
	CL_JumpUIInit ();
	jump_ui_loaded = true;
	Q_strlcpy (jump_ui_game, Cvar_VariableString ("game"), sizeof (jump_ui_game));
}

qboolean CL_JumpUIActive (void)
{
	return CL_JumpEnabled () && uis.activeMenu && cls.key_dest == key_menu;
}

qboolean CL_JumpUIKey (int key, qboolean down)
{
	if (!CL_JumpUIActive ())
		return false;
	if (HUD_EditorActive ())
		return HUD_EditorKey (key, down);
	if (down && jump_wait_key)
	{
		jump_wait_key (jump_wait_arg, key);
		return true;
	}
	UI_KeyEvent (key, down);
	return down;
}

qboolean CL_JumpUIChar (int key)
{
	if (!CL_JumpUIActive ())
		return false;
	if (HUD_EditorActive ())
		return HUD_EditorChar (key);
	UI_CharEvent (key);
	return true;
}

void CL_JumpUIMouse (int x, int y)
{
	if (CL_JumpUIActive ())
		UI_MouseEvent (x, y);
}

qboolean CL_JumpUIMain (void)
{
	if (!scr_rich_presentation || !CL_JumpEnabled ())
		return false;
	CL_JumpUITouch ();
	UI_OpenMenu (cls.state == ca_active ? UIMENU_GAME : UIMENU_MAIN);
	return uis.activeMenu != NULL;
}

void CL_JumpUIReleaseInputs (void)
{
	if (jump_wait_key)
		jump_wait_key (jump_wait_arg, K_ESCAPE);
	jump_wait_key = NULL;
	jump_wait_arg = NULL;
	uis.mouseTracker = NULL;
}

void CL_JumpUIClear (void)
{
	CL_JumpUIReleaseInputs ();
	CL_JumpDisplayClose ();
	if (uis.menuDepth)
		UI_ForceMenuOff ();
	CL_JumpServersClose ();
	CL_JumpDemosClose ();
	jump_registration++;
}

void CL_JumpUIMenuRegister (void)
{
	static const cmdreg_t commands[] = { { "sh", CL_JumpSHCommand, NULL }, { "ui_reload", CL_JumpMenuReload, NULL }, { NULL, NULL, NULL } };
	CL_JumpUIRegisterCommands (commands);
	CL_JumpUIRegister ();
	CL_JumpServersInit ();
}

#define Com_Printf CL_JumpUIPrintf

int CL_JumpUIParseOptions (const cmd_option_t *opt)
{
	const cmd_option_t *o;
	char *s, *p;

	cmd_optopt = cmd_null_string;

	if (cmd_optind == cmd_argc)
	{
		cmd_optarg = cmd_null_string;
		return -1; // no more arguments
	}

	s = cmd_argv[cmd_optind];
	if (*s != '-')
	{
		cmd_optarg = s;
		return -1; // non-option argument
	}
	cmd_optopt = s++;

	if (*s == '-')
	{
		s++;
		if (*s == 0)
		{
			if (++cmd_optind < cmd_argc)
			{
				cmd_optarg = cmd_argv[cmd_optind];
			}
			else
			{
				cmd_optarg = cmd_null_string;
			}
			return -1; // special terminator
		}

		// check for long option argument
		if ((p = strchr (s, '=')) != NULL)
		{
			*p = 0;
		}

		// parse long option
		for (o = opt; o->sh; o++)
		{
			if (!strcmp (o->lo, s))
			{
				break;
			}
		}
		if (!o->sh)
		{
			goto unknown;
		}

		// parse long option argument
		if (p)
		{
			if (o->sh[1] != ':')
			{
				Com_Printf ("%s does not take an argument.\n", cmd_argv[cmd_optind]);
				Cmd_PrintHint ();
				return '!';
			}
			cmd_optarg = p + 1;
		}
	}
	else
	{
		// parse short option
		for (o = opt; o->sh; o++)
		{
			if (o->sh[0] == *s)
			{
				break;
			}
		}
		if (!o->sh || s[1])
		{
			goto unknown;
		}
		p = NULL;
	}

	// parse option argument
	if (!p && o->sh[1] == ':')
	{
		if (cmd_optind + 1 == cmd_argc)
		{
			Com_Printf ("Missing argument to %s.\n", cmd_argv[cmd_optind]);
			Cmd_PrintHint ();
			return ':';
		}
		cmd_optarg = cmd_argv[++cmd_optind];
	}

	cmd_optind++;
	return o->sh[0];

unknown:
	Com_Printf ("Unknown option: %s.\n", cmd_argv[cmd_optind]);
	Cmd_PrintHint ();
	return '?';
}

char **CL_JumpUIList (const char *directory, const char *suffix, qboolean dirs, int *count)
{
	return FS_JumpList (directory, suffix, dirs, count);
}

void CL_JumpUIFreeList (void **list)
{
	int i;
	if (!list)
		return;
	for (i = 0; list[i]; i++)
		free (list[i]);
	free (list);
}

char **CL_JumpUIReallocList (char **list, int count)
{
	return CL_JumpUIRealloc (list, (size_t)count * sizeof (char *));
}

void CL_JumpUIStripExtension (char *out, const char *name, size_t size)
{
	char *dot;
	Q_strlcpy (out, name, size);
	dot = strrchr (out, '.');
	if (dot)
		*dot = 0;
}

qboolean CL_JumpUIFileExists (const char *name)
{
	FILE *file = NULL;
	qboolean closeHandle;
	int len = FS_FOpenFile (name, &file, HANDLE_NONE, &closeHandle);
	if (file && closeHandle)
		fclose (file);
	return len >= 0;
}

struct image_s *CL_JumpUISkin (const char *path)
{
	static const char *extensions[] = { ".pcx", ".png", ".jpg", ".tga" };
	char name[MAX_QPATH];
	int i;
	struct image_s *skin;
	for (i = 0; i < (int)q_countof (extensions); i++)
	{
		Com_sprintf (name, sizeof (name), "%s%s", path, extensions[i]);
		skin = re.RegisterSkin (name);
		if (skin)
			return skin;
	}
	return NULL;
}

void CL_JumpUIFrame (void)
{
	static unsigned lastFrame;
	static qboolean framed;
	if (framed && lastFrame == (unsigned)cls.realtime)
		return;
	framed = true;
	lastFrame = (unsigned)cls.realtime;
	CL_JumpServersFrame ();
	if (uis.initialized && strcmp (jump_ui_game, Cvar_VariableString ("game")))
	{
		CL_JumpUIShutdown ();
		jump_ui_loaded = false;
	}
	CL_JumpDisplayFrame ();
	if (!CL_JumpEnabled ())
		return;
	JUI_VideoFrame ();
}

qboolean CL_JumpUIBuiltinAllowed (void)
{
	const char *kind = CL_JumpUIArgv (0), *name;
	int i = 1;
	qboolean center = false;

	if (strcmp (kind, "range") && strcmp (kind, "toggle") &&
		strcmp (kind, "pairs") && strcmp (kind, "strings") &&
		strcmp (kind, "values") && strcmp (kind, "field") &&
		strcmp (kind, "select") && strcmp (kind, "select2"))
		return true;

	while (i < cmd_argc && cmd_argv[i][0] == '-')
	{
		const char *option = cmd_argv[i++];
		if (!strcmp (option, "--status") || !strcmp (option, "-s") || !strcmp (option, "--width") || !strcmp (option, "-w") || !strcmp (option, "--length") || !strcmp (option, "-l"))
			i++;
		else if (!strcmp (option, "--show-if") || !strcmp (option, "-I"))
			i += 2;
		else if (!strcmp (option, "--center") || !strcmp (option, "-c"))
			center = true;
	}

	/* Centered fields omit the label and start with the cvar name. */
	name = CL_JumpUIArgv (i + (!strcmp (kind, "field") && center ? 0 : 1));
	return *name && Cvar_IsRegistered (Cvar_FindVar (name));
}

char *CL_JumpUIExpand (const char *text)
{
	static char expanded[65536];
	char name[MAX_QPATH], value[4096];
	size_t i = 0, o = 0, n;
	int quoted = 0;
	while (text[i])
	{
		if (o + 1 >= sizeof (expanded))
			return NULL;
		if (text[i] == '"')
			quoted = !quoted;
		if (text[i] == '\\' && (text[i + 1] == '$' || text[i + 1] == '"' || text[i + 1] == '\\'))
		{
			if (o + 2 >= sizeof (expanded))
				return NULL;
			expanded[o++] = text[i++];
			expanded[o++] = text[i++];
			continue;
		}
		if (text[i] != '$' || quoted)
		{
			expanded[o++] = text[i++];
			continue;
		}
		i++;
		if (text[i] == '$')
		{
			expanded[o++] = '$';
			i++;
			continue;
		}
		n = 0;
		if (text[i] == '{')
		{
			i++;
			while (text[i] && text[i] != '}' && n + 1 < sizeof (name))
				name[n++] = text[i++];
			if (text[i] != '}')
				return NULL;
			i++;
		}
		else
			while ((isalnum ((unsigned char)text[i]) || text[i] == '_' || text[i] == '.') && n + 1 < sizeof (name))
				name[n++] = text[i++];
		name[n] = 0;
		if (!n)
		{
			expanded[o++] = '$';
			continue;
		}
		if (!CL_JumpMacro (name, value, sizeof (value)))
			Q_strlcpy (value, Cvar_VariableString (name), sizeof (value));
		n = strlen (value);
		if (n >= sizeof (expanded) - o)
			return NULL;
		memcpy (expanded + o, value, n);
		o += n;
	}
	if (quoted)
		return NULL;
	expanded[o] = 0;
	return expanded;
}
