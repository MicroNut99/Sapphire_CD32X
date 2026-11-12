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
            if (getenv("SPLIT")) {   /* a raster split at line L with another scroll: lines below L against the reference drawn with that scroll */
                const int L = atoi(getenv("SPLIT")); static uint8_t f4[224 * 320], f5[224 * 320]; uint16_t u4[16]; uint16_t *c5 = c1; uint8_t *o5 = o1;   /* (the reference's one map) */
                V32 *w = &v; const uint16_t bx0 = w->BXR, by0 = w->BYR, bx2 = (uint16_t)(bx0 + 43), by2 = (uint16_t)((by0 + 301) & 0x1FF);
                w->spl_n = 1; w->spl[0].line = (uint16_t)L; w->spl[0].bxr = bx2; w->spl[0].byr = by2; w->spl[0].cr = w->t.CR;
                bg32_masks(w, u4); bg32_prep(w, c2, o2, u4); bg32_rows(w, f4 + 32, 320, 0, 112); bg32_rows(w, f4 + 32, 320, 112, 224);
                w->spl_n = 0; w->BXR = bx2; w->BYR = (uint16_t)(by2 + 1 - L); old_draw(w, f5 + 32, 320, c5, o5); w->BXR = bx0; w->BYR = by0;
                for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++)
                    diff += y < L ? c1[f1[32 + y * 320 + x]] != c2[f4[32 + y * 320 + x]] : c5[f5[32 + y * 320 + x]] != c2[f4[32 + y * 320 + x]];
            }
            if (getenv("SAVE") && sx == 0) {   /* SAVE=<prefix>: the new drawing as a picture, <prefix><n>.ppm (to look at) */
                char nm[256]; snprintf(nm, sizeof nm, "%s%d.ppm", getenv("SAVE"), a); FILE *o = fopen(nm, "wb");
                if (o) { fprintf(o, "P6 256 224 255\n"); for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) { const uint16_t c = c2[f2[32 + y * 320 + x]];
                    fputc((c & 31) * 255 / 31, o); fputc((c >> 5 & 31) * 255 / 31, o); fputc((c >> 10 & 31) * 255 / 31, o); } fclose(o); }
            }
            long spill = 0; for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) if (x < 32 || x >= 288) spill += f2[y * 320 + x] != 0;
            n++; if (diff || spill || k1 != k2) { bad++; if (bad < 12) printf("%s +%d: %ld pixels differ, %ld spilt, ok %d/%d\n", argv[a], sx, diff, spill, k1, k2); }
        }
    }
    printf("%d pictures, %d not the same\n", n, bad); return bad != 0;
}
