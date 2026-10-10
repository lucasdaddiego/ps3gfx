/* The preview smoke: the smallest app on the frame, for the host only (CI
 * builds it with tools/preview/build.zsh APP=tools/preview/smoke and takes
 * one snapshot). No Makefile, no sfo.xml, no title id: it never runs on a
 * console. It includes the headers as an app on the console does; the host
 * preview resolves <ps3gfx/...> to the checkout's own headers. It also draws
 * a 4 x 4 A8R8G8B8 texture, 64 px a texel with nearest filtering, and CI's
 * check_tex8.py finds each texel's colour in a 64 x 64 block of the
 * snapshot: the 8-bit texture path end to end on the host. */
#include <stdlib.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/gfx.h>
#include <ps3gfx/fs.h>

#define SMOKE_DIR "/dev_hdd0/tmp/ps3gfx_smoke"

static int fs_ok, fs_mk, fs_op, fs_wr;
static gfx_tex tex8;
static int tex8_ok;

/* Texel (i, j): colours no part of the frame uses (check_tex8.py repeats the formula). */
static u32 tex8_colour(int i, int j)
{
    return 0xff000000u | (u32)(0x13 + 0x20 * i) << 16 | (u32)(0x17 + 0x20 * j) << 8 | (u32)(0xc5 - 0x10 * (i + j));
}

static void tex8_make(void)
{
    u32 off = 0;
    u32 *p = gfx_vram(4 * 64, 128, &off);   /* four rows of pitch 64 */
    if (!p) return;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) p[j * 16 + i] = tex8_colour(i, j);
    tex8 = (gfx_tex){off, 4, 4, 64, 0, GFX_TEX_A8R8G8B8};
    tex8_ok = 1;
}

static void screen(void)
{
    begin_frame();
    title();
    float y = TOP + 20, w = SW - 2 * MG;
    card(MG, y, w, 160);
    text(MG + 32, y + 32, F_MED, WHITE, "One card on the frame");
    if (fs_ok) text(MG + 32, y + 92, F_BODY, GREY, "The app folder is writable: starts.txt has one more line.");
    else text(MG + 32, y + 92, F_BODY, YELLOW, "The app folder is not writable: mkdir %d, open %d, write %d.", fs_mk, fs_op, fs_wr);
    if (tex8_ok) {
        float x0 = MG + 32, y0 = y + 200;
        gfx_texture(&tex8);
        gfx_vtx *q = gfx_prim(GFX_QUADS, 4);
        if (q) {
            q = gfx_put(q, x0, y0, 0xffffffff, 0, 0);
            q = gfx_put(q, x0 + 256, y0, 0xffffffff, 1, 0);
            q = gfx_put(q, x0 + 256, y0 + 256, 0xffffffff, 1, 1);
            gfx_put(q, x0, y0 + 256, 0xffffffff, 0, 1);
        }
        gfx_texture(NULL);
    }
    footer("CROSS nothing  START exit");
    ui_flip();
}

int main(void)
{
    ui_init(&(const ui_app){"ps3gfx smoke", "0.0", SMOKE_DIR});
    fs_ok = fs_selftest(SMOKE_DIR, &fs_mk, &fs_op, &fs_wr) == 0;
    tex8_make();
    atexit(ui_end);                          /* START, Quit Game and the script's EXIT (exit() in pv_flip) */
    while (!quit) {
        read_pad();
        if (ui_leave() < 0) break;
        screen();
    }
    return 0;
}
