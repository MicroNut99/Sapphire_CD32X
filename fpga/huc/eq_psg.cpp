// eq_psg.cpp - Sapphire docs/47: psg.sv (Verilator) against Mednafen's own PCE_PSG (beetle-pce's pce_psg.o) on the
// game's PSG writes (sys_host.c TB_PSGLOG: u32 PSG clock, u8 register, u8 value): every channel's state after each
// write. Usage: eq_psg <log>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#define private public
#include <mednafen/mednafen.h>
#include <mednafen/hw_sound/pce_psg/pce_psg.h>
#undef private
#include "Vpsg.h"
#include "Vpsg___024root.h"
int MDFNSS_StateAction(void *, int, int, SFORMAT *, const char *, bool) { return 1; }
static int32 hr[2][65536 + 64];
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    PCE_PSG m(hr[0], hr[1], PCE_PSG::REVISION_HUC6280);
    Vpsg *r = new Vpsg; r->clk = 0; r->tick = 0; r->we = 0; r->eval();
    auto R = r->rootp;
    uint32_t ts, now = 0, t0 = 0; uint8_t rv[2]; long n = 0, bad = 0, ticks = 0; int first = 1;
    long lsum = 0, obad = 0;
    while (fread(&ts, 4, 1, f) == 1 && fread(rv, 1, 2, f) == 2) {
        if (first) { t0 = ts; first = 0; m.ResetTS(0); }
        const uint32_t t = ts - t0;
        while (now < t) { r->tick = 1; r->clk = 1; r->eval(); r->clk = 0; r->tick = 0; r->eval(); now++; ticks++; }
        m.Write((int32)t, rv[0], rv[1]);
        r->we = 1; r->a = rv[0]; r->v = rv[1]; r->clk = 1; r->eval(); r->clk = 0; r->we = 0; r->eval();
        n++;
        if (rv[0] == 0) continue;                    // (Mednafen's select: no Update, its channels not run to now)
        for (int c = 0; c < 6; c++) {
            const auto &h = m.channel[c];
            const int ok = h.waveform_index == R->psg__DOT__widx[c] && h.dda == R->psg__DOT__dda[c] && h.control == R->psg__DOT__control[c]
                && h.frequency == R->psg__DOT__frequency[c] && (int32_t)h.counter == ((int32_t)(R->psg__DOT__counter[c] << 8) >> 8)
                && (uint32_t)h.freq_cache == R->psg__DOT__fcache[c] && h.vl[0] == R->psg__DOT__vl[c][0] && h.vl[1] == R->psg__DOT__vl[c][1]
                && (c < 4 || ((h.lfsr & 0x3FFFF) == R->psg__DOT__lfsr[c] && (int32_t)h.noisecount == ((int32_t)(R->psg__DOT__noisecount[c] << 8) >> 8)))
                && h.samp_accum == R->psg__DOT__accum[c];
            if (!ok && bad++ < 10)
                printf("write %ld (t %u, %X=%02X) ch %d: Mednafen idx %d dda %d ctl %02X freq %03X cnt %d fc %d vl %d/%d lfsr %05X nc %d | RTL idx %d dda %d ctl %02X freq %03X cnt %d fc %d vl %d/%d lfsr %05X nc %d\n",
                    n, t, rv[0], rv[1], c, h.waveform_index, h.dda, h.control, h.frequency, h.counter, h.freq_cache, h.vl[0], h.vl[1], h.lfsr, h.noisecount,
                    R->psg__DOT__widx[c], R->psg__DOT__dda[c], R->psg__DOT__control[c], R->psg__DOT__frequency[c], (int32_t)(R->psg__DOT__counter[c] << 8) >> 8,
                    R->psg__DOT__fcache[c], R->psg__DOT__vl[c][0], R->psg__DOT__vl[c][1], R->psg__DOT__lfsr[c], (int32_t)(R->psg__DOT__noisecount[c] << 8) >> 8);
        }
        {   // the output: each channel's level as Mednafen's UpdateOutput function for it gives it, from its tables
            int32_t L = 0, Rr = 0;
            for (int c = 0; c < 6; c++) {
                auto &h = m.channel[c]; int32_t a0, a1;
                if (h.UpdateOutput == &PCE_PSG::UpdateOutput_Off) a0 = a1 = 0;
                else if (h.UpdateOutput == &PCE_PSG::UpdateOutput_Noise) { const int sv = ((h.lfsr & 1) << 5) - (h.lfsr & 1); a0 = m.dbtable[h.vl[0]][sv]; a1 = m.dbtable[h.vl[1]][sv]; }
                else if (h.UpdateOutput == &PCE_PSG::UpdateOutput_Norm) { a0 = m.dbtable[h.vl[0]][h.dda]; a1 = m.dbtable[h.vl[1]][h.dda]; }
                else { a0 = (m.dbtable_volonly[h.vl[0]] * (int32)h.samp_accum) >> 13; a1 = (m.dbtable_volonly[h.vl[1]] * (int32)h.samp_accum) >> 13; }
                L += a0; Rr += a1;
            }
            const int32_t ol = (int32_t)(r->out_l << 12) >> 12, orr = (int32_t)(r->out_r << 12) >> 12;
            if ((ol != L || orr != Rr) && obad++ < 5) { printf("write %ld: output Mednafen %d/%d, RTL %d/%d\n", n, L, Rr, ol, orr);
                if (obad == 1) for (int c = 0; c < 6; c++) { auto &h = m.channel[c]; printf("  ch %d ctl %02X nctl %02X fc %d vl %d dda %d accum %d mode %s\n", c, h.control, h.noisectrl, h.freq_cache, h.vl[0], h.dda, h.samp_accum,
                    h.UpdateOutput == &PCE_PSG::UpdateOutput_Off ? "off" : h.UpdateOutput == &PCE_PSG::UpdateOutput_Noise ? "noise" : h.UpdateOutput == &PCE_PSG::UpdateOutput_Norm ? "norm" : "accum"); } }
            lsum += L < 0 ? -L : L;
        }
    }
    printf("psg.sv against Mednafen's PCE_PSG: %ld writes, %ld PSG clocks (%.1f s), %ld channel states differ, %ld outputs differ; mean |left| %ld\n",
           n, ticks, ticks / 7159090.0, bad, obad, n ? lsum / n : 0);
    return bad != 0 || obad != 0;
}
