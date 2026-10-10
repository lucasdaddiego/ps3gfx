/* The kit's host check: an app on the frame for the host only, as the smoke
 * app, that calls the kit and compares the results with the rules of ui.h
 * and gfx.h. CI runs it with a pad script that holds DOWN on frames 10 to 70
 * and again on 80 to 82:
 *   zsh tools/preview/build.zsh APP=tools/preview/kitcheck
 *   PV_ROOT=<sandbox with dev_hdd0/tmp> PV_OUT=<folder> \
 *       PV_SCRIPT="10-70:DOWN 80-82:DOWN" tools/preview/kitcheck/tools/preview/build/pv
 * It prints each failed check and exits 1, or prints "kitcheck: all checks
 * passed" and exits 0 after frame 100. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/gfx.h>
#include <ps3gfx/fs.h>

#define DIR "/dev_hdd0/tmp/ps3gfx_kitcheck"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* gfx_tex_new: rows rounded up to 64 bytes, the ranges */
static void textures(void)
{
    gfx_tex t;
    CHECK(gfx_tex_new(&t, 4, 4, GFX_TEX_A8R8G8B8, 0) && t.pitch == 64 && t.w == 4 && t.h == 4 && t.format == GFX_TEX_A8R8G8B8 && !t.linear);
    CHECK(gfx_tex_new(&t, 100, 10, GFX_TEX_A4R4G4B4, 1) && t.pitch == 256 && t.linear == 1 && t.format == GFX_TEX_A4R4G4B4);
    CHECK(gfx_tex_new(&t, 960, 540, GFX_TEX_A8R8G8B8, 1) && t.pitch == 3840 && t.offset % 128 == 0);
    CHECK(gfx_tex_new(&t, 416, 210, GFX_TEX_A4R4G4B4, 0) && t.pitch == 832);
    CHECK(!gfx_tex_new(&t, 0, 4, 0, 0) && !gfx_tex_new(&t, 4, 4097, 0, 0) && !gfx_tex_new(&t, 4, 4, 2, 0));
}

/* a block in the preview: the index of gfx.c, an address on a 128-byte line */
static void block(void)
{
    gfx_vtx *area = gfx_prim(GFX_QUADS, 4);                 /* outside a frame: the area's start */
    begin_frame();                                          /* the background: 4 vertices */
    gfx_vtx *b = gfx_block(GFX_QUADS, 400, 128);
    CHECK(b == area + 32 && ((uintptr_t)b & 127) == 0);
    for (int i = 0; i < 400; i++) gfx_put(b + i, 0, 0, 0, 0, 0);
    ui_flip();
}

int main(void)
{
    int mk, op, wr;
    ui_init(&(const ui_app){"ps3gfx kitcheck", "0.0", DIR});
    CHECK(fs_selftest(DIR, &mk, &op, &wr) == 0);
    atexit(ui_end);
    textures();
    block();

    ui_stat st;
    ui_stats(&st, 1);
    for (int f = 1; f <= 100; f++) {                        /* f is the preview's frame: block() drew frame 0 */
        read_pad();
        begin_frame();
        footer("L1 previous  R1 next  L1/R1 page  CROSS play");
        ui_flip();
    }

    /* ui_stats on the preview's clock: 16,667 us a frame, all of it in gfx_end */
    ui_stats(&st, 1);
    CHECK(st.frames == 100 && st.period_us == 16667 && st.wait_us == 16667 && st.busy_us == 0);
    CHECK(st.period_max_us == 16667 && st.slow == 0 && st.busy_sum_us == 0 && st.wait_sum_us == 100ull * 16667);
    CHECK(st.vertices > 0 && st.draws > 0 && st.drains == 0 && st.vertices_max >= st.vertices && st.drains_sum == 0);
    ui_stats(&st, 0);
    CHECK(st.frames == 0 && st.period_max_us == 0 && st.period_us == 16667);

    if (fails) {
        printf("kitcheck: %d checks failed\n", fails);
        exit(1);
    }
    printf("kitcheck: all checks passed\n");
    exit(0);
}
