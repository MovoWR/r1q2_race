/* Nerdstats presentation from frozen Q2PRO Race 5571a17dbb7fb03915e7b403b1f981b7006a1a05.
 * Retained under the accompanying MPL 2.0 module license. */
#include "strafe_helper.h"

#include <math.h>
#include "strafe_helper_customization.h"

char *SH_NerdStats_Draw (float hud_width, float hud_height, int font_pic)
{
	char formatted[64];
	const int lineSpacing = CHAR_HEIGHT;
	char buffer[MAX_STRING_CHARS];

	const int dataIndent = 20;
	const int titleIndent = 40;

	int block_width = 240;
	int block_height = hud_height;
	int x = 0;
	int margin = 10;
	int left_y = CHAR_HEIGHT + margin;


	if (cl_strafehelperNerdStats->intvalue == 1)
	{
		x = CHAR_WIDTH;
	}
	else if (cl_strafehelperNerdStats->intvalue == 2)
	{
		x = (int)(hud_width - block_width) + 10;
	}


	if (cl_strafehelperNerdStats && cl_strafehelperNerdStats->intvalue)
	{
		shc_drawFilledRectangle (x - 10, 0, block_width, block_height, shc_ElementId_NerdStats);


		// -------------------------------------
		// [Physics Timings]
		// -------------------------------------

		// Frame Timings Section
		R_SetColor (U32_YELLOW);
		left_y += lineSpacing * 2.0; // Add spacing
		R_DrawString (x + titleIndent, left_y, 0, MAX_STRING_CHARS, "[Frame Timings]", font_pic);
		left_y += lineSpacing;
		R_SetColor (U32_WHITE);

		R_Fps_m (formatted, sizeof (formatted));
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %s", "Render FPS:", formatted);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		CL_Mmps_m (formatted, sizeof (formatted));
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %s FPS", "Moves/sec:", formatted);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		CL_Pps_m (formatted, sizeof (formatted));
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %s packets", "Packets/sec:", formatted);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		CL_Ping_m (formatted, sizeof (formatted));
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %s ms", "Ping:", formatted);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		CL_Lag_m (formatted, sizeof (formatted));
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %s ", "Lag:", formatted);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;
		// -------------------------------------
		// [Velocity]
		// -------------------------------------
		R_SetColor (U32_YELLOW);
		left_y += lineSpacing * 2.0; // some extra spacing
		//
		R_DrawString (x + titleIndent, left_y, 0, MAX_STRING_CHARS, "[Velocity]", font_pic);
		left_y += lineSpacing;
		R_SetColor (U32_WHITE);

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "X:", ns.pred_velocity_x);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Y:", ns.pred_velocity_y);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Z:", ns.pred_velocity_z);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;


		// -------------------------------------
		// [Position]
		// -------------------------------------
		R_SetColor (U32_YELLOW);
		left_y += lineSpacing * 2.0; // some extra spacing
		R_DrawString (x + titleIndent, left_y, 0, MAX_STRING_CHARS, "[Origin]", font_pic);
		left_y += lineSpacing;
		R_SetColor (U32_WHITE);

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "X:", ns.pred_pos_x);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Y:", ns.pred_pos_y);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Z:", ns.pred_pos_z);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;


		// -------------------------------------
		// Orientation (pitch, yaw, roll)
		// -------------------------------------

		R_SetColor (U32_YELLOW);
		left_y += lineSpacing * 2.0; // some extra spacing
		R_DrawString (x + titleIndent, left_y, 0, MAX_STRING_CHARS, "[Orientation]", font_pic);
		left_y += lineSpacing;
		R_SetColor (U32_WHITE);

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Pitch:", ns.pitch);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Roll:", ns.roll);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.4f", "ViewAngle/Yaw:", ns.viewangles);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;


		// -------------------------------------
		// [Angles]
		// -------------------------------------
		R_SetColor (U32_YELLOW);
		left_y += lineSpacing * 2.0; // some extra spacing
		R_DrawString (x + titleIndent, left_y, 0, MAX_STRING_CHARS, "[Angles]", font_pic);
		left_y += lineSpacing;
		R_SetColor (U32_WHITE);
		//
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Optimal:", sh.angle_optimal);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Minimum:", sh.angle_minimum);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Maximum:", sh.angle_maximum);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Angle Diff:", sh.angle_diff);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;


		// -------------------------------------
		// [PM_Accelerate]
		// -------------------------------------
		R_SetColor (U32_YELLOW);
		left_y += lineSpacing * 2.0; // some extra spacing
		R_DrawString (x + titleIndent, left_y, 0, MAX_STRING_CHARS, "[PM_Accelerate]", font_pic);
		left_y += lineSpacing;
		R_SetColor (U32_WHITE);
		//
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Addspeed:", ns.addspeed_nerd);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Curr speed:", ns.currentspeed_nerd);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Accelspeed:", ns.accelspeed_nerd);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Wishspeed:", ns.wishspeed_nerd);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;
		Q_scnprintf (buffer, sizeof (buffer), "%-12s %+8.2f", "Fwd vel ang:", ns.forward_velocity_angle_nerd);
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;


		// -------------------------------------
		// [PMove Debug]
		// -------------------------------------
		R_SetColor (U32_YELLOW);
		left_y += lineSpacing * 2.0; // some extra spacing
		R_DrawString (x + titleIndent, left_y, 0, MAX_STRING_CHARS, "[PMove Debug]", font_pic);
		left_y += lineSpacing;
		R_SetColor (U32_WHITE);
		//
		static const char *const types[] = {
			"NORMAL", "SPECTATOR", "DEAD", "GIB", "FREEZE"
		};
		static const char *const flags[] = {
			"DUCKED", "JUMP_HELD", "ON_GROUND",
			"TIME_WATERJUMP", "TIME_LAND", "TIME_TELEPORT",
			"NO_PREDICTION", "TELEPORT_BIT"
		};
		unsigned pm_type = cl.frame.playerstate.pmove.pm_type;
		if (pm_type > PM_FREEZE)
			pm_type = PM_FREEZE;

		Q_scnprintf (buffer, sizeof (buffer), "%-12s %s", "pm_type:",
			(pm_type <= PM_FREEZE ? types[pm_type] : "UNKNOWN"));
		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
		left_y += lineSpacing;

		R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, "", font_pic);
		left_y += lineSpacing;

		unsigned pm_flags = cl.frame.playerstate.pmove.pm_flags;
		unsigned i;
		for (i = 0; i < 8; i++)
		{
			if (pm_flags & (1 << i))
			{
				Q_scnprintf (buffer, sizeof (buffer), "%-12s %s", "", flags[i]);
				R_DrawString (x + dataIndent, left_y, 0, MAX_STRING_CHARS, buffer, font_pic);
				left_y += lineSpacing;
			}
		}
	}


	return 0;
}

// Helper function to print a section header
void printSectionHeader (const char *title)
{
	Com_Printf ("\n---------- %s ----------\n", LOG_CLIENT, title);
}

// Unified helper for key-value pairs (strings or floats)
void printKeyValueGeneric (const char *label, const char *value)
{
	Com_Printf ("%-15s | %-5s\n", LOG_CLIENT, label, value); // Wider label and value columns
}

void printKeyValueFloat (const char *label, float value)
{
	char formattedValue[32];
	Q_scnprintf (formattedValue, sizeof (formattedValue), "%+10.2f", value); // Format float as a string
	printKeyValueGeneric (label, formattedValue);							 // Reuse generic helper
}

void printKeyValueFloatPrecise (const char *label, float value)
{
	char formattedValue[32];
	Q_scnprintf (formattedValue, sizeof (formattedValue), "%+10.4f", value); // Format high-precision float
	printKeyValueGeneric (label, formattedValue);							 // Reuse generic helper
}

void DebugNow (void)
{
	CL_JumpNerdUpdate ();
	// Print debug information immediately
	Com_Printf ("========== DebugNow Triggered ==========\n", LOG_CLIENT);

	// Predicted Velocity Section
	printSectionHeader ("Predicted Velocity");
	printKeyValueFloat ("X", ns.pred_velocity_x);
	printKeyValueFloat ("Y", ns.pred_velocity_y);
	printKeyValueFloat ("Z", ns.pred_velocity_z);

	// Predicted Origin Section
	printSectionHeader ("Predicted Origin");
	printKeyValueFloat ("X", ns.pred_pos_x);
	printKeyValueFloat ("Y", ns.pred_pos_y);
	printKeyValueFloat ("Z", ns.pred_pos_z);

	// Orientation Section
	printSectionHeader ("Orientation");
	printKeyValueFloat ("Pitch", ns.pitch);
	printKeyValueFloat ("Roll", ns.roll);
	printKeyValueFloatPrecise ("ViewAngle/Yaw", ns.viewangles);

	// Angles Section
	printSectionHeader ("Angles");
	printKeyValueFloat ("Optimal", sh.angle_optimal);
	printKeyValueFloat ("Minimum", sh.angle_minimum);
	printKeyValueFloat ("Maximum", sh.angle_maximum);
	printKeyValueFloat ("Angle Diff", sh.angle_diff);

	// PM_Accelerate Section
	printSectionHeader ("PM_Accelerate");
	printKeyValueFloat ("Addspeed", ns.addspeed_nerd);
	printKeyValueFloat ("Curr speed", ns.currentspeed_nerd);
	printKeyValueFloat ("Accelspeed", ns.accelspeed_nerd);
	printKeyValueFloat ("Wishspeed", ns.wishspeed_nerd);
	printKeyValueFloat ("Fwd vel ang", ns.forward_velocity_angle_nerd);

	Com_Printf ("========================================\n", LOG_CLIENT);
}

void SH_DebugNow_f (void)
{
	if (!CL_JumpEnabled ())
		return;
	DebugNow ();
}

NerdStats ns;

void CL_JumpNerdUpdate (void)
{
	vec3_t velocity, origin;

	memset (&ns, 0, sizeof (ns));
	if (cl_jump_sample.prediction_valid)
	{
		VectorCopy (cl_jump_sample.velocity, velocity);
		VectorCopy (cl_jump_sample.origin, origin);
	}
	else
	{
		VectorScale (cl.frame.playerstate.pmove.velocity, 0.125f, velocity);
		VectorScale (cl.frame.playerstate.pmove.origin, 0.125f, origin);
	}
	ns.pred_velocity_x = velocity[0];
	ns.pred_velocity_y = velocity[1];
	ns.pred_velocity_z = velocity[2];
	ns.pred_pos_x = origin[0];
	ns.pred_pos_y = origin[1];
	ns.pred_pos_z = origin[2];
	ns.locmove_x = cl_jump_sample.localmove[0];
	ns.locmove_y = cl_jump_sample.localmove[1];
	ns.locmove_z = cl_jump_sample.localmove[2];
	ns.pitch = cl.refdef.viewangles[PITCH];
	ns.roll = cl.refdef.viewangles[ROLL];
	ns.viewangles = cl.refdef.viewangles[YAW];
	ns.pmove = cl.frame.playerstate.pmove.pm_type;
	ns.currentspeed_nerd = cl_jump_sample.currentspeed;
	ns.addspeed_nerd = cl_jump_sample.addspeed;
	ns.accelspeed_nerd = cl_jump_sample.budget;
	ns.wishspeed_nerd = cl_jump_sample.wishspeed;
	ns.forward_velocity_angle_nerd = cl_jump_sample.forward_wish_angle;
}
