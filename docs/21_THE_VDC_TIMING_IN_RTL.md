# 21 - The VDC's timing in RTL (8 Nov 2026)

docs/20's next step. The CPU in the cart needs the video chips' timing: the status bits, the IRQ line, the times the
VDC holds a data-port access, the sprite table's copy. The game's own RAM and code don't depend on VRAM's contents
(it never reads VRAM, docs/18), so the FPGA keeps no VRAM: the VRAM writes leave for the SH2.

## `fpga/huc/vdc_timing.sv`

M2's C model, written from Mednafen's behaviour and checked against its trace (tools/m2host `vdc_timing.c`,
`vdc_model.c`), line for line in SystemVerilog, one master clock (21.48 MHz) a step:

- **The VCE:** a line of 1,365 clocks, frames of 263 lines.
- **The VDC:** its own clock (4, 3 or 2 master clocks: the dot clock); the horizontal phases with their register
  caches; the vertical phases; the raster counter and RCR; the vblank; the sprite table's copy (1,024 ticks); the
  VRAM DMA's timing; the busy phase in each horizontal sync; the sprites fetched each display line (4 ticks
  each).
- **The data port:** the held read and write, committed when the VDC is not busy. A committed VRAM write goes out
  on `wr_vram` (address, word) for the SH2.
- **The sprite table:** the timing needs each entry's Y and size. They come from a shadow of the VRAM words at
  DVSSR, taken from the committed writes, and copied at the sprite table's DMA.

## Checked against the C model (`fpga/huc/eq_vdc.cpp`, `run_eq.sh`, Verilator)

Both start from a window's video state (`vdc_start.bin`: the registers and Mednafen's timing counters) and take
every VDC and VCE access of the window at its time on the trace's clock. They are compared at every access and
every line: the status, the held accesses, both phases and their counters, the raster counter, the sprite fetch,
the sprite table's DMA, the busy phase, the divider, the VCE's counters, the scanline, the burst mode.

| Window (600 frames) | Accesses | Lines | Differences |
|---|---|---|---|
| stage 1 | 685,333 | 157,795 | **0** |
| stage 3 | 901,240 | 157,617 | **0** |
| stage 5 | 664,886 | 157,757 | **0** |

13 seconds a window.

## Next

The CPU and this together, against Mednafen's trace with nothing replayed but the pad, the card and the CD:
- the interrupts from `irq`, not held from the trace;
- the status read from the RTL;
- the cycles with the waits (Mednafen's `DoWaitStates`: the CPU stalled in steps to the chips' next event, as
  trace_main.c's `wait_states_timed`).

That needs the CPU in Verilator too: GHDL's `--synth --out=verilog` of the core (as docs/20's size estimate),
checked against the trace in Verilator first.
