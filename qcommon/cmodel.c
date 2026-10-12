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
// cmodel.c -- model loading

#include "qcommon.h"
#include "bsp_node_graph.h"
#include "bsp_format.h"

typedef struct
{
	cplane_t	*plane;
	fplane_t	*fastplane;
	int			children[2];		// negative numbers are leafs
} cnode_t;

typedef struct
{
	cplane_t	*plane;
	mapsurface_t	*surface;
} cbrushside_t;

typedef struct
{
	int			contents;
	int			numsides;
	int			firstbrushside;
	int			checkcount;		// to avoid repeated testings
} cbrush_t;

typedef struct
{
	int		numareaportals;
	int		firstareaportal;
	int		floodnum;			// if two areas have equal floodnums, they are connected
	int		floodvalid;
} carea_t;

static int			checkcount;

static char		map_name[MAX_QPATH];

static int			numbrushsides;
static cbrushside_t map_brushsides[MAX_MAP_BRUSHSIDES];

int			numtexinfo;
mapsurface_t	map_surfaces[MAX_MAP_TEXINFO];

static int			numplanes;
static cplane_t	map_planes[MAX_MAP_PLANES+6];		// extra for box hull
static fplane_t	map_fastplanes[MAX_MAP_PLANES+6];		// extra for box hull

static int			numnodes;
static cnode_t		map_nodes[MAX_MAP_NODES+6];		// extra for box hull

static int			numleafs = 1;	// allow leaf funcs to be called without a map
cleaf_t				map_leafs[MAX_MAP_LEAFS];
static int			emptyleaf, solidleaf;

static int			numleafbrushes;
static uint16		map_leafbrushes[MAX_MAP_LEAFBRUSHES];

int					numcmodels;
static cmodel_t		map_cmodels[MAX_MAP_MODELS];

static int			numbrushes;
static cbrush_t		map_brushes[MAX_MAP_BRUSHES];

static int			numvisibility;
static byte			map_visibility[MAX_MAP_VISIBILITY];

static int			numentitychars;
static char			map_entitystring[MAX_MAP_ENTSTRING];

static int			numareas = 1;
static carea_t		map_areas[MAX_MAP_AREAS];

static int			numareaportals;
static dareaportal_t map_areaportals[MAX_MAP_AREAPORTALS];

int			numclusters = 1;

static mapsurface_t	nullsurface;

static int			floodvalid;

static qboolean		portalopen[MAX_MAP_AREAPORTALS];


static	cvar_t		*map_noareas;

void	CM_InitBoxHull (void);
void	FloodAreaConnections (void);

#ifndef DEDICATED_ONLY
int		c_pointcontents;
int		c_traces, c_brush_traces;
#endif

/*
===============================================================================

					MAP LOADING

===============================================================================
*/

byte	*cmod_base;
int		cmod_size;

/*
=================
CMod_LoadSubmodels
=================
*/
void CMod_LoadSubmodels (lump_t *l)
{
	const byte *in;
	cmodel_t	*out;
	int			i, j, count;

	in = cmod_base + l->fileofs;
	if (l->filelen % sizeof (dmodel_t))
		Com_Error (ERR_DROP, "CMod_LoadSubmodels: funny lump size");

	count = l->filelen / sizeof (dmodel_t);

	if (count < 1)
		Com_Error (ERR_DROP, "Map with no models");

	if (count >= MAX_MAP_MODELS)
		Com_Error (ERR_DROP, "Map has too many models");

	numcmodels = count;

	for (i = 0; i < count; i++, in += sizeof (dmodel_t))
	{
		out = &map_cmodels[i];

		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = R1BSP_ReadFloat (in + j * 4) - 1;
			out->maxs[j] = R1BSP_ReadFloat (in + 12 + j * 4) + 1;
			out->origin[j] = R1BSP_ReadFloat (in + 24 + j * 4);
		}
		out->headnode = R1BSP_ReadI32 (in + offsetof (dmodel_t, headnode));
	}
}


/*
=================
CMod_LoadSurfaces
=================
*/
void CMod_LoadSurfaces (lump_t *l)
{
	const byte *in;
	mapsurface_t	*out;
	int			i, count;

	in = cmod_base + l->fileofs;
	if (l->filelen % sizeof (texinfo_t))
		Com_Error (ERR_DROP, "CMod_LoadSurfaces: funny lump size");
	count = l->filelen / sizeof (texinfo_t);
	if (count < 1)
		Com_Error (ERR_DROP, "CMod_LoadSurfaces: Map with no surfaces");
	if (count > MAX_MAP_TEXINFO)
		Com_Error (ERR_DROP, "CMod_LoadSurfaces: Map has too many surfaces");

	numtexinfo = count;
	out = map_surfaces;

	for (i = 0; i < count; i++, in += sizeof (texinfo_t), out++)
	{
		const byte *texture = in + offsetof (texinfo_t, texture);
		strncpy (out->c.name, (const char *)texture, sizeof (out->c.name) - 1);
		out->c.name[sizeof (out->c.name) - 1] = 0;
		strncpy (out->rname, (const char *)texture, sizeof (out->rname) - 1);
		out->rname[sizeof (out->rname) - 1] = 0;
		out->c.flags = R1BSP_ReadI32 (in + offsetof (texinfo_t, flags));
		out->c.value = R1BSP_ReadI32 (in + offsetof (texinfo_t, value));
	}
}


/*
=================
CMod_LoadNodes

=================
*/
void CMod_LoadNodes (lump_t *l)
{
	const byte *in;
	int child, planenum;
	cnode_t		*out;
	int			i, j, count;
	bsp38_node_graph_error_t graph_error;
	bsp38_node_graph_status_t graph_status;

	in = cmod_base + l->fileofs;
	if (l->filelen % sizeof (dnode_t))
		Com_Error (ERR_DROP, "CMod_LoadNodes: funny lump size");
	count = l->filelen / sizeof (dnode_t);

	if (count < 1)
		Com_Error (ERR_DROP, "Map has no nodes");
	if (count > MAX_MAP_NODES)
		Com_Error (ERR_DROP, "Map has too many nodes");
	graph_status = BSP38_ValidateNodeGraph (in, count,
		numleafs, &graph_error);
	if (graph_status == BSP38_NODE_GRAPH_BAD_NODE)
		Com_Error (ERR_DROP, "CMod_LoadNodes: node %d child %d has bad node %d",
			graph_error.node, graph_error.side, graph_error.child);
	else if (graph_status == BSP38_NODE_GRAPH_BAD_LEAF)
		Com_Error (ERR_DROP, "CMod_LoadNodes: node %d child %d has bad leaf %d",
			graph_error.node, graph_error.side, graph_error.child);
	else if (graph_status == BSP38_NODE_GRAPH_SHARED_NODE)
		Com_Error (ERR_DROP, "CMod_LoadNodes: node %d child %d has shared node %d",
			graph_error.node, graph_error.side, graph_error.child);
	else if (graph_status == BSP38_NODE_GRAPH_CYCLE)
		Com_Error (ERR_DROP, "CMod_LoadNodes: node %d child %d forms a cycle to node %d",
			graph_error.node, graph_error.side, graph_error.child);
	else if (graph_status != BSP38_NODE_GRAPH_OK)
		Com_Error (ERR_DROP, "CMod_LoadNodes: out of memory validating nodes");

	out = map_nodes;

	numnodes = count;

	for (i = 0; i < count; i++, out++, in += sizeof (dnode_t))
	{
		planenum = R1BSP_ReadI32 (in + offsetof (dnode_t, planenum));
		if (planenum < 0 || planenum >= numplanes)
			Com_Error (ERR_DROP, "CMod_LoadNodes: node %d has bad plane %d", i, planenum);
		out->plane = map_planes + planenum;
		out->fastplane = map_fastplanes + planenum;
		for (j=0 ; j<2 ; j++)
		{
			child = R1BSP_ReadI32 (in + offsetof (dnode_t, children) + j * 4);
			out->children[j] = child;
		}
	}

}

/*
=================
CMod_LoadBrushes

=================
*/
void CMod_LoadBrushes (lump_t *l)
{
	const byte *in;
	cbrush_t	*out;
	int			i, count;
	
	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof (dbrush_t))
		Com_Error (ERR_DROP, "CMod_LoadBrushes: funny lump size");
	count = l->filelen / sizeof (dbrush_t);

	if (count > MAX_MAP_BRUSHES)
		Com_Error (ERR_DROP, "Map has too many brushes");

	out = map_brushes;

	numbrushes = count;

	for (i = 0; i < count; i++, out++, in += sizeof (dbrush_t))
	{
		out->firstbrushside = R1BSP_ReadI32 (in);
		out->numsides = R1BSP_ReadI32 (in + 4);
		out->contents = R1BSP_ReadI32 (in + 8);
	}

}

/*
=================
CMod_LoadLeafs
=================
*/
void CMod_LoadLeafs (lump_t *l)
{
	int			i;
	cleaf_t		*out;
	const byte *in;
	int			count;

	in = cmod_base + l->fileofs;
	if (l->filelen % sizeof (dleaf_t))
		Com_Error (ERR_DROP, "CMod_LoadLeafs: funny lump size");
	count = l->filelen / sizeof (dleaf_t);

	if (count < 1)
		Com_Error (ERR_DROP, "Map with no leafs");
	// need to save space for box planes
	if (count > MAX_MAP_PLANES)
		Com_Error (ERR_DROP, "Map has too many planes");

	out = map_leafs;	
	numleafs = count;
	numclusters = 0;

	for (i = 0; i < count; i++, in += sizeof (dleaf_t), out++)
	{
		out->contents = R1BSP_ReadI32 (in + offsetof (dleaf_t, contents));
		out->cluster = R1BSP_ReadI16 (in + offsetof (dleaf_t, cluster));
		out->area = R1BSP_ReadI16 (in + offsetof (dleaf_t, area));
		out->firstleafbrush = R1BSP_ReadU16 (in + offsetof (dleaf_t, firstleafbrush));
		out->numleafbrushes = R1BSP_ReadU16 (in + offsetof (dleaf_t, numleafbrushes));

		if (out->cluster >= numclusters)
			numclusters = out->cluster + 1;
	}

	if (map_leafs[0].contents != CONTENTS_SOLID)
		Com_Error (ERR_DROP, "Map leaf 0 is not CONTENTS_SOLID");

	solidleaf = 0;
	emptyleaf = -1;
	for (i=1 ; i<numleafs ; i++)
	{
		if (!map_leafs[i].contents)
		{
			emptyleaf = i;
			break;
		}
	}
	if (emptyleaf == -1)
		Com_Error (ERR_DROP, "Map does not have an empty leaf");
}

/*
=================
CMod_LoadPlanes
=================
*/
void CMod_LoadPlanes (lump_t *l)
{
	int			i, j;
	cplane_t	*out;
	fplane_t	*fout;
	const byte *in;
	int			count;
	int			bits;

	in = cmod_base + l->fileofs;
	if (l->filelen % sizeof (dplane_t))
		Com_Error (ERR_DROP, "CMod_LoadPlanes: funny lump size");
	count = l->filelen / sizeof (dplane_t);

	if (count < 1)
		Com_Error (ERR_DROP, "Map with no planes");

	// need to save space for box planes
	if (count > MAX_MAP_PLANES)
		Com_Error (ERR_DROP, "Map has too many planes");

	out = map_planes;
	fout = map_fastplanes;
	numplanes = count;

	for (i = 0; i < count; i++, in += sizeof (dplane_t), out++, fout++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = R1BSP_ReadFloat (in + j * 4);
			if (FLOAT_LT_ZERO(out->normal[j]))
				bits |= 1<<j;
		}

		out->dist = R1BSP_ReadFloat (in + 12);
		if (R1BSP_ReadU32 (in + 16) > 5)
			Com_Error (ERR_DROP, "CMod_LoadPlanes: bad plane type %u (expected 0-5)", R1BSP_ReadU32 (in + 16));
		out->type = (byte)R1BSP_ReadU32 (in + 16);
		out->signbits = (byte)bits;

		//r1ch: "fast" planes, less byte->int work needed
		fout->dist = out->dist;
		fout->type = R1BSP_ReadU32 (in + 16);
		fout->signbits = bits;
		FastVectorCopy (out->normal, fout->normal);
	}
}

/*
=================
CMod_LoadLeafBrushes
=================
*/
void CMod_LoadLeafBrushes (lump_t *l)
{
	int			i;
	uint16		*out;
	const byte *in;
	int			count;
	
	in = (void *)(cmod_base + l->fileofs);

	if (l->filelen % sizeof (uint16))
		Com_Error (ERR_DROP, "CMod_LoadLeafBrushes: funny lump size");

	count = l->filelen / sizeof (uint16);

	if (count < 1)
		Com_Error (ERR_DROP, "Map with no planes");

	// need to save space for box planes
	if (count >= MAX_MAP_LEAFBRUSHES)
		Com_Error (ERR_DROP, "Map has too many leafbrushes");

	out = map_leafbrushes;
	numleafbrushes = count;

	for (i = 0; i < count; i++, in += sizeof (uint16), out++)
		*out = R1BSP_ReadU16 (in);
}

/*
=================
CMod_LoadBrushSides
=================
*/
void CMod_LoadBrushSides (lump_t *l)
{
	int			i, j;
	cbrushside_t	*out;
	const byte *in;
	int			count;
	int			num;

	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof (dbrushside_t))
		Com_Error (ERR_DROP, "CMod_LoadBrushSides: funny lump size");
	count = l->filelen / sizeof (dbrushside_t);

	// need to save space for box planes
	if (count > MAX_MAP_BRUSHSIDES)
		Com_Error (ERR_DROP, "Map has too many planes (%d > 65536)", count);

	out = map_brushsides;	
	numbrushsides = count;

	for (i = 0; i < count; i++, in += sizeof (dbrushside_t), out++)
	{
		num = R1BSP_ReadU16 (in);
		if (num >= numplanes)
			Com_Error (ERR_DROP, "Bad brushside plane (%d >= %d)", num, numplanes);
		out->plane = &map_planes[num];
		j = R1BSP_ReadI16 (in + 2);
		if (j >= numtexinfo)
			Com_Error (ERR_DROP, "Bad brushside texinfo (%d > %d)", j, numtexinfo);
		else if (j < 0)
			j = 0;
		out->surface = &map_surfaces[j];
	}
}

/* QBSP widened records are read from bytes; the classic CMod_Load* functions
 * above remain the IBSP path. */

static void CMod_LoadQBSPPlanes (lump_t *l)
{
	const byte *in = cmod_base + l->fileofs;
	cplane_t *out = map_planes;
	fplane_t *fout = map_fastplanes;
	uint32 type;
	int i, j, bits, count = l->filelen / sizeof (dplane_t);

	if (l->filelen % sizeof (dplane_t) || count < 1 ||
		count + 12 > MAX_MAP_PLANES)
		Com_Error (ERR_DROP, "CMod_LoadQBSPPlanes: bad lump size");
	numplanes = count;
	for (i = 0; i < count; i++, out++, fout++, in += sizeof (dplane_t))
	{
		bits = 0;
		for (j = 0; j < 3; j++)
		{
			out->normal[j] = R1BSP_ReadFloat (in + j * 4);
			if (FLOAT_LT_ZERO (out->normal[j]))
				bits |= 1 << j;
		}
		out->dist = R1BSP_ReadFloat (in + 12);
		type = R1BSP_ReadU32 (in + 16);
		if (type > 5)
			Com_Error (ERR_DROP, "CMod_LoadQBSPPlanes: bad type in plane %d", i);
		out->type = (byte)type;
		out->signbits = (byte)bits;
		fout->dist = out->dist;
		fout->type = type;
		fout->signbits = bits;
		FastVectorCopy (out->normal, fout->normal);
	}
}

static void CMod_LoadQBSPBrushSides (lump_t *l)
{
	const byte *in = cmod_base + l->fileofs;
	cbrushside_t *out = map_brushsides;
	uint32 plane, texinfo;
	int i, count = l->filelen / sizeof (qbsp_dbrushside_t);

	if (l->filelen % sizeof (qbsp_dbrushside_t) || count > MAX_MAP_BRUSHSIDES - 6)
		Com_Error (ERR_DROP, "CMod_LoadQBSPBrushSides: bad lump size");
	numbrushsides = count;
	for (i = 0; i < count; i++, out++, in += sizeof (qbsp_dbrushside_t))
	{
		plane = R1BSP_ReadU32 (in);
		texinfo = R1BSP_ReadU32 (in + 4);
		if (plane >= (uint32)numplanes ||
			(texinfo != (uint32)-1 && texinfo >= (uint32)numtexinfo))
			Com_Error (ERR_DROP, "CMod_LoadQBSPBrushSides: bad reference in side %d", i);
		out->plane = map_planes + plane;
		out->surface = texinfo == (uint32)-1 ? &nullsurface : map_surfaces + texinfo;
	}
}

static void CMod_LoadQBSPLeafBrushes (lump_t *l)
{
	const byte *in = cmod_base + l->fileofs;
	uint16 *out = map_leafbrushes;
	uint32 brush;
	int i, count = l->filelen / sizeof (qbsp_dleafbrush_t);

	if (l->filelen % sizeof (qbsp_dleafbrush_t) || count < 1 ||
		count >= MAX_MAP_LEAFBRUSHES)
		Com_Error (ERR_DROP, "CMod_LoadQBSPLeafBrushes: bad lump size");
	numleafbrushes = count;
	for (i = 0; i < count; i++, in += sizeof (qbsp_dleafbrush_t), out++)
	{
		brush = R1BSP_ReadU32 (in);
		if (brush >= (uint32)numbrushes || brush > 65535u)
			Com_Error (ERR_DROP, "CMod_LoadQBSPLeafBrushes: bad brush %u at %d", brush, i);
		*out = (uint16)brush;
	}
}

static void CMod_LoadQBSPLeafs (lump_t *l, qboolean has_visibility)
{
	const byte *in = cmod_base + l->fileofs;
	cleaf_t *out = map_leafs;
	uint32 cluster, area, first, span;
	int i, empty = -1, count = l->filelen / sizeof (qbsp_dleaf_t);

	if (l->filelen % sizeof (qbsp_dleaf_t) || count < 1 || count >= MAX_MAP_LEAFS)
		Com_Error (ERR_DROP, "CMod_LoadQBSPLeafs: bad lump size");
	numleafs = count;
	numclusters = 0;
	for (i = 0; i < count; i++, in += sizeof (qbsp_dleaf_t), out++)
	{
		out->contents = R1BSP_ReadI32 (in);
		cluster = R1BSP_QBSPLeafCluster (in, has_visibility);
		area = R1BSP_ReadU32 (in + 8);
		first = R1BSP_ReadU32 (in + 44);
		span = R1BSP_ReadU32 (in + 48);
		if ((has_visibility && cluster != (uint32)-1 &&
				cluster >= R1BSP_ReadU32 (map_visibility)) ||
			area >= (uint32)numareas || first > 65535u || span > 65535u ||
			(uint64)first + span > (uint32)numleafbrushes)
			Com_Error (ERR_DROP, "CMod_LoadQBSPLeafs: bad reference in leaf %d", i);
		out->cluster = cluster == (uint32)-1 ? -1 : (int)cluster;
		out->area = (int)area;
		out->firstleafbrush = (uint16)first;
		out->numleafbrushes = (uint16)span;
		if (out->cluster >= numclusters)
			numclusters = out->cluster + 1;
		if (i && !out->contents && empty == -1)
			empty = i;
	}
	if (map_leafs[0].contents != CONTENTS_SOLID || empty == -1)
		Com_Error (ERR_DROP, "CMod_LoadQBSPLeafs: missing solid or empty leaf");
	solidleaf = 0;
	emptyleaf = empty;
}

static void CMod_LoadQBSPNodes (lump_t *l)
{
	const byte *in = cmod_base + l->fileofs;
	cnode_t *out = map_nodes;
	bsp38_node_graph_error_t graph_error;
	bsp38_node_graph_status_t graph_status;
	uint32 plane;
	int i, j, count = l->filelen / sizeof (qbsp_dnode_t);

	if (l->filelen % sizeof (qbsp_dnode_t) || count < 1 ||
		count > MAX_MAP_NODES - 6)
		Com_Error (ERR_DROP, "CMod_LoadQBSPNodes: bad lump size");
	graph_status = BSP38_ValidateNodeGraphLayoutLimit (in, count, numleafs,
		sizeof (qbsp_dnode_t), offsetof (qbsp_dnode_t, children), 256,
		&graph_error);
	if (graph_status != BSP38_NODE_GRAPH_OK)
		Com_Error (ERR_DROP, "CMod_LoadQBSPNodes: bad graph (%d) at node %d child %d",
			graph_status, graph_error.node, graph_error.side);
	numnodes = count;
	for (i = 0; i < count; i++, out++, in += sizeof (qbsp_dnode_t))
	{
		plane = R1BSP_ReadU32 (in);
		if (plane >= (uint32)numplanes)
			Com_Error (ERR_DROP, "CMod_LoadQBSPNodes: bad plane in node %d", i);
		out->plane = map_planes + plane;
		out->fastplane = map_fastplanes + plane;
		for (j = 0; j < 2; j++)
			out->children[j] = R1BSP_ReadI32 (in + 4 + j * 4);
	}
}

/*
=================
CMod_LoadAreas
=================
*/
void CMod_LoadAreas (lump_t *l)
{
	int			i;
	carea_t		*out;
	const byte *in;
	int			count;

	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof (darea_t))
		Com_Error (ERR_DROP, "CMod_LoadAreas: funny lump size");
	count = l->filelen / sizeof (darea_t);

	if (count > MAX_MAP_AREAS)
		Com_Error (ERR_DROP, "Map has too many areas");

	out = map_areas;
	numareas = count;

	for (i = 0; i < count; i++, in += sizeof (darea_t), out++)
	{
		out->numareaportals = R1BSP_ReadI32 (in);
		out->firstareaportal = R1BSP_ReadI32 (in + 4);
		out->floodvalid = 0;
		out->floodnum = 0;
	}
}

/*
=================
CMod_LoadAreaPortals
=================
*/
void CMod_LoadAreaPortals (lump_t *l)
{
	int			i;
	dareaportal_t		*out;
	const byte *in;
	int count, highest = -1;

	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof (dareaportal_t))
		Com_Error (ERR_DROP, "CMod_LoadAreaPortals: funny lump size");

	count = l->filelen / sizeof (dareaportal_t);

	if (count > MAX_MAP_AREAPORTALS)
		Com_Error (ERR_DROP, "Map has too many area portals");

	out = map_areaportals;
	numareaportals = count;

	for (i = 0; i < count; i++, in += sizeof (dareaportal_t), out++)
	{
		out->portalnum = R1BSP_ReadI32 (in);
		out->otherarea = R1BSP_ReadI32 (in + 4);
		if (out->portalnum < 0 || out->portalnum >= MAX_MAP_AREAPORTALS)
			Com_Error (ERR_DROP, "CMod_LoadAreaPortals: bad portal state index %d", out->portalnum);
		if (out->portalnum > highest)
			highest = out->portalnum;
	}
	/* Portal numbers index portalopen, not the areaportal record array. */
	if (highest + 1 > numareaportals)
		numareaportals = highest + 1;
}

/*
=================
CMod_LoadVisibility
=================
*/
void CMod_LoadVisibility (lump_t *l)
{
	numvisibility = l->filelen;
	if (l->filelen > MAX_MAP_VISIBILITY)
		Com_Error (ERR_DROP, "Map has too large visibility lump");

	memcpy (map_visibility, cmod_base + l->fileofs, l->filelen);
}


/*
=================
CMod_LoadEntityString
=================
*/
void CMod_LoadEntityString (lump_t *l)
{
	numentitychars = l->filelen;
	if (l->filelen > MAX_MAP_ENTSTRING)
		Com_Error (ERR_DROP, "Map has too large entity lump (%d > %d)", l->filelen, MAX_MAP_ENTSTRING);

	memcpy (map_entitystring, cmod_base + l->fileofs, l->filelen);
}

qboolean CM_MapWillLoad (const char *name)
{
	char			csname[MAX_QPATH];

	if (!name || !name[0])
		return true;

	Com_sprintf (csname, sizeof(csname), "%s.override", name);

	if (FS_LoadFile (csname, NULL) != -1)
		return true;

	if (FS_LoadFile (name, NULL) != -1)
		return true;

	return false;
}

enum
{
	CM_BSP38_OK,
	CM_BSP38_SHORT,
	CM_BSP38_IDENT,
	CM_BSP38_VERSION,
	CM_BSP38_LUMP
};

// Decode a private header copy before the collision loader publishes map state.
static int CMod_DecodeBSP38Header (const void *buffer, uint32 length,
	dheader_t *header, int *bad_lump)
{
	int i, offset, size;

	if (length < sizeof (*header))
		return CM_BSP38_SHORT;

	memcpy (header, buffer, sizeof (*header));
	header->ident = LittleLong (header->ident);
	header->version = LittleLong (header->version);
	if (header->ident != IDBSPHEADER)
		return CM_BSP38_IDENT;
	if (header->version != BSPVERSION)
		return CM_BSP38_VERSION;

	for (i = 0; i < MAX_LUMPS; i++)
	{
		offset = LittleLong (header->lumps[i].fileofs);
		size = LittleLong (header->lumps[i].filelen);
		header->lumps[i].fileofs = offset;
		header->lumps[i].filelen = size;
		if (i == LUMP_POP)
			continue; // unused in BSP38; old maps may contain invalid POP fields
		if (offset < 0 || size < 0 || (uint32)offset > length ||
			(uint32)size > length - (uint32)offset)
		{
			*bad_lump = i;
			return CM_BSP38_LUMP;
		}
	}
	return CM_BSP38_OK;
}

/* Validate classic collision references before checksum publication. The disk
 * records are read fieldwise so valid odd-offset IBSP lumps remain usable. */
static qboolean CMod_QBSPValidVisibility (const byte *vis, uint32 length,
	uint32 clusters);

static const char *CMod_ValidateIBSPCollisionRecords (const byte *file,
	const dheader_t *header)
{
	const byte *p;
	bsp38_node_graph_error_t graph_error;
	bsp38_node_graph_status_t graph_status;
	uint32 i, j, first, span, plane, brush, visclusters = 0;
	uint32 planes, texinfos, brushes, sides, leafbrushes, leafs, areas, portals;
	uint32 nodes, models, faces, leaffaces, vertices, edges, surfedges;
	float value;
	int texinfo, cluster, area, headnode, firstedge, numedges, surfedge;
	qboolean empty = false;

	if (header->lumps[LUMP_PLANES].filelen % sizeof (dplane_t) ||
		header->lumps[LUMP_VERTEXES].filelen % sizeof (dvertex_t) ||
		header->lumps[LUMP_TEXINFO].filelen % sizeof (texinfo_t) ||
		header->lumps[LUMP_NODES].filelen % sizeof (dnode_t) ||
		header->lumps[LUMP_MODELS].filelen % sizeof (dmodel_t) ||
		header->lumps[LUMP_FACES].filelen % sizeof (dface_t) ||
		header->lumps[LUMP_LEAFFACES].filelen % sizeof (uint16) ||
		header->lumps[LUMP_EDGES].filelen % sizeof (dedge_t) ||
		header->lumps[LUMP_SURFEDGES].filelen % sizeof (int32) ||
		header->lumps[LUMP_BRUSHES].filelen % sizeof (dbrush_t) ||
		header->lumps[LUMP_BRUSHSIDES].filelen % sizeof (dbrushside_t) ||
		header->lumps[LUMP_LEAFBRUSHES].filelen % sizeof (uint16) ||
		header->lumps[LUMP_LEAFS].filelen % sizeof (dleaf_t) ||
		header->lumps[LUMP_AREAS].filelen % sizeof (darea_t) ||
		header->lumps[LUMP_AREAPORTALS].filelen % sizeof (dareaportal_t))
		return "invalid classic collision record stride";
	planes = header->lumps[LUMP_PLANES].filelen / sizeof (dplane_t);
	vertices = header->lumps[LUMP_VERTEXES].filelen / sizeof (dvertex_t);
	texinfos = header->lumps[LUMP_TEXINFO].filelen / sizeof (texinfo_t);
	nodes = header->lumps[LUMP_NODES].filelen / sizeof (dnode_t);
	models = header->lumps[LUMP_MODELS].filelen / sizeof (dmodel_t);
	faces = header->lumps[LUMP_FACES].filelen / sizeof (dface_t);
	leaffaces = header->lumps[LUMP_LEAFFACES].filelen / sizeof (uint16);
	edges = header->lumps[LUMP_EDGES].filelen / sizeof (dedge_t);
	surfedges = header->lumps[LUMP_SURFEDGES].filelen / sizeof (int32);
	brushes = header->lumps[LUMP_BRUSHES].filelen / sizeof (dbrush_t);
	sides = header->lumps[LUMP_BRUSHSIDES].filelen / sizeof (dbrushside_t);
	leafbrushes = header->lumps[LUMP_LEAFBRUSHES].filelen / sizeof (uint16);
	leafs = header->lumps[LUMP_LEAFS].filelen / sizeof (dleaf_t);
	areas = header->lumps[LUMP_AREAS].filelen / sizeof (darea_t);
	portals = header->lumps[LUMP_AREAPORTALS].filelen / sizeof (dareaportal_t);
	if (!planes || !texinfos || !nodes || !models || !leafs ||
		planes > MAX_MAP_PLANES || vertices > MAX_MAP_VERTS ||
		texinfos > MAX_MAP_TEXINFO ||
		nodes > MAX_MAP_NODES || models >= MAX_MAP_MODELS ||
		faces > MAX_MAP_FACES || leaffaces > MAX_MAP_LEAFFACES ||
		edges > MAX_MAP_EDGES || surfedges >= MAX_MAP_SURFEDGES ||
		brushes > MAX_MAP_BRUSHES || sides > MAX_MAP_BRUSHSIDES ||
		leafbrushes >= MAX_MAP_LEAFBRUSHES || leafs > MAX_MAP_LEAFS ||
		areas > MAX_MAP_AREAS || portals > MAX_MAP_AREAPORTALS)
		return "classic collision record count exceeds storage";
	if (header->lumps[LUMP_VISIBILITY].filelen)
	{
		p = file + header->lumps[LUMP_VISIBILITY].fileofs;
		if (header->lumps[LUMP_VISIBILITY].filelen < 4 ||
			header->lumps[LUMP_VISIBILITY].filelen > MAX_MAP_VISIBILITY)
			return "invalid classic visibility length";
		visclusters = R1BSP_ReadU32 (p);
		if (!CMod_QBSPValidVisibility (p,
				header->lumps[LUMP_VISIBILITY].filelen, visclusters))
			return "invalid classic visibility offset or RLE row";
	}

	p = file + header->lumps[LUMP_PLANES].fileofs;
	for (i = 0; i < planes; i++, p += sizeof (dplane_t))
	{
		for (j = 0; j < 4; j++)
		{
			value = R1BSP_ReadFloat (p + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				return "unsafe classic plane coordinate";
		}
		if (R1BSP_ReadU32 (p + 16) > 5)
			return "invalid classic plane type";
	}
	p = file + header->lumps[LUMP_TEXINFO].fileofs;
	for (i = 0; i < texinfos; i++, p += sizeof (texinfo_t))
	{
		texinfo = R1BSP_ReadI32 (p + offsetof (texinfo_t, nexttexinfo));
		if (texinfo > 0 && (uint32)texinfo >= texinfos)
			return "classic texture animation exceeds texture table";
	}
	if (!R1BSP_ValidTextureAnimations (
			file + header->lumps[LUMP_TEXINFO].fileofs, texinfos))
		return "classic texture animation enters unrelated cycle";
	p = file + header->lumps[LUMP_EDGES].fileofs;
	for (i = 0; i < edges; i++, p += sizeof (dedge_t))
	{
		if (R1BSP_ReadU16 (p) >= vertices ||
			R1BSP_ReadU16 (p + 2) >= vertices)
			return "classic edge vertex exceeds vertex lump";
	}
	p = file + header->lumps[LUMP_SURFEDGES].fileofs;
	for (i = 0; i < surfedges; i++, p += sizeof (int32))
	{
		surfedge = R1BSP_ReadI32 (p);
		if (surfedge == INT_MIN ||
			(uint32)(surfedge < 0 ? -surfedge : surfedge) >= edges)
			return "classic surfedge index exceeds edge lump";
	}
	p = file + header->lumps[LUMP_FACES].fileofs;
	for (i = 0; i < faces; i++, p += sizeof (dface_t))
	{
		firstedge = R1BSP_ReadI32 (p + offsetof (dface_t, firstedge));
		numedges = R1BSP_ReadI16 (p + offsetof (dface_t, numedges));
		texinfo = R1BSP_ReadI16 (p + offsetof (dface_t, texinfo));
		if (R1BSP_ReadU16 (p + offsetof (dface_t, planenum)) >= planes ||
			firstedge < 0 || numedges < 0 || texinfo < 0 ||
			(uint32)texinfo >= texinfos ||
			(uint64)(uint32)firstedge + (uint32)numedges > surfedges)
			return "classic face reference exceeds plane, texture or surfedge lump";
	}
	p = file + header->lumps[LUMP_LEAFFACES].fileofs;
	for (i = 0; i < leaffaces; i++, p += sizeof (uint16))
	{
		if (R1BSP_ReadU16 (p) >= faces)
			return "classic leaf face index exceeds face lump";
	}

	p = file + header->lumps[LUMP_NODES].fileofs;
	for (i = 0; i < nodes; i++, p += sizeof (dnode_t))
	{
		if (R1BSP_ReadU32 (p) >= planes)
			return "classic node plane exceeds plane lump";
		first = R1BSP_ReadU16 (p + offsetof (dnode_t, firstface));
		span = R1BSP_ReadU16 (p + offsetof (dnode_t, numfaces));
		if (first + span > faces)
			return "classic node face span exceeds lump";
	}
	graph_status = BSP38_ValidateNodeGraph (
		file + header->lumps[LUMP_NODES].fileofs, nodes, leafs, &graph_error);
	if (graph_status != BSP38_NODE_GRAPH_OK)
		return "invalid classic node child graph";

	p = file + header->lumps[LUMP_MODELS].fileofs;
	for (i = 0; i < models; i++, p += sizeof (dmodel_t))
	{
		for (j = 0; j < 9; j++)
		{
			value = R1BSP_ReadFloat (p + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				return "unsafe classic submodel coordinate";
		}
		headnode = R1BSP_ReadI32 (p + offsetof (dmodel_t, headnode));
		if ((headnode >= 0 && (uint32)headnode >= nodes) ||
			(headnode < 0 && headnode < -(int)leafs))
			return "classic submodel headnode exceeds node lump";
		first = R1BSP_ReadU32 (p + offsetof (dmodel_t, firstface));
		span = R1BSP_ReadU32 (p + offsetof (dmodel_t, numfaces));
		if ((uint64)first + span > faces)
			return "classic submodel face span exceeds lump";
	}

	p = file + header->lumps[LUMP_BRUSHES].fileofs;
	for (i = 0; i < brushes; i++, p += sizeof (dbrush_t))
	{
		first = R1BSP_ReadU32 (p);
		span = R1BSP_ReadU32 (p + 4);
		if ((uint64)first + span > sides)
			return "classic brush side span exceeds lump";
	}
	p = file + header->lumps[LUMP_BRUSHSIDES].fileofs;
	for (i = 0; i < sides; i++, p += sizeof (dbrushside_t))
	{
		plane = R1BSP_ReadU16 (p);
		texinfo = R1BSP_ReadI16 (p + 2);
		if (plane >= planes || texinfo >= (int)texinfos)
			return "classic brush side has invalid plane or texture";
	}
	p = file + header->lumps[LUMP_LEAFBRUSHES].fileofs;
	for (i = 0; i < leafbrushes; i++, p += sizeof (uint16))
	{
		brush = R1BSP_ReadU16 (p);
		if (brush >= brushes)
			return "classic leaf brush index exceeds brush lump";
	}
	p = file + header->lumps[LUMP_LEAFS].fileofs;
	for (i = 0; i < leafs; i++, p += sizeof (dleaf_t))
	{
		if (i == 0 && R1BSP_ReadI32 (p) != CONTENTS_SOLID)
			return "classic leaf zero is not solid";
		if (i && !R1BSP_ReadI32 (p))
			empty = true;
		cluster = R1BSP_ReadI16 (p + offsetof (dleaf_t, cluster));
		area = R1BSP_ReadI16 (p + offsetof (dleaf_t, area));
		if ((visclusters && cluster >= (int)visclusters) ||
			area < 0 || (uint32)area >= areas)
			return "classic leaf cluster or area exceeds table";
		first = R1BSP_ReadU16 (p + offsetof (dleaf_t, firstleafface));
		span = R1BSP_ReadU16 (p + offsetof (dleaf_t, numleaffaces));
		if (first + span > leaffaces)
			return "classic leaf face span exceeds lump";
		first = R1BSP_ReadU16 (p + offsetof (dleaf_t, firstleafbrush));
		span = R1BSP_ReadU16 (p + offsetof (dleaf_t, numleafbrushes));
		if (first + span > leafbrushes)
			return "classic leaf brush span exceeds lump";
	}
	if (!empty)
		return "classic map has no empty leaf";
	p = file + header->lumps[LUMP_AREAS].fileofs;
	for (i = 0; i < areas; i++, p += sizeof (darea_t))
	{
		first = R1BSP_ReadU32 (p + 4);
		span = R1BSP_ReadU32 (p);
		if ((uint64)first + span > portals)
			return "classic area portal span exceeds lump";
	}
	p = file + header->lumps[LUMP_AREAPORTALS].fileofs;
	for (i = 0; i < portals; i++, p += sizeof (dareaportal_t))
	{
		if (R1BSP_ReadU32 (p) >= MAX_MAP_AREAPORTALS ||
			R1BSP_ReadU32 (p + 4) >= areas)
			return "classic area portal has invalid reference";
	}
	return NULL;
}

/* CM_DecompressVis has no input-length argument. Validate every QBSP RLE row
 * before that function can be reached; repeated PVS/PHS offsets are checked
 * only once to keep large but bounded visibility tables inexpensive. */
static qboolean CMod_QBSPValidVisibility (const byte *vis, uint32 length,
	uint32 clusters)
{
	byte *checked;
	uint32 header_size, row, i, j, offset, pos, expanded, run, work = 0;
	qboolean valid = false;

	if (!clusters || clusters > MAX_MAP_LEAFS)
		return false;
	header_size = 4 + clusters * 8;
	if (length < header_size)
		return false;
	row = (clusters + 7) >> 3;
	checked = calloc (length, 1);
	if (!checked)
		return false;
	for (i = 0; i < clusters; i++)
	{
		for (j = 0; j < 2; j++)
		{
			offset = R1BSP_ReadU32 (vis + 4 + i * 8 + j * 4);
			if (offset < header_size || offset >= length)
				goto done;
			if (checked[offset])
				continue;
			pos = offset;
			expanded = 0;
			while (expanded < row)
			{
				if (pos >= length || work >= 16u * MAX_MAP_VISIBILITY)
					goto done;
				work++;
				if (vis[pos])
				{
					pos++;
					expanded++;
					continue;
				}
				if (pos + 1 >= length || work >= 16u * MAX_MAP_VISIBILITY)
					goto done;
				work++;
				run = vis[pos + 1];
				if (!run || run > row - expanded)
					goto done;
				pos += 2;
				expanded += run;
			}
			checked[offset] = 1;
		}
	}
	valid = true;
done:
	free (checked);
	return valid;
}

/* Admit only QBSP records representable by the existing collision arrays and
 * game/peer interface. This runs on the loaded bytes before checksum or
 * cmod_base publication; IBSP continues through its original loader path. */
static const char *CMod_ValidateQBSPMap (const byte *file,
	const r1bsp_disk_layout_t *layout)
{
	const byte *p, *vis;
	bsp38_node_graph_error_t graph_error;
	bsp38_node_graph_status_t graph_status;
	uint32 i, j, plane, texinfo, brush, cluster, area, first, span;
	uint32 visclusters = 0, maxcluster = 0;
	float value;
	qboolean empty = false;
	int shared_visible, nexttexinfo;
	if (!layout->count[LUMP_TEXINFO] || !layout->count[LUMP_PLANES] ||
		!layout->count[LUMP_LEAFS] || !layout->count[LUMP_NODES] ||
		!layout->count[LUMP_MODELS] || !layout->count[LUMP_LEAFBRUSHES] ||
		!layout->count[LUMP_AREAS] ||
		layout->count[LUMP_MODELS] > MAX_MODELS - 2)
		return "missing required lump or peer model limit exceeded";
	if (layout->count[LUMP_LEAFS] >= MAX_MAP_LEAFS ||
		layout->count[LUMP_PLANES] + 12 > MAX_MAP_PLANES ||
		layout->count[LUMP_NODES] + 6 > MAX_MAP_NODES ||
		layout->count[LUMP_BRUSHES] + 1 > MAX_MAP_BRUSHES ||
		layout->count[LUMP_BRUSHSIDES] + 6 > MAX_MAP_BRUSHSIDES ||
		layout->count[LUMP_LEAFBRUSHES] + 1 > MAX_MAP_LEAFBRUSHES)
		return "not enough room for collision box hull";
	if (layout->filelen[LUMP_VISIBILITY])
	{
		vis = file + layout->fileofs[LUMP_VISIBILITY];
		if (layout->filelen[LUMP_VISIBILITY] < 4)
			return "invalid visibility header, offset, or RLE row";
		visclusters = R1BSP_ReadU32 (vis);
		if (!CMod_QBSPValidVisibility (vis, layout->filelen[LUMP_VISIBILITY],
				visclusters))
			return "invalid visibility header, offset, or RLE row";
	}

	p = file + layout->fileofs[LUMP_PLANES];
	for (i = 0; i < layout->count[LUMP_PLANES]; i++, p += sizeof (dplane_t))
	{
		for (j = 0; j < 4; j++)
		{
			value = R1BSP_ReadFloat (p + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				return "unsafe plane coordinate";
		}
		if (R1BSP_ReadU32 (p + 16) > 5)
			return "invalid plane type";
	}

	p = file + layout->fileofs[LUMP_TEXINFO];
	for (i = 0; i < layout->count[LUMP_TEXINFO]; i++, p += sizeof (texinfo_t))
	{
		for (j = 0; j < 8; j++)
		{
			value = R1BSP_ReadFloat (p + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				return "unsafe texture coordinate";
		}
		if (!memchr (p + offsetof (texinfo_t, texture), 0,
				sizeof (((texinfo_t *)0)->texture)))
			return "unterminated texture name";
		nexttexinfo = R1BSP_ReadI32 (p + offsetof (texinfo_t, nexttexinfo));
		if (nexttexinfo > 0 &&
			(uint32)nexttexinfo >= layout->count[LUMP_TEXINFO])
			return "texture animation exceeds texture table";
	}
	if (!R1BSP_ValidTextureAnimations (file + layout->fileofs[LUMP_TEXINFO],
			layout->count[LUMP_TEXINFO]))
		return "texture animation enters unrelated cycle";

	p = file + layout->fileofs[LUMP_BRUSHES];
	for (i = 0; i < layout->count[LUMP_BRUSHES]; i++, p += sizeof (dbrush_t))
	{
		first = R1BSP_ReadU32 (p);
		span = R1BSP_ReadU32 (p + 4);
		if ((uint64)first + span > layout->count[LUMP_BRUSHSIDES])
			return "brush side span exceeds lump";
	}

	p = file + layout->fileofs[LUMP_BRUSHSIDES];
	for (i = 0; i < layout->count[LUMP_BRUSHSIDES];
		 i++, p += sizeof (qbsp_dbrushside_t))
	{
		plane = R1BSP_ReadU32 (p);
		texinfo = R1BSP_ReadU32 (p + 4);
		if (plane >= layout->count[LUMP_PLANES] ||
			(texinfo != (uint32)-1 && texinfo >= layout->count[LUMP_TEXINFO]))
			return "brush side has invalid plane or texture";
	}

	p = file + layout->fileofs[LUMP_LEAFBRUSHES];
	for (i = 0; i < layout->count[LUMP_LEAFBRUSHES];
		 i++, p += sizeof (qbsp_dleafbrush_t))
	{
		brush = R1BSP_ReadU32 (p);
		if (brush >= layout->count[LUMP_BRUSHES] || brush > 65535u)
			return "leaf brush index cannot fit collision storage";
	}

	p = file + layout->fileofs[LUMP_LEAFS];
	for (i = 0; i < layout->count[LUMP_LEAFS]; i++, p += sizeof (qbsp_dleaf_t))
	{
		if (R1BSP_ReadI32 (p) == -1)
			return "leaf contents uses internal-node sentinel";
		if (i == 0 && R1BSP_ReadI32 (p) != CONTENTS_SOLID)
			return "leaf zero is not solid";
		if (i && !R1BSP_ReadI32 (p))
			empty = true;
		cluster = R1BSP_QBSPLeafCluster (p, layout->filelen[LUMP_VISIBILITY] != 0);
		area = R1BSP_ReadU32 (p + 8);
		if (cluster != (uint32)-1)
		{
			if (layout->filelen[LUMP_VISIBILITY] && cluster >= visclusters)
				return "leaf cluster exceeds visibility table";
			if (cluster + 1 > maxcluster)
				maxcluster = cluster + 1;
		}
		if (area >= layout->count[LUMP_AREAS])
			return "leaf area exceeds area table";
		first = R1BSP_ReadU32 (p + 36);
		span = R1BSP_ReadU32 (p + 40);
		if ((uint64)first + span > layout->count[LUMP_LEAFFACES])
			return "leaf face span exceeds lump";
		first = R1BSP_ReadU32 (p + 44);
		span = R1BSP_ReadU32 (p + 48);
		if (first > 65535u || span > 65535u ||
			(uint64)first + span > layout->count[LUMP_LEAFBRUSHES])
			return "leaf brush span cannot fit collision storage";
	}
	if (!empty || (layout->filelen[LUMP_VISIBILITY] && maxcluster != visclusters))
		return "missing empty leaf or leaf/visibility cluster mismatch";

	p = file + layout->fileofs[LUMP_NODES];
	for (i = 0; i < layout->count[LUMP_NODES]; i++, p += sizeof (qbsp_dnode_t))
	{
		plane = R1BSP_ReadU32 (p);
		first = R1BSP_ReadU32 (p + 36);
		span = R1BSP_ReadU32 (p + 40);
		if (plane >= layout->count[LUMP_PLANES] ||
			first > USHRT_MAX || span > USHRT_MAX ||
			(uint64)first + span > layout->count[LUMP_FACES])
			return "node has invalid plane or face span";
	}
	graph_error.node = graph_error.side = graph_error.child = -1;
	graph_status = BSP38_ValidateNodeGraphLayoutLimit (
		file + layout->fileofs[LUMP_NODES], layout->count[LUMP_NODES],
		layout->count[LUMP_LEAFS], sizeof (qbsp_dnode_t),
		offsetof (qbsp_dnode_t, children), 256, &graph_error);
	if (graph_status != BSP38_NODE_GRAPH_OK)
		return graph_status == BSP38_NODE_GRAPH_TOO_DEEP ?
			"node child graph exceeds QBSP depth limit" :
			"invalid node child graph";
	shared_visible = R1BSP_HasSharedVisibleQBSPLeaf (file, layout);
	if (shared_visible)
		return shared_visible > 0 ? "shared visible QBSP leaf" :
									"cannot validate QBSP leaf parents";

	p = file + layout->fileofs[LUMP_MODELS];
	for (i = 0; i < layout->count[LUMP_MODELS]; i++, p += sizeof (dmodel_t))
	{
		uint32 headnode = R1BSP_ReadU32 (p + 36);
		for (j = 0; j < 9; j++)
		{
			value = R1BSP_ReadFloat (p + j * 4);
			if (!(value >= -1000000 && value <= 1000000))
				return "unsafe submodel coordinate";
		}
		first = R1BSP_ReadU32 (p + 40);
		span = R1BSP_ReadU32 (p + 44);
		if (headnode >= layout->count[LUMP_NODES] ||
			(i == 0 && headnode != 0) ||
			(uint64)first + span > layout->count[LUMP_FACES])
			return "submodel headnode or face span is invalid";
	}

	p = file + layout->fileofs[LUMP_AREAS];
	for (i = 0; i < layout->count[LUMP_AREAS]; i++, p += sizeof (darea_t))
	{
		span = R1BSP_ReadU32 (p);
		first = R1BSP_ReadU32 (p + 4);
		if ((uint64)first + span > layout->count[LUMP_AREAPORTALS])
			return "area portal span exceeds lump";
	}
	p = file + layout->fileofs[LUMP_AREAPORTALS];
	for (i = 0; i < layout->count[LUMP_AREAPORTALS];
		 i++, p += sizeof (dareaportal_t))
	{
		if (R1BSP_ReadU32 (p) >= layout->count[LUMP_AREAPORTALS] ||
			R1BSP_ReadU32 (p + 4) >= layout->count[LUMP_AREAS])
			return "area portal references invalid area or portal state";
	}
	return NULL;
}

/*
==================
CM_LoadMap

Loads in the map and all submodels
==================
*/
cmodel_t *CM_LoadMap (const char *name, qboolean clientload, uint32 *checksum)
{
	char			newname[MAX_QPATH];
	byte			*buf;
	int i, bad_lump = -1;
	dheader_t		header;
	r1bsp_disk_layout_t qbsp_layout;
	r1bsp_disk_status_t qbsp_status;
	const char *collision_error;
	qboolean qbsp = false;
	uint32			length;
	uint32			override_bits;
	static uint32	last_checksum;
	map_noareas = Cvar_Get ("map_noareas", "0", 0);

	if (!strcmp (map_name, name) && (clientload || !Cvar_IntValue ("flushmap")) )
	{
		*checksum = last_checksum;
		if (!clientload)
		{
			memset (portalopen, 0, sizeof(portalopen));
			FloodAreaConnections ();
		}
		return &map_cmodels[0];		// still have the right version
	}

	// free old stuff
	numplanes = 0;
	numnodes = 0;
	numleafs = 0;
	numcmodels = 0;
	numvisibility = 0;
	numentitychars = 0;

	//r1: fix for missing terminators on some badly compiled maps
	memset (map_entitystring, 0, sizeof(map_entitystring));
	memset (map_name, 0, sizeof(map_name));

	if (!name || !name[0])
	{
		numleafs = 1;
		numclusters = 1;
		numareas = 1;
		last_checksum = 0;
		*checksum = 0;
		return &map_cmodels[0];			// cinematic servers won't have anything at all
	}

	//
	// load the file
	//

	override_bits = 0;

	//r1: allow transparent server-side map entity replacement
	if (!clientload)
	{
		FILE			*script;
		char			csname[MAX_QPATH];
		qboolean		closeFile;

		Com_sprintf (csname, sizeof(csname), "%s.override", name);
		
		FS_FOpenFile (csname, &script, HANDLE_OPEN, &closeFile);

		if (script)
		{
			Com_Printf ("Using override file: %s\n", LOG_GENERAL, name);
			FS_Read (&override_bits, sizeof(override_bits), script);

			if (override_bits & 1)
				FS_Read (newname, sizeof(newname), script);

			if (override_bits & 2)
				FS_Read (&last_checksum, sizeof(last_checksum), script);

			if (override_bits & 4)
			{
				FS_Read (&length, sizeof(length), script);
				if (!length || length >= MAX_MAP_ENTSTRING)
				{
					FS_FCloseFile (script);
					Com_Error (ERR_DROP, "CM_LoadMap: bad entity string size %u", length);
				}
				FS_Read (map_entitystring, length, script);
			}

			if (closeFile)
				FS_FCloseFile (script);
			name = newname;
		}
	}

	//FIXME: make this work clientside too... ugly fs hacks needed i guess :/
	/*length = 0;

#ifndef NO_ZLIB
	{
		char		gzName[MAX_QPATH];
		FILE		*f;
		gzFile		compressed;
		qboolean	closeHandle;

		Com_sprintf (gzName, sizeof(gzName), "%s.gz", name);
		//compressed = gzopen (gzName, "rb");

		length = FS_FOpenFile (gzName, &f, HANDLE_OPEN, &closeHandle);
		if (length != -1)
		{
			int fd;

			fseek (f, -4, SEEK_END);
			fread (&length, sizeof(length), 1, f);
			rewind (f);

			fd = fileno (f);

			compressed = gzdopen (_dup(fd), "rb");
			if (compressed)
			{
				//length = gzseek (compressed, 0, SEEK_END);
				//gzrewind (compressed);
				buf = Z_TagMalloc (length, TAGMALLOC_FSLOADFILE);
				if (gzread (compressed, buf, length) == -1)
					length = 0;
				gzclose (compressed);
			}

			if (closeHandle)
				fclose (f);
		}
		else
			length = 0;
	}
#endif

	if (!length)*/
		length = FS_LoadFile (name, (void **)&buf);

	if (!buf)
	{
		if (!developer->intvalue || !clientload)
		{
			//r1: ugly, but flush fs cache in case they install it (now with right place!)
			FS_FlushCache ();
			Com_Error (ERR_DROP, "Couldn't load %s", name);
		}
		else
		{
			Com_Printf ("WARNING: Map not found, connecting in null map mode!\n", LOG_GENERAL);
			numleafs = 1;
			numclusters = 1;
			numareas = 1;
			last_checksum = 0;
			*checksum = 0;
			return &map_cmodels[0];
		}
	}

	if (length >= 4 && R1BSP_ReadU32 (buf) == IDQBSPHEADER)
	{
		qbsp = true;
		qbsp_status = R1BSP_Preflight (buf, length, &qbsp_layout);
		if (qbsp_status != R1BSP_DISK_OK)
		{
			FS_FreeFile (buf);
			Com_Error (ERR_DROP, "CM_LoadMap: %s has invalid QBSP disk layout (%d, lump %d)",
				name, qbsp_status, qbsp_layout.bad_lump);
			return NULL;
		}
		collision_error = CMod_ValidateQBSPMap (buf, &qbsp_layout);
		if (collision_error)
		{
			FS_FreeFile (buf);
			Com_Error (ERR_DROP, "CM_LoadMap: %s has unsupported QBSP collision data: %s",
				name, collision_error);
			return NULL;
		}
		memset (&header, 0, sizeof (header));
		header.ident = IDQBSPHEADER;
		header.version = BSPVERSION;
		for (i = 0; i < MAX_LUMPS; i++)
		{
			if (i == LUMP_POP)
				continue;
			header.lumps[i].fileofs = qbsp_layout.fileofs[i];
			header.lumps[i].filelen = qbsp_layout.filelen[i];
		}
	}
	else
	{
		i = CMod_DecodeBSP38Header (buf, length, &header, &bad_lump);
		if (i != CM_BSP38_OK)
		{
			FS_FreeFile (buf);
			if (i == CM_BSP38_SHORT)
				Com_Error (ERR_DROP, "CM_LoadMap: %s has a truncated BSP38 header", name);
			else if (i == CM_BSP38_IDENT)
				Com_Error (ERR_DROP, "CM_LoadMap: %s is not an IBSP map", name);
			else if (i == CM_BSP38_VERSION)
				Com_Error (ERR_DROP, "CM_LoadMap: %s has unsupported BSP version %d", name, header.version);
			else
				Com_Error (ERR_DROP, "CM_LoadMap: %s has invalid BSP lump %d (offset %d, size %d)",
					name, bad_lump, header.lumps[bad_lump].fileofs,
					header.lumps[bad_lump].filelen);
			return NULL;
		}
		collision_error = CMod_ValidateIBSPCollisionRecords (buf, &header);
		if (collision_error)
		{
			FS_FreeFile (buf);
			Com_Error (ERR_DROP, "CM_LoadMap: %s has invalid IBSP collision data: %s",
				name, collision_error);
			return NULL;
		}
	}

	if (!(override_bits & 2))
		last_checksum = LittleLong (Com_BlockChecksum (buf, length));
	*checksum = last_checksum;
	cmod_base = (byte *)buf;
	cmod_size = length;

	// load into heap
	// FIXME: any of these functions can Com_Error, we need to free buf.
	if (qbsp)
	{
		CMod_LoadSurfaces (&header.lumps[LUMP_TEXINFO]);
		CMod_LoadQBSPPlanes (&header.lumps[LUMP_PLANES]);
		CMod_LoadBrushes (&header.lumps[LUMP_BRUSHES]);
		CMod_LoadQBSPLeafBrushes (&header.lumps[LUMP_LEAFBRUSHES]);
		CMod_LoadQBSPBrushSides (&header.lumps[LUMP_BRUSHSIDES]);
		CMod_LoadAreas (&header.lumps[LUMP_AREAS]);
		CMod_LoadAreaPortals (&header.lumps[LUMP_AREAPORTALS]);
		CMod_LoadVisibility (&header.lumps[LUMP_VISIBILITY]);
		CMod_LoadQBSPLeafs (&header.lumps[LUMP_LEAFS],
			header.lumps[LUMP_VISIBILITY].filelen != 0);
		CMod_LoadQBSPNodes (&header.lumps[LUMP_NODES]);
		CMod_LoadSubmodels (&header.lumps[LUMP_MODELS]);
	}
	else
	{
		CMod_LoadSurfaces (&header.lumps[LUMP_TEXINFO]);
		CMod_LoadLeafs (&header.lumps[LUMP_LEAFS]);
		CMod_LoadLeafBrushes (&header.lumps[LUMP_LEAFBRUSHES]);
		CMod_LoadPlanes (&header.lumps[LUMP_PLANES]);
		CMod_LoadBrushes (&header.lumps[LUMP_BRUSHES]);
		CMod_LoadBrushSides (&header.lumps[LUMP_BRUSHSIDES]);
		CMod_LoadSubmodels (&header.lumps[LUMP_MODELS]);
		CMod_LoadNodes (&header.lumps[LUMP_NODES]);
		CMod_LoadAreas (&header.lumps[LUMP_AREAS]);
		CMod_LoadAreaPortals (&header.lumps[LUMP_AREAPORTALS]);
		CMod_LoadVisibility (&header.lumps[LUMP_VISIBILITY]);
	}

	if (!(override_bits & 4))
		CMod_LoadEntityString (&header.lumps[LUMP_ENTITIES]);

	Z_Free (buf);

	CM_InitBoxHull ();

	memset (portalopen, 0, sizeof(portalopen));
	FloodAreaConnections ();

	strcpy (map_name, name);

	return &map_cmodels[0];
}

const char *CM_MapName (void)
{
	return map_name;
}

/*
==================
CM_InlineModel
==================
*/
cmodel_t	*CM_InlineModel (const char *name)
{
	int		num;

	if (!name || name[0] != '*')
		Com_Error (ERR_DROP, "CM_InlineModel: bad name: %s", name);
	num = atoi (name+1);
	if (num < 1 || num >= numcmodels)
		Com_Error (ERR_DROP, "CM_InlineModel: bad number: %d (%d total cmodels)", num, numcmodels);

	return &map_cmodels[num];
}

/*__inline int	CM_NumClusters (void)
{
	return numclusters;
}

__inline int	CM_NumInlineModels (void)
{
	return numcmodels;
}*/

char	*CM_EntityString (void)
{
	return map_entitystring;
}

/*int		CM_LeafContents (int leafnum)
{
	if (leafnum < 0 || leafnum >= numleafs)
		Com_Error (ERR_DROP, "CM_LeafContents: bad number");
	return map_leafs[leafnum].contents;
}*/

/*
__inline int	CM_LeafCluster (int leafnum)
{
#ifndef DEDICATED_ONLY
	if (leafnum < 0 || leafnum >= numleafs)
		Com_Error (ERR_DROP, "CM_LeafCluster: bad number");
#endif
	return map_leafs[leafnum].cluster;
}

__inline int	 CM_LeafArea (int leafnum)
{
#ifndef DEDICATED_ONLY
	if (leafnum < 0 || leafnum >= numleafs)
		Com_Error (ERR_DROP, "CM_LeafArea: bad number");
#endif
	return map_leafs[leafnum].area;
}*/

//=======================================================================


fplane_t	*fbox_planes;
cplane_t	*box_planes;
int			box_headnode;
cbrush_t	*box_brush;
cleaf_t		*box_leaf;

/*
===================
CM_InitBoxHull

Set up the planes and nodes so that the six floats of a bounding box
can just be stored out and get a proper clipping hull structure.
===================
*/
void CM_InitBoxHull (void)
{
	int			i;
	int			side;
	cnode_t		*c;
	cplane_t	*p;
	fplane_t	*f;
	cbrushside_t	*s;

	box_headnode = numnodes;
	box_planes = &map_planes[numplanes];
	fbox_planes = &map_fastplanes[numplanes];

	if (numnodes+6 > MAX_MAP_NODES
		|| numbrushes+1 > MAX_MAP_BRUSHES
		|| numleafbrushes+1 > MAX_MAP_LEAFBRUSHES
		|| numbrushsides+6 > MAX_MAP_BRUSHSIDES
		|| numplanes+12 > MAX_MAP_PLANES)
		Com_Error (ERR_DROP, "Not enough room for box tree");

	box_brush = &map_brushes[numbrushes];
	box_brush->numsides = 6;
	box_brush->firstbrushside = numbrushsides;
	box_brush->contents = CONTENTS_MONSTER;

	box_leaf = &map_leafs[numleafs];
	box_leaf->contents = CONTENTS_MONSTER;
	box_leaf->firstleafbrush = numleafbrushes;
	box_leaf->numleafbrushes = 1;

	map_leafbrushes[numleafbrushes] = numbrushes;

	for (i=0 ; i<6 ; i++)
	{
		side = i&1;

		// brush sides
		s = &map_brushsides[numbrushsides+i];
		s->plane = 	map_planes + (numplanes+i*2+side);
		s->surface = &nullsurface;

		// nodes
		c = &map_nodes[box_headnode+i];
		c->plane = map_planes + (numplanes+i*2);
		c->fastplane = map_fastplanes + (numplanes+i*2);
		c->children[side] = -1 - emptyleaf;

		if (i != 5)
			c->children[side^1] = box_headnode+i + 1;
		else
			c->children[side^1] = -1 - numleafs;

		// planes
		p = &box_planes[i*2];
		p->type = i>>1;
		p->signbits = 0;
		VectorClear (p->normal);
		p->normal[i>>1] = 1;

		p = &box_planes[i*2+1];
		p->type = 3 + (i>>1);
		p->signbits = 0;
		VectorClear (p->normal);
		p->normal[i>>1] = -1;

		f = &fbox_planes[i*2];
		f->type = i>>1;
		f->signbits = 0;
		VectorClear (f->normal);
		f->normal[i>>1] = 1;

		f = &fbox_planes[i*2+1];
		f->type = 3 + (i>>1);
		f->signbits = 0;
		VectorClear (f->normal);
		f->normal[i>>1] = -1;
	}	
}


/*
===================
CM_HeadnodeForBox

To keep everything totally uniform, bounding boxes are turned into small
BSP trees instead of being compared directly.
===================
*/
int	CM_HeadnodeForBox (const vec3_t mins, const vec3_t maxs)
{
	box_planes[0].dist = fbox_planes[0].dist = maxs[0];
	box_planes[1].dist = fbox_planes[1].dist = -maxs[0];
	box_planes[2].dist = fbox_planes[2].dist = mins[0];
	box_planes[3].dist = fbox_planes[3].dist = -mins[0];
	box_planes[4].dist = fbox_planes[4].dist = maxs[1];
	box_planes[5].dist = fbox_planes[5].dist = -maxs[1];
	box_planes[6].dist = fbox_planes[6].dist = mins[1];
	box_planes[7].dist = fbox_planes[7].dist = -mins[1];
	box_planes[8].dist = fbox_planes[8].dist = maxs[2];
	box_planes[9].dist = fbox_planes[9].dist = -maxs[2];
	box_planes[10].dist = fbox_planes[10].dist = mins[2];
	box_planes[11].dist = fbox_planes[11].dist = -mins[2];

	return box_headnode;
}


/*
==================
CM_PointLeafnum_r

==================
*/
int CM_PointLeafnum_r (const vec3_t p, int num)
{
	float		d;
	cnode_t		*node;
	fplane_t	*plane;

	while (num >= 0)
	{
		node = &map_nodes[num];
		plane = node->fastplane;
		
		if (plane->type < 3)
			d = p[plane->type] - plane->dist;
		else
			d = DotProduct (plane->normal, p) - plane->dist;
		if (FLOAT_LT_ZERO(d))
			num = node->children[1];
		else
			num = node->children[0];
	}

#ifndef DEDICATED_ONLY
	c_pointcontents++;		// optimize counter
#endif

	return -1 - num;
}

int CM_PointLeafnum (const vec3_t p)
{
	if (!numplanes)
		return 0;		// sound may call this without map loaded
	return CM_PointLeafnum_r (p, 0);
}



/*
=============
CM_BoxLeafnums

Fills in a list of all the leafs touched
=============
*/
int		leaf_count, leaf_maxcount;
int		*leaf_list;
float	*leaf_mins, *leaf_maxs;
int		leaf_topnode;

void CM_BoxLeafnums_r (int nodenum)
{
	cplane_t	*plane;
	cnode_t		*node;
	int			s;

	for (;;)
	{
		if (nodenum < 0)
		{
			if (leaf_count >= leaf_maxcount)
			{
				return;
			}
			leaf_list[leaf_count++] = -1 - nodenum;
			return;
		}
	
		node = &map_nodes[nodenum];
		plane = node->plane;
//		s = BoxOnPlaneSide (leaf_mins, leaf_maxs, plane);
		s = BOX_ON_PLANE_SIDE(leaf_mins, leaf_maxs, plane);
		if (s == 1)
			nodenum = node->children[0];
		else if (s == 2)
			nodenum = node->children[1];
		else
		{	// go down both
			if (leaf_topnode == -1)
				leaf_topnode = nodenum;
			CM_BoxLeafnums_r (node->children[0]);
			nodenum = node->children[1];
		}

	}
}

int	CM_BoxLeafnums_headnode (vec3_t mins, vec3_t maxs, int *list, int listsize, int headnode, int /*@null@*/*topnode)
{
	leaf_list = list;
	leaf_count = 0;
	leaf_maxcount = listsize;
	leaf_mins = mins;
	leaf_maxs = maxs;

	leaf_topnode = -1;

	CM_BoxLeafnums_r (headnode);

	if (topnode)
		*topnode = leaf_topnode;

	return leaf_count;
}

int	CM_BoxLeafnums (vec3_t mins, vec3_t maxs, int *list, int listsize, int /*@null@*/*topnode)
{
	return CM_BoxLeafnums_headnode (mins, maxs, list,
		listsize, map_cmodels[0].headnode, topnode);
}



/*
==================
CM_PointContents

==================
*/
int CM_PointContents (const vec3_t p, int headnode)
{
	if (!numnodes)	// map not loaded
		return 0;

	return map_leafs[CM_PointLeafnum_r (p, headnode)].contents;
}

/*
==================
CM_TransformedPointContents

Handles offseting and rotation of the end points for moving and
rotating entities
==================
*/
int	CM_TransformedPointContents (vec3_t p, int headnode, vec3_t origin, vec3_t angles)
{
	vec3_t		p_l;
	vec3_t		temp;
	vec3_t		forward, right, up;
	int			l;

	// subtract origin offset
	VectorSubtract (p, origin, p_l);

	// rotate start and end into the models frame of reference
	if (headnode != box_headnode && 
	(FLOAT_NE_ZERO(angles[0]) || FLOAT_NE_ZERO(angles[1]) || FLOAT_NE_ZERO(angles[2])) )
	{
		AngleVectors (angles, forward, right, up);

		FastVectorCopy (p_l, temp);
		p_l[0] = DotProduct (temp, forward);
		p_l[1] = -DotProduct (temp, right);
		p_l[2] = DotProduct (temp, up);
	}

	l = CM_PointLeafnum_r (p_l, headnode);

	return map_leafs[l].contents;
}


/*
===============================================================================

BOX TRACING

===============================================================================
*/

// 1/32 epsilon to keep floating point happy
#define	DIST_EPSILON	(0.03125f)

vec3_t	trace_start, trace_end;
vec3_t	trace_mins, trace_maxs;
vec3_t	trace_extents;

trace_t	trace_trace;
int		trace_contents;
qboolean	trace_ispoint;		// optimized case

/*
================
CM_ClipBoxToBrush
================
*/
void CM_ClipBoxToBrush (vec3_t mins, vec3_t maxs, vec3_t p1, vec3_t p2,
					  trace_t *trace, cbrush_t *brush)
{
	int			i, j;
	cplane_t	*plane, *clipplane;
	float		dist;
	float		enterfrac, leavefrac;
	vec3_t		ofs;
	float		d1, d2;
	qboolean	getout, startout;
	float		f;
	cbrushside_t	*side, *leadside;

	enterfrac = -1;
	leavefrac = 1;
	clipplane = NULL;

	if (!brush->numsides)
		return;

#ifndef DEDICATED_ONLY
	c_brush_traces++;
#endif

	getout = false;
	startout = false;
	leadside = NULL;

	for (i=0 ; i<brush->numsides ; i++)
	{
		side = &map_brushsides[brush->firstbrushside+i];
		plane = side->plane;

		// FIXME: special case for axial

		if (!trace_ispoint)
		{	// general box case

			// push the plane out apropriately for mins/maxs

			// FIXME: use signbits into 8 way lookup for each mins/maxs
			for (j=0 ; j<3 ; j++)
			{
				if (FLOAT_LT_ZERO(plane->normal[j]))
					ofs[j] = maxs[j];
				else
					ofs[j] = mins[j];
			}
			dist = DotProduct (ofs, plane->normal);
			dist = plane->dist - dist;
		}
		else
		{	// special point case
			dist = plane->dist;
		}

		d1 = DotProduct (p1, plane->normal) - dist;
		d2 = DotProduct (p2, plane->normal) - dist;

		if (FLOAT_GT_ZERO(d2))
			getout = true;	// endpoint is not in solid

		if (FLOAT_GT_ZERO(d1))
			startout = true;

		// if completely in front of face, no intersection
		if (FLOAT_GT_ZERO(d1) && d2 >= d1)
			return;

		if (FLOAT_LE_ZERO (d1) && FLOAT_LE_ZERO(d2))
			continue;

		// crosses face
		if (d1 > d2)
		{	// enter
			f = (d1-DIST_EPSILON) / (d1-d2);
			if (f > enterfrac)
			{
				enterfrac = f;
				clipplane = plane;
				leadside = side;
			}
		}
		else
		{	// leave
			f = (d1+DIST_EPSILON) / (d1-d2);
			if (f < leavefrac)
				leavefrac = f;
		}
	}

	if (!startout)
	{	// original point was inside brush
		trace->startsolid = true;
		if (!getout)
			trace->allsolid = true;
		return;
	}
	if (enterfrac < leavefrac)
	{
		if (enterfrac > -1 && enterfrac < trace->fraction)
		{
			if (FLOAT_LT_ZERO(enterfrac))
				enterfrac = 0;
			
			if (!leadside)
				Com_Error (ERR_DROP, "CM_ClipBoxToBrush: missing leadside");

			trace->fraction = enterfrac;
			trace->plane = *clipplane;
			trace->surface = &(leadside->surface->c);
			trace->contents = brush->contents;
		}
	}
}

/*
================
CM_TestBoxInBrush
================
*/
void CM_TestBoxInBrush (vec3_t mins, vec3_t maxs, vec3_t p1,
					  trace_t *trace, cbrush_t *brush)
{
	int			i, j;
	cplane_t	*plane;
	float		dist;
	vec3_t		ofs;
	float		d1;
	cbrushside_t	*side;

	if (!brush->numsides)
		return;

	for (i=0 ; i<brush->numsides ; i++)
	{
		side = &map_brushsides[brush->firstbrushside+i];
		plane = side->plane;

		// FIXME: special case for axial

		// general box case

		// push the plane out apropriately for mins/maxs

		// FIXME: use signbits into 8 way lookup for each mins/maxs
		for (j=0 ; j<3 ; j++)
		{
			if (FLOAT_LT_ZERO(plane->normal[j]))
				ofs[j] = maxs[j];
			else
				ofs[j] = mins[j];
		}
		dist = DotProduct (ofs, plane->normal);
		dist = plane->dist - dist;

		d1 = DotProduct (p1, plane->normal) - dist;

		// if completely in front of face, no intersection
		if (d1 > 0)
			return;

	}

	// inside this brush
	trace->startsolid = trace->allsolid = true;
	trace->fraction = 0;
	trace->contents = brush->contents;
}


/*
================
CM_TraceToLeaf
================
*/
void CM_TraceToLeaf (int leafnum)
{
	int			k;
	int			brushnum;
	cleaf_t		*leaf;
	cbrush_t	*b;

	leaf = &map_leafs[leafnum];
	if ( !(leaf->contents & trace_contents))
		return;
	// trace line against all brushes in the leaf
	for (k=0 ; k<leaf->numleafbrushes ; k++)
	{
		brushnum = map_leafbrushes[leaf->firstleafbrush+k];
		b = &map_brushes[brushnum];
		if (b->checkcount == checkcount)
			continue;	// already checked this brush in another leaf
		b->checkcount = checkcount;

		if ( !(b->contents & trace_contents))
			continue;
		CM_ClipBoxToBrush (trace_mins, trace_maxs, trace_start, trace_end, &trace_trace, b);
		if (FLOAT_EQ_ZERO (trace_trace.fraction))
			return;
	}

}


/*
================
CM_TestInLeaf
================
*/
void CM_TestInLeaf (int leafnum)
{
	int			k;
	int			brushnum;
	cleaf_t		*leaf;
	cbrush_t	*b;

	leaf = &map_leafs[leafnum];
	if ( !(leaf->contents & trace_contents))
		return;
	// trace line against all brushes in the leaf
	for (k=0 ; k<leaf->numleafbrushes ; k++)
	{
		brushnum = map_leafbrushes[leaf->firstleafbrush+k];
		b = &map_brushes[brushnum];
		if (b->checkcount == checkcount)
			continue;	// already checked this brush in another leaf
		b->checkcount = checkcount;

		if ( !(b->contents & trace_contents))
			continue;
		CM_TestBoxInBrush (trace_mins, trace_maxs, trace_start, &trace_trace, b);
		if (FLOAT_EQ_ZERO(trace_trace.fraction))
			return;
	}

}

//int recursions = 0;
/*
==================
CM_RecursiveHullCheck

==================
*/
void CM_RecursiveHullCheck (int num, float p1f, float p2f, vec3_t p1, vec3_t p2)
{
	cnode_t		*node;
	fplane_t	*plane;
	float		t1, t2, offset;
	float		frac, frac2;
	float		idist;
	vec3_t		mid;
	int			side;
	float		midf;

	if (trace_trace.fraction <= p1f)
		return;		// already hit something nearer

	//if (++recursions == 512)
	//	Com_Error (ERR_DROP, "CM_RecursiveHullCheck: too many recursions!!");

	// if < 0, we are in a leaf node
	if (num < 0)
	{
		CM_TraceToLeaf (-1-num);
		return;
	}

	//
	// find the point distances to the seperating plane
	// and the offset for the size of the box
	//
	node = &map_nodes[num];
	plane = node->fastplane;

	//if (node->plane->type != plane->type || node->plane->dist != plane->dist || !VectorCompare (node->plane->normal, plane->normal))
	//	Sys_DebugBreak ();

	if (plane->type < 3)
	{
		t1 = p1[plane->type] - plane->dist;
		t2 = p2[plane->type] - plane->dist;
		offset = trace_extents[plane->type];
	}
	else
	{
		t1 = DotProduct (plane->normal, p1) - plane->dist;
		t2 = DotProduct (plane->normal, p2) - plane->dist;
		if (trace_ispoint)
			offset = 0;
		else
			offset = (float)fabs(trace_extents[0]*plane->normal[0]) +
				(float)fabs(trace_extents[1]*plane->normal[1]) +
				(float)fabs(trace_extents[2]*plane->normal[2]);
	}


#if 0
CM_RecursiveHullCheck (node->children[0], p1f, p2f, p1, p2);
CM_RecursiveHullCheck (node->children[1], p1f, p2f, p1, p2);
return;
#endif

	// see which sides we need to consider
	if (t1 >= offset && t2 >= offset)
	{
		CM_RecursiveHullCheck (node->children[0], p1f, p2f, p1, p2);
		return;
	}
	if (t1 < -offset && t2 < -offset)
	{
		CM_RecursiveHullCheck (node->children[1], p1f, p2f, p1, p2);
		return;
	}

	// put the crosspoint DIST_EPSILON pixels on the near side
	if (t1 < t2)
	{
		idist = 1.0f/(t1-t2);
		side = 1;
		frac2 = (t1 + offset + DIST_EPSILON)*idist;
		frac = (t1 - offset + DIST_EPSILON)*idist;
	}
	else if (t1 > t2)
	{
		idist = 1.0f/(t1-t2);
		side = 0;
		frac2 = (t1 - offset - DIST_EPSILON)*idist;
		frac = (t1 + offset + DIST_EPSILON)*idist;
	}
	else
	{
		side = 0;
		frac = 1;
		frac2 = 0;
	}

	// move up to the node
	if (FLOAT_LT_ZERO(frac))
		frac = 0;

	if (frac > 1)
		frac = 1;
		
	midf = p1f + (p2f - p1f)*frac;

	mid[0] = p1[0] + frac*(p2[0] - p1[0]);
	mid[1] = p1[1] + frac*(p2[1] - p1[1]);
	mid[2] = p1[2] + frac*(p2[2] - p1[2]);

	CM_RecursiveHullCheck (node->children[side], p1f, midf, p1, mid);


	// go past the node
	if (FLOAT_LT_ZERO(frac2))
		frac2 = 0;

	if (frac2 > 1)
		frac2 = 1;
		
	midf = p1f + (p2f - p1f)*frac2;
	mid[0] = p1[0] + frac2*(p2[0] - p1[0]);
	mid[1] = p1[1] + frac2*(p2[1] - p1[1]);
	mid[2] = p1[2] + frac2*(p2[2] - p1[2]);

	//if (trace_trace.fraction > midf)
		CM_RecursiveHullCheck (node->children[side^1], midf, p2f, mid, p2);
}



//======================================================================

/*
==================
CM_BoxTrace
==================
*/
trace_t		CM_BoxTrace (vec3_t start, vec3_t end,
						  vec3_t mins, vec3_t maxs,
						  int headnode, int brushmask)
{
	checkcount++;		// for multi-check avoidance

#ifndef DEDICATED_ONLY
	c_traces++;			// for statistics, may be zeroed
#endif

	// fill in a default trace
	memset (&trace_trace, 0, sizeof(trace_trace));

	trace_trace.fraction = 1;
	trace_trace.surface = &(nullsurface.c);

	/*trace_trace.allsolid = false;
	trace_trace.startsolid = false;
	trace_trace.fraction = 1;
	VectorClear (trace_trace.endpos);

	memset (&trace_trace.plane, 0, sizeof(trace_trace.plane));
	trace_trace.surface = &(nullsurface.c);
	trace_trace.contents = 0;
	trace_trace.ent = NULL;*/


	if (!numnodes)	// map not loaded
		return trace_trace;

	trace_contents = brushmask;
	FastVectorCopy (*start, trace_start);
	FastVectorCopy (*end, trace_end);
	FastVectorCopy (*mins, trace_mins);
	FastVectorCopy (*maxs, trace_maxs);

	//
	// check for position test special case
	//
	if (start[0] == end[0] && start[1] == end[1] && start[2] == end[2])
	{
		int		leafs[1024];
		int		i, numleafs;
		vec3_t	c1, c2;
		int		topnode;

		VectorAdd (start, mins, c1);
		VectorAdd (start, maxs, c2);

		c1[0] -= 1;
		c2[0] += 1;
		c1[1] -= 1;
		c2[1] += 1;
		c1[2] -= 1;
		c2[2] += 1;

		numleafs = CM_BoxLeafnums_headnode (c1, c2, leafs, 1024, headnode, &topnode);
		for (i=0 ; i<numleafs ; i++)
		{
			CM_TestInLeaf (leafs[i]);
			if (trace_trace.allsolid)
				break;
		}
		FastVectorCopy (*start, trace_trace.endpos);
		return trace_trace;
	}

	//
	// check for point special case
	//
	if (FLOAT_EQ_ZERO(mins[0]) && FLOAT_EQ_ZERO(mins[1]) && FLOAT_EQ_ZERO(mins[2])
		&& FLOAT_EQ_ZERO(maxs[0]) && FLOAT_EQ_ZERO(maxs[1]) && FLOAT_EQ_ZERO(maxs[2]))
	{
		trace_ispoint = true;
		VectorClear (trace_extents);
	}
	else
	{
		trace_ispoint = false;
		trace_extents[0] = -mins[0] > maxs[0] ? -mins[0] : maxs[0];
		trace_extents[1] = -mins[1] > maxs[1] ? -mins[1] : maxs[1];
		trace_extents[2] = -mins[2] > maxs[2] ? -mins[2] : maxs[2];
	}

	//
	// general sweeping through world
	//
	//recursions = 0;
	CM_RecursiveHullCheck (headnode, 0, 1, start, end);

	if (trace_trace.fraction == 1.0f)
	{
		FastVectorCopy (*end, trace_trace.endpos);
	}
	else
	{
		trace_trace.endpos[0] = start[0] + trace_trace.fraction * (end[0] - start[0]);
		trace_trace.endpos[1] = start[1] + trace_trace.fraction * (end[1] - start[1]);
		trace_trace.endpos[2] = start[2] + trace_trace.fraction * (end[2] - start[2]);
	}
	return trace_trace;
}


/*
==================
CM_TransformedBoxTrace

Handles offseting and rotation of the end points for moving and
rotating entities
==================
*/
//#ifdef _WIN32
//#pragma optimize( "", off )
//#endif


trace_t		CM_TransformedBoxTrace (vec3_t start, vec3_t end,
						  vec3_t mins, vec3_t maxs,
						  int headnode, int brushmask,
						  vec3_t origin, vec3_t angles)
{
	trace_t		trace;
	vec3_t		start_l, end_l;
	vec3_t		a;
	vec3_t		forward, right, up;
	vec3_t		temp;
	qboolean	rotated;

	// subtract origin offset
	VectorSubtract (start, origin, start_l);
	VectorSubtract (end, origin, end_l);

	// rotate start and end into the models frame of reference
	if (headnode != box_headnode && 
	(angles[0] || angles[1] || angles[2]) )
		rotated = true;
	else
		rotated = false;

	if (rotated)
	{
		AngleVectors (angles, forward, right, up);

		FastVectorCopy (start_l, temp);
		start_l[0] = DotProduct (temp, forward);
		start_l[1] = -DotProduct (temp, right);
		start_l[2] = DotProduct (temp, up);

		FastVectorCopy (end_l, temp);
		end_l[0] = DotProduct (temp, forward);
		end_l[1] = -DotProduct (temp, right);
		end_l[2] = DotProduct (temp, up);
	}

	// sweep the box through the model
	trace = CM_BoxTrace (start_l, end_l, mins, maxs, headnode, brushmask);

	if (rotated && trace.fraction != 1.0f)
	{
		// FIXME: figure out how to do this with existing angles
		VectorNegate (angles, a);
		AngleVectors (a, forward, right, up);

		FastVectorCopy (trace.plane.normal, temp);
		trace.plane.normal[0] = DotProduct (temp, forward);
		trace.plane.normal[1] = -DotProduct (temp, right);
		trace.plane.normal[2] = DotProduct (temp, up);
	}

	trace.endpos[0] = start[0] + trace.fraction * (end[0] - start[0]);
	trace.endpos[1] = start[1] + trace.fraction * (end[1] - start[1]);
	trace.endpos[2] = start[2] + trace.fraction * (end[2] - start[2]);

	return trace;
}

//#ifdef _WIN32
//#pragma optimize( "", on )
//#endif


/*
===============================================================================

PVS / PHS

===============================================================================
*/

/*
===================
CM_DecompressVis
===================
*/
void CM_DecompressVis (byte *in, byte *out)
{
	int		c;
	byte	*out_p;
	int		row;

	row = (numclusters+7)>>3;	
	out_p = out;

	if (!in || !numvisibility)
	{	// no vis info, so make all visible
		while (row)
		{
			*out_p++ = 0xff;
			row--;
		}
		return;		
	}

	do
	{
		if (*in)
		{
			*out_p++ = *in++;
			continue;
		}
	
		c = in[1];
		in += 2;
		if ((out_p - out) + c > row)
		{
			c = row - (int)(out_p - out);
			Com_DPrintf ("warning: Vis decompression overrun\n");
		}
		while (c)
		{
			*out_p++ = 0;
			c--;
		}
	} while (out_p - out < row);
}

byte	pvsrow[MAX_MAP_LEAFS/8];
byte	phsrow[MAX_MAP_LEAFS/8];

byte	*CM_ClusterPVS (int cluster)
{
	if (cluster == -1)
		memset (pvsrow, 0, (numclusters+7)>>3);
	else if (!numvisibility)
		CM_DecompressVis (NULL, pvsrow);
	else
		CM_DecompressVis (map_visibility + R1BSP_ReadU32 (map_visibility + 4 + cluster * 8 + DVIS_PVS * 4), pvsrow);
	return pvsrow;
}

byte	*CM_ClusterPHS (int cluster)
{
	if (cluster == -1)
		memset (phsrow, 0, (numclusters+7)>>3);
	else if (!numvisibility)
		CM_DecompressVis (NULL, phsrow);
	else
		CM_DecompressVis (map_visibility + R1BSP_ReadU32 (map_visibility + 4 + cluster * 8 + DVIS_PHS * 4), phsrow);
	return phsrow;
}


/*
===============================================================================

AREAPORTALS

===============================================================================
*/

void FloodArea_r (carea_t *area, int floodnum)
{
	int		i;
	dareaportal_t	*p;

	if (area->floodvalid == floodvalid)
	{
		if (area->floodnum == floodnum)
			return;
		Com_Error (ERR_DROP, "FloodArea_r: reflooded");
	}

	area->floodnum = floodnum;
	area->floodvalid = floodvalid;
	p = &map_areaportals[area->firstareaportal];
	for (i=0 ; i<area->numareaportals ; i++, p++)
	{
		if (portalopen[p->portalnum])
			FloodArea_r (&map_areas[p->otherarea], floodnum);
	}
}

/*
====================
FloodAreaConnections


====================
*/
void	FloodAreaConnections (void)
{
	int		i;
	carea_t	*area;
	int		floodnum;

	// all current floods are now invalid
	floodvalid++;
	floodnum = 0;

	// area 0 is not used
	for (i=1 ; i<numareas ; i++)
	{
		area = &map_areas[i];
		if (area->floodvalid == floodvalid)
			continue;		// already flooded into
		floodnum++;
		FloodArea_r (area, floodnum);
	}

}

void	EXPORT CM_SetAreaPortalState (int portalnum, qboolean open)
{
	if (portalnum < 0 || portalnum >= numareaportals)
		Com_Error (ERR_DROP, "areaportal outside loaded portal states");

	portalopen[portalnum] = open;
	FloodAreaConnections ();
}

qboolean	EXPORT CM_AreasConnected (int area1, int area2)
{
	if (map_noareas->intvalue)
		return true;

	if (area1 > numareas || area2 > numareas)
		Com_Error (ERR_DROP, "CM_AreasConnected: area > numareas");

	if (area1 < 0 || area2 < 0)
		Com_Error (ERR_DROP, "CM_AreasConnected: area < 0");

	if (map_areas[area1].floodnum == map_areas[area2].floodnum)
		return true;
	return false;
}


/*
=================
CM_WriteAreaBits

Writes a length byte followed by a bit vector of all the areas
that area in the same flood as the area parameter

This is used by the client refreshes to cull visibility
=================
*/
int CM_WriteAreaBits (byte *buffer, int area)
{
	int		i;
	int		floodnum;
	int		bytes;

	bytes = (numareas+7)>>3;

	if (map_noareas->intvalue)
	{	// for debugging, send everything
		memset (buffer, 255, bytes);
	}
	else
	{
		memset (buffer, 0, bytes);

		floodnum = map_areas[area].floodnum;
		for (i=0 ; i<numareas ; i++)
		{
			if (map_areas[i].floodnum == floodnum || !area)
				buffer[i>>3] |= 1<<(i&7);
		}
	}

	return bytes;
}


/*
===================
CM_WritePortalState

Writes the portal state to a savegame file
===================
*/
void	CM_WritePortalState (FILE *f)
{
	fwrite (portalopen, sizeof(portalopen), 1, f);
}

/*
===================
CM_ReadPortalState

Reads the portal state from a savegame file
and recalculates the area connections
===================
*/
void	CM_ReadPortalState (FILE *f)
{
	FS_Read (portalopen, sizeof(portalopen), f);
	FloodAreaConnections ();
}

/*
=============
CM_HeadnodeVisible

Returns true if any leaf under headnode has a cluster that
is potentially visible
=============
*/
qboolean CM_HeadnodeVisible (int nodenum, const byte *visbits)
{
	int		leafnum;
	int		cluster;
	cnode_t	*node;

	if (nodenum < 0)
	{
		leafnum = -1-nodenum;
		cluster = map_leafs[leafnum].cluster;
		if (cluster == -1)
			return false;
		if (visbits[cluster>>3] & (1<<(cluster&7)))
			return true;
		return false;
	}

	node = &map_nodes[nodenum];
	if (CM_HeadnodeVisible(node->children[0], visbits))
		return true;
	return CM_HeadnodeVisible(node->children[1], visbits);
}
