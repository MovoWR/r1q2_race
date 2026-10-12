#ifndef STRAFE_HELPER_CUSTOMIZATION_H
#define STRAFE_HELPER_CUSTOMIZATION_H
#ifdef __cplusplus
extern "C"
{
#endif
#pragma once
#include "jump_compat.h"

	enum shc_ElementId
	{
		shc_ElementId_AcceleratingAngles,
		shc_ElementId_OptimalAngle,
		shc_ElementId_CenterMarker,
		shc_ElementId_NerdStats,
	};

	qboolean shc_ParseColorCvar (const char *cvarValue, uint32 *outUint32, color_t *outColor);

	uint32 shc_ParseColorString (const char *colorStr, byte *r, byte *g, byte *b, byte *a);

	uint32 shc_ApplyHelperAlpha (uint32 color);

	uint32 getColorForElement (enum shc_ElementId element_id);

	void shc_drawFilledRectangle (float x, float y, float w, float h, enum shc_ElementId element_id);

	// x/w describe the visible clipped rectangle. gradient_start/end describe the
	// ordered, full band in the same pixel coordinates and must contain x..x+w.
	void shc_drawGradientRectangle (float x, float y, float w, float h, float peak_x,
		float gradient_start, float gradient_end,
		enum shc_ElementId edge_element_id,
		enum shc_ElementId peak_element_id);

	void shc_drawString (float x, float y, const char *string, float scale, enum shc_ElementId element_id);

#endif // STRAFE_HELPER_CUSTOMIZATION_H

#ifdef __cplusplus
} /* extern "C" */
#endif
