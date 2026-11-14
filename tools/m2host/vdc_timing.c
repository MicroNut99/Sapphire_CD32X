/* vdc_timing.c - M2: when the PC Engine's video chips do things (docs/13). Our own model of the HuC6260 VCE's line
 * and frame counters and the HuC6270 VDC's horizontal and vertical phases, written from Mednafen's behaviour (no
 * Mednafen code) and checked against its trace: the interrupts (vertical blank, raster, sprite-table and VRAM DMA
 * ends), the sprite-table copy, the VRAM DMA as it runs, and the times the VDC holds a CPU access back.
 *
 * Time is master clocks (21.48 MHz). The VCE: a line is 1,365 (horizontal blank 237, then 1,128); a frame is 263
 * lines (358,995) with the 263-line bit, 357,630 without; vertical blank 4,095 of it. The VDC runs on its own clock,
 * one tick every 4 master clocks (3 or 2 at the faster dot clocks), on a grid that the line doesn't divide.
 *
 * The VDC's line, from the end of the horizontal sync: HSW 8 ticks, then HDS (display start: three steps, (HDS+1)*8
 * in all), HDW (the display, (HDW+1)*8 - 4), HDW_FINAL (4: the raster counter steps here), HDE ((HDE+1)*8), HSW
 * ((HSW+1)*8), round again until the VCE's next sync restarts it. 48 ticks after a horizontal sync starts, the VDC
 * is busy for 16 ticks: an access the CPU makes to the VRAM data port is held until it ends. The frame: VSW, VDS,
 * VDW (the display lines: sprites fetched for each, up to 16, 4 ticks each), VCR; leaving VDW, the sprite table is
 * copied (1,024 ticks busy) if asked for, then a pending VRAM DMA runs (4 ticks a word). */
#include <string.h>
#include "vdc_model.h"

enum { H_HDS, H_HDS2, H_HDS3, H_HDW, H_HDW_FINAL, H_HDE, H_HSW };
enum { V_VDS, V_VDW, V_VCR, V_VSW };
static const int32_t ratios[4] = { 4, 3, 2, 2 };

static void raise(Vdc *v, uint8_t bit)
{
    if (!(v->status & 0x3F)) v->irq_rise = v->tick_now;      /* the IRQ line goes high at this tick */
    v->status |= bit;
}

static uint32_t t_byr(const Vdc *v) { return 1 + (v->hds_c > 2 ? (v->hds_c + 1) * 8 - 24 + 2 : 0); }
static uint32_t t_bxr(const Vdc *v) { return v->hds_c > 2 ? 1 : 2; }

static void fetch_sprites(Vdc *v)                            /* the sprites on this display line: 4 ticks each, 16 at most */
{
    int n = 0;
    static const int hgt[4] = { 16, 32, 64, 64 };
    for (int i = 0; i < 64 && n < 16; i++) {
        const int y = (int)(v->sat[i * 4] & 0x3FF) - 0x40;
        const uint16_t fl = v->sat[i * 4 + 3];
        const int h = hgt[(fl >> 12) & 3];
        if ((int)v->rcr_count >= y && (int)v->rcr_count < y + h) n += (fl & 0x100) ? 2 : 1;
    }
    if (n > 16) n = 16;
    v->spr_fetch = n * 4;                                     /* (sprite overflow, CR bit 1, is not enabled here) */
}

static void bgy_step(Vdc *v)                                  /* (docs/105: the Y offset's step, where Mednafen takes it) */
{
    if (!v->need_bgy_inc) return;
    v->need_bgy_inc = 0;
    v->bg_ymoo = v->rcr_count == 0 ? v->BYR : (uint16_t)(v->bg_ymoo + 1);
}

static void inc_rcr(Vdc *v)
{
    bgy_step(v); v->need_bgy_inc = 1;
    v->rcr_count++;
    if (--v->vphase_ctr <= 0) {
        v->vphase = (v->vphase + 1) % 4;
        switch (v->vphase) {
        case V_VDS: v->vphase_ctr = (int32_t)v->vds_c + 2; if (vdc_split_hook) vdc_split_hook(v, -1); break;
        case V_VDW:
            v->vphase_ctr = (int32_t)v->vdw_c + 1; v->rcr_count = 0;
            if (vdc_line0_hook) vdc_line0_hook(v);            /* (Sapphire docs/55: the display's first line - an observer) */
            v->burst_mode = !(v->CR & 0xC0);
            v->need_vb_test = 1; v->need_sat_test = 1;
            if (!v->burst_mode) {                             /* the display starts: a copy or a DMA still running stops */
                if (v->sat_dma > 0) { v->sat_dma = 0; vdc_commit(v); }
                if (v->dma_running) { v->dma_running = 0; v->dma_cut++; vdc_commit(v); }
            }
            break;
        case V_VCR: v->vphase_ctr = (int32_t)v->vcr_c; if (vdc_split_hook) vdc_split_hook(v, -1); break;   /* (the display's end) */
        case V_VSW:
            if (vdc_split_hook) vdc_split_hook(v, -1);
            v->vphase_ctr = (int32_t)v->vsw_c + 1; v->mwr_c = v->MWR;
            v->vds_c = (v->VSR >> 8) & 0xFF; v->vsw_c = v->VSR & 0x1F; v->vdw_c = v->VDR & 0x1FF; v->vcr_c = v->VCR & 0xFF;
            break;
        }
    }
    if (v->vphase == V_VDW && !v->burst_mode) fetch_sprites(v);
    if ((int)v->rcr_count == (int)v->RCR - 0x40 && (v->CR & 4)) { raise(v, 0x04); if (vdc_split_hook) vdc_split_hook(v, 1); }   /* (docs/55: a raster match) */
}

static void hds_start(Vdc *v)
{
    if (v->need_rcr_inc) { inc_rcr(v); v->need_rcr_inc = 0; }
    if (v->spr_fetch > 0) { v->spr_fetch = 0; vdc_commit(v); }
    v->hsw_c = v->HSR & 0x1F; v->hds_c = (v->HSR >> 8) & 0x7F; v->hdw_c = v->HDR & 0x7F; v->hde_c = (v->HDR >> 8) & 0x7F;
    v->cr_c = v->CR;
    v->hphase = H_HDS; v->hphase_ctr = (int32_t)t_byr(v);
}

static void dma_step(Vdc *v)                                  /* one half of a word: read, then write */
{
    if (!v->dma_rw) v->dma_buf = v->vram[v->SOUR];
    else {
        if (v->DESR < 0x8000) { v->vram[v->DESR] = v->dma_buf; if (vdc_vram_hook) vdc_vram_hook(v->DESR); }
        v->SOUR += (v->DCR & 4) ? 0xFFFF : 1; v->DESR += (v->DCR & 8) ? 0xFFFF : 1;
        if (--v->LENR == 0xFFFF) { v->dma_running = 0; vdc_commit(v); if (v->DCR & 2) raise(v, 0x10); return; }
    }
    v->dma_rw ^= 1;
}

static void vdc_tick(Vdc *v)                                  /* one VDC clock */
{
    if (v->myst_ctr > 0 && --v->myst_ctr <= 0) {
        v->myst_ph = !v->myst_ph;
        if (v->myst_ph) v->myst_ctr = 16; else vdc_commit(v);
    }
    if (v->spr_fetch > 0 && --v->spr_fetch <= 0) vdc_commit(v);
    if (v->vphase != V_VDW && v->need_sat_test) {
        v->need_sat_test = 0;
        if (v->satb_pending || (v->DCR & 0x10)) {
            v->satb_pending = 0; v->sat_dma = 1024;
            if (v->DVSSR < 0x8000) {
                uint32_t len = v->DVSSR > 0x8000 - 0x100 ? 0x8000u - v->DVSSR : 256;
                memcpy(v->sat, &v->vram[v->DVSSR], len * 2);
            }
        } else v->burst_mode = 1;
    }
    if (v->dma_pending && v->burst_mode) { v->dma_pending = 0; v->dma_running = 1; v->vdma_cyc = 0; v->dma_rw = 0; }
    if (v->sat_dma > 0) {
        if (--v->sat_dma <= 0) { if (v->DCR & 1) raise(v, 0x08); vdc_commit(v); v->burst_mode = 1; }
    } else if (v->dma_running) {
        if (++v->vdma_cyc >= 2) { v->vdma_cyc = 0; dma_step(v); }
    }
    if (--v->hphase_ctr > 0) return;
    while (v->hphase_ctr <= 0) {
        v->hphase = (v->hphase + 1) % 7;
        switch (v->hphase) {
        case H_HDS: hds_start(v); break;
        case H_HDS2: v->hphase_ctr = (int32_t)t_bxr(v); if (!v->in_exhsync) bgy_step(v); v->bg_yoff = v->bg_ymoo; break;
        case H_HDS3: v->hphase_ctr = (int32_t)((v->hds_c + 1) * 8 - t_byr(v) - t_bxr(v));
            if (v->rcr_count < 263) { v->lines[v->rcr_count][0] = v->BXR; v->lines[v->rcr_count][1] = (uint16_t)(v->bg_yoff | (v->CR & 0x40 ? 0 : 0x8000)); }   /* (Sapphire docs/69: bit 15, the line's sprites off - as the cart core's step 5) */
            break;
        case H_HDW:
            v->need_rcr_inc = 1;
            if (v->vphase != V_VDW && v->need_vb_test) { if (v->CR & 8) raise(v, 0x20); v->need_vb_test = 0; }
            vdc_commit(v);
            v->hphase_ctr = (int32_t)(v->hdw_c + 1) * 8 - 4;
            break;
        case H_HDW_FINAL:
            if (v->need_rcr_inc) { inc_rcr(v); v->need_rcr_inc = 0; }
            v->hphase_ctr = 4;
            break;
        case H_HDE: v->hphase_ctr = (int32_t)(v->hde_c + 1) * 8; break;
        case H_HSW: v->hphase_ctr = (int32_t)(v->hsw_c + 1) * 8; break;
        }
    }
}

static void hsync(Vdc *v, int hb)
{
    if ((v->CR >> 4) & 3) { v->in_exhsync = 0; return; }      /* external sync: not used here */
    v->in_exhsync = hb;
    if (hb) { v->myst_ctr = 48; v->myst_ph = 0; }             /* into the horizontal sync */
    else { v->hphase = H_HSW; v->hphase_ctr = 8; }            /* out of it: the line restarts */
}

static void vsync(Vdc *v, int vb)
{
    if (((v->CR >> 4) & 3) >= 2) { v->in_exvsync = 0; return; }
    v->in_exvsync = vb;
    if (vb) {
        v->need_rcr_inc = 0; v->mwr_c = v->MWR;
        v->vds_c = (v->VSR >> 8) & 0xFF; v->vsw_c = v->VSR & 0x1F; v->vdw_c = v->VDR & 0x1FF; v->vcr_c = v->VCR & 0xFF;
        v->vphase = V_VSW; v->vphase_ctr = (int32_t)v->vsw_c + 1;
    }
}

void vt_run(Vdc *v, int64_t t)
{
    while (v->now < t) {
        int64_t nxt = t;
        const int64_t th = v->now + v->hblank_ctr, tv = v->now + v->vblank_ctr, tk = v->now + (v->ratio - v->clkdiv);
        if (th < nxt) nxt = th;
        if (tv < nxt) nxt = tv;
        if (tk < nxt) nxt = tk;
        const int32_t d = (int32_t)(nxt - v->now);
        v->clkdiv += d; v->hblank_ctr -= d; v->vblank_ctr -= d; v->now = nxt;
        if (v->clkdiv >= v->ratio) { v->clkdiv -= v->ratio; v->tick_now = v->now; vdc_tick(v); }
        if (v->hblank_ctr <= 0) {                             /* the VCE's horizontal blank starts or ends */
            v->hblank ^= 1;
            if (!v->hblank) {
                if (v->need_sl_reset) v->scanline = 0; else v->scanline++;
                /* Mednafen's two run slices a frame end here: at line 123, and at the frame's end, line 14 + the last
                   line shown + 1 (the core's libretro default shows lines up to 242: 257) */
                if (v->scanline == 14 + 243) { v->exit_req = 2; v->exit_time = v->now; }
                else if (v->scanline == 123) { v->exit_req = 1; v->exit_time = v->now; }
                v->need_sl_reset = 0;
            }
            v->hblank_ctr = v->hblank ? 237 : 1128;
            v->tick_now = v->now; hsync(v, v->hblank);
        }
        if (v->vblank_ctr <= 0) {
            v->vblank ^= 1;
            v->vblank_ctr = v->vblank ? 4095 : (v->lc263 ? 358995 : 357630) - 4095;
            if (!v->vblank) v->need_sl_reset = 1;
            v->tick_now = v->now; vsync(v, v->vblank);
        }
    }
}

static int32_t vdc_next(const Vdc *v)                         /* VDC clocks to its next event */
{
    int32_t n = v->hphase_ctr;
    if (v->sat_dma > 0 && v->sat_dma < n) n = v->sat_dma;
    if (v->spr_fetch > 0 && v->spr_fetch < n) n = v->spr_fetch;
    if (v->dma_running) {
        const int32_t d = ((int32_t)v->LENR + 1) * 4 - v->dma_rw * 2 - v->vdma_cyc;
        if (d > 0 && d < n) n = d;
    }
    return n;
}

int32_t vt_next_event(Vdc *v, int64_t cd_next)
{
    int64_t n = v->hblank_ctr;
    if (v->vblank_ctr < n) n = v->vblank_ctr;
    if (cd_next - v->now < n) n = cd_next - v->now;
    const int64_t c = (int64_t)vdc_next(v) * v->ratio - v->clkdiv;
    if (c < n) n = c;
    return n < 1 ? 1 : (int32_t)n;
}

int vt_irq_at(const Vdc *v, int64_t t)
{
    return v->irq_rise <= t || (v->irq_prev_rise <= t && t < v->irq_prev_clear);
}

void vt_dot_clock(Vdc *v, uint8_t cr)
{
    const int dc = cr & 3;
    if (dc != v->dot_clock) v->clkdiv = 0;                    /* Mednafen restarts the divider */
    v->dot_clock = dc; v->ratio = ratios[dc]; v->lc263 = (cr & 4) != 0;
}

void vt_start(Vdc *v, const uint32_t *r, int64_t t0)
{
    v->timed = 1; v->now = t0; v->tick_now = t0;
    v->satb_pending = (uint8_t)r[30]; v->sat_dma = (int32_t)r[31]; v->burst_mode = (int)r[32]; v->vdma_cyc = (int32_t)r[33];
    v->dma_buf = (uint16_t)r[26]; v->dma_rw = (int)r[27]; v->dma_running = (uint8_t)r[28]; v->dma_pending = (uint8_t)r[29];
    v->dot_clock = (int)r[36]; v->lc263 = (int)r[37];
    v->hblank = (int)r[38]; v->hblank_ctr = (int32_t)r[39]; v->vblank = (int)r[40]; v->vblank_ctr = (int32_t)r[41];
    v->clkdiv = (int32_t)r[42]; v->scanline = (int32_t)r[46]; v->need_sl_reset = (int32_t)r[47]; v->ratio = (int32_t)r[48];
    v->hphase = (int)r[50]; v->hphase_ctr = (int32_t)r[51]; v->vphase = (int)r[52]; v->vphase_ctr = (int32_t)r[53];
    v->myst_ctr = (int32_t)r[54]; v->myst_ph = (int)r[55]; v->spr_fetch = (int32_t)r[56]; v->rcr_count = r[57];
    v->need_rcr_inc = (int)r[58]; v->need_vb_test = (int)r[59]; v->need_sat_test = (int)r[60];
    v->hsw_c = r[62]; v->hds_c = r[63]; v->hdw_c = r[64]; v->hde_c = r[65];
    v->vds_c = r[66]; v->vsw_c = r[67]; v->vdw_c = r[68]; v->vcr_c = r[69];
    v->cr_c = (uint16_t)r[70]; v->mwr_c = (uint16_t)r[71]; v->in_exhsync = (int)r[72]; v->in_exvsync = (int)r[73];
    v->irq_rise = (v->status & 0x3F) ? t0 : INT64_MAX; v->irq_prev_rise = INT64_MAX; v->irq_prev_clear = 0;
}
