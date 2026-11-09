# 40 - The game live in ares: the cart CPU's stand-in (9 Nov 2026)

Until now, the fast build (the HuC6280 in the cart's FPGA, docs/20-35) could only be shown in ares by playing back a
FIFO recorded on the PC. That was one window, with no pad. This round gives ares a stand-in for the FPGA's CPU. The
game runs live from power-on, with the pad, through the SH2's own FIFO player.

## 1. The FIFO from the runtime's pictures (`tools/m3rt/fifo_synth.c`)

The FPGA writes docs/24's entries as the CPU writes the VDC and VCE. The runtime instead has each picture's state at
the display start. `fifo_synth` turns that state into entries:
- the colours and VRAM words that changed;
- the sprite table, written into VRAM at DVSSR, copied by a type 9, then VRAM put back;
- each raster split, opened at its line with its registers;
- the current registers;
- the display start.

**`rt_pc`:** `RUN_FIFO_OUT=<file>` writes the entries. `RUN_FIFO=<file> RUN_V32HASH=1` plays them back.

**Checked:**
- Stage 1's window: 600 of 600 picture hashes are equal.
- From power-on: 2,000 of 2,000 are equal.

## 2. The stand-in (`tools/m3rt/huchost.c`, `tools/m3_huchost.sh`)

**What it is:** the runtime's C as a shared object, with:
- the interpreter and `m3gen3`'s translated blocks;
- the VDC timing, the CD unit and drive (from the state's table), the Arcade Card engine.

**Its interface:** `huchost_init(state, track)`, then `huchost_frame(pad, &entries)` runs one PC Engine frame and
returns the FIFO entries it made. The data track's sectors come from its image. On the real cart, the SH2 reads them
for the FPGA.

**Speed:** 2,000 frames take 0.3 s on the PC.

**The interpreter alone is not equal to the blocks.** From power-on, the game's state at frame 1,999 differs
(`7b3c292e` against `45a5bfed`), and so do the pictures from the first one. The blocks are what was checked against
Mednafen, so the stand-in links them. The interpreter's difference is still to be found. It also runs the code no
block covers in every build.

## 3. ares (`fpga/ares/0005-ares-huchost.patch`, branch `sapphire-ac-engine`)

**The board:** with `ARES_HUCHOST=<.so> ARES_HUC_STATE=<state> ARES_HUC_TRACK=<image>`, the EverDrive board:
- runs the game a picture at a time;
- writes the entries into a ring at cart `0x200000-0x3EFFFF`;
- stays up to `ARES_HUC_LEAD` (4) pictures past the count the SH2 writes to register `0xca`.

**The cell store model** reads the same ring.

**The pad** comes from the SH2 through registers `0xcd` and `0xce`, as it would go to the FPGA's pad register.

**The SH2 (`FIFO_LIVE=1`, with `FIFO=<any file> SPLAY=1`):**
- `fifo_play` reads the ring past the cache and wraps at `0x3F0000`;
- the draw hook sends the pad.

**The build and run:** `tools/m3_live_run.sh <state> <window> <tag> [frames]`, with the fast build's flags
(`SEC=2 VS_SEC=1 VSSWAP=1 FPGA_CELLS=1 FPGA_TILES=1`).

## 4. Played from power-on (`sp/pon.bin`, ares with `--press`)

Presses of RUN (A), then shots (B, C) and moves:

| ares frame | What |
|---|---|
| 3,500 | NOW LOADING |
| 5,000-12,500 | the title (START / CONFIG), then the player select |
| 17,000 | a briefing |
| 20,000-30,000 | stages played: CONTINUE, both players' scores, the later stages' scenery |

It all comes at the game's own rate: the SH2 player paces the PC Engine's frames by its clock.

**Seen:**
- The title's background is a brown tile pattern where the PC Engine shows black.
- The 2P score shows that a RUN press joined the second player.

## Next

1. The title's background against the PC's pictures.
2. Sound: the FIFO's PSG entries (type A), ADPCM and CD audio.
3. The interpreter's difference from the blocks.
4. The real FPGA: the CPU's memory and hardware page in RTL (docs/20 §3), and Quartus.
