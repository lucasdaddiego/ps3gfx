/* Between ui.c and text_utf8.c, not installed: the Latin atlases that
 * text_utf8() loads, and the call that switches ui.c's text to UTF-8. */
#ifndef UI_TEXT_H
#define UI_TEXT_H

#include "gfx.h"

#define UI_LATIN_MAX 128                     /* glyphs of an atlas of latin.bin */

typedef struct {
    int cw, ch, base, pad, asc, line, aw, ah, cols;
    unsigned char adv[UI_LATIN_MAX];
    gfx_tex tex;
} ui_atlas;

/* The two A4R4G4B4 texels of each byte of 4-bit alphas, as one 32-bit word in
 * memory order (ui.c fills it in fonts_init). */
const u32 *ui_texel_pairs(void);

/* From text_utf8(): the five Latin atlases, and map, which gives a code point
 * from U+0080 its slot in them (0 and up), 0x100 + an ASCII character to draw
 * instead, or -1 for no glyph and no width. */
void ui_text_latin(const ui_atlas *latin, int (*map)(unsigned cp));

#endif
