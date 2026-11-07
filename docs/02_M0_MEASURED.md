# 02 - M0: what was measured (7 Nov 2026)

Everything here was measured in Mednafen's PC Engine core (libretro beetle-pce `b96c11e`, DraculaX_32X's
`core/` plus this repo's `core/sapphire.cpp`: the Arcade Card's port reads and writes tracked), System Card 3,
Arcade Card on, driven by `tools/pcehost.py`. The disc: *Ginga Fukei Densetsu Sapphire* (Japan, reprint),
IsoBuster cue, 17 tracks. Nothing measured here is in the repository (CLAUDE.md rule 1); it is all in the
work folder and can be made again with the commands in each section.

## 1. The disc

| Track | What | LBA (INDEX 01) | Size |
|---|---|---|---|
| 1 | audio (the "this is a CD-ROM" warning) | 0 | 3,365 sectors |
| **2** | **data**, 7,599 sectors = **15.56 MB** | 3590 | to 11188 |
| 3-16 | CD audio, the music | 11339-151357 | 14 tracks |
| 17 | data, a copy of track 2 (7,600 sectors; the first 7,599 equal track 2's) | 166006 | |

`tools/make_disc.py` turns the cue's TOC into a cue Mednafen boots from the two `.iso` files, with the audio as
silent sparse files (the 408 MB `.bin` is never copied). Each INDEX 00 → 01 span becomes a PREGAP.

## 2. The boot, and the attract demo (`tools/setup_states.py`)

- RUN at the System Card screen, then nothing: deterministic (a second run gives a byte-equal state).
- **NOW LOADING: the whole Arcade Card is filled from the disc**: LBA 7220-8243 (1,024 sectors = 2 MB) in six
  READs, about 17 s at the drive's 125 KB/s. Arcade Card port writes from second 2 to second 20: 2,097,152
  bytes, all by the game's own code (the game copies CD data with its own routine, not the System Card's).
- Then the intro, the title, and with no input **the attract demo plays stages 1-4 whole, two players,
  bosses included**, each after its briefing, then the intro again: a cycle of 547 s. Each stage load
  starts at 146, 243, 370, 467 s (and 693, 790, 917, 1014 s on the second cycle).
- **The stage index is RAM `$0E15`** (0-4). Found as the one byte that is 0, 1, 2, 3 in the demo's four
  stages and constant in each, other than two that only follow it; written as 4 in `idle_140.st` (before the
  stage 1 load) the game loads stage 5 from LBA 6496 and plays it. 5, 6 and 7 read past the game's tables
  (LBAs off the disc): **there are five stage blocks**.
- The respawn timer is `$00D1` (set to 64 at a respawn, counted down every 7 frames); `$069F` and `$092B-C`
  change at the last life. Holding `$00D1` did not make the ship invincible. The demo needs no cheats: it
  is the recording source, as DraculaX's patterned play was.

## 3. What each stage loads (CD READs, `tools/m0_stage.py`)

| Stage | Briefing | Shared | Stage block | Into the Arcade Card | Into RAM |
|---|---|---|---|---|---|
| 1 | 8820 (14) | 11004 (18), 4196 (116) | 3600 (32) + 3632 (255+255+54) | 564 sectors | the rest |
| 2 | 8854 (11) | 11004 (18), 4196 (52) | 4920 (64), 4324 (32) + 4356 (564) | 564 sectors | the rest |
| 3 | 8888 (12) | 11004 (18), 4196 (52) | 5644 (64), 5048 (32) + 5080 (564) | 564 sectors | the rest |
| 4 | 8922 (11) | 11004 (18), 4196 (52) | 6368 (64), 5772 (32) + 5804 (564) | 564 sectors | the rest |
| 5 | (off the table) | 11004 (18), 4196 (52) | 7092 (64), 6496 (32) + 6528 (564) | 564 sectors | the rest |

- **The stage blocks sit on a 724-sector grid** from LBA 3600: stage *k* at 3600 + 724 (*k* - 1), five of
  them filling 3600-7219 exactly, up to the Arcade Card's boot block at 7220.
- After each stage, 7540 (116 sectors, inside the boot block) is read again.
- Not yet placed: LBA 8244-11003 (2,760 sectors, 5.4 MB): the intro, cutscenes, briefings' art, the ending.

## 4. The Arcade Card

The tracker (`core/sapphire.cpp`) flags every byte the game's port writes and reads touch.

| | Stage 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| Written by the stage load (frames) | 524-1076 | 339-892 | 463-1016 | 580-1133 | 526-1079 |
| Bytes written | 1,155,072 | 1,155,072 | 1,155,072 | 1,155,072 (+2,048 at frame 0, the end of the state's moment) | 1,155,072 |
| **Written in play** | **0** | **0** | **0** | **0** | **0** |
| Distinct bytes read in play | 586 KB | 367 KB | 433 KB | 229 KB | 211 KB |
| Port reads a frame in play (mean / most) | 457 / 10,790 | 425 / 10,729 | 548 / 10,828 | 84 / 10,814 | (*) |

- **Layout:** `0x000000-0x09FFFF` (640 KB) and `0x1BA000-0x1FFFFF` (280 KB) are the boot block's and never
  change; **`0x0A0000-0x1B9FFF` (1,128 KB) is each stage's**, written only by its load.
- **The Arcade Card is read-only in play.** On the 32X it can be the cart, which the SH2 reads: no Arcade
  Card RAM has to be writable. This is the most important finding of M0 for the port.
- Disc data verbatim (`tools/m0_discmatch.py`, 2 KB blocks against every track sector): 1.47-1.66 MB of the
  2 MB; 256 KB is not any sector (data the boot unpacked, not changed in play); the rest zeros.

(*) Stage 5 from the demo's state plays with the demo's stage 1 inputs: it ends early and the demo then reads
off the table. Its numbers after the stage's end are not counted.

## 5. RAM the game writes

| | Stage 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| Base RAM (8 KB) written | 2,116 | 2,119 | 2,121 | 2,121 | 2,121 |
| CD RAM (64 KB): disc data / zero / other | 44 / 6 / 14 KB | 42 / 8 / 14 | 46 / 4 / 14 | 40 / 10 / 14 | 34 / 16 / 14 |
| Super RAM (192 KB): disc data / zero / other | 156 / 18 / 18 KB | 166 / 8 / 18 | 166 / 12 / 14 | 158 / 16 / 18 | 172 / 6 / 14 |

The tracker cannot tell the game's CD copies from its own writes here (both are the game's code), so the
contents were matched against the disc instead: **all but 28-32 KB of the CD and Super RAM is disc data**.
The game's own writable RAM is at most ~40 KB with base RAM (DraculaX: ~15 KB); 256 KB of SDRAM holds it.
Which of it changes each frame is M0b's to measure.

## 6. The CPU (`tools/m0_cpu.py`, 60 s of each stage)

| | Stage 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| Code in Super RAM | 87.3 % | 89.3 % | 91.6 % | 88.8 % |
| System Card ROM | 6.7 % | 5.2 % | 5.4 % | 5.3 % |
| CD RAM / base RAM | 3.3 / 2.7 % | 2.4 / 3.1 % | 0 / 3.1 % | 3.2 / 2.7 % |
| Interrupts | VDC only (~2 a frame) | | | |
| Time in the 3 hottest 16-byte spans | 57 % | 61 % | 56 % | 57 % |

- The code runs from RAM loaded off the disc (Super RAM, `$68:0450` the hottest span), so the recompiler
  works on disc data, as DraculaX's overlays.
- If the hottest spans are the wait loops (M0b confirms), the game is busy **39-44 % of a frame**: close to
  DraculaX's 38-53 %.

## 7. The picture (`tools/genesis_fit.py`, the demo's captures every 10 frames)

| Stage | Screens checked | Background fits the Genesis' 4 palettes × 15 exactly | Tiles on a screen, most | Distinct tiles |
|---|---|---|---|---|
| 1 | 46 | **63.0 %** | 318 | 1,511 |
| 2 | 48 | 95.8 % | 359 | 1,674 |
| 3 | 47 | **100 %** | 331 | 1,193 |
| 4 | 48 | **100 %** | 320 | 1,156 |
| 5 | 43 | 88.4 % | 306 | 1,388 |

- Sprites on screen: 13-21 on average, at most 33-52 of 64 (crude count: entries with a position).
- Stages 3 and 4 fit the Genesis plane exactly; stages 2 and 5 nearly; stage 1 (the city) does not, as
  DraculaX's stages 0 and 1 (its D2: merge, or the 32X draws the far layer). The packer is greedy: a "no"
  may still fit.
- The screens include briefings and stage cards (the demo's), so these are a lower bound for play.

## 8. What it means for the cart (the owner's 4 MB / 14 MB question)

| What | Size |
|---|---|
| Arcade Card, the boot block's part (never changes) | 920 KB |
| Arcade Card, each stage's part | 1,128 KB × 5 = 5.5 MB |
| The stages' RAM loads (Super / CD RAM) and the shared 4196 / 11004 blocks | ≈ 0.75 MB |
| The rest of the track (intro, cutscenes, ending; not yet placed) | ≤ 5.4 MB |
| **The whole data track** | **15.56 MB** |

- **14 MB (EDCD, DraculaX's D3 layout, pages 0-6):** every stage's Arcade Card part resident with the boot
  part (6.4 MB) and the RAM loads, plus the recompiled code: everything copied into the cart once, no loads
  between stages. The cutscenes fit if they turn out ≤ ~5 MB, or stay streamed.
- **4 MB:** one Arcade Card image (2 MB) in the cart, the stage's part (1.1 MB) reloaded from the CD32X disc
  at each stage as the PC Engine does (about 9 s at the drive's speed, the original's own wait), the code in
  the rest.
- Both work, because the Arcade Card is read-only in play. 14 MB is the faster, simpler build; 4 MB keeps the
  original's loads and fits the plain RAM cart.

## How to make it all again

```
W=work; mkdir -p $W/sys && cp <your>/syscard3.pce $W/sys/
bash core/build_core.sh $W
python3 tools/make_disc.py "<disc>.cue" "<Track 02.iso>" "<Track 17.iso>" $W/disc
python3 tools/setup_states.py $W 560                     # states/idle_NNN.st, the CD READ log
python3 tools/m0_stage.py $W idle_140.st st1 88          # st2: idle_240 88, st3: idle_365 90, st4: idle_460 92
python3 tools/m0_stage.py $W idle_140.st st5 150 4       # stage 5: index 4 written into $0E15
python3 tools/genesis_fit.py $W st1 st2 st3 st4 st5
python3 tools/m0_discmatch.py $W "<Track 02.iso>" idle_200.st:st1 idle_300.st:st2 idle_420.st:st3 idle_520.st:st4 idle_140.st+4:st5
python3 tools/m0_cpu.py $W idle_170.st:st1 idle_265.st:st2 idle_390.st:st3 idle_490.st:st4
```
