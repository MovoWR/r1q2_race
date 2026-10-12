/* Native HUD calls enter a private scoped adapter; public renderer ABI stays intact. */
#include "jump_compat.h"

static struct
{
	qboolean active, layout;
	jump_screen_t saved;
	void (EXPORT *character) (int, int, int);
	void (EXPORT *picture) (int, int, char *);
	void (EXPORT *stretch) (int, int, int, int, char *);
	void (EXPORT *fill) (int, int, int, int, int);
} jump_native;

int CL_JumpViewportWidth (void)
{
	int width = viddef.width, height;
	if (scr_presentation)
		scr_presentation->GetSize (1, &width, &height);
	return width;
}

int CL_JumpViewportHeight (void)
{
	int width, height = viddef.height;
	if (scr_presentation)
		scr_presentation->GetSize (1, &width, &height);
	return height;
}

static void EXPORT CL_JumpNativeChar (int x, int y, int c)
{
	char text[2];
	text[0] = (char)c;
	text[1] = 0;
	SCR_DrawStringEx (x, y, 0, 1, text, 0);
}

static void EXPORT CL_JumpNativePic (int x, int y, char *name)
{
	int width, height;
	re.DrawGetPicSize (&width, &height, name);
	if (width > 0 && height > 0)
		R_DrawStretchPic (x, y, width, height, name);
}

static void EXPORT CL_JumpNativeStretch (int x, int y, int width, int height, char *name)
{
	R_DrawStretchPic (x, y, width, height, name);
}

static void EXPORT CL_JumpNativeFill (int x, int y, int width, int height, int color)
{
	R_DrawFill8 (x, y, width, height, color);
}

static qboolean CL_NativeLayoutBegin (int id, qboolean console)
{
	int width, height;
	float unit;
	if (jump_native.active)
		return false;
	if (!scr_presentation || !scr_rich_presentation)
		return false;
	if (!console && (!CL_JumpEnabled () || !HUD_LayoutCount () || cls.state != ca_active || !cl.frame.valid))
		return false;
	width = CL_JumpViewportWidth ();
	height = CL_JumpViewportHeight ();
	if (width < 64 || height < 48 || viddef.width < 1 || viddef.height < 1)
		return false;
	unit = viddef.width / (float)width;
	jump_native.saved = jump_screen;
	jump_screen.hud_width = viddef.width;
	jump_screen.hud_height = viddef.height;
	jump_screen.hud_scale = unit;
	jump_native.character = re.DrawChar;
	jump_native.picture = re.DrawPic;
	jump_native.stretch = re.DrawStretchPic;
	jump_native.fill = re.DrawFill;
	scr_rich_presentation->Begin ();
	jump_native.active = true;
	re.DrawChar = CL_JumpNativeChar;
	re.DrawPic = CL_JumpNativePic;
	re.DrawStretchPic = CL_JumpNativeStretch;
	re.DrawFill = CL_JumpNativeFill;
	R_SetScale (unit);
	R_ClearColor ();
	R_SetAlpha (console ? 1 : jump_hud_alpha ? Cvar_ClampValue (jump_hud_alpha, 0, 1) :
											   1);
	R_SetFont (Cvar_VariableString (console || id == HL_NOTIFY || id == HL_MESSAGE ? "con_font" : "scr_font"));
	jump_native.layout = id >= 0 && CL_JumpEnabled () && HUD_LayoutCount () && cls.state == ca_active && cl.frame.valid;
	if (jump_native.layout)
		HUD_LayoutBegin (id);
	return true;
}

void CL_JumpLayoutNativeBegin (int id)
{
	if (jump_native.active)
	{
		HUD_LayoutBegin (id);
		return;
	}
	CL_NativeLayoutBegin (id, false);
}

qboolean CL_ConsoleLayoutNativeBegin (int id)
{
	return CL_NativeLayoutBegin (id, true);
}

void CL_JumpLayoutNativeEnd (void)
{
	if (!jump_native.active)
		return;
	if (jump_native.layout)
		HUD_LayoutEnd ();
	re.DrawChar = jump_native.character;
	re.DrawPic = jump_native.picture;
	re.DrawStretchPic = jump_native.stretch;
	re.DrawFill = jump_native.fill;
	if (scr_rich_presentation)
		scr_rich_presentation->End ();
	jump_screen = jump_native.saved;
	jump_native.active = false;
	R_SetScale (1);
	R_ClearColor ();
	R_SetFont (NULL);
}

void CL_JumpLayoutClear (void)
{
	CL_JumpLayoutNativeEnd ();
	HUD_LayoutEnd ();
	CL_JumpEditorCancel ();
}

void CL_JumpLayoutPrepare (void)
{
	CL_JumpPrepareHUD ();
}

void SCR_HudEditorPrepare (void)
{
	CL_JumpLayoutPrepare ();
}

void CL_JumpLayoutRates (void)
{
	char text[64];
	int i;
	for (i = 0; i < 2; i++)
	{
		const hud_layout_item_t *item = HUD_LayoutItem (i ? HL_MOVE_FPS : HL_RENDER_FPS);
		if (!item || !item->visible->intvalue)
			continue;
		HUD_LayoutBegin (i ? HL_MOVE_FPS : HL_RENDER_FPS);
		if (i)
			CL_Mmps_m (text, sizeof (text));
		else
			R_Mfps_m (text, sizeof (text));
		R_ClearColor ();
		R_SetAlpha (Cvar_ClampValue (jump_hud_alpha, 0, 1));
		SCR_DrawString (jump_screen.hud_width - 8, 40 + i * 10, UI_RIGHT, va ("%s: %s %s", i ? "Moves" : "Render", text, i ? "MPS" : "FPS"));
		HUD_LayoutEnd ();
	}
}

qboolean CL_JumpLayoutCustomized (int id)
{
	const hud_layout_item_t *item;
	if (!CL_JumpEnabled ())
		return false;
	item = HUD_LayoutItem (id);
	return item && (item->x->value != 0 || item->y->value != 0 || item->scale->value != 1 || !item->visible->intvalue);
}
