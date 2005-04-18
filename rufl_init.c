/*
 * This file is part of RUfl
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license
 * Copyright 2005 James Bursa <james@semichrome.net>
 */

#include <assert.h>
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
unsigned short *rufl_substitution_table;
struct rufl_cache_entry rufl_cache[rufl_CACHE_SIZE];
int rufl_cache_time = 0;
bool rufl_old_font_manager = false;


/** An entry in rufl_style_table. */
struct rufl_style_table_entry {
	const char *name;
	unsigned int style;
};

/** Map from font name tail to font style. Must be sorted by name. */
const struct rufl_style_table_entry rufl_style_table[] = {
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
static rufl_code rufl_init_scan_font_old(unsigned int font_index);
static rufl_code rufl_init_read_encoding(font_f font,
		struct rufl_unicode_map *umap);
static int rufl_glyph_map_cmp(const void *keyval, const void *datum);
static int rufl_unicode_map_cmp(const void *z1, const void *z2);
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
	unsigned int changes = 0;
	unsigned int i;
	int fm_version;
	rufl_code code;
	font_f font;
	os_colour old_sand, old_glass;

	if (rufl_font_list_entries)
		/* already initialized */
		return rufl_OK;

	xhourglass_on();

	/* determine if the font manager support Unicode */
	rufl_fm_error = xfont_find_font("Homerton.Medium\\EUTF8", 160, 160,
			0, 0, &font, 0, 0);
	if (rufl_fm_error) {
		if (rufl_fm_error->errnum == error_FONT_ENCODING_NOT_FOUND) {
			rufl_old_font_manager = true;
		} else {
			LOG("xfont_find_font: 0x%x: %s",
					rufl_fm_error->errnum,
					rufl_fm_error->errmess);
			rufl_quit();
			xhourglass_off();
			return rufl_FONT_MANAGER_ERROR;
		}
	}
	LOG("%s font manager", rufl_old_font_manager ? "old" : "new");

	/* test if the font manager supports background blending */
	rufl_fm_error = xfont_cache_addr(&fm_version, 0, 0);
	if (rufl_fm_error)
		return rufl_FONT_MANAGER_ERROR;
	if (fm_version >= 335)
		rufl_can_background_blend = true;

	code = rufl_init_font_list();
	if (code != rufl_OK) {
		rufl_quit();
		xhourglass_off();
		return code;
	}
	LOG("%u faces, %u families", rufl_font_list_entries,
			rufl_family_list_entries);

	code = rufl_load_cache();
	if (code != rufl_OK) {
		LOG("rufl_load_cache: 0x%x", code);
		rufl_quit();
		xhourglass_off();
		return code;
	}

	xhourglass_leds(1, 0, 0);
	for (i = 0; i != rufl_font_list_entries; i++) {
		if (rufl_font_list[i].charset) {
			/* character set loaded from cache */
			continue;
		}
		LOG("scanning %u \"%s\"", i, rufl_font_list[i].identifier);
		xhourglass_percentage(100 * i / rufl_font_list_entries);
		if (rufl_old_font_manager)
			code = rufl_init_scan_font_old(i);
		else
			code = rufl_init_scan_font(i);
		if (code != rufl_OK) {
			LOG("rufl_init_scan_font: 0x%x", code);
			rufl_quit();
			xhourglass_off();
			return code;
		}
		changes++;
	}

	xhourglass_leds(2, 0, 0);
	xhourglass_colours(0x0000ff, 0x00ffff, &old_sand, &old_glass);
	code = rufl_init_substitution_table();
	if (code != rufl_OK) {
		LOG("rufl_init_substitution_table: 0x%x", code);
		rufl_quit();
		xhourglass_off();
		return code;
	}
	xhourglass_colours(old_sand, old_glass, 0, 0);

	if (changes) {
		LOG("%u new charsets", changes);
		xhourglass_leds(3, 0, 0);
		code = rufl_save_cache();
		if (code != rufl_OK) {
			LOG("rufl_save_cache: 0x%x", code);
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
		if (rufl_fm_error) {
			LOG("xfont_list_fonts: 0x%x: %s",
					rufl_fm_error->errnum,
					rufl_fm_error->errmess);
			return rufl_FONT_MANAGER_ERROR;
		}
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
		rufl_font_list[rufl_font_list_entries].umap = 0;
		rufl_font_list_entries++;

		/* read identifier */
		rufl_fm_error = xfont_list_fonts(identifier,
				font_RETURN_FONT_NAME | context,
				size, 0, 0, 0,
				&context, 0, 0);
		if (rufl_fm_error) {
			LOG("xfont_list_fonts: 0x%x: %s",
					rufl_fm_error->errnum,
					rufl_fm_error->errmess);
			return rufl_FONT_MANAGER_ERROR;
		}
		/*LOG("%u \"%s\"", rufl_font_list_entries - 1, identifier);*/

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
		/*LOG("new family %u", rufl_family_list_entries);*/
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

	/*LOG("font %u \"%s\"", font_index,
			rufl_font_list[font_index].identifier);*/

	charset = calloc(1, sizeof *charset);
	if (!charset)
		return rufl_OUT_OF_MEMORY;

	snprintf(font_name, sizeof font_name, "%s\\EUTF8",
			rufl_font_list[font_index].identifier);

	rufl_fm_error = xfont_find_font(font_name, 160, 160, 0, 0, &font, 0, 0);
	if (rufl_fm_error) {
		LOG("xfont_find_font(\"%s\"): 0x%x: %s", font_name,
				rufl_fm_error->errnum, rufl_fm_error->errmess);
		free(charset);
		return rufl_OK;
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
		LOG("xfont_scan_string: 0x%x: %s",
				rufl_fm_error->errnum, rufl_fm_error->errmess);
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
 * Scan a font for available characters (old font manager version).
 */

rufl_code rufl_init_scan_font_old(unsigned int font_index)
{
	const char *font_name = rufl_font_list[font_index].identifier;
	char string[2] = { 0, 0 };
	int x_out, y_out;
	unsigned int byte, bit;
	unsigned int i;
	unsigned int last_used = 0;
	unsigned int u;
	struct rufl_character_set *charset;
	struct rufl_character_set *charset2;
	struct rufl_unicode_map *umap;
	rufl_code code;
	font_f font;
	font_scan_block block = { { 0, 0 }, { 0, 0 }, -1, { 0, 0, 0, 0 } };

	/*LOG("font %u \"%s\"", font_index, font_name);*/

	charset = calloc(1, sizeof *charset);
	if (!charset)
		return rufl_OUT_OF_MEMORY;
	for (i = 0; i != 256; i++)
		charset->index[i] = BLOCK_EMPTY;

	umap = calloc(1, sizeof *umap);
	if (!umap) {
		free(charset);
		return rufl_OUT_OF_MEMORY;
	}

	rufl_fm_error = xfont_find_font(font_name, 160, 160, 0, 0, &font, 0, 0);
	if (rufl_fm_error) {
		LOG("xfont_find_font(\"%s\"): 0x%x: %s", font_name,
				rufl_fm_error->errnum, rufl_fm_error->errmess);
		free(umap);
		free(charset);
		return rufl_OK;
	}

	code = rufl_init_read_encoding(font, umap);
	if (code != rufl_OK) {
		free(umap);
		free(charset);
		return code;
	}

	for (i = 0; i != umap->entries; i++) {
		u = umap->map[i].u;
		string[0] = umap->map[i].c;
		rufl_fm_error = xfont_scan_string(font, (char *) string,
				font_RETURN_BBOX | font_GIVEN_FONT |
				font_GIVEN_LENGTH | font_GIVEN_BLOCK,
				0x7fffffff, 0x7fffffff,
				&block, 0, 1,
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
			if (charset->index[u >> 8] == BLOCK_EMPTY) {
				charset->index[u >> 8] = last_used;
				last_used++;
				if (last_used == 254)
					/* too many characters */
					break;
			}

			byte = (u >> 3) & 31;
			bit = u & 7;
			charset->block[last_used - 1][byte] |= 1 << bit;
		}
	}

	xfont_lose_font(font);

	if (rufl_fm_error) {
		free(umap);
		free(charset);
		LOG("xfont_scan_string: 0x%x: %s",
				rufl_fm_error->errnum, rufl_fm_error->errmess);
		return rufl_FONT_MANAGER_ERROR;
	}

	/* shrink-wrap */
	charset->size = offsetof(struct rufl_character_set, block) +
			32 * last_used;
	charset2 = realloc(charset, charset->size);
	if (!charset2) {
		free(umap);
		free(charset);
		return rufl_OUT_OF_MEMORY;
	}

	rufl_font_list[font_index].charset = charset;
	rufl_font_list[font_index].umap = umap;

	return rufl_OK;
}


/**
 * Parse an encoding file and fill in a rufl_unicode_map.
 */

rufl_code rufl_init_read_encoding(font_f font,
		struct rufl_unicode_map *umap)
{
	unsigned int u = 0;
	unsigned int i = 0;
	int c;
	int n;
	char filename[200];
	char s[200];
	struct rufl_glyph_map_entry *entry;
	FILE *fp;

	rufl_fm_error = xfont_read_encoding_filename(font, filename,
			sizeof filename, 0);
	if (rufl_fm_error) {
		LOG("xfont_read_encoding_filename: 0x%x: %s",
				rufl_fm_error->errnum, rufl_fm_error->errmess);
		return rufl_FONT_MANAGER_ERROR;
	}

	fp = fopen(filename, "r");
	if (!fp && errno == ENOENT)
		/* many "symbol" fonts have no encoding file: assume Latin 1 */
		fp = fopen("Resources:$.Fonts.Encodings.Latin1", "r");
	if (!fp)
		return rufl_IO_ERROR;

	while (!feof(fp) && u != 256) {
		c = fgetc(fp);
		if (c == '%') {
			/* comment line */
			fgets(s, sizeof s, fp);
		} else if (c == '/') {
			/* character definition */
			if (i++ < 32)
				continue;
			n = fscanf(fp, "%100s", s);
			if (n != 1)
				break;
			entry = bsearch(s, rufl_glyph_map,
					rufl_glyph_map_size,
					sizeof rufl_glyph_map[0],
					rufl_glyph_map_cmp);
			if (entry) {
				/* may be more than one unicode for the glyph
				 * sentinels stop overshooting array */
				while (strcmp(s, (entry - 1)->glyph_name) == 0)
					entry--;
				for (; strcmp(s, entry->glyph_name) == 0;
						entry++) {
					umap->map[u].u = entry->u;
					umap->map[u].c = i - 1;
					u++;
					if (u == 256)
						break;
				}
			}
		}
	}

	if (fclose(fp) == EOF)
		return rufl_IO_ERROR;

	/* sort by unicode */
	qsort(umap->map, u, sizeof umap->map[0], rufl_unicode_map_cmp);
	umap->entries = u;

	return rufl_OK;
}


int rufl_glyph_map_cmp(const void *keyval, const void *datum)
{
	const char *key = keyval;
	const struct rufl_glyph_map_entry *entry = datum;
	return strcmp(key, entry->glyph_name);
}


int rufl_unicode_map_cmp(const void *z1, const void *z2)
{
	const struct rufl_unicode_map_entry *entry1 = z1;
	const struct rufl_unicode_map_entry *entry2 = z2;
	if (entry1->u < entry2->u)
		return -1;
	else if (entry2->u < entry1->u)
		return 1;
	return 0;
}


/**
 * Construct the font substitution table.
 */

rufl_code rufl_init_substitution_table(void)
{
	unsigned char z;
	unsigned int i;
	unsigned int block, byte, bit;
	unsigned int u;
	unsigned int index;
	const struct rufl_character_set *charset;

	rufl_substitution_table = malloc(65536 *
			sizeof rufl_substitution_table[0]);
	if (!rufl_substitution_table) {
		LOG("malloc(%u) failed", 65536 *
				sizeof rufl_substitution_table[0]);
		return rufl_OUT_OF_MEMORY;
	}

	for (u = 0; u != 0x10000; u++)
		rufl_substitution_table[u] = NOT_AVAILABLE;

	for (i = 0; i != rufl_font_list_entries; i++) {
		charset = rufl_font_list[i].charset;
		if (!charset)
			continue;
		for (block = 0; block != 256; block++) {
			if (charset->index[block] == BLOCK_EMPTY)
				continue;
			if (charset->index[block] == BLOCK_FULL) {
				for (u = block << 8; u != (block << 8) + 256;
						u++) {
					if (rufl_substitution_table[u] ==
							NOT_AVAILABLE)
						rufl_substitution_table[u] = i;
				}
				continue;
			}
			index = charset->index[block];
			for (byte = 0; byte != 32; byte++) {
				z = charset->block[index][byte];
				if (z == 0)
					continue;
				u = (block << 8) | (byte << 3);
				for (bit = 0; bit != 8; bit++, u++) {
					if (rufl_substitution_table[u] ==
							NOT_AVAILABLE &&
							z & (1 << bit))
						rufl_substitution_table[u] = i;
				}
			}
		}
	}

	return rufl_OK;
}


/**
 * Save character sets to cache.
 */

rufl_code rufl_save_cache(void)
{
	unsigned int i;
	const unsigned int version = rufl_CACHE_VERSION;
	size_t len;
	FILE *fp;

	fp = fopen(rufl_CACHE, "wb");
	if (!fp) {
		LOG("fopen: 0x%x: %s", errno, strerror(errno));
		return rufl_OK;
	}

	/* cache format version */
	if (fwrite(&version, sizeof version, 1, fp) != 1) {
		LOG("fwrite: 0x%x: %s", errno, strerror(errno));
		fclose(fp);
		return rufl_OK;
	}

	/* font manager type flag */
	if (fwrite(&rufl_old_font_manager, sizeof rufl_old_font_manager, 1,
			fp) != 1) {
		LOG("fwrite: 0x%x: %s", errno, strerror(errno));
		fclose(fp);
		return rufl_OK;
	}

	for (i = 0; i != rufl_font_list_entries; i++) {
		if (!rufl_font_list[i].charset)
			continue;

		/* length of font identifier */
		len = strlen(rufl_font_list[i].identifier);
		if (fwrite(&len, sizeof len, 1, fp) != 1) {
			LOG("fwrite: 0x%x: %s", errno, strerror(errno));
			fclose(fp);
			return rufl_OK;
		}

		/* font identifier */
		if (fwrite(rufl_font_list[i].identifier, len, 1, fp) != 1) {
			LOG("fwrite: 0x%x: %s", errno, strerror(errno));
			fclose(fp);
			return rufl_OK;
		}

		/* character set */
		if (fwrite(rufl_font_list[i].charset,
				rufl_font_list[i].charset->size, 1, fp) != 1) {
			LOG("fwrite: 0x%x: %s", errno, strerror(errno));
			fclose(fp);
			return rufl_OK;
		}

		/* unicode map */
		if (rufl_old_font_manager) {
			if (fwrite(rufl_font_list[i].umap,
					sizeof *rufl_font_list[i].umap, 1,
					fp) != 1) {
				LOG("fwrite: 0x%x: %s", errno, strerror(errno));
				fclose(fp);
				return rufl_OK;
			}
		}
	}

	if (fclose(fp) == EOF) {
		LOG("fclose: 0x%x: %s", errno, strerror(errno));
		return rufl_OK;
	}

	LOG("%u charsets saved", i);

	return rufl_OK;
}


/**
 * Load character sets from cache.
 */

rufl_code rufl_load_cache(void)
{
	unsigned int version;
	unsigned int i = 0;
	bool old_font_manager;
	char *identifier;
	size_t len, size;
	FILE *fp;
	struct rufl_font_list_entry *entry;
	struct rufl_character_set *charset;
	struct rufl_unicode_map *umap = 0;

	fp = fopen(rufl_CACHE, "rb");
	if (!fp) {
		LOG("fopen: 0x%x: %s", errno, strerror(errno));
		return rufl_OK;
	}

	/* cache format version */
	if (fread(&version, sizeof version, 1, fp) != 1) {
		if (feof(fp))
			LOG("fread: %s", "unexpected eof");
		else
			LOG("fread: 0x%x: %s", errno, strerror(errno));
		fclose(fp);
		return rufl_OK;
	}
	if (version != rufl_CACHE_VERSION) {
		/* incompatible cache format */
		LOG("cache version %u (now %u)", version, rufl_CACHE_VERSION);
		fclose(fp);
		return rufl_OK;
	}

	/* font manager type flag */
	if (fread(&old_font_manager, sizeof old_font_manager, 1, fp) != 1) {
		if (feof(fp))
			LOG("fread: %s", "unexpected eof");
		else
			LOG("fread: 0x%x: %s", errno, strerror(errno));
		fclose(fp);
		return rufl_OK;
	}
	if (old_font_manager != rufl_old_font_manager) {
		/* font manager type has changed */
		LOG("font manager %u (now %u)", old_font_manager,
				rufl_old_font_manager);
		fclose(fp);
		return rufl_OK;
	}

	while (!feof(fp)) {
		/* length of font identifier */
		if (fread(&len, sizeof len, 1, fp) != 1) {
			/* eof at this point simply means that the whole cache
			 * file has been loaded */
			if (!feof(fp))
				LOG("fread: 0x%x: %s", errno, strerror(errno));
			break;
		}

		identifier = malloc(len + 1);
		if (!identifier) {
			LOG("malloc(%u) failed", len + 1);
			fclose(fp);
			return rufl_OUT_OF_MEMORY;
		}

		/* font identifier */
		if (fread(identifier, len, 1, fp) != 1) {
			if (feof(fp))
				LOG("fread: %s", "unexpected eof");
			else
				LOG("fread: 0x%x: %s", errno, strerror(errno));
			free(identifier);
			break;
		}
		identifier[len] = 0;

		/* character set */
		if (fread(&size, sizeof size, 1, fp) != 1) {
			if (feof(fp))
				LOG("fread: %s", "unexpected eof");
			else
				LOG("fread: 0x%x: %s", errno, strerror(errno));
			free(identifier);
			break;
		}

		charset = malloc(size);
		if (!charset) {
			LOG("malloc(%u) failed", size);
			free(identifier);
			fclose(fp);
			return rufl_OUT_OF_MEMORY;
		}

		charset->size = size;
		if (fread(charset->index, size - sizeof size, 1, fp) != 1) {
			if (feof(fp))
				LOG("fread: %s", "unexpected eof");
			else
				LOG("fread: 0x%x: %s", errno, strerror(errno));
			free(charset);
			free(identifier);
			break;
		}

		/* unicode map */
		if (rufl_old_font_manager) {
			umap = malloc(sizeof *umap);
			if (!umap) {
				LOG("malloc(%u) failed", sizeof *umap);
				free(charset);
				free(identifier);
				fclose(fp);
				return rufl_OUT_OF_MEMORY;
			}

			if (fread(umap, sizeof *umap, 1, fp) != 1) {
				if (feof(fp))
					LOG("fread: %s", "unexpected eof");
				else
					LOG("fread: 0x%x: %s", errno,
							strerror(errno));
				free(umap);
				free(charset);
				free(identifier);
				break;
			}
		}

		/* put in rufl_font_list */
		entry = bsearch(identifier, rufl_font_list,
				rufl_font_list_entries,
				sizeof rufl_font_list[0], rufl_font_list_cmp);
		if (entry) {
			entry->charset = charset;
			entry->umap = umap;
	                i++;
		} else {
			LOG("\"%s\" not in font list", identifier);
			free(umap);
			free(charset);
		}

                free(identifier);
	}
	fclose(fp);

	LOG("%u charsets loaded", i);

	return rufl_OK;
}


int rufl_font_list_cmp(const void *keyval, const void *datum)
{
	const char *key = keyval;
	const struct rufl_font_list_entry *entry = datum;
	return strcasecmp(key, entry->identifier);
}
