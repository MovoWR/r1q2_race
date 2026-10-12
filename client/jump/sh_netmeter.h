#ifndef SH_NETMETER_H
#define SH_NETMETER_H

#ifdef __cplusplus
extern "C"
{
#endif

	extern cvar_t *sh_netmeter;
	extern cvar_t *sh_netwarn_ping_adaptive;
	extern cvar_t *sh_netwarn_spike_ms;
	extern cvar_t *sh_netwarn_spike_pct;
	extern cvar_t *sh_netwarn_jitter_ms;
	extern cvar_t *sh_netalert;
	extern cvar_t *sh_netalert_x;
	extern cvar_t *sh_netalert_y;
	extern cvar_t *sh_netalert_color;
	extern cvar_t *sh_netalert_alpha;
	extern cvar_t *sh_netalert_duration_ms;
	extern cvar_t *sh_netalert_loss;
	extern cvar_t *sh_netalert_jitter;
	extern cvar_t *sh_netalert_spike;
	extern cvar_t *sh_lagometer_x;
	extern cvar_t *sh_lagometer_y;
	extern cvar_t *sh_netmeter_min_ms;
	extern cvar_t *sh_netmeter_max_ms;
	extern cvar_t *sh_netmeter_adaptive;
	extern cvar_t *sh_lagometer_color_normal;
	extern cvar_t *sh_lagometer_color_spike;
	extern cvar_t *sh_lagometer_color_jitter;
	extern cvar_t *sh_lagometer_color_loss_s2c;
	extern cvar_t *sh_lagometer_color_loss_c2s;
	extern cvar_t *sh_lagometer_alpha;
	extern cvar_t *sh_lagometer_bad_alpha;
	extern cvar_t *sh_netgraph_y;
	extern cvar_t *sh_netgraph_height;
	extern cvar_t *sh_netgraph_alpha;
	extern cvar_t *sh_netgraph_color_normal;
	extern cvar_t *sh_netgraph_color_spike;
	extern cvar_t *sh_netgraph_color_jitter;
	extern cvar_t *sh_netgraph_color_loss_s2c;
	extern cvar_t *sh_netgraph_color_loss_c2s;
	extern cvar_t *sh_histogram_x;
	extern cvar_t *sh_histogram_y;
	extern cvar_t *sh_histogram_width_mode;
	extern cvar_t *sh_histogram_width;
	extern cvar_t *sh_histogram_height;
	extern cvar_t *sh_histogram_fill_mode;
	extern cvar_t *sh_histogram_spacing_mode;
	extern cvar_t *sh_histogram_bg_alpha;
	extern cvar_t *sh_histogram_color_bg;
	extern cvar_t *sh_histogram_color_normal;
	extern cvar_t *sh_histogram_color_spike;
	extern cvar_t *sh_histogram_color_jitter;
	extern cvar_t *sh_histogram_color_loss_s2c;
	extern cvar_t *sh_histogram_color_loss_c2s;
	extern cvar_t *sh_histogram_alpha;
	extern cvar_t *sh_histogram_bad_alpha;
	extern cvar_t *sh_histogram_history;
	extern cvar_t *sh_histogram_ping;

	void SH_NetMeter_Sample (unsigned ping);
	void SH_NetMeter_PredictionError (int len);
	void SH_NetMeter_Draw (void);
	void SH_NetMeter_Clear (void);
	unsigned SH_NetMeter_GetAvgPing (void);

#ifdef __cplusplus
}
#endif

#endif
