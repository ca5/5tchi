#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#define FONT_GLYPH_W   5
#define FONT_GLYPH_H   7
#define FONT_SPACING_X 1
#define FONT_ADVANCE_X (FONT_GLYPH_W + FONT_SPACING_X)

void font_draw_char(uint16_t *fb, int x, int y, char c, uint16_t color, uint8_t scale);
void font_draw_str(uint16_t *fb, int x, int y, const char *str, uint16_t color, uint8_t scale);
int  font_measure_str(const char *str, uint8_t scale);

#endif
