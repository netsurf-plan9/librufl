/*
 * This file is part of RUfl
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license
 * Copyright 2005 James Bursa <james@semichrome.net>
 */

#include <limits.h>
#include "oslib/font.h"
#include "rufl.h"


/** The available characters in a font. The range which can be represented is
 * 0x0000 to 0xffff. The size of the structure is 4 + 256 + 32 * blocks. A
 * typical * 200 glyph font might have characters in 10 blocks, giving 580
 * bytes. The maximum possible size of the structure is 8388 bytes. Note that
 * since two index values are reserved, fonts with 65280-65024 glyphs may be
 * unrepresentable, if there are no full blocks. This is unlikely. The primary
 * aim of this structure is to make lookup fast. */
struct rufl_character_set {
	/** Size of structure / bytes. */
	size_t size;

	/** Index table. Each entry represents a block of 256 characters, so
	 * i[k] refers to characters [256*k, 256*(k+1)). The value is either
	 * BLOCK_EMPTY, BLOCK_FULL, or an offset into the block table. */
	unsigned char index[256];
	/** The block has no characters present. */
#	define BLOCK_EMPTY 254
	/** All characters in the block are present. */
#	define BLOCK_FULL 255

	/** Block table. Each entry is a 256-bit bitmap indicating which
	 * characters in the block are present and absent. */
	unsigned char block[254][32];
};


/** Part of struct rufl_unicode_map. */
struct rufl_unicode_map_entry {
	/** Unicode value. */
	unsigned short u;
	/** Corresponding character. */
	unsigned char c;
};


/** Old font manager: mapping from Unicode to character code. This is simply
 * an array sorted by Unicode value, suitable for bsearch(). */
struct rufl_unicode_map {
	/** Number of valid entries in u and c. */
	unsigned int entries;
	/** Map from Unicode to character code. */
	struct rufl_unicode_map_entry map[256];
};


/** An entry in rufl_font_list. */
struct rufl_font_list_entry {
	/** Font identifier (name). */
	char *identifier;
	/** Character set of font. */
	struct rufl_character_set *charset;
	/** Mapping from Unicode to character code. */
	struct rufl_unicode_map *umap;
};
/** List of all available fonts. */
extern struct rufl_font_list_entry *rufl_font_list;
/** Number of entries in rufl_font_list. */
extern unsigned int rufl_font_list_entries;


#define rufl_STYLES 4

/** Map from font family to fonts. rufl_STYLES entries per family. */
extern unsigned int *rufl_family_map;


/** Map from characters to a font which includes them. A typical machine might
 * have characters from 30 blocks, giving 15616 bytes. */
struct rufl_substitution_table {
	/** Index table. Each entry represents a block of 256 characters, so
	 * i[k] refers to characters [256*k, 256*(k+1)). The value is either
	 * BLOCK_NONE_AVAILABLE or an offset into the block table. */
	unsigned char index[256];
	/** None of the characters in the block are available in any font. */
#	define BLOCK_NONE_AVAILABLE 255

	/** Block table. Each entry is a map from the characters in the block
	 * to a font number which includes it, or NOT_AVAILABLE. */
	unsigned short block[255][256];
	/** No font contains this character. */
#	define NOT_AVAILABLE 65535
};


/** Font substitution table. */
extern struct rufl_substitution_table *rufl_substitution_table;


/** Number of slots in recent-use cache. This is the maximum number of RISC OS
 * font handles that will be used at any time by the library. */
#define rufl_CACHE_SIZE 10

/** An entry in rufl_cache. */
struct rufl_cache_entry {
	/** Font number (index in rufl_font_list), or rufl_CACHE_*. */
	unsigned int font;
	/** No font cached in this slot. */
#define rufl_CACHE_NONE UINT_MAX
	/** Font for rendering hex substitutions in this slot. */
#define rufl_CACHE_CORPUS (UINT_MAX - 1)
	/** Font size. */
	unsigned int size;
	/** Value of rufl_cache_time when last used. */
	unsigned int last_used;
	/** RISC OS font handle. */
	font_f f;
};
/** Cache of rufl_CACHE_SIZE most recently used font handles. */
extern struct rufl_cache_entry rufl_cache[rufl_CACHE_SIZE];
/** Counter for measuring age of cache entries. */
extern int rufl_cache_time;

/** Font manager does not support Unicode. */
extern bool rufl_old_font_manager;


bool rufl_character_set_test(struct rufl_character_set *charset,
		unsigned int c);
unsigned int rufl_substitution_lookup(unsigned int c);


#define rufl_utf8_read(s, l, u)						       \
	if (4 <= l && ((s[0] & 0xf8) == 0xf0) && ((s[1] & 0xc0) == 0x80) &&    \
			((s[2] & 0xc0) == 0x80) && ((s[3] & 0xc0) == 0x80)) {  \
		u = ((s[0] & 0x7) << 18) | ((s[1] & 0x3f) << 12) |	       \
				((s[2] & 0x3f) << 6) | (s[3] & 0x3f);	       \
		s += 4; l -= 4;						       \
	} else if (3 <= l && ((s[0] & 0xf0) == 0xe0) &&			       \
			((s[1] & 0xc0) == 0x80) &&			       \
			((s[2] & 0xc0) == 0x80)) {			       \
		u = ((s[0] & 0xf) << 12) | ((s[1] & 0x3f) << 6) |	       \
				(s[2] & 0x3f);				       \
		s += 3; l -= 3;						       \
	} else if (2 <= l && ((s[0] & 0xe0) == 0xc0) &&			       \
			((s[1] & 0xc0) == 0x80)) {			       \
		u = ((s[0] & 0x3f) << 6) | (s[1] & 0x3f);		       \
		s += 2; l -= 2;						       \
	} else if ((s[0] & 0x80) == 0) {				       \
		u = s[0];						       \
		s++; l--;						       \
	} else {							       \
		u = 0xfffd;						       \
		s++; l--;						       \
	}

#define rufl_CACHE "<Wimp$ScrapDir>.RUfl_cache"
#define rufl_CACHE_VERSION 2


struct rufl_glyph_map_entry {
	const char *glyph_name;
	unsigned short u;
};

extern const struct rufl_glyph_map_entry rufl_glyph_map[];
extern const size_t rufl_glyph_map_size;
