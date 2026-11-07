#!/bin/bash
# cd32x/apply.sh <BootCamp checkout> <build dir> - the CD32X build tree for Sapphire: a copy of
# MicroNut99/CD32X-BootCamp (with GENPLANE and VSPLIT in its stack, 1ae4847 or later) and the test ROM satest/.
# Build: GENPLANE=1 VSPLIT=1 GAME=satest DISC_NAME=SATEST bash build.sh   (satest/cd/LEVEL00.BIN from
# tools/m1_testframe.py). The stack is not changed (CLAUDE.md rule 2): check_boot.py checks it.
set -e
BC="$(cd "${1:?usage: apply.sh <BootCamp checkout> <build dir>}" && pwd)"; HERE="$(cd "$(dirname "$0")/.." && pwd)"
W="${2:?usage: apply.sh <BootCamp checkout> <build dir>}"
grep -q "DX-GENPLANE" "$BC/stack/subcpu/main.c" && grep -q "pending count" "$BC/stack/subcpu/hw_md.s" || { echo "*** $BC has no GENPLANE / VSPLIT: update it"; exit 1; }
rm -rf "$W"; mkdir -p "$W"
(cd "$BC" && tar cf - --exclude=./manuals --exclude=./.git --exclude=./disc .) | tar xf - -C "$W"
python3 "$W/tools/check_boot.py"
cp -r "$HERE/satest" "$W/satest"
cp "$BC/hello/splash.png" "$W/satest/" 2>/dev/null || true
echo "==> $W: GENPLANE=1 VSPLIT=1 GAME=satest DISC_NAME=SATEST bash build.sh"
