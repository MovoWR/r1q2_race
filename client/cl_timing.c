/* Optional command timing diagnostics. Capture in RAM; export only on request. */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include "client.h"
#include "cl_timing.h"
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#define CL_TIMING_SAMPLES 8192

extern cvar_t *cl_maxfps, *r_maxfps, *cl_instantpacket, *cl_instantack;
extern uint32 sys_frame_time;

typedef struct
{
	double elapsed, packet_interval, move_interval;
	unsigned sequence, engine_time, input_time, reasons, packetdup;
	int move, msec, previous_msec, oldest_msec, refreshed;
	int frame_msec, scheduled, frame_maxfps, frame_valid;
	int requested, userinfo, maxfps, renderfps, asynchronous;
	int instantpacket, instantack, length, reliable_bytes, result;
} cl_timing_sample_t;

static cl_timing_sample_t timing_samples[CL_TIMING_SAMPLES];

static struct
{
	qboolean active, available, written, have_packet, have_move;
	unsigned count, triggers, reasons;
	double start, last, last_packet, last_move;
	int seconds, frame_msec, scheduled, frame_maxfps, frame_valid;
	int requested, userinfo, refreshed, protocol, minor;
	const char *ended;
	char directory[MAX_OSPATH];
} timing;

/* Sys_Milliseconds changes curtime. Keep the diagnostic clock independent. */
static qboolean CL_TimingNow (double *milliseconds)
{
#ifdef _WIN32
	static LARGE_INTEGER frequency;
	LARGE_INTEGER tick;
	if (!frequency.QuadPart &&
		(!QueryPerformanceFrequency (&frequency) || frequency.QuadPart <= 0))
	{
		frequency.QuadPart = 0;
		return false;
	}
	if (!QueryPerformanceCounter (&tick))
		return false;
	*milliseconds = (double)tick.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
	struct timespec tick;
	if (clock_gettime (CLOCK_MONOTONIC, &tick))
		return false;
	*milliseconds = (double)tick.tv_sec * 1000.0 + (double)tick.tv_nsec / 1000000.0;
#endif
	return true;
}

static qboolean CL_TimingCheck (double *now)
{
	if (!timing.active)
		return false;
	if (!CL_TimingNow (now) || *now < timing.last)
	{
		timing.active = false;
		timing.ended = "clock failure";
		return false;
	}
	timing.last = *now;
	if (*now - timing.start >= timing.seconds * 1000.0)
	{
		timing.active = false;
		timing.ended = "duration reached";
		return false;
	}
	return true;
}

void CL_TimingFrame (int packet_msec, qboolean scheduled)
{
	if (!timing.active)
		return;
	timing.frame_valid = true;
	timing.frame_msec = packet_msec;
	timing.frame_maxfps = cl_maxfps->intvalue;
	timing.scheduled = scheduled;
	timing.refreshed = false;
	timing.requested = timing.userinfo = false;
	timing.reasons = scheduled ? CL_TIMING_SCHEDULED : 0;
	if (cl_timedemo->intvalue)
		timing.reasons |= CL_TIMING_TIMEDEMO;
	if (cl_async->intvalue != 1 && cl_async->intvalue != 2)
		timing.reasons |= CL_TIMING_SYNC;
}

void CL_TimingTrigger (unsigned reason)
{
	if (timing.active)
		timing.triggers |= reason;
}

void CL_TimingDecision (qboolean requested, qboolean userinfo)
{
	if (!timing.active)
		return;
	timing.requested = requested;
	timing.userinfo = userinfo;
	timing.reasons |= timing.triggers;
	if (requested)
		timing.reasons |= CL_TIMING_REQUEST;
	if (userinfo)
		timing.reasons |= CL_TIMING_USERINFO;
	timing.triggers = 0;
}

void CL_TimingRefresh (void)
{
	if (timing.active)
		timing.refreshed = true;
}

int CL_TimingTransmit (int length, const byte *data, const usercmd_t *cmd)
{
	cl_timing_sample_t *sample;
	double now;
	int result;

	if (!CL_TimingCheck (&now))
		return Netchan_Transmit (&cls.netchan, length, data);

	sample = &timing_samples[timing.count++];
	memset (sample, 0, sizeof (*sample));
	sample->elapsed = now - timing.start;
	sample->packet_interval = timing.have_packet ? now - timing.last_packet : -1;
	sample->move_interval = cmd && timing.have_move ? now - timing.last_move : -1;
	sample->move = cmd != NULL;
	sample->msec = cmd ? cmd->msec : -1;
	sample->sequence = cls.netchan.outgoing_sequence;
	sample->previous_msec = cmd ? cl.cmds[(sample->sequence - 1) & (CMD_BACKUP - 1)].msec : -1;
	sample->oldest_msec = cmd ? cl.cmds[(sample->sequence - 2) & (CMD_BACKUP - 1)].msec : -1;
	sample->engine_time = cls.realtime;
	sample->input_time = sys_frame_time;
	sample->refreshed = (cl_async->intvalue == 1 || cl_async->intvalue == 2) ? timing.refreshed : -1;
	sample->frame_msec = timing.frame_msec;
	sample->scheduled = timing.scheduled;
	sample->frame_maxfps = timing.frame_maxfps;
	sample->frame_valid = timing.frame_valid;
	sample->requested = timing.requested;
	sample->userinfo = timing.userinfo;
	sample->reasons = cmd ? timing.reasons : 0;
	sample->maxfps = cl_maxfps->intvalue;
	sample->renderfps = r_maxfps->intvalue;
	sample->asynchronous = cl_async->intvalue;
	sample->instantpacket = cl_instantpacket->intvalue;
	sample->instantack = cl_instantack->intvalue;
	sample->packetdup = cls.netchan.packetdup;
	sample->length = length;
	sample->reliable_bytes = cls.netchan.message.cursize;
	timing.last_packet = now;
	timing.have_packet = true;
	if (cmd)
	{
		timing.last_move = now;
		timing.have_move = true;
	}
	/* ERR_DROP can leave Netchan_Transmit via longjmp. Retain that distinction. */
	sample->result = -3;
	if (timing.count == CL_TIMING_SAMPLES)
	{
		timing.active = false;
		timing.ended = "buffer full";
	}
	result = Netchan_Transmit (&cls.netchan, length, data);
	sample->result = result;
	return result;
}

void CL_TimingEnd (void)
{
	if (timing.active)
	{
		timing.active = false;
		timing.ended = "connection cleared";
	}
}

/* Exclusive creation also protects captures made by another running client. */
static FILE *CL_TimingOpen (const char *path)
{
	int descriptor, error;
	FILE *file;
#ifdef _WIN32
	descriptor = _open (path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
		_S_IREAD | _S_IWRITE);
#else
	descriptor = open (path, O_WRONLY | O_CREAT | O_EXCL, 0666);
#endif
	if (descriptor < 0)
		return NULL;
#ifdef _WIN32
	file = _fdopen (descriptor, "wb");
#else
	file = fdopen (descriptor, "wb");
#endif
	if (!file)
	{
		error = errno;
#ifdef _WIN32
		_close (descriptor);
#else
		close (descriptor);
#endif
		errno = error;
	}
	return file;
}

static void CL_TimingSave (void)
{
	char path[MAX_OSPATH], stamp[32];
	FILE *file;
	time_t walltime;
	struct tm *local;
	unsigned i;
	int suffix, failed;
	const cl_timing_sample_t *s;

	walltime = time (NULL);
	local = localtime (&walltime);
	if (!local || !strftime (stamp, sizeof (stamp), "%Y%m%d-%H%M%S", local))
		strcpy (stamp, "capture");
	for (suffix = 0; suffix < 1000; suffix++)
	{
		Com_sprintf (path, sizeof (path), "%s/timing/%s-%03d.csv", timing.directory, stamp, suffix);
		FS_CreatePath (path);
		file = CL_TimingOpen (path);
		if (file || errno != EEXIST)
			break;
	}
	if (suffix == 1000)
	{
		Com_Printf ("cl_timing: no unused filename; capture retained.\n", LOG_CLIENT);
		return;
	}
	if (!file)
	{
		Com_Printf ("cl_timing: cannot write %s; capture retained, retry stop.\n", LOG_CLIENT, path);
		return;
	}
	fprintf (file, "# R1Q2 client timing capture v1; logical transmit attempts, not proof of delivery\n");
	fprintf (file, "# ended=%s; requested_seconds=%d; samples=%u; protocol=%d; minor=%d\n",
		timing.ended, timing.seconds, timing.count, timing.protocol, timing.minor);
	fprintf (file, "# reason_mask: stringcmd=1 attack=2 use=4 wait=8 userinfo=16 request=32 scheduled=64 timedemo=128 sync=256\n");
	fprintf (file, "# nonmove=ACK/reliable-only; -1 intervals have no preceding sample; refreshed=-1 is synchronous\n");
	fprintf (file, "# result: 0=accepted local attempt -1=send failure -2=message overflow -3=transmit did not return\n");
	fprintf (file, "elapsed_ms,packet_interval_ms,move_interval_ms,kind,sequence,cmd_msec,previous_msec,oldest_msec,engine_ms,input_ms,refreshed,frame_valid,frame_msec,scheduled,frame_maxfps,requested,userinfo,reason_mask,cl_maxfps,r_maxfps,cl_async,cl_instantpacket,cl_instantack,packetdup,move_bytes,queued_reliable_bytes,result\n");
	for (i = 0; i < timing.count; i++)
	{
		s = &timing_samples[i];
		fprintf (file, "%.3f,%.3f,%.3f,%s,%u,%d,%d,%d,%u,%u,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,%u,%d,%d,%d\n",
			s->elapsed, s->packet_interval, s->move_interval, s->move ? "move" : "nonmove",
			s->sequence, s->msec, s->previous_msec, s->oldest_msec, s->engine_time, s->input_time,
			s->refreshed, s->frame_valid, s->frame_msec, s->scheduled, s->frame_maxfps,
			s->requested, s->userinfo, s->reasons, s->maxfps, s->renderfps, s->asynchronous,
			s->instantpacket, s->instantack, s->packetdup, s->length, s->reliable_bytes, s->result);
	}
	failed = ferror (file);
	if (fclose (file))
		failed = true;
	if (failed)
	{
		Com_Printf ("cl_timing: write failed for %s; capture retained, retry stop.\n", LOG_CLIENT, path);
		return;
	}
	timing.written = true;
	Com_Printf ("cl_timing: saved %u samples to %s (%s).\n", LOG_CLIENT, timing.count, path, timing.ended);
}

static void CL_Timing_f (void)
{
	const char *action, *directory;
	char *end;
	long seconds;
	double now;

	action = Cmd_Argv (1);
	if (!strcmp (action, "start") && (Cmd_Argc () == 2 || Cmd_Argc () == 3))
	{
		seconds = 10;
		if (Cmd_Argc () == 3)
		{
			seconds = strtol (Cmd_Argv (2), &end, 10);
			if (!*Cmd_Argv (2) || *end || seconds < 1 || seconds > 60)
			{
				Com_Printf ("cl_timing: duration must be 1 to 60 seconds.\n", LOG_CLIENT);
				return;
			}
		}
		if (timing.available && !timing.written)
		{
			Com_Printf ("cl_timing: capture pending; use stop to save or clear to discard.\n", LOG_CLIENT);
			return;
		}
		if (cls.state != ca_active || cl.attractloop)
		{
			Com_Printf ("cl_timing: connect to a server before starting.\n", LOG_CLIENT);
			return;
		}
		directory = FS_Gamedir ();
		if (strlen (directory) + 64 >= sizeof (timing.directory) || !CL_TimingNow (&now))
		{
			Com_Printf ("cl_timing: game path too long or diagnostic clock unavailable.\n", LOG_CLIENT);
			return;
		}
		memset (&timing, 0, sizeof (timing));
		timing.start = timing.last = now;
		timing.seconds = (int)seconds;
		timing.protocol = cls.serverProtocol;
		timing.minor = cls.protocolVersion;
		timing.ended = "recording";
		strcpy (timing.directory, directory);
		Com_Printf ("cl_timing: capturing up to %d seconds in RAM; use cl_timing stop to save.\n", LOG_CLIENT, timing.seconds);
		timing.available = timing.active = true;
	}
	else if (!strcmp (action, "stop") && Cmd_Argc () == 2)
	{
		CL_TimingCheck (&now);
		if (!timing.available || timing.written)
		{
			Com_Printf ("cl_timing: no unsaved capture.\n", LOG_CLIENT);
			return;
		}
		if (timing.active)
			timing.ended = "stopped by user";
		timing.active = false;
		CL_TimingSave ();
	}
	else if (!strcmp (action, "clear") && Cmd_Argc () == 2)
	{
		memset (&timing, 0, sizeof (timing));
		Com_Printf ("cl_timing: capture cleared.\n", LOG_CLIENT);
	}
	else if (!strcmp (action, "status") && Cmd_Argc () == 2)
	{
		CL_TimingCheck (&now);
		Com_Printf ("cl_timing: %s, %u samples%s.\n", LOG_CLIENT,
			timing.available ? timing.ended : "idle", timing.count,
			timing.available ? (timing.written ? ", saved" : ", use stop to save") : "");
	}
	else
		Com_Printf ("Usage: cl_timing start [1-60 seconds] | stop | status | clear\n", LOG_CLIENT);
}

void CL_TimingInit (void)
{
	Cmd_AddCommand ("cl_timing", CL_Timing_f);
}
