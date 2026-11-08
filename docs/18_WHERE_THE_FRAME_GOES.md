# 18 - Where the frame goes: the sprites' cell cache (7 Nov 2026)

The owner chose the FPGA's way for speed (the 14 MB cart faster; the 4 MB plain cart may stay at today's speed).
Before building it, the costs were measured. **They point at the sprites' cell cache, not at VRAM.**

## 1. VRAM's traffic is small (rt_pc `-DVW_COUNT`, 600 frames)

| Words a frame | Stage 1 | Stage 3 | Stage 5 |
|---|---|---|---|
| written from the card's port (`TIA`, `rt_tia_card_vram`) | 163 | 189 | 117 |
| written by `vram_port` (rt.c) | 100 | 220 | 99 |
| other port writes, VRAM-to-VRAM DMA, port reads | 0.3 | 0.2 | 0.2 |
| read by the sprite table's DMA | 256 | 256 | 256 |

The game never reads VRAM back during play.

## 2. The write log (`VLOG=1`): exact, and slower

docs/17 found that the master waits for the secondary's background job, because the job reads the live VRAM. With
traffic this small, the game's VRAM writes can wait in a log (address, word: 4 KB) while the job runs. The log is
applied, with its marks, at the next picture, at a read of VRAM, when it fills, or at the first write after the
job's end. The sprite table's DMA takes the log's words on top of its copy. While the log is on, `left_min` sends
every VDC access the runtime's way (`VLOG_FORCE`), so the hot code's in-line VRAM paths need no change. The job gets
a copy of the registers, the colours and the splits (`v32_head`), and reads VRAM through `bg2_vram`.

- **The PC (rt_pc `-DVLOG`, the log on from one picture to the next):** stages 1, 3 and 5 end on the same RAM and
  VRAM hash as without it. The sprites have 0 wrong pixels, and the background has 0 wrong pixels in every checked
  picture.
- **ares (`SEC=2 LATE=1 SPR_CMDS=64 VLOG=1`):** slower. The game frame shown at ares frame 4050 is 205, and 201 with
  the log ending as soon as the job does (`SEC=2`: 224). Every VDC access through the runtime costs more than the
  wait it saves.

The log stays as a build option.

## 3. The master's frame (ares `--profile`, frames 3950-4550, `SEC=2`)

| Function | Share |
|---|---|
| `convert` (a sprite cell from VRAM to the 32X's form) | 21.3 % |
| `spr_frame` (each entry's test, the cell cache's search) | 19.2 % |
| the hot code | 13.3 % |
| `spr_clear` | 5.1 % |
| `cd_cmd` (command 78) | 4.6 % |
| `spr_rows*` (the drawing) | 5.8 % |

**The sprites take about half the master's time.** docs/16's profile (frames 4000-4900, by part) put them at 21.6 %.

## 4. Why: the cache is too small

The cache holds 32 cells in the frame buffer's spare part (9 KB). Stage 3 draws 80 cells a frame, and 55 are
converted again. rt_pc counted, for each conversion, where its cell's 64 words came from (`vw_src`, each VRAM word's
card byte):

| 600 frames | Stage 1 | Stage 3 | Stage 5 |
|---|---|---|---|
| conversions | 20,948 | 33,275 | 9,990 |
| of a cell written from one run of the card | 3,101 | 4,920 | 2,220 |
| of a cell not written in the 600 frames at all | 17,236 | 27,488 | 7,569 |

83 % of stage 3's conversions are of cells that did not change: the cache only lost them. With every one of the 512
cells kept, stage 3 would convert about 8 a frame instead of 55. The 32-way search goes too: a cell's place is its
number.

## 5. The 14 MB cart's way: every cell kept in the cart

512 cells × 288 bytes = 144 KB, too much for SDRAM or the frame buffer, but small for the cart:

- A cell is converted when its marks change (as now), and written into the cart through the engine's poke: the
  address set once, then a read per byte. That is about the cost of a conversion, paid only on a real change. The
  master's cache is purged for those 18 lines.
- The drawing reads the cell from the cart, like the "from the cart" cells of DraculaX's rip (`spr_pool`).
- The 144 KB must be seen by the SH2. The test build fills the cart's first 4 MB (window 0 and the stage's page).
  docs/03's layout leaves 832-1,216 KB free in each stage's page and 448 KB in page 0. **The next step:** the
  test build on docs/03's 14 MB layout, then the kept cells.
- The 4 MB plain cart keeps the 32-cell cache in the frame buffer.

## Tools

- `tools/m3rt` `-DVW_COUNT`: VRAM traffic, the card's source of each word, and the conversions' sources.
  `ac_index_of` / `ac_step` in ac_model.c are for that measure only.
- `tools/m3_picprof.sh <stage> <split>` (= `work/prof2.sh`) (EXTRA = the build's switches; `CPU=S` for the secondary): the picture build
  profiled.
