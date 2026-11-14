/* conv.c - DIAG for docs/45: what converting the PC Engine's changed VRAM on the master SH2 would cost.
 * A background tile (16 words: rows of planes 0/1, then of planes 2/3) to a Genesis tile (8 rows of 8 nibbles, the
 * left pixel high), and a sprite cell (64 words: 16 rows of each plane in turn) to 16 rows of 16 bytes (the pixel's
 * 4-bit colour, 0 see-through). By tables built once; nothing here is used by the game. */
#include <stdint.h>
#include "conv.h"

#define SDTEXT __attribute__((section(".sdtext")))
static uint32_t spread8[256];                 /* a plane byte: bit 7 (the left pixel) to bit 28, ... bit 0 to bit 0 */
static uint32_t spread4[16];                  /* a plane nibble: bit 3 (the left pixel) to bit 24, ... bit 0 to bit 0 */
static uint32_t spreadb[256][2];              /* a plane byte: its 8 pixels' bits as two longs of 4 bytes (bit 0 each) */

void conv_init(void)
{
    for (int b = 0; b < 256; b++) { uint32_t v = 0; for (int x = 0; x < 8; x++) if (b & (0x80 >> x)) v |= 1u << (28 - 4 * x); spread8[b] = v; }
    for (int n = 0; n < 16; n++) { uint32_t v = 0; for (int x = 0; x < 4; x++) if (n & (8 >> x)) v |= 1u << (24 - 8 * x); spread4[n] = v; }
    for (int b = 0; b < 256; b++) { spreadb[b][0] = spread4[b >> 4]; spreadb[b][1] = spread4[b & 15]; }
}

SDTEXT void conv_tile(const uint16_t *t, uint32_t *out)
{
    for (int y = 0; y < 8; y++) {
        const uint16_t a = t[y], b = t[y + 8];
        out[y] = spread8[a & 0xFF] | spread8[a >> 8] << 1 | spread8[b & 0xFF] << 2 | spread8[b >> 8] << 3;
    }
}

SDTEXT void conv_cell(const uint16_t *c, uint32_t *out)
{
    for (int y = 0; y < 16; y++, out += 4) {           /* (by bytes: 8 pixels from 4 table pairs, docs/45) */
        const uint16_t p0 = c[y], p1 = c[y + 16], p2 = c[y + 32], p3 = c[y + 48];
        const uint32_t *a = spreadb[p0 >> 8], *b = spreadb[p1 >> 8], *d = spreadb[p2 >> 8], *e = spreadb[p3 >> 8];
        out[0] = a[0] | b[0] << 1 | d[0] << 2 | e[0] << 3; out[1] = a[1] | b[1] << 1 | d[1] << 2 | e[1] << 3;
        a = spreadb[p0 & 0xFF]; b = spreadb[p1 & 0xFF]; d = spreadb[p2 & 0xFF]; e = spreadb[p3 & 0xFF];
        out[2] = a[0] | b[0] << 1 | d[0] << 2 | e[0] << 3; out[3] = a[1] | b[1] << 1 | d[1] << 2 | e[1] << 3;
    }
}
