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
/* Native physical-pixel adapters for the imported Jump HUD algorithms. */
#include "jump_compat.h"
#include "sh_netmeter.h"
#include "strafe_helper.h"
#include "sh_efficiency_draw.h"

jump_screen_t jump_screen;
cvar_t *jump_hud_alpha;

static float jump_draw_scale = 1;
static const char *jump_draw_font;

void R_SetFont (const char *font)
{
	jump_draw_font = font;
}

static float jump_view_x, jump_view_y, jump_view_scale = 1;
static uint32 jump_draw_color = U32_WHITE;
static uint32 jump_draw_altcolor = U32_WHITE;
static qboolean jump_draw_altset;

void R_SetAltColor (uint32 color)
{
	jump_draw_altcolor = color;
	jump_draw_altset = true;
}

static struct
{
	qboolean active, hidden, bounded;
	float sx, sy, x, y, ax, ay, left, top, right, bottom;
	float rawleft, rawtop, rawright, rawbottom;
} jump_draw_group;

void R_SetColor (uint32 color)
{
	jump_draw_color = color;
}

void R_ClearColor (void)
{
	jump_draw_color = U32_WHITE;
	jump_draw_altset = false;
}

void R_SetScale (float scale)
{
	jump_draw_scale = scale > 0 && scale <= 1024 ? scale : 1;
}

void R_BeginDrawGroup (float x, float y, qboolean hidden)
{
	memset (&jump_draw_group, 0, sizeof (jump_draw_group));
	jump_draw_group.active = true;
	jump_draw_group.hidden = hidden;
	jump_draw_group.sx = jump_draw_group.sy = 1;
	jump_draw_group.x = x;
	jump_draw_group.y = y;
}

void R_SetDrawGroupScale (float sx, float sy, float x, float y)
{
	jump_draw_group.sx = sx;
	jump_draw_group.sy = sy;
	jump_draw_group.ax = x;
	jump_draw_group.ay = y;
}

static void CL_JumpTransformInternal (float *x, float *y, float *w, float *h,
	qboolean measure)
{
	*x /= jump_draw_scale;
	*y /= jump_draw_scale;
	*w /= jump_draw_scale;
	*h /= jump_draw_scale;
	if (jump_draw_group.active)
	{
		if (measure && !jump_draw_group.bounded)
		{
			jump_draw_group.rawleft = *x;
			jump_draw_group.rawtop = *y;
			jump_draw_group.rawright = *x + *w;
			jump_draw_group.rawbottom = *y + *h;
		}
		else if (measure)
		{
			if (*x < jump_draw_group.rawleft)
				jump_draw_group.rawleft = *x;
			if (*y < jump_draw_group.rawtop)
				jump_draw_group.rawtop = *y;
			if (*x + *w > jump_draw_group.rawright)
				jump_draw_group.rawright = *x + *w;
			if (*y + *h > jump_draw_group.rawbottom)
				jump_draw_group.rawbottom = *y + *h;
		}
		*x = jump_draw_group.x + jump_draw_group.ax + (*x - jump_draw_group.ax) * jump_draw_group.sx;
		*y = jump_draw_group.y + jump_draw_group.ay + (*y - jump_draw_group.ay) * jump_draw_group.sy;
		*w *= jump_draw_group.sx;
		*h *= jump_draw_group.sy;
		if (measure && !jump_draw_group.bounded)
		{
			jump_draw_group.left = *x;
			jump_draw_group.top = *y;
			jump_draw_group.right = *x + *w;
			jump_draw_group.bottom = *y + *h;
			jump_draw_group.bounded = true;
		}
		else if (measure)
		{
			if (*x < jump_draw_group.left)
				jump_draw_group.left = *x;
			if (*y < jump_draw_group.top)
				jump_draw_group.top = *y;
			if (*x + *w > jump_draw_group.right)
				jump_draw_group.right = *x + *w;
			if (*y + *h > jump_draw_group.bottom)
				jump_draw_group.bottom = *y + *h;
		}
	}
	*x = jump_view_x + *x * jump_view_scale;
	*y = jump_view_y + *y * jump_view_scale;
	*w *= jump_view_scale;
	*h *= jump_view_scale;
}

static void CL_JumpTransform (float *x, float *y, float *w, float *h)
{
	CL_JumpTransformInternal (x, y, w, h, true);
}

void R_SetDrawTransform (float x, float y, float scale)
{
	jump_view_x = x;
	jump_view_y = y;
	jump_view_scale = scale >= .01f && scale <= 64 ? scale : 1;
}

void R_SetClipRect (const clipRect_t *clip)
{
	float x, y, w, h;
	float x2, y2;
	int left, top, right, bottom, viewport_width, viewport_height;
	if (!scr_rich_presentation)
		return;
	if (!clip || clip->right <= clip->left || clip->bottom <= clip->top)
	{
		scr_rich_presentation->Clip (0, 0, 0, 0);
		return;
	}
	x = (float)clip->left;
	y = (float)clip->top;
	w = (float)clip->right - (float)clip->left;
	h = (float)clip->bottom - (float)clip->top;
	CL_JumpTransformInternal (&x, &y, &w, &h, false);
	x2 = x + w;
	y2 = y + h;
	if (x2 < x)
	{
		float swap = x;
		x = x2;
		x2 = swap;
	}
	if (y2 < y)
	{
		float swap = y;
		y = y2;
		y2 = swap;
	}
	viewport_width = viddef.width;
	viewport_height = viddef.height;
	if (scr_presentation && scr_presentation->GetSize)
		scr_presentation->GetSize (1, &viewport_width, &viewport_height);
	if (viewport_width < 1 || viewport_width > 65535 ||
		viewport_height < 1 || viewport_height > 65535)
	{
		viewport_width = viddef.width;
		viewport_height = viddef.height;
	}
	x = CLAMP (x, 0.0f, (float)viewport_width);
	y = CLAMP (y, 0.0f, (float)viewport_height);
	x2 = CLAMP (x2, 0.0f, (float)viewport_width);
	y2 = CLAMP (y2, 0.0f, (float)viewport_height);
	left = (int)floor (x);
	top = (int)floor (y);
	right = (int)ceil (x2);
	bottom = (int)ceil (y2);
	scr_rich_presentation->Clip (left, top, right - left, bottom - top);
}

qboolean R_EndDrawGroup (vrect_t *bounds)
{
	qboolean bounded = jump_draw_group.bounded;
	if (bounded && bounds)
	{
		bounds->x = (int)floor (jump_draw_group.left);
		bounds->y = (int)floor (jump_draw_group.top);
		bounds->width = (int)ceil (jump_draw_group.right) - bounds->x;
		bounds->height = (int)ceil (jump_draw_group.bottom) - bounds->y;
	}
	jump_draw_group.active = false;
	jump_draw_group.hidden = false;
	return bounded;
}

qboolean R_EndDrawGroupRaw (vrect_t *bounds, vrect_t *raw)
{
	if (jump_draw_group.bounded && raw)
	{
		raw->x = (int)floor (jump_draw_group.rawleft);
		raw->y = (int)floor (jump_draw_group.rawtop);
		raw->width = (int)ceil (jump_draw_group.rawright) - raw->x;
		raw->height = (int)ceil (jump_draw_group.rawbottom) - raw->y;
	}
	return R_EndDrawGroup (bounds);
}

static void CL_JumpColorFloats (uint32 packed, float *rgba)
{
	color_t color;
	int i;
	color.u32 = packed;
	for (i = 0; i < 4; i++)
		rgba[i] = color.u8[i] / 255.0f;
}

void R_DrawFill32 (float x, float y, float w, float h, uint32 color)
{
	float rgba[4];
	CL_JumpTransform (&x, &y, &w, &h);
	if (!scr_rich_presentation || jump_draw_group.hidden)
		return;
	CL_JumpColorFloats (color, rgba);
	scr_rich_presentation->Fill (x, y, w, h, rgba);
}

int SCR_DrawStringEx (float x, float y, int flags, int max_chars, const char *text, int font)
{
	int length, i;
	float gx, gy, w, h, rgba[4], black[4];
	const char *font_name;
	(void)font;
	if (!text)
		return 0;
	length = (int)strlen (text);
	if (length > max_chars)
		length = max_chars;
	if (flags & UI_CENTER)
		x -= length * CHAR_WIDTH * 0.5f;
	else if (flags & UI_RIGHT)
		x -= length * CHAR_WIDTH;
	CL_JumpColorFloats ((flags & UI_ALTCOLOR) && jump_draw_altset ? jump_draw_altcolor : jump_draw_color, rgba);
	black[0] = black[1] = black[2] = 0;
	black[3] = rgba[3];
	font_name = jump_draw_font ? jump_draw_font : Cvar_VariableString ("scr_font");
	for (i = 0; i < length; i++)
	{
		gx = x + i * CHAR_WIDTH;
		gy = y;
		w = CHAR_WIDTH;
		h = CHAR_HEIGHT;
		CL_JumpTransform (&gx, &gy, &w, &h);
		if (!scr_rich_presentation || jump_draw_group.hidden)
			continue;
		if (flags & UI_DROPSHADOW)
			scr_rich_presentation->Glyph (gx + w / CHAR_WIDTH, gy + h / CHAR_HEIGHT,
				w, h, ((byte)text[i] | ((flags & UI_ALTCOLOR) ? 128 : 0)), font_name, black);
		scr_rich_presentation->Glyph (gx, gy, w, h, ((byte)text[i] | ((flags & UI_ALTCOLOR) ? 128 : 0)), font_name, rgba);
	}
	return length * CHAR_WIDTH;
}

void R_SetAlpha (float alpha)
{
	color_t color;
	color.u32 = jump_draw_color;
	alpha = alpha == alpha ? CLAMP (alpha, 0, 1) : 0;
	color.u8[3] = (byte)(alpha * 255);
	jump_draw_color = color.u32;
}

void R_DrawFill8 (float x, float y, float w, float h, int index)
{
	color_t color, current;
	if (!scr_rich_presentation || !scr_rich_presentation->Color8)
		return;
	current.u32 = jump_draw_color;
	color.u32 = scr_rich_presentation->Color8 (index);
	color.u8[3] = current.u8[3];
	R_DrawFill32 (x, y, w, h, color.u32);
}

void R_DrawStretchPic (float x, float y, float w, float h, const char *pic)
{
	float rgba[4];
	CL_JumpTransform (&x, &y, &w, &h);
	if (!scr_rich_presentation || jump_draw_group.hidden)
		return;
	CL_JumpColorFloats (jump_draw_color, rgba);
	scr_rich_presentation->Pic (x, y, w, h, pic, rgba);
}

void SCR_DrawString (float x, float y, int flags, const char *text)
{
	SCR_DrawStringEx (x, y, flags, MAX_STRING_CHARS, text, 0);
}

void CL_JumpClearDraw (void)
{
	CL_JumpLayoutClear ();
	CL_JumpUIClear ();
	jump_draw_font = NULL;
	R_SetDrawTransform (0, 0, 1);
	jump_draw_scale = 1;
	jump_draw_color = U32_WHITE;
	memset (&jump_draw_group, 0, sizeof (jump_draw_group));
	SH_ClearDrawState ();
	memset (&ns, 0, sizeof (ns));
	CL_JumpClearStats ();
	CL_JumpClearWidgets ();
	SH_NetMeter_Clear ();
}

/* Imported HUD algorithms use the inverse of the renderer pixel multiplier. */
qboolean CL_JumpPrepareHUD (void)
{
	cvar_t *selected;
	int width, height;
	float scale;

	if (!scr_presentation)
		return false;
	selected = Cvar_FindVar ("scr_scale");
	scale = scr_presentation->GetSize (selected ? selected->value : 1, &width, &height);
	if (!(scale >= 1 && scale <= 1024) || width < 64 || width > 0xfff8 ||
		height < 48 || height > 0xfffe)
		return false;
	jump_screen.hud_width = width;
	jump_screen.hud_height = height;
	jump_screen.hud_scale = 1 / scale;
	jump_screen.net_pic = "net";
	return true;
}

void CL_JumpDrawHUD (void)
{
	struct StrafeHelperParams params;
	int width, height;
	float scale;

	if (!CL_JumpEnabled () || cls.state != ca_active || !cl.frame.valid ||
		!scr_presentation || !scr_rich_presentation)
		return;
	CL_JumpRenderFrame ();
	if (!CL_JumpPrepareHUD () || HUD_EditorActive ())
		return;
	width = jump_screen.hud_width;
	height = jump_screen.hud_height;
	scale = jump_screen.hud_scale;
	R_SetScale (scale);
	R_ClearColor ();
	scr_rich_presentation->Begin ();
	CL_JumpUpdateDrawSample (CL_CommandFrameTime ());
	CL_JumpNerdUpdate ();
	params.center = cl_strafeHelperCenter->intvalue;
	params.center_marker = cl_strafeHelperCenterMarker->intvalue;
	params.scale = Cvar_ClampValue (cl_strafeHelperScale, 0.25f, 8);
	params.height = Cvar_ClampValue (cl_strafeHelperHeight, 0, 80);
	params.y = Cvar_ClampValue (cl_strafeHelperY, -height, height);
	params.hud_scale = scale;
	if (cl_drawStrafeHelper->intvalue)
		StrafeHelper_Draw (&params, width, height, 0);
	SH_Ups_Draw (width, height, scale, 0);
	SH_NetMeter_Draw ();
	HUD_LayoutBegin (HL_NERDSTATS);
	SH_NerdStats_Draw ((float)width, (float)height, 0);
	HUD_LayoutEnd ();
	SCR_DrawBindReminders (Cvar_ClampValue (jump_hud_alpha, 0, 1));
	CL_JumpDrawWidgets ();
	CL_JumpLayoutRates ();
	R_ClearColor ();
	R_SetScale (1);
	scr_rich_presentation->End ();
}
