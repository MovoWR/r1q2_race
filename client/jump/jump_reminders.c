/* Binding reminders adapted from frozen Q2PRO Race commit
 * 5571a17dbb7fb03915e7b403b1f981b7006a1a05, src/client/bind_reminders.c.
 * GNU GPL version 2 or later; see COPYING. */
#include "jump_compat.h"
/* Read-only native services for the frozen reminder discovery code. */
#include <ctype.h>
extern cmd_function_t *cmd_functions;
extern cmdalias_t *cmd_alias;
extern int keydown[256];
#define ALIAS_LOOP_COUNT 16
#define NUM_FPS_SLOTS JUMP_FPS_SLOTS
#define Q_isspace(c) isspace((unsigned char)(c))
#define UI_IGNORECOLOR 0
#define CL_ParseFpsInteger CL_JumpParseInteger
#define Q_stricmpn Q_strncasecmp
static cvar_t *reminder_x, *reminder_y, *reminder_visible, *reminder_scale;

static qboolean SCR_JumpReminderFinite (float value)
{
	return value >= -FLT_MAX && value <= FLT_MAX;
}

static void Q_strlcat (char *out, const char *text, size_t size)
{
	size_t used = strlen (out);
	if (used < size)
		Q_strlcpy (out + used, text, size - used);
}

static char *COM_TrimSpace (char *text)
{
	char *end;
	while (*text && Q_isspace (*text))
		text++;
	end = text + strlen (text);
	while (end > text && Q_isspace (end[-1]))
		*--end = 0;
	return text;
}

static char *COM_StripQuotes (char *text)
{
	size_t length = strlen (text);
	if (length >= 2 && text[0] == '"' && text[length - 1] == '"')
	{
		text[length - 1] = 0;
		return text + 1;
	}
	return text;
}

static qboolean Cmd_Exists (const char *name)
{
	cmd_function_t *command;
	for (command = cmd_functions; command; command = command->next)
		if (!strcmp (command->name, name))
			return true;
	return false;
}

static const char *Cmd_AliasCommand (const char *name)
{
	cmdalias_t *alias;
	for (alias = cmd_alias; alias; alias = alias->next)
		if (!strcmp (alias->name, name))
			return alias->value;
	return NULL;
}

static const char *Key_BindingForKey (int key)
{
	return key >= 0 && key < 256 && keybindings[key] ? keybindings[key] : "";
}

static int Key_IsDown (int key)
{
	return key >= 0 && key < 256 ? keydown[key] : 0;
}

static int Key_EnumBindings (int start, const char *command)
{
	int key;
	for (key = start < 0 ? 0 : start; key < 256; key++)
		if (keybindings[key] && !Q_stricmp (keybindings[key], command))
			return key;
	return -1;
}

static const char *Key_KeynumToLabel (int key)
{
	return Key_KeynumToString (key);
}

cvar_t *SCR_RegisterBindReminderCvar (const char *name, const char *legacy_name, const char *value)
{
	qboolean exists = Cvar_FindVar (name) != NULL;
	cvar_t *legacy = Cvar_FindVar (legacy_name);
	cvar_t *var = Cvar_Get (name, value, CVAR_ARCHIVE);
	if (!exists && legacy)
		Cvar_Set (var->name, legacy->string);
	return var;
}

void SCR_BindRemindersInit (void);
void SCR_DrawBindReminders (float hud_alpha);
void SCR_PreviewBindReminders (void);
/* Read-only reminders for selected key bindings and automatic FPS bindings. */

#define BIND_REMINDER_COUNT 8
#define BIND_REMINDER_EXTENDED_COUNT 4
#define BIND_REMINDER_MARGIN 8
#define BIND_REMINDER_ANCHOR_Y 0.525f
#define BIND_REMINDER_STEP 14
#define BIND_REMINDER_KEY_CHARS 8
#define BIND_REMINDER_CAPTION_CHARS 18
#define BIND_REMINDER_KEYS 256
#define BIND_REMINDER_SCAN_LIMIT 128

typedef struct
{
	char keys[48];
	char caption[128];
	qboolean fps, active;
} bind_reminder_row_t;

static cvar_t *scr_bindreminders_alpha, *scr_bindreminders_fps;
static cvar_t *reminder_commands[BIND_REMINDER_COUNT];
static cvar_t *reminder_labels[BIND_REMINDER_COUNT];
static cvar_t *reminder_extended[BIND_REMINDER_EXTENDED_COUNT];

static const struct
{
	const char *name, *command, *label, *value;
} extended_reminders[BIND_REMINDER_EXTENDED_COUNT] = {
	{ "scr_bindreminders_menu", "inven", "Menu", "1" },
	{ "scr_bindreminders_respawn", "kill", "Respawn", "1" },
	{ "scr_bindreminders_store", "store", "Store", "0" },
	{ "scr_bindreminders_observer", "observer", "Observer", "0" },
};

void SCR_BindRemindersInit (void)
{
	int i;
	int j;

	static const char *const commands[BIND_REMINDER_COUNT] = {
		"toggle cl_maxfps 30 120", "team hard", "recall", "team easy", "", "", "", ""
	};
	static const char *const labels[BIND_REMINDER_COUNT] = {
		"30 / 120", "Team hard", "Recall", "Team easy", "", "", "", ""
	};
	char name[64], legacy_name[64];

	reminder_x = SCR_RegisterBindReminderCvar ("scr_bindreminders_x", "hud_bind_reminders_x", "0");
	reminder_y = SCR_RegisterBindReminderCvar ("scr_bindreminders_y", "hud_bind_reminders_y", "0");
	reminder_visible = SCR_RegisterBindReminderCvar ("scr_bindreminders_visible", "hud_bind_reminders_visible", "1");
	reminder_scale = SCR_RegisterBindReminderCvar ("scr_bindreminders_scale", "hud_bind_reminders_scale", "1");
	scr_bindreminders_alpha = Cvar_Get ("scr_bindreminders_alpha", "0.6", CVAR_ARCHIVE);
	scr_bindreminders_fps = Cvar_Get ("scr_bindreminders_fps", "1", CVAR_ARCHIVE);
	for (i = 0; i < BIND_REMINDER_COUNT; i++)
	{
		Q_snprintf (name, sizeof (name), "scr_bindreminders_command_%d", i + 1);
		Q_snprintf (legacy_name, sizeof (legacy_name), "scr_bindreminder_command_%d", i + 1);
		reminder_commands[i] = SCR_RegisterBindReminderCvar (name, legacy_name, commands[i]);
		Q_snprintf (name, sizeof (name), "scr_bindreminders_label_%d", i + 1);
		Q_snprintf (legacy_name, sizeof (legacy_name), "scr_bindreminder_label_%d", i + 1);
		reminder_labels[i] = SCR_RegisterBindReminderCvar (name, legacy_name, labels[i]);
	}
	for (i = 0; i < BIND_REMINDER_EXTENDED_COUNT; i++)
	{
		qboolean configured = Cvar_FindVar (extended_reminders[i].name) != NULL;
		reminder_extended[i] = Cvar_Get (extended_reminders[i].name, extended_reminders[i].value, CVAR_ARCHIVE);
		/* Keep an existing custom action visible when its switch is first added. */
		if (!configured && extended_reminders[i].command)
		{
			for (j = 0; j < BIND_REMINDER_COUNT; j++)
			{
				if (!Q_stricmp (reminder_commands[j]->string, extended_reminders[i].command))
				{
					Cvar_Set (reminder_extended[i]->name, "1");
					break;
				}
			}
		}
	}
}

/* Match the command tokenizer without touching its global arguments or invoking
 * macro callbacks. The caller has already split semicolons/newlines. */
static qboolean SCR_ReminderToken (const char **text, char *token, size_t size)
{
	const char *p = *text;
	size_t length = 0;
	qboolean quoted;

	while (*p && (unsigned char)*p <= ' ')
		p++;
	if (!*p)
		return false;
	quoted = *p == '"';
	if (quoted)
		p++;
	while (*p && *p != '"' && (quoted || (unsigned char)*p > ' '))
	{
		if (length + 1 < size)
			token[length++] = *p;
		p++;
	}
	if (quoted && *p == '"')
		p++;
	token[length] = 0;
	*text = p;
	return true;
}

/* Only complete finite numbers can identify an active FPS target. */
static qboolean SCR_FpsMatches (const char *value)
{
	int i;

	const char *values[] = { value, Cvar_VariableString ("cl_maxfps") };
	float fps[2];

	for (i = 0; i < q_countof (values); i++)
	{
		char *end;
		fps[i] = (float)strtod (values[i], &end);
		if (end == values[i] || !SCR_JumpReminderFinite (fps[i]))
			return false;
		while (Q_isspace (*end))
			end++;
		if (*end)
			return false;
	}
	return fps[0] == fps[1];
}

static qboolean SCR_FpsScript (const char *script, qboolean held, int depth, int *budget,
	char *caption, size_t size, qboolean *active);

static qboolean SCR_FpsBranch (const char *start, size_t length, qboolean held, int depth, int *budget,
	char *caption, size_t size, qboolean *active)
{
	char branch[MAX_STRING_CHARS];

	if (length >= sizeof (branch))
		return false;
	memcpy (branch, start, length);
	branch[length] = 0;
	return SCR_FpsScript (COM_StripQuotes (COM_TrimSpace (branch)), held, depth,
		budget, caption, size, active);
}

/* Inspect both possible branches; evaluating conditions would mutate command
 * state or invoke macro callbacks. Branch boundaries follow Cmd_If_f. */
static qboolean SCR_FpsConditional (const char *rest, qboolean held, int depth, int *budget,
	char *caption, size_t size, qboolean *active)
{
	const char *branch = rest, *p = rest;
	char token[128];
	qboolean found;

	if (SCR_ReminderToken (&p, token, sizeof (token)) && !Q_stricmp (token, "then"))
		branch = p;
	p = branch;
	for (;;)
	{
		const char *start = p;
		if (!SCR_ReminderToken (&p, token, sizeof (token)))
			break;
		if (!Q_stricmp (token, "else"))
		{
			found = SCR_FpsBranch (branch, start - branch, held, depth, budget, caption, size, active) ||
				SCR_FpsBranch (p, strlen (p), held, depth, budget, caption, size, active);
			*active = false;
			return found;
		}
	}
	found = SCR_FpsBranch (branch, strlen (branch), held, depth, budget, caption, size, active);
	*active = false;
	return found;
}

static qboolean SCR_FpsCommand (const char *line, qboolean held, int depth, int *budget,
	char *caption, size_t size, qboolean *active)
{
	const char *p;

	/* Keep complete tokens so truncated text cannot appear to be numeric. */
	char args[4][MAX_STRING_CHARS];
	const char *rest = line, *alias;
	int argc = 0;

	*active = false;
	while (argc < q_countof (args) && SCR_ReminderToken (&rest, args[argc], sizeof (args[0])))
		argc++;
	if (!argc)
		return false;

	/* Registered commands win over aliases, then aliases win over cvars. */
	if (!Cmd_Exists (args[0]))
	{
		alias = Cmd_AliasCommand (args[0]);
		if (alias)
			return SCR_FpsScript (alias, held, depth + 1, budget, caption, size, active);
		if (!strcmp (args[0], "cl_maxfps") && argc >= 2)
		{
			/* Cvar_Command joins every value token, not only the first. */
			if (argc != 2)
			{
				Q_strlcpy (caption, "FPS", size);
				return true;
			}
			Q_snprintf (caption, size, "%s FPS", args[1]);
			*active = SCR_FpsMatches (args[1]);
			return true;
		}
		return false;
	}

	if (!strcmp (args[0], "if") && argc == 4)
	{
		if (!SCR_FpsConditional (rest, held, depth, budget, caption, size, active))
			return false;
		Q_strlcpy (caption, "FPS", size);
		return true;
	}

	if (args[0][0] == 'f')
	{
		char *end, canonical[16];
		long value = strtol (args[0] + 1, &end, 10);
		Q_snprintf (canonical, sizeof (canonical), "f%ld", value);
		if (!*end && value >= 20 && value <= 120 && !strcmp (args[0], canonical))
		{
			Q_snprintf (caption, size, "%ld FPS", value);
			*active = SCR_FpsMatches (args[0] + 1);
			return true;
		}
	}

	if ((!strcmp (args[0], "+fps") || !strcmp (args[0], "-fps")) && argc >= 3)
	{
		/* Both literals must satisfy CL_FpsDown_f/CL_FpsUp_f. Leave macros
         * unevaluated, and do not infer an active target from half a pair. */
		qboolean known = !strchr (args[1], '$') && !strchr (args[2], '$');
		if (known && (!CL_ParseFpsInteger (args[1]) || !CL_ParseFpsInteger (args[2])))
			return false;
		*active = known && SCR_FpsMatches (args[args[0][0] == '-' ? 2 : 1]);
		/* Key_Event generates releases only for a raw leading '+'. */
		if (args[0][0] == '+' && depth == 0 && line[0] == '+')
		{
			Q_snprintf (caption, size, "%s > %s", args[1], args[2]);
			*active = known && SCR_FpsMatches (args[held ? 1 : 2]);
		}
		else
		{
			Q_snprintf (caption, size, "%s FPS", args[args[0][0] == '-' ? 2 : 1]);
		}
		return true;
	}
	if ((!strcmp (args[0], "+fps_hold") || !strcmp (args[0], "-fps_hold")) && argc >= 2)
	{
		char name[32];
		int slot = CL_ParseFpsInteger (args[1]);
		if (slot >= 1 && slot <= NUM_FPS_SLOTS)
		{
			Q_snprintf (name, sizeof (name), "%s_%d",
				args[0][0] == '-' ? "fps_release" : "fps_hold", slot);
			const char *value = Cvar_VariableString (name);
			if (!CL_ParseFpsInteger (value))
				return false;
			Q_strlcpy (caption, value, size);
			*active = SCR_FpsMatches (value);
			if (args[0][0] == '+' && depth == 0 && line[0] == '+')
			{
				if (!held)
					*active = false;
				Q_snprintf (name, sizeof (name), "fps_release_%d", slot);
				value = Cvar_VariableString (name);
				if (CL_ParseFpsInteger (value))
				{
					Q_strlcat (caption, " > ", size);
					Q_strlcat (caption, value, size);
					if (!held)
						*active = SCR_FpsMatches (value);
					return true;
				}
			}
			Q_strlcat (caption, " FPS", size);
			return true;
		}
		if (strchr (args[1], '$'))
		{
			Q_strlcpy (caption, "FPS", size);
			return true;
		}
		return false;
	}

	if (argc < 2 || strcmp (args[1], "cl_maxfps"))
		return false;
	if ((!strcmp (args[0], "set") || !strcmp (args[0], "seta") ||
			!strcmp (args[0], "setu") || !strcmp (args[0], "sets")) &&
		argc >= 3)
	{
		qboolean single_value = argc == 3;
		/* Only plain set treats an exact fourth u/s token as a flag. */
		if (!strcmp (args[0], "set") && argc == 4 &&
			(!strcmp (args[3], "u") || !strcmp (args[3], "s")))
		{
			while (*rest && (unsigned char)*rest <= ' ')
				rest++;
			single_value = !*rest;
		}
		if (!single_value)
		{
			Q_strlcpy (caption, "FPS", size);
			return true;
		}
		Q_snprintf (caption, size, "%s FPS", args[2]);
		*active = SCR_FpsMatches (args[2]);
		return true;
	}
	if (!strcmp (args[0], "toggle"))
	{
		caption[0] = 0;
		/* Re-read the preset list, including lists longer than two values. */
		rest = line;
		SCR_ReminderToken (&rest, args[0], sizeof (args[0]));
		SCR_ReminderToken (&rest, args[1], sizeof (args[1]));
		qboolean first = true, have_value = false;
		while (SCR_ReminderToken (&rest, args[2], sizeof (args[2])))
		{
			for (p = args[2]; *p; p++)
				have_value |= (unsigned char)*p > ' ';
			Q_strlcat (caption, first ? "" : " / ", size);
			Q_strlcat (caption, args[2], size);
			*active |= SCR_FpsMatches (args[2]);
			first = false;
		}
		if (!have_value)
			Q_strlcpy (caption, "FPS", size);
		return true;
	}
	if (!strcmp (args[0], "inc") || !strcmp (args[0], "dec"))
	{
		Q_snprintf (caption, size, "%c%s FPS", args[0][0] == 'i' ? '+' : '-',
			argc >= 3 ? args[2] : "1");
		return true;
	}
	if (!strcmp (args[0], "creset"))
	{
		Q_strlcpy (caption, "FPS reset", size);
		return true;
	}
	return false;
}

static qboolean SCR_FpsScript (const char *script, qboolean held, int depth, int *budget,
	char *caption, size_t size, qboolean *active)
{
	const char *p = script;

	*active = false;
	if (depth > ALIAS_LOOP_COUNT)
		return false;
	while (*p && *budget > 0)
	{
		const char *start = p;
		char line[MAX_STRING_CHARS];
		qboolean quoted = false;
		size_t length;

		/* Same separators as Cbuf_Execute, including newlines inside quotes. */
		while (*p)
		{
			if (*p == '"')
				quoted = !quoted;
			if (*p == '\n' || (*p == ';' && !quoted))
				break;
			p++;
		}
		length = p - start;
		if (*p)
			p++;
		(*budget)--;
		if (quoted || length >= sizeof (line))
			continue;
		memcpy (line, start, length);
		line[length] = 0;
		if (SCR_FpsCommand (line, held, depth, budget, caption, size, active))
		{
			if (start != script || *p)
			{
				Q_strlcpy (caption, "FPS", size);
				*active = false;
			}
			return true;
		}
	}
	return false;
}

static qboolean SCR_FpsBinding (const char *binding, qboolean held, char *caption, size_t size, qboolean *active)
{
	int budget = BIND_REMINDER_SCAN_LIMIT;
	qboolean found = SCR_FpsScript (binding, held, 0, &budget, caption, size, active);

	/* Key_Event also invokes the release side of raw +alias bindings. */
	if (binding[0] == '+' && strlen (binding) < MAX_STRING_CHARS)
	{
		const char *rest = binding;
		char command[MAX_STRING_CHARS];
		SCR_ReminderToken (&rest, command, sizeof (command));
		/* Direct FPS commands already describe their release behavior. */
		if (found && Cmd_Exists (command))
			return true;

		char release[MAX_STRING_CHARS], release_caption[MAX_STRING_CHARS];
		qboolean release_active;
		Q_strlcpy (release, binding, sizeof (release));
		release[0] = '-';
		budget = BIND_REMINDER_SCAN_LIMIT;
		if (SCR_FpsScript (release, held, 0, &budget, release_caption, sizeof (release_caption), &release_active))
		{
			if (found)
			{
				/* Keep the compact caption while matching only the current phase. */
				Q_strlcpy (caption, "FPS", size);
				if (!held)
					*active = release_active;
			}
			else
			{
				Q_strlcpy (caption, release_caption, size);
				Q_strlcat (caption, " up", size);
				*active = !held && release_active;
			}
			return true;
		}
	}
	return found;
}

/* A manual row can group keys in different phases, including keys beyond the
 * two labels shown in the keycap. Any matching phase may activate that row. */
static qboolean SCR_FpsBoundKeysActive (const char *command)
{
	int key;

	for (key = Key_EnumBindings (0, command); key >= 0;
		 key = Key_EnumBindings (key + 1, command))
	{
		char caption[128];
		qboolean active;
		if (SCR_FpsBinding (command, Key_IsDown (key) != 0, caption, sizeof (caption), &active) && active)
			return true;
	}
	return false;
}

/* Short familiar mouse names save space while other layout labels stay intact. */
static void SCR_ReminderKeyLabel (char *label)
{
	if (!Q_stricmpn (label, "MOUSE", 5) && label[5] >= '1' && label[5] <= '8' && !label[6])
	{
		label[0] = 'M';
		label[1] = label[5];
		label[2] = 0;
	}
	else if (!Q_stricmp (label, "MWHEELUP"))
	{
		strcpy (label, "MWUP");
	}
	else if (!Q_stricmp (label, "MWHEELDOWN"))
	{
		strcpy (label, "MWDN");
	}
}

static void SCR_BindReminderKeys (const char *command, char *text, size_t size)
{
	int i;

	int key = -1;

	text[0] = 0;
	for (i = 0; i < 2; i++)
	{
		key = Key_EnumBindings (key + 1, command);
		if (key < 0)
			break;
		if (i)
			Q_strlcat (text, " / ", size);
		/* Key labels may share a static buffer; copy each before asking again. */
		char label[48];
		Q_strlcpy (label, Key_KeynumToLabel (key), sizeof (label));
		SCR_ReminderKeyLabel (label);
		Q_strlcat (text, label, size);
	}
	if (!text[0])
		Q_strlcpy (text, "--", size);
	else if (key >= 0 && Key_EnumBindings (key + 1, command) >= 0)
		Q_strlcat (text, " +", size);
}

static float SCR_BindReminderAlpha (float value)
{
	return SCR_JumpReminderFinite (value) ? CLAMP (value, 0, 1) : 0;
}

static int SCR_CollectBindReminders (bind_reminder_row_t *rows)
{
	int key;
	int i;
	int j;

	int count = 0;
	qboolean auto_fps = scr_bindreminders_fps->intvalue != 0;
	qboolean extended_seen[BIND_REMINDER_EXTENDED_COUNT] = { false };
	if (auto_fps)
	{
		for (key = 0; key < BIND_REMINDER_KEYS; key++)
		{
			const char *binding = Key_BindingForKey (key);
			bind_reminder_row_t *row = &rows[count];
			if (!binding[0] || !SCR_FpsBinding (binding, Key_IsDown (key) != 0, row->caption, sizeof (row->caption), &row->active))
				continue;
			Q_strlcpy (row->keys, Key_KeynumToLabel (key), sizeof (row->keys));
			for (i = 0; i < BIND_REMINDER_COUNT; i++)
			{
				if (reminder_labels[i]->string[0] && !Q_stricmp (binding, reminder_commands[i]->string))
				{
					Q_strlcpy (row->caption, reminder_labels[i]->string, sizeof (row->caption));
					break;
				}
			}
			row->fps = true;
			count++;
		}
	}
	for (i = 0; i < BIND_REMINDER_COUNT; i++)
	{
		const char *command = reminder_commands[i]->string;
		const char *label = reminder_labels[i]->string;
		bind_reminder_row_t *row = &rows[count];

		if (!command[0])
			continue;
		qboolean enabled = true;
		for (j = 0; j < BIND_REMINDER_EXTENDED_COUNT; j++)
		{
			if (!Q_stricmp (command, extended_reminders[j].command))
			{
				extended_seen[j] = true;
				enabled = reminder_extended[j]->intvalue != 0;
				break;
			}
		}
		if (!enabled || (auto_fps && SCR_FpsBinding (command, false, row->caption, sizeof (row->caption), &row->active)))
			continue;
		row->fps = SCR_FpsBinding (command, false, row->caption, sizeof (row->caption), &row->active);
		SCR_BindReminderKeys (command, row->keys, sizeof (row->keys));
		row->active = row->fps && SCR_FpsBoundKeysActive (command);
		if (label[0] || !row->fps)
			Q_strlcpy (row->caption, label[0] ? label : command, sizeof (row->caption));
		count++;
	}
	for (i = 0; i < BIND_REMINDER_EXTENDED_COUNT; i++)
	{
		if (!reminder_extended[i]->intvalue || extended_seen[i])
			continue;
		bind_reminder_row_t *row = &rows[count];
		memset (row, 0, sizeof (*row));
		SCR_BindReminderKeys (extended_reminders[i].command, row->keys, sizeof (row->keys));
		if (!strcmp (row->keys, "--"))
			continue;
		Q_strlcpy (row->caption, extended_reminders[i].label, sizeof (row->caption));
		count++;
	}
	for (i = 0; i < count; i++)
		if (rows[i].fps && !strcmp (rows[i].caption, "FPS 30 / 120"))
			Q_strlcpy (rows[i].caption, "30 / 120", sizeof (rows[i].caption));
	return count;
}

/* Keep long labels inside the panel. Work on copies, never on config strings. */
static void SCR_ReminderText (char *text, size_t limit)
{
	char *p;

	size_t length = strlen (text);
	for (p = text; *p; p++)
		if ((unsigned char)*p < 32 || (unsigned char)*p == 127)
			*p = ' ';
	if (length > limit)
	{
		memcpy (text + limit - 3, "...", 3);
		text[limit] = 0;
	}
}

static void SCR_DrawReminderPanel (bind_reminder_row_t *rows, int count,
	float alpha)
{
	int i;

	int key_chars = 2, caption_chars = 1;

	if (!count)
		return;
	for (i = 0; i < count; i++)
	{
		SCR_ReminderKeyLabel (rows[i].keys);
		SCR_ReminderText (rows[i].keys, BIND_REMINDER_KEY_CHARS);
		SCR_ReminderText (rows[i].caption, BIND_REMINDER_CAPTION_CHARS);
		key_chars = max (key_chars, (int)strlen (rows[i].keys));
		caption_chars = max (caption_chars, (int)strlen (rows[i].caption));
	}
	int key_width = key_chars * CHAR_WIDTH + 10;
	int width = 12 + key_width + 8 + caption_chars * CHAR_WIDTH;
	int height = 12 + (count - 1) * BIND_REMINDER_STEP + CHAR_HEIGHT;
	int x = max (0, jump_screen.hud_width - width - BIND_REMINDER_MARGIN);
	int y = min (Q_rint (jump_screen.hud_height * BIND_REMINDER_ANCHOR_Y),
		max (0, jump_screen.hud_height - height - BIND_REMINDER_MARGIN));
	R_DrawFill32 (x, y, width, height, MakeColor (14, 18, 23, (int)(alpha * 204)));
	R_DrawFill32 (x, y, width, 1, MakeColor (130, 156, 173, (int)(alpha * 80)));
	y += 6;
	for (i = 0; i < count; i++)
	{
		const bind_reminder_row_t *row = &rows[i];
		R_DrawFill32 (x + 6, y - 2, key_width, 12,
			MakeColor (70, 82, 94, (int)(alpha * 180)));
		R_DrawFill32 (x + 6, y + 9, key_width, 1,
			MakeColor (147, 165, 181, (int)(alpha * 110)));
		if (row->active)
		{
			uint32 border = MakeColor (190, 202, 210, (int)(alpha * 210));
			R_DrawFill32 (x + 6, y - 2, key_width, 1, border);
			R_DrawFill32 (x + 6, y + 9, key_width, 1, border);
			R_DrawFill32 (x + 6, y - 1, 1, 10, border);
			R_DrawFill32 (x + 5 + key_width, y - 1, 1, 10, border);
		}
		R_SetColor (MakeColor (214, 224, 231, 255));
		R_SetAlpha (alpha);
		int key_x = x + 6 + (key_width - (int)strlen (row->keys) * CHAR_WIDTH) / 2;
		SCR_DrawStringEx (key_x, y, UI_IGNORECOLOR, BIND_REMINDER_KEY_CHARS,
			row->keys, jump_screen.font_pic);
		R_SetColor (MakeColor (242, 244, 246, 255));
		R_SetAlpha (alpha);
		/* The renderer clips after applying the saved HUD position. */
		SCR_DrawStringEx (x + 6 + key_width + 8, y, UI_IGNORECOLOR,
			BIND_REMINDER_CAPTION_CHARS, row->caption, jump_screen.font_pic);
		y += BIND_REMINDER_STEP;
	}
}

void SCR_DrawBindReminders (float hud_alpha)
{
	bind_reminder_row_t rows[BIND_REMINDER_KEYS + BIND_REMINDER_COUNT + BIND_REMINDER_EXTENDED_COUNT];

	if (!scr_bindreminders_alpha)
		return;
	if (!reminder_visible->value)
		return;
	int count = SCR_CollectBindReminders (rows);
	HUD_LayoutBegin (HL_BIND_REMINDERS);
	hud_alpha = SCR_BindReminderAlpha (hud_alpha);
	SCR_DrawReminderPanel (rows, count,
		SCR_BindReminderAlpha (scr_bindreminders_alpha->value) * hud_alpha);
	R_ClearColor ();
	R_SetAlpha (hud_alpha);
	HUD_LayoutEnd ();
}

void SCR_PreviewBindReminders (void)
{
	bind_reminder_row_t rows[BIND_REMINDER_KEYS + BIND_REMINDER_COUNT + BIND_REMINDER_EXTENDED_COUNT];
	int count = scr_bindreminders_alpha ? SCR_CollectBindReminders (rows) : 0;

	/* Match gameplay bounds for alignment, even when the panel is disabled. */
	if (!count)
	{
		const bind_reminder_row_t sample[] = {
			{ "1", "20 FPS", true, true },
			{ "MOUSE4", "30 / 120", true, false },
			{ "MOUSE5", "30 > 120", true, false },
			{ "K", "Respawn", false, false },
			{ "F1", "Menu", false, false },
			{ "F5", "Team hard", false, false },
			{ "F6", "Team easy", false, false },
		};
		memcpy (rows, sample, sizeof (sample));
		count = q_countof (sample);
	}
	HUD_LayoutBegin (HL_BIND_REMINDERS);
	SCR_DrawReminderPanel (rows, count, 0.8f);
	R_ClearColor ();
	HUD_LayoutEnd ();
}
