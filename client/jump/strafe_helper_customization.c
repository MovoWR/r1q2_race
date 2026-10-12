#include "strafe_helper_customization.h"
#include "strafe_helper.h"
#include "sh_efficiency_draw.h"
#include "sh_draw_math.h"
#include <assert.h>
#include <float.h>
#include <math.h>

#include <stdio.h>

#define SHC_GRADIENT_MAX_SEGMENTS 64
#define SHC_GRADIENT_TARGET_SEGMENT_WIDTH 6.0f
#define SHC_GRADIENT_PEAK_FRACTION 0.70f

static qboolean shc_IsFinite (const float value)
{
	return value == value && value > -FLT_MAX && value < FLT_MAX;
}

static int shc_ClampInt (const int value, const int min_value, const int max_value)
{
	if (value < min_value)
	{
		return min_value;
	}
	if (value > max_value)
	{
		return max_value;
	}
	return value;
}

static float shc_ClampFloat (const float value, const float min_value, const float max_value)
{
	if (value < min_value)
	{
		return min_value;
	}
	if (value > max_value)
	{
		return max_value;
	}
	return value;
}

static byte shc_LerpByte (const byte from, const byte to, const float fraction)
{
	return (byte)roundf ((float)from + ((float)to - (float)from) * fraction);
}

static uint32 shc_LerpColor (const uint32 from, const uint32 to, const float fraction)
{
	int i;
	color_t from_color, to_color, out_color;
	const float clamped_fraction = shc_ClampFloat (fraction, 0.0f, 1.0f);

	from_color.u32 = from;
	to_color.u32 = to;

	for (i = 0; i < 4; i++)
	{
		out_color.u8[i] = shc_LerpByte (from_color.u8[i], to_color.u8[i], clamped_fraction);
	}
	return out_color.u32;
}

static float shc_HelperAlphaMultiplier (void)
{
	float multiplier = 1.0f;
	if (cl_strafehelperAlpha)
	{
		multiplier = sh_drawing_preview || HUD_EditorPreview () ? SH_ClampDrawValue (HUD_EditorValue (cl_strafehelperAlpha), 0.0f, 1.0f) : Cvar_ClampValue (cl_strafehelperAlpha, 0.0f, 1.0f);
	}

	if (!sh_drawing_preview)
	{
		const float move =
			fabsf (cl_jump_sample.localmove[0]) + fabsf (cl_jump_sample.localmove[1]) + fabsf (cl_jump_sample.localmove[2]);
		if (move < 0.1f || cl.frame.playerstate.pmove.pm_type != PM_NORMAL)
		{
			multiplier *= 0.25f;
		}
	}

	return multiplier;
}

uint32 shc_ApplyHelperAlpha (const uint32 color)
{
	color_t out_color;
	out_color.u32 = color;
	out_color.u8[3] = (byte)roundf ((float)out_color.u8[3] * shc_HelperAlphaMultiplier ());
	return out_color.u32;
}

uint32 shc_ParseColorString (const char *colorStr, byte *r, byte *g, byte *b, byte *a)
{
	int ri = 255, gi = 255, bi = 255, ai = 255;

	if (colorStr)
	{
		if (sscanf (colorStr, "%d %d %d %d", &ri, &gi, &bi, &ai) < 3)
		{
			ri = gi = bi = ai = 255;
		}
	}

	byte rr = (byte)(ri < 0 ? 0 : (ri > 255 ? 255 : ri));
	byte gg = (byte)(gi < 0 ? 0 : (gi > 255 ? 255 : gi));
	byte bb = (byte)(bi < 0 ? 0 : (bi > 255 ? 255 : bi));
	byte aa = (byte)(ai < 0 ? 0 : (ai > 255 ? 255 : ai));

	if (r)
		*r = rr;
	if (g)
		*g = gg;
	if (b)
		*b = bb;
	if (a)
		*a = aa;
	return MakeColor (rr, gg, bb, aa);
}

qboolean shc_ParseColorCvar (const char *cvarValue, uint32 *outUint32,
	color_t *outColor)
{
	int ri, gi, bi, ai = 255;

	if (!cvarValue || sscanf (cvarValue, "%d %d %d %d", &ri, &gi, &bi, &ai) < 3)
	{
		return false;
	}

	byte r = (byte)(ri < 0 ? 0 : (ri > 255 ? 255 : ri));
	byte g = (byte)(gi < 0 ? 0 : (gi > 255 ? 255 : gi));
	byte b = (byte)(bi < 0 ? 0 : (bi > 255 ? 255 : bi));
	byte a = (byte)(ai < 0 ? 0 : (ai > 255 ? 255 : ai));

	if (outUint32)
	{
		*outUint32 = (r << 24) | (g << 16) | (b << 8) | a;
	}

	if (outColor)
	{
		outColor->u8[0] = r;
		outColor->u8[1] = g;
		outColor->u8[2] = b;
		outColor->u8[3] = a;
	}

	return true;
}

uint32 getColorForElement (const enum shc_ElementId element_id)
{
	const char *colorString = NULL;

	switch (element_id)
	{
	case shc_ElementId_AcceleratingAngles:
		colorString = cl_strafehelper_color_accelerating->string;
		break;
	case shc_ElementId_OptimalAngle:
		colorString = cl_strafehelper_color_optimal->string;
		break;
	case shc_ElementId_CenterMarker:
		colorString = cl_strafehelper_color_centermarker->string;
		break;
	case shc_ElementId_NerdStats:
		colorString = cl_strafehelper_color_nerdstats->string;
		break;
	default:;
	}

	const uint32 base = shc_ParseColorString (colorString, NULL, NULL, NULL, NULL);
	return shc_ApplyHelperAlpha (SH_Efficiency_ApplyTint (element_id, base));
}

void shc_drawFilledRectangle (const float x, const float y,
	const float w, const float h,
	enum shc_ElementId element_id)
{
	const uint32 color = getColorForElement (element_id);
	R_DrawFill32 (roundf (x), roundf (y), roundf (w), roundf (h), color);
}

void shc_drawGradientRectangle (float x, const float y,
	float w, const float h, float peak_x,
	const float gradient_start, const float gradient_end,
	enum shc_ElementId edge_element_id,
	enum shc_ElementId peak_element_id)
{
	int i;
	if (!shc_IsFinite (x) || !shc_IsFinite (y) || !shc_IsFinite (w) ||
		!shc_IsFinite (h) || !shc_IsFinite (peak_x) ||
		!shc_IsFinite (gradient_start) || !shc_IsFinite (gradient_end))
	{
		return;
	}
	if (w == 0.0f || h == 0.0f)
	{
		return;
	}

	if (w < 0.0f)
	{
		x += w;
		w = -w;
	}

	const int ix = (int)roundf (x);
	const int iy = (int)roundf (y);
	const int iw = (int)roundf (w);
	const int ih = (int)roundf (h);
	if (iw <= 0 || ih <= 0)
	{
		return;
	}

	const uint32 edge_color = getColorForElement (edge_element_id);
	const uint32 peak_color = getColorForElement (peak_element_id);
	const int segment_count = shc_ClampInt ((int)ceilf ((float)iw / SHC_GRADIENT_TARGET_SEGMENT_WIDTH),
		1, min (iw, SHC_GRADIENT_MAX_SEGMENTS));
	// Color follows the full band even when only one side of an angular wrap is visible.
	const float start_x = roundf (gradient_start);
	const float end_x = start_x + roundf (gradient_end - gradient_start);
	const float clamped_peak_x = shc_ClampFloat (peak_x, start_x, end_x);

	for (i = 0; i < segment_count; i++)
	{
		const int segment_x = ix + iw * i / segment_count;
		const int segment_end_x = ix + iw * (i + 1) / segment_count;
		const int segment_width = segment_end_x - segment_x;
		if (segment_width <= 0)
		{
			continue;
		}

		const float midpoint_x = (float)segment_x + (float)segment_width * 0.5f;
		float quality;
		if (midpoint_x <= clamped_peak_x)
		{
			const float distance_to_peak = clamped_peak_x - start_x;
			quality = distance_to_peak > 0.0f ? (midpoint_x - start_x) / distance_to_peak : 1.0f;
		}
		else
		{
			const float distance_to_peak = end_x - clamped_peak_x;
			quality = distance_to_peak > 0.0f ? (end_x - midpoint_x) / distance_to_peak : 1.0f;
		}

		R_DrawFill32 (segment_x, iy, segment_width, ih,
			shc_LerpColor (edge_color, peak_color,
				quality * SHC_GRADIENT_PEAK_FRACTION));
	}
}
