#pragma once
#ifdef __cplusplus
extern "C"
{
#endif


#include "strafe_helper_customization.h"

	// Updates the live hold/smoothing state from this frame's prediction sample.
	// Preview calls and disabled efficiency leave live state unchanged. Nonfinite
	// values count as unavailable samples; repeated timestamps do not advance smoothing.
	void SH_Efficiency_Update (qboolean valid, float value);

	// Draws the current value relative to the helper bar without updating live state.
	// Call Update once in the live HUD pass before either drawing or querying tint.
	void SH_Efficiency_Draw (float helper_upper_y, float helper_height,
		float hud_width, float hud_scale, int font_pic);

	// Menu preview: animates a synthetic efficiency value.
	void SH_Efficiency_DrawPreview (float helper_upper_y, float helper_height,
		float hud_width, float hud_scale, int font_pic);

	// Blends a helper element's base color toward the current efficiency color,
	// per sh_efficiency_tint / sh_efficiency_tint_strength. Returns base unchanged
	// when tinting is off, the element is not targeted, or no value is live.
	uint32 SH_Efficiency_ApplyTint (enum shc_ElementId element_id, uint32 base);

#ifdef __cplusplus
} /* extern "C" */
#endif
