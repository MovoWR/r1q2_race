/* Private compatibility-context shader resources; no renderer API fields. */
#ifndef R1GL_SHADER_H
#define R1GL_SHADER_H

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS 0x8B4A
#define GL_MAX_VERTEX_ATTRIBS 0x8869
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622
#define GL_VERTEX_ATTRIB_ARRAY_SIZE 0x8623
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE 0x8624
#define GL_VERTEX_ATTRIB_ARRAY_TYPE 0x8625
#define GL_CURRENT_VERTEX_ATTRIB 0x8626
#define GL_VERTEX_ATTRIB_ARRAY_NORMALIZED 0x886A
#define GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING 0x889F
#define GL_VERTEX_ATTRIB_ARRAY_POINTER 0x8645
#endif

#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

extern cvar_t *gl_shaders, *gl_gpulerp, *gl_dotshading, *gl_celshading;
extern cvar_t *r_glowmaps, *gl_glowmap_intensity;

#ifndef GL_TEXTURE2
#define GL_TEXTURE2 0x84C2
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#ifndef GL_MAX_TEXTURE_IMAGE_UNITS
#define GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#endif
#ifndef GL_SAMPLER_BINDING
#define GL_SAMPLER_BINDING 0x8919
#endif

/* Private Q2PRO cubemap skies; retained public SetSky/DLL layouts. */
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_BINDING_CUBE_MAP 0x8514
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE 0x851C
#endif
#ifndef GL_TEXTURE_CUBE_MAP_SEAMLESS
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#endif

/* Private GL 3.1 skeleton resources; the renderer export table is unchanged. */
#ifndef GL_TEXTURE_BUFFER
#define GL_TEXTURE_BUFFER 0x8C2A
#define GL_MAX_TEXTURE_BUFFER_SIZE 0x8C2B
#define GL_TEXTURE_BINDING_BUFFER 0x8C2C
#endif
#ifndef GL_R8UI
#define GL_R8UI 0x8232
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_UNIFORM_BUFFER_BINDING 0x8A28
#define GL_MAX_VERTEX_UNIFORM_BLOCKS 0x8A2B
#define GL_MAX_UNIFORM_BUFFER_BINDINGS 0x8A2F
#define GL_MAX_UNIFORM_BLOCK_SIZE 0x8A30
#define GL_UNIFORM_BLOCK_DATA_SIZE 0x8A40
#endif
#ifndef GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 0x8B4C
#endif
#ifndef GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#endif
qboolean GL_SkyShaderReady (void);
qboolean GL_BeginSkyShader (void);
qboolean GL_ClassicSkyShaderReady (void);
qboolean GL_BeginClassicSkyShader (float time);
void GL_EndSkyShader (void);
void GL_SkyRegister (void);
void GL_UpdateSky (void);
void GL_ShutdownSky (void);
void GL_ClearSkyMaterials (void);
byte *GL_LoadSkyPixels (const char *basename, int *width, int *height);

void GL_ShaderRegister (void);
void GL_InitShaders (void);
void GL_UpdateShaderOptions (void);
void GL_ShutdownShaders (void);
void GL_ResetShaderStats (void);
void GL_PrintShaderInfo (void);
qboolean GL_SetShowTrisShader (qboolean enabled);
qboolean GL_SurfaceShaderAvailable (void);
qboolean GL_ShaderIntensityReady (void);
qboolean GL_GlowmapsAvailable (void);
qboolean GL_BeginMaterialShader (qboolean textured, qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale, GLuint glowtexnum);
qboolean GL_BeginLitMaterialShader (qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale, GLuint glowtexnum);
qboolean GL_BeginWarpMaterialShader (qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale,
	GLuint glowtexnum, const msurface_t *surf, qboolean lighting, qboolean *gpu_warp);
qboolean GL_BeginLightmapViewShader (qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale,
	const msurface_t *warp, qboolean *gpu_warp);
qboolean GL_BeginSurfaceShader (qboolean textured, qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale);
qboolean GL_BeginBloomShader (qboolean textured, qboolean alpha_texture, qboolean emitter);
void GL_EndSurfaceShader (void);
qboolean GL_BeginPointBloomShader (qboolean textured);
void GL_EndPointBloomShader (void);
void GL_FreeAliasBuffer (maliasmesh_t *mesh);
void GL_FreeMD3Buffer (mmd3mesh_t *mesh);
void GL_FreeMD5Buffer (mmd5mesh_t *mesh);
void GL_ClearAliasGPUFrame (maliasmesh_t *mesh);
void GL_BuildAliasBuffer (model_t *mod, dmdl_t *header);
void Mod_FreeAliasBuffers (void);
void Mod_FreeMD3Buffers (void);
void Mod_FreeMD5Buffers (void);
qboolean GL_DrawAliasGPU (dmdl_t *header, dtrivertx_t *verts,
	dtrivertx_t *oldverts, const float *move, const float *frontv,
	const float *backv, float alpha, float celscale, qboolean shadow,
	float lheight, GLuint glowtexnum);
qboolean GL_DrawMD3GPU (mmd3mesh_t *mesh, const vec3_t move,
	float backlerp, float alpha, GLuint glowtexnum);
qboolean GL_BeginMD5GPUPass (const mmd5joint_t *pose, int numjoints);
void GL_EndMD5GPUPass (void);
qboolean GL_DrawMD5GPU (mmd5mesh_t *mesh, const vec3_t move,
	float alpha, GLuint glowtexnum);

#endif
