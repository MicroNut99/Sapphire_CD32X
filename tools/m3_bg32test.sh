#!/bin/bash
# m3_bg32test.sh <gen dir (huc_ops.h: tools/m2_machine.py's)> <dump> [<dump> ...] - Sapphire docs/58: the 32X-layer
# background's fast drawing (tools/m3rt/rt_bg32.c) against the reference (bg32_ref.c) on the PC machine's state dumps,
# in the order given (sapm_pc with RUN_DUMP_EVERY=<n> RUN_DUMP_FILE=<prefix>). SPLIT=<line>: a raster split there; LINES=<line>
# (docs/59): a match on every line from there, some without a BYR write, some with the background off; FRESH=1; SAVE=<prefix>.
set -e
G=${1:?gen dir}; shift; HERE="$(cd "$(dirname "$0")" && pwd)"; R=$HERE/m3rt
gcc -O2 -w -I $R -I $HERE/m2host -I $G -o /tmp/bg32_test $R/bg32_test.c $R/rt_bg32.c $R/bg32_ref.c
/tmp/bg32_test "$@" | tail -12
