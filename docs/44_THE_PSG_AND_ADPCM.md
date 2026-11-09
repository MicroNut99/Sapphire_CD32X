# 44 - The PSG and ADPCM through the 32X's PWM (9 Nov 2026)

docs/43 brought the CD music. The rest of the PC Engine's sound is its PSG (the effects, six wave channels) and ADPCM
(the voices).

The cart can't put sound on the console's audio: the cart slot has no audio input. So the cart's CPU makes the
samples and the 32X plays them through its PWM.

## 1. The samples (`tools/m3rt/pce_sound.c`, in the stand-in)

**The PSG:**
- six channels: 32 five-bit samples, a 12-bit period at 3.58 MHz, DDA, channels 4-5's noise;
- volume in 1.5 dB steps, balance and main volume in 3 dB steps, as MAME's c6280 counts them;
- the I/O model gives each register write (`io_psg_hook`) with its place in the frame (`rt_io_until`, master clocks to
  the frame's end), and the frame's samples are made with each write at its time.

**The ADPCM:** the MSM5205's 4-bit ADPCM, over the nibbles the drive model's playback passed in the frame, stretched to
the frame's samples.

**The output:** 22,044 Hz, 16-bit mono (`huchost_audio`).

**Against the reference core** (`playL` frames 8,001-12,000, its audio captured through libretro):
- the strongest pitches are the same where the two runs are in step (382 / 387 Hz; 1,604-1,614 Hz);
- the loudness envelope correlates at 0.43. The runs drift apart: the stand-in has no lives held, the recording does;
- the PSG's gain is set to the reference's level (0.12).

**Second check, the stage 1 -> 2 window** (`sp/t12k.bin`, the stand-in with `HUC_CARD=` for the card's file; the
reference's audio over the same 1,699 frames):
- the loudness envelope correlates at **0.85**;
- from frame 1,400 on (stage 2's start), both run at about 3,000 RMS: 2,768 / 3,401, 3,099 / 3,282;
- the stand-in is silent for the first 600 frames, where the reference plays the boss's effects. The state carries no
  PSG registers, so the stand-in's channels start empty. From power-on this does not happen.

**The ADPCM, fixed:**
- The drive model's read address moves in jumps: it runs lazily to its next event. Taking "the nibbles passed in the
  frame" gave 72 frames with all of a voice packed into each.
- Now a play runs from the address it started at (`hw.addr`), at its own rate (32,087.5 / (16 - freq) nibbles a
  second), for its length as it started (`hw.length`), while the model says it plays.
- **Stage 1's start, frames 8,400-9,400**, where the voices are: the loudness envelope correlates at **0.87**. The
  levels side by side: 1,710 / 2,154, 3,529 / 2,430, 2,854 / 3,103, 95 / 82, 2,227 / 2,464, 0 / 0.
- Over frames 8,001-12,000 the level is 0.060 RMS against the reference's 0.079.

## 2. Into the cart (`fpga/ares/0006-ares-huchost-sound.patch`)

**The ring:** the board writes each frame's samples as PWM pulse widths (1 to 1,043 about 522) into a ring of 32,768
at cart `0x3F0000`.

**Register `0xcc`** (read through the engine's window) gives how many it has written.

## 3. The 32X (`PWM_SOUND=1`, with `FIFO_LIVE=1`)

**The secondary SH2:**
- sets the PWM to 22,044 Hz (cycle 1,045), both channels, an interrupt every 3 samples, and takes the PWM interrupt;
- `sec_pwm_irq` (`crt0.s`) fills the PWM FIFO from the ring, up to 3 samples, and gives the middle when the ring has
  nothing new.

**The master** switches it on at a picture, once 3,000 samples are written, playing 2,200 behind.

**In ares** (from power-on, played into stage 1, CD music off), the sound level rises over the silence (0.0166) in
bursts:
- at t = 27 s, the boot;
- through stage 1, t = 335-442 s, up to 0.063: the shots and explosions.

## 4. All of it together (`BG32=1 PWM_SOUND=1 FIFO_CDDA=1`, ares `ar_lv23`)

From power-on, played into stage 1:
- **The pictures:** the title, the player select and stage 1 (shots, power-ups, an explosion), as without the sound.
- **The sound:** 0.040-0.059 RMS from t = 75 s on, with silences where the game stops its track for loads. The
  music and the effects can't be told apart in that one figure.

## Next

1. On the console, the FPGA makes these samples. Its PSG and ADPCM in RTL come with the CPU's hardware page.
