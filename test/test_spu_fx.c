/* Host test of the effects program's contract and kernels (source/spu_fx.h,
 * source/spu_fx_kern.h): the sizes and the layout; fx_quad's vertex order;
 * the known-answer test; fx_tou at cfltu's edges; FX_SKY's band equality
 * (each band job alone gives its rows of the full picture, the glass rows
 * with their margin too; a glass row from its cloud row's samples is the
 * margin rows' rule), its determinism, the coverage ends, the RGB rule
 * under alpha 0, the dither below 0x30 and a NaN or far sun; the blur
 * (flat in, flat out; the weights of three box passes sum to 343 per axis
 * and spread an impulse symmetrically); FX_AURORA's band equality and its
 * brightness cap; FX_RAIN's count, bounds, slant against the wind, cap,
 * determinism, the sum only when the block asks, snow and a NaN or huge
 * time; quads 0 in a sky or aurora report after a rain job; the jobs'
 * refusals; and that every driver waits for a buffer before it fills it
 * again. The drivers run with a host fx_io that copies each
 * piece where the SPU's DMA would put it and checks at the wait that the
 * piece did not change.
 *
 *   cc -std=c99 -O2 -Wall -Wextra -ffp-contract=off -I source -I source/gfx -I tools/preview/stub \
 *      -o test_spu_fx test/test_spu_fx.c -lm && ./test_spu_fx
 *
 * Options: --golden checks the pictures' hashes (the same bits on arm64
 * and x86-64 with -ffp-contract=off; not in CI, whose compiler may differ);
 * --out DIR writes the reference pictures as PPM; --bench times the jobs;
 * --quantiles prints the cloud value's quantiles (FX_CLOUD_MID). */
#define _POSIX_C_SOURCE 200112L              /* clock_gettime and snprintf under -std=c99, glibc and macOS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include "spu_fx_kern.h"
#include "gfx.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* ---- the host fx_io ------------------------------------------------------- */

static uint32_t fnv(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x01000193u;
    return h;
}

/* The SPU's DMA may read the local store until the wait: a put keeps the
 * piece's place, length and hash, and a piece that changed before its
 * wait is an error (the driver filled the buffer too early). */
typedef struct {
    fx_io io;
    uint8_t *dst[2];
    size_t size[2];
    int pending[2], bad, puts;
    uint32_t bytes;
    const void *ls[2];
    uint32_t len[2], hash[2];
} host_io;

static void h_put(fx_io *io, uint32_t out, uint32_t offset, const void *ls, uint32_t bytes, uint32_t buf)
{
    host_io *h = (host_io *)io;
    h->puts++;
    if (out > 1 || buf > 1 || !h->dst[out] || (size_t)offset + bytes > h->size[out] || bytes > 16384 || (bytes & 15) ||
        (offset & 15) || ((uintptr_t)ls & 15) || h->pending[buf]) {
        h->bad++;
        return;
    }
    memcpy(h->dst[out] + offset, ls, bytes);
    h->pending[buf] = 1;
    h->ls[buf] = ls;
    h->len[buf] = bytes;
    h->hash[buf] = fnv(ls, bytes);
    h->bytes += bytes;
}

static void h_wait(fx_io *io, uint32_t buf)
{
    host_io *h = (host_io *)io;
    if (buf > 1) {
        h->bad++;
        return;
    }
    if (h->pending[buf] && fnv(h->ls[buf], h->len[buf]) != h->hash[buf]) h->bad++;
    h->pending[buf] = 0;
}

static void io_init(host_io *h, void *d0, size_t s0, void *d1, size_t s1)
{
    memset(h, 0, sizeof *h);
    h->io.put = h_put;
    h->io.wait = h_wait;
    h->dst[0] = d0;
    h->size[0] = s0;
    h->dst[1] = d1;
    h->size[1] = s1;
}

/* ---- the scenes ----------------------------------------------------------------- */

static uint32_t tab[FX_NOISE_N * FX_NOISE_N];
static uint16_t band[3 * FX_GLASS_H * FX_GLASS_W] __attribute__((aligned(128)));
static fx_sky_ws sws;
static fx_aur_ws aws;
static fx_rain_state rst, rst2;
static fx_vtx quads[2][FX_RAIN_CHUNK * 4] __attribute__((aligned(128)));

static uint32_t lerp_rgb(uint32_t a, uint32_t b, float t)
{
    uint32_t r = 0;
    for (int c = 0; c < 3; c++) {
        float x = fx_ch(a, c) + (fx_ch(b, c) - fx_ch(a, c)) * t;
        r = r << 8 | (uint32_t)(x + 0.5f);
    }
    return r;
}

/* the sky of 3.2: top, top-horizon at 55 %, horizon at 80 %, then a quarter back */
static void sky_stops(fx_ctl *c, uint32_t top, uint32_t hor)
{
    c->p.sky.sky[0] = top;
    c->p.sky.sky[1] = lerp_rgb(top, hor, 0.55f);
    c->p.sky.sky[2] = hor;
    c->p.sky.sky[3] = lerp_rgb(hor, top, 0.25f);
}

enum { DAY, DUSK, NIGHT, SCENES };
static const char *scene_name[SCENES] = {"day_rain", "twilight", "night"};

static void sky_ctl(fx_ctl *c, int scene)
{
    memset(c, 0, sizeof *c);
    c->serial = 1;
    c->mode = FX_SKY;
    c->check_row = 100;
    c->out = 0x10000000u;
    c->out2 = 0x10080000u;
    c->y0 = 0;
    c->y1 = FX_SKY_H;
    c->time = 12.5f;
    c->seed = 7;
    c->p.sky.off_x = 830.0f;
    c->p.sky.off_y = -140.0f;
    c->p.sky.alpha = 0.92f;
    if (scene == DAY) {                      /* WMO 61, 88 % cover, wet: the dark cloud colours of the mockup */
        sky_stops(c, 0x3c4b63, 0x66727f);
        c->p.sky.cover = 0.88f;
        c->p.sky.lit = 0x7d8899;
        c->p.sky.shade = 0x3c4656;
        c->p.sky.edge = 0x7d8899;
        c->p.sky.sun_x = 330.0f;
        c->p.sky.sun_y = 40.0f;
        c->p.sky.sun_gain = 0.3f;
    } else if (scene == DUSK) {              /* civil twilight, 45 %: orange edges near the low sun */
        sky_stops(c, 0x1b2550, 0xc8644a);
        c->p.sky.cover = 0.45f;
        c->p.sky.lit = 0xc9b4b0;
        c->p.sky.shade = 0x3a3550;
        c->p.sky.edge = 0xf2a65a;
        c->p.sky.edge_amt = 0.6f;
        c->p.sky.sun_x = 380.0f;
        c->p.sky.sun_y = 230.0f;
        c->p.sky.sun_gain = 1.0f;
    } else {                                 /* a clear night, 30 % */
        sky_stops(c, 0x050a18, 0x0d1a33);
        c->p.sky.cover = 0.3f;
        c->p.sky.lit = 0x2a3550;
        c->p.sky.shade = 0x121a2c;
        c->p.sky.edge = 0x2a3550;
    }
}

static uint32_t pic[FX_SKY_H * FX_SKY_W], glass[FX_GLASS_H * FX_GLASS_W];
static uint32_t pic2[FX_SKY_H * FX_SKY_W], glass2[FX_GLASS_H * FX_GLASS_W];

/* a sky job into p and g with a band of cap rows; its return */
static uint32_t sky_run(const fx_ctl *c, uint32_t cap, uint32_t *p, uint32_t *g, fx_rec *rec, host_io *h)
{
    io_init(h, p, sizeof pic, g, sizeof glass);
    memset(rec, 0, sizeof *rec);
    return fx_sky_job(c, tab, &sws, band, cap, &h->io, rec);
}

/* ---- the contract ----------------------------------------------------------------- */

static void test_contract(void)
{
    CHECK(sizeof(fx_ctl) == 128 && _Alignof(fx_ctl) == 128);
    CHECK(sizeof(fx_rec) == 128 && _Alignof(fx_rec) == 128);
    CHECK(offsetof(fx_ctl, out) == 16 && offsetof(fx_ctl, y0) == 32 && offsetof(fx_ctl, p) == 48);
    CHECK(sizeof(((fx_ctl *)0)->p) == 64);
    /* the report's first 48 bytes are the kit's: magic, index, mode, sum, arg0 to arg2, count */
    CHECK(offsetof(fx_rec, sum) == 12 && offsetof(fx_rec, arg0) == 16 && offsetof(fx_rec, count) == 40);
    CHECK(offsetof(fx_rec, serial) == 48 && offsetof(fx_rec, kat) == 80);
    CHECK(fx_arg2(5) == 0xce110005u && fx_arg2(0x12345) == 0xce112345u);
    CHECK(FX_SKY_PITCH % 128 == 0 && FX_AURORA_PITCH % 128 == 0 && FX_GLASS_PITCH % 64 == 0);
    CHECK(FX_GLASS_BAND_MAX == 45);
    CHECK(fx_rain_quads(1500, FX_DROPS) == 1540 && fx_rain_quads(300, FX_FLAKES) == 300);
    /* fx_vtx is gfx_vtx */
    CHECK(sizeof(fx_vtx) == sizeof(gfx_vtx) && offsetof(fx_vtx, rgba) == offsetof(gfx_vtx, rgba) &&
          offsetof(fx_vtx, u) == offsetof(gfx_vtx, u) && offsetof(fx_vtx, v) == offsetof(gfx_vtx, v));
    /* fx_quad: lane j of each input is vertex j's field (the SPU's five quadwords, the same shuffles here) */
    fx_vtx qv[5] __attribute__((aligned(16)));
    memset(qv, 0xa5, sizeof qv);
    CHECK(fx_quad(qv, fxv4(1, 2, 3, 4), fxv4(5, 6, 7, 8), fxu4(9, 10, 11, 12), fxv4(13, 14, 15, 16), fxv4(17, 18, 19, 20)) ==
          qv + 4);
    for (int j = 0; j < 4; j++)
        CHECK(qv[j].x == 1 + j && qv[j].y == 5 + j && qv[j].rgba == 9u + j && qv[j].u == 13 + j && qv[j].v == 17 + j);
    CHECK(qv[4].rgba == 0xa5a5a5a5u);        /* nothing past the quad */
    uint32_t y0, y1, prev = 0;
    for (uint32_t i = 0; i < 5; i++) {
        fx_band(i, 5, FX_SKY_H, &y0, &y1);
        CHECK(y0 == prev && y1 - y0 == 54 && !(y0 & 1));
        prev = y1;
    }
    fx_band(4, 5, FX_AURORA_H, &y0, &y1);
    CHECK(y0 == 288 && y1 == 360);
    fx_band(2, 4, FX_SKY_H, &y0, &y1);
    CHECK(y0 == 134 && y1 == 202);
    CHECK(fabsf(fx_rain_wind(20, 135) + 127.279f) < 0.01f);      /* from the south-east: left */
    CHECK(fx_rain_wind(20, 270) > 179.9f);                         /* from the west: right */
    uint32_t k = fx_kat();
    if (k != FX_KAT) printf("fx_kat 0x%08x, FX_KAT 0x%08x\n", k, FX_KAT);
    CHECK(k == FX_KAT);
    /* fx_tou at cfltu's edges: 2^32 - 256 (the largest float under 2^32) converts exactly, 2^32 and up saturate */
    fxu t = fx_tou(fxv4(4294967040.0f, 4294967296.0f, INFINITY, 1e30f));
    CHECK(fxu_lane(t, 0) == 0xffffff00u && fxu_lane(t, 1) == 0xffffffffu && fxu_lane(t, 2) == 0xffffffffu &&
          fxu_lane(t, 3) == 0xffffffffu);
    t = fx_tou(fxv4(-1.0f, 0.99f, 1.0f, 2147483648.0f));
    CHECK(fxu_lane(t, 0) == 0 && fxu_lane(t, 1) == 0 && fxu_lane(t, 2) == 1 && fxu_lane(t, 3) == 0x80000000u);
}

/* ---- FX_SKY ------------------------------------------------------------------------ */

static void test_sky(void)
{
    fx_ctl c;
    fx_rec r;
    host_io h;
    for (int s = 0; s < SCENES; s++) {
        sky_ctl(&c, s);
        CHECK(sky_run(&c, FX_GLASS_H, pic, glass, &r, &h) == 0);
        CHECK(h.bad == 0 && h.bytes == sizeof pic + sizeof glass);
        CHECK(r.check_row == 100 && r.sum == fx_wsum(pic + 100 * FX_SKY_W, FX_SKY_W));
        CHECK(r.sum2 == fx_wsum(glass + 50 * FX_GLASS_W, FX_GLASS_W));
        /* five bands of 54 (the SPU's), then six uneven ones: each alone gives its rows */
        static const uint32_t cuts[2][7] = {{0, 54, 108, 162, 216, 270, 0}, {0, 40, 94, 148, 202, 256, 270}};
        for (int v = 0; v < 2; v++) {
            memset(pic2, 0xa5, sizeof pic2);
            memset(glass2, 0xa5, sizeof glass2);
            for (int b = 0; b < 6 && cuts[v][b + 1]; b++) {
                fx_ctl cb = c;
                cb.y0 = cuts[v][b];
                cb.y1 = cuts[v][b + 1];
                io_init(&h, pic2, sizeof pic2, glass2, sizeof glass2);
                memset(&r, 0, sizeof r);
                CHECK(fx_sky_job(&cb, tab, &sws, band, FX_GLASS_BAND_MAX, &h.io, &r) == 0);
                CHECK(h.bad == 0 && h.bytes == (cb.y1 - cb.y0) * (FX_SKY_PITCH + FX_GLASS_PITCH / 2));
                CHECK((r.check_row == 100) == (cb.y0 <= 100 && 100 < cb.y1));
            }
            CHECK(memcmp(pic, pic2, sizeof pic) == 0);
            CHECK(memcmp(glass, glass2, sizeof glass) == 0);
        }
        /* one rule for the glass source: a band's own row (from its even cloud row's samples) is the margin
           row's (samples of its own), and that even cloud row is fx_cloud_row's */
        {
            static fxu g1[3][FX_GROW], g2[3][FX_GROW];
            static uint32_t row1[FX_SKY_W], row2[FX_SKY_W];
            static const uint32_t gys[] = {0, 1, 26, 27, 50, 67, 101, 134};
            fx_sky_k k;
            fx_sky_prep(&c, &k);
            for (unsigned i = 0; i < sizeof gys / sizeof gys[0]; i++) {
                memset(g1, 0x5a, sizeof g1);
                memset(g2, 0xa5, sizeof g2);
                uint32_t s1 = fx_cloud_glass_row(&k, tab, 2 * gys[i], row1, g1);
                uint32_t s2 = fx_cloud_row(&k, tab, 2 * gys[i], row2);
                fx_glass_src(&k, tab, gys[i], g2);
                CHECK(s1 == s2 && memcmp(row1, row2, sizeof row1) == 0);
                for (int ch = 0; ch < 3; ch++) CHECK(memcmp(&g1[ch][1], &g2[ch][1], FX_GV * sizeof(fxu)) == 0);
            }
        }
        /* the RGB rule: a texel with alpha 0 has the cloud's colour, never black; the dither: no alpha in 1 to 15,
           every alpha under 0x30 a multiple of 0x10 */
        uint32_t zero = 0, black = 0, low = 0, odd = 0;
        for (int i = 0; i < FX_SKY_H * FX_SKY_W; i++) {
            uint32_t a = pic[i] >> 24;
            if (!a) zero++;
            if (!a && !(pic[i] & 0xffffff)) black++;
            if (a && a < 16) low++;
            if (a < 0x30 && (a & 15)) odd++;
        }
        CHECK(black == 0 && low == 0 && odd == 0);
        printf("scene %s: %.1f %% of the texels have alpha 0\n", scene_name[s], 100.0 * zero / (FX_SKY_H * FX_SKY_W));
        CHECK(zero > 0 && zero < FX_SKY_H * FX_SKY_W);               /* partly cloudy in every scene */
        for (int i = 0; i < FX_GLASS_H * FX_GLASS_W; i++) CHECK((glass[i] >> 24) == 255);
    }
    /* determinism: the same block, the same bytes; another seed or time, another picture */
    sky_ctl(&c, DUSK);
    sky_run(&c, FX_GLASS_H, pic, glass, &r, &h);
    sky_run(&c, FX_GLASS_H, pic2, glass2, &r, &h);
    CHECK(memcmp(pic, pic2, sizeof pic) == 0 && memcmp(glass, glass2, sizeof glass) == 0);
    c.seed = 8;
    sky_run(&c, FX_GLASS_H, pic2, glass2, &r, &h);
    CHECK(memcmp(pic, pic2, sizeof pic) != 0);
    c.seed = 7;
    c.time += 30.0f;
    sky_run(&c, FX_GLASS_H, pic2, glass2, &r, &h);
    CHECK(memcmp(pic, pic2, sizeof pic) != 0);
    /* a drift of one lattice period (64 cells) gives the same clouds */
    sky_ctl(&c, DAY);
    sky_run(&c, FX_GLASS_H, pic, glass, &r, &h);
    c.p.sky.off_x += FX_NOISE_N * FX_CLOUD_CELL;
    sky_run(&c, FX_GLASS_H, pic2, glass2, &r, &h);
    uint32_t near = 0;
    for (int i = 0; i < FX_SKY_H * FX_SKY_W; i++) near += abs((int)(pic[i] >> 24) - (int)(pic2[i] >> 24)) <= 16;
    CHECK(near == FX_SKY_H * FX_SKY_W);
    /* cover 0: no cloud, and the colour still there; cover 1: a deck (every alpha at least the faded 0x30) */
    sky_ctl(&c, DAY);
    c.p.sky.cover = 0.0f;
    sky_run(&c, FX_GLASS_H, pic, glass, &r, &h);
    uint32_t any = 0;
    for (int i = 0; i < FX_SKY_H * FX_SKY_W; i++) any |= pic[i] >> 24;
    CHECK(any == 0 && (pic[0] & 0xffffff) != 0);
    c.p.sky.cover = 1.0f;
    sky_run(&c, FX_GLASS_H, pic, glass, &r, &h);
    uint32_t min = 255;
    for (int i = 0; i < FX_SKY_H * FX_SKY_W; i++) min = (pic[i] >> 24) < min ? pic[i] >> 24 : min;
    CHECK(min >= 0x30);
    /* the sun at a NaN, an inf or far away: the picture without the sun's light */
    sky_ctl(&c, DAY);
    c.p.sky.sun_gain = 0.0f;
    sky_run(&c, FX_GLASS_H, pic, glass, &r, &h);
    static const float suns[][2] = {{NAN, 40.0f}, {330.0f, NAN}, {INFINITY, -INFINITY}, {-1e30f, 40.0f}};
    for (unsigned i = 0; i < sizeof suns / sizeof suns[0]; i++) {
        sky_ctl(&c, DAY);
        c.p.sky.sun_x = suns[i][0];
        c.p.sky.sun_y = suns[i][1];
        sky_run(&c, FX_GLASS_H, pic2, glass2, &r, &h);
        CHECK(memcmp(pic, pic2, sizeof pic) == 0 && memcmp(glass, glass2, sizeof glass) == 0);
    }
    /* the refusals: nothing written */
    sky_ctl(&c, DAY);
    static const uint32_t bad[][2] = {{1, 54}, {0, 53}, {54, 54}, {60, 54}, {0, 272}, {54, 112}, {100, 160}};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        c.y0 = bad[i][0];
        c.y1 = bad[i][1];
        CHECK(sky_run(&c, FX_GLASS_BAND_MAX, pic, glass, &r, &h) == FX_EXIT_CTL && h.puts == 0);
    }
    c.y0 = 0;
    c.y1 = 72;                               /* the top band has no margin above: 36 + 9 rows fit */
    CHECK(sky_run(&c, FX_GLASS_BAND_MAX, pic, glass, &r, &h) == 0);
}

/* the dither alone: for every alpha under 0x30, the 4 x 4 cell's mean is the alpha */
static void test_dither(void)
{
    for (uint32_t a = 0; a < 0x30; a++) {
        uint32_t s = 0;
        for (uint32_t y = 0; y < 4; y++) {
            fxu d = fx_dither(FXU(a), fx_bayer_row(y));
            for (int j = 0; j < 4; j++) {
                uint32_t v = fxu_lane(d, j);
                CHECK(v % 16 == 0 && v <= 0x30);
                s += v;
            }
        }
        CHECK(s == 16 * a);
    }
    for (uint32_t a = 0x30; a < 256; a++) CHECK(fxu_lane(fx_dither(FXU(a), fx_bayer_row(3)), 2) == a);
}

/* ---- the blur ------------------------------------------------------------------------- */

static fxu hb[2][3][FX_GROW];

static void test_blur(void)
{
    const uint32_t n = 45, cap = FX_GLASS_BAND_MAX;
    uint32_t row[FX_GLASS_W] __attribute__((aligned(16)));
    /* flat in, flat out (every value, the edges too) */
    for (uint32_t v = 0; v < 256; v += 17) {
        for (uint32_t r = 0; r < n; r++) {
            for (int c = 0; c < 3; c++)
                for (int i = 1; i <= FX_GV; i++) hb[0][c][i] = FXU(v + c);
            fx_glass_hrow(hb, band, cap, r);
        }
        fx_glass_vblur(band, cap, n);
        uint32_t ok = 1;
        for (uint32_t r = 0; r < n; r++) {
            fx_glass_out(band, cap, r, row);
            for (int x = 0; x < FX_GLASS_W; x++) ok &= row[x] == (0xff000000u | v << 16 | (v + 1) << 8 | (v + 2));
        }
        CHECK(ok);
    }
    /* horizontal raw sums of an impulse 1: they sum to 343, are symmetric, peak 37 (box7 * box7 * box7) */
    for (int c = 0; c < 3; c++)
        for (int i = 1; i <= FX_GV; i++) hb[0][c][i] = FXU(0);
    hb[0][0][31] = fxu4(0, 1, 0, 0);         /* column 121 */
    fx_hbox(hb[0][0], hb[1][0]);
    fx_hbox(hb[1][0], hb[0][0]);
    fx_hbox(hb[0][0], hb[1][0]);
    uint32_t w[FX_GLASS_W], tot = 0;
    for (int x = 0; x < FX_GLASS_W; x++) tot += w[x] = fxu_lane(hb[1][0][1 + x / 4], x % 4);
    CHECK(tot == 343 && w[121] == 37 && w[112] == 1 && w[130] == 1 && w[111] == 0 && w[131] == 0);
    for (int d = 1; d <= 9; d++) CHECK(w[121 - d] == w[121 + d]);
    /* vertical raw sums of an impulse 1 in row 22 of a column: the same weights */
    uint16_t col[45 * FX_GLASS_W] __attribute__((aligned(16)));
    memset(col, 0, sizeof col);
    col[22 * FX_GLASS_W + 3] = 1;
    fx_vbox(col, 45, 0);
    fx_vbox(col, 45, 0);
    fx_vbox(col, 45, 0);
    tot = 0;
    for (int y = 0; y < 45; y++) tot += col[y * FX_GLASS_W + 3];
    CHECK(tot == 343 && col[22 * FX_GLASS_W + 3] == 37);
    for (int d = 1; d <= 9; d++) CHECK(col[(22 - d) * FX_GLASS_W + 3] == col[(22 + d) * FX_GLASS_W + 3]);
    for (int y = 0; y < 45; y++) CHECK(col[y * FX_GLASS_W + 2] == 0 && col[y * FX_GLASS_W + 4] == 0);
    /* random rows through the whole blur equal a plain reference: three box7 passes of raw sums with the
       edges repeated, x then y, each axis divided by 343 and rounded */
    {
        static uint32_t src[3][45][FX_GLASS_W], tmp[45][FX_GLASS_W];
        static uint32_t ref[3][45][FX_GLASS_W];
        uint32_t s = 12345;
        for (int c = 0; c < 3; c++)
            for (uint32_t r = 0; r < n; r++)
                for (int x = 0; x < FX_GLASS_W; x++) src[c][r][x] = fx_rand(&s) >> 24;
        for (int c = 0; c < 3; c++) {
            for (uint32_t r = 0; r < n; r++) {
                uint32_t a[FX_GLASS_W], b[FX_GLASS_W];
                memcpy(a, src[c][r], sizeof a);
                for (int p = 0; p < 3; p++) {
                    for (int x = 0; x < FX_GLASS_W; x++) {
                        uint32_t t = 0;
                        for (int d = -3; d <= 3; d++) t += a[x + d < 0 ? 0 : x + d >= FX_GLASS_W ? FX_GLASS_W - 1 : x + d];
                        b[x] = t;
                    }
                    memcpy(a, b, sizeof a);
                }
                for (int x = 0; x < FX_GLASS_W; x++) tmp[r][x] = (a[x] * 2 + 343) / 686;
            }
            for (int x = 0; x < FX_GLASS_W; x++) {
                uint32_t a[45], b[45];
                for (uint32_t r = 0; r < n; r++) a[r] = tmp[r][x];
                for (int p = 0; p < 3; p++) {
                    for (int y = 0; y < (int)n; y++) {
                        uint32_t t = 0;
                        for (int d = -3; d <= 3; d++) t += a[y + d < 0 ? 0 : y + d >= (int)n ? (int)n - 1 : y + d];
                        b[y] = t;
                    }
                    memcpy(a, b, sizeof a);
                }
                for (uint32_t r = 0; r < n; r++) ref[c][r][x] = (a[r] * 2 + 343) / 686;
            }
        }
        for (uint32_t r = 0; r < n; r++) {
            for (int c = 0; c < 3; c++)
                for (int i = 1; i <= FX_GV; i++)
                    hb[0][c][i] = fxu4(src[c][r][4 * i - 4], src[c][r][4 * i - 3], src[c][r][4 * i - 2], src[c][r][4 * i - 1]);
            fx_glass_hrow(hb, band, cap, r);
        }
        fx_glass_vblur(band, cap, n);
        uint32_t diff = 0;
        for (int c = 0; c < 3; c++)
            for (uint32_t r = 0; r < n; r++)
                for (int x = 0; x < FX_GLASS_W; x++) diff += band[(c * cap + r) * FX_GLASS_W + x] != ref[c][r][x];
        CHECK(diff == 0);
    }
    /* an impulse 255 through the whole blur: symmetric on both axes, about 255 in all */
    for (uint32_t r = 0; r < n; r++) {
        for (int c = 0; c < 3; c++)
            for (int i = 1; i <= FX_GV; i++) hb[0][c][i] = FXU(0);
        if (r == 22) hb[0][1][31] = fxu4(0, 255, 0, 0);
        fx_glass_hrow(hb, band, cap, r);
    }
    fx_glass_vblur(band, cap, n);
    int sym = 1;
    uint32_t mass = 0;
    for (int dy = -9; dy <= 9; dy++)
        for (int dx = -9; dx <= 9; dx++) {
            uint32_t v = band[(cap + 22 + dy) * FX_GLASS_W + 121 + dx];
            sym &= v == band[(cap + 22 - dy) * FX_GLASS_W + 121 - dx] && v == band[(cap + 22 + dy) * FX_GLASS_W + 121 - dx];
            mass += v;
        }
    CHECK(sym && mass >= 200 && mass <= 300);   /* the tails under 0.5 round to 0 */
}

/* ---- FX_AURORA ---------------------------------------------------------------------- */

static uint32_t aur[FX_AURORA_H * FX_AURORA_W], aur2[FX_AURORA_H * FX_AURORA_W];

static void aurora_ctl(fx_ctl *c, float t, uint32_t seed)
{
    memset(c, 0, sizeof *c);
    c->serial = 3;
    c->mode = FX_AURORA;
    c->check_row = 200;
    c->y0 = 0;
    c->y1 = FX_AURORA_H;
    c->time = t;
    c->seed = seed;
    c->p.aurora.gain = 1.0f;
    c->p.aurora.top = 0x040814;
    c->p.aurora.bottom = 0x0a1222;
}

static uint32_t aurora_run(const fx_ctl *c, uint32_t *p, fx_rec *rec, host_io *h)
{
    io_init(h, p, sizeof aur, NULL, 0);
    memset(rec, 0, sizeof *rec);
    return fx_aurora_job(c, tab, &aws, &h->io, rec);
}

static double luma_mean(const uint32_t *p, int n, double *peak)
{
    double s = 0, m = 0;
    for (int i = 0; i < n; i++) {
        double l = 0.299 * ((p[i] >> 16) & 255) + 0.587 * ((p[i] >> 8) & 255) + 0.114 * (p[i] & 255);
        s += l;
        m = l > m ? l : m;
    }
    if (peak) *peak = m / 255;
    return s / n / 255;
}

static void test_aurora(void)
{
    fx_ctl c;
    fx_rec r;
    host_io h;
    aurora_ctl(&c, 0.0f, 41);
    CHECK(aurora_run(&c, aur, &r, &h) == 0 && h.bad == 0 && h.bytes == sizeof aur);
    CHECK(r.check_row == 200 && r.sum == fx_wsum(aur + 200 * FX_AURORA_W, FX_AURORA_W));
    for (int i = 0; i < FX_AURORA_H * FX_AURORA_W; i++) CHECK((aur[i] >> 24) == 255);
    memset(aur2, 0x5a, sizeof aur2);
    for (uint32_t b = 0; b < 5; b++) {
        fx_ctl cb = c;
        fx_band(b, 5, FX_AURORA_H, &cb.y0, &cb.y1);
        io_init(&h, aur2, sizeof aur2, NULL, 0);
        CHECK(fx_aurora_job(&cb, tab, &aws, &h.io, &r) == 0 && h.bad == 0);
    }
    CHECK(memcmp(aur, aur2, sizeof aur) == 0);
    aurora_run(&c, aur2, &r, &h);
    CHECK(memcmp(aur, aur2, sizeof aur) == 0);
    c.time = 10.0f;
    aurora_run(&c, aur2, &r, &h);
    CHECK(memcmp(aur, aur2, sizeof aur) != 0);
    /* the cap: at gain 1 (and above: clamped) the mean brightness stays at most 20 % */
    static const float ts[] = {0, 5, 13, 40, 97, 300, 1234, 3600, 36000};
    double worst = 0, peak = 0, p;
    for (uint32_t seed = 0; seed < 4; seed++)
        for (unsigned i = 0; i < sizeof ts / sizeof ts[0]; i++) {
            aurora_ctl(&c, ts[i], seed);
            c.p.aurora.gain = seed == 3 ? 7.0f : 1.0f;
            aurora_run(&c, aur2, &r, &h);
            double m = luma_mean(aur2, FX_AURORA_H * FX_AURORA_W, &p);
            worst = m > worst ? m : worst;
            peak = p > peak ? p : peak;
        }
    printf("aurora: mean brightness at most %.1f %% (peak pixel %.0f %%) over 36 pictures at gain 1\n", worst * 100,
           peak * 100);
    CHECK(worst <= 0.20);
    aurora_ctl(&c, 0.0f, 41);
    c.p.aurora.gain = 0.0f;
    aurora_run(&c, aur2, &r, &h);
    CHECK(luma_mean(aur2, FX_AURORA_H * FX_AURORA_W, NULL) < 0.06);   /* the sky and the stars */
    c.y0 = 10;
    c.y1 = 10;
    CHECK(aurora_run(&c, aur2, &r, &h) == FX_EXIT_CTL && h.puts == 0);
    c.y1 = 361;
    CHECK(aurora_run(&c, aur2, &r, &h) == FX_EXIT_CTL && h.puts == 0);
}

/* ---- FX_RAIN ------------------------------------------------------------------------- */

static fx_vtx vtx[(FX_RAIN_MAX + FX_SPLASHES) * 4], vtx2[(FX_RAIN_MAX + FX_SPLASHES) * 4];

static void rain_ctl(fx_ctl *c, uint32_t kind, uint32_t count)
{
    memset(c, 0, sizeof *c);
    c->serial = 9;
    c->mode = FX_RAIN;
    c->seed = 5;
    c->p.rain.count = count;
    c->p.rain.kind = kind;
    c->p.rain.rgb = kind == FX_DROPS ? 0x9cc8ff : 0xf2f5fa;
    c->p.rain.dt = 1.0f / 60;
    c->p.rain.wind = fx_rain_wind(20, 135);
    c->p.rain.fall = kind == FX_DROPS ? 1300.0f : 90.0f;
    c->p.rain.ox = -67.0f;                   /* the default area: the screen's top left at canvas -67, -38 */
    c->p.rain.oy = -38.0f;
    c->p.rain.alpha = 1.0f;
    c->p.rain.check = 1;                     /* the sum in every report (an app asks in one job of many) */
}

static uint32_t rain_run(const fx_ctl *c, fx_rain_state *st, fx_vtx *v, fx_rec *rec, host_io *h)
{
    io_init(h, v, sizeof vtx, NULL, 0);
    memset(rec, 0, sizeof *rec);
    return fx_rain_job(c, st, quads, &h->io, rec);
}

static void test_rain(void)
{
    fx_ctl c;
    fx_rec r;
    host_io h;
    memset(&rst, 0, sizeof rst);
    rain_ctl(&c, FX_DROPS, 1500);
    int nan = 0, out = 0, slant = 0, angle = 0, tail = 0, frames = 120;
    for (int f = 0; f < frames; f++) {
        CHECK(rain_run(&c, &rst, vtx, &r, &h) == 0 && h.bad == 0);
        CHECK(r.quads == 1540 && h.bytes == 1540 * 80 && r.sum == fx_vsum(vtx, 1540 * 4));
        for (int i = 0; i < 1540 * 4; i++) {
            const fx_vtx *v = &vtx[i];
            nan += !isfinite(v->x) || !isfinite(v->y) || !isfinite(v->u) || !isfinite(v->v);
            out += v->x < c.p.rain.ox - FX_RAIN_MARGIN - 20 || v->x > c.p.rain.ox + 1920 + FX_RAIN_MARGIN + 20 ||
                   v->y < c.p.rain.oy - 150 || v->y > c.p.rain.oy + 1080 + 30;
        }
        for (int i = 0; i < 1500; i++) {
            const fx_vtx *q = vtx + 4 * i;   /* tail left, tail right, head right, head left */
            float dx = (q[2].x + q[3].x - q[0].x - q[1].x) / 2, dy = q[2].y - q[0].y;
            slant += !(dx < 0) || fabsf(dx - c.p.rain.wind * FX_STREAK) > 1e-3f || !(dy > 0);
            tail += (q[0].rgba & 255) != 0 || (q[3].rgba & 255) < 0x28 || (q[3].rgba & 255) > 0xa6 ||
                    (q[3].rgba >> 8) != 0x9cc8ff;
            if (i % 3 == 2) {                /* the near drops: about 5.6 deg for 20 km/h from the south-east */
                double deg = atan2(-dx, dy) * 57.29578;
                angle += deg < 5.0 || deg > 6.3;
            }
        }
    }
    CHECK(nan == 0 && out == 0 && slant == 0 && angle == 0 && tail == 0);
    /* splashes came, and each lives 6 frames */
    uint32_t live = 0;
    for (int i = 0; i < FX_SPLASHES; i++) live += rst.s[i].live && rst.s[i].age < 6;
    CHECK(live > 0);
    /* the cap: a storm wind gives 70 % of the fall: the near drops' slant is at most 35 deg */
    c.p.rain.wind = 1e6f;
    rain_run(&c, &rst, vtx, &r, &h);
    int capped = 0;
    for (int i = 0; i < 1500; i++) {
        const fx_vtx *q = vtx + 4 * i;
        capped += fabsf((q[2].x + q[3].x - q[0].x - q[1].x) / 2 - 0.7f * 1300 * FX_STREAK) < 1e-3f;
    }
    CHECK(capped == 1500);
    c.p.rain.wind = fx_rain_wind(30, 270);  /* from the west: right */
    rain_run(&c, &rst, vtx, &r, &h);
    for (int i = 0; i < 1500; i++) CHECK(vtx[4 * i + 2].x > vtx[4 * i + 1].x);
    /* determinism: two states, the same blocks, the same quads */
    memset(&rst, 0, sizeof rst);
    memset(&rst2, 0, sizeof rst2);
    rain_ctl(&c, FX_DROPS, 1100);
    for (int f = 0; f < 30; f++) {
        rain_run(&c, &rst, vtx, &r, &h);
        uint32_t s1 = r.sum;
        rain_run(&c, &rst2, vtx2, &r, &h);
        CHECK(s1 == r.sum && memcmp(vtx, vtx2, 1140 * 80) == 0);
    }
    /* check 0: sum 0, the same quads and the same particles after */
    c.p.rain.check = 0;
    for (int f = 0; f < 3; f++) {
        rain_run(&c, &rst, vtx, &r, &h);
        uint32_t s1 = r.sum;
        c.p.rain.check = 1;
        rain_run(&c, &rst2, vtx2, &r, &h);
        c.p.rain.check = 0;
        CHECK(s1 == 0 && r.sum == fx_vsum(vtx2, 1140 * 4) && r.sum != 0 && memcmp(vtx, vtx2, 1140 * 80) == 0);
        CHECK(memcmp(&rst, &rst2, sizeof rst) == 0);
    }
    c.p.rain.check = 1;
    /* the degrade step: 750 drops, 790 quads; back to 1500 */
    c.p.rain.count = 750;
    CHECK(rain_run(&c, &rst, vtx, &r, &h) == 0 && r.quads == 790 && h.bytes == 790 * 80);
    c.p.rain.count = 1500;
    CHECK(rain_run(&c, &rst, vtx, &r, &h) == 0 && r.quads == 1540 && rst.live == 1500);
    /* snow: 300 squares of 4, 6 or 9 px with the glow's UVs, swaying at most 20 px, falling 90 px/s at most */
    memset(&rst, 0, sizeof rst);
    rain_ctl(&c, FX_FLAKES, 300);
    float y_first[300];
    for (int f = 0; f <= 60; f++) {
        c.time = f / 60.0f;
        CHECK(rain_run(&c, &rst, vtx, &r, &h) == 0 && r.quads == 300 && h.bytes == 300 * 80);
        for (int i = 0; i < 300; i++) {
            const fx_vtx *q = vtx + 4 * i;
            float s = q[1].x - q[0].x, want = (float[]){4, 6, 9}[i % 3];
            CHECK(fabsf(s - want) < 1e-3f && fabsf(q[2].y - q[0].y - want) < 1e-3f);
            CHECK(q[0].u == 0 && q[2].u == 1 && q[2].v == 1 && isfinite(q[0].x));
            float base = rst.d[i].x + c.p.rain.ox;
            CHECK(fabsf((q[0].x + q[1].x) / 2 - base) <= 20.01f);
            if (f == 0) y_first[i] = rst.d[i].y;
            if (f == 60 && rst.d[i].y > y_first[i]) CHECK(rst.d[i].y - y_first[i] <= 99.1f && rst.d[i].y - y_first[i] >= 48.0f);
        }
    }
    /* the snow at a NaN, an inf or a time past 2^25 s: the sway of time 0; under it a sway (no int32_t overflow) */
    rain_ctl(&c, FX_FLAKES, 300);
    memset(&rst, 0, sizeof rst);
    rain_run(&c, &rst, vtx, &r, &h);
    static const float bad_t[] = {NAN, INFINITY, -INFINITY, 1e10f};
    for (unsigned i = 0; i < sizeof bad_t / sizeof bad_t[0]; i++) {
        c.time = bad_t[i];
        memset(&rst2, 0, sizeof rst2);
        CHECK(rain_run(&c, &rst2, vtx2, &r, &h) == 0 && memcmp(vtx, vtx2, 300 * 80) == 0);
    }
    c.time = 3e7f;
    memset(&rst2, 0, sizeof rst2);
    CHECK(rain_run(&c, &rst2, vtx2, &r, &h) == 0 && memcmp(vtx, vtx2, 300 * 80) != 0);
    for (int i = 0; i < 300 * 4; i++) CHECK(isfinite(vtx2[i].x));
    /* a sky or an aurora job after a rain job in the same report: quads 0 */
    rain_ctl(&c, FX_DROPS, 1500);
    CHECK(rain_run(&c, &rst, vtx, &r, &h) == 0 && r.quads == 1540);
    fx_ctl cs;
    sky_ctl(&cs, DAY);
    cs.y1 = 54;
    io_init(&h, pic, sizeof pic, glass, sizeof glass);
    CHECK(fx_sky_job(&cs, tab, &sws, band, FX_GLASS_BAND_MAX, &h.io, &r) == 0 && r.quads == 0);
    CHECK(rain_run(&c, &rst, vtx, &r, &h) == 0 && r.quads == 1540);
    aurora_ctl(&cs, 0.0f, 41);
    cs.y1 = 72;
    io_init(&h, aur, sizeof aur, NULL, 0);
    CHECK(fx_aurora_job(&cs, tab, &aws, &h.io, &r) == 0 && r.quads == 0);
    /* the refusals */
    rain_ctl(&c, FX_DROPS, 1501);
    CHECK(rain_run(&c, &rst, vtx, &r, &h) == FX_EXIT_CTL && h.puts == 0);
    rain_ctl(&c, FX_FLAKES, 301);
    CHECK(rain_run(&c, &rst, vtx, &r, &h) == FX_EXIT_CTL && h.puts == 0);
    rain_ctl(&c, 2, 10);
    CHECK(rain_run(&c, &rst, vtx, &r, &h) == FX_EXIT_CTL && h.puts == 0);
    rain_ctl(&c, FX_DROPS, 10);
    c.p.rain.fall = 0;
    CHECK(rain_run(&c, &rst, vtx, &r, &h) == FX_EXIT_CTL && h.puts == 0);
}

/* ---- the reference pictures ------------------------------------------------------------ */

static int write_ppm(const char *dir, const char *name, int w, int h, const float *rgb)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h * 3; i++) fputc((int)(rgb[i] < 0 ? 0 : rgb[i] > 255 ? 255 : rgb[i] + 0.5f), f);
    return fclose(f);
}

/* the sky gradient of a block at screen fraction f */
static void grad(const fx_ctl *c, float f, float *o)
{
    int s = f < 0.55f ? 0 : f < 0.8f ? 1 : 2;
    float y0 = s == 0 ? 0 : s == 1 ? 0.55f : 0.8f, y1 = s == 0 ? 0.55f : s == 1 ? 0.8f : 1.0f, t = (f - y0) / (y1 - y0);
    for (int i = 0; i < 3; i++) o[i] = fx_ch(c->p.sky.sky[s], i) + (fx_ch(c->p.sky.sky[s + 1], i) - fx_ch(c->p.sky.sky[s], i)) * t;
}

/* quads rasterized over img (w x h, canvas units * scale, minus the origin): two triangles each,
   the vertex alpha interpolated, a round fall-off when round (the glow texture) */
static void raster(float *img, int w, int h, float scale, float ox, float oy, const fx_vtx *v, int n, int round)
{
    for (int q = 0; q < n; q++)
        for (int t = 0; t < 2; t++) {
            const fx_vtx *a = &v[4 * q], *b = &v[4 * q + 1 + t], *c = &v[4 * q + 2 + t];
            float ax = (a->x - ox) * scale, ay = (a->y - oy) * scale, bx = (b->x - ox) * scale, by = (b->y - oy) * scale;
            float cx = (c->x - ox) * scale, cy = (c->y - oy) * scale;
            float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (fabsf(area) < 1e-6f) continue;
            int x0 = (int)floorf(fminf(ax, fminf(bx, cx))), x1 = (int)ceilf(fmaxf(ax, fmaxf(bx, cx)));
            int y0 = (int)floorf(fminf(ay, fminf(by, cy))), y1 = (int)ceilf(fmaxf(ay, fmaxf(by, cy)));
            for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < h; y++)
                for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < w; x++) {
                    float px = x + 0.5f, py = y + 0.5f;
                    float l1 = ((cx - bx) * (py - by) - (cy - by) * (px - bx)) / area;
                    float l2 = ((ax - cx) * (py - cy) - (ay - cy) * (px - cx)) / area;
                    float l3 = 1 - l1 - l2;
                    if (l1 < 0 || l2 < 0 || l3 < 0) continue;
                    float al = (l1 * (a->rgba & 255) + l2 * (b->rgba & 255) + l3 * (c->rgba & 255)) / 255;
                    if (round) {
                        float u = l1 * a->u + l2 * b->u + l3 * c->u - 0.5f, vv = l1 * a->v + l2 * b->v + l3 * c->v - 0.5f;
                        float d = 1 - 4 * (u * u + vv * vv);
                        al *= d < 0 ? 0 : d * d;
                    }
                    for (int i = 0; i < 3; i++) {
                        float s = (float)((a->rgba >> (24 - 8 * i)) & 255);
                        float *o = &img[(y * w + x) * 3 + i];
                        *o += (s - *o) * al;
                    }
                }
        }
}

static float rgbf[1920 * 1080 * 3];

static void write_refs(const char *dir)
{
    fx_ctl c;
    fx_rec r;
    host_io h;
    char name[64];
    for (int s = 0; s < SCENES; s++) {
        sky_ctl(&c, s);
        sky_run(&c, FX_GLASS_H, pic, glass, &r, &h);
        /* the clouds over the gradient (the alpha as the RSX's alpha test and blend take it), the alpha, the glass */
        for (int y = 0; y < FX_SKY_H; y++) {
            float g[3];
            grad(&c, (y + 0.5f) / FX_SKY_H, g);
            for (int x = 0; x < FX_SKY_W; x++) {
                uint32_t p = pic[y * FX_SKY_W + x];
                float a = (p >> 24) < 16 ? 0 : (p >> 24) / 255.0f;
                for (int i = 0; i < 3; i++) rgbf[(y * FX_SKY_W + x) * 3 + i] = g[i] + (fx_ch(p, i) - g[i]) * a;
            }
        }
        snprintf(name, sizeof name, "clouds_%s", scene_name[s]);
        write_ppm(dir, name, FX_SKY_W, FX_SKY_H, rgbf);
        for (int i = 0; i < FX_SKY_W * FX_SKY_H; i++) rgbf[3 * i] = rgbf[3 * i + 1] = rgbf[3 * i + 2] = (float)(pic[i] >> 24);
        snprintf(name, sizeof name, "clouds_%s_alpha", scene_name[s]);
        write_ppm(dir, name, FX_SKY_W, FX_SKY_H, rgbf);
        for (int i = 0; i < FX_GLASS_W * FX_GLASS_H; i++)
            for (int k = 0; k < 3; k++) rgbf[3 * i + k] = fx_ch(glass[i], k);
        snprintf(name, sizeof name, "glass_%s", scene_name[s]);
        write_ppm(dir, name, FX_GLASS_W, FX_GLASS_H, rgbf);
    }
    aurora_ctl(&c, 0.0f, 41);
    aurora_run(&c, aur, &r, &h);
    for (int i = 0; i < FX_AURORA_W * FX_AURORA_H; i++)
        for (int k = 0; k < 3; k++) rgbf[3 * i + k] = fx_ch(aur[i], k);
    write_ppm(dir, "aurora", FX_AURORA_W, FX_AURORA_H, rgbf);
    /* the rain after 2 s (120 jobs), and the snow, over the day sky at 1920 x 1080 */
    for (int kind = 0; kind < 2; kind++) {
        sky_ctl(&c, DAY);
        for (int y = 0; y < 1080; y++) {
            float g[3];
            grad(&c, (y + 0.5f) / 1080, g);
            for (int x = 0; x < 1920; x++)
                for (int i = 0; i < 3; i++) rgbf[(y * 1920 + x) * 3 + i] = g[i] * 0.8f;
        }
        fx_ctl rc;
        rain_ctl(&rc, kind, kind ? 300 : 1100);
        memset(&rst, 0, sizeof rst);
        for (int f = 0; f < 120; f++) {
            rc.time = f / 60.0f;
            rain_run(&rc, &rst, vtx, &r, &h);
        }
        raster(rgbf, 1920, 1080, 1.0f, rc.p.rain.ox, rc.p.rain.oy, vtx, (int)r.quads, kind);
        write_ppm(dir, kind ? "snow" : "rain", 1920, 1080, rgbf);
    }
    printf("reference pictures in %s\n", dir);
}

/* ---- the golden hashes (--golden) --------------------------------------------------- */

static void golden(void)
{
    /* clang 21 on arm64 (-O2, -O0) and on x86-64 (Rosetta): the same bits (2026-10-13; the sky's six
     * again on 2026-10-14, the five-octave clouds; the three glass on 2026-10-17, the samples at 2 gx, 2 gy:
     * arm64 -O2 and -O0 only, Rosetta did not run) */
    static const uint32_t want[] = {0xd79e27c1, 0xdb85a36a, 0x871960e1, 0x3b7e4f0a, 0xf822c06e,
                                    0x400faa04, 0xf317c787, 0x91773a61, 0x53902be5, 0x5c6026a1};
    uint32_t got[10];
    int n = 0;
    fx_ctl c;
    fx_rec r;
    host_io h;
    for (int s = 0; s < SCENES; s++) {
        sky_ctl(&c, s);
        sky_run(&c, FX_GLASS_H, pic, glass, &r, &h);
        got[n++] = fnv(pic, sizeof pic);
        got[n++] = fnv(glass, sizeof glass);
    }
    aurora_ctl(&c, 0.0f, 41);
    aurora_run(&c, aur, &r, &h);
    got[n++] = fnv(aur, sizeof aur);
    aurora_ctl(&c, 777.0f, 2);
    aurora_run(&c, aur, &r, &h);
    got[n++] = fnv(aur, sizeof aur);
    for (int kind = 0; kind < 2; kind++) {
        fx_ctl rc;
        rain_ctl(&rc, kind, kind ? 300 : 1500);
        memset(&rst, 0, sizeof rst);
        for (int f = 0; f < 60; f++) {
            rc.time = f / 60.0f;
            rain_run(&rc, &rst, vtx, &r, &h);
        }
        got[n++] = fnv(vtx, r.quads * 80);
    }
    int bad = 0;
    for (int i = 0; i < n; i++) bad += got[i] != want[i];
    printf("golden:");
    for (int i = 0; i < n; i++) printf(" 0x%08x", got[i]);
    printf(bad ? " (%d differ)\n" : " (all equal)\n", bad);
    if (bad) fails++;
}

/* ---- the timings (--bench) ------------------------------------------------------------ */

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void bench(void)
{
    fx_ctl c;
    fx_rec r;
    host_io h;
    int reps = 40;
    sky_ctl(&c, DUSK);
    double t0 = now();
    for (int i = 0; i < reps; i++)
        for (uint32_t b = 0; b < 5; b++) {
            fx_ctl cb = c;
            fx_band(b, 5, FX_SKY_H, &cb.y0, &cb.y1);
            io_init(&h, pic, sizeof pic, glass, sizeof glass);
            fx_sky_job(&cb, tab, &sws, band, FX_GLASS_BAND_MAX, &h.io, &r);
        }
    double sky = (now() - t0) / reps;
    /* the parts of one band: the clouds (with the glass source of the band's 27 rows), the margins' glass
       source (18 rows), the blur */
    fx_sky_k k;
    fx_sky_prep(&c, &k);
    t0 = now();
    for (int i = 0; i < reps * 5; i++)
        for (uint32_t y = 0; y < 54; y++) {
            if (y & 1) fx_cloud_row(&k, tab, y, sws.rows[0]);
            else fx_cloud_glass_row(&k, tab, y, sws.rows[0], sws.h[0]);
        }
    double cl = (now() - t0) / (reps * 5);
    t0 = now();
    for (int i = 0; i < reps * 5; i++)
        for (uint32_t gy = 0; gy < 18; gy++) fx_glass_src(&k, tab, gy, sws.h[0]);
    double src = (now() - t0) / (reps * 5);
    t0 = now();
    for (int i = 0; i < reps * 5; i++) {
        for (uint32_t gy = 0; gy < 45; gy++) fx_glass_hrow(sws.h, band, 45, gy);
        fx_glass_vblur(band, 45, 45);
    }
    double blur = (now() - t0) / (reps * 5);
    aurora_ctl(&c, 3.0f, 41);
    t0 = now();
    for (int i = 0; i < reps; i++)
        for (uint32_t b = 0; b < 5; b++) {
            fx_ctl cb = c;
            fx_band(b, 5, FX_AURORA_H, &cb.y0, &cb.y1);
            io_init(&h, aur, sizeof aur, NULL, 0);
            fx_aurora_job(&cb, tab, &aws, &h.io, &r);
        }
    double au = (now() - t0) / reps;
    rain_ctl(&c, FX_DROPS, 1500);
    c.p.rain.check = 0;                      /* the common job: no sum */
    memset(&rst, 0, sizeof rst);
    t0 = now();
    for (int i = 0; i < reps * 10; i++) rain_run(&c, &rst, vtx, &r, &h);
    double ra = (now() - t0) / (reps * 10);
    printf("host time (this CPU, one thread):\n");
    printf("  sky picture, 5 bands: %.3f ms (a band %.3f ms: clouds %.3f, margins' glass %.3f, blur %.3f; %.1f ns a cloud pixel)\n",
           sky * 1e3, sky * 1e3 / 5, cl * 1e3, src * 1e3, blur * 1e3, cl * 1e9 / (54 * FX_SKY_W));
    printf("  aurora picture, 5 bands: %.3f ms (%.1f ns a pixel)\n", au * 1e3, au * 1e9 / (FX_AURORA_W * FX_AURORA_H));
    printf("  rain, 1,500 drops and 40 splashes: %.3f ms a frame (%.1f ns a quad)\n", ra * 1e3, ra * 1e9 / 1540);
}

/* ---- the coverage quantiles (--quantiles) ------------------------------------------- */

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

/* The values of fx_cloud_n over 64 drift offsets and times (every 4th
 * pixel of each picture), and their quantiles every 10 %: the centres of
 * FX_CLOUD_MID (cover c: the 1 - c quantile). */
static void quantiles(void)
{
    enum { N = 64, SX = 120, SY = 68 };
    static float v[N * SX * SY];
    int n = 0;
    for (int i = 0; i < N; i++) {
        fx_ctl c;
        fx_sky_k k;
        sky_ctl(&c, DUSK);
        c.p.sky.off_x = 311.0f * (float)i;
        c.p.sky.off_y = -173.0f * (float)i;
        c.time = 97.0f * (float)i;
        fx_sky_prep(&c, &k);
        for (int y = 0; y < SY; y++)
            for (int x = 0; x < SX; x += 1) {
                fxv hf, s = fx_cloud_n(&k, tab, fxv4(4.0f * (float)x, 4.0f * (float)x + 1, 4.0f * (float)x + 2, 4.0f * (float)x + 3),
                                         FXV(4.0f * (float)y + 1), &hf);
                v[n++] = fxv_lane(s, i & 3);
            }
    }
    qsort(v, (size_t)n, sizeof v[0], cmpf);
    printf("quantiles of n (%d samples, min %.3f, max %.3f), cover 10 %% to 90 %%:", n, v[0], v[n - 1]);
    for (int q = 9; q >= 1; q--) printf(" %.3ff,", v[(size_t)n * q / 10]);
    printf("\n");
}

int main(int argc, char **argv)
{
    int gold = 0, ben = 0, qua = 0;
    const char *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--golden")) gold = 1;
        else if (!strcmp(argv[i], "--bench")) ben = 1;
        else if (!strcmp(argv[i], "--quantiles")) qua = 1;
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else {
            printf("usage: %s [--golden] [--bench] [--quantiles] [--out DIR]\n", argv[0]);
            return 2;
        }
    }
    fx_noise_init(tab);
    test_contract();
    test_dither();
    test_blur();
    test_sky();
    test_aurora();
    test_rain();
    if (gold) golden();
    if (out) write_refs(out);
    if (ben) bench();
    if (qua) quantiles();
    if (fails) {
        printf("%d checks failed\n", fails);
        return 1;
    }
    printf("spu_fx: all checks passed\n");
    return 0;
}
