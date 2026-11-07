/* ac_engine_model.c - the EverDrive Arcade Card engine on the PC (docs/10): the protocol of ac_engine.h decoded,
 * the card itself ac_model.c's (Mednafen's, line for line), its RAM read from the cart through the region table.
 * fpga/ac_engine.sv is checked against this, read for read (fpga/sim). */
#include "ac_engine.h"

static void ace_region(AcEngine *e, int k)
{
    e->ac.reg[k] = e->unit[k] == ACE_NONE ? 0 : e->cart + ((uint32_t)e->unit[k] << 8);
}

void ace_reset(AcEngine *e, const uint8_t *cart)
{
    static const uint32_t zero[19];
    const uint8_t *const none[32] = { 0 };
    ac_start_regions(&e->ac, zero, none);
    e->cart = cart; e->misses = 0;
    for (int k = 0; k < 32; k++) { e->unit[k] = ACE_NONE; ace_region(e, k); }
}

uint16_t ace_bus_read(AcEngine *e, uint32_t off)
{
    const unsigned r = (off >> 1) & 0xFF;
    if (!(off & ACE_WR)) {                           /* a register read */
        if (r >= 0x80 && r < 0xE0) return 0xFF;      /* (the set-up registers: not readable, as the card's) */
        const long m = e->ac.misses;
        if (r < 0x80 && (r & 0xF) < 2) {               /* a data port: a byte in bank 31 is never read (0xFF) */
            const ACPort *p = &e->ac.port[(r >> 4) & 3];
            uint32_t i = p->base; if (p->control & 2) { i += p->offset; if (p->control & 8) i += 0xFF0000; }
            i &= 0x1FFFFF;
            const uint16_t u = e->unit[i >> 16];
            if (u != ACE_NONE && ((((uint32_t)u << 8) + (i & 0xFFFF)) & 0xFFFFFF) >= ACE_BANK31) {
                const uint8_t *g = e->ac.reg[i >> 16]; e->ac.reg[i >> 16] = 0;
                const uint8_t v = ac_read(&e->ac, 0x1A00 | r); e->ac.reg[i >> 16] = g;
                e->misses += e->ac.misses - m; return v;
            }
        }
        const uint8_t v = ac_read(&e->ac, 0x1A00 | r);
        e->misses += e->ac.misses - m;
        return v;
    }
    const uint8_t v = (uint8_t)(off >> 9);
    if (r >= ACE_REG_UNIT && r < ACE_REG_UNIT + 64) {
        const int k = (r - ACE_REG_UNIT) >> 1;
        e->unit[k] = (r & 1) ? (uint16_t)((e->unit[k] & 0xFF) | v << 8) : (uint16_t)((e->unit[k] & 0xFF00) | v);
        ace_region(e, k);
    } else if (r == ACE_REG_SHIFT_RAW) e->ac.shift_bits = v & 0xF;
    else if (r == ACE_REG_ROT_RAW) e->ac.rotate_bits = v & 0xF;
    else if (r < 0x80 || r >= 0xE0) ac_write(&e->ac, 0x1A00 | r, v);
    return 0xFFFF;
}
