/*
 * This file is part of RUfl
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license
 * Copyright 2005 James Bursa <james@semichrome.net>
 */

#include <stdbool.h>
#include <stdlib.h>
#include "oslib/os.h"


/** Return code for RUfl functions. */
typedef enum {
	/** Success. */
	rufl_OK,
	/** Failure: memory was exhausted. */
	rufl_OUT_OF_MEMORY,
	/** Failure: Font Manager error; details in rufl_fm_error. */
	rufl_FONT_MANAGER_ERROR,
	/** Failure: no font with this name exists. */
	rufl_FONT_NOT_FOUND,
	/** Failure: file input / output error: details in errno. */
	rufl_IO_ERROR,
	/** Failure: file input unexpected eof. */
	rufl_IO_EOF,
} rufl_code;


typedef enum {
	rufl_REGULAR,
	rufl_SLANTED,
	rufl_BOLD,
	rufl_BOLD_SLANTED,
} rufl_style;


/** Last Font Manager error. */
extern os_error *rufl_fm_error;

/** List of available font families. */
extern char **rufl_family_list;
/** Number of entries in rufl_family_list. */
extern unsigned int rufl_family_list_entries;


/**
 * Initialise RUfl.
 *
 * All available fonts are scanned. May take some time.
 */

rufl_code rufl_init(void);


/**
 * Render Unicode text.
 */

rufl_code rufl_paint(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int x, int y);


/**
 * Dump the internal library state to stdout.
 */

void rufl_dump_state(void);


/**
 * Free all resources used by the library.
 */

void rufl_quit(void);
