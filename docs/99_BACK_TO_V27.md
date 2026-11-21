# 99. Back to v27 (21 Nov 2026)

The owner, 21 Nov: "revert to v27 - that was before you and the other AI that fucked it ALL".

`saeng/` and `tools/` are v27's again, file for file (`git checkout d5900df -- saeng tools`; `tools/sap_bg6.patch`
removed). The kit to play is v27's own, as built on 20 Nov and already on his PC: `console_sd_v27\`
(`Sapphire_EDCD.32X` md5 `8327a378...`, the core `8ac45e06...`). Nothing was built for this.

## What is withdrawn, and why
- **v28-v30** moved and restyled the subtitles he had (he had asked for more lines, not for other ones), and put the
  intro's lines in the wrong place.
- **v31-v33** put the subtitles back and took the intro's lines out again, but v32 also changed the picture
  (docs/97, the cover by parts) in the same kit. **On the console, a v32 or v33 kit: the CONFIGURATION screen with
  a portrait in white lines over its text, the title after it washed out, stage 1's city bright blue and white**
  (his videos PXL_20261121_164900826 and _165026467). In ares the same builds looked right. The cover by parts is
  the first suspect - the only change of those kits to how the picture is coloured - **not confirmed: he stopped the
  work before it was traced.**
- docs/93-98 stay as the record of those rounds; nothing of them is in the source.

## Corrected the same day: what broke v32 / v33 was the menu's wipe, not the cover
The suspicion above was wrong, and so was what the owner was told. Found by running the v33 kit's ROM beside v34 in
ares (recording playL, pictures at 8,380 / 9,300 / 10,400): **v33 shows the plane's side columns - the briefing spilling
past the picture's edges, junk left and right of the stage** (9,088 and 15,680 lit pixels outside the picture at 8,380;
v34: 4 and 0). docs/96's "fix" for the letters at the screen's sides made `gm_wipe` clear all 320 columns of the 32X's
frame buffer - **and the 32 columns either side hold the 32X's black mask over the Genesis plane's wider picture.**
Every Start press, menu close and subtitle change wipes: from then on the sides were open. In the owner's videos: the
title's floor letters with a third letter each side, CONFIGURATION's first letters again at the right edge, marks at
the right of stage 1. The earlier ares checks of v31-v33 had it in view (44 lit pixels left of the picture after the
menu closed, a column of marks left of the briefing) and it was passed over.
The cover by parts (docs/97) is neither cleared nor blamed by this: untested on the console.
**The lesson for the letters at the sides: shorten the hint line only; never widen the wipe.**

## For whoever takes this up next
- Start from v27. One change a kit, and no change to the picture in a kit made for something else.
- His subtitles are v13's: the briefing's lines in the band under the text box (rows 84 / 96), plain white, no box.
  He does not want them moved or restyled. He does not want lines in the intro.
- Left open in v27, seen in his videos of 21 Nov: the options menu's STEADY BLINK hint leaves letters at the
  screen's sides (docs/96 has the cause); palettes placed again show wrong colours for a quarter second (docs/97:
  reproduced in ares with `FREELESS`, but its cure is what is withdrawn here); ENGLISH TEXT is not saved.
- An ares run that looks right says nothing of the console (rule 3).
