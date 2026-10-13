# The effects program: spu-gcc with newlib's startup files and C library, no
# SPU library and no libm (the kernels' sin and exp are polynomials in
# ../../source/spu_fx_kern.h). The default target is the stripped ELF
# build/spu_fx.spu; "make -C spu/fx update" copies it to ../../data/spu_fx.spu,
# the committed copy that the archive embeds (spu_fx.spu.o), so make and make
# install need no spu-gcc. CI builds it again and compares the bytes with the
# committed copy. build/main.su is the stack use of each function
# (-fstack-usage: no change to the code).
ifeq ($(strip $(PS3DEV)),)
$(error "Please set PS3DEV in your environment (sdk_setup.sh)")
endif

SPU_CC    := $(PS3DEV)/spu/bin/spu-gcc
SPU_STRIP := $(PS3DEV)/spu/bin/spu-strip
CFLAGS    := -O2 -Wall -Wextra -Wno-main -ffunction-sections -fdata-sections -fstack-usage -I../../source
LDFLAGS   := -Wl,--gc-sections

build/spu_fx.spu: build/spu_fx.elf
	$(SPU_STRIP) $< -o $@

# The linker writes the -o path into .note.spu_name and spu-strip keeps it:
# a relative -o gives the same bytes in every checkout.
build/spu_fx.elf: build/main.o
	$(SPU_CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

build/main.o: main.c ../../source/spu_fx.h ../../source/spu_fx_kern.h Makefile
	@mkdir -p build
	$(SPU_CC) $(CFLAGS) -c -o $@ main.c

# a data file like fonts.bin: mode 644, not the executable bit of spu-strip's output
update: build/spu_fx.spu
	cp $< ../../data/spu_fx.spu
	chmod 644 ../../data/spu_fx.spu

# data/spu_fx.spu stays: it is committed
clean:
	rm -rf build

.PHONY: update clean
