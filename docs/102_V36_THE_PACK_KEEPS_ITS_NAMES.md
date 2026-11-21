# 102. v36 = v35 + one change: a pack placed again keeps its names (21 Nov 2026)

**State: untested on the console.** The owner, of docs/100's list: "the brief wrong-colour flashes, the still pictures
of 0.13-0.4 s - fix these, why wait?" (And, of the letters the options menu leaves at the screen's sides: "totally
forget this" - dropped; not to be raised again.)

## One fault, measured in ares (v35, recording playL, stage 1 = ares 9,000-15,480)
- **The still pictures are there in ares**: `--swaps` (a mark for each vblank with a new 32X picture). Stage 1: 1,904
  pictures, a new one every 3.4 frames; **five gaps of 28-34 frames** (9,096 / 11,397 / 11,733 / 12,337 / 13,334).
- A gap, frame by frame (`--dump-sdram` each frame, the program's counters): a picture given; 16 frames with none; then
  **one list of 13,411 words**, which the 68000 runs for 14 frames - ~550 VRAM words a frame (ares `ARES_VRAMRATE`,
  local: 350-580 in the picture's lines, ~170 in the blank's; the list is in runs of 8 words for the V splits).
- The original at that moment (the PC machine, 10,680-10,790, every frame): nothing of the kind - a few entries a
  frame. The long list is the port's own: `TRACE=1` (a diagnostic build, `SPR_BC` 208 for room) shows **the palettes
  placed again** - nine times from 11,000 on, each a list of 9-16,000 words, the pack that comes out mostly the old
  one under other names (p0 in Genesis palette 2 and p9 in 1 before; p0 in 1 and p9 in 2 after).
- So the still picture and the wrong colours are one event: the screen's tiles converted and its entries written
  again. With a 32X palette free for every palette moved the 32X covers it (docs/86) and only the pause is seen;
  without, the colours too.

## docs/97's cover by parts does not cure it
Tested again alone on v35 (`FREELESS=5`, pictures every 4 frames 8,600-12,400): all-or-nothing orange at 9,064 /
11,160 / 12,128; by parts orange at **9,156 / 11,256 / 12,204** - each ~90 frames later, as long. The covered move is
put back and tried again, and comes uncovered when something else refuses the cover (`why[0]`). docs/97's table looked
at 8,600-9,100 only. `tools/sap_bg6.patch` is not in the repository any more.
COLOURS FAST (no 32X palettes for the background) the same test: one orange event of 8 frames, colours merged: no cure.

## The change (`tools/sap_stable.patch`, `SAP_STABLE`; `STABLE=0` builds without)
`rt_bg2.c colours()`: after `colours_do` has made its pack, **`sap_stable` renames it to fall on the old one**. The
pack itself is untouched - the same palettes together, the same colours shown, the same indices lossy:
1. the Genesis palettes' numbers permuted (24 ways at most) so that the most map entries keep their Genesis palette;
2. in each, the 15 slots permuted so that the most entries keep their slot (a colour new to the palette: where the
   Genesis's CRAM has it already, else a free slot);
3. a palette back in its old Genesis palette with every index on its old slot is not touched (BG2_SAMEMAP's rule,
   now met far more often); one with some indices moved has those converted again, no more.
Not while a palette owns a Genesis palette (BG2_PRIV), never the HUD's palette. In ROM (`.rodata.sap_cover`); scratch
in `colb_v` / `colb_c`. SDRAM code ends 0x06005F40 of 0x06006000, bss 0x0603E6B0.

## Measured
**The PC harness** (Gate of Thunder's `tools/bg2` `bg2_host` built from this build's `rt_bg2.c`, `work/bg2s`; the PC
machine's dumps every 4th frame; `NOSTABLE=1` = without):
| | without | with |
|---|---|---|
| stage 1 (8,400-14,800, 1,601 pictures): lists of 4,000+ words or 600+ entries | 18 | 12 |
| tiles converted / entries written | 29,770 / 34,196 | 25,010 / 26,437 |
| pixels off the original's background, mean (the HUD's rows count: not on the plane) | 7.243 % | 7.243 % |
| the two runs' pictures against each other | | equal in the first 1,322; 0.1-0.3 % apart after, 5.6 % at most in the last 23 (a merge's nearest colours, chosen otherwise - as far from the original either way) |
| intro, title, briefing (1,000-7,000, 1,501 pictures): mean off the original | 0.943 % | 0.944 % |

**ares** (recording playL):
| | v35 | v36 |
|---|---|---|
| `FREELESS=5` (a second player's sprites take the cover's room), stage 1 to 12,400: orange pictures | at 9,064 / 11,160 / 12,128 | **at 9,060 only** |
| stage 1 from 11,000, trace builds: lists of 3,000+ words / the words in them | 9 / 109,711 | 7 / 48,192 |
| stage 1's gaps of 24+ frames / of 16-23 | 5 / 0 | 3 / 3 |
| stage 1's pictures (a new one every) | 1,904 (3.40 frames) | 1,958 (3.31) |
| 1,000-15,500, a picture every 20 frames: short wrong-colour events | 4 | the same 4 |
| the engine's frame at ares 15,480 | 14,807 | 14,810 |
| lit pixels outside the picture, twelve pictures to 15,400 | 0-8 | 0-4 |
| specks before the intro's city (RUN at 640) | 0 | 0 |

**What it is and is not.** Fewer moves to cover, so the cover fits more often: the flashes fewer. The still pictures
hardly shorter: three of 24-29 frames and three of 16-21 where there were five of 28-34 - a covered move's pause is
not the list's length alone (the 32X's cover of the screen, the pictures held for it).

## Open
- **The still pictures.** What a gap is made of besides the list is not measured yet (the cover's drawing, the
  pictures held at N + 1, `eng_even`). Candidates: the entries off the screen later (a re-placement rewrites the map's
  ~2,400 entries, ~900 are shown); the 68000's ~550 words a frame (the stack's H interrupt and runs of 8).
- A pack that must really change (a palette no longer fits its Genesis palette) still moves palettes; a repack that
  looks for the fitting pack nearest the old one is not written.
- Two players in ares are still `FREELESS`, not two players.
