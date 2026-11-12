# 52 - The game in the cart: the packed launcher (11 Nov 2026)

The owner: "the 32X.bin cart has 14MB of RAM for game assets. Dont use the CD unless you must. Just use it for CDDA if
at all possible." And of the packed launcher: "Yes just annote what you did in the ledger and only if it works."

**Result (ares only):** the play build runs from one 14 MB `.32X` and a disc that has only the stack's own files and
the music. Over the stage 1 to 2 window no data is read from the disc and the state at the end equals the PC's. From
power-on only the first loads were run.

## The file

`tools/sa_pack.py` builds it on the BootCamp's PACKED-1 (its ledger §35; `PACKED=1 EDCD_PACKED=1 EDCD_W0=8`). Nothing
in the stack is changed.

| Where | What |
|---|---|
| bank 0 | the EDCD launcher, `EDPK` at 0x1E0 |
| bank 8 (window 0 at the hand-over) | the program; `EDPK` + its length at + 0x7FFF0 |
| bank 8 + 0x40000 | the directory: `SAPK`, version 2 |
| cart 0x080000 | the level's start state (where commands 38 + 46 put it from the disc) |
| cart 0x2A0000 | the shared blocks (`IMAGE21`) |
| page 6, 0xC00000 | the Arcade Card's start contents (`--card`) |
| bank 8's tail, cart to 0x1C0000, page 1 to 0x3EF000, pages 2-5 | the pack |
| cart 0x3EF000 | the log (docs/51) |

Page 7 is never used. Page 0 holds nothing reached by command 68 (it accepts pages 1-7).

**The pack:**
- **The data track:** each different sector once (5,255 of 7,599), LZ4 against one shared 64 KB dictionary:
  **5.83 MB** (docs/49's per-sector pack without it: 6.38 MB).
- **The 11 block combinations:** 4 KB LZ4 pieces: **2.20 MB**.
- Every sector and piece has its sum; `sa_pack.py` reads each one back from the finished file before it writes it.
- It ends in page 5 at 0x1C3D3B: about 2.2 MB of pages 2-5 are still free.

## The program (`saspeed`, `SAPK=1`)

- `pk_ok`: the directory is there and is version 2. Then the boot does not load files 21 / 201, does not open the
  data track (102) and does not load the card.
- `pk_sector`: a track sector from the pack: the page switched in (command 68), the block decoded, its sum checked,
  page 1 back. A sector that fails stops the program at its status (`sa_to[6]`).
- `pk_combo`: a combination, piece by piece: decoded into the frame buffer's list area, written to the cart by
  command 101.
- The log (docs/51) has the pack's counts: sectors read, sums failed, page switches; a log every 64 sectors.

`tools/m3_sapk_build.sh` builds the combinations, the disc and the file, runs ares from the packed launcher
(`--ramcart-boot`) and prints the log; `tools/sa_sdram.py` reads the game's frame and the counts from an SDRAM dump.

## Three traps (found in ares; each is the hardware's rule)

1. **The 32X boot ROM reads the whole cart.** Its checksum reads every word in order, the Arcade Card engine's window
   (0x1C0000-0x1FFFFF) too, where a read is a command. The engine took them and wrote into the pack and the program.
   - **Fix: the engine is armed.** Until three reads in a row (registers F0, EF, F7 with A5 in the value) it answers
     0xFFFF and changes nothing. `fpga/ac_engine.sv`, the C model, ares's model and the runtime (`ace_load` arms
     first) all have it.
   - Checked in iverilog against the C model: 333,139 reads (a scan and random reads unarmed, then armed), 0 differ.
2. **No SH2 interrupt while the 68000 has the cart.** Commands 68 and 101 set RV; the Master's V interrupt handler
   lies in the cart and was fetched as garbage (an address error). The Master's interrupts are masked from the
   command's send to its end.
3. **A byte of 0x00 is not written to the 32X frame buffer.** Every combination's first piece "failed" until the
   decode area was cleared by words first.

## Measured in ares (console timing)

**From power-on** (`sp/pon.bin`): the launcher hands over, the program finds the pack and the game starts its loads from
it: by ares frame 9,000 it is at game frame 256 with 64 sectors read, 0 failed. **Not run further:** the title and the
way into stage 1 from power-on have not been run from the pack (an hour of ares time in this build, docs/39).

**The stage 1 to 2 window** (`sp/t12k.bin` with its card file, stopped at game frame 1,700):

| ares frame | game frame | combination loads | sectors from the pack | failed | page switches |
|---|---|---|---|---|---|
| 10,000 | 605 | 1 | 38 | 0 | 300 |
| 30,000 | 920 | 1 | 353 | 0 | 430 |
| 50,000 | 1,311 | 2 | 710 | 0 | 1,288 |
| 60,000 | 1,700 | 2 | 710 | 0 | 1,288 |

- **The end hash without the stack page: `b2fc64ab`**, the same as `rt_pc`'s and as docs/39's build that read the disc.
- 14 logs came back, each with a right sum; no wait gave up.
- It is still the SH2-alone build: 1,700 game frames took 60,000 of ares's (docs/39). The speed is the FPGA CPU's
  job (docs/40 on), which does not use the pack yet.

## The rule for the owner's USB script

DraculaX docs/177-178: while the EverDrive's USB reads the cart's memory, the console's own reads of that chip get the
USB's words. `tools/usb/sa_after.sh` reads the log once, after the run. The first script (`sa_watch.sh`, every 3 s
during play) is withdrawn; it was never sent.

## Not proven

- **Nothing here has run on a console.** Not this layout, not command 68 at this rate, not the armed engine in the
  real core (Quartus has not built the new RTL), not `memrd 0x3EF000` as the log's address.
- The whole game: only power-on's first loads and the stage 1 to 2 window were run from the pack.
- The live FPGA build (docs/40) still takes the track from the ring; it has to read the pack.

## Files

- `tools/sa_pack.py`, `tools/m3_sapk_build.sh`, `tools/sa_sdram.py`, `tools/m3_trackpack.py` (chains, lazy matching,
  a dictionary), `tools/usb/sa_after.sh`
- `saspeed/src/main.c`, `saspeed/Makefile` (`SAPK=1`, `SA_LOG=1`)
- `fpga/ac_engine.sv`, `fpga/sim/rand_ops.c`, `tools/m2host/ac_engine.h`, `ac_engine_model.c`, `tools/m3rt/rt.c`
- ares: `fpga/ares/0011-ares-ace-arming.patch` (the arming; `ARES_CART_WATCH`, `ARES_ACE_WHO`, `ARES_PC=1`)
- The ledger: §35b (MicroNut99/docs, branch `sapphire-packed-35b`)
