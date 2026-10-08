#!/usr/bin/env python3
"""Where tttt's syntax errors diverge from tsc's, by mechanism (plan 27). Run scripts/failstats.py first: this reads its dump.

Only *parser* diagnostics count: the codes tsc's parser.ts / scanner.ts report (grammar errors from the checker are
not parse errors, even when they're TS1xxx). A test is listed when tttt's parser diagnostics differ from tsc's.

Divergence kinds (TS tests; JS tests are counted apart, plan 26 covers them):
  A  tsc's parser reports nothing, tttt rejects the file (valid syntax, or tsc parses it and complains later)
  B  tttt's first parse error comes before tsc's first one
  C  same first position, different code
  D  tsc's first error comes before tttt's first one (tttt accepts what tsc rejects, then goes wrong)
  E  the first errors agree, recovery diverges later
  M  tsc reports parse errors, tttt reports none

  scripts/syntaxstats.py                  kinds, then clusters (a rough mechanism guess from the code and source line)
  scripts/syntaxstats.py --list KIND      one line per test of a kind (A..E, M) or cluster name prefix: first spurious and
                                    first missing error, and the source line of the first
  scripts/syntaxstats.py --names CLUSTER  just the test names (for -f @names.txt)
"""
import argparse, collections, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import failstats as fs

TSC = os.path.join(ROOT, 'thirdparty/tsc/src/compiler')

LEX_CODES = {'TS1125', 'TS1002', 'TS1124', 'TS1160', 'TS1198', 'TS1199', 'TS6188', 'TS1161', 'TS1126', 'TS1499',
             'TS1177', 'TS1351', 'TS1487', 'TS1185', 'TS1490', 'TS18026', 'TS1529', 'TS1489', 'TS1508'}

# First match wins. (name, predicate(source line, first spurious code, first missing code))
CLUSTERS = [
    ('lexical:identifiers', lambda l, c, m: '\x00' in l or (re.search(r'\\u', l) and c in ('TS1127', 'TS1005', 'TS1434', 'TS1003'))
        or (re.search(r'[^\x00-\x7f]', l) and c in ('TS1005', 'TS1127', 'TS1128'))),
    ('lexical:literals', lambda l, c, m: c in LEX_CODES or m in LEX_CODES),
    ('permissive:with', lambda l, c, m: re.search(r'\bwith\s*\(', l)),
    ('permissive:heritage', lambda l, c, m: re.search(r'\b(extends|implements)\b', l)
        and re.search(r'^\s*(export\s+)?(declare\s+)?(abstract\s+)?(class|interface)\b', l)),
    ('permissive:index-signature', lambda l, c, m: re.match(r'\s*(public\s+|private\s+|static\s+)?\[', l)
        and c in ('TS1131', 'TS1005', 'TS1003', 'TS1442')),
    ('permissive:modifiers', lambda l, c, m: re.search(r'\b(public|private|protected|declare|export|abstract|accessor)\b', l)
        and c in ('TS1434', 'TS1005', 'TS1128', 'TS1068')),
    ('permissive:yield-await', lambda l, c, m: re.search(r'\b(yield|await)\b', l)),
    ('permissive:private-names', lambda l, c, m: '#' in l),
    ('permissive:decorators', lambda l, c, m: '@' in l),
    ('recovery:list-context', lambda l, c, m: c in ('TS1434', 'TS1068', 'TS1128')
        or m in ('TS1068', 'TS1128', 'TS1434', 'TS1436', 'TS1437')),
    ('recovery:object-literal', lambda l, c, m: re.search(r'=\s*\{|^\s*\w+[;?]|\{\s*\*', l) and c in ('TS1005', 'TS1003')),
    ('recovery:TS1011', lambda l, c, m: m == 'TS1011'),
]


def parser_codes():
    gen = fs.read(f'{TSC}/diagnosticInformationMap.generated.ts')
    name2code = {m[1]: m[2] for m in re.finditer(r'^\s*(\w+): diag\((\d+),', gen, re.M)}
    codes = set()
    for f in ('parser.ts', 'scanner.ts'):
        for m in re.finditer(r'Diagnostics\.(\w+)', fs.read(f'{TSC}/{f}')):
            if m[1] in name2code:
                codes.add('TS' + name2code[m[1]])
    return codes


def source_line(name, file, line):
    text = fs.read(f'{fs.DUMP}/{name}.errors.txt') or ''
    m = re.search(r'^==== ' + re.escape(file) + r' \(\d+ errors\) ====\r?\n', text, re.M)
    if not m:
        return ''
    source = [l[4:] for l in text[m.end():].splitlines() if l.startswith('    ') and not l[4:].lstrip().startswith('~')]
    return source[line - 1] if 0 < line <= len(source) else ''


def classify():
    pcodes = parser_codes()
    names = [f[:-len('.errors.txt')] for f in sorted(os.listdir(fs.DUMP)) if f.endswith('.errors.txt')]
    rows = []
    for name in names:
        ours = [e[:4] for e in fs.parse_errors(fs.read(f'{fs.DUMP}/{name}.errors.txt')) if e[3] in pcodes]
        exp = [e[:4] for e in fs.parse_errors(fs.read(fs.ref(name, '.errors.txt'))) if e[3] in pcodes]
        if collections.Counter(ours) == collections.Counter(exp):
            continue
        spurious = list((collections.Counter(ours) - collections.Counter(exp)).elements())
        missing = list((collections.Counter(exp) - collections.Counter(ours)).elements())
        o, x = sorted(ours), sorted(exp)
        if not x:
            kind = 'A'
        elif not o:
            kind = 'M'
        else:
            i = 0
            while i < min(len(o), len(x)) and o[i] == x[i]:
                i += 1
            a, b = (o[i] if i < len(o) else None), (x[i] if i < len(x) else None)
            if i > 0:
                kind = 'E'
            elif a[:3] == b[:3]:
                kind = 'C'
            elif a[:3] < b[:3]:
                kind = 'B'
            else:
                kind = 'D'
        js = any(f.endswith(('.js', '.jsx', '.cjs', '.mjs')) for f, *_ in ours + exp)
        s, m = (sorted(spurious)[0] if spurious else None), (sorted(missing)[0] if missing else None)
        at = s or m
        line = source_line(name, at[0], at[1]) if at else ''
        cluster = next((n for n, f in CLUSTERS if f(line, s and s[3], m and m[3])), 'other:' + kind)
        rows.append(dict(name=name, kind=kind, js=js, cluster=cluster, spurious=s, missing=m, line=line.strip()))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--list')
    ap.add_argument('--names')
    args = ap.parse_args()
    rows = classify()
    ts = [r for r in rows if not r['js']]
    sel = args.list or args.names
    if sel:
        for r in ts:
            if r['kind'] == sel or r['cluster'].startswith(sel):
                if args.names:
                    print(r['name'])
                else:
                    fmt = lambda e: f'{e[3]}@{e[1]}:{e[2]}' if e else '-'
                    print(f"{r['name'][:48]:48} {r['kind']} ours {fmt(r['spurious']):14} tsc {fmt(r['missing']):14} | {r['line'][:70]}")
        return
    print(f'{len(rows)} tests whose parser diagnostics differ from tsc ({len(rows) - len(ts)} JS, not counted below)')
    kinds = collections.Counter(r['kind'] for r in ts)
    for k in 'ABCDEM':
        print(f'  {k} {kinds[k]:4}  {__doc__.split(chr(10) + "  " + k + "  ")[1].splitlines()[0]}')
    print('clusters:')
    clusters = collections.Counter(r['cluster'] for r in ts)
    for c, n in clusters.most_common():
        eg = ', '.join(r['name'] for r in ts if r['cluster'] == c)[:90]
        print(f'  {n:4}  {c:28} eg {eg}')


if __name__ == '__main__':
    main()
