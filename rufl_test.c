/*
 * This file is part of RUfl
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license
 * Copyright 2005 James Bursa <james@semichrome.net>
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "rufl.h"


static void try(rufl_code code, const char *context);


int main(void)
{
	char utf8_test[] = "Hello,	world! ὕαλον "
			"Uherské Hradiště.";
	int width;
	size_t char_offset;
	int x;
	int actual_x;

	try(rufl_init(), "rufl_init");
	rufl_dump_state();
	try(rufl_paint("NewHall", rufl_REGULAR, 240,
			utf8_test, sizeof utf8_test - 1,
			1200, 1200, 0), "rufl_paint");
	try(rufl_width("NewHall", rufl_REGULAR, 240,
			utf8_test, sizeof utf8_test - 1,
			&width), "rufl_width");
	printf("width: %i\n", width);
	for (x = 0; x < width + 100; x += 100) {
		try(rufl_x_to_offset("NewHall", rufl_REGULAR, 240,
				utf8_test, sizeof utf8_test - 1,
				x, &char_offset, &actual_x),
				"rufl_x_to_offset");
		printf("x to offset: %i -> %i %i \"%s\"\n", x, actual_x,
				char_offset, utf8_test + char_offset);
		try(rufl_split("NewHall", rufl_REGULAR, 240,
				utf8_test, sizeof utf8_test - 1,
				x, &char_offset, &actual_x),
				"rufl_split");
		printf("split: %i -> %i %i \"%s\"\n", x, actual_x,
				char_offset, utf8_test + char_offset);
	}
	rufl_quit();

	return 0;
}


void try(rufl_code code, const char *context)
{
	if (code == rufl_OK)
		return;
	else if (code == rufl_OUT_OF_MEMORY)
		printf("error: %s: out of memory\n", context);
	else if (code == rufl_FONT_MANAGER_ERROR)
		printf("error: %s: Font Manager error %x %s\n", context,
				rufl_fm_error->errnum,
				rufl_fm_error->errmess);
	else if (code == rufl_FONT_NOT_FOUND)
		printf("error: %s: font not found\n", context);
	else if (code == rufl_IO_ERROR)
		printf("error: %s: io error: %i %s\n", context, errno,
				strerror(errno));
	else if (code == rufl_IO_EOF)
		printf("error: %s: eof\n", context);
	else
		printf("error: %s: unknown error\n", context);
	rufl_quit();
	exit(1);
}
