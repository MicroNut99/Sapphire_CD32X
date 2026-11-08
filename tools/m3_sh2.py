#!/usr/bin/env python3
"""m3_sh2.py - M3: a stage's hot code as SH2 assembly (docs/20), in place of GCC's output of the merged C.

The C translation (tools/m2_recomp.py, tools/m3_speedtest.py merged_hot) stays the reference: this backend makes the
same decisions - which label a jump goes to, when the code leaves for the runtime, the cycles at every point the
runtime can see them - with the HuC6280's state in fixed SH2 registers:

    GBR  the Cpu struct (every field one instruction away, into R0)
    r14  base RAM + 128 (the zero page at -128..127: mov #imm reaches it), r5 base RAM + 0x100 (the stack page)
    r6   S        r7  the cycles left (c->left)
    r8   A        r9  X        r10 Y        (bytes, zero-extended; A sign- or zero-extended, docs/27)
    r11  Z: set when r11 is 0      r12  N: bit 7 of r12      r13  C (0 or 1)
    c->vf  V in bit 7;  c->P  I, D, B, T (its N, V, Z, C bits are stale inside)
    r0-r4  scratch (the shared subroutines keep r1-r3)

Z and N are lazy: an instruction leaves them in the register it set (A, X, Y) and they move to r11 / r12 only where
something reads them (flag_need: a jump settles only what its target reads before setting). V is computed only
where something may read it (v_live_out). Cycles are counted per straight run and settled where the runtime can see
them. The VDC's data port and the VCE's ports run inline or in shared subroutines while no VDC event is due (as
huc_ops.h's hw_wr_fast / hw_rd_fast); the register selected is tracked through the block (sel_in). TAM is inline
(rt.c set_mpr). A return or indirect jump tests the targets the windows saw it take (chain), then the shared lookup
(an entries' hash table). What it does not translate (block moves, BRK, SET's T mode, self-modified operands that
are not jumps) runs through the interpreter (interp_one) between synced states, as the C's slow paths do.

    import m3_sh2; asm = m3_sh2.gen(parts, m3_sh2.cpu_offsets(gen_dir), seen)   (m3_speedtest.py split, SPLIT_ASM=1)
"""
import re


# ---------------------------------------------------------------------------------------------------------------------
# the layout: SH2 instructions, labels, literals; branch forms and literal pools placed to fit their ranges
# ---------------------------------------------------------------------------------------------------------------------
class Asm:
    """A list of items: ('i', text) an instruction; ('l', name) a label; ('ld', size, reg, value) a literal load
    (mov.w / mov.l @(disp,PC)); ('b', op, target) a branch (bt bf bra: made long when out of range; 'bra' and 'jmp'
    forms take their delay slot from the next item, which must be an ('i')); ('u',) an unconditional end (a pool may
    follow); ('raw', text)."""

    def __init__(self):
        self.items = []
        self.r0v = None                       # the constant r0 holds here (Gen.r0k), None: not known

    def i(self, *texts):
        for t in texts:
            self.items.append(('i', t))
            a = t.partition('\t')[2]
            if a.rsplit(',', 1)[-1].strip() == 'r0': self.r0v = None

    def l(self, name):
        self.items.append(('l', name)); self.r0v = None

    def ld(self, size, reg, value):          # value: an int or a symbol expression
        self.items.append(('ld', size, reg, value))
        if reg == 'r0': self.r0v = None

    def b(self, op, target, likely=False):   # op: bt bf bra (bra: its delay slot is the next item); likely: taken
        self.items.append(('b', op, target, likely) if likely else ('b', op, target))   # most times (docs/34)

    def u(self):
        self.items.append(('u',))

    def raw(self, text):
        self.items.append(('raw', text))

    def d(self, text, slot):                 # an instruction with a delay slot (rts jmp jsr bsr bt/s bf/s braf)
        self.items.append(('d', text, slot)); self.r0v = None


TRAMP_GBR = {}                                # the trampolines' targets in c->hsub (Gen.run sets the offsets)


def layout(items, name='hot', refs=None):
    """the assembly text: branch forms chosen (short, or long through r0 and a literal), literal pools placed after
    unconditional ends, or with a branch around them, so every displacement fits. Iterates to a fixed point.
    refs: text outside the items (the tables) whose labels stay"""
    def hoist(items):                         # ('n', items): after the next unconditional end (docs/31)
        out = []; nr = []
        for it in items:
            if it[0] == 'n': nr.extend(hoist(it[1])); continue
            out.append(it)
            if it[0] == 'u' and nr: out.extend(nr); nr = []
        return out + nr
    items = hoist(items)
    if DROP_LABELS and refs is not None:      # (a game instruction's label nothing names: no barrier, docs/43)
        items = drop_labels(items, refs)
    if LOAD_SCHED:                            # (load-use pairs broken, docs/32)
        import sh2sched; items = list(items); sh2sched.schedule(items)
    if FILL_SLOTS:                            # (delayed branches' nop slots filled, docs/35)
        import sh2sched; items = list(items); sh2sched.fill_slots(items)
    long_b = set()                            # indexes of branches made long
    for _ in range(40):
        out, addr_of, size_ok = _place(items, long_b)
        bad = set()
        for k, (pc, tgt, short) in size_ok.items():
            if tgt not in addr_of: raise KeyError('no label %s' % tgt)
            d = addr_of[tgt] - (pc + 4)
            if short:
                if not (-256 <= d <= 254): bad.add(k)
            elif not (-4096 <= d <= 4094): bad.add(k)
        if not bad:
            if SLOT_DUP:                      # (a bra's nop slot from its target, docs/41)
                import sh2sched; out = sh2sched.fill_from_target(out)[0]
            return out
        long_b |= bad                           # (k: short -> island; ('i', k): island -> medium; ('m', k): -> long)
    raise RuntimeError('layout does not settle')


def drop_labels(items, refs):
    """the items without the game instructions' labels (L<block>_<offset>) that no item and no table names: a label
    ends the schedulers' straight runs (docs/32, docs/35), and most are there only for the instruction's address"""
    import re
    pat = re.compile(r'\bL\d+_[0-9A-F]{4}\b'); named = set(pat.findall(refs))
    def scan(its):
        for it in its:
            if it[0] == 'n': scan(it[1]); continue
            if it[0] == 'l': continue
            for x in it[1:]:
                if isinstance(x, str): named.update(pat.findall(x))
                elif isinstance(x, (list, tuple)): scan([y for y in x if isinstance(y, tuple)])
    scan(items)
    def keep(its):
        out = []
        for it in its:
            if it[0] == 'n': out.append(('n', keep(it[1]))); continue
            if it[0] == 'l' and re.fullmatch(r'L\d+_[0-9A-F]{4}', it[1]) and it[1] not in named: continue
            out.append(it)
        return out
    return keep(items)


LIT_W, LIT_L = 480, 960                       # how far ahead a mov.w / mov.l may be from its literal (margins kept)
MARKS = int(__import__('os').environ.get('SPLIT_MARKS', '0'))   # D6b's marks (docs/54): the hooks and each VRAM write's tag
CDSTEP = int(__import__('os').environ.get('SPLIT_CDSTEP', '0'))   # docs/75: the System Card's CD_READ byte loop in one go
TRAMP = {'.Lleave': int(__import__('os').environ.get('SPLIT_TRAMP_LEAVE', '200')), '.Llocal': 3000}
                                              # the shared exits: jumps to them go through a trampoline near by
                                              # (bt / bf reach 256 bytes, bra 4 KB: one every that many bytes; past
                                              # that a bt / bf goes through an island of its own, docs/28)


def _place(items, long_b):
    """one pass: (text lines, label -> address, {item index: (address, target)} for short branches)"""
    lines = []; addr = 0; addr_of = {}; checks = {}
    pend = []                                  # literals waiting: [label, size, value, address of the first use]
    nlit = [0]; nsk = [0]

    def lit(size, value):
        for p in pend:
            if p[1] == size and p[2] == value: return p[0]
        nlit[0] += 1; lab = '.Lk%d' % nlit[0]
        pend.append([lab, size, value, addr]); return lab

    def pool(after_uncond):
        nonlocal addr
        if not pend: return
        if not after_uncond:                   # a branch around it
            nsk[0] += 1; sk = '.Lps%d' % nsk[0]
            lines.append('\tbra\t%s' % sk); lines.append('\tnop'); addr += 4
        ws = [p for p in pend if p[1] == 2]; ls = [p for p in pend if p[1] == 4]
        for p in ws:                            # (the words first: their range is half the longs')
            lines.append('%s:\t.short\t%s' % (p[0], fmt(p[2] & 0xFFFF if isinstance(p[2], int) else p[2]))); addr += 2
        if ls and addr % 4: lines.append('\t.align 2'); addr += 2
        for p in ls:
            lines.append('%s:\t.long\t%s' % (p[0], fmt(p[2]))); addr += 4
        pend.clear()
        if not after_uncond:
            lines.append('%s:' % sk)

    isl = []                                    # islands waiting (docs/21): [label, target, the first branch's address]

    def islands(after_uncond):                  # a conditional branch's far target: bt / bf to a bra near by, so the
        nonlocal addr                           # path that falls through takes no branch
        if not isl: return
        if not after_uncond:
            nsk[0] += 1; sk = '.Lps%d' % nsk[0]
            lines.append('\tbra\t%s' % sk); lines.append('\tnop'); addr += 4
        for lab, tgt, _ in isl:
            lines.append('%s:' % lab); addr_of[lab] = addr
            if ('j', lab) in long_b:
                lines.extend(['\tmov.l\t%s,r0' % lit(4, tgt), '\tjmp\t@r0', '\tnop']); addr += 6
            else:
                checks[('j', lab)] = (addr, tgt, False); lines.extend(['\tbra\t%s' % tgt, '\tnop']); addr += 4
        isl.clear()
        if not after_uncond: lines.append('%s:' % sk)

    tramps = {t_: [] for t_ in TRAMP}           # trampolines to the shared exits: [(label, address)]
    last_tr = {t_: -10000 for t_ in TRAMP}

    def near(tgt, lim):                         # a trampoline to tgt behind us, within lim bytes
        for lab, a in reversed(tramps.get(tgt, ())):
            if addr + 4 - a <= lim: return lab
            break
        return None

    def due():                                  # a literal close to its range
        for p in pend:
            lim = LIT_W if p[1] == 2 else LIT_L
            if addr - p[3] > lim - 40: return True
        return False

    k = 0; n = len(items)
    while k < n:
        it = items[k]; t = it[0]
        if t == 'l':
            lines.append('%s:' % it[1]); addr_of[it[1]] = addr
        elif t == 'i':
            lines.append('\t' + it[1]); addr += 2
        elif t == 'raw':
            lines.append(it[1])
        elif t == 'd':
            lines.append('\t' + it[1]); lines.append('\t' + it[2]); addr += 4
        elif t == 'ld':
            size, reg, value = it[1], it[2], it[3]
            lab = lit(size, value)
            lines.append('\tmov.%s\t%s,%s' % ('w' if size == 2 else 'l', lab, reg)); addr += 2
        elif t == 'b':
            op, tgt = it[1], it[2]
            if tgt in TRAMP:
                t2 = near(tgt, 240 if op in ('bt', 'bf') else 4000)
                if t2: tgt = t2
            slot = None
            if op in ('bt/s', 'bf/s'):              # its delay slot: the next item (run on both paths); far: the slot
                slot = items[k + 1]; assert slot[0] == 'i', 'a branch delay slot must be an instruction'
                if k not in long_b:                 # first, then bt / bf as below
                    checks[k] = (addr, tgt, True); lines += ['\t%s\t%s' % (op, tgt), '\t' + slot[1]]; addr += 4
                    op = None
                else:
                    lines.append('\t' + slot[1]); addr += 2; op = op[:2]
            if op is None:
                k += 1
            elif op in ('bt', 'bf'):
                likely = len(it) > 3 and it[3] and LIKELY_MED   # (taken most times: over a bra, not an island)
                if k in long_b and ('i', k) not in long_b and not likely:   # bt / bf to an island
                    lab = '.Lis%d' % k
                    isl.append([lab, tgt, addr]); checks[('i', k)] = (addr, lab, True)
                    lines.append('\t%s\t%s' % (op, lab)); addr += 2
                elif k in long_b and ('m', k) not in long_b:   # bf / bt over a bra
                    nsk[0] += 1; sk = '.Lbs%d' % nsk[0]
                    checks[('m', k)] = (addr + 2, tgt, False)
                    lines += ['\t%s\t%s' % ('bf' if op == 'bt' else 'bt', sk), '\tbra\t%s' % tgt, '\tnop', '%s:' % sk]
                    addr += 6
                elif k in long_b:               # bf / bt over a long jump
                    nsk[0] += 1; sk = '.Lbs%d' % nsk[0]
                    lab = lit(4, tgt)
                    lines += ['\t%s\t%s' % ('bf' if op == 'bt' else 'bt', sk), '\tmov.l\t%s,r0' % lab, '\tjmp\t@r0', '\tnop',
                              '%s:' % sk]
                    addr += 8
                else:
                    checks[k] = (addr, tgt, True); lines.append('\t%s\t%s' % (op, tgt)); addr += 2
                if slot: k += 1
            elif op == 'bra':                   # its delay slot: the next item
                it2 = items[k + 1]; assert it2[0] == 'i', 'a branch delay slot must be an instruction'
                st2 = it2[1]
                if k in long_b:
                    lab = lit(4, tgt)
                    if st2 != 'nop' and _r0_in(st2):   # (a slot that uses r0 runs before the jump's r0, docs/35)
                        lines.append('\t' + st2); addr += 2; st2 = 'nop'
                    lines += ['\tmov.l\t%s,r0' % lab, '\tjmp\t@r0']; addr += 4
                else:
                    checks[k] = (addr, tgt, False); lines.append('\tbra\t%s' % tgt); addr += 2
                k += 1
                lines.append('\t' + st2); addr += 2
                if pend and k + 1 < n and items[k + 1][0] == 'u': pass
            else:
                raise ValueError(op)
        elif t == 'u':
            islands(True); pool(True)
            for tg, gap in TRAMP.items():        # (after an end: nothing runs into them)
                if addr - last_tr[tg] > gap:
                    nsk[0] += 1; lab = '.Ltr%d' % nsk[0]
                    lines.append('%s:' % lab); tramps[tg].append((lab, addr)); addr_of[lab] = addr
                    if tg in TRAMP_GBR: lines += ['\tmov.l\t@(%d,gbr),r0' % TRAMP_GBR[tg], '\tjmp\t@r0', '\tnop']
                    else: lines += ['\tmov.l\t%s,r0' % lit(4, tg), '\tjmp\t@r0', '\tnop']
                    addr += 6
                    pool(True); last_tr[tg] = addr
        k += 1
        if isl and addr - isl[0][2] > 200: islands(False)
        if pend and due(): pool(False)
    islands(True); pool(True)
    return lines, addr_of, checks


def _r0_in(text):
    import sh2sched; r, w = sh2sched.info(('i', text))[:2]
    return 'r0' in r or 'r0' in w or 'r0' in text


def fmt(v):
    return ('0x%X' % (v & 0xFFFFFFFF)) if isinstance(v, int) else v


# ---------------------------------------------------------------------------------------------------------------------
# the translation
# ---------------------------------------------------------------------------------------------------------------------
# what the backend translates; anything else stays in the C cold half (exact, slower)
_RW_MODES = {'zp', 'zpx', 'zpy', 'abs', 'abx', 'aby', 'izp', 'izx', 'izy'}
_SIMPLE = {'TAX', 'TXA', 'TAY', 'TYA', 'TSX', 'TXS', 'INX', 'DEX', 'INY', 'DEY', 'SAX', 'SAY', 'SXY', 'CLA', 'CLX',
           'CLY', 'CLC', 'SEC', 'CLD', 'SED', 'CLV', 'CLI', 'SEI', 'PHA', 'PHX', 'PHY', 'PHP', 'PLA', 'PLX', 'PLY', 'PLP',
           'NOP', 'ST0', 'ST1', 'ST2', 'TMA', '???'}   # (CSL / CSH: the cold half's rt_speed, docs/50)
_BR = {'BPL', 'BMI', 'BVC', 'BVS', 'BCC', 'BCS', 'BNE', 'BEQ', 'BRA'}
ALL = frozenset('ZN')
_ZNW = {'LDA', 'LDX', 'LDY', 'AND', 'ORA', 'EOR', 'ADC', 'SBC', 'CMP', 'CPX', 'CPY', 'BIT', 'TST', 'ASL', 'LSR', 'ROL',
        'ROR', 'INC', 'DEC', 'TSB', 'TRB', 'TAX', 'TXA', 'TAY', 'TYA', 'TSX', 'INX', 'DEX', 'INY', 'DEY', 'PLA', 'PLX',
        'PLY', 'PLP', 'RTI'}


def supported(m, mode, b, off):
    if off in b.dynops or off in b.patched: return m in ('RTS', 'RTI')   # (one byte: translated as they are)
    if m in _SIMPLE: return True
    if m in ('LDA', 'LDX', 'LDY', 'AND', 'ORA', 'EOR', 'ADC', 'SBC', 'CMP', 'CPX', 'CPY', 'BIT'):
        return mode in _RW_MODES or mode == 'imm'
    if m in ('STA', 'STX', 'STY', 'STZ'): return mode in _RW_MODES
    if m in ('ASL', 'LSR', 'ROL', 'ROR', 'INC', 'DEC'): return mode == 'acc' or mode in ('zp', 'zpx', 'abs', 'abx')
    if m in ('TSB', 'TRB'): return mode in ('zp', 'abs')
    if m == 'TST': return mode in ('imzp', 'imzx', 'imab', 'imax')
    if m[:3] in ('RMB', 'SMB') or m[:3] in ('BBR', 'BBS'): return True
    if m in _BR: return True
    if m in ('JMP', 'JSR', 'BSR', 'RTS', 'RTI', 'TAM'): return True
    return False


class Gen:
    """parts: [(id, key, Block, base, H: the offsets translated, E: the entries)]; off: the Cpu struct's offsets;
    seen: {(bank, offset): [(bank, address)...]} the targets the windows saw an indirect jump take (chains)"""

    def __init__(self, parts, off, seen=None, cycles=None):
        import m2_recomp as R
        self.R = R; self.CYC = R.CYCLES
        self.parts = parts; self.o = off; self.seen = seen or {}
        self.a = Asm(); self.k = 0
        self.ent_at = {}                          # (slot, offset) -> the blocks with that entry
        self.bank_id = {}; self.base_of = {}; self.ent_of = {}
        for (i, key, b, base, H, E) in parts:
            self.bank_id[b.bank] = i; self.base_of[i] = base; self.ent_of[i] = set(E)
            for o in E: self.ent_at.setdefault((base >> 13, o), []).append(i)
        self.ooL = []                             # out-of-line code: (label, items) emitted after the next end
        self.blk = {i: (b, base, H) for (i, key, b, base, H, E) in parts}
        self.az = False                           # A (r8) known zero-extended here (else either form: docs/27)
        self.kw = {}; self.cur_w = 0              # the RAM offsets loaded into r0 and their runs (docs/35)
        self.kpos = {}; self.kseq = 0             # (and where in the code: the table in that order, a line a part)
        self.ck = None; self.Tset = set(); self.pair_at = None; self.pair_next = None; self.cold_branch = None; self.cold_state = None         # C known for the next ADC / SBC (docs/30); the block's targets

    # -- small helpers --
    def u(self):
        self.k += 1; return self.k

    def i(self, *t): self.a.i(*t)

    def cy(self, n): self.pend += n

    def flush(self):
        while self.pend > 0:
            k = min(self.pend, 128); self.i('add\t#%d,r7' % -k); self.pend -= k
        while self.pend < 0:                      # (a delayed branch's path not taken: 2 counted ahead, docs/28)
            k = min(-self.pend, 127); self.i('add\t#%d,r7' % k); self.pend += k

    def adj(self, p):                             # r7 -= p (a slow path's pending cycles)
        while p > 0: k = min(p, 128); self.i('add\t#%d,r7' % -k); p -= k
        while p < 0: k = min(-p, 127); self.i('add\t#%d,r7' % k); p += k

    def cyc_call(self, sub, p):                   # a memory access's slow path: its pending cycles settled for the
        self.adj(p); self.call(sub); self.adj(-p)  # runtime and taken back after, as the fast path has them (docs/28)

    def hw_access(self, mode, ob):
        """the access's fast path may reach the hardware page (a constant address in slot 0: the ports, inline): the
        cycles settled before it. Memory elsewhere settles them only in its slow path (cyc_call)"""
        k = self.kind(mode, ob)
        if k == 'ram': return False
        if k == 'const': return not SLOW_CYC or (ob[0] | ob[1] << 8) >> 13 == 0
        return not SLOW_CYC

    def mat(self):
        if self.ns != 'r12': self.i('mov\t%s,r12' % self.ns)
        if self.zs != 'r11': self.i('mov\t%s,r11' % self.zs)
        self.zs, self.ns = 'r11', 'r12'

    def zn(self, reg):                            # Z and N from reg's value (u8)
        if reg in ('r8', 'r9', 'r10', 'r11'): self.zs = self.ns = reg
        else: self.i('mov\t%s,r11' % reg); self.zs = self.ns = 'r11'

    def keep(self, reg):                          # reg (A, X or Y) changes without the flags: they leave it first
        if reg in (self.zs, self.ns): self.mat()

    def imm(self, reg, v, u8=True):               # a constant into reg (zero-extended if u8)
        v &= 0xFFFF if not u8 else 0xFF
        if -128 <= v <= 127: self.i('mov\t#%d,%s' % (v, reg))
        elif u8: self.i('mov\t#%d,%s' % (v - 256, reg), 'extu.b\t%s,%s' % (reg, reg))
        elif v <= 0x7FFF: self.a.ld(2, reg, v)
        else: self.a.ld(2, reg, v - 0x10000); self.i('extu.w\t%s,%s' % (reg, reg))

    def imm_sx(self, reg, v):                     # a 16-bit constant into reg, sign-extended (an extu.w follows)
        v &= 0xFFFF
        if v >= 0x8000: v -= 0x10000
        if -128 <= v <= 127: self.i('mov\t#%d,%s' % (v, reg))
        else: self.a.ld(2, reg, v)

    def r0k(self, v):                             # r0 = a signed constant (RAM offsets); not again while r0 holds it
        if R0_CACHE and self.a.r0v == v: return   # (straight code: Asm forgets it at a label, a write of r0, a call)
        if -128 <= v <= 127: self.i('mov\t#%d,r0' % v)
        elif KOFF_N:                              # (a literal, or the GBR table's word: chosen at the end, docs/35)
            self.a.items.append(('ld', 2, 'r0', v, 'k')); self.kw[v] = self.kw.get(v, 0) + self.cur_w
            self.kseq += 1; self.kpos[v] = self.kpos.get(v, 0) + self.cur_w * self.kseq
        else: self.a.ld(2, 'r0', v)
        self.a.r0v = v

    def bra(self, target, slot='nop'):
        self.a.b('bra', target); self.i(slot)

    def leave(self, pc, idle=False):              # out to the runtime at pc (a constant)
        self.flush(); self.mat()
        if idle: self.i('mov\t#1,r0', 'mov.b\tr0,@(%d,gbr)' % self.o['idle'])
        self.imm('r1', pc, u8=False)
        self.bra('.Lleave')
        self.a.u()

    def vmark(self, m, t):                        # D6b (docs/54): the unit of MAWR in m gets the tag; r0, t taken
        if not MARKS or __import__('os').environ.get('SPLIT_MARKS_NOWRITE'): return
        o = self.o
        self.i('mov.l\t@(%d,gbr),r0' % (o['hsub'] + 4 * SUBS.index('_rt_marks')), 'mov\tr0,%s' % t,   # (no literal: docs/55)
               'mov\t%s,r0' % m, 'shlr2\tr0', 'shlr2\tr0', 'add\tr0,r0', 'add\t#4,r0',     # (rt_marks: tag, pad, v[])
               'add\t%s,r0' % t, 'mov.w\t@%s,%s' % (t, t), 'mov.w\t%s,@r0' % t)
        self.a.r0v = None

    def ool(self, lab, fn):                       # out-of-line code (a slow path): emitted after the next end
        self.ooL.append((lab, fn))

    def near(self, fn):                           # out-of-line code placed after the next end (the layout's 'n')
        sv = self.a.items; self.a.items = []
        fn(); self.a.u()
        its = self.a.items; self.a.items = sv; self.a.r0v = None
        self.a.items.append(('n', its))

    def call(self, sub):                          # a shared subroutine (PR is ours: saved at entry; r4 its address)
        if sub in SUBS:                           # (its address from c->hsub: no subroutine takes r0)
            self.i('mov.l\t@(%d,gbr),r0' % (self.o['hsub'] + 4 * SUBS.index(sub))); self.a.d('jsr\t@r0', 'nop')
        else:
            self.a.ld(4, 'r4', sub); self.a.d('jsr\t@r4', 'nop')

    # -- operands: the value into dst (zero-extended); ea in r1 for the pointer modes --
    def ram_off(self, a):                         # base RAM ($2000-$3FFF) offset from r14
        return (a & 0x1FFF) - 128

    def ea_of(self, mode, ob):
        """for modes that need an address: ('ram', d, index reg or None) | ('const', a) | ('ptr',) (ea in r1)"""
        if mode in ('abs', 'abx', 'aby'):
            a = ob[0] | ob[1] << 8
            ix = {'abs': None, 'abx': 'r9', 'aby': 'r10'}[mode]
            if a >> 13 == 1 and (ix is None or (a + 0xFF) >> 13 == 1): return ('ram', self.ram_off(a), ix)
            if ix is None: return ('const', a)
            if a >> 13 and (a + 0xFF) >> 13 == a >> 13: return ('slotx', a, ix)   # (indexed, inside one slot)
            self.imm_sx('r1', a); self.i('add\t%s,r1' % ix, 'extu.w\tr1,r1'); return ('ptr',)
        if mode == 'zp': return ('ram', ob[0] - 128, None)
        if mode in ('zpx', 'zpy'):
            ix = 'r9' if mode == 'zpx' else 'r10'
            self.i('mov\t%s,r0' % ix, 'add\t#%d,r0' % (ob[0] - 256 if ob[0] > 127 else ob[0]), 'extu.b\tr0,r0', 'add\t#-128,r0')
            return ('zpr',)
        if mode in ('izp', 'izx', 'izy'):
            if mode == 'izx':
                self.i('mov\tr9,r0', 'add\t#%d,r0' % (ob[0] - 256 if ob[0] > 127 else ob[0]), 'extu.b\tr0,r2',
                       'mov\tr2,r0', 'add\t#-128,r0', 'mov.b\t@(r0,r14),r1', 'mov\tr2,r0', 'add\t#1,r0', 'extu.b\tr0,r0',
                       'add\t#-128,r0', 'mov.b\t@(r0,r14),r0', 'extu.b\tr1,r1', 'shll8\tr0', 'extu.w\tr0,r0', 'or\tr0,r1')
            else:
                z = ob[0]
                if z & 1 == 0:
                    if mode == 'izy':                 # (one zero-extension for the word and + Y)
                        self.r0k(z - 128); self.i('mov.w\t@(r0,r14),r1', 'swap.b\tr1,r1', 'add\tr10,r1', 'extu.w\tr1,r1')
                        return ('ptr',)
                    self.word_ram(z - 128)
                else:
                    self.r0k(z - 128); self.i('mov.b\t@(r0,r14),r1'); self.r0k(((z + 1) & 0xFF) - 128)
                    self.i('mov.b\t@(r0,r14),r0', 'extu.b\tr1,r1', 'shll8\tr0', 'extu.w\tr0,r0', 'or\tr0,r1')
                if mode == 'izy': self.i('add\tr10,r1', 'extu.w\tr1,r1')
            return ('ptr',)
        raise ValueError(mode)

    def load(self, dst, e, ext=True):
        """the byte at e (ea_of's result) into dst (not r0, r1, r2); ext False: memory's byte sign-extended (A's
        lazy form, docs/27: its slow paths still zero-extend; either form is A's)"""
        if e[0] == 'ram':
            self.r0k(e[1])
            if e[2]: self.i('add\t%s,r0' % e[2])
            self.i('mov.b\t@(r0,r14),%s' % dst)
            if ext: self.i('extu.b\t%s,%s' % (dst, dst))
        elif e[0] == 'zpr':
            self.i('mov.b\t@(r0,r14),%s' % dst)
            if ext: self.i('extu.b\t%s,%s' % (dst, dst))
        elif e[0] == 'const':
            a = e[1]; s = a >> 13; n = self.u(); slow = '.Lrs%d' % n; done = '.Lrd%d' % n
            sub = self.port_rd_sub(a) if s == 0 else None
            if sub in ('_hs_vdcr2', '_hs_vdcr3'):  # the VDC's data reads in line (docs/23; huc_ops.h hw_rd_fast):
                o = self.o; vp = lambda f: o['vp_' + f]   # left > left_min1: MPR0 FF and nothing waits
                ol = '.Lro%d' % n
                self.i('mov.l\t@(%d,gbr),r0' % vp('left_min1'), 'cmp/gt\tr0,r7'); self.a.b('bf', ol)
                self.i('add\t#-1,r7', 'mov.w\t@(%d,gbr),r0' % vp('read_buffer'))
                if sub == '_hs_vdcr2': self.i('extu.b\tr0,%s' % dst)
                else:                             # the high byte; register 2: the buffer reloaded, MARR stepped
                    self.i('shlr8\tr0', 'extu.b\tr0,%s' % dst, 'mov.b\t@(%d,gbr),r0' % vp('select'), 'cmp/eq\t#2,r0')
                    self.a.b('bf', done)
                    z = '.Lrz%d' % n
                    self.i('mov.w\t@(%d,gbr),r0' % vp('marr'), 'mov\tr0,r1', 'cmp/pz\tr0', 'mov\t#0,r2'); self.a.b('bf', z)
                    self.i('add\tr0,r0', 'mov\tr0,r2', 'mov.l\t@(%d,gbr),r0' % vp('vram'), 'add\tr0,r2', 'mov.w\t@r2,r2')
                    self.a.l(z)
                    self.i('mov\tr2,r0', 'mov.w\tr0,@(%d,gbr)' % vp('read_buffer'), 'mov.w\t@(%d,gbr),r0' % vp('inc'),
                           'add\tr1,r0', 'mov.w\tr0,@(%d,gbr)' % vp('marr'))
                self.a.l(done)
                def rl(dst=dst, a=a, slow=slow, done=done, sub=sub, ol=ol):
                    mem = '.Lrm%d' % self.u()
                    self.a.l(ol); self.i('mov.b\t@(%d,gbr),r0' % self.o['MPR'], 'cmp/eq\t#-1,r0'); self.a.b('bf', mem)
                    self.call(sub); self.a.b('bf', slow); self.i('extu.b\tr0,%s' % dst); self.bra(done)
                    self.a.l(mem); self.i('mov.l\t@(%d,gbr),r0' % self.o['rp'], 'tst\tr0,r0'); self.a.b('bt', slow)
                    self.i('mov\tr0,r2'); self.r0k(a & 0x1FFF)
                    self.i('mov.b\t@(r0,r2),%s' % dst, 'extu.b\t%s,%s' % (dst, dst)); self.bra(done)
                    self.a.l(slow)
                    self.imm('r1', a, u8=False); self.call('_hs_rd_slow'); self.i('extu.b\tr0,%s' % dst); self.bra(done)
                self.ool('.Lrx%d' % n, rl)
                return
            if sub:                               # a port of the hardware page (MPR0 FF, its usual map): the
                mem = '.Lrm%d' % n                # subroutine in line, the memory aside (docs/21)
                if VCE_INLINE and sub in ('_hs_vcer4', '_hs_vcer5'):
                    vx = '.Lvc%d' % n             # (the colour's port in place, as _hs_vcer4 / 5, docs/28)
                    self.i('mov.l\t@(%d,gbr),r0' % self.o['vp_left_min1'], 'cmp/gt\tr0,r7'); self.a.b('bf', vx)
                    self.vce_ct('r1'); self.i('mov.w\t@r1,r0')
                    if sub == '_hs_vcer4': self.i('extu.b\tr0,%s' % dst)
                    else: self.i('shlr8\tr0', 'and\t#1,r0', 'or\t#254,r0', 'mov\tr0,%s' % dst); self.vce_step()
                    self.i('add\t#-1,r7'); self.bra(done)
                    self.a.l(vx)
                self.i('mov.b\t@(%d,gbr),r0' % self.o['MPR'], 'cmp/eq\t#-1,r0'); self.a.b('bf', mem)
                self.call(sub); self.a.b('bf', slow); self.i('extu.b\tr0,%s' % dst); self.a.l(done)
                def ml(dst=dst, a=a, slow=slow, done=done, mem=mem):
                    self.a.l(mem); self.i('mov.l\t@(%d,gbr),r0' % self.o['rp'], 'tst\tr0,r0'); self.a.b('bt', slow)
                    self.i('mov\tr0,r2'); self.r0k(a & 0x1FFF)
                    self.i('mov.b\t@(r0,r2),%s' % dst, 'extu.b\t%s,%s' % (dst, dst)); self.bra(done)
                    self.a.l(slow)
                    self.imm('r1', a, u8=False); self.call('_hs_rd_slow'); self.i('extu.b\tr0,%s' % dst); self.bra(done)
                self.ool('.Lrx%d' % n, ml)
                return
            self.i('mov.l\t@(%d,gbr),r0' % (self.o['rp'] + 4 * s), 'tst\tr0,r0'); self.a.b('bt', slow)
            self.i('mov\tr0,r2'); self.r0k(a & 0x1FFF)
            self.i('mov.b\t@(r0,r2),%s' % dst, 'extu.b\t%s,%s' % (dst, dst)); self.a.l(done)
            def sl(dst=dst, a=a, done=done, p=self.pend):
                sub = self.port_rd_sub(a) if s == 0 else None
                if sub:                           # (rp[0] is 0: the hardware page, if MPR0 is FF)
                    n2 = self.u(); sw = '.Lrw%d' % n2
                    self.i('mov.b\t@(%d,gbr),r0' % self.o['MPR'], 'cmp/eq\t#-1,r0'); self.a.b('bf', sw)
                    self.call(sub); self.a.b('bf', sw); self.i('extu.b\tr0,%s' % dst); self.bra(done)
                    self.a.l(sw)
                self.imm('r1', a, u8=False); self.cyc_call('_hs_rd_slow', p); self.i('extu.b\tr0,%s' % dst); self.bra(done)
            self.ool(slow, sl)
        elif e[0] == 'slotx':                     # a + X / Y inside slot s (not the hardware's): its memory
            a, ix = e[1], e[2]; s = a >> 13; n = self.u(); slow = '.Lrs%d' % n; done = '.Lrd%d' % n
            self.i('mov.l\t@(%d,gbr),r0' % (self.o['rp'] + 4 * s), 'tst\tr0,r0'); self.a.b('bt', slow)
            self.i('mov\tr0,r2'); self.r0k(a & 0x1FFF)
            self.i('add\t%s,r0' % ix, 'mov.b\t@(r0,r2),%s' % dst)
            if ext: self.i('extu.b\t%s,%s' % (dst, dst))
            self.a.l(done)
            def sl(dst=dst, a=a, ix=ix, done=done, p=self.pend):
                self.imm_sx('r1', a); self.i('add\t%s,r1' % ix, 'extu.w\tr1,r1')
                self.cyc_call('_hs_rd_slow', p); self.i('extu.b\tr0,%s' % dst); self.bra(done)
            self.ool(slow, sl)
        elif e[0] == 'ptr':                       # ea in r1: the page table (biased pointers, 1 KB pages)
            n = self.u(); slow = '.Lrs%d' % n; done = '.Lrd%d' % n
            self.i('swap.b\tr1,r0', 'shlr2\tr0', 'shlr\tr0', 'and\t#28,r0'); self.a.ld(4, 'r2', '_huc_pg')
            self.i('mov.l\t@(r0,r2),r2', 'tst\tr2,r2'); self.a.b('bt', slow)
            self.i('mov\tr1,r0', 'mov.b\t@(r0,r2),%s' % dst)
            if ext: self.i('extu.b\t%s,%s' % (dst, dst))
            self.a.l(done)
            def sl(dst=dst, done=done, p=self.pend):
                self.cyc_call('_hs_rd_slow', p); self.i('extu.b\tr0,%s' % dst); self.bra(done)
            self.ool(slow, sl)

    def store(self, src, e, keep_ea=False):
        """src's byte to e; src not r0, r1, r2 (a constant 0: src None, then r3 is set)"""
        if src is None: self.i('mov\t#0,r3'); src = 'r3'
        if e[0] == 'ram':
            self.r0k(e[1])
            if e[2]: self.i('add\t%s,r0' % e[2])
            self.i('mov.b\t%s,@(r0,r14)' % src)
        elif e[0] == 'zpr':
            self.i('mov.b\t%s,@(r0,r14)' % src)
        elif e[0] == 'const':
            a = e[1]; s = a >> 13; n = self.u(); slow = '.Lws%d' % n; done = '.Lwd%d' % n
            if s == 0 and a < 0x400 and (a & 3) >= 2:   # the VDC's data port, inline (the memory path aside)
                self.i('extu.b\t%s,r2' % src); self.vdc_data(a & 1, ('mem', a), done, self.pair_next)
                return
            if s == 0 and a < 0x1400 and (self.port_sub(a) or a < 0x400):
                # a port of the hardware page (MPR0 FF, its usual map), without the runtime while nothing waits
                # (VPort, the subroutines): in line, the memory aside (docs/21). The VDC's register select and
                # $0001: the select stored here (left > left_min1: MPR0 FF, nothing waits; huc_ops.h hw_wr_fast)
                mem = '.Lwm%d' % n; hw = '.Lwh%d' % n
                def memp(src=src, a=a, slow=slow, done=done, mem=mem):
                    self.a.l(mem); self.i('mov.b\t@(%d,gbr),r0' % self.o['wok'], 'tst\tr0,r0'); self.a.b('bt', slow)
                    self.i('mov.l\t@(%d,gbr),r0' % self.o['rp'], 'mov\tr0,r2'); self.r0k(a & 0x1FFF)
                    self.i('mov.b\t%s,@(r0,r2)' % src); self.bra(done)
                    self.a.l(slow); self.imm('r1', a, u8=False); self.i('extu.b\t%s,r2' % src); self.call('_hs_wr_slow')
                    self.bra(done)
                if a < 0x400 and (a & 3) < 2:
                    self.i('mov.l\t@(%d,gbr),r0' % self.o['vp_left_min1'], 'cmp/gt\tr0,r7'); self.a.b('bf', hw)
                    if (a & 3) == 0:
                        self.i('mov\t%s,r0' % src, 'and\t#31,r0', 'mov.b\tr0,@(%d,gbr)' % self.o['vp_select'])
                    self.i('add\t#-1,r7'); self.a.l(done)
                    def hp(src=src, a=a, slow=slow, done=done, mem=mem, hw=hw):
                        self.a.l(hw); self.i('mov.b\t@(%d,gbr),r0' % self.o['MPR'], 'cmp/eq\t#-1,r0'); self.a.b('bf', mem)
                        if (a & 3) == 0: self.i('extu.b\t%s,r2' % src); self.call('_hs_vdc0'); self.a.b('bt', done)
                        self.imm('r1', a, u8=False); self.i('extu.b\t%s,r2' % src); self.call('_hs_wr_slow'); self.bra(done)
                        memp()
                    self.ool('.Lwx%d' % n, hp)
                    return
                sub = self.port_sub(a)
                if VCE_INLINE and 0x400 <= a < 0x800 and (a & 7) in (4, 5):
                    # the colour's port in place while no event is due (left > left_min1: MPR0 FF), as _hs_vce4 / 5
                    # (docs/28); else the subroutine's path below
                    vx = '.Lvc%d' % n
                    self.i('mov.l\t@(%d,gbr),r0' % self.o['vp_left_min1'], 'cmp/gt\tr0,r7'); self.a.b('bf', vx)
                    self.vce_ct('r1')
                    if a & 1 == 0:
                        self.i('mov.w\t@r1,r0'); self.a.ld(2, 'r4', 0x100)
                        self.i('and\tr4,r0', 'extu.b\t%s,r4' % src, 'or\tr4,r0', 'mov.w\tr0,@r1')
                    else:
                        self.i('mov.w\t@r1,r0', 'extu.b\tr0,r4', 'mov\t%s,r0' % src, 'and\t#1,r0', 'shll8\tr0', 'or\tr4,r0',
                               'mov.w\tr0,@r1')
                        self.vce_step()
                    self.i('add\t#-1,r7'); self.a.l(done)
                    def vp(src=src, a=a, done=done, hw=hw, mem=mem, sub=sub, vx=vx):
                        self.a.l(vx)
                        self.i('mov.b\t@(%d,gbr),r0' % self.o['MPR'], 'cmp/eq\t#-1,r0'); self.a.b('bf', mem)
                        self.i('extu.b\t%s,r2' % src); self.call(sub); self.a.b('bt', done)
                        self.a.l(hw); self.imm('r1', a, u8=False); self.i('extu.b\t%s,r2' % src); self.call('_hs_wr_slow')
                        self.bra(done); memp()
                    self.ool('.Lwx%d' % n, vp)
                    return
                self.i('mov.b\t@(%d,gbr),r0' % self.o['MPR'], 'cmp/eq\t#-1,r0'); self.a.b('bf', mem)
                self.i('extu.b\t%s,r2' % src); self.call(sub); self.a.b('bf', hw); self.a.l(done)
                def hp(src=src, a=a, done=done, hw=hw):
                    self.a.l(hw); self.imm('r1', a, u8=False); self.i('extu.b\t%s,r2' % src); self.call('_hs_wr_slow')
                    self.bra(done); memp()
                self.ool('.Lwx%d' % n, hp)
                return
            self.i('mov.b\t@(%d,gbr),r0' % (self.o['wok'] + s), 'tst\tr0,r0'); self.a.b('bt', slow)
            self.i('mov.l\t@(%d,gbr),r0' % (self.o['rp'] + 4 * s), 'mov\tr0,r2')
            self.r0k(a & 0x1FFF); self.i('mov.b\t%s,@(r0,r2)' % src); self.a.l(done)
            def sl(src=src, a=a, done=done, p=self.pend):
                self.imm('r1', a, u8=False); self.i('extu.b\t%s,r2' % src); self.cyc_call('_hs_wr_slow', p); self.bra(done)
            self.ool(slow, sl)
        elif e[0] == 'slotx':
            a, ix = e[1], e[2]; s = a >> 13; n = self.u(); slow = '.Lws%d' % n; done = '.Lwd%d' % n
            self.i('mov.b\t@(%d,gbr),r0' % (self.o['wok'] + s), 'tst\tr0,r0'); self.a.b('bt', slow)
            self.i('mov.l\t@(%d,gbr),r0' % (self.o['rp'] + 4 * s), 'mov\tr0,r2'); self.r0k(a & 0x1FFF)
            self.i('add\t%s,r0' % ix, 'mov.b\t%s,@(r0,r2)' % src); self.a.l(done)
            def sl(src=src, a=a, ix=ix, done=done, p=self.pend):
                self.imm('r1', a, u8=False); self.i('add\t%s,r1' % ix, 'extu.w\tr1,r1', 'extu.b\t%s,r2' % src)
                self.cyc_call('_hs_wr_slow', p); self.bra(done)
            self.ool(slow, sl)
        elif e[0] == 'ptr':
            n = self.u(); slow = '.Lws%d' % n; done = '.Lwd%d' % n
            self.i('swap.b\tr1,r0', 'shlr2\tr0', 'shlr\tr0', 'and\t#28,r0'); self.a.ld(4, 'r2', '_huc_pgw')
            self.i('mov.l\t@(r0,r2),r2', 'tst\tr2,r2'); self.a.b('bt', slow)
            self.i('mov\tr1,r0', 'mov.b\t%s,@(r0,r2)' % src); self.a.l(done)
            def sl(src=src, done=done, p=self.pend):
                self.i('extu.b\t%s,r2' % src); self.cyc_call('_hs_wr_slow', p); self.bra(done)
            self.ool(slow, sl)

    def vdc_data(self, hi, pa, done, pair=None):
        """r2 = the byte for the VDC's data port (hi: its high byte): inline while no event is due and register 2 is
        selected (self.sel: known here), else the shared one (other registers), else the runtime (pa: ST1 / ST2's
        wr_phys address, None: wr_slow at $0002 / $0003)"""
        o = self.o; n = self.u(); sub = '.Lvs%d' % n
        self.i('mov.l\t@(%d,gbr),r0' % o['vp_left_min1'], 'cmp/gt\tr0,r7'); self.a.b('bf', sub)
        if self.sel != 2:
            selx = '.Lvx%d' % n
            self.i('mov.b\t@(%d,gbr),r0' % o['vp_select'], 'cmp/eq\t#2,r0'); self.a.b('bf', selx)
            def mw(hi=hi, done=done, sub=sub, selx=selx):   # register 0 (MAWR) here too (hw_wr_fast case 0)
                rd = '.Lvy%d' % self.u()
                # (the select read again: a far branch here goes through an island that takes r0, docs/31)
                self.a.l(selx); self.i('mov.b\t@(%d,gbr),r0' % o['vp_select'], 'cmp/eq\t#0,r0')
                if MARR_INLINE:                   # and 1 (MARR: its high byte reloads the read buffer, steps it;
                    self.a.b('bf', rd)            # _hs_vdc2 / 3's .Lv1, docs/28)
                else: self.a.b('bf', sub)
                self.i('add\t#-1,r7', 'mov\tr2,r0', 'mov.b\tr0,@(%d,gbr)' % (o['vp_mawr'] + (0 if hi else 1)))
                self.bra(done)
                if MARR_INLINE:
                    self.a.l(rd); self.i('cmp/eq\t#1,r0'); self.a.b('bf', sub)
                    self.i('add\t#-1,r7', 'mov\tr2,r0', 'mov.b\tr0,@(%d,gbr)' % (o['vp_marr'] + (0 if hi else 1)))
                    if hi:
                        z = '.Lvz%d' % self.u()
                        self.i('mov.w\t@(%d,gbr),r0' % o['vp_marr'], 'mov\t#0,r3', 'cmp/pz\tr0'); self.a.b('bf', z)
                        self.i('extu.w\tr0,r3', 'add\tr3,r3', 'mov.l\t@(%d,gbr),r0' % o['vp_vram'], 'add\tr0,r3', 'mov.w\t@r3,r3')
                        self.a.l(z)
                        self.i('mov\tr3,r0', 'mov.w\tr0,@(%d,gbr)' % o['vp_read_buffer'], 'mov.w\t@(%d,gbr),r0' % o['vp_marr'],
                               'mov\tr0,r3', 'mov.w\t@(%d,gbr),r0' % o['vp_inc'], 'add\tr3,r0', 'mov.w\tr0,@(%d,gbr)' % o['vp_marr'])
                    self.bra(done)
            self.ool(selx + 'o', mw)
        self.i('add\t#-1,r7')
        if not hi:
            self.i('mov\tr2,r0', 'mov.b\tr0,@(%d,gbr)' % o['vp_latch'])
        else:                                     # the word (big-endian: the byte written high, the latch low)
            sk = '.Lvk%d' % n                     # as two byte stores; MAWR kept in r4 for its step
            self.i('mov.w\t@(%d,gbr),r0' % o['vp_mawr'], 'mov\tr0,r4', 'cmp/pz\tr0'); self.a.b('bf', sk)
            self.i('mov\tr0,r3', 'add\tr3,r3', 'mov.l\t@(%d,gbr),r0' % o['vp_vram'], 'add\tr0,r3',
                   'mov.b\t@(%d,gbr),r0' % o['vp_latch'], 'mov.b\tr0,@(1,r3)', 'mov.b\tr2,@r3')
            self.vmark('r4', 'r3')
            self.a.l(sk)
            self.i('mov.w\t@(%d,gbr),r0' % o['vp_inc'], 'add\tr4,r0', 'mov.w\tr0,@(%d,gbr)' % o['vp_mawr'])
        self.a.l(done)
        def sp(hi=hi, pa=pa, done=done, pair=pair):
            if isinstance(pa, tuple):             # STA $0002 / $0003: memory unless slot 0 is the hardware page
                a = pa[1]; m2 = '.Lvm%d' % self.u()
                self.i('mov.b\t@(%d,gbr),r0' % o['MPR'], 'cmp/eq\t#-1,r0'); self.a.b('bf', m2)
                self.call('_hs_vdc3' if hi else '_hs_vdc2'); self.a.b('bt', done)
                if pair and pair[0] == 'run': self.pair_run_slow(pair, 0x1FE000 | a)
                elif pair:
                    self.a.ld(4, 'r1', 0x1FE000 | a); self.imm('r3', pair[0] | pair[1] << 8, u8=False)
                    self.call('_hs_wr_pair'); self.bra(pair[2])
                else: self.imm('r1', a, u8=False); self.call('_hs_wr_slow'); self.bra(done)
                self.a.l(m2); sl = '.Lvw%d' % self.u()
                self.i('mov.b\t@(%d,gbr),r0' % o['wok'], 'tst\tr0,r0'); self.a.b('bt', sl)
                self.i('mov\tr2,r3', 'mov.l\t@(%d,gbr),r0' % o['rp'], 'mov\tr0,r1'); self.r0k(a & 0x1FFF)
                self.i('mov.b\tr3,@(r0,r1)'); self.bra(done)
                self.a.l(sl); self.imm('r1', a, u8=False); self.call('_hs_wr_slow'); self.bra(done)
                return
            self.call('_hs_vdc3' if hi else '_hs_vdc2'); self.a.b('bt', done)
            if pair and pair[0] == 'run': self.pair_run_slow(pair, pa); return
            self.a.ld(4, 'r1', pa)
            if pair:                              # (this write, the next instruction's cycles, its write: past it)
                self.imm('r3', pair[0] | pair[1] << 8, u8=False); self.call('_hs_wr_pair'); self.bra(pair[2])
                return
            self.call('_hs_wr_phys')
            self.bra(done)
        self.ool(sub, sp)

    PURE = {'CLA', 'CLX', 'CLY', 'CLC', 'SEC', 'NOP', 'TAX', 'TXA', 'TAY', 'TYA', 'SAX', 'SAY', 'SXY', 'INX', 'DEX', 'INY',
            'DEY'}
    def pair_run(self, i, b, base, nxt, HC):
        """a write to $0002 (ST1, STA) then register-only instructions (nothing jumps to them, none changed by the
        code) up to a write to $0003 (ST2, STA / STX / STY): ('run', [their offsets], the write's offset, its label,
        the state here, the block) or None (docs/34)"""
        R = self.R; run = []; o = nxt
        for _ in range(PAIR_RUN + 1):
            if o not in HC or o in self.Tset or o in b.patched or o in b.dynops: return None
            m, mode, ob, n, t = R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, o)
            if m == 'ST2' or (m in ('STA', 'STX', 'STY') and mode == 'abs' and ob[0] == 3 and ob[1] == 0):
                if not run: return None           # (right after: the docs/31 form)
                return ('run', run, o, '.Lp2_%d' % self.u(), (self.zs, self.ns, self.az, self.ck, self.sel), (i, b, base, HC))
            if not (m in self.PURE or (mode == 'imm' and m in ('LDA', 'LDX', 'LDY', 'AND', 'ORA', 'EOR', 'ADC', 'SBC', 'CMP',
                                                                'CPX', 'CPY')) or (mode == 'acc' and m in ('ASL', 'LSR', 'ROL', 'ROR', 'INC', 'DEC'))):
                return None
            run.append(o); o += n
        return None

    def pair_run_slow(self, pair, pa):
        """the pair's way to the runtime, r2 = the first byte (pa its wr_phys address): the instructions between run
        here as on the hot path (the same state, so the same registers after), then both writes in one visit with
        their cycles between, and on after the second write (docs/34)"""
        _, run, sto, lab, st, (i, b, base, HC) = pair
        sv = (self.pend, self.zs, self.ns, self.az, self.ck, self.sel)
        self.pend = 0; self.zs, self.ns, self.az, self.ck, self.sel = st
        self.i('mov.l\tr2,@-r15'); total = 0
        for k, o in enumerate(run):
            total += self.CYC[int(b.data[o])]
            nx = self.ins(i, b, base, o, HC, run[k + 1] if k + 1 < len(run) else sto)
            assert nx == (run[k + 1] if k + 1 < len(run) else sto) and self.pend == total, (hex(o), self.pend, total)
        R = self.R; m, mode, ob, n, t = R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, sto)
        cyc = self.pend + self.CYC[int(b.data[sto])]
        if m == 'ST2': self.imm('r3', ob[0] | cyc << 8, u8=False)
        else:
            self.i('mov\t%s,r3' % {'STA': 'r8', 'STX': 'r9', 'STY': 'r10'}[m], 'extu.b\tr3,r3'); self.a.ld(2, 'r0', cyc << 8)
            self.i('or\tr0,r3')
        self.i('mov.l\t@r15+,r2'); self.a.ld(4, 'r1', pa); self.call('_hs_wr_pair'); self.bra(lab)
        self.pend, self.zs, self.ns, self.az, self.ck, self.sel = sv

    def vce_ct(self, r):                          # r = &color[ctaddress & 0x1FF] (subs_ports' ct_color)
        o = self.o
        self.i('mov.w\t@(%d,gbr),r0' % o['vp_ctaddress'], 'mov\tr0,%s' % r); self.a.ld(2, 'r0', 0x1FF)
        self.i('and\tr0,%s' % r, 'add\t%s,%s' % (r, r), 'mov.l\t@(%d,gbr),r0' % o['vp_color'], 'add\tr0,%s' % r)

    def vce_step(self):                           # ctaddress + 1 within $1FF (ct_step), through r4
        o = self.o
        self.i('mov.w\t@(%d,gbr),r0' % o['vp_ctaddress'], 'add\t#1,r0', 'mov\tr0,r4'); self.a.ld(2, 'r0', 0x1FF)
        self.i('and\tr4,r0', 'mov.w\tr0,@(%d,gbr)' % o['vp_ctaddress'])

    def port_sub(self, a):
        if a < 0x400:
            return {0: '_hs_vdc0', 1: None, 2: '_hs_vdc2', 3: '_hs_vdc3'}[a & 3]
        if a < 0x800 and (a & 7) in (2, 3, 4, 5): return '_hs_vce%d' % (a & 7)
        if 0x800 <= a < 0xC00: return '_hs_psg_w'
        if 0x1000 <= a < 0x1400: return '_hs_pad_w'
        return None

    def port_rd_sub(self, a):
        if a < 0x400:
            if INPLACE_RD and a & 3 == 0: return '_hs_vdcr0'   # (the status, docs/33)
            return '_hs_vdcr%d' % (a & 3) if a & 2 else None
        if INPLACE_RD and (a & 0x1C0F) in (0x180C, 0x180D): return '_hs_cdr%s' % ('c' if a & 1 == 0 else 'd')
        if a < 0x800 and (a & 7) in (4, 5): return '_hs_vcer%d' % (a & 7)
        if 0x1000 <= a < 0x1400: return '_hs_pad_r'
        return None

    # -------------------------------------------------------------------------------------------------------------
    def lab(self, i, off):
        return 'L%d_%04X' % (i, off)

    def kind(self, mode, ob):
        """where an operand is: 'ram' (base RAM, the zero page), 'const' (a constant address elsewhere), 'ptr'"""
        if mode in ('abs', 'abx', 'aby'):
            a = ob[0] | ob[1] << 8
            if a >> 13 == 1 and (mode == 'abs' or (a + 0xFF) >> 13 == 1): return 'ram'
            return 'const' if mode == 'abs' else 'ptr'
        if mode in ('izp', 'izx', 'izy'): return 'ptr'
        return 'ram'

    def operand(self, m, mode, ob, dst='r3', ext=True):
        """the operand's value into dst (zero-extended unless ext False); returns the access (for a write back)"""
        if mode == 'imm':
            self.imm(dst, ob[0]); return None
        if mode in ('imzp', 'imzx', 'imab', 'imax'):
            mode = {'imzp': 'zp', 'imzx': 'zpx', 'imab': 'abs', 'imax': 'abx'}[mode]; ob = ob[1:]
        if self.hw_access(mode, ob): self.flush()
        e = self.ea_of(mode, ob)
        self.load(dst, e, ext)
        return e

    def word_ram(self, d):                        # the little-endian word at r14 + d (d even) into r1 (u16)
        self.r0k(d); self.i('mov.w\t@(r0,r14),r1', 'swap.b\tr1,r1', 'extu.w\tr1,r1')

    def rd16_const(self, p):
        """the word at constant address p (two reads, p and p + 1 as 16-bit) into r1"""
        if p >> 13 == 1 and (p + 1) >> 13 == 1:
            d = self.ram_off(p)
            if d & 1 == 0: self.word_ram(d); return
            self.r0k(d); self.i('mov.b\t@(r0,r14),r1'); self.r0k(d + 1)
            self.i('mov.b\t@(r0,r14),r0', 'extu.b\tr1,r1', 'shll8\tr0', 'extu.w\tr0,r0', 'or\tr0,r1')
            return
        self.flush()
        self.load('r3', ('const', p)); self.i('mov.l\tr3,@-r15')
        self.load('r3', ('const', (p + 1) & 0xFFFF)); self.i('mov.l\t@r15+,r1', 'shll8\tr3', 'or\tr3,r1')

    def rd16_ptr(self, one_page=False):
        """the word at r1 (16-bit: r1 + 1 wraps) into r1; one_page: r1 + 1 is in r1's slot (docs/32)"""
        self.flush()
        if one_page:
            n = self.u(); slow = '.Lrp%d' % n; done = '.Lrq%d' % n
            self.i('swap.b\tr1,r0', 'shlr2\tr0', 'shlr\tr0', 'and\t#28,r0'); self.a.ld(4, 'r2', '_huc_pg')
            self.i('mov.l\t@(r0,r2),r2', 'tst\tr2,r2'); self.a.b('bt', slow)
            self.i('mov\tr1,r0', 'mov.b\t@(r0,r2),r3', 'add\t#1,r0', 'mov.b\t@(r0,r2),r1', 'extu.b\tr3,r3', 'shll8\tr1',
                   'extu.w\tr1,r1', 'or\tr3,r1')
            self.a.l(done)
            def sl(slow=slow, done=done):
                self.a.l(slow); self.rd16_ptr(); self.bra(done)
            self.ool(slow + 'x', sl)
            return
        self.i('mov.l\tr1,@-r15'); self.load('r3', ('ptr',))
        self.i('mov.l\t@r15+,r1', 'mov.l\tr3,@-r15', 'add\t#1,r1', 'extu.w\tr1,r1')
        self.load('r3', ('ptr',)); self.i('mov.l\t@r15+,r1', 'shll8\tr3', 'or\tr3,r1')

    def code_byte(self, base, o):                 # (dyn) an operand byte read when the code runs, into r3
        self.load('r3', ('const', (base + o) & 0xFFFF))

    def push_const(self, v):
        self.imm('r3', v); self.i('mov\tr6,r0', 'mov.b\tr3,@(r0,r5)', 'add\t#-1,r6', 'extu.b\tr6,r6')

    def push2_const(self, v):                     # the high byte then the low one (JSR / BSR): S - 1, S - 2
        b8 = lambda x: x - 256 if x > 127 else x          # (a byte store: the low 8 bits are enough)
        self.i('mov\tr6,r0', 'mov\t#%d,r3' % b8(v >> 8), 'mov.b\tr3,@(r0,r5)', 'add\t#-1,r0', 'extu.b\tr0,r0',
               'mov\t#%d,r3' % b8(v & 0xFF), 'mov.b\tr3,@(r0,r5)', 'add\t#-1,r0', 'extu.b\tr0,r6')

    def push_reg(self, r):
        self.i('mov\tr6,r0', 'mov.b\t%s,@(r0,r5)' % r, 'add\t#-1,r6', 'extu.b\tr6,r6')

    def pop(self, dst, ext=True):
        self.i('add\t#1,r6', 'extu.b\tr6,r6', 'mov\tr6,r0', 'mov.b\t@(r0,r5),%s' % dst)
        if ext: self.i('extu.b\t%s,%s' % (dst, dst))

    def za(self):                                 # A zero-extended where a use needs it (docs/27: A's lazy form)
        if not self.az: self.i('extu.b\tr8,r8'); self.az = True

    def set_mpr(self, k):
        """rt.c set_mpr(c, k, A), inline: the slot's memory, writable, written flag, page tables, merged block"""
        o = self.o; n = self.u(); same = '.Lms%d' % n
        self.i('mov.l\t@(%d,gbr),r0' % o['user'], 'mov\tr0,r1', 'mov\tr8,r2', 'shll2\tr2', 'add\tr2,r2')
        self.a.ld(2, 'r0', o['rt_bank_rec']); self.i('add\tr1,r0', 'add\tr0,r2', 'mov.l\t@r2,r3')   # r2 = &bank_rec[v], r3 its memory (docs/35)
        self.i('mov.b\t@(%d,gbr),r0' % (o['MPR'] + k), 'extu.b\tr0,r0', 'cmp/eq\tr8,r0'); self.a.b('bf', '.Lmc%d' % n)
        self.i('mov.l\t@(%d,gbr),r0' % (o['rp'] + 4 * k), 'cmp/eq\tr3,r0'); self.a.b('bt', same)
        self.a.l('.Lmc%d' % n)
        if TAM_SUB:                               # (the change, shared: one subroutine a slot, docs/32)
            self.call('_hs_tam%d' % k); self.a.l(same); self.mpr_after(k, n); return
        self.mpr_change(k, n)
        self.a.l(same); self.mpr_after(k, n)

    def mpr_after(self, k, n):
        o = self.o
        if k == 1:                                # (the translation reads base RAM straight: counted, docs/16)
            self.i('mov\t#-8,r2', 'extu.b\tr2,r2', 'cmp/eq\tr2,r8'); self.a.b('bt', '.Lmu%d' % n)
            self.a.ld(2, 'r0', o['rt_unmodelled']); self.i('add\tr1,r0', 'mov.l\t@r0,r2', 'add\t#1,r2', 'mov.l\tr2,@r0')
            self.a.l('.Lmu%d' % n)

    def mpr_change(self, k, n):
        """set_mpr's change: r1 = the Rt, r2 = &bank_rec[A] (docs/35), r3 = its memory, A zero-extended; r0, r2, r4 taken"""
        o = self.o
        self.i('mov\tr8,r0', 'mov.b\tr0,@(%d,gbr)' % (o['MPR'] + k), 'mov\tr3,r0', 'mov.l\tr0,@(%d,gbr)' % (o['rp'] + 4 * k))
        self.i('mov.b\t@(4,r2),r0', 'mov\tr0,r4', 'mov.b\tr0,@(%d,gbr)' % (o['wok'] + k))   # (wok)
        self.i('mov.b\t@(6,r2),r0', 'cmp/eq\t#%d,r0' % k, 'mov.b\t@(5,r2),r0')   # (hslot k: hid, else 0)
        self.a.b('bt', '.Lmh%d' % n); self.i('mov\t#0,r0')
        self.a.l('.Lmh%d' % n); self.i('mov.b\tr0,@(%d,gbr)' % (o['hid'] + k))
        self.a.ld(2, 'r0', o['rt_bank_dirty']); self.i('add\tr1,r0', 'add\tr8,r0', 'mov.l\tr0,@(%d,gbr)' % (o['dirty'] + 4 * k))
        self.i('mov\tr3,r0', 'tst\tr0,r0'); self.a.b('bt', '.Lmz%d' % n)
        if k: self.a.ld(4, 'r2', k << 13); self.i('sub\tr2,r0')     # (a long: 0x8000 up is no mov.w)
        self.a.l('.Lmz%d' % n)
        self.a.ld(4, 'r2', '_huc_pg'); self.i('mov.l\tr0,@(%d,r2)' % (4 * k), 'tst\tr4,r4'); self.a.b('bf', '.Lmw%d' % n)
        self.i('mov\t#0,r0'); self.a.l('.Lmw%d' % n)
        self.a.ld(4, 'r2', '_huc_pgw'); self.i('mov.l\tr0,@(%d,r2)' % (4 * k))
        if k == 0:                                # (rt.c set_mpr: left_min1 follows MPR0)
            nv = '.Lmn%d' % n; dn = '.Lmd%d' % n
            self.a.ld(4, 'r2', 0x40000000); self.i('mov\t#-1,r0', 'extu.b\tr0,r0', 'cmp/eq\tr0,r8'); self.a.b('bf', dn)
            self.i('mov.l\t@(%d,gbr),r0' % o['vp_left_min'], 'mov\tr0,r3'); self.a.ld(4, 'r0', 0x7FFFFFFF)
            self.i('cmp/eq\tr0,r3'); self.a.b('bt', dn); self.i('mov\tr3,r2', 'add\t#1,r2')
            self.a.l(dn); self.i('mov\tr2,r0', 'mov.l\tr0,@(%d,gbr)' % o['vp_left_min1'])

    def pop2(self, plus=0):                       # two pops: r1 = the low byte | the high one << 8, + plus, as
        self.i('mov\tr6,r0', 'add\t#1,r0', 'extu.b\tr0,r0', 'mov.b\t@(r0,r5),r1', 'add\t#1,r0', 'extu.b\tr0,r0',
               'mov.b\t@(r0,r5),r3', 'mov\tr0,r6', 'extu.b\tr1,r1', 'shll8\tr3', 'or\tr3,r1')   # a signed 16-bit
        if plus: self.i('add\t#%d,r1' % plus)    # value + plus (not extended again: chain() compares it as it is,
        # the one return it then misses, to $8000, goes to the shared lookup; .Llocal / .Lleave take its low 16 bits)

    def cross(self, a, rule=True):
        """a constant jump to another slot's address a: the label of the block mapped there, if a is one of its
        entries and the runtime would go on at once (c->xok, cycles left) - what local_ decides - or leave"""
        js = self.ent_at.get((a >> 13, a & 0x1FFF), [])
        self.flush(); self.mat()
        if js:
            n = self.u(); out = '.Lxo%d' % n
            self.i('mov.l\t@(%d,gbr),r0' % self.o['xmin'], 'cmp/gt\tr0,r7')
            self.i('mov.b\t@(%d,gbr),r0' % (self.o['hid'] + (a >> 13))); self.a.b('bf', out)   # (the load before the
                                                  # branch: its use not next, docs/43)
            for j in js:
                self.i('cmp/eq\t#%d,r0' % j); self.a.b('bt', self.lab(j, a & 0x1FFF))
            self.a.l(out)                         # (not through: the shared lookup, which looks again, docs/24)
            self.imm('r1', a, u8=False); self.i('mov\t#8,r3'); self.bra('.Llocal'); self.a.u(); return
        self.leave(a)

    def local(self, slot):                        # r1 = the address (u16), found when it runs: the shared lookup
        self.flush(); self.mat()
        self.i('mov\t#%d,r3' % slot); self.bra('.Llocal'); self.a.u()

    def poll_r1(self):                            # POLL(t) with t in r1: below 0, leave for t
        self.i('cmp/pz\tr7'); self.a.b('bf', '.Lleave')

    def mat_for(self, need):                      # the flags a label needs, in r11 / r12
        if 'N' in need and self.ns != 'r12': self.i('mov\t%s,r12' % self.ns); self.ns = 'r12'
        if 'Z' in need and self.zs != 'r11': self.i('mov\t%s,r11' % self.zs); self.zs = 'r11'

    def to_label(self, i, tgt, poll_pc=None):
        """this path goes to block i's label tgt (its flags, the cycles counted); poll_pc: POLL first (below 0, leave
        for poll_pc)"""
        self.mat_for(self.need[i].get(tgt, ALL))
        L = self.lab(i, tgt)
        if poll_pc is not None:                   # (the budget's test: taken but at its end)
            self.flush(); self.i('cmp/pz\tr7'); self.a.b('bt', L, likely=True); self.leave(poll_pc); return
        p = self.pend
        if 0 < p <= 128: self.a.b('bra', L); self.i('add\t#%d,r7' % -p); self.pend = 0
        else: self.flush(); self.bra(L)
        self.a.u()

    def goto(self, i, base, off, tgt, HC, poll):
        """a jump to offset tgt of this slot"""
        if 0 <= tgt < 0x2000 and tgt in HC:
            self.to_label(i, tgt, (base + tgt) & 0xFFFF if poll and tgt <= off else None)
        else:
            a = (base + tgt) & 0xFFFF
            if a >> 13 == base >> 13: self.leave(a)
            else: self.cross(a)

    def jump_abs(self, i, b, base, off, a, HC, m):
        R = self.R
        if (a & 0x1FFF) == off and m == 'JMP' and b.bank in R.SLOT and a >> 13 == R.SLOT[b.bank]:
            self.leave(a, idle=True); return
        if a >> 13 == base >> 13: self.goto(i, base, off, a & 0x1FFF, HC, True)
        else: self.cross(a)

    def jump_rel(self, i, b, base, off, tgt, HC, m):  # (the taken path: CY(2) charged by the caller)
        R = self.R
        if tgt == off and m == 'BRA' and b.bank in R.SLOT:
            self.leave(base + off, idle=True); return
        if (m == 'BEQ' and tgt == off - 2 and tgt >= 0 and int(b.data[tgt]) == 0xA5 and b.bank in R.SLOT
                and tgt in HC):
            # Sapphire's wait (m2_recomp.py, docs/09): LDA zp / BEQ back to it - left at the LDA as a waiting loop
            self.leave(base + tgt, idle=True); return
        if 0 <= tgt < 0x2000 and tgt in HC: self.goto(i, base, off, tgt, HC, True)
        else:
            a = (base + tgt) & 0xFFFF
            if a >> 13 == base >> 13: self.leave(a)
            else: self.cross(a)

    def cond(self, m):
        """T for the condition; returns the branch op taken on it"""
        if m in ('BNE', 'BEQ'): self.i('tst\t%s,%s' % (self.zs, self.zs)); return 'bf' if m == 'BNE' else 'bt'
        if m in ('BMI', 'BPL'): self.i('mov\t%s,r0' % self.ns, 'tst\t#128,r0'); return 'bf' if m == 'BMI' else 'bt'
        if m in ('BCC', 'BCS'): self.i('tst\tr13,r13'); return 'bt' if m == 'BCC' else 'bf'
        if m in ('BVC', 'BVS'):
            self.i('mov.b\t@(%d,gbr),r0' % self.o['vf'], 'tst\t#128,r0'); return 'bt' if m == 'BVC' else 'bf'
        raise ValueError(m)

    def branch(self, i, b, base, off, op, tgt, HC, m):
        """a conditional branch, T set, taken on op: CY(2) and the jump on the taken path; the other path keeps the
        state it had (cycles pending, flags where they were). Forward to a label of this block: bt/s (bf/s), the
        target's flags settled first (mov keeps T) and the cycles with the branch's 2 in its delay slot, run on both
        paths: the path not taken goes on with 2 counted ahead (pend -2, docs/28)"""
        if DELAYED_BR and tgt > off and 0 <= tgt < 0x2000 and tgt in HC and self.pend + 2 <= 128:
            self.mat_for(self.need[i].get(tgt, ALL))
            self.a.b(op + '/s', self.lab(i, tgt)); self.i('add\t#%d,r7' % -(self.pend + 2))
            self.pend = -2
            return
        n = self.u(); nt = '.Lnt%d' % n
        p0, zs0, ns0, az0 = self.pend, self.zs, self.ns, self.az
        self.a.b('bf' if op == 'bt' else 'bt', nt)
        self.pend += 2
        self.jump_rel(i, b, base, off, tgt, HC, m)
        self.pend, self.zs, self.ns, self.az = p0, zs0, ns0, az0
        self.a.l(nt)

    def interp(self, here, n_calls=1):
        self.flush(); self.mat()
        self.imm('r1', here, u8=False); self.i('mov\tr1,r0', 'mov.w\tr0,@(%d,gbr)' % self.o['pc'])
        for _ in range(n_calls): self.call('_hs_interp')
        self.az = True                            # (the registers back from c, zero-extended)

    def dchk(self, pc):                           # D set (PLP): leave for the runtime at pc (docs/24)
        n = self.u(); ok = '.Ldk%d' % n
        self.i('mov.b\t@(%d,gbr),r0' % self.o['P'], 'tst\t#8,r0'); self.a.b('bt', ok)
        self.leave(pc); self.a.l(ok)

    def irqchk(self, pc):
        n = self.u(); ok = '.Liq%d' % n
        self.i('mov.b\t@(%d,gbr),r0' % self.o['irq_wait'], 'tst\tr0,r0'); self.a.b('bt', ok)
        self.leave(pc); self.a.l(ok)

    def adc_sbc(self, m, v_live=True):            # x in r3 -> A, C, V, Z N from A (binary: the hot code runs with D
        n = self.u(); done = '.Ldd%d' % n         # clear, docs/24)
        ck, self.ck = self.ck, None                # (CLC / SEC just before: C known, docs/30)
        if m == 'ADC':
            self.i('mov\tr8,r2', 'add\tr3,r2')
            if ck != 0: self.i('add\tr13,r2')
            if v_live: self.i('mov\tr8,r0', 'xor\tr2,r0', 'xor\tr2,r3', 'and\tr3,r0',   # (r3 not needed after)
                              'mov.b\tr0,@(%d,gbr)' % self.o['vf'])
            self.i('mov\tr2,r13', 'shlr8\tr13', 'extu.b\tr2,r8')
        else:
            self.i('mov\tr8,r2', 'sub\tr3,r2')
            if ck != 1: self.i('mov\tr13,r0', 'xor\t#1,r0', 'sub\tr0,r2')
            if v_live: self.i('mov\tr8,r0', 'xor\tr2,r0', 'xor\tr8,r3', 'and\tr3,r0',
                              'mov.b\tr0,@(%d,gbr)' % self.o['vf'])
            self.i('mov\tr2,r0', 'shlr8\tr0', 'and\t#1,r0', 'xor\t#1,r0', 'mov\tr0,r13', 'extu.b\tr2,r8')
        self.a.l(done)
        self.zs = self.ns = 'r8'

    def rmw_op(self, m, r):                       # ASL LSR ROL ROR INC DEC on register r (u8)
        if m == 'ASL': self.i('add\t%s,%s' % (r, r), 'mov\t%s,r13' % r, 'shlr8\tr13', 'extu.b\t%s,%s' % (r, r))
        elif m == 'LSR': self.i('shlr\t%s' % r, 'movt\tr13')
        elif m == 'ROL': self.i('add\t%s,%s' % (r, r), 'or\tr13,%s' % r, 'mov\t%s,r13' % r, 'shlr8\tr13', 'extu.b\t%s,%s' % (r, r))
        elif m == 'ROR': self.i('mov\tr13,r0', 'shll8\tr0', 'or\tr0,%s' % r, 'shlr\t%s' % r, 'movt\tr13')
        elif m == 'INC': self.i('add\t#1,%s' % r, 'extu.b\t%s,%s' % (r, r))
        elif m == 'DEC': self.i('add\t#-1,%s' % r, 'extu.b\t%s,%s' % (r, r))

    # -------------------------------------------------------------------------------------------------------------
    def ins(self, i, b, base, off, HC, next_lab):
        """one instruction at offset off of block i (its label already placed); returns the offset it falls into
        (the next label, state carried) or None (it ended: a jump, a leave)"""
        R = self.R
        d = R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, off)
        m, mode, ob, n, t = d
        op = int(b.data[off]); nxt = off + n; here = (base + off) & 0xFFFF
        self.cur_w = COUNTS.get((b.bank, off), 0)  # (the instruction's runs: its constants' weight, docs/35)

        def goto_next(o=nxt):
            if o in HC:
                if o == next_lab: return o
                self.to_label(i, o); return None
            self.leave((base + o) & 0xFFFF); return None

        if off in b.patched:                      # its opcode may change: run only while it is this one
            n_ = self.u(); ok = '.Lpo%d' % n_
            self.flush(); self.mat(); self.code_byte(base, off)
            self.imm('r2', op); self.i('cmp/eq\tr2,r3'); self.a.b('bt', ok)
            # (docs/13: to the cold C translation, which runs the other opcodes the game writes here - m2_recomp.py
            # PATCH_ALTS - and leaves to the interpreter for any other)
            self.i('mov\t#3,r0', 'mov.b\tr0,@(%d,gbr)' % self.o['miss']); self.leave(here)
            self.a.l(ok)
        self.cy(self.CYC[op])
        dyn = (off in b.dynops or off in b.patched) and m not in ('RTS', 'RTI', 'BRK')

        if dyn and m in R.CTRL:
            return self.ins_dyn(i, b, base, off, m, mode, n, nxt, HC, goto_next)
        if not supported(m, mode, b, off):
            if m == 'SET':
                d2 = R.decode(lambda a: int(b.data[a & 0x1FFF]), nxt)
                m2, mode2, ob2, n2, t2 = d2
                if nxt in b.patched:              # (the interpreter, in T mode)
                    self.flush(); self.mat(); self.i('mov\t#1,r0', 'mov.b\tr0,@(%d,gbr)' % self.o['tflag'])
                    self.i('mov\t#1,r0', 'mov.b\tr0,@(%d,gbr)' % self.o['miss']); self.leave((base + nxt) & 0xFFFF); return None
                if m2 not in R.TCAP: return goto_next()
                self.cy(self.CYC[int(b.data[nxt])] + 3)
                self.interp(here, 2)
                return goto_next(nxt + n2)
            if m == 'BRK':
                self.interp(here)
                self.i('mov.w\t@(%d,gbr),r0' % self.o['pc'], 'extu.w\tr0,r1'); self.bra('.Lleave'); self.a.u(); return None
            if m in R.CTRL: raise NotImplementedError('%s %s' % (m, mode))
            self.interp(here)
            if mode == 'blk':                     # (RELOAD_LEFT)
                self.i('mov\t#0,r0', 'mov.b\tr0,@(%d,gbr)' % self.o['xok']); self.a.ld(4, 'r0', 0x7FFFFFFF)
                self.i('mov.l\tr0,@(%d,gbr)' % self.o['xmin'])
            if m in ('CLI', 'PLP'): self.irqchk((base + nxt) & 0xFFFF)
            return goto_next()

        A, X, Y = 'r8', 'r9', 'r10'
        if m in ('LDA', 'LDX', 'LDY'):
            r = {'LDA': A, 'LDX': X, 'LDY': Y}[m]
            lazy = r == A and LAZY_A and mode != 'imm'      # (A's byte as the load leaves it: docs/27)
            if mode == 'imm': self.imm(r, ob[0])
            else: self.operand(m, mode, ob, dst=r, ext=not lazy)
            if r == A: self.az = not lazy
            self.zn(r); return goto_next()
        if m in ('STA', 'STX', 'STY', 'STZ'):
            src = {'STA': A, 'STX': X, 'STY': Y, 'STZ': None}[m]
            if self.hw_access(mode, ob): self.flush()
            if PORT_PAIR and mode == 'abs' and ob[0] == 2 and ob[1] == 0 and nxt in HC and nxt not in self.Tset \
                    and nxt not in b.patched and nxt not in b.dynops and int(b.data[nxt]) == 0x23:
                # STA $0002 then ST2: as ST1 then ST2 (the hardware page's way, MPR0 $FF: wr_slow is wr_phys there)
                self.pair_next = (int(b.data[nxt + 1]), self.CYC[0x23], '.Lp2_%d' % self.u())
                self.pair_at = (nxt, self.pair_next[2])
            elif PAIR_RUN and mode == 'abs' and ob[0] == 2 and ob[1] == 0 and m != 'STZ':
                pr = self.pair_run(i, b, base, nxt, HC)   # (with register-only instructions between, docs/34)
                if pr: self.pair_next = pr; self.pair_at = (pr[2], pr[3])
            e = self.ea_of(mode, ob); self.store(src, e); self.pair_next = None
            if mode == 'abs' and ob[0] == 3 and ob[1] == 0 and self.pair_at and self.pair_at[0] == off:
                self.a.l(self.pair_at[1]); self.pair_at = None
            return goto_next()
        if m in ('AND', 'ORA', 'EOR'):
            ins = {'AND': 'and', 'ORA': 'or', 'EOR': 'xor'}[m]
            if m == 'EOR' and mode == 'imm':      # (the result zero-extended at once: either form of A, docs/32)
                if ob[0] == 0xFF: self.i('not\tr8,r8', 'extu.b\tr8,r8')
                else: self.i('mov\tr8,r0', 'xor\t#%d,r0' % ob[0], 'extu.b\tr0,r8')
                self.az = True; self.zn(A); return goto_next()
            if m == 'EOR': self.za()              # (AND: zero-extended after; ORA: either form stays one)
            if mode == 'imm': self.i('mov\tr8,r0', '%s\t#%d,r0' % (ins, ob[0]), 'mov\tr0,r8')
            else: self.operand(m, mode, ob); self.i('%s\tr3,r8' % ins)
            if m != 'ORA': self.az = True
            self.zn(A); return goto_next()
        if m in ('ADC', 'SBC'):
            self.operand(m, mode, ob); self.za(); self.adc_sbc(m, self.vlive[i].get(off, True)); return goto_next()
        if m in ('CMP', 'CPX', 'CPY'):
            r = {'CMP': A, 'CPX': X, 'CPY': Y}[m]
            if r == A: self.za()
            self.operand(m, mode, ob)
            self.i('mov\t%s,r2' % r, 'sub\tr3,r2', 'extu.b\tr2,r11', 'cmp/hs\tr3,%s' % r, 'movt\tr13')
            self.zs = self.ns = 'r11'; return goto_next()
        if m in ('BIT', 'TST'):
            self.operand(m, mode, ob)
            self.i('mov\tr3,r0', 'add\tr0,r0', 'mov.b\tr0,@(%d,gbr)' % self.o['vf'], 'mov\tr3,r12')
            if m == 'BIT': self.i('mov\tr3,r11', 'and\tr8,r11')
            else: self.i('mov\tr3,r0', 'and\t#%d,r0' % ob[0], 'mov\tr0,r11')
            self.zs, self.ns = 'r11', 'r12'; return goto_next()
        if m in ('ASL', 'LSR', 'ROL', 'ROR', 'INC', 'DEC'):
            if mode == 'acc':
                if m not in ('INC', 'DEC'): self.za()
                self.rmw_op(m, A); self.az = True; self.zn(A); return goto_next()
            if self.hw_access(mode, ob): self.flush()
            e = self.ea_of(mode, ob)
            if e[0] == 'zpr': self.i('mov\tr0,r4')
            self.load('r3', e, ext=m not in ('INC', 'DEC') or not R0_CACHE)   # (INC / DEC: the result extended)
            self.rmw_op(m, 'r3')
            self.i('mov\tr3,r11'); self.zs = self.ns = 'r11'
            if e[0] == 'zpr': self.i('mov\tr4,r0')
            self.store('r3', e); return goto_next()
        if m in ('TSB', 'TRB'):
            self.za()
            if self.hw_access(mode, ob): self.flush()
            e = self.ea_of(mode, ob); self.load('r3', e)
            self.i('mov\tr3,r0', 'add\tr0,r0', 'mov.b\tr0,@(%d,gbr)' % self.o['vf'], 'mov\tr3,r12', 'mov\tr3,r11')
            if m == 'TSB': self.i('or\tr8,r11', 'or\tr8,r3')
            else: self.i('mov\tr8,r0', 'not\tr0,r0', 'and\tr0,r11', 'and\tr0,r3')
            self.zs, self.ns = 'r11', 'r12'
            self.store('r3', e); return goto_next()
        if m[:3] in ('RMB', 'SMB'):
            bit = 1 << int(m[3])
            self.r0k(ob[0] - 128); self.i('mov.b\t@(r0,r14),r0')
            self.i(('and\t#%d,r0' % (~bit & 0xFF)) if m[0] == 'R' else ('or\t#%d,r0' % bit))
            self.i('mov\tr0,r3'); self.r0k(ob[0] - 128); self.i('mov.b\tr3,@(r0,r14)'); return goto_next()

        simple = {'TAX': (A, X), 'TXA': (X, A), 'TAY': (A, Y), 'TYA': (Y, A), 'TSX': ('r6', X)}
        if m in simple:
            s_, d_ = simple[m]
            self.i('%s\t%s,%s' % ('extu.b' if s_ == A and not self.az else 'mov', s_, d_))   # (X, Y: zero-extended)
            if d_ == A: self.az = True
            self.zn(d_); return goto_next()
        if m == 'TXS': self.i('mov\tr9,r6'); return goto_next()
        if m in ('INX', 'DEX', 'INY', 'DEY'):
            r = X if m[2] == 'X' else Y
            self.i('add\t#%d,%s' % (1 if m[0] == 'I' else -1, r), 'extu.b\t%s,%s' % (r, r)); self.zn(r); return goto_next()
        if m in ('SAX', 'SAY', 'SXY'):
            p, q = {'SAX': (A, X), 'SAY': (A, Y), 'SXY': (X, Y)}[m]
            if p == A: self.za()
            self.keep(p); self.keep(q); self.i('mov\t%s,r0' % p, 'mov\t%s,%s' % (q, p), 'mov\tr0,%s' % q); return goto_next()
        if m in ('CLA', 'CLX', 'CLY'):
            r = {'CLA': A, 'CLX': X, 'CLY': Y}[m]; self.keep(r); self.i('mov\t#0,%s' % r)
            if r == A: self.az = True
            return goto_next()
        if m in ('CLC', 'SEC'):
            # the ADC (SBC) right after it, reached only from here: C known there, r13 written by it (docs/30)
            if KNOWN_C and nxt == next_lab and nxt in HC and nxt not in self.Tset and nxt not in b.patched \
                    and nxt not in b.dynops and R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, nxt)[0] \
                    == ('ADC' if m == 'CLC' else 'SBC'):
                self.ck = int(m == 'SEC'); return goto_next()
            self.i('mov\t#%d,r13' % (m == 'SEC')); return goto_next()
        if m in ('CLD', 'SED', 'CLI', 'SEI'):
            bit = 8 if m[2] == 'D' else 4
            self.i('mov.b\t@(%d,gbr),r0' % self.o['P'])
            self.i(('and\t#%d,r0' % (~bit & 0xFF)) if m[:2] == 'CL' else ('or\t#%d,r0' % bit))
            self.i('mov.b\tr0,@(%d,gbr)' % self.o['P'])
            if m == 'SED': self.leave((base + nxt) & 0xFFFF); return None   # (decimal mode: the runtime's)
            if m == 'CLI': self.flush(); self.irqchk((base + nxt) & 0xFFFF)
            return goto_next()
        if m == 'CLV': self.i('mov\t#0,r0', 'mov.b\tr0,@(%d,gbr)' % self.o['vf']); return goto_next()
        if m in ('PHA', 'PHX', 'PHY'): self.push_reg({'PHA': A, 'PHX': X, 'PHY': Y}[m]); return goto_next()
        if m == 'PHP':
            self.mat(); self.call('_hs_getp'); self.i('or\t#16,r0', 'mov\tr0,r3'); self.push_reg('r3'); return goto_next()
        if m in ('PLA', 'PLX', 'PLY'):
            r = {'PLA': A, 'PLX': X, 'PLY': Y}[m]; self.pop(r, ext=not (r == A and LAZY_A))
            if r == A: self.az = not LAZY_A
            self.zn(r); return goto_next()
        if m == 'PLP':
            self.pop('r0'); self.call('_hs_setp'); self.zs, self.ns = 'r11', 'r12'
            self.flush(); self.irqchk((base + nxt) & 0xFFFF); self.dchk((base + nxt) & 0xFFFF); return goto_next()
        if m in ('NOP', '???'): return goto_next()
        if m in ('ST1', 'ST2'):
            self.flush(); k = self.u(); done = '.Lsd%d' % k
            pair = None                           # ST1 then ST2: the first one's way to the runtime (a timing
            if PORT_PAIR and m == 'ST1' and nxt in HC and nxt not in self.Tset and nxt not in b.patched \
                    and nxt not in b.dynops and int(b.data[nxt]) == 0x23:   # register: RCR...) does both (docs/31)
                pair = (int(b.data[nxt + 1]), self.CYC[0x23], '.Lp2_%d' % k); self.pair_at = (nxt, pair[2])
            elif PAIR_RUN and m == 'ST1':
                pair = self.pair_run(i, b, base, nxt, HC)
                if pair: self.pair_at = (pair[2], pair[3])
            self.imm('r2', ob[0])
            self.vdc_data(m == 'ST2', 0x1FE003 if m == 'ST2' else 0x1FE002, done, pair)
            if m == 'ST2' and self.pair_at and self.pair_at[0] == off: self.a.l(self.pair_at[1]); self.pair_at = None
            return goto_next()
        if m in ('ST0', 'ST1', 'ST2'):
            self.flush(); k = self.u(); done = '.Lsd%d' % k
            sub = {'ST0': '_hs_vdc0', 'ST1': '_hs_vdc2', 'ST2': '_hs_vdc3'}[m]
            self.imm('r2', ob[0]); self.call(sub); self.a.b('bt', done)
            self.a.ld(4, 'r1', {'ST0': 0x1FE000, 'ST1': 0x1FE002, 'ST2': 0x1FE003}[m]); self.call('_hs_wr_phys')
            self.a.l(done); return goto_next()
        if m == 'TMA':
            bits = [k for k in range(8) if ob[0] & (1 << k)]
            if bits: self.keep(A); self.i('mov.b\t@(%d,gbr),r0' % (self.o['MPR'] + bits[-1]), 'extu.b\tr0,r8'); self.az = True
            return goto_next()

        # control flow
        if m in R.COND and m != 'BRA':
            tgt = off + 2 + ((ob[0] ^ 0x80) - 0x80)
            opb = self.cond(m)
            if self.cold_branch and self.cold_branch[1] == tgt and -127 <= self.pend + 2 <= 128:
                # the way on is a cold run, placed out of the hot path: the branch turned round to it (its cycles
                # in the slot, run on both ways: the way on starts 2 ahead), the target the next label (docs/32)
                self.mat_for(self.need[i].get(tgt, ALL))
                self.a.b(('bf' if opb == 'bt' else 'bt') + '/s', self.cold_branch[0])
                self.i('add\t#%d,r7' % -(self.pend + 2))
                self.cold_state = (-2, self.zs, self.ns, self.az); self.pend = 0
                return tgt
            if COLD_OOL and nxt not in HC and tgt == next_lab and tgt > off and -127 <= self.pend + 2 <= 128:
                # the way on leaves (never run in the windows): out of line, the target the next label (docs/31)
                self.mat_for(self.need[i].get(tgt, ALL))
                co = '.Lco%d' % self.u()
                self.a.b(('bf' if opb == 'bt' else 'bt') + '/s', co); self.i('add\t#%d,r7' % -(self.pend + 2))
                self.pend = 0
                def cl(co=co, st=(self.zs, self.ns, self.az), pc=(base + nxt) & 0xFFFF):
                    sv = (self.pend, self.zs, self.ns, self.az)
                    self.pend = -2; self.zs, self.ns, self.az = st
                    self.leave(pc)
                    self.pend, self.zs, self.ns, self.az = sv
                self.near(lambda co=co, cl=cl: (self.a.l(co), cl()))   # (placed after the next end)
                return tgt
            self.branch(i, b, base, off, opb, tgt, HC, m); return goto_next()
        if m == 'BRA':
            tgt = off + 2 + ((ob[0] ^ 0x80) - 0x80)
            self.cy(2); self.jump_rel(i, b, base, off, tgt, HC, m); return None
        if m[:3] in ('BBR', 'BBS'):
            bit = 1 << int(m[3]); tgt = off + 3 + ((ob[1] ^ 0x80) - 0x80)
            self.r0k(ob[0] - 128); self.i('mov.b\t@(r0,r14),r0', 'tst\t#%d,r0' % bit)
            self.branch(i, b, base, off, 'bt' if m[:3] == 'BBR' else 'bf', tgt, HC, m); return goto_next()
        if m == 'JMP' and mode == 'abs':
            self.jump_abs(i, b, base, off, ob[0] | ob[1] << 8, HC, m); return None
        if m == 'JMP' and mode in ('ind', 'iax'):
            p = ob[0] | ob[1] << 8
            if mode == 'ind': self.rd16_const(p)
            elif p >> 13 == 1 and (p + 0x100) >> 13 == 1:
                d_ = self.ram_off(p)
                self.r0k(d_); self.i('add\tr9,r0', 'mov.b\t@(r0,r14),r1'); self.r0k(d_ + 1)
                self.i('add\tr9,r0', 'mov.b\t@(r0,r14),r0', 'extu.b\tr1,r1', 'shll8\tr0', 'extu.w\tr0,r0', 'or\tr0,r1')
            else:
                self.imm_sx('r1', p); self.i('add\tr9,r1', 'extu.w\tr1,r1')
                # (the table inside one slot whatever X: the two bytes on one page, one look, docs/32)
                self.rd16_ptr(one_page=RD16_PAGE and p >> 13 and (p + 0x100) >> 13 == p >> 13)
            self.flush(); self.mat(); self.chain(i, b, base, off, True); self.local(8); return None
        if m in ('JSR', 'BSR'):
            r = (base + off + (2 if m == 'JSR' else 1)) & 0xFFFF
            if m == 'JSR' and INLINE_LEAF:
                lf = self.leaf_body(i, base, off, ob[0] | ob[1] << 8)
                if lf: return self.inline_leaf(i, b, base, off, ob[0] | ob[1] << 8, r, lf, goto_next)
            self.push2_const(r)
            if m == 'JSR': self.jump_abs(i, b, base, off, ob[0] | ob[1] << 8, HC, m)
            else: self.cy(2); self.jump_rel(i, b, base, off, off + 2 + ((ob[0] ^ 0x80) - 0x80), HC, m)
            return None
        if m == 'RTS':
            self.pop2(1)
            self.flush(); self.mat(); self.poll_r1()
            self.chain(i, b, base, off, False, sx=True); self.local(base >> 13); return None
        if m == 'RTI':
            self.pop('r0'); self.i('and\t#%d,r0' % (~0x20 & 0xFF)); self.call('_hs_setp'); self.zs, self.ns = 'r11', 'r12'
            self.pop2()
            self.flush(); self.mat()
            self.i('mov.b\t@(%d,gbr),r0' % self.o['P'], 'tst\t#8,r0'); self.a.b('bf', '.Lleave')   # (D set)
            self.i('mov.b\t@(%d,gbr),r0' % self.o['irq_wait'], 'tst\tr0,r0'); self.a.b('bf', '.Lleave')   # (an
            # interrupt waits: the jumps to other slots no longer leave for it while the I flag holds it, docs/24)
            self.chain(i, b, base, off, True, sx=True); self.local(8); return None
        if m == 'TAM':
            self.flush(); self.mat(); self.za()
            for k in range(8):
                if ob[0] & (1 << k): self.set_mpr(k)
            if TAM_DIRECT and nxt in HC and nxt in self.ent_of[i]:
                # the next instruction in this block while its slot still maps it and the runtime would go on at
                # once: what the shared lookup would find (chain()'s test for an entry of another block, docs/27)
                n_ = self.u(); no = '.Ltm%d' % n_
                self.i('mov.l\t@(%d,gbr),r0' % self.o['xmin'], 'cmp/gt\tr0,r7')
                self.i('mov.b\t@(%d,gbr),r0' % (self.o['hid'] + (base >> 13))); self.a.b('bf', no)   # (docs/43)
                self.i('cmp/eq\t#%d,r0' % i); self.a.b('bt', self.lab(i, nxt))
                self.a.l(no)
            self.imm('r1', (base + nxt) & 0xFFFF, u8=False); self.local(8); return None
        raise NotImplementedError('%s %s' % (m, mode))

    def leaf_body(self, i, base, off, A):
        """a JSR's routine to run in line (docs/28): straight (a forward BRA followed), at most 8 instructions, each one
        translated and plain, none that touches the stack, P's I or D, the map or the speed, nor one that leaves; then
        RTS. Its block: the caller's (A an entry of it in its slot) or the one block with an entry at A in A's slot
        (cross() would go there). (block, its data, base, [(offset, decoded)]) or None"""
        R = self.R; s = A >> 13
        if s == base >> 13:
            j = i
            if (A & 0x1FFF) not in self.blk[i][2]: return None     # (else the JSR leaves: the runtime looks)
        else:
            js = self.ent_at.get((s, A & 0x1FFF), [])
            if len(js) != 1: return None
            j = js[0]
        bj, basej, Hj = self.blk[j]
        rd = lambda a: int(bj.data[a & 0x1FFF]) if a < 0x2000 else 0

        def walk(o, body, seen):
            for _ in range(12):
                if not 0 <= o < 0x2000 or o in bj.dynops or o in bj.patched or o in seen: return None
                d = R.decode(rd, o); m, mode = d[0], d[1]
                if m == 'RTS': return body
                if len(body) >= 8: return None
                seen = seen | {o}
                if m == 'BRA':
                    t = o + 2 + ((d[2][0] ^ 0x80) - 0x80)
                    if t <= o: return None
                    body = body + [(o, d)]; o = t; continue
                if m == 'JMP' and mode == 'abs' and LEAF_JMP and (d[2][0] | d[2][1] << 8) >> 13 == s:
                    body = body + [(o, d)]; o = (d[2][0] | d[2][1] << 8) & 0x1FFF; continue   # (docs/31)
                if m in R.COND and LEAF_COND and j == i:
                    # one way on in line, the other out into the routine's code at a label of this block (docs/31)
                    t = o + 2 + ((d[2][0] ^ 0x80) - 0x80); f = o + 2
                    for way, on, out in (('fall', f, t), ('taken', t, f)):
                        if out in self.Tset and out in Hj:
                            r_ = walk(on, body + [(o, d, way, out)], seen)
                            if r_ is not None: return r_
                    return None
                if m in LEAF_NO or m in R.CTRL or m in R.COND or not supported(m, mode, bj, o): return None
                body = body + [(o, d)]; o += d[3]
            return None
        body = walk(A & 0x1FFF, [], frozenset())
        return None if body is None else (j, bj, basej, body)

    def inline_leaf(self, i, b, base, off, A, r, lf, goto_next):
        """JSR, its leaf routine and its RTS in line (docs/28), each as it would run: the return address written below
        S as JSR writes it (S itself as RTS leaves it); JSR's own test (a call back in its slot polls; to another
        slot: the runtime going on at once, its block mapped there), failing: S taken down and the jump as JSR makes
        it; the routine's instructions and cycles; RTS's cycles and its poll, failing: out at the return address;
        back to another slot: the runtime going on at once, else the shared lookup from the return address"""
        j, bj, basej, body = lf
        o = self.o; n_ = self.u(); jo = '.Lfj%d' % n_; ro = '.Lfr%d' % n_
        cross = j != i
        b8 = lambda x: x - 256 if x > 127 else x
        self.i('mov\tr6,r0', 'mov\t#%d,r3' % b8(r >> 8), 'mov.b\tr3,@(r0,r5)', 'add\t#-1,r0', 'extu.b\tr0,r0',
               'mov\t#%d,r3' % b8(r & 0xFF), 'mov.b\tr3,@(r0,r5)')
        if cross or (A & 0x1FFF) <= off:
            self.flush(); self.mat()
            if cross:
                self.i('mov.l\t@(%d,gbr),r0' % o['xmin'], 'cmp/gt\tr0,r7')
                self.i('mov.b\t@(%d,gbr),r0' % (o['hid'] + (A >> 13))); self.a.b('bf', jo)   # (docs/43)
                self.i('cmp/eq\t#%d,r0' % j); self.a.b('bf', jo)
            else:
                self.i('cmp/pz\tr7'); self.a.b('bf', jo)
            def jl(A=A, jo=jo, cross=cross):
                self.a.l(jo); self.i('add\t#-2,r6', 'extu.b\tr6,r6'); self.imm('r1', A, u8=False)
                if cross: self.i('mov\t#8,r3'); self.bra('.Llocal')
                else: self.bra('.Lleave')
                self.a.u()
            self.ool(jo + 'x', jl)
        for k, it in enumerate(body):
            x, d = it[0], it[1]
            if d[0] == 'BRA': self.cy(self.CYC[int(bj.data[x])] + 2); continue
            if d[0] == 'JMP': self.cy(self.CYC[int(bj.data[x])]); continue
            if len(it) == 4:                      # a branch: out (S taken down) to the routine's label, or on
                way, out = it[2], it[3]
                self.cy(self.CYC[int(bj.data[x])])
                opb = self.cond(d[0])
                if way == 'taken': opb = 'bf' if opb == 'bt' else 'bt'
                self.mat_for(self.need[j].get(out, ALL))
                xo, xl = (2, 0) if way == 'fall' else (0, 2)   # (the taken path's 2 cycles)
                p = self.pend + xo
                fx = '.Lfc%d' % self.u()
                if -127 <= p <= 128:              # (the cycles in the slot: on both paths)
                    self.a.b(opb + '/s', fx); self.i('add\t#%d,r7' % -p); self.pend = xl - xo; sp = 0
                else:
                    self.flush(); self.a.b(opb, fx); self.pend = xl; sp = xo
                def cl(fx=fx, out=out, sp=sp):
                    self.a.l(fx)
                    if sp: self.i('add\t#%d,r7' % -sp)
                    self.i('add\t#-2,r6'); self.a.b('bra', self.lab(j, out)); self.i('extu.b\tr6,r6'); self.a.u()
                self.ool(fx + 'x', cl)
                continue
            nx = x + d[3]
            self.ins(j, bj, basej, x, {x, nx}, nx)
            t = self.sel_xfer(bj, basej, x)
            if t: self.sel = t[1] if t[0] == 'set' else None
        self.cy(self.CYC[0x60])                   # RTS
        self.flush(); self.mat()
        ret = (r + 1) & 0xFFFF
        self.i('cmp/pz\tr7'); self.a.b('bf', ro)
        if cross:
            self.i('mov.l\t@(%d,gbr),r0' % o['xmin'], 'cmp/gt\tr0,r7'); self.a.b('bf', ro + 'c')
        def rl(ret=ret, ro=ro, cross=cross, s=A >> 13):
            self.a.l(ro); self.imm('r1', ret, u8=False); self.bra('.Lleave'); self.a.u()
            if cross:
                self.a.l(ro + 'c'); self.imm('r1', ret, u8=False); self.i('mov\t#%d,r3' % s); self.bra('.Llocal')
                self.a.u()
        self.ool(ro + 'x', rl)
        return goto_next()

    def ins_dyn(self, i, b, base, off, m, mode, n, nxt, HC, goto_next):
        """code_dyn's control flow: operand bytes read when it runs"""
        self.flush(); self.mat()
        slot = base >> 13
        if m in self.R.COND:
            opb = self.cond(m) if m != 'BRA' else None
            k = self.u(); nt = '.Lnt%d' % k
            if opb: self.a.b('bf' if opb == 'bt' else 'bt', nt)
            self.code_byte(base, off + 1); self.i('exts.b\tr3,r3')
            self.imm('r1', (base + off + 2) & 0xFFFF, u8=False); self.i('add\tr3,r1', 'extu.w\tr1,r1', 'add\t#-2,r7')
            self.poll_r1(); self.local(slot); self.a.l(nt)
            if m == 'BRA': return None
            return goto_next()
        if m[:3] in ('BBR', 'BBS'):
            k = self.u(); nt = '.Lnt%d' % k
            self.code_byte(base, off + 1); self.i('mov\tr3,r0', 'add\t#-128,r0', 'mov.b\t@(r0,r14),r0',
                                                 'tst\t#%d,r0' % (1 << int(m[3])))
            self.a.b('bf' if m[:3] == 'BBR' else 'bt', nt)
            self.code_byte(base, off + 2); self.i('exts.b\tr3,r3')
            self.imm('r1', (base + off + 3) & 0xFFFF, u8=False); self.i('add\tr3,r1', 'extu.w\tr1,r1', 'add\t#-2,r7')
            self.poll_r1(); self.local(slot); self.a.l(nt)
            return goto_next()
        if m == 'JMP' and mode == 'abs':
            self.code_byte(base, off + 2); self.i('mov.l\tr3,@-r15'); self.code_byte(base, off + 1)
            self.i('mov.l\t@r15+,r1', 'shll8\tr1', 'or\tr3,r1')
            self.poll_r1(); self.local(slot); return None
        if m == 'JMP' and mode in ('ind', 'iax'):
            self.code_byte(base, off + 2); self.i('mov.l\tr3,@-r15'); self.code_byte(base, off + 1)
            self.i('mov.l\t@r15+,r1', 'shll8\tr1', 'or\tr3,r1')
            if mode == 'iax': self.i('add\tr9,r1', 'extu.w\tr1,r1')
            self.rd16_ptr(); self.local(8); return None
        if m in ('JSR', 'BSR'):
            r = (base + off + (2 if m == 'JSR' else 1)) & 0xFFFF
            self.push2_const(r)
            if m == 'JSR':
                self.code_byte(base, off + 2); self.i('mov.l\tr3,@-r15'); self.code_byte(base, off + 1)
                self.i('mov.l\t@r15+,r1', 'shll8\tr1', 'or\tr3,r1')
            else:
                self.code_byte(base, off + 1); self.i('exts.b\tr3,r3')
                self.imm('r1', (base + off + 2) & 0xFFFF, u8=False); self.i('add\tr3,r1', 'extu.w\tr1,r1', 'add\t#-2,r7')
            self.poll_r1(); self.local(slot); return None
        if m == 'TAM':
            self.interp((base + off) & 0xFFFF)
            self.imm('r1', (base + nxt) & 0xFFFF, u8=False); self.local(8); return None
        raise NotImplementedError('dyn %s %s' % (m, mode))

    def chain(self, i, b, base, off, strict, sx=False):
        """r1 = the address: the targets the windows saw this instruction take, tested first (what local_ decides
        for them: in this slot and block, at once; elsewhere - or strict, XLOCAL - if that block is mapped there, the
        runtime would go on, cycles left)"""
        seen = self.seen.get((b.bank, off), [])[:CHAIN_N]
        tests = []
        for B, A in seen:
            j = self.bank_id.get(B)
            if not j or (A >> 13) != (self.base_of[j] >> 13) or (A & 0x1FFF) not in self.ent_of[j]: continue
            same = (A >> 13) == (base >> 13)
            if same and not strict:
                if j == i: tests.append((A, j, False))
            else: tests.append((A, j, True))
        if not tests: return
        rv = 'r1' if sx else 'r2'                 # (sx: r1 is signed 16-bit already: pop2)
        if not sx: self.i('exts.w\tr1,r2')
        for A, j, full in tests:
            L = self.lab(j, A & 0x1FFF)
            self.a.ld(2, 'r0', A - 0x10000 if A >= 0x8000 else A); self.i('cmp/eq\tr0,%s' % rv)
            if not full: self.a.b('bt', L); continue
            n = self.u(); no = '.Lch%d' % n
            self.a.b('bf', no)
            self.i('mov.l\t@(%d,gbr),r0' % self.o['xmin'], 'cmp/gt\tr0,r7')
            self.i('mov.b\t@(%d,gbr),r0' % (self.o['hid'] + (A >> 13))); self.a.b('bf', no)   # (docs/43)
            self.i('cmp/eq\t#%d,r0' % j); self.a.b('bt', L)
            self.a.l(no)

    # -------------------------------------------------------------------------------------------------------------
    def targets(self, i, b, base, HC, labels):
        """the labels something jumps to (they need the settled state: cycles counted, Z and N in r11 / r12)"""
        R = self.R; T = set()
        nextl = {labels[k]: (labels[k + 1] if k + 1 < len(labels) else None) for k in range(len(labels))}
        for off in labels:
            m, mode, ob, n, t = R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, off)
            nxt = off + n
            if nxt in HC and nxt != nextl[off]: T.add(nxt)
            if m == 'SET': T.add(nxt + R.decode(lambda a: int(b.data[a & 0x1FFF]), nxt)[3]); T.add(nxt)
            if m in ('JSR', 'BSR'): T.add(nxt)
            if off in b.dynops or off in b.patched: continue
            if m in R.COND or m == 'BSR': T.add(off + 2 + ((ob[0] ^ 0x80) - 0x80))
            if m[:3] in ('BBR', 'BBS'): T.add(off + 3 + ((ob[1] ^ 0x80) - 0x80))
            if m in ('JMP', 'JSR') and mode == 'abs':
                a = ob[0] | ob[1] << 8
                if a >> 13 == base >> 13: T.add(a & 0x1FFF)
        return T

    def v_live_out(self, b, base, HC):
        """{offset: V may be read after this instruction (before it is set again, or at any exit)} - an ADC / SBC
        whose V nobody reads leaves it uncomputed"""
        R = self.R; info = {}
        for off in HC:
            m, mode, ob, n, t = R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, off)
            nxt = off + n; succ = []; ex = False; rd = False; w = False
            dyn = (off in b.dynops or off in b.patched) and m not in ('RTS', 'RTI', 'BRK')
            if off in b.patched or dyn or not supported(m, mode, b, off) or m in ('PHP', 'BVC', 'BVS', 'CLI'):
                rd = True
            if m in ('ADC', 'SBC', 'BIT', 'TST', 'TSB', 'TRB', 'CLV', 'PLP', 'RTI') and not rd: w = True
            if m in R.COND or m[:3] in ('BBR', 'BBS') or (m == 'JMP' and mode == 'abs') or m in ('JSR', 'BSR'):
                if m in ('JMP', 'JSR'):
                    tgt = ob[0] | ob[1] << 8; ok = tgt >> 13 == base >> 13 and not dyn; tgt &= 0x1FFF
                else:
                    tgt = off + (3 + ((ob[1] ^ 0x80) - 0x80) if m[:3] in ('BBR', 'BBS') else 2 + ((ob[0] ^ 0x80) - 0x80))
                    ok = not dyn
                if ok and 0 <= tgt < 0x2000 and tgt in HC and (tgt > off or m in ('JSR', 'BSR')): succ.append(tgt)
                else: ex = True
                if tgt <= off: ex = True           # (a POLL: an exit)
                if m not in ('BRA', 'JMP', 'JSR', 'BSR'):
                    if nxt in HC: succ.append(nxt)
                    else: ex = True
            elif m in R.CTRL or m == 'PLP':
                ex = True
            else:
                if nxt in HC: succ.append(nxt)
                else: ex = True
            info[off] = (rd, w, succ, ex)
        live_in = {off: True for off in HC}
        for _ in range(64):
            changed = False
            for off in sorted(HC, reverse=True):
                rd, w, succ, ex = info[off]
                out = ex or any(live_in.get(s_, True) for s_ in succ)
                new = rd or (out and not w)
                if new != live_in[off]: live_in[off] = new; changed = True
            if not changed: break
        return {off: (info[off][3] or any(live_in.get(s_, True) for s_ in info[off][2])) for off in HC}

    def flag_need(self, b, base, HC):
        """{offset: the flags (Z, N) the code from that label may read before it sets them} - a jump there settles
        only those (conservative: any exit, call of the interpreter or unknown case reads both)"""
        R = self.R; info = {}
        for off in HC:
            m, mode, ob, n, t = R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, off)
            nxt = off + n; rd = set(); w = set(); succ = []; ex = False
            dyn = (off in b.dynops or off in b.patched) and m not in ('RTS', 'RTI', 'BRK')
            if off in b.patched or dyn or not supported(m, mode, b, off) or m in ('PHP', 'CLI', 'RTS', 'TAM', 'JSR', 'BSR') \
                    or (m == 'JMP' and mode != 'abs'):
                rd |= ALL
            if m in _ZNW: w |= ALL
            if m in ('BNE', 'BEQ'): rd.add('Z')
            if m in ('BMI', 'BPL'): rd.add('N')
            if m in R.COND or m[:3] in ('BBR', 'BBS') or (m == 'JMP' and mode == 'abs'):
                if m == 'JMP': tgt = (ob[0] | ob[1] << 8); ok = tgt >> 13 == base >> 13; tgt &= 0x1FFF
                else:
                    tgt = off + (3 + ((ob[1] ^ 0x80) - 0x80) if m[:3] in ('BBR', 'BBS') else 2 + ((ob[0] ^ 0x80) - 0x80)); ok = True
                if ok and 0 <= tgt < 0x2000 and tgt in HC and tgt > off: succ.append(tgt)
                else: rd |= ALL
                if m not in ('BRA', 'JMP'):
                    if nxt in HC: succ.append(nxt)
                    else: ex = True
            elif m in ('RTI', 'PLP'):
                ex = True
                if m == 'PLP' and nxt in HC: succ.append(nxt)
            elif m in R.CTRL:
                rd |= ALL
            else:
                if nxt in HC: succ.append(nxt)
                else: ex = True
            info[off] = (rd, w, succ, ex)
        live = {off: set(ALL) for off in HC}
        for _ in range(64):
            changed = False
            for off in sorted(HC, reverse=True):
                rd, w, succ, ex = info[off]
                out = set(ALL) if ex else set()
                for s_ in succ: out |= live.get(s_, ALL)
                new = rd | (out - w)
                if new != live[off]: live[off] = new; changed = True
            if not changed: break
        return {o: frozenset(v) for o, v in live.items()}

    def sel_xfer(self, b, base, off):
        """(kind, value): what the instruction at off does to the VDC's selected register: ('set', k), ('lose',) or
        None (nothing)"""
        R = self.R
        m, mode, ob, n, t = R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, off)
        dyn = (off in b.dynops or off in b.patched) and m not in ('RTS', 'RTI', 'BRK')
        if m == 'ST0' and not dyn: return ('set', ob[0] & 0x1F)
        if dyn or not supported(m, mode, b, off): return ('lose',)
        if m in ('STA', 'STX', 'STY', 'STZ', 'ASL', 'LSR', 'ROL', 'ROR', 'INC', 'DEC', 'TSB', 'TRB') and mode != 'acc':
            k = self.kind(mode, ob)
            if k == 'ptr': return ('lose',)
            if k == 'const':
                a = ob[0] | ob[1] << 8
                if a < 0x400 and (a & 3) == 0: return ('lose',)
        return None

    def sel_in(self, b, base, HC, E):
        """{offset: the VDC register selected when the label is reached (an int), or None}: forward over the block's
        own jumps; the entries know nothing"""
        R = self.R; TOP = 'top'; succ = {}; xf = {}
        for off in HC:
            m, mode, ob, n, t = R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, off)
            nxt = off + n; ss = []
            dyn = (off in b.dynops or off in b.patched) and m not in ('RTS', 'RTI', 'BRK')
            if not dyn and (m in R.COND or m[:3] in ('BBR', 'BBS') or m == 'BSR'):
                ss.append(off + (3 + ((ob[1] ^ 0x80) - 0x80) if m[:3] in ('BBR', 'BBS') else 2 + ((ob[0] ^ 0x80) - 0x80)))
            if not dyn and m in ('JMP', 'JSR') and mode == 'abs':
                a = ob[0] | ob[1] << 8
                if a >> 13 == base >> 13: ss.append(a & 0x1FFF)
            if m == 'SET': ss.append(nxt + R.decode(lambda a: int(b.data[a & 0x1FFF]), nxt)[3])
            if m not in ('JMP', 'RTS', 'RTI', 'BRA', 'BRK', 'TAM', 'JSR', 'BSR') and not (m == 'SET'):
                ss.append(nxt)
            succ[off] = [x for x in ss if x in HC]; xf[off] = self.sel_xfer(b, base, off)
        val = {o: (None if o in E else TOP) for o in HC}
        for _ in range(200):
            changed = False
            for off in sorted(HC):
                v = val[off]
                x = xf[off]
                out = v if x is None else (x[1] if x[0] == 'set' else None)
                for t2 in succ[off]:
                    if t2 in E: continue
                    w = val[t2]
                    nw = out if w == TOP else (w if w == out else None)
                    if nw != w: val[t2] = nw; changed = True
            if not changed: break
        return {o: (None if v == TOP else v) for o, v in val.items()}

    def port_runs(self, b, labels, T, HC):
        """{first offset: [offsets]}: runs of two or more ST1 / ST2 one after another, reached only from the run's
        start (its other labels are no targets)"""
        R = self.R; runs = {}; cur = []
        def is_st(o):
            if o in b.dynops or o in b.patched or o not in HC: return False
            return R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, o)[0] in ('ST1', 'ST2')
        for off in labels:
            if is_st(off) and cur and cur[-1] + 2 == off and off not in T: cur.append(off); continue
            if len(cur) >= 2: runs[cur[0]] = cur
            cur = [off] if is_st(off) else []
        if len(cur) >= 2: runs[cur[0]] = cur
        return runs

    def port_run(self, i, b, base, run, HC):
        """a run of ST1 / ST2 at once (the VDC's data port): one test that no event is due before its last write
        (each write's own test holds if the last one's does: the cycles left only go down) and register 2 is
        selected; then the words written, MAWR stepped, the latch left as the run leaves it, the cycles and the
        writes' extra cycles counted, and on to the instruction after the run. Else the instructions one by one."""
        o = self.o; n = self.u(); gen = '.Lpg%d' % n
        self.flush()
        ops = [(int(b.data[x]), int(b.data[x + 1])) for x in run]
        names = [self.R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, x)[0] for x in run]
        cyc = sum(self.CYC[op] for op, v in ops)
        k = cyc + len(run) - 1                   # left0 > left_min1 + k  <=>  the last write's left - 1 > left_min
        assert k <= 127
        self.i('mov.l\t@(%d,gbr),r0' % o['vp_left_min1'], 'add\t#%d,r0' % k, 'cmp/gt\tr0,r7'); self.a.b('bf', gen)
        if self.sel != 2:
            self.i('mov.b\t@(%d,gbr),r0' % o['vp_select'], 'cmp/eq\t#2,r0'); self.a.b('bf', gen)
        his = names.count('ST2')
        if his:
            self.i('mov.l\t@(%d,gbr),r0' % o['vp_vram'], 'mov\tr0,r4', 'mov.w\t@(%d,gbr),r0' % o['vp_inc'], 'extu.w\tr0,r1',
                   'mov.w\t@(%d,gbr),r0' % o['vp_mawr'], 'mov\tr0,r2')
        latch = None
        for (op, v), m in zip(ops, names):
            if m == 'ST1': latch = v; continue
            sk = '.Lpk%d' % self.u()
            self.i('cmp/pz\tr2'); self.a.b('bf', sk); self.i('mov\tr2,r3', 'add\tr3,r3', 'add\tr4,r3')
            if latch is not None:
                w = latch | v << 8
                if w <= 127: self.i('mov\t#%d,r0' % w)
                else: self.a.ld(2, 'r0', w - 0x10000 if w >= 0x8000 else w)
            else:
                self.i('mov.b\t@(%d,gbr),r0' % o['vp_latch'], 'extu.b\tr0,r0', 'mov.l\tr1,@-r15'); self.imm('r1', v); self.i('shll8\tr1', 'or\tr1,r0', 'mov.l\t@r15+,r1')
            self.i('mov.w\tr0,@r3'); self.vmark('r2', 'r3'); self.a.l(sk); self.i('add\tr1,r2', 'exts.w\tr2,r2')
        if his: self.i('mov\tr2,r0', 'mov.w\tr0,@(%d,gbr)' % o['vp_mawr'])
        if latch is not None: self.imm('r0', latch); self.i('mov.b\tr0,@(%d,gbr)' % o['vp_latch'])
        p0, zs0, ns0, az0 = self.pend, self.zs, self.ns, self.az
        self.pend = cyc + len(run)
        after = run[-1] + 2
        if after in HC: self.to_label(i, after)
        else: self.leave((base + after) & 0xFFFF)
        self.pend, self.zs, self.ns, self.az = p0, zs0, ns0, az0
        self.a.l(gen)

    def fill_loops(self, b, groups, HC):
        """{loop head: ('C', w, T, exit, (words, latch, index register))}: a run of ST1 / ST2 pairs, DEX or DEY, BPL
        to the run's start (docs/27): the same words each iteration, X (Y) + 1 iterations"""
        R = self.R; out = {}
        rd = lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0
        for L, run in groups.items():
            ds = [R.decode(rd, x) for x in run]
            if len(run) % 2 or [d[0] for d in ds] != ['ST1', 'ST2'] * (len(run) // 2): continue
            o = run[-1] + 2; tail = []
            for _ in range(2):
                if o not in HC or o in b.dynops or o in b.patched: break
                d = R.decode(rd, o); tail.append((o,) + tuple(d)); o += d[3]
            if len(tail) < 2 or tail[0][1] not in ('DEX', 'DEY') or tail[1][1] != 'BPL': continue
            br = tail[1]
            if br[0] + 2 + ((br[3][0] ^ 0x80) - 0x80) != L: continue
            words = [ds[k][2][0] | ds[k + 1][2][0] << 8 for k in range(0, len(run), 2)]
            cyc = sum(self.CYC[int(b.data[x])] for x in run)
            w = cyc + len(run) - 1
            T = cyc + len(run) + self.CYC[int(b.data[tail[0][0]])] + self.CYC[int(b.data[br[0]])] + 2
            out[L] = ('C', w, T, br[0] + 2, (words, ds[-2][2][0], 'r9' if tail[0][1] == 'DEX' else 'r10'))
        return out

    def mul_loops(self, b, labels, HC):
        """{loop head: ('M', mul, prod, add, count register, cycles skipping, cycles adding, exit)}: the shift-and-add
        multiply (docs/27): LSR m1 / ROR m0 (or LSR m0) / BCC s / LDA p0 / CLC / ADC a0 / STA p0 / (LDA pk / ADC ak /
        STA pk)... / s: ASL a0 / ROL a1 ... / DEY (DEX) / BNE head; every operand a zero-page byte of its own"""
        R = self.R; out = {}
        rd = lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0
        def dec(o):
            if o not in HC or o in b.dynops or o in b.patched: return None
            return (o,) + tuple(R.decode(rd, o))
        def cy(x): return self.CYC[int(b.data[x[0]])]
        for L in labels:
            x = dec(L)
            if not x or x[1] != 'LSR' or x[2] != 'zp': continue
            mul = [x[3][0]]; common = cy(x); o = L + 2; x = dec(o)
            if x and x[1] == 'ROR' and x[2] == 'zp': mul.insert(0, x[3][0]); common += cy(x); o += 2; x = dec(o)
            if not x or x[1] != 'BCC': continue
            common += cy(x); s_ = o + 2 + ((x[3][0] ^ 0x80) - 0x80); o += 2
            P = []; A_ = []; addc = 0; okb = True
            while o < s_ and okb:
                x = dec(o)
                if not x or x[1] != 'LDA' or x[2] != 'zp': okb = False; break
                p_ = x[3][0]; addc += cy(x); o += 2; y = dec(o)
                if not P:
                    if not y or y[1] != 'CLC': okb = False; break
                    addc += cy(y); o += 1; y = dec(o)
                if not y or y[1] != 'ADC' or y[2] != 'zp': okb = False; break
                addc += cy(y); A_.append(y[3][0]); o += 2; y = dec(o)
                if not y or y[1] != 'STA' or y[2] != 'zp' or y[3][0] != p_: okb = False; break
                addc += cy(y); P.append(p_); o += 2
            if not okb or o != s_ or not 1 <= len(P) <= 3: continue
            for k, a_ in enumerate(A_):
                x = dec(o)
                if not x or x[1] != ('ASL' if k == 0 else 'ROL') or x[2] != 'zp' or x[3][0] != a_: okb = False; break
                common += cy(x); o += 2
            if not okb: continue
            x = dec(o)
            if not x or x[1] not in ('DEY', 'DEX'): continue
            common += cy(x); rc = 'r10' if x[1] == 'DEY' else 'r9'; o += 1; x = dec(o)
            if not x or x[1] != 'BNE' or o + 2 + ((x[3][0] ^ 0x80) - 0x80) != L: continue
            common += cy(x) + 2
            if len(set(mul + P + A_)) != len(mul) + 2 * len(P): continue
            out[L] = ('M', mul, P, A_, rc, common + 2, common + addc, o + 2)
        return out

    def mul_loop(self, i, b, base, L, info, HC):
        """the multiply's iterations in registers (docs/27): W (the multiplier) shifted out a bit at a time, the
        addend M added to the product P where it is 1, M shifted; each iteration's cycles (the add's path or BCC's)
        and the back branch's poll as the instructions take them; at the end (or the poll leaving) the zero page
        written back, A and V as the last add left them, C from the last ROL, Z N from the count. Count 0 (256
        iterations): the instructions."""
        _, mul, P, A_, rc, c_skip, c_add, ex = info
        o = self.o; n_ = self.u(); n = len(P); sh = 32 - 8 * n
        slow = '.Lms%d' % n_; lp = '.Lml%d' % n_; na = '.Lmn%d' % n_; nx = '.Lmx%d' % n_; dn = '.Lmd%d' % n_
        wb = '.Lmw%d' % n_; pl = '.Lmp%d' % n_; nd = '.Lmz%d' % n_
        assert self.pend == 0 and c_add <= 128
        def shl(r, k):
            for t in (16, 8, 2, 1):
                while k >= t: self.i('shll%s\t%s' % ('' if t == 1 else str(t), r)); k -= t
        def shr(r, k):
            for t in (16, 8, 2, 1):
                while k >= t: self.i('shlr%s\t%s' % ('' if t == 1 else str(t), r)); k -= t
        def ld_bytes(r, zs_):                     # r = the bytes (zs_ low first) as a number
            self.r0k(zs_[-1] - 128); self.i('mov.b\t@(r0,r14),%s' % r, 'extu.b\t%s,%s' % (r, r))
            for z in reversed(zs_[:-1]):
                self.i('shll8\t%s' % r); self.r0k(z - 128); self.i('mov.b\t@(r0,r14),r0', 'extu.b\tr0,r0', 'or\tr0,%s' % r)
        def st_bytes(r, zs_, k):                  # the bytes of r >> k (zs_ low first) back, through r12
            self.i('mov\t%s,r12' % r); shr('r12', k)
            for j, z in enumerate(zs_):
                if j: self.i('shlr8\tr12')
                self.r0k(z - 128); self.i('mov.b\tr12,@(r0,r14)')
        self.i('tst\t%s,%s' % (rc, rc)); self.a.b('bt', slow)
        ld_bytes('r1', mul); ld_bytes('r2', A_); shl('r2', sh); ld_bytes('r3', P); shl('r3', sh)
        self.i('mov\t#1,r11')                    # (r11: M at the last add; 1: none yet)
        self.a.l(lp)
        self.i('shlr\tr1'); self.a.b('bf', na)
        self.i('mov\tr3,r4', 'mov\tr2,r11', 'add\tr2,r3'); self.a.b('bra', nx); self.i('add\t#%d,r7' % -c_add)
        self.a.l(na); self.i('add\t#%d,r7' % -c_skip)
        self.a.l(nx); self.i('shll\tr2', 'movt\tr13', 'dt\t%s' % rc); self.a.b('bt', dn)
        self.i('cmp/pz\tr7'); self.a.b('bt', lp); self.bra(wb)
        self.a.l(dn); self.i('add\t#2,r7')       # (the last BNE not taken)
        self.a.l(wb)
        st_bytes('r1', mul, 0); st_bytes('r2', A_, sh); st_bytes('r3', P, sh)
        self.i('mov\tr11,r0', 'cmp/eq\t#1,r0'); self.a.b('bt', nd)
        self.i('mov\tr3,r8', 'shlr16\tr8', 'shlr8\tr8',                                   # A: P's high byte
               'mov\tr4,r0', 'xor\tr3,r0', 'mov\tr11,r12', 'xor\tr3,r12', 'and\tr12,r0', 'shlr16\tr0', 'shlr8\tr0',
               'mov.b\tr0,@(%d,gbr)' % o['vf'])                                            # V: the high bytes' add
        self.a.l(nd)
        p0, zs0, ns0, az0 = self.pend, self.zs, self.ns, self.az
        self.zs = self.ns = rc
        self.i('tst\t%s,%s' % (rc, rc)); self.a.b('bf', pl)
        if ex in HC: self.to_label(i, ex)
        else: self.leave((base + ex) & 0xFFFF)
        self.a.l(pl); self.zs = self.ns = rc
        self.leave((base + L) & 0xFFFF)           # (the poll: below 0 after a back branch)
        self.pend, self.zs, self.ns, self.az = p0, zs0, ns0, az0
        self.a.l(slow)

    def copy_loops(self, b, labels, HC):
        """{loop head: (kind, w, T, exit, extra)}: the copies to the VDC's data port in a loop (docs/27).
        A: LDA (z),Y / STA $..2 / INY / LDA (z),Y / STA $..3 / INY / CPY #n / BNE head (extra: z, n);
        B: LDA a,Y / STA $..2 / DEY / LDA a,Y / STA $..3 / DEY / BPL head (extra: a, its kind 'ram' or 'slotx').
        w: the cycles from the head to the second write's test; T: an iteration's cycles, its branch taken."""
        R = self.R; out = {}
        rd = lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0
        def port(x, hi):
            m, mode, ob = x[1], x[2], x[3]
            if m != 'STA' or mode != 'abs': return False
            a = ob[0] | ob[1] << 8
            return a < 0x400 and (a & 3) == (3 if hi else 2)
        for L in labels:
            seq = []; o = L
            for _ in range(8):
                if o not in HC or o in b.dynops or o in b.patched: break
                d = R.decode(rd, o); seq.append((o,) + tuple(d)); o += d[3]
            if len(seq) < 7: continue
            ms = [x[1] for x in seq]
            cyc = [self.CYC[int(b.data[x[0]])] for x in seq]
            if ms[:8] == ['LDA', 'STA', 'INY', 'LDA', 'STA', 'INY', 'CPY', 'BNE'] and len(seq) == 8:
                if not (seq[0][2] == seq[3][2] == 'izy' and seq[0][3][0] == seq[3][3][0] and seq[6][2] == 'imm'):
                    continue
                br = seq[7]; kind = 'A'; extra = (seq[0][3][0], seq[6][3][0])
            elif ms[:7] == ['LDA', 'STA', 'DEY', 'LDA', 'STA', 'DEY', 'BPL']:
                seq = seq[:7]; cyc = cyc[:7]
                if not (seq[0][2] == seq[3][2] == 'aby' and seq[0][3][:2] == seq[3][3][:2]): continue
                a = seq[0][3][0] | seq[0][3][1] << 8
                if a >> 13 == 1 and (a + 0xFF) >> 13 == 1: ak = 'ram'
                elif a >> 13 and (a + 0xFF) >> 13 == a >> 13: ak = 'slotx'
                else: continue
                br = seq[6]; kind = 'B'; extra = (a, ak)
            else:
                continue
            if not (port(seq[1], False) and port(seq[4], True)): continue
            if br[0] + 2 + ((br[3][0] ^ 0x80) - 0x80) != L: continue
            w = sum(cyc[:5]) + 1
            out[L] = (kind, w, sum(cyc) + 2 + 2, br[0] + 2, extra)
        return out

    def copy_loop(self, i, b, base, L, info, HC):
        """a loop to the VDC's data port at once (docs/27): while register 2 is selected, the count known (A: CPY's n
        above Y, n - Y even; B: Y odd, below $80; C: X below $80), the source in one readable slot at an even SH2
        address (a word read at once), MAWR from 0 to $8000 at most at the end (every word written, no wrap), and each
        iteration's last write's test passing (the cycles left above left_min1 + w: MPR0 FF, no VDC event due) and its
        branch's poll not leaving (left above T - 1), the iterations run in registers: the words written, MAWR stepped,
        the latch and A as the last iteration leaves them, the cycles counted. An iteration whose test fails goes to
        the instructions as they are, with the state at its start. Else the instructions one by one."""
        o = self.o; n_ = self.u()
        slow = '.Lcs%d' % n_; lp = '.Lcl%d' % n_; dn = '.Lcd%d' % n_; ok = '.Lco%d' % n_
        kind, w, T, ex, extra = info
        assert self.pend == 0 and w <= 127 and T <= 128
        if self.sel != 2:
            self.i('mov.b\t@(%d,gbr),r0' % o['vp_select'], 'cmp/eq\t#2,r0'); self.a.b('bf', slow)
        if kind == 'C':
            words, latch, xr = extra
            self.i('mov\t%s,r0' % xr, 'tst\t#128,r0'); self.a.b('bf', slow)                   # X below $80
            self.i('mov\t%s,r1' % xr, 'add\t#1,r1')                                            # r1: the words
            m = len(words)
            if m & (m - 1) == 0:
                for _ in range(m.bit_length() - 1): self.i('shll\tr1')
            else: self.i('mov\t#%d,r0' % m, 'mulu.w\tr0,r1', 'sts\tmacl,r1')
        elif kind == 'A':
            z, n = extra
            self.imm('r1', n)
            self.i('mov\tr1,r2', 'sub\tr10,r2', 'cmp/pl\tr2'); self.a.b('bf', slow)            # n - Y > 0
            self.i('mov\tr2,r0', 'tst\t#1,r0'); self.a.b('bf', slow)                            # and even
            if z & 1 == 0:
                self.r0k(z - 128); self.i('mov.w\t@(r0,r14),r3', 'swap.b\tr3,r3', 'extu.w\tr3,r3')
            else:
                self.r0k(z - 128); self.i('mov.b\t@(r0,r14),r3'); self.r0k(((z + 1) & 0xFF) - 128)
                self.i('mov.b\t@(r0,r14),r0', 'extu.b\tr3,r3', 'shll8\tr0', 'extu.w\tr0,r0', 'or\tr0,r3')
            self.i('mov\tr3,r4', 'add\tr1,r4', 'add\t#-1,r4', 'extu.w\tr4,r4',                 # the last byte read
                   'add\tr10,r3', 'extu.w\tr3,r3',                                                # the first
                   'mov\tr3,r0', 'xor\tr4,r0', 'shlr8\tr0', 'tst\t#224,r0'); self.a.b('bf', slow)   # one slot
            self.i('swap.b\tr3,r0', 'shlr2\tr0', 'shlr\tr0', 'and\t#28,r0'); self.a.ld(4, 'r4', '_huc_pg')
            self.i('mov.l\t@(r0,r4),r4', 'tst\tr4,r4'); self.a.b('bt', slow)
            self.i('add\tr4,r3', 'mov\tr2,r1', 'shlr\tr1')                     # r3: its SH2 address; r1: the words
        else:
            a, ak = extra
            self.i('mov\tr10,r0', 'and\t#129,r0', 'cmp/eq\t#1,r0'); self.a.b('bf', slow)       # Y odd, below $80
            if ak == 'ram':
                self.r0k(self.ram_off(a) - 1); self.i('add\tr10,r0', 'mov\tr0,r3', 'add\tr14,r3')
            else:
                self.i('mov.l\t@(%d,gbr),r0' % (o['rp'] + 4 * (a >> 13)), 'tst\tr0,r0'); self.a.b('bt', slow)
                self.i('mov\tr0,r3'); self.r0k((a & 0x1FFF) - 1); self.i('add\tr10,r0', 'add\tr0,r3')
            self.i('mov\tr10,r1', 'add\t#1,r1', 'shlr\tr1')     # r3: a + Y - 1 (the word: its byte high, a + Y low)
        if kind != 'C':
            self.i('mov\tr3,r0', 'tst\t#1,r0'); self.a.b('bf', slow)                            # (a word read)
        # MAWR + inc * words within 0 .. $8000
        self.i('mov.w\t@(%d,gbr),r0' % o['vp_inc'], 'mulu.w\tr0,r1', 'mov.w\t@(%d,gbr),r0' % o['vp_mawr'], 'cmp/pz\tr0')
        self.a.b('bf', slow)
        self.i('sts\tmacl,r1', 'add\tr0,r1'); self.a.ld(4, 'r0', 0x8000); self.i('cmp/hi\tr0,r1'); self.a.b('bt', slow)
        self.i('mov.l\t@(%d,gbr),r0' % o['vp_left_min1'], 'add\t#%d,r0' % w, 'mov\t#%d,r4' % (T - 1), 'cmp/gt\tr4,r0')
        self.a.b('bf', ok); self.i('mov\tr0,r4'); self.a.l(ok)                     # r4 = max(left_min1 + w, T - 1)
        self.i('cmp/gt\tr4,r7'); self.a.b('bf', slow)
        # (from here: r12 the limit, r11 VRAM, r4 inc * 2, r0 MAWR * 2, r1 the word; the count: r10 (A, B), X / Y (C))
        self.i('mov\tr4,r12')
        if kind == 'A': self.i('shlr\tr2', 'mov\tr2,r10')
        elif kind == 'B': self.i('add\t#1,r10', 'shlr\tr10')
        else: self.i('add\t#1,%s' % xr)
        self.i('mov.l\t@(%d,gbr),r0' % o['vp_vram'], 'mov\tr0,r11', 'mov.w\t@(%d,gbr),r0' % o['vp_inc'], 'extu.w\tr0,r4',
               'add\tr4,r4', 'mov.w\t@(%d,gbr),r0' % o['vp_mawr'], 'add\tr0,r0')
        same = kind == 'C' and len(set(extra[0])) == 1
        def wd(v):
            if v <= 127: self.i('mov\t#%d,r1' % v)
            else: self.a.ld(2, 'r1', v - 0x10000 if v >= 0x8000 else v)
        if same: wd(extra[0][0])
        self.a.l(lp)
        if kind == 'C':
            for v in words:
                if not same: wd(v)
                self.i('mov.w\tr1,@(r0,r11)', 'add\tr4,r0')
        elif kind == 'A':
            self.i('mov.w\t@r3+,r1', 'swap.b\tr1,r1', 'mov.w\tr1,@(r0,r11)', 'add\tr4,r0')   # (the bytes low, high)
        else:
            self.i('mov.w\t@r3,r1', 'add\t#-2,r3', 'mov.w\tr1,@(r0,r11)', 'add\tr4,r0')
        self.i('add\t#%d,r7' % -T, 'dt\t%s' % ('r10' if kind != 'C' else xr)); self.a.b('bt', dn)
        self.i('cmp/gt\tr12,r7'); self.a.b('bt', lp)
        # an iteration's test failed: the state at its start (the back branch's: CPY's flags / DEX's / DEY's), on to
        # the instructions
        def out():                                # MAWR, the latch; A
            self.i('shlr\tr0')
            if MARKS and not __import__('os').environ.get('SPLIT_MARKS_NOSPAN'): self.i('mov\tr0,r2'); self.call('_hs_vspan'); self.i('mov\tr2,r0')   # (r2: free here)
            self.i('mov.w\tr0,@(%d,gbr)' % o['vp_mawr'])
            if kind == 'C': self.imm('r0', latch)
            else: self.i('mov\tr1,r8', 'shlr8\tr8', 'extu.b\tr8,r8', 'mov\tr1,r0')
            self.i('mov.b\tr0,@(%d,gbr)' % o['vp_latch'])
        out()
        if kind == 'A':
            self.i('mov\tr10,r0', 'add\tr0,r0', 'neg\tr0,r11', 'extu.b\tr11,r11', 'mov\tr11,r12'); self.imm('r1', n)
            self.i('sub\tr0,r1', 'mov\tr1,r10', 'mov\t#0,r13')                                   # Y = n - 2k, C 0
        elif kind == 'B':
            self.i('add\tr10,r10', 'add\t#-1,r10', 'mov\tr10,r11', 'mov\tr10,r12')              # Y = 2k - 1
        else:
            self.i('add\t#-1,%s' % xr, 'mov\t%s,r11' % xr, 'mov\t%s,r12' % xr)                  # X = k - 1
        self.bra(slow)
        self.a.l(dn)                              # the end: the last branch not taken (2 cycles less)
        out(); self.i('add\t#2,r7')
        p0, zs0, ns0, az0 = self.pend, self.zs, self.ns, self.az
        if kind == 'A':
            self.imm('r10', n); self.i('mov\t#0,r11', 'mov\t#1,r13'); self.zs = self.ns = 'r11'
        else:
            r = 'r10' if kind == 'B' else xr
            self.i('mov\t#-1,%s' % r, 'extu.b\t%s,%s' % (r, r)); self.zs = self.ns = r
        if ex in HC: self.to_label(i, ex)
        else: self.leave((base + ex) & 0xFFFF)
        self.pend, self.zs, self.ns, self.az = p0, zs0, ns0, az0
        self.a.l(slow)

    def copy_entry(self, i, b, base, ldy, L, info, HC):
        """loop B entered from LDY #n just before it (docs/30): the count known (n odd, below $80: (n + 1) / 2 words),
        the source base RAM at fixed offsets, so the loop's words written straight, one test for them all (each
        iteration's last write after left_min1, each back branch's poll), MAWR's range by a shift, then the state as
        the loop leaves it; else on into the loop's head as it is. Only from the LDY's fall into it"""
        o = self.o; kind, w, T, ex, (a, ak) = info
        rd = lambda x: int(b.data[x & 0x1FFF]) if x < 0x2000 else 0
        m, mode, ob, n, t = self.R.decode(rd, ldy)
        y = ob[0]; k = (y + 1) >> 1
        if ak != 'ram' or (a + y - 1) & 1 or k & (k - 1): return False      # (words at even offsets of r14; k 2^j)
        c1 = w + (k - 1) * T; c2 = (k - 1) * T - 1; tot = k * T - 2
        if c1 > 127 or c2 > 127 or tot > 128: return False
        n_ = self.u(); fail = '.Lce%d' % n_; okl = '.Lcf%d' % n_
        self.flush()
        if self.sel != 2:
            self.i('mov.b\t@(%d,gbr),r0' % o['vp_select'], 'cmp/eq\t#2,r0'); self.a.b('bf', fail)
        self.i('mov.l\t@(%d,gbr),r0' % o['vp_left_min1'], 'add\t#%d,r0' % c1, 'mov\t#%d,r4' % c2, 'cmp/gt\tr4,r0')
        self.a.b('bf', okl); self.i('mov\tr0,r4'); self.a.l(okl)
        self.i('cmp/gt\tr4,r7'); self.a.b('bf', fail)
        self.i('mov.w\t@(%d,gbr),r0' % o['vp_inc'], 'extu.w\tr0,r1')
        for _ in range(k.bit_length() - 1): self.i('shll\tr1')
        self.i('mov.w\t@(%d,gbr),r0' % o['vp_mawr'], 'extu.w\tr0,r2', 'add\tr2,r1'); self.a.ld(4, 'r0', 0x8000)
        self.i('cmp/hi\tr0,r1'); self.a.b('bt', fail)
        self.i('mov.l\t@(%d,gbr),r0' % o['vp_vram'], 'mov\tr0,r3', 'mov.w\t@(%d,gbr),r0' % o['vp_inc'], 'extu.w\tr0,r4',
               'add\tr4,r4', 'add\tr2,r2')
        for j in range(k):
            self.r0k(self.ram_off(a) + y - 1 - 2 * j)
            self.i('mov.w\t@(r0,r14),r1', 'mov\tr2,r0', 'mov.w\tr1,@(r0,r3)', 'add\tr4,r2')
        self.i('mov\tr2,r0', 'shlr\tr0')
        if MARKS and not __import__('os').environ.get('SPLIT_MARKS_NOSPAN'): self.i('mov\tr0,r2'); self.call('_hs_vspan'); self.i('mov\tr2,r0')
        self.i('mov.w\tr0,@(%d,gbr)' % o['vp_mawr'],
               'mov\tr1,r8', 'shlr8\tr8', 'extu.b\tr8,r8', 'mov\tr1,r0', 'mov.b\tr0,@(%d,gbr)' % o['vp_latch'],
               'add\t#%d,r7' % -tot, 'mov\t#-1,r10', 'extu.b\tr10,r10')
        p0, zs0, ns0, az0 = self.pend, self.zs, self.ns, self.az
        self.zs = self.ns = 'r10'
        if ex in HC: self.to_label(i, ex)
        else: self.leave((base + ex) & 0xFFFF)
        self.pend, self.zs, self.ns, self.az = p0, zs0, ns0, az0
        self.a.l(fail)
        return True

    def block(self, i, key, b, base, HC, E):
        labels = sorted(HC)
        T = self.targets(i, b, base, HC, labels) | set(E)
        self.Tset = T; self.ck = None; self.pair_at = None
        selin = self.sel_in(b, base, HC, E)
        groups = self.port_runs(b, labels, T, HC)
        for g in groups.values():
            if g[-1] + 2 in HC: T.add(g[-1] + 2)
        loops = self.copy_loops(b, labels, HC) if COPY_LOOPS else {}
        if COPY_LOOPS > 1: loops.update(self.fill_loops(b, groups, HC))
        if MUL_LOOPS: loops.update(self.mul_loops(b, labels, HC))
        for L, info in list(loops.items()):
            if info[0] == 'M':
                if L not in T: del loops[L]; continue
                if info[7] in HC: T.add(info[7])
                continue
            if L not in T or (selin.get(L) is not None and selin.get(L) != 2): del loops[L]; continue
            if info[3] in HC: T.add(info[3])
        self.a.raw('/* ---- %s: block %d, slot %d, %d labels ---- */' % (key, i, base >> 13, len(labels)))
        cold = self.cold_runs(i, b, labels, HC, loops, groups) if COLD_RUNS else {}
        fall = None; k = 0
        while k < len(labels):
            off = labels[k]
            if fall is None:
                self.pend = 0; self.zs, self.ns = 'r11', 'r12'; self.az = False
            elif off in T:
                self.flush(); self.mat_for(self.need[i].get(off, ALL)); self.az = False
            self.a.l(self.lab(i, off))
            hk = HOOK_IDS.get((int(key[:2], 16), off)) if MARKS and not __import__('os').environ.get('SPLIT_MARKS_NOHOOK') else None
            if hk is not None:                    # (docs/54: m2_recomp.py's HOOKS, in the hot code too)
                self.flush(); self.i('mov\t#%d,r1' % hk); self.call('_hs_hook'); self.a.r0v = None
            if CDSTEP and (int(key[:2], 16), off) == CDSTEP_AT:   # (docs/75: rt_cdstep, as the C's hook after the LDA's
                self.flush(); self.adj(5); self.call('_hs_cdstep'); self.adj(-5); self.a.r0v = None   # 5 cycles)
                CD_USED.add('_hs_cdstep')
            if CDSTEP and (int(key[:2], 16), off) == ADSTEP_AT:   # (docs/76: rt_adstep, after the TST's 8 cycles)
                self.flush(); self.adj(8); self.call('_hs_adstep'); self.adj(-8); self.a.r0v = None
                CD_USED.add('_hs_adstep')
            if CDSTEP and (int(key[:2], 16), off) in PSKIP_AT:   # (docs/77: rt_pollskip, after the BRA's first 2 cycles;
                self.flush(); self.adj(2); self.i('mov\t#%d,r1' % PSKIP_AT[(int(key[:2], 16), off)])   # a literal call:
                self.call('_hs_pskip'); self.adj(-2); self.a.r0v = None                          # SUBS is full)
                CD_USED.add('_hs_pskip')
            self.sel = selin.get(off)
            if off in loops:
                if loops[off][0] == 'M': self.mul_loop(i, b, base, off, loops[off], HC)
                else: self.copy_loop(i, b, base, off, loops[off], HC)
            if off in groups: self.port_run(i, b, base, groups[off], HC)
            cr = cold.get(off)
            if cr: self.cold_branch = ('.Lcr%d' % self.u(), cr[0]); self.cold_state = None
            fall = self.ins(i, b, base, off, HC, labels[k + 1] if k + 1 < len(labels) else None)
            x = self.sel_xfer(b, base, off)
            if x: self.sel = x[1] if x[0] == 'set' else None
            if COPY_ENTRY and fall is not None and fall in loops and loops[fall][0] == 'B' and off == fall - 2 \
                    and int(b.data[off]) == 0xA0 and off not in b.patched and off not in b.dynops \
                    and (int(b.data[off + 1]) & 0x81) == 1:
                self.copy_entry(i, b, base, off, fall, loops[fall], HC)
            if cr and self.cold_state is not None:   # the run's code, placed after the next end (docs/32)
                lab = self.cold_branch[0]; self.cold_branch = None
                sv = self.a.items; self.a.items = []; self.a.r0v = None
                self.a.l(lab)
                self.pend, self.zs, self.ns, self.az = self.cold_state; self.cold_state = None
                sf = 'on'; run = cr[1]
                for j, x_ in enumerate(run):
                    if sf is None: self.pend = 0; self.zs, self.ns = 'r11', 'r12'; self.az = False
                    elif x_ in T: self.flush(); self.mat_for(self.need[i].get(x_, ALL)); self.az = False
                    self.a.l(self.lab(i, x_))
                    self.sel = selin.get(x_)
                    sf = self.ins(i, b, base, x_, HC, run[j + 1] if j + 1 < len(run) else None)
                    xx = self.sel_xfer(b, base, x_)
                    if xx: self.sel = xx[1] if xx[0] == 'set' else None
                assert sf is None
                side = self.a.items; self.a.items = sv; self.a.r0v = None
                self.a.items.append(('n', side))
                k = labels.index(cr[0]); continue
            self.cold_branch = None
            k += 1
        assert fall is None
        self.a.l('L%d_OOL' % i)                   # (the block's slow paths: a symbol of their own for the profile)
        self.drain()

    def costmap(self, path):
        """DIAG (docs/45): each game instruction's SH2 instructions in the hot path (from its label to the next, the
        out-of-line parts left out) with its runs in the windows: "bank offset runs sh2 mnemonic mode" lines"""
        import re
        bank = {i: b.bank for (i, key, b, base, H, E) in self.parts}
        blk = {i: b for (i, key, b, base, H, E) in self.parts}
        cur = None; n = 0; out = []
        def put():
            if cur is None: return
            i, off = cur; b = blk[i]
            d = self.R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, off)
            out.append('%02X %04X %d %d %s %s' % (bank[i], off, COUNTS.get((bank[i], off), 0), n, d[0], d[1]))
        for it in self.a.items:
            if it[0] == 'l':
                m = re.fullmatch(r'L(\d+)_([0-9A-F]{4})', it[1])
                if m: put(); cur = (int(m.group(1)), int(m.group(2), 16)); n = 0
                elif re.fullmatch(r'L\d+_OOL', it[1]): put(); cur = None
            elif cur is not None and it[0] in ('i', 'ld'): n += 1
            elif cur is not None and it[0] == 'b': n += 2 if it[1] in ('bra', 'bt/s', 'bf/s') else 1
            elif cur is not None and it[0] == 'd': n += 2
        put()
        open(path, 'w').write('\n'.join(out) + '\n')

    def cold_runs(self, i, b, labels, HC, loops, groups):
        """{branch offset: (target, [the run's labels])}: a forward conditional branch of the hot code whose way on,
        to its target, ran at most a quarter as often as the branch in the windows (docs/32)"""
        R = self.R; out = {}; pos = {o: k for k, o in enumerate(labels)}
        cnt = lambda o: COUNTS.get((b.bank, o), 0)
        for k, off in enumerate(labels):
            if off in b.dynops or off in b.patched: continue
            d = R.decode(lambda a: int(b.data[a & 0x1FFF]) if a < 0x2000 else 0, off)
            if d[0] not in R.COND or d[0] == 'BRA': continue
            tgt = off + 2 + ((d[2][0] ^ 0x80) - 0x80); nxt = off + 2
            if tgt <= nxt or tgt not in pos or nxt not in pos or cnt(off) < COLD_MIN: continue
            run = labels[k + 1:pos[tgt]]
            if not run or run[0] != nxt or len(run) > 64: continue
            if max(cnt(x) for x in run) * 4 > cnt(off): continue
            if any(x in loops or x in groups for x in run) or tgt in groups: continue
            out[off] = (tgt, run)
        for off in list(out):                     # (no run inside another's)
            if any(off in r for t, r in out.values()): del out[off]
        return out

    def drain(self):
        while self.ooL:
            lab, fn = self.ooL.pop(0)
            self.cur_w = 0; self.a.l(lab); fn(); self.a.u()

    # -- the function's entry, its exits, the lookup, the shared subroutines --
    def lookup(self, miss, slot_in_r4=False, entry2=None):
        """r1 = the address (u16): the entry label of the block mapped in its slot, else miss (r1 kept); entry2: a
        second entry with the slot already in r4 (.Llocal's, docs/28)"""
        o = self.o; a = self.a; n = self.u(); probe = '.Lpr%d' % n; hit = '.Lht%d' % n
        if not slot_in_r4: self.i('mov\tr1,r4', 'shlr8\tr4', 'shlr2\tr4', 'shlr2\tr4', 'shlr\tr4')
        if entry2: a.l(entry2)
        self.i('mov\tr4,r0')
        h = o['hid']
        if h <= 127: self.i('add\t#%d,r0' % h)
        else: self.i('add\t#127,r0', 'add\t#%d,r0' % (h - 127))
        self.i('stc\tgbr,r2', 'mov.b\t@(r0,r2),r2', 'tst\tr2,r2'); a.b('bt', miss)   # (block ids below 128)
        self.i('mov\tr2,r3', 'shll16\tr3', 'or\tr1,r3',
               'mov\tr1,r0', 'shlr8\tr0', 'xor\tr1,r0', 'shll2\tr2', 'add\tr2,r2', 'xor\tr2,r0')
        a.ld(2, 'r2', HMASK); self.i('and\tr2,r0', 'shll2\tr0', 'add\tr0,r0'); a.ld(4, 'r2', '_hs_tab'); self.i('add\tr0,r2')
        a.l(probe)
        self.i('mov.l\t@r2,r0', 'cmp/eq\tr3,r0'); a.b('bt', hit)
        self.i('tst\tr0,r0'); a.b('bt', miss)
        self.bra(probe, 'add\t#8,r2'); a.u()
        a.l(hit); self.i('mov.l\t@(4,r2),r0'); a.d('jmp\t@r0', 'nop'); a.u()

    def prologue(self):
        o = self.o; a = self.a
        pad = int(__import__('os').environ.get('SPLIT_PAD', '0'))   # (bytes before the hot code: its layout moved, for a mean over layouts)
        a.raw('\t.text\n\t.align 2\n' + ('\t.space\t%d\n' % pad if pad else '') + '\t.global\t_hot_stage\n_hot_stage:')
        for r in range(8, 15): self.i('mov.l\tr%d,@-r15' % r)
        self.i('sts.l\tpr,@-r15', 'stc.l\tgbr,@-r15', 'ldc\tr4,gbr',
               'mov.l\t@(%d,gbr),r0' % o['ram'], 'mov\tr0,r14', 'add\t#127,r14', 'add\t#1,r14',
               'mov\tr14,r5', 'add\t#127,r5', 'add\t#1,r5')
        for f, r in (('A', 'r8'), ('X', 'r9'), ('Y', 'r10'), ('S', 'r6')):
            self.i('mov.b\t@(%d,gbr),r0' % o[f], 'extu.b\tr0,%s' % r)
        self.i('mov.l\t@(%d,gbr),r0' % o['left'], 'mov\tr0,r7', 'mov.b\t@(%d,gbr),r0' % o['P'], 'tst\t#8,r0')
        a.b('bf', '.Lmiss')                       # (D set: the C code or the interpreter, docs/24)
        self.call('_hs_setp')
        self.i('mov.w\t@(%d,gbr),r0' % o['pc'], 'extu.w\tr0,r1')
        self.lookup('.Lmiss')
        a.l('.Lmiss'); self.i('mov\t#3,r0', 'mov.b\tr0,@(%d,gbr)' % o['miss'])
        a.l('.Lret'); self.i('ldc.l\t@r15+,gbr', 'lds.l\t@r15+,pr')
        for r in range(14, 8, -1): self.i('mov.l\t@r15+,r%d' % r)
        a.d('rts', 'mov.l\t@r15+,r8'); a.u()
        # leave: r1 = the address
        a.l('.Lleave'); self.call('_hs_getp'); self.i('mov.b\tr0,@(%d,gbr)' % o['P'])
        for f, r in (('A', 'r8'), ('X', 'r9'), ('Y', 'r10'), ('S', 'r6')):
            self.i('mov\t%s,r0' % r, 'mov.b\tr0,@(%d,gbr)' % o[f])
        self.i('mov\tr7,r0', 'mov.l\tr0,@(%d,gbr)' % o['left'], 'mov\tr1,r0', 'mov.w\tr0,@(%d,gbr)' % o['pc'])
        if LEAVE_FAST:                            # (at the budget's end or a wait: rt_run's loop end and top done
            self.i('stc\tgbr,r4')                # here, then on at c->pc - its S, cycles, docs/34)
            a.ld(4, 'r0', '_rt_leave_fast'); a.d('jsr\t@r0', 'nop'); self.i('tst\tr0,r0'); a.b('bt', '.Lret')
            self.i('mov\tr14,r5', 'add\t#127,r5', 'add\t#1,r5', 'mov.b\t@(%d,gbr),r0' % o['S'], 'extu.b\tr0,r6',
                   'mov.l\t@(%d,gbr),r0' % o['left'], 'mov\tr0,r7', 'mov.w\t@(%d,gbr),r0' % o['pc'], 'extu.w\tr0,r1')
            self.bra('.Lfind'); a.u()
        else: self.bra('.Lret'); a.u()
        # local: r1 = the address, r3 = the jumping block's slot (8: none, XLOCAL)
        a.l('.Llocal'); self.i('extu.w\tr1,r1')           # (pop2's addresses are signed)
        self.i('mov\tr1,r4', 'shlr8\tr4', 'shlr2\tr4', 'shlr2\tr4', 'shlr\tr4', 'cmp/eq\tr3,r4'); a.b('bt', '.Lfind4')
        self.i('mov.l\t@(%d,gbr),r0' % o['xmin'], 'cmp/gt\tr0,r7'); a.b('bf', '.Lxchk')
        a.b('bra', '.Lfind4'); self.i('nop')
        a.l('.Lfind'); self.lookup('.Lleave', entry2='.Lfind4')
        # xchk: the test didn't let it through - the runtime's look (rt_xcheck: the CD unit's event, the tests again)
        # without leaving; then on to the lookup, or out (docs/24). r1 = the address
        a.l('.Lxchk'); self.i('mov.l\tr1,@-r15', 'mov.l\tr5,@-r15', 'mov.l\tr6,@-r15',
                               'mov\tr7,r0', 'mov.l\tr0,@(%d,gbr)' % o['left'], 'stc\tgbr,r4')
        a.ld(4, 'r0', '_rt_xcheck'); a.d('jsr\t@r0', 'nop')
        self.i('mov\tr0,r2', 'mov.l\t@(%d,gbr),r0' % o['left'], 'mov\tr0,r7',
               'mov.l\t@r15+,r6', 'mov.l\t@r15+,r5', 'mov.l\t@r15+,r1', 'tst\tr2,r2')
        a.b('bf', '.Lfind'); self.bra('.Lleave'); a.u()

    def subs(self):
        o = self.o; a = self.a
        def push(*rs):
            for r in rs: self.i('mov.l\t%s,@-r15' % r)
        def pop(*rs):
            for r in reversed(rs): self.i('mov.l\t@r15+,%s' % r)
        def ccall(fn):
            a.ld(4, 'r0', fn); a.d('jsr\t@r0', 'nop')
        def ret():
            a.d('rts', 'nop'); a.u()
        if '_hs_cdstep' in CD_USED:               # (docs/75) rt_cdstep(c, Y): Y back in r10, left in r7; the rest kept
            a.l('_hs_cdstep'); self.i('sts.l\tpr,@-r15'); push('r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6')
            self.i('mov\tr7,r0', 'mov.l\tr0,@(%d,gbr)' % o['left'], 'stc\tgbr,r4', 'extu.b\tr10,r5'); ccall('_rt_cdstep')
            self.i('extu.b\tr0,r10', 'mov.l\t@(%d,gbr),r0' % o['left'], 'mov\tr0,r7')
            pop('r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6'); self.i('lds.l\t@r15+,pr'); ret()
        if '_hs_pskip' in CD_USED:                # (each only where a hook calls it: the room, stage 7)
            a.l('_hs_pskip'); self.i('sts.l\tpr,@-r15'); push('r0', 'r1', 'r2', 'r3', 'r5', 'r6')   # (docs/77; r4: the call's)
            self.i('mov\tr7,r0', 'mov.l\tr0,@(%d,gbr)' % o['left'], 'stc\tgbr,r4', 'mov\tr1,r5'); ccall('_rt_pollskip')
            self.i('mov.l\t@(%d,gbr),r0' % o['left'], 'mov\tr0,r7')
            pop('r0', 'r1', 'r2', 'r3', 'r5', 'r6'); self.i('lds.l\t@r15+,pr'); ret()
        if '_hs_adstep' in CD_USED:
            a.l('_hs_adstep'); self.i('sts.l\tpr,@-r15'); push('r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6')   # (docs/76)
            self.i('mov\tr7,r0', 'mov.l\tr0,@(%d,gbr)' % o['left'], 'stc\tgbr,r4', 'extu.b\tr8,r5'); ccall('_rt_adstep')
            self.i('mov.l\t@(%d,gbr),r0' % o['left'], 'mov\tr0,r7')
            pop('r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6'); self.i('lds.l\t@r15+,pr'); ret()
        if MARKS:                                 # (docs/54) every register kept; C's r0-r7 and pr saved
            # hook: r1 = which (rt_hook_hot), Y from r10
            a.l('_hs_hook'); self.i('sts.l\tpr,@-r15'); push('r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7')
            self.i('stc\tgbr,r4', 'mov\tr1,r5', 'extu.b\tr10,r6'); ccall('_rt_hook_hot')
            pop('r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7'); self.i('lds.l\t@r15+,pr'); ret()
            # vspan: r2 = MAWR after a copy loop's words (c->vp.mawr still the first): their units tagged. The step 1
            # case here, a store a unit (r0 and T taken, the rest kept); else the C (rt_vmark_span), a word at a time
            n_ = self.u(); slow = '.Lvsp%d' % n_; lp = '.Lvsl%d' % n_; dn = '.Lvsd%d' % n_
            a.l('_hs_vspan'); push('r1', 'r3', 'r4')
            self.i('mov.w\t@(%d,gbr),r0' % o['vp_inc'], 'cmp/eq\t#1,r0'); a.b('bf', slow)
            self.i('mov.w\t@(%d,gbr),r0' % o['vp_mawr'], 'extu.w\tr0,r1', 'extu.w\tr2,r3', 'cmp/eq\tr3,r1'); a.b('bt', dn)
            self.i('cmp/hs\tr3,r1'); a.b('bt', slow)
            self.i('add\t#-1,r3', 'shlr2\tr1', 'shlr2\tr1', 'shlr2\tr3', 'shlr2\tr3',      # units r1 .. r3
                   'sub\tr1,r3', 'add\tr1,r1')                                            # r3: the count - 1
            a.ld(4, 'r4', '_rt_marks')
            self.i('mov.w\t@r4,r0', 'add\t#4,r4', 'add\tr1,r4', 'add\t#1,r3')               # r0: the tag
            a.l(lp); self.i('mov.w\tr0,@r4', 'dt\tr3', 'add\t#2,r4'); a.b('bf', lp)
            a.l(dn); pop('r1', 'r3', 'r4'); ret()
            a.l(slow); pop('r1', 'r3', 'r4')
            self.i('sts.l\tpr,@-r15'); push('r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7')
            self.i('stc\tgbr,r4', 'extu.w\tr2,r5'); ccall('_rt_vmark_span')
            pop('r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7'); self.i('lds.l\t@r15+,pr'); ret()
        # getp: r0 = P (N V Z C from the registers, the rest from c->P)
        a.l('_hs_getp'); push('r3')
        self.i('mov.b\t@(%d,gbr),r0' % o['P'], 'and\t#60,r0', 'mov\tr0,r3', 'tst\tr11,r11', 'movt\tr0', 'add\tr0,r0',
               'or\tr0,r3', 'mov\tr12,r0', 'and\t#128,r0', 'or\tr0,r3', 'or\tr13,r3', 'mov.b\t@(%d,gbr),r0' % o['vf'],
               'and\t#128,r0', 'shlr\tr0', 'or\tr3,r0')
        pop('r3'); ret()
        # setp: r0 = P -> c->P, N Z C V
        a.l('_hs_setp'); push('r3')
        self.i('mov.b\tr0,@(%d,gbr)' % o['P'], 'extu.b\tr0,r0', 'mov\tr0,r12', 'mov\t#1,r13', 'and\tr0,r13',
               'mov\tr0,r11', 'not\tr11,r11', 'mov\t#2,r3', 'and\tr3,r11', 'add\tr0,r0', 'mov.b\tr0,@(%d,gbr)' % o['vf'])
        pop('r3'); ret()
        # the C runtime's slow paths: r1 (address), r2 (value) -> r0; r1-r3, r5-r14 kept, r7 = c->left after
        for name, fn, args in (('_hs_rd_slow', '_rd_slow', 'a'), ('_hs_wr_slow', '_wr_slow', 'av'),
                               ('_hs_wr_phys', '_wr_phys', 'pv'), ('_hs_set_mpr', '_set_mpr', 'iv'),
                               ('_hs_wr_pair', '_wr_phys2', 'pp')):
            a.l(name); self.i('sts.l\tpr,@-r15'); push('r1', 'r2', 'r3', 'r5', 'r6')
            self.i('mov\tr7,r0', 'mov.l\tr0,@(%d,gbr)' % o['left'], 'stc\tgbr,r4')
            if args == 'a': self.i('extu.w\tr1,r5')
            elif args == 'av': self.i('extu.w\tr1,r5', 'extu.b\tr2,r6')
            elif args == 'pv': self.i('mov\tr1,r5', 'extu.b\tr2,r6')
            elif args == 'pp': self.i('mov\tr1,r5', 'extu.b\tr2,r6', 'mov\tr3,r7')
            else: self.i('mov\tr1,r5', 'extu.b\tr2,r6')
            ccall(fn)
            self.i('mov.l\tr0,@-r15', 'mov.l\t@(%d,gbr),r0' % o['left'], 'mov\tr0,r7', 'mov.l\t@r15+,r0')
            pop('r1', 'r2', 'r3', 'r5', 'r6'); self.i('lds.l\t@r15+,pr'); ret()
        # tam k: set_mpr's change for slot k (docs/32): r1 = the Rt, r3 = bank[A], r8 = A; r0, r2, r4 taken
        if TAM_SUB:
            for k in range(8):
                a.l('_hs_tam%d' % k); self.mpr_change(k, self.u()); ret()
        # interp: the instruction at c->pc by the interpreter, the state synced out and back
        a.l('_hs_interp'); self.i('sts.l\tpr,@-r15'); push('r1', 'r2', 'r3', 'r5')
        self.call('_hs_getp'); self.i('mov.b\tr0,@(%d,gbr)' % o['P'])
        for f, r in (('A', 'r8'), ('X', 'r9'), ('Y', 'r10'), ('S', 'r6')):
            self.i('mov\t%s,r0' % r, 'mov.b\tr0,@(%d,gbr)' % o[f])
        self.i('mov\tr7,r0', 'mov.l\tr0,@(%d,gbr)' % o['left'], 'stc\tgbr,r4'); ccall('_interp_one')
        for f, r in (('A', 'r8'), ('X', 'r9'), ('Y', 'r10'), ('S', 'r6')):
            self.i('mov.b\t@(%d,gbr),r0' % o[f], 'extu.b\tr0,%s' % r)
        self.i('mov.l\t@(%d,gbr),r0' % o['left'], 'mov\tr0,r7', 'mov.b\t@(%d,gbr),r0' % o['P']); self.call('_hs_setp')
        pop('r1', 'r2', 'r3', 'r5'); self.i('lds.l\t@r15+,pr'); ret()
        # decimal ADC / SBC: r1 = the operand; A and the flags through the C helpers (GETP / SETP around them)
        for name, fn in (('_hs_adc_dec', '_op_adc'), ('_hs_sbc_dec', '_op_sbc')):
            a.l(name); self.i('sts.l\tpr,@-r15'); push('r1', 'r2', 'r3', 'r5', 'r6', 'r7')
            self.call('_hs_getp'); self.i('mov.b\tr0,@(%d,gbr)' % o['P'], 'stc\tgbr,r4', 'mov\tr8,r5', 'extu.b\tr1,r6')
            ccall(fn); self.i('extu.b\tr0,r8', 'mov.b\t@(%d,gbr),r0' % o['P']); self.call('_hs_setp')
            pop('r1', 'r2', 'r3', 'r5', 'r6', 'r7'); self.i('lds.l\t@r15+,pr'); ret()
        # the VDC's ports while no event is due (huc_ops.h hw_wr_fast): r2 = the byte; T = done (r7 -= 1); r1-r3 kept
        vp = lambda f: o['vp_' + f]
        def gate(no):
            push('r3'); self.i('mov.l\t@(%d,gbr),r0' % vp('left_min'), 'mov\tr7,r3', 'add\t#-1,r3', 'cmp/gt\tr0,r3')
            a.b('bf', no)
        def done_t():
            self.i('add\t#-1,r7'); pop('r3'); a.d('rts', 'sett'); a.u()
        a.l('_hs_vdc0'); gate('.Lv0n')
        self.i('mov\tr2,r0', 'and\t#31,r0', 'mov.b\tr0,@(%d,gbr)' % vp('select')); done_t()
        a.l('.Lv0n'); pop('r3'); a.d('rts', 'clrt'); a.u()
        for name, hi in (('_hs_vdc2', 0), ('_hs_vdc3', 1)):
            n = self.u(); no = '.Lvn%d' % n
            a.l(name); gate(no)
            self.i('mov.b\t@(%d,gbr),r0' % vp('select'), 'cmp/eq\t#2,r0'); a.b('bt', '.Lv2_%d' % n)
            self.i('cmp/eq\t#0,r0'); a.b('bt', '.Lv0_%d' % n)
            self.i('cmp/eq\t#1,r0'); a.b('bt', '.Lv1_%d' % n)
            # the other registers (docs/21): the scroll, MWR and the DMA addresses stored here; a timing register
            # written with the byte it has changes nothing (rt.c hw_write_ finds no change); else the runtime
            vg = '.Lvg%d' % n; vst = '.Lvt%d' % n; vok = '.Lvo%d' % n; vno = '.Lvx%d' % n
            push('r1'); self.i('mov.b\t@(%d,gbr),r0' % vp('select'), 'shll2\tr0', 'shll\tr0', 'mov\tr0,r3')
            a.ld(4, 'r0', '_hs_vreg'); self.i('add\tr0,r3', 'mov.w\t@(%d,r3),r0' % (2 * hi), 'tst\tr0,r0')
            a.b('bt', vno)
            self.i('stc\tgbr,r1', 'add\tr0,r1', 'mov\tr2,r4')
            if hi: self.i('mov.w\t@(4,r3),r0', 'and\tr0,r4')
            self.i('mov.w\t@(6,r3),r0', 'cmp/eq\t#1,r0'); a.b('bt', vst)
            vcr = '.Lcr%d' % n
            self.i('cmp/eq\t#3,r0'); a.b('bt', vcr)
            self.i('mov.b\t@r1,r0', 'extu.b\tr0,r0', 'cmp/eq\tr4,r0'); a.b('bf', vno); self.bra(vok)
            a.l(vst); self.i('mov.b\tr4,@r1')
            a.l(vok); pop('r1'); done_t()
            a.l(vno); pop('r1')
            a.l(no); pop('r3'); a.d('rts', 'clrt'); a.u()
            if CR_KIND == 3:
                # CR (kind 3, docs/39; here, after the exits: out of the other registers' lines): a change of the bits
                # the timing doesn't read - not the raster and vertical blank interrupts' (2, 3), not whether the
                # sprites and the background are both off (rt.c cr_timing) - stored here, the VRAM step with it,
                # while no inert lines skipped ahead wait to be settled (rt_vdc.c settle)
                at = o['rt_vdc_t_CR'] + (0 if hi else 1)   # (r1: the byte written - big-endian, the high one first)
                dn, dh, ds = (o['rt_vdc_t_' + f] - at for f in ('now', 't_hb', 'snap_on'))
                assert all(-128 <= d_ <= 127 for d_ in (dn, dh, ds)), (dn, dh, ds)
                vcs = '.Lcrs%d' % n
                a.l(vcr); self.i('mov.b\t@r1,r0', 'extu.b\tr0,r0', 'cmp/eq\tr4,r0'); a.b('bt', vok)
                if not hi:
                    self.i('xor\tr4,r0', 'tst\t#12,r0'); a.b('bf', vno)
                    self.i('mov.b\t@r1,r0', 'tst\t#192,r0', 'movt\tr3', 'mov\tr4,r0', 'tst\t#192,r0', 'movt\tr0',
                           'cmp/eq\tr3,r0'); a.b('bf', vno)
                self.i('mov\t#%d,r0' % ds, 'mov.l\t@(r0,r1),r0', 'tst\tr0,r0'); a.b('bt', vcs)
                self.i('mov\t#%d,r0' % dn, 'mov.l\t@(r0,r1),r3', 'mov\t#%d,r0' % dh, 'mov.l\t@(r0,r1),r0', 'cmp/ge\tr0,r3')
                a.b('bf', vno)
                a.l(vcs); self.i('mov.b\tr4,@r1')
                if hi:                                # the VRAM step: 1, 32, 64, 128 by CR's bits 11, 12
                    vi = '.Lcri%d' % n; vd = '.Lcrd%d' % n
                    self.i('mov\tr4,r0', 'shlr2\tr0', 'shlr\tr0', 'and\t#3,r0', 'tst\tr0,r0', 'mov\t#1,r3'); a.b('bt', vd)
                    self.i('mov\t#16,r3'); a.l(vi); self.i('shll\tr3', 'dt\tr0'); a.b('bf', vi)
                    a.l(vd); self.i('mov\tr3,r0', 'mov.w\tr0,@(%d,gbr)' % vp('inc'))
                self.bra(vok); a.u()
            for reg, lab in (('mawr', '.Lv0_%d' % n), ('marr', '.Lv1_%d' % n)):
                a.l(lab); self.i('mov.w\t@(%d,gbr),r0' % vp(reg))
                if not hi: self.i('shlr8\tr0', 'shll8\tr0', 'or\tr2,r0')
                else: self.i('extu.b\tr0,r0', 'mov\tr2,r3', 'shll8\tr3', 'or\tr3,r0')
                self.i('mov.w\tr0,@(%d,gbr)' % vp(reg))
                if hi and reg == 'marr':          # the read buffer from the new address, then the step
                    self.i('mov.w\t@(%d,gbr),r0' % vp('marr'), 'cmp/pz\tr0', 'mov\t#0,r3'); a.b('bf', '.Lvr%d' % n)
                    self.i('extu.w\tr0,r3', 'add\tr3,r3', 'mov.l\t@(%d,gbr),r0' % vp('vram'), 'add\tr0,r3', 'mov.w\t@r3,r3')
                    a.l('.Lvr%d' % n); self.i('mov\tr3,r0', 'mov.w\tr0,@(%d,gbr)' % vp('read_buffer'),
                                             'mov.w\t@(%d,gbr),r0' % vp('marr'), 'mov\tr0,r3', 'mov.w\t@(%d,gbr),r0' % vp('inc'),
                                             'add\tr3,r0', 'mov.w\tr0,@(%d,gbr)' % vp('marr'))
                done_t()
            a.l('.Lv2_%d' % n)
            if not hi:
                self.i('mov\tr2,r0', 'mov.b\tr0,@(%d,gbr)' % vp('latch'))
            else:
                self.i('mov.w\t@(%d,gbr),r0' % vp('mawr'), 'mov\tr0,r4', 'cmp/pz\tr0'); a.b('bf', '.Lvs%d' % n)
                self.i('extu.w\tr0,r3', 'add\tr3,r3', 'mov.l\t@(%d,gbr),r0' % vp('vram'), 'add\tr0,r3',
                       'mov.b\t@(%d,gbr),r0' % vp('latch'), 'mov.b\tr0,@(1,r3)', 'mov.b\tr2,@r3')
                self.vmark('r4', 'r3')
                a.l('.Lvs%d' % n)
                self.i('mov.w\t@(%d,gbr),r0' % vp('inc'), 'add\tr4,r0', 'mov.w\tr0,@(%d,gbr)' % vp('mawr'))
            done_t()

    def vreg_table(self):
        """_hs_vreg: for each VDC register, the Rt offsets of its low and high bytes (0: the runtime), the high byte's
        mask, and 1 (stored here) or 2 (a timing register: the same byte, nothing to do)"""
        o = self.o; L = ['\t.align 2', '_hs_vreg:']
        regs = {0x05: ('vdc_t_CR', 0xFF, CR_KIND), 0x06: ('vdc_t_RCR', 0x03, 2), 0x07: ('vdc_BXR', 0x03, 1),
                0x08: ('vdc_BYR', 0x01, 1), 0x09: ('vdc_MWR', 0xFF, 1), 0x0A: ('vdc_t_HSR', 0xFF, 2),
                0x0B: ('vdc_t_HDR', 0xFF, 2), 0x0C: ('vdc_t_VSR', 0xFF, 2), 0x0D: ('vdc_t_VDR', 0xFF, 2),
                0x0E: ('vdc_t_VCR', 0xFF, 2), 0x0F: ('vdc_t_DCR', 0xFF, 2), 0x10: ('vdc_SOUR', 0xFF, 1),
                0x11: ('vdc_DESR', 0xFF, 1)}
        for k in range(32):
            if k in regs:
                f, m, kind = regs[k]; x = o['rt_' + f]
                L.append('\t.short\t%d,%d,%d,%d' % (x + 1, x, m, kind))   # (big-endian: the high byte first)
            else: L.append('\t.short\t0,0,0,0')
        return '\n'.join(L)

    def subs_ports(self):
        """the VDC's data reads and the VCE's ports while no event is due (huc_ops.h hw_rd_fast / hw_wr_fast): the
        write ones take r2 = the byte; the reads return it in r0 (u8); T = done (r7 -= 1); r1-r3 kept, r4 not"""
        o = self.o; a = self.a; vp = lambda f: o['vp_' + f]
        def push(*rs):
            for r in rs: self.i('mov.l\t%s,@-r15' % r)
        def pop(*rs):
            for r in reversed(rs): self.i('mov.l\t@r15+,%s' % r)
        def gate(no):
            self.i('mov.l\tr3,@-r15', 'mov.l\t@(%d,gbr),r0' % vp('left_min'), 'mov\tr7,r3', 'add\t#-1,r3', 'cmp/gt\tr0,r3')
            a.b('bf', no)
        def done_t():
            self.i('add\t#-1,r7', 'mov.l\t@r15+,r3'); a.d('rts', 'sett'); a.u()
        def no_t(no):
            a.l(no); self.i('mov.l\t@r15+,r3'); a.d('rts', 'clrt'); a.u()
        def ct_color():                           # r3 = &color[ctaddress & 0x1FF]
            self.i('mov.w\t@(%d,gbr),r0' % vp('ctaddress'), 'mov\tr0,r3', 'mov.w\t.Lm1ff,r0', 'and\tr0,r3', 'add\tr3,r3',
                   'mov.l\t@(%d,gbr),r0' % vp('color'), 'add\tr0,r3')
        def ct_step():
            self.i('mov.w\t@(%d,gbr),r0' % vp('ctaddress'), 'add\t#1,r0', 'mov\tr0,r3', 'mov.w\t.Lm1ff,r0', 'and\tr3,r0',
                   'mov.w\tr0,@(%d,gbr)' % vp('ctaddress'))
        # VDC data reads: the read buffer's byte; the high one steps MARR (register 2: the buffer reloaded)
        for k in (2, 3):
            n = self.u(); no = '.Lrn%d' % n
            a.l('_hs_vdcr%d' % k); gate(no)
            self.i('mov.w\t@(%d,gbr),r0' % vp('read_buffer'))
            if k == 3: self.i('shlr8\tr0')
            self.i('extu.b\tr0,r4')
            if k == 3:
                self.i('mov.b\t@(%d,gbr),r0' % vp('select'), 'cmp/eq\t#2,r0'); a.b('bf', '.Lrq%d' % n)
                self.i('mov.w\t@(%d,gbr),r0' % vp('marr'), 'cmp/pz\tr0', 'mov\t#0,r3'); a.b('bf', '.Lrz%d' % n)
                self.i('extu.w\tr0,r3', 'add\tr3,r3', 'mov.l\t@(%d,gbr),r0' % vp('vram'), 'add\tr0,r3', 'mov.w\t@r3,r3')
                a.l('.Lrz%d' % n)
                self.i('mov\tr3,r0', 'mov.w\tr0,@(%d,gbr)' % vp('read_buffer'), 'mov.w\t@(%d,gbr),r0' % vp('marr'), 'mov\tr0,r3',
                       'mov.w\t@(%d,gbr),r0' % vp('inc'), 'add\tr3,r0', 'mov.w\tr0,@(%d,gbr)' % vp('marr'))
                a.l('.Lrq%d' % n)
            self.i('mov\tr4,r0'); done_t(); no_t(no)
        # VCE reads: the colour's low byte; its high bit (| FE), then the address steps
        for k in (4, 5):
            n = self.u(); no = '.Lcn%d' % n
            a.l('_hs_vcer%d' % k); gate(no); ct_color(); self.i('mov.w\t@r3,r0')
            if k == 4: self.i('extu.b\tr0,r4')
            else: self.i('shlr8\tr0', 'and\t#1,r0', 'or\t#254,r0', 'mov\tr0,r4'); ct_step()
            self.i('mov\tr4,r0'); done_t(); no_t(no)
        # VCE writes: the colour address (low, high bit), the colour (low byte, high bit then the step)
        for k in (2, 3, 4, 5):
            n = self.u(); no = '.Lwn%d' % n
            a.l('_hs_vce%d' % k); gate(no)
            if k == 2: self.i('mov.w\t@(%d,gbr),r0' % vp('ctaddress'), 'mov.w\t.Lm100,r3', 'and\tr3,r0', 'or\tr2,r0',
                              'mov.w\tr0,@(%d,gbr)' % vp('ctaddress'))
            elif k == 3: self.i('mov.w\t@(%d,gbr),r0' % vp('ctaddress'), 'extu.b\tr0,r3', 'mov\tr2,r0', 'and\t#1,r0', 'shll8\tr0',
                                'or\tr3,r0', 'mov.w\tr0,@(%d,gbr)' % vp('ctaddress'))
            elif k == 4: ct_color(); self.i('mov.w\t@r3,r0', 'mov.w\t.Lm100,r4', 'and\tr4,r0', 'or\tr2,r0', 'mov.w\tr0,@r3')
            else:
                ct_color(); self.i('mov.w\t@r3,r0', 'extu.b\tr0,r4', 'mov\tr2,r0', 'and\t#1,r0', 'shll8\tr0', 'or\tr4,r0',
                                   'mov.w\tr0,@r3'); ct_step()
            done_t(); no_t(no)
        a.raw('\t.align 1\n.Lm1ff:\t.short\t0x1FF\n.Lm100:\t.short\t0x100')
        # the PSG's and the pad's writes, the pad's reads (rt.c hw_write_ / hw_read_ for $0800 / $1000: io_model.c's
        # io_write / io_read) while the CD drive's bus is quiet (scsi_quiet: the CD unit needn't run to the access)
        # and the access comes before vram_at (left > left_min: before the frame's end, nothing stale): the runtime's
        # VDC catch-up and vp_update change nothing then. r2 = the byte / r0 = the byte read; T = done; r1-r3 kept
        def rt(f): return self.o['rt_' + f]
        def gate_q(no):
            push('r1', 'r3'); self.i('mov.l\t@(%d,gbr),r0' % vp('left_min'), 'cmp/gt\tr0,r7'); a.b('bf', no)
            self.i('stc\tgbr,r3'); q = '.Lq%d' % self.u()
            a.ld(2, 'r0', rt('hw_scsi_have')); self.i('mov.l\t@(r0,r3),r0', 'tst\tr0,r0'); a.b('bt', q)
            a.ld(2, 'r0', rt('hw_scsi_phase')); self.i('mov.l\t@(r0,r3),r0', 'tst\tr0,r0'); a.b('bf', no)
            a.ld(2, 'r0', rt('hw_scsi_signals')); self.i('mov.l\t@(r0,r3),r0', 'shlr8\tr0', 'tst\t#1,r0'); a.b('bf', no)
            a.l(q)
        def done_q(): pop('r1', 'r3'); a.d('rts', 'sett'); a.u()
        def no_q(no): a.l(no); pop('r1', 'r3'); a.d('rts', 'clrt'); a.u()
        def ld(f, r): a.ld(2, 'r0', rt(f)); self.i('mov.l\t@(r0,r3),%s' % r)
        def st(f, r): a.ld(2, 'r0', rt(f)); self.i('mov.l\t%s,@(r0,r3)' % r)
        n = self.u(); no = '.Lqn%d' % n
        a.l('_hs_psg_w'); gate_q(no)
        a.ld(2, 'r0', rt('hw_io_bus')); self.i('mov.b\tr2,@(r0,r3)'); done_q(); no_q(no)
        n = self.u(); no = '.Lqn%d' % n; md = '.Lqm%d' % n; c2 = '.Lqc%d' % n
        a.l('_hs_pad_w'); gate_q(no)
        a.ld(2, 'r0', rt('hw_io_bus')); self.i('mov.b\tr2,@(r0,r3)')
        ld('hw_multitap', 'r0'); self.i('tst\tr0,r0'); a.b('bt', md)
        self.i('mov\tr2,r0', 'tst\t#1,r0'); a.b('bt', md)                  # SEL low: nothing
        ld('hw_clr', 'r0'); self.i('tst\tr0,r0'); a.b('bf', md)             # CLR was high: nothing
        self.i('mov\tr2,r0', 'tst\t#2,r0'); a.b('bt', c2)
        self.i('mov\t#0,r1'); st('hw_read_index', 'r1'); self.bra(md)        # SEL high, CLR rising: port 0
        a.l(c2); ld('hw_sel', 'r0'); self.i('tst\tr0,r0'); a.b('bf', md)    # SEL rising: the next port (255 at most)
        ld('hw_read_index', 'r1'); self.i('mov\t#-1,r4', 'extu.b\tr4,r4', 'cmp/ge\tr4,r1'); a.b('bt', md)
        self.i('add\t#1,r1'); st('hw_read_index', 'r1')
        a.l(md); self.i('mov\t#1,r4', 'mov\tr2,r1', 'and\tr4,r1'); st('hw_sel', 'r1')
        self.i('mov\tr2,r1', 'shlr\tr1', 'and\tr4,r1'); st('hw_clr', 'r1'); done_q(); no_q(no)
        n = self.u(); no = '.Lqn%d' % n; p0 = '.Lqp%d' % n; pz = '.Lqz%d' % n; pd = '.Lqd%d' % n; lo = '.Lql%d' % n
        a.l('_hs_pad_r'); gate_q(no)
        self.i('mov\t#0,r1'); ld('hw_multitap', 'r0'); self.i('tst\tr0,r0'); a.b('bt', p0)
        ld('hw_read_index', 'r1')
        a.l(p0); self.i('mov\t#15,r4', 'tst\tr1,r1'); a.b('bt', '.Lqb%d' % n)
        self.i('mov\t#4,r0', 'cmp/gt\tr0,r1'); a.b('bf', pd); self.i('mov\t#0,r4'); self.bra(pd)
        a.l('.Lqb%d' % n); a.ld(2, 'r0', rt('hw_buttons')); self.i('mov.w\t@(r0,r3),r1')
        ld('hw_sel', 'r0'); self.i('tst\tr0,r0'); a.b('bt', lo); self.i('shlr2\tr1', 'shlr2\tr1')
        a.l(lo); self.i('mov\tr1,r0', 'and\t#15,r0', 'xor\t#15,r0', 'mov\tr0,r4')
        a.l(pd); self.i('mov\tr4,r0', 'or\t#48,r0', 'mov\tr0,r4'); a.ld(2, 'r0', rt('hw_io_bus')); self.i('mov.b\tr4,@(r0,r3)')
        self.i('mov\tr4,r0'); done_q(); no_q(no)
        if not INPLACE_RD: return
        # the VDC status (rt.c hw_read_ / v32_read, docs/33): left > st_min (the read before the VDC's next event
        # anything sees; IRQ2's sources off) and the I flag set: the status, cleared, irq_wait cleared (wait_down)
        st_min = rt('st_min'); assert st_min % 4 == 0 and st_min <= 1020
        n = self.u(); no = '.Lsn%d' % n
        a.l('_hs_vdcr0'); self.i('mov.l\t@(%d,gbr),r0' % st_min, 'cmp/gt\tr0,r7'); a.b('bf', no)
        self.i('mov.b\t@(%d,gbr),r0' % o['P'], 'tst\t#4,r0'); a.b('bt', no)
        self.i('stc\tgbr,r4'); a.ld(2, 'r0', rt('vdc_t_status'))
        self.i('mov.b\t@(r0,r4),r4', 'and.b\t#192,@(r0,gbr)', 'mov\t#0,r0', 'mov.b\tr0,@(%d,gbr)' % o['irq_wait'],
               'mov\tr4,r0', 'add\t#-1,r7'); a.d('rts', 'sett'); a.u()
        a.l(no); a.d('rts', 'clrt'); a.u()
        # the ADPCM's status and its last command ($180C, $180D: io_model.c io_read) before the CD unit's next event
        # (left > io_min), the VDC's next VRAM event (left > left_min) and the budget's end (left > 0): the runtime's
        # catch-up changes nothing then
        io_min = rt('io_min'); assert io_min % 4 == 0 and io_min <= 1020
        for k in ('c', 'd'):
            n = self.u(); no = '.Lan%d' % n
            a.l('_hs_cdr' + k)
            self.i('cmp/pl\tr7'); a.b('bf', no)  # (left > 0: no interrupt's raise by then - the runtime's would leave)
            self.i('mov.l\t@(%d,gbr),r0' % vp('left_min'), 'cmp/gt\tr0,r7'); a.b('bf', no)
            self.i('mov.l\t@(%d,gbr),r0' % io_min, 'cmp/gt\tr0,r7'); a.b('bf', no)
            self.i('stc\tgbr,r4')
            if k == 'd':
                a.ld(2, 'r0', rt('hw_last_cmd')); self.i('mov.b\t@(r0,r4),r0'); a.d('rts', 'sett'); a.u()
            else:                                 # end 1, playing 8, a write waiting 4, a read waiting 0x80
                self.i('mov.l\tr1,@-r15', 'mov\t#0,r1')
                for f, bit, test in (('hw_end', 1, 'tst'), ('hw_playing', 8, 'tst'), ('hw_write_pending', 4, 'pl'),
                                     ('hw_read_pending', -128, 'pl')):
                    sk = '.Lab%d' % self.u()
                    a.ld(2, 'r0', rt(f)); self.i('mov.l\t@(r0,r4),r0')
                    if test == 'tst': self.i('tst\tr0,r0'); a.b('bt', sk)
                    else: self.i('cmp/pl\tr0'); a.b('bf', sk)
                    self.i('mov\t#%d,r0' % bit, 'or\tr0,r1'); a.l(sk)
                self.i('mov\tr1,r0', 'mov.l\t@r15+,r1'); a.d('rts', 'sett'); a.u()
            a.l(no); a.d('rts', 'clrt'); a.u()

    def koff_choose(self):
        """docs/35: the RAM offsets the hot code loads into r0 most (by its instructions' runs in the windows) go in
        the Rt's table (koff, GBR + rt_koff: mov.w @(disp,GBR),r0 - a line the runtime and the hot code keep in the
        cache), the rest stay literals"""
        top = [v for v in sorted(self.kw, key=lambda v: (-self.kw[v], v)) if self.kw[v] > 0][:KOFF_N]
        if KOFF_ORDER: top.sort(key=lambda v: self.kpos[v] / self.kw[v])   # (the table by its users' place)
        base = self.o['rt_koff']; assert base % 2 == 0 and base + 2 * (KOFF_N - 1) <= 510, base
        idx = {v: k for k, v in enumerate(top)}
        def sub(items):
            for k, it in enumerate(items):
                if it[0] == 'n': sub(it[1])
                elif it[0] == 'ld' and len(it) == 5:
                    items[k] = ('i', 'mov.w\t@(%d,gbr),r0' % (base + 2 * idx[it[3]])) if it[3] in idx else it[:4]
        sub(self.a.items)
        return top

    def tables(self):
        """the entries' hash table (key: block << 16 | address; the label) and the return labels by stack slot"""
        tab = [None] * (HSIZE + HPAD)
        for (i, key, b, base, H, E) in self.parts:
            for off in sorted(E):
                if off not in H: continue
                t = (base + off) & 0xFFFF
                h = (t ^ (t >> 8) ^ (i << 3)) & HMASK
                while tab[h] is not None: h += 1
                assert h < HSIZE + HPAD - 1, 'hash table too full'
                tab[h] = (i << 16 | t, self.lab(i, off))
        L = ['\t.section %s' % ('.rodata' if HSTAB_CART else '.data'), '\t.align 2', '_hs_tab:']   # (the cart: docs/38)
        for e in tab:
            L.append('\t.long\t0x%X,%s' % e if e else '\t.long\t0,0')
        return '\n'.join(L)

    def run(self):
        self.pend = 0; self.zs, self.ns = 'r11', 'r12'; self.sel = None
        self.need = {i: self.flag_need(b, base, H) for (i, key, b, base, H, E) in self.parts}
        self.vlive = {i: self.v_live_out(b, base, H) for (i, key, b, base, H, E) in self.parts}
        self.prologue(); CD_USED.clear()
        for (i, key, b, base, H, E) in self.parts:
            self.block(i, key, b, base, H, E)
        self.subs(); self.subs_ports(); self.a.raw(self.vreg_table())
        TRAMP_GBR.clear(); TRAMP_GBR.update({t: self.o['hsub'] + 4 * SUBS.index(t) for t in TRAMP})
        hdr = '/* hot_stage.s - tools/m3_sh2.py: a stage\'s hot code as SH2 assembly (docs/20). Generated: not game data,\n' \
              '   but made from the game\'s code - the work folder only. */'
        sub_tab = '\t.section .data\n\t.align 2\n\t.global\t_hs_subtab\n_hs_subtab:\n' + \
            '\n'.join('\t.long\t%s' % (x if x not in ('_hs_cdstep', '_hs_adstep') or x in CD_USED else '0')
                      for x in SUBS + ['0'] * (32 - len(SUBS)))
        ktab = self.koff_choose()
        sub_tab += '\n\t.global\t_hs_kofftab\n_hs_kofftab:\n' + '\n'.join('\t.short\t%d' % x for x in ktab + [0] * (KOFF_N - len(ktab)))
        if __import__('os').environ.get('SPLIT_COSTMAP'): self.costmap(__import__('os').environ['SPLIT_COSTMAP'])
        tabs = self.tables()
        return hdr + '\n' + '\n'.join(layout(self.a.items, refs=tabs + sub_tab)) + '\n' + tabs + '\n' + sub_tab + '\n'


HSIZE, HPAD = int(__import__('os').environ.get('SPLIT_HSIZE', '2048')), 64   # (SPLIT_HSIZE: a power of two; 4096 for a stage start's
#  many blocks, docs/74 - its mask a mov.w literal: up to 32768)
CR_KIND = int(__import__('os').environ.get('SPLIT_CR_KIND', '3'))   # CR's changes the timing doesn't read in the hot
                                              # code (3, docs/39; 2: only a write of the byte it has)
HSTAB_CART = int(__import__('os').environ.get('SPLIT_HSTAB_CART', '0'))   # the entries' hash table in the cart, its
                                              # 17 KB of SDRAM for the Super RAM banks read most (stage 4, docs/38)
# the shared subroutines and exits reached through the Cpu struct (c->hsub, docs/23): a GBR load, no literal
SUBS = ['.Lleave', '.Llocal', '_hs_getp', '_hs_rd_slow', '_hs_wr_slow', '_hs_wr_phys', '_hs_set_mpr', '_hs_interp',
        '_hs_adc_dec', '_hs_sbc_dec', '_hs_vdc0', '_hs_vdc2', '_hs_vdc3', '_hs_vdcr2', '_hs_vdcr3', '_hs_vcer4',
        '_hs_vcer5', '_hs_vce2', '_hs_vce3', '_hs_vce4', '_hs_vce5', '_hs_psg_w', '_hs_pad_w', '_hs_pad_r', '_hs_vdcr0',
        '_hs_cdrc', '_hs_cdrd']
if int(__import__('os').environ.get('SPLIT_MARKS', '0')): SUBS += ['_hs_hook', '_hs_vspan', '_rt_marks']   # (docs/54; the last: data, the tags' address)
if CDSTEP: SUBS += ['_hs_cdstep', '_hs_adstep']    # (docs/75, docs/76)
CD_USED = set()                                    # (the steps' stubs a stage's hot code calls: only those emitted)
CDSTEP_AT = (0x00, 0x0A99)                        # (m2_recomp.py's: the loop's head, $EA99)
ADSTEP_AT = (0x00, 0x13D0)                        # (and the AD_TRANS wait's, $F3D0)
PSKIP_AT = {(0x00, 0x0A89): 0, (0x00, 0x0978): 1}  # (and the status waits' BRAs, docs/77)
assert len(SUBS) <= 32
CHAIN_N = int(__import__('os').environ.get('SPLIT_CHAIN_N', '4'))   # the targets a return / jump tests first
HMASK = HSIZE - 1


def gen(parts, off, seen=None):
    return Gen(parts, off, seen).run()


HOOK_IDS = {(0x00, 0x0009): 0, (0x6C, 0x01B8): 1, (0x6C, 0x016F): 2}   # rt_hook_hot's numbers (docs/54)
FIELDS = ['A', 'X', 'Y', 'S', 'P', 'speed', 'miss', 'tflag', 'pc', 'MPR', 'rp', 'wok', 'ram', 'idle', 'left', 'irq_wait',
          'dirty', 'user',
          'hid', 'xok', 'xmin', 'hsub', 'vf', 'vp.select', 'vp.latch', 'vp.mawr', 'vp.marr', 'vp.inc', 'vp.read_buffer', 'vp.vram',
          'vp.left_min', 'vp.color', 'vp.ctaddress', 'vp.left_min1']


RT_FIELDS = ['bank', 'writable', 'bank_dirty', 'bank_hid', 'bank_hslot', 'unmodelled', 'hw.io_bus', 'hw.sel', 'hw.clr',
             'hw.multitap', 'hw.read_index', 'hw.buttons', 'hw.scsi.have', 'hw.scsi.phase', 'hw.scsi.signals',
             'vdc.t.CR', 'vdc.t.RCR', 'vdc.t.HSR', 'vdc.t.HDR', 'vdc.t.VSR', 'vdc.t.VDR', 'vdc.t.VCR', 'vdc.t.DCR',
             'vdc.BXR', 'vdc.BYR', 'vdc.MWR', 'vdc.SOUR', 'vdc.DESR', 'st_min', 'io_min', 'koff', 'vdc.t.status', 'hw.end',
             'hw.playing', 'hw.write_pending', 'hw.read_pending', 'hw.last_cmd', 'bank_rec', 'vdc.t.now', 'vdc.t.t_hb',
             'vdc.t.snap_on']


def rt_offsets(gen_dir, root=None):
    """the runtime's Rt struct offsets (tools/m3rt/rt.h) for the inline TAM"""
    import os, subprocess, tempfile
    root = root or os.environ.get('ROOTDIR', '/opt/toolchains/sega')
    here = os.path.dirname(os.path.abspath(__file__))
    src = '#include <stddef.h>\n#include "rt.h"\nconst unsigned long rt_off[] = {\n' + \
          ',\n'.join('    offsetof(Rt, %s)' % f for f in RT_FIELDS) + '\n};\n'
    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, 'o.c'); open(c, 'w').write(src)
        r = subprocess.run([os.path.join(root, 'sh-elf/bin/sh-elf-gcc'), '-m2', '-mb', '-S', '-o', '-', '-I', gen_dir,
                            '-I', os.path.join(here, 'm3rt'), '-I', os.path.join(here, 'm2host'), '-DHUC_TIMED'] +
                           (['-DHUC_MARKS'] if MARKS else []) + [c],
                           capture_output=True, text=True)
        if r.returncode: raise RuntimeError(r.stderr)
    vals = [int(x) for x in re.findall(r'\.long\s+(\d+)', r.stdout)]
    assert len(vals) == len(RT_FIELDS), r.stdout
    return {'rt_' + f.replace('.', '_'): v for f, v in zip(RT_FIELDS, vals)}


def cpu_offsets(gen_dir, root=None):
    """the Cpu struct's offsets as the SH2 compiler lays it out (huc_ops.h in gen_dir)"""
    import os, subprocess, tempfile
    root = root or os.environ.get('ROOTDIR', '/opt/toolchains/sega')
    src = '#include <stddef.h>\n#include "huc_ops.h"\nconst unsigned long huc_off[] = {\n' + \
          ',\n'.join('    offsetof(Cpu, %s)' % f for f in FIELDS) + '\n};\n'
    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, 'o.c'); open(c, 'w').write(src)
        r = subprocess.run([os.path.join(root, 'sh-elf/bin/sh-elf-gcc'), '-m2', '-mb', '-S', '-o', '-', '-I', gen_dir,
                            '-DHUC_TIMED'] + (['-DHUC_MARKS'] if MARKS else []) + [c], capture_output=True, text=True)
        if r.returncode: raise RuntimeError(r.stderr)
    vals = [int(x) for x in re.findall(r'\.long\s+(\d+)', r.stdout)]
    assert len(vals) == len(FIELDS), r.stdout
    out = {f.replace('.', '_'): v for f, v in zip(FIELDS, vals)}
    for f in ('A', 'X', 'Y', 'S', 'P', 'miss', 'tflag', 'idle', 'irq_wait', 'xok', 'vf', 'speed', 'vp_select', 'vp_latch'):
        assert out[f] <= 255, f
    assert out['hid'] + 7 <= 255 and out['hsub'] % 4 == 0 and out['hsub'] + 4 * 31 <= 1020 and out['vp_mawr'] + 1 <= 255
    out.update(rt_offsets(gen_dir, root))
    return out
COPY_LOOPS = int(__import__('os').environ.get('SPLIT_COPY_LOOPS', '2'))   # the VDC copy loops at once (docs/27)
R0_CACHE = int(__import__('os').environ.get('SPLIT_R0_CACHE', '1'))     # r0's constant kept (docs/27)
MUL_LOOPS = int(__import__('os').environ.get('SPLIT_MUL_LOOPS', '1'))   # the shift-and-add multiply at once (docs/27)
LAZY_A = int(__import__('os').environ.get('SPLIT_LAZY_A', '1'))     # A's byte sign- or zero-extended (docs/27)
TAM_DIRECT = int(__import__('os').environ.get('SPLIT_TAM_DIRECT', '1'))   # TAM on to the next label (docs/27)
SLOW_CYC = int(__import__('os').environ.get('SPLIT_SLOW_CYC', '1'))   # memory's slow paths settle the cycles (docs/28)
DELAYED_BR = int(__import__('os').environ.get('SPLIT_DELAYED_BR', '1'))   # forward branches as bt/s (docs/28)
INLINE_LEAF = int(__import__('os').environ.get('SPLIT_INLINE_LEAF', '1'))   # small leaf routines in line (docs/28)
LEAF_NO = {'PHA', 'PHX', 'PHY', 'PHP', 'PLA', 'PLX', 'PLY', 'PLP', 'TSX', 'TXS', 'CLI', 'SEI', 'SED', 'CLD', 'TAM',
           'SET', 'BRK', 'CSL', 'CSH', '???', 'RTI', 'RTS', 'BRA'}
MARR_INLINE = int(__import__('os').environ.get('SPLIT_MARR_INLINE', '1'))   # MARR in the site's slow path (docs/28)
VCE_INLINE = int(__import__('os').environ.get('SPLIT_VCE_INLINE', '1'))   # the colour port in place (docs/28)
COPY_ENTRY = int(__import__('os').environ.get('SPLIT_COPY_ENTRY', '1'))   # loop B from LDY #n written straight (docs/30)
KNOWN_C = int(__import__('os').environ.get('SPLIT_KNOWN_C', '1'))   # CLC / SEC folded into the ADC / SBC after (docs/30)
LEAF_JMP = int(__import__('os').environ.get('SPLIT_LEAF_JMP', '1'))   # a leaf's JMP in its slot followed (docs/31)
LEAF_COND = int(__import__('os').environ.get('SPLIT_LEAF_COND', '1'))  # a leaf's branch: one way in line (docs/31)
COLD_OOL = int(__import__('os').environ.get('SPLIT_COLD_OOL', '1'))    # a branch's way on that leaves: out of line (docs/31)
PORT_PAIR = int(__import__('os').environ.get('SPLIT_PORT_PAIR', '1'))  # ST1 + ST2's slow path in one runtime visit (docs/31)
COLD_RUNS = int(__import__('os').environ.get('SPLIT_COLD_RUNS', '1'))  # a branch's cold way on placed out of the hot path (docs/32)
COLD_MIN = int(__import__('os').environ.get('SPLIT_COLD_MIN', '600'))   # (its branch run that often in the windows at least)
COUNTS = {}                                   # {(bank, offset): runs in the windows} (m3_speedtest split)
TAM_SUB = int(__import__('os').environ.get('SPLIT_TAM_SUB', '1'))    # TAM's bank change in a shared subroutine (docs/32)
RD16_PAGE = int(__import__('os').environ.get('SPLIT_RD16_PAGE', '1'))  # JMP (a,X)'s word: one page look (docs/32)
LOAD_SCHED = int(__import__('os').environ.get('SPLIT_LOAD_SCHED', '1'))  # the hot code's load-use pairs broken (docs/32)
INPLACE_RD = int(__import__('os').environ.get('SPLIT_INPLACE_RD', '1'))  # the VDC status and the ADPCM status read in place (docs/33)
LIKELY_MED = int(__import__('os').environ.get('SPLIT_LIKELY_MED', '1'))   # a far branch taken most times over a bra (docs/34)
PAIR_RUN = int(__import__('os').environ.get('SPLIT_PAIR_RUN', '4'))   # a $0002 / $0003 pair with up to this many register-only instructions between (docs/34)
KOFF_ORDER = int(__import__('os').environ.get('SPLIT_KOFF_ORDER', '1'))
KOFF_N = int(__import__('os').environ.get('SPLIT_KOFF_N', '108'))   # the RAM offsets from the Rt's table (docs/35; 0: literals)
DROP_LABELS = int(__import__('os').environ.get('SPLIT_DROP_LABELS', '1'))  # labels nothing names left out (docs/43)
SLOT_DUP = int(__import__('os').environ.get('SPLIT_SLOT_DUP', '1'))  # a bra's nop slot takes its target's first instruction (docs/41)
FILL_SLOTS = int(__import__('os').environ.get('SPLIT_FILL_SLOTS', '1'))  # delayed branches' nop slots filled (docs/35)
LEAVE_FAST = int(__import__('os').environ.get('SPLIT_LEAVE_FAST', '1'))  # rt_run's loop end and top from the hot code (docs/34)
