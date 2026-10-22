#!/usr/bin/env python3
"""Variance measurements across checkers (TTTT_VARIANCE_CENSUS=1's `VC` lines, src/ty/relate.c variance_census_line()).

    scripts/variancecensus.py [-k K] [--threads T] [--bin B] [--subject NAME] -p PROJECT

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

def run(binary, project, k, threads):
    env = dict(os.environ, TTTT_VARIANCE_CENSUS='1')
    p = subprocess.run([binary, '--threads', str(threads), '--checkers', str(k), '-q', '--keep-going', '--noEmit', '-p', project],
                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, errors='replace')
    rows = []
    for line in p.stderr.splitlines():
        f = line.split()
        if len(f) != 9 or f[0] != 'VC': continue
        rows.append(dict(chk=int(f[1]), depth=int(f[2]), sym=f[3], name=f[4], val=f[5], clean=f[6] == '1',
                         cuts=int(f[7]), types=int(f[8])))
    return rows

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-p', required=True)
    ap.add_argument('-k', type=int, default=12)
    ap.add_argument('--threads', type=int, default=1)
    ap.add_argument('--bin', default=None)
    ap.add_argument('--subject')
    a = ap.parse_args()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    binary = a.bin
    if not binary:
        subprocess.run(['ninja', '-C', os.path.join(root, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
        binary = os.path.join(root, 'release', 'tttt')
    k1 = run(binary, a.p, 1, 1)
    kk = run(binary, a.p, a.k, a.threads)
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

if __name__ == '__main__':
    main()
