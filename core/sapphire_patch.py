#!/usr/bin/env python3
"""sapphire_patch.py <beetle-pce dir> - Sapphire's additions on top of DraculaX's dumpvid.patch (run after it):
the Arcade Card's port reads and writes call actrack() (sapphire.cpp); its accesses through banks 40-43 go into
the M2 instruction trace (kind 14 read, 15 write, the physical address and the value); the exports are linked."""
import sys
D = sys.argv[1]
def edit(p, a, b):
    s = open(D + '/' + p).read()
    if b in s: return
    assert a in s, (p, a)
    open(D + '/' + p, 'w').write(s.replace(a, b, 1))
edit('mednafen/hw_misc/arcade_card/arcade_card.cpp', '#include "arcade_card.h"',
     '#include "arcade_card.h"\nextern "C" void actrack(unsigned int aci, int write);   /* Sapphire M0: sapphire.cpp */')
edit('mednafen/hw_misc/arcade_card/arcade_card.cpp', "\t      ret = ACRAM[aci];\n",
     "\t      ret = ACRAM[aci];\n\t      if(!peek) actrack(aci, 0);\n")
edit('mednafen/hw_misc/arcade_card/arcade_card.cpp', "              ACRAM[aci] = V;\n",
     "              ACRAM[aci] = V;\n              actrack(aci, 1);\n")
edit('mednafen/pce/huc.cpp', '''static DECLFR(AC_PhysRead)
{
	return(arcade_card->PhysRead(A, 0));
}

static DECLFW(AC_PhysWrite)
{
	return(arcade_card->PhysWrite(A, V));
}''', '''extern int dx_trace_on;                                   /* Sapphire M2: the Arcade Card's banks in the trace */
extern "C" void dx_trace_io(int kind, uint32 address, uint8 v);
static DECLFR(AC_PhysRead)
{
	const uint8 v = arcade_card->PhysRead(A, 0);
	if (dx_trace_on) dx_trace_io(14, A, v);
	return(v);
}

static DECLFW(AC_PhysWrite)
{
	if (dx_trace_on) dx_trace_io(15, A, V);
	return(arcade_card->PhysWrite(A, V));
}''')
edit('link.T', 'dumpscsistate;', 'dumpscsistate; actrack_get; acram_copy; acstat_get; acstate_get; rampoke;')
edit('Makefile.common', '$(CORE_DIR)/dumpvid.cpp \\', '$(CORE_DIR)/dumpvid.cpp \\\n\t\t$(CORE_DIR)/sapphire.cpp \\')
print('sapphire_patch: done')
