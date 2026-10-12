/* Frozen Q2PRO Race HUD layout adapted to private R1Q2 drawing.
 * GPL version 2 or later; see COPYING. */
#include "jump_compat.h"
#include "jump_layout_match.h"
#include "jump_layout_server.h"

static hud_layout_item_t items[HUD_LAYOUT_MAX];
static char object_names[HUD_LAYOUT_MAX][MAX_QPATH];
static char object_keys[HUD_LAYOUT_MAX][32];
static int item_count, current = -1, bounds_servercount;
static vrect_t last_bounds[HUD_LAYOUT_MAX];
static int last_width[HUD_LAYOUT_MAX], last_height[HUD_LAYOUT_MAX];
static float current_x, current_y;
static float object_anchor[HUD_LAYOUT_MAX][2];
static qboolean captured[HUD_LAYOUT_MAX];
static char matched_layout[HUD_LAYOUT_TEXT_MAX];
static hud_layout_token_t matched_tokens[HUD_LAYOUT_TOKEN_MAX];
static int matched_count;

static const struct
{
	const char *key, *name, *sample;
	float ax, ay;
	int x, y, width, height;
} defaults[HL_BUILTIN_COUNT] = {
	{ "health", "Health", "100 Health", .5f, 1, 150, -32, 54, 32 },
	{ "item", "Weapon icon", "Gun", .5f, 1, 120, -32, 24, 24 },
	{ "speed", "Speed (server)", "742 Speed", .5f, 1, 40, -32, 66, 32 },
	{ "target", "Target player name", "Player", .5f, 1, -48, -58, 96, 8 },
	{ "timer", "Run timer", "12.3 Time", 1, 1, -94, -32, 94, 32 },
	{ "inputs", "Input keys", "W A S D Jump", 0, 1, 2, -42, 64, 42 },
	{ "server_fps", "Reported player FPS", "120 FPS", 0, 1, 0, -76, 78, 32 },
	{ "vote", "Vote information", "Vote: next map", 0, 1, 2, -136, 200, 40 },
	{ "mapcount", "Available maps", "42 Maps", 1, 0, -32, 42, 32, 16 },
	{ "status1", "Team / mode", "Team: Hard", .5f, 1, -88, -32, 160, 8 },
	{ "status2", "Run info line 1", "Run info line 1", .5f, 1, -88, -24, 160, 8 },
	{ "status3", "Run info line 2", "Run info line 2", .5f, 1, -88, -16, 160, 8 },
	{ "status4", "Run info line 3", "Run info line 3", .5f, 1, -88, -8, 160, 8 },
	{ "map", "Current map", "Current map", 1, 0, -128, 2, 128, 8 },
	{ "prevmap1", "Previous map 1", "Previous map 1", 1, 0, -128, 10, 128, 8 },
	{ "prevmap2", "Previous map 2", "Previous map 2", 1, 0, -128, 18, 128, 8 },
	{ "prevmap3", "Previous map 3", "Previous map 3", 1, 0, -128, 26, 128, 8 },
	{ "addedtime", "Map time adjustment", "+5", 1, 0, -32, 100, 32, 8 },
	{ "timeleft", "Map time remaining", "Time 15", 1, 0, -50, 64, 50, 34 },
	{ "crosshair", "Crosshair", "+", .5f, .5f, -8, -8, 16, 16 },
	{ "hitmarker", "Hit marker", "X", .5f, .5f, -12, -12, 24, 24 },
	{ "chat", "Chat history", "Player: nice jump!", 0, 1, 8, -174, 224, 32 },
	{ "center", "Center messages", "Checkpoint reached", .5f, .25f, -80, -4, 160, 8 },
	{ "notify", "Console messages", "Connected to server", 0, 0, 8, 0, 240, 32 },
	{ "message", "Chat input", "say: message...", 0, 0, 8, 32, 240, 8 },
	{ "netalert", "Network alerts", "PACKET LOSS", .5f, .5f, -44, -4, 88, 8 },
	{ "neticon", "Ping graph / connection icon", "Ping", 0, 0, 166, -1, 48, 48 },
	{ "turtle", "Frame / prediction warnings", "PRED", 0, 0, 0, 0, 32, 32 },
	{ "pause", "Pause indicator", "PAUSED", .5f, .5f, -48, -12, 96, 24 },
	{ "inventory", "Inventory", "Inventory", .5f, .5f, -128, -120, 256, 240 },
	{ "scoreboard", "Scoreboard / server menus", "Scoreboard / server menus", .5f, .5f, -160, -120, 320, 240 },
	{ "other_status", "Other server HUD", "Unrecognized server HUD", .5f, 1, -160, -48, 320, 48 },
	{ "demo", "Demo progress", "Demo progress 50%", 0, 1, 0, -8, 320, 8 },
	{ "loading", "Loading indicator", "Loading", .5f, .5f, -48, -12, 96, 24 },
	{ "debuggraph", "Classic network / debug graph", "Network / debug graph", 0, 1, 0, -15, 320, 15 },
	{ "nerdstats", "Nerd stats panel", "Physics and frame timings", 0, 0, 0, 0, 240, 240 },
	{ "render_fps", "Render FPS", "Render: 120 FPS", 1, 0, -128, 40, 120, 8 },
	{ "move_fps", "Movement rate (MPS)", "Moves: 120 MPS", 1, 0, -128, 50, 120, 8 },
	{ "debug_stats", "Debug frame stats", "Frame / network statistics", 0, 0, 0, 100, 200, 96 },
	{ "debug_pmove", "Debug movement", "Movement state", 0, 0, 0, 200, 200, 96 },
	{ "bind_reminders", "Bind reminders", "Bind reminders", 1, .525f, -118, 0, 110, 90 },
};

static void HUD_RegisterItem (int id, const char *key, const char *name, qboolean visible)
{
	char buffer[MAX_QPATH];
	items[id].key = key;
	items[id].name = name;
	if (id == HL_BIND_REMINDERS)
	{
		items[id].x = SCR_RegisterBindReminderCvar ("scr_bindreminders_x", "hud_bind_reminders_x", "0");
		items[id].y = SCR_RegisterBindReminderCvar ("scr_bindreminders_y", "hud_bind_reminders_y", "0");
		items[id].visible = SCR_RegisterBindReminderCvar ("scr_bindreminders_visible",
			"hud_bind_reminders_visible", visible ? "1" : "0");
		items[id].scale = SCR_RegisterBindReminderCvar ("scr_bindreminders_scale", "hud_bind_reminders_scale", "1");
		return;
	}
	Q_snprintf (buffer, sizeof (buffer), "hud_%s_x", key);
	items[id].x = Cvar_Get (buffer, "0", CVAR_ARCHIVE);
	Q_snprintf (buffer, sizeof (buffer), "hud_%s_y", key);
	items[id].y = Cvar_Get (buffer, "0", CVAR_ARCHIVE);
	Q_snprintf (buffer, sizeof (buffer), "hud_%s_visible", key);
	items[id].visible = Cvar_Get (buffer, visible ? "1" : "0", CVAR_ARCHIVE);
	Q_snprintf (buffer, sizeof (buffer), "hud_%s_scale", key);
	items[id].scale = Cvar_Get (buffer, "1", CVAR_ARCHIVE);
}

void HUD_LayoutInit (void)
{
	current = -1;
	item_count = HL_BUILTIN_COUNT;
	matched_layout[0] = 0;
	matched_count = 0;
	memset (last_bounds, 0, sizeof (last_bounds));
	memset (object_anchor, 0, sizeof (object_anchor));
	{
		int i;
		for (i = 0; i < item_count; i++)
			HUD_RegisterItem (i, defaults[i].key, defaults[i].name,
				i != HL_RENDER_FPS && i != HL_MOVE_FPS);
	}
}

void HUD_LayoutFrame (void)
{
	HUD_LayoutEnd ();
	current = -1;
	memset (captured, 0, sizeof (captured));
}

int HUD_LayoutCount (void)
{
	return item_count;
}

const hud_layout_item_t *HUD_LayoutItem (int id)
{
	return id >= 0 && id < item_count ? &items[id] : NULL;
}

int HUD_LayoutObject (const char *name)
{
	/* Stable bounded cvar names, independent of registration order/name length. */
	char key[32];
	uint64 hash = (uint64)0xcbf29ce484222325ULL;
	{
		const byte *p;
		for (p = (const byte *)name; *p; p++)
		{
			hash ^= *p;
			hash *= (uint64)0x100000001b3ULL;
		}
	}
	Q_snprintf (key, sizeof (key), "draw_%08x%08x", (uint32)(hash >> 32), (uint32)hash);
	{
		int i;
		for (i = HL_BUILTIN_COUNT; i < item_count; i++)
			if (!strcmp (object_keys[i], key) && !strcmp (object_names[i], name))
				return i;
	}
	if (!item_count || item_count == HUD_LAYOUT_MAX || HUD_EditorActive ())
		return -1;
	int id = item_count++;
	Q_strlcpy (object_names[id], name, sizeof (object_names[id]));
	Q_strlcpy (object_keys[id], key, sizeof (object_keys[id]));
	HUD_RegisterItem (id, object_keys[id], object_names[id], true);
	return id;
}

void HUD_LayoutSetScaleAnchor (int id, float x, float y)
{
	if (id < HL_BUILTIN_COUNT || id >= item_count)
		return;
	object_anchor[id][0] = isfinite (x) ? x : 0;
	object_anchor[id][1] = isfinite (y) ? y : 0;
}

void HUD_LayoutScaleAnchor (int id, float *x, float *y)
{
	*x = *y = 0;
	if (id < 0 || id >= item_count)
		return;
	if (id < HL_BUILTIN_COUNT)
	{
		float scale = jump_screen.hud_scale > 0 ? jump_screen.hud_scale : 1;
		int width = Q_rint (CL_JumpViewportWidth () * scale), height = Q_rint (CL_JumpViewportHeight () * scale);
		*x = Q_rint (width * defaults[id].ax);
		*y = Q_rint (height * defaults[id].ay);
	}
	else
	{
		*x = object_anchor[id][0];
		*y = object_anchor[id][1];
	}
}

void HUD_LayoutBegin (int id)
{
	HUD_LayoutEnd ();
	if (id < 0 || id >= item_count)
		return;
	if (bounds_servercount != cl.servercount)
	{
		memset (last_bounds, 0, sizeof (last_bounds));
		bounds_servercount = cl.servercount;
	}
	const hud_layout_item_t *item = &items[id];
	float scale = jump_screen.hud_scale > 0 ? jump_screen.hud_scale : 1;
	current_x = HUD_EditorValue (item->x);
	current_y = HUD_EditorValue (item->y);
	current_x = isfinite (current_x) ? CLAMP (current_x, -16000, 16000) : 0;
	current_y = isfinite (current_y) ? CLAMP (current_y, -16000, 16000) : 0;
	qboolean hidden = !HUD_EditorValue (item->visible) || HUD_EditorActive ();
	if (HUD_EditorPreview ())
		hidden = !HUD_EditorShow (HUD_EDIT_LAYOUT_FIRST + id);
	R_BeginDrawGroup (current_x / scale, current_y / scale, hidden);
	if (HUD_LayoutEditable (id))
	{
		float item_scale = HUD_EditorValue (item->scale), x, y;
		item_scale = isfinite (item_scale) ? CLAMP (item_scale, HUD_SCALE_MIN, HUD_SCALE_MAX) : 1;
		HUD_LayoutScaleAnchor (id, &x, &y);
		R_SetDrawGroupScale (id == HL_DEBUGGRAPH ? 1 : item_scale, item_scale, x / scale, y / scale);
	}
	current = id;
}

void HUD_LayoutEnd (void)
{
	if (current < 0)
		return;
	vrect_t bounds, raw_bounds;
	if (R_EndDrawGroupRaw (&bounds, &raw_bounds))
	{
		float scale = jump_screen.hud_scale > 0 ? jump_screen.hud_scale : 1;
		if (!HUD_EditorPreview ())
			bounds = raw_bounds;
		bounds.x = Q_rint (bounds.x * scale);
		bounds.y = Q_rint (bounds.y * scale);
		bounds.width = Q_rint (bounds.width * scale);
		bounds.height = Q_rint (bounds.height * scale);
		if (HUD_EditorPreview ())
		{
			HUD_EditorBounds (HUD_EDIT_LAYOUT_FIRST + current, bounds.x, bounds.y, bounds.width, bounds.height);
		}
		else
		{
			if (captured[current])
			{
				vrect_t old = last_bounds[current];
				int right = max (old.x + old.width, bounds.x + bounds.width);
				int bottom = max (old.y + old.height, bounds.y + bounds.height);
				bounds.x = min (old.x, bounds.x);
				bounds.y = min (old.y, bounds.y);
				bounds.width = right - bounds.x;
				bounds.height = bottom - bounds.y;
			}
			captured[current] = true;
			last_bounds[current] = bounds;
			last_width[current] = Q_rint (CL_JumpViewportWidth () * scale);
			last_height[current] = Q_rint (CL_JumpViewportHeight () * scale);
		}
	}
	current = -1;
}

qboolean HUD_LayoutMatch (const char *layout)
{
	if (strlen (layout) >= sizeof (matched_layout))
		return false;
	if (strcmp (layout, matched_layout))
	{
		Q_strlcpy (matched_layout, layout, sizeof (matched_layout));
		matched_count = HudLayout_Match (layout, server_statusbar_layout,
			q_countof (server_statusbar_layout),
			matched_tokens, q_countof (matched_tokens));
	}
	return matched_count > 0;
}

int HUD_LayoutToken (const char *layout, const char *position)
{
	while (*position && (unsigned char)*position <= ' ')
		position++;
	size_t offset = position - layout;
	{
		int i;
		for (i = 0; i < matched_count; i++)
			if (matched_tokens[i].start == offset)
				return matched_tokens[i].group;
	}
	return -1;
}

void HUD_LayoutSetBounds (int id, vrect_t bounds)
{
	if (id < 0 || id >= item_count)
		return;
	last_bounds[id] = bounds;
	last_width[id] = Q_rint (CL_JumpViewportWidth () * jump_screen.hud_scale);
	last_height[id] = Q_rint (CL_JumpViewportHeight () * jump_screen.hud_scale);
}

static int HUD_SourceValue (const char *name, int fallback)
{
	cvar_t *var = Cvar_FindVar (name);
	float value = var ? HUD_EditorValue (var) : fallback;
	return isfinite (value) ? (int)Q_clip (value, -16000, 16000) : fallback;
}

/* Honor existing position cvars even when their overlay has never been shown. */
static void HUD_PreviewOrigin (int id, int width, int height, vrect_t *r)
{
	if (id == HL_CROSSHAIR || id == HL_HITMARKER)
	{
		r->x += HUD_SourceValue ("ch_x", 0);
		r->y += HUD_SourceValue ("ch_y", 0);
	}
	else if (id == HL_NETALERT)
	{
		int x = HUD_SourceValue ("sh_netalert_x", 0), y = HUD_SourceValue ("sh_netalert_y", 353);
		r->x = x == 0 ? (width - r->width) / 2 : x < 0 ? width + x + 1 - r->width :
														 x;
		r->y = y == 0 ? (height - r->height) / 2 : y < 0 ? height + y - r->height + 1 :
														   y;
	}
	else if (id == HL_NETICON)
	{
		int x = HUD_SourceValue ("sh_lagometer_x", 166), y = HUD_SourceValue ("sh_lagometer_y", -1);
		r->width = r->height = 48;
		/* SCR_DrawNet preserves signed edge offsets even outside the viewport. */
		r->x = x < 0 ? width + x - r->width + 1 : x;
		r->y = y < 0 ? height + y - r->height + 1 : y;
	}
	else if (id == HL_CHAT)
	{
		int x = HUD_SourceValue ("scr_chathud_x", 8), y = HUD_SourceValue ("scr_chathud_y", -150);
		int lines = Q_clip (HUD_SourceValue ("scr_chathud_lines", 4), 1, 32);
		r->height = lines * CHAR_HEIGHT;
		r->x = x < 0 ? width + x + 1 - r->width : x;
		r->y = y < 0 ? height + y - r->height + 1 : y;
	}
	else if (id == HL_DEBUGGRAPH)
	{
		/* Both classic graph modes fill the HUD width and grow up from its bottom. */
		r->height = max (1, HUD_SourceValue ("sh_netgraph_height", defaults[id].height));
		if (HUD_SourceValue ("scr_netgraph", 0) == 2)
			r->height = Q_clip (r->height, 10, 200);
		r->x = 0;
		r->y = height - r->height;
		r->width = width;
	}
	else if (id == HL_DEMO)
	{
		r->width = width;
	}
}

qboolean HUD_LayoutCaptured (int id)
{
	return id >= 0 && id < item_count && captured[id];
}

qboolean HUD_LayoutPreviewAvailable (int id)
{
	if (id < 0 || id >= item_count)
		return false;
	switch (id)
	{
	case HL_TARGET:
	case HL_VOTE:
	case HL_CHAT:
	case HL_CENTER:
	case HL_NOTIFY:
	case HL_MESSAGE:
	case HL_NETALERT:
	case HL_NETICON:
	case HL_TURTLE:
	case HL_INVENTORY:
	case HL_SCOREBOARD:
	case HL_OTHER_STATUS:
	case HL_DEBUGGRAPH:
		return captured[id];
	default:
		return true;
	}
}

static const char *HUD_ScenarioSample (int id, hud_preview_scenario_t scenario, const char *fallback)
{
	switch (scenario)
	{
	case HUD_PREVIEW_RUNNING:
		if (id == HL_STATUS2)
			return "Run in progress";
		if (id == HL_CENTER)
			return "Checkpoint reached";
		if (id == HL_CHAT)
			return "Player: nice jump!";
		break;
	case HUD_PREVIEW_SPECTATING:
		if (id == HL_TARGET)
			return "SamplePlayer";
		if (id == HL_STATUS1)
			return "Spectating";
		break;
	case HUD_PREVIEW_VOTING:
		if (id == HL_VOTE)
			return "Vote: next map\nMap: sample_map\nYes: 5  No: 1\nTime left: 12";
		if (id == HL_CHAT)
			return "Player: vote yes";
		break;
	case HUD_PREVIEW_SCOREBOARD:
		if (id == HL_SCOREBOARD)
			return "SAMPLE SCOREBOARD\nPlayer                   Time\nRunner One              12.34\nRunner Two              15.67\nRunner Three            19.02\nSpectator                  --";
		break;
	case HUD_PREVIEW_NETWORK:
		if (id == HL_NETALERT)
			return "PACKET LOSS";
		if (id == HL_NETICON)
			return "Ping";
		break;
	default:
		break;
	}
	return fallback;
}

void HUD_LayoutPreview (void)
{
	float scale = jump_screen.hud_scale > 0 ? jump_screen.hud_scale : 1;
	int width = Q_rint (CL_JumpViewportWidth () * scale), height = Q_rint (CL_JumpViewportHeight () * scale);
	hud_preview_scenario_t scenario = HUD_EditorScenario ();
	{
		int i;
		for (i = 0; i < item_count; i++)
		{
			if (!HUD_LayoutEditable (i))
				continue;
			if (i == HL_BIND_REMINDERS)
			{
				SCR_PreviewBindReminders ();
				continue;
			}
			/* Measure every editable group for reference alignment; the draw group
         * suppresses emission according to the editor's ghost/focus controls. */
			vrect_t r = last_bounds[i];
			const char *sample = items[i].name;
			if (i < HL_BUILTIN_COUNT)
				sample = defaults[i].sample;
			if (!r.width || last_width[i] != width || last_height[i] != height)
			{
				if (i < HL_BUILTIN_COUNT)
				{
					r = (vrect_t){ Q_rint (width * defaults[i].ax) + defaults[i].x,
						Q_rint (height * defaults[i].ay) + defaults[i].y, defaults[i].width, defaults[i].height };
				}
				else
				{
					r = (vrect_t){ width - 136, 80 + (i - HL_BUILTIN_COUNT) * 10, 128, 8 };
				}
				HUD_PreviewOrigin (i, width, height, &r);
			}
			/* Shared graph settings may change after the last live capture. */
			if (i == HL_NETICON || i == HL_DEBUGGRAPH)
				HUD_PreviewOrigin (i, width, height, &r);
			/* Keep the live group's full extent so alignment keeps all of it on screen. */
			sample = HUD_ScenarioSample (i, scenario, sample);
			int lines = 1;
			{
				const char *p;
				for (p = sample; *p; p++)
					lines += *p == '\n';
			}
			if (i != HL_DEBUGGRAPH)
				r.height = max (r.height, lines * CHAR_HEIGHT);
			HUD_LayoutBegin (i);
			R_DrawFill32 (r.x, r.y, r.width, r.height, MakeColor (35, 50, 62, 75));
			R_SetColor (MakeColor (180, 205, 220, 220));
			{
				int y;
				for (y = r.y; *sample && y + CHAR_HEIGHT <= r.y + r.height; y += CHAR_HEIGHT)
				{
					const char *end = strchr (sample, '\n');
					size_t length = end ? (size_t)(end - sample) : strlen (sample);
					SCR_DrawStringEx (r.x, y, UI_NOSHADOW, min (length, (size_t)max (1, r.width / CHAR_WIDTH)), sample, jump_screen.font_pic);
					if (!end)
						break;
					sample = end + 1;
				}
			}
			HUD_LayoutEnd ();
		}
	}
	R_ClearColor ();
}
