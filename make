./makeglyphs < Glyphs > rufl_glyph_map.c

/home/riscos/cross/bin/gcc -std=c99 -W -Wall -Wundef -Wpointer-arith -Wcast-qual -Wcast-align -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wnested-externs -Winline -Wno-unused-parameter -mpoke-function-name -I/home/riscos/env/include -L/home/riscos/env/lib -loslib -o rufl_test,ff8 rufl_test.c rufl_init.c rufl_quit.c rufl_dump_state.c rufl_character_set_test.c rufl_substitution_lookup.c rufl_paint.c rufl_glyph_map.c

/home/riscos/cross/bin/gcc -std=c99 -O3 -W -Wall -Wundef -Wpointer-arith -Wcast-qual -Wcast-align -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wnested-externs -Winline -Wno-unused-parameter -mpoke-function-name -I/home/riscos/env/include -L/home/riscos/env/lib -loslib -o rufl_chars,ff8 rufl_chars.c rufl_init.c rufl_quit.c rufl_dump_state.c rufl_character_set_test.c rufl_substitution_lookup.c rufl_paint.c rufl_glyph_map.c
