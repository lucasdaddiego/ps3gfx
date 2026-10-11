# ps3gfx

The renderer, the text and the app frame of [PS3 Health](https://github.com/lucasdaddiego/ps3-health),
as a static library for PSL1GHT homebrew: a 2D renderer of about 500 lines
that writes the RSX command words itself (no librsx), Inter bitmap atlases
drawn one quad per glyph (pixel exact at 1080p), a small drawing kit, the
pad with two-press gestures, a blocking call in a second thread with a
counter on screen, the first-run prompt, the visible area of the TV with its
measuring screen, and the freeze journal. An app links `libps3gfx.a`,
includes `<ps3gfx/ui.h>` and draws its screens inside a 1920 x 1080 canvas.
The code is PS3 Health 2.0.1's, moved here file by file; a host preview and a
host test come with it. The SPU kit, an SPU program and its contract for the
six SPUs of PS3 Health's tests, is here too.

## The API by header

`<ps3gfx/gfx.h>`, the renderer (`source/gfx/README.md` has the frame model,
the memory layout, the source of each RSX word and the shader recipe):
- `GFX_TRIANGLES`, `GFX_TRIANGLE_STRIP`, `GFX_TRIANGLE_FAN`, `GFX_QUADS`, `GFX_PRIM_MAX`
- `gfx_vtx` (x, y, rgba, u, v: 20 bytes), `gfx_tex` (offset, w, h, pitch, linear, format), `GFX_TEX_A4R4G4B4`
  (0, the default: the font atlases), `GFX_TEX_A8R8G8B8`, `gfx_w`, `gfx_h`
- `gfx_init`, `gfx_vram`, `gfx_tex_new` (a texture in that memory, rows rounded up to 64 bytes), `gfx_viewport`, `gfx_begin`, `gfx_texture`, `gfx_prim`, `gfx_block` (one draw in one
  piece from an aligned index, for a writer such as an SPU's DMA), `gfx_end`, `gfx_submit` and `gfx_wait` (`gfx_end` in
  two halves: the app computes while the RSX draws), `gfx_exit`, `gfx_put`
- `gfx_last_counts` (the last frame's vertices, draws and drains), `gfx_front` (the colour buffer on the TV,
  between frames)
- the back end for gfx.c only: `gfx_be_init`, `gfx_be_begin`, `gfx_be_viewport`, `gfx_be_draw`, `gfx_be_drain`, `gfx_be_submit`,
  `gfx_be_wait`, `gfx_be_exit`
  (`gfx_rsx.c` on the console, the preview's `gfx_soft.c` on the host)

`<ps3gfx/ui.h>`, the frame:
- the app: `ui_app` (`name`, `version`, `dir`), `ui_init(const ui_app *)`, `ui_end`, `safe_calibrate(int first)`
- `BTN_*`, the colours (`WHITE` to `BG_BOT`, `C_*`), the fonts `F_SMALL`, `F_BODY`, `F_MED`, `F_LARGE`, `F_TITLE`, `F_COUNT`, `TNUM`
- the canvas: `SW`, `SH`, `MG`, `TOP`, `BOTTOM`, `Y_STATUS`, `Y_FOOTER`, `Y_NOTE`
- the state: `quit`, `held`, `pressed`, `pad_last`, `pad_port`, `armed`, `ui_module`, `ui_demo`
- text: `text`, `text_r`, `text_c`, `text_fit`, `vtext`, `vtext_r`, `vtext_c`, `vtext_fit` (the same with a
  va_list), `text_wrap`, `text_wrap_lines` (the lines text_wrap draws, with no draw), `wrap_step` (text_wrap's
  line step: the lines it drew times it is the height it took), `text_w`, `line_h`, `base_dy`, `text_utf8`
  (UTF-8 text: a Latin atlas per font, `text_utf8.c` with `latin.bin`, linked only by an app that calls it)
- the kit: `rect`, `rect_v`, `round_rect`, `round_frame`, `frame`, `line`, `batch_begin`, `batch_rect`, `batch_line`, `batch_end`,
  `ring`, `disc`, `tri`, `poly_fan`, `tex_rect` (a texture drawn into a rect), `card`, `bar`, `heading`, `button_icon`, `spinner`, `background`, `option_card`, `test_card`, `TEST_RESULT_Y`, `num_field` (a number box of an editor),
  `screen_origin` (the canvas position of the screen's top left corner: a 1920 x 1080 rect from it covers the
  screen, as `background` does)
- a screen: `begin_frame`, `title`, `title_right`, `tabs`, `footer`, `footer_at`, `status_line`, `status_linef`, `ui_flip`,
  `ui_submit` (`ui_flip` without the wait: the next `begin_frame` waits), `ui_wait` (that wait now),
  `ui_stat`, `ui_stats` (the busy, wait and period times of the frames, and gfx's counts), `ui_shot` (the
  TV picture as a PPM, `shot.c`)
- the pad: `read_pad`, `gesture`, `ui_leave`, `pad_rep`, `pad_repeat` (a held button repeats after 350 ms, then every 30 ms)
- a job: `job_cancel`, `progress`, `job_set_percent`, `run_job`, `run_job_cancelable` (`progress()` takes a string literal or a static buffer)
- `first_run_prompt`
- the visible area: `safe_l`, `safe_t`, `safe_r`, `safe_b`, `SAFE_DEF_*`, `SAFE_MIN_*`, `safe_valid`, `safe_load`, `safe_set`, `safe_apply`, `safe_override`

`<ps3gfx/journal.h>`, the freeze journal (`journal.txt` in the app folder):
- `J_NONE`, `J_PENDING`, `J_OK`, `J_BAD`, `J_FROZEN`, `J_SKIPPED`, `JOURNAL_NAMES` (64)
- `journal_init` (called by `ui_init` with the app folder), `journal_load`, `journal_clear`, `journal_state`, `journal_skip`,
  `journal_start`, `journal_done`, `journal_needed`

`<ps3gfx/fs.h>`, whole files through the `sysLv2Fs*` syscalls:
- `fs_write_file`, `fs_read_file`, `fs_selftest(dir, &mkdir_rc, &open_rc, &write_rc)`
- `fs_read_line`, `fs_write_line`, `fs_remove`: one-line settings files and a delete (`fs_line.c`, linked
  only by an app that calls one)

`<ps3gfx/spu_kit.h>`, the SPU kit (the section "The SPU kit"): the contract of the SPU program
`data/spu_kit.spu`, for spu-gcc, ppu-gcc and the host:
- `SPU_KIT_SPUS`, `SPU_KIT_MAGIC`, `SPU_KIT_MARK`, `SPU_KIT_TERM_VALUE`, the modes `SPU_KIT_LS` and
  `SPU_KIT_HEAT`, the exit statuses `SPU_KIT_EXIT_ARGS`, `SPU_KIT_EXIT_MODE`, `SPU_KIT_EXIT_LS`,
  `SPU_KIT_EXIT_ROOM`, `SPU_KIT_HEAT_BATCH`, `SPU_KIT_LS_PASSES`, `SPU_KIT_LS_TOP`, `SPU_KIT_LS_STACK`
- `spu_kit_arg2`, the report `spu_kit_rec`, the local-store kernel `spu_kit_ls_pass` (with
  `spu_kit_ls_march`, `spu_kit_ls_addr` and `spu_kit_ls_got`; the accessors `SPU_KIT_RD` and `SPU_KIT_WR`)
- `spu_kit_spu`, `spu_kit_spu_size`: the program, a member of the archive (PPU and host only)

Every name is PS3 Health's, except the additions of 2026-10-09 for Cell
Bench: the `format` field of `gfx_tex` with A8R8G8B8 (an 8-bit picture
needs no dither), `wrap_step` and `screen_origin`, and a journal table of 64
names (was 32: Cell Bench uses 31). The journal keeps a static table of 64
names: an app passes a name at each call, the table keeps its first 23
characters (two names with the same first 23 share one state), and more
than 64 names share the last slot. `format` is the last field of `gfx_tex`,
so an initializer without it gets A4R4G4B4, and an app built for the first
library builds unchanged. The additions of 2026-10-10:
- `gfx_block`, which replaces Cell Bench's padding quads and its 150 calls
  of `gfx_prim` for the galaxy;
- `ui_stats` and `gfx_last_counts`: the first console numbers of the PPU's
  time per frame, for a test build;
- `gfx_tex_new` and `tex_rect`: a texture and a picture in one call each
  (the smoke app draws its texture with them);
- for editors and an app's own text helpers: `pad_repeat`, `num_field`,
  `text_wrap_lines`, the va_list text calls and `status_linef`;
- `fs_read_line`, `fs_write_line` and `fs_remove`, for a settings file such
  as Couch's LP10 address;
- `gfx_front` and `ui_shot`: a screenshot of the console's picture, to
  compare with the preview's snapshot of the same screen (`diff.py` reads
  PPM);
- `text_utf8`: UTF-8 text for Couch's song titles. U+00A0 to U+00FF and the
  rest of Windows-1252 draw from a second atlas per font (2.6 MB of RSX
  memory, 673 KB in the app's ELF); Latin Extended-A draws its base letters,
  other code points a white square. An app without the call keeps the byte
  text and links neither.

The addition of 2026-10-11: the SPU kit (`<ps3gfx/spu_kit.h>`,
`spu_kit_spu`), for PS3 Health's Cooling load and local-store test.

## The runtime pin

The archive is built with the toolchain PS3 Health runs on: the ps3dev
bundle `nightly-2026-07-26` (ppu-gcc 7.2) with PSL1GHT's 2020 runtime,
commit `6e565a70e927f55813babe86dc6f64149535f908`. Apps linked with
PSL1GHT's 2021+ runtime die before `main` on HFW 4.93 + PS3HEN 3.6.0 (PS3
Health's README has the finding). The library uses nothing from the bundle's
`portlibs/`: `make install` writes into it. `sdk_setup.sh` (PS3 Health's,
unchanged) makes `~/ps3dev` that SDK; an app keeps its own copy of the
script with the pin.

## Build and install

```zsh
./sdk_setup.sh                 # once: ~/ps3dev = the bundle + PSL1GHT 6e565a7 (about 20 s with the bundle cached)
export PS3DEV=$HOME/ps3dev PSL1GHT=$HOME/ps3dev; export PATH=$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin
make                           # lib/libps3gfx.a
make install                   # into $PS3DEV/portlibs/ppu
make clean
```

The Makefile is PSL1GHT's app Makefile cut to an archive: `SOURCES := source
source/gfx`, `DATA := data` (bin2o makes `fonts.bin` and `latin.bin` members
of the archive with the symbols `fonts_bin`, `fonts_bin_end`, `fonts_bin_size`
and the same for `latin_bin`; `ui.o` and `text_utf8.o` depend on them for
their headers; its own rule makes `spu_kit.spu` the member `spu_kit.spu.o`
with `bin2s -a 128`, the SPU kit), and `CFLAGS = -O2 -Wall -mcpu=cell
$(MACHDEP) $(INCLUDE)`, PS3 Health 2.0.1's flags, so the code is the
console-tested one. It errors when `PSL1GHT` or `PS3DEV` is unset (ppu_rules
would default `PS3DEV` to `/usr/local/ps3dev`). The vertex area
(`GFX_VTX_BYTES`, 960 KB) and the ring (`GFX_RING_BYTES`, 60 KB) are baked
into the archive; the preview and the harness compile from source, so their
smaller sizes still work there.

`make install` copies, into `$PS3DEV/portlibs/ppu`:
- `lib/libps3gfx.a`;
- `include/ps3gfx/gfx.h`, `ui.h`, `fs.h`, `journal.h`, `spu_kit.h`;
- `share/ps3gfx/OFL.txt`;
- `share/ps3gfx/COMMIT`: the full commit id of the checkout, `-dirty` when
  tracked files changed, `unknown` outside a git repository. An app's
  `build.sh` compares it with the commit its `sdk_setup.sh` pins: one
  `~/ps3dev` serves every app, and the last install wins.

An app pins a commit, never a tag. A tag marks a console round of an app on
the library: `v0.1.0` is 7afb1c6, PS3 Health 2.0.2's round of 2026-10-09, and `v0.2.0` is
e4efbd9, Cell Bench 0.3.0 rc5's round of 2026-10-10. The history of `main` is
never rewritten after a pin: a clone and a checkout of the pinned id must
keep working.

## An app on the library

Its Makefile (PSL1GHT's, as PS3 Health's):

```make
LIBDIRS := $(PSL1GHT)/portlibs/ppu
LIBS    := -lps3gfx -lgcm_sys -lio -lm -lsysutil     # -lps3gfx first: GNU ld resolves left to right
```

(`-lnet -lnetctl -lsysmodule` when it has network screens.) `LIBDIRS` gives
`-I$(PSL1GHT)/portlibs/ppu/include` and `-L.../lib`, so the sources include
`<ps3gfx/ui.h>`, `<ps3gfx/gfx.h>` (for `gfx_vram`), `<ps3gfx/journal.h>` and
`<ps3gfx/fs.h>`. No font line: GNU ld pulls `fonts.bin.o` from the archive
because `ui.o` references `fonts_bin`.

Its `main`:

```c
ui_init(&(const ui_app){"PS3 Health", APP_VERSION, APP_DIR});   /* first: the title bar, the folder, the journal */
fs_ok = fs_selftest(APP_DIR, &mk, &op, &wr) == 0;                /* mkdir, "start" appended to starts.txt */
if (!safe_load() && fs_ok) safe_calibrate(1);                    /* the first start: the visible area of this TV */
atexit(leave);                                                   /* leave() calls ui_end() */
```

then its screens: `read_pad`, `begin_frame`, `title`, the kit, `footer`,
`ui_flip`. `ui_leave()` after `read_pad()` says exit (START or Quit Game),
home (CIRCLE) or stay. A journaled call is `journal_start(name)`, the call,
`journal_done(name, rc, ok)`; a call the console never ran goes behind
`first_run_prompt`, and `journal_skip` remembers a decline. The library
writes four files in the app folder: `starts.txt`, `journal.txt`,
`safe_area.txt`, and `gfx_init.txt` only when the picture cannot start (the
number of the failed init step, `source/gfx/README.md`). A call of the
journal or the visible area before `ui_init` has no folder: call `ui_init`
first.

Two conventions of the kit are in its texts: SQUARE twice on the home screen
clears the journal (`gesture(BTN_SQUARE)`, then `journal_clear()`), and
"Hold the power button, then start the app again: it skips the frozen step."
An app adopts both. Every pkg that links the archive ships `OFL.txt` as
`USRDIR/OFL.txt` (the fonts are a modified Inter: NOTICE item 2). The kit's
globals have short names (`text`, `line`, `title`, `quit`, `SW`, `SH`): an
app with a global of the same name gets a duplicate-symbol link error, never
a silent bug.

## The SPU kit

`spu/main.c` is an SPU program for the six SPUs of PS3 Health's tests, and
`<ps3gfx/spu_kit.h>` is its contract. The archive embeds the stripped
program, `data/spu_kit.spu` (committed), as the member `spu_kit.spu.o` with
`bin2s -a 128`: the ELF starts on a 128-byte line, as Cell Bench's program
does. Its symbols are `spu_kit_spu`, `spu_kit_spu_end` and
`spu_kit_spu_size`.

The library makes no LV2 call. The app makes each call and journals it:
`sysSpuInitialize`, `sysSpuImageImport` of `spu_kit_spu`, the group of six
threads, the start, the terminate or the join, the exit statuses, the
destroy and `sysSpuImageClose`. An app that references no kit symbol links
neither the member nor an SPU syscall. Cell Bench keeps its own program,
with the modes 1 to 4 (echo, spin, galaxy, Mandelbrot). The kit has the
modes 5 and 6 and the same mark, 0xce11: one family of programs.

The arguments of each thread (`sysSpuThreadArgument`):
- arg0: the EA of the thread's report, 128-byte aligned, below 4 GB
- arg1: 0
- arg2: `spu_kit_arg2(mode, index)`, that is mode << 32 | 0xce11 << 16 | index
- arg3: not used (LV2 puts arg0 to arg3 into r3 to r6, and newlib's crt1
  gives `main` only r3 to r5)

The report, `spu_kit_rec`, is 128 bytes: one DMA to one cache line. Its
first 48 bytes are Cell Bench's `cell_rec`: `magic` (`SPU_KIT_MAGIC` once
the thread wrote it), `index`, `mode`, `sum`, the three arguments and
`count`. The local-store test's fields follow: `done`, `lo`, `hi`, `errors`,
`err_addr`, `err_want`, `err_got` and `ticks`. Each thread puts one report
at its start, and the app waits for six. The app reads the reports with
volatile reads.

The modes:
- `SPU_KIT_HEAT` (6), the Cooling load: eight independent 4-lane
  multiply-add chains, x = x * x + c. c is -1.984375 (-127/64), exact in
  float: spu-gcc rounds a constant toward zero, a host compiler to the
  nearest, and both give these bits. On [c, c * c + c] = [-1.984375,
  1.953369140625], x * x is at most c * c, so each value stays in that
  interval and no value runs off. No chain settles: in a host simulation of
  the SPU's arithmetic (fused, truncated toward zero), each of the 64
  possible start values, (8 * (index & 7) + k + 1) / 64 for chain k, enters
  a cycle of 361, 781 or 9,747 values. The eight values of a thread stay
  different, so about 13 of the 32 bits change from one fma to the next. A
  loop is 8 fma, an add and a branch, so the float pipeline works 8 cycles
  in 9 (an estimate: about 355 million loops a second per SPU). A report
  every 2^20 loops (`SPU_KIT_HEAT_BATCH`) has `count`, the loops so far, and
  `sum`, a fold of the chains. The mode never exits. The app ends the group
  with `sysSpuThreadGroupTerminate(group, SPU_KIT_TERM_VALUE)`, and the join
  gives the cause 4 (`SPU_THREAD_GROUP_JOIN_TERMINATED`) and the status
  0x5ed. The app watches `count`: a count that stops moving is a stopped
  SPU.
- `SPU_KIT_LS` (5), the local-store test: the area [lo, hi) of the thread's
  own 256 KB local store. lo is the end of the image rounded up to 128, and
  hi is 0x3c000: the top 16 KB stay for the stack. In this build the area is
  0x1980 to 0x3c000, 239,232 bytes (91 % of the local store). The test makes
  16 passes (`SPU_KIT_LS_PASSES`). A pass is March C- (up w0; up r0 w1; up
  r1 w0; down r0 w1; down r1 w0; up r0) on the background 0x00000000, then
  on 0x55555555, then a write and a read of each word's own address XOR a
  constant of the pass. `errors` counts the wrong reads and saturates. The
  first wrong read goes into `err_addr` (its local-store address),
  `err_want` and `err_got`. A report follows each pass (`count`: the passes
  done), then a last one with `ticks` (decrementer ticks: the timebase, 79.8
  MHz) and `done` = 1. The time is an estimate: about 21 million word steps
  per SPU, 0.05 to 0.2 s. The kernel, `spu_kit_ls_pass`, is plain C in the
  header: an app's preview twin runs it over a host buffer with addr0 = lo
  and reports the same addresses.

The exit statuses (`sysSpuThreadGetExitStatus`, after the join):

| Status | When |
|---|---|
| 0x200, `SPU_KIT_EXIT_ARGS` | the mark or arg0 is wrong: no DMA |
| 0x300, `SPU_KIT_EXIT_MODE` | an unknown mode: no DMA |
| 0x500 \| index, `SPU_KIT_EXIT_LS` | the local-store test ran: the result is in the report |
| 0x600 \| index, `SPU_KIT_EXIT_ROOM` | the area is under 4 KB or the stack reaches it: the report has `done` = 1, `lo` and `hi`, and the area was not written |

The thread exit is Cell Bench's: the status into the outbound mailbox, then
stop 0x102. newlib's `exit` and `_exit` (a stop 0x2000 with the status,
libspe2's code) are linked and never reached.

`make -C spu` makes `spu/build/spu_kit.elf` and the stripped
`spu/build/spu_kit.spu` with the bundle's `spu-gcc` and `spu-strip`
(`$(PS3DEV)/spu/bin`, Cell Bench's flags without `-lm`: newlib's startup
files and C library, no SPU library, no heap, no printf).
`make -C spu update` copies the program to `data/spu_kit.spu`, the
committed copy: `make`, `make install` and an app's `sdk_setup.sh` need no
`spu-gcc`. The linker
writes the `-o` path into `.note.spu_name`, and `spu-strip` keeps it: the
relative `-o build/spu_kit.elf` gives the same bytes in every checkout. The
extension `.spu` keeps the file out of the `data/*.bin` rule, which aligns
to 64 bytes. CI builds the program again and compares the bytes with
`data/spu_kit.spu`. It also checks that no malloc, sbrk or printf symbol is
linked (a heap would start at `_end`, inside the area) and that `_end` is
at most 0x8000.

This build (spu-gcc 7.2.0): text 5,240 bytes, data 800, bss 272, `_end`
0x1910, `data/spu_kit.spu` 7,048 bytes. Stack frames (`-fstack-usage`):
heat 320 bytes, ls 496, main 80. No console has run the kit yet: PS3 Health
2.1.0 rc1 is its first round.

## The preview on the host

`tools/preview/` compiles an app on the frame for the Mac or Linux against
stub PSL1GHT headers, with the real `gfx.c` on a software back end, a
scripted controller and a sandbox for the `/dev_*` paths, and renders chosen
frames to PPM:

```zsh
zsh tools/preview/build.zsh APP=<app folder>     # the app's source/ and tools/preview/*.c, into $APP/tools/preview/build/pv
```

The world refuses every LV2 syscall and answers the network offline, both as
weak symbols: an app's `tools/preview/pv_app.c` defines the strong
`pv_syscall` (PS3 Health: the drive from a console's sector dumps, 383 and
409) or a fake socket. `tools/preview/README.md` has the raster, the back
end, the gfx traces (`PV_GFXTRACE`), `diff.py` and the pad scripts.
`tools/preview/smoke/` is the smallest app, for CI. On the host an app
includes `<ps3gfx/ui.h>` as on the console: `tools/preview/stub/ps3gfx/`
holds five forwarders to the checkout's own headers, so the preview builds
against the sources it previews, never against the installed copy.

## Tests

- `test/test_journal.c`: the freeze journal in a temp folder against the stub
  `sys/file.h` and `test/lv2fs_stub.c` (froze at the next load, a skip
  persists, clear, compaction over 16 KB, the 32 KB tail, a done after froze,
  a failed write, the name past the table, a name of 24 characters or more): `cc -std=c99 -Wall -Wextra -I tools/preview/stub -I source test/test_journal.c source/journal.c source/fs.c test/lv2fs_stub.c`.
- `test/test_fs.c`: the one-line files of `fs_line.c` in a temp folder
  (a missing file, CR LF, no line end, an empty file, the edges, a cut at n,
  the 254-character limit, a write that replaces, a remove):
  `cc -std=c99 -Wall -Wextra -I tools/preview/stub -I source test/test_fs.c source/fs_line.c source/fs.c test/lv2fs_stub.c`.
- `test/test_gfx.c`: gfx's front end (`source/gfx/gfx.c`) on a back end that
  records the draws, with an area of 409 vertices (the batching, the bounds of
  `gfx_prim`, the drains, the blocks, the viewport, the frame, the submit and the wait): `cc -std=c99 -Wall -Wextra -DGFX_VTX_BYTES=8192 -I tools/preview/stub -I source/gfx test/test_gfx.c source/gfx/gfx.c`.
- `test/test_spu_kit.c`: the SPU kit's contract (the report's size, alignment
  and layout, the arg2 packing) and its local-store kernel over a buffer of
  the SPU's area size, 16 passes: a clean area gives no error, and injected
  faults (stuck-at 0 and 1, a transition, three couplings, an alias, a
  bridge, a pattern fault) are each found at their first address with the
  word written and the word read. Four of them need one part of a pass each:
  the down marches, the last up read, the background 0x55555555, the address
  pass. Also the saturating count and no write outside the area:
  `cc -std=c99 -Wall -Wextra -I source test/test_spu_kit.c`.
- `tools/preview/kitcheck/` (CI): an app for the host only that calls the kit
  on the preview and checks the results against the rules of the headers:
  text_wrap_lines against text_wrap (5,250 cases), the va_list calls against the variadic ones (also past the
  319-byte cut), the preview's offline socket calls, gfx_tex_new's rows, num_field's width, the gfx_block index and address, pad_repeat's frames
  with a held DOWN, ui_shot against the preview's own snapshot of the frame (CI compares the two files),
  ui_stats on the preview's clock, and after text_utf8: ASCII measures and draws as before, the fallbacks,
  the cut sequences and the text_fit and text_wrap breaks on whole code points
  (16,667 us a frame, all of it in gfx_end; with ui_submit all of it busy, counted in the next begin_frame).
  It exits 1 on a failed check.
- `tools/preview/smoke/check_tex8.py` (CI): the smoke app draws a 4 x 4
  A8R8G8B8 texture 64 px a texel, and each texel's colour must fill its block
  in the snapshot (the preview's 8-bit texture path).
- `source/gfx/programs.zsh`: rebuilds the shader programs with the SDK's
  `cgcomp -a` and compares their words with `gfx_rsx.c`.
- `ppu-nm lib/libps3gfx.a` (CI): `gfx_init`, `ui_init`, `journal_start`,
  `fonts_bin` and `spu_kit_spu` are there, the kit's member is 128-byte
  aligned (`ppu-objdump -h`), and no undefined symbol is outside PSL1GHT and
  the toolchain's newlib (the `sysLv2Fs*` calls are inline syscalls).
- The SPU kit's program (CI): the bundle's `spu-gcc` builds it again and the
  bytes must be `data/spu_kit.spu`; `spu-nm` finds no malloc, sbrk or printf
  symbol and `_end` at most 0x8000.
- The RSX-word harness (outside the repository) replays recorded traces
  through `source/gfx/` on a host model of the RSX and compares the words
  with PS3 Health 2.0.1's.
- CI (`.github/workflows/ci.yml`): the host job runs the journal, file, gfx and SPU kit tests, the kit check, the
  script and Python checks and the smoke preview on gcc with one snapshot and its texture check;
  the ppu job builds and installs the archive with the same `sdk_setup.sh`,
  builds the SPU kit's program again and compares it with the committed one,
  and runs the `spu-nm` and `ppu-nm` checks.

## Speed on the console

Cell Bench's kit check (`auto.txt` "k" in ps3tools_private's `cell-bench`) measures the kit on
the console: the main thread's busy time per frame (`ui_stats`, 300 frames a screen) on three
fixed screens, and a screenshot (`ui_shot`) of the first one, whose bytes must not change.
Rounds 8 to 18 of 2026-10-10:

| Screen | e63447f | circle tables | and glyph table |
|---|---|---|---|
| light: five fonts, the kit's shapes, a footer, a texture | 1.087 ms | 0.251 ms | 0.196 ms |
| text: 40 more formatted lines | 1.647 ms | 0.826 ms | 0.522 ms |
| shapes: 40 discs, 40 rings, 20 rounded rectangles | 3.988 ms | 0.378 ms | 0.331 ms |

A cosf or sinf call took about 0.37 us on the PPU, so the shapes now read their points from
tables that `ui_init` makes once, and `run()` reads each glyph's numbers from a table (no
int-to-float conversion, a store and a load on the PPU, per glyph). Every round kept the
screenshot's bytes. `ui_init` takes 0.4 to 0.7 s, nearly all of it in the firmware's
`gcmInitBody` and `videoConfigure`; the font upload is 3.4 ms (6.9 ms in 16-bit stores; 64-bit
and 16-byte stores to RSX memory were slower than 32-bit ones). The kit check also compared its
screenshot with the preview's: the text, the shapes and the texture are pixel-identical at
1080p; the background gradient differs by 1 level on some rows, and 6 pixels at the ends of
one thin diagonal line differ.

## The console it ran on

This code is PS3 Health 2.0.1's renderer and frame: a PS3 Super Slim
(CECH-4xxx), HFW 4.93 + PS3HEN 3.6.0, the six modules at 1080p (the drive,
the speed test, the temperatures and the fan duty under load, the
controller, the display and the visible area, internet and LAN, the memory
tests with XDR 194 MB and RSX 231 MB), Help with the QR code, Quit Game and
a new start. The archive went to the console with PS3 Health 2.0.2 on
7afb1c6 (tag `v0.1.0`, 2026-10-09). The additions of e4efbd9 (tag `v0.2.0`) went with Cell
Bench 0.3.0 rc5 (round 8, 2026-10-10): six SPUs write a 960 x 540
A8R8G8B8 picture into two `gfx_vram` buffers by DMA, the frame draws it over
the whole screen from `screen_origin` with bilinear filtering, 44 to 60
pictures a second, and Lucas's photo of the TV shows the colours in their
places; Cell Bench's 31 journal names fit the 64-name table.

## The VPATH rule

PSL1GHT's VPATH puts every object of every `SOURCES` folder into one
`build/`, so a base name may not repeat across `source/` and `source/gfx/`.
An app's object names cannot collide with the archive's members: the archive
is one file.

## Credits

- [PSL1GHT](https://github.com/ps3dev/PSL1GHT) and the
  [ps3dev](https://github.com/ps3dev/ps3dev) toolchain (MIT): the headers,
  and the RSX method names of `rsx/nv40.h`.
- RPCS3, for how libgcm and the RSX read the renderer's words.
- [GamePad Test](https://github.com/ErikPshat/GamePad-Test) by Zar, the model for
  the first UI of PS3 Health.
- The on-screen text uses [Inter](https://github.com/rsms/inter)
  (SIL Open Font License): bitmap atlases made by `art/make_font.py`.

## License

MIT, see [LICENSE](LICENSE). Third-party notices: [NOTICE](NOTICE). The font
license every pkg ships: [OFL.txt](OFL.txt).
