# 19 - Kept cells, and the splits in step (7 Nov 2026)

docs/18's plan, built: on the 14 MB cart every converted sprite cell is kept in the cart. Checking it in ares
found why the status bar's band sometimes went wrong; that is fixed too.

## 1. `KEEP=1`: every cell in the cart (`tools/m3rt/rt_spr.c`, `SPR_KEEP`)

- 512 cells × 288 bytes at cart `0x170000-0x193FFF` (page 0, past LEVEL01's resident slot; `KEEP_AT`). A cell's
  place is its number. `kt[512][4]` (4 KB of SDRAM) holds the marks it was converted under.
- A cell whose marks match is drawn straight from the cart (a cached read). Otherwise it is converted, drawn from the
  conversion, and written to its place by the engine's poke (`rt_ace_poke_run`: the address once, then a read a
  byte; the master's 18 cache lines purged).
- **The PC:** stage 3 converts 5.35 cells a frame instead of 55.5 (stage 1: 4.0, stage 5: 3.2). The sprites have
  0 wrong pixels against the layer drawn straight from VRAM on stages 1, 3 and 5.
- **ares:** a check build converted every cell anyway and compared the cart's copy, cached and past the cache.
  None differed.
- **Speed (stage 3):** the game frame shown at ares frame 4050 is 239 (224 before). The profile loses `convert`
  (21 %), and the cache's search is gone from `spr_frame` (19 % to 10 %).

## 2. The status bar's band went wrong: the list masks the raster interrupt

With the picture faster, half the screenshots were wrong by about 5,000 pixels. Matching each band separately
against the PC's pictures showed the cause. Either the status bar showed the plane below its own rows, or the whole
playfield had the bar's V offset. The pictures themselves were right.

Command 79 runs the picture's list on the 68000 with level 4 masked (BootCamp docs/03: a split due then lands
late). The list went out as soon as the master finished, so it often ran across line 203 (the bar's split) or line
224 (entry 0 put back), and the split was missed for that refresh.

**`VS_SEC=1`:**
- The master no longer sends the list. It marks it ready, and `pace_vbi` sends it at the next blank's start. The
  68000 then runs it in the blank and the display's first lines, well before line 203. An average list is 452
  words, about 2.8 ms.
- The secondary sends command 78 once the list has run, outside a blank. The splits' switch is then at the vblank
  of the 32X's swap, which the list's end decides.
- That takes command 78 (6.3 %) off the master.

| Stage 3, ares frames 3400-4300, every 100th | Wrong pixels |
|---|---|
| `SEC=2 KEEP=1` | 0 at 5 of 10; five near 5,000 (the band) |
| `SEC=2 KEEP=1 VS_SEC=1` | 0 at 7; 29, 256 (line 203 a line late); the first picture (the plane not yet filled) |

A list long enough to reach line 203 (2,000 words; the most seen is 2,720) still lands the split late for one
refresh. The stack's command 79 could switch a pending V-split list at its own swap, but that would be a change to
the stack and needs the owner.

## 3. Where the master's time is now (`SEC=2 KEEP=1 VS_SEC=1`, ares frames 3950-4550)

| Function | Share |
|---|---|
| the hot code | 19.0 % |
| `spr_frame` | 9.4 % |
| `draw_hook` (waiting for the swap) | 9.1 % |
| `spr_clear` | 7.6 % |
| `spr_rows*` | 12.5 % |
| the cold code (cart) | 20.6 % |

Stage 3 runs at about 24 game frames a second in ares. The game's own code with the runtime takes about one
refresh a frame (docs/15). The picture is now about 1.3 more. Next for speed: the sprites' clearing (only what
changed), drawing the cells on the secondary now that the master no longer converts (docs/17's `LATE`), and the
waiting.

## Builds

`EXTRA="SEC=2 KEEP=1 VS_SEC=1" tools/m3_piccheck_ares.sh st3 <split> <tag> <frames...>`;
`tools/m3_picprof.sh` the same way. `RTX=-DKEEP_TEST`: the cart's copies checked (COMM6 = cached << 8 | uncached
differences).
