#!/usr/bin/env python3
"""Why tests that pass their errors component fail the types component, from the last full ./failstats.py run
(its dumps in build/failstats/dump/ against tsc's baselines). Lines are compared as multisets (order is ignored),
paired by their left-hand side, and each mismatching pair is bucketed by a heuristic mechanism.

  ./displaydiff.py [-n N] [-k KIND] [-f REGEX]   -n: examples per kind (default 4), -k: only that kind, -f: test names
"""
import argparse, collections, csv, os, random, re
ap = argparse.ArgumentParser()
ap.add_argument('-n', type=int, default=4)
ap.add_argument('-k')
ap.add_argument('-f')
args = ap.parse_args()
REF = 'thirdparty/tsc/tests/baselines/reference/'
DUMP = 'build/failstats/dump/'
UNDERLINE = re.compile(r'^>\s*:\s*[\^\s]*$')

def lines(path):
    return [l for l in open(path, errors='replace').read().split('\n') if l.startswith('>') and not UNDERLINE.match(l)]

def kinds_of(tsc, ours):
    ks = []
    if sorted(re.split(r'[\s|]+', tsc)) == sorted(re.split(r'[\s|]+', ours)): return ['union-order']
    if ours == 'any': return ['we-any']
    if tsc == 'any': return ['tsc-any']
    if 'import("' in ours and 'import("' not in tsc: ks.append('import()-qualification')
    if re.search(r'\w_\d\b', tsc) and not re.search(r'\w_\d\b', ours): ks.append('T_1-rename')
    if re.search(r'\bArray<', tsc) and not re.search(r'\bArray<', ours): ks.append('Array<>-reuse')
    if 'typeof ' in tsc and 'typeof ' not in ours: ks.append('tsc-typeof')
    if 'prototype: ;' in ours: ks.append('class-expression')
    if tsc.replace(' | undefined', '') == ours.replace(' | undefined', ''): ks.append('undefined')
    return ks or ['other']

rows = list(csv.DictReader(open('build/failstats/tests.tsv'), delimiter='\t'))
by_lines = collections.Counter()
lines_of = collections.Counter()
tests_of = collections.defaultdict(set)
examples = collections.defaultdict(list)
for r in rows:
    name = r['name']
    if r['types'] == '1' or r['errors'] != '1' or (args.f and not re.fullmatch(args.f, name)): continue
    exp, got = REF + name + '.types', DUMP + name + '.types'
    if not (os.path.exists(exp) and os.path.exists(got)): continue
    E, O = collections.Counter(lines(exp)), collections.Counter(lines(got))
    missing, extra = list((E - O).elements()), list((O - E).elements())
    n = max(len(missing), len(extra))
    by_lines['underlines/order only' if n == 0 else str(n) if n < 5 else '5-9' if n < 10 else '10+'] += 1
    ours_by_lhs = {}
    for l in extra: ours_by_lhs.setdefault(l.rsplit(' : ', 1)[0], []).append(l)
    for x in missing:
        lhs = x.rsplit(' : ', 1)[0]
        if ours_by_lhs.get(lhs):
            y = ours_by_lhs[lhs].pop().rsplit(' : ', 1)[-1]
            ks = kinds_of(x.rsplit(' : ', 1)[-1], y)
        else:
            y, ks = '', ['line-missing']
        for k in ks:
            lines_of[k] += 1; tests_of[k].add(name); examples[k].append((name, x, y))
    for rest in ours_by_lhs.values():
        for y in rest:
            lines_of['line-extra'] += 1; tests_of['line-extra'].add(name); examples['line-extra'].append((name, '', y))
print('tests (errors pass, types fail) by number of differing lines:')
for k, v in sorted(by_lines.items(), key=lambda t: -t[1]): print(f'  {v:6}  {k}')
print('mismatching lines by kind (a line can have several):')
for k, v in lines_of.most_common(): print(f'  {v:6} lines in {len(tests_of[k]):5} tests  {k}')
random.seed(1)
for k in [args.k] if args.k else lines_of:
    print(f'\n{k}:')
    for name, x, y in random.sample(examples[k], min(args.n, len(examples[k]))):
        print(f'  {name}\n    tsc: {x[:160]}\n    us : {y[:160]}')
