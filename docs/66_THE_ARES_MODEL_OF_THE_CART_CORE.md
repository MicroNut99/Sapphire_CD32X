# 66 - The ares model of the cart core (13 Nov 2026)

Docs/62's step 5 begins here: before a program is written for Gate of Thunder's core with the Arcade Card (docs/62-65,
fit 4 met timing), ares must have that core, so the program runs against the console's contract and its traps. Gate
of Thunder did the same (its docs/37: the `gotx` configuration); Sapphire's is **`sapx`**.

## 1. The model

**Where:** the owner's ares_android, local branch `sapphire-ac-engine` (`9064d50`, on v1.9 `a2e14e8`), kept in this
repository as `fpga/ares/sapphire-on-ares-v1.9-a2e14e8.patch` (regenerated; it applies to `a2e14e8` as it is).
Chosen as `gotx` is: a `mega-pro.rbf` beside the launcher and a `mega-pro.rbf.ares` naming `sapx`.

**The machine** is Sapphire's own (`tools/m2host/sapm.c`: Dracula X's `dxm.c` with the Arcade Card, equal to Mednafen
from power-on, docs/53), copied into `ares/md/cartridge/board/sapx/` by **`tools/ares_sapx_sync.sh <ares tree>`**: the
models, `tools/m2_machine.py`'s generic interpreter (nothing from the game) and the track pack's reader, each file
through a wrapper in namespace `ares::MegaDrive::sapx` (Dracula X's `dracx` and GoT's `gotx` machines in the same
binary have models of the same names). `sapm.h` is included with `SAPM_CXX_NS`, so its API is in that namespace too
(C++ names: no clash with the old stand-in's `huchost.so`, which carries a `sapm` of its own).

**What the machine gained** (the same as GoT's `gotm.c` has; `sapm_pc` gives the same frames as before - 9,000 of
9,000 equal to the recording, the output identical to the old build's):

- the sound's write log (`sapm_snd`): the PSG's registers and the CD unit's $1808-$180F but $180A, a 512-entry ring
  of {frame's low 3 bits, code, byte} - the core's step-18 log (`io_snd_hook` in `io_model.c`; the card's $1Axx are
  taken before the I/O page, as the core's `ARCADE` decode keeps them off the log);
- the last 512 instruction PCs (`sapm_pcs`), for the trap "the CPU in the I/O page";
- the restart (`sapm_restart`, the key `GTRS`): the engine's reset alone - RAM, VRAM, colours, sprite table, the
  card's 2 MB, the stamps, the frame count and the log's pointer go on; the card's registers reset with the engine
  (`sap_ac`'s `rst` is the engine's);
- the lean card's flag (`sapm_card_flags`): set as `sap_ac.sv` (FULL = 0) sets `unimpl` - a control write with bit
  3, 5 or 6, a right shift, a rotate.

C++ found two things the C compiler let pass: `scsi_model.c`'s file handles (cast to `FILE *`, as GoT's) and an
uninitialised `const` array in `sapm.c`.

**The contract the model answers** (bank 26, wherever it is mapped; before the key everything is map_ssf's):

| Bank 26 + | The core (`fpga/gotcore/make_core.py` on GoT's step 20) | The model |
|---|---|---|
| 0x00000-0x213FF | the video window: VRAM, stamps, colours, sprite table, snapshot, line table | `sapm_vram` ... `sapm_lines` |
| 0x40000 / 0x40002 | the LBA the CD unit wants (15-0; 20-16), the poke port's byte (base RAM) in the high byte | `sapm_drive_next`; base RAM |
| 0x40004 | the busy word: `A1` (Sapphire's step), the card's flags (bit 3 a lost write, bit 2 the lean card's), bit 1 a sector wanted, bit 0 the load port busy | bit 1 = not (the port holds the wanted LBA); bit 2 = the flag; bit 0 never |
| 0x40006 / 0x40008 | CD-DA: playing, mode, start | `sapm_audio` |
| 0x40010 | the CD RAM peek: its byte, bit 7 valid, bit 6 the poke busy | at once: valid, never busy |
| 0x40080 | the start (once armed; after `GTRS` the restart) | `sapm_power` / `sapm_restart` |
| 0x41000-0x413FF | the load port: FwState's words, word 0xFF the key (`GTON`, `GTAR` - taken without its auto-RUN - or `GTRS`) | the same |
| 0x42000 + (a << 1) | the peek: the address's bits 1-0 (and CD RAM's byte read when h says so) | the same |
| 0x43000 + (h << 1) | h: bit 3 CD RAM, bits 2-0 its 8 KB bank | the same (0 until the start) |
| 0x46000 + (b << 1) | the sector port: the next byte, b, in order | the same |
| 0x47000 / 0x47800 | a sector's start (at the LBA wanted now) / its end (the CD unit may take it) | the same |
| 0x48000 + (p << 4) | the pad (SELECT given: step 19 on) | `sapm_pad_now` |
| 0x4D000 / 0x4F000 | the poke port (GoT's step 20) - base RAM, or CD RAM when h says so | the same |
| 0x4E000 / 0x4E400 | the sound log, its write pointer | `sapm_snd` |

While the CD unit waits for a sector the engine's master clock stands still, on the cart (the firmware waits without
limit, GoT docs/33) and in the model (`sapm_waiting`: the machine's time holds while ares's goes on).

**At the start** the model takes what the cart takes: the System Card from bank 27, the disc's table from the load
port, and **the Arcade Card's 2 MB from PSRAM chip 1's banks 16-19** (an even byte the word's high half) - so a card
the program did not clear holds what the EverDrive left there, as on the console (Mednafen's starts at zero).

**Traps** (`[sapx TRAP]`, the PC Engine CD cores' table, `ARES_32X_TRAP=1`):

| Kind | When | The model |
|---|---|---|
| 0 / 1 | the console reads / writes PSRAM chip 1 (banks 16-30) but bank 26 after the start: the engine owns it (the card, the System Card) | reads 0xFFFF, drops the write |
| 2 | the PC Engine CPU runs in the I/O page after having run above it (the last 64 PCs printed) | prints |
| 4 | the card's 2 MB are not all zero at the start | prints the count; the game sees them |
| 5 | the game used what the lean card leaves out | prints the frame; the busy word's bit 2 |
| 6 | a sector ended with other than 2,048 bytes | prints |

Switches: `SAPX_MASKS` (a recording's pad from power-on), `SAPX_POKES` (its RAM writes, `sapm_pc`'s `RUN_POKES`),
`SAPX_HASHES` (each frame's `sapm_hash`), `SAPX_BRAM`, `SAPX_BRAM_GARBAGE`, `SAPX_TRACE`.

## 2. The test: the contract from power-on, sectors through the port

**`tools/sap_files.py <cue> <System Card> <out>`** makes the cart's two start files, as GoT's `got_files.py`:
`SAPFWTAB.BIN` (the load-port image: FwState's words, the key last) and `SYSCARD.BIN`. Its disc table is
`disc/sap_tab.bin`'s, byte for byte.

**`tools/sapx_launcher.py <work> <out> <sector log>`** builds a 14 MB EverDrive image whose 68000 program (224 bytes,
assembled with the toolchain's `m68k-elf-as`) does what the real program will do through the contract: bank 26 into
window 1, the load port and the key, the start, then **feeds the CD unit**: while the busy word's bit 1 is set it reads
the LBA, finds the sector in its store, maps the store's bank into window 2, reads + 0x47000, the 2,048 byte reads,
+ 0x47800. The store holds the sectors a `sapm_pc SEC_LOG=1` run read (2,468 sectors in the first 30,000 frames,
banks 1-10); the Arcade Card's banks are zero in the image. Game data: the work folder only.

| Run | Frames | Equal to `sapm_pc` (each frame's `sapm_hash`) |
|---|---|---|
| ares (console timing, traps on), `SAPX_MASKS` = the play recording, `SAPX_POKES` its pokes, 600 frames | 597 | **597** |
| the same, 31,000 ares frames | 30,948 | **30,000 of 30,000** (the reference ran to 30,000), 2,468 sectors through the port |

No `[sapx TRAP]`: the program never touched chip 1 after the start, the card's banks were zero, the game used nothing
the lean card leaves out, every sector had its 2,048 bytes.

Sapphire's machine is equal to Mednafen for the recording's first 20,000 frames and 74,656 of its 75,000 (docs/53), so the
model through the contract is as well, where the store had the sectors.

## 3. Next

The program on the core (docs/62 step 5b): Gate of Thunder's engine program (`goteng`: the window's reads, the pad, the
menu, the sound from the log, CD-DA) with Sapphire's drawing and menu, the data track fed through the sector port from
the cart's pack, the save by the stack's SD-save commands; then the packed launcher and the console kit.
