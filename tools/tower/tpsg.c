/* tpsg.c - see tpsg.h. */
#include "tpsg.h"
/* a side's level for a volume index (0 loudest, 1.5 dB a step, 31 silent), 0-255: 10^(-1.5 i / 20) */
static const uint8_t GAIN[32] = { 255, 215, 181, 152, 128, 108, 91, 76, 64, 54, 45, 38, 32, 27, 23, 19, 16, 14, 11, 10, 8, 7, 6, 5, 4, 3, 3, 2, 2, 2, 1, 0 };
static const uint8_t SCALE[16] = { 0x00, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x10, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F };
#define ENV_TOP 96                                     /* a channel's loudest ENV: six of them stay inside the chip's 16 bits */

void tpsg_power(Tpsg *t)
{
    { uint8_t *z = (uint8_t *)t; for (unsigned i = 0; i < sizeof *t; i++) z[i] = 0; }
    for (int c = 0; c < 6; c++) { t->ch[c].dirty = 1; t->out[c].fd = t->out[c].ls = 0xFFFF; t->out[c].env = t->out[c].pan = 0; t->out[c].on = 0;   /* (0: neither is ever sent as 0) */ t->out[c].noise = 0xFF; }
}
void tpsg_write(Tpsg *t, unsigned a, uint8_t v)
{
    const int c = t->sel;
    switch (a & 15) {
    case 0: t->sel = v & 7; break;
    case 1: t->gbal = v; break;
    case 2: if (c < 6) t->ch[c].freq = (uint16_t)((t->ch[c].freq & 0xF00) | v); break;
    case 3: if (c < 6) t->ch[c].freq = (uint16_t)((t->ch[c].freq & 0x0FF) | (v & 15) << 8); break;
    case 4: if (c < 6) { if ((t->ch[c].ctrl & 0x40) && !(v & 0x40)) t->ch[c].widx = 0; t->ch[c].ctrl = v; } break;
    case 5: if (c < 6) t->ch[c].bal = v; break;
    case 6: if (c < 6) {                               /* a waveform byte: taken unless DDA; the index steps only with the channel off */
                if (!(t->ch[c].ctrl & 0x40) && t->ch[c].wave[t->ch[c].widx] != (v & 31)) { t->ch[c].wave[t->ch[c].widx] = v & 31; t->ch[c].dirty = 1; }
                if (!(t->ch[c].ctrl & 0xC0)) t->ch[c].widx = (t->ch[c].widx + 1) & 31; }
            break;
    case 7: if (c >= 4 && c < 6) t->ch[c].noise = v; break;
    case 8: t->lfofreq = v; break;
    case 9: t->lfoctrl = v; break;
    }
}
/* The chip's loop: R cycles of the waveform (32 R bytes) end at the piece's end mark; the address steps FD / 2048 bytes
   an output sample, and the mark sends it to the loop's start with no fraction - so a loop takes a whole number n of
   output samples, and the tone is R x 32,552 / n Hz. The PSG's tone is 3,579,545 / (32 period) Hz, so n should be
   R x period x 0.2910122. Every R (1-32) with the step just under or over the wanted one is tried; the nearest tone wins. */
#ifndef TPSG_SEARCH
#include "tpsg_tab.h"                                  /* (the search's answers for every period: tools/tower/tpsg_gen.c) */
int tpsg_tone(unsigned period, uint16_t *fd, uint8_t *cycles)
{
    period &= 0xFFF;
    if (!tpsg_fd[period]) return 0;
    *fd = tpsg_fd[period]; *cycles = tpsg_cyc[period]; return 1;
}
#else
int tpsg_tone(unsigned period, uint16_t *fd, uint8_t *cycles)
{
    if (!period) period = 4096;
    if (period < 7) return 0;                          /* (above 15.9 kHz; and a step past 16 bytes would jump the end marks) */
    uint32_t best_e = 0xFFFFFFFFu, best_f = 0; int best_r = 1;
    const uint32_t f0 = 225204u / period;                         /* the step, near enough: 2048 x 109.963 / period */
    for (int r = TPSG_CYCLES; r >= 1; r--) {
        const uint32_t x = (uint32_t)r * period * 19071u;         /* the n wanted, in 16.16: 0.2910122 x 65536 = 19071.78 */
        const uint32_t x2 = x + (((uint32_t)r * period * 51u) >> 6);    /* (+ 0.78: 51 / 64 = 0.797) */
        const uint32_t len = 32u * (uint32_t)r * 2048u;
        for (uint32_t f = f0 > 1 ? f0 - 1 : 1; f <= f0 + 2; f++) {        /* (a small step is coarse: each r lands on other tones) */
            const uint32_t n = (len + f - 1) / f;                 /* the output samples this loop takes at this step */
            const uint32_t d = x2 > (n << 16) ? x2 - (n << 16) : (n << 16) - x2;
            const uint32_t e = (uint32_t)(((unsigned long long)d << 16) / x2);   /* the tone's error, relative, x 65536 */
            if (e < best_e) { best_e = e; best_f = f; best_r = r; }
        }
    }
    const uint32_t f = best_f;
    if (f > 0x7FFF) return 0;
    *fd = (uint16_t)f; *cycles = (uint8_t)best_r; return 1;
}
#endif
static void put(const TpsgOut *o, int ch, int r, uint8_t v) { o->reg(o->u, ch, r, v); }
void tpsg_update(Tpsg *t, const TpsgOut *o)
{
    if (!t->noise_done) {                              /* the noise's piece, once: the PSG's own shift register's bits */
        uint32_t l = 1; static uint8_t b[256];
        for (uint32_t a = 0; a < TPSG_NOISE_N; a += 16) {
            for (int i = 0; i < 16; i++) { b[i] = (l & 1) ? 0x80 | 124 : 124; l = (l >> 1) | (((l ^ (l >> 1) ^ (l >> 11) ^ (l >> 12) ^ (l >> 17)) & 1) << 17); }
            o->ram(o->u, (uint16_t)(TPSG_NOISE + a), b, 16);
        }
        for (int i = 0; i < 16; i++) b[i] = 0xFF;
        o->ram(o->u, (uint16_t)(TPSG_NOISE + TPSG_NOISE_N), b, 16);
        t->noise_done = 1;
    }
    for (int c = 0; c < 6; c++) {
        const uint8_t ctrl = t->ch[c].ctrl;
        const int noise = c >= 4 && (t->ch[c].noise & 0x80);
        int on = (ctrl & 0x80) && !(ctrl & 0x40);                      /* (DDA, a sample written by the CPU: not played here) */
        /* the levels: each side's index = the main balance's + the channel's + the volume's, as the PSG adds them */
        int vl[2];
        for (int s = 0; s < 2; s++) {
            const uint8_t gb = s ? t->gbal & 15 : t->gbal >> 4, b = s ? t->ch[c].bal & 15 : t->ch[c].bal >> 4;
            const int x = (0x1F - SCALE[gb]) + (0x1F - SCALE[b]) + (0x1F - (ctrl & 0x1F));
            vl[s] = x > 0x1F ? 0x1F : x;
        }
        const unsigned gl = GAIN[vl[0]], gr = GAIN[vl[1]], gm = gl > gr ? gl : gr;
        uint16_t fd = 0, ls = 0; uint8_t env = 0, pan = 0;
        if (!gm) on = 0;
        if (on) {
            env = (uint8_t)((gm * ENV_TOP + 127) / 255); if (!env) env = 1;
            pan = (uint8_t)(((gl * 15 + gm / 2) / gm) | ((gr * 15 + gm / 2) / gm) << 4);     /* low nibble: left */
            if (noise) {
                const unsigned f = 0x1F - (t->ch[c].noise & 0x1F);
                fd = (uint16_t)(f ? (3519u + f / 2) / f : 7037u);      /* 55,930 / f Hz (f = 0: 111,861 Hz) in the chip's steps */
                ls = (uint16_t)TPSG_NOISE;
            } else {
                uint8_t r;
                unsigned p = t->ch[c].freq;
                if (c == 0 && (t->lfoctrl & 3)) on = 0;                /* (frequency modulation: not played - Sapphire never uses it) */
                if (!tpsg_tone(p, &fd, &r)) on = 0;
                else ls = r;                                           /* (the loop's cycles: its piece is found below) */
            }
        }
        int slot = t->out[c].slot;
        if (on && !noise) {                            /* the waveform's piece: the one that holds it, or a new one */
            int k, old = -1;
            for (k = 0; k < TPSG_SLOTS; k++) { if (!t->slot[k].used) continue; int i; for (i = 0; i < 32 && t->slot[k].wave[i] == t->ch[c].wave[i]; i++) { } if (i == 32) break; }
            if (k == TPSG_SLOTS) {                     /* not there: the piece no channel plays that was used longest ago */
                for (int q = 0; q < TPSG_SLOTS; q++) {
                    int busy = 0; for (int x = 0; x < 6; x++) if (t->out[x].on && !t->out[x].noise && t->out[x].slot == q) busy = 1;
                    if (busy) continue;
                    if (!t->slot[q].used) { old = q; break; }
                    if (old < 0 || (uint16_t)(t->clock - t->slot[q].age) > (uint16_t)(t->clock - t->slot[old].age)) old = q;
                }
                k = old;
                static uint8_t b[32 * TPSG_CYCLES + 16];
                for (int i = 0; i < 32; i++) t->slot[k].wave[i] = t->ch[c].wave[i];
                for (int i = 0; i < 32 * TPSG_CYCLES; i++) { const int m = (2 * t->ch[c].wave[i & 31] - 31) * 4; b[i] = (uint8_t)(m >= 0 ? 0x80 | m : -m); }
                for (int i = 0; i < 16; i++) b[32 * TPSG_CYCLES + i] = 0xFF;
                o->ram(o->u, (uint16_t)TPSG_SLOT(k), b, sizeof b);
                t->slot[k].used = 1;
            }
            t->slot[k].age = ++t->clock; slot = k;
            ls = (uint16_t)(TPSG_SLOT(k) + 32u * (TPSG_CYCLES - ls));     /* (ls held the loop's cycles) */
        }
        if (!on) { if (t->out[c].on) { o->key(o->u, c, 0); t->out[c].on = 0; } continue; }
        if (t->out[c].env != env) { put(o, c, 0, env); t->out[c].env = env; }
        if (t->out[c].pan != pan) { put(o, c, 1, pan); t->out[c].pan = pan; }
        if (t->out[c].ls != ls) { put(o, c, 4, (uint8_t)ls); put(o, c, 5, (uint8_t)(ls >> 8)); t->out[c].ls = ls; }
        if (t->out[c].fd != fd) { put(o, c, 2, (uint8_t)fd); put(o, c, 3, (uint8_t)(fd >> 8)); t->out[c].fd = fd; }
        if (!t->out[c].on || t->out[c].noise != (uint8_t)noise || (!noise && t->out[c].slot != slot)) {   /* a start (or tone <-> noise, or another waveform): from the piece's start */
            if (t->out[c].on) o->key(o->u, c, 0);
            put(o, c, 6, (uint8_t)((noise ? TPSG_NOISE + (c == 5 ? 0x2000u : 0) : TPSG_SLOT(slot)) >> 8));
            o->key(o->u, c, 1); t->out[c].on = 1; t->out[c].noise = (uint8_t)noise; t->out[c].slot = (uint8_t)slot;
        }
    }
}
