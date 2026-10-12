/* Private native display transactions; original GLW/public layouts are unchanged. */
#ifndef GLW_DISPLAY_H
#define GLW_DISPLAY_H
#include <windows.h>
#include "../client/ref_display.h"
const refdisplay_t *GLW_GetDisplayAPI (void);
qboolean GLW_DisplayConfigured (void);
qboolean GLW_DisplayStart (qboolean fullscreen, qboolean borderless, int width, int height, int refresh, int depth);
qboolean GLW_DisplayBusy (void);
int GLW_DisplayWindowFlags (void);
void GLW_DisplayShutdown (void);
qboolean GLW_DisplayActivate (qboolean active);
/* Narrow helpers for the existing GLW window/swap state. */
qboolean GLimp_DisplayWindowed (void);
qboolean GLimp_DisplayAdjust (RECT *rect, DWORD style, DWORD exstyle);
void GLimp_DisplayWindowState (jump_display_kind_t mode);
qboolean GLimp_DisplayDrawable (void);
qboolean GLimp_DisplayLegacyDesktop (DEVMODEA *desktop);
unsigned int GLimp_DisplayCapabilities (void);
int GLimp_DisplaySwapRead (void);
qboolean GLimp_DisplaySwapSet (int interval);
#endif
