#!/usr/bin/env python3
"""sapphire_patch.py <beetle-pce dir> - Sapphire's additions on top of DraculaX's dumpvid.patch (run after it):
the Arcade Card's port reads and writes call actrack() (sapphire.cpp), and its three exports are linked."""
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
edit('link.T', 'dumpscsistate;', 'dumpscsistate; actrack_get; acram_copy; acstat_get;')
edit('Makefile.common', '$(CORE_DIR)/dumpvid.cpp \\', '$(CORE_DIR)/dumpvid.cpp \\\n\t\t$(CORE_DIR)/sapphire.cpp \\')
print('sapphire_patch: done')
