/* Drawing helpers on the public gfx API (fx.h). An archive member of its
 * own: an app that calls none of them links none of this. */
#include <math.h>
#include <stddef.h>
#include "gfx.h"
#include "ui.h"
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

/* The corner points of round_rect (ui.c's round_pts: seg 4 below a radius
 * of 8, else 8), made at the first call with ui.c's angle expressions. Each
 * angle goes through a volatile, as in ui.c, so a compiler cannot fold a
 * cosf of a constant with other rounding than the library's. */
static float rc[2][36], rs[2][36];
static int corners_made;

static void corners(void)
{
    volatile float va;
    float b;
    for (int t = 0; t < 2; t++) {
        int seg = t ? 8 : 4, n = 0;
        for (int k = 0; k < 4; k++)
            for (int i = 0; i <= seg; i++) {
                float a = PI_F * (1 + k * 0.5f) + PI_F / 2 * i / seg;
                va = a;
                b = va;
                rc[t][n] = cosf(b);
                rs[t][n++] = sinf(b);
            }
    }
    corners_made = 1;
}

void tex_round(const gfx_tex *t, float x, float y, float w, float h, float r, float u0, float v0, float u1, float v1,
               u32 tint)
{
    float m = (w < h ? w : h) / 2;
    r = r > m ? m : r < 0 ? 0 : r;
    if (r < 1) {
        tex_sub(t, x, y, w, h, u0, v0, u1, v1, tint);
        return;
    }
    if (!corners_made) corners();
    int seg = r < 8 ? 4 : 8, n = 4 * (seg + 1);
    const float *c = rc[seg == 8], *s = rs[seg == 8];
    const float cx[4] = {x + r, x + w - r, x + w - r, x + r}, cy[4] = {y + r, y + r, y + h - r, y + h - r};
    float su = (u1 - u0) / w, sv = (v1 - v0) / h;
    gfx_texture(t);
    gfx_vtx *q = gfx_prim(GFX_TRIANGLE_FAN, n + 2);
    q = gfx_put(q, x + w / 2, y + h / 2, tint, (u0 + u1) / 2, (v0 + v1) / 2);
    for (int j = 0; j <= n; j++) {
        int i = j % n, k = i / (seg + 1);
        float px = cx[k] + c[i] * r, py = cy[k] + s[i] * r;
        q = gfx_put(q, px, py, tint, u0 + (px - x) * su, v0 + (py - y) * sv);
    }
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

/* The offset of point i's two strip vertices from it: the miter of the two
 * segments that meet there, hw long across each, at most 2 hw. */
static void miter(const float *pts, int n, int i, float hw, float *ox, float *oy)
{
    float ax = 0, ay = 0, bx = 0, by = 0;
    if (i > 0) normal(pts, i - 1, i, &ax, &ay);
    if (i < n - 1) normal(pts, i, i + 1, &bx, &by);
    if (ax == 0 && ay == 0) {                /* an end, or a segment of no length: the other one */
        ax = bx;
        ay = by;
    }
    if (bx == 0 && by == 0) {
        bx = ax;
        by = ay;
    }
    float mx = ax + bx, my = ay + by, ml = sqrtf(mx * mx + my * my);
    if (ml <= 0) {                           /* a turn back, or no length at all: square across */
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
 * the one before. */
void strip(const float *pts, int n, float width, u32 rgba)
{
    if (n < 2 || width <= 0) return;
    float hw = width / 2;
    for (int s = 0; s < n - 1; s += PTS_MAX - 1) {
        int e = n - 1 - s > PTS_MAX - 1 ? s + PTS_MAX - 1 : n - 1;
        gfx_texture(NULL);
        gfx_vtx *q = gfx_prim(GFX_TRIANGLE_STRIP, 2 * (e - s + 1));
        for (int i = s; i <= e; i++) {
            float ox, oy, x = pts[2 * i], y = pts[2 * i + 1];
            miter(pts, n, i, hw, &ox, &oy);
            q = gfx_put(q, x + ox, y + oy, rgba, 0, 0);
            q = gfx_put(q, x - ox, y - oy, rgba, 0, 0);
        }
    }
}

void area(const float *pts, int n, float base_y, u32 rgba_top, u32 rgba_base)
{
    if (n < 2) return;
    for (int s = 0; s < n - 1; s += PTS_MAX - 1) {
        int e = n - 1 - s > PTS_MAX - 1 ? s + PTS_MAX - 1 : n - 1;
        gfx_texture(NULL);
        gfx_vtx *q = gfx_prim(GFX_TRIANGLE_STRIP, 2 * (e - s + 1));
        for (int i = s; i <= e; i++) {
            q = gfx_put(q, pts[2 * i], pts[2 * i + 1], rgba_top, 0, 0);
            q = gfx_put(q, pts[2 * i], base_y, rgba_base, 0, 0);
        }
    }
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
