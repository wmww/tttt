#!/usr/bin/env python3
"""Which test components changed between two saved result files (default: git HEAD's test/baseline_results.ts vs the
working tree's; save the current run first with scripts/fasttest.sh -s).

  scripts/resultdiff.py [old] [new] [--lost|--gained] [--names]   --names prints bare test names (for -f regexes and loops)
"""
import re, subprocess, sys
args = [a for a in sys.argv[1:] if not a.startswith('--')]
flags = {a for a in sys.argv[1:] if a.startswith('--')}
PATH = 'test/baseline_results.ts'
def parse(text):
    found = re.findall(r"'([^']*)': \{\s*errors: (true|false),\s*symbols: (true|false),\s*types: (true|false)", text)
    return {n: (e == 'true', s == 'true', t == 'true') for n, e, s, t in found}
old = parse(open(args[0]).read() if args else subprocess.run(['git', 'show', 'HEAD:' + PATH], capture_output=True, text=True).stdout)
new = parse(open(args[1] if len(args) > 1 else PATH).read())
lost, gained = [], []
for name in sorted(set(old) | set(new)):
    o, n = old.get(name, (False,) * 3), new.get(name, (False,) * 3)
    l = [c for c, a, b in zip(('ERR', 'SYM', 'TYP'), o, n) if a and not b]
    g = [c for c, a, b in zip(('ERR', 'SYM', 'TYP'), o, n) if b and not a]
    if l: lost.append((name, l))
    if g: gained.append((name, g))
for title, rows in (('lost', lost), ('gained', gained)):
    if ('--lost' in flags and title != 'lost') or ('--gained' in flags and title != 'gained'): continue
    if '--names' not in flags: print(f'{title}: {len(rows)} tests, {sum(len(c) for _, c in rows)} components')
    for name, comps in rows:
        print(name if '--names' in flags else f'  {" ".join(comps):12} {name}')
full = lambda r: sum(all(v) for v in r.values())
if '--names' not in flags: print(f'full passes {full(old)} -> {full(new)}')
