# 20 - The CPU in the cart: the plan, and the core against Mednafen (7 Nov 2026)

The owner wants a solid 30 frames a second at the game's own speed, and chose the way that needs the HuC6280 in the
14 MB cart's FPGA (docs/10's second step). docs/15's speeds show why: the recompiled game alone takes 0.74-1.21
refreshes a frame on the SH2. Two game frames and one picture never fit in two refreshes on stage 1.

## 1. The idea: the PC Engine's CPU in the cart, the 32X as its video chip

The FPGA runs the game's own code at the PC Engine's own speed (7.16 MHz, its own clock), as the PC Engine does.
The SH2s run no game code: they turn what the CPU writes to the video chips into pictures. On the 14 MB cart
the recompiler and its runtime are then no longer in the picture's way. The 4 MB plain cart keeps them.

What the CPU touches, measured (rt_pc `-DVW_COUNT`, `tools/m3rt`, every access, 600 frames):

| A frame | Stage 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| VDC writes | 1,080 | 918 | 1,379 | 556 | 986 |
| VCE writes / reads | 46 / 15 | 91 / 30 | 91 / 30 | 80 / 27 | 90 / 30 |
| PSG writes | 30 | 39 | 46 | 35 | 3 |
| pad writes / reads | 12 / 10 | 12 / 10 | 12 / 10 | 12 / 10 | 12 / 10 |
| VDC reads (the status) | 2 | 2 | 2 | 2 | 2 |
| CD unit | 1.4 | 1.3 | 3.1 | 1.6 | 0.2 |
| Arcade Card reads | 326 | 241 | 379 | 7 | 233 |
| timer, IRQ controller | 0 | 0 | 0 | 0 | 0 |

The game never reads VRAM (docs/18). So the FPGA needs:

- **The CPU**, with its RAM: base RAM (8 KB) in the FPGA's block RAM; CD RAM (64 KB), Super RAM (192 KB) and the
  System Card in the cart's PSRAM, beside the Arcade Card's 2 MB (already the FPGA's, docs/10).
- **The hardware page:** the timer and the IRQ controller; the VCE's colour RAM (512 × 9 bits: the game reads it
  back); the pad's register, written by the SH2; the VDC's registers, status and timing. That means the PC
  Engine's line counter at its own clock, RCR, the vblank, the sprite table's DMA and the waits a VRAM access takes
  during the display: the CPU's cycles depend on them.
- **The way out:** every VDC, VCE and PSG write goes into a FIFO with the line it was made on. The SH2 reads it
  through the cart, one 16-bit read an entry (about 1,500 a frame), applies it to its VDC model (VRAM, registers,
  the raster splits, docs/16) and draws.
- **The CD unit** (a few accesses a frame): a mailbox to the SH2, which keeps today's drive model and the
  stack's CD reads. The CPU waits on it.

The PC Engine's frame is 59.82 Hz, the Genesis's 59.92: the SH2 shows the newest whole frame at each picture. One
frame is dropped about every ten seconds.

## 2. Step 1: the core against Mednafen

The core: MiSTer's TurboGrafx16 (GPL; Gregory Estrade's FPGAPCE, the HuC6280 rewritten for cycle accuracy by
srg320), `fpga/huc/fetch_core.sh` at commit `827d45f`, in `reference/tg16` (git-ignored). Only its CPU
(`HUC6280_CPU.vhd` and its microcode), unchanged.

**`fpga/huc/`:** `tb_huc.vhd` (GHDL, `ghdl-llvm`), `tb_host.c` (VHPIDIRECT), `patch_dbg.py` (the testbench's copy
of the core gets debug ports for its registers), `run_tb.sh <window> [instructions]`. The core starts from reset.
A stub then sets the window's first state (MPR0-7, S, P, A, X, Y, the speed) and jumps to its PC. From there:

- every memory read is answered from the window (RAM, the System Card), and every hardware and card access from
  Mednafen's log, each checked;
- every instruction's start is checked against Mednafen's trace: PC, A, X, Y, S, P, MPR0-7, and its cycles;
- the interrupts are held where Mednafen took them.

The CE is HUC6280.vhd's (a cycle every 6 or 24 clocks), and so is the wait a VDC or VCE access adds.

**Result, stage 3's window from its start:** 1,000,000 instructions (34 frames) with **0 differences in any
register, MPR or access**. 195 instructions take other cycles than Mednafen's. They are block moves into the
VDC (`TIA` from the card, `PC 73DD-73E9`: Mednafen 51-111, the core 15) and the long one at `PC 34D2`. Those are
the VDC's waits during the display, which the VDC's timing in the FPGA will give through RDY: Mednafen's log has
them (kinds 10-13). The whole 600 frames of stages 1, 3 and 5: §4.

## 3. Open

- **Fitting it in:** the EP4CE15 has 11,800 logic elements free (docs/10). An estimate of the CPU alone: GHDL's
  synthesis to Verilog, then Yosys to 4-input LUTs: **2,505 LUTs and 249 registers**. That includes its microcode
  ROM (2,048 × 42 bits), which Quartus would put in about 10 of the 56 M9K blocks instead. Quartus's own count is
  still to come (a `.bat`, as `build_sapphire.bat`).
- **The memory:** the cart has two PSRAM chips on their own buses (krikzz's `rom0`, 0-8 MB, and `rom1`, 8-16 MB:
  map_sapphire.sv already chooses between them). With the game's code in the FPGA, the SH2 no longer reads the
  stage's translated code from the cart. So `rom1` can hold the CPU's memory (the System Card, CD RAM, Super RAM,
  the card's 2 MB; bank 31 left out), and the console's reads go to `rom0`. The CPU's accesses (one every 140 ns
  at most) then wait only for the console's rare reads of `rom1`. To check in the simulation of krikzz's memory
  controller.
- **The licence:** MiSTer's core is GPL. Its files stay in `reference/` (fetched, not copied), the changes to them
  are patches, and the bitstream that contains them is under the GPL. The owner is to confirm.
- **The 4 MB plain cart:** no FPGA of ours, so it keeps the recompiler.

## 4. The whole windows

`run_tb.sh <window> 99999999` (about an hour a window with two at once on two cores):

| Window (600 frames) | Instructions | Register, MPR or access differences | Cycle differences |
|---|---|---|---|
| stage 1 | 17,775,072 | **0** | 2,616 |
| stage 3 | 17,426,743 | **0** | 2,356 |
| stage 5 | 17,760,369 | **0** | 2,773 |

**The core runs Sapphire's code exactly as Mednafen does.** Every hardware and card access is in the same order,
at the same instruction, with the same value. The cycle differences are the VDC's waits during the display (about
4 a frame), which the FPGA's VDC timing is to give.

## 5. ares

The owner's ares moved to v1.7 (ares_android `main` `bc3ffb9`: U-R-R's cart engine). The Arcade Card engine's two
commits are rebased onto it (local branch `sapphire-ac-engine`, `fpga/ares/0001-ares-arcade-card-engine.patch`
made again from it), and `ares-headless` is rebuilt. docs/19's best build (`SEC=2 KEEP=1 VS_SEC=1`) on it,
stage 3, ares frames 3400-4300 every 100th: **every screenshot equals its PC picture** except the stage's first
(the plane not yet filled).

## 6. Next

1. The VDC's timing in RTL (the line counter, RCR, the vblank, the sprite table's DMA, the waits during the
   display), checked against Mednafen's log (kinds 3-13) as the CPU was. With it the cycles must match too.
2. The hardware page around the CPU (timer, IRQ controller, VCE colours, pad), and the FIFO to the SH2.
3. The memory: `rom1`, its arbiter, and krikzz's controller in the simulation.
4. The SH2's side: the picture build fed from the FIFO instead of the recompiled game, first on the PC (rt_pc
   reading a FIFO the testbench writes), then in ares (the model of the whole FPGA in ares's EverDrive board, as the
   card's engine is).
