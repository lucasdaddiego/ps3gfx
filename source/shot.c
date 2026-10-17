/* The screenshot of ui.h, in its own file: an app links it, and its row
 * buffers, only when it calls it. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include "gfx.h"
#include "ui.h"

#define ROWS 64                              /* rows per write: 368 KB at 1080p */

/* One row of 0x??RRGGBB words as PPM bytes. */
static u8 *ppm_row(u8 *p, const volatile u32 *row, int w)
{
    for (int x = 0; x < w; x++) {
        u32 c = row[x];
        *p++ = (u8)(c >> 16);
        *p++ = (u8)(c >> 8);
        *p++ = (u8)c;
    }
    return p;
}

/* The rows come by gfx_copy_to_main into main memory, ROWS at a time: the
 * front buffer and each round start on a 128-byte line, and pitch x rows is
 * a multiple of 128 (the pitch is one of 64, the heights are even). A copy
 * that fails (or no memory for its rows) leaves the reads of RSX memory
 * through the mapping, about 0.5 us a word, for the rest of the picture. */
int ui_shot(const char *path)
{
    u32 pitch = 0;
    ui_wait();                               /* after a ui_submit: that frame on the TV, its times counted */
    gfx_wait();                              /* after a bare gfx_submit */
    const volatile u32 *px = gfx_front(&pitch);
    if (!px) return -1;
    u8 *buf = malloc((size_t)gfx_w * 3 * ROWS);
    if (!buf) return -1;
    u32 *copy = malloc((size_t)pitch * ROWS);
    s32 fd;
    u64 n = 0;
    s32 rc = sysLv2FsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, 0666, NULL, 0);
    if (rc) {
        free(copy);
        free(buf);
        return rc;
    }
    char head[32];
    int hl = snprintf(head, sizeof head, "P6\n%d %d\n255\n", gfx_w, gfx_h);
    rc = sysLv2FsWrite(fd, head, (u64)hl, &n);
    if (!rc && n != (u64)hl) rc = -1;
    for (int y0 = 0; !rc && y0 < gfx_h; y0 += ROWS) {
        int rows = gfx_h - y0 < ROWS ? gfx_h - y0 : ROWS;
        const volatile u32 *src = px + (size_t)y0 * (pitch / 4);
        if (copy && gfx_copy_to_main((const void *)src, copy, pitch * rows) == 0) src = copy;
        else {
            free(copy);
            copy = NULL;
        }
        u8 *p = buf;
        for (int y = 0; y < rows; y++) p = ppm_row(p, src + (size_t)y * (pitch / 4), gfx_w);
        rc = sysLv2FsWrite(fd, buf, (u64)(p - buf), &n);
        if (!rc && n != (u64)(p - buf)) rc = -1;
    }
    s32 sync_rc = sysLv2FsFsync(fd), close_rc = sysLv2FsClose(fd);
    free(copy);
    free(buf);
    if (!rc) rc = sync_rc ? sync_rc : close_rc;
    return rc;
}
