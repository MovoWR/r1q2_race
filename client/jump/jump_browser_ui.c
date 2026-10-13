/* Racing browsers use the full private menu widgets and native data services. */
#include "jump_menu.h"
#include "jump_servers.h"
#include "jump_demos.h"

typedef struct
{
	menuFrameWork_t menu;
	menuSpinControl_t tabs;
	menuList_t list;
	menuAction_t actions[10];
	unsigned selected, revision;
	unsigned ids[JUMP_SERVERS_MAX];
	char sources[2048], message[256];
} jump_servers_menu_t;

static jump_servers_menu_t server_menu;
static void JUI_ServerDetails (qboolean players);
static char *server_tabs[] = { "Jump", "All servers", "Favorites / LAN", NULL };

static void JUI_ListFree (menuList_t *list)
{
	int i;
	for (i = 0; i < list->numItems; i++)
		free (list->items[i]);
	free (list->items);
	list->items = NULL;
	list->numItems = 0;
}

static void JUI_ServerRows (void)
{
	jump_servers_snapshot_t snapshot;
	int i, count, index = 0;
	CL_JumpServersSnapshot (&snapshot);
	Com_sprintf (server_menu.message, sizeof (server_menu.message), "%d servers; %d valid; %d pending; %d fetching; %d errors%s", snapshot.total, snapshot.valid, snapshot.pending, snapshot.fetching, snapshot.errors, snapshot.truncated ? " (list capped)" : "");
	server_menu.menu.status = server_menu.message;
	if (server_menu.revision == snapshot.revision && server_menu.list.items)
		return;
	server_menu.revision = snapshot.revision;
	JUI_ListFree (&server_menu.list);
	count = CL_JumpServersCount ();
	server_menu.list.items = UI_Malloc ((size_t)max (1, count) * sizeof (void *));
	for (i = 0; i < count; i++)
	{
		const jump_server_row_t *row = CL_JumpServersRow (i);
		char players[32], ping[32];
		if (!row)
			break;
		Com_sprintf (players, sizeof (players), "%d/%d", row->players, row->max_players);
		if (row->state == JUMP_SERVER_VALID)
			Com_sprintf (ping, sizeof (ping), "%u", row->ping);
		else
			Q_strlcpy (ping, row->state == JUMP_SERVER_FAILED ? "failed" : "...", sizeof (ping));
		server_menu.ids[i] = row->id;
		server_menu.list.items[i] = UI_FormatColumns (0, (char *)(row->name[0] ? row->name : row->address), (char *)row->mod, (char *)row->map, players, ping, NULL);
		if (row->id == server_menu.selected)
			index = i;
	}
	server_menu.list.numItems = i;
	server_menu.list.curvalue = index;
	server_menu.list.prestep = CLAMP (server_menu.list.prestep, 0, max (0, i - server_menu.list.maxItems));
	if (!server_menu.selected && i)
		server_menu.selected = server_menu.ids[index];
}

static menuSound_t JUI_ServerChange (menuCommon_t *item)
{
	if (item == &server_menu.tabs.generic)
	{
		server_menu.selected = 0;
		server_menu.sources[0] = 0;
		CL_JumpServersOpen ((jump_servers_tab_t)server_menu.tabs.curvalue, NULL);
		server_menu.revision = 0;
		JUI_ServerRows ();
	}
	else if (server_menu.list.curvalue >= 0 && server_menu.list.curvalue < server_menu.list.numItems)
		server_menu.selected = server_menu.ids[server_menu.list.curvalue];
	return QMS_MOVE;
}

static menuSound_t JUI_ServerSort (menuList_t *list)
{
	CL_JumpServersSort (list->sortdir * (list->sortcol + 1));
	server_menu.revision = 0;
	JUI_ServerRows ();
	return QMS_MOVE;
}

static menuSound_t JUI_ServerAction (menuCommon_t *item)
{
	qboolean ok = true;
	char address[JUMP_SERVER_ADDRESS];
	JUI_ServerChange (&server_menu.list.generic);
	switch (item->id)
	{
	case 0:
		ok = CL_JumpServersConnect (server_menu.selected);
		if (ok)
			UI_ForceMenuOff ();
		break;
	case 1:
		CL_JumpServersRefresh ();
		break;
	case 2:
		ok = CL_JumpServersRefreshSelected (server_menu.selected);
		break;
	case 3:
		CL_JumpServersAbort ();
		break;
	case 4:
		ok = CL_JumpServersCopyAddress (server_menu.selected, address, sizeof (address));
		if (ok)
			CL_JumpFieldCopy (address);
		break;
	case 5:
		ok = CL_JumpServersSetRconAddress (server_menu.selected);
		break;
	case 6:
		ok = CL_JumpServersSaveFavorite (server_menu.selected);
		break;
	case 7:
		ok = CL_JumpServersRemoveFavorite (server_menu.selected);
		break;
	case 8:
		JUI_ServerDetails (false);
		break;
	case 9:
		JUI_ServerDetails (true);
		break;
	}
	server_menu.revision = 0;
	return ok ? QMS_IN : QMS_BEEP;
}

static bool JUI_ServerPush (menuFrameWork_t *menu)
{
	const char *args = CL_JumpUIArgsFrom (2);
	jump_servers_tab_t tab = JUMP_SERVERS_JUMP;
	if (strstr (args, "favorites://"))
		tab = JUMP_SERVERS_BOOK;
	else if (*args && !strstr (args, "@jump") && !strstr (args, "mod=jump"))
		tab = JUMP_SERVERS_ALL;
#ifndef _WIN32
	if (!*args)
		tab = JUMP_SERVERS_BOOK;
#endif
	Q_strlcpy (server_menu.sources, args, sizeof (server_menu.sources));
	server_menu.tabs.curvalue = tab;
	CL_JumpServersOpen (tab, *args ? server_menu.sources : NULL);
	server_menu.revision = 0;
	server_menu.selected = 0;
	JUI_ServerRows ();
	return true;
}

static void JUI_ServerPop (menuFrameWork_t *menu)
{
	CL_JumpServersClose ();
	JUI_ListFree (&server_menu.list);
}

static void JUI_ServerSize (menuFrameWork_t *menu)
{
	int i, width = min (uis.width - 32, 960), x = (uis.width - width) / 2;
	int columns = CLAMP (width / (18 * CHAR_WIDTH), 1, 4), rows = (10 + columns - 1) / columns;
	int actions_y = uis.height - 102 - rows * 18;
	int players_width = 8 * CHAR_WIDTH, ping_width = 7 * CHAR_WIDTH;
	int text_width = width - players_width - ping_width - MLIST_SCROLLBAR_WIDTH;
	int server_width = text_width * 55 / 100, mod_width = text_width * 20 / 100;
	server_menu.tabs.generic.x = x - LCOLUMN_OFFSET + (int)strlen (server_menu.tabs.generic.name) * CHAR_WIDTH;
	server_menu.tabs.generic.y = 40;
	server_menu.list.generic.x = x;
#ifdef _WIN32
	server_menu.list.generic.y = 64;
#else
	server_menu.list.generic.y = 40;
#endif
	server_menu.list.generic.height = max (30, actions_y - server_menu.list.generic.y - 10);
	server_menu.list.numcolumns = 5;
	server_menu.list.columns[0] = (menuListColumn_t){ "Server", server_width, UI_LEFT };
	server_menu.list.columns[1] = (menuListColumn_t){ "Mod", mod_width, UI_LEFT };
	server_menu.list.columns[2] = (menuListColumn_t){ "Map", text_width - server_width - mod_width, UI_LEFT };
	server_menu.list.columns[3] = (menuListColumn_t){ "Players", players_width, UI_RIGHT };
	server_menu.list.columns[4] = (menuListColumn_t){ "Ping", ping_width, UI_RIGHT };
	for (i = 0; i < 10; i++)
	{
		server_menu.actions[i].generic.x = x + ((i % columns) * 2 + 1) * width / (columns * 2);
		server_menu.actions[i].generic.y = actions_y + (i / columns) * 18;
	}
	menu->mins[0] = x;
	menu->mins[1] = 24;
	menu->maxs[0] = x + width;
	menu->maxs[1] = uis.height - 92;
}

static void JUI_ServerDraw (menuFrameWork_t *menu)
{
	const jump_server_row_t *row;
	const jump_server_rule_t *rules;
	const jump_server_player_t *players;
	int n, i, x, width, half;
	JUI_ServerRows ();
	Menu_Draw (menu);
	row = CL_JumpServersById (server_menu.selected);
	if (!row)
		return;
	x = server_menu.list.generic.x;
	width = server_menu.list.generic.rect.width;
	half = width / 2;
	R_SetColor (UI_MenuHintTextColor ());
	CL_JumpUIString (x, uis.height - 80, UI_LEFT, width / CHAR_WIDTH, row->address, uis.fontHandle);
	n = CL_JumpServersRules (row->id, &rules);
	for (i = 0; i < n && i < 2; i++)
		CL_JumpUIString (x, uis.height - 65 + i * 10, UI_LEFT, (half - CHAR_WIDTH) / CHAR_WIDTH, va ("%s = %s", rules[i].key, rules[i].value), uis.fontHandle);
	n = CL_JumpServersPlayers (row->id, &players);
	for (i = 0; i < n && i < 3; i++)
		CL_JumpUIString (x + half + CHAR_WIDTH, uis.height - 65 + i * 10, UI_LEFT, (width - half - CHAR_WIDTH) / CHAR_WIDTH, va ("%s  score %d  ping %d", players[i].name, players[i].score, players[i].ping), uis.fontHandle);
	R_ClearColor ();
}

static void JUI_ServerFree (menuFrameWork_t *menu)
{
	JUI_ServerPop (menu);
	free (menu->items);
	memset (&server_menu, 0, sizeof (server_menu));
}

void JUI_Menu_Servers (void)
{
	int i;
	static const char *actions[] = { "Connect", "Refresh all", "Refresh selected", "Stop", "Copy address", "Set RCON address", "Save favorite", "Remove favorite", "All rules", "All players" };
	server_menu.menu.name = "servers";
	server_menu.menu.title = "Server Browser";
	server_menu.menu.transparent = true;
	server_menu.menu.push = JUI_ServerPush;
	server_menu.menu.pop = JUI_ServerPop;
	server_menu.menu.size = JUI_ServerSize;
	server_menu.menu.draw = JUI_ServerDraw;
	server_menu.menu.free = JUI_ServerFree;
	server_menu.tabs.generic.type = MTYPE_SPINCONTROL;
	server_menu.tabs.generic.name = "Sources";
	server_menu.tabs.itemnames = server_tabs;
	server_menu.tabs.generic.change = JUI_ServerChange;
#ifdef _WIN32
	Menu_AddItem (&server_menu.menu, &server_menu.tabs);
#endif
	server_menu.list.generic.type = MTYPE_LIST;
	server_menu.list.generic.flags = QMF_HASFOCUS;
	server_menu.list.generic.change = JUI_ServerChange;
	server_menu.list.generic.activate = JUI_ServerAction;
	server_menu.list.mlFlags = MLF_HEADER | MLF_SCROLLBAR;
	server_menu.list.sort = JUI_ServerSort;
	Menu_AddItem (&server_menu.menu, &server_menu.list);
	for (i = 0; i < 10; i++)
	{
		server_menu.actions[i].generic.type = MTYPE_ACTION;
		server_menu.actions[i].generic.name = (char *)actions[i];
		server_menu.actions[i].generic.uiFlags = UI_CENTER;
		server_menu.actions[i].generic.id = i;
		server_menu.actions[i].generic.activate = JUI_ServerAction;
		Menu_AddItem (&server_menu.menu, server_menu.actions + i);
	}
	List_Append (&ui_menus, &server_menu.menu.entry);
}

typedef struct
{
	menuFrameWork_t menu;
	menuList_t list;
	menuAction_t refresh;
	char status[128];
} jump_demos_menu_t;

static jump_demos_menu_t demos_menu;

static void JUI_DemoRows (void)
{
	int i, count = CL_JumpDemosCount ();
	char size[32];
	JUI_ListFree (&demos_menu.list);
	demos_menu.list.items = UI_Malloc ((size_t)max (1, count) * sizeof (void *));
	for (i = 0; i < count; i++)
	{
		const jump_demo_row_t *row = CL_JumpDemosRow (i);
		if (row->directory)
			strcpy (size, "[DIR]");
		else
			Com_sprintf (size, sizeof (size), "%d KB", row->size > 0 ? row->size / 1024 : 0);
		demos_menu.list.items[i] = UI_FormatColumns (0, (char *)row->name, size, (char *)(row->directory ? "" : row->playable ? row->map :
																																"unsupported / invalid"),
			(char *)row->pov, NULL);
	}
	demos_menu.list.numItems = count;
	demos_menu.list.curvalue = 0;
	Com_sprintf (demos_menu.status, sizeof (demos_menu.status), "%s: %d entries. Native DM2 protocol 34/35 playback.", CL_JumpDemosDirectory (), count);
	demos_menu.menu.status = demos_menu.status;
}

static menuSound_t JUI_DemoAction (menuCommon_t *item)
{
	const jump_demo_row_t *row = CL_JumpDemosRow (demos_menu.list.curvalue);
	qboolean directory = row && row->directory;
	if (item->id == 1)
	{
		CL_JumpDemosOpen (CL_JumpDemosDirectory ());
		JUI_DemoRows ();
		Menu_Init (&demos_menu.menu);
		return QMS_IN;
	}
	if (!CL_JumpDemosSelect (demos_menu.list.curvalue))
		return QMS_BEEP;
	if (directory)
	{
		JUI_DemoRows ();
		Menu_Init (&demos_menu.menu);
	}
	else
		UI_ForceMenuOff ();
	return QMS_IN;
}

static menuSound_t JUI_DemoSort (menuList_t *list)
{
	CL_JumpDemosSort (list->sortdir * (min (2, list->sortcol) + 1));
	JUI_DemoRows ();
	return QMS_MOVE;
}

static bool JUI_DemoPush (menuFrameWork_t *menu)
{
	if (!CL_JumpDemosOpen ("demos"))
		return false;
	JUI_DemoRows ();
	return true;
}

static void JUI_DemoPop (menuFrameWork_t *menu)
{
	JUI_ListFree (&demos_menu.list);
	CL_JumpDemosClose ();
}

static void JUI_DemoSize (menuFrameWork_t *menu)
{
	int width = min (uis.width - 32, 800), x = (uis.width - width) / 2;
	demos_menu.list.generic.x = x;
	demos_menu.list.generic.y = 48;
	demos_menu.list.generic.height = max (60, uis.height - 112);
	demos_menu.list.numcolumns = 4;
	demos_menu.list.columns[0] = (menuListColumn_t){ "Name", width / 2, UI_LEFT };
	demos_menu.list.columns[1] = (menuListColumn_t){ "Size", width / 8, UI_RIGHT };
	demos_menu.list.columns[2] = (menuListColumn_t){ "Map", width / 4, UI_LEFT };
	demos_menu.list.columns[3] = (menuListColumn_t){ "POV", width / 8 - MLIST_SCROLLBAR_WIDTH, UI_LEFT };
	demos_menu.refresh.generic.x = x;
	demos_menu.refresh.generic.y = uis.height - 48;
	menu->mins[0] = x;
	menu->mins[1] = 24;
	menu->maxs[0] = x + width;
	menu->maxs[1] = uis.height - 24;
}

static void JUI_DemoFree (menuFrameWork_t *menu)
{
	JUI_DemoPop (menu);
	free (menu->items);
	memset (&demos_menu, 0, sizeof (demos_menu));
}

void JUI_Menu_Demos (void)
{
	demos_menu.menu.name = "demos";
	demos_menu.menu.title = "Native DM2 Browser";
	demos_menu.menu.transparent = true;
	demos_menu.menu.push = JUI_DemoPush;
	demos_menu.menu.pop = JUI_DemoPop;
	demos_menu.menu.size = JUI_DemoSize;
	demos_menu.menu.free = JUI_DemoFree;
	demos_menu.list.generic.type = MTYPE_LIST;
	demos_menu.list.generic.flags = QMF_HASFOCUS;
	demos_menu.list.generic.activate = JUI_DemoAction;
	demos_menu.list.mlFlags = MLF_HEADER | MLF_SCROLLBAR;
	demos_menu.list.sort = JUI_DemoSort;
	Menu_AddItem (&demos_menu.menu, &demos_menu.list);
	demos_menu.refresh.generic.type = MTYPE_ACTION;
	demos_menu.refresh.generic.name = "Refresh";
	demos_menu.refresh.generic.id = 1;
	demos_menu.refresh.generic.activate = JUI_DemoAction;
	Menu_AddItem (&demos_menu.menu, &demos_menu.refresh);
	List_Append (&ui_menus, &demos_menu.menu.entry);
}

/* Independent read-only rows allow all rules/players to be inspected and scrolled. */
typedef struct
{
	menuFrameWork_t menu;
	menuList_t list;
} jump_details_menu_t;

static jump_details_menu_t details_menu;

static void JUI_DetailsPop (menuFrameWork_t *menu)
{
	JUI_ListFree (&details_menu.list);
}

static void JUI_DetailsSize (menuFrameWork_t *menu)
{
	int width = min (uis.width - 32, 800), x = (uis.width - width) / 2;
	details_menu.list.generic.x = x;
	details_menu.list.generic.y = 48;
	details_menu.list.generic.height = max (60, uis.height - 96);
	details_menu.list.numcolumns = 2;
	details_menu.list.columns[0] = (menuListColumn_t){ "Name / rule", width / 2, UI_LEFT };
	details_menu.list.columns[1] = (menuListColumn_t){ "Value / score and ping", width / 2 - MLIST_SCROLLBAR_WIDTH, UI_LEFT };
	menu->mins[0] = x;
	menu->mins[1] = 24;
	menu->maxs[0] = x + width;
	menu->maxs[1] = uis.height - 24;
}

static void JUI_ServerDetails (qboolean players)
{
	const jump_server_rule_t *rules = NULL;
	const jump_server_player_t *rows = NULL;
	int i, count;
	char value[64];
	if (!details_menu.menu.name)
	{
		details_menu.menu.name = "server_details";
		details_menu.menu.transparent = true;
		details_menu.menu.pop = JUI_DetailsPop;
		details_menu.menu.size = JUI_DetailsSize;
		details_menu.list.generic.type = MTYPE_LIST;
		details_menu.list.generic.flags = QMF_HASFOCUS;
		details_menu.list.mlFlags = MLF_HEADER | MLF_SCROLLBAR;
		Menu_AddItem (&details_menu.menu, &details_menu.list);
	}
	JUI_ListFree (&details_menu.list);
	count = players ? CL_JumpServersPlayers (server_menu.selected, &rows) : CL_JumpServersRules (server_menu.selected, &rules);
	details_menu.list.items = UI_Malloc ((size_t)max (1, count) * sizeof (void *));
	for (i = 0; i < count; i++)
	{
		if (players)
		{
			Com_sprintf (value, sizeof (value), "score %d   ping %d", rows[i].score, rows[i].ping);
			details_menu.list.items[i] = UI_FormatColumns (0, (char *)rows[i].name, value, NULL);
		}
		else
			details_menu.list.items[i] = UI_FormatColumns (0, (char *)rules[i].key, (char *)rules[i].value, NULL);
	}
	details_menu.list.numItems = count;
	details_menu.list.curvalue = 0;
	details_menu.list.prestep = 0;
	details_menu.menu.title = players ? "Server Players" : "Server Rules";
	UI_PushMenu (&details_menu.menu);
}

void CL_JumpBrowserUIFree (void)
{
	JUI_DetailsPop (&details_menu.menu);
	free (details_menu.menu.items);
	memset (&details_menu, 0, sizeof (details_menu));
}
