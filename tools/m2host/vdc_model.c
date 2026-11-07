/* vdc_model.c - M2e: the PC Engine's video chips as the game sees them (HuC6270 VDC, HuC6260 VCE), our own
 * model, written from Mednafen's behaviour (docs/11) and checked against its trace. No pixels: the CD32X draws
 * from this state (M1). What it keeps: VRAM, the sprite table (SAT), the colour table, the registers.
 *
 * Timing (when the sprite table is copied, when a VRAM DMA runs, when the status bits rise): untimed, it is given from
 * outside (vdc_event(): the trace check passes Mednafen's events); timed, vdc_timing.c makes it (docs/13). */
#include <stdint.h>
#include <string.h>
#include "vdc_model.h"

static const uint16_t vram_inc[4] = { 1, 32, 64, 128 };

void vdc_init(Vdc *v) { memset(v, 0, sizeof *v); v->irq_rise = v->irq_prev_rise = INT64_MAX; v->irq_prev_clear = 0; }

int vdc_watch = -1;                           /* debugging: report writes to one VRAM word */
#include <stdio.h>
static void commit_pending(Vdc *v)
{
    /* the VDC holds an access while it is busy: with the timing model, its sprite-table copy, a VRAM DMA, or the
       16-clock phase in each horizontal sync (docs/13); without it, as the trace's events say */
    if (v->timed ? (v->sat_dma > 0 || v->dma_running || v->myst_ph) : v->busy) return;
    if (v->pending_write) {
        if (v->pending_write_addr == vdc_watch) fprintf(stderr, "  VRAM %04X <- %04X\n", vdc_watch, v->pending_write_latch);
        if (v->pending_write_addr < 0x8000) v->vram[v->pending_write_addr] = v->pending_write_latch;   /* 32 K words */
        v->pending_write = 0;
    }
    if (v->pending_read) { v->read_buffer = v->vram[v->pending_read_addr]; v->pending_read = 0; }
}

void vdc_commit(Vdc *v) { commit_pending(v); }

/* the CPU touches the data port while a DMA runs: it waits for the DMA to finish (Mednafen DoWaitStates) */
static void wait_states(Vdc *v)
{
    if (v->timed) return;                     /* timed: the caller has waited (trace_main.c) */
    if (v->dma_running) vdc_dma_finish(v);
    v->busy = 0;
    commit_pending(v);
}

void vdc_dma_finish(Vdc *v)
{
    if (!v->dma_running) return;
    do {
        const uint16_t w = v->vram[v->SOUR];
        if (v->DESR == vdc_watch) fprintf(stderr, "  VRAM %04X <- %04X (DMA from %04X)\n", vdc_watch, w, v->SOUR);
        v->vram[v->DESR] = w;
        v->SOUR += (v->DCR & 4) ? -1 : 1;
        v->DESR += (v->DCR & 8) ? -1 : 1;
        v->LENR--;
    } while (v->LENR != 0xFFFF);
    v->dma_running = 0;
}

void vdc_event(Vdc *v, int kind, uint32_t arg)
{
    switch (kind) {
    case 3:                                   /* the sprite table copied from VRAM at DVSSR (1024 VDC clocks busy) */
        memcpy(v->sat, &v->vram[v->DVSSR], 256 * 2);
        v->satb_pending = 0;
        v->busy = 1;
        break;
    case 4:                                   /* a VRAM-to-VRAM DMA starts: it reads and writes as it goes; the CPU
                                                 can't see VRAM until it ends, so it is done here at once */
        v->dma_pending = 0; v->dma_running = 1;
        vdc_dma_finish(v);
        v->dma_running = 1;                   /* (the end event clears it) */
        break;
    case 5: v->dma_running = 0; v->busy = 0; commit_pending(v); break;
    case 6: v->status |= (uint8_t)arg; break; /* an interrupt source: VD, RR, DS, DV */
    case 7: v->busy = 0; commit_pending(v); break;   /* the sprite table copy's 1024 clocks are over */
    }
}

uint8_t vdc_read(Vdc *v, unsigned a)
{
    const int msb = a & 1;
    switch (a & 3) {
    case 0: {
        const uint8_t r = (uint8_t)(v->status | ((v->timed && (v->pending_read || v->pending_write)) ? 0x40 : 0));
        if (v->timed && (v->status & 0x3F)) { v->irq_prev_rise = v->irq_rise; v->irq_prev_clear = v->now; }
        if (v->timed) v->irq_rise = INT64_MAX;
        v->status &= ~0x3F;                   /* untimed: bit 6 (busy) depends on timing, the checker masks it */
        return r;
    }
    case 2: case 3: {
        wait_states(v);
        const uint8_t r = msb ? (uint8_t)(v->read_buffer >> 8) : (uint8_t)v->read_buffer;
        if (v->select == 2 && msb) {
            v->pending_read = 1; v->pending_read_addr = v->MARR;
            v->MARR += vram_inc[(v->CR >> 11) & 3];
            commit_pending(v);
        }
        return r;
    }
    }
    return 0;
}

#define SETP(reg, val, msb) (reg) = (uint16_t)((msb) ? (((reg) & 0x00FF) | ((val) << 8)) : (((reg) & 0xFF00) | (val)))

void vdc_write(Vdc *v, unsigned a, uint8_t val)
{
    const int msb = a & 1;
    switch (a & 3) {
    case 0: v->select = val & 0x1F; return;
    case 1: return;
    }
    switch (v->select) {
    case 0x00: SETP(v->MAWR, val, msb); break;
    case 0x01: SETP(v->MARR, val, msb);
        if (msb) {
            wait_states(v);
            v->pending_read = 1; v->pending_read_addr = v->MARR;
            v->MARR += vram_inc[(v->CR >> 11) & 3];
            commit_pending(v);
        }
        break;
    case 0x02:
        if (!msb) v->write_latch = val;
        else {
            wait_states(v);
            v->pending_write = 1; v->pending_write_addr = v->MAWR;
            v->pending_write_latch = (uint16_t)(v->write_latch | val << 8);
            v->MAWR += vram_inc[(v->CR >> 11) & 3];
            commit_pending(v);
        }
        break;
    case 0x05: SETP(v->CR, val, msb); break;
    case 0x06: SETP(v->RCR, val, msb); v->RCR &= 0x3FF; break;
    case 0x07: SETP(v->BXR, val, msb); v->BXR &= 0x3FF; break;
    case 0x08: SETP(v->BYR, val, msb); v->BYR &= 0x1FF; break;
    case 0x09: SETP(v->MWR, val, msb); break;
    case 0x0A: SETP(v->HSR, val, msb); break;
    case 0x0B: SETP(v->HDR, val, msb); break;
    case 0x0C: SETP(v->VSR, val, msb); break;
    case 0x0D: SETP(v->VDR, val, msb); break;
    case 0x0E: SETP(v->VCR, val, msb); break;
    case 0x0F: SETP(v->DCR, val, msb); break;
    case 0x10: SETP(v->SOUR, val, msb); break;
    case 0x11: SETP(v->DESR, val, msb); break;
    case 0x12: SETP(v->LENR, val, msb); if (msb) v->dma_pending = 1; break;
    case 0x13: SETP(v->DVSSR, val, msb); v->satb_pending = 1; break;
    }
}

uint8_t vce_read(Vdc *v, unsigned a)
{
    switch (a & 7) {
    case 4: return (uint8_t)v->color[v->ctaddress & 0x1FF];
    case 5: { uint8_t r = (uint8_t)(((v->color[v->ctaddress & 0x1FF] >> 8) & 1) | 0xFE);
              v->ctaddress = (v->ctaddress + 1) & 0x1FF; return r; }
    }
    return 0xFF;
}

void vce_write(Vdc *v, unsigned a, uint8_t val)
{
    switch (a & 7) {
    case 0: if (v->timed) vt_dot_clock(v, val); v->vce_cr = val; break;
    case 2: v->ctaddress = (uint16_t)((v->ctaddress & 0x100) | val); break;
    case 3: v->ctaddress = (uint16_t)((v->ctaddress & 0x0FF) | (val & 1) << 8); break;
    case 4: v->color[v->ctaddress & 0x1FF] = (uint16_t)((v->color[v->ctaddress & 0x1FF] & 0x100) | val); break;
    case 5: v->color[v->ctaddress & 0x1FF] = (uint16_t)((v->color[v->ctaddress & 0x1FF] & 0xFF) | (val & 1) << 8);
            v->ctaddress = (v->ctaddress + 1) & 0x1FF; break;
    }
}
