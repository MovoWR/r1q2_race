/* Pure layout math shared by the editor and regression tests. */
#pragma once
#include <math.h>

static inline float HudEdit_Clamp (float value, float low, float high)
{
	if (!isfinite (value))
		return low;
	if (high < low)
		high = low;
	return value < low ? low : value > high ? high :
											  value;
}

/* Preserve the edge anchor of legacy signed graph coordinates. */
static inline float HudEdit_EdgeOffset (float position, float extent,
	float size, float original)
{
	position = HudEdit_Clamp (position, 0, extent - size);
	return original < 0 ? position - extent + size - 1 : position;
}

static inline float HudEdit_Snap (float position, float size, float extent,
	float threshold)
{
	const float targets[] = { 0, (extent - size) * 0.5f, extent - size };
	float result = position, distance = threshold;
	{
		int i;
		for (i = 0; i < 3; i++)
		{
			float delta = fabsf (position - targets[i]);
			if (delta <= distance)
			{
				result = targets[i];
				distance = delta;
			}
		}
	}
	return HudEdit_Clamp (result, 0, extent - size);
}

/* Efficiency is attached below or above the helper; zero means below. */
static inline float HudEdit_EfficiencyOffset (float top, float height,
	float helper_top, float helper_height)
{
	if (top + height < helper_top)
		return top + height - helper_top;
	return fmaxf (0, top - helper_top - helper_height);
}

/* Incremental nearest-target snapping; ties keep the first (screen) candidate. */
typedef struct
{
	float proposed, size, extent, distance, result, guide;
} hud_edit_snap_axis_t;

static inline void HudEdit_SnapAxisCandidate (hud_edit_snap_axis_t *axis,
	float position, float guide)
{
	if (!isfinite (position) || position < 0 || position > axis->extent - axis->size)
		return;
	float distance = fabsf (axis->proposed - position);
	if (distance > axis->distance ||
		(distance == axis->distance && isfinite (axis->guide)))
		return;
	axis->distance = distance;
	axis->result = position;
	axis->guide = guide;
}

static inline void HudEdit_SnapAxisInit (hud_edit_snap_axis_t *axis, float proposed,
	float size, float extent, float threshold)
{
	axis->proposed = isfinite (proposed) ? proposed : 0;
	axis->size = fmaxf (0, size);
	axis->extent = fmaxf (0, extent);
	axis->distance = fmaxf (0, threshold);
	axis->result = HudEdit_Clamp (axis->proposed, 0, axis->extent - axis->size);
	axis->guide = NAN;
	HudEdit_SnapAxisCandidate (axis, 0, 0);
	HudEdit_SnapAxisCandidate (axis, (axis->extent - axis->size) * 0.5f, axis->extent * 0.5f);
	HudEdit_SnapAxisCandidate (axis, axis->extent - axis->size, axis->extent);
}

static inline void HudEdit_SnapAxisTarget (hud_edit_snap_axis_t *axis, float start, float size)
{
	if (!isfinite (start) || !isfinite (size) || size < 0)
		return;
	{
		int target;
		for (target = 0; target < 3; target++)
		{
			float guide = start + size * target * 0.5f;
			if (guide < 0 || guide > axis->extent)
				continue;
			{
				int own;
				for (own = 0; own < 3; own++)
					HudEdit_SnapAxisCandidate (axis, guide - axis->size * own * 0.5f, guide);
			}
		}
	}
}

static inline float HudEdit_SnapAxisResult (const hud_edit_snap_axis_t *axis, float *guide)
{
	if (guide)
		*guide = axis->guide;
	return axis->result;
}
