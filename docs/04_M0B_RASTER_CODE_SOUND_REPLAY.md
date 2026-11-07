# 04 - M0b: the raster, the code map, the sound, the replay (7 Nov 2026)

M0's open items (docs/01, M0b), measured in Mednafen as docs/02. The windows are inside play: a demo stage
ends about 70 s after its load, so stage 1 is `idle_160`-`225`, stage 2 `255`-`322`, stage 3 `385`-`449`,
stage 4 `480`-`546` (seconds from power-on), stage 5 frames 1,300-4,400 after its index is written.

## 1. The raster (`tools/m0b_run.py`'s per-frame line log; DraculaX's `dumplines()`)

| | Stage 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| Horizontal bands a picture (mean / most) | 2.1 / 3 | 2.1 / 3 | 2.0 / 3 | 2.0 / 2 | 2.0 / 3 |
| Vertical offsets a picture (mean / most) | 2.06 / 3 | 2.11 / 3 | 2.02 / 3 | 2.00 / 2 | 2.02 / 3 |
| CR changes down the picture (most) | 2 | 1 | 2 | 2 | 1 |

- **One split, at line 203, in every picture of play**: the playfield above (lines 0-202), the HUD below
  (lines 203-223, 21 lines), each with its own X, Y and CR. 99-100 % of 600 pictures a stage (`idle_200`,
  `300`, `420`, `520`).
- The rest (3-11 % of pictures on stages 1-2): a second change at lines 1-5, the split's interrupt landing a
  few lines late in a busy frame, not an effect.
- **No line scroll, no parallax bands** in play: Sapphire's depth is in its tiles and sprites. On the Genesis,
  the playfield is plane A scrolled by register, and the HUD the window plane (or plane B) below line 203:
  no H-scroll table and no command 78 (VSPLIT) needed. Much simpler than Dracula X's stage 8.

## 2. The code map (`tools/m0_code.py`, 60 s inside each stage)

| Super RAM bank | From the disc | In every stage? |
|---|---|---|
| 68-6C, 74 (instruction starts in all) | LBA 4196-4247: the shared block's first 52 sectors (104 KB = banks 68-74) | **the kernel**, the same on all five |
| 75 (from +4 KB)-77 | stage 1: LBA 4249-4259 (the shared read's last 64 sectors); stages 2-5: their 64-sector block at 4920 / 5644 / 6368 / 7092 | **the stage's overlay** |
| F8 (base RAM) | written at run time | 750-1,070 instruction starts: small routines the game copies into base RAM |

- Instruction starts in 60 s: 9,000-11,000 in RAM banks, ~1,000 in the System Card ROM (its BIOS calls).
- **Almost no self-modifying code in play:** 3 executed bytes written in 60 s, in every stage (which, and why:
  M2's disassembly). 9 of the code banks' 36 quarters of 2 KB are not disc sectors any more: changed after
  loading (data beside the code, or patched code: M2 checks).
- **The wait loop:** `$68:045A LDA $43 / BEQ $045A / STZ $43`: waiting for the vertical blank interrupt's flag.
  57.6-62.8 % of the time on stages 1-4 (76.4 % on stage 5, docs/02 §6, corrected): the game is busy 37-42 %.
- The next hottest spans (about 1 % each): `$69:13D9-13EA`, a loop of `ST1 #0 / ST2 #0` (writing VRAM with zeros:
  clearing), and the stage's own routines.
- As Dracula X (docs/05 there: a 32 KB kernel, per-stage 40 KB overlays): the recompiler translates the kernel
  once and each overlay per stage.

## 3. The sound (`tools/m0_sound.py`)

| | Stage 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| ADPCM plays (65 s) | 187 | 233 | 236 | 219 | 10 |
| distinct samples, rates | 3, 16 kHz | 5, 8 + 16 kHz | 3, 16 kHz | 4, 16 kHz | 2, 16 kHz |
| PSG writes a frame (the demo) | 30.6 | 36.9 | 36.5 | 31.7 | 2.2 |

- **In the attract demo the stages' music is the PSG's** (all six channels, waveform RAM rewritten, noise on
  channels 4-5, no DDA). No CD audio is played in a demo stage; the demo plays CD tracks 15 (the intro) and 12.
- **In a real game the stage's music is CD audio**: a game started from the title (RUN through the menus,
  `st1_play.st`'s path) plays tracks 15, 12 and 13 through the menus and the briefing, then track 3 (LBA 11339) in a
  loop at the stage's start; track 11 at a stage's end (`st5`). The PSG then plays only effects: 12,898 writes
  in that run's 4,922 frames, menus included (2.6 a frame).
- ADPCM: loaded once from the CD (one DMA at the boot), voices and effects at 8 / 16 kHz; never loaded in play.
- On the CD32X: the music as CD audio from the disc's own tracks (the stack's command 45, 1:1); ADPCM samples as
  PCM effects; the PSG's effects on PWM (M4). The demo's PSG music is the one place the PSG plays music.

## 4. The recording and its replay (`tools/m0b_run.py`)

- **`demo`**: power-on, RUN at frames 120-125, then nothing, 33,600 frames (560 s): the intro, the title, the
  briefings, stages 1-4, the interludes. Every frame: a hash of base, CD and Super RAM, the Arcade Card, VRAM,
  the sprite table and the colours; the stage index; the line log; the Arcade Card's reads; the sound log.
- **`st5`**: `idle_140.st` with `$0E15` = 4 written at frame 0, 7,800 frames.
- **Replayed from a fresh core** (the masks and the poke only): every frame's hash equal, the sound log equal,
  on both (`m0b_run.py verify`). The recordings are M2's reference, as Dracula X's 14 were.
- Stage 5 still plays on the demo's stage 1 inputs (both ships die; it ends early): a recording with real
  inputs to its end is still to make, and with it where LBA 8244-8819 and 8956-11003 go (not read by the demo
  or a game start: the later scenes and the ending).

## What this changes in the plan

- The picture: one plane and a HUD, and the sprites. The raster work Dracula X needed (H-scroll tables, VSPLIT)
  is not needed; FBVDP (command 79) for the plane's columns and tiles stays.
- M2 starts from the kernel plus five overlays, the Arcade Card's four ports added to the CPU model's I/O.

## How to make it again

```
python3 tools/m0b_run.py record $W demo 560 && python3 tools/m0b_run.py verify $W demo
python3 tools/m0b_run.py record $W st5 130 idle_140.st 0:0xE15=4 && python3 tools/m0b_run.py verify $W st5
python3 tools/m0_code.py $W "<Track 02.iso>" idle_160.st:st1 idle_255.st:st2 idle_385.st:st3 idle_480.st:st4 idle_140.st+4:st5:3000
python3 tools/m0_sound.py $W demo:9600-13500 demo:15420-19380 demo:23040-26940 demo:28800-32760 st5:1300-4400
```
(The line summaries per window and the split at line 203 were read from `inp/demo.npz`'s `lines` and from
`dumplines()` at `idle_200` / `300` / `420` / `520`.)
