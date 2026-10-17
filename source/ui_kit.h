/* Between ui.c and the kit's other files (text_utf8.c, digits.c, icons.c,
 * fx.c), not installed: the tables that ui_init makes once for all of them,
 * floor_px and quad_next. */
#ifndef UI_KIT_H
#define UI_KIT_H

#include <math.h>
#include "gfx.h"

/* The two A4R4G4B4 texels of each byte of 4-bit alphas (the left pixel in
 * the high nibble), white also at alpha 0, as one 32-bit word in memory
 * order (ui.c fills it in fonts_init). */
const u32 *ui_texel_pairs(void);

/* round_pts' points on the unit circle for seg 4 (20 points) or 8 (36), in
 * its order (ui_init makes them): fx.c's tex_round and aa_corners. */
void ui_round_unit(int seg, const float **c, const float **s);

/* floorf in float adds: -mcpu=cell has no frim, so floorf is a library call.
 * Under 2^23, adding and taking away 2^23 rounds to a whole number, then one
 * down if that went up; from 2^23 a float is whole, and NaN passes. It gives
 * the console's floorf for every float (also +0 for -0), and the host's for
 * every x + 0.5f (never -0 or a signalling NaN). */
static inline float floor_px(float x)
{
    const float big = 8388608.0f;
    if (!(fabsf(x) < big)) return x;
    float r = x < 0 ? x - big + big : x + big - big;
    return r > x ? r - 1 : r;
}

/* Room for the next quad of a glyph run: one gfx_prim per up to
 * GFX_PRIM_MAX / 4 quads, not one per glyph (about 60 instructions and a
 * stack frame each). *left is the run's quads not reserved yet (counted
 * first), *room the reserved ones not written yet; q starts as NULL. gfx
 * joins consecutive quads of one texture into one draw, so the vertices and
 * the draw are those of one gfx_prim per glyph; only a drain, when the area
 * is full, comes before a reservation instead of inside it. */
static inline gfx_vtx *quad_next(gfx_vtx *q, int *room, int *left)
{
    if (!*room) {
        *room = *left < GFX_PRIM_MAX / 4 ? *left : GFX_PRIM_MAX / 4;
        *left -= *room;
        q = gfx_prim(GFX_QUADS, 4 * *room);
    }
    --*room;
    return q;
}

#endif
