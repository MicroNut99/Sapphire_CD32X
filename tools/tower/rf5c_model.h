/* rf5c_model.h - Sapphire docs/54: the Sega CD's PCM chip (Ricoh RF5C164) as a model for the PC: eight channels, each
 * stepping through the 64 KB wave RAM by its FD (11 fraction bits), a 0xFF byte sending it to its loop start; a sample is
 * sign (bit 7 = positive) and 7 bits, times ENV, times a side's PAN nibble, >> 5. One output sample every 384 clocks of
 * the Sub-CPU's 12.5 MHz: 32,552 Hz. (The arithmetic is the documented chip's, as MAME's rf5c68 has it.) */
#ifndef RF5C_MODEL_H
#define RF5C_MODEL_H
#include <stdint.h>
typedef struct { uint8_t env, pan, start; uint16_t step, loopst; uint32_t addr; uint8_t on; } Rf5cCh;
typedef struct { Rf5cCh ch[8]; uint8_t ram[65536]; } Rf5c;
void rf5c_key(Rf5c *c, int ch, int on);                  /* register 8's bit: on restarts the channel at its ST */
void rf5c_sample(Rf5c *c, int32_t *l, int32_t *r);       /* one output sample (before the DAC's clamp) */
#endif
