# 16 - The picture: the game on screen (7 Nov 2026)

The owner: "just build the game". This round puts the running game on screen: the background on the Genesis plane,
the sprites on the 32X, and the status bar in its own band. It adapts DraculaX's drawing (its docs/53-67) to
Sapphire. All checks are in ares on the 14 MB EverDrive with Sapphire's core, against the PC and Mednafen.
**A green ares run is not a console pass.**

## 1. No rip: every cell converted from VRAM

DraculaX ripped its cells from the disc (its D6) and converted on the console only what the game's code made.
Sapphire streams its sprite graphics from the Arcade Card (docs/14), so everything is treated as the code's:

- `MARKS=1` with an empty key table and seed (no hooks: `m2_recomp.py`'s `HOOKS` is empty for Sapphire).
- Every VRAM write tags its unit as code with the frame it happened in.
- The hot assembly must tag its own writes: `SPLIT_MARKS=1` in `split`. Without it the cache keeps stale tiles,
  which showed as rectangles of wrong background in ares.

| Check (rt_pc `RUN_SPR=-`, 600 frames) | Stage 1 | Stage 3 | Stage 5 |
|---|---|---|---|
| sprite cells drawn a frame | 61.0 | 79.9 | 55.2 |
| converted a frame, cache 32 (DraculaX's) | - | 55.5 | - |
| converted a frame, cache 96 | 7.3 | 11.3 | 6.8 |
| wrong pixels against the sprite layer drawn straight from VRAM | **0** | **0** | **0** |

The 96-cell cache (`SPR_SC`, now a build option) does not fit the frame buffer's spare part beside `rt_bg2`'s
tables. The SH2 build keeps 32 for now.

## 2. The background (`tools/m3rt/rt_bg2.c`)

- **The map's size from MWR**, not only 64 × 64: 32-128 wide, 32-64 tall, up to 4,096 entries (128 × 64 is more than
  a Genesis plane). Register 16 is sent in the list when the size changes. Sapphire's map is 128 × 32.
- **Only the window counts.** Sapphire's 128-wide map holds 215-234 colour slots in all 16 palettes, which never
  fits the Genesis' 4 × 15. The window a picture can show does (rt_pc `RUN_BGSTAT_WIN`):

  | Stage | 1 | 2 | 3 | 4 | 5 |
  |---|---|---|---|---|---|
  | fits 4 × 15 by value, the window | 22 % | 94 % | 100 % | 100 % | 100 % |

  So the colours are counted, exactly, over the window only (33 × 29 entries a band), and only its entries are sent.
  The others stay marked until they scroll into view. When the tile slots run out, the entries out of view let
  theirs go.

## 3. The status bar: the raster split

The game changes the scroll at line 203 for its status bar, through the raster-compare interrupt. Mednafen's
per-line log for stage 3 frame 23,900:

| From line | X | Y | CR |
|---|---|---|---|
| 0 | `050` | `1DE` | `0CC` (sprites on) |
| 203 | `300` | `0EA` | `08C` (sprites off) |

- **The runtime (`rt_vdc.c`):** a raster-compare match opens a split at the next line with the registers then; the
  game's BXR / BYR / CR writes fill it, until the display ends. Writes after that are the next frame's top. The
  picture uses the last whole frame's splits (`spl`). rt_pc `RUN_SPLITS` lists them: one split on every stage, line
  203, BXR `0x300`, BYR `0x0E9`, CR `0x08C`. Mednafen's line 203 Y `0x0EA` is BYR + 1.
- **The background (`rt_bg2.c`):** the picture is made of bands (the top, then each split), each with its own
  window.
  - **H scroll:** the H scroll is per line (register 11 = 3). Two tables take turns: the list writes the whole table
    (448 words) into the one not shown when a band's X changes, and the kept run switches register 13 at the
    commit, in step with the 32X swap.
  - **V scroll:** command 78 (the stack's VSPLIT, `VSPLIT=1`) gives each band below the top its V offset (Y −
    line). Entry 0, the top's, is sent every picture while a split is on: command 78 writes it after line 223 for
    the next picture. With only the kept run's VSRAM write the playfield sat at a stale V offset in ares.
  - `main.c` sends the entries before the list that asks for the swap, under `cdr_busy`.
- **The sprites (`rt_spr.c`):** `spr_ylim`, the lines the CR in force shows sprites on, is set each picture. The bar
  has none. It goes into each sprite's signature, and an old box is cleared at its full height.

**Checked on the PC (rt_pc `RUN_BG2`, every band's window, each line's H scroll, VSRAM, the splits):**

| Stage | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| pictures checked (the rest still sending or not fitting) | 0 | 376 | 586 | 584 | 567 |
| wrong pixels / scroll wrong | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| colours not fitting | 598 | 220 | 0 | 0 | 13 |

## 4. Against Mednafen, and ares against the PC

- **`tools/m3_piccheck.py`:** rt_pc's `RUN_PIC_DUMP` writes the picture as the console's models make it (the Genesis
  plane with the per-line H table, VSRAM and the V splits, the 32X sprites in front), and the tool compares it with
  Mednafen's screen at the same frame. Stage 3, every 40th picture: **8 of 15 exact** (with the bar). The first
  picture's plane is not filled yet, and six have 12-190 wrong pixels: the PC Engine's limit of 16 sprite cells a
  line, not modelled yet (the boss's beam cut on Mednafen).
- **`tools/m3_arescheck.py`:** ares's screenshots of the picture build against every PC picture, the closest found.
  Stage 3, `SEC=2` (the secondary SH2 makes the background list): **13 of 14 screenshots exact (0 wrong pixels)**;
  one has 64.
- **`SPLIT=1`** (both SH2s drawing the sprites, docs/67's) gives wrong pixels, the sprites out of step. Left off.

## 5. Speed: the next work

The picture build runs stage 3 at **22-27 game frames a second** in ares. The pacing shows one picture a refresh, so
a frame needing more than one refresh waits for the next. The profile (master SH2, `SEC=2`):

| Part | Share |
|---|---|
| the game's code (hot 16.8 %, cold in the cart 17.7 %) | 34.5 % |
| the runtime | 21.7 % |
| the sprites (drawing, converting 5.3 %, clearing 3.1 %) | 21.6 % |
| `draw_hook` | 8.1 % |
| waiting on the 68000 (command 78 every picture) | 6.1 % |

The secondary is idle 72 % of the time. The drawing's code took SDRAM from the hot set (`SPLIT_MIN=600` now, 300
before). The VRAM in the FPGA (docs/15 §4) would give it back on the 14 MB cart.

## 6. Open

- Speed (§5).
- The colours of stages 1 and 2 (merged, as M1 did for stage 1).
- The 16-cells-a-line sprite limit.
- `SPLIT=1`.
- Then the game played: the controller, the stage loads from the disc, from power-on.

## How to make it again

```
XF="-DHUC_MARKS -DRT_BG2 -DSPR_SC=96" EXTRA_RT="rt_spr rt_bg2" bash tools/m3_build_rtpc.sh $W/m3gen2 $W/rtbg
RUN_BG2=1 rt_pc st3.bin 600 tr/st3_600; RUN_SPR=- rt_pc ...; RUN_SPLITS=4 rt_pc ...
RUN_BG2=1 RUN_SPR=- RUN_SPR_DO=1 RUN_PIC_DUMP=pic.bin RUN_PIC_EVERY=40 rt_pc ...; python3 tools/m3_piccheck.py $W demo 23700 pic.bin
cp -r sel_st3 sel_st3_600m && SPLIT_MARKS=1 SPLIT_ASM=1 SPLIT_MIN=600 python3 tools/m3_speedtest.py split $W sel_st3_600m tr/st3_600
cd $B && GEN=... ACE=1 EDCD=1 M1WORDS=1 SDRAM_BANKS=0x69,0x85 CARTCODE="io_model scsi_model ac_model" MARKS=1 KEYS=none.keys \
  SEED=none.seed SPR=1 CELLS=none.cells MARKSIN=code.marks PACE=1 BG=1 SEC=2 GENPLANE=1 VSPLIT=1 FBVDP=1 GAME=saspeed ... build.sh
(none.keys: 16 zero bytes; none.seed: 1,024 bytes 0xFF; none.cells: 16 zero bytes; code.marks: 2,048 x 0x8000 big-endian)
tools/m3_picrun.sh, tools/m3_piccheck_ares.sh: the work folder's pic_run.sh / pic_check.sh
```
