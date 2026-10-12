/* Private server browser interface; public engine/DLL layouts are unchanged. */
#ifndef JUMP_SERVERS_H
#define JUMP_SERVERS_H
#include "../../qcommon/qcommon.h"

#define JUMP_SERVERS_MAX 1024
#define JUMP_SERVER_RULES 64
#define JUMP_SERVER_PLAYERS 64
#define JUMP_SERVER_ADDRESS 256

typedef enum
{
	JUMP_SERVERS_JUMP,
	JUMP_SERVERS_ALL,
	JUMP_SERVERS_BOOK
} jump_servers_tab_t;

typedef enum
{
	JUMP_SERVER_IDLE,
	JUMP_SERVER_RESOLVING,
	JUMP_SERVER_PENDING,
	JUMP_SERVER_VALID,
	JUMP_SERVER_FAILED
} jump_server_state_t;

typedef struct
{
	unsigned id;
	jump_server_state_t state;
	char address[JUMP_SERVER_ADDRESS]; /* original validated domain/address */
	char numeric[32];
	char name[128], mod[64], map[64];
	unsigned ping;
	int players, max_players;
	qboolean favorite, jump, restricted;
} jump_server_row_t;

typedef struct
{
	const char *key, *value;
} jump_server_rule_t;

typedef struct
{
	int score, ping;
	char name[32];
} jump_server_player_t;

typedef struct
{
	qboolean open, busy, truncated;
	jump_servers_tab_t tab;
	unsigned generation, revision;
	int total, visible, valid, pending, fetching, resolving, errors;
	char message[128];
} jump_servers_snapshot_t;

void CL_JumpServersInit (void);
void CL_JumpServersOpen (jump_servers_tab_t tab, const char *sources);
void CL_JumpServersRefresh (void);
void CL_JumpServersAbort (void);
void CL_JumpServersClose (void);
void CL_JumpServersFrame (void);
void CL_JumpServersSnapshot (jump_servers_snapshot_t *out);
int CL_JumpServersCount (void);
/* Pointers are read-only, valid until the next frame/refresh/close. IDs survive sort. */
const jump_server_row_t *CL_JumpServersRow (int index);
const jump_server_row_t *CL_JumpServersById (unsigned id);
int CL_JumpServersRules (unsigned id, const jump_server_rule_t **out);
int CL_JumpServersPlayers (unsigned id, const jump_server_player_t **out);
void CL_JumpServersSort (int column); /* 0 insertion, +/-1..5 name/mod/map/players/ping */
qboolean CL_JumpServersRefreshSelected (unsigned id);
qboolean CL_JumpServersConnect (unsigned id);
qboolean CL_JumpServersCopyAddress (unsigned id, char *out, size_t size);
qboolean CL_JumpServersSetRconAddress (unsigned id);
qboolean CL_JumpServersSaveFavorite (unsigned id);
qboolean CL_JumpServersRemoveFavorite (unsigned id);
/* Only outstanding exact IPv4+port status requests may bypass legacy routing. */
qboolean CL_JumpServersStatus (const netadr_t *from, const byte *data, size_t size);
/* Call only inside the existing native info acceptance gate; does not consume it. */
void CL_JumpServersInfo (const netadr_t *from);
#endif
