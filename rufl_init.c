/*
 * This file is part of RUfl
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license
 * Copyright 2005 James Bursa <james@semichrome.net>
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "oslib/font.h"
#include "oslib/hourglass.h"
#include "rufl_internal.h"


struct rufl_font_list_entry *rufl_font_list = 0;
unsigned int rufl_font_list_entries = 0;
char **rufl_family_list = 0;
unsigned int rufl_family_list_entries = 0;
unsigned int *rufl_family_map = 0;
os_error *rufl_fm_error = 0;
struct rufl_substitution_table *rufl_substitution_table = 0;
struct rufl_cache_entry rufl_cache[rufl_CACHE_SIZE];
int rufl_cache_time = 0;


struct rufl_style_table_entry {
	const char *name;
	unsigned int style;
};
struct rufl_style_table_entry rufl_style_table[] = {
	{ "Bold", rufl_BOLD },
	{ "Bold.Italic", rufl_BOLD_SLANTED },
	{ "Bold.Oblique", rufl_BOLD_SLANTED },
	{ "Italic", rufl_SLANTED },
	{ "Medium", rufl_REGULAR },
	{ "Medium.Italic", rufl_SLANTED },
	{ "Medium.Oblique", rufl_SLANTED },
	{ "Oblique", rufl_SLANTED },
	{ "Regular", rufl_REGULAR },
	{ "Regular.Italic", rufl_SLANTED },
	{ "Regular.Oblique", rufl_SLANTED },
};


static rufl_code rufl_init_font_list(void);
static int rufl_style_table_cmp(const void *keyval, const void *datum);
static rufl_code rufl_init_scan_font(unsigned int font);
static rufl_code rufl_init_substitution_table(void);
static rufl_code rufl_save_cache(void);
static rufl_code rufl_load_cache(void);
static int rufl_font_list_cmp(const void *keyval, const void *datum);


/**
 * Initialise RUfl.
 *
 * All available fonts are scanned. May take some time.
 */

rufl_code rufl_init(void)
{
	bool changes = false;
	unsigned int i;
	rufl_code code;

	if (rufl_font_list_entries)
		/* already initialized */
		return rufl_OK;

	xhourglass_on();

	code = rufl_init_font_list();
	if (code != rufl_OK) {
		rufl_quit();
		xhourglass_off();
		return code;
	}

	code = rufl_load_cache();
	if (code != rufl_OK) {
		rufl_quit();
		xhourglass_off();
		return code;
	}

	xhourglass_leds(1, 0, 0);
	for (i = 0; i != rufl_font_list_entries; i++) {
		if (rufl_font_list[i].charset)
			/* character set loaded from cache */
			continue;
		xhourglass_percentage(100 * i / rufl_font_list_entries);
		code = rufl_init_scan_font(i);
		if (code != rufl_OK) {
			rufl_quit();
			xhourglass_off();
			return code;
		}
		changes = true;
	}

	xhourglass_leds(2, 0, 0);
	code = rufl_init_substitution_table();
	if (code != rufl_OK) {
		rufl_quit();
		xhourglass_off();
		return code;
	}

	if (changes) {
		xhourglass_leds(3, 0, 0);
		code = rufl_save_cache();
		if (code != rufl_OK) {
			rufl_quit();
			xhourglass_off();
			return code;
		}
	}

	for (i = 0; i != rufl_CACHE_SIZE; i++)
		rufl_cache[i].font = rufl_CACHE_NONE;

	xhourglass_off();

	return rufl_OK;
}


/**
 * Build list of font in rufl_font_list and list of font families
 * in rufl_family_list.
 */

rufl_code rufl_init_font_list(void)
{
	int size;
	struct rufl_font_list_entry *font_list;
	char *identifier;
	char *dot;
	char **family_list;
	char *family;
	unsigned int *family_map;
	unsigned int i;
	font_list_context context = 0;
	font_list_context context2;
	struct rufl_style_table_entry *entry;

	while (context != -1) {
		/* find length of next identifier */
		rufl_fm_error = xfont_list_fonts(0,
				font_RETURN_FONT_NAME | context,
				0, 0, 0, 0,
				&context2, &size, 0);
		if (rufl_fm_error)
			return rufl_FONT_MANAGER_ERROR;
		if (context2 == -1)
			break;

		/* (re)allocate buffers */
		font_list = realloc(rufl_font_list, sizeof rufl_font_list[0] *
				(rufl_font_list_entries + 1));
		if (!font_list)
			return rufl_OUT_OF_MEMORY;
		rufl_font_list = font_list;

		identifier = malloc(size);
		if (!identifier)
			return rufl_OUT_OF_MEMORY;

		rufl_font_list[rufl_font_list_entries].identifier = identifier;
		rufl_font_list[rufl_font_list_entries].charset = 0;
		rufl_font_list_entries++;

		/* read identifier */
		rufl_fm_error = xfont_list_fonts(identifier,
				font_RETURN_FONT_NAME | context,
				size, 0, 0, 0,
				&context, 0, 0);
		if (rufl_fm_error)
			return rufl_FONT_MANAGER_ERROR;

		/* add family to list, if it is new */
		dot = strchr(identifier, '.');
		if (2 <= rufl_font_list_entries && dot &&
				strncmp(identifier, rufl_font_list
				[rufl_font_list_entries - 2].identifier,
				dot - identifier) == 0) {
			/* same family as last font */
			entry = bsearch(dot + 1, rufl_style_table,
					sizeof rufl_style_table /
					sizeof rufl_style_table[0],
					sizeof rufl_style_table[0],
					rufl_style_table_cmp);
			if (entry)
				rufl_family_map[rufl_STYLES *
						(rufl_family_list_entries - 1) +
						entry->style] =
						rufl_font_list_entries - 1;
			continue;
		}

		/* new family */
		family_list = realloc(rufl_family_list,
				sizeof rufl_family_list[0] *
				(rufl_family_list_entries + 1));
		if (!family_list)
			return rufl_OUT_OF_MEMORY;
		rufl_family_list = family_list;

		family_map = realloc(rufl_family_map,
				rufl_STYLES * sizeof rufl_family_map[0] *
				(rufl_family_list_entries + 1));
		if (!family_map)
			return rufl_OUT_OF_MEMORY;
		rufl_family_map = family_map;

		if (dot)
			family = strndup(identifier, dot - identifier);
		else
			family = strdup(identifier);
		if (!family)
			return rufl_OUT_OF_MEMORY;

		rufl_family_list[rufl_family_list_entries] = family;
		for (i = 0; i != rufl_STYLES; i++)
			rufl_family_map[rufl_STYLES * rufl_family_list_entries +
					i] = rufl_font_list_entries - 1;
		rufl_family_list_entries++;
	}

	return rufl_OK;
}


int rufl_style_table_cmp(const void *keyval, const void *datum)
{
	const char *key = keyval;
	const struct rufl_style_table_entry *entry = datum;
	return strcmp(key, entry->name);
}


/**
 * Scan a font for available characters.
 */

rufl_code rufl_init_scan_font(unsigned int font_index)
{
	char font_name[80];
	int x_out, y_out;
	unsigned int byte, bit;
	unsigned int block_count = 0;
	unsigned int last_used = 0;
	unsigned int string[2] = { 0, 0 };
	unsigned int u;
	struct rufl_character_set *charset;
	struct rufl_character_set *charset2;
	font_f font;
	font_scan_block block = { { 0, 0 }, { 0, 0 }, -1, { 0, 0, 0, 0 } };

	charset = calloc(1, sizeof *charset);
	if (!charset)
		return rufl_OUT_OF_MEMORY;

	snprintf(font_name, sizeof font_name, "%s\\EUTF8",
			rufl_font_list[font_index].identifier);

	rufl_fm_error = xfont_find_font(font_name, 160, 160, 0, 0, &font, 0, 0);
	if (rufl_fm_error) {
		free(charset);
		return rufl_FONT_MANAGER_ERROR;
	}

	/* scan through all characters */
	for (u = 32; u != 0x10000; u++) {
		string[0] = u;
		rufl_fm_error = xfont_scan_string(font, (char *) string,
				font_RETURN_BBOX | font_GIVEN32_BIT |
				font_GIVEN_FONT | font_GIVEN_LENGTH |
				font_GIVEN_BLOCK,
				0x7fffffff, 0x7fffffff,
				&block, 0, 4,
				0, &x_out, &y_out, 0);
		if (rufl_fm_error)
			break;

		if (block.bbox.x0 == 0x20000000 ||
				(x_out == 0 && y_out == 0 &&
				block.bbox.x0 == 0 && block.bbox.y0 == 0 &&
				block.bbox.x1 == 0 && block.bbox.y1 == 0)) {
			/* absent */
		} else {
			/* present */
			byte = (u >> 3) & 31;
			bit = u & 7;
			charset->block[last_used][byte] |= 1 << bit;

			block_count++;
		}

		if ((u + 1) % 256 == 0) {
			/* end of block */
			if (block_count == 0)
				charset->index[u >> 8] = BLOCK_EMPTY;
			else if (block_count == 256) {
				charset->index[u >> 8] = BLOCK_FULL;
				for (byte = 0; byte != 32; byte++)
					charset->block[last_used][byte] = 0;
			} else {
				charset->index[u >> 8] = last_used;
				last_used++;
				if (last_used == 254)
					/* too many characters */
					break;
			}
			block_count = 0;
		}
	}

	xfont_lose_font(font);

	if (rufl_fm_error) {
		free(charset);
		return rufl_FONT_MANAGER_ERROR;
	}

	/* shrink-wrap */
	charset->size = offsetof(struct rufl_character_set, block) +
			32 * last_used;
	charset2 = realloc(charset, charset->size);
	if (!charset2) {
		free(charset);
		return rufl_OUT_OF_MEMORY;
	}

	rufl_font_list[font_index].charset = charset;

	return rufl_OK;
}


/**
 * Construct the font substitution table.
 */

rufl_code rufl_init_substitution_table(void)
{
	unsigned int block_count = 0;
	unsigned int i;
	unsigned int last_used = 0;
	unsigned int u;
	struct rufl_substitution_table *substitution_table2;

	rufl_substitution_table = malloc(sizeof *rufl_substitution_table);
	if (!rufl_substitution_table)
		return rufl_OUT_OF_MEMORY;

	/* scan through all characters */
	for (u = 0; u != 0x10000; u++) {
		rufl_substitution_table->block[last_used][u & 255] =
				NOT_AVAILABLE;
		for (i = 0; i != rufl_font_list_entries; i++) {
			if (rufl_character_set_test(rufl_font_list[i].charset,
					u)) {
				rufl_substitution_table->block[last_used]
						[u & 255] = i;
				block_count++;
				break;
			}
		}

		if ((u + 1) % 256 == 0) {
			/* end of block */
			if (block_count == 0) {
				rufl_substitution_table->index[u >> 8] =
						BLOCK_NONE_AVAILABLE;
			} else {
				rufl_substitution_table->index[u >> 8] =
						last_used;
				last_used++;
				if (last_used == 255)
					/* too many characters */
					break;
			}
			block_count = 0;
		}
	}

	/* shrink-wrap */
	substitution_table2 = realloc(rufl_substitution_table,
			offsetof(struct rufl_substitution_table, block) +
			sizeof (short) * 256 * last_used);
	if (!substitution_table2)
		return rufl_OUT_OF_MEMORY;
	rufl_substitution_table = substitution_table2;

	return rufl_OK;
}


/**
 * Save character sets to cache.
 */

rufl_code rufl_save_cache(void)
{
	unsigned int i;
	size_t len;
	FILE *fp;

	fp = fopen(rufl_CACHE, "wb");
	if (!fp)
		return rufl_IO_ERROR;

	for (i = 0; i != rufl_font_list_entries; i++) {
		/* length of font identifier */
		len = strlen(rufl_font_list[i].identifier);
		if (fwrite(&len, sizeof len, 1, fp) != 1) {
			fclose(fp);
			return rufl_IO_ERROR;
		}

		/* font identifier */
		if (fwrite(rufl_font_list[i].identifier, len, 1, fp) != 1) {
			fclose(fp);
			return rufl_IO_ERROR;
		}

		/* character set */
		if (fwrite(rufl_font_list[i].charset,
				rufl_font_list[i].charset->size, 1, fp) != 1) {
			fclose(fp);
			return rufl_IO_ERROR;
		}
	}

	if (fclose(fp) == EOF)
		return rufl_IO_ERROR;

	return rufl_OK;
}


/**
 * Load character sets from cache.
 */

rufl_code rufl_load_cache(void)
{
	bool eof;
	char *identifier;
	size_t len, size;
	FILE *fp;
	struct rufl_font_list_entry *entry;
	struct rufl_character_set *charset;

	fp = fopen(rufl_CACHE, "rb");
	if (!fp) {
		if (errno == ENOENT)
			return rufl_OK;
		else
			return rufl_IO_ERROR;
	}

	while (!feof(fp)) {
		/* length of font identifier */
		if (fread(&len, sizeof len, 1, fp) != 1) {
			if (feof(fp))
				break;
			fclose(fp);
			return rufl_IO_ERROR;
		}

		identifier = malloc(len + 1);
		if (!identifier) {
			fclose(fp);
			return rufl_OUT_OF_MEMORY;
		}

		/* font identifier */
		if (fread(identifier, len, 1, fp) != 1) {
			eof = feof(fp);
			free(identifier);
			fclose(fp);
			return eof ? rufl_IO_EOF : rufl_IO_ERROR;
		}
		identifier[len] = 0;

		/* character set */
		if (fread(&size, sizeof size, 1, fp) != 1) {
			eof = feof(fp);
			free(identifier);
			fclose(fp);
			return eof ? rufl_IO_EOF : rufl_IO_ERROR;
		}

		charset = malloc(size);
		if (!charset) {
			free(identifier);
			fclose(fp);
			return rufl_OUT_OF_MEMORY;
		}

		charset->size = size;
		if (fread(charset->index, size - sizeof size, 1, fp) != 1) {
			eof = feof(fp);
			free(charset);
			free(identifier);
			fclose(fp);
			return eof ? rufl_IO_EOF : rufl_IO_ERROR;
		}

		/* put in rufl_font_list */
		entry = bsearch(identifier, rufl_font_list,
				rufl_font_list_entries,
				sizeof rufl_font_list[0], rufl_font_list_cmp);
		if (entry)
			entry->charset = charset;
		else
			free(charset);

                free(identifier);
	}

	if (fclose(fp) == EOF)
		return rufl_IO_ERROR;

	return rufl_OK;
}


int rufl_font_list_cmp(const void *keyval, const void *datum)
{
	const char *key = keyval;
	const struct rufl_font_list_entry *entry = datum;
	return strcmp(key, entry->identifier);
}
