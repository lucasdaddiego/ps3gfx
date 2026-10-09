/* Host test of the freeze journal (source/journal.c, source/fs.c) against the
 * stub sys/file.h and test/lv2fs_stub.c, in a temp folder. CI runs it:
 *   cc -std=c99 -Wall -Wextra -I tools/preview/stub -I source \
 *      test/test_journal.c source/journal.c source/fs.c test/lv2fs_stub.c
 * Each journal_load() is the next start of the app: it reads the file again. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "fs.h"
#include "journal.h"

extern int stub_fail_writes;

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static char dir[256], path[300];
static char buf[65536];

/* the file's content; 0 bytes when it is missing or empty */
static int file(void)
{
    int n = fs_read_file(path, buf, sizeof buf - 1);
    if (n < 0) n = 0;
    buf[n] = 0;
    return n;
}

static void put(const char *s) { CHECK(fs_write_file(path, s, strlen(s), 0) == 0); }

/* a fresh journal in a fresh folder, as a new console: every state J_NONE */
static void fresh(void)
{
    unlink(path);
    journal_load();
}

int main(void)
{
    snprintf(dir, sizeof dir, "%s/ps3gfx_journal_%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
    if (mkdir(dir, 0700)) { perror(dir); return 1; }
    snprintf(path, sizeof path, "%s/journal.txt", dir);
    journal_init(dir);

    /* 1. a "start" without a "done" is a freeze at the next load */
    fresh();
    CHECK(journal_state("probe") == J_NONE && journal_needed("probe") == 1);
    CHECK(journal_start("probe") == 1);
    CHECK(journal_state("probe") == J_PENDING);
    journal_load();
    CHECK(journal_state("probe") == J_FROZEN);
    CHECK(journal_start("probe") == 0);                /* never runs again */
    file();
    CHECK(strcmp(buf, "start probe\nfroze probe\n") == 0);
    printf("froze at the next load: ok\n");

    /* 2. a skip persists over a load; done and needed */
    fresh();
    journal_skip("errlog");
    CHECK(journal_start("temps") == 1);
    journal_done("temps", 0, 1);
    CHECK(journal_start("fan") == 1);
    journal_done("fan", (s32)0x80010003, 0);
    journal_load();
    CHECK(journal_state("errlog") == J_SKIPPED && journal_start("errlog") == 0);
    CHECK(journal_state("temps") == J_OK && journal_needed("temps") == 0);
    CHECK(journal_state("fan") == J_BAD && journal_needed("fan") == 0);
    file();
    CHECK(strcmp(buf, "skip errlog\nstart temps\ndone temps rc=0x00000000 ok=1\nstart fan\ndone fan rc=0x80010003 ok=0\n") == 0);
    printf("skip persists: ok\n");

    /* 3. the clear gesture empties the file and the table */
    CHECK(journal_clear() == 0);
    CHECK(file() == 0);
    CHECK(journal_state("errlog") == J_NONE && journal_needed("temps") == 1);
    journal_load();
    CHECK(journal_state("errlog") == J_NONE && journal_start("errlog") == 1);
    printf("clear empties: ok\n");

    /* 4. a file over 16 KB is compacted at the load: one line per name, the frozen and skipped ones kept */
    fresh();
    buf[0] = 0;
    for (int i = 0; i < 600; i++) strcat(buf, "done fill rc=0x00000000 ok=1\n");   /* 17,400 bytes */
    strcat(buf, "skip s\nfroze f\ndone b rc=0x00000001 ok=0\nstart p\n");
    put(buf);
    journal_load();
    CHECK(journal_state("fill") == J_OK && journal_state("s") == J_SKIPPED && journal_state("f") == J_FROZEN);
    CHECK(journal_state("b") == J_BAD && journal_state("p") == J_FROZEN);
    file();
    CHECK(strcmp(buf, "done fill rc=0x00000000 ok=1\nskip s\nfroze f\ndone b rc=0x00000000 ok=0\nfroze p\n") == 0);
    printf("compaction over 16 KB: ok\n");

    /* 5. only the last 32 KB are read: a line before the tail is not seen, the newest ones are */
    fresh();
    buf[0] = 0;
    strcat(buf, "start early\n");
    for (int i = 0; i < 1200; i++) strcat(buf, "done fill rc=0x00000000 ok=1\n");  /* 34,800 bytes: the tail cuts into them */
    strcat(buf, "start late\n");
    put(buf);
    journal_load();
    CHECK(journal_state("early") == J_NONE);           /* its "start" is before the tail: no freeze recorded */
    CHECK(journal_state("fill") == J_OK && journal_state("late") == J_FROZEN);
    file();
    CHECK(strcmp(buf, "done fill rc=0x00000000 ok=1\nfroze late\n") == 0);   /* compacted too */
    printf("the 32 KB tail: ok\n");

    /* 6. a "done" after a "froze" keeps the freeze */
    fresh();
    put("froze x\ndone x rc=0x00000000 ok=1\n");
    journal_load();
    CHECK(journal_state("x") == J_FROZEN && journal_start("x") == 0);
    printf("a done after froze keeps froze: ok\n");

    /* 7. a failed write of the clear keeps the entries, in the file and in the table */
    fresh();
    journal_skip("a");
    CHECK(journal_start("b") == 1);
    stub_fail_writes = 1;
    CHECK(journal_clear() != 0);
    stub_fail_writes = 0;
    CHECK(journal_state("a") == J_SKIPPED && journal_state("b") == J_PENDING);
    file();
    CHECK(strcmp(buf, "skip a\nstart b\n") == 0);
    journal_load();
    CHECK(journal_state("a") == J_SKIPPED && journal_state("b") == J_FROZEN);
    printf("a bad write keeps the entries: ok\n");

    /* 8. the table holds JOURNAL_NAMES names; the next one shares the last slot */
    fresh();
    char name[24];
    for (int i = 0; i < JOURNAL_NAMES; i++) {
        snprintf(name, sizeof name, "n%d", i);
        CHECK(journal_state(name) == J_NONE);
    }
    journal_skip("n32");                               /* the 33rd name: slot 31 */
    CHECK(journal_state("n31") == J_SKIPPED);
    CHECK(journal_state("n32") == J_SKIPPED);
    CHECK(journal_state("n30") == J_NONE);
    printf("the 33rd name shares slot 31: ok\n");

    unlink(path);
    rmdir(dir);
    if (fails) { printf("%d checks failed\n", fails); return 1; }
    printf("all checks passed\n");
    return 0;
}
