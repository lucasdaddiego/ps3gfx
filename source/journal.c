/* The freeze journal: journal.txt in the app folder. "start X" (synced) goes
 * in before a journaled call and "done X" after it. A "start" without a
 * "done" means the console froze inside X: the next start writes "froze X"
 * and never runs X again. "skip X" is a first-run prompt declined. SQUARE
 * twice on the home screen empties the file. */
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include "fs.h"
#include "journal.h"

static char jpath[128];                      /* <dir>/journal.txt, from journal_init; "" when it did not fit */
static struct { char name[24]; int state; } jst[JOURNAL_NAMES];
static int jcount, jloaded, jiofailed;
static int jtorn;                            /* the file may end inside a record: the next append starts a line first */

void journal_init(const char *dir)
{
    int n = snprintf(jpath, sizeof jpath, "%s/journal.txt", dir);
    if (n < 0 || n >= (int)sizeof jpath) jpath[0] = 0;   /* a cut path would name another file */
    jloaded = 0;
}

/* A name as the file holds it and journal_load reads it back (sscanf's
 * %23s): from its first character that is not a space to the next space, at
 * most 23 characters. Two names with the same cut form ("drive probe" and
 * "drive test", or the same first 23 characters) share one state. */
static int jspace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }   /* isspace in the C locale, as sscanf reads */

static void jcut(char *out, const char *name)
{
    int i = 0;
    while (jspace(*name)) name++;
    while (i < 23 && name[i] && !jspace(name[i])) out[i] = name[i], i++;
    out[i] = 0;
}

static int *jstate(const char *name)
{
    char cut[24];
    jcut(cut, name);
    for (int i = 0; i < jcount; i++)
        if (!strcmp(jst[i].name, cut)) return &jst[i].state;
    if (jcount == JOURNAL_NAMES) return &jst[JOURNAL_NAMES - 1].state;
    memcpy(jst[jcount].name, cut, sizeof cut);
    jst[jcount].state = J_NONE;
    return &jst[jcount++].state;
}

static int jwrite(const char *data, u64 len, int append)
{
    return jpath[0] ? fs_write_file(jpath, data, len, append) : -1;
}

/* One record, written once more when the write fails (a record after a short
 * write starts on a line of its own). A record that fails twice latches
 * journal_io_failed. */
static int jappend(const char *fmt, const char *name, s32 rc, int ok)
{
    char cut[24], line[64];
    int write_rc = 0;
    jcut(cut, name);
    for (int k = 0; k < 2; k++) {
        line[0] = '\n';
        snprintf(line + jtorn, sizeof line - jtorn, fmt, cut, (unsigned)rc, ok);
        write_rc = jwrite(line, strlen(line), 1);
        jtorn = write_rc != 0;
        if (!write_rc) break;
    }
    if (write_rc) jiofailed = 1;
    return write_rc;
}

/* The file is read once, at the first journal call of the session; drive_probe
 * reloads it explicitly. */
static void jready(void) { if (!jloaded) journal_load(); }

static void jparse(char *line)
{
    char verb[8], name[24];
    if (sscanf(line, "%7s %23s", verb, name) != 2) return;
    char *okp = strstr(line, "ok=");
    int ok = okp && okp[3] == '1';
    int *s = jstate(name);
    if (!strcmp(verb, "start")) *s = J_PENDING;
    else if (!strcmp(verb, "done") && *s != J_FROZEN) *s = ok ? J_OK : J_BAD;
    else if (!strcmp(verb, "froze")) *s = J_FROZEN;
    else if (!strcmp(verb, "skip")) *s = J_SKIPPED;
}

/* Stream the entire history with bounded memory: old freezes and declines
 * remain effective however much a later session writes. Keep the file
 * append-only until an explicit clear; rewriting it in place to compact it
 * could erase that history on a short write or a power loss. */
void journal_load(void)
{
    char text[4096], line[96];
    s32 fd;
    unsigned used = 0;
    int partial = 0;
    jcount = 0;
    jloaded = 1;
    jiofailed = 0;
    jtorn = 0;
    if (!jpath[0]) {
        jiofailed = 1;
        return;
    }
    s32 rc = sysLv2FsOpen(jpath, SYS_O_RDONLY, &fd, 0, NULL, 0);
    if (rc) {
        jiofailed = jtorn = rc != (s32)0x80010006;   /* ENOENT: a fresh journal; else its end is not known */
        return;
    }
    for (;;) {
        u64 n = 0;
        rc = sysLv2FsRead(fd, text, sizeof text, &n);
        if (rc) { jiofailed = 1; break; }
        if (!n) break;
        for (u64 i = 0; i < n; i++) {
            if (text[i] == '\n') {
                line[used] = 0;
                jparse(line);
                used = 0;
                partial = 0;
            } else {
                if (used < sizeof line - 1) line[used++] = text[i];
                partial = 1;
            }
        }
    }
    if (sysLv2FsClose(fd)) jiofailed = 1;
    if (jiofailed) {                         /* a part not read may hold a start's done: no freeze from this history */
        jtorn = 1;
        return;
    }
    if (partial) {
        line[used] = 0;
        jparse(line);
        jtorn = 1;                           /* a torn last record: the next append starts a line first */
    }
    for (int i = 0; i < jcount; i++)
        if (jst[i].state == J_PENDING) {
            jst[i].state = J_FROZEN;
            jappend("froze %s\n", jst[i].name, 0, 0);
        }
}

/* The clear gesture: an empty journal, so the next start runs every call
 * again. A failed clear keeps the table and latches journal_io_failed; the
 * file keeps its entries when the open failed, and may be empty already
 * when only the sync or the close failed (O_TRUNC). */
int journal_clear(void)
{
    jready();
    int rc = jwrite("", 0, 0);
    if (rc == 0) { jcount = 0; jiofailed = 0; jtorn = 0; }
    else jiofailed = 1;
    return rc;
}

int journal_state(const char *name)
{
    jready();
    return *jstate(name);
}

/* The first-run prompt was declined: remembered until the journal is cleared. */
void journal_skip(const char *name)
{
    jready();
    jappend("skip %s\n", name, 0, 0);
    *jstate(name) = J_SKIPPED;
}

int journal_start(const char *name)
{
    jready();
    int *s = jstate(name);
    if (*s == J_FROZEN || *s == J_SKIPPED) return 0;
    jappend("start %s\n", name, 0, 0);      /* a failed write latches journal_io_failed; the call still runs (as before 2026-10) */
    *s = J_PENDING;
    return 1;
}

void journal_done(const char *name, s32 rc, int ok)
{
    jready();
    jappend("done %s rc=0x%08x ok=%d\n", name, rc, ok != 0);   /* ok=1 or 0: the loader reads only those */
    *jstate(name) = ok ? J_OK : J_BAD;
}

/* A polled call (the Cooling module reads every 2 s) is journaled when its
 * state is not yet known in this session: the first call, and the first call
 * after SQUARE twice cleared the journal. */
int journal_needed(const char *name)
{
    jready();
    int s = *jstate(name);
    return s != J_OK && s != J_BAD;
}

int journal_io_failed(void) { jready(); return jiofailed; }
