/* ac_engine.h - the Arcade Card as hardware on the Mega EverDrive PRO (Sapphire CD32X docs/10): the protocol between
 * the SH2 (or the 68000) and the engine in the cart's FPGA, beside krikzz's SSF mapper (fpga/ac_engine.sv).
 *
 * The SH2s cannot write the cart, so every access is a 16-bit READ in the engine's window, cart 0x1C0000-0x1FFFFF
 * (256 KB in page 0's free part: window 0, always mapped whatever page window 1 shows; docs/03). The address carries
 * the operation; the low byte of the word read back is the answer:
 *
 *   window + (r << 1)                          read the card's register $1A00 + r (r 0-255): what ac_read returns,
 *                                              the data ports' auto-increment included
 *   window + 0x20000 + (v << 9) + (r << 1)     write v to register $1A00 + r: what ac_write does
 *
 * Registers $1A80-$1ADF do nothing on the card (Mednafen's ArcadeCard, tools/m2host/ac_model.c), so the engine uses
 * them for its own set-up, written only by the runtime (rt.c drops the game's accesses there, which the card ignores):
 *   $1A80 + 2k, $1A81 + 2k (k 0-31)   region k's place in the cart, in 256-byte units, low then high byte:
 *                                     card byte i is cart[(unit[i >> 16] << 8) + (i & 0xFFFF)]; 0xFFFF: not here (0xFF)
 *                                     a byte at or past 0xF80000 (bank 31, the EverDrive's battery RAM) reads 0xFF
 *   $1AC0, $1AC1                      the shift and rotate counts set as they are (a state's), without shifting
 * The card's RAM is read only (the game never writes it in play: docs/02 §4); a data port's write steps the address.
 */
#ifndef AC_ENGINE_H
#define AC_ENGINE_H
#include <stdint.h>

#define ACE_WINDOW   0x1C0000u                       /* the window's cart address */
#define ACE_SIZE     0x40000u
#define ACE_WR       0x20000u                        /* (offset) a write */
#define ACE_RD_OFF(r)      ((uint32_t)(r) << 1)
#define ACE_WR_OFF(r, v)   (ACE_WR | (uint32_t)(v) << 9 | (uint32_t)(r) << 1)
#define ACE_REG_UNIT       0x80                      /* + 2k: region k's unit, low byte; + 2k + 1 the high */
#define ACE_REG_SHIFT_RAW  0xC0
#define ACE_REG_ROT_RAW    0xC1
#define ACE_NONE           0xFFFFu
#define ACE_BANK31         0xF80000u                 /* bank 31 (the EverDrive's battery RAM) is never read: 0xFF */

/* the PC's model of the engine (ac_engine_model.c): the cart as the engine sees it, and its state */
#include "ac_model.h"
typedef struct {
    ArcadeCard ac;                                   /* the ports and the shift register, as the card's */
    uint16_t unit[32];
    const uint8_t *cart;                             /* the cart's bytes, from address 0 */
    long misses;
} AcEngine;
void ace_reset(AcEngine *e, const uint8_t *cart);
uint16_t ace_bus_read(AcEngine *e, uint32_t off);    /* a read at window + off: the word the cart bus sees */
#endif
