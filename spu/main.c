/* The SPU kit's program: one ELF for the six SPU threads of an app's group,
 * built by spu-gcc with newlib's startup files and C library and no SPU
 * library (spu/Makefile). Its two modes are PS3 Health's: heat, the Cooling
 * load, and ls, the local-store test (source/spu_kit.h has the contract).
 * newlib's crt1 gives main r3, r4 and r5 (arg0, arg1 and arg2 of
 * sysSpuThreadArgument; r6, arg3, goes to __ea_local_store). main never
 * returns: after main, crt1 calls exit, whose stop code is libspe2's, not
 * LV2's. A thread ends with thread_exit (stop 0x102), or the app terminates
 * the group. No heap and no printf: a heap would start at _end, inside the
 * area of the ls test. The exit, the report and the flags are those of Cell
 * Bench's console-proven program. */
#include <stdint.h>
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include "spu_kit.h"

#define TAG 1                                /* the report */

static spu_kit_rec rec __attribute__((aligned(128)));
extern char _end[];                          /* the linker's end of the image (.bss included) */

static void report(uint64_t ea)
{
    mfc_put(&rec, ea, sizeof rec, TAG, 0, 0);
    mfc_write_tag_mask(1 << TAG);
    mfc_read_tag_status_all();
}

/* LV2's SPU thread exit: the status into the outbound mailbox, then stop
 * 0x102 (PSL1GHT's spu_thread_exit is the same two lines). */
static void __attribute__((noreturn)) thread_exit(uint32_t status)
{
    spu_writech(SPU_WrOutMbox, status);
    spu_stop(0x102);
    for (;;)
        ;
}

/* An address the app sent: 128-byte aligned, not 0, inside the 32-bit user
 * space. Anything else means the arguments did not arrive as sent. */
static int ea_ok(uint64_t ea)
{
    return ea != 0 && (ea & 127) == 0 && (ea >> 32) == 0;
}

/* heat: eight independent 4-lane multiply-add chains x = x * x + c keep the
 * float pipeline busy (8 fma, an add and a branch per loop). c is -1.984375,
 * -127/64: exact, so spu-gcc (which rounds a constant toward zero) and a
 * host compiler give the same bits. On [c, c * c + c] = [-1.984375,
 * 1.953369140625], x * x is at most c * c, so each value stays in that
 * interval (the SPU truncates toward zero) and no value runs off. The 64
 * possible start values, (8 * (index & 7) + k + 1) / 64 for chain k, never
 * settle: a host simulation of the SPU's arithmetic (fused, truncated toward
 * zero) puts each into a cycle of 361, 781 or 9,747 values. The eight values
 * of a thread stay different, so about 13 of the 32 bits change from one fma
 * to the next. A report every SPU_KIT_HEAT_BATCH loops carries the count and
 * a fold of the chains, so the compiler keeps the work. The mode never
 * exits: the app terminates the group. */
static void __attribute__((noreturn, noinline)) heat(uint64_t ea, uint32_t index)
{
    const vec_float4 c = spu_splats(-1.984375f);
    const float d = 1.0f / 64, b = (8 * (index & 7) + 1) * d;    /* multiples of 1/64: exact */
    vec_float4 a0 = spu_splats(b), a1 = spu_splats(b + d), a2 = spu_splats(b + 2 * d), a3 = spu_splats(b + 3 * d);
    vec_float4 a4 = spu_splats(b + 4 * d), a5 = spu_splats(b + 5 * d), a6 = spu_splats(b + 6 * d), a7 = spu_splats(b + 7 * d);
    uint64_t n = 0;
    for (;;) {
        for (uint32_t i = 0; i < SPU_KIT_HEAT_BATCH; i++) {
            a0 = spu_madd(a0, a0, c);
            a1 = spu_madd(a1, a1, c);
            a2 = spu_madd(a2, a2, c);
            a3 = spu_madd(a3, a3, c);
            a4 = spu_madd(a4, a4, c);
            a5 = spu_madd(a5, a5, c);
            a6 = spu_madd(a6, a6, c);
            a7 = spu_madd(a7, a7, c);
        }
        n += SPU_KIT_HEAT_BATCH;
        vec_float4 s = spu_add(spu_add(spu_add(a0, a1), spu_add(a2, a3)), spu_add(spu_add(a4, a5), spu_add(a6, a7)));
        rec.sum = spu_extract((vec_uint4)s, 0);
        rec.count = n;
        report(ea);
    }
}

/* ls: the kernel over [lo, hi), lo the end of the image rounded up to 128,
 * hi under the stack's 16 KB. Nothing is written into the area when it is
 * under 4 KB or when this frame is less than 1 KB above hi. The decrementer
 * counts the timebase down once written (Cell BE Handbook 17.4: a new
 * thread's may be stopped). A report after each pass, so the count moves
 * while the test runs, then one with the ticks and done = 1. */
static void __attribute__((noreturn, noinline)) ls(uint64_t ea, uint32_t index)
{
    uint32_t lo = ((uint32_t)(uintptr_t)_end + 127u) & ~127u;
    uint32_t hi = SPU_KIT_LS_TOP - SPU_KIT_LS_STACK;
    uint32_t sp = (uint32_t)(uintptr_t)__builtin_frame_address(0);
    rec.lo = lo;
    rec.hi = hi;
    if (lo + 4096 > hi || sp < hi + 1024) {
        rec.done = 1;
        report(ea);
        thread_exit(SPU_KIT_EXIT_ROOM | index);
    }
    spu_writech(SPU_WrDec, 0xffffffffu);
    uint32_t d0 = spu_readch(SPU_RdDec);
    volatile uint32_t *w = (volatile uint32_t *)(uintptr_t)lo;
    for (uint32_t p = 0; p < SPU_KIT_LS_PASSES; p++) {
        spu_kit_ls_pass(&rec, w, (hi - lo) / 4, lo, p);
        rec.count = p + 1;
        report(ea);
    }
    rec.ticks = d0 - spu_readch(SPU_RdDec);  /* the decrementer counts down */
    rec.done = 1;
    report(ea);
    thread_exit(SPU_KIT_EXIT_LS | index);
}

int main(uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    uint32_t mode = (uint32_t)(arg2 >> 32), index = (uint32_t)arg2 & 0xffff;

    if (((uint32_t)arg2 >> 16) != SPU_KIT_MARK || !ea_ok(arg0)) thread_exit(SPU_KIT_EXIT_ARGS);
    if (mode != SPU_KIT_LS && mode != SPU_KIT_HEAT) thread_exit(SPU_KIT_EXIT_MODE);
    /* rec lies in .bss: cleared here, so no field depends on how the loader
     * fills .bss (done, errors and count start at 0). Volatile stores: GCC
     * would turn a plain loop into a memset call, which moves _end. */
    for (unsigned i = 0; i < sizeof rec / sizeof(vec_uint4); i++) ((volatile vec_uint4 *)&rec)[i] = spu_splats(0u);
    rec.magic = SPU_KIT_MAGIC;
    rec.index = index;
    rec.mode = mode;
    rec.arg0 = arg0;
    rec.arg1 = arg1;
    rec.arg2 = arg2;
    report(arg0);                            /* the start: the app waits for six */
    if (mode == SPU_KIT_HEAT) heat(arg0, index);
    ls(arg0, index);
}
