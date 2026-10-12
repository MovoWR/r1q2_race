/* Recognized server status-bar token contract. Map-name string operands vary,
 * and groups may have noncontiguous fragments.
 */
#pragma once
static const hud_layout_fragment_t server_statusbar_layout[] = {
	{ "if 1 yb -32 xv 310 hnum yb -8 xv 312 string2 \"Health\" endif", HL_HEALTH },
	{ "if 6 yb -32 xv 280 pic 6 endif", HL_ITEM },
	{ "if 7 yb -32 xv 200 num 4 7 xv 226 yb -8 string2 \"Speed\" endif", HL_SPEED },
	{ "if 27 xv 112 yb -58 stat_string 27 endif", HL_TARGET },
	{ "yb -16 xr -24 string2 \".\" yb -32 xr -94 num 4 17 xr -18 num 1 19", HL_TIMER },
	{ "xl 2 yb -42 if 20 pic 20 endif if 21 pic 21 endif if 22 pic 22 endif if 25 pic 25 endif", HL_INPUTS },
	{ "if 23 xl 0 yb -76 num 3 23 xl 54 yb -60 string2 \"FPS\" endif", HL_SERVER_FPS },
	{ "if 8 xl 2 yb -136 stat_string 8 yb -128 stat_string 2 yb -112 stat_string 11 yb -104 stat_string 9 endif", HL_VOTE },
	{ "if 18 xr -32 yt 42 stat_string 18 yt 50 string \"Maps\" endif", HL_MAPCOUNT },
	{ "xv 72 yb -32 stat_string 24", HL_STATUS1 },
	{ "yb -24 stat_string 26", HL_STATUS2 },
	{ "yb -16 stat_string 30", HL_STATUS3 },
	{ "yb -8 stat_string 31", HL_STATUS4 },
	{ "xr -128 yt 2 string @map", HL_MAP },
	{ "yt 10 string @map", HL_PREVMAP1 },
	{ "yt 18 string @map", HL_PREVMAP2 },
	{ "yt 26 string @map", HL_PREVMAP3 },
	{ "xr -32 yt 100 stat_string 10", HL_ADDEDTIME },
	{ "yt 64 string2 \"Time\"", HL_TIMELEFT },
	{ "yb -8 string2 \"Time\"", HL_TIMER },
	{ "xr -50 yt 74 num 3 28", HL_TIMELEFT },
};
