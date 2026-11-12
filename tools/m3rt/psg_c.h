/* psg_c.h - Sapphire docs/50: the PSG in C, clock for clock as fpga/huc/psg.sv (Mednafen's behaviour) */
#ifndef PSG_C_H
#define PSG_C_H
#include <stdint.h>
typedef struct {
    uint16_t frequency; uint8_t control, balance, noisectrl, widx, dda, vl[2]; uint8_t wave[32];
    int32_t counter, noisecount; uint32_t fcache, nfcache, lfsr; uint16_t accum;
} PsgCh;
typedef struct {
    PsgCh ch[6]; uint8_t select, gbal, lfofreq, lfoctrl, vu_which, vu_latch, vol_pending; uint16_t vu_counter;
} PsgC;
void psg_c_power(PsgC *p);
void psg_c_clock(PsgC *p);                         /* one PSG clock: the master clock / 3 */
void psg_c_write(PsgC *p, unsigned a, uint8_t v);  /* a register write (0-15), after the clocks before it */
void psg_c_out(const PsgC *p, int32_t *l, int32_t *r);   /* each side's level (pce_psg.cpp's dbtable, volume 1.0) */
#endif
