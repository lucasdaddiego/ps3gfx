/* Whole files through the sysLv2Fs* syscalls (the sysFs* calls are stubs for
 * the cellFs module, which an app must load first), and the write test at
 * start. */
#ifndef FS_H
#define FS_H

#include <stdint.h>

int fs_write_file(const char *path, const void *data, uint64_t len, int append);   /* 0, or the LV2 rc (-1: short write) */
int fs_read_file(const char *path, void *data, int n);       /* bytes read, -1 when missing or the read fails */
/* makes dir, appends "start" to dir/starts.txt: 0, or -1 with the rcs (open_rc -1: dir too long; write_rc -1: a short write) */
int fs_selftest(const char *dir, int *mkdir_rc, int *open_rc, int *write_rc);

/* One-line settings files (fs_line.c: an app links it only when it calls
 * one). fs_read_line: the file's first line in buf (n bytes with the NUL),
 * without the line end and the spaces and tabs at its edges; its length, or
 * -1 when the file is missing or cannot be read. fs_write_line: the file becomes s and a line
 * end (s of at most 254 characters): 0, or the LV2 rc (-1: s too long or a
 * short write). fs_remove: deletes the file: 0, or the LV2 rc. */
int fs_read_line(const char *path, char *buf, int n);
int fs_write_line(const char *path, const char *s);
int fs_remove(const char *path);

#endif
