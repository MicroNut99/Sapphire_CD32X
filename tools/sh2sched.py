"""sh2sched.py - docs/32: the SH2 hot code's load-use pairs broken where an instruction can move between them.

A register loaded from memory and read by the next instruction costs a cycle more (Hitachi SH7604 manual; ares's
console timing, timing.hpp). Within a straight run of the assembler's items (no label, branch, end or raw text
between), an instruction that depends on nothing it passes is moved in after the load: one from further on, or the
one just before the load. Registers, T, MAC and GBR are tracked; no memory access passes a store, no store passes one.
"""
import os
import re

SLOT_BACK = int(os.environ.get('SPLIT_SLOT_BACK', '1'))   # a bt/s slot into a load stall before it (docs/43)

_REG = re.compile(r'\br(1[0-5]|[0-9])\b')


def _split(args):
    out = []; depth = 0; cur = ''
    for ch in args:
        if ch == '(': depth += 1
        elif ch == ')': depth -= 1
        if ch == ',' and depth == 0: out.append(cur.strip()); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out


def _regs(s):
    return {'r' + m for m in _REG.findall(s)}


def info(it):
    """(reads, writes, mem: None / 'ld' / 'st', is a load writing (its register) or None, barrier)"""
    if it[0] == 'ld':
        return set(), {it[2]}, 'ld', it[2], False
    if it[0] != 'i': return set(), set(), None, None, True
    t = it[1]
    op, _, args = t.partition('\t')
    a = _split(args)
    R, W = set(), set()
    if op == 'nop': return R, W, None, None, False
    if op in ('sett', 'clrt'): return R, {'T'}, None, None, False
    if op.startswith('mov') and op not in ('movt', 'mova'):
        if len(a) != 2: return R, W, None, None, True
        s, d = a
        if s.startswith('@'):                     # a load
            R |= _regs(s) | ({'GBR'} if 'gbr' in s else set())
            if s.startswith('@') and s.endswith('+'): W |= _regs(s)
            W |= _regs(d)
            if 'pc' in s: return R, W, 'ld', d, False
            return R, W, 'ld', d, False
        if d.startswith('@'):                     # a store
            R |= _regs(s) | _regs(d) | ({'GBR'} if 'gbr' in d else set())
            if d.startswith('@-'): W |= _regs(d)
            return R, W, 'st', None, False
        if s.startswith('#'): return R, _regs(d), None, None, False
        return _regs(s), _regs(d), None, None, False
    if op == 'movt': return {'T'}, _regs(a[0]), None, None, False
    if op in ('add', 'sub', 'and', 'or', 'xor'):
        if a[0].startswith('#'): return _regs(a[1]), _regs(a[1]), None, None, False
        return _regs(a[0]) | _regs(a[1]), _regs(a[1]), None, None, False
    if op in ('addc', 'subc', 'negc'):
        return _regs(a[0]) | _regs(a[1]) | {'T'}, _regs(a[1]) | {'T'}, None, None, False
    if op in ('extu.b', 'extu.w', 'exts.b', 'exts.w', 'swap.b', 'swap.w', 'not', 'neg'):
        return _regs(a[0]), _regs(a[1]), None, None, False
    if op in ('shll', 'shlr', 'shal', 'shar', 'rotl', 'rotr', 'dt'):
        return _regs(a[0]), _regs(a[0]) | {'T'}, None, None, False
    if op in ('rotcl', 'rotcr'): return _regs(a[0]) | {'T'}, _regs(a[0]) | {'T'}, None, None, False
    if op in ('shll2', 'shll8', 'shll16', 'shlr2', 'shlr8', 'shlr16'): return _regs(a[0]), _regs(a[0]), None, None, False
    if op in ('tst', 'cmp/eq', 'cmp/gt', 'cmp/ge', 'cmp/hs', 'cmp/hi', 'cmp/str'):
        if a[0].startswith('#'): return _regs(a[1]), {'T'}, None, None, False
        return _regs(a[0]) | _regs(a[1]), {'T'}, None, None, False
    if op in ('cmp/pz', 'cmp/pl'): return _regs(a[0]), {'T'}, None, None, False
    if op == 'stc' and a[0] == 'gbr': return {'GBR'}, _regs(a[1]), None, None, False
    if op in ('mulu.w', 'muls.w', 'mul.l', 'dmulu.l', 'dmuls.l'): return _regs(a[0]) | _regs(a[1]), {'MAC'}, None, None, False
    if op == 'sts' and a[0] in ('macl', 'mach'): return {'MAC'}, _regs(a[1]), None, None, False
    return R, W, None, None, True                 # (anything else stays where it is)


def _indep(x, y):
    """x and y can change order"""
    rx, wx, mx, _, bx = x; ry, wy, my, _, by = y
    if bx or by: return False
    if wx & ry or rx & wy or wx & wy: return False
    if (mx == 'st' and my) or (my == 'st' and mx): return False
    return True


def schedule(items, K=int(os.environ.get("SPLIT_SCHED_K", "6"))):
    """the items with load-use pairs broken where they can be (in place in each straight run); returns the moves"""
    moves = 0
    k = 0; n = len(items)
    delayed = {'bra', 'bt/s', 'bf/s'}
    start = 0
    def run(lo, hi):
        nonlocal moves
        p = lo
        while p < hi - 1:
            a = info(items[p]); b = info(items[p + 1])
            if a[3] is not None and a[3] in b[0]:
                done = False
                for q in range(p + 2, min(hi, p + 1 + K)):     # one from further on
                    c = info(items[q])
                    if c[4]: break
                    if a[3] in c[0] or a[3] in c[1]: continue
                    if all(_indep(info(items[x]), c) for x in range(p + 1, q)):
                        if c[3] is not None and c[3] in b[0]: continue
                        items.insert(p + 1, items.pop(q)); moves += 1; done = True; break
                if not done and p > lo:                         # the one before the load, after it
                    e = info(items[p - 1])
                    if _indep(e, a) and not (e[3] is not None and e[3] in b[0]) and not (p - 2 >= lo and _stall(items[p - 2], items[p])):
                        items[p - 1], items[p] = items[p], items[p - 1]; moves += 1
            p += 1
    def _stall(x, y):
        ix, iy = info(x), info(y)
        return ix[3] is not None and ix[3] in iy[0]
    globals()['_stall'] = _stall
    while k < n:
        it = items[k]
        if it[0] in ('i', 'ld'):
            k += 1; continue
        if start < k: run(start, k)
        if SLOT_BACK and it[0] == 'b' and it[1] in ('bt/s', 'bf/s') and k + 1 < n and items[k + 1][0] == 'i':
            # docs/43: a load-use pair left before a bt/s / bf/s: its slot's instruction goes between them (bt / bf: the
            # same work on both ways; taken costs the cycle the pair cost, not taken saves it)
            sl = info(items[k + 1])
            for p in range(k - 2, start - 1, -1):
                a_ = info(items[p]); b_ = info(items[p + 1])
                if a_[3] is not None and a_[3] in b_[0]:
                    if not sl[4] and sl[2] is None and not ({a_[3]} & (sl[0] | sl[1])) and \
                            all(_indep(info(items[q]), sl) for q in range(p + 1, k)):
                        x = items.pop(k + 1); items.insert(p + 1, x); k += 1
                        items[k] = (items[k][0], items[k][1][:2]) + tuple(items[k][2:]); moves += 1
                        it = items[k]
                    break
        if it[0] == 'n': moves += schedule(it[1], K)
        k += 1
        if it[0] == 'b' and it[1] in delayed: k += 1     # (its delay slot stays)
        start = k
    if start < n: run(start, n)
    return moves


def _movable(items, j):
    """items[j] can go into a delay slot: an instruction (not PC-relative, not a nop, not a branch's slot itself)"""
    if j < 0: return None
    it = items[j]
    if it[0] != 'i': return None
    if j >= 1 and items[j - 1][0] == 'b' and items[j - 1][1] in ('bra', 'bt/s', 'bf/s'): return None
    t = it[1]; op = t.split('\t')[0]
    if op in ('nop', 'mova') or 'pc' in t.lower() or '.L' in t: return None
    inf = info(it)
    if inf[4]: return None
    return inf


def fill_slots(items):
    """docs/35: a delayed branch's nop slot takes the instruction before it (or the one before that, where the two can
    change order): bra; jsr / jmp @Rn (not one touching Rn); rts. The same instructions run in the same order"""
    moves = 0; k = 0
    while k < len(items):
        it = items[k]
        if it[0] == 'b' and it[1] == 'bra' and k + 1 < len(items) and items[k + 1] == ('i', 'nop'):
            if _movable(items, k - 1) is not None:
                x = items[k - 1]; items[k - 1] = it; items[k] = x; del items[k + 1]; moves += 1; k -= 1
            elif k >= 2 and items[k - 1][0] in ('i', 'ld') and not info(items[k - 1])[4] and _movable(items, k - 2) is not None \
                    and _indep(info(items[k - 2]), info(items[k - 1])):
                x = items.pop(k - 2); k -= 1; items[k + 1] = x; moves += 1
        elif it[0] == 'd' and it[2] == 'nop':
            op, _, arg = it[1].partition('\t')
            reg = _regs(arg) if op in ('jsr', 'jmp') else set() if op == 'rts' else None
            if reg is not None:
                def ok(j):
                    inf = _movable(items, j)
                    return inf is not None and not ((inf[0] | inf[1]) & reg)
                if ok(k - 1):
                    x = items.pop(k - 1); k -= 1; items[k] = ('d', it[1], x[1]); moves += 1
                elif k >= 2 and items[k - 1][0] in ('i', 'ld') and not info(items[k - 1])[4] and ok(k - 2) \
                        and _indep(info(items[k - 2]), info(items[k - 1])):
                    x = items.pop(k - 2); k -= 1; items[k] = ('d', it[1], x[1]); moves += 1
        k += 1
    return moves


_NOSLOT = re.compile(r'^(bra|bt|bf|bt/s|bf/s|jmp|jsr|rts|rte|bsr|braf|bsrf|trapa|nop|mova|sleep|ldc|ldc\.l|stc|stc\.l)$')


def _size(s):
    """a text line's bytes (None: none, or a label or directive that takes none)"""
    if not s or s.endswith(':') or s.startswith(('/*', '*', '.global', '.text', '.section', '.align', '.balign')): return None
    if s.startswith('.short'): return 2 * len(s.split(None, 1)[1].split(','))
    if s.startswith('.long'): return 4 * len(s.split(None, 1)[1].split(','))
    if s.startswith('.space'): return int(s.split()[1])
    if s.startswith('.'): raise ValueError('a directive the slot pass does not size: %s' % s)
    return 2


def fill_from_target(lines):
    """docs/41: a bra whose slot is still a nop (nothing before it could move there) takes its target's first
    instruction - not a branch, not PC-relative, not one naming a label - and goes on at a label after it. The same
    instructions run in the same order; no size changes (nothing else moves). Returns (lines, slots filled)"""
    L = []
    for l in lines: L.extend(l.split('\n'))
    lab_at = {}; addr = []; a = 0
    for i, l in enumerate(L):
        s = l.strip()
        if s.startswith('.align'):
            n = 1 << int(s.split()[1]); a = (a + n - 1) & ~(n - 1)
        addr.append(a)
        if not l.startswith('\t') and s.endswith(':'): lab_at[s[:-1]] = i
        z = _size(s.split(':', 1)[1].strip() if not l.startswith('\t') and ':' in s and not s.endswith(':') else s)
        if z: a += z
    out = list(L); new = {}; done = 0
    for i in range(len(L) - 1):
        m = re.match(r'^\tbra\t(\S+)$', L[i])
        if not m or L[i + 1] != '\tnop' or m.group(1) not in lab_at: continue
        j = lab_at[m.group(1)] + 1
        while j < len(L) and not L[j].startswith('\t') and L[j].strip().endswith(':'): j += 1
        if j >= len(L) or not L[j].startswith('\t'): continue
        t = L[j].strip(); op = t.split()[0]
        if _NOSLOT.match(op) or op.startswith('.') or 'pc' in t.lower() or re.search(r'\.L|\bL\d+_|\b_', t): continue
        if not -4096 <= addr[j] + 2 - (addr[i] + 4) <= 4094: continue
        lab = new.setdefault(j, '.Ld%d' % (len(new) + 1))
        out[i] = '\tbra\t%s' % lab; out[i + 1] = L[j]; done += 1
    # a long jump (mov.l LIT,r0; jmp @r0; nop) to a label here: the same, LIT moved on to the label after the
    # instruction - where every use of LIT is such a jump, and the instruction doesn't read r0 (the target's address)
    lit = {}; uses = {}
    for i, l in enumerate(L):
        m = re.match(r'^(\.Lk\d+):\t\.long\t(\S+)$', l)
        if m: lit[m.group(1)] = (i, m.group(2))
        m = re.match(r'^\tmov\.l\t(\.Lk\d+),(r\d+)$', l)
        if m: uses.setdefault(m.group(1), []).append((i, m.group(2)))
    for k, (li, tgt) in lit.items():
        us = uses.get(k, [])
        if not us or tgt not in lab_at: continue
        if not all(r == 'r0' and i + 2 < len(L) and L[i + 1] == '\tjmp\t@r0' and out[i + 2] == '\tnop' for i, r in us): continue
        j = lab_at[tgt] + 1
        while j < len(L) and not L[j].startswith('\t') and L[j].strip().endswith(':'): j += 1
        if j >= len(L) or not L[j].startswith('\t'): continue
        t = L[j].strip(); op = t.split()[0]
        if _NOSLOT.match(op) or op.startswith('.') or 'pc' in t.lower() or re.search(r'\.L|\bL\d+_|\b_', t): continue
        if 'r0' in info(('i', t))[0] or re.search(r'\br0\b', t.split('\t', 1)[-1].split(',')[0]) and '@' in t: continue
        lab = new.setdefault(j, '.Ld%d' % (len(new) + 1))
        out[li] = '%s:\t.long\t%s' % (k, lab)
        for i, r in us: out[i + 2] = L[j]; done += 1
    res = []
    for i, l in enumerate(out):
        res.append(l)
        if i in new: res.append('%s:' % new[i])
    return res, done
