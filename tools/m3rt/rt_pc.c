/* rt_pc.c - M3: the 32X runtime on the PC (docs/17): the same C as the SH2 program, run from a 'DXS1' state file.
 *
 *     rt_pc <state.bin> [frames [window dir]]
 *
 * With a window dir and RUN_DISC (the cue sheet), the CD drive is modelled as on the PC host (tools/m2host/scsi_model.c:
 * its state at the window's start, the disc): for comparing runs that use the drive. The 32X has its own (M3b).
 * Prints after each frame the state's hash (rt_hash, with and without the stack page), each time the game starts
 * waiting after an interrupt the hash without the stack page ("idle"), and what the run cost. tools/m2host's
 * trace_check --run prints the same "idle" hashes (RUN_IDLE_HASH), from the model checked against Mednafen.
 * RUN_QUIET: the last frame's hashes only (what dxspeed shows), for timing and profiling the runtime alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"
#include "rt_bg2.h"
uint32_t *rt_ro_hist, *rt_interp_hist;   /* (RUN_ROHIST, RUN_INTERP: docs/11, docs/13) */
#ifdef RT_AC_FPGA
#include "ac_engine.h"
#endif
static long cur_frame_any;                             /* (the frame now: RUN_TRACK_LOG, docs/69) */
#ifdef HUC_MARKS
#include "rt_spr.h"
#endif

static void *alloc(unsigned long n) { return malloc(n); }
static long idle_n, last_frame; static int quiet;
/* RUN_VDIFF (DIAG, docs/45): what each frame changed in VRAM, the colours and the sprite table, against the frame
   before - the map (BAT) words, the 16-word tiles past it, colours, sprite entries - and a window's summary */
static uint16_t pv[32768], pc_[512], ps[256]; static int pv_on;
#ifdef RT_PROV
uint32_t prov_last; static uint32_t prov_src[32768];          /* each VRAM word: the last read before its write */
uint32_t prov_pc;                                              /* the instruction running (prov_gen.py's CHK) */
/* RUN_WRITERS (DIAG, docs/51): each writer (bank:pc) of VRAM words, how many, where and in which frames */
typedef struct { uint32_t key; long n; unsigned lo, hi; long f0, f1; long src[4]; uint32_t s0, s1; } Wr;   /* src: the last read in base RAM, the CD / Super RAM, the I/O page (VRAM read back), elsewhere */
static Wr wr_tab[4096]; static int wr_on = -1; static long wr_frame;
/* RUN_MARKS=<file> (DIAG, docs/52): D6b's rule on the PC. A VRAM word written by the load list's copies (6C:8288,
   8221, 81FA, 823D: the ripped outputs) marks its 16-word unit 'ripped', any other write 'code'. At each frame's end
   every unit marked ripped must be one of the ripped outputs' units (<file>: their hashes, sorted u32); the units
   marked code that changed are the conversions the console would do that frame. */
static uint8_t mk_unit[2048]; static int mk_on = -1;
static int mk_load(uint32_t pc)
{
    if ((pc >> 16) != 0x6C) return 0;
    const unsigned o = pc & 0xFFFF;
    return o == 0x8288 || o == 0x8221 || o == 0x81FA || o == 0x823D;
}
uint32_t prov_unit_pc[2048];                                   /* DIAG: each unit's last writer */
void prov_vram(unsigned a)
{
    if (a < 0x8000) prov_unit_pc[a >> 4] = prov_pc;
    if (a < 0x8000) prov_src[a] = prov_last;
    if (mk_on < 0) mk_on = getenv("RUN_MARKS") != 0;
    if (mk_on && a < 0x8000) mk_unit[a >> 4] = mk_load(prov_pc) ? 1 : 2;
    if (wr_on < 0) wr_on = getenv("RUN_WRITERS") != 0;
    if (!wr_on) return;
    unsigned h = (prov_pc * 2654435761u) >> 20;
    while (wr_tab[h].n && wr_tab[h].key != prov_pc) h = (h + 1) & 4095;
    Wr *w = &wr_tab[h];
    if (!w->n) { w->key = prov_pc; w->lo = w->hi = a; w->f0 = wr_frame; }
    w->n++; if (a < w->lo) w->lo = a; if (a > w->hi) w->hi = a; w->f1 = wr_frame;
    const unsigned b = prov_last >> 13;
    w->src[b == 0xF8 ? 0 : b >= 0x68 && b < 0x88 ? 1 : b == 0xFF ? 2 : 3]++;
    if (b >= 0x68 && b < 0x88) { if (!w->s0 || prov_last < w->s0) w->s0 = prov_last; if (prov_last > w->s1) w->s1 = prov_last; }
}
static void writers_print(void)
{
    for (int i = 0; i < 4096; i++) if (wr_tab[i].n)
        printf("writer %02X:%04X words %ld VRAM %04X-%04X frames %ld-%ld from base RAM %ld, CD/Super RAM %ld (%06X-%06X), VRAM %ld, other %ld\n",
               wr_tab[i].key >> 16, wr_tab[i].key & 0xFFFF, wr_tab[i].n, wr_tab[i].lo, wr_tab[i].hi, wr_tab[i].f0, wr_tab[i].f1,
               wr_tab[i].src[0], wr_tab[i].src[1], wr_tab[i].s0, wr_tab[i].s1, wr_tab[i].src[2], wr_tab[i].src[3]);
}
static long pv_kind[8], pv_bank[256];
#endif
static long vd_satb, vd_where[8], vd_found, vd_notfound, vd_found_bank[256], vd_bg, vd_n, vd_bat, vd_tiles, vd_col, vd_sat, vd_max_bat, vd_max_tiles, vd_frames_tiles, vd_bat_cols;
static void vdiff(Rt *r)
{
    const uint16_t *v = r->vdc.vram; const int mw = (r->vdc.MWR >> 4) & 3, mh = (r->vdc.MWR >> 6) & 1;
    const int w = mw == 0 ? 32 : mw == 1 ? 64 : 128, h = mh ? 64 : 32, bat = w * h;
    if (pv_on) {
        long nb = 0, nt = 0, nc = 0, ns = 0; static uint8_t colh[128];
        for (int c = 0; c < w; c++) colh[c] = 0;
        for (int i = 0; i < bat; i++) if (v[i] != pv[i]) { nb++; colh[i % w] = 1; }
        static uint8_t used[2048];                         /* the tiles the map names */
        for (int t = 0; t < 2048; t++) used[t] = 0;
        for (int i = 0; i < bat; i++) used[v[i] & 0x7FF] = 1;
        for (int t = (bat + 15) / 16; t < 2048; t++) { int d = 0; for (int k = 0; k < 16; k++) d |= v[t * 16 + k] != pv[t * 16 + k];
            if (d && t >= r->vdc.DVSSR / 16 && t < r->vdc.DVSSR / 16 + 16) { vd_satb++; continue; }   /* (the sprite table's
                                                     copy in VRAM: attributes, not patterns) */
            nt += d; if (d && used[t]) vd_bg++;
            if (d && !used[t]) {                       /* a sprite cell's 16 words: found as they are in the RAM banks? */
                int found = 0;
                for (int bk = 0x68; bk < 0x88 && !found; bk++) {
                    const uint8_t *m = r->bank[bk]; if (!m) continue;
                    for (int o = 0; o + 32 <= 8192; o += 2) {
                        if ((uint16_t)(m[o] | m[o + 1] << 8) != v[t * 16]) continue;
                        int k = 1; while (k < 16 && (uint16_t)(m[o + 2 * k] | m[o + 2 * k + 1] << 8) == v[t * 16 + k]) k++;
                        if (k == 16) { found = 1; if (!(vd_found_bank[bk] >> 30)) vd_found_bank[bk]++; break; }
                    }
                }
#ifdef RT_PROV
                {   /* where its words were read from: one straight run (2 bytes a word), and in which bank */
                    const uint32_t s0 = prov_src[t * 16]; int straight = 1;
                    for (int k = 1; k < 16; k++) if (prov_src[t * 16 + k] != s0 + 2 * k) straight = 0;
                    pv_bank[(s0 >> 13) & 0xFF]++;
                    pv_kind[found ? (straight ? 0 : 1) : (straight ? 2 : 3)]++;
                    if (!found && !straight && getenv("RUN_PROV_SHOW") && pv_kind[3] <= atol(getenv("RUN_PROV_SHOW"))) {
                        printf("prov unit %04X frame %ld:", t * 16, r->frame);
                        for (int k = 0; k < 16; k++) printf(" %05X", prov_src[t * 16 + k] & 0x1FFFFF);
                        printf(" | words"); for (int k = 0; k < 4; k++) printf(" %04X", v[t * 16 + k]); printf("\n");
                    }
                }
#endif
                int where = found ? 1 : 0;              /* else: any other mapped bank, any byte offset; the VRAM before */
                for (int bk = 0; bk < 256 && !where; bk++) {
                    const uint8_t *m = r->bank[bk]; if (!m || (bk >= 0x68 && bk < 0x88)) continue;
                    const int len = bk == 0xF7 ? 2048 : 8192;
                    for (int o = 0; o + 32 <= len; o++) {
                        if ((uint16_t)(m[o] | m[o + 1] << 8) != v[t * 16]) continue;
                        int k = 1; while (k < 16 && (uint16_t)(m[o + 2 * k] | m[o + 2 * k + 1] << 8) == v[t * 16 + k]) k++;
                        if (k == 16) { where = bk == 0xF8 ? 2 : 3; break; }
                    }
                }
                for (int bk = 0x68; bk < 0x88 && !where; bk++) {   /* (an odd offset in the CD banks) */
                    const uint8_t *m = r->bank[bk]; if (!m) continue;
                    for (int o = 1; o + 32 <= 8192; o += 2) {
                        if ((uint16_t)(m[o] | m[o + 1] << 8) != v[t * 16]) continue;
                        int k = 1; while (k < 16 && (uint16_t)(m[o + 2 * k] | m[o + 2 * k + 1] << 8) == v[t * 16 + k]) k++;
                        if (k == 16) { where = 4; break; }
                    }
                }
                if (!where) for (int u = 0; u < 2048; u++) {   /* the VRAM as it was: a copy inside the VRAM */
                    int k = 0; while (k < 16 && pv[u * 16 + k] == v[t * 16 + k]) k++;
                    if (k == 16 && u != t) { where = 5; break; }
                }
                if (!where) {                            /* half of it (8 words) from the CD banks: built from pieces? */
                    for (int bk = 0x68; bk < 0x88 && !where; bk++) {
                        const uint8_t *m = r->bank[bk]; if (!m) continue;
                        for (int o = 0; o + 16 <= 8192; o += 2) {
                            if ((uint16_t)(m[o] | m[o + 1] << 8) != v[t * 16]) continue;
                            int k = 1; while (k < 8 && (uint16_t)(m[o + 2 * k] | m[o + 2 * k + 1] << 8) == v[t * 16 + k]) k++;
                            if (k == 8) { where = 6; break; }
                        }
                    }
                }
                {   int z = 1; for (int k = 0; k < 16; k++) z &= v[t * 16 + k] == 0; if (!where && z) where = 7; }
                vd_where[where]++;
                if (found) vd_found++; else vd_notfound++;
            }
        }
        for (int i = 0; i < 512; i++) nc += r->vdc.color[i] != pc_[i];
        for (int i = 0; i < 64; i++) { int d = 0; for (int k = 0; k < 4; k++) d |= r->vdc.sat[i * 4 + k] != ps[i * 4 + k]; ns += d; }
        long cols = 0; for (int c = 0; c < w; c++) cols += colh[c];
        vd_n++; vd_bat += nb; vd_tiles += nt; vd_col += nc; vd_sat += ns; vd_bat_cols += cols;
        if (nb > vd_max_bat) vd_max_bat = nb;
        if (nt > vd_max_tiles) vd_max_tiles = nt;
        if (nt) vd_frames_tiles++;
    }
    for (int i = 0; i < 32768; i++) pv[i] = v[i];
    for (int i = 0; i < 512; i++) pc_[i] = r->vdc.color[i];
    for (int i = 0; i < 256; i++) ps[i] = r->vdc.sat[i];
    pv_on = 1;
}
#ifdef HUC_MARKS
/* RUN_PACK=<m3_rip.py pack's .pack> (docs/53): at each frame's end every unit the marks name an output must hold that
   output's words; the units marked VSRC_CODE that changed are the console's conversions */
extern const uint8_t *rt_keys; extern uint32_t rt_nkeys; extern long rt_entries, rt_entries_found;
static uint16_t **pk_unit;                          /* [id][unit] -> 16 words, or 0 */
static long pk_n;
static void pack_load(const char *fn)
{
    FILE *f = fopen(fn, "rb"); if (!f) { fprintf(stderr, "no pack %s\n", fn); exit(2); }
    uint32_t n; if (fread(&n, 4, 1, f) != 1) exit(2);
    pk_unit = calloc(65536, sizeof *pk_unit);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t id, nu; if (fread(&id, 4, 1, f) != 1 || fread(&nu, 4, 1, f) != 1) exit(2);
        uint16_t *u = calloc(2048 * 16, 2); uint8_t *has = calloc(2048, 1);
        for (uint32_t j = 0; j < nu; j++) { uint16_t a, w[16]; if (fread(&a, 2, 1, f) != 1 || fread(w, 2, 16, f) != 16) exit(2);
            memcpy(u + (a >> 4) * 16, w, 32); has[a >> 4] = 1; }
        pk_unit[id] = u; (void)has; pk_n++;
        /* (a unit the output doesn't write: its words stay 0 and has[] says so) */
        pk_unit[id + 32768] = (uint16_t *)has;
    }
    fclose(f);
}
static void keys_load(const char *fn)
{
    FILE *f = fopen(fn, "rb"); if (!f) { fprintf(stderr, "no keys %s\n", fn); exit(2); }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) exit(2); fclose(f);
    rt_keys = b; rt_nkeys = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}
static void pack_check(Rt *r)
{
    static uint32_t last[2048]; static long bad, notin, conv, maxc, frames, named; static uint8_t stale[2048]; static long early, early_fr, early_max;
    long c = 0;
    const unsigned dv = r->vdc.DVSSR;
    for (unsigned u = 0x110; u < 0x800; u++) {
        if ((u << 4) >= dv && (u << 4) < dv + 256) continue;
        const uint16_t *w = r->vdc.vram + u * 16;
        uint32_t h = 2166136261u; for (int i = 0; i < 16; i++) { h ^= w[i]; h *= 16777619u; }
        const uint16_t id = rt_marks.v[u];
        if (!VSRC_IS_CODE(id)) {
            named++;
            const uint8_t *has = (const uint8_t *)pk_unit[id + 32768];
            if (!pk_unit[id] || !has[u]) { if (h == last[u] && notin++ < 5) printf("pack: unit %04X frame %ld: output %u doesn't write it\n", u << 4, r->frame, id); }
            else if (!memcmp(pk_unit[id] + u * 16, w, 32)) { if (stale[u]) { early++; early_fr += stale[u]; if (stale[u] > early_max) early_max = stale[u]; } stale[u] = 0; }
            else if (h != last[u]) stale[u] = 1;
            else if (++stale[u] == 30) { if (bad++ < 5) {             /* (30 frames the same: no copy still under way) */
                printf("pack: unit %04X frame %ld differs from output %u for 30 frames", u << 4, r->frame, id);
#ifdef RT_PROV
                { extern uint32_t prov_unit_pc[2048]; printf(" (last writer %02X:%04X)", prov_unit_pc[u] >> 16, prov_unit_pc[u] & 0xFFFF); }
#endif
                printf("\n"); } }
        } else if (h != last[u]) c++;
        last[u] = h;
    }
    if (frames++ == 0) c = 0;                       /* (the first frame: every unit counts as changed) */
    conv += c; if (c > maxc) maxc = c;
    if (r->frame == last_frame)
        printf("pack: %ld frames; entries %ld, found in the keys %ld; units named a frame %.0f; wrong %ld, not in their output %ld; code units changed %ld (%.2f a frame, most %ld); copies over frames %ld (frames %ld, longest %ld)\n",
               frames, rt_entries, rt_entries_found, (double)named / frames, bad, notin, conv, (double)conv / frames, maxc, early, early_fr, early_max);
}
#endif
#ifdef HUC_MARKS
/* RUN_DRAWSTAT (DIAG, docs/55): what drawing the sprites from the marks would take, each frame - the visible
   sprites' 16 x 16 cells, ripped (their 4 units tagged with one output) or the code's, and for the code's how many
   a cache of converted cells (by content, least recently used; 16 to 256 of them) would have to convert */
#define DS_N 5
static const int ds_size[DS_N] = { 16, 32, 64, 128, 256 };
static uint32_t ds_key[DS_N][256]; static long ds_use[DS_N][256], ds_conv[DS_N], ds_conv_max[DS_N], ds_clock;
static long ds_frames, ds_cells, ds_rip, ds_code, ds_cells_max, ds_code_max;
static void drawstat(Rt *r)
{
    long cells = 0, rip = 0, code = 0, conv[DS_N] = { 0 };
    for (int i = 0; i < 64; i++) {
        const uint16_t *e = r->vdc.sat + i * 4;
        const int y = (e[0] & 0x3FF) - 64, x = (e[1] & 0x3FF) - 32, pat = e[2], attr = e[3];
        const int cgx = attr & 0x100 ? 2 : 1, cgy = ((attr >> 12) & 3) == 0 ? 1 : ((attr >> 12) & 3) == 1 ? 2 : 4;
        if (y >= 224 || y + cgy * 16 <= 0 || x >= 256 || x + cgx * 16 <= 0) continue;
        const int c0 = ((pat >> 1) & 0x3FF) & ~(cgx - 1) & ~((cgy - 1) * 2);
        for (int cy = 0; cy < cgy; cy++) for (int cx = 0; cx < cgx; cx++) {
            const int c = (c0 + cy * 2 + cx) & 0x1FF; const unsigned u = (unsigned)c * 4;
            cells++;
            const uint16_t t = rt_marks.v[u];
            if (!VSRC_IS_CODE(t) && rt_marks.v[u + 1] == t && rt_marks.v[u + 2] == t && rt_marks.v[u + 3] == t) { rip++; continue; }
            code++;
            uint32_t h = 2166136261u; for (int k = 0; k < 64; k++) { h ^= r->vdc.vram[c * 64 + k]; h *= 16777619u; }
            ds_clock++;
            for (int z = 0; z < DS_N; z++) {
                int hit = -1, lru = 0;
                for (int j = 0; j < ds_size[z]; j++) { if (ds_use[z][j] && ds_key[z][j] == h) { hit = j; break; } if (ds_use[z][j] < ds_use[z][lru]) lru = j; }
                if (hit < 0) { hit = lru; ds_key[z][hit] = h; conv[z]++; }
                ds_use[z][hit] = ds_clock;
            }
        }
    }
    ds_frames++; ds_cells += cells; ds_rip += rip; ds_code += code;
    if (cells > ds_cells_max) ds_cells_max = cells; if (code > ds_code_max) ds_code_max = code;
    for (int z = 0; z < DS_N; z++) { ds_conv[z] += conv[z]; if (conv[z] > ds_conv_max[z]) ds_conv_max[z] = conv[z]; }
    if (r->frame == last_frame) {
        printf("drawstat: %ld frames; cells drawn %.1f a frame (most %ld): ripped %.1f, the code's %.1f (most %ld); conversions a frame (most) with a cache of",
               ds_frames, (double)ds_cells / ds_frames, ds_cells_max, (double)ds_rip / ds_frames, (double)ds_code / ds_frames, ds_code_max);
        for (int z = 0; z < DS_N; z++) printf(" %d: %.2f (%ld)", ds_size[z], (double)ds_conv[z] / ds_frames, ds_conv_max[z]);
        printf("\n");
    }
}
#endif
#ifdef HUC_MARKS
/* RUN_SPR=<cells file> (DIAG, docs/56): rt_spr.c's drawing each frame, into two frame buffers in turn (the 32X's swap),
   against the sprite layer drawn straight from VRAM: every pixel of each buffer's 224 lines of 320 */
static uint8_t spr_fb[2][0x20000], spr_ref[SPR_LINES][256];
static long spr_bad_px, spr_bad_frames, spr_first_bad = -1;
static void spr_ref_draw(const V32 *r, const uint16_t *sat)
{
    memset(spr_ref, 0, sizeof spr_ref);
    extern int spr_ylim;                            /* (Sapphire docs/16: the lines with sprites on, as the drawing's) */
    for (int i = 63; i >= 0; i--) {
        const uint16_t *e = sat + i * 4;
        const int sy = (e[0] & 0x3FF) - 64, sx = (e[1] & 0x3FF) - 32, pat = e[2], attr = e[3];
        const int cgx = attr & 0x100 ? 2 : 1, cgy = ((attr >> 12) & 3) == 0 ? 1 : ((attr >> 12) & 3) == 1 ? 2 : 4;
        const int c0 = ((pat >> 1) & 0x3FF) & ~(cgx - 1) & ~((cgy - 1) * 2);
        const int hf = (attr & 0x800) != 0, vf = (attr & 0x8000) != 0, pal = attr & 15;
        for (int cy = 0; cy < cgy; cy++) for (int cx = 0; cx < cgx; cx++) {
            const uint16_t *w = r->vram + ((c0 + cy * 2 + cx) & 0x1FF) * 64;
            for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
                const int b = 15 - x, v = ((w[y] >> b) & 1) | ((w[16 + y] >> b) & 1) << 1 | ((w[32 + y] >> b) & 1) << 2 | ((w[48 + y] >> b) & 1) << 3;
                const int X = sx + 16 * (hf ? cgx - 1 - cx : cx) + (hf ? 15 - x : x), Y = sy + 16 * (vf ? cgy - 1 - cy : cy) + (vf ? 15 - y : y);
                if (v && X >= 0 && X < 256 && Y >= 0 && Y < spr_ylim) spr_ref[Y][X] = (uint8_t)(pal << 4 | v);
            }
        }
    }
}
/* RUN_BGSTAT (DIAG, docs/59): what the background needs, at each display start, over the whole 64 x 64 map (a 64 x 64
   Genesis plane holds it one for one): its tile kinds (tile + palette); for each palette the colour indices its tiles
   show; whether those fit the Genesis' 4 palettes of 15 by index (each palette's indices kept apart: fades stay
   colour writes) and, if not, by value (docs/02's packer); and what changed since the display start before - map
   words, the tiles the map names, the background colours */
static long bg_n, bg_fit_idx, bg_fit_val, bg_slots_max, bg_kinds_max, bg_tiles_max, bg_pals_max, bg_map_frames,
            bg_map_words, bg_tile_frames, bg_tile_units, bg_col_frames, bg_col_n, bg_slots_sum, bg_kinds_sum, bg_mwr_bad;
static uint16_t bg_pv[0x8000], bg_pc[256]; static int bg_have; static long bg_misfit_art, bg_misfit_play, bg_art, bg_play, bg_play_le4;
static int popc16(unsigned x) { int n = 0; while (x) { n += x & 1; x >>= 1; } return n; }
static int pack4(const uint32_t *set, int nsets, int cap, int byval)
{   /* greedy: largest first, each into the bin it grows least (docs/02); sets: 16-bit index masks or 512-bit values */
    (void)byval; int ord[16]; for (int i = 0; i < nsets; i++) ord[i] = i;
    for (int i = 0; i < nsets; i++) for (int j = i + 1; j < nsets; j++) if (popc16(set[ord[j]]) > popc16(set[ord[i]])) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
    int fill[4] = { 0 };
    for (int k = 0; k < nsets; k++) {
        const int n = popc16(set[ord[k]]); int best = -1;
        for (int b = 0; b < 4; b++) if (fill[b] + n <= cap && (best < 0 || fill[b] > fill[best])) best = b;
        if (best < 0) return 0;
        fill[best] += n;
    }
    return 1;
}
static void bgstat(const V32 *v)
{
    const int mw = bg2_mw(v->MWR), mh = bg2_mh(v->MWR), ms = mw == 32 ? 5 : mw == 64 ? 6 : 7;   /* (Sapphire docs/16) */
    if (mw * mh > 4096) { bg_mwr_bad++; return; }
    static uint8_t seen_kind[0x10000 / 8]; memset(seen_kind, 0, sizeof seen_kind);
    static uint8_t seen_tile[0x800 / 8]; memset(seen_tile, 0, sizeof seen_tile);
    uint32_t used[16] = { 0 }; int kinds = 0, tiles = 0;
    static uint16_t tmask[0x800]; static uint32_t tmask_ok[0x800 / 32]; memset(tmask_ok, 0, sizeof tmask_ok);
    const int win = getenv("RUN_BGSTAT_WIN") != 0;  /* (the visible window at the display start: 33 x 29 entries from
                                                       BXR / BYR - one band; the lines below a raster split not seen) */
    const int tx0 = (v->BXR >> 3) & (mw - 1), ty0 = (v->BYR >> 3) & (mh - 1);
    for (int a = 0; a < mw * mh; a++) {
        if (win && (((a & (mw - 1)) - tx0) & (mw - 1)) > 32) continue;
        if (win && (((a >> ms) - ty0) & (mh - 1)) > 28) continue;
        const uint16_t e = v->vram[a]; const unsigned t = e & 0x7FF, p = e >> 12;
        if (!(tmask_ok[t >> 5] >> (t & 31) & 1)) {   /* (the tile's pixel values used) */
            const uint16_t *w = v->vram + t * 16; unsigned m = 0;
            for (int y = 0; y < 8; y++) { const unsigned p01 = w[y], p23 = w[8 + y];
                for (int x = 0; x < 8; x++) { const unsigned b = 7 - x; const unsigned i = ((p01 >> b) & 1) | ((p01 >> (b + 8)) & 1) << 1 | ((p23 >> b) & 1) << 2 | ((p23 >> (b + 8)) & 1) << 3; m |= 1u << i; } }
            tmask[t] = (uint16_t)(m & ~1u); tmask_ok[t >> 5] |= 1u << (t & 31);
        }
        used[p] |= tmask[t];
        if (!(seen_kind[e >> 3] >> (e & 7) & 1)) { seen_kind[e >> 3] |= 1 << (e & 7); kinds++; }
        if (!(seen_tile[t >> 3] >> (t & 7) & 1)) { seen_tile[t >> 3] |= 1 << (t & 7); tiles++; }
    }
    int slots = 0, pals = 0; uint32_t sets[16]; int ns = 0;
    for (int p = 0; p < 16; p++) if (used[p]) { slots += popc16(used[p]); pals++; sets[ns++] = used[p]; }
    const int fit_idx = pack4(sets, ns, 15, 0);
    int fit_val = fit_idx;
    if (!fit_idx) {                                 /* (by value: each palette's colours as a set; merged where equal) */
        uint32_t vsets[16][16]; int nv[16]; int nsv = 0;
        for (int p = 0; p < 16; p++) if (used[p]) { nv[nsv] = 0;
            for (int i = 1; i < 16; i++) if (used[p] >> i & 1) { const uint32_t c = v->color[p * 16 + i] & 0x1FF; int k;
                for (k = 0; k < nv[nsv]; k++) if (vsets[nsv][k] == c) break; if (k == nv[nsv]) vsets[nsv][nv[nsv]++] = c; }
            nsv++; }
        /* greedy into 4 bins of 15 distinct values */
        uint32_t bin[4][64]; int nb[4] = { 0 }; int ord[16]; for (int i = 0; i < nsv; i++) ord[i] = i;
        for (int i = 0; i < nsv; i++) for (int j = i + 1; j < nsv; j++) if (nv[ord[j]] > nv[ord[i]]) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
        fit_val = 1;
        for (int k = 0; k < nsv && fit_val; k++) { const int s2 = ord[k]; int best = -1, bestgrow = 99;
            for (int b = 0; b < 4; b++) { int grow = 0; for (int i = 0; i < nv[s2]; i++) { int q; for (q = 0; q < nb[b]; q++) if (bin[b][q] == vsets[s2][i]) break; if (q == nb[b]) grow++; }
                if (nb[b] + grow <= 15 && grow < bestgrow) { best = b; bestgrow = grow; } }
            if (best < 0) fit_val = 0;
            else for (int i = 0; i < nv[s2]; i++) { int q; for (q = 0; q < nb[best]; q++) if (bin[best][q] == vsets[s2][i]) break; if (q == nb[best]) bin[best][nb[best]++] = vsets[s2][i]; } }
    }
    if (!fit_val) { if (tiles > 400) bg_misfit_art++; else bg_misfit_play++; }
    if (tiles > 400) bg_art++; else { bg_play++; if (pals <= 4) bg_play_le4++; }
    bg_n++; bg_fit_idx += fit_idx; bg_fit_val += fit_val; bg_slots_sum += slots; bg_kinds_sum += kinds;
    if (slots > bg_slots_max) bg_slots_max = slots; if (kinds > bg_kinds_max) bg_kinds_max = kinds;
    if (tiles > bg_tiles_max) bg_tiles_max = tiles; if (pals > bg_pals_max) bg_pals_max = pals;
    if (bg_have) {                                  /* (what changed since the display start before) */
        long mw = 0, tu = 0, cc = 0;
        for (int a = 0; a < 4096; a++) if (v->vram[a] != bg_pv[a]) mw++;
        for (int t = 0x100; t < 0x800; t++) if (seen_tile[t >> 3] >> (t & 7) & 1) {
            if (memcmp(v->vram + t * 16, bg_pv + t * 16, 32)) tu++; }
        for (int i = 0; i < 256; i++) if (v->color[i] != bg_pc[i]) cc++;
        if (mw) { bg_map_frames++; bg_map_words += mw; } if (tu) { bg_tile_frames++; bg_tile_units += tu; } if (cc) { bg_col_frames++; bg_col_n += cc; }
    }
    memcpy(bg_pv, v->vram, sizeof bg_pv); memcpy(bg_pc, v->color, sizeof bg_pc); bg_have = 1;
}
static long spr_frame_no;
/* RUN_SPR_SKIPSTAT (DIAG, docs/58): what drawing only what changed would save. A sprite is unchanged when its table
   entry and its cells' tags are those of the same entry at this buffer's drawing before, and none of its cells is
   of a copy under way; the boxes to clear are those of the sprites changed, gone or new; an unchanged sprite over one
   of them is drawn again too */
typedef struct { uint16_t e[4]; uint32_t sig; int x0, x1, y0, y1, cells, on; } SkSpr;
static SkSpr sk_last[2][64]; static long sk_n, sk_cells, sk_cells_drawn, sk_words, sk_words_cleared;
static void skipstat(const V32 *v, int b)
{
    SkSpr now[64];
    for (int i = 0; i < 64; i++) {
        SkSpr *k = &now[i]; const uint16_t *e = v->sat + i * 4; memset(k, 0, sizeof *k);
        const int sy = (e[0] & 0x3FF) - 64, sx = (e[1] & 0x3FF) - 32; const unsigned attr = e[3];
        const int cgx = attr & 0x100 ? 2 : 1, hc = (int)(attr >> 12) & 3, cgy = hc == 0 ? 1 : hc == 1 ? 2 : 4;
        if (sy >= 224 || sy + cgy * 16 <= 0 || sx >= 256 || sx + cgx * 16 <= 0) continue;
        k->on = 1; memcpy(k->e, e, 8);
        k->x0 = sx < 0 ? 0 : sx & ~1; k->x1 = (sx + cgx * 16 + 1) & ~1; if (k->x1 > 256) k->x1 = 256;
        k->y0 = sy < 0 ? 0 : sy; k->y1 = sy + cgy * 16 > 224 ? 224 : sy + cgy * 16;
        const int c0 = ((e[2] >> 1) & 0x3FF) & ~(cgx - 1) & ~((cgy - 1) * 2);
        uint32_t h = 2166136261u; int under = 0;
        for (int cy = 0; cy < cgy; cy++) for (int cx = 0; cx < cgx; cx++) {
            const unsigned c = (unsigned)(c0 + cy * 2 + cx) & 0x1FF; k->cells++;
            for (int u = 0; u < 4; u++) { const uint16_t t = rt_marks.v[c * 4 + u]; h = (h ^ t) * 16777619u;
                if (t == rt_marks.tag && !VSRC_IS_CODE(t)) under = 1; }
        }
        k->sig = under ? 0 : (h | 1);                  /* (0: never the same) */
    }
    int same[64], dirty[256][4], nd = 0;
    for (int i = 0; i < 64; i++) {
        const SkSpr *a = &now[i], *o = &sk_last[b][i];
        same[i] = a->on && o->on && a->sig && a->sig == o->sig && !memcmp(a->e, o->e, 8);
        if (same[i]) continue;
        if (o->on) { dirty[nd][0] = o->x0; dirty[nd][1] = o->x1; dirty[nd][2] = o->y0; dirty[nd][3] = o->y1; nd++; }
        if (a->on) { dirty[nd][0] = a->x0; dirty[nd][1] = a->x1; dirty[nd][2] = a->y0; dirty[nd][3] = a->y1; nd++; }
    }
    for (int i = 0; i < 64; i++) {
        const SkSpr *a = &now[i]; if (!a->on) continue;
        int draw = !same[i];
        for (int d = 0; d < nd && !draw; d++) draw = a->x0 < dirty[d][1] && dirty[d][0] < a->x1 && a->y0 < dirty[d][3] && dirty[d][2] < a->y1;
        sk_cells += a->cells; if (draw) sk_cells_drawn += a->cells;
    }
    for (int i = 0; i < 64; i++) { const SkSpr *o = &sk_last[b][i]; if (o->on) { sk_words += (o->x1 - o->x0) / 2 * (o->y1 - o->y0);
        if (!same[i]) sk_words_cleared += (o->x1 - o->x0) / 2 * (o->y1 - o->y0); } }
    memcpy(sk_last[b], now, sizeof now); sk_n++;
}
#ifdef RT_BG
/* RUN_BG (docs/63): the background as the Genesis' plane, D7 (rt_bg.c), at each display start; each picture checked
   against the map (the window it shows; the whole map every 64th), the words it sends counted */
#include "rt_bg.h"
static uint8_t id_used[256]; static long id_pics, id_conf_pics, id_conf_max;
static Bg bg_state; static long bgc_big, bgc_big_off, bgc_big_dark, bgc_n, bgc_bad, bgc_badpics, bgc_whole_bad, bgc_misfit_art, bgc_misfit_play, bgc_skip;
/* RUN_BG_LINES=<m3_lines.py's file> (docs/63): each picture's per-line scroll from the runtime - the BXR / BYR writes
   after the display start, each line taking what was written before its latch (RUN_BG_D clocks after the display
   start's event, then a line every 1,365); Y as the VDC counts it (BYR at line 0, one more each line, a BYR write
   setting it again) - against Mednafen's latched values, line for line */
#ifdef RT_LINES
#define LN 224
typedef struct { int32_t t; uint8_t reg; uint16_t val; } ScEv;
static ScEv sc_ev[4096]; static int sc_n, sc_lines = LN; static uint16_t sc_bx0, sc_by0; static int sc_have; static long sc_frame_at;
static long ln_shift, ln_other, ln_pics, ln_bad_pics, ln_bad_lines, ln_over, ln_frames; static FILE *ln_f; static long ln_off;
static void scroll_hook(V32 *v, int reg)
{
    if (sc_n < 4096) { sc_ev[sc_n].t = v32_access_t - v32_line0_t - (getenv("RUN_BG_NOLATE") || v32_access_t - v32_irq_take > 4000 ? 0 : v32_irq_late); sc_ev[sc_n].reg = (uint8_t)reg; sc_ev[sc_n].val = reg == 7 ? v->BXR : v->BYR; sc_n++; } else ln_over++;
}
static long on_frames;                               /* (rt frames ended: the frame a picture is in) */
static void lines_of(uint16_t *bx, uint16_t *by, int32_t dx, int32_t dy)
{   /* (X and Y latched at their own points of the line: dx, dy clocks after the display start's event) */
    int kx = 0, ky = 0; unsigned x = sc_bx0, y = sc_by0, ylast = sc_by0; int ywrote = 0;
    for (int r = 0; r < LN; r++) {
        while (kx < sc_n && sc_ev[kx].t < dx + 1365 * r) { if (sc_ev[kx].reg == 7) x = sc_ev[kx].val; kx++; }
        while (ky < sc_n && sc_ev[ky].t < dy + 1365 * r) { if (sc_ev[ky].reg == 8) { ylast = sc_ev[ky].val; ywrote = 1; } ky++; }
        if (r == 0) y = ylast; else y = ywrote ? ylast + 1 : y + 1;
        ywrote = 0; bx[r] = (uint16_t)x; by[r] = (uint16_t)(y & 0x1FF);
    }
}
/* what the per-line scroll costs the Genesis each picture: the H scroll table's lines that changed (the table rewritten
   in runs), the vertical offsets (Y - line: one a plane, more is VSPLIT's, docs/04) */
static long hs_pics, hs_changed_pics, hs_words, hs_words_max, vo_max, vo_over2, vo_pics_split;
static void lines_cost(void)
{
    static uint16_t last_x[LN]; static int have;
    uint16_t bx[LN], by[LN]; lines_of(bx, by, 400, 530);
    long w = 0; for (int r = 0; r < sc_lines; r++) w += !have || bx[r] != last_x[r];
    memcpy(last_x, bx, sizeof last_x); have = 1;
    int nv = 0; unsigned vo[LN]; int splits = 0;
    for (int r = 0; r < sc_lines; r++) { const unsigned o = (by[r] - r) & 0x1FF; int k; for (k = 0; k < nv; k++) if (vo[k] == o) break; if (k == nv) vo[nv++] = o;
        if (r && ((by[r] - r) & 0x1FF) != ((by[r - 1] - (r - 1)) & 0x1FF)) splits++; }
    hs_pics++; hs_changed_pics += w != 0; hs_words += w; if (w > hs_words_max) hs_words_max = w;
    if (nv > vo_max) vo_max = nv; vo_over2 += nv > 2; vo_pics_split += splits > 0;
}
static void lines_check(void)
{
    if (sc_have) lines_cost();
    if (!sc_have || !ln_f) return;
    const long fr = sc_frame_at + ln_off; if (fr < 0 || fr >= ln_frames) return;
    if (getenv("RUN_BG_LUNTIL") && fr >= atol(getenv("RUN_BG_LUNTIL"))) return;   /* (where the runtime leaves Mednafen's run: docs/50) */
    static uint8_t raw[240 * 4]; fseek(ln_f, fr * (long)sizeof raw, SEEK_SET);
    if (fread(raw, 1, sizeof raw, ln_f) != sizeof raw) return;
    const char *dv = getenv("RUN_BG_D"), *dyv = getenv("RUN_BG_DY"); const int32_t d = dv ? atoi(dv) : 400, dy = dyv ? atoi(dyv) : 530;
    uint16_t bx[LN], by[LN]; lines_of(bx, by, d, dy);
    long bad = 0;
#define MX(r) ((unsigned)(raw[4 * (r)] << 8 | raw[4 * (r) + 1]))
#define MY(r) ((unsigned)(raw[4 * (r) + 2] << 8 | raw[4 * (r) + 3]) & 0x1FF)
    for (int r = 0; r < sc_lines; r++) { const unsigned mx = MX(r), my = MY(r);
        if (mx != bx[r] || my != by[r]) {
            const int sh = (r > 0 && MX(r - 1) == bx[r] && MY(r - 1) == by[r]) || (r + 1 < LN && MX(r + 1) == bx[r] && MY(r + 1) == by[r])
                || (r + 1 < LN && bx[r + 1] == mx && by[r + 1] == my) || (r > 0 && bx[r - 1] == mx && by[r - 1] == my);
            if (sh) ln_shift++; else ln_other++;
            bad++;
            if (getenv("RUN_BG_LSHOW") && ln_bad_pics < 3 && bad < 4) printf("lines: frame %ld line %d: ours %03X %03X, Mednafen's %03X %03X (events %d)\n", fr, r, bx[r], by[r], mx, my, sc_n); } }
    if (bad && getenv("RUN_BG_LEV") && atol(getenv("RUN_BG_LEV")) == fr) for (int k = 0; k < sc_n; k++)
        printf("  ev %d: t %d (line %.2f) reg %d val %03X\n", k, sc_ev[k].t, (sc_ev[k].t - d) / 1365.0, sc_ev[k].reg, sc_ev[k].val);
    ln_pics++; ln_bad_pics += bad != 0; ln_bad_lines += bad;
}
static void lines_pic_start(V32 *v)                  /* (at each display start: the picture before checked, this one begun) */
{
    lines_check();
    if (getenv("RUN_BG_T0")) { static int32_t last; static long k; if (k++ < 40 || (on_frames >= 835 && on_frames <= 842)) printf("t0: frame %ld at %d, after %d (lines %.3f), access %d\n", on_frames, v32_line0_t, v32_line0_t - last, (v32_line0_t - last) / 1365.0, v32_access_t); last = v32_line0_t; }
    sc_n = 0; sc_bx0 = v->BXR; sc_by0 = v->BYR; sc_have = 1; sc_frame_at = on_frames;
    sc_lines = (int)v->t.vdw_c + 1; if (sc_lines > LN) sc_lines = LN;   /* (the display's lines: Mednafen logs only those) */
}
#endif
static void bg_run_pc(V32 *v)
{
#ifdef RT_LINES
    if (getenv("RUN_BG_LINES")) lines_pic_start(v);
#endif
    static int inited; if (!inited) { bg_init(&bg_state); inited = 1; }
    const long w0 = bg_state.st.tiles_conv * 16 + bg_state.st.map_words + bg_state.st.col_words;
    const int r = bg_frame(&bg_state, v);
    if (getenv("RUN_BG_DUMPAT")) { const long a0 = atol(getenv("RUN_BG_DUMPAT")); const long k = (long)bg_state.clock;
        if (k >= a0 && k <= a0 + 2 || k == a0 + 8 || k == a0 + 9) { printf("pic %ld:", k);
            for (int p = 0; p < 16; p++) { unsigned u = 0; for (int a = 0; a < 4096; a++) if ((v->vram[a] >> 12) == p) u |= bg_state.tmask[v->vram[a] & 0x7FF];
                if (!u) continue; printf(" | p%d g%d", p, bg_state.g[p]); for (int i = 1; i < 16; i++) if (u >> i & 1) printf(" %03X", v->color[p * 16 + i] & 0x1FF); }
            printf("\n"); } }
    {   const long w = bg_state.st.tiles_conv * 16 + bg_state.st.map_words + bg_state.st.col_words - w0;
        if (w >= 1024) { int dark = 1; for (int i = 0; i < 256; i++) if ((i & 15) && (v->color[i] & 0x1FF)) { dark = 0; break; }
            bgc_big++; bgc_big_off += !(v->t.CR & 0x80); bgc_big_dark += dark && (v->t.CR & 0x80); } }
    if (r == 0) { bgc_skip++; return; }
    if (r == 2) { int tiles = 0; static uint8_t s[256]; memset(s, 0, sizeof s);
        for (int a = 0; a < 4096; a++) { const unsigned t = v->vram[a] & 0x7FF; if (!(s[t >> 3] >> (t & 7) & 1)) { s[t >> 3] |= 1 << (t & 7); tiles++; } }
        if (tiles > 400) bgc_misfit_art++; else bgc_misfit_play++; return; }
    if (getenv("RUN_BG_IDENT")) {                   /* (docs/64: one Genesis tile a PC Engine tile - the conflicts, the tiles used) */
        static int8_t pal_of[2048]; static uint32_t pic_of[2048]; static uint32_t pic; pic++; int conf = 0;
        for (int a = 0; a < 4096; a++) { const unsigned t = v->vram[a] & 0x7FF; const int p = v->vram[a] >> 12;
            id_used[t >> 3] |= (uint8_t)(1 << (t & 7));
            if (pic_of[t] != pic) { pic_of[t] = pic; pal_of[t] = (int8_t)p; continue; }
            const int q = pal_of[t]; if (q == p) continue;
            const unsigned u = bg_state.tmask[t]; int same = bg_state.g[p] == bg_state.g[q];
            for (int i = 1; i < 16 && same; i++) if (u >> i & 1) same = bg_state.m[p][i] == bg_state.m[q][i];
            if (!same) { conf++; pal_of[t] = (int8_t)p; } }
        id_pics++; id_conf_pics += conf != 0; if (conf > id_conf_max) id_conf_max = conf;
    }
    const long bad = bg_check(&bg_state, v, 0); bgc_n++; bgc_bad += bad; bgc_badpics += bad != 0;
    if (bad && getenv("RUN_BG_SHOW") && bgc_badpics <= 5) printf("bg: picture %ld wrong pixels %ld\n", bgc_n, bad);
    if ((bgc_n & 63) == 0) bgc_whole_bad += bg_check(&bg_state, v, 1);
}
#endif
#ifdef RT_BG2
/* RUN_BG2 (docs/64): rt_bg2 - the console's background - with two frame buffers taking turns, each picture's list
   for command 79 run into a model of the Genesis' VRAM and colours (the kept runs at the commit), then every pixel of
   the window checked against the PC Engine's map as RUN_BG does */
#include "rt_bg2.h"
static Bg2 bg2; static uint16_t bg2_fb[2][65536]; static uint16_t gvram[32768], gcram[64], gvsram[64];
static long b2_jrn_over, b2_scroll_bad, b2_pics, b2_checked, b2_bad, b2_badpics, b2_whole_bad, b2_skip;
static uint16_t keep_buf[256]; static int keep_n;   /* (the listener's fb_keep) */
static unsigned g_htab = BG2_HSCROLL;                /* (docs/16: register 13's table) */
static uint16_t jrn_buf[2048]; static int jrn_n;     /* (and its fb_jrn: pairs for the other buffer) */
static void gen_run(const uint16_t *L, int kept)   /* the listener's fb_list on a model of the VDP */
{
    static unsigned addr, code, inc = 2;
    for (;;) {
        const unsigned h = *L++, n = h & 0xFFF, k = h & 0xF000;
        if (!h) return;
        if (k == 0x1000) for (unsigned i = 0; i < n; i++) { const unsigned w = *L++;
            if ((w & 0xC000) == 0x8000) { if ((w >> 8 & 0x1F) == 15) inc = w & 0xFF;
                if ((w >> 8 & 0x1F) == 13) g_htab = (w & 0x3F) << 10;   /* (docs/16: the H scroll table shown) */
                continue; }
            if (i + 1 < n) { const unsigned w2 = *L++; i++; code = (w >> 14) | (w2 >> 2 & 0x3C); addr = (w & 0x3FFF) | (w2 & 3) << 14; } }
        else if (k == 0x2000) for (unsigned i = 0; i < n; i++) { const unsigned w = *L++;
            if ((code & 0xF) == 1) gvram[(addr >> 1) & 0x7FFF] = (uint16_t)w; else if ((code & 0xF) == 3) gcram[(addr >> 1) & 63] = (uint16_t)w;
            else if ((code & 0xF) == 5) gvsram[(addr >> 1) & 63] = (uint16_t)w;
            addr += inc; }
        else if (k == 0x3000) { for (unsigned i = 0; i < n; i++) { if (keep_n < 255) keep_buf[keep_n++] = *L; L++; } (void)kept; }
        else if (k == 0x4000) { for (unsigned i = 0; i < n; i++) { if (jrn_n < 2048) jrn_buf[jrn_n++] = *L; else b2_jrn_over++; L++; } }
        else return;
    }
}
static long gen_check(const V32 *v, int whole)
{
    const int mw = bg2_mw(v->MWR), mh = bg2_mh(v->MWR);   /* (Sapphire docs/16: the bands' windows, rt_bg2's) */
    long bad = 0;
    for (int a = 0; a < mw * mh; a++) {
        if (!whole) { int in = 0;
            for (int q = 0; q < bg2.nr && !in; q++)
                in = ((((a & (mw - 1)) - bg2.rx[q]) & (mw - 1)) <= 32 && (((a >> bg2.ms) - bg2.ry[q]) & (mh - 1)) < bg2.rn[q]);
            if (!in) continue; }
        const uint16_t e = v->vram[a], ge = gvram[(BG2_PLANE_A >> 1) + a]; const uint16_t *w = v->vram + (e & 0x7FF) * 16;
        const uint16_t *gt = gvram + (ge & 0x7FF) * 16; const int g = ge >> 13 & 3, p = e >> 12;
        for (int y = 0; y < 8; y++) { const unsigned p01 = w[y], p23 = w[8 + y]; const uint32_t row = (uint32_t)gt[2 * y] << 16 | gt[2 * y + 1];
            for (int x = 0; x < 8; x++) { const unsigned sh = 7 - x;
                const unsigned i = ((p01 >> sh) & 1) | ((p01 >> (sh + 8)) & 1) << 1 | ((p23 >> sh) & 1) << 2 | ((p23 >> (sh + 8)) & 1) << 3;
                const unsigned n = row >> (28 - 4 * x) & 15, c = v->color[p * 16 + i] & 0x1FF;
                const unsigned want = i ? (unsigned)((c & 7) << 9 | (c >> 6 & 7) << 5 | (c >> 3 & 7) << 1) : 0x8000, got = n ? gcram[16 * g + n] : 0x8000;
                if (want != got && getenv("RUN_BG2_WHY") && !whole) { static long shown; if (shown++ < 3) {
                    const unsigned t = e & 0x7FF, sl = ge & 0x7FF; const uint16_t *f = bg2_fb[bg2.cur ^ 1];
                    printf("bg2 why: entry %d tile %03X pal %d (g %d) -> ge %04X slot %u key %04X cver %u tag %04X refs %u; tile tag %04X marks %04X mask %04X prim %u; px %d,%d i %u n %u want %03X got %03X; ver %u chg %u m %u\n",
                        a, t, p, bg2.g[p], ge, sl, f[(BG2_T_SLOT >> 1) + 4 * sl], f[(BG2_T_SLOT >> 1) + 4 * sl + 1], f[(BG2_T_SLOT >> 1) + 4 * sl + 2], f[(BG2_T_SLOT >> 1) + 4 * sl + 3],
                        f[(BG2_T_TAG >> 1) + t], rt_marks.v[t], f[(BG2_T_MASK >> 1) + t], f[(BG2_T_PRIM >> 1) + t], x, y, i, n, want, got, bg2.ver[p], bg2.chg[p][i], bg2.m[p][i]); } }
                bad += want != got; } } }
    return bad;
}
static long *runs_p; static void runs_print(void) { if (runs_p) printf("bg2 late runs: 1 %ld, 2-4 %ld, 5-9 %ld, 10-29 %ld, 30-99 %ld, 100+ %ld\n", runs_p[0], runs_p[1], runs_p[2], runs_p[3], runs_p[4], runs_p[5]); }
static void atexit_runs(long *r) { runs_p = r; atexit(runs_print); }
static int slots_in_use(void) { int n = 0; const uint16_t *f = bg2_fb[bg2.cur]; for (int s = 0; s < BG2_SLOTS; s++) n += f[(BG2_T_SLOT >> 1) + 4 * s + 3] != 0; return n; }
static void bg2_run_pc(V32 *v)
{
    static int inited, cur; if (!inited) { bg2_init(&bg2, bg2_fb[0], bg2_fb[1]); bg2_fill(&bg2, 0); bg2_fill(&bg2, 1); inited = 1; }
    const long misfit0 = bg2.st.misfit;
    const int n = bg2_frame(&bg2, v, cur);
    if (!n) { b2_skip++; return; }
    gen_run(bg2_fb[cur] + (BG2_LIST >> 1), 0);
    { keep_buf[keep_n] = 0; static uint16_t k[256]; memcpy(k, keep_buf, sizeof k); keep_n = 0; gen_run(k, 1);   /* (the commit, after the swap: */
      for (int i = 0; i + 1 < jrn_n; i += 2) bg2_fb[cur ^ 1][jrn_buf[i]] = jrn_buf[i + 1]; jrn_n = 0; }               /*  then the pairs, into the next buffer) */
    cur ^= 1; b2_pics++;
    { static long run, runs[6]; static int was;   /* (the pictures still sending, by the length of their runs) */
      if (bg2.late) run++; else if (run) { runs[run < 2 ? 0 : run < 5 ? 1 : run < 10 ? 2 : run < 30 ? 3 : run < 100 ? 4 : 5]++;
          if (getenv("RUN_BG2_RUNS") && run >= 30) printf("bg2 late run: %ld pictures to %ld (dirty %d)\n", run, b2_pics, bg2.ndirty); run = 0; }
      (void)was; if (getenv("RUN_BG2_RUNS") && b2_pics == 1) atexit_runs(runs); }
    if (getenv("RUN_BG2_LATE") && (bg2.late || bg2.st.misfit != misfit0)) printf("bg2 not checked: picture %ld (%s)\n", b2_pics - 1, bg2.late ? "still sending" : "not fitting");
    if (bg2.late || bg2.st.misfit != misfit0) return;
    {   /* (docs/16) each line's H scroll in the table shown, the top's V in VSRAM, the splits as sent for command 78 */
        int sbad = gvsram[0] != (v->BYR & 0x3FF) || gvsram[1] != gvsram[0];
        for (int k = 0, l = 0; k < bg2.nb; k++) { const int end = k + 1 < bg2.nb ? bg2.bl[k + 1] : 224;
            for (; l < end; l++) sbad |= gvram[(g_htab >> 1) + 2 * l] != ((32 - bg2.bx[k]) & 0x3FF) || gvram[(g_htab >> 1) + 2 * l + 1] != gvram[(g_htab >> 1) + 2 * l]; }
        for (int k = 1; k < bg2.nb; k++) sbad |= bg2.vsp_e[k] != ((uint32_t)bg2.bl[k] << 20 | (uint32_t)((bg2.by[k] - bg2.bl[k]) & 0x3FF) << 10 | ((bg2.by[k] - bg2.bl[k]) & 0x3FF));
        if (sbad) { b2_scroll_bad++; if (getenv("RUN_BG_SHOW") && b2_scroll_bad < 4) {
            printf("bg2 scroll wrong: picture %ld (bands %d, table %04X, htab %d) vsram %03X BYR %03X; bands:", b2_pics, bg2.nb, g_htab, bg2.htab, gvsram[0], v->BYR);
            for (int k = 0; k < bg2.nb; k++) printf(" [L%d x%03X y%03X]", bg2.bl[k], bg2.bx[k], bg2.by[k]);
            printf("; table lines 0 %03X 100 %03X 202 %03X 203 %03X 223 %03X\n", gvram[(g_htab >> 1)], gvram[(g_htab >> 1) + 200], gvram[(g_htab >> 1) + 404], gvram[(g_htab >> 1) + 406], gvram[(g_htab >> 1) + 446]); } }
    }
    const long bad = gen_check(v, 0); b2_checked++; b2_bad += bad; b2_badpics += bad != 0;
    if (bad && getenv("RUN_BG_SHOW") && b2_badpics <= 5) printf("bg2: picture %ld wrong pixels %ld\n", b2_pics, bad);
    if ((b2_checked & 63) == 0) b2_whole_bad += gen_check(v, 1);
}
#endif
#ifdef RT_BG2
/* RUN_PIC_DUMP=<file> [RUN_PIC_EVERY=n] (Sapphire docs/16): every n-th picture as the console shows it - the Genesis
   model's plane (per-line H scroll, the top's VSRAM, the V splits, its tiles and CRAM) with the 32X's sprite layer in
   front - as PC Engine colours (u32 the frame, then 256 x 224 u16), for tools/m3_piccheck.py against Mednafen.
   Game data: the work folder only */
static void pic_dump(const V32 *v, int b)
{
    static FILE *f; static long n; const long every = getenv("RUN_PIC_EVERY") ? atol(getenv("RUN_PIC_EVERY")) : 50;
    if (n++ % every) return;
    if (!f) f = fopen(getenv("RUN_PIC_DUMP"), "wb");
    static uint16_t px[224][256];
    const int mw = bg2_mw(v->MWR), mh = bg2_mh(v->MWR), W = mw * 8, H = mh * 8;
    for (int l = 0; l < 224; l++) {
        unsigned vs = gvsram[0];
        for (int k = 1; k < bg2.vsp_n; k++) if ((int)(bg2.vsp_e[k] >> 20) <= l) vs = (bg2.vsp_e[k] >> 10) & 0x3FF;
        const unsigned hs = gvram[(g_htab >> 1) + 2 * l];
        for (int x = 0; x < 256; x++) {
            const int sp = spr_fb[b][0x200 + l * SPR_LINE + x + 32];
            if (sp) { px[l][x] = v->color[256 + sp] & 0x1FF; continue; }
            const unsigned X = (unsigned)(x + 32 - (int)hs) & (W - 1), Y = (l + vs) & (H - 1);
            const uint16_t e = gvram[(BG2_PLANE_A >> 1) + (Y >> 3) * mw + (X >> 3)];
            const unsigned t = e & 0x7FF, g = e >> 13 & 3, ty = (e & 0x1000) ? 7 - (Y & 7) : Y & 7, tx = (e & 0x800) ? 7 - (X & 7) : X & 7;
            const uint32_t row = (uint32_t)gvram[t * 16 + 2 * ty] << 16 | gvram[t * 16 + 2 * ty + 1];
            const unsigned nib = row >> (28 - 4 * tx) & 15, c = gcram[nib ? 16 * g + nib : 0];
            px[l][x] = (uint16_t)(((c >> 5) & 7) << 6 | ((c >> 1) & 7) << 3 | ((c >> 9) & 7));
        }
    }
    const uint32_t fr = (uint32_t)cur_frame_any; fwrite(&fr, 4, 1, f); fwrite(px, 2, 256 * 224, f); fflush(f);
}
#endif
static void spr_draw_pc(V32 *v)                    /* (rt_draw_hook: at the display's start) */
{
#ifdef RT_BG2
    if (getenv("RUN_BG2")) { bg2_run_pc(v); if (!getenv("RUN_SPR_DO")) return; }
#endif
#ifdef RT_BG
    if (getenv("RUN_BG")) { bg_run_pc(v); if (!getenv("RUN_SPR_DO")) return; }
#endif
    if (getenv("RUN_BGSTAT")) { bgstat(v); if (!getenv("RUN_SPR_DO")) return; }
    if (getenv("RUN_SPR_SKIPSTAT")) skipstat(v, (int)(spr_frame_no & 1));
    const int b = getenv("RUN_SPR_ONEBUF") ? 0 : (int)(spr_frame_no & 1);   /* (ONEBUF: the speed test's way) */
    spr_frame(v, b); spr_ref_draw(v, v->sat);
    long bad = 0;
    for (int y = 0; y < SPR_LINES; y++) for (int x = 0; x < SPR_LINE; x++) {
        const uint8_t got = spr_fb[b][0x200 + y * SPR_LINE + x], want = x >= 32 && x < 288 ? spr_ref[y][x - 32] : 0;
        if (got != want) { if (!bad && spr_first_bad < 0) { spr_first_bad = spr_frame_no; printf("spr: drawing %ld first wrong at (%d, %d): %02X, want %02X\n", spr_frame_no, x - 32, y, got, want); } bad++; }
    }
    spr_bad_px += bad; if (bad) spr_bad_frames++;
#ifdef RT_BG2
    if (getenv("RUN_PIC_DUMP") && getenv("RUN_BG2")) pic_dump(v, b);
#endif
    if (getenv("RUN_SPR_DUMP")) {                  /* (DIAG, docs/57: each drawing's sprite layer and sprite colours, for
                                                      tools/m3_sprcheck.py - game data: the work folder only) */
        static FILE *df; if (!df) df = fopen(getenv("RUN_SPR_DUMP"), "wb");
        if (df) { fwrite(spr_ref, 1, sizeof spr_ref, df); fwrite(v->color + 256, 2, 256, df); }
    }
    if (getenv("RUN_BGPIC_DUMP")) {                /* (DIAG, docs/65: each drawing's background from VRAM with the display
                                                      start's scroll - 224 x 256 of the PC Engine's colour, 0x8000 for
                                                      none - then the backdrop colour; game data: the work folder only) */
        static FILE *bf; if (!bf) bf = fopen(getenv("RUN_BGPIC_DUMP"), "wb");
        static uint16_t pic[224][256];
        for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) {
            const unsigned py = (v->BYR + y) & 511, px = (v->BXR + x) & 511;
            const uint16_t e = v->vram[(py >> 3) * 64 + (px >> 3)]; const uint16_t *w = v->vram + (e & 0x7FF) * 16;
            const unsigned r = py & 7, s2 = 7 - (px & 7), p01 = w[r], p23 = w[8 + r];
            const unsigned i = ((p01 >> s2) & 1) | ((p01 >> (s2 + 8)) & 1) << 1 | ((p23 >> s2) & 1) << 2 | ((p23 >> (s2 + 8)) & 1) << 3;
            pic[y][x] = i ? (uint16_t)(v->color[(e >> 12) * 16 + i] & 0x1FF) : 0x8000; }
        if (bf) { fwrite(pic, 2, 224 * 256, bf); fwrite(v->color, 2, 1, bf); }
    }
    spr_frame_no++;
}
static void spr_check(Rt *r)
{
    static int on;
    if (!on) {
        static uint8_t *cells; const char *p = getenv("RUN_SPR");
        FILE *f = *p && strcmp(p, "-") ? fopen(p, "rb") : 0;
        if (f) { fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f); cells = malloc(n); if (fread(cells, 1, n, f) != (size_t)n) cells = 0; fclose(f); }
        spr_init(cells);
        for (int b = 0; b < 2; b++) { spr_buf[b].ovl = spr_buf[b].nrm = spr_fb[b] + 0x200 + 32; spr_buf[b].tail = spr_fb[b] + ((0x20000 - SPR_SC * SPR_CELL) & ~0xFFF); }   /* (Sapphire docs/16: the cache at the end) */
        rt_draw_hook = spr_draw_pc;
        on = 1;
    }
    if (r->frame == last_frame) {
        const SprStat *s = &spr_stat;
        printf("spr: %u frames; sprites %.1f a frame, cells %.1f: from the cart %.1f, the cache %.1f, converted %.2f (most %u in a frame; of a copy under way %u); sprites not drawn again %.1f a frame; wrong pixels %ld in %ld frames\n",
               s->frames, (double)s->sprites / s->frames, (double)s->cells / s->frames, (double)s->cart / s->frames,
               (double)s->hits / s->frames, (double)s->conv / s->frames, s->conv_max, s->under_way, (double)s->skipped / s->frames, spr_bad_px, spr_bad_frames);
        if (getenv("RUN_SPR_SKIPSTAT")) printf("spr skip: %ld drawings; cells %.1f a drawing, drawn %.1f; clearing %.0f words a drawing, of them needed %.0f\n",
            sk_n, (double)sk_cells / sk_n, (double)sk_cells_drawn / sk_n, (double)sk_words / sk_n, (double)sk_words_cleared / sk_n);
        { uint32_t h = 2166136261u; for (int i = 0x200; i < 0x200 + SPR_LINES * SPR_LINE; i++) { h ^= spr_fb[0][i]; h *= 16777619u; }
          printf("spr: buffer 0's picture hash %08X\n", h); }   /* (dxspeed SPR=1 leaves the same in COMM8) */
#ifdef SPR_VERIFY
        { extern long spr_verify_bad[3], spr_verify_bad_copying; printf("spr verify: cells unlike VRAM's from the cart %ld, the cache %ld, conversions %ld; of them with a unit of the copy under way %ld\n", spr_verify_bad[0], spr_verify_bad[1], spr_verify_bad[2], spr_verify_bad_copying); }
#endif
    }
}
/* RUN_MARKS_AT=<frame> RUN_MARKS_OUT=<file> (DIAG, docs/56): the tags at that frame's end (2,048 big-endian u16; the
   code's as VSRC_CODE) - the speed test's start, which RUN_MARKS_IN and dxspeed take - and in <file>.ids the outputs
   tagged at any frame's end from then on (big-endian u16, sorted): the cells its cart needs */
static void marks_out(Rt *r)
{
    static uint8_t seen[0x8000]; const long at = atol(getenv("RUN_MARKS_AT"));
    if (r->frame < at) return;
    for (int i = 0; i < 2048; i++) if (!VSRC_IS_CODE(rt_marks.v[i])) seen[rt_marks.v[i]] = 1;
    char p[600];
    if (r->frame == at) {
        FILE *f = fopen(getenv("RUN_MARKS_OUT"), "wb");
        for (int i = 0; i < 2048; i++) { const uint16_t t = VSRC_IS_CODE(rt_marks.v[i]) ? VSRC_CODE : rt_marks.v[i]; fputc(t >> 8, f); fputc(t & 0xFF, f); }
        fclose(f);
    }
    if (r->frame == last_frame) {
        snprintf(p, sizeof p, "%s.ids", getenv("RUN_MARKS_OUT")); FILE *f = fopen(p, "wb"); int n = 0;
        for (int i = 0; i < 0x8000; i++) if (seen[i]) { fputc(i >> 8, f); fputc(i & 0xFF, f); n++; }
        fclose(f); printf("marks out: tags at frame %ld, %d outputs tagged since\n", at, n);
    }
}
#endif
#ifdef HUC_MARKS
/* RUN_SPLITS (Sapphire docs/16): each picture's raster splits as the VDC model kept them (the frame before the display
   start), their kinds counted: line, BXR, BYR - line (the V offset), CR's sprite bit, what the handler wrote */
static void splits_stat(const V32 *v, int show)
{
    static struct { char k[96]; long n; } kinds[64]; static int nk; static long pics;
    char k[96]; int o = 0; pics++;
    o += snprintf(k + o, sizeof k - o, "top %03X %03X CR %04X |", v->BXR, v->BYR, v->t.CR);
    for (int i = 0; i < v->spl_n && o < 80; i++)
        o += snprintf(k + o, sizeof k - o, " L%d x%03X y%03X(o%03X) CR%04X w%d", v->spl[i].line, v->spl[i].bxr, v->spl[i].byr,
                      (v->spl[i].byr - v->spl[i].line + 1) & 0x1FF, v->spl[i].cr, v->spl[i].wrote);
    if (show) printf("splits: %s\n", k);
    const char *kk = strchr(k, '|');            /* (counted without the top's values, which move each frame) */
    int j; for (j = 0; j < nk; j++) if (!strcmp(kinds[j].k, kk)) break;
    if (j == nk && nk < 64) { strncpy(kinds[nk].k, kk, 95); kinds[nk].n = 0; nk++; }
    if (j < nk) kinds[j].n++;
    if (show == 2) for (int q = 0; q < nk; q++) printf("split kind %ld x:%s\n", kinds[q].n, kinds[q].k);
}
#endif
static void on_frame(Rt *r)
{
    cur_frame_any = r->frame;
#ifdef RT_SDUMP
    if (getenv("RUN_DUMP_FRAME") && r->frame == atol(getenv("RUN_DUMP_FRAME"))) {   /* (DIAG, docs/74) */
        static uint8_t b[80000]; const long n = rt_sdump(r, b); FILE *f = fopen("frame_dump_rt.bin", "wb"); fwrite(b, 1, (size_t)n, f); fclose(f); }
#endif
    if (getenv("RUN_FRAME_HASH")) printf("fhash %ld %08x\n", r->frame, rt_hash(r, 0));   /* (DIAG, docs/74) */
#ifdef RT_LINES
    on_frames++;
#endif
#ifdef HUC_MARKS
    if (getenv("RUN_DRAWSTAT")) drawstat(r);
    if (getenv("RUN_SPLITS")) splits_stat(&r->vdc, r->frame < atol(getenv("RUN_SPLITS")) ? 1 : r->frame == last_frame ? 2 : 0);
    if (getenv("RUN_SPR")) spr_check(r);
    if (getenv("RUN_MARKS_AT")) marks_out(r);
#endif
#ifdef HUC_MARKS
    if (getenv("RUN_PACK")) pack_check(r);
#endif
#ifdef RT_PROV
    if (getenv("RUN_MARKS")) {
        static uint32_t *set; static long nset; static uint32_t last[2048]; static long fr, bad, conv, maxconv, conv_frames, ripped_units;
        static unsigned dv = 0;
        if (!set) { FILE *f = fopen(getenv("RUN_MARKS"), "rb"); fseek(f, 0, SEEK_END); nset = ftell(f) / 4; rewind(f);
                    set = malloc(nset * 4); if (fread(set, 4, nset, f) != (size_t)nset) nset = 0; fclose(f); }
        dv = r->vdc.DVSSR;
        long c = 0;
        for (unsigned u = 0x110; u < 0x800; u++) {
            if ((u << 4) >= dv && (u << 4) < dv + 256) continue;      /* the sprite table */
            uint32_t h = 2166136261u; for (unsigned i = u << 4; i < (u << 4) + 16; i++) { h ^= r->vdc.vram[i]; h *= 16777619u; }
            if (mk_unit[u] == 1) {
                long lo = 0, hi = nset;                                /* in the rip? */
                while (lo < hi) { long m = (lo + hi) / 2; if (set[m] < h) lo = m + 1; else hi = m; }
                if (h != 0x69691905u && !(lo < nset && set[lo] == h) && h == last[u]) {   /* (and the same a frame later: not a copy cut by the frame's end) */
                    if (bad++ < 5) printf("marks: unit %04X frame %ld ripped but not in the rip\n", u << 4, r->frame); }
                ripped_units++;
            } else if (mk_unit[u] == 2 && h != last[u]) c++;
            last[u] = h;
        }
        fr++; conv += c; if (c > maxconv) maxconv = c; if (c) conv_frames++;
        if (r->frame == last_frame) printf("marks: %ld frames; ripped units not in the rip %ld; code-made units changed %ld (%.2f a frame, most %ld, in %ld frames)\n",
                                            fr, bad, conv, (double)conv / fr, maxconv, conv_frames);
    }
#endif
#ifdef RT_PROV
    wr_frame = r->frame + 1;
#endif
    if (getenv("RUN_REGION")) {                       /* DIAG (docs/47): "lo,hi" (VRAM words, hex): the region's hash a frame */
        unsigned lo = 0, hi = 0; sscanf(getenv("RUN_REGION"), "%x,%x", &lo, &hi);
        uint32_t h = 2166136261u; for (unsigned i = lo; i < hi && i < 0x8000; i++) { h ^= r->vdc.vram[i]; h *= 16777619u; }
        printf("region %ld %08x\n", r->frame, h); return;
    }
    if (getenv("RUN_VDIFF")) { vdiff(r); return; }
    if (getenv("RUN_UNITS")) {                         /* DIAG (docs/51): "lo,hi": each 16-word unit's hash, every frame */
        static FILE *uf; static unsigned lo, hi;
        if (!uf) { sscanf(getenv("RUN_UNITS"), "%x,%x", &lo, &hi); uf = fopen(getenv("RUN_UNITS_OUT") ? getenv("RUN_UNITS_OUT") : "units.bin", "wb"); }
        for (unsigned u = lo; u < hi && u < 0x8000; u += 16) {
            uint32_t h = 2166136261u; for (unsigned i = u; i < u + 16; i++) { h ^= r->vdc.vram[i]; h *= 16777619u; }
            fwrite(&h, 4, 1, uf);
        }
    }
#ifdef RT_PLAY
    if (getenv("RUN_POKE") && r->frame == atol(getenv("RUN_POKE"))) {   /* DIAG: RUN_POKE=frame RUN_POKES=addr:val,... (base RAM) */
        const char *p = getenv("RUN_POKES"); unsigned a, v; int k;
        while (p && sscanf(p, "%x:%x%n", &a, &v, &k) == 2) { r->base_ram[a & 0x1FFF] = (uint8_t)v; p += k; if (*p == ',') p++; else break; } }
    if (getenv("RUN_PAD_FROM") && r->frame >= atol(getenv("RUN_PAD_FROM")))   /* DIAG: a pad of our own from that frame */
        rt_pad_live = (int32_t)strtol(getenv("RUN_PAD_MASK") ? getenv("RUN_PAD_MASK") : "0", 0, 16);
#endif
    if (getenv("RUN_FRAME_MARKS")) fprintf(stderr, "FRAME %ld\n", r->frame);   /* DIAG (docs/49): frames in the load log */
    if (getenv("RUN_VDUMP_EVERY")) {                  /* DIAG (docs/49): every n frames, what m0_replay.py hashes */
        static FILE *vf; const long n = atol(getenv("RUN_VDUMP_EVERY"));
        if (!vf) vf = fopen(getenv("RUN_VDUMP") ? getenv("RUN_VDUMP") : "vdump.bin", "wb");
        if (vf && n > 0 && r->frame % n == 0) { const uint32_t fr = (uint32_t)r->frame; fwrite(&fr, 4, 1, vf);
            static const uint16_t z[32768]; fwrite(r->base_ram, 1, 8192, vf);   /* m0_replay.py's frame_hash order */
            for (int b = 0x80; b < 0x88; b++) fwrite(r->bank[b], 1, 8192, vf);
            for (int b = 0x68; b < 0x80; b++) fwrite(r->bank[b], 1, 8192, vf);
            fwrite(r->vdc.vram, 2, 32768, vf); fwrite(z, 2, 32768, vf); fwrite(r->vdc.sat, 2, 256, vf);
            fwrite(r->vdc.color, 2, 512, vf); fflush(vf); }
    }
    if (!quiet || r->frame == last_frame) printf("frame %ld hash %08x %08x\n", r->frame, rt_hash(r, 1), rt_hash(r, 0));
}
static void on_idle(Rt *r)
{
    if (quiet) { idle_n++; return; }
    printf("idle %ld frame %ld %08x at %02X:%04X\n", idle_n, r->frame, rt_hash(r, 0), r->c.MPR[r->c.pc >> 13], r->c.pc);
    if (getenv("RUN_DUMP_IDLE") && atol(getenv("RUN_DUMP_IDLE")) == idle_n) {   /* the state there, for a diff */
        FILE *f = fopen("idle_dump_rt.bin", "wb");
        if (f) { fwrite(r->base_ram, 1, 8192, f); for (int b = 0x80; b < 0x88; b++) fwrite(r->bank[b], 1, 8192, f);
                 for (int b = 0x68; b < 0x80; b++) fwrite(r->bank[b], 1, 8192, f);
                 fwrite(r->vdc.vram, 2, 32768, f); fwrite(r->vdc.sat, 2, 256, f); fwrite(r->vdc.color, 2, 512, f); fclose(f); }
    }
    idle_n++;
}

static long on_frames_any(void) { return cur_frame_any; }
int rt_disc_sector(int32_t sector, uint8_t *out)      /* (docs/69: RUN_TRACK, the data track's image) */
{
    static FILE *f; if (!f) f = fopen(getenv("RUN_TRACK"), "rb");
    if (getenv("RUN_TRACK_LOG")) { static int32_t last = -9; static long n, runs; n++; if (sector != last + 1) runs++; last = sector;
        if (!(n & 63) || sector != last) printf("track: %ld sectors read, %ld runs, at %d (frame %ld)\n", n, runs, sector, on_frames_any()); }
    return f && fseek(f, (long)sector * 2048, SEEK_SET) == 0 && fread(out, 1, 2048, f) == 2048;
}
#ifdef RT_POOL
static int pool_wb_pc(const uint8_t *mem, uint8_t *home) { memcpy(home, mem, 8192); return 1; }
#ifdef RT_POOL_LOG
static void pool_log_pc(uint32_t fr, int b, int out, uint32_t h) { printf("pool %u %02X %02X %08x\n", fr, b, out & 0xFF, out >= 0 ? h : 0); }
#endif
static void pool_report(void) { printf("pool: %ld banks taken, %ld written back\n", rt_pool_takes, rt_pool_wbs); }
#endif
#ifdef RT_WCLASS
/* RUN_WCLASS (DIAG, docs/71): the writes to the Super / CD RAM banks, as a CD read's (into the destination of a
   System Card CD_READ since) or the game's own; per bank the distinct bytes of each */
extern void (*rt_wclass_wr)(int b, unsigned off, uint8_t v); extern void (*rt_wclass_cd)(int b, unsigned off);
static uint8_t wc_cdm[32][8192], wc_gm[32][8192]; static long wc_cdn, wc_gn;
static long wc_w[32], wc_nf[32], wc_fw[32], wc_mx[32], wc_lf[32] = { [0 ... 31] = -1 }, wc_ff[32];
static int wc_i(int b) { return b >= 0x80 ? b - 0x80 : b - 0x68 + 8; }
static void wc_cd(int b, unsigned off) { if (b >= 0x68 && b < 0x88) wc_cdm[wc_i(b)][off] = 1; }
static void wc_wr(int b, unsigned off, uint8_t v)
{
    (void)v; const int i = wc_i(b);
    if (wc_cdm[i][off] == 1) { wc_cdm[i][off] = 2; wc_cdn++; }   /* (its CD read's byte) */
    else { wc_gm[i][off] = 1; wc_gn++;
        const long f = cur_frame_any; wc_w[i]++;
        if (f != wc_lf[i]) { if (wc_fw[i] > wc_mx[i]) wc_mx[i] = wc_fw[i]; wc_fw[i] = 0; wc_lf[i] = f; wc_nf[i]++; if (!wc_ff[i]) wc_ff[i] = f + 1; }
        wc_fw[i]++; }
}
static void wc_report(void)
{
    long g = 0, d = 0;
    printf("wclass: game writes %ld, CD-read writes %ld; distinct bytes per bank (game / CD):", wc_gn, wc_cdn);
    for (int j = 0; j < 32; j++) { long a = 0, c = 0; for (int k = 0; k < 8192; k++) { a += wc_gm[j][k]; c += wc_cdm[j][k] == 2; }
        g += a; d += c; if (a || c) printf(" %02X:%ld/%ld", j < 8 ? 0x80 + j : 0x68 + j - 8, a, c); }
    printf("; total game %ld, CD %ld\n", g, d);
    for (int j = 0; j < 32; j++) if (wc_w[j]) { if (wc_fw[j] > wc_mx[j]) wc_mx[j] = wc_fw[j];
        printf("wclass rate %02X: %ld writes in %ld frames (most %ld in a frame), frames %ld-%ld\n", j < 8 ? 0x80 + j : 0x68 + j - 8,
               wc_w[j], wc_nf[j], wc_mx[j], wc_ff[j] - 1, wc_lf[j]); }
}
#endif
#ifdef RT_IOSTAT
long rt_iostat[4][8][2][4], rt_iostat_reg[8][16];
static void iostat_show(void) { for (int k = 0; k < 4; k++) for (int p = 0; p < 8; p++) for (int q = 0; q < 2; q++) for (int l = 0; l < 4; l++) if (rt_iostat[k][p][q][l]) printf("iostat reg %d phase %d quiet %d lazyhit %d anew %d: %ld\n", k, p, q, l & 1, l >> 1, rt_iostat[k][p][q][l]);
    for (int g = 0; g < 8; g++) for (int o = 0; o < 16; o++) if (rt_iostat_reg[g][o]) printf("iostat read %04X: %ld\n", g << 10 | o, rt_iostat_reg[g][o]); }
#endif
#ifdef HUC_CDSTEP
extern long rt_cdsteps, rt_cdstep_bytes, rt_adsteps, rt_adstep_iters, rt_pskips, rt_pskip_iters;
static void cdstep_show(void) { printf("cdstep: %ld steps, %ld bytes; adstep: %ld steps, %ld iterations; pollskip: %ld steps, %ld iterations\n", rt_cdsteps, rt_cdstep_bytes, rt_adsteps, rt_adstep_iters, rt_pskips, rt_pskip_iters); }
#endif
int main(int argc, char **argv)
{
#ifdef HUC_CDSTEP
    atexit(cdstep_show);
#endif
#ifdef RT_IOSTAT
    atexit(iostat_show);
#endif
    if (argc < 2) { fprintf(stderr, "usage: rt_pc <state.bin> [frames]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *blob = malloc((size_t)n); if (fread(blob, 1, (size_t)n, f) != (size_t)n) return 2; fclose(f);
    static RtState st; if (!rt_state_load(blob, &st, alloc)) { fprintf(stderr, "not a DXS1 state\n"); return 2; }
    static uint8_t *banks[256];
#ifdef RT_WCLASS
    if (!getenv("RUN_WCLASS"))                                  /* (RUN_WCLASS, DIAG docs/71: the RAM banks kept
                                                                   read-only, each write classed in wr_slow) */
#endif
#ifdef RT_POOL
    if (!getenv("RUN_POOL"))                                    /* (RUN_POOL=n, docs/72: the RAM banks as the SH2 has
                                                                   them - read only - and a pool of n slots) */
#endif
    for (int b = 0x68; b < 0x88; b++) banks[b] = malloc(8192);
    if (getenv("RUN_RW")) {                                     /* (RUN_RW=68,69,85: only those RAM banks writable, as
                                                                   the SH2 build's SDRAM_BANKS; the rest read only, in the
                                                                   state - writes there counted, docs/09) */
        for (int b = 0x68; b < 0x88; b++) banks[b] = 0;
        for (const char *q = getenv("RUN_RW"); *q; ) { char *e; long b = strtol(q, &e, 16); if (e == q) break;
            if (b >= 0x68 && b < 0x88) banks[b] = malloc(8192); q = *e ? e + 1 : e; }
    }
#ifdef RT_POOL
    if (getenv("RUN_POOL")) { static uint8_t *sl[RT_POOL_MAX]; const int n = atoi(getenv("RUN_POOL"));
        for (int k = 0; k < n && k < RT_POOL_MAX; k++) sl[k] = malloc(8192);
        rt_pool_init(sl, n); rt_pool_wb = pool_wb_pc; atexit(pool_report);
#ifdef RT_POOL_LOG
        rt_pool_log = pool_log_pc;
#endif
    }
#endif
#ifdef RT_WCLASS
    if (getenv("RUN_WCLASS")) { rt_wclass_wr = wc_wr; rt_wclass_cd = wc_cd; atexit(wc_report); }
#endif
    banks[0xF8] = malloc(8192); banks[0xF7] = malloc(2048);
#ifdef RT_AC_FPGA
    { extern uint8_t *rt_ace_cart; extern FILE *rt_ace_log; rt_ace_cart = blob;
      if (getenv("RUN_ACE_LOG")) rt_ace_log = fopen(getenv("RUN_ACE_LOG"), "wb"); }   /* (docs/10: the engine's cart is the state's bytes) */
#endif
    if (getenv("RUN_ROHIST")) rt_ro_hist = calloc(256 << 13, 4);
    if (getenv("RUN_INTERP")) rt_interp_hist = calloc(256u << 21, 4);   /* (each lost write's place, docs/11) */
    static Rt rt; rt_init(&rt, &st, banks);
    rt.on_idle = on_idle; quiet = getenv("RUN_QUIET") != 0;
    if (argc > 3 && getenv("RUN_DISC")) {                       /* the CD drive, as the PC host has it */
        char p[512], q[512]; snprintf(p, sizeof p, "%s/scsi_start.bin", argv[3]); snprintf(q, sizeof q, "%s/toc.bin", argv[3]);
        FILE *g = fopen(p, "rb"), *h = fopen(q, "rb");
        static uint32_t so[256 + 512], tc[204]; static uint8_t fifo[2048]; char err[256];
        if (g && h && fread(so, 1, 1024, g) == 1024 && fread(fifo, 1, 2048, g) == so[17] && fread(tc, 4, 204, h) == 204 &&
            scsi_start(&rt.hw, so, fifo, tc, getenv("RUN_DISC"), st.io_t0 + (int64_t)(int32_t)(so[34] - st.io_regs[18]), err, sizeof err))
            printf("the CD drive modelled\n");
        else printf("the CD drive not modelled\n");
    }
    if (getenv("RUN_TRACK") && st.scsi_o) {                  /* (docs/69: the drive from the state's own table, sectors
                                                                   from the data track's image, as the SH2 runs it) */
        char err[256];
        if (scsi_start_tab(&rt.hw, st.scsi_o, st.scsi_fifo, st.toc, st.disc_tab, st.io_t0 + (int64_t)(int32_t)(st.scsi_o[34] - st.io_regs[18]), err, sizeof err))
            printf("the CD drive modelled from the state's table (phase %d, signals %03X, CD-DA %d, read timer %d)\n", rt.hw.scsi.phase, (unsigned)rt.hw.scsi.signals, rt.hw.cdda_status, (int)rt.hw.read_timer);
        else printf("the CD drive not modelled: %s\n", err);
    }
    const long frames = argc > 2 ? atol(argv[2]) : st.nmasks;
    last_frame = frames - 1;
    printf("frame -1 hash %08x %08x\n", rt_hash(&rt, 1), rt_hash(&rt, 0));
#ifdef HUC_MARKS
    if (getenv("RUN_KEYS")) keys_load(getenv("RUN_KEYS"));
    if (getenv("RUN_PACK")) pack_load(getenv("RUN_PACK"));
    if (getenv("RUN_SEED")) {                         /* (m3_rip.py seed: the CD reads' table for a window's start) */
        extern uint16_t rt_cdpos[512]; FILE *f = fopen(getenv("RUN_SEED"), "rb"); uint8_t b[1024];
        if (!f || fread(b, 1, 1024, f) != 1024) { fprintf(stderr, "bad seed\n"); exit(2); } fclose(f);
        for (int i = 0; i < 512; i++) rt_cdpos[i] = (uint16_t)(b[2 * i] << 8 | b[2 * i + 1]);
    }
    if (getenv("RUN_MARKS_IN")) {                     /* (RUN_MARKS_OUT's tags: a window's start, docs/56) */
        FILE *f = fopen(getenv("RUN_MARKS_IN"), "rb"); uint8_t b[4096];
        if (!f || fread(b, 1, 4096, f) != 4096) { fprintf(stderr, "bad marks\n"); exit(2); } fclose(f);
        for (int i = 0; i < 2048; i++) rt_marks.v[i] = (uint16_t)(b[2 * i] << 8 | b[2 * i + 1]);
    }
#endif
#ifdef HUC_MARKS
    if (getenv("RUN_SPR")) spr_check(&rt);         /* (its set-up: the drawing runs from rt_draw_hook) */
    if (getenv("RUN_BGSTAT")) rt_draw_hook = spr_draw_pc;
#ifdef RT_BG2
    if (getenv("RUN_BG2")) rt_draw_hook = spr_draw_pc;
#endif
#ifdef RT_BG
    if (getenv("RUN_BG")) rt_draw_hook = spr_draw_pc;
#ifdef RT_LINES
    if (getenv("RUN_BG_LINES")) { ln_f = fopen(getenv("RUN_BG_LINES"), "rb"); if (ln_f) { fseek(ln_f, 0, SEEK_END); ln_frames = ftell(ln_f) / 960; }
        ln_off = getenv("RUN_BG_LOFF") ? atol(getenv("RUN_BG_LOFF")) : 0; v32_scroll = scroll_hook; }
#endif
#endif
#endif
    rt_run(&rt, frames, on_frame);
    #ifdef RT_IRQ_CHECK
    { extern void rt_irq_check_report(void); rt_irq_check_report(); }
    #endif
    #ifdef RT_INLINE_CHECK
    { extern void rt_inline_check_report(void); rt_inline_check_report(); }
    #endif
    #ifdef RT_SCHED_CHECK
    { extern void rt_sched_check_report(void); rt_sched_check_report(); }
#endif
    if (getenv("RUN_VDIFF") && vd_n) printf("vdiff: %ld frames; a frame: map words %.1f (most %ld; columns %.1f), tiles %.1f (most %ld; frames with any %ld), colours %.1f, sprite entries %.1f (MWR %04X); tiles the map names %.1f\n",
        vd_n, (double)vd_bat / vd_n, vd_max_bat, (double)vd_bat_cols / vd_n, (double)vd_tiles / vd_n, vd_max_tiles, vd_frames_tiles, (double)vd_col / vd_n, (double)vd_sat / vd_n, rt.vdc.MWR, (double)vd_bg / vd_n);
    if (getenv("RUN_VDIFF") && vd_n) { printf("vdiff: the sprite table's units in VRAM %.1f a frame; sprite units found in the RAM banks %ld, not %ld; banks:", (double)vd_satb / vd_n, vd_found, vd_notfound);
        for (int b = 0; b < 256; b++) if (vd_found_bank[b]) printf(" %02X:%ld", b, vd_found_bank[b]); printf("\n");
        printf("vdiff: where - CD banks %ld, base RAM %ld, other banks %ld, CD banks odd %ld, VRAM before %ld, first half only %ld, all zero %ld, none %ld\n",
               vd_where[1], vd_where[2], vd_where[3], vd_where[4], vd_where[5], vd_where[6], vd_where[7], vd_where[0]); }
#ifdef RT_PROV
    if (getenv("RUN_VDIFF")) { printf("prov: found+straight %ld, found+not straight %ld, not found+straight %ld, not found+not straight %ld; the last read's bank:",
        pv_kind[0], pv_kind[1], pv_kind[2], pv_kind[3]); for (int b = 0; b < 256; b++) if (pv_bank[b]) printf(" %02X:%ld", b, pv_bank[b]); printf("\n"); }
#endif
#ifdef HUC_MARKS
    { uint32_t h = 2166136261u; for (int i = 0; i < 2048; i++) { h ^= rt_marks.v[i]; h *= 16777619u; }
      printf("marks: hash %04X, entries %ld, found %ld\n", (unsigned)((h >> 16) ^ (h & 0xFFFF)), rt_entries, rt_entries_found); }
#endif
#ifdef RT_PROV
    if (getenv("RUN_WRITERS")) writers_print();
    if (getenv("RUN_PROV_DUMP")) { FILE *g = fopen(getenv("RUN_PROV_DUMP"), "wb"); if (g) { fwrite(prov_src, 4, 32768, g); fwrite(rt.vdc.vram, 2, 32768, g); fclose(g); } }
#endif
#ifdef HUC_MARKS                                       /* (the picture statistics exist only in marks builds) */
    if (getenv("RUN_BGSTAT") && bg_n) printf("bg art: pictures with over 400 tiles %ld; not fitting by value: of them %ld, of the rest %ld; the rest with 4 palettes or fewer %ld of %ld\n", bg_art, bg_misfit_art, bg_misfit_play, bg_play_le4, bg_play);
    if (getenv("RUN_BGSTAT") && bg_n) printf("bg: %ld pictures (%ld not a 64 x 64 map); fit 4 x 15 by index %.1f%%, by value %.1f%%; colour slots %.1f (most %ld), palettes most %ld; tile kinds %.0f (most %ld), tiles most %ld; changed - map in %ld pictures (%.0f words each), the map's tiles in %ld (%.1f each), colours in %ld (%.1f each)\n",
        bg_n, bg_mwr_bad, 100.0 * bg_fit_idx / bg_n, 100.0 * bg_fit_val / bg_n, (double)bg_slots_sum / bg_n, bg_slots_max, bg_pals_max, (double)bg_kinds_sum / bg_n, bg_kinds_max, bg_tiles_max,
        bg_map_frames, bg_map_frames ? (double)bg_map_words / bg_map_frames : 0.0, bg_tile_frames, bg_tile_frames ? (double)bg_tile_units / bg_tile_frames : 0.0, bg_col_frames, bg_col_frames ? (double)bg_col_n / bg_col_frames : 0.0);
#endif
#ifdef RT_BG
    if (getenv("RUN_BG")) { const BgStat *q = &bg_state.st; const double n = q->frames ? (double)q->frames : 1;
        printf("bg: %ld pictures checked, wrong pixels %ld (in %ld pictures), the whole map every 64th: %ld; not fitting: art %ld, play %ld; not 64 x 64: %ld\n",
               bgc_n, bgc_bad, bgc_badpics, bgc_whole_bad, bgc_misfit_art, bgc_misfit_play, bgc_skip);
        printf("bg words: a picture tiles %.2f (most %ld), map %.1f (most %ld), colours %.2f (most %ld); most words in one %ld; slots most %ld; splits %ld, moves %ld, repacks %ld, evicted %ld\n",
               q->tiles_conv / n, q->conv_max, q->map_words / n, q->map_max, q->col_words / n, q->col_max, q->words_max, q->slots_max, q->splits, q->moves, q->repacks, q->evicted);
#ifdef RT_LINES
        printf("bg scroll: %ld pictures; the H table changed in %ld, %.1f lines a picture (most %ld); vertical offsets most %ld, over 2 in %ld pictures, a split in %ld\n", hs_pics, hs_changed_pics, hs_pics ? (double)hs_words / hs_pics : 0.0, hs_words_max, vo_max, vo_over2, vo_pics_split);
        if (ln_f) printf("bg lines: %ld pictures, %ld with lines that differ (%ld lines: %ld a line early or late, %ld other); events lost %ld\n", ln_pics, ln_bad_pics, ln_bad_lines, ln_shift, ln_other, ln_over);
#endif
        if (getenv("RUN_BG_IDENT")) { printf("bg ident: %ld pictures, a tile with two palettes of different maps in %ld (most %ld tiles); tiles the map ever names:", id_pics, id_conf_pics, id_conf_max);
            int run0 = -1; for (int t = 0; t <= 2048; t++) { const int on = t < 2048 && (id_used[t >> 3] >> (t & 7) & 1); if (on && run0 < 0) run0 = t; if (!on && run0 >= 0) { printf(" %03X-%03X", run0, t - 1); run0 = -1; } } printf("\n"); }
        printf("bg big frames (1,024 words or more): %ld; the background off %ld, all its colours black %ld\n", bgc_big, bgc_big_off, bgc_big_dark);
        printf("bg frames by words: 0 %ld, 1-63 %ld, 64-255 %ld, 256-1023 %ld, 1024-4095 %ld, 4096+ %ld\n", q->hist[0], q->hist[1], q->hist[2], q->hist[3], q->hist[4], q->hist[5]); }
#endif
#ifdef RT_BG2
    if (getenv("RUN_BG2")) { const Bg2Stat *q = &bg2.st;
        printf("bg2: %ld pictures, %ld checked (%ld still sending, %ld not fitting), wrong pixels %ld (in %ld), the whole map every 64th %ld; not 64 x 64 %ld; scroll wrong %ld; journal lost %ld\n",
               b2_pics, b2_checked, q->behind, q->misfit, b2_bad, b2_badpics, b2_whole_bad, b2_skip, b2_scroll_bad, b2_jrn_over);
        printf("bg2 words: list %.1f a picture (most %ld), tiles %ld, map %ld, colours %ld; journal most %ld; evicted %ld, splits %ld, repacks %ld; entries waiting: under way %ld, not looked at %ld, no slot %ld (the other palettes' table full %ld, every slot in use %ld); slots in use at the end %d\n",
               b2_pics ? (double)q->words / b2_pics : 0.0, q->list_max, q->tiles, q->map, q->cols, q->journal_max, q->evicted, q->splits, q->repacks, q->underway, q->unseen, q->noslot, q->ovf_full, q->all_used, slots_in_use()); }
#endif
#ifdef RT_BG2
    if (getenv("RUN_BG2")) { const Bg2Stat *q = &bg2.st; const double n = b2_pics ? (double)b2_pics : 1;
        printf("bg2 work a picture: groups changed %.1f, tiles hashed %.1f (under way %.1f), masked %.2f; plane scans: tiles %.3f, colours %.3f\n",
               q->grp_changed / n, q->hashed / n, q->underway_t / n, q->masked / n, q->scan_t / n, q->scan_c / n); }
#endif
    printf("schedules from a line start: %ld made, %ld found again\n", rt.vdc.sc_made, rt.vdc.sc_hits);
#ifdef RT_SCHED_CHECK
    { extern long sc_checks, sc_bad; printf("schedule cache checks %ld, wrong %ld\n", sc_checks, sc_bad); }
#endif
    printf("run: %ld frames; %ld block entries, %ld interpreted; interrupts %ld + IRQ2 %ld; idle exits %ld; rematches %ld; "
           "VDC timing steps %ld; writes to read-only banks %ld (last bank %02X); not modelled %ld%s%s; cold entries %ld; chained %ld\n",
           rt.frame, rt.block_entries, rt.interpreted, rt.irqs, rt.irq2s, rt.idles, rt.rematches, rt.vdc.t.steps,
           rt.ro_writes, rt.ro_write_bank, rt.unmodelled + rt.hw.unmodelled, rt.hw.why ? ": " : "", rt.hw.why ? rt.hw.why : "",
           rt.cold_entries, rt.chained);
    printf("Arcade Card: %ld reads, %ld writes (%ld not made: its RAM read only), %ld reads of a region not here\n",
           rt.ac.reads, rt.ac.writes, rt.ac.ro_writes, rt.ac.misses);
    {                                                           /* (docs/10: the card's registers at the end) */
#ifdef RT_AC_FPGA
        extern AcEngine rt_ace; const ArcadeCard *acp = &rt_ace.ac;
        { extern long rt_ace_pokes; printf("Arcade Card engine (RT_AC_FPGA): %ld reads of a region not here; %ld writes to the cart (pokes), %ld made\n", rt_ace.misses, rt_ace_pokes, rt_ace.pokes); }
        { extern FILE *rt_ace_log; if (rt_ace_log) { fclose(rt_ace_log);        /* (and the cart after the pokes) */
            char q[600]; snprintf(q, sizeof q, "%s.cart", getenv("RUN_ACE_LOG")); FILE *g = fopen(q, "wb");
            extern uint8_t *rt_ace_cart; fwrite(rt_ace_cart, 1, (size_t)n, g); fclose(g); } }
#else
        const ArcadeCard *acp = &rt.ac;
#endif
        uint32_t w[19], h = 2166136261u; ac_state(acp, w);
        for (int i = 0; i < 19; i++) h = (h ^ w[i]) * 16777619u;
        printf("Arcade Card registers at the end: %08x\n", h);
    }
    if (rt_interp_hist) {                                       /* (the places interpreted most, with their opcode) */
        for (int k = 0; k < 25; k++) {
            uint32_t best = 0, bi = 0; for (uint32_t i = 0; i < (256u << 21); i++) if (rt_interp_hist[i] > best) { best = rt_interp_hist[i]; bi = i; }
            if (!best) break;
            printf("interpreted %6u x %02X:%04X op %02X (when interpreted)\n", best, (bi >> 13) & 0xFF, bi & 0x1FFF, bi >> 21);
            rt_interp_hist[bi] = 0;
        }
    }
    if (rt_ro_hist) { FILE *h = fopen(getenv("RUN_ROHIST"), "wb"); fwrite(rt_ro_hist, 4, 256 << 13, h); fclose(h); }
    printf("banks written:");
    for (int b = 0; b < 256; b++) if (rt.bank_dirty[b] || (b == 0xF8)) printf(" %02X", b);
    printf("\n");
    return 0;
}
