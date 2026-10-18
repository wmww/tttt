#!/usr/bin/env python3
"""Why tests with the right error codes at the right positions still fail their errors component (the "same
codes+positions, message text differs" shape of scripts/failstats.py, plan 34), from the last full scripts/failstats.py run.

  scripts/errtextdiff.py [-n N] [-k KIND] [-f REGEX] [--names] [--coarse]
    -n: examples per kind (default 4), -k: only that kind (a prefix matches: -k chain), -f: test names,
    --names: with -k, the tests that kind alone fails, --coarse: bucket by the part only (header/chain/related/...)

Each diagnostic of the .errors.txt body (a `!!! error` header, its indented elaboration chain lines, its `!!! related`
lines, the squiggles above it) is paired in order with tsc's, and the first part that differs names the kind:
  header:<code> <mechanism>  the message itself; the mechanism compares the first differing quoted piece (type
                             printing: import(), union order, alias lost...) or `wording` when the text outside the
                             quotes differs
  chain:<what>               the elaboration lines: `missing` (tsc elaborates, tttt doesn't), `extra`, `shallower`/
                             `deeper` (tttt's is a prefix of tsc's or the reverse), each with the first unpaired line, or `<tsc line> / <our line>` as templates
                             (quoted parts replaced by _) for the first differing line
  related:<what>             missing/extra/position/text of the related information
  underline:<code> <how>     the squiggles: tsc's are `shorter`/`longer` than ours or `moved`
  order                      the same diagnostics in another order
"Tests fixed by one kind" counts the tests whose every differing diagnostic has that kind.
"""
import argparse, collections, csv, os, random, re
from displaykinds import type_kinds

DUMP = 'build/failstats/dump/'
SQUIGGLE = re.compile(r'^\s*~+[\s~]*$')
ERROR = re.compile(r'^!!! error (TS\d+): (.*)$')
RELATED = re.compile(r'^!!! related (TS\d+) (\S+): (.*)$')


def diagnostics(path):
    """The body's diagnostics in order, as dicts: code, header, chain, related, squiggles"""
    out, squiggles, body = [], [], False
    for line in open(path, encoding='utf-8', errors='replace').read().split('\n'):
        line = line.rstrip('\r')
        if line.startswith('==== '): body = True; continue
        if not body: continue
        m = ERROR.match(line)
        if m:
            if m[2].startswith('  ') and out and out[-1]['code'] == m[1] and not out[-1]['related']:
                out[-1]['chain'].append(m[2])
            else:
                out.append({'code': m[1], 'header': m[2], 'chain': [], 'related': [], 'squiggles': squiggles})
                squiggles = []
            continue
        m = RELATED.match(line)
        if m and out:
            out[-1]['related'].append((m[1], m[2], m[3])); continue
        if SQUIGGLE.match(line[4:] if line.startswith('    ') else line) and '~' in line:
            squiggles.append(line)
    return out


QUOTED = re.compile(r"'((?:[^']|'(?=[^ ,.:;)]))*)'")
def template(msg):
    return QUOTED.sub('_', msg.strip())


def header_kind(tsc, ours):
    if template(tsc) != template(ours):
        t, o = template(tsc), template(ours)
        if o.startswith(t) or t.startswith(o): return 'suffix'
        return 'wording'
    for t, o in zip(QUOTED.findall(tsc), QUOTED.findall(ours)):
        if t == o: continue
        ks = []
        if re.fullmatch(r'"\d+"', t) and re.fullmatch(r'\d+', o) or '"0"' in t and '"0"' not in o: ks.append('quoted-number')
        if 'undefined[]' in o and 'any[]' in t: ks.append('undefined[]-vs-any[]')
        if 'typeof import(' in t and 'typeof import(' not in o: ks.append('typeof-import()')
        if '.' in t and '.' not in o and t.endswith('.' + o): ks.append('qualified-name')
        return '+'.join(ks or type_kinds(t, o))
    return 'other'


def chain_kind(t, o):
    if not t: return 'extra: ' + template(o[0])[:70]
    if not o: return 'missing: ' + template(t[0])[:70]
    if o == t[:len(o)]: return 'shallower: ' + template(t[len(o)])[:70]
    if t == o[:len(t)]: return 'deeper: ' + template(o[len(t)])[:70]
    i = next(i for i, (a, b) in enumerate(zip(t, o)) if a != b)
    ta, oa = template(t[i]), template(o[i])
    if ta == oa: return 'text: ' + ta[:70]
    return f'{ta[:70]} / {oa[:70]}'


def related_kind(t, o):
    if len(t) != len(o):
        c = collections.Counter(t if len(t) > len(o) else o) - collections.Counter(o if len(t) > len(o) else t)
        first = next(iter(c.elements()), None) or (t if len(t) > len(o) else o)[0]
        return ('missing ' if len(t) > len(o) else 'extra ') + first[0] + ' ' + template(first[2])[:60]
    for a, b in zip(t, o):
        if a == b: continue
        if a[0] != b[0]: return 'code'
        if a[1] != b[1]: return 'position ' + a[0]
        return 'text'
    return 'order'


def diag_kind(t, o):
    if t['header'] != o['header']: return f'header:{t["code"]} ' + header_kind(t['header'], o['header'])
    if t['chain'] != o['chain']: return 'chain:' + chain_kind(t['chain'], o['chain'])
    if t['related'] != o['related']: return 'related:' + related_kind(t['related'], o['related'])
    if t['squiggles'] != o['squiggles']:
        tl, ol = sum(l.count('~') for l in t['squiggles']), sum(l.count('~') for l in o['squiggles'])
        return f'underline:{t["code"]} ' + ('shorter' if tl < ol else 'longer' if tl > ol else 'moved')
    return None


def show_diag(d):
    s = d['code'] + ': ' + d['header'][:150]
    for c in d['chain'][:4]: s += '\n        ' + c[:150]
    for rel in d['related'][:2]: s += '\n        related ' + ' '.join(rel)[:150]
    for q in d['squiggles'][:3]: s += '\n        ' + q[:150]
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-n', type=int, default=4)
    ap.add_argument('-k')
    ap.add_argument('-f')
    ap.add_argument('--names', action='store_true')
    ap.add_argument('--coarse', action='store_true')
    args = ap.parse_args()
    from suite import ref
    coarse = lambda k: k.split(':')[0] if args.coarse else k
    show = show_diag
    rows = list(csv.DictReader(open('build/failstats/tests.tsv'), delimiter='\t'))
    diags_of, tests_of = collections.Counter(), collections.defaultdict(set)
    single, single_names, examples = collections.Counter(), collections.defaultdict(list), collections.defaultdict(list)
    total = 0
    for r in rows:
        name = r['name']
        if r['errors'] == '1' or r['missing'] or r['spurious'] or (args.f and not re.fullmatch(args.f, name)): continue
        exp, got = ref(name, '.errors.txt'), DUMP + name + '.errors.txt'
        if not (os.path.exists(exp) and os.path.exists(got)): continue
        total += 1
        T, O = diagnostics(exp), diagnostics(got)
        kinds = set()
        pairs = [(t, o, diag_kind(t, o)) for t, o in zip(T, O)]
        if len(T) != len(O):
            kinds.add('count')
        elif any(k for _, _, k in pairs) and sorted(map(repr, T)) == sorted(map(repr, O)):
            kinds.add('order')
        else:
            for t, o, k in pairs:
                if not k: continue
                k = coarse(k)
                kinds.add(k); diags_of[k] += 1; tests_of[k].add(name)
                examples[k].append((name, t, o))
        if not kinds: kinds.add('outside diagnostics')
        if len(kinds) == 1:
            k = next(iter(kinds)); single[k] += 1; single_names[k].append(name)

    selected = lambda k: not args.k or k.startswith(args.k)
    print(f'{total} tests with the same codes at the same positions and a different errors.txt')
    print('differing diagnostics by kind (diagnostics, tests):')
    for k, v in diags_of.most_common():
        if selected(k): print(f'  {v:5} {len(tests_of[k]):5}  {k}')
    print('tests fixed by one kind:')
    for k, v in single.most_common():
        if selected(k): print(f'  {v:5}  {k}')
    if args.names and args.k:
        print('\n' + ' '.join(sorted(n for k in single_names if selected(k) for n in single_names[k])))
        raise SystemExit
    random.seed(1)
    for k in sorted(examples, key=lambda k: -diags_of[k]):
        if not args.n or not selected(k) or (not args.k and diags_of[k] < 3): continue
        print(f'\n{k}:')
        for name, t, o in random.sample(examples[k], min(args.n, len(examples[k]))):
            print(f'  {name}\n    tsc: {show(t)}\n    us : {show(o)}')


if __name__ == '__main__':
    main()
