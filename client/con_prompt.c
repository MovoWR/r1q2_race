/*
 * Console editing and history adapted from Q2PRO field.c/prompt.c.
 * Copyright (C) 2003-2006 Andrey Nazarov
 * GPL version 2 or (at your option) any later version.
 * Native completion uses a private parser, leaving command execution state intact.
 */
#include "client.h"
#include "con_prompt.h"
#include <ctype.h>

extern cmd_function_t *cmd_functions;
extern cmdalias_t *cmd_alias;
extern cvar_t *cvar_vars;
extern qboolean keydown[256];
extern char **FS_JumpList (const char *, const char *, qboolean, int *);
extern void CL_JumpFieldCopy (const char *);
extern qboolean Key_GetOverstrikeMode (void);
extern void Key_SetOverstrikeMode (qboolean);

void Con_FieldReplace (con_field_t *field, const char *text)
{
	Q_strncpy (field->text, text ? text : "", sizeof (field->text) - 1);
	field->cursor = (int)strlen (field->text);
}

static int Con_WordLeft (const con_field_t *field)
{
	int pos = field->cursor;
	while (pos && (unsigned char)field->text[pos - 1] <= ' ')
		pos--;
	while (pos && (unsigned char)field->text[pos - 1] > ' ')
		pos--;
	return pos;
}

static int Con_WordRight (const con_field_t *field)
{
	int pos = field->cursor;
	while (field->text[pos] && (unsigned char)field->text[pos] <= ' ')
		pos++;
	while ((unsigned char)field->text[pos] > ' ')
		pos++;
	return pos;
}

qboolean Con_FieldKey (con_field_t *field, int key)
{
	int end, start = field->cursor;
	qboolean ctrl = keydown[K_CTRL], alt = keydown[K_ALT];
	if ((ctrl || alt) && key >= 'A' && key <= 'Z')
		key += 'a' - 'A';
	if (key == K_DEL)
	{
		end = ctrl ? Con_WordRight (field) : start + (field->text[start] != 0);
		memmove (field->text + start, field->text + end, strlen (field->text + end) + 1);
	}
	else if (key == K_BACKSPACE || (ctrl && (key == 'h' || key == 'w')))
	{
		end = start;
		start = ctrl && key != 'h' ? Con_WordLeft (field) : (start ? start - 1 : 0);
		memmove (field->text + start, field->text + end, strlen (field->text + end) + 1);
		field->cursor = start;
	}
	else if (ctrl && key == 'u')
	{
		memmove (field->text, field->text + start, strlen (field->text + start) + 1);
		field->cursor = 0;
	}
	else if (ctrl && key == 'k')
		field->text[start] = 0;
	else if (ctrl && key == 'c')
		CL_JumpFieldCopy (field->text);
	else if ((ctrl && key == K_LEFTARROW) || (alt && key == 'b'))
		field->cursor = Con_WordLeft (field);
	else if ((ctrl && key == K_RIGHTARROW) || (alt && key == 'f'))
		field->cursor = Con_WordRight (field);
	else if (key == K_LEFTARROW || (ctrl && key == 'b'))
		field->cursor -= start != 0;
	else if (key == K_RIGHTARROW || (ctrl && key == 'f'))
		field->cursor += field->text[start] != 0;
	else if (key == K_HOME || (ctrl && key == 'a'))
		field->cursor = 0;
	else if (key == K_END || (ctrl && key == 'e'))
		field->cursor = (int)strlen (field->text);
	else if (key == K_INS)
		Key_SetOverstrikeMode (!Key_GetOverstrikeMode ());
	else
		return false;
	return true;
}

void Con_FieldChar (con_field_t *field, int key)
{
	int length = (int)strlen (field->text), pos = field->cursor;
	if (key < 32 || key >= 127 || pos >= MAXCMDLINE - 1)
		return;
	if (Key_GetOverstrikeMode ())
	{
		field->text[pos] = (char)key;
		if (pos == length)
			field->text[pos + 1] = 0;
	}
	else
	{
		if (length >= MAXCMDLINE - 1)
			return;
		memmove (field->text + pos + 1, field->text + pos, length - pos + 1);
		field->text[pos] = (char)key;
	}
	field->cursor++;
}

void Con_PromptClearState (con_prompt_t *prompt)
{
	prompt->search[0] = 0;
	prompt->searching = false;
	prompt->too_many = false;
}

qboolean Con_PromptAction (con_prompt_t *prompt, char *text)
{
	const char *s = prompt->field.text;
	unsigned previous = (prompt->head - 1) & CON_HISTORY_MASK;
	Con_PromptClearState (prompt);
	Q_strncpy (text, s, MAXCMDLINE - 1);
	if (!*s || ((*s == '/' || *s == '\\') && !s[1]))
	{
		Con_FieldReplace (&prompt->field, "");
		return false;
	}
	if (!prompt->count || strcmp (prompt->history[previous], s))
	{
		Q_strncpy (prompt->history[prompt->head & CON_HISTORY_MASK], s, MAXCMDLINE - 1);
		prompt->head++;
		if (prompt->count < CON_HISTORY_SIZE)
			prompt->count++;
	}
	prompt->position = prompt->head;
	prompt->draft[0] = 0;
	Con_FieldReplace (&prompt->field, "");
	return true;
}

void Con_PromptHistory (con_prompt_t *prompt, int direction, qboolean search)
{
	unsigned pos = prompt->position;
	const char *s;
	prompt->too_many = false;
	if (pos == prompt->head)
		Q_strncpy (prompt->draft, prompt->field.text, MAXCMDLINE - 1);
	if (search)
	{
		if (!prompt->searching)
		{
			s = prompt->field.text;
			if (*s == '/' || *s == '\\')
				s++;
			Q_strncpy (prompt->search, s, MAXCMDLINE - 1);
			prompt->searching = true;
		}
	}
	else
		Con_PromptClearState (prompt);
	while (true)
	{
		if (direction < 0)
		{
			if (prompt->head - pos >= prompt->count)
				return;
			pos--;
		}
		else
		{
			if (pos == prompt->head)
				return;
			pos++;
		}
		s = pos == prompt->head ? prompt->draft : prompt->history[pos & CON_HISTORY_MASK];
		if (!search || (strstr (s, prompt->search) && strcmp (s, prompt->field.text)))
			break;
	}
	prompt->position = pos;
	Con_FieldReplace (&prompt->field, s);
}

void Con_PromptSave (const con_prompt_t *prompt, const char *path, int lines)
{
	FILE *file;
	unsigned i, count;
	if (lines < 1)
		return;
	count = lines > CON_HISTORY_SIZE ? CON_HISTORY_SIZE : lines;
	if (count > prompt->count)
		count = prompt->count;
	FS_CreatePath ((char *)path);
	file = fopen (path, "wb");
	if (!file)
		return;
	for (i = prompt->head - count; i != prompt->head; i++)
		fprintf (file, "%s\n", prompt->history[i & CON_HISTORY_MASK]);
	if (fclose (file))
		Com_Printf ("Couldn't save console history.\n", LOG_CLIENT);
}

qboolean Con_PromptLoad (con_prompt_t *prompt, const char *path)
{
	FILE *file = fopen (path, "rb");
	char line[MAXCMDLINE], saved[MAXCMDLINE];
	if (!file)
		return false;
	memset (prompt, 0, sizeof (*prompt));
	while (fgets (line, sizeof (line), file))
	{
		size_t length = strlen (line);
		if (length && line[length - 1] != '\n' && !feof (file))
		{
			int ch;
			while ((ch = fgetc (file)) != EOF && ch != '\n')
			{
			}
		}
		while (length && (line[length - 1] == '\r' || line[length - 1] == '\n'))
			line[--length] = 0;
		if (!length)
			continue;
		Con_FieldReplace (&prompt->field, line);
		Con_PromptAction (prompt, saved);
	}
	fclose (file);
	return true;
}

/* Completion is bounded and independent of Cmd_Argv/Cmd_Args. */
#define CON_MAX_MATCHES 16384

typedef struct
{
	char *text;
	int kind;
} con_match_t;

typedef struct
{
	con_match_t *matches;
	int count;
	const char *partial;
} con_matches_t;

static void Con_AddMatch (con_matches_t *ctx, const char *text, int kind)
{
	int i;
	size_t length = strlen (text);
	if (!length || strchr (text, '"') || strchr (text, '\n') || strchr (text, '\r') ||
		Q_strncasecmp (text, ctx->partial, (int)strlen (ctx->partial)) || ctx->count == CON_MAX_MATCHES)
		return;
	for (i = 0; i < ctx->count; i++)
		if (!Q_stricmp (ctx->matches[i].text, text))
			return;
	ctx->matches[ctx->count].text = malloc (length + 1);
	if (!ctx->matches[ctx->count].text)
		return;
	memcpy (ctx->matches[ctx->count].text, text, length + 1);
	ctx->matches[ctx->count++].kind = kind;
}

static void Con_Names (con_matches_t *ctx, int types)
{
	cmd_function_t *cmd;
	cmdalias_t *alias;
	cvar_t *var;
	if (types & 1)
		for (cmd = cmd_functions; cmd; cmd = cmd->next)
			Con_AddMatch (ctx, cmd->name, 1);
	if (types & 2)
		for (var = cvar_vars; var; var = var->next)
			Con_AddMatch (ctx, var->name, 2);
	if (types & 4)
		for (alias = cmd_alias; alias; alias = alias->next)
			Con_AddMatch (ctx, alias->name, 4);
}

static void Con_FileMatches (con_matches_t *ctx, const char *directory, const char *suffix, qboolean strip)
{
	char path[MAX_QPATH], candidate[MAX_QPATH];
	char **files;
	const char *slash = strrchr (ctx->partial, '/');
	int i, count, prefix = slash ? (int)(slash - ctx->partial + 1) : 0;
	if (prefix >= MAX_QPATH / 2)
		return;
	Com_sprintf (path, sizeof (path), "%s%s%.*s", directory, *directory && prefix ? "/" : "", prefix, ctx->partial);
	if (prefix && path[0])
		path[strlen (path) - 1] = 0;
	files = FS_JumpList (path, suffix, false, &count);
	for (i = 0; i < count; i++)
	{
		Com_sprintf (candidate, sizeof (candidate), "%.*s%s", prefix, ctx->partial, files[i]);
		if (strip && suffix && strlen (candidate) >= strlen (suffix))
			candidate[strlen (candidate) - strlen (suffix)] = 0;
		Con_AddMatch (ctx, candidate, 8);
		free (files[i]);
	}
	free (files);
	/* Directory candidates permit subsequent TAB completion inside them. */
	files = FS_JumpList (path, NULL, true, &count);
	for (i = 0; i < count; i++)
	{
		Com_sprintf (candidate, sizeof (candidate), "%.*s%s/", prefix, ctx->partial, files[i]);
		Con_AddMatch (ctx, candidate, 8);
		free (files[i]);
	}
	free (files);
}

static int Con_MatchCompare (const void *a, const void *b)
{
	return Q_stricmp (((const con_match_t *)a)->text, ((const con_match_t *)b)->text);
}

static void Con_ShowMatches (con_matches_t *ctx, int width, int mode)
{
	int kind, i, col, columns, longest = 0;
	for (i = 0; i < ctx->count; i++)
		if ((int)strlen (ctx->matches[i].text) > longest)
			longest = (int)strlen (ctx->matches[i].text);
	columns = mode == 0 ? 1 : width / (longest + 2);
	if (columns < 1)
		columns = 1;
	if (columns > 6)
		columns = 6;
	for (kind = 1; kind <= 8; kind *= 2)
	{
		col = 0;
		if (mode == 2)
		{
			qboolean found = false;
			for (i = 0; i < ctx->count; i++)
				if (ctx->matches[i].kind == kind)
					found = true;
			if (!found)
				continue;
			Com_Printf ("%s:\n", LOG_CLIENT, kind == 1 ? "Commands" : kind == 2 ? "Variables" :
					kind == 4													? "Aliases" :
																				  "Arguments");
		}
		for (i = 0; i < ctx->count; i++)
		{
			if (mode == 2 && ctx->matches[i].kind != kind)
				continue;
			Com_Printf ("%-*s", LOG_CLIENT, longest + 2, ctx->matches[i].text);
			if (++col == columns)
			{
				Com_Printf ("\n", LOG_CLIENT);
				col = 0;
			}
		}
		if (col)
			Com_Printf ("\n", LOG_CLIENT);
		if (mode != 2)
			break;
	}
}

void Con_PromptComplete (con_prompt_t *prompt, int width)
{
	con_matches_t ctx;
	char command[MAXCMDLINE], partial[MAXCMDLINE], result[MAXCMDLINE], replacement[MAXCMDLINE];
	char *text = prompt->field.text;
	int pos = prompt->field.cursor, length = (int)strlen (text), begin = 0, end, arg = 0, i, token, quoted = 0;
	int threshold, mode, prefix, common;
	qboolean quote;
	if (*text != '/' && *text != '\\')
	{
		if (length >= MAXCMDLINE - 1)
			return;
		memmove (text + 1, text, length + 1);
		text[0] = '\\';
		pos++;
		length++;
	}
	for (i = 1; i < pos; i++)
	{
		if (text[i] == '"')
			quoted ^= 1;
		else if (!quoted && text[i] == ';')
			begin = i;
	}
	i = begin + 1;
	command[0] = partial[0] = 0;
	while (true)
	{
		while (i < pos && (unsigned char)text[i] <= ' ')
			i++;
		token = i;
		quoted = 0;
		while (text[i])
		{
			if (text[i] == '"')
				quoted ^= 1;
			else if (!quoted && ((unsigned char)text[i] <= ' ' || text[i] == ';'))
				break;
			i++;
		}
		end = i;
		if (pos <= end || !text[i])
			break;
		if (!arg)
		{
			prefix = text[token] == '"';
			Com_sprintf (command, sizeof (command), "%.*s", end - token - prefix - (prefix && text[end - 1] == '"'), text + token + prefix);
		}
		arg++;
		i++;
	}
	prefix = text[token] == '"';
	Com_sprintf (partial, sizeof (partial), "%.*s", end - token - prefix - (prefix && end > token && text[end - 1] == '"'), text + token + prefix);
	memset (&ctx, 0, sizeof (ctx));
	ctx.partial = partial;
	ctx.matches = calloc (CON_MAX_MATCHES, sizeof (*ctx.matches));
	if (!ctx.matches)
		return;
	if (!arg)
		Con_Names (&ctx, 7);
	else if (!Q_stricmp (command, "set") || !Q_stricmp (command, "seta") || !Q_stricmp (command, "toggle") || !Q_stricmp (command, "reset") || !Q_stricmp (command, "cvarhelp"))
	{
		if (arg == 1)
			Con_Names (&ctx, 2);
	}
	else if (!Q_stricmp (command, "alias") || !Q_stricmp (command, "unalias"))
	{
		if (arg == 1)
			Con_Names (&ctx, 4);
		else
			Con_Names (&ctx, 7);
	}
	else if (!Q_stricmp (command, "bind") || !Q_stricmp (command, "unbind"))
	{
		if (arg == 1)
		{
			for (i = 0; i < 256; i++)
				Con_AddMatch (&ctx, Key_KeynumToString (i), 8);
		}
		else
			Con_Names (&ctx, 7);
	}
	else if (arg == 1 && !Q_stricmp (command, "exec"))
		Con_FileMatches (&ctx, "", ".cfg", false);
	else if (arg == 1 && (!Q_stricmp (command, "map") || !Q_stricmp (command, "gamemap")))
		Con_FileMatches (&ctx, "maps", ".bsp", true);
	else if (arg == 1 && (!Q_stricmp (command, "demomap") || !Q_stricmp (command, "playdemo")))
		Con_FileMatches (&ctx, "demos", ".dm2", true);
	else if (!Q_stricmp (command, "play"))
		Con_FileMatches (&ctx, "sound", ".wav", false);
	else if (arg == 1 && !Q_stricmp (command, "sky"))
	{
		Con_FileMatches (&ctx, "env", "rt.tga", true);
		Con_FileMatches (&ctx, "env", "rt.pcx", true);
	}
	else if (arg == 1)
	{
		cvar_t *var = Cvar_FindVar (command);
		if (var)
		{
			const char *value = Cvar_DefaultString (var);
			Con_AddMatch (&ctx, var->string, 8);
			if (value)
				Con_AddMatch (&ctx, value, 8);
		}
	}
	if (!ctx.count)
	{
		prompt->too_many = false;
		goto done;
	}
	threshold = (int)Cvar_VariableValue ("com_completion_treshold");
	if (threshold < 1)
		threshold = 1;
	if (ctx.count > threshold && !prompt->too_many)
	{
		Com_Printf ("Press TAB again to display all %d possibilities.\n", LOG_CLIENT, ctx.count);
		prompt->too_many = true;
		goto done;
	}
	prompt->too_many = false;
	qsort (ctx.matches, ctx.count, sizeof (*ctx.matches), Con_MatchCompare);
	common = (int)strlen (ctx.matches[0].text);
	for (i = 1; i < ctx.count; i++)
	{
		int j = 0;
		while (j < common && tolower ((unsigned char)ctx.matches[0].text[j]) == tolower ((unsigned char)ctx.matches[i].text[j]))
			j++;
		common = j;
	}
	quote = false;
	for (i = 0; i < common; i++)
		if ((unsigned char)ctx.matches[0].text[i] <= ' ' || ctx.matches[0].text[i] == '$' || ctx.matches[0].text[i] == ';')
			quote = true;
	Com_sprintf (replacement, sizeof (replacement), "%s%.*s%s", quote ? "\"" : "", common, ctx.matches[0].text, quote ? "\"" : "");
	/* Keep all trailing commands/arguments. Refuse truncation of the edited line. */
	pos = token + (int)strlen (replacement);
	prefix = ctx.count == 1 && ctx.matches[0].text[common - 1] != '/' && !text[end];
	if (pos + prefix + strlen (text + end) >= sizeof (result))
		goto done;
	Com_sprintf (result, sizeof (result), "%.*s%s%s%s", token, text, replacement, prefix ? " " : "", text + end);
	Con_FieldReplace (&prompt->field, result);
	prompt->field.cursor = pos + prefix;
	if (ctx.count > 1)
	{
		Com_Printf ("]%s\n", LOG_CLIENT, text);
		mode = (int)Cvar_VariableValue ("com_completion_mode");
		Con_ShowMatches (&ctx, width, mode);
	}
done:
	for (i = 0; i < ctx.count; i++)
		free (ctx.matches[i].text);
	free (ctx.matches);
}
