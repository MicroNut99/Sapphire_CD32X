/* cellstore.c - Sapphire docs/28: the sprite cells the FPGA keeps converted, as a C model (the RTL's reference).
 *
 * The FPGA sees every VRAM write (it makes them). For each it updates its own copy of VRAM and the cell's 32X form
 * (tools/m3rt/rt_spr.c convert(): 16 rows of 8 bytes of nibbles, the odd x's rows of 9, the rows' mask), so the SH2
 * never converts a cell.
 *
 * Exact pictures: the SH2 draws frame k while the game is already in k + 1 or k + 2. So the store is copy-on-write
 * from a frame boundary:
 *   - cs_release (the SH2: its picture before is on screen) clears the frozen cells and arms the lock;
 *   - the next frame boundary (the display's start, FIFO type 5) locks: from then on, a cell about to change for the
 *     first time is first copied to its frozen slot;
 *   - the SH2 reads a cell from its frozen slot if it has one, else the live one: the cells as they were at the lock,
 *     which is the boundary of the frame the SH2 draws (it draws the first frame's end after its release).
 * In the FPGA: VRAM's copy and both stores in the cart's second PSRAM; the 512 frozen bits readable by the SH2.
 */
#include <stdint.h>
#include <string.h>
#include "cellstore.h"

static uint16_t cvram[0x8000];
static uint8_t live[512][CS_CELL], frozen[512][CS_CELL];
static uint8_t isfrozen[512];
static int locked, armed;
unsigned long cs_freezes, cs_updates;

static void row(unsigned c, unsigned y)
{
    static const uint32_t z = 0;
    const uint16_t *w = cvram + c * 64;
    const unsigned p0 = w[y], p1 = w[16 + y], p2 = w[32 + y], p3 = w[48 + y];
    uint8_t *e = live[c] + 8 * y, *o = live[c] + CS_ODD + 9 * y;
    for (int x = 0; x < 16; x++) {                     /* pixel x: bit 15 - x of each plane */
        const unsigned b = 15 - x;
        const unsigned v = (p0 >> b & 1) | (p1 >> b & 1) << 1 | (p2 >> b & 1) << 2 | (p3 >> b & 1) << 3;
        if (x & 1) e[x >> 1] = (uint8_t)((e[x >> 1] & 0xF0) | v); else e[x >> 1] = (uint8_t)((e[x >> 1] & 0x0F) | v << 4);
    }
    o[0] = (uint8_t)(e[0] >> 4);
    for (int k = 1; k < 8; k++) o[k] = (uint8_t)((e[k - 1] & 15) << 4 | e[k] >> 4);
    o[8] = (uint8_t)((e[7] & 15) << 4);
    unsigned m = (unsigned)live[c][CS_MASK] << 8 | live[c][CS_MASK + 1];
    if (p0 | p1 | p2 | p3) m |= 0x8000u >> y; else m &= ~(0x8000u >> y);
    live[c][CS_MASK] = (uint8_t)(m >> 8); live[c][CS_MASK + 1] = (uint8_t)m;
    (void)z;
}

void cs_init(const uint16_t *vram)
{
    memcpy(cvram, vram, sizeof cvram);
    memset(live, 0, sizeof live);
    for (unsigned c = 0; c < 512; c++) for (unsigned y = 0; y < 16; y++) row(c, y);
    memset(isfrozen, 0, sizeof isfrozen); locked = 0; armed = 0;
}

void cs_vram(unsigned a, uint16_t w)
{
    if (a >= 0x8000 || cvram[a] == w) { if (a < 0x8000) cvram[a] = w; return; }
    const unsigned c = a >> 6;
    if (locked && !isfrozen[c]) { memcpy(frozen[c], live[c], CS_CELL); isfrozen[c] = 1; cs_freezes++; }
    cvram[a] = w; row(c, a & 15); cs_updates++;
}

void cs_boundary(void) { if (armed) { armed = 0; locked = 1; memset(isfrozen, 0, sizeof isfrozen); } }
void cs_release(void) { locked = 0; armed = 1; memset(isfrozen, 0, sizeof isfrozen); }
const uint8_t *cs_cell(unsigned c) { c &= 511; return isfrozen[c] ? frozen[c] : live[c]; }
int cs_locked(void) { return locked; }
