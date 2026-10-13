/* The effects program: what the SPU program spu_fx (spu/fx/main.c,
 * committed as data/spu_fx.spu, the archive's spu_fx.spu.o member), an app's
 * PPU code and the host share. Couch 0.3.0's design, section 3 and 4.4: the
 * clouds and the frosted glass (FX_SKY), the rain and the snow (FX_RAIN) and
 * the aurora (FX_AURORA). The kernels are in <ps3gfx/spu_fx_kern.h>.
 *
 * spu_fx is a second program, not new modes of the SPU kit: PS3 Health
 * embeds spu_kit.spu, so any change to that program changes Health's ELF,
 * and the kit's local-store test needs the kit's image under 0x8000, while
 * this program keeps about 110 KB of buffers. An app that references neither
 * spu_fx_spu nor spu_fx_spu_size links neither the member nor this code.
 *
 * The library makes no LV2 call: the app imports spu_fx_spu, makes the group
 * of six threads, gives each its control block and its report, sends the job
 * words, and journals the calls (Couch's spu_fx_run.c).
 *
 * The job loop. Each thread has a 128-byte control block (fx_ctl) and a
 * 128-byte report (fx_rec) in main memory. For a job the app writes the
 * block, then __sync_synchronize(), then the job's serial into the thread's
 * inbound mailbox (sysSpuThreadWriteMb). The thread reads the block by DMA
 * and runs the job only when the block's serial equals the mailbox word;
 * otherwise it puts a report with status FX_ST_SERIAL, writes nothing else,
 * and waits for the next word. After the job it puts the report (serial,
 * done = 1). The mode is a field of each job's block: one thread may run a
 * sky job, then an aurora job. The rain's particles stay in the thread's
 * local store from one rain job to the next. The app sends a word only to a
 * thread whose last report came, so the 4-deep mailbox never fills.
 *
 * spu-gcc, ppu-gcc and the host compiler build this file. */
#ifndef SPU_FX_H
#define SPU_FX_H

#include <stdint.h>

#define FX_SPUS 6                            /* the threads of the group */
#define FX_MAGIC 0x43454c4cu                 /* "CELL": the report arrived (the family's word) */
#define FX_MARK 0xce11u                      /* bits 16..31 of arg2: Cell Bench's and the kit's mark (one family) */

/* The modes, in a job's control block. 1 to 4 are Cell Bench's own
 * program, 5 and 6 the SPU kit's. */
enum { FX_SKY = 10, FX_RAIN = 11, FX_AURORA = 12 };

/* The thread's exit status (sysSpuThreadGetExitStatus after the join). */
#define FX_EXIT_ARGS 0x200                   /* the mark, arg0 or arg1 was not as sent: no DMA */
#define FX_EXIT_MODE 0x300                   /* | index: a job of an unknown mode: no output, no report */
#define FX_EXIT_CTL 0x400                    /* | index: a job's addresses or numbers were out of range: no output, no report */
#define FX_EXIT_DONE 0x700                   /* | index: a clean exit after the exit flag; the last report has done = 2 */

/* A report's status */
#define FX_ST_OK 0
#define FX_ST_SERIAL 1                       /* the block's serial was not the mailbox word: the job did not run */

/* The pictures: A8R8G8B8, rows of PITCH bytes from gfx_tex_new (the app
 * checks that its texture's pitch is this). */
#define FX_SKY_W 480                         /* the clouds: drawn over the screen, 4x, bilinear */
#define FX_SKY_H 270
#define FX_SKY_PITCH (FX_SKY_W * 4)          /* 1,920 B: a multiple of 128 */
#define FX_GLASS_W 240                       /* the frosted glass: the sky blurred */
#define FX_GLASS_H 135
#define FX_GLASS_PITCH (FX_GLASS_W * 4)      /* 960 B: a multiple of 64 (gfx_tex_new's rows) */
#define FX_AURORA_W 640                      /* the aurora: drawn over the screen, 3x, bilinear */
#define FX_AURORA_H 360
#define FX_AURORA_PITCH (FX_AURORA_W * 4)    /* 2,560 B: a multiple of 128 */

#define FX_SKY_BAND_MAX 54                   /* sky: the most picture rows of one job (5 bands of 54) */
#define FX_GLASS_MARGIN 9                    /* sky: glass rows above and below a band (3 box passes of radius 3) */
#define FX_GLASS_BAND_MAX (FX_SKY_BAND_MAX / 2 + 2 * FX_GLASS_MARGIN)   /* 45 rows in the local store */

#define FX_RAIN_MAX 1500                     /* rain drops */
#define FX_SNOW_MAX 300                      /* snow flakes */
#define FX_SPLASHES 40                       /* rain: splash quads after the drops */
#define FX_RAIN_CHUNK 200                    /* quads per DMA: 200 x 80 B = 16,000 B */
#define FX_RAIN_MARGIN 64.0f                 /* rain: the screen plus this on the left and right (the x wrap) */
enum { FX_DROPS = 0, FX_FLAKES = 1 };        /* a rain job's kind */

#define FX_NO_CHECK 0xffffffffu              /* check_row: no row */
#define FX_KAT 0x1184536du                   /* the report's kat: the known-answer test of the vector helpers */

/* What the app writes for one thread before each job's mailbox word: one
 * 128-byte DMA. Colours are 0x00RRGGBB. Coordinates of the sky are picture
 * pixels (480 x 270), of the rain canvas units (ox, oy is the screen's top
 * left, from screen_origin). */
typedef struct {
    uint32_t serial;                         /* the job: the mailbox word too */
    uint32_t mode;                           /* FX_SKY, FX_RAIN, FX_AURORA */
    uint32_t exit;                           /* 1: the thread ends (exit status FX_EXIT_DONE | index) */
    uint32_t check_row;                      /* sky, aurora: the picture row whose word sum the report has */
    uint64_t out;                            /* sky, aurora: the EA of row 0 of the picture; rain: of the block's first vertex; 128-byte aligned */
    uint64_t out2;                           /* sky: the EA of row 0 of the glass, 128-byte aligned */
    uint32_t y0, y1;                         /* sky, aurora: the job's rows [y0, y1) (sky: even) */
    float time;                              /* seconds: the clouds' change, the aurora's motion, the snow's sway */
    uint32_t seed;                           /* the look: noise offsets; rain: the random numbers */
    union {
        struct {
            float off_x, off_y;              /* the clouds' drift in picture pixels (the app adds wind x dt each frame) */
            float cover;                     /* 0 no cloud to 1 a full deck */
            float alpha;                     /* the alpha of a thick cloud, 0 to 1 */
            float sun_x, sun_y;              /* the sun's place in picture pixels */
            float sun_gain;                  /* 0: no sun light (night, a storm) */
            float flash;                     /* 0 to 1: lightning lights the clouds */
            float edge_amt;                  /* 0 to 1: the edge colour on thin cloud (twilight) */
            uint32_t lit, shade, edge;       /* the cloud colours: lit side, thick core, thin edge */
            uint32_t sky[4];                 /* the sky gradient at 0, 55 %, 80 % and 100 % of the height (the glass source) */
        } sky;
        struct {
            uint32_t count;                  /* drops (at most FX_RAIN_MAX) or flakes (FX_SNOW_MAX) */
            uint32_t kind;                   /* FX_DROPS or FX_FLAKES */
            uint32_t reset;                  /* 1: new particles over the screen */
            uint32_t rgb;                    /* the colour (the alpha is per particle) */
            float dt;                        /* seconds since the last job (at most 0.1) */
            float wind;                      /* horizontal px/s (fx_rain_wind); capped at 70 % of fall */
            float fall;                      /* the nearest drops' fall in px/s (rain 1,300, snow 90) */
            float ox, oy;                    /* the screen's top left in canvas units */
            float alpha;                     /* 0 to 1: every particle's alpha times this */
        } rain;
        struct {
            float gain;                      /* 0 to 1; 1 is the cap (mean brightness at most 20 %) */
            uint32_t top, bottom;            /* the night sky behind the curtains */
        } aurora;
        uint32_t words[16];
    } p;
    uint32_t pad[4];
} __attribute__((aligned(128))) fx_ctl;

_Static_assert(sizeof(fx_ctl) == 128, "fx_ctl is one 128-byte DMA");

/* One thread's report: one 128-byte DMA to one cache line. The first 48
 * bytes are the kit's spu_kit_rec and Cell Bench's cell_rec. Each thread
 * puts one at its start (serial 0, done 0: the app waits for six), one
 * after each job, and one at the exit flag (done 2). The app reads it with
 * volatile reads. */
typedef struct {
    uint32_t magic;                          /* FX_MAGIC once the thread wrote the report */
    uint32_t index, mode;                    /* from arg2; mode: the job's mode */
    uint32_t sum;                            /* sky, aurora: the word sum of check_row; rain: of every word sent */
    uint64_t arg0, arg1, arg2;               /* the three arguments main received */
    uint64_t count;                          /* the jobs done */
    uint32_t serial;                         /* the job (the block's serial) */
    uint32_t word;                           /* the mailbox word */
    uint32_t done;                           /* 1: the job ran; 2: the exit report */
    uint32_t status;                         /* FX_ST_OK or FX_ST_SERIAL */
    uint32_t ticks;                          /* decrementer ticks of this job (the timebase, 79.8 MHz) */
    uint32_t check_row;                      /* the row of sum, FX_NO_CHECK when it is not in the job's rows */
    uint32_t sum2;                           /* sky: the word sum of glass row check_row / 2, when in the job */
    uint32_t quads;                          /* rain: the quads written */
    uint32_t kat;                            /* FX_KAT when the vector helpers gave the known answers at the start */
    uint32_t pad[11];
} __attribute__((aligned(128))) fx_rec;

_Static_assert(sizeof(fx_rec) == 128, "fx_rec is one 128-byte DMA");

/* The arguments of each thread (sysSpuThreadArgument): arg0 is the EA of
 * its report, arg1 the EA of its control block (both 128-byte aligned,
 * below 4 GB), arg2 fx_arg2(index): bits 32..63 are 0 (the job names the
 * mode), the mark, the index. */
static inline uint64_t fx_arg2(uint32_t index)
{
    return (uint64_t)FX_MARK << 16 | (index & 0xffff);
}

/* The quads of a rain job: the drops and the splashes, or the flakes. The
 * app asks gfx_block(GFX_QUADS, 4 * fx_rain_quads(count, kind), 128). */
static inline uint32_t fx_rain_quads(uint32_t count, uint32_t kind)
{
    return count + (kind == FX_DROPS ? FX_SPLASHES : 0);
}

/* Band i of n over rows picture rows, each band's rows even: the rows
 * [*y0, *y1) of a sky or aurora job. */
static inline void fx_band(uint32_t i, uint32_t n, uint32_t rows, uint32_t *y0, uint32_t *y1)
{
    *y0 = rows / 2 * i / n * 2;
    *y1 = i + 1 >= n ? rows : rows / 2 * (i + 1) / n * 2;
}

#ifndef __SPU__
#include <math.h>
/* The rain's horizontal speed in px/s from the wind in km/h and the
 * direction it comes from (degrees, 0 north, 90 east): screen right is
 * east, so a wind from the south-east (135) moves the drops left. The
 * kernel caps it at 70 % of the fall speed. */
static inline float fx_rain_wind(float kmh, float from_deg)
{
    return -kmh * 9.0f * sinf(from_deg * 0.017453292f);
}

/* The program: the stripped SPU ELF, 128-byte aligned (bin2s -a 128), for
 * sysSpuImageImport. */
extern const uint8_t spu_fx_spu[];
extern const uint32_t spu_fx_spu_size;
#endif

#endif
