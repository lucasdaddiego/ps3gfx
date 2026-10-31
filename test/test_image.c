/* The image decoder on the host (image.c, stb_image): the fixtures of
 * test/fixtures (made by PIL: a PNG with alpha, a baseline and a
 * progressive JPEG, a 1 x 1 PNG, a wide JPEG, a PNG past the side limit),
 * the sizes, the scale, a pixel's colour, the arena's limit, a cut file, a
 * file that is no image. Run from the repo root. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static unsigned char *load(const char *name, int *n)
{
    char path[256];
    snprintf(path, sizeof path, "test/fixtures/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("no fixture %s\n", path); exit(1); }
    static unsigned char buf[65536];
    *n = (int)fread(buf, 1, sizeof buf, f);
    fclose(f);
    return buf;
}

static int near(unsigned a, unsigned b, int tol) { return (int)a - (int)b <= tol && (int)b - (int)a <= tol; }
static unsigned R(uint32_t t) { return (t >> 16) & 255; }
static unsigned G(uint32_t t) { return (t >> 8) & 255; }
static unsigned B(uint32_t t) { return t & 255; }

int main(void)
{
    static uint32_t out[640 * 360];
    static unsigned char arena[IMAGE_ARENA_MIN];
    int n, w, h;
    unsigned char *d;

    /* image_fit: the aspect kept, never up */
    CHECK(image_fit(240, 160, 640, 360, &w, &h) == 0 && w == 240 && h == 160);
    CHECK(image_fit(1600, 900, 640, 360, &w, &h) == 0 && w == 640 && h == 360);
    CHECK(image_fit(1600, 1600, 640, 360, &w, &h) == 0 && w == 360 && h == 360);
    CHECK(image_fit(800, 200, 640, 360, &w, &h) == 0 && w == 640 && h == 160);
    CHECK(image_fit(0, 10, 640, 360, &w, &h) == -1);

    /* the PNG with alpha: its size, the gradient, the alpha dropped */
    d = load("gradient.png", &n);
    CHECK(image_info(d, n, &w, &h) == 0 && w == 97 && h == 61);
    CHECK(image_decode(d, n / 2, out, 640, 360, &w, &h, arena, sizeof arena) == -1);   /* a PNG cut in half: refused */
    CHECK(image_decode(d, n, out, 640, 360, &w, &h, arena, sizeof arena) == 0 && w == 97 && h == 61);
    CHECK((out[0] >> 24) == 255);
    CHECK(near(R(out[0]), 0, 2) && near(B(out[0]), 255, 2) && near(G(out[0]), 40, 2));        /* x 0: blue */
    CHECK(near(R(out[47]), 125, 3) && near(B(out[47]), 130, 3));                             /* x 47: the middle, opaque */
    CHECK(near(R(out[96]), 255, 2) && near(B(out[96]), 0, 2) && near(G(out[96]), 40, 2));      /* x 96: red; the PNG's alpha 128 is dropped */

    /* the JPEGs: baseline and progressive decode alike; the sun's colour; the ground */
    for (int k = 0; k < 2; k++) {
        d = load(k ? "photo_prog.jpg" : "photo.jpg", &n);
        CHECK(image_info(d, n, &w, &h) == 0 && w == 240 && h == 160);
        CHECK(image_decode(d, n, out, 640, 360, &w, &h, arena, sizeof arena) == 0 && w == 240 && h == 160);
        uint32_t sun = out[45 * 240 + 175], ground = out[130 * 240 + 20];
        CHECK(near(R(sun), 250, 12) && near(G(sun), 240, 12) && near(B(sun), 120, 16));
        CHECK(near(R(ground), 120, 8) && near(G(ground), 80, 8) && near(B(ground), 40, 10));
        /* scaled to fit 120 x 80: the same colours at the same places */
        CHECK(image_decode(d, n, out, 120, 80, &w, &h, arena, sizeof arena) == 0 && w == 120 && h == 80);
        sun = out[22 * 120 + 87];
        ground = out[65 * 120 + 10];
        CHECK(near(R(sun), 250, 16) && near(B(sun), 120, 24));
        CHECK(near(R(ground), 120, 8) && near(G(ground), 80, 8));
        /* a JPEG cut in half: the baseline one decodes (stb fills what is missing), the progressive one is refused;
         * the fetch's byte count, not the decoder, tells a cut */
        int cut = image_decode(d, n / 2, out, 640, 360, &w, &h, arena, sizeof arena);
        CHECK(cut == -1 || (cut == 0 && w == 240 && h == 160));
    }

    /* 1 x 1, white; the wide one scaled to the width; the tall one past the limit */
    d = load("dot.png", &n);
    CHECK(image_decode(d, n, out, 640, 360, &w, &h, arena, sizeof arena) == 0 && w == 1 && h == 1 && out[0] == 0xffffffffu);
    d = load("wide.jpg", &n);
    CHECK(image_decode(d, n, out, 640, 360, &w, &h, arena, sizeof arena) == 0 && w == 640 && h == 160);
    CHECK(near(R(out[80 * 640 + 320]), 0, 3) && near(G(out[80 * 640 + 320]), 128, 3) && near(B(out[80 * 640 + 320]), 255, 3));
    d = load("tall.png", &n);
    CHECK(image_info(d, n, &w, &h) == -1 || (w == 10 && h == 1700));      /* stb refuses the size in info or in the decode */
    CHECK(image_decode(d, n, out, 640, 360, &w, &h, arena, sizeof arena) == -1);

    /* no image; no arena; an arena too small for the JPEG */
    CHECK(image_info("couch-board 1\nnow=1\nend\n", 20, &w, &h) == -1);
    CHECK(image_decode("couch-board 1\nnow=1\nend\n", 20, out, 640, 360, &w, &h, arena, sizeof arena) == -1);
    d = load("photo.jpg", &n);
    CHECK(image_decode(d, n, out, 640, 360, &w, &h, NULL, 0) == -1);
    CHECK(image_decode(d, n, out, 640, 360, &w, &h, arena, 4096) == -2);

    printf(fails ? "%d FAIL\n" : "PASS image: the fixtures decode, scale and refuse as the contract says\n", fails);
    return fails ? 1 : 0;
}
