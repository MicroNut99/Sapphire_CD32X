# 07 - M2: the host build alone, every recording from its first frame (7 Nov 2026)

docs/06 checked the translation against Mednafen's trace. This round runs it the way DraculaX's docs/11-15 do,
in two steps:
- **`--timed`:** our own clock, with the trace only compared against;
- **`--run`:** no trace at all, on the recorded pad inputs alone. Our clock, our interrupts, and every read from
  our models: the VDC and VCE, the pad port, the interrupt controller, the CD unit and its drive, and the Arcade
  Card.

DraculaX's models (`tools/m2host/`) carried over unchanged; Sapphire's only additions are docs/06's Arcade Card
and the frame hash below.

## 1. `--timed`: our clock against Mednafen's

All six windows of docs/06 (title, stages 1-5, 60 frames each):

| | Title | Stage 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| Instruction starts at a time other than Mednafen's (master clocks) | 0 | 0 | 0 | 0 | 0 | 0 |
| VDC interrupts ours / Mednafen's | 60 / 60 | 120 / 120 | 120 / 120 | 120 / 120 | 120 / 120 | 120 / 120 |
| The window's length, ours = Mednafen's | 21,539,703 | 21,539,712 | 21,539,694 | 21,539,688 | 21,539,694 | 21,539,697 |

The wait states, the CD unit's next events, the reads of the pad / interrupt / CD registers, the chips' counters
and the CD unit's state after each window are all equal.

## 2. `--run`: alone, frame by frame (`tools/m2_run.py`)

Each frame's md5 is taken the way `m0b_run.py` hashed the recordings: base, CD and Super RAM, **the Arcade Card's
2 MB**, VRAM, the sprite table and the colours. `trace_main.c`'s `frame_md5` was changed to match; DraculaX's left
the card out and padded 16 KB.

| Run from | Frames | Equal to Mednafen | Instructions | Interpreted |
|---|---|---|---|---|
| Each window of docs/06 (title, stages 1-5), 60 frames | 60 each | **60 each** | 1.5-1.8 M each | 0-0.09 % |
| Stage 4's window to the demo's end (`demo` 29400) | 4,200 | **4,200** | 128,195,696 | 102,035 |
| Stage 5's window to its recording's end (`st5` 2000) | 5,800 | **5,800** | 139,636,952 | 167,679 |
| **Power-on (`demo` frame 1) to the demo's end** | **33,599** | **33,599** | **990,038,194** | 602,059 (0.06 %) |

**The whole demo recording runs alone, every frame equal to Mednafen's.** That covers:
- the boot, the System Card, and the 2 MB loaded into the Arcade Card from the CD;
- the intro, the title, the briefings;
- stages 1-4 with their bosses, and every stage's load (the CD drive model reading the data track);
- the interludes between stages.

In real time this is 9 min 20 s of the game; the host ran it in 3 min on one core.

## 3. Where M2 stands

**DraculaX's M2 finish line is met for Sapphire:** the host build matches Mednafen's RAM, VRAM and (Sapphire's
addition) the Arcade Card, frame for frame, from power-on, with the recorded inputs. As in DraculaX, the System
Card's BIOS calls run as its own code (translated from the ROM); their HLE moves to M3.

Open, as DraculaX's were at this point:
- **Stage 5 with real inputs:** its recording still plays the demo's stage 1 inputs (docs/04).
- **Coverage:** the interpreter runs where the recordings never went: 0.06 % over the whole demo.

## How to make it again

```
python3 tools/m2_trace.py build $W $W/m2gen $W/m2check
$W/m2check $W/tr/st3 --timed                               # §1
python3 tools/m2_trace.py capture $W demo 1 60 $W/tr/boot    # (the windows of docs/06 the same way)
python3 tools/m2_run.py $W title:demo:3000 st1:demo:10317 st2:demo:15900 st3:demo:23700 st4:demo:29400 st5:st5:2000
python3 tools/m2_run.py $W --long boot:demo:1 st5:st5:2000
```
