/* Private BSP38/QBSP disk layout only. Include after qfiles.h. */
#ifndef R1Q2_BSP_FORMAT_H
#define R1Q2_BSP_FORMAT_H

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Q2PRO 601a8df8433b0c50dbbe37c0716c3793fff140a7 uses these
 * seven widened records. The paired private loaders select by ident. */
#define IDQBSPHEADER (('P'<<24)+('S'<<16)+('B'<<8)+'Q')

typedef struct
{
	uint32 planenum;
	uint32 children[2];
	float mins[3], maxs[3];
	uint32 firstface, numfaces;
} qbsp_dnode_t;

typedef struct
{
	int32 contents;
	uint32 cluster, area;
	float mins[3], maxs[3];
	uint32 firstleafface, numleaffaces;
	uint32 firstleafbrush, numleafbrushes;
} qbsp_dleaf_t;

typedef struct
{
	uint32 planenum, side;
	int32 firstedge;
	uint32 numedges, texinfo;
	byte styles[MAXLIGHTMAPS];
	int32 lightofs;
} qbsp_dface_t;

typedef struct
{
	uint32 v[2];
} qbsp_dedge_t;

typedef struct
{
	uint32 planenum, texinfo;
} qbsp_dbrushside_t;

typedef uint32 qbsp_dleafface_t;
typedef uint32 qbsp_dleafbrush_t;

typedef char qbsp_header_size_check[(sizeof (dheader_t) == 160) ? 1 : -1];
typedef char qbsp_node_size_check[(sizeof (qbsp_dnode_t) == 44) ? 1 : -1];
typedef char qbsp_leaf_size_check[(sizeof (qbsp_dleaf_t) == 52) ? 1 : -1];
typedef char qbsp_face_size_check[(sizeof (qbsp_dface_t) == 28) ? 1 : -1];
typedef char qbsp_edge_size_check[(sizeof (qbsp_dedge_t) == 8) ? 1 : -1];
typedef char qbsp_brushside_size_check[(sizeof (qbsp_dbrushside_t) == 8) ? 1 : -1];

typedef enum
{
	R1BSP_DISK_OK,
	R1BSP_DISK_BAD_ARGUMENT,
	R1BSP_DISK_SHORT_HEADER,
	R1BSP_DISK_FILE_TOO_LARGE,
	R1BSP_DISK_BAD_IDENT,
	R1BSP_DISK_BAD_VERSION,
	R1BSP_DISK_BAD_SPAN,
	R1BSP_DISK_BAD_STRIDE,
	R1BSP_DISK_TOO_MANY
} r1bsp_disk_status_t;

typedef struct
{
	int extended; /* zero: IBSP; one: QBSP */
	int bad_lump; /* -1 for header-level errors */
	uint32 fileofs[MAX_LUMPS];
	uint32 filelen[MAX_LUMPS];
	uint32 count[MAX_LUMPS];
} r1bsp_disk_layout_t;

static uint32 R1BSP_ReadU32 (const byte *p)
{
	return (uint32)p[0] | ((uint32)p[1] << 8) |
		((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}

static uint16 R1BSP_ReadU16 (const byte *p)
{
	return (uint16)p[0] | ((uint16)p[1] << 8);
}

static int16 R1BSP_ReadI16 (const byte *p)
{
	uint16 bits = R1BSP_ReadU16 (p);
	int16 value;

	memcpy (&value, &bits, sizeof (value));
	return value;
}

static int32 R1BSP_ReadI32 (const byte *p)
{
	uint32 bits = R1BSP_ReadU32 (p);
	int32 value;

	memcpy (&value, &bits, sizeof (value));
	return value;
}

static float R1BSP_ReadFloat (const byte *p)
{
	uint32 bits = R1BSP_ReadU32 (p);
	float value;

	memcpy (&value, &bits, sizeof (value));
	return value;
}

/* With no visibility, the donor retains the null cluster sentinel and maps
 * every other cluster to zero. Use the current file's VIS presence explicitly. */
static uint32 R1BSP_QBSPLeafCluster (const byte *leaf, int has_visibility)
{
	uint32 cluster = R1BSP_ReadU32 (leaf + offsetof (qbsp_dleaf_t, cluster));

	return has_visibility || cluster == UINT_MAX ? cluster : 0;
}

/* Renderer animation counting follows each positive nexttexinfo until the
 * chain ends or returns to its start. A tail into another cycle would loop
 * forever. This checks byte records without relying on their alignment. */
static int R1BSP_ValidTextureAnimations (const byte *records, uint32 count)
{
	byte *state;
	uint16 *path;
	uint32 start, depth, i, node;
	int next, valid = 0;

	if (!records || !count || count > MAX_MAP_TEXINFO)
		return 0;
	state = calloc (count, 1);
	path = malloc ((size_t)count * sizeof (*path));
	if (!state || !path)
		goto done;
	for (start = 0; start < count; start++)
	{
		if (state[start])
			continue;
		node = start;
		depth = 0;
		for (;;)
		{
			state[node] = 1;
			path[depth++] = (uint16)node;
			next = R1BSP_ReadI32 (records +
				(size_t)node * sizeof (texinfo_t) +
				offsetof (texinfo_t, nexttexinfo));
			if (next > 0 && (uint32)next >= count)
				goto done;
			if (next <= 0 || state[next] == 2)
			{
				for (i = 0; i < depth; i++)
					state[path[i]] = 2;
				break;
			}
			if (state[next] == 3 || (state[next] == 1 && (uint32)next != start))
				goto done;
			if (state[next] == 1)
			{
				for (i = 0; i < depth; i++)
					state[path[i]] = 3;
				break;
			}
			node = (uint32)next;
		}
	}
	valid = 1;
done:
	free (path);
	free (state);
	return valid;
}

/* Header, span, stride and count preflight. Nested references are checked
 * by each reader before using a map; this helper does not inspect payloads. */
static r1bsp_disk_status_t R1BSP_Preflight (const byte *file, size_t length,
	r1bsp_disk_layout_t *layout)
{
	static const unsigned short stride[2][MAX_LUMPS] = {
		{ 1, sizeof (dplane_t), sizeof (dvertex_t), 1,
			sizeof (dnode_t), sizeof (texinfo_t), sizeof (dface_t), 1,
			sizeof (dleaf_t), sizeof (uint16), sizeof (uint16),
			sizeof (dedge_t), sizeof (int32), sizeof (dmodel_t),
			sizeof (dbrush_t), sizeof (dbrushside_t), 0,
			sizeof (darea_t), sizeof (dareaportal_t) },
		{ 1, sizeof (dplane_t), sizeof (dvertex_t), 1,
			sizeof (qbsp_dnode_t), sizeof (texinfo_t), sizeof (qbsp_dface_t), 1,
			sizeof (qbsp_dleaf_t), sizeof (qbsp_dleafface_t), sizeof (qbsp_dleafbrush_t),
			sizeof (qbsp_dedge_t), sizeof (int32), sizeof (dmodel_t),
			sizeof (dbrush_t), sizeof (qbsp_dbrushside_t), 0,
			sizeof (darea_t), sizeof (dareaportal_t) }
	};
	static const uint32 max_count[MAX_LUMPS] = {
		MAX_MAP_ENTSTRING, MAX_MAP_PLANES, MAX_MAP_VERTS,
		MAX_MAP_VISIBILITY, MAX_MAP_NODES, MAX_MAP_TEXINFO,
		MAX_MAP_FACES, MAX_MAP_LIGHTING, MAX_MAP_LEAFS,
		MAX_MAP_LEAFFACES, MAX_MAP_LEAFBRUSHES - 1,
		MAX_MAP_EDGES, MAX_MAP_SURFEDGES - 1,
		MAX_MAP_MODELS - 1, MAX_MAP_BRUSHES, MAX_MAP_BRUSHSIDES,
		0, MAX_MAP_AREAS, MAX_MAP_AREAPORTALS
	};
	uint32 ident, version, offset, size;
	int i, extended;

	if (!layout)
		return R1BSP_DISK_BAD_ARGUMENT;
	memset (layout, 0, sizeof (*layout));
	layout->bad_lump = -1;
	if (!file)
		return R1BSP_DISK_BAD_ARGUMENT;
	if (length < sizeof (dheader_t))
		return R1BSP_DISK_SHORT_HEADER;
	if (length > INT_MAX)
		return R1BSP_DISK_FILE_TOO_LARGE;

	ident = R1BSP_ReadU32 (file);
	version = R1BSP_ReadU32 (file + 4);
	if (ident != IDBSPHEADER && ident != IDQBSPHEADER)
		return R1BSP_DISK_BAD_IDENT;
	if (version != BSPVERSION)
		return R1BSP_DISK_BAD_VERSION;
	extended = ident == IDQBSPHEADER;
	layout->extended = extended;

	for (i = 0; i < MAX_LUMPS; i++)
	{
		offset = R1BSP_ReadU32 (file + 8 + i * 8);
		size = R1BSP_ReadU32 (file + 12 + i * 8);
		layout->fileofs[i] = offset;
		layout->filelen[i] = size;
		if (i == LUMP_POP)
			continue; /* unused; historical IBSP POP fields can be invalid */
		layout->bad_lump = i;
		if (offset > INT_MAX || size > INT_MAX ||
			(size_t)offset > length || (size_t)size > length - (size_t)offset)
			return R1BSP_DISK_BAD_SPAN;
		if (size % stride[extended][i])
			return R1BSP_DISK_BAD_STRIDE;
		layout->count[i] = size / stride[extended][i];
		if (layout->count[i] > max_count[i])
			return R1BSP_DISK_TOO_MANY;
	}
	layout->bad_lump = -1;
	return R1BSP_DISK_OK;
}

/* The renderer stores one parent per leaf for visibility marking. Solid leaves
 * normally use cluster -1 and may be shared; a visible leaf cannot safely have
 * multiple incoming node references. Call after node-graph validation. */
static int R1BSP_HasSharedVisibleQBSPLeaf (const byte *file,
	const r1bsp_disk_layout_t *layout)
{
	byte *seen;
	uint32 i, j, leaf;
	int32 child;
	int shared = 0;

	if (!file || !layout || !layout->extended || !layout->count[LUMP_LEAFS])
		return -1;
	seen = calloc (layout->count[LUMP_LEAFS], 1);
	if (!seen)
		return -1;
	for (i = 0; i < layout->count[LUMP_NODES]; i++)
	{
		const byte *node = file + layout->fileofs[LUMP_NODES] +
			(size_t)i * sizeof (qbsp_dnode_t);
		for (j = 0; j < 2; j++)
		{
			child = R1BSP_ReadI32 (node + offsetof (qbsp_dnode_t, children) + j * 4);
			if (child >= 0)
				continue;
			leaf = (uint32)(-(child + 1));
			if (leaf >= layout->count[LUMP_LEAFS])
			{
				shared = -1;
				goto done;
			}
			if (R1BSP_ReadU32 (file + layout->fileofs[LUMP_LEAFS] +
					(size_t)leaf * sizeof (qbsp_dleaf_t) +
					offsetof (qbsp_dleaf_t, cluster)) == UINT_MAX)
				continue;
			if (seen[leaf])
			{
				shared = 1;
				goto done;
			}
			seen[leaf] = 1;
		}
	}
done:
	free (seen);
	return shared;
}

#endif
