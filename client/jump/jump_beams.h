/* Scene helpers require the native client entity type; movement does not. */
#ifndef R1Q2_JUMP_BEAMS_H
#define R1Q2_JUMP_BEAMS_H
#include "jump.h"
void CL_JumpSortScene (entity_t *entities, int count);
void CL_JumpPublishScene (const entity_t *entities, int count);
#endif
