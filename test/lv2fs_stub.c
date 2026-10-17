/* The sysLv2Fs* calls of the stub sys/file.h on POSIX, for test_journal.c and test_fs.c:
 * no path mapping (the test passes a temp folder to journal_init), and
 * stub_fail_writes refuses every open for writing with the LV2 rc that the
 * preview's sandbox returns, so a test can see a failed write. The *_once
 * faults hit the next open for writing, or the next write, only. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>

int stub_fail_writes, stub_fail_sync, stub_fail_close, stub_fail_reads, stub_short_writes;
int stub_fail_once, stub_short_once;

s32 sysLv2FsOpen(const char *path, s32 of, s32 *fd, u32 mode, const void *arg, u64 sz)
{
    (void)mode; (void)arg; (void)sz;
    int f = (of & 3) == SYS_O_WRONLY ? O_WRONLY : (of & 3) == SYS_O_RDWR ? O_RDWR : O_RDONLY;
    if (of & SYS_O_CREAT) f |= O_CREAT;
    if (of & SYS_O_EXCL) f |= O_EXCL;
    if (of & SYS_O_TRUNC) f |= O_TRUNC;
    if (of & SYS_O_APPEND) f |= O_APPEND;
    if (stub_fail_writes && (of & 3)) return (s32)0x80010006;
    if (stub_fail_once && (of & 3)) {
        stub_fail_once = 0;
        return (s32)0x80010006;
    }
    int r = open(path, f, 0666);
    if (r < 0) return errno == ENOENT ? (s32)0x80010006 : (s32)0x80010009;
    *fd = r;
    return 0;
}
s32 sysLv2FsClose(s32 fd) { int rc = close(fd); return stub_fail_close ? -1 : rc; }
s32 sysLv2FsRead(s32 fd, void *b, u64 l, u64 *n) { if (stub_fail_reads) { *n = 0; return -1; } ssize_t r = read(fd, b, l); *n = r > 0 ? (u64)r : 0; return r < 0 ? -1 : 0; }
s32 sysLv2FsWrite(s32 fd, const void *b, u64 l, u64 *n)
{
    if ((stub_short_writes || stub_short_once) && l) l /= 2;
    stub_short_once = 0;
    ssize_t r = write(fd, b, l);
    *n = r > 0 ? (u64)r : 0;
    return r < 0 ? -1 : 0;
}
s32 sysLv2FsFsync(s32 fd) { (void)fd; return stub_fail_sync ? -1 : 0; }
s32 sysLv2FsMkdir(const char *path, s32 mode) { (void)mode; return mkdir(path, 0777) ? (s32)0x80010006 : 0; }
s32 sysLv2FsUnlink(const char *path) { return unlink(path) ? (s32)0x80010006 : 0; }
