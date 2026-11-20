# 89. v24: the background's one-picture jump, again (19 Nov 2026)

**State: untested on the console.** The owner, of v22: "flashing screen and tile problems"; "FUCKING FIX IT".

## The console (every camera frame of 968, 969, 043508258)
968 (stage 1, ~75 s of play): three pictures with the background displaced - 48.2 s, 51.2 s, 84.9 s (the carrier's
top drawn a third of the screen down). docs/78's kind C, which v16 (docs/80) had cured in ares, not on the console.
The rest of the scan's events: the bomb's white disc / blue ball and other effects the game swaps every frame (see
the end), the game's own one-frame flashes.

## What still put the status bar's V at the top of a picture
VSPLIT's hint_vs puts the top entry back at line 223's H interrupt. ares (local DIAG `ARES_HINT_LOG=<frame>`: an H
interrupt not taken for 4+ lines, with the 68000's PC and mask), v23, stage 1:
- **command 79 (`fb_list`), a list that ends in a vertical blank, waits for the display (and the 32X's blank) before
  the swap with level 4 masked** - through line 223, so the top's H interrupt waited past the blank: then the swap's
  section at level 7 (the kept runs) - held 4+ lines at lines 10-81 of the picture. The 68000 touches no VDP address
  in those waits (the list's runs are done; the kept runs set their own).
- its op 1 (the pairs into the 32X's frame buffer) at level 4 too: no VDP there either.
- the playfield's V passing the status bar's ($01F in stage 1): the picture is one band, the engine turned the splits
  off, then on again a picture later - the top written when the command came, mid-picture.
ares counts only one-pixel jumps (the V's then differ by 1); the console's three are a third of a screen: its
68000 later, the same mechanism assumed - not proven.

## v24
1. `PAIRSH` (the build's copy of the stack, `tools/saeng_disc.sh`; `PAIRSH=0` without): `move.w #0x2300,sr` before
   fb_list's wait for the display and before the pairs - the H interrupt let in there.
2. `eng.c`: a picture of one band right after pictures with splits keeps the splits, each at its line with this V
   (up to 60 pictures in a row).

ares, recording playL, stage 1 (9,000-16,000): no H interrupt held 4+ lines in the picture by those waits (left: the
blank's own, and a few at level 4 in a list's run); **no picture with its top at the status bar's V** but those where
the playfield's V is $01F itself (v23: 4 such, v22: 4). One-frame faults: stage 1 9,400-9,900 none, the intro
1,600-1,820 none. The game 30 frames later from the boot on (the loading), the same in play.

## Not done: the effects the game swaps every frame
The bomb's explosion (white disc / blue ball), blinking faces and signs: on the PC Engine one at 60 a second, seen as
a blend; the 32X shows a picture every 2-3 frames and catches either at random. docs/88's hold of a red palette is
that cure for one case; the general one is next.
