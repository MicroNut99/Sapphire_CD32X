#!/usr/bin/env python3
"""m2_recomp.py - M2: the HuC6280 -> C recompiler (docs/09 §2).

    m2_recomp.py <work> <out dir>
        <work>/m2/*.npz (m2_code.py: each code block's bytes and the instruction starts seen) ->
        <out>/huc_ops.h   one C macro per instruction form (shared by the translation and the interpreter)
        <out>/interp.c    the interpreter: any address, one instruction at a time (code nobody saw run)
        <out>/blk_<key>.c one function per code block (8 KB bank): every instruction start a label
        <out>/blocks.c    the list of blocks (bank, key, function, the code bytes it was made from)

The semantics are Mednafen's (huc6280.cpp / huc6280_ops.inc at libretro beetle-pce b96c11e), instruction by
instruction: the T flag (SET), decimal ADC / SBC, TSB / TRB / TST, BIT #, TAM / TMA, the block moves (Y, A, X
pushed and popped), BSR, ST0-ST2, undefined opcodes = 1-byte NOPs. The interpreter keeps P as the CPU keeps it;
a translated block keeps the registers in C locals and N, V, Z, C apart from P, lazily (docs/16). Checked against
Mednafen's instruction trace and the recorded runs (tools/m2_trace.py, m2_timing.py, m2_run.py: docs/10-15).

Control flow (bank-threaded): each code bank is translated for the slot of the CPU's address space it runs in
(SLOT). Inside a block, branches, JMP, JSR and RTS to an address in that slot go straight to the label; anything
else sets c->pc and returns to the dispatcher (trace_main.c on the PC), which looks at MPR to find the next block.
TAM ends a block (the mapping may change under it). Instructions that code may write read their bytes when they
run (find_patched).
"""
import glob
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from huc6280 import OPS, SIZE, decode                    # noqa: E402

# ---- operand access for each mode: (setup, read expression, write statement with {v}, effective address)
# o0, o1, o2... are the operand bytes (constants in a translation, variables in the interpreter). The registers
# are rA rX rY rS rP and the flags go through ZN / ZNB / ZSET / NSET / GETP / SETP (huc_ops.h): in the interpreter
# they are the Cpu struct's, in a translated block C locals (docs/16).
def mode_access(mode, ob=None):
    """ob: the operand bytes when the translation knows them. An absolute address in base RAM ($2000-$3FFF) is then
    read and written there directly: MPR1 is bank F8 in every run, as the zero page's own access assumes (the
    runtime stops if a game maps anything else there)."""
    if ob is not None and mode in ('abs', 'abx', 'aby', 'imab', 'imax'):
        a = (ob[0] | ob[1] << 8) if mode[:2] != 'im' else (ob[1] | ob[2] << 8)
        if mode in ('abs', 'imab') and a >> 13 == 1:
            return ('', 'RAMP[0x%04X]' % (a & 0x1FFF), 'RAMP[0x%04X] = (uint8_t)({v});' % (a & 0x1FFF), None)
        if mode in ('abx', 'aby', 'imax') and a >> 13 == 1 and (a + 0xFF) >> 13 == 1:
            r = 'rY' if mode == 'aby' else 'rX'
            return ('const unsigned eo = 0x%04X + %s;' % (a & 0x1FFF, r), 'RAMP[eo]', 'RAMP[eo] = (uint8_t)({v});', None)
    if mode == 'zp':  return ('', 'ZR(O0)', 'ZW(O0, {v});', '(0x2000 | O0)')
    if mode == 'zpx': return ('const uint8_t za = (uint8_t)(O0 + rX);', 'ZR(za)', 'ZW(za, {v});', '(0x2000 | za)')
    if mode == 'zpy': return ('const uint8_t za = (uint8_t)(O0 + rY);', 'ZR(za)', 'ZW(za, {v});', '(0x2000 | za)')
    if mode == 'izp': return ('const uint16_t ea = ZPTR(O0);', 'RD(ea)', 'WR(ea, {v});', 'ea')
    if mode == 'izx': return ('const uint16_t ea = ZPTR((uint8_t)(O0 + rX));', 'RD(ea)', 'WR(ea, {v});', 'ea')
    if mode == 'izy': return ('const uint16_t ea = (uint16_t)(ZPTR(O0) + rY);', 'RD(ea)', 'WR(ea, {v});', 'ea')
    if mode == 'abs': return ('const uint16_t ea = (uint16_t)(O0 | O1 << 8);', 'RD(ea)', 'WR(ea, {v});', 'ea')
    if mode == 'abx': return ('const uint16_t ea = (uint16_t)((O0 | O1 << 8) + rX);', 'RD(ea)', 'WR(ea, {v});', 'ea')
    if mode == 'aby': return ('const uint16_t ea = (uint16_t)((O0 | O1 << 8) + rY);', 'RD(ea)', 'WR(ea, {v});', 'ea')
    if mode == 'imm': return ('', 'O0', None, None)
    if mode == 'imzp': return ('', 'ZR(O1)', None, None)
    if mode == 'imzx': return ('const uint8_t za = (uint8_t)(O1 + rX);', 'ZR(za)', None, None)
    if mode == 'imab': return ('const uint16_t ea = (uint16_t)(O1 | O2 << 8);', 'RD(ea)', None, None)
    if mode == 'imax': return ('const uint16_t ea = (uint16_t)((O1 | O2 << 8) + rX);', 'RD(ea)', None, None)
    return None


TCAP = {'AND', 'ORA', 'EOR', 'ADC'}                      # take the T flag (Mednafen TPREFIX / TPOSTFIX)
ALU = {
    'AND': 'a &= x; ZN(a);',
    'ORA': 'a |= x; ZN(a);',
    'EOR': 'a ^= x; ZN(a);',
    'ADC': 'a = ADC(a, x);',
}


def body(m, mode, tmode=False, ob=None):
    """C statements for one instruction (no control flow but via macros J_*, which the caller defines).
    tmode: the instruction follows SET (the T flag): AND / ORA / EOR / ADC work on $2000 + X instead of A.
    ob: the operand bytes, when known (mode_access)."""
    acc = mode_access(mode, ob)
    if m in ALU:
        setup, rd, _, _ = acc
        if tmode and m in TCAP:
            return '{ %s const uint8_t x = %s; uint8_t a = ZR(rX); %s ZW(rX, a); }' % (setup, rd, ALU[m])
        return '{ %s const uint8_t x = %s; uint8_t a = rA; %s rA = a; }' % (setup, rd, ALU[m])
    if m == 'SBC':
        setup, rd, _, _ = acc
        return '{ %s const uint8_t x = %s; rA = SBC(rA, x); }' % (setup, rd)
    if m in ('LDA', 'LDX', 'LDY'):
        setup, rd, _, _ = acc
        r = 'r' + m[2]
        return '{ %s %s = %s; ZN(%s); }' % (setup, r, rd, r)
    if m in ('STA', 'STX', 'STY', 'STZ'):
        setup, _, wr, _ = acc
        v = {'STA': 'rA', 'STX': 'rX', 'STY': 'rY', 'STZ': '0'}[m]
        return '{ %s %s }' % (setup, wr.format(v=v))
    if m in ('CMP', 'CPX', 'CPY'):
        setup, rd, _, _ = acc
        r = {'CMP': 'rA', 'CPX': 'rX', 'CPY': 'rY'}[m]
        return '{ %s CMPL(%s, %s); }' % (setup, r, rd)
    if m == 'BIT':
        setup, rd, _, _ = acc
        return '{ %s const uint8_t x = %s; SETVB(x << 1); ZNB(x & rA, x); }' % (setup, rd)
    if m == 'TST':
        setup, rd, _, _ = acc
        return '{ %s const uint8_t x = %s; SETVB(x << 1); ZNB(x & O0, x); }' % (setup, rd)
    rmw = {'ASL': '{ const unsigned t_ = (unsigned)x << 1; SETC(t_ >> 8); x = (uint8_t)t_; ZN(x); }',
           'LSR': 'SETC(x & 1); x >>= 1; ZN(x);',
           'ROL': '{ const unsigned t_ = ((unsigned)x << 1) | CSET; SETC(t_ >> 8); x = (uint8_t)t_; ZN(x); }',
           'ROR': '{ const unsigned t_ = x | ((unsigned)CSET << 8); SETC(x & 1); x = (uint8_t)(t_ >> 1); ZN(x); }',
           'INC': 'x++; ZN(x);', 'DEC': 'x--; ZN(x);',
           'TSB': 'SETVB(x << 1); ZNB(x | rA, x); x |= rA;',
           'TRB': 'SETVB(x << 1); ZNB(x & ~rA, x); x &= (uint8_t)~rA;'}
    if m in rmw:
        if mode == 'acc':
            return '{ uint8_t x = rA; %s rA = x; }' % rmw[m]
        setup, rd, wr, _ = acc
        return '{ %s uint8_t x = %s; %s %s }' % (setup, rd, rmw[m], wr.format(v='x'))
    if m[:3] in ('RMB', 'SMB'):
        b = int(m[3])
        op = 'x &= (uint8_t)~%d;' % (1 << b) if m[0] == 'R' else 'x |= %d;' % (1 << b)
        return '{ uint8_t x = ZR(O0); %s ZW(O0, x); }' % op
    simple = {
        'TAX': 'rX = rA; ZN(rX);', 'TXA': 'rA = rX; ZN(rA);', 'TAY': 'rY = rA; ZN(rY);',
        'TYA': 'rA = rY; ZN(rA);', 'TSX': 'rX = rS; ZN(rX);', 'TXS': 'rS = rX;',
        'INX': 'rX++; ZN(rX);', 'DEX': 'rX--; ZN(rX);', 'INY': 'rY++; ZN(rY);', 'DEY': 'rY--; ZN(rY);',
        'SAX': '{ const uint8_t t = rX; rX = rA; rA = t; }', 'SAY': '{ const uint8_t t = rY; rY = rA; rA = t; }',
        'SXY': '{ const uint8_t t = rX; rX = rY; rY = t; }',
        'CLA': 'rA = 0;', 'CLX': 'rX = 0;', 'CLY': 'rY = 0;',
        'CLC': 'SETC(0);', 'SEC': 'SETC(1);', 'CLD': 'rP &= ~DF;', 'SED': 'rP |= DF;',
        'CLV': 'SETVB(0);', 'CLI': 'rP &= ~IF;', 'SEI': 'rP |= IF;',
        'PHA': 'PUSH(rA);', 'PHX': 'PUSH(rX);', 'PHY': 'PUSH(rY);', 'PHP': 'PUSH(GETP() | BF);',
        'PLA': 'rA = POP(); ZN(rA);', 'PLX': 'rX = POP(); ZN(rX);', 'PLY': 'rY = POP(); ZN(rY);',
        'PLP': 'SETP(POP());',
        'NOP': '', 'CSL': 'SPEED(0);', 'CSH': 'SPEED(1);',
        'ST0': 'IOWP(0x1FE000, O0);', 'ST1': 'IOWP(0x1FE002, O0);', 'ST2': 'IOWP(0x1FE003, O0);',
        'TMA': '{ for (int i = 0; i < 8; i++) if (O0 & (1 << i)) rA = c->MPR[i]; }',
        '???': '',
    }
    if m in simple:
        return '{ %s }' % simple[m]
    if mode == 'blk':                                     # (block_move works on the Cpu struct: the registers go there)
        k = {'TII': 0, 'TDD': 1, 'TIN': 2, 'TIA': 3, 'TAI': 4}[m]
        return 'SYNC_OUT(); BMOVE(c, %d, (uint16_t)(O0 | O1 << 8), (uint16_t)(O2 | O3 << 8), (uint16_t)(O4 | O5 << 8)); SYNC_IN_AXY(); RELOAD_LEFT();' % k
    return None                                           # control flow: handled by the caller


# The T flag is cleared after every instruction but SET (Mednafen: `skip_T_flag_clear`); P's T bit is kept
# clear in this runtime and SET's following instruction is translated in T mode. PHP pushes P | B (T clear).
CTRL = {'JMP', 'JSR', 'RTS', 'RTI', 'BRK', 'BSR', 'TAM', 'SET'} | {'BBR%d' % i for i in range(8)} | \
       {'BBS%d' % i for i in range(8)} | {'BPL', 'BMI', 'BVC', 'BVS', 'BCC', 'BCS', 'BNE', 'BEQ', 'BRA'}
COND = {'BPL': '!NSET', 'BMI': 'NSET', 'BVC': '!VSET', 'BVS': 'VSET',
        'BCC': '!CSET', 'BCS': 'CSET', 'BNE': '!ZSET', 'BEQ': 'ZSET', 'BRA': '1'}


def _cycles():
    """the base cycles of each opcode (m2host/huc6280_cycles.h, measured from Mednafen: docs/11)"""
    import re
    h = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'm2host', 'huc6280_cycles.h')).read()
    return [int(x) for x in re.findall(r'\d+', h[h.index('{'):h.index('}')])]


CYCLES = _cycles()
assert len(CYCLES) == 256


def operands(ob):
    return ''.join('\n#define O%d 0x%02X' % (i, b) for i, b in enumerate(ob))


def undef(ob):
    return ''.join('\n#undef O%d' % i for i in range(len(ob)))


# The slot (MPR number) each code bank runs in, in every trace window: the game's memory layout. A block of one of
# these banks is translated for that slot alone, so its addresses are constants; run anywhere else, the interpreter
# takes it (DraculaX docs/16). Sapphire's, measured over the six windows of docs/06 (each bank ran in one slot only;
# DraculaX's own table is not Sapphire's). Banks not listed (01, 87) take the slot their own JMP / JSR targets vote for.
SLOT = {0x00: 7, 0x02: 6, 0x68: 2, 0x69: 3, 0x6A: 2, 0x6B: 3, 0x6C: 4, 0x74: 2, 0x75: 3, 0x76: 4, 0x77: 5, 0xF8: 1}


# the runtime's hooks for the picture (docs/53), at these instructions (bank, offset in it), in HUC_MARKS builds: the
# System Card's CD_READ (E009: where each piece of RAM was read from), the VRAM load list's entry (6C:81B8, its source,
# VRAM address, flags and count read: the ripped output it writes) and the list's end (6C:816F)
# (DraculaX's: (0x00, 0x0009) CD_READ, (0x6C, 0x01B8) / (0x6C, 0x016F) its load list. Sapphire's are found at M3:
# DraculaX's 6C addresses are other code in Sapphire's bank 6C.)
HOOKS = {}
# docs/13: code that writes a whole instruction, opcode too, into a place the code map saw one opcode at (Sapphire
# 69:1235 / 12E4 / 1345: a JMP the game turns into CPY / LDX each frame). PATCH_ALTS[(bank, offset)] = the other
# opcodes seen there; the translation picks by the opcode in memory instead of leaving to the interpreter. Read from
# <work>/m2/patch_alts.json ({"69:1235": [204], ...}: tools/m3rt rt_pc RUN_INTERP lists them) - the game's, so not here
PATCH_ALTS = {}
# docs/75: the System Card's CD_READ byte loop, its plain iterations in one go (HUC_CDSTEP builds; rt.c rt_cdstep)
CDSTEP_AT = (0x00, 0x0A99)
ADSTEP_AT = (0x00, 0x13D0)   # docs/76: its AD_TRANS wait (rt_adstep)
PSKIP_AT = {(0x00, 0x0A89): 0, (0x00, 0x0978): 1}   # docs/77: its waits for the drive's status (rt_pollskip)


class Block:
    def __init__(self, key, bank, data, starts):
        self.key, self.bank, self.data = key, bank, data
        self.starts = set(int(i) for i in np.nonzero(starts)[0])

    patched = frozenset()                              # (find_patched)
    dynops = frozenset()
    own = np.zeros(0x2000, bool)                        # (merge_blocks: the bytes the game rewrites)

    def instructions(self):
        """{offset: (mnemonic, mode, operand bytes, length, targets)} for the starts seen, statically followed"""
        out = {}; work = sorted(self.starts)
        while work:
            off = work.pop()
            if off in out or off >= 0x2000: continue
            d = decode(lambda a: int(self.data[a & 0x1FFF]), off)
            if off + d[3] > 0x2000: continue
            out[off] = d
            if d[0] not in ('JMP', 'RTS', 'RTI', 'BRA', 'BRK', 'TAM'): work.append(off + d[3])
            for tt in d[4]:
                if d[1] in ('rel', 'zrel') and 0 <= tt < 0x2000: work.append(tt)
        return out

    def slot(self, ins):
        """the 8 KB slot this bank runs in: where its own absolute JMP / JSR targets point"""
        votes = {}
        for off, (m, mode, ob, n, t) in ins.items():
            if m in ('JMP', 'JSR') and mode == 'abs' and (t[0] & 0x1FFF) in ins:
                votes[t[0] >> 13] = votes.get(t[0] >> 13, 0) + 1
        return max(votes, key=votes.get) if votes else None

    def mem(self, slot_base):
        return lambda a: int(self.data[(a - slot_base) & 0x1FFF]) if (a & 0xE000) == slot_base else 0

    def gen(self, keep=None, fn=None, miss=1, out='LEAVE', xout='LEAVE', rs=False, entries=None):
        """C for this block. Labels for every instruction start (from the runs), plus static fall-through and
        branch targets inside the bank. The slot is not fixed: base = the running PC's slot.
        keep: only these offsets get labels (the 32X's hot and cold halves, tools/m3_speedtest.py split): a jump to
        any other leaves for the dispatcher. fn: the function's name. miss: what c->miss says when it has no label
        for the address it is entered at (1: the interpreter runs it; the hot half says 3: try the cold half).
        out: the macro a jump to an address with no label here uses (LEAVE; a stage's merged hot code, LOCAL).
        xout: the macro JMP (ind), JMP (abs,X), TAM and RTI go on with (LEAVE; merged, XLOCAL: no exit while nothing
        is due)."""
        self._out = out; self._xout = xout; self._rs = rs   # rs: a shadow stack of calls (RSPUSH / RSPOP: merged code)
        work = sorted(self.starts); seen = set()
        while work:
            off = work.pop()
            if off in seen or off >= 0x2000: continue
            d = decode(lambda a: int(self.data[a & 0x1FFF]) if (a & 0x1FFF) < 0x2000 else 0, off)
            m, mode, ob, n, t = d
            if off + n > 0x2000: continue                 # its last bytes are the next bank's: the interpreter
            seen.add(off)
            if m not in ('JMP', 'RTS', 'RTI', 'BRA', 'BRK') and m != 'TAM' and m != 'SET':
                work.append(off + n)
            if m == 'SET':
                d2 = decode(lambda a: int(self.data[a & 0x1FFF]), off + n)
                work.append(off + n + d2[3])
                if d2[0] not in TCAP: work.append(off + n)
            if mode in ('rel', 'zrel'):
                for tt in t:
                    if 0 <= tt < 0x2000: work.append(tt)
        self.labels = sorted(o for o in seen if keep is None or o in keep)
        self._ls = set(self.labels)
        L = []
        fn = fn or 'blk_%s' % self.key
        L.append('/* %s: bank %02X, %d instruction starts (%d seen run) */' % (self.key, self.bank, len(self.labels), len(self.starts)))
        L.append('void %s(Cpu *c)\n{' % fn)
        L.append('    static void *const lab[0x2000] = {')
        # entries: only these labels can be found by address (merged code, docs/19); the rest are reached only by
        # falling through or a jump in the code, so the compiler can join them to the instruction before
        L.append(',\n'.join('        [0x%04X] = &&l%04X' % (o, o) for o in self.labels if entries is None or o in entries))
        L.append('    };')
        if self.bank in SLOT:                          # the slot it runs in: every address in it a constant
            L.append('    if ((c->pc & 0xE000) != 0x%04X || !lab[c->pc & 0x1FFF]) { c->miss = %d; return; }' % (SLOT[self.bank] << 13, miss))
            L.append('    const uint16_t base = 0x%04X;' % (SLOT[self.bank] << 13))
        else:
            L.append('    if (!lab[c->pc & 0x1FFF]) { c->miss = %d; return; }' % miss)
            L.append('    const uint16_t base = c->pc & 0xE000;')
        L.append('    uint16_t t;')
        L.append('    (void)t;')
        L.append('    BLOCK_LOCALS;')
        # an address only known when it runs: its label if it is in this bank's slot, otherwise the dispatcher
        # every exit shares one copy of the struct's update (docs/19): a site is the address and a jump
        L.append('#define LOCAL(addr) do { t = (addr); if ((t & 0xE000) == base && lab[t & 0x1FFF]) goto *lab[t & 0x1FFF]; goto leave_; } while (0)')
        L.append('#define LEAVE(addr) do { t = (addr); goto leave_; } while (0)')
        L.append('    goto *lab[c->pc & 0x1FFF];')
        for off in self.labels:
            d = decode(lambda a: int(self.data[a & 0x1FFF]) if a < 0x2000 else 0, off)
            m, mode, ob, n, t = d
            alts = [o for o in PATCH_ALTS.get((self.bank, off), ()) if o != int(self.data[off]) and OPS[o]] if off in self.patched else []
            if alts:                                      # (docs/13: its other opcodes translated too)
                L.append('l%04X: { const uint8_t op_ = RD((uint16_t)(base + 0x%04X)); if (__builtin_expect(op_ != 0x%02X, 0)) { %s SYNC_OUT(); c->pc = (uint16_t)(base + 0x%04X); c->miss = 1; return; } }'
                         % (off, off, int(self.data[off]), ' '.join('if (op_ == 0x%02X) goto a%04X_%02X;' % (o, off, o) for o in alts), off))
                L.append('    CHK(0x%04X); CY(%d); /* %s %s */%s' % (off, CYCLES[int(self.data[off])], m, mode, self.ops_def(off, ob)))
            elif off in self.patched:                     # code may write its opcode: run it only while it is this
                L.append('l%04X: if (__builtin_expect(RD((uint16_t)(base + 0x%04X)) != 0x%02X, 0)) { SYNC_OUT(); c->pc = (uint16_t)(base + 0x%04X); c->miss = 1; return; }  /* else the interpreter */'
                         % (off, off, int(self.data[off]), off))
                L.append('    CHK(0x%04X); CY(%d); /* %s %s */%s' % (off, CYCLES[int(self.data[off])], m, mode, self.ops_def(off, ob)))
            else:
                L.append('l%04X: CHK(0x%04X); CY(%d); /* %s %s */%s' % (off, off, CYCLES[int(self.data[off])], m, mode, self.ops_def(off, ob)))
            if (self.bank, off) in HOOKS:                 # (the picture's marks, docs/53: HUC_MARKS builds only)
                L.append('#ifdef HUC_MARKS\n    %s;\n#endif' % HOOKS[(self.bank, off)])
            if (self.bank, off) == CDSTEP_AT:
                L.append('#if defined(HUC_CDSTEP) && defined(HUC_TIMED) && defined(HUC_LOCAL)\n'
                         '    { c->left = left_; rY = (uint8_t)rt_cdstep(c, rY); left_ = c->left; }\n#endif')
            if (self.bank, off) == ADSTEP_AT:
                L.append('#if defined(HUC_CDSTEP) && defined(HUC_TIMED) && defined(HUC_LOCAL)\n'
                         '    { c->left = left_; rt_adstep(c, rA); left_ = c->left; }\n#endif')
            if (self.bank, off) in PSKIP_AT:
                L.append('#if defined(HUC_CDSTEP) && defined(HUC_TIMED) && defined(HUC_LOCAL)\n'
                         '    { c->left = left_; rt_pollskip(c, %d); left_ = c->left; }\n#endif' % PSKIP_AT[(self.bank, off)])
            nxt = off + n
            L.append(self.code(m, mode, ob, n, off, nxt))
            L.append(undef(ob).strip())
            for o in alts:                                # (docs/13: each other opcode, its operands read when it runs)
                m2, mode2, ob2, n2, _ = decode(lambda a, o=o: o if a == off else (int(self.data[a & 0x1FFF]) if a < 0x2000 else 0), off)
                L.append('a%04X_%02X: CHK(0x%04X); CY(%d); /* %s %s (written by code) */%s' % (off, o, off, CYCLES[o], m2, mode2, self.ops_def(off, ob2)))
                L.append(self.code(m2, mode2, ob2, n2, off, off + n2))
                L.append(undef(ob2).strip())
        L.append('leave_:\n    SYNC_OUT(); c->pc = t; return;')
        L.append('#undef LOCAL\n#undef LEAVE\n}\n')
        return '\n'.join(x for x in L if x)

    def ops_def(self, off, ob):
        """the operand bytes' #defines: constants, or for an instruction whose bytes code writes, reads of them"""
        if off in self.dynops or off in self.patched:
            return ''.join('\n#define O%d RD((uint16_t)(base + 0x%04X))' % (i, off + 1 + i) for i in range(len(ob)))
        return operands(ob)

    def code(self, m, mode, ob, n, off, nxt):
        here = lambda o: '(uint16_t)(base + 0x%04X)' % o
        if off in self.dynops or off in self.patched:
            return self.code_dyn(m, mode, ob, n, off, nxt)
        return self.code_static(m, mode, ob, n, off, nxt)

    def code_static(self, m, mode, ob, n, off, nxt):
        here = lambda o: '(uint16_t)(base + 0x%04X)' % o
        def goto_next(o):
            return ('goto l%04X;' % o) if o in self.labels_set() else '%s(%s);' % (self._out, here(o))
        def jump_rel(tgt):                               # a target in this slot (a branch's): its label, or leave
            if tgt == off and m == 'BRA' and self.bank in SLOT:
                return 'IDLE(l%04X, (uint16_t)(base + 0x%04X));' % (off, off)
            if (m == 'BEQ' and tgt == off - 2 and tgt >= 0 and int(self.data[tgt]) == 0xA5 and self.bank in SLOT
                    and tgt in self.labels_set()):
                # Sapphire's wait (docs/08): LDA zp / BEQ back to it - the flag only an interrupt sets. Waiting:
                # leave at the LDA, as a jump to itself does (the runtime skips to the next event)
                return 'IDLE(l%04X, (uint16_t)(base + 0x%04X));' % (tgt, tgt)
            if 0 <= tgt < 0x2000 and tgt in self.labels_set():
                # a branch back: the 32X build's time check (HUC_TIMED), so a loop can't run past an interrupt
                return ('POLL((uint16_t)(base + 0x%04X)); ' % tgt if tgt <= off else '') + 'goto l%04X;' % tgt
            return '%s((uint16_t)(base + (%d)));' % (self._out, tgt)
        def jump_abs(a):                                 # an absolute target: its label if the block runs in its slot
            if a & 0x1FFF == off and m == 'JMP' and self.bank in SLOT and a >> 13 == SLOT[self.bank]:
                return 'IDLE(l%04X, 0x%04X);' % (off, a)     # a jump to itself: the game waits for an interrupt
            if (a & 0x1FFF) in self.labels_set():
                poll = 'POLL(0x%04X); ' % a if (a & 0x1FFF) <= off else ''
                return 'if (base == 0x%04X) { %sgoto l%04X; } %s(0x%04X);' % (a & 0xE000, poll, a & 0x1FFF, self._out, a)
            return '%s(0x%04X);' % (self._out, a)
        b = body(m, mode, ob=ob)
        if m not in CTRL:
            if b is None:
                raise ValueError('no template for %s %s' % (m, mode))
            if m in ('CLI', 'PLP'):                       # (I may clear: a waiting interrupt is taken now)
                return '    %s\n    IRQCHK(%s); %s' % (b, here(nxt), goto_next(nxt))
            return '    %s\n    %s' % (b, goto_next(nxt))
        if m == 'SET':                                   # the next instruction in T mode, then on
            d2 = decode(lambda a: int(self.data[a & 0x1FFF]) if a < 0x2000 else 0, nxt)
            m2, mode2, ob2, n2, t2 = d2
            if nxt in self.patched:                       # (its opcode may change: the interpreter, in T mode)
                return '    SYNC_OUT(); c->tflag = 1; c->pc = %s; c->miss = 1; return;' % here(nxt)
            if m2 not in TCAP:                            # T changes only AND / ORA / EOR / ADC: a no-op here
                return '    %s' % goto_next(nxt)
            dyn2 = nxt in self.dynops
            return '    { /* SET: the next instruction, %s, in T mode */%s\n    CHK(0x%04X); CY(%d);\n    %s%s\n    }\n    %s' % (
                m2, self.ops_def(nxt, ob2), nxt, CYCLES[int(self.data[nxt])] + 3, body(m2, mode2, tmode=True, ob=None if dyn2 else ob2),
                undef(ob2), goto_next(nxt + n2))
        if m in COND:
            tgt = off + 2 + ((ob[0] ^ 0x80) - 0x80)
            if m == 'BRA':
                return '    CY(2); %s' % jump_rel(tgt)
            return '    if (%s) { CY(2); %s } %s' % (COND[m], jump_rel(tgt), goto_next(nxt))
        if m[:3] in ('BBR', 'BBS'):
            bit = int(m[3]); tgt = off + 3 + ((ob[1] ^ 0x80) - 0x80)
            cond = ('!' if m[:3] == 'BBR' else '') + '(ZR(O0) & %d)' % (1 << bit)
            return '    if (%s) { CY(2); %s } %s' % (cond, jump_rel(tgt), goto_next(nxt))
        if m == 'JMP' and mode == 'abs':
            return '    %s' % jump_abs(ob[0] | ob[1] << 8)
        if m == 'JMP' and mode == 'ind':
            return '    { const uint16_t p = 0x%04X; %s((uint16_t)(RD(p) | RD((uint16_t)(p + 1)) << 8)); }' % (ob[0] | ob[1] << 8, self._xout)
        if m == 'JMP' and mode == 'iax':
            return '    { const uint16_t p = (uint16_t)(0x%04X + rX); %s((uint16_t)(RD(p) | RD((uint16_t)(p + 1)) << 8)); }' % (ob[0] | ob[1] << 8, self._xout)
        if m == 'JSR':
            return '    { const uint16_t r = %s; PUSH(r >> 8); PUSH(r & 0xFF);%s } %s' % (here(off + 2), self.rspush(off + 3), jump_abs(ob[0] | ob[1] << 8))
        if m == 'BSR':
            tgt = off + 2 + ((ob[0] ^ 0x80) - 0x80)
            return '    { const uint16_t r = %s; PUSH(r >> 8); PUSH(r & 0xFF);%s } CY(2); %s' % (here(off + 1), self.rspush(off + 2), jump_rel(tgt))
        if m == 'RTS':
            if self._rs == 'rk':                          # the return's label kept by its stack slot (merged code)
                return '    { const unsigned k_ = (uint8_t)(rS + 1); uint16_t r = POP(); r |= POP() << 8; r++; POLL(r); RKPOP(k_, r); LOCAL(r); }'
            return '    { uint16_t r = POP(); r |= POP() << 8; r++; POLL(r);%s LOCAL(r); }' % (' RSPOP(r);' if self._rs else '')
        if m == 'RTI':
            return '    { SETP(POP() & ~TF); uint16_t r = POP(); r |= POP() << 8; %s(r); }' % self._xout
        if m == 'BRK':
            return '    { const uint16_t r = %s; PUSH(r >> 8); PUSH(r & 0xFF); PUSH(GETP() | BF); rP = (rP | IF) & ~DF; LEAVE((uint16_t)(RD(0xFFF6) | RD(0xFFF7) << 8)); }' % here(off + 2)
        if m == 'TAM':
            return '    { for (int i = 0; i < 8; i++) if (O0 & (1 << i)) SETMPR(i, rA); %s(%s); }' % (self._xout, here(nxt))
        raise ValueError(m)

    def code_dyn(self, m, mode, ob, n, off, nxt):
        """an instruction whose operand bytes code may write: O0... read them (ops_def); targets found when it runs"""
        here = lambda o: '(uint16_t)(base + 0x%04X)' % o
        goto_next = lambda o: ('goto l%04X;' % o) if o in self.labels_set() else '%s(%s);' % (self._out, here(o))
        if m not in CTRL:
            return '    %s\n    %s' % (body(m, mode), goto_next(nxt))
        if m in COND:
            return '    if (%s) { const uint16_t t2 = (uint16_t)(base + 0x%04X + (int8_t)O0); CY(2); POLL(t2); LOCAL(t2); } %s' % (
                COND[m], off + 2, goto_next(nxt))
        if m[:3] in ('BBR', 'BBS'):
            cond = ('!' if m[:3] == 'BBR' else '') + '(ZR(O0) & %d)' % (1 << int(m[3]))
            return '    if (%s) { const uint16_t t2 = (uint16_t)(base + 0x%04X + (int8_t)O1); CY(2); POLL(t2); LOCAL(t2); } %s' % (
                cond, off + 3, goto_next(nxt))
        if m == 'JMP' and mode == 'abs':
            return '    { const uint16_t t2 = (uint16_t)(O0 | O1 << 8); POLL(t2); LOCAL(t2); }'
        if m == 'JMP' and mode == 'ind':
            return '    { const uint16_t p = (uint16_t)(O0 | O1 << 8); %s((uint16_t)(RD(p) | RD((uint16_t)(p + 1)) << 8)); }' % self._xout
        if m == 'JMP' and mode == 'iax':
            return '    { const uint16_t p = (uint16_t)((O0 | O1 << 8) + rX); %s((uint16_t)(RD(p) | RD((uint16_t)(p + 1)) << 8)); }' % self._xout
        if m == 'JSR':
            return '    { const uint16_t r = %s, t2 = (uint16_t)(O0 | O1 << 8); PUSH(r >> 8); PUSH(r & 0xFF);%s POLL(t2); LOCAL(t2); }' % (here(off + 2), self.rspush(off + 3))
        if m == 'BSR':
            return '    { const uint16_t r = %s, t2 = (uint16_t)(base + 0x%04X + (int8_t)O0); PUSH(r >> 8); PUSH(r & 0xFF);%s CY(2); POLL(t2); LOCAL(t2); }' % (here(off + 1), off + 2, self.rspush(off + 2))
        if m == 'TAM':
            return '    { const uint8_t k = O0; for (int i = 0; i < 8; i++) if (k & (1 << i)) SETMPR(i, rA); %s(%s); }' % (self._xout, here(nxt))
        if m in ('RTS', 'RTI', 'BRK'):                    # (one byte: only its opcode can be written, and is guarded)
            return self.code_static(m, mode, ob, n, off, nxt)
        raise ValueError('operands written by code: %s %s' % (m, mode))

    def rspush(self, ret):
        """(rs) the shadow stack's push for a call returning to offset ret, if ret has a label here"""
        if not getattr(self, '_rs', False) or ret not in self.labels_set(): return ''
        if self._rs == 'rk':                              # (by the stack slot of the return address's low byte)
            return ' RKPUSH((uint8_t)(rS + 1), (uint16_t)(base + 0x%04X), &&l%04X);' % (ret, ret)
        return ' RSPUSH((uint16_t)(base + 0x%04X), &&l%04X);' % (ret, ret)

    def labels_set(self):
        if not hasattr(self, '_ls'): self._ls = set(self.labels)
        return self._ls


RUNTIME_H = r'''/* huc_ops.h - generated by tools/m2_recomp.py: the runtime the translated HuC6280 code uses. */
#pragma once
#include <stdint.h>
enum { CF = 0x01, ZF = 0x02, IF = 0x04, DF = 0x08, BF = 0x10, TF = 0x20, VF = 0x40, NF = 0x80 };
typedef struct VPort {          /* HUC_TIMED builds: the VDC's and VCE's ports, kept here for the translated code
                                   to use without the runtime (docs/19); the runtime's VDC uses the same fields */
    uint16_t *vram, *color;     /* VRAM (32 K words), the colours (512) */
    uint16_t mawr, marr, inc;   /* the write and read addresses, their step (CR bits 11-12) */
    uint16_t read_buffer, ctaddress;
    uint8_t latch, select;      /* the low byte written; the register selected */
    int32_t left_min;           /* while left - 1 is above it, no VDC event is due and nothing waits for the runtime */
    int32_t left_min1;          /* the SH2 hot code's (tools/m3_sh2.py): left_min + 1 while slot 0 is the hardware
                                   page (MPR0 FF), else VP_NEVER - no overflow when a run's cycles are added to it */
} VPort;
#define VSRC_CODE 0x8000            /* made by the game's code: converted on the console (D6b); its tags are 0x8000 |
                                       the frame (rt_marks.code, docs/56), so a converted cell is known unchanged */
#define VSRC_IS_CODE(t) ((t) & 0x8000)
struct Cpu;
unsigned rt_cdstep(struct Cpu *c, unsigned y);   /* docs/75 (HUC_CDSTEP) */
void rt_adstep(struct Cpu *c, unsigned a);       /* docs/76 */
void rt_pollskip(struct Cpu *c, int which);      /* docs/77 */
#ifdef HUC_MARKS
/* each 16-word unit of VRAM: the ripped output that wrote it, or VSRC_CODE; tag: what a write marks now (the output
   while a load list's entry copies, else VSRC_CODE). One place outside the Cpu, the tag first: the SH2 hot code
   reaches both from one address (docs/53, docs/54) */
typedef struct RtMarks { uint16_t tag, code; uint16_t v[2048]; } RtMarks;   /* code: the code's tag this frame */
extern RtMarks rt_marks;
#define VMARK(p, a) ((void)(p), rt_marks.v[(a) >> 4] = rt_marks.tag)
struct Cpu;
void rt_hook_cdread(struct Cpu *c);
void rt_hook_entry(struct Cpu *c, unsigned y);
void rt_hook_listend(struct Cpu *c);
void rt_marks_frame(void);
struct V32; extern void (*rt_draw_hook)(struct V32 *v);
#else
#define VMARK(p, a) ((void)0)
#endif
#define VP_NEVER 0x40000000
#define HSUB_N 32
typedef struct Cpu {
    uint8_t A, X, Y, S, P, speed, miss, tflag;   /* tflag: the interpreter's SET (the next instruction in T mode) */
    uint16_t pc;
    uint8_t MPR[8];
    uint8_t *rp[8];             /* each slot's memory, or 0: the hardware / unmapped (rd_slow / wr_slow) */
    uint8_t wok[8];             /* the slot may be written (RAM) */
    uint8_t *ram;               /* base RAM (bank F8): zero page $2000, stack $2100 */
    uint8_t *dirty[8];          /* each slot's bank's "written" flag: code loaded into RAM picks its block again */
    uint8_t idle;               /* HUC_IDLE_EXIT builds: the code left at a jump to itself */
    int32_t left;               /* HUC_TIMED builds: CPU cycles left before the runtime's next event (docs/17) */
    uint8_t irq_wait;           /* HUC_TIMED builds: an interrupt waits for the I flag: CLI / PLP leave the block */
    VPort vp;
    uint8_t hid[8];             /* a stage's merged hot code (docs/19): the block each slot's bank is there (0: none) */
    uint8_t xok;                /* a jump to another slot's block may go straight there (the runtime's conditions) */
    uint8_t vf;                 /* the SH2 hot code (tools/m3_sh2.py, docs/20): V in bit 7 while it runs */
    int32_t xmin;               /* the SH2 hot code (docs/22): such a jump goes straight there while left > xmin
                                   (xok, cycles left and - IRQ2 enabled - the CD unit's next event ahead); else never */
    void *user;
    void *hsub[HSUB_N];         /* the SH2 hot code's shared subroutines and exits (docs/23): one GBR load away */
} Cpu;
#define COLD __attribute__((cold))
COLD uint8_t rd_slow(Cpu *c, uint16_t a);
COLD void wr_slow(Cpu *c, uint16_t a, uint8_t v);
void wr_phys(Cpu *c, uint32_t pa, uint8_t v);     /* ST0-ST2 */
void set_mpr(Cpu *c, int i, uint8_t v);
COLD void rt_speed(Cpu *c, int v);                 /* CSL / CSH in the 32X runtime (HUC_TIMED) */
void block_move(Cpu *c, int kind, uint16_t src, uint16_t dst, uint16_t len);
COLD uint8_t op_adc(Cpu *c, uint8_t a, uint8_t x);     /* (the translation does the binary mode inline) */
COLD uint8_t op_sbc(Cpu *c, uint8_t a, uint8_t x);
int trace_check(Cpu *c, uint16_t pc);             /* TRACECHECK builds: 1 = stop / take an interrupt here */
#ifdef HUC_NODIRTY                                /* (a build where loads are known: no written-bank flags) */
#define MARK_DIRTY(s) do { } while (0)
#else
#define MARK_DIRTY(s) (*c->dirty[s] = 1)
#endif
static inline uint8_t RD_(Cpu *c, uint16_t a) { uint8_t *p = c->rp[a >> 13]; return __builtin_expect(p != 0, 1) ? p[a & 0x1FFF] : rd_slow(c, a); }
static inline void WR_(Cpu *c, uint16_t a, uint8_t v) { if (__builtin_expect(c->wok[a >> 13], 1)) { c->rp[a >> 13][a & 0x1FFF] = v; MARK_DIRTY(a >> 13); } else wr_slow(c, a, v); }
#if defined(HUC_TIMED) && defined(HUC_LOCAL)
/* The 32X runtime's build (docs/17): each instruction counts its cycles down in left_ (CY), and a branch back, a jump
   back, an RTS, a JMP to a found address poll it (POLL): below 0, the block leaves for the runtime's next event. A
   hardware access tells the runtime the time (c->left) and takes back the cycles it now has. */
#define CY(n) (left_ -= (n))
#define POLL(addr) do { if (__builtin_expect(left_ < 0, 0)) LEAVE(addr); } while (0)
#define IRQCHK(addr) do { if (__builtin_expect(c->irq_wait, 0)) LEAVE(addr); } while (0)
#define LEFT_DECL int32_t left_ = c->left;
#define LEFT_OUT , c->left = left_
#define RELOAD_LEFT() (left_ = c->left, c->xok = 0, c->xmin = 0x7FFFFFFF)   /* (after a block move: new code) */
#define RD(a) ({ const uint16_t a_ = (uint16_t)(a); uint8_t *const p_ = c->rp[a_ >> 13]; uint8_t v_; \
    if (__builtin_expect(p_ != 0, 1)) v_ = p_[a_ & 0x1FFF]; \
    else if (HWC(a_) && hw_rd_fast(c, a_, &v_, &left_)) { } \
    else { c->left = left_; v_ = rd_slow(c, a_); left_ = c->left; } v_; })
/* the VDC's and VCE's ports at a constant address of the hardware page, without the runtime while no event is due
   (VPort): the extra cycle of a VDC / VCE access, then what the runtime's v32_write / v32_read / v32_vce_* do. The
   VDC's register select, MAWR, MARR, VRAM; the VCE's colour address and colours. Anything else: 0, the runtime. */
static inline __attribute__((always_inline)) int hw_wr_fast(Cpu *c, unsigned a, uint8_t v, int32_t *lp)
{
    VPort *const p = &c->vp;
    if (a >= 0x800 || *lp - 1 <= p->left_min) return 0;
    if (a < 0x400) {
        const int msb = a & 1;
        if ((a & 3) == 0) { *lp -= 1; p->select = v & 0x1F; return 1; }
        if ((a & 3) == 1) { *lp -= 1; return 1; }
        switch (p->select) {
        case 0: *lp -= 1; p->mawr = (uint16_t)(msb ? (p->mawr & 0x00FF) | v << 8 : (p->mawr & 0xFF00) | v); return 1;
        case 1: *lp -= 1; p->marr = (uint16_t)(msb ? (p->marr & 0x00FF) | v << 8 : (p->marr & 0xFF00) | v);
            if (msb) { p->read_buffer = p->marr < 0x8000 ? p->vram[p->marr] : 0; p->marr += p->inc; }
            return 1;
        case 2: *lp -= 1;
            if (!msb) p->latch = v;
            else { if (p->mawr < 0x8000) { p->vram[p->mawr] = (uint16_t)(p->latch | v << 8); VMARK(p, p->mawr); } p->mawr += p->inc; }
            return 1;
        }
        return 0;
    }
    switch (a & 7) {
    case 2: *lp -= 1; p->ctaddress = (uint16_t)((p->ctaddress & 0x100) | v); return 1;
    case 3: *lp -= 1; p->ctaddress = (uint16_t)((p->ctaddress & 0x0FF) | (v & 1) << 8); return 1;
    case 4: *lp -= 1; p->color[p->ctaddress & 0x1FF] = (uint16_t)((p->color[p->ctaddress & 0x1FF] & 0x100) | v); return 1;
    case 5: *lp -= 1; p->color[p->ctaddress & 0x1FF] = (uint16_t)((p->color[p->ctaddress & 0x1FF] & 0xFF) | (v & 1) << 8);
        p->ctaddress = (p->ctaddress + 1) & 0x1FF; return 1;
    }
    return 0;
}
static inline __attribute__((always_inline)) int hw_rd_fast(Cpu *c, unsigned a, uint8_t *v, int32_t *lp)
{
    VPort *const p = &c->vp;
    if (a >= 0x800 || *lp - 1 <= p->left_min) return 0;
    if (a < 0x400) {
        if (!(a & 2)) return 0;                          /* (the status: its bits come from the VDC's events) */
        *lp -= 1;
        const int msb = a & 1;
        *v = (uint8_t)(msb ? p->read_buffer >> 8 : p->read_buffer);
        if (p->select == 2 && msb) { p->read_buffer = p->marr < 0x8000 ? p->vram[p->marr] : 0; p->marr += p->inc; }
        return 1;
    }
    switch (a & 7) {
    case 4: *lp -= 1; *v = (uint8_t)p->color[p->ctaddress & 0x1FF]; return 1;
    case 5: *lp -= 1; *v = (uint8_t)(((p->color[p->ctaddress & 0x1FF] >> 8) & 1) | 0xFE); p->ctaddress = (p->ctaddress + 1) & 0x1FF; return 1;
    }
    return 0;
}
#define HWC(a_) (__builtin_constant_p(a_) && (a_) < 0x2000 && c->MPR[0] == 0xFF)   /* a constant hardware-page address */
#define VP_STALE() (c->vp.left_min = 0x7FFFFFFF, c->vp.left_min1 = VP_NEVER, c->xok = 0, c->xmin = 0x7FFFFFFF)
/* CSL / CSH: the runtime counts the cycles so far at the old speed first, and the budget's end stays where it is in
   time (DraculaX docs/50: a speed change inside a block used to time every cycle since the block's entry at the new
   speed) */
#define SPEED(v) do { c->left = left_; rt_speed(c, v); left_ = c->left; } while (0)
#define WR(a, v) do { const uint16_t a_ = (uint16_t)(a); const uint8_t v_ = (uint8_t)(v); \
    if (HWC(a_) && hw_wr_fast(c, a_, v_, &left_)) { } \
    else if (__builtin_expect(c->wok[a_ >> 13], 1)) { c->rp[a_ >> 13][a_ & 0x1FFF] = v_; MARK_DIRTY(a_ >> 13); } \
    else { c->left = left_; wr_slow(c, a_, v_); left_ = c->left; } } while (0)
#define IOWP(pa, v) do { const uint8_t pv_ = (uint8_t)(v); if (hw_wr_fast(c, (pa) & 0x1FFF, pv_, &left_)) { } \
    else { c->left = left_; wr_phys(c, pa, pv_); left_ = c->left; } } while (0)
#else
#define CY(n) ((void)0)
#define POLL(addr) do { } while (0)
#define IRQCHK(addr) do { } while (0)
#define LEFT_DECL
#define LEFT_OUT
#define RELOAD_LEFT() ((void)0)
#define RD(a) RD_(c, (uint16_t)(a))
#define WR(a, v) WR_(c, (uint16_t)(a), (uint8_t)(v))
#define IOWP(pa, v) wr_phys(c, pa, v)
#define VP_STALE() ((void)0)
#ifdef HUC_TIMED
#define SPEED(v) rt_speed(c, v)                         /* (the interpreter: c->left is the CPU's) */
#else
#define SPEED(v) (c->speed = (uint8_t)(v))
#endif
#endif
#ifdef HUC_LOCAL
/* A translated block (blk_*.c): the registers are C locals, so the compiler keeps them in machine registers. The
   flags N, V, Z and C are kept apart from P, lazily (32-bit, so no masking): Z is set when zr_ is 0, N is bit 7 of
   nr_ (BIT / TST / TSB / TRB set them from different values), V is bit 7 of vf_, C is cf_ (0 or 1); P_ holds the
   others (I, D). The Cpu
   struct is brought up to date (SYNC_OUT) wherever anything else can look at it: before the trace check of each
   instruction (TRACECHECK builds), a block move, a decimal ADC / SBC, and every exit to the dispatcher. */
#define rA A_
#define rX X_
#define rY Y_
#define rS S_
#define rP P_
#define RAMP ram_
#define ZN(v) (zr_ = nr_ = (uint8_t)(v))
#define ZNB(z, n) (zr_ = (uint8_t)(z), nr_ = (uint8_t)(n))
#define ZSET (!zr_)
#define NSET (nr_ & NF)
#define SETC(v) (cf_ = (v))                               /* v: 0 or 1 */
#define CSET (cf_)
#define SETVB(v) (vf_ = (v))                              /* V: bit 7 of v */
#define VSET (vf_ & 0x80)
#define GETP() ((uint8_t)((rP & ~(ZF | NF | CF | VF)) | (zr_ ? 0 : ZF) | (nr_ & NF) | cf_ | ((vf_ & 0x80) >> 1)))
#define SETP(v) do { const uint8_t p_ = (uint8_t)(v); rP = p_; zr_ = (uint8_t)(~p_ & ZF); nr_ = p_; cf_ = p_ & CF; vf_ = (unsigned)p_ << 1; } while (0)
#define SYNC_OUT() (c->A = rA, c->X = rX, c->Y = rY, c->S = rS, c->P = GETP() LEFT_OUT)
#define SYNC_IN_AXY() (rA = c->A, rX = c->X, rY = c->Y)     /* after a block move: it pops them from the stack */
#define BLOCK_LOCALS uint8_t A_ = c->A, X_ = c->X, Y_ = c->Y, S_ = c->S, P_; unsigned zr_, nr_, cf_, vf_; uint8_t *const ram_ = c->ram; LEFT_DECL SETP(c->P)
#define ADC(a, x) ({ uint8_t r_; \
    if (__builtin_expect(rP & DF, 0)) { c->P = GETP(); r_ = op_adc(c, a, x); SETP(c->P); } \
    else { const unsigned l_ = (unsigned)(a) + (x) + cf_; \
           vf_ = ~((a) ^ (x)) & ((a) ^ l_); cf_ = l_ >> 8; r_ = (uint8_t)l_; ZN(r_); } \
    r_; })
#define SBC(a, x) ({ uint8_t r_; \
    if (__builtin_expect(rP & DF, 0)) { c->P = GETP(); r_ = op_sbc(c, a, x); SETP(c->P); } \
    else { const unsigned l_ = (unsigned)(a) - (x) - (cf_ ^ 1); \
           vf_ = ((a) ^ l_) & ((a) ^ (x)); cf_ = ((l_ >> 8) & 1) ^ 1; r_ = (uint8_t)l_; ZN(r_); } \
    r_; })
#else
/* the interpreter and the runtime: the Cpu struct itself */
#define rA (c->A)
#define rX (c->X)
#define rY (c->Y)
#define rS (c->S)
#define rP (c->P)
#define RAMP (c->ram)
#define ZN(v) (c->P = (c->P & ~(ZF | NF)) | ((uint8_t)(v) ? 0 : ZF) | ((v) & NF))
#define ZNB(z, n) (c->P = (c->P & ~(ZF | NF)) | ((uint8_t)(z) ? 0 : ZF) | ((n) & NF))
#define ZSET (c->P & ZF)
#define NSET (c->P & NF)
#define SETC(v) (c->P = (c->P & ~CF) | (uint8_t)(v))
#define CSET (c->P & CF)
#define SETVB(v) (c->P = (c->P & ~VF) | (((v) & 0x80) >> 1))
#define VSET (c->P & VF)
#define GETP() (c->P)
#define SETP(v) (c->P = (uint8_t)(v))
#define SYNC_OUT() ((void)0)
#define SYNC_IN_AXY() ((void)0)
#define ADC(a, x) op_adc(c, a, x)
#define SBC(a, x) op_sbc(c, a, x)
#endif
#define ZR(z) (RAMP[(uint8_t)(z)])
#define ZW(z, v) (RAMP[(uint8_t)(z)] = (uint8_t)(v))
#define ZPTR(z) ((uint16_t)(ZR(z) | ZR((uint8_t)((z) + 1)) << 8))
#define PUSH(v) (RAMP[0x100 + rS--] = (uint8_t)(v))
#define POP() (RAMP[0x100 + ++rS])
#define CMPL(r, m) do { const uint8_t r_ = (r), m_ = (m); ZN((uint8_t)(r_ - m_)); SETC(r_ >= m_); } while (0)
#define SETMPR(i, v) set_mpr(c, i, v)
#define BMOVE(c, k, s, d, l) block_move(c, k, s, d, l)
#ifdef HUC_IDLE_EXIT          /* the 32X runtime: a jump to itself waits for an interrupt, so the dispatcher skips
                                 the time to the next one (docs/16) */
#define IDLE(lbl, addr) do { c->idle = 1; LEAVE(addr); } while (0)
#else
#define IDLE(lbl, addr) do { POLL(addr); goto lbl; } while (0)
#endif
#ifdef TRACECHECK
#define CHK(off) do { SYNC_OUT(); if (trace_check(c, (uint16_t)(base + (off)))) return; } while (0)   /* it set c->pc */
#else
#define CHK(off) do { } while (0)
#endif
'''

RUNTIME_C = r'''/* huc_rt.c - generated by tools/m2_recomp.py: the helpers behind huc_ops.h (Mednafen's semantics). */
#include "huc_ops.h"
uint8_t op_adc(Cpu *c, uint8_t A, uint8_t x)
{
    if (c->P & DF) {
        unsigned tmp = (A & 0x0F) + (x & 0x0F) + (c->P & 1);
        if (tmp >= 0x0A) tmp += 0x06;
        tmp += (A & 0xF0) + (x & 0xF0);
        if (tmp >= 0xA0) tmp += 0x60;
        c->P &= ~(ZF | NF | CF);
        if (tmp & 0xFF00) c->P |= CF;
        A = (uint8_t)tmp;
        c->P |= (A ? 0 : ZF) | (A & NF);
    } else {
        unsigned l = A + x + (c->P & 1);
        c->P &= ~(ZF | CF | NF | VF);
        c->P |= ((((A ^ x) & 0x80) ^ 0x80) & ((A ^ l) & 0x80)) >> 1;
        c->P |= (l >> 8) & CF;
        A = (uint8_t)l;
        c->P |= (A ? 0 : ZF) | (A & NF);
    }
    return A;
}
uint8_t op_sbc(Cpu *c, uint8_t A, uint8_t x)
{
    if (c->P & DF) {
        const uint8_t m = (A & 0xF) - (x & 0xF) - ((c->P & 1) ^ 1);
        const uint8_t n = (A >> 4) - (x >> 4) - ((m >> 4) & 1);
        uint8_t res = (uint8_t)((n << 4) | (m & 0xF));
        c->P &= ~(ZF | NF | CF);
        if (m & 0x10) res -= 0x06;
        if (n & 0x10) res -= 0x60;
        A = res;
        c->P |= ((n >> 4) & 0x1) ^ 1;
        c->P |= (A ? 0 : ZF) | (A & NF);
    } else {
        unsigned l = A - x - ((c->P & 1) ^ 1);
        c->P &= ~(ZF | CF | NF | VF);
        c->P |= ((A ^ l) & (A ^ x) & 0x80) >> 1;
        c->P |= ((l >> 8) & CF) ^ CF;
        A = (uint8_t)l;
        c->P |= (A ? 0 : ZF) | (A & NF);
    }
    return A;
}
#ifdef TRACECHECK
void bm_byte(Cpu *c, unsigned done);              /* trace_main.c: a run slice may end inside a block move (docs/14) */
#endif
void block_move(Cpu *c, int kind, uint16_t src, uint16_t dst, uint16_t len)
{
    PUSH(c->Y); PUSH(c->A); PUSH(c->X);
    unsigned alt = 0;
#if defined(HUC_TIMED) && !defined(TRACECHECK)
    if (kind == 3) {                              /* (Sapphire docs/14: the card's port to the VRAM port, the runtime's) */
        extern int rt_tia_card_vram(Cpu *c, uint16_t src, uint16_t dst, uint32_t len);
        const int n = rt_tia_card_vram(c, src, dst, len ? len : 0x10000u);
        if (n) {
            src = (uint16_t)(src + n); alt = (unsigned)n & 1;
            if ((uint32_t)n == (len ? len : 0x10000u)) { c->X = POP(); c->A = POP(); c->Y = POP(); return; }
            len = (uint16_t)(len - n);
        }
    }
#endif
#ifdef TRACECHECK
    unsigned done = 0;
#endif
    do {
        uint16_t w = dst;
        switch (kind) {
        case 0: WR(dst, RD(src)); src++; dst++; break;                    /* TII */
        case 1: WR(dst, RD(src)); src--; dst--; break;                    /* TDD */
        case 2: WR(dst, RD(src)); src++; break;                           /* TIN */
        case 3: w = (uint16_t)(dst + alt); WR(w, RD(src)); src++; alt ^= 1; break; /* TIA */
        case 4: WR(dst, RD((uint16_t)(src + alt))); dst++; alt ^= 1; break; /* TAI */
        }
        len--;
#ifdef HUC_TIMED
        c->left -= 6;                             /* (6 cycles a byte: the runtime sees each access's time) */
#endif
#ifdef HUC_NODIRTY
        if (c->wok[w >> 13]) *c->dirty[w >> 13] = 1;   /* code may be copied into place this way (docs/05) */
#else
        (void)w;
#endif
#ifdef TRACECHECK
        bm_byte(c, ++done);
#endif
    } while (len);
    c->X = POP(); c->A = POP(); c->Y = POP();
}
'''


def interp():
    """the interpreter: one instruction at c->pc. Returns after it (c->pc = the next instruction)."""
    L = ['/* interp.c - generated by tools/m2_recomp.py: one HuC6280 instruction at c->pc (Mednafen semantics). */',
         '#include "huc_ops.h"', '#ifdef TRACECHECK', '#define base 0', '#endif', 'void interp_one(Cpu *c)', '{',
         '    const uint16_t pc0 = c->pc;', '    const uint8_t op = RD(pc0);',
         '    const int tm = c->tflag; c->tflag = 0; (void)tm;',
         '#define O0 RD((uint16_t)(pc0 + 1))', '#define O1 RD((uint16_t)(pc0 + 2))', '#define O2 RD((uint16_t)(pc0 + 3))',
         '#define O3 RD((uint16_t)(pc0 + 4))', '#define O4 RD((uint16_t)(pc0 + 5))', '#define O5 RD((uint16_t)(pc0 + 6))',
         '    switch (op) {']
    for op in range(256):
        m, mode = OPS[op] if OPS[op] else ('???', 'imp')
        n = SIZE[mode]
        nxt = '(uint16_t)(pc0 + %d)' % n
        b = body(m, mode)
        if m not in CTRL:
            if m in TCAP:
                b = 'if (tm) %s else %s' % (body(m, mode, tmode=True), b)
            L.append('    case 0x%02X: %s c->pc = %s; return;  /* %s */' % (op, b, nxt, m))
            continue
        if m in COND:
            L.append('    case 0x%02X: c->pc = (%s) ? (uint16_t)(pc0 + 2 + (int8_t)O0) : %s; return;' % (op, COND[m], nxt))
        elif m[:3] in ('BBR', 'BBS'):
            cond = ('!' if m[:3] == 'BBR' else '') + '(ZR(O0) & %d)' % (1 << int(m[3]))
            L.append('    case 0x%02X: c->pc = (%s) ? (uint16_t)(pc0 + 3 + (int8_t)O1) : %s; return;' % (op, cond, nxt))
        elif m == 'JMP' and mode == 'abs':
            L.append('    case 0x%02X: c->pc = (uint16_t)(O0 | O1 << 8); return;' % op)
        elif m == 'JMP' and mode == 'ind':
            L.append('    case 0x%02X: { const uint16_t p = (uint16_t)(O0 | O1 << 8); c->pc = (uint16_t)(RD(p) | RD((uint16_t)(p + 1)) << 8); } return;' % op)
        elif m == 'JMP' and mode == 'iax':
            L.append('    case 0x%02X: { const uint16_t p = (uint16_t)((O0 | O1 << 8) + c->X); c->pc = (uint16_t)(RD(p) | RD((uint16_t)(p + 1)) << 8); } return;' % op)
        elif m == 'JSR':
            L.append('    case 0x%02X: { const uint16_t r = (uint16_t)(pc0 + 2), t = (uint16_t)(O0 | O1 << 8); PUSH(r >> 8); PUSH(r & 0xFF); c->pc = t; } return;' % op)
        elif m == 'BSR':
            L.append('    case 0x%02X: { const uint16_t r = (uint16_t)(pc0 + 1); PUSH(r >> 8); PUSH(r & 0xFF); c->pc = (uint16_t)(pc0 + 2 + (int8_t)O0); } return;' % op)
        elif m == 'RTS':
            L.append('    case 0x%02X: { uint16_t r = POP(); r |= POP() << 8; c->pc = (uint16_t)(r + 1); } return;' % op)
        elif m == 'RTI':
            L.append('    case 0x%02X: { c->P = POP() & ~TF; uint16_t r = POP(); r |= POP() << 8; c->pc = r; } return;' % op)
        elif m == 'BRK':
            L.append('    case 0x%02X: { const uint16_t r = (uint16_t)(pc0 + 2); PUSH(r >> 8); PUSH(r & 0xFF); PUSH(c->P | BF); c->P = (c->P | IF) & ~DF; c->pc = (uint16_t)(RD(0xFFF6) | RD(0xFFF7) << 8); } return;' % op)
        elif m == 'TAM':
            L.append('    case 0x%02X: { const uint8_t k = O0; for (int i = 0; i < 8; i++) if (k & (1 << i)) SETMPR(i, c->A); c->pc = %s; } return;' % (op, nxt))
        elif m == 'SET':
            L.append('    case 0x%02X: c->tflag = 1; c->pc = %s; return;' % (op, nxt))
    L += ['    }', '#undef O0', '#undef O1', '#undef O2', '#undef O3', '#undef O4', '#undef O5', '}']
    return '\n'.join(L)


STORES = {'STA', 'STX', 'STY', 'STZ', 'INC', 'DEC', 'ASL', 'LSR', 'ROL', 'ROR', 'TSB', 'TRB',
          'RMB0', 'RMB1', 'RMB2', 'RMB3', 'RMB4', 'RMB5', 'RMB6', 'RMB7', 'SMB0', 'SMB1', 'SMB2', 'SMB3',
          'SMB4', 'SMB5', 'SMB6', 'SMB7'}


def find_patched(blocks):
    """self-modifying code: every byte the code map saw the game rewrite (m2_code.py), and every absolute store (any
    block) whose address is a code byte of a block that runs in that address's slot. Such an instruction is translated with its operand bytes read from memory when it runs
    (Block.dynops, docs/16); if its opcode may be written too (Block.patched), the translation runs only while the
    opcode is the one translated, and the interpreter otherwise. (Many of these are other stages' data addresses that fall on
    this overlay's code: reading the operand is right either way.)"""
    info = {}
    for b in blocks:
        ins = b.instructions(); info[b.key] = (ins, SLOT.get(b.bank, b.slot(ins)))
    stores = set()
    for b in blocks:
        for off, (m, mode, ob, n, t) in info[b.key][0].items():
            if m in STORES and mode == 'abs':
                stores.add(ob[0] | ob[1] << 8)
    report = []
    for b in blocks:
        ins, slot = info[b.key]
        if slot is None or b.bank < 0x20: continue      # ROM can't be written. (Block moves into code banks are
                                                          # the game copying code into place: loading, docs/05.)
        op, opd = set(), set()
        for start, (m, mode, ob, n, t) in ins.items():    # bytes the code map saw the game rewrite (m2_code.py)
            if b.own[start]: op.add(start)
            elif b.own[start + 1:start + n].any(): opd.add(start)
        for a in stores:
            if a >> 13 != slot: continue
            o = a & 0x1FFF
            for start in range(max(0, o - 6), o + 1):
                if start in ins and start <= o < start + ins[start][3]:
                    (op if o == start else opd).add(start)
        if op or opd:
            b.patched = frozenset(op); b.dynops = frozenset(opd - op)
            report.append('%s (slot %d): opcodes %s; operands %s' % (b.key, slot, ' '.join('%04X' % (slot * 0x2000 + h) for h in sorted(op)) or '-',
                                                                  ' '.join('%04X' % (slot * 0x2000 + h) for h in sorted(b.dynops)) or '-'))
    return report


def merge_blocks(W):
    """m2_code.py's blocks by bank; blocks of one bank whose code bytes agree are one block - every byte that is
    code in either (the bank also holds variables that change, which may differ). Returns [Block]."""
    by_bank = {}
    for p in sorted(glob.glob(os.path.join(W, 'm2', '*.npz'))):
        k = os.path.basename(p)[:-4]; d = np.load(p)
        by_bank.setdefault(int(k[:2], 16), []).append((k, d['bytes'], d['starts'],
                                                       d['own'] if 'own' in d.files else np.zeros(0x2000, bool)))
    out = []
    for bank, lst in sorted(by_bank.items()):
        groups = []
        for k, data, st, own in lst:
            # the bytes of every instruction seen (opcode + operands)
            mask = np.zeros(0x2000, bool)
            for o in np.nonzero(st)[0]:
                n = SIZE[OPS[data[o]][1]] if OPS[data[o]] else 1
                mask[o:o + n] = True
            for g in groups:
                either = (mask | g['mask']) & ~(own | g['own'])   # every code byte of both must be the same (but
                if (g['data'][either] == data[either]).all():     # those the game rewrites)
                    g['starts'] |= st; g['keys'].append(k); g['own'] |= own
                    # keep the code bytes of both
                    g['data'] = np.where(mask & ~g['mask'], data, g['data']); g['mask'] |= mask
                    break
            else:
                groups.append(dict(data=data.copy(), starts=st.copy(), mask=mask, keys=[k], own=own.copy()))
        for i, g in enumerate(groups):
            key = '%02X_%d' % (bank, i)
            b = Block(key, bank, g['data'], g['starts']); b.sources = g['keys']; b.mask = g['mask']; b.own = g['own']
            out.append(b)
    return out


def load_patch_alts(W):
    """PATCH_ALTS from <work>/m2/patch_alts.json (docs/13); also tools/m3_speedtest.py split's"""
    pa = os.path.join(W, 'm2', 'patch_alts.json')
    if not os.path.exists(pa): return False
    import json
    for k, v in json.load(open(pa)).items():
        bk, of = k.split(':'); PATCH_ALTS[(int(bk, 16), int(of, 16))] = [int(x) for x in v]
    return True


def main():
    W, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(out, 'blk_*.c')):  # (a block of an earlier code map would still be compiled)
        os.remove(f)
    open(os.path.join(out, 'huc_ops.h'), 'w').write(RUNTIME_H)
    open(os.path.join(out, 'huc_rt.c'), 'w').write(RUNTIME_C)
    if load_patch_alts(W): print('patch_alts: %d places' % len(PATCH_ALTS))
    blocks = merge_blocks(W)
    for line in find_patched(blocks):
        print('code written by code, its bytes read when it runs:', line)
    reg = ['/* blocks.c - generated by tools/m2_recomp.py */', '#include "huc_ops.h"', '#include <stddef.h>',
           'typedef struct { uint8_t bank; const char *key; void (*fn)(Cpu *); const uint8_t *code; const uint8_t *mask; } BlockDef;']
    total = 0
    for b in blocks:
        src = b.gen()
        total += len(b.labels)
        open(os.path.join(out, 'blk_%s.c' % b.key), 'w').write('#define HUC_LOCAL\n#include "huc_ops.h"\n' + src)
        reg.append('void blk_%s(Cpu *c);' % b.key)
        reg.append('static const uint8_t code_%s[8192] = {%s};' % (b.key, ','.join(str(int(x)) for x in b.data)))
        m = b.mask.copy()                                 # the bytes that pick the block: code, but not code that code
        for o in b.patched:                               # writes (the interpreter runs it, always current)
            m[o:o + (SIZE[OPS[b.data[o]][1]] if OPS[b.data[o]] else 1)] = False
        for o in b.dynops:                                # nor operands that code writes (read when they run)
            m[o + 1:o + (SIZE[OPS[b.data[o]][1]] if OPS[b.data[o]] else 1)] = False
        reg.append('static const uint8_t mask_%s[8192] = {%s};' % (b.key, ','.join('1' if x else '0' for x in m)))
    reg.append('const BlockDef blocks[] = {')
    reg += ['    { 0x%02X, "%s", blk_%s, code_%s, mask_%s },' % (b.bank, b.key, b.key, b.key, b.key) for b in blocks]
    reg.append('    { 0, NULL, NULL, NULL, NULL } };')
    open(os.path.join(out, 'blocks.c'), 'w').write('\n'.join(reg) + '\n')
    open(os.path.join(out, 'interp.c'), 'w').write(interp() + '\n')
    for b in blocks:
        print('%s  from %s  %d labels (%d seen)' % (b.key, ','.join(b.sources), len(b.labels), len(b.starts)))
    print('%d blocks, %d labels' % (len(blocks), total))


if __name__ == '__main__':
    main()
