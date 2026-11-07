/* rt_state.c - M3: a 'DXS1' state file (tools/m3_speedtest.py pack) as an RtState. Big-endian: on the SH2 the
 * arrays are used where they are (the cart); on a little-endian PC they are copied, turned (alloc). */
#include <string.h>
#include "rt.h"

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

static const void *words(const uint8_t *p, long n, int size, void *(*alloc)(unsigned long))
{
    const uint16_t one = 1;
    if (*(const uint8_t *)&one == 0 || !alloc) return p;    /* big-endian: as it is */
    if (size == 2) { uint16_t *w = alloc((unsigned long)n * 2); for (long i = 0; i < n; i++) w[i] = be16(p + 2 * i); return w; }
    uint32_t *w = alloc((unsigned long)n * 4); for (long i = 0; i < n; i++) w[i] = be32(p + 4 * i); return w;
}

int rt_state_load(const uint8_t *b, RtState *st, void *(*alloc)(unsigned long))
{
    if (memcmp(b, "DXS1", 4) || be32(b + 4) != 1) return 0;
    memset(st, 0, sizeof *st);
    st->A = b[8]; st->X = b[9]; st->Y = b[10]; st->S = b[11]; st->P = b[12]; st->speed = b[13];
    st->pc = be16(b + 14); memcpy(st->mpr, b + 16, 8);
    st->vdc_t0 = (int32_t)be32(b + 24); st->io_t0 = (int32_t)be32(b + 28);
    st->nmasks = (long)be32(b + 32);
    const int adpcm = be32(b + 36) & 1;
    uint32_t o[12]; for (int i = 0; i < 12; i++) o[i] = be32(b + 40 + 4 * i);
    st->base_ram = b + o[0]; st->cd_ram = b + o[1]; st->super_ram = b + o[2]; st->bram = b + o[3]; st->rom = b + o[4];
    st->adpcm = adpcm ? b + o[5] : 0;
    st->vram = words(b + o[6], 32768, 2, alloc); st->sat = words(b + o[7], 256, 2, alloc); st->color = words(b + o[8], 512, 2, alloc);
    st->vdc_regs = words(b + o[9], 96, 4, alloc); st->io_regs = words(b + o[10], 128, 4, alloc);
    st->masks = words(b + o[11], st->nmasks, 2, alloc);
    if (be32(b + 36) & 2) {                         /* (docs/69: the drive's state, the TOC, the disc's table) */
        const uint32_t p12 = be32(b + 40 + 48), p13 = be32(b + 40 + 52), p14 = be32(b + 40 + 56);
        st->scsi_o = words(b + p12, 256, 4, alloc); st->scsi_fifo = b + p12 + 1024;
        st->toc = words(b + p13, 204, 4, alloc); st->disc_tab = (const int32_t *)words(b + p14, 3 + 6 * 101, 4, alloc);
    }
    if (be32(b + 36) & 4) {                         /* (Sapphire docs/08: the Arcade Card, parts 15 and 16) */
        st->ac_regs = words(b + be32(b + 40 + 60), 19, 4, alloc); st->ac_ram = b + be32(b + 40 + 64);
    }
    return 1;
}
