/* image.h: stb_image configured for the PS3 (no stdio, no SIMD, no thread
 * locals, PNG and JPEG only) on a bump allocator over the caller's arena,
 * then the area-average scale into ARGB texels. The decoder runs on the
 * caller's thread; one decode at a time (the arena is bound to the call). */
#include <string.h>
#include "image.h"

/* ---- the arena: a bump allocator with a size header, so a realloc copies ---- */

static struct { unsigned char *base; size_t n, used; int oom; } A;
static unsigned char scratch[96 << 10];      /* image_info's arena: the JPEG reader's state (about 20 KB) before any pixel */

static void *a_malloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (!A.base || n > A.n - A.used) {
        A.oom = 1;
        return NULL;
    }
    void *p = A.base + A.used;
    A.used += n;
    return p;
}

static void bind(void *arena, size_t n)
{
    A.base = (unsigned char *)arena;
    A.n = n;
    A.used = 0;
    A.oom = 0;
}

static void *a_realloc_sized(void *p, size_t old, size_t n)
{
    if (!p) return a_malloc(n);
    if (n <= old) return p;
    void *q = a_malloc(n);
    if (q) memcpy(q, p, old);
    return q;
}

static void a_free(void *p) { (void)p; }   /* the arena is reset at the end of the call */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS IMAGE_MAX_SIDE
#define STBI_MALLOC(n) a_malloc(n)
#define STBI_REALLOC_SIZED(p, old, n) a_realloc_sized(p, old, n)
#define STBI_FREE(p) a_free(p)
#define STBI_ASSERT(x) ((void)0)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

int image_info(const void *data, int n, int *w, int *h)
{
    int c;
    if (!data || n <= 0) return -1;
    bind(scratch, sizeof scratch);
    int ok = stbi_info_from_memory((const stbi_uc *)data, n, w, h, &c);
    bind(NULL, 0);
    return ok ? 0 : -1;
}

int image_fit(int sw, int sh, int max_w, int max_h, int *w, int *h)
{
    if (sw <= 0 || sh <= 0 || max_w <= 0 || max_h <= 0) return -1;
    int dw = sw, dh = sh;
    if (dw > max_w) {
        dh = (int)((long long)sh * max_w / sw);
        dw = max_w;
    }
    if (dh > max_h) {
        dw = (int)((long long)dw * max_h / dh);
        dh = max_h;
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    *w = dw;
    *h = dh;
    return 0;
}

/* The area average of the source rect of each destination pixel: whole
 * source pixels (the rects of a column or a row differ by one pixel when
 * the ratio is not whole). */
static void scale_rgb(const unsigned char *src, int sw, int sh, uint32_t *out, int dw, int dh)
{
    for (int y = 0; y < dh; y++) {
        int y0 = (int)((long long)y * sh / dh), y1 = (int)((long long)(y + 1) * sh / dh);
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = (int)((long long)x * sw / dw), x1 = (int)((long long)(x + 1) * sw / dw);
            if (x1 <= x0) x1 = x0 + 1;
            unsigned r = 0, g = 0, b = 0, k = 0;
            for (int yy = y0; yy < y1; yy++) {
                const unsigned char *p = src + ((size_t)yy * sw + x0) * 3;
                for (int xx = x0; xx < x1; xx++, p += 3) {
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    k++;
                }
            }
            r = (r + k / 2) / k;
            g = (g + k / 2) / k;
            b = (b + k / 2) / k;
            out[(size_t)y * dw + x] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
}

int image_decode(const void *data, int n, uint32_t *out, int max_w, int max_h, int *w, int *h, void *arena, size_t arena_n)
{
    int sw, sh, c, rc;
    if (!data || n <= 0 || !out || !arena || max_w <= 0 || max_h <= 0) return -1;
    bind(arena, arena_n);
    if (!stbi_info_from_memory((const stbi_uc *)data, n, &sw, &sh, &c) || sw > IMAGE_MAX_SIDE || sh > IMAGE_MAX_SIDE) rc = A.oom ? -2 : -1;
    else {
        unsigned char *px = stbi_load_from_memory((const stbi_uc *)data, n, &sw, &sh, &c, 3);   /* RGB: a PNG's alpha is dropped */
        if (!px) rc = A.oom ? -2 : -1;       /* the arena ran out, or a bad or cut file */
        else {
            image_fit(sw, sh, max_w, max_h, w, h);
            scale_rgb(px, sw, sh, out, *w, *h);
            rc = 0;
        }
    }
    bind(NULL, 0);
    return rc;
}
