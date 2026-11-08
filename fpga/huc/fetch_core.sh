#!/bin/bash
# fetch_core.sh [dir]: the MiSTer TurboGrafx16 core (GPL, Gregory Estrade's FPGAPCE; the HuC6280 CPU rewritten
# for cycle accuracy by srg320) at the pinned commit, into reference/tg16 (git-ignored: not this repository's code)
D=${1:-$(dirname "$0")/../../reference/tg16}
C=827d45f45799b8ab3a536510847f2108a92e81bd
[ -d "$D/.git" ] || git clone -q https://github.com/MiSTer-devel/TurboGrafx16_MiSTer.git "$D"
git -C "$D" fetch -q origin "$C" 2>/dev/null; git -C "$D" checkout -q "$C" && echo "tg16 at $C in $D"
