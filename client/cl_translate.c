/*
 * Opt-in online chat translation. GPL-2.0-or-later.
 * Original chat is handled normally; this module only adds translated lines.
 */
#include "client.h"
#ifdef NO_CHAT_TRANSLATOR

void CL_TranslateInit (void) {}

void CL_TranslateShutdown (void) {}

void CL_TranslateCancel (void) {}

void CL_TranslateFrame (void) {}

void CL_TranslateChat (const char *chat)
{
	(void)chat;
}

qboolean CL_TranslateSendChat (const char *command, const char *text)
{
	(void)command;
	(void)text;
	return false;
}

#else

#include "cl_timing.h"

#define CT_TEXT_BYTES 384
#define CT_PREFIX_BYTES 96
#define CT_RESPONSE_BYTES 16384
#define CT_QUEUE_SIZE 8
#define CT_SAY_BYTES 120

static cvar_t *cl_translate;
static cvar_t *cl_translate_language;
static cvar_t *cl_translate_send_language;
static cvar_t *cl_translate_source;
static cvar_t *cl_translate_send_source;
static cvar_t *cl_translate_provider;

#ifdef USE_CURL

typedef enum
{
	CT_PREVIEW,
	CT_SAY,
	CT_TEAM,
	CT_INCOMING
} translate_kind_t;

typedef struct
{
	translate_kind_t kind;
	qboolean mymemory;
	char text[CT_TEXT_BYTES];
	char prefix[CT_PREFIX_BYTES];
	char source[16], target[16];
} translate_job_t;

typedef struct
{
	char type;
	size_t start, end;
	int next;
} translate_json_token_t;

typedef struct
{
	const char *text;
	size_t length, pos;
	int count;
	translate_json_token_t tokens[512];
} translate_json_t;

static translate_job_t ct_queue[CT_QUEUE_SIZE], ct_active;
static int ct_count;
static CURLM *ct_multi;
static CURL *ct_easy;
static qboolean ct_attached;
static char ct_response[CT_RESPONSE_BYTES];
static size_t ct_response_size;
static unsigned ct_started, ct_next_request, ct_next_warning;
static qboolean ct_rate_limited, ct_warning_limited;
static char ct_setting_source[16], ct_setting_target[16], ct_setting_send[16];
static char ct_setting_send_source[16], ct_setting_provider[16];
static int ct_setting_enabled;

static qboolean CL_TranslateLanguage (const char *s, qboolean source)
{
	size_t i, length;

	if (source && !strcmp (s, "auto"))
		return true;
	length = strlen (s);
	if (length < 2 || length >= 16)
		return false;
	for (i = 0; i < length; i++)
	{
		if ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))
			continue;
		if (s[i] != '-' || i < 2 || i == length - 1)
			return false;
	}
	return true;
}

/* Quake's high bit selects colored glyphs, rather than a Unicode encoding. */
static qboolean CL_TranslateQuakeText (const char *input, char *out, size_t capacity, qboolean trim)
{
	size_t n;
	unsigned char c;

	n = 0;
	while (*input)
	{
		c = (unsigned char)*input++ & 127;
		if (c < 32 || c == 127)
			c = ' ';
		if (n + 1 >= capacity)
			return false;
		out[n++] = (char)c;
	}
	while (trim && n && out[n - 1] == ' ')
		n--;
	out[n] = 0;
	return n != 0;
}

/* The legacy console/HUD font has no Unicode glyphs. Fold common Latin letters. */
static const char *CL_TranslateGlyph (unsigned codepoint, char ascii[2])
{
	ascii[0] = '?';
	ascii[1] = 0;
	if (codepoint >= 32 && codepoint < 127)
		ascii[0] = (char)codepoint;
	else if (codepoint < 32 || codepoint == 127 || codepoint == 160)
		ascii[0] = ' ';
	else if (codepoint >= 0x300 && codepoint <= 0x36f)
		ascii[0] = 0;
	else
	{
		switch (codepoint)
		{
		case 0xc0:
		case 0xc1:
		case 0xc2:
		case 0xc3:
		case 0xc4:
		case 0xc5:
		case 0x104:
			return "A";
		case 0xe0:
		case 0xe1:
		case 0xe2:
		case 0xe3:
		case 0xe4:
		case 0xe5:
		case 0x105:
			return "a";
		case 0xc6:
			return "AE";
		case 0xe6:
			return "ae";
		case 0xc7:
		case 0x106:
		case 0x10c:
			return "C";
		case 0xe7:
		case 0x107:
		case 0x10d:
			return "c";
		case 0xc8:
		case 0xc9:
		case 0xca:
		case 0xcb:
		case 0x118:
		case 0x11a:
			return "E";
		case 0xe8:
		case 0xe9:
		case 0xea:
		case 0xeb:
		case 0x119:
		case 0x11b:
			return "e";
		case 0xcc:
		case 0xcd:
		case 0xce:
		case 0xcf:
			return "I";
		case 0xec:
		case 0xed:
		case 0xee:
		case 0xef:
			return "i";
		case 0x141:
			return "L";
		case 0x142:
			return "l";
		case 0xd1:
		case 0x143:
			return "N";
		case 0xf1:
		case 0x144:
			return "n";
		case 0xd2:
		case 0xd3:
		case 0xd4:
		case 0xd5:
		case 0xd6:
		case 0xd8:
			return "O";
		case 0xf2:
		case 0xf3:
		case 0xf4:
		case 0xf5:
		case 0xf6:
		case 0xf8:
			return "o";
		case 0x152:
			return "OE";
		case 0x153:
			return "oe";
		case 0x15a:
		case 0x160:
			return "S";
		case 0x15b:
		case 0x161:
			return "s";
		case 0xdf:
			return "ss";
		case 0xd9:
		case 0xda:
		case 0xdb:
		case 0xdc:
			return "U";
		case 0xf9:
		case 0xfa:
		case 0xfb:
		case 0xfc:
			return "u";
		case 0xdd:
		case 0x178:
			return "Y";
		case 0xfd:
		case 0xff:
			return "y";
		case 0x179:
		case 0x17b:
		case 0x17d:
			return "Z";
		case 0x17a:
		case 0x17c:
		case 0x17e:
			return "z";
		case 0x2018:
		case 0x2019:
			return "'";
		case 0x201c:
		case 0x201d:
			return "\"";
		case 0x2013:
		case 0x2014:
			return "-";
		case 0x2026:
			return "...";
		default:
			break;
		}
	}
	return ascii;
}

static qboolean CL_TranslateHex4 (translate_json_t *json, unsigned *value)
{
	int i, digit;
	unsigned char c;

	*value = 0;
	for (i = 0; i < 4; i++)
	{
		if (json->pos >= json->length)
			return false;
		c = (unsigned char)json->text[json->pos++];
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'a' && c <= 'f')
			digit = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			digit = c - 'A' + 10;
		else
			return false;
		*value = (*value << 4) | digit;
	}
	return true;
}

/* Validate strings even when skipping metadata. Decode only into bounded output. */
static qboolean CL_TranslateJSONString (translate_json_t *json, char *out, size_t capacity)
{
	unsigned cp, low, minimum;
	unsigned char c;
	int remaining;
	size_t used, length;
	char ascii[2];
	const char *glyph;

	used = 0;
	if (json->pos >= json->length || json->text[json->pos++] != '"')
		return false;
	while (json->pos < json->length)
	{
		c = (unsigned char)json->text[json->pos++];
		if (c == '"')
		{
			if (out)
				out[used] = 0;
			return true;
		}
		if (c < 32)
			return false;
		cp = c;
		if (c == '\\')
		{
			if (json->pos >= json->length)
				return false;
			c = (unsigned char)json->text[json->pos++];
			switch (c)
			{
			case '"':
			case '\\':
			case '/':
				cp = c;
				break;
			case 'b':
			case 'f':
			case 'n':
			case 'r':
			case 't':
				cp = ' ';
				break;
			case 'u':
				if (!CL_TranslateHex4 (json, &cp) || !cp)
					return false;
				if (cp >= 0xd800 && cp <= 0xdbff)
				{
					if (json->length - json->pos < 6 ||
						json->text[json->pos++] != '\\' || json->text[json->pos++] != 'u' ||
						!CL_TranslateHex4 (json, &low) || low < 0xdc00 || low > 0xdfff)
						return false;
					cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
				}
				else if (cp >= 0xdc00 && cp <= 0xdfff)
					return false;
				break;
			default:
				return false;
			}
		}
		else if (c >= 128)
		{
			if (c >= 0xc2 && c <= 0xdf)
			{
				cp = c & 31;
				remaining = 1;
				minimum = 0x80;
			}
			else if (c >= 0xe0 && c <= 0xef)
			{
				cp = c & 15;
				remaining = 2;
				minimum = 0x800;
			}
			else if (c >= 0xf0 && c <= 0xf4)
			{
				cp = c & 7;
				remaining = 3;
				minimum = 0x10000;
			}
			else
				return false;
			while (remaining--)
			{
				if (json->pos >= json->length)
					return false;
				c = (unsigned char)json->text[json->pos++];
				if ((c & 0xc0) != 0x80)
					return false;
				cp = (cp << 6) | (c & 63);
			}
			if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
				return false;
		}
		if (out)
		{
			glyph = CL_TranslateGlyph (cp, ascii);
			length = strlen (glyph);
			if (length >= capacity - used)
				return false;
			memcpy (out + used, glyph, length);
			used += length;
		}
	}
	return false;
}

static void CL_TranslateJSONSpace (translate_json_t *json)
{
	while (json->pos < json->length &&
		(json->text[json->pos] == ' ' || json->text[json->pos] == '\t' ||
			json->text[json->pos] == '\r' || json->text[json->pos] == '\n'))
		json->pos++;
}

static qboolean CL_TranslateJSONValue (translate_json_t *json, int depth)
{
	int index;
	char c, closing;
	const char *literal;
	size_t length;

	CL_TranslateJSONSpace (json);
	if (depth > 16 || json->pos >= json->length || json->count == 512)
		return false;
	index = json->count++;
	c = json->text[json->pos];
	json->tokens[index].type = c;
	json->tokens[index].start = json->pos;
	if (c == '"')
	{
		if (!CL_TranslateJSONString (json, NULL, 0))
			return false;
	}
	else if (c == '[' || c == '{')
	{
		closing = c == '[' ? ']' : '}';
		json->pos++;
		CL_TranslateJSONSpace (json);
		if (json->pos < json->length && json->text[json->pos] == closing)
			json->pos++;
		else
		{
			for (;;)
			{
				if (c == '{')
				{
					if (json->pos >= json->length || json->text[json->pos] != '"' ||
						!CL_TranslateJSONValue (json, depth + 1))
						return false;
					CL_TranslateJSONSpace (json);
					if (json->pos >= json->length || json->text[json->pos++] != ':')
						return false;
				}
				if (!CL_TranslateJSONValue (json, depth + 1))
					return false;
				CL_TranslateJSONSpace (json);
				if (json->pos >= json->length)
					return false;
				if (json->text[json->pos++] == closing)
					break;
				if (json->text[json->pos - 1] != ',')
					return false;
				CL_TranslateJSONSpace (json);
			}
		}
	}
	else if (c == 't' || c == 'f' || c == 'n')
	{
		literal = c == 't' ? "true" : c == 'f' ? "false" :
												 "null";
		length = strlen (literal);
		if (length > json->length - json->pos ||
			memcmp (json->text + json->pos, literal, length))
			return false;
		json->pos += length;
	}
	else
	{
		if (c == '-')
			json->pos++;
		if (json->pos >= json->length)
			return false;
		c = json->text[json->pos++];
		if (c != '0')
		{
			if (c < '1' || c > '9')
				return false;
			while (json->pos < json->length && json->text[json->pos] >= '0' && json->text[json->pos] <= '9')
				json->pos++;
		}
		if (json->pos < json->length && json->text[json->pos] == '.')
		{
			json->pos++;
			length = json->pos;
			while (json->pos < json->length && json->text[json->pos] >= '0' && json->text[json->pos] <= '9')
				json->pos++;
			if (length == json->pos)
				return false;
		}
		if (json->pos < json->length && (json->text[json->pos] == 'e' || json->text[json->pos] == 'E'))
		{
			json->pos++;
			if (json->pos < json->length && (json->text[json->pos] == '+' || json->text[json->pos] == '-'))
				json->pos++;
			length = json->pos;
			while (json->pos < json->length && json->text[json->pos] >= '0' && json->text[json->pos] <= '9')
				json->pos++;
			if (length == json->pos)
				return false;
		}
	}
	json->tokens[index].end = json->pos;
	json->tokens[index].next = json->count;
	return true;
}

static qboolean CL_TranslateDecode (const char *response, size_t size, char *out, size_t capacity)
{
	translate_json_t json;
	int segment, field;
	size_t used;

	memset (&json, 0, sizeof (json));
	if (!capacity || !size || size >= CT_RESPONSE_BYTES)
		return false;
	json.text = response;
	json.length = size;
	if (!CL_TranslateJSONValue (&json, 0))
		return false;
	CL_TranslateJSONSpace (&json);
	if (json.pos != size || json.count < 4 || json.tokens[0].type != '[' ||
		json.tokens[1].type != '[')
		return false;
	used = 0;
	for (segment = 2; segment < json.tokens[1].next; segment = json.tokens[segment].next)
	{
		field = segment + 1;
		if (json.tokens[segment].type != '[' || field >= json.tokens[segment].next ||
			json.tokens[field].type != '"')
			return false;
		json.pos = json.tokens[field].start;
		if (!CL_TranslateJSONString (&json, out + used, capacity - used))
			return false;
		used += strlen (out + used);
	}
	while (used && out[used - 1] == ' ')
		out[--used] = 0;
	return used != 0;
}

/* Find only direct object fields. Reject duplicate protocol fields. */
static int CL_TranslateJSONField (const translate_json_t *json, int object, const char *name)
{
	int key, value, found;
	size_t length;

	if (object < 0 || json->tokens[object].type != '{')
		return -1;
	length = strlen (name);
	found = -1;
	for (key = object + 1; key < json->tokens[object].next; key = json->tokens[value].next)
	{
		value = key + 1;
		if (json->tokens[key].end - json->tokens[key].start == length + 2 &&
			!memcmp (json->text + json->tokens[key].start + 1, name, length))
		{
			if (found >= 0)
				return -2;
			found = value;
		}
	}
	return found;
}

static qboolean CL_TranslateDecodeMyMemory (const char *response, size_t size, char *out, size_t capacity)
{
	translate_json_t json;
	int field, data;
	size_t used;

	memset (&json, 0, sizeof (json));
	if (!capacity || !size || size >= CT_RESPONSE_BYTES)
		return false;
	json.text = response;
	json.length = size;
	if (!CL_TranslateJSONValue (&json, 0))
		return false;
	CL_TranslateJSONSpace (&json);
	if (json.pos != size || json.tokens[0].type != '{')
		return false;
	field = CL_TranslateJSONField (&json, 0, "responseStatus");
	if (field < 0 || json.tokens[field].end - json.tokens[field].start != 3 ||
		memcmp (response + json.tokens[field].start, "200", 3))
		return false;
	/* MyMemory can report quota/API failures in an HTTP 200 response. */
	field = CL_TranslateJSONField (&json, 0, "quotaFinished");
	if (field == -2 || (field >= 0 && json.tokens[field].type != 'f'))
		return false;
	if (CL_TranslateJSONField (&json, 0, "error") != -1)
		return false;
	data = CL_TranslateJSONField (&json, 0, "responseData");
	if (data < 0 || json.tokens[data].type != '{')
		return false;
	field = CL_TranslateJSONField (&json, data, "translatedText");
	if (field < 0 || json.tokens[field].type != '"')
		return false;
	json.pos = json.tokens[field].start;
	if (!CL_TranslateJSONString (&json, out, capacity))
		return false;
	used = strlen (out);
	while (used && out[used - 1] == ' ')
		out[--used] = 0;
	return used != 0;
}

static void CL_TranslateRelease (void)
{
	if (ct_easy)
	{
		if (ct_attached)
			curl_multi_remove_handle (ct_multi, ct_easy);
		curl_easy_cleanup (ct_easy);
		ct_easy = NULL;
	}
	ct_attached = false;
	ct_response_size = 0;
}

void CL_TranslateCancel (void)
{
	CL_TranslateRelease ();
	ct_count = 0;
}

static qboolean CL_TranslateSettings (void)
{
	qboolean changed;

	changed = ct_setting_enabled != cl_translate->intvalue ||
		strcmp (ct_setting_source, cl_translate_source->string) ||
		strcmp (ct_setting_target, cl_translate_language->string) ||
		strcmp (ct_setting_send, cl_translate_send_language->string) ||
		strcmp (ct_setting_send_source, cl_translate_send_source->string) ||
		strcmp (ct_setting_provider, cl_translate_provider->string);
	if (changed)
	{
		CL_TranslateCancel ();
		ct_setting_enabled = cl_translate->intvalue;
		Com_sprintf (ct_setting_source, sizeof (ct_setting_source), "%s", cl_translate_source->string);
		Com_sprintf (ct_setting_target, sizeof (ct_setting_target), "%s", cl_translate_language->string);
		Com_sprintf (ct_setting_send, sizeof (ct_setting_send), "%s", cl_translate_send_language->string);
		Com_sprintf (ct_setting_send_source, sizeof (ct_setting_send_source), "%s", cl_translate_send_source->string);
		Com_sprintf (ct_setting_provider, sizeof (ct_setting_provider), "%s", cl_translate_provider->string);
	}
	return cl_translate->intvalue != 0;
}

static void CL_TranslateFailure (const translate_job_t *job, const char *reason)
{
	unsigned now;

	now = Sys_Milliseconds ();
	if (job->kind == CT_INCOMING)
	{
		if (ct_warning_limited && (int)(now - ct_next_warning) < 0)
			return;
		ct_warning_limited = true;
		ct_next_warning = now + 15000;
	}
	Com_Printf ("Translator: %s\n", LOG_CLIENT, reason);
}

static size_t CL_TranslateReceive (char *data, size_t size, size_t count, void *context)
{
	size_t bytes;

	(void)context;
	if (size && count > (CT_RESPONSE_BYTES - 1 - ct_response_size) / size)
		return 0;
	bytes = size * count;
	memcpy (ct_response + ct_response_size, data, bytes);
	ct_response_size += bytes;
	ct_response[ct_response_size] = 0;
	return bytes;
}

static qboolean CL_TranslateStart (unsigned now)
{
	char url[CT_TEXT_BYTES * 3 + 256];
	char *encoded;
	CURLcode error;
	const curl_version_info_data *version;

	version = curl_version_info (CURLVERSION_NOW);
	if (!version || !(version->features & CURL_VERSION_ASYNCHDNS))
		return false;
	if (!ct_multi)
		ct_multi = curl_multi_init ();
	if (!ct_multi)
		return false;
	ct_easy = curl_easy_init ();
	if (!ct_easy)
		return false;
	encoded = curl_easy_escape (ct_easy, ct_active.text, 0);
	if (!encoded)
		goto failed;
	if (ct_active.mymemory)
		Com_sprintf (url, sizeof (url),
			"https://api.mymemory.translated.net/get?q=%s&langpair=%s%%7C%s",
			encoded, ct_active.source, ct_active.target);
	else
		Com_sprintf (url, sizeof (url),
			"https://translate.googleapis.com/translate_a/single?client=gtx&sl=%s&tl=%s&dt=t&ie=UTF-8&oe=UTF-8&q=%s",
			ct_active.source, ct_active.target, encoded);
	curl_free (encoded);
	ct_response_size = 0;
#define CT_OPTION(option, value) \
	do { error = curl_easy_setopt (ct_easy, option, value); if (error != CURLE_OK) goto failed; } while (0)
	CT_OPTION (CURLOPT_URL, url);
	CT_OPTION (CURLOPT_WRITEFUNCTION, CL_TranslateReceive);
	CT_OPTION (CURLOPT_WRITEDATA, NULL);
	CT_OPTION (CURLOPT_USERAGENT, "R1Q2-Race/ChatTranslator");
	CT_OPTION (CURLOPT_CONNECTTIMEOUT_MS, 3000L);
	CT_OPTION (CURLOPT_TIMEOUT_MS, 8000L);
	CT_OPTION (CURLOPT_NOSIGNAL, 1L);
	CT_OPTION (CURLOPT_SSL_VERIFYPEER, 1L);
	CT_OPTION (CURLOPT_SSL_VERIFYHOST, 2L);
	CT_OPTION (CURLOPT_FOLLOWLOCATION, 0L);
	CT_OPTION (CURLOPT_FAILONERROR, 1L);
	CT_OPTION (CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
#undef CT_OPTION
	if (curl_multi_add_handle (ct_multi, ct_easy) != CURLM_OK)
		goto failed;
	ct_attached = true;
	ct_started = now;
	return true;
failed:
	CL_TranslateRelease ();
	return false;
}

static void CL_TranslateResult (void)
{
	char text[CT_TEXT_BYTES], line[512], command[CT_SAY_BYTES + 32];
	size_t i;

	if (!(ct_active.mymemory ? CL_TranslateDecodeMyMemory (ct_response, ct_response_size, text, sizeof (text)) :
							   CL_TranslateDecode (ct_response, ct_response_size, text, sizeof (text))))
	{
		CL_TranslateFailure (&ct_active, ct_active.mymemory ? "MyMemory rejected the request, exhausted its quota, or returned an invalid response." : "invalid or oversized service response.");
		if (ct_active.mymemory)
		{
			ct_next_request = Sys_Milliseconds () + 15000;
			ct_rate_limited = true;
		}
		return;
	}
	if (ct_active.kind == CT_SAY || ct_active.kind == CT_TEAM)
	{
		if (cls.state != ca_active || cl.attractloop)
			return;
		if (strlen (text) > CT_SAY_BYTES)
		{
			Com_Printf ("Translator: result exceeds %d chat characters; not sent: %s\n",
				LOG_CLIENT, CT_SAY_BYTES, text);
			return;
		}
		/* Never execute provider output through Cbuf. Keep a single quoted say. */
		for (i = 0; text[i]; i++)
			if (text[i] == '"' || text[i] == '\\' || text[i] == ';')
				text[i] = '\'';
		Com_sprintf (command, sizeof (command), "%s \"%s\"",
			ct_active.kind == CT_TEAM ? "say_team" : "say", text);
		MSG_WriteByte (clc_stringcmd);
		MSG_Print (command);
		MSG_EndWriting (&cls.netchan.message);
		send_packet_now = true;
		CL_TimingTrigger (CL_TIMING_STRINGCMD);
		return;
	}
	if (ct_active.kind == CT_INCOMING && !strcmp (text, ct_active.text))
		return;
	Com_sprintf (line, sizeof (line), "%s[tr %s] %s\n", ct_active.prefix, ct_active.target, text);
	SCR_AddChatMessage (line);
	Com_Printf ("%s", LOG_CLIENT | LOG_CHAT, line);
}

void CL_TranslateFrame (void)
{
	CURLMsg *message;
	CURLMcode error;
	long status;
	int running, remaining;
	unsigned now;
	char reason[192];

	if (!cl_translate || !CL_TranslateSettings ())
	{
		CL_TranslateCancel ();
		return;
	}
	now = Sys_Milliseconds ();
	if (ct_easy)
	{
		error = curl_multi_perform (ct_multi, &running);
		if (error != CURLM_OK && error != CURLM_CALL_MULTI_PERFORM)
		{
			CL_TranslateFailure (&ct_active, "network polling failed.");
			CL_TranslateRelease ();
		}
		else
		{
			while ((message = curl_multi_info_read (ct_multi, &remaining)) != NULL)
			{
				if (message->msg != CURLMSG_DONE || message->easy_handle != ct_easy)
					continue;
				status = 0;
				curl_easy_getinfo (ct_easy, CURLINFO_RESPONSE_CODE, &status);
				if (message->data.result == CURLE_OK &&
					status == 200)
					CL_TranslateResult ();
				else
				{
					Com_sprintf (reason, sizeof (reason),
						"service unavailable or request failed (HTTP %ld, curl %d: %s).",
						status, (int)message->data.result, curl_easy_strerror (message->data.result));
					CL_TranslateFailure (&ct_active, reason);
					/* Back off service failures, including quota/rate limits. */
					ct_next_request = now + 15000;
					ct_rate_limited = true;
				}
				CL_TranslateRelease ();
				break;
			}
			if (ct_easy && now - ct_started >= 8000)
			{
				CL_TranslateFailure (&ct_active, "request timed out.");
				CL_TranslateRelease ();
			}
		}
	}
	if (!ct_easy && ct_count && (!ct_rate_limited || (int)(now - ct_next_request) >= 0))
	{
		ct_active = ct_queue[0];
		ct_count--;
		memmove (ct_queue, ct_queue + 1, ct_count * sizeof (ct_queue[0]));
		ct_next_request = now + 1000;
		ct_rate_limited = true;
		if (!CL_TranslateStart (now))
			CL_TranslateFailure (&ct_active, "HTTP support with asynchronous DNS is unavailable.");
	}
}

static void CL_TranslateQueue (translate_kind_t kind, const char *text, const char *prefix)
{
	translate_job_t job;
	const char *source, *target;

	if (!CL_TranslateSettings ())
	{
		if (kind != CT_INCOMING)
			Com_Printf ("Translator: enable cl_translate 1 first (chat text is sent to the selected online provider).\n", LOG_CLIENT);
		return;
	}
	target = kind == CT_SAY || kind == CT_TEAM ? cl_translate_send_language->string : cl_translate_language->string;
	memset (&job, 0, sizeof (job));
	job.kind = kind;
	if (strcmp (cl_translate_provider->string, "google") && strcmp (cl_translate_provider->string, "mymemory"))
	{
		CL_TranslateFailure (&job, "unknown provider; choose google or mymemory.");
		return;
	}
	job.mymemory = !strcmp (cl_translate_provider->string, "mymemory");
	source = (kind == CT_SAY || kind == CT_TEAM) && cl_translate_send_source->string[0] ?
		cl_translate_send_source->string :
		cl_translate_source->string;
	if (!CL_TranslateLanguage (source, true) || !CL_TranslateLanguage (target, false))
	{
		CL_TranslateFailure (&job, "invalid source or target language code.");
		return;
	}
	if (job.mymemory && !strcmp (source, "auto"))
	{
		CL_TranslateFailure (&job, "MyMemory needs an explicit source language: set cl_translate_source for received chat and cl_translate_send_source for outgoing chat.");
		return;
	}
	if (ct_count >= (kind == CT_INCOMING ? CT_QUEUE_SIZE - 2 : CT_QUEUE_SIZE))
	{
		if (kind != CT_INCOMING)
			CL_TranslateFailure (&job, "request queue is full.");
		return;
	}
	if (!CL_TranslateQuakeText (text, job.text, sizeof (job.text), true))
	{
		if (kind != CT_INCOMING)
			CL_TranslateFailure (&job, "text is empty or too long (maximum 383 characters).");
		return;
	}
	if (!strcmp (source, target))
	{
		if (kind != CT_INCOMING)
			CL_TranslateFailure (&job, "source and target languages are identical.");
		return;
	}
	Com_sprintf (job.prefix, sizeof (job.prefix), "%s", prefix);
	strcpy (job.source, source);
	strcpy (job.target, target);
	ct_queue[ct_count++] = job;
}

void CL_TranslateChat (const char *chat)
{
	int i, best, maxclients;
	size_t name_length, prefix_length, best_length;
	const char *skin;
	char line[CT_TEXT_BYTES + CT_PREFIX_BYTES], name[MAX_QPATH], prefix[CT_PREFIX_BYTES];

	if (!cl_translate || !cl_translate->intvalue || cls.state != ca_active || cl.attractloop ||
		!CL_TranslateQuakeText (chat, line, sizeof (line), true))
		return;
	best = -1;
	best_length = 0;
	maxclients = cl.maxclients < MAX_CLIENTS ? cl.maxclients : MAX_CLIENTS;
	for (i = 0; i < maxclients; i++)
	{
		skin = cl.configstrings[CS_PLAYERSKINS + i];
		name_length = strcspn (skin, "\\");
		if (!name_length || name_length >= sizeof (name))
			continue;
		memcpy (name, skin, name_length);
		name[name_length] = 0;
		/* Q2Jump renders control glyphs as spaces and keeps trailing name spaces. */
		if (!CL_TranslateQuakeText (name, name, sizeof (name), false))
			continue;
		name_length = strlen (name);
		prefix_length = 0;
		if (!strncmp (line, name, name_length) && !strncmp (line + name_length, ": ", 2))
			prefix_length = name_length + 2;
		else if (line[0] == '(' && !strncmp (line + 1, name, name_length) &&
			!strncmp (line + name_length + 1, "): ", 3))
			prefix_length = name_length + 4;
		if (prefix_length > best_length)
		{
			best = i;
			best_length = prefix_length;
		}
	}
	if (best < 0 || best == cl.playernum || best_length >= sizeof (prefix))
		return;
	memcpy (prefix, line, best_length);
	prefix[best_length] = 0;
	CL_TranslateQueue (CT_INCOMING, line + best_length, prefix);
}

qboolean CL_TranslateSendChat (const char *command, const char *text)
{
	translate_kind_t kind;

	if (strcmp (command, "translate_say") && strcmp (command, "translate_say_team"))
		return false;
	if (cls.state != ca_active || cl.attractloop)
	{
		Com_Printf ("Translator: join a server before sending translated chat.\n", LOG_CLIENT);
		return true;
	}
	kind = !strcmp (command, "translate_say_team") ? CT_TEAM : CT_SAY;
	CL_TranslateQueue (kind, text, "");
	return true;
}

static void CL_Translate_f (void)
{
	translate_kind_t kind;

	kind = !strcmp (Cmd_Argv (0), "translate_say")	 ? CT_SAY :
		!strcmp (Cmd_Argv (0), "translate_say_team") ? CT_TEAM :
													   CT_PREVIEW;
	if (Cmd_Argc () < 2)
	{
		if (kind == CT_SAY || kind == CT_TEAM)
		{
			if (cls.state != ca_active || cl.attractloop)
				Com_Printf ("Translator: join a server before sending translated chat.\n", LOG_CLIENT);
			else if (!cl_translate->intvalue)
				Com_Printf ("Translator: enable cl_translate 1 first (chat text is sent to the selected online provider).\n", LOG_CLIENT);
			else
				Con_OpenChatPrompt (Cmd_Argv (0), "");
			return;
		}
		Com_Printf ("Usage: %s \"text\"\n", LOG_CLIENT, Cmd_Argv (0));
		Com_Printf ("Translator: %s, receive %s, send %s, source %s.\n", LOG_CLIENT,
			cl_translate->intvalue ? "enabled" : "disabled", cl_translate_language->string,
			cl_translate_send_language->string, cl_translate_source->string);
		Com_Printf ("Provider: %s, outgoing source: %s.\n", LOG_CLIENT, cl_translate_provider->string,
			cl_translate_send_source->string[0] ? cl_translate_send_source->string : "inherit reading source");
		return;
	}
	if (CL_TranslateSendChat (Cmd_Argv (0), Cmd_Args2 (1)))
		return;
	CL_TranslateQueue (kind, Cmd_Args2 (1), "");
}

#else

void CL_TranslateCancel (void) {}

void CL_TranslateFrame (void) {}

void CL_TranslateChat (const char *chat)
{
	(void)chat;
}

qboolean CL_TranslateSendChat (const char *command, const char *text)
{
	(void)text;
	if (strcmp (command, "translate_say") && strcmp (command, "translate_say_team"))
		return false;
	Com_Printf ("Translator: this client was built without USE_CURL.\n", LOG_CLIENT);
	return true;
}

static void CL_Translate_f (void)
{
	Com_Printf ("Translator: this client was built without USE_CURL.\n", LOG_CLIENT);
}

#endif

/* Observe every edit, including disable/re-enable within one command buffer. */
static void CL_TranslateChanged (cvar_t *self, char *old_value, char *new_value)
{
	(void)self;
	(void)old_value;
	(void)new_value;
	CL_TranslateCancel ();
}

void CL_TranslateInit (void)
{
	cl_translate = Cvar_Get ("cl_translate", "0", CVAR_ARCHIVE);
	cl_translate_language = Cvar_Get ("cl_translate_language", "en", CVAR_ARCHIVE);
	cl_translate_send_language = Cvar_Get ("cl_translate_send_language", "en", CVAR_ARCHIVE);
	cl_translate_source = Cvar_Get ("cl_translate_source", "auto", CVAR_ARCHIVE);
	cl_translate_send_source = Cvar_Get ("cl_translate_send_source", "", CVAR_ARCHIVE);
	cl_translate_provider = Cvar_Get ("cl_translate_provider", "google", CVAR_ARCHIVE);
	cl_translate->changed = CL_TranslateChanged;
	cl_translate_language->changed = CL_TranslateChanged;
	cl_translate_send_language->changed = CL_TranslateChanged;
	cl_translate_source->changed = CL_TranslateChanged;
	cl_translate_send_source->changed = CL_TranslateChanged;
	cl_translate_provider->changed = CL_TranslateChanged;
	Cmd_AddCommand ("translate", CL_Translate_f);
	Cmd_AddCommand ("translate_say", CL_Translate_f);
	Cmd_AddCommand ("translate_say_team", CL_Translate_f);
}

void CL_TranslateShutdown (void)
{
	CL_TranslateCancel ();
#ifdef USE_CURL
	if (ct_multi)
	{
		curl_multi_cleanup (ct_multi);
		ct_multi = NULL;
	}
#endif
	Cmd_RemoveCommand ("translate");
	Cmd_RemoveCommand ("translate_say");
	Cmd_RemoveCommand ("translate_say_team");
}

#endif /* NO_CHAT_TRANSLATOR */
