/* Private R1GL HQ2x/HQ4x adapter; no engine or renderer API additions. */
#ifndef R1GL_HQX_H
#define R1GL_HQX_H

#include "../game/q_shared.h"

void GL_InitHQx (float y, float cb, float cr);
void GL_HQ2x (uint32 *output, const uint32 *input, int width, int height);
void GL_HQ4x (uint32 *output, const uint32 *input, int width, int height);

#endif
