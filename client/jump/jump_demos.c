/* Native DM2 browser with bounded metadata parsing and native playback. */
#include "jump_compat.h"
#include "jump_demos.h"
#include <ctype.h>
extern char **FS_JumpList (const char *, const char *, qboolean, int *);
extern int FS_JumpReadPrefix (const char *, void *, int, int *);
static jump_demo_row_t *jump_demo_rows;
static int jump_demo_count, jump_demo_sort;
static char jump_demo_directory[MAX_QPATH] = "demos";

static qboolean CL_JumpDemoName (const char *name, qboolean path)
{
	const unsigned char *p = (const unsigned char *)name;
	if (!name || !*p || *p == '/' || strlen (name) >= MAX_QPATH || strstr (name, "..") ||
		(path && (strstr (name, "//") || name[strlen (name) - 1] == '/')))
		return false;
	for (; *p; p++)
		if (!isalnum (*p) && *p != '_' && *p != '-' && *p != '.' && *p != ' ' && !(path && *p == '/'))
			return false;
	return true;
}

static uint32 CL_JumpDemoLong (const byte *p)
{
	return p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}

static const byte *CL_JumpDemoString (const byte *p, const byte *end, char *out, size_t size)
{
	size_t length;
	const byte *zero = memchr (p, 0, (size_t)(end - p));
	if (!zero)
		return NULL;
	length = (size_t)(zero - p);
	if (length >= size)
		length = size - 1;
	memcpy (out, p, length);
	out[length] = 0;
	return zero + 1;
}

static void CL_JumpDemoInfo (const char *name, jump_demo_row_t *row)
{
	byte data[MAX_MSGLEN + 4];
	const byte *p, *end;
	uint32 length;
	int count;
	count = FS_JumpReadPrefix (name, data, sizeof (data), &row->size);
	if (count < 4 || row->size < 4)
		return;
	length = CL_JumpDemoLong (data);
	if (length < 11 || length > MAX_MSGLEN || length > (uint32)(count - 4) || length > (uint32)(row->size - 4))
		return;
	p = data + 4;
	end = p + length;
	while (p < end && *p == svc_nop)
		p++;
	if (end - p < 11 || *p++ != svc_serverdata)
		return;
	row->protocol = (int)CL_JumpDemoLong (p);
	p += 9;
	if (row->protocol != PROTOCOL_ORIGINAL && row->protocol != PROTOCOL_R1Q2)
		return;
	{
		char game[MAX_QPATH];
		p = CL_JumpDemoString (p, end, game, sizeof (game));
	}
	if (!p || end - p < 2)
		return;
	{
		unsigned number = p[0] | ((unsigned)p[1] << 8);
		Com_sprintf (row->pov, sizeof (row->pov), number == 65535 ? "cinematic" : "player %u", number);
	}
	p += 2;
	p = CL_JumpDemoString (p, end, row->map, sizeof (row->map));
	if (!p)
		return;
	if (row->protocol == PROTOCOL_R1Q2)
	{
		unsigned minor;
		if (end - p < 3)
			return;
		minor = p[1] | ((unsigned)p[2] << 8);
		p += 3;
		if (minor >= 1903 && end - p < 2)
			return;
	}
	row->playable = true;
}

static void CL_JumpDemosListFree (char **list, int count)
{
	int i;
	for (i = 0; i < count; i++)
		free (list[i]);
	free (list);
}

void CL_JumpDemosClose (void)
{
	free (jump_demo_rows);
	jump_demo_rows = NULL;
	jump_demo_count = 0;
}

qboolean CL_JumpDemosOpen (const char *directory)
{
	char **list, path[MAX_QPATH];
	int count, i, kind;
	if (!CL_JumpEnabled () || !CL_JumpDemoName (directory, true) ||
		(strcmp (directory, "demos") && strncmp (directory, "demos/", 6)))
		return false;
	CL_JumpDemosClose ();
	if (directory != jump_demo_directory)
		Q_strlcpy (jump_demo_directory, directory, sizeof (jump_demo_directory));
	jump_demo_rows = calloc (JUMP_DEMOS_MAX, sizeof (*jump_demo_rows));
	if (!jump_demo_rows)
		return false;
	if (strcmp (directory, "demos"))
	{
		strcpy (jump_demo_rows[0].name, "..");
		jump_demo_rows[0].directory = true;
		jump_demo_count = 1;
	}
	for (kind = 0; kind < 2; kind++)
	{
		list = FS_JumpList (directory, kind ? ".dm2" : NULL, !kind, &count);
		for (i = 0; i < count && jump_demo_count < JUMP_DEMOS_MAX; i++)
		{
			jump_demo_row_t *row;
			if (!CL_JumpDemoName (list[i], false) || strlen (directory) + strlen (list[i]) + 2 > sizeof (path))
				continue;
			if (kind && (strlen (list[i]) < 4 || strcmp (list[i] + strlen (list[i]) - 4, ".dm2")))
				continue;
			row = jump_demo_rows + jump_demo_count++;
			Q_strlcpy (row->name, list[i], sizeof (row->name));
			row->directory = !kind;
			if (kind)
			{
				Com_sprintf (path, sizeof (path), "%s/%s", directory, list[i]);
				CL_JumpDemoInfo (path, row);
			}
		}
		CL_JumpDemosListFree (list, count);
	}
	CL_JumpDemosSort (jump_demo_sort);
	return true;
}

const char *CL_JumpDemosDirectory (void)
{
	return jump_demo_directory;
}

int CL_JumpDemosCount (void)
{
	return jump_demo_count;
}

const jump_demo_row_t *CL_JumpDemosRow (int index)
{
	return index >= 0 && index < jump_demo_count ? jump_demo_rows + index : NULL;
}

static int CL_JumpDemosCompare (const void *a, const void *b)
{
	const jump_demo_row_t *x = a, *y = b;
	int direction = jump_demo_sort < 0 ? -1 : 1, compare, column = abs (jump_demo_sort);
	if (!strcmp (x->name, ".."))
		return strcmp (y->name, "..") ? -1 : 0;
	if (!strcmp (y->name, ".."))
		return 1;
	if (x->directory != y->directory)
		return x->directory ? -1 : 1;
	if (column == 2)
		compare = (x->size > y->size) - (x->size < y->size);
	else if (column == 3)
		compare = Q_stricmp (x->map, y->map);
	else
		compare = Q_stricmp (x->name, y->name);
	if (!compare)
		compare = Q_stricmp (x->name, y->name);
	return direction * compare;
}

void CL_JumpDemosSort (int column)
{
	jump_demo_sort = column >= -3 && column <= 3 ? column : 1;
	if (jump_demo_rows)
		qsort (jump_demo_rows, jump_demo_count, sizeof (*jump_demo_rows), CL_JumpDemosCompare);
}

qboolean CL_JumpDemosSelect (int index)
{
	const jump_demo_row_t *row = CL_JumpDemosRow (index);
	char path[MAX_QPATH], command[MAX_QPATH + 32];
	if (!CL_JumpEnabled ())
		return false;
	if (!row)
		return false;
	if (row->directory)
	{
		if (!strcmp (row->name, ".."))
		{
			char *slash;
			Q_strlcpy (path, jump_demo_directory, sizeof (path));
			slash = strrchr (path, '/');
			if (!slash)
				return false;
			*slash = 0;
		}
		else
			Com_sprintf (path, sizeof (path), "%s/%s", jump_demo_directory, row->name);
		return CL_JumpDemosOpen (path);
	}
	if (!row->playable || !CL_JumpDemoName (row->name, false))
		return false;
	if (!strcmp (jump_demo_directory, "demos"))
		Q_strlcpy (path, row->name, sizeof (path));
	else
		Com_sprintf (path, sizeof (path), "%s/%s", jump_demo_directory + 6, row->name);
	Com_sprintf (command, sizeof (command), "demomap \"%s\"\n", path);
	Cbuf_AddText (command);
	return true;
}
