/* cellstore.h - Sapphire docs/28: the FPGA's converted sprite cells (cellstore.c) */
#ifndef CELLSTORE_H
#define CELLSTORE_H
#include <stdint.h>
#define CS_CELL 288                     /* (rt_spr.h SPR_CELL) */
#define CS_ODD 128
#define CS_MASK 272
void cs_init(const uint16_t *vram);     /* the store from VRAM (32,768 words) */
void cs_vram(unsigned a, uint16_t w);   /* a VRAM write, as the FPGA commits it */
void cs_boundary(void);                 /* a frame boundary (the display starts): locks if armed */
void cs_release(void);                  /* the SH2: its picture before is on screen */
const uint8_t *cs_cell(unsigned c);     /* the cell as at the lock (the SH2's read) */
int cs_locked(void);
extern unsigned long cs_freezes, cs_updates;
#endif
