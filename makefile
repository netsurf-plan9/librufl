#
# This file is part of RUfl
# Licensed under the MIT License,
#                http://www.opensource.org/licenses/mit-license
# Copyright 2005 James Bursa <james@semichrome.net>
#

CC = /home/riscos/cross/bin/gcc
CFLAGS = -std=c99 -O3 -W -Wall -Wundef -Wpointer-arith -Wcast-qual \
	-Wcast-align -Wwrite-strings -Wstrict-prototypes \
	-Wmissing-prototypes -Wmissing-declarations \
	-Wnested-externs -Winline -Wno-unused-parameter \
	-mpoke-function-name -I/home/riscos/env/include
LIBS = -L/home/riscos/env/lib -loslib

SOURCE = rufl_init.c rufl_quit.c rufl_dump_state.c \
	rufl_character_set_test.c rufl_substitution_lookup.c \
	rufl_paint.c rufl_glyph_map.c rufl_invalidate_cache.c

all: rufl.o rufl_test,ff8 rufl_chars,ff8

rufl.o: $(SOURCE) Glyphs
	$(CC) $(CFLAGS) -c -o $@ $(SOURCE)

rufl_glyph_map.c: Glyphs makeglyphs
	./makeglyphs < Glyphs > $@

rufl_test,ff8: rufl_test.c rufl.o
	$(CC) $(CFLAGS) $(LIBS) -o $@ $^

rufl_chars,ff8: rufl_chars.c rufl.o
	$(CC) $(CFLAGS) $(LIBS) -o $@ $^


clean:
	-rm rufl.o rufl_glyph_map.c rufl_test,ff8 rufl_chars,ff8
