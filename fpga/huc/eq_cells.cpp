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
static uint16_t M[0x2E000]; static int lat_max = 6;
static bool op; static int op_left; static bool g_sack; static uint16_t g_sq; static uint64_t rnd = 88172645463325252ull;
static int rlat() { rnd ^= rnd << 13; rnd ^= rnd >> 7; rnd ^= rnd << 17; return (int)(rnd % (uint64_t)lat_max) + 1; }
static void tick() {
    // the memory: a request latched, answered after its latency (the acknowledge for one cycle, the word with it)
    t->mack = 0;
    if (op && op_left == 0) {
        const uint32_t a = t->maddr; if (a >= 0x2E000) { fprintf(stderr, "address %05X\n", a); exit(3); }
        if (t->mwe) { uint16_t w = M[a]; if (t->mbe & 2) w = (w & 0x00FF) | (t->mwd & 0xFF00); if (t->mbe & 1) w = (w & 0xFF00) | (t->mwd & 0x00FF); M[a] = w; }
        else t->mrd = M[a];
        t->mack = 1;
    }
    t->clk = 0; t->eval(); g_sack = t->sack; g_sq = t->sq;   // (what the edge sees)
    t->clk = 1; t->eval(); cyc++;
    static long tr0 = getenv("RUN_TRACE") ? atol(getenv("RUN_TRACE")) : -1; if (tr0 >= 0 && ((long)cyc >= tr0 && (long)cyc < tr0 + 40 || cyc % 1000000 == 0)) fprintf(stderr, "c%llu st %d k %d mreq %d mwe %d a %05X op %d left %d mack %d busy %d pend %d on %d\n", (unsigned long long)cyc, (int)R(st), (int)R(k), t->mreq, t->mwe, t->maddr, op, op_left, t->mack, t->busy, (int)R(pend), (int)R(on));
    if (cyc > 400000000ull) { fprintf(stderr, "stuck: state %d k %d op %d mreq %d\n", (int)R(st), (int)R(k), op, t->mreq); exit(4); }
    if (op && op_left == 0) op = false; else if (op) op_left--;
    t->clk = 0; t->eval();
    if (!op && t->mreq) { op = true; op_left = rlat(); }
}
static uint8_t cell_byte(bool fz, int c, int i) { const uint16_t w = M[(fz ? 0x1A000 : 0x8000) + c * 144 + i / 2]; return i & 1 ? (uint8_t)w : (uint8_t)(w >> 8); }
static uint32_t tile_long(bool fz, int tl) { const uint32_t b = (fz ? 0x2D000 : 0x2C000) + 2 * tl; return (uint32_t)M[b] << 16 | M[b + 1]; }
static uint16_t sh2_read(uint32_t a) { t->sreq = 1; t->sa = a; for (;;) { tick(); if (g_sack) { t->sreq = 0; return g_sq; } } }
static void wr(int r, int v) { t->reg_we = 1; t->reg_r = r; t->reg_v = v; tick(); t->reg_we = 0; }
static long bad;
static void compare(long k) {
    for (int c = 0; c < 512; c++) {
        const uint8_t *ref = cs_cell(c); const bool fz = R(cfz)[c];
        for (int i = 0; i < 288; i++) { const uint8_t g = cell_byte(fz, c, i);
            if (g != ref[i]) { if (bad++ < 10) fprintf(stderr, "DIFF frame %ld cell %d byte %d: C %02X RTL %02X (frozen %d)\n", k, c, i, ref[i], g, fz); break; } }
    }
    for (int n = 0; n < 64; n++) {   // (the read port: a few cells' words and tiles' longs, as the SH2 reads them)
        const int c = (int)((k * 37 + n * 113) & 511), i = (n * 18) % 288 & ~1, tl = (int)((k * 101 + n * 29) & 2047);
        const uint32_t a = (R(cfz)[c] ? 0x194000u : 0x170000u) + c * 288 + i;
        const uint16_t w = (uint16_t)(cs_cell(c)[i] << 8 | cs_cell(c)[i + 1]);
        if (sh2_read(a) != w) { if (bad++ < 10) fprintf(stderr, "DIFF port frame %ld cell %d word %d\n", k, c, i); }
        const uint16_t hi = sh2_read(0x1B8000u + 4 * tl), lo = sh2_read(0x1B8000u + 4 * tl + 2);
        if ((uint32_t)(hi << 16 | lo) != cs_tile(tl)) { if (bad++ < 10) fprintf(stderr, "DIFF port frame %ld tile %d\n", k, tl); }
    }
    for (int tl = 0; tl < 2048; tl++) { const uint32_t g = tile_long(R(tfz)[tl], tl);
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
    if (getenv("RUN_LAT")) lat_max = atoi(getenv("RUN_LAT"));
    memcpy(M, vram, sizeof vram);                         // (the VRAM's copy in the memory before the unlock)
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
            if (t->rd_q != (uint16_t)((k + 1) & 0x7FFF) || !cs_locked()) { if (bad++ < 10) fprintf(stderr, "frame %ld: locked at %u, want %ld\n", k, t->rd_q, k + 1); }
            compare(k); pics++;
        }
        if (fi >= ne) break;
    }
    printf("eq_cells: %ld frames, %ld locks compared (512 cells, 2048 tiles each), memory latency 1-%d, %llu cycles, %lu C freezes: %s\n",
           frames, pics, lat_max, (unsigned long long)cyc, cs_freezes, bad ? "DIFFERENCES" : "equal");
    return bad != 0;
}
