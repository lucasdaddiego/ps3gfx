/* One-line settings files and a delete (fs.h), apart from fs.c: every app
 * links fs.o (the journal, the self-test), and these only when it calls
 * them. */
#include <stdio.h>
#include <sys/file.h>
#include "fs.h"

static int edge(char c) { return c == ' ' || c == '\t'; }

int fs_read_line(const char *path, char *buf, int n)
{
    if (n < 1) return -1;
    int len = fs_read_file(path, buf, n - 1);
    if (len < 0) return -1;
    buf[len] = 0;
    int b = 0, e = 0;
    while (buf[e] && buf[e] != '\n' && buf[e] != '\r') e++;
    while (e > 0 && edge(buf[e - 1])) e--;
    while (b < e && edge(buf[b])) b++;
    for (int i = b; i < e; i++) buf[i - b] = buf[i];
    buf[e - b] = 0;
    return e - b;
}

/* The file is replaced (O_TRUNC, as safe_set): a cut write leaves a short
 * line, and the app then uses its default, as safe_load does with an area
 * it cannot read. */
int fs_write_line(const char *path, const char *s)
{
    char line[256];
    int n = snprintf(line, sizeof line, "%s\n", s);
    if (n < 0 || n >= (int)sizeof line) return -1;
    return fs_write_file(path, line, (uint64_t)n, 0);
}

int fs_remove(const char *path) { return sysLv2FsUnlink(path); }
