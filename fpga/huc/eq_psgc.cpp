// eq_psgc.cpp - Sapphire docs/50: tools/m3rt/psg_c.c (the stand-in's PSG, psg.sv in C) against Mednafen's own PCE_PSG
// on a TB_PSGLOG / m3_psgsyn.py log, as eq_psg.cpp does for psg.sv: every channel's state and both sides' levels after
// each write. Usage: eq_psgc <log>   (run_eqpsg.sh with PSGC=1 builds it)
#include <cstdio>
#include <cstdint>
#define private public
#include <mednafen/mednafen.h>
#include <mednafen/hw_sound/pce_psg/pce_psg.h>
#undef private
extern "C" {
#include "psg_c.h"
}
int MDFNSS_StateAction(void *, int, int, SFORMAT *, const char *, bool) { return 1; }
static int32 hr[2][65536 + 64];
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    PCE_PSG m(hr[0], hr[1], PCE_PSG::REVISION_HUC6280);
    static PsgC p; psg_c_power(&p);
    uint32_t ts, now = 0, t0 = 0; uint8_t rv[2]; long n = 0, bad = 0, obad = 0; int first = 1;
    while (fread(&ts, 4, 1, f) == 1 && fread(rv, 1, 2, f) == 2) {
        if (first) { t0 = ts; first = 0; m.ResetTS(0); }
        const uint32_t t = ts - t0;
        while (now < t) { psg_c_clock(&p); now++; }
        m.Write((int32)t, rv[0], rv[1]); psg_c_write(&p, rv[0], rv[1]); n++;
        if (rv[0] == 0) continue;
        int32_t L = 0, R = 0;
        for (int c = 0; c < 6; c++) {
            auto &h = m.channel[c]; const PsgCh &q = p.ch[c];
            if (!(h.waveform_index == q.widx && h.dda == q.dda && h.control == q.control && h.frequency == q.frequency && h.counter == q.counter
                  && (uint32_t)h.freq_cache == q.fcache && h.vl[0] == q.vl[0] && h.vl[1] == q.vl[1] && h.samp_accum == q.accum
                  && (c < 4 || (h.lfsr == q.lfsr && h.noisecount == q.noisecount))) && bad++ < 5)
                printf("write %ld ch %d: Mednafen idx %d cnt %d fc %d | C idx %d cnt %d fc %u\n", n, c, h.waveform_index, h.counter, h.freq_cache, q.widx, q.counter, q.fcache);
            int32_t a0, a1;
            if (h.UpdateOutput == &PCE_PSG::UpdateOutput_Off) a0 = a1 = 0;
            else if (h.UpdateOutput == &PCE_PSG::UpdateOutput_Noise) { const int sv = ((h.lfsr & 1) << 5) - (h.lfsr & 1); a0 = m.dbtable[h.vl[0]][sv]; a1 = m.dbtable[h.vl[1]][sv]; }
            else if (h.UpdateOutput == &PCE_PSG::UpdateOutput_Norm) { a0 = m.dbtable[h.vl[0]][h.dda]; a1 = m.dbtable[h.vl[1]][h.dda]; }
            else { a0 = (m.dbtable_volonly[h.vl[0]] * (int32)h.samp_accum) >> 13; a1 = (m.dbtable_volonly[h.vl[1]] * (int32)h.samp_accum) >> 13; }
            L += a0; R += a1;
        }
        int32_t cl, cr; psg_c_out(&p, &cl, &cr);
        if ((cl != L || cr != R) && obad++ < 5) printf("write %ld: output Mednafen %d/%d, C %d/%d\n", n, L, R, cl, cr);
    }
    printf("psg_c.c against Mednafen's PCE_PSG: %ld writes, %u PSG clocks, %ld channel states differ, %ld outputs differ\n", n, now, bad, obad);
    return bad || obad;
}
