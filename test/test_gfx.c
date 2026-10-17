/* Host test of gfx's front end (source/gfx/gfx.c) on a back end that records
 * the draws: the batching, the vertex area and its drains, the blocks, the
 * viewport, the frame, the submit and the wait, and the copy's rules and its
 * job request. CI runs it with a small area of 409 vertices:
 *   cc -std=c99 -Wall -Wextra -DGFX_VTX_BYTES=8192 -I tools/preview/stub -I source/gfx \
 *      test/test_gfx.c source/gfx/gfx.c */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gfx.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* ---- the back end: it records what gfx.c sends ---------------------------------- */

static gfx_vtx *area;
static u32 area_n;                           /* the area in vertices */
static struct { int type; u32 first, n; const gfx_tex *t; } draws[256];
static int nd, drains, begins, ends, waits, exits, viewports;
static int outstanding;                      /* a submit without its wait: the back end gets nothing else */
static int open_frame;                       /* between a begin and its submit */
static int copies, naps, nap_mode;           /* nap_mode 1: a nap draws a frame, as the drawing thread does; 2: and asks again first */
static int nested_rc;
static u8 vram[4096] __attribute__((aligned(128)));   /* the RSX memory of the copies */
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

void gfx_be_begin(const float *vp)
{
    (void)vp;
    CHECK(!outstanding);
    open_frame = 1;
    begins++;
}

void gfx_be_viewport(const float *vp)
{
    memcpy(last_vp, vp, sizeof last_vp);
    viewports++;
}

void gfx_be_draw(int type, u32 first, u32 n, const gfx_tex *t)
{
    CHECK(!outstanding);
    if (nd < 256) {
        draws[nd].type = type;
        draws[nd].first = first;
        draws[nd].n = n;
        draws[nd].t = t;
    }
    nd++;
}

void gfx_be_drain(void)
{
    CHECK(!outstanding);
    drains++;
}

void gfx_be_submit(void)
{
    CHECK(!outstanding);
    outstanding = 1;
    open_frame = 0;
    ends++;
}

void gfx_be_wait(void)
{
    outstanding = 0;
    waits++;
}

void gfx_be_exit(void)
{
    CHECK(!outstanding);
    exits++;
}
/* A copy runs between frames only: no frame open, none waiting. */
int gfx_be_copy(const void *src, void *dst, u32 bytes)
{
    const u8 *s = src;
    CHECK(!outstanding && !open_frame);
    copies++;
    if (s < vram || bytes > sizeof vram || (size_t)(s - vram) > sizeof vram - bytes) return GFX_COPY_ERANGE;
    memcpy(dst, s, bytes);
    return 0;
}

static u8 job_dst[256];

void gfx_be_nap(void)
{
    naps++;
    if (nap_mode == 2) nested_rc = gfx_copy_to_main_job(vram, job_dst, 128);   /* the first request still waits */
    if (nap_mode) {
        gfx_begin();
        gfx_prim(GFX_QUADS, 4);
        gfx_end();
    }
}

void *gfx_vram(u32 size, u32 align, u32 *offset) { (void)size; (void)align; (void)offset; return NULL; }

static void reset(void) { nd = drains = begins = ends = waits = exits = viewports = copies = naps = 0; }

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

/* A block: a draw of its own from the next index that is a multiple of
 * align / 4 vertices (32 for 128 bytes, an address on a 128-byte line here);
 * later primitives of its type and texture join it; one that does not fit
 * drains first; one that fits exactly does not. */
static void block(void)
{
    static const gfx_tex t1 = {0x1000, 16, 16, 32, 0, GFX_TEX_A4R4G4B4};
    reset();
    gfx_begin();
    gfx_texture(NULL);
    gfx_prim(GFX_QUADS, 4);
    CHECK(gfx_block(GFX_QUADS, 0, 128) == NULL);
    CHECK(gfx_block(GFX_QUADS, 412, 128) == NULL);           /* more than the area */
    CHECK(gfx_block(GFX_QUADS, 6, 128) == NULL);             /* not whole quads */
    CHECK(gfx_block(GFX_TRIANGLES, 4, 128) == NULL);         /* not whole triangles */
    CHECK(gfx_block(GFX_TRIANGLE_FAN, 6, 128) == NULL);
    CHECK(gfx_block(GFX_QUADS, 4, 0) == NULL && gfx_block(GFX_QUADS, 4, 96) == NULL && gfx_block(GFX_QUADS, 4, 256) == NULL);
    gfx_vtx *b = gfx_block(GFX_QUADS, 100, 128);
    CHECK(b == area + 32 && ((uintptr_t)b & 127) == 0);
    gfx_prim(GFX_QUADS, 4);                                   /* joins the block's draw */
    gfx_texture(&t1);
    CHECK(gfx_block(GFX_TRIANGLES, 30, 16) == area + 136);    /* 136 is a multiple of 4 */
    CHECK(gfx_block(GFX_TRIANGLES, 3, 4) == area + 166);      /* align 4: the next index */
    CHECK(drains == 0);
    CHECK(gfx_block(GFX_QUADS, 300, 128) == area && drains == 1);   /* 192 + 300 > 409 */
    gfx_prim(GFX_QUADS, 4);
    gfx_end();
    CHECK(nd == 5);
    CHECK(draw_is(0, GFX_QUADS, 0, 4, NULL));
    CHECK(draw_is(1, GFX_QUADS, 32, 104, NULL));
    CHECK(draw_is(2, GFX_TRIANGLES, 136, 30, &t1));
    CHECK(draw_is(3, GFX_TRIANGLES, 166, 3, &t1));
    CHECK(draw_is(4, GFX_QUADS, 0, 304, &t1));
    reset();
    gfx_begin();
    gfx_prim(GFX_QUADS, 4);
    CHECK(gfx_block(GFX_TRIANGLES, 405, 4) == area + 4 && drains == 0);   /* 4 + 405 = the area */
    CHECK(gfx_block(GFX_QUADS, 4, 4) == area && drains == 1);
    gfx_end();
    CHECK(nd == 3 && draw_is(1, GFX_TRIANGLES, 4, 405, &t1) && draw_is(2, GFX_QUADS, 0, 4, &t1));
    reset();
    CHECK(gfx_block(GFX_QUADS, 8, 128) == area);              /* outside a frame: dropped */
    gfx_end();
    CHECK(nd == 0 && drains == 0);
    gfx_texture(NULL);
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

/* gfx_end is gfx_submit, then gfx_wait. A submitted frame gets one wait:
 * from gfx_wait, the next gfx_begin, a gfx_prim or gfx_block outside a frame
 * (its writes go to the area), or gfx_exit. A gfx_wait with nothing pending
 * and a gfx_submit outside a frame do nothing. */
static void submit(void)
{
    reset();
    gfx_begin();
    gfx_prim(GFX_QUADS, 4);
    gfx_end();
    CHECK(ends == 1 && waits == 1);
    gfx_wait();
    CHECK(waits == 1);

    reset();
    gfx_begin();
    gfx_prim(GFX_QUADS, 4);
    gfx_submit();
    CHECK(ends == 1 && waits == 0);
    gfx_submit();
    CHECK(ends == 1 && waits == 0);
    gfx_begin();
    CHECK(begins == 2 && waits == 1);
    CHECK(gfx_prim(GFX_QUADS, 4) == area);           /* after the wait the vertices start at index 0 */
    gfx_submit();
    gfx_wait();
    gfx_wait();
    CHECK(ends == 2 && waits == 2);

    reset();
    CHECK(gfx_prim(GFX_QUADS, 4) == area && waits == 0);     /* outside a frame, nothing pending */
    gfx_begin();
    gfx_prim(GFX_QUADS, 4);
    gfx_submit();
    CHECK(gfx_prim(GFX_QUADS, 4) == area && waits == 1);
    gfx_begin();
    gfx_prim(GFX_QUADS, 4);
    gfx_submit();
    CHECK(gfx_block(GFX_QUADS, 4, 4) == area && waits == 2);
    gfx_end();                                       /* no frame: no submit, nothing to wait for */
    CHECK(ends == 2 && waits == 2);

    reset();
    gfx_begin();
    gfx_prim(GFX_QUADS, 4);
    gfx_submit();
    gfx_exit();
    CHECK(ends == 1 && waits == 1 && exits == 1);
}

/* The copy: src and bytes multiples of 128, bytes 1 to GFX_COPY_MAX, src in
 * RSX memory (the back end's rule). gfx_copy_to_main runs outside a frame,
 * after the wait of a submitted frame. */
static void copy(void)
{
    static u8 dst[512];
    for (unsigned i = 0; i < sizeof vram; i++) vram[i] = (u8)(i * 7 + 1);
    reset();
    CHECK(gfx_copy_to_main(vram, dst, 0) == GFX_COPY_EARG);
    CHECK(gfx_copy_to_main(vram + 64, dst, 128) == GFX_COPY_EARG);
    CHECK(gfx_copy_to_main(vram, dst, 100) == GFX_COPY_EARG);
    CHECK(gfx_copy_to_main(vram, dst, GFX_COPY_MAX + 128) == GFX_COPY_EARG);
    CHECK(gfx_copy_to_main_job(vram + 4, dst, 128) == GFX_COPY_EARG);
    CHECK(copies == 0);
    CHECK(gfx_copy_to_main(vram + sizeof vram - 128, dst, 256) == GFX_COPY_ERANGE);
    memset(dst, 0, sizeof dst);
    CHECK(gfx_copy_to_main(vram + 256, dst + 1, 384) == 0);   /* dst has no rule */
    CHECK(!memcmp(dst + 1, vram + 256, 384) && dst[0] == 0 && dst[385] == 0);
    gfx_begin();
    CHECK(gfx_copy_to_main(vram, dst, 128) == GFX_COPY_EFRAME);
    gfx_prim(GFX_QUADS, 4);
    gfx_submit();
    CHECK(waits == 0);
    CHECK(gfx_copy_to_main(vram, dst, 128) == 0 && waits == 1);
    CHECK(copies == 3 && ends == 1);
}

/* gfx_copy_to_main_job: the request runs at the drawing thread's next
 * gfx_wait, between frames; one at a time; taken back when no frame ends
 * in 10,000 naps (about 2 s). */
static void copy_job(void)
{
    reset();
    nap_mode = 0;
    CHECK(gfx_copy_to_main_job(vram, job_dst, 128) == GFX_COPY_ENOFRAME);
    CHECK(naps == 10000 && copies == 0);
    reset();
    nap_mode = 1;
    memset(job_dst, 0, sizeof job_dst);
    CHECK(gfx_copy_to_main_job(vram + 128, job_dst, 256) == 0);
    CHECK(naps == 1 && copies == 1 && begins == 1 && ends == 1 && waits == 1);
    CHECK(!memcmp(job_dst, vram + 128, 256));
    reset();
    nap_mode = 2;
    nested_rc = 0;
    CHECK(gfx_copy_to_main_job(vram + sizeof vram, job_dst, 128) == GFX_COPY_ERANGE);   /* the back end's code comes back */
    CHECK(nested_rc == GFX_COPY_EBUSY && naps == 1 && copies == 1);
    reset();
    nap_mode = 1;
    CHECK(gfx_copy_to_main_job(vram, job_dst, 128) == 0 && copies == 1);   /* the slot is free again */
    nap_mode = 0;
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
    block();
    viewport();
    submit();
    copy();
    copy_job();
    frames();
    if (fails) {
        printf("%d checks failed\n", fails);
        return 1;
    }
    printf("test_gfx: all checks passed\n");
    return 0;
}
