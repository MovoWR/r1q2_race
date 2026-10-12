/* Private full racing menu contract; no public ABI changes. */
#ifndef R1Q2_JUMP_MENU_H
#define R1Q2_JUMP_MENU_H
#include "jump_ui_compat.h"
#include <errno.h>
#define MenuList_Init JUI_MenuList_Init
#define MenuList_SetValue JUI_MenuList_SetValue
#define MenuList_Sort JUI_MenuList_Sort
#define Menu_AddItem JUI_Menu_AddItem
#define Menu_AdjustCursor JUI_Menu_AdjustCursor
#define Menu_CharEvent JUI_Menu_CharEvent
#define Menu_Draw JUI_Menu_Draw
#define Menu_Ease01 JUI_Menu_Ease01
#define Menu_Free JUI_Menu_Free
#define Menu_FreeColorPicker JUI_Menu_FreeColorPicker
#define Menu_HandleOpenSelect JUI_Menu_HandleOpenSelect
#define Menu_HitTest JUI_Menu_HitTest
#define Menu_Init JUI_Menu_Init
#define Menu_ItemAtCursor JUI_Menu_ItemAtCursor
#define Menu_KeyEvent JUI_Menu_KeyEvent
#define Menu_Keydown JUI_Menu_Keydown
#define Menu_Layout JUI_Menu_Layout
#define Menu_MouseMove JUI_Menu_MouseMove
#define Menu_Pop JUI_Menu_Pop
#define Menu_Push JUI_Menu_Push
#define Menu_SelectItem JUI_Menu_SelectItem
#define Menu_SetColor JUI_Menu_SetColor
#define Menu_SetFocus JUI_Menu_SetFocus
#define Menu_Size JUI_Menu_Size
#define Menu_SlideItem JUI_Menu_SlideItem
#define Menu_UpdateShowIf JUI_Menu_UpdateShowIf
#define SpinControl_Init JUI_SpinControl_Init

#include <limits.h>
#include <stdarg.h>
#define bool qboolean
#define qtrue true
#define qfalse false

#define Q_atoi atoi
#define Q_atof atof
#define Q_isspace(c) isspace((unsigned char)(c))
#define Q_isdigit(c) isdigit((unsigned char)(c))
#define Q_isprint(c) isprint((unsigned char)(c))
#define Q_ALIGN(n, a) (((n)+(a)-1)&~((a)-1))
#define Q_ERR(e) (-(e))
#define CVAR_NOARCHIVE 0
#define MAX_FIELD_TEXT 256
#define CVAR_PRIVATE 0
#define CVAR_ROM CVAR_NOSET
#define FROM_MENU 0
#define FROM_CODE 1
#define FROM_CONSOLE 0
#define MAX_CLIENT_NAME 32
#define MakeColor(r, g, b, a) ((uint32)(byte)(r)|((uint32)(byte)(g)<<8)|((uint32)(byte)(b)<<16)|((uint32)(byte)(a)<<24))
#define UI_Malloc(n) CL_JumpUIAlloc(n)
#define UI_Mallocz(n) CL_JumpUIAlloc(n)
#define UI_CopyString(s) CL_JumpUICopy(s)
#define Z_Malloc(n) CL_JumpUIAlloc(n)
#define Z_Realloc(p, n) CL_JumpUIRealloc(p,n)
#define Z_Free(p) free(p)
#define Com_Printf CL_JumpUIPrintf
#define Com_WPrintf Com_Printf
#define Com_EPrintf Com_Printf
#define Q_ErrorString(ret) "not found or unreadable"
#define Q_strcasecmp Q_stricmp
#define Q_concat(...) CL_JumpUIConcat(__VA_ARGS__,NULL)
#define Q_strlcat CL_JumpUIStrlcat
#define COM_IsFloat CL_JumpUIIsFloat
#define COM_IsUint CL_JumpUIIsUint
#define Com_EscapeString CL_JumpUIEscape
#define COM_Compress CL_JumpUICompress
#define Cvar_WeakGet(n) Cvar_Get((n),"",0)
#define Cvar_SetByVar(v, s, from) CL_JumpUISet((v),(s),(from))
#define Cvar_SetInteger(v, n, from) CL_JumpUISetValue((v),(n),(from))
#define Cvar_SetValue(v, n, from) CL_JumpUISetValue((v),(n),(from))
#define Cvar_SetEx(n, s, from) Cvar_Set((n),(s))
#define Cvar_VariableInteger(n) ((int)Cvar_VariableValue(n))
#define Cvar_VariableStringBuffer(n, p, z) Q_strlcpy((p),Cvar_VariableString(n),(z))
#define R_RegisterFont R_RegisterPic
#define R_RegisterModel(n) re.RegisterModel((char *)(n))
#define R_RegisterSkin(n) re.RegisterSkin((char *)(n))
#define R_RenderFrame CL_JumpUIRenderFrame
#define R_RegistrationSequence CL_JumpUIRegistrationSequence
#define R_ClampScale CL_JumpUIScale
#define Key_KeynumToLabel Key_KeynumToString
#define Key_WaitKey CL_JumpUIWaitKey
#define Key_EnumBindings CL_JumpUIEnumBindings
#define Cmd_Argc CL_JumpUIArgc
#define Cmd_Argv CL_JumpUIArgv
#define Cmd_ArgsFrom CL_JumpUIArgsFrom
#define Cmd_ArgvBuffer(i, p, z) Q_strlcpy((p),Cmd_Argv(i),(z))
#define Cmd_TokenizeString CL_JumpUITokenize
#define Cmd_RemoveArg CL_JumpUIRemoveArg
#define Cmd_ParseOptions CL_JumpUIParseOptions
#define Cmd_FindMacro CL_JumpUIFindMacro
#define Cmd_Register CL_JumpUIRegisterCommands
#define Cmd_Deregister CL_JumpUIDeregisterCommands
#define Cmd_PrintHint() ((void)0)
#define Prompt_AddMatch(ctx, s) ((void)0)
#define Z_LeakTest(tag) ((void)0)
#define TAG_UI 0
#define IN_WarpMouse(x, y) ((void)0)
#define Con_Close(force) ((void)0)
#define CL_IsJumpGame() CL_JumpEnabled()
#define fs_game Cvar_FindVar("game")

typedef struct
{
	int unused;
} genctx_t;

typedef struct
{
	const char *name;
	void (*function) (void);
	void (*completer) (genctx_t *, int);
} cmdreg_t;

typedef struct
{
	const char *sh, *lo, *help;
} cmd_option_t;

typedef struct
{
	size_t (*function) (char *, size_t);
} cmd_macro_t;

typedef enum
{
	UIMENU_DEFAULT,
	UIMENU_MAIN,
	UIMENU_GAME,
	UIMENU_NONE
} uiMenu_t;

extern int cmd_optind;
extern char *cmd_optarg, *cmd_optopt;
void CL_JumpUISetNumberByName (const char *name, float value);
uint32 CL_JumpUIRandom (uint32 bound);
char **FS_JumpList (const char *directory, const char *suffix, qboolean dirs, int *count);
char **CL_JumpUIList (const char *directory, const char *suffix, qboolean dirs, int *count);
void CL_JumpUIFreeList (void **list);
char **CL_JumpUIReallocList (char **list, int count);
void CL_JumpUIStripExtension (char *out, const char *name, size_t size);
qboolean CL_JumpUIFileExists (const char *name);
void CL_JumpUIPrintf (const char *fmt, ...);
void *CL_JumpUIAlloc (size_t size);
void *CL_JumpUIRealloc (void *ptr, size_t size);
char *CL_JumpUICopy (const char *text);
size_t CL_JumpUIConcat (char *out, size_t size, ...);
size_t CL_JumpUIStrlcat (char *out, const char *text, size_t size);
qboolean CL_JumpUIIsFloat (const char *s);
qboolean CL_JumpUIIsUint (const char *s);
size_t CL_JumpUIEscape (char *out, const char *s, size_t size);
void CL_JumpUICompress (char *s);
void CL_JumpUISet (cvar_t *var, const char *value, int from);
void CL_JumpUISetValue (cvar_t *var, float value, int from);
int CL_JumpUIArgc (void);
char *CL_JumpUIArgv (int index);
char *CL_JumpUIArgsFrom (int index);
void CL_JumpUICaptureArgs (void);
char *CL_JumpUIParse (const char **data);
void CL_JumpUITokenize (char *s, qboolean expand);
void CL_JumpUIRemoveArg (int index);
int CL_JumpUIParseOptions (const cmd_option_t *options);
cmd_macro_t *CL_JumpUIFindMacro (const char *name);
void CL_JumpUIRegisterCommands (const cmdreg_t *commands);
void CL_JumpUIDeregisterCommands (const cmdreg_t *commands);
void CL_JumpUIWaitKey (qboolean (*callback) (void *, int), void *arg);
int CL_JumpUIEnumBindings (int start, const char *command);
void R_SetAltColor (uint32 color);
void R_DrawPic (int x, int y, qhandle_t pic);
void R_DrawKeepAspectPic (int x, int y, int w, int h, qhandle_t pic);
int CL_JumpUIString (int x, int y, int flags, int maxchars, const char *text, qhandle_t font);
void R_DrawChar (int x, int y, int flags, int ch, qhandle_t font);
qboolean SCR_ParseColor (const char *text, color_t *color);
float Q_circ_clipf (float value, float low, float high);
float CL_JumpUIScale (cvar_t *var);
struct image_s *CL_JumpUISkin (const char *path);
void CL_JumpUIFrame (void);
void CL_JumpUIRenderFrame (refdef_t *refdef);
unsigned CL_JumpUIRegistrationSequence (void);
qboolean CL_JumpUIFullscreen (void);
float V_CalcFov (float fov, int width, int height);
char *SV_GetSaveInfo (const char *slot);
qboolean SH_GetPresetColors (const char *name, const char **accel, const char **optimal, const char **center);
qboolean Cvar_IsRegistered (const cvar_t *var);
qboolean CL_JumpUIBuiltinAllowed (void);
char *CL_JumpUIExpand (const char *text);
void CL_JumpUIRegister (void);
void CL_JumpUIInit (void);
void CL_JumpUIShutdown (void);
qboolean CL_JumpUIActive (void);
qboolean CL_JumpUIKey (int key, qboolean down);
qboolean CL_JumpUIChar (int key);
void CL_JumpUIMouse (int x, int y);
qboolean CL_JumpUIMain (void);
void CL_JumpUIClear (void);
void CL_JumpUITouch (void);
void CL_JumpUIMenuRegister (void);
void UI_KeyEvent (int key, qboolean down);
void UI_CharEvent (int key);
void UI_MouseEvent (int x, int y);
void UI_Draw (unsigned realtime);
void UI_ModeChanged (void);
void UI_OpenMenu (uiMenu_t type);
extern jump_list_t ui_menus;

extern cvar_t *ui_debug;
extern cvar_t *cl_menu_cursor;

extern cvar_t *ui_menu_focus_width;
extern cvar_t *ui_menu_focus_padding_x;
extern cvar_t *ui_menu_focus_padding_y;
extern cvar_t *ui_menu_density;

extern cvar_t *ui_menu_bar_image;
extern cvar_t *ui_menu_bar_image_alpha;
extern cvar_t *ui_menu_bar_image_mode;

extern cvar_t *ui_menu_anim;
extern cvar_t *ui_menu_anim_focus_ms;

extern cvar_t *ui_menu_title_top_padding;
extern cvar_t *ui_menu_title_item_gap;

void UI_SetCursor (const char *name);
const char *UI_GetCursorTextureName (const char *name);

void UI_PushMenu (menuFrameWork_t *menu);
void UI_ForceMenuOff (void);
void UI_PopMenu (void);
void UI_StartSound (menuSound_t sound);
qboolean UI_DoHitTest (void);
qboolean UI_CursorInRect (const vrect_t *rect);
void *UI_FormatColumns (int extrasize, ...);
char *UI_GetColumn (char *s, int n);
void UI_DrawString (int x, int y, int flags, const char *string);
void UI_DrawChar (int x, int y, int flags, int ch);
void UI_DrawRect8 (const vrect_t *rect, int border, int c);
//void        UI_DrawRect32(const vrect_t *rect, int border, uint32 color);
void UI_StringDimensions (vrect_t *rc, int flags, const char *string);
uint32 UI_MenuBackgroundColor (const menuFrameWork_t *menu);
uint32 UI_MenuListHeaderColor (void);
uint32 UI_MenuScrollbarColor (void);
uint32 UI_MenuHintBackgroundColor (void);
uint32 UI_MenuHintTextColor (void);
qboolean UI_MenuStyleFocusFill (void);

uiMenuStyleId_t UI_MenuStyleId (void);
uint32 UI_MenuFocusMarkerColor (void);
uint32 UI_MenuValueColor (void);
uint32 UI_MenuValueActiveColor (void);
uint32 UI_MenuValueChangedColor (void);
uint32 UI_MenuSliderTrackColor (void);
uint32 UI_MenuSliderFillColor (void);
uint32 UI_MenuSliderThumbColor (void);
uint32 UI_MenuSliderBorderColor (void);
uint32 UI_MenuSortedHeaderColor (void);
uint32 UI_MenuTabTextColor (void);
uint32 UI_MenuTabActiveTextColor (void);
uint32 UI_MenuTabActiveBgColor (void);
uint32 UI_MenuTabInactiveBgColor (void);
uint32 UI_MenuPanelBorderColor (void);
uint32 UI_MenuPanelShadowColor (void);
uint32 UI_MenuTabUnderlineColor (void);

void UI_LoadScript (void);
bool UI_IsMenuActive (const char *name);
menuFrameWork_t *UI_FindMenu (const char *name);

void Menu_SetColor (uint32 color);
void Menu_Init (menuFrameWork_t *menu);
void Menu_Layout (menuFrameWork_t *menu);
void Menu_Size (menuFrameWork_t *menu);
void Menu_Draw (menuFrameWork_t *menu);
void Menu_UpdateShowIf (menuFrameWork_t *menu);
void Menu_AddItem (menuFrameWork_t *menu, void *item);
menuSound_t Menu_SelectItem (menuFrameWork_t *menu);
menuSound_t Menu_SlideItem (menuFrameWork_t *menu, int dir);
menuSound_t Menu_KeyEvent (menuCommon_t *item, int key);
menuSound_t Menu_CharEvent (menuCommon_t *item, int key);
menuSound_t Menu_MouseMove (menuCommon_t *item);
qboolean Menu_HandleOpenSelect (void);
menuSound_t Menu_Keydown (menuFrameWork_t *menu, int key);
void Menu_SetFocus (menuCommon_t *item);
menuSound_t Menu_AdjustCursor (menuFrameWork_t *menu, int dir);
menuCommon_t *Menu_ItemAtCursor (menuFrameWork_t *menu);
menuCommon_t *Menu_HitTest (menuFrameWork_t *menu);
void MenuList_Init (menuList_t *l);
void MenuList_SetValue (menuList_t *l, int value);
void MenuList_Sort (menuList_t *l, int offset,
	int (*cmpfunc) (const void *, const void *));
void SpinControl_Init (menuSpinControl_t *s);
qboolean Menu_Push (menuFrameWork_t *menu);
void Menu_Pop (menuFrameWork_t *menu);
void Menu_Free (menuFrameWork_t *menu);
void Menu_FreeColorPicker (void);

void JUI_Menu_PlayerConfig (void);
void JUI_Menu_Demos (void);
void JUI_Menu_Video (void);
void JUI_VideoFrame (void);
qboolean JUI_VideoApplyChanges (void);
void JUI_VideoDiscardChanges (void);
qboolean JUI_VideoTabsAvailable (void);
void Menu_VideoPrepare (menuFrameWork_t *menu);
qboolean Menu_VideoApply (void);
void Menu_VideoDiscard (void);
qboolean Menu_VideoPending (void);
void Menu_VideoClose (void);
void JUI_Menu_Servers (void);
void CL_JumpBrowserUIFree (void);

extern menuFrameWork_t *ui_drawing_menu;
extern int menu_drawing_item_index;
extern qboolean ui_builtin_menu_active;

void UI_SetColor_Wrapper (uint32 color);
void UI_SetAltColor_Wrapper (uint32 color);
void UI_ClearColor_Wrapper (void);
void UI_DrawFill32_Wrapper (int x, int y, int w, int h, uint32 color);

float Menu_Ease01 (float t);

#ifndef UI_C_IMPLEMENTATION
#define R_SetColor(color) UI_SetColor_Wrapper(color)
#define R_SetAltColor(color) UI_SetAltColor_Wrapper(color)
#define R_ClearColor() UI_ClearColor_Wrapper()
#define R_DrawFill32(x, y, w, h, color) UI_DrawFill32_Wrapper(x, y, w, h, color)
#endif

#endif
