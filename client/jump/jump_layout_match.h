/* Exact token matching: unknown status-bar programs remain unmodified. */
#pragma once

#include <stddef.h>
#include <string.h>

#define HUD_LAYOUT_TEXT_MAX 8192
#define HUD_LAYOUT_TOKEN_MAX 512

typedef struct
{
	const char *text;
	int group;
} hud_layout_fragment_t;

typedef struct
{
	size_t start;
	int group;
} hud_layout_token_t;

static qboolean HudLayout_Next (const char **cursor, char *out, size_t size, const char **start)
{
	const char *p = *cursor;
	while (*p && (unsigned char)*p <= ' ')
		p++;
	*start = p;
	if (!*p)
	{
		*cursor = p;
		return false;
	}
	qboolean quoted = *p == '"';
	if (quoted)
		p++;
	size_t n = 0;
	while (*p && (quoted ? *p != '"' : (unsigned char)*p > ' '))
	{
		if (n + 1 >= size)
			return false;
		out[n++] = *p++;
	}
	if (quoted)
	{
		if (*p != '"')
			return false;
		p++;
	}
	out[n] = 0;
	*cursor = p;
	return true;
}

static int HudLayout_Match (const char *layout, const hud_layout_fragment_t *fragments,
	size_t count, hud_layout_token_t *tokens, size_t capacity)
{
	if (strlen (layout) >= HUD_LAYOUT_TEXT_MAX)
		return 0;
	const char *actual = layout, *start, *unused;
	char a[256], b[256];
	size_t n = 0;
	{
		size_t i;
		for (i = 0; i < count; i++)
		{
			const char *expected = fragments[i].text;
			while (HudLayout_Next (&expected, b, sizeof (b), &unused))
			{
				if (n == capacity || !HudLayout_Next (&actual, a, sizeof (a), &start))
					return 0;
				if (strcmp (b, "@map") && strcmp (a, b))
					return 0;
				tokens[n++] = (hud_layout_token_t){ (size_t)(start - layout), fragments[i].group };
			}
		}
	}
	while (*actual && (unsigned char)*actual <= ' ')
		actual++;
	return *actual ? 0 : (int)n;
}
