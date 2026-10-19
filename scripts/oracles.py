#!/usr/bin/env python3
"""Every oracle in one go, one summary line each, for comparing a change against HEAD (plan 51's deletion census):
the suite (saves test/baseline_results.ts like scripts/fasttest.sh -s, then the components lost/gained against git HEAD's),
every pinned corpus, the CLI's whole output with message text (scripts/clidiff.py, from tsc's cached output; --quick:
the corpora's), the DefinitelyTyped sweep, `$ExpectType`, flagflip, the source sweep, perf at breadth
(scripts/perfsweep.py: check time and type count ratios against tsgo), declaration emit (scripts/dtsdiff.py), and the output's independence of the checker count
(scripts/kdiff.py, the suite's --dump at 1 and 16 checkers) and of the checker's history (a fresh checker per file,
--checker-memory 0, plan 67; the suite's too with deferred bodies, whose materializations are then dropped and made
again, plan 78).

  scripts/oracles.py                  # everything (~15 min)
  scripts/oracles.py --quick          # suite, corpora and their clidiff only (~2 min)
  scripts/oracles.py --only sweep,expect
The per-oracle scripts keep their --last caches per tree name in /tmp, so worktrees can run this side by side.
"""
import argparse, os, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
CORPORA = ['tsc', 'tsc-src', 'type-fest', 'valibot', 'react', 'nest', 'webpack', 'zod', 'zod-root', 'xstate', 'drizzle',
           'vue', 'effect', 'vscode', 'excalidraw', 'tanstack-router', 'typescript-eslint']

def run(cmd, timeout=1800):
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    return p.stdout + p.stderr

def suite():
    out = run(['scripts/fasttest.sh', '-s'])
    m = re.search(r'(\d+)\D*passed', re.sub(r'\x1b\[[0-9;]*m', '', out))
    diff = run(['scripts/resultdiff.py'])
    lost = re.search(r'lost: (\d+) tests, (\d+) components', diff)
    gained = re.search(r'gained: (\d+) tests, (\d+) components', diff)
    print(f'suite: {m.group(1) if m else "?"} passed, lost {lost.group(2) if lost else "?"} components '
          f'({lost.group(1) if lost else "?"} tests), gained {gained.group(2) if gained else "?"}', flush=True)
    names = [l.strip() for l in diff.split('gained:')[0].splitlines()[1:] if l.startswith('  ')]
    for n in names[:40]: print('   ', n)
    if len(names) > 40: print(f'    ... ({len(names) - 40} more: scripts/resultdiff.py --lost)')
    # Every function body parsed up front (plan 78 defers them, the default) against the results just saved
    out = re.sub(r'\x1b\[[0-9;]*m', '', run(['release/baseline_tests', '--no-defer-bodies', '--summary']))
    m = re.search(r'(\d+)\D*passed', out)
    lost = re.findall(r'^ (?:ERR|SYM|TYP|DTS| - ).*?\s(\S+)\s+\(', out, re.M)
    print(f'suite --no-defer-bodies: {m.group(1) if m else "?"} passed, {len(lost)} newly failing', flush=True)
    for n in lost[:20]: print('   ', n)
    # Their materializations dropped and made again (plan 78 stage 2): a fresh checker per file, whose diagnostics and
    # declarations must be the same (its expression types go with the checkers: not compared)
    import tempfile, filecmp
    with tempfile.TemporaryDirectory(prefix='oracles-defer-') as tmp:
        dirs = [os.path.join(tmp, d) for d in ('kept', 'dropped')]
        for d, extra in zip(dirs, ([], ['--checker-memory', '0'])):
            os.mkdir(d)
            run(['release/baseline_tests', '--dump', d] + extra)
        names = sorted(f for f in os.listdir(dirs[0]) if f.endswith(('.errors.txt', '.d.ts')))
        differing = [f for f in names if not os.path.exists(os.path.join(dirs[1], f)) or
                     not filecmp.cmp(os.path.join(dirs[0], f), os.path.join(dirs[1], f), shallow=False)]
        print(f'suite --checker-memory 0: {len(differing)} of {len(names)} diagnostics/declaration '
              f'outputs differ', flush=True)
        for n in differing[:20]: print('   ', n)

def corpora():
    rows = []
    for c in CORPORA:
        out = run(['scripts/realworld.py', '--corpus', c, '--no-build'])
        # (Against tsc's cached diagnostics on the corpus: spurious/missing, `!` unless both are 0)
        m = re.search(r'against tsc: (\d+) spurious, (\d+) missing', out)
        crash = '' if re.search(r'exit [012],', out) else ' (' + (out.splitlines() or ['?'])[0][:40] + ')'
        stale = ' STALE' if 'STALE cache' in out else ''
        rows.append(f'{c} {m.group(1) + "/" + m.group(2) if m else "?"}{crash}{stale}' +
                    ('' if m and m.group(1) == m.group(2) == '0' and not crash else ' !'))
    print('corpora (spurious/missing against tsc): ' + ', '.join(rows), flush=True)

def sweep():
    out = run(['scripts/sweep.py', '--no-build', '-n', '5'])
    m = re.search(r'match \d+, spurious \d+, missing \d+, both \d+, crash \d+, timeout \d+', out)
    print('sweep: ' + (m.group(0) if m else '?'), flush=True)

def expect():
    out = run(['scripts/expecttypes.py', '--no-build', '-n', '5'])
    m = re.search(r'(\d+) match \(', out)
    c = re.search(r'(\d+) crash/timeout', out)
    print(f'expecttypes: {m.group(1) if m else "?"} match, {c.group(1) if c else "?"} crash/timeout', flush=True)

def flagflip():
    out = run(['scripts/flagflip.py', '--no-build', '-n', '5'])
    m = re.search(r'missing \d+, spurious \d+, other code \d+', out)
    print('flagflip: ' + (m.group(0) if m else '?'), flush=True)

def sources():
    # (A fixed quarter: all of it is ~9 minutes)
    out = run(['scripts/sourcesweep.py', '--no-build', '-n', '5', '--sample', '4'])
    m = re.search(r'\d+ / \d+ projects match.*', out)
    print('sourcesweep (sample 4): ' + (m.group(0) if m else '?'), flush=True)
    # (The solutions, tsc -b against tttt -b: ~3 min)
    out = run(['scripts/sourcesweep.py', '--no-build', '-n', '5', '--build'])
    m = re.search(r'\d+ / \d+ projects match.*', out)
    print('sourcesweep -b: ' + (m.group(0) if m else '?'), flush=True)

def perf():
    # (tttt only, tsgo's side is cached; -j 4 inflates the times of both sides alike, types are exact)
    out = run(['scripts/perfsweep.py', '--no-build', '-j', '4', '--summary'])
    # (Its stderr follows stdout in `out`: ensure-typescript's messages when tsgo's side ran for new projects)
    lines = [l for l in out.strip().splitlines() if not l.startswith('ensure-typescript')]
    print('perfsweep (tttt/tsgo): ' + (lines or ['?'])[-1], flush=True)

def dts():
    # (Plan 64: tsgo's .d.ts files on the corpora and the declaration-emitting source sweep projects, byte for byte)
    out = run(['scripts/dtsdiff.py', '--no-build', '-n', '5'])
    m = re.search(r'dts: projects (\d+ / \d+) fully match, files (\d+ / \d+) match', out)
    single = re.search(r'(\d+) more projects match tsgo --singleThreaded, (\d+) files', out)
    print(f'dts: projects {m.group(1)}, files {m.group(2)}' if m else 'dts: ?', flush=True, end='')
    print(f' (+{single.group(1)} projects, {single.group(2)} files as tsgo --singleThreaded)' if single else '', flush=True)

def clidiff(corpora_only=False):
    # (The CLI's whole output, message text included, against tsc's cached output: every corpus and source sweep
    # project, ~5 min; --quick: the corpora, ~1 min)
    out = run(['scripts/clidiff.py', '--no-build', '-n', '5', *(['--corpora'] if corpora_only else [])])
    m = re.search(r'clidiff: (.*)', out)
    c = re.search(r'^corpora: (.*)', out, re.M)
    print(f'clidiff{" (corpora)" if corpora_only else ""}: ' + (m.group(1) if m else '?') +
          (f'; corpora: {c.group(1)}' if c else ''), flush=True)

def kdiff():
    # (Plan 62: tsgo's output doesn't depend on its checker count, tttt's mustn't either. The suite dumped at 1 and 16
    # checkers, then the source sweep's raw CLI output at 1 and 16, ~7 min)
    import shutil, tempfile
    tmp = tempfile.mkdtemp(prefix='oracles-kdiff-')
    try:
        for k in (1, 16):
            os.mkdir(f'{tmp}/k{k}')
            run(['release/baseline_tests', '--checkers', str(k), '--dump', f'{tmp}/k{k}'])
        differ = subprocess.run(['diff', '-rq', f'{tmp}/k1', f'{tmp}/k16'], capture_output=True, text=True).stdout
        suite_differ = len(differ.splitlines())
    finally:
        shutil.rmtree(tmp)
    out = run(['scripts/kdiff.py'], timeout=3600)
    m = re.search(r'\d+ / \d+ projects differ.*', out)
    projects = [re.search(r'"project": "([^"]+)"', l).group(1) for l in out.splitlines() if l.startswith('{')]
    print(f'kdiff (1 vs 16 checkers): suite {suite_differ} files differ, sources ' + (m.group(0) if m else '?') +
          (': ' + ', '.join(projects[:12]) if projects else ''), flush=True)

def recycle():
    # (Plan 67: a fresh checker per file, --checker-memory 0, against one checker that never recycles, on the source
    # sweep: what a file's diagnostics depend on beyond its own checks, a stronger order-independence oracle than K)
    out = run(['scripts/kdiff.py', '-k', '1', '-m', '0'], timeout=3600)
    m = re.search(r'(\d+ / \d+) projects differ[^\n]*?(, \d+ timed out)?(?::|$)', out, re.M)
    projects = [re.search(r'"project": "([^"]+)"', l).group(1) for l in out.splitlines() if l.startswith('{')]
    print(f'recycle (fresh checker per file vs one): sources {m.group(1) if m else "?"} differ{m.group(2) or "" if m else ""}' +
          (': ' + ', '.join(projects[:12]) if projects else ''), flush=True)

ORACLES = {'suite': suite, 'corpora': corpora, 'sweep': sweep, 'expect': expect, 'flagflip': flagflip, 'sources': sources,
           'perf': perf, 'dts': dts, 'clidiff': clidiff, 'clidiff-corpora': lambda: clidiff(True), 'kdiff': kdiff,
           'recycle': recycle}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--quick', action='store_true', help='suite, corpora and their clidiff only')
    ap.add_argument('--only', help='comma-separated: ' + ','.join(ORACLES))
    args = ap.parse_args()
    which = args.only.split(',') if args.only else ['suite', 'corpora', 'clidiff-corpora'] if args.quick else \
        [o for o in ORACLES if o != 'clidiff-corpora']
    # (The suite builds; the others are run with --no-build after it)
    if 'suite' not in which: run(['ninja', '-C', 'release'])
    for w in which:
        start = time.time()
        ORACLES[w]()
        print(f'    ({time.time() - start:.0f}s)', flush=True) if os.environ.get('ORACLES_TIME') else None

if __name__ == '__main__':
    main()
