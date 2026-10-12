/*
Copyright (C) 1997-2001 Id Software, Inc.

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
/* Frozen model discovery adapted to the native merged filesystem. */
#include "jump_menu.h"

/*
=============================================================================

PLAYER MODELS

=============================================================================
*/

static bool IconOfSkinExists (const char *skin, char **files, int nfiles)
{
	int i;
	char scratch[MAX_QPATH];

	CL_JumpUIStripExtension (scratch, skin, sizeof (scratch));
	Q_strlcat (scratch, "_i", sizeof (scratch));

	for (i = 0; i < nfiles; i++)
	{
		if (!Q_strncasecmp (files[i], scratch, strlen (scratch)))
			return true;
	}

	return false;
}

static int pmicmpfnc (const void *_a, const void *_b)
{
	const playerModelInfo_t *a = (const playerModelInfo_t *)_a;
	const playerModelInfo_t *b = (const playerModelInfo_t *)_b;

	/*
    ** sort by male, female, then alphabetical
    */
	if (Q_stricmp (a->directory, "male") == 0)
		return -1;
	if (Q_stricmp (b->directory, "male") == 0)
		return 1;

	if (Q_stricmp (a->directory, "female") == 0)
		return -1;
	if (Q_stricmp (b->directory, "female") == 0)
		return 1;

	return Q_stricmp (a->directory, b->directory);
}

void PlayerModel_Load (void)
{
	char scratch[MAX_QPATH];
	int i, ndirs;
	char **dirnames;
	playerModelInfo_t *pmi;

	Q_assert (!uis.numPlayerModels);

	// get a list of directories
	if (!(dirnames = (char **)CL_JumpUIList ("players", NULL, true, &ndirs)))
	{
		return;
	}

	// go through the subdirectories
	for (i = 0; i < ndirs; i++)
	{
		int k;
		char **allfiles;
		char **skinnames;
		int nfiles;
		int nskins = 0;
		static const char *skin_exts[] = { ".pcx", ".png", ".jpg", ".tga" };
		int ext;

		// verify the existence of tris.md2
		Q_concat (scratch, sizeof (scratch), "players/", dirnames[i], "/tris.md2");
		if (!CL_JumpUIFileExists (scratch))
		{
			continue;
		}

		// collect all supported image files in one list
		allfiles = NULL;
		nfiles = 0;

		for (ext = 0; ext < q_countof (skin_exts); ext++)
		{
			char **extfiles;
			int nextfiles;

			Q_concat (scratch, sizeof (scratch), "players/", dirnames[i]);
			extfiles = (char **)CL_JumpUIList (scratch, skin_exts[ext], 0, &nextfiles);
			if (!extfiles || !nextfiles)
				continue;

			allfiles = CL_JumpUIReallocList (allfiles, nfiles + nextfiles);
			memcpy (allfiles + nfiles, extfiles, nextfiles * sizeof (char *));
			Z_Free (extfiles);
			nfiles += nextfiles;
		}

		if (!allfiles)
		{
			continue;
		}

		// NULL-terminate the concatenated list for CL_JumpUIFreeList
		allfiles = CL_JumpUIReallocList (allfiles, nfiles + 1);
		allfiles[nfiles] = NULL;

		// copy the valid skins (deduplicated by base name)
		skinnames = UI_Malloc (sizeof (char *) * (nfiles + 1));
		nskins = 0;

		for (k = 0; k < nfiles; k++)
		{
			int t;

			if (strstr (allfiles[k], "_i."))
				continue;

			if (!IconOfSkinExists (allfiles[k], allfiles, nfiles))
				continue;

			CL_JumpUIStripExtension (scratch, allfiles[k], sizeof (scratch));

			for (t = 0; t < nskins; t++)
			{
				if (!Q_stricmp (skinnames[t], scratch))
					break;
			}
			if (t < nskins)
				continue; // already added

			skinnames[nskins++] = UI_CopyString (scratch);
		}

		skinnames[nskins] = NULL;

		if (!nskins)
		{
			CL_JumpUIFreeList ((void **)allfiles);
			Z_Free (skinnames);
			continue;
		}

		CL_JumpUIFreeList ((void **)allfiles);

		// at this point we have a valid player model
		pmi = &uis.pmi[uis.numPlayerModels++];
		pmi->nskins = nskins;
		pmi->skindisplaynames = skinnames;
		pmi->directory = UI_CopyString (dirnames[i]);

		if (uis.numPlayerModels == MAX_PLAYERMODELS)
			break;
	}

	CL_JumpUIFreeList ((void **)dirnames);

	qsort (uis.pmi, uis.numPlayerModels, sizeof (uis.pmi[0]), pmicmpfnc);
}

void PlayerModel_Free (void)
{
	playerModelInfo_t *pmi;
	int i, j;

	for (i = 0, pmi = uis.pmi; i < uis.numPlayerModels; i++, pmi++)
	{
		if (pmi->skindisplaynames)
		{
			for (j = 0; j < pmi->nskins; j++)
			{
				Z_Free (pmi->skindisplaynames[j]);
			}
			Z_Free (pmi->skindisplaynames);
		}
		Z_Free (pmi->directory);
		memset (pmi, 0, sizeof (*pmi));
	}

	uis.numPlayerModels = 0;
}
