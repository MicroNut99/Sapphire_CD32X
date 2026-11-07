/* ac_model.h - the Arcade Card for the M2 host (Sapphire CD32X, docs/06): four ports into 2 MB of RAM and the shift
 * register, as Mednafen's arcade_card.cpp (libretro beetle-pce b96c11e), which the trace checks it against. */
#ifndef AC_MODEL_H
#define AC_MODEL_H
#include <stdint.h>
typedef struct { uint32_t base; uint16_t offset, increment; uint8_t control; } ACPort;
typedef struct {
    ACPort port[4];
    uint32_t shift_latch; uint8_t shift_bits, rotate_bits;
    uint8_t *ram;                                    /* 2 MB */
    long reads, writes;
    const uint8_t *reg[32];                          /* (regions: the RAM as 32 pieces of 64 KB, 0 = not here) */
    int regions; long misses;                        /* regions: reads go through reg[]; misses: reads of a piece not here */
    int ram_ro; long ro_writes;                      /* ram_ro: the RAM is read only (the 32X's cart): a data write is
                                                        counted, not made (the address still steps) */
} ArcadeCard;
void ac_start(ArcadeCard *ac, const uint32_t *state /* 19 x u32: core acstate_get */, uint8_t *ram);
void ac_state(const ArcadeCard *ac, uint32_t *state);  /* the same 19 words */
/* the RAM as 64 KB regions (Sapphire docs/09: only those a stage reads are in the 32X's cart), read only */
void ac_start_regions(ArcadeCard *ac, const uint32_t *state, const uint8_t *const reg[32]);
uint8_t ac_read(ArcadeCard *ac, uint32_t a);           /* a: the hardware page's address ($1A00-$1AFF) */
void ac_write(ArcadeCard *ac, uint32_t a, uint8_t v);
/* banks 40-43 (physical address pa) are ports 0-3's data register */
static inline uint32_t ac_phys(uint32_t pa) { return 0x1A00 | ((pa >> 9) & 0x30); }
#endif
