# 41 - The music, a first try (9 Nov 2026)

docs/40's live build has no sound. This round starts on the CD audio. In play, the music is the game's own CD tracks
(docs/04).

## 1. The game's CD audio as FIFO entries (`tools/m3rt/huchost.c`)

**What the stand-in reports:** after each frame, the drive's audio state: playing, its start sector and its mode. When
it changes, the stand-in adds an entry:
- type B;
- x = 1 if the track loops;
- d = the PC Engine track (found from the disc's table), or 0 for a stop.

**From power-on with the recording's pad:**

| Picture | Event |
|---|---|
| 1,348 | track 15 |
| 6,586 | track 12 |
| 6,909 | track 13, looped |
| 7,378 | stop |
| 8,112 | track 3, looped |

That is the intro, the title, then stage 1.

**On the real cart**, these come from the CD unit's commands through the mailbox to the SH2.

## 2. The music on the CD32X disc (`tools/m3_music.py`)

The PC Engine's audio tracks 3-16 become `TrackNN.wav` for `build.sh`'s `music/`. PC Engine track n is CD32X track
n - 1, because the CD32X's track 1 is its data. That is 343 MB, in the work folder only.

## 3. The SH2 (`FIFO_CDDA=1`, with `FIFO_LIVE=1`)

**What it does:** `fifo_play`'s type B calls the stack's command 45, or 54 to stop, with `cdr_busy` set.

**Not working yet:**
- **No sound in ares:** the audio level stays at its empty value.
- **The title's background differs from run to run:** brown tiles with the 14 music tracks on the disc, black
  without them. §4 shows that neither is right.

So docs/40 §5's "fixed" was half right:
- the division by zero was real, and is fixed;
- the run I took for the proof had no music on its disc.

**`FIFO_CDDA` is off by default.** The music stays out of the work folder's build tree (`work/music_cd32x.off`,
`work/bc3_music_full`) until this is understood.

## 4. The title's background is a picture the Genesis plane doesn't show

The reference core's title (`playL` frames 6,600-7,100) has a full background behind the logo: the hangar, with the
ship. Ours shows black or broken tiles there. The PC's own picture build shows the same:
- from the runtime (`rt_pc RUN_BG2`, `sp/pon.bin`, picture 6,499);
- from the live FIFO (`RUN_FIFO`).

So:
- **The title is not ares or the music.** "Brown pattern" or "black" only depends on which pictures get drawn.
- **docs/40 §5's division by zero** was a real fix, but not this.
- **Measured** (`rt_pc RUN_V32DUMP`, picture 6,850, `MWR` 0, a 32 x 32 map): the visible 32 x 30 entries use 679
  different tiles and all 16 palettes, which is 256 colours. The Genesis plane has 4 palettes of 16. No tile
  conversion can fit that.
- **The background art fails:** the title's background is a whole-screen picture, and `rt_bg2`'s Genesis plane (its
  tiles and 4 palettes of 16 colours) does not show it. The stages' and briefings' backgrounds come out right.

## 5. Found: the music works, but the tracks are silent

**Command 45 from the FIFO works.** In ares, with the stack's test tone as the only music track:
- the SH2's music commands play it, with sound at ares t = 74 s and 105 s;
- with the full disc, the SH2's log (`cdda_log`) shows the calls answered (COMM12 = 1).

**There is no sound because the work folder's audio tracks are silent.** `work/disc/a01.bin` and `a03.bin`-`a16.bin`
are all zeros: the placeholders docs/02 made, since only the data tracks (`Track 02.iso`, `Track 17.iso`) were
uploaded. The real music needs the disc's audio tracks from the owner.

**The title on the music disc** (§3's broken background) was the background of §4, now drawn by docs/42's `BG32=1`.

## Next

1. The disc's audio tracks (the owner): then `tools/m3_music.py` again, and command 45 from the FIFO, and the sound in ares.
3. ADPCM and PSG.
