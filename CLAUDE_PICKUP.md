# Pick-up notes for the next Claude session (7 Nov 2026, after docs/04)

Read `CLAUDE.md` first.

## 1. Where it stands

- **M0 done** (docs/02). Mednafen boots the disc with the Arcade Card, deterministic from power-on; the attract
  demo is the recording source (stages 1-4); `$0E15` = stage index (4 = stage 5).
- **Open, in this order:**
  1. **D1 decided: both carts** (docs/03). `tools/make_packs.py` lays out the Arcade Card for each; every later
     build makes both (the 14 MB one `CART=ED`, pages switched; the 4 MB one loading `STGn.PAK`).
  2. **M0b done** (docs/04). Left from it: stage 5 recorded with real inputs to its end, and where LBA 8244-8819 /
     8956-11003 go (the later scenes, the ending). The reference recordings are `inp/demo.npz` and `inp/st5.npz`.
  3. **M1:** a stage 3 or 4 frame through commands 62-67 in ares (DraculaX's `tools/m1_testframe.py`, `dxtest/`).
  4. **M2:** DraculaX's `tools/m2_*` and `m2host/` with the Arcade Card's ports in the CPU model's I/O map
     (`$1A00-$1AFF`, banks 40-43).

## 2. Where things live

| What | Where | In git? |
|---|---|---|
| This port | `/home/claude/sapphire_cd32x` (MicroNut99/Sapphire_CD32X, `main`) | yes |
| The template | `/home/claude/draculax_32x` (MicroNut99/DraculaX_32X) | yes |
| BootCamp | `/home/claude/repos/CD32X-BootCamp` | yes |
| The SH2 toolchain | `/opt/toolchains/sega/sh-elf` (GCC 12.1, from the owner's `sh-elf-toolchain.zip`, stripped and re-packed on his PC as `sh-elf-toolchain/sh-elf-stripped.tar.zst`, 84 MB; untar, then `chmod +x` `bin/` and `libexec/`). The m68k-elf toolchain is not set up yet (DraculaX's `tools/toolchain/setup_m68k.sh`) | no |
| **Game data**, the work folder | `/home/claude/work`: `core/`, `disc/` (cue + symlinks), `sys/`, `states/` (`idle_NNN.st` 0-1380 s, `st1_play.st`), `rec/st1-5`, `inp/` (demo, st5: docs/04), `m0/`, `packs/` (docs/03), `shots/` | never |
| The owner's originals | his PC, `S:\KoboPort\sega-toolchain-12.1\sega\Sapphire`: the cue / bin, `Track 02.iso`, `Track 17.iso`, `syscard3.pce`, `bios/`, `bootblocks/`, `ldscripts/`, `Session 1/` (the audio as WAV), `sh-elf-toolchain/` and the `m68k-elf-gcc` files (put there 7 Nov 2026 for M1 on: the BootCamp's `ROOTDIR`) | never |

If the session's workspace is new, stage the files again from the owner's folder (the device bridge:
`Track 02.iso`, `Track 17.iso`, the cue, `syscard3.pce`) and rebuild with README's steps. The owner offered his
PC's space and compute for the big traces (DraculaX: ~0.7 MB a frame).
