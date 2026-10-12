/* Optional context-preserving display API, negotiated separately from drawing.
 * Version 4 of GetPresentationAPI; all earlier private/public layouts are retained.
 */
#ifndef REF_DISPLAY_H
#define REF_DISPLAY_H
#include "jump/jump_display.h"
#define REF_DISPLAY_VERSION 4

typedef struct
{
	int version, size;
	qboolean (EXPORT *Available) (void);
	int (EXPORT *Monitors) (jump_display_monitor_t *out, int capacity);
	int (EXPORT *Modes) (const char *display, jump_display_mode_t *out, int capacity);
	qboolean (EXPORT *Read) (jump_display_settings_t *out, unsigned int *capabilities);
	qboolean (EXPORT *Begin) (void);
	qboolean (EXPORT *Apply) (const jump_display_settings_t *settings, jump_display_settings_t *actual);
	/* End(false) restores the native checkpoint, or a usable window if unplugged. */
	qboolean (EXPORT *End) (qboolean keep);
	const char *(EXPORT *Message) (void);
} refdisplay_t;

void CL_JumpDisplaySetAPI (const refdisplay_t *api);
#endif
