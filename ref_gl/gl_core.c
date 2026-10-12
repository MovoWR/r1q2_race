/*
 * Renderer-private core-profile submission. Existing renderer geometry is
 * consumed through explicit vertex attributes and bounded streaming buffers;
 * no deprecated OpenGL entry point is forwarded to a core driver.
 */
#include "gl_local.h"
#ifdef R1Q2_GL_CORE
#include "gl_core.h"
#include <float.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#ifndef GL_CONTEXT_PROFILE_MASK
#define GL_CONTEXT_PROFILE_MASK 0x9126
#define GL_CONTEXT_CORE_PROFILE_BIT 0x00000001
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#define GL_RG8 0x822B
#define GL_RG 0x8227
#endif
#ifndef GL_TEXTURE_SWIZZLE_RGBA
#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46
#endif

#define CORE_MAX_VERTICES 65536
#define CORE_MAX_INDICES (CORE_MAX_VERTICES * 6)
#define CORE_MAX_UPLOAD (16 * 1024 * 1024)
#define CORE_CLIENT_STACK 16
#define CORE_MAX_LEGACY_TEXTURES 8192
/* Image/glow tables plus lightmaps, effects and per-mesh MD5 texture views. */
#define CORE_MAX_TEXTURE_NAMES (8 * MAX_GLTEXTURES)

typedef struct
{
	const char *name;
	void *address;
} core_saved_proc_t;

/* The generated slot inventory is populated before installing any facade. */
static core_saved_proc_t core_saved_procs[512];
static unsigned core_saved_count;
static qboolean core_active;
static char core_extensions[131072];

#ifdef _WIN32
static PROC (WINAPI *core_resolver) (LPCSTR);
#else
extern void *GLimp_GetProcAddress (const char *name);
#endif

typedef struct
{
	GLint size;
	GLenum type;
	GLsizei stride;
	const GLvoid *pointer;
	GLuint buffer;
	GLboolean enabled;
} core_array_t;

typedef struct
{
	core_array_t vertex, color, texcoord[GL_CORE_TEXTURE_UNITS];
	unsigned texture;
	GLint array_buffer, element_buffer;
} core_client_t;

typedef struct
{
	GLfloat position[4], color[4], texcoord[2][4];
} core_vertex_t;

static core_client_t core_client, core_client_stack[CORE_CLIENT_STACK];
static unsigned core_client_depth;
static core_vertex_t core_vertices[CORE_MAX_VERTICES];
static GLuint core_indices[CORE_MAX_INDICES];
static unsigned core_vertex_count;
static GLenum core_primitive;
static qboolean core_building;
static GLuint core_vao, core_buffers[5];
static GLuint core_default_vao;

typedef struct
{
	GLuint name;
	GLenum format;
} core_texture_t;

static core_texture_t core_textures[CORE_MAX_LEGACY_TEXTURES];

/* The renderer's fixed texture IDs are public names, not core driver objects. */
typedef struct
{
	GLuint name, native;
	GLenum target;
} core_texture_name_t;

static core_texture_name_t core_texture_names[CORE_MAX_TEXTURE_NAMES];
#define CORE_GENERATED_TEXTURE_FIRST 0x10000000u
static GLuint core_next_texture_name = CORE_GENERATED_TEXTURE_FIRST;
static GLuint core_default_texture_name;
static core_texture_t core_default_texture_record;

typedef struct
{
	void (APIENTRY *GenVertexArrays) (GLsizei, GLuint *);
	void (APIENTRY *DeleteVertexArrays) (GLsizei, const GLuint *);
	void (APIENTRY *BindVertexArray) (GLuint);
	void (APIENTRY *GenBuffers) (GLsizei, GLuint *);
	void (APIENTRY *DeleteBuffers) (GLsizei, const GLuint *);
	void (APIENTRY *BindBuffer) (GLenum, GLuint);
	void (APIENTRY *BufferData) (GLenum, GLsizeiptr, const GLvoid *, GLenum);
	void (APIENTRY *GetBufferSubData) (GLenum, GLintptr, GLsizeiptr, GLvoid *);
	void (APIENTRY *GetBufferParameteriv) (GLenum, GLenum, GLint *);
	void (APIENTRY *EnableVertexAttribArray) (GLuint);
	void (APIENTRY *DisableVertexAttribArray) (GLuint);
	void (APIENTRY *VertexAttribPointer) (GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *);
	void (APIENTRY *VertexAttrib4fv) (GLuint, const GLfloat *);
	void (APIENTRY *GetVertexAttribfv) (GLuint, GLenum, GLfloat *);
	void (APIENTRY *ActiveTexture) (GLenum);
	const GLubyte *(APIENTRY *GetStringi) (GLenum, GLuint);
} core_procs_t;

static core_procs_t core_procs;

static void Core_SaveSlots (void);
static void Core_RestoreSlots (void);

qboolean GL_CoreActive (void)
{
	return core_active;
}

void GL_CoreStateError (const char *message)
{
	ri.Sys_Error (ERR_DROP, "OpenGL core backend: %s", message);
}

void *GL_CoreDriverProc (const char *name)
{
	unsigned i;
	void *address;
	for (i = 0; i < core_saved_count; i++)
		if (!strcmp (name, core_saved_procs[i].name) && core_saved_procs[i].address)
			return core_saved_procs[i].address;
#ifdef _WIN32
	address = core_resolver ? (void *)core_resolver (name) : NULL;
	if ((size_t)address <= 3 || (size_t)address == (size_t)-1)
		return NULL;
#else
	address = GLimp_GetProcAddress (name);
#endif
	return address;
}

static void (APIENTRY *Core_DriverDrawElements) (GLenum, GLsizei, GLenum, const GLvoid *);
static void (APIENTRY *Core_DriverDrawArrays) (GLenum, GLint, GLsizei);
static const GLubyte *(APIENTRY *Core_DriverGetString) (GLenum);
static void (APIENTRY *Core_DriverTexImage2D) (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
static void (APIENTRY *Core_DriverTexSubImage2D) (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
static void (APIENTRY *Core_DriverGetTexLevelParameteriv) (GLenum, GLint, GLenum, GLint *);
static void (APIENTRY *Core_DriverDeleteTextures) (GLsizei, const GLuint *);
static void (APIENTRY *Core_DriverGenTextures) (GLsizei, GLuint *);
static void (APIENTRY *Core_DriverBindTexture) (GLenum, GLuint);
static GLboolean (APIENTRY *Core_DriverIsTexture) (GLuint);
static void (APIENTRY *Core_DriverGetIntegerv) (GLenum, GLint *);
static void (APIENTRY *Core_DriverFramebufferTexture2D) (GLenum, GLenum, GLenum, GLuint, GLint);
static void (APIENTRY *Core_DriverGetFramebufferAttachmentParameteriv) (GLenum, GLenum, GLenum, GLint *);
static void (APIENTRY *Core_DriverTexParameteri) (GLenum, GLenum, GLint);
static void (APIENTRY *Core_DriverTexParameterf) (GLenum, GLenum, GLfloat);
static void (APIENTRY *Core_DriverTexParameteriv) (GLenum, GLenum, const GLint *);
static GLenum (APIENTRY *Core_DriverGetError) (void);
static GLenum core_texture_error;

static void Core_TextureNameError (GLenum error)
{
	if (core_texture_error == GL_NO_ERROR)
		core_texture_error = error;
}

static core_texture_name_t *Core_TextureNameRecord (GLuint name, qboolean reserve)
{
	unsigned i;
	core_texture_name_t *free_slot = NULL;
	if (!name)
		return NULL;
	for (i = 0; i < CORE_MAX_TEXTURE_NAMES; i++)
	{
		if (core_texture_names[i].name == name)
			return &core_texture_names[i];
		if (!core_texture_names[i].name && !free_slot)
			free_slot = &core_texture_names[i];
	}
	if (reserve && !free_slot)
		Core_TextureNameError (GL_OUT_OF_MEMORY);
	return reserve ? free_slot : NULL;
}

GLuint GL_CoreTextureName (GLuint native)
{
	unsigned i;
	if (!native || native == core_default_texture_name)
		return 0;
	for (i = 0; i < CORE_MAX_TEXTURE_NAMES; i++)
		if (core_texture_names[i].name && core_texture_names[i].native == native)
			return core_texture_names[i].name;
	return native;
}

qboolean GL_CoreTextureBinding (GLenum pname, GLuint *name)
{
	GLint native = 0;
	switch (pname)
	{
	case GL_TEXTURE_BINDING_1D:
	case GL_TEXTURE_BINDING_2D:
	case 0x806A:
	case 0x8514: /* 3D, cube */
	case 0x8C1C:
	case 0x8C1D:
	case 0x84F6:
	case 0x8C2C: /* arrays, rectangle, buffer */
	case 0x9104:
	case 0x9105: /* multisample, multisample array */
		Core_DriverGetIntegerv (pname, &native);
		*name = GL_CoreTextureName ((GLuint)native);
		return true;
	default:
		return false;
	}
}

static qboolean Core_TextureTarget (GLenum target)
{
	switch (target)
	{
	case GL_TEXTURE_1D:
	case GL_TEXTURE_2D:
	case 0x806F:
	case 0x8513: /* 3D, cube */
	case 0x8C18:
	case 0x8C1A:
	case 0x84F5:
	case 0x8C2A: /* arrays, rectangle, buffer */
	case 0x9100:
	case 0x9102:
		return true; /* multisample, multisample array */
	default:
		return false;
	}
}

static GLuint Core_GenerateNativeTexture (void)
{
	GLuint native = 0;
	GLenum error;
	Core_DriverGenTextures (1, &native);
	if (!native)
	{
		error = Core_DriverGetError ();
		Core_TextureNameError (error != GL_NO_ERROR ? error : GL_OUT_OF_MEMORY);
	}
	return native;
}

static void APIENTRY Core_BindTexture (GLenum target, GLuint name)
{
	core_texture_name_t *record;
	GLuint native;
	if (!Core_TextureTarget (target))
	{
		Core_TextureNameError (GL_INVALID_ENUM);
		return;
	}
	if (!name)
	{
		/* Raw movie frames use compatibility's texture-zero 2D object. */
		if (target != GL_TEXTURE_2D)
		{
			Core_DriverBindTexture (target, 0);
			return;
		}
		native = core_default_texture_name;
		if (!native)
			native = Core_GenerateNativeTexture ();
		if (!native)
			return;
		Core_DriverBindTexture (target, native);
		if (Core_DriverIsTexture (native))
			core_default_texture_name = native;
		else if (!core_default_texture_name)
			Core_DriverDeleteTextures (1, &native);
		return;
	}
	record = Core_TextureNameRecord (name, true);
	if (!record)
		return;
	if (record->name)
	{
		Core_DriverBindTexture (target, record->native);
		if (!record->target && Core_DriverIsTexture (record->native))
			record->target = target;
		return;
	}
	native = Core_GenerateNativeTexture ();
	if (!native)
		return;
	Core_DriverBindTexture (target, native);
	if (!Core_DriverIsTexture (native))
	{
		Core_DriverDeleteTextures (1, &native);
		return;
	}
	record->name = name;
	record->native = native;
	record->target = target;
}

static void APIENTRY Core_GenTextures (GLsizei count, GLuint *textures)
{
	GLsizei i, free_count = 0;
	unsigned j;
	core_texture_name_t *record;
	GLuint name, native;
	if (count < 0)
	{
		Core_TextureNameError (GL_INVALID_VALUE);
		return;
	}
	for (j = 0; j < CORE_MAX_TEXTURE_NAMES; j++)
		if (!core_texture_names[j].name)
			free_count++;
	if (count > free_count)
	{
		Core_TextureNameError (GL_OUT_OF_MEMORY);
		return;
	}
	for (i = 0; i < count; i++)
	{
		do
		{
			if (core_next_texture_name < CORE_GENERATED_TEXTURE_FIRST || core_next_texture_name > INT_MAX)
				core_next_texture_name = CORE_GENERATED_TEXTURE_FIRST;
			name = core_next_texture_name++;
		} while (Core_TextureNameRecord (name, false));
		record = Core_TextureNameRecord (name, true);
		native = Core_GenerateNativeTexture ();
		if (!native)
		{
			/* Roll back only names reserved by this failed generation call. */
			for (j = 0; j < (unsigned)i; j++)
			{
				record = Core_TextureNameRecord (textures[j], false);
				Core_DriverDeleteTextures (1, &record->native);
				memset (record, 0, sizeof (*record));
				textures[j] = 0;
			}
			textures[i] = 0;
			return;
		}
		record->name = name;
		record->native = native;
		record->target = 0;
		textures[i] = name;
	}
}

static GLboolean APIENTRY Core_IsTexture (GLuint name)
{
	core_texture_name_t *record = Core_TextureNameRecord (name, false);
	return record ? Core_DriverIsTexture (record->native) : GL_FALSE;
}

static void APIENTRY Core_FramebufferTexture2D (GLenum target, GLenum attachment,
	GLenum textarget, GLuint texture, GLint level)
{
	core_texture_name_t *record = Core_TextureNameRecord (texture, false);
	if (texture && !record)
	{
		Core_TextureNameError (GL_INVALID_OPERATION);
		return;
	}
	Core_DriverFramebufferTexture2D (target, attachment, textarget, record ? record->native : 0, level);
}

static void APIENTRY Core_GetFramebufferAttachmentParameteriv (GLenum target,
	GLenum attachment, GLenum pname, GLint *out)
{
	GLint type = 0;
	Core_DriverGetFramebufferAttachmentParameteriv (target, attachment, pname, out);
	if (pname == 0x8CD1) /* FRAMEBUFFER_ATTACHMENT_OBJECT_NAME */
	{
		Core_DriverGetFramebufferAttachmentParameteriv (target, attachment, 0x8CD0, &type);
		if (type == GL_TEXTURE)
			*out = (GLint)GL_CoreTextureName ((GLuint)*out);
	}
}

static GLenum APIENTRY Core_GetError (void)
{
	GLenum error = core_texture_error;
	if (error != GL_NO_ERROR)
	{
		core_texture_error = GL_NO_ERROR;
		return error;
	}
	return Core_DriverGetError ();
}

static const GLubyte *APIENTRY Core_GetString (GLenum name)
{
	return name == GL_EXTENSIONS ? (const GLubyte *)core_extensions : Core_DriverGetString (name);
}

static unsigned Core_TypeBytes (GLenum type)
{
	switch (type)
	{
	case GL_UNSIGNED_BYTE:
	case GL_BYTE:
		return 1;
	case GL_UNSIGNED_SHORT:
	case GL_SHORT:
		return 2;
	case GL_UNSIGNED_INT:
	case GL_INT:
	case GL_FLOAT:
		return 4;
	case GL_DOUBLE:
		return 8;
	default:
		return 0;
	}
}

static void Core_ArrayPointer (core_array_t *array, GLint size, GLenum type,
	GLsizei stride, const GLvoid *pointer)
{
	GLint binding = 0;
	unsigned bytes = Core_TypeBytes (type);
	if (size < 1 || size > 4 || !bytes || stride < 0)
	{
		GL_CoreStateError ("invalid vertex array format");
		return;
	}
	qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &binding);
	array->size = size;
	array->type = type;
	array->stride = stride ? stride : (GLsizei)(size * bytes);
	array->pointer = pointer;
	array->buffer = binding;
}

static void APIENTRY Core_VertexPointer (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	Core_ArrayPointer (&core_client.vertex, size, type, stride, pointer);
}

static void APIENTRY Core_ColorPointer (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	Core_ArrayPointer (&core_client.color, size, type, stride, pointer);
}

static void APIENTRY Core_TexCoordPointer (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	unsigned unit = GL_CoreState ()->client_texture;
	if (unit >= GL_CORE_TEXTURE_UNITS)
	{
		GL_CoreStateError ("texture array unit out of range");
		return;
	}
	Core_ArrayPointer (&core_client.texcoord[unit], size, type, stride, pointer);
}

static core_array_t *Core_Array (GLenum name)
{
	switch (name)
	{
	case GL_VERTEX_ARRAY:
		return &core_client.vertex;
	case GL_COLOR_ARRAY:
		return &core_client.color;
	case GL_TEXTURE_COORD_ARRAY:
		return &core_client.texcoord[GL_CoreState ()->client_texture];
	default:
		return NULL;
	}
}

static void APIENTRY Core_EnableClientState (GLenum name)
{
	core_array_t *array = Core_Array (name);
	if (array)
		array->enabled = GL_TRUE;
	else if (name != GL_NORMAL_ARRAY && name != GL_INDEX_ARRAY && name != GL_EDGE_FLAG_ARRAY)
		GL_CoreStateError ("unsupported client array");
}

static void APIENTRY Core_DisableClientState (GLenum name)
{
	core_array_t *array = Core_Array (name);
	if (array)
		array->enabled = GL_FALSE;
}

static void APIENTRY Core_NormalPointer (GLenum type, GLsizei stride, const GLvoid *pointer)
{
	/* Renderer model lighting is computed explicitly; normals are not consumed. */
	(void)type;
	(void)stride;
	(void)pointer;
}

static void APIENTRY Core_PushClientAttrib (GLbitfield mask)
{
	core_client_t *saved;
	if (core_client_depth == CORE_CLIENT_STACK)
	{
		GL_CoreStateError ("client array stack overflow");
		return;
	}
	saved = &core_client_stack[core_client_depth++];
	*saved = core_client;
	saved->texture = GL_CoreState ()->client_texture;
	qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved->array_buffer);
	qglGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &saved->element_buffer);
	(void)mask;
}

static void APIENTRY Core_PopClientAttrib (void)
{
	if (!core_client_depth)
	{
		GL_CoreStateError ("client array stack underflow");
		return;
	}
	core_client = core_client_stack[--core_client_depth];
	qglClientActiveTextureARB (GL_TEXTURE0 + core_client.texture);
	core_procs.BindBuffer (GL_ARRAY_BUFFER, core_client.array_buffer);
	core_procs.BindBuffer (GL_ELEMENT_ARRAY_BUFFER, core_client.element_buffer);
}

static unsigned Core_Triangulate (GLenum mode, unsigned count, GLenum *result)
{
	unsigned i, output = 0;
	if (mode == GL_QUADS)
	{
		if (count / 4 > CORE_MAX_INDICES / 6)
			return 0;
		output = (count / 4) * 6;
		for (i = count / 4; i > 0; i--)
		{
			unsigned in = (i - 1) * 4, out = (i - 1) * 6;
			GLuint a = core_indices[in], b = core_indices[in + 1];
			GLuint c = core_indices[in + 2], d = core_indices[in + 3];
			core_indices[out] = a;
			core_indices[out + 1] = b;
			core_indices[out + 2] = d;
			core_indices[out + 3] = b;
			core_indices[out + 4] = c;
			core_indices[out + 5] = d;
		}
		*result = GL_TRIANGLES;
		return output;
	}
	if (mode == GL_POLYGON)
		mode = GL_TRIANGLE_FAN;
	*result = mode;
	return count;
}

static qboolean Core_UploadBuffer (GLenum target, GLuint buffer, size_t bytes,
	const GLvoid *data, const char **failure)
{
	GLint available = 0;
	if (core_texture_error != GL_NO_ERROR || Core_DriverGetError () != GL_NO_ERROR)
	{
		*failure = "OpenGL error before core buffer upload";
		return false;
	}
	core_procs.BindBuffer (target, buffer);
	core_procs.BufferData (target, bytes, data, GL_STREAM_DRAW);
	/* A failed replacement can retain an old store with the requested size. */
	if (Core_DriverGetError () != GL_NO_ERROR)
	{
		*failure = "OpenGL core buffer upload failed";
		return false;
	}
	core_procs.GetBufferParameteriv (target, GL_BUFFER_SIZE, &available);
	if (Core_DriverGetError () != GL_NO_ERROR)
	{
		*failure = "OpenGL core buffer upload size query failed";
		return false;
	}
	if (available < 0 || (size_t)available != bytes)
	{
		*failure = "OpenGL core buffer upload store size mismatch";
		return false;
	}
	return true;
}

static qboolean Core_BindArray (GLuint attribute, const core_array_t *array,
	unsigned first, unsigned span, const GLfloat current[4], GLuint stream, const char **failure)
{
	size_t offset, bytes;
	unsigned element = array->size * Core_TypeBytes (array->type);
	if (!array->enabled)
	{
		core_procs.DisableVertexAttribArray (attribute);
		core_procs.VertexAttrib4fv (attribute, current);
		return true;
	}
	if (!array->stride || !element || !span)
		return false;
	if ((size_t)(span - 1) > ((size_t)-1 - element) / array->stride)
		return false;
	offset = (size_t)first * array->stride;
	bytes = (size_t)(span - 1) * array->stride + element;
	if (offset / array->stride != first || (!array->buffer && bytes > CORE_MAX_UPLOAD))
		return false;
	if (array->buffer)
	{
		GLint available = 0;
		size_t position = (size_t)array->pointer + offset;
		if (position < offset)
			return false;
		core_procs.BindBuffer (GL_ARRAY_BUFFER, array->buffer);
		core_procs.GetBufferParameteriv (GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &available);
		if (available < 0 || position > (size_t)available || bytes > (size_t)available - position)
			return false;
		core_procs.VertexAttribPointer (attribute, array->size, array->type,
			array == &core_client.color && array->type != GL_FLOAT && array->type != GL_DOUBLE,
			array->stride, (const GLvoid *)position);
	}
	else
	{
		if (!array->pointer)
			return false;
		if (!Core_UploadBuffer (GL_ARRAY_BUFFER, stream, bytes, (const byte *)array->pointer + offset, failure))
			return false;
		core_procs.VertexAttribPointer (attribute, array->size, array->type,
			array == &core_client.color && array->type != GL_FLOAT && array->type != GL_DOUBLE,
			array->stride, NULL);
	}
	core_procs.EnableVertexAttribArray (attribute);
	return true;
}

/* Share only complete, gap-free client records. Every uploaded byte then
 * belongs to an enabled attribute, including the final vertex in the span.
 * Return zero for retained layouts and negative one for an upload failure. */
static int Core_BindInterleavedArrays (unsigned first, unsigned span,
	const glcore_state_t *state, const char **failure)
{
	const core_array_t *arrays[4] = { &core_client.vertex, &core_client.color,
		&core_client.texcoord[0], &core_client.texcoord[1] };
	size_t widths[4] = { 0 }, offsets[4] = { 0 };
	size_t stride = 0, covered = 0, previous, offset, bytes;
	uintptr_t base = UINTPTR_MAX, address;
	unsigned i, j, enabled = 0;

	if (!core_client.vertex.enabled || !span)
		return 0;
	for (i = 0; i < 4; i++)
	{
		const core_array_t *array = arrays[i];
		if (!array->enabled)
			continue;
		if (array->buffer || !array->pointer || array->stride <= 0 ||
			array->size < 1 || array->size > 4)
			return 0;
		widths[i] = array->size * Core_TypeBytes (array->type);
		if (!widths[i] || widths[i] > (size_t)array->stride ||
			(stride && stride != (size_t)array->stride))
			return 0;
		stride = array->stride;
		address = (uintptr_t)array->pointer;
		if (address < base)
			base = address;
		enabled++;
	}
	if (enabled < 2 || span > CORE_MAX_UPLOAD / stride)
		return 0;
	for (i = 0; i < 4; i++)
	{
		if (!widths[i])
			continue;
		offsets[i] = (uintptr_t)arrays[i]->pointer - base;
		if (offsets[i] > stride - widths[i])
			return 0;
	}
	/* At most four extensions cover the record, regardless of attribute order. */
	for (i = 0; i < 4 && covered < stride; i++)
	{
		previous = covered;
		for (j = 0; j < 4; j++)
			if (widths[j] && offsets[j] <= covered && offsets[j] + widths[j] > covered)
				covered = offsets[j] + widths[j];
		if (covered == previous)
			return 0;
	}
	if (covered != stride)
		return 0;
	if (first > (size_t)-1 / stride)
		return -1;
	offset = (size_t)first * stride;
	bytes = (size_t)span * stride;
	if (offset > UINTPTR_MAX - base || bytes > UINTPTR_MAX - base - offset)
		return -1;
	if (!Core_UploadBuffer (GL_ARRAY_BUFFER, core_buffers[0], bytes,
			(const GLvoid *)(base + offset), failure))
		return -1;
	for (i = 0; i < 4; i++)
	{
		const core_array_t *array = arrays[i];
		if (array->enabled)
		{
			core_procs.VertexAttribPointer (i, array->size, array->type,
				i == 1 && array->type != GL_FLOAT && array->type != GL_DOUBLE,
				array->stride, (const GLvoid *)offsets[i]);
			core_procs.EnableVertexAttribArray (i);
		}
		else
		{
			core_procs.DisableVertexAttribArray (i);
			core_procs.VertexAttrib4fv (i, i < 2 ? state->color : state->texcoord[i - 2]);
		}
	}
	return 1;
}

static void Core_SubmitArrays (GLenum mode, unsigned count, qboolean immediate)
{
	GLint saved_vao = 0, saved_array = 0, saved_element = 0;
	GLuint saved_program;
	GLfloat saved_attributes[4][4];
	GLboolean saved_point_size;
	unsigned i, first = UINT_MAX, last = 0, span;
	const glcore_state_t *state = GL_CoreState ();
	const char *failure = "vertex array span exceeds its buffer or core upload budget";
	qboolean valid = true;
	if (!count || count > CORE_MAX_INDICES)
		return;
	for (i = 0; i < count; i++)
	{
		if (core_indices[i] < first)
			first = core_indices[i];
		if (core_indices[i] > last)
			last = core_indices[i];
	}
	if (last == UINT_MAX)
	{
		GL_CoreStateError ("vertex range overflow");
		return;
	}
	span = last - first + 1;
	qglGetIntegerv (GL_VERTEX_ARRAY_BINDING, &saved_vao);
	qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved_array);
	qglGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &saved_element);
	for (i = 0; i < 4; i++)
		core_procs.GetVertexAttribfv (i, GL_CURRENT_VERTEX_ATTRIB, saved_attributes[i]);
	saved_point_size = qglIsEnabled (GL_PROGRAM_POINT_SIZE);
	qglEnable (GL_PROGRAM_POINT_SIZE);
	core_procs.BindVertexArray (core_vao);
	if (immediate)
	{
		valid = Core_UploadBuffer (GL_ARRAY_BUFFER, core_buffers[0],
			core_vertex_count * sizeof (core_vertices[0]), core_vertices, &failure);
		for (i = 0; valid && i < 4; i++)
		{
			size_t offset = i == 0 ? offsetof (core_vertex_t, position) :
				i == 1			   ? offsetof (core_vertex_t, color) :
									 offsetof (core_vertex_t, texcoord) + (i - 2) * 4 * sizeof (GLfloat);
			core_procs.EnableVertexAttribArray (i);
			core_procs.VertexAttribPointer (i, 4, GL_FLOAT, GL_FALSE, sizeof (core_vertex_t), (const GLvoid *)offset);
		}
		first = 0;
	}
	else
	{
		int interleaved = Core_BindInterleavedArrays (first, span, state, &failure);
		if (interleaved)
			valid = interleaved > 0;
		else
			valid = core_client.vertex.enabled &&
				Core_BindArray (0, &core_client.vertex, first, span, state->color, core_buffers[0], &failure) &&
				Core_BindArray (1, &core_client.color, first, span, state->color, core_buffers[1], &failure) &&
				Core_BindArray (2, &core_client.texcoord[0], first, span, state->texcoord[0], core_buffers[2], &failure) &&
				Core_BindArray (3, &core_client.texcoord[1], first, span, state->texcoord[1], core_buffers[3], &failure);
	}
	if (valid)
	{
		for (i = 0; i < count; i++)
			core_indices[i] -= first;
		count = Core_Triangulate (mode, count, &mode);
		if (count)
		{
			valid = Core_UploadBuffer (GL_ELEMENT_ARRAY_BUFFER, core_buffers[4],
				count * sizeof (core_indices[0]), core_indices, &failure);
			if (valid)
			{
				GL_CoreShaderPointMode (mode);
				saved_program = GL_CoreShaderPrepare ();
				Core_DriverDrawElements (mode, count, GL_UNSIGNED_INT, NULL);
				GL_CoreShaderFinish (saved_program);
			}
		}
	}
	core_procs.BindVertexArray (saved_vao);
	core_procs.BindBuffer (GL_ARRAY_BUFFER, saved_array);
	if (saved_vao)
		core_procs.BindBuffer (GL_ELEMENT_ARRAY_BUFFER, saved_element);
	for (i = 0; i < 4; i++)
		core_procs.VertexAttrib4fv (i, saved_attributes[i]);
	if (!saved_point_size)
		qglDisable (GL_PROGRAM_POINT_SIZE);
	if (!valid)
		GL_CoreStateError (failure);
}

static void APIENTRY Core_DrawElements (GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	GLint element = 0, available = 0;
	GLuint saved_program;
	unsigned i, width;
	const byte *input = indices;
	static byte packed_indices[CORE_MAX_INDICES * sizeof (GLuint)];
	if (count <= 0)
		return;
	if (!GL_CoreShaderGeneric ())
	{
		GL_CoreShaderPointMode (mode);
		saved_program = GL_CoreShaderPrepare ();
		Core_DriverDrawElements (mode, count, type, indices);
		GL_CoreShaderFinish (saved_program);
		return;
	}
	width = Core_TypeBytes (type);
	if (count > CORE_MAX_INDICES ||
		(type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT))
	{
		GL_CoreStateError ("invalid core element count/type");
		return;
	}
	qglGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &element);
	if (element)
	{
		core_procs.GetBufferParameteriv (GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &available);
		if (core_texture_error != GL_NO_ERROR || Core_DriverGetError () != GL_NO_ERROR)
		{
			GL_CoreStateError ("OpenGL error before core element buffer readback");
			return;
		}
		if (available < 0 || (size_t)indices > (size_t)available || (size_t)count * width > (size_t)available - (size_t)indices)
		{
			GL_CoreStateError ("element span exceeds buffer");
			return;
		}
		core_procs.GetBufferSubData (GL_ELEMENT_ARRAY_BUFFER, (GLintptr)indices, count * width, packed_indices);
		if (Core_DriverGetError () != GL_NO_ERROR)
		{
			GL_CoreStateError ("OpenGL core element buffer readback failed");
			return;
		}
		input = packed_indices;
	}
	if (!input)
	{
		GL_CoreStateError ("missing core element data");
		return;
	}
	for (i = 0; i < (unsigned)count; i++)
	{
		GLuint index = 0;
		memcpy (&index, input + (size_t)i * width, width);
		core_indices[i] = index;
	}
	Core_SubmitArrays (mode, count, false);
}

static void APIENTRY Core_DrawArrays (GLenum mode, GLint first, GLsizei count)
{
	unsigned i;
	GLuint saved_program;
	if (count <= 0)
		return;
	if (!GL_CoreShaderGeneric ())
	{
		GL_CoreShaderPointMode (mode);
		saved_program = GL_CoreShaderPrepare ();
		Core_DriverDrawArrays (mode, first, count);
		GL_CoreShaderFinish (saved_program);
		return;
	}
	if (first < 0 || count > CORE_MAX_VERTICES || (unsigned)first > UINT_MAX - (unsigned)count)
	{
		GL_CoreStateError ("invalid core array range");
		return;
	}
	for (i = 0; i < (unsigned)count; i++)
		core_indices[i] = first + i;
	Core_SubmitArrays (mode, count, false);
}

static void APIENTRY Core_Begin (GLenum mode)
{
	if (core_building)
	{
		GL_CoreStateError ("nested vertex submission");
		return;
	}
	core_building = true;
	core_primitive = mode;
	core_vertex_count = 0;
}

static void APIENTRY Core_Vertex3f (GLfloat x, GLfloat y, GLfloat z)
{
	core_vertex_t *vertex;
	const glcore_state_t *state = GL_CoreState ();
	if (!core_building || core_vertex_count == CORE_MAX_VERTICES)
	{
		GL_CoreStateError ("vertex submission outside begin/end or over budget");
		return;
	}
	vertex = &core_vertices[core_vertex_count++];
	vertex->position[0] = x;
	vertex->position[1] = y;
	vertex->position[2] = z;
	vertex->position[3] = 1;
	memcpy (vertex->color, state->color, sizeof (vertex->color));
	memcpy (vertex->texcoord, state->texcoord, sizeof (vertex->texcoord));
}

static void APIENTRY Core_Vertex2f (GLfloat x, GLfloat y)
{
	Core_Vertex3f (x, y, 0);
}

static void APIENTRY Core_Vertex3fv (const GLfloat *v)
{
	Core_Vertex3f (v[0], v[1], v[2]);
}

static GLfloat Core_Component (const byte *p, GLenum type, qboolean normalize)
{
	GLfloat result;
#define CORE_READ(TYPE) do { TYPE value; memcpy (&value, p, sizeof(value)); result = (GLfloat)value; } while (0)
	switch (type)
	{
	case GL_FLOAT:
		CORE_READ (GLfloat);
		break;
	case GL_DOUBLE:
		CORE_READ (GLdouble);
		break;
	case GL_UNSIGNED_BYTE:
		CORE_READ (GLubyte);
		if (normalize)
			result /= 255;
		break;
	case GL_BYTE:
		CORE_READ (GLbyte);
		if (normalize)
			result = result < -127 ? -1 : result / 127;
		break;
	case GL_UNSIGNED_SHORT:
		CORE_READ (GLushort);
		if (normalize)
			result /= 65535;
		break;
	case GL_SHORT:
		CORE_READ (GLshort);
		if (normalize)
			result = result < -32767 ? -1 : result / 32767;
		break;
	case GL_UNSIGNED_INT:
		CORE_READ (GLuint);
		if (normalize)
			result /= 4294967295.0f;
		break;
	case GL_INT:
		CORE_READ (GLint);
		if (normalize)
			result = result < -2147483647.0f ? -1 : result / 2147483647.0f;
		break;
	default:
		result = 0;
		break;
	}
#undef CORE_READ
	return result;
}

static qboolean Core_ReadArray (const core_array_t *array, GLint index, GLfloat value[4], qboolean color)
{
	byte data[4 * sizeof (GLdouble)];
	const byte *source;
	unsigned i, width = Core_TypeBytes (array->type);
	size_t offset = (size_t)index * array->stride;
	GLint saved = 0, available = 0;
	GLenum error;
	if (!array->enabled)
		return false;
	if (index < 0 || !width || !array->stride || offset / array->stride != (size_t)index)
	{
		GL_CoreStateError ("invalid array element");
		return false;
	}
	if (array->buffer)
	{
		qglGetIntegerv (GL_ARRAY_BUFFER_BINDING, &saved);
		core_procs.BindBuffer (GL_ARRAY_BUFFER, array->buffer);
		core_procs.GetBufferParameteriv (GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &available);
		if (core_texture_error != GL_NO_ERROR || Core_DriverGetError () != GL_NO_ERROR)
		{
			core_procs.BindBuffer (GL_ARRAY_BUFFER, saved);
			GL_CoreStateError ("OpenGL error before core array element readback");
			return false;
		}
		if ((size_t)array->pointer > (size_t)-1 - offset)
		{
			core_procs.BindBuffer (GL_ARRAY_BUFFER, saved);
			GL_CoreStateError ("array element offset overflow");
			return false;
		}
		offset += (size_t)array->pointer;
		if (available < 0 || offset > (size_t)available || array->size * width > (size_t)available - offset)
		{
			core_procs.BindBuffer (GL_ARRAY_BUFFER, saved);
			GL_CoreStateError ("array element exceeds buffer");
			return false;
		}
		core_procs.GetBufferSubData (GL_ARRAY_BUFFER, offset, array->size * width, data);
		error = Core_DriverGetError ();
		core_procs.BindBuffer (GL_ARRAY_BUFFER, saved);
		if (error != GL_NO_ERROR)
		{
			GL_CoreStateError ("OpenGL core array element readback failed");
			return false;
		}
		source = data;
	}
	else
	{
		if (!array->pointer)
		{
			GL_CoreStateError ("missing array element data");
			return false;
		}
		source = (const byte *)array->pointer + offset;
	}
	value[0] = value[1] = value[2] = 0;
	value[3] = 1;
	for (i = 0; i < (unsigned)array->size; i++)
		value[i] = Core_Component (source + i * width, array->type, color);
	return true;
}

static void APIENTRY Core_ArrayElement (GLint index)
{
	GLfloat value[4][4];
	unsigned i, saved = GL_CoreState ()->active_texture;
	/* Read every enabled array before changing current values or emitting a vertex. */
	if ((core_client.color.enabled && !Core_ReadArray (&core_client.color, index, value[1], true)) ||
		(core_client.texcoord[0].enabled && !Core_ReadArray (&core_client.texcoord[0], index, value[2], false)) ||
		(core_client.texcoord[1].enabled && !Core_ReadArray (&core_client.texcoord[1], index, value[3], false)) ||
		(core_client.vertex.enabled && !Core_ReadArray (&core_client.vertex, index, value[0], false)))
		return;
	if (core_client.color.enabled)
		qglColor4fv (value[1]);
	for (i = 0; i < 2; i++)
		if (core_client.texcoord[i].enabled)
		{
			qglActiveTextureARB (GL_TEXTURE0 + i);
			qglTexCoord3f (value[i + 2][0], value[i + 2][1], value[i + 2][2]);
		}
	qglActiveTextureARB (GL_TEXTURE0 + saved);
	if (core_client.vertex.enabled)
		Core_Vertex3fv (value[0]);
}

static void APIENTRY Core_End (void)
{
	unsigned i;
	if (!core_building)
	{
		GL_CoreStateError ("end without begin");
		return;
	}
	core_building = false;
	for (i = 0; i < core_vertex_count; i++)
		core_indices[i] = i;
	Core_SubmitArrays (core_primitive, core_vertex_count, true);
}

/* Only formats used by the retained renderer need legacy sampling semantics. */
static GLenum Core_LegacyFormat (GLint format)
{
	switch (format)
	{
	case 1:
	case GL_LUMINANCE:
	case GL_LUMINANCE8:
		return GL_LUMINANCE;
	case 2:
	case GL_LUMINANCE_ALPHA:
	case GL_LUMINANCE8_ALPHA8:
		return GL_LUMINANCE_ALPHA;
	case GL_ALPHA:
	case GL_ALPHA8:
		return GL_ALPHA;
	case GL_INTENSITY:
	case GL_INTENSITY8:
		return GL_INTENSITY;
	default:
		return 0;
	}
}

static core_texture_t *Core_TextureRecord (GLenum target, qboolean reserve)
{
	GLint binding = 0;
	unsigned i;
	core_texture_t *free_slot = NULL;
	if (target != GL_TEXTURE_2D)
		return NULL;
	qglGetIntegerv (GL_TEXTURE_BINDING_2D, &binding);
	if (!binding)
		return core_default_texture_name ? &core_default_texture_record : NULL;
	for (i = 0; i < CORE_MAX_LEGACY_TEXTURES; i++)
	{
		if (core_textures[i].name == (GLuint)binding)
			return &core_textures[i];
		if (!core_textures[i].name && !free_slot)
			free_slot = &core_textures[i];
	}
	/* Publish a reserved slot only after the driver accepts the image. */
	if (reserve && free_slot)
		return free_slot;
	if (reserve)
		GL_CoreStateError ("legacy texture metadata budget exceeded");
	return NULL;
}

static void Core_TextureSwizzle (GLenum target, GLenum format)
{
	GLint swizzle[4] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
	if (format == GL_ALPHA)
	{
		swizzle[0] = swizzle[1] = swizzle[2] = GL_ONE;
		swizzle[3] = GL_RED;
	}
	else if (format)
	{
		swizzle[0] = swizzle[1] = swizzle[2] = GL_RED;
		swizzle[3] = format == GL_INTENSITY ? GL_RED : format == GL_LUMINANCE_ALPHA ? GL_GREEN :
																					  GL_ONE;
	}
	Core_DriverTexParameteriv (target, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
}

/* Legacy lightmap uploads are bytes. Honor their original unpack row state. */
static byte *Core_TexturePixels (GLsizei width, GLsizei height, GLenum *format,
	GLenum type, const GLvoid *pixels, GLenum destination, GLint unpack[4])
{
	static const GLenum names[4] = { GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS };
	unsigned components, output = destination == GL_LUMINANCE_ALPHA ? 2 : 1, i;
	int red = 0, alpha = -1;
	size_t row, extent, offset, x, y;
	const byte *source = pixels;
	byte *converted;
	GLint pbo = 0;
	if (!pixels || !destination || width <= 0 || height <= 0)
		return NULL;
	switch (*format)
	{
	case GL_RGBA:
	case GL_BGRA:
		components = 4;
		red = *format == GL_BGRA ? 2 : 0;
		alpha = 3;
		break;
	case GL_RGB:
	case GL_BGR:
		components = 3;
		red = *format == GL_BGR ? 2 : 0;
		break;
	case GL_ALPHA:
		components = 1;
		red = -1;
		alpha = 0;
		break;
	case GL_LUMINANCE_ALPHA:
		components = 2;
		alpha = 1;
		break;
	case GL_LUMINANCE:
	case GL_RED:
		components = 1;
		break;
	default:
		GL_CoreStateError ("unsupported legacy texture input format");
		return NULL;
	}
	if (type != GL_UNSIGNED_BYTE || (size_t)width > CORE_MAX_UPLOAD / output / (size_t)height)
	{
		GL_CoreStateError ("legacy texture conversion exceeds its byte upload budget");
		return NULL;
	}
	for (i = 0; i < 4; i++)
		qglGetIntegerv (names[i], &unpack[i]);
	qglGetIntegerv (GL_PIXEL_UNPACK_BUFFER_BINDING, &pbo);
	if (pbo || (unpack[0] != 1 && unpack[0] != 2 && unpack[0] != 4 && unpack[0] != 8) ||
		unpack[1] < 0 || unpack[2] < 0 || unpack[3] < 0)
	{
		GL_CoreStateError ("invalid legacy texture unpack state");
		return NULL;
	}
	if ((size_t)(unpack[1] ? unpack[1] : width) > CORE_MAX_UPLOAD / components ||
		(size_t)unpack[3] > CORE_MAX_UPLOAD / components)
	{
		GL_CoreStateError ("legacy texture unpack row exceeds budget");
		return NULL;
	}
	row = (size_t)(unpack[1] ? unpack[1] : width) * components;
	row = (row + unpack[0] - 1) & ~(size_t)(unpack[0] - 1);
	if (row && (size_t)unpack[2] > CORE_MAX_UPLOAD / row)
	{
		GL_CoreStateError ("legacy texture unpack offset exceeds budget");
		return NULL;
	}
	if (row && (size_t)(height - 1) > CORE_MAX_UPLOAD / row)
	{
		GL_CoreStateError ("legacy texture unpack height exceeds budget");
		return NULL;
	}
	offset = (size_t)unpack[2] * row + (size_t)unpack[3] * components;
	extent = (size_t)(height - 1) * row + (size_t)width * components;
	if (row > CORE_MAX_UPLOAD || offset > CORE_MAX_UPLOAD || extent > CORE_MAX_UPLOAD - offset)
	{
		GL_CoreStateError ("legacy texture unpack span exceeds budget");
		return NULL;
	}
	converted = malloc ((size_t)width * height * output);
	if (!converted)
	{
		GL_CoreStateError ("legacy texture conversion allocation failed");
		return NULL;
	}
	for (y = 0; y < (size_t)height; y++)
		for (x = 0; x < (size_t)width; x++)
		{
			const byte *in = source + offset + y * row + x * components;
			byte *out = converted + (y * width + x) * output;
			out[0] = destination == GL_ALPHA ? (alpha < 0 ? 255 : in[alpha]) : (red < 0 ? 0 : in[red]);
			if (output == 2)
				out[1] = alpha < 0 ? 255 : in[alpha];
		}
	qglPixelStorei (GL_UNPACK_ALIGNMENT, 1);
	for (i = 1; i < 4; i++)
		qglPixelStorei (names[i], 0);
	*format = output == 2 ? GL_RG : GL_RED;
	return converted;
}

static void Core_TextureUnpackRestore (const GLint unpack[4])
{
	qglPixelStorei (GL_UNPACK_ALIGNMENT, unpack[0]);
	qglPixelStorei (GL_UNPACK_ROW_LENGTH, unpack[1]);
	qglPixelStorei (GL_UNPACK_SKIP_ROWS, unpack[2]);
	qglPixelStorei (GL_UNPACK_SKIP_PIXELS, unpack[3]);
}

static void APIENTRY Core_TexImage2D (GLenum target, GLint level, GLint internal,
	GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	GLenum legacy = Core_LegacyFormat (internal);
	GLenum error;
	core_texture_t *record;
	byte *converted = NULL;
	GLint unpack[4], binding = 0;
	/* Keep an unobserved upload error visible to the renderer's fallback checks. */
	if (core_texture_error != GL_NO_ERROR)
		return;
	record = Core_TextureRecord (target, legacy != 0);
	if (legacy)
	{
		if (!record || border)
		{
			GL_CoreStateError ("invalid legacy core texture target/border");
			return;
		}
		if (!record->name)
			qglGetIntegerv (GL_TEXTURE_BINDING_2D, &binding);
		internal = legacy == GL_LUMINANCE_ALPHA ? GL_RG8 : GL_R8;
		if (pixels && width > 0 && height > 0)
		{
			converted = Core_TexturePixels (width, height, &format, type, pixels, legacy, unpack);
			if (!converted)
				return;
			pixels = converted;
		}
		else
			format = legacy == GL_LUMINANCE_ALPHA ? GL_RG : GL_RED;
	}
	else if (internal == 3)
		internal = GL_RGB;
	else if (internal == 4)
		internal = GL_RGBA;
#ifdef GL_RGB2_EXT
	else if (internal == GL_RGB2_EXT)
		internal = GL_R3_G3_B2;
#endif
	error = Core_DriverGetError ();
	if (error == GL_NO_ERROR)
	{
		Core_DriverTexImage2D (target, level, internal, width, height, border, format, type, pixels);
		error = Core_DriverGetError ();
	}
	if (converted)
	{
		Core_TextureUnpackRestore (unpack);
		free (converted);
	}
	if (error != GL_NO_ERROR)
	{
		core_texture_error = error;
		return;
	}
	if (legacy && !record->name)
		record->name = binding;
	if (target == GL_TEXTURE_2D && level == 0)
	{
		if (record)
			record->format = legacy;
		if (record && !legacy)
			record->name = 0;
		Core_TextureSwizzle (target, legacy);
	}
}

static void APIENTRY Core_TexSubImage2D (GLenum target, GLint level, GLint x, GLint y,
	GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	core_texture_t *record = Core_TextureRecord (target, false);
	byte *converted = NULL;
	GLint unpack[4];
	if (record && record->format && pixels && width > 0 && height > 0)
	{
		converted = Core_TexturePixels (width, height, &format, type, pixels, record->format, unpack);
		if (!converted)
			return;
		pixels = converted;
	}
	Core_DriverTexSubImage2D (target, level, x, y, width, height, format, type, pixels);
	if (converted)
	{
		Core_TextureUnpackRestore (unpack);
		free (converted);
	}
}

static void APIENTRY Core_GetTexLevelParameteriv (GLenum target, GLint level, GLenum pname, GLint *out)
{
	core_texture_t *record;
	Core_DriverGetTexLevelParameteriv (target, level, pname, out);
	if (pname == GL_TEXTURE_INTERNAL_FORMAT && *out && (record = Core_TextureRecord (target, false)) && record->format)
		*out = record->format;
}

static void APIENTRY Core_DeleteTextures (GLsizei count, const GLuint *textures)
{
	GLsizei i;
	unsigned j;
	core_texture_name_t *record;
	qboolean restore_default = false;
	GLint binding = 0;
	if (count < 0)
	{
		Core_TextureNameError (GL_INVALID_VALUE);
		return;
	}
	for (i = 0; i < count; i++)
	{
		record = Core_TextureNameRecord (textures[i], false);
		if (!record)
			continue;
		if (record->target == GL_TEXTURE_2D)
			restore_default = true;
		Core_DriverDeleteTextures (1, &record->native);
		memset (record, 0, sizeof (*record));
		for (j = 0; j < CORE_MAX_LEGACY_TEXTURES; j++)
			if (core_textures[j].name == textures[i])
				memset (&core_textures[j], 0, sizeof (core_textures[j]));
	}
	if (restore_default && core_default_texture_name)
	{
		/* Deleting a bound texture selects the shared legacy default object. */
		for (j = 0; j < GL_CORE_TEXTURE_UNITS; j++)
		{
			core_procs.ActiveTexture (0x84C0 + j);
			Core_DriverGetIntegerv (GL_TEXTURE_BINDING_2D, &binding);
			if (!binding)
				Core_DriverBindTexture (GL_TEXTURE_2D, core_default_texture_name);
		}
		core_procs.ActiveTexture (0x84C0 + GL_CoreState ()->active_texture);
	}
}

static GLint Core_TextureParameter (GLenum pname, GLint value)
{
	/* Retained GL_CLAMP users submit coordinates in [0,1], including beam edges. */
	if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) && value == GL_CLAMP)
		return GL_CLAMP_TO_BORDER;
	return value;
}

static void APIENTRY Core_TexParameteri (GLenum target, GLenum pname, GLint value)
{
	GLint binding = 0;
	if (target == GL_TEXTURE_2D)
	{
		qglGetIntegerv (GL_TEXTURE_BINDING_2D, &binding);
		if (!binding && !core_default_texture_name)
			return;
	}
	Core_DriverTexParameteri (target, pname, Core_TextureParameter (pname, value));
}

static void APIENTRY Core_TexParameterf (GLenum target, GLenum pname, GLfloat value)
{
	GLint binding = 0;
	if (target == GL_TEXTURE_2D)
	{
		qglGetIntegerv (GL_TEXTURE_BINDING_2D, &binding);
		if (!binding && !core_default_texture_name)
			return;
	}
	if (value == GL_CLAMP)
		value = (GLfloat)Core_TextureParameter (pname, GL_CLAMP);
	Core_DriverTexParameterf (target, pname, value);
}

static void APIENTRY Core_BindVertexArray (GLuint name)
{
	core_procs.BindVertexArray (name ? name : core_default_vao);
}

static void APIENTRY Core_LockArrays (GLint first, GLsizei count)
{
	/* Driver pinning is an optional compatibility hint; VBO uploads are explicit. */
	(void)first;
	(void)count;
}

static void APIENTRY Core_UnlockArrays (void) {}

GLenum GL_CoreTextureFormat (unsigned unit)
{
	GLint format = GL_RGBA;
	GLenum result;
	if (unit >= GL_CORE_TEXTURE_UNITS)
		return GL_RGBA;
	core_procs.ActiveTexture (GL_TEXTURE0 + unit);
	qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
	core_procs.ActiveTexture (GL_TEXTURE0 + GL_CoreState ()->active_texture);
	switch (format)
	{
	case GL_RGB:
	case GL_RGB8:
	case GL_RGB4:
	case GL_RGB5:
	case GL_R3_G3_B2:
		result = GL_RGB;
		break;
	case GL_ALPHA:
	case GL_ALPHA8:
		result = GL_ALPHA;
		break;
	case GL_LUMINANCE:
	case GL_LUMINANCE8:
		result = GL_LUMINANCE;
		break;
	case GL_LUMINANCE_ALPHA:
	case GL_LUMINANCE8_ALPHA8:
		result = GL_LUMINANCE_ALPHA;
		break;
	case GL_INTENSITY:
	case GL_INTENSITY8:
		result = GL_INTENSITY;
		break;
	default:
		result = GL_RGBA;
		break;
	}
	return result;
}

void *GL_CoreProcAddress (const char *name)
{
	void *proc;
	if (core_active)
	{
		if (!strcmp (name, "glGetError"))
			return (void *)Core_GetError;
		if (!strcmp (name, "glBindTexture"))
			return (void *)Core_BindTexture;
		if (!strcmp (name, "glGenTextures"))
			return (void *)Core_GenTextures;
		if (!strcmp (name, "glIsTexture"))
			return (void *)Core_IsTexture;
		if (!strcmp (name, "glDeleteTextures"))
			return (void *)Core_DeleteTextures;
		if (!strcmp (name, "glFramebufferTexture2D"))
			return (void *)Core_FramebufferTexture2D;
		if (!strcmp (name, "glGetFramebufferAttachmentParameteriv"))
			return (void *)Core_GetFramebufferAttachmentParameteriv;
		if (!strcmp (name, "glBindVertexArray"))
			return (void *)Core_BindVertexArray;
		if (!strcmp (name, "glLockArraysEXT"))
			return (void *)Core_LockArrays;
		if (!strcmp (name, "glUnlockArraysEXT"))
			return (void *)Core_UnlockArrays;
		proc = GL_CoreShaderProc (name);
		if (proc)
			return proc;
		proc = GL_CoreStateProc (name);
		if (proc)
			return proc;
	}
	return GL_CoreDriverProc (name);
}

#ifdef _WIN32
static PROC WINAPI Core_Resolver (LPCSTR name)
{
	return (PROC)GL_CoreProcAddress (name);
}
#endif

qboolean GL_CoreInit (void)
{
	GLint profile = 0, count = 0;
	unsigned i;
	int major = 0, minor = 0;
	const char *version = (const char *)qglGetString (GL_VERSION);
	if (!version || sscanf (version, "%d.%d", &major, &minor) != 2 || major < 3 || (major == 3 && minor < 2))
		return true;
	qglGetIntegerv (GL_CONTEXT_PROFILE_MASK, &profile);
	if (!(profile & GL_CONTEXT_CORE_PROFILE_BIT))
		return true;
#ifdef _WIN32
	core_resolver = qwglGetProcAddress;
#endif
	Core_SaveSlots ();
#define CORE_PROC(name) do { core_procs.name = (void *)GL_CoreDriverProc ("gl" #name); if (!core_procs.name) goto failed; } while (0)
	CORE_PROC (GenVertexArrays);
	CORE_PROC (DeleteVertexArrays);
	CORE_PROC (BindVertexArray);
	CORE_PROC (GenBuffers);
	CORE_PROC (DeleteBuffers);
	CORE_PROC (BindBuffer);
	CORE_PROC (BufferData);
	CORE_PROC (GetBufferSubData);
	CORE_PROC (GetBufferParameteriv);
	CORE_PROC (EnableVertexAttribArray);
	CORE_PROC (DisableVertexAttribArray);
	CORE_PROC (VertexAttribPointer);
	CORE_PROC (VertexAttrib4fv);
	CORE_PROC (GetVertexAttribfv);
	CORE_PROC (ActiveTexture);
	CORE_PROC (GetStringi);
#undef CORE_PROC
	Core_DriverDrawElements = qglDrawElements;
	Core_DriverDrawArrays = qglDrawArrays;
	Core_DriverGetString = qglGetString;
	Core_DriverTexImage2D = qglTexImage2D;
	Core_DriverTexSubImage2D = qglTexSubImage2D;
	Core_DriverGetTexLevelParameteriv = qglGetTexLevelParameteriv;
	Core_DriverDeleteTextures = qglDeleteTextures;
	Core_DriverGenTextures = qglGenTextures;
	Core_DriverBindTexture = qglBindTexture;
	Core_DriverIsTexture = qglIsTexture;
	Core_DriverGetIntegerv = qglGetIntegerv;
	Core_DriverFramebufferTexture2D = (void *)GL_CoreDriverProc ("glFramebufferTexture2D");
	Core_DriverGetFramebufferAttachmentParameteriv = (void *)GL_CoreDriverProc ("glGetFramebufferAttachmentParameteriv");
	if (!Core_DriverFramebufferTexture2D || !Core_DriverGetFramebufferAttachmentParameteriv)
		goto failed;
	Core_DriverTexParameteri = qglTexParameteri;
	Core_DriverTexParameterf = qglTexParameterf;
	Core_DriverTexParameteriv = qglTexParameteriv;
	Core_DriverGetError = qglGetError;
	core_texture_error = GL_NO_ERROR;
	memset (core_textures, 0, sizeof (core_textures));
	memset (core_texture_names, 0, sizeof (core_texture_names));
	memset (&core_default_texture_record, 0, sizeof (core_default_texture_record));
	core_default_texture_name = 0;
	core_next_texture_name = CORE_GENERATED_TEXTURE_FIRST;
	core_extensions[0] = 0;
	qglGetIntegerv (GL_NUM_EXTENSIONS, &count);
	if (count < 0 || count > 65536)
		goto failed;
	for (i = 0; i < (unsigned)count; i++)
	{
		const char *extension = (const char *)core_procs.GetStringi (GL_EXTENSIONS, i);
		size_t used = strlen (core_extensions), length;
		if (!extension)
			goto failed;
		length = strlen (extension);
		if (length + used + 2 >= sizeof (core_extensions))
			goto failed;
		memcpy (core_extensions + used, extension, length);
		core_extensions[used + length] = ' ';
		core_extensions[used + length + 1] = 0;
	}
	/* These renderer capabilities are provided by core APIs/private state. */
	if (strlen (core_extensions) + 256 >= sizeof (core_extensions))
		goto failed;
	strcat (core_extensions, "GL_ARB_multitexture GL_ARB_vertex_buffer_object GL_ARB_point_sprite GL_EXT_point_parameters GL_EXT_texture_edge_clamp");
	core_procs.GenVertexArrays (1, &core_vao);
	core_procs.GenVertexArrays (1, &core_default_vao);
	core_procs.GenBuffers (5, core_buffers);
	if (!core_vao || !core_default_vao)
		goto failed;
	for (i = 0; i < 5; i++)
		if (!core_buffers[i])
			goto failed;
	core_procs.BindVertexArray (core_default_vao);
	core_active = true;
	GL_CoreStateInstall ();
	if (!GL_CoreStateProc ("glActiveTexture"))
		goto failed;
	qglGetString = Core_GetString;
	qglGetError = Core_GetError;
	qglBindTexture = Core_BindTexture;
	qglGenTextures = Core_GenTextures;
	qglIsTexture = Core_IsTexture;
	qglBegin = Core_Begin;
	qglEnd = Core_End;
	qglVertex2f = Core_Vertex2f;
	qglVertex3f = Core_Vertex3f;
	qglVertex3fv = Core_Vertex3fv;
	qglVertexPointer = Core_VertexPointer;
	qglColorPointer = Core_ColorPointer;
	qglTexCoordPointer = Core_TexCoordPointer;
	qglNormalPointer = Core_NormalPointer;
	qglEnableClientState = Core_EnableClientState;
	qglDisableClientState = Core_DisableClientState;
	qglPushClientAttrib = Core_PushClientAttrib;
	qglPopClientAttrib = Core_PopClientAttrib;
	qglDrawElements = Core_DrawElements;
	qglDrawArrays = Core_DrawArrays;
	qglArrayElement = Core_ArrayElement;
	qglLockArraysEXT = Core_LockArrays;
	qglUnlockArraysEXT = Core_UnlockArrays;
	qglTexImage2D = Core_TexImage2D;
	qglTexSubImage2D = Core_TexSubImage2D;
	qglGetTexLevelParameteriv = Core_GetTexLevelParameteriv;
	qglDeleteTextures = Core_DeleteTextures;
	qglTexParameteri = Core_TexParameteri;
	qglTexParameterf = Core_TexParameterf;
#ifdef _WIN32
	qwglGetProcAddress = Core_Resolver;
#endif
	/* Every legacy 2D unit starts with the shared default texture object. */
	for (i = 0; i < GL_CORE_TEXTURE_UNITS; i++)
	{
		qglActiveTextureARB (0x84C0 + i);
		qglBindTexture (GL_TEXTURE_2D, 0);
	}
	qglActiveTextureARB (0x84C0);
	if (!core_default_texture_name || !GL_CoreShaderInit ())
		goto failed;
	memset (&core_client, 0, sizeof (core_client));
	core_client_depth = core_vertex_count = 0;
	core_building = false;
	ri.Con_Printf (PRINT_ALL, "...OpenGL core renderer initialized (%s)\n", version);
	return true;
failed:
	ri.Con_Printf (PRINT_ALL, "OpenGL core renderer initialization failed; keeping compatibility available.\n");
	GL_CoreShutdown ();
	return false;
}

void GL_CoreShutdown (void)
{
	unsigned i;
	if (core_active)
	{
		GL_CoreShaderShutdown ();
		GL_CoreStateRestore ();
	}
	if (core_procs.DeleteBuffers)
		core_procs.DeleteBuffers (5, core_buffers);
	if (core_vao && core_procs.DeleteVertexArrays)
		core_procs.DeleteVertexArrays (1, &core_vao);
	if (core_default_vao && core_procs.DeleteVertexArrays)
		core_procs.DeleteVertexArrays (1, &core_default_vao);
	if (Core_DriverDeleteTextures)
	{
		for (i = 0; i < CORE_MAX_TEXTURE_NAMES; i++)
			if (core_texture_names[i].name)
				Core_DriverDeleteTextures (1, &core_texture_names[i].native);
		if (core_default_texture_name)
			Core_DriverDeleteTextures (1, &core_default_texture_name);
	}
	if (core_saved_count)
		Core_RestoreSlots ();
#ifdef _WIN32
	if (core_resolver)
		qwglGetProcAddress = core_resolver;
	core_resolver = NULL;
#endif
	core_active = false;
	core_vao = core_default_vao = 0;
	core_saved_count = 0;
	memset (core_buffers, 0, sizeof (core_buffers));
	memset (&core_procs, 0, sizeof (core_procs));
	memset (core_textures, 0, sizeof (core_textures));
	memset (core_texture_names, 0, sizeof (core_texture_names));
	memset (&core_default_texture_record, 0, sizeof (core_default_texture_record));
	core_default_texture_name = 0;
	core_next_texture_name = CORE_GENERATED_TEXTURE_FIRST;
	core_client_depth = core_vertex_count = 0;
	core_building = false;
	core_texture_error = GL_NO_ERROR;
}

static void Core_SaveSlots (void)
{
	core_saved_count = 0;
#define CORE_SAVE(slot) do { core_saved_procs[core_saved_count].name = "gl" #slot; core_saved_procs[core_saved_count++].address = (void *)qgl##slot; } while (0)
	CORE_SAVE (ActiveTextureARB);
	CORE_SAVE (AlphaFunc);
	CORE_SAVE (AreTexturesResident);
	CORE_SAVE (ArrayElement);
	CORE_SAVE (Begin);
	CORE_SAVE (BeginQueryARB);
	CORE_SAVE (BindBuffer);
	CORE_SAVE (BindTexture);
	CORE_SAVE (Bitmap);
	CORE_SAVE (BlendFunc);
	CORE_SAVE (BufferData);
	CORE_SAVE (BufferSubData);
	CORE_SAVE (CallList);
	CORE_SAVE (CallLists);
	CORE_SAVE (Clear);
	CORE_SAVE (ClearAccum);
	CORE_SAVE (ClearColor);
	CORE_SAVE (ClearDepth);
	CORE_SAVE (ClearIndex);
	CORE_SAVE (ClearStencil);
	CORE_SAVE (ClientActiveTextureARB);
	CORE_SAVE (ClipPlane);
	CORE_SAVE (Color3b);
	CORE_SAVE (Color3bv);
	CORE_SAVE (Color3d);
	CORE_SAVE (Color3dv);
	CORE_SAVE (Color3f);
	CORE_SAVE (Color3fv);
	CORE_SAVE (Color3i);
	CORE_SAVE (Color3iv);
	CORE_SAVE (Color3s);
	CORE_SAVE (Color3sv);
	CORE_SAVE (Color3ub);
	CORE_SAVE (Color3ubv);
	CORE_SAVE (Color3ui);
	CORE_SAVE (Color3uiv);
	CORE_SAVE (Color3us);
	CORE_SAVE (Color3usv);
	CORE_SAVE (Color4b);
	CORE_SAVE (Color4bv);
	CORE_SAVE (Color4d);
	CORE_SAVE (Color4dv);
	CORE_SAVE (Color4f);
	CORE_SAVE (Color4fv);
	CORE_SAVE (Color4i);
	CORE_SAVE (Color4iv);
	CORE_SAVE (Color4s);
	CORE_SAVE (Color4sv);
	CORE_SAVE (Color4ub);
	CORE_SAVE (Color4ubv);
	CORE_SAVE (Color4ui);
	CORE_SAVE (Color4uiv);
	CORE_SAVE (Color4us);
	CORE_SAVE (Color4usv);
	CORE_SAVE (ColorMask);
	CORE_SAVE (ColorMaterial);
	CORE_SAVE (ColorPointer);
	CORE_SAVE (ColorTableEXT);
	CORE_SAVE (CopyPixels);
	CORE_SAVE (CopyTexImage1D);
	CORE_SAVE (CopyTexImage2D);
	CORE_SAVE (CopyTexSubImage1D);
	CORE_SAVE (CopyTexSubImage2D);
	CORE_SAVE (CullFace);
	CORE_SAVE (DeleteBuffers);
	CORE_SAVE (DeleteLists);
	CORE_SAVE (DeleteQueriesARB);
	CORE_SAVE (DeleteTextures);
	CORE_SAVE (DepthFunc);
	CORE_SAVE (DepthMask);
	CORE_SAVE (DepthRange);
	CORE_SAVE (Disable);
	CORE_SAVE (DisableClientState);
	CORE_SAVE (DrawArrays);
	CORE_SAVE (DrawBuffer);
	CORE_SAVE (DrawElements);
	CORE_SAVE (DrawPixels);
	CORE_SAVE (Enable);
	CORE_SAVE (EnableClientState);
	CORE_SAVE (End);
	CORE_SAVE (EndList);
	CORE_SAVE (EndQueryARB);
	CORE_SAVE (FeedbackBuffer);
	CORE_SAVE (Finish);
	CORE_SAVE (Flush);
	CORE_SAVE (Fogf);
	CORE_SAVE (Fogfv);
	CORE_SAVE (Fogi);
	CORE_SAVE (Fogiv);
	CORE_SAVE (FrontFace);
	CORE_SAVE (Frustum);
	CORE_SAVE (GenBuffers);
	CORE_SAVE (GenLists);
	CORE_SAVE (GenQueriesARB);
	CORE_SAVE (GenTextures);
	CORE_SAVE (GetBooleanv);
	CORE_SAVE (GetBufferParameteriv);
	CORE_SAVE (GetClipPlane);
	CORE_SAVE (GetDoublev);
	CORE_SAVE (GetError);
	CORE_SAVE (GetFloatv);
	CORE_SAVE (GetIntegerv);
	CORE_SAVE (GetLightfv);
	CORE_SAVE (GetLightiv);
	CORE_SAVE (GetMapdv);
	CORE_SAVE (GetMapfv);
	CORE_SAVE (GetMapiv);
	CORE_SAVE (GetMaterialfv);
	CORE_SAVE (GetMaterialiv);
	CORE_SAVE (GetPixelMapfv);
	CORE_SAVE (GetPixelMapuiv);
	CORE_SAVE (GetPixelMapusv);
	CORE_SAVE (GetPointerv);
	CORE_SAVE (GetPolygonStipple);
	CORE_SAVE (GetQueryObjectivARB);
	CORE_SAVE (GetQueryObjectuivARB);
	CORE_SAVE (GetQueryivARB);
	CORE_SAVE (GetString);
	CORE_SAVE (GetTexEnvfv);
	CORE_SAVE (GetTexEnviv);
	CORE_SAVE (GetTexGendv);
	CORE_SAVE (GetTexGenfv);
	CORE_SAVE (GetTexGeniv);
	CORE_SAVE (GetTexImage);
	CORE_SAVE (GetTexLevelParameteriv);
	CORE_SAVE (GetTexParameterfv);
	CORE_SAVE (GetTexParameteriv);
	CORE_SAVE (Hint);
	CORE_SAVE (IndexMask);
	CORE_SAVE (IndexPointer);
	CORE_SAVE (Indexd);
	CORE_SAVE (Indexdv);
	CORE_SAVE (Indexf);
	CORE_SAVE (Indexfv);
	CORE_SAVE (Indexi);
	CORE_SAVE (Indexiv);
	CORE_SAVE (Indexs);
	CORE_SAVE (Indexsv);
	CORE_SAVE (Indexub);
	CORE_SAVE (Indexubv);
	CORE_SAVE (InitNames);
	CORE_SAVE (InterleavedArrays);
	CORE_SAVE (IsEnabled);
	CORE_SAVE (IsList);
	CORE_SAVE (IsQueryARB);
	CORE_SAVE (IsTexture);
	CORE_SAVE (LightModelf);
	CORE_SAVE (LightModelfv);
	CORE_SAVE (LightModeli);
	CORE_SAVE (LightModeliv);
	CORE_SAVE (Lightf);
	CORE_SAVE (Lightfv);
	CORE_SAVE (Lighti);
	CORE_SAVE (Lightiv);
	CORE_SAVE (LineStipple);
	CORE_SAVE (LineWidth);
	CORE_SAVE (ListBase);
	CORE_SAVE (LoadIdentity);
	CORE_SAVE (LoadMatrixd);
	CORE_SAVE (LoadMatrixf);
	CORE_SAVE (LoadName);
	CORE_SAVE (LockArraysEXT);
	CORE_SAVE (LogicOp);
	CORE_SAVE (MTexCoord2fSGIS);
	CORE_SAVE (MTexCoord2fvSGIS);
	CORE_SAVE (MatrixMode);
	CORE_SAVE (MultMatrixd);
	CORE_SAVE (MultMatrixf);
	CORE_SAVE (NewList);
	CORE_SAVE (NormalPointer);
	CORE_SAVE (Ortho);
	CORE_SAVE (PassThrough);
	CORE_SAVE (PixelMapfv);
	CORE_SAVE (PixelMapuiv);
	CORE_SAVE (PixelMapusv);
	CORE_SAVE (PixelStoref);
	CORE_SAVE (PixelStorei);
	CORE_SAVE (PixelTransferf);
	CORE_SAVE (PixelTransferi);
	CORE_SAVE (PixelZoom);
	CORE_SAVE (PointParameterfARB);
	CORE_SAVE (PointParameterfEXT);
	CORE_SAVE (PointParameterfvARB);
	CORE_SAVE (PointParameterfvEXT);
	CORE_SAVE (PointSize);
	CORE_SAVE (PolygonMode);
	CORE_SAVE (PolygonOffset);
	CORE_SAVE (PolygonStipple);
	CORE_SAVE (PopAttrib);
	CORE_SAVE (PopClientAttrib);
	CORE_SAVE (PopMatrix);
	CORE_SAVE (PopName);
	CORE_SAVE (PrioritizeTextures);
	CORE_SAVE (PushAttrib);
	CORE_SAVE (PushClientAttrib);
	CORE_SAVE (PushMatrix);
	CORE_SAVE (PushName);
	CORE_SAVE (ReadBuffer);
	CORE_SAVE (ReadPixels);
	CORE_SAVE (Rectd);
	CORE_SAVE (Rectdv);
	CORE_SAVE (Rectf);
	CORE_SAVE (Rectfv);
	CORE_SAVE (Recti);
	CORE_SAVE (Rectiv);
	CORE_SAVE (Rects);
	CORE_SAVE (Rectsv);
	CORE_SAVE (RenderMode);
	CORE_SAVE (Rotated);
	CORE_SAVE (Rotatef);
	CORE_SAVE (Scaled);
	CORE_SAVE (Scalef);
	CORE_SAVE (Scissor);
	CORE_SAVE (SelectBuffer);
	CORE_SAVE (SelectTextureSGIS);
	CORE_SAVE (ShadeModel);
	CORE_SAVE (StencilFunc);
	CORE_SAVE (StencilMask);
	CORE_SAVE (StencilOp);
	CORE_SAVE (TexCoord1d);
	CORE_SAVE (TexCoord1dv);
	CORE_SAVE (TexCoord1f);
	CORE_SAVE (TexCoord1fv);
	CORE_SAVE (TexCoord1i);
	CORE_SAVE (TexCoord1iv);
	CORE_SAVE (TexCoord1s);
	CORE_SAVE (TexCoord1sv);
	CORE_SAVE (TexCoord2d);
	CORE_SAVE (TexCoord2dv);
	CORE_SAVE (TexCoord2f);
	CORE_SAVE (TexCoord2fv);
	CORE_SAVE (TexCoord2i);
	CORE_SAVE (TexCoord2iv);
	CORE_SAVE (TexCoord2s);
	CORE_SAVE (TexCoord2sv);
	CORE_SAVE (TexCoord3d);
	CORE_SAVE (TexCoord3dv);
	CORE_SAVE (TexCoord3f);
	CORE_SAVE (TexCoord3fv);
	CORE_SAVE (TexCoord3i);
	CORE_SAVE (TexCoord3iv);
	CORE_SAVE (TexCoord3s);
	CORE_SAVE (TexCoord3sv);
	CORE_SAVE (TexCoord4d);
	CORE_SAVE (TexCoord4dv);
	CORE_SAVE (TexCoord4f);
	CORE_SAVE (TexCoord4fv);
	CORE_SAVE (TexCoord4i);
	CORE_SAVE (TexCoord4iv);
	CORE_SAVE (TexCoord4s);
	CORE_SAVE (TexCoord4sv);
	CORE_SAVE (TexCoordPointer);
	CORE_SAVE (TexEnvf);
	CORE_SAVE (TexEnvfv);
	CORE_SAVE (TexEnvi);
	CORE_SAVE (TexEnviv);
	CORE_SAVE (TexGend);
	CORE_SAVE (TexGendv);
	CORE_SAVE (TexGenf);
	CORE_SAVE (TexGenfv);
	CORE_SAVE (TexGeni);
	CORE_SAVE (TexGeniv);
	CORE_SAVE (TexImage1D);
	CORE_SAVE (TexImage2D);
	CORE_SAVE (TexParameterf);
	CORE_SAVE (TexParameterfv);
	CORE_SAVE (TexParameteri);
	CORE_SAVE (TexParameteriv);
	CORE_SAVE (TexSubImage1D);
	CORE_SAVE (TexSubImage2D);
	CORE_SAVE (Translated);
	CORE_SAVE (Translatef);
	CORE_SAVE (UnlockArraysEXT);
	CORE_SAVE (Vertex2d);
	CORE_SAVE (Vertex2dv);
	CORE_SAVE (Vertex2f);
	CORE_SAVE (Vertex2fv);
	CORE_SAVE (Vertex2i);
	CORE_SAVE (Vertex2iv);
	CORE_SAVE (Vertex2s);
	CORE_SAVE (Vertex2sv);
	CORE_SAVE (Vertex3d);
	CORE_SAVE (Vertex3dv);
	CORE_SAVE (Vertex3f);
	CORE_SAVE (Vertex3fv);
	CORE_SAVE (Vertex3i);
	CORE_SAVE (Vertex3iv);
	CORE_SAVE (Vertex3s);
	CORE_SAVE (Vertex3sv);
	CORE_SAVE (Vertex4d);
	CORE_SAVE (Vertex4dv);
	CORE_SAVE (Vertex4f);
	CORE_SAVE (Vertex4fv);
	CORE_SAVE (Vertex4i);
	CORE_SAVE (Vertex4iv);
	CORE_SAVE (Vertex4s);
	CORE_SAVE (Vertex4sv);
	CORE_SAVE (VertexPointer);
	CORE_SAVE (Viewport);
#undef CORE_SAVE
}

static void Core_RestoreSlots (void)
{
	unsigned slot = 0;
#define CORE_RESTORE(name) qgl##name = (void *)core_saved_procs[slot++].address
	CORE_RESTORE (ActiveTextureARB);
	CORE_RESTORE (AlphaFunc);
	CORE_RESTORE (AreTexturesResident);
	CORE_RESTORE (ArrayElement);
	CORE_RESTORE (Begin);
	CORE_RESTORE (BeginQueryARB);
	CORE_RESTORE (BindBuffer);
	CORE_RESTORE (BindTexture);
	CORE_RESTORE (Bitmap);
	CORE_RESTORE (BlendFunc);
	CORE_RESTORE (BufferData);
	CORE_RESTORE (BufferSubData);
	CORE_RESTORE (CallList);
	CORE_RESTORE (CallLists);
	CORE_RESTORE (Clear);
	CORE_RESTORE (ClearAccum);
	CORE_RESTORE (ClearColor);
	CORE_RESTORE (ClearDepth);
	CORE_RESTORE (ClearIndex);
	CORE_RESTORE (ClearStencil);
	CORE_RESTORE (ClientActiveTextureARB);
	CORE_RESTORE (ClipPlane);
	CORE_RESTORE (Color3b);
	CORE_RESTORE (Color3bv);
	CORE_RESTORE (Color3d);
	CORE_RESTORE (Color3dv);
	CORE_RESTORE (Color3f);
	CORE_RESTORE (Color3fv);
	CORE_RESTORE (Color3i);
	CORE_RESTORE (Color3iv);
	CORE_RESTORE (Color3s);
	CORE_RESTORE (Color3sv);
	CORE_RESTORE (Color3ub);
	CORE_RESTORE (Color3ubv);
	CORE_RESTORE (Color3ui);
	CORE_RESTORE (Color3uiv);
	CORE_RESTORE (Color3us);
	CORE_RESTORE (Color3usv);
	CORE_RESTORE (Color4b);
	CORE_RESTORE (Color4bv);
	CORE_RESTORE (Color4d);
	CORE_RESTORE (Color4dv);
	CORE_RESTORE (Color4f);
	CORE_RESTORE (Color4fv);
	CORE_RESTORE (Color4i);
	CORE_RESTORE (Color4iv);
	CORE_RESTORE (Color4s);
	CORE_RESTORE (Color4sv);
	CORE_RESTORE (Color4ub);
	CORE_RESTORE (Color4ubv);
	CORE_RESTORE (Color4ui);
	CORE_RESTORE (Color4uiv);
	CORE_RESTORE (Color4us);
	CORE_RESTORE (Color4usv);
	CORE_RESTORE (ColorMask);
	CORE_RESTORE (ColorMaterial);
	CORE_RESTORE (ColorPointer);
	CORE_RESTORE (ColorTableEXT);
	CORE_RESTORE (CopyPixels);
	CORE_RESTORE (CopyTexImage1D);
	CORE_RESTORE (CopyTexImage2D);
	CORE_RESTORE (CopyTexSubImage1D);
	CORE_RESTORE (CopyTexSubImage2D);
	CORE_RESTORE (CullFace);
	CORE_RESTORE (DeleteBuffers);
	CORE_RESTORE (DeleteLists);
	CORE_RESTORE (DeleteQueriesARB);
	CORE_RESTORE (DeleteTextures);
	CORE_RESTORE (DepthFunc);
	CORE_RESTORE (DepthMask);
	CORE_RESTORE (DepthRange);
	CORE_RESTORE (Disable);
	CORE_RESTORE (DisableClientState);
	CORE_RESTORE (DrawArrays);
	CORE_RESTORE (DrawBuffer);
	CORE_RESTORE (DrawElements);
	CORE_RESTORE (DrawPixels);
	CORE_RESTORE (Enable);
	CORE_RESTORE (EnableClientState);
	CORE_RESTORE (End);
	CORE_RESTORE (EndList);
	CORE_RESTORE (EndQueryARB);
	CORE_RESTORE (FeedbackBuffer);
	CORE_RESTORE (Finish);
	CORE_RESTORE (Flush);
	CORE_RESTORE (Fogf);
	CORE_RESTORE (Fogfv);
	CORE_RESTORE (Fogi);
	CORE_RESTORE (Fogiv);
	CORE_RESTORE (FrontFace);
	CORE_RESTORE (Frustum);
	CORE_RESTORE (GenBuffers);
	CORE_RESTORE (GenLists);
	CORE_RESTORE (GenQueriesARB);
	CORE_RESTORE (GenTextures);
	CORE_RESTORE (GetBooleanv);
	CORE_RESTORE (GetBufferParameteriv);
	CORE_RESTORE (GetClipPlane);
	CORE_RESTORE (GetDoublev);
	CORE_RESTORE (GetError);
	CORE_RESTORE (GetFloatv);
	CORE_RESTORE (GetIntegerv);
	CORE_RESTORE (GetLightfv);
	CORE_RESTORE (GetLightiv);
	CORE_RESTORE (GetMapdv);
	CORE_RESTORE (GetMapfv);
	CORE_RESTORE (GetMapiv);
	CORE_RESTORE (GetMaterialfv);
	CORE_RESTORE (GetMaterialiv);
	CORE_RESTORE (GetPixelMapfv);
	CORE_RESTORE (GetPixelMapuiv);
	CORE_RESTORE (GetPixelMapusv);
	CORE_RESTORE (GetPointerv);
	CORE_RESTORE (GetPolygonStipple);
	CORE_RESTORE (GetQueryObjectivARB);
	CORE_RESTORE (GetQueryObjectuivARB);
	CORE_RESTORE (GetQueryivARB);
	CORE_RESTORE (GetString);
	CORE_RESTORE (GetTexEnvfv);
	CORE_RESTORE (GetTexEnviv);
	CORE_RESTORE (GetTexGendv);
	CORE_RESTORE (GetTexGenfv);
	CORE_RESTORE (GetTexGeniv);
	CORE_RESTORE (GetTexImage);
	CORE_RESTORE (GetTexLevelParameteriv);
	CORE_RESTORE (GetTexParameterfv);
	CORE_RESTORE (GetTexParameteriv);
	CORE_RESTORE (Hint);
	CORE_RESTORE (IndexMask);
	CORE_RESTORE (IndexPointer);
	CORE_RESTORE (Indexd);
	CORE_RESTORE (Indexdv);
	CORE_RESTORE (Indexf);
	CORE_RESTORE (Indexfv);
	CORE_RESTORE (Indexi);
	CORE_RESTORE (Indexiv);
	CORE_RESTORE (Indexs);
	CORE_RESTORE (Indexsv);
	CORE_RESTORE (Indexub);
	CORE_RESTORE (Indexubv);
	CORE_RESTORE (InitNames);
	CORE_RESTORE (InterleavedArrays);
	CORE_RESTORE (IsEnabled);
	CORE_RESTORE (IsList);
	CORE_RESTORE (IsQueryARB);
	CORE_RESTORE (IsTexture);
	CORE_RESTORE (LightModelf);
	CORE_RESTORE (LightModelfv);
	CORE_RESTORE (LightModeli);
	CORE_RESTORE (LightModeliv);
	CORE_RESTORE (Lightf);
	CORE_RESTORE (Lightfv);
	CORE_RESTORE (Lighti);
	CORE_RESTORE (Lightiv);
	CORE_RESTORE (LineStipple);
	CORE_RESTORE (LineWidth);
	CORE_RESTORE (ListBase);
	CORE_RESTORE (LoadIdentity);
	CORE_RESTORE (LoadMatrixd);
	CORE_RESTORE (LoadMatrixf);
	CORE_RESTORE (LoadName);
	CORE_RESTORE (LockArraysEXT);
	CORE_RESTORE (LogicOp);
	CORE_RESTORE (MTexCoord2fSGIS);
	CORE_RESTORE (MTexCoord2fvSGIS);
	CORE_RESTORE (MatrixMode);
	CORE_RESTORE (MultMatrixd);
	CORE_RESTORE (MultMatrixf);
	CORE_RESTORE (NewList);
	CORE_RESTORE (NormalPointer);
	CORE_RESTORE (Ortho);
	CORE_RESTORE (PassThrough);
	CORE_RESTORE (PixelMapfv);
	CORE_RESTORE (PixelMapuiv);
	CORE_RESTORE (PixelMapusv);
	CORE_RESTORE (PixelStoref);
	CORE_RESTORE (PixelStorei);
	CORE_RESTORE (PixelTransferf);
	CORE_RESTORE (PixelTransferi);
	CORE_RESTORE (PixelZoom);
	CORE_RESTORE (PointParameterfARB);
	CORE_RESTORE (PointParameterfEXT);
	CORE_RESTORE (PointParameterfvARB);
	CORE_RESTORE (PointParameterfvEXT);
	CORE_RESTORE (PointSize);
	CORE_RESTORE (PolygonMode);
	CORE_RESTORE (PolygonOffset);
	CORE_RESTORE (PolygonStipple);
	CORE_RESTORE (PopAttrib);
	CORE_RESTORE (PopClientAttrib);
	CORE_RESTORE (PopMatrix);
	CORE_RESTORE (PopName);
	CORE_RESTORE (PrioritizeTextures);
	CORE_RESTORE (PushAttrib);
	CORE_RESTORE (PushClientAttrib);
	CORE_RESTORE (PushMatrix);
	CORE_RESTORE (PushName);
	CORE_RESTORE (ReadBuffer);
	CORE_RESTORE (ReadPixels);
	CORE_RESTORE (Rectd);
	CORE_RESTORE (Rectdv);
	CORE_RESTORE (Rectf);
	CORE_RESTORE (Rectfv);
	CORE_RESTORE (Recti);
	CORE_RESTORE (Rectiv);
	CORE_RESTORE (Rects);
	CORE_RESTORE (Rectsv);
	CORE_RESTORE (RenderMode);
	CORE_RESTORE (Rotated);
	CORE_RESTORE (Rotatef);
	CORE_RESTORE (Scaled);
	CORE_RESTORE (Scalef);
	CORE_RESTORE (Scissor);
	CORE_RESTORE (SelectBuffer);
	CORE_RESTORE (SelectTextureSGIS);
	CORE_RESTORE (ShadeModel);
	CORE_RESTORE (StencilFunc);
	CORE_RESTORE (StencilMask);
	CORE_RESTORE (StencilOp);
	CORE_RESTORE (TexCoord1d);
	CORE_RESTORE (TexCoord1dv);
	CORE_RESTORE (TexCoord1f);
	CORE_RESTORE (TexCoord1fv);
	CORE_RESTORE (TexCoord1i);
	CORE_RESTORE (TexCoord1iv);
	CORE_RESTORE (TexCoord1s);
	CORE_RESTORE (TexCoord1sv);
	CORE_RESTORE (TexCoord2d);
	CORE_RESTORE (TexCoord2dv);
	CORE_RESTORE (TexCoord2f);
	CORE_RESTORE (TexCoord2fv);
	CORE_RESTORE (TexCoord2i);
	CORE_RESTORE (TexCoord2iv);
	CORE_RESTORE (TexCoord2s);
	CORE_RESTORE (TexCoord2sv);
	CORE_RESTORE (TexCoord3d);
	CORE_RESTORE (TexCoord3dv);
	CORE_RESTORE (TexCoord3f);
	CORE_RESTORE (TexCoord3fv);
	CORE_RESTORE (TexCoord3i);
	CORE_RESTORE (TexCoord3iv);
	CORE_RESTORE (TexCoord3s);
	CORE_RESTORE (TexCoord3sv);
	CORE_RESTORE (TexCoord4d);
	CORE_RESTORE (TexCoord4dv);
	CORE_RESTORE (TexCoord4f);
	CORE_RESTORE (TexCoord4fv);
	CORE_RESTORE (TexCoord4i);
	CORE_RESTORE (TexCoord4iv);
	CORE_RESTORE (TexCoord4s);
	CORE_RESTORE (TexCoord4sv);
	CORE_RESTORE (TexCoordPointer);
	CORE_RESTORE (TexEnvf);
	CORE_RESTORE (TexEnvfv);
	CORE_RESTORE (TexEnvi);
	CORE_RESTORE (TexEnviv);
	CORE_RESTORE (TexGend);
	CORE_RESTORE (TexGendv);
	CORE_RESTORE (TexGenf);
	CORE_RESTORE (TexGenfv);
	CORE_RESTORE (TexGeni);
	CORE_RESTORE (TexGeniv);
	CORE_RESTORE (TexImage1D);
	CORE_RESTORE (TexImage2D);
	CORE_RESTORE (TexParameterf);
	CORE_RESTORE (TexParameterfv);
	CORE_RESTORE (TexParameteri);
	CORE_RESTORE (TexParameteriv);
	CORE_RESTORE (TexSubImage1D);
	CORE_RESTORE (TexSubImage2D);
	CORE_RESTORE (Translated);
	CORE_RESTORE (Translatef);
	CORE_RESTORE (UnlockArraysEXT);
	CORE_RESTORE (Vertex2d);
	CORE_RESTORE (Vertex2dv);
	CORE_RESTORE (Vertex2f);
	CORE_RESTORE (Vertex2fv);
	CORE_RESTORE (Vertex2i);
	CORE_RESTORE (Vertex2iv);
	CORE_RESTORE (Vertex2s);
	CORE_RESTORE (Vertex2sv);
	CORE_RESTORE (Vertex3d);
	CORE_RESTORE (Vertex3dv);
	CORE_RESTORE (Vertex3f);
	CORE_RESTORE (Vertex3fv);
	CORE_RESTORE (Vertex3i);
	CORE_RESTORE (Vertex3iv);
	CORE_RESTORE (Vertex3s);
	CORE_RESTORE (Vertex3sv);
	CORE_RESTORE (Vertex4d);
	CORE_RESTORE (Vertex4dv);
	CORE_RESTORE (Vertex4f);
	CORE_RESTORE (Vertex4fv);
	CORE_RESTORE (Vertex4i);
	CORE_RESTORE (Vertex4iv);
	CORE_RESTORE (Vertex4s);
	CORE_RESTORE (Vertex4sv);
	CORE_RESTORE (VertexPointer);
	CORE_RESTORE (Viewport);
#undef CORE_RESTORE
}

#endif
