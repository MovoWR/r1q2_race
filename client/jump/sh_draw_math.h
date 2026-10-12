/* Effective drawing bounds never change saved cvars or editor drafts. */
#pragma once
#include <math.h>
#include "jump_compat.h"

static inline float SH_ClampDrawValue (float value, float low, float high)
{
	if (high < low)
		high = low;
	if (!isfinite (value))
		return low;
	return value < low ? low : value > high ? high :
											  value;
}

/* Negative offsets anchor a fixed-size overlay to the far screen edge. */
static inline int SH_EdgeOrigin (int offset, int size, int extent)
{
	int origin = offset < 0 ? extent + offset + 1 - size : offset;
	return origin < 0 ? 0 : origin > extent - size ? extent - size :
													 origin;
}

/* Native widgets use physical-pixel draw groups, outside the workbench zoom. */
static inline float SH_VisualScale (const cvar_t *var)
{
	const float value = var ? HUD_EditorValue (var) : 1.0f;
	return isfinite (value) ? SH_ClampDrawValue (value, HUD_SCALE_MIN, HUD_SCALE_MAX) : 1.0f;
}

static inline void SH_BeginVisualDraw (hud_edit_id_t id, float sx, float sy,
	float x, float y, float hud_scale)
{
	const float unit = hud_scale > 0.0f ? hud_scale : 1.0f;
	R_BeginDrawGroup (0, 0, !HUD_EditorShow (id));
	R_SetDrawGroupScale (sx, sy, x / unit, y / unit);
}

static inline void SH_EndVisualDraw (hud_edit_id_t id, float hud_scale)
{
	vrect_t bounds;
	if (R_EndDrawGroup (&bounds))
	{
		const float unit = hud_scale > 0.0f ? hud_scale : 1.0f;
		HUD_EditorBounds (id, bounds.x * unit, bounds.y * unit,
			bounds.width * unit, bounds.height * unit);
	}
}

/* Oversize items retain the requested edge instead of producing inverted limits. */
static inline int SH_VisualEdgeOrigin (int offset, int size, int extent, float scale)
{
	if (scale == 1.0f)
		return SH_EdgeOrigin (offset, size, extent);
	const int scaled_size = (int)ceilf (size * scale);
	const int spare = extent - scaled_size;
	const int origin = offset < 0 ? extent + offset + 1 - scaled_size : offset;
	return (int)SH_ClampDrawValue (origin, min (0, spare), max (0, spare));
}
