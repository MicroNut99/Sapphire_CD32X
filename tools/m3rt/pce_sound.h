/* pce_sound.h - Sapphire docs/44: the PC Engine's PSG and ADPCM as samples (pce_sound.c) */
#pragma once
#include <stdint.h>

void psnd_write(unsigned reg, uint8_t v);             /* a PSG register write (0-15) */
/* one frame's samples at rate (fps frames a second) into out; the ADPCM nibbles from (a0, n0) to (a1, n1) of adram
   when playing. Returns the count */
int psnd_frame(const uint8_t *adram, unsigned a0, int n0, unsigned a1, int n1, int playing, int16_t *out, int rate, double fps);
