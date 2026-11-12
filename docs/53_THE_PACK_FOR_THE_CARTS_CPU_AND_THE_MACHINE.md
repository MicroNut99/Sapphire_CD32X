# 53 - The pack for the cart's CPU, and the machine from power-on as a library (11-12 Nov 2026)

The owner: "Use these repos as examples on how to build out the game" (Gate-of-Thunder_CD32X, DraculaX_32X). Both
reached the console the same way: the machine from power-on as a C library with no translated code, proven against
Mednafen; that library as the cart's model in ares; the cart core proven against it; the whole game packed into the
`.32X`; a USB kit. This round takes the first of those steps for Sapphire and gives the cart's CPU side the pack.

## 1. The pack read by the cart's physical addresses

docs/52's pack is read by the SH2 through the 32X's windows. The CPU in the FPGA reads the cart's memory itself.

- **`tools/m3rt/sapk.c`:** a track sector from the pack by physical address: the directory at 0x440000, the piece,
  LZ4 with the 64 KB dictionary, the sector's sum.
- **`fpga/huc/pak_read.sv`:** the same in RTL (a byte a request to the cart's memory, the sector into a 2 KB buffer).
- **`fpga/huc/run_eqpak.sh <packed .32X> <track>`** (Verilator, the memory answering after 0-3 clocks):

| Check | Result |
|---|---|
| every sector of the track (7,599), RTL = C = the track | **0 differ** |
| 5,000 spoiled packs (a byte changed in a piece, a table entry or a sum) | **0 differ**: 4,697 refused by both, 303 passed by both (the byte was as before) |
| a sector | about 10,100 clocks and 1,281 cart bytes |

- **The stand-in:** `HUC_SAPK=<the packed .32X>` reads its sectors with `sapk.c`.
  **The whole recorded game (`playL`, 74,999 frames from power-on): the same pictures and sound, entry for entry, from
  the pack as from the disc's track; 4,616 sectors read, 0 bad.**

## 2. The live build from the packed launcher

- **`tools/sa_pack.py --live`:** the launcher, the program (bank 8), the state, the directory and the track's pack. No
  blocks. The pack keeps clear of page 1 (the FIFO's and the sound's rings), page 5 (kept for the FPGA CPU's own
  memory) and page 6 (the card): 10 MB, ending in page 4.
- **`tools/m3_live_pack.sh`:** builds it (`bc5`, a copy of `bc4`) and runs ares; the disc's files are the stack's own
  (`CDLOADER.BIN GAME.32X IMAGE.RAW LEVEL00.BIN`) and the music.
- **Run once** (`lpk`, power-on, the `playL` recording): the program starts from the launcher and the stand-in finds the
  pack's directory. **The pictures of that run have not been compared yet.**

## 3. The machine from power-on, with no translated code

Gate of Thunder docs/35, Dracula X docs/98-99. Sapphire's stand-in (docs/40) runs translated blocks from a state; the
examples' ares model is the plain machine, which is also what the cart core is proven against.

- **`tools/m2_machine.py`** (Gate of Thunder's): the interpreter alone, built with Sapphire's exact host
  (`trace_main.c` + the Arcade Card): `work/m2mach`. `M2_EXE=<it> tools/m2_run.py`.
- **`tools/m2host/sapm.c` / `sapm.h` / `sapm_pc.c`:** Dracula X's `dxm.c` with the Arcade Card (its registers, banks
  40-43, its 2 MB in the frame's hash). Power-on is in code: only the System Card, the disc's table and track files,
  and the pads go in. `RUN_POKES=<file>` gives the recording's RAM writes.
- Dracula X's observers came with it: `vdc_vram_hook`, `vdc_scroll_hook`, the line table, `rt_disc_lba`.

| Run | Frames | Equal to Mednafen |
|---|---|---|
| `m2mach`, from the `pon` window, no pokes | 74,999 | 7,899 (the recording's first poke is at 7,900) |
| **`sapm_pc`, from power-on in code, with the pokes** | 75,000 | **74,656; the last frame equal** |

The 344 other frames come in short runs (20,142-20,143, 47,348 on, 68,282 on ...) and return to equal each time.
**Not looked into yet.** About 10 minutes for the 21 minutes of game.

## Next (the examples' order)

1. The 344 frames.
2. The cart's contract and the ares model: `sapm` in ares in place of the stand-in (Gate of Thunder docs/36-37).
3. The cart core against `sapm` (Sapphire's docs/20-23 engine, with `pak_read.sv`, `ac_engine.sv`).
