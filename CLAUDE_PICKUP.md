# Pick-up notes for the next Claude session (7 Nov 2026, after docs/05)

Read `CLAUDE.md` first.

## 1. Where it stands

- **M0 done** (docs/02). Mednafen boots the disc with the Arcade Card, deterministic from power-on; the attract
  demo is the recording source (stages 1-4); `$0E15` = stage index (4 = stage 5).
- **Open, in this order:**
  1. **D1 decided: both carts** (docs/03). `tools/make_packs.py` lays out the Arcade Card for each; every later
     build makes both (the 14 MB one `CART=ED`, pages switched; the 4 MB one loading `STGn.PAK`).
  2. **M0b done** (docs/04). Left from it: stage 5 recorded with real inputs to its end, and where LBA 8244-8819 /
     8956-11003 go (the later scenes, the ending). The reference recordings are `inp/demo.npz` and `inp/st5.npz`.
  3. **M1 test 1 done** (docs/05): every stage's frame exact in ares, RAM cart and EDCD. **D3 open** (the HUD: the
     Genesis split needs a stack fix on EDCD, docs/05 §3; or the 32X draws it). The owner was told about the
     command 78 / MODE1 finding. Next in M1: the rip of whole stages on the PC.
  4. **M2:** DraculaX's `tools/m2_*` and `m2host/` with the Arcade Card's ports in the CPU model's I/O map
     (`$1A00-$1AFF`, banks 40-43).

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
