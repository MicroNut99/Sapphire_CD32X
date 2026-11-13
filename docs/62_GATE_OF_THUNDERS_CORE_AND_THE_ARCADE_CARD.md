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
| 2 | The card in `got_cart_sram.sv`: its 2 MB in PSRAM chip 1 (a write path for that chip), `got_cart.sv` letting $1A00-$1AFF through as memory; the sector port in place of the feed; the backup RAM on the battery RAM chip (section 5) | **written, compiles; not simulated yet** (section 3b) |
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

## 3b. Step 2 as written: `fpga/gotcore/make_core.py`

`make_core.py <Gate-of-Thunder_CD32X checkout> <out dir>` writes edited copies of GoT's `got_cart.sv`,
`got_cart_sram.sv`, `map_ssf_got.sv` and the bench's `cart_tb.sv` into the work folder (GoT's source is not copied
into this repository; each edit must find its text exactly once in GoT at `4a438c6`, or the script stops).

- **The card:** `$1A00-$1AFF` is no longer the CD unit's page for the engine (`ARCADE`), so the card's accesses are
  memory accesses and wait as memory does. Its byte is read from PSRAM chip 1 at `AC_BASE` + the port's index and
  kept until the port moves; a write is held while the chip takes it (address and byte a clock, WE four, a clock
  more) and a read of the card waits for it. The port steps at the CPU's edge that ends the access.
- **The sector port:** reads of bank 26 + 0x47000 (start), + 0x46000 + byte × 2 (2,048 of them), + 0x47800 (done);
  "a sector is wanted" is bit 1 of the busy word (+ 0x40004), the number at + 0x40000 as before.
- **The card's flags:** bits 2-3 of the same word: the game used what the lean card leaves out; a card write lost.
- **The backup RAM:** the controller's transactions for it go to the battery RAM chip's pins (offset 0, an even byte
  the word's high half - so `bram.srm` is the PC Engine's backup RAM byte for byte); chip 1 and that chip are the
  engine's once it starts.

Verilator reads the four files with `sap_ac.sv`, the MiSTer chip and Dracula X's parts without an error. **That is
all that is known of it**: no frame has run. To check first in the bench: that the MiSTer chip makes exactly one
read cycle for each byte the game reads from the card (a dummy read would step a port); the PSRAM write's timing is
this file's guess and has never been on a console; GoT's own windows must still pass with the edits (they are not in
this container any more).

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

**The owner's rules for saving (13 Nov), and how this design meets them:**

| Rule | Here |
|---|---|
| The game never writes files; it writes the battery RAM and the EverDrive's software keeps it as `MEGA/gamedata/<launcher name>/bram.srm` | the engine's backup RAM *is* that chip's first 2 KB |
| Nothing reaches the card without the header's `RA` entry | the launcher is built with the BootCamp's `EDCD_SAVE=1`; to check in the built file at 0x1B0 |
| No invented bank-31 layout: OpenLara's (the save area in the first 64 KB, 4 KB pieces; slots at 0x10000) | the backup RAM is the save area's piece 0 (offset 0, 2 KB of its 4 KB); nothing else of the bank is used |
| Nothing kept in PSRAM across boots; banks 28-30 the EverDrive's; page 7 only bank 31 | as before (the card's 2 MB and the System Card are loaded at every start) |
| A blank or garbage save must read as "no save" | the System Card's own check of the backup RAM's header (docs/60: the game boots the same on an unwritten one); to see on the console with a fresh `bram.srm` |
| Every build its own launcher name | the kit's launcher gets its own EDCD tag |
| Test: save, power off, power on; then the file's date on the card | the console check for this step |

**Measured (13 Nov): Sapphire itself never saves.** The PC machine with every access to bank $F7 logged, the whole
recorded game (power-on, the System Card, the title, all five stages, 75,000 frames): **no read and no write of the
backup RAM at all**. The data track has no call of the System Card's `bm_read` ($E04E) or `bm_write` ($E051), by `JSR` or
`JMP`; one `JSR $E048` and one `JSR $E057` sit in sectors 4,847 and 5,536, away from any code found so far (most likely
data bytes). For comparison: `cd_read` 7 calls, `ad_play` 13, `ad_stop` 11. So the battery RAM chip will hold the System
Card's own backup RAM (its BACKUP RAM screen), and nothing of the game's. What else could be kept (the options menu's
settings, a high score) is the owner's call, and needs a port the SH2 can write (section 3b's core has none for it).

**The owner (13 Nov), on the direct route below:** "if it works on real hardware then it will get documented and moved
into ARES as an error trap" - so it stays, as a trial on the console.

One difference from the other ports, for the owner to know: they reach bank 31 with the stack's save commands (the
68000 copying 4 KB pieces). Here the core holds the chip once the engine starts, so no command is used and the 68000
must not map bank 31 while the game runs. If the commands are wanted instead, it is a port in the core and a copy in
the program.

## 6. Open

Everything from step 2 on. On the console: nothing of Sapphire has run. ares' picture rate is not the console's
(GoT: 55 in ares, about 15 there), and Sapphire draws more than Gate of Thunder does.
