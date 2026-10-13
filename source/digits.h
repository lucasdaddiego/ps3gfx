/* The display digits (digits.c): numbers in three big sizes of Inter, for the
 * values read from across a room (a clock, a temperature, a price). The
 * atlases are data/digits.bin (art/make_font.py --digits), an archive member
 * that only digits.c references: an app links both, and uses their 1.9 MB of
 * RSX memory, only when it calls digits_init. One quad per glyph, pixel exact
 * at 1080p as the text of ui.h.
 *
 * The glyphs: 0 to 9, tabular (one advance, so a number that changes does
 * not move), then : , . - + % and the degree sign, written as UTF-8 (C2 B0)
 * or as the Latin-1 byte B0. '-' draws the minus sign (the width of '+').
 * Any other byte draws nothing and has no width. The font licence is
 * Inter's: OFL.txt. */
#ifndef DIGITS_H
#define DIGITS_H

#include <ppu-types.h>

enum { D_M, D_L, D_XL, D_COUNT };            /* 84, 112 and 168 px (weights 600, 560 and 500) */

/* The atlases into RSX memory, after ui_init; again: nothing. 0, or -1 when
 * digits.bin is bad or RSX memory is short (then the calls below draw
 * nothing and measure 0). */
int digits_init(void);

/* y is the top of the line box, as in text(); the calls return the x after
 * the text (digits) or its start (digits_r). x and y snap to whole pixels. */
float digits(float x, float y, int size, u32 rgba, const char *s);
float digits_r(float xr, float y, int size, u32 rgba, const char *s);
int digits_w(int size, const char *s);
int digits_h(int size);                      /* the line height */

/* One glyph's top half (half 0) or bottom half (half 1), for a flip card:
 * the halves meet at the middle of the digits' ink, the middle line, and
 * scale_y (0 to 1) shrinks the half toward that line (0 draws nothing).
 * The quad's texture rows stop at the line, so no scissor is needed: at
 * scale 1 the two halves are the whole glyph of digits(). x is the pen
 * position, y the top of the line box; it returns the x after the glyph. */
float digits_half(float x, float y, int size, char ch, int half, float scale_y, u32 rgba);
int digits_mid(int size);                    /* the middle line, from the top of the line box */

#endif
