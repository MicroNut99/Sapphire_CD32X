# 38 - The stage clear and the next stage's load, on the PC (8 Nov 2026)

docs/37 played each stage from its own start. Joining them means running the stage clear: the CD reads the next
stage, the game writes it into the Arcade Card's RAM, and new code goes into Super RAM. This round makes the runtime
do that on the PC, with the 14 MB cart's engine. It also measures every stage's screenshots against the PC.

## 1. The screenshots, stage by stage (`pic_check.sh`, `RTBG=work/rtbg3`, `SEC=2 VS_SEC=1 VSSWAP=1 KEEP=1`)

Ten screenshots a stage, ares frames 3,700-4,600. The number is the pixels wrong against the PC's closest picture:

| Stage | Wrong pixels |
|---|---|
| 1 | 27,834 27,834 0 0 0 0 33,889 31,629 2,715 6,641 |
| 2 | 46,101 46,101 0 0 0 0 0 0 0 0 |
| 3 | 0 0 0 0 0 0 1,022 173 0 472 |
| 4 | all 0 |
| 5 | 544 0 0 718 0 0 0 0 0 0 |

- **Stage 2's first two** are the loading screen, before the game's first picture.
- **Stage 1's large ones** are its opening effect (the warp, ares frames 4,300-4,600). The PC's picture has a band
  of the HUD at the top and a gridded background; ares shows the city map and no top band. The raster's bands then
  aren't followed: more splits than the bands and the command-78 list carry. Without `KEEP` it is the same (30,818
  at 4,400), so it is not the cells.

## 2. The card's RAM written (engine, `tools/m2host/ac_engine_model.c`)

**Why it matters:** a stage's load writes about 1.1 MB into the Arcade Card. On the 14 MB cart the card's RAM is the
cart's PSRAM, and the engine read it only.

**The change:** a write to a data port is now made into the cart at the byte's place, the place a read takes:
- not in bank 31, and not in the engine's window;
- a region not here takes nothing;
- the card then steps as before.

`AcEngine.ram_writes` counts these writes, and rt_pc prints the count.

**The RTL** (`fpga/ac_engine.sv`): a data port's write drives the PSRAM's write enable for the byte at its place,
during the CPU's read, as the poke does. The same guards apply: the region here, not bank 31, not the window.
- `fpga/sim/tb_ac_engine.sv` now holds 4 MB, with the state at cart `0x080000` (`log2txt.py`).
- It was run on rt_pc's 1,586,486 engine reads of the stage change below (1,155,072 card writes, 203,615 pokes):
  **0 differ.**

**ares** (`fpga/ares/0004-ares-ace-ram-writes.patch`, branch `sapphire-ac-engine`): the engine writes the card's
RAM the same way.

## 3. The regions out of the engine's window (`tools/m3_speedtest.py pack`)

- **The problem:** with all 32 regions packed (2 MB), regions 12-15 fell in cart `0x1C0000-0x1FFFFF`, the engine's
  window. Those 262,144 writes were refused. On the cart, those reads would hit the engine's registers.
- **The fix:** `AC_REGION_BASE` = the regions' part's place in the state file (a pack without it prints that place).
  rt_pc's cart now has the state at `0x080000`, as the SH2's does: it was at 0, so the window checks fell in the
  wrong place.
  A region that would reach into the window then starts at `0x200000`. `AC_STATE_AT` is the state's cart address,
  `0x080000` by default.

## 4. Code loaded into a poked bank (`tools/m3rt/rt.c`)

The 14 MB build writes the RAM banks it doesn't keep in SDRAM through the engine's poke (docs/11). Those writes didn't
mark the bank's code as changed. When stage 2's overlay was loaded into Super RAM, the hot code went on running the
old version:
- the game stopped at its wait from frame 1,283, the start of stage 2;
- 8.9 million entries were interpreted, and there were timer reads.

**The fix:** a poke now marks the bank as POOL's writes do (`bank_dirty`, the hot block dropped until a dispatch
matches it again).

## 5. Checked: stage 1's clear into stage 2 (`work/tr/t12`, `sp/t12.bin`)

**The window:** `playL` frames 20,300-21,999, 1,700 frames, all 32 regions. It holds stage 1's end, the clear, the
briefing, stage 2's load and the start of stage 2.

| rt_pc (`-DRT_AC_FPGA`, `RUN_DISC`) | Card RAM writes made | Waits reached | Block entries / interpreted | Hash at frame 1,699 |
|---|---|---|---|---|
| all banks writable | 1,155,072 | to frame 1,698 | 77,310 / 4,400 | `06a1de71 b2fc64ab` |
| `RUN_RW=0x69,0x85`, the rest poked | 1,155,072 | to frame 1,698 | 77,310 / 4,400 | `06a1de71 b2fc64ab` |

The two are equal at all 760 waits.

**Against Mednafen:** `m2check --timed` on this window stops matching after 620 frames. Its interrupts differ by one
(1,398 against 1,399) around the load's CD timing. So a window with a load is checked between our two runtimes, not
against Mednafen, until the drive's timing over a long load is looked into.

## Next: the whole game on the 32X (docs/03's 14 MB layout, now with the card written)

The SH2 sees 4 MB, and it is full:

| Cart | What |
|---|---|
| `0x000000` | `GAME.32X` |
| `0x080000` | the state (1.26 MB with a stage's regions) |
| `0x200000` | the translated blocks, 1.6 MB for one stage's selection, up to `0x3F0000` |

One build for the whole game can't hold every stage's code and the 2 MB card in that space. The plan:
1. **The card's 2 MB in a page the SH2 never maps** (for example page 6, cart `0xC00000`). Only the engine reads
   and writes it, by its physical address. The state's regions are loaded there once, and each stage's load writes
   it through the game's own code.
2. **Each stage's blocks in its own page of window 1** (pages 1-5), with the shared blocks in page 0. When the
   game's banks change to another stage's code, the runtime switches the page (command 68).
3. **`CDR=1`:** the stack with `CDRING=1`, and the PC Engine's data track on the disc as `DXTRACK.TRK`. The CDR
   ring moves out of the state's way.
4. **Then the title and power-on:** the System Card's boot from the disc, as recorded in `playL`.
