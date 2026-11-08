# 31 - The FPGA keeps the tiles' hash and colours: 320 pictures (8 Nov 2026)

docs/30 left the heavy background lists spending 120 lines a picture on the tiles' hashes and colour masks. The
game streams new tiles in, and every tile whose VRAM tag changed was hashed (16 multiplies) and masked again.

## The design

- **What the FPGA keeps:** it already keeps VRAM's copy and the sprite cells (docs/28-29). Now it also keeps, for
  each of the 2,048 tiles of 8 × 8, the long `hash << 16 | colour mask`, exactly as rt_bg2.c's `tile_hash` and
  `tile_mask` make them.
- **Frozen like the cells:** after the lock, a tile about to change has its value copied first. The release and the
  lock clear the copies.
- **Where the SH2 reads it:** one long a tile at cart `0x1B8000 + 4 t` (8 KB, in page 0's free part). The FPGA itself
  picks the frozen value or the live one, so there is no bit to read first.
- **No race in this build:** the secondary reads the tiles while the master draws the sprites. The master writes no
  frame count until the list is made, and the store only moves on that count (docs/29).

## The code

| What | Where |
|---|---|
| The C model | `fpga/huc/cellstore.c`: `cs_tile(t)`; `tinfo` is `tile_hash` and `tile_mask` in one |
| rt_bg2's use of it | `-DBG2_FPGA_TILES`: `bg2_fpga_tile(t)` instead of `tile_hash` and `tile_mask_of`. `tile_mask` split into `tile_mask_of` and `tile_mask_set` |
| The SH2 build | saspeed `FPGA_TILES=1` (with `FPGA_CELLS=1`): a 32-bit read past the cache |
| The ares model | `fpga/ares/0002-ares-sapphire-cells.patch`, now 2 commits: the tiles' window at `0x1B8000` |

## Checked on the PC

rt_pc ran `RUN_FIFO_CS=1` on stage 3's FIFO in two builds: the cells from the store, with and without
`BG2_FPGA_TILES`.

- **Drawn every 2nd frame:** 300 pictures, the two pictures' dumps identical byte for byte.
- **Drawn every 3rd frame:** 200 pictures.
- **`RUN_BG2_TILE_CHECK=1`:** each value from the store is compared with the hash and the mask made from the SH2's
  VRAM. No difference in either run.

## Measured in ares (stage 3, FIFO, `SEC=2 VS_SEC=1 VSSWAP=1 FPGA_CELLS=1 FPGA_TILES=1`)

| | Pictures / 600 | Refreshes (1 / 2 / 3 / 4) | Heavy list (lines) | Light list (lines) |
|---|---|---|---|---|
| docs/30 | 311 | 70 / 205 / 25 / 11 | 269 | 110 |
| the tiles from the FPGA | **320** | 76 / 214 / 25 / 5 | **206** | 99 |

The heavy list's tiles step went from 120 lines to 58.

## Where the 30 slow pictures are (the ring at `0x0603E940`)

1. **The stage's first 20 pictures:** lists of 1,500-2,800 words, made 360-550 lines after the picture's start.
   The window is filled and many tiles are converted. The background is the slow side.
2. **The boss, pictures 260-312:** the sprites take 230-295 lines and the lists are made at 300-340. Both sides are
   near the limit, and the sprites are the longer.

## Next

- **The sprites (the boss):** docs/27's clearing and tests.
- **The stage's start:** the conversions of a full window (step 4: `convert` and the slots).
