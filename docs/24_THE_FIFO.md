# 24 - The FIFO: what the SH2 gets from the CPU in the cart (8 Nov 2026)

With the game in the FPGA (docs/20-23), the SH2 only draws. This round defines what it reads to do so, and checks on
the PC that it is enough: the state each picture is made from, rebuilt from the FIFO alone, against the state the
32X runtime's own picture build has.

## The entries (`sys_fifo`, 32 bits: type << 28 | x << 16 | d)

| Type | x | d | When |
|---|---|---|---|
| 1 | | a VRAM word | the VDC commits a write (`vdc_timing.sv`'s `wr_vram`); the address steps by 1 |
| 2 | | a VRAM address | before a word whose address is not the last one's + 1 |
| 3 | register << 1 \| high byte | the byte | a VDC register byte written (CR, RCR, BXR, BYR, MWR, DVSSR...: 5 and up) |
| 4 | the colour's index | the colour (9 bits) | a VCE colour write (its low or high byte) |
| 5 | | | the display starts: a picture |
| 6 | | | the display ends |
| 7 | | the split's first line | a raster-compare match (`ev_rcr`) |
| 9 | | DVSSR | the sprite table's copy starts |
| A | the PSG register | the byte | a PSG write (for the sound, M4) |

`vdc_timing.sv` gains the display's events (`ev_line0`, `ev_vend`, `ev_rcr` with its line). `vsys.cpp` (`RUN=1`,
`TB_FIFO=<file>`) writes them in the RTL's order with the VRAM words, the copies and the registers. `sys_host.c`
adds the colours and the PSG.

**Stage 3: 769 entries a frame** (600 frames: 402,268 VRAM words, 1,306 addresses, 9,600 register bytes, 18,100
colours, 600 splits, 27,368 PSG writes). The SH2 reads an entry as two 16-bit cart reads: about 1,540 reads a
frame.

## The SH2's side (`fpga/huc/fifo_v32.c`)

The entries are applied to the picture's state (VRAM, the sprite table, the colours, BXR, BYR, CR, MWR) with
rt_vdc.c's split rules (docs/16):
- a raster match opens a split from its line with the registers as they are;
- the game's BXR / BYR / CR writes go into the open one;
- the display's end closes it;
- the display's start keeps the frame's splits for the picture.

At each display start it prints the state's hash. rt_pc prints the same hash (`RUN_V32HASH=1`, the runtime's V32 at
its draw hook).

## Result (stage 3, 600 pictures)

| | Pictures |
|---|---|
| everything equal (VRAM, sprite table, colours, registers, splits) | **460** |
| VRAM differs | 139 |
| a colour differs | 1 |
| the registers, the splits or the sprite table differ | **0** |

Where VRAM differs it is a word or so: picture 0 has one word, `0x4CB0`. It is a write of the card's stream that
lands on the other side of the display's start, because the waits (docs/22) move the CPU's bus a little. It is the
next picture's anyway. **The FIFO carries all a picture needs.**

## Next

- The FPGA's model in ares: the EverDrive board with the CPU, `vdc_timing`, the hardware page and the FIFO behind
  the cart's window, as the card's engine is (`fpga/ares/`). ares's own PC Engine CPU core could stand in for the
  RTL there.
- The SH2's build reading the FIFO instead of running the recompiled game: rt_vdc's model replaced by `fifo_v32.c`'s
  rules, the drawing (rt_bg2, rt_spr, docs/16-19) unchanged.
- The FPGA logic itself: the hardware page, `rom1`, the FIFO in block RAM, Quartus.
