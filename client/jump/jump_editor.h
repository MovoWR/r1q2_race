/* Native HUD editor. Draft settings are visible only in the preview pass. */
#ifndef R1Q2_JUMP_EDITOR_H
#define R1Q2_JUMP_EDITOR_H

#include "jump_layout.h"

typedef enum
{
	HUD_EDIT_UPS,
	HUD_EDIT_STRAFE,
	HUD_EDIT_EFFICIENCY,
	HUD_EDIT_NETWORK,
	HUD_EDIT_LAYOUT_FIRST,
	HUD_EDIT_COUNT = HUD_EDIT_LAYOUT_FIRST + HUD_LAYOUT_MAX
} hud_edit_id_t;

void HUD_EditorInit (void);
void HUD_EditorShutdown (void);
qboolean HUD_EditorActive (void);
/* Physical viewport for the workbench scene; leaves the output alone when closed. */
qboolean HUD_EditorViewport (vrect_t *viewport);
qboolean HUD_EditorPreview (void);
qboolean HUD_EditorSelected (int id);
/* Focus affects preview drawing only; bounds remain available for alignment. */
qboolean HUD_EditorShow (int id);
int HUD_EditorNetworkMode (void);
hud_preview_scenario_t HUD_EditorScenario (void);
qboolean HUD_EditorKey (int key, qboolean down);
qboolean HUD_EditorChar (int key);
void HUD_EditorMouse (int x, int y);
/* Draft values are read only during preview; normal drawing reads live cvars. */
float HUD_EditorValue (const cvar_t *var);
float HUD_EditorClamp (cvar_t *var, float low, float high);
/* Union bounds in scaled HUD coordinates; ignored outside the preview pass. */
void HUD_EditorBounds (hud_edit_id_t id, float x, float y, float w, float h);

void CL_JumpEditorCancel (void);
void CL_JumpEditorFocus (qboolean active);
void CL_JumpEditorReleaseInputs (void);
void CL_JumpEditorDraw (void);
qboolean CL_JumpEditorView (const refdef_t *source, refdef_t *out);
#endif
