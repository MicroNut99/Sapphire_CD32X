# satest - the Sapphire CD32X M1 test ROM (DraculaX_32X's dxtest) (from hello) that boots from CD on the CD32X (CD32X BootCamp)
#   make ROOTDIR=/opt/toolchains/sega        -> hello.32x (a normal 32X ROM)
# then tools/prep_rom.py hello.32x GAME.32X  (disc/build.sh does both)
ROOTDIR ?= /opt/toolchains/sega
SHROOT  ?= $(ROOTDIR)
MDROOT  ?= $(ROOTDIR)

TARGET  = satest
BUILD   = build

SHPREFIX = $(SHROOT)/sh-elf/bin/sh-elf-
SHCC     = $(SHPREFIX)gcc
SHAS     = $(SHPREFIX)as
SHOBJC   = $(SHPREFIX)objcopy
CC_VER  := $(shell $(SHCC) -dumpversion)

LIBPATH  = -L$(SHROOT)/sh-elf/lib -L$(SHROOT)/sh-elf/lib/gcc/sh-elf/$(CC_VER) -L$(SHROOT)/sh-elf/sh-elf/lib
INCPATH  = -Isrc -I$(SHROOT)/sh-elf/include -I$(SHROOT)/sh-elf/sh-elf/include
CCFLAGS  = -D__32X__ -m2 -mb -O2 -Wall -c -fomit-frame-pointer -std=gnu99
# DIAG=1: the diagnostic disc (build.sh DIAG=1): hello starts on the Sub-CPU's green text
DIAG    ?= 0
CCFLAGS += -DHELLO_DIAG_TEXT=$(DIAG) $(EXTRA)
LDFLAGS  = -T $(SHROOT)/ldscripts/mars.ld -Wl,-Map=$(BUILD)/output.map -nostdlib
ASFLAGS  = --big
LIBS     = $(LIBPATH) -lc -lgcc -lgcc-Os-4-200 -lnosys

# crt0.o must be first: it holds the ROM header, the 32X header and the SH2 start-up
OBJS = $(BUILD)/crt0.o $(BUILD)/main.o $(BUILD)/cdapi.o

all: $(TARGET).32x

$(TARGET).32x: $(TARGET).elf
	$(SHOBJC) -O binary $< $(BUILD)/temp.bin
	dd if=$(BUILD)/temp.bin of=$@ bs=128K conv=sync status=none

$(TARGET).elf: src-md/m68k.bin $(OBJS)
	$(SHCC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

# the 68000 side of the cartridge (never run on the CD32X: the 68000 runs the CD listener there)
src-md/m68k.bin:
	$(MAKE) -C src-md ROOTDIR=$(MDROOT)

$(BUILD)/crt0.o: src/crt0.s src-md/m68k.bin | $(BUILD)
	$(SHAS) $(ASFLAGS) -I. -o $@ $<

$(BUILD)/%.o: src/%.c src/32x.h src/cdapi.h | $(BUILD)
	$(SHCC) $(CCFLAGS) $(INCPATH) -o $@ $<

$(BUILD):
	mkdir -p $@

clean:
	$(MAKE) -C src-md clean ROOTDIR=$(MDROOT)
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).32x
