# 35 - The top line's scroll, on time (8 Nov 2026)

docs/33 found line 0 a picture late in 3 of 21 screenshots: line 0 showed the picture's line 1. The owner said yes to
the stack change ("Do you want commands... yes").

## What happened (ares, `ARES_VSRAM_LOG=<frame>`: VSRAM 0's writes with the line they land on)

A DIAG hook in ares_android's VDP (`fifo.cpp`, branch `sapphire-ac-engine` only) prints VSRAM 0's writes and the
line each lands on. From frame 3,900, a normal frame's writes are:

| Line | What writes VSRAM 0 |
|---|---|
| 202 | the status bar's split |
| 223 | `hint_vs` putting the top back |
| 225-230 | command 79's kept runs, the new top, at the swap's vblank |

**22 times, the new top came at line 0.** The kept runs had run from command 79 itself, not from the vertical blank's
interrupt. The order was:
1. the list ended in the Genesis's blank;
2. `fb_list` waits for the Genesis's display to start, then writes the 32X's FS;
3. the 32X's own blank had not ended yet, so the 32X swapped at once;
4. the command saw it swapped and ran the kept runs itself, at line 0.

## The change (CD32X-BootCamp branch `vsswap`, commit `5403e34`; ledger §24b's addition, docs `f81fc3f`)

In the `DX-VSSWAP` block: ops 2 / 3 also wait for the 32X's blank to end (FBCTL bit 15) before the FS write. There is
no new command or number, `check_boot.py --update-genplane` was run, and docs/03 notes VSSWAP on command 79's row.

| From frame 3,900 | Before | After |
|---|---|---|
| VSRAM 0 written at line 0 | 22 | **0** |
| Screenshots 4109, 4155, 4316 (line 0 wrong) | 84, 79, 94 pixels | right |

## What is left: the status bar's split comes late

- **How often:** in about 1 picture in 10 (41 from frame 4,040 on), the split due at line 202 lands on 203-216. Line
  203 then shows the wrong band (256 pixels), or several lines do (3,082 pixels at 4109 in one run).
- **Why:** the H interrupt is masked while command 79 runs a list, and some lists sent at once still run past line
  202. docs/03 says so for every command that touches the VDP.
- **Tried, the SH2's estimate of a list's time:** 6 words a line instead of 10 (`LIST_WPL`). Fewer late splits (54 → 31
  from 3,900), but errors elsewhere, so the default stays 10.
- **Tried, ledger §31's HWIN:** Dracula X's design, which lets the H interrupt in before each control run of command
  79's list. It is in the ledger, but its code is in no repository here, so it was written from the ledger's text.
  Every screenshot then had one wrong 8-pixel tile in the status bar's first row. So it was taken out again: the
  branch has no HWIN.

## Screenshots now (21, frames 4040-4500)

- **Exact:** 15.
- **The status bar's band:** 4040 (55 pixels), 4109 (3,082), 4500 (256).
- **The boss's colours:** 3 (docs/29).
- **Pictures:** 279 of 291 at 2 refreshes.
