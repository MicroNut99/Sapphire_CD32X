// sapphire.cpp - Sapphire M0 export (not part of Mednafen): who reads and writes the Arcade Card's 2 MB.
// actrack(): called by the Arcade Card's port data read / write (arcade_card.cpp). Flags per byte:
//   1 = written while the CPU ran System Card ROM code (banks 00-3F), 2 = written by the game,
//   4 = read by the game (port reads; peeks are not counted).
// acstat_get(): totals since the last clear: [0] port writes, [1] port reads.
// acram_copy(): the Arcade Card RAM itself (2 MB).
#include <mednafen/mednafen.h>
#define private public
#include "mednafen/pce/pce.h"
#undef private
#include "mednafen/pce/huc.h"
#include "mednafen/hw_misc/arcade_card/arcade_card.h"
static uint8 ac_flags[0x200000];
static uint64 ac_w, ac_r;
extern "C" void actrack(unsigned int aci, int write)
{
  aci &= 0x1FFFFF;
  if (write) { ac_flags[aci] |= (HuCPU.MPR[(HuCPU.PC >> 13) & 7] < 0x40) ? 1 : 2; ac_w++; }
  else { ac_flags[aci] |= 4; ac_r++; }
}
extern "C" __attribute__((visibility("default"))) uint32 actrack_get(uint8 *out, int clear)
{
  memcpy(out, ac_flags, sizeof(ac_flags));
  if (clear) memset(ac_flags, 0, sizeof(ac_flags));
  return sizeof(ac_flags);
}
extern "C" __attribute__((visibility("default"))) void acstat_get(uint64 *out, int clear)
{
  out[0] = ac_w; out[1] = ac_r; if (clear) ac_w = ac_r = 0;
}
extern ArcadeCard *arcade_card;
extern "C" __attribute__((visibility("default"))) int acram_copy(uint8 *out)
{
  if (!arcade_card) return 0;
  arcade_card->PeekRAM(0, 0x200000, out);
  return 1;
}
