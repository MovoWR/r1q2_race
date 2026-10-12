#ifndef R1Q2_STRAFE_HELPER_H
#define R1Q2_STRAFE_HELPER_H
#pragma once
#ifdef __cplusplus
extern "C"
{
#endif

#include <math.h>
#include "jump_compat.h"
#include "strafe_efficiency.h"

#ifndef CLAMP
#define CLAMP(value, min, max) ((value) < (min) ? (min) : ((value) > (max) ? (max) : (value)))
#endif

// Fixed setting limits; viewport clipping must not change saved preferences.
#define SH_MARKER_HEIGHT_MAX          80.0f
#define SH_EFFICIENCY_WIDTH_MIN       8.0f
#define SH_EFFICIENCY_WIDTH_MAX       4000.0f
#define SH_EFFICIENCY_HEIGHT_MIN      1.0f
#define SH_EFFICIENCY_HEIGHT_MAX      64.0f
#define SH_EFFICIENCY_X_MIN           (-4000.0f)
#define SH_EFFICIENCY_X_MAX           4000.0f
#define SH_EFFICIENCY_Y_MIN           (-400.0f)
#define SH_EFFICIENCY_Y_MAX           400.0f
#define SH_EFFICIENCY_HOLD_MIN        0.0f
#define SH_EFFICIENCY_HOLD_MAX        2000.0f
#define SH_EFFICIENCY_TEXT_SCALE_MIN  0.25f
#define SH_EFFICIENCY_TEXT_SCALE_MAX  8.0f
#define SH_UPS_X_MIN                  (-4000.0f)
#define SH_UPS_X_MAX                  4000.0f
#define SH_UPS_Y_MIN                  (-4000.0f)
#define SH_UPS_Y_MAX                  4000.0f
#define SH_UPS_SCALE_MIN              0.25f
#define SH_UPS_SCALE_MAX              8.0f

	struct StrafeHelperParams
	{
		int center;
		int center_marker;
		float scale;
		float height;
		float y;
		float hud_scale;
	};

	typedef struct
	{
		// [Velocity]
		float pred_velocity_x;
		float pred_velocity_y;
		float pred_velocity_z;
		// [Position]
		float pred_pos_x;
		float pred_pos_y;
		float pred_pos_z;
		float originX;
		float originY;
		float originZ;
		vec3_t origin;
		// [Network]
		float ref_msec;
		float phys_msec;
		float main_msec;
		// [Local Move]
		float locmove_x;
		float locmove_y;
		float locmove_z;
		// [pitch, yaw, roll]
		float pitch;
		float roll;
		float viewangles;
		// [PMove Debug]
		float pmove;
		// [PM_Accelerate]
		float accelspeed_nerd;
		float addspeed_nerd;
		float currentspeed_nerd;
		float wishspeed_nerd;
		float forward_velocity_angle_nerd;
	} NerdStats;

	extern NerdStats ns;

	typedef struct
	{
		// [Angles]
		float angle_optimal;
		float angle_minimum;
		float angle_maximum;
		float angle_current;
		float angle_diff;
		float velocity_norm;
	} StrafeHelper;

	extern StrafeHelper sh;

	// StrafeHud
	void StrafeHelper_SetAccelerationValues (const float forward[3],
		const float velocity[3],
		const float wishdir[3],
		const float wishspeed,
		const float acceleration_target,
		const float accel,
		const float frametime);
	// Capture only the final local prediction; publish its sample once per frame.
	void StrafeHelper_BeginPrediction (void);
	void StrafeHelper_EndPrediction (void);
	qboolean StrafeHelper_IsPredicting (void);
	void StrafeHelper_Clear (void);
	void StrafeHelper_SetEfficiency (const float velocity[3],
		const float wishdir[3],
		float target, float budget);
	void StrafeHelper_ClearEfficiency (void);
	// Publish the private prediction sample to the live display before HUD drawing.
	void StrafeHelper_UpdateEfficiency (void);

	// StrafeHud
	void StrafeHelper_Draw (const struct StrafeHelperParams *params,
		float hud_width, float hud_height, int font_pic);

	qboolean StrafeHelper_HasData (void);

	extern qboolean sh_drawing_preview;

	void StrafeHelper_DrawPreview (const struct StrafeHelperParams *params,
		float hud_width, float hud_height, int font_pic);

	void SH_Ups_Draw (float hud_width, float hud_height, float hud_scale, int font_pic);

	// NerdStats
	void NerdStatsUpdate (const float velocity[3],
		const float wishdir[3],
		float wishspeed,
		float acceleration_target,
		float accel,
		float frametime,
		float forward_velocity_angle);

	void OriginUpdate (void);

	char *SH_NerdStats_Draw (float hud_width, float hud_height, int font_pic);

	// Debug
	void SH_DebugNow_f (void);

	void DebugNow (void);

	void StrafeHelper_DebugNow (void);

	void printKeyValueFloatPrecise (const char *label, float value);

	void printKeyValueFloat (const char *label, float value);

	void printSectionHeader (const char *title);

	void printKeyValueGeneric (const char *label, const char *value);

	//
	// Cvar declarations (definitions + registration in sh_init.c)
	//
	extern cvar_t *hud_ups_scale;
	extern cvar_t *hud_strafe_scale;
	extern cvar_t *hud_efficiency_scale;
	extern cvar_t *hud_network_scale;

	extern cvar_t *cl_drawStrafeHelper;
	extern cvar_t *cl_strafehelperEfficiency;
	extern cvar_t *cl_strafehelperEffStyle;
	extern cvar_t *cl_strafehelperEffWidth;
	extern cvar_t *cl_strafehelperEffHeight;
	extern cvar_t *cl_strafehelperEffX;
	extern cvar_t *cl_strafehelperEffY;
	extern cvar_t *cl_strafehelperEffBorder;
	extern cvar_t *cl_strafehelperEffMarker;
	extern cvar_t *cl_strafehelperEffColorMode;
	extern cvar_t *cl_strafehelperEffColorGood;
	extern cvar_t *cl_strafehelperEffColorMid;
	extern cvar_t *cl_strafehelperEffColorBad;
	extern cvar_t *cl_strafehelperEffColorBg;
	extern cvar_t *cl_strafehelperEffColorMidpoint;
	extern cvar_t *cl_strafehelperEffSmoothing;
	extern cvar_t *cl_strafehelperEffHoldMs;
	extern cvar_t *cl_strafehelperEffTextScale;
	extern cvar_t *cl_strafehelperEffTint;
	extern cvar_t *cl_strafehelperEffTintStrength;
	extern cvar_t *cl_strafeHelperCenter;
	extern cvar_t *cl_strafeHelperCenterMarker;
	extern cvar_t *cl_strafeHelperHeight;
	extern cvar_t *cl_strafeHelperScale;
	extern cvar_t *cl_strafeHelperY;
	extern cvar_t *cl_strafehelperUps;
	extern cvar_t *cl_strafehelperUpsScale;
	extern cvar_t *cl_strafehelperUpsY;
	extern cvar_t *cl_strafehelperUpsX;
	extern cvar_t *cl_strafehelperUpsShadow;
	extern cvar_t *cl_strafehelperUpsHideZero;
	extern cvar_t *cl_strafehelperUpsColorMode;
	extern cvar_t *cl_strafehelperUpsColorGain;
	extern cvar_t *cl_strafehelperUpsColorLoss;
	extern cvar_t *cl_strafehelperUpsColorNeutral;
	extern cvar_t *cl_strafehelperUpsFormat;
	extern cvar_t *cl_strafehelperUps3D;
	extern cvar_t *cl_strafehelperAlpha;
	extern cvar_t *cl_strafehelperBarStyle;
	extern cvar_t *cl_strafehelperSmoothing;
	extern cvar_t *cl_strafehelperSmoothingMode;
	extern cvar_t *cl_strafehelperNerdStats;
	extern cvar_t *cl_strafehelper_center_width;
	extern cvar_t *cl_strafehelper_optimal_width;
	extern cvar_t *cl_strafehelper_center_height;
	extern cvar_t *cl_strafehelper_optimal_height;
	extern cvar_t *cl_strafehelper_optimal_outline;
	extern cvar_t *cl_strafehelper_color_accelerating;
	extern cvar_t *cl_strafehelper_color_optimal;
	extern cvar_t *cl_strafehelper_color_centermarker;
	extern cvar_t *cl_strafehelper_color_nerdstats;
	extern cvar_t *cl_race_color;
	extern cvar_t *cl_race_life;
	extern cvar_t *cl_race_width;
	extern cvar_t *cl_race_alpha;
	extern cvar_t *cl_input_keys;

	void SH_Init (void);

	void SH_ClearDrawState (void);
	void SH_Efficiency_Clear (void);
#endif
