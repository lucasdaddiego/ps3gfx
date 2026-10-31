/* A PNG or JPEG decoded into ARGB texels (image.c, 0.3.0): stb_image (public
 * domain, source/stb_image.h) on the caller's memory, nothing kept after a
 * call. The caller gives the arena the decoder works in and the buffer the
 * texels go to, so the library makes no allocation: an app with a photo on
 * its screen keeps one arena and one texture (RSX memory is never given
 * back: gfx.h). */
#ifndef GFX_IMAGE_H
#define GFX_IMAGE_H

#include <stddef.h>
#include <stdint.h>

/* The limits. An image with a side past IMAGE_MAX_SIDE is refused before
 * the decode (stb_image's STBI_MAX_DIMENSIONS). IMAGE_ARENA_MIN bytes decode
 * any image within them: the RGB rows (1600 x 1600 x 3), a progressive
 * JPEG's coefficients (2 bytes a sample, 3 components) and the decoder's
 * line buffers, with room to spare. */
#define IMAGE_MAX_SIDE 1600
#define IMAGE_ARENA_MIN (24u << 20)

/* The image's size without a decode: 0 and *w, *h; -1 when data (n bytes)
 * is not a PNG or JPEG the decoder reads. */
int image_info(const void *data, int n, int *w, int *h);

/* data (n bytes, a PNG or JPEG) decoded into out: 0xAARRGGBB texels, alpha
 * 255 (a PNG's own alpha is dropped: the colour as stored), the image scaled down by area
 * averaging to fit max_w x max_h with its aspect kept, never scaled up; out
 * holds max_w * max_h texels, row y at out + y * *w (the result's own
 * width), *w and *h the result's size. arena (arena_n bytes) is the
 * decoder's memory for this call. 0; -1 when data is not an image, a cut
 * PNG, or a side is past IMAGE_MAX_SIDE (a cut JPEG decodes, its missing
 * part filled: the fetch's byte count tells a cut, not the decoder); -2
 * when the arena ran out (give it IMAGE_ARENA_MIN). */
int image_decode(const void *data, int n, uint32_t *out, int max_w, int max_h, int *w, int *h, void *arena, size_t arena_n);

/* The size image_decode gives data within max_w x max_h: 0 and *w, *h, or
 * image_info's -1. The texture to make before the decode. */
int image_fit(int sw, int sh, int max_w, int max_h, int *w, int *h);

#endif
