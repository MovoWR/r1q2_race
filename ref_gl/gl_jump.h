/* Private Jump effects, outside the published renderer structures. */
#ifndef R1GL_JUMP_H
#define R1GL_JUMP_H
#include "../client/ref_present.h"
void EXPORT GL_JumpSetBeams (const void *entities, const refbeam_t *beams, int count);
qboolean GL_JumpDrawBeam (entity_t *entity);
qboolean GL_JumpQueueBeam (entity_t *entity);
qboolean GL_BeamQueueEntity (entity_t *entity);
qboolean GL_NativeDrawBeam (entity_t *entity);
void GL_JumpFlushBeams (void);
void GL_JumpResetBeams (void);
qboolean GL_JumpLavaName (const char *name, int length);
void GL_JumpLavaBegin (void);
void GL_JumpLavaSurface (msurface_t *surface);
void GL_JumpLavaDraw (void);
void GL_JumpLavaReset (void);
void GL_JumpDrawOrigin (void);
void GL_JumpReset (void);
#endif
