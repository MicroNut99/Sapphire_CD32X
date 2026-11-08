/* rand_ops.c <n> <out dir> - random accesses to every register of the EverDrive Arcade Card engine (docs/10), run
 * through the PC's model (tools/m2host/ac_engine_model.c): log.txt and mem.hex for tb_ac_engine.sv, as log2txt.py
 * makes them from a game's run. The cart is 2 MB of random bytes; the region table points into it, some regions
 * not here. Covers what a game's window may not: every control mode, the shift and rotate register, the mirrors. */
#include <stdio.h>
#include <stdlib.h>
#include "ac_engine.h"

static uint32_t s = 12345;
static uint32_t rnd(void) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

int main(int argc, char **argv)
{
    const long n = atol(argv[1]); char p[512];
    static uint8_t cart[16 << 20];                          /* (2 MB of random bytes, mirrored as the testbench's) */
    for (int i = 0; i < (2 << 20); i++) cart[i] = (uint8_t)rnd();
    for (int i = 2 << 20; i < (16 << 20); i++) cart[i] = cart[i & 0x1FFFFF];
    snprintf(p, sizeof p, "%s/mem.hex", argv[2]); FILE *m = fopen(p, "w");
    for (int i = 0; i < (2 << 20); i += 2) fprintf(m, "%02x%02x\n", cart[i], cart[i + 1]);
    fclose(m);
    snprintf(p, sizeof p, "%s/log.txt", argv[2]); FILE *f = fopen(p, "w");
    static AcEngine e; ace_reset(&e, cart);
#define OP(o) do { const uint32_t o_ = (o); fprintf(f, "%05x %04x\n", o_, ace_bus_read(&e, o_)); } while (0)
    for (int k = 0; k < 32; k++) {                          /* the table: units in the first 1.9 MB, a few not here */
        const uint32_t x = rnd() & 7;                       /* (1 in 8 not here, 1 in 8 reaching into bank 31) */
        const uint16_t u = x == 0 ? 0xFFFF : x == 1 ? (uint16_t)(0xF7F8 + rnd() % 0x100) : (uint16_t)(rnd() % 0x1D00);
        OP(ACE_WR_OFF(ACE_REG_UNIT + 2 * k, u & 0xFF)); OP(ACE_WR_OFF(ACE_REG_UNIT + 2 * k + 1, u >> 8));
    }
    for (long i = 0; i < n; i++) {
        const uint32_t x = rnd(); const unsigned r = (x >> 8) & 0xFF, v = (x >> 16) & 0xFF;
        switch (x & 7) {
        case 0: case 1: case 2: OP(ACE_RD_OFF((x >> 8) & 0x71)); break;           /* data ports (and mirrors) */
        case 3: OP(ACE_RD_OFF(r)); break;                                         /* any register */
        case 4: OP(ACE_WR_OFF(r & 0x7F, v)); break;                              /* port registers */
        case 5: OP(ACE_WR_OFF(0xE0 | (r & 0x1F), v)); break;                      /* the shift register's */
        case 6: OP(ACE_RD_OFF(0xE0 | (r & 0x1F))); break;
        case 7:
            if ((x >> 24) & 1) {                                                 /* a poke (docs/11), in the 2 MB */
                const uint32_t a0 = e.poke_a;
                if (r & 1) OP(ACE_WR_OFF(ACE_REG_POKE_A + (r >> 1) % 3, ((r >> 1) % 3) == 2 ? v & 0x1F : v));
                else { OP(ACE_WR_OFF(ACE_REG_POKE, v));
                       for (uint32_t m = 2 << 20; m < (16u << 20); m += 2 << 20) cart[m + (a0 & 0x1FFFFF)] = cart[a0 & 0x1FFFFF]; }
                break;
            }
            OP(ACE_WR_OFF(r < 0x80 || r >= 0xC2 ? (r >= 0xC2 && r <= 0xC5 ? 0xE4 : r) : 0xE4, v)); break;   /* any other */
        }
    }
    fclose(f);
    printf("%ld operations, %ld reads of a region not here, %ld pokes made\n", n + 64, e.misses, e.pokes);
    return 0;
}
