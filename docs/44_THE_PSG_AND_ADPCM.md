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

**The ADPCM is barely heard in that stretch:** the game plays three samples in it. Checking it is still to come.

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

## Next

1. The ADPCM against the reference over a stretch with voices (the briefings).
2. The music and these together (`FIFO_CDDA=1 PWM_SOUND=1`), their levels.
3. On the console, the FPGA makes these samples. Its PSG and ADPCM in RTL come with the CPU's hardware page.
