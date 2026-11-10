/* huchost.c - Sapphire docs/40: the cart's CPU as a library, for ares's EverDrive board (the FPGA's stand-in).
 *
 * The real cart runs the game on the FPGA's HuC6280 (docs/20-23) and hands the SH2 a FIFO of what it wrote to the
 * video chips (docs/24). ares has no model of that FPGA; this library is its stand-in: the 32X runtime's own C (the
 * interpreter, the VDC's timing, the CD unit and drive, the Arcade Card engine - tools/m3rt, tools/m2host) runs the
 * game from a DXS1 state, a PC Engine frame at a call, and gives the FIFO's entries for the pictures it made
 * (fifo_synth.c). The data track's sectors come from its image (the disc's DXTRACK.TRK on the real thing, read by the
 * SH2 for the FPGA). Built as a shared object (tools/m3_huchost.sh); ares loads it with ARES_HUCHOST=<.so>.
 *
 *   int huchost_init(const char *state, const char *track)     1 = ok
 *   int huchost_frame(uint16_t pad, const uint32_t **entries)  one frame with that pad (libretro mask); the entries
 *                                                               made in it (0 or one picture's)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"
#include "ac_engine.h"
#include "fifo_synth.h"
#include "pce_sound.h"

uint32_t *rt_ro_hist, *rt_interp_hist;                  /* (rt_pc's DIAG histograms: none) */
#ifdef HUCHOST_INTERP
const BlockDef blocks[] = { { 0, NULL, NULL, NULL, NULL } };   /* (no translated code: the interpreter runs it all - NOT
                                                                   equal to the blocks from power-on: docs/40) */
#endif

static Rt rt; static RtState st;
/* (docs/48) a recorded game: HUC_MASKS=<u16 a frame, from the state's frame> instead of the board's pad, and
   HUC_POKES=<u32 frame, u32 address, u32 value> the recording's RAM writes, each before its frame (tools/m0_replay.py:
   below 0x2000 base RAM, from 0x2000 CD RAM) */
static uint8_t **huc_banks; static uint16_t *rec_masks; static long rec_n, rec_k; static uint32_t (*rec_pokes)[3]; static long rec_np;
static void *slurp(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); *n = ftell(f);
    fseek(f, 0, SEEK_SET); void *m = malloc(*n ? *n : 1); if (fread(m, 1, *n, f) != (size_t)*n) { fclose(f); return 0; } fclose(f); return m; } static uint8_t *cart16; static FILE *track;
static int16_t snd[2048]; static int nsnd;
#define SND_RATE 22044                                      /* (the 32X's PWM: 23.01 MHz / 1,044) */
static FifoShadow shadow; static uint32_t *ents; static int nents, maxents = 1 << 18;

int rt_disc_sector(int32_t sector, uint8_t *out)
{
    if (!track || sector < 0 || fseek(track, (long)sector * 2048, SEEK_SET) || fread(out, 1, 2048, track) != 2048) return 0;
    return 1;
}

static uint16_t vsnap[32768];                              /* (DIAG: VRAM at the last picture's display start) */
static void draw(V32 *v)
{
    memcpy(vsnap, v->vram, sizeof vsnap);
    const int n = fifo_synth(&shadow, v, ents + nents, maxents - nents);
    nents += n < maxents - nents ? n : maxents - nents;
}

static void *alloc(unsigned long n) { return malloc(n); }

int huchost_init(const char *state, const char *trk)
{
    FILE *f = fopen(state, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
    cart16 = calloc(16u << 20, 1);
    uint8_t *blob = cart16 + 0x80000;                         /* (the state where the SH2 has it, as rt_pc: docs/38) */
    if (n > (16 << 20) - 0x80000 || fread(blob, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 0; }
    fclose(f);
    if (!rt_state_load(blob, &st, alloc)) return 0;
    if (getenv("HUC_CARD") && st.ac_phys[0]) {               /* (a state whose card is in its own file, docs/39) */
        FILE *g = fopen(getenv("HUC_CARD"), "rb"); if (!g) return 0;
        if (fread(cart16 + st.ac_phys[0], 1, 32u << 16, g) == 0) { fclose(g); return 0; } fclose(g); }
    static uint8_t *banks_[256]; uint8_t **banks = banks_; huc_banks = banks_;
    for (int b = 0x68; b < 0x88; b++) banks[b] = malloc(8192);   /* (every RAM bank writable: the FPGA's PSRAM) */
    banks[0xF8] = malloc(8192); banks[0xF7] = malloc(2048);
    extern uint8_t *rt_ace_cart; rt_ace_cart = cart16;
    ents = malloc(sizeof *ents * (size_t)maxents);
    rt_init(&rt, &st, banks);
    if (trk) track = fopen(trk, "rb");
    if (st.scsi_o) {
        char err[256];
        if (!scsi_start_tab(&rt.hw, st.scsi_o, st.scsi_fifo, st.toc, st.disc_tab,
                            st.io_t0 + (int64_t)(int32_t)(st.scsi_o[34] - st.io_regs[18]), err, sizeof err)) {
            fprintf(stderr, "huchost: the CD drive not modelled: %s\n", err); return 0; }
    }
    /* (docs/46: the first picture sends everything - VRAM, colours, sprite table - as the PC's FIFO file did
       (rt_pc RUN_FIFO_OUT, fifo_synth's start): a shadow seeded from the state or the runtime left words, then the
       colours, unsent, and the player and cell store kept their own - the title's white blocks and white text) */
    memset(&shadow, 0, sizeof shadow);
    if (getenv("HUC_MASKS")) { long n; rec_masks = slurp(getenv("HUC_MASKS"), &n); rec_n = n / 2; }
    if (getenv("HUC_POKES")) { long n; rec_pokes = slurp(getenv("HUC_POKES"), &n); rec_np = n / 12; }
    if (rec_masks) fprintf(stderr, "huchost: the recording's pad (%ld frames), %ld pokes\n", rec_n, rec_np);
    rt_draw_hook = draw;
    io_psg_hook = psnd_write;
    return 1;
}

/* (docs/41) the CD audio the game started or stopped, as a FIFO entry of its own: type B, x = 1 to loop, d = the PC
   Engine track (0: stop). On the real cart the CD unit's commands reach the SH2 by the mailbox; here, from the drive */
static void cdda_events(void)
{
    static int was; static uint32_t at; static int mode;
    const int playing = rt.hw.cdda_status == 1;
    if (playing && (!was || rt.hw.read_sec_start != at || rt.hw.play_mode != mode)) {
        int trk = 0;
        if (st.disc_tab) for (int t = st.disc_tab[0]; t <= st.disc_tab[1] && t < 101; t++)
            if ((int32_t)rt.hw.read_sec_start >= st.disc_tab[3 + 6 * t]) trk = t;
        if (nents < maxents) ents[nents++] = 0xBu << 28 | (uint32_t)(rt.hw.play_mode == 3) << 16 | (uint32_t)trk;
        at = rt.hw.read_sec_start; mode = rt.hw.play_mode;
    }
    else if (!playing && was && nents < maxents) ents[nents++] = 0xBu << 28;
    was = playing;
}

int huchost_frame(uint16_t pad, const uint32_t **entries)
{
    if (rec_masks) {                                          /* (docs/48: the recording, not the board's pad) */
        pad = rec_k < rec_n ? rec_masks[rec_k] : 0;
        for (long i = 0; i < rec_np; i++) if (rec_pokes[i][0] == (uint32_t)rec_k) {
            const uint32_t a = rec_pokes[i][1];
            if (a < 0x2000) huc_banks[0xF8][a] = (uint8_t)rec_pokes[i][2];
            else if (a - 0x2000 < 0x10000) huc_banks[0x80 + ((a - 0x2000) >> 13)][(a - 0x2000) & 0x1FFF] = (uint8_t)rec_pokes[i][2];
        }
        rec_k++;
    }
    rt_pad_live = pad;
    nents = 0;
    const unsigned a0 = rt.hw.read_addr; const int n0 = rt.hw.nibble, p0 = rt.hw.playing;
    rt_run(&rt, rt.frame + 1, 0);
    cdda_events();
    static unsigned ad_len; if (!p0 && rt.hw.playing) ad_len = rt.hw.length;   /* (the play's length as it started) */
    /* (docs/44) the frame's sound: the PSG as written, the ADPCM the playback passed over */
    nsnd = psnd_frame(rt.hw.ram, rt.hw.addr, ad_len, rt.hw.freq, p0 || rt.hw.playing, snd, SND_RATE, 59.8261);
    (void)a0; (void)n0;
    *entries = ents;
    return nents;
}

/* (docs/44) the samples the last frame made: 16-bit mono at 22,044 Hz */
int huchost_audio(const int16_t **samples) { *samples = snd; return nsnd; }

/* (DIAG, docs/46) the game's VRAM now, 32 K words: what the FIFO's pictures carry */
const uint16_t *huchost_vram(void) { return vsnap; }
