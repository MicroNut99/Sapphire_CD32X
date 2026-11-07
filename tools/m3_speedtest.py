#!/usr/bin/env python3
"""m3_speedtest.py - M3: the 32X runtime's first test, the translated game on the SH2 from a trace window (docs/17).

    m3_speedtest.py pack <work> <window dir> <run> <first frame> <frames> <out.bin>
        the window's starting state (m2_trace.py capture: RAM, System Card ROM, the CPU, the video chips, the CD unit,
        ADPCM RAM) and the recording's pad masks from <first frame> on, in one file, big-endian ('DXS1', below): what
        tools/m3rt/rt_pc.c and the SH2 program (dxspeed/) both start from. Game data: the work folder only.

'DXS1' (big-endian, every part 4-byte aligned):
    0 'DXS1'   4 u32 1   8 A X Y S P speed (u8)   14 pc u16   16 MPR[8]   24 s32 vdc_t0   28 s32 io_t0
   32 u32 frames   36 u32 flags (1: ADPCM RAM)   40 u32 offsets: base RAM (8 K), CD RAM (64 K), Super RAM (192 K),
      backup RAM (2 K), System Card ROM (256 K), ADPCM RAM (64 K), VRAM (32 K words), sprite table (256 words),
      colours (512 words), VDC registers (96 u32), CD unit (128 u32), pad masks (u16 a frame); flag 2 (docs/69): the
      drive (parts 12-14); flag 4 (Sapphire docs/08): the Arcade Card's 19 registers and its 2 MB (parts 15, 16);
      flag 8 (docs/09, AC_REGIONS=00,01,...): its registers, a table of 32 regions (offset or ~0), only those 64 KB
      regions' bytes (parts 15-17)
    m3_speedtest.py select <gen dir> <window dir> <out dir>
        the generated code (tools/m2_recomp.py) cut down to the blocks of the banks the window starts with (as
        <traces>/m2gen/trace_check lists them): huc_ops.h, interp.c, huc_rt.c, those blk_*.c and a blocks.c of them
        alone - what dxspeed/ links into LEVEL00.BIN (the whole set is 4.2 MB; one stage's fits the level slot).
    m3_speedtest.py split <work> <dir> <window dir>...
        <dir> (select's output) with each block cut in two (docs/18): the instructions the windows ran (their ins.bin)
        as hot_<key>.c, linked into SDRAM, and the rest as blk_<key>.c, in the cart. A jump from one half to the
        other goes through the dispatcher. hot.c lists the hot halves. The C is made again from <work>'s code map by
        the current translator, so <dir>'s other files must be from it too.
        SPLIT_ASM=1: the merged hot code as SH2 assembly (hot_stage.s, tools/m3_sh2.py, docs/20); SPLIT_MIN=n: hot
        only what the windows ran n times or more.
"""
import os
import re
import shutil
import subprocess
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m2_cycles import INS                                 # noqa: E402


def pack(W, d, run, first, frames, out):
    st = open(os.path.join(d, 'start.bin'), 'rb').read()
    base, cd, sup, bram = st[:8192], st[8192:8192 + 65536], st[8192 + 65536:8192 + 65536 + 196608], st[8192 + 65536 + 196608:][:2048]
    rom = open(os.path.join(d, 'rom.bin'), 'rb').read()[:262144]
    rom += b'\xff' * (262144 - len(rom))
    i0 = np.fromfile(os.path.join(d, 'ins.bin'), dtype=INS, count=1)[0]
    vs = np.fromfile(os.path.join(d, 'vdc_start.bin'), dtype='<u2')
    vram, sat, col = vs[:65536], vs[65536:65536 + 256], vs[65536 + 256:65536 + 256 + 512]
    vregs = np.frombuffer(vs[65536 + 256 + 512:].tobytes(), dtype='<u4')
    if vram[0x8000:].any():
        sys.exit('VRAM above 32 K words is not empty: the runtime keeps 32 K')
    ioregs = np.fromfile(os.path.join(d, 'io_start.bin'), dtype='<u4')
    ap = os.path.join(d, 'adpcm.bin')
    adpcm = open(ap, 'rb').read() if os.path.exists(ap) else b''
    masks = np.load(os.path.join(W, 'inp', run + '.npz'))['masks'][first:first + frames].astype('>u2')
    ts = int(i0['ts'])
    vdc_t0 = struct.unpack('<i', struct.pack('<I', (int(vregs[49]) - ts) & 0xFFFFFFFF))[0]
    io_t0 = struct.unpack('<i', struct.pack('<I', (int(ioregs[18]) - ts) & 0xFFFFFFFF))[0]
    parts = [base, cd, sup, bram, rom, adpcm, vram[:0x8000].astype('>u2').tobytes(), sat.astype('>u2').tobytes(),
             col.astype('>u2').tobytes(), vregs.astype('>u4').tobytes(), ioregs[:128].astype('>u4').tobytes(), masks.tobytes()]
    drive = 0
    cue = os.environ.get('PACK_CUE')                      # (docs/69: the drive's state and the disc's table, for M3b)
    if cue and os.path.exists(os.path.join(d, 'scsi_start.bin')):
        so = open(os.path.join(d, 'scsi_start.bin'), 'rb').read()
        o = np.frombuffer(so[:1024], dtype='<u4')
        parts += [o.astype('>u4').tobytes() + so[1024:1024 + 2048].ljust(2048, b'\0'),
                  np.fromfile(os.path.join(d, 'toc.bin'), dtype='<u4')[:204].astype('>u4').tobytes(), disc_table(cue)]
        drive = 2
    ac = 0                                                # (Sapphire docs/08: the Arcade Card, parts 15 and 16)
    ap2 = os.path.join(d, 'ac_start.bin')
    if os.path.exists(ap2):
        a0 = open(ap2, 'rb').read()
        while len(parts) < 15: parts.append(b'')         # (no drive parts: empty)
        regs = os.environ.get('AC_REGIONS')              # (docs/09: only these 64 KB regions, e.g. 00,01,0a,...: flag 8)
        if regs:
            keep = sorted({int(x, 16) for x in regs.split(',')})
            tab = [0xFFFFFFFF] * 32; body = b''
            for k in keep:
                tab[k] = len(body); body += a0[76 + k * 0x10000:76 + (k + 1) * 0x10000]
            parts += [np.frombuffer(a0[:76], dtype='<u4').astype('>u4').tobytes(), struct.pack('>32I', *tab), body]
            ac = 8
        else:
            parts += [np.frombuffer(a0[:76], dtype='<u4').astype('>u4').tobytes(), a0[76:76 + 0x200000]]
            ac = 4
    head = bytearray(40 + 4 * len(parts))
    head[0:4] = b'DXS1'
    struct.pack_into('>I6BH8BiiII', head, 4, 1, int(i0['a']), int(i0['x']), int(i0['y']), int(i0['s']), int(i0['p']),
                     int(i0['speed']), int(i0['pc']), *[int(x) for x in i0['mpr']], vdc_t0, io_t0, len(masks),
                     (1 if adpcm else 0) | drive | ac)
    body = bytearray(); offs = []
    for p in parts:
        while (len(head) + len(body)) % 4: body.append(0)
        offs.append(len(head) + len(body)); body += p
    struct.pack_into('>%dI' % len(parts), head, 40, *offs)
    open(out, 'wb').write(bytes(head) + bytes(body))
    print('%s: %s frames %d-%d, %d bytes' % (out, run, first, first + len(masks) - 1, len(head) + len(body)))


def disc_table(cue):
    """the disc's tracks as tools/m2host/scsi_model.c's disc_open makes them from the cue sheet: first, last, lead-out,
    then for tracks 0-100 lba, sectors, pregap, control, data, sector size (big-endian i32) - the drive model's table
    on the SH2, which has no cue sheet (docs/69)"""
    d = os.path.dirname(cue); t = 0; first = last = 0; fil = None; firstin = False
    T = {n: dict(lba=0, sectors=0, pregap=0, control=0, data=0, size=0) for n in range(101)}
    for l in open(cue):
        m = re.match(r'\s*FILE "([^"]+)"', l)
        if m: fil = os.path.join(d, m.group(1)); firstin = True; continue
        m = re.match(r'\s*TRACK (\d+) (\S+)', l)
        if m:
            t = int(m.group(1)); first = first or t; last = t
            data = m.group(2) != 'AUDIO'; T[t].update(data=int(data), size=2048 if data else 2352, control=4 if data else 0)
            if firstin and os.path.exists(fil): T[t]['sectors'] = os.path.getsize(fil) // T[t]['size']
            firstin = False; continue
        m = re.match(r'\s*PREGAP (\d+):(\d+):(\d+)', l)
        if m: T[t]['pregap'] = (int(m.group(1)) * 60 + int(m.group(2))) * 75 + int(m.group(3))
    T[first]['pregap'] += 150; lba = -150
    for n in range(first, last + 1):
        lba += T[n]['pregap']; T[n]['lba'] = lba; lba += T[n]['sectors']
    out = struct.pack('>3i', first, last, lba)
    for n in range(101):
        x = T[n]; out += struct.pack('>6i', x['lba'], x['sectors'], x['pregap'], x['control'], x['data'], x['size'])
    return out


def select(gen, d, out):
    r = subprocess.run([os.environ.get('M2CHECK', os.path.join(TRACES, 'm2check')), d, '--run', '0'], capture_output=True, text=True,
                       env=dict(os.environ, RUN_MASKS='/dev/null'))
    keys = {l.split()[3] for l in r.stdout.splitlines() if l.startswith('bank ')}
    also = os.environ.get('SELECT_ALSO')   # (docs/74: rt_pc RT_MATCH_LOG's 'matched <key>' lines over the window - the
    if also:                                #  blocks the window's loads bring in, which the start's RAM does not hold)
        keys |= {l.split()[1] for l in open(also) if l.startswith('matched ')}
    keys = sorted(keys)
    if not keys:
        sys.exit('no blocks listed for ' + d)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith('.c'): os.remove(os.path.join(out, f))
    for f in ('huc_ops.h', 'interp.c', 'huc_rt.c'):
        shutil.copy(os.path.join(gen, f), out)
    for k in keys:
        shutil.copy(os.path.join(gen, 'blk_%s.c' % k), out)
    keep = set(keys)
    lines = []
    for l in open(os.path.join(gen, 'blocks.c')):
        m = re.search(r'(?:blk_|code_|mask_)([0-9A-F]{2}_\d+)\b', l)
        if m and m.group(1) not in keep: continue
        lines.append(l)
    open(os.path.join(out, 'blocks.c'), 'w').write(''.join(lines))
    print('%s: %d blocks (%s)' % (out, len(keys), ' '.join(keys)))


def hot_set(dirs):
    """{(bank, offset)} of every instruction the windows ran (SPLIT_MIN=n: run at least n times in them all)"""
    from collections import Counter
    cnt = Counter()
    for d in dirs:
        a = np.fromfile(os.path.join(d, 'ins.bin'), dtype=INS)
        pc = a['pc'].astype(np.int64)
        bank = a['mpr'][np.arange(len(a)), pc >> 13].astype(np.int64)
        u, n = np.unique(bank << 13 | (pc & 0x1FFF), return_counts=True)
        cnt.update(dict(zip(u.tolist(), n.tolist())))
    # SPLIT_BANKS_FROM=<trace dir>:<bank>[,<bank>...]:<least> (hex banks, docs/75): also that trace's instructions in
    # those banks run at least <least> times - a stage start's hot code from after its loads, and the System Card's read
    # loops (bank 00) from the loads themselves
    bf = os.environ.get('SPLIT_BANKS_FROM'); extra = {}
    if bf:
        d, bl, least = bf.rsplit(':', 2); least = int(least); keep = np.array([int(x, 16) for x in bl.split(',')], dtype=np.int64)
        a = np.fromfile(os.path.join(d, 'ins.bin'), dtype=INS)
        pc = a['pc'].astype(np.int64)
        bank = a['mpr'][np.arange(len(a)), pc >> 13].astype(np.int64)
        m = np.isin(bank, keep)
        u, n = np.unique(bank[m] << 13 | (pc[m] & 0x1FFF), return_counts=True)
        extra = {k: v for k, v in zip(u.tolist(), n.tolist()) if v >= least}
    lo = int(os.environ.get('SPLIT_MIN', '1'))
    hot = {k for k, n in cnt.items() if n >= lo} | set(extra)
    for k, v in extra.items(): cnt[k] += v
    hot_set.counts = {(k >> 13, k & 0x1FFF): n for k, n in cnt.items()}   # (tools/m3_sh2.py COUNTS, docs/32)
    return {(k >> 13, k & 0x1FFF) for k in hot}


def merged_hot(parts, seen=None, bank_of=None):
    """one function for a stage's hot halves (docs/19): each block's code, its labels renamed, its slot's base a
    constant; a jump to another slot's block goes straight there when the runtime allows (c->xok)"""
    L = ["/* hot_stage.c - tools/m3_speedtest.py split: a stage's hot halves in one function (docs/19) */",
         '#define HUC_LOCAL', '#include "huc_ops.h"', 'void hot_stage(Cpu *c)', '{']
    for i, (k, base, labs, code) in enumerate(parts, 1):
        L.append('    static void *const lab_%d[0x2000] = {  /* %s */' % (i, k))
        L.append(',\n'.join(labs)); L.append('    };')
    L.append('    static void *const *const labs[%d] = { 0, %s };' % (len(parts) + 1, ', '.join('lab_%d' % i for i in range(1, len(parts) + 1))))
    L += r"""    uint16_t t; unsigned s_; void *lab_;
    (void)t; (void)s_;
    /* the labels found lately: the tables are in the cart, this in SDRAM (an address and its slot's block) */
    static struct { uint16_t pc; uint8_t id; void *lab; } cache_[512];
#define FIND(a, id_) do { const unsigned h_ = ((a) ^ (id_) << 9) & 511; \
    if (cache_[h_].pc == (a) && cache_[h_].id == (id_)) lab_ = cache_[h_].lab; \
    else { void *const *T_ = labs[id_]; lab_ = T_ ? T_[(a) & 0x1FFF] : 0; cache_[h_].pc = (a); cache_[h_].id = (uint8_t)(id_); cache_[h_].lab = lab_; } } while (0)
    { const uint16_t a0_ = c->pc; const unsigned i0_ = c->hid[a0_ >> 13]; FIND(a0_, i0_); if (!lab_) { c->miss = 3; return; } }
    BLOCK_LOCALS;
/* an address found when it runs, or a jump out of a block (one shared lookup: local_). XLOCAL: JMP (ind) and
   (abs,X), TAM, RTI - on without the runtime only while nothing is due, as its chain would */
#define LOCAL(addr) do { t = (addr); s_ = base >> 13; goto local_; } while (0)
#define XLOCAL(addr) do { t = (addr); s_ = 8; goto local_; } while (0)
#define LEAVE(addr) do { t = (addr); goto leave_; } while (0)
/* a shadow of the calls (docs/19): JSR / BSR push the return address with its label and block; RTS takes the top
   (or the one under it) if it is the address popped, that block is still in its slot, and the slot rule holds */
    static uint16_t rsa_[64]; static uint8_t rsi_[64]; static void *rsl_[64]; static unsigned rsp_;
#define RSPUSH(ra, lab) do { const unsigned p_ = rsp_++ & 63; rsa_[p_] = (ra); rsi_[p_] = BID; rsl_[p_] = (lab); } while (0)
#define RSPOP(r_) do { t = (r_); s_ = base >> 13; goto rts_; } while (0)
/* the return labels by stack slot (SPLIT_RS=k, docs/20): JSR / BSR keep the return address, its block and its label
   at the slot of the address's low byte; RTS takes it if the address popped is that one, the block is still in its
   slot and the slot rule holds. A stale entry is still right: it names the label of that address in that block */
    static struct { uint16_t pc; uint8_t id; void *lab; } rk_[256] = { [0 ... 255] = { 0, 0xFF, 0 } };  /* (no block is 255) */
#define RKPUSH(k_, ra, lb_) do { rk_[k_].pc = (ra); rk_[k_].id = BID; rk_[k_].lab = (lb_); } while (0)
#define RKPOP(k_, r_) do { const unsigned q_ = (r_) >> 13; \
    if (rk_[k_].pc == (r_) && rk_[k_].id == c->hid[q_] && (q_ == (base >> 13) || (c->xok && left_ > 0))) goto *rk_[k_].lab; } while (0)
    goto *lab_;""".split('\n')
    # the hot labels of each slot's blocks: a constant jump to another slot goes straight to the label of the block
    # mapped there, one to this slot that has no label here leaves (it is cold) - what local_ would find
    hot_at = {}
    for i, (k, base, labs, code) in enumerate(parts, 1):
        for l in labs:
            o = int(re.search(r'\[0x([0-9A-F]{4})\]', l).group(1), 16)
            hot_at.setdefault((base >> 13, o), []).append(i)
    def resolve(base, a):
        if a >> 13 == base >> 13: return 'LEAVE(0x%04X)' % a
        js = hot_at.get((a >> 13, a & 0x1FFF), [])
        if not js: return 'LEAVE(0x%04X)' % a
        if os.environ.get('SPLIT_XJ', '0') != '1': return 'LOCAL(0x%04X)' % a
        tests = ' '.join('if (h_ == %d) goto L%d_%04X;' % (j, j, a & 0x1FFF) for j in js)   # (no switch: its table would be in the cart)
        return 'do { if (c->xok && left_ > 0) { const unsigned h_ = c->hid[%d]; %s } t = 0x%04X; goto leave_; } while (0)' % (a >> 13, tests, a)
    part_of = {bank_of[k]: i for i, (k, base, labs, code) in enumerate(parts, 1)} if bank_of else {}
    def chain(i, base, off, var, strict):
        # the targets this RTS / JMP (ind) / RTI went to in the windows, tested before the lookup: a compare each
        # (strict: XLOCAL's rule, the runtime's chain conditions even in this slot)
        tests = []
        for B, A in (seen or {}).get((bank_of[parts[i - 1][0]], off), [])[:6]:
            j = part_of.get(B)
            if not j or (A >> 13) != (parts[j - 1][1] >> 13) or (A & 0x1FFF) not in hot_at_set.get(j, ()): continue
            if (A >> 13) == (base >> 13) and not strict:
                if j == i: tests.append('if (%s == 0x%04X) goto L%d_%04X;' % (var, A, j, A & 0x1FFF))
            else:
                tests.append('if (%s == 0x%04X && c->hid[%d] == %d && c->xok && left_ > 0) goto L%d_%04X;' % (var, A, A >> 13, j, j, A & 0x1FFF))
        return ' '.join(tests)
    hot_at_set = {}
    for (sl, o), js in hot_at.items():
        for j in js: hot_at_set.setdefault(j, set()).add(o)
    def chains(i, base, code):
        out = []; off = None
        for line in code.split('\n'):
            m = re.match(r'^L\d+_([0-9A-F]{4}):', line)
            if m: off = int(m.group(1), 16)
            if off is not None and 'POLL(r); LOCAL(r); }' in line and 'RKPOP' not in line:
                ch = chain(i, base, off, 'r', False)
                if ch: line = line.replace('POLL(r); LOCAL(r); }', 'POLL(r); %s LOCAL(r); }' % ch)
            elif off is not None and 'XLOCAL(' in line:
                m2 = re.search(r'XLOCAL\((.*)\); \}$', line)
                ch = chain(i, base, off, 'x_', True) if m2 else ''
                if ch: line = line[:m2.start()] + '{ const uint16_t x_ = %s; %s XLOCAL(x_); } }' % (m2.group(1), ch)
            out.append(line)
        return '\n'.join(out)
    for i, (k, base, labs, code) in enumerate(parts, 1):
        code = chains(i, base, code)
        code = re.sub(r'\bLOCAL\(0x([0-9A-F]{4})\)', lambda m: resolve(base, int(m.group(1), 16)), code)
        code = re.sub(r'\bLOCAL\(\(uint16_t\)\(base \+ 0x([0-9A-F]{4})\)\)', lambda m: resolve(base, (base + int(m.group(1), 16)) & 0xFFFF), code)
        code = re.sub(r'\bLOCAL\(\(uint16_t\)\(base \+ \((-?\d+)\)\)\)', lambda m: resolve(base, (base + int(m.group(1))) & 0xFFFF), code)
        L += ['/* ---- %s ---- */' % k, '#define base 0x%04X' % base, '#define BID %d' % i, code, '#undef BID', '#undef base']
    L += r"""/* its label if the block mapped in its slot has one - in another slot than the jump's only while the runtime
   would go on to it at once (rt.c rt_run's chain: c->xok) */
rts_:
    { unsigned p_ = (rsp_ - 1) & 63;
      if (rsp_ && (rsa_[p_] == t || (rsp_ > 1 && rsa_[p_ = (rsp_ - 2) & 63] == t && (rsp_--, 1)))) { rsp_--;
          if (c->hid[t >> 13] == rsi_[p_] && ((unsigned)(t >> 13) == s_ || (c->xok && left_ > 0))) goto *rsl_[p_]; } }
local_:
    { const unsigned i_ = c->hid[t >> 13]; FIND(t, i_);
      if (lab_ && ((unsigned)(t >> 13) == s_ || (c->xok && left_ > 0))) goto *lab_; }
leave_:
    SYNC_OUT(); c->pc = t; return;
#undef LOCAL
#undef XLOCAL
#undef LEAVE
#undef FIND
#undef RSPUSH
#undef RSPOP
#undef RKPUSH
#undef RKPOP
}""".split('\n')
    return '\n'.join(L) + '\n'


def trace_targets(dirs, data_of=None):
    """{(bank, offset)} the windows' traces arrived at other than by running on from the instruction before: jump,
    branch, call and return targets, interrupt entries and returns. With data_of (bank -> its 8 KB of code bytes),
    the instruction's own length decides for those banks (docs/21: a jump a few bytes ahead, as a JMP (ind) to the
    next routine, is a target too); else an instruction is taken as at most 7 bytes"""
    from huc6280 import size
    ln = None
    if data_of:
        sz = np.array([size(op) for op in range(256)], dtype=np.int64)
        ln = {B: sz[np.asarray(code, dtype=np.int64)[:0x2000]] for B, code in data_of.items()}
    out = set()
    for d in dirs:
        a = np.fromfile(os.path.join(d, 'ins.bin'), dtype=INS)
        pc = a['pc'].astype(np.int64)
        bank = a['mpr'][np.arange(len(a)), pc >> 13].astype(np.int64)
        step = np.diff(pc)
        jump = (step <= 0) | (step > 7) | (np.diff(bank) != 0)
        if ln:
            for B, L in ln.items():
                k = np.nonzero(bank[:-1] == B)[0]
                jump[k] |= step[k] != L[pc[k] & 0x1FFF]
        jump = np.concatenate([[True], jump])
        if os.environ.get('SPLIT_NORTI', '1') == '1':
            # an interrupt's return goes back to where it was taken: anywhere in the trace (the reference takes it at
            # the instruction), but in the runtime only where the code leaves - a POLL's target or an instruction
            # that leaves, entries already (entry_set). Those arrivals alone make no entry (docs/24)
            rti = np.zeros(len(pc), dtype=bool)
            for B, code in (data_of or {}).items():
                k = np.nonzero(bank[:-1] == B)[0]
                rti[k + 1] = np.asarray(code, dtype=np.int64)[pc[k] & 0x1FFF] == 0x40
            if not data_of:                       # (the opcode from the trace's own bytes is not at hand: all kept)
                rti[:] = False
            key = bank << 13 | (pc & 0x1FFF)
            other = set(key[jump & ~rti].tolist())
            out |= other
        else:
            out |= set((bank[jump] << 13 | (pc[jump] & 0x1FFF)).tolist())
    return {(k >> 13, k & 0x1FFF) for k in out}


def observed_targets(dirs, data_of):
    """{(bank, offset): [(target bank, target address), ...] most frequent first} for the RTS, RTI, JMP (ind) and
    JMP (abs,X) the windows ran: where each one went next (data_of: bank -> its 8 KB of code bytes)"""
    from collections import Counter
    out = {}
    for d in dirs:
        a = np.fromfile(os.path.join(d, 'ins.bin'), dtype=INS)
        pc = a['pc'].astype(np.int64)
        bank = a['mpr'][np.arange(len(a)), pc >> 13].astype(np.int64)
        for B, code in data_of.items():
            idx = np.nonzero(bank[:-1] == B)[0]
            if not len(idx): continue
            op = np.asarray(code)[pc[idx] & 0x1FFF]
            sel = idx[np.isin(op, (0x60, 0x40, 0x6C, 0x7C))]
            for i in sel.tolist():
                out.setdefault((B, int(pc[i]) & 0x1FFF), Counter())[(int(bank[i + 1]), int(pc[i + 1]))] += 1
    return {k: [t for t, n in v.most_common(10)] for k, v in out.items()}


def entry_set(R, b, every, hot, targets):
    """the hot labels code can come to other than by falling through or a jump in the same block: what the traces
    jumped to, returns, the instruction after one that leaves (CLI, PLP, TAM, BRK, a patched opcode), and the first
    hot instruction after cold code"""
    E = {o for o in hot if (b.bank, o) in targets}
    rd = lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0
    for o in every:
        m, mode, ob, n, t = R.decode(rd, o)
        nxt = o + n
        if m in ('JSR', 'BSR', 'CLI', 'PLP', 'TAM', 'BRK', 'SET') or o in b.patched: E.add(nxt)
        if m in R.COND or m in ('BRA', 'BSR') or m[:3] in ('BBR', 'BBS'):
            for tt in t:
                if 0 <= tt < 0x2000: E.add(tt)          # (branch targets: found again after a POLL leaves)
        if m in ('JMP', 'JSR') and mode == 'abs' and t[0] >> 13 == R.SLOT.get(b.bank, t[0] >> 13):
            E.add(t[0] & 0x1FFF)                    # (in this block's slot: a call to another slot's is that block's)
        if o not in hot and nxt in hot and m not in ('JMP', 'RTS', 'RTI', 'BRA', 'BRK'): E.add(nxt)
    for o in hot:
        if o not in E and not any((o - k) in every and R.decode(rd, o - k)[3] == k for k in range(1, 8)):
            E.add(o)                                    # (nothing falls into it)
    return E & hot


def split(W, out, dirs, merge=True):
    import m2_recomp as R
    hot = hot_set(dirs); ne = 0
    keys = sorted(f[4:-2] for f in os.listdir(out) if f.startswith('blk_') and f.endswith('.c'))
    blocks = {b.key: b for b in R.merge_blocks(W)}
    targets = trace_targets(dirs, {blocks[k].bank: blocks[k].data for k in keys if k in blocks})
    R.find_patched(list(blocks.values()))
    for f in os.listdir(out):
        if f.startswith('hot_') and f.endswith('.c'): os.remove(os.path.join(out, f))
    table = []; parts = []; nh = nc = 0
    use_asm = os.environ.get('SPLIT_ASM', '0') == '1'; aparts = []   # (tools/m3_sh2.py: the hot code as SH2 assembly)
    open(os.path.join(out, 'huc_ops.h'), 'w').write(R.RUNTIME_H)      # (the current translator's)
    for f in os.listdir(out):
        if f.startswith('hot_') and f.endswith('.s'): os.remove(os.path.join(out, f))
    for k in keys:
        b = blocks[k]
        b.gen(); every = list(b.labels)
        h = {o for o in every if (b.bank, o) in hot}
        open(os.path.join(out, 'blk_%s.c' % k), 'w').write('#define HUC_LOCAL\n#include "huc_ops.h"\n' + b.gen(keep=set(every) - h))
        nh += len(h); nc += len(every) - len(h)
        if not h: continue
        if merge and b.bank in R.SLOT:
            E = entry_set(R, b, every, h, targets) if os.environ.get('SPLIT_ENTRIES', '1') == '1' else None
            rs = os.environ.get('SPLIT_RS', '0'); rs = 'rk' if rs == 'k' else rs == '1'
            src = b.gen(keep=h, fn='x', miss=3, out='LOCAL', xout='XLOCAL', rs=rs, entries=E)
            ne += len(E) if E is not None else len(h)
            lines = src.split('\n')
            labs = [l.rstrip(',') for l in lines if re.match(r'^        \[0x[0-9A-F]{4}\] = &&l[0-9A-F]{4},?$', l)]
            first = next(j for j, l in enumerate(lines) if re.match(r'^l[0-9A-F]{4}:', l))
            last = next(j for j, l in enumerate(lines) if l == 'leave_:')
            i = len(parts) + 1
            ren = lambda x: re.sub(r'\bl([0-9A-F]{4})\b', 'L%d_\\1' % i, x)
            parts.append((k, R.SLOT[b.bank] << 13, [ren(l) for l in labs], ren('\n'.join(lines[first:last]))))
            table.append((k, 'hot_stage', i, R.SLOT[b.bank]))
            aparts.append((i, k, b, R.SLOT[b.bank] << 13, set(h), set(E if E is not None else h)))
        else:
            open(os.path.join(out, 'hot_%s.c' % k), 'w').write('#define HUC_LOCAL\n#include "huc_ops.h"\n' + b.gen(keep=h, fn='hot_' + k, miss=3))
            table.append((k, 'hot_' + k, 0, 0))
    if parts:
        bank_of = {k: blocks[k].bank for k, base, labs, code in parts}
        seen = observed_targets(dirs, {blocks[k].bank: blocks[k].data for k, base, labs, code in parts}) \
            if os.environ.get('SPLIT_CHAINS', '1') == '1' else None
        if use_asm:
            import m3_sh2
            m3_sh2.COUNTS = getattr(hot_set, 'counts', {})
            open(os.path.join(out, 'hot_stage.s'), 'w').write(m3_sh2.gen(aparts, m3_sh2.cpu_offsets(out), seen))
        else:
            open(os.path.join(out, 'hot_stage.c'), 'w').write(merged_hot(parts, seen, bank_of))
    L = ['/* hot.c - tools/m3_speedtest.py split: the blocks\' hot halves (docs/18, docs/19) */', '#include "huc_ops.h"',
         '#include <stddef.h>', 'typedef struct { const char *key; void (*fn)(Cpu *); uint8_t id, slot; } HotDef;']
    L += ['void %s(Cpu *c);' % f for f in sorted({t[1] for t in table})]
    L += ['const HotDef hot_blocks[] = {'] + ['    { "%s", %s, %d, %d },' % t for t in table] + ['    { NULL, NULL, 0, 0 } };']
    open(os.path.join(out, 'hot.c'), 'w').write('\n'.join(L) + '\n')
    print('%s: %d blocks, %d hot labels (%d found by address), %d cold, %d hot halves merged (hot from %s)' % (
        out, len(keys), nh, ne, nc, len(parts), ' '.join(os.path.basename(d) for d in dirs)))


TRACES = '/home/claude/work'

if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] == 'pack':
        pack(sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]), int(sys.argv[6]), sys.argv[7])
    elif sys.argv[1] == 'select':
        select(sys.argv[2], sys.argv[3], sys.argv[4])
    elif sys.argv[1] == 'split':                         # SPLIT_SEPARATE=1: a function for each hot half (docs/18)
        split(sys.argv[2], sys.argv[3], sys.argv[4:], merge=not os.environ.get('SPLIT_SEPARATE'))
    else:
        sys.exit(__doc__)
