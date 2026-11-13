/* fifo_synth.c - Sapphire docs/40: docs/24's FIFO made from the runtime's own picture state.
 *
 * The cart's FPGA writes the FIFO as the CPU makes its VDC / VCE writes (vdc_timing.sv, vsys.cpp). A stand-in that
 * runs the game in C (tools/m3rt, in ares's EverDrive board) has the state each picture is made from at each display
 * start instead (rt_draw_hook: VRAM, the sprite table, the colours, BXR, BYR, CR, MWR and the frame's raster splits).
 * fifo_synth turns that into entries that give the SH2's player (saspeed fifo_play, fpga/huc/fifo_v32.c) the same
 * state: what changed since the last picture, the sprite table through a copy of its own (written into VRAM where
 * DVSSR points, copied by a type 9, then VRAM put back), the splits, the registers, then the display's start.
 * rt_pc RUN_FIFO_OUT=<file> writes them; RUN_FIFO=<file> RUN_V32HASH=1 plays them back: the hashes are the same.
 */
#include <string.h>
#include "fifo_synth.h"

#define E(t, x, d) do { if (n < max) out[n] = (uint32_t)(t) << 28 | (uint32_t)((x) & 0xFFF) << 16 | (uint16_t)(d); n++; } while (0)
#define REG(sel, val) do { E(3, (sel) << 1, (val) & 0xFF); E(3, (sel) << 1 | 1, (val) >> 8); } while (0)

int fifo_synth(FifoShadow *s, const V32 *v, uint32_t *out, int max)
{
    int n = 0;
    if (!s->started) {                                        /* (the player starts from nothing known: everything -
                                                                 docs/59: every word, also one that is 0xFFFF. The
                                                                 shadow was all 0xFFFF, so those were never sent: 509
                                                                 VRAM words after a SAPM_SKIP start, where the player
                                                                 had 0) */
        for (unsigned a = 0; a < 0x8000; a++) s->vram[a] = (uint16_t)~v->vram[a];
        for (int i = 0; i < 512; i++) s->color[i] = (uint16_t)~v->color[i];
        for (int i = 0; i < 256; i++) s->sat[i] = (uint16_t)~v->sat[i];
        s->dvssr = 0xFFFF; s->started = 1; s->cur = 0xFFFFFFFFu; s->bxr = s->cr = 0xFFFFFFFFu;
    }
    E(6, 0, 0);                                               /* (no split open: the registers below are the current ones) */
    for (int i = 0; i < 512; i++) if (v->color[i] != s->color[i]) { E(4, i, v->color[i]); s->color[i] = v->color[i]; }
    /* the sprite table: v->sat as the VDC copied it, through VRAM at DVSSR (the player copies from there) */
    if (memcmp(s->sat, v->sat, sizeof s->sat)) {
        const unsigned d = v->DVSSR < 0x8000 - 0x100 ? v->DVSSR : 0x8000 - 0x100;
        if (d != s->dvssr) { REG(0x13, d); s->dvssr = d; }
        E(2, 0, d); s->cur = d;
        for (int i = 0; i < 256; i++) { E(1, 0, v->sat[i]); s->vram[d + i] = v->sat[i]; }
        s->cur = d + 256;
        E(9, 0, d);
        memcpy(s->sat, v->sat, sizeof s->sat);
    }
    for (unsigned a = 0; a < 0x8000; a++) {                   /* VRAM: the words that changed, in runs */
        if (v->vram[a] == s->vram[a]) continue;
        if (a != s->cur) E(2, 0, a);
        E(1, 0, v->vram[a]); s->vram[a] = v->vram[a]; s->cur = a + 1;
    }
    if (v->xsl_n > 0) {
        /* (docs/59) every match of the frame (v->xsl: the stand-in's, sapmhost.c), as the cart's FIFO has them. A
           register only when it differs from what the player has - at a match it takes its current ones - and BYR
           whenever the handler wrote it: a BYR write restarts the lines' count, the same value or not. 178 matches
           are three or four entries each, not eight */
        unsigned cb = s->bxr, cc = s->cr;
        for (int i = 0; i < v->xsl_n; i++) {
            E(7, 0, v->xsl[i].line);
            if (v->xsl[i].bxr != cb) { REG(7, v->xsl[i].bxr); cb = v->xsl[i].bxr; }
            if (v->xsl[i].wrote & 2) REG(8, v->xsl[i].byr);
            if (v->xsl[i].cr != cc) { REG(5, v->xsl[i].cr); cc = v->xsl[i].cr; }
        }
        E(6, 0, 0);
    } else
    for (int i = 0; i < v->spl_n && i < 4; i++) {             /* the splits: each opened at its line, its registers in it */
        E(7, 0, v->spl[i].line);
        REG(7, v->spl[i].bxr); REG(8, v->spl[i].byr); REG(5, v->spl[i].cr);
        E(6, 0, 0);
    }
    REG(7, v->BXR); REG(8, v->BYR); REG(5, v->t.CR); REG(9, v->MWR);
    s->bxr = v->BXR; s->cr = v->t.CR;
    E(5, 0, 0);
    return n;
}
