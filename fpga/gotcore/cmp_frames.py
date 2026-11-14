#!/usr/bin/env python3
"""cmp_frames.py <host output> <bench output> - docs/64: the frames equal, with and without the stack page (md5 / md5ns lines)."""
import sys
def rd(p, kind):
    return {int(l.split()[1]): l.split()[-1] for l in open(p) if l.startswith('frame ') and l.split()[2] == kind}
for kind in ('md5', 'md5ns'):
    a, b = rd(sys.argv[1], kind), rd(sys.argv[2], kind)
    ks = sorted(set(a) & set(b))
    eq = [k for k in ks if a[k] == b[k]]
    bad = [k for k in ks if a[k] != b[k]]
    print('%-5s %d of %d frames equal%s' % (kind, len(eq), len(ks), ('; differ: ' + ' '.join(map(str, bad[:12])) + (' ...' if len(bad) > 12 else '')) if bad else ''))
