/* gfx: the app's 2D renderer (README.md in this folder). A frame is
 * gfx_begin, then primitives, then gfx_end, which shows the frame and returns
 * when the RSX has drawn it, or gfx_submit, which returns at once, and
 * gfx_wait later. Positions are canvas units that gfx_viewport
 * maps to output pixels. gfx.c is the portable front end (batching, the
 * viewport, the vertex area); a back end draws: gfx_rsx.c on the console,
 * the preview's gfx_soft.c on the host. Plain C types only, so the preview
 * compiles this header too. */
#ifndef GFX_H
#define GFX_H

#include <ppu-types.h>

/* the primitive types, with the RSX's BEGIN_END values */
#define GFX_TRIANGLES      5
#define GFX_TRIANGLE_STRIP 6
#define GFX_TRIANGLE_FAN   7
#define GFX_QUADS          8
#define GFX_PRIM_MAX       256               /* the most vertices of one gfx_prim */

/* A vertex as the RSX reads it (20 bytes): the position (the RSX adds z = 0
 * and w = 1), the colour 0xRRGGBBAA, the texture coordinate. Write it with
 * gfx_put. */
typedef struct {
    float x, y;
    u32 rgba;
    float u, v;
} gfx_vtx;

/* A texture in RSX memory, rows of pitch bytes, clamped to the edge;
 * linear: bilinear filtering, else nearest; format: A4R4G4B4 (16-bit texels
 * 0xARGB, the font atlases') or A8R8G8B8 (32-bit 0xAARRGGBB). format is the
 * last field, so an initializer without it gets A4R4G4B4. gfx keeps the
 * pointer that gfx_texture gets until the frame ends, and compares textures
 * by it. */
#define GFX_TEX_A4R4G4B4 0
#define GFX_TEX_A8R8G8B8 1
typedef struct gfx_tex {
    u32 offset;
    u16 w, h, pitch, linear;
    u16 format;                              /* GFX_TEX_* */
} gfx_tex;

extern int gfx_w, gfx_h;                     /* the output in pixels, set by gfx_init */

int gfx_init(void);                          /* 0, or the number of the step that failed (gfx_be_init) */
/* gfx_be_init's slow calls in microseconds: gcmInitBody, videoConfigure (0 on the host) */
extern long long gfx_be_us[2];
/* RSX memory that is never given back, and its RSX offset when offset is not
 * NULL; NULL when it does not fit. After gfx_init only one thread at a time
 * may call it (the app: the RSX memory test). */
void *gfx_vram(u32 size, u32 align, u32 *offset);
/* A texture of w x h texels (1 to 4096 each) in that memory: rows of pitch
 * bytes, w texels rounded up to 64 bytes, from a 128-byte line. It fills *t
 * and returns the address of row 0, NULL when it does not fit or an argument
 * is out of range. The caller writes row y at the address + y * t->pitch;
 * a read of RSX memory is slow (about 0.5 us). */
void *gfx_tex_new(gfx_tex *t, u16 w, u16 h, u16 format, u16 linear);
void gfx_viewport(float tx, float ty, float sx, float sy);   /* pixel = canvas * s + t, at once */
void gfx_begin(void);
void gfx_texture(const gfx_tex *t);          /* for the next primitives; NULL: colour only */
/* Room for one primitive of n vertices (1 to GFX_PRIM_MAX); the caller
 * writes all n before its next gfx call. Outside a frame they are dropped. */
gfx_vtx *gfx_prim(int type, int n);
/* A block: room for n vertices (1 to the area, whole quads or triangles) of
 * one GFX_QUADS or GFX_TRIANGLES draw, in one piece, for a writer that needs
 * them contiguous (an SPU by DMA). The first vertex sits at a multiple of
 * align bytes (a power of two up to 128) from the start of the area, which is
 * on a 128-byte line. It starts a draw of its own, skips the vertices before
 * the aligned one (never drawn), and drains first when the block does not
 * fit. The rule of gfx_prim applies: the caller writes all n before its next
 * gfx call. NULL when type, n or align is out of range; outside a frame they
 * are dropped. */
gfx_vtx *gfx_block(int type, u32 n, u32 align);
/* gfx_submit ends the frame: the RSX gets its draws, the flip and the wait
 * for it, and gfx_submit returns at once, so the app can compute (the next
 * frame's logic) while the RSX draws and the TV waits for its vsync.
 * gfx_wait returns when that frame is on the TV and the RSX has read every
 * vertex; at once when no frame waits. Until gfx_wait returns, the frame's
 * vertices and the texels of every texture it drew are still read: write
 * neither. gfx_begin, and gfx_prim and gfx_block outside a frame, call
 * gfx_wait first. gfx_end is gfx_submit, then gfx_wait. */
void gfx_submit(void);
void gfx_wait(void);
void gfx_end(void);
void gfx_exit(void);                         /* at exit: the RSX runs what is left (at most about 2 s) */
/* The colour buffer on the TV, between a gfx_end (or a gfx_wait) and the next gfx_begin:
 * gfx_h rows of *pitch bytes, gfx_w words 0x??RRGGBB each (the top byte is
 * not alpha). It is RSX memory: about 0.5 us a read. NULL before gfx_init. */
const volatile u32 *gfx_front(u32 *pitch);
/* The last frame's counts at its gfx_end or gfx_submit: the vertices drawn,
 * the draws and the drains; a NULL pointer is skipped. */
void gfx_last_counts(u32 *vertices, u32 *draws, u32 *drains);

/* One vertex, and a pointer to the next. Each field is its own 4-byte store:
 * GCC 7.2 can merge plain stores into 8-byte ones, which RSX memory may not
 * take if the vertices ever move there. */
static inline gfx_vtx *gfx_put(gfx_vtx *p, float x, float y, u32 rgba, float u, float v)
{
    volatile gfx_vtx *q = p;
    q->x = x;
    q->y = y;
    q->rgba = rgba;
    q->u = u;
    q->v = v;
    return p + 1;
}

/* The back end, for gfx.c only. vp is tx, ty, sx, sy. A draw is n vertices
 * of the area from index first, t NULL for colour only. drain returns when
 * the RSX has read every vertex drawn so far. submit queues the end of the
 * frame (its label, the flip and the wait for it) and returns; wait returns
 * when the flip is done and the RSX has passed the label. */
int gfx_be_init(u32 vtx_bytes, gfx_vtx **area);
void gfx_be_begin(const float *vp);
void gfx_be_viewport(const float *vp);
void gfx_be_draw(int type, u32 first, u32 n, const gfx_tex *t);
void gfx_be_drain(void);
void gfx_be_submit(void);
void gfx_be_wait(void);
void gfx_be_exit(void);

#endif
