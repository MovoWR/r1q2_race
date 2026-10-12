/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
/* Frozen Jump defaults; configuration registration never overwrites saved values. */
#include "../client.h"
#include "jump_settings.h"
extern cvar_t *jump_hud_alpha;

cvar_t *hud_ups_scale;
cvar_t *hud_strafe_scale;
cvar_t *hud_efficiency_scale;
cvar_t *hud_network_scale;
cvar_t *cl_drawStrafeHelper;
cvar_t *cl_strafehelperEfficiency;
cvar_t *cl_strafehelperEffStyle;
cvar_t *cl_strafehelperEffWidth;
cvar_t *cl_strafehelperEffHeight;
cvar_t *cl_strafehelperEffX;
cvar_t *cl_strafehelperEffY;
cvar_t *cl_strafehelperEffBorder;
cvar_t *cl_strafehelperEffMarker;
cvar_t *cl_strafehelperEffColorMode;
cvar_t *cl_strafehelperEffColorGood;
cvar_t *cl_strafehelperEffColorMid;
cvar_t *cl_strafehelperEffColorBad;
cvar_t *cl_strafehelperEffColorBg;
cvar_t *cl_strafehelperEffColorMidpoint;
cvar_t *cl_strafehelperEffSmoothing;
cvar_t *cl_strafehelperEffHoldMs;
cvar_t *cl_strafehelperEffTextScale;
cvar_t *cl_strafehelperEffTint;
cvar_t *cl_strafehelperEffTintStrength;
cvar_t *cl_strafeHelperCenter;
cvar_t *cl_strafeHelperCenterMarker;
cvar_t *cl_strafeHelperHeight;
cvar_t *cl_strafeHelperScale;
cvar_t *cl_strafeHelperY;
cvar_t *cl_strafehelperUps;
cvar_t *cl_strafehelperUpsScale;
cvar_t *cl_strafehelperUpsX;
cvar_t *cl_strafehelperUpsY;
cvar_t *cl_strafehelperUpsShadow;
cvar_t *cl_strafehelperUpsHideZero;
cvar_t *cl_strafehelperUpsColorMode;
cvar_t *cl_strafehelperUpsColorGain;
cvar_t *cl_strafehelperUpsColorLoss;
cvar_t *cl_strafehelperUpsColorNeutral;
cvar_t *cl_strafehelperUpsFormat;
cvar_t *cl_strafehelperUps3D;
cvar_t *cl_strafehelperAlpha;
cvar_t *cl_strafehelperBarStyle;
cvar_t *cl_strafehelperSmoothing;
cvar_t *cl_strafehelperSmoothingMode;
cvar_t *cl_strafehelper_center_width;
cvar_t *cl_strafehelper_optimal_width;
cvar_t *cl_strafehelper_center_height;
cvar_t *cl_strafehelper_optimal_height;
cvar_t *cl_strafehelper_optimal_outline;
cvar_t *cl_strafehelper_color_accelerating;
cvar_t *cl_strafehelper_color_optimal;
cvar_t *cl_strafehelper_color_centermarker;
cvar_t *cl_strafehelper_color_nerdstats;
cvar_t *cl_strafehelperNerdStats;
cvar_t *cl_race_width;
cvar_t *cl_race_color;
cvar_t *cl_race_alpha;
cvar_t *cl_race_life;
cvar_t *cl_input_keys;
cvar_t *sh_netmeter;
cvar_t *sh_netwarn_ping_adaptive;
cvar_t *sh_netwarn_spike_ms;
cvar_t *sh_netwarn_spike_pct;
cvar_t *sh_netwarn_jitter_ms;
cvar_t *sh_netalert;
cvar_t *sh_netalert_x;
cvar_t *sh_netalert_y;
cvar_t *sh_netalert_color;
cvar_t *sh_netalert_alpha;
cvar_t *sh_netalert_duration_ms;
cvar_t *sh_netalert_loss;
cvar_t *sh_netalert_jitter;
cvar_t *sh_netalert_spike;
cvar_t *sh_lagometer_x;
cvar_t *sh_lagometer_y;
cvar_t *sh_netmeter_min_ms;
cvar_t *sh_netmeter_max_ms;
cvar_t *sh_netmeter_adaptive;
cvar_t *sh_lagometer_color_normal;
cvar_t *sh_lagometer_color_spike;
cvar_t *sh_lagometer_color_jitter;
cvar_t *sh_lagometer_color_loss_s2c;
cvar_t *sh_lagometer_color_loss_c2s;
cvar_t *sh_lagometer_alpha;
cvar_t *sh_lagometer_bad_alpha;
cvar_t *sh_netgraph_y;
cvar_t *sh_netgraph_height;
cvar_t *sh_netgraph_alpha;
cvar_t *sh_netgraph_color_normal;
cvar_t *sh_netgraph_color_spike;
cvar_t *sh_netgraph_color_jitter;
cvar_t *sh_netgraph_color_loss_s2c;
cvar_t *sh_netgraph_color_loss_c2s;
cvar_t *sh_histogram_x;
cvar_t *sh_histogram_y;
cvar_t *sh_histogram_width_mode;
cvar_t *sh_histogram_width;
cvar_t *sh_histogram_height;
cvar_t *sh_histogram_fill_mode;
cvar_t *sh_histogram_spacing_mode;
cvar_t *sh_histogram_bg_alpha;
cvar_t *sh_histogram_color_bg;
cvar_t *sh_histogram_color_normal;
cvar_t *sh_histogram_color_spike;
cvar_t *sh_histogram_color_jitter;
cvar_t *sh_histogram_color_loss_s2c;
cvar_t *sh_histogram_color_loss_c2s;
cvar_t *sh_histogram_alpha;
cvar_t *sh_histogram_bad_alpha;
cvar_t *sh_histogram_history;
cvar_t *sh_histogram_ping;
cvar_t *fps_default_hold;
cvar_t *fps_default_release;

typedef struct
{
	const char *name, *value;
	int flags;
	cvar_t **variable;
} jump_setting_t;

static const jump_setting_t jump_settings[] = {
	{ "hud_ups_scale", "1", CVAR_ARCHIVE, &hud_ups_scale },
	{ "hud_strafe_scale", "1", CVAR_ARCHIVE, &hud_strafe_scale },
	{ "hud_efficiency_scale", "1", CVAR_ARCHIVE, &hud_efficiency_scale },
	{ "hud_network_scale", "1", CVAR_ARCHIVE, &hud_network_scale },
	{ "sh_draw", "1", CVAR_ARCHIVE, &cl_drawStrafeHelper },
	{ "sh_efficiency", "1", CVAR_ARCHIVE, &cl_strafehelperEfficiency },
	{ "sh_efficiency_style", "text", CVAR_ARCHIVE, &cl_strafehelperEffStyle },
	{ "sh_efficiency_width", "80", CVAR_ARCHIVE, &cl_strafehelperEffWidth },
	{ "sh_efficiency_height", "4", CVAR_ARCHIVE, &cl_strafehelperEffHeight },
	{ "sh_efficiency_x", "0", CVAR_ARCHIVE, &cl_strafehelperEffX },
	{ "sh_efficiency_y", "0", CVAR_ARCHIVE, &cl_strafehelperEffY },
	{ "sh_efficiency_border", "1", CVAR_ARCHIVE, &cl_strafehelperEffBorder },
	{ "sh_efficiency_marker", "0.9", CVAR_ARCHIVE, &cl_strafehelperEffMarker },
	{ "sh_efficiency_color_mode", "dynamic", CVAR_ARCHIVE, &cl_strafehelperEffColorMode },
	{ "sh_efficiency_color_good", "80 220 90 255", CVAR_ARCHIVE, &cl_strafehelperEffColorGood },
	{ "sh_efficiency_color_mid", "235 200 60 255", CVAR_ARCHIVE, &cl_strafehelperEffColorMid },
	{ "sh_efficiency_color_bad", "235 70 60 255", CVAR_ARCHIVE, &cl_strafehelperEffColorBad },
	{ "sh_efficiency_color_bg", "40 40 40 200", CVAR_ARCHIVE, &cl_strafehelperEffColorBg },
	{ "sh_efficiency_color_midpoint", "0.5", CVAR_ARCHIVE, &cl_strafehelperEffColorMidpoint },
	{ "sh_efficiency_smoothing", "0", CVAR_ARCHIVE, &cl_strafehelperEffSmoothing },
	{ "sh_efficiency_hold_ms", "150", CVAR_ARCHIVE, &cl_strafehelperEffHoldMs },
	{ "sh_efficiency_text_scale", "1", CVAR_ARCHIVE, &cl_strafehelperEffTextScale },
	{ "sh_efficiency_tint", "off", CVAR_ARCHIVE, &cl_strafehelperEffTint },
	{ "sh_efficiency_tint_strength", "0.75", CVAR_ARCHIVE, &cl_strafehelperEffTintStrength },
	{ "sh_center", "1", CVAR_ARCHIVE, &cl_strafeHelperCenter },
	{ "sh_centermarker", "1", CVAR_ARCHIVE, &cl_strafeHelperCenterMarker },
	{ "sh_height", "15", CVAR_ARCHIVE, &cl_strafeHelperHeight },
	{ "sh_scale", "1.500000", CVAR_ARCHIVE, &cl_strafeHelperScale },
	{ "sh_y", "0", CVAR_ARCHIVE, &cl_strafeHelperY },
	{ "sh_ups", "1", CVAR_ARCHIVE, &cl_strafehelperUps },
	{ "sh_ups_scale", "1", CVAR_ARCHIVE, &cl_strafehelperUpsScale },
	{ "sh_ups_x", "0", CVAR_ARCHIVE, &cl_strafehelperUpsX },
	{ "sh_ups_y", "-15", CVAR_ARCHIVE, &cl_strafehelperUpsY },
	{ "sh_ups_shadow", "1", CVAR_ARCHIVE, &cl_strafehelperUpsShadow },
	{ "sh_ups_hide_zero", "1", CVAR_ARCHIVE, &cl_strafehelperUpsHideZero },
	{ "sh_ups_color_mode", "dynamic", CVAR_ARCHIVE, &cl_strafehelperUpsColorMode },
	{ "sh_ups_color_gain", "0 255 0 255", CVAR_ARCHIVE, &cl_strafehelperUpsColorGain },
	{ "sh_ups_color_loss", "255 0 0 255", CVAR_ARCHIVE, &cl_strafehelperUpsColorLoss },
	{ "sh_ups_color_neutral", "255 255 255 255", CVAR_ARCHIVE, &cl_strafehelperUpsColorNeutral },
	{ "sh_ups_format", "plain", CVAR_ARCHIVE, &cl_strafehelperUpsFormat },
	{ "sh_ups_3d", "0", CVAR_ARCHIVE, &cl_strafehelperUps3D },
	{ "sh_alpha", "0.500000", CVAR_ARCHIVE, &cl_strafehelperAlpha },
	{ "sh_bar_style", "gradient", CVAR_ARCHIVE, &cl_strafehelperBarStyle },
	{ "sh_smoothing", "0", CVAR_ARCHIVE, &cl_strafehelperSmoothing },
	{ "sh_smoothing_mode", "1", CVAR_ARCHIVE, &cl_strafehelperSmoothingMode },
	{ "sh_center_width", "2", CVAR_ARCHIVE, &cl_strafehelper_center_width },
	{ "sh_optimal_width", "2", CVAR_ARCHIVE, &cl_strafehelper_optimal_width },
	{ "sh_center_height", "0", CVAR_ARCHIVE, &cl_strafehelper_center_height },
	{ "sh_optimal_height", "0", CVAR_ARCHIVE, &cl_strafehelper_optimal_height },
	{ "sh_optimal_outline", "1", CVAR_ARCHIVE, &cl_strafehelper_optimal_outline },
	{ "sh_color_accelerating", "0 128 0 128", CVAR_ARCHIVE, &cl_strafehelper_color_accelerating },
	{ "sh_color_optimal", "255 215 0 255", CVAR_ARCHIVE, &cl_strafehelper_color_optimal },
	{ "sh_color_centermarker", "255 255 255 255", CVAR_ARCHIVE, &cl_strafehelper_color_centermarker },
	{ "sh_color_nerdstats", "50 0 50 100", CVAR_ARCHIVE, &cl_strafehelper_color_nerdstats },
	{ "sh_nerdstats", "0", CVAR_ARCHIVE, &cl_strafehelperNerdStats },
	{ "race_width", "5", CVAR_ARCHIVE, &cl_race_width },
	{ "race_color", "0 255 0", CVAR_ARCHIVE, &cl_race_color },
	{ "race_alpha", "0.5", CVAR_ARCHIVE, &cl_race_alpha },
	{ "race_life", "500", CVAR_ARCHIVE, &cl_race_life },
	{ "cl_input_keys", "", 0, &cl_input_keys },
	{ "sh_netmeter", "3", CVAR_ARCHIVE, &sh_netmeter },
	{ "sh_netwarn_ping_adaptive", "1", CVAR_ARCHIVE, &sh_netwarn_ping_adaptive },
	{ "sh_netwarn_spike_ms", "300", CVAR_ARCHIVE, &sh_netwarn_spike_ms },
	{ "sh_netwarn_spike_pct", "100", CVAR_ARCHIVE, &sh_netwarn_spike_pct },
	{ "sh_netwarn_jitter_ms", "100", CVAR_ARCHIVE, &sh_netwarn_jitter_ms },
	{ "sh_netalert", "1", CVAR_ARCHIVE, &sh_netalert },
	{ "sh_netalert_x", "0", CVAR_ARCHIVE, &sh_netalert_x },
	{ "sh_netalert_y", "353", CVAR_ARCHIVE, &sh_netalert_y },
	{ "sh_netalert_color", "1", CVAR_ARCHIVE, &sh_netalert_color },
	{ "sh_netalert_alpha", "1", CVAR_ARCHIVE, &sh_netalert_alpha },
	{ "sh_netalert_duration_ms", "2000", CVAR_ARCHIVE, &sh_netalert_duration_ms },
	{ "sh_netalert_loss", "1", CVAR_ARCHIVE, &sh_netalert_loss },
	{ "sh_netalert_jitter", "1", CVAR_ARCHIVE, &sh_netalert_jitter },
	{ "sh_netalert_spike", "1", CVAR_ARCHIVE, &sh_netalert_spike },
	{ "sh_lagometer_x", "166", CVAR_ARCHIVE, &sh_lagometer_x },
	{ "sh_lagometer_y", "-1", CVAR_ARCHIVE, &sh_lagometer_y },
	{ "sh_netmeter_min_ms", "0", CVAR_ARCHIVE, &sh_netmeter_min_ms },
	{ "sh_netmeter_max_ms", "150", CVAR_ARCHIVE, &sh_netmeter_max_ms },
	{ "sh_netmeter_adaptive", "1", CVAR_ARCHIVE, &sh_netmeter_adaptive },
	{ "sh_lagometer_color_normal", "212", CVAR_ARCHIVE, &sh_lagometer_color_normal },
	{ "sh_lagometer_color_spike", "225", CVAR_ARCHIVE, &sh_lagometer_color_spike },
	{ "sh_lagometer_color_jitter", "220", CVAR_ARCHIVE, &sh_lagometer_color_jitter },
	{ "sh_lagometer_color_loss_s2c", "231", CVAR_ARCHIVE, &sh_lagometer_color_loss_s2c },
	{ "sh_lagometer_color_loss_c2s", "243", CVAR_ARCHIVE, &sh_lagometer_color_loss_c2s },
	{ "sh_lagometer_alpha", "0.300000", CVAR_ARCHIVE, &sh_lagometer_alpha },
	{ "sh_lagometer_bad_alpha", "1", CVAR_ARCHIVE, &sh_lagometer_bad_alpha },
	{ "sh_netgraph_y", "-1", CVAR_ARCHIVE, &sh_netgraph_y },
	{ "sh_netgraph_height", "15", CVAR_ARCHIVE, &sh_netgraph_height },
	{ "sh_netgraph_alpha", "0.75", CVAR_ARCHIVE, &sh_netgraph_alpha },
	{ "sh_netgraph_color_normal", "213", CVAR_ARCHIVE, &sh_netgraph_color_normal },
	{ "sh_netgraph_color_spike", "208", CVAR_ARCHIVE, &sh_netgraph_color_spike },
	{ "sh_netgraph_color_jitter", "220", CVAR_ARCHIVE, &sh_netgraph_color_jitter },
	{ "sh_netgraph_color_loss_s2c", "233", CVAR_ARCHIVE, &sh_netgraph_color_loss_s2c },
	{ "sh_netgraph_color_loss_c2s", "11", CVAR_ARCHIVE, &sh_netgraph_color_loss_c2s },
	{ "sh_histogram_x", "0", CVAR_ARCHIVE, &sh_histogram_x },
	{ "sh_histogram_y", "-1", CVAR_ARCHIVE, &sh_histogram_y },
	{ "sh_histogram_width_mode", "2", CVAR_ARCHIVE, &sh_histogram_width_mode },
	{ "sh_histogram_width", "1280", CVAR_ARCHIVE, &sh_histogram_width },
	{ "sh_histogram_height", "15", CVAR_ARCHIVE, &sh_histogram_height },
	{ "sh_histogram_fill_mode", "0", CVAR_ARCHIVE, &sh_histogram_fill_mode },
	{ "sh_histogram_spacing_mode", "0", CVAR_ARCHIVE, &sh_histogram_spacing_mode },
	{ "sh_histogram_bg_alpha", "0.150000", CVAR_ARCHIVE, &sh_histogram_bg_alpha },
	{ "sh_histogram_color_bg", "0", CVAR_ARCHIVE, &sh_histogram_color_bg },
	{ "sh_histogram_color_normal", "209", CVAR_ARCHIVE, &sh_histogram_color_normal },
	{ "sh_histogram_color_spike", "220", CVAR_ARCHIVE, &sh_histogram_color_spike },
	{ "sh_histogram_color_jitter", "215", CVAR_ARCHIVE, &sh_histogram_color_jitter },
	{ "sh_histogram_color_loss_s2c", "227", CVAR_ARCHIVE, &sh_histogram_color_loss_s2c },
	{ "sh_histogram_color_loss_c2s", "241", CVAR_ARCHIVE, &sh_histogram_color_loss_c2s },
	{ "sh_histogram_alpha", "1", CVAR_ARCHIVE, &sh_histogram_alpha },
	{ "sh_histogram_bad_alpha", "1", CVAR_ARCHIVE, &sh_histogram_bad_alpha },
	{ "sh_histogram_history_ms", "52000", CVAR_ARCHIVE, &sh_histogram_history },
	{ "sh_histogram_ping", "1", CVAR_ARCHIVE, &sh_histogram_ping },
	{ "fps_default_hold", "30", CVAR_ARCHIVE, &fps_default_hold },
	{ "fps_default_release", "120", CVAR_ARCHIVE, &fps_default_release },
};

void CL_JumpConfigInit (void)
{
	unsigned i;
	for (i = 0; i < sizeof (jump_settings) / sizeof (jump_settings[0]); i++)
		*jump_settings[i].variable = Cvar_Get (jump_settings[i].name, jump_settings[i].value, jump_settings[i].flags);
	jump_hud_alpha = Cvar_Get ("scr_alpha", "1", CVAR_ARCHIVE);
}

const char *CL_JumpDefault (const char *name)
{
	unsigned i;
	for (i = 0; i < sizeof (jump_settings) / sizeof (jump_settings[0]); i++)
		if (!strcmp (name, jump_settings[i].name))
			return jump_settings[i].value;
	return NULL;
}
