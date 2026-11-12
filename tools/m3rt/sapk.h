/* sapk.h - Sapphire docs/53: a data-track sector from the cart's pack (tools/sa_pack.py, "SAPK" version 2), read by the
   cart's physical addresses: what the FPGA's CPU side does (fpga/huc/pak_read.sv is checked against this), and what
   the stand-in does with the packed file as its cart (HUC_SAPK). saspeed's pk_sector reads the same pack through
   the 32X's windows. */
#ifndef SAPK_H
#define SAPK_H
#include <stdint.h>
#define SAPK_DIR 0x440000u                     /* bank 8 + 0x40000 */
typedef struct { const uint8_t *cart; uint32_t size, reads, bad; } Sapk;
int sapk_open(Sapk *k, const uint8_t *cart, uint32_t size);        /* 1: the directory is there */
int sapk_sector(Sapk *k, uint32_t sector, uint8_t out[2048]);       /* 1, or 0: out of range, a bad piece or a bad sum */
#endif
