/* Native protocol 34/35 packet observation; no wire or scheduling changes. */
#include "jump_compat.h"
#include "sh_netmeter.h"
#include <limits.h>

static struct
{
	unsigned sequence, time;
	qboolean valid;
} jump_packets[CMD_BACKUP];

static struct
{
	unsigned start, last_render, render_msec, moves, packets, renders, clients, last_move, move_msec, demo_frames;
	float moves_per_second, packets_per_second, renders_per_second, clients_per_second;
	qboolean started, rendered, moved;
} jump_measure;

void CL_JumpClearStats (void)
{
	memset (&jump_measure, 0, sizeof (jump_measure));
}

static void CL_JumpUpdateRates (void)
{
	unsigned elapsed;

	if (!jump_measure.started)
	{
		jump_measure.start = cls.realtime;
		jump_measure.started = true;
		return;
	}
	elapsed = cls.realtime - jump_measure.start;
	if (elapsed < 1000)
		return;
	jump_measure.moves_per_second = jump_measure.moves * 1000.0f / elapsed;
	jump_measure.packets_per_second = jump_measure.packets * 1000.0f / elapsed;
	jump_measure.renders_per_second = jump_measure.renders * 1000.0f / elapsed;
	jump_measure.clients_per_second = jump_measure.clients * 1000.0f / elapsed;
	jump_measure.moves = jump_measure.packets = jump_measure.renders = jump_measure.clients = 0;
	jump_measure.start = cls.realtime;
}

void CL_JumpMoveFrame (void)
{
	if (!CL_JumpEnabled () || cls.state != ca_active)
		return;
	CL_JumpUpdateRates ();
	if (jump_measure.moved)
		jump_measure.move_msec = cls.realtime - jump_measure.last_move;
	jump_measure.last_move = cls.realtime;
	jump_measure.moved = true;
	jump_measure.moves++;
}

void CL_JumpClientFrame (void)
{
	CL_JumpSyncGame ();
	CL_JumpUIFrame ();
	if (!CL_JumpEnabled () || cls.state != ca_active)
		return;
	CL_JumpUpdateRates ();
	jump_measure.clients++;
}

void CL_JumpRenderFrame (void)
{
	if (!CL_JumpEnabled ())
		return;
	if (jump_measure.rendered)
		jump_measure.render_msec = cls.realtime - jump_measure.last_render;
	jump_measure.last_render = cls.realtime;
	jump_measure.rendered = true;
	jump_measure.renders++;
	CL_JumpUpdateRates ();
}

void CL_JumpClearNetwork (void)
{
	memset (jump_packets, 0, sizeof (jump_packets));
	SH_NetMeter_Clear ();
	CL_JumpClearStats ();
}

void CL_JumpPacketSent (void)
{
	unsigned sequence, index;

	if (!CL_JumpEnabled ())
		return;
	sequence = (unsigned)cls.netchan.outgoing_sequence;
	index = sequence & (CMD_BACKUP - 1);
	jump_packets[index].sequence = sequence;
	jump_packets[index].time = cls.realtime;
	jump_packets[index].valid = true;
	CL_JumpUpdateRates ();
	jump_measure.packets++;
}

void CL_JumpPacketSample (void)
{
	unsigned sequence, index, ping;

	if (!CL_JumpEnabled () || cls.state != ca_active)
		return;
	if (cl.attractloop)
		return;
	sequence = (unsigned)cls.netchan.incoming_acknowledged;
	index = sequence & (CMD_BACKUP - 1);
	if (!jump_packets[index].valid || jump_packets[index].sequence != sequence ||
		(unsigned)cls.netchan.outgoing_sequence - sequence > CMD_BACKUP)
		return;
	ping = cls.realtime - jump_packets[index].time;
	if (ping > INT_MAX)
		return;
	SH_NetMeter_Sample (ping);
}

size_t R_Fps_m (char *out, size_t size)
{
	return Com_sprintf (out, (int)size, "%.1f", jump_measure.render_msec ? 1000.0f / jump_measure.render_msec : 0);
}

size_t CL_Mmps_m (char *out, size_t size)
{
	return Com_sprintf (out, (int)size, "%.2f", jump_measure.moves_per_second);
}

size_t CL_Pps_m (char *out, size_t size)
{
	return Com_sprintf (out, (int)size, "%.0f", jump_measure.packets_per_second);
}

size_t CL_Ping_m (char *out, size_t size)
{
	return Com_sprintf (out, (int)size, "%u", SH_NetMeter_GetAvgPing ());
}

size_t CL_Lag_m (char *out, size_t size)
{
	return Com_sprintf (out, (int)size, "%.2f%%", cls.netchan.total_received ? 100.0f * cls.netchan.total_dropped / cls.netchan.total_received : 0);
}

size_t R_Mfps_m (char *out, size_t size)
{
	return Com_sprintf (out, (int)size, "%.2f", jump_measure.renders_per_second);
}

size_t CL_Mfps_m (char *out, size_t size)
{
	return Com_sprintf (out, (int)size, "%.0f", jump_measure.clients_per_second);
}

size_t CL_Mps_m (char *out, size_t size)
{
	return Com_sprintf (out, (int)size, "%.2f", jump_measure.move_msec ? 1000.0f / jump_measure.move_msec : 0);
}

size_t CL_DemoPos_m (char *out, size_t size)
{
	unsigned frames = cl.attractloop ? jump_measure.demo_frames : 0;
	return Com_sprintf (out, (int)size, "%u:%02u.%u", frames / 600, (frames / 10) % 60, frames % 10);
}

void CL_JumpDemoFrame (void)
{
	if (CL_JumpEnabled () && cl.attractloop && cl.frame.valid)
		jump_measure.demo_frames++;
}
