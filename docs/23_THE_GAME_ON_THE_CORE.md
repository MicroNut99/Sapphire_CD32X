# 23 - The game on the core: 600 frames from models (8 Nov 2026)

docs/22's next step. MiSTer's HuC6280 core and `vdc_timing.sv` run each window from its start with nothing taken
from Mednafen's trace but the starting state and the recorded pad. The test is the game's state, not the cycles.

## `fpga/huc/sys_host.c`, `RUN=1 run_vsys.sh <window> [frames]`

The rest of the PC Engine around the RTL, from our own models (tools/m2host):

- memory from the window's start, the System Card;
- the Arcade Card (`ac_model.c`);
- the pad (`inp.bin`, a frame), the interrupt controller and the CD unit (`io_model.c`), and IRQ2 from it;
- the VCE's colours (`vdc_model.c`);
- VRAM from the RTL's committed writes, and the sprite table copied from it when the RTL starts its copy.

IRQ1 is the RTL's line, masked by the interrupt controller. The status reads are the RTL's, and so are Mednafen's
waits (docs/22).

At each frame's end (the next instruction after Mednafen's last line, as trace_main.c takes it) the run prints
trace_main.c's hashes. At the window's end it compares RAM, the card's RAM, VRAM, the sprite table and the colours
with Mednafen's (`end.bin`, `ac_end.bin`, `vdc_end.bin`).

## Result

| Window (600 frames, about 3.5 minutes each) | Against Mednafen's end |
|---|---|
| stage 1 | **all equal** but 6 bytes of the stack page (`0x21E9-`) |
| stage 3 | **all equal** but 3 bytes of the stack page (`0x21F2-`) |
| stage 5 | **all equal**, every byte |

The stack bytes are what an interrupt leaves below the stack: where in the idle loop (`445A` or `445C`) the vblank
interrupt found the game, which the waits move (docs/22). The game reads none of them.

Frame by frame (trace_main.c `--run`'s hash without the stack page): stage 3 has 575 of 600 frames equal, and stage
5 has 458. The others come back equal at the next frame. They are a frame's end that a write from the next frame
reaches before the hash or after it, so the state is the same, only seen at another moment.

Two of the harness's own faults were found on the way. The sprite table's copy was taken at a count it never shows
(the copy's first tick already counts down). The VCE's colour address was not loaded from the state, so stage 5's
colour writes went to colour 0.

## What this means

**The PC Engine's CPU in the cart runs Sapphire as Mednafen does**, for 1,800 frames of three stages. That holds
with the video chips' timing in RTL, and with every interrupt and status read made by the hardware model, not
replayed. The 14 MB cart's way to the owner's 30 frames a second (docs/20) stands on it.

## Next

1. The FIFO: what the SH2 needs from the RTL each frame (the VRAM writes, the registers by line, the colours),
   and the SH2's side: the picture build fed from it (first on the PC: rt_pc reading the FIFO's log).
2. The hardware page and memory as FPGA logic (the timer and interrupt controller, the pad register, the colours'
   RAM, the card's ports beside the CPU, `rom1`), and the CD unit's mailbox to the SH2.
3. Quartus: the core and `vdc_timing.sv` in the EverDrive's FPGA (its size; `sprites_on`'s 64-entry loop made
   sequential over the line).
