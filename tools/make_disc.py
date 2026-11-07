#!/usr/bin/env python3
"""make_disc.py - a bootable CUE for Mednafen from the disc's IsoBuster .cue TOC and its two data tracks.

    python3 tools/make_disc.py <game.cue> <Track 02.iso> <Track 17.iso> <work>/disc

The TOC is read from the original cue's "REM MSF ... = LBA" lines (INDEX 00 / INDEX 01 of each track).
The audio tracks are written as silent sparse files of the exact TOC length (the graphics work needs no
music, and the 408 MB image never has to be copied). Data tracks are symlinked, not copied. Each track's
INDEX 00 -> INDEX 01 span becomes a PREGAP, the file running from INDEX 01 to the next track's INDEX 00
(that is how the data track dumps were cut: Track 02.iso is 7,599 sectors = 11189 - 3590).
"""
import os
import re
import sys


def read_toc(cue):
    """[(track, is_data, idx0_lba or None, idx1_lba)]"""
    tracks, cur = [], None
    for line in open(cue, encoding='latin-1'):
        m = re.match(r'\s*TRACK (\d+) (\S+)', line)
        if m:
            cur = [int(m.group(1)), m.group(2) != 'AUDIO', None, None]; tracks.append(cur); continue
        m = re.match(r'\s*INDEX (\d+) ', line)
        if m:
            idx = int(m.group(1)); continue
        m = re.search(r'LBA: (\d+)', line)
        if m and cur is not None:
            cur[2 + idx] = int(m.group(1))
    return tracks


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    cue, iso_a, iso_b, out = sys.argv[1:]
    tracks = read_toc(cue)
    os.makedirs(out, exist_ok=True)
    datafiles = iter([os.path.abspath(iso_a), os.path.abspath(iso_b)])
    lines = []
    for i, (n, data, i0, i1) in enumerate(tracks):
        if i + 1 < len(tracks):
            nx = tracks[i + 1]
            end = nx[2] if nx[2] is not None else nx[3]
        else:
            end = None
        if data:
            src = next(datafiles); name = 't%02d.iso' % n
            if os.path.lexists(os.path.join(out, name)):
                os.remove(os.path.join(out, name))
            os.symlink(src, os.path.join(out, name))
            have = os.path.getsize(src) // 2048
            print('track %02d data  LBA %6d  TOC %5s sectors, file %5d' % (n, i1, end - i1 if end else '?', have))
            lines += ['FILE "%s" BINARY' % name, '  TRACK %02d MODE1/2048' % n]
        else:
            secs = end - i1
            name = 'a%02d.bin' % n
            with open(os.path.join(out, name), 'wb') as f:      # sparse: no disc space used
                f.truncate(secs * 2352)
            print('track %02d audio LBA %6d  %5d sectors (silent)' % (n, i1, secs))
            lines += ['FILE "%s" BINARY' % name, '  TRACK %02d AUDIO' % n]
        if i0 is not None and i1 - i0 > 0:
            lines.append('    PREGAP %02d:%02d:%02d' % ((i1 - i0) // 4500, (i1 - i0) // 75 % 60, (i1 - i0) % 75))
        lines.append('    INDEX 01 00:00:00')
    open(os.path.join(out, 'sapphire.cue'), 'w').write('\n'.join(lines) + '\n')
    print('wrote', os.path.join(out, 'sapphire.cue'))


if __name__ == '__main__':
    main()
