/* Private fixed-function state for the optional desktop core renderer. */
#include "gl_local.h"
#include "gl_core.h"

#ifdef R1Q2_GL_CORE
#include <float.h>

#define CORE_MATRIX_DEPTH 32
#define CORE_ATTRIB_DEPTH 16
#define CORE_TEXTURE0 0x84C0
#define CORE_ACTIVE_TEXTURE 0x84E0
#define CORE_CLIENT_ACTIVE_TEXTURE 0x84E1
#define CORE_MAX_TEXTURE_UNITS 0x84E2
#define CORE_POINT_SPRITE 0x8861
#define CORE_COORD_REPLACE 0x8862
#define CORE_TEXTURE_BINDING_3D 0x806A
#define CORE_TEXTURE_BINDING_CUBE 0x8514
#define CORE_TEXTURE_3D 0x806F
#define CORE_TEXTURE_CUBE 0x8513

static glcore_state_t core_state;
static GLenum core_matrix_mode;
static GLfloat core_matrices[2 + GL_CORE_TEXTURE_UNITS][CORE_MATRIX_DEPTH][16];
static unsigned core_matrix_depth[2 + GL_CORE_TEXTURE_UNITS];
static unsigned core_attrib_depth;
static qboolean core_state_installed;

#define CORE_SLOT(name, result, args) static result (APIENTRY *core_saved_##name) args
CORE_SLOT (Enable, void, (GLenum));
CORE_SLOT (Disable, void, (GLenum));
CORE_SLOT (IsEnabled, GLboolean, (GLenum));
CORE_SLOT (GetFloatv, void, (GLenum, GLfloat *));
CORE_SLOT (GetDoublev, void, (GLenum, GLdouble *));
CORE_SLOT (GetIntegerv, void, (GLenum, GLint *));
CORE_SLOT (GetBooleanv, void, (GLenum, GLboolean *));
CORE_SLOT (MatrixMode, void, (GLenum));
CORE_SLOT (LoadIdentity, void, (void));
CORE_SLOT (LoadMatrixf, void, (const GLfloat *));
CORE_SLOT (LoadMatrixd, void, (const GLdouble *));
CORE_SLOT (MultMatrixf, void, (const GLfloat *));
CORE_SLOT (MultMatrixd, void, (const GLdouble *));
CORE_SLOT (PushMatrix, void, (void));
CORE_SLOT (PopMatrix, void, (void));
CORE_SLOT (Translatef, void, (GLfloat, GLfloat, GLfloat));
CORE_SLOT (Translated, void, (GLdouble, GLdouble, GLdouble));
CORE_SLOT (Scalef, void, (GLfloat, GLfloat, GLfloat));
CORE_SLOT (Scaled, void, (GLdouble, GLdouble, GLdouble));
CORE_SLOT (Rotatef, void, (GLfloat, GLfloat, GLfloat, GLfloat));
CORE_SLOT (Rotated, void, (GLdouble, GLdouble, GLdouble, GLdouble));
CORE_SLOT (Ortho, void, (GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble));
CORE_SLOT (Frustum, void, (GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble));
CORE_SLOT (AlphaFunc, void, (GLenum, GLclampf));
CORE_SLOT (ShadeModel, void, (GLenum));
CORE_SLOT (Hint, void, (GLenum, GLenum));
CORE_SLOT (TexEnvi, void, (GLenum, GLenum, GLint));
CORE_SLOT (TexEnvf, void, (GLenum, GLenum, GLfloat));
CORE_SLOT (TexEnviv, void, (GLenum, GLenum, const GLint *));
CORE_SLOT (TexEnvfv, void, (GLenum, GLenum, const GLfloat *));
CORE_SLOT (GetTexEnviv, void, (GLenum, GLenum, GLint *));
CORE_SLOT (GetTexEnvfv, void, (GLenum, GLenum, GLfloat *));
CORE_SLOT (PointSize, void, (GLfloat));
CORE_SLOT (PointParameterfEXT, void, (GLenum, GLfloat));
CORE_SLOT (PointParameterfvEXT, void, (GLenum, const GLfloat *));
CORE_SLOT (PointParameterfARB, void, (GLenum, GLfloat));
CORE_SLOT (PointParameterfvARB, void, (GLenum, const GLfloat *));
CORE_SLOT (ActiveTextureARB, void, (GLenum));
CORE_SLOT (ClientActiveTextureARB, void, (GLenum));
CORE_SLOT (SelectTextureSGIS, void, (GLenum));
CORE_SLOT (MTexCoord2fSGIS, void, (GLenum, GLfloat, GLfloat));
CORE_SLOT (MTexCoord2fvSGIS, void, (GLenum, GLfloat *));
CORE_SLOT (PushAttrib, void, (GLbitfield));
CORE_SLOT (PopAttrib, void, (void));

static void (APIENTRY *core_active_texture) (GLenum);
static void (APIENTRY *core_blend_func_separate) (GLenum, GLenum, GLenum, GLenum);
static void (APIENTRY *core_blend_equation_separate) (GLenum, GLenum);
static void (APIENTRY *core_blend_color) (GLclampf, GLclampf, GLclampf, GLclampf);
static void (APIENTRY *core_stencil_func_separate) (GLenum, GLenum, GLint, GLuint);
static void (APIENTRY *core_stencil_op_separate) (GLenum, GLenum, GLenum, GLenum);
static void (APIENTRY *core_stencil_mask_separate) (GLenum, GLuint);

static void Core_Identity (GLfloat *m)
{
	memset (m, 0, 16 * sizeof (*m));
	m[0] = m[5] = m[10] = m[15] = 1;
}

static unsigned Core_MatrixIndex (void)
{
	if (core_matrix_mode == GL_PROJECTION)
		return 1;
	if (core_matrix_mode == GL_TEXTURE)
		return 2 + core_state.active_texture;
	return 0;
}

static GLfloat *Core_Matrix (void)
{
	if (core_matrix_mode == GL_PROJECTION)
		return core_state.projection;
	if (core_matrix_mode == GL_TEXTURE)
		return core_state.texture[core_state.active_texture];
	return core_state.modelview;
}

static void APIENTRY Core_MatrixMode (GLenum mode)
{
	if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE)
	{
		GL_CoreStateError ("Invalid matrix mode");
		return;
	}
	core_matrix_mode = mode;
}

static void APIENTRY Core_LoadIdentity (void)
{
	Core_Identity (Core_Matrix ());
}

static void APIENTRY Core_LoadMatrixf (const GLfloat *m)
{
	memcpy (Core_Matrix (), m, 16 * sizeof (*m));
}

static void APIENTRY Core_LoadMatrixd (const GLdouble *m)
{
	GLfloat *out = Core_Matrix ();
	int i;
	for (i = 0; i < 16; i++)
		out[i] = (GLfloat)m[i];
}

static void APIENTRY Core_MultMatrixf (const GLfloat *m)
{
	GLfloat out[16], *a = Core_Matrix ();
	int row, col, k;
	for (col = 0; col < 4; col++)
		for (row = 0; row < 4; row++)
		{
			GLdouble sum = 0;
			for (k = 0; k < 4; k++)
				sum += (GLdouble)a[k * 4 + row] * m[col * 4 + k];
			out[col * 4 + row] = (GLfloat)sum;
		}
	memcpy (a, out, sizeof (out));
}

static void APIENTRY Core_MultMatrixd (const GLdouble *m)
{
	GLfloat out[16];
	int i;
	for (i = 0; i < 16; i++)
		out[i] = (GLfloat)m[i];
	Core_MultMatrixf (out);
}

static void APIENTRY Core_PushMatrix (void)
{
	unsigned index = Core_MatrixIndex (), depth = core_matrix_depth[index];
	if (depth >= CORE_MATRIX_DEPTH - 1)
	{
		GL_CoreStateError ("Matrix stack overflow");
		return;
	}
	memcpy (core_matrices[index][depth], Core_Matrix (), 16 * sizeof (GLfloat));
	core_matrix_depth[index] = depth + 1;
}

static void APIENTRY Core_PopMatrix (void)
{
	unsigned index = Core_MatrixIndex (), depth = core_matrix_depth[index];
	if (!depth)
	{
		GL_CoreStateError ("Matrix stack underflow");
		return;
	}
	core_matrix_depth[index] = --depth;
	memcpy (Core_Matrix (), core_matrices[index][depth], 16 * sizeof (GLfloat));
}

static void APIENTRY Core_Translated (GLdouble x, GLdouble y, GLdouble z)
{
	GLfloat m[16];
	Core_Identity (m);
	m[12] = (GLfloat)x;
	m[13] = (GLfloat)y;
	m[14] = (GLfloat)z;
	Core_MultMatrixf (m);
}

static void APIENTRY Core_Translatef (GLfloat x, GLfloat y, GLfloat z)
{
	Core_Translated (x, y, z);
}

static void APIENTRY Core_Scaled (GLdouble x, GLdouble y, GLdouble z)
{
	GLfloat m[16];
	Core_Identity (m);
	m[0] = (GLfloat)x;
	m[5] = (GLfloat)y;
	m[10] = (GLfloat)z;
	Core_MultMatrixf (m);
}

static void APIENTRY Core_Scalef (GLfloat x, GLfloat y, GLfloat z)
{
	Core_Scaled (x, y, z);
}

static void APIENTRY Core_Rotated (GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
	GLdouble length = sqrt (x * x + y * y + z * z), c, s, t;
	GLfloat m[16];
	if (length == 0)
		return;
	x /= length;
	y /= length;
	z /= length;
	c = cos (angle * 0.017453292519943295);
	s = sin (angle * 0.017453292519943295);
	t = 1 - c;
	Core_Identity (m);
	m[0] = (GLfloat)(x * x * t + c);
	m[4] = (GLfloat)(x * y * t - z * s);
	m[8] = (GLfloat)(x * z * t + y * s);
	m[1] = (GLfloat)(y * x * t + z * s);
	m[5] = (GLfloat)(y * y * t + c);
	m[9] = (GLfloat)(y * z * t - x * s);
	m[2] = (GLfloat)(z * x * t - y * s);
	m[6] = (GLfloat)(z * y * t + x * s);
	m[10] = (GLfloat)(z * z * t + c);
	Core_MultMatrixf (m);
}

static void APIENTRY Core_Rotatef (GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	Core_Rotated (angle, x, y, z);
}

static void APIENTRY Core_Ortho (GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	GLfloat m[16];
	if (l == r || b == t || n == f)
	{
		GL_CoreStateError ("Degenerate orthographic projection");
		return;
	}
	Core_Identity (m);
	m[0] = (GLfloat)(2 / (r - l));
	m[5] = (GLfloat)(2 / (t - b));
	m[10] = (GLfloat)(-2 / (f - n));
	m[12] = (GLfloat)(-(r + l) / (r - l));
	m[13] = (GLfloat)(-(t + b) / (t - b));
	m[14] = (GLfloat)(-(f + n) / (f - n));
	Core_MultMatrixf (m);
}

static void APIENTRY Core_Frustum (GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	GLfloat m[16];
	if (l == r || b == t || n == f || n <= 0 || f <= 0)
	{
		GL_CoreStateError ("Invalid perspective projection");
		return;
	}
	memset (m, 0, sizeof (m));
	m[0] = (GLfloat)(2 * n / (r - l));
	m[5] = (GLfloat)(2 * n / (t - b));
	m[8] = (GLfloat)((r + l) / (r - l));
	m[9] = (GLfloat)((t + b) / (t - b));
	m[10] = (GLfloat)(-(f + n) / (f - n));
	m[11] = -1;
	m[14] = (GLfloat)(-2 * f * n / (f - n));
	Core_MultMatrixf (m);
}

static GLfloat Core_Clamp (GLdouble value)
{
	if (value < 0)
		return 0;
	if (value > 1)
		return 1;
	return (GLfloat)value;
}

static qboolean Core_Finite (GLfloat value)
{
	return value >= -FLT_MAX && value <= FLT_MAX;
}

static void Core_Color (GLdouble r, GLdouble g, GLdouble b, GLdouble a)
{
	core_state.color[0] = (GLfloat)r;
	core_state.color[1] = (GLfloat)g;
	core_state.color[2] = (GLfloat)b;
	core_state.color[3] = (GLfloat)a;
}

#define CORE_COLOR(suffix, type, divisor, bias) \
	CORE_SLOT(Color3##suffix, void, (type, type, type)); \
	CORE_SLOT(Color3##suffix##v, void, (const type *)); \
	CORE_SLOT(Color4##suffix, void, (type, type, type, type)); \
	CORE_SLOT(Color4##suffix##v, void, (const type *)); \
	static void APIENTRY Core_Color3##suffix (type r, type g, type b) \
	{ Core_Color (((GLdouble)r+(bias))/(divisor), ((GLdouble)g+(bias))/(divisor), ((GLdouble)b+(bias))/(divisor), 1); } \
	static void APIENTRY Core_Color3##suffix##v (const type *v) \
	{ Core_Color3##suffix (v[0], v[1], v[2]); } \
	static void APIENTRY Core_Color4##suffix (type r, type g, type b, type a) \
	{ Core_Color (((GLdouble)r+(bias))/(divisor), ((GLdouble)g+(bias))/(divisor), ((GLdouble)b+(bias))/(divisor), ((GLdouble)a+(bias))/(divisor)); } \
	static void APIENTRY Core_Color4##suffix##v (const type *v) \
	{ Core_Color4##suffix (v[0], v[1], v[2], v[3]); }
CORE_COLOR (f, GLfloat, 1.0, 0.0)
CORE_COLOR (d, GLdouble, 1.0, 0.0)
CORE_COLOR (b, GLbyte, 127.5, 0.5)
CORE_COLOR (s, GLshort, 32767.5, 0.5)
CORE_COLOR (i, GLint, 2147483647.5, 0.5)
CORE_COLOR (ub, GLubyte, 255.0, 0.0)
CORE_COLOR (us, GLushort, 65535.0, 0.0)
CORE_COLOR (ui, GLuint, 4294967295.0, 0.0)

static void Core_TexCoord (unsigned unit, GLdouble s, GLdouble t, GLdouble r, GLdouble q)
{
	GLfloat *out = core_state.texcoord[unit];
	out[0] = (GLfloat)s;
	out[1] = (GLfloat)t;
	out[2] = (GLfloat)r;
	out[3] = (GLfloat)q;
}

#define CORE_TEXCOORD(suffix, type) \
	CORE_SLOT(TexCoord1##suffix, void, (type)); \
	CORE_SLOT(TexCoord1##suffix##v, void, (const type *)); \
	CORE_SLOT(TexCoord2##suffix, void, (type, type)); \
	CORE_SLOT(TexCoord2##suffix##v, void, (const type *)); \
	CORE_SLOT(TexCoord3##suffix, void, (type, type, type)); \
	CORE_SLOT(TexCoord3##suffix##v, void, (const type *)); \
	CORE_SLOT(TexCoord4##suffix, void, (type, type, type, type)); \
	CORE_SLOT(TexCoord4##suffix##v, void, (const type *)); \
	static void APIENTRY Core_TexCoord1##suffix (type s) { Core_TexCoord (0, s, 0, 0, 1); } \
	static void APIENTRY Core_TexCoord1##suffix##v (const type *v) { Core_TexCoord1##suffix (v[0]); } \
	static void APIENTRY Core_TexCoord2##suffix (type s, type t) { Core_TexCoord (0, s, t, 0, 1); } \
	static void APIENTRY Core_TexCoord2##suffix##v (const type *v) { Core_TexCoord2##suffix (v[0], v[1]); } \
	static void APIENTRY Core_TexCoord3##suffix (type s, type t, type r) { Core_TexCoord (0, s, t, r, 1); } \
	static void APIENTRY Core_TexCoord3##suffix##v (const type *v) { Core_TexCoord3##suffix (v[0], v[1], v[2]); } \
	static void APIENTRY Core_TexCoord4##suffix (type s, type t, type r, type q) { Core_TexCoord (0, s, t, r, q); } \
	static void APIENTRY Core_TexCoord4##suffix##v (const type *v) { Core_TexCoord4##suffix (v[0], v[1], v[2], v[3]); }
CORE_TEXCOORD (f, GLfloat)
CORE_TEXCOORD (d, GLdouble)
CORE_TEXCOORD (i, GLint)
CORE_TEXCOORD (s, GLshort)

static int Core_TextureUnit (GLenum texture)
{
	if (texture >= CORE_TEXTURE0 && texture < CORE_TEXTURE0 + GL_CORE_TEXTURE_UNITS)
		return (int)(texture - CORE_TEXTURE0);
	GL_CoreStateError ("Texture unit outside core facade capacity");
	return -1;
}

static void APIENTRY Core_ActiveTexture (GLenum texture)
{
	int unit = Core_TextureUnit (texture);
	if (unit < 0)
		return;
	core_state.active_texture = (unsigned)unit;
	core_active_texture (texture);
}

static void APIENTRY Core_ClientActiveTexture (GLenum texture)
{
	int unit = Core_TextureUnit (texture);
	if (unit >= 0)
		core_state.client_texture = (unsigned)unit;
}

static void APIENTRY Core_SelectTexture (GLenum texture)
{
	if (texture < GL_TEXTURE0_SGIS || texture >= GL_TEXTURE0_SGIS + GL_CORE_TEXTURE_UNITS)
	{
		GL_CoreStateError ("Invalid SGIS texture unit");
		return;
	}
	Core_ActiveTexture (CORE_TEXTURE0 + texture - GL_TEXTURE0_SGIS);
}

static void APIENTRY Core_MultiTexCoord2f (GLenum texture, GLfloat s, GLfloat t)
{
	int unit = Core_TextureUnit (texture);
	if (unit >= 0)
		Core_TexCoord ((unsigned)unit, s, t, 0, 1);
}

static void APIENTRY Core_MultiTexCoord2fv (GLenum texture, GLfloat *v)
{
	Core_MultiTexCoord2f (texture, v[0], v[1]);
}

static void APIENTRY Core_MTexCoord2fSGIS (GLenum texture, GLfloat s, GLfloat t)
{
	if (texture < GL_TEXTURE0_SGIS || texture >= GL_TEXTURE0_SGIS + GL_CORE_TEXTURE_UNITS)
	{
		GL_CoreStateError ("Invalid SGIS texture coordinate unit");
		return;
	}
	Core_TexCoord (texture - GL_TEXTURE0_SGIS, s, t, 0, 1);
}

static void APIENTRY Core_MTexCoord2fvSGIS (GLenum texture, GLfloat *v)
{
	Core_MTexCoord2fSGIS (texture, v[0], v[1]);
}

static void APIENTRY Core_AlphaFunc (GLenum func, GLclampf ref)
{
	if (func < GL_NEVER || func > GL_ALWAYS)
	{
		GL_CoreStateError ("Invalid alpha comparison");
		return;
	}
	core_state.alpha_func = func;
	core_state.alpha_ref = Core_Clamp (ref);
}

static void APIENTRY Core_ShadeModel (GLenum mode)
{
	if (mode != GL_FLAT && mode != GL_SMOOTH)
	{
		GL_CoreStateError ("Invalid shading model");
		return;
	}
	core_state.shade_model = mode;
}

static void APIENTRY Core_TexEnvi (GLenum target, GLenum pname, GLint param)
{
	if (target == CORE_POINT_SPRITE && pname == CORE_COORD_REPLACE)
	{
		if (param != GL_FALSE && param != GL_TRUE)
			GL_CoreStateError ("Invalid point coordinate replacement");
		else
			core_state.point_replace[core_state.active_texture] = (GLboolean)param;
		return;
	}
	if (target == GL_TEXTURE_ENV && (pname == 0x8571 || pname == 0x8572))
	{
		/* Retained overbright combines only modulate RGB and alpha. */
		if (param != GL_MODULATE)
			GL_CoreStateError ("Unsupported texture combine function");
		return;
	}
	if (target == GL_TEXTURE_ENV && pname == 0x8573)
	{
		if (param != 1 && param != 2 && param != 4)
			GL_CoreStateError ("Invalid texture RGB scale");
		else
			core_state.texenv_scale[core_state.active_texture] = (GLfloat)param;
		return;
	}
	if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE ||
		(param != GL_MODULATE && param != GL_REPLACE && param != GL_ADD && param != GL_DECAL && param != 0x8570))
	{
		GL_CoreStateError ("Unsupported texture environment");
		return;
	}
	core_state.texenv[core_state.active_texture] = (GLenum)param;
}

static void APIENTRY Core_TexEnvf (GLenum target, GLenum pname, GLfloat param)
{
	if (!Core_Finite (param) || (GLdouble)param < -2147483648.0 ||
		(GLdouble)param > 2147483647.0 || (GLdouble)param != floor ((GLdouble)param))
	{
		GL_CoreStateError ("Invalid floating texture environment value");
		return;
	}
	Core_TexEnvi (target, pname, (GLint)param);
}

static void APIENTRY Core_TexEnviv (GLenum target, GLenum pname, const GLint *params)
{
	Core_TexEnvi (target, pname, params[0]);
}

static void APIENTRY Core_TexEnvfv (GLenum target, GLenum pname, const GLfloat *params)
{
	Core_TexEnvf (target, pname, params[0]);
}

static void APIENTRY Core_GetTexEnviv (GLenum target, GLenum pname, GLint *out)
{
	if (target == CORE_POINT_SPRITE && pname == CORE_COORD_REPLACE)
		*out = core_state.point_replace[core_state.active_texture];
	else if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_MODE)
		*out = (GLint)core_state.texenv[core_state.active_texture];
	else if (target == GL_TEXTURE_ENV && (pname == 0x8571 || pname == 0x8572))
		*out = GL_MODULATE;
	else if (target == GL_TEXTURE_ENV && pname == 0x8573)
		*out = (GLint)core_state.texenv_scale[core_state.active_texture];
	else
		GL_CoreStateError ("Unsupported texture environment query");
}

static void APIENTRY Core_GetTexEnvfv (GLenum target, GLenum pname, GLfloat *out)
{
	GLint value = 0;
	Core_GetTexEnviv (target, pname, &value);
	*out = (GLfloat)value;
}

static void APIENTRY Core_PointSize (GLfloat size)
{
	if (!(size > 0) || !Core_Finite (size))
	{
		GL_CoreStateError ("Invalid point size");
		return;
	}
	core_state.point_size = size;
	core_saved_PointSize (size);
}

static void APIENTRY Core_PointParameterfv (GLenum param, const GLfloat *value)
{
	if (param == GL_DISTANCE_ATTENUATION_EXT)
	{
		if (value[0] < 0 || value[1] < 0 || value[2] < 0 ||
			!Core_Finite (value[0]) || !Core_Finite (value[1]) || !Core_Finite (value[2]))
		{
			GL_CoreStateError ("Invalid point attenuation");
			return;
		}
		memcpy (core_state.point_attenuation, value, sizeof (core_state.point_attenuation));
	}
	else if (*value < 0 || !Core_Finite (*value))
		GL_CoreStateError ("Invalid point parameter");
	else if (param == GL_POINT_SIZE_MIN_EXT)
		core_state.point_min = *value;
	else if (param == GL_POINT_SIZE_MAX_EXT)
		core_state.point_max = *value;
	else if (param == GL_POINT_FADE_THRESHOLD_SIZE_EXT)
		core_state.point_fade = *value;
	else
		GL_CoreStateError ("Unsupported point parameter");
}

static void APIENTRY Core_PointParameterf (GLenum param, GLfloat value)
{
	if (param == GL_DISTANCE_ATTENUATION_EXT)
		GL_CoreStateError ("Point attenuation requires three values");
	else
		Core_PointParameterfv (param, &value);
}

/* Removed enable bits are private metadata, never submitted to the driver. */
static const GLenum core_removed_caps[] = {
	GL_FOG, GL_LIGHTING, GL_COLOR_MATERIAL, GL_NORMALIZE,
	GL_TEXTURE_GEN_S, GL_TEXTURE_GEN_T, GL_TEXTURE_GEN_R, GL_TEXTURE_GEN_Q,
	GL_LINE_STIPPLE, GL_POLYGON_STIPPLE, GL_LIGHT0, GL_LIGHT1, GL_LIGHT2,
	GL_LIGHT3, GL_LIGHT4, GL_LIGHT5, GL_LIGHT6, GL_LIGHT7, 0x803A
};
#define CORE_REMOVED_COUNT (sizeof(core_removed_caps) / sizeof(core_removed_caps[0]))
static GLboolean core_removed_enabled[CORE_REMOVED_COUNT];
static const GLenum core_removed_hints[] = {
	GL_PERSPECTIVE_CORRECTION_HINT, GL_POINT_SMOOTH_HINT, GL_FOG_HINT
};
static GLenum core_hint_values[3];
static const GLenum core_native_hints[] = {
	GL_LINE_SMOOTH_HINT, GL_POLYGON_SMOOTH_HINT, 0x84EF, 0x8B8B
};

static int Core_RemovedCap (GLenum cap)
{
	unsigned i;
	for (i = 0; i < CORE_REMOVED_COUNT; i++)
		if (core_removed_caps[i] == cap)
			return (int)i;
	return -1;
}

static void Core_SetEnabled (GLenum cap, GLboolean enabled)
{
	int index;
	if (cap == GL_TEXTURE_2D)
		core_state.texture_enabled[core_state.active_texture] = enabled;
	else if (cap == GL_ALPHA_TEST)
		core_state.alpha_test = enabled;
	else if (cap == CORE_POINT_SPRITE)
		core_state.point_sprite = enabled;
	else if (cap == GL_POINT_SMOOTH)
		core_state.point_smooth = enabled;
	else if ((index = Core_RemovedCap (cap)) >= 0)
	{
		/* Those fixed-function algorithms are absent from the renderer's shader contract. */
		if (enabled)
			GL_CoreStateError ("Unsupported fixed-function capability enabled");
		else
			core_removed_enabled[index] = GL_FALSE;
	}
	else if (enabled)
		core_saved_Enable (cap);
	else
		core_saved_Disable (cap);
}

static void APIENTRY Core_Enable (GLenum cap)
{
	Core_SetEnabled (cap, GL_TRUE);
}

static void APIENTRY Core_Disable (GLenum cap)
{
	Core_SetEnabled (cap, GL_FALSE);
}

static GLboolean APIENTRY Core_IsEnabled (GLenum cap)
{
	int index;
	if (cap == GL_TEXTURE_2D)
		return core_state.texture_enabled[core_state.active_texture];
	if (cap == GL_ALPHA_TEST)
		return core_state.alpha_test;
	if (cap == CORE_POINT_SPRITE)
		return core_state.point_sprite;
	if (cap == GL_POINT_SMOOTH)
		return core_state.point_smooth;
	index = Core_RemovedCap (cap);
	if (index >= 0)
		return core_removed_enabled[index];
	return core_saved_IsEnabled (cap);
}

static void APIENTRY Core_Hint (GLenum target, GLenum mode)
{
	unsigned i;
	if (mode != GL_DONT_CARE && mode != GL_FASTEST && mode != GL_NICEST)
	{
		GL_CoreStateError ("Invalid hint mode");
		return;
	}
	for (i = 0; i < 3; i++)
		if (target == core_removed_hints[i])
		{
			core_hint_values[i] = mode;
			return;
		}
	core_saved_Hint (target, mode);
}

static void (APIENTRY *core_get_attachment) (GLenum, GLenum, GLenum, GLint *);

static GLint Core_FramebufferBits (GLenum pname)
{
	GLint framebuffer = 0, attachment = 0, object = 0, value = 0, doubled = 0;
	GLenum query;
	core_saved_GetIntegerv (0x8CA6, &framebuffer); /* DRAW_FRAMEBUFFER_BINDING */
	if (pname == GL_DEPTH_BITS)
	{
		attachment = framebuffer ? 0x8D00 : GL_DEPTH;
		query = 0x8216;
	}
	else if (pname == GL_STENCIL_BITS)
	{
		attachment = framebuffer ? 0x8D20 : GL_STENCIL;
		query = 0x8217;
	}
	else
	{
		query = pname == GL_RED_BITS ? 0x8212 : pname == GL_GREEN_BITS ? 0x8213 :
			pname == GL_BLUE_BITS									   ? 0x8214 :
																		 0x8215;
		if (framebuffer)
		{
			core_saved_GetIntegerv (GL_DRAW_BUFFER, &attachment);
			if (attachment == GL_NONE)
				return 0;
		}
		else
		{
			core_saved_GetIntegerv (GL_DOUBLEBUFFER, &doubled);
			attachment = doubled ? GL_BACK_LEFT : GL_FRONT_LEFT;
		}
	}
	core_get_attachment (0x8CA9, (GLenum)attachment, 0x8CD0, &object);
	if (object != GL_NONE)
		core_get_attachment (0x8CA9, (GLenum)attachment, query, &value);
	return value;
}

static int Core_Query (GLenum pname, GLfloat *out)
{
	const GLfloat *values = NULL;
	unsigned i;
	int count = 1, removed;
	switch (pname)
	{
	case GL_MODELVIEW_MATRIX:
		values = core_state.modelview;
		count = 16;
		break;
	case GL_PROJECTION_MATRIX:
		values = core_state.projection;
		count = 16;
		break;
	case GL_TEXTURE_MATRIX:
		values = core_state.texture[core_state.active_texture];
		count = 16;
		break;
	case GL_CURRENT_COLOR:
		values = core_state.color;
		count = 4;
		break;
	case GL_CURRENT_TEXTURE_COORDS:
		values = core_state.texcoord[core_state.active_texture];
		count = 4;
		break;
	case GL_MATRIX_MODE:
		out[0] = (GLfloat)core_matrix_mode;
		break;
	case GL_MODELVIEW_STACK_DEPTH:
		out[0] = (GLfloat)(core_matrix_depth[0] + 1);
		break;
	case GL_PROJECTION_STACK_DEPTH:
		out[0] = (GLfloat)(core_matrix_depth[1] + 1);
		break;
	case GL_TEXTURE_STACK_DEPTH:
		out[0] = (GLfloat)(core_matrix_depth[2 + core_state.active_texture] + 1);
		break;
	case GL_MAX_MODELVIEW_STACK_DEPTH:
	case GL_MAX_PROJECTION_STACK_DEPTH:
	case GL_MAX_TEXTURE_STACK_DEPTH:
		out[0] = CORE_MATRIX_DEPTH;
		break;
	case GL_MAX_ATTRIB_STACK_DEPTH:
		out[0] = CORE_ATTRIB_DEPTH;
		break;
	case GL_ATTRIB_STACK_DEPTH:
		out[0] = (GLfloat)core_attrib_depth;
		break;
	case GL_ALPHA_TEST:
		out[0] = core_state.alpha_test;
		break;
	case GL_ALPHA_TEST_FUNC:
		out[0] = (GLfloat)core_state.alpha_func;
		break;
	case GL_ALPHA_TEST_REF:
		out[0] = core_state.alpha_ref;
		break;
	case GL_SHADE_MODEL:
		out[0] = (GLfloat)core_state.shade_model;
		break;
	case GL_POINT_SIZE:
		out[0] = core_state.point_size;
		break;
	case GL_POINT_SIZE_MIN_EXT:
		out[0] = core_state.point_min;
		break;
	case GL_POINT_SIZE_MAX_EXT:
		out[0] = core_state.point_max;
		break;
	case GL_POINT_FADE_THRESHOLD_SIZE_EXT:
		out[0] = core_state.point_fade;
		break;
	case GL_DISTANCE_ATTENUATION_EXT:
		values = core_state.point_attenuation;
		count = 3;
		break;
	case GL_POINT_SMOOTH:
		out[0] = core_state.point_smooth;
		break;
	case CORE_POINT_SPRITE:
		out[0] = core_state.point_sprite;
		break;
	case GL_TEXTURE_2D:
		out[0] = core_state.texture_enabled[core_state.active_texture];
		break;
	case CORE_ACTIVE_TEXTURE:
		out[0] = (GLfloat)(CORE_TEXTURE0 + core_state.active_texture);
		break;
	case CORE_CLIENT_ACTIVE_TEXTURE:
		out[0] = (GLfloat)(CORE_TEXTURE0 + core_state.client_texture);
		break;
	case CORE_MAX_TEXTURE_UNITS:
		out[0] = GL_CORE_TEXTURE_UNITS;
		break;
	case GL_MAX_CLIP_PLANES:
		out[0] = 0;
		break;
	case GL_RED_BITS:
	case GL_GREEN_BITS:
	case GL_BLUE_BITS:
	case GL_ALPHA_BITS:
	case GL_DEPTH_BITS:
	case GL_STENCIL_BITS:
		out[0] = (GLfloat)Core_FramebufferBits (pname);
		break;
	default:
		removed = Core_RemovedCap (pname);
		if (removed >= 0)
			out[0] = core_removed_enabled[removed];
		else
		{
			for (i = 0; i < 3; i++)
				if (pname == core_removed_hints[i])
				{
					out[0] = (GLfloat)core_hint_values[i];
					return 1;
				}
			return 0;
		}
	}
	if (values)
		memcpy (out, values, count * sizeof (*out));
	return count;
}

static void APIENTRY Core_GetFloatv (GLenum pname, GLfloat *out)
{
	GLuint texture;
	if (GL_CoreTextureBinding (pname, &texture))
	{
		*out = (GLfloat)texture;
		return;
	}
	if (!Core_Query (pname, out))
		core_saved_GetFloatv (pname, out);
}

static void APIENTRY Core_GetDoublev (GLenum pname, GLdouble *out)
{
	GLfloat values[16];
	GLuint texture;
	int i, count;
	if (GL_CoreTextureBinding (pname, &texture))
	{
		*out = (GLdouble)texture;
		return;
	}
	count = Core_Query (pname, values);
	if (!count)
		core_saved_GetDoublev (pname, out);
	else
		for (i = 0; i < count; i++)
			out[i] = values[i];
}

static void APIENTRY Core_GetIntegerv (GLenum pname, GLint *out)
{
	GLfloat values[16];
	GLuint texture;
	int i, count;
	/* Logical names need the integer path: large aliases lose float precision. */
	if (GL_CoreTextureBinding (pname, &texture))
	{
		*out = (GLint)texture;
		return;
	}
	count = Core_Query (pname, values);
	if (!count)
		core_saved_GetIntegerv (pname, out);
	else
		for (i = 0; i < count; i++)
		{
			GLdouble value = values[i];
			if (pname == GL_CURRENT_COLOR || pname == GL_ALPHA_TEST_REF)
				value *= 2147483647.0;
			if (value != value)
			{
				GL_CoreStateError ("Nonfinite integer state conversion");
				out[i] = 0;
			}
			else if (value >= 2147483647.0)
				out[i] = 2147483647;
			else if (value <= -2147483648.0)
				out[i] = (-2147483647 - 1);
			else
				out[i] = (GLint)floor (value + 0.5);
		}
}

static void APIENTRY Core_GetBooleanv (GLenum pname, GLboolean *out)
{
	GLfloat values[16];
	GLuint texture;
	int i, count;
	if (GL_CoreTextureBinding (pname, &texture))
	{
		*out = texture ? GL_TRUE : GL_FALSE;
		return;
	}
	count = Core_Query (pname, values);
	if (!count)
		core_saved_GetBooleanv (pname, out);
	else
		for (i = 0; i < count; i++)
			out[i] = values[i] != 0 ? GL_TRUE : GL_FALSE;
}

typedef struct
{
	GLenum cap;
	GLbitfield group;
} core_enable_t;

/* A core context has no hidden legacy enable state to preserve. */
static const core_enable_t core_enables[] = {
	{ GL_BLEND, GL_COLOR_BUFFER_BIT }, { GL_DITHER, GL_COLOR_BUFFER_BIT },
	{ GL_COLOR_LOGIC_OP, GL_COLOR_BUFFER_BIT }, { GL_DEPTH_TEST, GL_DEPTH_BUFFER_BIT },
	{ GL_STENCIL_TEST, GL_STENCIL_BUFFER_BIT }, { GL_CULL_FACE, GL_POLYGON_BIT },
	{ GL_SCISSOR_TEST, GL_SCISSOR_BIT }, { GL_LINE_SMOOTH, GL_LINE_BIT },
	{ GL_POLYGON_SMOOTH, GL_POLYGON_BIT }, { GL_POLYGON_OFFSET_FILL, GL_POLYGON_BIT },
	{ GL_POLYGON_OFFSET_LINE, GL_POLYGON_BIT }, { GL_POLYGON_OFFSET_POINT, GL_POLYGON_BIT },
	{ 0x809D, 0x20000000 }, { 0x809E, 0x20000000 }, { 0x809F, 0x20000000 }, { 0x80A0, 0x20000000 },
	{ 0x8642, GL_POINT_BIT }, { 0x8C89, 0 }, { 0x884F, GL_TEXTURE_BIT },
	{ 0x3000, GL_TRANSFORM_BIT }, { 0x3001, GL_TRANSFORM_BIT }, { 0x3002, GL_TRANSFORM_BIT },
	{ 0x3003, GL_TRANSFORM_BIT }, { 0x3004, GL_TRANSFORM_BIT }, { 0x3005, GL_TRANSFORM_BIT },
	{ 0x3006, GL_TRANSFORM_BIT }, { 0x3007, GL_TRANSFORM_BIT }
};
#define CORE_ENABLE_COUNT (sizeof(core_enables) / sizeof(core_enables[0]))
#define CORE_TEXTURE_TARGETS 9
static const GLenum core_texture_targets[CORE_TEXTURE_TARGETS] = {
	GL_TEXTURE_1D, GL_TEXTURE_2D, CORE_TEXTURE_3D, CORE_TEXTURE_CUBE,
	0x8C18, 0x8C1A, 0x84F5, 0x8C2A, 0x9100
};
static const GLenum core_texture_bindings[CORE_TEXTURE_TARGETS] = {
	GL_TEXTURE_BINDING_1D, GL_TEXTURE_BINDING_2D, CORE_TEXTURE_BINDING_3D, CORE_TEXTURE_BINDING_CUBE,
	0x8C1C, 0x8C1D, 0x84F6, 0x8C2C, 0x9104
};
static const GLenum core_texture_int_params[9] = {
	GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T,
	0x8072, 0x813C, 0x813D, 0x884C, 0x884D
};
static const GLenum core_texture_lod_params[3] = { 0x813A, 0x813B, 0x8501 };

typedef struct
{
	GLboolean bound;
	GLint integers[9];
	GLfloat border[4], lod[3];
} core_texture_params_t;

typedef struct
{
	GLbitfield mask;
	glcore_state_t state;
	GLenum matrix_mode, hints[3];
	GLint native_hints[4];
	GLboolean removed[CORE_REMOVED_COUNT], enabled[CORE_ENABLE_COUNT];
	GLint textures[GL_CORE_TEXTURE_UNITS][CORE_TEXTURE_TARGETS];
	core_texture_params_t texture_params[GL_CORE_TEXTURE_UNITS][7];
	GLint blend[6], logic, draw_buffer, read_buffer;
	GLfloat blend_color[4], clear_color[4];
	GLboolean color_mask[4];
	GLint depth_func;
	GLboolean depth_mask;
	GLdouble clear_depth, depth_range[2];
	GLint stencil[2][7], clear_stencil;
	GLint cull_face, front_face, polygon_mode[2];
	GLfloat polygon_factor, polygon_units, line_width;
	GLint scissor[4], viewport[4];
	GLfloat sample_coverage;
	GLboolean sample_invert;
} core_attrib_t;

static core_attrib_t core_attribs[CORE_ATTRIB_DEPTH];
static unsigned core_clip_distances;
static void (APIENTRY *core_sample_coverage) (GLclampf, GLboolean);

static qboolean Core_AttribGroup (GLbitfield mask, GLbitfield group)
{
	return mask == GL_ALL_ATTRIB_BITS || (mask & group) != 0;
}

static void APIENTRY Core_PushAttrib (GLbitfield mask)
{
	core_attrib_t *a;
	unsigned i, unit;
	static const GLenum stencil_pnames[2][7] = {
		{ GL_STENCIL_FUNC, GL_STENCIL_REF, GL_STENCIL_VALUE_MASK, GL_STENCIL_WRITEMASK,
			GL_STENCIL_FAIL, GL_STENCIL_PASS_DEPTH_FAIL, GL_STENCIL_PASS_DEPTH_PASS },
		{ 0x8800, 0x8CA3, 0x8CA4, 0x8CA5, 0x8801, 0x8802, 0x8803 }
	};
	if (core_attrib_depth == CORE_ATTRIB_DEPTH)
	{
		GL_CoreStateError ("Attribute stack overflow");
		return;
	}
	a = &core_attribs[core_attrib_depth++];
	a->mask = mask;
	a->state = core_state;
	a->matrix_mode = core_matrix_mode;
	memcpy (a->removed, core_removed_enabled, sizeof (a->removed));
	memcpy (a->hints, core_hint_values, sizeof (a->hints));
	if (Core_AttribGroup (mask, GL_HINT_BIT))
		for (i = 0; i < 4; i++)
			core_saved_GetIntegerv (core_native_hints[i], &a->native_hints[i]);
	for (i = 0; i < CORE_ENABLE_COUNT; i++)
	{
		if (core_enables[i].cap >= 0x3000 && core_enables[i].cap <= 0x3007 &&
			core_enables[i].cap - 0x3000 >= core_clip_distances)
			continue;
		if (Core_AttribGroup (mask, GL_ENABLE_BIT | core_enables[i].group))
			a->enabled[i] = core_saved_IsEnabled (core_enables[i].cap);
	}
	if (Core_AttribGroup (mask, GL_TEXTURE_BIT))
	{
		for (unit = 0; unit < GL_CORE_TEXTURE_UNITS; unit++)
		{
			core_state.active_texture = unit;
			core_active_texture (CORE_TEXTURE0 + unit);
			for (i = 0; i < CORE_TEXTURE_TARGETS; i++)
			{
				qglGetIntegerv (core_texture_bindings[i], &a->textures[unit][i]);
				if (i < 7)
				{
					core_texture_params_t *p = &a->texture_params[unit][i];
					GLint native_texture = 0;
					unsigned k, count = i == 6 ? 5 : 9;
					/* Logical zero can own a private native default object. */
					core_saved_GetIntegerv (core_texture_bindings[i], &native_texture);
					p->bound = native_texture != 0;
					if (!p->bound)
						continue;
					for (k = 0; k < count; k++)
						qglGetTexParameteriv (core_texture_targets[i], core_texture_int_params[k], &p->integers[k]);
					qglGetTexParameterfv (core_texture_targets[i], GL_TEXTURE_BORDER_COLOR, p->border);
					if (i != 6)
						for (k = 0; k < 3; k++)
							qglGetTexParameterfv (core_texture_targets[i], core_texture_lod_params[k], &p->lod[k]);
				}
			}
		}
		core_state.active_texture = a->state.active_texture;
		core_active_texture (CORE_TEXTURE0 + core_state.active_texture);
	}
	if (Core_AttribGroup (mask, GL_COLOR_BUFFER_BIT))
	{
		core_saved_GetIntegerv (0x80C9, &a->blend[0]); /* SRC_RGB */
		core_saved_GetIntegerv (0x80C8, &a->blend[1]); /* DST_RGB */
		core_saved_GetIntegerv (0x80CB, &a->blend[2]); /* SRC_ALPHA */
		core_saved_GetIntegerv (0x80CA, &a->blend[3]); /* DST_ALPHA */
		core_saved_GetIntegerv (0x8009, &a->blend[4]); /* EQUATION_RGB */
		core_saved_GetIntegerv (0x883D, &a->blend[5]); /* EQUATION_ALPHA */
		core_saved_GetFloatv (0x8005, a->blend_color);
		core_saved_GetFloatv (GL_COLOR_CLEAR_VALUE, a->clear_color);
		core_saved_GetBooleanv (GL_COLOR_WRITEMASK, a->color_mask);
		core_saved_GetIntegerv (GL_LOGIC_OP_MODE, &a->logic);
		core_saved_GetIntegerv (GL_DRAW_BUFFER, &a->draw_buffer);
	}
	if (Core_AttribGroup (mask, GL_PIXEL_MODE_BIT))
		core_saved_GetIntegerv (GL_READ_BUFFER, &a->read_buffer);
	if (Core_AttribGroup (mask, GL_DEPTH_BUFFER_BIT))
	{
		core_saved_GetIntegerv (GL_DEPTH_FUNC, &a->depth_func);
		core_saved_GetBooleanv (GL_DEPTH_WRITEMASK, &a->depth_mask);
		core_saved_GetDoublev (GL_DEPTH_CLEAR_VALUE, &a->clear_depth);
	}
	if (Core_AttribGroup (mask, GL_VIEWPORT_BIT))
	{
		core_saved_GetIntegerv (GL_VIEWPORT, a->viewport);
		core_saved_GetDoublev (GL_DEPTH_RANGE, a->depth_range);
	}
	if (Core_AttribGroup (mask, GL_STENCIL_BUFFER_BIT))
	{
		unsigned side;
		for (side = 0; side < 2; side++)
			for (i = 0; i < 7; i++)
				core_saved_GetIntegerv (stencil_pnames[side][i], &a->stencil[side][i]);
		core_saved_GetIntegerv (GL_STENCIL_CLEAR_VALUE, &a->clear_stencil);
	}
	if (Core_AttribGroup (mask, GL_POLYGON_BIT))
	{
		core_saved_GetIntegerv (GL_CULL_FACE_MODE, &a->cull_face);
		core_saved_GetIntegerv (GL_FRONT_FACE, &a->front_face);
		core_saved_GetIntegerv (GL_POLYGON_MODE, a->polygon_mode);
		core_saved_GetFloatv (GL_POLYGON_OFFSET_FACTOR, &a->polygon_factor);
		core_saved_GetFloatv (GL_POLYGON_OFFSET_UNITS, &a->polygon_units);
	}
	if (Core_AttribGroup (mask, GL_LINE_BIT))
		core_saved_GetFloatv (GL_LINE_WIDTH, &a->line_width);
	if (Core_AttribGroup (mask, GL_SCISSOR_BIT))
		core_saved_GetIntegerv (GL_SCISSOR_BOX, a->scissor);
	if (Core_AttribGroup (mask, 0x20000000))
	{
		core_saved_GetFloatv (0x80AA, &a->sample_coverage);
		core_saved_GetBooleanv (0x80AB, &a->sample_invert);
	}
}

static void APIENTRY Core_PopAttrib (void)
{
	core_attrib_t *a;
	GLbitfield mask;
	unsigned i, unit;
	if (!core_attrib_depth)
	{
		GL_CoreStateError ("Attribute stack underflow");
		return;
	}
	a = &core_attribs[--core_attrib_depth];
	mask = a->mask;
	if (Core_AttribGroup (mask, GL_CURRENT_BIT))
	{
		memcpy (core_state.color, a->state.color, sizeof (core_state.color));
		memcpy (core_state.texcoord, a->state.texcoord, sizeof (core_state.texcoord));
	}
	if (Core_AttribGroup (mask, GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT))
		core_state.alpha_test = a->state.alpha_test;
	if (Core_AttribGroup (mask, GL_COLOR_BUFFER_BIT))
	{
		core_state.alpha_func = a->state.alpha_func;
		core_state.alpha_ref = a->state.alpha_ref;
		core_blend_func_separate ((GLenum)a->blend[0], (GLenum)a->blend[1], (GLenum)a->blend[2], (GLenum)a->blend[3]);
		core_blend_equation_separate ((GLenum)a->blend[4], (GLenum)a->blend[5]);
		core_blend_color (a->blend_color[0], a->blend_color[1], a->blend_color[2], a->blend_color[3]);
		qglColorMask (a->color_mask[0], a->color_mask[1], a->color_mask[2], a->color_mask[3]);
		qglClearColor (a->clear_color[0], a->clear_color[1], a->clear_color[2], a->clear_color[3]);
		qglLogicOp ((GLenum)a->logic);
		qglDrawBuffer ((GLenum)a->draw_buffer);
	}
	if (Core_AttribGroup (mask, GL_PIXEL_MODE_BIT))
		qglReadBuffer ((GLenum)a->read_buffer);
	if (Core_AttribGroup (mask, GL_POINT_BIT))
	{
		core_state.point_size = a->state.point_size;
		core_state.point_min = a->state.point_min;
		core_state.point_max = a->state.point_max;
		core_state.point_fade = a->state.point_fade;
		memcpy (core_state.point_attenuation, a->state.point_attenuation, sizeof (core_state.point_attenuation));
		core_saved_PointSize (core_state.point_size);
	}
	if (Core_AttribGroup (mask, GL_POINT_BIT | GL_ENABLE_BIT))
	{
		core_state.point_sprite = a->state.point_sprite;
		core_state.point_smooth = a->state.point_smooth;
	}
	if (Core_AttribGroup (mask, GL_LIGHTING_BIT))
		core_state.shade_model = a->state.shade_model;
	if (Core_AttribGroup (mask, GL_HINT_BIT))
	{
		memcpy (core_hint_values, a->hints, sizeof (core_hint_values));
		for (i = 0; i < 4; i++)
			core_saved_Hint (core_native_hints[i], (GLenum)a->native_hints[i]);
	}
	if (Core_AttribGroup (mask, GL_ENABLE_BIT))
		memcpy (core_removed_enabled, a->removed, sizeof (core_removed_enabled));
	if (Core_AttribGroup (mask, GL_TRANSFORM_BIT))
		core_matrix_mode = a->matrix_mode;
	if (Core_AttribGroup (mask, GL_TEXTURE_BIT | GL_ENABLE_BIT))
		memcpy (core_state.texture_enabled, a->state.texture_enabled, sizeof (core_state.texture_enabled));
	if (Core_AttribGroup (mask, GL_TEXTURE_BIT))
	{
		memcpy (core_state.texenv, a->state.texenv, sizeof (core_state.texenv));
		memcpy (core_state.texenv_scale, a->state.texenv_scale, sizeof (core_state.texenv_scale));
		memcpy (core_state.point_replace, a->state.point_replace, sizeof (core_state.point_replace));
		for (unit = 0; unit < GL_CORE_TEXTURE_UNITS; unit++)
		{
			core_state.active_texture = unit;
			core_active_texture (CORE_TEXTURE0 + unit);
			for (i = 0; i < CORE_TEXTURE_TARGETS; i++)
			{
				qglBindTexture (core_texture_targets[i], (GLuint)a->textures[unit][i]);
				if (i < 7 && a->texture_params[unit][i].bound)
				{
					core_texture_params_t *p = &a->texture_params[unit][i];
					unsigned k, count = i == 6 ? 5 : 9;
					for (k = 0; k < count; k++)
						qglTexParameteri (core_texture_targets[i], core_texture_int_params[k], p->integers[k]);
					qglTexParameterfv (core_texture_targets[i], GL_TEXTURE_BORDER_COLOR, p->border);
					if (i != 6)
						for (k = 0; k < 3; k++)
							qglTexParameterf (core_texture_targets[i], core_texture_lod_params[k], p->lod[k]);
				}
			}
		}
		core_state.active_texture = a->state.active_texture;
		core_active_texture (CORE_TEXTURE0 + core_state.active_texture);
	}
	if (Core_AttribGroup (mask, GL_DEPTH_BUFFER_BIT))
	{
		qglDepthFunc ((GLenum)a->depth_func);
		qglDepthMask (a->depth_mask);
		qglClearDepth (a->clear_depth);
	}
	if (Core_AttribGroup (mask, GL_VIEWPORT_BIT))
	{
		qglViewport (a->viewport[0], a->viewport[1], a->viewport[2], a->viewport[3]);
		qglDepthRange (a->depth_range[0], a->depth_range[1]);
	}
	if (Core_AttribGroup (mask, GL_STENCIL_BUFFER_BIT))
	{
		unsigned side;
		for (side = 0; side < 2; side++)
		{
			GLenum face = side ? GL_BACK : GL_FRONT;
			GLint *s = a->stencil[side];
			core_stencil_func_separate (face, (GLenum)s[0], s[1], (GLuint)s[2]);
			core_stencil_mask_separate (face, (GLuint)s[3]);
			core_stencil_op_separate (face, (GLenum)s[4], (GLenum)s[5], (GLenum)s[6]);
		}
		qglClearStencil (a->clear_stencil);
	}
	if (Core_AttribGroup (mask, GL_POLYGON_BIT))
	{
		qglCullFace ((GLenum)a->cull_face);
		qglFrontFace ((GLenum)a->front_face);
		/* Desktop core only permits FRONT_AND_BACK and has one polygon mode. */
		qglPolygonMode (GL_FRONT_AND_BACK, (GLenum)a->polygon_mode[0]);
		qglPolygonOffset (a->polygon_factor, a->polygon_units);
	}
	if (Core_AttribGroup (mask, GL_LINE_BIT))
		qglLineWidth (a->line_width);
	if (Core_AttribGroup (mask, GL_SCISSOR_BIT))
		qglScissor (a->scissor[0], a->scissor[1], a->scissor[2], a->scissor[3]);
	if (Core_AttribGroup (mask, 0x20000000))
		core_sample_coverage (a->sample_coverage, a->sample_invert);
	for (i = 0; i < CORE_ENABLE_COUNT; i++)
	{
		if (core_enables[i].cap >= 0x3000 && core_enables[i].cap <= 0x3007 &&
			core_enables[i].cap - 0x3000 >= core_clip_distances)
			continue;
		if (Core_AttribGroup (mask, GL_ENABLE_BIT | core_enables[i].group))
		{
			if (a->enabled[i])
				core_saved_Enable (core_enables[i].cap);
			else
				core_saved_Disable (core_enables[i].cap);
		}
	}
}

#define CORE_STATE_SLOTS(X) \
	X(Enable, Enable) X(Disable, Disable) X(IsEnabled, IsEnabled) \
	X(GetFloatv, GetFloatv) X(GetDoublev, GetDoublev) X(GetIntegerv, GetIntegerv) X(GetBooleanv, GetBooleanv) \
	X(MatrixMode, MatrixMode) X(LoadIdentity, LoadIdentity) X(LoadMatrixf, LoadMatrixf) X(LoadMatrixd, LoadMatrixd) \
	X(MultMatrixf, MultMatrixf) X(MultMatrixd, MultMatrixd) X(PushMatrix, PushMatrix) X(PopMatrix, PopMatrix) \
	X(Translatef, Translatef) X(Translated, Translated) X(Scalef, Scalef) X(Scaled, Scaled) \
	X(Rotatef, Rotatef) X(Rotated, Rotated) X(Ortho, Ortho) X(Frustum, Frustum) \
	X(AlphaFunc, AlphaFunc) X(ShadeModel, ShadeModel) X(Hint, Hint) \
	X(TexEnvi, TexEnvi) X(TexEnvf, TexEnvf) X(TexEnviv, TexEnviv) X(TexEnvfv, TexEnvfv) \
	X(GetTexEnviv, GetTexEnviv) X(GetTexEnvfv, GetTexEnvfv) X(PointSize, PointSize) \
	X(PointParameterfEXT, PointParameterf) X(PointParameterfvEXT, PointParameterfv) \
	X(PointParameterfARB, PointParameterf) X(PointParameterfvARB, PointParameterfv) \
	X(ActiveTextureARB, ActiveTexture) X(ClientActiveTextureARB, ClientActiveTexture) \
	X(SelectTextureSGIS, SelectTexture) X(MTexCoord2fSGIS, MultiTexCoord2f) X(MTexCoord2fvSGIS, MultiTexCoord2fv) \
	X(PushAttrib, PushAttrib) X(PopAttrib, PopAttrib)

#define CORE_COLOR_SLOTS(X, suffix) \
	X(Color3##suffix, Color3##suffix) X(Color3##suffix##v, Color3##suffix##v) \
	X(Color4##suffix, Color4##suffix) X(Color4##suffix##v, Color4##suffix##v)
#define CORE_ALL_COLOR_SLOTS(X) \
	CORE_COLOR_SLOTS(X, f) CORE_COLOR_SLOTS(X, d) CORE_COLOR_SLOTS(X, b) CORE_COLOR_SLOTS(X, s) \
	CORE_COLOR_SLOTS(X, i) CORE_COLOR_SLOTS(X, ub) CORE_COLOR_SLOTS(X, us) CORE_COLOR_SLOTS(X, ui)
#define CORE_TEXCOORD_SLOTS(X, suffix) \
	X(TexCoord1##suffix, TexCoord1##suffix) X(TexCoord1##suffix##v, TexCoord1##suffix##v) \
	X(TexCoord2##suffix, TexCoord2##suffix) X(TexCoord2##suffix##v, TexCoord2##suffix##v) \
	X(TexCoord3##suffix, TexCoord3##suffix) X(TexCoord3##suffix##v, TexCoord3##suffix##v) \
	X(TexCoord4##suffix, TexCoord4##suffix) X(TexCoord4##suffix##v, TexCoord4##suffix##v)
#define CORE_ALL_TEXCOORD_SLOTS(X) \
	CORE_TEXCOORD_SLOTS(X, f) CORE_TEXCOORD_SLOTS(X, d) CORE_TEXCOORD_SLOTS(X, i) CORE_TEXCOORD_SLOTS(X, s)

void GL_CoreStateInstall (void)
{
	unsigned i;
	GLint max_clips = 0;
	GLfloat point_range[2] = { 0, 0 };
	if (core_state_installed)
		return;
	core_active_texture = (void (APIENTRY *) (GLenum))GL_CoreDriverProc ("glActiveTexture");
	core_blend_func_separate = (void (APIENTRY *) (GLenum, GLenum, GLenum, GLenum))GL_CoreDriverProc ("glBlendFuncSeparate");
	core_blend_equation_separate = (void (APIENTRY *) (GLenum, GLenum))GL_CoreDriverProc ("glBlendEquationSeparate");
	core_blend_color = (void (APIENTRY *) (GLclampf, GLclampf, GLclampf, GLclampf))GL_CoreDriverProc ("glBlendColor");
	core_stencil_func_separate = (void (APIENTRY *) (GLenum, GLenum, GLint, GLuint))GL_CoreDriverProc ("glStencilFuncSeparate");
	core_stencil_op_separate = (void (APIENTRY *) (GLenum, GLenum, GLenum, GLenum))GL_CoreDriverProc ("glStencilOpSeparate");
	core_stencil_mask_separate = (void (APIENTRY *) (GLenum, GLuint))GL_CoreDriverProc ("glStencilMaskSeparate");
	core_sample_coverage = (void (APIENTRY *) (GLclampf, GLboolean))GL_CoreDriverProc ("glSampleCoverage");
	core_get_attachment = (void (APIENTRY *) (GLenum, GLenum, GLenum, GLint *))GL_CoreDriverProc ("glGetFramebufferAttachmentParameteriv");
	if (!core_active_texture || !core_blend_func_separate || !core_blend_equation_separate ||
		!core_blend_color || !core_stencil_func_separate || !core_stencil_op_separate ||
		!core_stencil_mask_separate || !core_sample_coverage || !core_get_attachment)
		return;
	memset (&core_state, 0, sizeof (core_state));
	memset (core_matrix_depth, 0, sizeof (core_matrix_depth));
	memset (core_removed_enabled, 0, sizeof (core_removed_enabled));
	Core_Identity (core_state.modelview);
	Core_Identity (core_state.projection);
	for (i = 0; i < GL_CORE_TEXTURE_UNITS; i++)
	{
		Core_Identity (core_state.texture[i]);
		core_state.texcoord[i][3] = 1;
		core_state.texenv[i] = GL_MODULATE;
		core_state.texenv_scale[i] = 1;
	}
	core_state.color[0] = core_state.color[1] = core_state.color[2] = core_state.color[3] = 1;
	core_state.alpha_func = GL_ALWAYS;
	core_state.shade_model = GL_SMOOTH;
	qglGetFloatv (0x846D, point_range); /* ALIASED_POINT_SIZE_RANGE */
	core_state.point_size = core_state.point_fade = 1;
	core_state.point_max = Core_Finite (point_range[1]) && point_range[1] > 0 ? point_range[1] : 1;
	core_state.point_attenuation[0] = 1;
	core_matrix_mode = GL_MODELVIEW;
	core_attrib_depth = 0;
	for (i = 0; i < 3; i++)
		core_hint_values[i] = GL_DONT_CARE;
	qglGetIntegerv (0x0D32, &max_clips);
	core_clip_distances = max_clips > 0 ? (unsigned)max_clips : 0;
	if (core_clip_distances > 8)
		core_clip_distances = 8;
#define CORE_INSTALL(name, function) core_saved_##name = qgl##name; qgl##name = Core_##function;
	CORE_STATE_SLOTS (CORE_INSTALL)
	CORE_ALL_COLOR_SLOTS (CORE_INSTALL)
	CORE_ALL_TEXCOORD_SLOTS (CORE_INSTALL)
#undef CORE_INSTALL
	/* R1Q2 prefers the SGIS selector if present; core uses the ARB unit numbering. */
	qglSelectTextureSGIS = NULL;
	core_active_texture (CORE_TEXTURE0);
	core_state_installed = true;
}

void GL_CoreStateRestore (void)
{
	if (!core_state_installed)
		return;
#define CORE_RESTORE(name, function) qgl##name = core_saved_##name;
	CORE_STATE_SLOTS (CORE_RESTORE)
	CORE_ALL_COLOR_SLOTS (CORE_RESTORE)
	CORE_ALL_TEXCOORD_SLOTS (CORE_RESTORE)
#undef CORE_RESTORE
	core_state_installed = false;
	core_attrib_depth = 0;
	memset (core_matrix_depth, 0, sizeof (core_matrix_depth));
}

const glcore_state_t *GL_CoreState (void)
{
	return &core_state;
}

void *GL_CoreStateProc (const char *name)
{
	if (!core_state_installed)
		return NULL;
	if (!strcmp (name, "glActiveTexture") || !strcmp (name, "glActiveTextureARB"))
		return (void *)Core_ActiveTexture;
	if (!strcmp (name, "glClientActiveTexture") || !strcmp (name, "glClientActiveTextureARB"))
		return (void *)Core_ClientActiveTexture;
	if (!strcmp (name, "glMultiTexCoord2f") || !strcmp (name, "glMultiTexCoord2fARB"))
		return (void *)Core_MultiTexCoord2f;
	if (!strcmp (name, "glMultiTexCoord2fv") || !strcmp (name, "glMultiTexCoord2fvARB"))
		return (void *)Core_MultiTexCoord2fv;
	if (!strcmp (name, "glSelectTextureSGIS"))
		return (void *)Core_SelectTexture;
	if (!strcmp (name, "glMTexCoord2fSGIS"))
		return (void *)Core_MTexCoord2fSGIS;
	if (!strcmp (name, "glMTexCoord2fvSGIS"))
		return (void *)Core_MTexCoord2fvSGIS;
	if (!strcmp (name, "glPointParameterf") || !strcmp (name, "glPointParameterfARB") ||
		!strcmp (name, "glPointParameterfEXT"))
		return (void *)Core_PointParameterf;
	if (!strcmp (name, "glPointParameterfv") || !strcmp (name, "glPointParameterfvARB") ||
		!strcmp (name, "glPointParameterfvEXT"))
		return (void *)Core_PointParameterfv;
	return NULL;
}

#else

void GL_CoreStateInstall (void) {}

void GL_CoreStateRestore (void) {}

const glcore_state_t *GL_CoreState (void)
{
	return NULL;
}

void *GL_CoreStateProc (const char *name)
{
	(void)name;
	return NULL;
}

#endif
