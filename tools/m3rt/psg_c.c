/* psg_c.c - Sapphire docs/50: fpga/huc/psg.sv in C, clock for clock (the PSG as Mednafen's pce_psg.cpp runs it,
   docs/47), for the stand-in's sound. psg_c_clock() is one PSG clock (the master clock / 3); psg_c_write() a register
   write after the clocks before it; psg_c_out() each side's sum of the channels' levels (dbtable[vl][sample], as
   SetVolume(1.0) builds it). Checked against Mednafen's PCE_PSG by fpga/huc/eq_psg.cpp's PSG_C build (run_eqpsg.sh C=1).
   Mednafen's code is GPL-2.0-or-later; this re-implements its behaviour. */
#include <math.h>
#include <string.h>
#include "psg_c.h"

static int32_t dbtable[32][32], volonly[32];
static int tables;
static void make_tables(void)
{
    for (int vl = 0; vl < 32; vl++) {
        double flub = 1.0 * 1.0 * 8 / 6;
        if (vl) flub /= pow(2, 1.0 / 4 * vl);
        if (vl == 0x1F) flub = 0;
        for (int s = 0; s < 32; s++) dbtable[vl][s] = (int32_t)(flub * (s * 2) * 128);
        volonly[vl] = (int32_t)(flub * 65536);
    }
    tables = 1;
}

static uint32_t fc(const PsgC *p, int c)
{
    if (c == 0 && (p->lfoctrl & 3)) {
        const uint32_t sh = ((p->lfoctrl & 3) - 1) << 1;
        const uint32_t t = (p->ch[0].frequency + ((uint32_t)(p->ch[1].dda - 0x10) << sh)) & 0xFFF;
        return (t ? t : 4096) << 1;
    }
    uint32_t r = (p->ch[c].frequency ? p->ch[c].frequency : 4096) << 1;
    if (c == 1 && (p->lfoctrl & 3)) r *= p->lfofreq ? p->lfofreq : 256;
    return r;
}
static uint32_t nfc(uint8_t n) { const int f = 0x1F - (n & 0x1F); return (f ? (uint32_t)f << 6 : 0x20) << 1; }
static const uint8_t scale_tab[16] = { 0x00, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x10, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F };
static uint8_t getvl(const PsgC *p, int c, int lr)
{
    const uint8_t gb = lr ? p->gbal & 15 : p->gbal >> 4, b = lr ? p->ch[c].balance & 15 : p->ch[c].balance >> 4;
    const int s = (0x1F - scale_tab[gb]) + (0x1F - scale_tab[b]) + (0x1F - (p->ch[c].control & 0x1F));
    return s > 0x1F ? 0x1F : (uint8_t)s;
}

void psg_c_power(PsgC *p)
{
    if (!tables) make_tables();
    memset(p, 0, sizeof *p);
    for (int c = 0; c < 6; c++) {
        p->ch[c].vl[0] = p->ch[c].vl[1] = 0x1F; p->ch[c].fcache = 8192; p->ch[c].counter = 8192;
        p->ch[c].noisecount = 1; p->ch[c].lfsr = 1; p->ch[c].nfcache = c >= 4 ? 3968 : 0;
    }
}

void psg_c_clock(PsgC *p)
{
    int lfo_on = (p->lfoctrl & 3) != 0;
    if (lfo_on && (!(p->ch[1].control & 0x80) || (p->lfoctrl & 0x80))) { lfo_on = 0; p->ch[0].fcache = fc(p, 0); }
    for (int k = 0; k < 6; k++) {
        const int c = k == 0 ? 1 : k == 1 ? 0 : k;
        PsgCh *h = &p->ch[c];
        if (c >= 4) {
            if (--h->noisecount <= 0) {
                const uint32_t l = h->lfsr;
                h->lfsr = (l >> 1) | (((l ^ (l >> 1) ^ (l >> 11) ^ (l >> 12) ^ (l >> 17)) & 1) << 17);
                h->noisecount += (int32_t)h->nfcache;
            }
        }
        if (!(h->control & 0x80) || (c == 1 && (p->lfoctrl & 0x80)) || (h->control & 0x40)) continue;
        if (--h->counter <= 0) {
            h->widx = (h->widx + 1) & 31; h->dda = h->wave[h->widx];
            if (c == 0 && lfo_on) { h->fcache = fc(p, 0); h->counter += h->fcache <= 7 ? 7 : (int32_t)h->fcache; }
            else h->counter += (int32_t)h->fcache;
        }
    }
    if (p->vu_counter) {
        if (--p->vu_counter == 0) {
            const int which = p->vu_which, chn = which >> 2, lr = ((which >> 1) & 1) ^ 1, phase = which & 1;
            if (!phase) { if (chn < 6) p->vu_latch = getvl(p, chn, lr); }
            else if (chn < 6) p->ch[chn].vl[lr] = p->vu_latch;
            p->vu_which = (which + 1) & 31;
            if (p->vu_which) p->vu_counter = phase ? 1 : 255;
            else if (p->vol_pending) { p->vu_counter = 1; p->vol_pending = 0; }
        }
    }
}

void psg_c_write(PsgC *p, unsigned a, uint8_t v)
{
    if (a == 0) { p->select = v & 7; return; }
    const int c = p->select; PsgCh *h = c <= 5 ? &p->ch[c] : 0;
    switch (a) {
    case 1: p->gbal = v; p->vol_pending = 1; break;
    case 2: if (h) { h->frequency = (h->frequency & 0xF00) | v; h->fcache = fc(p, c); } break;
    case 3: if (h) { h->frequency = (h->frequency & 0x0FF) | (uint16_t)(v & 15) << 8; h->fcache = fc(p, c); } break;
    case 4: if (h) {
            if ((h->control & 0x40) && !(v & 0x40)) { h->widx = 0; h->dda = h->wave[0]; h->counter = (int32_t)h->fcache; }
            if (!(h->control & 0x80) && (v & 0x80) && !(v & 0x40)) { h->widx = (h->widx + 1) & 31; h->dda = h->wave[h->widx]; }
            h->control = v; h->fcache = fc(p, c); p->vol_pending = 1;
        } break;
    case 5: if (h) { h->balance = v; p->vol_pending = 1; } break;
    case 6: if (h) {
            v &= 31;
            if (!(h->control & 0x40)) { h->accum = (uint16_t)(h->accum - h->wave[h->widx] + v); h->wave[h->widx] = v; }
            if (!(h->control & 0xC0)) h->widx = (h->widx + 1) & 31;
            if (h->control & 0x80) h->dda = v;
        } break;
    case 7: if (h && c >= 4) { h->noisectrl = v; h->nfcache = nfc(v); } break;
    case 8: p->lfofreq = v; break;
    case 9:
        if (v & 0x80) { p->ch[1].widx = 0; p->ch[1].dda = p->ch[1].wave[0]; p->ch[1].counter = (int32_t)p->ch[1].fcache; }
        p->lfoctrl = v; p->ch[0].fcache = fc(p, 0); p->ch[1].fcache = fc(p, 1); break;
    default: break;
    }
    if (p->vol_pending && !p->vu_counter && !p->vu_which) { p->vu_counter = 1; p->vol_pending = 0; }
}

void psg_c_out(const PsgC *p, int32_t *l, int32_t *r)
{
    int32_t L = 0, R = 0;
    for (int c = 0; c < 6; c++) {
        const PsgCh *h = &p->ch[c];
        if (!(h->control & 0x80)) continue;
        if (c >= 4 && (h->noisectrl & 0x80)) { const int s = (h->lfsr & 1) ? 0x1F : 0; L += dbtable[h->vl[0]][s]; R += dbtable[h->vl[1]][s]; }
        else if ((h->control & 0xC0) == 0x80 && h->fcache <= 7 && (c != 1 || !(p->lfoctrl & 0x80))) {
            L += (volonly[h->vl[0]] * (int32_t)h->accum) >> 13; R += (volonly[h->vl[1]] * (int32_t)h->accum) >> 13; }
        else { L += dbtable[h->vl[0]][h->dda]; R += dbtable[h->vl[1]][h->dda]; }
    }
    *l = L; *r = R;
}
