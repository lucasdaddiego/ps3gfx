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

extern int stub_fail_writes, stub_fail_sync, stub_fail_close, stub_fail_reads, stub_short_writes;

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

    /* 4. loading a long history preserves it and every frozen/skipped entry */
    fresh();
    buf[0] = 0;
    for (int i = 0; i < 600; i++) strcat(buf, "done fill rc=0x00000000 ok=1\n");   /* 17,400 bytes */
    strcat(buf, "skip s\nfroze f\ndone b rc=0x00000001 ok=0\nstart p\n");
    put(buf);
    journal_load();
    CHECK(journal_state("fill") == J_OK && journal_state("s") == J_SKIPPED && journal_state("f") == J_FROZEN);
    CHECK(journal_state("b") == J_BAD && journal_state("p") == J_FROZEN);
    file();
    CHECK(strlen(buf) > 16000 && strstr(buf, "start p\nfroze p\n") != NULL);
    printf("long history retained: ok\n");

    /* 5. freezes, declines and pending calls before the last 32 KB survive */
    fresh();
    buf[0] = 0;
    strcat(buf, "start early\nfroze frozen\nskip declined\nstart completed\n");
    for (int i = 0; i < 1200; i++) strcat(buf, "done fill rc=0x00000000 ok=1\n");  /* 34,800 bytes: the tail cuts into them */
    strcat(buf, "start late\ndone completed rc=0x00000000 ok=1\n");
    put(buf);
    journal_load();
    CHECK(journal_state("early") == J_FROZEN && journal_start("early") == 0);
    CHECK(journal_state("frozen") == J_FROZEN && journal_start("frozen") == 0);
    CHECK(journal_state("declined") == J_SKIPPED && journal_start("declined") == 0);
    CHECK(journal_state("completed") == J_OK);
    CHECK(journal_state("fill") == J_OK && journal_state("late") == J_FROZEN);
    file();
    CHECK(strlen(buf) > 32768);
    journal_load();
    CHECK(journal_start("early") == 0 && journal_start("frozen") == 0 && journal_start("declined") == 0);
    printf("full history beyond 32 KB: ok\n");

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
    char name[24], last[24], before[24];
    for (int i = 0; i < JOURNAL_NAMES; i++) {
        snprintf(name, sizeof name, "n%d", i);
        CHECK(journal_state(name) == J_NONE);
    }
    snprintf(name, sizeof name, "n%d", JOURNAL_NAMES);         /* one name more than the table */
    snprintf(last, sizeof last, "n%d", JOURNAL_NAMES - 1);
    snprintf(before, sizeof before, "n%d", JOURNAL_NAMES - 2);
    journal_skip(name);
    CHECK(journal_state(last) == J_SKIPPED);
    CHECK(journal_state(name) == J_SKIPPED);
    CHECK(journal_state(before) == J_NONE);
    printf("name %d shares slot %d: ok\n", JOURNAL_NAMES + 1, JOURNAL_NAMES - 1);

    /* 9. a name of 24 characters or more: the file has it whole, the table and
     * the loader its first 23, so the freeze holds; a polled one is journaled
     * once; a name with the same first 23 characters shares its state */
    fresh();
    const char *lng = "couch_lp10_reconnect_tcp";                 /* 24 characters */
    CHECK(journal_start(lng) == 1);
    journal_load();
    CHECK(journal_state(lng) == J_FROZEN && journal_start(lng) == 0);
    file();
    CHECK(strcmp(buf, "start couch_lp10_reconnect_tcp\nfroze couch_lp10_reconnect_tc\n") == 0);
    fresh();
    for (int i = 0; i < 40; i++)                                   /* 40 polls of a 24-character name */
        if (journal_needed("couch_board_weather_poll")) {
            CHECK(journal_start("couch_board_weather_poll") == 1);
            journal_done("couch_board_weather_poll", 0, 1);
        }
    int nl = 0;
    file();
    for (const char *c = buf; *c; c++) nl += *c == '\n';
    CHECK(nl == 2);
    CHECK(journal_state("couch_board_weather_pollen") == J_OK);
    CHECK(journal_state("couch_board_weather") == J_NONE);
    printf("a long name keeps its freeze: ok\n");

    /* A failed write, sync, close or short write latches journal_io_failed
     * until a clear or a reload; the call still runs (1). */
    int *faults[] = { &stub_fail_writes, &stub_fail_sync, &stub_fail_close, &stub_short_writes };
    for (unsigned i = 0; i < sizeof faults / sizeof faults[0]; i++) {
        fresh();
        CHECK(journal_io_failed() == 0);
        *faults[i] = 1;
        CHECK(journal_start("unrecorded") == 1);
        CHECK(journal_io_failed() == 1);
        *faults[i] = 0;
        CHECK(journal_start("another") == 1 && journal_io_failed() == 1);
        CHECK(journal_clear() == 0);
        CHECK(journal_io_failed() == 0);
    }
    fresh();
    journal_skip("declined");
    stub_fail_reads = 1;
    journal_load();
    CHECK(journal_io_failed() == 1 && journal_start("new") == 1);
    stub_fail_reads = 0;
    journal_load();
    CHECK(journal_io_failed() == 0 && journal_state("declined") == J_SKIPPED && journal_start("declined") == 0);
    printf("I/O failures latch journal_io_failed, the calls still run: ok\n");

    fresh();
    put("start torn");
    journal_load();
    CHECK(journal_state("torn") == J_FROZEN);
    CHECK(journal_start("next") == 1);
    journal_done("next", 0, 1);
    journal_load();
    CHECK(journal_state("torn") == J_FROZEN && journal_state("next") == J_OK);
    printf("unterminated last record: ok\n");

    unlink(path);
    rmdir(dir);
    if (fails) { printf("%d checks failed\n", fails); return 1; }
    printf("all checks passed\n");
    return 0;
}
