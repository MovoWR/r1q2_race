VPATH=../../ref_gl ../../linux ../../game

CORE_RENDERER ?= 1
ifeq ($(CORE_RENDERER),1)
CFLAGS+=-DR1Q2_GL_CORE
endif

CFLAGS+=-fPIC $(shell sdl2-config --cflags)

ref_gl_SRC:=gl_jump.c gl_jump_lava.c gl_jump_beams.c gl_draw.c gl_image.c gl_hqx.c gl_light.c gl_mesh.c gl_model.c gl_core.c gl_core_state.c gl_core_shader.c gl_shader.c gl_postfx.c gl_rmain.c\
			gl_rmisc.c gl_rsurf.c gl_warp.c gl_sdl.c glob.c q_shared.c\
			q_shlinux.c qgl_linux.c

ref_gl_OBJ:=$(ref_gl_SRC:.c=.o)
ALLSRC:=$(ref_gl_SRC)

.PHONY: default combine

default: ref_gl.so

TARGETS:=ref_gl.so ref_gl-combine.so

include ../make.inc

LDFLAGS+=$(shell sdl2-config --libs) -lm -lGL -ljpeg -lpng

ref_gl.so: $(ref_gl_OBJ)
	$(CC) -shared -g -o $@ $^ $(LDFLAGS)

# only works with gcc >4.1
combine: ref_gl-combine.so
ref_gl-combine.so: $(ref_gl_SRC) .depends/race_version_generated.h
	$(CC) $(shell sdl2-config --cflags) -shared -O2 -ffloat-store -DNDEBUG -DLINUX -fPIC -fwhole-program --combine -DR1Q2_GENERATED_VERSION -I.depends -g -o $@ $(ref_gl_SRC) $(LDFLAGS)

-include .depends/*.d
