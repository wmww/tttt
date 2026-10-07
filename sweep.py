#!/usr/bin/env python3
"""The breadth sweep (plan 44): tttt on every DefinitelyTyped package (thirdparty/ensure-corpora.sh pins the checkout),
each `-p types/NAME` with its own tsconfig, compared with what tsc reports for it. tsc's verdicts are cached in
test/sweep/DefinitelyTyped.tsc.jsonl (regenerate with --tsc, ~40 minutes on 4 cores, after changing the pin); they're
compared by (file, line, code), like scenarios.py. A crash, hang or memory runaway is listed first, then the
diagnostics tttt gets wrong ranked by root cause *across packages*: code and message shape (quoted parts elided), and
for a diagnostic in a shared dependency (types/node/..., node_modules/...) that dependency, each with the number of
packages it appears in and how many packages it is the only problem of. A missing check under a `@ts-expect-error`
shows up as a spurious TS2578.

  ./sweep.py                  # every package (~5 minutes on 4 cores)
  ./sweep.py -f '^react'      # the packages whose name matches
  ./sweep.py -k 'TS2339'      # list the packages and diagnostics whose root-cause key contains this
  ./sweep.py --names spurious # the package names of a verdict (crash, timeout, spurious, missing, both, match)
  ./sweep.py --last ...       # reuse the last run's results (-k, --names) without running tttt again
  ./sweep.py --flags nuia     # every package rechecked with a flag on top of its tsconfig (plan 46): own oracle file
                              # (test/sweep/DefinitelyTyped.nuia.tsc.jsonl.gz, --tsc --flags nuia makes it)

A package that wants an older major of a dependency gets it (test/sweep_links.py writes pnpm's links), and the
versioned directories (types/NAME/vN) are packages of their own.
"""
import argparse, collections, concurrent.futures, gzip, json, os, re, resource, subprocess, sys, time

ROOT = os.path.dirname(os.path.abspath(__file__))
DT = os.path.join(ROOT, 'thirdparty/corpora/DefinitelyTyped')
ORACLE = os.path.join(ROOT, 'test/sweep/DefinitelyTyped.tsc.jsonl')
TREE = os.path.basename(ROOT)
LAST = f'/tmp/sweep-{TREE}.json'
# Flag sets for --flags (DT packages are strict already: only the flags no package sets are worth a run)
FLAGS = {
    'nuia': ['--noUncheckedIndexedAccess'],
    'eopt': ['--exactOptionalPropertyTypes'],
}
DIAG = re.compile(r'(\S+):(\d+) \[TS(\d+)\] (.*)')


def list_packages(filter=None):
    """Every package, and every versioned directory of one (types/NAME/vN), which is a package of its own"""
    packages = []
    for n in os.listdir(os.path.join(DT, 'types')):
        base = os.path.join(DT, 'types', n)
        if os.path.exists(os.path.join(base, 'tsconfig.json')):
            packages.append('types/' + n)
        if os.path.isdir(base):
            packages += [f'types/{n}/{v}' for v in os.listdir(base)
                         if re.fullmatch(r'v\d+(\.\d+)?', v) and os.path.exists(os.path.join(base, v, 'tsconfig.json'))]
    packages.sort()
    # The ~460 generated Google API wrappers (types/gapi.client.*) are one shape that needs npm-only typings: no signal
    packages = [p for p in packages if not p.startswith('types/gapi.client.')]
    if filter:
        packages = [p for p in packages if re.search(filter, p[len('types/'):])]
    return packages


def run_tsc(packages, jobs, extra=()):
    """tsc on the packages, `jobs` node processes each checking a share in one process (test/sweep_tsc.js)"""
    procs = []
    for i in range(jobs):
        share = packages[i::jobs]
        p = subprocess.Popen(['node', '--max-old-space-size=4096', os.path.join(ROOT, 'test/sweep_tsc.js'), *extra],
                             cwd=DT, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        p.stdin.write('\n'.join(share) + '\n')
        p.stdin.close()
        procs.append(p)
    results = {}
    for p in procs:
        for line in p.stdout:
            r = json.loads(line)
            results[r['project']] = r
        p.wait()
    return results


def run_tttt(package, timeout, mem_gb, flags=()):
    limit = mem_gb << 30
    start = time.time()
    try:
        proc = subprocess.run([os.path.join(ROOT, 'release/tttt'), '-q', '--keep-going', '--all-errors', '--threads', '1',
                               '--checkers', '1', '-p', package, *flags], cwd=DT, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=timeout, env={**os.environ, 'TTTT_NO_REEXEC': '1'},
                              preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
        out, code = proc.stdout, proc.returncode
        status = 'ok' if code in (0, 1) else f'exit {code}' if code > 0 else f'signal {-code}'
    except subprocess.TimeoutExpired as e:
        out, status = e.stdout or b'', 'timeout'
    wall = time.time() - start
    text = re.sub(r'\x1b\[[0-9;]*m', '', out.decode(errors='replace'))
    diags = []
    for line in text.split('\n'):
        m = DIAG.match(line)
        if m:
            diags.append([m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)])
        elif status == 'ok' and ('panic' in line.lower() or re.search(r'\.c:\d+: error:', line)):
            status = 'log_error'
    tail = '\n'.join(l for l in text.split('\n') if l.strip())[-600:] if status != 'ok' else ''
    return {'package': package, 'status': status, 'wall': wall, 'diags': diags, 'tail': tail}


def shape(message):
    """A message with its quoted parts elided: the same mistake on different names groups together"""
    return re.sub(r"'[^']*'", "'…'", re.sub(r'"[^"]*"', '"…"', message))[:120]


def lib_key(f, l):
    """A default lib's diagnostic: tsc's lib.X.d.ts and tttt's own copy (thirdparty/lib/X.d.ts) number lines
    differently, so only the lib's name counts"""
    m = re.search(r'(?:/tsc/lib/lib\.|/tsc/built/local/lib\.|/thirdparty/lib/)([^/]+)\.d\.ts$', f)
    # (Relative to DefinitelyTyped in the main tree, where thirdparty/corpora isn't a symlink: ../../lib/dom.d.ts)
    if not m and f.startswith('../') and os.path.realpath(os.path.join(DT, f)).startswith(os.path.join(ROOT, 'thirdparty/lib/')):
        m = re.search(r'([^/]+)\.d\.ts$', f)
    return (f'lib.{m.group(1)}.d.ts', 0) if m else (f, l)


def compare(package, tsc, tttt):
    """The diagnostics tttt reports that tsc doesn't (spurious) and the other way round (missing), by (file, line,
    code) multiset"""
    want = collections.Counter((*lib_key(f, l), c) for f, l, _, c in tsc['diags'])
    got = collections.Counter((*lib_key(f, l), c) for f, l, c, _ in tttt['diags'])
    spurious, missing = [], []
    remaining = want.copy()
    for f, l, c, msg in tttt['diags']:
        f, l = lib_key(f, l)
        if remaining[(f, l, c)] > 0:
            remaining[(f, l, c)] -= 1
        else:
            spurious.append((f, l, c, msg))
    for (f, l, c), n in (want - got).items():
        missing += [(f, l, c)] * n
    return spurious, missing


def dependency(path):
    """The package a file outside the checked one belongs to: types/NAME, node_modules/[@scope/]NAME, or the file"""
    parts = path.split('/')
    if parts[0] == 'types' and len(parts) > 2:
        versioned = len(parts) > 3 and re.fullmatch(r'v\d+(\.\d+)?', parts[2])
        return '/'.join(parts[:3 if versioned else 2])
    if 'node_modules' in parts:
        i = parts.index('node_modules')
        return '/'.join(parts[:i + (3 if parts[i + 1:i + 2] and parts[i + 1].startswith('@') else 2)])
    return path


def root_cause(package, diag, spurious):
    """The key a mismatching diagnostic is ranked under: its code and message shape (quoted parts elided). Outside the
    package's own directory (a dependency, a lib) also the dependency: the same declaration of types/node wrong in
    2000 packages is one bug"""
    f, l, c = diag[:3]
    where = '' if f.startswith(package + '/') and dependency(f) == package else f' in {dependency(f)}'
    return f'+TS{c}{where} {shape(diag[3])}' if spurious else f'-TS{c}{where}'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-f', '--filter', help='regex the package name (types/NAME) must match (re.search)')
    ap.add_argument('-j', '--jobs', type=int, default=os.cpu_count())
    ap.add_argument('-n', type=int, default=30, help='rows per table')
    ap.add_argument('-k', '--key', help='list the diagnostics whose root-cause key contains this')
    ap.add_argument('--names', help='list the packages with this verdict')
    ap.add_argument('--last', action='store_true', help="reuse the last run's results")
    ap.add_argument('--tsc', action='store_true', help="regenerate tsc's cached verdicts (all packages, or -f's)")
    ap.add_argument('--timeout', type=int, default=60)
    ap.add_argument('--mem-gb', type=int, default=4)
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--flags', choices=sorted(FLAGS), help='recheck with a flag set, against its own tsc oracle')
    args = ap.parse_args()
    global ORACLE, LAST
    flags = FLAGS[args.flags] if args.flags else []
    if args.flags:
        ORACLE = ORACLE.replace('.tsc.jsonl', f'.{args.flags}.tsc.jsonl.gz')
        LAST = LAST.replace('.json', f'-{args.flags}.json')
    opener = gzip.open if ORACLE.endswith('.gz') else open

    subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-corpora.sh')], check=True)
    # Older majors of dependencies where a package asks for them, as pnpm would link them (idempotent: writes nothing
    # once they're there)
    subprocess.run([sys.executable, os.path.join(ROOT, 'test/sweep_links.py'), DT], check=True)
    packages = list_packages(args.filter)

    if args.tsc:
        oracle = {}
        if os.path.exists(ORACLE):
            oracle = {r['project']: r for r in map(json.loads, opener(ORACLE, 'rt'))}
        start = time.time()
        oracle.update(run_tsc(packages, args.jobs, flags))
        os.makedirs(os.path.dirname(ORACLE), exist_ok=True)
        with opener(ORACLE, 'wt') as f:
            for p in sorted(oracle):
                f.write(json.dumps(oracle[p], separators=(',', ':')) + '\n')
        print(f'tsc on {len(packages)} packages in {time.time() - start:.0f}s, written to {ORACLE}')
        return

    oracle = {r['project']: r for r in map(json.loads, opener(ORACLE, 'rt'))}
    missing_oracle = [p for p in packages if p not in oracle]
    if missing_oracle:
        print(f'{len(missing_oracle)} packages have no tsc verdict (run --tsc): {" ".join(missing_oracle[:5])}...')
    # tsc itself crashing on a package leaves it without an oracle
    packages = [p for p in packages if p in oracle and 'crash' not in oracle[p]]

    if args.last:
        results = json.load(open(LAST))
        results = [r for r in results if r['package'] in set(packages)]
    else:
        if not args.no_build:
            subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
        start = time.time()
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            results = list(pool.map(lambda p: run_tttt(p, args.timeout, args.mem_gb, flags), packages))
        elapsed = time.time() - start
        with open(LAST, 'w') as f:
            json.dump(results, f)

    verdicts = collections.defaultdict(list)
    causes = collections.defaultdict(lambda: {'packages': set(), 'diags': 0, 'example': None})
    only = collections.Counter()
    for r in results:
        p = r['package']
        if r['status'] != 'ok':
            verdicts['timeout' if r['status'] == 'timeout' else 'crash'].append(p)
            continue
        spurious, missing = compare(p, oracle[p], r)
        verdicts['both' if spurious and missing else 'spurious' if spurious else 'missing' if missing else 'match'].append(p)
        keys = set()
        for d, sp in [(d, True) for d in spurious] + [(d, False) for d in missing]:
            k = root_cause(p, d, sp)
            keys.add(k)
            c = causes[k]
            c['packages'].add(p)
            c['diags'] += 1
            if c['example'] is None:
                c['example'] = f'{d[0]}:{d[1]}' + (f'  {d[3][:100]}' if sp else '')
            if args.key and args.key in k:
                print(f'{p}  {d[0]}:{d[1]} TS{d[2]}' + (f' {d[3][:200]}' if sp else ' (missing)'))
        if len(keys) == 1:
            only[keys.pop()] += 1
    if args.key:
        return
    if args.names:
        print('\n'.join(verdicts.get(args.names, [])))
        return

    total = len(results)
    tsc_clean = sum(1 for r in results if not oracle[r['package']]['diags'])
    print(f'{total} packages ({tsc_clean} clean under tsc): ' +
          ', '.join(f'{k} {len(verdicts[k])}' for k in ('match', 'spurious', 'missing', 'both', 'crash', 'timeout')))
    tttt_wall = sum(r['wall'] for r in results)
    tsc_ms = sum(oracle[r['package']]['ms'] for r in results)
    print(f'tttt {tttt_wall:.0f}s single-threaded in total' + (f' ({elapsed:.0f}s wall)' if not args.last else '') +
          f', tsc {tsc_ms / 1000:.0f}s (in one warm process, libs parsed once)')
    for v in ('crash', 'timeout'):
        for p in verdicts[v][:args.n]:
            r = next(r for r in results if r['package'] == p)
            print(f'\n{v.upper()} {p} ({r["status"]}):\n    ' + r['tail'].replace('\n', '\n    ')[-400:])
    slow = sorted((r for r in results if r['wall'] > 0.5 and r['wall'] > oracle[r['package']]['ms'] / 1000),
                  key=lambda r: -r['wall'])
    if slow:
        print('\nslower than tsc (perf bugs):\n' + '\n'.join(
            f'  {r["package"]:40} tttt {r["wall"]:6.2f}s  tsc {oracle[r["package"]]["ms"] / 1000:6.2f}s' for r in slow[:args.n]))
    print(f'\nroot causes by packages (+ spurious, - missing; "only": packages it is the only mismatch of):')
    print(f'  {"pkgs":>5} {"only":>5} {"diags":>6}  key / example')
    for k, c in sorted(causes.items(), key=lambda kc: (-len(kc[1]['packages']), -kc[1]['diags']))[:args.n]:
        print(f'  {len(c["packages"]):5} {only[k]:5} {c["diags"]:6}  {k[:110]}\n{"":21}{c["example"][:130]}')
    print(f'\nresults saved for --last in {LAST}')


if __name__ == '__main__':
    sys.exit(main())
