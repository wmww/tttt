#!/usr/bin/env python3
"""Duplicated inlined code that is hot: for each function inlined somewhere (a DWARF abstract origin), how many of its
copies execute hot instructions, how many hot instructions lie outside its biggest copy (roughly what one shared,
out-of-line copy would take off the hot footprint) and the I1 misses in its copies. The candidates for a
[[gnu::noinline]] (agents/notes/code_layout.md "Inlining"); measure each with a full fastbuild.sh and cachegrind.

  PGO_DIR=pgo-g PGO_CFLAGS=-g scripts/pgobuild.sh          # the shipped PGO build with debug info (same code)
  scripts/inlinedup.py --run CORPUS [--bin pgo-g/tttt]     # callgrind --dump-instr --cache-sim at K1 (~2.5 min on
                                                           # drizzle), then the ranking
  scripts/inlinedup.py OUT [--bin pgo-g/tttt] [--hot N] [-n N]   # rank an earlier run

An instruction is hot when it ran at least --hot times (default 10000). Copies inside inlined copies count for both
origins (outlining the outer one takes the inner ones along).
"""
import argparse, bisect, collections, os, re, subprocess, sys

CORPORA = {
    'tsc': 'thirdparty/corpora/TypeScript6/src/compiler',
    'drizzle': 'thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json',
    'zod': 'thirdparty/corpora/zod/packages/zod/tsconfig.test.json',
    'effect': 'thirdparty/corpora/effect/packages/effect/tsconfig.corpus.json',
    'excalidraw': 'thirdparty/corpora/excalidraw/tsconfig.corpus.json',
}


def instr_costs(path):
    ir, i1 = {}, {}
    iir = ii1 = None
    addr, skip = 0, False
    for line in open(path):
        c = line[:1]
        if line.startswith('events:'):
            events = line.split()[1:]
            iir, ii1 = events.index('Ir'), events.index('I1mr')
        elif line.startswith('positions:'):
            if 'instr' not in line.split():
                sys.exit('inlinedup: the callgrind run needs --dump-instr=yes')
        elif line.startswith('calls='):
            skip = True  # (The next cost line is the call's inclusive cost)
        elif c in '0+-*':
            p = line.split()
            a = p[0]
            if a.startswith('0x'): addr = int(a, 16)
            elif a[0] == '+': addr += int(a[1:])
            elif a[0] == '-': addr -= int(a[1:])
            if skip:
                skip = False
                continue
            vals = p[2:]
            ir[addr] = ir.get(addr, 0) + (int(vals[iir]) if len(vals) > iir else 0)
            i1[addr] = i1.get(addr, 0) + (int(vals[ii1]) if len(vals) > ii1 else 0)
    return ir, i1


def inlined_copies(binary):
    """(origin, [(lo, hi)...]) for every DW_TAG_inlined_subroutine"""
    dump = subprocess.run(['llvm-dwarfdump-21', '--debug-info', binary], capture_output=True, text=True, check=True).stdout
    cur, ranges, lowpc, inside = None, [], None, False
    for line in dump.splitlines():
        if 'DW_TAG_' in line:
            if cur and ranges: yield cur, ranges
            cur, ranges, lowpc = None, [], None
            inside = 'DW_TAG_inlined_subroutine' in line
            continue
        if not inside: continue
        m = re.search(r'DW_AT_abstract_origin\t\(0x[0-9a-f]+ "([^"]+)"\)', line)
        if m: cur = m.group(1); continue
        m = re.search(r'DW_AT_low_pc\t\((0x[0-9a-f]+)\)', line)
        if m: lowpc = int(m.group(1), 16); continue
        m = re.search(r'DW_AT_high_pc\t\((0x[0-9a-f]+)\)', line)
        if m and lowpc is not None: ranges.append((lowpc, int(m.group(1), 16))); continue
        m = re.search(r'\[(0x[0-9a-f]+), (0x[0-9a-f]+)\)', line)
        if m: ranges.append((int(m.group(1), 16), int(m.group(2), 16)))
    if cur and ranges: yield cur, ranges


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out', nargs='?')
    ap.add_argument('--run')
    ap.add_argument('--bin', default='pgo-g/tttt')
    ap.add_argument('--hot', type=int, default=10000)
    ap.add_argument('-n', type=int, default=40)
    a = ap.parse_args()
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    out = a.out
    if a.run:
        out = out or f'/tmp/inlinedup-{a.run}.out'
        subprocess.run(['valgrind', '--tool=callgrind', '--dump-instr=yes', '--cache-sim=yes', f'--callgrind-out-file={out}',
                        a.bin, '--checkers', '1', '--threads', '1', '-q', '--keep-going', '--noEmit', '-p', CORPORA[a.run]],
                       stdout=subprocess.DEVNULL, check=False)
        print(f'({out})')
    if not out: ap.error('an output file or --run CORPUS')
    ir, i1 = instr_costs(out)
    addrs = sorted(ir)
    print(f'{sum(1 for x in addrs if ir[x] >= a.hot)} hot instructions of {len(addrs)} executed')
    hot, miss, copies, hot_copies, biggest = (collections.Counter() for _ in range(5))
    for origin, ranges in inlined_copies(a.bin):
        h = m = 0
        for lo, hi in ranges:
            k = bisect.bisect_left(addrs, lo)
            while k < len(addrs) and addrs[k] < hi:
                h += ir[addrs[k]] >= a.hot
                m += i1[addrs[k]]
                k += 1
        copies[origin] += 1
        hot[origin] += h
        miss[origin] += m
        if h:
            hot_copies[origin] += 1
            biggest[origin] = max(biggest[origin], h)
    print('dup-hot: hot instructions outside the biggest copy')
    for f in sorted(hot, key=lambda f: -(hot[f] - biggest[f]))[:a.n]:
        print(f'dup-hot {hot[f] - biggest[f]:6d}  hot {hot[f]:6d} in {hot_copies[f]:4d}/{copies[f]:4d} copies  I1mr {miss[f] / 1e6:5.2f}M  {f}')


main()
