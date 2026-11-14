/* snd.c - (Sapphire docs/67: Gate of Thunder CD32X's, 4a438c6; SAP_NO_ADPCM - the PSG alone: Sapphire's ADPCM RAM is not
 *  one fixed bank, and the cart has no copy of it yet)
 * Gate of Thunder CD32X docs/54: the PC Engine's sound on the 32X's PWM, by the secondary SH2.
 *
 * The cart core has no PSG (docs/23: it did not fit) and nothing heard its ADPCM. Step 18's core logs the engine's sound
 * writes in a ring of 512 entries the SH2 reads through bank 26 (+ 0x4E000 + 2 x i; the write pointer at + 0x4E400):
 * {the frame's low 3 bits, 1 = a CD unit register, the register's low 4 bits, the byte}. Here: the secondary SH2's DMA
 * channel 1 feeds the PWM from one of two buffers of SND_N stereo samples (the way Doom 32X Resurrection's mixer does:
 * CHCR 0x18E5, PWM control 0x0185); at each buffer's end its interrupt starts the other buffer, takes the log's new
 * writes into a model of the PSG (the registers as Mednafen's pce_psg.cpp has them: 6 channels of 32 5-bit steps, the
 * 1.5 dB volume steps, the two noise channels' 18-bit LFSR; no LFO, no DDA - the game uses neither, docs/06) and of
 * the CD unit's ADPCM, and mixes the buffer just played. Included by main.c (the DX_ENGINE build); the master only sets SND_GO.
 * With a core before step 18 the log reads as the bank's memory: the pointer never moves in a way two reads agree on
 * for long - snd_poll takes at most 511 entries a buffer, so the worst is noise; goteng_disc.sh builds this only with SND=1. */
#define SND_N 256
#define SND_CYCLE 1045                                        /* 23,011,361 / 1044 = 22,041 Hz (NTSC) */
#define SND_MID 522
#define SND_K 10643977u                                       /* 3,579,545 x 65,536 / 22,041: a channel's steps a sample, x its period */
#define SND_LOG (*(volatile uint16_t *)(0x22300000u + 0x4E400u))
#define SND_ENT(i) (*(volatile uint16_t *)(0x22300000u + 0x4E000u + 2u * (i)))

typedef struct {
    uint32_t ph, step;
    uint16_t freq; uint8_t ctrl, bal, noise, widx, dirty, live;
    uint8_t wave[32];
    int16_t wl[32], wr[32];                                   /* the wave's steps at the channel's volume, left and right */
    int16_t nl, nr;                                           /* a noise step's size */
} SndCh;
static SndCh snd_ch[6];
static uint8_t snd_sel, snd_main = 0xFF;
static uint32_t snd_lfsr[2] = { 1, 1 }, snd_nph[2];
static uint32_t snd_buf[2][SND_N];
static int16_t snd_mixb[2 * SND_N];
static int snd_cur; static uint16_t snd_rp;
uint32_t snd_irqs, snd_writes;                                /* (for the SLOG values) */
uint32_t snd_vol = 8;                                         /* (Sapphire docs/67: the menu's EFFECTS VOLUME, 0-8; the master writes it) */
#define SND_VOL (*(volatile uint32_t *)((uintptr_t)&snd_vol | 0x20000000))
uint32_t snd_fader;                                           /* (Sapphire docs/67: the game's last write to the CD unit's fader $180F,
                                                                 the log's code 0x1F - the master follows it with command 104, eng_fade) */
#define SND_FADER (*(volatile uint32_t *)((uintptr_t)&snd_fader | 0x20000000))
/* the CD unit's ADPCM (docs/54): its 64 KB of RAM always hold the data track's sectors 68-99 (docs/06), which the pack puts
   at cart 0x100000 (bank 2, tools/got_pack.py); the registers as tools/m2host/io_model.c has them ($1808 / $1809 the
   address, $180D the command, $180E the rate), the chip an OKI MSM5205 (4 bits a sample, the high half first) */
#ifdef SAP_ADPCM
/* Sapphire docs/67: ADPCM RAM's mirror. Sapphire loads its ADPCM RAM whole, 64 KB at address 0 by the CD unit's DMA
   from 32 sectors, 9 times in the recording (each stage's block begins with its 32; before stages 2-5 a briefing's voice
   bank from LBA 8956 + 32 n; no byte by the CPU's $180A: sapm_pc ADW_LOG=1). The cart's core keeps ADPCM RAM to itself;
   the master copies each sector the DMA moves (by command 101: the frame buffer into the cart) into this mirror - bank
   15 (window 0, the program's bank) + 0x50000, past the program and its table. Which sectors: this SH2 follows the
   loads as Dracula X's eng_ad_log does (its docs/191, Mednafen's pcecd.cpp): from the log's $1808 / $1809 / $180D the
   write address, from $180B the DMA; a sector the drive takes while the DMA is on goes to ADPCM RAM at the write
   address, 2,048 bytes; the sector taken just before the DMA went on is the load's first. The takes come from the master,
   which feeds the sectors: each with the log's write pointer when it was fed (ad_sh.tq_*), so they fall between the
   right log entries. A page (2 KB) is played once the master has copied the sector last assigned to it (seq == done) */
#define SND_AD ((const volatile uint8_t *)0x22050000u)
#define AD_MIR_CART 0x050000u
typedef struct { uint16_t lba[32], seq[32], done[32], tq_pos[64]; uint32_t tq_lba[64], tq_w, tq_r, loads, drops; } AdShare;
AdShare ad_sh;                                                /* (the master writes tq_* and done[], this SH2 the rest) */
#define ADS (*(volatile AdShare *)((uintptr_t)&ad_sh | 0x20000000))
static uint16_t adm_addr, adm_write; static uint8_t adm_cmd, adm_dma, adm_fresh; static uint32_t adm_take, adm_n;
static uint32_t eng_ad_copies, eng_ad_fails;                  /* (the master's: eng.c eng_ad_copy; here for menu.c's STATUS) */
static void adm_assign(uint32_t lba)
{
    /* (64 KB a load at most: Sapphire leaves the DMA on while it reads the rest of a stage's block - 724 sectors - into
       the card, and those do not go to ADPCM RAM: sapm_pc ADW_LOG shows 65,536 bytes a load, every time) */
    if (adm_n >= 65536) return;
    adm_n += 2048;
    if (!(adm_write & 2047)) { const unsigned pg = (adm_write >> 11) & 31; ADS.lba[pg] = (uint16_t)lba; ADS.seq[pg] = (uint16_t)(ADS.seq[pg] + 1);
        if (!pg) ADS.loads = ADS.loads + 1; }
    adm_write = (uint16_t)(adm_write + 2048);                 /* (a load not on a 2 KB edge is not mirrored: none seen) */
}
static void adm_reg(unsigned r, unsigned v)                   /* a CD unit register, $1808 + r */
{
    if (r == 8 || r == 9) { if (!(adm_cmd & 0x80)) adm_addr = r == 9 ? (uint16_t)((adm_addr & 0x00FF) | v << 8) : (uint16_t)((adm_addr & 0xFF00) | v); }
    else if (r == 0xB) { if (!adm_dma && (v & 3)) { adm_n = 0; if (adm_fresh) adm_assign(adm_take); } adm_dma = (uint8_t)(v & 3); }
    else if (r == 0xD) {
        if (v & 0x80) { adm_addr = adm_write = 0; adm_cmd = 0; }
        else { if (!(adm_cmd & 0x02) && (v & 0x02)) adm_write = (v & 1) ? adm_addr : (uint16_t)(adm_addr - 1); adm_cmd = (uint8_t)v; }
    }
    adm_fresh = 0;
}
static void adm_takes(unsigned i)                             /* the takes fed before log entry i (or at the write pointer i) */
{
    while (ADS.tq_r != ADS.tq_w) {
        const unsigned q = ADS.tq_r & 63, pos = ADS.tq_pos[q];
        if (((i - pos) & 511) >= 256) break;                  /* (pos still ahead of i) */
        adm_take = ADS.tq_lba[q]; adm_fresh = 1;
        if (adm_dma) adm_assign(adm_take);
        ADS.tq_r = ADS.tq_r + 1;
    }
}
#else
#define SND_AD ((const volatile uint8_t *)0x22100000u)
#endif
static uint16_t ad_addr, ad_read, ad_len; static uint8_t ad_cmd, ad_play, ad_freq, ad_nib, ad_byte;
static int16_t ad_out; static int ad_val, ad_idx; static uint32_t ad_ph;
uint32_t snd_plays;
static const uint16_t ad_steps[49] = { 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552 };
static void ad_write(unsigned r, unsigned v)
{
    switch (r) {
    case 0x8: case 0x9:
        if (ad_cmd & 0x80) break;
        ad_addr = (r & 1) ? (uint16_t)((ad_addr & 0x00FF) | v << 8) : (uint16_t)((ad_addr & 0xFF00) | v);
        if (ad_cmd & 0x10) ad_len = ad_addr;
        break;
    case 0xD:
        if (v & 0x80) { ad_addr = ad_read = ad_len = 0; ad_cmd = 0; ad_play = 0; ad_nib = 0; break; }
        if (ad_play && !(v & 0x20)) ad_play = 0;
        if (!ad_play && (v & 0x20)) { ad_play = 1; ad_nib = 0; ad_val = 0; ad_idx = 0; ad_ph = 0; snd_plays++; }
        if (v & 0x10) ad_len = ad_addr;
        if (!(ad_cmd & 0x08) && (v & 0x08)) ad_read = (v & 4) ? ad_addr : (uint16_t)(ad_addr - 1);
        ad_cmd = (uint8_t)v;
        break;
    case 0xE: ad_freq = v & 15; break;
    }
}
static void ad_tick(void)                                     /* one of the chip's samples */
{
    if (!ad_nib) {
        if (!ad_len && !(ad_cmd & 0x10)) { if (ad_cmd & 0x40) { ad_play = 0; return; } }
#ifdef SAP_ADPCM
        {   const unsigned pg = (ad_read >> 11) & 31;
            ad_byte = ADS.seq[pg] == ADS.done[pg] ? SND_AD[ad_read] : 0x80; }   /* (a page not there yet: +d / -d, nothing heard) */
#else
        ad_byte = SND_AD[ad_read];
#endif
        ad_read++;
        if (ad_len && !(ad_cmd & 0x10)) ad_len--;
    }
    const unsigned n = ad_nib ? ad_byte & 15 : ad_byte >> 4;
    ad_nib ^= 4;
    const int st = ad_steps[ad_idx]; int d = st >> 3;
    if (n & 4) d += st;
    if (n & 2) d += st >> 1;
    if (n & 1) d += st >> 2;
    ad_val += (n & 8) ? -d : d;
    if (ad_val > 2047) ad_val = 2047; else if (ad_val < -2048) ad_val = -2048;
    ad_idx += (n & 4) ? 2 * (int)(n & 3) + 2 : -1;
    if (ad_idx < 0) ad_idx = 0; else if (ad_idx > 48) ad_idx = 48;
    ad_out = (int16_t)(ad_val >> 2);
}
static volatile uint32_t snd_go_, snd_on_, snd_rst_;
#define SND_GO (*(volatile uint32_t *)((uintptr_t)&snd_go_ | 0x20000000))
#define SND_ON (*(volatile uint32_t *)((uintptr_t)&snd_on_ | 0x20000000))
#define SND_RST (*(volatile uint32_t *)((uintptr_t)&snd_rst_ | 0x20000000))   /* (docs/58: the engine restarted - the chips silent) */
static const uint16_t snd_db[32] = { 256, 215, 181, 152, 128, 108, 91, 76, 64, 54, 45, 38, 32, 27, 23, 19,
                                     16, 14, 11, 10, 8, 7, 6, 5, 4, 3, 3, 2, 2, 2, 1, 0 };   /* 256 x 10^(-1.5 i / 20) */

static void snd_write(unsigned code, unsigned v)
{
    if (code == 0x1F) SND_FADER = v;                          /* (Sapphire docs/67: for eng_fade) */
#ifdef SAP_ADPCM
    if (code & 0x10) adm_reg(code & 15, v);                   /* (docs/67: the loads into ADPCM RAM, for the mirror) */
#endif
#ifdef SAP_NO_ADPCM
    if (code & 0x10) return;                                  /* (Sapphire docs/67: its ADPCM RAM changes - no copy of it yet) */
#endif
    if (code & 0x10) { ad_write(code & 15, v); return; }
    SndCh *c = &snd_ch[snd_sel < 6 ? snd_sel : 0];
    const int has = snd_sel < 6;
    switch (code) {
    case 0: snd_sel = v & 7; break;
    case 1: snd_main = (uint8_t)v; for (int i = 0; i < 6; i++) snd_ch[i].dirty = 1; break;
    case 2: if (has) { c->freq = (uint16_t)((c->freq & 0xF00) | v); c->dirty = 1; } break;
    case 3: if (has) { c->freq = (uint16_t)((c->freq & 0x0FF) | (v & 15) << 8); c->dirty = 1; } break;
    case 4:
        if (!has) break;
        if ((c->ctrl & 0x40) && !(v & 0x40)) c->widx = 0;
        if (!(c->ctrl & 0x80) && (v & 0x80)) c->ph = (uint32_t)c->widx << 16;        /* on: from the wave's index */
        if ((c->ctrl & 0x80) && !(v & 0x80)) c->widx = (uint8_t)(c->ph >> 16 & 31);  /* off: the index where it stopped */
        c->ctrl = (uint8_t)v; c->dirty = 1; break;
    case 5: if (has) { c->bal = (uint8_t)v; c->dirty = 1; } break;
    case 6:
        if (!has) break;
        if (!(c->ctrl & 0x40)) { c->wave[c->widx] = v & 31; c->dirty = 1; }
        if (!(c->ctrl & 0xC0)) c->widx = (c->widx + 1) & 31;
        break;
    case 7: if (snd_sel == 4 || snd_sel == 5) { c->noise = (uint8_t)v; c->dirty = 1; } break;
    }
}
static void snd_poll(void)
{
    if (SND_RST) {                                            /* (docs/58: as the chips after a reset - every channel off, no ADPCM) */
        SND_RST = 0;
        for (int i = 0; i < 6; i++) { snd_ch[i].ctrl = 0; snd_ch[i].noise = 0; snd_ch[i].dirty = 1; }
        ad_play = 0; ad_cmd = 0; ad_out = 0;
#ifdef SAP_ADPCM
        adm_addr = adm_write = 0; adm_cmd = adm_dma = adm_fresh = 0;
#endif
    }
    unsigned wp = SND_LOG & 511;
    for (int k = 0; k < 4; k++) { const unsigned w2 = SND_LOG & 511; if (w2 == wp) break; wp = w2; }   /* (it moves under the read) */
    while (snd_rp != wp) {
#ifdef SAP_ADPCM
        adm_takes(snd_rp);
#endif
        const unsigned e = SND_ENT(snd_rp);
        snd_rp = (snd_rp + 1) & 511;
        snd_write(e >> 8 & 31, e & 255); snd_writes++;
    }
#ifdef SAP_ADPCM
    adm_takes(wp);
#endif
}
static void snd_set(SndCh *c, int n)
{
    c->dirty = 0;
    const int vol = c->ctrl & 31;
    int al = (31 - vol) + (15 - (c->bal >> 4)) * 2 + (15 - (snd_main >> 4)) * 2;
    int ar = (31 - vol) + (15 - (c->bal & 15)) * 2 + (15 - (snd_main & 15)) * 2;
    if (al > 31) al = 31;
    if (ar > 31) ar = 31;
    const int gl = snd_db[al], gr = snd_db[ar];
    const unsigned per = c->freq ? c->freq : 0x1000;
    c->live = (c->ctrl & 0xC0) == 0x80 && (gl | gr) != 0;     /* (DDA: a level, not a sound) */
    if (n >= 4 && (c->noise & 0x80)) {
        const unsigned nf = 31 - (c->noise & 31);
        c->step = SND_K / (nf ? nf * 64 : 32);
        c->nl = (int16_t)(31 * gl >> 5); c->nr = (int16_t)(31 * gr >> 5);
        return;
    }
    if (per < 6) c->live = 0;                                 /* (above the half rate by far: nothing to hear) */
    c->step = SND_K / per;
    for (int i = 0; i < 32; i++) { const int s = 2 * c->wave[i] - 31; c->wl[i] = (int16_t)(s * gl >> 5); c->wr[i] = (int16_t)(s * gr >> 5); }
}
static void snd_mix(uint32_t *out)
{
    int16_t *m = snd_mixb; int any = 0;
#ifdef SAP_ADPCM_ONLY
    for (int n = 6; n < 6; n++) {                             /* (docs/67's test: the ADPCM alone, against Mednafen's) */
#else
    for (int n = 0; n < 6; n++) {
#endif
        SndCh *c = &snd_ch[n];
        if (c->dirty) snd_set(c, n);
        if (!c->live) continue;
        if (!any) { any = 1; for (int i = 0; i < 2 * SND_N; i++) m[i] = 0; }
        if (n >= 4 && (c->noise & 0x80)) {
            uint32_t l = snd_lfsr[n - 4], ph = snd_nph[n - 4]; const uint32_t st = c->step; const int nl = c->nl, nr = c->nr;
            int16_t *p = m;
            for (int i = SND_N; i; i--) {
                ph += st;
                for (unsigned k = ph >> 16; k; k--) l = (l >> 1) | ((l ^ (l >> 1) ^ (l >> 11) ^ (l >> 12) ^ (l >> 17)) & 1) << 17;
                ph &= 0xFFFF;
                if (l & 1) { p[0] = (int16_t)(p[0] + nl); p[1] = (int16_t)(p[1] + nr); }
                else { p[0] = (int16_t)(p[0] - nl); p[1] = (int16_t)(p[1] - nr); }
                p += 2;
            }
            snd_lfsr[n - 4] = l; snd_nph[n - 4] = ph;
            continue;
        }
        uint32_t ph = c->ph; const uint32_t st = c->step; const int16_t *wl = c->wl, *wr = c->wr;
        int16_t *p = m;
        for (int i = SND_N; i; i--) {
            const unsigned x = ph >> 16 & 31;
            p[0] = (int16_t)(p[0] + wl[x]); p[1] = (int16_t)(p[1] + wr[x]); p += 2; ph += st;
        }
        c->ph = ph;
    }
    if (ad_play) {                                            /* the ADPCM: 32,087.5 / (16 - rate) Hz, held between its samples */
        const uint32_t st = 95405u / (16u - ad_freq);         /* (32,087.5 x 65,536 / 22,041) */
        uint32_t ph = ad_ph; int16_t *p = m;
        if (!any) { any = 1; for (int i = 0; i < 2 * SND_N; i++) m[i] = 0; }
        for (int i = SND_N; i; i--) {
            ph += st;
            while (ph >> 16) { ph -= 0x10000; if (ad_play) ad_tick(); }
            p[0] = (int16_t)(p[0] + ad_out); p[1] = (int16_t)(p[1] + ad_out); p += 2;
        }
        ad_ph = ph;
        if (!ad_play) ad_out = 0;
    }
    if (!any) { for (int i = 0; i < SND_N; i++) out[i] = (uint32_t)SND_MID << 16 | SND_MID; return; }
    const int vol = (int)SND_VOL * 3;                         /* (x 3/32: GoT's 3/4 at the menu's 8) */
    for (int i = 0; i < SND_N; i++) {
        int l = m[2 * i] * vol >> 5, r = m[2 * i + 1] * vol >> 5;   /* (the level: by ear against the CD's music, to be set on the console) */
        if (l > 500) l = 500; else if (l < -500) l = -500;
        if (r > 500) r = 500; else if (r < -500) r = -500;
        out[i] = (uint32_t)(SND_MID + l) << 16 | (uint32_t)(SND_MID + r);
    }
}
/* the secondary's DMA channel 1 interrupt (crt0.s sec_dma_irq, every interrupt masked): the buffer has been played */
void sec_dma1_handler(void)
{
    (void)SH2_DMA_CHCR1; SH2_DMA_CHCR1 = 0;                   /* (TE read as 1, then cleared) */
    snd_cur ^= 1;
    SH2_DMA_SAR1 = (uint32_t)snd_buf[snd_cur]; SH2_DMA_TCR1 = SND_N; SH2_DMA_CHCR1 = 0x18E5;
    snd_irqs++;
    snd_poll();
    snd_mix(snd_buf[snd_cur ^ 1]);
}
/* on the secondary, once (its loop sees SND_GO): the PWM, the two buffers at the middle, DMA channel 1, interrupts on */
static void snd_start(void)
{
    SND_ON = 1;
    for (int k = 0; k < 2; k++) for (int i = 0; i < SND_N; i++) snd_buf[k][i] = (uint32_t)SND_MID << 16 | SND_MID;
    for (int i = 0; i < 6; i++) snd_ch[i].dirty = 1;
    snd_rp = SND_LOG & 511; { const unsigned w2 = SND_LOG & 511; if (w2 != snd_rp) snd_rp = SND_LOG & 511; }
    MARS_PWM_CTRL = 0; MARS_PWM_CYCLE = SND_CYCLE; MARS_PWM_CTRL = 0x0185;   /* TM = 1, the DMA request, left and right */
    for (int s = 1; s <= SND_MID; s++)                        /* (up to the middle slowly: no click on the console) */
        for (int k = 0; k < 6; k++) { for (int w = 0; w < 4000 && (MARS_PWM_MONO & 0x8000); w++) { } MARS_PWM_MONO = (uint16_t)s; }
    SH2_DMA_VCR1 = 66;                                        /* (crt0.s: the table's "DMA interrupt (Level 4 & 5)") */
    SH2_INT_IPRA = (uint16_t)((SH2_INT_IPRA & 0xF0FF) | 0x0500);
    SH2_DMA_DMAOR = 1;
    snd_cur = 0;
    SH2_DMA_DAR1 = 0x20004034; SH2_DMA_SAR1 = (uint32_t)snd_buf[0]; SH2_DMA_TCR1 = SND_N; SH2_DMA_CHCR1 = 0x18E5;
    __asm__ volatile("ldc %0,sr" : : "r"(0x00));              /* (the secondary's mask was 13: nothing else is enabled for it) */
}
