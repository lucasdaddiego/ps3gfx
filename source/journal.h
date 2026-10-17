/* The freeze journal: a file in the app folder that records each journaled
 * call before it runs, so that a call which froze the console never runs
 * again (journal.c has the file format). */
#ifndef JOURNAL_H
#define JOURNAL_H

#include <ppu-types.h>

/* Journal state of one named call. SKIPPED = declined at the first-run prompt,
 * kept until SQUARE twice clears the journal. */
enum { J_NONE, J_PENDING, J_OK, J_BAD, J_FROZEN, J_SKIPPED };

/* The table: 64 names; more share the last slot. A name counts up to its
 * first space and at most 23 characters, in the file as in the table: names
 * with the same cut form share one state, and a name with no character but
 * spaces is never recorded. */
#define JOURNAL_NAMES 64

void journal_init(const char *dir);                    /* the app folder: the file is <dir>/journal.txt */
void journal_load(void);                               /* streams the whole file with bounded memory; a "start" without a "done" becomes froze; the others call it once, at their first use */
int journal_clear(void);                               /* the clear gesture: an empty journal, so the next start runs every call again; 0, or the write rc */
int journal_state(const char *name);                   /* the J_* state of a named call */
void journal_skip(const char *name);                   /* the first-run prompt was declined: remembered until the journal is cleared */
int journal_start(const char *name);                   /* "start name" before the call: 1, or 0 for a frozen or skipped entry (do not run it) */
void journal_done(const char *name, s32 rc, int ok);   /* "done name rc= ok=" after the call (ok: 0 or not 0) */
/* 1 after a read, or a record written twice, or a clear failed (until a
 * reload or a clear): the history may be short, a freeze may go unrecorded,
 * and a call whose done record failed shows as frozen at the next start */
int journal_io_failed(void);
int journal_needed(const char *name);                  /* a polled call is journaled when its state is not yet known in this session */

#endif
