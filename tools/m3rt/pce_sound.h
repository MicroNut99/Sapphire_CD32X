/* pce_sound.h - Sapphire docs/44: the PC Engine's PSG and ADPCM as samples (pce_sound.c) */
#pragma once
#include <stdint.h>

void psnd_write(unsigned reg, uint8_t v);             /* a PSG register write (0-15) */
/* one frame's samples at rate (fps frames a second) into out; the ADPCM from adram's start address at its
   rate (freq: $180E's low 4 bits) while playing. Returns the count */
int psnd_frame(const uint8_t *adram, unsigned start, unsigned length, unsigned freq, int playing, int16_t *out, int rate, double fps);

void psnd_nibble(int n);
