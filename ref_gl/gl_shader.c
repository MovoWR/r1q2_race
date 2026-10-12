/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2003-2006, 2008, 2018 Andrey Nazarov

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* Q2PRO src/refresh/{shader,mesh,models}.c program/buffer ownership and
 * old/new-frame interpolation, adapted to R1Q2's command UVs, quantized
 * lighting and current-frame shell normals. Compatibility GLSL is used
 * while the other renderer passes still use fixed-function state.
 */
#include "gl_local.h"
#include "../qcommon/md3_format.h"
#include <float.h>
#include <limits.h>
#include <stddef.h>

#define ALIAS_GPU_MAX_BYTES (64u * 1024u * 1024u)
#define ALIAS_SHADER_ATTRIBUTES 3
#define MD3_SHADER_ATTRIBUTES 5
#define MD5_SHADER_ATTRIBUTES 3
#define MD5_GPU_JOINT_BYTES (MD5_MAX_JOINTS * 8 * sizeof(float))
#define MD5_WEIGHT_TMU GL_TEXTURE3
#define MD5_JOINT_TMU GL_TEXTURE4
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#define GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS 0x90D6
#define GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS 0x90DC
#define GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS 0x90DD
#define GL_MAX_SHADER_STORAGE_BLOCK_SIZE 0x90DE
#define GL_SHADER_STORAGE_BLOCK 0x92E6

cvar_t *gl_shaders, *gl_gpulerp, *gl_dotshading, *gl_celshading;
cvar_t *r_glowmaps, *gl_glowmap_intensity;

extern float r_avertexnormals[162][3];
extern float shadelight[3];
extern vec3_t shadevector;

typedef struct
{
	GLuint (APIENTRY *CreateShader) (GLenum);
	void (APIENTRY *ShaderSource) (GLuint, GLsizei, const char *const *, const GLint *);
	void (APIENTRY *CompileShader) (GLuint);
	void (APIENTRY *GetShaderiv) (GLuint, GLenum, GLint *);
	void (APIENTRY *GetShaderInfoLog) (GLuint, GLsizei, GLsizei *, char *);
	void (APIENTRY *DeleteShader) (GLuint);
	GLuint (APIENTRY *CreateProgram) (void);
	void (APIENTRY *AttachShader) (GLuint, GLuint);
	void (APIENTRY *BindAttribLocation) (GLuint, GLuint, const char *);
	void (APIENTRY *LinkProgram) (GLuint);
	void (APIENTRY *GetProgramiv) (GLuint, GLenum, GLint *);
	void (APIENTRY *GetProgramInfoLog) (GLuint, GLsizei, GLsizei *, char *);
	void (APIENTRY *DeleteProgram) (GLuint);
	void (APIENTRY *UseProgram) (GLuint);
	GLint (APIENTRY *GetUniformLocation) (GLuint, const char *);
	void (APIENTRY *Uniform1i) (GLint, GLint);
	void (APIENTRY *Uniform1f) (GLint, GLfloat);
	void (APIENTRY *Uniform3fv) (GLint, GLsizei, const GLfloat *);
	void (APIENTRY *Uniform4fv) (GLint, GLsizei, const GLfloat *);
	void (APIENTRY *VertexAttribPointer) (GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *);
	void (APIENTRY *EnableVertexAttribArray) (GLuint);
	void (APIENTRY *DisableVertexAttribArray) (GLuint);
	void (APIENTRY *GetVertexAttribfv) (GLuint, GLenum, GLfloat *);
	void (APIENTRY *GenVertexArrays) (GLsizei, GLuint *);
	void (APIENTRY *DeleteVertexArrays) (GLsizei, const GLuint *);
	void (APIENTRY *BindVertexArray) (GLuint);
	void (APIENTRY *VertexAttrib4fv) (GLuint, const GLfloat *);
} glshaderprocs_t;

typedef struct
{
	void (APIENTRY *TexBuffer) (GLenum, GLenum, GLuint);
	void (APIENTRY *GetIntegeri_v) (GLenum, GLuint, GLint *);
	void (APIENTRY *BindBufferBase) (GLenum, GLuint, GLuint);
	GLuint (APIENTRY *GetUniformBlockIndex) (GLuint, const char *);
	void (APIENTRY *GetActiveUniformBlockiv) (GLuint, GLuint, GLenum, GLint *);
	void (APIENTRY *UniformBlockBinding) (GLuint, GLuint, GLuint);
} md5shaderprocs_t;

typedef struct
{
	GLuint (APIENTRY *GetProgramResourceIndex) (GLuint, GLenum, const char *);
	void (APIENTRY *ShaderStorageBlockBinding) (GLuint, GLuint, GLuint);
} md5ssboprocs_t;

typedef struct
{
	GLint move, color, shadevector, dotshading;
	GLint sampler, weights, jointnums, glowmap, glowkind;
	GLint glowscale, bloommode, intensity;
} md5uniforms_t;

static glshaderprocs_t shader_procs;
static md5shaderprocs_t md5_procs;
static md5ssboprocs_t md5_ssbo_procs;
static GLuint alias_program;
static GLuint md3_program;
static GLuint md5_program, md5_pose_buffer, md5_skeleton_block;
static GLuint md5_ssbo_program, md5_ssbo_skeleton_block;
static GLuint md5_ssbo_weight_block, md5_ssbo_joint_block;
static md5uniforms_t md5_uniform, md5_ssbo_uniform;

static struct
{
	GLint move, backlerp, color, shadevector, dotshading;
	GLint texture, sampler, glowmap, glowkind, glowscale, bloommode, intensity;
} md3_uniform;

static GLint uniform_move, uniform_front, uniform_back, uniform_color;
static GLint uniform_normals, uniform_shell, uniform_shadow;
static GLint uniform_backlerp, uniform_dotshading;
static GLint uniform_shadevector, uniform_height, uniform_texture, uniform_sampler;
static GLint uniform_glowmap, uniform_glowkind, uniform_glowscale, uniform_bloommode, uniform_intensity;
static qboolean shader_capable, shader_attempted;
static qboolean md3_shader_attempted;
static qboolean md5_shader_attempted, md5_capable, md5_common_capable;
static qboolean md5_ssbo_shader_attempted, md5_ssbo_capable, md5_pose_attempted;
static int shader_major, shader_minor, shader_attributes;
static int md5_ubo_bindings;
static int md5_ssbo_bindings;
static GLuint md5_next_texture = MD5_TEXTURE_FIRST;
static int md5_pose_joints;
static qboolean md5_pose_ready;
static unsigned alias_mode, alias_gpu_bytes, alias_gpu_draws, alias_gpu_shadows;
static unsigned md3_gpu_draws;
static unsigned md5_gpu_draws;

/* Packed MD2 frames and UV seams upload once. No per-draw staging allocation. */
static union
{
	dtrivertx_t frame[ALIAS_MESH_MAX_VERTICES];
	float st[ALIAS_MESH_MAX_VERTICES][2];
} alias_upload;

static float md5_weight_upload[MD5_MAX_WEIGHTS][4];
static unsigned char md5_joint_upload[MD5_MAX_WEIGHTS + 3];
static float md5_pose_upload[MD5_MAX_JOINTS][8];

static const char alias_vertex_shader[] =
	"#version 110\n"
	"attribute vec4 a_new;\n"
	"attribute vec4 a_old;\n"
	"attribute vec2 a_tc;\n"
	"uniform vec3 u_move, u_front, u_back;\n"
	"uniform vec4 u_color;\n"
	"uniform vec3 u_normals[162], u_shadevector;\n"
	"uniform bool u_shell, u_dotshading;\n"
	"uniform int u_shadow;\n"
	"uniform float u_height, u_backlerp;\n"
	"varying vec2 v_tc;\n"
	"varying vec4 v_color;\n"
	"float shadedot(int index) {\n"
	"  float d = dot(u_normals[index], u_shadevector);\n"
	"  if (d < 0.0) d *= 0.3;\n"
	"  return d + 1.0;\n"
	"}\n"
	"void main() {\n"
	"  int n = int(a_new.w);\n"
	"  vec3 p = u_move + a_old.xyz * u_back + a_new.xyz * u_front;\n"
	"  if (u_shell) p += u_normals[n] * 4.0;\n"
	"  if (u_shadow != 0) {\n"
	"    if (u_shadow == 1) {\n"
	"      p.xy -= u_shadevector.xy * (p.z + u_height);\n"
	"      p.z = 1.0 - u_height;\n"
	"    }\n"
	"    v_color = clamp(u_color, 0.0, 1.0);\n"
	"  } else {\n"
	"    float light = 1.0;\n"
	"    if (!u_shell && u_dotshading) {\n"
	"      light = shadedot(int(a_old.w)) * u_backlerp +\n"
	"              shadedot(n) * (1.0 - u_backlerp);\n"
	"    }\n"
	"    v_color = clamp(vec4(u_color.rgb * light, u_color.a), 0.0, 1.0);\n"
	"  }\n"
	"  v_tc = (gl_TextureMatrix[0] * vec4(a_tc, 0.0, 1.0)).xy;\n"
	"  gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);\n"
	"}\n";

/* MD3 uses packed short positions and latitude/longitude normal bytes. */
static const char md3_vertex_shader[] =
	"#version 110\n"
	"attribute vec3 a_new_pos, a_old_pos;\n"
	"attribute vec2 a_new_norm, a_old_norm, a_tc;\n"
	"uniform vec3 u_move, u_shadevector;\n"
	"uniform vec4 u_color;\n"
	"uniform float u_backlerp;\n"
	"uniform bool u_dotshading;\n"
	"varying vec2 v_tc;\n"
	"varying vec4 v_color;\n"
	"vec3 normal_from_bytes(vec2 code) {\n"
	"  float lat = code.x * (6.283185307179586 / 255.0);\n"
	"  float lng = code.y * (6.283185307179586 / 255.0);\n"
	"  float latcos = sin(mod(code.x + 64.0, 256.0) * (6.283185307179586 / 255.0));\n"
	"  float lngcos = sin(mod(code.y + 64.0, 256.0) * (6.283185307179586 / 255.0));\n"
	"  return vec3(sin(lat) * lngcos, sin(lat) * sin(lng), latcos);\n"
	"}\n"
	"float shadedot(vec3 normal) {\n"
	"  float d = dot(normal, u_shadevector);\n"
	"  if (d < 0.0) d *= 0.3;\n"
	"  return d + 1.0;\n"
	"}\n"
	"void main() {\n"
	"  vec3 p = u_move + mix(a_new_pos, a_old_pos, u_backlerp) / 64.0;\n"
	"  float light = 1.0;\n"
	"  if (u_dotshading)\n"
	"    light = shadedot(normal_from_bytes(a_new_norm)) * (1.0 - u_backlerp) +\n"
	"            shadedot(normal_from_bytes(a_old_norm)) * u_backlerp;\n"
	"  v_color = clamp(vec4(u_color.rgb * light, u_color.a), 0.0, 1.0);\n"
	"  v_tc = (gl_TextureMatrix[0] * vec4(a_tc, 0.0, 1.0)).xy;\n"
	"  gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);\n"
	"}\n";

static const char alias_fragment_shader[] =
	"#version 110\n"
	"uniform bool u_showtris;\n"
	"uniform sampler2D u_sampler;\n"
	"uniform bool u_texture;\n"
	"uniform sampler2D u_glowmap;\n"
	"uniform int u_glowkind, u_bloommode;\n"
	"uniform float u_glowscale, u_intensity;\n"
	"varying vec2 v_tc;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  if (u_showtris) {\n"
	"    gl_FragData[0] = vec4(1.0);\n"
	"    gl_FragData[1] = vec4(0.0);\n"
	"    return;\n"
	"  }\n"
	"  vec4 c = u_texture ? texture2D(u_sampler, v_tc) * v_color : v_color;\n"
	"  if (u_intensity > 0.0) c.rgb *= u_intensity;\n"
	"  vec3 emission = u_glowkind == 2 ? texture2D(u_glowmap, v_tc).rgb * u_glowscale : vec3(0.0);\n"
	"  c.rgb += emission;\n"
	"  if (u_bloommode == 2) emission = c.rgb;\n"
	"  gl_FragData[0] = c; gl_FragData[1] = vec4(emission, c.a);\n"
	"}\n";

/* The CPU still interpolates joints; only weighted vertex skinning moves to GL. */
static const char md5_vertex_shader[] =
	"#version 140\n"
	"struct Joint { vec4 pos_scale; vec4 orient; };\n"
	"layout(std140) uniform Skeleton { Joint u_joints[256]; };\n"
	"in vec2 a_tc; in vec3 a_norm; in vec2 a_vert;\n"
	"uniform samplerBuffer u_weights; uniform usamplerBuffer u_jointnums;\n"
	"uniform vec3 u_move, u_shadevector; uniform vec4 u_color;\n"
	"uniform bool u_dotshading;\n"
	"out vec2 v_tc; out vec4 v_color;\n"
	"vec3 rotate_q(vec4 q, vec3 v) {\n"
	"  vec3 c = cross(q.xyz, v);\n"
	"  vec3 c2 = cross(q.xyz, c);\n"
	"  return v + 2.0 * (q.w * c + c2);\n"
	"}\n"
	"void main() {\n"
	"  vec3 p = u_move, n = vec3(0.0);\n"
	"  uvec2 span = uvec2(a_vert);\n"
	"  for (uint k = 0u; k < span.y; k++) {\n"
	"    int i = int(span.x + k);\n"
	"    vec4 w = texelFetch(u_weights, i);\n"
	"    Joint j = u_joints[int(texelFetch(u_jointnums, i).r)];\n"
	"    p += w.w * (j.pos_scale.xyz + j.pos_scale.w * rotate_q(j.orient, w.xyz));\n"
	"    if (u_dotshading) n += w.w * rotate_q(j.orient, a_norm);\n"
	"  }\n"
	"  float light = 1.0;\n"
	"  if (u_dotshading) {\n"
	"    float d = dot(n, u_shadevector);\n"
	"    if (d < 0.0) d *= 0.3;\n"
	"    light += d;\n"
	"  }\n"
	"  v_color = clamp(vec4(u_color.rgb * light, u_color.a), 0.0, 1.0);\n"
	"  v_tc = (gl_TextureMatrix[0] * vec4(a_tc, 0.0, 1.0)).xy;\n"
	"  gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);\n"
	"}\n";

static const char md5_fragment_shader[] =
	"#version 140\n"
	"uniform bool u_showtris;\n"
	"uniform sampler2D u_sampler, u_glowmap;\n"
	"uniform int u_glowkind, u_bloommode;\n"
	"uniform float u_glowscale, u_intensity;\n"
	"in vec2 v_tc; in vec4 v_color;\n"
	"void main() {\n"
	"  if (u_showtris) {\n"
	"    gl_FragData[0] = vec4(1.0);\n"
	"    gl_FragData[1] = vec4(0.0);\n"
	"    return;\n"
	"  }\n"
	"  vec4 c = texture(u_sampler, v_tc) * v_color;\n"
	"  if (u_intensity > 0.0) c.rgb *= u_intensity;\n"
	"  vec3 emission = u_glowkind == 2 ? texture(u_glowmap, v_tc).rgb * u_glowscale : vec3(0.0);\n"
	"  c.rgb += emission;\n"
	"  if (u_bloommode == 2) emission = c.rgb;\n"
	"  gl_FragData[0] = c; gl_FragData[1] = vec4(emission, c.a);\n"
	"}\n";

/* The 4.3 backend shares the CPU pose and GPU buffers with the 3.1 path. */
static const char md5_ssbo_vertex_shader[] =
	"#version 430 compatibility\n"
	"struct Joint { vec4 pos_scale; vec4 orient; };\n"
	"layout(std140) uniform Skeleton { Joint u_joints[256]; };\n"
	"layout(std430) readonly buffer Weights { vec4 b_weights[]; };\n"
	"layout(std430) readonly buffer JointNums { uint b_jointnums[]; };\n"
	"in vec2 a_tc; in vec3 a_norm; in vec2 a_vert;\n"
	"uniform vec3 u_move, u_shadevector; uniform vec4 u_color;\n"
	"uniform bool u_dotshading;\n"
	"out vec2 v_tc; out vec4 v_color;\n"
	"vec3 rotate_q(vec4 q, vec3 v) {\n"
	"  vec3 c = cross(q.xyz, v);\n"
	"  vec3 c2 = cross(q.xyz, c);\n"
	"  return v + 2.0 * (q.w * c + c2);\n"
	"}\n"
	"void main() {\n"
	"  vec3 p = u_move, n = vec3(0.0);\n"
	"  uvec2 span = uvec2(a_vert);\n"
	"  for (uint k = 0u; k < span.y; k++) {\n"
	"    uint i = span.x + k;\n"
	"    vec4 w = b_weights[i];\n"
	"    uint jointnum = (b_jointnums[i / 4u] >> ((i & 3u) * 8u)) & 255u;\n"
	"    Joint j = u_joints[jointnum];\n"
	"    p += w.w * (j.pos_scale.xyz + j.pos_scale.w * rotate_q(j.orient, w.xyz));\n"
	"    if (u_dotshading) n += w.w * rotate_q(j.orient, a_norm);\n"
	"  }\n"
	"  float light = 1.0;\n"
	"  if (u_dotshading) {\n"
	"    float d = dot(n, u_shadevector);\n"
	"    if (d < 0.0) d *= 0.3;\n"
	"    light += d;\n"
	"  }\n"
	"  v_color = clamp(vec4(u_color.rgb * light, u_color.a), 0.0, 1.0);\n"
	"  v_tc = (gl_TextureMatrix[0] * vec4(a_tc, 0.0, 1.0)).xy;\n"
	"  gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);\n"
	"}\n";

static const char md5_ssbo_fragment_shader[] =
	"#version 430 compatibility\n"
	"uniform bool u_showtris;\n"
	"uniform sampler2D u_sampler, u_glowmap;\n"
	"uniform int u_glowkind, u_bloommode;\n"
	"uniform float u_glowscale, u_intensity;\n"
	"in vec2 v_tc; in vec4 v_color;\n"
	"void main() {\n"
	"  if (u_showtris) {\n"
	"    gl_FragData[0] = vec4(1.0);\n"
	"    gl_FragData[1] = vec4(0.0);\n"
	"    return;\n"
	"  }\n"
	"  vec4 c = texture(u_sampler, v_tc) * v_color;\n"
	"  if (u_intensity > 0.0) c.rgb *= u_intensity;\n"
	"  vec3 emission = u_glowkind == 2 ? texture(u_glowmap, v_tc).rgb * u_glowscale : vec3(0.0);\n"
	"  c.rgb += emission;\n"
	"  if (u_bloommode == 2) emission = c.rgb;\n"
	"  gl_FragData[0] = c; gl_FragData[1] = vec4(emission, c.a);\n"
	"}\n";

static size_t GL_ShaderProcAddress (const char *name)
{
	size_t address = (size_t)qwglGetProcAddress (name);
	if (!address || address == 1 || address == 2 || address == 3 || address == (size_t)-1)
		return 0;
	return address;
}

static qboolean GL_RenderShadersEnabled (void)
{
	float value = gl_shaders ? gl_shaders->value : 0;
	/* An off request must not expose unscaled cached textures this frame. */
	return GL_ImageShaderIntensity () ||
		(value >= -FLT_MAX && value <= FLT_MAX && FLOAT_NE_ZERO (value));
}

static unsigned GL_AliasRequestedMode (void)
{
	float lerp = gl_gpulerp ? gl_gpulerp->value : 0;
	unsigned mode = 0;
	if (GL_RenderShadersEnabled ())
		mode |= 1;
	if (lerp >= 1 && lerp <= FLT_MAX)
		mode |= lerp >= 2 ? 4 : 2;
	if (gl_vbo && FLOAT_NE_ZERO (gl_vbo->value))
		mode |= 8;
	if (gl_batch_models && FLOAT_NE_ZERO (gl_batch_models->value))
		mode |= 16;
	if (gl_vertex_arrays && FLOAT_NE_ZERO (gl_vertex_arrays->value))
		mode |= 32;
	return mode;
}

static qboolean GL_AliasGPUAvailable (void)
{
	unsigned mode = GL_AliasRequestedMode ();
	if ((mode & 57) != 57 || !(mode & 6) || !shader_capable || !alias_program)
		return false;
	if ((mode & 2) && (shader_major < 4 || (shader_major == 4 && shader_minor < 3)))
		return false;
	return qglGenBuffers && qglDeleteBuffers && qglBindBuffer && qglBufferData &&
		qglBufferSubData && qglGetBufferParameteriv && qglDrawElements &&
		qglGetIntegerv && qglGetFloatv && qglGetError && qglPushAttrib && qglPopAttrib;
}

static qboolean GL_MD3GPUAvailable (void)
{
	unsigned mode = GL_AliasRequestedMode ();
	if ((mode & 57) != 57 || !(mode & 6) || !shader_capable ||
		shader_attributes < MD3_SHADER_ATTRIBUTES || !md3_program)
		return false;
	if ((mode & 2) && (shader_major < 4 || (shader_major == 4 && shader_minor < 3)))
		return false;
	return qglGenBuffers && qglDeleteBuffers && qglBindBuffer && qglBufferData &&
		qglBufferSubData && qglGetBufferParameteriv && qglDrawElements &&
		qglGetIntegerv && qglGetError && qglPushAttrib && qglPopAttrib;
}

static qboolean GL_MD5CommonAvailable (void)
{
	unsigned mode = GL_AliasRequestedMode ();
	if ((mode & 57) != 57 || !(mode & 6) || !shader_capable ||
		!md5_common_capable || !md5_pose_buffer ||
		shader_attributes < MD5_SHADER_ATTRIBUTES)
		return false;
	if ((mode & 2) && (shader_major < 4 || (shader_major == 4 && shader_minor < 3)))
		return false;
	return qglGenBuffers && qglDeleteBuffers && qglBindBuffer &&
		qglBufferData && qglBufferSubData && qglGetBufferParameteriv &&
		qglDrawElements && qglGetIntegerv && qglGetError &&
		qglPushAttrib && qglPopAttrib;
}

static qboolean GL_MD5TBOAvailable (void)
{
	return GL_MD5CommonAvailable () && md5_capable && md5_program &&
		qglIsTexture && qglDeleteTextures && qglBindTexture &&
		qglActiveTextureARB && md5_procs.TexBuffer;
}

static qboolean GL_MD5SSBOAvailable (void)
{
	return GL_MD5CommonAvailable () && md5_ssbo_capable &&
		md5_ssbo_program && md5_ssbo_bindings >= 2;
}

static qboolean GL_MD5GPUAvailable (void)
{
	return GL_MD5TBOAvailable () || GL_MD5SSBOAvailable ();
}

/* Unit 2 is outside the engine's two-unit texture cache. Restore native state. */
typedef struct
{
	GLint active, texture, sampler;
	qboolean saved;
} glglowstate_t;

static void (APIENTRY *glow_bind_sampler) (GLuint, GLuint);
static qboolean glow_capable, glow_failed;

static qboolean GL_GlowExtension (const char *name)
{
	const char *found, *extensions = gl_config.extensions_string;
	size_t length = strlen (name);
	if (!extensions)
		return false;
	for (found = strstr (extensions, name); found; found = strstr (found + length, name))
		if ((found == extensions || found[-1] == ' ') &&
			(found[length] == 0 || found[length] == ' '))
			return true;
	return false;
}

qboolean GL_GlowmapsAvailable (void)
{
	float maps = r_glowmaps ? r_glowmaps->value : 0;
	return shader_capable && glow_capable && !glow_failed &&
		(!gl_nobind || FLOAT_EQ_ZERO (gl_nobind->value)) &&
		GL_RenderShadersEnabled () &&
		maps >= -FLT_MAX && maps <= FLT_MAX && FLOAT_NE_ZERO (maps);
}

static float GL_GlowmapScale (void)
{
	double scale = (GL_ImageShaderIntensity () ? GL_ImageIntensityScale () : (intensity ? intensity->value : 2)) *
		(double)(gl_glowmap_intensity ? gl_glowmap_intensity->value : 0.75f);
	return scale >= -FLT_MAX && scale <= FLT_MAX ? (float)scale : 0;
}

static void GL_EndGlowMap (glglowstate_t *state)
{
	if (!state->saved)
		return;
	qglActiveTextureARB (GL_TEXTURE2);
	qglBindTexture (GL_TEXTURE_2D, (GLuint)state->texture);
	if (glow_bind_sampler)
		glow_bind_sampler (2, (GLuint)state->sampler);
	qglActiveTextureARB ((GLenum)state->active);
	state->saved = false;
}

static qboolean GL_BeginGlowMap (GLuint texture, glglowstate_t *state)
{
	memset (state, 0, sizeof (*state));
	if (!texture || !GL_GlowmapsAvailable () || !GL_ClearErrors ())
		return false;
	qglGetIntegerv (GL_ACTIVE_TEXTURE, &state->active);
	if (qglGetError () != GL_NO_ERROR)
		goto failed;
	qglActiveTextureARB (GL_TEXTURE2);
	qglGetIntegerv (GL_TEXTURE_BINDING_2D, &state->texture);
	if (glow_bind_sampler)
		qglGetIntegerv (GL_SAMPLER_BINDING, &state->sampler);
	if (qglGetError () != GL_NO_ERROR)
	{
		qglActiveTextureARB ((GLenum)state->active);
		goto failed;
	}
	state->saved = true;
	qglBindTexture (GL_TEXTURE_2D, texture);
	if (glow_bind_sampler)
		glow_bind_sampler (2, 0);
	qglActiveTextureARB ((GLenum)state->active);
	if (qglGetError () == GL_NO_ERROR)
		return true;
	GL_EndGlowMap (state);
failed:
	glow_failed = true;
	ri.Con_Printf (PRINT_ALL, "Glow material state failed; disabled until renderer restart.\n");
	return false;
}

static GLuint GL_CompileShader (GLenum type, const char *source)
{
	GLuint shader = shader_procs.CreateShader (type);
	GLint status = 0, length = (GLint)strlen (source);
	char log[1024];
	if (!shader)
		return 0;
	shader_procs.ShaderSource (shader, 1, &source, &length);
	shader_procs.CompileShader (shader);
	shader_procs.GetShaderiv (shader, GL_COMPILE_STATUS, &status);
	if (status)
		return shader;
	memset (log, 0, sizeof (log));
	shader_procs.GetShaderInfoLog (shader, sizeof (log) - 1, NULL, log);
	ri.Con_Printf (PRINT_ALL, "Renderer shader compilation failed: %s\n", log);
	shader_procs.DeleteShader (shader);
	return 0;
}

static void GL_FreeAliasProgram (void)
{
	GLint current = 0;
	if (!alias_program)
		return;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &current);
	if ((GLuint)current == alias_program)
		shader_procs.UseProgram (0);
	shader_procs.DeleteProgram (alias_program);
	alias_program = 0;
}

static qboolean GL_CreateAliasProgram (void)
{
	GLuint vertex = 0, fragment = 0, program = 0;
	GLint status = 0, saved = 0;
	char log[1024];

	if (shader_attempted || !shader_capable)
		return alias_program != 0;
	shader_attempted = true;
	if (!GL_ClearErrors ())
		return false;
	program = shader_procs.CreateProgram ();
	if (!program)
		goto fail;
	vertex = GL_CompileShader (GL_VERTEX_SHADER, alias_vertex_shader);
	if (!vertex)
		goto fail;
	fragment = GL_CompileShader (GL_FRAGMENT_SHADER, alias_fragment_shader);
	if (!fragment)
		goto fail;
	shader_procs.AttachShader (program, vertex);
	shader_procs.AttachShader (program, fragment);
	shader_procs.BindAttribLocation (program, 0, "a_new");
	shader_procs.BindAttribLocation (program, 1, "a_old");
	shader_procs.BindAttribLocation (program, 2, "a_tc");
	shader_procs.LinkProgram (program);
	shader_procs.GetProgramiv (program, GL_LINK_STATUS, &status);
	if (!status)
	{
		memset (log, 0, sizeof (log));
		shader_procs.GetProgramInfoLog (program, sizeof (log) - 1, NULL, log);
		ri.Con_Printf (PRINT_ALL, "Alias shader linking failed: %s\n", log);
		goto fail;
	}

#define ALIAS_UNIFORM(field, name) \
	field = shader_procs.GetUniformLocation (program, name); \
	if (field < 0) goto fail
	ALIAS_UNIFORM (uniform_move, "u_move");
	ALIAS_UNIFORM (uniform_front, "u_front");
	ALIAS_UNIFORM (uniform_back, "u_back");
	ALIAS_UNIFORM (uniform_color, "u_color");
	ALIAS_UNIFORM (uniform_backlerp, "u_backlerp");
	ALIAS_UNIFORM (uniform_dotshading, "u_dotshading");
	ALIAS_UNIFORM (uniform_normals, "u_normals[0]");
	ALIAS_UNIFORM (uniform_shell, "u_shell");
	ALIAS_UNIFORM (uniform_shadow, "u_shadow");
	ALIAS_UNIFORM (uniform_shadevector, "u_shadevector");
	ALIAS_UNIFORM (uniform_height, "u_height");
	ALIAS_UNIFORM (uniform_texture, "u_texture");
	ALIAS_UNIFORM (uniform_sampler, "u_sampler");
	ALIAS_UNIFORM (uniform_glowmap, "u_glowmap");
	ALIAS_UNIFORM (uniform_glowkind, "u_glowkind");
	ALIAS_UNIFORM (uniform_glowscale, "u_glowscale");
	ALIAS_UNIFORM (uniform_bloommode, "u_bloommode");
	ALIAS_UNIFORM (uniform_intensity, "u_intensity");
#undef ALIAS_UNIFORM
	qglGetIntegerv (GL_CURRENT_PROGRAM, &saved);
	shader_procs.UseProgram (program);
	shader_procs.Uniform3fv (uniform_normals, 162, r_avertexnormals[0]);
	shader_procs.Uniform1i (uniform_sampler, 0);
	shader_procs.UseProgram ((GLuint)saved);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.DeleteShader (vertex);
	shader_procs.DeleteShader (fragment);
	alias_program = program;
	return true;

fail:
	if (vertex)
		shader_procs.DeleteShader (vertex);
	if (fragment)
		shader_procs.DeleteShader (fragment);
	if (program)
		shader_procs.DeleteProgram (program);
	ri.Con_Printf (PRINT_ALL, "Using CPU alias interpolation after shader setup failure.\n");
	return false;
}

static void GL_FreeMD3Program (void)
{
	GLint current = 0;
	if (!md3_program)
		return;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &current);
	if ((GLuint)current == md3_program)
		shader_procs.UseProgram (0);
	shader_procs.DeleteProgram (md3_program);
	md3_program = 0;
}

static qboolean GL_CreateMD3Program (void)
{
	GLuint vertex = 0, fragment = 0, program = 0;
	GLint status = 0, saved = 0;
	char log[1024];

	if (md3_shader_attempted || !shader_capable ||
		shader_attributes < MD3_SHADER_ATTRIBUTES)
		return md3_program != 0;
	if (!GL_ClearErrors ())
		return false;
	md3_shader_attempted = true;
	program = shader_procs.CreateProgram ();
	if (!program)
		goto fail;
	vertex = GL_CompileShader (GL_VERTEX_SHADER, md3_vertex_shader);
	if (!vertex)
		goto fail;
	fragment = GL_CompileShader (GL_FRAGMENT_SHADER, alias_fragment_shader);
	if (!fragment)
		goto fail;
	shader_procs.AttachShader (program, vertex);
	shader_procs.AttachShader (program, fragment);
	shader_procs.BindAttribLocation (program, 0, "a_new_pos");
	shader_procs.BindAttribLocation (program, 1, "a_old_pos");
	shader_procs.BindAttribLocation (program, 2, "a_new_norm");
	shader_procs.BindAttribLocation (program, 3, "a_old_norm");
	shader_procs.BindAttribLocation (program, 4, "a_tc");
	shader_procs.LinkProgram (program);
	shader_procs.GetProgramiv (program, GL_LINK_STATUS, &status);
	if (!status)
	{
		memset (log, 0, sizeof (log));
		shader_procs.GetProgramInfoLog (program, sizeof (log) - 1, NULL, log);
		ri.Con_Printf (PRINT_ALL, "MD3 shader linking failed: %s\n", log);
		goto fail;
	}

#define MD3_UNIFORM(field, name) \
	md3_uniform.field = shader_procs.GetUniformLocation (program, name); \
	if (md3_uniform.field < 0) goto fail
	MD3_UNIFORM (move, "u_move");
	MD3_UNIFORM (backlerp, "u_backlerp");
	MD3_UNIFORM (color, "u_color");
	MD3_UNIFORM (shadevector, "u_shadevector");
	MD3_UNIFORM (dotshading, "u_dotshading");
	MD3_UNIFORM (texture, "u_texture");
	MD3_UNIFORM (sampler, "u_sampler");
	MD3_UNIFORM (glowmap, "u_glowmap");
	MD3_UNIFORM (glowkind, "u_glowkind");
	MD3_UNIFORM (glowscale, "u_glowscale");
	MD3_UNIFORM (bloommode, "u_bloommode");
	MD3_UNIFORM (intensity, "u_intensity");
#undef MD3_UNIFORM
	qglGetIntegerv (GL_CURRENT_PROGRAM, &saved);
	shader_procs.UseProgram (program);
	shader_procs.Uniform1i (md3_uniform.sampler, 0);
	shader_procs.UseProgram ((GLuint)saved);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.DeleteShader (vertex);
	shader_procs.DeleteShader (fragment);
	md3_program = program;
	return true;

fail:
	if (vertex)
		shader_procs.DeleteShader (vertex);
	if (fragment)
		shader_procs.DeleteShader (fragment);
	if (program)
		shader_procs.DeleteProgram (program);
	ri.Con_Printf (PRINT_ALL, "Using CPU MD3 interpolation after shader setup failure.\n");
	return false;
}

static void GL_FreeMD5Program (void)
{
	GLint current = 0;
	if (md5_program)
	{
		qglGetIntegerv (GL_CURRENT_PROGRAM, &current);
		if ((GLuint)current == md5_program)
			shader_procs.UseProgram (0);
		shader_procs.DeleteProgram (md5_program);
		md5_program = 0;
	}
	md5_skeleton_block = 0;
}

static void GL_FreeMD5SSBOProgram (void)
{
	GLint current = 0;
	if (md5_ssbo_program)
	{
		qglGetIntegerv (GL_CURRENT_PROGRAM, &current);
		if ((GLuint)current == md5_ssbo_program)
			shader_procs.UseProgram (0);
		shader_procs.DeleteProgram (md5_ssbo_program);
		md5_ssbo_program = 0;
	}
	md5_ssbo_skeleton_block = md5_ssbo_weight_block =
		md5_ssbo_joint_block = 0;
}

static void GL_FreeMD5PoseBuffer (void)
{
	if (md5_pose_buffer && qglDeleteBuffers)
		qglDeleteBuffers (1, &md5_pose_buffer);
	md5_pose_buffer = 0;
}

static qboolean GL_CreateMD5PoseBuffer (void)
{
	GLuint pose_buffer = 0;
	GLint saved = 0, actual = 0;
	qboolean bound = false;

	if (md5_pose_buffer || md5_pose_attempted)
		return md5_pose_buffer != 0;
	md5_pose_attempted = true;
	if (!md5_common_capable || !GL_ClearErrors ())
		return false;
	qglGetIntegerv (GL_UNIFORM_BUFFER_BINDING, &saved);
	if (qglGetError () != GL_NO_ERROR)
		return false;
	qglGenBuffers (1, &pose_buffer);
	if (!pose_buffer)
		goto fail;
	qglBindBuffer (GL_UNIFORM_BUFFER, pose_buffer);
	bound = true;
	qglBufferData (GL_UNIFORM_BUFFER, MD5_GPU_JOINT_BYTES, NULL, GL_STREAM_DRAW);
	qglGetBufferParameteriv (GL_UNIFORM_BUFFER, GL_BUFFER_SIZE, &actual);
	if (actual != MD5_GPU_JOINT_BYTES || qglGetError () != GL_NO_ERROR)
		goto fail;
	qglBindBuffer (GL_UNIFORM_BUFFER, (GLuint)saved);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	md5_pose_buffer = pose_buffer;
	return true;

fail:
	if (bound)
		qglBindBuffer (GL_UNIFORM_BUFFER, (GLuint)saved);
	if (pose_buffer)
		qglDeleteBuffers (1, &pose_buffer);
	return false;
}

static qboolean GL_CreateMD5BackendProgram (qboolean ssbo)
{
	GLuint vertex = 0, fragment = 0, program = 0, block;
	GLuint weights = 0, joints = 0;
	GLint status = 0, saved_program = 0, actual = 0;
	md5uniforms_t *uniforms = ssbo ? &md5_ssbo_uniform : &md5_uniform;
	char log[1024];
	qboolean program_bound = false;

	if (ssbo ? md5_ssbo_shader_attempted : md5_shader_attempted)
		return (ssbo ? md5_ssbo_program : md5_program) != 0;
	if (ssbo ? !md5_ssbo_capable : !md5_capable)
		return false;
	if (ssbo)
		md5_ssbo_shader_attempted = true;
	else
		md5_shader_attempted = true;
	if (!GL_ClearErrors ())
		return false;
	program = shader_procs.CreateProgram ();
	if (!program)
		goto fail;
	vertex = GL_CompileShader (GL_VERTEX_SHADER,
		ssbo ? md5_ssbo_vertex_shader : md5_vertex_shader);
	fragment = GL_CompileShader (GL_FRAGMENT_SHADER,
		ssbo ? md5_ssbo_fragment_shader : md5_fragment_shader);
	if (!vertex || !fragment)
		goto fail;
	shader_procs.AttachShader (program, vertex);
	shader_procs.AttachShader (program, fragment);
	shader_procs.BindAttribLocation (program, 0, "a_tc");
	shader_procs.BindAttribLocation (program, 1, "a_norm");
	shader_procs.BindAttribLocation (program, 2, "a_vert");
	shader_procs.LinkProgram (program);
	shader_procs.GetProgramiv (program, GL_LINK_STATUS, &status);
	if (!status)
	{
		memset (log, 0, sizeof (log));
		shader_procs.GetProgramInfoLog (program, sizeof (log) - 1, NULL, log);
		ri.Con_Printf (PRINT_ALL, "MD5 %s shader linking failed: %s\n",
			ssbo ? "SSBO" : "TBO", log);
		goto fail;
	}
	block = md5_procs.GetUniformBlockIndex (program, "Skeleton");
	if (block == (GLuint)-1)
		goto fail;
	md5_procs.GetActiveUniformBlockiv (program, block,
		GL_UNIFORM_BLOCK_DATA_SIZE, &actual);
	if (actual != MD5_GPU_JOINT_BYTES)
		goto fail;
	if (ssbo)
	{
		weights = md5_ssbo_procs.GetProgramResourceIndex (program,
			GL_SHADER_STORAGE_BLOCK, "Weights");
		joints = md5_ssbo_procs.GetProgramResourceIndex (program,
			GL_SHADER_STORAGE_BLOCK, "JointNums");
		if (weights == (GLuint)-1 || joints == (GLuint)-1)
			goto fail;
	}

#define MD5_UNIFORM(field, name) \
	uniforms->field = shader_procs.GetUniformLocation (program, name); \
	if (uniforms->field < 0) goto fail
	MD5_UNIFORM (move, "u_move");
	MD5_UNIFORM (color, "u_color");
	MD5_UNIFORM (shadevector, "u_shadevector");
	MD5_UNIFORM (dotshading, "u_dotshading");
	MD5_UNIFORM (sampler, "u_sampler");
	if (!ssbo)
	{
		MD5_UNIFORM (weights, "u_weights");
		MD5_UNIFORM (jointnums, "u_jointnums");
	}
	MD5_UNIFORM (glowmap, "u_glowmap");
	MD5_UNIFORM (glowkind, "u_glowkind");
	MD5_UNIFORM (glowscale, "u_glowscale");
	MD5_UNIFORM (bloommode, "u_bloommode");
	MD5_UNIFORM (intensity, "u_intensity");
#undef MD5_UNIFORM

	qglGetIntegerv (GL_CURRENT_PROGRAM, &saved_program);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.UseProgram (program);
	program_bound = true;
	shader_procs.Uniform1i (uniforms->sampler, 0);
	if (!ssbo)
	{
		shader_procs.Uniform1i (uniforms->weights, 3);
		shader_procs.Uniform1i (uniforms->jointnums, 4);
	}
	shader_procs.UseProgram ((GLuint)saved_program);
	program_bound = false;
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.DeleteShader (vertex);
	shader_procs.DeleteShader (fragment);
	if (ssbo)
	{
		md5_ssbo_skeleton_block = block;
		md5_ssbo_weight_block = weights;
		md5_ssbo_joint_block = joints;
		md5_ssbo_program = program;
	}
	else
	{
		md5_skeleton_block = block;
		md5_program = program;
	}
	return true;

fail:
	if (program_bound)
		shader_procs.UseProgram ((GLuint)saved_program);
	if (vertex)
		shader_procs.DeleteShader (vertex);
	if (fragment)
		shader_procs.DeleteShader (fragment);
	if (program)
		shader_procs.DeleteProgram (program);
	ri.Con_Printf (PRINT_ALL, "MD5 %s shader unavailable; using the other backend or CPU.\n",
		ssbo ? "SSBO" : "TBO");
	return false;
}

static qboolean GL_CreateMD5Program (void)
{
	return GL_CreateMD5BackendProgram (false);
}

static qboolean GL_CreateMD5SSBOProgram (void)
{
	return GL_CreateMD5BackendProgram (true);
}

static void GL_InitMD5Capabilities (void)
{
	GLint block_bytes = 0, blocks = 0, bindings = 0;
	GLint texels = 0, vertex_units = 0, combined_units = 0;

	md5_common_capable = md5_capable = false;
	memset (&md5_procs, 0, sizeof (md5_procs));
	if (!shader_capable || (shader_major == 3 && shader_minor < 1) ||
		shader_major < 3 || shader_attributes < MD5_SHADER_ATTRIBUTES ||
		!qglGenBuffers || !qglDeleteBuffers ||
		!qglBindBuffer || !qglBufferData || !qglBufferSubData ||
		!qglGetBufferParameteriv || !qglDrawElements ||
		!qglGetIntegerv || !qglGetError)
		return;
#define MD5_PROC(name, result, parameters) \
	md5_procs.name = (result (APIENTRY *) parameters)GL_ShaderProcAddress ("gl" #name); \
	if (!md5_procs.name) goto unavailable
	MD5_PROC (GetIntegeri_v, void, (GLenum, GLuint, GLint *));
	MD5_PROC (BindBufferBase, void, (GLenum, GLuint, GLuint));
	MD5_PROC (GetUniformBlockIndex, GLuint, (GLuint, const char *));
	MD5_PROC (GetActiveUniformBlockiv, void, (GLuint, GLuint, GLenum, GLint *));
	MD5_PROC (UniformBlockBinding, void, (GLuint, GLuint, GLuint));
#undef MD5_PROC
	qglGetIntegerv (GL_MAX_UNIFORM_BLOCK_SIZE, &block_bytes);
	qglGetIntegerv (GL_MAX_VERTEX_UNIFORM_BLOCKS, &blocks);
	qglGetIntegerv (GL_MAX_UNIFORM_BUFFER_BINDINGS, &bindings);
	if (qglGetError () != GL_NO_ERROR || block_bytes < MD5_GPU_JOINT_BYTES ||
		blocks < 1 || bindings < 1)
		goto unavailable;
	md5_ubo_bindings = bindings;
	md5_common_capable = true;
	if (!qglActiveTextureARB || !qglIsTexture || !qglDeleteTextures ||
		!qglBindTexture)
		return;
	md5_procs.TexBuffer = (void (APIENTRY *) (GLenum, GLenum, GLuint))
		GL_ShaderProcAddress ("glTexBuffer");
	if (!md5_procs.TexBuffer)
		return;
	qglGetIntegerv (GL_MAX_TEXTURE_BUFFER_SIZE, &texels);
	qglGetIntegerv (GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &vertex_units);
	qglGetIntegerv (GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &combined_units);
	if (qglGetError () != GL_NO_ERROR || texels < MD5_MAX_WEIGHTS ||
		vertex_units < 2 || combined_units < 5)
		return;
	md5_capable = true;
	return;

unavailable:
	memset (&md5_procs, 0, sizeof (md5_procs));
}

static void GL_InitMD5SSBOCapabilities (void)
{
	GLint block_bytes = 0, vertex_blocks = 0, combined_blocks = 0;
	GLint bindings = 0;

	md5_ssbo_capable = false;
	md5_ssbo_bindings = 0;
	memset (&md5_ssbo_procs, 0, sizeof (md5_ssbo_procs));
	if (!md5_common_capable || shader_major < 4 ||
		(shader_major == 4 && shader_minor < 3))
		return;
	md5_ssbo_procs.GetProgramResourceIndex =
		(GLuint (APIENTRY *) (GLuint, GLenum, const char *))
			GL_ShaderProcAddress ("glGetProgramResourceIndex");
	md5_ssbo_procs.ShaderStorageBlockBinding =
		(void (APIENTRY *) (GLuint, GLuint, GLuint))
			GL_ShaderProcAddress ("glShaderStorageBlockBinding");
	if (!md5_ssbo_procs.GetProgramResourceIndex ||
		!md5_ssbo_procs.ShaderStorageBlockBinding)
		return;
	qglGetIntegerv (GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &block_bytes);
	qglGetIntegerv (GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &vertex_blocks);
	qglGetIntegerv (GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS, &combined_blocks);
	qglGetIntegerv (GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &bindings);
	if (qglGetError () != GL_NO_ERROR ||
		block_bytes < MD5_MAX_WEIGHTS * (int)sizeof (md5_weight_upload[0]) ||
		vertex_blocks < 2 || combined_blocks < 2 || bindings < 2)
		return;
	md5_ssbo_bindings = bindings;
	md5_ssbo_capable = true;
}

/* Surface/UI passes keep compatibility vertex inputs and matrix ownership.
 * Existing batches feed both CPU arrays and static world VBOs unchanged.
 * Texture environment results clamp between stages, as in retained R1GL.
 */
static const char surface_vertex_shader[] =
	"#version 110\n"
	"varying vec4 v_tc0, v_tc1, v_color;\n"
	"void main() {\n"
	"  gl_Position = ftransform();\n"
	"  gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;\n"
	"  v_tc0 = gl_TextureMatrix[0] * gl_MultiTexCoord0;\n"
	"  v_tc1 = gl_TextureMatrix[1] * gl_MultiTexCoord1;\n"
	"  v_color = clamp(gl_Color, 0.0, 1.0);\n"
	"}\n";

static const char surface_fragment_shader[] =
	"#version 110\n"
	"uniform bool u_showtris;\n"
	"uniform sampler2D u_sampler0, u_sampler1;\n"
	"uniform int u_mode0, u_mode1, u_format0, u_format1;\n"
	"uniform float u_scale1, u_modulate, u_brightness;\n"
	"uniform sampler2D u_glowmap;\n"
	"uniform int u_glowkind, u_bloommode;\n"
	"uniform float u_glowscale, u_intensity;\n"
	"uniform vec4 u_warp;\n"
	"varying vec4 v_tc0, v_tc1, v_color;\n"
	"vec4 stage(vec4 p, vec4 t, int mode, int fmt, float scale) {\n"
	"  bool rgb = fmt != 3;\n"
	"  bool alpha = fmt != 1;\n"
	"  vec4 c = p;\n"
	"  if (mode == 1) {\n"
	"    if (rgb) c.rgb = t.rgb;\n"
	"    if (alpha) c.a = t.a;\n"
	"  } else if (mode == 2) {\n"
	"    if (rgb) c.rgb *= t.rgb * scale;\n"
	"    if (alpha) c.a *= t.a;\n"
	"  } else if (mode == 3) {\n"
	"    c.rgb = fmt == 2 ? mix(p.rgb, t.rgb, t.a) : t.rgb;\n"
	"  } else if (mode == 4) {\n"
	"    if (rgb) c.rgb += t.rgb;\n"
	"    if (alpha) c.a = fmt == 4 ? p.a + t.a : p.a * t.a;\n"
	"  }\n"
	"  return clamp(c, 0.0, 1.0);\n"
	"}\n"
	"void main() {\n"
	"  if (u_showtris) {\n"
	"    gl_FragData[0] = vec4(1.0);\n"
	"    gl_FragData[1] = vec4(0.0);\n"
	"    return;\n"
	"  }\n"
	"  vec4 c = v_color;\n"
	"  vec4 tc0 = v_tc0;\n"
	"  if (u_warp.w > 0.0) {\n"
	"    vec2 tc = tc0.xy / tc0.w + u_warp.xy;\n"
	"    tc += vec2(0.0625) * sin(tc.yx * 4.0 + u_warp.z);\n"
	"    tc0.xy = tc * tc0.w;\n"
	"  }\n"
	"  if (u_mode0 != 0) c = stage(c, texture2DProj(u_sampler0, tc0), u_mode0, u_format0, 1.0);\n"
	"  vec4 glowmap = u_glowkind != 0 ? texture2DProj(u_glowmap, tc0) : vec4(0.0);\n"
	"  vec3 emission = vec3(0.0);\n"
	"  if (u_glowkind == 1) emission = c.rgb * glowmap.a;\n"
	"  if (u_mode1 != 0) {\n"
	"    vec4 lm = texture2DProj(u_sampler1, v_tc1);\n"
	"    if (u_glowkind == 1) lm.rgb = mix(lm.rgb, vec3(1.0), glowmap.a);\n"
	"    if (u_modulate >= 0.0 && u_mode1 == 2 && u_format1 != 3) {\n"
	"      c.rgb *= (lm.rgb + vec3(u_brightness)) * u_modulate * u_scale1;\n"
	"      if (u_format1 != 1) c.a *= lm.a;\n"
	"    } else c = stage(c, lm, u_mode1, u_format1, u_scale1);\n"
	"  }\n"
	"  if (u_intensity > 0.0) { c.rgb *= u_intensity; emission *= u_intensity; }\n"
	"  if (u_glowkind == 2) {\n"
	"    emission = glowmap.rgb * u_glowscale; c.rgb += emission;\n"
	"  }\n"
	"  if (u_bloommode == 2) emission = c.rgb;\n"
	"  gl_FragData[0] = c; gl_FragData[1] = vec4(emission, c.a);\n"
	"}\n";

static GLuint surface_program;
static GLint surface_uniform_sampler0, surface_uniform_sampler1;
static GLint surface_uniform_mode0, surface_uniform_mode1;
static GLint surface_uniform_format0, surface_uniform_format1, surface_uniform_scale1;
static GLint surface_uniform_modulate, surface_uniform_brightness;
static GLint surface_uniform_glowmap, surface_uniform_glowkind, surface_uniform_glowscale, surface_uniform_bloommode, surface_uniform_intensity, surface_uniform_warp;
static glglowstate_t surface_glow_state;
static GLint surface_saved_program;
static qboolean surface_attempted, surface_active;
static unsigned surface_world_draws, surface_ui_draws;

static void GL_FreeSurfaceProgram (void)
{
	GLint current = 0;
	if (!surface_program)
		return;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &current);
	if ((GLuint)current == surface_program)
		shader_procs.UseProgram (0);
	shader_procs.DeleteProgram (surface_program);
	surface_program = 0;
}

static qboolean GL_CreateSurfaceProgram (void)
{
	GLuint vertex = 0, fragment = 0, program = 0;
	GLint status = 0;
	char log[1024];
	if (surface_attempted || !shader_capable)
		return surface_program != 0;
	surface_attempted = true;
	if (!GL_ClearErrors ())
		return false;
	program = shader_procs.CreateProgram ();
	if (!program)
		goto fail;
	vertex = GL_CompileShader (GL_VERTEX_SHADER, surface_vertex_shader);
	if (!vertex)
		goto fail;
	fragment = GL_CompileShader (GL_FRAGMENT_SHADER, surface_fragment_shader);
	if (!fragment)
		goto fail;
	shader_procs.AttachShader (program, vertex);
	shader_procs.AttachShader (program, fragment);
	shader_procs.LinkProgram (program);
	shader_procs.GetProgramiv (program, GL_LINK_STATUS, &status);
	if (!status)
	{
		memset (log, 0, sizeof (log));
		shader_procs.GetProgramInfoLog (program, sizeof (log) - 1, NULL, log);
		ri.Con_Printf (PRINT_ALL, "Surface shader linking failed: %s\n", log);
		goto fail;
	}
#define SURFACE_UNIFORM(field, name) \
	field = shader_procs.GetUniformLocation (program, name); \
	if (field < 0) goto fail
	SURFACE_UNIFORM (surface_uniform_sampler0, "u_sampler0");
	SURFACE_UNIFORM (surface_uniform_sampler1, "u_sampler1");
	SURFACE_UNIFORM (surface_uniform_mode0, "u_mode0");
	SURFACE_UNIFORM (surface_uniform_mode1, "u_mode1");
	SURFACE_UNIFORM (surface_uniform_format0, "u_format0");
	SURFACE_UNIFORM (surface_uniform_format1, "u_format1");
	SURFACE_UNIFORM (surface_uniform_scale1, "u_scale1");
	SURFACE_UNIFORM (surface_uniform_glowmap, "u_glowmap");
	SURFACE_UNIFORM (surface_uniform_glowkind, "u_glowkind");
	SURFACE_UNIFORM (surface_uniform_glowscale, "u_glowscale");
	SURFACE_UNIFORM (surface_uniform_bloommode, "u_bloommode");
	SURFACE_UNIFORM (surface_uniform_intensity, "u_intensity");
	SURFACE_UNIFORM (surface_uniform_modulate, "u_modulate");
	SURFACE_UNIFORM (surface_uniform_brightness, "u_brightness");
#undef SURFACE_UNIFORM
	/* The water effect is optional; an absent uniform must not disable the
	 * ordinary surface/lightmap material. */
	surface_uniform_warp = shader_procs.GetUniformLocation (program, "u_warp");
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.DeleteShader (vertex);
	shader_procs.DeleteShader (fragment);
	surface_program = program;
	return true;
fail:
	if (vertex)
		shader_procs.DeleteShader (vertex);
	if (fragment)
		shader_procs.DeleteShader (fragment);
	if (program)
		shader_procs.DeleteProgram (program);
	ri.Con_Printf (PRINT_ALL, "Surface shader setup failed; retaining native drawing.\n");
	return false;
}

qboolean GL_ShaderIntensityReady (void)
{
	/* Opaque world fallback without ARB multitexture is a two-pass draw.
	 * Select raw image intensity only when every required material path exists. */
	return (alias_mode & 1) && shader_capable &&
		qglActiveTextureARB && qglClientActiveTextureARB &&
		qglMTexCoord2fSGIS && qglMTexCoord2fvSGIS && qglGetTexEnviv && qglIsEnabled &&
		GL_CreateAliasProgram () && GL_CreateSurfaceProgram ();
}

static int GL_SurfaceTextureMode (GLenum mode)
{
	switch (mode)
	{
	case 0:
		return 0;
	case GL_REPLACE:
		return 1;
	case GL_MODULATE:
		return 2;
	case GL_DECAL:
		return 3;
	case GL_ADD:
		return 4;
	default:
		return -1;
	}
}

static int GL_SurfaceTextureFormat (GLenum format)
{
	switch (format)
	{
	case 0:
		return 0;
	case 3:
	case GL_RGB:
	case GL_RGB8:
	case GL_RGB5:
	case GL_RGB4:
	case GL_R3_G3_B2:
	case GL_LUMINANCE:
	case GL_LUMINANCE8:
		return 1;
	case 4:
	case GL_RGBA:
	case GL_RGBA8:
	case GL_RGB10_A2:
	case GL_RGB5_A1:
	case GL_RGBA4:
	case GL_RGBA2:
		return 2;
	case GL_ALPHA:
	case GL_ALPHA8:
		return 3;
	case GL_INTENSITY:
	case GL_INTENSITY8:
		return 4;
	default:
		return -1;
	}
}

qboolean GL_SurfaceShaderAvailable (void)
{
	return GL_RenderShadersEnabled () &&
		shader_capable && !surface_active && (!surface_attempted || surface_program);
}

static qboolean GL_BeginMaterialShaderScaled (qboolean textured, qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale, GLuint glowtexnum, float image_intensity, qboolean lighting)
{
	static const GLfloat no_warp[4] = { 0, 0, 0, 0 };
	int glowkind = 0;
	int mode0 = textured ? GL_SurfaceTextureMode (texenv) : 0;
	int format1 = GL_SurfaceTextureFormat (lightmap_format);
	int mode1 = format1 ? GL_SurfaceTextureMode (lightmap_env) : 0;
	if (!GL_SurfaceShaderAvailable () ||
		!qglIsEnabled || qglIsEnabled (GL_FOG) || qglIsEnabled (GL_LIGHTING) ||
		mode0 < 0 || format1 < 0 || mode1 < 0 ||
		(mode1 == 3 && format1 != 1 && format1 != 2) ||
		!(lightmap_scale >= 1 && lightmap_scale <= 4) ||
		!GL_CreateSurfaceProgram () || !GL_ClearErrors ())
	{
		GL_BloomAbort ();
		return false;
	}
	memset (&surface_glow_state, 0, sizeof (surface_glow_state));
	if (textured && glowtexnum && (!format1 || (mode1 == 2 && format1 != 3)) && GL_BeginGlowMap (glowtexnum, &surface_glow_state))
		glowkind = format1 ? 1 : 2;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &surface_saved_program);
	shader_procs.UseProgram (surface_program);
	shader_procs.Uniform1i (surface_uniform_sampler0, 0);
	shader_procs.Uniform1i (surface_uniform_sampler1, 1);
	shader_procs.Uniform1i (surface_uniform_mode0, mode0);
	shader_procs.Uniform1i (surface_uniform_mode1, mode1);
	shader_procs.Uniform1i (surface_uniform_format0, alpha_texture ? 2 : 1);
	shader_procs.Uniform1i (surface_uniform_format1, format1);
	shader_procs.Uniform1f (surface_uniform_scale1, lightmap_scale);
	shader_procs.Uniform1f (surface_uniform_intensity, image_intensity);
	shader_procs.Uniform1f (surface_uniform_modulate,
		lighting && format1 && GL_LightingShader () ? GL_WorldModulate () : -1);
	shader_procs.Uniform1f (surface_uniform_brightness,
		lighting && format1 && GL_LightingShader () ? GL_LightingBrightness () : 0);
	shader_procs.Uniform1i (surface_uniform_glowmap, glowkind ? 2 : 0);
	shader_procs.Uniform1i (surface_uniform_glowkind, glowkind);
	shader_procs.Uniform1f (surface_uniform_glowscale, GL_GlowmapScale ());
	shader_procs.Uniform1i (surface_uniform_bloommode, GL_BloomActive () ? 1 : 0);
	if (surface_uniform_warp >= 0)
		shader_procs.Uniform4fv (surface_uniform_warp, 1, no_warp);
	if (qglGetError () != GL_NO_ERROR || !GL_BloomTargets (true))
	{
		shader_procs.UseProgram ((GLuint)surface_saved_program);
		GL_EndGlowMap (&surface_glow_state);
		GL_FreeSurfaceProgram ();
		GL_BloomAbort ();
		ri.Con_Printf (PRINT_ALL, "Surface shader uniforms failed; retaining native drawing.\n");
		return false;
	}
	surface_active = true;
	if (format1)
		surface_world_draws++;
	else if (!glowkind)
		surface_ui_draws++;
	return true;
}

qboolean GL_BeginMaterialShader (qboolean textured, qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale, GLuint glowtexnum)
{
	return GL_BeginMaterialShaderScaled (textured, alpha_texture, texenv,
		lightmap_format, lightmap_env, lightmap_scale, glowtexnum, 0, false);
}

qboolean GL_BeginLitMaterialShader (qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale, GLuint glowtexnum)
{
	qboolean drawn = GL_BeginMaterialShaderScaled (true, alpha_texture, texenv,
		lightmap_format, lightmap_env, lightmap_scale, glowtexnum, GL_ImageIntensityScale (), true);
	if (!drawn)
		GL_ImageShaderIntensityFailure ();
	return drawn;
}

/* A failed optional warp uniform may leave the base material usable. Keep its
 * lightmap and draw with the retained CPU UVs if the zero reset succeeds. */
static qboolean GL_BeginWarpMaterialShaderScaled (qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale,
	GLuint glowtexnum, const msurface_t *surf, qboolean lighting,
	float image_intensity, qboolean *gpu_warp)
{
	static const GLfloat no_warp[4] = { 0, 0, 0, 0 };
	GLfloat warp[4] = { 0, 0, r_newrefdef.time, 1 };
	unsigned flags;
	float speed = 0;
	qboolean opaque_lava;

	*gpu_warp = false;
	if (!GL_BeginMaterialShaderScaled (true, alpha_texture, texenv,
			lightmap_format, lightmap_env, lightmap_scale, glowtexnum,
			image_intensity, lighting))
	{
		if (lighting)
			GL_ImageShaderIntensityFailure ();
		return false;
	}
	if (!surf || !surf->texinfo)
		return true;
	flags = (unsigned)surf->texinfo->flags;
	opaque_lava = surf->texinfo->is_lava &&
		!(flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66));
	/* The face's lava material controls glow independently of whether texture
	 * intensity was baked at upload time. Other water adds unscaled glow. */
	shader_procs.Uniform1f (surface_uniform_glowscale,
		opaque_lava ? GL_GlowmapScale () : 1.0f);
	if (qglGetError () != GL_NO_ERROR)
	{
		GL_EndSurfaceShader ();
		GL_FreeSurfaceProgram ();
		GL_BloomAbort ();
		if (lighting)
			GL_ImageShaderIntensityFailure ();
		return false;
	}
	if (!surf->texinfo->image || surf->texinfo->image->width <= 0 ||
		surf->texinfo->image->height <= 0 ||
		surface_uniform_warp < 0 ||
		!(r_newrefdef.time >= -FLT_MAX && r_newrefdef.time <= FLT_MAX))
		return true;
	if (flags & (SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y))
		speed = 0.78125f;
	else if (flags & SURF_FLOWING)
		speed = 0.5f;
	if (flags & SURF_N64_SCROLL_FLIP)
		speed = -speed;
	speed *= r_newrefdef.time;
	if (flags & SURF_N64_SCROLL_Y)
		warp[1] = speed;
	else
		warp[0] = -speed;
	shader_procs.Uniform4fv (surface_uniform_warp, 1, warp);
	if (qglGetError () == GL_NO_ERROR)
	{
		*gpu_warp = true;
		return true;
	}
	shader_procs.Uniform4fv (surface_uniform_warp, 1, no_warp);
	if (qglGetError () == GL_NO_ERROR)
		return true;
	GL_EndSurfaceShader ();
	GL_FreeSurfaceProgram ();
	GL_BloomAbort ();
	if (lighting)
		GL_ImageShaderIntensityFailure ();
	return false;
}

qboolean GL_BeginWarpMaterialShader (qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale,
	GLuint glowtexnum, const msurface_t *surf, qboolean lighting, qboolean *gpu_warp)
{
	return GL_BeginWarpMaterialShaderScaled (alpha_texture, texenv,
		lightmap_format, lightmap_env, lightmap_scale, glowtexnum, surf,
		lighting, lighting ? GL_ImageIntensityScale () : 0, gpu_warp);
}

/* Only sampled world/brush faces opt in. The caller binds opaque white on
 * unit zero; lightmap brightness/modulation and translucent vertex alpha stay. */
qboolean GL_BeginLightmapViewShader (qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale,
	const msurface_t *warp, qboolean *gpu_warp)
{
	qboolean drawn;

	if (gpu_warp)
		*gpu_warp = false;
	if (!lightmap_format)
		return false;
	if (warp)
		return GL_BeginWarpMaterialShaderScaled (alpha_texture, texenv,
			lightmap_format, lightmap_env, lightmap_scale, 0, warp,
			true, 0, gpu_warp);
	drawn = GL_BeginMaterialShaderScaled (true, alpha_texture, texenv,
		lightmap_format, lightmap_env, lightmap_scale, 0, 0, true);
	if (!drawn)
		GL_ImageShaderIntensityFailure ();
	return drawn;
}

qboolean GL_BeginSurfaceShader (qboolean textured, qboolean alpha_texture, GLenum texenv,
	GLenum lightmap_format, GLenum lightmap_env, float lightmap_scale)
{
	return GL_BeginMaterialShader (textured, alpha_texture, texenv,
		lightmap_format, lightmap_env, lightmap_scale, 0);
}

void GL_EndSurfaceShader (void)
{
	if (!surface_active)
		return;
	shader_procs.UseProgram ((GLuint)surface_saved_program);
	GL_EndGlowMap (&surface_glow_state);
	GL_BloomTargets (false);
	surface_active = false;
}

/* A fragment-only program leaves fixed-function vertex processing in place.
 * That retains the point-size distance attenuation, min/max clamp and
 * multisample fade used by the original GL_POINTS particle paths. */
static const char point_fragment_shader[] =
	"#version 120\n"
	"uniform bool u_showtris;\n"
	"uniform sampler2D u_sampler;\n"
	"uniform int u_point_textured;\n"
	"void main() {\n"
	"  if (u_showtris) {\n"
	"    gl_FragData[0] = vec4(1.0);\n"
	"    gl_FragData[1] = vec4(0.0);\n"
	"    return;\n"
	"  }\n"
	"  vec4 c = clamp(gl_Color, 0.0, 1.0);\n"
	"  if (u_point_textured != 0) c = clamp(c * texture2D(u_sampler, gl_PointCoord), 0.0, 1.0);\n"
	"  gl_FragData[0] = c;\n"
	"  gl_FragData[1] = vec4(c.rgb, c.a);\n"
	"}\n";
static GLuint point_program;
static GLint point_uniform_sampler, point_uniform_textured, point_saved_program;
static qboolean point_attempted, point_active;
static qboolean sky_active;

void GL_EndPointBloomShader (void)
{
	if (!point_active)
		return;
	shader_procs.UseProgram ((GLuint)point_saved_program);
	GL_BloomTargets (false);
	point_active = false;
}

static void GL_FreePointProgram (void)
{
	GLint current = 0;
	GL_EndPointBloomShader ();
	if (!point_program)
		return;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &current);
	if ((GLuint)current == point_program)
		shader_procs.UseProgram (0);
	shader_procs.DeleteProgram (point_program);
	point_program = 0;
}

static qboolean GL_CreatePointProgram (void)
{
	GLuint fragment = 0, program = 0;
	GLint status = 0;
	char log[1024];
	if (point_attempted || !shader_capable)
		return point_program != 0;
	point_attempted = true;
	if (!GL_ClearErrors ())
		return false;
	program = shader_procs.CreateProgram ();
	if (!program)
		goto fail;
	fragment = GL_CompileShader (GL_FRAGMENT_SHADER, point_fragment_shader);
	if (!fragment)
		goto fail;
	shader_procs.AttachShader (program, fragment);
	shader_procs.LinkProgram (program);
	shader_procs.GetProgramiv (program, GL_LINK_STATUS, &status);
	if (!status)
	{
		memset (log, 0, sizeof (log));
		shader_procs.GetProgramInfoLog (program, sizeof (log) - 1, NULL, log);
		ri.Con_Printf (PRINT_ALL, "Point particle shader linking failed: %s\n", log);
		goto fail;
	}
	point_uniform_sampler = shader_procs.GetUniformLocation (program, "u_sampler");
	point_uniform_textured = shader_procs.GetUniformLocation (program, "u_point_textured");
	if (point_uniform_sampler < 0 || point_uniform_textured < 0 ||
		qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.DeleteShader (fragment);
	point_program = program;
	return true;
fail:
	if (fragment)
		shader_procs.DeleteShader (fragment);
	if (program)
		shader_procs.DeleteProgram (program);
	ri.Con_Printf (PRINT_ALL, "Point particle shader setup failed; retaining native points.\n");
	return false;
}

qboolean GL_BeginPointBloomShader (qboolean textured)
{
	if (!GL_BloomActive ())
		return false;
	if (!GL_RenderShadersEnabled () || !shader_capable || point_active ||
		surface_active || sky_active || !qglIsEnabled ||
		qglIsEnabled (GL_FOG) || qglIsEnabled (GL_LIGHTING) ||
		(textured && gl_state.currenttmu != 0) ||
		!GL_CreatePointProgram () || !GL_ClearErrors ())
	{
		GL_BloomAbort ();
		return false;
	}
	qglGetIntegerv (GL_CURRENT_PROGRAM, &point_saved_program);
	if (qglGetError () != GL_NO_ERROR)
	{
		GL_BloomAbort ();
		return false;
	}
	shader_procs.UseProgram (point_program);
	shader_procs.Uniform1i (point_uniform_sampler, 0);
	shader_procs.Uniform1i (point_uniform_textured, textured ? 1 : 0);
	if (qglGetError () != GL_NO_ERROR || !GL_BloomTargets (true))
	{
		shader_procs.UseProgram ((GLuint)point_saved_program);
		GL_BloomTargets (false);
		GL_FreePointProgram ();
		GL_BloomAbort ();
		return false;
	}
	point_active = true;
	return true;
}

/* Q2PRO default sky coordinates and zero-emission bloom material output. */
static const char sky_vertex_shader[] =
	"#version 110\n"
	"varying vec3 v_direction;\n"
	"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
	"  v_direction = gl_MultiTexCoord0.xyz; }\n";
static const char sky_fragment_shader[] =
	"#version 110\n"
	"uniform bool u_showtris;\n"
	"uniform samplerCube u_sampler; varying vec3 v_direction;\n"
	"void main() {\n"
	"  if (u_showtris) {\n"
	"    gl_FragData[0] = vec4(1.0);\n"
	"    gl_FragData[1] = vec4(0.0);\n"
	"    return;\n"
	"  }\n"
	"  vec4 c = vec4(textureCube(u_sampler, v_direction).rgb, 1.0);\n"
	"  gl_FragData[0] = c; gl_FragData[1] = vec4(0.0, 0.0, 0.0, c.a); }\n";
/* Q2PRO classic sky uses the lower atlas half as the solid layer. */
static const char classic_sky_fragment_shader[] =
	"#version 110\n"
	"uniform bool u_showtris;\n"
	"uniform sampler2D u_solid; uniform sampler2D u_alpha; uniform float u_time;\n"
	"varying vec3 v_direction;\n"
	"void main() {\n"
	"  if (u_showtris) {\n"
	"    gl_FragData[0] = vec4(1.0);\n"
	"    gl_FragData[1] = vec4(0.0);\n"
	"    return;\n"
	"  }\n"
	"  vec2 dir = v_direction.xy * (3.0 / max(length(v_direction), 0.0001));\n"
	"  vec3 solid = texture2D(u_solid, dir + vec2(u_time * 0.0625)).rgb;\n"
	"  vec3 alpha = texture2D(u_alpha, dir + vec2(u_time * 0.1250)).rgb;\n"
	"  vec4 c = vec4((solid - alpha * 0.25) * 0.65, 1.0);\n"
	"  gl_FragData[0] = c; gl_FragData[1] = vec4(0.0, 0.0, 0.0, c.a); }\n";
static GLuint sky_program;
static GLint sky_uniform_sampler, sky_saved_program, sky_saved_active, sky_saved_sampler;
static qboolean sky_capable, sky_attempted, sky_samplers, sky_seamless, sky_saved_seamless;
static GLuint classic_sky_program;
static GLint classic_sky_uniform_solid, classic_sky_uniform_alpha, classic_sky_uniform_time;
static GLint sky_saved_sampler1;
static qboolean classic_sky_capable, classic_sky_attempted, classic_sky_active;

static void GL_FreeSkyProgram (void)
{
	GLint current = 0;
	if (!sky_program)
		return;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &current);
	if ((GLuint)current == sky_program)
		shader_procs.UseProgram (0);
	shader_procs.DeleteProgram (sky_program);
	sky_program = 0;
}

static void GL_FreeClassicSkyProgram (void)
{
	GLint current = 0;
	if (!classic_sky_program)
		return;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &current);
	if ((GLuint)current == classic_sky_program)
		shader_procs.UseProgram (0);
	shader_procs.DeleteProgram (classic_sky_program);
	classic_sky_program = 0;
}

static qboolean GL_CreateSkyProgram (void)
{
	GLuint vertex = 0, fragment = 0, program = 0;
	GLint status = 0;
	if (sky_attempted)
		return sky_program != 0;
	sky_attempted = true;
	if (!GL_ClearErrors ())
		return false;
	program = shader_procs.CreateProgram ();
	if (!program)
		goto fail;
	vertex = GL_CompileShader (GL_VERTEX_SHADER, sky_vertex_shader);
	if (!vertex)
		goto fail;
	fragment = GL_CompileShader (GL_FRAGMENT_SHADER, sky_fragment_shader);
	if (!fragment)
		goto fail;
	shader_procs.AttachShader (program, vertex);
	shader_procs.AttachShader (program, fragment);
	shader_procs.LinkProgram (program);
	shader_procs.GetProgramiv (program, GL_LINK_STATUS, &status);
	if (!status)
		goto fail;
	sky_uniform_sampler = shader_procs.GetUniformLocation (program, "u_sampler");
	if (sky_uniform_sampler < 0 || qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.DeleteShader (vertex);
	shader_procs.DeleteShader (fragment);
	sky_program = program;
	return true;
fail:
	if (vertex)
		shader_procs.DeleteShader (vertex);
	if (fragment)
		shader_procs.DeleteShader (fragment);
	if (program)
		shader_procs.DeleteProgram (program);
	ri.Con_Printf (PRINT_ALL, "Cubemap sky shader setup failed; retaining six-face skies.\n");
	return false;
}

static qboolean GL_CreateClassicSkyProgram (void)
{
	GLuint vertex = 0, fragment = 0, program = 0;
	GLint status = 0;
	if (classic_sky_attempted)
		return classic_sky_program != 0;
	classic_sky_attempted = true;
	if (!GL_ClearErrors ())
		return false;
	program = shader_procs.CreateProgram ();
	if (!program)
		goto fail;
	vertex = GL_CompileShader (GL_VERTEX_SHADER, sky_vertex_shader);
	fragment = GL_CompileShader (GL_FRAGMENT_SHADER, classic_sky_fragment_shader);
	if (!vertex || !fragment)
		goto fail;
	shader_procs.AttachShader (program, vertex);
	shader_procs.AttachShader (program, fragment);
	shader_procs.LinkProgram (program);
	shader_procs.GetProgramiv (program, GL_LINK_STATUS, &status);
	if (!status)
		goto fail;
	classic_sky_uniform_solid = shader_procs.GetUniformLocation (program, "u_solid");
	classic_sky_uniform_alpha = shader_procs.GetUniformLocation (program, "u_alpha");
	classic_sky_uniform_time = shader_procs.GetUniformLocation (program, "u_time");
	if (classic_sky_uniform_solid < 0 || classic_sky_uniform_alpha < 0 ||
		classic_sky_uniform_time < 0 || qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.DeleteShader (vertex);
	shader_procs.DeleteShader (fragment);
	classic_sky_program = program;
	return true;
fail:
	if (vertex)
		shader_procs.DeleteShader (vertex);
	if (fragment)
		shader_procs.DeleteShader (fragment);
	if (program)
		shader_procs.DeleteProgram (program);
	ri.Con_Printf (PRINT_ALL, "Classic sky shader setup failed; retaining global sky.\n");
	return false;
}

qboolean GL_SkyShaderReady (void)
{
	return GL_RenderShadersEnabled () &&
		shader_capable && sky_capable &&
		GL_CreateSkyProgram ();
}

qboolean GL_ClassicSkyShaderReady (void)
{
	return GL_RenderShadersEnabled () && shader_capable && classic_sky_capable &&
		GL_CreateClassicSkyProgram ();
}

void GL_EndSkyShader (void)
{
	if (!sky_active)
		return;
	shader_procs.UseProgram ((GLuint)sky_saved_program);
	qglActiveTextureARB (GL_TEXTURE0);
	if (sky_samplers)
		glow_bind_sampler (0, (GLuint)sky_saved_sampler);
	if (classic_sky_active)
	{
		qglActiveTextureARB (GL_TEXTURE1);
		if (sky_samplers)
			glow_bind_sampler (1, (GLuint)sky_saved_sampler1);
	}
	else if (sky_seamless && !sky_saved_seamless)
		qglDisable (GL_TEXTURE_CUBE_MAP_SEAMLESS);
	qglActiveTextureARB ((GLenum)sky_saved_active);
	GL_BloomTargets (false);
	sky_active = classic_sky_active = false;
}

qboolean GL_BeginSkyShader (void)
{
	if (sky_active || surface_active || !qglIsEnabled ||
		qglIsEnabled (GL_FOG) || qglIsEnabled (GL_LIGHTING) ||
		!GL_SkyShaderReady () || !GL_ClearErrors ())
		return false;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &sky_saved_program);
	qglGetIntegerv (GL_ACTIVE_TEXTURE, &sky_saved_active);
	if (qglGetError () != GL_NO_ERROR)
		return false;
	qglActiveTextureARB (GL_TEXTURE0);
	if (sky_samplers)
		qglGetIntegerv (GL_SAMPLER_BINDING, &sky_saved_sampler);
	if (sky_seamless)
		sky_saved_seamless = qglIsEnabled (GL_TEXTURE_CUBE_MAP_SEAMLESS);
	if (qglGetError () != GL_NO_ERROR)
	{
		qglActiveTextureARB ((GLenum)sky_saved_active);
		return false;
	}
	sky_active = true;
	classic_sky_active = false;
	if (sky_samplers)
		glow_bind_sampler (0, 0);
	if (sky_seamless)
		qglEnable (GL_TEXTURE_CUBE_MAP_SEAMLESS);
	shader_procs.UseProgram (sky_program);
	shader_procs.Uniform1i (sky_uniform_sampler, 0);
	if (qglGetError () != GL_NO_ERROR || !GL_BloomTargets (true))
	{
		GL_EndSkyShader ();
		GL_FreeSkyProgram ();
		GL_BloomAbort ();
		return false;
	}
	return true;
}

qboolean GL_BeginClassicSkyShader (float time)
{
	if (sky_active || surface_active || !qglIsEnabled ||
		qglIsEnabled (GL_FOG) || qglIsEnabled (GL_LIGHTING) ||
		!GL_ClassicSkyShaderReady () || !GL_ClearErrors ())
		return false;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &sky_saved_program);
	qglGetIntegerv (GL_ACTIVE_TEXTURE, &sky_saved_active);
	if (qglGetError () != GL_NO_ERROR)
		return false;
	qglActiveTextureARB (GL_TEXTURE0);
	if (sky_samplers)
		qglGetIntegerv (GL_SAMPLER_BINDING, &sky_saved_sampler);
	qglActiveTextureARB (GL_TEXTURE1);
	if (sky_samplers)
		qglGetIntegerv (GL_SAMPLER_BINDING, &sky_saved_sampler1);
	if (qglGetError () != GL_NO_ERROR)
	{
		qglActiveTextureARB ((GLenum)sky_saved_active);
		return false;
	}
	sky_active = classic_sky_active = true;
	if (sky_samplers)
	{
		glow_bind_sampler (0, 0);
		glow_bind_sampler (1, 0);
	}
	shader_procs.UseProgram (classic_sky_program);
	shader_procs.Uniform1i (classic_sky_uniform_solid, 0);
	shader_procs.Uniform1i (classic_sky_uniform_alpha, 1);
	shader_procs.Uniform1f (classic_sky_uniform_time, time);
	if (qglGetError () != GL_NO_ERROR || !GL_BloomTargets (true))
	{
		GL_EndSkyShader ();
		GL_FreeClassicSkyProgram ();
		GL_BloomAbort ();
		return false;
	}
	return true;
}

void GL_ShaderRegister (void)
{
	gl_shaders = ri.Cvar_Get ("gl_shaders", "1", CVAR_ARCHIVE);
	gl_gpulerp = ri.Cvar_Get ("gl_gpulerp", "1", CVAR_ARCHIVE);
	gl_dotshading = ri.Cvar_Get ("gl_dotshading", "1", 0);
	gl_celshading = ri.Cvar_Get ("gl_celshading", "0", 0);
	r_glowmaps = ri.Cvar_Get ("r_glowmaps", "1", 0);
	gl_glowmap_intensity = ri.Cvar_Get ("gl_glowmap_intensity", "0.75", 0);
}

void GL_InitShaders (void)
{
	GLint uniforms = 0, attributes = 0, texture_units = 0;
	qboolean samplers;
	shader_capable = shader_attempted = false;
	md3_shader_attempted = false;
	md5_shader_attempted = md5_capable = md5_common_capable = false;
	md5_ssbo_shader_attempted = md5_ssbo_capable = md5_pose_attempted = false;
	md5_ubo_bindings = md5_ssbo_bindings = 0;
	md5_next_texture = MD5_TEXTURE_FIRST;
	md5_pose_ready = false;
	md5_pose_joints = 0;
	memset (&md5_procs, 0, sizeof (md5_procs));
	memset (&md5_ssbo_procs, 0, sizeof (md5_ssbo_procs));
	glow_capable = glow_failed = false;
	glow_bind_sampler = NULL;
	shader_major = shader_minor = shader_attributes = 0;
	alias_program = md3_program = md5_program = md5_ssbo_program =
		md5_pose_buffer = 0;
	md5_skeleton_block = md5_ssbo_skeleton_block = 0;
	md5_ssbo_weight_block = md5_ssbo_joint_block = 0;
	surface_program = sky_program = classic_sky_program = point_program = 0;
	sky_capable = sky_attempted = sky_active = sky_samplers = sky_seamless = false;
	classic_sky_capable = classic_sky_attempted = classic_sky_active = false;
	surface_attempted = surface_active = false;
	point_attempted = point_active = false;
	memset (&shader_procs, 0, sizeof (shader_procs));
	alias_mode = GL_AliasRequestedMode ();
	if (!qwglGetProcAddress || !qglGetIntegerv || !qglGetError ||
		!gl_config.version_string ||
		sscanf (gl_config.version_string, "%d.%d", &shader_major, &shader_minor) != 2 ||
		shader_major < 3)
		return;
	qglGetIntegerv (GL_MAX_VERTEX_UNIFORM_COMPONENTS, &uniforms);
	qglGetIntegerv (GL_MAX_VERTEX_ATTRIBS, &attributes);
	if (uniforms < 1024 || attributes < ALIAS_SHADER_ATTRIBUTES)
		return;
	shader_attributes = attributes;

#define SHADER_PROC(name, result, parameters) \
	shader_procs.name = (result (APIENTRY *) parameters)GL_ShaderProcAddress ("gl" #name); \
	if (!shader_procs.name) goto unavailable
	SHADER_PROC (CreateShader, GLuint, (GLenum));
	SHADER_PROC (ShaderSource, void, (GLuint, GLsizei, const char *const *, const GLint *));
	SHADER_PROC (CompileShader, void, (GLuint));
	SHADER_PROC (GetShaderiv, void, (GLuint, GLenum, GLint *));
	SHADER_PROC (GetShaderInfoLog, void, (GLuint, GLsizei, GLsizei *, char *));
	SHADER_PROC (DeleteShader, void, (GLuint));
	SHADER_PROC (CreateProgram, GLuint, (void));
	SHADER_PROC (AttachShader, void, (GLuint, GLuint));
	SHADER_PROC (BindAttribLocation, void, (GLuint, GLuint, const char *));
	SHADER_PROC (LinkProgram, void, (GLuint));
	SHADER_PROC (GetProgramiv, void, (GLuint, GLenum, GLint *));
	SHADER_PROC (GetProgramInfoLog, void, (GLuint, GLsizei, GLsizei *, char *));
	SHADER_PROC (DeleteProgram, void, (GLuint));
	SHADER_PROC (UseProgram, void, (GLuint));
	SHADER_PROC (GetUniformLocation, GLint, (GLuint, const char *));
	SHADER_PROC (Uniform1i, void, (GLint, GLint));
	SHADER_PROC (Uniform1f, void, (GLint, GLfloat));
	SHADER_PROC (Uniform3fv, void, (GLint, GLsizei, const GLfloat *));
	SHADER_PROC (Uniform4fv, void, (GLint, GLsizei, const GLfloat *));
	SHADER_PROC (VertexAttribPointer, void, (GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *));
	SHADER_PROC (EnableVertexAttribArray, void, (GLuint));
	SHADER_PROC (DisableVertexAttribArray, void, (GLuint));
	SHADER_PROC (GetVertexAttribfv, void, (GLuint, GLenum, GLfloat *));
	SHADER_PROC (GenVertexArrays, void, (GLsizei, GLuint *));
	SHADER_PROC (DeleteVertexArrays, void, (GLsizei, const GLuint *));
	SHADER_PROC (BindVertexArray, void, (GLuint));
	SHADER_PROC (VertexAttrib4fv, void, (GLuint, const GLfloat *));
#undef SHADER_PROC
	shader_capable = true;
	GL_InitMD5Capabilities ();
	GL_InitMD5SSBOCapabilities ();
	qglGetIntegerv (GL_MAX_TEXTURE_IMAGE_UNITS, &texture_units);
	samplers = shader_major > 3 || (shader_major == 3 && shader_minor >= 3) ||
		GL_GlowExtension ("GL_ARB_sampler_objects");
	sky_samplers = samplers;
	sky_seamless = shader_major > 3 || (shader_major == 3 && shader_minor >= 2) ||
		GL_GlowExtension ("GL_ARB_seamless_cube_map");
	if (samplers)
		glow_bind_sampler = (void (APIENTRY *) (GLuint, GLuint))GL_ShaderProcAddress ("glBindSampler");
	sky_capable = qglGetError () == GL_NO_ERROR && texture_units >= 1 &&
		qglActiveTextureARB && qglBindTexture && (!samplers || glow_bind_sampler);
	classic_sky_capable = sky_capable && texture_units >= 2;
	glow_capable = sky_capable && texture_units >= 3;
	if (alias_mode & 1)
	{
		GL_CreateAliasProgram ();
		GL_CreateMD3Program ();
	}
	return;

unavailable:
	memset (&shader_procs, 0, sizeof (shader_procs));
	ri.Con_Printf (PRINT_ALL, "Alias shader procedures unavailable; retaining CPU interpolation.\n");
}

void GL_UpdateShaderOptions (void)
{
	unsigned mode = GL_AliasRequestedMode ();
	float value = gl_shaders ? gl_shaders->value : 0;
	qboolean requested = value >= -FLT_MAX && value <= FLT_MAX && FLOAT_NE_ZERO (value);
	if (GL_ImageColorProfileReady () && requested != ((alias_mode & 1) != 0))
	{
		/* Texture color policy and shader lifetime change as one restart. */
		ri.Cvar_Get ("vid_ref", "gl", CVAR_ARCHIVE)->modified = true;
		return;
	}
	if (mode == alias_mode)
		return;
	GL_EndMD5GPUPass ();
	Mod_FreeAliasBuffers ();
	Mod_FreeMD3Buffers ();
	Mod_FreeMD5Buffers ();
	alias_mode = mode;
	if (!(mode & 1))
	{
		GL_FreeAliasProgram ();
		GL_FreeMD3Program ();
		GL_FreeMD5Program ();
		GL_FreeMD5SSBOProgram ();
		GL_FreeMD5PoseBuffer ();
		GL_EndSurfaceShader ();
		GL_FreeSurfaceProgram ();
		GL_FreePointProgram ();
		GL_EndSkyShader ();
		GL_FreeSkyProgram ();
		GL_FreeClassicSkyProgram ();
		sky_attempted = false;
		classic_sky_attempted = false;
		surface_attempted = false;
		point_attempted = false;
	}
	else
	{
		md5_shader_attempted = md5_ssbo_shader_attempted = false;
		md5_pose_attempted = false;
		if (!alias_program)
		{
			shader_attempted = false;
			GL_CreateAliasProgram ();
		}
		if (!md3_program)
		{
			md3_shader_attempted = false;
			GL_CreateMD3Program ();
		}
	}
}

void GL_FreeAliasBuffer (maliasmesh_t *mesh)
{
	if (!mesh)
		return;
	GL_ClearAliasGPUFrame (mesh);
	if (mesh->gpu_vertex_array)
		shader_procs.DeleteVertexArrays (1, &mesh->gpu_vertex_array);
	if (mesh->gpu_buffer)
	{
		if (qglDeleteBuffers)
			qglDeleteBuffers (1, &mesh->gpu_buffer);
		alias_gpu_bytes -= mesh->gpu_bytes;
	}
	mesh->gpu_buffer = mesh->gpu_vertex_array = mesh->gpu_bytes = 0;
	mesh->gpu_failed = false;
}

void GL_FreeMD3Buffer (mmd3mesh_t *mesh)
{
	if (!mesh)
		return;
	if (mesh->gpu_vertex_array && shader_procs.DeleteVertexArrays)
		shader_procs.DeleteVertexArrays (1, &mesh->gpu_vertex_array);
	if (mesh->gpu_buffer)
	{
		if (qglDeleteBuffers)
			qglDeleteBuffers (1, &mesh->gpu_buffer);
		alias_gpu_bytes = mesh->gpu_bytes <= alias_gpu_bytes ?
			alias_gpu_bytes - mesh->gpu_bytes :
			0;
	}
	mesh->gpu_buffer = mesh->gpu_vertex_array = mesh->gpu_bytes = 0;
	mesh->gpu_failed = false;
}

static void GL_FreeMD5TBOViews (mmd5mesh_t *mesh)
{
	GLuint textures[2];
	textures[0] = mesh->gpu_weight_texture;
	textures[1] = mesh->gpu_joint_texture;
	if ((textures[0] || textures[1]) && qglDeleteTextures)
		qglDeleteTextures (2, textures);
	mesh->gpu_weight_texture = mesh->gpu_joint_texture = 0;
}

void GL_FreeMD5Buffer (mmd5mesh_t *mesh)
{
	GLuint buffers[3];
	if (!mesh)
		return;
	if (mesh->gpu_vertex_array && shader_procs.DeleteVertexArrays)
		shader_procs.DeleteVertexArrays (1, &mesh->gpu_vertex_array);
	GL_FreeMD5TBOViews (mesh);
	buffers[0] = mesh->gpu_buffer;
	buffers[1] = mesh->gpu_weights;
	buffers[2] = mesh->gpu_joints;
	if (buffers[0] || buffers[1] || buffers[2])
	{
		if (qglDeleteBuffers)
			qglDeleteBuffers (3, buffers);
		alias_gpu_bytes = mesh->gpu_bytes <= alias_gpu_bytes ?
			alias_gpu_bytes - mesh->gpu_bytes :
			0;
	}
	mesh->gpu_buffer = mesh->gpu_weights = mesh->gpu_joints = 0;
	mesh->gpu_vertex_array = mesh->gpu_bytes = 0;
	mesh->gpu_failed = mesh->gpu_tbo_failed = false;
}

void GL_ShutdownShaders (void)
{
	Mod_FreeAliasBuffers ();
	Mod_FreeMD3Buffers ();
	Mod_FreeMD5Buffers ();
	GL_EndMD5GPUPass ();
	GL_ClearAliasGPUFrame (NULL);
	GL_FreeAliasProgram ();
	GL_FreeMD3Program ();
	GL_FreeMD5Program ();
	GL_FreeMD5SSBOProgram ();
	GL_FreeMD5PoseBuffer ();
	GL_EndSurfaceShader ();
	GL_FreeSurfaceProgram ();
	GL_FreePointProgram ();
	GL_EndSkyShader ();
	GL_FreeSkyProgram ();
	GL_FreeClassicSkyProgram ();
	sky_capable = sky_attempted = sky_samplers = sky_seamless = false;
	classic_sky_capable = classic_sky_attempted = classic_sky_active = false;
	surface_attempted = point_attempted = false;
	shader_capable = shader_attempted = md3_shader_attempted = false;
	md5_shader_attempted = md5_capable = md5_common_capable = false;
	md5_ssbo_shader_attempted = md5_ssbo_capable = md5_pose_attempted = false;
	md5_ubo_bindings = md5_ssbo_bindings = 0;
	md5_next_texture = MD5_TEXTURE_FIRST;
	glow_capable = glow_failed = false;
	glow_bind_sampler = NULL;
	shader_major = shader_minor = shader_attributes = 0;
	alias_mode = 0;
	memset (&shader_procs, 0, sizeof (shader_procs));
	memset (&md5_procs, 0, sizeof (md5_procs));
	memset (&md5_ssbo_procs, 0, sizeof (md5_ssbo_procs));
	GL_ResetShaderStats ();
}

void GL_ResetShaderStats (void)
{
	alias_gpu_draws = alias_gpu_shadows = 0;
	md3_gpu_draws = 0;
	md5_gpu_draws = 0;
	surface_world_draws = surface_ui_draws = 0;
}

void GL_PrintShaderInfo (void)
{
	ri.Con_Printf (PRINT_ALL, "%u GPU alias draws (%u shadows), %u GPU model bytes, %s interpolation\n",
		alias_gpu_draws, alias_gpu_shadows, alias_gpu_bytes,
		GL_AliasGPUAvailable () ? "compatibility GLSL" : "CPU");
	ri.Con_Printf (PRINT_ALL, "%u GPU MD3 draws, %s interpolation\n",
		md3_gpu_draws, GL_MD3GPUAvailable () ? "compatibility GLSL" : "CPU");
	ri.Con_Printf (PRINT_ALL, "%u GPU MD5 draws, %s skinning\n",
		md5_gpu_draws, GL_MD5SSBOAvailable () ? "SSBO GLSL" : GL_MD5TBOAvailable () ? "texture-buffer GLSL" :
																					  "CPU");
	ri.Con_Printf (PRINT_ALL, "%u lightmapped and %u 2D shader submissions\n",
		surface_world_draws, surface_ui_draws);
	ri.Con_Printf (PRINT_ALL, "Image intensity: %s, %.3g\n",
		GL_ImageShaderIntensity () ? "live material shader" : "cached CPU profile",
		GL_ImageShaderIntensity () ? GL_ImageIntensityScale () : (intensity ? intensity->value : 2));
}

static qboolean GL_AliasBufferLayout (model_t *mod, dmdl_t *header,
	unsigned *frame_bytes, unsigned *st_offset, unsigned *index_offset, unsigned *bytes)
{
	maliasmesh_t *mesh = mod->aliasmesh;
	unsigned frames_end;
	if (!mesh || header != mod->extradata || header->num_frames < 1 ||
		header->num_frames > MAX_FRAMES || header->num_xyz < 1 ||
		header->num_xyz > MAX_VERTS || header->ofs_frames < (int)sizeof (*header) ||
		header->framesize < (int)(sizeof (daliasframe_t) - sizeof (dtrivertx_t) +
								header->num_xyz * sizeof (dtrivertx_t)) ||
		mod->extradatasize < 0 ||
		!mesh->numverts || mesh->numverts > ALIAS_MESH_MAX_VERTICES ||
		!mesh->numindices || mesh->numindices > ALIAS_MESH_MAX_INDICES)
		return false;
	if ((unsigned)header->ofs_frames > (unsigned)mod->extradatasize ||
		(unsigned)header->num_frames >
			((unsigned)mod->extradatasize - header->ofs_frames) / (unsigned)header->framesize)
		return false;
	frames_end = header->ofs_frames + header->num_frames * header->framesize;
	if (frames_end > (unsigned)mod->extradatasize)
		return false;
	*frame_bytes = mesh->numverts * sizeof (dtrivertx_t);
	*st_offset = header->num_frames * *frame_bytes;
	*index_offset = *st_offset + mesh->numverts * 2 * sizeof (float);
	*bytes = *index_offset + mesh->numindices * sizeof (GLuint);
	return *bytes <= ALIAS_GPU_MAX_BYTES && *bytes <= INT_MAX;
}

void GL_BuildAliasBuffer (model_t *mod, dmdl_t *header)
{
	maliasmesh_t *mesh = mod->aliasmesh;
	GLuint buffer = 0, vertex_array = 0;
	GLint saved = 0, saved_vertex_array = 0, actual = 0;
	unsigned frame_bytes, st_offset, index_offset, bytes, i;
	int frame, index;
	daliasframe_t *source;

	if (!mesh || mesh->gpu_buffer || mesh->gpu_failed || !GL_AliasGPUAvailable ())
		return;
	if (!GL_AliasBufferLayout (mod, header, &frame_bytes, &st_offset, &index_offset, &bytes) ||
		bytes > ALIAS_GPU_MAX_BYTES - alias_gpu_bytes || !GL_ClearErrors ())
		goto unavailable;
	qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved);
	qglGetIntegerv (GL_VERTEX_ARRAY_BINDING, &saved_vertex_array);
	qglGenBuffers (1, &buffer);
	if (!buffer)
		goto unavailable;
	qglBindBuffer (GL_ARRAY_BUFFER, buffer);
	qglBufferData (GL_ARRAY_BUFFER, (GLsizeiptr)bytes, NULL, GL_STATIC_DRAW);
	qglGetBufferParameteriv (GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &actual);
	if (actual != (GLint)bytes || qglGetError () != GL_NO_ERROR)
		goto fail;
	for (frame = 0; frame < header->num_frames; frame++)
	{
		source = (daliasframe_t *)((byte *)header + header->ofs_frames + frame * header->framesize);
		for (i = 0; i < mesh->numverts; i++)
		{
			index = mesh->vertices[i].index_xyz;
			if (index < 0 || index >= header->num_xyz || source->verts[index].lightnormalindex >= 162)
				goto fail;
			alias_upload.frame[i] = source->verts[index];
		}
		qglBufferSubData (GL_ARRAY_BUFFER, (GLintptr)(frame * frame_bytes),
			(GLsizeiptr)frame_bytes, alias_upload.frame);
		if (qglGetError () != GL_NO_ERROR)
			goto fail;
	}
	for (i = 0; i < mesh->numverts; i++)
		memcpy (alias_upload.st[i], mesh->vertices[i].st, sizeof (alias_upload.st[i]));
	qglBufferSubData (GL_ARRAY_BUFFER, (GLintptr)st_offset,
		(GLsizeiptr)(mesh->numverts * sizeof (alias_upload.st[0])), alias_upload.st);
	qglBufferSubData (GL_ARRAY_BUFFER, (GLintptr)index_offset,
		(GLsizeiptr)(mesh->numindices * sizeof (GLuint)), mesh->indices);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.GenVertexArrays (1, &vertex_array);
	if (!vertex_array)
		goto fail;
	shader_procs.BindVertexArray (vertex_array);
	qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, buffer);
	shader_procs.VertexAttribPointer (0, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (dtrivertx_t), NULL);
	shader_procs.VertexAttribPointer (1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (dtrivertx_t), NULL);
	shader_procs.VertexAttribPointer (2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof (float),
		(const GLvoid *)(size_t)st_offset);
	for (i = 0; i < ALIAS_SHADER_ATTRIBUTES; i++)
		shader_procs.EnableVertexAttribArray (i);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.BindVertexArray ((GLuint)saved_vertex_array);
	qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved);
	mesh->gpu_buffer = buffer;
	mesh->gpu_vertex_array = vertex_array;
	mesh->gpu_bytes = bytes;
	alias_gpu_bytes += bytes;
	return;

fail:
	shader_procs.BindVertexArray ((GLuint)saved_vertex_array);
	qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved);
	if (vertex_array)
		shader_procs.DeleteVertexArrays (1, &vertex_array);
	qglDeleteBuffers (1, &buffer);
unavailable:
	mesh->gpu_failed = true;
	ri.Con_Printf (PRINT_ALL, "%s: GPU alias storage unavailable; retaining CPU interpolation.\n", mod->name);
}

/* Use the same interpolated GPU geometry for depth and blended color. */
static qboolean GL_WeaponDepthPrefill (float alpha)
{
	return (currententity->flags & RF_WEAPONMODEL) &&
		!(currententity->flags & RF_FULLBRIGHT) && alpha > 0 && alpha < 1;
}

static qboolean GL_DrawWeaponDepthPrefillTyped (GLsizei count,
	GLenum index_type, const GLvoid *indices)
{
	qglPushAttrib (GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	qglEnable (GL_DEPTH_TEST);
	qglDepthMask (GL_TRUE);
	qglDisable (GL_BLEND);
	qglDisable (GL_ALPHA_TEST);
	qglDisable (GL_TEXTURE_2D);
	qglColorMask (GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	qglDrawElements (GL_TRIANGLES, count, index_type, indices);
	qglPopAttrib ();
	return qglGetError () == GL_NO_ERROR;
}

static qboolean GL_DrawWeaponDepthPrefillElements (GLsizei count,
	const GLvoid *indices)
{
	return GL_DrawWeaponDepthPrefillTyped (count, GL_UNSIGNED_INT, indices);
}

qboolean GL_DrawAliasGPU (dmdl_t *header, dtrivertx_t *verts,
	dtrivertx_t *oldverts, const float *move, const float *frontv,
	const float *backv, float alpha, float celscale, qboolean shadow,
	float lheight, GLuint glowtexnum)
{
	glglowstate_t glow_state;
	int glowkind = 0;
	maliasmesh_t *mesh = currentmodel->aliasmesh;
	GLfloat saved_attributes[ALIAS_SHADER_ATTRIBUTES - 1][4];
	GLint saved_array = 0, saved_vertex_array = 0, saved_program = 0;
	unsigned frame_bytes, st_offset, index_offset, bytes, newframe, oldframe;
	unsigned i;
	GLfloat color[4];
	qboolean failed = false, drawn = false;
	qboolean shell = currententity->flags & RF_SHELL_MASK;

	if (!mesh || mesh->gpu_failed || !GL_AliasGPUAvailable () ||
		!GL_AliasBufferLayout (currentmodel, header, &frame_bytes, &st_offset, &index_offset, &bytes))
		return false;
	newframe = (unsigned)currententity->frame;
	oldframe = (unsigned)currententity->oldframe;
	if (newframe >= (unsigned)header->num_frames || oldframe >= (unsigned)header->num_frames ||
		verts != ((daliasframe_t *)((byte *)header + header->ofs_frames + newframe * header->framesize))->verts ||
		oldverts != ((daliasframe_t *)((byte *)header + header->ofs_frames + oldframe * header->framesize))->verts)
		return false;
	for (i = 0; i < 3; i++)
		if (!(move[i] >= -FLT_MAX && move[i] <= FLT_MAX) ||
			!(frontv[i] >= -FLT_MAX && frontv[i] <= FLT_MAX) ||
			!(backv[i] >= -FLT_MAX && backv[i] <= FLT_MAX))
			return false;
	GL_BuildAliasBuffer (currentmodel, header);
	if (!mesh->gpu_buffer || !GL_ClearErrors ())
		return false;
	memset (&glow_state, 0, sizeof (glow_state));
	if (!shell && !shadow && glowtexnum && GL_BeginGlowMap (glowtexnum, &glow_state))
		glowkind = 2;
	qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_array);
	qglGetIntegerv (GL_VERTEX_ARRAY_BINDING, &saved_vertex_array);
	qglGetIntegerv (GL_CURRENT_PROGRAM, &saved_program);
	for (i = 1; i < ALIAS_SHADER_ATTRIBUTES; i++)
		shader_procs.GetVertexAttribfv (i, GL_CURRENT_VERTEX_ATTRIB, saved_attributes[i - 1]);
	if (shadow)
		qglGetFloatv (GL_CURRENT_COLOR, color);
	else
	{
		memcpy (color, shadelight, sizeof (vec3_t));
		color[3] = alpha;
	}
	qglPushAttrib (GL_CURRENT_BIT);
	shader_procs.BindVertexArray (mesh->gpu_vertex_array);
	qglBindBuffer (GL_ARRAY_BUFFER, mesh->gpu_buffer);
	shader_procs.VertexAttribPointer (0, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (dtrivertx_t),
		(const GLvoid *)(size_t)(newframe * frame_bytes));
	shader_procs.VertexAttribPointer (1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (dtrivertx_t),
		(const GLvoid *)(size_t)(oldframe * frame_bytes));
	shader_procs.UseProgram (alias_program);
	shader_procs.Uniform3fv (uniform_move, 1, move);
	shader_procs.Uniform3fv (uniform_front, 1, frontv);
	shader_procs.Uniform3fv (uniform_back, 1, backv);
	shader_procs.Uniform4fv (uniform_color, 1, color);
	shader_procs.Uniform1f (uniform_backlerp, currententity->backlerp);
	shader_procs.Uniform1i (uniform_dotshading,
		!gl_dotshading || FLOAT_NE_ZERO (gl_dotshading->value));
	shader_procs.Uniform1i (uniform_shell, shell != 0);
	shader_procs.Uniform1i (uniform_shadow, shadow ? (gl_shadows->value >= 2 ? 2 : 1) : 0);
	shader_procs.Uniform3fv (uniform_shadevector, 1, shadevector);
	shader_procs.Uniform1f (uniform_height, lheight);
	shader_procs.Uniform1i (uniform_texture, !shell && !shadow);
	shader_procs.Uniform1f (uniform_intensity, !shell && !shadow ? GL_ImageIntensityScale () : 0);
	shader_procs.Uniform1i (uniform_glowmap, glowkind ? 2 : 0);
	shader_procs.Uniform1i (uniform_glowkind, glowkind);
	shader_procs.Uniform1f (uniform_glowscale, GL_GlowmapScale ());
	shader_procs.Uniform1i (uniform_bloommode, GL_BloomActive () ? (shell && !shadow ? 2 : 1) : 0);
	if (qglGetError () == GL_NO_ERROR && GL_BloomTargets (true))
	{
		if (!shadow && GL_WeaponDepthPrefill (alpha))
		{
			failed = !GL_DrawWeaponDepthPrefillElements (mesh->numindices,
				(const GLvoid *)(size_t)index_offset);
			alias_gpu_draws++;
		}
		if (!failed)
		{
			qglDrawElements (GL_TRIANGLES, mesh->numindices, GL_UNSIGNED_INT,
				(const GLvoid *)(size_t)index_offset);
			drawn = true;
			failed = qglGetError () != GL_NO_ERROR;
			alias_gpu_draws++;
			if (shadow)
				alias_gpu_shadows++;
		}
		if (!failed && !shadow && celscale > 0 && gl_celshading &&
			qglPolygonMode && qglLineWidth && qglCullFace && qglBlendFunc)
		{
			float width = gl_celshading->value;
			GLfloat black[4] = { 0, 0, 0, celscale };
			if (width > 10)
				width = 10;
			qglPushAttrib (GL_CURRENT_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
				GL_POLYGON_BIT | GL_LINE_BIT);
			qglEnable (GL_BLEND);
			qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			qglEnable (GL_CULL_FACE);
			qglCullFace ((currententity->flags & RF_WEAPONMODEL) &&
						r_lefthand && r_lefthand->value == 1.0f ?
					GL_FRONT :
					GL_BACK);
			qglPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
			qglLineWidth (width * celscale);
			shader_procs.Uniform4fv (uniform_color, 1, black);
			shader_procs.Uniform1i (uniform_texture, 0);
			shader_procs.Uniform1i (uniform_dotshading, 0);
			shader_procs.Uniform1i (uniform_glowkind, 0);
			shader_procs.Uniform1i (uniform_bloommode, GL_BloomActive () ? 1 : 0);
			shader_procs.Uniform1f (uniform_intensity, 0);
			if (qglGetError () == GL_NO_ERROR)
			{
				qglDrawElements (GL_TRIANGLES, mesh->numindices, GL_UNSIGNED_INT,
					(const GLvoid *)(size_t)index_offset);
				alias_gpu_draws++;
				failed = qglGetError () != GL_NO_ERROR;
			}
			else
				failed = true;
			qglPopAttrib ();
		}
		if (!failed && !shadow)
			GL_ShowTrisElements (GL_SHOWTRIS_MESH, GL_TRIANGLES, mesh->numindices,
				GL_UNSIGNED_INT, (const GLvoid *)(size_t)index_offset);
	}
	else
		failed = true;
	GL_BloomTargets (false);
	if (failed)
		GL_BloomAbort ();
	shader_procs.UseProgram ((GLuint)saved_program);
	GL_EndGlowMap (&glow_state);
	shader_procs.BindVertexArray ((GLuint)saved_vertex_array);
	qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
	qglPopAttrib ();
	for (i = 1; i < ALIAS_SHADER_ATTRIBUTES; i++)
		shader_procs.VertexAttrib4fv (i, saved_attributes[i - 1]);
	if (failed)
	{
		GL_FreeAliasBuffer (mesh);
		mesh->gpu_failed = true;
		ri.Con_Printf (PRINT_ALL, "%s: alias shader draw failed; using CPU on subsequent draws.\n", currentmodel->name);
	}
	return drawn; // Never repeat a possibly submitted translucent draw.
}

/* MD3 GPU interpolation: packed frame-major vertices, UVs and indices upload once. */
static qboolean GL_MD3BufferLayout (model_t *mod, mmd3mesh_t *mesh,
	unsigned *frame_bytes, unsigned *st_offset, unsigned *index_offset,
	unsigned *bytes)
{
	unsigned frames_bytes, st_bytes, index_bytes;
	if (!mod || mod->type != mod_md3 || !mod->md3 || !mesh ||
		mod->md3->numframes < 1 || mod->md3->numframes > MD3_MAX_FRAMES ||
		mesh->numverts < 3 || mesh->numverts > MD3_MAX_VERTICES ||
		mesh->numtris < 1 || mesh->numtris > MD3_MAX_DRAW_TRIANGLES ||
		!mesh->vertices || !mesh->st || !mesh->indices ||
		sizeof (unsigned) != sizeof (GLuint))
		return false;
	*frame_bytes = (unsigned)mesh->numverts * sizeof (mmd3vertex_t);
	if ((unsigned)mod->md3->numframes > ALIAS_GPU_MAX_BYTES / *frame_bytes)
		return false;
	frames_bytes = (unsigned)mod->md3->numframes * *frame_bytes;
	st_bytes = (unsigned)mesh->numverts * sizeof (mesh->st[0]);
	index_bytes = (unsigned)mesh->numtris * 3 * sizeof (GLuint);
	if (frames_bytes > ALIAS_GPU_MAX_BYTES - st_bytes ||
		frames_bytes + st_bytes > ALIAS_GPU_MAX_BYTES - index_bytes)
		return false;
	*st_offset = frames_bytes;
	*index_offset = frames_bytes + st_bytes;
	*bytes = *index_offset + index_bytes;
	return *bytes <= INT_MAX;
}

static void GL_BuildMD3Buffer (model_t *mod, mmd3mesh_t *mesh)
{
	GLuint buffer = 0, vertex_array = 0;
	GLint saved = 0, saved_vertex_array = 0, actual = 0;
	unsigned frame_bytes, st_offset, index_offset, bytes, i;

	if (!mesh || mesh->gpu_buffer || mesh->gpu_failed || !GL_MD3GPUAvailable ())
		return;
	if (!GL_MD3BufferLayout (mod, mesh, &frame_bytes, &st_offset,
			&index_offset, &bytes) ||
		bytes > ALIAS_GPU_MAX_BYTES - alias_gpu_bytes || !GL_ClearErrors ())
		goto unavailable;
	for (i = 0; i < (unsigned)mesh->numtris * 3; i++)
		if (mesh->indices[i] >= (unsigned)mesh->numverts)
			goto unavailable;
	qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved);
	qglGetIntegerv (GL_VERTEX_ARRAY_BINDING, &saved_vertex_array);
	qglGenBuffers (1, &buffer);
	if (!buffer)
		goto unavailable;
	qglBindBuffer (GL_ARRAY_BUFFER, buffer);
	qglBufferData (GL_ARRAY_BUFFER, (GLsizeiptr)bytes, NULL, GL_STATIC_DRAW);
	qglGetBufferParameteriv (GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &actual);
	if (actual != (GLint)bytes || qglGetError () != GL_NO_ERROR)
		goto fail;
	qglBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr)st_offset, mesh->vertices);
	qglBufferSubData (GL_ARRAY_BUFFER, (GLintptr)st_offset,
		(GLsizeiptr)((unsigned)mesh->numverts * sizeof (mesh->st[0])), mesh->st);
	qglBufferSubData (GL_ARRAY_BUFFER, (GLintptr)index_offset,
		(GLsizeiptr)((unsigned)mesh->numtris * 3 * sizeof (GLuint)), mesh->indices);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.GenVertexArrays (1, &vertex_array);
	if (!vertex_array)
		goto fail;
	shader_procs.BindVertexArray (vertex_array);
	qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, buffer);
	shader_procs.VertexAttribPointer (0, 3, GL_SHORT, GL_FALSE,
		sizeof (mmd3vertex_t), NULL);
	shader_procs.VertexAttribPointer (1, 3, GL_SHORT, GL_FALSE,
		sizeof (mmd3vertex_t), NULL);
	shader_procs.VertexAttribPointer (2, 2, GL_UNSIGNED_BYTE, GL_FALSE,
		sizeof (mmd3vertex_t), (const GLvoid *)offsetof (mmd3vertex_t, normal));
	shader_procs.VertexAttribPointer (3, 2, GL_UNSIGNED_BYTE, GL_FALSE,
		sizeof (mmd3vertex_t), (const GLvoid *)offsetof (mmd3vertex_t, normal));
	shader_procs.VertexAttribPointer (4, 2, GL_FLOAT, GL_FALSE,
		sizeof (mesh->st[0]), (const GLvoid *)(size_t)st_offset);
	for (i = 0; i < MD3_SHADER_ATTRIBUTES; i++)
		shader_procs.EnableVertexAttribArray (i);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.BindVertexArray ((GLuint)saved_vertex_array);
	qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved);
	mesh->gpu_buffer = buffer;
	mesh->gpu_vertex_array = vertex_array;
	mesh->gpu_bytes = bytes;
	alias_gpu_bytes += bytes;
	return;

fail:
	shader_procs.BindVertexArray ((GLuint)saved_vertex_array);
	qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved);
	if (vertex_array)
		shader_procs.DeleteVertexArrays (1, &vertex_array);
	if (buffer)
		qglDeleteBuffers (1, &buffer);
unavailable:
	mesh->gpu_failed = true;
	ri.Con_Printf (PRINT_ALL, "%s: GPU MD3 storage unavailable; retaining CPU interpolation.\n",
		mod->name);
}

qboolean GL_DrawMD3GPU (mmd3mesh_t *mesh, const vec3_t move,
	float backlerp, float alpha, GLuint glowtexnum)
{
	glglowstate_t glow_state;
	GLfloat saved_attributes[MD3_SHADER_ATTRIBUTES - 1][4];
	GLfloat color[4];
	GLint saved_array = 0, saved_vertex_array = 0, saved_program = 0;
	unsigned frame_bytes, st_offset, index_offset, bytes, newframe, oldframe;
	unsigned i;
	int glowkind = 0;
	qboolean failed = false, drawn = false;
	qboolean outlines = GL_ShowTrisActive ();

	if (!mesh || mesh->gpu_failed || !currentmodel || !currententity)
		return false;
	if (!md3_program && !md3_shader_attempted &&
		(GL_AliasRequestedMode () & 1))
		GL_CreateMD3Program ();
	if (!GL_MD3GPUAvailable () ||
		!GL_MD3BufferLayout (currentmodel, mesh, &frame_bytes,
			&st_offset, &index_offset, &bytes) ||
		!(backlerp >= 0 && backlerp <= 1) ||
		!(alpha >= 0 && alpha <= FLT_MAX))
		return false;
	newframe = (unsigned)currententity->frame;
	oldframe = (unsigned)currententity->oldframe;
	if (newframe >= (unsigned)currentmodel->md3->numframes ||
		oldframe >= (unsigned)currentmodel->md3->numframes)
		return false;
	for (i = 0; i < 3; i++)
		if (!(move[i] >= -FLT_MAX && move[i] <= FLT_MAX))
			return false;
	GL_BuildMD3Buffer (currentmodel, mesh);
	if (!mesh->gpu_buffer || !GL_ClearErrors ())
		return false;
	memset (&glow_state, 0, sizeof (glow_state));
	if (glowtexnum && GL_BeginGlowMap (glowtexnum, &glow_state))
		glowkind = 2;
	qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_array);
	qglGetIntegerv (GL_VERTEX_ARRAY_BINDING, &saved_vertex_array);
	qglGetIntegerv (GL_CURRENT_PROGRAM, &saved_program);
	for (i = 1; i < MD3_SHADER_ATTRIBUTES; i++)
		shader_procs.GetVertexAttribfv (i, GL_CURRENT_VERTEX_ATTRIB,
			saved_attributes[i - 1]);
	memcpy (color, shadelight, sizeof (vec3_t));
	color[3] = alpha;
	qglPushAttrib (GL_CURRENT_BIT);
	shader_procs.BindVertexArray (mesh->gpu_vertex_array);
	qglBindBuffer (GL_ARRAY_BUFFER, mesh->gpu_buffer);
	shader_procs.VertexAttribPointer (0, 3, GL_SHORT, GL_FALSE,
		sizeof (mmd3vertex_t), (const GLvoid *)(size_t)(newframe * frame_bytes));
	shader_procs.VertexAttribPointer (1, 3, GL_SHORT, GL_FALSE,
		sizeof (mmd3vertex_t), (const GLvoid *)(size_t)(oldframe * frame_bytes));
	shader_procs.VertexAttribPointer (2, 2, GL_UNSIGNED_BYTE, GL_FALSE,
		sizeof (mmd3vertex_t),
		(const GLvoid *)(size_t)(newframe * frame_bytes + offsetof (mmd3vertex_t, normal)));
	shader_procs.VertexAttribPointer (3, 2, GL_UNSIGNED_BYTE, GL_FALSE,
		sizeof (mmd3vertex_t),
		(const GLvoid *)(size_t)(oldframe * frame_bytes + offsetof (mmd3vertex_t, normal)));
	shader_procs.UseProgram (md3_program);
	shader_procs.Uniform3fv (md3_uniform.move, 1, move);
	shader_procs.Uniform1f (md3_uniform.backlerp, backlerp);
	shader_procs.Uniform4fv (md3_uniform.color, 1, color);
	shader_procs.Uniform3fv (md3_uniform.shadevector, 1, shadevector);
	shader_procs.Uniform1i (md3_uniform.dotshading,
		!gl_dotshading || FLOAT_NE_ZERO (gl_dotshading->value));
	shader_procs.Uniform1i (md3_uniform.texture, 1);
	shader_procs.Uniform1f (md3_uniform.intensity, GL_ImageIntensityScale ());
	shader_procs.Uniform1i (md3_uniform.glowmap, glowkind ? 2 : 0);
	shader_procs.Uniform1i (md3_uniform.glowkind, glowkind);
	shader_procs.Uniform1f (md3_uniform.glowscale, GL_GlowmapScale ());
	shader_procs.Uniform1i (md3_uniform.bloommode, GL_BloomActive () ? 1 : 0);
	if (qglGetError () == GL_NO_ERROR && GL_BloomTargets (true))
	{
		if (!outlines && GL_WeaponDepthPrefill (alpha))
		{
			failed = !GL_DrawWeaponDepthPrefillElements (mesh->numtris * 3,
				(const GLvoid *)(size_t)index_offset);
			md3_gpu_draws++;
		}
		if (!failed)
		{
			if (outlines)
				GL_SetShowTrisShader (true);
			qglDrawElements (GL_TRIANGLES, mesh->numtris * 3, GL_UNSIGNED_INT,
				(const GLvoid *)(size_t)index_offset);
			if (outlines)
				GL_SetShowTrisShader (false);
			drawn = true;
			failed = qglGetError () != GL_NO_ERROR;
			if (!outlines)
				md3_gpu_draws++;
		}
	}
	else
		failed = true;
	GL_BloomTargets (false);
	if (failed)
		GL_BloomAbort ();
	shader_procs.UseProgram ((GLuint)saved_program);
	GL_EndGlowMap (&glow_state);
	shader_procs.BindVertexArray ((GLuint)saved_vertex_array);
	qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
	qglPopAttrib ();
	for (i = 1; i < MD3_SHADER_ATTRIBUTES; i++)
		shader_procs.VertexAttrib4fv (i, saved_attributes[i - 1]);
	if (failed)
	{
		GL_FreeMD3Buffer (mesh);
		mesh->gpu_failed = true;
		ri.Con_Printf (PRINT_ALL, "%s: MD3 shader draw failed; using CPU on subsequent draws.\n",
			currentmodel->name);
	}
	return drawn; /* Never repeat a possibly submitted translucent draw. */
}

qboolean GL_BeginMD5GPUPass (const mmd5joint_t *pose, int numjoints)
{
	GLint saved = 0;
	unsigned mode = GL_AliasRequestedMode ();
	int i, j;

	md5_pose_ready = false;
	md5_pose_joints = 0;
	if (!pose || numjoints < 1 || numjoints > MD5_MAX_JOINTS ||
		(mode & 57) != 57 || !(mode & 6) || !md5_common_capable ||
		((mode & 2) && (shader_major < 4 || (shader_major == 4 && shader_minor < 3))))
		return false;
	if (!md5_ssbo_program && !md5_ssbo_shader_attempted)
		GL_CreateMD5SSBOProgram ();
	if (!md5_program && !md5_shader_attempted)
		GL_CreateMD5Program ();
	if ((!md5_ssbo_program && !md5_program) ||
		!GL_CreateMD5PoseBuffer () || !GL_MD5GPUAvailable () ||
		!GL_ClearErrors ())
		return false;
	for (i = 0; i < numjoints; i++)
	{
		for (j = 0; j < 3; j++)
			md5_pose_upload[i][j] = pose[i].pos[j];
		md5_pose_upload[i][3] = pose[i].scale;
		for (j = 0; j < 4; j++)
			md5_pose_upload[i][4 + j] = pose[i].orient[j];
		for (j = 0; j < 8; j++)
			if (!(md5_pose_upload[i][j] >= -FLT_MAX &&
					md5_pose_upload[i][j] <= FLT_MAX))
				return false;
	}
	qglGetIntegerv (GL_UNIFORM_BUFFER_BINDING, &saved);
	if (qglGetError () != GL_NO_ERROR)
		return false;
	qglBindBuffer (GL_UNIFORM_BUFFER, md5_pose_buffer);
	qglBufferData (GL_UNIFORM_BUFFER, MD5_GPU_JOINT_BYTES, NULL,
		GL_STREAM_DRAW);
	qglBufferSubData (GL_UNIFORM_BUFFER, 0,
		(GLsizeiptr)(numjoints * sizeof (md5_pose_upload[0])), md5_pose_upload);
	qglBindBuffer (GL_UNIFORM_BUFFER, (GLuint)saved);
	if (qglGetError () != GL_NO_ERROR)
	{
		GL_FreeMD5PoseBuffer ();
		md5_pose_attempted = true;
		return false;
	}
	md5_pose_joints = numjoints;
	md5_pose_ready = true;
	return true;
}

void GL_EndMD5GPUPass (void)
{
	md5_pose_ready = false;
	md5_pose_joints = 0;
}

static qboolean GL_MD5BufferLayout (const mmd5mesh_t *mesh, int numjoints,
	unsigned *vertex_bytes, unsigned *index_bytes,
	unsigned *weight_bytes, unsigned *joint_bytes, unsigned *bytes)
{
	unsigned i;
	if (!mesh || numjoints < 1 || numjoints > MD5_MAX_JOINTS ||
		mesh->numverts < 1 || mesh->numverts > MD5_MAX_VERTICES ||
		mesh->numtris < 1 || mesh->numtris > MD5_MAX_TRIANGLES ||
		mesh->numweights < 1 || mesh->numweights > MD5_MAX_WEIGHTS ||
		!mesh->vertices || !mesh->indices || !mesh->weights)
		return false;
	for (i = 0; i < (unsigned)mesh->numverts; i++)
	{
		const mmd5vertex_t *v = &mesh->vertices[i];
		if (!v->count || (unsigned)v->start + v->count > (unsigned)mesh->numweights)
			return false;
	}
	for (i = 0; i < (unsigned)mesh->numtris * 3; i++)
		if (mesh->indices[i] >= (unsigned)mesh->numverts)
			return false;
	for (i = 0; i < (unsigned)mesh->numweights; i++)
		if (mesh->weights[i].joint >= numjoints)
			return false;
	*vertex_bytes = (unsigned)mesh->numverts * sizeof (mesh->vertices[0]);
	*index_bytes = (unsigned)mesh->numtris * 3 * sizeof (mesh->indices[0]);
	*weight_bytes = (unsigned)mesh->numweights * sizeof (md5_weight_upload[0]);
	/* std430 uint loads need a complete zero-padded final four-byte word. */
	*joint_bytes = ((unsigned)mesh->numweights + 3u) & ~3u;
	*bytes = *vertex_bytes + *index_bytes + *weight_bytes + *joint_bytes;
	return *bytes <= ALIAS_GPU_MAX_BYTES &&
		*vertex_bytes + *index_bytes <= INT_MAX;
}

static GLuint GL_MD5NextTextureName (void)
{
	unsigned scanned;
	for (scanned = 0; scanned < MD5_TEXTURE_NAME_COUNT; scanned++)
	{
		GLuint name = md5_next_texture++;
		if (md5_next_texture >= MD5_TEXTURE_FIRST + MD5_TEXTURE_NAME_COUNT)
			md5_next_texture = MD5_TEXTURE_FIRST;
		if (!qglIsTexture (name))
			return name;
	}
	return 0;
}

static void GL_BuildMD5Buffer (model_t *mod, mmd5mesh_t *mesh, int numjoints)
{
	GLuint buffers[3] = { 0, 0, 0 }, array = 0;
	GLint saved_array = 0, saved_vao = 0, actual = 0;
	unsigned vb, ib, wb, jb, bytes, i;
	qboolean state_saved = false;

	if (!mesh || mesh->gpu_buffer || mesh->gpu_failed || !GL_MD5GPUAvailable ())
		return;
	if (!GL_MD5BufferLayout (mesh, numjoints, &vb, &ib, &wb, &jb,
			&bytes) ||
		bytes > ALIAS_GPU_MAX_BYTES - alias_gpu_bytes ||
		!GL_ClearErrors ())
		goto unavailable;
	memset (md5_joint_upload, 0, jb);
	for (i = 0; i < (unsigned)mesh->numweights; i++)
	{
		const mmd5weight_t *w = &mesh->weights[i];
		memcpy (md5_weight_upload[i], w->pos, sizeof (w->pos));
		md5_weight_upload[i][3] = w->bias;
		md5_joint_upload[i] = w->joint;
	}
	qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_array);
	qglGetIntegerv (GL_VERTEX_ARRAY_BINDING, &saved_vao);
	if (qglGetError () != GL_NO_ERROR)
		goto unavailable;
	state_saved = true;
	qglGenBuffers (3, buffers);
	if (!buffers[0] || !buffers[1] || !buffers[2])
		goto fail;
	qglBindBuffer (GL_ARRAY_BUFFER, buffers[0]);
	qglBufferData (GL_ARRAY_BUFFER, (GLsizeiptr)(vb + ib), NULL, GL_STATIC_DRAW);
	qglGetBufferParameteriv (GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &actual);
	if (actual != (GLint)(vb + ib) || qglGetError () != GL_NO_ERROR)
		goto fail;
	qglBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr)vb, mesh->vertices);
	qglBufferSubData (GL_ARRAY_BUFFER, (GLintptr)vb,
		(GLsizeiptr)ib, mesh->indices);
	qglBindBuffer (GL_ARRAY_BUFFER, buffers[1]);
	qglBufferData (GL_ARRAY_BUFFER, (GLsizeiptr)wb, md5_weight_upload,
		GL_STATIC_DRAW);
	qglGetBufferParameteriv (GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &actual);
	if (actual != (GLint)wb || qglGetError () != GL_NO_ERROR)
		goto fail;
	qglBindBuffer (GL_ARRAY_BUFFER, buffers[2]);
	qglBufferData (GL_ARRAY_BUFFER, (GLsizeiptr)jb, md5_joint_upload,
		GL_STATIC_DRAW);
	qglGetBufferParameteriv (GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &actual);
	if (actual != (GLint)jb || qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.GenVertexArrays (1, &array);
	if (!array)
		goto fail;
	shader_procs.BindVertexArray (array);
	qglBindBuffer (GL_ARRAY_BUFFER, buffers[0]);
	qglBindBuffer (GL_ELEMENT_ARRAY_BUFFER, buffers[0]);
	shader_procs.VertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE,
		sizeof (mesh->vertices[0]), (const GLvoid *)offsetof (mmd5vertex_t, st));
	shader_procs.VertexAttribPointer (1, 3, GL_FLOAT, GL_FALSE,
		sizeof (mesh->vertices[0]), (const GLvoid *)offsetof (mmd5vertex_t, normal));
	shader_procs.VertexAttribPointer (2, 2, GL_UNSIGNED_SHORT, GL_FALSE,
		sizeof (mesh->vertices[0]), (const GLvoid *)offsetof (mmd5vertex_t, start));
	for (i = 0; i < MD5_SHADER_ATTRIBUTES; i++)
		shader_procs.EnableVertexAttribArray (i);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	shader_procs.BindVertexArray ((GLuint)saved_vao);
	qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
	mesh->gpu_buffer = buffers[0];
	mesh->gpu_weights = buffers[1];
	mesh->gpu_joints = buffers[2];
	mesh->gpu_vertex_array = array;
	mesh->gpu_bytes = bytes;
	alias_gpu_bytes += bytes;
	return;

fail:
	if (state_saved)
	{
		shader_procs.BindVertexArray ((GLuint)saved_vao);
		qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
	}
	if (array)
		shader_procs.DeleteVertexArrays (1, &array);
	if (buffers[0] || buffers[1] || buffers[2])
		qglDeleteBuffers (3, buffers);
unavailable:
	mesh->gpu_failed = true;
	ri.Con_Printf (PRINT_ALL, "%s: GPU MD5 storage unavailable; retaining CPU skinning.\n",
		mod->name);
}

static qboolean GL_EnsureMD5TBOViews (mmd5mesh_t *mesh)
{
	GLuint textures[2] = { 0, 0 };
	GLint saved_active = 0, saved_tbo = 0;
	qboolean state_saved = false;

	if (mesh->gpu_weight_texture && mesh->gpu_joint_texture)
		return true;
	if (mesh->gpu_tbo_failed || !mesh->gpu_weights || !mesh->gpu_joints ||
		!GL_MD5TBOAvailable () || !GL_ClearErrors ())
		return false;
	qglGetIntegerv (GL_ACTIVE_TEXTURE, &saved_active);
	qglGetIntegerv (GL_TEXTURE_BINDING_BUFFER, &saved_tbo);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	state_saved = true;
	textures[0] = GL_MD5NextTextureName ();
	if (!textures[0] || qglGetError () != GL_NO_ERROR)
		goto fail;
	qglBindTexture (GL_TEXTURE_BUFFER, textures[0]);
	md5_procs.TexBuffer (GL_TEXTURE_BUFFER, GL_RGBA32F, mesh->gpu_weights);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	textures[1] = GL_MD5NextTextureName ();
	if (!textures[1] || qglGetError () != GL_NO_ERROR)
		goto fail;
	qglBindTexture (GL_TEXTURE_BUFFER, textures[1]);
	md5_procs.TexBuffer (GL_TEXTURE_BUFFER, GL_R8UI, mesh->gpu_joints);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	qglBindTexture (GL_TEXTURE_BUFFER, (GLuint)saved_tbo);
	qglActiveTextureARB ((GLenum)saved_active);
	if (qglGetError () != GL_NO_ERROR)
		goto fail;
	mesh->gpu_weight_texture = textures[0];
	mesh->gpu_joint_texture = textures[1];
	return true;

fail:
	if (state_saved)
	{
		qglBindTexture (GL_TEXTURE_BUFFER, (GLuint)saved_tbo);
		qglActiveTextureARB ((GLenum)saved_active);
	}
	if (textures[0] || textures[1])
		qglDeleteTextures (2, textures);
	mesh->gpu_tbo_failed = true;
	return false;
}

static qboolean GL_DrawMD5GPUBackend (mmd5mesh_t *mesh,
	const vec3_t move, float alpha, GLuint glowtexnum, qboolean ssbo)
{
	glglowstate_t glow_state;
	GLfloat color[4];
	GLfloat saved_attributes[MD5_SHADER_ATTRIBUTES - 1][4];
	md5uniforms_t *uniforms = ssbo ? &md5_ssbo_uniform : &md5_uniform;
	GLuint program = ssbo ? md5_ssbo_program : md5_program;
	GLint saved_array = 0, saved_vao = 0, saved_program = 0;
	GLint saved_active = 0, saved_weight = 0, saved_joint = 0;
	GLint saved_ubo = 0, saved_ssbo = 0, indexed = 0;
	int slot = -1, ssbo_slots[2] = { -1, -1 }, i, glowkind = 0;
	unsigned index_offset;
	qboolean failed = false, drawn = false, backend_failed = false;
	qboolean outlines = GL_ShowTrisActive ();

	if (!(ssbo ? GL_MD5SSBOAvailable () : GL_MD5TBOAvailable ()) ||
		!GL_ClearErrors ())
		return false;
	for (i = 0; i < md5_ubo_bindings; i++)
	{
		md5_procs.GetIntegeri_v (GL_UNIFORM_BUFFER_BINDING, i, &indexed);
		if (indexed == 0)
		{
			slot = i;
			break;
		}
	}
	if (slot < 0 || qglGetError () != GL_NO_ERROR)
		return false;
	if (ssbo)
	{
		for (i = 0; i < md5_ssbo_bindings; i++)
		{
			md5_procs.GetIntegeri_v (GL_SHADER_STORAGE_BUFFER_BINDING,
				i, &indexed);
			if (indexed == 0)
			{
				if (ssbo_slots[0] < 0)
					ssbo_slots[0] = i;
				else
				{
					ssbo_slots[1] = i;
					break;
				}
			}
		}
		if (qglGetError () != GL_NO_ERROR)
		{
			GL_FreeMD5SSBOProgram ();
			return false;
		}
		if (ssbo_slots[1] < 0)
			return false; /* Foreign ranges stay bound. */
	}
	index_offset = (unsigned)mesh->numverts * sizeof (mesh->vertices[0]);
	qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_array);
	qglGetIntegerv (GL_VERTEX_ARRAY_BINDING, &saved_vao);
	qglGetIntegerv (GL_CURRENT_PROGRAM, &saved_program);
	qglGetIntegerv (GL_ACTIVE_TEXTURE, &saved_active);
	qglGetIntegerv (GL_UNIFORM_BUFFER_BINDING, &saved_ubo);
	if (ssbo)
		qglGetIntegerv (GL_SHADER_STORAGE_BUFFER_BINDING, &saved_ssbo);
	for (i = 1; i < MD5_SHADER_ATTRIBUTES; i++)
		shader_procs.GetVertexAttribfv (i, GL_CURRENT_VERTEX_ATTRIB,
			saved_attributes[i - 1]);
	if (qglGetError () != GL_NO_ERROR)
		return false;
	if (!ssbo)
	{
		qglActiveTextureARB (MD5_WEIGHT_TMU);
		qglGetIntegerv (GL_TEXTURE_BINDING_BUFFER, &saved_weight);
		qglActiveTextureARB (MD5_JOINT_TMU);
		qglGetIntegerv (GL_TEXTURE_BINDING_BUFFER, &saved_joint);
		qglActiveTextureARB ((GLenum)saved_active);
		if (qglGetError () != GL_NO_ERROR)
			return false;
	}
	memset (&glow_state, 0, sizeof (glow_state));
	if (glowtexnum && GL_BeginGlowMap (glowtexnum, &glow_state))
		glowkind = 2;
	memcpy (color, shadelight, sizeof (vec3_t));
	color[3] = alpha;
	qglPushAttrib (GL_CURRENT_BIT);
	shader_procs.BindVertexArray (mesh->gpu_vertex_array);
	qglBindBuffer (GL_ARRAY_BUFFER, mesh->gpu_buffer);
	shader_procs.UseProgram (program);
	shader_procs.Uniform3fv (uniforms->move, 1, move);
	shader_procs.Uniform4fv (uniforms->color, 1, color);
	shader_procs.Uniform3fv (uniforms->shadevector, 1, shadevector);
	shader_procs.Uniform1i (uniforms->dotshading,
		!gl_dotshading || FLOAT_NE_ZERO (gl_dotshading->value));
	shader_procs.Uniform1f (uniforms->intensity, GL_ImageIntensityScale ());
	shader_procs.Uniform1i (uniforms->glowmap, glowkind ? 2 : 0);
	shader_procs.Uniform1i (uniforms->glowkind, glowkind);
	shader_procs.Uniform1f (uniforms->glowscale, GL_GlowmapScale ());
	shader_procs.Uniform1i (uniforms->bloommode, GL_BloomActive () ? 1 : 0);
	if (ssbo)
	{
		md5_ssbo_procs.ShaderStorageBlockBinding (program,
			md5_ssbo_weight_block, ssbo_slots[0]);
		md5_ssbo_procs.ShaderStorageBlockBinding (program,
			md5_ssbo_joint_block, ssbo_slots[1]);
		md5_procs.BindBufferBase (GL_SHADER_STORAGE_BUFFER,
			ssbo_slots[0], mesh->gpu_weights);
		md5_procs.BindBufferBase (GL_SHADER_STORAGE_BUFFER,
			ssbo_slots[1], mesh->gpu_joints);
	}
	else
	{
		qglActiveTextureARB (MD5_WEIGHT_TMU);
		qglBindTexture (GL_TEXTURE_BUFFER, mesh->gpu_weight_texture);
		qglActiveTextureARB (MD5_JOINT_TMU);
		qglBindTexture (GL_TEXTURE_BUFFER, mesh->gpu_joint_texture);
		qglActiveTextureARB ((GLenum)saved_active);
	}
	md5_procs.UniformBlockBinding (program,
		ssbo ? md5_ssbo_skeleton_block : md5_skeleton_block, slot);
	md5_procs.BindBufferBase (GL_UNIFORM_BUFFER, slot, md5_pose_buffer);
	backend_failed = qglGetError () != GL_NO_ERROR;
	if (!backend_failed && GL_BloomTargets (true))
	{
		if (!outlines && GL_WeaponDepthPrefill (alpha))
		{
			failed = !GL_DrawWeaponDepthPrefillTyped (mesh->numtris * 3,
				GL_UNSIGNED_SHORT, (const GLvoid *)(size_t)index_offset);
			backend_failed = failed;
			md5_gpu_draws++;
		}
		if (!failed)
		{
			if (outlines)
				GL_SetShowTrisShader (true);
			qglDrawElements (GL_TRIANGLES, mesh->numtris * 3,
				GL_UNSIGNED_SHORT, (const GLvoid *)(size_t)index_offset);
			if (outlines)
				GL_SetShowTrisShader (false);
			drawn = true;
			failed = qglGetError () != GL_NO_ERROR;
			backend_failed = failed;
			if (!outlines)
				md5_gpu_draws++;
		}
	}
	else
		failed = true;
	GL_BloomTargets (false);
	if (failed)
		GL_BloomAbort ();
	md5_procs.BindBufferBase (GL_UNIFORM_BUFFER, slot, 0);
	qglBindBuffer (GL_UNIFORM_BUFFER, (GLuint)saved_ubo);
	if (ssbo)
	{
		md5_procs.BindBufferBase (GL_SHADER_STORAGE_BUFFER, ssbo_slots[0], 0);
		md5_procs.BindBufferBase (GL_SHADER_STORAGE_BUFFER, ssbo_slots[1], 0);
		qglBindBuffer (GL_SHADER_STORAGE_BUFFER, (GLuint)saved_ssbo);
	}
	else
	{
		qglActiveTextureARB (MD5_WEIGHT_TMU);
		qglBindTexture (GL_TEXTURE_BUFFER, (GLuint)saved_weight);
		qglActiveTextureARB (MD5_JOINT_TMU);
		qglBindTexture (GL_TEXTURE_BUFFER, (GLuint)saved_joint);
		qglActiveTextureARB ((GLenum)saved_active);
	}
	shader_procs.UseProgram ((GLuint)saved_program);
	GL_EndGlowMap (&glow_state);
	shader_procs.BindVertexArray ((GLuint)saved_vao);
	qglBindBuffer (GL_ARRAY_BUFFER, (GLuint)saved_array);
	qglPopAttrib ();
	for (i = 1; i < MD5_SHADER_ATTRIBUTES; i++)
		shader_procs.VertexAttrib4fv (i, saved_attributes[i - 1]);
	if (failed)
	{
		if (ssbo)
		{
			if (backend_failed)
			{
				GL_FreeMD5SSBOProgram ();
				md5_ssbo_shader_attempted = true;
				ri.Con_Printf (PRINT_ALL, "%s: MD5 SSBO draw failed; using TBO or CPU.\n",
					currentmodel->name);
			}
		}
		else
		{
			if (GL_MD5SSBOAvailable ())
			{
				GL_FreeMD5TBOViews (mesh);
				mesh->gpu_tbo_failed = true;
			}
			else
			{
				GL_FreeMD5Buffer (mesh);
				mesh->gpu_failed = true;
			}
			ri.Con_Printf (PRINT_ALL, "%s: MD5 TBO draw failed; using SSBO or CPU.\n",
				currentmodel->name);
		}
	}
	return drawn; /* A submitted translucent color draw must never repeat. */
}

qboolean GL_DrawMD5GPU (mmd5mesh_t *mesh, const vec3_t move,
	float alpha, GLuint glowtexnum)
{
	int i;
	if (!md5_pose_ready || !mesh || mesh->gpu_failed || !currentmodel ||
		!currentmodel->md5 || !currententity ||
		!(alpha >= 0 && alpha <= FLT_MAX) ||
		mesh->numverts < 1 || mesh->numverts > MD5_MAX_VERTICES ||
		mesh->numtris < 1 || mesh->numtris > MD5_MAX_TRIANGLES)
		return false;
	for (i = 0; i < 3; i++)
		if (!(move[i] >= -FLT_MAX && move[i] <= FLT_MAX))
			return false;
	if (!GL_MD5GPUAvailable () || !GL_ClearErrors ())
		return false;
	GL_BuildMD5Buffer (currentmodel, mesh, md5_pose_joints);
	if (!mesh->gpu_buffer || !mesh->gpu_vertex_array ||
		!mesh->gpu_weights || !mesh->gpu_joints ||
		!GL_ClearErrors ())
		return false;
	if (GL_MD5SSBOAvailable () &&
		GL_DrawMD5GPUBackend (mesh, move, alpha, glowtexnum, true))
		return true;
	if (GL_MD5TBOAvailable () && GL_EnsureMD5TBOViews (mesh))
		return GL_DrawMD5GPUBackend (mesh, move, alpha, glowtexnum, false);
	return false;
}

qboolean GL_BeginBloomShader (qboolean textured, qboolean alpha_texture, qboolean emitter)
{
	GLint mode = GL_MODULATE;
	if (!GL_BloomActive ())
		return false;
	if (textured)
	{
		if (!qglGetTexEnviv)
		{
			GL_BloomAbort ();
			return false;
		}
		qglGetTexEnviv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &mode);
		if (qglGetError () != GL_NO_ERROR)
		{
			GL_BloomAbort ();
			return false;
		}
	}
	if (!GL_BeginMaterialShader (textured, alpha_texture, (GLenum)mode, 0, 0, 1, 0))
		return false;
	shader_procs.Uniform1i (surface_uniform_bloommode, emitter ? 2 : 1);
	if (qglGetError () != GL_NO_ERROR)
	{
		GL_EndSurfaceShader ();
		GL_BloomAbort ();
		return false;
	}
	return true;
}

/* Outline overlays retain the current vertex program and its pose/warp data. */
qboolean GL_SetShowTrisShader (qboolean enabled)
{
	GLint program = 0, location;

	if (!qglGetIntegerv || !shader_procs.GetUniformLocation ||
		!shader_procs.Uniform1i)
		return false;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &program);
	if (!program || ((GLuint)program != alias_program && (GLuint)program != md3_program && (GLuint)program != md5_program && (GLuint)program != md5_ssbo_program && (GLuint)program != surface_program && (GLuint)program != point_program && (GLuint)program != sky_program && (GLuint)program != classic_sky_program))
		return false;
	location = shader_procs.GetUniformLocation ((GLuint)program, "u_showtris");
	if (location < 0)
		return false;
	shader_procs.Uniform1i (location, enabled != 0);
	return true;
}
