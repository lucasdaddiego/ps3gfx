/* The portable front end of gfx: the vertex area, the batching and the
 * viewport. The console (gfx_rsx.c) and the host preview build this file. */
#include <stddef.h>
#include <stdint.h>
#include "gfx.h"

#ifndef GFX_VTX_BYTES
#define GFX_VTX_BYTES (960 * 1024)           /* the vertex area; the preview tests smaller ones */
#endif

int gfx_w = 1920, gfx_h = 1080;

static gfx_vtx *area;                        /* the vertices of the frame, from index 0 */
static u32 cap, used;                        /* the area in vertices, and the part in use */
static float vp[4] = {0, 0, 1, 1};           /* the viewport: tx, ty, sx, sy */
static const gfx_tex *tex;                   /* the texture of the next primitives */
static int in_frame;
static int pending;                          /* a submitted frame gfx_wait has not waited for */
static u32 n_vtx, n_draws, n_drains;         /* this frame's counts */
static u32 last_vtx, last_draws, last_drains;    /* the last frame's, at its gfx_end */
static void copy_serve(void);

/* The draw that the next primitives can still join. */
static struct {
    int type;
    const gfx_tex *tex;
    u32 first, n;
} open;

long long gfx_be_us[2];

int gfx_init(void)
{
    cap = GFX_VTX_BYTES / sizeof(gfx_vtx);
    if (cap < GFX_PRIM_MAX) return -1;       /* a build error: no room for one primitive */
    return gfx_be_init(GFX_VTX_BYTES, &area);
}

void *gfx_tex_new(gfx_tex *t, u16 w, u16 h, u16 format, u16 linear)
{
    if (!w || !h || w > 4096 || h > 4096 || format > GFX_TEX_A8R8G8B8) return NULL;
    u32 pitch = ((u32)w * (format == GFX_TEX_A8R8G8B8 ? 4 : 2) + 63) & ~63u, offset;
    void *p = gfx_vram(pitch * h, 128, &offset);
    if (!p) return NULL;
    t->offset = offset;
    t->w = w;
    t->h = h;
    t->pitch = (u16)pitch;
    t->linear = linear;
    t->format = format;
    return p;
}

/* Sends the open draw to the back end. */
static void close_draw(void)
{
    if (open.n) {
        gfx_be_draw(open.type, open.first, open.n, open.tex);
        n_vtx += open.n;
        n_draws++;
    }
    open.n = 0;
}

void gfx_viewport(float tx, float ty, float sx, float sy)
{
    close_draw();                            /* the vertices so far keep the old viewport */
    vp[0] = tx;
    vp[1] = ty;
    vp[2] = sx;
    vp[3] = sy;
    if (in_frame) gfx_be_viewport(vp);
}

/* After the wait for the last frame the RSX has read its vertices, so the
 * vertices start again at index 0. */
void gfx_begin(void)
{
    if (in_frame) return;
    gfx_wait();
    in_frame = 1;
    used = 0;
    gfx_be_begin(vp);
}

void gfx_texture(const gfx_tex *t) { tex = t; }

/* Consecutive QUADS or TRIANGLES with the same texture join one draw. A
 * strip or a fan is a draw of its own: one more vertex would continue it. */
gfx_vtx *gfx_prim(int type, int n)
{
    if (n < 1 || n > GFX_PRIM_MAX) return NULL;      /* a caller error: a crash, not a write past the area */
    if (!in_frame) {                         /* the dropped writes go to the area: not while the RSX reads it */
        gfx_wait();
        return area;
    }
    if (used + n > cap) {                    /* the area is full: wait until the RSX read it, then reuse it */
        close_draw();
        gfx_be_drain();
        n_drains++;
        used = 0;
    }
    if (!open.n || open.type != type || open.tex != tex || (type != GFX_QUADS && type != GFX_TRIANGLES)) {
        close_draw();
        open.type = type;
        open.tex = tex;
        open.first = used;
    }
    open.n += n;
    used += n;
    return area + used - n;
}

/* 20-byte vertices meet an align-byte line every align / 4 vertices (128:
 * every 32 vertices, 640 bytes); the area starts on a 128-byte line. */
gfx_vtx *gfx_block(int type, u32 n, u32 align)
{
    if (type != GFX_QUADS && type != GFX_TRIANGLES) return NULL;
    if (!n || n > cap || n % (type == GFX_QUADS ? 4 : 3) || !align || align > 128 || (align & (align - 1))) return NULL;
    if (!in_frame) {
        gfx_wait();
        return area;
    }
    u32 step = align > 4 ? align / 4 : 1, first = (used + step - 1) / step * step;
    close_draw();
    if (first + n > cap) {                   /* as in gfx_prim: wait until the RSX read the area, then reuse it */
        gfx_be_drain();
        n_drains++;
        first = 0;
    }
    open.type = type;
    open.tex = tex;
    open.first = first;
    open.n = n;
    used = first + n;
    return area + first;
}

void gfx_submit(void)
{
    if (!in_frame) return;
    close_draw();
    in_frame = 0;
    last_vtx = n_vtx;
    last_draws = n_draws;
    last_drains = n_drains;
    n_vtx = n_draws = n_drains = 0;
    gfx_be_submit();
    pending = 1;
}

void gfx_wait(void)
{
    if (!pending) return;
    pending = 0;
    gfx_be_wait();
    copy_serve();
}

void gfx_end(void)
{
    gfx_submit();
    gfx_wait();
}

void gfx_last_counts(u32 *vertices, u32 *draws, u32 *drains)
{
    if (vertices) *vertices = last_vtx;
    if (draws) *draws = last_draws;
    if (drains) *drains = last_drains;
}

/* At exit, as PSL1GHT's samples do: the RSX runs the words after the last
 * flush, and gfx waits until it has. */
void gfx_exit(void)
{
    gfx_end();
    gfx_be_exit();
}

/* ---- the copy --------------------------------------------------------------- */

/* A job's copy request, from gfx_copy_to_main_job to the drawing thread's
 * next gfx_wait: 0 none, 1 being written, 2 posted, 3 taken, 4 done, 5 taken
 * and given up by the job (the drawing thread frees the slot at the end). */
static volatile int creq;
static const void *creq_src;
static void *creq_dst;
static u32 creq_bytes;
static volatile int creq_rc;
#define JOB_POLLS 10000                      /* naps of 200 us: about 2 s */

static int copy_args(const void *src, u32 bytes)
{
    if (!bytes || bytes > GFX_COPY_MAX || (((uintptr_t)src | bytes) & 127)) return GFX_COPY_EARG;
    return 0;
}

/* In the drawing thread, between frames: the vertex area is free. */
static void copy_serve(void)
{
    if (creq != 2 || !__sync_bool_compare_and_swap(&creq, 2, 3)) return;
    creq_rc = gfx_be_copy(creq_src, creq_dst, creq_bytes);
    __sync_synchronize();
    if (!__sync_bool_compare_and_swap(&creq, 3, 4)) creq = 0;   /* 5: no one waits for it */
}

int gfx_copy_to_main(const void *src, void *dst, u32 bytes)
{
    int rc = copy_args(src, bytes);
    if (rc) return rc;
    if (in_frame) return GFX_COPY_EFRAME;
    gfx_wait();
    return gfx_be_copy(src, dst, bytes);
}

/* A request that no frame took in about 2 s is taken back (ENOFRAME). A
 * taken one is waited for: each round of the drawing thread ends within
 * about 2 s. After 8 s more the call gives up (ETIME): dst may still be
 * written, and later calls get EBUSY until the drawing thread ends the copy. */
int gfx_copy_to_main_job(const void *src, void *dst, u32 bytes)
{
    int rc = copy_args(src, bytes);
    if (rc) return rc;
    if (!__sync_bool_compare_and_swap(&creq, 0, 1)) return GFX_COPY_EBUSY;
    creq_src = src;
    creq_dst = dst;
    creq_bytes = bytes;
    __sync_synchronize();
    creq = 2;
    for (int i = 0; creq != 4; i++) {
        if (i >= JOB_POLLS && __sync_bool_compare_and_swap(&creq, 2, 0)) return GFX_COPY_ENOFRAME;
        if (i >= 5 * JOB_POLLS && __sync_bool_compare_and_swap(&creq, 3, 5)) return GFX_COPY_ETIME;
        gfx_be_nap();
    }
    __sync_synchronize();
    rc = creq_rc;
    creq = 0;
    return rc;
}
