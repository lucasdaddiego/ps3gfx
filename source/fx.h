/* Drawing helpers on the public gfx API (fx.c): parts of textures, textured
 * rounded rectangles for frosted glass, a 9-slice halo, a turned quad, a
 * polyline and the fill under it, anti-aliased lines and shapes, and two
 * textures that take turns (a picture that an SPU writes while the RSX
 * draws the other). They change no back end and make no LV2 call; fx.c is
 * an archive member of its own, so an app links it only when it calls one.
 * Draw them between begin_frame and ui_flip, as the kit. A NULL texture
 * draws the tint alone. */
#ifndef FX_H
#define FX_H

#include <ppu-types.h>
#include "gfx.h"

/* The part (u0, v0) to (u1, v1) of a texture (0 to 1 over its texels) into
 * a rect, in the corner order of rect; tint 0xffffffff shows the texels as
 * they are. tex_rect of ui.h is tex_sub with 0, 0, 1, 1. */
void tex_sub(const gfx_tex *t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 tint);
/* The same in a rounded rect of radius r, the outline of round_rect: one
 * fan around the centre, each point with the UV of its place in the rect. */
void tex_round(const gfx_tex *t, float x, float y, float w, float h, float r, float u0, float v0, float u1, float v1,
               u32 tint);
/* The UVs of a canvas rect in a texture that covers the screen (1920 x
 * 1080 from screen_origin): u = (x - ox) / 1920, v = (y - oy) / 1080. A tile
 * drawn with them shows the part of a full-screen picture behind it, also
 * while the canvas moves (a slide by safe_l and safe_r, the ambient drift). */
void glass_uv(float x, float y, float w, float h, float *u0, float *v0, float *u1, float *v1);
/* A halo around the rect x, y, w, h: the 8 outer slices of a 9-slice (no
 * centre: the rect itself stays clear), each corner a 2 pad square centred
 * on the rect's corner with one quarter of the texture, the edges the
 * texture's middle row or column stretched. pad is at most half the rect's
 * smaller side. */
void glow9(const gfx_tex *t, float x, float y, float w, float h, float pad, u32 tint);
/* A w x h quad centred at cx, cy, turned by angle (radians, clockwise on the
 * screen). At angle 0 it is tex_sub of the rect cx - w / 2, cy - h / 2. */
void quad_rot(const gfx_tex *t, float cx, float cy, float w, float h, float angle, float u0, float v0, float u1,
              float v1, u32 tint);
/* A polyline of n points (x, y pairs in pts) width px wide: one triangle
 * strip with mitred joins (a miter at most twice the half width), so a
 * translucent line covers no pixel twice. */
void strip(const float *pts, int n, float width, u32 rgba);
/* The fill under a polyline down to base_y: rgba_top at the points,
 * rgba_base at base_y (a fade with a lower alpha there). */
void area(const float *pts, int n, float base_y, u32 rgba_top, u32 rgba_base);

/* Anti-aliased lines and shapes, untextured: every edge fades from the
 * colour's alpha to 0 over 1 px, so a slanted or curved edge reads smooth
 * at 1080p (the RSX draws with no multisampling). A w px line is a core of
 * w - 1 px at the colour and a 1 px feather on each side: the same ink as
 * a w px line (under 1 px: no core, the alpha times w). Quads that touch
 * and never overlap, so a translucent line covers no pixel twice, sent as
 * GFX_QUADS: consecutive calls join one draw. The alpha test drops the
 * feather's last 16th. */
/* A segment, its ends feathered too (the ink ends at the end points). */
void aa_line(float x0, float y0, float x1, float y1, float w, u32 rgba);
/* n separate segments of 4 floats each (x0, y0, x1, y1): dashes, ticks. */
void aa_lines(const float *seg, int n, float w, u32 rgba);
/* A polyline of n points with the mitred joins of strip, its two ends
 * feathered. */
void aa_strip(const float *pts, int n, float w, u32 rgba);
/* A circle's outline, w px wide, centred on radius r. */
void aa_ring(float cx, float cy, float r, float w, u32 rgba);
/* A filled circle: the ink ends at radius r. */
void aa_disc(float cx, float cy, float r, u32 rgba);
/* A filled convex polygon of n points (either winding), its ink to the
 * points' outline. */
void aa_poly(const float *pts, int n, u32 rgba);
/* The fringe of round_rect's corners: 1 px outside each corner's arc, from
 * half the alpha at the arc to 0, so a rounded rect drawn with pixel
 * centres (round_rect, tex_round) gets about the coverage its corners
 * miss. The straight edges get none: on whole pixels they are exact. */
void aa_corners(float x, float y, float w, float h, float r, u32 rgba);

/* A picture from main memory into a texture: t->h rows of the texels of
 * t->w (whole 32-bit words), from src (rows of src_pitch_words words) to
 * dst (the address gfx_tex_new gave), in 32-bit stores (the fastest into
 * RSX memory on the console). Not while a frame that draws t is in flight. */
void tex_upload(const gfx_tex *t, void *dst, const void *src, u32 src_pitch_words);

/* Two textures of one size that take turns: the frame draws the shown one
 * while a writer (the PPU, or an SPU by DMA: addr is its effective address)
 * fills the other; tex_pair_swap, after ui_flip returned and the writer
 * finished, shows the new picture. The two have different gfx_tex pointers,
 * so each swap binds anew and the RSX reads no stale texture cache. */
typedef struct {
    gfx_tex tex[2];
    void *addr[2];                           /* row 0 of each, from gfx_tex_new */
    int shown;                               /* 0 or 1 */
} tex_pair;
int tex_pair_new(tex_pair *p, u16 w, u16 h, u16 format, u16 linear);   /* 0, or -1 when RSX memory is short */
const gfx_tex *tex_pair_shown(const tex_pair *p);
const gfx_tex *tex_pair_wtex(const tex_pair *p);   /* the one being written */
void *tex_pair_written(const tex_pair *p);         /* its address */
void tex_pair_swap(tex_pair *p);

#endif
