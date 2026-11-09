#!/usr/bin/env python3
"""m3_music.py <cue> <out dir> - Sapphire docs/41: the PC Engine disc's audio tracks 3-16 as the CD32X disc's music
(TrackNN.wav for build.sh's music/, PC Engine track n -> CD32X track n - 1: the CD32X's track 1 is its data). The
raw CD audio (44.1 kHz, 16-bit stereo, little-endian) gets a WAV header. Game data: the work folder only."""
import os
import re
import struct
import sys

cue, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
d = os.path.dirname(cue); fil = None
for l in open(cue):
    m = re.match(r'\s*FILE "([^"]+)"', l)
    if m: fil = os.path.join(d, m.group(1)); continue
    m = re.match(r'\s*TRACK (\d+) AUDIO', l)
    if m and int(m.group(1)) >= 3:
        n = int(m.group(1)); raw = open(fil, 'rb').read()
        hdr = b'RIFF' + struct.pack('<I', 36 + len(raw)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 2, 44100, 44100 * 4, 4, 16) + b'data' + struct.pack('<I', len(raw))
        p = os.path.join(out, 'Track%02d.wav' % (n - 1)); open(p, 'wb').write(hdr + raw)
        print('%s: PC Engine track %d, %.1f s' % (p, n, len(raw) / 176400))
