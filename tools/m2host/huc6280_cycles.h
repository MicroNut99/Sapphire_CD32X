/* huc6280_cycles.h - generated: base cycles a HuC6280 opcode takes (tools/huc6280_cycles.json, measured from
 * Mednafen, M2d; the opcodes no trace ran take the regular pattern, which every measured opcode follows).
 * Extras in the runtime: +1 a VDC / VCE access, +2 a branch taken, +1 decimal ADC / SBC, +3 T mode, +6 a block-move
 * byte, +8 an interrupt entry. */
#pragma once
static const unsigned char huc_cycles[256] = {
    8, 7, 3, 4, 6, 4, 6, 7, 3, 2, 2, 2, 7, 5, 7, 6,
    2, 7, 7, 4, 6, 4, 6, 7, 2, 5, 2, 2, 7, 5, 7, 6,
    7, 7, 3, 4, 4, 4, 6, 7, 4, 2, 2, 2, 5, 5, 7, 6,
    2, 7, 7, 2, 4, 4, 6, 7, 2, 5, 2, 2, 5, 5, 7, 6,
    7, 7, 3, 4, 6, 4, 6, 7, 3, 2, 2, 2, 4, 5, 7, 6,
    2, 7, 7, 5, 3, 4, 6, 7, 2, 5, 3, 2, 2, 5, 7, 6,
    7, 7, 2, 2, 4, 4, 6, 7, 4, 2, 2, 2, 7, 5, 7, 6,
    2, 7, 7, 17, 4, 4, 6, 7, 2, 5, 4, 2, 7, 5, 7, 6,
    2, 7, 2, 7, 4, 4, 4, 7, 2, 2, 2, 2, 5, 5, 5, 6,
    2, 7, 7, 8, 4, 4, 4, 7, 2, 5, 2, 2, 5, 5, 5, 6,
    2, 7, 2, 7, 4, 4, 4, 7, 2, 2, 2, 2, 5, 5, 5, 6,
    2, 7, 7, 8, 4, 4, 4, 7, 2, 5, 2, 2, 5, 5, 5, 6,
    2, 7, 2, 17, 4, 4, 6, 7, 2, 2, 2, 2, 5, 5, 7, 6,
    2, 7, 7, 17, 3, 4, 6, 7, 2, 5, 3, 2, 2, 5, 7, 6,
    2, 7, 2, 17, 4, 4, 6, 7, 2, 2, 2, 2, 5, 5, 7, 6,
    2, 7, 7, 17, 2, 4, 6, 7, 2, 5, 4, 2, 2, 5, 7, 6
};
