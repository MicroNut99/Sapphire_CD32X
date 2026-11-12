/* tower_render.c - Sapphire docs/54: a PSG write log (u32 PSG clock, u8 register, u8 value: tools/m3_psglog.py) played
   by tpsg.c on the PCM chip's model: raw s16 mono at 32,552 Hz and <out>.n (u16 samples a frame), for
   tools/m3_sndcmp.py and tools/tower/tower_cmp.py.
     tower_render <log> <frames> <out> [updates a frame: 0 = at every write, 1 (default), 2 ...] [phase 0-99 %]
   The PSG's writes reach the chip only at the updates: what the console's path will do (a frame's writes sent once). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tpsg.h"
#include "rf5c_model.h"
static Rf5c chip; static long nreg, nram, nkey;
static void reg(void *u, int ch, int r, uint8_t v) { (void)u; Rf5cCh *h = &chip.ch[ch]; nreg++;
    switch (r) { case 0: h->env = v; break; case 1: h->pan = v; break; case 2: h->step = (uint16_t)((h->step & 0xFF00) | v); break;
    case 3: h->step = (uint16_t)((h->step & 0xFF) | v << 8); break; case 4: h->loopst = (uint16_t)((h->loopst & 0xFF00) | v); break;
    case 5: h->loopst = (uint16_t)((h->loopst & 0xFF) | v << 8); break; case 6: h->start = v; break; } }
static void key(void *u, int ch, int on) { (void)u; nkey++; rf5c_key(&chip, ch, on); }
static void ram(void *u, uint16_t a, const uint8_t *d, unsigned n) { (void)u; nram += n; memcpy(chip.ram + a, d, n); }
int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    FILE *f = fopen(argv[1], "rb"), *o = fopen(argv[3], "wb"), *on; char nn[512]; snprintf(nn, sizeof nn, "%s.n", argv[3]); on = fopen(nn, "wb");
    if (!f || !o || !on) return 2;
    const long nf = atol(argv[2]); const int upd = argc > 4 ? atoi(argv[4]) : 1; const double phase = argc > 5 ? atof(argv[5]) / 100 : 0;
    static Tpsg t; tpsg_power(&t); const TpsgOut out = { reg, key, ram, 0 };
    uint32_t wt; uint8_t rv[2]; int have = fread(&wt, 4, 1, f) == 1 && fread(rv, 1, 2, f) == 2;
    const double clocks = 358995.0 / 3, rate = 12500000.0 / 384, fps = 21477272.0 / 358995; double acc = 0;
    for (long fr = 0; fr < nf; fr++) {
        acc += rate / fps; const int ns = (int)acc; acc -= ns; int done = 0;
        for (int i = 0; i < ns; i++) {
            const double pos = (double)i / ns; const uint64_t now = (uint64_t)((fr + pos) * clocks);
            int go = 0;
            while (have && wt <= now) { tpsg_write(&t, rv[0], rv[1]); have = fread(&wt, 4, 1, f) == 1 && fread(rv, 1, 2, f) == 2; if (!upd) go = 1; }
            if (upd) { const int k = (int)((pos + 1 - phase) * upd) - upd + 1; if (k > done) { done = k; go = 1; } }
            if (go) tpsg_update(&t, &out);
            int32_t l, r; rf5c_sample(&chip, &l, &r);
            const int16_t v = (int16_t)((l + r) / 2); fwrite(&v, 2, 1, o);
        }
        const uint16_t k = (uint16_t)ns; fwrite(&k, 2, 1, on);
    }
    fprintf(stderr, "%ld frames: %ld register writes, %ld key changes, %ld wave RAM bytes to the chip (%.1f register writes a frame)\n", nf, nreg, nkey, nram, (double)nreg / nf);
    return 0;
}
