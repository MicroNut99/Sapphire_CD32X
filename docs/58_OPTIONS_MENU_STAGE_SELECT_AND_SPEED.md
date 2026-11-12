# 58 - The options menu with a stage select; the crash at the game's start; stage 1 at the game's speed (12 Nov 2026)

The owner, 12 Nov: "Add an options menu like MetalSlug. Its ok to edit the bootstack." and "1. Add all missing 2. Fix
all that you can. Do Not Stop".

**In ares, console timing, the live build from power-on** (`work/full.sh`: the machine as the cart's CPU, the game's
recording; `EXTRA="BG32=1 PWM_SOUND=1 TPSG=1 PADVB=1 SP_MENU=1"`):

| ares frames | What plays | Game frames a second | Pictures a second: before / now |
|---|---|---|---|
| 3,000-6,000 | the intro | 59.9 | 27.6 / 27.9 |
| 6,000-9,000 | the title, the briefing | 59.9 | 21.5 / 25.0 |
| 9,000-12,000 | stage 1's start | 30.4, then 97 catching up / **59.9** | **2.2 / 24.6** |
| 12,000-15,000 | stage 1 | 27.1 / **59.9** | 2.2 / 26.6 |

- **No crash.** Before, the secondary SH2 returned to address 1 at the game's start (ares frame 7,290) in every run.
- **The sound effects against Mednafen's PSG** over those 14,255 game frames (`tower_ares_cmp.py`, the PCM chip alone):
  loudness correlation **0.977** in 30-frame windows, sound in 181 of 183 windows (docs/56: 0.94-0.96; the slow
  run of this round's start: 0.61).
- **The menu**: START opens it over the held game; every row tried with the pad in ares (below).

Nothing here has run on a console.

## 1. The options menu (`SP_MENU=1`)

START (free on the pad: A is RUN, X/Y/Z/MODE are SELECT) opens it at any picture; the game stands still (the cart's
CPU makes a frame only when asked), the effects are silenced, the music plays on.

| Row | What it does | How |
|---|---|---|
| RESUME | back to the game | |
| STAGE 1-5 | A / START: a new game from that stage | the cart's register 0xd2 (0xd3 first: the lives), §2 |
| LIVES 1-9 | the lives left, at once | the poke port: CD RAM $A00C |
| INFINITE LIVES | the lives written again every 32 game frames | the same |
| AUTOFIRE | button B (II) let go every other pair of frames while held | the pad's mask |
| MUSIC VOLUME 0-8 | heard at once | the stack's command 103, COMM2 = 3 (TPSG-3): the BIOS's `FDRSET` |
| EFFECTS VOLUME 0-8 | on leaving the menu | command 103, COMM2 = 2: the PCM channels' ENV |
| EXACT COLOURS | OFF: the Genesis plane even where its 4 palettes can't hold the picture | the picture's rule, §4 |
| CREDITS | a page | |

**The cart's ports** (registers of the `CS_WR` space; in ares the stand-in board, `fpga/ares/0014`; **the FPGA's
RTL for them is not written**): 0xcf / 0xd0 the poke address (below 0x2000 base RAM, from 0x2000 CD RAM), 0xd1 the
value, written at once; 0xd3 a new game's lives (0: the game's own), 0xd2 the stage - the new game starts.

**The stack: TPSG-3** (BootCamp `f1ccdd8`, ledger §36, `DX-TPSG` blocks, `TPSG=1` builds only; `check_boot` passes
with the switch off). Command 103's COMM2 low byte: 2 = the effects' volume (high byte 0-8; `tpsg_volume` scales
every channel's ENV, 0 turns the channels off - the menu's silence), 3 = the CD music's volume (COMM8L 0-0x400 to
`FDRSET`, now and at each track's start).

**Tried in ares with the pad alone** (`work/warp.sh`, `--press`): a new game at stage 1 by the menu, AUTOFIRE on
and B held - the shots sound, the PCM chip's RMS 800; under the menu 0; EFFECTS VOLUME 4: 352; 0: 0. MUSIC VOLUME 8
to 0: the CD fader 0x4000 down to 0 in the menu's steps. LIVES: the poke arrives (`ARES_HUC_LOG`).

Two faults found on the way: the menu's own colours never arrived (the last picture's commit had left the 32X's VDP
with the 68000: `bg2_take()` first), and a new picture was let go just before the menu's check (`OPT_HOLD`).

## 2. The stage select: how the game loads a stage

Found on the PC machine (`tools/m2host/sapm_pc.c`: `SEC_LOG`, `SCSI_LOG`, `RAM_LOG`, `SEC_REMAP`, `SEC_DUMP`,
`IFF`; pictures of its dumps: `tools/m2_dumppic.py`).

- **The stage is base RAM $069C** (0-4): the one byte that is s - 1 through every stage of the recording.
- A new game reads the briefing's program (18 sectors at LBA 11,004), then **one block of 116 at 4,196: 52 sectors
  the game's own program - Super RAM banks $68-$74, for the whole game - then 64 the first stage's own**, then the
  stage's block (596 at 3,600). A later stage reads only its own 64 (4,920 + 724 a stage) and its block (+ 724 a
  stage).
- So $069C written before the briefing loads the right stage's sectors but never the game's program: it jumps into
  nothing (docs/37's "waits forever": the interrupt handler, over and over). Written after the briefing's read, the
  program is there but the stage's own 64 sectors are stage 1's: its map is rubbish.
- **What works, for stages 2-5: a new game; the block's last 64 sectors (4,248-4,311) served from the stage's own;
  $069C = s - 1 once the block's read has begun.** The game then reads the stage's block itself and plays it - all
  four on the PC machine, with the right briefing.
- A new game is reached by a reset and RUN pressed once a second: the game's start is read 29 s after it (the
  title takes 13 of them whatever the drive does); the pad's RUN + SELECT does not reset this game in play. With
  the drive four times as fast meanwhile (`scsi_turbo`, off again at the stage block's end): 20 s to the briefing,
  **26 s from the menu to the stage's block read**.
- The game writes its lives (CD RAM $A00C, 0 until then) once the stage's block is in: the menu's count goes in
  right after, if the menu ever set one.

The stand-in does all of it (`sapmhost.c` `huchost_warp`: the reset keeps the backup RAM and the video memory).
**In ares, the pad alone: stage 3 and stage 5 from the menu, in play, in colour** (`work/warp.sh`). On the cart it is
the engine's work - its reset, the feed's LBA (`pak_read.sv`), the poke port, the CD firmware's rate: **not
written**.

## 3. The crash: `rt_bg2.c` `colours()`

The secondary SH2 took an illegal instruction at PC = 1, PR = 1, in every run from power-on. A diagnostic for ares
(`ARES_WATCH=<lo>,<hi>`: the last 1,024 SH2 writes into an SDRAM range with the writer's PC, printed at an illegal
instruction; with it PR / R15 / R0-R3) showed `bg2_frame` writing 0 then 1 into consecutive longs up through the
secondary's stack: `colours()` counts, for each Genesis palette slot, the distinct colours its entries now have, in
`vals[64]` / `cnt[64]`. 16 palettes' 240 entries can share one slot - all black in a fade - and then each take
its own colour: the 65th wrote past `cnt[]` over the caller's saved registers. Mended: no more than 64 are counted
(an entry not counted differs from the colour kept and is unmapped like the rest).

**DraculaX_32X's and Gate-of-Thunder's `rt_bg2.c` have the same lines.** Not touched from here.

## 4. Stage 1 at the game's speed

docs/57's measure was wrong (a `SAPM_SKIP` start's colours pack, the game's do not): from power-on stage 1's first
3,000 frames made 2.2 pictures a second, the master SH2 in `bg32_draw` in 89 of 100 samples, the secondary idle.
On the PC, 50 of 78 pictures of that stretch cannot go into the plane's 4 palettes of 15 at all (`work/packfit.py`,
a full search: up to 63 colours in 15 palettes). The 32X layer is needed; it was 0.45 s a picture.

In the order found (`work/spd.sh` from a skip start with `RTX=-DBG32_FORCE`: the layer's speed alone; ares's
`--stats` for the bus):

| Step | Pictures a second (forced) |
|---|---|
| a tile's row at a time through two tables, not a pixel at a time; the lower half by the secondary; no list for the hidden plane | 18.7 |
| tile by tile through a band of 8 lines (line by line, the 4 KB cache did not keep 66 tiles' VRAM lines) | 25.3 |
| a tile's 64 pixels kept (`tile_px`, 256 a half) - no gain alone: 77,000 words a picture were being written to SDRAM | 25.8 |
| the band's loop with everything in registers (the SH2's cache writes through; both SH2s share the one bus) | 24.6 |
| a list-less picture swapped by the SH2 itself at the next blank, not by the 68000 a refresh later | 26.2 |
| the plane's list made only when a quick count says its colours can pack (`pack_fits`), not every 8th picture | **29.0** |
| the background's colour entries kept when the sprites' palettes change (the kept tiles stay valid) | in the last row |

- `rt_bg32.c`: `bg32_masks` / `bg32_prep` / `bg32_rows`. On the PC the picture is the old `bg32_draw`'s on 1,200 +
  1,240 pictures of the machine's states (the colour maps and the kept tiles carried from one to the next, as on
  the console; whole and in two halves): `tools/m3_bg32test.sh <gen dir> <dumps>`, the old drawing kept as
  `tools/m3rt/bg32_ref.c`. It caught the one fault of the way: a kept tile's slot given to another tile of the same
  band while the band still held its pointer.
- `FIFO_DEBT` (8): the game never runs faster than its rate; what it fell behind beyond 8 frames is forgiven.
- The borders are the opaque black both buffers always have: not written again each picture.

From power-on stage 1's start is 24.6 pictures a second, not the forced test's 29: its pictures change between the
plane and the layer, and a plane picture goes through the 68000's list (two or three refreshes).

## Open

- **Nothing on a console.** The stack's TPSG-3, the SH2's own swap, both SH2s drawing one buffer at once.
- **The FPGA's RTL** for the poke port, the hold, and the stage select (§1, §2). Quartus not run.
- 25-28 pictures a second, not 30: a plane picture's list through the 68000. EXACT COLOURS OFF leaves holes of the
  backdrop where the pack fails (the menu's line says so), not the nearest colour.
- A `SAPM_SKIP` start's black-and-white colours (docs/57): not looked into.
- After a stage select the first seconds are the System Card's and the title's pictures, RUN pressed by the engine:
  nothing says "loading".
- The pictures of the new rule against the PC's reference, frame for frame: only looked at.
- From before: the 344 frames of the machine against Mednafen; `ARES_HUC_PACE`'s pictures; tones below 112 Hz; the
  ADPCM ring's writer and the PSG write logger in RTL; the packed SH2-alone build (docs/52) to stage 1.
