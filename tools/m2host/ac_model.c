/* ac_model.c - the Arcade Card for the M2 host: see ac_model.h. Line for line Mednafen's ArcadeCard::Read / Write. */
#include "ac_model.h"

void ac_start(ArcadeCard *ac, const uint32_t *s, uint8_t *ram)
{
    for (int i = 0; i < 4; i++) {
        ac->port[i].base = s[i * 4] & 0xFFFFFF; ac->port[i].offset = (uint16_t)s[i * 4 + 1];
        ac->port[i].increment = (uint16_t)s[i * 4 + 2]; ac->port[i].control = (uint8_t)s[i * 4 + 3];
    }
    ac->shift_latch = s[16]; ac->shift_bits = (uint8_t)s[17]; ac->rotate_bits = (uint8_t)s[18];
    ac->ram = ram; ac->reads = ac->writes = 0;
}

void ac_state(const ArcadeCard *ac, uint32_t *s)
{
    for (int i = 0; i < 4; i++) {
        s[i * 4] = ac->port[i].base; s[i * 4 + 1] = ac->port[i].offset;
        s[i * 4 + 2] = ac->port[i].increment; s[i * 4 + 3] = ac->port[i].control;
    }
    s[16] = ac->shift_latch; s[17] = ac->shift_bits; s[18] = ac->rotate_bits;
}

static uint32_t ac_index(const ACPort *p)
{
    uint32_t aci = p->base;
    if (p->control & 0x2) {
        aci += p->offset;
        if (p->control & 0x8) aci += 0xFF0000;
    }
    return aci & 0x1FFFFF;
}

static void ac_auto_increment(ACPort *p)
{
    if (p->control & 0x1) {
        if (p->control & 0x10) p->base = (p->base + p->increment) & 0xFFFFFF;
        else p->offset = (uint16_t)(p->offset + p->increment);
    }
}

static void ac_add_offset(ACPort *p)
{
    if (p->control & 0x08) p->base += 0xFF0000;
    p->base = (p->base + p->offset) & 0xFFFFFF;
}

uint8_t ac_read(ArcadeCard *ac, uint32_t a)
{
    if ((a & 0x1F00) != 0x1A00) return 0xFF;
    if ((a & 0xFF) < 0x80) {
        ACPort *p = &ac->port[(a >> 4) & 3];
        switch (a & 0xF) {
        case 0x0: case 0x1: { const uint8_t r = ac->ram[ac_index(p)]; ac->reads++; ac_auto_increment(p); return r; }
        case 0x2: return (uint8_t)p->base;
        case 0x3: return (uint8_t)(p->base >> 8);
        case 0x4: return (uint8_t)(p->base >> 16);
        case 0x5: return (uint8_t)p->offset;
        case 0x6: return (uint8_t)(p->offset >> 8);
        case 0x7: return (uint8_t)p->increment;
        case 0x8: return (uint8_t)(p->increment >> 8);
        case 0x9: return p->control;
        }
    } else if ((a & 0xFF) >= 0xE0) {
        switch (a & 0x1F) {
        case 0x00: case 0x01: case 0x02: case 0x03: return (uint8_t)(ac->shift_latch >> (a & 3) * 8);
        case 0x04: return ac->shift_bits;
        case 0x05: return ac->rotate_bits;
        case 0x1C: case 0x1D: return 0x00;
        case 0x1E: return 0x10;                         /* the version, as Mednafen */
        case 0x1F: return 0x51;                         /* the Arcade Card's ID */
        }
    }
    return 0xFF;
}

void ac_write(ArcadeCard *ac, uint32_t a, uint8_t v)
{
    if ((a & 0x1F00) != 0x1A00) return;
    if ((a & 0xFF) < 0x80) {
        ACPort *p = &ac->port[(a >> 4) & 3];
        switch (a & 0xF) {
        case 0x0: case 0x1: ac->ram[ac_index(p)] = v; ac->writes++; ac_auto_increment(p); break;
        case 0x2: p->base = (p->base & ~0xFFu) | v; break;
        case 0x3: p->base = (p->base & ~0xFF00u) | (uint32_t)v << 8; break;
        case 0x4: p->base = (p->base & ~0xFF0000u) | (uint32_t)v << 16; break;
        case 0x5: p->offset = (uint16_t)((p->offset & ~0xFF) | v); if ((p->control & 0x60) == 0x20) ac_add_offset(p); break;
        case 0x6: p->offset = (uint16_t)((p->offset & ~0xFF00) | v << 8); if ((p->control & 0x60) == 0x40) ac_add_offset(p); break;
        case 0x7: p->increment = (uint16_t)((p->increment & ~0xFF) | v); break;
        case 0x8: p->increment = (uint16_t)((p->increment & ~0xFF00) | v << 8); break;
        case 0x9: p->control = v & 0x7F; break;
        case 0xA: if ((p->control & 0x60) == 0x60) ac_add_offset(p); break;
        default: break;
        }
    } else if ((a & 0xFF) >= 0xE0) {
        switch (a & 0x1F) {
        case 0x00: case 0x01: case 0x02: case 0x03:
            ac->shift_latch &= ~(0xFFu << (a & 3) * 8); ac->shift_latch |= (uint32_t)v << (a & 3) * 8; break;
        case 0x04:
            ac->shift_bits = v & 0xF;
            if (ac->shift_bits) {
                if (ac->shift_bits & 0x8) ac->shift_latch >>= 16 - ac->shift_bits;
                else ac->shift_latch <<= ac->shift_bits;
            }
            break;
        case 0x05:
            ac->rotate_bits = v & 0xF;
            if (ac->rotate_bits) {
                if (ac->rotate_bits & 0x8) {
                    const unsigned sa = 16 - ac->rotate_bits;
                    const unsigned orv = ac->shift_latch << (32 - sa);
                    ac->shift_latch = (ac->shift_latch >> sa) | orv;
                } else {
                    const unsigned sa = ac->rotate_bits;
                    const unsigned orv = (ac->shift_latch >> (32 - sa)) & ((1u << sa) - 1);
                    ac->shift_latch = (ac->shift_latch << sa) | orv;
                }
            }
            break;
        default: break;
        }
    }
}
