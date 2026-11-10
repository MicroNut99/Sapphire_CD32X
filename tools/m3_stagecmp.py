#!/usr/bin/env python3
"""m3_stagecmp.py <ref dir> <ares dir> [first ref frame] - Sapphire docs/48: a whole stage in ares against the reference.

<ref dir>: tools/m3_stageref.py's pics.npy (the reference's pictures at half size, one a frame) and snd.s16 (44.1 kHz
stereo). <ares dir>: an ares run's screenshots s<frame>.ppm and out.wav (harness --wav).

Pictures: each screenshot's PC Engine area (256 x 224 at the 32X layer's 4x horizontal scale), halved, against every
reference picture: the closest one (a 32 x 28 mean picture first, then the 12 best at half size, the screen's
place searched), and the share of its pixels more than 40 (of 255) apart in any colour. The run's place in the
reference: ares frame -> reference frame.

Sound: the ares run's audio mapped onto the reference's time through the pictures' places (piecewise linear), then
every 1/30 s the loudness (RMS) of both, and every second the log spectrum (64 bands, 60 Hz-8 kHz) of both:
the loudness envelopes' correlation, the levels, and the spectra's correlation.
Game data stays in the work folder; this prints numbers only."""
import sys, os, glob, re, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m1_aresdiff import ppm

ref, ad = sys.argv[1], sys.argv[2]
F0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0
P = np.load(ref + '/pics.npy', mmap_mode='r')
N, H, Wd = P.shape[:3]
print('reference: %d pictures %dx%d (half size)' % (N, Wd, H))
# thumbnails 32 x 28 of the reference (mean over blocks), in chunks
TH, TW = 28, 32
def thumb(a):                                  # a: (..., h, w, 3) uint8 -> (..., 28, 32, 3) float
    h, w = a.shape[-3], a.shape[-2]
    a = a[..., : h // TH * TH, : w // TW * TW, :].astype(np.float32)
    return a.reshape(a.shape[:-3] + (TH, h // TH, TW, w // TW, 3)).mean((-4, -2))
thumbs = np.concatenate([thumb(np.asarray(P[i:i + 500, 4:116, :])) for i in range(0, N, 500)])

shots = sorted((int(re.findall(r's(\d+)\.ppm', f)[0]), f) for f in glob.glob(ad + '/s*.ppm'))
rows = []
for fr, f in shots:
    im = ppm(f).astype(np.int16)
    best = None
    for x0, ya in [(x, y) for x in range(60, 70, 2) for y in range(0, 13, 2)]:
        pic = im[ya:ya + 224, x0 + 128: x0 + 128 + 1024: 4]
        if pic.shape[:2] != (224, 256): continue
        half = pic[::2, ::2]                                         # 112 x 128
        if half.mean() < 3: best = (1.0, -1, x0, 0); break             # (black: no place)
        t = thumb(half[None, :, :, :].clip(0, 255).astype(np.uint8))[0]
        d = np.abs(thumbs - t[None]).mean((1, 2, 3))
        for k in np.argsort(d)[:12]:
            for dy in range(0, 5):
                r = np.asarray(P[k, dy:dy + 112, :128]).astype(np.int16)
                if r.shape[0] != 112: continue
                w = (np.abs(r - half) > 40).any(2).mean()
                if best is None or w < best[0]: best = (float(w), int(k), x0, dy)
    rows.append((fr, best[1], best[0]))
    print('ares %6d: reference frame %6s, %5.1f%% of pixels apart' % (fr, best[1] + F0 if best[1] >= 0 else '-', best[0] * 100))
ok = [r for r in rows if r[1] >= 0]
w = np.array([r[2] for r in ok])
print('pictures: %d shots placed; %.1f%% apart at the median, %d of them under 2%%, %d under 5%%, the worst %.1f%%' %
      (len(ok), np.median(w) * 100, (w < .02).sum(), (w < .05).sum(), w.max() * 100))
mono = [r for r in ok if r[2] < .05]
if len(mono) > 2:
    a = np.array([r[0] for r in mono], float); b = np.array([r[1] for r in mono], float)
    sl = np.polyfit(a, b, 1)
    print('place: reference frame = %.4f x ares frame %+.0f (pictures a 32X frame); monotonic: %s' %
          (sl[0], sl[1] + F0, bool(np.all(np.diff(b) >= -2))))

# ---- the sound ----
wf = ad + '/out.wav'
if os.path.exists(wf) and len(mono) > 2:
    with wave.open(wf) as g:
        rate, ch, n = g.getframerate(), g.getnchannels(), g.getnframes(); x = np.frombuffer(g.readframes(n), np.int16)
    x = x.reshape(-1, ch).astype(np.float32).mean(1)
    y = np.fromfile(ref + '/snd.s16', np.int16).reshape(-1, 2).astype(np.float32).mean(1)
    yr = 44100
    afps = 59.922743; rfps = 59.826105                             # (the Genesis's NTSC frame rate; the PC Engine's)
    a = np.array([r[0] for r in mono], float); b = np.array([r[1] for r in mono], float)
    # the reference's time for each ares sample, through the pictures' places
    t_a = np.arange(len(x)) / rate
    fa = t_a * afps
    fr_ = np.interp(fa, a, b, left=np.nan, right=np.nan)
    keep = ~np.isnan(fr_)
    xs, ts = x[keep], fr_[keep] / rfps
    step = 1 / 30
    edges = np.arange(ts[0], ts[-1], step)
    ia = np.searchsorted(ts, edges); ir = (edges * yr).astype(int)
    ea, er = [], []
    for i in range(len(edges) - 1):
        s1 = xs[ia[i]:ia[i + 1]]; s2 = y[ir[i]:ir[i + 1]] if ir[i + 1] <= len(y) else np.array([])
        if len(s1) < 10 or len(s2) < 10: continue
        ea.append(np.sqrt((s1 ** 2).mean())); er.append(np.sqrt((s2 ** 2).mean()))
    ea, er = np.array(ea), np.array(er)
    print('sound: %.0f s compared (ares %d Hz, reference 44,100 Hz)' % (len(ea) * step, rate))
    print('  loudness every 1/30 s: correlation %.2f; RMS ares %.0f, reference %.0f' % (np.corrcoef(ea, er)[0, 1], ea.mean(), er.mean()))
    # the spectra, a second at a time
    bands = np.geomspace(60, 8000, 65)
    def spec(s, r):
        if len(s) < r // 4: return None
        m = np.abs(np.fft.rfft(s * np.hanning(len(s)))); fq = np.fft.rfftfreq(len(s), 1 / r)
        return np.log10(np.array([m[(fq >= bands[i]) & (fq < bands[i + 1])].mean() + 1e-3 for i in range(64)]))
    cs = []
    for t0 in np.arange(ts[0], ts[-1] - 1, 1.0):
        i0, i1 = np.searchsorted(ts, [t0, t0 + 1]); s1 = spec(xs[i0:i1], int((i1 - i0)))
        s2 = spec(y[int(t0 * yr): int((t0 + 1) * yr)], yr)
        if s1 is not None and s2 is not None: cs.append(np.corrcoef(s1, s2)[0, 1])
    cs = np.array(cs)
    print('  spectra a second at a time: %d seconds, correlation median %.2f, %d of them over 0.8, the lowest %.2f' %
          (len(cs), np.median(cs), (cs > .8).sum(), cs.min()))

# ---- the effects in the game's time (docs/48): the stand-in's PSG and ADPCM (ares ARES_HUC_WAV=<file>, with <file>.n)
# against the reference's with its CD audio off (m3_stageref.py REF_NOCDDA=1), frame by frame ----
fx = os.environ.get('FX')                    # FX=<ares s16>,<reference dir>
if fx:
    af, rd = fx.split(',')
    xa = np.fromfile(af, np.int16).astype(np.float32); na = np.fromfile(af + '.n', np.uint16).astype(np.int64)
    ya = np.fromfile(rd + '/snd.s16', np.int16).reshape(-1, 2).astype(np.float32).mean(1)
    ca = np.concatenate([[0], np.cumsum(na)]); per = 44100 / 59.826105
    nf = min(len(na), int(len(ya) / per) - 1)
    ea = np.array([np.sqrt((xa[ca[k]:ca[k + 1]] ** 2).mean()) if na[k] else 0 for k in range(nf)])
    er = np.array([np.sqrt((ya[int(k * per):int((k + 1) * per)] ** 2).mean()) for k in range(nf)])
    print('effects (PSG + ADPCM) in the game\'s time: %d frames (%.0f s of the game)' % (nf, nf / 59.826))
    print('  loudness a frame: correlation %.3f; RMS stand-in %.0f, reference %.0f (the stand-in\'s gain: docs/44)' %
          (np.corrcoef(ea, er)[0, 1], ea.mean(), er.mean()))
    for w in (6, 30):
        k = nf // w; A = ea[:k * w].reshape(k, w).mean(1); R = er[:k * w].reshape(k, w).mean(1)
        print('  loudness over %d frames: correlation %.3f' % (w, np.corrcoef(A, R)[0, 1]))
    bands = np.geomspace(60, 8000, 65); cs = []; silent = 0
    for k in range(0, nf - 60, 60):
        s1 = xa[ca[k]:ca[k + 60]]; s2 = ya[int(k * per):int((k + 60) * per)]
        if np.sqrt((s2 ** 2).mean()) < 30 or np.sqrt((s1 ** 2).mean()) < 30: silent += 1; continue
        def sp(s, r):
            m = np.abs(np.fft.rfft(s * np.hanning(len(s)))); fq = np.fft.rfftfreq(len(s), 1 / r)
            return np.log10(np.array([m[(fq >= bands[i]) & (fq < bands[i + 1])].mean() + 1e-3 for i in range(64)]))
        cs.append(np.corrcoef(sp(s1, 22044), sp(s2, 44100))[0, 1])
    cs = np.array(cs)
    print('  spectra a second (60 frames) at a time: %d seconds with sound, correlation median %.2f, %d over 0.8, %d over 0.6; %d quiet' %
          (len(cs), np.median(cs), (cs > .8).sum(), (cs > .6).sum(), silent))
