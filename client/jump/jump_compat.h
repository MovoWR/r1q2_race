/* Native client adapters used only by the imported Jump drawing modules. */
#ifndef R1Q2_JUMP_COMPAT_H
#define R1Q2_JUMP_COMPAT_H
#include "../client.h"
#include "../ref_present.h"
#include "jump.h"
#include "jump_settings.h"
#include <float.h>
#include <assert.h>

#define HUD_SCALE_MIN 0.25f
#define HUD_SCALE_MAX 4.0f
#define CHAR_WIDTH 8
#define CHAR_HEIGHT 8
#define UI_CENTER 1
#define UI_DROPSHADOW 2
#define UI_NOSHADOW 4
#define UI_RIGHT 8
#define UI_ALTCOLOR 16
#define U32_WHITE 0xffffffffu
#define U32_YELLOW 0xff00ffffu
#define UI_LEFT 0
#define q_countof(a) (sizeof(a)/sizeof((a)[0]))
#ifndef BIT
#define BIT(n) (1u << (n))
#endif
#define Q_clip CLAMP
#define LAG_WIDTH 48
#define LAG_HEIGHT 48

static __inline void Q_strlcpy (char *dst, const char *src, size_t size)
{
	if (size)
	{
		strncpy (dst, src, size - 1);
		dst[size - 1] = 0;
	}
}

#define U32_GREEN 0xff00ff00u
#define U32_RED 0xff0000ffu
#define Q_rint(x) ((int)floor((x) + 0.5f))
#define Q_scnprintf Com_sprintf
#define Q_snprintf Com_sprintf
#ifndef min
#define min(a, b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a, b) ((a)>(b)?(a):(b))
#endif
#ifndef CLAMP
#define CLAMP(x, low, high) ((x)<(low)?(low):(x)>(high)?(high):(x))
#endif

typedef union
{
	uint32 u32;
	byte u8[4];
} color_t;

#include "jump_editor.h"

static __inline uint32 MakeColor (byte r, byte g, byte b, byte a)
{
	color_t color;
	color.u8[0] = r;
	color.u8[1] = g;
	color.u8[2] = b;
	color.u8[3] = a;
	return color.u32;
}

static __inline float Cvar_ClampValue (const cvar_t *var, float low, float high)
{
	float value = var ? var->value : low;
	return value == value && value >= -FLT_MAX && value <= FLT_MAX ? CLAMP (value, low, high) : low;
}

typedef struct
{
	int hud_width, hud_height;
	float hud_scale;
	const char *net_pic;
	int font_pic;
} jump_screen_t;

extern jump_screen_t jump_screen;
qboolean CL_JumpPrepareHUD (void);
extern cvar_t *jump_hud_alpha;

static __inline int Cvar_ClampInteger (const cvar_t *var, int low, int high)
{
	return (int)Cvar_ClampValue (var, (float)low, (float)high);
}

#define R_DrawString SCR_DrawStringEx
size_t R_Fps_m (char *out, size_t size);
size_t R_Mfps_m (char *out, size_t size);
size_t CL_Mfps_m (char *out, size_t size);
size_t CL_Mps_m (char *out, size_t size);
size_t CL_DemoPos_m (char *out, size_t size);
size_t CL_Mmps_m (char *out, size_t size);
size_t CL_Pps_m (char *out, size_t size);
size_t CL_Ping_m (char *out, size_t size);
size_t CL_Lag_m (char *out, size_t size);
void R_SetAlpha (float alpha);
void R_DrawFill8 (float x, float y, float w, float h, int color);
void R_DrawStretchPic (float x, float y, float w, float h, const char *pic);
void SCR_DrawString (float x, float y, int flags, const char *text);
void R_SetColor (uint32 color);
void R_ClearColor (void);
void R_SetScale (float scale);
void R_SetFont (const char *font);

typedef struct
{
	int left, top, right, bottom;
} clipRect_t;

void R_SetClipRect (const clipRect_t *clip);
void R_SetDrawTransform (float x, float y, float scale);
void R_DrawFill32 (float x, float y, float w, float h, uint32 color);
void R_BeginDrawGroup (float x, float y, qboolean hidden);
void R_SetDrawGroupScale (float sx, float sy, float x, float y);
qboolean R_EndDrawGroup (vrect_t *bounds);
qboolean R_EndDrawGroupRaw (vrect_t *bounds, vrect_t *raw);
int SCR_DrawStringEx (float x, float y, int flags, int max_chars, const char *text, int font);
void CL_JumpUpdateDrawSample (float frametime);
qboolean CL_JumpParseColor (const char *text, uint32 *color);
#endif
