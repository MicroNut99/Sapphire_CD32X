# 81. v17: v10's intro reading back (17 Nov 2026)

**State: untested on the console.** The owner, of v16's intro: "Intro still runs blocky. Maybe you should try going back
to using the first one that looked good" - v10's (docs/75), which he called better on the console (16 Nov).

## v17
`V10PIC=1` (default; `V10PIC=0` = v16): in a picture with no sprites (the intro's ship is all background), v10's
reading - a picture that finds a VRAM block under way makes the next 60 read from a frame's end (`eng_sync`), no
staging ahead - and no copy-on-write of the plane's tiles (`sap_bg3.patch` off). In a stage (sprites on) v16's reading
stays: v10's there gave a frame of scrambled tiles in ares (frame 9,047). Everything else of v11-v16 kept.
ares, every frame: the fly-in (1,650-1,780) no one-frame fault, stage 1 (9,000-9,300) none.

## v16 on the console (the owner's eight videos of 17 Nov, every frame scanned, 5 a second seen)
- Intro: the ship in blocks 8.4-9.6 s (checker, then torn rows); one frame of red lines at 10.1 s.
- Play: no background jump seen. Left: (B) single frames with the background in wrong colours (gold, orange, green) -
  about one in 10-20 s in every stage; (D) a screen's first frames garbled (CAUTION / WARNING text, stage starts);
  hit sprites still turning solid orange in the stage 2 boss (v14's HIT FLASH misses that case); 35.6-41.4 s of one
  video an orange, blocky background through the stage 2 boss's death - not yet compared with Mednafen.
