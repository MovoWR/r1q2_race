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

/*

d*_t structures are on-disk representations
m*_t structures are in-memory

*/

/*
==============================================================================

BRUSH MODELS

==============================================================================
*/


//
// in memory representation
//
// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct
{
	vec3_t		position;
} mvertex_t;

typedef struct
{
	vec3_t		mins, maxs;
	vec3_t		origin;		// for sounds or lights
	float		radius;
	int			headnode;
	int			visleafs;		// not including the solid leaf 0
	int			firstface, numfaces;
} mmodel_t;


#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2


#define	SURF_PLANEBACK		2
#define	SURF_DRAWSKY		4
#define SURF_DRAWTURB		0x10
#define SURF_DRAWBACKGROUND	0x40
#define SURF_UNDERWATER		0x80
#define SURF_BSPX_NODRAW	0x100 // renderer-private skip for BSPX NODRAW or fake sky

#define SKY_MATERIAL_DEFAULT	0
#define SKY_MATERIAL_CUBE	1
#define SKY_MATERIAL_CLASSIC	2

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct
{
	unsigned short	v[2];
	unsigned int	cachededgeoffset;
} medge_t;

typedef struct mtexinfo_s
{
	float		vecs[2][4];
	int			flags;
	int			numframes;
	struct mtexinfo_s	*next;		// animation chain
	image_t		*image;
	qboolean is_lava;  // original BSP material name
	int sky_material;  // private Q2PRO sky asset selection
	char sky_name[32]; // original BSP texture name, lower case
	qboolean sky_fallback_checked;
} mtexinfo_t;

#define VERTEXSIZE	8

typedef struct glpoly_s
{
	struct	glpoly_s	*next;
	struct	glpoly_s	*chain;
	int		numverts;
	int		flags;			// for SURF_UNDERWATER (not needed anymore?)
	int vbo_firstvertex;	// private static-world offset, -1 for CPU-only polygons
	float verts[4][VERTEXSIZE]; // variable sized (xyz s1t1 s2t2 packed RGBA)
} glpoly_t;

typedef struct msurface_s
{
	int			visframe;		// should be drawn when node is crossed

	cplane_t	*plane;
	int			flags;

	int			firstedge;	// look up in model->surfedges[], negative numbers
	int			numedges;	// are backwards edges
	
	short		texturemins[2];
	short		extents[2];

	int			light_s, light_t;	// gl lightmap coordinates
	int			dlight_s, dlight_t; // gl lightmap coordinates for dynamic lightmaps

	glpoly_t	*polys;				// multiple if warped
	struct	msurface_s	*texturechain;
	struct  msurface_s	*lightmapchain;

	mtexinfo_t	*texinfo;
	
// lighting info
	int			dlightframe;
	int			dlightbits;

	int			lightmaptexturenum;
	byte		styles[MAXLIGHTMAPS];
	float		cached_light[MAXLIGHTMAPS];	// values currently used in lightmap
	unsigned lighting_sequence;				// private lightmap color generation
	byte		*samples;		// [numstyles*surfsize]
	qboolean lm_decoupled;		// BSPX or sampled N64 lightmap coordinates are in texels
	float lm_axis[2][4];		// world xyz and lightmap offset
} msurface_t;

typedef struct mnode_s
{
// common with leaf
	int			contents;		// -1, to differentiate from leafs
	int			visframe;		// node needs to be traversed if current
	
	float		minmaxs[6];		// for bounding box culling

	struct mnode_s	*parent;

// node specific
	cplane_t	*plane;
	struct mnode_s	*children[2];	

	unsigned short		firstsurface;
	unsigned short		numsurfaces;
} mnode_t;



typedef struct mleaf_s
{
// common with node
	int			contents;		// wil be a negative contents number
	int			visframe;		// node needs to be traversed if current

	float		minmaxs[6];		// for bounding box culling

	struct mnode_s	*parent;

// leaf specific
	int			cluster;
	int			area;

	msurface_t	**firstmarksurface;
	int			nummarksurfaces;
} mleaf_t;


//===================================================================

//
// Whole model
//

typedef enum
{
	mod_bad,
	mod_brush,
	mod_sprite,
	mod_alias,
	mod_md3,
	mod_empty
} modtype_t;

// Private CPU representation of MD3 models. Geometry is frame-major and
// remains in the model hunk; model_t and the public renderer ABI are unchanged.
// Match the pinned Q2PRO tessellator limits; the disk-format maxima differ.
#define MD3_MAX_VERTICES 6144
#define MD3_MAX_DRAW_TRIANGLES 6144

typedef struct
{
	vec3_t mins, maxs, translate;
	float radius;
} mmd3frame_t;

typedef struct
{
	short xyz[3];
	byte normal[2]; // file order: latitude, longitude
} mmd3vertex_t;

typedef struct
{
	int numverts, numtris, numskins;
	unsigned *indices; // three indices per triangle
	float (*st)[2];
	mmd3vertex_t *vertices; // [frame * numverts + vertex]
	struct image_s **skins;
	unsigned gpu_buffer, gpu_vertex_array, gpu_bytes;
	qboolean gpu_failed;
} mmd3mesh_t;

typedef struct
{
	int numframes, nummeshes;
	mmd3frame_t *frames;
	mmd3mesh_t *meshes;
} mmd3model_t;

/* Private CPU MD5 replacement attached to an ordinary MD2 or MD3 model. */
#define MD5_MAX_JOINTS 256
#define MD5_MAX_MESHES 32
#define MD5_MAX_WEIGHTS 8192
#define MD5_MAX_FRAMES 1024
#define MD5_MAX_VERTICES 6144
#define MD5_MAX_TRIANGLES 6144

typedef struct
{
	vec3_t pos;
	float scale;
	float orient[4];
} mmd5joint_t;

typedef struct
{
	float st[2];
	vec3_t normal; /* weighted bind-pose normal in joint-local space */
	unsigned short start, count;
} mmd5vertex_t;

typedef struct
{
	vec3_t pos;
	float bias;
	unsigned char joint;
} mmd5weight_t;

typedef struct
{
	int numverts, numtris, numweights;
	mmd5vertex_t *vertices;
	unsigned short *indices;
	mmd5weight_t *weights;
	unsigned gpu_buffer, gpu_weights, gpu_joints;
	unsigned gpu_weight_texture, gpu_joint_texture, gpu_vertex_array;
	unsigned gpu_bytes;
	qboolean gpu_failed, gpu_tbo_failed;
} mmd5mesh_t;

typedef struct
{
	float position_radius;
	float scaled_weight_radius;
} mmd5framebound_t;

typedef struct
{
	int numframes, numjoints, nummeshes, numskins;
	mmd5joint_t *frames; /* [frame * numjoints + joint] */
	mmd5mesh_t *meshes;
	struct image_s **skins;	  /* shared by every mesh */
	mmd5framebound_t *bounds; /* optional [frame], NULL disables culling */
} mmd5model_t;

// Private indexed MD2 topology; engine entity/model handles retain their ABI.
#define ALIAS_MESH_MAX_VERTICES (MAX_TRIANGLES * 3)
#define ALIAS_MESH_MAX_INDICES (MAX_TRIANGLES * 3)

typedef struct
{
	int index_xyz;
	float st[2];
} maliasvertex_t;

typedef struct
{
	unsigned numverts, numindices, bytes;
	maliasvertex_t *vertices;
	GLuint *indices;
	GLuint gpu_buffer, gpu_vertex_array;
	unsigned gpu_bytes;
	qboolean gpu_failed;
} maliasmesh_t;

/* Renderer-owned, decoded BSPX LIGHTGRID_OCTREE data. */
typedef struct
{
	uint32 point[3];
	uint32 children[8];
} rlightgrid_node_t;

typedef struct
{
	uint32 mins[3], size[3];
	uint32 firstsample, numsamples;
} rlightgrid_leaf_t;

typedef struct
{
	byte style, rgb[3];
} rlightgrid_sample_t;

typedef struct
{
	float scale[3], mins[3];
	uint32 size[3], rootnode, numnodes, numleafs, numsamples;
	unsigned numstyles;
	rlightgrid_node_t *nodes;
	rlightgrid_leaf_t *leafs;
	rlightgrid_sample_t *samples;
} rlightgrid_t;

typedef struct model_s
{
	char		name[MAX_QPATH];

	int			registration_sequence;

	modtype_t	type;
	int			numframes;
	
	int			flags;

//
// volume occupied by the model graphics
//		
	vec3_t		mins, maxs;
	float		radius;

//
// solid volume for clipping 
//
	qboolean	clipbox;
	vec3_t		clipmins, clipmaxs;

//
// brush model
//
	int			firstmodelsurface, nummodelsurfaces;
	qboolean has_bspx;			// private inline-model sky gate
	int			lightmap;		// only for submodels

	int			numsubmodels;
	mmodel_t	*submodels;

	int			numplanes;
	cplane_t	*planes;

	int			numleafs;		// number of visible leafs, not counting 0
	mleaf_t		*leafs;

	int			numvertexes;
	mvertex_t	*vertexes;

	int			numedges;
	medge_t		*edges;

	int			numnodes;
	int			firstnode;
	mnode_t		*nodes;

	int			numtexinfo;
	mtexinfo_t	*texinfo;

	int			numsurfaces;
	msurface_t	*surfaces;

	int			numsurfedges;
	int			*surfedges;

	int			nummarksurfaces;
	msurface_t	**marksurfaces;

	dvis_t		*vis;

	byte		*lightdata;
	rlightgrid_t *lightgrid; // optional heap-owned world light samples

	// for alias models and skins
	image_t		*skins[MAX_MD2SKINS];

	int			extradatasize;
	void		*extradata;
	float *shadow_radii;	 // Hunk-owned base-frame radii; -1 means unavailable
	maliasmesh_t *aliasmesh; // optional CPU topology, freed before the model hunk
	mmd3model_t *md3;		 // private Hunk-owned MD3 payload
	mmd5model_t *md5;		 // optional heap-owned MD5 replacement
	struct model_s	*hash_next;
} model_t;

//============================================================================

void	Mod_Init (void);
void	Mod_ClearAll (void);
model_t *Mod_ForName (char *name, qboolean crash);
mleaf_t *Mod_PointInLeaf (float *p, model_t *model);
byte	*Mod_ClusterPVS (int cluster, model_t *model);

void	Mod_Modellist_f (void);
void	Mod_FreeAll (void);
void	Mod_Free (model_t *mod);
const rlightgrid_sample_t *Mod_LookupLightgrid (const rlightgrid_t *grid,
	const uint32 point[3]);
