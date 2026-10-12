#pragma once
#include "jump_compat.h"

typedef struct
{
	float gain;
	float maximum_gain;
	float efficiency; // Current gain divided by maximum gain, clamped to [0, 1].
	qboolean valid;
} StrafeEfficiency;

typedef struct
{
	unsigned char r, g, b, a;
} StrafeEfficiencyColor;

// target is the acceleration addspeed cap. budget is the maximum acceleration
// this command can apply (accel * frame time * unclamped wishspeed).
StrafeEfficiency StrafeEfficiency_Calculate (const float velocity[2],
	const float wishdir[2],
	float target,
	float budget);

// Piecewise-linear bad->mid->good gradient over value in [0, 1]. midpoint is
// the value at which the mid color sits, clamped to [0.05, 0.95].
StrafeEfficiencyColor StrafeEfficiency_MapColor (float value,
	float midpoint,
	StrafeEfficiencyColor bad,
	StrafeEfficiencyColor mid,
	StrafeEfficiencyColor good);

// One exponential smoothing step from previous toward target. smoothing uses
// the same 0-10 scale as sh_smoothing (0 = snap), frametime is in seconds.
// With smoothing enabled, zero elapsed time preserves the previous value.
float StrafeEfficiency_SmoothStep (float previous, float target,
	float smoothing, float frametime);

// Blends base RGB toward tint RGB by strength in [0, 1], preserving the base
// alpha so translucent HUD elements keep their translucency when tinted.
StrafeEfficiencyColor StrafeEfficiency_BlendColor (StrafeEfficiencyColor base,
	StrafeEfficiencyColor tint,
	float strength);
