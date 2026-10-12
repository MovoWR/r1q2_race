/*
Copyright (C) 2012 Andrey Nazarov

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2 of the License, or (at your option)
any later version. This program is distributed WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.
*/
/* Server browser adapted from Q2PRO Race frozen commit
 * 5571a17dbb7fb03915e7b403b1f981b7006a1a05, src/client/ui/servers.c and
 * src/client/main.c. Native connection, RCON, and channel state are untouched.
 */
#include "../client.h"
#include "jump.h"
#include "jump_servers.h"
#include "jump_servers_http.h"
#include <limits.h>

#define JS_SOURCES 32
#define JS_ARGS 2048
#define JS_STATUS_BYTES 4096
#define JS_REQUEST_TIMEOUT 2000u
#define JS_LAN_TIMEOUT 3000u
#define JS_RECORDS_PER_FRAME 128

typedef struct
{
	jump_server_rule_t rules[JUMP_SERVER_RULES];
	jump_server_player_t players[JUMP_SERVER_PLAYERS];
	char info[MAX_INFO_STRING];
	int num_rules, num_players;
} js_details_t;

typedef struct
{
	jump_server_row_t row;
	netadr_t remote;
	js_details_t *details;
	unsigned sent, dns_token;
	unsigned short port;
	int attempts;
	qboolean resolved, pending;
	char host[JUMP_SERVER_ADDRESS];
} js_slot_t;

typedef enum
{
	JS_FILE,
	JS_HTTP,
	JS_BOOK,
	JS_CVAR,
	JS_ADDRESS,
	JS_LAN
} js_kind_t;

typedef struct
{
	js_kind_t kind;
	char value[1024];
	unsigned stride, token;
	int cursor, state; /* 0 queued, 1 platform work, 2 consumed */
} js_source_t;

static js_slot_t js_slots[JUMP_SERVERS_MAX];
static int js_order[JUMP_SERVERS_MAX], js_count, js_visible, js_source_count;
static js_source_t js_sources[JS_SOURCES];
static jump_servers_snapshot_t js_snapshot;
static char js_args[JS_ARGS], js_status[JS_STATUS_BYTES + 1];
static byte js_file[JUMP_SERVERS_FETCH_BYTES];
static const byte *js_blob;
static size_t js_blob_size, js_blob_cursor;
static int js_blob_source = -1, js_blob_http = -1, js_sort;
static unsigned js_serial, js_last_send, js_lan_sent;
static qboolean js_initialized, js_dirty, js_sent, js_lan_pending, js_lan_active;
static cvar_t *js_pingrate, *js_sortvar, *js_colorvar;

static unsigned JS_Next (void)
{
	if (!++js_serial)
		++js_serial;
	return js_serial;
}

static void JS_Copy (char *out, size_t size, const char *text)
{
	size_t n;

	if (!size)
		return;
	n = strlen (text);
	if (n >= size)
		n = size - 1;
	memcpy (out, text, n);
	out[n] = 0;
}

static void JS_Error (const char *text)
{
	js_snapshot.errors++;
	JS_Copy (js_snapshot.message, sizeof (js_snapshot.message), text);
	js_dirty = true;
}

static qboolean JS_Exact (const netadr_t *a, const netadr_t *b)
{
	return a->type == b->type && a->port == b->port &&
		!memcmp (a->ip, b->ip, sizeof (a->ip));
}

static unsigned short JS_Port (unsigned value)
{
	byte bytes[2];
	unsigned short port;

	bytes[0] = (byte)(value >> 8);
	bytes[1] = (byte)value;
	memcpy (&port, bytes, sizeof (port));
	return port;
}

static qboolean JS_Unicast (const netadr_t *address)
{
	return address->type == NA_IP && address->port &&
		address->ip[0] && address->ip[0] < 224;
}

/* Restrict all command/cvar addresses to IPv4 or DNS labels and a decimal port. */
static qboolean JS_Address (const char *text, char *host, char *canonical,
	unsigned short *port, netadr_t *numeric, qboolean *resolved)
{
	const char *colon, *p;
	size_t length, label;
	unsigned value, segment, port_digits, port_value;
	int i, digits;
	qboolean ip;

	length = strlen (text);
	if (!length || length >= JUMP_SERVER_ADDRESS)
		return false;
	colon = strchr (text, ':');
	if (colon && strchr (colon + 1, ':'))
		return false;
	length = colon ? (size_t)(colon - text) : length;
	if (!length || length > 253)
		return false;
	value = PORT_SERVER;
	if (colon)
	{
		value = 0;
		for (p = colon + 1; *p; p++)
		{
			if (*p < '0' || *p > '9' || value > (65535u - (*p - '0')) / 10)
				return false;
			value = value * 10 + (*p - '0');
		}
		if (!value)
			return false;
	}
	port_digits = 1;
	for (port_value = value; port_value >= 10; port_value /= 10)
		port_digits++;
	if (length + 1 + port_digits >= JUMP_SERVER_ADDRESS)
		return false;
	memcpy (host, text, length);
	host[length] = 0;
	label = 0;
	ip = true;
	for (i = 0; i < (int)length; i++)
	{
		char c = host[i];
		if (c >= 'A' && c <= 'Z')
			host[i] = c += 'a' - 'A';
		if (c != '.' && (c < '0' || c > '9'))
			ip = false;
		if (c == '.')
		{
			if (!label || host[i - 1] == '-')
				return false;
			label = 0;
		}
		else
		{
			if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') ||
				(!label && c == '-') || ++label > 63)
				return false;
		}
	}
	if (!label || host[length - 1] == '-')
		return false;
	memset (numeric, 0, sizeof (*numeric));
	numeric->type = NA_IP;
	*port = numeric->port = JS_Port (value);
	*resolved = false;
	if (!strcmp (host, "localhost"))
	{
		numeric->ip[0] = 127;
		numeric->ip[3] = 1;
		*resolved = true;
	}
	else if (ip)
	{
		p = host;
		for (i = 0; i < 4; i++)
		{
			segment = 0;
			digits = 0;
			while (*p >= '0' && *p <= '9')
			{
				segment = segment * 10 + (*p++ - '0');
				if (++digits > 3 || segment > 255)
					return false;
			}
			if (!digits || (i < 3 ? *p++ != '.' : *p != 0))
				return false;
			numeric->ip[i] = (byte)segment;
		}
		*resolved = true;
	}
	if (*resolved && !JS_Unicast (numeric))
		return false;
	Com_sprintf (canonical, JUMP_SERVER_ADDRESS, "%s:%u", host, value);
	return true;
}

static js_slot_t *JS_ById (unsigned id)
{
	int i;

	for (i = 0; i < js_count; i++)
		if (js_slots[i].row.id == id)
			return &js_slots[i];
	return NULL;
}

static void JS_FreeDetails (js_slot_t *slot)
{
	if (slot->details)
		Z_Free (slot->details);
	slot->details = NULL;
}

static void JS_Add (const char *text, qboolean favorite)
{
	char host[JUMP_SERVER_ADDRESS], canonical[JUMP_SERVER_ADDRESS];
	netadr_t remote;
	unsigned short port;
	qboolean resolved;
	int i;
	js_slot_t *slot;

	if (!JS_Address (text, host, canonical, &port, &remote, &resolved))
		return;
	for (i = 0; i < js_count; i++)
	{
		slot = &js_slots[i];
		if (!strcmp (canonical, slot->row.address) ||
			(resolved && slot->resolved && JS_Exact (&remote, &slot->remote)))
		{
			slot->row.favorite |= favorite;
			js_dirty = true;
			return;
		}
	}
	if (js_count == JUMP_SERVERS_MAX)
	{
		js_snapshot.truncated = true;
		return;
	}
	slot = &js_slots[js_count++];
	memset (slot, 0, sizeof (*slot));
	slot->row.id = JS_Next ();
	JS_Copy (slot->host, sizeof (slot->host), host);
	JS_Copy (slot->row.address, sizeof (slot->row.address), canonical);
	JS_Copy (slot->row.name, sizeof (slot->row.name), canonical);
	slot->port = port;
	slot->resolved = resolved;
	slot->remote = remote;
	slot->row.favorite = favorite;
	if (resolved)
		JS_Copy (slot->row.numeric, sizeof (slot->row.numeric), NET_AdrToString (&remote));
	js_dirty = true;
}

static int JS_Compare (const void *left, const void *right)
{
	const jump_server_row_t *a, *b;
	int cmp, column;

	a = &js_slots[*(const int *)left].row;
	b = &js_slots[*(const int *)right].row;
	column = js_sort < 0 ? -js_sort : js_sort;
	if (column && (a->state == JUMP_SERVER_VALID) != (b->state == JUMP_SERVER_VALID))
		return a->state == JUMP_SERVER_VALID ? -1 : 1;
	cmp = 0;
	switch (column)
	{
	case 1:
		cmp = Q_stricmp (a->name, b->name);
		break;
	case 2:
		cmp = Q_stricmp (a->mod, b->mod);
		break;
	case 3:
		cmp = Q_stricmp (a->map, b->map);
		break;
	case 4:
		cmp = (b->players > a->players) - (b->players < a->players);
		break;
	case 5:
		cmp = (a->ping > b->ping) - (a->ping < b->ping);
		break;
	}
	if (cmp)
		return js_sort < 0 ? -cmp : cmp;
	return (a->id > b->id) - (a->id < b->id);
}

static void JS_Rebuild (void)
{
	int i;

	js_visible = 0;
	for (i = 0; i < js_count; i++)
		if (js_snapshot.tab != JUMP_SERVERS_JUMP ||
			js_slots[i].row.state != JUMP_SERVER_VALID || js_slots[i].row.jump)
			js_order[js_visible++] = i;
	qsort (js_order, js_visible, sizeof (js_order[0]), JS_Compare);
	js_snapshot.revision++;
	js_dirty = false;
}

static void JS_Source (js_kind_t kind, const char *value, unsigned stride)
{
	js_source_t *source;

	if (js_source_count == JS_SOURCES)
	{
		js_snapshot.truncated = true;
		return;
	}
	source = &js_sources[js_source_count++];
	memset (source, 0, sizeof (*source));
	source->kind = kind;
	source->stride = stride;
	JS_Copy (source->value, sizeof (source->value), value);
}

static void JS_ParseArgs (void)
{
	const char *p, *url;
	char token[1024];
	unsigned stride;
	size_t n;
	qboolean quoted, overflow, stride_digits;

	p = js_args;
	while (*p)
	{
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			p++;
		if (!*p)
			break;
		quoted = *p == '"';
		if (quoted)
			p++;
		n = 0;
		overflow = false;
		while (*p && (quoted ? *p != '"' : *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n'))
		{
			if (n + 1 < sizeof (token))
				token[n++] = *p;
			else
				overflow = true;
			p++;
		}
		if (quoted && *p != '"')
			overflow = true;
		if (quoted && *p)
			p++;
		token[n] = 0;
		if (overflow || !n)
		{
			JS_Error ("Invalid or oversized server-list source");
			continue;
		}
		url = token;
		stride = 0;
		if (*url == '+' || *url == '-')
		{
			url++;
			stride = 0;
			stride_digits = *url >= '0' && *url <= '9';
			while (*url >= '0' && *url <= '9')
			{
				if (stride > (4096u - (*url - '0')) / 10)
				{
					stride = 1;
					break;
				}
				stride = stride * 10 + (*url++ - '0');
			}
			if (!stride_digits)
				stride = 6;
			if (stride < 6 || stride > 4096)
			{
				JS_Error ("Invalid binary server-list stride");
				continue;
			}
		}
		if (!strncmp (url, "http://", 7) || !strncmp (url, "https://", 8))
			JS_Source (JS_HTTP, url, stride);
		else if (!strncmp (url, "file://", 7))
		{
			url += 7;
			while (*url == '/')
				url++;
			if (!*url || strlen (url) >= MAX_QPATH || strstr (url, "..") || strchr (url, ':') || strchr (url, '\\'))
				JS_Error ("Invalid virtual server-list path");
			else
				JS_Source (JS_FILE, url, stride);
		}
		else if (!strcmp (url, "favorites://"))
			JS_Source (JS_BOOK, "", 0);
		else if (!strncmp (url, "cvar://", 7))
			JS_Source (JS_CVAR, url + 7, 0);
		else if (!strcmp (url, "broadcast://"))
			JS_Source (JS_LAN, "", 0);
		else if (!strncmp (url, "quake2://", 9))
			JS_Source (JS_ADDRESS, url + 9, 0);
		else
			JS_Error ("Unsupported server-list source");
	}
	if (!js_source_count && !*js_args)
	{
		JS_Source (JS_BOOK, "", 0);
		JS_Source (JS_LAN, "", 0);
	}
}

static void JS_ClearBlob (void)
{
	if (js_blob_http >= 0)
		CL_JumpServersHTTPRelease (js_blob_http);
	js_blob = NULL;
	js_blob_size = js_blob_cursor = 0;
	js_blob_source = js_blob_http = -1;
}

void CL_JumpServersInit (void)
{
	if (js_initialized)
		return;
	js_initialized = true;
	js_pingrate = Cvar_Get ("ui_pingrate", "0", 0);
	js_sortvar = Cvar_Get ("ui_sortservers", "0", 0);
	js_colorvar = Cvar_Get ("ui_colorservers", "0", 0);
	CL_JumpServersHTTPInit ();
}

void CL_JumpServersAbort (void)
{
	int i;

	JS_ClearBlob ();
	CL_JumpServersHTTPCancel ();
	js_snapshot.generation = JS_Next ();
	for (i = 0; i < js_source_count; i++)
		js_sources[i].state = 2;
	for (i = 0; i < js_count; i++)
	{
		js_slots[i].pending = false;
		js_slots[i].dns_token = 0;
		if (js_slots[i].row.state != JUMP_SERVER_VALID)
			js_slots[i].row.state = JUMP_SERVER_FAILED;
	}
	js_lan_pending = js_lan_active = false;
	js_dirty = true;
}

void CL_JumpServersClose (void)
{
	int i;

	CL_JumpServersAbort ();
	for (i = 0; i < js_count; i++)
		JS_FreeDetails (&js_slots[i]);
	memset (js_slots, 0, sizeof (js_slots));
	js_count = js_visible = js_source_count = 0;
	js_snapshot.open = false;
	js_snapshot.busy = false;
}

void CL_JumpServersRefresh (void)
{
	jump_servers_tab_t tab;

	if (!CL_JumpEnabled () || !js_snapshot.open)
		return;
	tab = js_snapshot.tab;
	CL_JumpServersClose ();
	memset (&js_snapshot, 0, sizeof (js_snapshot));
	js_snapshot.open = true;
	js_snapshot.tab = tab;
	js_snapshot.generation = JS_Next ();
	js_sort = js_sortvar->intvalue;
	if (js_sort < -5 || js_sort > 5)
		js_sort = 0;
	JS_ParseArgs ();
	NET_Config (NET_CLIENT);
	js_dirty = true;
}

void CL_JumpServersOpen (jump_servers_tab_t tab, const char *sources)
{
	static const char *defaults[] = {
		"+http://q2servers.com/?raw=2&mod=jump",
		"+http://q2servers.com/?raw=2",
		"favorites:// file:///servers.lst broadcast://"
	};

	CL_JumpServersInit ();
	if (!CL_JumpEnabled () || tab < JUMP_SERVERS_JUMP || tab > JUMP_SERVERS_BOOK)
		return;
	if (!sources)
		sources = defaults[tab];
	if (strlen (sources) >= sizeof (js_args))
	{
		JS_Error ("Server-list arguments exceed 2047 bytes");
		return;
	}
	JS_Copy (js_args, sizeof (js_args), sources);
	js_snapshot.open = true;
	js_snapshot.tab = tab;
	CL_JumpServersRefresh ();
}

static void JS_BlobFrame (void)
{
	js_source_t *source;
	char line[JUMP_SERVER_ADDRESS];
	int records;
	size_t start, end, n;
	netadr_t remote;

	if (!js_blob)
		return;
	source = &js_sources[js_blob_source];
	for (records = 0; records < JS_RECORDS_PER_FRAME && js_blob_cursor < js_blob_size; records++)
	{
		if (source->stride)
		{
			if (js_blob_size - js_blob_cursor < source->stride)
			{
				js_blob_cursor = js_blob_size;
				break;
			}
			memset (&remote, 0, sizeof (remote));
			remote.type = NA_IP;
			memcpy (remote.ip, js_blob + js_blob_cursor, 4);
			memcpy (&remote.port, js_blob + js_blob_cursor + 4, 2);
			js_blob_cursor += source->stride;
			if (JS_Unicast (&remote))
				JS_Add (NET_AdrToString (&remote), false);
			continue;
		}
		start = js_blob_cursor;
		while (js_blob_cursor < js_blob_size && js_blob[js_blob_cursor] != '\n')
			js_blob_cursor++;
		end = js_blob_cursor;
		if (js_blob_cursor < js_blob_size)
			js_blob_cursor++;
		while (start < end && (js_blob[start] == ' ' || js_blob[start] == '\t'))
			start++;
		while (end > start && (js_blob[end - 1] == ' ' || js_blob[end - 1] == '\t' || js_blob[end - 1] == '\r'))
			end--;
		n = end - start;
		if (!n || n >= sizeof (line) || js_blob[start] == '#' || js_blob[start] == ';' || memchr (js_blob + start, 0, n))
			continue;
		memcpy (line, js_blob + start, n);
		line[n] = 0;
		JS_Add (line, false);
	}
	if (js_blob_cursor == js_blob_size)
	{
		source->state = 2;
		JS_ClearBlob ();
	}
}

static void JS_SourcesFrame (unsigned now)
{
	jump_servers_http_result_t result;
	js_source_t *source;
	FILE *file;
	qboolean close_file;
	int i, j, len, accepted;
	char name[32];
	cvar_t *var;

	if (!js_blob && CL_JumpServersHTTPPoll (&result))
	{
		for (i = 0; i < js_source_count; i++)
			if (js_sources[i].state == 1 && js_sources[i].token == result.token)
				break;
		if (i == js_source_count || !result.ok)
		{
			CL_JumpServersHTTPRelease (result.slot);
			if (i < js_source_count)
			{
				js_sources[i].state = 2;
				JS_Error ("Server-list HTTP request failed");
			}
		}
		else
		{
			js_blob = result.data;
			js_blob_size = result.size;
			js_blob_cursor = 0;
			js_blob_source = i;
			js_blob_http = result.slot;
		}
	}
	JS_BlobFrame ();
	for (i = 0; i < js_source_count; i++)
	{
		source = &js_sources[i];
		if (source->state)
			continue;
		switch (source->kind)
		{
		case JS_HTTP:
			if (!source->token)
				source->token = JS_Next ();
			accepted = CL_JumpServersHTTPStart (source->token, source->value, now);
			if (!accepted)
				continue;
			source->state = accepted > 0 ? 1 : 2;
			if (accepted < 0)
				JS_Error ("HTTP server lists unavailable on this platform");
			break;
		case JS_FILE:
			if (js_blob)
				continue;
			/* Query length before HANDLE_DUPE can materialize an archived stream. */
			len = FS_LoadFile (source->value, NULL);
			if (len < 0)
			{
				source->state = 2;
				break;
			}
			if (len > JUMP_SERVERS_FETCH_BYTES)
			{
				source->state = 2;
				JS_Error ("Server-list file exceeds 128 KiB");
				break;
			}
			file = NULL;
			close_file = false;
			len = FS_FOpenFile (source->value, &file, HANDLE_DUPE, &close_file);
			source->state = 2;
			if (len >= 0 && len <= JUMP_SERVERS_FETCH_BYTES && file)
			{
				if (fread (js_file, 1, len, file) == (size_t)len)
				{
					js_blob = js_file;
					js_blob_size = len;
					js_blob_cursor = 0;
					js_blob_source = i;
					source->state = 1;
				}
				else
					JS_Error ("Unable to read server-list file");
			}
			else if (len > JUMP_SERVERS_FETCH_BYTES)
				JS_Error ("Server-list file exceeds 128 KiB");
			if (file && close_file)
				FS_FCloseFile (file);
			break;
		case JS_BOOK:
			for (j = 0; j < JS_RECORDS_PER_FRAME && source->cursor < JUMP_SERVERS_MAX; j++)
			{
				Com_sprintf (name, sizeof (name), "adr%d", source->cursor++);
				var = Cvar_FindVar (name);
				if (var && *var->string)
					JS_Add (var->string, true);
			}
			if (source->cursor == JUMP_SERVERS_MAX)
				source->state = 2;
			break;
		case JS_CVAR:
			var = Cvar_FindVar (source->value);
			if (var && *var->string)
				JS_Add (var->string, false);
			source->state = 2;
			break;
		case JS_ADDRESS:
			JS_Add (source->value, false);
			source->state = 2;
			break;
		case JS_LAN:
			js_lan_pending = true;
			source->state = 2;
			break;
		}
		break; /* bounded source/engine work; HTTP callbacks never enter here */
	}
}

static void JS_DNSFrame (unsigned now)
{
	jump_servers_dns_result_t result;
	js_slot_t *slot, *other;
	int i, j, accepted;

	if (CL_JumpServersDNSPoll (&result))
	{
		for (i = 0; i < js_count; i++)
			if (js_slots[i].dns_token == result.token)
				break;
		if (i < js_count)
		{
			slot = &js_slots[i];
			slot->dns_token = 0;
			slot->remote.type = NA_IP;
			slot->remote.port = slot->port;
			memcpy (slot->remote.ip, result.ip, 4);
			if (result.ok && JS_Unicast (&slot->remote))
			{
				slot->resolved = true;
				slot->row.state = JUMP_SERVER_IDLE;
				JS_Copy (slot->row.numeric, sizeof (slot->row.numeric), NET_AdrToString (&slot->remote));
				for (j = 0; j < js_count; j++)
				{
					other = &js_slots[j];
					if (j == i || !other->resolved || !JS_Exact (&other->remote, &slot->remote))
						continue;
					other->row.favorite |= slot->row.favorite;
					if (slot->row.favorite)
						JS_Copy (other->row.address, sizeof (other->row.address), slot->row.address);
					JS_FreeDetails (slot);
					memmove (slot, slot + 1, (js_count - i - 1) * sizeof (*slot));
					memset (&js_slots[--js_count], 0, sizeof (*slot));
					break;
				}
			}
			else
			{
				slot->row.state = JUMP_SERVER_FAILED;
				JS_Error ("Hostname lookup failed");
			}
			js_dirty = true;
		}
	}
	for (i = 0; i < js_count; i++)
	{
		slot = &js_slots[i];
		if (slot->resolved || slot->dns_token || slot->row.state == JUMP_SERVER_FAILED)
			continue;
		accepted = CL_JumpServersDNSStart (slot->row.id, slot->host, now);
		if (!accepted)
			break;
		if (accepted < 0)
		{
			slot->row.state = JUMP_SERVER_FAILED;
			JS_Error ("Hostname lookup unavailable on this platform");
		}
		else
		{
			slot->dns_token = slot->row.id;
			slot->row.state = JUMP_SERVER_RESOLVING;
		}
		js_dirty = true;
		break;
	}
}

static void JS_PingFrame (unsigned now)
{
	js_slot_t *slot;
	netadr_t broadcast;
	cvar_t *info_rate;
	int i, rate, stage;
	unsigned interval;

	for (i = 0; i < js_count; i++)
	{
		slot = &js_slots[i];
		if (slot->pending && (unsigned)(now - slot->sent) >= JS_REQUEST_TIMEOUT)
		{
			slot->pending = false;
			slot->row.state = slot->attempts >= 3 ? JUMP_SERVER_FAILED : JUMP_SERVER_IDLE;
			js_dirty = true;
		}
	}
	if (js_lan_active && (unsigned)(now - js_lan_sent) >= JS_LAN_TIMEOUT)
		js_lan_active = false;
	rate = js_pingrate->intvalue;
	if (rate < 0)
		rate = 0;
	if (!rate)
	{
		info_rate = Cvar_FindVar ("info_rate");
		rate = (info_rate ? info_rate->intvalue : 15000) / 450;
	}
	if (rate < 1)
		rate = 1;
	if (rate > 100)
		rate = 100;
	for (i = 0; i < js_count; i++)
	{
		slot = &js_slots[i];
		if (slot->resolved && slot->row.state == JUMP_SERVER_IDLE)
			break;
	}
	if (i == js_count && !js_lan_pending)
		return;
	stage = i < js_count ? js_slots[i].attempts + 1 : 3;
	interval = (3000u + rate * stage - 1) / (rate * stage);
	if (js_sent && (unsigned)(now - js_last_send) < interval)
		return;
	js_sent = true;
	js_last_send = now;
	if (js_lan_pending)
	{
		memset (&broadcast, 0, sizeof (broadcast));
		broadcast.type = NA_BROADCAST;
		memset (broadcast.ip, 255, 4);
		broadcast.port = JS_Port (PORT_SERVER);
		Netchan_OutOfBandPrint (NS_CLIENT, &broadcast, "info %d", PROTOCOL_ORIGINAL);
		js_lan_pending = false;
		js_lan_active = true;
		js_lan_sent = now;
		return;
	}
	slot = &js_slots[i];
	slot->sent = now;
	slot->pending = true;
	slot->attempts++;
	slot->row.state = JUMP_SERVER_PENDING;
	Netchan_OutOfBandPrint (NS_CLIENT, &slot->remote, "status");
	js_dirty = true;
}

void CL_JumpServersFrame (void)
{
	unsigned now;
	int sort;

	now = (unsigned)cls.realtime;
	CL_JumpServersHTTPStep (now); /* also retire callbacks after close */
	if (!js_snapshot.open)
		return;
	if (!CL_JumpEnabled ())
	{
		CL_JumpServersClose ();
		return;
	}
	JS_SourcesFrame (now);
	JS_DNSFrame (now);
	JS_PingFrame (now);
	sort = js_sortvar->intvalue;
	if (sort < -5 || sort > 5)
		sort = 0;
	if (js_sort != sort)
	{
		js_sort = sort;
		js_dirty = true;
	}
	if (js_dirty)
		JS_Rebuild ();
}

static qboolean JS_Integer (const char **cursor, int *out)
{
	const char *p;
	unsigned value, limit;
	int sign, digits;

	p = *cursor;
	while (*p == ' ' || *p == '\t')
		p++;
	sign = *p == '-' ? -1 : 1;
	if (*p == '-' || *p == '+')
		p++;
	limit = sign < 0 ? (unsigned)INT_MAX + 1u : INT_MAX;
	value = 0;
	digits = 0;
	while (*p >= '0' && *p <= '9')
	{
		if (value > (limit - (*p - '0')) / 10)
			return false;
		value = value * 10 + (*p++ - '0');
		digits++;
	}
	if (!digits || (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n'))
		return false;
	*out = sign < 0 ? (value == (unsigned)INT_MAX + 1u ? INT_MIN : -(int)value) : (int)value;
	*cursor = p;
	return true;
}

static const char *JS_Rule (const js_details_t *details, const char *key)
{
	int i;

	for (i = 0; i < details->num_rules; i++)
		if (!strcmp (details->rules[i].key, key))
			return details->rules[i].value;
	return "";
}

static int JS_RuleInteger (const js_details_t *details, const char *key)
{
	const char *p;
	int value;

	p = JS_Rule (details, key);
	return JS_Integer (&p, &value) && !*p ? value : 0;
}

static qboolean JS_Contains (const char *text, const char *needle)
{
	for (; *text; text++)
		if (!Q_strncasecmp (text, needle, (int)strlen (needle)))
			return true;
	return false;
}

static int JS_PlayerCompare (const void *a, const void *b)
{
	const jump_server_player_t *left, *right;

	left = a;
	right = b;
	return (right->score > left->score) - (right->score < left->score);
}

static void JS_ParseDetails (js_details_t *details)
{
	char *end, *p, *key, *value;
	const char *scan, *name;
	size_t n;
	int score, ping;
	qboolean valid;
	jump_server_player_t *player;

	end = strchr (js_status, '\n');
	n = end ? (size_t)(end - js_status) : strlen (js_status);
	if (n && js_status[n - 1] == '\r')
		n--;
	if (n >= sizeof (details->info))
		n = sizeof (details->info) - 1;
	memcpy (details->info, js_status, n);
	details->info[n] = 0;
	valid = n > 0 && details->info[0] == '\\';
	for (p = details->info; *p; p++)
		if ((unsigned char)*p < 32 || *p == 127 || *p == ';' || *p == '"')
			valid = false;
	/* Validate complete pairs before replacing separators with private NULs. */
	for (p = details->info; valid && *p;)
	{
		key = ++p;
		p = strchr (p, '\\');
		if (!p || p == key)
		{
			valid = false;
			break;
		}
		p = strchr (p + 1, '\\');
		if (!p)
			break;
	}
	if (!valid)
		strcpy (details->info, "\\hostname\\badinfo");
	p = details->info + 1;
	while (*p && details->num_rules < JUMP_SERVER_RULES)
	{
		key = p;
		p = strchr (p, '\\');
		*p++ = 0;
		value = p;
		p = strchr (p, '\\');
		details->rules[details->num_rules].key = key;
		details->rules[details->num_rules++].value = value;
		if (!p)
			break;
		*p++ = 0;
	}

	scan = end ? end + 1 : "";
	while (*scan && details->num_players < JUMP_SERVER_PLAYERS)
	{
		while (*scan == '\n' || *scan == '\r')
			scan++;
		if (!*scan)
			break;
		p = strchr (scan, '\n');
		if (p)
			*p = 0;
		if (JS_Integer (&scan, &score) && JS_Integer (&scan, &ping))
		{
			while (*scan == ' ' || *scan == '\t')
				scan++;
			if (*scan == '"')
			{
				name = ++scan;
				while (*scan && *scan != '"' && *scan != '\r')
					scan++;
				if (*scan == '"')
				{
					n = (size_t)(scan - name);
					scan++;
					while (*scan == ' ' || *scan == '\t' || *scan == '\r')
						scan++;
					if (!*scan)
					{
						player = &details->players[details->num_players++];
						player->score = score;
						player->ping = ping;
						if (n >= sizeof (player->name))
							n = sizeof (player->name) - 1;
						memcpy (player->name, name, n);
						player->name[n] = 0;
					}
				}
			}
		}
		scan = p ? p + 1 : "";
	}
	qsort (details->players, details->num_players, sizeof (details->players[0]), JS_PlayerCompare);
}

qboolean CL_JumpServersStatus (const netadr_t *from, const byte *data, size_t size)
{
	js_slot_t *slot;
	js_details_t *details;
	unsigned now;
	int i, value;
	const char *mod;

	if (!js_snapshot.open || !CL_JumpEnabled () || !from || !data ||
		!size || size > JS_STATUS_BYTES || data[0] != '\\')
		return false;
	if (!data[size - 1])
		size--;
	if (!size || memchr (data, 0, size))
		return false;
	now = (unsigned)cls.realtime;
	for (i = 0; i < js_count; i++)
		if (js_slots[i].pending && (unsigned)(now - js_slots[i].sent) < JS_REQUEST_TIMEOUT &&
			JS_Exact (from, &js_slots[i].remote))
			break;
	if (i == js_count)
		return false;
	slot = &js_slots[i];
	memcpy (js_status, data, size);
	js_status[size] = 0;
	details = Z_TagMalloc (sizeof (*details), TAGMALLOC_NOT_TAGGED);
	if (!details)
		return false;
	memset (details, 0, sizeof (*details));
	JS_ParseDetails (details);
	JS_FreeDetails (slot);
	slot->details = details;
	slot->pending = false;
	slot->row.state = JUMP_SERVER_VALID;
	slot->row.ping = (unsigned)(now - slot->sent);
	JS_Copy (slot->row.name, sizeof (slot->row.name), JS_Rule (details, "hostname"));
	if (!slot->row.name[0])
		JS_Copy (slot->row.name, sizeof (slot->row.name), slot->row.address);
	mod = JS_Rule (details, "game");
	JS_Copy (slot->row.mod, sizeof (slot->row.mod), *mod ? mod : "baseq2");
	JS_Copy (slot->row.map, sizeof (slot->row.map), JS_Rule (details, "mapname"));
	slot->row.jump = JS_Contains (slot->row.mod, "jump") || JS_Contains (slot->row.mod, "race");
	slot->row.players = details->num_players;
	value = JS_RuleInteger (details, "maxclients");
	slot->row.max_players = value < 0 ? 0 : value;
	slot->row.restricted = JS_RuleInteger (details, "needpass") >= 1 ||
		JS_RuleInteger (details, "anticheat") >= 2 || (JS_RuleInteger (details, "NoFake") >= 1 || !Q_stricmp (JS_Rule (details, "NoFake"), "ENABLED"));
	js_dirty = true;
	return true;
}

void CL_JumpServersInfo (const netadr_t *from)
{
	if (!CL_JumpEnabled () || !js_snapshot.open || !js_lan_active || !from || !JS_Unicast (from) ||
		(unsigned)((unsigned)cls.realtime - js_lan_sent) >= JS_LAN_TIMEOUT)
		return;
	JS_Add (NET_AdrToString ((netadr_t *)from), false);
}

void CL_JumpServersSnapshot (jump_servers_snapshot_t *out)
{
	int i;
	js_slot_t *slot;

	if (!out)
		return;
	if (js_dirty)
		JS_Rebuild ();
	js_snapshot.total = js_count;
	js_snapshot.visible = js_visible;
	js_snapshot.valid = js_snapshot.pending = js_snapshot.fetching = js_snapshot.resolving = 0;
	js_snapshot.busy = js_snapshot.open && (js_lan_pending || js_lan_active);
	for (i = 0; i < js_source_count; i++)
		if (js_sources[i].state != 2)
		{
			js_snapshot.busy = true;
			js_snapshot.fetching++;
		}
	for (i = 0; i < js_count; i++)
	{
		slot = &js_slots[i];
		if (slot->row.state == JUMP_SERVER_VALID)
			js_snapshot.valid++;
		if (slot->pending)
			js_snapshot.pending++;
		if (slot->dns_token)
			js_snapshot.resolving++;
		if (slot->row.state < JUMP_SERVER_VALID)
			js_snapshot.busy = true;
	}
	*out = js_snapshot;
}

int CL_JumpServersCount (void)
{
	if (js_dirty)
		JS_Rebuild ();
	return js_visible;
}

const jump_server_row_t *CL_JumpServersRow (int index)
{
	if (js_dirty)
		JS_Rebuild ();
	return index >= 0 && index < js_visible ? &js_slots[js_order[index]].row : NULL;
}

const jump_server_row_t *CL_JumpServersById (unsigned id)
{
	js_slot_t *slot;

	slot = JS_ById (id);
	return slot ? &slot->row : NULL;
}

int CL_JumpServersRules (unsigned id, const jump_server_rule_t **out)
{
	js_slot_t *slot;

	if (!out)
		return 0;
	slot = JS_ById (id);
	*out = slot && slot->details ? slot->details->rules : NULL;
	return *out ? slot->details->num_rules : 0;
}

int CL_JumpServersPlayers (unsigned id, const jump_server_player_t **out)
{
	js_slot_t *slot;

	if (!out)
		return 0;
	slot = JS_ById (id);
	*out = slot && slot->details ? slot->details->players : NULL;
	return *out ? slot->details->num_players : 0;
}

void CL_JumpServersSort (int column)
{
	char value[16];

	if (column < -5 || column > 5)
		return;
	Com_sprintf (value, sizeof (value), "%d", column);
	Cvar_Set ("ui_sortservers", value);
	js_sort = column;
	js_dirty = true;
}

qboolean CL_JumpServersRefreshSelected (unsigned id)
{
	js_slot_t *slot;

	if (!CL_JumpEnabled ())
		return false;
	slot = JS_ById (id);
	if (!js_snapshot.open || !slot)
		return false;
	JS_FreeDetails (slot);
	slot->pending = false;
	slot->attempts = 0;
	slot->row.players = slot->row.max_players = 0;
	slot->row.ping = 0;
	slot->row.mod[0] = slot->row.map[0] = 0;
	slot->row.jump = slot->row.restricted = false;
	slot->row.state = slot->resolved ? JUMP_SERVER_IDLE : JUMP_SERVER_RESOLVING;
	js_dirty = true;
	return true;
}

qboolean CL_JumpServersCopyAddress (unsigned id, char *out, size_t size)
{
	js_slot_t *slot;

	if (!CL_JumpEnabled ())
		return false;
	slot = JS_ById (id);
	if (!slot || !out || size <= strlen (slot->row.address))
		return false;
	JS_Copy (out, size, slot->row.address);
	return true;
}

qboolean CL_JumpServersConnect (unsigned id)
{
	js_slot_t *slot;
	char command[JUMP_SERVER_ADDRESS + 16];

	if (!CL_JumpEnabled ())
		return false;
	slot = JS_ById (id);
	if (!slot || slot->row.state == JUMP_SERVER_FAILED)
		return false;
	Com_sprintf (command, sizeof (command), "connect %s\n", slot->row.address);
	Cbuf_AddText (command); /* native CL_Connect_f owns the actual connection */
	return true;
}

qboolean CL_JumpServersSetRconAddress (unsigned id)
{
	js_slot_t *slot;

	if (!CL_JumpEnabled ())
		return false;
	slot = JS_ById (id);
	if (!slot || slot->row.state == JUMP_SERVER_FAILED)
		return false;
	Cvar_Set ("rcon_address", slot->row.address);
	return true;
}

static qboolean JS_FavoriteMatches (const char *text, const js_slot_t *slot)
{
	char host[JUMP_SERVER_ADDRESS], canonical[JUMP_SERVER_ADDRESS];
	netadr_t numeric;
	unsigned short port;
	qboolean resolved;

	return JS_Address (text, host, canonical, &port, &numeric, &resolved) &&
		(!strcmp (canonical, slot->row.address) ||
			(resolved && slot->resolved && JS_Exact (&numeric, &slot->remote)));
}

qboolean CL_JumpServersSaveFavorite (unsigned id)
{
	js_slot_t *slot;
	cvar_t *var;
	char name[32];
	int i, empty;

	if (!CL_JumpEnabled ())
		return false;
	slot = JS_ById (id);
	if (!slot)
		return false;
	empty = -1;
	for (i = 0; i < JUMP_SERVERS_MAX; i++)
	{
		Com_sprintf (name, sizeof (name), "adr%d", i);
		var = Cvar_FindVar (name);
		if (var && JS_FavoriteMatches (var->string, slot))
			break;
		if ((!var || !*var->string) && empty < 0)
			empty = i;
	}
	if (i == JUMP_SERVERS_MAX)
	{
		if (empty < 0)
			return false;
		Com_sprintf (name, sizeof (name), "adr%d", empty);
		Cvar_Get (name, "", CVAR_ARCHIVE);
		Cvar_Set (name, slot->row.address);
	}
	slot->row.favorite = true;
	js_dirty = true;
	return true;
}

qboolean CL_JumpServersRemoveFavorite (unsigned id)
{
	js_slot_t *slot;
	cvar_t *var;
	char name[32];
	int i;

	if (!CL_JumpEnabled ())
		return false;
	slot = JS_ById (id);
	if (!slot)
		return false;
	for (i = 0; i < JUMP_SERVERS_MAX; i++)
	{
		Com_sprintf (name, sizeof (name), "adr%d", i);
		var = Cvar_FindVar (name);
		if (var && JS_FavoriteMatches (var->string, slot))
			Cvar_Set (name, "");
	}
	slot->row.favorite = false;
	js_dirty = true;
	return true;
}
