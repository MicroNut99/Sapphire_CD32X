# 64 - The combined core in Gate of Thunder's bench (13 Nov 2026)

docs/62's step 3: the core `make_core.py` writes (Gate of Thunder's core + the Arcade Card, the sector port, the
backup RAM on the battery RAM chip, docs/63's ports) run in Gate of Thunder's own bench against Sapphire's PC host.

## 1. The bench

`fpga/gotcore/make_bench.py <GoT checkout> <out>` writes `scart.cpp`, an edited copy of GoT's `tools/fpga/cart.cpp`
(its `CART_SRAM` form: the engine on the cart's memories, the sram chip model, the PSRAM's timing); `run_bench.sh <GoT>
<DraculaX_32X> <tg16> <build dir>` makes the core's files (`make_core.py`), the bench, GoT's GHDL-made MiSTer CPU,
Dracula X's `dx_vdc` / `dx_cd` / `dx_rv` / `dx_io`, and builds `scart` with GoT's C models (the drive, the CD unit, the VDC).
`scart <window> --run <frames>` with `RUN_DISC=<sapphire.cue>`, `RUN_NOSTACK=1`. Edits:

- the PSRAM model holds the Arcade Card's 2 MB from the window's `ac_start.bin` (rom1 offset 0 = `AC_BASE`; the even
  byte the word's high half) and takes the core's writes; the card's registers (`sap_ac`) from its 19 words;
- the sectors come through the core's sector port, a byte a clock, from the drive model, whenever the core shows one
  is wanted (the SH2's job on the cart);
- a 2 KB model of the battery RAM chip;
- the frame hash as Sapphire's host makes it (`trace_main.c`: base, CD and Super RAM, the card's 2 MB, VRAM, SAT,
  colours), and the same without the stack page (`md5ns`; added to the host checker: `RUN_NOSTACK`, `work/m2check_ns`).

`fpga/gotcore/cmp_frames.py <host> <bench>` counts the equal frames.

## 2. Results (600 frames each, the windows Mednafen captured)

| Window | Card word reads | Frames equal (all / without the stack page) | Differing frames | Last frame |
|---|---|---|---|---|
| st3_600 (stage 3: the card's heaviest reader) | 496,302 | 302 / **586 of 601** | 15, each alone | equal |
| st1_600 (stage 1) | 321,553 | 296 / **527 of 601** | 74, at most 5 in a row | equal |
| ps2_600 | 303,355 | 312 / **572 of 601** | 29, each alone | equal |

- **The stack page** ($0100-$01FF: an interrupt's return address) differs in about half the frames: the frame's end
  falls one instruction apart from the host's, the same as Gate of Thunder's bench (its docs/21: "the vblank
  interrupt one instruction apart in the wait loop").
- **Outside it,** the differing frames come back equal by themselves (stage 3's frame 19: base RAM $0363 alone);
  every window ends equal. A wrong card byte would carry on into the frames after it (the game copies the card's data
  into VRAM and RAM), so **the card's reads through the core are right**: 1.12 million card word reads, 0 bytes off at
  any window's end. Gate of Thunder's own count of such frames: 48 of 9,600 (docs/21), later 6 in 11,100 (docs/31) -
  Sapphire's are more (about 3 % here, 12 % in stage 1): not studied yet.
- The card flags (a write lost, a use the lean card leaves out) stay 0.


## 3. A fault found: the card's writes (fixed)

`p1_8100` (8,192 card writes) differed from its first frame: RAM and VRAM equal, the card's 2 MB not (`RUN_ACDUMP=<file>`
with `RUN_DUMP=<frame>`, host and bench: the card's RAM after a frame). Only 40 of the frame's writes reached the PSRAM,
and the core's own flag said "a card write lost". `make_core.py`'s write took its timing from the age of the PSRAM
address - but between accesses that address is already the card's current byte, the very one written, so the age did
not start again and the write ended without its WE. The write now has its own count from the clock it owns the chip
(`wc`: address and byte, WE clocks 1-4, done at 6). After it: all 8,192 writes in, the card's 2 MB equal to the host's
after frame 0.
