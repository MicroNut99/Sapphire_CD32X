# 62 - Gate of Thunder's core, and the Arcade Card in it (13 Nov 2026)

The owner, 13 Nov: **"switch to Gate of Thunder's console-proven core and add the Arcade Card engine to it"**, and on
saving: **"Saves directly to the MEDPro SDCard ... straight wiring job thru the MEDPros OS"**.

This round is the plan, what was measured for it, and the first part built: the card itself. Nothing here has run on a
console, and the combined core does not exist yet.

## 1. What changes

Until docs/61 Sapphire's live build was a stand-in in ares (the C machine as the cart's CPU) feeding a FIFO, a cell
store and the SH2's players; its core parts (docs/60 section 8) were never one core. Gate of Thunder's core is one
core, fitted and run on the console (its step 20: MiSTer HuC6280 + Dracula X's `dx_vdc` + lean `dx_cd` + the engine
`got_cart.sv` on `got_cart_sram.sv`, inside krikzz's `map_ssf`; 15,183 of 15,408 logic elements, 56 of 56 M9Ks,
+0.136 ns). Sapphire takes that core as it is and adds the one thing its game needs more: the Arcade Card.

What goes with it, in order:

| Step | What | State |
|---|---|---|
| 1 | The card on the HuC6280's bus: `fpga/gotcore/sap_ac.sv`, checked against Mednafen's card | **done** (section 3) |
| 2 | The card in `got_cart_sram.sv`: its 2 MB in PSRAM chip 1 (a write path for that chip), `got_cart.sv` letting $1A00-$1AFF through as memory; the sector port in place of the feed; the backup RAM on the battery RAM chip (section 5) | next |
| 3 | GoT's bench (`tools/fpga/cart.sh`, built here unchanged as the baseline: `work/gc/b0`) with Sapphire's machine `sapm` as the host: power-on, frames against `sapm_pc` | |
| 4 | A size-and-timing fit on the owner's PC (a `.bat`): what must leave to make room (section 4) | |
| 5 | The ares model (GoT's `gotx` way: the C machine in ares, here `sapm`), the program (GoT's `goteng` front end reading the core's window, Sapphire's drawing behind it), the packed launcher, the kit | |

## 2. Measured: what the game uses of the Arcade Card

A counting copy of `ac_model.c` in the PC machine, the whole recorded game (`inp/playL_masks.u16`, 75,000 frames,
74,656 equal to Mednafen as before):

| | |
|---|---|
| Data reads / writes | 41.7 M / 7.9 M (ports 0-3; `$1Ap0`, `$1Ap1` and banks $40-$43) |
| Control values written | **00, 03, 11, 13 only** (never bit 3's 0xFF0000, never bits 5-6: no offset added to the base on a write) |
| Shift register `$1AE0-$1AE3` | written and read ~500 times; `$1AE4` **only left, by 0, 1, 2 and 4**; `$1AE5` written once, with 0 |
| Registers `$1Ap2-$1Ap9` read back | once each |

So a lean card is enough for this game: three control bits, a left shift. The lean form flags any use of what it
leaves out (`unimpl`, for the core's status page), so a missed case shows on the console and not as a wrong picture.

## 3. Step 1: `sap_ac.sv`

The card's registers and ports; the 2 MB are the memory side's. `FULL=1` is the whole card, `FULL=0` the lean one.

- `fpga/gotcore/run_eqac.sh`: 3,000,000 random accesses each against Mednafen's card (`ac_model.c`) - full: 0 wrong;
  lean (the game's control values and shifts): 0 wrong, and the five left-out uses each raise `unimpl`.
- yosys (`synth_intel -family cycloneive`, a rough count: docs/60): lean 871 LUTs / 293 flip-flops, lean without
  the read-back 778, full 1,135. `ac_engine.sv` (docs/10, the card behind the SH2's window) counted 2,575 the same way
  and was about 1,920 in Quartus; by that ratio the lean card is about 600-650 logic elements. **Not a Quartus number.**

## 4. The room, and the data track

- **Logic:** 225 elements are free in GoT's step 20. The card wants ~650 more, its PSRAM write path some more.
  GoT's sector feed (331, its docs/29) goes: see below. That leaves a few hundred to find; candidates, to be measured
  in the fit and not guessed here: the read-back (`REGRD=0`), the diagnostics added for GoT's console runs, parts of
  `map_ssf` a 32X launcher never uses. **No block RAM is free**; the card needs none.
- **The track:** Sapphire's data track is 7,599 sectors, 5,255 different: **10.26 MB** as they are (GoT's: 3.7 MB, laid
  in PSRAM chip 1 and read by the core's feed). Chip 1 holds 7.5 MB at most, less the card's 2 MB and the System
  Card. So the track stays packed (docs/52: 5.83 MB) in chip 0 with the program, the SH2 unpacks a sector the CD unit
  asks for and gives it to the core through a **sector port** (reads with the byte in the address, as the core's
  poke port). The core already waits for a sector without limit and shows which one it wants (`sec_want`).
- **Chip 1** is then: the Arcade Card's 2 MB, the System Card (bank 27), the core's window (bank 26). Bank 31 never.

## 5. Saving

Not yet (asked 13 Nov). GoT's core keeps the PC Engine's 2 KB backup RAM in the cart's `sram` chip (byte 0x40000),
which nothing writes to the card. krikzz's source has a second chip beside it, the battery RAM (`bram`, the 68000's
bank 31), and the EverDrive's OS writes its first 256 KB to `MEGA/gamedata/<launcher>/bram.srm` and reads it back at
the launch when the launcher's header has the save entry (BootCamp `EDCD_SAVE=1`; console-measured there, 25 Oct).
The mapper drives that chip itself (`mout.bram`). **So the backup RAM moves to the battery RAM chip at offset 0, and
the OS does the rest**: no port, no command, nothing for the program. To check on the console: that chip driven by
the engine (GoT's and Dracula X's console findings are about `sram`'s pins; the same controller will drive it).

## 6. Open

Everything from step 2 on. On the console: nothing of Sapphire has run. ares' picture rate is not the console's
(GoT: 55 in ares, about 15 there), and Sapphire draws more than Gate of Thunder does.
