# 36 - Playing it: stage 1 with the pad, in ares (8 Nov 2026)

Until now every build replayed the attract demo, whose ship flies itself. This round makes a real game the source:
a one-player run started from the title. The SH2 build then plays that stage with the controller in ares, past the
recorded window, for as long as it runs.

## 1. A real game recorded (`tools/m0b_play1.py`, `work/inp/play1.npz`)

**From power-on**, in Mednafen with the Arcade Card:
1. RUN at the System Card (frame 120);
2. RUN at the title, about 115 s in (START / CONFIG);
3. RUN at the player select;
4. the briefing, the stage card, and stage 1 from about frame 7,900.

**Then a scripted player:** both shot buttons pulsed (4 frames on, 4 off), weaving left and right, up and down. By
frame 9,800 it has 4,800 points, has picked up a power-up and has lost one life.

`tools/m2_runs.py` knows `play1`, so every M2 / M3 tool takes it like `demo`.

## 2. The window, and the PC

The window starts at frame 8,600 (`work/tr/p1_8600`, `work/sp/p1_8600.bin`; linked as `p1`). It reads Arcade Card
regions `0a,0c,0d,0e,17,18,1e`.

**Two earlier starts did not work:**
- **8,000:** the stage was still loading. There were CD reads and 212,992 writes to the Arcade Card, which the
  runtime does not make.
- **9,000:** in the runtime it ran off course: 2 interrupts and 10 million instructions interpreted.

**The checks:**

| Check | Result |
|---|---|
| `m2check --timed` against Mednafen's trace | equal: 1,199 interrupts, 7,955 waits matched, 0 differ |
| rt_pc against `m2check --run`, no buttons pressed | equal at every wait, except `$34` at the end. `$34` is where the interrupt handler (RAM code at `$1A34`) saves MPR2, so it records which bank was mapped when the interrupt came: the same class as docs/08's line-203 byte |
| rt_pc against `m2check --run`, the recorded pad | apart from frame 12 on |
| rt_pc with the engine's pokes (`-DRT_AC_FPGA`, `RUN_RW=0x69,0x85`) against rt_pc | equal at every wait; 12,142 pokes made |

**Why the recorded pad comparison comes apart:**
- The demo never used the pad, so the input's timing against the frame was never compared before.
- The first state difference is the game's pad byte `$00`, with the shot pulse a frame apart.
- With one player at the pad, a frame of input timing does not matter. With no input the two are equal, so the
  runtime is exact.

**Watch out:** `m2check`'s `RUN_MASKS` file is 16-bit masks. A 32-bit file reads as nonsense.

## 3. The SH2 build in ares

**The hot code:**
- `work/sel_p1` is the window's blocks, selected from `m3gen2`. 6,322 entries a window are interpreted: code the
  demo never ran.
- `work/sel_p1_600m` is that selection split with `SPLIT_MARKS=1 SPLIT_ASM=1 SPLIT_MIN=600`.

**A crash, and its cause.**
- **What happened:** the first builds died at about picture 214-252. They took a CPU exception, and the report
  showed the exception handler's own address, so the stack was broken.
- **Without the drawing** (`-DSPR_NOHOOK`) it ran all 600 frames.
- **The cause was the DIAG areas.** They sat at fixed addresses, `0x0603E800-0x0603EF40`, inside the master SH2's
  3 KB of stack (its top is `0x0603F440`).
  - `PACE_HIST`'s ring of slow pictures (`0x0603E940`) wrote over return addresses once a picture was slow enough.
  - Stage 1 in play has more slow pictures than stage 3's FIFO had.
- **The fix:** `diag_area[160]` in the bss (`PACE_HIST`, `BG2_PROF`, `SPR_PROF`), read past the cache from both
  CPUs. The ring is now 16 pictures. `work/hist.sh` and `work/prof.sh` find it by its symbol in `output.map`.
  - The FIFO measurements of docs/27-35 used `PACE_HIST` too. They didn't crash, and their counts stand.

**The recorded window** (`SEC=2 VS_SEC=1 VSSWAP=1 KEEP=1`) plays its 600 frames. The screenshots are within
71-1,678 pixels of the PC's pictures (not looked into yet). The game build (the SH2 alone, no FPGA CPU) is slower
than 30 fps:

| Refreshes a picture | 1 | 2 | 3 | 4 | 5 | 6 | 7+ |
|---|---|---|---|---|---|---|---|
| pictures | 67 | 180 | 48 | 12 | 33 | 19 | 19 |

## 4. `SPLAY=1`: played with the pad

`SPLAY=1` (saspeed's Makefile: `-DRT_PLAY -DSAPPHIRE_PLAY`):
- each picture, at the moment `draw_hook` waits for nothing, reads pad 1 (the stack's command 48) and gives it to
  the game as its pad (`rt_pad_live`);
- the run has no end.

**The buttons:** B is II, C is I, A is RUN, and X, Y, Z and MODE are SELECT.

**In ares,** with 602 presses (`--press`, through `pic_run.sh`'s `PRESS=`): the shots on and off, left and right.
The ship moves and fires. It picks up a power-up (the blue shot), and the score goes from 2,400 to 4,800. It plays on
well past the window's 600 frames (screenshots at ares frames 3,800-6,000), on code the window never traced.

## Next

1. A play window that starts the stage cleanly: at the stage's start, after its load, as a level would begin. Then
   the other stages the same way.
2. The game build's speed (above), and the screenshots against the PC.
3. Power-on through the title on the SH2: the CD loads the runtime models (docs/08's `RUN_DISC`) and the stage loads.
