/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// models.c -- model loading and caching

#include "gl_local.h"
#include "gl_jump.h"
#include "../qcommon/md3_format.h"
#include "../qcommon/bsp_node_graph.h"
#include "../qcommon/bsp_format.h"
#include <errno.h>
#include <float.h>
#include <limits.h>


model_t	*loadmodel;
int		modfilelen;
/* Set by the validated BSPX directory lookup, even without DECOUPLED_LM. */
static qboolean mod_bspx_directory_present;
/* ERR_DROP leaves the renderer through longjmp. Own the in-flight file and
 * model slot until loading completes, then release them at the next entry. */
static model_t *mod_pending_model;
static void *mod_pending_buffer;

static void Mod_FreeLoadFile (void *buffer)
{
	if (mod_pending_buffer == buffer)
		mod_pending_buffer = NULL;
	ri.FS_FreeFile (buffer);
}

static void Mod_FreePendingLoad (void)
{
	model_t *mod = mod_pending_model;
	void *buffer = mod_pending_buffer;

	mod_pending_model = NULL;
	mod_pending_buffer = NULL;
	if (buffer)
		ri.FS_FreeFile (buffer);
	if (mod)
		Mod_Free (mod);
}

void Mod_LoadSpriteModel (model_t *mod, void *buffer);
void Mod_LoadBrushModel (model_t *mod, void *buffer);
void Mod_LoadAliasModel (model_t *mod, void *buffer);
void Mod_LoadMd3Model (model_t *mod, void *buffer);
static void Mod_LoadMD5 (model_t *mod, const byte *base_file, int base_len);
static void Mod_FreeMD5 (mmd5model_t *md5);

static void Mod_QBSPReject (const byte *file, const char *name,
	const char *reason, int index)
{
	char model_name[MAX_QPATH];

	strncpy (model_name, name, sizeof (model_name) - 1);
	model_name[sizeof (model_name) - 1] = 0;
	Mod_FreeLoadFile ((void *)file);
	loadmodel->name[0] = 0;
	ri.Sys_Error (ERR_DROP, "Mod_ForName: %s has %s at %d", model_name, reason, index);
}

static void Mod_CheckQBSPCommon (const byte *file,
	const r1bsp_disk_layout_t *layout, const char *name, int *reserve);
static int Mod_FindBSPXDecoupledLM (const byte *file, size_t filelen,
	const r1bsp_disk_layout_t *layout, const byte **records);
static int Mod_FindIBSPDecoupledLM (const byte *file, size_t filelen,
	const dheader_t *header, r1bsp_disk_layout_t *layout,
	const byte **records);
static int Mod_CheckIBSPBSPXCommon (const byte *file,
	const r1bsp_disk_layout_t *layout);
static int Mod_CheckBSPXRecords (const byte *file,
	const r1bsp_disk_layout_t *layout, const byte *records);
model_t *Mod_LoadModel (model_t *mod, qboolean crash);

byte	mod_novis[MAX_MAP_LEAFS/8];

#define	MAX_MOD_KNOWN	512
model_t	mod_known[MAX_MOD_KNOWN];
int		mod_numknown;

// the inline * models from the current map are kept seperate
model_t	mod_inline[MAX_MOD_KNOWN];

int			registration_sequence;
qboolean	r_registering;
/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t		*node;
	float		d;
	cplane_t	*plane;
	
	if (!model || !model->nodes)
		ri.Sys_Error (ERR_DROP, "Mod_PointInLeaf: bad model");

	node = model->nodes;
	for (;;)
	{
		if (node->contents != -1)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		/* Match collision's child choice for points exactly on the plane. */
		if (!FLOAT_LT_ZERO (d))
			node = node->children[0];
		else
			node = node->children[1];
	}
	
	//return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	static byte	decompressed[MAX_MAP_LEAFS/8];
	int		c;
	byte	*out;
	int		row;

	row = (model->vis->numclusters+7)>>3;	
	out = decompressed;

	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return decompressed;		
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}
	
		c = in[1];
		in += 2;
		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);
	
	return decompressed;
}

/*
==============
Mod_ClusterPVS
==============
*/
byte *Mod_ClusterPVS (int cluster, model_t *model)
{
	if (cluster == -1 || !model->vis)
		return mod_novis;
	return Mod_DecompressVis ( (byte *)model->vis + model->vis->bitofs[cluster][DVIS_PVS],
		model);
}


//===============================================================================

/*
================
Mod_Modellist_f
================
*/
void Mod_Modellist_f (void)
{
	int		i;
	model_t	*mod;
	int		total, num;
	int numbrush, numalias, nummd3, numsprites, numsub;

	total = num = 0;
	numbrush = numalias = nummd3 = numsprites = numsub = 0;

	ri.Con_Printf (PRINT_ALL,"Loaded models:\n");
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		num++;
		switch (mod->type)
		{
			case mod_brush:
				ri.Con_Printf (PRINT_ALL, "B ");
				numsub += mod->numsubmodels;
				numbrush++;
				break;
			case mod_sprite:
				ri.Con_Printf (PRINT_ALL, "S ");
				numsprites++;
				break;
			case mod_alias:
				ri.Con_Printf (PRINT_ALL, "A ");
				numalias++;
				break;
			case mod_empty:
				ri.Con_Printf (PRINT_ALL, "E ");
				break;
			case mod_md3:
				ri.Con_Printf (PRINT_ALL, "3 ");
				nummd3++;
				break;
			default:
				ri.Con_Printf (PRINT_ALL, "! ");
				break;
		}
		ri.Con_Printf (PRINT_ALL, "%8i : %s\n", mod->extradatasize, mod->name);
		total += mod->extradatasize;
	}

	ri.Con_Printf (PRINT_ALL, "%d brush models (B) with %d submodels, %d alias models (A), %d MD3 models (3), %d sprites (S)\n", numbrush, numsub, numalias, nummd3, numsprites);
	ri.Con_Printf (PRINT_ALL, "Total resident: %i bytes (%.2f MB) in %d models (%d with submodels)\n", total, (float)total / 1024 / 1024, num, num + numsub);
}

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	memset (mod_novis, 0xff, sizeof(mod_novis));
}

#define MODEL_HASH_SIZE	32

typedef struct mscache_s
{
	char				name[MAX_QPATH];
	struct mscache_s	*hash_next;
	int					size;
} mscache_t;

static model_t		*models_hash[MODEL_HASH_SIZE];
static mscache_t	*model_size_cache[MODEL_HASH_SIZE];

enum
{
	MOD_BSP38_OK,
	MOD_BSP38_SHORT,
	MOD_BSP38_IDENT,
	MOD_BSP38_VERSION,
	MOD_BSP38_LUMP
};

// Decode a private header copy. Lump order and padding are not constrained.
static int Mod_DecodeBSP38Header (const void *buffer, int length,
	dheader_t *header, int *bad_lump)
{
	int i, offset, size;

	if (length < (int)sizeof (*header))
		return MOD_BSP38_SHORT;

	memcpy (header, buffer, sizeof (*header));
	header->ident = LittleLong (header->ident);
	header->version = LittleLong (header->version);
	if (header->ident != IDBSPHEADER)
		return MOD_BSP38_IDENT;
	if (header->version != BSPVERSION)
		return MOD_BSP38_VERSION;

	for (i = 0; i < MAX_LUMPS; i++)
	{
		offset = LittleLong (header->lumps[i].fileofs);
		size = LittleLong (header->lumps[i].filelen);
		header->lumps[i].fileofs = offset;
		header->lumps[i].filelen = size;
		if (i == LUMP_POP)
			continue; // unused in BSP38; old maps may contain invalid POP fields
		if (offset < 0 || size < 0 || offset > length || size > length - offset)
		{
			*bad_lump = i;
			return MOD_BSP38_LUMP;
		}
	}
	return MOD_BSP38_OK;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, qboolean crash)
{
	model_t		*mod;
	model_t		*modelhash;
	mscache_t	*model_size;
	byte		*buf;
	int			i;
	unsigned	hash;
	int variable_brush_hunk = 0;

	Mod_FreePendingLoad ();

	if (!name || !name[0])
		ri.Sys_Error (ERR_DROP, "Mod_ForName: NULL name");
		
	//
	// inline models are grabbed only from worldmodel
	//
	if (name[0] == '*')
	{
		i = atoi(name+1);
		if (i < 1 || !r_worldmodel || i >= r_worldmodel->numsubmodels)
			ri.Sys_Error (ERR_DROP, "bad inline model number %d", i);
		return &mod_inline[i];
	}

	fast_strlwr (name);

	hash = hashify (name) % MODEL_HASH_SIZE;

	for (modelhash = models_hash[hash]; modelhash; modelhash = modelhash->hash_next)
	{
		if (!strcmp (modelhash->name, name))
		{
			return modelhash;
		}
	}

	for (model_size = model_size_cache[hash]; model_size; model_size = model_size->hash_next)
	{
		if (!strcmp (model_size->name, name))
			break;
	}

	//
	// search the currently loaded models
	//
	/*for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0] || mod->hash != hash)
			continue;
		if (!strcmp (mod->name, name) )
			return mod;
	}*/
	
	//
	// find a free model slot spot
	//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			break;	// free spot
	}

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			ri.Sys_Error (ERR_DROP, "mod_numknown == MAX_MOD_KNOWN");
		mod_numknown++;
	}

	strncpy (mod->name, name, sizeof(mod->name)-1);

	//
	// load the file
	//
	mod_pending_model = mod;
	modfilelen = ri.FS_LoadFile (name, &mod_pending_buffer);
	buf = mod_pending_buffer;
	if (!buf)
	{
		mod_pending_model = NULL;
		mod->name[0] = 0;
		if (crash)
			ri.Sys_Error (ERR_DROP, "Mod_NumForName: %s not found", name);
		return NULL;
	}

	if (modfilelen < (int)sizeof (int))
	{
		Mod_FreeLoadFile (buf);
		mod->name[0] = 0;
		ri.Sys_Error (ERR_DROP, "Mod_ForName: %s has a truncated model identifier", name);
		return NULL;
	}

	loadmodel = mod;

	//
	// fill it in
	//


	// call the apropriate loader
	
	switch (LittleLong(*(unsigned *)buf))
	{
		case IDALIASHEADER:
			/* Validate and reserve the current normalized MD2 size. */
			Mod_LoadAliasModel (mod, buf);
			break;

		case MD3_IDENT:
			// MD3 validates all file sections before it starts its exact-size hunk.
			Mod_LoadMd3Model (mod, buf);
			break;
			
		case IDSPRITEHEADER:
			// Allocate using the validated sprite table size.
			Mod_LoadSpriteModel (mod, buf);
			break;
		
		case IDBSPHEADER:
		case IDQBSPHEADER:
		{
			dheader_t bsp_header;
			r1bsp_disk_layout_t qbsp_layout;
			int bad_lump = -1;
			int qbsp_reserve = 0;
			int bspx_status, bad_face;
			const byte *lm_records = NULL;
			int qbsp = R1BSP_ReadU32 ((const byte *)buf) == IDQBSPHEADER;
			int check = qbsp ? (int)R1BSP_Preflight ((const byte *)buf,
								   (size_t)modfilelen, &qbsp_layout) :
							   Mod_DecodeBSP38Header (buf, modfilelen, &bsp_header, &bad_lump);
			if (check != 0)
			{
				Mod_FreeLoadFile (buf);
				mod->name[0] = 0;
				if (qbsp)
				{
					ri.Sys_Error (ERR_DROP,
						"Mod_ForName: %s has invalid QBSP header/lump %d (status %d)",
						name, qbsp_layout.bad_lump, check);
					return NULL;
				}
				if (check == MOD_BSP38_SHORT)
					ri.Sys_Error (ERR_DROP, "Mod_ForName: %s has a truncated BSP38 header", name);
				else if (check == MOD_BSP38_IDENT)
					ri.Sys_Error (ERR_DROP, "Mod_ForName: %s is not an IBSP map", name);
				else if (check == MOD_BSP38_VERSION)
					ri.Sys_Error (ERR_DROP, "Mod_ForName: %s has unsupported BSP version %d", name, bsp_header.version);
				else
					ri.Sys_Error (ERR_DROP, "Mod_ForName: %s has invalid BSP lump %d (offset %d, size %d)",
						name, bad_lump, bsp_header.lumps[bad_lump].fileofs,
						bsp_header.lumps[bad_lump].filelen);
				return NULL;
			}
			if (qbsp)
			{
				Mod_CheckQBSPCommon ((const byte *)buf, &qbsp_layout, name,
					&qbsp_reserve);
				bspx_status = Mod_FindBSPXDecoupledLM ((const byte *)buf,
					(size_t)modfilelen, &qbsp_layout, &lm_records);
			}
			else
			{
				bspx_status = Mod_FindIBSPDecoupledLM ((const byte *)buf,
					(size_t)modfilelen, &bsp_header, &qbsp_layout, &lm_records);
			}
			if (bspx_status < 0)
			{
				Mod_FreeLoadFile (buf);
				mod->name[0] = 0;
				ri.Sys_Error (ERR_DROP,
					"Mod_ForName: %s has an invalid BSPX directory or DECOUPLED_LM lump", name);
				return NULL;
			}
			if (bspx_status > 0 && !qbsp)
			{
				check = (int)R1BSP_Preflight ((const byte *)buf,
					(size_t)modfilelen, &qbsp_layout);
				if (check != 0)
				{
					Mod_FreeLoadFile (buf);
					mod->name[0] = 0;
					ri.Sys_Error (ERR_DROP,
						"Mod_ForName: %s has an invalid IBSP BSPX layout (status %d)",
						name, check);
					return NULL;
				}
				bad_face = Mod_CheckIBSPBSPXCommon ((const byte *)buf,
					&qbsp_layout);
				if (bad_face >= 0)
				{
					Mod_FreeLoadFile (buf);
					mod->name[0] = 0;
					ri.Sys_Error (ERR_DROP,
						"Mod_ForName: %s has invalid IBSP BSPX geometry at %d",
						name, bad_face);
					return NULL;
				}
			}
			if (bspx_status > 0)
			{
				bad_face = Mod_CheckBSPXRecords ((const byte *)buf,
					&qbsp_layout, lm_records);
				if (bad_face >= 0)
				{
					Mod_FreeLoadFile (buf);
					mod->name[0] = 0;
					ri.Sys_Error (ERR_DROP,
						"Mod_ForName: %s has invalid BSPX face %d", name, bad_face);
					return NULL;
				}
			}
			if (qbsp)
			{
				variable_brush_hunk = 1;
				loadmodel->extradata = Hunk_Begin (qbsp_reserve, 0);
			}
			else if (bspx_status > 0)
			{
				/* A cached exact classic size can belong to older bytes with
				 * the same name. Reserve a bounded, growable map hunk instead. */
				variable_brush_hunk = 1;
				loadmodel->extradata = Hunk_Begin (64 * 1024 * 1024, 0);
			}
			else if (model_size)
				loadmodel->extradata = Hunk_Begin (model_size->size, model_size->size);
			else
				loadmodel->extradata = Hunk_Begin (0x1000000, 0);
			Mod_LoadBrushModel (mod, buf);
			break;
		}

		default:
			ri.Sys_Error (ERR_DROP,"Mod_NumForName: unknown 0x%.8x fileid for %s", LittleLong(*(unsigned *)buf), mod->name);
			break;
	}

	if (mod->type == mod_empty)
	{
		loadmodel->extradatasize = 0;
	}
	else if (mod->type == mod_alias || mod->type == mod_md3 || mod->type == mod_sprite || variable_brush_hunk)
	{
		// Checked model sizes may change when an asset is replaced in place.
		loadmodel->extradatasize = Hunk_End ();
	}
	else if (model_size)
	{
		loadmodel->extradatasize = model_size->size;
	}
	else
	{
		loadmodel->extradatasize = Hunk_End ();

		model_size = malloc (sizeof(*model_size));
		if (!model_size)
			ri.Sys_Error (ERR_FATAL, "Mod_ForName: out of memory");
		strcpy (model_size->name, mod->name);
		model_size->size = loadmodel->extradatasize;
		model_size->hash_next = model_size_cache[hash];

		model_size_cache[hash] = model_size;
	}

	if (mod->type == mod_alias)
		GL_BuildAliasBuffer (mod, (dmdl_t *)mod->extradata);
	if ((mod->type == mod_alias || mod->type == mod_md3) &&
		gl_md5_load && gl_md5_load->intvalue)
		Mod_LoadMD5 (mod, buf, modfilelen);

	mod->hash_next = models_hash[hash];
	models_hash[hash] = mod;

	Mod_FreeLoadFile (buf);

	mod_pending_model = NULL;
	return mod;
}

/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

byte	*mod_base;


/*
=================
Mod_LoadLighting
=================
*/
void Mod_LoadLighting (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->lightdata = NULL;
		return;
	}
	loadmodel->lightdata = Hunk_Alloc ( l->filelen);	
	memcpy (loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
#if Q_BIGENDIAN
	int		i;
#endif

	if (!l->filelen)
	{
		loadmodel->vis = NULL;
		return;
	}

	loadmodel->vis = Hunk_Alloc ( l->filelen);	
	memcpy (loadmodel->vis, mod_base + l->fileofs, l->filelen);

#if Q_BIGENDIAN
	loadmodel->vis->numclusters = LittleLong (loadmodel->vis->numclusters);

	for (i=0 ; i<loadmodel->vis->numclusters ; i++)
	{
		loadmodel->vis->bitofs[i][0] = LittleLong (loadmodel->vis->bitofs[i][0]);
		loadmodel->vis->bitofs[i][1] = LittleLong (loadmodel->vis->bitofs[i][1]);
	}
#endif
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
	const byte *in;
	mvertex_t	*out;
	int			count;

#if Q_BIGENDIAN
	int			i;
	dvertex_t record;
#endif

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dvertex_t))
		ri.Sys_Error (ERR_DROP, "Mod_LoadVertexes: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof (dvertex_t);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

#if Q_BIGENDIAN
	for (i = 0; i < count; i++, in += sizeof (record), out++)
	{
		memcpy (&record, in, sizeof (record));
		out->position[0] = LittleFloat (record.point[0]);
		out->position[1] = LittleFloat (record.point[1]);
		out->position[2] = LittleFloat (record.point[2]);
	}
#else
	memcpy (out, in, sizeof(dvertex_t)*count);
#endif
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	vec3_t	corner;

	corner[0] = (float)(fabs(mins[0]) > fabs(maxs[0]) ? fabs(mins[0]) : fabs(maxs[0]));
	corner[1] = (float)(fabs(mins[1]) > fabs(maxs[1]) ? fabs(mins[1]) : fabs(maxs[1]));
	corner[2] = (float)(fabs(mins[2]) > fabs(maxs[2]) ? fabs(mins[2]) : fabs(maxs[2]));

	return VectorLength (corner);
}


/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	const byte *disk;
	dmodel_t record, *in = &record;
	mmodel_t	*out;
	int			i, count;

	disk = mod_base + l->fileofs;
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for (i = 0; i < count; i++, disk += sizeof (record), out++)
	{
		memcpy (&record, disk, sizeof (record));
		out->mins[0] = LittleFloat (in->mins[0]) - 1;
		out->maxs[0] = LittleFloat (in->maxs[0]) + 1;
		out->origin[0] = LittleFloat (in->origin[0]);
		out->mins[1] = LittleFloat (in->mins[1]) - 1;
		out->maxs[1] = LittleFloat (in->maxs[1]) + 1;
		out->origin[1] = LittleFloat (in->origin[1]);
		out->mins[2] = LittleFloat (in->mins[2]) - 1;
		out->maxs[2] = LittleFloat (in->maxs[2]) + 1;
		out->origin[2] = LittleFloat (in->origin[2]);

		out->radius = RadiusFromBounds (out->mins, out->maxs);
		out->headnode = LittleLong (in->headnode);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
		out->visleafs = 0;
	}
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges (lump_t *l)
{
	const byte *disk;
	dedge_t record, *in = &record;
	medge_t *out;
	int 	i, count;

	disk = mod_base + l->fileofs;

	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "Mod_LoadEdges: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);

	//r1: was count+1
	out = Hunk_Alloc (count * sizeof(*out));	

	loadmodel->edges = out;
	loadmodel->numedges = count;

	for (i = 0; i < count; i++, disk += sizeof (record), out++)
	{
		memcpy (&record, disk, sizeof (record));
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

//FIXME: this is bad, loads entire file for just 8 bytes!
qboolean GetWalInfo (const char *name, int *width, int *height)
{
	miptex_t *mt;
	int length;
	qboolean valid = false;

	*width = *height = 0;
	length = ri.FS_LoadFile (name, (void **)&mt);
	if (!mt)
		return false;
	if (length >= (int)sizeof (*mt))
	{
		*width = LittleLong (mt->width);
		*height = LittleLong (mt->height);
		valid = *width > 0 && *height > 0 &&
			*width <= MAX_TEXTURE_DIMENSIONS && *height <= MAX_TEXTURE_DIMENSIONS;
	}
	ri.FS_FreeFile (mt);
	if (!valid)
		*width = *height = 0;
	return valid;
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (lump_t *l)
{
	const byte *disk;
	texinfo_t record, *in = &record;
	//	image_t	*last_image;
	mtexinfo_t *out, *step;
	int 	i, count;
	char	name[MAX_QPATH];
	int		next;

	disk = mod_base + l->fileofs;

	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "Mod_LoadTexinfo: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for (i = 0; i < count; i++, disk += sizeof (record), out++)
	{
		memcpy (&record, disk, sizeof (record));
		if (!memchr (in->texture, 0, sizeof (in->texture)))
			ri.Sys_Error (ERR_DROP, "Mod_LoadTexinfo: unterminated texture name in %s", loadmodel->name);
#if Q_BIGENDIAN
		out->vecs[0][0] = LittleFloat (in->vecs[0][0]);
		out->vecs[0][1] = LittleFloat (in->vecs[0][1]);
		out->vecs[0][2] = LittleFloat (in->vecs[0][2]);
		out->vecs[0][3] = LittleFloat (in->vecs[0][3]);

		out->vecs[1][0] = LittleFloat (in->vecs[1][0]);
		out->vecs[1][1] = LittleFloat (in->vecs[1][1]);
		out->vecs[1][2] = LittleFloat (in->vecs[1][2]);
		out->vecs[1][3] = LittleFloat (in->vecs[1][3]);
#else
		memcpy (out->vecs, in->vecs, sizeof(out->vecs));
#endif

		out->flags = LittleLong (in->flags);
		out->is_lava = GL_JumpLavaName (in->texture, sizeof (in->texture));
		out->sky_material = SKY_MATERIAL_DEFAULT;
		out->sky_name[0] = 0;
		out->sky_fallback_checked = false;
		next = LittleLong (in->nexttexinfo);

		if (next > 0 && next < count)
			out->next = loadmodel->texinfo + next;
		else if (next >= count)
			ri.Sys_Error (ERR_DROP, "Mod_LoadTexinfo: bad animation target in %s", loadmodel->name);
		else
		    out->next = NULL;

		if (mod_bspx_directory_present &&
			(out->flags & (SURF_NODRAW | SURF_SKY)) == SURF_NODRAW)
		{
			out->image = r_notexture;
			continue;
		}

		fast_strlwr (in->texture);
		if (out->flags & SURF_SKY)
		{
			if (strstr (in->texture, "env/sky"))
				out->sky_material = SKY_MATERIAL_CLASSIC;
			else if (!strncmp (in->texture, "sky/", 4) && in->texture[4])
				out->sky_material = SKY_MATERIAL_CUBE;
			if (out->sky_material != SKY_MATERIAL_DEFAULT)
			{
				memcpy (out->sky_name, in->texture, sizeof (out->sky_name));
				out->image = r_notexture;
				continue;
			}
		}

		Com_sprintf (name, sizeof (name), "textures/%s.wal", in->texture);

		out->image = GL_FindWallImage (name, in->texture, !!(out->flags & SURF_WARP));
		if (!out->image)
		{
			ri.Con_Printf (PRINT_ALL, "Couldn't load %s\n", name);
			out->image = r_notexture;
		}
	}

	// count animation frames
	for (i=0 ; i<count ; i++)
	{
		out = &loadmodel->texinfo[i];
		out->numframes = 1;
		for (step = out->next ; step && step != out ; step=step->next)
		{
			if (out->numframes >= count)
				ri.Sys_Error (ERR_DROP, "Mod_LoadTexinfo: animation cycle excludes start in %s", loadmodel->name);
			out->numframes++;
		}
	}
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;
	
	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		
		for (j=0 ; j<2 ; j++)
		{
			val = v->position[0] * tex->vecs[j][0] + 
				v->position[1] * tex->vecs[j][1] +
				v->position[2] * tex->vecs[j][2] +
				tex->vecs[j][3];
			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{	
		bmins[i] = (int)floor(mins[i]/16);
		bmaxs[i] = (int)ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

//		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 512 /* 256 */ )
//			ri.Sys_Error (ERR_DROP, "Bad surface extents");
	}
}


void GL_BuildPolygonFromSurface(msurface_t *fa);
void GL_CreateSurfaceLightmap (msurface_t *surf);
qboolean GL_TryCreateSurfaceLightmap (msurface_t *surf);
void GL_EndBuildingLightmaps (void);
void GL_BeginBuildingLightmaps (qboolean decoupled, int min_side);
int GL_LightmapBlockSize (void);

/* The classic loader also consumes RGB light samples. Check the complete
 * styled face span before forming a pointer into the lighting lump. */
static qboolean Mod_ValidateClassicLightSamples (const msurface_t *surf,
	int lightofs, int lighting_size, int face)
{
	size_t samples;
	int maps;

	/* Classic warp and style-less faces have no sample consumer. Ignore their
	 * stale offsets, as well as offsets retained by maps without lighting. */
	if ((surf->texinfo->flags & SURF_WARP) || surf->styles[0] == 255 ||
		lightofs == -1 || !lighting_size)
		return false;
	if (lightofs < 0 || lightofs >= lighting_size ||
		surf->extents[0] < 0 || surf->extents[1] < 0)
		ri.Sys_Error (ERR_DROP, "Mod_LoadFaces: face %d has bad light offset in %s",
			face, loadmodel->name);
	for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		;
	samples = (size_t)((surf->extents[0] >> 4) + 1) *
		(size_t)((surf->extents[1] >> 4) + 1) * 3u * (size_t)maps;
	if (samples > (size_t)(lighting_size - lightofs))
		ri.Sys_Error (ERR_DROP, "Mod_LoadFaces: face %d has truncated light samples in %s",
			face, loadmodel->name);
	return true;
}

/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces (lump_t *l, int lighting_size)
{
	const byte *disk;
	dface_t record, *in = &record;
	msurface_t 	*out;
	int			i, count, surfnum;
	int			planenum, side;
	int			ti;

	disk = mod_base + l->fileofs;

	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "Mod_LoadFaces: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);

	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	currentmodel = loadmodel;

	GL_BeginBuildingLightmaps (false, 0);

	for (surfnum = 0; surfnum < count; surfnum++, disk += sizeof (record), out++)
	{
		memcpy (&record, disk, sizeof (record));
		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);

		out->flags = 0;
		out->polys = NULL;

		out->texturechain = NULL;
		out->lightmapchain = NULL;
		out->lm_decoupled = false;
		out->dlight_s = 0;
		out->dlight_t = 0;
		out->dlightframe = 0;
		out->dlightbits = 0;

		out->visframe = 0;

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);
		if (side)
			out->flags |= SURF_PLANEBACK;			

		out->plane = loadmodel->planes + planenum;

		ti = LittleShort (in->texinfo);
		if (ti < 0 || ti >= loadmodel->numtexinfo)
			ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: bad texinfo number");

		out->texinfo = loadmodel->texinfo + ti;

		CalcSurfaceExtents (out);
				
	// lighting info

		//for (i=0 ; i<MAXLIGHTMAPS ; i++)
			//out->styles[i] = in->styles[i];

		memcpy (out->styles, in->styles, sizeof(byte) * MAXLIGHTMAPS);

		i = LittleLong(in->lightofs);
		if (!Mod_ValidateClassicLightSamples (out, i, lighting_size, surfnum) ||
			!loadmodel->lightdata)
			out->samples = NULL;
		else
			out->samples = loadmodel->lightdata + i;
		/* Vanilla maps historically draw this bit. BSPX maps use it to
		 * suppress otherwise valid faces, including hidden light sources. */
		if (mod_bspx_directory_present &&
			(out->texinfo->flags & (SURF_NODRAW | SURF_SKY)) == SURF_NODRAW)
		{
			out->flags |= SURF_BSPX_NODRAW;
			continue;
		}

	// set the drawing flags
		
		if (out->texinfo->flags & SURF_WARP)
		{
			out->flags |= SURF_DRAWTURB;
			for (i=0 ; i<2 ; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}
			GL_SubdivideSurface (out);	// cut up polygon for warps
		}

		// create lightmaps and polygons
		if (!(out->texinfo->flags & SURF_WARP)) 
		{
			if (!(out->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_DRAWSKY|SURF_DRAWTURB)))
			{
				GL_CreateSurfaceLightmap (out);
			}
			else
			{
				out->light_s = out->light_t = 0;
			}
		
			GL_BuildPolygonFromSurface(out);
		}
	}

	GL_EndBuildingLightmaps ();
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	typedef struct
	{
		mnode_t *node;
		int next_child;
	} parent_frame_t;

	parent_frame_t *stack;
	byte *seen;
	mnode_t *child;
	int depth, index;

	if (node->contents != -1)
	{
		node->parent = parent;
		return;
	}
	stack = malloc ((size_t)loadmodel->numnodes * sizeof (*stack));
	seen = calloc ((size_t)loadmodel->numnodes, 1);
	if (!stack || !seen)
	{
		free (stack);
		free (seen);
		ri.Sys_Error (ERR_DROP, "Mod_SetParent: out of memory in %s", loadmodel->name);
		return;
	}

	node->parent = parent;
	index = (int)(node - loadmodel->nodes);
	seen[index] = 1;
	stack[0].node = node;
	stack[0].next_child = 0;
	depth = 1;
	while (depth)
	{
		parent_frame_t *frame = &stack[depth - 1];
		if (frame->next_child == 2)
		{
			depth--;
			continue;
		}
		child = frame->node->children[frame->next_child++];
		child->parent = frame->node;
		if (child->contents != -1)
			continue;
		index = (int)(child - loadmodel->nodes);
		if (seen[index])
			continue;
		seen[index] = 1;
		stack[depth].node = child;
		stack[depth].next_child = 0;
		depth++;
	}
	free (stack);
	free (seen);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes (lump_t *l)
{
	int			i, j, count, p;
	const byte *disk;
	dnode_t record, *in = &record;
	mnode_t 	*out;
	bsp38_node_graph_error_t graph_error;
	bsp38_node_graph_status_t graph_status;

	disk = mod_base + l->fileofs;

	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "Mod_LoadNodes: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);
	if (count < 1 || count > MAX_MAP_NODES)
		ri.Sys_Error (ERR_DROP, "Mod_LoadNodes: bad node count %d in %s",
			count, loadmodel->name);
	graph_status = BSP38_ValidateNodeGraph (disk, count,
		loadmodel->numleafs, &graph_error);
	if (graph_status == BSP38_NODE_GRAPH_BAD_NODE)
		ri.Sys_Error (ERR_DROP, "Mod_LoadNodes: node %d child %d has bad node %d in %s",
			graph_error.node, graph_error.side, graph_error.child, loadmodel->name);
	else if (graph_status == BSP38_NODE_GRAPH_BAD_LEAF)
		ri.Sys_Error (ERR_DROP, "Mod_LoadNodes: node %d child %d has bad leaf %d in %s",
			graph_error.node, graph_error.side, graph_error.child, loadmodel->name);
	else if (graph_status == BSP38_NODE_GRAPH_SHARED_NODE)
		ri.Sys_Error (ERR_DROP, "Mod_LoadNodes: node %d child %d has shared node %d in %s",
			graph_error.node, graph_error.side, graph_error.child, loadmodel->name);
	else if (graph_status == BSP38_NODE_GRAPH_CYCLE)
		ri.Sys_Error (ERR_DROP, "Mod_LoadNodes: node %d child %d forms a cycle to node %d in %s",
			graph_error.node, graph_error.side, graph_error.child, loadmodel->name);
	else if (graph_status != BSP38_NODE_GRAPH_OK)
		ri.Sys_Error (ERR_DROP, "Mod_LoadNodes: out of memory validating nodes in %s",
			loadmodel->name);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i = 0; i < count; i++, disk += sizeof (record), out++)
	{
		memcpy (&record, disk, sizeof (record));
		out->minmaxs[0] = LittleShort (in->mins[0]);
		out->minmaxs[1] = LittleShort (in->mins[1]);
		out->minmaxs[2] = LittleShort (in->mins[2]);

		out->minmaxs[3] = LittleShort (in->maxs[0]);
		out->minmaxs[4] = LittleShort (in->maxs[1]);
		out->minmaxs[5] = LittleShort (in->maxs[2]);
	
		p = LittleLong(in->planenum);
		if (p < 0 || p >= loadmodel->numplanes)
			ri.Sys_Error (ERR_DROP, "Mod_LoadNodes: node %d has bad plane %d in %s",
				i, p, loadmodel->name);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleShort (in->firstface);
		out->numsurfaces = LittleShort (in->numfaces);
		if (out->firstsurface > loadmodel->numsurfaces ||
			out->numsurfaces > loadmodel->numsurfaces - out->firstsurface)
			ri.Sys_Error (ERR_DROP, "Mod_LoadNodes: node %d has bad face span in %s",
				i, loadmodel->name);
		out->contents = -1;	// differentiate from leafs

		out->parent = NULL;
		out->visframe = 0;

		for (j=0 ; j<2 ; j++)
		{
			p = LittleLong (in->children[j]);
			if (p >= 0)
				out->children[j] = loadmodel->nodes + p;
			else
				out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
		}
	}
	
	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l)
{
	const byte *disk;
	dleaf_t record, *in = &record;
	mleaf_t 	*out;
	int i, count, firstface, numfaces;
	//	glpoly_t	*poly;

	disk = mod_base + l->fileofs;

	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "Mod_LoadLeafs: funny lump size in %s",loadmodel->name);
	
	count = l->filelen / sizeof(*in);

	out = Hunk_Alloc ( count*sizeof(*out));	

	//memset (out, 0, count*sizeof(*out));

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i = 0; i < count; i++, disk += sizeof (record), out++)
	{
		memcpy (&record, disk, sizeof (record));
		out->minmaxs[0] = LittleShort (in->mins[0]);
		out->minmaxs[1] = LittleShort (in->mins[1]);
		out->minmaxs[2] = LittleShort (in->mins[2]);

		out->minmaxs[3] = LittleShort (in->maxs[0]);
		out->minmaxs[4] = LittleShort (in->maxs[1]);
		out->minmaxs[5] = LittleShort (in->maxs[2]);

		out->contents = LittleLong(in->contents);
		if (out->contents == -1)
			ri.Sys_Error (ERR_DROP, "Mod_LoadLeafs: leaf %d has reserved contents -1 in %s",
				i, loadmodel->name);
		out->cluster = LittleShort(in->cluster);
		out->area = LittleShort(in->area);

		firstface = (unsigned short)LittleShort (in->firstleafface);
		numfaces = (unsigned short)LittleShort (in->numleaffaces);
		if (firstface > loadmodel->nummarksurfaces ||
			numfaces > loadmodel->nummarksurfaces - firstface)
			ri.Sys_Error (ERR_DROP, "Mod_LoadLeafs: leaf %d has bad face span in %s",
				i, loadmodel->name);
		out->firstmarksurface = loadmodel->marksurfaces ?
			loadmodel->marksurfaces + firstface :
			NULL;
		out->nummarksurfaces = numfaces;

		out->parent = NULL;
		out->visframe = 0;
		
		// gl underwater warp
#if 0
		if (out->contents & (CONTENTS_WATER|CONTENTS_SLIME|CONTENTS_LAVA|CONTENTS_THINWATER) )
		{
			for (j=0 ; j<out->nummarksurfaces ; j++)
			{
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
				for (poly = out->firstmarksurface[j]->polys ; poly ; poly=poly->next)
					poly->flags |= SURF_UNDERWATER;
			}
		}
#endif
	}	
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l)
{
	int		i, j, count;
	const byte *in;
	msurface_t **out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (uint16))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof (uint16);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
		j = R1BSP_ReadU16 (in + i * sizeof (uint16));
		if (j < 0 ||  j >= loadmodel->numsurfaces)
			ri.Sys_Error (ERR_DROP, "Mod_ParseMarksurfaces: bad surface number");
		out[i] = loadmodel->surfaces + j;
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{
	int		count;
	const byte *in;
	int *out;

#if Q_BIGENDIAN
	int		i;
#endif

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (int32))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof (int32);
	if (count < 1 || count >= MAX_MAP_SURFEDGES)
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: bad surfedges count in %s: %i",
		loadmodel->name, count);

	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

#if Q_BIGENDIAN
	for ( i=0 ; i<count ; i++)
		out[i] = R1BSP_ReadI32 (in + i * sizeof (int32));
#else
	memcpy (out, in, sizeof (int32) * count);
#endif
}


/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	cplane_t	*out;
	const byte *disk;
	dplane_t record, *in = &record;
	int			count;
	int			bits;

	disk = mod_base + l->fileofs;

	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "Mod_LoadPlanes: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*2*sizeof(*out));	
	
	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for (i = 0; i < count; i++, disk += sizeof (record), out++)
	{
		memcpy (&record, disk, sizeof (record));
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (FLOAT_LT_ZERO(out->normal[j]))
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = (byte)LittleLong (in->type);
		out->signbits = bits;
	}
}

/* QBSP has wider disk records, but the renderer keeps its existing memory types. */
static void Mod_LoadQBSPEdges (lump_t *l)
{
	const byte *in = mod_base + l->fileofs;
	medge_t *out;
	uint32 v[2];
	int i, j, count = l->filelen / sizeof (qbsp_dedge_t);

	out = Hunk_Alloc (count * sizeof (*out));
	loadmodel->edges = out;
	loadmodel->numedges = count;
	for (i = 0; i < count; i++, in += sizeof (qbsp_dedge_t), out++)
	{
		for (j = 0; j < 2; j++)
		{
			v[j] = R1BSP_ReadU32 (in + j * 4);
			if (v[j] > USHRT_MAX || v[j] >= (uint32)loadmodel->numvertexes)
				ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPEdges: edge %d has bad vertex %u in %s",
					i, v[j], loadmodel->name);
			out->v[j] = (unsigned short)v[j];
		}
	}
}

static void Mod_CheckQBSPSurfedges (void)
{
	int i, edge;

	for (i = 0; i < loadmodel->numsurfedges; i++)
	{
		edge = loadmodel->surfedges[i];
		if (edge == INT_MIN ||
			(edge < 0 ? -edge : edge) >= loadmodel->numedges)
			ri.Sys_Error (ERR_DROP, "Mod_CheckQBSPSurfedges: bad edge %d at %d in %s",
				edge, i, loadmodel->name);
	}
}

static void Mod_LoadQBSPMarksurfaces (lump_t *l)
{
	const byte *in = mod_base + l->fileofs;
	msurface_t **out;
	uint32 face;
	int i, count = l->filelen / sizeof (qbsp_dleafface_t);

	out = Hunk_Alloc (count * sizeof (*out));
	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;
	for (i = 0; i < count; i++, in += sizeof (qbsp_dleafface_t))
	{
		face = R1BSP_ReadU32 (in);
		if (face >= (uint32)loadmodel->numsurfaces)
			ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPMarksurfaces: bad face %u at %d in %s",
				face, i, loadmodel->name);
		out[i] = loadmodel->surfaces + face;
	}
}

static void Mod_LoadQBSPLeafs (lump_t *l, uint32 leafbrush_count,
	qboolean has_visibility)
{
	const byte *in = mod_base + l->fileofs;
	mleaf_t *out;
	uint32 cluster, area, firstface, numfaces, firstbrush, numbrushes;
	float bound;
	int i, j, count = l->filelen / sizeof (qbsp_dleaf_t);

	out = Hunk_Alloc (count * sizeof (*out));
	loadmodel->leafs = out;
	loadmodel->numleafs = count;
	for (i = 0; i < count; i++, in += sizeof (qbsp_dleaf_t), out++)
	{
		out->contents = R1BSP_ReadI32 (in);
		if (out->contents == -1)
			ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPLeafs: leaf %d has reserved contents -1 in %s",
				i, loadmodel->name);
		cluster = R1BSP_QBSPLeafCluster (in, has_visibility);
		area = R1BSP_ReadU32 (in + offsetof (qbsp_dleaf_t, area));
		if ((cluster != (uint32)-1 && cluster >= MAX_MAP_LEAFS) ||
			area >= MAX_MAP_AREAS)
			ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPLeafs: leaf %d has bad cluster/area in %s",
				i, loadmodel->name);
		out->cluster = cluster == (uint32)-1 ? -1 : (int)cluster;
		out->area = (int)area;
		for (j = 0; j < 6; j++)
		{
			bound = R1BSP_ReadFloat (in + offsetof (qbsp_dleaf_t, mins) + j * 4);
			if (!(bound >= -1000000 && bound <= 1000000))
				ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPLeafs: leaf %d has unsafe bounds in %s",
					i, loadmodel->name);
			out->minmaxs[j] = bound;
		}
		firstface = R1BSP_ReadU32 (in + offsetof (qbsp_dleaf_t, firstleafface));
		numfaces = R1BSP_ReadU32 (in + offsetof (qbsp_dleaf_t, numleaffaces));
		firstbrush = R1BSP_ReadU32 (in + offsetof (qbsp_dleaf_t, firstleafbrush));
		numbrushes = R1BSP_ReadU32 (in + offsetof (qbsp_dleaf_t, numleafbrushes));
		if (firstface > (uint32)loadmodel->nummarksurfaces ||
			numfaces > (uint32)loadmodel->nummarksurfaces - firstface ||
			firstbrush > leafbrush_count || numbrushes > leafbrush_count - firstbrush)
			ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPLeafs: leaf %d has bad face/brush span in %s",
				i, loadmodel->name);
		out->firstmarksurface = loadmodel->marksurfaces ?
			loadmodel->marksurfaces + firstface :
			NULL;
		out->nummarksurfaces = (int)numfaces;
		out->parent = NULL;
		out->visframe = 0;
	}
}

static void Mod_LoadQBSPNodes (lump_t *l)
{
	const byte *in = mod_base + l->fileofs;
	mnode_t *out;
	bsp38_node_graph_error_t graph_error;
	bsp38_node_graph_status_t graph_status;
	uint32 plane, firstface, numfaces;
	float bound;
	int i, j, child, count = l->filelen / sizeof (qbsp_dnode_t);

	if (count < 1 || count > MAX_MAP_NODES)
		ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPNodes: bad node count %d in %s",
			count, loadmodel->name);
	graph_status = BSP38_ValidateNodeGraphLayoutLimit (in, count,
		loadmodel->numleafs, sizeof (qbsp_dnode_t),
		offsetof (qbsp_dnode_t, children), 256, &graph_error);
	if (graph_status == BSP38_NODE_GRAPH_TOO_DEEP)
		ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPNodes: node %d exceeds QBSP depth 256 in %s",
			graph_error.node, loadmodel->name);
	if (graph_status != BSP38_NODE_GRAPH_OK)
		ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPNodes: node graph error %d at node %d child %d in %s",
			graph_status, graph_error.node, graph_error.side, loadmodel->name);
	out = Hunk_Alloc (count * sizeof (*out));
	loadmodel->nodes = out;
	loadmodel->numnodes = count;
	for (i = 0; i < count; i++, in += sizeof (qbsp_dnode_t), out++)
	{
		plane = R1BSP_ReadU32 (in);
		if (plane >= (uint32)loadmodel->numplanes)
			ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPNodes: node %d has bad plane %u in %s",
				i, plane, loadmodel->name);
		out->plane = loadmodel->planes + plane;
		for (j = 0; j < 6; j++)
		{
			bound = R1BSP_ReadFloat (in + offsetof (qbsp_dnode_t, mins) + j * 4);
			if (!(bound >= -1000000 && bound <= 1000000))
				ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPNodes: node %d has unsafe bounds in %s",
					i, loadmodel->name);
			out->minmaxs[j] = bound;
		}
		firstface = R1BSP_ReadU32 (in + offsetof (qbsp_dnode_t, firstface));
		numfaces = R1BSP_ReadU32 (in + offsetof (qbsp_dnode_t, numfaces));
		if (firstface > USHRT_MAX || numfaces > USHRT_MAX ||
			firstface > (uint32)loadmodel->numsurfaces ||
			numfaces > (uint32)loadmodel->numsurfaces - firstface)
			ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPNodes: node %d has unrepresentable face span in %s",
				i, loadmodel->name);
		out->firstsurface = (unsigned short)firstface;
		out->numsurfaces = (unsigned short)numfaces;
		out->contents = -1;
		out->parent = NULL;
		out->visframe = 0;
		for (j = 0; j < 2; j++)
		{
			child = R1BSP_ReadI32 (in + offsetof (qbsp_dnode_t, children) + j * 4);
			out->children[j] = child >= 0 ? loadmodel->nodes + child :
											(mnode_t *)(loadmodel->leafs + (-1 - child));
		}
	}
	Mod_SetParent (loadmodel->nodes, NULL);
}

enum
{
	MAX_BSPX_LIGHTMAP_SIDE = 513,
	MAX_BSPX_LIGHTMAP_PIXELS = 513 * 513
};

/* BSPX follows the last standard BSP lump, rounded up to four bytes. Return
 * per-face DECOUPLED_LM records only when the table and that entry are valid. */
static int Mod_FindBSPXLump (const byte *file, size_t filelen,
	const r1bsp_disk_layout_t *layout, const char *name,
	const byte **data, size_t *length, qboolean *directory_present)
{
	size_t pos = sizeof (dheader_t), directory_end, name_length = strlen (name) + 1;
	uint32 count, offset, bytes;
	int i, found = 0;
	const byte *entry;

	*data = NULL;
	*length = 0;
	if (directory_present)
		*directory_present = false;
	for (i = 0; i < MAX_LUMPS; i++)
	{
		size_t end;

		if (i == LUMP_POP)
			continue;
		end = (size_t)layout->fileofs[i] + layout->filelen[i];
		if (end > pos)
			pos = end;
	}
	pos = (pos + 3u) & ~(size_t)3u;
	if (pos > filelen || filelen - pos < 8 || memcmp (file + pos, "BSPX", 4))
		return 0;
	count = R1BSP_ReadU32 (file + pos + 4);
	pos += 8;
	if (count > 4096 || count > (filelen - pos) / 32u)
		return -1;
	directory_end = pos + (size_t)count * 32u;
	for (i = 0; i < (int)count; i++)
	{
		entry = file + pos + (size_t)i * 32u;
		if (memcmp (entry, name, name_length))
			continue;
		if (found)
			return -1;
		offset = R1BSP_ReadU32 (entry + 24);
		bytes = R1BSP_ReadU32 (entry + 28);
		if (!bytes || (size_t)offset < directory_end ||
			(size_t)offset > filelen || (size_t)bytes > filelen - (size_t)offset)
			return -1;
		*data = file + offset;
		*length = bytes;
		found = 1;
	}
	if (directory_present)
		*directory_present = true;
	return found;
}

static int Mod_FindBSPXDecoupledLM (const byte *file, size_t filelen,
	const r1bsp_disk_layout_t *layout, const byte **records)
{
	size_t length;
	qboolean directory_present;
	int status;

	mod_bspx_directory_present = false;
	status = Mod_FindBSPXLump (file, filelen, layout, "DECOUPLED_LM",
		records, &length, &directory_present);
	if (status < 0 || (status > 0 && (length % 40u || length / 40u < layout->count[LUMP_FACES])))
		return -1;
	mod_bspx_directory_present = directory_present;
	return status;
}

/* The classic header is already decoded by Mod_ForName. Build only the
 * directory locator's layout here; a recognized extension gets the full
 * R1BSP_Preflight before it is allowed into the bounded face route. */
static int Mod_FindIBSPDecoupledLM (const byte *file, size_t filelen,
	const dheader_t *header, r1bsp_disk_layout_t *layout,
	const byte **records)
{
	int i;

	memset (layout, 0, sizeof (*layout));
	for (i = 0; i < MAX_LUMPS; i++)
	{
		if (i == LUMP_POP)
			continue;
		layout->fileofs[i] = (uint32)header->lumps[i].fileofs;
		layout->filelen[i] = (uint32)header->lumps[i].filelen;
	}
	layout->count[LUMP_FACES] = layout->filelen[LUMP_FACES] /
		sizeof (dface_t);
	return Mod_FindBSPXDecoupledLM (file, filelen, layout, records);
}

/* Keep lightgrid discovery independent of the DECOUPLED_LM status. A map with
 * only LIGHTGRID_OCTREE still takes the ordinary IBSP face and hunk route. */
static int Mod_FindBSPXLightgrid (const byte *file, size_t filelen,
	const r1bsp_disk_layout_t *layout, const byte **data, size_t *length)
{
	return Mod_FindBSPXLump (file, filelen, layout, "LIGHTGRID_OCTREE",
		data, length, NULL);
}

#define LIGHTGRID_OCCLUDED 0x40000000u
#define LIGHTGRID_LEAF 0x80000000u
#define MAX_LIGHTGRID_BYTES (32u * 1024u * 1024u)
#define MAX_LIGHTGRID_DEPTH 256

/* Validate the reachable octree before filling split points, using point[0]
 * as a temporary visited bit. Shared internal nodes and cycles are rejected. */
static int Mod_ValidateLightgridNode (rlightgrid_t *grid, uint32 nodenum,
	unsigned depth)
{
	rlightgrid_node_t *node;
	int i;

	if (nodenum & LIGHTGRID_OCCLUDED)
		return 1;
	if (nodenum & LIGHTGRID_LEAF)
		return (nodenum & ~LIGHTGRID_LEAF) < grid->numleafs;
	if (nodenum >= grid->numnodes || depth >= MAX_LIGHTGRID_DEPTH)
		return 0;
	node = &grid->nodes[nodenum];
	if (node->point[0])
		return 0;
	node->point[0] = 1;
	for (i = 0; i < 8; i++)
		if (!Mod_ValidateLightgridNode (grid, node->children[i], depth + 1))
			return 0;
	return 1;
}

/* A malformed BSPX header or table is rejected by Mod_ForName before the hunk.
 * A malformed optional grid within a valid directory falls back to BSP face
 * sampling. Decode variable-length samples into fixed style-255-padded slots;
 * one allocation owns the grid and all arrays, so map unload frees it once. */
static rlightgrid_t *Mod_LoadLightgrid (const byte *file, size_t filelen,
	const r1bsp_disk_layout_t *layout)
{
	const byte *data, *record;
	size_t length, pos;
	uint32 count, leaf_count, root;
	uint32 dimensions[3];
	float scale[3], mins[3], spacing;
	uint64 fixed_bytes, max_voxels, voxels, total_voxels = 0;
	uint64 decoded_bytes;
	unsigned numstyles, style_count;
	rlightgrid_t *grid = NULL;
	byte *storage;
	int i, j, status;

	status = Mod_FindBSPXLightgrid (file, filelen, layout, &data, &length);
	if (!status)
		return NULL;
	if (status < 0 || length < 49)
		goto invalid;
	for (i = 0; i < 3; i++)
	{
		spacing = R1BSP_ReadFloat (data + i * 4);
		if (!(spacing > 0 && spacing <= FLT_MAX))
			goto invalid;
		scale[i] = 1.0f / spacing;
		if (!(scale[i] > 0 && scale[i] <= FLT_MAX))
			goto invalid;
		dimensions[i] = R1BSP_ReadU32 (data + 12 + i * 4);
		if (!dimensions[i])
			goto invalid;
		mins[i] = R1BSP_ReadFloat (data + 24 + i * 4);
		if (!(mins[i] >= -FLT_MAX && mins[i] <= FLT_MAX))
			goto invalid;
	}
	numstyles = data[36];
	if (!numstyles || numstyles > MAXLIGHTMAPS)
		goto invalid;
	root = R1BSP_ReadU32 (data + 37);
	count = R1BSP_ReadU32 (data + 41);
	pos = 45;
	if (count > (length - pos) / 44u)
		goto invalid;
	pos += (size_t)count * 44u;
	if (length - pos < 4)
		goto invalid;
	leaf_count = R1BSP_ReadU32 (data + pos);
	pos += 4;
	if (!leaf_count || leaf_count > (length - pos) / 24u)
		goto invalid;
	fixed_bytes = sizeof (*grid) + (uint64)count * sizeof (rlightgrid_node_t) +
		(uint64)leaf_count * sizeof (rlightgrid_leaf_t);
	if (fixed_bytes >= MAX_LIGHTGRID_BYTES)
		goto invalid;
	max_voxels = (MAX_LIGHTGRID_BYTES - fixed_bytes) /
		((uint64)numstyles * sizeof (rlightgrid_sample_t));
	for (i = 0; i < (int)leaf_count; i++)
	{
		uint32 leaf_size[3], leaf_min[3];

		if (length - pos < 24)
			goto invalid;
		for (j = 0; j < 3; j++)
		{
			leaf_min[j] = R1BSP_ReadU32 (data + pos + j * 4);
			leaf_size[j] = R1BSP_ReadU32 (data + pos + 12 + j * 4);
			if (!leaf_size[j] || leaf_min[j] >= dimensions[j] ||
				leaf_size[j] > dimensions[j] - leaf_min[j])
				goto invalid;
		}
		pos += 24;
		voxels = 1;
		for (j = 0; j < 3; j++)
		{
			if (voxels > max_voxels - total_voxels ||
				leaf_size[j] > (max_voxels - total_voxels) / voxels)
				goto invalid;
			voxels *= leaf_size[j];
		}
		total_voxels += voxels;
		while (voxels--)
		{
			if (pos >= length)
				goto invalid;
			style_count = data[pos++];
			if (style_count == 255)
				continue;
			if (style_count > numstyles ||
				(size_t)style_count > (length - pos) / sizeof (rlightgrid_sample_t))
				goto invalid;
			pos += (size_t)style_count * sizeof (rlightgrid_sample_t);
		}
	}
	decoded_bytes = fixed_bytes + total_voxels * numstyles * sizeof (rlightgrid_sample_t);
	grid = malloc ((size_t)decoded_bytes);
	if (!grid)
		goto invalid;
	memset (grid, 0, (size_t)decoded_bytes);
	storage = (byte *)(grid + 1);
	grid->nodes = (rlightgrid_node_t *)storage;
	storage += (size_t)count * sizeof (rlightgrid_node_t);
	grid->leafs = (rlightgrid_leaf_t *)storage;
	storage += (size_t)leaf_count * sizeof (rlightgrid_leaf_t);
	grid->samples = (rlightgrid_sample_t *)storage;
	memset (grid->samples, 255,
		(size_t)(total_voxels * numstyles * sizeof (rlightgrid_sample_t)));
	grid->rootnode = root;
	grid->numnodes = count;
	grid->numleafs = leaf_count;
	grid->numsamples = (uint32)total_voxels;
	grid->numstyles = numstyles;
	for (i = 0; i < 3; i++)
	{
		grid->scale[i] = scale[i];
		grid->size[i] = dimensions[i];
		grid->mins[i] = mins[i];
	}
	pos = 45;
	for (i = 0; i < (int)count; i++)
	{
		rlightgrid_node_t *node = &grid->nodes[i];
		for (j = 0; j < 8; j++)
			node->children[j] = R1BSP_ReadU32 (data + pos + 12 + j * 4);
		pos += 44;
	}
	if (!Mod_ValidateLightgridNode (grid, root, 0))
		goto invalid;
	pos = 45;
	for (i = 0; i < (int)count; i++)
	{
		rlightgrid_node_t *node = &grid->nodes[i];
		for (j = 0; j < 3; j++)
		{
			node->point[j] = R1BSP_ReadU32 (data + pos + j * 4);
			if (node->point[j] > dimensions[j])
				goto invalid;
		}
		pos += 44;
	}
	pos += 4;
	total_voxels = 0;
	for (i = 0; i < (int)leaf_count; i++)
	{
		rlightgrid_leaf_t *leaf = &grid->leafs[i];
		uint64 n;

		for (j = 0; j < 3; j++)
		{
			leaf->mins[j] = R1BSP_ReadU32 (data + pos + j * 4);
			leaf->size[j] = R1BSP_ReadU32 (data + pos + 12 + j * 4);
		}
		leaf->firstsample = (uint32)(total_voxels * numstyles);
		leaf->numsamples = leaf->size[0] * leaf->size[1] * leaf->size[2];
		total_voxels += leaf->numsamples;
		pos += 24;
		for (n = 0; n < leaf->numsamples; n++)
		{
			rlightgrid_sample_t *sample;

			style_count = data[pos++];
			if (style_count == 255)
				continue;
			sample = grid->samples + leaf->firstsample + n * numstyles;
			record = data + pos;
			memcpy (sample, record,
				(size_t)style_count * sizeof (rlightgrid_sample_t));
			pos += (size_t)style_count * sizeof (rlightgrid_sample_t);
		}
	}
	return grid;

invalid:
	free (grid);
	ri.Con_Printf (PRINT_ALL, "Ignoring invalid or unavailable LIGHTGRID_OCTREE in %s\n",
		loadmodel->name);
	return NULL;
}

const rlightgrid_sample_t *Mod_LookupLightgrid (const rlightgrid_t *grid,
	const uint32 point[3])
{
	uint32 nodenum, local[3];
	uint64 index, sample_offset;
	const rlightgrid_node_t *node;
	const rlightgrid_leaf_t *leaf;
	unsigned depth;
	int i;

	if (!grid || !point || !grid->numleafs || !grid->numstyles ||
		grid->numstyles > MAXLIGHTMAPS ||
		!grid->leafs || !grid->samples || (grid->numnodes && !grid->nodes))
		return NULL;
	for (i = 0; i < 3; i++)
		if (point[i] >= grid->size[i])
			return NULL;
	nodenum = grid->rootnode;
	for (depth = 0; depth <= MAX_LIGHTGRID_DEPTH; depth++)
	{
		if (nodenum & LIGHTGRID_OCCLUDED)
			return NULL;
		if (nodenum & LIGHTGRID_LEAF)
		{
			if ((nodenum & ~LIGHTGRID_LEAF) >= grid->numleafs)
				return NULL;
			leaf = &grid->leafs[nodenum & ~LIGHTGRID_LEAF];
			for (i = 0; i < 3; i++)
			{
				if (point[i] < leaf->mins[i])
					return NULL;
				local[i] = point[i] - leaf->mins[i];
				if (local[i] >= leaf->size[i])
					return NULL;
			}
			index = ((uint64)local[2] * leaf->size[1] + local[1]) *
					leaf->size[0] +
				local[0];
			if (index >= leaf->numsamples)
				return NULL;
			sample_offset = leaf->firstsample + index * grid->numstyles;
			if (sample_offset + grid->numstyles >
				(uint64)grid->numsamples * grid->numstyles)
				return NULL;
			return grid->samples + (size_t)sample_offset;
		}
		if (nodenum >= grid->numnodes)
			return NULL;
		node = &grid->nodes[nodenum];
		nodenum = node->children[(point[0] >= node->point[0]) << 2 |
			(point[1] >= node->point[1]) << 1 |
			(point[2] >= node->point[2])];
	}
	return NULL;
}

/* Only IBSP maps with recognized decoupled records take this stricter route.
 * The legacy no-BSPX loader and its admission behavior are unchanged. */
static int Mod_CheckIBSPBSPXCommon (const byte *file,
	const r1bsp_disk_layout_t *layout)
{
	const byte *in;
	uint32 first, count, texinfo;
	int i, j, index;
	float value;

	if (!layout->count[LUMP_PLANES] || !layout->count[LUMP_VERTEXES] ||
		!layout->count[LUMP_FACES] || !layout->count[LUMP_TEXINFO] ||
		!layout->count[LUMP_EDGES] || !layout->count[LUMP_SURFEDGES])
		return 0;
	for (i = 0; i < (int)layout->count[LUMP_VERTEXES]; i++)
	{
		in = file + layout->fileofs[LUMP_VERTEXES] + (size_t)i * sizeof (dvertex_t);
		for (j = 0; j < 3; j++)
		{
			value = R1BSP_ReadFloat (in + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				return i + 1;
		}
	}
	for (i = 0; i < (int)layout->count[LUMP_PLANES]; i++)
	{
		in = file + layout->fileofs[LUMP_PLANES] + (size_t)i * sizeof (dplane_t);
		for (j = 0; j < 4; j++)
		{
			value = R1BSP_ReadFloat (in + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				return i + 1;
		}
	}
	for (i = 0; i < (int)layout->count[LUMP_TEXINFO]; i++)
	{
		in = file + layout->fileofs[LUMP_TEXINFO] + (size_t)i * sizeof (texinfo_t);
		for (j = 0; j < 8; j++)
		{
			value = R1BSP_ReadFloat (in + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				return i + 1;
		}
		if (!memchr (in + offsetof (texinfo_t, texture), 0,
				sizeof (((texinfo_t *)0)->texture)))
			return i + 1;
		index = R1BSP_ReadI32 (in + offsetof (texinfo_t, nexttexinfo));
		if (index >= (int)layout->count[LUMP_TEXINFO])
			return i + 1;
	}
	for (i = 0; i < (int)layout->count[LUMP_EDGES]; i++)
	{
		in = file + layout->fileofs[LUMP_EDGES] + (size_t)i * sizeof (dedge_t);
		for (j = 0; j < 2; j++)
		{
			index = (int)in[j * 2] | ((int)in[j * 2 + 1] << 8);
			if ((uint32)index >= layout->count[LUMP_VERTEXES])
				return i + 1;
		}
	}
	for (i = 0; i < (int)layout->count[LUMP_SURFEDGES]; i++)
	{
		in = file + layout->fileofs[LUMP_SURFEDGES] + (size_t)i * 4u;
		index = R1BSP_ReadI32 (in);
		if (index == INT_MIN ||
			(uint32)(index < 0 ? -index : index) >= layout->count[LUMP_EDGES])
			return i + 1;
	}
	for (i = 0; i < (int)layout->count[LUMP_FACES]; i++)
	{
		in = file + layout->fileofs[LUMP_FACES] + (size_t)i * sizeof (dface_t);
		index = (int)in[0] | ((int)in[1] << 8);
		if ((uint32)index >= layout->count[LUMP_PLANES] ||
			((uint32)in[2] | ((uint32)in[3] << 8)) > 1)
			return i + 1;
		first = R1BSP_ReadU32 (in + 4);
		count = (uint32)in[8] | ((uint32)in[9] << 8);
		texinfo = (uint32)in[10] | ((uint32)in[11] << 8);
		if (first > layout->count[LUMP_SURFEDGES] || count < 3 ||
			count > 4096 || count > layout->count[LUMP_SURFEDGES] - first ||
			texinfo >= layout->count[LUMP_TEXINFO])
			return i + 1;
	}
	return -1;
}

/* Check recognized records before reserving the map hunk. The face loader
 * repeats its checks against decoded geometry before creating GL lightmaps. */
static int Mod_BSPXAxesValid (const float axis[2][4], const float normal[3])
{
	double normal2 = 0, projection, tangent[2][3];
	double gram00 = 0, gram01 = 0, gram11 = 0;
	int j, k;

	for (j = 0; j < 3; j++)
		normal2 += (double)normal[j] * normal[j];
	if (!(normal2 >= 1e-12 && normal2 <= 1e12))
		return -1;
	for (j = 0; j < 2; j++)
	{
		projection = 0;
		for (k = 0; k < 3; k++)
			projection += (double)axis[j][k] * normal[k];
		projection /= normal2;
		for (k = 0; k < 3; k++)
			tangent[j][k] = axis[j][k] - projection * normal[k];
	}
	for (k = 0; k < 3; k++)
	{
		gram00 += tangent[0][k] * tangent[0][k];
		gram01 += tangent[0][k] * tangent[1][k];
		gram11 += tangent[1][k] * tangent[1][k];
	}
	return gram00 >= 1e-16 && gram11 >= 1e-16 &&
		gram00 * gram11 - gram01 * gram01 > gram00 * gram11 * 1e-6;
}

static int Mod_CheckBSPXRecords (const byte *file,
	const r1bsp_disk_layout_t *layout, const byte *records)
{
	const byte *face, *lm, *tex, *plane, *surf, *edge, *vertex;
	uint32 planenum, texinfo, width, height, flags, first, numedges;
	int i, j, k, styleofs, lightofs, maps, edgeindex, vertindex;
	int lit, use_decoupled;
	float axis[2][4], value;
	float normal[3];

	size_t samples, stride;

	stride = layout->extended ? sizeof (qbsp_dface_t) : sizeof (dface_t);
	styleofs = layout->extended ? 20 : 12;
	for (i = 0; i < (int)layout->count[LUMP_FACES]; i++)
	{
		face = file + layout->fileofs[LUMP_FACES] + (size_t)i * stride;
		lm = records + (size_t)i * 40u;
		planenum = layout->extended ? R1BSP_ReadU32 (face) :
									  ((uint32)face[0] | ((uint32)face[1] << 8));
		texinfo = layout->extended ? R1BSP_ReadU32 (face + 16) :
									 ((uint32)face[10] | ((uint32)face[11] << 8));
		if (planenum >= layout->count[LUMP_PLANES] ||
			texinfo >= layout->count[LUMP_TEXINFO])
			return i;
		tex = file + layout->fileofs[LUMP_TEXINFO] +
			(size_t)texinfo * sizeof (texinfo_t);
		flags = R1BSP_ReadU32 (tex + offsetof (texinfo_t, flags));
		lit = !(flags & (SURF_WARP | SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_DRAWSKY | SURF_DRAWTURB));
		lightofs = R1BSP_ReadI32 (lm + 4);
		if (lightofs < -1 || (lightofs >= 0 && (uint32)lightofs >= layout->filelen[LUMP_LIGHTING]))
			return i;
		width = (uint32)lm[0] | ((uint32)lm[1] << 8);
		height = (uint32)lm[2] | ((uint32)lm[3] << 8);
		if (lightofs >= 0 && (!width || !height))
			return i;
		if ((width && height) &&
			(width > MAX_BSPX_LIGHTMAP_SIDE ||
				height > MAX_BSPX_LIGHTMAP_SIDE ||
				width * height > MAX_BSPX_LIGHTMAP_PIXELS))
			return i;
		for (j = 0; j < 8; j++)
		{
			value = R1BSP_ReadFloat (lm + 8 + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				return i;
			axis[j / 4][j % 4] = value;
		}
		if (lightofs >= 0)
		{
			for (maps = 0; maps < MAXLIGHTMAPS &&
				 face[styleofs + maps] != 255;
				 maps++)
				;
			if (!maps)
				return i;
			samples = (size_t)width * height * 3u * maps;
			if (samples > (size_t)layout->filelen[LUMP_LIGHTING] -
					(size_t)lightofs)
				return i;
		}
		/* Only BSPX samples can light liquids and translucent faces. Vanilla
		 * face light offsets for those surfaces are not reliable. */
		use_decoupled = width && height && (lit || (lightofs >= 0 && !(flags & SURF_SKY)));
		if (!use_decoupled)
			continue;
		plane = file + layout->fileofs[LUMP_PLANES] +
			(size_t)planenum * sizeof (dplane_t);
		for (j = 0; j < 3; j++)
			normal[j] = R1BSP_ReadFloat (plane + j * 4);
		if (Mod_BSPXAxesValid (axis, normal) <= 0)
			return i;
		first = R1BSP_ReadU32 (face + (layout->extended ? 8 : 4));
		numedges = layout->extended ? R1BSP_ReadU32 (face + 12) :
									  ((uint32)face[8] | ((uint32)face[9] << 8));
		if (first > layout->count[LUMP_SURFEDGES] ||
			numedges > layout->count[LUMP_SURFEDGES] - first)
			return i;
		for (j = 0; j < (int)numedges; j++)
		{
			surf = file + layout->fileofs[LUMP_SURFEDGES] +
				(size_t)(first + j) * 4u;
			edgeindex = R1BSP_ReadI32 (surf);
			if (edgeindex == INT_MIN ||
				(uint32)(edgeindex < 0 ? -edgeindex : edgeindex) >=
					layout->count[LUMP_EDGES])
				return i;
			edge = file + layout->fileofs[LUMP_EDGES] +
				(size_t)(edgeindex < 0 ? -edgeindex : edgeindex) *
					(layout->extended ? sizeof (qbsp_dedge_t) : sizeof (dedge_t));
			k = edgeindex < 0 ? 1 : 0;
			vertindex = layout->extended ?
				(int)R1BSP_ReadU32 (edge + k * 4) :
				(int)((uint32)edge[k * 2] | ((uint32)edge[k * 2 + 1] << 8));
			if (vertindex < 0 || (uint32)vertindex >= layout->count[LUMP_VERTEXES])
				return i;
			vertex = file + layout->fileofs[LUMP_VERTEXES] +
				(size_t)vertindex * sizeof (dvertex_t);
			for (k = 0; k < 2; k++)
			{
				value = R1BSP_ReadFloat (vertex) * axis[k][0] +
					R1BSP_ReadFloat (vertex + 4) * axis[k][1] +
					R1BSP_ReadFloat (vertex + 8) * axis[k][2] + axis[k][3];
				if (!(value >= -1 && value <= (k ? height : width)))
					return i;
			}
		}
	}
	return -1;
}

/* Some N64 maps retain valid ordinary light offsets on liquid/alpha faces.
 * Follow the donor's map-level marker instead of trusting a stray flag in a
 * vanilla map, where those offsets are often broken. */
static int Mod_N64SampledLightmaps (lump_t *l, qboolean qbsp)
{
	const byte *face = mod_base + l->fileofs;
	uint32 texinfo;
	int i, marked = 0;
	int stride = qbsp ? sizeof (qbsp_dface_t) : sizeof (dface_t);

	if (!l->filelen || l->filelen % stride)
		return false;
	for (i = 0; i < l->filelen / stride; i++, face += stride)
	{
		texinfo = qbsp ? R1BSP_ReadU32 (face + 16) :
						 (uint32)face[10] | ((uint32)face[11] << 8);
		if (texinfo >= (uint32)loadmodel->numtexinfo)
			return false;
		if (loadmodel->texinfo[texinfo].flags &
			(SURF_N64_UV | SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y))
			if (++marked > 100)
				return true;
	}
	return false;
}

/* The bounded face route also decodes classic IBSP faces when BSPX lighting or
 * N64 sampled special faces are present. Other IBSP maps keep the old path. */
static void Mod_LoadQBSPFaces (lump_t *l, int lighting_size,
	const byte *lm_records, qboolean qbsp)
{
	const byte *base = mod_base + l->fileofs;
	const byte *in, *lm;
	msurface_t *out;
	uint32 plane, side, numedges, texinfo, width, height;
	int32 firstedge, lightofs;
	int i, j, k, lit, use_decoupled, edge, vertex;
	int special_atlas_side, min_side = 0;
	qboolean special_lightmaps, special_face_lightmap, n64_sampled;
	int stride = qbsp ? sizeof (qbsp_dface_t) : sizeof (dface_t);
	int styles_ofs = qbsp ? 20 : 12;
	int lightofs_ofs = qbsp ? 24 : 16;
	int count = l->filelen / stride;
	int bmins[2], bmaxs[2], smax, tmax, maps;
	float mins[2], maxs[2], value, dec_axis[2][4];
	int axes_status;
	size_t samples;

	if (count < 1)
		ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: empty faces in %s", loadmodel->name);
	n64_sampled = !lm_records && Mod_N64SampledLightmaps (l, qbsp);
	/* Validate every face before lightmap or polygon construction. */
	for (i = 0; i < count; i++)
	{
		in = base + (size_t)i * stride;
		plane = qbsp ? R1BSP_ReadU32 (in) :
					   (uint32)in[0] | ((uint32)in[1] << 8);
		side = qbsp ? R1BSP_ReadU32 (in + 4) :
					  (uint32)in[2] | ((uint32)in[3] << 8);
		firstedge = R1BSP_ReadI32 (in + (qbsp ? 8 : 4));
		numedges = qbsp ? R1BSP_ReadU32 (in + 12) :
						  (uint32)in[8] | ((uint32)in[9] << 8);
		texinfo = qbsp ? R1BSP_ReadU32 (in + 16) :
						 (uint32)in[10] | ((uint32)in[11] << 8);
		lm = lm_records ? lm_records + (size_t)i * 40u : NULL;
		lightofs = lm ? R1BSP_ReadI32 (lm + 4) :
						R1BSP_ReadI32 (in + lightofs_ofs);
		if (plane >= (uint32)loadmodel->numplanes || side > 1 ||
			texinfo >= (uint32)loadmodel->numtexinfo || firstedge < 0 ||
			numedges < 3 || numedges > 4096 ||
			(uint32)firstedge > (uint32)loadmodel->numsurfedges ||
			numedges > (uint32)loadmodel->numsurfedges - (uint32)firstedge)
			ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d has bad geometry references in %s",
				i, loadmodel->name);
		if ((loadmodel->texinfo[texinfo].flags & SURF_WARP) && numedges > 60)
			ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: warp face %d exceeds 60 edges in %s",
				i, loadmodel->name);
		lit = !(loadmodel->texinfo[texinfo].flags &
			(SURF_WARP | SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_DRAWSKY | SURF_DRAWTURB));
		use_decoupled = 0;
		if (lm)
		{
			width = (uint32)lm[0] | ((uint32)lm[1] << 8);
			height = (uint32)lm[2] | ((uint32)lm[3] << 8);
			use_decoupled = width && height && (lit || (lightofs >= 0 && !(loadmodel->texinfo[texinfo].flags & SURF_SKY)));
			for (j = 0; j < 8; j++)
			{
				value = R1BSP_ReadFloat (lm + 8 + j * 4);
				if (!(value >= -1000000 && value <= 1000000))
					ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d has unsafe BSPX axis in %s",
						i, loadmodel->name);
				dec_axis[j / 4][j % 4] = value;
			}
			if (lightofs >= 0 && (!width || !height))
				ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d has missing BSPX lightmap dimensions in %s",
					i, loadmodel->name);
			if (width && height)
			{
				if (width > MAX_BSPX_LIGHTMAP_SIDE ||
					height > MAX_BSPX_LIGHTMAP_SIDE ||
					width * height > MAX_BSPX_LIGHTMAP_PIXELS)
					ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d has unsupported BSPX lightmap size in %s",
						i, loadmodel->name);
			}
			if (use_decoupled)
			{
				axes_status = Mod_BSPXAxesValid (dec_axis, loadmodel->planes[plane].normal);
				if (axes_status < 0)
					ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d has invalid BSPX plane in %s",
						i, loadmodel->name);
				if (!axes_status)
					ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d has degenerate BSPX axes in %s",
						i, loadmodel->name);
			}
		}
		mins[0] = mins[1] = 999999;
		maxs[0] = maxs[1] = -99999;
		for (j = 0; j < (int)numedges; j++)
		{
			edge = loadmodel->surfedges[firstedge + j];
			vertex = edge >= 0 ? loadmodel->edges[edge].v[0] :
								 loadmodel->edges[-edge].v[1];
			for (k = 0; k < 2; k++)
			{
				float *point = loadmodel->vertexes[vertex].position;
				float *vec = loadmodel->texinfo[texinfo].vecs[k];
				value = point[0] * vec[0] + point[1] * vec[1] +
					point[2] * vec[2] + vec[3];
				if (!(value >= -32000 && value <= 32000))
					ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d has unsafe texture coordinates in %s",
						i, loadmodel->name);
				if (value < mins[k])
					mins[k] = value;
				if (value > maxs[k])
					maxs[k] = value;
				if (use_decoupled)
				{
					value = point[0] * dec_axis[k][0] +
						point[1] * dec_axis[k][1] +
						point[2] * dec_axis[k][2] + dec_axis[k][3];
					if (!(value >= -1 && value <= (k ? height : width)))
						ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d exceeds BSPX lightmap axes in %s",
							i, loadmodel->name);
				}
			}
		}
		if (lm && (use_decoupled || lightofs >= 0))
		{
			smax = (int)width;
			tmax = (int)height;
		}
		else
		{
			for (k = 0; k < 2; k++)
			{
				bmins[k] = (int)floor (mins[k] / 16);
				bmaxs[k] = (int)ceil (maxs[k] / 16);
				if (bmins[k] * 16 < SHRT_MIN || bmaxs[k] * 16 > SHRT_MAX ||
					(bmaxs[k] - bmins[k]) * 16 > SHRT_MAX)
					ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d has unrepresentable extents in %s",
						i, loadmodel->name);
			}
			smax = bmaxs[0] - bmins[0] + 1;
			tmax = bmaxs[1] - bmins[1] + 1;
		}
		if (lit && (smax > (lm ? MAX_BSPX_LIGHTMAP_SIDE : 128) || tmax > (lm ? MAX_BSPX_LIGHTMAP_SIDE : 128) || smax * tmax > (lm ? MAX_BSPX_LIGHTMAP_PIXELS : 867)))
			ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d exceeds lightmap capacity in %s",
				i, loadmodel->name);
		if (lightofs < -1 || (lightofs >= 0 && lightofs >= lighting_size))
			ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d has bad light offset in %s",
				i, loadmodel->name);
		if (lightofs >= 0)
		{
			for (maps = 0; maps < MAXLIGHTMAPS && in[styles_ofs + maps] != 255; maps++)
				;
			if (!maps)
				ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d has no light style in %s",
					i, loadmodel->name);
			samples = (size_t)smax * tmax * 3 * maps;
			if (samples > (size_t)lighting_size - (size_t)lightofs)
				ri.Sys_Error (ERR_DROP, "Mod_LoadQBSPFaces: face %d has truncated light samples in %s",
					i, loadmodel->name);
		}
		/* Opaque fullbright faces also require atlas space. Optional liquid
		 * and alpha lightmaps retain their existing allocation fallback. */
		if (lit && !(mod_bspx_directory_present && (loadmodel->texinfo[texinfo].flags & (SURF_NODRAW | SURF_SKY)) == SURF_NODRAW))
		{
			if (smax > min_side)
				min_side = smax;
			if (tmax > min_side)
				min_side = tmax;
		}
	}

	out = Hunk_Alloc (count * sizeof (*out));
	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;
	currentmodel = loadmodel;
	GL_BeginBuildingLightmaps (lm_records != NULL, min_side);
	/* The legacy draw path cannot use sampled liquid/alpha lightmaps. Reserve
	 * BSPX and marked N64 atlas space only with the matching shader path. */
	special_lightmaps = (lm_records || n64_sampled) && GL_ImageShaderIntensity () &&
		GL_SurfaceShaderAvailable () && qglMTexCoord2fSGIS &&
		qglMTexCoord2fvSGIS && qglGetTexEnviv && qglGetBooleanv;
	special_atlas_side = GL_LightmapBlockSize ();
	for (i = 0; i < count; i++, out++)
	{
		in = base + (size_t)i * stride;
		out->firstedge = R1BSP_ReadI32 (in + (qbsp ? 8 : 4));
		out->numedges = qbsp ? (int)R1BSP_ReadU32 (in + 12) :
							   (int)((uint32)in[8] | ((uint32)in[9] << 8));
		out->flags = (qbsp ? R1BSP_ReadU32 (in + 4) :
							 ((uint32)in[2] | ((uint32)in[3] << 8))) ?
			SURF_PLANEBACK :
			0;
		out->visframe = 0;
		out->polys = NULL;
		out->texturechain = NULL;
		out->lightmapchain = NULL;
		out->light_s = out->light_t = 0;
		out->lightmaptexturenum = 0;
		out->lm_decoupled = false;
		out->dlight_s = out->dlight_t = 0;
		out->dlightframe = out->dlightbits = 0;
		out->plane = loadmodel->planes + (qbsp ? R1BSP_ReadU32 (in) : ((uint32)in[0] | ((uint32)in[1] << 8)));
		out->texinfo = loadmodel->texinfo + (qbsp ? R1BSP_ReadU32 (in + 16) : ((uint32)in[10] | ((uint32)in[11] << 8)));
		lm = lm_records ? lm_records + (size_t)i * 40u : NULL;
		lightofs = lm ? R1BSP_ReadI32 (lm + 4) :
						R1BSP_ReadI32 (in + lightofs_ofs);
		lit = !(out->texinfo->flags &
			(SURF_WARP | SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_DRAWSKY | SURF_DRAWTURB));
		use_decoupled = lm && (lm[0] || lm[1]) && (lm[2] || lm[3]) &&
			(lit || (lightofs >= 0 && !(out->texinfo->flags & SURF_SKY)));
		if (!use_decoupled)
			CalcSurfaceExtents (out);
		if (n64_sampled && !lm && lightofs >= 0 &&
			!(out->texinfo->flags & SURF_SKY) &&
			(out->texinfo->flags & (SURF_WARP | SURF_TRANS33 | SURF_TRANS66)))
		{
			/* Reuse the bounded optional sampled path with classic 16-unit
			 * lightmap axes; base UV scaling never changes these coordinates. */
			out->lm_decoupled = true;
			for (j = 0; j < 2; j++)
			{
				for (k = 0; k < 3; k++)
					out->lm_axis[j][k] = out->texinfo->vecs[j][k] * (1.0f / 16);
				out->lm_axis[j][3] =
					(out->texinfo->vecs[j][3] - out->texturemins[j]) * (1.0f / 16);
			}
		}
		if (use_decoupled)
		{
			out->lm_decoupled = true;
			out->texturemins[0] = out->texturemins[1] = 0;
			out->extents[0] = (short)((((uint32)lm[0] | ((uint32)lm[1] << 8)) - 1u) * 16u);
			out->extents[1] = (short)((((uint32)lm[2] | ((uint32)lm[3] << 8)) - 1u) * 16u);
			for (j = 0; j < 8; j++)
				out->lm_axis[j / 4][j % 4] = R1BSP_ReadFloat (lm + 8 + j * 4);
		}
		memcpy (out->styles, in + styles_ofs, MAXLIGHTMAPS);
		out->samples = lightofs == -1 ? NULL : loadmodel->lightdata + lightofs;
		if (mod_bspx_directory_present &&
			(out->texinfo->flags & (SURF_NODRAW | SURF_SKY)) == SURF_NODRAW)
		{
			out->flags |= SURF_BSPX_NODRAW;
			continue;
		}
		/* Opaque faces reserve atlases before optional BSPX/N64 liquid and
		 * alpha faces can consume the remaining budget. */
		if (lit)
			GL_CreateSurfaceLightmap (out);
	}
	for (i = 0, out = loadmodel->surfaces; i < count; i++, out++)
	{
		if (out->flags & SURF_BSPX_NODRAW)
			continue;
		/* A valid BSPX face can still be larger than this GPU's selected
		 * atlas. Keep its classic base-only draw instead of rejecting the map. */
		special_face_lightmap = out->lm_decoupled && out->samples &&
			special_lightmaps &&
			(out->texinfo->flags & (SURF_WARP | SURF_TRANS33 | SURF_TRANS66)) &&
			(out->extents[0] >> 4) + 1 <= special_atlas_side &&
			(out->extents[1] >> 4) + 1 <= special_atlas_side;
		if (out->texinfo->flags & SURF_WARP)
		{
			/* Build the sampled BSPX lightmap while its real extents are
			 * intact; the legacy warp flag suppresses atlas allocation. */
			if (special_face_lightmap)
				GL_TryCreateSurfaceLightmap (out);
			out->flags |= SURF_DRAWTURB;
			if (!out->lm_decoupled || !out->samples)
			{
				for (j = 0; j < 2; j++)
				{
					out->extents[j] = 16384;
					out->texturemins[j] = -8192;
				}
			}
			GL_SubdivideSurface (out);
		}
		else
		{
			if (special_face_lightmap)
				GL_TryCreateSurfaceLightmap (out);
			GL_BuildPolygonFromSurface (out);
		}
	}
	GL_EndBuildingLightmaps ();
}

/* Charge 32-byte-rounded requests so the same reserve covers Win32 and Linux.
 * Win32's page-tail loss is less than the next request, hence the 2x margin. */
static int Mod_QBSPChargeHunk (uint64 *total, uint64 requested)
{
	uint64 rounded;

	if (requested > INT_MAX)
		return 0;
	rounded = (requested + 31u) & ~(uint64)31u;
	if (rounded > (64u * 1024u * 1024u - 1024u * 1024u) / 2u - *total)
		return 0;
	*total += rounded;
	return 1;
}

static void Mod_CheckQBSPCommon (const byte *file,
	const r1bsp_disk_layout_t *layout, const char *name, int *reserve)
{
	const byte *in, *vis, *face, *surf, *vertex, *record;
	bsp38_node_graph_error_t graph_error;
	bsp38_node_graph_status_t graph_status;
	uint64 hunk_bytes, polygon_count, polygon_bytes;
	uint32 clusters, offset, run, vis_work, first, span, faces_edges;
	uint32 numedges, texinfo, vertindex, warp_checks;
	float value, mins[3], maxs[3], warp_points[32][3];
	double cross, plane_normal[3], plane_dist, plane_length, distance;
	int i, j, k, next, step, row, decoded, header_size, edge, axis, split, split_total;
	int shared_visible;
	int u, v, positive, negative, noncollinear;
	static const int required[] = { LUMP_PLANES, LUMP_VERTEXES,
		LUMP_NODES, LUMP_TEXINFO, LUMP_FACES, LUMP_LEAFS,
		LUMP_EDGES, LUMP_SURFEDGES, LUMP_MODELS };

	for (i = 0; i < (int)(sizeof (required) / sizeof (required[0])); i++)
		if (!layout->count[required[i]])
			Mod_QBSPReject (file, name, "empty required QBSP lump", required[i]);
	if (layout->count[LUMP_MODELS] > MAX_MODELS - 2)
		Mod_QBSPReject (file, name, "too many QBSP inline models", -1);

	for (i = 0; i < (int)layout->count[LUMP_VERTEXES]; i++)
	{
		in = file + layout->fileofs[LUMP_VERTEXES] + (size_t)i * sizeof (dvertex_t);
		for (j = 0; j < 3; j++)
		{
			value = R1BSP_ReadFloat (in + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				Mod_QBSPReject (file, name, "unsafe QBSP vertex", i);
		}
	}
	for (i = 0; i < (int)layout->count[LUMP_PLANES]; i++)
	{
		in = file + layout->fileofs[LUMP_PLANES] + (size_t)i * sizeof (dplane_t);
		for (j = 0; j < 4; j++)
		{
			value = R1BSP_ReadFloat (in + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				Mod_QBSPReject (file, name, "unsafe QBSP plane", i);
		}
		if (R1BSP_ReadU32 (in + offsetof (dplane_t, type)) > PLANE_ANYZ)
			Mod_QBSPReject (file, name, "bad QBSP plane type", i);
	}
	for (i = 0; i < (int)layout->count[LUMP_TEXINFO]; i++)
	{
		in = file + layout->fileofs[LUMP_TEXINFO] + (size_t)i * sizeof (texinfo_t);
		for (j = 0; j < 8; j++)
		{
			value = R1BSP_ReadFloat (in + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				Mod_QBSPReject (file, name, "unsafe QBSP texinfo", i);
		}
		if (!memchr (in + offsetof (texinfo_t, texture), 0, sizeof (((texinfo_t *)0)->texture)))
			Mod_QBSPReject (file, name, "unterminated QBSP texture", i);
		next = R1BSP_ReadI32 (in + offsetof (texinfo_t, nexttexinfo));
		if (next >= (int)layout->count[LUMP_TEXINFO])
			Mod_QBSPReject (file, name, "bad QBSP texinfo link", i);
	}
	if (!R1BSP_ValidTextureAnimations (
			file + layout->fileofs[LUMP_TEXINFO], layout->count[LUMP_TEXINFO]))
		Mod_QBSPReject (file, name, "invalid QBSP texture animation graph", -1);

	vis = file + layout->fileofs[LUMP_VISIBILITY];
	clusters = 0;
	vis_work = 0;
	if (layout->filelen[LUMP_VISIBILITY])
	{
		if (layout->filelen[LUMP_VISIBILITY] < 4)
			Mod_QBSPReject (file, name, "short QBSP visibility", -1);
		clusters = R1BSP_ReadU32 (vis);
		if (clusters > MAX_MAP_LEAFS || clusters > (UINT_MAX - 4) / 8)
			Mod_QBSPReject (file, name, "bad QBSP visibility clusters", -1);
		header_size = 4 + (int)clusters * 8;
		if (header_size > (int)layout->filelen[LUMP_VISIBILITY])
			Mod_QBSPReject (file, name, "short QBSP visibility table", -1);
		row = ((int)clusters + 7) >> 3;
		for (i = 0; i < (int)clusters; i++)
		{
			for (j = 0; j < 2; j++)
			{
				offset = R1BSP_ReadU32 (vis + 4 + i * 8 + j * 4);
				if (offset < (uint32)header_size ||
					offset >= layout->filelen[LUMP_VISIBILITY])
					Mod_QBSPReject (file, name, "bad QBSP visibility offset", i);
				decoded = 0;
				while (decoded < row)
				{
					if (++vis_work > 16u * MAX_MAP_VISIBILITY)
						Mod_QBSPReject (file, name, "QBSP visibility decode work exceeds limit", i);
					if (offset >= layout->filelen[LUMP_VISIBILITY])
						Mod_QBSPReject (file, name, "truncated QBSP visibility", i);
					if (vis[offset++])
						decoded++;
					else
					{
						if (offset >= layout->filelen[LUMP_VISIBILITY])
							Mod_QBSPReject (file, name, "truncated QBSP visibility run", i);
						run = vis[offset++];
						if (!run || run > (uint32)(row - decoded))
							Mod_QBSPReject (file, name, "invalid QBSP visibility run", i);
						decoded += (int)run;
					}
				}
			}
		}
	}
	for (i = 0; i < (int)layout->count[LUMP_LEAFS]; i++)
	{
		in = file + layout->fileofs[LUMP_LEAFS] + (size_t)i * sizeof (qbsp_dleaf_t);
		offset = R1BSP_ReadU32 (in + offsetof (qbsp_dleaf_t, cluster));
		if (layout->filelen[LUMP_VISIBILITY] && offset != UINT_MAX && offset >= clusters)
			Mod_QBSPReject (file, name, "bad QBSP leaf cluster", i);
	}
	for (i = 0; i < (int)layout->count[LUMP_MODELS]; i++)
	{
		in = file + layout->fileofs[LUMP_MODELS] + (size_t)i * sizeof (dmodel_t);
		for (j = 0; j < 9; j++)
		{
			value = R1BSP_ReadFloat (in + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				Mod_QBSPReject (file, name, "unsafe QBSP submodel", i);
		}
		k = R1BSP_ReadI32 (in + offsetof (dmodel_t, headnode));
		next = R1BSP_ReadI32 (in + offsetof (dmodel_t, firstface));
		step = R1BSP_ReadI32 (in + offsetof (dmodel_t, numfaces));
		if (k < 0 || k >= (int)layout->count[LUMP_NODES] ||
			(i == 0 && k != 0) || next < 0 || step < 0 ||
			next > (int)layout->count[LUMP_FACES] ||
			step > (int)layout->count[LUMP_FACES] - next)
			Mod_QBSPReject (file, name, "bad QBSP submodel references", i);
	}
	/* These widened references are consumed after face/lightmap construction.
	 * Reject them now, before Hunk_Begin or any GL construction. */
	for (i = 0; i < (int)layout->count[LUMP_EDGES]; i++)
	{
		in = file + layout->fileofs[LUMP_EDGES] + (size_t)i * sizeof (qbsp_dedge_t);
		for (j = 0; j < 2; j++)
		{
			offset = R1BSP_ReadU32 (in + j * 4);
			if (offset > USHRT_MAX || offset >= layout->count[LUMP_VERTEXES])
				Mod_QBSPReject (file, name, "bad QBSP edge vertex", i);
		}
	}
	for (i = 0; i < (int)layout->count[LUMP_SURFEDGES]; i++)
	{
		in = file + layout->fileofs[LUMP_SURFEDGES] + (size_t)i * sizeof (int32);
		edge = R1BSP_ReadI32 (in);
		if (edge == INT_MIN ||
			(uint32)(edge < 0 ? -edge : edge) >= layout->count[LUMP_EDGES])
			Mod_QBSPReject (file, name, "bad QBSP surfedge", i);
	}
	for (i = 0; i < (int)layout->count[LUMP_LEAFFACES]; i++)
	{
		in = file + layout->fileofs[LUMP_LEAFFACES] + (size_t)i * sizeof (qbsp_dleafface_t);
		if (R1BSP_ReadU32 (in) >= layout->count[LUMP_FACES])
			Mod_QBSPReject (file, name, "bad QBSP leafface", i);
	}
	for (i = 0; i < (int)layout->count[LUMP_LEAFS]; i++)
	{
		in = file + layout->fileofs[LUMP_LEAFS] + (size_t)i * sizeof (qbsp_dleaf_t);
		if (R1BSP_ReadI32 (in) == -1)
			Mod_QBSPReject (file, name, "reserved QBSP leaf contents", i);
		offset = R1BSP_QBSPLeafCluster (in, layout->filelen[LUMP_VISIBILITY] != 0);
		if ((offset != UINT_MAX && offset >= MAX_MAP_LEAFS) ||
			R1BSP_ReadU32 (in + offsetof (qbsp_dleaf_t, area)) >= MAX_MAP_AREAS)
			Mod_QBSPReject (file, name, "bad QBSP leaf cluster or area", i);
		for (j = 0; j < 6; j++)
		{
			value = R1BSP_ReadFloat (in + offsetof (qbsp_dleaf_t, mins) + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				Mod_QBSPReject (file, name, "unsafe QBSP leaf bounds", i);
		}
		first = R1BSP_ReadU32 (in + offsetof (qbsp_dleaf_t, firstleafface));
		span = R1BSP_ReadU32 (in + offsetof (qbsp_dleaf_t, numleaffaces));
		if (first > layout->count[LUMP_LEAFFACES] ||
			span > layout->count[LUMP_LEAFFACES] - first)
			Mod_QBSPReject (file, name, "bad QBSP leaf face span", i);
		first = R1BSP_ReadU32 (in + offsetof (qbsp_dleaf_t, firstleafbrush));
		span = R1BSP_ReadU32 (in + offsetof (qbsp_dleaf_t, numleafbrushes));
		if (first > layout->count[LUMP_LEAFBRUSHES] ||
			span > layout->count[LUMP_LEAFBRUSHES] - first)
			Mod_QBSPReject (file, name, "bad QBSP leaf brush span", i);
	}
	for (i = 0; i < (int)layout->count[LUMP_NODES]; i++)
	{
		in = file + layout->fileofs[LUMP_NODES] + (size_t)i * sizeof (qbsp_dnode_t);
		if (R1BSP_ReadU32 (in) >= layout->count[LUMP_PLANES])
			Mod_QBSPReject (file, name, "bad QBSP node plane", i);
		for (j = 0; j < 6; j++)
		{
			value = R1BSP_ReadFloat (in + offsetof (qbsp_dnode_t, mins) + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				Mod_QBSPReject (file, name, "unsafe QBSP node bounds", i);
		}
		first = R1BSP_ReadU32 (in + offsetof (qbsp_dnode_t, firstface));
		span = R1BSP_ReadU32 (in + offsetof (qbsp_dnode_t, numfaces));
		if (first > USHRT_MAX || span > USHRT_MAX ||
			first > layout->count[LUMP_FACES] ||
			span > layout->count[LUMP_FACES] - first)
			Mod_QBSPReject (file, name, "bad QBSP node face span", i);
	}
	graph_error.node = graph_error.side = graph_error.child = -1;
	graph_status = BSP38_ValidateNodeGraphLayoutLimit (
		file + layout->fileofs[LUMP_NODES], layout->count[LUMP_NODES],
		layout->count[LUMP_LEAFS], sizeof (qbsp_dnode_t),
		offsetof (qbsp_dnode_t, children), 256, &graph_error);
	if (graph_status == BSP38_NODE_GRAPH_TOO_DEEP)
		Mod_QBSPReject (file, name, "QBSP node graph exceeds depth 256", graph_error.node);
	if (graph_status != BSP38_NODE_GRAPH_OK)
		Mod_QBSPReject (file, name, "bad QBSP node graph", graph_error.node);
	shared_visible = R1BSP_HasSharedVisibleQBSPLeaf (file, layout);
	if (shared_visible)
		Mod_QBSPReject (file, name, shared_visible > 0 ? "shared visible QBSP leaf" : "cannot validate QBSP leaf parents", -1);

	hunk_bytes = 0;
	if (!Mod_QBSPChargeHunk (&hunk_bytes, layout->filelen[LUMP_LIGHTING]) ||
		!Mod_QBSPChargeHunk (&hunk_bytes, layout->filelen[LUMP_VISIBILITY]) ||
		!Mod_QBSPChargeHunk (&hunk_bytes,
			(uint64)layout->count[LUMP_VERTEXES] * sizeof (mvertex_t)) ||
		!Mod_QBSPChargeHunk (&hunk_bytes,
			(uint64)layout->count[LUMP_EDGES] * sizeof (medge_t)) ||
		!Mod_QBSPChargeHunk (&hunk_bytes,
			(uint64)layout->count[LUMP_SURFEDGES] * sizeof (int)) ||
		!Mod_QBSPChargeHunk (&hunk_bytes,
			(uint64)layout->count[LUMP_PLANES] * 2 * sizeof (cplane_t)) ||
		!Mod_QBSPChargeHunk (&hunk_bytes,
			(uint64)layout->count[LUMP_TEXINFO] * sizeof (mtexinfo_t)) ||
		!Mod_QBSPChargeHunk (&hunk_bytes,
			(uint64)layout->count[LUMP_FACES] * sizeof (msurface_t)) ||
		!Mod_QBSPChargeHunk (&hunk_bytes,
			(uint64)layout->count[LUMP_LEAFFACES] * sizeof (msurface_t *)) ||
		!Mod_QBSPChargeHunk (&hunk_bytes,
			(uint64)layout->count[LUMP_LEAFS] * sizeof (mleaf_t)) ||
		!Mod_QBSPChargeHunk (&hunk_bytes,
			(uint64)layout->count[LUMP_NODES] * sizeof (mnode_t)) ||
		!Mod_QBSPChargeHunk (&hunk_bytes,
			(uint64)layout->count[LUMP_MODELS] * sizeof (mmodel_t)))
		Mod_QBSPReject (file, name, "QBSP hunk budget exceeds 64 MiB", -1);

	faces_edges = 0;
	warp_checks = 0;
	for (i = 0; i < (int)layout->count[LUMP_FACES]; i++)
	{
		face = file + layout->fileofs[LUMP_FACES] + (size_t)i * sizeof (qbsp_dface_t);
		first = R1BSP_ReadU32 (face + offsetof (qbsp_dface_t, firstedge));
		numedges = R1BSP_ReadU32 (face + offsetof (qbsp_dface_t, numedges));
		texinfo = R1BSP_ReadU32 (face + offsetof (qbsp_dface_t, texinfo));
		if (R1BSP_ReadU32 (face) >= layout->count[LUMP_PLANES] ||
			R1BSP_ReadU32 (face + offsetof (qbsp_dface_t, side)) > 1 ||
			texinfo >= layout->count[LUMP_TEXINFO] ||
			numedges < 3 || numedges > 4096 ||
			first > layout->count[LUMP_SURFEDGES] ||
			numedges > layout->count[LUMP_SURFEDGES] - first)
			Mod_QBSPReject (file, name, "bad QBSP face references", i);
		if (numedges > 4u * MAX_MAP_SURFEDGES - faces_edges)
			Mod_QBSPReject (file, name, "QBSP face edge work exceeds limit", i);
		faces_edges += numedges;
		record = file + layout->fileofs[LUMP_TEXINFO] +
			(size_t)texinfo * sizeof (texinfo_t);
		if (R1BSP_ReadU32 (record + offsetof (texinfo_t, flags)) & SURF_WARP)
		{
			if (numedges > 32 || numedges * numedges > 4u * MAX_MAP_SURFEDGES - warp_checks)
				Mod_QBSPReject (file, name, "QBSP warp complexity exceeds limit", i);
			warp_checks += numedges * numedges;
			for (axis = 0; axis < 3; axis++)
			{
				mins[axis] = FLT_MAX;
				maxs[axis] = -FLT_MAX;
			}
			for (j = 0; j < (int)numedges; j++)
			{
				surf = file + layout->fileofs[LUMP_SURFEDGES] +
					(size_t)(first + j) * sizeof (int32);
				edge = R1BSP_ReadI32 (surf);
				record = file + layout->fileofs[LUMP_EDGES] +
					(size_t)(edge < 0 ? -edge : edge) * sizeof (qbsp_dedge_t);
				for (k = 0; k < 2; k++)
				{
					vertindex = R1BSP_ReadU32 (record + k * 4);
					vertex = file + layout->fileofs[LUMP_VERTEXES] +
						(size_t)vertindex * sizeof (dvertex_t);
					for (axis = 0; axis < 3; axis++)
					{
						value = R1BSP_ReadFloat (vertex + axis * 4);
						if (k == (edge > 0 ? 0 : 1))
							warp_points[j][axis] = value;
						if (value < mins[axis])
							mins[axis] = value;
						if (value > maxs[axis])
							maxs[axis] = value;
					}
				}
			}
			record = file + layout->fileofs[LUMP_PLANES] +
				(size_t)R1BSP_ReadU32 (face) * sizeof (dplane_t);
			plane_length = 0;
			for (axis = 0; axis < 3; axis++)
			{
				plane_normal[axis] = R1BSP_ReadFloat (record + axis * 4);
				plane_length += plane_normal[axis] * plane_normal[axis];
			}
			plane_dist = R1BSP_ReadFloat (record + 12);
			if (!(plane_length >= 0.5 && plane_length <= 2.0))
				Mod_QBSPReject (file, name, "invalid QBSP warp plane", i);
			for (j = 0; j < (int)numedges; j++)
			{
				distance = -plane_dist;
				for (axis = 0; axis < 3; axis++)
					distance += plane_normal[axis] * warp_points[j][axis];
				if (fabs (distance) > 1.0)
					Mod_QBSPReject (file, name, "nonplanar QBSP warp face", i);
				for (k = 0; k < j; k++)
					if (warp_points[j][0] == warp_points[k][0] &&
						warp_points[j][1] == warp_points[k][1] &&
						warp_points[j][2] == warp_points[k][2])
						Mod_QBSPReject (file, name, "duplicate QBSP warp vertex", i);
			}
			/* Every axial cut of a convex projection adds at most two
			 * vertices. Check all three projections before subdivision. */
			noncollinear = 0;
			for (axis = 0; axis < 3; axis++)
			{
				u = (axis + 1) % 3;
				v = (axis + 2) % 3;
				for (j = 0; j < (int)numedges; j++)
				{
					positive = negative = 0;
					for (k = 0; k < (int)numedges; k++)
					{
						cross = ((double)warp_points[(j + 1) % numedges][u] - warp_points[j][u]) *
								((double)warp_points[k][v] - warp_points[j][v]) -
							((double)warp_points[(j + 1) % numedges][v] - warp_points[j][v]) *
								((double)warp_points[k][u] - warp_points[j][u]);
						if (cross > 0)
							positive = noncollinear = 1;
						if (cross < 0)
							negative = noncollinear = 1;
						if (positive && negative)
							Mod_QBSPReject (file, name, "nonconvex QBSP warp face", i);
					}
				}
			}
			if (!noncollinear)
				Mod_QBSPReject (file, name, "degenerate QBSP warp face", i);
			split_total = 0;
			polygon_count = 1;
			for (axis = 0; axis < 3; axis++)
			{
				split = (int)ceil ((maxs[axis] - mins[axis]) / 64.0f);
				if (split > 28 - split_total)
					Mod_QBSPReject (file, name, "QBSP warp span exceeds split limit", i);
				split_total += split;
				polygon_count *= (uint64)split + 1;
			}
			if (numedges + 2 * split_total > 60)
				Mod_QBSPReject (file, name, "QBSP warp exceeds vertex limit", i);
			/* Repeated or unusual edge walks can exceed the convex estimate.
			 * The runtime guard rejects >60 before allocation. */
			polygon_bytes = sizeof (glpoly_t) +
				(60 - 2) * VERTEXSIZE * sizeof (float);
		}
		else
		{
			polygon_count = 1;
			polygon_bytes = sizeof (glpoly_t) - VERTEXSIZE * sizeof (float) +
				(numedges - 3) * VERTEXSIZE * sizeof (float);
		}
		polygon_bytes = (polygon_bytes + 31u) & ~(uint64)31u;
		if (!Mod_QBSPChargeHunk (&hunk_bytes, polygon_count * polygon_bytes))
			Mod_QBSPReject (file, name, "QBSP hunk budget exceeds 64 MiB", i);
	}
	*reserve = (int)(2 * hunk_bytes + 1024u * 1024u);
}

/* Vanilla mist leaves can reference light-emitting or NODRAW sky faces that
 * should not contribute to the cubemap sky. Keep ordinary sky faces visible.
 * This follows Q2PRO's fake-sky rule, using the validated leaf/node spans. */
static void Mod_RemoveFakeSkyFaces (qboolean cubemaps)
{
	int i, j, k, face_index;
	mleaf_t *leaf;
	mnode_t *node;
	msurface_t *face;

	if (!cubemaps || mod_bspx_directory_present)
		return;
	for (i = 1; i < loadmodel->numleafs; i++)
	{
		leaf = loadmodel->leafs + i;
		if (!(leaf->contents & CONTENTS_MIST))
			continue;
		for (j = 0; j < leaf->nummarksurfaces; j++)
		{
			face = leaf->firstmarksurface[j];
			if (!(face->texinfo->flags & SURF_SKY) ||
				!(face->texinfo->flags & (SURF_LIGHT | SURF_NODRAW)) ||
				(face->flags & SURF_BSPX_NODRAW))
				continue;
			face_index = (int)(face - loadmodel->surfaces);
			face->flags |= SURF_BSPX_NODRAW;
			for (node = leaf->parent; node; node = node->parent)
				if (face_index >= node->firstsurface &&
					face_index - node->firstsurface < node->numsurfaces)
					break;
			if (!node)
				continue;
			for (k = 0; k < node->numsurfaces; k++)
			{
				face = loadmodel->surfaces + node->firstsurface + k;
				if ((face->texinfo->flags & SURF_SKY) &&
					(face->texinfo->flags & (SURF_LIGHT | SURF_NODRAW)))
					face->flags |= SURF_BSPX_NODRAW;
			}
		}
	}
}

static void Mod_RegisterInlineSkyFallbacks (model_t *mod)
{
	int i, j;
	for (i = 1; i < mod->numsubmodels; i++)
	{
		int first = mod->submodels[i].firstface;
		int count = mod->submodels[i].numfaces;
		if (first < 0 || count < 0 || first > mod->numsurfaces ||
			count > mod->numsurfaces - first)
			continue;
		for (j = first; j < first + count; j++)
		{
			mtexinfo_t *tex = mod->surfaces[j].texinfo;
			char name[MAX_QPATH];
			image_t *image;
			if (tex->sky_material == SKY_MATERIAL_DEFAULT || tex->sky_fallback_checked)
				continue;
			tex->sky_fallback_checked = true;
			Com_sprintf (name, sizeof (name), "textures/%s.wal", tex->sky_name);
			image = GL_FindWallImage (name, tex->sky_name, false);
			if (image)
				tex->image = image;
		}
	}
}

/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	int i, bad_lump = -1, qbsp, bspx_status;
	dheader_t header;
	mmodel_t 	*bm;
	r1bsp_disk_layout_t layout;
	const byte *lm_records = NULL;

	qbsp = R1BSP_ReadU32 ((const byte *)buffer) == IDQBSPHEADER;
	if (qbsp)
	{
		if (R1BSP_Preflight ((const byte *)buffer, (size_t)modfilelen, &layout) != R1BSP_DISK_OK)
			ri.Sys_Error (ERR_DROP, "Mod_LoadBrushModel: %s has an invalid QBSP header", mod->name);
		bspx_status = Mod_FindBSPXDecoupledLM ((const byte *)buffer,
			(size_t)modfilelen, &layout, &lm_records);
		if (bspx_status < 0)
			ri.Sys_Error (ERR_DROP, "Mod_LoadBrushModel: %s has an invalid BSPX directory or DECOUPLED_LM lump", mod->name);
		header.ident = IDQBSPHEADER;
		header.version = BSPVERSION;
		for (i = 0; i < MAX_LUMPS; i++)
		{
			header.lumps[i].fileofs = i == LUMP_POP ? 0 : (int)layout.fileofs[i];
			header.lumps[i].filelen = i == LUMP_POP ? 0 : (int)layout.filelen[i];
		}
	}
	else
	{
		if (Mod_DecodeBSP38Header (buffer, modfilelen, &header, &bad_lump) != MOD_BSP38_OK)
			ri.Sys_Error (ERR_DROP, "Mod_LoadBrushModel: %s has an invalid BSP38 header", mod->name);
		bspx_status = Mod_FindIBSPDecoupledLM ((const byte *)buffer,
			(size_t)modfilelen, &header, &layout, &lm_records);
		if (bspx_status < 0 || (bspx_status > 0 && R1BSP_Preflight ((const byte *)buffer, (size_t)modfilelen, &layout) != R1BSP_DISK_OK))
			ri.Sys_Error (ERR_DROP, "Mod_LoadBrushModel: %s has an invalid IBSP BSPX layout", mod->name);
	}

	loadmodel->type = mod_brush;
	loadmodel->has_bspx = mod_bspx_directory_present;
	if (loadmodel != mod_known)
		ri.Sys_Error (ERR_DROP, "Loaded a brush model after the world");

	mod_base = (byte *)buffer;

	// load into heap

	Mod_LoadVertexes (&header.lumps[LUMP_VERTEXES]);
	if (qbsp)
		Mod_LoadQBSPEdges (&header.lumps[LUMP_EDGES]);
	else
		Mod_LoadEdges (&header.lumps[LUMP_EDGES]);
	Mod_LoadSurfedges (&header.lumps[LUMP_SURFEDGES]);
	if (qbsp)
		Mod_CheckQBSPSurfedges ();
	Mod_LoadLighting (&header.lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header.lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header.lumps[LUMP_TEXINFO]);
	if (qbsp || lm_records ||
		Mod_N64SampledLightmaps (&header.lumps[LUMP_FACES], false))
	{
		Mod_LoadQBSPFaces (&header.lumps[LUMP_FACES],
			header.lumps[LUMP_LIGHTING].filelen, lm_records, qbsp);
		if (qbsp)
			Mod_LoadQBSPMarksurfaces (&header.lumps[LUMP_LEAFFACES]);
		else
			Mod_LoadMarksurfaces (&header.lumps[LUMP_LEAFFACES]);
	}
	else
	{
		Mod_LoadFaces (&header.lumps[LUMP_FACES],
			header.lumps[LUMP_LIGHTING].filelen);
		Mod_LoadMarksurfaces (&header.lumps[LUMP_LEAFFACES]);
	}
	Mod_LoadVisibility (&header.lumps[LUMP_VISIBILITY]);
	if (qbsp)
	{
		Mod_LoadQBSPLeafs (&header.lumps[LUMP_LEAFS], layout.count[LUMP_LEAFBRUSHES],
			header.lumps[LUMP_VISIBILITY].filelen != 0);
		Mod_LoadQBSPNodes (&header.lumps[LUMP_NODES]);
	}
	else
	{
		Mod_LoadLeafs (&header.lumps[LUMP_LEAFS]);
		Mod_LoadNodes (&header.lumps[LUMP_NODES]);
	}
	if (!mod_bspx_directory_present)
	{
		cvar_t *cubemaps = ri.Cvar_Get ("gl_cubemaps", "0", CVAR_ARCHIVE);
		float enabled = cubemaps ? cubemaps->value : 0;
		Mod_RemoveFakeSkyFaces (enabled >= -FLT_MAX && enabled <= FLT_MAX &&
			FLOAT_NE_ZERO (enabled));
	}
	Mod_LoadSubmodels (&header.lumps[LUMP_MODELS]);
	Mod_RegisterInlineSkyFallbacks (mod);
	mod->numframes = 2;		// regular and alternate animation
	
//
// set up the submodels
//
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		model_t	*starmod;

		bm = &mod->submodels[i];
		starmod = &mod_inline[i];

		*starmod = *loadmodel;
		
		starmod->firstmodelsurface = bm->firstface;
		starmod->nummodelsurfaces = bm->numfaces;
		starmod->firstnode = bm->headnode;
		if (starmod->firstnode >= loadmodel->numnodes)
			ri.Sys_Error (ERR_DROP, "Inline model %i has bad firstnode", i);

		FastVectorCopy (bm->maxs, starmod->maxs);
		FastVectorCopy (bm->mins, starmod->mins);
		starmod->radius = bm->radius;
	
		if (i == 0)
			*loadmodel = *starmod;

		starmod->numleafs = bm->visleafs;
	}
	/* Inline models share the brush hunk but never own the optional grid. The
	 * filesystem buffer is still valid until Mod_ForName returns. */
	if (mod->lightdata)
		mod->lightgrid = Mod_LoadLightgrid ((const byte *)buffer,
			(size_t)modfilelen, &layout);
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/


/* Private indexed MD2 geometry follows the Q2PRO src/refresh/models.c mesh
 * representation (Copyright (C) 2008 Andrey Nazarov; GPL version 2 or later).
 * Retain R1Q2 command UVs, order and CPU frames rather than importing its ABI.
 */
#define ALIAS_CACHE_MAX_BYTES (16u * 1024u * 1024u)
unsigned c_alias_mesh_bytes;

static qboolean Mod_AliasSection (const dmdl_t *header, unsigned offset,
	int count, unsigned element_size)
{
	return count >= 0 && offset >= sizeof (*header) && offset <= header->ofs_end &&
		(unsigned)count <= (header->ofs_end - offset) / element_size;
}

static qboolean Mod_AliasAddSize (unsigned *size, int count, unsigned element_size)
{
	if (count < 0 || (unsigned)count > (INT_MAX - *size) / element_size)
		return false;
	*size += count * element_size;
	return true;
}

static qboolean Mod_AliasCommands (const dmdl_t *header, const byte *buffer,
	unsigned *numverts, unsigned *numindices)
{
	const byte *order = buffer + header->ofs_glcmds;
	int remaining = header->num_glcmds;
	int count, index, i;
	float st[2];

	*numverts = *numindices = 0;
	while (remaining)
	{
		memcpy (&count, order, sizeof (count));
		count = LittleLong (count);
		order += sizeof (count);
		remaining--;
		if (!count)
			return true;
		if (count == (-2147483647 - 1))
			return false;
		if (count < 0)
			count = -count;
		if (count > remaining / 3)
			return false;
		*numverts += count;
		if (count > 2)
			*numindices += 3 * (count - 2);
		for (i = 0; i < count; i++, order += 3 * sizeof (int))
		{
			memcpy (st, order, sizeof (st));
			st[0] = LittleFloat (st[0]);
			st[1] = LittleFloat (st[1]);
			memcpy (&index, order + sizeof (st), sizeof (index));
			index = LittleLong (index);
			if (index < 0 || index >= header->num_xyz ||
				!(st[0] >= -FLT_MAX && st[0] <= FLT_MAX) ||
				!(st[1] >= -FLT_MAX && st[1] <= FLT_MAX))
				return false;
		}
		remaining -= 3 * count;
	}
	return false; // A bounded terminator is required by the retained draw paths.
}

/* The donor ignores the optional command section. For fallback only, keep
 * triangle corner order and signed ST coordinates without a half-texel bias.
 * Invalid triangles are skipped as in Q2PRO; valid command streams remain
 * authoritative, including models with unused, broken triangle indices. */
#define ALIAS_HUNK_MAX_BYTES (64u * 1024u * 1024u)
#define ALIAS_FILE_MAX_BYTES (128u * 1024u * 1024u)

static qboolean Mod_AliasTriangle (const dmdl_t *header, const byte *buffer,
	unsigned triangle, unsigned xyz[3], unsigned st[3])
{
	dtriangle_t value;
	int i;

	memcpy (&value, buffer + header->ofs_tris + triangle * sizeof (value), sizeof (value));
	for (i = 0; i < 3; i++)
	{
		xyz[i] = (unsigned short)LittleShort (value.index_xyz[i]);
		st[i] = (unsigned short)LittleShort (value.index_st[i]);
		if (xyz[i] >= (unsigned)header->num_xyz || st[i] >= (unsigned)header->num_st)
			return false;
	}
	return true;
}

static void Mod_AliasTriangleCommands (const dmdl_t *input, const byte *buffer,
	dmdl_t *output)
{
	unsigned i, j, xyz[3], st[3];
	int *order = (int *)((byte *)output + output->ofs_glcmds);
	dstvert_t value;
	float uv[2], scale_s = 1.0f / input->skinwidth, scale_t = 1.0f / input->skinheight;

	for (i = 0; i < (unsigned)input->num_tris; i++)
		if (Mod_AliasTriangle (input, buffer, i, xyz, st))
		{
			*order++ = 3;
			for (j = 0; j < 3; j++, order += 3)
			{
				memcpy (&value, buffer + input->ofs_st + st[j] * sizeof (value), sizeof (value));
				uv[0] = (short)LittleShort (value.s) * scale_s;
				uv[1] = (short)LittleShort (value.t) * scale_t;
				memcpy (order, uv, sizeof (uv));
				order[2] = xyz[j];
			}
		}
	*order = 0;
}

/* Match the donor radius before frame translation, using the vertices that
 * R1Q2 actually submits. Keep this Hunk data independent of optional batching. */
static void Mod_AliasShadowRadii (const dmdl_t *header, float *radii)
{
	byte used[MAX_VERTS] = { 0 };
	const int *order = (const int *)((const byte *)header + header->ofs_glcmds);
	const daliasframe_t *frame;
	double corner[3], value, radius;
	int count, i, j, axis;

	while ((count = *order++) != 0)
	{
		if (count < 0)
			count = -count;
		for (i = 0; i < count; i++, order += 3)
			if (count >= 3)
				used[order[2]] = 1;
	}
	for (i = 0; i < header->num_frames; i++)
	{
		frame = (const daliasframe_t *)((const byte *)header +
			header->ofs_frames + i * header->framesize);
		corner[0] = corner[1] = corner[2] = 0;
		for (j = 0; j < header->num_xyz; j++)
			if (used[j])
				for (axis = 0; axis < 3; axis++)
				{
					value = fabs ((double)frame->verts[j].v[axis] * frame->scale[axis]);
					if (value > corner[axis])
						corner[axis] = value;
				}
		radius = sqrt (corner[0] * corner[0] + corner[1] * corner[1] +
			corner[2] * corner[2]);
		/* Preserve accepted files whose finite scale exceeds a float radius. */
		radii[i] = radius <= FLT_MAX ? (float)radius : -1.0f;
	}
}

static void Mod_BuildAliasMesh (model_t *mod, dmdl_t *header,
	unsigned numverts, unsigned numindices)
{
	maliasmesh_t *mesh;
	const int *order;
	unsigned bytes, first = 0, used = 0;
	int count, i;
	qboolean fan;

	if (!numindices)
		return;
	if (numverts > ALIAS_MESH_MAX_VERTICES || numindices > ALIAS_MESH_MAX_INDICES)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "%s: indexed alias capacity exceeded; retaining strips/fans\n", mod->name);
		return;
	}
	bytes = sizeof (*mesh) + numverts * sizeof (maliasvertex_t) + numindices * sizeof (GLuint);
	if (bytes > ALIAS_CACHE_MAX_BYTES - c_alias_mesh_bytes)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "%s: alias cache limit reached; retaining strips/fans\n", mod->name);
		return;
	}
	mesh = malloc (bytes);
	if (!mesh)
	{
		ri.Con_Printf (PRINT_ALL, "%s: indexed alias allocation failed; retaining strips/fans\n", mod->name);
		return;
	}
	mesh->numverts = numverts;
	mesh->numindices = numindices;
	mesh->bytes = bytes;
	mesh->gpu_buffer = mesh->gpu_vertex_array = mesh->gpu_bytes = 0;
	mesh->gpu_failed = false;
	mesh->vertices = (maliasvertex_t *)(mesh + 1);
	mesh->indices = (GLuint *)(mesh->vertices + numverts);
	order = (const int *)((byte *)header + header->ofs_glcmds);
	while ((count = *order++) != 0)
	{
		fan = count < 0;
		if (fan)
			count = -count;
		for (i = 0; i < count; i++, order += 3)
		{
			mesh->vertices[first + i].index_xyz = order[2];
			memcpy (mesh->vertices[first + i].st, order, sizeof (mesh->vertices[0].st));
		}
		for (i = 2; i < count; i++)
		{
			mesh->indices[used++] = fan ? first : first + i - 2 + (i & 1);
			mesh->indices[used++] = first + i - 1 - (!fan && (i & 1));
			mesh->indices[used++] = first + i;
		}
		first += count;
	}
	mod->aliasmesh = mesh;
	c_alias_mesh_bytes += bytes;
}

static void Mod_FreeAliasMesh (model_t *mod)
{
	if (!mod->aliasmesh)
		return;
	GL_FreeAliasBuffer (mod->aliasmesh);
	c_alias_mesh_bytes -= mod->aliasmesh->bytes;
	free (mod->aliasmesh);
	mod->aliasmesh = NULL;
}

/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	unsigned int i, j, k;
	dmdl_t header, normalized;
	dmdl_t *pheader;
	dstvert_t value_st, *poutst;
	dtriangle_t value_tri, *pouttri;
	daliasframe_t *pinframe, *poutframe;
	int value_cmd, *poutcmd;
	unsigned required, hunk_bytes, numverts, numindices, offsets[4], sizes[4];
	unsigned repaired_normals = 0, triangles = 0, xyz[3], st[3];
	qboolean commands;
	float value;
	char *skin_name;

	if (modfilelen < (int)sizeof (header))
		ri.Sys_Error (ERR_DROP, "model %s has a truncated MD2 header", mod->name);
	memcpy (&header, buffer, sizeof (header));
#if Q_BIGENDIAN
	for (i = 0; i < sizeof (header) / 4; i++)
		((int *)&header)[i] = LittleLong (((int *)&header)[i]);
#endif
	if (header.ident != IDALIASHEADER || header.version != ALIAS_VERSION)
		ri.Sys_Error (ERR_DROP, "model %s has an invalid MD2 ident/version", mod->name);
	if (header.num_xyz <= 0 || header.num_st <= 0 || header.num_tris <= 0 || header.num_frames <= 0)
	{
		mod->type = mod_empty;
		mod->numframes = 0;
		mod->extradata = NULL;
		mod->extradatasize = 0;
		return;
	}
	/* Preserve valid legacy command output for positive, degenerate models.
	 * Other under-three XYZ/ST models follow the donor's empty-model rule. */
	if (header.num_xyz < 3 || header.num_st < 3)
	{
		normalized = header;
		normalized.ofs_end = modfilelen;
		if ((unsigned)modfilelen > ALIAS_FILE_MAX_BYTES || header.num_glcmds <= 0 ||
			(header.ofs_glcmds & 3) ||
			!Mod_AliasSection (&normalized, header.ofs_glcmds, header.num_glcmds, sizeof (int)) ||
			!Mod_AliasCommands (&normalized, buffer, &numverts, &numindices))
		{
			mod->type = mod_empty;
			mod->numframes = 0;
			mod->extradata = NULL;
			mod->extradatasize = 0;
			return;
		}
	}
	if (header.num_xyz <= 0 || header.num_xyz > MAX_VERTS || header.num_st <= 0 ||
		header.num_tris <= 0 || header.num_frames <= 0 ||
		header.num_skins < 0 || header.num_skins > MAX_MD2SKINS ||
		header.skinwidth <= 0 || header.skinheight <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has invalid MD2 counts/dimensions", mod->name);
	if (header.framesize < 0 || (header.framesize & 3) ||
		(unsigned)header.framesize < sizeof (daliasframe_t) - sizeof (dtrivertx_t) +
				header.num_xyz * sizeof (dtrivertx_t))
		ri.Sys_Error (ERR_DROP, "model %s has an invalid MD2 frame size", mod->name);
	if ((unsigned)modfilelen > ALIAS_FILE_MAX_BYTES)
		ri.Sys_Error (ERR_DROP, "model %s has invalid MD2 file size", mod->name);
	/* The donor ignores ofs_end and the optional command metadata. Validate
	 * used sections against the actual file, then derive a safe private end. */
	header.ofs_end = modfilelen;
	if (!Mod_AliasSection (&header, header.ofs_st, header.num_st, sizeof (dstvert_t)) ||
		!Mod_AliasSection (&header, header.ofs_tris, header.num_tris, sizeof (dtriangle_t)) ||
		!Mod_AliasSection (&header, header.ofs_frames, header.num_frames, header.framesize) ||
		(header.num_skins && !Mod_AliasSection (&header, header.ofs_skins, header.num_skins, MAX_SKINNAME)) ||
		(header.ofs_frames & 3))
		ri.Sys_Error (ERR_DROP, "model %s has invalid MD2 section offsets", mod->name);
	offsets[0] = header.ofs_skins;
	sizes[0] = header.num_skins * MAX_SKINNAME;
	offsets[1] = header.ofs_st;
	sizes[1] = header.num_st * sizeof (dstvert_t);
	offsets[2] = header.ofs_tris;
	sizes[2] = header.num_tris * sizeof (dtriangle_t);
	offsets[3] = header.ofs_frames;
	sizes[3] = header.num_frames * header.framesize;
	required = sizeof (header);
	for (i = 0; i < 4; i++)
		if (sizes[i] && offsets[i] + sizes[i] > required)
			required = offsets[i] + sizes[i];
	for (i = 0; i < 4; i++)
		for (j = 0; j < i; j++)
			if (sizes[i] && sizes[j] && offsets[i] < offsets[j] + sizes[j] &&
				offsets[j] < offsets[i] + sizes[i])
				ri.Sys_Error (ERR_DROP, "model %s has overlapping MD2 sections", mod->name);
	for (i = 0; i < (unsigned)header.num_skins; i++)
		if (!memchr ((byte *)buffer + header.ofs_skins + i * MAX_SKINNAME, 0, MAX_SKINNAME))
			ri.Sys_Error (ERR_DROP, "model %s has an unterminated MD2 skin name", mod->name);
	for (i = 0; i < (unsigned)header.num_frames; i++)
	{
		pinframe = (daliasframe_t *)((byte *)buffer + header.ofs_frames + i * header.framesize);
		for (j = 0; j < 3; j++)
		{
			value = LittleFloat (pinframe->scale[j]);
			if (!(value >= -FLT_MAX && value <= FLT_MAX))
				ri.Sys_Error (ERR_DROP, "model %s has a non-finite MD2 frame scale", mod->name);
			value = LittleFloat (pinframe->translate[j]);
			if (!(value >= -FLT_MAX && value <= FLT_MAX))
				ri.Sys_Error (ERR_DROP, "model %s has a non-finite MD2 frame origin", mod->name);
		}
	}

	commands = header.num_glcmds > 0 && !(header.ofs_glcmds & 3) &&
		Mod_AliasSection (&header, header.ofs_glcmds, header.num_glcmds, sizeof (int));
	if (commands)
	{
		for (i = 0; i < 4; i++)
			if (sizes[i] && header.ofs_glcmds < offsets[i] + sizes[i] &&
				offsets[i] < header.ofs_glcmds + header.num_glcmds * sizeof (int))
				commands = false;
		if (commands)
			commands = Mod_AliasCommands (&header, buffer, &numverts, &numindices);
		if (commands && header.ofs_glcmds + header.num_glcmds * sizeof (int) > required)
			required = header.ofs_glcmds + header.num_glcmds * sizeof (int);
	}
	if (!commands && (header.num_xyz < 3 || header.num_st < 3))
	{
		mod->type = mod_empty;
		mod->numframes = 0;
		mod->extradata = NULL;
		mod->extradatasize = 0;
		return;
	}
	normalized = header;
	required = (required + 3) & ~3u;
	if (!commands)
	{
		for (i = 0; i < (unsigned)header.num_tris; i++)
			if (Mod_AliasTriangle (&header, buffer, i, xyz, st))
				triangles++;
		if (!triangles)
			ri.Sys_Error (ERR_DROP, "model %s has no valid MD2 triangles", mod->name);
		/* Ten words per triangle (count plus three ST/index records), then 0. */
		normalized.num_glcmds = triangles * 10 + 1;
		required = (required + 3) & ~3u;
		normalized.ofs_glcmds = required;
		if (!Mod_AliasAddSize (&required, normalized.num_glcmds, sizeof (int)))
			ri.Sys_Error (ERR_DROP, "model %s has oversized MD2 triangle commands", mod->name);
		normalized.ofs_end = required;
		numverts = numindices = triangles * 3;
	}
	normalized.ofs_end = required;
	hunk_bytes = required;
	if (!Mod_AliasAddSize (&hunk_bytes, header.num_frames, sizeof (float)) ||
		hunk_bytes > ALIAS_HUNK_MAX_BYTES - 31)
		ri.Sys_Error (ERR_DROP, "model %s has oversized MD2 hunk data", mod->name);
	/* File gaps and fallback commands can change size under the same pathname.
	 * Validate first and never reuse the old cached exact-size reservation. */
	mod->extradata = Hunk_Begin ((hunk_bytes + 31) & ~31u, 0);
	pheader = Hunk_Alloc (hunk_bytes);
	mod->shadow_radii = (float *)((byte *)pheader + required);
	memcpy (pheader, &normalized, sizeof (*pheader));

	poutst = (dstvert_t *)((byte *)pheader + pheader->ofs_st);
	for (i = 0; i < (unsigned)header.num_st; i++)
	{
		memcpy (&value_st, (byte *)buffer + header.ofs_st + i * sizeof (value_st), sizeof (value_st));
		value_st.s = LittleShort (value_st.s);
		value_st.t = LittleShort (value_st.t);
		memcpy (&poutst[i], &value_st, sizeof (value_st));
	}
	pouttri = (dtriangle_t *)((byte *)pheader + pheader->ofs_tris);
	for (i = 0; i < (unsigned)header.num_tris; i++)
	{
		memcpy (&value_tri, (byte *)buffer + header.ofs_tris + i * sizeof (value_tri), sizeof (value_tri));
		for (j = 0; j < 3; j++)
		{
			value_tri.index_xyz[j] = LittleShort (value_tri.index_xyz[j]);
			value_tri.index_st[j] = LittleShort (value_tri.index_st[j]);
		}
		memcpy (&pouttri[i], &value_tri, sizeof (value_tri));
	}
	for (i = 0; i < (unsigned)header.num_frames; i++)
	{
		pinframe = (daliasframe_t *)((byte *)buffer + header.ofs_frames + i * header.framesize);
		poutframe = (daliasframe_t *)((byte *)pheader + pheader->ofs_frames + i * pheader->framesize);
		memcpy (poutframe->name, pinframe->name, sizeof(poutframe->name));
		for (j = 0; j < 3; j++)
		{
			poutframe->scale[j] = LittleFloat (pinframe->scale[j]);
			poutframe->translate[j] = LittleFloat (pinframe->translate[j]);
		}
		memcpy (poutframe->verts, pinframe->verts, header.num_xyz * sizeof (dtrivertx_t));
		for (k = 0; k < (unsigned)header.num_xyz; k++)
			if (poutframe->verts[k].lightnormalindex >= 162)
			{
				poutframe->verts[k].lightnormalindex = 0;
				repaired_normals++;
			}
	}
	if (repaired_normals)
		ri.Con_Printf (PRINT_DEVELOPER, "%s: normalized %u invalid MD2 normal indices\n", mod->name, repaired_normals);
	mod->type = mod_alias;
	poutcmd = (int *)((byte *)pheader + pheader->ofs_glcmds);
	if (commands)
		for (i = 0; i < (unsigned)header.num_glcmds; i++)
		{
			memcpy (&value_cmd, (byte *)buffer + header.ofs_glcmds + i * sizeof (int), sizeof (int));
			poutcmd[i] = LittleLong (value_cmd);
		}
	else
		Mod_AliasTriangleCommands (&header, buffer, pheader);
	Mod_AliasShadowRadii (pheader, mod->shadow_radii);
	if (header.num_skins)
		memcpy ((byte *)pheader + pheader->ofs_skins, (byte *)buffer + header.ofs_skins,
			header.num_skins * MAX_SKINNAME);
	for (i = 0; i < (unsigned)header.num_skins; i++)
	{
		skin_name = (char *)pheader + pheader->ofs_skins + i * MAX_SKINNAME;
		fast_strlwr (skin_name);
		mod->skins[i] = GL_FindImage (skin_name, skin_name, it_skin);
	}
	mod->mins[0] = mod->mins[1] = mod->mins[2] = -32;
	mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = 32;
	Mod_BuildAliasMesh (mod, pheader, numverts, numindices);
}

/* MD3 MODEL LOADING */

#define MD3_MAX_FILE_BYTES (128u * 1024u * 1024u)
#define MD3_MAX_HUNK_BYTES (64u * 1024u * 1024u)

typedef struct
{
	dmd3header_t header;
	dmd3mesh_t meshes[MD3_MAX_MESHES];
	unsigned mesh_offsets[MD3_MAX_MESHES];
	mmd3frame_t frames[MD3_MAX_FRAMES];
	vec3_t shadow_corners[MD3_MAX_FRAMES];
	unsigned hunk_bytes;
} md3preflight_t;

static unsigned Mod_MD3UInt (const void *source)
{
	uint32 value;
	memcpy (&value, source, sizeof (value));
	return (unsigned)LittleLong (value);
}

static float Mod_MD3Float (const void *source)
{
	float value;
	memcpy (&value, source, sizeof (value));
	return LittleFloat (value);
}

static qboolean Mod_MD3Finite (float value)
{
	return value >= -FLT_MAX && value <= FLT_MAX;
}

static qboolean Mod_MD3Section (unsigned offset, unsigned bytes,
	unsigned minimum, unsigned limit)
{
	return offset >= minimum && offset <= limit && bytes <= limit - offset;
}

static qboolean Mod_MD3Overlap (unsigned first, unsigned first_size,
	unsigned second, unsigned second_size)
{
	return first_size && second_size && first < second + second_size &&
		second < first + first_size;
}

static qboolean Mod_MD3AddStorage (unsigned *total, unsigned count,
	unsigned element_size)
{
	unsigned bytes;

	if (!count)
		return true;
	if (*total > MD3_MAX_HUNK_BYTES - 7)
		return false;
	if (element_size > (MD3_MAX_HUNK_BYTES - *total) / count)
		return false;
	bytes = count * element_size;
	if (bytes > MD3_MAX_HUNK_BYTES - *total - 7)
		return false;
	*total += (bytes + 7) & ~7u;
	return true;
}

static void *Mod_MD3Take (byte **cursor, unsigned count, unsigned element_size)
{
	void *result = *cursor;
	*cursor += (count * element_size + 7) & ~7u;
	return result;
}

static qboolean Mod_MD3SkinName (const char *name)
{
	const char *end, *part, *p;

	end = memchr (name, 0, MD3_MAX_PATH);
	if (!end || end == name || end - name >= MAX_QPATH)
		return false;
	part = name;
	for (p = name; p <= end; p++)
	{
		unsigned char c = (unsigned char)*p;
		if (p != end && (c < 32 || c == 127 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|'))
			return false;
		if (p == end || c == '/' || c == '\\')
		{
			if (p == part || (p - part == 1 && part[0] == '.') ||
				(p - part == 2 && part[0] == '.' && part[1] == '.'))
				return false;
			part = p + 1;
		}
	}
	return true;
}

static qboolean Mod_MD3Preflight (const byte *file, unsigned length,
	md3preflight_t *check)
{
	dmd3header_t *header = &check->header;
	unsigned frame_size, tag_size, mesh_offset, i, j, k, f;
	unsigned offsets[4], sizes[4], hunk_bytes = 7;
	dmd3frame_t disk_frame;
	dmd3mesh_t *mesh;
	dmd3coord_t disk_coord;
	const byte *base;
	double position, corner[3], radius;

	if (length < sizeof (*header) || length > MD3_MAX_FILE_BYTES)
		return false;
	memcpy (header, file, sizeof (*header));
	header->ident = Mod_MD3UInt (&header->ident);
	header->version = Mod_MD3UInt (&header->version);
	header->flags = Mod_MD3UInt (&header->flags);
	header->num_frames = Mod_MD3UInt (&header->num_frames);
	header->num_tags = Mod_MD3UInt (&header->num_tags);
	header->num_meshes = Mod_MD3UInt (&header->num_meshes);
	header->num_skins = Mod_MD3UInt (&header->num_skins);
	header->ofs_frames = Mod_MD3UInt (&header->ofs_frames);
	header->ofs_tags = Mod_MD3UInt (&header->ofs_tags);
	header->ofs_meshes = Mod_MD3UInt (&header->ofs_meshes);
	header->ofs_end = Mod_MD3UInt (&header->ofs_end);
	if (header->ident != MD3_IDENT || header->version != MD3_VERSION ||
		header->num_frames < 1 || header->num_frames > MD3_MAX_FRAMES ||
		header->num_tags > MD3_MAX_TAGS ||
		header->num_meshes < 1 || header->num_meshes > MD3_MAX_MESHES ||
		header->num_skins > MD3_MAX_SKINS)
		return false;
	frame_size = header->num_frames * sizeof (dmd3frame_t);
	tag_size = header->num_frames * header->num_tags * sizeof (dmd3tag_t);
	if (!Mod_MD3Section (header->ofs_frames, frame_size, sizeof (*header), length) ||
		(tag_size && !Mod_MD3Section (header->ofs_tags, tag_size, sizeof (*header), length)) ||
		!Mod_MD3Section (header->ofs_meshes, sizeof (dmd3mesh_t), sizeof (*header), length) ||
		(header->ofs_frames & 3) || (tag_size && (header->ofs_tags & 3)) ||
		(header->ofs_meshes & 3) ||
		Mod_MD3Overlap (header->ofs_frames, frame_size, header->ofs_tags, tag_size))
		return false;
	if (!Mod_MD3AddStorage (&hunk_bytes, 1, sizeof (mmd3model_t)) ||
		!Mod_MD3AddStorage (&hunk_bytes, header->num_frames, sizeof (mmd3frame_t)) ||
		!Mod_MD3AddStorage (&hunk_bytes, header->num_frames, sizeof (float)) ||
		!Mod_MD3AddStorage (&hunk_bytes, header->num_meshes, sizeof (mmd3mesh_t)))
		return false;

	for (i = 0; i < header->num_frames; i++)
	{
		mmd3frame_t *frame = &check->frames[i];
		memcpy (&disk_frame, file + header->ofs_frames + i * sizeof (disk_frame), sizeof (disk_frame));
		for (j = 0; j < 3; j++)
		{
			float low = Mod_MD3Float (&disk_frame.mins[j]);
			float high = Mod_MD3Float (&disk_frame.maxs[j]);
			frame->translate[j] = Mod_MD3Float (&disk_frame.translate[j]);
			if (!Mod_MD3Finite (low) || !Mod_MD3Finite (high) || low > high ||
				!Mod_MD3Finite (frame->translate[j]))
				return false;
			frame->mins[j] = FLT_MAX;
			frame->maxs[j] = -FLT_MAX;
			check->shadow_corners[i][j] = 0;
		}
		frame->radius = Mod_MD3Float (&disk_frame.radius);
		if (!Mod_MD3Finite (frame->radius) || frame->radius < 0)
			return false;
	}
	mesh_offset = header->ofs_meshes;
	for (i = 0; i < header->num_meshes; i++)
	{
		if (!Mod_MD3Section (mesh_offset, sizeof (dmd3mesh_t),
				header->ofs_meshes, length))
			return false;
		mesh = &check->meshes[i];
		check->mesh_offsets[i] = mesh_offset;
		memcpy (mesh, file + mesh_offset, sizeof (*mesh));
		mesh->ident = Mod_MD3UInt (&mesh->ident);
		mesh->flags = Mod_MD3UInt (&mesh->flags);
		mesh->num_frames = Mod_MD3UInt (&mesh->num_frames);
		mesh->num_skins = Mod_MD3UInt (&mesh->num_skins);
		mesh->num_verts = Mod_MD3UInt (&mesh->num_verts);
		mesh->num_tris = Mod_MD3UInt (&mesh->num_tris);
		mesh->ofs_indexes = Mod_MD3UInt (&mesh->ofs_indexes);
		mesh->ofs_skins = Mod_MD3UInt (&mesh->ofs_skins);
		mesh->ofs_tcs = Mod_MD3UInt (&mesh->ofs_tcs);
		mesh->ofs_verts = Mod_MD3UInt (&mesh->ofs_verts);
		mesh->meshsize = Mod_MD3UInt (&mesh->meshsize);
		if (mesh->ident != MD3_IDENT || mesh->num_frames != header->num_frames ||
			mesh->num_verts < 3 || mesh->num_verts > MD3_MAX_VERTICES ||
			mesh->num_tris < 1 || mesh->num_tris > MD3_MAX_DRAW_TRIANGLES ||
			mesh->num_skins > MD3_MAX_SKINS ||
			!memchr (mesh->name, 0, sizeof (mesh->name)) ||
			mesh->meshsize < sizeof (*mesh) || (mesh->meshsize & 3) ||
			mesh->meshsize > length - mesh_offset)
			return false;
		offsets[0] = mesh->ofs_indexes;
		sizes[0] = mesh->num_tris * 3 * sizeof (uint32);
		offsets[1] = mesh->ofs_skins;
		sizes[1] = mesh->num_skins * sizeof (dmd3skin_t);
		offsets[2] = mesh->ofs_tcs;
		sizes[2] = mesh->num_verts * sizeof (dmd3coord_t);
		offsets[3] = mesh->ofs_verts;
		sizes[3] = mesh->num_frames * mesh->num_verts * sizeof (dmd3vertex_t);
		for (j = 0; j < 4; j++)
		{
			if (!Mod_MD3Section (offsets[j], sizes[j],
					sizes[j] ? sizeof (*mesh) : 0, mesh->meshsize) ||
				(sizes[j] && (offsets[j] & 3)))
				return false;
			for (k = 0; k < j; k++)
				if (Mod_MD3Overlap (offsets[j], sizes[j], offsets[k], sizes[k]))
					return false;
		}
		if (!Mod_MD3AddStorage (&hunk_bytes, mesh->num_tris * 3, sizeof (unsigned)) ||
			!Mod_MD3AddStorage (&hunk_bytes, mesh->num_verts, sizeof (float[2])) ||
			!Mod_MD3AddStorage (&hunk_bytes, mesh->num_frames * mesh->num_verts,
				sizeof (mmd3vertex_t)) ||
			!Mod_MD3AddStorage (&hunk_bytes, mesh->num_skins, sizeof (image_t *)))
			return false;
		base = file + mesh_offset;
		for (j = 0; j < mesh->num_skins; j++)
			if (!Mod_MD3SkinName ((const char *)(base + mesh->ofs_skins +
					j * sizeof (dmd3skin_t))))
				return false;
		for (j = 0; j < mesh->num_verts; j++)
		{
			memcpy (&disk_coord, base + mesh->ofs_tcs + j * sizeof (disk_coord),
				sizeof (disk_coord));
			if (!Mod_MD3Finite (Mod_MD3Float (&disk_coord.st[0])) ||
				!Mod_MD3Finite (Mod_MD3Float (&disk_coord.st[1])))
				return false;
		}
		for (j = 0; j < mesh->num_tris * 3; j++)
			if (Mod_MD3UInt (base + mesh->ofs_indexes + j * sizeof (uint32)) >= mesh->num_verts)
				return false;
		for (f = 0; f < header->num_frames; f++)
		{
			mmd3frame_t *frame = &check->frames[f];
			for (j = 0; j < mesh->num_verts; j++)
			{
				const byte *vertex = base + mesh->ofs_verts +
					(f * mesh->num_verts + j) * sizeof (dmd3vertex_t);
				for (k = 0; k < 3; k++)
				{
					int16 raw;
					memcpy (&raw, vertex + k * sizeof (raw), sizeof (raw));
					position = (int16)LittleShort (raw) * (double)MD3_XYZ_SCALE;
					if (fabs (position) > check->shadow_corners[f][k])
						check->shadow_corners[f][k] = (float)fabs (position);
					position += frame->translate[k];
					if (position < -FLT_MAX || position > FLT_MAX)
						return false;
					if (position < frame->mins[k])
						frame->mins[k] = (float)position;
					if (position > frame->maxs[k])
						frame->maxs[k] = (float)position;
				}
			}
		}
		mesh_offset += mesh->meshsize;
	}
	if (Mod_MD3Overlap (header->ofs_frames, frame_size, header->ofs_meshes,
			mesh_offset - header->ofs_meshes) ||
		Mod_MD3Overlap (header->ofs_tags, tag_size, header->ofs_meshes,
			mesh_offset - header->ofs_meshes))
		return false;
	for (i = 0; i < header->num_frames; i++)
	{
		mmd3frame_t *frame = &check->frames[i];
		for (j = 0; j < 3; j++)
			corner[j] = fabs (frame->mins[j]) > fabs (frame->maxs[j]) ?
				fabs (frame->mins[j]) :
				fabs (frame->maxs[j]);
		radius = sqrt (corner[0] * corner[0] + corner[1] * corner[1] +
			corner[2] * corner[2]);
		if (radius > FLT_MAX)
			return false;
		frame->radius = (float)radius;
	}
	check->hunk_bytes = (hunk_bytes + 31) & ~31u;
	return check->hunk_bytes <= MD3_MAX_HUNK_BYTES;
}

void Mod_LoadMd3Model (model_t *mod, void *buffer)
{
	md3preflight_t check;
	dmd3header_t *header = &check.header;
	mmd3model_t *md3;
	byte *cursor, *raw;
	unsigned i, j, f, k;
	const byte *base;
	char skin_name[MD3_MAX_PATH];
	dmd3coord_t disk_coord;
	const dmd3mesh_t *disk_mesh;

	if (modfilelen < 0 || !Mod_MD3Preflight ((const byte *)buffer, (unsigned)modfilelen, &check))
	{
		ri.Sys_Error (ERR_DROP, "model %s has invalid MD3 data", mod->name);
		return;
	}
	mod->extradata = Hunk_Begin (check.hunk_bytes, 0);
	raw = Hunk_Alloc (check.hunk_bytes);
	memset (raw, 0, check.hunk_bytes);
	cursor = (byte *)(((size_t)raw + 7) & ~(size_t)7);
	md3 = Mod_MD3Take (&cursor, 1, sizeof (*md3));
	md3->numframes = header->num_frames;
	md3->nummeshes = header->num_meshes;
	md3->frames = Mod_MD3Take (&cursor, header->num_frames, sizeof (*md3->frames));
	mod->shadow_radii = Mod_MD3Take (&cursor, header->num_frames, sizeof (float));
	for (i = 0; i < header->num_frames; i++)
		mod->shadow_radii[i] = VectorLength (check.shadow_corners[i]);
	md3->meshes = Mod_MD3Take (&cursor, header->num_meshes, sizeof (*md3->meshes));
	memcpy (md3->frames, check.frames, header->num_frames * sizeof (*md3->frames));
	for (i = 0; i < header->num_meshes; i++)
	{
		mmd3mesh_t *mesh = &md3->meshes[i];
		disk_mesh = &check.meshes[i];
		base = (const byte *)buffer + check.mesh_offsets[i];
		mesh->numverts = disk_mesh->num_verts;
		mesh->numtris = disk_mesh->num_tris;
		mesh->numskins = disk_mesh->num_skins;
		mesh->indices = Mod_MD3Take (&cursor, disk_mesh->num_tris * 3,
			sizeof (*mesh->indices));
		mesh->st = Mod_MD3Take (&cursor, disk_mesh->num_verts, sizeof (*mesh->st));
		mesh->vertices = Mod_MD3Take (&cursor,
			disk_mesh->num_frames * disk_mesh->num_verts, sizeof (*mesh->vertices));
		if (mesh->numskins)
			mesh->skins = Mod_MD3Take (&cursor, mesh->numskins, sizeof (*mesh->skins));
		for (j = 0; j < disk_mesh->num_tris * 3; j++)
			mesh->indices[j] = Mod_MD3UInt (base + disk_mesh->ofs_indexes +
				j * sizeof (uint32));
		for (j = 0; j < disk_mesh->num_verts; j++)
		{
			memcpy (&disk_coord, base + disk_mesh->ofs_tcs + j * sizeof (disk_coord),
				sizeof (disk_coord));
			mesh->st[j][0] = Mod_MD3Float (&disk_coord.st[0]);
			mesh->st[j][1] = Mod_MD3Float (&disk_coord.st[1]);
		}
		for (f = 0; f < header->num_frames; f++)
			for (j = 0; j < disk_mesh->num_verts; j++)
			{
				const byte *source = base + disk_mesh->ofs_verts +
					(f * disk_mesh->num_verts + j) * sizeof (dmd3vertex_t);
				mmd3vertex_t *vertex = &mesh->vertices[f * disk_mesh->num_verts + j];
				for (k = 0; k < 3; k++)
				{
					int16 value;
					memcpy (&value, source + k * sizeof (value), sizeof (value));
					vertex->xyz[k] = (int16)LittleShort (value);
				}
				vertex->normal[0] = source[6];
				vertex->normal[1] = source[7];
			}
		for (j = 0; j < disk_mesh->num_skins; j++)
		{
			memcpy (skin_name, base + disk_mesh->ofs_skins + j * sizeof (dmd3skin_t), sizeof (skin_name));
			for (k = 0; skin_name[k]; k++)
				if (skin_name[k] == '\\')
					skin_name[k] = '/';
			fast_strlwr (skin_name);
			mesh->skins[j] = GL_FindImage (skin_name, skin_name, it_skin);
		}
	}
	mod->md3 = md3;
	mod->type = mod_md3;
	mod->numframes = md3->numframes;
	VectorCopy (md3->frames[0].mins, mod->mins);
	VectorCopy (md3->frames[0].maxs, mod->maxs);
	mod->radius = md3->frames[0].radius;
	for (f = 1; f < header->num_frames; f++)
	{
		for (k = 0; k < 3; k++)
		{
			if (md3->frames[f].mins[k] < mod->mins[k])
				mod->mins[k] = md3->frames[f].mins[k];
			if (md3->frames[f].maxs[k] > mod->maxs[k])
				mod->maxs[k] = md3->frames[f].maxs[k];
		}
		if (md3->frames[f].radius > mod->radius)
			mod->radius = md3->frames[f].radius;
	}
}

/* MD5 MODEL LOADING */
#define MD5_MAX_TEXT_BYTES (64u * 1024u * 1024u)
#define MD5_MAX_SCALE_BYTES (1024u * 1024u)
#define MD5_MAX_DATA_BYTES (64u * 1024u * 1024u)
#define MD5_TOKEN_BYTES 1024
#define MD5_MAX_SCALAR 1000000.0

typedef struct
{
	const char *data;
	size_t length, offset;
	char token[MD5_TOKEN_BYTES];
} md5text_t;

typedef struct
{
	char name[48];
	int parent;
	vec3_t pos;
	float orient[4];
} md5bind_t;

typedef struct
{
	char name[48];
	int parent, flags, start;
	qboolean scale_positions;
} md5hierarchy_t;

typedef struct
{
	size_t used;
} md5budget_t;

static qboolean Mod_MD5Finite (double value)
{
	return value >= -FLT_MAX && value <= FLT_MAX;
}

static void *Mod_MD5Alloc (md5budget_t *budget, size_t count, size_t size)
{
	size_t bytes;
	void *memory;

	if (!count)
		return NULL;
	if (!size || count > (MD5_MAX_DATA_BYTES - budget->used) / size)
		return NULL;
	bytes = count * size;
	memory = calloc (count, size);
	if (memory)
		budget->used += bytes;
	return memory;
}

static void Mod_FreeMD5 (mmd5model_t *md5)
{
	int i;

	if (!md5)
		return;
	if (md5->meshes)
		for (i = 0; i < md5->nummeshes; i++)
		{
			GL_FreeMD5Buffer (&md5->meshes[i]);
			free (md5->meshes[i].vertices);
			free (md5->meshes[i].indices);
			free (md5->meshes[i].weights);
		}
	free (md5->meshes);
	free (md5->frames);
	free (md5->skins);
	free (md5->bounds);
	free (md5);
}

/* The renderer FS loader does not promise a trailing NUL byte. */
static qboolean Mod_MD5ReadFile (const char *path, unsigned limit,
	char **data, size_t *length)
{
	void *raw = NULL;
	int size, loaded;

	*data = NULL;
	*length = 0;
	size = ri.FS_LoadFile (path, NULL);
	if (size < 0 || (unsigned)size > limit)
		return false;
	loaded = ri.FS_LoadFile (path, &raw);
	if (loaded < 0 || (unsigned)loaded > limit || !raw ||
		memchr (raw, 0, loaded))
	{
		if (raw)
			ri.FS_FreeFile (raw);
		return false;
	}
	*data = raw;
	*length = (size_t)loaded;
	return true;
}

/* Match Q2PRO's quoted token and C/C++ comment grammar without reading past FS data. */
static qboolean Mod_MD5Token (md5text_t *text)
{
	size_t n = 0;
	char c;

	text->token[0] = 0;
	for (;;)
	{
		while (text->offset < text->length &&
			(unsigned char)text->data[text->offset] <= 32)
			text->offset++;
		if (text->offset + 1 < text->length &&
			text->data[text->offset] == '/' &&
			text->data[text->offset + 1] == '/')
		{
			text->offset += 2;
			while (text->offset < text->length &&
				text->data[text->offset] != '\n')
				text->offset++;
			continue;
		}
		if (text->offset + 1 < text->length &&
			text->data[text->offset] == '/' &&
			text->data[text->offset + 1] == '*')
		{
			text->offset += 2;
			while (text->offset + 1 < text->length &&
				(text->data[text->offset] != '*' ||
					text->data[text->offset + 1] != '/'))
				text->offset++;
			if (text->offset + 1 >= text->length)
				return false;
			text->offset += 2;
			continue;
		}
		break;
	}
	if (text->offset >= text->length)
		return false;
	if (text->data[text->offset] == '"')
	{
		text->offset++;
		while (text->offset < text->length &&
			text->data[text->offset] != '"')
		{
			if (n + 1 >= sizeof (text->token))
				return false;
			text->token[n++] = text->data[text->offset++];
		}
		if (text->offset >= text->length)
			return false;
		text->offset++;
	}
	else
	{
		while (text->offset < text->length &&
			(unsigned char)text->data[text->offset] > 32)
		{
			c = text->data[text->offset++];
			if (n + 1 >= sizeof (text->token))
				return false;
			text->token[n++] = c;
		}
	}
	text->token[n] = 0;
	return true;
}

static qboolean Mod_MD5AtEnd (md5text_t *text)
{
	for (;;)
	{
		while (text->offset < text->length &&
			(unsigned char)text->data[text->offset] <= 32)
			text->offset++;
		if (text->offset + 1 < text->length &&
			text->data[text->offset] == '/' &&
			text->data[text->offset + 1] == '/')
		{
			text->offset += 2;
			while (text->offset < text->length &&
				text->data[text->offset] != '\n')
				text->offset++;
			continue;
		}
		if (text->offset + 1 < text->length &&
			text->data[text->offset] == '/' &&
			text->data[text->offset + 1] == '*')
		{
			text->offset += 2;
			while (text->offset + 1 < text->length &&
				(text->data[text->offset] != '*' ||
					text->data[text->offset + 1] != '/'))
				text->offset++;
			if (text->offset + 1 >= text->length)
				return false;
			text->offset += 2;
			continue;
		}
		return text->offset == text->length;
	}
}

static qboolean Mod_MD5Expect (md5text_t *text, const char *expected)
{
	return Mod_MD5Token (text) && !strcmp (text->token, expected);
}

static qboolean Mod_MD5Int (md5text_t *text, int minimum, int maximum,
	int *output)
{
	char *end;
	long value;

	if (!Mod_MD5Token (text) || !text->token[0])
		return false;
	errno = 0;
	value = strtol (text->token, &end, 10);
	if (errno || *end || value < minimum || value > maximum)
		return false;
	*output = (int)value;
	return true;
}

static qboolean Mod_MD5Float (md5text_t *text, float *output)
{
	char *end;
	double value;

	if (!Mod_MD5Token (text) || !text->token[0])
		return false;
	errno = 0;
	value = strtod (text->token, &end);
	if (errno || *end || !Mod_MD5Finite (value) ||
		value < -MD5_MAX_SCALAR || value > MD5_MAX_SCALAR)
		return false;
	*output = (float)value;
	return true;
}

static qboolean Mod_MD5Vector (md5text_t *text, vec3_t output)
{
	return Mod_MD5Expect (text, "(") &&
		Mod_MD5Float (text, &output[0]) &&
		Mod_MD5Float (text, &output[1]) &&
		Mod_MD5Float (text, &output[2]) &&
		Mod_MD5Expect (text, ")");
}

static qboolean Mod_MD5QuatW (float orient[4])
{
	double length = (double)orient[0] * orient[0] +
		(double)orient[1] * orient[1] + (double)orient[2] * orient[2];

	if (length > 1.0001)
		return false;
	orient[3] = length >= 1 ? 0 : (float)-sqrt (1 - length);
	return true;
}

static void Mod_MD5QuatRotate (const float q[4], const vec3_t input,
	vec3_t output)
{
	vec3_t uv, uuv;

	uv[0] = q[1] * input[2] - q[2] * input[1];
	uv[1] = q[2] * input[0] - q[0] * input[2];
	uv[2] = q[0] * input[1] - q[1] * input[0];
	uuv[0] = q[1] * uv[2] - q[2] * uv[1];
	uuv[1] = q[2] * uv[0] - q[0] * uv[2];
	uuv[2] = q[0] * uv[1] - q[1] * uv[0];
	output[0] = input[0] + 2 * (q[3] * uv[0] + uuv[0]);
	output[1] = input[1] + 2 * (q[3] * uv[1] + uuv[1]);
	output[2] = input[2] + 2 * (q[3] * uv[2] + uuv[2]);
}

static void Mod_MD5QuatMultiply (const float a[4], const float b[4],
	float output[4])
{
	output[0] = a[0] * b[3] + a[3] * b[0] + a[1] * b[2] - a[2] * b[1];
	output[1] = a[1] * b[3] + a[3] * b[1] + a[2] * b[0] - a[0] * b[2];
	output[2] = a[2] * b[3] + a[3] * b[2] + a[0] * b[1] - a[1] * b[0];
	output[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static qboolean Mod_MD5QuatNormalize (float q[4])
{
	double length = sqrt ((double)q[0] * q[0] + (double)q[1] * q[1] +
		(double)q[2] * q[2] + (double)q[3] * q[3]);
	int i;

	if (!(length > 0) || !Mod_MD5Finite (length))
		return false;
	for (i = 0; i < 4; i++)
		q[i] = (float)(q[i] / length);
	return true;
}

/* Smooth bind-pose normals across vertices at the same position, including UV seams. */
static qboolean Mod_MD5Normals (mmd5mesh_t *mesh, const md5bind_t *bind)
{
	vec3_t *positions = NULL, *normals = NULL;
	int *representative = NULL, *next = NULL, *buckets = NULL;
	int i, j, k, slot, found, index, chain;
	qboolean ok = false;

	if (!mesh->numverts)
		return true;
	positions = calloc (mesh->numverts, sizeof (*positions));
	normals = calloc (mesh->numverts, sizeof (*normals));
	representative = malloc (mesh->numverts * sizeof (*representative));
	next = malloc (mesh->numverts * sizeof (*next));
	buckets = malloc (8192 * sizeof (*buckets));
	if (!positions || !normals || !representative || !next || !buckets)
		goto done;
	for (i = 0; i < 8192; i++)
		buckets[i] = -1;
	for (i = 0; i < mesh->numverts; i++)
	{
		mmd5vertex_t *vertex = &mesh->vertices[i];
		unsigned hash = 2166136261u;
		for (j = 0; j < vertex->count; j++)
		{
			const mmd5weight_t *weight = &mesh->weights[vertex->start + j];
			vec3_t rotated;
			Mod_MD5QuatRotate (bind[weight->joint].orient, weight->pos, rotated);
			for (k = 0; k < 3; k++)
				positions[i][k] += weight->bias *
					(bind[weight->joint].pos[k] + rotated[k]);
		}
		for (k = 0; k < 3; k++)
		{
			uint32 bits;
			if (!Mod_MD5Finite (positions[i][k]))
				goto done;
			if (positions[i][k] == 0)
				positions[i][k] = 0; /* canonicalize negative zero */
			memcpy (&bits, &positions[i][k], sizeof (bits));
			hash = (hash ^ bits) * 16777619u;
		}
		slot = (int)(hash & 8191);
		found = -1;
		chain = 0;
		for (index = buckets[slot]; index >= 0; index = next[index])
		{
			if (++chain > 64)
				goto done;
			if (positions[index][0] == positions[i][0] &&
				positions[index][1] == positions[i][1] &&
				positions[index][2] == positions[i][2])
			{
				found = index;
				break;
			}
		}
		if (found >= 0)
			representative[i] = found;
		else
		{
			representative[i] = i;
			next[i] = buckets[slot];
			buckets[slot] = i;
		}
	}
	for (i = 0; i < mesh->numtris; i++)
	{
		const unsigned short *tri = &mesh->indices[i * 3];
		vec3_t a, b, normal;
		float dot, angle;

		VectorSubtract (positions[tri[2]], positions[tri[0]], a);
		VectorSubtract (positions[tri[1]], positions[tri[0]], b);
		if (!VectorNormalize (a) || !VectorNormalize (b))
			continue;
		CrossProduct (a, b, normal);
		if (!VectorNormalize (normal))
			continue;
		dot = DotProduct (a, b);
		if (dot < -1)
			dot = -1;
		if (dot > 1)
			dot = 1;
		angle = (float)acos (dot);
		for (j = 0; j < 3; j++)
			for (k = 0; k < 3; k++)
				normals[representative[tri[j]]][k] += normal[k] * angle;
	}
	for (i = 0; i < mesh->numverts; i++)
		if (representative[i] == i)
			VectorNormalize (normals[i]);
	for (i = 0; i < mesh->numverts; i++)
	{
		mmd5vertex_t *vertex = &mesh->vertices[i];
		for (j = 0; j < vertex->count; j++)
		{
			const mmd5weight_t *weight = &mesh->weights[vertex->start + j];
			float inverse[4];
			vec3_t rotated;
			const float *orient = bind[weight->joint].orient;

			inverse[0] = -orient[0];
			inverse[1] = -orient[1];
			inverse[2] = -orient[2];
			inverse[3] = orient[3];
			Mod_MD5QuatRotate (inverse, normals[representative[i]], rotated);
			for (k = 0; k < 3; k++)
				vertex->normal[k] += weight->bias * rotated[k];
		}
		for (k = 0; k < 3; k++)
			if (!Mod_MD5Finite (vertex->normal[k]))
				goto done;
	}
	ok = true;
done:
	free (positions);
	free (normals);
	free (representative);
	free (next);
	free (buckets);
	return ok;
}

static qboolean Mod_MD5ParseMesh (md5text_t *text, mmd5model_t *md5,
	md5budget_t *budget, md5bind_t *bind)
{
	unsigned char *seen = NULL;
	mmd5mesh_t *mesh;
	int i, j, k, index, numtris, influence_count;
	float sum;
	qboolean drawable = false;

	if (!Mod_MD5Expect (text, "MD5Version") ||
		!Mod_MD5Expect (text, "10") ||
		!Mod_MD5Expect (text, "commandline") ||
		!Mod_MD5Token (text) ||
		!Mod_MD5Expect (text, "numJoints") ||
		!Mod_MD5Int (text, 1, MD5_MAX_JOINTS, &md5->numjoints) ||
		!Mod_MD5Expect (text, "numMeshes") ||
		!Mod_MD5Int (text, 1, MD5_MAX_MESHES, &md5->nummeshes) ||
		!Mod_MD5Expect (text, "joints") || !Mod_MD5Expect (text, "{"))
		return false;
	for (i = 0; i < md5->numjoints; i++)
	{
		if (!Mod_MD5Token (text) || !text->token[0] ||
			strlen (text->token) >= sizeof (bind[i].name))
			return false;
		strcpy (bind[i].name, text->token);
		for (j = 0; j < i; j++)
			if (!strcmp (bind[i].name, bind[j].name))
				return false;
		if (!Mod_MD5Int (text, -1, i - 1, &bind[i].parent) ||
			!Mod_MD5Vector (text, bind[i].pos) ||
			!Mod_MD5Vector (text, bind[i].orient) ||
			!Mod_MD5QuatW (bind[i].orient) ||
			!Mod_MD5QuatNormalize (bind[i].orient))
			return false;
	}
	if (!Mod_MD5Expect (text, "}"))
		return false;
	md5->meshes = Mod_MD5Alloc (budget, md5->nummeshes, sizeof (*md5->meshes));
	if (!md5->meshes)
		return false;
	for (i = 0; i < md5->nummeshes; i++)
	{
		mesh = &md5->meshes[i];
		if (!Mod_MD5Expect (text, "mesh") || !Mod_MD5Expect (text, "{") ||
			!Mod_MD5Expect (text, "shader") || !Mod_MD5Token (text) ||
			!Mod_MD5Expect (text, "numverts") ||
			!Mod_MD5Int (text, 0, MD5_MAX_VERTICES, &mesh->numverts))
			goto fail;
		mesh->vertices = Mod_MD5Alloc (budget, mesh->numverts, sizeof (*mesh->vertices));
		if (mesh->numverts && !mesh->vertices)
			goto fail;
		seen = calloc (mesh->numverts ? mesh->numverts : 1, 1);
		if (!seen)
			goto fail;
		for (j = 0; j < mesh->numverts; j++)
		{
			mmd5vertex_t *vertex;
			int start, count;
			if (!Mod_MD5Expect (text, "vert") ||
				!Mod_MD5Int (text, 0, mesh->numverts - 1, &index) || seen[index] ||
				!Mod_MD5Expect (text, "("))
				goto fail;
			seen[index] = 1;
			vertex = &mesh->vertices[index];
			if (!Mod_MD5Float (text, &vertex->st[0]) ||
				!Mod_MD5Float (text, &vertex->st[1]) ||
				!Mod_MD5Expect (text, ")") ||
				!Mod_MD5Int (text, 0, MD5_MAX_WEIGHTS, &start) ||
				!Mod_MD5Int (text, 0, MD5_MAX_WEIGHTS, &count))
				goto fail;
			vertex->start = (unsigned short)start;
			vertex->count = (unsigned short)count;
		}
		free (seen);
		seen = NULL;
		if (!Mod_MD5Expect (text, "numtris") ||
			!Mod_MD5Int (text, 0, MD5_MAX_TRIANGLES, &numtris) ||
			(numtris && mesh->numverts < 3))
			goto fail;
		mesh->numtris = numtris;
		mesh->indices = Mod_MD5Alloc (budget, (size_t)numtris * 3,
			sizeof (*mesh->indices));
		if (numtris && !mesh->indices)
			goto fail;
		seen = calloc (numtris ? numtris : 1, 1);
		if (!seen)
			goto fail;
		for (j = 0; j < numtris; j++)
		{
			if (!Mod_MD5Expect (text, "tri") ||
				!Mod_MD5Int (text, 0, numtris - 1, &index) || seen[index])
				goto fail;
			seen[index] = 1;
			for (k = 0; k < 3; k++)
			{
				int vertex_index;
				if (!Mod_MD5Int (text, 0, mesh->numverts - 1, &vertex_index))
					goto fail;
				mesh->indices[index * 3 + k] = (unsigned short)vertex_index;
			}
		}
		free (seen);
		seen = NULL;
		if (!Mod_MD5Expect (text, "numweights") ||
			!Mod_MD5Int (text, 0, MD5_MAX_WEIGHTS, &mesh->numweights))
			goto fail;
		mesh->weights = Mod_MD5Alloc (budget, mesh->numweights, sizeof (*mesh->weights));
		if (mesh->numweights && !mesh->weights)
			goto fail;
		seen = calloc (mesh->numweights ? mesh->numweights : 1, 1);
		if (!seen)
			goto fail;
		for (j = 0; j < mesh->numweights; j++)
		{
			mmd5weight_t *weight;
			int joint;
			if (!Mod_MD5Expect (text, "weight") ||
				!Mod_MD5Int (text, 0, mesh->numweights - 1, &index) || seen[index] ||
				!Mod_MD5Int (text, 0, md5->numjoints - 1, &joint))
				goto fail;
			seen[index] = 1;
			weight = &mesh->weights[index];
			weight->joint = (unsigned char)joint;
			if (!Mod_MD5Float (text, &weight->bias) ||
				weight->bias < 0 || weight->bias > 1 ||
				!Mod_MD5Vector (text, weight->pos))
				goto fail;
		}
		free (seen);
		seen = NULL;
		if (!Mod_MD5Expect (text, "}"))
			goto fail;
		influence_count = 0;
		for (j = 0; j < mesh->numverts; j++)
		{
			mmd5vertex_t *vertex = &mesh->vertices[j];
			if (!vertex->count || vertex->start + vertex->count > mesh->numweights)
				goto fail;
			if (vertex->count > mesh->numweights - influence_count)
				goto fail;
			influence_count += vertex->count;
			sum = 0;
			for (k = 0; k < vertex->count; k++)
				sum += mesh->weights[vertex->start + k].bias;
			if (sum < 0.99f || sum > 1.01f)
				goto fail;
		}
		if (!Mod_MD5Normals (mesh, bind))
			goto fail;
		if (mesh->numverts >= 3 && mesh->numtris > 0)
			drawable = true;
	}
	return drawable && Mod_MD5AtEnd (text);
fail:
	free (seen);
	return false;
}

typedef struct
{
	const char *data;
	size_t length, offset;
} md5json_t;

static void Mod_MD5JSONSpace (md5json_t *json)
{
	while (json->offset < json->length &&
		(json->data[json->offset] == ' ' || json->data[json->offset] == '\t' ||
			json->data[json->offset] == '\r' || json->data[json->offset] == '\n'))
		json->offset++;
}

static qboolean Mod_MD5JSONChar (md5json_t *json, char expected)
{
	Mod_MD5JSONSpace (json);
	if (json->offset >= json->length || json->data[json->offset] != expected)
		return false;
	json->offset++;
	return true;
}

static qboolean Mod_MD5JSONString (md5json_t *json, char *output, size_t size)
{
	size_t n = 0;
	char c;

	if (!size || !Mod_MD5JSONChar (json, '"'))
		return false;
	while (json->offset < json->length)
	{
		c = json->data[json->offset++];
		if (c == '"')
		{
			output[n] = 0;
			return true;
		}
		if ((unsigned char)c < 32)
			return false;
		if (c == '\\')
		{
			if (json->offset >= json->length)
				return false;
			c = json->data[json->offset++];
			switch (c)
			{
			case '"':
			case '\\':
			case '/':
				break;
			case 'b':
				c = '\b';
				break;
			case 'f':
				c = '\f';
				break;
			case 'n':
				c = '\n';
				break;
			case 'r':
				c = '\r';
				break;
			case 't':
				c = '\t';
				break;
			default:
				return false;
			}
		}
		if (n + 1 >= size)
			return false;
		output[n++] = c;
	}
	return false;
}

static qboolean Mod_MD5JSONBool (md5json_t *json, qboolean *output)
{
	Mod_MD5JSONSpace (json);
	if (json->length - json->offset >= 4 &&
		!memcmp (json->data + json->offset, "true", 4))
	{
		json->offset += 4;
		*output = true;
		return true;
	}
	if (json->length - json->offset >= 5 &&
		!memcmp (json->data + json->offset, "false", 5))
	{
		json->offset += 5;
		*output = false;
		return true;
	}
	return false;
}

static qboolean Mod_MD5JSONFloat (md5json_t *json, float *output)
{
	size_t start, length;
	char number[64], *end;
	double value;

	Mod_MD5JSONSpace (json);
	start = json->offset;
	if (json->offset < json->length && json->data[json->offset] == '-')
		json->offset++;
	if (json->offset >= json->length)
		return false;
	if (json->data[json->offset] == '0')
		json->offset++;
	else
	{
		if (json->data[json->offset] < '1' || json->data[json->offset] > '9')
			return false;
		while (json->offset < json->length &&
			json->data[json->offset] >= '0' && json->data[json->offset] <= '9')
			json->offset++;
	}
	if (json->offset < json->length && json->data[json->offset] == '.')
	{
		json->offset++;
		if (json->offset >= json->length ||
			json->data[json->offset] < '0' || json->data[json->offset] > '9')
			return false;
		while (json->offset < json->length &&
			json->data[json->offset] >= '0' && json->data[json->offset] <= '9')
			json->offset++;
	}
	if (json->offset < json->length &&
		(json->data[json->offset] == 'e' || json->data[json->offset] == 'E'))
	{
		json->offset++;
		if (json->offset < json->length &&
			(json->data[json->offset] == '+' || json->data[json->offset] == '-'))
			json->offset++;
		if (json->offset >= json->length ||
			json->data[json->offset] < '0' || json->data[json->offset] > '9')
			return false;
		while (json->offset < json->length &&
			json->data[json->offset] >= '0' && json->data[json->offset] <= '9')
			json->offset++;
	}
	length = json->offset - start;
	if (!length || length >= sizeof (number))
		return false;
	memcpy (number, json->data + start, length);
	number[length] = 0;
	errno = 0;
	value = strtod (number, &end);
	if (errno || *end || !Mod_MD5Finite (value) ||
		value < -MD5_MAX_SCALAR || value > MD5_MAX_SCALAR)
		return false;
	*output = (float)value;
	return true;
}

static qboolean Mod_MD5Scales (const char *path, mmd5model_t *md5,
	md5hierarchy_t *hierarchy)
{
	md5json_t json;
	char *data = NULL;
	char name[48], key[32];
	unsigned char joint_seen[MD5_MAX_JOINTS] = { 0 };
	unsigned char frame_seen[MD5_MAX_FRAMES];
	qboolean pos_seen, value;
	int joint, frame, i;
	float scale;
	char *end;

	if (ri.FS_LoadFile (path, NULL) < 0)
		return true;
	if (!Mod_MD5ReadFile (path, MD5_MAX_SCALE_BYTES, &data, &json.length))
		return false;
	json.data = data;
	json.offset = 0;
	if (!Mod_MD5JSONChar (&json, '{'))
		goto fail;
	Mod_MD5JSONSpace (&json);
	if (json.offset < json.length && json.data[json.offset] == '}')
		json.offset++;
	else
		for (;;)
		{
			if (!Mod_MD5JSONString (&json, name, sizeof (name)) ||
				!Mod_MD5JSONChar (&json, ':') ||
				!Mod_MD5JSONChar (&json, '{'))
				goto fail;
			joint = -1;
			for (i = 0; i < md5->numjoints; i++)
				if (!strcmp (name, hierarchy[i].name))
					joint = i;
			if (joint < 0 || joint_seen[joint])
				goto fail;
			joint_seen[joint] = 1;
			memset (frame_seen, 0, md5->numframes);
			pos_seen = false;
			Mod_MD5JSONSpace (&json);
			if (json.offset < json.length && json.data[json.offset] == '}')
				json.offset++;
			else
				for (;;)
				{
					if (!Mod_MD5JSONString (&json, key, sizeof (key)) ||
						!Mod_MD5JSONChar (&json, ':'))
						goto fail;
					if (!strcmp (key, "scale_positions"))
					{
						if (pos_seen || !Mod_MD5JSONBool (&json, &value))
							goto fail;
						pos_seen = true;
						hierarchy[joint].scale_positions = value;
					}
					else
					{
						for (i = 0; key[i]; i++)
							if (key[i] < '0' || key[i] > '9')
								goto fail;
						if (!i)
							goto fail;
						frame = (int)strtol (key, &end, 10);
						if (*end || frame < 0 || frame >= md5->numframes ||
							frame_seen[frame] || !Mod_MD5JSONFloat (&json, &scale))
							goto fail;
						frame_seen[frame] = 1;
						md5->frames[frame * md5->numjoints + joint].scale = scale;
					}
					Mod_MD5JSONSpace (&json);
					if (json.offset < json.length && json.data[json.offset] == '}')
					{
						json.offset++;
						break;
					}
					if (!Mod_MD5JSONChar (&json, ','))
						goto fail;
				}
			Mod_MD5JSONSpace (&json);
			if (json.offset < json.length && json.data[json.offset] == '}')
			{
				json.offset++;
				break;
			}
			if (!Mod_MD5JSONChar (&json, ','))
				goto fail;
		}
	Mod_MD5JSONSpace (&json);
	if (json.offset != json.length)
		goto fail;
	ri.FS_FreeFile (data);
	return true;
fail:
	ri.FS_FreeFile (data);
	return false;
}

static qboolean Mod_MD5BuildFrame (mmd5model_t *md5,
	const md5hierarchy_t *hierarchy, const md5bind_t *base,
	const float *components, int frame)
{
	mmd5joint_t *joints = &md5->frames[frame * md5->numjoints];
	int i, j, k;

	for (i = 0; i < md5->numjoints; i++)
	{
		float values[6], orient[4];
		vec3_t position, rotated;
		mmd5joint_t *joint = &joints[i];
		const md5hierarchy_t *info = &hierarchy[i];
		int component = 0;

		for (j = 0; j < 3; j++)
		{
			values[j] = base[i].pos[j];
			values[j + 3] = base[i].orient[j];
		}
		for (j = 0; j < 6; j++)
			if (info->flags & (1 << j))
				values[j] = components[info->start + component++];
		for (j = 0; j < 3; j++)
		{
			position[j] = values[j];
			orient[j] = values[j + 3];
		}
		if (!Mod_MD5QuatW (orient) || !Mod_MD5QuatNormalize (orient))
			return false;
		if (info->scale_positions)
			for (j = 0; j < 3; j++)
				position[j] *= joint->scale;
		if (info->parent < 0)
		{
			VectorCopy (position, joint->pos);
			memcpy (joint->orient, orient, sizeof (joint->orient));
		}
		else
		{
			const mmd5joint_t *parent = &joints[info->parent];
			Mod_MD5QuatRotate (parent->orient, position, rotated);
			VectorAdd (parent->pos, rotated, joint->pos);
			Mod_MD5QuatMultiply (parent->orient, orient, joint->orient);
			if (!Mod_MD5QuatNormalize (joint->orient))
				return false;
		}
		for (k = 0; k < 3; k++)
			if (!Mod_MD5Finite (joint->pos[k]))
				return false;
	}
	return true;
}

static qboolean Mod_MD5ParseAnim (md5text_t *text, mmd5model_t *md5,
	md5budget_t *budget, const md5bind_t *mesh_bind,
	const char *scale_path)
{
	md5hierarchy_t hierarchy[MD5_MAX_JOINTS];
	md5bind_t base[MD5_MAX_JOINTS];
	float components[MD5_MAX_JOINTS * 6];
	unsigned char frame_seen[MD5_MAX_FRAMES] = { 0 };
	vec3_t ignored_bounds;
	int joints, component_count, framerate, i, j, k, frame, start, flags, parent;

	if (!Mod_MD5Expect (text, "MD5Version") ||
		!Mod_MD5Expect (text, "10") ||
		!Mod_MD5Expect (text, "commandline") ||
		!Mod_MD5Token (text) ||
		!Mod_MD5Expect (text, "numFrames") ||
		!Mod_MD5Int (text, 1, MD5_MAX_FRAMES, &md5->numframes) ||
		!Mod_MD5Expect (text, "numJoints") ||
		!Mod_MD5Int (text, 1, MD5_MAX_JOINTS, &joints) ||
		joints != md5->numjoints ||
		!Mod_MD5Expect (text, "frameRate") ||
		!Mod_MD5Int (text, 1, 1000, &framerate) ||
		!Mod_MD5Expect (text, "numAnimatedComponents") ||
		!Mod_MD5Int (text, 0, MD5_MAX_JOINTS * 6, &component_count) ||
		!Mod_MD5Expect (text, "hierarchy") || !Mod_MD5Expect (text, "{"))
		return false;
	for (i = 0; i < md5->numjoints; i++)
	{
		if (!Mod_MD5Token (text) || !text->token[0] ||
			strlen (text->token) >= sizeof (hierarchy[i].name))
			return false;
		strcpy (hierarchy[i].name, text->token);
		for (j = 0; j < i; j++)
			if (!strcmp (hierarchy[i].name, hierarchy[j].name))
				return false;
		if (!Mod_MD5Int (text, -1, i - 1, &parent) ||
			!Mod_MD5Int (text, 0, 63, &flags) ||
			!Mod_MD5Int (text, 0, component_count, &start))
			return false;
		hierarchy[i].parent = parent;
		if (strcmp (hierarchy[i].name, mesh_bind[i].name) ||
			parent != mesh_bind[i].parent)
			return false;
		hierarchy[i].flags = flags;
		hierarchy[i].start = start;
		hierarchy[i].scale_positions = false;
		for (j = k = 0; j < 6; j++)
			if (flags & (1 << j))
				k++;
		if (k > component_count - start)
			return false;
	}
	if (!Mod_MD5Expect (text, "}") ||
		!Mod_MD5Expect (text, "bounds") || !Mod_MD5Expect (text, "{"))
		return false;
	for (i = 0; i < md5->numframes * 2; i++)
		if (!Mod_MD5Vector (text, ignored_bounds))
			return false;
	if (!Mod_MD5Expect (text, "}") ||
		!Mod_MD5Expect (text, "baseframe") || !Mod_MD5Expect (text, "{"))
		return false;
	for (i = 0; i < md5->numjoints; i++)
		if (!Mod_MD5Vector (text, base[i].pos) ||
			!Mod_MD5Vector (text, base[i].orient) ||
			!Mod_MD5QuatW (base[i].orient) ||
			!Mod_MD5QuatNormalize (base[i].orient))
			return false;
	if (!Mod_MD5Expect (text, "}"))
		return false;
	md5->frames = Mod_MD5Alloc (budget,
		(size_t)md5->numframes * md5->numjoints, sizeof (*md5->frames));
	if (!md5->frames)
		return false;
	for (i = 0; i < md5->numframes * md5->numjoints; i++)
		md5->frames[i].scale = 1.0f;
	if (!Mod_MD5Scales (scale_path, md5, hierarchy))
		return false;
	for (i = 0; i < md5->numframes; i++)
	{
		if (!Mod_MD5Expect (text, "frame") ||
			!Mod_MD5Int (text, 0, md5->numframes - 1, &frame) ||
			frame_seen[frame] || !Mod_MD5Expect (text, "{"))
			return false;
		frame_seen[frame] = 1;
		for (j = 0; j < component_count; j++)
			if (!Mod_MD5Float (text, &components[j]))
				return false;
		if (!Mod_MD5Expect (text, "}") ||
			!Mod_MD5BuildFrame (md5, hierarchy, base, components, frame))
			return false;
	}
	return Mod_MD5AtEnd (text);
}

/* Optional conservative radii for CPU-skinned frames. A failed bound leaves
 * the replacement drawable; the renderer then skips culling for that model. */
static void Mod_MD5BuildBounds (mmd5model_t *md5, md5budget_t *budget)
{
	double weight_radius[MD5_MAX_JOINTS] = { 0 };
	unsigned char used[MD5_MAX_JOINTS] = { 0 };
	mmd5framebound_t *bounds;
	int m, v, w, f, j;

	for (m = 0; m < md5->nummeshes; m++)
	{
		const mmd5mesh_t *mesh = &md5->meshes[m];
		for (v = 0; v < mesh->numverts; v++)
		{
			const mmd5vertex_t *vertex = &mesh->vertices[v];
			if ((unsigned)vertex->start + vertex->count > (unsigned)mesh->numweights)
				return;
			for (w = 0; w < vertex->count; w++)
			{
				const mmd5weight_t *weight = &mesh->weights[vertex->start + w];
				double x, y, z, length;
				if (!weight->bias)
					continue;
				if (weight->joint >= md5->numjoints)
					return;
				x = weight->pos[0];
				y = weight->pos[1];
				z = weight->pos[2];
				length = sqrt (x * x + y * y + z * z);
				if (!Mod_MD5Finite (length))
					return;
				used[weight->joint] = 1;
				if (length > weight_radius[weight->joint])
					weight_radius[weight->joint] = length;
			}
		}
	}
	bounds = Mod_MD5Alloc (budget, md5->numframes, sizeof (*bounds));
	if (!bounds)
		return;
	for (f = 0; f < md5->numframes; f++)
	{
		const mmd5joint_t *pose = &md5->frames[f * md5->numjoints];
		double position_radius = 0, scaled_weight_radius = 0;
		for (j = 0; j < md5->numjoints; j++)
		{
			double x, y, z, position, scaled_weight;
			if (!used[j])
				continue;
			x = pose[j].pos[0];
			y = pose[j].pos[1];
			z = pose[j].pos[2];
			position = sqrt (x * x + y * y + z * z);
			scaled_weight = fabs ((double)pose[j].scale) * weight_radius[j];
			if (!Mod_MD5Finite (position) || !Mod_MD5Finite (scaled_weight))
				goto unavailable;
			if (position > position_radius)
				position_radius = position;
			if (scaled_weight > scaled_weight_radius)
				scaled_weight_radius = scaled_weight;
		}
		bounds[f].position_radius = (float)position_radius;
		bounds[f].scaled_weight_radius = (float)scaled_weight_radius;
	}
	md5->bounds = bounds;
	return;

unavailable:
	budget->used -= (size_t)md5->numframes * sizeof (*bounds);
	free (bounds);
}

static qboolean Mod_MD5SafePath (const char *path)
{
	const char *part, *p;

	if (!path || !path[0] || path[0] == '/' || path[0] == '\\')
		return false;
	part = path;
	for (p = path;; p++)
	{
		unsigned char c = (unsigned char)*p;
		if (c && (c < 32 || c == 127 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|'))
			return false;
		if (!c || c == '/' || c == '\\')
		{
			if (p == part || (p - part == 1 && part[0] == '.') ||
				(p - part == 2 && part[0] == '.' && part[1] == '.'))
				return false;
			if (!c)
				return true;
			part = p + 1;
		}
	}
}

static qboolean Mod_MD5Path (const char *model, const char *extension,
	char output[MAX_QPATH])
{
	char normalized[MAX_QPATH];
	const char *slash, *dot;
	size_t length, prefix, stem, suffix, i;

	length = strlen (model);
	if (length >= sizeof (normalized) || !Mod_MD5SafePath (model))
		return false;
	for (i = 0; i <= length; i++)
		normalized[i] = model[i] == '\\' ? '/' : model[i];
	slash = strrchr (normalized, '/');
	dot = strrchr (normalized, '.');
	if (!dot || (slash && dot <= slash + 1))
		return false;
	prefix = slash ? (size_t)(slash - normalized + 1) : 0;
	stem = (size_t)(dot - normalized) - prefix;
	suffix = strlen (extension);
	if (!stem || prefix + 4 + stem + suffix >= MAX_QPATH)
		return false;
	memcpy (output, normalized, prefix);
	memcpy (output + prefix, "md5/", 4);
	memcpy (output + prefix + 4, normalized + prefix, stem);
	memcpy (output + prefix + 4 + stem, extension, suffix + 1);
	return true;
}

static qboolean Mod_MD5SkinPath (const char *original,
	char output[MAX_QPATH])
{
	const char *slash, *backslash;
	size_t prefix, length, i;

	if (!Mod_MD5SafePath (original))
		return false;
	slash = strrchr (original, '/');
	backslash = strrchr (original, '\\');
	if (backslash && (!slash || backslash > slash))
		slash = backslash;
	prefix = slash ? (size_t)(slash - original + 1) : 0;
	length = strlen (original);
	if (length + 4 >= MAX_QPATH)
		return false;
	for (i = 0; i < prefix; i++)
		output[i] = original[i] == '\\' ? '/' : original[i];
	memcpy (output + prefix, "md5/", 4);
	for (i = prefix; i <= length; i++)
		output[i + 4] = original[i] == '\\' ? '/' : original[i];
	return true;
}

static qboolean Mod_MD5FirstMD3Skin (const byte *base, int length,
	int slot, char output[MAX_QPATH])
{
	dmd3header_t header;
	dmd3mesh_t mesh;
	unsigned mesh_offset, skin_offset, skins, i;
	const char *name;

	if (length < (int)sizeof (header))
		return false;
	memcpy (&header, base, sizeof (header));
	mesh_offset = Mod_MD3UInt (&header.ofs_meshes);
	if (!Mod_MD3Section (mesh_offset, sizeof (mesh), sizeof (header), (unsigned)length))
		return false;
	memcpy (&mesh, base + mesh_offset, sizeof (mesh));
	skins = Mod_MD3UInt (&mesh.num_skins);
	if (slot < 0 || (unsigned)slot >= skins || skins > MD3_MAX_SKINS)
		return false;
	skin_offset = Mod_MD3UInt (&mesh.ofs_skins);
	if (!Mod_MD3Section (skin_offset, skins * sizeof (dmd3skin_t),
			sizeof (mesh), Mod_MD3UInt (&mesh.meshsize)) ||
		skin_offset > (unsigned)length - mesh_offset ||
		skins * sizeof (dmd3skin_t) > (unsigned)length - mesh_offset - skin_offset)
		return false;
	name = (const char *)(base + mesh_offset + skin_offset + slot * sizeof (dmd3skin_t));
	if (!Mod_MD3SkinName (name))
		return false;
	strcpy (output, name);
	for (i = 0; output[i]; i++)
		if (output[i] == '\\')
			output[i] = '/';
	fast_strlwr (output);
	return true;
}

static qboolean Mod_MD5LoadSkins (model_t *mod, mmd5model_t *md5,
	md5budget_t *budget, const byte *base_file, int base_len)
{
	char original[MAX_QPATH], skin_path[MAX_QPATH];
	int i;

	if (mod->type == mod_alias)
		md5->numskins = ((dmdl_t *)mod->extradata)->num_skins;
	else
		md5->numskins = mod->md3->meshes[0].numskins;
	if (!md5->numskins)
		return true;
	md5->skins = Mod_MD5Alloc (budget, md5->numskins, sizeof (*md5->skins));
	if (!md5->skins)
		return false;
	for (i = 0; i < md5->numskins; i++)
	{
		if (mod->type == mod_alias)
		{
			const dmdl_t *header = mod->extradata;
			const char *name = (const char *)header + header->ofs_skins +
				i * MAX_SKINNAME;
			if (strlen (name) >= sizeof (original))
				return false;
			strcpy (original, name);
		}
		else if (!Mod_MD5FirstMD3Skin (base_file, base_len, i, original))
			return false;
		if (!Mod_MD5SkinPath (original, skin_path))
			return false;
		md5->skins[i] = GL_FindImage (skin_path, skin_path, it_skin);
	}
	return true;
}

static void Mod_LoadMD5 (model_t *mod, const byte *base_file, int base_len)
{
	char mesh_path[MAX_QPATH], anim_path[MAX_QPATH], scale_path[MAX_QPATH];
	char *mesh_data = NULL, *anim_data = NULL;
	size_t mesh_length = 0, anim_length = 0, anim_name_length;
	md5text_t mesh_text, anim_text;
	md5bind_t mesh_bind[MD5_MAX_JOINTS];
	md5budget_t budget = { 0 };
	mmd5model_t *md5 = NULL;
	qboolean valid = false;

	if (!Mod_MD5Path (mod->name, ".md5mesh", mesh_path) ||
		!Mod_MD5Path (mod->name, ".md5anim", anim_path))
		return;
	anim_name_length = strlen (anim_path);
	if (anim_name_length < 8 || anim_name_length + 1 >= sizeof (scale_path))
		return;
	memcpy (scale_path, anim_path, anim_name_length - 8);
	memcpy (scale_path + anim_name_length - 8, ".md5scale", 10);
	if (ri.FS_LoadFile (mesh_path, NULL) < 0 ||
		ri.FS_LoadFile (anim_path, NULL) < 0)
		return;
	if (!Mod_MD5ReadFile (mesh_path, MD5_MAX_TEXT_BYTES,
			&mesh_data, &mesh_length) ||
		!Mod_MD5ReadFile (anim_path, MD5_MAX_TEXT_BYTES,
			&anim_data, &anim_length))
		goto done;
	md5 = Mod_MD5Alloc (&budget, 1, sizeof (*md5));
	if (!md5)
		goto done;
	mesh_text.data = mesh_data;
	mesh_text.length = mesh_length;
	mesh_text.offset = 0;
	anim_text.data = anim_data;
	anim_text.length = anim_length;
	anim_text.offset = 0;
	valid = Mod_MD5ParseMesh (&mesh_text, md5, &budget, mesh_bind) &&
		Mod_MD5ParseAnim (&anim_text, md5, &budget, mesh_bind, scale_path) &&
		Mod_MD5LoadSkins (mod, md5, &budget, base_file, base_len);
	if (valid)
	{
		Mod_MD5BuildBounds (md5, &budget);
		mod->md5 = md5;
		md5 = NULL;
	}
done:
	if (mesh_data)
		ri.FS_FreeFile (mesh_data);
	if (anim_data)
		ri.FS_FreeFile (anim_data);
	Mod_FreeMD5 (md5);
	if (!valid)
		ri.Con_Printf (PRINT_DEVELOPER, "%s: invalid or oversized MD5 replacement\n",
			mod->name);
}


/*
==============================================================================

SPRITE MODELS

==============================================================================
*/

/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	const dsprite_t *sprin;
	dsprite_t *sprout;
	int32 header[3];
	unsigned table_bytes, i;

	// The flexible frame array is absent from a zero-frame SP2.
	if (modfilelen < (int)sizeof (header))
	{
		ri.Sys_Error (ERR_DROP, "sprite %s has a truncated header", mod->name);
		return;
	}
	memcpy (header, buffer, sizeof (header));
	header[0] = LittleLong (header[0]);
	header[1] = LittleLong (header[1]);
	header[2] = LittleLong (header[2]);
	if (header[0] != IDSPRITEHEADER || header[1] != SPRITE_VERSION)
	{
		ri.Sys_Error (ERR_DROP, "sprite %s has wrong version number (%i should be %i)",
			mod->name, header[1], SPRITE_VERSION);
		return;
	}
	if ((unsigned)header[2] > MAX_MD2SKINS)
	{
		ri.Sys_Error (ERR_DROP, "sprite %s has too many frames (%i > %i)",
			mod->name, header[2], MAX_MD2SKINS);
		return;
	}
	table_bytes = sizeof (header) + (unsigned)header[2] * sizeof (dsprframe_t);
	if (table_bytes > (unsigned)modfilelen)
	{
		ri.Sys_Error (ERR_DROP, "sprite %s has a truncated frame table", mod->name);
		return;
	}
	sprin = (const dsprite_t *)buffer;
	for (i = 0; i < (unsigned)header[2]; i++)
		if (!Mod_MD3SkinName (sprin->frames[i].name))
		{
			ri.Sys_Error (ERR_DROP, "sprite %s has an invalid frame skin name", mod->name);
			return;
		}

	// Linux hunks round allocations to 32 bytes; the file may have trailing padding.
	mod->extradata = Hunk_Begin ((table_bytes + 31) & ~31u, 0);
	sprout = Hunk_Alloc (table_bytes);
	sprout->ident = header[0];
	sprout->version = header[1];
	sprout->numframes = header[2];

	// byte swap everything
	for (i=0 ; i<sprout->numframes ; i++)
	{
		sprout->frames[i].width = LittleLong (sprin->frames[i].width);
		sprout->frames[i].height = LittleLong (sprin->frames[i].height);
		sprout->frames[i].origin_x = LittleLong (sprin->frames[i].origin_x);
		sprout->frames[i].origin_y = LittleLong (sprin->frames[i].origin_y);
		memcpy (sprout->frames[i].name, sprin->frames[i].name, MAX_SKINNAME);
		fast_strlwr (sprout->frames[i].name);
		mod->skins[i] = GL_FindImage (sprout->frames[i].name, sprout->frames[i].name, it_sprite);

		//r1: sprites crash if they don't have valid skins for framenum so be noisy
		if (!mod->skins[i])
		{
			ri.Con_Printf (PRINT_ALL, "GL_FindImage: Couldn't find skin '%s' for sprite '%s'\n", sprout->frames[i].name, mod->name);
			mod->skins[i] = r_notexture;
		}
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginRegistration

Specifies the model that will be used as the world
@@@@@@@@@@@@@@@@@@@@@
*/
void EXPORT R_BeginRegistration (char *model)
{
	char	fullname[MAX_QPATH];
	cvar_t	*flushmap;

	Mod_FreePendingLoad ();
	Draw_Flush ();
	GL_InvalidateVertexLighting ();
	r_registering = true;

#ifdef RB_IMAGE_CACHE
	EmptyImageCache();
#endif

	registration_sequence++;
	GL_ClearSkyMaterials ();
	r_oldviewcluster = -1;		// force markleafs

	Com_sprintf (fullname, sizeof(fullname), "maps/%s.bsp", model);

	// explicitly free the old map if different
	// this guarantees that mod_known[0] is the world map
	flushmap = ri.Cvar_Get ("flushmap", "0", 0);
	/* A dropped load has a name but no completed hunk or model hash entry. */
	if (strcmp (mod_known[0].name, fullname) || FLOAT_NE_ZERO (flushmap->value) ||
		!mod_known[0].extradatasize)
		Mod_Free (&mod_known[0]);
	r_worldmodel = Mod_ForName(fullname, true);
	GL_BuildWorldBuffer (r_worldmodel);

	r_viewcluster = -1;
}


/*
@@@@@@@@@@@@@@@@@@@@@
R_RegisterModel

@@@@@@@@@@@@@@@@@@@@@
*/
struct model_s * EXPORT R_RegisterModel (char *name)
{
	model_t	*mod;
	int		i;
	dsprite_t	*sprout;
	dmdl_t		*pheader;

	mod = Mod_ForName (name, false);
	if (mod)
	{
		mod->registration_sequence = registration_sequence;

		// register any images used by the models
		switch (mod->type)
		{
		case mod_brush:
			{
				for (i=0 ; i<mod->numtexinfo ; i++)
					mod->texinfo[i].image->registration_sequence = registration_sequence;
			}
			break;

		case mod_sprite:
			{
				sprout = (dsprite_t *)mod->extradata;
				for (i=0 ; i<sprout->numframes ; i++)
				{
					if (mod->skins[i])
						mod->skins[i]->registration_sequence  = registration_sequence;
				}
			}
			break;

		case mod_alias:
			{
				pheader = (dmdl_t *)mod->extradata;
				for (i=0 ; i<pheader->num_skins ; i++)
				{
					if (mod->skins[i])
						mod->skins[i]->registration_sequence  = registration_sequence;
				}
				mod->numframes = pheader->num_frames;
			}
			break;
			case mod_md3:
			{
				mmd3model_t *md3 = mod->md3;
				int mesh, skin;
				if (!md3)
					break;
				for (mesh = 0; mesh < md3->nummeshes; mesh++)
					for (skin = 0; skin < md3->meshes[mesh].numskins; skin++)
						if (md3->meshes[mesh].skins[skin])
							md3->meshes[mesh].skins[skin]->registration_sequence = registration_sequence;
				mod->numframes = md3->numframes;
			}
			break;
			default:
				break;
		}
		if (mod->md5)
			for (i = 0; i < mod->md5->numskins; i++)
				if (mod->md5->skins[i])
					mod->md5->skins[i]->registration_sequence = registration_sequence;
	}

	return mod;
}


/*
@@@@@@@@@@@@@@@@@@@@@
R_EndRegistration

@@@@@@@@@@@@@@@@@@@@@
*/
void EXPORT R_EndRegistration (void)
{
	int		i;
	model_t	*mod;

	for (i=0, mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;

		if (mod->registration_sequence != registration_sequence)
		{	// don't need this model
			Mod_Free (mod);
		}
	}

	GL_FreeUnusedImages ();
	r_registering = false;
}


//=============================================================================


/*
================
Mod_Free
================
*/
void Mod_Free (model_t *mod)
{
	model_t		*hashstart;
	model_t		**prev;
	unsigned	hash;

	if (mod_pending_model == mod)
	{
		mod_pending_model = NULL;
		if (mod_pending_buffer)
			Mod_FreeLoadFile (mod_pending_buffer);
	}
	hash = hashify (mod->name) % MODEL_HASH_SIZE;
	
	prev = &models_hash[hash];
	for (;;)
	{
		hashstart = *prev;
		if (!hashstart)
			break;
		if (hashstart == mod)
		{
			*prev = hashstart->hash_next;
			break;
		}
		prev = &hashstart->hash_next;
	}

	Mod_FreeAliasMesh (mod);
	Mod_FreeMD5 (mod->md5);
	if (mod->md3)
	{
		int i;
		for (i = 0; i < mod->md3->nummeshes; i++)
			GL_FreeMD3Buffer (&mod->md3->meshes[i]);
	}
	GL_FreeWorldBuffer (mod);
	free (mod->lightgrid);
	if (mod->extradata)
		Hunk_Free (mod->extradata);
	memset (mod, 0, sizeof(*mod));
}

void Mod_FreeAliasBuffers (void)
{
	int i;
	for (i = 0; i < mod_numknown; i++)
		GL_FreeAliasBuffer (mod_known[i].aliasmesh);
}

void Mod_FreeMD3Buffers (void)
{
	int i, j;
	for (i = 0; i < mod_numknown; i++)
		if (mod_known[i].md3)
			for (j = 0; j < mod_known[i].md3->nummeshes; j++)
				GL_FreeMD3Buffer (&mod_known[i].md3->meshes[j]);
}

void Mod_FreeMD5Buffers (void)
{
	int i, j;
	for (i = 0; i < mod_numknown; i++)
		if (mod_known[i].md5)
			for (j = 0; j < mod_known[i].md5->nummeshes; j++)
				GL_FreeMD5Buffer (&mod_known[i].md5->meshes[j]);
}

/*
================
Mod_FreeAll
================
*/
void Mod_FreeAll (void)
{
	int		i;
#ifdef REF_HARD_LINKED
	mscache_t *cache, *next;
#endif

	Mod_FreePendingLoad ();
	for (i=0 ; i<mod_numknown ; i++)
	{
		if (mod_known[i].extradatasize || mod_known[i].type == mod_empty)
			Mod_Free (&mod_known[i]);
	}

#ifdef REF_HARD_LINKED
	/* Do not retain names or cached allocation sizes across renderer/mod reloads. */
	for (i = 0; i < MODEL_HASH_SIZE; i++)
	{
		for (cache = model_size_cache[i]; cache; cache = next)
		{
			next = cache->hash_next;
			free (cache);
		}
		model_size_cache[i] = NULL;
	}
	memset (models_hash, 0, sizeof (models_hash));
	memset (mod_known, 0, sizeof (mod_known));
	mod_numknown = 0;
#endif
}
