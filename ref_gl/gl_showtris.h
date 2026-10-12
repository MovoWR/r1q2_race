/* Private diagnostic overlays, using Q2PRO's gl_showtris category bits. */
#ifndef R1GL_SHOWTRIS_H
#define R1GL_SHOWTRIS_H

#define GL_SHOWTRIS_WORLD 1
#define GL_SHOWTRIS_MESH 2
#define GL_SHOWTRIS_PIC 4
#define GL_SHOWTRIS_FX 8

qboolean GL_ShowTrisEnabled (int mask);
qboolean GL_ShowTrisActive (void);
qboolean GL_ShowTrisBegin (int mask);
void GL_ShowTrisEnd (void);
void GL_ShowTrisElements (int mask, GLenum mode, GLsizei count,
	GLenum type, const void *indices);
void GL_ShowTrisArrays (int mask, GLenum mode, GLint first, GLsizei count);
void GL_ShowTrisVertices (int mask, GLenum mode, GLsizei count,
	const void *vertices, GLsizei stride, GLint components);
void GL_SetWorldShowTrisDeferred (qboolean deferred);
int GL_WorldShowTrisMask (void);
qboolean GL_JumpLavaShowTrisDeferred (const msurface_t *surface);

#endif
