# The SPU kit's program: spu-gcc with newlib's startup files and C library,
# no SPU library and no libm (Cell Bench's flags without -lm). The default
# target is the stripped ELF build/spu_kit.spu; "make -C spu update" copies it
# to ../data/spu_kit.spu, the committed copy that the archive embeds
# (spu_kit.spu.o), so make and make install need no spu-gcc. CI builds it
# again and compares the bytes with the committed copy.
ifeq ($(strip $(PS3DEV)),)
$(error "Please set PS3DEV in your environment (sdk_setup.sh)")
endif

SPU_CC    := $(PS3DEV)/spu/bin/spu-gcc
SPU_STRIP := $(PS3DEV)/spu/bin/spu-strip
CFLAGS    := -O2 -Wall -Wextra -Wno-main -ffunction-sections -fdata-sections -I../source
LDFLAGS   := -Wl,--gc-sections

build/spu_kit.spu: build/spu_kit.elf
	$(SPU_STRIP) $< -o $@

# The linker writes the -o path into .note.spu_name and spu-strip keeps it:
# a relative -o gives the same bytes in every checkout.
build/spu_kit.elf: main.c ../source/spu_kit.h Makefile
	@mkdir -p build
	$(SPU_CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c

# a data file like fonts.bin: mode 644, not the executable bit of spu-strip's output
update: build/spu_kit.spu
	cp $< ../data/spu_kit.spu
	chmod 644 ../data/spu_kit.spu

# data/spu_kit.spu stays: it is committed
clean:
	rm -rf build

.PHONY: update clean
