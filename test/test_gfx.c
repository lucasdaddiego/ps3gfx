/* Host test of gfx's front end (source/gfx/gfx.c) on a back end that records
 * the draws: the batching, the vertex area and its drains, the viewport and
 * the frame. CI runs it with a small area of 409 vertices:
 *   cc -std=c99 -Wall -Wextra -DGFX_VTX_BYTES=8192 -I tools/preview/stub -I source/gfx \
 *      test/test_gfx.c source/gfx/gfx.c */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* ---- the back end: it records what gfx.c sends ---------------------------------- */

static gfx_vtx *area;
static u32 area_n;                           /* the area in vertices */
static struct { int type; u32 first, n; const gfx_tex *t; } draws[256];
static int nd, drains, begins, ends, exits, viewports;
static float last_vp[4];

int gfx_be_init(u32 vtx_bytes, gfx_vtx **out)
{
    void *p;
    if (posix_memalign(&p, 128, vtx_bytes)) return 1;
    area = p;
    area_n = vtx_bytes / sizeof(gfx_vtx);
    *out = area;
    return 0;
}

void gfx_be_begin(const float *vp) { (void)vp; begins++; }

void gfx_be_viewport(const float *vp)
{
    memcpy(last_vp, vp, sizeof last_vp);
    viewports++;
}

void gfx_be_draw(int type, u32 first, u32 n, const gfx_tex *t)
{
    if (nd < 256) {
        draws[nd].type = type;
        draws[nd].first = first;
        draws[nd].n = n;
        draws[nd].t = t;
    }
    nd++;
}

void gfx_be_drain(void) { drains++; }
void gfx_be_end(void) { ends++; }
void gfx_be_exit(void) { exits++; }
void *gfx_vram(u32 size, u32 align, u32 *offset) { (void)size; (void)align; (void)offset; return NULL; }

static void reset(void) { nd = drains = begins = ends = exits = viewports = 0; }

static int draw_is(int i, int type, u32 first, u32 n, const gfx_tex *t)
{
    return i < nd && draws[i].type == type && draws[i].first == first && draws[i].n == n && draws[i].t == t;
}

/* ---- the tests ------------------------------------------------------------------ */

/* Consecutive QUADS or TRIANGLES with the same texture join one draw; a
 * texture or a type change starts a new one; a strip or a fan never joins. */
static void batching(void)
{
    static const gfx_tex t1 = {0x1000, 16, 16, 32, 0, GFX_TEX_A4R4G4B4};
    reset();
    gfx_begin();
    gfx_texture(NULL);
    gfx_vtx *a = gfx_prim(GFX_QUADS, 4);
    gfx_vtx *b = gfx_prim(GFX_QUADS, 8);
    CHECK(a == area && b == a + 4);
    gfx_texture(&t1);
    gfx_prim(GFX_QUADS, 4);
    gfx_prim(GFX_TRIANGLES, 3);
    gfx_prim(GFX_TRIANGLES, 6);
    gfx_prim(GFX_TRIANGLE_FAN, 5);
    gfx_prim(GFX_TRIANGLE_FAN, 5);
    gfx_prim(GFX_TRIANGLE_STRIP, 4);
    gfx_end();
    CHECK(nd == 6);
    CHECK(draw_is(0, GFX_QUADS, 0, 12, NULL));
    CHECK(draw_is(1, GFX_QUADS, 12, 4, &t1));
    CHECK(draw_is(2, GFX_TRIANGLES, 16, 9, &t1));
    CHECK(draw_is(3, GFX_TRIANGLE_FAN, 25, 5, &t1));
    CHECK(draw_is(4, GFX_TRIANGLE_FAN, 30, 5, &t1));
    CHECK(draw_is(5, GFX_TRIANGLE_STRIP, 35, 4, &t1));
    CHECK(begins == 1 && ends == 1 && drains == 0);
    gfx_texture(NULL);
}

/* 1 to GFX_PRIM_MAX vertices; outside a frame the room is the area's start
 * and nothing is drawn. */
static void bounds(void)
{
    reset();
    gfx_begin();
    CHECK(gfx_prim(GFX_QUADS, 0) == NULL);
    CHECK(gfx_prim(GFX_QUADS, GFX_PRIM_MAX + 1) == NULL);
    CHECK(gfx_prim(GFX_QUADS, -4) == NULL);
    CHECK(gfx_prim(GFX_QUADS, GFX_PRIM_MAX) == area);
    gfx_end();
    CHECK(draw_is(0, GFX_QUADS, 0, GFX_PRIM_MAX, NULL) && nd == 1);
    reset();
    CHECK(gfx_prim(GFX_QUADS, 4) == area);
    gfx_end();
    CHECK(nd == 0 && ends == 0);
}

/* A full area drains: the open draw goes out first, then the vertices start
 * again at index 0. Each frame starts at index 0. */
static void drain(void)
{
    reset();
    gfx_begin();
    gfx_vtx *a = gfx_prim(GFX_QUADS, 256);
    gfx_vtx *b = gfx_prim(GFX_QUADS, 256);               /* 512 > 409: a drain */
    CHECK(a == area && b == area);
    CHECK(drains == 1 && draw_is(0, GFX_QUADS, 0, 256, NULL));
    gfx_prim(GFX_QUADS, 148);                            /* 404: still room */
    CHECK(drains == 1);
    gfx_prim(GFX_QUADS, 8);                              /* 412 > 409 */
    CHECK(drains == 2 && draw_is(1, GFX_QUADS, 0, 404, NULL));
    gfx_end();
    CHECK(draw_is(2, GFX_QUADS, 0, 8, NULL) && nd == 3);
    gfx_begin();
    CHECK(gfx_prim(GFX_QUADS, 4) == area);
    gfx_end();
    CHECK(area_n == 409);
}

/* A viewport change closes the open draw; the back end gets it only inside
 * a frame, and the next frame starts with it. */
static void viewport(void)
{
    reset();
    gfx_viewport(1, 2, 3, 4);
    CHECK(viewports == 0);
    gfx_begin();
    gfx_prim(GFX_QUADS, 4);
    gfx_viewport(5, 6, 7, 8);
    CHECK(viewports == 1 && last_vp[0] == 5 && last_vp[3] == 8);
    CHECK(draw_is(0, GFX_QUADS, 0, 4, NULL));
    gfx_prim(GFX_QUADS, 4);
    gfx_end();
    CHECK(draw_is(1, GFX_QUADS, 4, 4, NULL) && nd == 2);
    gfx_viewport(0, 0, 1, 1);
}

/* A second gfx_begin or gfx_end does nothing; gfx_exit ends the frame, then
 * calls the back end's exit. */
static void frames(void)
{
    reset();
    gfx_begin();
    gfx_begin();
    gfx_prim(GFX_QUADS, 4);
    gfx_exit();
    CHECK(begins == 1 && ends == 1 && exits == 1 && nd == 1);
    gfx_end();
    CHECK(ends == 1);
}

int main(void)
{
    if (gfx_init() != 0) {
        printf("FAIL: gfx_init\n");
        return 1;
    }
    batching();
    bounds();
    drain();
    viewport();
    frames();
    if (fails) {
        printf("%d checks failed\n", fails);
        return 1;
    }
    printf("test_gfx: all checks passed\n");
    return 0;
}
