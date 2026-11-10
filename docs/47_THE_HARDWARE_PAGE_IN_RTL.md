# 47 - The CPU's memory, the hardware page, the PSG and the ADPCM in RTL (9 Nov 2026)

docs/23's next step 2: the parts of the PC Engine around the CPU in the cart, as FPGA logic, each checked against
the model that already matches Mednafen, or against Mednafen's own code.

## The new RTL (`fpga/huc/`)

| File | What it is | Checked against |
|---|---|---|
| `huc_sys.sv` | the CPU's 21-bit physical address into one flat memory layout (System Card, Super RAM, CD RAM, base RAM, backup RAM), the selects for the VDC, the CD unit and the Arcade Card; the hardware page's VCE colours, pad port (Mednafen's multitap), interrupt controller, I/O bus, PSG port; the timer | the game run on the core from models (docs/23) |
| `psg.sv` | the HuC6280's PSG, clock for clock as Mednafen's `pce_psg.cpp`: six channels, noise, the LFO, the volume sequencer, and the levels each side adds up | Mednafen's own `PCE_PSG` (beetle-pce's object) |
| `adpcm.sv` | the CD unit's ADPCM: `$1808/9` address, `$180D` command, `$180E` rate, the playback clock and fetch with the length count and the half and end flags, and the MSM5205 decoder | `io_model.c`'s ADPCM (which matches Mednafen) and Mednafen's own decoder (`okiadpcm.h`) |

## How they're checked

**`HWRTL=1 run_vsys.sh <window> [frames]`** builds `vrtl`: the CPU core, `vdc_timing.sv`, `huc_sys.sv` and
`adpcm.sv`, all from Verilator, with `sys_host.c` around them. Every memory access and hardware-page access goes
through `huc_sys.sv`'s decode. The memory is one flat array in its layout. The colours are the RTL's alone. The pad,
interrupt-controller and PSG-port reads are compared with `io_model.c`'s, which runs beside it. `adpcm.sv` is stepped to
the CPU's time at each access to the CD unit and compared with `io_model.c`'s ADPCM state. Each nibble it decodes is
fed to Mednafen's decoder too.

**`run_eqpsg.sh <log>`**: `psg.sv` against Mednafen's `PCE_PSG`, compiled from beetle-pce. The input is a log of PSG
writes with their PSG clocks: `TB_PSGLOG=<log>` on a `vrtl` run, or `tools/m3_psgsyn.py` for random writes. After
each write it compares every channel's state: the wave index, sample, period counter, the frequency cache, noise
LFSR and counter, the two volumes and the accumulator. It also compares the level each side gets, from Mednafen's
own tables and output choice.

## Results

| Run | Result |
|---|---|
| stage 1, 3, 5 windows (600 frames each), `vrtl` | against Mednafen's end, the same as docs/23 with the C hardware: stage 5 every byte equal, stages 1 and 3 all equal but the same 6 and 3 stack bytes. About 52 million memory accesses through `huc_sys.sv` each, about 6,000 hardware-page reads against `io_model.c`: **0 differ** |
| ADPCM, stages 1, 3 and 5 | **0 of 1,806, 3,326 and 844** state comparisons differ from `io_model.c`; **79,244, 120,965 and 61,933 nibbles** decoded, **0** differ from Mednafen's decoder |
| PSG, the game's writes (stages 1 and 3, 10 s each: 17,877 and 27,368 writes) | **0** channel states differ |
| PSG, random writes (`m3_psgsyn.py`, seeds 2-5: 160,000 writes, 11 s of PSG time, every register, LFO modes, noise, DDA, short periods) | **0** channel states and **0** levels differ |

The power-on window (`tr/pon`) can't be run this way: `sys_host.c` has no CD drive (it reads `$1800` and stops), the
same with the C hardware.

## Found on the way

- **`$1402`, the interrupt mask read:** the first RTL put the I/O bus's top bits above the mask. `io_model.c` and
  Mednafen give the mask alone (bits 3-7 zero). The check found it on its first window.
- **`psg.sv`'s LFO shift:** it was computed 2 bits wide, so mode 3's shift of 4 became 0. The random writes found
  it. The game never turns the LFO on.
- **The ADPCM RAM read path (`$180A`):** a CPU read moves the read address and the length too. Without it,
  `adpcm.sv` was one address behind `io_model.c` after the game's first read. It is now an input (`rd_done`).
- **Mednafen's volume sequencer:** `pce_psg.cpp` looks at a pending volume change only at its next `Update`. But
  the sequencer is idle from the write to then, so starting it at the write gives the same clocks.

## What is not yet RTL, or not checked

- **The timer:** written as Mednafen's HuC6280 has it, but Sapphire never touches it (`io_model.c` notes any
  access; none in any window). It is unchecked.
- **The game's PSG output:** the windows start from Mednafen's PSG at power-on, not from the game's PSG state then,
  so the volumes stay at silence. The game's writes check the channels' state; the levels are checked by the random
  writes.
- **The CD unit:** still `io_model.c` and `scsi_model.c`. That covers the drive, the data FIFO, the ADPCM write path
  and DMA (`wr_done` is its input to `adpcm.sv`), CD-DA and the fader.
- **The sizes:** `psg.sv` and `adpcm.sv` are written per clock with the channel loop in one always block. For the
  EverDrive's FPGA, `psg.sv`'s six channels want to share one adder over the clock's 3 master clocks. Quartus hasn't
  seen any of it; the owner's `build_sapphire.bat` is the place.
- **ares:** none of this is in the ares board yet. The stand-in (docs/40) still runs the C.

## The reference's licence

`psg.sv` and `adpcm.sv` re-implement the behaviour of Mednafen's code (GPL-2.0-or-later). Their headers say so.
The checks link Mednafen's objects from the work folder's beetle-pce build; nothing of Mednafen is in this
repository.
