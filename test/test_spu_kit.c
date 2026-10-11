/* Host test of the SPU kit's contract (source/spu_kit.h): the report's size,
 * alignment and layout, the arg2 packing, and the local-store kernel over a
 * buffer of the SPU's area size. Injected faults (stuck-at 0 and 1, a
 * transition fault, three coupling faults, an alias, a bridge between two
 * bits, a pattern fault) are each found at their first address with the
 * word written and the word read. Four of them need one part of a pass
 * each: a coupling fault the down marches, another the last up read, the
 * bridge the background 0x55555555, the pattern fault the address pass. A
 * clean area gives no error, and the kernel writes nothing outside the area.
 * CI runs it:
 *   cc -std=c99 -Wall -Wextra -I source test/test_spu_kit.c */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* the faults, injected through SPU_KIT_RD and SPU_KIT_WR */
enum { F_NONE, F_STUCK0, F_STUCK1, F_RISE, F_CF_UP, F_CF_DOWN, F_CF_FALL, F_ALIAS, F_BRIDGE, F_PATTERN };
#define FI 1234u                             /* the faulty word */
#define FJ 5678u                             /* a second word: the coupled one, or the address that reaches FI's cell */
#define FBIT (1u << 7)
#define FBIT2 (1u << 6)                      /* the bridge: bits 6 and 7 of FI read as their AND */
static int fault;

static uint32_t rd(volatile uint32_t *w, uint32_t i)
{
    if (fault == F_ALIAS && i == FJ) i = FI;
    uint32_t v = w[i];
    if (i == FI && fault == F_STUCK0) v &= ~FBIT;
    if (i == FI && fault == F_STUCK1) v |= FBIT;
    if (i == FI && fault == F_BRIDGE && !((v & FBIT) && (v & FBIT2))) v &= ~(FBIT | FBIT2);
    if (i == FI && fault == F_PATTERN && (v >> 16) == 0xa5a5) v ^= FBIT;              /* only the address words of pass 0 */
    return v;
}

static void wr(volatile uint32_t *w, uint32_t i, uint32_t v)
{
    if (fault == F_ALIAS && i == FJ) i = FI;
    uint32_t old = w[i];
    if (i == FI && fault == F_RISE && !(old & FBIT)) v &= ~FBIT;                   /* the bit cannot rise */
    w[i] = v;
    if (fault == F_CF_UP && i == FI && !(old & FBIT) && (v & FBIT)) w[FJ] ^= FBIT;     /* a rise in FI inverts FJ, above it */
    if (fault == F_CF_DOWN && i == FJ && !(old & FBIT) && (v & FBIT)) w[FI] |= FBIT;   /* a rise in FJ sets FI, below it */
    if (fault == F_CF_FALL && i == FI && (old & FBIT) && !(v & FBIT)) w[FJ] |= FBIT;   /* a fall in FI sets FJ, above it */
}

#define SPU_KIT_RD(w, i) rd(w, i)
#define SPU_KIT_WR(w, i, v) wr(w, i, v)
#include "spu_kit.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* the area of this build: lo is _end (0x1910) rounded up to 128, as in
 * spu/main.c, hi is under the stack's 16 KB. Another lo moves only the
 * addresses. */
#define LO 0x1980u
#define HI (SPU_KIT_LS_TOP - SPU_KIT_LS_STACK)
#define N ((HI - LO) / 4)
#define GUARD 0x6b6b6b6bu
static uint32_t mem[N + 2];                  /* a guard word on each side of the area */

static spu_kit_rec run(int f, int passes)
{
    spu_kit_rec r;
    memset(&r, 0, sizeof r);
    memset(mem, 0, sizeof mem);
    mem[0] = mem[N + 1] = GUARD;
    fault = f;
    for (int p = 0; p < passes; p++) spu_kit_ls_pass(&r, mem + 1, N, LO, (uint32_t)p);
    fault = F_NONE;
    CHECK(mem[0] == GUARD && mem[N + 1] == GUARD);
    return r;
}

/* a fault: found, and first at the address of word i with the words written and read */
static void found(int f, const char *name, uint32_t i, uint32_t want, uint32_t got)
{
    spu_kit_rec r = run(f, SPU_KIT_LS_PASSES);
    int ok = r.errors > 0 && r.err_addr == LO + 4 * i && r.err_want == want && r.err_got == got;
    if (!ok) {
        printf("FAIL %s: errors %u, first at 0x%05x, wrote 0x%08x, read 0x%08x\n", name, r.errors, r.err_addr, r.err_want,
               r.err_got);
        fails++;
    } else {
        printf("%s: %u errors, first at 0x%05x: ok\n", name, r.errors, r.err_addr);
    }
}

int main(void)
{
    CHECK(sizeof(spu_kit_rec) == 128);
    CHECK(__alignof__(spu_kit_rec) == 128);
    CHECK(offsetof(spu_kit_rec, magic) == 0 && offsetof(spu_kit_rec, index) == 4 && offsetof(spu_kit_rec, mode) == 8);
    CHECK(offsetof(spu_kit_rec, sum) == 12 && offsetof(spu_kit_rec, arg0) == 16 && offsetof(spu_kit_rec, arg1) == 24);
    CHECK(offsetof(spu_kit_rec, arg2) == 32 && offsetof(spu_kit_rec, count) == 40);    /* Cell Bench's cell_rec up to here */
    CHECK(offsetof(spu_kit_rec, done) == 48 && offsetof(spu_kit_rec, lo) == 52 && offsetof(spu_kit_rec, hi) == 56);
    CHECK(offsetof(spu_kit_rec, errors) == 60 && offsetof(spu_kit_rec, err_addr) == 64);
    CHECK(offsetof(spu_kit_rec, err_want) == 68 && offsetof(spu_kit_rec, err_got) == 72);
    CHECK(offsetof(spu_kit_rec, ticks) == 76 && offsetof(spu_kit_rec, pad) == 80);
    printf("spu_kit_rec: ok\n");

    /* Cell Bench's cell_arg2, and the SPU's reading of it in spu/main.c */
    CHECK(spu_kit_arg2(SPU_KIT_LS, 3) == (5ull << 32 | 0xce11ull << 16 | 3));
    CHECK(spu_kit_arg2(SPU_KIT_HEAT, 5) == 0x6ce110005ull);
    CHECK(spu_kit_arg2(SPU_KIT_HEAT, 0x12345) == 0x6ce112345ull);                     /* the index keeps 16 bits */
    uint64_t a = spu_kit_arg2(SPU_KIT_HEAT, 4);
    CHECK((uint32_t)(a >> 32) == SPU_KIT_HEAT && ((uint32_t)a >> 16) == SPU_KIT_MARK && ((uint32_t)a & 0xffff) == 4);
    printf("spu_kit_arg2: ok\n");

    /* a clean area: 16 passes, no error, and the area holds the last address pass */
    spu_kit_rec r = run(F_NONE, SPU_KIT_LS_PASSES);
    CHECK(r.errors == 0 && r.err_addr == 0 && r.err_want == 0 && r.err_got == 0);
    int pattern = 1;
    for (uint32_t i = 0; i < N; i++) pattern &= mem[1 + i] == spu_kit_ls_addr(LO + 4 * i, SPU_KIT_LS_PASSES - 1);
    CHECK(pattern);
    CHECK(spu_kit_ls_addr(LO, 0) != spu_kit_ls_addr(LO, 1));                           /* each pass writes new words */
    printf("clean area, %u words, %d passes: ok\n", (unsigned)N, SPU_KIT_LS_PASSES);

    found(F_STUCK0, "stuck-at 0", FI, 0xffffffffu, 0xffffff7fu);                         /* up r1 w0 */
    found(F_STUCK1, "stuck-at 1", FI, 0x00000000u, 0x00000080u);                         /* up r0 w1 */
    found(F_RISE, "transition", FI, 0xffffffffu, 0xffffff7fu);                           /* up r1 w0 */
    found(F_CF_UP, "coupling, a rise inverts a word above", FJ, 0x00000000u, 0x00000080u);     /* up r0 w1 */
    found(F_CF_DOWN, "coupling, a rise sets a word below", FI, 0x00000000u, 0x00000080u);      /* only down r0 w1 */
    found(F_CF_FALL, "coupling, a fall sets a word above", FJ, 0x00000000u, 0x00000080u);      /* only the last up r0 */
    found(F_ALIAS, "alias", FJ, 0x00000000u, 0xffffffffu);                               /* up r0 w1 */
    found(F_BRIDGE, "bridge", FI, 0x55555555u, 0x55555515u);                             /* only the background 0x55555555 */
    uint32_t aw = spu_kit_ls_addr(LO + 4 * FI, 0);
    found(F_PATTERN, "pattern", FI, aw, aw ^ FBIT);                                      /* only the address pass */

    /* the count saturates, and a later error keeps the first one */
    memset(&r, 0, sizeof r);
    r.errors = 0xfffffffeu;
    r.err_addr = 0x1234;
    memset(mem, 0, sizeof mem);
    fault = F_STUCK1;
    spu_kit_ls_pass(&r, mem + 1, N, LO, 0);
    fault = F_NONE;
    CHECK(r.errors == 0xffffffffu && r.err_addr == 0x1234);
    printf("saturation: ok\n");

    if (fails) { printf("%d checks failed\n", fails); return 1; }
    printf("all checks passed\n");
    return 0;
}
