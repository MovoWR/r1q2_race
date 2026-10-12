/* Engine-free Windows asynchronous HTTP and bounded hostname jobs.
 * Callbacks/workers never call Cvar, FS, NET, commands, or engine logging.
 * Fixed contexts remain alive through HANDLE_CLOSING / worker completion.
 */
#include "jump_servers_http.h"
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winsock.h>
#include <winhttp.h>

#define JH_READ_BYTES 8192
#define JH_URL_CHARS 1024

typedef enum
{
	JH_FREE,
	JH_SEND,
	JH_WAIT_SEND,
	JH_RECEIVE,
	JH_WAIT_HEADERS,
	JH_HEADERS,
	JH_READ,
	JH_WAIT_READ,
	JH_COPY,
	JH_END,
	JH_CLOSING,
	JH_READY,
	JH_DELIVERED
} jh_state_t;

typedef struct
{
	jh_state_t state;
	HINTERNET session, connection, request;
	unsigned token, started, error;
	int status, ok, closed, discard;
	DWORD read_count;
	size_t size;
	unsigned char read[JH_READ_BYTES], body[JUMP_SERVERS_FETCH_BYTES];
} jh_job_t;

typedef struct
{
	int used, done, discard, reported, timeout, ok;
	unsigned token, started, error;
	char host[256];
	unsigned char ip[4];
} jh_dns_t;

static jh_job_t jh_jobs[JUMP_SERVERS_FETCH_JOBS];
static jh_dns_t jh_dns[JUMP_SERVERS_FETCH_JOBS];
static CRITICAL_SECTION jh_lock;
static int jh_initialized, jh_available;
static HMODULE jh_library;

#define JH_PROC(name, result, args) static result (WINAPI *jh_##name) args
JH_PROC (Open, HINTERNET, (LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD));
JH_PROC (Connect, HINTERNET, (HINTERNET, LPCWSTR, INTERNET_PORT, DWORD));
JH_PROC (OpenRequest, HINTERNET, (HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD));
JH_PROC (CrackUrl, BOOL, (LPCWSTR, DWORD, DWORD, LPURL_COMPONENTS));
JH_PROC (SetOption, BOOL, (HINTERNET, DWORD, LPVOID, DWORD));
JH_PROC (SetTimeouts, BOOL, (HINTERNET, int, int, int, int));
JH_PROC (SetStatusCallback, WINHTTP_STATUS_CALLBACK, (HINTERNET, WINHTTP_STATUS_CALLBACK, DWORD, DWORD_PTR));
JH_PROC (SendRequest, BOOL, (HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR));
JH_PROC (ReceiveResponse, BOOL, (HINTERNET, LPVOID));
JH_PROC (QueryHeaders, BOOL, (HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD));
JH_PROC (ReadData, BOOL, (HINTERNET, LPVOID, DWORD, LPDWORD));
JH_PROC (CloseHandle, BOOL, (HINTERNET));
#undef JH_PROC

static void CALLBACK JH_Callback (HINTERNET handle, DWORD_PTR context,
	DWORD status, LPVOID information, DWORD length)
{
	jh_job_t *job;

	job = (jh_job_t *)context;
	if (!job)
		return;
	EnterCriticalSection (&jh_lock);
	if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
		job->closed = 1;
	else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && job->state < JH_END)
	{
		job->error = information && length >= sizeof (WINHTTP_ASYNC_RESULT) ?
			((WINHTTP_ASYNC_RESULT *)information)->dwError :
			ERROR_INVALID_DATA;
		job->state = JH_END;
	}
	else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && job->state == JH_WAIT_SEND)
		job->state = JH_RECEIVE;
	else if (status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE && job->state == JH_WAIT_HEADERS)
		job->state = JH_HEADERS;
	else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE && job->state == JH_WAIT_READ)
	{
		if ((length && information != job->read) || length > sizeof (job->read))
		{
			job->error = ERROR_INVALID_DATA;
			job->state = JH_END;
		}
		else
		{
			job->read_count = length;
			job->state = JH_COPY;
		}
	}
	LeaveCriticalSection (&jh_lock);
	/* No context access after unlocking: HANDLE_CLOSING permits slot reuse. */
}

void CL_JumpServersHTTPInit (void)
{
	char path[MAX_PATH];
	UINT length;

	if (jh_initialized)
		return;
	jh_initialized = 1;
	InitializeCriticalSection (&jh_lock);
	length = GetSystemDirectoryA (path, sizeof (path));
	if (!length || length >= sizeof (path) || length + sizeof ("\\winhttp.dll") > sizeof (path))
		return;
	strcat (path, "\\winhttp.dll");
	jh_library = LoadLibraryA (path);
	if (!jh_library)
		return;
#define JH_LOAD(name) do { *(FARPROC *)&jh_##name = GetProcAddress (jh_library, "WinHttp" #name); if (!jh_##name) return; } while (0)
	JH_LOAD (Open);
	JH_LOAD (Connect);
	JH_LOAD (OpenRequest);
	JH_LOAD (CrackUrl);
	JH_LOAD (SetOption);
	JH_LOAD (SetTimeouts);
	JH_LOAD (SetStatusCallback);
	JH_LOAD (SendRequest);
	JH_LOAD (ReceiveResponse);
	JH_LOAD (QueryHeaders);
	JH_LOAD (ReadData);
	JH_LOAD (CloseHandle);
#undef JH_LOAD
	jh_available = 1;
	/* System module/lock persist for process lifetime; cancellation is asynchronous. */
}

static void JH_Close (jh_job_t *job)
{
	HINTERNET request;
	DWORD error;

	EnterCriticalSection (&jh_lock);
	request = job->request;
	job->request = NULL;
	job->state = JH_CLOSING;
	LeaveCriticalSection (&jh_lock);
	if (!request)
		return;
	/* Only the main thread calls APIs. Never hold our lock across an API call. */
	if (!jh_CloseHandle (request))
	{
		error = GetLastError ();
		EnterCriticalSection (&jh_lock);
		job->error = error;
		job->ok = 0;
		LeaveCriticalSection (&jh_lock);
		/* Retain/quarantine context if no final callback arrives. */
	}
}

int CL_JumpServersHTTPStart (unsigned token, const char *url, unsigned now)
{
	WCHAR wide[JH_URL_CHARS], host[JH_URL_CHARS], target[JH_URL_CHARS];
	URL_COMPONENTS parts;
	jh_job_t *job;
	DWORD_PTR context;
	DWORD flags;
	int i, count;
	size_t path_length;

	CL_JumpServersHTTPInit ();
	if (!jh_available || !url || !token || strlen (url) >= JH_URL_CHARS)
		return -1;
	memset (&parts, 0, sizeof (parts));
	parts.dwStructSize = sizeof (parts);
	parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = (DWORD)-1;
	parts.dwUserNameLength = parts.dwPasswordLength = (DWORD)-1;
	count = MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, wide, JH_URL_CHARS);
	if (!count || !jh_CrackUrl (wide, 0, 0, &parts) ||
		(parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) ||
		!parts.dwHostNameLength || parts.dwHostNameLength >= JH_URL_CHARS ||
		parts.dwUserNameLength || parts.dwPasswordLength)
		return -1;
	path_length = parts.dwUrlPathLength + parts.dwExtraInfoLength;
	if (path_length >= JH_URL_CHARS)
		return -1;
	memcpy (host, parts.lpszHostName, parts.dwHostNameLength * sizeof (WCHAR));
	host[parts.dwHostNameLength] = 0;
	if (parts.dwUrlPathLength)
		memcpy (target, parts.lpszUrlPath, parts.dwUrlPathLength * sizeof (WCHAR));
	if (parts.dwExtraInfoLength)
		memcpy (target + parts.dwUrlPathLength, parts.lpszExtraInfo, parts.dwExtraInfoLength * sizeof (WCHAR));
	target[path_length] = 0;
	if (!path_length)
	{
		target[0] = L'/';
		target[1] = 0;
	}
	EnterCriticalSection (&jh_lock);
	for (i = 0; i < JUMP_SERVERS_FETCH_JOBS; i++)
		if (jh_jobs[i].state == JH_FREE)
			break;
	if (i == JUMP_SERVERS_FETCH_JOBS)
	{
		LeaveCriticalSection (&jh_lock);
		return 0;
	}
	job = &jh_jobs[i];
	memset (job, 0, sizeof (*job));
	job->token = token;
	job->started = now;
	job->state = JH_SEND;
	LeaveCriticalSection (&jh_lock);
	job->session = jh_Open (L"R1Q2 server browser", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
	if (!job->session)
		goto failed;
	if (!jh_SetTimeouts (job->session, 5000, 5000, 5000, 5000))
		goto failed;
	job->connection = jh_Connect (job->session, host, parts.nPort, 0);
	if (!job->connection)
		goto failed;
	flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
	job->request = jh_OpenRequest (job->connection, L"GET", target, NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!job->request)
		goto failed;
	context = (DWORD_PTR)job;
	if (!jh_SetOption (job->request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof (context)))
		goto failed;
	if (jh_SetStatusCallback (job->request, JH_Callback,
			WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
		goto failed;
	return 1;
failed:
	job->error = GetLastError ();
	/* No asynchronous operation or installed callback exists along these failures. */
	if (job->request)
		jh_CloseHandle (job->request);
	if (job->connection)
		jh_CloseHandle (job->connection);
	if (job->session)
		jh_CloseHandle (job->session);
	job->request = job->connection = job->session = NULL;
	job->closed = 1;
	job->state = JH_READY;
	return 1; /* report setup failure through the same bounded result queue */
}

static DWORD WINAPI JH_Resolve (LPVOID context)
{
	jh_dns_t *job;
	WSADATA data;
	struct hostent *entry;
	unsigned error;
	unsigned char ip[4];
	int ok, startup, discard;

	job = context;
	EnterCriticalSection (&jh_lock);
	discard = job->discard;
	LeaveCriticalSection (&jh_lock);
	ok = 0;
	error = ERROR_OPERATION_ABORTED;
	memset (ip, 0, sizeof (ip));
	if (!discard)
	{
		startup = WSAStartup (MAKEWORD (1, 1), &data);
		error = startup;
		if (!startup)
		{
			entry = gethostbyname (job->host);
			if (entry && entry->h_addrtype == AF_INET && entry->h_length == 4 && entry->h_addr_list && entry->h_addr_list[0])
			{
				memcpy (ip, entry->h_addr_list[0], sizeof (ip));
				ok = 1;
				error = 0;
			}
			else
				error = WSAGetLastError ();
			WSACleanup ();
		}
	}
	EnterCriticalSection (&jh_lock);
	job->error = error;
	job->ok = ok;
	memcpy (job->ip, ip, sizeof (ip));
	job->done = 1;
	LeaveCriticalSection (&jh_lock);
	return 0; /* no context access after publishing completion */
}

int CL_JumpServersDNSStart (unsigned token, const char *host, unsigned now)
{
	jh_dns_t *job;
	HANDLE thread;
	DWORD error;
	int i;

	CL_JumpServersHTTPInit ();
	if (!token || !host || !*host || strlen (host) >= sizeof (jh_dns[0].host))
		return -1;
	EnterCriticalSection (&jh_lock);
	for (i = 0; i < JUMP_SERVERS_FETCH_JOBS; i++)
		if (!jh_dns[i].used)
			break;
	if (i == JUMP_SERVERS_FETCH_JOBS)
	{
		LeaveCriticalSection (&jh_lock);
		return 0;
	}
	job = &jh_dns[i];
	memset (job, 0, sizeof (*job));
	job->used = 1;
	job->token = token;
	job->started = now;
	strcpy (job->host, host);
	LeaveCriticalSection (&jh_lock);
	thread = CreateThread (NULL, 0, JH_Resolve, job, 0, NULL);
	if (thread)
		CloseHandle (thread);
	else
	{
		error = GetLastError ();
		EnterCriticalSection (&jh_lock);
		job->error = error;
		job->done = 1;
		LeaveCriticalSection (&jh_lock);
	}
	return 1;
}

static void JH_StepJob (jh_job_t *job, unsigned now)
{
	jh_state_t state;
	HINTERNET request, session, connection;
	DWORD code, bytes, error;
	BOOL ok;

	EnterCriticalSection (&jh_lock);
	if (job->state == JH_FREE || job->state >= JH_READY)
	{
		LeaveCriticalSection (&jh_lock);
		return;
	}
	if (job->state < JH_END && (unsigned)(now - job->started) >= JUMP_SERVERS_FETCH_TIMEOUT)
	{
		job->error = ERROR_TIMEOUT;
		job->state = JH_END;
	}
	state = job->state;
	request = job->request;
	if (state == JH_SEND)
		job->state = JH_WAIT_SEND;
	if (state == JH_RECEIVE)
		job->state = JH_WAIT_HEADERS;
	if (state == JH_READ)
		job->state = JH_WAIT_READ;
	if (state == JH_COPY)
	{
		if (!job->read_count)
		{
			job->ok = !job->error;
			job->state = JH_END;
		}
		else if (job->read_count > sizeof (job->body) - job->size)
		{
			job->error = ERROR_FILE_TOO_LARGE;
			job->state = JH_END;
		}
		else
		{
			memcpy (job->body + job->size, job->read, job->read_count);
			job->size += job->read_count;
			job->state = JH_READ;
		}
	}
	if (state == JH_CLOSING && job->closed)
	{
		session = job->session;
		connection = job->connection;
		job->session = job->connection = NULL;
		job->state = job->discard ? JH_FREE : JH_READY;
		LeaveCriticalSection (&jh_lock);
		if (connection)
			jh_CloseHandle (connection);
		if (session)
			jh_CloseHandle (session);
		return;
	}
	LeaveCriticalSection (&jh_lock);
	if (state == JH_END)
	{
		JH_Close (job);
		return;
	}
	ok = TRUE;
	if (state == JH_SEND)
		ok = jh_SendRequest (request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
			WINHTTP_NO_REQUEST_DATA, 0, 0, (DWORD_PTR)job);
	else if (state == JH_RECEIVE)
		ok = jh_ReceiveResponse (request, NULL);
	else if (state == JH_READ)
		ok = jh_ReadData (request, job->read, sizeof (job->read), NULL);
	else if (state == JH_HEADERS)
	{
		bytes = sizeof (code);
		ok = jh_QueryHeaders (request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &code, &bytes, WINHTTP_NO_HEADER_INDEX);
		error = ok ? 0 : GetLastError ();
		EnterCriticalSection (&jh_lock);
		if (job->state != JH_HEADERS)
		{
			LeaveCriticalSection (&jh_lock);
			return;
		}
		job->status = ok ? (int)code : 0;
		if (ok && code == 200)
			job->state = JH_READ;
		else
		{
			job->error = ok ? ERROR_INVALID_DATA : error;
			job->state = JH_END;
		}
		LeaveCriticalSection (&jh_lock);
		return;
	}
	if (!ok)
	{
		error = GetLastError ();
		if (error != ERROR_IO_PENDING)
		{
			EnterCriticalSection (&jh_lock);
			job->error = error;
			job->state = JH_END;
			LeaveCriticalSection (&jh_lock);
		}
	}
}

void CL_JumpServersHTTPStep (unsigned now)
{
	int i;

	if (!jh_initialized)
		return;
	if (jh_available)
		for (i = 0; i < JUMP_SERVERS_FETCH_JOBS; i++)
			JH_StepJob (&jh_jobs[i], now);
	EnterCriticalSection (&jh_lock);
	for (i = 0; i < JUMP_SERVERS_FETCH_JOBS; i++)
	{
		if (!jh_dns[i].used)
			continue;
		if (!jh_dns[i].done && (unsigned)(now - jh_dns[i].started) >= JUMP_SERVERS_FETCH_TIMEOUT)
			jh_dns[i].timeout = 1;
		if (jh_dns[i].done && (jh_dns[i].discard || jh_dns[i].reported))
			memset (&jh_dns[i], 0, sizeof (jh_dns[i]));
	}
	LeaveCriticalSection (&jh_lock);
}

int CL_JumpServersHTTPPoll (jump_servers_http_result_t *out)
{
	int i;
	jh_job_t *job;

	if (!out || !jh_initialized)
		return 0;
	EnterCriticalSection (&jh_lock);
	for (i = 0; i < JUMP_SERVERS_FETCH_JOBS; i++)
	{
		job = &jh_jobs[i];
		if (job->state != JH_READY || job->discard)
			continue;
		out->token = job->token;
		out->error = job->error;
		out->ok = job->ok;
		out->status = job->status;
		out->data = job->body;
		out->size = job->size;
		out->slot = i;
		job->state = JH_DELIVERED;
		LeaveCriticalSection (&jh_lock);
		return 1;
	}
	LeaveCriticalSection (&jh_lock);
	return 0;
}

void CL_JumpServersHTTPRelease (int slot)
{
	if (!jh_initialized || slot < 0 || slot >= JUMP_SERVERS_FETCH_JOBS)
		return;
	EnterCriticalSection (&jh_lock);
	if (jh_jobs[slot].state == JH_DELIVERED)
		memset (&jh_jobs[slot], 0, sizeof (jh_jobs[slot]));
	LeaveCriticalSection (&jh_lock);
}

int CL_JumpServersDNSPoll (jump_servers_dns_result_t *out)
{
	int i;
	jh_dns_t *job;

	if (!out || !jh_initialized)
		return 0;
	EnterCriticalSection (&jh_lock);
	for (i = 0; i < JUMP_SERVERS_FETCH_JOBS; i++)
	{
		job = &jh_dns[i];
		if (!job->used || job->discard || job->reported || (!job->done && !job->timeout))
			continue;
		out->token = job->token;
		out->error = job->timeout ? ERROR_TIMEOUT : job->error;
		out->ok = job->done && !job->timeout && job->ok;
		memcpy (out->ip, job->ip, sizeof (out->ip));
		job->reported = 1;
		if (job->done)
			memset (job, 0, sizeof (*job));
		LeaveCriticalSection (&jh_lock);
		return 1;
	}
	LeaveCriticalSection (&jh_lock);
	return 0;
}

void CL_JumpServersHTTPCancel (void)
{
	int i, close_request;
	jh_job_t *job;

	if (!jh_initialized)
		return;
	for (i = 0; i < JUMP_SERVERS_FETCH_JOBS; i++)
	{
		job = &jh_jobs[i];
		EnterCriticalSection (&jh_lock);
		job->discard = 1;
		if (job->state >= JH_READY)
			memset (job, 0, sizeof (*job));
		else if (job->state != JH_FREE && job->state != JH_CLOSING)
			job->state = JH_END;
		close_request = job->state == JH_END;
		LeaveCriticalSection (&jh_lock);
		if (close_request)
			JH_Close (job);
	}
	EnterCriticalSection (&jh_lock);
	for (i = 0; i < JUMP_SERVERS_FETCH_JOBS; i++)
	{
		jh_dns[i].discard = 1;
		if (jh_dns[i].done)
			memset (&jh_dns[i], 0, sizeof (jh_dns[i]));
	}
	LeaveCriticalSection (&jh_lock);
}

#else
/* Legacy Unix builds retain local/numeric/LAN browsing without new libraries. */
void CL_JumpServersHTTPInit (void) {}

int CL_JumpServersHTTPStart (unsigned token, const char *url, unsigned now)
{
	return -1;
}

int CL_JumpServersDNSStart (unsigned token, const char *host, unsigned now)
{
	return -1;
}

void CL_JumpServersHTTPStep (unsigned now) {}

int CL_JumpServersHTTPPoll (jump_servers_http_result_t *out)
{
	return 0;
}

void CL_JumpServersHTTPRelease (int slot) {}

int CL_JumpServersDNSPoll (jump_servers_dns_result_t *out)
{
	return 0;
}

void CL_JumpServersHTTPCancel (void) {}
#endif
