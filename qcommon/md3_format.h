/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

// MD3 disk layout shared by client precaching and the private renderer.
// Include after q_shared.h, which defines uint32, int16, byte and PACKED_STRUCT.
#ifndef R1Q2_MD3_FORMAT_H
#define R1Q2_MD3_FORMAT_H

#define MD3_IDENT			(('3'<<24)+('P'<<16)+('D'<<8)+'I')
#define MD3_VERSION			15
#define MD3_MAX_TRIANGLES	8192
#define MD3_MAX_VERTS		4096
#define MD3_MAX_SKINS		256
#define MD3_MAX_FRAMES		1024
#define MD3_MAX_MESHES		32
#define MD3_MAX_TAGS		16
#define MD3_MAX_PATH		64
#define MD3_XYZ_SCALE		(1.0f/64.0f)

#ifdef _WIN32
#pragma pack(push, 1)
#endif

typedef struct
{
	float st[2];
} PACKED_STRUCT dmd3coord_t;

typedef struct
{
	int16 point[3];
	byte norm[2];
} PACKED_STRUCT dmd3vertex_t;

typedef struct
{
	float mins[3];
	float maxs[3];
	float translate[3];
	float radius;
	char creator[16];
} PACKED_STRUCT dmd3frame_t;

typedef struct
{
	char name[MD3_MAX_PATH];
	float origin[3];
	float axis[3][3];
} PACKED_STRUCT dmd3tag_t;

typedef struct
{
	char name[MD3_MAX_PATH];
	uint32 unused;
} PACKED_STRUCT dmd3skin_t;

typedef struct
{
	uint32 ident;
	char name[MD3_MAX_PATH];
	uint32 flags;
	uint32 num_frames;
	uint32 num_skins;
	uint32 num_verts;
	uint32 num_tris;
	uint32 ofs_indexes;
	uint32 ofs_skins;
	uint32 ofs_tcs;
	uint32 ofs_verts;
	uint32 meshsize;
} PACKED_STRUCT dmd3mesh_t;

typedef struct
{
	uint32 ident;
	uint32 version;
	char filename[MD3_MAX_PATH];
	uint32 flags;
	uint32 num_frames;
	uint32 num_tags;
	uint32 num_meshes;
	uint32 num_skins;
	uint32 ofs_frames;
	uint32 ofs_tags;
	uint32 ofs_meshes;
	uint32 ofs_end;
} PACKED_STRUCT dmd3header_t;

#ifdef _WIN32
#pragma pack(pop)
#endif

typedef char md3_header_size_check[(sizeof (dmd3header_t) == 108) ? 1 : -1];
typedef char md3_mesh_size_check[(sizeof (dmd3mesh_t) == 108) ? 1 : -1];
typedef char md3_frame_size_check[(sizeof (dmd3frame_t) == 56) ? 1 : -1];
typedef char md3_tag_size_check[(sizeof (dmd3tag_t) == 112) ? 1 : -1];
typedef char md3_skin_size_check[(sizeof (dmd3skin_t) == 68) ? 1 : -1];
typedef char md3_vertex_size_check[(sizeof (dmd3vertex_t) == 8) ? 1 : -1];

#endif
