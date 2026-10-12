VPATH=../../game ../../linux ../../qcommon ../../server ../../client ../../client/jump	

quake2_SRC:=cl_cin.c cl_media.c cl_movie.c cl_ents.c cl_fx.c cl_input.c cl_inv.c cl_main.c cl_timing.c cl_translate.c\
	    cl_parse.c cl_pred.c cl_tent.c cl_scrn.c cl_view.c cl_newfx.c\
	    jump_commands.c jump_prediction.c jump_config.c jump_input.c\
	    sh_nerdstats.c sh_netmeter.c jump_network.c jump_widgets.c jump_layout.c jump_layout_native.c jump_editor.c jump_ui_compat.c jump_menu.c jump_script.c jump_ui.c jump_builtin_menu.c jump_menu_services.c jump_sh_commands.c jump_playerconfig.c jump_playermodels.c jump_demos.c jump_display.c jump_video_ui.c jump_browser_ui.c jump_servers.c jump_servers_http.c jump_field.c jump_reminders.c jump_beams.c jump_draw.c strafe_helper_draw.c strafe_efficiency.c strafe_helper_customization.c sh_efficiency_draw.c\
	    con_prompt.c console.c keys.c menu.c snd_dma.c snd_mem.c snd_ogg.c snd_stream.c snd_music.c snd_music_al.c snd_movie_al.c snd_mix.c qmenu.c\
	    m_flash.c\
	    cmd.c cmodel.c common.c crc.c cvar.c files.c md4.c net_chan.c\
	    sv_ccmds.c sv_ents.c sv_game.c sv_init.c sv_main.c sv_send.c\
	    sv_user.c sv_world.c \
	    q_shlinux.c vid_menu.c vid_so.c sys_linux.c glob.c net_udp.c\
	    q_shared.c pmove.c mersennetwister.c le_util.c\
	    le_physics.c redblack.c cd_linux.c snd_linux.c unzip.c ioapi.c

# The optional backend resolves OpenAL dynamically; libdl is linked below.
USE_OPENAL?=0
ifeq ($(USE_OPENAL),1)
PKG_CONFIG?=pkg-config
ifeq ($(shell $(PKG_CONFIG) --exists openal && echo yes),)
$(error USE_OPENAL=1 requires OpenAL development headers and pkg-config)
endif
quake2_SRC+=al_linux.c qal_linux.c
CFLAGS+=-DUSE_OPENAL $(shell $(PKG_CONFIG) --cflags openal)
endif

CFLAGS+=-DCINEMATICS
quake2_OBJ:=$(quake2_SRC:.c=.o)
ALLSRC:=$(quake2_SRC)

.PHONY: default combine

default: quake2

TARGETS:=quake2 quake2-combine

include ../make.inc

LDFLAGS+=-lm -lz

# Optional audio/movie decoders; standalone Windows codec delivery is separate.
ifeq ($(USE_AVCODEC),1)
PKG_CONFIG?=pkg-config
FFMPEG_LIBS:=libavformat libavcodec libswresample libswscale libavutil
ifeq ($(shell $(PKG_CONFIG) --exists 'libavcodec >= 59.37.100' 'libavformat >= 59.27.100' 'libavutil >= 57.28.100' 'libswresample >= 4.7.100' 'libswscale >= 6.7.100' && echo yes),)
$(error USE_AVCODEC=1 requires FFmpeg 5.1 or newer development libraries and pkg-config)
endif
CFLAGS+=-DUSE_AVCODEC $(shell $(PKG_CONFIG) --cflags $(FFMPEG_LIBS))
LDFLAGS+=$(shell $(PKG_CONFIG) --libs --static $(FFMPEG_LIBS))
endif

ifeq ($(shell uname),Linux)
LDFLAGS+=-ldl
endif

ifeq ($(shell uname),FreeBSD)
LDFLAGS+=-lexecinfo
endif


quake2: $(quake2_OBJ)
	$(CC) -g -o $@ $^ $(LDFLAGS)

# only works with gcc >4.1
combine: quake2-combine
quake2-combine: $(quake2_SRC)
	$(CC) -O2 -ffloat-store -DNDEBUG -DLINUX -fwhole-program --combine -g -o $@ $^ $(LDFLAGS)

-include .depends/*.d
