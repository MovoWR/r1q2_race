/*
Copyright (C) 2008 Andrey Nazarov

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
/* Adapted from frozen Q2PRO Race 5571a17dbb7fb03915e7b403b1f981b7006a1a05. */
#include "jump_menu.h"


bool ui_builtin_menu_active = false;

static menuSound_t Activate (menuCommon_t *self)
{
	switch (self->type)
	{
	case MTYPE_ACTION:
		Cbuf_AddText (((menuAction_t *)self)->cmd);
		Cbuf_AddText ("\n");
		break;
	case MTYPE_BITMAP:
		Cbuf_AddText (((menuBitmap_t *)self)->cmd);
		Cbuf_AddText ("\n");
		break;
	case MTYPE_SAVEGAME:
		Cbuf_AddText (va ("save \"%s\"; forcemenuoff\n", ((menuAction_t *)self)->cmd));
		break;
	case MTYPE_LOADGAME:
		Cbuf_AddText (va ("load \"%s\"\n", ((menuAction_t *)self)->cmd));
		break;
	default:
		break;
	}

	return QMS_NOTHANDLED;
}

static const cmd_option_t o_common[] = {
	{ "s:", "status" },
	{ "v", "show-value" },
	{ "d", "defer" },
	{ NULL }
};

static void SetShowIf (menuCommon_t *item, const char *cvar, const char *val)
{
	if (cvar && val)
	{
		item->show_if_cvar = UI_CopyString (cvar);
		item->show_if_value = UI_CopyString (val);
	}
}

static void ParseShowIf (char **show_if_cvar, char **show_if_value)
{
	int i;
	*show_if_cvar = NULL;
	*show_if_value = NULL;
	for (i = 1; i < Cmd_Argc (); i++)
	{
		if (strcmp (Cmd_Argv (i), "--show-if") == 0 || strcmp (Cmd_Argv (i), "-I") == 0)
		{
			if (i + 2 < Cmd_Argc ())
			{
				*show_if_cvar = Cmd_Argv (i + 1);
				*show_if_value = Cmd_Argv (i + 2);
				Cmd_RemoveArg (i);
				Cmd_RemoveArg (i);
				Cmd_RemoveArg (i);
				return;
			}
			else
			{
				Com_Printf ("Missing arguments for --show-if option.\n");
				return;
			}
		}
	}
}

static void add_string (menuSpinControl_t *s, const char *tok)
{
	if (s->numItems < MAX_MENU_ITEMS)
	{
		s->itemnames = Z_Realloc (s->itemnames, Q_ALIGN (s->numItems + 2, MIN_MENU_ITEMS) * sizeof (char *));
		s->itemnames[s->numItems++] = UI_CopyString (tok);
	}
}

static void add_expand (menuSpinControl_t *s, const char *tok)
{
	char buf[MAX_STRING_CHARS], *temp = NULL;
	const char *data;

	cmd_macro_t *macro = Cmd_FindMacro (tok);
	if (macro)
	{
		size_t len = macro->function (buf, sizeof (buf));
		if (len < sizeof (buf))
		{
			data = buf;
		}
		else if (len < INT_MAX)
		{
			data = temp = UI_Malloc (len + 1);
			macro->function (temp, len + 1);
		}
		else
		{
			Com_Printf ("Expanded line exceeded %i chars, discarded.\n", INT_MAX);
			return;
		}
	}
	else
	{
		cvar_t *var = Cvar_FindVar (tok);
		if (var && !(var->flags & CVAR_PRIVATE))
			data = var->string;
		else
			return;
	}

	while (1)
	{
		tok = CL_JumpUIParse (&data);
		if (!data)
			break;
		add_string (s, tok);
	}

	Z_Free (temp);
}

static void long_args_hack (menuSpinControl_t *s, int argc)
{
	int i;

	s->itemnames = UI_Malloc (MIN_MENU_ITEMS * sizeof (char *));

	for (i = 0; i < argc; i++)
	{
		char *tok = Cmd_Argv (cmd_optind + i);
		if (*tok == '$')
		{
			tok++;
			if (*tok == '$')
				add_string (s, tok);
			else
				add_expand (s, tok);
		}
		else
		{
			add_string (s, tok);
		}
	}

	s->itemnames[s->numItems] = NULL;
}

static void Parse_Spin (menuFrameWork_t *menu, menuType_t type)
{
	menuSpinControl_t *s;
	int c, i, numItems;
	int flags = 0;
	char *status = NULL;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_common)) != -1)
	{
		switch (c)
		{
		case 's':
			status = cmd_optarg;
			break;
		case 'v':
			flags |= QMF_SHOW_VALUE;
			break;
		case 'd':
			flags |= QMF_DEFER_COMMIT;
			break;
		default:
			return;
		}
	}

	numItems = Cmd_Argc () - (cmd_optind + 2);
	if (numItems < 1)
	{
		Com_Printf ("Usage: %s <name> <cvar> <desc1> [...]\n", Cmd_Argv (0));
		return;
	}

	s = UI_Mallocz (sizeof (*s));
	s->generic.type = type;
	s->generic.name = UI_CopyString (Cmd_Argv (cmd_optind));
	s->generic.status = UI_CopyString (status);
	s->generic.flags = flags;
	s->cvar = Cvar_WeakGet (Cmd_Argv (cmd_optind + 1));

	cmd_optind += 2;
	if (strchr (Cmd_ArgsFrom (cmd_optind), '$'))
	{
		long_args_hack (s, numItems);
	}
	else
	{
		s->itemnames = UI_Mallocz (sizeof (char *) * (numItems + 1));
		for (i = 0; i < numItems; i++)
		{
			s->itemnames[i] = UI_CopyString (Cmd_Argv (cmd_optind + i));
		}
		s->numItems = numItems;
	}

	SetShowIf (&s->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, s);
}

static void Parse_Pairs (menuFrameWork_t *menu)
{
	menuSpinControl_t *s;
	int c, i, numItems;
	int flags = 0;
	char *status = NULL;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_common)) != -1)
	{
		switch (c)
		{
		case 's':
			status = cmd_optarg;
			break;
		case 'v':
			flags |= QMF_SHOW_VALUE;
			break;
		case 'd':
			flags |= QMF_DEFER_COMMIT;
			break;
		default:
			return;
		}
	}

	numItems = Cmd_Argc () - (cmd_optind + 2);
	if (numItems < 2 || (numItems & 1))
	{
		Com_Printf ("Usage: %s <name> <cvar> <desc1> <value1> [...]\n", Cmd_Argv (0));
		return;
	}

	s = UI_Mallocz (sizeof (*s));
	s->generic.type = MTYPE_PAIRS;
	s->generic.name = UI_CopyString (Cmd_Argv (cmd_optind));
	s->generic.status = UI_CopyString (status);
	s->generic.flags = flags;
	s->cvar = Cvar_WeakGet (Cmd_Argv (cmd_optind + 1));
	numItems /= 2;
	s->itemnames = UI_Mallocz (sizeof (char *) * (numItems + 1));
	s->itemvalues = UI_Mallocz (sizeof (char *) * (numItems + 1));
	for (i = 0; i < numItems; i++)
	{
		s->itemnames[i] = UI_CopyString (Cmd_Argv (cmd_optind + 2 + i * 2));
		s->itemvalues[i] = UI_CopyString (Cmd_Argv (cmd_optind + 3 + i * 2));
	}
	s->numItems = numItems;

	SetShowIf (&s->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, s);
}

static void Parse_Range (menuFrameWork_t *menu)
{
	menuSlider_t *s;
	char *status = NULL;
	int flags = 0;
	int c;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_common)) != -1)
	{
		switch (c)
		{
		case 's':
			status = cmd_optarg;
			break;
		case 'v':
			flags |= QMF_SHOW_VALUE;
			break;
		case 'd':
			flags |= QMF_DEFER_COMMIT;
			break;
		default:
			return;
		}
	}

	if (Cmd_Argc () - cmd_optind < 4)
	{
		Com_Printf ("Usage: %s <name> <cvar> <min> <max> [step]\n", Cmd_Argv (0));
		return;
	}

	s = UI_Mallocz (sizeof (*s));
	s->generic.type = MTYPE_SLIDER;
	s->generic.name = UI_CopyString (Cmd_Argv (cmd_optind));
	s->generic.status = UI_CopyString (status);
	s->generic.flags = flags;
	s->cvar = Cvar_WeakGet (Cmd_Argv (cmd_optind + 1));
	s->minvalue = Q_atof (Cmd_Argv (cmd_optind + 2));
	s->maxvalue = Q_atof (Cmd_Argv (cmd_optind + 3));
	if (Cmd_Argc () - cmd_optind > 4)
	{
		s->step = Q_atof (Cmd_Argv (cmd_optind + 4));
	}
	else
	{
		s->step = (s->maxvalue - s->minvalue) / SLIDER_RANGE;
	}

	SetShowIf (&s->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, s);
}

static void Parse_Action (menuFrameWork_t *menu)
{
	static const cmd_option_t o_action[] = {
		{ "a", "align" },
		{ "s:", "status" },
		{ NULL }
	};
	menuAction_t *a;
	int uiFlags = UI_CENTER;
	char *status = NULL;
	int c;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_action)) != -1)
	{
		switch (c)
		{
		case 'a':
			uiFlags = UI_LEFT | UI_ALTCOLOR;
			break;
		case 's':
			status = cmd_optarg;
			break;
		default:
			return;
		}
	}

	if (Cmd_Argc () - cmd_optind < 2)
	{
		Com_Printf ("Usage: %s <name> <command>\n", Cmd_Argv (0));
		return;
	}

	a = UI_Mallocz (sizeof (*a));
	a->generic.type = MTYPE_ACTION;
	a->generic.name = UI_CopyString (Cmd_Argv (cmd_optind));
	a->generic.activate = Activate;
	a->generic.uiFlags = uiFlags;
	a->generic.status = UI_CopyString (status);
	a->cmd = UI_CopyString (Cmd_ArgsFrom (cmd_optind + 1));

	SetShowIf (&a->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, a);
}

static void Parse_ActionSelect (menuFrameWork_t *menu)
{
	static const cmd_option_t o_action[] = {
		{ "a", "align" },
		{ "v", "show-value" },
		{ "s:", "status" },
		{ NULL }
	};
	menuSpinControl_t *s;
	int uiFlags = UI_CENTER;
	char state_name[MAX_QPATH];
	int flags = 0;
	char *status = NULL;
	char *name, *command_name;
	int c, i, numItems;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_action)) != -1)
	{
		switch (c)
		{
		case 'a':
			uiFlags = UI_LEFT | UI_ALTCOLOR;
			break;
		case 'v':
			flags |= QMF_SHOW_VALUE;
			break;
		case 's':
			status = cmd_optarg;
			break;
		default:
			return;
		}
	}

	numItems = Cmd_Argc () - (cmd_optind + 2);
	if (numItems < 1)
	{
		Com_Printf ("Usage: %s <name> <command_name> <command_value> [...]\n", Cmd_Argv (0));
		return;
	}

	name = Cmd_Argv (cmd_optind);
	command_name = Cmd_Argv (cmd_optind + 1);

	if (Q_snprintf (state_name, sizeof (state_name), "ui_action_select_%s",
			command_name) >= sizeof (state_name))
	{
		Com_Printf ("action_select command name is too long: %s\n", command_name);
		return;
	}

	s = UI_Mallocz (sizeof (*s));
	s->generic.type = MTYPE_ACTION_SELECT;
	s->generic.name = UI_CopyString (name);
	s->generic.status = UI_CopyString (status);
	s->generic.flags = flags;
	s->generic.uiFlags = uiFlags;
	s->cmd = UI_CopyString (command_name);
	s->cvar = Cvar_Get (state_name, Cmd_Argv (cmd_optind + 2),
		CVAR_PRIVATE | CVAR_NOARCHIVE);

	cmd_optind += 2;
	if (strchr (Cmd_ArgsFrom (cmd_optind), '$'))
	{
		long_args_hack (s, numItems);
	}
	else
	{
		s->itemnames = UI_Mallocz (sizeof (char *) * (numItems + 1));
		for (i = 0; i < numItems; i++)
		{
			s->itemnames[i] = UI_CopyString (Cmd_Argv (cmd_optind + i));
		}
		s->numItems = numItems;
	}

	SetShowIf (&s->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, s);
}

static void Parse_Bitmap (menuFrameWork_t *menu)
{
	static const cmd_option_t o_bitmap[] = {
		{ "s:", "status" },
		{ "N:", "altname" },
		{ NULL }
	};
	menuBitmap_t *b;
	char *status = NULL, *altname = NULL;
	int c;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_bitmap)) != -1)
	{
		switch (c)
		{
		case 's':
			status = cmd_optarg;
			break;
		case 'N':
			altname = cmd_optarg;
			break;
		default:
			return;
		}
	}

	if (Cmd_Argc () - cmd_optind < 2)
	{
		Com_Printf ("Usage: %s <name> <command>\n", Cmd_Argv (0));
		return;
	}

	b = UI_Mallocz (sizeof (*b));
	b->generic.type = MTYPE_BITMAP;
	b->generic.activate = Activate;
	b->generic.status = UI_CopyString (status);
	b->cmd = UI_CopyString (Cmd_ArgsFrom (cmd_optind + 1));
	b->pics[0] = R_RegisterPic (Cmd_Argv (cmd_optind));
	if (!altname)
		altname = va ("%s_sel", Cmd_Argv (cmd_optind));
	b->pics[1] = R_RegisterPic (altname);
	R_GetPicSize (&b->generic.width, &b->generic.height, b->pics[0]);

	SetShowIf (&b->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, b);
}

static void Parse_Bind (menuFrameWork_t *menu)
{
	static const cmd_option_t o_bind[] = {
		{ "a", "align" },
		{ "s:", "status" },
		{ "S:", "altstatus" },
		{ NULL }
	};
	menuKeybind_t *k;
	const char *status = "Press Enter to change, Backspace to clear";
	const char *altstatus = "Press the desired key, Escape to cancel";
	int c;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_bind)) != -1)
	{
		switch (c)
		{
		case 'a':
			break;
		case 's':
			status = cmd_optarg;
			break;
		case 'S':
			altstatus = cmd_optarg;
			break;
		default:
			return;
		}
	}

	if (Cmd_Argc () - cmd_optind < 2)
	{
		Com_Printf ("Usage: %s <name> <command>\n", Cmd_Argv (0));
		return;
	}

	k = UI_Mallocz (sizeof (*k));
	k->generic.type = MTYPE_KEYBIND;
	k->generic.name = UI_CopyString (Cmd_Argv (cmd_optind));
	k->generic.uiFlags = UI_CENTER;
	k->generic.status = UI_CopyString (status);
	k->cmd = UI_CopyString (Cmd_ArgsFrom (cmd_optind + 1));
	k->altstatus = UI_CopyString (altstatus);

	SetShowIf (&k->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, k);
}

static void Parse_Select (menuFrameWork_t *menu)
{
	static const cmd_option_t o_select[] = {
		{ "s:", "status" },
		{ "d", "defer" },
		{ "v", "show-value" },
		{ NULL }
	};
	menuSelect_t *s;
	const char *status = NULL;
	int c, numItems, i;
	bool deferCommit = false;
	bool showValue = false;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_select)) != -1)
	{
		switch (c)
		{
		case 's':
			status = cmd_optarg;
			break;
		case 'd':
			deferCommit = true;
			break;
		case 'v':
			showValue = true;
			break;
		default:
			return;
		}
	}

	numItems = Cmd_Argc () - cmd_optind - 2;
	if (numItems < 1)
	{
		Com_Printf ("Usage: %s <name> <cvar> <value1> [value2 ...]\n", Cmd_Argv (0));
		return;
	}

	s = UI_Mallocz (sizeof (*s));
	s->generic.type = MTYPE_SELECT;
	s->generic.name = UI_CopyString (Cmd_Argv (cmd_optind));
	s->generic.uiFlags = UI_CENTER;
	if (showValue)
		s->generic.flags |= QMF_SHOW_VALUE;
	if (deferCommit)
		s->generic.flags |= QMF_DEFER_COMMIT;
	s->cvar = Cvar_WeakGet (Cmd_Argv (cmd_optind + 1));
	s->numItems = numItems;
	s->curvalue = -1;
	s->hoverIndex = -1;
	s->itemnames = Z_Malloc (numItems * sizeof (*s->itemnames));
	s->itemRects = Z_Malloc (numItems * sizeof (*s->itemRects));

	if (status)
	{
		s->generic.status = UI_CopyString (status);
	}

	for (i = 0; i < numItems; i++)
	{
		s->itemnames[i] = UI_CopyString (Cmd_Argv (cmd_optind + 2 + i));
	}

	SetShowIf (&s->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, s);
}

static void Parse_Select2 (menuFrameWork_t *menu)
{
	static const cmd_option_t o_sel2[] = {
		{ "s:", "status" },
		{ NULL }
	};
	menuSelect2_t *s;
	const char *status = NULL;
	int c, i, split, n1, n2;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_sel2)) != -1)
	{
		switch (c)
		{
		case 's':
			status = cmd_optarg;
			break;
		default:
			return;
		}
	}

	/* Syntax: select2 <name> <cvar1> <v1> <v2> ... : <cvar2> <v1> <v2> ... */
	split = -1;
	for (i = cmd_optind + 2; i < Cmd_Argc (); i++)
	{
		if (!strcmp (Cmd_Argv (i), ":"))
		{
			split = i;
			break;
		}
	}
	if (split < 0)
	{
		Com_Printf ("Usage: %s <name> <cvar1> <v1> ... : <cvar2> <v1> ...\n", Cmd_Argv (0));
		return;
	}

	n1 = split - cmd_optind - 2;
	n2 = Cmd_Argc () - split - 2;
	if (n1 < 1 || n2 < 1)
	{
		Com_Printf ("Usage: %s <name> <cvar1> <v1> ... : <cvar2> <v1> ...\n", Cmd_Argv (0));
		return;
	}

	s = UI_Mallocz (sizeof (*s));
	s->generic.type = MTYPE_SELECT2;
	s->generic.name = UI_CopyString (Cmd_Argv (cmd_optind));
	s->generic.uiFlags = UI_CENTER;
	s->openIdx = -1;
	s->hoverIndex = -1;

	s->cvar[0] = Cvar_WeakGet (Cmd_Argv (cmd_optind + 1));
	s->numItems[0] = n1;
	s->itemnames[0] = Z_Malloc (n1 * sizeof (char *));
	for (i = 0; i < n1; i++)
		s->itemnames[0][i] = UI_CopyString (Cmd_Argv (cmd_optind + 2 + i));

	s->cvar[1] = Cvar_WeakGet (Cmd_Argv (split + 1));
	s->numItems[1] = n2;
	s->itemnames[1] = Z_Malloc (n2 * sizeof (char *));
	for (i = 0; i < n2; i++)
		s->itemnames[1][i] = UI_CopyString (Cmd_Argv (split + 2 + i));

	if (status)
		s->generic.status = UI_CopyString (status);

	SetShowIf (&s->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, s);
}

static void Parse_BindSelect2 (menuFrameWork_t *menu)
{
	static const cmd_option_t o_bs2[] = { { "s:", "status" }, { "S:", "altstatus" }, { NULL } };
	menuBindSelect2_t *bs;
	const char *status = "Enter to bind  |  Click selects to open  |  Tab to switch  |  L/R to adjust";
	const char *altstatus = "Press the desired key, Escape to cancel";
	int c, i, split, n1, n2;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_bs2)) != -1)
	{
		switch (c)
		{
		case 's':
			status = cmd_optarg;
			break;
		case 'S':
			altstatus = cmd_optarg;
			break;
		default:
			return;
		}
	}
	split = -1;
	for (i = cmd_optind + 3; i < Cmd_Argc (); i++)
	{
		if (!strcmp (Cmd_Argv (i), ":"))
		{
			split = i;
			break;
		}
	}
	if (split < 0)
	{
		Com_Printf ("Usage: %s <name> <cmd> <cvar1> <v1> ... : <cvar2> <v1> ...\n", Cmd_Argv (0));
		return;
	}
	n1 = split - cmd_optind - 3;
	n2 = Cmd_Argc () - split - 2;
	if (n1 < 1 || n2 < 1)
	{
		Com_Printf ("Usage: %s <name> <cmd> <cvar1> <v1> ... : <cvar2> <v1> ...\n", Cmd_Argv (0));
		return;
	}
	bs = UI_Mallocz (sizeof (*bs));
	bs->generic.type = MTYPE_BINDSELECT2;
	bs->generic.name = UI_CopyString (Cmd_Argv (cmd_optind));
	bs->generic.uiFlags = UI_CENTER;
	bs->generic.status = UI_CopyString (status);
	bs->cmd = UI_CopyString (Cmd_Argv (cmd_optind + 1));
	bs->legacycmd = UI_CopyString (Cmd_ArgsFrom (cmd_optind + 1));
	bs->altstatus = UI_CopyString (altstatus);
	bs->openIdx = -1;
	bs->hoverIndex = -1;
	bs->cvar[0] = Cvar_WeakGet (Cmd_Argv (cmd_optind + 2));
	bs->numItems[0] = n1;
	bs->itemnames[0] = Z_Malloc (n1 * sizeof (char *));
	for (i = 0; i < n1; i++)
		bs->itemnames[0][i] = UI_CopyString (Cmd_Argv (cmd_optind + 3 + i));
	bs->cvar[1] = Cvar_WeakGet (Cmd_Argv (split + 1));
	bs->numItems[1] = n2;
	bs->itemnames[1] = Z_Malloc (n2 * sizeof (char *));
	for (i = 0; i < n2; i++)
		bs->itemnames[1][i] = UI_CopyString (Cmd_Argv (split + 2 + i));
	bs->itemRects = Z_Malloc ((n1 > n2 ? n1 : n2) * sizeof (vrect_t));
	SetShowIf (&bs->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, bs);
}

static void Parse_Savegame (menuFrameWork_t *menu, menuType_t type)
{
	menuAction_t *a;
	char *status = NULL;
	int c;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_common)) != -1)
	{
		switch (c)
		{
		case 's':
			status = cmd_optarg;
			break;
		default:
			return;
		}
	}

	if (Cmd_Argc () - cmd_optind < 1)
	{
		Com_Printf ("Usage: %s <dir>\n", Cmd_Argv (0));
		return;
	}

	a = UI_Mallocz (sizeof (*a));
	a->generic.type = type;
	a->generic.name = UI_CopyString ("<EMPTY>");
	a->generic.activate = Activate;
	a->generic.uiFlags = UI_CENTER;
	a->generic.status = UI_CopyString (status);
	a->cmd = UI_CopyString (Cmd_Argv (cmd_optind));

	if (type == MTYPE_LOADGAME)
		a->generic.flags |= QMF_GRAYED;

	SetShowIf (&a->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, a);
}

static void Parse_Toggle (menuFrameWork_t *menu)
{
	static const char *const yes_no_names[] = { "no", "yes", NULL };
	menuSpinControl_t *s;
	bool negate = false;
	menuType_t type = MTYPE_TOGGLE;
	int c, bit = 0, flags = 0;
	char *b, *status = NULL;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_common)) != -1)
	{
		switch (c)
		{
		case 's':
			status = cmd_optarg;
			break;
		case 'v':
			flags |= QMF_SHOW_VALUE;
			break;
		case 'd':
			flags |= QMF_DEFER_COMMIT;
			break;
		default:
			return;
		}
	}

	if (Cmd_Argc () - cmd_optind < 2)
	{
		Com_Printf ("Usage: %s <name> <cvar> [~][bit]\n", Cmd_Argv (0));
		return;
	}

	b = Cmd_Argv (cmd_optind + 2);
	if (*b == '~')
	{
		negate = true;
		b++;
	}
	if (*b)
	{
		bit = Q_atoi (b);
		if (bit < 0 || bit >= 32)
		{
			Com_Printf ("Invalid bit number: %d\n", bit);
			return;
		}
		type = MTYPE_BITFIELD;
	}

	s = UI_Mallocz (sizeof (*s));
	s->generic.type = type;
	s->generic.name = UI_CopyString (Cmd_Argv (cmd_optind));
	s->generic.status = UI_CopyString (status);
	s->generic.flags = flags;
	s->cvar = Cvar_WeakGet (Cmd_Argv (cmd_optind + 1));
	s->itemnames = (char **)yes_no_names;
	s->numItems = 2;
	s->negate = negate;
	s->mask = 1U << bit;

	SetShowIf (&s->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, s);
}

static void Parse_Field (menuFrameWork_t *menu)
{
	static const cmd_option_t o_field[] = {
		{ "c", "center" },
		{ "p", "color" },
		{ "P", "picker" },
		{ "i", "integer" },
		{ "n", "numeric" },
		{ "s:", "status" },
		{ "w:", "width" },
		{ "l:", "length" },
		{ NULL }
	};
	menuField_t *f;
	bool center = false;
	bool colorPreview = false;
	bool colorPickerOnly = false;
	int flags = 0;
	char *status = NULL;
	int width = 16, length = 0;
	int c;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_field)) != -1)
	{
		switch (c)
		{
		case 'c':
			center = true;
			break;
		case 'p':
			colorPreview = true;
			break;
		case 'P':
			colorPreview = true;
			colorPickerOnly = true;
			break;
		case 'i':
		case 'n':
			flags |= QMF_NUMBERSONLY;
			break;
		case 's':
			status = cmd_optarg;
			break;
		case 'w':
			width = Q_atoi (cmd_optarg);
			if (width < 1 || width > 32)
			{
				Com_Printf ("Invalid width\n");
				return;
			}
			break;
		case 'l':
		{
			unsigned long parsed_length = strtoul (cmd_optarg, NULL, 10);
			if (!COM_IsUint (cmd_optarg) || parsed_length < 1 || parsed_length >= MAX_FIELD_TEXT)
			{
				Com_Printf ("Invalid length\n");
				return;
			}
			length = (int)parsed_length;
			break;
		}
		default:
			return;
		}
	}

	f = UI_Mallocz (sizeof (*f));
	f->generic.type = MTYPE_FIELD;
	f->generic.name = center ? NULL : UI_CopyString (Cmd_Argv (cmd_optind));
	f->generic.status = UI_CopyString (status);
	f->generic.flags = flags;
	f->cvar = Cvar_WeakGet (Cmd_Argv (center ? cmd_optind : cmd_optind + 1));
	f->width = width;
	f->length = length;
	f->colorPreview = colorPreview;
	f->colorPickerOnly = colorPickerOnly;

	SetShowIf (&f->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, f);
}

static void Parse_Blank (menuFrameWork_t *menu)
{
	menuSeparator_t *s;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	s = UI_Mallocz (sizeof (*s));
	s->generic.type = MTYPE_SEPARATOR;

	SetShowIf (&s->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, s);
}

static void Parse_Static (menuFrameWork_t *menu)
{
	static const cmd_option_t o_static[] = {
		{ "a", "align" },
		{ "s:", "status" },
		{ NULL }
	};
	menuStatic_t *s;
	int uiFlags = UI_CENTER | UI_ALTCOLOR;
	char *status = NULL;
	int c;
	char *show_if_cvar = NULL;
	char *show_if_value = NULL;

	ParseShowIf (&show_if_cvar, &show_if_value);

	while ((c = Cmd_ParseOptions (o_static)) != -1)
	{
		switch (c)
		{
		case 'a':
			uiFlags = UI_LEFT | UI_ALTCOLOR;
			break;
		case 's':
			status = cmd_optarg;
			break;
		default:
			return;
		}
	}

	if (Cmd_Argc () - cmd_optind < 1)
	{
		Com_Printf ("Usage: %s <name>\n", Cmd_Argv (0));
		return;
	}

	s = UI_Mallocz (sizeof (*s));
	s->generic.type = MTYPE_STATIC;
	s->generic.name = UI_CopyString (Cmd_Argv (cmd_optind));
	s->generic.uiFlags = uiFlags;
	s->generic.status = UI_CopyString (status);

	SetShowIf (&s->generic, show_if_cvar, show_if_value);
	Menu_AddItem (menu, s);
}

static void Parse_Background (menuFrameWork_t *menu)
{
	char *s = Cmd_Argv (1);

	if (SCR_ParseColor (s, &menu->color))
	{
		menu->image = 0;
		menu->transparent = menu->color.u8[3] != 255;
	}
	else
	{
		menu->image = R_RegisterPic (s);
		menu->transparent = R_GetPicSize (NULL, NULL, menu->image);
	}
}

static const cmd_option_t o_style[] = {
	{ "c", "compact" },
	{ "x", "left" },
	{ "m", "center" },
	{ "r", "right" },
	{ "C", "no-compact" },
	{ "l", "live" },
	{ "L", "no-live" },
	{ "t", "transparent" },
	{ "T", "no-transparent" },
	{ NULL }
};

typedef struct
{
	bool compactSet;
	bool compact;
	bool halignSet;
	menuHAlign_t halign;
	bool liveSet;
	bool live;
	bool transparentSet;
	bool transparent;
	bool focusSet;
	bool focusMenuWidth;
	int focusBorderWidth;
	int focusInset;
	int focusHeight;
} menuDefaultStyle_t;

static menuDefaultStyle_t ui_menu_defaults;

static void Apply_DefaultStyle (menuFrameWork_t *menu)
{
	if (ui_menu_defaults.compactSet)
	{
		menu->compact = ui_menu_defaults.compact;
	}
	if (ui_menu_defaults.halignSet)
	{
		menu->halign = ui_menu_defaults.halign;
	}
	if (ui_menu_defaults.liveSet)
	{
		menu->live = ui_menu_defaults.live;
	}
	if (ui_menu_defaults.transparentSet)
	{
		menu->transparent = ui_menu_defaults.transparent;
	}
	if (ui_menu_defaults.focusSet)
	{
		menu->focusStyleSet = true;
		menu->focusMenuWidth = ui_menu_defaults.focusMenuWidth;
		menu->focusBorderWidth = ui_menu_defaults.focusBorderWidth;
		menu->focusInset = ui_menu_defaults.focusInset;
		menu->focusHeight = ui_menu_defaults.focusHeight;
	}
}

static void Parse_GlobalStyle (void)
{
	int c;

	while ((c = Cmd_ParseOptions (o_style)) != -1)
	{
		switch (c)
		{
		case 'c':
			ui_menu_defaults.compactSet = true;
			ui_menu_defaults.compact = true;
			break;
		case 'x':
			ui_menu_defaults.halignSet = true;
			ui_menu_defaults.halign = MENU_HALIGN_LEFT;
			break;
		case 'm':
			ui_menu_defaults.halignSet = true;
			ui_menu_defaults.halign = MENU_HALIGN_CENTER;
			break;
		case 'r':
			ui_menu_defaults.halignSet = true;
			ui_menu_defaults.halign = MENU_HALIGN_RIGHT;
			break;
		case 'C':
			ui_menu_defaults.compactSet = true;
			ui_menu_defaults.compact = false;
			break;
		case 'l':
			ui_menu_defaults.liveSet = true;
			ui_menu_defaults.live = true;
			break;
		case 'L':
			ui_menu_defaults.liveSet = true;
			ui_menu_defaults.live = false;
			break;
		case 't':
			ui_menu_defaults.transparentSet = true;
			ui_menu_defaults.transparent = true;
			break;
		case 'T':
			ui_menu_defaults.transparentSet = true;
			ui_menu_defaults.transparent = false;
			break;
		default:
			return;
		}
	}
}

static void Parse_Style (menuFrameWork_t *menu)
{
	int c;

	while ((c = Cmd_ParseOptions (o_style)) != -1)
	{
		switch (c)
		{
		case 'c':
			menu->compact = true;
			break;
		case 'x':
			menu->halign = MENU_HALIGN_LEFT;
			break;
		case 'm':
			menu->halign = MENU_HALIGN_CENTER;
			break;
		case 'r':
			menu->halign = MENU_HALIGN_RIGHT;
			break;
		case 'C':
			menu->compact = false;
			break;
		case 'l':
			menu->live = true;
			break;
		case 'L':
			menu->live = false;
			break;
		case 't':
			menu->transparent = true;
			break;
		case 'T':
			menu->transparent = false;
			break;
		default:
			return;
		}
	}
}

static void Parse_Focus (menuFrameWork_t *menu)
{
	int i;

	if (!menu->focusStyleSet)
	{
		menu->focusBorderWidth = 2;
		menu->focusInset = 0;
		menu->focusHeight = 0;
		menu->focusMenuWidth = false;
		menu->focusStyleSet = true;
	}

	for (i = 1; i < Cmd_Argc (); i++)
	{
		char *arg = Cmd_Argv (i);

		if (!strcmp (arg, "--fullwidth") || !strcmp (arg, "--full-width"))
		{
			menu->focusMenuWidth = false;
		}
		else if (!strcmp (arg, "--menuwidth") || !strcmp (arg, "--menu-width"))
		{
			menu->focusMenuWidth = true;
		}
		else if (!strcmp (arg, "--no-border"))
		{
			menu->focusBorderWidth = 0;
		}
		else if (!strcmp (arg, "--border"))
		{
			if (++i >= Cmd_Argc ())
			{
				Com_Printf ("Missing value for --border option.\n");
				return;
			}
			menu->focusBorderWidth = Q_clip (Q_atoi (Cmd_Argv (i)), 0, 64);
		}
		else if (!strcmp (arg, "--inset"))
		{
			if (++i >= Cmd_Argc ())
			{
				Com_Printf ("Missing value for --inset option.\n");
				return;
			}
			menu->focusInset = Q_clip (Q_atoi (Cmd_Argv (i)), 0, 256);
		}
		else if (!strcmp (arg, "--height"))
		{
			if (++i >= Cmd_Argc ())
			{
				Com_Printf ("Missing value for --height option.\n");
				return;
			}
			menu->focusHeight = Q_clip (Q_atoi (Cmd_Argv (i)), 0, 256);
		}
		else
		{
			Com_Printf ("Unknown focus option '%s'\n", arg);
		}
	}
}

static void Parse_GlobalFocus (void)
{
	menuFrameWork_t defaults = { 0 };

	Parse_Focus (&defaults);
	if (defaults.focusStyleSet)
	{
		ui_menu_defaults.focusSet = true;
		ui_menu_defaults.focusMenuWidth = defaults.focusMenuWidth;
		ui_menu_defaults.focusBorderWidth = defaults.focusBorderWidth;
		ui_menu_defaults.focusInset = defaults.focusInset;
		ui_menu_defaults.focusHeight = defaults.focusHeight;
	}
}

static void Parse_Color (void)
{
	char *s, *c;

	if (Cmd_Argc () < 3)
	{
		Com_Printf ("Usage: %s <state> <color>\n", Cmd_Argv (0));
		return;
	}

	s = Cmd_Argv (1);
	c = Cmd_Argv (2);

	if (!strcmp (s, "normal"))
	{
		if (SCR_ParseColor (c, &uis.color.normal))
		{
			uis.color.selectable = uis.color.normal;
		}
	}
	else if (!strcmp (s, "selectable") || !strcmp (s, "action"))
	{
		SCR_ParseColor (c, &uis.color.selectable);
	}
	else if (!strcmp (s, "alternate") || !strcmp (s, "alt"))
	{
		SCR_ParseColor (c, &uis.color.alternate);
	}
	else if (!strcmp (s, "title"))
	{
		SCR_ParseColor (c, &uis.color.title);
	}
	else if (!strcmp (s, "active"))
	{
		SCR_ParseColor (c, &uis.color.active);
	}
	else if (!strcmp (s, "selection"))
	{
		SCR_ParseColor (c, &uis.color.selection);
	}
	else if (!strcmp (s, "focus"))
	{
		SCR_ParseColor (c, &uis.color.focus);
	}
	else if (!strcmp (s, "focus_border") || !strcmp (s, "focus-border"))
	{
		SCR_ParseColor (c, &uis.color.focus_border);
	}
	else if (!strcmp (s, "disabled"))
	{
		SCR_ParseColor (c, &uis.color.disabled);
	}
	else
	{
		Com_Printf ("Unknown state '%s'\n", s);
	}
}

static qboolean Parse_PlaquePositionArg (const char *s, plaquePosition_t *out)
{
	if (!s || !s[0])
	{
		return qfalse;
	}

	if (!Q_stricmp (s, "left"))
	{
		*out = PLAQUE_LEFT;
		return qtrue;
	}

	if (!Q_stricmp (s, "right"))
	{
		*out = PLAQUE_RIGHT;
		return qtrue;
	}

	if (!Q_stricmp (s, "top"))
	{
		*out = PLAQUE_TOP;
		return qtrue;
	}

	if (!Q_stricmp (s, "bottom"))
	{
		*out = PLAQUE_BOTTOM;
		return qtrue;
	}

	if (!Q_stricmp (s, "center"))
	{
		*out = PLAQUE_CENTER;
		return qtrue;
	}

	if (!Q_stricmp (s, "screen-topleft"))
	{
		*out = PLAQUE_SCREEN_TOPLEFT;
		return qtrue;
	}

	if (!Q_stricmp (s, "screen-topright"))
	{
		*out = PLAQUE_SCREEN_TOPRIGHT;
		return qtrue;
	}

	if (!Q_stricmp (s, "screen-bottomleft"))
	{
		*out = PLAQUE_SCREEN_BOTTOMLEFT;
		return qtrue;
	}

	if (!Q_stricmp (s, "screen-bottomright"))
	{
		*out = PLAQUE_SCREEN_BOTTOMRIGHT;
		return qtrue;
	}

	return qfalse;
}

static void Parse_Plaque (menuFrameWork_t *menu)
{
	int i;

	if (Cmd_Argc () < 2)
	{
		Com_Printf ("Usage: %s <plaque> [logo] [position]\n", Cmd_Argv (0));
		return;
	}

	menu->plaquePosition = PLAQUE_LEFT;

	menu->plaque = R_RegisterPic (Cmd_Argv (1));
	if (menu->plaque)
	{
		R_GetPicSize (&menu->plaque_rc.width,
			&menu->plaque_rc.height, menu->plaque);
	}

	for (i = 2; i < Cmd_Argc (); i++)
	{
		const char *arg = Cmd_Argv (i);
		plaquePosition_t pos;

		if (Parse_PlaquePositionArg (arg, &pos))
		{
			menu->plaquePosition = pos;
			continue;
		}

		if (!menu->logo)
		{
			menu->logo = R_RegisterPic (arg);
			if (menu->logo)
			{
				R_GetPicSize (&menu->logo_rc.width,
					&menu->logo_rc.height, menu->logo);
			}
			continue;
		}

		Com_Printf ("Unknown plaque argument: %s\n", arg);
	}
}

static void Parse_Banner (menuFrameWork_t *menu)
{
	if (Cmd_Argc () < 2)
	{
		Com_Printf ("Usage: %s <banner>\n", Cmd_Argv (0));
		return;
	}

	menu->banner = R_RegisterPic (Cmd_Argv (1));
	if (menu->banner)
	{
		R_GetPicSize (&menu->banner_rc.width,
			&menu->banner_rc.height, menu->banner);
	}
}

static bool IsReservedMenuName (const char *name)
{
	return !strcmp (name, "demos") ||
		!strcmp (name, "servers") ||
		!strcmp (name, "players") || !strcmp (name, "video");
}

static bool Parse_File (const char *path, int depth);

static bool Parse_Buffer (const char *path, char *data, int depth)
{
	char *p, *cmd;
	int argc;
	menuFrameWork_t *menu = NULL;
	menuFrameWork_t *replace = NULL;
	bool skip_menu = false;
	int line = 0;

	COM_Compress (data);

	while (*data)
	{
		line++;
		p = strchr (data, '\n');
		if (p)
		{
			*p = 0;
		}

		Cmd_TokenizeString (data, true);

		argc = Cmd_Argc ();
		if (argc)
		{
			if (ui_builtin_menu_active && !CL_JumpUIBuiltinAllowed ())
			{
				data = p ? p + 1 : data + strlen (data);
				continue;
			}
			cmd = Cmd_Argv (0);
			if (skip_menu)
			{
				if (!strcmp (cmd, "end"))
				{
					skip_menu = false;
				}
			}
			else if (menu)
			{
				if (!strcmp (cmd, "end"))
				{
					if (menu->nitems)
					{
						if (replace)
						{
							List_Remove (&replace->entry);
							if (replace->free)
							{
								replace->free (replace);
							}
							replace = NULL;
						}
						List_Append (&ui_menus, &menu->entry);
					}
					else
					{
						Com_WPrintf ("%s:%d: Menu entry '%s' without items\n",
							path, line, menu->name);
						menu->free (menu);
					}
					menu = NULL;
					replace = NULL;
				}
				else if (!strcmp (cmd, "title"))
				{
					Z_Free (menu->title);
					menu->title = UI_CopyString (Cmd_Argv (1));
				}
				else if (!strcmp (cmd, "plaque"))
				{
					Parse_Plaque (menu);
				}
				else if (!strcmp (cmd, "banner"))
				{
					Parse_Banner (menu);
				}
				else if (!strcmp (cmd, "background"))
				{
					Parse_Background (menu);
				}
				else if (!strcmp (cmd, "style"))
				{
					Parse_Style (menu);
				}
				else if (!strcmp (cmd, "focus"))
				{
					Parse_Focus (menu);
				}
				else if (!strcmp (cmd, "values"))
				{
					Parse_Spin (menu, MTYPE_SPINCONTROL);
				}
				else if (!strcmp (cmd, "strings"))
				{
					Parse_Spin (menu, MTYPE_STRINGS);
				}
				else if (!strcmp (cmd, "pairs"))
				{
					Parse_Pairs (menu);
				}
				else if (!strcmp (cmd, "range"))
				{
					Parse_Range (menu);
				}
				else if (!strcmp (cmd, "action"))
				{
					Parse_Action (menu);
				}
				else if (!strcmp (cmd, "action_select"))
				{
					Parse_ActionSelect (menu);
				}
				else if (!strcmp (cmd, "bitmap"))
				{
					Parse_Bitmap (menu);
				}
				else if (!strcmp (cmd, "bind"))
				{
					Parse_Bind (menu);
				}
				else if (!strcmp (cmd, "select"))
				{
					Parse_Select (menu);
				}
				else if (!strcmp (cmd, "select2"))
				{
					Parse_Select2 (menu);
				}
				else if (!strcmp (cmd, "bindselect2"))
				{
					Parse_BindSelect2 (menu);
				}
				else if (!strcmp (cmd, "savegame"))
				{
					Parse_Savegame (menu, MTYPE_SAVEGAME);
				}
				else if (!strcmp (cmd, "loadgame"))
				{
					Parse_Savegame (menu, MTYPE_LOADGAME);
				}
				else if (!strcmp (cmd, "toggle"))
				{
					Parse_Toggle (menu);
				}
				else if (!strcmp (cmd, "field"))
				{
					Parse_Field (menu);
				}
				else if (!strcmp (cmd, "blank"))
				{
					Parse_Blank (menu);
				}
				else if (!strcmp (cmd, "static"))
				{
					Parse_Static (menu);
				}
				else
				{
					Com_WPrintf ("%s:%d: Unknown keyword '%s'\n",
						path, line, cmd);
				}
			}
			else
			{
				if (!strcmp (cmd, "begin"))
				{
					char *s = Cmd_Argv (1);
					if (!*s)
					{
						Com_WPrintf ("%s:%d: Expected menu name after '%s'\n",
							path, line, cmd);
						break;
					}
					if (IsReservedMenuName (s))
					{
						Com_WPrintf ("%s:%d: Built-in menu '%s' can not be overridden\n",
							path, line, s);
						skip_menu = true;
						data = p ? p + 1 : data + strlen (data);
						continue;
					}
					replace = UI_FindMenu (s);
					menu = UI_Mallocz (sizeof (*menu));
					menu->name = UI_CopyString (s);
					menu->push = Menu_Push;
					menu->pop = Menu_Pop;
					menu->free = Menu_Free;
					menu->image = uis.backgroundHandle;
					menu->color.u32 = uis.color.background.u32;
					menu->transparent = uis.transparent;
					Apply_DefaultStyle (menu);
				}
				else if (!strcmp (cmd, "include"))
				{
					char *s = Cmd_Argv (1);
					if (!*s)
					{
						Com_WPrintf ("%s:%d: Expected file name after '%s'\n",
							path, line, cmd);
						break;
					}
					if (depth == 16)
					{
						Com_WPrintf ("%s:%d: Includes too deeply nested\n",
							path, line);
					}
					else
					{
						Parse_File (s, depth + 1);
					}
				}
				else if (!strcmp (cmd, "color"))
				{
					Parse_Color ();
				}
				else if (!strcmp (cmd, "background"))
				{
					char *s = Cmd_Argv (1);

					if (SCR_ParseColor (s, &uis.color.background))
					{
						uis.backgroundHandle = 0;
						uis.transparent = uis.color.background.u8[3] != 255;
					}
					else
					{
						uis.backgroundHandle = R_RegisterPic (s);
						uis.transparent = R_GetPicSize (NULL, NULL, uis.backgroundHandle);
					}
				}
				else if (!strcmp (cmd, "font"))
				{
					uis.fontHandle = R_RegisterFont (Cmd_Argv (1));
				}
				else if (!strcmp (cmd, "cursor"))
				{
					const char *cursor_name = (cl_menu_cursor && cl_menu_cursor->string && *cl_menu_cursor->string) ? cl_menu_cursor->string : Cmd_Argv (1);
					UI_SetCursor (cursor_name);
				}
				else if (!strcmp (cmd, "weapon"))
				{
					Cmd_ArgvBuffer (1, uis.weaponModel, sizeof (uis.weaponModel));
				}
				else if (!strcmp (cmd, "style"))
				{
					Parse_GlobalStyle ();
				}
				else if (!strcmp (cmd, "focus"))
				{
					Parse_GlobalFocus ();
				}
				else
				{
					Com_WPrintf ("%s:%d: Unknown keyword '%s'\n",
						path, line, cmd);
					break;
				}
			}
		}

		if (!p)
		{
			break;
		}

		data = p + 1;
	}

	if (menu)
	{
		Com_WPrintf ("%s:%d: Menu entry '%s' without 'end' terminator\n",
			path, line, menu->name);
		menu->free (menu);
		replace = NULL;
	}
	if (skip_menu)
	{
		Com_WPrintf ("%s:%d: Skipped menu entry without 'end' terminator\n",
			path, line);
	}

	return true;
}

static bool Parse_File (const char *path, int depth)
{
	char *raw;
	int ret;

	ret = FS_LoadFile (path, (void **)&raw);
	if (!raw)
	{
		if (ret != Q_ERR (ENOENT) || depth)
		{
			Com_WPrintf ("Couldn't %s %s: %s\n", depth ? "include" : "load",
				path, Q_ErrorString (ret));
		}
		return false;
	}

	if (ret < 0 || ret > 1024 * 1024)
	{
		FS_FreeFile (raw);
		return false;
	}
	char *copy = UI_Malloc ((size_t)ret + 1);
	memcpy (copy, raw, ret);
	copy[ret] = 0;
	Parse_Buffer (path, copy, depth);
	Z_Free (copy);

	FS_FreeFile (raw);

	return true;
}

extern const char *ui_builtin_menu;

static void Parse_BuiltinMenu (void)
{
	char *data = UI_CopyString (ui_builtin_menu);
	menuFrameWork_t *menu;

	Parse_Buffer ("builtin", data, 0);
	Z_Free (data);

	LIST_FOR_EACH (menuFrameWork_t, menu, &ui_menus, entry)
	{
		if (!strcmp (menu->name, "video") ||
			!strcmp (menu->name, "video_graphics") ||
			!strcmp (menu->name, "video_advanced"))
			menu->videoApply = true;
	}
}

static bool UI_ShouldUseBuiltinMenu (cvar_t *ui_external_menu)
{
	if (ui_external_menu->intvalue)
	{
		return false;
	}

	return fs_game && !Q_stricmp (fs_game->string, "jump");
}

void UI_LoadScript (void)
{
	cvar_t *ui_external_menu = Cvar_Get ("ui_external_menu", "0", CVAR_ARCHIVE);

	memset (&ui_menu_defaults, 0, sizeof (ui_menu_defaults));

	ui_builtin_menu_active = UI_ShouldUseBuiltinMenu (ui_external_menu);

	if (ui_builtin_menu_active)
	{
		Parse_BuiltinMenu ();
	}
	else
	{
		Parse_File ("q2pro.menu", 0);
	}
	if (CL_IsJumpGame ())
	{
		uis.fontHandle = R_RegisterFont ("prochars");
		if (!uis.fontHandle)
			uis.fontHandle = R_RegisterFont ("conchars");
	}
}
