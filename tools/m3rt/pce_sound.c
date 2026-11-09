/* pce_sound.c - Sapphire docs/44: the PC Engine's PSG and ADPCM as 16-bit mono samples, a frame at a time, for the
 * cart CPU's stand-in (huchost.c) to hand the 32X's PWM.
 *
 * The PSG: six channels of 32 five-bit samples, a 12-bit period at 3.58 MHz, 5-bit volume, 4-bit balance and the main
 * volume (1.5 dB a step for the volume, 3 dB for the others), channels 4-5's noise; DDA writes heard as they come.
 * Its register writes come from the I/O model (io_psg_hook) and act from the frame's next sample on.
 * The ADPCM: the MSM5205's 4-bit ADPCM from ADPCM RAM, the nibbles the drive model's playback passed over in the
 * frame (its read address and nibble before and after), stretched to the frame's samples.
 */
#include <string.h>
#include <math.h>
#include "pce_sound.h"

#define CLK 3579545.0

typedef struct { uint16_t freq; uint8_t ctrl, bal, wave[32], widx, noise, dda; double pos; uint32_t lfsr; double npos; } Ch;
static Ch ch[6]; static uint8_t sel, mainvol;

/* (the writes are kept with their place in the frame - master clocks to its end - and played in order) */
extern int32_t rt_io_until;
static struct { int32_t until; uint8_t r, v; } ev[8192]; static int nev;
void psnd_write(unsigned r, uint8_t v)
{
    if (nev < 8192) { ev[nev].until = rt_io_until; ev[nev].r = (uint8_t)r; ev[nev].v = v; nev++; }
}
static void psnd_apply(unsigned r, uint8_t v)
{
    Ch *c = &ch[sel < 6 ? sel : 0];
    switch (r) {
    case 0: sel = v & 7; break;
    case 1: mainvol = v; break;
    case 2: c->freq = (uint16_t)((c->freq & 0xF00) | v); break;
    case 3: c->freq = (uint16_t)((c->freq & 0x0FF) | (v & 15) << 8); break;
    case 4: if ((c->ctrl & 0x40) && !(v & 0x40)) {} if ((v & 0xC0) == 0x40) c->widx = 0; c->ctrl = v; break;
    case 5: c->bal = v; break;
    case 6: if (c->ctrl & 0x40) c->dda = v & 31; else if (!(c->ctrl & 0x80)) { c->wave[c->widx & 31] = v & 31; c->widx++; } break;
    case 7: if (sel >= 4) c->noise = v; break;
    }
}

static double att(int steps15) { return pow(10.0, -steps15 / 20.0); }

static const int step_tab[49] = { 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
    1166, 1282, 1411, 1552 };
static const int adj[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static int ad_sig, ad_step;
static int16_t ad_buf[8192];
long psnd_nibbles;                                      /* (DIAG: ADPCM nibbles played so far) */

static int adpcm_nib(unsigned n)
{
    int d = step_tab[ad_step] >> 3;
    if (n & 1) d += step_tab[ad_step] >> 2;
    if (n & 2) d += step_tab[ad_step] >> 1;
    if (n & 4) d += step_tab[ad_step];
    ad_sig += (n & 8) ? -d : d;
    if (ad_sig > 2047) ad_sig = 2047; if (ad_sig < -2048) ad_sig = -2048;
    ad_step += adj[n & 7]; if (ad_step < 0) ad_step = 0; if (ad_step > 48) ad_step = 48;
    return ad_sig;
}

int psnd_frame(const uint8_t *adram, unsigned start, unsigned length, unsigned freq, int playing, int16_t *out, int rate, double fps)
{
    static double acc; acc += rate / fps; const int ns = (int)acc; acc -= ns;
    /* the ADPCM (docs/44): from the address the play started at, at its own rate (32,087.5 / (16 - freq) nibbles a
       second), while the drive model says it plays - the model's own read address moves in jumps */
    static int ad_on; static uint32_t ad_pos, ad_left; static double ad_acc;
    int na = 0;
    if (adram && playing) {
        if (!ad_on) { ad_on = 1; ad_pos = (start & 0xFFFF) * 2; ad_left = (length ? length : 0x10000) * 2u; ad_sig = 0; ad_step = 0; ad_acc = 0; }
        ad_acc += 32087.5 / (16 - (freq & 15)) / fps; int k = (int)ad_acc; ad_acc -= k;
        if (k > 4096) k = 4096;
        if ((uint32_t)k > ad_left) k = (int)ad_left;          /* (the play's length: its end, whatever the model says) */
        ad_left -= (uint32_t)k;
        for (; na < k; na++) {
            const uint8_t b = adram[(ad_pos >> 1) & 0xFFFF];
            ad_buf[na] = (int16_t)adpcm_nib((ad_pos & 1) ? b & 15 : b >> 4); ad_pos++; psnd_nibbles++;
        }
    }
    else { ad_on = 0; ad_sig = ad_sig * 7 / 8; }
    const double mv_l = att(((15 - (mainvol >> 4)) * 3)), mv_r = att(((15 - (mainvol & 15)) * 3));
    int e = 0; const double flen = 21477272.0 / fps;
    for (int i = 0; i < ns; i++) {
        while (e < nev && (1.0 - ev[e].until / flen) * ns <= i) { psnd_apply(ev[e].r, ev[e].v); e++; }
        double s = 0;
        for (int c = 0; c < 6; c++) {
            Ch *h = &ch[c];
            if (!(h->ctrl & 0x80)) continue;
            const double vol = att((31 - (h->ctrl & 31)) * 3 / 2) * 0.5 * (att((15 - (h->bal >> 4)) * 3) * mv_l + att((15 - (h->bal & 15)) * 3) * mv_r);
            int smp;
            if (h->ctrl & 0x40) smp = h->dda;
            else if (c >= 4 && (h->noise & 0x80)) {
                const int nf = (~h->noise) & 31; const double rate_n = CLK / (64.0 * (nf ? nf : 32));
                h->npos += rate_n / rate;
                while (h->npos >= 1) { h->npos -= 1; if (!h->lfsr) h->lfsr = 1; h->lfsr = (h->lfsr >> 1) ^ (-(h->lfsr & 1) & 0xB400u); }
                smp = (h->lfsr & 1) ? 31 : 0;
            }
            else {
                const int f = h->freq ? h->freq : 4096;
                h->pos += CLK / f / rate;
                while (h->pos >= 32) h->pos -= 32;
                smp = h->wave[(int)h->pos];
            }
            s += (smp - 16) / 16.0 * vol;
        }
        double a = 0;
        if (na) a = ad_buf[(int)((long)i * na / ns)] / 2048.0;
        double o = s * 0.12 + a * 0.5;                    /* (the PSG at the reference core's level, docs/44) */
        if (o > 1) o = 1; if (o < -1) o = -1;
        out[i] = (int16_t)(o * 32000);
    }
    while (e < nev) { psnd_apply(ev[e].r, ev[e].v); e++; }
    nev = 0;
    return ns;
}
