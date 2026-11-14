# 68 - The missing parts: the ADPCM, the stage's picture rate, the bands' order, the pad at every blank, a faster stage select, the title's background, the colours (14 Nov 2026)

The owner, 14 Nov 00:30: "List everything missing and then rebuild and or add it." The list, as sent:

| # | Missing | Now |
|---|---|---|
| 1 | **The ADPCM** - the voices and the game's ADPCM effects were silent (`SAP_NO_ADPCM`) | **added** (section 1): in ares the ADPCM alone follows Mednafen's ADPCM alone, heard in the same 120 seconds of 259, loudness 0.959 |
| 2 | **The stage's picture rate** - ~15 a second in ares in stage play (the title: 38) | **28.6 a second** in the same stretch (section 2) |
| 3 | The status bar's bands sent a picture early (before the previous picture's swap) | **sent after it**, and only what changed (section 3) |
| 4 | Sprites over the black curtain before stage 4 (~10 s) | not done (open) |
| 5 | STAGE SELECT ~55 s from the title | **~26 s** from the title (section 5) |
| 6 | Menu presses missed while sectors are fed | **the pad at every blank** (section 4) |
| 7 | The console | the owner's: kit `console_sd_v3` (section 8) |
| 8 | Found on the way: **the title's hangar missing** in some runs (the plane covered for good after the intro) | **fixed** (section 6) |
| 9 | Found on the way: the title's colours a fifth of the pixels off once the hangar showed | **0.6 %** (section 7) |

**Nothing here has run on a console.** All runs: ares (the owner's ares_android, `ARES_32X_TIMING=console`, `ARES_32X_TRAP=1`,
the `sapx` model with fit 4's core), the play recording replayed (`SAPX_MASKS`, `SAPX_POKES`) unless said; no trap but
docs/60's 68000 FM write at the start in any run.

## 1. The ADPCM: ADPCM RAM mirrored in the cart (`SAP_ADPCM`, default on; `ADPCM=0` without)

**What Sapphire does** (`sapm_pc ADW_LOG=1`, new: every byte into ADPCM RAM, by the CD unit's DMA or the CPU's $180A,
in runs): **9 loads in the whole recording, each 65,536 bytes at address 0 by the DMA from 32 sectors; no byte by the
CPU.** Each stage's block begins with its 32 (LBA 3600 + 724 (n - 1)); before stages 2-5 a briefing's voice bank from
LBA 8956 + 32 k. The game leaves the DMA on ($180B = 1, `SND_LOG=1B`) for ~10 s while it reads the rest of a stage's
block (724 sectors) into the card - those do not go to ADPCM RAM (65,536 bytes a load, every time).

**The mirror:** 64 KB in bank 15 (window 0, the program's bank) + 0x50000, past the program and its table; the packed
launcher leaves it zero.
- **The secondary follows the loads** (`snd.c adm_*`), as Dracula X's `eng_ad_log` does (its docs/191, Mednafen's
  pcecd.cpp): from the sound log's $1808 / $1809 / $180D the write address, from $180B the DMA. A sector taken while
  the DMA is on goes to ADPCM RAM at the write address, 2,048 bytes; the sector taken just before the DMA went on is the
  load's first; **64 KB a load at most** (the rule the recording shows). Why the secondary: it reads the log every
  11.6 ms; the master can spend 60 ms on a picture, and the log (512 entries) takes up to 446 in 4 frames of play.
- **The takes come from the master**, which feeds the sectors: each with the log's write pointer when it was fed (the
  machine stands still while a sector is wanted, so the take falls between the right log entries) - a ring of 64
  (`ad_sh.tq_*`).
- **The master copies** each assigned page (`eng_ad_copy`, one a pass of the main loop): the sector made from the pack
  again, through the frame buffer's list area and command 101 into the mirror. A page is played once its copy is in
  (`seq == done`); a page not there yet plays as nothing.
- `snd.c`'s ADPCM decoder (Gate of Thunder's) reads the mirror in place of GoT's fixed bank. STATUS: ADPCM LOADS / PAGES.

**Tests:**
- run a1 (to frame 22,000): the mirror's state against `sapm_pc`'s loads - at 9,000 one load, pages 0-31 = LBA
  3600-3631; at 22,000 three loads, pages = 4324-4355 (stage 2's): **exactly the PC machine's**; 96 pages copied, none
  failed, no take dropped.
- run ao (`RTX=-DSAP_ADPCM_ONLY`: the PSG not mixed) against Mednafen's ADPCM alone (`tools/m3_stageref.py` with
  `REF_ONLY=adpcm REF_NOCDDA=1`), second by second in the game's time: **ADPCM heard in the same 120 of 259 seconds,
  none where Mednafen has none; loudness correlation 0.959.**
- With the PSG (a1 against Mednafen's PSG + ADPCM): loudness 0.947 (0.938 without the ADPCM: the PSG is most of it).

## 2. The stage's picture rate

**Measured where a stage picture's time goes** (`RTX=-DSEC_TIMES`, now on the engine build too - the watchdog clock
started in `main()`; `sec_t[]` in the SDRAM dumps; new slots in `eng.c` and, through `tools/sap_spr.patch`, in the
sprite drawing). Stage 1's base, ares frames 12,000-16,000, ms a picture:

| | p3 (docs/67's build) | p4 (32X palettes off, bands after the swap) | **p5 (and the pad at every blank)** |
|---|---|---|---|
| Pictures a second | 15.0 | 25.5 | **28.6** |
| The sprites' phase (of it: the 32X's background cells) | 31.9 (24.5) | 7.0 (0.2) | 6.9 |
| The pad: waiting for COMM0, command 48 | 12.0 + 1.7 | 12.4 + 2.0 | 0.4 (no command) |
| The bands (command 78) | 6.7 (3.1 commands) | 4.4 (1.9) | 4.3 (1.9) |
| Waiting for the picture before to be shown | 10.5 | 11.3 | 23.0 (the list's time and the blank: what is left) |

- **The 32X's background cells** (Gate of Thunder's `BG2_X32`: a palette the Genesis' 4 x 15 has no room for, drawn by
  the 32X): in stage 1's base the choice put palette 0 on the 32X - **678 cells in one picture**, 24.5 ms of the master's
  time a picture. The merge by nearest colours instead, measured on the PC (Gate of Thunder's plane harness, section
  3 of docs/67, frames 14,000-14,600 and 15,200-15,800 against the game's own background): **0.49-0.50 % of the pixels
  more than 40 of 255 apart, 0.007-0.07 % more than 80**. So **`X32` was turned off by default** - section 7 turns it on
again, with a budget and the menu's COLOURS (AUTO: 24.9 pictures a second in the same stretch).
- The rest: sections 3 and 4.

## 3. The bands: after the swap, and only what changed

- **Sent after the previous picture is shown** (`eng_bands_send`, called by `draw_hook` after its `pal_wait`): with
  VSSWAP a list committed while the splits run is shown at the next swap of command 79, so picture N's bands must come
  after N - 1's swap. They were sent before it (from `eng_scroll`), so they went out with N - 1's picture: the
  playfield's scroll a step ahead of its picture whenever the list ended before the blank.
- **Only the entries the list being filled lacks:** the stack keeps two lists; an entry goes into the one not shown and
  each commit makes that one shown at the next swap, so the program keeps what each holds (`eng_vsl`). In play only the
  playfield's V (entry 0) changes: 1.9 commands a picture with the commit, from 3.1.
- **V modulo the map's height** (the plane is the map's size): a playfield whose Y passes 511 within a picture was two
  bands (a false one at line 201, a command more).

## 4. The pad at every blank (the stack's `PADVB=1`; `SAP_PADVB`, default on, `PADVB=0` without)

The stack's PADVB-1 (ledger section 37; Dracula X's v30 on, which has played on the console): once COMM6 holds 0x5056,
the 68000's vertical blank handler writes pad 1 into COMM4. The master's blank interrupt (`padvb_vbi`) keeps the word
and ORs every button pressed since `eng_pad` last took them: **a tap between two pictures is not lost** (run mu1's DOWN
in docs/67), and `eng_pad` no longer sends command 48 - which waited for COMM0, i.e. for the 68000 to finish the last
picture's list: 12 ms of each stage picture. In run p5 the word was taken at all 4,000 blanks.

**For the tests:** COMM4 / COMM6 are the stack's then, so ares's log has no engine frame (`comm6` reads 5056). The
harness now prints the `sapx` engine's own last frame on each screenshot line (`eng <frame>`, ares local - section 9):
the comparisons against Mednafen use that, with any build.

## 5. STAGE SELECT from the title without the restart

From the intro or the title the menu's STAGE SELECT no longer restarts the engine: RUN until the game starts, then the
stage's own block (docs/58's way, `gm_macro` states 30-32). From a stage, the restart first as before. Run m3 (no
recording; RUN at the System Card, the menu at the title at ares frame 8,200, STAGE SELECT 3, A at 8,340): **stage 3
in play by ~9,900, 26 s** (docs/67's m1: 53 s). The rest is the game's own loading at the drive's speed (the stage's
block, the briefing), as on a PC Engine.

## 6. The title's background: the entries past a smaller map (`tools/sap_bg2.patch`)

**Seen:** after the intro the title showed the logo and the text on black - no hangar - in four of six runs of this
round's builds, whatever was switched; the hangar was there in the other two and in docs/67's build at one RUN timing,
missing at two others. A race, not a switch.

**Found** (ares VDP dumps with the planes' registers, the SDRAM's `bg2` state; run v1, RUN at ares frame 1,320, the
title at 8,400): the plane held the hangar's top rows and, below row 12, the intro's ship; the 32X's colour 0 was opaque
(`draw_hook`'s hold, docs/115: the plane stays covered until a picture's list is whole) and the list was never whole:
`ndirty` 1,660, `late` 1. Of the 1,660 entries still to send, **1,013 lay past the title's map** (32 x 32 = 1,024 entries;
the intro's was 64 x 64): `send_entries` goes to the map's end, so they were never sent and never cleared. The other
647 were the title's own from entry 377 on: **no slot** - 1,418 of the 1,536 tile slots held, 816 of them by the old
map's entries past the new one, whose references were never let go. Whether entries were still waiting when the map
shrank depends on the converter's pace: hence the race.

**Fixed** in Dracula X's `rt_bg2.c` as this build takes it (`DXREV=8dd9b47` + Gate of Thunder's patch), by
`tools/sap_bg2.patch` (applied by `saeng_disc.sh`): when the map's size changes, the dirty marks past it are dropped;
then their slots are let go, 1,024 a picture at most (`bg2_tail_hi`). Run w1 (the same timing): the hangar from the
title's first picture, `ndirty` 0, the hold off. The same code is in Dracula X's `rt_bg2.c` at its head (`b51d67e`) and
in Gate of Thunder's build: noted for them, not changed there.

## 7. The colours: the biggest palettes first; COLOURS (AUTO / EXACT / FAST)

With the hangar back, the title was **20.5 %** of its pixels more than 40 of 255 off Mednafen's (`ref_title`: the demo
route, RUN at 120): its greys and yellows shown as browns and purples. The title shows 16 PC Engine palettes - the hangar
alone is palette 15, 825 of the 1,024 entries; the mech, the letters and the lights 15 small ones (199 entries) - in the
Genesis' 4 x 15 colours. The pack (`repack`) placed the palettes by their indices' count and the merge (`merge`) by
number, so the hangar came last and took what the small ones had left.

- **`pal_order`** (`tools/sap_bg2.patch`): the pack and the merge place the palettes by their entries on the map, the
  most first (then by indices). The title: **4.6 %** without the 32X's help (X32 off), **1.4 %** with it.
- **`BG2_X32` back on by default, with a budget**: the 32X draws a palette the Genesis has no room for (Gate of
  Thunder's docs/60) while the palettes it draws hold at most `bg2_x32_budget` entries of the map; past it the merge,
  and a palette that went with few entries and grew past it (the stage scrolled more of it in) comes back, the biggest
  first, at the next check (every 64 pictures). Without the budget stage 1's base kept 1,211 entries' palettes on the
  32X, 654 cells drawn a picture.
- **The menu's COLOURS** (`menu.c`, saved in the record's byte 7, which was 0 - an older record reads AUTO): **AUTO**
  (budget 256: the title's small palettes, not stage 1's large), **EXACT** (any), **FAST** (none: the room 0, every
  palette back to the Genesis). `bgx_slots` sets both each picture.

**Measured** - ares, the play recording from power-on, ares frames 2,000-20,000 (37 shots, each against Mednafen's
pictures from 40 frames before the engine's frame of the shot to 2 after: the picture shown lags the engine by 4-12
frames), the pictures a second from `bg2_sent` between frames 12,000 and 16,000 (stage 1's base):

| | pictures a second | shots: median / mean / worst off | the title (8,000) |
|---|---|---|---|
| FAST (the merge, the new order) | 27.4 | 1.42 / 2.50 / 12.0 % | 4.6 % |
| EXACT (the 32X, no budget) | 18.0 | 0.00 / 1.51 / 9.9 % | 1.4 % |
| **AUTO (the default)** | **24.9** | 0.45 / 2.00 / 13.1 % | **0.6 %** |

The engine itself is unaffected: in the FAST and EXACT runs every one of 15,579 / 15,571 engine frames equal to the PC
machine's (`SAPX_HASHES` against `sapm_pc`). What the worst shots show: an animation a few frames apart, a merged
colour (a boss's white wings grey). COLOURS on the menu checked in ares: AUTO -> EXACT -> FAST, and FAST took every
palette off the 32X (`x32_set` 0, room 0).

**A trap met on the way:** a build from the BootCamp checkout at `/home/claude/cd32x-bootcamp` (its branch `tpsg`, no
command 104) hangs at the game's first fade - COMM0 holds 0x68 unanswered and the master waits in `pal_wait` for good.
The builds are from `/home/claude/repos/CD32X-BootCamp` (`main`, `CDFADE-1` at `a9f8b12`).

## 8. The kit

`console_sd_v3` (docs/67's `tools/sap_kit.sh`) in the owner's `Sapphire\console_sd_v3\`: `Sapphire_EDCD.32X` (this
round's program, the music build, md5 `d82c565e`), fit 4's `mega-pro.rbf` (`10d6bdf7`), the disc as v2's
(`disc\SAPENG.cue`, `SAPENG.bin` md5 `706349f0` - made on the owner's PC from his `Session 1`; the launcher reads only
the music from it: v3's launcher with v2's disc in ares - the title 0.8 % off, the music, the menu - as with its own).
The README names the menu's COLOURS.

## 9. New and changed files

- `saeng/src/snd.c`: `SAP_ADPCM` (the loads followed, the mirror read), `SAP_ADPCM_ONLY` (a test).
- `saeng/src/eng.c`: the takes (`eng_feed`), `eng_ad_copy`, `eng_bands_send` and the bands' new rules, the PADVB pad,
  STAGE SELECT from the title, `SEC_TIMES` slots.
- `saeng/src/main.c`: `padvb_vbi`; `eng_bands_send` in `draw_hook`; `prof_init` for `SEC_TIMES`; `sap_wdt`.
- `saeng/src/menu.c`: STATUS's ADPCM line.
- `tools/sap_spr.patch`: `SEC_TIMES` slots in the sprite drawing.
- `tools/saeng_disc.sh`: `ADPCM`, `X32` (default 0 here), `PADVB`.
- `tools/m2host/io_model.c` / `.h`: `io_adw_hook`; `sapm_pc.c`: `ADW_LOG`.
- `tools/sap_bg2.patch` (new): the entries past a smaller map (section 6), `pal_order`, `bg2_x32_budget` (section 7).
- `saeng/src/menu.c` / `eng.c`: COLOURS; `tools/saeng_disc.sh`: `X32=1` by default, `sap_bg2.patch`; `tools/sap_kit.sh`:
  the README's COLOURS.
- ares (local, branch `sapphire-ac-engine`, not pushed): `--dump-vdp` files end with 16 words of the VDP's state (the
  planes' addresses, the size, the scroll modes, the display's switches); the screenshot lines end with `eng <frame>`
  (the `sapx` engine's last frame).

## Open

- **The console**: all of it. On the console, COLOURS and STATUS's pictures a second say which mode suits it.
- The curtain before stage 4 (docs/67 section 4): sprites over a band the game has them off in, ~10 s once a game.
- The rest of a stage picture: the 68000's list and the blank (~19 ms of 35 in ares). A list's length is the plane's
  changes; the console's rate is not ares's.
- The game's input reaches the engine once a picture (now ~29 a second in ares, with no tap lost); writing it at every
  blank (Dracula X's `padvb_vbi` does) would need the menu's rules in the interrupt.
