# 71 - The console, played through; music in the attract demos (14 Nov 2026)

**The console** (kit `console_sd_v5`, fit 6): the owner played the whole game - the intro, the title, the attract demos,
stages 1-5, ALL CLEAR, the ending, the credits, NAME ENTRY, the ranking - and filmed it: 36 videos, 1,750 s. All of them
looked at here at one frame a second (39 contact sheets), the suspect places at full size.

## 1. Music in the attract demos (the owner: "add CDDA to the attract mode demos")

The game plays its demos silent (Mednafen, `AUDIO_LOG=1`, no pad: CD-DA stops at frame 8,381 after the title's jingle
and starts again at 20,004 with the next title). Base RAM `$069E` is 1 in a demo and 0 in a game, `$069C` the stage
less one (RAM dumps of both, `RUN_DUMP_EVERY`). `sap_tick` (eng.c) reads both every 30 frames: in a demo's stage (the
status bar's CR) `eng_audio` plays that stage's own track, looped, when the game plays nothing, and stops it when the
demo ends. The stages' tracks start at LBA 11,339 / 29,473 / 47,670 / 65,804 / 83,973 (the boss's 111,300).
ares, no pad, the music disc: silence 22 s after the title's jingle (the briefing), then 67 s of music through the demo,
silence at the ranking, music again in the next.

## 2. What the videos show, checked against Mednafen

A run of the play recording in ares to frame 75,000 with a shot every 60 frames (1,221 shots, each against Mednafen at
its engine frame; `/tmp/e4c/rowprof.py`): median 1.8 % of pixels off, mean 4.9 %, 195 shots over 10 %.

| Seen on the console | In ares too? | Cause |
|---|---|---|
| Wrong colours for whole areas (a room's grey floor yellow or teal, a grey floor red, a blue sky dark red; the boss's death flames blue-grey) | yes | the Genesis' 60 colours: scenes with 16 palettes / 232 colours (`bg2_host`, frames 30,500-30,716: 10.8 % mean, 57 % worst, the same with or without the 32X's palettes at this budget). The merge's choice of colours is the fault to mend: not done here |
| White or black lines across the picture for a moment (stage 3) | yes (frame 48,060) | not found yet |
| The background missing at a stage's start or a briefing's (black, sprites only) for a moment | yes (22,140; 7,800) | the plane held covered until its first list is whole |
| A row of wrong tiles just above the status bar; a row of the status bar's text at the top of the picture (stage 5's CAUTION) | no (ares: each screen row's error is flat, 4-6 %) | console only: the V split late on the console - not mended here |
| A thin dashed line down the right edge of the screen (the left on ALL CLEAR) | no (no CRAM write in the display in 12,000 ares frames: `ARES_CRAMDOT=1`, ares local) | console only: not found |
| ALL CLEAR: the BONUS lines doubled every few seconds | not reached by the recording | - |
| The boss's death: orange blocks over the screen | - | the game's own effect (Mednafen shows it) |
| The title's "A3" / "0E" | - | the game's own floor markings |

The ending, the credits and NAME ENTRY are past the recording's end (stage 5's start): no Mednafen picture to hold
them against yet.

## 3. Kit

`console_sd_v6`: the launcher with the demos' music; fit 6's `mega-pro.rbf` and the disc as v5.

## Open

- The colours' merge (section 2's first row) - the biggest fault by far, in both the console and ares.
- The lines across the picture; the V split's lateness and the dashed line on the console.
- A recording to the game's end, for the ending's pictures.
