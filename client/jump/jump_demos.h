/* Native DM2 browser; metadata reads do not execute recorded messages. */
#ifndef R1Q2_JUMP_DEMOS_H
#define R1Q2_JUMP_DEMOS_H
#include "../../qcommon/qcommon.h"
#define JUMP_DEMOS_MAX 4096

typedef struct
{
	char name[MAX_QPATH], map[64], pov[32];
	int size, protocol;
	qboolean directory, playable;
} jump_demo_row_t;

qboolean CL_JumpDemosOpen (const char *directory);
void CL_JumpDemosClose (void);
const char *CL_JumpDemosDirectory (void);
int CL_JumpDemosCount (void);
const jump_demo_row_t *CL_JumpDemosRow (int index);
qboolean CL_JumpDemosSelect (int index);
void CL_JumpDemosSort (int column);
#endif
