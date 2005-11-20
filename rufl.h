/*
 * This file is part of RUfl
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license
 * Copyright 2005 James Bursa <james@semichrome.net>
 */

#ifndef RUFL_H
#define RUFL_H

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
	rufl_REGULAR = 0,
	rufl_SLANTED = 1,
	rufl_BOLD = 2,
	rufl_BOLD_SLANTED = 3,
} rufl_style;

/** rufl_paint(_transformed) flags */
#define rufl_BLEND_FONT 0x01

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
		int x, int y, unsigned int flags);


/**
 * Render Unicode text with a transformation matrix.
 *
 * Only transformations which keep the x-axis direction unchanged are
 * supported.
 */

rufl_code rufl_paint_transformed(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int x, int y, os_trfm *trfm, unsigned int flags);


/**
 * Measure the width of Unicode text.
 */

rufl_code rufl_width(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int *width);


/**
 * Find where in a string a x coordinate falls.
 */

rufl_code rufl_x_to_offset(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int click_x,
		size_t *char_offset, int *actual_x);


/**
 * Find the prefix of a string that will fit in a specified width.
 */

rufl_code rufl_split(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int width,
		size_t *char_offset, int *actual_x);


/** Type of callback function for rufl_paint_callback(). */
typedef void (*rufl_callback_t)(void *context,
		const char *font_name, unsigned int font_size,
		const char *s8, unsigned short *s16, unsigned int n,
		int x, int y);


/**
 * Render text, but call a callback instead of each call to Font_Paint.
 */

rufl_code rufl_paint_callback(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int x, int y,
		rufl_callback_t callback, void *context);


/**
 * Dump the internal library state to stdout.
 */

void rufl_dump_state(void);


/**
 * Clear the internal font handle cache.
 *
 * Call this function on mode changes or output redirection changes.
 */

void rufl_invalidate_cache(void);


/**
 * Free all resources used by the library.
 */

void rufl_quit(void);


#endif
