# 85. v20: the white before the title; the console's wrong-colour frames read (19 Nov 2026)

**State: untested on the console.** The owner, on v19 (videos 892-898, PXL_20261119_121831791): "The intro is looking
great ... The rest of the game is looking better. But still lots of glitches and crazy tiles." and "The Main title also
looks great except for a white flash. If that's part of the design then please remove it".

## The white flash (v20's one change)
It is the game's: Mednafen (recording playL) fades every colour to white from frame 6,513 (colour 0 $049, 16 colours
white) to 6,540 ($1FF, 262 white) and holds it to ~6,576, then the title (START / CONFIG). In ares v19 held it 72
frames (the title's first pictures take longer). `eng.c eng_nowhite` (after `eng_noflash`, both picture paths): a picture
whose colour 0 is a grey (not black) with 8 or more colours white is shown black. Checked on the PC machine's colours,
every 15th frame of the recording's 75,000: only frame 0 (the boot) and 6,525-6,570 are such pictures; colour 0 is
otherwise black, a dark blue ($001 / $002 / $010 / $082) or, for a frame, $1FC (a flash in play: not grey); the white
backdrop at 4,545-4,575 has 4 white colours. ares v20: the intro's last picture, then black (ares 6,970-7,068), then the
title; play as before (ares 9,000 = engine 8,399).

## The wrong colours / crazy tiles in play (not changed)
The owner's videos, every 8th frame and every frame where something changes: stages 1-3 mostly right; the events are
the "B" kind - stage 2 (897 12.1-12.4 s: the arches and pillars orange, spreading over ~8 camera frames; 898 2.9 s gold
squares; 898 6.3-6.5 s: the walls white and orange, the shaft's grille white, ~8 frames). One found in ares (docs/84,
13,543) traced with `TRACE=1` and the converter's state per picture (`sap_bg5.patch` under `SAP_TRACE`: tile changes,
the 32X's palettes, entries left, list words, each palette's Genesis slot, the versions' hash): the merge placed
palettes again (their slots changed: `g` f32f1/22f100 -> f02f1/22f030), 365 entries and their tiles to send again - a
list of 4,107 words, run by the 68000 over ~7 frames (the next picture came 7 engine frames later). The tiles and entries
change as the list runs; the colours are kept for the swap at its end. For those frames the new tiles show in the old
colours, band by band - the console's spreading orange. (docs/84's trial, the colours first, showed the old tiles in the
new colours instead.) What would cure it: the list run inside a blank or two (it is paced, `GOT_MORE_RUN` / `REST` and the
68000's list speed), or the new tiles written to free slots and switched with the colours, or the 32X drawing the moved
palettes until the plane has them. Next round.

## Also
- The game's CONFIGURATION shows CREDIT (videos 893 / 894): the credits may be the game's own setting.
- `tools/saeng_disc.sh` (docs/84): refuses a BootCamp without command 104.
