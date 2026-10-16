/* Host test of the one-line files (source/fs_line.c) against the stub
 * sys/file.h and test/lv2fs_stub.c, in a temp folder. CI runs it:
 *   cc -std=c99 -Wall -Wextra -I tools/preview/stub -I source \
 *      test/test_fs.c source/fs_line.c source/fs.c test/lv2fs_stub.c */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "fs.h"

extern int stub_fail_sync, stub_fail_close, stub_short_writes;

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static char dir[256], path[300];

static void put(const char *s) { CHECK(fs_write_file(path, s, strlen(s), 0) == 0); }

/* the first line of content, as fs_read_line gives it in n bytes */
static int line_is(const char *content, int n, const char *want)
{
    char buf[64];
    put(content);
    int r = fs_read_line(path, buf, n);
    return r == (int)strlen(want) && !strcmp(buf, want);
}

int main(void)
{
    char buf[64], file[300];
    snprintf(dir, sizeof dir, "%s/ps3gfx_fs_%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
    if (mkdir(dir, 0700)) { perror(dir); return 1; }
    snprintf(path, sizeof path, "%s/lp10.txt", dir);

    CHECK(fs_read_line(path, buf, sizeof buf) == -1);                 /* missing */
    CHECK(line_is("192.168.0.13\n", 64, "192.168.0.13"));
    CHECK(line_is("192.168.0.13\r\nsecond line\n", 64, "192.168.0.13"));
    CHECK(line_is("no line end", 64, "no line end"));
    CHECK(line_is("", 64, ""));
    CHECK(line_is("\nthe second line", 64, ""));
    CHECK(line_is("  \t 10.0.0.2 \t \nnext", 64, "10.0.0.2"));
    CHECK(line_is("   \n", 64, ""));
    CHECK(line_is("0123456789abcdef", 8, "0123456"));                  /* n bytes with the NUL: 7 characters */
    CHECK(line_is("  0123456789", 8, "01234"));                         /* the cut first, then the edges */
    put("x");
    CHECK(fs_read_line(path, buf, 0) == -1);
    printf("fs_read_line: ok\n");

    put("a much longer old content that the write must replace\n");
    CHECK(fs_write_line(path, "192.168.0.20") == 0);
    CHECK(fs_read_file(path, file, sizeof file) == 13 && !memcmp(file, "192.168.0.20\n", 13));
    CHECK(fs_read_line(path, buf, sizeof buf) == 12 && !strcmp(buf, "192.168.0.20"));
    char big[300];
    memset(big, '9', 255);
    big[255] = 0;                                                       /* 255 characters: one too many */
    CHECK(fs_write_line(path, big) == -1);
    CHECK(fs_read_line(path, buf, sizeof buf) == 12);                   /* the file stays as it was */
    big[254] = 0;
    CHECK(fs_write_line(path, big) == 0);
    printf("fs_write_line: ok\n");

    CHECK(fs_remove(path) == 0);
    CHECK(fs_read_line(path, buf, sizeof buf) == -1);
    CHECK(fs_remove(path) != 0);                                        /* already gone */
    printf("fs_remove: ok\n");

    int *faults[] = { &stub_fail_sync, &stub_fail_close, &stub_short_writes };
    for (unsigned i = 0; i < sizeof faults / sizeof faults[0]; i++) {
        int mk, op, wr;
        *faults[i] = 1;
        CHECK(fs_write_file(path, "record\n", 7, 1) != 0);
        CHECK(fs_selftest(dir, &mk, &op, &wr) != 0);
        *faults[i] = 0;
    }
    CHECK(fs_write_file(path, "record\n", 7, 1) == 0);
    CHECK(fs_remove(path) == 0);
    snprintf(path, sizeof path, "%s/starts.txt", dir);
    CHECK(fs_remove(path) == 0);
    printf("write, sync and close errors propagated: ok\n");
    rmdir(dir);
    if (fails) { printf("%d checks failed\n", fails); return 1; }
    printf("all checks passed\n");
    return 0;
}
