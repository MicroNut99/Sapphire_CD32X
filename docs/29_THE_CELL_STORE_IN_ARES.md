# 29 - The cell store in ares: exact, and not yet faster (8 Nov 2026)

docs/28 checked the FPGA's cell store on the PC. This round runs it in ares on stage 3's FIFO playback
(`FPGA_CELLS=1`), with a test model of the store on the EverDrive PRO board.

## The model (`fpga/ares/0002-ares-sapphire-cells.patch`, ares_android branch `sapphire-ac-engine`, local)

`ARES_CELLS=<the FIFO's bytes> ARES_CELLS_VRAM=<vdc_start.bin> ARES_CELLS_LEAD=<frames>`; off when unset.

- **Stores:** live cells at cart `0x170000`, frozen ones at `0x194000` (288 bytes a cell).
- **Registers:** through the Arcade Card engine's window (docs/10). `ac_engine.h` leaves `0xc6`-`0xcb` free.

| Register | Access | What it does |
|---|---|---|
| `0xc6` | write | Group: which 16 cells `0xc7` reports. |
| `0xc7` | read | That group's frozen bits. |
| `0xc8` | write | Release: clear the frozen cells; lock at the next boundary. |
| `0xc9` | read | The boundary the lock came at. `0xffff`: not locked yet. |
| `0xca` | write | The SH2's frame count, low 8 bits. |
| `0xcb` | write | The unlock: `0xa5`, then `0x5a` at once. |

## Two things the first run showed

1. **Something reads the whole engine window at start-up.** It reads `0x1E0000`-`0x1FFFFF` once, before the FIFO
   is loaded. Every register of the store took those reads as writes: 256 releases and 256 frame counts, which
   moved the store 256 frames along.
   - The store now answers nothing until the unlock pair, and it restarts from `vdc_start` there.
   - A plain read across the window can't produce the pair. The RTL needs the same gate.
   - Whatever reads the window is still to be found. The Arcade Card engine has the same exposure.
2. **A store ahead of the SH2 costs a frame or two a picture.** The FPGA runs its own cursor up to the lead. So a
   release at frame k locked at k + 1 + lead, and the picture waited for it: **143 pictures** in 600 frames, against
   262 without the store.

## The fix: the store follows the SH2's count

The store converts from the same FIFO entries the SH2 applies, and only up to the boundary the SH2 has passed
(register `0xca`, written at each frame's end).

- At a frame's end the SH2 first releases (when its picture before is on screen), then writes its count. The
  store locks at that boundary, the frame just applied, and the SH2 draws it at once. That is when the build
  without the store draws.
- **In the FPGA:** the converter gets a second read pointer into the FIFO's ring, gated by the count. A ring entry
  is free once both pointers have passed it.
- **The race docs/28 guarded against is gone in this build.** The store only moves when the master writes `0xca`,
  and the master draws the sprites itself here (`SEC=2`: the secondary does the background). Nothing changes under
  a draw.
  - A build where the secondary draws while the master runs on (`SPR_LATE`) still needs docs/28's
    read-then-recheck.

## Measured (stage 3, FIFO, `SEC=2 VS_SEC=1 VSSWAP=1`, ares `ARES_32X_TIMING=console`)

| Build | Pictures / 600 | Refreshes a picture (2 / 3 / 4) | Screenshots exact (23: frames 4040, 4063 ... 4546) |
|---|---|---|---|
| `KEEP=1` (docs/27) | 262 | 192 / 58 / 10 | 13 |
| `FPGA_CELLS=1` | **264** | 198 / 54 / 10 | **16** |

Where the screenshots are wrong:
- **The last two (4523, 4546):** past the playback's end, as before (docs/26).
- **`KEEP=1` only (4109, 4293, 4500):** about 256 pixels each.
- **The boss's frames (4385-4477):** both builds.
  - The store build: 55-259 pixels in a few 16-pixel cells.
  - `KEEP=1`: 1-270 pixels at the same frames.
  - The shapes are right and the colours differ (the boss's hit flash), so a colour's timing is the likely cause,
    not the cells. Not yet found.

**The store is exact, but it doesn't speed the pictures up.** `KEEP=1` had already made the conversions cheap (93 % of
them streams). So the boss's pictures are no faster. The limit is the drawing and the list's timing (docs/27's
list: the sprites' clearing, the sprites' tests, the background's list on the master's path, line 196).

What the store does give:
- the SH2 converts and pokes nothing;
- the play build needs no `KEEP` region kept current by engine pokes.

## Next

1. docs/27's list toward a solid 30, starting with the sprites' clearing and the list split at line 196.
2. `cellstore.sv`: the store in RTL with the unlock gate and the count-following cursor, checked against
   `cellstore.c` in Verilator.
3. Find what reads the engine's window at start-up.
