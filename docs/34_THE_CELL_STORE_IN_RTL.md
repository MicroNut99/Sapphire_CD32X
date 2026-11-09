# 34 - The cell store in RTL, equal to its C model (8 Nov 2026)

docs/28-31 built the FPGA's cell store as a C model (`fpga/huc/cellstore.c`) and an ares board model. This round
writes it in SystemVerilog and checks it against the C model in Verilator, as docs/21 did for the VDC's timing.

## `fpga/huc/cellstore.sv`

- **Its inputs:**
  - the FIFO's entries for the store's own cursor (`ent_valid` / `ent_ready`). They are taken only while the
    boundaries passed are below the SH2's count, as in docs/29.
  - the SH2's register writes (`0xc6` group, `0xc8` release, `0xca` count, `0xcb` unlock);
  - VRAM loaded before the unlock.
- **Its outputs:**
  - the register reads: `0xc7` a group's frozen bits, `0xc9` the lock's boundary;
  - a read port for the three windows the SH2 reads, with the word a cycle later. Live cells are at `0x170000` and
    frozen cells at `0x194000`. The tiles' view is at `0x1B8000`, where the port picks the frozen long itself.
- **A VRAM word that changes:**
  1. First the frozen copies, if the store is locked and the cell or tile has none yet. A cell copies 288 bytes,
     one a cycle; a tile copies one long.
  2. Then the cell's row: its four planes are read, then 8 + 9 bytes and the rows' mask are written.
  3. Then the tile's hash (FNV-1a, a multiply a cycle) and its colour mask, over its 16 words.
  - About 45 cycles a word, plus 288 when a cell is first frozen.
- **Register writes:** the group and the unlock's first half are taken at once. The release, the count and the
  unlock are held one deep and taken between entries, as the C model takes them. The test writes the count while
  the release is still clearing, so the hold is exercised every drawn frame.
- **The release and the lock:** each clears the 512 + 2,048 frozen bits, 2,048 cycles. The unlock also rebuilds
  every cell and tile from VRAM, about 8,192 rows.
- **The memories:**
  - VRAM's copy: 64 KB;
  - the live cells and the frozen cells: 144 KB each;
  - the tiles' live and frozen longs: 8 KB each.
  - These are plain arrays here, to map onto the cart's second PSRAM next.

## Checked (`fpga/huc/run_eqcells.sh <fifo> <vdc_start> [frames] [every]`, `eq_cells.cpp`)

The test drives both models with saspeed's `fifo_play` protocol:
1. the unlock;
2. then at each frame's end: a release every `every` frames, the count, and the store run up to it;
3. at each lock, the lock's boundary (`0xc9`) is checked. Then every cell's view (512 × 288 bytes) and every
   tile's view (2,048 longs) are compared with `cs_cell` and `cs_tile`. 64 cells' words and tiles' longs are also
   read through the port.

| Stage 3's FIFO, 600 frames | Locks compared | Cycles | Cells frozen (C) | Result |
|---|---|---|---|---|
| a release every 2 frames | 300 | 7.76 M | 1,877 | **equal** |
| every 3 | 200 | 7.41 M | 2,123 | **equal** |
| every frame | 600 | 8.39 M | 0 | **equal** |

About 13,000 cycles a frame: 0.26 ms at 50 MHz. That leaves room for the PSRAM's slower accesses.

## Not done yet

- **The PSRAM:** the arrays on the cart's second PSRAM, with its controller and the arbiter shared with the SH2's
  reads (docs/20's rom1 arbiter).
- **The engine window's decode:** the register writes come from reads at `0x1C0000 + 0x20000 + v << 9 + r << 1`
  (docs/10). Then the HuC6280 core's VDC feeds the cursor, in place of the recorded FIFO.
