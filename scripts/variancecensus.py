#!/usr/bin/env python3
"""Variance measurements across checkers (TTTT_VARIANCE_CENSUS=1's `VC` lines, src/ty/relate.c variance_census_line()).

    scripts/variancecensus.py [-k K] [--threads T] [--bin B] [--subject NAME] [--flag] -p PROJECT

--flag runs the K side under --shared-graph (TTTT_SHARED_GRAPH=1). With a -DTTTT_TAINT_SITES build as --bin (each
VC line then ends with the sites of the measurement's taint events), the tainted measurements are also split by what
tainted them: the first event that isn't internal to the variance group (the running measurement's own claim, its
group's pending values; under per-frame cleanliness also an enclosing claim, which is internal to the outermost
measurement), by origin (deep cut, an enclosing claim, private reads...) or propagation (a CACHE_DEEP
relation hit, a poisoned variance slot, a tainted memo hit), and by every kind of event they saw.

Runs tttt at --checkers 1 and --checkers K (default 12, --threads 1) and reports measurements against distinct
subjects (the duplication factor of variances alone), the duplicated subjects split into (a) clean in every checker
that measured them, (b) clean in some and tainted in others (taint depends on the checker's history: adopting a
shared clean value there could change a result), (c) tainted everywhere, with value disagreements, and the types made
inside outermost measurements by class (a proxy for their cost; callgrind instructions are in notes/shared_graph.md).
`Clean` is the shared graph's rule: no taint event (graph_taint_now) in the measurement but reads of its own claim.
--subject NAME prints every measurement of that subject (alias subjects are `alias:NAME`).
Line format: VC checker variance_depth symbol:declared_kind(99 alias) name values(hex per param) clean deep_cuts types
"""
import argparse, collections, os, subprocess, sys

# Taint event sites (relate.c, graph.c, overlay.c) by kind; the lines move with the code: by the site's text
SITE_KINDS = (
    ('internal', ('relate.c', 'Separate sites for the audit build'), ('relate.c', 'variances->pending'),
        ('relate.c', 'static void pending_variances_read')),
    ('enclosing-claim', ('relate.c', "an enclosing measurement's claim")),
    ('deep-cut', ('relate.c', 'my deep_cuts++')),
    ('deep-hit', ('relate.c', 'deep_hit_no_taint) TAINT_EVENT'), ('relate.c', 'deep_hit_frames(me, ')),
    ('poisoned-slot', ('relate.c', 'graph_memo_taint_block(me, intern_slot')),
    ('tainted-memo', ('graph.c', 'memo_stat_hits')),
)

def site_map(root):
    """file:line -> kind, by finding each SITE_KINDS text and the next TAINT_EVENT line at or after it"""
    out = {}
    for kind, *specs in SITE_KINDS:
        for fname, text in specs:
            lines = open(os.path.join(root, 'src', 'ty', fname)).read().split('\n')
            for i, l in enumerate(lines):
                if text in l:
                    for j in range(i, min(i + 8, len(lines))):
                        if 'TAINT_EVENT' in lines[j]:
                            out[f'{fname}:{j + 1}'] = kind
                            break
    return out

KEY_NAMES = []
def key_name(n):
    """src/ty/ty_internal.h's KEY_ enum (from KEY_LITERAL = 1, in order)"""
    if not KEY_NAMES:
        import re
        text = open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'src/ty/ty_internal.h')).read()
        body = text[text.index('KEY_LITERAL = 1'):]
        body = body[:body.index('}')]
        KEY_NAMES.extend(['?'] + re.findall(r'^\s*(KEY_\w+)', body, re.M))
    return KEY_NAMES[n] if n < len(KEY_NAMES) else f'kind{n}'

def site_kind(sites, site):
    if site.endswith(':i'): return 'internal' # (internal to the outermost frame, said by the binary)
    if ':k' in site: # (a tainted memo hit's kind)
        site, k = site.split(':k')
        return 'tainted-memo:' + key_name(int(k))
    fl = site.rsplit(':', 1)[0]
    if fl in sites: return sites[fl]
    return fl.split(':')[0].replace('.c', '') + '-other'

def run(binary, project, k, threads, flag=False):
    env = dict(os.environ, TTTT_VARIANCE_CENSUS='1')
    if flag: env['TTTT_SHARED_GRAPH'] = '1'
    else: env.pop('TTTT_SHARED_GRAPH', None)
    p = subprocess.run([binary, '--threads', str(threads), '--checkers', str(k), '-q', '--keep-going', '--noEmit', '-p', project],
                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, errors='replace')
    rows = []
    for line in p.stderr.splitlines():
        f = line.split()
        if len(f) < 8 or f[0] != 'VC': continue
        if len(f) < 9 or not f[8].isdigit(): f.insert(4, '(anonymous)') # (a subject without a name)
        rows.append(dict(chk=int(f[1]), depth=int(f[2]), sym=f[3], name=f[4], val=f[5], clean=f[6] == '1',
                         cuts=int(f[7]), types=int(f[8]), sites=[x for x in f[9:] if not x.startswith('@')]))
    return rows

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-p', required=True)
    ap.add_argument('-k', type=int, default=12)
    ap.add_argument('--threads', type=int, default=1)
    ap.add_argument('--bin', default=None)
    ap.add_argument('--subject')
    ap.add_argument('--flag', action='store_true', help='the K run under --shared-graph')
    a = ap.parse_args()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    binary = a.bin
    if not binary:
        subprocess.run(['ninja', '-C', os.path.join(root, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
        binary = os.path.join(root, 'release', 'tttt')
    k1 = run(binary, a.p, 1, 1)
    kk = run(binary, a.p, a.k, a.threads, a.flag)
    if a.subject:
        for label, rows in (('K1', k1), (f'K{a.k}', kk)):
            for r in rows:
                if r['name'] == a.subject:
                    print(f"{label} checker {r['chk']} depth {r['depth']} {r['sym']} values {r['val']} clean {int(r['clean'])} cuts {r['cuts']} types {r['types']}")
        return
    by = collections.defaultdict(list)
    for r in kk: by[r['sym']].append(r)
    print(f"K1: {len(k1)} measurements ({sum(r['clean'] for r in k1)} clean); K{a.k}: {len(kk)} over {len(by)} subjects "
          f"(x{len(kk) / max(1, len(by)):.2f}), {sum(not r['clean'] for r in kk)} tainted")
    cls, meas, dup, diff = (collections.Counter() for _ in range(4))
    clean_disagree = []
    for sym, rs in by.items():
        c = [r['clean'] for r in rs]
        k = 'single' if len(rs) == 1 else 'a' if all(c) else 'b' if any(c) else 'c'
        cls[k] += 1; meas[k] += len(rs); dup[k] += len(rs) - 1
        if len({r['val'] for r in rs}) > 1: diff[k] += 1
        if len({r['val'] for r in rs if r['clean']}) > 1: clean_disagree.append(rs[0]['name'])
    names = {'single': 'measured once', 'a': '(a) clean everywhere', 'b': '(b) clean and tainted', 'c': '(c) tainted everywhere'}
    for k in ('single', 'a', 'b', 'c'):
        print(f"  {names[k]:24} subjects {cls[k]:5}  measurements {meas[k]:6}  duplicates {dup[k]:6}  with differing values {diff[k]}")
    print(f"  clean measurements disagreeing on values: {len(clean_disagree)} {' '.join(clean_disagree[:10])}")
    outer = [r for r in kk if r['depth'] == 0]
    tot = max(1, sum(r['types'] for r in outer))
    seen, dupc, tainted = set(), 0, 0
    for r in outer:
        if not r['clean']: tainted += r['types']
        elif r['sym'] in seen: dupc += r['types']
        else: seen.add(r['sym'])
    print(f"  types made in outermost measurements: K1 {sum(r['types'] for r in k1 if r['depth'] == 0)}, K{a.k} {tot}: "
          f"clean duplicates {100 * dupc / tot:.1f}%, tainted {100 * tainted / tot:.1f}%")
    if any(r['sites'] for r in kk): reasons(root, kk, a.k)

def reasons(root, rows, k):
    sites = site_map(root)
    first, every, firstt = collections.Counter(), collections.Counter(), collections.Counter()
    for r in rows:
        if r['clean'] or r['depth']: continue
        kinds = [site_kind(sites, x) for x in r['sites']]
        ext = [x for x in kinds if x != 'internal']
        f = ext[0] if ext else 'none-seen'
        first[f] += 1; firstt[f] += r['types']
        for x in set(ext): every[x] += 1
    n = max(1, sum(first.values())); nt = max(1, sum(firstt.values()))
    print(f"  tainted outermost measurements at K{k}: {sum(first.values())}, by their first non-internal event (share of them, of their types) and by any event:")
    for x, c in first.most_common():
        print(f"    {x:18} first {c:5} ({100 * c / n:4.1f}%, types {100 * firstt[x] / nt:4.1f}%)  any {every[x]}")
    for x, c in every.items():
        if x not in first: print(f"    {x:18} first     0                       any {c}")

if __name__ == '__main__':
    main()
