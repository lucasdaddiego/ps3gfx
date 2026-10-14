/* The kernels of the effects program (<ps3gfx/spu_fx.h> has the contract):
 * plain C on a small 4-lane layer, spu-gcc's vectors under __SPU__ and four
 * plain lanes elsewhere (Cell Bench's cell_mandel.h pattern). The SPU
 * program, the host test and an app's preview twin run this same source,
 * the job drivers included: a driver computes into local-store buffers and
 * hands each finished piece to an fx_io (the SPU's DMA, the host's memcpy).
 *
 * The SPU's float arithmetic truncates and has no denormals, so an SPU
 * picture and a host picture can differ in the last bit of a channel. The
 * blur is integer arithmetic, the same bits on both. On the host, build
 * with -ffp-contract=off: a fused multiply-add changes the host's bits.
 *
 * The local store (spu/fx/main.c): the noise table 16 KB, the rain's
 * particles 24.6 KB, and one union of the jobs' buffers (sky 73 KB, aurora
 * 20 KB, rain 31.3 KB). */
#ifndef SPU_FX_KERN_H
#define SPU_FX_KERN_H

#include <stdint.h>
#include "spu_fx.h"

/* ---- the 4-lane layer ------------------------------------------------------- */

#if defined(__SPU__)
#include <spu_intrinsics.h>
typedef vec_float4 fxv;
typedef vec_uint4 fxu;
#define FXV(a) spu_splats((float)(a))
#define FXU(a) spu_splats((uint32_t)(a))
static inline fxv fxv4(float a, float b, float c, float d) { return (fxv){a, b, c, d}; }
static inline fxu fxu4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return (fxu){a, b, c, d}; }
static inline fxv fx_add(fxv a, fxv b) { return spu_add(a, b); }
static inline fxv fx_sub(fxv a, fxv b) { return spu_sub(a, b); }
static inline fxv fx_mul(fxv a, fxv b) { return spu_mul(a, b); }
static inline fxv fx_madd(fxv a, fxv b, fxv c) { return spu_madd(a, b, c); }      /* a * b + c */
static inline fxv fx_nmsub(fxv a, fxv b, fxv c) { return spu_nmsub(a, b, c); }    /* c - a * b */
static inline fxu fx_gt(fxv a, fxv b) { return spu_cmpgt(a, b); }                  /* all ones where a > b */
static inline fxv fx_sel(fxv a, fxv b, fxu m) { return spu_sel(a, b, m); }         /* b where m */
static inline fxu fxu_add(fxu a, fxu b) { return spu_add(a, b); }
static inline fxu fxu_sub(fxu a, fxu b) { return spu_sub(a, b); }
static inline fxu fxu_and(fxu a, fxu b) { return spu_and(a, b); }
static inline fxu fxu_or(fxu a, fxu b) { return spu_or(a, b); }
static inline fxu fxu_xor(fxu a, fxu b) { return spu_xor(a, b); }
static inline fxu fxu_shl(fxu a, uint32_t n) { return spu_sl(a, n); }
static inline fxu fxu_shr(fxu a, uint32_t n) { return spu_rlmask(a, -(int)n); }
static inline fxu fxu_gt(fxu a, fxu b) { return spu_cmpgt(a, b); }                 /* unsigned */
static inline fxu fxu_sel(fxu a, fxu b, fxu m) { return spu_sel(a, b, m); }
static inline fxu fx_tou(fxv a) { return spu_convtu(a, 0); }                       /* toward zero; under 0: 0 */
static inline fxv fx_tof(fxu a) { return spu_convtf(a, 0); }
static inline fxu fx_bits(fxv a) { return (fxu)a; }
static inline fxv fx_float(fxu a) { return (fxv)a; }
static inline uint32_t fxu_lane(fxu a, int j) { return spu_extract(a, j); }
static inline float fxv_lane(fxv a, int j) { return spu_extract(a, j); }
/* t[i] for each lane */
static inline fxu fxu_gather(const uint32_t *t, fxu i)
{
    return (fxu){t[spu_extract(i, 0)], t[spu_extract(i, 1)], t[spu_extract(i, 2)], t[spu_extract(i, 3)]};
}
/* lanes k to k + 3 of the eight lanes a, b (k 1 to 3) */
static inline fxu fxu_cat(fxu a, fxu b, int k)
{
    static const vec_uchar16 pat[3] = {
        {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19},
        {8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
        {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27}};
    return spu_shuffle(a, b, pat[k - 1]);
}
/* eight 16-bit words at p (16-byte aligned) as two 4-lane words, and back
 * (each lane's low 16 bits) */
static inline void fxu_ld16(const uint16_t *p, fxu *lo, fxu *hi)
{
    const vec_uchar16 l = {0x80, 0x80, 0, 1, 0x80, 0x80, 2, 3, 0x80, 0x80, 4, 5, 0x80, 0x80, 6, 7};
    const vec_uchar16 h = {0x80, 0x80, 8, 9, 0x80, 0x80, 10, 11, 0x80, 0x80, 12, 13, 0x80, 0x80, 14, 15};
    fxu q = *(const fxu *)p;
    *lo = spu_shuffle(q, q, l);
    *hi = spu_shuffle(q, q, h);
}
static inline void fxu_st16(uint16_t *p, fxu lo, fxu hi)
{
    const vec_uchar16 s = {2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31};
    *(fxu *)p = spu_shuffle(lo, hi, s);
}

#else
#include <string.h>
typedef struct { float f[4]; } fxv;
typedef struct { uint32_t u[4]; } fxu;
#define FX_V(op) { fxv r; for (int k = 0; k < 4; k++) r.f[k] = (op); return r; }
#define FX_U(op) { fxu r; for (int k = 0; k < 4; k++) r.u[k] = (op); return r; }
static inline fxv fx_splatv(float a) FX_V(a)
static inline fxu fx_splatu(uint32_t a) FX_U(a)
#define FXV(a) fx_splatv((float)(a))
#define FXU(a) fx_splatu((uint32_t)(a))
static inline fxv fxv4(float a, float b, float c, float d) { fxv r = {{a, b, c, d}}; return r; }
static inline fxu fxu4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { fxu r = {{a, b, c, d}}; return r; }
static inline fxv fx_add(fxv a, fxv b) FX_V(a.f[k] + b.f[k])
static inline fxv fx_sub(fxv a, fxv b) FX_V(a.f[k] - b.f[k])
static inline fxv fx_mul(fxv a, fxv b) FX_V(a.f[k] * b.f[k])
static inline fxv fx_madd(fxv a, fxv b, fxv c) FX_V(a.f[k] * b.f[k] + c.f[k])
static inline fxv fx_nmsub(fxv a, fxv b, fxv c) FX_V(c.f[k] - a.f[k] * b.f[k])
static inline fxu fx_gt(fxv a, fxv b) FX_U(a.f[k] > b.f[k] ? 0xffffffffu : 0)
static inline fxv fx_sel(fxv a, fxv b, fxu m) FX_V(m.u[k] ? b.f[k] : a.f[k])
static inline fxu fxu_add(fxu a, fxu b) FX_U(a.u[k] + b.u[k])
static inline fxu fxu_sub(fxu a, fxu b) FX_U(a.u[k] - b.u[k])
static inline fxu fxu_and(fxu a, fxu b) FX_U(a.u[k] & b.u[k])
static inline fxu fxu_or(fxu a, fxu b) FX_U(a.u[k] | b.u[k])
static inline fxu fxu_xor(fxu a, fxu b) FX_U(a.u[k] ^ b.u[k])
static inline fxu fxu_shl(fxu a, uint32_t n) FX_U(a.u[k] << n)
static inline fxu fxu_shr(fxu a, uint32_t n) FX_U(a.u[k] >> n)
static inline fxu fxu_gt(fxu a, fxu b) FX_U(a.u[k] > b.u[k] ? 0xffffffffu : 0)
static inline fxu fxu_sel(fxu a, fxu b, fxu m) FX_U(m.u[k] ? b.u[k] : a.u[k])
/* as the SPU's cfltu: toward zero, saturated (no NaN reaches it) */
static inline fxu fx_tou(fxv a) FX_U(!(a.f[k] > 0.0f) ? 0u : a.f[k] >= 4294967040.0f ? 0xffffffffu : (uint32_t)a.f[k])
static inline fxv fx_tof(fxu a) FX_V((float)a.u[k])
static inline fxu fx_bits(fxv a) { fxu r; memcpy(&r, &a, sizeof r); return r; }
static inline fxv fx_float(fxu a) { fxv r; memcpy(&r, &a, sizeof r); return r; }
static inline uint32_t fxu_lane(fxu a, int j) { return a.u[j]; }
static inline float fxv_lane(fxv a, int j) { return a.f[j]; }
static inline fxu fxu_gather(const uint32_t *t, fxu i) FX_U(t[i.u[k]])
static inline fxu fxu_cat(fxu a, fxu b, int n) FX_U(k + n < 4 ? a.u[k + n] : b.u[k + n - 4])
static inline void fxu_ld16(const uint16_t *p, fxu *lo, fxu *hi)
{
    for (int k = 0; k < 4; k++) {
        lo->u[k] = p[k];
        hi->u[k] = p[k + 4];
    }
}
static inline void fxu_st16(uint16_t *p, fxu lo, fxu hi)
{
    for (int k = 0; k < 4; k++) {
        p[k] = (uint16_t)lo.u[k];
        p[k + 4] = (uint16_t)hi.u[k];
    }
}
#undef FX_V
#undef FX_U
#endif

/* ---- helpers on the layer ----------------------------------------------------- */

static inline fxv fx_max(fxv a, fxv b) { return fx_sel(a, b, fx_gt(b, a)); }
static inline fxv fx_min(fxv a, fxv b) { return fx_sel(a, b, fx_gt(a, b)); }
static inline fxv fx_clamp01(fxv a) { return fx_min(fx_max(a, FXV(0.0f)), FXV(1.0f)); }
static inline fxu fxu_min(fxu a, fxu b) { return fxu_sel(a, b, fxu_gt(a, b)); }
/* a + (b - a) * t */
static inline fxv fx_mix(fxv a, fxv b, fxv t) { return fx_madd(fx_sub(b, a), t, a); }
/* round to an integer 0 to 255 (a >= 0 or clamped to 0) */
static inline fxu fx_byte(fxv a) { return fxu_min(fx_tou(fx_add(a, FXV(0.5f))), FXU(255)); }
/* 2^-e for e >= 0 to about 2e-4: the integer part by the exponent, the
 * fraction by a degree-5 polynomial of e^(-f ln 2) */
static inline fxv fx_exp2n(fxv e)
{
    e = fx_min(e, FXV(30.0f));
    fxu i = fx_tou(e);
    fxv f = fx_sub(e, fx_tof(i));
    fxv p = fx_madd(FXV(-0.0013333558f), f, FXV(0.0096181291f));
    p = fx_madd(p, f, FXV(-0.0555041087f));
    p = fx_madd(p, f, FXV(0.2402265070f));
    p = fx_madd(p, f, FXV(-0.6931471806f));
    p = fx_madd(p, f, FXV(1.0f));
    return fx_mul(p, fx_float(fxu_shl(fxu_sub(FXU(127), i), 23)));
}

/* the word sum of n words */
static inline uint32_t fx_wsum(const uint32_t *w, uint32_t n)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += w[i];
    return s;
}

/* x - p * floor(x / p): 0 <= result < p (0 for |x / p| >= 2^24) */
static inline float fx_wrap(float x, float p)
{
    float q = x / p;
    if (!(q < 16777216.0f && q > -16777216.0f)) return 0.0f;
    int32_t i = (int32_t)q;
    if ((float)i > q) i--;
    float r = x - (float)i * p;
    return r < 0.0f ? 0.0f : r >= p ? 0.0f : r;
}

/* a scalar 32-bit mix (lowbias32): seeds and the noise table */
static inline uint32_t fx_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* xorshift32, and a float in [0, 1) from it (24 bits: exact) */
static inline uint32_t fx_rand(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static inline float fx_rnd(uint32_t *s) { return (float)(fx_rand(s) >> 8) * (1.0f / 16777216.0f); }

/* sin(2 pi f) for any f, about 2e-4: the quarter wave by a degree-7 Taylor
 * polynomial (scalar) */
static inline float fx_sin2pi(float f)
{
    int32_t i = (int32_t)f;
    if ((float)i > f) i--;
    float t = (f - (float)i) * 4.0f;
    int32_t q = (int32_t)t;
    float r = t - (float)q;
    if (q & 1) r = 1.0f - r;
    float x = r * 1.5707963f, x2 = x * x;
    float s = x * (1.0f + x2 * (-0.16666667f + x2 * (0.0083333333f - x2 * 0.00019841270f)));
    return (q & 2) ? -s : s;
}

static inline float fx_ch(uint32_t rgb, int c) { return (float)((rgb >> (16 - 8 * c)) & 255); }

/* ---- the noise table ------------------------------------------------------------- */

/* Value noise on a 64 x 64 lattice that tiles: cell (i, j) is one word with
 * the values of its four corners, v(i, j) << 24 | v(i + 1, j) << 16 |
 * v(i, j + 1) << 8 | v(i + 1, j + 1), so a sample is one table read per
 * lane. 16 KB. */
#define FX_NOISE_N 64
#define FX_NOISE_SEED 0x5eedc10du

static inline uint32_t fx_lattice(uint32_t i, uint32_t j)
{
    return fx_hash((i & (FX_NOISE_N - 1)) + FX_NOISE_N * (j & (FX_NOISE_N - 1)) + FX_NOISE_SEED) >> 24;
}

static inline void fx_noise_init(uint32_t *tab)
{
    for (uint32_t j = 0; j < FX_NOISE_N; j++)
        for (uint32_t i = 0; i < FX_NOISE_N; i++)
            tab[j * FX_NOISE_N + i] = fx_lattice(i, j) << 24 | fx_lattice(i + 1, j) << 16 | fx_lattice(i, j + 1) << 8 |
                                      fx_lattice(i + 1, j + 1);
}

/* the noise at (u, v) in lattice cells, u and v >= 0: 0 to 1, smooth (a
 * cubic fade between the corners) */
static inline fxv fx_noise(const uint32_t *tab, fxv u, fxv v)
{
    fxu iu = fx_tou(u), iv = fx_tou(v);
    fxv fu = fx_sub(u, fx_tof(iu)), fv = fx_sub(v, fx_tof(iv));
    fxu idx = fxu_or(fxu_shl(fxu_and(iv, FXU(FX_NOISE_N - 1)), 6), fxu_and(iu, FXU(FX_NOISE_N - 1)));
    fxu w = fxu_gather(tab, idx);
    fxv c00 = fx_tof(fxu_shr(w, 24)), c10 = fx_tof(fxu_and(fxu_shr(w, 16), FXU(255)));
    fxv c01 = fx_tof(fxu_and(fxu_shr(w, 8), FXU(255))), c11 = fx_tof(fxu_and(w, FXU(255)));
    fxv su = fx_mul(fx_mul(fu, fu), fx_nmsub(FXV(2.0f), fu, FXV(3.0f)));
    fxv sv = fx_mul(fx_mul(fv, fv), fx_nmsub(FXV(2.0f), fv, FXV(3.0f)));
    fxv a = fx_mix(c00, c10, su), b = fx_mix(c01, c11, su);
    return fx_mul(fx_mix(a, b, sv), FXV(1.0f / 255.0f));
}

/* 1D noise along u >= 0 on lattice row j: the corners v(i, j), v(i + 1, j) */
static inline fxv fx_noise1(const uint32_t *tab, uint32_t j, fxv u)
{
    fxu iu = fx_tou(u);
    fxv fu = fx_sub(u, fx_tof(iu));
    fxu idx = fxu_or(FXU((j & (FX_NOISE_N - 1)) << 6), fxu_and(iu, FXU(FX_NOISE_N - 1)));
    fxu w = fxu_gather(tab, idx);
    fxv c0 = fx_tof(fxu_shr(w, 24)), c1 = fx_tof(fxu_and(fxu_shr(w, 16), FXU(255)));
    fxv su = fx_mul(fx_mul(fu, fu), fx_nmsub(FXV(2.0f), fu, FXV(3.0f)));
    return fx_mul(fx_mix(c0, c1, su), FXV(1.0f / 255.0f));
}

/* ---- the output ---------------------------------------------------------------- */

/* Where a driver's finished pieces go: put copies bytes from ls to byte
 * offset of output out (0: ctl.out, 1: ctl.out2) through buffer buf (0 or
 * 1); wait returns when buffer buf may be written again. The SPU's put is
 * one DMA (at most 16 KB), its wait the tag's. */
typedef struct fx_io {
    void (*put)(struct fx_io *io, uint32_t out, uint32_t offset, const void *ls, uint32_t bytes, uint32_t buf);
    void (*wait)(struct fx_io *io, uint32_t buf);
} fx_io;

/* ---- FX_SKY: the clouds and the glass ---------------------------------------- */

/* the numbers of one sky job, from its block */
typedef struct {
    float sc, ox, oy;                        /* octave 1: cells per pixel, the offsets in cells */
    float ws, wx, wy;                        /* the warp's noise */
    float lo, iw;                            /* coverage: t = (n - lo) * iw */
    float amax;                              /* the alpha of a thick cloud, 0 to 255 */
    float lit[3], shade[3], edge[3], edge_amt;
    float sun_x, sun_y, sun_k, sun_gain, flash;
    float sky[4][3];
} fx_sky_k;

#define FX_CLOUD_CELL 80.0f                  /* picture pixels per lattice cell, octave 1 */
/* The octaves' weights (sum 1), the coverage band's width in n, the
 * thickness over which the light falls, the finest octaves' shading, and
 * the band's centre for cover 0 %, 10 %, ... 100 % (the quantiles of n that
 * test_spu_fx --quantiles measures; the ends sit past every sample). */
#define FX_CLOUD_W1 0.46f
#define FX_CLOUD_W2 0.25f
#define FX_CLOUD_W3 0.15f
#define FX_CLOUD_W4 0.09f
#define FX_CLOUD_W5 0.05f
#define FX_CLOUD_BAND 0.085f
#define FX_CLOUD_THICK 0.30f
#define FX_CLOUD_TEX 0.45f
#define FX_CLOUD_MID {1.06f, 0.654f, 0.601f, 0.561f, 0.526f, 0.494f, 0.462f, 0.429f, 0.392f, 0.344f, -0.04f}

static inline float fx_clampf(float x, float lo, float hi) { return !(x > lo) ? lo : x > hi ? hi : x; }

static inline void fx_sky_prep(const fx_ctl *c, fx_sky_k *k)
{
    const float period = FX_NOISE_N * FX_CLOUD_CELL;
    uint32_t h = fx_hash(c->seed);
    float s0 = (float)(h & 0xfff) * (FX_CLOUD_CELL / 64.0f), s1 = (float)(h >> 20) * (FX_CLOUD_CELL / 64.0f);
    float cover = fx_clampf(c->p.sky.cover, 0.0f, 1.0f);
    k->sc = 1.0f / FX_CLOUD_CELL;
    k->ox = fx_wrap(c->p.sky.off_x + s0, period) * k->sc + FX_NOISE_N;
    k->oy = fx_wrap(c->p.sky.off_y + s1, period) * k->sc + FX_NOISE_N;
    k->ws = 1.0f / 170.0f;
    k->wx = fx_wrap(c->time * 0.021f, FX_NOISE_N) + FX_NOISE_N;
    k->wy = fx_wrap(c->time * 0.013f + 17.0f, FX_NOISE_N) + FX_NOISE_N;
    /* the coverage: the band (FX_CLOUD_BAND wide) is centred on the value
     * that the share 1 - cover of the sky's samples stays under (the
     * measured quantiles of n, every 10 %), so cover is about the cloudy
     * share; 0 leaves the band above every sample, 1 below */
    static const float mid[11] = FX_CLOUD_MID;
    int ci = (int)(cover * 10.0f);
    ci = ci > 9 ? 9 : ci;
    float m = mid[ci] + (mid[ci + 1] - mid[ci]) * (cover * 10.0f - (float)ci);
    k->lo = m - 0.5f * FX_CLOUD_BAND;
    k->iw = 1.0f / FX_CLOUD_BAND;
    k->amax = fx_clampf(c->p.sky.alpha, 0.0f, 1.0f) * 255.0f;
    for (int i = 0; i < 3; i++) {
        k->lit[i] = fx_ch(c->p.sky.lit, i);
        k->shade[i] = fx_ch(c->p.sky.shade, i);
        k->edge[i] = fx_ch(c->p.sky.edge, i);
        for (int j = 0; j < 4; j++) k->sky[j][i] = fx_ch(c->p.sky.sky[j], i);
    }
    k->edge_amt = fx_clampf(c->p.sky.edge_amt, 0.0f, 1.0f);
    k->sun_x = c->p.sky.sun_x;
    k->sun_y = c->p.sky.sun_y;
    k->sun_k = 1.0f / (160.0f * 160.0f);
    k->sun_gain = fx_clampf(c->p.sky.sun_gain, 0.0f, 1.0f) * 0.35f;
    k->flash = fx_clampf(c->p.sky.flash, 0.0f, 1.0f) * 0.6f;
}

/* the alpha's fade toward the horizon: 1 at the top, 0.745 at 75 % of the
 * height, 0.43 at the bottom (picture row y) */
static inline float fx_sky_fade(uint32_t y)
{
    float f = ((float)y + 0.5f) * (1.0f / FX_SKY_H);
    return f < 0.75f ? 1.0f - 0.34f * f : 0.745f - (f - 0.75f) * 1.26f;
}

/* The cloud value of four samples at picture coordinates X, Y (pixel y's
 * centre is y): a warp, then five octaves of value noise, 0 to 1 (about
 * normal, mean 0.5), the fine ones heavier than a plain 1/2 falloff so
 * that the edges have detail at a few picture pixels; *hf is the two finest
 * octaves alone (0 to 1), the texture inside a cloud. Six table reads per
 * lane. (Until 2026-10-14 three octaves of 0.55, 0.30 and 0.15: soft blobs
 * on the TV.) */
static inline fxv fx_cloud_n(const fx_sky_k *k, const uint32_t *tab, fxv X, fxv Y, fxv *hf)
{
    fxv w = fx_sub(fx_noise(tab, fx_madd(X, FXV(k->ws), FXV(k->wx)), fx_madd(Y, FXV(k->ws), FXV(k->wy))), FXV(0.5f));
    fxv u = fx_madd(w, FXV(0.7f), fx_madd(X, FXV(k->sc), FXV(k->ox)));
    fxv v = fx_madd(w, FXV(-0.45f), fx_madd(Y, FXV(k->sc), FXV(k->oy)));
    fxv n1 = fx_noise(tab, u, v);
    fxv n2 = fx_noise(tab, fx_madd(u, FXV(2.03f), FXV(19.7f)), fx_madd(v, FXV(2.03f), FXV(7.3f)));
    fxv n3 = fx_noise(tab, fx_madd(u, FXV(4.11f), FXV(3.1f)), fx_madd(v, FXV(4.11f), FXV(23.9f)));
    fxv n4 = fx_noise(tab, fx_madd(u, FXV(8.37f), FXV(41.3f)), fx_madd(v, FXV(8.37f), FXV(11.7f)));
    fxv n5 = fx_noise(tab, fx_madd(u, FXV(17.1f), FXV(29.9f)), fx_madd(v, FXV(17.1f), FXV(53.1f)));
    *hf = fx_madd(n4, FXV(FX_CLOUD_W4 / (FX_CLOUD_W4 + FX_CLOUD_W5)), fx_mul(n5, FXV(FX_CLOUD_W5 / (FX_CLOUD_W4 + FX_CLOUD_W5))));
    fxv n = fx_madd(n1, FXV(FX_CLOUD_W1), fx_madd(n2, FXV(FX_CLOUD_W2), fx_mul(n3, FXV(FX_CLOUD_W3))));
    return fx_madd(n4, FXV(FX_CLOUD_W4), fx_madd(n5, FXV(FX_CLOUD_W5), n));
}

/* Four cloud samples: the colour (0 to 255, also where the alpha is 0) and
 * the alpha (0 to 255, not dithered). The density d is a smooth step over
 * the band (FX_CLOUD_BAND wide: a crisp edge); the light falls with the
 * thickness (the value past the band's start over FX_CLOUD_THICK), rises
 * near the sun and in a flash, and the finest octaves shade the inside
 * (FX_CLOUD_TEX: billows, not a flat fill); thin cloud takes the edge
 * colour. */
static inline void fx_cloud4(const fx_sky_k *k, const uint32_t *tab, fxv X, fxv Y, float fade, fxv rgb[3], fxv *a)
{
    fxv hf;
    fxv n = fx_cloud_n(k, tab, X, Y, &hf);
    fxv p = fx_sub(n, FXV(k->lo));
    fxv t = fx_clamp01(fx_mul(p, FXV(k->iw)));
    fxv d = fx_mul(fx_mul(t, t), fx_nmsub(FXV(2.0f), t, FXV(3.0f)));
    *a = fx_mul(d, FXV(k->amax * fade));
    fxv th = fx_clamp01(fx_mul(p, FXV(1.0f / FX_CLOUD_THICK)));
    fxv dx = fx_sub(X, FXV(k->sun_x)), dy = fx_sub(Y, FXV(k->sun_y));
    fxv sun = fx_max(fx_nmsub(fx_madd(dx, dx, fx_mul(dy, dy)), FXV(k->sun_k), FXV(1.0f)), FXV(0.0f));
    fxv light = fx_nmsub(th, FXV(0.55f), FXV(1.0f + k->flash));
    light = fx_madd(fx_sub(hf, FXV(0.5f)), FXV(FX_CLOUD_TEX), light);
    light = fx_madd(sun, FXV(k->sun_gain), light);
    fxv e = fx_nmsub(d, FXV(k->edge_amt), FXV(k->edge_amt));             /* edge_amt (1 - d) */
    for (int c = 0; c < 3; c++) {
        fxv col = fx_madd(FXV(k->lit[c] - k->shade[c]), light, FXV(k->shade[c]));
        rgb[c] = fx_mix(col, FXV(k->edge[c]), e);
    }
}

/* The 4 x 4 ordered dither: alpha under 0x30 goes to a multiple of 0x10
 * (0, 0x10, 0x20 or 0x30) whose mean over the 4 x 4 cell is the alpha, so
 * the RSX's alpha test at 0x10 drops no soft edge as one step. t is the
 * row's thresholds for the lanes x & 3 = 0 to 3. */
static inline fxu fx_dither(fxu a, fxu t)
{
    fxu d = fxu_shl(fxu_shr(fxu_add(a, t), 4), 4);
    return fxu_sel(a, d, fxu_gt(FXU(0x30), a));
}

static inline fxu fx_bayer_row(uint32_t y)
{
    static const uint32_t b[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    return fxu4(b[y & 3][0], b[y & 3][1], b[y & 3][2], b[y & 3][3]);
}

static inline fxu fx_argb(fxu a, fxu r, fxu g, fxu b)
{
    return fxu_or(fxu_or(fxu_shl(a, 24), fxu_shl(r, 16)), fxu_or(fxu_shl(g, 8), b));
}

/* cloud row y (FX_SKY_W words of A8R8G8B8) into out; its word sum */
static inline uint32_t fx_cloud_row(const fx_sky_k *k, const uint32_t *tab, uint32_t y, uint32_t *out)
{
    fxv X = fxv4(0.0f, 1.0f, 2.0f, 3.0f), Y = FXV((float)y);
    float fade = fx_sky_fade(y);
    fxu t = fx_bayer_row(y), sum = FXU(0);
    for (int x = 0; x < FX_SKY_W; x += 4) {
        fxv rgb[3], a;
        fx_cloud4(k, tab, X, Y, fade, rgb, &a);
        fxu w = fx_argb(fx_dither(fx_byte(a), t), fx_byte(rgb[0]), fx_byte(rgb[1]), fx_byte(rgb[2]));
        *(fxu *)(out + x) = w;
        sum = fxu_add(sum, w);
        X = fx_add(X, FXV(4.0f));
    }
    return fxu_lane(sum, 0) + fxu_lane(sum, 1) + fxu_lane(sum, 2) + fxu_lane(sum, 3);
}

/* The glass. A row is FX_GLASS_W pixels as 60 vectors per channel, with one
 * vector of padding at each end: [0] and [61]. */
#define FX_GV (FX_GLASS_W / 4)
#define FX_GROW (FX_GV + 2)

/* the sky gradient at glass row gy (the RSX's three rect_v bands: 0 to 55 %,
 * 55 to 80 %, 80 to 100 %) */
static inline void fx_sky_at(const fx_sky_k *k, uint32_t gy, float c[3])
{
    float f = ((float)gy + 0.5f) * (1.0f / FX_GLASS_H);
    int s = f < 0.55f ? 0 : f < 0.8f ? 1 : 2;
    static const float y0[3] = {0.0f, 0.55f, 0.8f}, inv[3] = {1.0f / 0.55f, 1.0f / 0.25f, 1.0f / 0.2f};
    float t = (f - y0[s]) * inv[s];
    for (int i = 0; i < 3; i++) c[i] = k->sky[s][i] + (k->sky[s + 1][i] - k->sky[s][i]) * t;
}

/* The glass source of row gy: the sky and the clouds composited, sampled
 * at the centre of each glass pixel (picture x = 2 gx + 0.5), each channel
 * an integer 0 to 255, into h[c][1..60]. */
static inline void fx_glass_src(const fx_sky_k *k, const uint32_t *tab, uint32_t gy, fxu h[3][FX_GROW])
{
    float s[3];
    fx_sky_at(k, gy, s);
    float fade = fx_sky_fade(2 * gy + 1);
    fxv X = fxv4(0.5f, 2.5f, 4.5f, 6.5f), Y = FXV(2.0f * (float)gy + 0.5f);
    for (int i = 1; i <= FX_GV; i++) {
        fxv rgb[3], a;
        fx_cloud4(k, tab, X, Y, fade, rgb, &a);
        fxv t = fx_mul(a, FXV(1.0f / 255.0f));
        for (int c = 0; c < 3; c++) h[c][i] = fx_byte(fx_mix(FXV(s[c]), rgb[c], t));
        X = fx_add(X, FXV(8.0f));
    }
}

/* One horizontal box pass of radius 3, raw sums (no division), from a to b:
 * the pads repeat the edge pixel (the picture's edge: a row is always
 * whole). */
static inline void fx_hbox(fxu *a, fxu *b)
{
    a[0] = FXU(fxu_lane(a[1], 0));
    a[FX_GV + 1] = FXU(fxu_lane(a[FX_GV], 3));
    for (int i = 1; i <= FX_GV; i++) {
        fxu p = a[i - 1], c = a[i], n = a[i + 1];
        fxu s = fxu_add(fxu_add(fxu_cat(p, c, 1), fxu_cat(p, c, 2)), fxu_add(fxu_cat(p, c, 3), c));
        b[i] = fxu_add(s, fxu_add(fxu_add(fxu_cat(c, n, 1), fxu_cat(c, n, 2)), fxu_cat(c, n, 3)));
    }
}

/* x / 343 rounded, for 0 <= x < 2^24: 343 is odd, so x / 343 is never a
 * half, and the float product is far from one (the same on the SPU) */
static inline fxu fx_div343(fxu x) { return fx_tou(fx_madd(fx_tof(x), FXV(1.0f / 343.0f), FXV(0.5f))); }

/* The three horizontal passes of row h (h[0] in, h[1] work), divided by 343
 * and rounded, into row r of the band (planes of cap rows). */
static inline void fx_glass_hrow(fxu h[2][3][FX_GROW], uint16_t *band, uint32_t cap, uint32_t r)
{
    for (int c = 0; c < 3; c++) {
        fx_hbox(h[0][c], h[1][c]);
        fx_hbox(h[1][c], h[0][c]);
        fx_hbox(h[0][c], h[1][c]);
        uint16_t *p = band + ((uint32_t)c * cap + r) * FX_GLASS_W;
        for (int i = 1; i <= FX_GV; i += 2) fxu_st16(p + 4 * (i - 1), fx_div343(h[1][c][i]), fx_div343(h[1][c][i + 1]));
    }
}

/* One vertical box pass of radius 3 over n rows of 8 columns at p (rows of
 * FX_GLASS_W words), in place: raw sums, or divided by 343 when last. Rows
 * outside [0, n) repeat the edge row: the band's own edge, which is the
 * picture's at its top and bottom, and else the margin's far end, whose
 * error reaches 9 rows in three passes and so never the band. */
static inline void fx_vbox(uint16_t *p, uint32_t n, int last)
{
    fxu ring[8][2], slo = FXU(0), shi = FXU(0);
    for (int j = -3; j <= 3; j++) {
        uint32_t r = j < 0 ? 0 : (uint32_t)j >= n ? n - 1 : (uint32_t)j;
        fxu_ld16(p + r * FX_GLASS_W, &ring[(j + 8) & 7][0], &ring[(j + 8) & 7][1]);
        slo = fxu_add(slo, ring[(j + 8) & 7][0]);
        shi = fxu_add(shi, ring[(j + 8) & 7][1]);
    }
    for (uint32_t y = 0; y < n; y++) {
        uint32_t r = y + 4 < n ? y + 4 : n - 1;
        fxu nlo, nhi;
        fxu_ld16(p + r * FX_GLASS_W, &nlo, &nhi);          /* before row y is written */
        if (last) fxu_st16(p + y * FX_GLASS_W, fx_div343(slo), fx_div343(shi));
        else fxu_st16(p + y * FX_GLASS_W, slo, shi);
        uint32_t o = (y + 5) & 7;                          /* row y - 3 */
        slo = fxu_add(fxu_sub(slo, ring[o][0]), nlo);
        shi = fxu_add(fxu_sub(shi, ring[o][1]), nhi);
        ring[(y + 4) & 7][0] = nlo;
        ring[(y + 4) & 7][1] = nhi;
    }
}

/* the three vertical passes over the band's n rows, every channel */
static inline void fx_glass_vblur(uint16_t *band, uint32_t cap, uint32_t n)
{
    for (uint32_t c = 0; c < 3; c++)
        for (uint32_t x = 0; x < FX_GLASS_W; x += 8) {
            uint16_t *p = band + c * cap * FX_GLASS_W + x;
            fx_vbox(p, n, 0);
            fx_vbox(p, n, 0);
            fx_vbox(p, n, 1);
        }
}

/* band row r as FX_GLASS_W opaque A8R8G8B8 words; their word sum */
static inline uint32_t fx_glass_out(const uint16_t *band, uint32_t cap, uint32_t r, uint32_t *out)
{
    fxu sum = FXU(0);
    for (int x = 0; x < FX_GLASS_W; x += 8) {
        fxu c[3][2];
        for (uint32_t i = 0; i < 3; i++) fxu_ld16(band + (i * cap + r) * FX_GLASS_W + x, &c[i][0], &c[i][1]);
        for (int j = 0; j < 2; j++) {
            fxu w = fx_argb(FXU(255), c[0][j], c[1][j], c[2][j]);
            *(fxu *)(out + x + 4 * j) = w;
            sum = fxu_add(sum, w);
        }
    }
    return fxu_lane(sum, 0) + fxu_lane(sum, 1) + fxu_lane(sum, 2) + fxu_lane(sum, 3);
}

/* the sky job's buffers (band: 3 planes of cap rows, apart) */
typedef struct {
    uint32_t rows[2][FX_SKY_W];              /* the cloud rows; also two glass rows */
    fxu h[2][3][FX_GROW];                    /* the horizontal passes */
} __attribute__((aligned(128))) fx_sky_ws;

/* A sky job: the cloud rows [y0, y1) and the glass rows [y0 / 2, y1 / 2)
 * from the glass source of those rows and FX_GLASS_MARGIN rows on each
 * side. 0, or FX_EXIT_CTL with nothing written. */
static inline uint32_t fx_sky_job(const fx_ctl *c, const uint32_t *tab, fx_sky_ws *ws, uint16_t *band, uint32_t cap,
                                  fx_io *io, fx_rec *rec)
{
    uint32_t y0 = c->y0, y1 = c->y1;
    if (y0 >= y1 || y1 > FX_SKY_H || (y0 & 1) || (y1 & 1)) return FX_EXIT_CTL;
    uint32_t g0 = y0 / 2, g1 = y1 / 2;
    uint32_t r0 = g0 > FX_GLASS_MARGIN ? g0 - FX_GLASS_MARGIN : 0;
    uint32_t r1 = g1 + FX_GLASS_MARGIN < FX_GLASS_H ? g1 + FX_GLASS_MARGIN : FX_GLASS_H;
    if (r1 - r0 > cap) return FX_EXIT_CTL;
    fx_sky_k k;
    fx_sky_prep(c, &k);
    rec->check_row = FX_NO_CHECK;
    rec->sum = rec->sum2 = 0;
    uint32_t n = 0;
    for (uint32_t y = y0; y < y1; y++, n++) {
        uint32_t b = n & 1;
        if (n >= 2) io->wait(io, b);
        uint32_t s = fx_cloud_row(&k, tab, y, ws->rows[b]);
        if (y == c->check_row) {
            rec->check_row = y;
            rec->sum = s;
        }
        io->put(io, 0, y * FX_SKY_PITCH, ws->rows[b], FX_SKY_PITCH, b);
    }
    for (uint32_t gy = r0; gy < r1; gy++) {
        fx_glass_src(&k, tab, gy, ws->h[0]);
        fx_glass_hrow(ws->h, band, cap, gy - r0);
    }
    fx_glass_vblur(band, cap, r1 - r0);
    for (uint32_t gy = g0; gy < g1; gy++, n++) {
        uint32_t b = n & 1;
        if (n >= 2) io->wait(io, b);
        uint32_t s = fx_glass_out(band, cap, gy - r0, ws->rows[b]);
        if (c->check_row != FX_NO_CHECK && gy == c->check_row / 2) rec->sum2 = s;
        io->put(io, 1, gy * FX_GLASS_PITCH, ws->rows[b], FX_GLASS_PITCH, b);
    }
    io->wait(io, 0);
    io->wait(io, 1);
    return 0;
}

/* ---- FX_AURORA ------------------------------------------------------------------ */

#define FX_AURORA_GAIN 0.80f                 /* the curtains at gain 1: the mean brightness stays at most 20 % (the test) */

/* per column: each curtain's base row and its brightness */
typedef struct {
    float base[3][FX_AURORA_W];
    float amp[3][FX_AURORA_W];
} __attribute__((aligned(128))) fx_aur_cols;

/* The three curtains along x for the job's time: the base height from two
 * octaves of 1D noise (one fold in about 20 s), fine rays (high-frequency
 * noise) and a slow brightness along the curtain. */
static inline void fx_aurora_prep(const fx_ctl *c, const uint32_t *tab, fx_aur_cols *col)
{
    float g = fx_clampf(c->p.aurora.gain, 0.0f, 1.0f) * FX_AURORA_GAIN;
    float t = c->time;
    uint32_t h = fx_hash(c->seed);
    for (uint32_t i = 0; i < 3; i++) {
        float fi = (float)i, so = (float)((h >> (8 * i)) & 255) * (FX_NOISE_N / 256.0f);
        float o1 = fx_wrap(t * (0.05f + 0.01f * fi) + so, FX_NOISE_N) + FX_NOISE_N;
        float o2 = fx_wrap(t * 0.09f + so + 11.0f, FX_NOISE_N) + FX_NOISE_N;
        float o3 = fx_wrap(t * 0.6f + so * 3.0f, FX_NOISE_N) + FX_NOISE_N;
        float o4 = fx_wrap(t * 0.04f + so + 29.0f, FX_NOISE_N) + FX_NOISE_N;
        float f1 = (1.0f + 0.3f * fi) / 150.0f, gain = (0.7f - 0.2f * fi) * g;
        float base = (0.50f + 0.12f * fi) * FX_AURORA_H;
        fxv X = fxv4(0.0f, 1.0f, 2.0f, 3.0f);
        for (int x = 0; x < FX_AURORA_W; x += 4) {
            fxv u = fx_madd(X, FXV(f1), FXV(o1));
            fxv h1 = fx_noise1(tab, 3 * i, u), h2 = fx_noise1(tab, 3 * i + 1, fx_madd(u, FXV(2.3f), FXV(o2 - o1)));
            fxv b = fx_madd(fx_sub(h1, FXV(0.5f)), FXV(0.16f * FX_AURORA_H), FXV(base));
            b = fx_madd(fx_sub(h2, FXV(0.5f)), FXV(0.06f * FX_AURORA_H), b);
            fxv r = fx_noise1(tab, 3 * i + 2, fx_madd(X, FXV(1.0f / 11.0f), FXV(o3)));
            fxv f = fx_noise1(tab, 40 + i, fx_madd(X, FXV(1.0f / 90.0f), FXV(o4)));
            fxv a = fx_mul(fx_madd(r, FXV(0.75f), FXV(0.25f)), fx_madd(f, FXV(0.5f), FXV(0.5f)));
            *(fxv *)(col->base[i] + x) = b;
            *(fxv *)(col->amp[i] + x) = fx_mul(a, FXV(gain));
            X = fx_add(X, FXV(4.0f));
        }
    }
}

/* a pixel's star value from its place: Jenkins' one-at-a-time mix in
 * shifts and adds (no 32-bit multiply on the SPU) */
static inline fxu fx_star_hash(fxu x, uint32_t y, uint32_t s)
{
    fxu h = fxu_xor(fxu_or(x, FXU(y << 12)), FXU(s));
    h = fxu_add(h, fxu_shl(h, 10));
    h = fxu_xor(h, fxu_shr(h, 6));
    h = fxu_add(h, fxu_shl(h, 3));
    h = fxu_xor(h, fxu_shr(h, 11));
    h = fxu_add(h, fxu_shl(h, 15));
    h = fxu_xor(h, fxu_shr(h, 13));
    h = fxu_add(h, fxu_shl(h, 9));
    return fxu_xor(h, fxu_shr(h, 16));
}

#define FX_STAR_T 4772185u                   /* 2^32 / 900: one pixel in 900 is a star */

/* aurora row y (FX_AURORA_W opaque words) into out; its word sum */
static inline uint32_t fx_aurora_row(const fx_ctl *c, const fx_aur_cols *col, uint32_t y, uint32_t *out)
{
    static const float G[3] = {0x3c, 0xf0, 0xa0}, V[3] = {0x8a, 0x5c, 0xff};   /* AURORA_G, AURORA_V */
    float f = ((float)y + 0.5f) * (1.0f / FX_AURORA_H), sky[3];
    for (int i = 0; i < 3; i++)
        sky[i] = fx_ch(c->p.aurora.top, i) + (fx_ch(c->p.aurora.bottom, i) - fx_ch(c->p.aurora.top, i)) * f;
    float tw = fx_wrap(c->time * 0.8f, 1.0f);
    uint32_t s = fx_hash(c->seed ^ 0xa5a5a5a5u);
    fxv Y = FXV((float)y);
    fxu X = fxu4(0, 1, 2, 3), sum = FXU(0);
    for (int x = 0; x < FX_AURORA_W; x += 4) {
        fxv rgb[3] = {FXV(sky[0]), FXV(sky[1]), FXV(sky[2])};
        for (int i = 0; i < 3; i++) {
            fxv d = fx_sub(*(const fxv *)(col->base[i] + x), Y);              /* > 0: above the base */
            fxu up = fx_gt(d, FXV(0.0f));
            fxv e = fx_sel(fx_mul(d, FXV(-0.577f)), fx_mul(d, FXV(0.030826f)), up);   /* e^(-d / 46.8) above, 2^(d / 1.73) under */
            fxv b = fx_mul(*(const fxv *)(col->amp[i] + x), fx_exp2n(e));
            fxv hue = fx_clamp01(fx_mul(d, FXV(1.0f / 90.0f)));
            for (int ch = 0; ch < 3; ch++) rgb[ch] = fx_madd(fx_madd(FXV(V[ch] - G[ch]), hue, FXV(G[ch])), b, rgb[ch]);
        }
        fxu h = fx_star_hash(X, y, s);
        fxu star = fxu_gt(FXU(FX_STAR_T), h);
        fxv ph = fx_mul(fx_tof(fxu_and(h, FXU(255))), FXV(1.0f / 256.0f));
        fxv fr = fx_add(ph, FXV(tw));
        fr = fx_sel(fr, fx_sub(fr, FXV(1.0f)), fx_gt(fr, FXV(1.0f)));
        fxv tri = fx_madd(fr, FXV(2.0f), FXV(-1.0f));
        tri = fx_max(tri, fx_sub(FXV(0.0f), tri));                          /* |2 fr - 1| */
        fxv sb = fx_sel(FXV(0.0f), fx_madd(tri, FXV(110.0f), FXV(70.0f)), star);
        fxu w = fx_argb(FXU(255), fx_byte(fx_madd(sb, FXV(0.90f), rgb[0])), fx_byte(fx_madd(sb, FXV(0.93f), rgb[1])),
                        fx_byte(fx_add(sb, rgb[2])));
        *(fxu *)(out + x) = w;
        sum = fxu_add(sum, w);
        X = fxu_add(X, FXU(4));
    }
    return fxu_lane(sum, 0) + fxu_lane(sum, 1) + fxu_lane(sum, 2) + fxu_lane(sum, 3);
}

typedef struct {
    fx_aur_cols cols;
    uint32_t rows[2][FX_AURORA_W];
} __attribute__((aligned(128))) fx_aur_ws;

/* An aurora job: the rows [y0, y1). 0, or FX_EXIT_CTL with nothing written. */
static inline uint32_t fx_aurora_job(const fx_ctl *c, const uint32_t *tab, fx_aur_ws *ws, fx_io *io, fx_rec *rec)
{
    if (c->y0 >= c->y1 || c->y1 > FX_AURORA_H) return FX_EXIT_CTL;
    fx_aurora_prep(c, tab, &ws->cols);
    rec->check_row = FX_NO_CHECK;
    rec->sum = rec->sum2 = 0;
    uint32_t n = 0;
    for (uint32_t y = c->y0; y < c->y1; y++, n++) {
        uint32_t b = n & 1;
        if (n >= 2) io->wait(io, b);
        uint32_t s = fx_aurora_row(c, &ws->cols, y, ws->rows[b]);
        if (y == c->check_row) {
            rec->check_row = y;
            rec->sum = s;
        }
        io->put(io, 0, y * FX_AURORA_PITCH, ws->rows[b], FX_AURORA_PITCH, b);
    }
    io->wait(io, 0);
    io->wait(io, 1);
    return 0;
}

/* ---- FX_RAIN ---------------------------------------------------------------------- */

/* A vertex as ps3gfx's gfx_vtx (20 bytes): x, y, the colour 0xRRGGBBAA, u, v */
typedef struct {
    float x, y;
    uint32_t rgba;
    float u, v;
} fx_vtx;

_Static_assert(sizeof(fx_vtx) == 20, "fx_vtx is gfx_vtx");
_Static_assert(FX_RAIN_CHUNK * 4 * sizeof(fx_vtx) <= 16384 && FX_RAIN_CHUNK * 4 * sizeof(fx_vtx) % 128 == 0,
               "a chunk is one DMA and keeps the 128-byte alignment");

/* A particle (16 bytes). Drop: the head x, y (screen pixels), the fall
 * speed, the alpha 0 to 1. Flake: the centre x without the sway, y, the
 * fall speed, the sway's phase 0 to 1. The layer (far, middle, near) is
 * the index mod 3. */
typedef struct { float x, y, vy, k; } fx_drop;
typedef struct { float x, y; uint32_t age, live; } fx_splash;

typedef struct {
    fx_drop d[FX_RAIN_MAX];
    fx_splash s[FX_SPLASHES];
    uint32_t rng, live, kind, seed, next, ready;
    uint32_t pad[2];
} __attribute__((aligned(128))) fx_rain_state;

#define FX_STREAK 0.018f                     /* a streak is the velocity times 18 ms */
#define FX_SCREEN_W 1920.0f
#define FX_SCREEN_H 1080.0f

/* a new particle i: at the top, or anywhere on the screen (first) */
static inline void fx_spawn(fx_rain_state *st, uint32_t i, float fall, int first)
{
    static const float sp[2][3] = {{0.55f, 0.75f, 1.0f}, {0.6f, 0.8f, 1.0f}};
    static const float al[3] = {0x30 / 255.0f, 0x60 / 255.0f, 0x90 / 255.0f};
    fx_drop *p = &st->d[i];
    uint32_t layer = i % 3, snow = st->kind == FX_FLAKES;
    p->x = -FX_RAIN_MARGIN + fx_rnd(&st->rng) * (FX_SCREEN_W + 2 * FX_RAIN_MARGIN);
    p->vy = fall * sp[snow][layer] * (0.9f + 0.2f * fx_rnd(&st->rng));
    float r = fx_rnd(&st->rng);
    p->y = first ? r * (FX_SCREEN_H + 120.0f) - 120.0f : -r * 120.0f;
    p->k = snow ? fx_rnd(&st->rng) : al[layer] * (0.85f + 0.3f * fx_rnd(&st->rng));
}

/* x back into [-margin, screen + margin) when the wind took it out */
static inline float fx_xwrap(float x)
{
    const float lo = -FX_RAIN_MARGIN, span = FX_SCREEN_W + 2 * FX_RAIN_MARGIN;
    if (x < lo) x += span;
    else if (x >= lo + span) x -= span;
    return x;
}

/* the word sum of n vertices (each float as its bits) */
static inline uint32_t fx_vsum(const fx_vtx *v, uint32_t n)
{
    union { float f; uint32_t u; } x, y, a, b;
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) {
        x.f = v[i].x;
        y.f = v[i].y;
        a.f = v[i].u;
        b.f = v[i].v;
        s += x.u + y.u + v[i].rgba + a.u + b.u;
    }
    return s;
}

static inline fx_vtx *fx_quad(fx_vtx *q, const float *x, const float *y, const uint32_t *rgba, const float *u,
                              const float *v)
{
    for (int j = 0; j < 4; j++) {
        q[j].x = x[j];
        q[j].y = y[j];
        q[j].rgba = rgba[j];
        q[j].u = u[j];
        q[j].v = v[j];
    }
    return q + 4;
}

/* the rain job's numbers */
typedef struct {
    float dt, wind, fall, ox, oy, alpha, time;
    uint32_t rgb;
} fx_rain_k;

/* Drop i one step, then its quad: a parallelogram from the tail (alpha 0)
 * to the head, its width horizontal (1, 1.5 or 2 px by layer). A drop whose
 * tail passed the bottom comes back at the top; a near one may leave a
 * splash. */
static inline fx_vtx *fx_drop_step(fx_rain_state *st, const fx_rain_k *k, uint32_t i, fx_vtx *q)
{
    static const float hw[3] = {0.5f, 0.75f, 1.0f};
    fx_drop *p = &st->d[i];
    p->y += p->vy * k->dt;
    p->x = fx_xwrap(p->x + k->wind * k->dt);
    float tx = p->x - k->wind * FX_STREAK, ty = p->y - p->vy * FX_STREAK;
    if (ty > FX_SCREEN_H) {
        if (i % 3 == 2 && fx_rnd(&st->rng) < 0.34f) {
            fx_splash *s = &st->s[st->next++ % FX_SPLASHES];
            s->x = p->x;
            s->y = FX_SCREEN_H - 6.0f - 28.0f * fx_rnd(&st->rng);
            s->age = 0;
            s->live = 1;
        }
        fx_spawn(st, i, k->fall, 0);
        tx = p->x - k->wind * FX_STREAK;
        ty = p->y - p->vy * FX_STREAK;
    }
    float w = hw[i % 3];
    uint32_t a = (uint32_t)(fx_clampf(p->k * k->alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    const float x[4] = {k->ox + tx - w, k->ox + tx + w, k->ox + p->x + w, k->ox + p->x - w};
    const float y[4] = {k->oy + ty, k->oy + ty, k->oy + p->y, k->oy + p->y};
    const uint32_t c[4] = {k->rgb << 8, k->rgb << 8, k->rgb << 8 | a, k->rgb << 8 | a};
    const float z[4] = {0, 0, 0, 0};
    return fx_quad(q, x, y, c, z, z);
}

/* Flake i one step, then its quad: a square of 4, 6 or 9 px by layer for
 * the glow texture, swaying 20 px at 0.5 Hz. */
static inline fx_vtx *fx_flake_step(fx_rain_state *st, const fx_rain_k *k, uint32_t i, fx_vtx *q)
{
    static const float sz[3] = {2.0f, 3.0f, 4.5f};
    static const float al[3] = {0x70 / 255.0f, 0xa0 / 255.0f, 0xd0 / 255.0f};
    fx_drop *p = &st->d[i];
    float h = sz[i % 3];
    p->y += p->vy * k->dt;
    p->x = fx_xwrap(p->x + k->wind * 0.35f * k->dt);
    if (p->y - h > FX_SCREEN_H) fx_spawn(st, i, k->fall, 0);
    float cx = k->ox + p->x + 20.0f * fx_sin2pi(0.5f * k->time + p->k), cy = k->oy + p->y;
    uint32_t a = (uint32_t)(fx_clampf(al[i % 3] * k->alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    const float x[4] = {cx - h, cx + h, cx + h, cx - h}, y[4] = {cy - h, cy - h, cy + h, cy + h};
    const uint32_t c[4] = {k->rgb << 8 | a, k->rgb << 8 | a, k->rgb << 8 | a, k->rgb << 8 | a};
    const float u[4] = {0, 1, 1, 0}, v[4] = {0, 0, 1, 1};
    return fx_quad(q, x, y, c, u, v);
}

/* splash i: a flat arc that widens (12 to 22 px) and fades over 6 frames;
 * a free slot is 4 points at the screen's corner with alpha 0 */
static inline fx_vtx *fx_splash_quad(const fx_rain_state *st, const fx_rain_k *k, uint32_t i, fx_vtx *q)
{
    const fx_splash *s = &st->s[i];
    const float z[4] = {0, 0, 0, 0};
    if (!s->live) {
        const float x[4] = {k->ox, k->ox, k->ox, k->ox}, y[4] = {k->oy, k->oy, k->oy, k->oy};
        const uint32_t c[4] = {0, 0, 0, 0};
        return fx_quad(q, x, y, c, z, z);
    }
    float w = 6.0f + 2.0f * (float)s->age, cx = k->ox + s->x, cy = k->oy + s->y;
    uint32_t a = (uint32_t)(fx_clampf(0x60 / 255.0f * (1.0f - (float)s->age / 6.0f) * k->alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    const float x[4] = {cx - w, cx + w, cx + w - 2.0f, cx - w + 2.0f}, y[4] = {cy, cy, cy - 2.0f, cy - 2.0f};
    const uint32_t c[4] = {k->rgb << 8 | a, k->rgb << 8 | a, k->rgb << 8, k->rgb << 8};
    return fx_quad(q, x, y, c, z, z);
}

/* A rain job: move every particle by dt, then the quads (the drops, then
 * the FX_SPLASHES splashes; or the flakes) in chunks of FX_RAIN_CHUNK
 * through the two buffers q. The particles stay in st. 0, or FX_EXIT_CTL
 * with nothing written. */
static inline uint32_t fx_rain_job(const fx_ctl *c, fx_rain_state *st, fx_vtx (*q)[FX_RAIN_CHUNK * 4], fx_io *io,
                                   fx_rec *rec)
{
    const uint32_t kind = c->p.rain.kind, count = c->p.rain.count;
    fx_rain_k k;
    k.fall = c->p.rain.fall;
    if (kind > FX_FLAKES || count > (kind == FX_FLAKES ? FX_SNOW_MAX : FX_RAIN_MAX) || !(k.fall > 0.0f) || k.fall > 4000.0f)
        return FX_EXIT_CTL;
    k.dt = fx_clampf(c->p.rain.dt, 0.0f, 0.1f);
    k.wind = fx_clampf(c->p.rain.wind, -0.7f * k.fall, 0.7f * k.fall);
    k.ox = c->p.rain.ox;
    k.oy = c->p.rain.oy;
    k.alpha = fx_clampf(c->p.rain.alpha, 0.0f, 1.0f);
    k.time = c->time;
    k.rgb = c->p.rain.rgb & 0xffffff;
    if (c->p.rain.reset || !st->ready || st->kind != kind || st->seed != c->seed) {
        st->kind = kind;
        st->seed = c->seed;
        st->rng = fx_hash(c->seed) | 1;
        st->live = 0;
        st->next = 0;
        for (uint32_t i = 0; i < FX_SPLASHES; i++) st->s[i].live = 0;
        st->ready = 1;
    }
    for (; st->live < count; st->live++) fx_spawn(st, st->live, k.fall, 1);
    for (uint32_t i = 0; i < FX_SPLASHES; i++)
        if (st->s[i].live && ++st->s[i].age >= 6) st->s[i].live = 0;
    uint32_t total = fx_rain_quads(count, kind), sum = 0;
    for (uint32_t done = 0, n = 0; done < total; n++) {
        uint32_t b = n & 1, m = total - done < FX_RAIN_CHUNK ? total - done : FX_RAIN_CHUNK;
        if (n >= 2) io->wait(io, b);
        fx_vtx *v = q[b];
        for (uint32_t j = done; j < done + m; j++) {
            if (j < count) v = kind == FX_FLAKES ? fx_flake_step(st, &k, j, v) : fx_drop_step(st, &k, j, v);
            else v = fx_splash_quad(st, &k, j - count, v);
        }
        sum += fx_vsum(q[b], 4 * m);
        io->put(io, 0, done * 80, q[b], m * 80, b);
        done += m;
    }
    io->wait(io, 0);
    io->wait(io, 1);
    rec->sum = sum;
    rec->sum2 = 0;
    rec->quads = total;
    rec->check_row = FX_NO_CHECK;
    return 0;
}

/* ---- the known-answer test --------------------------------------------------------- */

/* The vector helpers whose SPU form is a shuffle, a gather or a conversion,
 * on known integers (exact on any float unit): FNV-1a of the results. The
 * SPU program reports it as kat; the host test checks it is FX_KAT. */
static inline uint32_t fx_kat(void)
{
    uint32_t t[64] __attribute__((aligned(16)));
    uint16_t s[16] __attribute__((aligned(16)));
    uint32_t h = 0x811c9dc5u;
    for (uint32_t i = 0; i < 64; i++) t[i] = fx_hash(i);
    fxu a = fxu4(3, 17, 40, 63), b = fxu4(0x12345678u, 0x9abcdef0u, 0xffffffffu, 7);
    fxu r[12];
    r[0] = fxu_gather(t, a);
    r[1] = fxu_cat(a, b, 1);
    r[2] = fxu_cat(a, b, 2);
    r[3] = fxu_cat(a, b, 3);
    fxu_st16(s, fxu4(1, 0xfffe, 0x8000, 0x1234), fxu4(0x7fff, 2, 0xabcd, 0));
    fxu_ld16(s, &r[4], &r[5]);
    r[6] = fx_tou(fx_tof(fxu4(0, 255, 87465, 16777215)));
    r[7] = fx_div343(fxu4(0, 171, 87465, 343 * 200 + 172));
    r[8] = fxu_gt(a, fxu4(3, 16, 41, 0x80000000u));
    r[9] = fx_dither(fxu4(0x2f, 0x0f, 0x30, 0x01), fx_bayer_row(1));
    r[10] = fxu_shr(fxu_shl(b, 7), 9);
    r[11] = fx_bits(fx_exp2n(fxv4(0.0f, 1.0f, 3.0f, 30.0f)));   /* whole e: the fraction is 0, the result exact */
    for (int i = 0; i < 12; i++)
        for (int j = 0; j < 4; j++) {
            uint32_t w = fxu_lane(r[i], j);
            for (int k = 0; k < 4; k++) h = (h ^ ((w >> (8 * k)) & 255)) * 0x01000193u;
        }
    return h;
}

#endif
