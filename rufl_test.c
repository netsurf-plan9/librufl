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

	try(rufl_init(), "rufl_init");
	rufl_dump_state();
	try(rufl_paint("NewHall", rufl_REGULAR, 240,
			utf8_test, sizeof utf8_test - 1,
			1200, 1200), "rufl_paint");
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
