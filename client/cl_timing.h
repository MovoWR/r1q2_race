#ifndef CL_TIMING_H
#define CL_TIMING_H

/* Diagnostic flags; these never control packet scheduling. */
#define CL_TIMING_STRINGCMD 1u
#define CL_TIMING_ATTACK 2u
#define CL_TIMING_USE 4u
#define CL_TIMING_WAIT 8u
#define CL_TIMING_USERINFO 16u
#define CL_TIMING_REQUEST 32u
#define CL_TIMING_SCHEDULED 64u
#define CL_TIMING_TIMEDEMO 128u
#define CL_TIMING_SYNC 256u

void CL_TimingInit (void);
void CL_TimingFrame (int packet_msec, qboolean scheduled);
void CL_TimingDecision (qboolean requested, qboolean userinfo);
void CL_TimingTrigger (unsigned reason);
void CL_TimingRefresh (void);
void CL_TimingKey (uint32 timestamp);
void CL_TimingRender (qboolean begin);
int CL_TimingTransmit (int length, const byte *data, const usercmd_t *cmd);
void CL_TimingEnd (void);

#endif
