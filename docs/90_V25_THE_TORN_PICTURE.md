# 90. v25: the torn picture (20 Nov 2026)

**State: untested on the console.** The owner's v24 videos 981 (stage 1), 983 (stage 2), 985 (stage 3), two players, and:
"Analyse for lost tiles, frames and glitching".

## Every camera frame (scan.py, then the events at full size)
| kind | where | what |
|---|---|---|
| torn picture | 981 27.50; 983 24.53, 47.67; 985 15.43, 24.23 | for one TV frame the playfield from a line part-way down is another part of the map (981: HOTEL twice) |
| status bar garbage | 985 24.23, 64.00 | the bar drawn as map tiles for one frame |
| top lines | 983 55.27, 55.47 | a light-blue band across the top few lines |
| the game's | everywhere | the bombs' white / blue, faces and signs blinking, the 2P sprites (docs/89's last section) |
| ending | 985 74-77 | black, then the high-score table: the game over, not a hang |

v24's line-223 fix (docs/89) does not touch these: they start below the top.

## The cause found in ares
DIAG `ARES_PCWIN=<lo>:<hi>:<from>` (the lines of a picture the 68000 spends in a range; prints the code there) and
`ARES_VLATE`. The listener runs at **RAM = object offset + 0xFF0EDE** (not + 0xFF0E8C as docs/89 took: its
"pairs at level 7" was the swap block).
- command 79's swap block (level 7): when the 32X showed the new buffer at once ("shown already"), the **kept runs -
  the plane's held rows, V, colours - ran there and then, in the middle of the picture**, with the H interrupt held (ares
  v24, stage 1: H interrupt held 4+ lines at lines 10-81 and 221 there). Under the beam the lower part is drawn with the
  new map / V, the upper with the old: the torn picture; past line 203 the bar's split is missed: the bar's garbage.
  ares does it quickly (a line or two); the console, its VDP slower in the display, longer.

## v25 (`ARMWAIT`, the build's copy of the stack; `ARMWAIT=0` without)
- "shown already" in the display: the kept runs armed for the next vblank (vblank_int runs them), not run.
- a command 79 that comes while kept runs are armed waits for that vblank (H / V interrupts in, ~2.5 frames at most).
ares, recording playL, stage 1 (9,000-16,000): the kept runs never in the picture (296 samples, all at fb_overdue's
empty test); H interrupts held in the picture: 10 (level 4, a list's run), none at level 7; no jump; one-frame faults
stage 1 9,400-9,900 and the intro 1,600-1,820: none; stage 2's start, the briefing, the dialogue right; the pace v24's.
The 32X can now show its picture up to one frame before the plane's held changes: not seen in ares.
