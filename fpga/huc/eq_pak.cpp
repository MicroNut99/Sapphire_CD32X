// eq_pak.cpp - Sapphire docs/53: pak_read.sv against tools/m3rt/sapk.c on a packed file, and both against the track.
//   eq_pak <packed .32X> <the data track .iso> [spoiled trials]
// Every sector of the track is read by both and compared with the track's; then the pack is spoiled (a byte changed in
// a sector's piece, its table entry or its sum) and both must give the same answer and, when they pass, the same bytes.
#include "Vpak_read.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
#include "sapk.h"
}
static std::vector<uint8_t> slurp(const char *p) { FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(2); } fseek(f, 0, SEEK_END); long n = ftell(f);
    fseek(f, 0, SEEK_SET); std::vector<uint8_t> v(n); if (fread(v.data(), 1, n, f) != (size_t)n) exit(2); fclose(f); return v; }
static Vpak_read *d; static std::vector<uint8_t> cart; static uint32_t rnd = 12345; static uint64_t clocks, reqs;
static uint32_t rn() { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return rnd; }
static int wait_, pend;
static void tick()
{
    // the cart: a request answered after 0-3 clocks (the SDRAM's turn is not the reader's to know)
    d->m_ack = 0;
    if (d->m_req) { if (!pend) { pend = 1; wait_ = rn() & 3; reqs++; }
        if (wait_ == 0) { d->m_ack = 1; d->m_data = d->m_addr < cart.size() ? cart[d->m_addr] : 0xFF; pend = 0; } else wait_--; }
    else pend = 0;
    d->clk = 0; d->eval(); d->clk = 1; d->eval(); clocks++;
}
static int rtl_sector(uint32_t s, uint8_t *out)
{
    d->sector = s; d->start = 1; tick(); d->start = 0;
    for (long n = 0; !d->done; n++) { tick(); if (n > 4000000) { printf("*** sector %u: no end\n", s); exit(1); } }
    for (int i = 0; i < 2048; i++) { d->b_addr = i; tick(); out[i] = d->b_data; }
    return d->ok;
}
static void reset() { d->rst = 1; d->start = 0; d->m_ack = 0; pend = 0; for (int i = 0; i < 4; i++) tick(); d->rst = 0; tick(); }
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    cart = slurp(argv[1]); const std::vector<uint8_t> trk = slurp(argv[2]); const int spoil = argc > 3 ? atoi(argv[3]) : 2000;
    d = new Vpak_read; reset();
    Sapk k; if (!sapk_open(&k, cart.data(), cart.size())) { printf("*** no SAPK directory\n"); return 1; }
    const uint32_t n = trk.size() / 2048; uint32_t bad = 0; uint8_t a[2048], b[2048];
    for (uint32_t s = 0; s < n; s++) {
        const int oa = sapk_sector(&k, s, a), ob = rtl_sector(s, b);
        if (!oa || !ob || memcmp(a, trk.data() + s * 2048, 2048) || memcmp(b, a, 2048)) { if (bad++ < 5) printf("*** sector %u: C %d RTL %d\n", s, oa, ob); }
    }
    const uint64_t c1 = clocks;
    int oa = sapk_sector(&k, n, a), ob = rtl_sector(n, b);       // past the end
    if (oa || ob) { bad++; printf("*** past the end: C %d RTL %d\n", oa, ob); }
    printf("%u sectors: %u differ; %.0f clocks a sector, %.0f cart bytes a sector; RTL counts %u reads %u bad\n", n, bad, (double)c1 / n, (double)reqs / n, d->reads, d->bad);
    // spoiled packs
    const uint32_t D = SAPK_DIR; auto rl = [&](uint32_t o) { return (uint32_t)cart[o] << 24 | cart[o + 1] << 16 | cart[o + 2] << 8 | cart[o + 3]; };
    int diff = 0, fails = 0, pass = 0;
    for (int t = 0; t < spoil; t++) {
        const uint32_t s = rn() % n, e = rl(D + rl(D + 16) + 4 * s), pc = D + rl(D + 20) + 8 * (e & 0xFFFFFF);
        const uint32_t loc = rl(pc), len = rl(pc + 4), pg = loc >> 24, off = loc & 0xFFFFFF;
        const uint32_t src = pg == 0xFF ? (off < 0x80000 ? 0x400000 + off : off) : (pg << 21) + off;
        uint32_t at; const int what = rn() % 8;
        if (what < 5) at = src + rn() % len; else if (what == 5) at = pc + rn() % 8; else if (what == 6) at = D + rl(D + 24) + 4 * s + rn() % 4;
        else at = D + rl(D + 16) + 4 * s + rn() % 4;
        const uint8_t old = cart[at]; cart[at] = what < 5 && (rn() & 1) ? 0xFF : (uint8_t)rn();
        oa = sapk_sector(&k, s, a); ob = rtl_sector(s, b);
        if (oa != ob || (oa && memcmp(a, b, 2048))) { if (diff++ < 5) printf("*** spoiled %d (sector %u, at 0x%X): C %d RTL %d\n", t, s, at, oa, ob); }
        if (oa) pass++; else fails++;
        cart[at] = old;
    }
    printf("%d spoiled packs: %d differ (%d refused by both, %d passed by both: the byte was as before or the sum held)\n", spoil, diff, fails, pass);
    return bad || diff;
}
