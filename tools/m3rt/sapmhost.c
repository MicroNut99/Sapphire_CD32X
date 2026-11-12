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
/* docs/58, the options menu's stage select: a new game held to a stage. Found on the PC machine (tools/m2host/sapm_pc.c
   SEC_LOG / SEC_REMAP / RUN_POKES): a new game reads the briefing's program (18 sectors at W_GAME), then one block of
   116 at W_BRIEF - 52 sectors the game's own program, in Super RAM for the whole game, then 64 the first stage's own -
   then the stage's block (596 at W_STAGE); a later stage reads only its own 64 (W_BRIEF_N + 724 a stage) and its block
   (+ 724 a stage), the stage from base RAM $069C. So stage s (2-5) from the start: the machine reset, RUN pressed
   each second until the game's start is read, the block's last 64 sectors served from stage s's, $069C = s - 1 once
   the block's read has begun, and the game loads stage s itself. The drive four times as fast meanwhile (scsi_turbo:
   27 s from the menu to the stage, 41 s without). On the cart this is the engine's to do (the feed's LBA, the poke
   port, the firmware's rate): not written yet */
#define W_GAME 11004
#define W_BRIEF 4196
#define W_BRIEF_OWN (W_BRIEF + 52)
#define W_BRIEF_N 4920
#define W_STAGE 3600
static const uint8_t *rom_kept;                               /* (the System Card: huchost_warp's reset) */
static int warp_stage, warp_phase, warp_lives; static long warp_t;   /* (phase 1: to the game's start; 2: to the stage's
                                                                 block; 3: to the game's own first write of its lives, CD RAM
                                                                 $A00C, 0 until then - the menu's count goes in after it) */
extern int scsi_turbo;
static int sector(void *user, int32_t lba, uint8_t *out)
{
    (void)user; rt_disc_reads++;
    if (warp_phase) {
        if (warp_phase == 1 && lba == W_GAME) warp_phase = 2;
        if (warp_phase == 2) {
            if (lba == W_BRIEF) sapm_base_ram()[0x69C] = (uint8_t)(warp_stage - 1);
            if (warp_stage >= 2 && lba >= W_BRIEF_OWN && lba < W_BRIEF_OWN + 64) lba = W_BRIEF_N + 724 * (warp_stage - 2) + (lba - W_BRIEF_OWN);
            else if (lba >= W_STAGE + 724 * (warp_stage - 1) + 590 && lba < W_STAGE + 724 * (warp_stage - 1) + 596) { warp_phase = 3; scsi_turbo = 1;   /* (the block's end: the drive's own rate again) */
                fprintf(stderr, "sapmhost: stage %d's block read, %ld frames after the reset\n", warp_stage, warp_t); }
        }
    }
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
    /* (docs/59) A frame with more matches than the picture's 4 places: the last match keeps the last place. The game
       scrolls line by line before stage 1's boss (a match on every line from 37 on, the score bar's at 203 the last):
       with the first four kept and every later write landing in the fourth, the bar was gone and its "sprites off"
       sat at line 40 - no ship for four seconds. The lines' own scroll between is not shown */
    if (what == 1) { const int k = sp_n < 4 ? sp_n++ : 3; sp[k].line = (uint16_t)(v->rcr_count + 1); sp[k].bxr = v->BXR; sp[k].byr = v->BYR; sp[k].cr = v->CR; sp_cur = k; }
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
    if (warp_phase) warp_t++;
    if (warp_phase == 3 && sapm_cd_ram()[0xA00C]) { if (warp_lives) sapm_cd_ram()[0xA00C] = (uint8_t)warp_lives; warp_phase = 0;
        fprintf(stderr, "sapmhost: the new game's lives written, %ld frames after the reset\n", warp_t); }
    if (warp_phase == 1) sapm_pad(warp_t >= 120 && warp_t % 60 < 6 ? 8 : 0);   /* (RUN each second: the System Card, the title) */
    else sapm_pad(rec_masks ? (k + 1 < rec_n ? rec_masks[k + 1] : 0) : live_pad);
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
    rom_kept = rom;
    if (getenv("HUC_MASKS")) { long m; rec_masks = slurp(getenv("HUC_MASKS"), &m); rec_n = m / 2; }
    if (getenv("HUC_POKES")) { long m; rec_pokes = slurp(getenv("HUC_POKES"), &m); rec_np = m / 12; }
    sapm_on_frame(at_frame, 0);
    sapm_pad(rec_masks && rec_n ? rec_masks[0] : 0);
    if (getenv("SAPM_SKIP")) {                                /* (docs/57: a test's start - that many frames run here first, with no
                                                                 picture or sound made of them: a stage reached in seconds. The PSG's
                                                                 chip then lacks what was written before: for speed tests, not sound) */
        const long n = atol(getenv("SAPM_SKIP")); sapm_run_frames(n);
        fprintf(stderr, "sapmhost: %ld frames run before the first picture\n", n);
    }
    vdc_line0_hook = line0; vdc_split_hook = split; vdc_scroll_hook = scroll;
    io_psg_hook = psg_tap; io_adpcm_hook = psnd_nibble;
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
/* docs/57 (a measure for the PC): the background of the last picture - the PC Engine palettes its window uses, their
   colours in all, and whether they pack into the Genesis plane's 4 palettes of 15 (a palette's colours stay together;
   greedy, the biggest first: what rt_bg2.c's pack can at best do) */
int huchost_bgstat(int *colours_out, int *fits)
{
    const V32 *v = &pic; const int w = (v->MWR >> 4) & 3, mw = w == 0 ? 32 : w == 1 ? 64 : 128, mh = (v->MWR & 0x40) ? 64 : 32;
    uint16_t used[16]; memset(used, 0, sizeof used);
    if (!(v->t.CR & 0x80)) { *colours_out = 0; *fits = 1; return 0; }
    for (int ty = 0; ty < 29; ty++) for (int tx = 0; tx < 33; tx++) {
        const int my = ((v->BYR >> 3) + ty) & (mh - 1), mx = ((v->BXR >> 3) + tx) & (mw - 1);
        const uint16_t e = v->vram[my * mw + mx]; const unsigned t = e & 0x7FF; uint16_t m = 0;
        for (int y = 0; y < 8; y++) { const uint16_t a = v->vram[(t * 16 + y) & 0x7FFF], b = v->vram[(t * 16 + 8 + y) & 0x7FFF];
            for (int x = 0; x < 8; x++) { const int c = (a >> x & 1) | (a >> (8 + x) & 1) << 1 | (b >> x & 1) << 2 | (b >> (8 + x) & 1) << 3; m |= (uint16_t)(1u << c); } }
        used[e >> 12] |= m;
    }
    int np = 0; uint16_t set[16][16]; int ns[16]; int order[16];
    static uint8_t seen[512]; memset(seen, 0, sizeof seen); int total = 0;
    for (int p = 0; p < 16; p++) { ns[p] = 0; if (used[p] & 0xFFFE) np++;
        for (int i = 1; i < 16; i++) if (used[p] >> i & 1) { const uint16_t c = v->color[p * 16 + i] & 0x1FF; int k; for (k = 0; k < ns[p] && set[p][k] != c; k++) { }
            if (k == ns[p]) set[p][ns[p]++] = c; if (!seen[c]) { seen[c] = 1; total++; } }
        order[p] = p; }
    for (int i = 0; i < 16; i++) for (int j = i + 1; j < 16; j++) if (ns[order[j]] > ns[order[i]]) { const int t = order[i]; order[i] = order[j]; order[j] = t; }
    uint16_t g[4][15]; int gn[4] = { 0, 0, 0, 0 }; int ok = 1;
    for (int oi = 0; oi < 16 && ok; oi++) { const int p = order[oi]; if (!ns[p]) continue; int best = -1, bestadd = 99;
        for (int q = 0; q < 4; q++) { int add = 0; for (int k = 0; k < ns[p]; k++) { int j; for (j = 0; j < gn[q] && g[q][j] != set[p][k]; j++) { } if (j == gn[q]) add++; }
            if (gn[q] + add <= 15 && add < bestadd) { bestadd = add; best = q; } }
        if (best < 0) { ok = 0; break; }
        for (int k = 0; k < ns[p]; k++) { int j; for (j = 0; j < gn[best] && g[best][j] != set[p][k]; j++) { } if (j == gn[best]) g[best][gn[best]++] = set[p][k]; } }
    *colours_out = total; *fits = ok; return np;
}
int huchost_psg(const uint16_t **w) { *w = psgw; const int n = npsg; npsg = 0; return n; }
/* docs/58 (the options menu): a byte into the game's RAM through the cart's poke port - below 0x2000 the base RAM, from
   0x2000 the CD RAM (the recordings' pokes' addresses) */
void huchost_poke(uint32_t a, uint8_t v) { if (a < 0x2000) sapm_base_ram()[a] = v; else if (a - 0x2000 < 0x10000) sapm_cd_ram()[a - 0x2000] = v; }
void huchost_warp(int stage)                                  /* (docs/58: the cart's register - a new game at stage 1-5;
                                                                 bits 8-15: its lives, 0 the game's own) */
{
    warp_lives = stage >> 8 & 0xFF; stage &= 0xFF;
    if (stage < 1 || stage > 5 || !rom_kept) return;
    static uint8_t br[2048]; static uint16_t vr[65536], sa[256], co[512];
    memcpy(br, sapm_bram(), sizeof br);                       /* (the backup RAM stays; the video memory too - its change
                                                                 would be 32,768 words in one picture) */
    memcpy(vr, sapm_vram(), sizeof vr); memcpy(sa, sapm_sat(), sizeof sa); memcpy(co, sapm_colors(), sizeof co);
    for (unsigned c = 0; c < 6; c++) { psg_tap(0, (uint8_t)c); psg_tap(4, 0); }   /* (no note left sounding) */
    if (!sapm_power(rom_kept, br, tab, sector, 0)) return;
    memcpy(sapm_vram(), vr, sizeof vr); memcpy(sapm_sat(), sa, sizeof sa); memcpy(sapm_colors(), co, sizeof co);
    rec_masks = NULL; rec_n = 0; rec_np = 0;                  /* (a recording's pad and pokes end here) */
    warp_stage = stage; warp_phase = 1; warp_t = 0; scsi_turbo = 4; sapm_pad(0);
    fprintf(stderr, "sapmhost: a new game at stage %d\n", stage);
}
const uint16_t *huchost_vram(void) { return vsnap; }
uint32_t huchost_hash(void) { return sapm_hash(0); }
