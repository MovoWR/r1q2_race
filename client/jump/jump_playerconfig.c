/*
Copyright (C) 2003-2008 Andrey Nazarov

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
/*
=============================================================================

PLAYER CONFIG MENU

=============================================================================
*/

#define ID_MODEL 103

typedef struct
{
	menuFrameWork_t menu;
	menuField_t name;
	menuSpinControl_t model;
	menuSpinControl_t skin;
	menuSpinControl_t hand;
	char *pmnames[MAX_PLAYERMODELS + 1];
} m_player_t;

static m_player_t m_player;

static const char *const handedness[] = {
	"right",
	"left",
	"center",
	NULL
};

static menuSound_t Change (menuCommon_t *self)
{
	m_player.skin.itemnames = uis.pmi[m_player.model.curvalue].skindisplaynames;
	m_player.skin.curvalue = 0;
	SpinControl_Init (&m_player.skin);
	Menu_Layout (&m_player.menu);
	return QMS_MOVE;
}

static void Pop (menuFrameWork_t *self)
{
	char scratch[MAX_QPATH];

	Cvar_SetEx ("name", m_player.name.field.text, FROM_CONSOLE);
	Q_concat (scratch, sizeof (scratch),
		uis.pmi[m_player.model.curvalue].directory, "/",
		uis.pmi[m_player.model.curvalue].skindisplaynames[m_player.skin.curvalue]);
	Cvar_SetEx ("skin", scratch, FROM_CONSOLE);
	Cvar_SetEx ("hand", va ("%d", m_player.hand.curvalue), FROM_CONSOLE);
}

static bool Push (menuFrameWork_t *self)
{
	char currentdirectory[MAX_QPATH];
	char currentskin[MAX_QPATH];
	int i, j;
	int currentdirectoryindex = 0;
	int currentskinindex = 0;
	char *p;

	// find the models and skins available for the in-game appearance
	if (!uis.numPlayerModels)
	{
		PlayerModel_Load ();
		if (!uis.numPlayerModels)
		{
			Com_Printf ("No player models found.\n");
			return false;
		}
	}

	Cvar_VariableStringBuffer ("skin", currentdirectory, sizeof (currentdirectory));

	if ((p = strchr (currentdirectory, '/')) || (p = strchr (currentdirectory, '\\')))
	{
		*p++ = 0;
		Q_strlcpy (currentskin, p, sizeof (currentskin));
	}
	else
	{
		strcpy (currentdirectory, "male");
		strcpy (currentskin, "grunt");
	}

	for (i = 0; i < uis.numPlayerModels; i++)
	{
		m_player.pmnames[i] = uis.pmi[i].directory;
		if (Q_stricmp (uis.pmi[i].directory, currentdirectory) == 0)
		{
			currentdirectoryindex = i;

			for (j = 0; j < uis.pmi[i].nskins; j++)
			{
				if (Q_stricmp (uis.pmi[i].skindisplaynames[j], currentskin) == 0)
				{
					currentskinindex = j;
					break;
				}
			}
		}
	}

	IF_Init (&m_player.name.field, m_player.name.width, m_player.name.width);
	IF_Replace (&m_player.name.field, Cvar_VariableString ("name"));

	m_player.model.curvalue = currentdirectoryindex;
	m_player.model.itemnames = m_player.pmnames;

	m_player.skin.curvalue = currentskinindex;
	m_player.skin.itemnames = uis.pmi[currentdirectoryindex].skindisplaynames;

	m_player.hand.curvalue = Cvar_VariableInteger ("hand");
	if (m_player.hand.curvalue < 0 || m_player.hand.curvalue > 2)
		m_player.hand.curvalue = 0;

	return true;
}

static void Free (menuFrameWork_t *self)
{
	Z_Free (m_player.menu.items);
	memset (&m_player, 0, sizeof (m_player));
}

void JUI_Menu_PlayerConfig (void)
{
	m_player.menu.name = "players";
	m_player.menu.title = "Player Setup";
	m_player.menu.push = Push;
	m_player.menu.pop = Pop;
	m_player.menu.free = Free;
	m_player.menu.compact = true;
	m_player.menu.transparent = true;
	m_player.menu.color = uis.color.background;

	m_player.name.generic.type = MTYPE_FIELD;
	m_player.name.generic.flags = QMF_HASFOCUS;
	m_player.name.generic.name = "name";
	m_player.name.width = MAX_CLIENT_NAME - 1;

	m_player.model.generic.type = MTYPE_SPINCONTROL;
	m_player.model.generic.id = ID_MODEL;
	m_player.model.generic.name = "model";
	m_player.model.generic.change = Change;

	m_player.skin.generic.type = MTYPE_SPINCONTROL;
	m_player.skin.generic.name = "skin";

	m_player.hand.generic.type = MTYPE_SPINCONTROL;
	m_player.hand.generic.name = "handedness";
	m_player.hand.itemnames = (char **)handedness;

	Menu_AddItem (&m_player.menu, &m_player.name);
	Menu_AddItem (&m_player.menu, &m_player.model);
	Menu_AddItem (&m_player.menu, &m_player.skin);
	Menu_AddItem (&m_player.menu, &m_player.hand);
	List_Append (&ui_menus, &m_player.menu.entry);
}
