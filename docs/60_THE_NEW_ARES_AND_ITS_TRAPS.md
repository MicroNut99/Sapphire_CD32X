# 60 - The new ares and its error traps; SELECT never given; the split line (13 Nov 2026)

The owner, 13 Nov: "continue", then: **"Make sure to use the new ARES. It has PCE CD errors traps that could help"**.

ares_android is at v1.9 (`a2e14e8`). Since v1.7 (`bc3ffb9`, where Sapphire's models sat) it has the two PC Engine CD
cart cores that run on the console - Dracula X's `dracx` and Gate of Thunder's `gotx` - and **what those two learned
there, as traps**: its `README-FPGA-FINDINGS.md` section 6. "Both found that ares ran green while the console
failed." This round moved Sapphire onto it, ran the live build through the traps, gave Sapphire's stand-in its own row
in the trap table, and took one finding into the program (SELECT). **Nothing here has run on a console.**

## 1. Sapphire on ares v1.9

- The local branch `sapphire-ac-engine` (18 commits on v1.7) is merged onto `a2e14e8`; one conflict file
  (`everdrive-pro.cpp`: both sides kept). Not pushed to ares_android (the rule). In this repository:
  `fpga/ares/sapphire-on-ares-v1.9-a2e14e8.patch` - everything in one patch that applies to `a2e14e8` as it is
  (`git apply --check`: clean) - and `fpga/ares/README.md`. The numbered patches stay as the history on v1.7.
- The AI's build: `/home/claude/ares_build2/ares-headless` (the work folder's scripts take `ARESBIN=`).

## 2. The live build through the traps

`ARES_32X_TRAP=1` (the 32X's FM rule: on the console an SH2 that touches the frame buffer, the palette or a VDP
register while FM is 0 **waits** - for ever, if it is the one that would set FM), console timing:

| Run | `[32X TRAP]` lines | `[sapphire TRAP]` lines |
|---|---|---|
| From power-on to stage 1 (9,500 ares frames) | 1: the 68000 sets FM at the start | the backup RAM's note |
| The options menu, the stage select to stage 3, the menu again in the stage; X, MODE and A + X pressed | the same 1 | the same |
| The same with `ARES_32X_TRAP_FM=1 ARES_32X_TRAP=2` (every FM change printed) | the SH2 gives the frame buffer away 10 times and takes it back 10 times - at the start and round the menu; **none in play** | |
| The whole recording, 76,500 ares frames (G7) | the same 1 | the same |

No SH2 access with FM at 0, no 68000 access with FM at 1. **In play the frame buffer never changes hands**: with the
background on the 32X layer (docs/59) there is no list for the 68000 and no command a picture. That is the case the
two cores stopped in on the console (Dracula X docs/175: "a master that touches the VDP while the 68000 holds the frame
buffer waits for itself for ever").

`SAPM_BRAM_GARBAGE=1` (the cart's backup RAM as its sram is before anything wrote it: 0x0FFF read back on the console,
Gate of Thunder docs/45): the System Card, the intro, the title, the briefing and stage 1 as with Mednafen's formatted
blank one - no backup RAM screen.

## 3. Sapphire's row in the trap table

Sapphire has no cart core yet: its PC Engine in the cart is the stand-in library (`tools/m3rt/sapmhost.c`, docs/53).
The stand-in now takes the cores' kinds (`everdrive-pro.cpp` `TrapSapphire`, `spTrap`), so a run shows what the
core-to-be will not serve:

| `[sapphire TRAP]` | When | The model then |
|---|---|---|
| the console read / wrote PSRAM chip 1 (banks 16-30) after the start | the SH2 or the 68000 touches the engine's memory once the store is unlocked: the System Card and track 2 are there, as in both cores | reads 0xFFFF; the write is lost |
| SELECT asked by the SH2 | the pad byte for the cart has bit 2 | masks it |
| the PC Engine's CPU below $2000 at a frame's end | the stand-in's PC (`sapm_pc`, `huchost_pc`) in the I/O page after having run above it - Gate of Thunder's run 1b sat at PC 0008 | prints |
| the backup RAM | at power-on, by the stand-in itself: which one it has | - |

None fires in the runs of section 2 but the backup RAM's note. The SELECT trap was checked the other way round: the
build before this round with X held prints it; this round's does not.

## 4. SELECT is never given

`sp_pad_mask` made SELECT of X, Y, Z and MODE. Dracula X docs/132 and Gate of Thunder's `CONSOLE_FINDINGS.md`: the
stack's six-button read reported those bits with nothing pressed on the console, RUN with SELECT is the PC Engine's
reset pair, and both cores stopped giving SELECT at all. Sapphire's program does the same now: B is II, C is I, A is
RUN, Start the options menu; X, Y, Z and MODE are nothing. The game loses nothing in play: the recording's 75,000
frames never press SELECT, and the ships' speed is not the player's to change (it is each ship's own).

## 5. The split line between the two SH2s, and the count by both

docs/59 section 4 measured a picture's phases: the master waited 0.16 of a refresh for the secondary's half of the
background in stage 1, and drew for 1.17 with the secondary long done where a boss fills the upper half; and the
secondary idled through the colours' count.

- **The split line moves** (`bg32_m`, was 112): the master's lines are 0 to `bg32_m` - 1. Up four lines when the master
  waited more than 12 ticks (of 94 a refresh) three pictures running, down four when the secondary was out first
  three pictures running; then it stays 12 pictures. (Eight lines a step moved the two ends by more than the 12
  ticks together and the line swung every 22 pictures.)
- **The count is both SH2s'** (`bg32_masks_rng`: the tiles of a range of lines; the secondary's part through
  `SEC_CK`).

`RTX=-DSEC_TIMES`, refreshes a picture:

| Game frames | | The count | The master's part | Waiting for the secondary's | Pictures a second |
|---|---|---|---|---|---|
| 16,284-17,034 | docs/59 | 0.15 | 0.74 | 0.16 | 29.2 |
| | now | 0.10 | 0.78 | 0.08 | 29.6 |
| 72,284-73,034 (a boss in the upper half) | docs/59 | 0.12 | 1.17 | 0.00 | 26.5 |
| | now | 0.09 | 0.87 | 0.06 | 28.9 |

The sprites by both SH2s (`SPLIT=1`) measured again with the phases: 0.60 of a refresh for 0.53 by the master alone
at 16,284 - still nothing.

## 6. The whole game

| From power-on in ares | docs/59's last (G5, `8896fa7`) | The split line and the count (G6, ares v1.7) | ... on ares v1.9 with the traps, SELECT out (G7, `3f712a0`) |
|---|---|---|---|
| Game frames a second, every stretch | 59.9 | 59.9 | 59.9 |
| Pictures a second by stretch: lowest / median / highest | 27.0 / 29.7 / 30.0 | 28.4 / 29.9 / 30.0 | **28.4 / 29.9 / 30.0** |
| Pictures two game frames after the one before | 31,416 of 32,450 (96.8 %) | 32,562 of 32,848 (99.1 %) | **32,530 of 32,837 (99.1 %)** |
| Screenshots within 2 % / 5 % of Mednafen's, of 265; the worst | 227 / 248; 13 % | 229 / 245; 17 % | 232 / 246; 17 % |
| Illegal instructions | 0 | 0 | 0 |

The effects against Mednafen's PSG in G7: 0.971, as before. The split line moved 897 times in G7's 32,837 pictures.

## 7. What the two cores' console runs say about Sapphire

Read: ares_android `README-FPGA-FINDINGS.md` section 6, Gate of Thunder's `CONSOLE_FINDINGS.md` and docs/52-61, Dracula
X docs/132, 175, 177-178. For this port:

- **ares's picture rate is not the console's.** Gate of Thunder drew 53-60 pictures a second in ares and 14-18 on the
  console, and does not yet know why. Sapphire's 30 is an ares figure. Its drawing is not Gate of Thunder's (no
  68000 list, no command a picture; both SH2s on the frame buffer, the sprites' cells read from the cart), so the
  factor need not be the same - but nothing says it is 1.
- **A fault that takes pictures to heal lasts four times as long there.** Sapphire's layer is drawn whole each
  picture and has nothing to heal; the plane's rules (`BG32_NOPREFER`) would.
- **A command "not waited for" can starve** (the 68000 is nearly always busy on the console): Sapphire's pad comes
  through the stack's PADVB (the 68000 writes it into COMM4 at every blank: no command), and a picture sends none.
- **A USB read of the cart's memories stops the game**; only the mapper's page at 0x1830000 is safe. A core for
  Sapphire needs its status page and PC ring there (Gate of Thunder docs/41) - there is no core yet.
- **Chip 1 is the engine's once it runs**; windows 6 and 7 are the core's in every page; the program's bank below 16.
- The checks "before the console" of section 6 there: (1) the traps with console timing - done, clean; (2) the
  status page - no core; (3) the findings - read.

## 8. A first count of the cart core's parts

The live build's PC Engine is a library in ares; a console needs it as a core in the EverDrive PRO's FPGA (15,408
logic elements). Its parts exist as RTL with benches (docs/10, 20-24, 31-33, 47-52) but not as one core, and Quartus
has seen only the Arcade Card engine. Gate of Thunder's core - the same CPU, Dracula X's VDC and CD unit, no Arcade
Card, no cell store - fits at 98 %. So, each part alone through yosys 0.33 for the same family
(`fpga/huc/yosys_count.sh`; not Quartus, not a fit):

| Part | yosys: LUTs / flip-flops | Quartus, where it has run |
|---|---|---|
| krikzz's `map_ssf` | | 1,754 logic elements (docs/10) |
| `ac_engine.sv` | 2,575 / 853 | about 1,920, with 8 multipliers for its shifters (docs/10) |
| `huc_sys.sv` | 4,676 / 4,666 | |
| `pak_read.sv` | 2,307 / 611, 4 block RAMs | |
| `vdc_timing.sv` | not finished in 600 s (its two copies of the sprite table become registers) | |
| `cellstore.sv` | not read (`automatic` in an `always_comb`: yosys 0.33's SystemVerilog) | |
| `psg.sv`, `adpcm.sv` | not read (a table's hex file the bench makes; a block RAM's start values) | |
| the MiSTer HuC6280 | not read (it has latches) | about 2,400, worked out from Gate of Thunder docs/23's fits (14,528 without the PSG, its VDC 10,144, `map_ssf` 1,754; the VCE is in the rest) |
| a CD unit | none here | Dracula X's lean `dx_cd`: 2,783 (Gate of Thunder docs/26) |

What it shows, and no more than that:

- **`huc_sys.sv` cannot go in as it is.** 4,608 of its flip-flops are the VCE's 512 colours, read in the same clock
  they are addressed in - no block RAM can be that. They must become one (a registered read; the CPU's read of a
  colour a clock later), and the part counted again.
- Where both have run (the Arcade Card engine), Quartus needs about three quarters of yosys's LUTs.
- The parts with a Quartus figure are 8,857 of the 15,408 (`map_ssf`, the engine, the CPU, a CD unit). `huc_sys`,
  the pack's reader, the VDC's timing, the cell store, the PSG's logger, ADPCM, the FIFO's writer and the memory's
  controller come on top. **It is tight at best, and it is not known whether it fits.** That, not a picture a
  second in ares, decides whether this build reaches a console.

## Open

- **The cart core** (section 8): one core from the parts, on the EverDrive's memories, through Quartus; before
  that, the colours in a block RAM and a count of every part.
- The menu's ports and the stage select in RTL (docs/58).
- The layer as a kept, scrolled surface (docs/59 section 4): with the split line 99 % of the pictures are on time in
  ares, so it waits for a console figure.
- `fifo_v32.c` and `rt_pc.c`'s players do not keep `xs`.
