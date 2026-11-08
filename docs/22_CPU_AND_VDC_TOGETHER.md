# 22 - The CPU and the VDC together (8 Nov 2026)

docs/21's next step: MiSTer's HuC6280 core and `vdc_timing.sv` in one simulation against Mednafen's trace, with the
VDC's status reads and the IRQ1 line from the RTL and Mednafen's waits.

## Tools (`fpga/huc/`)

- **`run_vtb.sh`, `vtb_huc.cpp`:** the core in Verilator (GHDL's `--synth --out=verilog` of `patch_dbg.py`'s copy),
  with `tb_huc.vhd`'s checks and the same `tb_host.c`. The same result as GHDL (stage 3, 1,000,000 instructions:
  0 differences, the same 195 cycle differences), about 10 times faster.
- **`run_vsys.sh`, `vsys.cpp`:** the core and `vdc_timing.sv` (Verilator, two models in one program):
  - the video chips start from the window's video state, run on by the C model to the first instruction;
  - every VDC and VCE access of the core goes to the RTL at its master clock, and the status read is the RTL's;
  - IRQ1 is the RTL's line;
  - a data-port access while the VDC holds one stalls the core as Mednafen's `DoWaitStates` does: steps of the
    chips' next event times the CPU's clock, 455 × 64 at most a run slice.
  Everything else is still answered from the trace (memory, the other hardware, IRQ2).

## Result: exact up to the first VDC wait, then Mednafen's waits and the core's bus part

Stage 3 runs 22,304 instructions in step: every register, access and interrupt is the same. Then a vblank
interrupt comes later for the core than for Mednafen, and the two part.

The cause is the cycles, from the first block move into the VDC (instruction 1,016, `PC 34D2`, a `TIA` of about
55,900 master clocks): the core with the RTL's waits takes 55,482, Mednafen 55,902. The same holds for the stores
to the data port at `PC 73DD-73E9`: the stall falls on the next store for the core.

**Why:** Mednafen's waits are its own: it runs a block move with its own access times (`14 + 6 × (bytes + 1)` cycles
into the instruction) and stalls the CPU in steps to the chips' next event. The core makes its bus cycles where
the HuC6280 makes them. A store that lands a cycle earlier or later against the VDC's 16-tick busy phase in each
line is held, or not. The VDC's model is exact (docs/21), and the CPU is exact in what it does (docs/20). Where
the core's bus cycles and Mednafen's notion of them disagree, the waits differ, and the core's are the real
chip's.

**So the CPU in the cart cannot be Mednafen cycle for cycle through the VDC's waits.** That is not a fault of either
model: Mednafen approximates the bus there. What can and must hold is the game: the RAM, VRAM, sprite table and
colours at the end of every frame equal Mednafen's. The game runs its frame once a vblank and waits in its idle
loop (`PC 445A-445C`), so a few hundred clocks of difference inside a frame change nothing unless a frame runs
long.

## Next: frame by frame, everything from models

The test becomes M2's `--run` (trace_main.c standalone) with the RTL: from the window's state, the pad from its
recorded masks, the CD unit and the IRQ controller from `io_model.c`, the card from `ac_model.c`, the colours and
VRAM kept from the RTL's writes. Then the RAM, VRAM, sprite table and colours' md5 at each frame against
Mednafen's (`work/m0_st*.log`, as `tools/m0_replay.py` records them).
