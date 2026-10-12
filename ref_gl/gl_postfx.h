/* Renderer-private post-processing; original renderer/DLL tables are unchanged. */
#ifndef R1GL_POSTFX_H
#define R1GL_POSTFX_H

void GL_PostFXRegister (void);
void GL_InitPostFX (void);
void GL_UpdatePostFX (void);
void GL_ShutdownPostFX (void);
void GL_PostProcessView (const refdef_t *fd);
void GL_PrintPostFXInfo (void);

qboolean GL_BloomActive (void);
qboolean GL_BloomTargets (qboolean material);
void GL_BloomAbort (void);
qboolean GL_BloomBeginView (const refdef_t *fd);
void GL_BloomEndView (const refdef_t *fd);
void GL_PrintBloomInfo (void);

#endif
