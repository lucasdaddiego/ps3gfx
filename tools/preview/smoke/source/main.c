/* The preview smoke: the smallest app on the frame, for the host only (CI
 * builds it with tools/preview/build.zsh APP=tools/preview/smoke and takes
 * one snapshot). No Makefile, no sfo.xml, no title id: it never runs on a
 * console. It includes the headers as an app on the console does; the host
 * preview resolves <ps3gfx/...> to the checkout's own headers. It also draws
 * a 4 x 4 A8R8G8B8 texture, 64 px a texel with nearest filtering, and CI's
 * check_tex8.py finds each texel's colour in a 64 x 64 block of the
 * snapshot: the 8-bit texture path end to end on the host. R1 turns to
 * three more pages, the snapshots of the display digits, the icons and fx.h
 * (CI takes each twice and compares the bytes):
 *   1  frosted glass over a picture (tex_pair, tex_upload, tex_round,
 *      glass_uv), a halo (glow9), a curve (strip, area), a turned quad;
 *   2  the display digits and a flip card at four phases (digits_half);
 *   3  every icon, the duotone layers tinted, and two turned ones. */
#include <stdlib.h>
#include <math.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/gfx.h>
#include <ps3gfx/fs.h>
#include <ps3gfx/digits.h>
#include <ps3gfx/icons.h>
#include <ps3gfx/fx.h>

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
    u32 *p = gfx_tex_new(&tex8, 4, 4, GFX_TEX_A8R8G8B8, 0);   /* four rows of pitch 64 */
    if (!p) return;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) p[j * tex8.pitch / 4 + i] = tex8_colour(i, j);
    tex8_ok = 1;
}

/* ---- the pages of fx.h, the digits and the icons --------------------------- */

static int page, digits_ok, icons_ok, fx_ok;
static tex_pair sky, glass;                  /* a 64 x 36 picture over the screen, and its blurred copy */
static gfx_tex halo;

#define SKY_W 64
#define SKY_H 36

/* A sky with two light blobs, then a 5 x 5 box blur of it: the glass. */
static void fx_make(void)
{
    static u32 pic[SKY_W * SKY_H], blur[SKY_W * SKY_H];
    if (tex_pair_new(&sky, SKY_W, SKY_H, GFX_TEX_A8R8G8B8, 1) || tex_pair_new(&glass, SKY_W, SKY_H, GFX_TEX_A8R8G8B8, 1)) return;
    for (int y = 0; y < SKY_H; y++)
        for (int x = 0; x < SKY_W; x++) {
            int b1 = 900 - ((x - 18) * (x - 18) + (y - 10) * (y - 10)) * 6, b2 = 700 - ((x - 46) * (x - 46) + (y - 24) * (y - 24)) * 5;
            int l = (b1 > 0 ? b1 : 0) + (b2 > 0 ? b2 : 0);
            int r = 0x2a + y * 2 + l / 9, g = 0x46 + y * 3 + l / 8, bl = 0x9c + y + l / 12;
            pic[y * SKY_W + x] = 0xff000000u | (u32)(r > 255 ? 255 : r) << 16 | (u32)(g > 255 ? 255 : g) << 8 | (u32)(bl > 255 ? 255 : bl);
        }
    for (int y = 0; y < SKY_H; y++)
        for (int x = 0; x < SKY_W; x++) {
            u32 sr = 0, sg = 0, sb = 0, n = 0;
            for (int j = y - 2; j <= y + 2; j++)
                for (int i = x - 2; i <= x + 2; i++) {
                    u32 c = pic[(j < 0 ? 0 : j >= SKY_H ? SKY_H - 1 : j) * SKY_W + (i < 0 ? 0 : i >= SKY_W ? SKY_W - 1 : i)];
                    sr += c >> 16 & 255, sg += c >> 8 & 255, sb += c & 255, n++;
                }
            blur[y * SKY_W + x] = 0xff000000u | (sr / n) << 16 | (sg / n) << 8 | (sb / n);
        }
    tex_upload(tex_pair_wtex(&sky), tex_pair_written(&sky), pic, SKY_W);
    tex_upload(tex_pair_wtex(&glass), tex_pair_written(&glass), blur, SKY_W);
    tex_pair_swap(&sky);
    tex_pair_swap(&glass);
    u32 *h = gfx_tex_new(&halo, 64, 64, GFX_TEX_A8R8G8B8, 1);   /* white, the alpha falls off from a rounded rect */
    if (!h) return;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float dx = fabsf(x + 0.5f - 32) - 8, dy = fabsf(y + 0.5f - 32) - 8;
            float d = sqrtf((dx > 0 ? dx * dx : 0) + (dy > 0 ? dy * dy : 0)) / 24;
            int a = d >= 1 ? 0 : (int)(255 * (1 - d) * (1 - d));
            h[y * halo.pitch / 4 + x] = (u32)a << 24 | 0xffffff;
        }
    fx_ok = 1;
}

static void glass_tile(float x, float y, float w, float h)
{
    float u0, v0, u1, v1;
    glass_uv(x, y, w, h, &u0, &v0, &u1, &v1);
    tex_round(tex_pair_shown(&glass), x, y, w, h, 20, u0, v0, u1, v1, 0xffffffff);
    round_rect(x, y, w, h, 20, 0x0b142680);
    rect(x + 20, y, w - 40, 1, 0xffffff22);
    rect(x + 20, y + h - 1, w - 40, 1, 0x0000002e);
}

static void page_fx(void)
{
    float ox, oy;
    screen_origin(&ox, &oy);
    if (!fx_ok) {
        text(MG, TOP, F_MED, YELLOW, "No RSX memory for the textures of fx.h");
        return;
    }
    tex_rect(tex_pair_shown(&sky), ox, oy, 1920, 1080, 0xffffffff);
    glass_tile(MG, TOP, 760, 300);
    text(MG + 32, TOP + 32, F_MED, WHITE, "Frosted glass");
    text(MG + 32, TOP + 84, F_BODY, GREY, "tex_round with glass_uv: the blurred copy of the sky behind it.");
    float pts[24];
    for (int i = 0; i < 12; i++) pts[2 * i] = MG + 64 + i * 58, pts[2 * i + 1] = TOP + 200 + ((i * 37) % 11) * 6 - (i > 6 ? 30 : 0);
    area(pts, 12, TOP + 280, 0xffffff40, 0xffffff00);
    strip(pts, 12, 3, WHITE);
    glow9(&halo, MG + 860, TOP + 40, 420, 220, 24, 0xffb35c90);
    glass_tile(MG + 860, TOP + 40, 420, 220);
    text(MG + 892, TOP + 72, F_MED, WHITE, "A halo");
    quad_rot(&halo, MG + 1500, TOP + 150, 160, 100, 0.5f, 0.2f, 0.2f, 0.8f, 0.8f, 0x4fd1c5ff);
}

/* A flip card: '3' turns to '4' at phase ph (0 to 1). The new top and the
 * old bottom stay; the old top folds down over the new one (to 0.5), then
 * the new bottom unfolds over the old one. Each moving half has its own
 * opaque piece of card behind it, and the fold darkens as it falls. */
static void flip_card(float x, float y, float ph)
{
    const u32 card = 0x1f3256ff;
    float w = digits_w(D_L, "0") + 12, xl = x - 6, top = y - 6, bot = y + digits_h(D_L) + 6, mid = y + digits_mid(D_L);
    round_rect(xl, top, w, bot - top, 10, card);
    digits_half(x, y, D_L, '4', 0, 1, WHITE);
    digits_half(x, y, D_L, '3', 1, 1, WHITE);
    if (ph < 0.5f) {
        float c = cosf(ph * 3.14159265f), h = (mid - top) * c;
        rect(xl, mid - h, w, h, card);
        digits_half(x, y, D_L, '3', 0, c, WHITE);
        rect(xl, mid - h, w, h, (u32)(ph * 2 * 0x60));
    } else {
        float sn = sinf((ph - 0.5f) * 3.14159265f);
        rect(xl, mid, w, (bot - mid) * sn, card);
        digits_half(x, y, D_L, '4', 1, sn, WHITE);
    }
    rect(xl, mid - 1, w, 2, 0x050a14ff);
}

static void page_digits(void)
{
    if (!digits_ok) {
        text(MG, TOP, F_MED, YELLOW, "digits_init failed");
        return;
    }
    float y = TOP;
    digits(MG, y, D_XL, WHITE, "19\xc2\xb0");
    digits(MG + 360, y, D_XL, GREY, "-3,5\xb0");
    y += digits_h(D_XL);
    float x = digits(MG, y, D_L, WHITE, "14:47");
    text(x + 16, y + digits_mid(D_L) - line_h(F_MED) / 2, F_MED, GREY, ":03");
    digits_r(SW - MG, y, D_M, WHITE, "1.540,00");
    digits_r(SW - MG, y + digits_h(D_M), D_M, 0xffb35cff, "+0,3%");
    y += digits_h(D_L) + 40;
    for (int k = 0; k < 4; k++) {
        flip_card(MG + 40 + k * 220, y, k * 0.25f);
        text(MG + 40 + k * 220, y + digits_h(D_L) + 16, F_SMALL, DIM, "phase %d%%", k * 25);
    }
}

static void page_icons(void)
{
    if (!icons_ok) {
        text(MG, TOP, F_MED, YELLOW, "icons_init failed");
        return;
    }
    for (int id = IC_S_CARET_UP; id <= IC_S_UMBRELLA; id++) icon(id, MG + 24 + (id - IC_S_CARET_UP) * 56, TOP + 24, id % 3 ? WHITE : BLUE);
    for (int k = 0; k <= (IC_M_CLOUD_LIGHTNING_LINE - IC_M_SUN_FILL) / 2; k++) {
        float cx = MG + 40 + k * 110, cy = TOP + 140;
        icon(IC_M_SUN_FILL + 2 * k, cx, cy, 0xffd36b55);
        icon(IC_M_SUN_LINE + 2 * k, cx, cy, WHITE);
    }
    for (int k = 0; k <= (IC_L_SNOWFLAKE_LINE - IC_L_SUN_FILL) / 2; k++) {
        float cx = MG + 100 + k * 220, cy = TOP + 330;
        icon(IC_L_SUN_FILL + 2 * k, cx, cy, 0x6cb8ff55);
        icon(IC_L_SUN_LINE + 2 * k, cx, cy, WHITE);
    }
    icon_rot(IC_L_SUN_FILL, MG + 1300, TOP + 330, 0.3f, 1, 0xffd36b55);
    icon_rot(IC_L_SUN_LINE, MG + 1300, TOP + 330, 0.3f, 1, 0xffd36bff);
    icon_rot(IC_L_CLOUD_LINE, MG + 1560, TOP + 330, 0, 0.5f, GREY);
}

static void screen(void)
{
    begin_frame();
    if (page) {
        static const char *const names[] = {"", "fx.h", "digits", "icons"};
        if (page == 1) page_fx();
        else if (page == 2) page_digits();
        else page_icons();
        title();
        title_right("%s", names[page]);
        footer("R1 next page  START exit");
        ui_flip();
        return;
    }
    title();
    float y = TOP + 20, w = SW - 2 * MG;
    card(MG, y, w, 160);
    text(MG + 32, y + 32, F_MED, WHITE, "One card on the frame");
    if (fs_ok) text(MG + 32, y + 92, F_BODY, GREY, "The app folder is writable: starts.txt has one more line.");
    else text(MG + 32, y + 92, F_BODY, YELLOW, "The app folder is not writable: mkdir %d, open %d, write %d.", fs_mk, fs_op, fs_wr);
    if (tex8_ok) tex_rect(&tex8, MG + 32, y + 200, 256, 256, 0xffffffff);
    footer("CROSS nothing  START exit");
    ui_flip();
}

int main(void)
{
    ui_init(&(const ui_app){"ps3gfx smoke", "0.0", SMOKE_DIR});
    fs_ok = fs_selftest(SMOKE_DIR, &fs_mk, &fs_op, &fs_wr) == 0;
    tex8_make();
    digits_ok = digits_init() == 0;
    icons_ok = icons_init() == 0;
    fx_make();
    atexit(ui_end);                          /* START, Quit Game and the script's EXIT (exit() in pv_flip) */
    while (!quit) {
        read_pad();
        if (ui_leave() < 0) break;
        if (pressed & BTN_R1) page = (page + 1) % 4;
        screen();
    }
    return 0;
}
