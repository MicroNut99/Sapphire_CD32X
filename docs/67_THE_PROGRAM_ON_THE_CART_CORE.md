# 67 - The program on the cart core; the status bar's split, the saves, the music's level (13-14 Nov 2026)

Docs/62's step 5: the SH2 program for Gate of Thunder's core with the Arcade Card (docs/62-65; ares's `sapx`, docs/66).
The owner's standing direction: "1. Add all missing 2. Fix all that you can. Do Not Stop"; for the saves (13 Nov) the
settings and a HIGH SCORE ("Best Score" renamed) kept on the EverDrive's SD card. **Nothing here has run on a console.**

| In ares (console timing, `ARES_32X_TRAP=1`, `sapx` with fit 4's core) | Result |
|---|---|
| From power-on with the play recording (run t1, 20,010 ares frames: the intro, the title, stages 1-2) | 45 screenshots from frame 9,000 to 20,000 against Mednafen's pictures: **27 within 2 %, 36 within 5 %**, the worst 14.2 % (an effect seen at another phase); **the status bar right in all 45** |
| The same before this round's fixes (run e4) | 37 screenshots: 23 / 31, the worst 10.5 %; **the status bar garbage in 5 of 19 stage shots**, sprites over it |
| The options menu: settings changed and closed (run m2), then a second launch from the same cart RAM (m2b) | the record in bank 31 + 0x1000 (`SPS1` ... lives 4, music 6, effects 7); **m2b's menu shows them** |
| MUSIC VOLUME 8 → 4 at the title, the CD music recorded (run mu2, against mu0: the same run without the change) | the CD music at **0.500** of mu0's after the change, 1.000 before (the two recordings sample for sample alike there) |
| The game's own fades of its CD music (run mu1, the recording) | 2 fade-outs sent as the game wrote them; the music down to silence 2.4 s after the second, as Mednafen |
| Error traps | none but docs/60's 68000 FM write at the start |

## 1. The program (`saeng/`, `tools/saeng_disc.sh`, `tools/sap_pack.py`)

**Gate of Thunder's engine program** (`goteng/` at GoT `4a438c6`: its docs/38-61, console-proven with its core step 20)
taken whole, with Sapphire's parts:

- **The data track from the cart's pack** (`eng_feed`): Sapphire's track 2 is 7,599 sectors (5,255 different) and does
  not fit chip 1 beside the Arcade Card, so the pack (docs/49: `tools/m3_trackpack.py`, each different sector once, LZ4,
  each sector's FNV-1a) lies in chip 0 from cart `0x081000`: its head and first ~1 MB in banks 1-3 (windows 1-3, seen
  always), the rest in banks 4-13 through command 68's pages 1-3. When the core's busy word (bit 1) wants a sector: the
  LBA, the sector made from the pack, then the core's **sector port** (docs/63-66: + 0x47000 start - the LBA read again
  after it, a seek in between and nothing is given; 2,048 reads of + 0x46000 + (byte << 1); + 0x47800 end). The
  engine's clock waits meanwhile: a slow feed costs time, never a wrong frame.
- **The page session** (found in ares, run e2: `[sapx TRAP]` kind 0, the console reading chip 1 after the start): while
  window 1 shows a pack page, nothing may read the core's window - the master holds `cdr_busy` (pace_vbi sends
  nothing), parks the secondary (sound and lists) for the whole session, switches with command 68 from SDRAM
  (`ed_cmd_wait`), and puts page 6 back before it touches the port. A session is refused while a command is in
  flight (COMM0 busy: the 68000 on a picture's list) - the next pass tries again.
- **The start:** the pack's table checked (`SA` `PK`, version 1, the sums of SAPFWTAB.BIN and the System Card), the
  pack's head (7,599 sectors, its length), page 4's 2 MB zero (the card), page 6's System Card, then GoT's load port
  and start read. The packed launcher (`tools/sap_pack.py`, 14 MB, pages 0-6 only): the program in bank 15, the pack
  from cart `0x081000`, SAPFWTAB.BIN at `0x080000`, the System Card in bank 27, the card's banks 16-19 and bank 26 zero.
- **The options menu** on START held (GoT's, docs/56-61) with Sapphire's items (docs/58, 61, 63): RESUME, STAGE SELECT
  1-5 (the engine restarted with `GTRS`, RUN given until the game's start, base RAM $069C set and the stage's own sectors
  fed in place of stage 1's: docs/58's way, now the feed's), LIVES (CD RAM $A00C through the core's CD RAM poke),
  INFINITE LIVES, AUTOFIRE, MUSIC VOLUME, EFFECTS VOLUME, STATUS, CREDITS, the HIGH SCORE line.
- **The sound:** GoT's `snd.c` (the PSG modelled by the secondary on the PWM from the core's sound log) with
  `SAP_NO_ADPCM`: Sapphire's ADPCM RAM is not one fixed bank as GoT's, and the cart has no copy of it yet.
- **The build:** `tools/saeng_disc.sh <BootCamp> <DraculaX_32X> <Gate-of-Thunder_CD32X> <work> <build dir>` (`MUSIC=<the
  owner's Session 1>`: the disc with the CD music). Dracula X's tools are taken at **`DXREV`** (default `8dd9b47`, what
  every run here used): its later `rt_bg2.c` is not this build's, and GoT's patches apply to that one.

## 2. The first runs (e3, e4, m1) and what they showed

- **e3**, power-on to the title with no pad: 1,051 sectors fed from the pack, none failing its sum; 515 page switches;
  one sector asked for outside track 2 (the System Card's, below LBA 3590: zeros). The counter `eng_wants` (31,423 by
  t1's end) counts the passes that found a sector wanted while a command was in flight: the feed waits for COMM0, so
  it is many passes a sector - not a fault.
- **e4**, the play recording replayed (`SAPX_MASKS`, `SAPX_POKES`), 37 screenshots from 2,000 to 20,000 against
  Mednafen (`tools/m3_stageref.py REF_AT=`; each shot against the reference frames within 8 of the engine frame the
  run's log gives): the intro and title exact (0-1.4 %); **in stages the status bar (lines 203-223) was garbage in 5
  of 19 shots** and sprites were drawn over it.
- **m1**, the menu's STAGE SELECT 3 from the title: the game restarted, "STARTING STAGE 3 ..." over the picture, stage 3
  in play 55 s later (the game's own boot, intro skip, stage 1's start and the briefing pass first).

## 3. The status bar's split: found on the PC, mended by the stack's switches

Gate of Thunder's plane harness (`tools/bg2` at GoT: Dracula X's `bg2_host` + GoT's patches) on Sapphire's machine:
`sapm_pc` gained **`RUN_DUMP_VIDEO=1`** (the dump's RAM a hole: `bg2_host`'s input, as GoT's `gotm_pc`) and gives
`LINES_FROM`'s line table. Frames 11,000-11,600 of the recording (stage 1), every second frame:

| `bg2_host` against `ref.py` (the game's own background) | Status bar |
|---|---|
| `TRUEV=1` (each line's own V, as the Genesis's V splits make it) | right in all 301 pictures; 0.12 % of the pixels apart outside the build's black backdrop (the colour merge), at worst 0.37 % |
| line 0's V for the whole picture (no split) | the garbage ares showed |

So the plane was right and **the split was not applied** in those pictures. The program sends the bands (line 0: the
playfield's V; line 203: V 31, the status bar's rows 234-255) with command 78, and the 68000's H interrupt changes VSRAM
at line 203 - but the build lacked the stack's **`VSSWAP=1 HWIN=1`**, which Gate of Thunder's kits have had since its
v13 (its docs/51-53): without HWIN the H interrupt is held off while a picture's list (command 79) runs, so a split due
then comes at the list's end, too late for the picture; without VSSWAP the list committed while the splits run is
switched at the end of the picture it came in, not at the swap that shows it. **`tools/saeng_disc.sh` now builds with
both** (`VSS=0`: without). Run t1: the status bar right in all 45 screenshots.

## 4. The sprites cut at the status bar (`SPR_YLIM`, `tools/sap_spr.patch`)

The game turns sprites off at its split (CR $8C from line 203). The core's line table has each line's scroll but not
its CR, so the program finds the bar by its rows. Measured over the whole recording (`sapm_pc SPLIT_LOG=0,75000`, every
split and CR write): **61,695 + 386 frames turn the sprites off at line 203 with Y 234 / 235 there** (233 / 234 written
to BYR at the split), and nowhere else but a 600-frame curtain before stage 4 (sprites off from a line moving down the
screen, frames 55,732 on) and 13 single frames. The rule (`eng.c spr_hud_line`): the first line from 192 down where a new
band begins with Y 234 or 235; the sprites are drawn above it only (`rt_spr.c`: `geo()`'s clip and the cells' rows use
`spr_ylim` in place of 224 - a patch applied after GoT's `rt_spr.patch`). A change of the line redraws both buffers
whole (`gm_wipe_n`): the sprite drawing clears old boxes only above the cut. Not covered: the curtain's 600 frames.

## 5. The settings and the high score on the SD card

**The route** is Dracula X's v37 (its docs/194), which is OpenLara's, proven on the console: the BootCamp's `sdcard/` kit
merged into the build's copy of the stack (`sdcard/subcpu/merge_sdsave.py`; `SDSAVE=1 SDSAVE_OUT=76 SDSAVE_IN=77`, the
numbers the ledger keeps for the kit, section 18) and the launcher built `EDCD_SAVE=1` (the listener's route to bank 31,
the header's save-RAM entry: the EverDrive's OS keeps the bank on the SD card as `MEGA/gamedata/<the launcher's file
name>/bram.srm`). No change to the stack's files, no core change (bank 31 is krikzz's `bram`, untouched by the core).

**The record** is docs/63's, where docs/63 put it (bank 31 + 0x1000 = the kit's piece 1): `SPS1`, the length (12), the
FNV-1a sum and its complement, then version 1, lives, lives set, infinite lives, autofire, music volume, effects
volume, a spare byte, the high score / 10 (32 bits, big-endian). Commands: 58 puts each of the record's 14 words into
the Sub-CPU's 4 KB buffer, **76** copies the buffer to the piece and reads it back (COMM12 = 0x201); at the start **77**
with COMM2 = 0x8000 asks whether the bank is there, 77 copies the piece into the buffer, 57 reads the words. 76 and 77
copy with RV = 1, so they go by `ed_cmd` (the secondary parked, the master in SDRAM with its interrupts masked), as 68
and 101. `cdr_busy` is set before each wait for COMM0 (Dracula X docs/194's race: the blank's commit 79 between a word
and its 58).

**When:** read once at the start, before the engine's load port. Written when the menu closes (the game is still paused
then: no play is lost to the parked SH2s) and when play has been over for 10 s (no stage frame-end CR, 0x8C / 0xCC, for
600 engine frames: a briefing, the title after a game) - never in play; only when the record would differ from the one
last read or written. The high score is the best score seen while someone holds the pad (docs/63's rule: the attract
demo plays with scores). The menu says where it goes: "KEPT ON THE SD CARD", "NOT SAVED: NO SD SAVE BANK" (the disc's
own program), "THE LAST SAVE FAILED"; STATUS shows the record at the start (READ / NEW / BAD) and the saves taken and
refused.

**In ares** (the board keeps bank 31 in the cart's RAM file and does not wipe it at a launcher's start): m2 - LIVES 3 → 4,
MUSIC VOLUME 8 → 6, EFFECTS VOLUME 8 → 7, closed with B: the cart RAM's bank 31 + 0x1000 holds `53505331 0000000C
4EC4DC88 B13B2377 01 04 01 00 00 06 07 00 00000000`; m2b (the same cart RAM, power-on again): the menu shows LIVES 4,
MUSIC VOLUME 6, EFFECTS VOLUME 7.

**The launcher's name** is the save's folder: the kit's launcher is `Sapphire_EDCD.32X` from now on (`tools/sap_kit.sh`).

## 6. MUSIC VOLUME and the game's own fades (command 104, `CDFADE=1`)

- **MUSIC VOLUME** is back in the menu (docs/58 had it by TPSG-3's command 103, which this build does not use): 0-8 as
  0-0x400, sent at once by the stack's **command 104** (CDFADE-1, ledger section 38: COMM2 the level, COMM8 0 = at
  once, the BIOS's FDRSET), and again after each track's command 45 (a track starts at full volume).
- **The game fades its CD music** with the CD unit's fader $180F, which the core's sound log carries (code 0x1F). Over
  the recording (`sapm_pc SND_LOG=1F`, new): 43 writes - 0x0E then 0x0C (the music fading in 2.5 s; 0x0E's bit 1 is
  the ADPCM's), 0x00 full again; about 20 fades, at stages' ends and before briefings. Without them the music was cut
  where the game fades it. `snd.c` keeps the last fader value (`snd_fader`), and the master follows it as Dracula X's
  `eng_fade` does (its docs/193): 104 to 0 with a slope that takes the menu's level there in that time (the drive's 75
  interrupts a second: 0x400 in 2.5 s is 6 a step), or back to the menu's level at once.
- **Run mu1** (the recording, the music disc): the game's fade-out at engine frame 7,276 - the CD music (the recording's
  left minus right: the PWM is the same in both) falls from 446 to 114 in 2.3 s and is silent from 7,471 (Mednafen's
  fade: 2.5 s); the next track at 8,238 at full. `eng_fades` 2 (6,903 - which the game takes back 5 frames later - and
  7,276), no 104 refused. (In mu1 one DOWN of five was not taken - the cursor stopped at AUTOFIRE - so its MUSIC VOLUME
  step is mu2's.)
- **Runs mu2 / mu0** (no recording: RUN at the System Card, the menu at the title at ares frame 8,200; mu0 the same
  presses without LEFT): MUSIC VOLUME 4 shown; the CD music (left minus right) 676 / 676 over the 16 s before the change
  (correlation 1.0000), **130 / 259 and 308 / 617 after it - 0.500**, what 0x200 of 0x400 should give.

## 7. The kit

`tools/sap_kit.sh <build dir> <rbf> <music build's disc/> <kit dir>`: `Sapphire_EDCD.32X` (the packed launcher) and
`mega-pro.rbf` (fit 4's core) for one folder of the SD card, `disc/` (SAPENG.cue + .bin: the stack's files and the CD
music - the packed launcher reads nothing else from it), a README. In the owner's folder: `Sapphire\console_sd_v2\`.

## 8. New and changed files

- `saeng/` (new): the program - `src/eng.c` (the feed, the start, the menu's Sapphire parts, `spr_hud_line`, the save,
  the music's level, `eng_fade`), `src/menu.c` (the items, the save line, STATUS's core step in hex), `src/snd.c`
  (`SAP_NO_ADPCM`, the effects' level, `snd_fader`), `game_files.h`; the rest GoT's.
- `tools/saeng_disc.sh` (new): the build (`DXREV`, `VSS`, `SAVE`, `MUSIC`, `SND`, `MENU`).
- `tools/sap_pack.py` (new): the packed launcher. `tools/sap_kit.sh` (new): the kit.
- `tools/sap_spr.patch` (new): `SPR_YLIM` on the sprite renderer.
- `tools/m2host/sapm_pc.c`: `RUN_DUMP_VIDEO`, `SND_LOG`.
- `.gitignore`: the program's build and cd folders.

## Open

- **The console.** None of it has run there: the core (fit 4's rbf), the program, the sector port at the console's
  pace, the saves through 76 / 77 on this launcher, 104.
- **The picture rate in stages:** ares gives about 14 pictures a second in stage play (t1, ares frames 12,000-20,000:
  1,906 pictures; the title and intro 40-45). Without VSSWAP / HWIN (run t1n, `VSS=0`, the same frames): 2,104 pictures -
  the switches cost a tenth - and the status bar garbage again in 1 of t1n's 5 shots. The stage's own cost is the next
  thing to measure (the program's `DX_PROF` line). The console's rate is not ares's (Gate of Thunder: ~55 in ares, ~15
  on the console).
- **The ADPCM** (Sapphire's voices): its RAM is filled from changing sectors, not one bank - a copy for the PWM, or a
  core window, is still to make.
- The curtain before stage 4 (section 4); the menu's presses while the intro streams (mu1).
