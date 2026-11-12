/* psg_render.c - Sapphire docs/50: a PSG write log (u32 PSG clock, u8 register, u8 value: tools/m3_psglog.py,
   TB_PSGLOG) played through psg_c.c, as the stand-in plays its own: raw s16 at 22,044 Hz and <out>.n (u16 samples a
   frame of 358,995 master clocks). For telling the sound path from the writes: the reference's own writes through
   the stand-in's PSG, against the reference's sound (tools/m3_sndcmp.py).
   cc -O2 psg_render.c psg_c.c -lm -o psg_render && ./psg_render <log> <frames> <out> */
#include <stdio.h>
#include <stdlib.h>
#include "psg_c.h"
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"), *o = fopen(argv[3], "wb"), *on; char nn[512]; snprintf(nn, sizeof nn, "%s.n", argv[3]); on = fopen(nn, "wb");
    if (!f || !o || !on) return 2;
    const long nf = atol(argv[2]); static PsgC p; psg_c_power(&p);
    uint32_t t; uint8_t rv[2]; int have = fread(&t, 4, 1, f) == 1 && fread(rv, 1, 2, f) == 2;
    const double clocks = 358995.0 / 3, rate = 22044, fps = 21477272.0 / 358995;
    uint64_t now = 0; double acc = 0;
    for (long fr = 0; fr < nf; fr++) {
        acc += rate / fps; const int ns = (int)acc; acc -= ns;
        const uint64_t f0 = (uint64_t)(fr * clocks);
        for (int i = 0; i < ns; i++) {
            const uint64_t to = f0 + (uint64_t)((i + 1) * clocks / ns); int64_t sum = 0; long cnt = 0;
            while (now < to) {
                while (have && t <= now) { psg_c_write(&p, rv[0], rv[1]); have = fread(&t, 4, 1, f) == 1 && fread(rv, 1, 2, f) == 2; }
                psg_c_clock(&p); int32_t l, r; psg_c_out(&p, &l, &r); sum += l + r; cnt++; now++;
            }
            double s = cnt ? (double)sum / cnt / 65536.0 : 0; if (s > 1) s = 1; if (s < -1) s = -1;
            const int16_t v = (int16_t)(s * 32000); fwrite(&v, 2, 1, o);
        }
        const uint16_t k = (uint16_t)ns; fwrite(&k, 2, 1, on);
    }
    fclose(o); fclose(on); return 0;
}
