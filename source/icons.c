/* The icons (icons.h): data/icons.bin (art/make_icons.py) into RSX memory,
 * and the glyph quads. icons.bin is an archive member that only this file
 * references, so an app links both only when it calls icons_init. */
#include <math.h>
#include <string.h>
#include "gfx.h"
#include "icons.h"
#include "icons_bin.h"

#define IC_SETS 3

typedef struct {
    int set;
    float w, h, cx, cy;                      /* the cell, and the centre in it */
    float u0, v0, u1, v1;
} iglyph;

typedef struct {
    int px;
    gfx_tex tex;
    gfx_tex lin;                             /* the same texels, bilinear: icon_rot's (an S or M icon that moves) */
} iset;

static iglyph glyphs[IC_COUNT];
static iset sets[IC_SETS];
static int done;                             /* 1 after icons_init, -1 after one that failed */

static unsigned rd16(const u8 *p) { return (unsigned)p[0] << 8 | p[1]; }

/* floorf in float adds, as digits.c's floor_px (-mcpu=cell has no frim):
 * the console's floorf for every float, the host's for every x + 0.5f. */
static inline float floor_px(float x)
{
    const float big = 8388608.0f;
    if (!(fabsf(x) < big)) return x;
    float r = x < 0 ? x - big + big : x + big - big;
    return r > x ? r - 1 : r;
}

int icons_init(void)
{
    const u8 *p = icons_bin, *end = icons_bin + icons_bin_size;
    u32 pair[256];
    if (done) return done > 0 ? 0 : -1;
    done = -1;                               /* until the end: gfx_vram gives no memory back, so a retry would take more */
    if (icons_bin_size < 12 || p[0] != 'P' || p[1] != 'H' || p[2] != 'I' || p[3] != '1') return -1;
    if (rd16(p + 4) != IC_SETS || rd16(p + 6) != IC_COUNT) return -1;
    p += 12;
    if (end - p < 16 * IC_COUNT) return -1;
    const u8 *table = p;
    p += 16 * IC_COUNT;
    /* A byte of 4-bit alphas as its two A4R4G4B4 texels in memory order, white
     * also at alpha 0: the L set's bilinear samples mix no black into the
     * edges (a grey fringe on a bright sky). */
    for (int b = 0; b < 256; b++) {
        u16 t[2] = {(u16)((b >> 4) << 12 | 0x0fff), (u16)((b & 15) << 12 | 0x0fff)};
        memcpy(&pair[b], t, 4);
    }
    int aw[IC_SETS], ah[IC_SETS];
    for (int k = 0; k < IC_SETS; k++) {
        if (end - p < 8) return -1;
        int turns = rd16(p + 2);
        sets[k].px = rd16(p);
        aw[k] = rd16(p + 4);
        ah[k] = rd16(p + 6);
        p += 8;
        int npx = aw[k] * ah[k];
        if (!npx || (aw[k] & 1) || end - p < npx / 2) return -1;
        u16 linear = turns || !(gfx_w == 1920 && gfx_h == 1080);
        u8 *t = gfx_tex_new(&sets[k].tex, (u16)aw[k], (u16)ah[k], GFX_TEX_A4R4G4B4, linear);
        if (!t) return -1;
        sets[k].lin = sets[k].tex;           /* no new RSX memory: a second gfx_tex, so gfx binds it anew */
        sets[k].lin.linear = 1;
        for (int y = 0; y < ah[k]; y++) {
            u32 *row = (u32 *)(t + y * sets[k].tex.pitch);
            const u8 *src = p + y * aw[k] / 2;
            for (int x = 0; x < aw[k] / 2; x++) row[x] = pair[src[x]];
        }
        p += npx / 2;
        p += (4 - (p - icons_bin) % 4) % 4;
    }
    for (int i = 0; i < IC_COUNT; i++) {
        const u8 *e = table + 16 * i;
        int k = rd16(e), x = rd16(e + 2), y = rd16(e + 4), w = rd16(e + 6), h = rd16(e + 8);
        if (k >= IC_SETS || !w || !h || x + w > aw[k] || y + h > ah[k]) return -1;
        iglyph *g = &glyphs[i];
        g->set = k;
        g->w = w;
        g->h = h;
        g->cx = rd16(e + 10);
        g->cy = rd16(e + 12);
        g->u0 = (float)x / aw[k];
        g->v0 = (float)y / ah[k];
        g->u1 = (float)(x + w) / aw[k];
        g->v1 = (float)(y + h) / ah[k];
    }
    done = 1;
    return 0;
}

int icon_size(int id) { return done > 0 && id >= 0 && id < IC_COUNT ? sets[glyphs[id].set].px : 0; }

void icon(int id, float cx, float cy, u32 rgba)
{
    if (done <= 0 || id < 0 || id >= IC_COUNT) return;
    const iglyph *g = &glyphs[id];
    float x0 = floor_px(cx + 0.5f) - g->cx, y0 = floor_px(cy + 0.5f) - g->cy, x1 = x0 + g->w, y1 = y0 + g->h;
    gfx_texture(&sets[g->set].tex);
    gfx_vtx *q = gfx_prim(GFX_QUADS, 4);
    q = gfx_put(q, x0, y0, rgba, g->u0, g->v0);
    q = gfx_put(q, x1, y0, rgba, g->u1, g->v0);
    q = gfx_put(q, x1, y1, rgba, g->u1, g->v1);
    gfx_put(q, x0, y1, rgba, g->u0, g->v1);
    gfx_texture(NULL);
}

void icon_rot(int id, float cx, float cy, float angle, float scale, u32 rgba)
{
    if (done <= 0 || id < 0 || id >= IC_COUNT) return;
    const iglyph *g = &glyphs[id];
    float c = cosf(angle) * scale, s = sinf(angle) * scale;
    float dx[4] = {-g->cx, g->w - g->cx, g->w - g->cx, -g->cx}, dy[4] = {-g->cy, -g->cy, g->h - g->cy, g->h - g->cy};
    float u[4] = {g->u0, g->u1, g->u1, g->u0}, v[4] = {g->v0, g->v0, g->v1, g->v1};
    gfx_texture(&sets[g->set].lin);
    gfx_vtx *q = gfx_prim(GFX_QUADS, 4);
    for (int k = 0; k < 4; k++) q = gfx_put(q, cx + dx[k] * c - dy[k] * s, cy + dx[k] * s + dy[k] * c, rgba, u[k], v[k]);
    gfx_texture(NULL);
}
