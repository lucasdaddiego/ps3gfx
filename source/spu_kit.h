/* The SPU kit: what the kit's SPU program (spu/main.c, committed as
 * data/spu_kit.spu, the archive's spu_kit.spu.o member), an app's PPU code
 * and the host share. The library makes no LV2 call: the app imports
 * spu_kit_spu, makes the group of six threads, starts, terminates or joins
 * and destroys it, and journals those calls (README.md, "The SPU kit").
 * spu-gcc, ppu-gcc and the host compiler build this file. */
#ifndef SPU_KIT_H
#define SPU_KIT_H

#include <stdint.h>

#define SPU_KIT_SPUS 6                       /* the SPUs an app gets on a PS3 (sysSpuInitialize(6, 0)) */
#define SPU_KIT_MAGIC 0x43454c4cu            /* "CELL": the report arrived */
#define SPU_KIT_MARK 0xce11u                 /* bits 16..31 of arg2, Cell Bench's mark (one family of programs): the arguments arrived as sent */
#define SPU_KIT_TERM_VALUE 0x5ed             /* heat: the app's terminate value, back from the join */

/* The modes, in bits 32..63 of arg2. 1 to 4 are Cell Bench's own program
 * (echo, spin, galaxy, Mandelbrot). */
enum { SPU_KIT_LS = 5, SPU_KIT_HEAT = 6 };

/* The thread's exit status (sysSpuThreadGetExitStatus after the join). heat
 * never exits: the app terminates the group. */
#define SPU_KIT_EXIT_ARGS 0x200              /* the mark or arg0 was not as sent: no DMA */
#define SPU_KIT_EXIT_MODE 0x300              /* an unknown mode: no DMA */
#define SPU_KIT_EXIT_LS 0x500                /* | index: ls ran, the result is in the report */
#define SPU_KIT_EXIT_ROOM 0x600              /* | index: the area is under 4 KB or the stack reaches it: the report has lo and hi, the area was not written */

#define SPU_KIT_HEAT_BATCH (1u << 20)        /* heat: loops between two reports */
#define SPU_KIT_LS_PASSES 16                 /* ls: passes over the area, a report after each */
#define SPU_KIT_LS_TOP 0x40000u              /* the local store: 256 KB */
#define SPU_KIT_LS_STACK 0x4000u             /* ls: the top 16 KB stay for the stack, so the area ends at 0x3c000 */

/* The arguments of each thread (sysSpuThreadArgument): arg0 is the EA of its
 * report (128-byte aligned, below 4 GB), arg1 is 0, arg2 is
 * spu_kit_arg2(mode, index). LV2 puts arg0 to arg3 into r3 to r6, and
 * newlib's crt1 gives main only r3 to r5: arg3 never reaches main. */
static inline uint64_t spu_kit_arg2(uint32_t mode, uint32_t index)
{
    return (uint64_t)mode << 32 | (uint64_t)SPU_KIT_MARK << 16 | (index & 0xffff);
}

/* One thread's report: one 128-byte DMA to one cache line. Each thread puts
 * one at its start (the app waits for six), heat one every
 * SPU_KIT_HEAT_BATCH loops, ls one after each pass. The first 48 bytes are
 * Cell Bench's cell_rec. The app reads it with volatile reads. */
typedef struct {
    uint32_t magic;                          /* SPU_KIT_MAGIC once the thread wrote the report */
    uint32_t index, mode;                    /* from arg2 */
    uint32_t sum;                            /* heat: a fold of the chains, so the compiler keeps the work */
    uint64_t arg0, arg1, arg2;               /* the three arguments main received */
    uint64_t count;                          /* heat: the loops so far; ls: the passes done */
    uint32_t done;                           /* ls: 1 in the last report, just before the exit */
    uint32_t lo, hi;                         /* ls: the area [lo, hi) of the local store */
    uint32_t errors;                         /* ls: the wrong reads, saturating at 0xffffffff */
    uint32_t err_addr, err_want, err_got;    /* ls: the first wrong read: its local-store address, the word written, the word read */
    uint32_t ticks;                          /* ls: decrementer ticks of the test (the timebase, 79.8 MHz on every PS3) */
    uint32_t pad[12];
} __attribute__((aligned(128))) spu_kit_rec;

_Static_assert(sizeof(spu_kit_rec) == 128, "spu_kit_rec is one 128-byte DMA");

/* The local-store kernel, plain C: the SPU runs it over its own local store,
 * the host test and an app's preview twin over a buffer. A pass is March C-
 * (up w0; up r0 w1; up r1 w0; down r0 w1; down r1 w0; up r0) on the
 * background 0x00000000, then on 0x55555555, then each word holds its own
 * address XOR a constant of the pass. addr0 is the local-store address of
 * w[0], so the host reports the addresses an SPU reports. The first wrong
 * read goes into err_addr, err_want and err_got, and errors counts them all.
 * SPU_KIT_RD and SPU_KIT_WR are macros, so the host test can inject faults:
 * it defines them before the include. */
#ifndef SPU_KIT_RD
#define SPU_KIT_RD(w, i) ((w)[i])
#endif
#ifndef SPU_KIT_WR
#define SPU_KIT_WR(w, i, v) ((w)[i] = (v))
#endif

static inline void spu_kit_ls_got(spu_kit_rec *r, uint32_t addr, uint32_t want, uint32_t got)
{
    if (got == want) return;
    if (!r->errors) {
        r->err_addr = addr;
        r->err_want = want;
        r->err_got = got;
    }
    if (r->errors != 0xffffffffu) r->errors++;
}

/* reads word i of w and checks it (r, w and addr0 of the caller) */
#define SPU_KIT_CHECK(i, want) spu_kit_ls_got(r, addr0 + 4 * (i), (want), SPU_KIT_RD(w, i))

static inline void spu_kit_ls_march(spu_kit_rec *r, volatile uint32_t *w, uint32_t n, uint32_t addr0, uint32_t d0)
{
    uint32_t d1 = ~d0, i;
    for (i = 0; i < n; i++) SPU_KIT_WR(w, i, d0);
    for (i = 0; i < n; i++) { SPU_KIT_CHECK(i, d0); SPU_KIT_WR(w, i, d1); }
    for (i = 0; i < n; i++) { SPU_KIT_CHECK(i, d1); SPU_KIT_WR(w, i, d0); }
    for (i = n; i-- > 0;) { SPU_KIT_CHECK(i, d0); SPU_KIT_WR(w, i, d1); }
    for (i = n; i-- > 0;) { SPU_KIT_CHECK(i, d1); SPU_KIT_WR(w, i, d0); }
    for (i = 0; i < n; i++) SPU_KIT_CHECK(i, d0);
}

/* the address pass's word for the local-store address addr */
static inline uint32_t spu_kit_ls_addr(uint32_t addr, uint32_t pass)
{
    return addr ^ (0xa5a5a5a5u >> (pass & 15));
}

static inline void spu_kit_ls_pass(spu_kit_rec *r, volatile uint32_t *w, uint32_t n, uint32_t addr0, uint32_t pass)
{
    uint32_t i;
    spu_kit_ls_march(r, w, n, addr0, 0x00000000u);
    spu_kit_ls_march(r, w, n, addr0, 0x55555555u);
    for (i = 0; i < n; i++) SPU_KIT_WR(w, i, spu_kit_ls_addr(addr0 + 4 * i, pass));
    for (i = 0; i < n; i++) SPU_KIT_CHECK(i, spu_kit_ls_addr(addr0 + 4 * i, pass));
}

#undef SPU_KIT_CHECK

#ifndef __SPU__
/* The program: the stripped SPU ELF, 128-byte aligned (bin2s -a 128), for
 * sysSpuImageImport. An app that references neither symbol links neither
 * the member nor an SPU syscall. */
extern const uint8_t spu_kit_spu[];
extern const uint32_t spu_kit_spu_size;
#endif

#endif
