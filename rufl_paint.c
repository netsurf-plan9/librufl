/*
 * This file is part of RUfl
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license
 * Copyright 2005 James Bursa <james@semichrome.net>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oslib/font.h"
#include "rufl_internal.h"


typedef enum { rufl_PAINT, rufl_WIDTH } rufl_action;


static rufl_code rufl_process(rufl_action action,
		const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int x, int y, int *width);
static int rufl_family_list_cmp(const void *keyval, const void *datum);
static rufl_code rufl_process_span(rufl_action action,
		unsigned short *s, unsigned int n,
		unsigned int font, unsigned int font_size, int *x, int y);
static rufl_code rufl_process_span_old(rufl_action action,
		unsigned short *s, unsigned int n,
		unsigned int font, unsigned int font_size, int *x, int y);
static int rufl_unicode_map_search_cmp(const void *keyval, const void *datum);
static rufl_code rufl_process_not_available(rufl_action action,
		unsigned short *s, unsigned int n,
		unsigned int font_size, int *x, int y);
static rufl_code rufl_place_in_cache(unsigned int font, unsigned int font_size,
		font_f f);


/**
 * Render Unicode text.
 */

rufl_code rufl_paint(const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int x, int y)
{
	return rufl_process(rufl_PAINT,
			font_family, font_style, font_size, string,
			length, x, y, 0);
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
			length, 0, 0, width);
}


/**
 * Render, measure, or split Unicode text.
 */

rufl_code rufl_process(rufl_action action,
		const char *font_family, rufl_style font_style,
		unsigned int font_size,
		const char *string, size_t length,
		int x, int y, int *width)
{
	unsigned short s[80];
	unsigned int font;
	unsigned int font0, font1;
	unsigned int n;
	unsigned int u;
	char **family;
	struct rufl_character_set *charset;
	rufl_code code;

	if (length == 0)
		return rufl_OK;

	family = bsearch(font_family, rufl_family_list,
			rufl_family_list_entries,
			sizeof rufl_family_list[0], rufl_family_list_cmp);
	if (!family)
		return rufl_FONT_NOT_FOUND;
	font = rufl_family_map[rufl_STYLES * (family - rufl_family_list) +
			font_style];
	charset = rufl_font_list[font].charset;

	rufl_utf8_read(string, length, u);
	if (rufl_character_set_test(charset, u))
		font1 = font;
	else
		font1 = rufl_substitution_lookup(u);
	do {
		s[0] = u;
		n = 1;
		font0 = font1;
		/* invariant: s[0..n) is in font font0 */
		while (0 < length && n < 70 && font1 == font0) {
			rufl_utf8_read(string, length, u);
			s[n] = u;
			if (rufl_character_set_test(charset, u))
				font1 = font;
			else
				font1 = rufl_substitution_lookup(u);
			if (font1 == font0)
				n++;
		}
		s[n] = 0;

		if (font0 == NOT_AVAILABLE)
			code = rufl_process_not_available(action, s, n,
					font_size, &x, y);
		else if (rufl_old_font_manager)
			code = rufl_process_span_old(action, s, n, font0,
					font_size, &x, y);
		else
			code = rufl_process_span(action, s, n, font0,
					font_size, &x, y);

		if (code != rufl_OK)
			return code;

	} while (!(length == 0 && font1 == font0));

	if (width)
		*width = x;

	return rufl_OK;
}


int rufl_family_list_cmp(const void *keyval, const void *datum)
{
	const char *key = keyval;
	const char * const *entry = datum;
	return strcmp(key, *entry);
}


/**
 * Render a string of characters from a single RISC OS font.
 */

rufl_code rufl_process_span(rufl_action action,
		unsigned short *s, unsigned int n,
		unsigned int font, unsigned int font_size, int *x, int y)
{
	char font_name[80];
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
		if (rufl_fm_error)
			return rufl_FONT_MANAGER_ERROR;
		/* place in cache */
		code = rufl_place_in_cache(font, font_size, f);
		if (code != rufl_OK)
			return code;
	}

	if (action == rufl_PAINT) {
		/* paint span */
		rufl_fm_error = xfont_paint(f, (const char *) s,
				font_OS_UNITS | font_GIVEN_LENGTH |
				font_GIVEN_FONT | font_KERN | font_GIVEN16_BIT,
				*x, y, 0, 0, n * 2);
		if (rufl_fm_error) {
			xfont_lose_font(f);
			return rufl_FONT_MANAGER_ERROR;
		}
	}

	/* increment x by width of span */
	rufl_fm_error = xfont_scan_string(f, (const char *) s,
			font_GIVEN_LENGTH | font_GIVEN_FONT | font_KERN |
			font_GIVEN16_BIT,
			0x7fffffff, 0x7fffffff, 0, 0, n * 2,
			0, &x_out, &y_out, 0);
	if (rufl_fm_error) {
		xfont_lose_font(f);
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
		unsigned int font, unsigned int font_size, int *x, int y)
{
	char s2[80];
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
				font_GIVEN_LENGTH | font_GIVEN_FONT | font_KERN,
				*x, y, 0, 0, n);
		if (rufl_fm_error) {
			xfont_lose_font(f);
			return rufl_FONT_MANAGER_ERROR;
		}
	}

	/* increment x by width of span */
	rufl_fm_error = xfont_scan_string(f, s2,
			font_GIVEN_LENGTH | font_GIVEN_FONT | font_KERN,
			0x7fffffff, 0x7fffffff, 0, 0, n,
			0, &x_out, &y_out, 0);
	if (rufl_fm_error) {
		xfont_lose_font(f);
		return rufl_FONT_MANAGER_ERROR;
	}
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
		unsigned int font_size, int *x, int y)
{
	char missing[] = "0000";
	unsigned int i;
	font_f f;
	rufl_code code;

	if (action == rufl_WIDTH) {
		*x += 7 * font_size / 64;
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
				font_GIVEN_LENGTH | font_GIVEN_FONT | font_KERN,
				*x, y + 5 * font_size / 64,
				0, 0, 2);
		if (rufl_fm_error)
			return rufl_FONT_MANAGER_ERROR;

		/* last two characters underneath */
		rufl_fm_error = xfont_paint(f, missing + 2, font_OS_UNITS |
				font_GIVEN_LENGTH | font_GIVEN_FONT | font_KERN,
				*x, y, 0, 0, 2);
		if (rufl_fm_error)
			return rufl_FONT_MANAGER_ERROR;

		*x += 7 * font_size / 64;
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
	rufl_fm_error = xfont_lose_font(rufl_cache[evict].f);
	if (rufl_fm_error)
		return rufl_FONT_MANAGER_ERROR;
	rufl_cache[evict].font = font;
	rufl_cache[evict].size = font_size;
	rufl_cache[evict].f = f;
	rufl_cache[evict].last_used = rufl_cache_time++;

	return rufl_OK;
}
