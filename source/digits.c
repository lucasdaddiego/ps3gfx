/* The display digits (digits.h): data/digits.bin (art/make_font.py
 * --digits) into RSX memory, and the glyph quads. digits.bin is an archive
 * member that only this file references, so an app links both only when it
 * calls digits_init. */
#include <stddef.h>
#include "gfx.h"
#include "digits.h"
#include "ui_kit.h"
#include "digits_bin.h"

#define DG_MAX 32                            /* glyphs of an atlas */

typedef struct { float u0, u1, v0, v1, adv; } dglyph;

typedef struct {
    int cw, ch, base, pad, asc, line, aw, ah, mid;
    dglyph g[DG_MAX];
    gfx_tex tex;
} dfont;

static dfont dfonts[D_COUNT];
static u8 slot[256];                         /* the glyph of a byte plus 1, 0 for none */
static int done;                             /* 1 after digits_init, -1 after one that failed */

static unsigned rd16(const u8 *p) { return (unsigned)p[0] << 8 | p[1]; }
static const u8 *align4(const u8 *p) { return p + (4 - (p - digits_bin) % 4) % 4; }

int digits_init(void)
{
    const u8 *p = digits_bin, *end = digits_bin + digits_bin_size;
    if (done) return done > 0 ? 0 : -1;
    done = -1;                               /* until the end: gfx_vram gives no memory back, so a retry would take more */
    if (digits_bin_size < 12 || p[0] != 'P' || p[1] != 'H' || p[2] != 'D' || p[3] != '1') return -1;
    int n = rd16(p + 4), count = rd16(p + 6), cols = rd16(p + 8);
    if (n < D_COUNT || count < 1 || count > DG_MAX || !cols) return -1;
    p += 12;
    if (end - p < 2 * count) return -1;
    for (int i = 0; i < count; i++) {
        unsigned cp = rd16(p + 2 * i);
        if (cp > 255 || cp == 0xc2) return -1;   /* C2: the UTF-8 lead byte of the degree sign draws nothing */
        slot[cp] = (u8)(i + 1);
    }
    p = align4(p + 2 * count);
    /* ui.c's texel pairs, white also at alpha 0: a bilinear sample (at 720p
     * and 480p) then mixes no black into the edges. */
    const u32 *pair = ui_texel_pairs();
    u16 linear = !(gfx_w == 1920 && gfx_h == 1080);   /* as the fonts: nearest at 1080p */
    for (int k = 0; k < D_COUNT; k++) {
        dfont *f = &dfonts[k];
        if (end - p < 20 + count) return -1;
        f->cw = rd16(p);
        f->ch = rd16(p + 2);
        f->base = rd16(p + 4);
        f->pad = rd16(p + 6);
        f->asc = rd16(p + 8);
        f->line = rd16(p + 10);
        f->aw = rd16(p + 12);
        f->ah = rd16(p + 14);
        f->mid = rd16(p + 16);
        const u8 *adv = p + 20;
        p = align4(p + 20 + count);
        int npx = f->aw * f->ah;
        if (!npx || (f->aw & 1) || end - p < npx / 2 || f->mid > f->ch) return -1;
        u8 *t = gfx_tex_new(&f->tex, (u16)f->aw, (u16)f->ah, GFX_TEX_A4R4G4B4, linear);
        if (!t) return -1;
        for (int y = 0; y < f->ah; y++) {
            u32 *row = (u32 *)(t + y * f->tex.pitch);
            const u8 *src = p + y * f->aw / 2;
            for (int x = 0; x < f->aw / 2; x++) row[x] = pair[src[x]];
        }
        p = align4(p + npx / 2);
        float du = (float)f->cw / f->aw, dv = (float)f->ch / f->ah;
        for (int i = 0; i < count; i++) {
            dglyph *g = &f->g[i];
            float u = (i % cols) * du, v = (i / cols) * dv;
            g->u0 = u;
            g->u1 = u + du;
            g->v0 = v;
            g->v1 = v + dv;
            g->adv = adv[i];
        }
    }
    done = 1;
    return 0;
}

/* The glyph of a byte, -1 for none. The UTF-8 lead byte of the degree sign,
 * C2, has no slot (digits_init refuses one), so C2 B0 draws B0's glyph. */
static int glyph_at(unsigned char c) { return (int)slot[c] - 1; }

static int ok(int size) { return done > 0 && size >= 0 && size < D_COUNT; }

int digits_w(int size, const char *s)
{
    int w = 0;
    if (!ok(size)) return 0;
    for (int i = 0; s[i]; i++) {
        int k = glyph_at(s[i]);
        if (k >= 0) w += (int)dfonts[size].g[k].adv;
    }
    return w;
}

int digits_h(int size) { return ok(size) ? dfonts[size].line : 0; }

int digits_mid(int size) { return ok(size) ? dfonts[size].asc - dfonts[size].base + dfonts[size].mid : 0; }

float digits(float x, float y, int size, u32 rgba, const char *s)
{
    if (!ok(size)) return x;
    const dfont *f = &dfonts[size];
    float px = floor_px(x + 0.5f), top = floor_px(y + 0.5f) + f->asc - f->base;
    float pad = f->pad, cw = f->cw, y1 = top + f->ch;
    int left = 0, room = 0;
    gfx_vtx *q = NULL;
    for (int i = 0; s[i]; i++) left += glyph_at(s[i]) >= 0;   /* the quads, for quad_next */
    gfx_texture(&f->tex);
    for (int i = 0; s[i]; i++) {
        int k = glyph_at(s[i]);
        if (k < 0) continue;
        const dglyph *g = &f->g[k];
        float x0 = px - pad, x1 = x0 + cw;
        q = quad_next(q, &room, &left);
        q = gfx_put(q, x0, top, rgba, g->u0, g->v0);
        q = gfx_put(q, x1, top, rgba, g->u1, g->v0);
        q = gfx_put(q, x1, y1, rgba, g->u1, g->v1);
        q = gfx_put(q, x0, y1, rgba, g->u0, g->v1);
        px += g->adv;
    }
    gfx_texture(NULL);
    return px;
}

float digits_r(float xr, float y, int size, u32 rgba, const char *s)
{
    float x = xr - digits_w(size, s);
    digits(x, y, size, rgba, s);
    return x;
}

float digits_half(float x, float y, int size, char ch, int half, float scale_y, u32 rgba)
{
    if (!ok(size)) return x;
    int k = (int)slot[(unsigned char)ch] - 1;
    float px = floor_px(x + 0.5f);
    if (k < 0) return px;
    const dfont *f = &dfonts[size];
    const dglyph *g = &f->g[k];
    if (scale_y > 0) {
        float top = floor_px(y + 0.5f) + f->asc - f->base, line = top + f->mid;
        float x0 = px - f->pad, x1 = x0 + f->cw;
        float vm = g->v0 + (float)f->mid / f->ah;      /* the middle line's row in the atlas */
        float ya, yb, va, vb;
        if (!half) {
            ya = line - f->mid * scale_y;
            yb = line;
            va = g->v0;
            vb = vm;
        } else {
            ya = line;
            yb = line + (f->ch - f->mid) * scale_y;
            va = vm;
            vb = g->v1;
        }
        gfx_texture(&f->tex);
        gfx_vtx *q = gfx_prim(GFX_QUADS, 4);
        q = gfx_put(q, x0, ya, rgba, g->u0, va);
        q = gfx_put(q, x1, ya, rgba, g->u1, va);
        q = gfx_put(q, x1, yb, rgba, g->u1, vb);
        gfx_put(q, x0, yb, rgba, g->u0, vb);
        gfx_texture(NULL);
    }
    return px + g->adv;
}
