# Pick-up notes for the next Claude session (7 Nov 2026, after docs/08)

Read `CLAUDE.md` first.

## 1. Where it stands

- **M0 done** (docs/02). Mednafen boots the disc with the Arcade Card, deterministic from power-on; the attract
  demo is the recording source (stages 1-4); `$0E15` = stage index (4 = stage 5).
- **Open, in this order:**
  1. **D1 decided: both carts** (docs/03). `tools/make_packs.py` lays out the Arcade Card for each; every later
     build makes both (the 14 MB one `CART=ED`, pages switched; the 4 MB one loading `STGn.PAK`).
  2. **M0b done** (docs/04). Left from it: stage 5 recorded with real inputs to its end, and where LBA 8244-8819 /
     8956-11003 go (the later scenes, the ending). The reference recordings are `inp/demo.npz` and `inp/st5.npz`.
  3. **M1 done** (docs/05). D3: the HUD as Genesis sprites, built by the Sub-CPU in Word RAM in the port (a
     stack command at M3). The VSPLIT / MODE1 fix: CD32X-BootCamp PR #1, merged as `1eaff00` (clone at
     `/home/claude/cd32x-bootcamp`); ledger §21b written (MicroNut99/docs `009a13a`, clone `/home/claude/docs`).
  4. **M2 done** (docs/06-07): the translation, our clock, and the host alone: the whole demo from power-on equal
     to Mednafen every frame (`work/tr/<window>`, `work/m2check`, `work/m2gen`, `work/m2`). 
  5. **M3 step 1 done** (docs/08): `rt_pc` (`work/rtobj`, C from `work/m3gen`, state `work/sp/st3.bin`).
  6. **M3 step 2 done** (docs/09): `saspeed/` in ares equal to `rt_pc` (hash `66AC699E`). Build tree `work/bc3`
     (`cd32x/apply.sh`), state `work/sp/st3r.bin` (`AC_REGIONS`, 13 regions), code sets `work/sel_st3c` (all C) and
     `work/sel_h200` (hot asm, `SPLIT_MIN=200`), `SDRAM_BANKS=0x68,0x69,0x6B,0x74,0x75,0x85`. Check a bank set on the
     PC first with `RUN_RW=` (rt_pc rebuilt with `-I work/m3gen`). 3.95 refreshes a frame. **Next:** room in SDRAM
     for more hot code (docs/09 §4; DraculaX's pool needs a stack change, so not without the owner), then
     DraculaX's speed rounds (its docs/19 on).
  7. **The EverDrive's FPGA** (docs/10): `fpga/` (the engine, krikzz's mapper with it, the Quartus kit, sims, the
     ares patch). ares with the engine: `/home/claude/ares_android` branch `sapphire-ac-engine` (local, not pushed;
     `fpga/ares/*.patch`), `ARES_ACE=1`. **Waiting on the owner:** `Sapphire\fpga\quartus\build_sapphire.bat`
     (fit / timing / `mega-pro.rbf` into `fpga\quartus\out`). Carts: 14 MB EverDrive PRO + core (`EDCD=1 ACE=1`),
     4 MB plain EverDrive (`EDCD=1`), 4 MB RAM cart; bank 31 never. Then: the HuC6280 core on the cart (step 2).
  8. **docs/11:** the poke (engine registers `$1AC2-$1AC5`), the hot set from `work/tr/st3_600` (the whole 600
     frames). Best: 14 MB + core `SDRAM_BANKS=0x69,0x85 ACE=1` with `work/sel_f300` (1,062 refreshes); 4 MB
     6 banks with `work/sel_f700` (1,846). `/tmp/runf.sh` was the run script (in docs/11's how-to).
     **Waiting on the owner:** the console runs of `Sapphire\console_test\ace` (14 MB + core) and `plain\`
     (4 MB plain EverDrive): a photo of each end screen. Quartus done (docs/10 §2b: 3,677 LEs, timing met).
  9. **docs/12:** every stage: `work/sp/st{1,2,4,5}.bin`, `work/tr/st*_600`, `work/sel_st*`, `work/stage_run.sh`
     (= `tools/m3_stagerun.sh`). Next: the 14 MB build's profile, the VRAM in the FPGA, the console results.
  10. **docs/13:** `work/m2/patch_alts.json`, the translation `work/m3gen2` (rt_pc `work/rtobj2`); `sel_st*` reselected
     from it. Next: the alternatives in the hot asm, the VRAM port (17 %), rd/wr_slow, the console results.
  11. **docs/14:** `rt_tia_card_vram` (rt.c) from `block_move` (m2_recomp RUNTIME_C). After a regeneration copy
     `m3gen2/huc_rt.c` into every `sel_st*` folder (select / split keep the old one).
  12. **docs/15:** m3_sh2 `SPLIT_DYN_LOADS`, PATCH_ALTS inline; `ac_stream`; `work/prof_run.sh` (ares profile by part:
     `PSTART`/`PEND` frames inside the test, which starts near frame 3300 now). Next: VRAM in the cart on the 14 MB
     (docs/15 §4), measured first.
     **Waiting on the owner:** `Sapphire\console_test` now holds docs/15's stage 3 builds (ares: core 552
     refreshes, plain 1,140, HASH `66AC699E`); a photo of each end screen before more is built on the engine.
  13. **docs/16, the picture:** `work/rtbg` (rt_pc with marks + bg2), `work/rip/` (the empty rip files),
     `work/pic_run.sh` / `pic_check.sh` (= tools/m3_picrun.sh, m3_piccheck_ares.sh), `work/pic_<st>_all.bin` (every PC
     picture), the split `work/sel_st3_600m` (SPLIT_MARKS=1). Build: docs/16's how-to with `SEC=2`. The owner said
     not to burn a CD until the game is ready. Next: speed, colours (stages 1-2), sprite line limit, then play.
  14. **docs/17, speed:** SPLIT / LATE / command-78 shadowing tried, none faster: the background's job reads the live
     VDC model, so the master waits for it. Next: a copy for that job (map window, colours, tags; the tiles are the
     hard part, SDRAM has no room for VRAM), then `LATE=1 SPR_CMDS=64` pays.
  15. **docs/18:** the owner chose the FPGA's way (the 4 MB cart may stay slower). Measured: VRAM traffic small; the
     write log (`VLOG=1`) exact on the PC but slower in ares; the sprites are half the master (convert 21 %, the cache's
     search 19 %), 83 % of conversions being cells the 32-slot cache lost. Next: the test build on docs/03's 14 MB
     layout, then all 512 converted cells kept in the cart (engine pokes), found by number.
  16. **docs/19:** `KEEP=1` (cells at cart 0x170000) and `VS_SEC=1` (list sent by pace_vbi at the blank; command 78 by
     the secondary after it). Best build: `SEC=2 KEEP=1 VS_SEC=1`, about 24 game fps, screenshots exact but for
     line 203 now and then. Next: spr_clear, drawing on the secondary, the waits.
  17. **docs/20, the CPU in the cart** (owner: a solid 30 fps at the game's speed, via the FPGA's HuC6280). `fpga/huc/`
     (GHDL `ghdl-llvm`; `apt install ghdl ghdl-llvm`, and link libLLVM-18.so.18.1): the core exact on three whole
     windows. ares is v1.7 now (the owner: "use the new ares"): the engine branch rebased, ares-headless rebuilt.
     Next: the VDC's timing in RTL, the hardware page, the FIFO, rom1, the SH2's side.
  18. **docs/21-23:** `fpga/huc/vdc_timing.sv` (= the C model: run_eq.sh), the core in Verilator (run_vtb.sh: GHDL synth
     to `/tmp/vhuc/huc_cpu_dbg.v` first), the two together (run_vsys.sh; `RUN=1`: from models, the end against
     Mednafen's). Next: the FIFO and the SH2's side, the hardware page in RTL, Quartus.
  19. **docs/24, the FIFO:** `TB_FIFO=` on `RUN=1 run_vsys.sh` writes it; `fpga/huc/fifo_v32.c` rebuilds each picture's
     state from it; rt_pc `RUN_V32HASH=1` (`work/rtvh`) gives the runtime's. Next: the FPGA in ares (ares's PCE CPU as
     a stand-in), the SH2's build from the FIFO, the FPGA logic and Quartus.
  20. **docs/25:** `FIFO=work/fifo_st3.bin` in EXTRA plays the recorded FIFO in the picture build (screenshots after
     ares frame 4000: the disc loads 1.85 MB first). 24 pictures a second at the game's full speed; the waits are the
     stack's commands 79 / 78. ares's recompiler doesn't step the FRT: the watchdog gives the line.
  21. **docs/26, VSSWAP** (owner: change 79 / 78): CD32X-BootCamp branch `vsswap` (ledger 24b, docs `d696f4b`); the PR
     is the owner's to open/merge. `work/bc3` carries it. 263 pictures / 600 frames. Next: the sprites' cost.
  22. **docs/27:** 262 pictures: 198 at 2 refreshes, 54 at 3, 10 at 4 (`RTX=-DPACE_HIST`). A picture's drawing must end
     within ~1.4 refreshes for 2; the list rule (line 196) decides. Next: docs/27's list (sprite clears, tests, bg).
  23. **docs/28** (owner: FPGA cell conversion, exact): `fpga/huc/cellstore.c` (copy-on-write from a frame boundary);
     rt_pc `-DSPR_FPGA_CELLS` + `RUN_FIFO_CS=1` (EXTRA_C=fpga/huc/cellstore.c): pictures identical. Next: cellstore.sv,
     the SH2's read path, the ares board model.
  24. **docs/29, the store in ares:** board model `fpga/ares/0002-ares-sapphire-cells.patch` (`ARES_CELLS`, `ARES_CELLS_VRAM`,
     `ARES_CELLS_LEAD=0`); the store follows the SH2's count (reg 0xca) and answers only after the unlock (0xcb a5/5a).
     `pic_check.sh st3 work/sel_st3_600m <tag> ...` with `EXTRA="SEC=2 VS_SEC=1 VSSWAP=1 FPGA_CELLS=1 FIFO=work/fifo_st3.bin"`;
     `work/hist.sh <tag>` reads the pacing histogram (`RTX=-DPACE_HIST`). 264 pictures, exact, but no faster than KEEP.
     Next: docs/27's list; cellstore.sv; what reads the engine window at start-up.
  25. **docs/30, the background's list faster:** `WIN_FOR_DIRTY`, `win_count` kept, the journal in SDRAM; 311 pictures
     (70 / 205 / 25 / 11 at 1-4 refreshes), all identical on the PC. `work/prof.sh <tag>` (BG2PROF=1) times bg2's steps.
     Next: the tiles' hash and mask from the FPGA (the heavy lists' 120 lines).
  26. **docs/31, the tiles' hash and mask from the FPGA:** `cs_tile` (cellstore.c), `BG2_FPGA_TILES` / saspeed
     `FPGA_TILES=1`, ares window 0x1B8000. 320 pictures (76 / 214 / 25 / 5). PC check: rt_pc with `-DSPR_FPGA_CELLS
     [-DBG2_FPGA_TILES]` + `EXTRA_C=fpga/huc/cellstore.c`, `RUN_FIFO_CS=1 RUN_BG2_TILE_CHECK=1`. Next: the boss's sprites.
  27. **docs/32:** pic_extras' number blocks drawn only where they change (37 -> 19 lines); `SPRPROF=1` times the
     master's sprites (1,800 clocks a cell: the frame buffer shared with bg2's tables). Next: VRAM read from the FPGA,
     the 64 KB freed, rt_bg2's tables to SDRAM (no journal).
  28. **docs/33, 30 fps:** tiles' tags/hashes in SDRAM (`bg2_ttag`, `bg2_thash`); fifo_play draws at most every 2nd
     frame. 292 pictures, 282 at 2 refreshes (10 slow: the stage's first pictures, one boss). PC reference remade
     (`work/pic_st3_all.bin`; old `.docs32.bin`, `work/rtbg.docs32`). Open: line 0's V scroll late in 3 of 21
     screenshots (the commit vs the raster).
  29. **docs/34, cellstore.sv:** the store in RTL on a 16-bit memory port (cells, tiles, count-following cursor, unlock,
     held register writes, the SH2's reads first); `RUN_LAT=<n>` the memory's latency; `fpga/huc/run_eqcells.sh work/fifo_st3.bin work/tr/st3_600/vdc_start.bin 600 2`: equal to cellstore.c
     at every lock (every 1, 2, 3). Next: the PSRAM mapping, the engine window decode, the VDC feeding the cursor.
  30. **docs/35, the top line:** BootCamp `vsswap` 5403e34 (ops 2 / 3 wait for the 32X's blank too; ledger 24b, docs
     f81fc3f): VSRAM's top never at line 0 now. ares `ARES_VSRAM_LOG=<frame>` (fpga/ares/0003). Open: the status bar's
     split late ~1 picture in 10 (command 79 masks the H interrupt). HWIN (ledger 31) tried from its text: worse.
  31. **docs/36, playing it:** `inp/play1.npz` (tools/m0b_play1.py: a 1P run from power-on), window `tr/p1_8600`,
     `sp/p1_8600.bin` (= `p1`), selections `sel_p1`, `sel_p1_600m`. `SPLAY=1`: pad 1 to the game, endless; ares presses
     via `PRESS="--press f:BC ..." pic_run.sh`. DIAG areas moved into `diag_area[]` (they were in the master's stack).
  32. **docs/37, every stage:** `LIVES=1 tools/m0b_play1.py 75000 playL` (lives CD RAM $A00C held; core `rampoke`).
     Windows `tr/ps<k>_600`, `sp/ps<k>.bin` (stages 1-5 at 8150 / 21650 / 37750 / 52450 / 66400). Translation `m3gen3`
     (m2 merged with playL's code, `m2_excluded/F8_85f9b51329` left out), rt_pc `rtobj3`, picture runtime `rtbg3`.
     Selections `sel_ps<k>_600m` (stage 1: `sel_ps1_1500m`). All five play in ares (SPLAY=1).
  33. **docs/38, a stage change on the PC:** the engine writes the card's RAM (ac_engine_model.c); pack
     `AC_REGION_BASE=<printed offset>` keeps regions out of 0x1C0000-0x1FFFFF; a poked bank's code marked changed (rt.c).
     Window `tr/t12`, `sp/t12.bin` (stage 1 -> 2, 1,700 frames): rt_pc `-DRT_AC_FPGA` (/tmp/rtace3) with RUN_RW=0x69,0x85
     equal to all-writable. ac_engine.sv writes too (tb: 1,586,486 reads, 0 differ); ares fpga/ares/0004. rt_pc's cart:
     state at 0x080000. Next (docs/38 end): card in an unmapped page, stage blocks per page, CDR=1, power-on.
  34. **docs/39, the whole game in the cart:** the card in page 6 (`CARD=1`, stack `CART=16 M1PAGE=1`; pack
     `AC_CARD_AT=0xC00000 AC_CARD_N=32 AC_CARD_OUT=`, `PACK_CUE=`; rt_pc `RUN_CARD=`); `saspeed/game_files.h` (102 DXTRACK,
     103 + IMAGE24 the card, 104+ the combinations); `tools/m3_pagemap.py` (`m3gen3/pagemap_playL.txt`); the blocks in 11
     combinations (`tools/m3_combo.py`, `COMBO=1`, `saspeed_combo.blk`: `work/comboh`, built and put on one disc by
     `tools/m3_combo_build.sh`). Split: `COMBO_SPLIT=work:<12 windows> COMBO_OWN=... SPLIT_ASM=1 SPLIT_MIN=7000
     SPLIT_MIN_FOR="75,76,77:200"` (docs/39 §4). New windows `tr/c00 c02 c04 c06 c08 c10_600`. ares `ar_cbh`: c02 then c03
     loaded, boss -> briefing -> stage 2, hash b2fc64ab = rt_pc's (with the stack page: differs). Power-on state
     `sp/pon.bin` (CARD=2, CD ring at 0x160000 in COMBO builds). Next: ares's PCE CPU as the FPGA's stand-in (speed).
  35. **docs/40, the game live in ares:** `tools/m3rt/fifo_synth.c` (rt_pc `RUN_FIFO_OUT=`), `tools/m3rt/huchost.c` built by
     `tools/m3_huchost.sh work/m3gen3 work/huchost.so` (blocks' objects in `huchost.so.d`); ares patch
     `fpga/ares/0005-ares-huchost.patch` (`ARES_HUCHOST`, `ARES_HUC_STATE`, `ARES_HUC_TRACK`, `ARES_HUC_LEAD`, `ARES_HUC_LOG`);
     saspeed `FIFO_LIVE=1`; `tools/m3_live_run.sh work/sp/pon.bin work/tr/pon <tag> <frames>` (`PRESS="--press f:A ..."`).
     From power-on: title, briefing, stages. Open: sound, the interpreter != blocks.
  36. **docs/41, the music:** huchost's CD audio as FIFO type B; `tools/m3_music.py` (`work/music_cd32x.off`); saspeed
     `FIFO_CDDA=1` (off: no sound in ares yet, and a disc with the 14 music tracks leaves the title's background on the
     loading screen's tiles - with the stack's test track it is clean). Next: why.
  37. **docs/42, the title:** `tools/m3rt/rt_bg32.c`, saspeed `BG32=1` (in EXTRA for `m3_live_run.sh`): a background with
     more than 4 palettes drawn on the 32X layer in the sprite palettes' free entries. The title as the reference's.
  38. **docs/43, the music:** the owner's `Session 1/Track NN.wav` (uploads), linked as `work/music_cd32x/TrackNN.wav`
     (n - 1) and in `work/bc3/saspeed/music`; `EXTRA="BG32=1 FIFO_CDDA=1"`: the music in ares through the title and
     stage 1. Title presses: hold RUN ~30 frames.
  39. **docs/44, PSG and ADPCM:** `tools/m3rt/pce_sound.c` (in huchost.so; `io_psg_hook`, `rt_io_until`), ares patch
     `fpga/ares/0006` (ring at cart 0x3F0000, reg 0xcc), saspeed `PWM_SOUND=1` (crt0.s `sec_pwm_irq`). Effects heard in
     ares in stage 1; ADPCM at 0.87 against the reference; all together (`BG32=1 PWM_SOUND=1 FIFO_CDDA=1`) plays.
  40. **docs/45, the pad for real:** docs/40-44's "played" was RUN held (pad never sent; a start-up scan wrote reg
     0xcd; ares `--press` holds until the next). Fixed: fifo_play sends the pad, ares patch 0007 gates it; release
     presses with `--press f:`. Stage 1 played in ares.

## 2. Where things live

| What | Where | In git? |
|---|---|---|
| This port | `/home/claude/sapphire_cd32x` (MicroNut99/Sapphire_CD32X, `main`) | yes |
| The template | `/home/claude/draculax_32x` (MicroNut99/DraculaX_32X) | yes |
| BootCamp | `/home/claude/repos/CD32X-BootCamp` | yes |
| The SH2 toolchain | `/opt/toolchains/sega/sh-elf` (GCC 12.1, from the owner's `sh-elf-toolchain.zip`, stripped and re-packed on his PC as `sh-elf-toolchain/sh-elf-stripped.tar.zst`, 84 MB; untar, then `chmod +x` `bin/` and `libexec/`). The m68k-elf toolchain is not set up yet (DraculaX's `tools/toolchain/setup_m68k.sh`) | no |
| m68k-elf (stand-in), ldscripts, boot blocks | `/opt/toolchains/sega` (docs/05 §5) | no |
| ares | `/home/claude/ares_build/ares-headless`; source `/home/claude/ares_android` | no |
| The build tree | `/home/claude/work/bc1` (`cd32x/apply.sh`) | no |
| US CD BIOS | `/mnt/user-data/uploads/Sapphire/bios/bios_CD_U.bin` (staged from the owner's folder) | never |
| **Game data**, the work folder | `/home/claude/work`: `core/`, `disc/` (cue + symlinks), `sys/`, `states/` (`idle_NNN.st` 0-1380 s, `st1_play.st`), `rec/st1-5`, `inp/` (demo, st5: docs/04), `m0/`, `packs/` (docs/03), `m1/` (docs/05), `ar_*` (ares runs), `shots/` | never |
| The owner's originals | his PC, `S:\KoboPort\sega-toolchain-12.1\sega\Sapphire`: the cue / bin, `Track 02.iso`, `Track 17.iso`, `syscard3.pce`, `bios/`, `bootblocks/`, `ldscripts/`, `Session 1/` (the audio as WAV), `sh-elf-toolchain/` and the `m68k-elf-gcc` files (put there 7 Nov 2026 for M1 on: the BootCamp's `ROOTDIR`) | never |

If the session's workspace is new, stage the files again from the owner's folder (the device bridge:
`Track 02.iso`, `Track 17.iso`, the cue, `syscard3.pce`) and rebuild with README's steps. The owner offered his
PC's space and compute for the big traces (DraculaX: ~0.7 MB a frame).
