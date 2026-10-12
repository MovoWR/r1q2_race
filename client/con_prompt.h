/* Private console input; no public client or renderer ABI changes. */
#ifndef R1Q2_CON_PROMPT_H
#define R1Q2_CON_PROMPT_H

#define CON_HISTORY_SIZE 128
#define CON_HISTORY_MASK (CON_HISTORY_SIZE - 1)

typedef struct
{
	char text[MAXCMDLINE];
	int cursor;
} con_field_t;

typedef struct
{
	con_field_t field;
	char history[CON_HISTORY_SIZE][MAXCMDLINE];
	char draft[MAXCMDLINE], search[MAXCMDLINE];
	unsigned head, position, count;
	qboolean too_many, searching;
} con_prompt_t;

void Con_FieldReplace (con_field_t *field, const char *text);
qboolean Con_FieldKey (con_field_t *field, int key);
void Con_FieldChar (con_field_t *field, int key);
void Con_PromptClearState (con_prompt_t *prompt);
void Con_PromptHistory (con_prompt_t *prompt, int direction, qboolean search);
qboolean Con_PromptAction (con_prompt_t *prompt, char *text);
void Con_PromptComplete (con_prompt_t *prompt, int width);
void Con_PromptSave (const con_prompt_t *prompt, const char *path, int lines);
qboolean Con_PromptLoad (con_prompt_t *prompt, const char *path);

#endif
