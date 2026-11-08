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
