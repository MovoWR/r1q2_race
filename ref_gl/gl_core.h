/* Renderer-private OpenGL core profile. No public renderer table changes. */
#ifndef R1Q2_GL_CORE_H
#define R1Q2_GL_CORE_H

#define GL_CORE_TEXTURE_UNITS 4

typedef struct
{
	GLfloat modelview[16], projection[16], texture[GL_CORE_TEXTURE_UNITS][16];
	GLfloat color[4], texcoord[GL_CORE_TEXTURE_UNITS][4];
	GLenum texenv[GL_CORE_TEXTURE_UNITS];
	GLfloat texenv_scale[GL_CORE_TEXTURE_UNITS];
	GLboolean texture_enabled[GL_CORE_TEXTURE_UNITS], alpha_test, point_sprite;
	GLboolean point_replace[GL_CORE_TEXTURE_UNITS];
	GLboolean point_smooth;
	GLenum alpha_func;
	GLenum shade_model;
	GLfloat alpha_ref, point_size, point_min, point_max, point_fade, point_attenuation[3];
	unsigned active_texture, client_texture;
} glcore_state_t;

qboolean GL_CoreActive (void);
qboolean GL_CoreInit (void);
void GL_CoreShutdown (void);
void *GL_CoreDriverProc (const char *name);
void *GL_CoreProcAddress (const char *name);

/* State installation owns/restores only its QGL slots. */
void GL_CoreStateInstall (void);
void GL_CoreStateRestore (void);
void *GL_CoreStateProc (const char *name);
const glcore_state_t *GL_CoreState (void);
void GL_CoreStateError (const char *message);
GLenum GL_CoreTextureFormat (unsigned unit);
GLuint GL_CoreTextureName (GLuint native);
qboolean GL_CoreTextureBinding (GLenum pname, GLuint *name);

/* Shader installation owns source/link/use slots returned by the resolver. */
qboolean GL_CoreShaderInit (void);
void GL_CoreShaderShutdown (void);
void *GL_CoreShaderProc (const char *name);
qboolean GL_CoreShaderGeneric (void);
void GL_CoreShaderPointMode (GLenum mode);
GLuint GL_CoreShaderPrepare (void);
void GL_CoreShaderFinish (GLuint saved_program);

#endif
