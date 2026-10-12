// Adapted from q2re-jump's GPLv2 src/jump/jump_logic.cpp gain model.
#include "strafe_efficiency.h"

#include <math.h>

#define STRAFE_EFFICIENCY_MIN_GAIN 0.0001

static double StrafeEfficiency_Clamp (const double value,
	const double minimum,
	const double maximum)
{
	if (value < minimum)
	{
		return minimum;
	}
	if (value > maximum)
	{
		return maximum;
	}
	return value;
}

static double StrafeEfficiency_Gain (const double speed,
	const double projection,
	const double target,
	const double budget)
{
	const double acceleration = StrafeEfficiency_Clamp (target - projection,
		0.0, budget);
	const double delta = 2.0 * acceleration * projection + acceleration * acceleration;
	// Near a full stop, roundoff can put squared speed just below zero.
	const double radicand = fmax (speed * speed + delta, 0.0);

	const double denominator = sqrt (radicand) + speed;
	if (denominator <= 0.0)
	{
		return 0.0;
	}

	// Equivalent to sqrt(speed * speed + delta) - speed, without
	// cancellation at typical jump speeds.
	return delta / denominator;
}

StrafeEfficiency StrafeEfficiency_Calculate (const float velocity[2],
	const float wishdir[2],
	const float target,
	const float budget)
{
	StrafeEfficiency result = { 0 };

	if (!velocity || !wishdir || !isfinite (velocity[0]) || !isfinite (velocity[1]) || !isfinite (wishdir[0]) || !isfinite (wishdir[1]) || !isfinite (target) || !isfinite (budget) || target <= 0.0f || budget <= 0.0f)
	{
		return result;
	}

	const double velocity_x = velocity[0];
	const double velocity_y = velocity[1];
	const double wish_x = wishdir[0];
	const double wish_y = wishdir[1];
	const double wish_length = sqrt (wish_x * wish_x + wish_y * wish_y);

	if (wish_length <= STRAFE_EFFICIENCY_MIN_GAIN)
	{
		return result;
	}

	const double speed = sqrt (velocity_x * velocity_x + velocity_y * velocity_y);
	const double projection = (velocity_x * wish_x + velocity_y * wish_y) / wish_length;
	const double optimal_projection = StrafeEfficiency_Clamp (
		(double)target - (double)budget, 0.0, speed);
	const double gain = StrafeEfficiency_Gain (speed, projection,
		target, budget);
	const double maximum_gain = StrafeEfficiency_Gain (
		speed, optimal_projection, target, budget);

	if (maximum_gain <= STRAFE_EFFICIENCY_MIN_GAIN)
	{
		return result;
	}

	result.gain = (float)gain;
	result.maximum_gain = (float)maximum_gain;
	result.efficiency = (float)StrafeEfficiency_Clamp (
		gain > 0.0 ? gain / maximum_gain : 0.0, 0.0, 1.0);
	result.valid = true;
	return result;
}

static unsigned char StrafeEfficiency_LerpByte (const unsigned char from,
	const unsigned char to,
	const double fraction)
{
	const double value = (double)from + ((double)to - (double)from) * fraction;
	return (unsigned char)StrafeEfficiency_Clamp (value + 0.5, 0.0, 255.0);
}

static StrafeEfficiencyColor StrafeEfficiency_LerpColor (
	const StrafeEfficiencyColor from,
	const StrafeEfficiencyColor to,
	const double fraction)
{
	const double t = StrafeEfficiency_Clamp (fraction, 0.0, 1.0);
	const StrafeEfficiencyColor result = {
		StrafeEfficiency_LerpByte (from.r, to.r, t),
		StrafeEfficiency_LerpByte (from.g, to.g, t),
		StrafeEfficiency_LerpByte (from.b, to.b, t),
		StrafeEfficiency_LerpByte (from.a, to.a, t),
	};
	return result;
}

StrafeEfficiencyColor StrafeEfficiency_MapColor (const float value,
	const float midpoint,
	const StrafeEfficiencyColor bad,
	const StrafeEfficiencyColor mid,
	const StrafeEfficiencyColor good)
{
	const double clamped_value = isfinite (value) ? StrafeEfficiency_Clamp (value, 0.0, 1.0) : 0.0;
	const double clamped_midpoint = isfinite (midpoint) ? StrafeEfficiency_Clamp (midpoint, 0.05, 0.95) : 0.5;

	if (clamped_value <= clamped_midpoint)
	{
		return StrafeEfficiency_LerpColor (bad, mid,
			clamped_value / clamped_midpoint);
	}
	return StrafeEfficiency_LerpColor (mid, good,
		(clamped_value - clamped_midpoint) / (1.0 - clamped_midpoint));
}

StrafeEfficiencyColor StrafeEfficiency_BlendColor (const StrafeEfficiencyColor base,
	const StrafeEfficiencyColor tint,
	const float strength)
{
	const double t = isfinite (strength) ? StrafeEfficiency_Clamp (strength, 0.0, 1.0) : 0.0;
	const StrafeEfficiencyColor result = {
		StrafeEfficiency_LerpByte (base.r, tint.r, t),
		StrafeEfficiency_LerpByte (base.g, tint.g, t),
		StrafeEfficiency_LerpByte (base.b, tint.b, t),
		base.a,
	};
	return result;
}

float StrafeEfficiency_SmoothStep (const float previous, const float target,
	const float smoothing, const float frametime)
{
	if (!isfinite (previous) || !isfinite (target))
	{
		return target;
	}
	if (!isfinite (smoothing) || !isfinite (frametime) || smoothing <= 0.0f || frametime < 0.0f)
	{
		return target;
	}
	if (frametime == 0.0f)
	{
		return previous;
	}

	// Same time constant scale as the helper bar's sh_smoothing.
	const double tau = 0.05 * StrafeEfficiency_Clamp (smoothing, 0.0, 10.0);
	const double factor = 1.0 - exp (-(double)frametime / tau);

	return (float)(previous + factor * ((double)target - previous));
}
