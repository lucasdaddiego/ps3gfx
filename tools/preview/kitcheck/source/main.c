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

static float via_vtext(float x, float y, int font, u32 color, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    float r = vtext(x, y, font, color, fmt, ap);
    va_end(ap);
    return r;
}

static float via_vtext_fit(float x, float y, int font, u32 color, float maxw, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    float r = vtext_fit(x, y, font, color, maxw, fmt, ap);
    va_end(ap);
    return r;
}

/* text_wrap_lines counts what text_wrap draws, for every font, width and line limit */
static void wrap_lines(void)
{
    static const char *const texts[] = {
        "The counter runs while the console works. If it stops, the console froze.",
        "Hold the power button, then start the app again: it skips the frozen step.",
        "short",
        "one\ntwo\n\nfour lines, the third one empty",
        "Averyveryveryverylongwordwithoutanyspacethatcannotbreakanywhereatall and more words after it",
        "  leading spaces and trailing ones   ",
        "",
    };
    int n = 0;
    for (unsigned t = 0; t < sizeof texts / sizeof texts[0]; t++)
        for (int f = 0; f < F_COUNT; f++)
            for (int w = 60; w <= 1800; w += 70)
                for (int m = 1; m <= 6; m++) {
                    int drawn = text_wrap(0, 0, f, WHITE, (float)w, m, texts[t]);
                    CHECK(text_wrap_lines(f, (float)w, m, texts[t]) == drawn);
                    n++;
                }
    printf("text_wrap_lines: %d cases\n", n);
}

/* the va_list calls give the x of the variadic ones, also past the 319-byte cut */
static void va_calls(void)
{
    char big[400];
    memset(big, 'm', sizeof big - 1);
    big[sizeof big - 1] = 0;
    for (int f = 0; f < F_COUNT; f++) {
        CHECK(text(10, 20, f, WHITE, "%d of %s", 42, "x") == via_vtext(10, 20, f, WHITE, "%d of %s", 42, "x"));
        CHECK(text(10, 20, f, WHITE, "%s", big) == via_vtext(10, 20, f, WHITE, "%s", big));
        CHECK(text_fit(10, 20, f, WHITE, 300, "%s", big) == via_vtext_fit(10, 20, f, WHITE, 300, "%s", big));
        CHECK(text(0, 0, f, WHITE, "%s", big) == text(0, 0, f, WHITE, "%.319s", big));
        CHECK(text(0, 0, f, WHITE, "%s", big) > text(0, 0, f, WHITE, "%.318s", big));   /* the cut is at 319, not before */
    }
}

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

/* num_field: one width for every value of its digits */
static void fields(void)
{
    float a = num_field(100, 100, F_LARGE, 0, 3, 0), b = num_field(100, 100, F_LARGE, 255, 3, 1);
    float c = num_field(100, 100, F_LARGE, 7, 3, 0), d = num_field(100, 100, F_LARGE, 7, 4, 0);
    CHECK(a == b && a == c && d > c && a > 100);
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
    wrap_lines();
    va_calls();
    textures();
    fields();
    block();

    /* pad_repeat: the press, then 350 ms, then every 30 ms; groups apart; a new press after a release */
    pad_rep down = {0}, side = {0};
    unsigned fired[128] = {0};
    ui_stat st;
    ui_stats(&st, 1);
    for (int f = 1; f <= 100; f++) {                        /* f is the preview's frame: block() drew frame 0 */
        read_pad();
        fired[f] = pad_repeat(&down, BTN_DOWN | BTN_UP);
        CHECK(!pad_repeat(&side, BTN_LEFT | BTN_RIGHT));
        begin_frame();
        footer("L1 previous  R1 next  L1/R1 page  CROSS play");
        ui_flip();
    }
    int want[128] = {0}, ok = 1;
    want[10] = want[80] = 1;
    for (int f = 31; f <= 70; f += 2) want[f] = 1;
    for (int f = 1; f <= 100; f++)
        if (!!fired[f] != want[f] || (fired[f] && fired[f] != BTN_DOWN)) { printf("FAIL pad_repeat at frame %d: %x\n", f, fired[f]); ok = 0; }
    CHECK(ok);

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
