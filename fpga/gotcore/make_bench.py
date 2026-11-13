#!/usr/bin/env python3
"""make_bench.py <Gate-of-Thunder_CD32X checkout> <out dir> - Sapphire docs/64: GoT's cart bench (tools/fpga/cart.cpp, its
CART_SRAM form) for the core make_core.py writes. Edits, each found exactly once (GoT at 4a438c6):
  - the PSRAM model holds the Arcade Card's 2 MB (rom1 offset 0 = AC_BASE) from the window's ac_start.bin, and takes the
    core's writes (rom_we_hi / rom_we_lo); the System Card stays at 0x580000; no data track there
  - the card's registers (sap_ac) from ac_start.bin's 19 words
  - the sectors through the core's sector port (sp_*), a byte a clock, from the drive model (RUN_DISC), when sp_need
  - the battery RAM chip (bram_*): a 2 KB model, as the sram's pins answer (a word at the address, written on WE)
"""
import sys, os
F = os.path.join(sys.argv[1], 'tools/fpga'); O = sys.argv[2]; os.makedirs(O, exist_ok=True)
s = open(os.path.join(F, 'cart.cpp')).read()
s = s.replace('#include "Vcart_got_cart_sram.h"', '#include "Vcart_got_cart_sram.h"\n#include "Vcart_sap_ac.h"', 1)
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('cart.cpp: %d of "%s"' % (s.count(old), old[:80]))
    s = s.replace(old, new)
rep('    memcpy(base, &st[0], 0x2000);',
    '    static std::vector<uint8_t> acb = load(d + "/ac_start.bin");   // (Sapphire docs/64: 19 words, then the card\'s 2 MB)\n'
    '    if (acb.size() != 19 * 4 + 0x200000) { fprintf(stderr, "ac_start.bin: %zu bytes\\n", acb.size()); return 2; }\n'
    '    static std::vector<uint8_t> acram(acb.begin() + 76, acb.end()); g_ac = &acram;   // (the first hash is taken before the PSRAM model is made)\n'
    '    memcpy(base, &st[0], 0x2000);')
# the frame's hash as Sapphire's host makes it (trace_main.c frame_md5): base, CD and Super RAM, the card's 2 MB, VRAM, SAT, colours
rep("        EVP_DigestUpdate(y, ns, 8192); EVP_DigestUpdate(y, zero, sizeof zero);\n        EVP_DigestUpdate(y, cdram, 65536); EVP_DigestUpdate(y, sup, 196608);",
    "        EVP_DigestUpdate(y, ns, 8192);\n        EVP_DigestUpdate(y, cdram, 65536); EVP_DigestUpdate(y, sup, 196608); if (g_ac) EVP_DigestUpdate(y, g_ac->data(), 0x200000);")
rep("static long frame;\nstatic void frame_md5(long k) {", "static long frame;\nstatic std::vector<uint8_t> *g_ac;   // (Sapphire docs/64: the card's RAM, the PSRAM model's)\nstatic void frame_md5(long k) {")
rep("    EVP_DigestUpdate(x, base, 8192); EVP_DigestUpdate(x, zero, sizeof zero);\n    EVP_DigestUpdate(x, cdram, 65536); EVP_DigestUpdate(x, sup, 196608);",
    "    EVP_DigestUpdate(x, base, 8192);\n    EVP_DigestUpdate(x, cdram, 65536); EVP_DigestUpdate(x, sup, 196608); if (g_ac) EVP_DigestUpdate(x, g_ac->data(), 0x200000);")
# the PSRAM model: the card's RAM
rep('    psd.assign(1882u * 2048u, 0); for (int i = 0; i < 1882; i++) scsi_sector(&hw, 3590 + i, &psd[(size_t)i * 2048]);',
    '    psd.swap(acram); g_ac = &psd;   // (Sapphire docs/64: the Arcade Card\'s RAM at rom1 offset 0, a byte an address)\n'
    '    { auto *C = top->cart_tb->w->__PVT__card; const uint32_t *aw = (const uint32_t *)acb.data();   // (its registers, as ac_start: base, offset, increment, control a port)\n'
    '      for (int i = 0; i < 4; i++) { C->base[i] = aw[i * 4] & 0xFFFFFF; C->offs[i] = aw[i * 4 + 1] & 0xFFFF; C->incr[i] = aw[i * 4 + 2] & 0xFFFF; C->ctrl[i] = aw[i * 4 + 3] & 0x7F; }\n'
    '      C->latch = aw[16]; C->shift_bits = aw[17] & 15; C->rotate_bits = aw[18] & 15; }\n'
    '    static uint16_t brm[1024]; long ac_w = 0; int sp_st = 0, sp_i = 0; uint8_t sp_buf[2048];   // (the battery RAM chip; the card\'s writes; the sector port)')
rep('''          if (ra < 0x580000u) { const uint32_t o0 = ra & ~1u;   // (docs/38: from sector 992 the track is 64 KB further - the stack's page hole)
            const bool hole = o0 >= 0x1F0000u && o0 < 0x200000u; const uint32_t o = o0 >= 0x200000u ? o0 - 0x10000u : o0;
            top->rom_dato = !hole && o + 1 < psd.size() ? (uint16_t)(psd[o] << 8 | psd[o + 1]) : 0;   // (docs/33: as the 68000 writes the file - the even byte high)
            if (top->rom_req && !r_was && !booting) { psn++; if (hole || o + 1 >= psd.size()) psbad++; } }''',
    '''          if (ra < 0x580000u) { const uint32_t o = ra & ~1u;   // (Sapphire docs/64: the card's RAM, the even byte high)
            top->rom_dato = o + 1 < psd.size() ? (uint16_t)(psd[o] << 8 | psd[o + 1]) : 0;
            if (top->rom_req && !r_was && !booting) { psn++; if (o + 1 >= psd.size()) psbad++; }
            if ((top->rom_we_hi || top->rom_we_lo) && o + 1 < psd.size()) { ac_w++;
              if (top->rom_we_hi) psd[o] = (uint8_t)(top->rom_dati >> 8); if (top->rom_we_lo) psd[o + 1] = (uint8_t)top->rom_dati; } }''')
# the sector port and the battery RAM chip, each clock before the edge
rep('        if (pt && pa_age < pt - 1) top->rom_dato ^= 0xA5C3;',
    '''        if (pt && pa_age < pt - 1) top->rom_dato ^= 0xA5C3;
          top->bram_dato = brm[(top->bram_addr >> 1) & 1023];
          if (top->bram_we_hi) brm[(top->bram_addr >> 1) & 1023] = (uint16_t)((brm[(top->bram_addr >> 1) & 1023] & 0x00FF) | (top->bram_dati & 0xFF00));
          if (top->bram_we_lo) brm[(top->bram_addr >> 1) & 1023] = (uint16_t)((brm[(top->bram_addr >> 1) & 1023] & 0xFF00) | (top->bram_dati & 0x00FF));
          // (Sapphire docs/64) the sector port: the sector wanted, from the drive model, a byte a clock; then "done"
          top->sp_rst = 0; top->sp_we = 0; top->sp_commit = 0;
          if (!booting) { if (sp_st == 0 && top->sp_need) { const int32_t l = (int32_t)top->sec_want_o; memset(sp_buf, 0, sizeof sp_buf); scsi_sector(&hw, l, sp_buf);
                  lba_n++; if (l < lba_min) lba_min = l; if (l > lba_max) lba_max = l; top->sp_rst = 1; sp_st = 1; sp_i = 0; }
              else if (sp_st == 1) { top->sp_we = 1; top->sp_d = sp_buf[sp_i]; if (++sp_i == 2048) sp_st = 2; }
              else if (sp_st == 2) { top->sp_commit = 1; sp_st = 3; }
              else if (sp_st == 3) sp_st = 0; }''')
# the old feed's sector log (its signals are gone)
rep("        { auto *W = top->cart_tb->w; if (W->sec_we && W->sec_wa == 0) { const int32_t l = (int32_t)W->f_lba; lba_n++;",
    "        if (0) { auto *W = top->cart_tb->w; if (W->sec_we && W->sec_wa == 0) { const int32_t l = 0; lba_n++;")
rep('      printf("PSRAM data track: %ld word reads, %ld outside it\\n", psn, psbad);',
    '      printf("PSRAM (the Arcade Card): %ld word reads, %ld outside it, %ld byte writes; the card flags (lost write, unused feature) %d\\n", psn, psbad, ac_w, (int)top->d_ac);')
open(os.path.join(O, 'scart.cpp'), 'w').write(s)
print('wrote', os.path.join(O, 'scart.cpp'))
