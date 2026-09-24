#!/usr/bin/env python3
"""Plan 16 measurements, after a full ./failstats.py run (reads build/failstats).

relocation: failing-errors tests whose diff is explained by tttt reporting an assignability error earlier in the
statement than tsc (tsc's elaboration), ie each spurious one pairs with a missing one at a later position <= 30 lines on.
related: failing tests that expect related info, and those that would pass if the related lines were all that differed.
"""
import collections, os, re, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../..'))
import failstats as fs

ASSIGN = {'TS2322', 'TS2345', 'TS2353', 'TS2741', 'TS2739', 'TS2740', 'TS2559', 'TS2326', 'TS2561', 'TS2769', 'TS2375',
          'TS2412', 'TS2820', 'TS2418', 'TS1360'}
rows = [l.split('\t') for l in open(os.path.join(fs.OUT, 'tests.tsv')).read().splitlines()[1:]]
failing = [r[0] for r in rows if r[1] == '0']
strip = lambda t: re.sub(r'\r?\n!!! related TS\d+ [^\r\n]*', '', t)

explained, partial, related_tests, related_only, expecting = [], 0, collections.Counter(), collections.Counter(), 0
for n in failing:
    ours, exp = fs.read(f'{fs.DUMP}/{n}.errors.txt') or '', fs.read(f'{fs.REF}/{n}.errors.txt') or ''
    for code in set(re.findall(r'related (TS\d+)', exp)): related_tests[code] += 1
    expecting += 'related TS' in exp
    if 'related TS' in exp and strip(exp) == ours:
        related_only[tuple(sorted(set(re.findall(r'related (TS\d+)', exp))))] += 1
    O = collections.Counter(x[:4] for x in fs.parse_errors(ours))
    E = collections.Counter(x[:4] for x in fs.parse_errors(exp))
    miss, spur = list((E - O).elements()), list((O - E).elements())
    miss_a = sorted(m for m in miss if m[3] in ASSIGN)
    other = [x for x in miss + spur if x[3] not in ASSIGN]
    used, ok = set(), True
    for s in sorted(s for s in spur if s[3] in ASSIGN):
        cand = [i for i, m in enumerate(miss_a) if i not in used and m[0] == s[0] and m[1:3] >= s[1:3] and m[1] - s[1] <= 30]
        if cand: used.add(cand[0])
        else: ok = False
    if used and not other and ok: explained.append((n, len(miss_a) - len(used)))
    elif used: partial += 1

print(f'relocation: {len(explained)} tests fully explained ({sum(1 for _, l in explained if not l)} with nothing '
      f'left over), {partial} partly')
print(f'related: {expecting} failing tests expect related info; by code:',
      related_tests.most_common(15))
print(f'related: {sum(related_only.values())} tests pass but for related lines:', related_only.most_common(10))
if '-v' in sys.argv: print('\n'.join(f'{n}\t{l}' for n, l in explained))
