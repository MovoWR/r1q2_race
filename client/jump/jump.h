/* Private Jump client interfaces; never part of the renderer or game DLL ABI. */
#ifndef R1Q2_JUMP_H
#define R1Q2_JUMP_H

#define JUMP_FPS_SLOTS 12
#define CL_JumpEnabled() (!Q_stricmp(Cvar_VariableString("game"), "jump"))
void CL_JumpSyncGame (void);
int CL_JumpParseInteger (const char *text);
void SCR_BindRemindersInit (void);
void SCR_DrawBindReminders (float alpha);
void SCR_PreviewBindReminders (void);

typedef struct
{
	qboolean prediction_valid, valid, efficiency_valid, on_ground;
	int waterlevel, command_msec;
	vec3_t velocity, origin, forward, wishdir, localmove;
	float wishspeed, target, budget, frametime, velocity_norm;
	float currentspeed, addspeed, forward_wish_angle;
	float optimal, minimum, maximum, current, difference, side;
	float gain, maximum_gain, efficiency;
} jump_sample_t;

extern cvar_t *cl_jump;
extern jump_sample_t cl_jump_sample;

qboolean CL_JumpParseLaser (vec3_t start, vec3_t end);
void CL_JumpAddLasers (void);
void CL_JumpClearBeams (void);
void CL_JumpSceneClear (void);
void CL_JumpSceneEntity (int index);
void CL_JumpRegisterWidgets (void);
void CL_JumpClearWidgets (void);
void CL_JumpDrawWidgets (void);
qboolean CL_JumpMacro (const char *name, char *out, int size);
void CL_JumpClientFrame (void);
void CL_JumpDemoFrame (void);
void CL_JumpRegisterCommands (void);
void CL_JumpInit (void);
void CL_JumpUpdateInput (void);
void CL_JumpClearInput (void);
void CL_JumpClearDraw (void);
void CL_JumpDrawHUD (void);
qboolean CL_JumpMuteSound (const char *name, int entnum);
void CL_JumpClearNetwork (void);
void CL_JumpPacketSent (void);
void CL_JumpMoveFrame (void);
void CL_JumpRenderFrame (void);
void CL_JumpClearStats (void);
void CL_JumpNerdUpdate (void);
void SH_DebugNow_f (void);
void CL_JumpPacketSample (void);
void CL_JumpClear (void);
void CL_JumpResetPrediction (void);
void CL_JumpBeginCommand (qboolean capture);
void CL_JumpEndCommand (const pmove_new_t *pm);
void CL_JumpAcceleration (const vec3_t forward, const vec3_t velocity,
	const vec3_t wishdir, float wishspeed, float target, float accel,
	float frametime, qboolean on_ground, int waterlevel, qboolean ladder);
int CL_JumpLastCommand (int acknowledged, int current, qboolean asynchronous);

/* Private racing UI; keep full widget macros out of native engine headers. */
void CL_JumpUIFrame (void);
void CL_JumpUIMenuRegister (void);
qboolean CL_JumpUIActive (void);
qboolean CL_JumpUIKey (int key, qboolean down);
qboolean CL_JumpUIChar (int key);
void CL_JumpUIMouse (int x, int y);
qboolean CL_JumpUIMain (void);
void CL_JumpUIClear (void);
#endif
