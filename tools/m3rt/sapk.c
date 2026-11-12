/* sapk.c - see sapk.h. The steps and their checks are pak_read.sv's, one for one. */
#include "sapk.h"
static uint8_t rb(const Sapk *k, uint32_t a) { a &= 0xFFFFFF; return a < k->size ? k->cart[a] : 0xFF; }
static uint32_t rl(const Sapk *k, uint32_t a) { return (uint32_t)rb(k, a) << 24 | rb(k, a + 1) << 16 | rb(k, a + 2) << 8 | rb(k, a + 3); }
static uint32_t phys(uint32_t loc)             /* page << 24 | offset; page 0xFF = window 0 (bank 8 in bank 0's place) */
{
    const uint32_t pg = loc >> 24, off = loc & 0xFFFFFF;
    if (pg == 0xFF) return off < 0x80000 ? 0x400000 + off : off;
    return ((pg & 7) << 21) + off;
}
int sapk_open(Sapk *k, const uint8_t *cart, uint32_t size)
{
    k->cart = cart; k->size = size; k->reads = k->bad = 0;
    return rl(k, SAPK_DIR) == 0x5341504Bu && rl(k, SAPK_DIR + 4) == 2;
}
int sapk_sector(Sapk *k, uint32_t sector, uint8_t out[2048])
{
    const uint32_t D = SAPK_DIR;
    k->reads++;
    if (!(rl(k, D) == 0x5341504Bu && rl(k, D + 4) == 2) || sector >= (rl(k, D + 8) & 0xFFFFFF)) { k->bad++; return 0; }
    const uint32_t e = rl(k, D + rl(k, D + 16) + 4 * sector), u = e & 0xFFFFFF;
    const uint32_t pc = D + rl(k, D + 20) + 8 * u, len = rl(k, pc + 4), src = phys(rl(k, pc));
    const uint32_t sum = rl(k, D + rl(k, D + 24) + 4 * sector), dic = D + rl(k, D + 36), dn = rl(k, D + 40) & 0xFFFFFF;
    uint32_t i = 0, o = 0, h = 2166136261u; int ok = 1;
    if (len > 2048) { k->bad++; return 0; }                   /* (a piece is never longer than its sector) */
#define PUT(b) { const uint8_t b_ = (b); out[o++] = b_; h = (h ^ b_) * 16777619u; }
    if (e & 0x80000000u) { if (len != 2048) ok = 0; else while (o < 2048) PUT(rb(k, src + o)) }
    else while (i < len) {
        const uint8_t t = rb(k, src + i++); uint32_t L = t >> 4;
        if (L == 15) { uint8_t x; do { if (i >= len) { ok = 0; break; } x = rb(k, src + i++); L += x; } while (x == 255); if (!ok) break; }
        if (i + L > len || o + L > 2048) { ok = 0; break; }
        for (uint32_t n = 0; n < L; n++) PUT(rb(k, src + i++))
        if (i >= len) break;
        if (i + 2 > len) { ok = 0; break; }
        const uint32_t off = rb(k, src + i) | (uint32_t)rb(k, src + i + 1) << 8; i += 2;
        uint32_t m = (t & 15) + 4;
        if ((t & 15) == 15) { uint8_t x; do { if (i >= len) { ok = 0; break; } x = rb(k, src + i++); m += x; } while (x == 255); if (!ok) break; }
        if (off == 0 || off > o + dn || o + m > 2048) { ok = 0; break; }
        for (uint32_t n = 0; n < m; n++) PUT(off > o ? rb(k, dic + dn + o - off) : out[o - off])
    }
    if (ok && !(o == 2048 && h == sum)) ok = 0;
    if (!ok) k->bad++;
    return ok;
}
