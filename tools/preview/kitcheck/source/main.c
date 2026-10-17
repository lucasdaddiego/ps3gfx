/* The kit's host check: an app on the frame for the host only, as the smoke
 * app, that calls the kit and compares the results with the rules of ui.h
 * and gfx.h. CI runs it with a pad script that holds DOWN on frames 10 to 70
 * and again on 80 to 82, and a snapshot of frame 50:
 *   zsh tools/preview/build.zsh APP=tools/preview/kitcheck
 *   PV_ROOT=<sandbox with dev_hdd0/tmp> PV_OUT=<folder> \
 *       PV_SCRIPT="10-70:DOWN 80-82:DOWN 50:SNAP=kit" tools/preview/kitcheck/tools/preview/build/pv
 * It prints each failed check and exits 1, or prints "kitcheck: all checks
 * passed" and exits 0 after frame 100. Frame 50's ui_shot, shot.ppm in the
 * app folder, must equal the snapshot kit.ppm byte for byte (CI: cmp), and
 * the front buffer's rows come by gfx_copy_to_main, not by ui_shot's reads.
 * Before them, gfx_copy_to_main between frames (a memcpy here).
 * Then gesture's two presses on the same pad frames, the text calls' returns
 * and the bytes past ASCII before text_utf8, and at the end a run_job whose
 * job copies by gfx_copy_to_main_job.
 * After the pad's frames: the display digits, the icons and fx.h, checked
 * on the vertices each call writes (the frame's vertex area is contiguous:
 * a mark before and after a call holds them), and the RSX memory that
 * digits_init and icons_init take. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <net/net.h>
#include <sysutil/sysutil.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/gfx.h>
#include <ps3gfx/fs.h>
#include <ps3gfx/digits.h>
#include <ps3gfx/icons.h>
#include <ps3gfx/fx.h>

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

/* gfx_copy_to_main between frames on the preview: a texture's bytes come
 * back whole; a src outside RSX memory and a src off a 128-byte line are
 * refused */
static void copies(void)
{
    static u8 host[128] __attribute__((aligned(128)));       /* main memory */
    gfx_tex t;
    u8 *p = gfx_tex_new(&t, 64, 8, GFX_TEX_A8R8G8B8, 0), *dst = malloc(2048 + 1);
    CHECK(p && dst);
    if (!p || !dst) { free(dst); return; }
    for (int i = 0; i < 2048; i++) p[i] = (u8)(i * 13 + 5);
    memset(dst, 0, 2049);
    CHECK(gfx_copy_to_main(p, dst + 1, 2048) == 0 && !memcmp(dst + 1, p, 2048) && dst[0] == 0);
    CHECK(gfx_copy_to_main(host, dst, 128) == GFX_COPY_ERANGE);
    CHECK(gfx_copy_to_main(p + 4, dst, 128) == GFX_COPY_EARG);
    free(dst);
}

/* the front buffer is RSX memory on a 128-byte line, as on the console: its
 * rows come by gfx_copy_to_main (ui_shot's path), the same bytes */
static void front_rows(void)
{
    u32 pitch = 0;
    const volatile u32 *px = gfx_front(&pitch);
    u32 *rows = malloc((size_t)pitch * 2);
    CHECK(px && rows && ((uintptr_t)px & 127) == 0);
    if (px && rows) CHECK(gfx_copy_to_main((const void *)px, rows, pitch * 2) == 0 && !memcmp(rows, (const void *)px, pitch * 2));
    free(rows);
}

/* UTF-8 after text_utf8: ASCII measures and draws as before, the fallbacks
 * have the widths of what they draw, cuts and wraps keep whole code points */
static void utf8(void)
{
    static const char *const ascii[] = {"The counter runs while the console works.", "0123 456", "a  b", ""};
    int before[4][F_COUNT], lines[4];
    for (int i = 0; i < 4; i++) {
        for (int f = 0; f < F_COUNT; f++) before[i][f] = text_w(f | TNUM, ascii[i]);
        lines[i] = text_wrap_lines(F_BODY, 300, 6, ascii[i]);
    }
    begin_frame();
    footer("CROSS play  L1 previous");
    text(MG, TOP, F_BODY, WHITE, "%s", ascii[0]);
    ui_flip();
    u32 v0, d0, v1, d1;
    gfx_last_counts(&v0, &d0, NULL);
    CHECK(text_utf8() == 0 && text_utf8() == 0);
    begin_frame();
    footer("CROSS play  L1 previous");
    text(MG, TOP, F_BODY, WHITE, "%s", ascii[0]);
    ui_flip();
    gfx_last_counts(&v1, &d1, NULL);
    CHECK(v0 == v1 && d0 == d1);                              /* ASCII draws as in byte mode */
    for (int i = 0; i < 4; i++) {
        for (int f = 0; f < F_COUNT; f++) CHECK(text_w(f | TNUM, ascii[i]) == before[i][f]);
        CHECK(text_wrap_lines(F_BODY, 300, 6, ascii[i]) == lines[i]);
    }
    int sq = text_w(F_BODY, "\xe2\x96\xa1");                 /* the white square */
    CHECK(sq > 0 && text_w(F_BODY, "\xc3\xb3") > 0 && text_w(F_BODY, "\xc3\xb3") != text_w(F_BODY, "\xc3"));
    CHECK(text_w(F_BODY, "a\xcc\x81") == text_w(F_BODY, "a"));          /* a combining mark: no width */
    CHECK(text_w(F_BODY, "ab\xe2\x80\x8b" "cd") == text_w(F_BODY, "abcd"));   /* zero width */
    CHECK(text_w(F_BODY, "\xc5\x81\xc5\x99") == text_w(F_BODY, "Lr")); /* Latin Extended-A: the base letters */
    CHECK(text_w(F_BODY, "\xc5\x92") != text_w(F_BODY, "O"));           /* OE is in the atlas */
    CHECK(text_w(F_BODY, "\xff") == sq && text_w(F_BODY, "\xf0\x9f\x8e\xb5") == sq && text_w(F_BODY, "\xef\xbf\xbd") == sq);
    CHECK(text_w(F_BODY, "\xc0\x80") == 2 * sq && text_w(F_BODY, "\xed\xa0\x80") == sq + 2 * sq);  /* overlong, a surrogate */
    CHECK(text_w(F_BODY, "ab\xc3") == text_w(F_BODY, "ab") && text_w(F_BODY, "ab\xe2\x82") == text_w(F_BODY, "ab"));   /* cut at the end */
    char big[330];
    memset(big, 'a', 318);
    memcpy(big + 318, "\xc3\xb3", 3);                        /* 320 bytes: the format buffer keeps 318 a and half the o */
    CHECK(text(0, 0, F_BODY, WHITE, "%s", big) == text(0, 0, F_BODY, WHITE, "%.318s", big));
    const char *oo = "\xc3\xb3\xc3\xb3\xc3\xb3\xc3\xb3\xc3\xb3\xc3\xb3\xc3\xb3\xc3\xb3";
    int w1 = text_w(F_BODY, "\xc3\xb3"), ell = text_w(F_BODY, "...");
    for (int m = 0; m <= 8 * w1 + ell; m += 3) {               /* text_fit cuts between code points */
        float x = text_fit(0, 0, F_BODY, WHITE, (float)m, "%s", oo);
        CHECK((int)x == 8 * w1 || ((int)x - ell) % w1 == 0);
    }
    CHECK(text_wrap_lines(F_LARGE, 5, 4, "\xc5\x92\xc5\x92\xc5\x92") == 3);   /* too wide: one whole code point a line */
    CHECK(text_wrap_lines(F_BODY, 2000, 4, "\xc3\xb3 \xc3\xb3\n\xc3\xb1") == 2);
}

/* the preview's offline network has PSL1GHT's socket calls and constants */
static void net(void)
{
    char b[4];
    struct pollfd p = {0, POLLIN, 0};
    s32 e = 0;
    socklen_t l = sizeof e;
    CHECK(netSocket(AF_INET, SOCK_STREAM, 0) == -1 && netRecv(0, b, 1, MSG_DONTWAIT) == -1);
    CHECK(netPoll(&p, 1, 0) == -1 && netGetSockOpt(0, SOL_SOCKET, SO_ERROR, &e, &l) == -1 && netShutdown(0, SHUT_RDWR) == -1);
    CHECK(net_errno == NET_ENETDOWN && NET_EWOULDBLOCK == 35 && NET_EINPROGRESS == 36);
    s32 tz = 0, fmt = 0;                                  /* the system settings: UTC-3, 24 hours */
    CHECK(sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_TIMEZONE, &tz) == 0 && tz == -180);
    CHECK(sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_TIME_FORMAT, &fmt) == 0 && fmt == 1);
}

/* ---- digits, icons and fx.h: the vertices of each call -------------------- */

/* A mark in the frame's vertex area: a triangle of alpha 0 (the alpha test
 * drops it). A call's vertices lie between the end of one mark and the next. */
static gfx_vtx *mark(void)
{
    gfx_texture(NULL);
    gfx_vtx *q = gfx_prim(GFX_TRIANGLES, 3);
    for (int i = 0; i < 3; i++) gfx_put(q + i, 0, 0, 0, 0, 0);
    return q;
}

static gfx_vtx cap[1024];
static int ncap;
static gfx_vtx *cap_from;

static void cap_start(void) { cap_from = mark() + 3; }

static int cap_end(void)
{
    gfx_vtx *e = mark();
    ncap = (int)(e - cap_from);
    if (ncap > 1024) ncap = 1024;
    if (ncap > 0) memcpy(cap, cap_from, ncap * sizeof *cap);
    return ncap;
}

static int same(const gfx_vtx *a, const gfx_vtx *b) { return !memcmp(a, b, sizeof *a); }
static int near(float a, float b, float e) { return fabsf(a - b) <= e; }

/* The RSX memory a call takes: the offsets of two small blocks around it. */
static u32 vram_at(void)
{
    u32 off = 0;
    gfx_vram(128, 128, &off);
    return off;
}

/* ---- the text calls' returns, the bytes past ASCII, the "%s" path -------- */

static gfx_vtx cap2[1024];

/* text and text_c return the x after the text, text_r the x where it starts;
 * before text_utf8 a byte past ASCII is '?', with TNUM too; a plain "%s"
 * writes the vertices of vsnprintf's copy, also past the 319-byte cut and
 * for NULL */
static void text_checks(void)
{
    char big[400];
    const char *volatile nul = NULL;
    memset(big, 'w', sizeof big - 1);
    big[sizeof big - 1] = 0;
    begin_frame();
    for (int f = 0; f < F_COUNT; f++) {
        int w = text_w(f, "abc");
        CHECK(text(500, 20, f, WHITE, "abc") == 500 + w);
        CHECK(text_r(500, 20, f, WHITE, "abc") == 500 - w);
        CHECK(text_c(500, 20, f, WHITE, "abc") == floorf(500 - w / 2 + 0.5f) + w);
        CHECK(text_w(f, "\x80\x89\xff") == 3 * text_w(f, "?"));
        CHECK(text_w(f | TNUM, "\x80\x85") == 2 * text_w(f | TNUM, "?") && text_w(f | TNUM, "0") != 0);
        const char *s[] = {"Couch 0.3.0", big, "", nul};
        for (int k = 0; k < 4; k++) {
            cap_start();
            float a = text_fit(10, 40, f, WHITE, 600, "%s", s[k]);
            int n = cap_end();
            memcpy(cap2, cap, n * sizeof *cap);
            cap_start();
            float b = text_fit(10, 40, f, WHITE, 600, "%s%s", s[k], "");
            CHECK(cap_end() == n && a == b && !memcmp(cap, cap2, n * sizeof *cap));
        }
    }
    ui_flip();
}

/* ---- run_job: a job's copy, the bar, the presses after it ----------------- */

static u8 *job_src, job_dst[256];
static int job_rc = 1;

static void job(void)
{
    job_set_percent(150);                                   /* drawn as 100 % */
    progress("kitcheck job");
    job_rc = gfx_copy_to_main_job(job_src, job_dst, sizeof job_dst);
}

/* the job's copy runs in this thread's next gfx_wait; after the job no press
 * and no armed gesture are left */
static void jobs(void)
{
    gfx_tex t;
    job_src = gfx_tex_new(&t, 64, 1, GFX_TEX_A8R8G8B8, 0);
    CHECK(job_src != NULL);
    if (!job_src) return;
    for (unsigned i = 0; i < sizeof job_dst; i++) job_src[i] = (u8)(i * 5 + 3);
    pressed = BTN_CROSS;
    armed = BTN_SQUARE;
    run_job("kitcheck job", job);
    CHECK(job_rc == 0 && !memcmp(job_dst, job_src, sizeof job_dst));
    CHECK(pressed == 0 && armed == 0);
}

/* A failed digits_init or icons_init stays failed. In a child process (its
 * own copy of the RSX memory), with room for two of the three atlases but
 * not the third, the first call fails partway; a second returns -1 at once
 * and takes nothing (a retry would take the first two again). */
#define PV_LOCAL 0x0F900000u                 /* the preview's RSX memory (gfx_soft.c), 249 MB */

static int init_fails(int (*init)(void), u32 left)
{
    u32 off = vram_at();
    gfx_vram(PV_LOCAL - (off + 128) - left, 128, NULL);         /* left bytes stay free */
    u32 a = vram_at();
    int r1 = init();
    u32 b = vram_at();
    int r2 = init();
    u32 c = vram_at();
    if (r1 != -1 || b - a <= 128 || r2 != -1 || c - b != 128) printf("FAIL init after a failure: %d, %u bytes, %d, %u bytes\n", r1, b - a - 128, r2, c - b - 128);
    return r1 == -1 && b - a > 128 && r2 == -1 && c - b == 128;
}

/* A failed text_utf8 stays failed too, in a child before utf8() loads it
 * (room for some of the five Latin atlases, not all, and for the first
 * again: a retry would take it). */
static void utf8_failure(void)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        int good = init_fails(text_utf8, 1200000) && text_w(F_BODY, "\xc3\xb3") == 2 * text_w(F_BODY, "?");   /* bytes: '?' twice */
        fflush(stdout);
        _exit(good ? 0 : 1);
    }
    int st = 0;
    CHECK(pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

/* safe_set writes the area and safe_load reads it back; a file that is not
 * a valid area reads as none (the default stays) */
static void safe_area(void)
{
    CHECK(safe_valid(100, 60, 1820, 1020) && !safe_valid(100, 60, 1500, 1020) && !safe_valid(-1, 0, 1920, 1080));
    safe_set(100, 60, 1820, 1020);
    safe_l = safe_t = safe_r = safe_b = 0;
    CHECK(safe_load() == 1 && safe_l == 100 && safe_t == 60 && safe_r == 1820 && safe_b == 1020 && SW == 1720 && SH == 960);
    CHECK(fs_write_file(DIR "/safe_area.txt", "10 10 20 20\n", 12, 0) == 0 && safe_load() == 0 && safe_l == 100);
    CHECK(fs_write_file(DIR "/safe_area.txt", "x", 1, 0) == 0 && safe_load() == 0);
    safe_set(SAFE_DEF_L, SAFE_DEF_T, SAFE_DEF_R, SAFE_DEF_B);
    CHECK(SW == SAFE_DEF_R - SAFE_DEF_L && SH == SAFE_DEF_B - SAFE_DEF_T);
}

static void init_failures(void)
{
    for (int k = 0; k < 2; k++) {
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {                      /* digits: 295,680 and 515,968 bytes fit, 1,133,824 do not; icons: 39,168 and 154,752, not 559,872 */
            int good = k == 0 ? init_fails(digits_init, 1500000) && digits_h(D_M) == 0 && digits_w(D_M, "1") == 0
                              : init_fails(icons_init, 600000) && icon_size(IC_S_CARET_UP) == 0;
            fflush(stdout);
            _exit(good ? 0 : 1);
        }
        int st = 0;
        CHECK(pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }
}

static void digit_checks(void)
{
    static const char *const sz[D_COUNT] = {"D_M", "D_L", "D_XL"};
    const char *all = "0123456789:,.-+%\xc2\xb0";
    CHECK(digits_w(D_L, "12") == 0 && digits_h(D_L) == 0);           /* before digits_init: nothing */
    u32 a = vram_at();
    CHECK(digits_init() == 0);
    u32 b = vram_at();
    CHECK(digits_init() == 0 && vram_at() - b == 128);              /* again: nothing, no memory (the mark's 128 bytes) */
    printf("digits_init: %u bytes of RSX memory\n", b - a - 128);
    CHECK(digits_h(D_M) > 0 && digits_h(D_L) > digits_h(D_M) && digits_h(D_XL) > digits_h(D_L));
    CHECK(digits_w(-1, "1") == 0 && digits_w(D_COUNT, "1") == 0 && digits_h(D_COUNT) == 0);
    begin_frame();
    for (int k = 0; k < D_COUNT; k++) {
        int w0 = digits_w(k, "0"), sum = 0;
        for (char c = '0'; c <= '9'; c++) {                       /* tabular: one advance */
            char d[2] = {c, 0};
            CHECK(digits_w(k, d) == w0);
        }
        CHECK(w0 > 0);
        for (const char *p = "0123456789:,.-+%\xb0"; *p; p++) {    /* each glyph's width, and the sum */
            char d[2] = {*p, 0};
            CHECK(digits_w(k, d) > 0);
            sum += digits_w(k, d);
        }
        CHECK(digits_w(k, all) == sum && digits_w(k, "0123456789:,.-+%\xb0") == sum);
        CHECK(digits_w(k, "\xc2\xb0") == digits_w(k, "\xb0"));  /* the degree sign in UTF-8 and Latin-1 */
        CHECK(digits_w(k, "1a 2") == digits_w(k, "12") && digits_w(k, "") == 0);   /* other bytes: no width */
        CHECK(digits_w(k, "-") == digits_w(k, "+"));             /* the minus sign: the width of the plus */
        cap_start();
        float x = digits(100.4f, 200, k, WHITE, all);
        CHECK(cap_end() == 17 * 4 && x == 100 + sum);             /* every glyph draws its cell, from a whole pixel */
        float cw = cap[1].x - cap[0].x, ch = cap[2].y - cap[1].y;
        for (int i = 0; i < 17; i++) {
            const gfx_vtx *q = cap + 4 * i;
            CHECK(q[1].x - q[0].x == cw && q[2].y - q[1].y == ch && q[0].y == cap[0].y && q[0].u < q[1].u && q[1].v < q[2].v);
            CHECK(q[0].u >= 0 && q[1].u <= 1 && q[0].v >= 0 && q[2].v <= 1);
        }
        CHECK(digits_r(500, 200, k, WHITE, "12:34") == 500 - digits_w(k, "12:34"));
        for (const char *p = "0123456789:,.-+%\xb0"; *p; p++) {    /* the two halves at scale 1: the whole glyph */
            char d[2] = {*p, 0};
            gfx_vtx g[4], t[4], u[4];
            cap_start();
            float xa = digits(300, 400.3f, k, WHITE, d);
            CHECK(cap_end() == 4);
            memcpy(g, cap, sizeof g);
            cap_start();
            float xb = digits_half(300, 400.3f, k, *p, 0, 1, WHITE);
            CHECK(cap_end() == 4);
            memcpy(t, cap, sizeof t);
            cap_start();
            float xc = digits_half(300, 400.3f, k, *p, 1, 1, WHITE);
            CHECK(cap_end() == 4);
            memcpy(u, cap, sizeof u);
            CHECK(xa == xb && xb == xc);
            CHECK(same(&t[0], &g[0]) && same(&t[1], &g[1]) && same(&u[2], &g[2]) && same(&u[3], &g[3]));
            CHECK(t[2].y == u[1].y && t[3].y == u[0].y && t[2].v == u[1].v && t[3].v == u[0].v);
            CHECK(t[2].x == g[2].x && u[0].x == g[3].x && t[2].u == g[2].u && u[0].u == g[3].u);
            CHECK(t[2].y == 400 + digits_mid(k) && t[2].y > g[0].y && t[2].y < g[2].y);
            CHECK(near(t[2].v, g[0].v + (g[2].v - g[0].v) * (t[2].y - g[0].y) / (g[2].y - g[0].y), 1e-6f));   /* the line's texel row */
            cap_start();
            digits_half(300, 400, k, *p, 0, 0.5f, WHITE);           /* half height, from the middle line */
            CHECK(cap_end() == 4 && cap[2].y == 400 + digits_mid(k) && near(cap[2].y - cap[0].y, (t[2].y - t[0].y) / 2, 0.001f));
            cap_start();
            digits_half(300, 400, k, *p, 1, 0, WHITE);
            CHECK(cap_end() == 0);                                /* scale 0: nothing */
        }
        printf("digits %s: cell %.0f x %.0f, digit advance %d, line %d, middle line %d\n", sz[k], cw, ch, w0, digits_h(k), digits_mid(k));
    }
    cap_start();
    CHECK(digits(10, 10, D_COUNT, WHITE, "1") == 10 && digits_half(10, 10, -1, '1', 0, 1, WHITE) == 10);
    CHECK(cap_end() == 0);
    ui_flip();
}

static void icon_checks(void)
{
    CHECK(icon_size(IC_S_CARET_UP) == 0);                       /* before icons_init: nothing */
    u32 a = vram_at();
    CHECK(icons_init() == 0);
    u32 b = vram_at();
    CHECK(icons_init() == 0 && vram_at() - b == 128);               /* again: nothing, no memory */
    printf("icons_init: %u bytes of RSX memory, %d icons\n", b - a - 128, IC_COUNT);
    CHECK(icon_size(IC_S_CARET_UP) == 32 && icon_size(IC_S_UMBRELLA) == 32 && icon_size(IC_M_SUN_FILL) == 64);
    CHECK(icon_size(IC_M_CLOUD_LIGHTNING_LINE) == 64 && icon_size(IC_L_SUN_FILL) == 168 && icon_size(IC_L_SNOWFLAKE_LINE) == 168);
    CHECK(icon_size(-1) == 0 && icon_size(IC_COUNT) == 0);
    static float uv[IC_COUNT][4];
    begin_frame();
    for (int id = 0; id < IC_COUNT; id++) {
        gfx_vtx g[4];
        cap_start();
        icon(id, 400, 300, WHITE);
        CHECK(cap_end() == 4);                                    /* every id draws its cell */
        memcpy(g, cap, sizeof g);
        CHECK(g[1].x > g[0].x && g[2].y > g[1].y && g[0].u < g[1].u && g[1].v < g[2].v && g[0].u >= 0 && g[1].u <= 1);
        CHECK((g[0].x + g[1].x) / 2 == 400 && (g[1].y + g[2].y) / 2 == 300);   /* centred */
        CHECK(g[1].x - g[0].x >= icon_size(id) / 2 && g[1].x - g[0].x <= 2 * icon_size(id));
        uv[id][0] = g[0].u, uv[id][1] = g[0].v, uv[id][2] = g[2].u, uv[id][3] = g[2].v;
        cap_start();
        icon(id, 400.4f, 299.6f, WHITE);                          /* the centre snaps to a whole pixel */
        CHECK(cap_end() == 4 && same(&cap[0], &g[0]) && same(&cap[2], &g[2]));
        cap_start();
        icon_rot(id, 400, 300, 0, 1, WHITE);                      /* no turn: the vertices of icon */
        CHECK(cap_end() == 4);
        for (int k = 0; k < 4; k++) CHECK(same(&cap[k], &g[k]));
        cap_start();
        icon_rot(id, 400.25f, 299.5f, 0, 1, WHITE);               /* no snap (bilinear for every set): a fraction moves it by that */
        CHECK(cap_end() == 4);
        for (int k = 0; k < 4; k++) CHECK(cap[k].x == g[k].x + 0.25f && cap[k].y == g[k].y - 0.5f && cap[k].u == g[k].u && cap[k].v == g[k].v);
        cap_start();
        icon_rot(id, 400, 300, 3.14159265f, 2, WHITE);            /* a half turn at scale 2: about the centre */
        CHECK(cap_end() == 4 && near(cap[0].x, 400 + 2 * (400 - g[0].x), 0.01f) && near(cap[0].y, 300 + 2 * (300 - g[0].y), 0.01f));
        CHECK(cap[0].u == g[0].u && cap[2].v == g[2].v);
    }
    for (int i = 0; i < IC_COUNT; i++)                          /* no two ids share a cell */
        for (int j = 0; j < i; j++)
            CHECK(memcmp(uv[i], uv[j], sizeof uv[i]) || icon_size(i) != icon_size(j));
    cap_start();
    icon(-1, 10, 10, WHITE);
    icon(IC_COUNT, 10, 10, WHITE);
    icon_rot(IC_COUNT, 10, 10, 0, 1, WHITE);
    CHECK(cap_end() == 0);
    ui_flip();
}

/* The anti-aliased helpers (fx.h): the quads' count, the ink's extent, the
 * feather's alpha, nothing for no width or no points. */
static int extent(float *x0, float *y0, float *x1, float *y1)
{
    *x0 = *y0 = 1e9f;
    *x1 = *y1 = -1e9f;
    for (int i = 0; i < ncap; i++) {
        if (cap[i].x < *x0) *x0 = cap[i].x;
        if (cap[i].x > *x1) *x1 = cap[i].x;
        if (cap[i].y < *y0) *y0 = cap[i].y;
        if (cap[i].y > *y1) *y1 = cap[i].y;
    }
    return ncap;
}

static int count_rgba(u32 c)
{
    int n = 0;
    for (int i = 0; i < ncap; i++) n += cap[i].rgba == c;
    return n;
}

static void aa_checks(void)
{
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    cap_start();                                                  /* a 3 px line: a 3 x 3 grid of quads, the core 2 px, 1 px of feather */
    aa_line(100, 100, 200, 100, 3, 0x6cb8ffc0);
    CHECK(cap_end() == 36 && extent(&x0, &y0, &x1, &y1));
    CHECK(x0 == 99.5f && x1 == 200.5f && y0 == 98 && y1 == 102);
    CHECK(count_rgba(0x6cb8ffc0) == 16 && count_rgba(0x6cb8ff00) == 20);   /* the core 4, each feather side 2, each corner 1 */
    cap_start();                                                  /* under 1 px: no core, the alpha times w */
    aa_line(100, 100, 200, 100, 0.5f, 0x6cb8ffc0);
    CHECK(cap_end() == 36 && count_rgba(0x6cb8ff60) == 16 && extent(&x0, &y0, &x1, &y1) && y0 == 99 && y1 == 101);
    float seg[8] = {10, 10, 10, 40, 50, 10, 80, 40};
    cap_start();
    aa_lines(seg, 2, 2, WHITE);
    CHECK(cap_end() == 72);
    float st[6] = {100, 100, 200, 100, 300, 100};
    cap_start();                                                  /* a strip: a cap, two segments, a cap; the ink to the end points */
    aa_strip(st, 3, 3, 0xffd36bff);
    CHECK(cap_end() == 48 && extent(&x0, &y0, &x1, &y1) && x0 == 99.5f && x1 == 300.5f && y0 == 98 && y1 == 102);
    float rt[6] = {100, 100, 200, 100, 200, 200};
    cap_start();                                                  /* a right angle: the miter of strip at each offset */
    aa_strip(rt, 3, 4, WHITE);
    CHECK(cap_end() == 48);
    int corner = 0;
    for (int i = 0; i < ncap; i++) corner += near(cap[i].x, 202.5f, 0.001f) && near(cap[i].y, 97.5f, 0.001f);
    CHECK(corner > 0);
    static float lng[2 * 120];
    for (int i = 0; i < 120; i++) lng[2 * i] = 10 + i * 6, lng[2 * i + 1] = 500 + (i % 7) * 3;
    cap_start();
    aa_strip(lng, 80, 2, WHITE);                                  /* 79 segments and two caps, 243 quads: four primitives */
    CHECK(cap_end() == 243 * 4);
    cap_start();                                                  /* a ring: every vertex at one of its four radii */
    aa_ring(500, 500, 30, 3, 0xffffff40);
    CHECK(cap_end() == 48 * 12);
    int off = 0;
    for (int i = 0; i < ncap; i++) {
        float d = sqrtf((cap[i].x - 500) * (cap[i].x - 500) + (cap[i].y - 500) * (cap[i].y - 500));
        off += !(near(d, 32, 0.01f) || near(d, 31, 0.01f) || near(d, 29, 0.01f) || near(d, 28, 0.01f));
    }
    CHECK(off == 0 && count_rgba(0xffffff40) == 48 * 8);
    cap_start();                                                  /* a disc: the fan to r - 0.5, the feather to r + 0.5 */
    aa_disc(300, 300, 10, WHITE);
    CHECK(cap_end() == 24 * 8 && extent(&x0, &y0, &x1, &y1) && near(x0, 289.5f, 0.001f) && near(y1, 310.5f, 0.001f));
    float tr[6] = {400, 400, 440, 400, 420, 380};
    cap_start();                                                  /* a triangle: the core inside, the feather outside */
    aa_poly(tr, 3, WHITE);
    CHECK(cap_end() == 24 && extent(&x0, &y0, &x1, &y1) && x0 < 400 && x1 > 440 && y0 < 380 && y1 > 400);
    float rev[6] = {420, 380, 440, 400, 400, 400};                /* the other winding: the same extent */
    float e0 = 0, e1 = 0, e2 = 0, e3 = 0;
    cap_start();
    aa_poly(rev, 3, WHITE);
    CHECK(cap_end() == 24 && extent(&e0, &e1, &e2, &e3) && near(e0, x0, 0.001f) && near(e2, x1, 0.001f) && near(e1, y0, 0.001f));
    cap_start();                                                  /* the corners: 8 quads each, half the alpha at the arc */
    aa_corners(100, 100, 200, 100, 20, 0x0b142680);
    CHECK(cap_end() == 128 && count_rgba(0x0b142640) == 64 && extent(&x0, &y0, &x1, &y1) && near(x0, 99, 0.001f) && near(y1, 201, 0.001f));
    cap_start();
    aa_corners(100, 100, 200, 100, 0.5f, WHITE);
    aa_line(10, 10, 20, 20, 0, WHITE);
    aa_strip(st, 1, 3, WHITE);
    aa_ring(10, 10, 5, 0, WHITE);
    aa_disc(10, 10, 0, WHITE);
    aa_poly(st, 2, WHITE);
    CHECK(cap_end() == 0);
}

/* The ink of the captured quads: each triangle's area times its mean alpha. */
static float ink(void)
{
    double s = 0;
    for (int i = 0; i + 3 < ncap; i += 4)
        for (int t = 1; t <= 2; t++) {
            const gfx_vtx *a = &cap[i], *b = &cap[i + t], *c = &cap[i + t + 1];
            double ar = fabs((double)(b->x - a->x) * (c->y - a->y) - (double)(b->y - a->y) * (c->x - a->x)) / 2;
            s += ar * ((a->rgba & 255) + (b->rgba & 255) + (c->rgba & 255)) / 3 / 255;
        }
    return (float)s;
}

/* The anti-aliased helpers at their edges: a repeated point (aa_strip,
 * aa_poly) and a closed outline draw as the plain ones, aa_poly past 64
 * points, and a segment's ink in proportion to its length, also under 1 px. */
static void aa_edges(void)
{
    static gfx_vtx ref[256];
    int nref;
    float tr[6] = {400, 400, 440, 400, 420, 380};
    float trs[3][8] = {{400, 400, 440, 400, 420, 380, 400, 400}, {400, 400, 440, 400, 440, 400, 420, 380}, {400, 400, 400, 400, 440, 400, 420, 380}};
    cap_start();
    aa_poly(tr, 3, WHITE);
    nref = cap_end();
    memcpy(ref, cap, nref * sizeof *cap);
    for (int k = 0; k < 3; k++) {                                 /* closed, a repeat inside, a repeat first: the triangle's vertices */
        cap_start();
        aa_poly(trs[k], 4, WHITE);
        CHECK(cap_end() == nref && !memcmp(cap, ref, nref * sizeof *cap));
    }
    float two[8] = {400, 400, 440, 400, 440, 400, 400, 400};      /* two points after the repeats: nothing */
    cap_start();
    aa_poly(two, 4, WHITE);
    CHECK(cap_end() == 0);
    static float ring100[200];                                    /* 100 points: 2 quads each, the feather 0.5 px past the points */
    for (int i = 0; i < 100; i++) ring100[2 * i] = 600 + 50 * cosf(i * 6.2831853f / 100), ring100[2 * i + 1] = 300 + 50 * sinf(i * 6.2831853f / 100);
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    cap_start();
    aa_poly(ring100, 100, 0x6cb8ffc0);
    CHECK(cap_end() == 800 && count_rgba(0x6cb8ff00) == 200 && extent(&x0, &y0, &x1, &y1) && near(x1 - x0, 101, 0.01f));
    float st[6] = {100, 100, 200, 100, 200, 200}, sts[14] = {100, 100, 100, 100, 200, 100, 200, 100, 200, 100, 200, 200, 200, 200};
    cap_start();
    aa_strip(st, 3, 3, WHITE);
    nref = cap_end();
    memcpy(ref, cap, nref * sizeof *cap);
    cap_start();                                                  /* repeats first, inside and last: the plain strip's vertices */
    aa_strip(sts, 7, 3, WHITE);
    CHECK(cap_end() == nref && !memcmp(cap, ref, nref * sizeof *cap));
    cap_start();
    aa_strip(sts, 2, 3, WHITE);                                   /* no length at all: nothing */
    CHECK(cap_end() == 0);
    cap_start();                                                  /* 1 px: the reference ink */
    aa_line(100, 100, 101, 100, 3, 0x6cb8ffc0);
    CHECK(cap_end() == 36);
    float i1 = ink();
    static const float lens[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.5f, 2, 10};
    for (int k = 0; k < 7; k++) {
        cap_start();
        aa_line(100, 100, 100 + lens[k] * 0.6f, 100 + lens[k] * 0.8f, 3, 0x6cb8ffc0);
        CHECK(cap_end() == 36 && near(ink(), lens[k] * i1, 0.006f * i1));
    }
    cap_start();                                                  /* half a pixel: the alpha times 2 len / (1 + len), 2 / 3 */
    aa_line(100, 100, 100.5f, 100, 3, 0x6cb8ffc0);
    CHECK(cap_end() == 36 && count_rgba(0x6cb8ff80) == 16);
}

static void fx_checks(void)
{
    static const int areas[3][4] = {{SAFE_DEF_L, SAFE_DEF_T, SAFE_DEF_R, SAFE_DEF_B}, {160, 90, 1760, 990}, {0, 2, 1920, 1078}};
    gfx_tex t8, t4;
    u32 *p8 = gfx_tex_new(&t8, 64, 36, GFX_TEX_A8R8G8B8, 1);
    u16 *p4 = gfx_tex_new(&t4, 5, 3, GFX_TEX_A4R4G4B4, 0);
    CHECK(p8 && p4);
    begin_frame();
    cap_start();                                                  /* tex_sub: the rect and the UVs, in rect's order */
    tex_sub(&t8, 10, 20, 300, 100, 0.25f, 0.5f, 0.75f, 1, 0x11223344);
    CHECK(cap_end() == 4 && cap[0].x == 10 && cap[0].y == 20 && cap[1].x == 310 && cap[2].y == 120 && cap[3].x == 10);
    CHECK(cap[0].u == 0.25f && cap[0].v == 0.5f && cap[2].u == 0.75f && cap[2].v == 1 && cap[1].rgba == 0x11223344);
    for (int k = 0; k < 3; k++) {                                 /* the glass at the three areas: the UVs are the screen's */
        float x = 40, y = 152, w = 1076, h = 440, u0, v0, u1, v1, ox, oy;
        safe_l = areas[k][0], safe_t = areas[k][1], safe_r = areas[k][2], safe_b = areas[k][3];
        safe_apply();
        screen_origin(&ox, &oy);
        glass_uv(x, y, w, h, &u0, &v0, &u1, &v1);
        CHECK(near(u0, (x + areas[k][0]) / 1920, 1e-6f) && near(v1, (y + h + areas[k][1]) / 1080, 1e-6f));
        cap_start();
        tex_sub(&t8, x, y, w, h, u0, v0, u1, v1, WHITE);
        CHECK(cap_end() == 4);
        for (int i = 0; i < 4; i++) CHECK(near(cap[i].u * 1920, cap[i].x - ox, 0.01f) && near(cap[i].v * 1080, cap[i].y - oy, 0.01f));
        cap_start();
        tex_round(&t8, x, y, w, h, 20, u0, v0, u1, v1, WHITE);
        CHECK(cap_end() == 38 && same(&cap[1], &cap[37]));       /* a closed fan: the centre and 36 points */
        for (int i = 0; i < 38; i++) {
            CHECK(near(cap[i].u * 1920, cap[i].x - ox, 0.01f) && near(cap[i].v * 1080, cap[i].y - oy, 0.01f));
            CHECK(cap[i].x >= x && cap[i].x <= x + w && cap[i].y >= y && cap[i].y <= y + h);
        }
        float d = 300, su0, sv0, su1, sv1;                        /* a slide: the canvas d px right */
        safe_l += (int)d, safe_r += (int)d;
        safe_apply();
        glass_uv(x, y, w, h, &su0, &sv0, &su1, &sv1);
        CHECK(near(su0 - u0, d / 1920, 1e-6f) && near(su1 - u1, d / 1920, 1e-6f) && sv0 == v0 && sv1 == v1);
    }
    safe_l = SAFE_DEF_L, safe_t = SAFE_DEF_T, safe_r = SAFE_DEF_R, safe_b = SAFE_DEF_B;
    safe_apply();
    cap_start();                                                  /* a small radius: the 4-segment corners; none: tex_sub */
    tex_round(&t8, 100, 100, 50, 30, 5, 0, 0, 1, 1, WHITE);
    CHECK(cap_end() == 22);
    cap_start();
    tex_round(&t8, 100, 100, 50, 30, 0.5f, 0, 0, 1, 1, WHITE);
    CHECK(cap_end() == 4 && cap[2].x == 150 && cap[2].y == 130);
    /* glow9: 8 slices, each grid cell of the ring covered once, the rect and the outside never */
    int gx = 200, gy = 100, gw = 120, gh = 80, pad = 24;
    cap_start();
    glow9(&t8, gx, gy, gw, gh, pad, WHITE);
    CHECK(cap_end() == 32);
    int bad = 0;
    for (int py = gy - pad - 2; py < gy + gh + pad + 2; py++)
        for (int px = gx - pad - 2; px < gx + gw + pad + 2; px++) {
            float cx = px + 0.5f, cy = py + 0.5f;
            int n = 0;
            for (int q = 0; q < 8; q++)
                n += cx > cap[4 * q].x && cx < cap[4 * q + 1].x && cy > cap[4 * q].y && cy < cap[4 * q + 2].y;
            int outer = px >= gx - pad && px < gx + gw + pad && py >= gy - pad && py < gy + gh + pad;
            int inner = px >= gx + pad && px < gx + gw - pad && py >= gy + pad && py < gy + gh - pad;
            bad += n != (outer && !inner);
        }
    CHECK(bad == 0);
    CHECK(cap[0].u == 0 && cap[1].u == 0.5f && cap[4].u == 0.5f && cap[5].u == 0.5f && cap[9].u == 1 && cap[22].v == 1);
    cap_start();
    glow9(&t8, 0, 0, 40, 30, 100, WHITE);                         /* pad over half the smaller side: 15 */
    CHECK(cap_end() == 32 && cap[0].x == -15 && cap[0].y == -15);
    cap_start();                                                  /* quad_rot at 0: tex_sub of the rect */
    quad_rot(&t8, 500, 300, 64, 40, 0, 0.1f, 0.2f, 0.9f, 0.8f, 0xff8800ff);
    CHECK(cap_end() == 4);
    gfx_vtx r[4];
    memcpy(r, cap, sizeof r);
    cap_start();
    tex_sub(&t8, 500 - 32, 300 - 20, 64, 40, 0.1f, 0.2f, 0.9f, 0.8f, 0xff8800ff);
    CHECK(cap_end() == 4);
    for (int k = 0; k < 4; k++) CHECK(same(&cap[k], &r[k]));
    cap_start();                                                  /* a quarter turn clockwise: the top left corner goes right */
    quad_rot(&t8, 500, 300, 64, 40, 1.5707963f, 0, 0, 1, 1, WHITE);
    CHECK(cap_end() == 4 && near(cap[0].x, 520, 0.001f) && near(cap[0].y, 268, 0.001f) && near(cap[2].x, 480, 0.001f));
    /* strip: a straight line, a right angle with its miter, the pieces of a long line */
    float st[6] = {100, 100, 200, 100, 300, 100};
    cap_start();
    strip(st, 3, 4, WHITE);
    CHECK(cap_end() == 6 && cap[0].x == 100 && cap[0].y == 98 && cap[1].y == 102 && cap[2].x == 200 && cap[2].y == 98 && cap[5].y == 102);
    float rt[6] = {100, 100, 200, 100, 200, 200};
    cap_start();
    strip(rt, 3, 4, WHITE);
    CHECK(cap_end() == 6 && near(cap[2].x, 202, 0.001f) && near(cap[2].y, 98, 0.001f) && near(cap[3].x, 198, 0.001f) && near(cap[3].y, 102, 0.001f));
    CHECK(near(cap[4].x, 202, 0.001f) && near(cap[5].x, 198, 0.001f) && cap[4].y == 200);
    float back[6] = {100, 100, 200, 100, 100, 100};                /* a turn back: no spike */
    cap_start();
    strip(back, 3, 4, WHITE);
    CHECK(cap_end() == 6 && near(cap[2].x, 200, 2.001f) && fabsf(cap[2].y - 100) <= 4.001f);
    gfx_vtx sr[6];                                                /* a repeated point: its rows are the point's, the miter stays */
    cap_start();
    strip(rt, 3, 4, WHITE);
    CHECK(cap_end() == 6);
    memcpy(sr, cap, sizeof sr);
    float rp[10] = {100, 100, 100, 100, 200, 100, 200, 100, 200, 200};
    cap_start();
    strip(rp, 5, 4, WHITE);
    CHECK(cap_end() == 10);
    for (int k = 0; k < 2; k++)
        CHECK(same(&cap[k], &sr[k]) && same(&cap[2 + k], &sr[k]) && same(&cap[4 + k], &sr[2 + k]) && same(&cap[6 + k], &sr[2 + k]) && same(&cap[8 + k], &sr[4 + k]));
    float one[6] = {100, 100, 100, 100, 100, 100};
    cap_start();
    strip(one, 3, 4, WHITE);                                      /* no length at all: nothing */
    CHECK(cap_end() == 0);
    static float lng[2 * 300];
    for (int i = 0; i < 300; i++) lng[2 * i] = 10 + i * 6, lng[2 * i + 1] = 500 + (i % 7) * 3;
    cap_start();
    strip(lng, 300, 3, WHITE);
    CHECK(cap_end() == 2 * (128 + 128 + 46));                    /* 300 points: pieces of 128, each from the last point before */
    CHECK(same(&cap[254], &cap[256]) && same(&cap[255], &cap[257]) && same(&cap[510], &cap[512]) && same(&cap[511], &cap[513]));
    cap_start();
    strip(lng, 1, 3, WHITE);
    strip(lng, 5, 0, WHITE);
    area(lng, 1, 600, WHITE, 0);
    CHECK(cap_end() == 0);
    cap_start();
    area(st, 3, 160, 0x6cb8ff40, 0x6cb8ff00);
    CHECK(cap_end() == 6 && cap[0].y == 100 && cap[1].y == 160 && cap[4].x == 300 && cap[0].rgba == 0x6cb8ff40 && cap[1].rgba == 0x6cb8ff00);
    /* area across its base: a pair of vertices at each crossing, the two triangles meet there */
    float cr[6] = {50, 100, 100, 200, 150, 100};
    cap_start();
    area(cr, 3, 150, 0x6cb8ff40, 0x6cb8ff00);
    CHECK(cap_end() == 10 && same(&cap[2], &cap[3]) && same(&cap[6], &cap[7]) && cap[2].x == 75 && cap[6].x == 125);
    CHECK(cap[2].y == 150 && cap[6].y == 150 && cap[2].rgba == 0x6cb8ff00 && cap[4].x == 100 && cap[4].y == 200 && cap[4].rgba == 0x6cb8ff40);
    CHECK(cap[0].x == 50 && cap[1].y == 150 && cap[8].x == 150 && cap[9].y == 150);
    float on[6] = {50, 150, 100, 100, 150, 150};                  /* points on the base: no crossing */
    cap_start();
    area(on, 3, 150, WHITE, 0);
    CHECK(cap_end() == 6);
    static float zz[2 * 200];                                     /* every segment crosses: pieces of 64 points, 254 vertices */
    for (int i = 0; i < 200; i++) zz[2 * i] = 10 + i * 6, zz[2 * i + 1] = i & 1 ? 200 : 100;
    cap_start();
    area(zz, 200, 150, WHITE, 0);
    CHECK(cap_end() == 3 * 254 + 2 + 10 * 4 && same(&cap[252], &cap[254]) && same(&cap[253], &cap[255]) && same(&cap[506], &cap[508]));
    aa_checks();
    aa_edges();
    ui_flip();
    /* tex_upload: whole words of each row, from a source pitch, nothing past them */
    static u32 src[7 * 36];
    for (int i = 0; i < 7 * 36; i++) src[i] = 0x01000000u * (u32)(i & 0xff) + (u32)i;
    for (int i = 0; i < t8.pitch / 4 * 36; i++) p8[i] = 0xdeadbeef;
    t8.w = 5;                                                     /* a 5-texel row of the same memory */
    tex_upload(&t8, p8, src, 7);
    int up = 1;
    for (int y = 0; y < 36; y++)
        for (int x = 0; x < 16; x++) up &= p8[y * t8.pitch / 4 + x] == (x < 5 ? src[y * 7 + x] : 0xdeadbeef);
    CHECK(up);
    for (int i = 0; i < t4.pitch / 2 * 3; i++) p4[i] = 0xbeef;
    tex_upload(&t4, p4, src, 7);                                  /* 5 texels of 16 bits: 3 words, 6 texels */
    CHECK(((u32 *)p4)[2] == src[2] && ((u32 *)p4)[t4.pitch / 4 + 2] == src[9] && p4[6] == 0xbeef && p4[t4.pitch / 2 + 6] == 0xbeef);
    tex_pair tp;                                                  /* tex_pair: the shown one and the written one take turns */
    CHECK(tex_pair_new(&tp, 480, 270, GFX_TEX_A8R8G8B8, 1) == 0);
    CHECK(tp.shown == 0 && tex_pair_shown(&tp) == &tp.tex[0] && tex_pair_wtex(&tp) == &tp.tex[1] && tex_pair_written(&tp) == tp.addr[1]);
    CHECK(tp.addr[0] != tp.addr[1] && tp.tex[0].offset != tp.tex[1].offset && tp.tex[0].pitch == 1920 && tp.tex[1].linear == 1);
    tex_pair_swap(&tp);
    CHECK(tex_pair_shown(&tp) == &tp.tex[1] && tex_pair_written(&tp) == tp.addr[0] && tex_pair_wtex(&tp) == &tp.tex[0]);
    tex_pair_swap(&tp);
    CHECK(tex_pair_shown(&tp) == &tp.tex[0]);
    CHECK(tex_pair_new(&tp, 0, 4, 0, 0) == -1);
}

int main(void)
{
    int mk, op, wr;
    ui_init(&(const ui_app){"ps3gfx kitcheck", "0.0", DIR});
    CHECK(fs_selftest(DIR, &mk, &op, &wr) == 0);
    atexit(ui_end);
    net();
    wrap_lines();
    va_calls();
    textures();
    fields();
    block();
    copies();

    /* pad_repeat: the press, then 350 ms, then every 30 ms; groups apart; a new press after a release.
     * gesture: DOWN's press at frame 10 disarms an armed SQUARE in read_pad and arms DOWN; a second call
     * in that frame does not fire; DOWN's next press, at frame 80, fires once, for DOWN's call only */
    pad_rep down = {0}, side = {0};
    unsigned fired[128] = {0}, gest[128] = {0}, gest_other = 0;
    ui_stat st;
    ui_stats(&st, 1);
    for (int f = 1; f <= 100; f++) {                        /* f is the preview's frame: block() drew frame 0 */
        if (f == 10) armed = BTN_SQUARE;
        read_pad();
        if (f == 10) CHECK(armed == 0);
        gest[f] = gesture(BTN_DOWN);
        gest_other |= gesture(BTN_DOWN | BTN_UP) | gesture(BTN_UP);
        fired[f] = pad_repeat(&down, BTN_DOWN | BTN_UP);
        CHECK(!pad_repeat(&side, BTN_LEFT | BTN_RIGHT));
        begin_frame();
        footer("L1 previous  R1 next  L1/R1 page  CROSS play");
        ui_flip();
        if (f == 50) {                                      /* the snapshot's frame: the front buffer holds it */
            CHECK(ui_shot(DIR "/shot.ppm") == 0);
            front_rows();
        }
    }
    int want[128] = {0}, ok = 1;
    want[10] = want[80] = 1;
    for (int f = 31; f <= 70; f += 2) want[f] = 1;
    for (int f = 1; f <= 100; f++)
        if (!!fired[f] != want[f] || (fired[f] && fired[f] != BTN_DOWN)) { printf("FAIL pad_repeat at frame %d: %x\n", f, fired[f]); ok = 0; }
    CHECK(ok);
    ok = !gest_other;
    for (int f = 1; f <= 100; f++)
        if (gest[f] != (f == 80 ? BTN_DOWN : 0)) { printf("FAIL gesture at frame %d: %x\n", f, gest[f]); ok = 0; }
    CHECK(ok);

    /* ui_stats on the preview's clock: 16,667 us a frame, all of it in gfx_end */
    ui_stats(&st, 1);
    CHECK(st.frames == 100 && st.period_us == 16667 && st.wait_us == 16667 && st.busy_us == 0);
    CHECK(st.period_max_us == 16667 && st.slow == 0 && st.busy_sum_us == 0 && st.wait_sum_us == 100ull * 16667);
    CHECK(st.vertices > 0 && st.draws > 0 && st.drains == 0 && st.vertices_max >= st.vertices && st.drains_sum == 0);
    ui_stats(&st, 0);
    CHECK(st.frames == 0 && st.period_max_us == 0 && st.period_us == 16667);

    /* ui_submit: a frame counts at its wait in the next begin_frame. The
     * preview's clock moves in pv_flip, at the submit, so the 16,667 us are
     * busy and the wait is 0. The tenth frame waits for text_checks' begin_frame. */
    for (int f = 0; f < 10; f++) {
        begin_frame();
        footer("L1 previous  R1 next  L1/R1 page  CROSS play");
        ui_submit();
    }
    ui_stats(&st, 1);
    CHECK(st.frames == 9 && st.period_us == 16667 && st.wait_us == 0 && st.busy_us == 16667);
    CHECK(st.wait_sum_us == 0 && st.busy_sum_us == 9ull * 16667 && st.slow == 0 && st.vertices > 0);
    text_checks();                                          /* before utf8: the bytes past ASCII */
    utf8_failure();
    utf8();                                                 /* the text stays UTF-8: the checks after it draw no text */
    init_failures();                                        /* before digit_checks: the children need the atlases not loaded */
    digit_checks();
    icon_checks();
    fx_checks();
    jobs();
    safe_area();

    if (fails) {
        printf("kitcheck: %d checks failed\n", fails);
        exit(1);
    }
    printf("kitcheck: all checks passed\n");
    exit(0);
}
