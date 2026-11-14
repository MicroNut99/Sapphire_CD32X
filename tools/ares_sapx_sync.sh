#!/bin/bash
# ares_sapx_sync.sh <ares_android tree> - Sapphire docs/66 (Gate of Thunder's ares_gotx_sync.sh, its docs/37): Sapphire's
# machine (tools/m2host's sapm.c with the Arcade Card, its models, tools/m2_machine.py's generic runtime and interpreter)
# into the ares tree's EverDrive 'sapx' model folder, ares/md/cartridge/board/sapx/. Nothing from the game: the
# interpreter is made from the opcode tables.
# Dracula X's 'dracx' and Gate of Thunder's 'gotx' are in the same binary with models of the same names (vdc_read,
# io_run, ...), so each file is compiled through a wrapper that puts it in namespace sapx (the C standard headers first,
# outside it); sapm.h is included with SAPM_CXX_NS, so its API is in the namespace too (C++ names, no clash with the
# stand-in's huchost.so, which carries a sapm of its own). The namespace is ares::MegaDrive::sapx: board.cpp, which
# declares the API, is compiled inside ares::MegaDrive.
set -e
T=$(readlink -f "$1")/ares/md/cartridge/board/sapx; H=$(dirname "$(readlink -f "$0")"); M=$H/m2host
[ -d "$(dirname "$T")" ] || { echo "not an ares tree: $1"; exit 1; }
mkdir -p "$T"
python3 "$H/m2_machine.py" "$T" > /dev/null
rm -f "$T/blocks.c" "$T/disc_stub.c"
for f in sapm.c sapm.h vdc_model.c vdc_model.h vdc_timing.c io_model.c io_model.h scsi_model.c ac_model.c ac_model.h \
         huc6280_cycles.h huc6280_optab.h; do cp "$M/$f" "$T/$f"; done
cp "$H/m3rt/trackpak.c" "$H/m3rt/trackpak.h" "$T/"   # (scsi_model.c includes the track pack's reader: SCSI_PAK, unused here)
sed -i 's#"../m3rt/trackpak.c"#"trackpak.c"#' "$T/scsi_model.c"
for f in sapm huc_rt interp vdc_model vdc_timing io_model scsi_model ac_model; do
  cat > "$T/sx_$f.cpp" <<W
// sx_$f.cpp - Sapphire docs/66: $f.c in namespace sapx (beside Dracula X's 'dracx' and Gate of Thunder's 'gotx' machines,
// whose files have the same names)
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SAPM_CXX_NS
namespace ares::MegaDrive::sapx {
#include "$f.c"
}
W
done
echo "the machine: $(ls "$T" | wc -l) files in $T"
