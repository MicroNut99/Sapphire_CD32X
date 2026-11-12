/* tpsg.h - Sapphire docs/54: the PC Engine's PSG played by the Sega CD's PCM chip. The HuC6280's six channels are
 * wavetable channels (a 32-step waveform, a 12-bit period, a volume and a balance; channels 5 and 6 can be noise); the
 * RF5C164 has eight channels that loop a piece of wave RAM at any rate, each with its level and pan. So each PSG channel
 * is one PCM channel: its waveform written into the wave RAM (64 cycles and an end mark), the period turned into the
 * chip's step and a loop start, the volumes into ENV and PAN. No CPU mixes anything.
 *
 * This file is the part that runs on the console (integers only; it makes register writes) and on the PC (the same
 * code, into tools/tower/rf5c_model.c). The PSG's registers come in as the game wrote them; tpsg_update() sends what
 * changed since the last call. */
#ifndef TPSG_H
#define TPSG_H
#include <stdint.h>
/* the wave RAM: TPSG_SLOTS pieces for waveforms, each TPSG_CYCLES cycles x 32 steps and 16 end marks, at TPSG_SLOT(n)
   (a waveform is written once and kept: a game has few - Sapphire 13); the noise's piece at TPSG_NOISE (TPSG_NOISE_N
   bytes of the PSG's own shift register, then 16 end marks) */
#define TPSG_CYCLES    64
#define TPSG_SLOTS     21
#define TPSG_SLOT(n)   ((n) * 0x900u)
#define TPSG_NOISE     0xC000u
#define TPSG_NOISE_N   0x3FF0u
typedef struct {
    /* the PSG as the game wrote it */
    uint8_t sel, gbal, lfofreq, lfoctrl;
    struct { uint16_t freq; uint8_t ctrl, bal, noise, widx, wave[32], dirty; } ch[6];
    /* what the PCM chip was last given (0xFFFF...: nothing yet) */
    struct { uint16_t fd, ls; uint8_t env, pan, on, noise, slot; } out[6];
    uint8_t noise_done;
    uint8_t vol;                                          /* the player's own volume, 0 (silent) - 8 (full): tpsg_volume */
    struct { uint8_t wave[32], used; uint16_t age; } slot[TPSG_SLOTS]; uint16_t clock;   /* the waveforms the wave RAM holds */
} Tpsg;
/* the chip's side, given by the host: a register of channel ch (0 ENV, 1 PAN, 2 FDL, 3 FDH, 4 LSL, 5 LSH, 6 ST), the
   channel's on/off, n bytes of wave RAM at a */
typedef struct {
    void (*reg)(void *u, int ch, int r, uint8_t v);
    void (*key)(void *u, int ch, int on);
    void (*ram)(void *u, uint16_t a, const uint8_t *d, unsigned n);
    void *u;
} TpsgOut;
void tpsg_power(Tpsg *t);
void tpsg_write(Tpsg *t, unsigned a, uint8_t v);          /* a PSG register write ($0800 + a, a = 0-9) */
void tpsg_update(Tpsg *t, const TpsgOut *o);              /* send the chip what changed */
void tpsg_volume(Tpsg *t, unsigned vol);                  /* 0-8: every channel's level scaled (a game's menu); the next tpsg_update sends it */
/* a tone's period (1-4095; 0 = 4096) as the chip's step and the cycles its loop holds; 0: too high to play (muted) */
int tpsg_tone(unsigned period, uint16_t *fd, uint8_t *cycles);
#endif
