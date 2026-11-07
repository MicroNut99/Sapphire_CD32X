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
  5. **M3 step 1 done** (docs/08): `rt_pc` (`work/rtobj`, C from `work/m3gen`, state `work/sp/st3.bin`). **Next, M3
     step 2:** DraculaX's `dxspeed/` for Sapphire: the Arcade Card reached through docs/03's 64 KB region table
     (the 2.7 MB state does not fit the level slot), the SH2 build in ares (`ARES_32X_TIMING=console`), its hash
     against `rt_pc`'s. DraculaX's `tools/m3_sh2.py` (the hot code as SH2 assembly) after that.
     **Mind docs/08 §4:** a stage's whole translation is 1.61 MB at `-Os`; with its state and the card's regions it does not
     fit the 4 MB cart, nor a 14 MB page. So step 2 starts from DraculaX's split (`m3_speedtest.py split`: the hot half
     in SDRAM, the cold in the cart), and measures the cold half translated vs interpreted. `select` runs the checker
     named by `M2CHECK` (default `work/m2check`).

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
