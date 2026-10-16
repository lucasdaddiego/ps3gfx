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

static char jpath[128];                      /* <dir>/journal.txt, from journal_init */
static struct { char name[24]; int state; } jst[JOURNAL_NAMES];
static int jcount, jloaded, jiofailed;

void journal_init(const char *dir)
{
    snprintf(jpath, sizeof jpath, "%s/journal.txt", dir);
    jloaded = 0;
}

/* The table keeps the first 23 characters of a name, as journal_load reads
 * them from the file: a longer name matches its cut form, so its freeze
 * holds. Two names with the same first 23 characters share one state. */
static int *jstate(const char *name)
{
    for (int i = 0; i < jcount; i++)
        if (!strncmp(jst[i].name, name, sizeof jst[i].name - 1)) return &jst[i].state;
    if (jcount == JOURNAL_NAMES) return &jst[JOURNAL_NAMES - 1].state;
    snprintf(jst[jcount].name, sizeof jst[jcount].name, "%s", name);
    jst[jcount].state = J_NONE;
    return &jst[jcount++].state;
}

static int jappend(const char *fmt, const char *name, s32 rc, int ok)
{
    char line[96];
    snprintf(line, sizeof line, fmt, name, (unsigned)rc, ok);
    int write_rc = fs_write_file(jpath, line, strlen(line), 1);
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
    s32 rc = sysLv2FsOpen(jpath, SYS_O_RDONLY, &fd, 0, NULL, 0);
    if (rc) {
        jiofailed = rc != (s32)0x80010006;    /* ENOENT: a fresh journal */
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
    if (jiofailed) return;                  /* incomplete history: no guarded calls */
    if (partial) {
        line[used] = 0;
        jparse(line);
        if (jappend("\n", "", 0, 0)) return;  /* separate a torn last record from future appends */
    }
    for (int i = 0; i < jcount; i++)
        if (jst[i].state == J_PENDING) {
            jst[i].state = J_FROZEN;
            jappend("froze %s\n", jst[i].name, 0, 0);
        }
}

/* The clear gesture: an empty journal, so the next start runs every call again. */
int journal_clear(void)
{
    jready();
    int rc = fs_write_file(jpath, "", 0, 0);
    if (rc == 0) { jcount = 0; jiofailed = 0; }             /* a failed write keeps the old entries, in the file and here */
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
    jappend("done %s rc=0x%08x ok=%d\n", name, rc, ok);
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
