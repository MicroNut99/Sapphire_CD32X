/* sapm_pc.c - docs/99: sapm.c on the PC, from power-on: the machine as the ares model will run it, checked against a
 * recording's frame hashes.
 *
 *   sapm_pc <System Card (with or without its 512-byte header)> <backup RAM (2 KB) or -> <disc table> <masks> [frames]
 *
 * <disc table>: tools/m3_speedtest.py disc_table (big-endian) followed by each data track's file name, one a line
 * ("<track> <path>"); <masks>: u16 a frame from power-on. Prints each frame's md5 (tools/m0_replay.py frame_hash). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sapm.h"
#include "vdc_model.h"
static long scroll_from = -1;
static void scroll_log(Vdc *v) { if (scroll_from >= 0 && sapm_frame() >= scroll_from) printf("scroll frame %ld line %d BXR %d BYR %d\n", sapm_frame(), (int)v->rcr_count - 64, v->BXR, v->BYR); }

#include "io_model.h"
static FILE *psg_f;
static void psg_log(unsigned r, uint8_t v) { const uint32_t t = (uint32_t)(sapm_now() / 3); const uint8_t b[2] = { (uint8_t)r, v }; fwrite(&t, 4, 1, psg_f); fwrite(b, 1, 2, psg_f); }
static int32_t tab[SAPM_TAB_WORDS];
static const uint16_t *masks; static long nmasks;
static long late, late_n; static int32_t ready = -1;           /* FEED_LATE: the sector given so far */
static int have(void *user, int32_t lba) { (void)user; return lba == ready; }
static void at_frame(void *user, long k);
static FILE *track_f[101];
static int32_t adw_lba = -1;                                   /* (docs/67: the last sector the drive took, for ADW_LOG) */
static int sector(void *user, int32_t lba, uint8_t *out)
{
    adw_lba = lba;
    (void)user;
    {   /* docs/58, SEC_LOG=1: each run of sectors read - its first frame, its first LBA and its length */
        static int on = -1; static int32_t first = -1, next = -1; static long f0;
        if (on < 0) on = getenv("SEC_LOG") != NULL;
        if (on && (lba != next || out == NULL)) { if (first >= 0) printf("read frame %ld lba %d n %d\n", f0, first, next - first); first = lba; f0 = sapm_frame(); }
        if (out == NULL) return 0;                 /* (the run's end: the last run printed) */
        {   /* SEC_REMAP=<lo>:<n>:<to> (docs/58, the stage select's experiment): sectors lo..lo+n-1 come from to.. */
            static int32_t lo = -2, n, to;
            if (lo == -2) { const char *e = getenv("SEC_REMAP"); lo = -1; if (e) sscanf(e, "%d:%d:%d", &lo, &n, &to); }
            if (lo >= 0 && lba >= lo && lba < lo + n) lba = to + (lba - lo);
        }
        {   /* SEC_DUMP=<lba>:<file>: base RAM, CD RAM and Super RAM as that sector is first asked for */
            static int32_t at = -2; static const char *fn;
            if (at == -2) { const char *e = getenv("SEC_DUMP"); at = e ? atoi(e) : -1; fn = e && strchr(e, ':') ? strchr(e, ':') + 1 : NULL; }
            if (fn && lba == at) { FILE *f = fopen(fn, "wb"); if (f) { fwrite(sapm_base_ram(), 1, 8192, f); fwrite(sapm_cd_ram(), 1, 65536, f); fwrite(sapm_super_ram(), 1, 196608, f); fclose(f); } fn = NULL; }
        }
        next = lba + 1;
    }
    for (int t = tab[0]; t <= tab[1]; t++) {
        const int32_t *x = tab + 3 + 6 * t;
        if (lba >= x[0] && lba < x[0] + x[1] && track_f[t]) {
            if (fseek(track_f[t], (long)(lba - x[0]) * 2048, SEEK_SET) || fread(out, 1, 2048, track_f[t]) != 2048) return 0;
            return 1;
        }
    }
    return 0;
}
static void *load(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc(*n + 1); if (fread(b, 1, *n, f) != (size_t)*n) exit(2); fclose(f); return b;
}
/* SPLIT_LOG=<from>,<to> (docs/59): each frame's scroll at the display's first line and at each raster match (the line
   after it, as the stand-in's pictures take them) */
static long sl_from = -1, sl_to;
static void sl_line0(Vdc *v) { const long f = sapm_frame(); if (f >= sl_from && f < sl_to) printf("split frame %ld line 0: BXR %u BYR %u CR %04X MWR %02X colour 0 %03X colour 256 %03X\n", f, v->BXR, v->BYR, v->CR, v->MWR, sapm_colors()[0] & 0x1FF, sapm_colors()[256] & 0x1FF); }
static void sl_split(Vdc *v, int what) { const long f = sapm_frame(); if (f >= sl_from && f < sl_to) { if (what == 1) printf("split frame %ld line %d: BXR %u BYR %u CR %04X (at the match)\n", f, v->rcr_count + 1, v->BXR, v->BYR, v->CR);
        else if (what == 0) printf("split frame %ld   CR write: %04X\n", f, v->CR); } }
static void sl_scroll(Vdc *v) { const long f = sapm_frame(); if (f >= sl_from && f < sl_to) printf("split frame %ld   scroll write: BXR %u BYR %u\n", f, v->BXR, v->BYR); }
/* docs/67, ADW_LOG=1: ADPCM RAM's writes in runs - by the DMA from the drive (with the last sector the drive took when
   the run began) or by the CPU's $180A - a line a run: "adw frame <k> dma <0|1> addr <a> len <n> lba <s>" */
static int adw_dma = -1; static unsigned adw_a0, adw_n; static long adw_f; static int32_t adw_l;
static void adw_flush(void) { if (adw_dma >= 0 && adw_n) printf("adw frame %ld dma %d addr %04X len %u lba %d\n", adw_f, adw_dma, adw_a0, adw_n, adw_l); adw_n = 0; }
static void adw_hook(int dma, unsigned a, uint8_t v)
{
    (void)v;
    if (adw_n && dma == adw_dma && ((adw_a0 + adw_n) & 0xFFFF) == (a & 0xFFFF) && adw_n < 65536) { adw_n++; return; }
    adw_flush(); adw_dma = dma; adw_a0 = a & 0xFFFF; adw_n = 1; adw_f = sapm_frame(); adw_l = adw_lba;
}
int main(int argc, char **argv)
{
    { extern int scsi_turbo; if (getenv("SCSI_TURBO")) scsi_turbo = atoi(getenv("SCSI_TURBO")); }   /* (docs/58: an experiment) */
    if (getenv("SPLIT_LOG")) { sscanf(getenv("SPLIT_LOG"), "%ld,%ld", &sl_from, &sl_to); vdc_line0_hook = sl_line0; vdc_split_hook = sl_split; vdc_scroll_hook = sl_scroll; }
    if (argc < 5) { fprintf(stderr, "sapm_pc <System Card> <backup RAM | -> <disc table> <masks> [frames]\n"); return 2; }
    long n; uint8_t *sc = load(argv[1], &n); const uint8_t *rom = n == 262144 + 512 ? sc + 512 : sc;
    if (n != 262144 && n != 262144 + 512) { fprintf(stderr, "the System Card: %ld bytes\n", n); return 2; }
    uint8_t *br = strcmp(argv[2], "-") ? load(argv[2], &n) : NULL;
    {   FILE *f = fopen(argv[3], "rb"); uint8_t be[SAPM_TAB_WORDS * 4];
        if (!f || fread(be, 1, sizeof be, f) != sizeof be) { fprintf(stderr, "the disc table\n"); return 2; }
        for (int i = 0; i < SAPM_TAB_WORDS; i++) tab[i] = (int32_t)((uint32_t)be[4 * i] << 24 | be[4 * i + 1] << 16 | be[4 * i + 2] << 8 | be[4 * i + 3]);
        char l[1024]; int t;
        while (fgets(l, sizeof l, f)) { char p[1000]; if (sscanf(l, "%d %999[^\n]", &t, p) == 2 && t >= 0 && t < 101) track_f[t] = fopen(p, "rb"); }
        fclose(f); }
    uint16_t *m = load(argv[4], &n); const long nm = n / 2;
    const long frames = argc > 5 ? atol(argv[5]) : nm;
    if (!sapm_power(rom, br, tab, sector, NULL)) return 1;
    sapm_on_frame(at_frame, NULL); masks = m; nmasks = nm;
    if (getenv("ADW_LOG")) io_adw_hook = adw_hook;
    if (getenv("PSG_LOG")) { psg_f = fopen(getenv("PSG_LOG"), "wb"); io_psg_hook = psg_log; }   /* Sapphire docs/54: the PSG's writes, as tools/m3_psglog.py's form */
    if (getenv("SCROLL_LOG")) { scroll_from = atol(getenv("SCROLL_LOG")); vdc_scroll_hook = scroll_log; }   /* docs/105: the scroll's writes, a line each */
    if (getenv("FEED_LATE")) {   /* docs/101: each sector given only after the machine has waited for it FEED_LATE times */
        late = atol(getenv("FEED_LATE")); sapm_feed(have);
        long waits = 0;
        while (sapm_frame() < frames) {
            sapm_run_frames(frames - sapm_frame());
            if (sapm_waiting()) { waits++; if (++late_n >= late) { ready = sapm_drive_next(); late_n = 0; } }
        }
        fprintf(stderr, "feed: %ld waits, %ld sectors missed\n", waits, sapm_missed());
    } else if (getenv("RUN_STEP")) {   /* docs/104: as the ares model runs it - to a time a few hundred clocks on, again and again */
        const int64_t step = atol(getenv("RUN_STEP"));
        while (sapm_frame() < frames) sapm_run_until(sapm_now() + step);
    } else sapm_run_frames(frames);
    adw_flush();
    sector(NULL, -1, NULL);
    return 0;
}
static void at_frame(void *user, long k)                       /* the frame's end: its hash, and the next frame's pad */
{
    (void)user;
    sapm_pad(k + 1 < nmasks ? masks[k + 1] : 0);                /* (taken right after this: the trace host's inp[frame + 1]) */
    if (getenv("AUDIO_LOG")) { static uint32_t last; const uint32_t a = sapm_audio();   /* docs/103: CD-DA's play, as it changes */
        if (a != last) { printf("frame %ld audio: playing %u mode %u start %u\n", k, a >> 31, a >> 24 & 3, a & 0x1FFFFF); last = a; } }
    if (getenv("STAMP_LOG") && k >= atol(getenv("STAMP_LOG"))) {   /* docs/109: VRAM blocks written this frame, and those changed */
        static uint16_t prev[32768]; static int have; const uint16_t *st = sapm_stamps(), *vr = sapm_vram(); int w = 0, c = 0;
        for (int b = 0; b < 512; b++) if (st[b] == (uint16_t)k) { w++; if (!have || memcmp(prev + 64 * b, vr + 64 * b, 128)) c++; }
        memcpy(prev, vr, sizeof prev); have = 1;
        printf("frame %ld stamps: written %d, changed %d\n", k, w, c);
    }
    if (getenv("SND_LOG")) {   /* docs/67: the cart's sound log as the machine makes it (code 0-15 the PSG's $0800-$080F,
                                  0x10 | n the CD unit's $1808 + n but $180A): SND_LOG=<code in hex> prints that code's
                                  writes, SND_LOG=all every one */
        static unsigned rp; static int code = -2;
        if (code == -2) code = strcmp(getenv("SND_LOG"), "all") ? (int)strtol(getenv("SND_LOG"), NULL, 16) : -1;
        unsigned wp; const uint16_t *r = sapm_snd(&wp);
        for (; (rp & 511) != wp; rp++) { const uint16_t e = r[rp & 511];
            if (code < 0 || (int)(e >> 8 & 0x1F) == code) printf("snd frame %ld code %02X value %02X\n", k, e >> 8 & 0x1F, e & 0xFF); }
    }
    if (getenv("LINES_FROM") && k >= atol(getenv("LINES_FROM"))) {   /* docs/105: each frame's line table, appended */
        static FILE *lf; if (!lf) lf = fopen("lines.bin", "wb");
        if (lf) fwrite(sapm_lines(), 2, 263 * 2, lf);
    }
    if (getenv("RAM_LOG")) {   /* docs/58, RAM_LOG=<hex address>[,...] (below 0x2000 base RAM, from 0x2000 CD RAM): each byte as it changes */
        static uint32_t ad[16]; static int na = -1, last[16];
        if (na < 0) { na = 0; for (const char *q = getenv("RAM_LOG"); q && *q && na < 16; ) { ad[na] = (uint32_t)strtoul(q, (char **)&q, 16); last[na++] = -1; if (*q == ',') q++; } }
        for (int i = 0; i < na; i++) { const int v = ad[i] < 0x2000 ? sapm_base_ram()[ad[i]] : sapm_cd_ram()[(ad[i] - 0x2000) & 0xFFFF];
            if (v != last[i]) { printf("ram frame %ld %04X = %02X\n", k, ad[i], v); last[i] = v; } }
    }
    {
        static int quiet = -1; if (quiet < 0) quiet = getenv("QUIET") != NULL;   /* (docs/58: no md5 lines) */
        uint8_t md[16]; if (!quiet) sapm_md5(md);
        if (getenv("PRINT_HASH")) printf("frame %ld hash %08x\n", k, sapm_hash(0));   /* (as the ares model's DRACX_HASHES) */
        if (!quiet) { printf("frame %ld md5 ", k); for (int i = 0; i < 16; i++) printf("%02x", md[i]); printf("\n"); }
        if (getenv("RUN_STATE") && atol(getenv("RUN_STATE")) == k) sapm_state_text();
        const long every = getenv("RUN_DUMP_EVERY") ? atol(getenv("RUN_DUMP_EVERY")) : 0;   /* or every n frames: <file>.<k> */
        if ((getenv("RUN_DUMP") && atol(getenv("RUN_DUMP")) == k) || (every > 0 && k % every == 0 && k >= (getenv("RUN_DUMP_FROM") ? atol(getenv("RUN_DUMP_FROM")) : 0))) {   /* the state after frame k, as trace_main.c's RUN_DUMP */
            char name[1100]; const char *base = getenv("RUN_DUMP_FILE") ? getenv("RUN_DUMP_FILE") : "run_dump.bin";
            if (every > 0) snprintf(name, sizeof name, "%s.%ld", base, k); else snprintf(name, sizeof name, "%s", base);
            FILE *f = fopen(name, "wb");
            if (f && getenv("RUN_DUMP_VIDEO")) fseek(f, 8192 + 65536 + 196608, SEEK_SET);   /* (docs/67, Gate of Thunder's docs/55: the video part
                                                                 alone, the RAM a hole - bg2_host's input) */
            else if (f) { fwrite(sapm_base_ram(), 1, 8192, f); fwrite(sapm_cd_ram(), 1, 65536, f); fwrite(sapm_super_ram(), 1, 196608, f); }
            if (f) { fwrite(sapm_vram(), 2, 65536, f); fwrite(sapm_sat(), 2, 256, f); fwrite(sapm_colors(), 2, 512, f);
                     { uint16_t r[4]; sapm_regs(r, r + 1, r + 2, r + 3); fwrite(r, 2, 4, f); } fclose(f); }   /* + BXR BYR MWR CR */
        }
    }
    {   /* Sapphire docs/53: RUN_POKES=<u32 frame, address, value> - the recording's RAM writes, each before its frame
           (tools/m0_replay.py: below 0x2000 base RAM, from 0x2000 CD RAM), as the stand-in's HUC_POKES */
        static const uint32_t (*pk)[3]; static long np = -1;
        if (np < 0) { np = 0; if (getenv("RUN_POKES")) { long n; pk = load(getenv("RUN_POKES"), &n); np = n / 12; } }
        for (long i = 0; i < np; i++) if (pk[i][0] == (uint32_t)(k + 1)) {   /* (after this frame's hash, before the next frame) */
            const uint32_t a = pk[i][1];
            if (a < 0x2000) sapm_base_ram()[a] = (uint8_t)pk[i][2]; else if (a - 0x2000 < 0x10000) sapm_cd_ram()[a - 0x2000] = (uint8_t)pk[i][2];
        }
    }
}
