# 28 - The FPGA keeps the sprite cells converted, exactly (8 Nov 2026)

docs/27 found the heavy pictures (the stage 3 boss: 36 cells converted a picture, sprites 1.1-1.7 refreshes) are what
stop a solid 30. The owner chose (option 2): the FPGA converts the cells, with the pictures kept exact.

## The design (`fpga/huc/cellstore.c`: the C model, the RTL's reference)

- The FPGA makes every VRAM write, so it keeps its own copy of VRAM and every cell's 32X form (rt_spr.c's
  `convert`: 16 rows of 8 nibble bytes, the odd x's rows of 9, the rows' mask). Each write updates its cell's row.
  The SH2 converts nothing.
- **Exact:** the SH2 draws frame k while the game is a frame or two on. So the store is copy-on-write from a
  frame boundary:
  - `cs_release` (the SH2: its picture before is on screen) clears the frozen cells and arms the lock;
  - the next frame boundary (FIFO type 5) locks. From then on, a cell about to change is first copied to its
    frozen slot;
  - the SH2 reads a cell's frozen slot if it has one, else the live one. That is the cells as at the lock, which
    is the boundary of the frame it draws: the first frame's end after its release, the FPGA's own count.
- **In the FPGA:** VRAM's copy and both stores in the cart's second PSRAM (64 KB + 2 × 144 KB), the 512 frozen
  bits readable by the SH2.
- **The SH2's read:** read the cell, then its frozen bit. If the bit is set, read the frozen slot instead. A
  freeze always comes before the live cell's change, so a cell read while the bit was clear and still clear after
  is the lock's.

## Checked on the PC (rt_pc `-DSPR_FPGA_CELLS`, `RUN_FIFO_CS=1`)

Two cursors run on stage 3's FIFO:
- **The SH2's** applies everything and draws every 2nd (or 3rd) frame.
- **The FPGA's** applies only the VRAM words and boundaries to `cellstore.c`. It is released just before the drawn
  frame's boundary and runs on to just before the next release: the most it can be ahead, 1 (or 2) frames.

The pictures are compared with the same playback drawn with cells converted from the SH2's VRAM.

| Drawn every | Pictures | Cells frozen | Pictures equal |
|---|---|---|---|
| 2 frames | 300 | 1,877 | **all, byte for byte** |
| 3 frames | 200 | 2,123 | **all, byte for byte** |

About 6-11 cells frozen a picture: 3.5 KB of copying for the FPGA, against 117,627 row updates in 600 frames.

## Next

1. `cellstore.sv`: the RTL, against `cellstore.c` in Verilator (the PSRAM as a model).
2. The SH2's read path (the frozen bits through the engine's window, the cells from a bank of the second PSRAM in
   the SH2's cart window) and the release.
3. In ares: the FIFO read through a port and the cell store in the EverDrive board's model, so the playback
   measures the SH2 with no conversion at all.
