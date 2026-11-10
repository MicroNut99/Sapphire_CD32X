# 48 - A whole stage against the reference (9 Nov 2026, under way)

The owner's task 2: a recorded game in ares against the reference over a whole stage, pictures and sound. This
records where the work stands; it was paused before the long run was compared.

## The tools

- **`tools/m3_stageref.py <recording> <from> <to> <out>`:** Mednafen (the libretro core) plays the recording from
  power-on, pokes included. From `<from>` it keeps each frame's picture at half size (`pics.npy`) and the sound
  (`snd.s16`, 44.1 kHz stereo). With `REF_NOCDDA=1` the core's CD audio is off, leaving the PSG and ADPCM alone;
  `REF_NOPICS=1` keeps only the sound. Output goes to the work folder.
- **`tools/m3rt/huchost.c`:** `HUC_MASKS=<u16 a frame>` and `HUC_POKES=<u32 frame, address, value>`. With these the
  stand-in plays the recording instead of the board's pad, writing the recording's RAM pokes before their frames.
- **ares `ARES_HUC_WAV=<file>`** (`fpga/ares/0009`, DIAG): the stand-in's sound as it makes it, in the game's time
  (raw s16 at 22,044 Hz, plus `<file>.n` with each frame's count).
- **`tools/m3_stagecmp.py <ref> <ares run> <first frame>`:**
  - Pictures: each screenshot against every reference picture (the closest, with the screen's place searched).
  - Sound: the ares run's audio mapped onto the reference's time.
  - With `FX=<ares s16>,<ref dir>`: the stand-in's effects frame by frame against the reference's.

## Stage 1 (`playL`, frames 8,150-21,000; state `sp/ps1.bin`)

**First run** (`work/ar_s1`, 15,000 ares frames, `BG32=1 PWM_SOUND=1 FIFO_CDDA=1`):

- **What it played:** the recording, through stage 1 to its boss.
- **Pictures:** where the shots are placed, 7-8% of pixels are apart at best. Shot 4,050 against reference frame
  8,764 is 5.0%, with the screen's place searched: x 66, line 0 (not docs/31's line 11).
- **The finding: the live build shows the game slower than the PC Engine.**
  - From ares frame 3,300 to 9,300, 1,960 reference frames went by: about **0.33 pictures a 32X frame**, about 20 a
    second.
  - Up to the boss it averages about 0.48. Heavy scenes are slower.
  - The FIFO player draws every picture, so the game slows down rather than dropping pictures. That is short of the
    owner's 30 a second.
- **Sound:** the real-time mix doesn't line up with the slowed game (loudness correlation 0.04):
  - the CD music plays in real time;
  - the effects come at the game's pace, and the PWM ring runs dry between them.
  - So the effects are compared in the game's time instead (`FX=`).

**Second run** (`work/ar_s1L`, 40,000 frames, `ARES_HUC_WAV`), to cover the whole stage at that speed: started, not
yet compared. Neither was the reference's effects-only capture (`work/stage1fx`).

## Next

1. Compare `ar_s1L` (pictures over the whole stage, `FX=` for the effects).
2. Find where the 32X's time goes in the live build (the FIFO player, BG32, the sprites), and drop pictures to hold
   the game's speed (30 shown a second, every other picture).
