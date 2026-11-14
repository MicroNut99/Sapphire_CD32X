// eq_ac.cpp - sap_ac.sv against tools/m2host/ac_model.c (Mednafen's card), random accesses. Built twice by run_eqac.sh:
// FULL=1 with every register and control value, FULL=0 with what the game uses (docs/62) - and there a write the lean
// card leaves out must raise `unimpl`.
#include "Vsap_ac.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "ac_model.h"
}
static Vsap_ac *t;
static void clk() { t->clk = 0; t->eval(); t->clk = 1; t->eval(); }
int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    const long N = argc > 1 ? atol(argv[1]) : 2000000;
#ifdef LEAN
    const int lean = 1;
#else
    const int lean = 0;
#endif
    static uint8_t ram[0x200000], ram2[0x200000];
    srand(7);
    for (unsigned i = 0; i < sizeof ram; i++) ram2[i] = ram[i] = (uint8_t)rand();
    ArcadeCard ac; uint32_t s[19] = {0}; ac_start(&ac, s, ram);
    t = new Vsap_ac; t->rst = 1; t->step = 0; t->we = 0; clk(); t->rst = 0;
    long bad = 0, rd = 0, wr = 0, regs = 0;
    static const uint8_t leanctl[4] = {0x00, 0x03, 0x11, 0x13};
    for (long i = 0; i < N && bad < 10; i++) {
        unsigned r; const int k = rand() % 10;
        if (k < 5) r = (rand() & 0x30) | (rand() & 1);                 // a data register
        else if (k < 8) r = (rand() & 0x70) | (2 + rand() % 9);         // a port's register (mirrors too)
        else r = 0xE0 | (rand() & 0x1F);
        const int w = rand() & 1; uint8_t v = (uint8_t)rand();
        if (lean && w) {
            if (r < 0x80 && (r & 15) == 9) v = leanctl[rand() & 3];
            if (r == 0xE4) v &= 7;
            if (r == 0xE5) v = 0;
        }
        t->r = r; t->wd = v; t->step = 0; t->we = 0; t->eval();
        const int data = r < 0x80 && (r & 15) < 2;
        if (t->is_data != data) { printf("is_data %02X\n", r); bad++; }
        if (data) {
            const unsigned ix = t->idx;                                  // the memory side's part
            if (w) { ram2[ix] = v; ac_write(&ac, 0x1A00 | r, v); wr++; }
            else { const uint8_t m = ac_read(&ac, 0x1A00 | r); if (m != ram2[ix]) { printf("%ld: read %02X idx %06X: %02X, model %02X\n", i, r, ix, ram2[ix], m); bad++; } rd++; }
            t->step = 1; clk(); t->step = 0;
        } else if (w) { ac_write(&ac, 0x1A00 | r, v); t->we = 1; clk(); t->we = 0; regs++; for (int k = 0; k < 8; k++) clk(); }   /* (docs/65: the lean card's shift takes up to 7 clocks; the CPU's next access is cycles away) */
        else { const uint8_t m = ac_read(&ac, 0x1A00 | r); if (m != t->q) { printf("%ld: reg %02X: %02X, model %02X\n", i, r, t->q, m); bad++; } regs++; }
        if (t->unimpl) { printf("%ld: unimpl after %02X %s %02X\n", i, r, w ? "w" : "r", v); bad++; }
    }
    if (memcmp(ram, ram2, sizeof ram)) { printf("the RAMs differ\n"); bad++; }
    if (lean) {                                                          // what is left out must be flagged
        static const uint8_t tr[][2] = {{0x09, 0x08}, {0x19, 0x20}, {0x29, 0x40}, {0xE4, 0x09}, {0xE5, 0x01}};
        for (auto &x : tr) { t->rst = 1; clk(); t->rst = 0; t->r = x[0]; t->wd = x[1]; t->we = 1; clk(); t->we = 0;
            if (!t->unimpl) { printf("no unimpl for %02X <- %02X\n", x[0], x[1]); bad++; } }
    }
    printf("%s: %ld accesses (%ld data reads, %ld data writes, %ld register), %ld wrong\n", lean ? "lean" : "full", N, rd, wr, regs, bad);
    return bad != 0;
}
