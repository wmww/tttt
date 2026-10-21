#!/usr/bin/env python3
"""Ranks bench/sampler/sampler.c's samples: self (leaf) and inclusive (anywhere on the stack) per function.
report.py SAMPLER_OUT [--top N] [--callers FUNC] [--under FUNC]"""
import argparse, bisect, collections, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('--top', type=int, default=40)
ap.add_argument('--under', help='only samples with FUNC on the stack')
ap.add_argument('--exclude', action='append', default=[], help='drop samples with FUNC on the stack')
ap.add_argument('--callers', help='what calls FUNC (the frame above its first occurrence)')
args = ap.parse_args()

maps, samples = [], []
for line in open(args.out):
    if line.startswith('M '):
        parts = line[2:].split()
        if len(parts) >= 6:
            lo, hi = (int(x, 16) for x in parts[0].split('-'))
            maps.append((lo, hi, int(parts[2], 16), parts[5]))
    elif line.startswith('S'):
        samples.append([int(x, 16) for x in line[1:].split()])

bases = {}
for lo, hi, off, path in maps:
    if off == 0 and path not in bases: bases[path] = lo
symtabs = {}
def symtab(path):
    if path not in symtabs:
        syms = []
        try:
            out = subprocess.run(['nm', '-n', '--defined-only', path], capture_output=True, text=True).stdout
            if not out.strip(): out = subprocess.run(['nm', '-n', '-D', '--defined-only', path], capture_output=True, text=True).stdout
            for l in out.splitlines():
                p = l.split()
                if len(p) >= 3 and p[1] in 'tTwW': syms.append((int(p[0], 16), p[2]))
        except Exception: pass
        symtabs[path] = ([a for a, _ in syms], [n for _, n in syms])
    return symtabs[path]
cache = {}
def name(addr):
    if addr in cache: return cache[addr]
    ret = '?'
    for lo, hi, off, path in maps:
        if lo <= addr < hi:
            addrs, names = symtab(path)
            v = addr - bases.get(path, lo)
            i = bisect.bisect_right(addrs, v) - 1
            ret = names[i] if i >= 0 else path.split('/')[-1]
            if path.endswith('.so') or '.so.' in path: ret = ret + '@' + path.split('/')[-1].split('.')[0]
            break
    cache[addr] = ret
    return ret

stacks = []
for s in samples:
    # (A return address points past the call: -1 for the caller frames)
    stacks.append([name(a if i == 0 else a - 1) for i, a in enumerate(s)])
if args.under: stacks = [s for s in stacks if args.under in s]
for x in args.exclude: stacks = [s for s in stacks if x not in s]
total = len(stacks)
print(f'{total} samples')
if args.callers:
    c = collections.Counter()
    for s in stacks:
        if args.callers in s:
            i = s.index(args.callers)
            c[s[i + 1] if i + 1 < len(s) else '<root>'] += 1
    for f, n in c.most_common(args.top): print(f'{n:7d} {100*n/total:5.1f}%  {f}')
    sys.exit()
self_c, incl = collections.Counter(), collections.Counter()
for s in stacks:
    if s: self_c[s[0]] += 1
    for f in set(s): incl[f] += 1
print('self:')
for f, n in self_c.most_common(args.top): print(f'{n:7d} {100*n/total:5.1f}%  {f}')
print('inclusive:')
for f, n in incl.most_common(args.top): print(f'{n:7d} {100*n/total:5.1f}%  {f}')
