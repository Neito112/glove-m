#ifndef PERIPHERAL_FONT_H
#define PERIPHERAL_FONT_H

#include <stdint.h>

// A compact 5x7 font for the displays.
//
// Every glyph is five columns wide and seven rows high. Each column holds the
// eight pixels of one column, the lowest bit being the top row. The glyphs
// cover the printable ASCII range from space to tilde.

#define FONT_GLYPH_WIDTH 5
#define FONT_GLYPH_HEIGHT 7
#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR 126
#define FONT_GLYPH_COUNT (FONT_LAST_CHAR - FONT_FIRST_CHAR + 1)

// The names match the ones used in the generated font table.
#define PERIPHERAL_FONT_GLYPH_WIDTH FONT_GLYPH_WIDTH
#define PERIPHERAL_FONT_GLYPH_HEIGHT FONT_GLYPH_HEIGHT
#define PERIPHERAL_FONT_FIRST_CHAR FONT_FIRST_CHAR
#define PERIPHERAL_FONT_LAST_CHAR FONT_LAST_CHAR
#define PERIPHERAL_FONT_GLYPH_COUNT FONT_GLYPH_COUNT

// The five column bytes of one glyph.
const uint8_t *font_get_glyph(char c);

// The number of characters that fit on a display of the given width.
int font_chars_per_line(int display_width);

#endif // PERIPHERAL_FONT_H
