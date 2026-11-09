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

uint32_t *rt_ro_hist, *rt_interp_hist;                  /* (rt_pc's DIAG histograms: none) */
#ifdef HUCHOST_INTERP
const BlockDef blocks[] = { { 0, NULL, NULL, NULL, NULL } };   /* (no translated code: the interpreter runs it all - NOT
                                                                   equal to the blocks from power-on: docs/40) */
#endif

static Rt rt; static RtState st; static uint8_t *cart16; static FILE *track;
static FifoShadow shadow; static uint32_t *ents; static int nents, maxents = 1 << 18;

int rt_disc_sector(int32_t sector, uint8_t *out)
{
    if (!track || sector < 0 || fseek(track, (long)sector * 2048, SEEK_SET) || fread(out, 1, 2048, track) != 2048) return 0;
    return 1;
}

static void draw(V32 *v)
{
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
    static uint8_t *banks[256];
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
    /* (the player starts from the same state's VRAM, colours and sprite table: what changed from there) */
    memcpy(shadow.vram, rt.vdc.vram, sizeof shadow.vram); memcpy(shadow.color, rt.vdc.color, sizeof shadow.color);
    memcpy(shadow.sat, rt.vdc.sat, sizeof shadow.sat); shadow.dvssr = 0xFFFF; shadow.cur = 0x10000; shadow.started = 1;
    rt_draw_hook = draw;
    return 1;
}

int huchost_frame(uint16_t pad, const uint32_t **entries)
{
    rt_pad_live = pad;
    nents = 0;
    rt_run(&rt, rt.frame + 1, 0);
    *entries = ents;
    return nents;
}
