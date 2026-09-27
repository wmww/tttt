#!/usr/bin/env python3
"""Why tests that pass their errors component fail the types (or symbols) component, from the last full ./failstats.py
run (its dumps in build/failstats/dump/ against tsc's baselines). Lines are compared as multisets (order is ignored),
paired by their left-hand side, and each mismatching pair is bucketed by a heuristic mechanism.

  ./displaydiff.py [-n N] [-k KIND] [-f REGEX] [--symbols] [--names]
    -n: examples per kind (default 4), -k: only that kind (examples and, with --names, the test names),
    -f: test names, --symbols: the symbols component instead of types
    --fine: split the catch-alls (other, we-any, tsc-any, line-extra) into <shape>/<annotated node> (plan 35), and
    rank the tests by the kind of their first differing line; --shape: the same with the shape only

"Tests fixed by one kind" counts the tests whose every mismatching line has that kind: what fixing it alone would
pass (the displaydiff analogue of failstats' single-code table). JS tests (with a .js/.jsx file or @allowJs) are
counted apart: JS/JSDoc semantics are their own plan.
"""
import argparse, collections, csv, os, random, re
ap = argparse.ArgumentParser()
ap.add_argument('-n', type=int, default=4)
ap.add_argument('-k')
ap.add_argument('-f')
ap.add_argument('--symbols', action='store_true')
ap.add_argument('--names', action='store_true')
ap.add_argument('--fine', action='store_true')
ap.add_argument('--shape', action='store_true')
args = ap.parse_args()
REF = 'thirdparty/tsc/tests/baselines/reference/'
DUMP = 'build/failstats/dump/'
UNDERLINE = re.compile(r'^>\s*:\s*[\^\s]*$')
EXT = '.symbols' if args.symbols else '.types'
COMPONENT = 'symbols' if args.symbols else 'types'

def lines(path, sources=None):
    out, source = [], ''
    for l in open(path, errors='replace').read().split('\n'):
        if not l.startswith('>'): source = l
        elif not UNDERLINE.match(l):
            out.append(l)
            if sources is not None: sources.setdefault(l, source)
    return out

def source_of(name):
    base = name.split('(')[0]
    for root, _, files in os.walk('thirdparty/tsc/tests/cases'):
        for ext in ('.ts', '.tsx'):
            if base + ext in files: return os.path.join(root, base + ext)
    return None

JS = re.compile(r'@(allowjs|checkjs)\s*:\s*true|@filename:\s*\S+\.(js|jsx|cjs|mjs)\b', re.I)
_sources = {}
def is_js(name):
    if name not in _sources:
        path = _sources[name] = source_of(name)
        _sources[name] = bool(path and JS.search(open(path, errors='replace').read()))
    return _sources[name]

from displaykinds import IDENT, type_kinds, fine_kind

SYMBOL = re.compile(r'Symbol\((.*?), (Decl\(.*\))?\)$')
def symbol_kinds(tsc, ours):
    t, o = SYMBOL.search(tsc), SYMBOL.search(ours)
    if not t or not o: return ['other']
    tn, on = t.group(1), o.group(1)
    td = re.findall(r'Decl\(([^)]*)\)', tsc)
    od = re.findall(r'Decl\(([^)]*)\)', ours)
    ks = []
    if tn != on: ks.append('name')
    if td != od:
        if sorted(td) == sorted(od): ks.append('decl-order')
        elif set(od) < set(td): ks.append('decls-missing' + ('-lib' if any('lib.' in d for d in set(td) - set(od)) else ''))
        elif set(td) < set(od): ks.append('decls-extra')
        else: ks.append('decls-differ')
    return ks or ['other']

rows = list(csv.DictReader(open('build/failstats/tests.tsv'), delimiter='\t'))
by_lines = collections.Counter()
lines_of = collections.Counter()
tests_of = collections.defaultdict(set)
single_of = collections.Counter()
single_js = collections.Counter()
single_names = collections.defaultdict(list)
first_of = collections.Counter() # tests by the kind of their first differing line (the likely root cause)
first_names = collections.defaultdict(list)
examples = collections.defaultdict(list)
js_tests = 0
for r in rows:
    name = r['name']
    if r[COMPONENT] == '1' or r['errors'] != '1' or (args.f and not re.fullmatch(args.f, name)): continue
    exp, got = REF + name + EXT, DUMP + name + EXT
    if not (os.path.exists(exp) and os.path.exists(got)): continue
    src = {}
    E, O = collections.Counter(lines(exp, src)), collections.Counter(lines(got, src))
    def refine(ks, lhs, x, y):
        if not (args.fine or args.shape) or args.symbols: return ks
        ks = [fine_kind(k, lhs, src.get(x or y, ''), x.rsplit(split, 1)[-1], y) for k in ks]
        return [k.split('/')[0] if args.shape else k for k in ks]
    missing, extra = list((E - O).elements()), list((O - E).elements())
    n = max(len(missing), len(extra))
    by_lines['underlines/order only' if n == 0 else str(n) if n < 5 else '5-9' if n < 10 else '10+'] += 1
    js = is_js(name)
    js_tests += js
    ours_by_lhs = {}
    split = ' : '
    for l in extra: ours_by_lhs.setdefault(l.rsplit(split, 1)[0], []).append(l)
    test_kinds = set()
    first_kind = None
    for x in missing:
        lhs = x.rsplit(split, 1)[0]
        if ours_by_lhs.get(lhs):
            y = ours_by_lhs[lhs].pop().rsplit(split, 1)[-1]
            ks = symbol_kinds(x, y) if args.symbols else type_kinds(x.rsplit(split, 1)[-1], y)
        else:
            y, ks = '', ['line-missing']
        ks = refine(ks, lhs, x, y)
        for k in ks:
            lines_of[k] += 1; tests_of[k].add(name); examples[k].append((name, x, y))
        test_kinds.add(tuple(ks) if len(ks) > 1 else ks[0])
        if first_kind is None: first_kind = ks[0]
    for rest in ours_by_lhs.values():
        for y in rest:
            k = refine(['line-extra'], y.rsplit(split, 1)[0], '', y)[0]
            lines_of[k] += 1; tests_of[k].add(name); examples[k].append((name, '', y))
            test_kinds.add(k)
            if first_kind is None: first_kind = k
    if n == 0:
        first_kind = 'underlines/order only'
    if first_kind and not js:
        first_of[first_kind] += 1; first_names[first_kind].append(name)
    if n == 0:
        test_kinds = {'underlines/order only'}
    if len(test_kinds) == 1:
        k = next(iter(test_kinds))
        k = '+'.join(k) if isinstance(k, tuple) else k
        (single_js if js else single_of)[k] += 1
        single_names[k].append(name)

print(f'tests (errors pass, {COMPONENT} fail) by number of differing lines ({js_tests} of them JS):')
for k, v in sorted(by_lines.items(), key=lambda t: -t[1]): print(f'  {v:6}  {k}')
print('mismatching lines by kind (a line can have several):')
for k, v in lines_of.most_common(): print(f'  {v:6} lines in {len(tests_of[k]):5} tests  {k}')
print('tests fixed by one kind (TS tests, JS tests):')
for k in sorted(set(single_of) | set(single_js), key=lambda k: -(single_of[k] + single_js[k])):
    print(f'  {single_of[k]:6} {single_js[k]:5}  {k}')
if args.fine or args.shape:
    print('TS tests by the kind of their first differing line (the likely root cause, whatever else differs):')
    for k, v in first_of.most_common(40): print(f'  {v:6}  {k}')
if args.names and args.k:
    print(f'\n{args.k} alone: ' + ' '.join(sorted(single_names[args.k])))
    if args.fine or args.shape: print(f'\n{args.k} first: ' + ' '.join(sorted(first_names[args.k])))
random.seed(1)
for k in [args.k] if args.k else lines_of:
    print(f'\n{k}:')
    for name, x, y in random.sample(examples[k], min(args.n, len(examples[k]))):
        print(f'  {name}\n    tsc: {x[:160]}\n    us : {y[:160]}')
