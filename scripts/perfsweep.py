#!/usr/bin/env python3
"""Perf at breadth (plan 59): tttt against tsgo 7.1 on every source sweep project tsgo actually checks (its `Types`
above --min-types: a program gated by options errors makes ~100), both single-threaded. tttt runs with
`--checkers 1 --threads 1 --time`, tsgo with `-p P --noEmit --singleThreaded --extendedDiagnostics`; both sides' peak
RSS comes from wait4. Its ru_maxrss keeps the forked Python parent's resident set across exec (~45 MB on both sides):
the RSS ratios leave out projects where either side is below RSS_FLOOR_KB.

The type count is the machine-independent number: tttt's and tsgo's agree within ~10% where both do the same work
(type-fest 4.78M vs 5.10M), so a types ratio far above 1 is a missing cache or a runaway instantiation, found without
timing anything. Times are per machine and noisy under load: tsgo's cached side records the machine and the -j it was
measured at; compare time ratios only with nothing else running and the same -j on both sides.

tsgo's side is cached in test/perfsweep/tsgo.jsonl.gz (one line per project: types, instantiations, check/total s,
memory used, RSS, machine); projects without an entry are measured on the way (all of them: ~10 min at -j 4). tsc
--noEmit reads and writes the .tsbuildinfo of an incremental project, and skips checking what it says is unchanged: tsgo
gets a build info file of its own in /tmp each run (a cold check, nothing written into the checkouts).

  scripts/perfsweep.py                    # every project tsgo checks
  scripts/perfsweep.py --sample 4         # a fixed quarter (scripts/sourcesweep.py's hash)
  scripts/perfsweep.py -f '^rxjs/'        # projects matching a regex (REPO/path)
  scripts/perfsweep.py --corpora          # the -p corpora (scripts/realworld.py's) instead
  scripts/perfsweep.py --last             # reprint the last run's tables
  scripts/perfsweep.py --tsgo [-f ...]    # remeasure tsgo's side (after a machine or pin change)
"""
import argparse, concurrent.futures, gzip, json, os, re, resource, signal, socket, statistics, subprocess, sys, tempfile, time, zlib

import sourcesweep
from realworld import CORPORA, BUILD_CORPORA

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = sourcesweep.SOURCES
TSGO = os.path.join(ROOT, 'thirdparty/typescript/built/tsc')
CACHE = os.path.join(ROOT, 'test/perfsweep/tsgo.jsonl.gz')
LAST = f'/tmp/perfsweep-{os.path.basename(ROOT)}.json'
MACHINE = socket.gethostname()


def run(cmd, cwd, timeout, mem_gb):
    """Runs cmd, returns (stdout+stderr text, exit status or 'timeout', wall s, peak RSS KB)"""
    limit = mem_gb << 30
    start = time.time()
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
    out = []
    reader = concurrent.futures.ThreadPoolExecutor(1).submit(lambda: out.append(proc.stdout.read()))
    status = None
    while status is None:
        pid, code, usage = os.wait4(proc.pid, os.WNOHANG)
        if pid:
            status = os.waitstatus_to_exitcode(code)
            break
        if time.time() - start > timeout:
            proc.kill()
            _, code, usage = os.wait4(proc.pid, 0)
            status = 'timeout'
            break
        time.sleep(0.005)
    proc.returncode = 0  # (Reaped above: keep Popen from waiting again)
    reader.result()
    wall = time.time() - start
    return (out[0] if out else b'').decode(errors='replace'), status, wall, usage.ru_maxrss


def project_args(project):
    """(cwd, -p argument) for a source sweep project or a corpus"""
    if project in CORPORA:
        path = os.path.join(ROOT, CORPORA[project])
        return (os.path.dirname(path), os.path.basename(path)) if path.endswith('.json') else (path, '.')
    return SOURCES, project


def incremental(cwd, p):
    """Whether the project is incremental (composite implies it): tsc --noEmit then reads and writes its .tsbuildinfo,
    and skips checking what an earlier run's build info says is unchanged"""
    out = subprocess.run([TSGO, '-p', p, '--showConfig'], cwd=cwd, capture_output=True, text=True).stdout
    try:
        options = json.loads(out).get('compilerOptions', {})
    except ValueError:
        return False
    return options.get('incremental') is True or options.get('composite') is True


def run_tsgo(project, timeout, mem_gb, jobs):
    cwd, p = project_args(project)
    # (A cold check, the build info out of the checkout: a fresh file of our own)
    with tempfile.TemporaryDirectory(prefix='perfsweep-') as tmp:
        build_info = ['--tsBuildInfoFile', os.path.join(tmp, 'x.tsbuildinfo')] if incremental(cwd, p) else []
        text, status, wall, rss = run([TSGO, '-p', p, '--noEmit', '--singleThreaded', '--extendedDiagnostics',
                                       '--pretty', 'false', *build_info], cwd, timeout, mem_gb)
    stat = lambda name, conv=int: conv(m.group(1)) if (m := re.search(rf'^{name}:\s+([\d.]+)', text, re.M)) else None
    return {'project': project, 'status': status if status in (0, 1, 2) else str(status), 'types': stat('Types'),
            'instantiations': stat('Instantiations'), 'memory_kb': stat('Memory used'),
            'check_s': stat('Check time', float), 'total_s': stat('Total time', float), 'wall_s': round(wall, 3),
            'rss_kb': rss, 'machine': MACHINE, 'jobs': jobs}


def run_tttt(project, timeout, mem_gb):
    cwd, p = project_args(project)
    text, status, wall, rss = run([os.path.join(ROOT, 'release/tttt'), '-p', p, '--noEmit', '--pretty', 'false', '--checkers', '1',
                                   '--threads', '1', '--time'], cwd, timeout, mem_gb)
    phases = {m.group(1): float(m.group(2)) for m in re.finditer(r'^time: ([a-z+ ]+?) ([\d.]+) ms', text, re.M)}
    checker = re.search(r'^time: checker 0: ([\d.]+) ms, arena (\d+) MB, (\d+) types', text, re.M)
    return {'project': project, 'status': status if status in (0, 1, 2) else str(status),
            'check_s': phases.get('typecheck', 0) / 1000, 'total_s': sum(phases.values()) / 1000, 'wall_s': wall,
            'types': int(checker.group(3)) if checker else 0, 'arena_mb': int(checker.group(2)) if checker else 0,
            'rss_kb': rss}


def load_cache():
    return {r['project']: r for r in map(json.loads, gzip.open(CACHE, 'rt'))} if os.path.exists(CACHE) else {}


def save_cache(cache):
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with gzip.open(CACHE, 'wt') as f:
        for p in sorted(cache):
            f.write(json.dumps(cache[p], separators=(',', ':')) + '\n')


def parallel(fn, items, jobs, label):
    results = []
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        for i, r in enumerate(pool.map(fn, items)):
            results.append(r)
            if sys.stderr.isatty():
                print(f'\r{label} {i + 1} / {len(items)}', end='', file=sys.stderr, flush=True)
    if sys.stderr.isatty():
        print(file=sys.stderr)
    return results


# (Below it, wait4's peak RSS is the Python parent's at fork time, not the checker's)
RSS_FLOOR_KB = 64 << 10


def ratio(a, b):
    return a / b if b else float('inf')


def report(rows, n, cache):
    """rows: (project, tttt result, tsgo result) for every project both checked"""
    if not rows:
        print('no project checked by tsgo')
        return
    check = sorted(ratio(t['check_s'], g['check_s']) for _, t, g in rows)
    wall = sorted(ratio(t['wall_s'], g['wall_s']) for _, t, g in rows)
    types = sorted(ratio(t['types'], g['types']) for _, t, g in rows)
    rss = sorted(ratio(t['rss_kb'], g['rss_kb']) for _, t, g in rows
                 if min(t['rss_kb'], g['rss_kb']) >= RSS_FLOOR_KB)
    q = lambda xs, f: xs[min(len(xs) - 1, int(f * len(xs)))]
    print(f'{len(rows)} projects checked by both (tttt K1 vs tsgo --singleThreaded), ratios tttt / tsgo:')
    for name, xs in (('check time', check), ('wall', wall), ('types', types), ('peak RSS*', rss)):
        print(f'  {name:11} median {statistics.median(xs):5.2f}  p90 {q(xs, .9):5.2f}  worst {xs[-1]:6.2f}  '
              f'best {xs[0]:5.2f}  (> 1: {sum(1 for x in xs if x > 1)})')
    print(f'  (*: the {len(rss)} projects with both sides above {RSS_FLOOR_KB >> 10} MB, the forked Python\'s floor)')
    tc, gc = sum(t['check_s'] for _, t, _ in rows), sum(g['check_s'] for _, _, g in rows)
    tw, gw = sum(t['wall_s'] for _, t, _ in rows), sum(g['wall_s'] for _, _, g in rows)
    print(f'  total: check tttt {tc:.0f}s vs tsgo {gc:.0f}s ({ratio(tc, gc):.2f}), wall {tw:.0f}s vs {gw:.0f}s '
          f'({ratio(tw, gw):.2f}); types {sum(t["types"] for _, t, _ in rows) / 1e6:.1f}M vs '
          f'{sum(g["types"] for _, _, g in rows) / 1e6:.1f}M')
    machines = {(g['machine'], g['jobs']) for _, _, g in rows}
    if any(m != MACHINE for m, _ in machines) or len(machines) > 1:
        print(f'  (tsgo measured on {", ".join(f"{m} -j{j}" for m, j in sorted(machines))}: times not comparable '
              f'across machines, --tsgo remeasures)')

    def table(title, key):
        print(f'\n{title}:')
        print(f'  {"project":52} {"types":>6} {"tttt":>8} {"tsgo":>8} {"check":>6} {"tttt s":>7} {"tsgo s":>7} '
              f'{"rss":>5} {"tttt MB":>7}')
        for p, t, g in sorted(rows, key=key)[:n]:
            print(f'  {p[:52]:52} {ratio(t["types"], g["types"]):6.2f} {t["types"]:8} {g["types"]:8} '
                  f'{ratio(t["check_s"], g["check_s"]):6.2f} {t["check_s"]:7.2f} {g["check_s"]:7.2f} '
                  f'{ratio(t["rss_kb"], g["rss_kb"]):5.2f} {t["rss_kb"] >> 10:7}')
    table('worst types ratio (a missing cache or a runaway)', lambda r: -ratio(r[1]['types'], r[2]['types']))
    # (Projects checking in under 0.2 s are startup noise)
    table('worst check time ratio (tsgo check > 0.2 s)',
          lambda r: -ratio(r[1]['check_s'], r[2]['check_s']) if r[2]['check_s'] > 0.2 else 0)
    table('slowest in tttt', lambda r: -r[1]['check_s'])


def summary_line(rows):
    """One line for scripts/oracles.py"""
    if not rows:
        return '?'
    check = statistics.median(ratio(t['check_s'], g['check_s']) for _, t, g in rows)
    worst = max(rows, key=lambda r: ratio(r[1]['types'], r[2]['types']))
    return (f'{len(rows)} projects, median check ratio {check:.2f}, worst types ratio '
            f'{ratio(worst[1]["types"], worst[2]["types"]):.2f} ({worst[0]})')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-f', '--filter', help='regex the project must match (re.search)')
    ap.add_argument('-j', '--jobs', type=int, default=1, help='projects at once (times are inflated under load)')
    ap.add_argument('-n', type=int, default=20, help='rows per table')
    ap.add_argument('--sample', type=int, help='only the projects whose name hashes to 0 modulo this')
    ap.add_argument('--corpora', action='store_true', help="scripts/realworld.py's -p corpora instead")
    ap.add_argument('--last', action='store_true', help="reuse the last run's results")
    ap.add_argument('--tsgo', action='store_true', help="remeasure tsgo's side of the selected projects")
    ap.add_argument('--min-types', type=int, default=1000, help="tsgo's types below which a project isn't checked")
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--mem-gb', type=int, default=16)
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--summary', action='store_true', help='only the one line scripts/oracles.py prints')
    args = ap.parse_args()
    global LAST

    if args.corpora:
        projects = [c for c in CORPORA if c not in BUILD_CORPORA]
        LAST = LAST.replace('.json', '-corpora.json')
    else:
        projects = sourcesweep.list_projects()
    if args.filter:
        projects = [p for p in projects if re.search(args.filter, p)]
    if args.sample:
        projects = [p for p in projects if zlib.crc32(p.encode()) % args.sample == 0]
        LAST = LAST.replace('.json', f'-sample{args.sample}.json')
    if args.jobs > 1 and not args.summary:
        print(f'(-j {args.jobs}: times are inflated under load; types and RSS are not)', file=sys.stderr)

    cache = load_cache()
    todo = projects if args.tsgo else [p for p in projects if p not in cache]
    if todo:
        subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')], check=True, stdout=subprocess.DEVNULL)
        start = time.time()
        marker = tempfile.NamedTemporaryFile(prefix='perfsweep-marker-', delete=False).name
        # (The biggest first by the source sweep's tsc time, so they don't end up alone at the end)
        oracle = {r['project']: r['ms'] for r in map(json.loads, gzip.open(sourcesweep.ORACLE, 'rt'))}
        todo.sort(key=lambda p: -oracle.get(p, 1e9))
        for r in parallel(lambda p: run_tsgo(p, args.timeout, args.mem_gb, args.jobs), todo, args.jobs, 'tsgo'):
            cache[r['project']] = r
        save_cache(cache)
        # (Whatever build info got past incremental() anyway)
        stray = subprocess.run(['find', SOURCES + '/', '-name', 'node_modules', '-prune', '-o', '-name', '*.tsbuildinfo',
                                '-newer', marker, '-print', '-delete'], capture_output=True, text=True).stdout.split()
        os.unlink(marker)
        if stray:
            print(f'deleted {len(stray)} .tsbuildinfo files tsgo wrote: {" ".join(stray[:3])}', file=sys.stderr)
        if not args.summary:
            print(f'tsgo on {len(todo)} projects in {time.time() - start:.0f}s, cached in {CACHE}', file=sys.stderr)

    checked = [p for p in projects if (cache[p].get('types') or 0) >= args.min_types and cache[p]['status'] in (0, 1, 2)]
    if args.last:
        results = {r['project']: r for r in json.load(open(LAST))}
    else:
        if not args.no_build:
            subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
        order = sorted(checked, key=lambda p: -(cache[p]['check_s'] or 0))
        results = {r['project']: r for r in parallel(lambda p: run_tttt(p, args.timeout, args.mem_gb), order,
                                                      args.jobs, 'tttt')}
        with open(LAST, 'w') as f:
            json.dump(list(results.values()), f)

    failed = [p for p in checked if p in results and results[p]['status'] not in (0, 1, 2)]
    rows = [(p, results[p], cache[p]) for p in checked if p in results and p not in failed and results[p]['types']]
    if args.summary:
        print(summary_line(rows) + (f', {len(failed)} failed' if failed else ''))
        return
    print(f'{len(projects)} projects, {len(checked)} checked by tsgo (types >= {args.min_types})')
    for p in failed:
        print(f'  tttt FAILED {p}: {results[p]["status"]}')
    for p in checked:
        if p in results and p not in failed and not results[p]['types']:
            print(f'  tttt checked nothing: {p} (tsgo {cache[p]["types"]} types)')
    report(rows, args.n, cache)
    if not args.last:
        print(f'\n{sourcesweep.sweep.load_note()} at the end of the run')
    print(f'\nresults saved for --last in {LAST}')


if __name__ == '__main__':
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    sys.exit(main())
