#!/usr/bin/env python3
"""m1_checkall.py <work> [step] [--reuse] [--modes split,hudspr,hud32x] - M1's finish line on the PC: every <step>th
frame of every stage's play, through tools/m1_testframe.py, each HUD way asked for (D3: the Genesis split by
command 78, `split`; the HUD as Genesis sprites, `hudspr`, the owner's choice; drawn by the 32X, `hud32x`).

The windows are docs/04's, inside play: stage 1 frames from `idle_160` for 3,900, stage 2 from `idle_255`, stage 3
`idle_385`, stage 4 `idle_480`, stage 5 `idle_140` with $0E15 = 4, from frame 1,300 for 3,100. Each sampled frame
is captured as m1_capture.py does (VRAM, sprite table, colours at the first display line; the line log), then
converted. Counted per stage: the re-render against Mednafen's screenshot, the composite against the re-render,
frames the converter refuses (too many tiles, colours, splits), the frames merged to 4 x 15 and their loss, the
largest tile and colour counts. Captures go to <work>/m1/all/ (game data).
"""
import ctypes as C
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from pcehost import Host

HERE = os.path.dirname(os.path.abspath(__file__))
WINDOWS = [('st1', 'idle_160.st', None, 0, 3900), ('st2', 'idle_255.st', None, 0, 3900),
           ('st3', 'idle_385.st', None, 0, 3840), ('st4', 'idle_480.st', None, 0, 3900),
           ('st5', 'idle_140.st', 4, 1300, 3100)]


def capture(W, step):
    h = Host(W + '/core/mednafen_pce_libretro.so', W + '/disc/sapphire.cue', W + '/sys')
    h.run(2); lb = (C.c_uint16 * 1536)(); files = []
    for name, st, poke, skip, n in WINDOWS:
        d = '%s/m1/all/%s' % (W, name); os.makedirs(d, exist_ok=True)
        h.load_state(W + '/states/' + st)
        if poke is not None: h.ram()[0xE15] = poke
        h.run(skip)
        for f in range(n):
            h.run(1)
            if f % step: continue
            h.lib.dumplines(lb); cap = h.capture()
            p = '%s/%05d.npz' % (d, f)
            np.savez_compressed(p, screen=h.frame.copy(), lines=np.frombuffer(bytes(lb), np.uint16).reshape(512, 3).copy(),
                                vram=cap['vram0'], vram_end=cap['vram'], sat=cap['sat0'], pal=cap['pal0'], regs=cap['regs'])
            files.append((name, p))
    return files


def convert(args):
    name, p, hud = args
    out = p[:-4] + '_' + hud
    r = subprocess.run([sys.executable, '-I', HERE + '/m1_testframe.py', p, out] + ([] if hud == 'split' else ['--' + hud]),
                       capture_output=True, text=True)
    t = r.stdout + r.stderr
    g = lambda pat: (lambda m: int(m.group(1)) if m else None)(re.search(pat, t))
    loss = re.search(r'D2 loss: (\d+) of', t)
    rr = re.search(r'; (\d+) of them on sprite', t)
    return dict(name=name, hud=hud, ok=r.returncode == 0, render_spr=int(rr.group(1)) if rr else None,
                vend='VRAM as at the frame end' in t, err=(t.strip().splitlines() or [''])[-1] if r.returncode else '',
                render=g(r're-render vs Mednafen: (\d+)'), comp=g(r'inside the 256 columns\): (\d+)'),
                merged='D2:' in t, loss=int(loss.group(1)) if loss else 0,
                tiles=g(r'bytes: (\d+) Genesis tiles'), c32=g(r'tiles, (\d+) 32X colours'))


def main():
    W = sys.argv[1]; step = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    if '--reuse' in sys.argv:                  # the captures already made: convert only
        import glob
        files = [(n, p) for n, *_ in WINDOWS for p in sorted(glob.glob('%s/m1/all/%s/*.npz' % (W, n)))]
    else:
        files = capture(W, step)
    modes = next((a.split('=', 1)[1] if '=' in a else sys.argv[sys.argv.index(a) + 1]
                  for a in sys.argv if a.startswith('--modes')), 'split,hudspr').split(',')
    jobs = [(n, p, hud) for n, p in files for hud in modes]
    with ThreadPoolExecutor(os.cpu_count() or 2) as ex:
        res = list(ex.map(convert, jobs))
    import json
    json.dump([dict(r, file=j[1]) for r, j in zip(res, jobs)], open(W + '/m1/all/results.json', 'w'))
    # composite: counted on the frames not merged (a merged frame differs by design: its loss is counted)
    for name, *_ in WINDOWS:
        for hud in modes:
            rs = [r for r in res if r['name'] == name and r['hud'] == hud]
            bad = [r for r in rs if not r['ok']]
            good = [r for r in rs if r['ok']]
            print('%s %-12s frames %4d | re-render exact %4d (%d from the frame-end VRAM) | composite exact %4d (of the unmerged) | refused %d%s | merged %d (worst %.1f%% of pixels) | tiles max %d | 32X colours max %d'
                  % (name, {'split': 'HUD split', 'hudspr': 'HUD sprites', 'hud32x': 'HUD on 32X'}[hud], len(rs), sum(r['render'] == 0 for r in good),
                     sum(r['render'] == 0 and r['vend'] for r in good),
                     sum(r['comp'] == 0 for r in good if not r['merged']), len(bad), (' (%s)' % bad[0]['err'][:70]) if bad else '',
                     sum(r['merged'] for r in good), 100 * max([r['loss'] for r in good] or [0]) / 57344,
                     max([r['tiles'] or 0 for r in good] or [0]), max([r['c32'] or 0 for r in good] or [0])))


if __name__ == '__main__':
    main()
