#!/usr/bin/env python3
"""Ranks where tttt's single-threaded time goes in the memory system (plan 86 stage 2), from cachegrind or callgrind.

  scripts/cgrank.py --run CORPUS [--bin B] [-o OUT]    cachegrind --cache-sim=yes on a corpus at K1 (instrcount.sh's
                                                       names; default binary fastbuild.sh --measured, no line info: --bin a -g build
                                                       for --lines), then ranks it
  scripts/cgrank.py OUT [-n N]                         functions by the stall estimate
  scripts/cgrank.py OUT --lines [-f FUNC]              source lines (needs a -g build), FUNC: a substring of the function
  scripts/cgrank.py OUT --callers FUNC [-e EVENT]      a callgrind output: callers of FUNC by EVENT (default DLmw), e.g.
                                                       who memsets fresh memory (memset's PLT stub is ???:0x... in an LTO
                                                       build: pass the stub's name)

The stall estimate is a model, not a measurement (perf counters may be unavailable, perf_event_paranoid): cycles =
D1mr*12 + DLmr*150 + I1mr*10 + DLmw*20. Cachegrind models neither prefetching nor the kernel: sequential sweeps
(table growth, memset) and first writes to fresh pages count as misses that real hardware mostly hides (the page
fault is the real cost, in sys time). Rank by it, confirm a lever with instructions and wall
"""
import argparse, collections, os, re, subprocess, sys

W = {'D1mr': 12, 'DLmr': 150, 'I1mr': 10, 'DLmw': 20}
CORPORA = {
    'tsc': 'thirdparty/corpora/TypeScript6/src/compiler',
    'drizzle': 'thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json',
    'zod': 'thirdparty/corpora/zod/packages/zod/tsconfig.test.json',
    'effect': 'thirdparty/corpora/effect/packages/effect/tsconfig.corpus.json',
    'webpack': 'thirdparty/corpora/webpack',
    'excalidraw': 'thirdparty/corpora/excalidraw/tsconfig.corpus.json',
    'type-fest': 'thirdparty/corpora/type-fest',
    'vue': 'thirdparty/corpora/vue/tsconfig.corpus.json',
    'vscode': 'thirdparty/corpora/vscode/src/tsconfig.corpus.json',
}


def cost(c):
    return sum(c[e] * w for e, w in W.items())


def parse(path, by_line=False, only=None):
    events, fl, fn = None, None, None
    agg = collections.defaultdict(collections.Counter)
    names = {}
    for line in open(path):
        if line.startswith('events:'):
            events = line.split()[1:]
        elif line.startswith(('fl=', 'fi=', 'fe=')):
            fl = intern_name(names, 'fl', line[3:].strip())
        elif line.startswith('fn='):
            fn = intern_name(names, 'fn', line[3:].strip())
        elif line[:1].isdigit():
            if only and only not in fn:
                continue
            p = line.split()
            key = (os.path.basename(fl) + ':' + p[0], fn) if by_line else fn
            for e, v in zip(events, p[1:]):
                agg[key][e] += int(v)
    return agg


def intern_name(names, kind, s):
    # callgrind compresses names: "(12) name" defines, "(12)" refers
    m = re.match(r'\((\d+)\)(?: (.*))?$', s)
    if not m:
        return s
    if m.group(2):
        names[(kind, m.group(1))] = m.group(2)
    return names.get((kind, m.group(1)), s)


def rank(path, n, by_line, only):
    if open(path).read(4096).find('creator: callgrind') >= 0:
        sys.exit('a callgrind output: rank a cachegrind one (--run), --callers reads callgrind ones')
    agg = parse(path, by_line, only)
    tot = collections.Counter()
    for c in agg.values():
        tot.update(c)
    T = cost(tot) or 1
    print(f"Ir {tot['Ir'] / 1e9:.3f} G, stall estimate {T / 1e9:.2f} G cycles: " +
          ", ".join(f"{e} {tot[e] * w / 1e9:.2f} G" for e, w in W.items()) +
          f"; D1 read miss rate {100 * tot['D1mr'] / max(tot['Dr'], 1):.2f}%, I1 {100 * tot['I1mr'] / max(tot['Ir'], 1):.2f}%")
    for k, c in sorted(agg.items(), key=lambda kv: -cost(kv[1]))[:n]:
        name = f"{k[0]} {k[1]}" if by_line else k
        print(f"{cost(c) / 1e6:8.1f}M {100 * cost(c) / T:5.1f}%  Ir {c['Ir'] / 1e6:7.1f}M  D1mr {c['D1mr'] / 1e3:6.0f}k "
              f"DLmr {c['DLmr'] / 1e3:5.0f}k I1mr {c['I1mr'] / 1e3:6.0f}k DLmw {c['DLmw'] / 1e3:5.0f}k  {name}")


def callers(path, pat, ev, n):
    events, fn, cfn, incall = None, None, None, False
    names = {}
    agg = collections.Counter()
    for line in open(path):
        if line.startswith('events:'):
            events = line.split()[1:]
        elif line.startswith('fn='):
            fn = intern_name(names, 'fn', line[3:].strip())
        elif line.startswith('cfn='):
            cfn = intern_name(names, 'fn', line[4:].strip())
        elif line.startswith('calls='):
            incall = True
        elif incall and (line[:1].isdigit() or line[:1] in '+-*'):
            incall = False
            p = line.split()
            i = 1 + events.index(ev)
            if pat in cfn and len(p) > i:
                agg[fn] += int(p[i])
    tot = sum(agg.values()) or 1
    for f, v in agg.most_common(n):
        print(f"{v:14,} {100 * v / tot:5.1f}%  {f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', nargs='?')
    ap.add_argument('--run')
    ap.add_argument('--bin', default=None)
    ap.add_argument('-o')
    ap.add_argument('-n', type=int, default=30)
    ap.add_argument('--lines', action='store_true')
    ap.add_argument('-f')
    ap.add_argument('--callers')
    ap.add_argument('-e', default='DLmw')
    a = ap.parse_args()
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    if a.run:
        binary = a.bin or subprocess.run(['scripts/fastbuild.sh', '--measured'], capture_output=True, text=True, check=True).stdout.strip()
        out = a.o or f'/tmp/cgrank.{a.run}.out'
        subprocess.run(['valgrind', '--tool=cachegrind', '--cache-sim=yes', f'--cachegrind-out-file={out}', binary,
                        '--checkers', '1', '--threads', '1', '-q', '--keep-going', '--noEmit', '-p',
                        os.path.realpath(CORPORA[a.run])], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f'({out})')
        a.out = out
    if not a.out:
        ap.error('an output file or --run')
    if a.callers:
        callers(a.out, a.callers, a.e, a.n)
    else:
        rank(a.out, a.n, a.lines, a.f)


if __name__ == '__main__':
    main()
