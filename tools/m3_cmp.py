#!/usr/bin/env python3
"""m3_cmp.py <rt_pc output> <m2check --run output (RUN_IDLE_HASH=1)> - M3: the 32X runtime on the PC against M2's exact
host (docs/08). Two comparisons:
- every frame's end: the state's hash without the stack page (the frame's edge may fall in an interrupt on one side:
  the stack page then differs and nothing else);
- every wait the two report at the same place in the same frame (the game's wait loops: rt_pc reports the waits its
  translation leaves at, the exact host the first after each interrupt): the same hash."""
import re
import sys


def read(f):
    t = open(f).read()
    fr = {int(k): h for k, h in re.findall(r'^frame (-?\d+) hash [0-9a-f]+ ([0-9a-f]+)$', t, re.M)}
    idle = {}
    for n, f_, h, at in re.findall(r'^idle (\d+) frame (\d+) ([0-9a-f]+) at ([0-9A-F]+:[0-9A-F]+)$', t, re.M):
        idle.setdefault((int(f_), at), []).append(h)
    return fr, idle


a, ai = read(sys.argv[1]); b, bi = read(sys.argv[2])
ks = sorted(set(a) & set(b)); bad = [k for k in ks if a[k] != b[k]]
print('frame ends compared %d, differ %d%s' % (len(ks), len(bad), (', first frame %d' % bad[0]) if bad else ''))
common = sorted(set(ai) & set(bi)); wb = [k for k in common if ai[k][0] != bi[k][0]]
print('waits at the same place and frame compared %d, differ %d%s' % (len(common), len(wb), (', first %s' % (wb[0],)) if wb else ''))
sys.exit(1 if bad or wb else 0)
