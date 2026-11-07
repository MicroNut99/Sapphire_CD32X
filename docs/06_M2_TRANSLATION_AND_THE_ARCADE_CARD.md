# 06 - M2 step 1: the translation and the Arcade Card, checked against Mednafen (7 Nov 2026)

DraculaX_32X's M2 (its docs/09-10), brought to Sapphire: the game's HuC6280 code recompiled to C, run on the PC
against Mednafen's instruction trace, every instruction's registers and every hardware access compared. Sapphire
adds the Arcade Card, which Dracula X never touches (its host answered "the game never uses it").

## 1. The pieces

| What | Where | Change from DraculaX |
|---|---|---|
| The core | `core/sapphire.cpp`, `core/sapphire_patch.py` | the Arcade Card's accesses through banks 40-43 go into the trace (kind 14 read, 15 write: the physical address and the value); `acstate_get()` exports its registers (4 ports × base, offset, increment, control; the shift latch, shift and rotate bits) |
| Recordings | `tools/m0_replay.py`, `tools/m2_runs.py` | DraculaX's tools open Sapphire's recordings (docs/04: `demo` from power-on, `st5` from a demo state with `$0E15` = 4) |
| The code | `tools/m2_code.py` | collects CD RAM (80-87) and **base RAM (F8)** code too. Base RAM's variables change all the time: there a version changes only with its code bytes |
| The recompiler | `tools/m2_recomp.py` | **Sapphire's slot table** (below); DraculaX's picture hooks off |
| The Arcade Card model | `tools/m2host/ac_model.c` | new: Mednafen's `ArcadeCard::Read` / `Write` line for line (ports, auto-increment, the offset-add triggers, the shift and rotate register, ID `0x51`) |
| The checker | `tools/m2host/trace_main.c` | the card's state and 2 MB from the window's start (`ac_start.bin`); every read through its registers (`$1A00-$1AFF`) or banks 40-43 **computed by the model and compared with Mednafen's value**; its registers and RAM compared after the window (`ac_end.bin`) |
| The capture | `tools/m2_trace.py` | writes `ac_start.bin` / `ac_end.bin` |

## 2. The check

Six windows of 60 frames: the title and each stage's play.

| Window | Instructions | Equal to Mednafen | Interpreted | Arcade Card reads (all = Mednafen's) | RAM / VRAM / card after |
|---|---|---|---|---|---|
| Title (`demo` 3000) | 1,494,258 | all | 0 | 70,432 | all equal |
| Stage 1 (`demo` 10317) | 1,833,571 | all | 1,320 | 2,176 | all equal |
| Stage 2 (`demo` 15900) | 1,712,679 | all | 1,560 | 61,560 | all equal |
| Stage 3 (`demo` 23700) | 1,556,337 | all | 1,380 | 148,600 | all equal |
| Stage 4 (`demo` 29400) | 1,824,756 | all | 1,070 | 120 | all equal |
| Stage 5 (`st5` 2000) | 1,753,883 | all | 900 | 38,528 | all equal |

**10,175,484 instructions, every one's registers equal to Mednafen's; every hardware access equal; base, CD and
Super RAM, VRAM, the sprite table, the colours, and the Arcade Card's registers and 2 MB equal after each window.**
The Arcade Card model answered 321,416 reads, none different. The game never writes the card in play (docs/02 §4):
0 writes in every window. The interpreter ran 0-0.09 % of the instructions (DraculaX at this stage: 0.1-0.8 %).

(Stage 1's window is at frame 10317: the checker can't start a window on an interrupt, and 10200, 10201 and 10230
did.)

## 3. Found on the way

1. **DraculaX's slot table is not Sapphire's.** The recompiler translates each code bank for the 8 KB slot of
   the CPU's address space it runs in. The table was Dracula X's (its bank 69 in slot 2, 76 in slot 5), so most of
   Sapphire's code was translated for the wrong addresses and ran in the interpreter: 9-31 % of the instructions,
   300,000-430,000 block entries a window. Measured from the traces, every bank runs in one slot only:

   | Bank | 00 | 02 | 68 | 69 | 6A | 6B | 6C | 74 | 75 | 76 | 77 | F8 |
   |---|---|---|---|---|---|---|---|---|---|---|---|---|
   | Slot | 7 | 6 | 2 | 3 | 2 | 3 | 4 | 2 | 3 | 4 | 5 | 1 |

   With it: 3 % interpreted, 5,000-8,000 block entries.
2. **Code in base RAM.** The rest of the 3 % was code the game copies into base RAM (bank F8, slot 1: 1,699
   instruction starts), which DraculaX never had. Collected as one more block (its variables beside it are
   ignored; 334 of its code bytes are rewritten in the run, translated with their bytes read when they run, as
   DraculaX's docs/16): 0-0.09 % interpreted.
3. **DraculaX's picture hooks** (`6C:81B8`, its VRAM load list) would have fired inside unrelated code in
   Sapphire's bank 6C in a marks build. Off; Sapphire's are found at M3.
4. The demo rewrites 8,298 code bytes over its 560 s, nearly all outside play; a stage's play rewrites a few
   (stage 5: 135). The translation reads those bytes when they run.

## 4. What is next in M2

As DraculaX's docs/11-15: the cycle model and the VDC / VCE timing against Mednafen's clock (`--timed`), then the
pad, the interrupt controller and the CD unit modelled, then every recording run alone from its first frame, its
state equal to Mednafen's after every frame. Sapphire adds the Arcade Card to the standalone run: its model is
already the source of every card read.

## How to make it again

```
python3 tools/m2_code.py $W demo st5                    # $W/m2: the code blocks
python3 tools/m2_trace.py capture $W demo 23700 60 $W/tr/st3   # (title 3000, st1 10317, st2 15900, st4 29400; st5 2000)
python3 tools/m2_trace.py build $W $W/m2gen $W/m2check
$W/m2check $W/tr/st3                                    # RUN_INTERP_LOG=<file>: where the interpreter ran
```
The core must be rebuilt (`core/build_core.sh`) for the Arcade Card's trace and `acstate_get`; the recordings
replay identically on it (`m0b_run.py verify`: `st5`, every frame's hash and the sound log equal).
