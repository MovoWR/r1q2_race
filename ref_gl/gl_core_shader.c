/* Private shader adaptation for the optional core renderer. */
#include "gl_local.h"
#ifdef R1Q2_GL_CORE
#include "gl_core.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#define CORE_SHADER_LIMIT 128
#define CORE_PROGRAM_LIMIT 64
#define CORE_SOURCE_LIMIT (128 * 1024)
#define CORE_OUTPUT_LIMIT (CORE_SOURCE_LIMIT * 2)

typedef struct
{
	GLuint name;
	GLenum type;
	qboolean generic, deleted, flat;
	unsigned attachments;
} core_shader_t;

typedef struct
{
	GLuint name;
	qboolean generic;
	GLint mvp, modelview, projection, texture;
	GLint alpha_enabled, alpha_func, alpha_ref, point_sprite, point_replace, point_size, attenuation, point_range;
	GLint points, point_smooth, point_fade;
	GLint sampler0, sampler1, enabled, texenv, format, texenv_scale;
	GLint showtris_uniform;
	qboolean showtris;
} core_program_t;

static struct
{
	GLuint (APIENTRY *CreateShader) (GLenum);
	void (APIENTRY *DeleteShader) (GLuint);
	void (APIENTRY *ShaderSource) (GLuint, GLsizei, const char *const *, const GLint *);
	void (APIENTRY *CompileShader) (GLuint);
	void (APIENTRY *GetShaderiv) (GLuint, GLenum, GLint *);
	void (APIENTRY *GetShaderInfoLog) (GLuint, GLsizei, GLsizei *, char *);
	GLuint (APIENTRY *CreateProgram) (void);
	void (APIENTRY *AttachShader) (GLuint, GLuint);
	void (APIENTRY *DetachShader) (GLuint, GLuint);
	void (APIENTRY *GetAttachedShaders) (GLuint, GLsizei, GLsizei *, GLuint *);
	void (APIENTRY *LinkProgram) (GLuint);
	void (APIENTRY *GetProgramiv) (GLuint, GLenum, GLint *);
	void (APIENTRY *GetProgramInfoLog) (GLuint, GLsizei, GLsizei *, char *);
	void (APIENTRY *DeleteProgram) (GLuint);
	void (APIENTRY *UseProgram) (GLuint);
	void (APIENTRY *GetIntegerv) (GLenum, GLint *);
	GLint (APIENTRY *GetUniformLocation) (GLuint, const char *);
	void (APIENTRY *Uniform1i) (GLint, GLint);
	void (APIENTRY *Uniform1f) (GLint, GLfloat);
	void (APIENTRY *Uniform2f) (GLint, GLfloat, GLfloat);
	void (APIENTRY *Uniform2i) (GLint, GLint, GLint);
	void (APIENTRY *Uniform3f) (GLint, GLfloat, GLfloat, GLfloat);
	void (APIENTRY *UniformMatrix4fv) (GLint, GLsizei, GLboolean, const GLfloat *);
} core_shader_procs;

static core_shader_t core_shaders[CORE_SHADER_LIMIT];
static core_program_t core_programs[CORE_PROGRAM_LIMIT];
static GLuint core_default_program;
static GLuint core_flat_program;
static qboolean core_draw_points;

static const char core_default_vertex[] =
	"#version 110\n"
	"varying vec4 v_color; varying vec4 v_tc0, v_tc1;\n"
	"void main() { gl_Position = ftransform(); v_color = clamp(gl_Color, 0.0, 1.0);"
	"v_tc0 = gl_TextureMatrix[0] * gl_MultiTexCoord0;"
	"v_tc1 = gl_TextureMatrix[1] * gl_MultiTexCoord1; }\n";
static const char core_default_fragment[] =
	"#version 110\n"
	"uniform bool r1_showtris;\n"
	"uniform sampler2D r1_sampler0, r1_sampler1;\n"
	"uniform ivec2 r1_enabled, r1_texenv;\n"
	"uniform ivec2 r1_format; uniform vec2 r1_texenv_scale;\n"
	"varying vec4 v_color; varying vec4 v_tc0, v_tc1;\n"
	"vec4 stage(vec4 c, vec4 t, int mode, int fmt, float scale) {"
	"bool rgb = fmt != 3; bool alpha = fmt != 1;"
	"if (mode == 7681) { if (rgb) c.rgb = t.rgb; if (alpha) c.a = t.a; }"
	"else if (mode == 260) { if (rgb) c.rgb += t.rgb; if (alpha) c.a = fmt == 4 ? c.a + t.a : c.a * t.a; }"
	"else if (mode == 8449) { c.rgb = fmt == 2 ? mix(c.rgb, t.rgb, t.a) : t.rgb; }"
	"else { if (rgb) c.rgb *= t.rgb * (mode == 34160 ? scale : 1.0); if (alpha) c.a *= t.a; }"
	"return clamp(c, 0.0, 1.0); }\n"
	"void main() { if (r1_showtris) { gl_FragData[0] = vec4(1.0); gl_FragData[1] = vec4(0.0); return; } vec4 c = v_color;"
	"if (r1_enabled.x != 0) c = stage(c, texture2DProj(r1_sampler0, v_tc0), r1_texenv.x, r1_format.x, r1_texenv_scale.x);"
	"if (r1_enabled.y != 0) c = stage(c, texture2DProj(r1_sampler1, v_tc1), r1_texenv.y, r1_format.y, r1_texenv_scale.y);"
	"gl_FragData[0] = c; gl_FragData[1] = vec4(0.0, 0.0, 0.0, c.a); }\n";

static core_shader_t *CoreShaderRecord (GLuint name)
{
	unsigned i;
	for (i = 0; i < CORE_SHADER_LIMIT; i++)
		if (core_shaders[i].name == name && name)
			return &core_shaders[i];
	return NULL;
}

static core_program_t *CoreProgramRecord (GLuint name, qboolean create)
{
	unsigned i;
	core_program_t *slot = NULL;
	for (i = 0; i < CORE_PROGRAM_LIMIT; i++)
	{
		if (core_programs[i].name == name && name)
			return &core_programs[i];
		if (!core_programs[i].name && !slot)
			slot = &core_programs[i];
	}
	if (create && slot)
	{
		memset (slot, 0, sizeof (*slot));
		slot->name = name;
		return slot;
	}
	return NULL;
}

/* Only renderer-owned GLSL is accepted. Bound concatenation before allocation. */
static char *CoreShaderJoin (GLsizei count, const char *const *strings, const GLint *lengths)
{
	size_t total = 0, length, offset = 0;
	GLsizei i;
	char *joined;
	if (count < 1 || count > CORE_SHADER_LIMIT || !strings)
		return NULL;
	for (i = 0; i < count; i++)
	{
		if (!strings[i])
			return NULL;
		if (lengths && lengths[i] >= 0)
			length = (size_t)lengths[i];
		else
		{
			for (length = 0; length <= CORE_SOURCE_LIMIT && strings[i][length]; length++)
			{
			}
		}
		if (length > CORE_SOURCE_LIMIT - total)
			return NULL;
		/* Embedded NUL bytes are not renderer shader text. */
		if (memchr (strings[i], 0, length))
			return NULL;
		total += length;
	}
	joined = malloc (total + 1);
	if (!joined)
		return NULL;
	for (i = 0; i < count; i++)
	{
		length = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen (strings[i]);
		memcpy (joined + offset, strings[i], length);
		offset += length;
	}
	joined[offset] = 0;
	return joined;
}

static qboolean CoreShaderAppend (char *out, size_t *used, const char *text, size_t length)
{
	if (length >= CORE_OUTPUT_LIMIT - *used)
		return false;
	memcpy (out + *used, text, length);
	*used += length;
	out[*used] = 0;
	return true;
}

static qboolean CoreShaderWord (const char *text, size_t length, const char *word)
{
	return strlen (word) == length && !memcmp (text, word, length);
}

/* Token replacement leaves comments, numeric literals and effect expressions intact. */
static char *CoreShaderTranslate (GLenum type, const char *source, qboolean *generic, qboolean flat)
{
	static const struct
	{
		const char *from, *to;
	} tokens[] = {
		{ "gl_ModelViewProjectionMatrix", "r1_core_mvp" },
		{ "gl_ModelViewMatrix", "r1_core_modelview" },
		{ "gl_ProjectionMatrix", "r1_core_projection" },
		{ "gl_TextureMatrix", "r1_core_texture" },
		{ "gl_Vertex", "r1_core_position" },
		{ "gl_MultiTexCoord0", "r1_core_tc0" },
		{ "gl_MultiTexCoord1", "r1_core_tc1" },
		{ "gl_ClipVertex", "r1_core_clip" },
		{ "gl_FragColor", "r1_core_frag[0]" },
		{ "gl_FragData", "r1_core_frag" },
		{ "texture2D", "texture" }, { "textureCube", "texture" },
		{ "ftransform", "r1_core_transform" }, { "main", "r1_core_main" }
	};

	char *body, *out;
	const char *p, *start, *replacement;
	size_t used = 0, length;
	unsigned i;
	qboolean vertex = type == GL_VERTEX_SHADER, color_input = false;
	int version = 330;
	body = malloc (CORE_OUTPUT_LIMIT);
	out = malloc (CORE_OUTPUT_LIMIT);
	if (!body || !out)
	{
		free (body);
		free (out);
		return NULL;
	}
	body[0] = 0;
	*generic = false;
	for (p = source; *p;)
	{
		start = p;
		if (*p == '#' && !strncmp (p, "#version", 8))
		{
			if (atoi (p + 8) >= 430)
				version = 430;
			while (*p && *p != '\n')
				p++;
			continue;
		}
		if ((p[0] == '/' && p[1] == '/') || (p[0] == '/' && p[1] == '*'))
		{
			if (p[1] == '/')
			{
				while (*p && *p != '\n')
					p++;
			}
			else
			{
				p += 2;
				while (*p && !(p[0] == '*' && p[1] == '/'))
					p++;
				if (*p)
					p += 2;
			}
			if (!CoreShaderAppend (body, &used, start, p - start))
				goto fail;
			continue;
		}
		if (isalpha ((unsigned char)*p) || *p == '_')
		{
			p++;
			while (isalnum ((unsigned char)*p) || *p == '_')
				p++;
			length = p - start;
			replacement = NULL;
			if (CoreShaderWord (start, length, "attribute"))
				replacement = "in";
			else if (CoreShaderWord (start, length, "varying"))
			{
				const char *declaration = p;
				while (isspace ((unsigned char)*declaration))
					declaration++;
				replacement = flat && !strncmp (declaration, "vec4 v_color;", 13) ? (vertex ? "flat out" : "flat in") : (vertex ? "out" : "in");
			}
			else if (CoreShaderWord (start, length, "gl_Color"))
			{
				replacement = vertex ? "r1_core_color" : "v_color";
				color_input = true;
			}
			else if (CoreShaderWord (start, length, "texture2DProj"))
			{
				const char *sampler = p;
				while (isspace ((unsigned char)*sampler))
					sampler++;
				if (*sampler == '(')
					sampler++;
				while (isspace ((unsigned char)*sampler))
					sampler++;
				replacement = !strncmp (sampler, "u_sampler1", 10) || !strncmp (sampler, "r1_sampler1", 11) ? "r1_core_textureProj1" : "r1_core_textureProj0";
			}
			else
				for (i = 0; i < sizeof (tokens) / sizeof (tokens[0]); i++)
					if (CoreShaderWord (start, length, tokens[i].from))
					{
						replacement = tokens[i].to;
						break;
					}
			if (vertex && (CoreShaderWord (start, length, "gl_Vertex") || CoreShaderWord (start, length, "ftransform")))
				*generic = true;
			if (!CoreShaderAppend (body, &used, replacement ? replacement : start,
					replacement ? strlen (replacement) : length))
				goto fail;
		}
		else
		{
			p++;
			if (!CoreShaderAppend (body, &used, start, 1))
				goto fail;
		}
	}
	used = 0;
#define CORE_TEXT(s) do { if (!CoreShaderAppend(out, &used, s, strlen(s))) goto fail; } while (0)
	CORE_TEXT (version == 430 ? "#version 430 core\n" : "#version 330 core\n");
	CORE_TEXT ("uniform mat4 r1_core_mvp, r1_core_modelview, r1_core_projection, r1_core_texture[4];\n");
	if (vertex)
	{
		CORE_TEXT ("out float r1_core_point_alpha;\n");
		if (*generic)
		{
			CORE_TEXT ("layout(location=0) in vec4 r1_core_position;\n"
					   "layout(location=1) in vec4 r1_core_color;\n"
					   "layout(location=2) in vec4 r1_core_tc0;\n"
					   "layout(location=3) in vec4 r1_core_tc1;\n"
					   "vec4 r1_core_clip;\n"
					   "vec4 r1_core_transform() { return r1_core_mvp * r1_core_position; }\n"
					   "uniform float r1_core_point_size; uniform vec2 r1_core_point_range;\n"
					   "uniform int r1_core_points; uniform float r1_core_point_fade;\n"
					   "uniform vec3 r1_core_attenuation;\n");
		}
	}
	else
	{
		CORE_TEXT ("layout(location=0) out vec4 r1_core_frag[2];\n"
				   "in float r1_core_point_alpha; uniform int r1_core_points, r1_core_point_smooth;\n"
				   "uniform int r1_core_alpha_enabled, r1_core_alpha_func, r1_core_point_sprite;\n"
				   "uniform ivec2 r1_core_point_replace;\n"
				   "uniform float r1_core_alpha_ref;\n"
				   "vec4 r1_core_textureProj0(sampler2D s, vec4 tc) { return r1_core_point_sprite != 0 && r1_core_point_replace.x != 0 ?"
				   " texture(s, gl_PointCoord) : textureProj(s, tc); }\n"
				   "vec4 r1_core_textureProj1(sampler2D s, vec4 tc) { return r1_core_point_sprite != 0 && r1_core_point_replace.y != 0 ?"
				   " texture(s, gl_PointCoord) : textureProj(s, tc); }\n"
				   "bool r1_core_alpha_pass(float a) { float r = r1_core_alpha_ref; int f = r1_core_alpha_func;"
				   "if (f == 512) return false; if (f == 513) return a < r; if (f == 514) return a == r;"
				   "if (f == 515) return a <= r; if (f == 516) return a > r; if (f == 517) return a != r;"
				   "if (f == 518) return a >= r; return true; }\n");
		if (color_input)
			CORE_TEXT ("in vec4 v_color;\n");
	}
	CORE_TEXT (body);
	if (vertex && *generic)
		CORE_TEXT ("\nvoid main() { r1_core_main(); r1_core_point_alpha = 1.0; gl_PointSize = 1.0;"
				   "if (r1_core_points != 0) { float d = length((r1_core_modelview * r1_core_position).xyz);"
				   "float q = dot(r1_core_attenuation, vec3(1.0, d, d*d));"
				   "float size = clamp(r1_core_point_size * inversesqrt(max(q, 0.000001)),"
				   "r1_core_point_range.x, r1_core_point_range.y);"
				   "float fade = max(r1_core_point_fade, 0.000001);"
				   "r1_core_point_alpha = min(1.0, size * size / (fade * fade));"
				   "gl_PointSize = max(size, fade); } }\n");
	else if (vertex)
		CORE_TEXT ("\nvoid main() { r1_core_main(); r1_core_point_alpha = 1.0; }\n");
	else
		CORE_TEXT ("\nvoid main() { r1_core_frag[1] = vec4(0.0); r1_core_main();"
				   "if (r1_core_points != 0) { float coverage = r1_core_point_alpha;"
				   "if (r1_core_point_smooth != 0) { vec2 d = gl_PointCoord - vec2(0.5); float r = dot(d,d);"
				   "float edge = max(fwidth(r), 0.000001); coverage *= 1.0 - smoothstep(0.25-edge, 0.25+edge, r);"
				   "if (coverage <= 0.0) discard; }"
				   "r1_core_frag[0].a *= coverage; r1_core_frag[1].a *= coverage; }"
				   "if (r1_core_alpha_enabled != 0 && !r1_core_alpha_pass(clamp(r1_core_frag[0].a, 0.0, 1.0))) discard; }\n");
#undef CORE_TEXT
	free (body);
	return out;
fail:
	free (body);
	free (out);
	return NULL;
}

static GLuint APIENTRY CoreCreateShader (GLenum type)
{
	unsigned i;
	GLuint name;
	for (i = 0; i < CORE_SHADER_LIMIT; i++)
		if (!core_shaders[i].name)
			break;
	if (i == CORE_SHADER_LIMIT)
	{
		GL_CoreStateError ("core shader record limit");
		return 0;
	}
	name = core_shader_procs.CreateShader (type);
	if (name)
	{
		memset (&core_shaders[i], 0, sizeof (core_shaders[i]));
		core_shaders[i].name = name;
		core_shaders[i].type = type;
	}
	return name;
}

static void APIENTRY CoreDeleteShader (GLuint name)
{
	core_shader_t *record = CoreShaderRecord (name);
	core_shader_procs.DeleteShader (name);
	/* GL keeps attached deleted shaders alive, including across relinks. */
	if (record)
	{
		if (record->attachments)
			record->deleted = true;
		else
			memset (record, 0, sizeof (*record));
	}
}

static void APIENTRY CoreAttachShader (GLuint program, GLuint shader)
{
	core_shader_t *s = CoreShaderRecord (shader);
	core_shader_procs.AttachShader (program, shader);
	if (s)
		s->attachments++;
}

static void APIENTRY CoreDetachShader (GLuint program, GLuint shader)
{
	core_shader_t *s = CoreShaderRecord (shader);
	core_shader_procs.DetachShader (program, shader);
	if (s && s->attachments)
	{
		s->attachments--;
		if (!s->attachments && s->deleted)
			memset (s, 0, sizeof (*s));
	}
}

static void APIENTRY CoreShaderSource (GLuint name, GLsizei count, const char *const *strings, const GLint *lengths)
{
	core_shader_t *record = CoreShaderRecord (name);
	char *joined = NULL, *translated = NULL;
	const char *rejected = "#version 330 core\n#error Core shader source rejected\n";
	if (record)
	{
		joined = CoreShaderJoin (count, strings, lengths);
		if (joined)
			translated = CoreShaderTranslate (record->type, joined, &record->generic, record->flat);
	}
	if (translated)
	{
		const char *source = translated;
		core_shader_procs.ShaderSource (name, 1, &source, NULL);
	}
	else
	{
		GL_CoreStateError ("invalid or oversized core shader source");
		core_shader_procs.ShaderSource (name, 1, &rejected, NULL);
	}
	free (joined);
	free (translated);
}

static void CoreProgramUniforms (core_program_t *p)
{
#define CORE_UNIFORM(field, text) p->field = core_shader_procs.GetUniformLocation(p->name, text)
	CORE_UNIFORM (mvp, "r1_core_mvp");
	CORE_UNIFORM (modelview, "r1_core_modelview");
	CORE_UNIFORM (projection, "r1_core_projection");
	CORE_UNIFORM (texture, "r1_core_texture[0]");
	CORE_UNIFORM (alpha_enabled, "r1_core_alpha_enabled");
	CORE_UNIFORM (alpha_func, "r1_core_alpha_func");
	CORE_UNIFORM (alpha_ref, "r1_core_alpha_ref");
	CORE_UNIFORM (point_sprite, "r1_core_point_sprite");
	CORE_UNIFORM (point_replace, "r1_core_point_replace");
	CORE_UNIFORM (point_size, "r1_core_point_size");
	CORE_UNIFORM (attenuation, "r1_core_attenuation");
	CORE_UNIFORM (point_range, "r1_core_point_range");
	CORE_UNIFORM (points, "r1_core_points");
	CORE_UNIFORM (point_smooth, "r1_core_point_smooth");
	CORE_UNIFORM (point_fade, "r1_core_point_fade");
	CORE_UNIFORM (sampler0, "r1_sampler0");
	CORE_UNIFORM (sampler1, "r1_sampler1");
	CORE_UNIFORM (enabled, "r1_enabled");
	CORE_UNIFORM (texenv, "r1_texenv");
	CORE_UNIFORM (format, "r1_format");
	CORE_UNIFORM (texenv_scale, "r1_texenv_scale");
	CORE_UNIFORM (showtris_uniform, "r1_showtris");
	p->showtris = false;
#undef CORE_UNIFORM
}

static void APIENTRY CoreLinkProgram (GLuint name)
{
	GLuint attached[16];
	GLsizei count = 0, i;
	GLint status = 0;
	core_program_t *p = CoreProgramRecord (name, true);
	if (!p)
	{
		GL_CoreStateError ("core program record limit");
		return;
	}
	p->generic = false;
	core_shader_procs.GetAttachedShaders (name, 16, &count, attached);
	for (i = 0; i < count; i++)
	{
		core_shader_t *s = CoreShaderRecord (attached[i]);
		if (s && s->type == GL_VERTEX_SHADER)
			p->generic = s->generic;
	}
	core_shader_procs.LinkProgram (name);
	core_shader_procs.GetProgramiv (name, GL_LINK_STATUS, &status);
	if (status)
		CoreProgramUniforms (p);
}

static void APIENTRY CoreDeleteProgram (GLuint name)
{
	GLuint attached[16];
	GLsizei count = 0, i;
	core_program_t *p = CoreProgramRecord (name, false);
	core_shader_procs.GetAttachedShaders (name, 16, &count, attached);
	core_shader_procs.DeleteProgram (name);
	if (p)
		memset (p, 0, sizeof (*p));
	for (i = 0; i < count; i++)
	{
		core_shader_t *s = CoreShaderRecord (attached[i]);
		if (s && s->attachments)
		{
			s->attachments--;
			if (!s->attachments && s->deleted)
				memset (s, 0, sizeof (*s));
		}
	}
}

static void APIENTRY CoreUseProgram (GLuint name)
{
	core_shader_procs.UseProgram (name);
}

void *GL_CoreShaderProc (const char *name)
{
#define CORE_PROXY(suffix, proc) if (!strcmp(name, "gl" suffix)) return (void *)proc
	CORE_PROXY ("CreateShader", CoreCreateShader);
	CORE_PROXY ("DeleteShader", CoreDeleteShader);
	CORE_PROXY ("ShaderSource", CoreShaderSource);
	CORE_PROXY ("AttachShader", CoreAttachShader);
	CORE_PROXY ("DetachShader", CoreDetachShader);
	CORE_PROXY ("LinkProgram", CoreLinkProgram);
	CORE_PROXY ("DeleteProgram", CoreDeleteProgram);
	CORE_PROXY ("UseProgram", CoreUseProgram);
#undef CORE_PROXY
	return NULL;
}

static GLuint CoreCompileDefault (GLenum type, const char *source, qboolean flat)
{
	GLuint shader = CoreCreateShader (type);
	GLint status = 0;
	char log[1024];
	if (!shader)
		return 0;
	CoreShaderRecord (shader)->flat = flat;
	CoreShaderSource (shader, 1, &source, NULL);
	core_shader_procs.CompileShader (shader);
	core_shader_procs.GetShaderiv (shader, GL_COMPILE_STATUS, &status);
	if (status)
		return shader;
	log[0] = 0;
	core_shader_procs.GetShaderInfoLog (shader, sizeof (log) - 1, NULL, log);
	log[sizeof (log) - 1] = 0;
	ri.Con_Printf (PRINT_ALL, "Core shader compile failed: %s\n", log);
	CoreDeleteShader (shader);
	return 0;
}

static GLuint CoreBuildDefault (qboolean flat)
{
	GLuint vertex = 0, fragment = 0, program = 0;
	GLint status = 0;
	char log[1024];
	vertex = CoreCompileDefault (GL_VERTEX_SHADER, core_default_vertex, flat);
	fragment = CoreCompileDefault (GL_FRAGMENT_SHADER, core_default_fragment, flat);
	if (!vertex || !fragment)
		goto fail;
	program = core_shader_procs.CreateProgram ();
	if (!program)
		goto fail;
	CoreAttachShader (program, vertex);
	CoreAttachShader (program, fragment);
	CoreLinkProgram (program);
	core_shader_procs.GetProgramiv (program, GL_LINK_STATUS, &status);
	if (!status)
	{
		log[0] = 0;
		core_shader_procs.GetProgramInfoLog (program, sizeof (log) - 1, NULL, log);
		log[sizeof (log) - 1] = 0;
		ri.Con_Printf (PRINT_ALL, "Core shader link failed: %s\n", log);
		goto fail;
	}
	CoreDeleteShader (vertex);
	CoreDeleteShader (fragment);
	return program;
fail:
	if (vertex)
		CoreDeleteShader (vertex);
	if (fragment)
		CoreDeleteShader (fragment);
	if (program)
		CoreDeleteProgram (program);
	return 0;
}

qboolean GL_CoreShaderInit (void)
{
	if (core_default_program && core_flat_program)
		return true;
#define CORE_PROC(field) do { *(void **)(&core_shader_procs.field) = GL_CoreDriverProc("gl" #field); if (!core_shader_procs.field) goto fail; } while (0)
	CORE_PROC (CreateShader);
	CORE_PROC (DeleteShader);
	CORE_PROC (ShaderSource);
	CORE_PROC (CompileShader);
	CORE_PROC (GetShaderiv);
	CORE_PROC (GetShaderInfoLog);
	CORE_PROC (CreateProgram);
	CORE_PROC (AttachShader);
	CORE_PROC (DetachShader);
	CORE_PROC (GetAttachedShaders);
	CORE_PROC (LinkProgram);
	CORE_PROC (GetProgramiv);
	CORE_PROC (GetProgramInfoLog);
	CORE_PROC (DeleteProgram);
	CORE_PROC (UseProgram);
	CORE_PROC (GetIntegerv);
	CORE_PROC (GetUniformLocation);
	CORE_PROC (Uniform1i);
	CORE_PROC (Uniform1f);
	CORE_PROC (Uniform2f);
	CORE_PROC (Uniform2i);
	CORE_PROC (Uniform3f);
	CORE_PROC (UniformMatrix4fv);
#undef CORE_PROC
	core_default_program = CoreBuildDefault (false);
	if (!core_default_program)
		goto fail;
	core_flat_program = CoreBuildDefault (true);
	if (!core_flat_program)
		goto fail;
	return true;
fail:
	GL_CoreShaderShutdown ();
	return false;
}

void GL_CoreShaderShutdown (void)
{
	GLint current = 0;
	if (core_default_program && core_shader_procs.DeleteProgram)
	{
		core_shader_procs.GetIntegerv (GL_CURRENT_PROGRAM, &current);
		if ((GLuint)current == core_default_program || (GLuint)current == core_flat_program)
			core_shader_procs.UseProgram (0);
		CoreDeleteProgram (core_default_program);
		if (core_flat_program)
			CoreDeleteProgram (core_flat_program);
	}
	core_default_program = core_flat_program = 0;
	core_draw_points = false;
	memset (core_shaders, 0, sizeof (core_shaders));
	memset (core_programs, 0, sizeof (core_programs));
	memset (&core_shader_procs, 0, sizeof (core_shader_procs));
}

qboolean GL_CoreShaderGeneric (void)
{
	GLint current = 0;
	core_program_t *p;
	core_shader_procs.GetIntegerv (GL_CURRENT_PROGRAM, &current);
	if (!current)
		return true;
	p = CoreProgramRecord ((GLuint)current, false);
	return p ? p->generic : false;
}

void GL_CoreShaderPointMode (GLenum mode)
{
	core_draw_points = mode == GL_POINTS;
}

GLuint GL_CoreShaderPrepare (void)
{
	const glcore_state_t *s = GL_CoreState ();
	GLint current = 0;
	core_program_t *p;
	GLfloat mvp[16];
	unsigned row, col, k;
	core_shader_procs.GetIntegerv (GL_CURRENT_PROGRAM, &current);
	p = CoreProgramRecord (current ? (GLuint)current : s->shade_model == GL_FLAT ? core_flat_program :
																				   core_default_program,
		false);
	if (!p)
	{
		GL_CoreStateError ("untracked core draw program");
		return (GLuint)current;
	}
	if (!current)
		core_shader_procs.UseProgram (p->name);
	for (col = 0; col < 4; col++)
		for (row = 0; row < 4; row++)
		{
			mvp[col * 4 + row] = 0;
			for (k = 0; k < 4; k++)
				mvp[col * 4 + row] += s->projection[k * 4 + row] * s->modelview[col * 4 + k];
		}
	core_shader_procs.UniformMatrix4fv (p->mvp, 1, GL_FALSE, mvp);
	core_shader_procs.UniformMatrix4fv (p->modelview, 1, GL_FALSE, s->modelview);
	core_shader_procs.UniformMatrix4fv (p->projection, 1, GL_FALSE, s->projection);
	core_shader_procs.UniformMatrix4fv (p->texture, GL_CORE_TEXTURE_UNITS, GL_FALSE, s->texture[0]);
	core_shader_procs.Uniform1i (p->alpha_enabled, s->alpha_test ? 1 : 0);
	core_shader_procs.Uniform1i (p->alpha_func, (GLint)s->alpha_func);
	core_shader_procs.Uniform1f (p->alpha_ref, s->alpha_ref);
	core_shader_procs.Uniform1i (p->point_sprite, s->point_sprite ? 1 : 0);
	core_shader_procs.Uniform2i (p->point_replace, s->point_replace[0] ? 1 : 0, s->point_replace[1] ? 1 : 0);
	core_shader_procs.Uniform1f (p->point_size, s->point_size);
	core_shader_procs.Uniform2f (p->point_range, s->point_min, s->point_max);
	core_shader_procs.Uniform1i (p->points, core_draw_points ? 1 : 0);
	/* Point sprites use square rasterization even when POINT_SMOOTH is enabled. */
	core_shader_procs.Uniform1i (p->point_smooth, s->point_smooth && !s->point_sprite ? 1 : 0);
	core_shader_procs.Uniform1f (p->point_fade, s->point_fade);
	core_shader_procs.Uniform3f (p->attenuation, s->point_attenuation[0], s->point_attenuation[1], s->point_attenuation[2]);
	if (p->name == core_default_program || p->name == core_flat_program)
	{
		qboolean outline = GL_ShowTrisActive ();
		if (p->showtris != outline)
		{
			core_shader_procs.Uniform1i (p->showtris_uniform, outline ? 1 : 0);
			p->showtris = outline;
		}
		core_shader_procs.Uniform1i (p->sampler0, 0);
		core_shader_procs.Uniform1i (p->sampler1, 1);
		core_shader_procs.Uniform2i (p->enabled, s->texture_enabled[0] ? 1 : 0, s->texture_enabled[1] ? 1 : 0);
		core_shader_procs.Uniform2i (p->texenv, (GLint)s->texenv[0], (GLint)s->texenv[1]);
		core_shader_procs.Uniform2i (p->format,
			GL_CoreTextureFormat (0) == GL_ALPHA ? 3 : GL_CoreTextureFormat (0) == GL_INTENSITY ? 4 :
				GL_CoreTextureFormat (0) == GL_RGB || GL_CoreTextureFormat (0) == GL_LUMINANCE	? 1 :
																								  2,
			GL_CoreTextureFormat (1) == GL_ALPHA ? 3 : GL_CoreTextureFormat (1) == GL_INTENSITY ? 4 :
				GL_CoreTextureFormat (1) == GL_RGB || GL_CoreTextureFormat (1) == GL_LUMINANCE	? 1 :
																								  2);
		core_shader_procs.Uniform2f (p->texenv_scale, s->texenv_scale[0], s->texenv_scale[1]);
	}
	return (GLuint)current;
}

void GL_CoreShaderFinish (GLuint saved_program)
{
	core_shader_procs.UseProgram (saved_program);
}
#endif
