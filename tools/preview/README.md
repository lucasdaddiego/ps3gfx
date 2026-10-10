# Preview on the host

An app on the frame compiles for the Mac (or Linux) against stub PSL1GHT
headers (`stub/`), and runs in a small simulated world (`stubs.c`): the
library's renderer (`source/gfx/gfx.c`) on a software back end, a scripted
controller, 1/60 s per frame, the `/dev_*` paths inside a sandbox folder, the
network offline and every LV2 syscall refused. It renders chosen frames to
PPM. It is a check of layouts and text, not of the console: the RSX timing,
the real file speeds and the network are not simulated.

Needs: zsh, a C compiler (`cc`), Python 3 (Pillow only for `diff.py` and a
PNG conversion).

```sh
zsh tools/preview/build.zsh APP=<app folder>      # $APP/tools/preview/build/pv; PV_CFLAGS adds cc flags (below)
PV_ROOT=<sandbox> PV_OUT=<folder> PV_SCRIPT="3:SNAP=home 4:EXIT" $APP/tools/preview/build/pv
python3 tools/preview/diff.py A B [D]             # pixel diff of two snapshot folders
```

`build.zsh` compiles `$APP/source/*.c`, `$APP/tools/preview/*.c` (none is
fine), the library's `source/*.c`, the world and `gfx.c`
with its API renamed to `gfx_real_*` (the trace wrapper below). The sandbox
must hold the parents of the app folder (`mkdir -p $sandbox/dev_hdd0/tmp`):
`sysLv2FsMkdir` makes one level, as on the console. `smoke/` is the smallest
app (one screen); CI builds it and takes one snapshot. `kitcheck/` calls the
kit and checks the results against the headers' rules; CI runs it and it
exits 1 on a failed check. An app keeps its own
run script with its scenarios (PS3 Health: `run.zsh`, `shots.zsh`).

## The app's part: pv_app.c

The world refuses every LV2 syscall (`pv_syscall` returns `0x80010003`) and
answers the network offline. Both are weak symbols: an app that needs more
defines the strong ones in its `tools/preview/pv_app.c`. PS3 Health's
`pv_syscall` emulates the drive from a console's sector dumps (syscall 616),
the temperatures (383) and the fan duty (409); an app with a network screen
defines `netSocket`, `netConnect`, `netSend` and `netRecv` for a fake peer.
The weak list is at the end of `stubs.c`: every socket call of PSL1GHT's
`net/net.h` that a TCP or UDP client uses (also `netPoll`, `netSelect`,
`netGetSockOpt`, `netShutdown`, `netErrnoLoc` for `net_errno`), each failing,
and the stub headers have PSL1GHT's constants (`SO_ERROR`, `MSG_DONTWAIT`,
`NET_EWOULDBLOCK` ...), so an app's network code compiles in the preview.

## The raster

`raster.c` draws as the console does, so that the pictures of two renderers
compare pixel for pixel. Each primitive splits into triangles (QUADS into
(0,1,2) and (0,2,3) per quad, a FAN around vertex 0, a STRIP with every second
triangle turned), and each triangle is drawn once, in order. A triangle covers
a pixel whose centre is inside it; a centre on an edge belongs to it only on a
top or a left edge, so triangles that share an edge never cover a pixel twice.
Then the alpha test GEQUAL 16/255, and source-alpha blending in 8-bit math into
an 8-bit XRGB buffer. Model choices, not console facts: positions snap to 1/256
pixel, pixel centres are at +0.5, the interpolation and the texture sampling
are float, the alpha test compares before the 8-bit conversion, and the 8-bit
conversions round to nearest.

## The renderer's back end

`gfx_soft.c` is the preview's back end of `source/gfx` (the console's is
`gfx_rsx.c`), so the preview runs the real `gfx.c`: the batching, the
viewport and the vertex area with its drains. The draws wait in a list until
a drain or the end of the frame, as the RSX reads the vertices only after a
flush; then `raster.c` draws them, and the vertices that they used become NaN.
The vertex area starts on a 128-byte line (`posix_memalign`), as on the
console, so the address of a `gfx_block` is aligned on both.
A draw that reads a vertex from before the last drain draws nothing, and the
snapshot shows it. A build with a small vertex area drains many times in
every frame, and must give the same snapshots:

    PV_CFLAGS='-DGFX_VTX_BYTES=8192' zsh tools/preview/build.zsh APP=<app folder>

`gfx_vram` gets the RSX memory that gfx leaves free on the console: 249 MB (the
`gcmGetConfiguration` size) less what `gfx_rsx.c` takes first, in its order
and alignment: the two colour buffers and the two fragment programs. So an
RSX memory test counts 231 MB at 1080p, 239 at 720p and 244 at 480p.

## gfx traces

`PV_GFXTRACE=file` writes every gfx call of the app, for the end-to-end check
of the trace harness (replay through `gfx.c` and `gfx_rsx.c`). `gfx_trace.c`
wraps the API: `build.zsh` compiles `gfx.c` with it renamed to `gfx_real_*`.
One line per call, integers in hex, floats as hex floats (`%a`, exact):

    video 0x780 0x438                      the output that gfx_init set
    gfx_init = 0
    gfx_vram 0x2aa80 0x80 = 0xfd2200       size, align = the RSX offset (or NULL)
    gfx_viewport 0x1.0cp+6 0x1.3p+5 0x1p+0 0x1p+0
    gfx_begin
    gfx_texture 0xfd2200 0x1a0 0xd2 0x340 0    offset w h pitch linear [format], or NULL
                                           (format only when not 0: 1 is A8R8G8B8)
    gfx_prim 8 4                           type n, then n vertex lines:
    v 0x1.28p+5 0x1.7p+4 0xf2f5faff 0x0p+0 0x1.b6db7p-2
                                           x y rgba u v, as the app wrote them
    gfx_block 8 38400 128                  type n align, then n vertex lines
    gfx_end
    repeat 57                              57 more frames equal to the one before
    end 7402                               the number of frames

A frame is the calls up to and with `gfx_end`. The vertex lines of a
`gfx_prim` or a `gfx_block` go out at the app's next gfx call (then they are
written).
A `gfx_vram` from a job thread (an RSX memory test) can sit between two calls
of a frame. A replay maps the offset of a `gfx_texture` line to the offset
that its own `gfx_vram` gave for the same call (a layout other than the
preview's moves the blocks), and passes one `gfx_tex` object per distinct
texture: gfx compares textures by address. `PV_GFXTRACE_FRAMES=n` ends the
trace after n frames (the app runs on). Run one scenario per trace file:
`gfx_trace.c` opens the file with "w".

## Comparing snapshots

`diff.py` prints one line per snapshot: the count of different pixels and the
largest difference of one channel. With a third folder it writes diff images
(a pixel 1 off in yellow, more in red). A job thread runs in real time, so a
screen that shows a running job can change from run to run: run twice and
compare to see which snapshots repeat.

## Pad scripts and switches

`PV_SCRIPT` is a list of `frame:ACTION`: a button name (`CROSS`, `UP`,
`L2+UP+START`) pressed on that frame, `from-to:BUTTONS` held over a range,
`SPIN` for both sticks turning in circles, `SNAP=name` for a snapshot, `EXIT` to stop.

| Variable | Effect |
|---|---|
| `PV_ROOT`, `PV_OUT` | the sandbox (default `/tmp/pv`), the snapshot folder (default `.`) |
| `PV_RES=720`, `PV_RES=480` | 1280x720 or 720x480 output instead of 1920x1080 |
| `PV_STICKS=lx,ly,rx,ry` | stick offsets from the centre |
| `PV_STAT=1` | print the most vertices and draws in a frame and the drains at exit (the area holds 49,152 vertices) |
| `PV_GFXTRACE=file` | write a gfx trace (above); `PV_GFXTRACE_FRAMES=n` stops it after n frames |
| `PV_MAXFRAMES=n` | stop after n frames (default 20,000, exit status 3) |

An app's `pv_app.c` adds its own (PS3 Health: `PV_DRIVE`, `PV_ST`, `PV_RISE`,
`PV_TEMP_OFF`, `PV_383_RC`, in its README).
