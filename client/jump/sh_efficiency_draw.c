/*
 * Efficiency display for the strafe helper HUD.
 *
 * Renders the airborne acceleration-efficiency value (0-100%) computed by
 * strafe_efficiency.c as a bar and/or text readout below the helper bar.
 * All appearance is driven by the sh_efficiency_* cvars; the color/smoothing
 * policy lives in strafe_efficiency.c so it stays unit-testable.
 */

#include "sh_efficiency_draw.h"
#include "strafe_helper.h"
#include "strafe_helper_customization.h"
#include "strafe_efficiency.h"
#include "sh_draw_math.h"
#include <math.h>

// Displayed value state: smoothing accumulator plus a short hold so the bar
// does not flicker when pmove data skips a render frame.
static float eff_displayed;
static qboolean eff_displayed_valid;
static float eff_held_value;
static qboolean eff_have_held;
static unsigned eff_last_valid_ms;
static unsigned eff_last_update_ms;

typedef enum
{
	SH_EfficiencyStyle_Bar,
	SH_EfficiencyStyle_Text,
	SH_EfficiencyStyle_Both,
	SH_EfficiencyStyle_None
} SH_EfficiencyStyle;

static qboolean SH_Efficiency_StringEquals (const char *value, const char *expected)
{
	return value && !Q_stricmp (value, expected);
}

// Fixed limits may normalize live configuration; previews only read their values.
static float SH_Efficiency_ClampCvar (cvar_t *var, float low, float high)
{
	if (sh_drawing_preview || HUD_EditorPreview ())
		return SH_ClampDrawValue (HUD_EditorValue (var), low, high);
	return Cvar_ClampValue (var, low, high);
}

static SH_EfficiencyStyle SH_Efficiency_GetStyle (void)
{
	const char *style = cl_strafehelperEffStyle ? cl_strafehelperEffStyle->string : "bar";

	if (SH_Efficiency_StringEquals (style, "text") || SH_Efficiency_StringEquals (style, "2"))
	{
		return SH_EfficiencyStyle_Text;
	}
	if (SH_Efficiency_StringEquals (style, "both") || SH_Efficiency_StringEquals (style, "3"))
	{
		return SH_EfficiencyStyle_Both;
	}
	if (SH_Efficiency_StringEquals (style, "none") || SH_Efficiency_StringEquals (style, "0"))
	{
		return SH_EfficiencyStyle_None;
	}
	return SH_EfficiencyStyle_Bar;
}

static StrafeEfficiencyColor SH_Efficiency_ParseColor (cvar_t *color_cvar)
{
	StrafeEfficiencyColor color = { 255, 255, 255, 255 };
	if (color_cvar)
	{
		shc_ParseColorString (color_cvar->string,
			&color.r, &color.g, &color.b, &color.a);
	}
	return color;
}

static StrafeEfficiencyColor SH_Efficiency_FillColorBytes (const float value)
{
	const StrafeEfficiencyColor good =
		SH_Efficiency_ParseColor (cl_strafehelperEffColorGood);

	if (SH_Efficiency_StringEquals (cl_strafehelperEffColorMode ? cl_strafehelperEffColorMode->string : "dynamic",
			"static"))
	{
		return good;
	}

	return StrafeEfficiency_MapColor (
		value,
		cl_strafehelperEffColorMidpoint ? cl_strafehelperEffColorMidpoint->value : 0.5f,
		SH_Efficiency_ParseColor (cl_strafehelperEffColorBad),
		SH_Efficiency_ParseColor (cl_strafehelperEffColorMid),
		good);
}

static uint32 SH_Efficiency_FillColor (const float value)
{
	const StrafeEfficiencyColor color = SH_Efficiency_FillColorBytes (value);
	return MakeColor (color.r, color.g, color.b, color.a);
}

static unsigned SH_Efficiency_HoldMs (void)
{
	return cl_strafehelperEffHoldMs ? (unsigned)SH_ClampDrawValue (cl_strafehelperEffHoldMs->value,
										  SH_EFFICIENCY_HOLD_MIN, SH_EFFICIENCY_HOLD_MAX) :
									  0;
}

// True while a value is live: fresh data this frame, or still inside the hold
// window. Unlike SH_Efficiency_UpdateValue this never mutates state, so the
// tint path can query it even on frames where the display is not drawn.
static qboolean SH_Efficiency_HasDisplayValue (void)
{
	return eff_have_held && cls.realtime - eff_last_valid_ms <= SH_Efficiency_HoldMs ();
}

// Menu previews share their value with element tint without changing live state.
static float SH_Efficiency_PreviewValue (void)
{
	return HUD_EditorPreview () ? 0.78f : 0.65f + 0.35f * sinf ((float)cls.realtime * 0.0015f);
}

uint32 SH_Efficiency_ApplyTint (const enum shc_ElementId element_id,
	const uint32 base)
{
	if (!cl_strafehelperEfficiency || !cl_strafehelperEfficiency->intvalue)
	{
		return base;
	}

	const char *mode = cl_strafehelperEffTint ? cl_strafehelperEffTint->string : "off";
	const qboolean tint_optimal = SH_Efficiency_StringEquals (mode, "optimal") || SH_Efficiency_StringEquals (mode, "both");
	const qboolean tint_zone = SH_Efficiency_StringEquals (mode, "zone") || SH_Efficiency_StringEquals (mode, "both");

	if (!((element_id == shc_ElementId_OptimalAngle && tint_optimal) || (element_id == shc_ElementId_AcceleratingAngles && tint_zone)))
	{
		return base;
	}

	const qboolean preview = sh_drawing_preview || HUD_EditorPreview ();
	if (!preview && !SH_Efficiency_HasDisplayValue ())
	{
		return base;
	}

	color_t base_color;
	base_color.u32 = base;
	const StrafeEfficiencyColor blended = StrafeEfficiency_BlendColor (
		(StrafeEfficiencyColor){ base_color.u8[0], base_color.u8[1],
			base_color.u8[2], base_color.u8[3] },
		SH_Efficiency_FillColorBytes (preview ? SH_Efficiency_PreviewValue () : eff_displayed),
		cl_strafehelperEffTintStrength ? SH_ClampDrawValue (cl_strafehelperEffTintStrength->value, 0.0f, 1.0f) : 0.75f);
	return MakeColor (blended.r, blended.g, blended.b, blended.a);
}

static void SH_Efficiency_FillRect (const float x, const float y,
	const float width, const float height,
	const uint32 color)
{
	if (!isfinite (x) || !isfinite (y) || !isfinite (width) || !isfinite (height) ||
		width <= 0.0f || height <= 0.0f)
	{
		return;
	}

	R_DrawFill32 (roundf (x), roundf (y), roundf (width), roundf (height),
		shc_ApplyHelperAlpha (color));
}

// Advances the smoothing/hold state. Returns true when a value should be
// shown and stores it in *out_value.
static qboolean SH_Efficiency_UpdateValue (const qboolean valid, const float value,
	float *out_value)
{
	float target;

	if (valid)
	{
		eff_held_value = CLAMP (value, 0.0f, 1.0f);
		eff_have_held = true;
		eff_last_valid_ms = cls.realtime;
		target = eff_held_value;
	}
	else
	{
		if (!SH_Efficiency_HasDisplayValue ())
		{
			eff_have_held = false;
			eff_displayed_valid = false;
			eff_last_update_ms = 0;
			return false;
		}
		target = eff_held_value;
	}

	if (eff_displayed_valid)
	{
		const float smoothing = cl_strafehelperEffSmoothing ? Cvar_ClampValue (cl_strafehelperEffSmoothing, 0.0f, 10.0f) : 0.0f;
		eff_displayed = StrafeEfficiency_SmoothStep (eff_displayed, target,
			smoothing,
			(cls.realtime - eff_last_update_ms) * 0.001f);
	}
	else
	{
		eff_displayed = target;
		eff_displayed_valid = true;
	}

	eff_last_update_ms = cls.realtime;
	*out_value = eff_displayed;
	return true;
}

// Called by the live HUD pass, independently of display style or draw order.
void SH_Efficiency_Update (const qboolean valid, const float value)
{
	if (sh_drawing_preview || HUD_EditorPreview () ||
		!cl_strafehelperEfficiency || !cl_strafehelperEfficiency->intvalue)
	{
		return;
	}

	if (cl_strafehelperEffHoldMs)
		Cvar_ClampValue (cl_strafehelperEffHoldMs, SH_EFFICIENCY_HOLD_MIN, SH_EFFICIENCY_HOLD_MAX);
	if (cl_strafehelperEffTintStrength)
		Cvar_ClampValue (cl_strafehelperEffTintStrength, 0.0f, 1.0f);

	float displayed;
	SH_Efficiency_UpdateValue (valid && isfinite (value), value, &displayed);
}

static void SH_Efficiency_DrawText (const float center_x, const float top_y,
	const float hud_scale, const int font_pic,
	const float value)
{
	char buffer[8];
	const float text_scale = cl_strafehelperEffTextScale ? SH_Efficiency_ClampCvar (cl_strafehelperEffTextScale,
															   SH_EFFICIENCY_TEXT_SCALE_MIN,
															   SH_EFFICIENCY_TEXT_SCALE_MAX) :
														   1.0f;
	const float draw_scale = (hud_scale > 0.0f ? hud_scale : 1.0f) / text_scale;

	Q_scnprintf (buffer, sizeof (buffer), "%d%%", Q_rint (value * 100.0f));

	const int text_y = Q_rint (top_y / text_scale);
	R_SetColor (shc_ApplyHelperAlpha (SH_Efficiency_FillColor (value)));
	R_SetScale (draw_scale);
	SCR_DrawStringEx (Q_rint (center_x / text_scale),
		text_y,
		UI_CENTER | UI_DROPSHADOW, sizeof (buffer),
		buffer, font_pic);
	R_SetScale (hud_scale);
	R_ClearColor ();
}

void SH_Efficiency_Draw (const float helper_upper_y, const float helper_height,
	const float hud_width, const float hud_scale,
	const int font_pic)
{
	if (!HUD_EditorPreview () && (!cl_strafehelperEfficiency || !cl_strafehelperEfficiency->intvalue))
	{
		return;
	}

	const SH_EfficiencyStyle style = SH_Efficiency_GetStyle ();
	const float bar_width = cl_strafehelperEffWidth ? SH_ClampDrawValue (SH_Efficiency_ClampCvar (cl_strafehelperEffWidth,
																			 SH_EFFICIENCY_WIDTH_MIN, SH_EFFICIENCY_WIDTH_MAX),
														  SH_EFFICIENCY_WIDTH_MIN, hud_width) :
													  80.0f;
	const float bar_height = cl_strafehelperEffHeight ? SH_Efficiency_ClampCvar (cl_strafehelperEffHeight,
															SH_EFFICIENCY_HEIGHT_MIN, SH_EFFICIENCY_HEIGHT_MAX) :
														4.0f;
	const float x_offset = cl_strafehelperEffX ? SH_ClampDrawValue (SH_Efficiency_ClampCvar (cl_strafehelperEffX,
																		SH_EFFICIENCY_X_MIN, SH_EFFICIENCY_X_MAX),
													 -hud_width * 0.5f, hud_width * 0.5f) :
												 0.0f;
	const float y_offset = cl_strafehelperEffY ? SH_Efficiency_ClampCvar (cl_strafehelperEffY, SH_EFFICIENCY_Y_MIN, SH_EFFICIENCY_Y_MAX) : 3.0f;

	const float bar_x = (hud_width - bar_width) * 0.5f + x_offset;
	// Non-negative offsets sit below the helper bar, negative ones above it.
	const float bar_y = y_offset >= 0.0f ? helper_upper_y + helper_height + y_offset : helper_upper_y + y_offset - bar_height;

	const float visual_scale = SH_VisualScale (hud_efficiency_scale);
	SH_BeginVisualDraw (HUD_EDIT_EFFICIENCY, visual_scale, visual_scale,
		hud_width * 0.5f + x_offset,
		y_offset < 0.0f ? bar_y + bar_height : bar_y, hud_scale);
	const qboolean preview = sh_drawing_preview || HUD_EditorPreview ();
	const float displayed = preview ? SH_Efficiency_PreviewValue () : eff_displayed;
	const qboolean show_value = preview || (eff_displayed_valid && SH_Efficiency_HasDisplayValue ());

	if (style == SH_EfficiencyStyle_Bar || style == SH_EfficiencyStyle_Both)
	{
		if (cl_strafehelperEffBorder && cl_strafehelperEffBorder->intvalue)
		{
			SH_Efficiency_FillRect (bar_x - 1.0f, bar_y - 1.0f,
				bar_width + 2.0f, bar_height + 2.0f,
				MakeColor (0, 0, 0, 255));
		}

		const StrafeEfficiencyColor bg =
			SH_Efficiency_ParseColor (cl_strafehelperEffColorBg);
		SH_Efficiency_FillRect (bar_x, bar_y, bar_width, bar_height,
			MakeColor (bg.r, bg.g, bg.b, bg.a));

		if (show_value)
		{
			SH_Efficiency_FillRect (bar_x, bar_y, bar_width * displayed,
				bar_height,
				SH_Efficiency_FillColor (displayed));
		}

		const float marker = cl_strafehelperEffMarker ? SH_Efficiency_ClampCvar (cl_strafehelperEffMarker, 0.0f, 1.0f) : 0.0f;
		if (marker > 0.0f)
		{
			SH_Efficiency_FillRect (bar_x + bar_width * marker, bar_y,
				1.0f, bar_height,
				MakeColor (200, 200, 200, 160));
		}
	}

	if ((style == SH_EfficiencyStyle_Text || style == SH_EfficiencyStyle_Both) && show_value)
	{
		const float text_y = style == SH_EfficiencyStyle_Both ? bar_y + bar_height + 3.0f : bar_y;
		SH_Efficiency_DrawText (bar_x + bar_width * 0.5f, text_y,
			hud_scale, font_pic, displayed);
	}
	SH_EndVisualDraw (HUD_EDIT_EFFICIENCY, hud_scale);
}

void SH_Efficiency_DrawPreview (const float helper_upper_y,
	const float helper_height,
	const float hud_width, const float hud_scale,
	const int font_pic)
{
	const qboolean previous_preview = sh_drawing_preview;
	sh_drawing_preview = true;
	SH_Efficiency_Draw (helper_upper_y, helper_height, hud_width, hud_scale,
		font_pic);
	sh_drawing_preview = previous_preview;
}

void SH_Efficiency_Clear (void)
{
	eff_displayed = eff_held_value = 0;
	eff_displayed_valid = eff_have_held = false;
	eff_last_valid_ms = eff_last_update_ms = 0;
}
