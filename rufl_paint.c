/*
 * This file is part of RUfl
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license
 * Copyright 2005 James Bursa <james@semichrome.net>
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "oslib/font.h"
#include "rufl_internal.h"


typedef enum { rufl_PAINT, rufl_WIDTH, rufl_X_TO_OFFSET,
		rufl_SPLIT } rufl_action;
#define rufl_PROCESS_CHUNK 200

bool rufl_can_background_blend = false;


static rufl_code rufl_process(rufl_action action,
		const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string0, size_t length,
		int x, int y, os_trfm *trfm, unsigned int flags,
		int *width, int click_x, size_t *char_offset, int *actual_x);
static int rufl_family_list_cmp(const void *keyval, const void *datum);
static rufl_code rufl_process_span(rufl_action action,
		unsigned short *s, unsigned int n,
		unsigned int font, unsigned int font_size, int *x, int y,
		os_trfm *trfm, unsigned int flags,
		int click_x, size_t *offset);
static rufl_code rufl_process_span_old(rufl_action action,
		unsigned short *s, unsigned int n,
		unsigned int font, unsigned int font_size, int *x, int y,
		os_trfm *trfm, unsigned int flags,
		int click_x, size_t *offset);
static int rufl_unicode_map_search_cmp(const void *keyval, const void *datum);
static rufl_code rufl_process_not_available(rufl_action action,
		unsigned short *s, unsigned int n,
		unsigned int font_size, int *x, int y,
		os_trfm *trfm, unsigned int flags,
		int click_x, size_t *offset);
static rufl_code rufl_place_in_cache(unsigned int font, unsigned int font_size,
		font_f f);


/**
 * Render Unicode text.
 */

rufl_code rufl_paint(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int x, int y, unsigned int flags)
{
	return rufl_process(rufl_PAINT,
			font_family, font_style, font_size, string,
			length, x, y, 0, flags, 0, 0, 0, 0);
}


/**
 * Render Unicode text with a transformation matrix.
 *
 * Only transformations which keep the x-axis direction unchanged are
 * supported.
 */

rufl_code rufl_paint_transformed(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int x, int y, os_trfm *trfm, unsigned int flags)
{
	return rufl_process(rufl_PAINT,
			font_family, font_style, font_size, string,
			length, x, y, trfm, flags, 0, 0, 0, 0);
}


/**
 * Measure the width of Unicode text.
 */

rufl_code rufl_width(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int *width)
{
	return rufl_process(rufl_WIDTH,
			font_family, font_style, font_size, string,
			length, 0, 0, 0, 0, width, 0, 0, 0);
}


/**
 * Find the nearest character boundary in a string to where an x coordinate
 * falls.
 */

rufl_code rufl_x_to_offset(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int click_x,
		size_t *char_offset, int *actual_x)
{
	return rufl_process(rufl_X_TO_OFFSET,
			font_family, font_style, font_size, string,
			length, 0, 0, 0, 0, 0,
			click_x, char_offset, actual_x);
}


/**
 * Find the prefix of a string that will fit in a specified width.
 */

rufl_code rufl_split(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int width,
		size_t *char_offset, int *actual_x)
{
	return rufl_process(rufl_SPLIT,
			font_family, font_style, font_size, string,
			length, 0, 0, 0, 0, 0,
			width, char_offset, actual_x);
}


/**
 * Render, measure, or split Unicode text.
 */

rufl_code rufl_process(rufl_action action,
		const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string0, size_t length,
		int x, int y, os_trfm *trfm, unsigned int flags,
		int *width, int click_x, size_t *char_offset, int *actual_x)
{
	unsigned short s[rufl_PROCESS_CHUNK];
	unsigned int font;
	unsigned int font0, font1;
	unsigned int n;
	unsigned int u;
	size_t offset;
	size_t offset_u;
	size_t offset_map[rufl_PROCESS_CHUNK];
	char **family;
	const char *string = string0;
	struct rufl_character_set *charset;
	rufl_code code;

	assert(action == rufl_PAINT ||
			(action == rufl_WIDTH && width) ||
			(action == rufl_X_TO_OFFSET && char_offset &&
					actual_x) ||
			(action == rufl_SPLIT && char_offset &&
					actual_x));

	if ((flags & rufl_BLEND_FONT) && !rufl_can_background_blend) {
		/* unsuitable FM => clear blending bit */
		flags &= ~rufl_BLEND_FONT;
	}

	if (length == 0) {
		if (action == rufl_WIDTH)
			*width = 0;
		else if (action == rufl_X_TO_OFFSET || action == rufl_SPLIT) {
			*char_offset = 0;
			*actual_x = 0;
		}
		return rufl_OK;
	}
	if ((action == rufl_X_TO_OFFSET || action == rufl_SPLIT) &&
			click_x <= 0) {
		*char_offset = 0;
		*actual_x = 0;
		return rufl_OK;
	}

	family = bsearch(font_family, rufl_family_list,
			rufl_family_list_entries,
			sizeof rufl_family_list[0], rufl_family_list_cmp);
	if (!family)
		return rufl_FONT_NOT_FOUND;
	font = rufl_family_map[rufl_STYLES * (family - rufl_family_list) +
			font_style];
	charset = rufl_font_list[font].charset;

	offset_u = 0;
	rufl_utf8_read(string, length, u);
	if (charset && rufl_character_set_test(charset, u))
		font1 = font;
	else
		font1 = rufl_substitution_table[u];
	do {
		s[0] = u;
		offset_map[0] = offset_u;
		n = 1;
		font0 = font1;
		/* invariant: s[0..n) is in font font0 */
		while (0 < length && n < rufl_PROCESS_CHUNK && font1 == font0) {
			offset_u = string - string0;
			rufl_utf8_read(string, length, u);
			s[n] = u;
			offset_map[n] = offset_u;
			if (charset && rufl_character_set_test(charset, u))
				font1 = font;
			else
				font1 = rufl_substitution_table[u];
			if (font1 == font0)
				n++;
		}
		if (n == rufl_PROCESS_CHUNK)
			n--;
		s[n] = 0;
		offset_map[n] = offset_u;
		if (length == 0 && font1 == font0)
			offset_map[n] = string - string0;

		if (font0 == NOT_AVAILABLE)
			code = rufl_process_not_available(action, s, n,
					font_size, &x, y, trfm, flags,
					click_x, &offset);
		else if (rufl_old_font_manager)
			code = rufl_process_span_old(action, s, n, font0,
					font_size, &x, y, trfm, flags,
					click_x, &offset);
		else
			code = rufl_process_span(action, s, n, font0,
					font_size, &x, y, trfm, flags,
					click_x, &offset);

		if ((action == rufl_X_TO_OFFSET || action == rufl_SPLIT) &&
				(offset < n || click_x < x))
			break;
		if (code != rufl_OK)
			return code;

	} while (!(length == 0 && font1 == font0));

	if (action == rufl_WIDTH)
		*width = x;
	else if (action == rufl_X_TO_OFFSET || action == rufl_SPLIT) {
		*char_offset = offset_map[offset];
		*actual_x = x;
	}

	return rufl_OK;
}


int rufl_family_list_cmp(const void *keyval, const void *datum)
{
	const char *key = keyval;
	const char * const *entry = datum;
	return strcasecmp(key, *entry);
}


/**
 * Render a string of characters from a single RISC OS font.
 */

rufl_code rufl_process_span(rufl_action action,
		unsigned short *s, unsigned int n,
		unsigned int font, unsigned int font_size, int *x, int y,
		os_trfm *trfm, unsigned int flags,
		int click_x, size_t *offset)
{
	char font_name[80];
	unsigned short *split_point;
	int x_out, y_out;
	unsigned int i;
	font_f f;
	rufl_code code;

	/* search cache */
	for (i = 0; i != rufl_CACHE_SIZE; i++) {
		if (rufl_cache[i].font == font &&
				rufl_cache[i].size == font_size)
			break;
	}
	if (i != rufl_CACHE_SIZE) {
		/* found in cache */
		f = rufl_cache[i].f;
		rufl_cache[i].last_used = rufl_cache_time++;
	} else {
		/* not found */
		snprintf(font_name, sizeof font_name, "%s\\EUTF8",
				rufl_font_list[font].identifier);
		rufl_fm_error = xfont_find_font(font_name,
				font_size, font_size, 0, 0, &f, 0, 0);
		if (rufl_fm_error) {
			LOG("xfont_find_font: 0x%x: %s",
					rufl_fm_error->errnum,
					rufl_fm_error->errmess);
			return rufl_FONT_MANAGER_ERROR;
		}
		/* place in cache */
		code = rufl_place_in_cache(font, font_size, f);
		if (code != rufl_OK)
			return code;
	}

	if (action == rufl_PAINT) {
		/* paint span */
		rufl_fm_error = xfont_paint(f, (const char *) s,
				font_OS_UNITS |
				(trfm ? font_GIVEN_TRFM : 0) |
				font_GIVEN_LENGTH |
				font_GIVEN_FONT | font_KERN |
				font_GIVEN16_BIT |
				((flags & rufl_BLEND_FONT) ?
						font_BLEND_FONT : 0),
				*x, y, 0, trfm, n * 2);
		if (rufl_fm_error) {
			LOG("xfont_paint: 0x%x: %s",
					rufl_fm_error->errnum,
					rufl_fm_error->errmess);
			for (i = 0; i != n; i++)
				fprintf(stderr, "0x%x ", s[i]);
			fprintf(stderr, " (%u)\n", n);
			return rufl_FONT_MANAGER_ERROR;
		}
	}

	/* increment x by width of span */
	if (action == rufl_X_TO_OFFSET || action == rufl_SPLIT) {
		rufl_fm_error = xfont_scan_string(f, (const char *) s,
				(trfm ? font_GIVEN_TRFM : 0) |
				font_GIVEN_LENGTH | font_GIVEN_FONT |
				font_KERN | font_GIVEN16_BIT |
				((action == rufl_X_TO_OFFSET) ?
						font_RETURN_CARET_POS : 0),
				(click_x - *x) * 400, 0x7fffffff, 0, trfm,
				n * 2,
				(char **) &split_point, &x_out, &y_out, 0);
		*offset = split_point - s;
	} else {
		rufl_fm_error = xfont_scan_string(f, (const char *) s,
				(trfm ? font_GIVEN_TRFM : 0) |
				font_GIVEN_LENGTH | font_GIVEN_FONT |
				font_KERN | font_GIVEN16_BIT,
				0x7fffffff, 0x7fffffff, 0, trfm, n * 2,
				0, &x_out, &y_out, 0);
	}
	if (rufl_fm_error) {
		LOG("xfont_scan_string: 0x%x: %s",
				rufl_fm_error->errnum,
				rufl_fm_error->errmess);
		return rufl_FONT_MANAGER_ERROR;
	}
	*x += x_out / 400;

	return rufl_OK;
}


/**
 * Render a string of characters from a single RISC OS font  (old font manager
 * version).
 */

rufl_code rufl_process_span_old(rufl_action action,
		unsigned short *s, unsigned int n,
		unsigned int font, unsigned int font_size, int *x, int y,
		os_trfm *trfm, unsigned int flags,
		int click_x, size_t *offset)
{
	char s2[rufl_PROCESS_CHUNK];
	char *split_point;
	const char *font_name = rufl_font_list[font].identifier;
	int x_out, y_out;
	unsigned int i;
	font_f f;
	rufl_code code;
	struct rufl_unicode_map_entry *entry;

	/* search cache */
	for (i = 0; i != rufl_CACHE_SIZE; i++) {
		if (rufl_cache[i].font == font &&
				rufl_cache[i].size == font_size)
			break;
	}
	if (i != rufl_CACHE_SIZE) {
		/* found in cache */
		f = rufl_cache[i].f;
		rufl_cache[i].last_used = rufl_cache_time++;
	} else {
		/* not found */
		rufl_fm_error = xfont_find_font(font_name,
				font_size, font_size, 0, 0, &f, 0, 0);
		if (rufl_fm_error)
			return rufl_FONT_MANAGER_ERROR;
		/* place in cache */
		code = rufl_place_in_cache(font, font_size, f);
		if (code != rufl_OK)
			return code;
	}

	/* convert Unicode string into character string */
	for (i = 0; i != n; i++) {
		entry = bsearch(&s[i], rufl_font_list[font].umap->map,
				rufl_font_list[font].umap->entries,
				sizeof rufl_font_list[font].umap->map[0],
				rufl_unicode_map_search_cmp);
		s2[i] = entry->c;
	}
	s2[i] = 0;

	if (action == rufl_PAINT) {
		/* paint span */
		rufl_fm_error = xfont_paint(f, s2, font_OS_UNITS |
				(trfm ? font_GIVEN_TRFM : 0) |
				font_GIVEN_LENGTH | font_GIVEN_FONT |
				font_KERN |
				((flags & rufl_BLEND_FONT) ?
						font_BLEND_FONT : 0),
				*x, y, 0, trfm, n);
		if (rufl_fm_error)
			return rufl_FONT_MANAGER_ERROR;
	}

	/* increment x by width of span */
	if (action == rufl_X_TO_OFFSET || action == rufl_SPLIT) {
		rufl_fm_error = xfont_scan_string(f, s2,
				(trfm ? font_GIVEN_TRFM : 0) |
				font_GIVEN_LENGTH | font_GIVEN_FONT |
				font_KERN |
				((action == rufl_X_TO_OFFSET) ?
						font_RETURN_CARET_POS : 0),
				(click_x - *x) * 400, 0x7fffffff, 0, trfm, n,
				&split_point, &x_out, &y_out, 0);
		*offset = split_point - s2;
	} else {
		rufl_fm_error = xfont_scan_string(f, s2,
				(trfm ? font_GIVEN_TRFM : 0) |
				font_GIVEN_LENGTH | font_GIVEN_FONT | font_KERN,
				0x7fffffff, 0x7fffffff, 0, trfm, n,
				0, &x_out, &y_out, 0);
	}
	if (rufl_fm_error)
		return rufl_FONT_MANAGER_ERROR;
	*x += x_out / 400;

	return rufl_OK;
}


int rufl_unicode_map_search_cmp(const void *keyval, const void *datum)
{
	const unsigned short *key = keyval;
	const struct rufl_unicode_map_entry *entry = datum;
	if (*key < entry->u)
		return -1;
	else if (entry->u < *key)
		return 1;
	return 0;
}


/**
 * Render a string of characters not available in any font as their hex code.
 */

rufl_code rufl_process_not_available(rufl_action action,
		unsigned short *s, unsigned int n,
		unsigned int font_size, int *x, int y,
		os_trfm *trfm, unsigned int flags,
		int click_x, size_t *offset)
{
	char missing[] = "0000";
	int dx = 7 * font_size *
			(trfm ? trfm->entries[0][0] / 0x10000 : 1) / 64;
	unsigned int i;
	font_f f;
	rufl_code code;

	if (action == rufl_WIDTH) {
		*x += n * dx;
		return rufl_OK;
	} else if (action == rufl_X_TO_OFFSET || action == rufl_SPLIT) {
		if (click_x - *x < (int) (n * dx))
			*offset = (click_x - *x) / dx;
		else
			*offset = n;
		*x += *offset * dx;
		return rufl_OK;
	}

	/* search cache */
	for (i = 0; i != rufl_CACHE_SIZE; i++) {
		if (rufl_cache[i].font == rufl_CACHE_CORPUS &&
				rufl_cache[i].size == font_size)
			break;
	}
	if (i != rufl_CACHE_SIZE) {
		/* found in cache */
		f = rufl_cache[i].f;
		rufl_cache[i].last_used = rufl_cache_time++;
	} else {
		/* not found */
		rufl_fm_error = xfont_find_font("Corpus.Medium\\ELatin1",
				font_size / 2, font_size / 2, 0, 0,
				&f, 0, 0);
		if (rufl_fm_error)
			return rufl_FONT_MANAGER_ERROR;
		/* place in cache */
		code = rufl_place_in_cache(rufl_CACHE_CORPUS, font_size, f);
		if (code != rufl_OK)
			return code;
	}

	for (i = 0; i != n; i++) {
		missing[0] = "0123456789abcdef"[(s[i] >> 12) & 0xf];
		missing[1] = "0123456789abcdef"[(s[i] >> 8) & 0xf];
		missing[2] = "0123456789abcdef"[(s[i] >> 4) & 0xf];
		missing[3] = "0123456789abcdef"[(s[i] >> 0) & 0xf];

		/* first two characters in top row */
		rufl_fm_error = xfont_paint(f, missing, font_OS_UNITS |
				(trfm ? font_GIVEN_TRFM : 0) |
				font_GIVEN_LENGTH | font_GIVEN_FONT |
				font_KERN |
				((flags & rufl_BLEND_FONT) ?
						font_BLEND_FONT : 0),
				*x, y + (trfm ? trfm->entries[1][1] / 0x10000 :
				1) * 5 * font_size / 64,
				0, trfm, 2);
		if (rufl_fm_error)
			return rufl_FONT_MANAGER_ERROR;

		/* last two characters underneath */
		rufl_fm_error = xfont_paint(f, missing + 2, font_OS_UNITS |
				(trfm ? font_GIVEN_TRFM : 0) |
				font_GIVEN_LENGTH | font_GIVEN_FONT |
				font_KERN |
				((flags & rufl_BLEND_FONT) ?
						font_BLEND_FONT : 0),
				*x, y, 0, trfm, 2);
		if (rufl_fm_error)
			return rufl_FONT_MANAGER_ERROR;

		*x += dx;
	}

	return rufl_OK;
}


/**
 * Place a font into the recent-use cache, making space if necessary.
 */

rufl_code rufl_place_in_cache(unsigned int font, unsigned int font_size,
		font_f f)
{
	unsigned int i;
	unsigned int max_age = 0;
	unsigned int evict = 0;

	for (i = 0; i != rufl_CACHE_SIZE; i++) {
		if (rufl_cache[i].font == rufl_CACHE_NONE) {
			evict = i;
			break;
		} else if (max_age < rufl_cache_time -
				rufl_cache[i].last_used) {
			max_age = rufl_cache_time -
					rufl_cache[i].last_used;
			evict = i;
		}
	}
	if (rufl_cache[evict].font != rufl_CACHE_NONE) {
		rufl_fm_error = xfont_lose_font(rufl_cache[evict].f);
		if (rufl_fm_error)
			return rufl_FONT_MANAGER_ERROR;
	}
	rufl_cache[evict].font = font;
	rufl_cache[evict].size = font_size;
	rufl_cache[evict].f = f;
	rufl_cache[evict].last_used = rufl_cache_time++;

	return rufl_OK;
}
