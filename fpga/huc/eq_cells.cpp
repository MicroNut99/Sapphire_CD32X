// eq_cells.cpp - Sapphire docs/34: cellstore.sv (Verilator) against cellstore.c on a FIFO. The SH2's protocol as
// saspeed's fifo_play has it: the unlock, then at each frame's end a release every RUN_EVERY frames (before the
// count), the count, and the store run to it; at each lock, every cell's and every tile's view compared.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "Vcellstore.h"
#include "Vcellstore___024root.h"
#include "Vcellstore_cellstore.h"
extern "C" {
#include "cellstore.h"
}
static Vcellstore *t; static uint64_t cyc;
#define R(x) (t->rootp->cellstore->x)
static void tick() { t->clk = 0; t->eval(); t->clk = 1; t->eval(); cyc++; }
static void wr(int r, int v) { t->reg_we = 1; t->reg_r = r; t->reg_v = v; tick(); t->reg_we = 0; }
static long bad;
static void compare(long k) {
    for (int c = 0; c < 512; c++) {
        const uint8_t *ref = cs_cell(c); const bool fz = R(cfz)[c];
        for (int i = 0; i < 288; i++) { const uint8_t g = fz ? R(frozen)[c * 288 + i] : R(live)[c * 288 + i];
            if (g != ref[i]) { if (bad++ < 10) fprintf(stderr, "DIFF frame %ld cell %d byte %d: C %02X RTL %02X (frozen %d)\n", k, c, i, ref[i], g, fz); break; } }
    }
    for (int n = 0; n < 64; n++) {   // (the read port: a few cells' words and tiles' longs, as the SH2 reads them)
        const int c = (int)((k * 37 + n * 113) & 511), i = (n * 18) % 288 & ~1, tl = (int)((k * 101 + n * 29) & 2047);
        const uint32_t a = (R(cfz)[c] ? 0x194000u : 0x170000u) + c * 288 + i; t->sa = a; tick();
        const uint16_t w = (uint16_t)(cs_cell(c)[i] << 8 | cs_cell(c)[i + 1]);
        if (t->sq != w) { if (bad++ < 10) fprintf(stderr, "DIFF port frame %ld cell %d word %d\n", k, c, i); }
        t->sa = 0x1B8000u + 4 * tl; tick(); const uint16_t hi = t->sq; t->sa += 2; tick();
        if ((uint32_t)(hi << 16 | t->sq) != cs_tile(tl)) { if (bad++ < 10) fprintf(stderr, "DIFF port frame %ld tile %d\n", k, tl); }
    }
    for (int tl = 0; tl < 2048; tl++) { const uint32_t g = R(tfz)[tl] ? R(tfrozen)[tl] : R(tlive)[tl];
        if (g != cs_tile(tl)) { if (bad++ < 10) fprintf(stderr, "DIFF frame %ld tile %d: C %08X RTL %08X\n", k, tl, cs_tile(tl), g); } }
    if (bad > 20) exit(1);
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "eq_cells <fifo.bin> <vdc_start.bin> [frames] [every]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long ne = ftell(f) / 4; fseek(f, 0, SEEK_SET);
    uint32_t *E = (uint32_t *)malloc(ne * 4); if (fread(E, 4, ne, f) != (size_t)ne) return 2; fclose(f);
    static uint16_t vram[0x8000]; f = fopen(argv[2], "rb"); if (!f || fread(vram, 2, 0x8000, f) != 0x8000) return 2; fclose(f);
    const long frames = argc > 3 ? atol(argv[3]) : 600, every = argc > 4 ? atol(argv[4]) : 2;
    t = new Vcellstore; t->rst = 1; tick(); tick(); t->rst = 0;
    for (int a = 0; a < 0x8000; a++) { t->vload_we = 1; t->vload_a = a; t->vload_d = vram[a]; tick(); } t->vload_we = 0;
    cs_init(vram);
    wr(0xcb, 0xa5); wr(0xcb, 0x5a); while (t->busy) tick();
    long fi = 0, ri = 0, pics = 0; unsigned faddr = 0, cb = 0;   // (fi: the C model's cursor; ri: the RTL's)
    for (long k = 0; k < frames; k++) {
        const bool draw = k % every == 0;
        if (draw) { cs_release(); wr(0xc8, 0); }
        // the count k + 1: both run to boundary k + 1
        while (fi < ne && cb < (unsigned)(k + 1)) { const uint32_t e = E[fi++]; const unsigned ty = e >> 28, d = e & 0xFFFF;
            if (ty == 2) faddr = d; else if (ty == 1) { cs_vram(faddr, (uint16_t)d); faddr = (faddr + 1) & 0xFFFF; } else if (ty == 5) { cs_boundary(); cb++; } }
        tick(); wr(0xca, (k + 1) & 0xFF);   // (the count while the release clears: held)
        while (ri < ne) { t->ent_valid = 1; t->ent = E[ri]; t->eval(); if (!t->ent_ready) { if (!t->busy) break; tick(); continue; } tick(); ri++; }
        t->ent_valid = 0; while (t->busy) tick();
        if (draw) {
            t->rd_r = 0xc9; t->eval();
            if (t->rd_q != (uint16_t)(k + 1) || !cs_locked()) { if (bad++ < 10) fprintf(stderr, "frame %ld: locked at %u, want %ld\n", k, t->rd_q, k + 1); }
            compare(k); pics++;
        }
        if (fi >= ne) break;
    }
    printf("eq_cells: %ld frames, %ld locks compared (512 cells, 2048 tiles each), %llu cycles, %lu C freezes: %s\n",
           frames, pics, (unsigned long long)cyc, cs_freezes, bad ? "DIFFERENCES" : "equal");
    return bad != 0;
}
