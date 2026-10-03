#ifndef GL30_TYPE_H
#define GL30_TYPE_H
#include <stdint.h>
typedef enum { GL30_TYPE_BODY, GL30_TYPE_TITLE, GL30_TYPE_DIGITS,
               GL30_TYPE_SMALL, GL30_TYPE_WATCH } gl30_type_face;
typedef struct {
    uint32_t codepoint, offset;
    uint8_t width, height;
    int8_t left, top;
    uint8_t advance;
} gl30_type_glyph;
extern const uint8_t gl30_type_pixels[];
extern const gl30_type_glyph gl30_type_glyphs[];
extern const uint16_t gl30_type_starts[6];
int gl30_type_width(gl30_type_face face, const char *text);
void gl30_type_draw(gl30_type_face face, int x, int y, const char *text, uint16_t color);
#endif
