# saspeed - Sapphire CD32X, M3's speed test ROM (docs/09; DraculaX_32X's dxspeed, its docs/17): the translated game
# on the master SH2, the Arcade Card from the state's regions (RT_AC_RO: its RAM read only, in the cart).
#   GEN=<dir> make ROOTDIR=/opt/toolchains/sega   -> dxspeed.32x (the ROM) and cd/LEVEL00.BIN (the blocks)
# GEN: tools/m3_speedtest.py select's output - the generated code (tools/m2_recomp.py) for one window's banks.
# cd/LEVEL01.BIN: tools/m3_speedtest.py pack. Both are game data: build in the work folder, never commit them.
# Then the BootCamp's build.sh (GAME=dxspeed) makes the disc.
ROOTDIR ?= /opt/toolchains/sega
SHROOT  ?= $(ROOTDIR)
MDROOT  ?= $(ROOTDIR)

TARGET  = saspeed
BUILD   = build
RT      = ../tools/m3rt
M2H     = ../tools/m2host
ifeq ($(GEN),)
$(error GEN=<the generated code for the window's banks: tools/m3_speedtest.py select>)
endif

SHPREFIX = $(SHROOT)/sh-elf/bin/sh-elf-
SHCC     = $(SHPREFIX)gcc
SHAS     = $(SHPREFIX)as
SHOBJC   = $(SHPREFIX)objcopy
CC_VER  := $(shell $(SHCC) -dumpversion)

LIBPATH  = -L$(SHROOT)/sh-elf/lib -L$(SHROOT)/sh-elf/lib/gcc/sh-elf/$(CC_VER) -L$(SHROOT)/sh-elf/sh-elf/lib
INCPATH  = -Isrc -I$(GEN) -I$(RT) -I$(M2H) -I$(SHROOT)/sh-elf/include -I$(SHROOT)/sh-elf/sh-elf/include
CCFLAGS  = -D__32X__ -m2 -mb -Os -ffunction-sections -falign-functions=16 -Wall -c -fomit-frame-pointer -std=gnu99 -DHUC_TIMED -DHUC_IDLE_EXIT -DHUC_NODIRTY -DRT_AC_RO
# RT_DIGEST=1: the validation build - each wait's state hash folded into a digest (src/main.c, docs/20)
ifeq ($(RT_DIGEST),1)
CCFLAGS += -DRT_DIGEST
endif
# ACE=1 (docs/10): the Arcade Card is the EverDrive PRO's engine (fpga/ac_engine.sv, Sapphire's mega-pro.rbf): each
# access one read of cart 0x1C0000-0x1FFFFF past the cache; the 14 MB EDCD cart only (ares: ARES_ACE=1)
ifeq ($(ACE),1)
CCFLAGS += -DRT_AC_FPGA
endif
# CARD=1 (docs/39, with ACE=1; the stack with CART=16 M1PAGE=1): the card's 2 MB in cart page 6 (0xC00000), loaded from
# LEVEL03.BIN + IMAGE24.RAW (m3_speedtest.py pack AC_CARD_AT=0xC00000 AC_CARD_N=32), not in the state
ifneq ($(FRAMES),)
CCFLAGS += -DFRAMES=$(FRAMES)
endif
# COMBO=1 (docs/39, GEN = a tools/m3_combo.py c<ii> dir): window 1 = the combination's file (cd/LEVEL00.BIN, at 0x200000) +
# the shared blocks (cd/IMAGE21.RAW, at 0x2A0000: saspeed_combo.blk); the runtime loads the next file when the code changes
comma := ,
ifeq ($(COMBO),1)
CCFLAGS += -DRT_COMBO
endif
# FIFO_LIVE=1 (docs/40, with FIFO=<any file> SPLAY=1): the FIFO a ring at cart 0x200000-0x3EFFFF that the cart's CPU
# writes as the game runs (ares: ARES_HUCHOST, tools/m3_huchost.sh); the pad to it through registers 0xcd / 0xce
# BG32=1 (docs/42, with BG=1): a background with more than 4 palettes (the title) drawn on the 32X layer (rt_bg32.c)
ifeq ($(BG32),1)
CCFLAGS += -DBG32
endif
# PWM_SOUND=1 (docs/44, with FIFO_LIVE=1): the cart CPU's PSG and ADPCM through the 32X's PWM
ifeq ($(PWM_SOUND),1)
CCFLAGS += -DPWM_SOUND
endif
ifeq ($(FIFO_LIVE),1)
CCFLAGS += -DFIFO_LIVE
endif
# FIFO_CDDA=1 (docs/41, with FIFO_LIVE=1): the game's CD audio (FIFO type B) by the stack's command 45 - not working yet
ifeq ($(FIFO_CDDA),1)
CCFLAGS += -DFIFO_CDDA
endif
ifeq ($(CARD),1)
CCFLAGS += -DCARD_PAGE=6
endif
ifeq ($(CARD),2)
CCFLAGS += -DCARD_PAGE=6 -DCARD_NOLOAD                    # (from power-on: the card's page, nothing loaded into it)
endif
# SDRAM_BANKS=0x72,0x6A: those Super RAM banks in SDRAM (src/main.c, docs/38)
ifneq ($(SDRAM_BANKS),)
CCFLAGS += -DSDRAM_BANKS=$(SDRAM_BANKS)
endif
# RTX: the runtime's test defines (docs/37: RTX=-DRT_REBASE=0x100000, the times moved every 2^20 clocks)
CCFLAGS += $(RTX)
# MARKS=1 KEYS=<m3_rip.py pack's .keys> SEED=<m3_rip.py seed's file>: D6b's marks (docs/54) - the hooks, each VRAM
# unit's tag, the key table in the cart; the seed is the CD reads' table for the window's start (the speed test only)
ifeq ($(MARKS),1)
CCFLAGS += -DHUC_MARKS
MARKS_HUC = -DHUC_MARKS
MARKS_OBJ = $(BUILD)/rip_keys.o $(BUILD)/rip_seed.o
endif
# SPR=1 CELLS=<m3_rip.py cells' file> MARKSIN=<rt_pc RUN_MARKS_OUT's file> (with MARKS=1): the sprites drawn into the
# frame buffer at each display start (tools/m3rt/rt_spr.c, docs/56), from the window's marks at its start; PACE=1 with
# it: one picture a 32X refresh, swapped, with its colours (docs/57)
ifeq ($(SPR),1)
CCFLAGS += -DSPR_DRAW $(if $(filter 1,$(PACE)),-DSPR_PACE)
SPR_OBJ = $(BUILD)/rt_spr.o $(BUILD)/spr_rows.o $(BUILD)/rip_cells.o $(BUILD)/rip_marks.o
endif
# BG=1 (with SPR=1 PACE=1): the background on the Genesis' plane A (tools/m3rt/rt_bg2.c, docs/65): each picture's
# list sent with command 79 and its kept runs committed after the swap - the stack built GENPLANE=1 VSPLIT=1 FBVDP=1
# CDR=1: the data track from the CD32X disc while the game runs (docs/69): the drive model from the state's table,
# sectors by the stack's command 91 (build the stack with CDRING=1; the disc's cd/DXTRACK.TRK)
ifeq ($(CDR),1)
CCFLAGS += -DRT_CD_RING
endif
# SEC=1 (with BG=1): the sprites drawn by the secondary SH2 while the master makes the background's list (docs/66)
ifeq ($(SEC),1)
CCFLAGS += -DSPR_SEC
endif
ifeq ($(SEC),2)
CCFLAGS += -DSPR_SEC -DSEC_BG
endif
# SPLIT=1 (with SEC=2): the sprites drawn by both SH2s, the picture in two bands at line SPLIT_Y (docs/67)
ifeq ($(SPLIT),1)
CCFLAGS += -DSPR_SPLIT $(if $(SPLIT_Y),-DSPLIT_Y=$(SPLIT_Y))
endif
# LATE=1 (with SEC=2): the secondary draws the sprites the master listed and sends the picture, while the master runs
# the next frame (docs/17)
# VS_SEC=1 (with SEC=2 VSPLIT=1): command 78 sent by the secondary after the list has run (docs/19)
ifeq ($(VS_SEC),1)
CCFLAGS += -DVS_SEC
endif
# FIFO=<file> (docs/25): the picture build played from a recorded FIFO (fpga/huc, TB_FIFO), LEVEL00.BIN = the FIFO:
# no game code on the SH2s, the PC Engine's frames due at its own rate, the newest drawn at each picture
ifneq ($(FIFO),)
CCFLAGS += -DFIFO_PLAY
endif
# VSSWAP=1 (with VS_SEC=1; the stack built VSSWAP=1, docs/26): command 78 sent by the master before the list - the
# stack switches the V splits at command 79's swap - and nothing after it
ifeq ($(VSSWAP),1)
CCFLAGS += -DVS_SWAP
endif
# SPRPROF=1 (DIAG, docs/32): spr_frame's tests and drawing timed on the master, summed at SDRAM 0x0603E8C0
ifeq ($(SPRPROF),1)
CCFLAGS += -DSPR_PROF
endif
# BG2PROF=1 (DIAG, docs/30): bg2_frame's steps timed on the secondary, summed at SDRAM 0x0603E800
ifeq ($(BG2PROF),1)
CCFLAGS += -DBG2_PROF
endif
# FPGA_TILES=1 (with FPGA_CELLS=1): the background tiles' hash and colour mask from the FPGA too (docs/31)
ifeq ($(FPGA_TILES),1)
CCFLAGS += -DBG2_FPGA_TILES
endif
# FPGA_CELLS=1 (with FIFO=, ACE=1; ares ARES_CELLS): the FPGA's converted cells (docs/28-29) read from the cart
ifeq ($(FPGA_CELLS),1)
CCFLAGS += -DSPR_FPGA_CELLS
endif
# KEEP=1 (with ACE=1, the 14 MB cart): every sprite cell kept converted in the cart, by the engine's poke (docs/19)
ifeq ($(KEEP),1)
CCFLAGS += -DSPR_KEEP
endif
# VLOG=1 (with LATE=1): the game's VRAM writes logged while the secondary makes the background (docs/18)
ifeq ($(VLOG),1)
CCFLAGS += -DVLOG
endif
ifeq ($(LATE),1)
CCFLAGS += -DSPR_SPLIT -DSPR_LATE $(if $(SPR_CMDS),-DSPR_CMDS=$(SPR_CMDS))
endif
ifeq ($(BG),1)
CCFLAGS += -DBG2_DRAW
BG_OBJ = $(BUILD)/rt_bg2.o $(if $(filter 1,$(BG32)),$(BUILD)/rt_bg32.o)
endif
# SPLAY=1 (docs/36): Sapphire played with pad 1 from the state (LEVEL01) on, no end; the code the state's window
# did not trace runs interpreted
# SA_LOG=1 (docs/51; the stack with FBCART=1): the console's log, 4 KB at cart SA_LOG_AT (0x3EF000), read over USB
ifeq ($(SA_LOG),1)
CCFLAGS += -DSA_LOG
endif
ifeq ($(SPLAY),1)
CCFLAGS += -DRT_PLAY -DSAPPHIRE_PLAY
# (docs/46) the picture's number in the left border (white blocks: tools/m3_sprcheck.py's) is off in the played game; PICNUM=1 keeps it
ifeq ($(PICNUM),1)
CCFLAGS += -DPICNUM
endif
endif
# PLAY=1 DX_STAGE=<n> (with PACE=1 BG=1 SEC=2): the game played with the controller, Start's menu (level select,
# lives, credits) and the stage switch (docs/70; src/chain.s); the stage's files are game_files.h's S<n>BLK / S<n>STA
ifeq ($(PLAY),1)
CCFLAGS += -DDX_PLAY -DRT_PLAY $(if $(DX_STAGE),-DDX_STAGE=$(DX_STAGE))
PLAY_OBJ = $(BUILD)/chain.o
endif
# POOL=1 (with CDR=1 BG=1; the stack with FBCART=1): the RAM banks the game writes made writable one at a time in an
# SDRAM slot, written back to the cart by command 101 (docs/72)
ifeq ($(POOL),1)
CCFLAGS += -DRT_POOL
endif
# CDSTEP=1: the System Card's CD_READ byte loop's plain iterations in one go (docs/75: rt_cdstep; the code made with
# m2_recomp.py's hook, the hot code with SPLIT_CDSTEP=1)
ifeq ($(CDSTEP),1)
CCFLAGS += -DHUC_CDSTEP
endif
# CONV=<tiles> [CONV_CELLS=<cells>]: DIAG (docs/45) - that many VRAM tiles and sprite cells converted each frame
ifneq ($(CONV),)
CCFLAGS += -DCONV_TEST=$(CONV) -DCONV_CELLS=$(or $(CONV_CELLS),0)
CONV_OBJ = $(BUILD)/conv.o
endif
# the hot code's leave by hand (src/rt_leave.s, docs/40): RT_LEAVE_ASM=0 for rt.c's rt_leave_fast
ifneq ($(RT_LEAVE_ASM),0)
CCFLAGS += -DRT_LEAVE_ASM
LEAVE_OBJ = $(BUILD)/rt_leave.o
endif
# the translated code: -Os (docs/16), cold paths after the hot ones (docs/19)
HUCOPT  ?= -freorder-blocks -freorder-blocks-algorithm=stc
HUCFLAGS = -D__32X__ -m2 -mb -Os $(HUCOPT) -c -fomit-frame-pointer -std=gnu99 -DHUC_TIMED -DHUC_IDLE_EXIT -DHUC_NODIRTY $(MARKS_HUC)
# CARTCODE="io_model" (docs/71): those objects' code in the cart instead of SDRAM (the room for a RAM bank)
# SDCODE="rt_spr spr_rows rt_bg2" (docs/67): those objects' code in SDRAM (after the runtime's) instead of the cart,
# where a stage's SDRAM has the room (the link says if not): build/saspeed.ld is saspeed.ld with them moved
LDSCRIPT = $(if $(SDCODE)$(CARTCODE)$(filter 1,$(COMBO)),$(BUILD)/saspeed.ld,saspeed.ld)
LDFLAGS  = -T $(LDSCRIPT) -Wl,-Map=$(BUILD)/output.map -nostdlib
ASFLAGS  = --big
LIBS     = $(LIBPATH) -lc -lgcc -lgcc-Os-4-200 -lnosys

BLK  = $(patsubst $(GEN)/%.c,$(BUILD)/%.o,$(wildcard $(GEN)/blk_*.c $(GEN)/bks_*.c))
BLKTAB = $(if $(filter 1,$(COMBO)),$(BUILD)/blocks_sh.o $(BUILD)/blocks_cb.o $(BUILD)/combo_dir.o,$(BUILD)/blocks.o)
# split blocks (tools/m3_speedtest.py split, docs/18-19): the hot halves, one function a stage (into SDRAM:
# saspeed.ld), and their list
HOT  = $(patsubst $(GEN)/%.c,$(BUILD)/%.o,$(wildcard $(GEN)/hot_*.c $(GEN)/hot.c)) \
       $(patsubst $(GEN)/%.s,$(BUILD)/%.o,$(wildcard $(GEN)/hot_*.s))   # (tools/m3_sh2.py: SH2 assembly, docs/20)
# crt0.o must be first: it holds the ROM header, the 32X header and the SH2 start-up
OBJS = $(BUILD)/crt0.o $(BUILD)/main.o $(BUILD)/cdapi.o $(BUILD)/rt.o $(BUILD)/rt_vdc.o $(BUILD)/rt_state.o \
       $(BUILD)/io_model.o $(BUILD)/scsi_model.o $(BUILD)/ac_model.o $(BUILD)/interp.o $(BUILD)/huc_rt.o $(BLKTAB) $(LEAVE_OBJ) $(PLAY_OBJ) $(CONV_OBJ) $(MARKS_OBJ) $(SPR_OBJ) $(BG_OBJ) $(BLK) $(HOT)

all: $(TARGET).32x cd/LEVEL00.BIN $(if $(filter 1,$(COMBO)),cd/IMAGE21.RAW)

$(TARGET).32x: $(TARGET).elf
	$(SHOBJC) -O binary -R .blk -R .blkc -R .blks -R .hot $< $(BUILD)/temp.bin
	dd if=$(BUILD)/temp.bin of=$@ bs=128K conv=sync status=none

cd/LEVEL00.BIN: $(TARGET).elf
	mkdir -p cd
	$(if $(FIFO),cp $(FIFO) $@,$(SHOBJC) -O binary $(if $(filter 1,$(COMBO)),-j .blkc -j .hot,-j .blk) $< $@)

cd/IMAGE21.RAW: $(TARGET).elf
	mkdir -p cd
	$(SHOBJC) -O binary -j .blks $< $@

$(BUILD)/saspeed.ld: saspeed.ld saspeed_combo.blk Makefile
	@mkdir -p $(BUILD)
	sed -e '/) \.text/s/\*hot_\*\.o)/*hot_*.o $(foreach o,$(SDCODE),*$(o).o))/' \
	    -e 's|    /\* SDCODE: .*|    . = ALIGN(16); $(foreach o,$(SDCODE),*$(o).o(.text .text.*))|' \
	    -e 's|    \*(.gnu.linkonce.t.\*)|&$(foreach o,$(CARTCODE), *$(o).o(.text .text.*))|' \
	    $(if $(filter 1,$(COMBO)),-e '/BLK begin/r saspeed_combo.blk' -e '/BLK begin/$(comma)/BLK end/d') saspeed.ld > $@

$(TARGET).elf: src-md/m68k.bin $(OBJS) saspeed.ld $(if $(SDCODE)$(CARTCODE)$(filter 1,$(COMBO)),$(BUILD)/saspeed.ld)
	$(SHCC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

src-md/m68k.bin:
	$(MAKE) -C src-md ROOTDIR=$(MDROOT)

$(BUILD)/crt0.o: src/crt0.s src-md/m68k.bin | $(BUILD)
	$(SHAS) $(ASFLAGS) -I. -o $@ $<

# the Rt's offsets for rt_leave.s: src/rt_off.c's .equ lines (its assembly, the compiler's comment marks taken out)
$(BUILD)/rt_off.inc: src/rt_off.c $(RT)/rt.h $(RT)/rt_vdc.h | $(BUILD)
	$(SHCC) $(CCFLAGS) $(INCPATH) -S -o $(BUILD)/rt_off.s $<
	grep '^[[:space:]]*\.equ' $(BUILD)/rt_off.s | sed 's/#//g' > $@
$(BUILD)/rt_leave.o: src/rt_leave.s $(BUILD)/rt_off.inc | $(BUILD)
	$(SHAS) $(ASFLAGS) -I. -o $@ $<
$(BUILD)/spr_rows.o: src/spr_rows.s | $(BUILD)
	$(SHAS) $(ASFLAGS) -o $@ $<
$(BUILD)/chain.o: src/chain.s | $(BUILD)
	$(SHAS) $(ASFLAGS) -o $@ $<

# (the key table and the seed as data in the cart: binary_rip_keys_start, binary_rip_seed_start)
$(BUILD)/rip_keys.o: $(KEYS) | $(BUILD)
	cp $< $(BUILD)/rip_keys && cd $(BUILD) && $(SHOBJC) -I binary -O elf32-sh -B sh --rename-section .data=.rodata.rip rip_keys rip_keys.o
$(BUILD)/rip_seed.o: $(SEED) | $(BUILD)
	cp $< $(BUILD)/rip_seed && cd $(BUILD) && $(SHOBJC) -I binary -O elf32-sh -B sh --rename-section .data=.rodata.rip rip_seed rip_seed.o
$(BUILD)/rip_cells.o: $(CELLS) | $(BUILD)
	cp $< $(BUILD)/rip_cells && cd $(BUILD) && $(SHOBJC) -I binary -O elf32-sh -B sh --rename-section .data=.rodata.rip --set-section-alignment .rodata.rip=16 rip_cells rip_cells.o
$(BUILD)/rip_marks.o: $(MARKSIN) | $(BUILD)
	cp $< $(BUILD)/rip_marks && cd $(BUILD) && $(SHOBJC) -I binary -O elf32-sh -B sh --rename-section .data=.rodata.rip --set-section-alignment .rodata.rip=16 rip_marks rip_marks.o

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(SHCC) $(CCFLAGS) $(INCPATH) -o $@ $<
$(BUILD)/%.o: $(RT)/%.c | $(BUILD)
	$(SHCC) $(CCFLAGS) $(INCPATH) -o $@ $<
# the drive without its disc files (RT_NO_DISC, tools/m2host/scsi_model.c): no stdio on the SH2
$(BUILD)/%.o: $(M2H)/%.c | $(BUILD)
	$(SHCC) $(CCFLAGS) -DRT_NO_DISC $(INCPATH) -o $@ $<
$(BUILD)/%.o: $(GEN)/%.c | $(BUILD)
	$(SHCC) $(HUCFLAGS) $(INCPATH) -o $@ $<
$(BUILD)/%.o: $(GEN)/%.s | $(BUILD)
	$(SHAS) $(ASFLAGS) -o $@ $<

$(BUILD):
	mkdir -p $@

clean:
	$(MAKE) -C src-md clean ROOTDIR=$(MDROOT)
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).32x cd/LEVEL00.BIN
