/* UTF-8 text (ui.h, text_utf8): the Latin atlases of data/latin.bin
 * (art/make_font.py) into RSX memory, and the map from a code point to its
 * glyph. latin.bin is an archive member that only this file references, so
 * an app links both only when it calls text_utf8. */
#include <string.h>
#include "gfx.h"
#include "ui.h"
#include "ui_text.h"
#include "latin_bin.h"

static ui_atlas atlas[F_COUNT];
static u16 cps[UI_LATIN_MAX];                /* the code point of each slot, ascending */
static int ncp, square;                      /* the slots, and the white square's */

/* The base letters of U+0100 to U+017F, Latin Extended-A: a letter that the
 * atlas does not have draws as its base letter. */
static const char base_a[] =
    "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlL"
    "lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

/* Combining marks (a decomposed accent: its letter stands alone), the
 * zero-width spaces and joiners, the variation selectors, the byte order mark. */
static int zero_width(unsigned cp)
{
    return (cp >= 0x300 && cp < 0x370) || (cp >= 0x200b && cp <= 0x200f) || cp == 0x2060 ||
           (cp >= 0xfe00 && cp <= 0xfe0f) || cp == 0xfeff;
}

static int map(unsigned cp)
{
    int lo = 0, hi = ncp - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (cps[m] == cp) return m;
        if (cps[m] < cp) lo = m + 1;
        else hi = m - 1;
    }
    if (zero_width(cp)) return -1;
    if (cp >= 0x100 && cp < 0x180) return 0x100 + base_a[cp - 0x100];
    return square;
}

static unsigned rd16(const u8 *p) { return (unsigned)p[0] << 8 | p[1]; }
static const u8 *align4(const u8 *p) { return p + (4 - (p - latin_bin) % 4) % 4; }

/* latin.bin into RSX textures as fonts_init does fonts.bin: 4-bit alpha to
 * A4R4G4B4, white, bilinear unless the output is 1080p. */
static int load(void)
{
    const u8 *p = latin_bin, *end = latin_bin + latin_bin_size;
    if (latin_bin_size < 12 || memcmp(p, "PHL1", 4)) return -1;
    int n = rd16(p + 4), count = rd16(p + 6), cols = rd16(p + 8);
    if (n < F_COUNT || count < 1 || count > UI_LATIN_MAX || !cols) return -1;
    p += 12;
    if (end - p < 2 * count) return -1;
    for (int i = 0; i < count; i++) {
        cps[i] = (u16)rd16(p + 2 * i);
        if (i && cps[i] <= cps[i - 1]) return -1;
    }
    p = align4(p + 2 * count);
    for (int k = 0; k < F_COUNT; k++) {
        ui_atlas *a = &atlas[k];
        if (end - p < 16 + count) return -1;
        a->cw = rd16(p);
        a->ch = rd16(p + 2);
        a->base = rd16(p + 4);
        a->pad = rd16(p + 6);
        a->asc = rd16(p + 8);
        a->line = rd16(p + 10);
        a->aw = rd16(p + 12);
        a->ah = rd16(p + 14);
        a->cols = cols;
        memcpy(a->adv, p + 16, count);
        p = align4(p + 16 + count);
        int npx = a->aw * a->ah;
        if (!npx || (npx & 1) || end - p < npx / 2) return -1;
        u8 *t = gfx_tex_new(&a->tex, (u16)a->aw, (u16)a->ah, GFX_TEX_A4R4G4B4, !(gfx_w == 1920 && gfx_h == 1080));
        if (!t) return -1;
        const u32 *pair = ui_texel_pairs();      /* two texels per 32-bit store: half the stores to RSX memory */
        for (int y = 0; y < a->ah; y++) {
            u32 *row = (u32 *)(t + y * a->tex.pitch);
            const u8 *src = p + y * a->aw / 2;
            for (int x = 0; x < a->aw / 2; x++) row[x] = pair[src[x]];
        }
        p = align4(p + npx / 2);
    }
    ncp = count;
    square = map(0x25a1);
    if (square < 0 || square >= count) return -1;
    ui_text_latin(atlas, map);
    return 0;
}

/* The first call loads; a later one gives its result again. A failed load
 * keeps the RSX memory it took (gfx_vram gives none back), so it is not
 * tried again. */
int text_utf8(void)
{
    static int done;                         /* 1 loaded, -1 failed */
    if (!done) done = load() ? -1 : 1;
    return done > 0 ? 0 : -1;
}
