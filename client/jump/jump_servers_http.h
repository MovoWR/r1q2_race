/* Engine-free bounded platform work; consumed only by the client frame loop. */
#ifndef JUMP_SERVERS_HTTP_H
#define JUMP_SERVERS_HTTP_H
#include <stddef.h>
#define JUMP_SERVERS_FETCH_JOBS 4
#define JUMP_SERVERS_FETCH_BYTES (128 * 1024)
#define JUMP_SERVERS_FETCH_TIMEOUT 20000u

typedef struct
{
	unsigned token, error;
	int ok, status;
	const unsigned char *data;
	size_t size;
	int slot; /* release after parsing; no pointer survives release */
} jump_servers_http_result_t;

typedef struct
{
	unsigned token, error;
	int ok;
	unsigned char ip[4];
} jump_servers_dns_result_t;

void CL_JumpServersHTTPInit (void);
int CL_JumpServersHTTPStart (unsigned token, const char *url, unsigned now);
int CL_JumpServersDNSStart (unsigned token, const char *host, unsigned now);
void CL_JumpServersHTTPStep (unsigned now);
int CL_JumpServersHTTPPoll (jump_servers_http_result_t *out);
void CL_JumpServersHTTPRelease (int slot);
int CL_JumpServersDNSPoll (jump_servers_dns_result_t *out);
void CL_JumpServersHTTPCancel (void);
/* Start returns 1 accepted, 0 pool busy, -1 unavailable/invalid. */
#endif
