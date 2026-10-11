/* The screenshot of ui.h, in its own file: an app links it, and its row
 * buffer, only when it calls it. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include "gfx.h"
#include "ui.h"

#define ROWS 64                              /* rows per write: 368 KB at 1080p */

int ui_shot(const char *path)
{
    u32 pitch = 0;
    ui_wait();                               /* after a ui_submit: that frame on the TV, its times counted */
    gfx_wait();                              /* after a bare gfx_submit */
    const volatile u32 *px = gfx_front(&pitch);
    if (!px) return -1;
    u8 *buf = malloc((size_t)gfx_w * 3 * ROWS);
    if (!buf) return -1;
    s32 fd;
    u64 n = 0;
    s32 rc = sysLv2FsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, 0666, NULL, 0);
    if (rc) {
        free(buf);
        return rc;
    }
    char head[32];
    int hl = snprintf(head, sizeof head, "P6\n%d %d\n255\n", gfx_w, gfx_h);
    rc = sysLv2FsWrite(fd, head, (u64)hl, &n);
    if (!rc && n != (u64)hl) rc = -1;
    for (int y0 = 0; !rc && y0 < gfx_h; y0 += ROWS) {
        u8 *p = buf;
        for (int y = y0; y < y0 + ROWS && y < gfx_h; y++) {
            const volatile u32 *row = px + (size_t)y * (pitch / 4);
            for (int x = 0; x < gfx_w; x++) {
                u32 c = row[x];
                *p++ = (u8)(c >> 16);
                *p++ = (u8)(c >> 8);
                *p++ = (u8)c;
            }
        }
        rc = sysLv2FsWrite(fd, buf, (u64)(p - buf), &n);
        if (!rc && n != (u64)(p - buf)) rc = -1;
    }
    sysLv2FsFsync(fd);
    sysLv2FsClose(fd);
    free(buf);
    return rc;
}
