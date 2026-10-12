/* Full frozen Q2PRO Race HUD editor, private R1Q2 adaptation.
 * GPL version 2 or later; see COPYING. */
#include "jump_ui_compat.h"
#include "jump_editor_math.h"
#include "strafe_helper.h"
#include "sh_netmeter.h"
extern const char *Cvar_DefaultString (const cvar_t *var);

typedef struct
{
	const char *name;
	hud_edit_id_t owner;
	float low, high;
	cvar_t *var;
	float original, value;
	qboolean changed;
} hud_setting_t;

static const hud_setting_t native_settings[] = {
	{ "hud_ups_scale", HUD_EDIT_UPS, HUD_SCALE_MIN, HUD_SCALE_MAX },
	{ "hud_strafe_scale", HUD_EDIT_STRAFE, HUD_SCALE_MIN, HUD_SCALE_MAX },
	{ "hud_efficiency_scale", HUD_EDIT_EFFICIENCY, HUD_SCALE_MIN, HUD_SCALE_MAX },
	{ "hud_network_scale", HUD_EDIT_NETWORK, HUD_SCALE_MIN, HUD_SCALE_MAX },
	{ "sh_ups", HUD_EDIT_UPS, 0, 1 },
	{ "sh_ups_x", HUD_EDIT_UPS, SH_UPS_X_MIN, SH_UPS_X_MAX },
	{ "sh_ups_y", HUD_EDIT_UPS, SH_UPS_Y_MIN, SH_UPS_Y_MAX },
	{ "sh_ups_scale", HUD_EDIT_UPS, SH_UPS_SCALE_MIN, SH_UPS_SCALE_MAX },
	{ "sh_draw", HUD_EDIT_STRAFE, 0, 1 },
	{ "sh_y", HUD_EDIT_STRAFE, -4000, 4000 },
	{ "sh_height", HUD_EDIT_STRAFE, 4, 80 },
	{ "sh_scale", HUD_EDIT_STRAFE, 0.25f, 8 },
	{ "sh_efficiency", HUD_EDIT_EFFICIENCY, 0, 1 },
	{ "sh_efficiency_x", HUD_EDIT_EFFICIENCY, SH_EFFICIENCY_X_MIN, SH_EFFICIENCY_X_MAX },
	{ "sh_efficiency_y", HUD_EDIT_EFFICIENCY, SH_EFFICIENCY_Y_MIN, SH_EFFICIENCY_Y_MAX },
	{ "sh_efficiency_width", HUD_EDIT_EFFICIENCY, SH_EFFICIENCY_WIDTH_MIN, SH_EFFICIENCY_WIDTH_MAX },
	{ "sh_efficiency_height", HUD_EDIT_EFFICIENCY, SH_EFFICIENCY_HEIGHT_MIN, SH_EFFICIENCY_HEIGHT_MAX },
	{ "sh_efficiency_text_scale", HUD_EDIT_EFFICIENCY, SH_EFFICIENCY_TEXT_SCALE_MIN, SH_EFFICIENCY_TEXT_SCALE_MAX },
	{ "sh_netmeter", HUD_EDIT_NETWORK, 0, 3 },
	{ "sh_lagometer_x", HUD_EDIT_NETWORK, -4000, 4000 },
	{ "sh_lagometer_y", HUD_EDIT_NETWORK, -4000, 4000 },
	{ "sh_netgraph_y", HUD_EDIT_NETWORK, -4000, 4000 },
	{ "sh_netgraph_height", HUD_EDIT_NETWORK, 1, 4000 },
	{ "sh_histogram_x", HUD_EDIT_NETWORK, -4000, 4000 },
	{ "sh_histogram_y", HUD_EDIT_NETWORK, -4000, 4000 },
	{ "sh_histogram_width", HUD_EDIT_NETWORK, 10, 4000 },
	{ "sh_histogram_width_mode", HUD_EDIT_NETWORK, 0, 2 },
	{ "sh_histogram_height", HUD_EDIT_NETWORK, 1, 4000 },
};

enum
{
	HUD_NATIVE_SETTINGS = q_countof (native_settings)
};

static hud_setting_t settings[HUD_NATIVE_SETTINGS + HUD_LAYOUT_MAX * 4];
static int setting_count;

static const struct
{
	const char *name;
	const char *enable;
	const char *key;
} native_widgets[HUD_EDIT_LAYOUT_FIRST] = {
	[HUD_EDIT_UPS] = { "Speed (UPS)", "sh_ups", "ups" },
	[HUD_EDIT_STRAFE] = { "Strafe helper", "sh_draw", "strafe" },
	[HUD_EDIT_EFFICIENCY] = { "Strafe efficiency", "sh_efficiency", "efficiency" },
	[HUD_EDIT_NETWORK] = { "Network monitor", "sh_netmeter", "network" },
};

typedef enum
{
	HUD_BUTTON_APPLY,
	HUD_BUTTON_CANCEL,
	HUD_BUTTON_RESET_SELECTED,
	HUD_BUTTON_SNAP,
	HUD_BUTTON_VISIBLE,
	HUD_BUTTON_PREVIOUS,
	HUD_BUTTON_NEXT,
	HUD_BUTTON_MOVE_LEFT,
	HUD_BUTTON_MOVE_RIGHT,
	HUD_BUTTON_MOVE_UP,
	HUD_BUTTON_MOVE_DOWN,
	HUD_BUTTON_RESET_ALL,
	HUD_BUTTON_UNDO,
	HUD_BUTTON_REDO,
	HUD_BUTTON_LOCK,
	HUD_BUTTON_FOCUS,
	HUD_BUTTON_ALIGN_LEFT,
	HUD_BUTTON_ALIGN_HCENTER,
	HUD_BUTTON_ALIGN_RIGHT,
	HUD_BUTTON_ALIGN_TOP,
	HUD_BUTTON_ALIGN_VCENTER,
	HUD_BUTTON_ALIGN_BOTTOM,
	HUD_BUTTON_GHOSTS,
	HUD_BUTTON_STEP,
	HUD_BUTTON_REFERENCE,
	HUD_BUTTON_CHANGES,
	HUD_BUTTON_KEYS,
	HUD_BUTTON_SCENARIO_LIVE,
	HUD_BUTTON_SCENARIO_RUNNING,
	HUD_BUTTON_SCENARIO_SPECTATING,
	HUD_BUTTON_SCENARIO_VOTING,
	HUD_BUTTON_SCENARIO_SCOREBOARD,
	HUD_BUTTON_SCENARIO_NETWORK,
	HUD_BUTTON_FIT,
	HUD_BUTTON_100,
	HUD_BUTTON_SELECTION,
	HUD_BUTTON_ZOOM_OUT,
	HUD_BUTTON_ZOOM_IN,
	HUD_BUTTON_INFO,
	HUD_BUTTON_EDITOR_VISIBLE,
	HUD_BUTTON_COUNT
} hud_button_t;

typedef enum
{
	HUD_CATEGORY_ALL,
	HUD_CATEGORY_CLIENT,
	HUD_CATEGORY_SERVER,
	HUD_CATEGORY_COUNT
} hud_category_t;

enum
{
	HUD_GROUP_COUNT = 6
};

typedef enum
{
	HUD_EDITOR_AUTO,
	HUD_EDITOR_SHOWN,
	HUD_EDITOR_HIDDEN
} hud_editor_visibility_t;

typedef enum
{
	HUD_FIELD_NONE,
	HUD_FIELD_FILTER,
	HUD_FIELD_X,
	HUD_FIELD_Y,
	HUD_FIELD_SCALE,
	HUD_FIELD_COUNT
} hud_field_t;

static const char *const group_names[] = {
	"Speed and strafing", "Run and map", "Player state", "Messages", "Network", "Diagnostics"
};

static struct
{
	menuFrameWork_t menu;
	qboolean open, preview, dragging, resizing, snap, hide_tools, focus;
	qboolean locked[HUD_EDIT_COUNT], original_locks[HUD_EDIT_COUNT];
	cvar_t *lock_vars[HUD_EDIT_COUNT];
	int history_pos, history_count;
	float guide_x, guide_y;
	int selected, network_mode, servercount, list_first, page_rows;
	hud_category_t category;
	float width, height, scale, mouse_x, mouse_y, grab_x, grab_y;
	float start_width, start_height, start_scale, start_x, start_y;
	float drag_values[q_countof (settings)];
	qboolean drag_changed[q_countof (settings)];
	vrect_t drag_bounds;
	vrect_t bounds[HUD_EDIT_COUNT], rows[HUD_EDIT_COUNT];
	vrect_t buttons[HUD_BUTTON_COUNT], categories[HUD_CATEGORY_COUNT];
	active_t active;
	qboolean ghosts, keys_open, changes_open, align_reference, field_bad, replace_field;
	qboolean collapsed[HUD_GROUP_COUNT];
	/* Per-element preview choices never enter saved settings or undo history. */
	hud_editor_visibility_t editor_visibility[HUD_EDIT_COUNT];
	int reference, step, change_first, held_button;
	qboolean skip_char;
	/* Setting index + 1 for each element's HUD enable cvar; 0 until first use. */
	int enable_setting[HUD_EDIT_COUNT];
	qboolean held_changed;
	unsigned hold_time;
	hud_field_t field;
	inputField_t input, filter;
	vrect_t rail, dock, stage, overlay, fields[HUD_FIELD_COUNT], groups[HUD_GROUP_COUNT];
	vrect_t toggles[HUD_EDIT_COUNT], preview_toggles[HUD_EDIT_COUNT];
	vrect_t change_rows[HUD_EDIT_COUNT], reverts[HUD_EDIT_COUNT];
	/* View state is deliberately excluded from draft settings and undo history. */
	hud_preview_scenario_t scenario;
	qboolean info_expanded, panning;
	int pan_button;
	float preview_zoom, manual_zoom, view_x, view_y, center_x, center_y;
	float pan_start_x, pan_start_y, pan_center_x, pan_center_y;
	qhandle_t backdrop;
} editor;

#define HUD_HISTORY_STATES 65

static struct
{
	float values[q_countof (settings)];
	/* Preview clamping alone is not an edit. */
	qboolean changed[q_countof (settings)];
	qboolean locked[HUD_EDIT_COUNT];
	int selected, network_mode;
} history[HUD_HISTORY_STATES];

static void HUD_HistoryCapture (int slot)
{
	{
		size_t i;
		for (i = 0; i < setting_count; i++)
		{
			history[slot].values[i] = settings[i].value;
			history[slot].changed[i] = settings[i].changed;
		}
	}
	memcpy (history[slot].locked, editor.locked, sizeof (editor.locked));
	history[slot].selected = editor.selected;
	history[slot].network_mode = editor.network_mode;
}

static void HUD_HistoryBegin (void)
{
	if (editor.history_count)
		return;
	editor.history_pos = 0;
	editor.history_count = 1;
	HUD_HistoryCapture (0);
}

static void HUD_HistoryCommit (void)
{
	if (!editor.history_count)
		return;
	int slot = editor.history_pos;
	qboolean changed = memcmp (history[slot].locked, editor.locked, sizeof (editor.locked)) != 0;
	{
		size_t i;
		for (i = 0; i < setting_count; i++)
			changed |= settings[i].value != history[slot].values[i] ||
				settings[i].changed != history[slot].changed[i];
	}
	if (!changed)
		return;
	if (slot == HUD_HISTORY_STATES - 1)
	{
		memmove (history, history + 1, sizeof (history) - sizeof (history[0]));
		slot--;
	}
	editor.history_pos = slot + 1;
	editor.history_count = editor.history_pos + 1;
	HUD_HistoryCapture (editor.history_pos);
}

static void HUD_EndDrag (void)
{
	if (editor.dragging)
		HUD_HistoryCommit ();
	editor.dragging = false;
	editor.guide_x = editor.guide_y = NAN;
}

/* Another element's lock protects geometry this element moves or shares. */
static qboolean HUD_LockedByOther (int id)
{
	/* Changing the helper geometry also moves its attached efficiency display. */
	return (id == HUD_EDIT_STRAFE && editor.locked[HUD_EDIT_EFFICIENCY]) ||
		(id == HUD_EDIT_NETWORK &&
			((editor.network_mode == 1 && editor.locked[HUD_EDIT_LAYOUT_FIRST + HL_NETICON]) ||
				(editor.network_mode == 2 && editor.locked[HUD_EDIT_LAYOUT_FIRST + HL_DEBUGGRAPH])));
}

static qboolean HUD_Locked (int id)
{
	return editor.locked[id] || HUD_LockedByOther (id);
}

static const char *HUD_Name (int id);
static qboolean HUD_Enabled (int id);

static qboolean HUD_ContextAvailable (int layout)
{
	return editor.scenario == HUD_PREVIEW_LIVE ? HUD_LayoutPreviewAvailable (layout) :
												 HUD_LayoutScenarioIncludes (layout, editor.scenario);
}

static qboolean HUD_CanvasItem (int id)
{
	if (editor.editor_visibility[id] == HUD_EDITOR_HIDDEN || (editor.focus && id != editor.selected))
		return false;
	if (editor.editor_visibility[id] == HUD_EDITOR_SHOWN)
		return true;
	if (id >= HUD_EDIT_LAYOUT_FIRST && id != editor.selected &&
		!HUD_ContextAvailable (id - HUD_EDIT_LAYOUT_FIRST))
		return false;
	return editor.ghosts || id == editor.selected || HUD_Enabled (id);
}

static void HUD_SetEditorVisible (int id, qboolean visible)
{
	HUD_EndDrag ();
	editor.panning = false;
	editor.held_button = -1;
	editor.editor_visibility[id] = visible ? HUD_EDITOR_SHOWN : HUD_EDITOR_HIDDEN;
}

qboolean HUD_EditorShow (int id)
{
	return !editor.preview || HUD_CanvasItem (id);
}

static int HUD_Limit (void)
{
	return HUD_EDIT_LAYOUT_FIRST + HUD_LayoutCount ();
}

static qboolean HUD_Editable (int id)
{
	return id < HUD_EDIT_LAYOUT_FIRST || HUD_LayoutEditable (id - HUD_EDIT_LAYOUT_FIRST);
}

static hud_category_t HUD_Category (int id)
{
	if (id < HUD_EDIT_LAYOUT_FIRST)
		return HUD_CATEGORY_CLIENT;
	int layout = id - HUD_EDIT_LAYOUT_FIRST;
	return layout < HL_SERVER_COUNT || layout == HL_SCOREBOARD || layout == HL_OTHER_STATUS ? HUD_CATEGORY_SERVER : HUD_CATEGORY_CLIENT;
}

static int HUD_Group (int id)
{
	if (id < HUD_EDIT_LAYOUT_FIRST)
		return id == HUD_EDIT_NETWORK ? 4 : 0;
	switch (id - HUD_EDIT_LAYOUT_FIRST)
	{
	case HL_SPEED:
		return 0;
	case HL_TIMER:
	case HL_TIMELEFT:
	case HL_ADDEDTIME:
	case HL_MAP:
	case HL_PREVMAP1:
	case HL_PREVMAP2:
	case HL_PREVMAP3:
	case HL_MAPCOUNT:
	case HL_STATUS1:
	case HL_STATUS2:
	case HL_STATUS3:
	case HL_STATUS4:
		return 1;
	case HL_HEALTH:
	case HL_ITEM:
	case HL_INPUTS:
	case HL_TARGET:
	case HL_BIND_REMINDERS:
		return 2;
	case HL_CHAT:
	case HL_MESSAGE:
	case HL_CENTER:
	case HL_NOTIFY:
	case HL_VOTE:
	case HL_SCOREBOARD:
		return 3;
	case HL_NETALERT:
	case HL_NETICON:
	case HL_TURTLE:
	case HL_SERVER_FPS:
	case HL_RENDER_FPS:
	case HL_MOVE_FPS:
	case HL_DEBUGGRAPH:
		return 4;
	default:
		return 5;
	}
}

static qboolean HUD_Matches (int id)
{
	if (!HUD_Editable (id) || (editor.category != HUD_CATEGORY_ALL && HUD_Category (id) != editor.category))
		return false;
	if (!editor.filter.text[0])
		return true;
	const char *key =
		id < HUD_EDIT_LAYOUT_FIRST ? native_widgets[id].key : HUD_LayoutItem (id - HUD_EDIT_LAYOUT_FIRST)->key;
	return Q_stristr (HUD_Name (id), editor.filter.text) || Q_stristr (key, editor.filter.text);
}

static qboolean HUD_ListItem (int id)
{
	return HUD_Matches (id) && (!editor.collapsed[HUD_Group (id)] || editor.filter.text[0]);
}

/* Rows are compact; selection and renderer bounds retain their stable HUD IDs. */
static int HUD_Row (int id)
{
	int row = 0;
	{
		int group;
		for (group = 0; group < HUD_GROUP_COUNT; group++)
		{
			int i;
			for (i = 0; i < HUD_Limit (); i++)
			{
				if (i == id && HUD_Group (i) == group)
					return row;
				if (HUD_Group (i) == group && HUD_ListItem (i))
					row++;
			}
		}
	}
	return row;
}

static int HUD_Count (void)
{
	return HUD_Row (HUD_Limit ());
}

static int HUD_Entry (int row)
{
	{
		int group;
		for (group = 0; group < HUD_GROUP_COUNT; group++)
		{
			int i;
			for (i = 0; i < HUD_Limit (); i++)
				if (HUD_Group (i) == group && HUD_ListItem (i) && row-- == 0)
					return i;
		}
	}
	return editor.selected;
}

static void HUD_Select (int id)
{
	if (id != editor.selected)
		editor.reference = editor.selected;
	editor.selected = id;
	editor.field = HUD_FIELD_NONE;
	editor.collapsed[HUD_Group (id)] = false;
}

/* Row geometry is stale once list contents change; it returns on the next draw. */
static void HUD_InvalidateList (void)
{
	editor.list_first = 0;
	memset (editor.rows, 0, sizeof (editor.rows));
	memset (editor.toggles, 0, sizeof (editor.toggles));
	memset (editor.preview_toggles, 0, sizeof (editor.preview_toggles));
}

/* Make an element's row visible without changing which element is selected. */
static void HUD_Reveal (int id)
{
	if (HUD_ListItem (id))
		return;
	editor.collapsed[HUD_Group (id)] = false;
	if (!HUD_ListItem (id))
	{
		editor.category = HUD_CATEGORY_ALL;
		IF_Clear (&editor.filter);
	}
	HUD_InvalidateList ();
}

static void HUD_SetCategory (hud_category_t category)
{
	HUD_EndDrag ();
	editor.category = category;
	if (HUD_Count () && !HUD_ListItem (editor.selected))
		HUD_Select (HUD_Entry (0));
	HUD_InvalidateList ();
}

static void HUD_SelectStep (int step)
{
	HUD_EndDrag ();
	int count = HUD_Count ();
	if (!count)
		return;
	int row = HUD_ListItem (editor.selected) ? HUD_Row (editor.selected) : (step < 0 ? count : -1);
	HUD_Select (HUD_Entry (Q_clip (row + step, 0, count - 1)));
}

static float HUD_Value (const char *name);

/* Input can change a draft several times before the next preview supplies bounds. */
static void HUD_UpdateValue (hud_setting_t *setting, float value)
{
	int id = setting->owner - HUD_EDIT_LAYOUT_FIRST;
	const hud_layout_item_t *item = id >= 0 ? HUD_LayoutItem (id) : NULL;
	vrect_t *r = &editor.bounds[setting->owner];
	if (item && r->width && value != setting->value)
	{
		if (!strcmp (setting->name, item->x->name))
		{
			r->x += Q_rint (value - setting->value);
		}
		else if (!strcmp (setting->name, item->y->name))
		{
			r->y += Q_rint (value - setting->value);
		}
		else if (!strcmp (setting->name, item->scale->name) && setting->value > 0)
		{
			float px, py, ratio = value / setting->value;
			float x = HUD_Value (item->x->name), y = HUD_Value (item->y->name);
			HUD_LayoutScaleAnchor (id, &px, &py);
			if (id != HL_DEBUGGRAPH)
			{
				r->x = Q_rint (px + (r->x - px - x) * ratio + x);
				r->width = max (1, Q_rint (r->width * ratio));
			}
			r->y = Q_rint (py + (r->y - py - y) * ratio + y);
			r->height = max (1, Q_rint (r->height * ratio));
		}
	}
	setting->value = value;
}

static void HUD_HistoryRestore (int slot, int selected)
{
	{
		size_t i;
		for (i = 0; i < setting_count; i++)
		{
			HUD_UpdateValue (&settings[i], history[slot].values[i]);
			settings[i].changed = history[slot].changed[i];
		}
	}
	memcpy (editor.locked, history[slot].locked, sizeof (editor.locked));
	editor.network_mode = history[slot].network_mode;
	editor.history_pos = slot;
	editor.selected = selected;
	/* Keep the undone element selected even if a filter or collapsed group hid it. */
	HUD_Reveal (selected);
	editor.guide_x = editor.guide_y = NAN;
}

static void HUD_Undo (void)
{
	HUD_EndDrag ();
	if (editor.history_pos > 0)
		HUD_HistoryRestore (editor.history_pos - 1, history[editor.history_pos].selected);
}

static void HUD_Redo (void)
{
	HUD_EndDrag ();
	if (editor.history_pos + 1 < editor.history_count)
		HUD_HistoryRestore (editor.history_pos + 1, history[editor.history_pos + 1].selected);
}

static const char *HUD_Name (int id)
{
	return id < HUD_EDIT_LAYOUT_FIRST ? native_widgets[id].name : HUD_LayoutItem (id - HUD_EDIT_LAYOUT_FIRST)->name;
}

static const char *HUD_Enable (int id)
{
	return id < HUD_EDIT_LAYOUT_FIRST ? native_widgets[id].enable : HUD_LayoutItem (id - HUD_EDIT_LAYOUT_FIRST)->visible->name;
}

static qboolean HUD_CanResize (void)
{
	return HUD_Editable (editor.selected) && !HUD_Locked (editor.selected);
}

qboolean HUD_EditorSelected (int id)
{
	return editor.preview && editor.selected == id;
}

static hud_setting_t *HUD_Setting (const char *name)
{
	{
		size_t i;
		for (i = 0; i < setting_count; i++)
			if (!strcmp (settings[i].name, name))
				return &settings[i];
	}
	return NULL;
}

static float HUD_Value (const char *name)
{
	return HUD_Setting (name)->value;
}

/* Resolved once per open session; the settings table is fixed while editing. */
static qboolean HUD_Enabled (int id)
{
	if (!editor.enable_setting[id])
		editor.enable_setting[id] = (int)(HUD_Setting (HUD_Enable (id)) - settings) + 1;
	return settings[editor.enable_setting[id] - 1].value != 0;
}

/* Resets/reverts also touch modes other than the currently displayed one. */
static qboolean HUD_SharedSettingLocked (const char *name)
{
	if (!strcmp (name, "sh_lagometer_x") || !strcmp (name, "sh_lagometer_y"))
		return editor.locked[HUD_EDIT_LAYOUT_FIRST + HL_NETICON];
	if (!strcmp (name, "sh_netgraph_height"))
		return editor.locked[HUD_EDIT_LAYOUT_FIRST + HL_DEBUGGRAPH];
	return false;
}

static void HUD_Set (const char *name, float value)
{
	if (HUD_SharedSettingLocked (name))
		return;
	hud_setting_t *s = HUD_Setting (name);
	HUD_UpdateValue (s, HudEdit_Clamp (value, s->low, s->high));
	s->changed = s->value != s->original;
}

qboolean HUD_EditorActive (void)
{
	return CL_JumpEnabled () && editor.open && uis.activeMenu == &editor.menu;
}

qboolean HUD_EditorPreview (void)
{
	return CL_JumpEnabled () && editor.preview;
}

hud_preview_scenario_t HUD_EditorScenario (void)
{
	return editor.scenario;
}

int HUD_EditorNetworkMode (void)
{
	return editor.network_mode;
}

float HUD_EditorValue (const cvar_t *var)
{
	if (editor.preview)
	{
		size_t i;
		for (i = 0; i < setting_count; i++)
			if (settings[i].var == var)
				return settings[i].value;
	}
	return var->value;
}

float HUD_EditorClamp (cvar_t *var, float low, float high)
{
	if (editor.preview)
		return HudEdit_Clamp (HUD_EditorValue (var), low, high);
	return Cvar_ClampValue (var, low, high);
}

/* Multiple calls union the bar, text, and border of one widget. */
void HUD_EditorBounds (hud_edit_id_t id, float x, float y, float w, float h)
{
	if (!editor.preview || id < 0 || id >= HUD_EDIT_COUNT)
		return;
	vrect_t *r = &editor.bounds[id];
	int left = (int)floorf (x), top = (int)floorf (y);
	int right = (int)ceilf (x + w), bottom = (int)ceilf (y + h);
	if (r->width)
	{
		left = min (left, r->x);
		top = min (top, r->y);
		right = max (right, r->x + r->width);
		bottom = max (bottom, r->y + r->height);
	}
	*r = (vrect_t){ left, top, max (1, right - left), max (1, bottom - top) };
}

static void HUD_Pop (menuFrameWork_t *menu)
{
	editor.open = editor.preview = editor.dragging = editor.panning = false;
}

static void HUD_Size (menuFrameWork_t *menu)
{
	menu->mins[0] = menu->mins[1] = 0;
	menu->maxs[0] = uis.width;
	menu->maxs[1] = uis.height;
}

static void HUD_Reset (void)
{
	if (HUD_Locked (editor.selected))
		return;
	{
		size_t i;
		for (i = 0; i < setting_count; i++)
			if (settings[i].owner == editor.selected)
				HUD_Set (settings[i].name, strtof (Cvar_DefaultString (settings[i].var), NULL));
	}
	editor.dragging = false;
}

static void HUD_ResetAll (void)
{
	{
		size_t i;
		for (i = 0; i < setting_count; i++)
			if (HUD_Editable (settings[i].owner) && !HUD_Locked (settings[i].owner))
				HUD_Set (settings[i].name, strtof (Cvar_DefaultString (settings[i].var), NULL));
	}
	editor.dragging = false;
}

static void HUD_Apply (void)
{
	HUD_EndDrag ();
	{
		size_t i;
		for (i = 0; i < setting_count; i++)
			if (settings[i].changed)
				Cvar_SetValue (settings[i].var->name, settings[i].value);
	}
	{
		int i;
		for (i = 0; i < HUD_Limit (); i++)
			if (editor.lock_vars[i] && editor.locked[i] != editor.original_locks[i])
				Cvar_SetValue (editor.lock_vars[i]->name, (float)editor.locked[i]);
	}
	UI_PopMenu ();
}

static void HUD_Toggle (void)
{
	if (HUD_Locked (editor.selected))
		return;
	const char *name = HUD_Enable (editor.selected);
	HUD_Set (name, HUD_Value (name) ? 0 : editor.selected == HUD_EDIT_NETWORK ? editor.network_mode :
																				1);
}

static void HUD_NetworkAxes (const char **x, const char **y)
{
	*x = editor.network_mode == 1 ? "sh_lagometer_x" :
		editor.network_mode == 3  ? "sh_histogram_x" :
									NULL;
	*y = editor.network_mode == 1 ? "sh_lagometer_y" :
		editor.network_mode == 3  ? "sh_histogram_y" :
									"sh_netgraph_y";
}

static void HUD_Move (float dx, float dy, qboolean snap)
{
	editor.guide_x = editor.guide_y = NAN;
	if (HUD_Locked (editor.selected))
		return;
	vrect_t r = editor.bounds[editor.selected];
	float x = r.x + dx, y = r.y + dy;
	if (snap)
	{
		hud_edit_snap_axis_t sx, sy;
		/* Six screen pixels at the current preview zoom, expressed in HUD units. */
		float tolerance = 6 * editor.scale / max (editor.preview_zoom, .01f);
		HudEdit_SnapAxisInit (&sx, x, r.width, editor.width, tolerance);
		HudEdit_SnapAxisInit (&sy, y, r.height, editor.height, tolerance);
		{
			int i;
			for (i = 0; i < HUD_Limit (); i++)
			{
				const vrect_t *other = &editor.bounds[i];
				if (i == editor.selected || !HUD_Editable (i) || !other->width || !HUD_CanvasItem (i))
					continue;
				/* Do not snap an attached widget to geometry that moves with it. */
				if (editor.selected == HUD_EDIT_STRAFE && i == HUD_EDIT_EFFICIENCY)
					continue;
				HudEdit_SnapAxisTarget (&sx, other->x, other->width);
				HudEdit_SnapAxisTarget (&sy, other->y, other->height);
			}
		}
		x = HudEdit_SnapAxisResult (&sx, &editor.guide_x);
		y = HudEdit_SnapAxisResult (&sy, &editor.guide_y);
		if (editor.selected == HUD_EDIT_STRAFE ||
			(editor.selected == HUD_EDIT_NETWORK && editor.network_mode == 2))
		{
			x = r.x;
			editor.guide_x = NAN;
		}
	}
	else
	{
		x = HudEdit_Clamp (x, 0, editor.width - r.width);
		y = HudEdit_Clamp (y, 0, editor.height - r.height);
	}
	dx = x - r.x;
	dy = y - r.y;
	if (editor.selected >= HUD_EDIT_LAYOUT_FIRST)
	{
		const hud_layout_item_t *item = HUD_LayoutItem (editor.selected - HUD_EDIT_LAYOUT_FIRST);
		float old_x = HUD_Value (item->x->name), old_y = HUD_Value (item->y->name);
		HUD_Set (item->x->name, old_x + dx);
		HUD_Set (item->y->name, old_y + dy);
		return;
	}
	switch (editor.selected)
	{
	case HUD_EDIT_UPS:
		HUD_Set ("sh_ups_x", HUD_Value ("sh_ups_x") + dx);
		HUD_Set ("sh_ups_y", HUD_Value ("sh_ups_y") + dy);
		break;
	case HUD_EDIT_STRAFE:
		HUD_Set ("sh_y", HUD_Value ("sh_y") + dy);
		break;
	case HUD_EDIT_EFFICIENCY:
	{
		float h = HUD_Value ("sh_efficiency_height") * HUD_Value ("hud_efficiency_scale");
		float hh = HUD_Value ("sh_height") * HUD_Value ("hud_strafe_scale");
		float helper_top = (editor.height - hh) * 0.5f + HUD_Value ("sh_y");
		float offset = HUD_Value ("sh_efficiency_y");
		float top = offset >= 0 ? helper_top + hh + offset : helper_top + offset - h;
		HUD_Set ("sh_efficiency_x", HUD_Value ("sh_efficiency_x") + dx);
		HUD_Set ("sh_efficiency_y", HudEdit_EfficiencyOffset (top + dy, h, helper_top, hh));
		break;
	}
	case HUD_EDIT_NETWORK:
	{
		const char *xn, *yn;
		HUD_NetworkAxes (&xn, &yn);
		if (xn)
			HUD_Set (xn, HudEdit_EdgeOffset (x, editor.width, r.width, HUD_Value (xn)));
		HUD_Set (yn, HudEdit_EdgeOffset (y, editor.height, r.height, HUD_Value (yn)));
		break;
	}
	default:
		break;
	}
}

static qboolean HUD_EfficiencyTextOnly (void)
{
	const char *style = cl_strafehelperEffStyle ? cl_strafehelperEffStyle->string : NULL;
	return style && (!Q_stricmp (style, "text") || !strcmp (style, "2"));
}

static const char *HUD_ScaleSetting (void)
{
	static const char *const native_scales[] = {
		"hud_ups_scale", "hud_strafe_scale", "hud_efficiency_scale", "hud_network_scale"
	};
	return editor.selected < HUD_EDIT_LAYOUT_FIRST ? native_scales[editor.selected] :
													 HUD_LayoutItem (editor.selected - HUD_EDIT_LAYOUT_FIRST)->scale->name;
}

/* Keep a grouped item's chosen corner fixed while changing its visual size.
 * Full-width graphs grow upward from their bottom edge. Native overlays retain
 * their own center/edge anchors and their existing dimension controls. */
static void HUD_SetVisualScale (float value)
{
	if (HUD_Locked (editor.selected))
		return;
	const char *name = HUD_ScaleSetting ();
	float previous = HUD_Value (name);
	vrect_t r = editor.bounds[editor.selected];
	HUD_Set (name, value);
	if (editor.selected < HUD_EDIT_LAYOUT_FIRST || previous <= 0)
		return;
	int id = editor.selected - HUD_EDIT_LAYOUT_FIRST;
	const hud_layout_item_t *item = HUD_LayoutItem (id);
	if (!r.width)
		return;
	float px, py, ratio = HUD_Value (name) / previous;
	HUD_LayoutScaleAnchor (id, &px, &py);
	float x = HUD_Value (item->x->name), y = HUD_Value (item->y->name);
	if (id != HL_DEBUGGRAPH)
		HUD_Set (item->x->name, x + (r.x - px - x) * (1 - ratio));
	HUD_Set (item->y->name, y + (r.y + (id == HL_DEBUGGRAPH ? r.height : 0) - py - y) * (1 - ratio));
}

static void HUD_StartResize (void)
{
	vrect_t r = editor.bounds[editor.selected];
	editor.start_width = r.width;
	editor.start_height = r.height;
	editor.start_scale = HUD_Value (HUD_ScaleSetting ());
	switch (editor.selected)
	{
	case HUD_EDIT_UPS:
		editor.start_scale = HUD_Value ("sh_ups_scale");
		break;
	case HUD_EDIT_STRAFE:
		editor.start_scale = HUD_Value ("sh_scale");
		editor.start_height = HUD_Value ("sh_height");
		break;
	case HUD_EDIT_EFFICIENCY:
		if (HUD_EfficiencyTextOnly ())
		{
			editor.start_scale = HUD_Value ("sh_efficiency_text_scale");
		}
		else
		{
			editor.start_width = HUD_Value ("sh_efficiency_width");
			editor.start_height = HUD_Value ("sh_efficiency_height");
		}
		break;
	case HUD_EDIT_NETWORK:
	{
		float visual = HUD_Value ("hud_network_scale");
		if (editor.network_mode != 1)
		{
			editor.start_width /= visual;
			editor.start_height /= visual;
		}
		break;
	}
	default:
		break;
	}
}

static void HUD_Resize (float dx, float dy)
{
	if (!HUD_CanResize ())
		return;
	if (editor.selected >= HUD_EDIT_LAYOUT_FIRST ||
		(editor.selected == HUD_EDIT_NETWORK && editor.network_mode == 1))
	{
		float w = editor.start_width, h = editor.start_height;
		qboolean full_width = editor.selected == HUD_EDIT_LAYOUT_FIRST + HL_DEBUGGRAPH;
		float delta = full_width ? dy / fmaxf (1, h) : (w * dx + h * dy) / fmaxf (1, w * w + h * h);
		HUD_SetVisualScale (editor.start_scale * fmaxf (0.1f, 1 + delta));
		return;
	}
	switch (editor.selected)
	{
	case HUD_EDIT_UPS:
		HUD_Set ("sh_ups_scale", editor.start_scale * fmaxf (0.1f, (editor.start_width + dx) / fmaxf (1, editor.start_width)));
		break;
	case HUD_EDIT_STRAFE:
		HUD_Set ("sh_scale", editor.start_scale * fmaxf (0.1f, (editor.start_width + 2 * dx) / fmaxf (1, editor.start_width)));
		if (dy != 0)
			HUD_Set ("sh_height", editor.start_height + dy / HUD_Value ("hud_strafe_scale"));
		break;
	case HUD_EDIT_EFFICIENCY:
		if (HUD_EfficiencyTextOnly ())
		{
			HUD_Set ("sh_efficiency_text_scale", editor.start_scale * fmaxf (0.1f, (editor.start_width + dx) / fmaxf (1, editor.start_width)));
		}
		else
		{
			float visual = HUD_Value ("hud_efficiency_scale");
			if (dx != 0)
				HUD_Set ("sh_efficiency_width", fminf (editor.width, editor.start_width + 2 * dx / visual));
			if (dy != 0)
				HUD_Set ("sh_efficiency_height", editor.start_height + dy / visual);
		}
		break;
	case HUD_EDIT_NETWORK:
	{
		float visual = HUD_Value ("hud_network_scale");
		if (editor.network_mode == 2 && dy != 0)
			HUD_Set ("sh_netgraph_height", fminf (editor.height, editor.start_height + dy / visual));
		else if (editor.network_mode == 3)
		{
			if (dx != 0)
			{
				HUD_Set ("sh_histogram_width_mode", 0);
				HUD_Set ("sh_histogram_width", fmaxf (10, fminf (editor.width, editor.start_width + dx / visual)));
			}
			if (dy != 0)
				HUD_Set ("sh_histogram_height", fminf (editor.height, editor.start_height + dy / visual));
		}
		break;
	}
	default:
		break;
	}
}

static void HUD_Align (hud_button_t button)
{
	vrect_t r = editor.bounds[editor.selected];
	float dx = 0, dy = 0;
	vrect_t base = { 0, 0, (int)editor.width, (int)editor.height };
	if (editor.align_reference && editor.reference >= 0 && editor.reference < HUD_Limit () &&
		editor.reference != editor.selected && editor.bounds[editor.reference].width > 0)
		base = editor.bounds[editor.reference];

	switch (button)
	{
	case HUD_BUTTON_ALIGN_LEFT:
		dx = base.x - r.x;
		break;
	case HUD_BUTTON_ALIGN_HCENTER:
		dx = base.x + (base.width - r.width) * 0.5f - r.x;
		break;
	case HUD_BUTTON_ALIGN_RIGHT:
		dx = base.x + base.width - r.width - r.x;
		break;
	case HUD_BUTTON_ALIGN_TOP:
		dy = base.y - r.y;
		break;
	case HUD_BUTTON_ALIGN_VCENTER:
		dy = base.y + (base.height - r.height) * 0.5f - r.y;
		break;
	case HUD_BUTTON_ALIGN_BOTTOM:
		dy = base.y + base.height - r.height - r.y;
		break;
	default:
		return;
	}
	HUD_Move (dx, dy, false);
}

/* Pad and arrow movement use the selected step; Shift multiplies it by ten. */
static float HUD_PadStep (void)
{
	return max (1, editor.step) * (Key_IsDown (K_SHIFT) ? 10 : 1);
}

static void HUD_PadDelta (int button, float *dx, float *dy)
{
	float step = HUD_PadStep ();
	*dx = button == HUD_BUTTON_MOVE_LEFT ? -step : button == HUD_BUTTON_MOVE_RIGHT ? step :
																					 0;
	*dy = button == HUD_BUTTON_MOVE_UP ? -step : button == HUD_BUTTON_MOVE_DOWN ? step :
																				  0;
}

static void HUD_Nudge (float dx, float dy, qboolean resize)
{
	HUD_HistoryBegin ();
	if (resize)
	{
		HUD_StartResize ();
		HUD_Resize (dx, dy);
	}
	else
	{
		HUD_Move (dx, dy, false);
	}
	HUD_HistoryCommit ();
}

/* Invalidate geometry before a view change can receive another click. */
static void HUD_ClearToolsHits (void)
{
	memset (editor.buttons, 0, sizeof (editor.buttons));
	memset (editor.rows, 0, sizeof (editor.rows));
	memset (editor.categories, 0, sizeof (editor.categories));
	memset (editor.fields, 0, sizeof (editor.fields));
	memset (editor.groups, 0, sizeof (editor.groups));
	memset (editor.toggles, 0, sizeof (editor.toggles));
	memset (editor.preview_toggles, 0, sizeof (editor.preview_toggles));
	memset (editor.change_rows, 0, sizeof (editor.change_rows));
	memset (editor.reverts, 0, sizeof (editor.reverts));
	editor.overlay = (vrect_t){ 0 };
}

/* One pass over the draft; the workbench needs every element's flag each frame. */
static void HUD_DirtyFlags (qboolean dirty[HUD_EDIT_COUNT])
{
	{
		int id;
		for (id = 0; id < HUD_EDIT_COUNT; id++)
			dirty[id] = editor.locked[id] != editor.original_locks[id];
	}
	{
		int i;
		for (i = 0; i < setting_count; i++)
			dirty[settings[i].owner] |= settings[i].changed;
	}
}

static void HUD_Revert (int id)
{
	HUD_HistoryBegin ();
	/* Reverting restores the element's own lock, never another element's protection. */
	qboolean protected_values = HUD_LockedByOther (id);
	{
		int i;
		for (i = 0; i < setting_count; i++)
			if (settings[i].owner == id && !protected_values && !HUD_SharedSettingLocked (settings[i].name))
			{
				hud_setting_t *s = &settings[i];
				HUD_UpdateValue (s, s->low == HUD_SCALE_MIN && s->high == HUD_SCALE_MAX && !isfinite (s->original) ? 1 : HudEdit_Clamp (s->original, s->low, s->high));
				settings[i].changed = false;
			}
	}
	editor.locked[id] = editor.original_locks[id];
	HUD_HistoryCommit ();
}

static void HUD_WorkbenchLayout (void);
static void HUD_ViewCommand (hud_button_t button);

static void HUD_Command (hud_button_t button)
{
	HUD_EndDrag ();
	editor.panning = false;
	editor.held_button = -1;
	if (button >= HUD_BUTTON_SCENARIO_LIVE && button <= HUD_BUTTON_SCENARIO_NETWORK)
	{
		editor.scenario = (hud_preview_scenario_t)(button - HUD_BUTTON_SCENARIO_LIVE);
		return;
	}
	if (button >= HUD_BUTTON_FIT && button <= HUD_BUTTON_ZOOM_IN)
	{
		HUD_ViewCommand (button);
		return;
	}
	switch (button)
	{
	case HUD_BUTTON_INFO:
		editor.info_expanded = !editor.info_expanded;
		HUD_ClearToolsHits ();
		HUD_WorkbenchLayout ();
		return;
	case HUD_BUTTON_EDITOR_VISIBLE:
		HUD_SetEditorVisible (editor.selected, !HUD_CanvasItem (editor.selected));
		return;
	case HUD_BUTTON_GHOSTS:
		editor.ghosts = !editor.ghosts;
		return;
	case HUD_BUTTON_STEP:
		editor.step = editor.step == 1 ? 5 : editor.step == 5 ? 10 :
																1;
		return;
	case HUD_BUTTON_REFERENCE:
		editor.align_reference = !editor.align_reference;
		return;
	case HUD_BUTTON_CHANGES:
		editor.changes_open = !editor.changes_open;
		editor.keys_open = false;
		HUD_ClearToolsHits ();
		return;
	case HUD_BUTTON_KEYS:
		editor.keys_open = !editor.keys_open;
		editor.changes_open = false;
		HUD_ClearToolsHits ();
		return;
	case HUD_BUTTON_APPLY:
		HUD_Apply ();
		return;
	case HUD_BUTTON_CANCEL:
		UI_PopMenu ();
		return;
	case HUD_BUTTON_SNAP:
		editor.snap = !editor.snap;
		return;
	case HUD_BUTTON_PREVIOUS:
		HUD_SelectStep (-1);
		return;
	case HUD_BUTTON_NEXT:
		HUD_SelectStep (1);
		return;
	case HUD_BUTTON_UNDO:
		HUD_Undo ();
		return;
	case HUD_BUTTON_REDO:
		HUD_Redo ();
		return;
	case HUD_BUTTON_FOCUS:
		editor.focus = !editor.focus;
		return;
	default:
		break;
	}

	HUD_HistoryBegin ();
	switch (button)
	{
	case HUD_BUTTON_RESET_SELECTED:
		HUD_Reset ();
		break;
	case HUD_BUTTON_VISIBLE:
		HUD_Toggle ();
		break;
	case HUD_BUTTON_RESET_ALL:
		HUD_ResetAll ();
		break;
	case HUD_BUTTON_LOCK:
		editor.locked[editor.selected] = !editor.locked[editor.selected];
		break;
	case HUD_BUTTON_ALIGN_LEFT:
	case HUD_BUTTON_ALIGN_HCENTER:
	case HUD_BUTTON_ALIGN_RIGHT:
	case HUD_BUTTON_ALIGN_TOP:
	case HUD_BUTTON_ALIGN_VCENTER:
	case HUD_BUTTON_ALIGN_BOTTOM:
		HUD_Align (button);
		break;
	case HUD_BUTTON_MOVE_LEFT:
	case HUD_BUTTON_MOVE_RIGHT:
	case HUD_BUTTON_MOVE_UP:
	case HUD_BUTTON_MOVE_DOWN:
	{
		float dx, dy;
		HUD_PadDelta (button, &dx, &dy);
		HUD_Move (dx, dy, false);
		break;
	}
	default:
		break;
	}
	HUD_HistoryCommit ();
}

static qboolean HUD_Hit (const vrect_t *r, float x, float y)
{
	return r->width > 0 && x >= r->x && y >= r->y &&
		x < r->x + r->width && y < r->y + r->height;
}

/* Fit the command bar at small resolutions and respect the menu UI scale. */
static float HUD_ToolsZoom (void)
{
	if (uis.width <= 0 || uis.height <= 0)
		return 1;
	return min (1.4f, min (uis.width / 1000.0f, uis.height / 562.5f));
}

static float HUD_ToolsMouse (int axis)
{
	return uis.mouseCoords[axis] / HUD_ToolsZoom ();
}

static void HUD_WorkbenchLayout (void)
{
	float z = HUD_ToolsZoom (), ui_scale = uis.scale > 0 ? uis.scale : 1;
	int width = uis.width / z, height = uis.height / z;
	int rail = 280, dock = 94 + (editor.info_expanded ? 64 : 22);
	editor.rail = (vrect_t){ 0, 0, rail, height - dock };
	editor.dock = (vrect_t){ 0, height - dock, width, dock };
	if (width <= 0 || height <= 0)
	{
		editor.stage = (vrect_t){ 0 };
		editor.preview_zoom = 1;
		editor.view_x = editor.view_y = 0;
		return;
	}
	int top = editor.hide_tools ? 0 : 30;
	float fit = editor.hide_tools ? 1 :
									min ((width - rail) / (float)width, (height - dock - top) / (float)height);
	editor.stage = (vrect_t){ Q_rint (width * (1 - fit) * z / ui_scale), Q_rint (top * z / ui_scale),
		Q_rint (width * fit * z / ui_scale), Q_rint (height * fit * z / ui_scale) };
	editor.preview_zoom = editor.manual_zoom > 0 ? editor.manual_zoom : fit;
	if (!editor.manual_zoom)
	{
		editor.center_x = uis.width * editor.scale / ui_scale / 2;
		editor.center_y = uis.height * editor.scale / ui_scale / 2;
	}
	else if (editor.width > 0 && editor.height > 0)
	{
		/* Zoom and pan share this clamp, so a later pan cannot jump. */
		editor.center_x = Q_clipf (editor.center_x, 0, editor.width);
		editor.center_y = Q_clipf (editor.center_y, 0, editor.height);
	}
	editor.view_x = editor.stage.x + editor.stage.width / 2.0f - editor.center_x * editor.preview_zoom / editor.scale;
	editor.view_y = editor.stage.y + editor.stage.height / 2.0f - editor.center_y * editor.preview_zoom / editor.scale;
}

/* Anchor a zoom at a physical pointer position; toolbar controls use the center. */
static void HUD_ZoomAt (float zoom, float x, float y)
{
	HUD_WorkbenchLayout ();
	float point_x = (x - editor.view_x) * editor.scale / editor.preview_zoom;
	float point_y = (y - editor.view_y) * editor.scale / editor.preview_zoom;
	editor.manual_zoom = Q_clipf (zoom, .25f, 4.0f);
	editor.center_x = point_x + (editor.stage.x + editor.stage.width / 2.0f - x) * editor.scale / editor.manual_zoom;
	editor.center_y = point_y + (editor.stage.y + editor.stage.height / 2.0f - y) * editor.scale / editor.manual_zoom;
	HUD_WorkbenchLayout ();
}

static void HUD_ViewCommand (hud_button_t button)
{
	HUD_WorkbenchLayout ();
	if (button == HUD_BUTTON_FIT)
	{
		editor.manual_zoom = 0;
	}
	else if (button == HUD_BUTTON_SELECTION)
	{
		vrect_t r = editor.bounds[editor.selected];
		if (r.width <= 0 || r.height <= 0)
			return;
		editor.manual_zoom = Q_clipf (min (editor.stage.width * editor.scale / (r.width + 32),
										  editor.stage.height * editor.scale / (r.height + 32)),
			.25f, 4.0f);
		editor.center_x = r.x + r.width / 2.0f;
		editor.center_y = r.y + r.height / 2.0f;
	}
	else
	{
		float zoom = button == HUD_BUTTON_100 ? 1 : editor.preview_zoom * (button == HUD_BUTTON_ZOOM_IN ? 1.25f : .8f);
		HUD_ZoomAt (zoom, editor.stage.x + editor.stage.width / 2.0f, editor.stage.y + editor.stage.height / 2.0f);
	}
	HUD_WorkbenchLayout ();
}

static qboolean HUD_PointerInStage (void)
{
	if (uis.scale <= 0)
		return false;
	return HUD_Hit (&editor.stage, uis.mouseCoords[0] / uis.scale, uis.mouseCoords[1] / uis.scale);
}

static void HUD_StartPan (int button)
{
	if (!HUD_PointerInStage () || CL_JumpWindowActive () != ACT_ACTIVATED)
		return;
	HUD_EndDrag ();
	editor.held_button = -1;
	editor.panning = true;
	editor.pan_button = button;
	editor.manual_zoom = editor.preview_zoom;
	editor.pan_center_x = editor.center_x;
	editor.pan_center_y = editor.center_y;
	editor.pan_start_x = uis.mouseCoords[0] / uis.scale;
	editor.pan_start_y = uis.mouseCoords[1] / uis.scale;
}

qboolean HUD_EditorViewport (vrect_t *viewport)
{
	if (!HUD_EditorActive ())
		return false;
	HUD_WorkbenchLayout ();
	*viewport = editor.stage;
	return true;
}

static qboolean HUD_ToolsVisible (void)
{
	return !editor.hide_tools;
}

void HUD_EditorMouse (int x, int y)
{
	if (!HUD_EditorActive ())
		return;
	HUD_WorkbenchLayout ();
	if (editor.panning)
	{
		if (!Key_IsDown (editor.pan_button) || CL_JumpWindowActive () != ACT_ACTIVATED ||
			(editor.pan_button == K_MOUSE1 && !Key_IsDown (K_SPACE)))
		{
			editor.panning = false;
		}
		else
		{
			editor.center_x = editor.pan_center_x - (x - editor.pan_start_x) * editor.scale / editor.preview_zoom;
			editor.center_y = editor.pan_center_y - (y - editor.pan_start_y) * editor.scale / editor.preview_zoom;
			HUD_WorkbenchLayout ();
		}
	}
	editor.mouse_x = (x - editor.view_x) * editor.scale / editor.preview_zoom;
	editor.mouse_y = (y - editor.view_y) * editor.scale / editor.preview_zoom;
	if (!Key_IsDown (K_MOUSE1) || CL_JumpWindowActive () != ACT_ACTIVATED)
	{
		HUD_EndDrag ();
		editor.held_button = -1;
	}
	if (!editor.dragging)
		return;
	/* Mouse events can arrive several times before the next rendered frame. */
	{
		size_t i;
		for (i = 0; i < setting_count; i++)
		{
			settings[i].value = editor.drag_values[i];
			settings[i].changed = editor.drag_changed[i];
		}
	}
	vrect_t previous_bounds = editor.bounds[editor.selected];
	editor.bounds[editor.selected] = editor.drag_bounds;
	if (editor.resizing)
		HUD_Resize (editor.mouse_x - editor.start_x, editor.mouse_y - editor.start_y);
	else
	{
		vrect_t r = editor.bounds[editor.selected];
		HUD_Move (editor.mouse_x - editor.grab_x - r.x,
			editor.mouse_y - editor.grab_y - r.y,
			editor.snap && !Key_IsDown (K_ALT));
	}
	if (editor.selected < HUD_EDIT_LAYOUT_FIRST)
		editor.bounds[editor.selected] = previous_bounds;
}

static qboolean HUD_FieldEnabled (hud_field_t field)
{
	if (field == HUD_FIELD_FILTER)
		return true;
	if (HUD_Locked (editor.selected))
		return false;
	if (field == HUD_FIELD_SCALE)
		return HUD_ScaleSetting () != NULL;
	if (field == HUD_FIELD_X &&
		(editor.selected == HUD_EDIT_STRAFE || (editor.selected == HUD_EDIT_NETWORK && editor.network_mode == 2)))
		return false;
	return editor.bounds[editor.selected].width > 0;
}

static void HUD_FocusField (hud_field_t field)
{
	if (!HUD_FieldEnabled (field))
		return;
	HUD_EndDrag ();
	editor.held_button = -1;
	editor.field = field;
	editor.field_bad = false;
	editor.replace_field = field != HUD_FIELD_FILTER;
	IF_Init (&editor.input, 8, 24);
	if (field == HUD_FIELD_X || field == HUD_FIELD_Y)
		IF_Replace (&editor.input, va ("%d", field == HUD_FIELD_X ? editor.bounds[editor.selected].x : editor.bounds[editor.selected].y));
	else if (field == HUD_FIELD_SCALE)
		IF_Replace (&editor.input, va ("%.2f", HUD_Value (HUD_ScaleSetting ())));
}

static qboolean HUD_CommitField (void)
{
	if (editor.field <= HUD_FIELD_FILTER)
	{
		editor.field = HUD_FIELD_NONE;
		return true;
	}
	char *end;
	float value = strtof (editor.input.text, &end);
	qboolean parsed = end != editor.input.text;
	while (*end == ' ')
		end++;
	if (!parsed || *end || !isfinite (value))
	{
		editor.field_bad = true;
		return false;
	}
	HUD_HistoryBegin ();
	if (HUD_FieldEnabled (editor.field))
	{
		if (editor.field == HUD_FIELD_SCALE)
			HUD_SetVisualScale (value);
		else
		{
			vrect_t r = editor.bounds[editor.selected];
			/* Clamp before rounding so arbitrary input never reaches an int cast. */
			value = Q_rint (HudEdit_Clamp (value, 0, editor.field == HUD_FIELD_X ? editor.width : editor.height));
			HUD_Move (editor.field == HUD_FIELD_X ? value - r.x : 0,
				editor.field == HUD_FIELD_Y ? value - r.y : 0, false);
		}
	}
	HUD_HistoryCommit ();
	editor.field = HUD_FIELD_NONE;
	editor.field_bad = false;
	return true;
}

qboolean HUD_EditorChar (int key)
{
	if (!HUD_EditorActive ())
		return false;
	/* Keys are positional but chars follow the keyboard layout, so the char
     * after a field shortcut need not equal its key (QWERTZ 'y' types 'z'). */
	if (editor.skip_char)
	{
		editor.skip_char = false;
		return true;
	}
	if (editor.field == HUD_FIELD_NONE)
		return true;
	inputField_t *field = editor.field == HUD_FIELD_FILTER ? &editor.filter : &editor.input;
	if (editor.replace_field && key >= 32 && key < 127)
	{
		IF_Clear (field);
		editor.replace_field = false;
	}
	if (IF_CharEvent (field, key))
	{
		editor.field_bad = false;
		if (editor.field == HUD_FIELD_FILTER)
			HUD_InvalidateList ();
	}
	return true;
}

/* Follow HUD_Draw's preview order; StrafeHelper_DrawPreview draws efficiency first.
 * selected_hit: the pointer is over the selected element or its resize corner. */
static int HUD_CanvasHit (float x, float y, qboolean selected_hit)
{
	static const int native_order[HUD_EDIT_LAYOUT_FIRST] = {
		HUD_EDIT_EFFICIENCY, HUD_EDIT_STRAFE, HUD_EDIT_UPS, HUD_EDIT_NETWORK
	};
	int top_unlocked = -1, top_locked = -1;
	{
		int order;
		for (order = 0; order < HUD_Limit (); order++)
		{
			int i = order < HUD_EDIT_LAYOUT_FIRST ? native_order[order] : order;
			if (!HUD_Editable (i) || !HUD_CanvasItem (i) || !HUD_Hit (&editor.bounds[i], x, y))
				continue;
			if (HUD_Locked (i))
				top_locked = i;
			else
				top_unlocked = i;
		}
	}
	if (selected_hit && !HUD_Locked (editor.selected))
		return editor.selected;
	if (top_unlocked >= 0)
		return top_unlocked;
	return selected_hit ? editor.selected : top_locked;
}

qboolean HUD_EditorKey (int key, qboolean down)
{
	if (!HUD_EditorActive ())
		return false;
	if (!down)
	{
		editor.skip_char = false;
		if (key == editor.pan_button || key == K_SPACE)
			editor.panning = false;
		if (key == K_MOUSE1)
		{
			HUD_EndDrag ();
			editor.held_button = -1;
		}
		return true;
	}
	if (key != editor.pan_button && key != K_SPACE)
		editor.panning = false;
	if (key != K_MOUSE1)
	{
		HUD_EndDrag ();
		editor.held_button = -1;
	}
	if (editor.field != HUD_FIELD_NONE && key != K_MOUSE1)
	{
		inputField_t *field = editor.field == HUD_FIELD_FILTER ? &editor.filter : &editor.input;
		if (key == K_ESCAPE)
		{
			editor.field = HUD_FIELD_NONE;
			editor.field_bad = false;
			return true;
		}
		if (key == K_ENTER)
		{
			if (editor.field == HUD_FIELD_FILTER)
			{
				if (HUD_Count () && !HUD_ListItem (editor.selected))
					HUD_Select (HUD_Entry (0));
				editor.field = HUD_FIELD_NONE;
			}
			else
				HUD_CommitField ();
			return true;
		}
		if (editor.field == HUD_FIELD_FILTER && (key == K_DOWNARROW || key == K_UPARROW))
		{
			HUD_SelectStep (key == K_DOWNARROW ? 1 : -1);
			editor.field = HUD_FIELD_FILTER;
			return true;
		}
		if (key == K_TAB)
		{
			HUD_CommitField ();
			return true;
		}
		if (Key_IsDown (K_CTRL) && key == 'a')
		{
			editor.replace_field = true;
			return true;
		}
		if (editor.replace_field && (key == K_BACKSPACE || key == K_DEL))
		{
			IF_Clear (field);
			editor.replace_field = false;
		}
		else if (IF_KeyEvent (field, key))
		{
			editor.replace_field = false;
		}
		editor.field_bad = false;
		if (editor.field == HUD_FIELD_FILTER)
			HUD_InvalidateList ();
		return true;
	}
	if (key == K_ESCAPE)
	{
		if (editor.keys_open || editor.changes_open)
		{
			editor.keys_open = editor.changes_open = false;
			HUD_ClearToolsHits ();
		}
		else
			UI_PopMenu ();
		return true;
	}
	if (editor.keys_open)
	{
		if (key == K_MOUSE1 || key == '?' || (key == '/' && Key_IsDown (K_SHIFT)))
		{
			editor.keys_open = false;
			HUD_ClearToolsHits ();
		}
		return true;
	}
	if (!editor.changes_open && editor.field == HUD_FIELD_NONE)
	{
		if (key == K_MOUSE3 || (key == K_MOUSE1 && Key_IsDown (K_SPACE)))
		{
			HUD_StartPan (key);
			return true;
		}
		if (Key_IsDown (K_CTRL) && (key == K_MWHEELUP || key == K_MWHEELDOWN) && HUD_PointerInStage ())
		{
			HUD_ZoomAt (editor.preview_zoom * (key == K_MWHEELUP ? 1.25f : .8f),
				uis.mouseCoords[0] / uis.scale, uis.mouseCoords[1] / uis.scale);
			return true;
		}
	}
	if (Key_IsDown (K_CTRL) && key == 'z')
	{
		if (Key_IsDown (K_SHIFT))
			HUD_Redo ();
		else
			HUD_Undo ();
		return true;
	}
	if (Key_IsDown (K_CTRL) && key == 'y')
	{
		HUD_Redo ();
		return true;
	}
	if (key == K_ENTER)
	{
		HUD_Apply ();
		return true;
	}
	if (key == K_TAB)
	{
		int count = HUD_Count ();
		if (count)
		{
			/* From an unlisted selection, Tab starts at the first row and Shift+Tab at the last. */
			int row = HUD_ListItem (editor.selected) ? HUD_Row (editor.selected) : Key_IsDown (K_SHIFT) ? 0 :
																										  -1;
			HUD_Select (HUD_Entry ((row + (Key_IsDown (K_SHIFT) ? count - 1 : 1) + count) % count));
		}
	}
	else if (key == K_MWHEELDOWN || key == K_MWHEELUP || key == K_PGDN || key == K_PGUP)
	{
		int step = (key == K_PGDN || key == K_PGUP) ? max (1, editor.page_rows) : 1;
		if (key == K_MWHEELUP || key == K_PGUP)
			step = -step;
		if (editor.changes_open && HUD_Hit (&editor.overlay, HUD_ToolsMouse (0), HUD_ToolsMouse (1)))
			editor.change_first = max (0, editor.change_first + step);
		else
			HUD_SelectStep (step);
	}
	else if (key >= '1' && key <= '3')
		HUD_SetCategory ((hud_category_t)(key - '1'));
	else if (key == '?' || (key == '/' && Key_IsDown (K_SHIFT)))
		HUD_Command (HUD_BUTTON_KEYS);
	else if (key == '/' || key == 'x' || key == 'y' || key == 's')
	{
		HUD_FocusField (key == '/' ? HUD_FIELD_FILTER : key == 'x' ? HUD_FIELD_X :
				key == 'y'										   ? HUD_FIELD_Y :
																	 HUD_FIELD_SCALE);
		editor.skip_char = true;
	}
	else if (key == 'r')
		HUD_Command (Key_IsDown (K_SHIFT) ? HUD_BUTTON_RESET_ALL : HUD_BUTTON_RESET_SELECTED);
	else if (key == 'n')
		HUD_Command (HUD_BUTTON_SNAP);
	else if (key == 'g')
		HUD_Command (HUD_BUTTON_GHOSTS);
	else if (key == 'p')
		HUD_Command (HUD_BUTTON_CHANGES);
	else if (key == 'e')
		HUD_Command (HUD_BUTTON_EDITOR_VISIBLE);
	else if (key == 'v')
		HUD_Command (HUD_BUTTON_VISIBLE);
	else if (key == 'l')
		HUD_Command (HUD_BUTTON_LOCK);
	else if (key == 'f')
		HUD_Command (HUD_BUTTON_FOCUS);
	else if (key == 'h')
	{
		editor.hide_tools = !editor.hide_tools;
		HUD_ClearToolsHits ();
		HUD_WorkbenchLayout ();
	}
	else if (key == K_LEFTARROW || key == K_RIGHTARROW || key == K_UPARROW || key == K_DOWNARROW)
	{
		float step = HUD_PadStep ();
		HUD_Nudge (key == K_LEFTARROW ? -step : key == K_RIGHTARROW ? step :
																	  0,
			key == K_UPARROW ? -step : key == K_DOWNARROW ? step :
															0,
			Key_IsDown (K_CTRL));
	}
	else if (key == K_MOUSE1)
	{
		HUD_EndDrag ();
		editor.held_button = -1;
		float ux = HUD_ToolsMouse (0), uy = HUD_ToolsMouse (1);
		if (!editor.hide_tools)
		{
			if (HUD_Hit (&editor.buttons[HUD_BUTTON_CANCEL], ux, uy))
			{
				HUD_Command (HUD_BUTTON_CANCEL);
				return true;
			}
			{
				int i;
				for (i = HUD_FIELD_FILTER; i < HUD_FIELD_COUNT; i++)
					if (HUD_Hit (&editor.fields[i], ux, uy))
					{
						if (editor.field != i && HUD_CommitField ())
							HUD_FocusField ((hud_field_t)i);
						return true;
					}
			}
			if (!HUD_CommitField ())
				return true;
			if (editor.changes_open && HUD_Hit (&editor.overlay, ux, uy))
			{
				{
					int i;
					for (i = 0; i < HUD_Limit (); i++)
					{
						if (HUD_Hit (&editor.reverts[i], ux, uy))
						{
							HUD_Revert (i);
							HUD_ClearToolsHits ();
							return true;
						}
						if (HUD_Hit (&editor.change_rows[i], ux, uy))
						{
							HUD_Select (i);
							HUD_Reveal (i);
							return true;
						}
					}
				}
				return true;
			}
			{
				int i;
				for (i = 0; i < HUD_CATEGORY_COUNT; i++)
					if (HUD_Hit (&editor.categories[i], ux, uy))
					{
						HUD_SetCategory ((hud_category_t)i);
						return true;
					}
			}
			{
				int i;
				for (i = 0; i < HUD_BUTTON_COUNT; i++)
				{
					if (!HUD_Hit (&editor.buttons[i], ux, uy))
						continue;
					float before[q_countof (settings)];
					{
						int j;
						for (j = 0; j < setting_count; j++)
							before[j] = settings[j].value;
					}
					HUD_Command ((hud_button_t)i);
					if (i >= HUD_BUTTON_MOVE_LEFT && i <= HUD_BUTTON_MOVE_DOWN)
					{
						editor.held_changed = false;
						{
							int j;
							for (j = 0; j < setting_count; j++)
								editor.held_changed |= before[j] != settings[j].value;
						}
						editor.held_button = i;
						editor.hold_time = com_localTime + 260;
					}
					return true;
				}
			}
			{
				int i;
				for (i = 0; i < HUD_GROUP_COUNT; i++)
					if (HUD_Hit (&editor.groups[i], ux, uy))
					{
						editor.collapsed[i] = !editor.collapsed[i];
						editor.list_first = 0;
						HUD_ClearToolsHits ();
						return true;
					}
			}
			{
				int i;
				for (i = 0; i < HUD_Limit (); i++)
				{
					if (!HUD_ListItem (i))
						continue;
					if (HUD_Hit (&editor.preview_toggles[i], ux, uy))
					{
						qboolean visible = !HUD_CanvasItem (i);
						HUD_Select (i);
						HUD_SetEditorVisible (i, visible);
						return true;
					}
					if (HUD_Hit (&editor.toggles[i], ux, uy))
					{
						HUD_Select (i);
						HUD_Command (HUD_BUTTON_VISIBLE);
						return true;
					}
					if (HUD_Hit (&editor.rows[i], ux, uy))
					{
						HUD_Select (i);
						return true;
					}
				}
			}
			if (HUD_Hit (&editor.rail, ux, uy) || HUD_Hit (&editor.dock, ux, uy))
				return true;
		}
		if (editor.stage.width > 0)
		{
			if (!HUD_PointerInStage ())
				return true;
			editor.mouse_x = (uis.mouseCoords[0] / uis.scale - editor.view_x) * editor.scale / editor.preview_zoom;
			editor.mouse_y = (uis.mouseCoords[1] / uis.scale - editor.view_y) * editor.scale / editor.preview_zoom;
		}
		if (editor.mouse_x < 0 || editor.mouse_y < 0 || editor.mouse_x >= editor.width ||
			editor.mouse_y >= editor.height)
			return true;
		vrect_t r = editor.bounds[editor.selected];
		int handle = max (4, Q_rint (8 * editor.scale / editor.preview_zoom));
		vrect_t corner = { r.x + r.width - handle, r.y + r.height - handle, handle * 2, handle * 2 };
		qboolean selected_shown = HUD_CanvasItem (editor.selected);
		editor.resizing = selected_shown && HUD_Hit (&corner, editor.mouse_x, editor.mouse_y) && HUD_CanResize ();
		int hit = HUD_CanvasHit (editor.mouse_x, editor.mouse_y,
			selected_shown && (HUD_Hit (&r, editor.mouse_x, editor.mouse_y) || editor.resizing));
		if (hit < 0)
			return true;
		HUD_Select (hit);
		HUD_Reveal (hit);
		if (HUD_Locked (hit))
			return true;
		r = editor.bounds[hit];
		HUD_HistoryBegin ();
		editor.dragging = true;
		editor.grab_x = editor.mouse_x - r.x;
		editor.grab_y = editor.mouse_y - r.y;
		editor.start_x = editor.mouse_x;
		editor.start_y = editor.mouse_y;
		editor.drag_bounds = r;
		{
			int i;
			for (i = 0; i < setting_count; i++)
			{
				editor.drag_values[i] = settings[i].value;
				editor.drag_changed[i] = settings[i].changed;
			}
		}
		HUD_StartResize ();
	}
	return true;
}

static void HUD_Outline (const vrect_t *r, uint32 color)
{
	if (!r->width)
		return;
	R_DrawFill32 (r->x, r->y, r->width, 1, color);
	R_DrawFill32 (r->x, r->y + r->height - 1, r->width, 1, color);
	R_DrawFill32 (r->x, r->y, 1, r->height, color);
	R_DrawFill32 (r->x + r->width - 1, r->y, 1, r->height, color);
}

/* All toolbox controls share the same hover and active feedback. */
static void HUD_Button (vrect_t *rect, int x, int y, int width, const char *label, qboolean active)
{
	*rect = (vrect_t){ x, y, width, 18 };
	qboolean hover = HUD_Hit (rect, HUD_ToolsMouse (0), HUD_ToolsMouse (1));
	uint32 color = active ? MakeColor (48, 91, 102, 255) : MakeColor (34, 44, 53, 255);
	if (hover)
		color = MakeColor (66, 89, 104, 255);
	R_DrawFill32 (x, y, width, 18, color);
	if (active)
		R_DrawFill32 (x, y + 17, width, 1, MakeColor (115, 218, 214, 255));
	UI_DrawString (x + width / 2, y + 5, UI_CENTER, label);
}

static void HUD_WorkbenchField (hud_field_t field, int x, int y, int width)
{
	vrect_t *rect = &editor.fields[field];
	*rect = (vrect_t){ x, y, width, 20 };
	qboolean active = editor.field == field, enabled = HUD_FieldEnabled (field);
	R_DrawFill32 (x, y, width, 20, MakeColor (14, 22, 29, 255));
	HUD_Outline (rect, active ? (editor.field_bad ? MakeColor (179, 104, 95, 255) : MakeColor (115, 218, 214, 255)) : MakeColor (45, 63, 76, 255));
	R_SetColor (enabled ? MakeColor (226, 237, 250, 255) : MakeColor (111, 130, 148, 255));
	if (field == HUD_FIELD_FILTER)
	{
		if (active || editor.filter.text[0])
			IF_Draw (&editor.filter, x + 5, y + 6, active ? UI_DRAWCURSOR : 0, uis.fontHandle);
		else
			UI_DrawString (x + 5, y + 6, UI_LEFT, "/ filter elements");
	}
	else if (active)
	{
		editor.input.visibleChars = max (1, (width - 8) / CHAR_WIDTH);
		IF_Draw (&editor.input, x + 4, y + 6, UI_DRAWCURSOR, uis.fontHandle);
	}
	else
		UI_DrawString (x + 4, y + 6, UI_LEFT, !enabled ? "--" : field == HUD_FIELD_SCALE ? va ("%.2f", HUD_Value (HUD_ScaleSetting ())) :
																						   va ("%d", field == HUD_FIELD_X ? editor.bounds[editor.selected].x : editor.bounds[editor.selected].y));
	R_ClearColor ();
}

typedef struct
{
	const char *contents, *appears;
} hud_info_t;

static const hud_info_t native_info[HUD_EDIT_LAYOUT_FIRST] = {
	[HUD_EDIT_UPS] = { "Local speed in units per second.", "When the local speed display has player movement data." },
	[HUD_EDIT_STRAFE] = { "Strafe guidance bar, zones and aim markers.", "While the helper is enabled and movement data is available." },
	[HUD_EDIT_EFFICIENCY] = { "Efficiency percentage, bar and marker; positioned relative to the strafe helper.", "When the helper and efficiency display are enabled and a measured value is available." },
	[HUD_EDIT_NETWORK] = { "Lagometer, netgraph or histogram; the histogram can also show ping text.", "During a network connection; live sampling is unavailable during demo playback." },
};
static const hud_info_t layout_info[HL_BUILTIN_COUNT] = {
	[HL_HEALTH] = { "Health value and Health label move together.", "When the Grish server supplies a nonzero health field." },
	[HL_ITEM] = { "The selected weapon/item icon supplied by the server.", "When the Grish server supplies an item icon." },
	[HL_SPEED] = { "Server speed number and Speed label; independent of local UPS.", "When the Grish server supplies speed." },
	[HL_TARGET] = { "The player name supplied for identification or chase display.", "When the server supplies a target player name." },
	[HL_TIMER] = { "Run time: seconds, decimal point, tenths and Time label.", "While the recognized Grish status bar is active." },
	[HL_INPUTS] = { "Server-provided movement/action icons; all keys move as one group.", "When the server reports the corresponding inputs." },
	[HL_SERVER_FPS] = { "Reported player or replay FPS and label; this is not measured local rendering FPS.", "When the server supplies reported FPS." },
	[HL_VOTE] = { "Four server voting lines move together.", "During a server vote." },
	[HL_MAPCOUNT] = { "Available map count and Maps label.", "When the server supplies the map count." },
	[HL_STATUS1] = { "Server team/mode text, such as Easy, Hard or Observer.", "While the server supplies this status line." },
	[HL_STATUS2] = { "First dynamic run-info line. Its exact meaning is controlled by the Grish server.", "During the server's race, replay, checkpoint or lap states." },
	[HL_STATUS3] = { "Second dynamic run-info line. Its exact meaning is controlled by the Grish server.", "During the server's race, replay, checkpoint or lap states." },
	[HL_STATUS4] = { "Third dynamic run-info line. Its exact meaning is controlled by the Grish server.", "During the server's race, replay, checkpoint or lap states." },
	[HL_MAP] = { "Current map name supplied by the server.", "While the recognized Grish status bar is active." },
	[HL_PREVMAP1] = { "Most recent previous map name.", "When the server includes its map history." },
	[HL_PREVMAP2] = { "Second previous map name.", "When the server includes its map history." },
	[HL_PREVMAP3] = { "Third previous map name.", "When the server includes its map history." },
	[HL_ADDEDTIME] = { "Positive or negative adjustment to map time.", "When the server supplies a map-time adjustment." },
	[HL_TIMELEFT] = { "Remaining map time and Time label.", "While the recognized Grish status bar is active." },
	[HL_CHAT] = { "Chat-history lines and their fade behavior.", "After chat messages arrive, with chat history enabled in client settings." },
	[HL_CENTER] = { "Center-printed messages such as checkpoint announcements.", "While a center message is active and has not timed out." },
	[HL_NOTIFY] = { "Recent console notification lines.", "While notifications are active; client menus temporarily hide them." },
	[HL_MESSAGE] = { "Chat input prompt, typed text and cursor.", "While typing a chat message outside client menus." },
	[HL_NETALERT] = { "Packet-loss, jitter and ping-spike alerts; independent of graph mode.", "During a network incident, with alerts enabled in client settings." },
	[HL_NETICON] = { "Legacy ping graph and blinking connection icon share one 48 x 48 area.", "With the legacy ping graph enabled, or while a connection backlog warning is active." },
	[HL_TURTLE] = { "Frame, prediction, packet-drop and suppression warnings.", "When the client detects a frame/prediction problem and warnings are enabled." },
	[HL_INVENTORY] = { "Inventory panel, item counts, hotkeys and selection cursor.", "When the server opens the inventory panel." },
	[HL_SCOREBOARD] = { "Grish scoreboard/server menu: its rows, labels and icons move together.", "When scores or a server menu are open." },
	[HL_DEBUGGRAPH] = { "Classic network, timing or debug graph; all modes share this group.", "When a classic graph is enabled in client settings." },
	[HL_RENDER_FPS] = { "Measured local rendering frames per second.", "During active gameplay with this layout element enabled." },
	[HL_MOVE_FPS] = { "Measured local movement updates per second.", "During active gameplay with this layout element enabled." },
	[HL_BIND_REMINDERS] = { "Selected key bindings and their action labels move together.", "During active gameplay when enabled and at least one selected key has a binding." },
};

static const hud_info_t *HUD_Info (int id)
{
	static const hud_info_t custom = { "Custom text drawn from its named client macro or setting.",
		"When the registered custom text object produces output." };
	static const hud_info_t excluded = { "This element is outside the editor scope.", "Not editable here." };
	if (id < HUD_EDIT_LAYOUT_FIRST)
		return &native_info[id];
	int layout = id - HUD_EDIT_LAYOUT_FIRST;
	if (layout >= HL_BUILTIN_COUNT)
		return &custom;
	return layout_info[layout].contents ? &layout_info[layout] : &excluded;
}

/* Read draft feature settings where available; other feature gates remain live. */
static float HUD_Option (const char *name, float fallback)
{
	hud_setting_t *s = HUD_Setting (name);
	if (s)
		return s->value;
	cvar_t *var = Cvar_FindVar (name);
	return var && isfinite (var->value) ? var->value : fallback;
}

typedef enum
{
	HUD_STATUS_DISABLED,
	HUD_STATUS_WAITING,
	HUD_STATUS_VISIBLE,
	HUD_STATUS_PREVIEW
} hud_status_t;

static hud_status_t HUD_Status (int id, const char **reason)
{
	if (!HUD_Enabled (id))
	{
		*reason = "Disabled in HUD. Use Enable in HUD to restore it; editor visibility is separate.";
		return HUD_STATUS_DISABLED;
	}
	if (HUD_Option ("scr_draw2d", 2) <= 0)
	{
		*reason = "The client HUD is turned off in client settings.";
		return HUD_STATUS_DISABLED;
	}
	int layout = id - HUD_EDIT_LAYOUT_FIRST;
	const char *gate = NULL;
	if (id == HUD_EDIT_EFFICIENCY)
		gate = "sh_draw";
	else if (layout == HL_CHAT)
		gate = "scr_chathud";
	else if (layout == HL_NETALERT)
		gate = "sh_netalert";
	else if (layout == HL_TURTLE)
		gate = "scr_showturtle";
	if (gate && !HUD_Option (gate, 1))
	{
		*reason = id == HUD_EDIT_EFFICIENCY ? "Enable the strafe helper to display its attached efficiency element." :
			layout == HL_CHAT				? "Chat history is turned off in client settings." :
			layout == HL_NETALERT			? "Network alerts are turned off in client settings." :
											  "Frame/prediction warnings are turned off in client settings.";
		return HUD_STATUS_DISABLED;
	}
	if (layout == HL_DEBUGGRAPH && !HUD_Option ("scr_netgraph", 0) && !HUD_Option ("scr_timegraph", 0) &&
		!HUD_Option ("scr_debuggraph", 0))
	{
		*reason = "Enable a classic network, time or debug graph in client settings.";
		return HUD_STATUS_DISABLED;
	}
	if (layout >= 0 && layout < HL_SERVER_COUNT && HUD_Option ("scr_draw2d", 2) <= 1)
	{
		*reason = "Server status-bar drawing is turned off in client settings.";
		return HUD_STATUS_DISABLED;
	}
	if (editor.scenario != HUD_PREVIEW_LIVE)
	{
		qboolean included = id < HUD_EDIT_LAYOUT_FIRST || HUD_ContextAvailable (layout);
		*reason = included ? "Scenario sample only. Live HUD data and saved settings are unchanged." :
							 "Outside this scenario. Use Show in Editor to arrange a sample.";
		return included ? HUD_STATUS_PREVIEW : HUD_STATUS_WAITING;
	}
	if (cls.state != ca_active)
	{
		*reason = "Load a map for live HUD data. The canvas uses sample content.";
		return HUD_STATUS_WAITING;
	}
	if (id < HUD_EDIT_LAYOUT_FIRST)
	{
		if (id == HUD_EDIT_NETWORK && (!cls.netchan.protocol || cl.attractloop))
		{
			*reason = "Live network samples require a connection and are unavailable in demos.";
			return HUD_STATUS_WAITING;
		}
		*reason = "Sample values are shown while editing; live measurements resume when the editor closes.";
		return HUD_STATUS_PREVIEW;
	}
	if (HUD_LayoutCaptured (layout))
	{
		*reason = "The live group is active. The canvas uses representative sample content.";
		return HUD_STATUS_VISIBLE;
	}
	*reason = layout == HL_NOTIFY || layout == HL_MESSAGE ? "Client menus hide this live group. Use Show in Editor to arrange a sample." :
		layout == HL_NETALERT							  ? "Live alerts resume after leaving the editor. Use Show in Editor to arrange a sample." :
															"No live output this frame. See Appears above; use Show in Editor to arrange a sample.";
	return HUD_STATUS_WAITING;
}

static const char *HUD_StatusLabel (hud_status_t status)
{
	static const char *const labels[] = { "Disabled", "Waiting for event", "Visible", "Preview only" };
	return labels[status];
}

static uint32 HUD_StatusColor (hud_status_t status)
{
	static const uint32 colors[] = { 0xffa69483u, 0xff66bce7u,
		0xffd2d79au, 0xffe5be9eu };
	return colors[status];
}

static const char *HUD_EditDescription (int id)
{
	if (editor.locked[id])
		return "Locked. Press L to unlock this element.";
	if (id == HUD_EDIT_STRAFE && editor.locked[HUD_EDIT_EFFICIENCY])
		return "Placement is protected by locked Strafe efficiency. Unlock that element first.";
	if (id == HUD_EDIT_NETWORK && editor.network_mode == 1 && editor.locked[HUD_EDIT_LAYOUT_FIRST + HL_NETICON])
		return "Shared position is protected by locked Ping graph / connection icon. Unlock that element first.";
	if (id == HUD_EDIT_NETWORK && editor.network_mode == 2 && editor.locked[HUD_EDIT_LAYOUT_FIRST + HL_DEBUGGRAPH])
		return "Shared height is protected by locked Classic network / debug graph. Unlock that element first.";
	switch (id)
	{
	case HUD_EDIT_UPS:
		return "X / Y position and visual scale. Resize adjusts text size. HUD enable/disable and lock.";
	case HUD_EDIT_STRAFE:
		return "Y position and visual scale; resize adjusts angle scale / bar height. The helper stays centered.";
	case HUD_EDIT_EFFICIENCY:
		return HUD_EfficiencyTextOnly () ? "Position relative to helper and visual scale; resize adjusts text size. HUD enable/disable and lock." : "Position relative to helper and visual scale; resize adjusts bar width / height. HUD enable/disable and lock.";
	case HUD_EDIT_NETWORK:
		return editor.network_mode == 1 ? "X / Y position and visual scale. Position is shared with the legacy ping graph; scale is independent." : editor.network_mode == 2 ? "Y position and height scale; width stays full screen. Native height is shared with the classic graph." :
																																											   "X / Y position and visual scale. Resize adjusts width / height and selects custom-width mode.";
	default:
		return id == HUD_EDIT_LAYOUT_FIRST + HL_DEBUGGRAPH ?
			"X / Y position and height scale; width stays full screen. HUD enable/disable and lock." :
			"X / Y position and visual scale. Resize scales all contents together. HUD enable/disable and lock.";
	}
}

static void HUD_DrawInfo (int width, int y)
{
	const hud_info_t *info = HUD_Info (editor.selected);
	const char *reason;
	hud_status_t status = HUD_Status (editor.selected, &reason);
	char state[512];
	Q_snprintf (state, sizeof (state), "Editor: %s | HUD: %s | %s: %s", HUD_CanvasItem (editor.selected) ? "Shown" : "Hidden",
		HUD_Enabled (editor.selected) ? "Enabled" : "Disabled", HUD_StatusLabel (status), reason);
	R_DrawFill32 (0, y, width, editor.info_expanded ? 64 : 22, MakeColor (18, 27, 35, 255));
	R_DrawFill32 (0, y, width, 1, MakeColor (42, 59, 70, 255));
	int chars = max (1, (width - 100) / CHAR_WIDTH);
	R_ClearColor ();
	HUD_Button (&editor.buttons[HUD_BUTTON_INFO], width - 80, y + 2, 74, editor.info_expanded ? "Less info" : "More info", editor.info_expanded);
	if (!editor.info_expanded)
	{
		R_SetColor (HUD_StatusColor (status));
		UI_DrawString (12, y + 7, UI_LEFT, va ("%.*s", chars, state));
		R_ClearColor ();
		return;
	}
	UI_DrawString (12, y + 7, UI_LEFT, va ("%.*s", chars, va ("About: %s", info->contents)));
	UI_DrawString (12, y + 21, UI_LEFT, va ("%.*s", chars, va ("Appears: %s", info->appears)));
	R_SetColor (HUD_StatusColor (status));
	UI_DrawString (12, y + 35, UI_LEFT, va ("%.*s", chars, state));
	R_ClearColor ();
	UI_DrawString (12, y + 49, UI_LEFT, va ("%.*s", chars, va ("Edit: %s", HUD_EditDescription (editor.selected))));
}

static void HUD_WorkbenchDraw (void)
{
	float zoom = HUD_ToolsZoom ();
	int width = uis.width / zoom, height = uis.height / zoom;
	int tx = 0, bottom = editor.dock.y, rail = editor.rail.width;
	HUD_ClearToolsHits ();
	R_SetScale (uis.scale / zoom);
	R_ClearColor ();
	int stage_bottom = Q_rint ((editor.stage.y + editor.stage.height) * uis.scale / zoom);
	int stage_left = Q_rint (editor.stage.x * uis.scale / zoom);
	if (stage_left > rail)
		R_DrawFill32 (rail, 0, stage_left - rail, bottom, MakeColor (5, 7, 10, 255));
	if (stage_bottom < bottom)
		R_DrawFill32 (rail, stage_bottom, width - rail, bottom - stage_bottom, MakeColor (5, 7, 10, 255));
	R_DrawFill32 (0, 0, rail, bottom, MakeColor (12, 18, 24, 248));
	R_DrawFill32 (0, bottom, width, height - bottom, MakeColor (12, 18, 24, 252));
	R_DrawFill32 (rail - 1, 0, 1, bottom, MakeColor (42, 59, 70, 255));
	R_DrawFill32 (0, bottom, width, 1, MakeColor (60, 81, 95, 255));
	R_DrawFill32 (0, 0, rail, 20, MakeColor (27, 40, 51, 255));
	R_DrawFill32 (0, 0, 2, 20, MakeColor (115, 218, 214, 255));
	UI_DrawString (8, 6, UI_LEFT, "ELEMENTS");
	qboolean dirty_flags[HUD_EDIT_COUNT];
	HUD_DirtyFlags (dirty_flags);
	int matches = 0, enabled = 0, dirty = 0;
	{
		int id;
		for (id = 0; id < HUD_Limit (); id++)
		{
			if (!HUD_Editable (id))
				continue;
			dirty += dirty_flags[id];
			if (!HUD_Matches (id))
				continue;
			matches++;
			enabled += HUD_Enabled (id);
		}
	}
	UI_DrawString (rail - 8, 6, UI_RIGHT, va ("%d listed / %d enabled", matches, enabled));
	HUD_WorkbenchField (HUD_FIELD_FILTER, 6, 26, rail - 12);
	static const char *const categories[] = { "All", "Client", "Server" };
	int category_width = (rail - 12) / HUD_CATEGORY_COUNT;
	{
		int i;
		for (i = 0; i < HUD_CATEGORY_COUNT; i++)
			HUD_Button (&editor.categories[i], 6 + i * category_width, 52, category_width - 4, categories[i], editor.category == i);
	}
	UI_DrawString (8, 79, UI_LEFT, "Element");
	UI_DrawString (rail - 94, 79, UI_CENTER, "Editor");
	UI_DrawString (rail - 38, 79, UI_CENTER, "HUD");

	int rows[HUD_EDIT_COUNT + HUD_GROUP_COUNT], row_count = 0, selected_slot = -1;
	{
		int group;
		for (group = 0; group < HUD_GROUP_COUNT; group++)
		{
			int count = 0;
			{
				int id;
				for (id = 0; id < HUD_Limit (); id++)
					count += HUD_Group (id) == group && HUD_Matches (id);
			}
			if (!count)
				continue;
			rows[row_count++] = -group - 1;
			if (editor.collapsed[group] && !editor.filter.text[0])
				continue;
			{
				int id;
				for (id = 0; id < HUD_Limit (); id++)
					if (HUD_Group (id) == group && HUD_ListItem (id))
					{
						if (id == editor.selected)
							selected_slot = row_count;
						rows[row_count++] = id;
					}
			}
		}
	}
	int slots = max (1, (bottom - 92 - 76) / 20);
	if (selected_slot >= 0)
		editor.list_first = Q_clip (editor.list_first, max (0, selected_slot - slots + 1), selected_slot);
	editor.list_first = Q_clip (editor.list_first, 0, max (0, row_count - slots));
	editor.page_rows = max (1, slots - HUD_GROUP_COUNT);
	{
		int line;
		for (line = 0; line < slots && line + editor.list_first < row_count; line++)
		{
			int id = rows[line + editor.list_first], y = 92 + line * 20;
			if (id < 0)
			{
				int group = -id - 1;
				editor.groups[group] = (vrect_t){ 4, y, rail - 8, 20 };
				R_SetColor (MakeColor (143, 160, 180, 255));
				UI_DrawString (8, y + 4, UI_LEFT, va ("%c %s", editor.collapsed[group] && !editor.filter.text[0] ? '+' : '-', group_names[group]));
				R_ClearColor ();
				continue;
			}
			editor.rows[id] = (vrect_t){ 6, y, rail - 12, 20 };
			qboolean selected = editor.selected == id;
			if (selected || HUD_Hit (&editor.rows[id], HUD_ToolsMouse (0), HUD_ToolsMouse (1)))
				R_DrawFill32 (6, y, rail - 12, 20, selected ? MakeColor (39, 68, 80, 255) : MakeColor (29, 41, 50, 255));
			if (selected)
				R_DrawFill32 (6, y, 2, 20, MakeColor (115, 218, 214, 255));
			R_SetColor (HUD_Category (id) == HUD_CATEGORY_SERVER ? MakeColor (173, 201, 234, 255) : MakeColor (143, 210, 205, 255));
			UI_DrawString (10, y + 4, UI_LEFT, HUD_Category (id) == HUD_CATEGORY_SERVER ? "SV" : "CL");
			R_SetColor (HUD_CanvasItem (id) || selected ? MakeColor (226, 237, 250, 255) : MakeColor (120, 136, 154, 255));
			const char *name = HUD_Name (id);
			UI_DrawString (32, y + 5, UI_LEFT, strlen (name) > 15 ? va ("%.12s...", name) : name);
			if (dirty_flags[id])
			{
				R_SetColor (MakeColor (255, 205, 80, 255));
				UI_DrawString (rail - 124, y + 5, UI_LEFT, "*");
			}
			R_ClearColor ();
			qboolean shown = HUD_CanvasItem (id), on = HUD_Enabled (id);
			HUD_Button (&editor.preview_toggles[id], rail - 114, y + 1, 40, shown ? "Hide" : "Show", shown);
			HUD_Button (&editor.toggles[id], rail - 70, y + 1, 64, HUD_Locked (id) ? "Locked" : on ? "Disable" :
																									 "Enable",
				on);
		}
	}
	if (!matches)
		UI_DrawString (8, 98, UI_LEFT, "No matching elements.");
	int track_height = slots * 20;
	if (row_count > slots)
	{
		int thumb = max (6, track_height * slots / row_count);
		R_DrawFill32 (rail - 3, 92, 2, track_height, MakeColor (34, 49, 60, 255));
		R_DrawFill32 (rail - 3, 92 + (track_height - thumb) * editor.list_first / (row_count - slots), 2, thumb, MakeColor (101, 156, 169, 255));
	}
	int footer = bottom - 72;
	R_DrawFill32 (0, footer, rail, 1, MakeColor (34, 49, 60, 255));
	HUD_Button (&editor.buttons[HUD_BUTTON_RESET_ALL], 6, footer + 6, rail - 12, "Reset to Default", false);
	HUD_Button (&editor.buttons[HUD_BUTTON_SNAP], 6, footer + 29, 54, "Snap", editor.snap);
	HUD_Button (&editor.buttons[HUD_BUTTON_GHOSTS], 65, footer + 29, 116, "Show disabled", editor.ghosts);
	HUD_Button (&editor.buttons[HUD_BUTTON_KEYS], rail - 28, footer + 29, 22, "?", editor.keys_open);
	R_SetColor (MakeColor (131, 148, 166, 255));
	UI_DrawString (6, footer + 54, UI_LEFT, va ("%dx%d / %s %d%%", (int)editor.width, (int)editor.height, editor.scenario == HUD_PREVIEW_LIVE ? "zoom" : "sample", Q_rint (editor.preview_zoom * 100)));
	R_ClearColor ();

	int selected_width = width - 812;
	vrect_t r = editor.bounds[editor.selected];
	UI_DrawString (12, bottom + 18, UI_LEFT, HUD_Category (editor.selected) == HUD_CATEGORY_SERVER ? "SV" : "CL");
	UI_DrawString (38, bottom + 18, UI_LEFT, va ("%.*s", max (1, (selected_width - 46) / CHAR_WIDTH), HUD_Name (editor.selected)));
	R_SetColor (MakeColor (158, 190, 229, 255));
	UI_DrawString (12, bottom + 36, UI_LEFT, va ("%d, %d  / %dx%d", r.x, r.y, r.width, r.height));
	R_SetColor (MakeColor (131, 148, 166, 255));
	UI_DrawString (12, bottom + 53, UI_LEFT, HUD_Locked (editor.selected) ? "HUD locked - see info" : editor.field_bad ? "Enter a valid number" :
																														 "Screen coordinates");
	R_ClearColor ();
	HUD_Button (&editor.buttons[HUD_BUTTON_LOCK], 12, bottom + 66, 60, editor.locked[editor.selected] ? "Unlock" : "Lock", editor.locked[editor.selected]);
	HUD_Button (&editor.buttons[HUD_BUTTON_RESET_SELECTED], 78, bottom + 66, 70, "Reset", false);
	tx = selected_width;
	R_DrawFill32 (tx, bottom, 1, height - bottom, MakeColor (34, 49, 60, 255));
	UI_DrawString (tx + 12, bottom + 18, UI_LEFT, "POSITION AND SCALE");
	static const char *const field_labels[] = { "X", "Y", "S" };
	{
		int i;
		for (i = 0; i < 3; i++)
		{
			UI_DrawString (tx + 10 + i * 56, bottom + 55, UI_LEFT, field_labels[i]);
			HUD_WorkbenchField (HUD_FIELD_X + i, tx + 20 + i * 56, bottom + 49, 42);
		}
	}
	HUD_Button (&editor.buttons[HUD_BUTTON_MOVE_UP], tx + 207, bottom + 34, 26, "^", false);
	HUD_Button (&editor.buttons[HUD_BUTTON_MOVE_DOWN], tx + 207, bottom + 58, 26, "v", false);
	HUD_Button (&editor.buttons[HUD_BUTTON_MOVE_LEFT], tx + 177, bottom + 58, 26, "<", false);
	HUD_Button (&editor.buttons[HUD_BUTTON_MOVE_RIGHT], tx + 237, bottom + 58, 26, ">", false);
	HUD_Button (&editor.buttons[HUD_BUTTON_STEP], tx + 237, bottom + 34, 26, va ("%d", max (1, editor.step)), editor.step > 1);
	tx += 272;
	R_DrawFill32 (tx, bottom, 1, height - bottom, MakeColor (34, 49, 60, 255));
	UI_DrawString (tx + 10, bottom + 18, UI_LEFT, "VISIBILITY");
	qboolean shown = HUD_CanvasItem (editor.selected), on = HUD_Enabled (editor.selected);
	HUD_Button (&editor.buttons[HUD_BUTTON_EDITOR_VISIBLE], tx + 8, bottom + 34, 144, shown ? "Hide in Editor" : "Show in Editor", shown);
	HUD_Button (&editor.buttons[HUD_BUTTON_VISIBLE], tx + 8, bottom + 58, 144,
		HUD_Locked (editor.selected) ? "HUD Locked" : on ? "Disable in HUD" :
														   "Enable in HUD",
		on);
	tx += 160;
	R_DrawFill32 (tx, bottom, 1, height - bottom, MakeColor (34, 49, 60, 255));
	UI_DrawString (tx + 8, bottom + 18, UI_LEFT, "ALIGN TO");
	HUD_Button (&editor.buttons[HUD_BUTTON_REFERENCE], tx + 76, bottom + 12, 62, editor.align_reference && editor.reference >= 0 ? "Ref" : "Screen", editor.align_reference);
	static const char *const align[] = { "Left", "Ctr", "Right", "Top", "Mid", "Bot" };
	{
		int i;
		for (i = 0; i < 6; i++)
			HUD_Button (&editor.buttons[HUD_BUTTON_ALIGN_LEFT + i], tx + 6 + (i % 3) * 44, bottom + 39 + (i / 3) * 23, 42, align[i], false);
	}
	tx += 144;
	R_DrawFill32 (tx, bottom, 1, height - bottom, MakeColor (34, 49, 60, 255));
	UI_DrawString (tx + 10, bottom + 18, UI_LEFT, dirty ? va ("SESSION  %d edits", dirty) : "SESSION  no edits");
	HUD_Button (&editor.buttons[HUD_BUTTON_CHANGES], width - 66, bottom + 12, 60, "Details", editor.changes_open);
	HUD_Button (&editor.buttons[HUD_BUTTON_UNDO], tx + 8, bottom + 49, 46, "Undo", editor.history_pos > 0);
	HUD_Button (&editor.buttons[HUD_BUTTON_REDO], tx + 58, bottom + 49, 46, "Redo", editor.history_pos + 1 < editor.history_count);
	HUD_Button (&editor.buttons[HUD_BUTTON_CANCEL], tx + 108, bottom + 49, 58, "Cancel", false);
	HUD_Button (&editor.buttons[HUD_BUTTON_APPLY], tx + 170, bottom + 49, 60, "Apply", dirty != 0);

	HUD_DrawInfo (width, bottom + 94);

	R_DrawFill32 (rail, 0, width - rail, 30, MakeColor (18, 27, 35, 255));
	static const char *const scenarios[] = { "Live", "Running", "Spectating", "Voting", "Scoreboard", "Net warning" };
	int toolbar_x = rail + 6;
	{
		int i;
		for (i = 0; i < HUD_PREVIEW_COUNT; i++)
		{
			int w = strlen (scenarios[i]) * CHAR_WIDTH + 12;
			HUD_Button (&editor.buttons[HUD_BUTTON_SCENARIO_LIVE + i], toolbar_x, 6, w, scenarios[i], editor.scenario == i);
			toolbar_x += w + 4;
		}
	}
	toolbar_x += 8;
	HUD_Button (&editor.buttons[HUD_BUTTON_FIT], toolbar_x, 6, 36, "Fit", !editor.manual_zoom);
	HUD_Button (&editor.buttons[HUD_BUTTON_100], toolbar_x + 40, 6, 44, "100%", editor.manual_zoom == 1);
	HUD_Button (&editor.buttons[HUD_BUTTON_SELECTION], toolbar_x + 88, 6, 84, "Selection", false);
	HUD_Button (&editor.buttons[HUD_BUTTON_ZOOM_OUT], toolbar_x + 176, 6, 22, "-", false);
	HUD_Button (&editor.buttons[HUD_BUTTON_ZOOM_IN], toolbar_x + 202, 6, 22, "+", false);

	if (editor.changes_open)
	{
		int shown = min (dirty, 9), w = 436, h = 44 + max (1, shown) * 20;
		int x = width - w - 8, y = max (8, bottom - h - 8);
		editor.overlay = (vrect_t){ x, y, w, h };
		R_DrawFill32 (x, y, w, h, MakeColor (12, 18, 24, 252));
		HUD_Outline (&editor.overlay, MakeColor (60, 81, 95, 255));
		R_DrawFill32 (x, y, w, 22, MakeColor (27, 40, 51, 255));
		UI_DrawString (x + 8, y + 7, UI_LEFT, "PENDING CHANGES");
		UI_DrawString (x + w - 8, y + 7, UI_RIGHT, "Esc closes");
		editor.change_first = Q_clip (editor.change_first, 0, max (0, dirty - shown));
		int skip = editor.change_first, line = 0;
		{
			int id;
			for (id = 0; id < HUD_Limit () && line < shown; id++)
			{
				if (!HUD_Editable (id) || !dirty_flags[id])
					continue;
				if (skip-- > 0)
					continue;
				int ry = y + 28 + line++ * 20;
				editor.change_rows[id] = (vrect_t){ x + 4, ry, w - 62, 18 };
				editor.reverts[id] = (vrect_t){ x + w - 58, ry, 54, 18 };
				UI_DrawString (x + 8, ry + 5, UI_LEFT, va ("%.26s", HUD_Name (id)));
				UI_DrawString (x + 226, ry + 5, UI_LEFT, editor.locked[id] != editor.original_locks[id] ? "lock / layout" : "layout");
				UI_DrawString (x + w - 54, ry + 5, UI_LEFT, "revert");
			}
		}
		if (!dirty)
			UI_DrawString (x + 8, y + 33, UI_LEFT, "No edits yet. Drag or type a coordinate.");
		else
			UI_DrawString (
				x + 8, y + h - 11, UI_LEFT,
				va ("%d-%d of %d / wheel to scroll", editor.change_first + 1, editor.change_first + shown, dirty));
	}
	if (editor.keys_open)
	{
		static const char *const keys[][2] = {
			{ "/", "Filter elements" }, { "Tab / Shift-Tab", "Next / previous filtered element" },
			{ "Wheel / Page", "Select / page through elements" }, { "Drag / corner", "Move / resize where supported" },
			{ "Ctrl + wheel", "Zoom at pointer (25-400%)" }, { "Middle / Space-drag", "Pan preview; Fit resets view" },
			{ "Arrows / Shift", "Nudge / ten times pad step" }, { "Ctrl + arrows", "Resize selected element" },
			{ "X / Y / S", "Edit screen X / Y / visual scale" }, { "E / V / L", "Editor view / HUD enable / lock" },
			{ "G / N", "Show disabled / snapping" }, { "R / Shift-R", "Reset selected / all unlocked" },
			{ "P / ?", "Pending changes / key map" }, { "Ctrl-Z / Ctrl-Y", "Undo / redo" },
			{ "F / H", "Focus selected / hide workbench" }, { "Enter", "Commit field; otherwise Apply" },
			{ "Esc", "Leave field / close overlay / Cancel" }
		};
		int w = 510, h = 36 + q_countof (keys) * 20, x = (width - w) / 2, y = max (8, (bottom - h) / 2);
		R_DrawFill32 (0, 0, width, height, MakeColor (5, 7, 10, 160));
		editor.overlay = (vrect_t){ x, y, w, h };
		R_DrawFill32 (x, y, w, h, MakeColor (12, 18, 24, 255));
		HUD_Outline (&editor.overlay, MakeColor (60, 81, 95, 255));
		R_DrawFill32 (x, y, w, 22, MakeColor (27, 40, 51, 255));
		UI_DrawString (x + 10, y + 7, UI_LEFT, "KEYS");
		UI_DrawString (x + w - 10, y + 7, UI_RIGHT, "Esc closes");
		{
			int i;
			for (i = 0; i < q_countof (keys); i++)
			{
				R_SetColor (MakeColor (154, 215, 210, 255));
				UI_DrawString (x + 12, y + 29 + i * 20, UI_LEFT, keys[i][0]);
				R_ClearColor ();
				UI_DrawString (x + 160, y + 29 + i * 20, UI_LEFT, keys[i][1]);
			}
		}
	}
	if (!editor.keys_open && !editor.changes_open)
	{
		const char *tip = NULL;
		{
			int id;
			for (id = 0; id < HUD_Limit (); id++)
				if (HUD_ListItem (id) && HUD_Hit (&editor.rows[id], HUD_ToolsMouse (0), HUD_ToolsMouse (1)))
				{
					const char *reason;
					hud_status_t status = HUD_Status (id, &reason);
					if (HUD_Hit (&editor.preview_toggles[id], HUD_ToolsMouse (0), HUD_ToolsMouse (1)))
						tip = HUD_CanvasItem (id) ? "Hide in editor only; HUD stays unchanged." : "Show in editor only; HUD stays unchanged.";
					else if (HUD_Hit (&editor.toggles[id], HUD_ToolsMouse (0), HUD_ToolsMouse (1)))
						tip = HUD_Locked (id) ? HUD_EditDescription (id) : HUD_Enabled (id) ? "Disable in HUD after Apply; editor view stays separate." :
																							  "Enable in HUD after Apply; normal event conditions still apply.";
					else
						tip = va ("%s [Editor: %s | HUD: %s | %s]", HUD_Name (id), HUD_CanvasItem (id) ? "Shown" : "Hidden",
							HUD_Enabled (id) ? "Enabled" : "Disabled", HUD_StatusLabel (status));
				}
		}
		if (tip)
		{
			int tw = (int)strlen (tip) * CHAR_WIDTH + 12;
			int x = rail + 6, y = Q_clip ((int)HUD_ToolsMouse (1), 4, max (4, bottom - 24));
			R_DrawFill32 (x, y, min (tw, width - x), 20, MakeColor (27, 40, 51, 255));
			UI_DrawString (x + 6, y + 6, UI_LEFT, va ("%.*s", (width - x - 12) / CHAR_WIDTH, tip));
		}
	}
	R_SetScale (uis.scale);
	R_ClearColor ();
}

/* Called with physical coordinates and the preview clip already installed. */
static void HUD_DrawBackdrop (void)
{
	if (cls.state >= ca_active && editor.scenario == HUD_PREVIEW_LIVE && !editor.manual_zoom)
		return;
	vrect_t r = editor.stage;
	R_ClearColor ();
	R_DrawFill32 (r.x, r.y, r.width, r.height, MakeColor (5, 7, 10, 255));
	if (editor.manual_zoom)
	{
		r = (vrect_t){ Q_rint (editor.view_x), Q_rint (editor.view_y),
			Q_rint (editor.width * editor.preview_zoom / editor.scale), Q_rint (editor.height * editor.preview_zoom / editor.scale) };
	}
	R_DrawFill32 (r.x, r.y, r.width, r.height, MakeColor (16, 26, 35, 255));
	int width = 0, height = 0;
	if (editor.backdrop)
		R_GetPicSize (&width, &height, editor.backdrop);
	if (width <= 0 || height <= 0)
		return;
	float fit = max (r.width / (float)width, r.height / (float)height);
	int w = Q_rint (width * fit), h = Q_rint (height * fit);
	R_DrawStretchPic (r.x + (r.width - w) / 2, r.y + (r.height - h) / 2, w, h, editor.backdrop);
}

static void HUD_Draw (menuFrameWork_t *menu)
{
	if (!HUD_EditorActive ())
		return;
	if (cl.servercount != editor.servercount || (Key_GetDest () == key_console))
	{
		UI_PopMenu ();
		return;
	}
	float scale = jump_screen.hud_scale > 0 ? jump_screen.hud_scale : 1;
	float width = jump_screen.hud_width, height = jump_screen.hud_height;
	if (width != editor.width || height != editor.height || scale != editor.scale || CL_JumpWindowActive () != editor.active)
	{
		HUD_EndDrag ();
		editor.panning = false;
		editor.held_button = -1;
		SCR_HudEditorPrepare ();
	}
	if (HUD_Value ("sh_netmeter") > 0)
		editor.network_mode = (int)HUD_Value ("sh_netmeter");
	editor.width = width;
	editor.height = height;
	editor.scale = scale;
	editor.active = CL_JumpWindowActive ();
	if (editor.held_button >= 0)
	{
		int button = editor.held_button;
		if (!Key_IsDown (K_MOUSE1) || CL_JumpWindowActive () != ACT_ACTIVATED ||
			!HUD_Hit (&editor.buttons[button], HUD_ToolsMouse (0), HUD_ToolsMouse (1)))
			editor.held_button = -1;
		else if ((int)(com_localTime - editor.hold_time) >= 0)
		{
			float dx, dy;
			HUD_PadDelta (button, &dx, &dy);
			HUD_Move (dx, dy, false);
			if (editor.held_changed)
				HUD_HistoryCapture (editor.history_pos);
			else
			{
				int pos = editor.history_pos;
				HUD_HistoryCommit ();
				editor.held_changed = editor.history_pos != pos;
			}
			editor.hold_time = com_localTime + 70;
		}
	}
	int saved_width = jump_screen.hud_width, saved_height = jump_screen.hud_height;
	jump_screen.hud_width = width;
	jump_screen.hud_height = height;
	memset (editor.bounds, 0, sizeof (editor.bounds));
	HUD_WorkbenchLayout ();
	/* Keep saved HUD coordinates independent from the view transform. */
	R_SetScale (1);
	const clipRect_t clip = { .left = editor.stage.x, .top = editor.stage.y, .right = editor.stage.x + editor.stage.width, .bottom = editor.stage.y + editor.stage.height };
	R_SetClipRect (&clip);
	HUD_DrawBackdrop ();
	R_SetDrawTransform (editor.view_x, editor.view_y, editor.preview_zoom);
	editor.preview = true;
	R_SetScale (scale);
	R_ClearColor ();
	R_DrawFill32 (0, 0, width, height, MakeColor (5, 7, 10, 96));
	if (cls.state < ca_active || editor.scenario != HUD_PREVIEW_LIVE || editor.manual_zoom)
	{
		R_SetColor (MakeColor (143, 160, 180, 255));
		UI_DrawString (8, 8, UI_LEFT, editor.scenario != HUD_PREVIEW_LIVE ? "Sample scenario" : editor.manual_zoom ? "HUD zoom - static backdrop" :
																													 "Preview - no map loaded");
		R_ClearColor ();
	}
	const struct StrafeHelperParams params = {
		.center = cl_strafeHelperCenter->intvalue,
		.center_marker = cl_strafeHelperCenterMarker->intvalue,
		.scale = HUD_Value ("sh_scale"),
		.height = HUD_Value ("sh_height"),
		.y = HUD_Value ("sh_y"),
		.hud_scale = scale,
	};
	StrafeHelper_DrawPreview (&params, width, height, jump_screen.font_pic);
	SH_Ups_Draw (width, height, scale, jump_screen.font_pic);
	SH_NetMeter_Draw ();
	HUD_LayoutPreview ();
	editor.preview = false;
	jump_screen.hud_width = saved_width;
	jump_screen.hud_height = saved_height;
	{
		int i;
		for (i = 0; i < HUD_Limit (); i++)
			if (HUD_Editable (i) && HUD_CanvasItem (i))
				HUD_Outline (&editor.bounds[i], i == editor.selected ? MakeColor (255, 205, 80, 255) : MakeColor (130, 153, 170, 95));
	}
	vrect_t r = editor.bounds[editor.selected];
	qboolean selected_shown = HUD_CanvasItem (editor.selected);
	if (r.width && selected_shown)
	{
		const char *label = HUD_Name (editor.selected);
		int label_width = min ((int)strlen (label), 30) * CHAR_WIDTH + 8;
		int lx = Q_clip (r.x, 0, max (0, (int)width - label_width));
		int ly = Q_clip (r.y - 15, 0, max (0, (int)height - 14));
		R_DrawFill32 (lx, ly, label_width, 13, MakeColor (35, 31, 20, 235));
		UI_DrawString (lx + 4, ly + 3, UI_LEFT, va ("%.30s", label));
	}
	if (selected_shown && HUD_CanResize ())
		R_DrawFill32 (r.x + r.width - 3, r.y + r.height - 3, 6, 6, MakeColor (255, 205, 80, 255));
	if (editor.dragging && !editor.resizing)
	{
		if (isfinite (editor.guide_x))
			R_DrawFill32 (editor.guide_x, 0, 1, height, MakeColor (115, 218, 214, 220));
		if (isfinite (editor.guide_y))
			R_DrawFill32 (0, editor.guide_y, width, 1, MakeColor (115, 218, 214, 220));
	}
	if (editor.snap)
	{
		R_DrawFill32 (width / 2, 0, 1, height, MakeColor (130, 150, 170, 50));
		R_DrawFill32 (0, height / 2, width, 1, MakeColor (130, 150, 170, 50));
	}
	R_SetDrawTransform (0, 0, 1);
	R_SetClipRect (NULL);
	R_SetScale (uis.scale);
	R_ClearColor ();
	if (!HUD_ToolsVisible ())
		return;
	HUD_WorkbenchDraw ();
}

static void HUD_Open_f (void)
{
	if (!CL_JumpEnabled () || !scr_rich_presentation)
		return;
	CL_JumpUITouch ();
	if (!(uis.scale > 0 && uis.scale <= 1024))
		uis.scale = 1;
	if (HUD_EditorActive ())
	{
		UI_PopMenu ();
		return;
	}
	SCR_HudEditorPrepare ();
	memcpy (settings, native_settings, sizeof (native_settings));
	setting_count = HUD_NATIVE_SETTINGS;
	{
		int i;
		for (i = 0; i < HUD_LayoutCount (); i++)
		{
			const hud_layout_item_t *item = HUD_LayoutItem (i);
			cvar_t *vars[] = { item->x, item->y, item->visible, item->scale };
			{
				int j;
				for (j = 0; j < q_countof (vars); j++)
					settings[setting_count++] = (hud_setting_t){ vars[j]->name,
						HUD_EDIT_LAYOUT_FIRST + i, j == 3 ? HUD_SCALE_MIN : j == 2 ? 0 :
																					 -16000,
						j == 3 ? HUD_SCALE_MAX : j == 2 ? 1 :
														  16000 };
			}
		}
	}
	{
		size_t i;
		for (i = 0; i < setting_count; i++)
		{
			hud_setting_t *s = &settings[i];
			s->var = Cvar_FindVar (s->name);
			if (!s->var)
			{
				Com_Printf ("HUD editor: missing %s\n", LOG_CLIENT, s->name);
				return;
			}
			s->original = s->var->value;
			s->value = s->low == HUD_SCALE_MIN && s->high == HUD_SCALE_MAX && !isfinite (s->original) ?
				1 :
				HudEdit_Clamp (s->original, s->low, s->high);
			s->changed = false;
		}
	}
	Key_ClearStates ();
	editor.open = true;
	editor.dragging = editor.panning = editor.info_expanded = false;
	editor.manual_zoom = 0;
	editor.scenario = HUD_PREVIEW_LIVE;
	editor.selected = HUD_EDIT_UPS;
	editor.list_first = 0;
	editor.snap = true;
	editor.ghosts = true;
	memset (editor.editor_visibility, 0, sizeof (editor.editor_visibility));
	memset (editor.enable_setting, 0, sizeof (editor.enable_setting));
	editor.reference = -1;
	editor.held_button = -1;
	editor.step = 1;
	editor.change_first = 0;
	editor.field = HUD_FIELD_NONE;
	editor.skip_char = false;
	editor.field_bad = false;
	editor.keys_open = editor.changes_open = editor.align_reference = false;
	memset (editor.collapsed, 0, sizeof (editor.collapsed));
	IF_Init (&editor.filter, 21, 64);
	IF_Init (&editor.input, 8, 24);
	editor.hide_tools = false;
	editor.category = HUD_CATEGORY_ALL;
	editor.focus = false;
	editor.page_rows = 6;
	HUD_ClearToolsHits ();
	editor.history_count = editor.history_pos = 0;
	editor.guide_x = editor.guide_y = NAN;
	memset (editor.lock_vars, 0, sizeof (editor.lock_vars));
	memset (editor.locked, 0, sizeof (editor.locked));
	{
		int i;
		for (i = 0; i < HUD_Limit (); i++)
		{
			if (!HUD_Editable (i))
				continue;
			const char *key = i < HUD_EDIT_LAYOUT_FIRST ? native_widgets[i].key : HUD_LayoutItem (i - HUD_EDIT_LAYOUT_FIRST)->key;
			char name[MAX_QPATH];
			Q_snprintf (name, sizeof (name), "hud_lock_%s", key);
			editor.lock_vars[i] = Cvar_Get (name, "0", CVAR_ARCHIVE);
			editor.locked[i] = editor.lock_vars[i]->intvalue != 0;
		}
	}
	memcpy (editor.original_locks, editor.locked, sizeof (editor.locked));
	editor.network_mode = (int)HUD_Value ("sh_netmeter");
	if (!editor.network_mode)
		editor.network_mode = 3;
	editor.servercount = cl.servercount;
	editor.backdrop = R_RegisterPic ("q2jump_background");
	uis.scale = 1;
	uis.width = CL_JumpViewportWidth ();
	uis.height = CL_JumpViewportHeight ();
	editor.scale = jump_screen.hud_scale > 0 ? jump_screen.hud_scale : 1;
	UI_PushMenu (&editor.menu);
	HUD_EditorMouse (uis.mouseCoords[0] / uis.scale, uis.mouseCoords[1] / uis.scale);
}

void HUD_EditorInit (void)
{
	/* The shared menu UI is initialized on demand by CL_JumpUITouch. */
	memset (&editor, 0, sizeof (editor));
	editor.menu.name = "hud_editor";
	editor.menu.transparent = true;
	editor.menu.draw = HUD_Draw;
	editor.menu.pop = HUD_Pop;
	editor.menu.size = HUD_Size;
	Cmd_AddCommand ("hud_edit", HUD_Open_f);
}

void HUD_EditorShutdown (void)
{
	CL_JumpEditorCancel ();
	editor.open = editor.preview = editor.dragging = editor.panning = false;
	Cmd_RemoveCommand ("hud_edit");
}

void CL_JumpEditorReleaseInputs (void)
{
	HUD_EndDrag ();
	editor.panning = false;
	editor.held_button = -1;
	editor.skip_char = false;
}

void CL_JumpEditorCancel (void)
{
	if (HUD_EditorActive ())
		UI_PopMenu ();
	else if (editor.open)
		HUD_Pop (&editor.menu);
	R_SetDrawTransform (0, 0, 1);
}

void CL_JumpEditorDraw (void)
{
	if (!HUD_EditorActive () || !scr_rich_presentation || !CL_JumpPrepareHUD ())
		return;
	uis.width = CL_JumpViewportWidth ();
	uis.height = CL_JumpViewportHeight ();
	uis.scale = 1;
	scr_rich_presentation->Begin ();
	HUD_Draw (&editor.menu);
	R_SetDrawTransform (0, 0, 1);
	R_SetClipRect (NULL);
	R_SetScale (1);
	R_ClearColor ();
	scr_rich_presentation->End ();
}

/* Keep viewport edits in a renderer copy; captured native refdefs stay intact. */
qboolean CL_JumpEditorView (const refdef_t *source, refdef_t *out)
{
	vrect_t viewport;
	int width, height;
	float base, tangent;
	if (!source || !out || !scr_presentation || !HUD_EditorViewport (&viewport))
		return false;
	base = scr_presentation->GetSize (-1, &width, &height);
	if (!(base >= 1 && base <= 1024) || !(source->fov_y >= 1 && source->fov_y <= 179))
		return false;
	*out = *source;
	out->x = (int)(viewport.x / base);
	out->y = (int)(viewport.y / base);
	out->width = max (1, (int)(viewport.width / base));
	out->height = max (1, (int)(viewport.height / base));
	tangent = (float)tan (source->fov_y * M_PI / 360);
	out->fov_x = (float)(atan (tangent * out->width / out->height) * 360 / M_PI);
	return true;
}
