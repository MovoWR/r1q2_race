/* Private display trials; adapted to native R1Q2 video capabilities.
 * Backend/menu integration must complete before use; public DLL ABI is unchanged.
 */
#ifndef JUMP_DISPLAY_H
#define JUMP_DISPLAY_H
#include "../../qcommon/qcommon.h"

#define JUMP_DISPLAY_CONFIRM_MSEC 15000u
#define JUMP_DISPLAY_MAX_MONITORS 32
#define JUMP_DISPLAY_MAX_MODES 4096
#define JUMP_DISPLAY_WINDOW_BORDERLESS 1
#define JUMP_DISPLAY_WINDOW_NOTITLE 2
#define JUMP_DISPLAY_WINDOW_NORESIZE 4
#define JUMP_DISPLAY_WINDOW_FLAGS 7
#define JUMP_DISPLAY_CAP_VSYNC 1u
#define JUMP_DISPLAY_CAP_ADAPTIVE 2u

typedef enum
{
	JUMP_DISPLAY_WINDOWED,
	JUMP_DISPLAY_BORDERLESS,
	JUMP_DISPLAY_EXCLUSIVE
} jump_display_kind_t;

typedef struct
{
	int x, y, width, height;
} jump_display_rect_t;

typedef struct
{
	jump_display_kind_t mode;
	char display[32]; /* native monitor ID, empty selects the available default */
	jump_display_rect_t window;
	int window_flags, width, height, refresh, depth, vsync;
} jump_display_settings_t;

typedef struct
{
	char id[32], name[128];
	jump_display_rect_t desktop;
	int refresh, depth;
	qboolean primary;
} jump_display_monitor_t;

typedef struct
{
	int width, height, refresh, depth;
} jump_display_mode_t;

typedef struct
{
	qboolean available, active, applying, committing;
	unsigned int capabilities; /* VSync/adaptive choices require native support. */
	int seconds;
	jump_display_settings_t current, previous, requested;
	char message[160];
} jump_display_snapshot_t;

void CL_JumpDisplayInit (void);
qboolean CL_JumpDisplayAvailable (void);
qboolean CL_JumpDisplayBusy (void);
qboolean CL_JumpDisplayActive (void);
qboolean CL_JumpDisplayToggle (void);
void CL_JumpDisplaySnapshot (jump_display_snapshot_t *out);
/* Caller-owned arrays; return filled count, bounded by capacity and native limits. */
int CL_JumpDisplayMonitors (jump_display_monitor_t *out, int capacity);
int CL_JumpDisplayModes (const char *display, jump_display_mode_t *out, int capacity);
qboolean CL_JumpDisplayApply (const jump_display_settings_t *settings);
qboolean CL_JumpDisplayConfirm (void);
void CL_JumpDisplayRevert (void);
void CL_JumpDisplayFrame (void);
void CL_JumpDisplayFocus (qboolean active);
void CL_JumpDisplayClose (void);
#endif
