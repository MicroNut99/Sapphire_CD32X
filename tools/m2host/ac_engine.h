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
 *   $1AC2, $1AC3, $1AC4               the poke address (cart byte address, 24 bits), low, middle, high byte
 *   $1AC5                             poke: v written to the cart at the poke address, which steps (docs/11: the
 *                                     game's writes to its RAM banks kept in the cart). Not in bank 31, not in
 *                                     the engine's window: refused (the address still steps)
 * Arming (docs/52): from its reset the engine answers every read with 0xFFFF and changes nothing, until three reads
 * in a row: register $1AF0, $1AEF, $1AF7, each with 0xA5 in the value field (ACE_ARM_OFF). The 32X boot ROM's
 * checksum reads the whole cart in order; through the window such a scan set the region table and poked bytes into
 * the cart where the garbage table pointed - the program, the pack. A scan in order never makes the three reads.
 * The runtime (rt.c) arms it before it sets the table.
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
#define ACE_REG_POKE_A     0xC2                      /* 0xC2-0xC4 */
#define ACE_REG_POKE       0xC5
#define ACE_NONE           0xFFFFu
#define ACE_ARM_OFF(r)     ((uint32_t)0xA5 << 9 | (uint32_t)(r) << 1)   /* (a read, not a write: bit 17 clear) */
#define ACE_ARM_1 0xF0
#define ACE_ARM_2 0xEF
#define ACE_ARM_3 0xF7
#define ACE_BANK31         0xF80000u                 /* bank 31 (the EverDrive's battery RAM): never read (0xFF), never poked */
static inline int ace_poke_ok(uint32_t a) { return a < ACE_BANK31 && (a < ACE_WINDOW || a >= ACE_WINDOW + ACE_SIZE); }

/* the PC's model of the engine (ac_engine_model.c): the cart as the engine sees it, and its state */
#include "ac_model.h"
typedef struct {
    ArcadeCard ac;                                   /* the ports and the shift register, as the card's */
    uint16_t unit[32];
    uint8_t *cart;                                   /* the cart's bytes, from address 0 (pokes write them) */
    uint32_t poke_a; long pokes;
    long ram_writes;                                 /* (docs/38) the card's RAM written into the cart by its data ports */
    long misses;
    uint8_t armed, arm_st;                           /* (docs/52) the arming: off from its reset */
} AcEngine;
void ace_reset(AcEngine *e, uint8_t *cart);
uint16_t ace_bus_read(AcEngine *e, uint32_t off);    /* a read at window + off: the word the cart bus sees */
#endif
