/* sapmhost.c - Sapphire docs/55: the cart CPU's stand-in for ares (huchost.c's interface: huchost_init / _frame /
 * _audio / _psg / _vram) on the machine itself - tools/m2host/sapm.c, the PC Engine CD with the Arcade Card from
 * power-on with no translated code, equal to Mednafen (docs/53) - in place of the runtime and its translated blocks.
 * As Gate of Thunder's and Dracula X's ares models are their C machines (their docs/37, 101). The pictures are
 * fifo_synth's from the machine's video state at each display start (the raster splits kept as rt_vdc.c keeps them);
 * the sound is pce_sound.c's from the PSG's writes and the ADPCM's nibbles, each at its time; the PSG's writes are
 * also given out for the cart's ring (docs/54).
 *
 *   SAPM_SYSCARD=<the System Card>  SAPM_TAB=<the disc's table + its data tracks' files: sapm_pc's>
 *   HUC_SAPK=<the packed .32X>: track 2 from the cart's pack    HUC_MASKS / HUC_POKES: a recording, as huchost.c
 * huchost_init's two arguments (a state, a track) are not used. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sapm.h"
#include "vdc_model.h"
#include "io_model.h"
#include "fifo_synth.h"
#include "pce_sound.h"
#include "sapk.h"

static void *slurp(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); *n = ftell(f);
    fseek(f, 0, SEEK_SET); void *m = malloc(*n ? *n : 1); if (fread(m, 1, *n, f) != (size_t)*n) { fclose(f); return 0; } fclose(f); return m; }
static int32_t tab[SAPM_TAB_WORDS]; static FILE *track_f[101];
static Sapk sapk; static int sapk_on;
uint32_t rt_disc_reads; static long other_missed;
uint32_t huchost_pack_reads(void) { return sapk.reads; } uint32_t huchost_pack_bad(void) { return sapk.bad; }
static int sector(void *user, int32_t lba, uint8_t *out)
{
    (void)user; rt_disc_reads++;
    for (int t = tab[0]; t <= tab[1] && t < 101; t++) {
        const int32_t *x = tab + 3 + 6 * t;
        if (lba < x[0] || lba >= x[0] + x[1]) continue;
        if (t == 2 && sapk_on) { if (sapk_sector(&sapk, (uint32_t)(lba - x[0]), out)) return 1; memset(out, 0, 2048); return 0; }
        if (track_f[t] && !fseek(track_f[t], (long)(lba - x[0]) * 2048, SEEK_SET) && fread(out, 1, 2048, track_f[t]) == 2048) return 1;
    }
    other_missed++; return 0;
}

/* the picture: the FIFO's entries from the video state at each display start */
static FifoShadow shadow; static uint32_t *ents; static int nents, maxents = 1 << 18;
static V32 pic;                                              /* (fifo_synth's view: the fields it reads are filled) */
static struct { uint16_t line, bxr, byr, cr; } sp[4]; static int sp_n, sp_cur = -1;
static uint16_t vsnap[32768];
static void line0(Vdc *v)
{
    for (int i = 0; i < sp_n; i++) { pic.spl[i].line = sp[i].line; pic.spl[i].bxr = sp[i].bxr; pic.spl[i].byr = sp[i].byr; pic.spl[i].cr = sp[i].cr; }
    pic.spl_n = (int8_t)sp_n; sp_n = 0; sp_cur = -1;
    memcpy(pic.vram, v->vram, sizeof pic.vram); memcpy(pic.sat, v->sat, sizeof pic.sat); memcpy(pic.color, v->color, sizeof pic.color);
    pic.DVSSR = v->DVSSR; pic.BXR = v->BXR; pic.BYR = v->BYR; pic.t.CR = v->CR; pic.MWR = v->MWR;
    memcpy(vsnap, v->vram, sizeof vsnap);
    const int n = fifo_synth(&shadow, &pic, ents + nents, maxents - nents);
    nents += n < maxents - nents ? n : maxents - nents;
}
static void split(Vdc *v, int what)
{
    if (what == 1) { if (sp_n < 4) { const int k = sp_n++; sp[k].line = (uint16_t)(v->rcr_count + 1); sp[k].bxr = v->BXR; sp[k].byr = v->BYR; sp[k].cr = v->CR; sp_cur = k; } }
    else if (what < 0) sp_cur = -1;
    else if (sp_cur >= 0) sp[sp_cur].cr = v->CR;
}
static void scroll(Vdc *v) { if (sp_cur >= 0) { sp[sp_cur].bxr = v->BXR; sp[sp_cur].byr = v->BYR; } }

/* the sound */
static int16_t snd[2048]; static int nsnd;
#define SND_RATE 22044
int32_t rt_io_until; static int64_t frame_t0;                /* (pce_sound.c: a PSG write's master clocks to its frame's end) */
static uint16_t psgw[8192]; static int npsg;
static void psg_tap(unsigned r, uint8_t v)
{
    if (npsg < 8192) psgw[npsg++] = (uint16_t)((r & 15) << 8 | v);
    const int64_t u = frame_t0 + 358995 - sapm_now(); rt_io_until = (int32_t)(u < 0 ? 0 : u);
    psnd_write(r, v);
}
static void cdda_events(void)
{
    static int was; static uint32_t at; static int mode; Hw *hw = sapm_hw();
    const int playing = hw->cdda_status == 1;
    if (playing && (!was || hw->read_sec_start != at || hw->play_mode != mode)) {
        int trk = 0;
        for (int t = tab[0]; t <= tab[1] && t < 101; t++) if ((int32_t)hw->read_sec_start >= tab[3 + 6 * t]) trk = t;
        if (nents < maxents) ents[nents++] = 0xBu << 28 | (uint32_t)(hw->play_mode == 3) << 16 | (uint32_t)trk;
        at = hw->read_sec_start; mode = hw->play_mode;
    }
    else if (!playing && was && nents < maxents) ents[nents++] = 0xBu << 28;
    was = playing;
}

/* the pad and a recording's pokes: at each frame's end, for the next frame (as sapm_pc.c) */
static uint16_t *rec_masks; static long rec_n; static uint32_t (*rec_pokes)[3]; static long rec_np; static uint16_t live_pad;
static void at_frame(void *user, long k)
{
    (void)user;
    sapm_pad(rec_masks ? (k + 1 < rec_n ? rec_masks[k + 1] : 0) : live_pad);
    for (long i = 0; i < rec_np; i++) if (rec_pokes[i][0] == (uint32_t)(k + 1)) {
        const uint32_t a = rec_pokes[i][1];
        if (a < 0x2000) sapm_base_ram()[a] = (uint8_t)rec_pokes[i][2]; else if (a - 0x2000 < 0x10000) sapm_cd_ram()[a - 0x2000] = (uint8_t)rec_pokes[i][2];
    }
}

int huchost_init(const char *state, const char *trk)
{
    (void)state; (void)trk;
    const char *sc = getenv("SAPM_SYSCARD"), *tb = getenv("SAPM_TAB");
    if (!sc || !tb) { fprintf(stderr, "sapmhost: SAPM_SYSCARD and SAPM_TAB are needed\n"); return 0; }
    long n; uint8_t *rom = slurp(sc, &n); if (!rom || (n != 262144 && n != 262144 + 512)) { fprintf(stderr, "sapmhost: the System Card\n"); return 0; }
    if (n == 262144 + 512) rom += 512;
    {   FILE *f = fopen(tb, "rb"); uint8_t be[SAPM_TAB_WORDS * 4];
        if (!f || fread(be, 1, sizeof be, f) != sizeof be) { fprintf(stderr, "sapmhost: the disc table\n"); return 0; }
        for (int i = 0; i < SAPM_TAB_WORDS; i++) tab[i] = (int32_t)((uint32_t)be[4 * i] << 24 | be[4 * i + 1] << 16 | be[4 * i + 2] << 8 | be[4 * i + 3]);
        char l[1024]; int t;
        while (fgets(l, sizeof l, f)) { char p[1000]; if (sscanf(l, "%d %999[^\n]", &t, p) == 2 && t >= 0 && t < 101) track_f[t] = fopen(p, "rb"); }
        fclose(f); }
    if (getenv("HUC_SAPK")) { long pn; const uint8_t *pf = slurp(getenv("HUC_SAPK"), &pn);
        sapk_on = pf && sapk_open(&sapk, pf, (uint32_t)pn);
        fprintf(stderr, "sapmhost: track 2 from the cart's pack: %s\n", sapk_on ? "its directory found" : "NO DIRECTORY (the track's file instead)"); }
    ents = malloc(sizeof(uint32_t) * (size_t)maxents); memset(&shadow, 0, sizeof shadow);
    if (!sapm_power(rom, 0, tab, sector, 0)) return 0;
    vdc_line0_hook = line0; vdc_split_hook = split; vdc_scroll_hook = scroll;
    io_psg_hook = psg_tap; io_adpcm_hook = psnd_nibble;
    if (getenv("HUC_MASKS")) { long m; rec_masks = slurp(getenv("HUC_MASKS"), &m); rec_n = m / 2; }
    if (getenv("HUC_POKES")) { long m; rec_pokes = slurp(getenv("HUC_POKES"), &m); rec_np = m / 12; }
    sapm_on_frame(at_frame, 0);
    sapm_pad(rec_masks && rec_n ? rec_masks[0] : 0);
    fprintf(stderr, "sapmhost: the machine from power-on (the System Card, the disc's table%s)\n", rec_masks ? ", a recording's pad" : "");
    return 1;
}

int huchost_frame(uint16_t pad, const uint32_t **entries)
{
    live_pad = pad; nents = 0;
    Hw *hw = sapm_hw(); const int p0 = hw->playing;
    frame_t0 = sapm_now();
    sapm_run_frames(1);
    cdda_events();
    static unsigned ad_len; if (!p0 && hw->playing) ad_len = hw->length;
    nsnd = psnd_frame(hw->ram, hw->addr, ad_len, hw->freq, p0 || hw->playing, snd, SND_RATE, 59.8261);
    *entries = ents;
    return nents;
}
int huchost_audio(const int16_t **samples) { *samples = snd; return nsnd; }
int huchost_psg(const uint16_t **w) { *w = psgw; const int n = npsg; npsg = 0; return n; }
const uint16_t *huchost_vram(void) { return vsnap; }
uint32_t huchost_hash(void) { return sapm_hash(0); }
