/* tpsg_gen.c - Sapphire docs/54: tpsg_tab.h, tpsg.c's tone search for every period (the console has no time for it).
   cc -DTPSG_SEARCH tpsg_gen.c tpsg.c -lm -o tpsg_gen && ./tpsg_gen > tpsg_tab.h   (prints the accuracy to stderr) */
#include <stdio.h>
#include <math.h>
#include "tpsg.h"
int main(void)
{
    static uint16_t fd[4096]; static uint8_t cy[4096]; double worst = 0; int wp = 0, over3 = 0, lo3 = 0;
    for (int p = 0; p < 4096; p++) {
        uint16_t f; uint8_t r; if (!tpsg_tone((unsigned)p, &f, &r)) continue;
        fd[p] = f; cy[p] = r;
        const int pp = p ? p : 4096; const long n = (long)ceil(32.0 * r * 2048 / f);
        const double c = fabs(1200 * log2(r * (12500000.0 / 384) / n / (3579545.0 / (32.0 * pp))));
        if (c > worst) { worst = c; wp = pp; }
        if (c > 3) { over3++; if (pp < lo3 || !lo3) lo3 = pp; }
    }
    fprintf(stderr, "the worst tone: %.2f cents (period %d); more than 3 cents off: %d periods, the shortest %d (%.0f Hz)\n", worst, wp, over3, lo3, lo3 ? 3579545.0 / 32 / lo3 : 0);
    printf("/* tpsg_tab.h - made by tools/tower/tpsg_gen.c: a PSG period as the PCM chip's step (0: not played) and its loop's cycles */\n");
    printf("static const uint16_t tpsg_fd[4096] = {"); for (int p = 0; p < 4096; p++) printf("%s%u,", p % 16 ? "" : "\n", fd[p]); printf("\n};\n");
    printf("static const uint8_t tpsg_cyc[4096] = {"); for (int p = 0; p < 4096; p++) printf("%s%u,", p % 32 ? "" : "\n", cy[p]); printf("\n};\n");
    return 0;
}
