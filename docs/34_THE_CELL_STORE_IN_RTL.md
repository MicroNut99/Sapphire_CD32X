# 34 - The cell store in RTL, on a memory port, equal to its C model (8 Nov 2026)

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
- **A VRAM word written:** it is read first (unchanged: nothing more). Then, if it changed:
  1. First the frozen copies, if the store is locked and the cell or tile has none yet. A cell copies 288 bytes,
     one a cycle; a tile copies one long.
  2. Then the cell's row: its four planes are read, then 8 + 9 bytes and the rows' mask are written.
  3. Then the tile's hash (FNV-1a, a multiply a cycle) and its colour mask, over its 16 words.
  - About 45 memory accesses a word, plus 288 when a cell is first frozen.
- **Register writes:** the group and the unlock's first half are taken at once. The release, the count and the
  unlock are held one deep and taken between entries, as the C model takes them. The test writes the count while
  the release is still clearing, so the hold is exercised every drawn frame.
- **The release and the lock:** each clears the 512 + 2,048 frozen bits, 2,048 cycles. The unlock also rebuilds
  every cell and tile from VRAM, about 8,192 rows.
- **The memory, on one 16-bit port** (`mreq` / `mack`: any latency; byte enables; the cart's second PSRAM). In
  word addresses:

  | Words | What |
  |---|---|
  | `0x00000` | VRAM's copy (64 KB) |
  | `0x08000` | the live cells (144 KB) |
  | `0x1A000` | the frozen cells (144 KB) |
  | `0x2C000` | the tiles' live longs |
  | `0x2D000` | the tiles' frozen longs, to `0x2E000` (376 KB in all) |

  - The frozen bits (512 + 2,048) are flip-flops.
  - The SH2's reads go first on the port, between the store's accesses. The tiles' window chooses the frozen long
    or the live one by the tile's bit.
  - A first version kept plain arrays. It was equal to the C model too, and the memory version replaced it.

## Checked (`fpga/huc/run_eqcells.sh <fifo> <vdc_start> [frames] [every]`, `eq_cells.cpp`)

The memory is modelled with a random latency of 1 to `RUN_LAT` cycles an access. The test drives both models with
saspeed's `fifo_play` protocol:
1. the unlock;
2. then at each frame's end: a release every `every` frames, the count, and the store run up to it;
3. at each lock, the lock's boundary (`0xc9`) is checked. Then every cell's view (512 × 288 bytes) and every
   tile's view (2,048 longs) are compared with `cs_cell` and `cs_tile`, from the memory. 64 cells' words and 64
   tiles' longs are also read through the SH2's port.

| Stage 3's FIFO, 600 frames | Memory latency | Locks compared | Cycles | Result |
|---|---|---|---|---|
| a release every 2 frames | 1 | 300 | 17.7 M | **equal** |
| every 3 | 1 | 200 | 17.4 M | **equal** |
| every 2 | 1-6 | 300 | 37.4 M | **equal** |
| every 3 | 1-6 | 200 | 37.2 M | **equal** |
| every 2 | 1-12 | 300 | 61.1 M | **equal** |
| every 3 | 1-12 | 200 | 61.1 M | **equal** |

The cycles include the test's own reads through the port (192 a lock) and the rebuild. With a 1-12 cycle memory,
the store takes about 100,000 cycles a frame: 2 ms at 50 MHz, against the frame's 16.7.

## Not done yet

- **The PSRAM and its arbiter.** krikzz's `mem_ctrl` (`lib_base/var.sv`) drives each PSRAM chip
  combinationally from the console's bus, and his DMA takes the chip over by `dma_req`. There is no arbiter: the
  memory is asynchronous (`ce = oe | we`), and its timing comes from whoever drives it.
  - The store would live in `rom1` (8-16 MB). The console reads that chip too: pages 4-6 are stages 3-5, in
    window 1, and the Arcade Card's regions for those stages are read through the engine.
  - **So the store's accesses must fit between the console's.** A store access is about 4 clocks at 50 MHz
    (70 ns PSRAM). A console access starting during one has to wait for it to end: a sync of about 60 ns, the rest
    of the store's access, then the PSRAM's 70 ns.
  - That adds up to about 210 ns on a path that is combinational today. Whether the SH2's cart read (9 clocks of
    23 MHz, about 390 ns) still gets its word in time is docs/10's to-do 2: measured on the console, not in ares.
  - A store write must never be cut short: the address would change under WE.
  - The same arbiter is needed for the HuC6280 core's reads (docs/20) and the engine. One design should serve all
    of them, measured on the console first.
- **The engine window's decode:** the register writes come from reads at `0x1C0000 + 0x20000 + v << 9 + r << 1`
  (docs/10). Then the HuC6280 core's VDC feeds the cursor, in place of the recorded FIFO.
