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
host test come with it.

## The API by header

`<ps3gfx/gfx.h>`, the renderer (`source/gfx/README.md` has the frame model,
the memory layout, the source of each RSX word and the shader recipe):
- `GFX_TRIANGLES`, `GFX_TRIANGLE_STRIP`, `GFX_TRIANGLE_FAN`, `GFX_QUADS`, `GFX_PRIM_MAX`
- `gfx_vtx` (x, y, rgba, u, v: 20 bytes), `gfx_tex` (offset, w, h, pitch, linear, format), `GFX_TEX_A4R4G4B4`
  (0, the default: the font atlases), `GFX_TEX_A8R8G8B8`, `gfx_w`, `gfx_h`
- `gfx_init`, `gfx_vram`, `gfx_tex_new` (a texture in that memory, rows rounded up to 64 bytes), `gfx_viewport`, `gfx_begin`, `gfx_texture`, `gfx_prim`, `gfx_block` (one draw in one
  piece from an aligned index, for a writer such as an SPU's DMA), `gfx_end`, `gfx_exit`, `gfx_put`
- `gfx_last_counts` (the last frame's vertices, draws and drains)
- the back end for gfx.c only: `gfx_be_init`, `gfx_be_begin`, `gfx_be_viewport`, `gfx_be_draw`, `gfx_be_drain`, `gfx_be_end`, `gfx_be_exit`
  (`gfx_rsx.c` on the console, the preview's `gfx_soft.c` on the host)

`<ps3gfx/ui.h>`, the frame:
- the app: `ui_app` (`name`, `version`, `dir`), `ui_init(const ui_app *)`, `ui_end`, `safe_calibrate(int first)`
- `BTN_*`, the colours (`WHITE` to `BG_BOT`, `C_*`), the fonts `F_SMALL`, `F_BODY`, `F_MED`, `F_LARGE`, `F_TITLE`, `F_COUNT`, `TNUM`
- the canvas: `SW`, `SH`, `MG`, `TOP`, `BOTTOM`, `Y_STATUS`, `Y_FOOTER`, `Y_NOTE`
- the state: `quit`, `held`, `pressed`, `pad_last`, `pad_port`, `armed`, `ui_module`, `ui_demo`
- text: `text`, `text_r`, `text_c`, `text_fit`, `text_wrap`, `wrap_step` (text_wrap's line step: the lines it
  drew times it is the height it took), `text_w`, `line_h`, `base_dy`
- the kit: `rect`, `rect_v`, `round_rect`, `round_frame`, `frame`, `line`, `batch_begin`, `batch_rect`, `batch_line`, `batch_end`,
  `ring`, `disc`, `tri`, `poly_fan`, `tex_rect` (a texture drawn into a rect), `card`, `bar`, `heading`, `button_icon`, `spinner`, `background`, `option_card`, `test_card`, `TEST_RESULT_Y`,
  `screen_origin` (the canvas position of the screen's top left corner: a 1920 x 1080 rect from it covers the
  screen, as `background` does)
- a screen: `begin_frame`, `title`, `title_right`, `tabs`, `footer`, `footer_at`, `status_line`, `ui_flip`,
  `ui_stat`, `ui_stats` (the busy, wait and period times of the frames, and gfx's counts)
- the pad: `read_pad`, `gesture`, `ui_leave`
- a job: `job_cancel`, `progress`, `job_set_percent`, `run_job`, `run_job_cancelable` (`progress()` takes a string literal or a static buffer)
- `first_run_prompt`
- the visible area: `safe_l`, `safe_t`, `safe_r`, `safe_b`, `SAFE_DEF_*`, `SAFE_MIN_*`, `safe_valid`, `safe_load`, `safe_set`, `safe_apply`, `safe_override`

`<ps3gfx/journal.h>`, the freeze journal (`journal.txt` in the app folder):
- `J_NONE`, `J_PENDING`, `J_OK`, `J_BAD`, `J_FROZEN`, `J_SKIPPED`, `JOURNAL_NAMES` (64)
- `journal_init` (called by `ui_init` with the app folder), `journal_load`, `journal_clear`, `journal_state`, `journal_skip`,
  `journal_start`, `journal_done`, `journal_needed`

`<ps3gfx/fs.h>`, whole files through the `sysLv2Fs*` syscalls:
- `fs_write_file`, `fs_read_file`, `fs_selftest(dir, &mkdir_rc, &open_rc, &write_rc)`

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
  (the smoke app draws its texture with them).

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
source/gfx`, `DATA := data` (bin2o makes `fonts.bin` a member of the archive
with the symbols `fonts_bin`, `fonts_bin_end` and `fonts_bin_size`; `ui.o`
depends on it for `fonts_bin.h`), and `CFLAGS = -O2 -Wall -mcpu=cell
$(MACHDEP) $(INCLUDE)`, PS3 Health 2.0.1's flags, so the code is the
console-tested one. It errors when `PSL1GHT` or `PS3DEV` is unset (ppu_rules
would default `PS3DEV` to `/usr/local/ps3dev`). The vertex area
(`GFX_VTX_BYTES`, 960 KB) and the ring (`GFX_RING_BYTES`, 60 KB) are baked
into the archive; the preview and the harness compile from source, so their
smaller sizes still work there.

`make install` copies, into `$PS3DEV/portlibs/ppu`:
- `lib/libps3gfx.a`;
- `include/ps3gfx/gfx.h`, `ui.h`, `fs.h`, `journal.h`;
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
holds four forwarders to the checkout's own headers, so the preview builds
against the sources it previews, never against the installed copy.

## Tests

- `test/test_journal.c`: the freeze journal in a temp folder against the stub
  `sys/file.h` and `test/lv2fs_stub.c` (froze at the next load, a skip
  persists, clear, compaction over 16 KB, the 32 KB tail, a done after froze,
  a failed write, the name past the table, a name of 24 characters or more): `cc -std=c99 -Wall -Wextra -I tools/preview/stub -I source test/test_journal.c source/journal.c source/fs.c test/lv2fs_stub.c`.
- `test/test_gfx.c`: gfx's front end (`source/gfx/gfx.c`) on a back end that
  records the draws, with an area of 409 vertices (the batching, the bounds of
  `gfx_prim`, the drains, the blocks, the viewport, the frame): `cc -std=c99 -Wall -Wextra -DGFX_VTX_BYTES=8192 -I tools/preview/stub -I source/gfx test/test_gfx.c source/gfx/gfx.c`.
- `tools/preview/kitcheck/` (CI): an app for the host only that calls the kit
  on the preview and checks the results against the rules of the headers:
  the gfx_block index and address, gfx_tex_new's rows, and ui_stats on the preview's clock
  (16,667 us a frame, all of it in gfx_end). It exits 1 on a failed check.
- `tools/preview/smoke/check_tex8.py` (CI): the smoke app draws a 4 x 4
  A8R8G8B8 texture 64 px a texel, and each texel's colour must fill its block
  in the snapshot (the preview's 8-bit texture path).
- `source/gfx/programs.zsh`: rebuilds the shader programs with the SDK's
  `cgcomp -a` and compares their words with `gfx_rsx.c`.
- `ppu-nm lib/libps3gfx.a` (CI): `gfx_init`, `ui_init`, `journal_start` and
  `fonts_bin` are there, and no undefined symbol is outside PSL1GHT and the
  toolchain's newlib (the `sysLv2Fs*` calls are inline syscalls).
- The RSX-word harness (outside the repository) replays recorded traces
  through `source/gfx/` on a host model of the RSX and compares the words
  with PS3 Health 2.0.1's.
- CI (`.github/workflows/ci.yml`): the host job runs the journal and gfx tests, the kit check, the
  script and Python checks and the smoke preview on gcc with one snapshot and its texture check;
  the ppu job builds and installs the archive with the same `sdk_setup.sh`
  and runs the `ppu-nm` checks.

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
