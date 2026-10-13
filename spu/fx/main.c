/* The effects program: one ELF for the six SPU threads of an app's group,
 * built by spu-gcc with newlib's startup files and C library and no SPU
 * library (spu/fx/Makefile). <ps3gfx/spu_fx.h> has the contract and the job
 * loop, <ps3gfx/spu_fx_kern.h> the kernels and the job drivers, which the
 * host test runs too; this file is the SPU's side: the arguments, the
 * control block's DMA, the output's DMA, the report and the exit. newlib's
 * crt1 gives main r3, r4 and r5 (arg0, arg1 and arg2 of
 * sysSpuThreadArgument). main never returns: a thread ends with thread_exit
 * (stop 0x102), or the app terminates the group. No heap and no printf.
 *
 * The local store (spu-nm, README.md): the code, the noise table (16 KB),
 * the rain's particles (24.7 KB, kept from one rain job to the next), and
 * one union of the jobs' buffers (sky 73 KB); _end at most 0x28000, the
 * stack in the top 16 KB. */
#include <stdint.h>
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include "spu_fx_kern.h"

#define TAG 1                                /* the report, the control block */
#define TAG_BUF 2                            /* 2 and 3: the two output buffers */

static fx_rec rec __attribute__((aligned(128)));
static fx_ctl ctl __attribute__((aligned(128)));
static uint32_t tab[FX_NOISE_N * FX_NOISE_N] __attribute__((aligned(128)));
static fx_rain_state rain;                   /* 128-byte aligned by its type */
static union {
    struct {
        fx_sky_ws ws;
        uint16_t band[3 * FX_GLASS_BAND_MAX * FX_GLASS_W];
    } sky;
    fx_aur_ws aur;
    fx_vtx quads[2][FX_RAIN_CHUNK * 4];
} u __attribute__((aligned(128)));

static uint64_t out_ea[2];                   /* the job's ctl.out and ctl.out2 */

static void wait_tags(uint32_t mask)
{
    mfc_write_tag_mask(mask);
    mfc_read_tag_status_all();
}

/* the drivers' output: one DMA (at most 16 KB, 16-byte aligned on both
 * sides: the drivers' pieces are) into the job's output */
static void put(fx_io *io, uint32_t out, uint32_t offset, const void *ls, uint32_t bytes, uint32_t buf)
{
    (void)io;
    mfc_put((volatile void *)(uintptr_t)ls, out_ea[out & 1] + offset, bytes, TAG_BUF + (buf & 1), 0, 0);
}

static void wait(fx_io *io, uint32_t buf)
{
    (void)io;
    wait_tags(1u << (TAG_BUF + (buf & 1)));
}

static fx_io io = {put, wait};

static void report(uint64_t ea)
{
    mfc_put(&rec, ea, sizeof rec, TAG, 0, 0);
    wait_tags(1 << TAG);
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
 * space. */
static int ea_ok(uint64_t ea)
{
    return ea != 0 && (ea & 127) == 0 && (ea >> 32) == 0;
}

int main(uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    uint32_t index = (uint32_t)arg2 & 0xffff;

    if ((arg2 >> 16) != FX_MARK || !ea_ok(arg0) || !ea_ok(arg1)) thread_exit(FX_EXIT_ARGS);
    /* rec and the rain's state lie in .bss: cleared here, so nothing depends
     * on how the loader fills .bss. Volatile stores: GCC would turn a plain
     * loop into a memset call. */
    for (unsigned i = 0; i < sizeof rec / sizeof(vec_uint4); i++) ((volatile vec_uint4 *)&rec)[i] = spu_splats(0u);
    *(volatile uint32_t *)&rain.ready = 0;
    rec.magic = FX_MAGIC;
    rec.index = index;
    rec.arg0 = arg0;
    rec.arg1 = arg1;
    rec.arg2 = arg2;
    rec.check_row = FX_NO_CHECK;
    /* the decrementer counts the timebase down once written (Cell BE
     * Handbook 17.4: a new thread's may be stopped); written once, here */
    spu_writech(SPU_WrDec, 0xffffffffu);
    fx_noise_init(tab);
    rec.kat = fx_kat();
    report(arg0);                            /* the start: the app waits for six */
    for (;;) {
        uint32_t word = spu_readch(SPU_RdInMbox);
        mfc_get(&ctl, arg1, sizeof ctl, TAG, 0, 0);
        wait_tags(1 << TAG);
        rec.word = word;
        rec.mode = ctl.mode;
        if (ctl.exit) {
            rec.serial = ctl.serial;
            rec.done = 2;
            report(arg0);
            thread_exit(FX_EXIT_DONE | index);
        }
        if (ctl.serial != word) {            /* the block is not this job's: no job, no output */
            rec.serial = word;
            rec.status = FX_ST_SERIAL;
            rec.done = 1;
            report(arg0);
            continue;
        }
        uint32_t d0 = spu_readch(SPU_RdDec), rc;
        out_ea[0] = ctl.out;
        out_ea[1] = ctl.out2;
        if (ctl.mode == FX_SKY) {
            if (!ea_ok(ctl.out) || !ea_ok(ctl.out2)) thread_exit(FX_EXIT_CTL | index);
            rc = fx_sky_job(&ctl, tab, &u.sky.ws, u.sky.band, FX_GLASS_BAND_MAX, &io, &rec);
        } else if (ctl.mode == FX_AURORA) {
            if (!ea_ok(ctl.out)) thread_exit(FX_EXIT_CTL | index);
            rc = fx_aurora_job(&ctl, tab, &u.aur, &io, &rec);
        } else if (ctl.mode == FX_RAIN) {
            if (!ea_ok(ctl.out)) thread_exit(FX_EXIT_CTL | index);
            rc = fx_rain_job(&ctl, &rain, u.quads, &io, &rec);
        } else
            thread_exit(FX_EXIT_MODE | index);
        if (rc) thread_exit(rc | index);
        rec.ticks = d0 - spu_readch(SPU_RdDec);   /* the decrementer counts down */
        rec.count++;
        rec.serial = ctl.serial;
        rec.status = FX_ST_OK;
        rec.done = 1;
        report(arg0);
    }
}
