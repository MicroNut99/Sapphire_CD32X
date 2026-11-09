# 43 - The music plays (9 Nov 2026)

**Correction (docs/45):** "stage 1 played" below was the game with RUN held, not the pad. The music is as stated.

docs/41 found that the music commands worked but the work folder's audio tracks were silent placeholders. The owner
supplied the disc's audio (`Session 1/Track 01.wav`, `Track 03-16.wav`: 44.1 kHz, 16-bit stereo).

**On the disc:**
- PC Engine track n becomes CD32X track n - 1: `work/music_cd32x/TrackNN.wav`, links to the uploads.
- That is about 30 minutes of music in 14 tracks. Game data: the work folder only.

**In ares** (live build, `EXTRA="BG32=1 FIFO_CDDA=1"`, from power-on):
- **The music:** sound from ares t = 70 s on, the level about 0.045-0.058 RMS (silence is 0.0166). It continues
  through the title, the player select and stage 1, with short gaps where the game stops a track for a load.
- **The pictures:** the title with its background, the player select, then stage 1 played with shots, power-ups and
  the enemies.

**The pad at the title:**
- A RUN press of a frame or two often goes unseen. The SH2 reads the pad when it draws a picture, at most 30 times a
  second, and a music command's seek holds it for a moment.
- Held presses (RUN for 30 frames every 1,000) start the game.
- On the console, the player's own presses are long enough.

**Seen:** the player select's borders show a strip of the Genesis plane and the picture counter's column. The
borders aren't drawn black there, since that screen uses the plane.

## Next

1. ADPCM (the voices) and the PSG (the effects).
2. The player select's borders.
3. docs/42's first-picture colours.
