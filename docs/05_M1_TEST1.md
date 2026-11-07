# 05 - M1: every stage's frames on the Genesis plane + the 32X, the HUD as Genesis sprites, RAM cart and EDCD (7 Nov 2026)

**What was tested:** one Mednafen frame of each stage drawn the REV way: the background on the Genesis' plane A
through the BootCamp stack's Genesis picture commands, the sprites on the 32X in front. The test ROM is `satest/`
(DraculaX_32X's `dxtest`, adapted). The converter is `tools/m1_testframe.py` (DraculaX's, adapted).
**Run:** ares (`ares_android`, `ares-headless`, `ARES_32X_TIMING=console`, US BIOS) on the 4 MB RAM cart and as
an **EDCD** boot (the EverDrive PRO, `--ramcart-type everdrive --ramcart-boot EDCD.32X`).
**Not tested:** on the console. A green ares run is never a console pass.
**The boot stack: unchanged** (`check_boot.py`: 21 files as the kit ships them; built `GENPLANE=1 VSPLIT=1`).

## 1. The frames, on the PC (test 1: one frame a stage)

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
- **Fixed at the owner's request (7 Nov 2026):** CD32X-BootCamp PR #1 (branch `vsplit-mode1`): under `OL_MODE1`,
  inside the `DX-VSPLIT` block, `set_vs_line` also points the jump's target (`0xFFFD0E`) at `hint_vs`. The default
  build and the CD boot's VSPLIT build are byte-identical to before. **This frame on the EDCD boot with the split:
  0 of 71,680 pixels off** (was 3,233). Documented in the BootCamp's docs/03 and docs/12; the ledger's §21b
  (MicroNut99/docs) is still to be written from docs/12's section.
- This affects every game using VSPLIT on the EDCD boot (Dracula X's stage 8 bands; ledger §21).

## 4. What it means for the port

- **The REV split reproduces Sapphire's picture exactly**, every stage, both boots: M1's first question answered
  in emulation.
- **The HUD: two ways, both exact in ares.**
  - (a) on the Genesis, split by command 78: needs the stack fix (§3) for EDCD;
  - (b) on the 32X: 256 × 21 opaque pixels, no H interrupt, no stack change. In the game it would be redrawn
    when the HUD changes (the scores, the lives); it took 10 more of the 32X's 256 colours on every stage here.
  
  **D3, the owner (7 Nov 2026):** first "the 32X is ok", then **"taking the status bar off the 32X is worth
  gaining some fps. The Sega CD's 68000 and its Word RAM could build the status bar's tiles and sprite table,
  so the SH2s wouldn't have to"**: the HUD as **Genesis sprites** (§7), built in the port by the Sub-CPU.
- Stage 1's merge (11 % of pixels a step off) is as Dracula X's D2 (4.8-4.9 %); stage 5 fits exactly after all.

## 6. Every frame of every stage, on the PC (`tools/m1_checkall.py`)

Every 10th frame of each stage's play (docs/04's windows), 1,864 frames, captured as §1 (VRAM also at the frame's
end) and converted. Three things found and fixed in the converter on the way:
- **The PC Engine's sprite limit:** at most 16 cells of 16 pixels on a line, in table order. Mednafen fetches a
  32-wide sprite as two halves, left first, so with 15 used its left half is drawn (`FetchSpriteData`). Sapphire's
  busy lines reach 32 cells, so the port must keep the limit to look the same.
- **The picture's first row:** searched (a black top, as stage 5's space, fooled the first-non-black rule).
- **Splits above the HUD:** stage 5 has a band at lines 0-24 on some frames; the HUD is the *last* split.

| Stage | Frames | Re-render = Mednafen | of them from the frame-end VRAM | Not reproducible from one VRAM state |
|---|---|---|---|---|
| 1 | 390 | 384 | 1 | 6 |
| 2 | 390 | 366 | 13 | 24 |
| 3 | 384 | 367 | 61 | 17 |
| 4 | 390 | 389 | 4 | 1 |
| 5 | 310 | 310 | 6 | 0 |

**The game writes sprite cells into VRAM while the picture is drawn** (nearly every frame writes VRAM then).
Mostly the picture equals VRAM as at the display's start, sometimes as at the frame's end (85 frames), and in 48
frames (2.6 %) it is a mix line by line (a boss's wings half old, half new), which no single state reproduces.
In the port the sprites are drawn from one state: on those frames a cell would show its old or new shape a
frame early or late, for part of its height. Measured here, judged in M3 (drawing after the frame's last write,
as DraculaX draws at a VRAM event of its schedule, is the likely answer).

**The composite (Genesis + 32X) equals the re-render on every frame that is not merged, every stage, every HUD
way.** Merged frames (stage 1's city does not fit 4 × 15):

| HUD | Stage 1 merged (worst loss) | Stage 2 | 3 | 4 | 5 | Tiles, most |
|---|---|---|---|---|---|---|
| Genesis split (78) | 172 of 390 (13.9 %) | 2 (4.9 %) | 0 | 0 | 25 (14.5 %) | 374 |
| **Genesis sprites (D3)** | **174 (13.3 %)** | **0** | **0** | **0** | **0** | 469 |

(The split ran before the last-split fix: stage 5's 25 merged frames are its band frames, taken as the HUD.)
The HUD uses 10 colours (14 at most, stage 5): one Genesis palette holds it.

## 7. The HUD as Genesis sprites (D3)

`m1_testframe.py --hudspr`: lines 203-223 as a row of eight 32 × 24 Genesis sprites (4 × 3 cells, priority set)
at x = 32-288, y = 203, over plane A, which keeps the playfield's scroll on every line; each sprite in one of the
plane's 4 palettes (their colour sets packed with the plane's). 96 tiles and 8 sprite-table entries. `satest`
loads the table with command 63 (no new command).

| ares (`ARES_32X_TIMING=console`) | Stage 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| 4 MB RAM cart | 0 of 71,680 | 0 | 0 | 0 | 0 |
| EDCD (EverDrive PRO) | 0 of 71,680 | 0 | 0 | 0 | 0 |

No split, no H interrupt, nothing on the 32X: the SH2s never touch the HUD. In the port the Sub-CPU (the Sega
CD's 68000) builds the digits' tiles and the 8 entries in Word RAM when the scores or lives change, and the
68000 copies them to VRAM: a new stack command, behind a switch, at M3.

## 8. The tools set up for this round (not in git: the container's)

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
