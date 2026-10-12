/* GPL-2.0-or-later; frozen Q2PRO Race Jump command algorithms. */
#include "jump_menu.h"
#include "jump_sh_commands.h"
#include "strafe_helper.h"
#include "strafe_helper_customization.h"

/* sh_hud_menu.c */


typedef struct
{
	const char *name;
	const char *color_optimal;
	const char *color_accelerating;
	const char *color_centermarker;
	const char *description;
} ColorPreset;

static const ColorPreset presets[] = {
	{ "Vanilla", "0 255 64 192", "0 128 32 96", "255 255 255 192", "Classic" },
	{ "Gold", "255 215 0 255", "184 134 11 90", "0 0 255 255", "Rich gold with deep blue contrasts" },
	{ "Rainbow", "255 0 0 255", "0 255 0 80", "0 0 255 255", "Vivid RGB rainbow colors" },
	{ "FeverDream", "255 0 255 255", "0 255 255 80", "255 255 0 255", "Magenta, cyan, and yellow overload" },
	{ "Nightmare", "255 0 128 255", "128 0 255 90", "0 0 0 255", "Bold magenta and purple on black" },
	{ "Ocean", "0 128 255 255", "0 64 128 80", "128 192 255 255", "Ocean blue with soft highlights" },
	{ "Retro", "0 255 128 255", "255 128 0 80", "255 0 128 255", "Neon green, orange, and pink" },
	{ "Amber", "255 128 64 255", "255 64 128 90", "255 255 192 255", "Warm amber tones with subtle highlights" },
	{ "SkyBlue", "135 206 235 255", "70 130 180 100", "255 0 128 255", "Cool sky blue with contrasting pink highlights" },
	{ "Candy", "255 182 193 255", "255 105 180 90", "255 255 0 255", "Bright pinks with yellow" },
	{ "Cyberpunk", "255 20 147 255", "0 255 255 80", "0 0 255 255", "Bright pink and cyan for a futuristic feel" },
	{ "Party", "255 255 0 255", "0 255 255 80", "255 0 255 255", "Yellow, cyan, and magenta" },
	{ "Aussie", "255 215 0 255", "0 128 0 128", "255 255 255 255", "Golden wattle yellow, green, and white" },
	{ "Pepega", "0 255 0 255", "128 128 128 128", "255 0 255 255", "Bright green with muted gray and magenta" },
	{ "Toxic", "0 255 0 255", "255 0 255 80", "0 0 0 255", "Toxic green with magenta and black" },
	{ "Pixelated", "0 0 0 255", "255 255 255 90", "0 255 255 255", "Black, white, and cyan blocks" },
	{ "Forest", "0 128 0 255", "0 64 0 80", "128 192 0 255", "Earthy green tones" },
	{ "Radioactive", "0 255 0 255", "0 128 0 90", "255 0 255 255", "Neon green, dark green, and magenta" },
	{ "Coral", "255 127 80 255", "255 69 0 80", "0 0 255 255", "Warm coral tones with vivid blue accents" },
	{ "Funky", "255 0 255 255", "0 255 0 80", "255 165 0 255", "Magenta, lime green, and orange" },
	{ "Electric", "0 0 255 255", "255 255 0 80", "255 0 255 255", "Blue, yellow, and magenta sparks" },
	{ "Hudslut", "255 0 0 255", "0 255 0 255", "0 0 255 255", "Bold red, green, and blue for maximum contrast" },
	{ "Sand", "194 178 128 255", "153 138 102 90", "255 244 224 255", "Muted sandy tones with soft highlights" },
	{ "Fire", "255 69 0 255", "128 32 0 90", "255 255 0 255", "Fiery red and orange with a yellow highlight" },
	{ "Mint", "152 255 152 255", "0 204 153 80", "255 0 0 255", "Refreshing mint green with bold red highlights" },
	{ "Karen", "255 0 255 255", "255 255 255 128", "0 0 0 255", "Magenta, bright white, and black" },
	{ "Nuclear", "0 255 0 255", "255 255 0 90", "255 0 0 255", "Toxic green, yellow, and red explosion" },
	{ "Highlighter", "255 255 0 255", "255 0 255 90", "0 255 255 255", "Neon yellow, magenta, and cyan" },
	{ "Cursed", "0 0 0 255", "255 0 255 255", "255 255 255 255", "Dark black, spooky magenta, and haunting white" },
	{ "Zebra", "255 255 255 255", "0 0 0 80", "128 128 128 255", "Black, white, and gray" },
	{ "Ice", "0 255 255 255", "0 128 255 80", "255 255 255 255", "Cool icy cyan and white" },
	{ "Imposs", "0 0 0 0", "0 0 0 0", "0 0 0 0", "?" },
	{ "Clown", "255 0 0 255", "0 0 255 80", "255 255 0 255", "Red, blue, and yellow for a playful feel" },
	{ "Acid", "102 255 0 255", "255 255 0 90", "0 255 128 255", "Acidic green and yellow with teal" },
	{ "Cherry", "255 0 0 255", "128 0 0 80", "255 128 128 255", "Deep cherry red with soft pink accents" },
	{ "Venom", "0 0 0 255", "0 255 0 80", "255 0 0 255", "Black, neon green, and blood red" },
	{ "Psycho", "255 0 0 255", "255 255 0 90", "0 0 255 255", "Red, yellow, and blue for mind-bending contrast" },
	{ "Doge", "255 223 0 255", "0 191 255 255", "255 0 128 255", "Golden yellow, sky blue, and hot pink for much wow" },
	{ "Lava", "255 69 0 255", "255 140 0 80", "255 0 255 255", "Fiery red and orange with magenta" },
	{ "ToxicSunset", "255 128 0 255", "255 0 128 90", "0 255 0 255", "Orange, pink, and green madness" },
	{ "Shadow", "64 64 64 255", "32 32 32 80", "192 192 192 255", "Neutral gray with shadowy tones" },
	{ "Lavender", "230 230 250 255", "216 191 216 90", "0 255 0 255", "Soft lavender tones with bright green" },
	{ "Emerald", "80 200 120 255", "40 100 60 80", "200 255 200 255", "Lush emerald green with light highlights" },
};

bool SH_GetPresetColors (const char *name, const char **accelerating,
	const char **optimal, const char **centermarker)
{
	if (!name || !*name)
	{
		return false;
	}

	{
		int i;
		for (i = 0; i < q_countof (presets); i++)
		{
			if (!Q_stricmp (name, presets[i].name))
			{
				if (accelerating)
				{
					*accelerating = presets[i].color_accelerating;
				}
				if (optimal)
				{
					*optimal = presets[i].color_optimal;
				}
				if (centermarker)
				{
					*centermarker = presets[i].color_centermarker;
				}
				return true;
			}
		}
	}

	return false;
}

void SH_SetPreset_f (void)
{
	if (Cmd_Argc () < 4)
	{
		Com_Printf ("Available presets:\n");
		Com_Printf ("===============================================================================\n");
		Com_Printf ("| %-5s | %-15s | %-50s |\n", "Num", "Name", "Description");
		Com_Printf ("===============================================================================\n");

		{
			int i;
			for (i = 0; i < q_countof (presets); i++)
			{
				Com_Printf ("| %-5d | %-15s | %-50s |\n", i + 1, presets[i].name, presets[i].description);
				Com_Printf ("-------------------------------------------------------------------------------\n");
			}
		}

		Com_Printf ("| %-5s | %-15s | %-50s |\n", "N/A", "random", "Apply a random preset.");
		Com_Printf ("===============================================================================\n");
		Com_Printf ("Usage: 'sh hud preset <name_or_number>'\n");

		return;
	}


	const char *selectedpreset = Cmd_Argv (3);

	// Handle random preset option
	if (!Q_stricmp (selectedpreset, "random"))
	{
		int randomIndex = (int)(CL_JumpUIRandom ((uint32)q_countof (presets)));
		Cvar_Set ("sh_color_optimal", presets[randomIndex].color_optimal);
		Cvar_Set ("sh_color_accelerating", presets[randomIndex].color_accelerating);
		Cvar_Set ("sh_color_centermarker", presets[randomIndex].color_centermarker);
		Com_Printf ("Random preset applied: '%s' - %s\n", presets[randomIndex].name, presets[randomIndex].description);
		return;
	}

	// Check if input is a number
	int presetNumber = -1;
	if (sscanf (selectedpreset, "%d", &presetNumber) == 1)
	{
		if (presetNumber >= 1 && presetNumber <= q_countof (presets))
		{
			int index = presetNumber - 1; // Convert to 0-based index
			Cvar_Set ("sh_color_optimal", presets[index].color_optimal);
			Cvar_Set ("sh_color_accelerating", presets[index].color_accelerating);
			Cvar_Set ("sh_color_centermarker", presets[index].color_centermarker);
			Com_Printf ("Preset '%s' applied: %s\n", presets[index].name, presets[index].description);
			return;
		}
		else
		{
			Com_Printf ("Invalid preset number. Use 'sh hud preset' to see available options.\n");
			return;
		}
	}

	// Check if input matches a preset name
	{
		int i;
		for (i = 0; i < q_countof (presets); i++)
		{
			if (!Q_stricmp (selectedpreset, presets[i].name))
			{
				Cvar_Set ("sh_color_optimal", presets[i].color_optimal);
				Cvar_Set ("sh_color_accelerating", presets[i].color_accelerating);
				Cvar_Set ("sh_color_centermarker", presets[i].color_centermarker);
				Com_Printf ("Preset '%s' applied: %s\n", presets[i].name, presets[i].description);
				return;
			}
		}
	}

	Com_Printf ("Invalid preset name or number. Use 'sh hud preset' to see available options.\n");
}

void SH_Enable_f (void)
{
	Cvar_Set ("sh_draw", "1");
	Com_Printf ("Strafe helper enabled.\n");
}

void SH_Disable_f (void)
{
	Cvar_Set ("sh_draw", "0");
	Com_Printf ("Strafe helper disabled.\n");
}

void SH_Scale_f (void)
{
	const char *scale = Cmd_ArgsFrom (3);
	float value;
	if (Cmd_Argc () < 4) // Check if no value argument provided
	{
		Com_Printf ("- Scale: %.2f\n", cl_strafeHelperScale->value);
		return;
	}
	if (sscanf (scale, "%f", &value) == 1 && value > 0.0f)
	{
		Cvar_Set ("sh_scale", scale);
		Com_Printf ("Strafe helper scale set to: %s\n", scale);
	}
	else
	{
		Com_EPrintf ("Invalid scale value. Usage: 'sh hud scale <value>'\n");
	}
}

static void SH_SetFloatCvar (const char *cvar_name, const char *label,
	const char *usage, const float min_value,
	const float max_value)
{
	const char *arg = Cmd_Argv (3);
	char *end;

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- %s: %s\n", label, Cvar_VariableString (cvar_name));
		return;
	}

	errno = 0;
	const float value = strtof (arg, &end);
	const bool parsed = end != arg;
	while (Q_isspace (*end))
	{
		end++;
	}

	if (Cmd_Argc () == 4 && parsed && !*end && errno != ERANGE &&
		isfinite (value) && value >= min_value && value <= max_value)
	{
		Cvar_Set (cvar_name, arg);
		Com_Printf ("%s set to: %s\n", label, arg);
	}
	else
	{
		Com_Printf ("Invalid value. Usage: '%s'\n", usage);
	}
}

void SH_ypos_f (void)
{
	SH_SetFloatCvar ("sh_y", "Strafe helper Y offset from center",
		"sh hud ypos <-4000-4000>", -4000.0f, 4000.0f);
}

void SH_Height_f (void)
{
	const char *height = Cmd_ArgsFrom (3);
	int value;

	if (Cmd_Argc () < 4) // Check if no value argument provided
	{
		Com_Printf ("- Height: %d\n", cl_strafeHelperHeight->intvalue);
		return;
	}

	if (sscanf (height, "%d", &value) == 1 && value > 0)
	{
		Cvar_Set ("sh_height", height);
		Com_Printf ("Strafe helper height set to: %s\n", height);
	}
	else
	{
		Com_Printf ("Invalid height value. Usage: 'sh hud height <value>' \n");
	}
}

void SH_CenterMarker_f (void)
{
	const bool enable = !cl_strafeHelperCenterMarker->intvalue;
	Cvar_Set ("sh_centermarker", enable ? "1" : "0");
	Com_Printf ("Center marker %s.\n", enable ? "enabled" : "disabled");
}

void SH_CenterHeight_f (void)
{
	SH_SetFloatCvar ("sh_center_height", "Center marker height (0 = half bar height)",
		"sh hud center_height <0-80>", 0.0f, SH_MARKER_HEIGHT_MAX);
}

void SH_OptimalHeight_f (void)
{
	SH_SetFloatCvar ("sh_optimal_height", "Optimal marker height (0 = bar height)",
		"sh hud optimal_height <0-80>", 0.0f, SH_MARKER_HEIGHT_MAX);
}

void SH_CenterWidth_f (void)
{
	if (Cmd_Argc () < 4) // Check if no value argument provided
	{
		Com_Printf ("- Current Center Width: %f\n", cl_strafehelper_center_width->value);
		return;
	}
	const char *width = Cmd_Argv (3); // Fetch the third argument
	float value;
	if (sscanf (width, "%f", &value) == 1 && value >= 0.1f && value <= 5.0f)
	{
		Cvar_Set ("sh_center_width", width);
		Com_Printf ("Center line width set to: %s\n", width);
	}
	else
	{
		Com_Printf ("Invalid Center line width value. Usage: 'sh hud center_width <value>' (0.1-5.0)\n");
	}
}

void SH_OptimalWidth_f (void)
{
	if (Cmd_Argc () < 4) // Check if no value argument provided
	{
		Com_Printf ("- Current Optimal Width: %.2f\n", cl_strafehelper_optimal_width->value);
		return;
	}
	const char *width = Cmd_ArgsFrom (3);
	float value;
	if (sscanf (width, "%f", &value) == 1 && value > 0.0f)
	{
		Cvar_Set ("sh_optimal_width", width);
		Com_Printf ("Optimal line width set to: %s\n", width);
	}
	else
	{
		Com_Printf ("Invalid optimal line width value. Usage: 'sh hud optimal_width <value>' (0.1-5.0)\n");
	}
}

static bool SH_IsOnOffValue (const char *value)
{
	return value && (!strcmp (value, "0") || !strcmp (value, "1"));
}

static bool SH_IsColorString (const char *color)
{
	int r, g, b, a;
	return sscanf (color, "%d %d %d %d", &r, &g, &b, &a) == 4 &&
		r >= 0 && r <= 255 && g >= 0 && g <= 255 &&
		b >= 0 && b <= 255 && a >= 0 && a <= 255;
}

static void SH_SetColorCvar (const char *cvar_name, const char *label, const char *usage)
{
	const char *color = Cmd_ArgsFrom (3);

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- %s: %s\n", label, Cvar_VariableString (cvar_name));
		return;
	}

	if (SH_IsColorString (color))
	{
		Cvar_Set (cvar_name, color);
		Com_Printf ("%s set to: %s\n", label, color);
	}
	else
	{
		Com_Printf ("Wrong input. Use '%s'\n", usage);
	}
}

void SH_Alpha_f (void)
{
	const char *alpha = Cmd_Argv (3);
	float value;

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Alpha: %.2f\n", cl_strafehelperAlpha->value);
		return;
	}

	if (sscanf (alpha, "%f", &value) == 1 && value >= 0.0f && value <= 1.0f)
	{
		Cvar_Set ("sh_alpha", alpha);
		Com_Printf ("Strafe helper alpha set to: %s\n", alpha);
	}
	else
	{
		Com_Printf ("Invalid alpha value. Usage: 'sh hud alpha <0.0-1.0>'\n");
	}
}

void SH_BarStyle_f (void)
{
	const char *style = Cmd_Argv (3);

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Bar style: %s\n", cl_strafehelperBarStyle->string);
		return;
	}

	if (!Q_stricmp (style, "solid") || !Q_stricmp (style, "gradient") ||
		!Q_stricmp (style, "outline") || !Q_stricmp (style, "minimal"))
	{
		Cvar_Set ("sh_bar_style", style);
		Com_Printf ("Bar style set to: %s\n", style);
	}
	else
	{
		Com_Printf ("Invalid bar style. Usage: 'sh hud bar_style <solid|gradient|outline|minimal>'\n");
	}
}

void SH_Smoothing_f (void)
{
	const char *value = Cmd_Argv (3);
	float smoothing;

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Smoothing: %.2f\n", cl_strafehelperSmoothing->value);
		return;
	}

	if (sscanf (value, "%f", &smoothing) == 1 && smoothing >= 0.0f && smoothing <= 10.0f)
	{
		Cvar_Set ("sh_smoothing", value);
		Com_Printf ("Strafe helper smoothing set to: %s\n", value);
	}
	else
	{
		Com_Printf ("Invalid smoothing value. Usage: 'sh hud smoothing <0.0-10.0>'\n");
	}
}

void SH_SmoothingMode_f (void)
{
	const char *mode = Cmd_Argv (3);
	const char *mode_value = NULL;

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Smoothing mode: %s\n", cl_strafehelperSmoothingMode->string);
		return;
	}

	if (!Q_stricmp (mode, "linear") || !strcmp (mode, "1"))
	{
		mode_value = "1";
	}
	else if (!Q_stricmp (mode, "quadratic") || !strcmp (mode, "2"))
	{
		mode_value = "2";
	}
	else if (!Q_stricmp (mode, "cubic") || !strcmp (mode, "3"))
	{
		mode_value = "3";
	}
	else if (!Q_stricmp (mode, "sine") || !strcmp (mode, "4"))
	{
		mode_value = "4";
	}
	else if (!Q_stricmp (mode, "exponential") || !strcmp (mode, "5"))
	{
		mode_value = "5";
	}

	if (mode_value)
	{
		Cvar_Set ("sh_smoothing_mode", mode_value);
		Com_Printf ("Smoothing mode set to: %s\n", mode);
	}
	else
	{
		Com_Printf ("Invalid smoothing mode. Usage: 'sh hud smoothing_mode <linear|quadratic|cubic|sine|exponential>'\n");
	}
}

void SH_Eff_Enable_f (void)
{
	Cvar_Set ("sh_efficiency", "1");
	Com_Printf ("Efficiency display enabled.\n");
}

void SH_Eff_Disable_f (void)
{
	Cvar_Set ("sh_efficiency", "0");
	Com_Printf ("Efficiency display disabled.\n");
}

void SH_Eff_Toggle_f (void)
{
	const bool enable = !cl_strafehelperEfficiency->intvalue;
	Cvar_Set ("sh_efficiency", enable ? "1" : "0");
	Com_Printf ("Efficiency display %s.\n", enable ? "enabled" : "disabled");
}

void SH_Eff_Style_f (void)
{
	const char *style = Cmd_Argv (3);

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Efficiency style: %s\n", cl_strafehelperEffStyle->string);
		return;
	}

	if (!Q_stricmp (style, "bar") || !Q_stricmp (style, "text") ||
		!Q_stricmp (style, "both") || !Q_stricmp (style, "none"))
	{
		Cvar_Set ("sh_efficiency_style", style);
		Com_Printf ("Efficiency style set to: %s\n", style);
	}
	else
	{
		Com_Printf ("Invalid style. Usage: 'sh eff style <bar|text|both|none>'\n");
	}
}

void SH_Eff_Tint_f (void)
{
	const char *mode = Cmd_Argv (3);

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Efficiency helper tint: %s\n", cl_strafehelperEffTint->string);
		return;
	}

	if (!Q_stricmp (mode, "off") || !Q_stricmp (mode, "optimal") ||
		!Q_stricmp (mode, "zone") || !Q_stricmp (mode, "both"))
	{
		Cvar_Set ("sh_efficiency_tint", mode);
		Com_Printf ("Efficiency helper tint set to: %s\n", mode);
	}
	else
	{
		Com_Printf ("Invalid tint mode. Usage: 'sh eff tint <off|optimal|zone|both>'\n");
	}
}

void SH_Eff_TintStrength_f (void)
{
	SH_SetFloatCvar ("sh_efficiency_tint_strength", "Efficiency tint strength",
		"sh eff tint_strength <0.0-1.0>", 0.0f, 1.0f);
}

void SH_Eff_Width_f (void)
{
	SH_SetFloatCvar ("sh_efficiency_width", "Efficiency bar width",
		"sh eff width <8-4000>", SH_EFFICIENCY_WIDTH_MIN, SH_EFFICIENCY_WIDTH_MAX);
}

void SH_Eff_Height_f (void)
{
	SH_SetFloatCvar ("sh_efficiency_height", "Efficiency bar height",
		"sh eff height <1-64>", SH_EFFICIENCY_HEIGHT_MIN, SH_EFFICIENCY_HEIGHT_MAX);
}

void SH_Eff_Xpos_f (void)
{
	SH_SetFloatCvar ("sh_efficiency_x", "Efficiency X offset",
		"sh eff xpos <-4000-4000>", SH_EFFICIENCY_X_MIN, SH_EFFICIENCY_X_MAX);
}

void SH_Eff_Ypos_f (void)
{
	SH_SetFloatCvar ("sh_efficiency_y", "Efficiency Y offset",
		"sh eff ypos <-400-400>", SH_EFFICIENCY_Y_MIN, SH_EFFICIENCY_Y_MAX);
}

void SH_Eff_Border_f (void)
{
	const char *value = Cmd_Argv (3);

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Efficiency border: %d\n", cl_strafehelperEffBorder->intvalue);
		return;
	}

	if (SH_IsOnOffValue (value))
	{
		Cvar_Set ("sh_efficiency_border", value);
		Com_Printf ("Efficiency border %s.\n",
			cl_strafehelperEffBorder->intvalue ? "enabled" : "disabled");
	}
	else
	{
		Com_Printf ("Invalid value. Usage: 'sh eff border <0|1>'\n");
	}
}

void SH_Eff_Marker_f (void)
{
	SH_SetFloatCvar ("sh_efficiency_marker", "Efficiency marker",
		"sh eff marker <0.0-1.0>", 0.0f, 1.0f);
}

void SH_Eff_ColorMode_f (void)
{
	const char *mode = Cmd_Argv (3);

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Efficiency color mode: %s\n", cl_strafehelperEffColorMode->string);
		return;
	}

	if (!Q_stricmp (mode, "dynamic") || !Q_stricmp (mode, "static"))
	{
		Cvar_Set ("sh_efficiency_color_mode", mode);
		Com_Printf ("Efficiency color mode set to: %s\n", mode);
	}
	else
	{
		Com_Printf ("Invalid color mode. Usage: 'sh eff color_mode <dynamic|static>'\n");
	}
}

void SH_Eff_ColorGood_f (void)
{
	SH_SetColorCvar ("sh_efficiency_color_good", "Efficiency good color",
		"sh eff color_good R G B A");
}

void SH_Eff_ColorMid_f (void)
{
	SH_SetColorCvar ("sh_efficiency_color_mid", "Efficiency mid color",
		"sh eff color_mid R G B A");
}

void SH_Eff_ColorBad_f (void)
{
	SH_SetColorCvar ("sh_efficiency_color_bad", "Efficiency bad color",
		"sh eff color_bad R G B A");
}

void SH_Eff_ColorBg_f (void)
{
	SH_SetColorCvar ("sh_efficiency_color_bg", "Efficiency background color",
		"sh eff color_bg R G B A");
}

void SH_Eff_Midpoint_f (void)
{
	SH_SetFloatCvar ("sh_efficiency_color_midpoint", "Efficiency gradient midpoint",
		"sh eff midpoint <0.05-0.95>", 0.05f, 0.95f);
}

void SH_Eff_Smoothing_f (void)
{
	SH_SetFloatCvar ("sh_efficiency_smoothing", "Efficiency smoothing",
		"sh eff smoothing <0.0-10.0>", 0.0f, 10.0f);
}

void SH_Eff_Hold_f (void)
{
	SH_SetFloatCvar ("sh_efficiency_hold_ms", "Efficiency hold time",
		"sh eff hold <0-2000>", SH_EFFICIENCY_HOLD_MIN, SH_EFFICIENCY_HOLD_MAX);
}

void SH_Eff_TextScale_f (void)
{
	SH_SetFloatCvar ("sh_efficiency_text_scale", "Efficiency text scale",
		"sh eff text_scale <0.25-8.0>", SH_EFFICIENCY_TEXT_SCALE_MIN, SH_EFFICIENCY_TEXT_SCALE_MAX);
}

void SH_Eff_Status_f (void)
{
	Com_Printf ("- Efficiency display: %s\n",
		cl_strafehelperEfficiency->intvalue ? "enabled" : "disabled");
	Com_Printf ("- Style: %s\n", cl_strafehelperEffStyle->string);
	Com_Printf ("- Width: %.0f\n", cl_strafehelperEffWidth->value);
	Com_Printf ("- Height: %.0f\n", cl_strafehelperEffHeight->value);
	Com_Printf ("- X offset: %.0f\n", cl_strafehelperEffX->value);
	Com_Printf ("- Y offset: %.0f\n", cl_strafehelperEffY->value);
	Com_Printf ("- Border: %d\n", cl_strafehelperEffBorder->intvalue);
	Com_Printf ("- Marker: %.2f\n", cl_strafehelperEffMarker->value);
	Com_Printf ("- Helper tint: %s\n", cl_strafehelperEffTint->string);
	Com_Printf ("- Tint strength: %.2f\n", cl_strafehelperEffTintStrength->value);
	Com_Printf ("- Color mode: %s\n", cl_strafehelperEffColorMode->string);
	Com_Printf ("- Good color: %s\n", cl_strafehelperEffColorGood->string);
	Com_Printf ("- Mid color: %s\n", cl_strafehelperEffColorMid->string);
	Com_Printf ("- Bad color: %s\n", cl_strafehelperEffColorBad->string);
	Com_Printf ("- Background color: %s\n", cl_strafehelperEffColorBg->string);
	Com_Printf ("- Gradient midpoint: %.2f\n", cl_strafehelperEffColorMidpoint->value);
	Com_Printf ("- Smoothing: %.2f\n", cl_strafehelperEffSmoothing->value);
	Com_Printf ("- Hold time: %.0f ms\n", cl_strafehelperEffHoldMs->value);
	Com_Printf ("- Text scale: %.2f\n", cl_strafehelperEffTextScale->value);
}

void SH_Eff_Help_f (void)
{
	Com_Printf ("========================================================================================\n");
	Com_Printf ("Efficiency Display Menu\n");
	Com_Printf ("Usage: sh eff <command> [options]\n");
	Com_Printf ("========================================================================================\n");
	Com_Printf ("Visibility\n");
	Com_Printf ("  %-32s %s\n", "enable", "Show the airborne efficiency display.");
	Com_Printf ("  %-32s %s\n", "disable", "Hide the efficiency display.");
	Com_Printf ("  %-32s %s\n", "toggle", "Toggle the efficiency display.");
	Com_Printf ("  %-32s %s\n", "status", "Show current efficiency settings.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Layout\n");
	Com_Printf ("  %-32s %s\n", "style <bar|text|both|none>", "Bar, percentage readout, both, or neither.");
	Com_Printf ("  %-32s %s\n", "tint <off|optimal|zone|both>", "Tint strafe helper elements by efficiency.");
	Com_Printf ("  %-32s %s\n", "tint_strength <0.0-1.0>", "How strongly the tint overrides helper colors.");
	Com_Printf ("  %-32s %s\n", "width <8-4000>", "Bar width in pixels.");
	Com_Printf ("  %-32s %s\n", "height <1-64>", "Bar height in pixels.");
	Com_Printf ("  %-32s %s\n", "xpos <-4000-4000>", "Horizontal offset from center.");
	Com_Printf ("  %-32s %s\n", "ypos <-400-400>", "Gap below the helper; negative places above.");
	Com_Printf ("  %-32s %s\n", "border <0|1>", "Toggle the 1 pixel outline.");
	Com_Printf ("  %-32s %s\n", "marker <0.0-1.0>", "Threshold marker position; 0 hides it.");
	Com_Printf ("  %-32s %s\n", "text_scale <0.25-8.0>", "Percentage text scale.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Behavior\n");
	Com_Printf ("  %-32s %s\n", "smoothing <0.0-10.0>", "Smooth the displayed value; 0 disables.");
	Com_Printf ("  %-32s %s\n", "hold <0-2000>", "Keep last value this many ms after data stops.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Color\n");
	Com_Printf ("  %-32s %s\n", "color_mode <dynamic|static>", "Color by value or use one fixed color.");
	Com_Printf ("  %-32s %s\n", "color_good R G B A", "Color near 100%; also the static color.");
	Com_Printf ("  %-32s %s\n", "color_mid R G B A", "Color at the gradient midpoint.");
	Com_Printf ("  %-32s %s\n", "color_bad R G B A", "Color near 0%.");
	Com_Printf ("  %-32s %s\n", "color_bg R G B A", "Bar track background color.");
	Com_Printf ("  %-32s %s\n", "midpoint <0.05-0.95>", "Value where the mid color sits.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Examples\n");
	Com_Printf ("  sh eff enable\n");
	Com_Printf ("  sh eff style both\n");
	Com_Printf ("  sh eff smoothing 3\n");
	Com_Printf ("  sh eff color_mode static\n");
	Com_Printf ("========================================================================================\n");
}

void SH_Ups_Enable_f (void)
{
	Cvar_Set ("sh_ups", "1");
	Com_Printf ("Center UPS enabled.\n");
}

void SH_Ups_Disable_f (void)
{
	Cvar_Set ("sh_ups", "0");
	Com_Printf ("Center UPS disabled.\n");
}

void SH_Ups_Toggle_f (void)
{
	const bool enable = !cl_strafehelperUps->intvalue;
	Cvar_Set ("sh_ups", enable ? "1" : "0");
	Com_Printf ("Center UPS %s.\n", enable ? "enabled" : "disabled");
}

void SH_Ups_Status_f (void)
{
	Com_Printf ("- Center UPS: %s\n", cl_strafehelperUps->intvalue ? "enabled" : "disabled");
	Com_Printf ("- Scale: %.2f\n", cl_strafehelperUpsScale->value);
	Com_Printf ("- X offset: %.2f\n", cl_strafehelperUpsX->value);
	Com_Printf ("- Y offset: %.2f\n", cl_strafehelperUpsY->value);
	Com_Printf ("- Shadow: %d\n", cl_strafehelperUpsShadow->intvalue);
	Com_Printf ("- Hide zero: %d\n", cl_strafehelperUpsHideZero->intvalue);
	Com_Printf ("- Color mode: %s\n", cl_strafehelperUpsColorMode->string);
	Com_Printf ("- Format: %s\n", cl_strafehelperUpsFormat->string);
	Com_Printf ("- Gain color: %s\n", cl_strafehelperUpsColorGain->string);
	Com_Printf ("- Loss color: %s\n", cl_strafehelperUpsColorLoss->string);
	Com_Printf ("- Neutral color: %s\n", cl_strafehelperUpsColorNeutral->string);
}

void SH_Ups_Ypos_f (void)
{
	SH_SetFloatCvar ("sh_ups_y", "Center UPS Y pos",
		"sh ups ypos <-4000-4000>", SH_UPS_Y_MIN, SH_UPS_Y_MAX);
}

void SH_Ups_Scale_f (void)
{
	SH_SetFloatCvar ("sh_ups_scale", "Center UPS scale",
		"sh ups scale <0.25-8.0>", SH_UPS_SCALE_MIN, SH_UPS_SCALE_MAX);
}

void SH_Ups_Shadow_f (void)
{
	const char *value = Cmd_Argv (3);

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Center UPS shadow: %d\n", cl_strafehelperUpsShadow->intvalue);
		return;
	}

	if (SH_IsOnOffValue (value))
	{
		Cvar_Set ("sh_ups_shadow", value);
		Com_Printf ("Center UPS shadow %s.\n", cl_strafehelperUpsShadow->intvalue ? "enabled" : "disabled");
	}
	else
	{
		Com_Printf ("Invalid value. Usage: 'sh ups shadow <0|1>'\n");
	}
}

void SH_Ups_HideZero_f (void)
{
	const char *value = Cmd_Argv (3);

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Center UPS hide zero: %d\n", cl_strafehelperUpsHideZero->intvalue);
		return;
	}

	if (SH_IsOnOffValue (value))
	{
		Cvar_Set ("sh_ups_hide_zero", value);
		Com_Printf ("Center UPS hide zero %s.\n", cl_strafehelperUpsHideZero->intvalue ? "enabled" : "disabled");
	}
	else
	{
		Com_Printf ("Invalid value. Usage: 'sh ups hide_zero <0|1>'\n");
	}
}

void SH_Ups_ColorMode_f (void)
{
	const char *mode = Cmd_Argv (3);

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Center UPS color mode: %s\n", cl_strafehelperUpsColorMode->string);
		return;
	}

	if (!Q_stricmp (mode, "dynamic") || !Q_stricmp (mode, "static") ||
		!Q_stricmp (mode, "threshold") || !Q_stricmp (mode, "rainbow") ||
		!Q_stricmp (mode, "gradient") || !Q_stricmp (mode, "strafing"))
	{
		Cvar_Set ("sh_ups_color_mode", mode);
		Com_Printf ("Center UPS color mode set to: %s\n", mode);
	}
	else
	{
		Com_Printf ("Invalid color mode. Usage: 'sh ups color_mode <dynamic|static|threshold|rainbow|gradient|strafing>'\n");
	}
}

void SH_Ups_ColorGain_f (void)
{
	SH_SetColorCvar ("sh_ups_color_gain", "Center UPS gain color", "sh ups color_gain R G B A");
}

void SH_Ups_ColorLoss_f (void)
{
	SH_SetColorCvar ("sh_ups_color_loss", "Center UPS loss color", "sh ups color_loss R G B A");
}

void SH_Ups_ColorNeutral_f (void)
{
	SH_SetColorCvar ("sh_ups_color_neutral", "Center UPS neutral color", "sh ups color_neutral R G B A");
}

void SH_Ups_Format_f (void)
{
	const char *format = Cmd_Argv (3);

	if (Cmd_Argc () < 4)
	{
		Com_Printf ("- Center UPS format: %s\n", cl_strafehelperUpsFormat->string);
		return;
	}

	if (!Q_stricmp (format, "plain") || !Q_stricmp (format, "suffix") ||
		!Q_stricmp (format, "prefix") || !strcmp (format, "0") ||
		!strcmp (format, "1") || !strcmp (format, "2"))
	{
		Cvar_Set ("sh_ups_format", format);
		Com_Printf ("Center UPS format set to: %s\n", format);
	}
	else
	{
		Com_Printf ("Invalid format. Usage: 'sh ups format <plain|suffix|prefix>'\n");
	}
}

void SH_Ups_Help_f (void)
{
	Com_Printf ("========================================================================================\n");
	Com_Printf ("Center UPS Menu\n");
	Com_Printf ("Usage: sh ups <command> [options]\n");
	Com_Printf ("========================================================================================\n");
	Com_Printf ("Visibility\n");
	Com_Printf ("  %-32s %s\n", "enable", "Draw cl_ups in the center of the screen.");
	Com_Printf ("  %-32s %s\n", "disable", "Hide the center cl_ups display.");
	Com_Printf ("  %-32s %s\n", "toggle", "Toggle center cl_ups.");
	Com_Printf ("  %-32s %s\n", "status", "Show current center cl_ups settings.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Layout\n");
	Com_Printf ("  %-32s %s\n", "scale <0.25-8.0>", "Resize center UPS text.");
	Com_Printf ("  %-32s %s\n", "ypos <-1000-1000>", "Move center UPS text up/down from center.");
	Com_Printf ("  %-32s %s\n", "shadow <0|1>", "Toggle text shadow.");
	Com_Printf ("  %-32s %s\n", "hide_zero <0|1>", "Hide when rounded UPS is 0.");
	Com_Printf ("  %-32s %s\n", "format <plain|suffix|prefix>", "Use 742, 742 ups, or UPS: 742.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Color\n");
	Com_Printf ("  %-32s %s\n", "color_mode <mode>", "dynamic, static, threshold, rainbow, gradient, or strafing.");
	Com_Printf ("  %-32s %s\n", "color_gain R G B A", "Gain/high-speed color.");
	Com_Printf ("  %-32s %s\n", "color_loss R G B A", "Loss/low-speed color.");
	Com_Printf ("  %-32s %s\n", "color_neutral R G B A", "Neutral/static color.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Examples\n");
	Com_Printf ("  sh ups enable\n");
	Com_Printf ("  sh ups scale 1.5\n");
	Com_Printf ("  sh ups format suffix\n");
	Com_Printf ("  sh ups color_mode rainbow\n");
	Com_Printf ("========================================================================================\n");
}

void SH_Color_Accel_f (void)
{
	const char *color = Cmd_ArgsFrom (3);
	int r, g, b, a;

	if (Cmd_Argc () < 4) // Check if no value argument provided
	{
		Com_Printf ("- Accelerating color: %s\n", cl_strafehelper_color_accelerating->string);
		return;
	}

	if (sscanf (color, "%d %d %d %d", &r, &g, &b, &a) == 4 &&
		r >= 0 && r <= 255 && g >= 0 && g <= 255 &&
		b >= 0 && b <= 255 && a >= 0 && a <= 255)
	{
		Cvar_Set ("sh_color_accelerating", color);
		Com_Printf ("Accelerating color set to: %s\n", color);
	}
	else
	{
		Com_Printf ("Wrong input. Use 'sh hud color_accelerating R G B A'\n");
	}
}

void SH_Color_Optimal_f (void)
{
	const char *color = Cmd_ArgsFrom (3);
	int r, g, b, a;

	if (Cmd_Argc () < 4) // Check if no value argument provided
	{
		Com_Printf ("- Optimal color: %s\n", cl_strafehelper_color_optimal->string);
		return;
	}

	if (sscanf (color, "%d %d %d %d", &r, &g, &b, &a) == 4 &&
		r >= 0 && r <= 255 && g >= 0 && g <= 255 &&
		b >= 0 && b <= 255 && a >= 0 && a <= 255)
	{
		Cvar_Set ("sh_color_optimal", color);
		Com_Printf ("Optimal color changed to: %s\n", color);
	}
	else
	{
		Com_Printf ("Wrong input. Use 'sh hud color_optimal R G B A'\n");
	}
}

void SH_Color_CenterMarker_f (void)
{
	const char *color = Cmd_ArgsFrom (3);
	int r, g, b, a;

	if (Cmd_Argc () < 4) // Check if no value argument provided
	{
		Com_Printf ("- Center marker color: %s\n", cl_strafehelper_color_centermarker->string);
		return;
	}

	if (sscanf (color, "%d %d %d %d", &r, &g, &b, &a) == 4 &&
		r >= 0 && r <= 255 && g >= 0 && g <= 255 &&
		b >= 0 && b <= 255 && a >= 0 && a <= 255)
	{
		Cvar_Set ("sh_color_centermarker", color);
		Com_Printf ("Center marker color set to: %s\n", color);
	}
	else
	{
		Com_Printf ("Wrong input. Use 'sh hud color_centermarker R G B A'\n");
	}
}

void SH_Hud_Help_f (void)
{
	Com_Printf ("========================================================================================\n");
	Com_Printf ("Strafe HUD Menu\n");
	Com_Printf ("Usage: sh hud <command> [options]\n");
	Com_Printf ("========================================================================================\n");
	Com_Printf ("Visibility\n");
	Com_Printf ("  %-34s %s\n", "enable", "Enable the strafe helper bar.");
	Com_Printf ("  %-34s %s\n", "disable", "Disable the strafe helper bar.");
	Com_Printf ("  %-34s %s\n", "status", "Show current HUD and UPS settings.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Layout\n");
	Com_Printf ("  %-34s %s\n", "scale <value>", "Set bar horizontal scale.");
	Com_Printf ("  %-34s %s\n", "ypos <-4000-4000>", "Set vertical offset from screen center.");
	Com_Printf ("  %-34s %s\n", "height <value>", "Set accelerating bar height and automatic marker heights.");
	Com_Printf ("  %-34s %s\n", "center_height <0-80>", "Center marker pixels; 0 uses half the bar height.");
	Com_Printf ("  %-34s %s\n", "optimal_height <0-80>", "Optimal marker pixels; 0 uses the bar height.");
	Com_Printf ("  %-34s %s\n", "center_width <0.1-5.0>", "Set center marker width.");
	Com_Printf ("  %-34s %s\n", "optimal_width <value>", "Set optimal marker width.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Style\n");
	Com_Printf ("  %-34s %s\n", "alpha <0.0-1.0>", "Set helper opacity.");
	Com_Printf ("  %-34s %s\n", "bar_style <style>", "solid, gradient, outline, or minimal.");
	Com_Printf ("  %-34s %s\n", "smoothing <0.0-10.0>", "Smooth visual angle movement; 0 disables it.");
	Com_Printf ("  %-34s %s\n", "smoothing_mode <mode>", "linear, quadratic, cubic, sine, or exponential.");
	Com_Printf ("  %-34s %s\n", "center_marker", "Toggle the center marker.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Color\n");
	Com_Printf ("  %-34s %s\n", "color_accelerating R G B A", "Acceleration zone color.");
	Com_Printf ("  %-34s %s\n", "color_optimal R G B A", "Optimal marker color.");
	Com_Printf ("  %-34s %s\n", "color_centermarker R G B A", "Center marker color.");
	Com_Printf ("  %-34s %s\n", "preset <name|number|random>", "Apply a predefined color preset.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Examples\n");
	Com_Printf ("  sh hud enable\n");
	Com_Printf ("  sh hud bar_style outline\n");
	Com_Printf ("  sh hud alpha 0.7\n");
	Com_Printf ("  sh hud preset random\n");
	Com_Printf ("========================================================================================\n");
}

/* sh_menus.c */

void SH_Help_f (void)
{
	Com_Printf ("========================================================================================\n");
	Com_Printf ("Strafe Helper\n");
	Com_Printf ("Usage: sh <section> <command> [options]\n");
	Com_Printf ("========================================================================================\n");
	Com_Printf ("Sections\n");
	Com_Printf ("  %-18s %s\n", "hud", "Strafe bar visibility, geometry, style, and colors.");
	Com_Printf ("  %-18s %s\n", "eff", "Airborne acceleration-efficiency bar/text display.");
	Com_Printf ("  %-18s %s\n", "ups", "Centered cl_ups readout and its display/color options.");
	Com_Printf ("  %-18s %s\n", "status", "Show all current strafe helper settings.");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Common examples\n");
	Com_Printf ("  sh hud enable\n");
	Com_Printf ("  sh hud bar_style gradient\n");
	Com_Printf ("  sh hud alpha 0.75\n");
	Com_Printf ("  sh eff enable\n");
	Com_Printf ("  sh eff style both\n");
	Com_Printf ("  sh ups enable\n");
	Com_Printf ("  sh ups color_mode dynamic\n");
	Com_Printf ("----------------------------------------------------------------------------------------\n");
	Com_Printf ("Related cvars\n");
	Com_Printf ("  %-18s %s\n", "sh_nerdstats", "1 left, 2 right.");
	Com_Printf ("  %-18s %s\n", "race_width", "Race line thickness, 1 to 20.");
	Com_Printf ("  %-18s %s\n", "race_color", "Race line RGB color.");
	Com_Printf ("  %-18s %s\n", "race_alpha", "Race line opacity, 0 to 1.");
	Com_Printf ("  %-18s %s\n", "race_life", "Race line lifetime, 100 to 5000 ms.");
	Com_Printf ("========================================================================================\n");
}

void SH_Cmd_g (genctx_t *ctx, int argnum)
{
	if (argnum == 1)
	{
		Prompt_AddMatch (ctx, "hud");
		Prompt_AddMatch (ctx, "eff");
		Prompt_AddMatch (ctx, "ups");
		Prompt_AddMatch (ctx, "status");
		Prompt_AddMatch (ctx, "help");
	}
	else if (argnum == 2)
	{
		const char *subcmd = Cmd_Argv (1);
		if (!strcmp (subcmd, "hud"))
		{
			Prompt_AddMatch (ctx, "enable");
			Prompt_AddMatch (ctx, "disable");
			Prompt_AddMatch (ctx, "center_marker");
			Prompt_AddMatch (ctx, "scale");
			Prompt_AddMatch (ctx, "ypos");
			Prompt_AddMatch (ctx, "height");
			Prompt_AddMatch (ctx, "alpha");
			Prompt_AddMatch (ctx, "bar_style");
			Prompt_AddMatch (ctx, "smoothing");
			Prompt_AddMatch (ctx, "smoothing_mode");
			Prompt_AddMatch (ctx, "center_width");
			Prompt_AddMatch (ctx, "optimal_width");
			Prompt_AddMatch (ctx, "center_height");
			Prompt_AddMatch (ctx, "optimal_height");
			Prompt_AddMatch (ctx, "color_accelerating");
			Prompt_AddMatch (ctx, "color_optimal");
			Prompt_AddMatch (ctx, "color_centermarker");
			Prompt_AddMatch (ctx, "preset");
			Prompt_AddMatch (ctx, "status");
			Prompt_AddMatch (ctx, "help");
		}
		else if (!strcmp (subcmd, "eff"))
		{
			Prompt_AddMatch (ctx, "enable");
			Prompt_AddMatch (ctx, "disable");
			Prompt_AddMatch (ctx, "toggle");
			Prompt_AddMatch (ctx, "status");
			Prompt_AddMatch (ctx, "style");
			Prompt_AddMatch (ctx, "tint");
			Prompt_AddMatch (ctx, "tint_strength");
			Prompt_AddMatch (ctx, "width");
			Prompt_AddMatch (ctx, "height");
			Prompt_AddMatch (ctx, "xpos");
			Prompt_AddMatch (ctx, "ypos");
			Prompt_AddMatch (ctx, "border");
			Prompt_AddMatch (ctx, "marker");
			Prompt_AddMatch (ctx, "color_mode");
			Prompt_AddMatch (ctx, "color_good");
			Prompt_AddMatch (ctx, "color_mid");
			Prompt_AddMatch (ctx, "color_bad");
			Prompt_AddMatch (ctx, "color_bg");
			Prompt_AddMatch (ctx, "midpoint");
			Prompt_AddMatch (ctx, "smoothing");
			Prompt_AddMatch (ctx, "hold");
			Prompt_AddMatch (ctx, "text_scale");
			Prompt_AddMatch (ctx, "help");
		}
		else if (!strcmp (subcmd, "ups"))
		{
			Prompt_AddMatch (ctx, "enable");
			Prompt_AddMatch (ctx, "disable");
			Prompt_AddMatch (ctx, "toggle");
			Prompt_AddMatch (ctx, "status");
			Prompt_AddMatch (ctx, "scale");
			Prompt_AddMatch (ctx, "ypos");
			Prompt_AddMatch (ctx, "shadow");
			Prompt_AddMatch (ctx, "hide_zero");
			Prompt_AddMatch (ctx, "color_mode");
			Prompt_AddMatch (ctx, "color_gain");
			Prompt_AddMatch (ctx, "color_loss");
			Prompt_AddMatch (ctx, "color_neutral");
			Prompt_AddMatch (ctx, "format");
			Prompt_AddMatch (ctx, "help");
		}
		else if (!strcmp (subcmd, "status"))
		{
			Prompt_AddMatch (ctx, "status");
		}
	}
	else if (argnum == 3)
	{
		const char *subcmd = Cmd_Argv (1);
		const char *cmd = Cmd_Argv (2);

		if (!strcmp (subcmd, "hud"))
		{
			if (!strcmp (cmd, "bar_style"))
			{
				Prompt_AddMatch (ctx, "gradient");
				Prompt_AddMatch (ctx, "solid");
				Prompt_AddMatch (ctx, "outline");
				Prompt_AddMatch (ctx, "minimal");
			}
			else if (!strcmp (cmd, "smoothing_mode"))
			{
				Prompt_AddMatch (ctx, "linear");
				Prompt_AddMatch (ctx, "quadratic");
				Prompt_AddMatch (ctx, "cubic");
				Prompt_AddMatch (ctx, "sine");
				Prompt_AddMatch (ctx, "exponential");
			}
		}
		else if (!strcmp (subcmd, "eff"))
		{
			if (!strcmp (cmd, "border"))
			{
				Prompt_AddMatch (ctx, "0");
				Prompt_AddMatch (ctx, "1");
			}
			else if (!strcmp (cmd, "style"))
			{
				Prompt_AddMatch (ctx, "bar");
				Prompt_AddMatch (ctx, "text");
				Prompt_AddMatch (ctx, "both");
				Prompt_AddMatch (ctx, "none");
			}
			else if (!strcmp (cmd, "tint"))
			{
				Prompt_AddMatch (ctx, "off");
				Prompt_AddMatch (ctx, "optimal");
				Prompt_AddMatch (ctx, "zone");
				Prompt_AddMatch (ctx, "both");
			}
			else if (!strcmp (cmd, "color_mode"))
			{
				Prompt_AddMatch (ctx, "dynamic");
				Prompt_AddMatch (ctx, "static");
			}
		}
		else if (!strcmp (subcmd, "ups"))
		{
			if (!strcmp (cmd, "shadow") || !strcmp (cmd, "hide_zero"))
			{
				Prompt_AddMatch (ctx, "0");
				Prompt_AddMatch (ctx, "1");
			}
			else if (!strcmp (cmd, "color_mode"))
			{
				Prompt_AddMatch (ctx, "dynamic");
				Prompt_AddMatch (ctx, "static");
				Prompt_AddMatch (ctx, "threshold");
				Prompt_AddMatch (ctx, "rainbow");
				Prompt_AddMatch (ctx, "gradient");
				Prompt_AddMatch (ctx, "strafing");
			}
			else if (!strcmp (cmd, "format"))
			{
				Prompt_AddMatch (ctx, "plain");
				Prompt_AddMatch (ctx, "suffix");
				Prompt_AddMatch (ctx, "prefix");
			}
		}
	}
}

void SH_Cmd_f (void)
{
	const char *subcmd = Cmd_Argv (1); // First argument after "sh"

	if (!subcmd || !subcmd[0])
	{
		SH_Help_f ();
		return;
	}

	if (!strcmp (subcmd, "hud"))
	{
		// Forward to strafe_helper commands
		const char *cmd = Cmd_Argv (2); // Command after "sh hud"
		if (!cmd || !cmd[0])
		{
			SH_Hud_Help_f ();
			return;
		}

		if (!strcmp (cmd, "enable"))
			SH_Enable_f ();
		else if (!strcmp (cmd, "disable"))
			SH_Disable_f ();
		else if (!strcmp (cmd, "scale"))
			SH_Scale_f ();
		else if (!strcmp (cmd, "ypos"))
			SH_ypos_f ();
		else if (!strcmp (cmd, "height"))
			SH_Height_f ();
		else if (!strcmp (cmd, "alpha"))
			SH_Alpha_f ();
		else if (!strcmp (cmd, "bar_style"))
			SH_BarStyle_f ();
		else if (!strcmp (cmd, "smoothing"))
			SH_Smoothing_f ();
		else if (!strcmp (cmd, "smoothing_mode"))
			SH_SmoothingMode_f ();
		else if (!strcmp (cmd, "center_marker"))
			SH_CenterMarker_f ();
		else if (!strcmp (cmd, "center_width"))
			SH_CenterWidth_f ();
		else if (!strcmp (cmd, "optimal_width"))
			SH_OptimalWidth_f ();
		else if (!strcmp (cmd, "center_height"))
			SH_CenterHeight_f ();
		else if (!strcmp (cmd, "optimal_height"))
			SH_OptimalHeight_f ();
		else if (!strcmp (cmd, "color_accelerating"))
			SH_Color_Accel_f ();
		else if (!strcmp (cmd, "color_optimal"))
			SH_Color_Optimal_f ();
		else if (!strcmp (cmd, "color_centermarker"))
			SH_Color_CenterMarker_f ();
		else if (!strcmp (cmd, "preset"))
			SH_SetPreset_f ();
		else if (!strcmp (cmd, "status"))
			SH_Status_f ();
		else if (!strcmp (cmd, "help"))
			SH_Hud_Help_f ();
		else
			Com_Printf ("Unknown hud command. Use 'sh hud' for a list of commands.\n");
	}
	else if (!strcmp (subcmd, "eff"))
	{
		const char *cmd = Cmd_Argv (2);
		if (!cmd || !cmd[0])
		{
			SH_Eff_Help_f ();
			return;
		}
		if (!strcmp (cmd, "enable"))
			SH_Eff_Enable_f ();
		else if (!strcmp (cmd, "disable"))
			SH_Eff_Disable_f ();
		else if (!strcmp (cmd, "toggle"))
			SH_Eff_Toggle_f ();
		else if (!strcmp (cmd, "status"))
			SH_Eff_Status_f ();
		else if (!strcmp (cmd, "style"))
			SH_Eff_Style_f ();
		else if (!strcmp (cmd, "tint"))
			SH_Eff_Tint_f ();
		else if (!strcmp (cmd, "tint_strength"))
			SH_Eff_TintStrength_f ();
		else if (!strcmp (cmd, "width"))
			SH_Eff_Width_f ();
		else if (!strcmp (cmd, "height"))
			SH_Eff_Height_f ();
		else if (!strcmp (cmd, "xpos"))
			SH_Eff_Xpos_f ();
		else if (!strcmp (cmd, "ypos"))
			SH_Eff_Ypos_f ();
		else if (!strcmp (cmd, "border"))
			SH_Eff_Border_f ();
		else if (!strcmp (cmd, "marker"))
			SH_Eff_Marker_f ();
		else if (!strcmp (cmd, "color_mode"))
			SH_Eff_ColorMode_f ();
		else if (!strcmp (cmd, "color_good"))
			SH_Eff_ColorGood_f ();
		else if (!strcmp (cmd, "color_mid"))
			SH_Eff_ColorMid_f ();
		else if (!strcmp (cmd, "color_bad"))
			SH_Eff_ColorBad_f ();
		else if (!strcmp (cmd, "color_bg"))
			SH_Eff_ColorBg_f ();
		else if (!strcmp (cmd, "midpoint"))
			SH_Eff_Midpoint_f ();
		else if (!strcmp (cmd, "smoothing"))
			SH_Eff_Smoothing_f ();
		else if (!strcmp (cmd, "hold"))
			SH_Eff_Hold_f ();
		else if (!strcmp (cmd, "text_scale"))
			SH_Eff_TextScale_f ();
		else if (!strcmp (cmd, "help"))
			SH_Eff_Help_f ();
		else
			Com_Printf ("Unknown eff command. Use 'sh eff' for a list of commands.\n");
	}
	else if (!strcmp (subcmd, "ups"))
	{
		const char *cmd = Cmd_Argv (2);
		if (!cmd || !cmd[0])
		{
			SH_Ups_Help_f ();
			return;
		}
		if (!strcmp (cmd, "enable"))
			SH_Ups_Enable_f ();
		else if (!strcmp (cmd, "disable"))
			SH_Ups_Disable_f ();
		else if (!strcmp (cmd, "toggle"))
			SH_Ups_Toggle_f ();
		else if (!strcmp (cmd, "status"))
			SH_Ups_Status_f ();
		else if (!strcmp (cmd, "scale"))
			SH_Ups_Scale_f ();
		else if (!strcmp (cmd, "ypos"))
			SH_Ups_Ypos_f ();
		else if (!strcmp (cmd, "shadow"))
			SH_Ups_Shadow_f ();
		else if (!strcmp (cmd, "hide_zero"))
			SH_Ups_HideZero_f ();
		else if (!strcmp (cmd, "color_mode"))
			SH_Ups_ColorMode_f ();
		else if (!strcmp (cmd, "color_gain"))
			SH_Ups_ColorGain_f ();
		else if (!strcmp (cmd, "color_loss"))
			SH_Ups_ColorLoss_f ();
		else if (!strcmp (cmd, "color_neutral"))
			SH_Ups_ColorNeutral_f ();
		else if (!strcmp (cmd, "format"))
			SH_Ups_Format_f ();
		else if (!strcmp (cmd, "help"))
			SH_Ups_Help_f ();
		else
			Com_Printf ("Unknown ups command. Use 'sh ups' for a list of commands.\n");
	}
	else if (!strcmp (subcmd, "status"))
	{
		SH_Status_f ();
	}
	else
	{
		SH_Help_f ();
	}
}

static const char *SH_DefaultString (cvar_t *var)
{
	return var && Cvar_DefaultString (var) ? Cvar_DefaultString (var) : "?";
}

static void SH_PrintStatusInt (const char *label, cvar_t *var)
{
	Com_Printf ("  %-20s : %-20d : def: %s\n",
		label, var ? var->intvalue : 0, SH_DefaultString (var));
}

static void SH_PrintStatusFloat (const char *label, cvar_t *var)
{
	Com_Printf ("  %-20s : %-20.2f : def: %s\n",
		label, var ? var->value : 0.0f, SH_DefaultString (var));
}

static void SH_PrintStatusString (const char *label, cvar_t *var)
{
	Com_Printf ("  %-20s : %-20s : def: %s\n",
		label, var ? var->string : "?", SH_DefaultString (var));
}

void SH_Status_f (void)
{
	Com_Printf ("------------------------------------------------------------------\n");
	Com_Printf ("                        Strafe Helper Status:\n");
	Com_Printf ("------------------------------------------------------------------\n");
	SH_PrintStatusInt ("Enabled", cl_drawStrafeHelper);
	SH_PrintStatusInt ("Center marker", cl_strafeHelperCenterMarker);
	SH_PrintStatusFloat ("Y pos", cl_strafeHelperY);
	SH_PrintStatusFloat ("Scale", cl_strafeHelperScale);
	SH_PrintStatusInt ("Height", cl_strafeHelperHeight);
	SH_PrintStatusFloat ("Alpha", cl_strafehelperAlpha);
	SH_PrintStatusString ("Bar style", cl_strafehelperBarStyle);
	SH_PrintStatusFloat ("Smoothing", cl_strafehelperSmoothing);
	SH_PrintStatusInt ("Smoothing mode", cl_strafehelperSmoothingMode);
	SH_PrintStatusFloat ("Center width", cl_strafehelper_center_width);
	SH_PrintStatusFloat ("Optimal width", cl_strafehelper_optimal_width);
	SH_PrintStatusFloat ("Center height", cl_strafehelper_center_height);
	SH_PrintStatusFloat ("Optimal height", cl_strafehelper_optimal_height);
	SH_PrintStatusInt ("Optimal outline", cl_strafehelper_optimal_outline);
	SH_PrintStatusString ("Accelerating color", cl_strafehelper_color_accelerating);
	SH_PrintStatusString ("Optimal color", cl_strafehelper_color_optimal);
	SH_PrintStatusString ("Center marker color", cl_strafehelper_color_centermarker);
	Com_Printf ("------------------------------------------------------------------\n");
	Com_Printf ("                        Efficiency Display Status:\n");
	Com_Printf ("------------------------------------------------------------------\n");
	SH_PrintStatusInt ("Enabled", cl_strafehelperEfficiency);
	SH_PrintStatusString ("Style", cl_strafehelperEffStyle);
	SH_PrintStatusString ("Helper tint", cl_strafehelperEffTint);
	SH_PrintStatusFloat ("Tint strength", cl_strafehelperEffTintStrength);
	SH_PrintStatusFloat ("Width", cl_strafehelperEffWidth);
	SH_PrintStatusFloat ("Height", cl_strafehelperEffHeight);
	SH_PrintStatusFloat ("X offset", cl_strafehelperEffX);
	SH_PrintStatusFloat ("Y offset", cl_strafehelperEffY);
	SH_PrintStatusInt ("Border", cl_strafehelperEffBorder);
	SH_PrintStatusFloat ("Marker", cl_strafehelperEffMarker);
	SH_PrintStatusString ("Color mode", cl_strafehelperEffColorMode);
	SH_PrintStatusString ("Good color", cl_strafehelperEffColorGood);
	SH_PrintStatusString ("Mid color", cl_strafehelperEffColorMid);
	SH_PrintStatusString ("Bad color", cl_strafehelperEffColorBad);
	SH_PrintStatusString ("Background color", cl_strafehelperEffColorBg);
	SH_PrintStatusFloat ("Gradient midpoint", cl_strafehelperEffColorMidpoint);
	SH_PrintStatusFloat ("Smoothing", cl_strafehelperEffSmoothing);
	SH_PrintStatusFloat ("Hold time (ms)", cl_strafehelperEffHoldMs);
	SH_PrintStatusFloat ("Text scale", cl_strafehelperEffTextScale);
	Com_Printf ("------------------------------------------------------------------\n");
	Com_Printf ("                        Center UPS Status:\n");
	Com_Printf ("------------------------------------------------------------------\n");
	SH_PrintStatusInt ("Enabled", cl_strafehelperUps);
	SH_PrintStatusFloat ("Scale", cl_strafehelperUpsScale);
	SH_PrintStatusFloat ("X offset", cl_strafehelperUpsX);
	SH_PrintStatusFloat ("Y offset", cl_strafehelperUpsY);
	SH_PrintStatusInt ("Shadow", cl_strafehelperUpsShadow);
	SH_PrintStatusInt ("Hide zero", cl_strafehelperUpsHideZero);
	SH_PrintStatusString ("Color mode", cl_strafehelperUpsColorMode);
	SH_PrintStatusString ("Format", cl_strafehelperUpsFormat);
	SH_PrintStatusString ("Gain color", cl_strafehelperUpsColorGain);
	SH_PrintStatusString ("Loss color", cl_strafehelperUpsColorLoss);
	SH_PrintStatusString ("Neutral color", cl_strafehelperUpsColorNeutral);
	Com_Printf ("------------------------------------------------------------------\n");
}
