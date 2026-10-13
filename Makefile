#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif
# ppu_rules defaults PS3DEV to /usr/local/ps3dev, and install would write there
ifeq ($(strip $(PS3DEV)),)
$(error "Please set PS3DEV in your environment. export PS3DEV=<path>")
endif

include $(PSL1GHT)/ppu_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the archive, lib/lib$(TARGET).a
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
# DATA holds fonts.bin, latin.bin, digits.bin and icons.bin: bin2o makes each
# a member of the archive (fonts_bin, latin_bin, digits_bin, icons_bin). It
# also holds spu_kit.spu, the SPU kit's program
# (spu/Makefile builds it, the file is committed): the member spu_kit.spu.o,
# from its own rule below
#---------------------------------------------------------------------------------
TARGET		:=	ps3gfx
BUILD		:=	build
# source/gfx is the renderer (its README.md). A base name in source/gfx/ must
# not also exist in source/: VPATH puts all objects in one build/ folder.
SOURCES		:=	source source/gfx
DATA		:=	data
INCLUDES	:=	source source/gfx

#---------------------------------------------------------------------------------
# options for code generation: the flags of PS3 Health 2.0.1, so the code is
# the same as the console-tested one
#---------------------------------------------------------------------------------

CFLAGS		=	-O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/lib/lib$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

export BUILDDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))

export OFILES	:=	$(addsuffix .o,$(BINFILES)) spu_kit.spu.o $(CFILES:.c=.o)

#---------------------------------------------------------------------------------
# build a list of include paths: the library uses nothing from $(PORTLIBS)
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(LIBPSL1GHT_INC) \
					-I$(CURDIR)/$(BUILD)

.PHONY: $(BUILD) clean install

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@[ -d lib ] || mkdir -p lib
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) lib
	@$(MAKE) --no-print-directory -C spu clean

#---------------------------------------------------------------------------------
# install: the archive, the public headers as <ps3gfx/*.h>, OFL.txt (every
# pkg that links the archive ships it as USRDIR/OFL.txt), PHOSPHOR.txt (every
# pkg that links icons.bin ships it as USRDIR/PHOSPHOR.txt) and COMMIT, the full
# commit id of this checkout, -dirty when tracked files changed, unknown
# outside a git repository. An app's build.sh compares COMMIT with its pin.
#---------------------------------------------------------------------------------
install: $(BUILD)
	@mkdir -p $(PORTLIBS)/lib $(PORTLIBS)/include/ps3gfx $(PORTLIBS)/share/ps3gfx
	@cp lib/lib$(TARGET).a $(PORTLIBS)/lib/
	@cp source/gfx/gfx.h source/ui.h source/fs.h source/journal.h source/spu_kit.h $(PORTLIBS)/include/ps3gfx/
	@cp source/digits.h source/icons.h source/icons_ids.h source/fx.h $(PORTLIBS)/include/ps3gfx/
	@cp OFL.txt LICENSES/PHOSPHOR.txt $(PORTLIBS)/share/ps3gfx/
	@rev=$$(git rev-parse HEAD 2>/dev/null) && { git diff --quiet HEAD 2>/dev/null || rev=$$rev-dirty; } || rev=unknown; \
		echo $$rev > $(PORTLIBS)/share/ps3gfx/COMMIT
	@echo "ps3gfx $$(cat $(PORTLIBS)/share/ps3gfx/COMMIT) installed in $(PORTLIBS)"

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main target: base_rules' %.a rule (ppu-ar -rc) with these members
#---------------------------------------------------------------------------------
$(OUTPUT).a:	$(OFILES)

# ui.c includes fonts_bin.h, which the bin2o rule writes next to fonts.bin.o;
# text_utf8.c includes latin_bin.h, digits.c digits_bin.h, icons.c icons_bin.h
ui.o: fonts.bin.o
text_utf8.o: latin.bin.o
digits.o: digits.bin.o
icons.o: icons.bin.o

#---------------------------------------------------------------------------------
# This rule links in binary data with the .bin extension
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# The SPU kit's program: bin2o's first step with a 128-byte alignment (bin2o
# gives 64), so the SPU ELF and its load segments start on a cache line for
# the image import, as Cell Bench's program does. No header: source/spu_kit.h
# declares spu_kit_spu and spu_kit_spu_size. The extension .spu keeps the
# file out of the data/*.bin rule.
#---------------------------------------------------------------------------------
spu_kit.spu.o	:	spu_kit.spu
	@echo $(notdir $<)
	@bin2s -a 128 $< | $(AS) -o $(@)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
