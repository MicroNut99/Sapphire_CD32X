/* bg32_test.c - Sapphire docs/58: rt_bg32.c's drawing against the reference (bg32_ref.c) on the PC machine's state dumps
 * (tools/m2host/sapm_pc.c RUN_DUMP_EVERY; game data: the work folder's). Each dump at 8 horizontal scroll offsets, drawn
 * whole and in two halves; the colour maps and the kept tiles carried from one picture to the next, as on the console
 * (FRESH=1: a new map each picture). The same means: every pixel's colour (cram[fb]) equal, nothing written outside the
 * picture's 256 columns, the same answer. Build and run: tools/m3_bg32test.sh <gen dir> <dump> ... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt_bg32.h"
int old_draw(const V32 *v, uint8_t *fb, int pitch, uint16_t cram[256], uint8_t own[256]);
int main(int argc, char **argv)
{
    static V32 v; static uint8_t d[8192 + 65536 + 196608 + 131072 + 512 + 1024 + 8]; int bad = 0, n = 0;
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); if (!f || fread(d, 1, sizeof d, f) != sizeof d) { printf("%s: short\n", argv[a]); continue; } fclose(f);
        const uint8_t *q = d + 8192 + 65536 + 196608;
        memcpy(v.vram, q, 65536); memcpy(v.sat, q + 131072, 512); memcpy(v.color, q + 131072 + 512, 1024);
        uint16_t reg[4]; memcpy(reg, q + 131072 + 1536, 8); v.BXR = reg[0]; v.BYR = reg[1]; v.MWR = reg[2]; v.t.CR = reg[3];
        for (int sx = 0; sx < 8; sx++) {
            v.BXR = (uint16_t)(reg[0] + sx);
            static uint8_t f1[224 * 320], f2[224 * 320]; static uint16_t c1[256], c2[256]; static uint8_t o1[256], o2[256];   /* (the maps live on, as on the console) */
            if (getenv("FRESH")) { memset(o1, 0, 256); memset(o2, 0, 256); }
            const int k1 = old_draw(&v, f1 + 32, 320, c1, o1), k2 = bg32_draw(&v, f2 + 32, 320, c2, o2);
            long diff = 0; for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) diff += c1[f1[32 + y * 320 + x]] != c2[f2[32 + y * 320 + x]];
            { static uint8_t f3[224 * 320]; uint16_t *c3 = c2, u3[16]; uint8_t *o3 = o2;   /* the two halves, the same map */
              bg32_masks(&v, u3); bg32_prep(&v, c3, o3, u3); bg32_rows(&v, f3 + 32, 320, 0, 112); bg32_rows(&v, f3 + 32, 320, 112, 224);
              for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) diff += c1[f1[32 + y * 320 + x]] != c3[f3[32 + y * 320 + x]]; }
            long spill = 0; for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) if (x < 32 || x >= 288) spill += f2[y * 320 + x] != 0;
            n++; if (diff || spill || k1 != k2) { bad++; if (bad < 12) printf("%s +%d: %ld pixels differ, %ld spilt, ok %d/%d\n", argv[a], sx, diff, spill, k1, k2); }
        }
    }
    printf("%d pictures, %d not the same\n", n, bad); return bad != 0;
}
