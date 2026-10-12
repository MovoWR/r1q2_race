/* Private menu storage for Q2PRO-style detected modes; GPL version 2 or later. */
#ifndef R1Q2_VID_MODE_MENU_H
#define R1Q2_VID_MODE_MENU_H

#include "vid_config.h"
#include <stdio.h>

typedef struct
{
	const char **names;
	char *labels;
	int count;
} vid_modemenu_t;

static void VID_FreeModeMenu (vid_modemenu_t *menu)
{
	free (menu->names);
	free (menu->labels);
	memset (menu, 0, sizeof (*menu));
}

static qboolean VID_BuildModeMenu (vid_modemenu_t *menu, const char *list)
{
	vid_displaymode_t mode;
	const char *text;
	char *label;
	int i, count;

	VID_FreeModeMenu (menu);
	text = list;
	count = 0;
	while (count < VID_MAX_DISPLAY_MODES + 1 && VID_ParseDisplayMode (&text, &mode))
		count++;
	menu->names = malloc ((count + 2) * sizeof (*menu->names));
	menu->labels = malloc ((count + 1) * 32);
	if (!menu->names || !menu->labels)
	{
		VID_FreeModeMenu (menu);
		return false;
	}
	strcpy (menu->labels, "legacy resolution");
	menu->names[0] = menu->labels;
	text = list;
	for (i = 1; i <= count; i++)
	{
		VID_ParseDisplayMode (&text, &mode);
		label = menu->labels + i * 32;
		if (mode.desktop)
			strcpy (label, "desktop");
		else if (mode.frequency && mode.depth)
			sprintf (label, "%dx%d @ %d Hz : %d", mode.width, mode.height, mode.frequency, mode.depth);
		else if (mode.depth)
			sprintf (label, "%dx%d : %d-bit", mode.width, mode.height, mode.depth);
		else if (mode.frequency)
			sprintf (label, "%dx%d @ %d Hz", mode.width, mode.height, mode.frequency);
		else
			sprintf (label, "%dx%d", mode.width, mode.height);
		menu->names[i] = label;
	}
	menu->names[count + 1] = NULL;
	menu->count = count;
	return true;
}

#endif
