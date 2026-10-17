/* Drawing helpers on the public gfx API (fx.h). An archive member of its
 * own: an app that calls none of them links none of this. The
 * anti-aliased helpers (aa_*) were added for Couch 0.3.0's console round
 * of 2026-10-14 ("some lines are rough"). */
#include <math.h>
#include <stddef.h>
#include "gfx.h"
#include "ui.h"
#include "ui_kit.h"
#include "fx.h"

#define PI_F 3.14159265f
#define PTS_MAX (GFX_PRIM_MAX / 2)           /* the points of one strip primitive */

void tex_sub(const gfx_tex *t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 tint)
{
    gfx_texture(t);
    gfx_vtx *q = gfx_prim(GFX_QUADS, 4);
    q = gfx_put(q, x, y, tint, u0, v0);
    q = gfx_put(q, x + w, y, tint, u1, v0);
    q = gfx_put(q, x + w, y + h, tint, u1, v1);
    gfx_put(q, x, y + h, tint, u0, v1);
    gfx_texture(NULL);
}

/* One point of tex_round's outline, its texture coordinate from its place
 * in the rect. */
static inline gfx_vtx *round_vtx(gfx_vtx *q, float px, float py, float x, float y, float u0, float v0, float su,
                                 float sv, u32 tint)
{
    return gfx_put(q, px, py, tint, u0 + (px - x) * su, v0 + (py - y) * sv);
}

/* The outline of round_rect: ui.c's corner points (round_pts' tables, seg 4
 * below a radius of 8, else 8), walked corner by corner as round_pts does,
 * then the first point again to close the fan: no division per vertex. */
void tex_round(const gfx_tex *t, float x, float y, float w, float h, float r, float u0, float v0, float u1, float v1,
               u32 tint)
{
    float m = (w < h ? w : h) / 2;
    r = r > m ? m : r < 0 ? 0 : r;
    if (r < 1) {
        tex_sub(t, x, y, w, h, u0, v0, u1, v1, tint);
        return;
    }
    int seg = r < 8 ? 4 : 8, n = 4 * (seg + 1);
    const float *c, *s;
    ui_round_unit(seg, &c, &s);
    const float cx[4] = {x + r, x + w - r, x + w - r, x + r}, cy[4] = {y + r, y + r, y + h - r, y + h - r};
    float su = (u1 - u0) / w, sv = (v1 - v0) / h;
    gfx_texture(t);
    gfx_vtx *q = gfx_prim(GFX_TRIANGLE_FAN, n + 2);
    q = gfx_put(q, x + w / 2, y + h / 2, tint, (u0 + u1) / 2, (v0 + v1) / 2);
    for (int k = 0, j = 0; k < 4; k++)
        for (int i = 0; i <= seg; i++, j++)
            q = round_vtx(q, cx[k] + c[j] * r, cy[k] + s[j] * r, x, y, u0, v0, su, sv, tint);
    round_vtx(q, cx[0] + c[0] * r, cy[0] + s[0] * r, x, y, u0, v0, su, sv, tint);
    gfx_texture(NULL);
}

void glass_uv(float x, float y, float w, float h, float *u0, float *v0, float *u1, float *v1)
{
    float ox, oy;
    screen_origin(&ox, &oy);
    *u0 = (x - ox) / 1920;
    *u1 = (x + w - ox) / 1920;
    *v0 = (y - oy) / 1080;
    *v1 = (y + h - oy) / 1080;
}

void glow9(const gfx_tex *t, float x, float y, float w, float h, float pad, u32 tint)
{
    float m = (w < h ? w : h) / 2;
    pad = pad > m ? m : pad;
    if (pad <= 0) return;
    const float X[4] = {x - pad, x + pad, x + w - pad, x + w + pad}, Y[4] = {y - pad, y + pad, y + h - pad, y + h + pad};
    const float U[4] = {0, 0.5f, 0.5f, 1};
    gfx_texture(t);
    gfx_vtx *q = gfx_prim(GFX_QUADS, 32);
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            if (i == 1 && j == 1) continue;
            q = gfx_put(q, X[i], Y[j], tint, U[i], U[j]);
            q = gfx_put(q, X[i + 1], Y[j], tint, U[i + 1], U[j]);
            q = gfx_put(q, X[i + 1], Y[j + 1], tint, U[i + 1], U[j + 1]);
            q = gfx_put(q, X[i], Y[j + 1], tint, U[i], U[j + 1]);
        }
    gfx_texture(NULL);
}

void quad_rot(const gfx_tex *t, float cx, float cy, float w, float h, float angle, float u0, float v0, float u1,
              float v1, u32 tint)
{
    float c = cosf(angle), s = sinf(angle), hw = w / 2, hh = h / 2;
    const float dx[4] = {-hw, hw, hw, -hw}, dy[4] = {-hh, -hh, hh, hh};
    const float u[4] = {u0, u1, u1, u0}, v[4] = {v0, v0, v1, v1};
    gfx_texture(t);
    gfx_vtx *q = gfx_prim(GFX_QUADS, 4);
    for (int k = 0; k < 4; k++) q = gfx_put(q, cx + dx[k] * c - dy[k] * s, cy + dx[k] * s + dy[k] * c, tint, u[k], v[k]);
    gfx_texture(NULL);
}

/* The unit normal (to the left of the direction of travel, y down) of the
 * segment from point a to point b; 0, 0 for no length. */
static void normal(const float *pts, int a, int b, float *nx, float *ny)
{
    float dx = pts[2 * b] - pts[2 * a], dy = pts[2 * b + 1] - pts[2 * a + 1], len = sqrtf(dx * dx + dy * dy);
    if (len <= 0) {
        *nx = *ny = 0;
        return;
    }
    *nx = dy / len;
    *ny = -dx / len;
}

/* The normal of the first segment from point i on that has a length (a
 * repeated point makes none), 0, 0 when no segment has; *j is its first
 * point (n - 1 for none). */
static void next_normal(const float *pts, int n, int i, int *j, float *nx, float *ny)
{
    *nx = *ny = 0;
    for (; i < n - 1; i++) {
        normal(pts, i, i + 1, nx, ny);
        if (*nx != 0 || *ny != 0) break;
    }
    *j = i;
}

/* The offset of a point's two strip vertices from it: the miter of the
 * segments before and after it (their unit normals a and b), hw long across
 * each, at most 2 hw. Never inlined (ppu-gcc 7.2 does not inline it either):
 * inlined, a compiler that fuses across statements (gcc's default
 * -ffp-contract=fast) can make the caller's x + ox one fmadds, and the
 * vertices move by an ulp. */
static __attribute__((noinline)) void miter(float ax, float ay, float bx, float by, float hw, float *ox, float *oy)
{
    if (ax == 0 && ay == 0) {                /* an end: the other segment */
        ax = bx;
        ay = by;
    }
    if (bx == 0 && by == 0) {
        bx = ax;
        by = ay;
    }
    float mx = ax + bx, my = ay + by, ml = sqrtf(mx * mx + my * my);
    if (ml <= 0) {                           /* a turn back: square across */
        *ox = ax * hw;
        *oy = ay * hw;
        return;
    }
    mx /= ml;
    my /= ml;
    float d = mx * ax + my * ay, k = d > 0.5f ? hw / d : 2 * hw;
    *ox = mx * k;
    *oy = my * k;
}

/* A strip in pieces of at most PTS_MAX points, each from the last point of
 * the one before. Each segment's normal is made once: a is the normal of the
 * segment before the point, b of the next segment with a length (it starts
 * at point j). A repeated point gets the rows of the point it repeats: its
 * triangles have no area. */
void strip(const float *pts, int n, float width, u32 rgba)
{
    if (n < 2 || width <= 0) return;
    float hw = width / 2, ax = 0, ay = 0, bx, by;
    int j;
    next_normal(pts, n, 0, &j, &bx, &by);
    if (j == n - 1) return;                  /* no length at all */
    for (int s = 0; s < n - 1; s += PTS_MAX - 1) {
        int e = n - 1 - s > PTS_MAX - 1 ? s + PTS_MAX - 1 : n - 1;
        gfx_texture(NULL);
        gfx_vtx *q = gfx_prim(GFX_TRIANGLE_STRIP, 2 * (e - s + 1));
        for (int i = s; i <= e; i++) {
            if (i > j) {                     /* past segment j: it is the one before */
                ax = bx;
                ay = by;
                next_normal(pts, n, i, &j, &bx, &by);
            }
            float ox, oy, x = pts[2 * i], y = pts[2 * i + 1];
            miter(ax, ay, bx, by, hw, &ox, &oy);
            q = gfx_put(q, x + ox, y + oy, rgba, 0, 0);
            q = gfx_put(q, x - ox, y - oy, rgba, 0, 0);
        }
    }
}

/* 1 when the segment from point i to i + 1 crosses base_y (its ends on its
 * two sides). */
static int crosses(const float *pts, int i, float base_y)
{
    float a = pts[2 * i + 1] - base_y, b = pts[2 * i + 3] - base_y;
    return (a < 0 && b > 0) || (a > 0 && b < 0);
}

/* A strip in pieces of at most GFX_PRIM_MAX vertices, each from the last
 * point of the one before. Where a segment crosses base_y, its quad would be
 * a bowtie whose triangles overlap: a pair of vertices at the crossing
 * splits it into two triangles that meet there (the two between them have no
 * area). */
void area(const float *pts, int n, float base_y, u32 rgba_top, u32 rgba_base)
{
    if (n < 2) return;
    for (int s = 0; s < n - 1;) {
        int e = s, nv = 2;                   /* the piece: points s to e, nv vertices */
        while (e < n - 1 && nv + 2 + 2 * crosses(pts, e, base_y) <= GFX_PRIM_MAX) nv += 2 + 2 * crosses(pts, e++, base_y);
        gfx_texture(NULL);
        gfx_vtx *q = gfx_prim(GFX_TRIANGLE_STRIP, nv);
        for (int i = s; i <= e; i++) {
            if (i > s && crosses(pts, i - 1, base_y)) {
                float x0 = pts[2 * i - 2], y0 = pts[2 * i - 1];
                float x = x0 + (pts[2 * i] - x0) * ((base_y - y0) / (pts[2 * i + 1] - y0));
                q = gfx_put(q, x, base_y, rgba_base, 0, 0);
                q = gfx_put(q, x, base_y, rgba_base, 0, 0);
            }
            q = gfx_put(q, pts[2 * i], pts[2 * i + 1], rgba_top, 0, 0);
            q = gfx_put(q, pts[2 * i], base_y, rgba_base, 0, 0);
        }
        s = e;
    }
}

/* ---- anti-aliased lines and shapes ---------------------------------------- */

/* The helpers' quads, gathered here and sent as GFX_QUADS primitives of at
 * most GFX_PRIM_MAX vertices (consecutive ones with no texture join one
 * draw). Each helper sends what is left before it returns. */
typedef struct {
    float x, y;
    u32 c;
} aav;
static aav aq[GFX_PRIM_MAX];
static int aqn;

static void aa_flush(void)
{
    if (!aqn) return;
    gfx_texture(NULL);
    gfx_vtx *q = gfx_prim(GFX_QUADS, aqn);
    for (int i = 0; i < aqn; i++) q = gfx_put(q, aq[i].x, aq[i].y, aq[i].c, 0, 0);
    aqn = 0;
}

/* One quad: the corners a, b, c, d in order, each with its colour. */
static void aa_quad(const aav *a, const aav *b, const aav *c, const aav *d)
{
    if (aqn + 4 > GFX_PRIM_MAX) aa_flush();
    aq[aqn++] = *a;
    aq[aqn++] = *b;
    aq[aqn++] = *c;
    aq[aqn++] = *d;
}

/* The core's half width of a w px line; under 1 px the alpha goes down instead
 * (under 16 the alpha test drops the whole line). */
static float aa_core(float w, u32 *rgba)
{
    if (w >= 1) return w / 2 - 0.5f;
    u32 a = (u32)((float)(*rgba & 255) * (w > 0 ? w : 0) + 0.5f);
    *rgba = (*rgba & ~0xffu) | a;
    return 0;
}

/* Two rows of four vertices across a line (outer left, inner left, inner
 * right, outer right): the three quads between them. */
static void aa_rows(const aav *p, const aav *q)
{
    for (int k = 0; k < 3; k++) aa_quad(&p[k], &q[k], &q[k + 1], &p[k + 1]);
}

/* The row across point x, y: offsets o[k] times the unit across (nx, ny),
 * the colours c (a for the inner two, z for the outer two, or z for all). */
static void aa_row(aav *r, float x, float y, float nx, float ny, float hc, u32 in, u32 out)
{
    const float o[4] = {hc + 1, hc, -hc, -hc - 1};
    for (int k = 0; k < 4; k++) {
        r[k].x = x + nx * o[k];
        r[k].y = y + ny * o[k];
        r[k].c = k == 1 || k == 2 ? in : out;
    }
}

/* One segment: a 3 x 3 grid of quads, the core between the inner rows and
 * half a pixel in from each end, the feather 1 px wide around it. Under 1 px
 * long the core has no length and the two end ramps meet in the middle, with
 * (1 + len) / 2 of ink along: the alpha times 2 len / (1 + len) makes it len. */
static void aa_seg(float x0, float y0, float x1, float y1, float hc, u32 c)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0) return;
    float ux = dx / len, uy = dy / len, nx = uy, ny = -ux, e = len < 1 ? len / 2 : 0.5f;
    u32 z = c & ~0xffu;
    if (len < 1) c = z | (u32)((float)(c & 255) * 2 * len / (1 + len) + 0.5f);
    aav r[4][4];
    aa_row(r[0], x0 - ux * 0.5f, y0 - uy * 0.5f, nx, ny, hc, z, z);
    aa_row(r[1], x0 + ux * e, y0 + uy * e, nx, ny, hc, c, z);
    aa_row(r[2], x1 - ux * e, y1 - uy * e, nx, ny, hc, c, z);
    aa_row(r[3], x1 + ux * 0.5f, y1 + uy * 0.5f, nx, ny, hc, z, z);
    for (int j = 0; j < 3; j++) aa_rows(r[j], r[j + 1]);
}

void aa_line(float x0, float y0, float x1, float y1, float w, u32 rgba)
{
    if (w <= 0) return;
    float hc = aa_core(w, &rgba);
    aa_seg(x0, y0, x1, y1, hc, rgba);
    aa_flush();
}

void aa_lines(const float *seg, int n, float w, u32 rgba)
{
    if (w <= 0) return;
    float hc = aa_core(w, &rgba);
    for (int i = 0; i < n; i++) aa_seg(seg[4 * i], seg[4 * i + 1], seg[4 * i + 2], seg[4 * i + 3], hc, rgba);
    aa_flush();
}

/* The polyline: at each point the row across along strip's miter (miter at
 * half width 1, times each offset), the ends half a pixel in, and a row of
 * alpha 0 half a pixel past each end. A repeated point is skipped: only the
 * last copy of a point draws, so the first point is the first one with a
 * segment, and each next one starts the next segment with a length (or is
 * the last point). a and b are the normals of the segments before and after
 * the point, each made once. */
void aa_strip(const float *pts, int n, float w, u32 rgba)
{
    if (n < 2 || w <= 0) return;
    float ax = 0, ay = 0, bx, by;
    int i, p = 0;                            /* this point, the one before */
    next_normal(pts, n, 0, &i, &bx, &by);
    if (i == n - 1) return;                  /* no length at all */
    int first = i;
    float hc = aa_core(w, &rgba);
    u32 z = rgba & ~0xffu;
    aav prev[4], cur[4], cap[4];
    for (;;) {
        float mx, my, x = pts[2 * i], y = pts[2 * i + 1];
        miter(ax, ay, bx, by, 1, &mx, &my);
        if (i == first || i == n - 1) {                   /* an end: the segment's direction, outward */
            int a = i == first ? i + 1 : p, b = i;
            float dx = pts[2 * b] - pts[2 * a], dy = pts[2 * b + 1] - pts[2 * a + 1], len = sqrtf(dx * dx + dy * dy);
            float ux = len > 0 ? dx / len : 0, uy = len > 0 ? dy / len : 0, e = len < 1 ? len / 2 : 0.5f;
            aa_row(cap, x + ux * 0.5f, y + uy * 0.5f, mx, my, hc, z, z);
            aa_row(cur, x - ux * e, y - uy * e, mx, my, hc, rgba, z);
            if (i == first) aa_rows(cap, cur);
            else {
                aa_rows(prev, cur);
                aa_rows(cur, cap);
            }
        } else {
            aa_row(cur, x, y, mx, my, hc, rgba, z);
            aa_rows(prev, cur);
        }
        for (int k = 0; k < 4; k++) prev[k] = cur[k];
        if (i == n - 1) break;
        p = i;
        ax = bx;
        ay = by;
        next_normal(pts, n, i + 1, &i, &bx, &by);
    }
    aa_flush();
}

/* The unit circle in 96 steps, made at the first call (each angle through a
 * volatile, as ui.c's circle tables). A circle takes 16 points under radius
 * 8, 24 under 16, 48 under 100, else 96 (r the outer radius: a ring's outer
 * feather, a disc's r + 0.5). A chord lies up to r (1 - cos(pi / points))
 * inside the true circle: 0.15 px under 8, 0.14 under 16, 0.21 under 100,
 * then r / 1868 (0.2 px at 374, 0.29 at 540). */
#define CIRC 96
static float cc[CIRC + 1], cs[CIRC + 1];
static int circ_made;

static void circ(void)
{
    volatile float va;
    for (int i = 0; i < CIRC; i++) {
        va = 2 * PI_F * (float)i / CIRC;
        float a = va;
        cc[i] = cosf(a);
        cs[i] = sinf(a);
    }
    cc[CIRC] = cc[0];
    cs[CIRC] = cs[0];
    circ_made = 1;
}

static int circ_step(float r) { return r < 8 ? 6 : r < 16 ? 4 : r < 100 ? 2 : 1; }

void aa_ring(float cx, float cy, float r, float w, u32 rgba)
{
    if (w <= 0 || r <= 0) return;
    if (!circ_made) circ();
    float hc = aa_core(w, &rgba);
    u32 z = rgba & ~0xffu;
    float rad[4] = {r + hc + 1, r + hc, r - hc, r - hc - 1};
    for (int k = 0; k < 4; k++) rad[k] = rad[k] < 0 ? 0 : rad[k];
    int st = circ_step(r + hc + 1);
    aav p[4], q[4];
    for (int i = 0; i <= CIRC; i += st) {
        for (int k = 0; k < 4; k++) {
            q[k].x = cx + cc[i] * rad[k];
            q[k].y = cy + cs[i] * rad[k];
            q[k].c = k == 1 || k == 2 ? rgba : z;
        }
        if (i) aa_rows(p, q);
        for (int k = 0; k < 4; k++) p[k] = q[k];
    }
    aa_flush();
}

void aa_disc(float cx, float cy, float r, u32 rgba)
{
    if (r <= 0) return;
    if (!circ_made) circ();
    u32 z = rgba & ~0xffu;
    float ri = r > 0.5f ? r - 0.5f : 0, ro = r + 0.5f;
    if (r < 0.5f) rgba = (rgba & ~0xffu) | (u32)((float)(rgba & 255) * 2 * r + 0.5f);
    int st = circ_step(ro);
    aav c = {cx, cy, rgba}, p[2], q[2];
    for (int i = 0; i <= CIRC; i += st) {
        q[0] = (aav){cx + cc[i] * ri, cy + cs[i] * ri, rgba};
        q[1] = (aav){cx + cc[i] * ro, cy + cs[i] * ro, z};
        if (i) {
            if (ri > 0) aa_quad(&c, &p[0], &q[0], &q[0]);   /* a triangle of the fan: the last corner twice */
            aa_quad(&p[0], &p[1], &q[1], &q[0]);
        }
        p[0] = q[0];
        p[1] = q[1];
    }
    aa_flush();
}

/* At each point, in at the colour half a pixel in along the miter of its two
 * edges (their unit normals a and b), out at alpha 0 half a pixel out; the
 * quads from the point before, and at the end the ones back to the first.
 * A point equal to the next one (the last to the first too) is skipped, so
 * the edges have a length; each edge's normal is made once per pass. */
void aa_poly(const float *pts, int n, u32 rgba)
{
    if (n < 3) return;
    u32 z = rgba & ~0xffu;
    float gx = 0, gy = 0, ax, ay, bx, by;
    int m = 0, last = 0;
    for (int i = 0; i < n; i++) {
        normal(pts, i, (i + 1) % n, &bx, &by);
        if (bx == 0 && by == 0) continue;
        gx += pts[2 * i], gy += pts[2 * i + 1];
        m++;
        last = i;
    }
    if (m < 3) return;
    gx /= (float)m;
    gy /= (float)m;
    normal(pts, last, (last + 1) % n, &ax, &ay);   /* the edge into the first point */
    aav g = {gx, gy, rgba}, in0 = g, out0 = g, pin = g, pout = g;
    for (int i = 0, first = 1; i < n; i++) {
        normal(pts, i, (i + 1) % n, &bx, &by);
        if (bx == 0 && by == 0) continue;
        float x = pts[2 * i], y = pts[2 * i + 1], mx = ax + bx, my = ay + by, ml = sqrtf(mx * mx + my * my);
        if (ml <= 0) mx = ax, my = ay, ml = 1;
        mx /= ml;
        my /= ml;
        float d = mx * ax + my * ay, k = d > 0.25f ? 1 / d : 4;   /* the miter, at most 4 times */
        if (mx * (x - gx) + my * (y - gy) < 0) k = -k;            /* outward */
        aav in = {x - mx * k * 0.5f, y - my * k * 0.5f, rgba}, out = {x + mx * k * 0.5f, y + my * k * 0.5f, z};
        if (first) in0 = in, out0 = out, first = 0;
        else {
            aa_quad(&g, &pin, &in, &in);
            aa_quad(&pin, &pout, &out, &in);
        }
        pin = in;
        pout = out;
        ax = bx;
        ay = by;
    }
    aa_quad(&g, &pin, &in0, &in0);
    aa_quad(&pin, &pout, &out0, &in0);
    aa_flush();
}

void aa_corners(float x, float y, float w, float h, float r, u32 rgba)
{
    float m = (w < h ? w : h) / 2;
    r = r > m ? m : r;
    if (r < 1) return;
    int seg = r < 8 ? 4 : 8;
    const float *c, *s;
    ui_round_unit(seg, &c, &s);
    const float cx[4] = {x + r, x + w - r, x + w - r, x + r}, cy[4] = {y + r, y + r, y + h - r, y + h - r};
    u32 half = (rgba & ~0xffu) | ((rgba & 255) + 1) / 2, z = rgba & ~0xffu;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < seg; i++) {
            int j = k * (seg + 1) + i;
            aav a = {cx[k] + c[j] * r, cy[k] + s[j] * r, half}, b = {cx[k] + c[j + 1] * r, cy[k] + s[j + 1] * r, half};
            aav bo = {cx[k] + c[j + 1] * (r + 1), cy[k] + s[j + 1] * (r + 1), z}, ao = {cx[k] + c[j] * (r + 1), cy[k] + s[j] * (r + 1), z};
            aa_quad(&a, &b, &bo, &ao);
        }
    aa_flush();
}

void tex_upload(const gfx_tex *t, void *dst, const void *src, u32 src_pitch_words)
{
    u32 words = (t->w * (t->format == GFX_TEX_A8R8G8B8 ? 4u : 2u) + 3) / 4;
    for (u32 y = 0; y < t->h; y++) {
        volatile u32 *d = (volatile u32 *)((u8 *)dst + y * t->pitch);   /* one 32-bit store each: no merged stores */
        const u32 *s = (const u32 *)src + y * src_pitch_words;
        for (u32 x = 0; x < words; x++) d[x] = s[x];
    }
}

int tex_pair_new(tex_pair *p, u16 w, u16 h, u16 format, u16 linear)
{
    p->shown = 0;
    p->addr[0] = gfx_tex_new(&p->tex[0], w, h, format, linear);
    p->addr[1] = p->addr[0] ? gfx_tex_new(&p->tex[1], w, h, format, linear) : NULL;
    return p->addr[1] ? 0 : -1;
}

const gfx_tex *tex_pair_shown(const tex_pair *p) { return &p->tex[p->shown]; }
const gfx_tex *tex_pair_wtex(const tex_pair *p) { return &p->tex[!p->shown]; }
void *tex_pair_written(const tex_pair *p) { return p->addr[!p->shown]; }
void tex_pair_swap(tex_pair *p) { p->shown = !p->shown; }
