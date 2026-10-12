/*
 * Q2PRO-style video configuration, adapted for R1Q2's boolean fullscreen setting.
 * Syntax reference: Q2PRO src/client/refresh.c (id Software / Q2PRO contributors).
 * This file is distributed under the GNU GPL, version 2 or later.
 * Private presentation helpers; no renderer import/export layouts are changed.
 */
#ifndef R1Q2_VID_CONFIG_H
#define R1Q2_VID_CONFIG_H

#include <stdlib.h>
#include <string.h>

#define VID_MAX_DISPLAY_MODES 4096

typedef struct
{
	int width, height, frequency, depth;
	qboolean desktop;
} vid_displaymode_t;

typedef struct
{
	int width, height, x, y;
} vid_geometry_t;

static const char *VID_SkipSpace (const char *s)
{
	while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
		s++;
	return s;
}

static qboolean VID_ReadNumber (const char **text, int limit, int *value)
{
	const char *s;
	int n, digit;

	s = *text;
	if (*s < '0' || *s > '9')
		return false;
	n = 0;
	do
	{
		digit = *s++ - '0';
		if (n > (limit - digit) / 10)
			return false;
		n = n * 10 + digit;
	} while (*s >= '0' && *s <= '9');
	*text = s;
	*value = n;
	return true;
}

static qboolean VID_ParseGeometry (const char *text, vid_geometry_t *geometry)
{
	const char *s;
	vid_geometry_t result;
	int sign;

	if (!text)
		return false;
	s = VID_SkipSpace (text);
	memset (&result, 0, sizeof (result));
	if (!VID_ReadNumber (&s, 8192, &result.width) || (*s != 'x' && *s != 'X'))
		return false;
	s++;
	if (!VID_ReadNumber (&s, 8192, &result.height))
		return false;
	if (*s == '+' || *s == '-')
	{
		sign = *s++ == '-' ? -1 : 1;
		if (!VID_ReadNumber (&s, 32767, &result.x))
			return false;
		result.x *= sign;
		if (*s == '+' || *s == '-')
		{
			sign = *s++ == '-' ? -1 : 1;
			if (!VID_ReadNumber (&s, 32767, &result.y))
				return false;
			result.y *= sign;
		}
	}
	if (*VID_SkipSpace (s) || result.width < 320 || result.height < 240)
		return false;
	*geometry = result;
	return true;
}

static qboolean VID_ParseDisplayMode (const char **text, vid_displaymode_t *mode)
{
	const char *s;
	vid_displaymode_t result;
	int first;

	s = VID_SkipSpace (*text);
	memset (&result, 0, sizeof (result));
	if (!strncmp (s, "desktop", 7))
	{
		s += 7;
		result.desktop = true;
	}
	else
	{
		if (!VID_ReadNumber (&s, 8192, &result.width) || (*s != 'x' && *s != 'X'))
			return false;
		s++;
		if (!VID_ReadNumber (&s, 8192, &result.height))
			return false;
		first = *s;
		if (first == '@' || first == ':')
		{
			s++;
			if (!VID_ReadNumber (&s, first == '@' ? 1000 : 32,
					first == '@' ? &result.frequency : &result.depth))
				return false;
			if (*s == (first == '@' ? ':' : '@'))
			{
				s++;
				if (!VID_ReadNumber (&s, first == '@' ? 32 : 1000,
						first == '@' ? &result.depth : &result.frequency))
					return false;
			}
		}
		if (result.width < 320 || result.height < 240)
			return false;
	}
	if (*s && VID_SkipSpace (s) == s)
		return false;
	*text = VID_SkipSpace (s);
	*mode = result;
	return true;
}

static qboolean VID_GetModeFromList (const char *text, int index, vid_displaymode_t *mode)
{
	vid_displaymode_t result;
	int i;

	if (!text || index < 1 || index > VID_MAX_DISPLAY_MODES + 1)
		return false;
	for (i = 1; i <= index; i++)
		if (!VID_ParseDisplayMode (&text, &result))
			return false;
	*mode = result;
	return true;
}

static int VID_CompareDisplayModes (const void *a, const void *b)
{
	const vid_displaymode_t *first, *second;

	first = (const vid_displaymode_t *)a;
	second = (const vid_displaymode_t *)b;
	if (first->width != second->width)
		return first->width < second->width ? -1 : 1;
	if (first->height != second->height)
		return first->height < second->height ? -1 : 1;
	if (first->frequency != second->frequency)
		return first->frequency < second->frequency ? -1 : 1;
	return 0;
}

#endif
