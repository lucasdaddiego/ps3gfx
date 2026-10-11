#!/bin/zsh
# Build an app's host preview, $APP/tools/preview/build/pv: the app's own
# sources and its tools/preview/*.c (pv_app.c: the strong pv_syscall, a fake
# socket), the library's source/*.c, the world in stubs.c, and
# the real source/gfx/gfx.c on the preview's back end (gfx_soft.c), all
# against the stub headers. gfx_rsx.c is never compiled on the host.
#   zsh tools/preview/build.zsh APP=<app folder>      (the folder with source/)
#   PV_CFLAGS='-DGFX_VTX_BYTES=8192' zsh tools/preview/build.zsh APP=...   (extra flags, e.g. a small vertex area)
set -e
L=${0:A:h:h:h}
APP=${${(M)@:#APP=*}#APP=}
[[ -n $APP ]] || { print -u2 "usage: zsh build.zsh APP=<app folder>"; exit 2 }
APP=${APP:A}
[[ -d $APP/source ]] || { print -u2 "no $APP/source"; exit 2 }
O=$APP/tools/preview/build
mkdir -p $O
for bin in fonts latin; do                    # data/*.bin as C arrays, the symbols of bin2o
python3 -I - "$L/data/$bin.bin" "$O/${bin}_bin.c" $bin <<'PY'
import sys
d, n = open(sys.argv[1], 'rb').read(), sys.argv[3]
with open(sys.argv[2], 'w') as f:
    f.write('#include "ppu-types.h"\nconst u8 %s_bin[] __attribute__((aligned(64))) = {\n' % n)
    for i in range(0, len(d), 24):
        f.write(','.join(str(b) for b in d[i:i + 24]) + ',\n')
    f.write('};\nconst u8 %s_bin_end[1];\nconst u32 %s_bin_size = %d;\n' % (n, n, len(d)))
PY
done
CC=(cc -O2 -std=gnu11 -Wall -Wno-unused-variable -Wno-unused-parameter -Wno-incompatible-pointer-types-discards-qualifiers -Wno-macro-redefined
    -I"$L/tools/preview/stub" -I"$L/source" -I"$L/source/gfx" -I"$APP/source" ${=PV_CFLAGS:-})
# gfx.c's API as gfx_real_*: gfx_trace.c wraps it (PV_GFXTRACE)
$CC -Dgfx_init=gfx_real_init -Dgfx_viewport=gfx_real_viewport -Dgfx_begin=gfx_real_begin -Dgfx_texture=gfx_real_texture \
    -Dgfx_prim=gfx_real_prim -Dgfx_block=gfx_real_block -Dgfx_end=gfx_real_end -Dgfx_submit=gfx_real_submit \
    -Dgfx_wait=gfx_real_wait -c "$L/source/gfx/gfx.c" -o "$O/gfx.o"
# (N): an app without a tools/preview/*.c of its own builds too
$CC -o "$O/pv" "$APP"/source/*.c "$APP"/tools/preview/*.c(N) "$L"/source/*.c \
    "$L/tools/preview/stubs.c" "$L/tools/preview/gfx_soft.c" "$L/tools/preview/gfx_trace.c" "$L/tools/preview/raster.c" \
    "$O/fonts_bin.c" "$O/latin_bin.c" "$O/gfx.o" -lm -lpthread
print "built $O/pv"
