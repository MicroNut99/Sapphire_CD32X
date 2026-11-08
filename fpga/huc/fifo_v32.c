/* fifo_v32.c - Sapphire docs/24: the SH2's side of the FIFO, on the PC. The entries (sys_host.c's sys_fifo) applied to
 * the video state the picture build draws from, with rt_vdc.c's rules for the raster splits (docs/16): a raster
 * match opens a split from its next line with the registers then, the game's BXR / BYR / CR writes go into the open
 * one, the display's end closes it, and the display's start keeps the frame's splits for the picture. At each display
 * start the state's hash, as rt_pc RUN_V32HASH prints it.
 *   fifo_v32 <fifo file> <vdc_start.bin>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t vram[65536], sat[256], color[512];
static uint16_t BXR, BYR, CR, MWR, RCR, DVSSR;
static struct { uint16_t line, bxr, byr, cr; uint8_t wrote; } sp[4], spl[4];
static int sp_n, sp_cur = -1, spl_n; static long pal_used[16];

static uint32_t hash(void)
{
    uint32_t h = 2166136261u;
#define HW(x) do { const uint16_t w_ = (uint16_t)(x); h = (h ^ (w_ & 0xFF)) * 16777619u; h = (h ^ (w_ >> 8)) * 16777619u; } while (0)
    for (int i = 0; i < 32768; i++) HW(vram[i]);
    for (int i = 0; i < 256; i++) HW(sat[i]);
    for (int i = 0; i < 512; i++) HW(color[i]);
    HW(BXR); HW(BYR); HW(CR); HW(MWR); HW(spl_n);
    for (int i = 0; i < spl_n; i++) { HW(spl[i].line); HW(spl[i].bxr); HW(spl[i].byr); HW(spl[i].cr); }
    return h;
}
#define SETB(r, v, hi) (r) = (uint16_t)((hi) ? (((r) & 0x00FF) | (v) << 8) : (((r) & 0xFF00) | (v)))

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: fifo_v32 <fifo> <vdc_start.bin>\n"); return 2; }
    FILE *f = fopen(argv[2], "rb"); static uint16_t vs[65536 + 256 + 512 + 192];
    if (!f || fread(vs, 2, sizeof vs / 2, f) != sizeof vs / 2) return 2; fclose(f);
    memcpy(vram, vs, sizeof vram); memcpy(sat, vs + 65536, 512); memcpy(color, vs + 65536 + 256, 1024);
    const uint32_t *r = (const uint32_t *)(vs + 65536 + 256 + 512);
    CR = r[3]; RCR = r[4]; BXR = r[5] & 0x3FF; BYR = r[6] & 0x1FF; MWR = r[7]; DVSSR = r[17];
    f = fopen(argv[1], "rb"); if (!f) return 2;
    uint32_t e; unsigned addr = 0; long pic = 0, n = 0, kinds[16] = { 0 };
    while (fread(&e, 4, 1, f) == 1) {
        const unsigned t = e >> 28, x = (e >> 16) & 0xFFF, d = e & 0xFFFF; n++; kinds[t]++;
        switch (t) {
        case 1: if (addr < 0x8000) vram[addr] = (uint16_t)d; addr = (addr + 1) & 0xFFFF; break;
        case 2: addr = d; break;
        case 3: {
            const unsigned sel = x >> 1, hi = x & 1;
            switch (sel) {
            case 5: SETB(CR, d, hi); if (sp_cur >= 0) { sp[sp_cur].cr = CR; sp[sp_cur].wrote |= 4; } break;
            case 6: SETB(RCR, d, hi); RCR &= 0x3FF; break;
            case 7: SETB(BXR, d, hi); BXR &= 0x3FF; if (sp_cur >= 0) { sp[sp_cur].bxr = BXR; sp[sp_cur].wrote |= 1; } break;
            case 8: SETB(BYR, d, hi); BYR &= 0x1FF; if (sp_cur >= 0) { sp[sp_cur].byr = BYR; sp[sp_cur].wrote |= 2; } break;
            case 9: SETB(MWR, d, hi); break;
            case 0x13: SETB(DVSSR, d, hi); break;
            }
            break;
        }
        case 4: color[x & 0x1FF] = (uint16_t)d; break;
        case 5:
            for (int i = 0; i < sp_n; i++) spl[i] = sp[i];
            spl_n = sp_n; sp_n = 0; sp_cur = -1;
            printf("pic %ld v32 %08x", pic++, hash());
            printf(" | BXR %03X BYR %03X CR %04X MWR %04X splits %d L%d %03X %03X %04X", BXR, BYR, CR, MWR, spl_n, spl_n ? spl[0].line : 0, spl_n ? spl[0].bxr : 0, spl_n ? spl[0].byr : 0, spl_n ? spl[0].cr : 0);
            { uint32_t a = 0, b = 0, c = 0; for (int i = 0; i < 32768; i++) a = a * 31 + vram[i]; for (int i = 0; i < 256; i++) b = b * 31 + sat[i]; for (int i = 0; i < 512; i++) c = c * 31 + color[i]; printf(" vram %08x sat %08x col %08x\n", a, b, c); }
            if (getenv("FV_DUMP") && atol(getenv("FV_DUMP")) == pic - 1) { FILE *g = fopen("/tmp/ff_vram.bin", "wb"); fwrite(vram, 2, 32768, g); fclose(g); }
            break;
        case 6: sp_cur = -1; break;
        case 7: if (sp_n < 4) { const int k = sp_n++; sp[k].line = (uint16_t)d; sp[k].bxr = BXR; sp[k].byr = BYR; sp[k].cr = CR; sp[k].wrote = 0; sp_cur = k; } break;
        case 9: if (d < 0x8000) { const unsigned len = d > 0x8000 - 0x100 ? 0x8000u - d : 256; memcpy(sat, &vram[d], len * 2);
                    for (int i = 0; i < 64; i++) { const int y = (sat[i * 4] & 0x3FF) - 64, xx = (sat[i * 4 + 1] & 0x3FF) - 32;
                        if (y > -64 && y < 224 && xx > -32 && xx < 256) pal_used[sat[i * 4 + 3] & 15]++; } } break;
        }
    }
    fprintf(stderr, "fifo_v32: %ld entries, %ld pictures; VRAM %ld, addresses %ld, registers %ld, colours %ld, splits %ld, PSG %ld\n",
            n, pic, kinds[1], kinds[2], kinds[3], kinds[4], kinds[7], kinds[10]);
    fprintf(stderr, "sprite palettes used:"); for (int i = 0; i < 16; i++) fprintf(stderr, " %ld", pal_used[i]); fprintf(stderr, "\n");
    return 0;
}
