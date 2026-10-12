/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2018 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.
*/

/* Q2PRO src/refresh/{main,shader,texture}.c framebuffer lifetime and screen
 * warp, adapted to the retained R1Q2 view. Resolve the existing rendered view
 * before processing it; world/input/packet scheduling is not redirected.
 */
#include "gl_local.h"
#include <float.h>
#include <limits.h>

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE 0x8211
#define GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING 0x8210
#define GL_UNSIGNED_NORMALIZED 0x8C17
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#ifndef GL_SAMPLER_BINDING
#define GL_SAMPLER_BINDING 0x8919
#endif
#ifndef GL_RGB565
#define GL_RGB565 0x8D62
#endif

#define POSTFX_MAX_BYTES (64u * 1024u * 1024u)
/* R1GL uses fixed lightmap/scrap/image names. Do not generate an image name
 * which a subsequent map/skin registration could claim from that namespace.
 */
#define POSTFX_SCENE_TEXTURE ((GLuint)(TEXNUM_IMAGES + MAX_GLTEXTURES))

typedef struct
{
	void (APIENTRY *GenFramebuffers) (GLsizei, GLuint *);
	void (APIENTRY *DeleteFramebuffers) (GLsizei, const GLuint *);
	void (APIENTRY *BindFramebuffer) (GLenum, GLuint);
	void (APIENTRY *FramebufferTexture2D) (GLenum, GLenum, GLenum, GLuint, GLint);
	GLenum (APIENTRY *CheckFramebufferStatus) (GLenum);
	void (APIENTRY *BlitFramebuffer) (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
	void (APIENTRY *GetFramebufferAttachmentParameteriv) (GLenum, GLenum, GLenum, GLint *);
	void (APIENTRY *BindBuffer) (GLenum, GLuint);
	void (APIENTRY *ActiveTexture) (GLenum);
	GLuint (APIENTRY *CreateShader) (GLenum);
	void (APIENTRY *ShaderSource) (GLuint, GLsizei, const char *const *, const GLint *);
	void (APIENTRY *CompileShader) (GLuint);
	void (APIENTRY *GetShaderiv) (GLuint, GLenum, GLint *);
	void (APIENTRY *GetShaderInfoLog) (GLuint, GLsizei, GLsizei *, char *);
	void (APIENTRY *DeleteShader) (GLuint);
	GLuint (APIENTRY *CreateProgram) (void);
	void (APIENTRY *AttachShader) (GLuint, GLuint);
	void (APIENTRY *LinkProgram) (GLuint);
	void (APIENTRY *GetProgramiv) (GLuint, GLenum, GLint *);
	void (APIENTRY *GetProgramInfoLog) (GLuint, GLsizei, GLsizei *, char *);
	void (APIENTRY *DeleteProgram) (GLuint);
	void (APIENTRY *UseProgram) (GLuint);
	GLint (APIENTRY *GetUniformLocation) (GLuint, const char *);
	void (APIENTRY *Uniform1i) (GLint, GLint);
	void (APIENTRY *Uniform1f) (GLint, GLfloat);
	void (APIENTRY *BindSampler) (GLuint, GLuint);
} postfxprocs_t;

static void GL_InitBloom (void);
static void GL_ShutdownBloom (void);
static void GL_UpdateBloom (void);
static cvar_t *gl_bloom, *gl_bloom_sigma, *gl_showbloom;

static postfxprocs_t post_procs;
static cvar_t *gl_waterwarp;
static GLuint post_program, post_framebuffer, post_texture;
static GLint post_uniform_scene, post_uniform_time;
static int post_width, post_height, post_failed_width, post_failed_height, post_clip_planes;
static GLenum post_format;
static unsigned post_bytes, post_warps, post_options;
static qboolean post_capable, post_failed, post_attempted, post_rgb565;

static const char post_vertex_shader[] =
	"#version 110\n"
	"varying vec2 v_tc;\n"
	"void main() { gl_Position = gl_Vertex; v_tc = gl_MultiTexCoord0.xy; }\n";

static const char post_fragment_shader[] =
	"#version 110\n"
	"uniform sampler2D u_scene;\n"
	"uniform float u_time;\n"
	"varying vec2 v_tc;\n"
	"void main() {\n"
	"  vec2 tc = v_tc + vec2(0.0025) * sin(v_tc.ts * vec2(31.4159265359) + u_time);\n"
	"  gl_FragColor = texture2D(u_scene, tc);\n"
	"}\n";

static unsigned GL_PostFXRequested (void)
{
	unsigned options = 0;
	float shaders = gl_shaders ? gl_shaders->value : 0;
	if (shaders >= -FLT_MAX && shaders <= FLT_MAX && FLOAT_NE_ZERO (shaders))
		options |= 1;
	if (gl_waterwarp && gl_waterwarp->intvalue &&
		gl_waterwarp->value >= -FLT_MAX && gl_waterwarp->value <= FLT_MAX)
		options |= 2;
	return options;
}

static qboolean GL_PostFXExtension (const char *name)
{
	const char *extensions = gl_config.extensions_string, *p;
	size_t length = strlen (name);
	if (!extensions)
		return false;
	for (p = extensions; (p = strstr (p, name)) != NULL; p += length)
		if ((p == extensions || p[-1] == ' ') && (p[length] == ' ' || !p[length]))
			return true;
	return false;
}

static size_t GL_PostFXProcedure (const char *name)
{
	size_t address = (size_t)qwglGetProcAddress (name);
	if (!address || address == 1 || address == 2 || address == 3 || address == (size_t)-1)
		return 0;
	return address;
}

static void GL_FreePostFXScene (void)
{
	if (post_framebuffer)
		post_procs.DeleteFramebuffers (1, &post_framebuffer);
	if (post_texture)
		qglDeleteTextures (1, &post_texture);
	post_framebuffer = post_texture = 0;
	post_width = post_height = 0;
	post_bytes = 0;
	post_format = 0;
}

static void GL_FreePostFXProgram (void)
{
	GLint current = 0;
	if (!post_program)
		return;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &current);
	if ((GLuint)current == post_program)
		post_procs.UseProgram (0);
	post_procs.DeleteProgram (post_program);
	post_program = 0;
}

void GL_PostFXRegister (void)
{
	/* Match the pinned Q2PRO control/default; explicit config files can enable it. */
	gl_waterwarp = ri.Cvar_Get ("gl_waterwarp", "0", 0);
	gl_bloom = ri.Cvar_Get ("gl_bloom", "0", CVAR_ARCHIVE);
	gl_bloom_sigma = ri.Cvar_Get ("gl_bloom_sigma", "4", CVAR_ARCHIVE);
	/* R1Q2 has no private cheat-cvar flag; do not extend the shared cvar ABI. */
	gl_showbloom = ri.Cvar_Get ("gl_showbloom", "0", 0);
}

void GL_ShutdownPostFX (void)
{
	GL_ShutdownBloom ();
	GL_FreePostFXScene ();
	GL_FreePostFXProgram ();
	memset (&post_procs, 0, sizeof (post_procs));
	post_capable = post_failed = post_attempted = post_rgb565 = false;
	post_failed_width = post_failed_height = post_clip_planes = 0;
	post_warps = post_options = 0;
}

void GL_InitPostFX (void)
{
	int major = 0, minor = 0;
	qboolean samplers;
	GL_ShutdownPostFX ();
	post_options = GL_PostFXRequested ();
	if (!qwglGetProcAddress || !gl_config.version_string ||
		sscanf (gl_config.version_string, "%d.%d", &major, &minor) != 2 || major < 3 ||
		!qglGetIntegerv || !qglGetError || !qglIsEnabled || !qglIsTexture ||
		!qglBindTexture || !qglDeleteTextures || !qglTexImage2D || !qglTexParameteri ||
		!qglGetTexLevelParameteriv || !qglPushAttrib || !qglPopAttrib ||
		!qglViewport || !qglReadBuffer || !qglDrawBuffer || !qglDisable ||
		!qglDepthMask || !qglPolygonMode || !qglBegin || !qglEnd ||
		!qglTexCoord2f || !qglVertex2f)
		return;
#define POST_PROC(name, result, parameters) \
	post_procs.name = (result (APIENTRY *) parameters)GL_PostFXProcedure ("gl" #name); \
	if (!post_procs.name) goto unavailable
	POST_PROC (GenFramebuffers, void, (GLsizei, GLuint *));
	POST_PROC (DeleteFramebuffers, void, (GLsizei, const GLuint *));
	POST_PROC (BindFramebuffer, void, (GLenum, GLuint));
	POST_PROC (FramebufferTexture2D, void, (GLenum, GLenum, GLenum, GLuint, GLint));
	POST_PROC (CheckFramebufferStatus, GLenum, (GLenum));
	POST_PROC (BlitFramebuffer, void, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum));
	POST_PROC (GetFramebufferAttachmentParameteriv, void, (GLenum, GLenum, GLenum, GLint *));
	POST_PROC (BindBuffer, void, (GLenum, GLuint));
	POST_PROC (ActiveTexture, void, (GLenum));
	POST_PROC (CreateShader, GLuint, (GLenum));
	POST_PROC (ShaderSource, void, (GLuint, GLsizei, const char *const *, const GLint *));
	POST_PROC (CompileShader, void, (GLuint));
	POST_PROC (GetShaderiv, void, (GLuint, GLenum, GLint *));
	POST_PROC (GetShaderInfoLog, void, (GLuint, GLsizei, GLsizei *, char *));
	POST_PROC (DeleteShader, void, (GLuint));
	POST_PROC (CreateProgram, GLuint, (void));
	POST_PROC (AttachShader, void, (GLuint, GLuint));
	POST_PROC (LinkProgram, void, (GLuint));
	POST_PROC (GetProgramiv, void, (GLuint, GLenum, GLint *));
	POST_PROC (GetProgramInfoLog, void, (GLuint, GLsizei, GLsizei *, char *));
	POST_PROC (DeleteProgram, void, (GLuint));
	POST_PROC (UseProgram, void, (GLuint));
	POST_PROC (GetUniformLocation, GLint, (GLuint, const char *));
	POST_PROC (Uniform1i, void, (GLint, GLint));
	POST_PROC (Uniform1f, void, (GLint, GLfloat));
	samplers = major > 3 || (major == 3 && minor >= 3) ||
		GL_PostFXExtension ("GL_ARB_sampler_objects");
	if (samplers)
	{
		POST_PROC (BindSampler, void, (GLuint, GLuint));
	}
#undef POST_PROC
	post_rgb565 = major > 4 || (major == 4 && minor >= 1) ||
		GL_PostFXExtension ("GL_ARB_ES2_compatibility");
	qglGetIntegerv (GL_MAX_CLIP_PLANES, &post_clip_planes);
	if (post_clip_planes < 0 || post_clip_planes > 32)
		goto unavailable;
	post_capable = true;
	GL_InitBloom ();
	return;
unavailable:
	memset (&post_procs, 0, sizeof (post_procs));
	ri.Con_Printf (PRINT_ALL, "Post-processing procedures unavailable; retaining the view.\n");
}

void GL_UpdatePostFX (void)
{
	unsigned options = GL_PostFXRequested ();
	GL_UpdateBloom ();
	if (options == post_options && (!gl_waterwarp || !gl_waterwarp->modified))
		return;
	if (gl_waterwarp)
		gl_waterwarp->modified = false;
	post_options = options;
	post_failed = post_attempted = false;
	if (options != 3)
	{
		GL_FreePostFXScene ();
		GL_FreePostFXProgram ();
	}
}

static GLuint GL_CompilePostFXShader (GLenum type, const char *source)
{
	GLuint shader = post_procs.CreateShader (type);
	GLint compiled = 0;
	char log[1024];
	if (!shader)
		return 0;
	post_procs.ShaderSource (shader, 1, &source, NULL);
	post_procs.CompileShader (shader);
	post_procs.GetShaderiv (shader, GL_COMPILE_STATUS, &compiled);
	if (compiled)
		return shader;
	memset (log, 0, sizeof (log));
	post_procs.GetShaderInfoLog (shader, sizeof (log) - 1, NULL, log);
	ri.Con_Printf (PRINT_ALL, "Post-processing shader compilation failed: %s\n", log);
	post_procs.DeleteShader (shader);
	return 0;
}

static qboolean GL_CreatePostFXProgram (void)
{
	GLuint vertex = 0, fragment = 0, program = 0;
	GLint linked = 0;
	char log[1024];
	if (post_program || post_attempted)
		return post_program != 0;
	post_attempted = true;
	program = post_procs.CreateProgram ();
	if (!program)
		goto fail;
	vertex = GL_CompilePostFXShader (GL_VERTEX_SHADER, post_vertex_shader);
	if (!vertex)
		goto fail;
	fragment = GL_CompilePostFXShader (GL_FRAGMENT_SHADER, post_fragment_shader);
	if (!fragment)
		goto fail;
	post_procs.AttachShader (program, vertex);
	post_procs.AttachShader (program, fragment);
	post_procs.LinkProgram (program);
	post_procs.GetProgramiv (program, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		memset (log, 0, sizeof (log));
		post_procs.GetProgramInfoLog (program, sizeof (log) - 1, NULL, log);
		ri.Con_Printf (PRINT_ALL, "Post-processing shader linking failed: %s\n", log);
		goto fail;
	}
	post_uniform_scene = post_procs.GetUniformLocation (program, "u_scene");
	post_uniform_time = post_procs.GetUniformLocation (program, "u_time");
	if (post_uniform_scene < 0 || post_uniform_time < 0 || qglGetError () != GL_NO_ERROR)
		goto fail;
	post_procs.DeleteShader (vertex);
	post_procs.DeleteShader (fragment);
	post_program = program;
	return true;
fail:
	if (vertex)
		post_procs.DeleteShader (vertex);
	if (fragment)
		post_procs.DeleteShader (fragment);
	if (program)
		post_procs.DeleteProgram (program);
	return false;
}

static qboolean GL_PostFXFormat (GLenum draw_buffer, GLenum *format, unsigned *bytes)
{
	GLint red = 0, green = 0, blue = 0, alpha = 0, encoding = 0, component = 0;
	GLenum attachment;
	/* This controller captures the ordinary drawable; other owners keep their FBO. */
	switch (draw_buffer)
	{
	case GL_FRONT:
	case GL_FRONT_LEFT:
		attachment = GL_FRONT_LEFT;
		break;
	case GL_BACK:
	case GL_BACK_LEFT:
		attachment = GL_BACK_LEFT;
		break;
	case GL_FRONT_RIGHT:
		attachment = GL_FRONT_RIGHT;
		break;
	case GL_BACK_RIGHT:
		attachment = GL_BACK_RIGHT;
		break;
	default:
		return false;
	}
	post_procs.GetFramebufferAttachmentParameteriv (GL_DRAW_FRAMEBUFFER, attachment,
		GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &encoding);
	post_procs.GetFramebufferAttachmentParameteriv (GL_DRAW_FRAMEBUFFER, attachment,
		GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &component);
	if (encoding != GL_LINEAR || component != GL_UNSIGNED_NORMALIZED)
		return false;
	qglGetIntegerv (GL_RED_BITS, &red);
	qglGetIntegerv (GL_GREEN_BITS, &green);
	qglGetIntegerv (GL_BLUE_BITS, &blue);
	qglGetIntegerv (GL_ALPHA_BITS, &alpha);
	if (red == 8 && green == 8 && blue == 8 && (alpha == 0 || alpha == 8))
	{
		*format = alpha ? GL_RGBA8 : GL_RGB8;
		*bytes = alpha ? 4 : 3;
		return true;
	}
	if (red == 5 && green == 5 && blue == 5 && alpha == 1)
	{
		*format = GL_RGB5_A1;
		*bytes = 2;
		return true;
	}
	if (red == 5 && green == 6 && blue == 5 && !alpha && post_rgb565)
	{
		*format = GL_RGB565;
		*bytes = 2;
		return true;
	}
	if (red == 10 && green == 10 && blue == 10 && alpha == 2)
	{
		*format = GL_RGB10_A2;
		*bytes = 4;
		return true;
	}
	return false;
}

/* Caller has isolated server state and will restore read/draw FBO/PBO bindings. */
static qboolean GL_EnsurePostFXScene (int width, int height, GLenum format, unsigned bytes)
{
	GLint actual_width = 0, actual_height = 0, actual_format = 0;
	GLint saved_unpack = 0, binding = 0;
	GLuint framebuffer = 0;
	if (post_texture && post_width == width && post_height == height && post_format == format)
		return true;
	GL_FreePostFXScene ();
	if (qglIsTexture (POSTFX_SCENE_TEXTURE))
		return false;
	post_texture = POSTFX_SCENE_TEXTURE;
	post_procs.ActiveTexture (GL_TEXTURE0_ARB);
	qglBindTexture (GL_TEXTURE_2D, post_texture);
	qglGetIntegerv (GL_TEXTURE_BINDING_2D, &binding);
	if (binding != (GLint)post_texture || qglGetError () != GL_NO_ERROR)
		return false;
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	qglGetIntegerv (GL_PIXEL_UNPACK_BUFFER_BINDING, &saved_unpack);
	post_procs.BindBuffer (GL_PIXEL_UNPACK_BUFFER, 0);
	qglGetIntegerv (GL_PIXEL_UNPACK_BUFFER_BINDING, &binding);
	if (binding || qglGetError () != GL_NO_ERROR)
	{
		post_procs.BindBuffer (GL_PIXEL_UNPACK_BUFFER, (GLuint)saved_unpack);
		return false;
	}
	qglTexImage2D (GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	post_procs.BindBuffer (GL_PIXEL_UNPACK_BUFFER, (GLuint)saved_unpack);
	qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &actual_width);
	qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &actual_height);
	qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &actual_format);
	if (actual_width != width || actual_height != height || actual_format != (GLint)format ||
		qglGetError () != GL_NO_ERROR)
		return false;
	post_procs.GenFramebuffers (1, &framebuffer);
	post_framebuffer = framebuffer;
	if (!framebuffer)
		return false;
	post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, framebuffer);
	qglGetIntegerv (GL_DRAW_FRAMEBUFFER_BINDING, &binding);
	if (binding != (GLint)framebuffer || qglGetError () != GL_NO_ERROR)
		return false;
	post_procs.FramebufferTexture2D (GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, post_texture, 0);
	qglDrawBuffer (GL_COLOR_ATTACHMENT0);
	if (post_procs.CheckFramebufferStatus (GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
		qglGetError () != GL_NO_ERROR)
		return false;
	post_width = width;
	post_height = height;
	post_format = format;
	post_bytes = bytes;
	return true;
}

static void GL_PostFXQuad (void)
{
	qglBegin (GL_TRIANGLE_STRIP);
	qglTexCoord2f (0, 0);
	qglVertex2f (-1, -1);
	qglTexCoord2f (1, 0);
	qglVertex2f (1, -1);
	qglTexCoord2f (0, 1);
	qglVertex2f (-1, 1);
	qglTexCoord2f (1, 1);
	qglVertex2f (1, 1);
	qglEnd ();
}

void GL_PostProcessView (const refdef_t *fd)
{
	GLint viewport[4] = { 0 }, maximum = 0, program = 0, read_fbo = 0, draw_fbo = 0;
	GLint read_buffer = 0, draw_buffer = 0, sampler = 0, source_read_buffer = 0, binding = 0;
	GLenum format = 0;
	unsigned bytes = 0, pixel_bytes = 0;
	qboolean failed = false, captured = false, submitted = false, source_read_saved = false;
	int i;
	GL_UpdatePostFX ();
	if (!fd || !(fd->rdflags & RDF_UNDERWATER) || post_options != 3 || !post_capable ||
		!(fd->time >= -FLT_MAX && fd->time <= FLT_MAX))
		return;
	if (post_failed)
	{
		if (post_failed_width == fd->width && post_failed_height == fd->height)
			return;
		post_failed = post_attempted = false;
	}
	qglGetIntegerv (GL_VIEWPORT, viewport);
	if (viewport[0] < 0 || viewport[1] < 0 || viewport[2] <= 0 || viewport[3] <= 0 ||
		viewport[0] > INT_MAX - viewport[2] || viewport[1] > INT_MAX - viewport[3] ||
		(unsigned)viewport[2] > vid.width || (unsigned)viewport[3] > vid.height ||
		(unsigned)viewport[0] > vid.width - (unsigned)viewport[2] ||
		(unsigned)viewport[1] > vid.height - (unsigned)viewport[3])
		return;
	qglGetIntegerv (GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
	if (draw_fbo || qglIsEnabled (GL_FRAMEBUFFER_SRGB))
		return;
	qglGetIntegerv (GL_DRAW_BUFFER, &draw_buffer);
	if (!GL_PostFXFormat ((GLenum)draw_buffer, &format, &pixel_bytes))
		return;
	qglGetIntegerv (GL_MAX_TEXTURE_SIZE, &maximum);
	if (viewport[2] > maximum || viewport[3] > maximum ||
		(unsigned)viewport[2] > POSTFX_MAX_BYTES / pixel_bytes / (unsigned)viewport[3])
		return;
	bytes = (unsigned)viewport[2] * (unsigned)viewport[3] * pixel_bytes;
	if (!GL_ClearErrors ())
		return;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &program);
	qglGetIntegerv (GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
	qglGetIntegerv (GL_READ_BUFFER, &read_buffer);
	if (qglGetError () != GL_NO_ERROR)
		return;
	qglPushAttrib (GL_ALL_ATTRIB_BITS);
	if (qglGetError () != GL_NO_ERROR)
		return;
	post_procs.ActiveTexture (GL_TEXTURE0_ARB);
	if (post_procs.BindSampler)
	{
		qglGetIntegerv (GL_SAMPLER_BINDING, &sampler);
		post_procs.BindSampler (0, 0);
	}
	if (!GL_CreatePostFXProgram () ||
		!GL_EnsurePostFXScene (viewport[2], viewport[3], format, bytes))
	{
		failed = true;
		goto restore;
	}
	qglDisable (GL_SCISSOR_TEST);
	post_procs.BindFramebuffer (GL_READ_FRAMEBUFFER, (GLuint)draw_fbo);
	qglGetIntegerv (GL_READ_FRAMEBUFFER_BINDING, &binding);
	qglGetIntegerv (GL_READ_BUFFER, &source_read_buffer);
	if (qglGetError () != GL_NO_ERROR || binding != draw_fbo)
	{
		failed = true;
		goto restore;
	}
	source_read_saved = true;
	qglReadBuffer ((GLenum)draw_buffer);
	post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, post_framebuffer);
	qglDrawBuffer (GL_COLOR_ATTACHMENT0);
	if (qglGetError () != GL_NO_ERROR)
	{
		failed = true;
		goto restore;
	}
	/* Resolve MSAA at identical dimensions/formats before sampling the view. */
	post_procs.BlitFramebuffer (viewport[0], viewport[1], viewport[0] + viewport[2], viewport[1] + viewport[3],
		0, 0, viewport[2], viewport[3], GL_COLOR_BUFFER_BIT, GL_NEAREST);
	if (qglGetError () != GL_NO_ERROR)
	{
		failed = true;
		goto restore;
	}
	captured = true;
	post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, (GLuint)draw_fbo);
	qglDrawBuffer ((GLenum)draw_buffer);
	qglViewport (viewport[0], viewport[1], viewport[2], viewport[3]);
	qglDisable (GL_DEPTH_TEST);
	qglDepthMask (GL_FALSE);
	qglDisable (GL_STENCIL_TEST);
	qglDisable (GL_ALPHA_TEST);
	qglDisable (GL_CULL_FACE);
	qglDisable (GL_BLEND);
	qglDisable (GL_LIGHTING);
	qglDisable (GL_FOG);
	qglDisable (GL_COLOR_LOGIC_OP);
	qglDisable (GL_SAMPLE_ALPHA_TO_COVERAGE);
	qglDisable (GL_SAMPLE_ALPHA_TO_ONE);
	qglDisable (GL_SAMPLE_COVERAGE);
	qglDisable (GL_POLYGON_SMOOTH);
	qglDisable (GL_POLYGON_OFFSET_FILL);
	qglPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	for (i = 0; i < post_clip_planes; i++)
		qglDisable (GL_CLIP_PLANE0 + i);
	qglBindTexture (GL_TEXTURE_2D, post_texture);
	post_procs.UseProgram (post_program);
	post_procs.Uniform1i (post_uniform_scene, 0);
	post_procs.Uniform1f (post_uniform_time, fd->time);
	if (qglGetError () != GL_NO_ERROR)
	{
		failed = true;
		goto restore;
	}
	submitted = true;
	GL_PostFXQuad ();
	failed = qglGetError () != GL_NO_ERROR;
	if (!failed)
		post_warps++;
restore:
	if (failed && captured && submitted)
	{
		/* Restore the captured view once; never repeat world/model/alpha draws. */
		post_procs.BindFramebuffer (GL_READ_FRAMEBUFFER, post_framebuffer);
		qglReadBuffer (GL_COLOR_ATTACHMENT0);
		post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, (GLuint)draw_fbo);
		qglDrawBuffer ((GLenum)draw_buffer);
		if (qglGetError () != GL_NO_ERROR)
			ri.Con_Printf (PRINT_ALL, "Post-processing view restoration setup failed.\n");
		else
		{
			post_procs.BlitFramebuffer (0, 0, viewport[2], viewport[3],
				viewport[0], viewport[1], viewport[0] + viewport[2], viewport[1] + viewport[3],
				GL_COLOR_BUFFER_BIT, GL_NEAREST);
			if (qglGetError () != GL_NO_ERROR)
				ri.Con_Printf (PRINT_ALL, "Post-processing view restoration failed.\n");
		}
	}
	post_procs.UseProgram ((GLuint)program);
	if (post_procs.BindSampler)
		post_procs.BindSampler (0, (GLuint)sampler);
	if (source_read_saved)
	{
		post_procs.BindFramebuffer (GL_READ_FRAMEBUFFER, (GLuint)draw_fbo);
		qglGetIntegerv (GL_READ_FRAMEBUFFER_BINDING, &binding);
		if (qglGetError () == GL_NO_ERROR && binding == draw_fbo)
			qglReadBuffer ((GLenum)source_read_buffer);
		else
			ri.Con_Printf (PRINT_ALL, "Post-processing read-buffer restoration failed.\n");
	}
	post_procs.BindFramebuffer (GL_READ_FRAMEBUFFER, (GLuint)read_fbo);
	qglReadBuffer ((GLenum)read_buffer);
	post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, (GLuint)draw_fbo);
	qglPopAttrib ();
	if (failed)
	{
		GL_FreePostFXScene ();
		GL_FreePostFXProgram ();
		post_failed = true;
		post_failed_width = fd->width;
		post_failed_height = fd->height;
		ri.Con_Printf (PRINT_ALL, "Underwater post-processing disabled after failure; resize or toggle to retry.\n");
	}
}

void GL_PrintPostFXInfo (void)
{
	ri.Con_Printf (PRINT_ALL, "%u underwater shader submissions, %u scene bytes, %s post-processing\n",
		post_warps, post_bytes, post_capable ? "available" : "unavailable");
}

/* Material bloom from pinned Q2PRO src/refresh/{main,shader,texture}.c.
 * A second scene attachment carries emission and alpha occlusion. Preserve
 * the drawable's colour/depth formats, sample count and z-trick depth history.
 */
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#define GL_RENDERBUFFER_BINDING 0x8CA7
#define GL_RENDERBUFFER_WIDTH 0x8D42
#define GL_RENDERBUFFER_HEIGHT 0x8D43
#define GL_RENDERBUFFER_INTERNAL_FORMAT 0x8D44
#define GL_RENDERBUFFER_SAMPLES 0x8CAB
#define GL_MAX_RENDERBUFFER_SIZE 0x84E8
#define GL_MAX_SAMPLES 0x8D57
#define GL_MAX_DRAW_BUFFERS 0x8824
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_DEPTH32F_STENCIL8 0x8CAD
#endif
#define BLOOM_MAX_BYTES (512u * 1024u * 1024u)
#define BLOOM_TEXTURE_FIRST ((GLuint)(TEXNUM_IMAGES + 2 * MAX_GLTEXTURES + 1))
#define BLOOM_MAX_RADIUS 50
#define BLOOM_MAX_TAPS (BLOOM_MAX_RADIUS + 1)

typedef struct
{
	void (APIENTRY *GenRenderbuffers) (GLsizei, GLuint *);
	void (APIENTRY *DeleteRenderbuffers) (GLsizei, const GLuint *);
	void (APIENTRY *BindRenderbuffer) (GLenum, GLuint);
	void (APIENTRY *RenderbufferStorage) (GLenum, GLenum, GLsizei, GLsizei);
	void (APIENTRY *RenderbufferStorageMultisample) (GLenum, GLsizei, GLenum, GLsizei, GLsizei);
	void (APIENTRY *GetRenderbufferParameteriv) (GLenum, GLenum, GLint *);
	void (APIENTRY *FramebufferRenderbuffer) (GLenum, GLenum, GLenum, GLuint);
	void (APIENTRY *DrawBuffers) (GLsizei, const GLenum *);
	void (APIENTRY *ClearBufferfv) (GLenum, GLint, const GLfloat *);
	void (APIENTRY *Uniform2f) (GLint, GLfloat, GLfloat);
	void (APIENTRY *Uniform1fv) (GLint, GLsizei, const GLfloat *);
} bloomprocs_t;

static bloomprocs_t bloom_procs;
static GLuint bloom_textures[4], bloom_fbos[4], bloom_buffers[3];
static GLuint bloom_blur_program, bloom_composite_program;
static GLint bloom_blur_source, bloom_blur_mode, bloom_blur_dir;
static GLint bloom_blur_count, bloom_blur_offsets, bloom_blur_weights;
static GLint bloom_composite_scene, bloom_composite_blur, bloom_composite_warp;
static GLint bloom_composite_show, bloom_composite_time;
static GLint bloom_viewport[4], bloom_scissor[4], bloom_read_fbo, bloom_read_buffer;
static GLint bloom_draw_buffer, bloom_source_read, bloom_samples;
static int bloom_width, bloom_height, bloom_small_width, bloom_small_height;
static GLenum bloom_format, bloom_depth_format;
static unsigned bloom_bytes, bloom_views, bloom_passes;
static qboolean bloom_capable, bloom_active, bloom_aborted, bloom_failed;
static qboolean bloom_scissor_enabled;
static int bloom_failed_width, bloom_failed_height;
static float bloom_sigma = -1;
static int bloom_taps;
static GLfloat bloom_offsets[BLOOM_MAX_TAPS], bloom_weights[BLOOM_MAX_TAPS];

static const char bloom_blur_shader[] =
	"#version 120\n"
	"uniform sampler2D u_source;\n"
	"uniform vec2 u_dir;\n"
	"uniform int u_mode, u_count;\n"
	"uniform float u_offsets[51], u_weights[51];\n"
	"varying vec2 v_tc;\n"
	"void main() {\n"
	"  vec4 c = vec4(0.0);\n"
	"  if (u_mode == 0) {\n"
	"    c += texture2D(u_source, v_tc + vec2(-0.25, -0.25) * u_dir) * 0.25;\n"
	"    c += texture2D(u_source, v_tc + vec2(-0.25,  0.25) * u_dir) * 0.25;\n"
	"    c += texture2D(u_source, v_tc + vec2( 0.25, -0.25) * u_dir) * 0.25;\n"
	"    c += texture2D(u_source, v_tc + vec2( 0.25,  0.25) * u_dir) * 0.25;\n"
	"  } else {\n"
	"    for (int i = 0; i < 51; i++)\n"
	"      if (i < u_count) c += texture2D(u_source, v_tc + u_dir * u_offsets[i]) * u_weights[i];\n"
	"  }\n"
	"  gl_FragColor = c;\n"
	"}\n";

static const char bloom_composite_shader[] =
	"#version 110\n"
	"uniform sampler2D u_scene, u_blur;\n"
	"uniform int u_warp, u_show;\n"
	"uniform float u_time;\n"
	"varying vec2 v_tc;\n"
	"void main() {\n"
	"  vec2 tc = v_tc;\n"
	"  if (u_warp != 0) tc += vec2(0.0025) * sin(tc.ts * vec2(31.4159265359) + u_time);\n"
	"  vec4 c = texture2D(u_scene, tc);\n"
	"  vec4 b = texture2D(u_blur, tc);\n"
	"  gl_FragColor = u_show != 0 ? b : vec4(c.rgb + b.rgb, c.a);\n"
	"}\n";

static int GL_BloomIterations (void)
{
	if (!gl_bloom || !(gl_bloom->value >= -FLT_MAX && gl_bloom->value <= FLT_MAX) ||
		!gl_bloom->intvalue)
		return 0;
	return gl_bloom->intvalue < 1 ? 1 : gl_bloom->intvalue > 8 ? 8 :
																 gl_bloom->intvalue;
}

static void GL_FreeBloomScene (void)
{
	int i;
	for (i = 0; i < 4; i++)
	{
		if (bloom_fbos[i])
			post_procs.DeleteFramebuffers (1, &bloom_fbos[i]);
		if (bloom_textures[i])
			qglDeleteTextures (1, &bloom_textures[i]);
	}
	for (i = 0; i < 3; i++)
		if (bloom_buffers[i])
			bloom_procs.DeleteRenderbuffers (1, &bloom_buffers[i]);
	memset (bloom_fbos, 0, sizeof (bloom_fbos));
	memset (bloom_textures, 0, sizeof (bloom_textures));
	memset (bloom_buffers, 0, sizeof (bloom_buffers));
	bloom_width = bloom_height = bloom_bytes = 0;
	bloom_sigma = -1;
}

static void GL_ShutdownBloom (void)
{
	GL_FreeBloomScene ();
	if (bloom_blur_program)
		post_procs.DeleteProgram (bloom_blur_program);
	if (bloom_composite_program)
		post_procs.DeleteProgram (bloom_composite_program);
	bloom_blur_program = bloom_composite_program = 0;
	memset (&bloom_procs, 0, sizeof (bloom_procs));
	bloom_capable = bloom_active = bloom_aborted = bloom_failed = false;
	bloom_views = bloom_passes = 0;
}

static void GL_InitBloom (void)
{
	GLint buffers = 0, units = 0;
	if (!qglScissor || !qglColorMask || !qglEnable)
		return;
#define BLOOM_PROC(name, result, parameters) \
	bloom_procs.name = (result (APIENTRY *) parameters)GL_PostFXProcedure ("gl" #name); \
	if (!bloom_procs.name) goto unavailable
	BLOOM_PROC (GenRenderbuffers, void, (GLsizei, GLuint *));
	BLOOM_PROC (DeleteRenderbuffers, void, (GLsizei, const GLuint *));
	BLOOM_PROC (BindRenderbuffer, void, (GLenum, GLuint));
	BLOOM_PROC (RenderbufferStorage, void, (GLenum, GLenum, GLsizei, GLsizei));
	BLOOM_PROC (RenderbufferStorageMultisample, void, (GLenum, GLsizei, GLenum, GLsizei, GLsizei));
	BLOOM_PROC (GetRenderbufferParameteriv, void, (GLenum, GLenum, GLint *));
	BLOOM_PROC (FramebufferRenderbuffer, void, (GLenum, GLenum, GLenum, GLuint));
	BLOOM_PROC (DrawBuffers, void, (GLsizei, const GLenum *));
	BLOOM_PROC (ClearBufferfv, void, (GLenum, GLint, const GLfloat *));
	BLOOM_PROC (Uniform2f, void, (GLint, GLfloat, GLfloat));
	BLOOM_PROC (Uniform1fv, void, (GLint, GLsizei, const GLfloat *));
#undef BLOOM_PROC
	qglGetIntegerv (GL_MAX_DRAW_BUFFERS, &buffers);
	qglGetIntegerv (GL_MAX_TEXTURE_IMAGE_UNITS, &units);
	bloom_capable = buffers >= 2 && units >= 3 && qglGetError () == GL_NO_ERROR;
	return;
unavailable:
	memset (&bloom_procs, 0, sizeof (bloom_procs));
}

static void GL_UpdateBloom (void)
{
	if (bloom_active)
		return;
	if ((gl_bloom && gl_bloom->modified) || (gl_bloom_sigma && gl_bloom_sigma->modified) ||
		(gl_showbloom && gl_showbloom->modified))
	{
		bloom_failed = false;
		bloom_sigma = -1;
		if (gl_bloom)
			gl_bloom->modified = false;
		if (gl_bloom_sigma)
			gl_bloom_sigma->modified = false;
		if (gl_showbloom)
			gl_showbloom->modified = false;
	}
	if (!GL_BloomIterations () || !gl_shaders || FLOAT_EQ_ZERO (gl_shaders->value))
	{
		GL_FreeBloomScene ();
		if (bloom_blur_program)
			post_procs.DeleteProgram (bloom_blur_program);
		if (bloom_composite_program)
			post_procs.DeleteProgram (bloom_composite_program);
		bloom_blur_program = bloom_composite_program = 0;
	}
}

qboolean GL_BloomActive (void)
{
	return bloom_active && !bloom_aborted;
}

void GL_BloomAbort (void)
{
	if (bloom_active)
		bloom_aborted = true;
}

qboolean GL_BloomTargets (qboolean material)
{
	static const GLenum attachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	if (!bloom_active)
		return true;
	bloom_procs.DrawBuffers (material && !bloom_aborted ? 2 : 1, attachments);
	if (qglGetError () == GL_NO_ERROR)
		return true;
	GL_BloomAbort ();
	qglDrawBuffer (GL_COLOR_ATTACHMENT0);
	return false;
}

/* Pair adjacent normalized Gaussian weights for bilinear sampling exactly as
 * the donor does. The fixed uniform arrays avoid shader recompilation on resize.
 */
static void GL_BloomKernel (float sigma)
{
	float raw[2 * BLOOM_MAX_RADIUS + 1], sum = 0;
	int radius, i, j;
	if (bloom_sigma == sigma)
		return;
	bloom_sigma = sigma;
	radius = (int)(sigma * 2 + 0.5f);
	if (radius > BLOOM_MAX_RADIUS)
		radius = BLOOM_MAX_RADIUS;
	memset (bloom_offsets, 0, sizeof (bloom_offsets));
	memset (bloom_weights, 0, sizeof (bloom_weights));
	if (radius < 1)
	{
		bloom_taps = 1;
		bloom_weights[0] = 1;
		return;
	}
	for (i = -radius; i <= radius; i++)
	{
		raw[i + radius] = (float)exp (-(i * i) / (double)(sigma * sigma));
		sum += raw[i + radius];
	}
	for (i = 0; i <= radius * 2; i++)
		raw[i] /= sum;
	for (i = -radius, j = 0; i <= radius; i += 2, j++)
	{
		float weight = raw[i + radius];
		bloom_offsets[j] = (float)i;
		if (i != radius)
		{
			weight += raw[i + radius + 1];
			if (weight > 0)
				bloom_offsets[j] += raw[i + radius + 1] / weight;
		}
		bloom_weights[j] = weight;
	}
	bloom_taps = radius + 1;
}

static GLuint GL_CreateBloomProgram (const char *source)
{
	GLuint program = 0, vertex = 0, fragment = 0;
	GLint linked = 0;
	program = post_procs.CreateProgram ();
	if (!program)
		return 0;
	vertex = GL_CompilePostFXShader (GL_VERTEX_SHADER, post_vertex_shader);
	fragment = GL_CompilePostFXShader (GL_FRAGMENT_SHADER, source);
	if (!vertex || !fragment)
		goto fail;
	post_procs.AttachShader (program, vertex);
	post_procs.AttachShader (program, fragment);
	post_procs.LinkProgram (program);
	post_procs.GetProgramiv (program, GL_LINK_STATUS, &linked);
	if (!linked || qglGetError () != GL_NO_ERROR)
		goto fail;
	post_procs.DeleteShader (vertex);
	post_procs.DeleteShader (fragment);
	return program;
fail:
	if (vertex)
		post_procs.DeleteShader (vertex);
	if (fragment)
		post_procs.DeleteShader (fragment);
	post_procs.DeleteProgram (program);
	return 0;
}

static qboolean GL_EnsureBloomPrograms (void)
{
	if (!bloom_blur_program)
	{
		bloom_blur_program = GL_CreateBloomProgram (bloom_blur_shader);
		if (!bloom_blur_program)
			return false;
#define BLOOM_UNIFORM(field, name) \
	field = post_procs.GetUniformLocation (bloom_blur_program, name); \
	if (field < 0) return false
		BLOOM_UNIFORM (bloom_blur_source, "u_source");
		BLOOM_UNIFORM (bloom_blur_mode, "u_mode");
		BLOOM_UNIFORM (bloom_blur_dir, "u_dir");
		BLOOM_UNIFORM (bloom_blur_count, "u_count");
		BLOOM_UNIFORM (bloom_blur_offsets, "u_offsets[0]");
		BLOOM_UNIFORM (bloom_blur_weights, "u_weights[0]");
#undef BLOOM_UNIFORM
	}
	if (!bloom_composite_program)
	{
		bloom_composite_program = GL_CreateBloomProgram (bloom_composite_shader);
		if (!bloom_composite_program)
			return false;
#define BLOOM_UNIFORM(field, name) \
	field = post_procs.GetUniformLocation (bloom_composite_program, name); \
	if (field < 0) return false
		BLOOM_UNIFORM (bloom_composite_scene, "u_scene");
		BLOOM_UNIFORM (bloom_composite_blur, "u_blur");
		BLOOM_UNIFORM (bloom_composite_warp, "u_warp");
		BLOOM_UNIFORM (bloom_composite_show, "u_show");
		BLOOM_UNIFORM (bloom_composite_time, "u_time");
#undef BLOOM_UNIFORM
	}
	return qglGetError () == GL_NO_ERROR;
}

static qboolean GL_BloomBuffer (int index, GLenum format, int samples)
{
	GLint width = 0, height = 0, actual_format = 0, actual_samples = 0;
	bloom_procs.GenRenderbuffers (1, &bloom_buffers[index]);
	if (!bloom_buffers[index])
		return false;
	bloom_procs.BindRenderbuffer (GL_RENDERBUFFER, bloom_buffers[index]);
	if (samples)
		bloom_procs.RenderbufferStorageMultisample (GL_RENDERBUFFER, samples, format, bloom_width, bloom_height);
	else
		bloom_procs.RenderbufferStorage (GL_RENDERBUFFER, format, bloom_width, bloom_height);
	bloom_procs.GetRenderbufferParameteriv (GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
	bloom_procs.GetRenderbufferParameteriv (GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
	bloom_procs.GetRenderbufferParameteriv (GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &actual_format);
	bloom_procs.GetRenderbufferParameteriv (GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &actual_samples);
	return width == bloom_width && height == bloom_height && actual_format == (GLint)format &&
		actual_samples == samples && qglGetError () == GL_NO_ERROR;
}

static qboolean GL_EnsureBloomScene (int width, int height, GLenum format, GLenum depth_format, int samples)
{
	int i;
	GLint actual = 0;
	if (bloom_textures[0] && width == bloom_width && height == bloom_height &&
		format == bloom_format && depth_format == bloom_depth_format && samples == bloom_samples)
		return true;
	GL_FreeBloomScene ();
	bloom_width = width;
	bloom_height = height;
	bloom_format = format;
	bloom_depth_format = depth_format;
	bloom_samples = samples;
	bloom_small_width = width / 4;
	bloom_small_height = height / 4;
	if (!bloom_small_width)
		bloom_small_width = 1;
	if (!bloom_small_height)
		bloom_small_height = 1;
	for (i = 0; i < 4; i++)
	{
		int w = i < 2 ? width : bloom_small_width;
		int h = i < 2 ? height : bloom_small_height;
		GLenum internal = i ? GL_RGBA8 : format;
		GLuint texture = BLOOM_TEXTURE_FIRST + i;
		if (qglIsTexture (texture))
			return false;
		bloom_textures[i] = texture;
		qglBindTexture (GL_TEXTURE_2D, texture);
		qglGetIntegerv (GL_TEXTURE_BINDING_2D, &actual);
		if (actual != (GLint)texture || qglGetError () != GL_NO_ERROR)
			return false;
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		qglTexImage2D (GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &actual);
		if (actual != w)
			return false;
		qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &actual);
		if (actual != h)
			return false;
		qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &actual);
		if (actual != (GLint)internal || qglGetError () != GL_NO_ERROR)
			return false;
	}
	post_procs.GenFramebuffers (4, bloom_fbos);
	for (i = 0; i < 4; i++)
		if (!bloom_fbos[i])
			return false;
	post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, bloom_fbos[1]);
	for (i = 0; i < 2; i++)
		post_procs.FramebufferTexture2D (GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, bloom_textures[i], 0);
	qglDrawBuffer (GL_COLOR_ATTACHMENT0);
	if (post_procs.CheckFramebufferStatus (GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		return false;
	post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, bloom_fbos[0]);
	if (samples)
	{
		for (i = 0; i < 2; i++)
		{
			if (!GL_BloomBuffer (i, i ? GL_RGBA8 : format, samples))
				return false;
			bloom_procs.FramebufferRenderbuffer (GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER, bloom_buffers[i]);
		}
	}
	else
		for (i = 0; i < 2; i++)
			post_procs.FramebufferTexture2D (GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, bloom_textures[i], 0);
	if (!GL_BloomBuffer (2, depth_format, samples))
		return false;
	bloom_procs.FramebufferRenderbuffer (GL_DRAW_FRAMEBUFFER,
		depth_format == GL_DEPTH24_STENCIL8 || depth_format == GL_DEPTH32F_STENCIL8 ?
			GL_DEPTH_STENCIL_ATTACHMENT :
			GL_DEPTH_ATTACHMENT,
		GL_RENDERBUFFER, bloom_buffers[2]);
	qglDrawBuffer (GL_COLOR_ATTACHMENT0);
	if (post_procs.CheckFramebufferStatus (GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		return false;
	for (i = 2; i < 4; i++)
	{
		post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, bloom_fbos[i]);
		post_procs.FramebufferTexture2D (GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom_textures[i], 0);
		qglDrawBuffer (GL_COLOR_ATTACHMENT0);
		if (post_procs.CheckFramebufferStatus (GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			return false;
	}
	return qglGetError () == GL_NO_ERROR;
}

static void GL_BloomRestoreFrame (void)
{
	post_procs.BindFramebuffer (GL_READ_FRAMEBUFFER, 0);
	qglReadBuffer ((GLenum)bloom_source_read);
	post_procs.BindFramebuffer (GL_READ_FRAMEBUFFER, (GLuint)bloom_read_fbo);
	qglReadBuffer ((GLenum)bloom_read_buffer);
	post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, 0);
	qglDrawBuffer ((GLenum)bloom_draw_buffer);
	qglViewport (bloom_viewport[0], bloom_viewport[1], bloom_viewport[2], bloom_viewport[3]);
	qglScissor (bloom_scissor[0], bloom_scissor[1], bloom_scissor[2], bloom_scissor[3]);
}

static void GL_BloomFailure (void)
{
	GL_FreeBloomScene ();
	if (bloom_blur_program)
		post_procs.DeleteProgram (bloom_blur_program);
	if (bloom_composite_program)
		post_procs.DeleteProgram (bloom_composite_program);
	bloom_blur_program = bloom_composite_program = 0;
	bloom_failed = true;
	bloom_failed_width = bloom_viewport[2];
	bloom_failed_height = bloom_viewport[3];
	ri.Con_Printf (PRINT_ALL, "Material bloom disabled after failure; resize or change bloom controls to retry.\n");
}

qboolean GL_BloomBeginView (const refdef_t *fd)
{
	GLint maximum = 0, render_maximum = 0, maximum_samples = 0, draw_fbo = 0;
	GLint depth = 0, stencil = 0, component = 0, samples = 0, saved_unpack = 0, saved_buffer = 0, binding = 0;
	GLenum format = 0, depth_format = 0;
	unsigned pixel_bytes = 0, depth_bytes = 4, bytes;
	unsigned long long storage;
	GLfloat zero[4] = { 0, 0, 0, 0 };
	double sx, sy;
	qboolean ready;
	GL_UpdatePostFX ();
	if (!fd || (fd->rdflags & RDF_NOWORLDMODEL) || !bloom_capable || bloom_active ||
		!GL_BloomIterations () || !(post_options & 1) || !GL_SurfaceShaderAvailable ())
		return false;
	qglGetIntegerv (GL_VIEWPORT, bloom_viewport);
	if (bloom_viewport[0] < 0 || bloom_viewport[1] < 0 || bloom_viewport[2] <= 0 || bloom_viewport[3] <= 0 ||
		(unsigned)bloom_viewport[2] > vid.width || (unsigned)bloom_viewport[3] > vid.height ||
		(unsigned)bloom_viewport[0] > vid.width - (unsigned)bloom_viewport[2] ||
		(unsigned)bloom_viewport[1] > vid.height - (unsigned)bloom_viewport[3] ||
		bloom_viewport[0] > INT_MAX - bloom_viewport[2] || bloom_viewport[1] > INT_MAX - bloom_viewport[3])
		return false;
	if (bloom_failed && bloom_failed_width == bloom_viewport[2] && bloom_failed_height == bloom_viewport[3])
		return false;
	bloom_failed = false;
	qglGetIntegerv (GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
	if (draw_fbo || qglIsEnabled (GL_FRAMEBUFFER_SRGB))
		return false;
	qglGetIntegerv (GL_DRAW_BUFFER, &bloom_draw_buffer);
	if (!GL_PostFXFormat ((GLenum)bloom_draw_buffer, &format, &pixel_bytes))
		return false;
	qglGetIntegerv (GL_DEPTH_BITS, &depth);
	qglGetIntegerv (GL_STENCIL_BITS, &stencil);
	post_procs.GetFramebufferAttachmentParameteriv (GL_DRAW_FRAMEBUFFER, GL_DEPTH,
		GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &component);
	if (stencil == 8 && depth == 24 && component == GL_UNSIGNED_NORMALIZED)
		depth_format = GL_DEPTH24_STENCIL8;
	else if (stencil == 8 && depth == 32 && component == GL_FLOAT)
		depth_format = GL_DEPTH32F_STENCIL8;
	else if (!stencil && component == GL_UNSIGNED_NORMALIZED)
		depth_format = depth == 16 ? GL_DEPTH_COMPONENT16 : depth == 24 ? GL_DEPTH_COMPONENT24 :
			depth == 32													? GL_DEPTH_COMPONENT32 :
																		  0;
	else if (!stencil && depth == 32 && component == GL_FLOAT)
		depth_format = GL_DEPTH_COMPONENT32F;
	if (!depth_format)
		return false;
	qglGetIntegerv (GL_SAMPLES, &samples);
	qglGetIntegerv (GL_MAX_SAMPLES, &maximum_samples);
	qglGetIntegerv (GL_MAX_TEXTURE_SIZE, &maximum);
	qglGetIntegerv (GL_MAX_RENDERBUFFER_SIZE, &render_maximum);
	if (samples < 0 || samples > maximum_samples || bloom_viewport[2] > maximum || bloom_viewport[3] > maximum ||
		bloom_viewport[2] > render_maximum || bloom_viewport[3] > render_maximum)
		return false;
	/* Packed 32F/stencil consumes eight bytes; other admitted depth stores need at most four. */
	if (depth_format == GL_DEPTH32F_STENCIL8)
		depth_bytes = 8;
	storage = (unsigned long long)bloom_viewport[2] * bloom_viewport[3] *
		(pixel_bytes + 4 + depth_bytes) * (samples ? (unsigned)samples : 1);
	if (samples)
		storage += (unsigned long long)bloom_viewport[2] * bloom_viewport[3] * (pixel_bytes + 4);
	storage += (unsigned long long)(bloom_viewport[2] / 4 + 1) * (bloom_viewport[3] / 4 + 1) * 8;
	if (storage > BLOOM_MAX_BYTES)
		return false;
	bytes = (unsigned)storage;
	qglGetIntegerv (GL_SCISSOR_BOX, bloom_scissor);
	sx = (double)bloom_scissor[0] - bloom_viewport[0];
	sy = (double)bloom_scissor[1] - bloom_viewport[1];
	if (sx < INT_MIN || sx > INT_MAX || sy < INT_MIN || sy > INT_MAX)
		return false;
	bloom_scissor_enabled = qglIsEnabled (GL_SCISSOR_TEST);
	qglGetIntegerv (GL_READ_FRAMEBUFFER_BINDING, &bloom_read_fbo);
	qglGetIntegerv (GL_READ_BUFFER, &bloom_read_buffer);
	qglGetIntegerv (GL_PIXEL_UNPACK_BUFFER_BINDING, &saved_unpack);
	qglGetIntegerv (GL_RENDERBUFFER_BINDING, &saved_buffer);
	if (!GL_ClearErrors ())
		return false;
	qglPushAttrib (GL_ALL_ATTRIB_BITS);
	if (qglGetError () != GL_NO_ERROR)
		return false;
	post_procs.ActiveTexture (GL_TEXTURE0_ARB);
	post_procs.BindBuffer (GL_PIXEL_UNPACK_BUFFER, 0);
	qglDisable (GL_SCISSOR_TEST);
	qglGetIntegerv (GL_PIXEL_UNPACK_BUFFER_BINDING, &binding);
	ready = !binding && qglGetError () == GL_NO_ERROR && GL_EnsureBloomPrograms () &&
		GL_EnsureBloomScene (bloom_viewport[2], bloom_viewport[3], format, depth_format, samples);
	post_procs.BindBuffer (GL_PIXEL_UNPACK_BUFFER, (GLuint)saved_unpack);
	bloom_procs.BindRenderbuffer (GL_RENDERBUFFER, (GLuint)saved_buffer);
	post_procs.BindFramebuffer (GL_READ_FRAMEBUFFER, 0);
	qglGetIntegerv (GL_READ_BUFFER, &bloom_source_read);
	qglReadBuffer ((GLenum)bloom_draw_buffer);
	post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, bloom_fbos[0]);
	qglGetIntegerv (GL_DRAW_FRAMEBUFFER_BINDING, &binding);
	ready = ready && binding == (GLint)bloom_fbos[0] && qglGetError () == GL_NO_ERROR;
	if (ready)
	{
		qglDrawBuffer (GL_COLOR_ATTACHMENT0);
		post_procs.BlitFramebuffer (bloom_viewport[0], bloom_viewport[1], bloom_viewport[0] + bloom_width, bloom_viewport[1] + bloom_height,
			0, 0, bloom_width, bloom_height, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | (stencil ? GL_STENCIL_BUFFER_BIT : 0), GL_NEAREST);
		/* Clear only emission; keep the drawable's existing clear/z-trick content. */
		ready = qglGetError () == GL_NO_ERROR;
		if (ready)
		{
			qglDrawBuffer (GL_COLOR_ATTACHMENT1);
			qglColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			if (qglGetError () == GL_NO_ERROR)
				bloom_procs.ClearBufferfv (GL_COLOR, 0, zero);
			else
				ready = false;
			ready = ready && qglGetError () == GL_NO_ERROR;
		}
	}
	GL_BloomRestoreFrame ();
	qglPopAttrib ();
	if (!ready)
	{
		GL_BloomFailure ();
		return false;
	}
	bloom_bytes = bytes;
	post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, bloom_fbos[0]);
	qglDrawBuffer (GL_COLOR_ATTACHMENT0);
	qglViewport (0, 0, bloom_width, bloom_height);
	qglScissor ((GLint)sx, (GLint)sy, bloom_scissor[2], bloom_scissor[3]);
	if (qglGetError () != GL_NO_ERROR)
	{
		GL_BloomRestoreFrame ();
		GL_BloomFailure ();
		return false;
	}
	bloom_active = true;
	bloom_aborted = false;
	return true;
}

static void GL_BloomScreenState (void)
{
	int i;
	qglDisable (GL_SCISSOR_TEST);
	qglDisable (GL_DEPTH_TEST);
	qglDepthMask (GL_FALSE);
	qglDisable (GL_STENCIL_TEST);
	qglDisable (GL_ALPHA_TEST);
	qglDisable (GL_CULL_FACE);
	qglDisable (GL_BLEND);
	qglDisable (GL_LIGHTING);
	qglDisable (GL_FOG);
	qglDisable (GL_COLOR_LOGIC_OP);
	qglDisable (GL_SAMPLE_ALPHA_TO_COVERAGE);
	qglDisable (GL_SAMPLE_ALPHA_TO_ONE);
	qglDisable (GL_SAMPLE_COVERAGE);
	qglDisable (GL_POLYGON_SMOOTH);
	qglDisable (GL_POLYGON_OFFSET_FILL);
	qglPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	qglColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	for (i = 0; i < post_clip_planes; i++)
		qglDisable (GL_CLIP_PLANE0 + i);
}

void GL_BloomEndView (const refdef_t *fd)
{
	GLint program = 0, sampler[2] = { 0, 0 }, color_mask[4] = { 0 };
	unsigned sampler_saved = 0;
	int i, iterations;
	float sigma;
	qboolean ok, pushed = false;
	if (!bloom_active)
		return;
	ok = !bloom_aborted;
	qglGetIntegerv (GL_CURRENT_PROGRAM, &program);
	qglGetIntegerv (GL_COLOR_WRITEMASK, color_mask);
	if (qglGetError () == GL_NO_ERROR)
	{
		qglPushAttrib (GL_ALL_ATTRIB_BITS);
		pushed = qglGetError () == GL_NO_ERROR;
	}
	if (!pushed)
		ok = false;
	if (pushed)
	{
		GL_BloomScreenState ();
		/* Restore the sampled depth to the drawable for the retained z-trick. */
		post_procs.BindFramebuffer (GL_READ_FRAMEBUFFER, bloom_fbos[0]);
		post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, 0);
		qglDrawBuffer ((GLenum)bloom_draw_buffer);
		post_procs.BlitFramebuffer (0, 0, bloom_width, bloom_height, bloom_viewport[0], bloom_viewport[1],
			bloom_viewport[0] + bloom_width, bloom_viewport[1] + bloom_height,
			GL_DEPTH_BUFFER_BIT | (bloom_depth_format == GL_DEPTH24_STENCIL8 || bloom_depth_format == GL_DEPTH32F_STENCIL8 ? GL_STENCIL_BUFFER_BIT : 0), GL_NEAREST);
		if (qglGetError () != GL_NO_ERROR)
			ok = false;
		if (bloom_samples)
		{
			post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, bloom_fbos[1]);
			for (i = 0; i < 2; i++)
			{
				qglReadBuffer (GL_COLOR_ATTACHMENT0 + i);
				qglDrawBuffer (GL_COLOR_ATTACHMENT0 + i);
				post_procs.BlitFramebuffer (0, 0, bloom_width, bloom_height, 0, 0, bloom_width, bloom_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			}
			if (qglGetError () != GL_NO_ERROR)
				ok = false;
		}
		for (i = 0; i < 2; i++)
		{
			post_procs.ActiveTexture (GL_TEXTURE0_ARB + i);
			if (post_procs.BindSampler)
			{
				qglGetIntegerv (GL_SAMPLER_BINDING, &sampler[i]);
				if (qglGetError () == GL_NO_ERROR)
				{
					sampler_saved |= 1u << i;
					post_procs.BindSampler (i, 0);
				}
				else
					ok = false;
			}
		}
		post_procs.ActiveTexture (GL_TEXTURE0_ARB);
		if (ok)
		{
			sigma = gl_bloom_sigma && gl_bloom_sigma->value >= -FLT_MAX && gl_bloom_sigma->value <= FLT_MAX ? gl_bloom_sigma->value : 4;
			if (sigma < 1)
				sigma = 1;
			if (sigma > 25)
				sigma = 25;
			GL_BloomKernel (sigma * (bloom_height / 1080.0f));
			post_procs.UseProgram (bloom_blur_program);
			post_procs.Uniform1i (bloom_blur_source, 0);
			post_procs.Uniform1i (bloom_blur_mode, 0);
			post_procs.Uniform1i (bloom_blur_count, bloom_taps);
			bloom_procs.Uniform1fv (bloom_blur_offsets, BLOOM_MAX_TAPS, bloom_offsets);
			bloom_procs.Uniform1fv (bloom_blur_weights, BLOOM_MAX_TAPS, bloom_weights);
			bloom_procs.Uniform2f (bloom_blur_dir, 1.0f / bloom_small_width, 1.0f / bloom_small_height);
			qglViewport (0, 0, bloom_small_width, bloom_small_height);
			post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, bloom_fbos[2]);
			qglDrawBuffer (GL_COLOR_ATTACHMENT0);
			qglBindTexture (GL_TEXTURE_2D, bloom_textures[1]);
			GL_PostFXQuad ();
			ok = qglGetError () == GL_NO_ERROR;
			iterations = GL_BloomIterations () * 2;
			post_procs.Uniform1i (bloom_blur_mode, 1);
			for (i = 0; i < iterations && ok; i++)
			{
				int j = i & 1;
				bloom_procs.Uniform2f (bloom_blur_dir, j ? 1.0f / bloom_small_width : 0, j ? 0 : 1.0f / bloom_small_height);
				post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, bloom_fbos[j ? 2 : 3]);
				qglDrawBuffer (GL_COLOR_ATTACHMENT0);
				qglBindTexture (GL_TEXTURE_2D, bloom_textures[j ? 3 : 2]);
				GL_PostFXQuad ();
				ok = qglGetError () == GL_NO_ERROR;
				bloom_passes++;
			}
		}
		post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, 0);
		qglDrawBuffer ((GLenum)bloom_draw_buffer);
		qglViewport (bloom_viewport[0], bloom_viewport[1], bloom_width, bloom_height);
		qglColorMask ((GLboolean)color_mask[0], (GLboolean)color_mask[1], (GLboolean)color_mask[2], (GLboolean)color_mask[3]);
		if (ok && fd && fd->time >= -FLT_MAX && fd->time <= FLT_MAX)
		{
			post_procs.UseProgram (bloom_composite_program);
			post_procs.Uniform1i (bloom_composite_scene, 0);
			post_procs.Uniform1i (bloom_composite_blur, 1);
			post_procs.Uniform1i (bloom_composite_warp, (fd->rdflags & RDF_UNDERWATER) && (post_options & 2));
			post_procs.Uniform1i (bloom_composite_show, gl_showbloom && gl_showbloom->intvalue);
			post_procs.Uniform1f (bloom_composite_time, fd->time);
			qglBindTexture (GL_TEXTURE_2D, bloom_textures[0]);
			post_procs.ActiveTexture (GL_TEXTURE1_ARB);
			qglBindTexture (GL_TEXTURE_2D, bloom_textures[2]);
			post_procs.ActiveTexture (GL_TEXTURE0_ARB);
			if (qglGetError () == GL_NO_ERROR)
			{
				GL_PostFXQuad ();
				ok = qglGetError () == GL_NO_ERROR;
			}
			else
				ok = false;
			if (ok)
				bloom_views++;
		}
		else
			ok = false;
		if (!ok)
		{
			/* A single copy restores base colour; never resubmit world/alpha geometry. */
			post_procs.BindFramebuffer (GL_READ_FRAMEBUFFER, bloom_fbos[0]);
			qglReadBuffer (GL_COLOR_ATTACHMENT0);
			post_procs.BlitFramebuffer (0, 0, bloom_width, bloom_height, bloom_viewport[0], bloom_viewport[1],
				bloom_viewport[0] + bloom_width, bloom_viewport[1] + bloom_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			if (qglGetError () != GL_NO_ERROR)
				ri.Con_Printf (PRINT_ALL, "Bloom base-view restoration failed.\n");
		}
		post_procs.UseProgram ((GLuint)program);
		if (post_procs.BindSampler)
			for (i = 0; i < 2; i++)
				if (sampler_saved & (1u << i))
					post_procs.BindSampler (i, (GLuint)sampler[i]);
		qglPopAttrib ();
	}
	else
	{
		/* Attribute-stack failure cannot consume the view or re-render entities. */
		qglDisable (GL_SCISSOR_TEST);
		post_procs.BindFramebuffer (GL_READ_FRAMEBUFFER, bloom_fbos[0]);
		qglReadBuffer (GL_COLOR_ATTACHMENT0);
		post_procs.BindFramebuffer (GL_DRAW_FRAMEBUFFER, 0);
		qglDrawBuffer ((GLenum)bloom_draw_buffer);
		post_procs.BlitFramebuffer (0, 0, bloom_width, bloom_height, bloom_viewport[0], bloom_viewport[1],
			bloom_viewport[0] + bloom_width, bloom_viewport[1] + bloom_height, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | (bloom_depth_format == GL_DEPTH24_STENCIL8 || bloom_depth_format == GL_DEPTH32F_STENCIL8 ? GL_STENCIL_BUFFER_BIT : 0), GL_NEAREST);
		if (bloom_scissor_enabled)
			qglEnable (GL_SCISSOR_TEST);
	}
	GL_BloomRestoreFrame ();
	bloom_active = false;
	if (!ok)
	{
		GL_BloomFailure ();
		GL_PostProcessView (fd);
	}
}

void GL_PrintBloomInfo (void)
{
	ri.Con_Printf (PRINT_ALL, "%u material bloom views, %u blur passes, %u logical framebuffer bytes, %s bloom\n",
		bloom_views, bloom_passes, bloom_bytes, bloom_capable ? "available" : "unavailable");
}
