#
# This file is part of RUfl
# Licensed under the MIT License,
#                http://www.opensource.org/licenses/mit-license
# Copyright 2005 James Bursa <james@semichrome.net>
#

# choose one of the two below
COMPILER = gcc
#COMPILER = norcroft


SOURCE = rufl_init.c rufl_quit.c rufl_dump_state.c \
	rufl_character_set_test.c \
	rufl_paint.c rufl_glyph_map.c rufl_invalidate_cache.c \
	rufl_find.c rufl_decompose.c rufl_metrics.c
HDRS = rufl.h rufl_internal.h

.PHONY: all install clean

ifeq ($(COMPILER), gcc)
# cross-compiling using GCCSDK or native build with GCC

HOST := $(shell uname -s)
ifeq ($(HOST),)
  HOST := riscos
  $(warning Build platform determination failed but that's a known problem for RISC OS so we're assuming a native RISC OS build.)
else
  ifeq ($(HOST),RISC OS)
    # Fixup uname -s returning "RISC OS"
    HOST := riscos
  endif
endif

ifeq ($(HOST),riscos)
  GCCSDK_INSTALL_ENV ?= <NSLibs$$Dir>
  CC := gcc
  AR := ar
else
  GCCSDK_INSTALL_CROSSBIN ?= /home/riscos/cross/bin
  GCCSDK_INSTALL_ENV ?= /home/riscos/env
  CC := $(wildcard $(GCCSDK_INSTALL_CROSSBIN)/*gcc)
  AR := $(wildcard $(GCCSDK_INSTALL_CROSSBIN)/*ar)
endif

CFLAGS = -std=c99 -O3 -W -Wall -Wundef -Wpointer-arith -Wcast-qual \
	-Wcast-align -Wwrite-strings -Wstrict-prototypes \
	-Wmissing-prototypes -Wmissing-declarations \
	-Wnested-externs -Winline -Wno-unused-parameter \
	-mpoke-function-name -I$(GCCSDK_INSTALL_ENV)/include
ARFLAGS = cr
LIBS = -L$(GCCSDK_INSTALL_ENV)/lib -lOSLib32
INSTALL = $(GCCSDK_INSTALL_ENV)/ro-install
OBJS = $(SOURCE:.c=.o)
ifneq (,$(findstring arm-unknown-riscos-gcc,$(CC)))
  EXEEXT=,e1f
else
  EXEEXT=,ff8
endif

all: librufl.a rufl_test$(EXEEXT) rufl_chars$(EXEEXT)

librufl.a: $(OBJS)
	$(AR) $(ARFLAGS) $@ $(OBJS)

install: librufl.a
	$(INSTALL) librufl.a $(GCCSDK_INSTALL_ENV)/lib/librufl.a
	$(INSTALL) rufl.h $(GCCSDK_INSTALL_ENV)/include/rufl.h
else
# compiling on RISC OS using Norcroft
CC = cc
CFLAGS = -fn -ecz -wap -IOSLib: -DNDEBUG
LD = link
LDFLAGS = -aof
LIBS = OSLib:o.oslib32
MKDLK = makedlk
SOURCE += strfuncs.c
OBJS = $(SOURCE:.c=.o)
EXEEXT =

all: librufl.a rufl/pyd rufl_test rufl_chars

librufl.a: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
o.ruflmodule: ruflmodule.o librufl.a
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)
ruflmodule.o: ruflmodule.c
	$(CC) -fn -wp -IPyInc:Include,PyInc:RISCOS,TCPIPLibs:,OSLib: -c $@ $<
rufl/pyd: o.ruflmodule
	$(MKDLK) -s <Python$$Dir>.RISCOS.s.linktab -o $< -d $@ -e initrufl
endif


# common rules
rufl_glyph_map.c: Glyphs makeglyphs
	./makeglyphs < Glyphs > $@

rufl_test$(EXEEXT): rufl_test.c librufl.a
	$(CC) $(CFLAGS) $(LIBS) -o $@ $^

rufl_chars$(EXEEXT): rufl_chars.c librufl.a
	$(CC) $(CFLAGS) $(LIBS) -o $@ $^

.c.o: $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-rm *.o librufl.a rufl_glyph_map.c rufl_test$(EXEEXT) rufl_chars$(EXEEXT)
