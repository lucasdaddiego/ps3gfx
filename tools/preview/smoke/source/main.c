/* The preview smoke: the smallest app on the frame, for the host only (CI
 * builds it with tools/preview/build.zsh APP=tools/preview/smoke and takes
 * one snapshot). No Makefile, no sfo.xml, no title id: it never runs on a
 * console. It includes the headers as an app on the console does; the host
 * preview resolves <ps3gfx/...> to the checkout's own headers. */
#include <stdlib.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/fs.h>

#define SMOKE_DIR "/dev_hdd0/tmp/ps3gfx_smoke"

static int fs_ok, fs_mk, fs_op, fs_wr;

static void screen(void)
{
    begin_frame();
    title();
    float y = TOP + 20, w = SW - 2 * MG;
    card(MG, y, w, 160);
    text(MG + 32, y + 32, F_MED, WHITE, "One card on the frame");
    if (fs_ok) text(MG + 32, y + 92, F_BODY, GREY, "The app folder is writable: starts.txt has one more line.");
    else text(MG + 32, y + 92, F_BODY, YELLOW, "The app folder is not writable: mkdir %d, open %d, write %d.", fs_mk, fs_op, fs_wr);
    footer("CROSS nothing  START exit");
    ui_flip();
}

int main(void)
{
    ui_init(&(const ui_app){"ps3gfx smoke", "0.0", SMOKE_DIR});
    fs_ok = fs_selftest(SMOKE_DIR, &fs_mk, &fs_op, &fs_wr) == 0;
    atexit(ui_end);                          /* START, Quit Game and the script's EXIT (exit() in pv_flip) */
    while (!quit) {
        read_pad();
        if (ui_leave() < 0) break;
        screen();
    }
    return 0;
}
