/* Private native services for the frozen HUD editor controller. */
#ifndef R1Q2_JUMP_UI_COMPAT_H
#define R1Q2_JUMP_UI_COMPAT_H
#include "jump_compat.h"
#include <ctype.h>
#include "jump_list.h"
typedef const char *qhandle_t;
typedef int active_t;
#define ACT_ACTIVATED 1
#define KEY_CONSOLE key_console
#define UI_DRAWCURSOR 32
#define UI_IGNORECOLOR 0
#define K_SPACE 32
#define Q_clipf CLAMP
#define com_localTime cls.realtime

typedef struct
{
	char text[256];
	size_t maxChars, visibleChars, cursorPos;
} inputField_t;

#define MIN_MENU_ITEMS  64
#define MAX_MENU_ITEMS  4096

typedef enum
{
	MTYPE_BAD,
	MTYPE_SLIDER,
	MTYPE_LIST,
	MTYPE_ACTION,
	MTYPE_SPINCONTROL,
	MTYPE_SEPARATOR,
	MTYPE_FIELD,
	MTYPE_BITFIELD,
	MTYPE_PAIRS,
	MTYPE_STRINGS,
	MTYPE_VALUES,
	MTYPE_TOGGLE,
	MTYPE_STATIC,
	MTYPE_KEYBIND,
	MTYPE_BITMAP,
	MTYPE_SAVEGAME,
	MTYPE_LOADGAME,
	MTYPE_BINDFIELDS,
	MTYPE_SELECT,
	MTYPE_SELECT2,
	MTYPE_BINDSELECT2,
	MTYPE_ACTION_SELECT
} menuType_t;

#define QMF_LEFT_JUSTIFY    BIT(0)
#define QMF_GRAYED          BIT(1)
#define QMF_NUMBERSONLY     BIT(2)
#define QMF_HASFOCUS        BIT(3)
#define QMF_HIDDEN          BIT(4)
#define QMF_DISABLED        BIT(5)
#define QMF_CUSTOM_COLOR    BIT(6)
#define QMF_SHOW_VALUE      BIT(7)
#define QMF_DEFER_COMMIT    BIT(8)

typedef enum
{
	PLAQUE_LEFT,
	PLAQUE_RIGHT,
	PLAQUE_TOP,
	PLAQUE_BOTTOM,
	PLAQUE_CENTER,
	PLAQUE_SCREEN_TOPLEFT,
	PLAQUE_SCREEN_TOPRIGHT,
	PLAQUE_SCREEN_BOTTOMLEFT,
	PLAQUE_SCREEN_BOTTOMRIGHT
} plaquePosition_t;

typedef enum
{
	QMS_NOTHANDLED,
	QMS_SILENT,
	QMS_IN,
	QMS_MOVE,
	QMS_OUT,
	QMS_BEEP
} menuSound_t;

typedef enum
{
	MENU_HALIGN_CENTER,
	MENU_HALIGN_LEFT,
	MENU_HALIGN_RIGHT
} menuHAlign_t;

#define RCOLUMN_OFFSET  (CHAR_WIDTH * 2)
#define LCOLUMN_OFFSET -RCOLUMN_OFFSET

#define GENERIC_SPACING(x)   ((x) + (x) / 4)

int UI_MenuSpacing (void);
#define MENU_SPACING    UI_MenuSpacing()

#define DOUBLE_CLICK_DELAY    300

#define UI_IsItemSelectable(item) \
    ((item)->type != MTYPE_SEPARATOR && \
     (item)->type != MTYPE_STATIC && \
     !((item)->flags & (QMF_GRAYED | QMF_HIDDEN | QMF_DISABLED)))

typedef struct menuFrameWork_s
{
	jump_list_t entry;

	char *name, *title, *status;

	void **items;
	int nitems;
	int itemCapacity;

	qboolean compact;
	qboolean transparent;
	qboolean live;
	qboolean videoApply;
	qboolean videoPendingInitialized;
	qboolean videoTabsFocused;
	qboolean keywait;
	menuHAlign_t halign;

	qhandle_t image;
	color_t color;
	int y1, y2;
	vrect_t panel_rc;

	int mins[2];
	int maxs[2];
	int scrollOffset;
	int maxVisible;
	qboolean focusStyleSet;
	qboolean focusMenuWidth;
	int focusBorderWidth;
	int focusInset;
	int focusHeight;
	qhandle_t banner;
	vrect_t banner_rc;

	qhandle_t plaque;
	vrect_t plaque_rc;

	qhandle_t logo;
	vrect_t logo_rc;
	plaquePosition_t plaquePosition;

	float focusCurrentX;
	float focusCurrentWidth;
	float focusCurrentY;
	float focusCurrentHeight;
	unsigned int focusLastTime;
	qboolean focusInitialized;
	float focusPrevX;
	float focusPrevWidth;
	float focusPrevY;
	float focusPrevHeight;
	float focusTargetX;
	float focusTargetWidth;
	float focusTargetY;
	float focusTargetHeight;
	unsigned int focusAnimStartTime;
	unsigned int openTime;
	unsigned int closeTime;
	int prevFocusedIndex;
	int currFocusedIndex;
	qhandle_t bar_image_handle;
	qboolean bar_image_loaded;

	qboolean (*push) (struct menuFrameWork_s *);
	void (*pop) (struct menuFrameWork_s *);
	void (*expose) (struct menuFrameWork_s *);
	void (*draw) (struct menuFrameWork_s *);
	void (*size) (struct menuFrameWork_s *);
	void (*free) (struct menuFrameWork_s *);
	menuSound_t (*keydown) (struct menuFrameWork_s *, int);
} menuFrameWork_t;

typedef struct menuCommon_s
{
	menuType_t type;
	int id;
	char *name;
	menuFrameWork_t *parent;
	color_t color;
	vrect_t rect;
	char *status;
	char *show_if_cvar;
	char *show_if_value;

	int x, y;
	int width, height;

	int flags;
	int uiFlags;

	menuSound_t (*activate) (struct menuCommon_s *);
	menuSound_t (*change) (struct menuCommon_s *);
	menuSound_t (*keydown) (struct menuCommon_s *, int key);
	menuSound_t (*focus) (struct menuCommon_s *, qboolean gain);
} menuCommon_t;

typedef struct
{
	menuCommon_t generic;
	inputField_t field;
	cvar_t *cvar;
	int width, length;
	qboolean colorPreview;
	qboolean colorPickerOnly;
} menuField_t;

#define SLIDER_RANGE 10

typedef struct
{
	menuCommon_t generic;
	cvar_t *cvar;
	qboolean modified;

	float minvalue;
	float maxvalue;
	float curvalue;
	float step;
} menuSlider_t;

#define MAX_COLUMNS     8

#define MLIST_SPACING           GENERIC_SPACING(CHAR_HEIGHT)
#define MLIST_BORDER_WIDTH      1
#define MLIST_SCROLLBAR_WIDTH   GENERIC_SPACING(CHAR_WIDTH)
#define MLIST_PRESTEP           3
#define MLIST_PADDING           (MLIST_PRESTEP*2)

#define MLF_HEADER      BIT(0)
#define MLF_SCROLLBAR   BIT(1)
#define MLF_COLOR       BIT(2)

typedef struct
{
	const char *name;
	int width;
	int uiFlags;
} menuListColumn_t;

typedef struct menuList_s
{
	menuCommon_t generic;

	void **items;
	int numItems;
	int maxItems;
	int mlFlags;
	int extrasize;

	int prestep;
	int curvalue;
	unsigned clickTime;


	int drag_y;

	menuListColumn_t columns[MAX_COLUMNS];
	int numcolumns;
	int sortdir, sortcol;

	menuSound_t (*sort) (struct menuList_s *);
} menuList_t;

typedef struct
{
	menuCommon_t generic;
	cvar_t *cvar;
	char *cmd;
	qboolean modified;

	char **itemnames;
	char **itemvalues;
	int numItems;
	int curvalue;

	int mask;
	qboolean negate;
} menuSpinControl_t;

typedef struct
{
	menuCommon_t generic;
	char *cmd;
} menuAction_t;

typedef struct
{
	menuCommon_t generic;
} menuSeparator_t;

typedef struct
{
	menuCommon_t generic;
	int maxChars;
} menuStatic_t;

typedef struct
{
	menuCommon_t generic;
	qhandle_t pics[2];
	char *cmd;
} menuBitmap_t;

typedef struct
{
	menuCommon_t generic;
	char binding[32];
	char altbinding[32];
	char *cmd;
	char *altstatus;
} menuKeybind_t;

typedef struct
{
	menuCommon_t generic;
	char binding[32];
	char altbinding[32];
	char *cmd;
	char *altstatus;
	cvar_t **cvars;
	inputField_t *fields;
	int *fieldWidths;
	char **fieldLabels;
	int numFields;
	int focusPart; /* 0 = bind, 1..N = field index */
	qboolean numericOnly;
	int clampMin;		 /* minimum valid value (0 = no clamp) */
	int clampMax;		 /* maximum valid value (0 = no clamp) */
	vrect_t *fieldRects; /* per-focusPart mouse hit rects (numFields+1) */
} menuBindFields_t;

typedef struct
{
	menuCommon_t generic;
	cvar_t *cvar;
	char **itemnames;
	int numItems;
	int curvalue;		/* selected index, -1 if unset */
	qboolean open;		/* dropdown is visible */
	int hoverIndex;		/* mouse hover index in dropdown, -1 = none */
	vrect_t dropRect;	/* dropdown panel bounds */
	vrect_t *itemRects; /* per-item mouse hit rects in dropdown */
	int dropWidth;		/* width of dropdown panel */
	qboolean modified;
} menuSelect_t;

typedef struct
{
	menuCommon_t generic;
	cvar_t *cvar[2];
	char **itemnames[2];
	int numItems[2];
	int curvalue[2];
	int hoverIndex;
	qboolean open;
	int openIdx; /* which side is open (0 or 1), -1 = closed */
	vrect_t dropRect;
	vrect_t *itemRects;
	int dropWidth;
	int colWidth[2]; /* width of each column's value display */
} menuSelect2_t;

typedef struct
{
	menuCommon_t generic;
	char binding[32];
	char altbinding[32];
	char *cmd;
	char *legacycmd;
	char *altstatus;
	cvar_t *cvar[2];
	char **itemnames[2];
	int numItems[2];
	int curvalue[2];
	int hoverIndex;
	qboolean open;
	int openIdx;   /* which select is open: 0, 1, or -1 */
	int focusPart; /* 0 = bind, 1 = select0, 2 = select1 */
	vrect_t dropRect;
	vrect_t *itemRects;
	int colWidth[3]; /* bind, select0, select1 */
} menuBindSelect2_t;

#define MAX_PLAYERMODELS 1024

typedef struct
{
	int nskins;
	char **skindisplaynames;
	char *directory;
} playerModelInfo_t;

void PlayerModel_Load (void);
void PlayerModel_Free (void);

#define MAX_MENU_DEPTH    8

// animated menu cursor
#define CURSOR_WIDTH    32
#define CURSOR_OFFSET   25

#define NUM_CURSOR_FRAMES 15

typedef enum
{
	UI_MENU_STYLE_CUSTOM,
} uiMenuStyleId_t;

typedef struct uiColorStyle_s
{
	color_t background;
	color_t title;
	color_t normal;
	color_t selectable;
	color_t alternate;
	color_t active;
	color_t selection;
	color_t focus;
	color_t focus_border;
	color_t disabled;
} uiColorStyle_t;

typedef struct
{
	qboolean initialized;
	unsigned realtime;
	int width, height; // scaled
	float scale;
	int menuDepth;
	menuFrameWork_t *layers[MAX_MENU_DEPTH];
	menuFrameWork_t *activeMenu;
	menuCommon_t *mouseTracker;
	int mouseCoords[2];
	qboolean entersound; // play after drawing a frame, so caching
						 // won't disrupt the sound
	qboolean transparent;
	int numPlayerModels;
	playerModelInfo_t pmi[MAX_PLAYERMODELS];
	char weaponModel[32];

	qhandle_t backgroundHandle;
	qhandle_t fontHandle;
	qhandle_t cursorHandle;
	int cursorWidth, cursorHeight;

	qhandle_t bitmapCursors[NUM_CURSOR_FRAMES];

	uiColorStyle_t color;
	uint32 listHeaderColor;
	uint32 scrollbarColor;
	uint32 hintBackgroundColor;
	uint32 hintTextColor;

	uint32 focusMarkerColor;
	uint32 valueColor;
	uint32 valueActiveColor;
	uint32 valueChangedColor;
	uint32 sliderTrackColor;
	uint32 sliderFillColor;
	uint32 sliderThumbColor;
	uint32 sliderBorderColor;
	uint32 sortedHeaderColor;
	uint32 tabTextColor;
	uint32 tabActiveTextColor;
	uint32 tabActiveBgColor;
	uint32 tabInactiveBgColor;

	uint32 panelBorderColor;
	uint32 panelShadowColor;
	uint32 tabUnderlineColor;

	qboolean styleFocusFill;
	qboolean styleMenuBackground;
} uiStatic_t;

extern uiStatic_t uis;
qboolean Key_GetOverstrikeMode (void);
void Key_SetOverstrikeMode (qboolean state);
void CL_JumpFieldCopy (const char *text);
void IF_Init (inputField_t *field, size_t visible, size_t limit);
void IF_Clear (inputField_t *field);
void IF_Replace (inputField_t *field, const char *text);
qboolean IF_CharEvent (inputField_t *field, int key);
qboolean IF_KeyEvent (inputField_t *field, int key);
int IF_Draw (const inputField_t *field, int x, int y, int flags, qhandle_t font);
void UI_DrawString (int x, int y, int flags, const char *text);
void UI_PushMenu (menuFrameWork_t *menu);
void UI_PopMenu (void);
void M_JumpPushMenu (void (*draw) (void), const char *(*key) (int));
void M_JumpPopMenu (void);
qboolean CL_JumpWindowActive (void);
void CL_JumpEditorFocus (qboolean active);
void CL_JumpUIReleaseInputs (void);
void CL_JumpUITouch (void);
void CL_JumpEditorReleaseInputs (void);
const char *Q_stristr (const char *text, const char *needle);
qhandle_t R_RegisterPic (const char *name);
qboolean R_GetPicSize (int *width, int *height, qhandle_t pic);
int Key_IsDown (int key);
int Key_GetDest (void);
#endif
