# 05 - M1 test 1: a frame of every stage on the Genesis plane + the 32X, RAM cart and EDCD (7 Nov 2026)

**What was tested:** one Mednafen frame of each stage drawn the REV way: the background on the Genesis' plane A
through the BootCamp stack's Genesis picture commands, the sprites on the 32X in front. The test ROM is `satest/`
(DraculaX_32X's `dxtest`, adapted). The converter is `tools/m1_testframe.py` (DraculaX's, adapted).
**Run:** ares (`ares_android`, `ares-headless`, `ARES_32X_TIMING=console`, US BIOS) on the 4 MB RAM cart and as
an **EDCD** boot (the EverDrive PRO, `--ramcart-type everdrive --ramcart-boot EDCD.32X`).
**Not tested:** on the console. A green ares run is never a console pass.
**The boot stack: unchanged** (`check_boot.py`: 21 files as the kit ships them; built `GENPLANE=1 VSPLIT=1`).

## 1. The frames, on the PC

`tools/m1_capture.py` takes a frame from a demo state with VRAM, the sprite table and the colours as they were
at the first display line (DraculaX's `dumpvid0`; the frame-end table already holds the next frame's sprites)
and the per-line log. `tools/m1_testframe.py` then:
- **re-renders the PC Engine picture** and compares it with Mednafen's screenshot. Each line uses its own CR,
  so the HUD's lines have no sprites (CR bit 6 off below line 203: docs/04 §1).
  **0 of 57,344 pixels differ on all five stages.**
- **converts** only the map cells the picture shows (Sapphire's 128 × 32 BAT holds more than the screen) into
  Genesis tiles and 4 palettes of 15, and the sprites into the 32X layer;
- **checks the composite** (Genesis plane, then the 32X over it) against the re-render: **0 pixels differ on all
  five**.

| Stage | Frame | Genesis tiles | 32X colours | Palettes |
|---|---|---|---|---|
| 1 | `idle_200` + 23 | 157 | 58 | **merged** (D2-like, k-means): 11.0 % of pixels one step off, mean 2.6 / 255 a channel, at most 37 |
| 2 | `idle_300` + 41 | 102 | 32 | exact |
| 3 | `idle_420` + 37 | 116 | 47 | exact |
| 4 | `idle_520` + 17 | 119 | 48 | exact |
| 5 | `$0E15` = 4 at `idle_140`, + 2,401 | 188 | 3 | exact (the greedy packer failed; the k-means found an exact 4 × 15) |

(Stage 1's composite is compared with the merged colours: "0 pixels" there means the console shows the
merge exactly; the merge itself is the 11 % above.)

## 2. The test ROM: `satest/`

As `dxtest` (DraculaX docs/03 §2), with two changes for Sapphire:
- **The map's size from the file** (`SAF1` header: width, height). Register 16 is set to it: 128 × 32 here, a size
  the Genesis has.
- **The map sent with command 63** (cart → VRAM, linear, 1,023 words a command), row by row from 0xE000: command
  67's column copy steps 64 cells a row (the listener's `copy_vram_col` sets auto-increment 0x80), so it only
  fits 64-wide maps. 63 needs no stack change.

Every command answered (`4B10`).

| Boot | HUD | Stages | ares vs `expected.png` (320 × 224) |
|---|---|---|---|
| 4 MB RAM cart | Genesis, V split at line 203 (command 78) | 1, 2, 3, 4, 5 | **0 of 71,680** on each |
| EDCD (EverDrive PRO) | Genesis, V split (command 78) | 3 | **3,233 wrong: the HUD's 21 lines** (§3) |
| EDCD | **the 32X draws lines 203-223** (`--hud32x`) | 1, 2, 3, 4, 5 | **0 of 71,680** on each |
| 4 MB RAM cart | the 32X draws lines 203-223 | 3 | **0 of 71,680** |

## 3. Found: command 78 (VSPLIT) has no H interrupt in a cartridge (EDCD) boot

- **What ares shows:** on the EDCD boot, lines 0-202 are right. Lines 203-218 show the HUD 5 lines too far down,
  and 219-223 garbage.
- **Why:** with no split at all, line 203 shows map row (292 + 203) mod 256 = 239, which is the HUD's row 5
  (the HUD starts at map row 234 = 31 + 203). The playfield's V scroll wraps onto the HUD's rows; nothing
  changed the V scroll at line 203.
- **The cause, in the stack's source:** `set_vs_line` (`stack/subcpu/hw_md.s`) hooks the H interrupt by writing
  `hint_vs`'s low word to `0xA12006`. That is the Mega-CD gate array's H-INT redirect, which only applies when
  the 68000 runs from the Sega CD's vectors (a CD boot). In the cartridge boot (`MODE1`), the 68000's level 4
  vector is the cart's: `EDCD.32X` sends it to the RAM jump table at `0xFFFD0C`, which the launcher fills with
  `jmp 0xFFFDAE`, an `rte` (`edboot/edcd_md.s` step 2). So `hint_vs` never runs.
- **Not changed:** the stack is off limits without the owner (CLAUDE.md rule 2). The likely fix (under
  `OL_MODE1`, also write `hint_vs`'s address into the jump table at `0xFFFD0E`) was not built:
  `check_boot.py` refuses a changed stack, as it should.
- This affects every game using VSPLIT on the EDCD boot (Dracula X's stage 8 bands; ledger §21).

## 4. What it means for the port

- **The REV split reproduces Sapphire's picture exactly**, every stage, both boots: M1's first question answered
  in emulation.
- **The HUD: two ways, both exact in ares.**
  - (a) on the Genesis, split by command 78: needs the stack fix (§3) for EDCD;
  - (b) on the 32X: 256 × 21 opaque pixels, no H interrupt, no stack change. In the game it would be redrawn
    when the HUD changes (the scores, the lives); it took 10 more of the 32X's 256 colours on every stage here.
  
  **D3** (the owner's) chooses.
- Stage 1's merge (11 % of pixels a step off) is as Dracula X's D2 (4.8-4.9 %); stage 5 fits exactly after all.

## 5. The tools set up for this round (not in git: the container's)

| What | Where |
|---|---|
| sh-elf GCC 12.1 | `/opt/toolchains/sega/sh-elf` (the owner's, docs/03's pickup) |
| m68k-elf | DraculaX's stand-in (`tools/toolchain/setup_m68k.sh` there: Ubuntu's m68k-linux-gnu, freestanding; libgcc from GCC 12.1's `lb1sf68.S`). For a disc to burn, the owner's WSL toolchain |
| `ldscripts/`, `bootblocks/` | the owner's, from his Sapphire folder |
| ares | `/home/claude/ares_build/ares-headless` (ares_android, BootCamp docs/13) |
| The build tree | `/home/claude/work/bc1` (`cd32x/apply.sh <BootCamp> <dir>`: the BootCamp copied, `satest/` added) |

## How to make it again

```
python3 tools/m1_capture.py $W idle_420.st 37 $W/m1/st3_a.npz
python3 tools/m1_testframe.py $W/m1/st3_a.npz $W/m1/st3_a [--hud32x]
bash cd32x/apply.sh <BootCamp checkout> $W/bc1 && cp $W/m1/st3_a/LEVEL00.BIN $W/bc1/satest/cd/
cd $W/bc1 && [EDCD=1] GENPLANE=1 VSPLIT=1 GAME=satest DISC_NAME=SATEST bash build.sh
ARES_32X_TIMING=console ares-headless --game disc/SATEST.cue --machine cd32x --bios-us <bios_CD_U.bin> \
    [--ramcart-type everdrive --ramcart-boot edboot/EDCD.32X] --ramcart r.ram --home h --frames 3000 --screenshot 2999:s.ppm
python3 tools/m1_aresdiff.py s.ppm $W/m1/st3_a/expected.png cmp.png
```
