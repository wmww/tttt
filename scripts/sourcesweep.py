#!/usr/bin/env python3
"""The source sweep (plan 52): tttt on ~150 real TypeScript repositories (test/sourcesweep/repos.txt, fetched and
installed by thirdparty/ensure-sources.sh), every tsconfig.json in each checked with `-p` (REPO/path/to/dir), and every
other tsconfig*.json no config extends (REPO/path/tsconfig.build.json), compared with what tsc reports for it. tsc's verdicts are cached in test/sourcesweep/tsc.jsonl.gz (regenerate with --tsc, ~12 minutes on 4
cores, after changing the manifest or the oracle's pin). Most projects aren't clean under tsc (test configs without the runner's types,
unbuilt references, fixtures): the oracle is tsc's output, not cleanliness. A crash, hang or memory runaway is listed
first, then the diagnostics tttt gets wrong ranked by root cause across *repositories*, then projects: code and message
shape (quoted parts elided), and for a diagnostic in node_modules the dependency it is in.

  scripts/sourcesweep.py                  # every project (a few minutes on 4 cores)
  scripts/sourcesweep.py -f '^hono/'      # the projects (REPO/path/to/dir) matching a regex
  scripts/sourcesweep.py -k '+TS2322'     # list the projects and diagnostics whose root-cause key contains this
  scripts/sourcesweep.py -k=-TS6305       # (a missing-diagnostic key starts with '-': attach it with '=')
  scripts/sourcesweep.py --names spurious # the projects of a verdict (crash, timeout, spurious, missing, both, match)
  scripts/sourcesweep.py --last ...       # reuse the last run's results (-k, --names) without running tttt again
  scripts/sourcesweep.py --sample 4       # a fixed quarter of the projects (by a hash of the name: scripts/oracles.py's)
  scripts/sourcesweep.py --tsc [-f ...]   # regenerate tsc's cached verdicts (all projects, or -f's)
  scripts/sourcesweep.py --tsc-new        # tsc on the projects without a cached verdict (repos added to the manifest)
  (tsgo-side runs keep at most MemAvailable / 8 GB processes, sweep.tsc_jobs(): openclaw's take 6-11 GB each)
  scripts/sourcesweep.py --build [...]    # the solutions instead: `tsc -b` against `tttt -b` (list_build_projects)
  scripts/sourcesweep.py --defaults [--runs 3]  # tttt at its default threads and checkers (what users run), each
                                          # project N times: a project whose runs differ is verdict `varies`
The plain sweep runs tttt at --threads 1 (one checker): a checker-count or schedule dependence (which file a checker
saw first) is invisible there and visible to every user. --defaults runs what users run (fewer jobs at once: each
tttt takes the cores); `varies` lists a project whose runs disagree (on any diagnostic line), and its first run is
compared with tsc as usual. Its results file is its own (--last with --defaults reads it).
"""
import argparse, collections, concurrent.futures, gzip, hashlib, json, os, re, resource, signal, subprocess, sys, tempfile, threading, time, zlib

import sweep, tscformat, tttt_run

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = os.path.join(ROOT, 'thirdparty/sources')
MANIFEST = os.path.join(ROOT, 'test/sourcesweep/repos.txt')
ORACLE = os.path.join(ROOT, 'test/sourcesweep/tsc.jsonl.gz')
LAST = f'/tmp/sourcesweep-{os.path.basename(ROOT)}.json'
BUILD_ORACLE = os.path.join(ROOT, 'test/sourcesweep/tsc-build.jsonl.gz')
TSC = [os.path.join(ROOT, 'thirdparty/typescript/built/tsc')]
SANDBOX = ['bwrap', '--ro-bind', '/', '/', '--bind', '/tmp', '/tmp', '--dev', '/dev', '--proc', '/proc']  # (clidiff's)
# --build: solutions whose mismatch is a tsgo bug tttt deliberately doesn't emulate, with why (agents/bugs/
# sourcesweep_build.md). A mismatch there is verdict `known` (listed apart, not among the root causes); a match is a match
KNOWN_BUILD = {
    'atproto/tsconfig.json': "tsgo's build-wide cachedvfs never sees writes: a dist an earlier program probed stays "
                             'missing (TS2307 tsgo reports, tttt rightly not)',
    'b4run/tsconfig.json': "tsgo's builder race: core starts after permissions only by timing (2 places apart in "
                           'schedule order, inside the 4-builder window)',
}
# Directories that hold no project of the repo's own: outputs, dependencies, tooling state
SKIP_DIRS = {'node_modules', 'dist', 'build'}


def list_repos():
    return [line.split()[0] for line in open(MANIFEST) if line.strip() and not line.startswith('#')]


def no_perf_repos():
    """Repos a `# perf: no NAME` line excludes from perf measurements (correctness only)"""
    return {line.split()[3] for line in open(MANIFEST) if line.startswith('# perf: no ')}


def list_projects(filter=None, every_config=False, perf=False):
    """Every directory with a tsconfig.json in every installed repo, as REPO/path (outside node_modules, outputs and
    dot-directories). every_config (the -p sweep's): also every other `tsconfig*.json` and `*.tsconfig.json` there, as
    REPO/path/tsconfig.build.json, except the bases another config of the repo extends (`tsconfig.base.json`: nobody
    checks one by itself, and without `include` it would take its whole directory). tsconfig.json keeps its directory
    name, so the cached verdicts stay valid"""
    projects = []
    excluded = no_perf_repos() if perf else set()  # (perf: the perf scripts' list)
    for repo in list_repos():
        if repo in excluded:
            continue
        base = os.path.join(SOURCES, repo)
        if not os.path.exists(os.path.join(base, '.tttt-installed')):
            continue
        others, extended = [], set()
        for root, dirs, files in os.walk(base):
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith('.'))
            if 'tsconfig.json' in files:
                projects.append(os.path.relpath(root, SOURCES))
            if not every_config:
                continue
            for f in sorted(files):
                if not (re.fullmatch(r'tsconfig.*\.json', f) or f.endswith('.tsconfig.json')):
                    continue
                path = os.path.join(root, f)
                if f != 'tsconfig.json':
                    others.append(path)
                c = jsonc(path)
                ext = c.get('extends') if isinstance(c, dict) else None
                for e in ext if isinstance(ext, list) else [ext]:
                    if isinstance(e, str) and e.startswith('.'):
                        target = os.path.normpath(os.path.join(root, e))
                        extended.update({target, target + '.json', os.path.join(target, 'tsconfig.json')})
        projects += [os.path.relpath(p, SOURCES) for p in others if p not in extended]
    if filter:
        projects = [p for p in projects if re.search(filter, p)]
    return projects


def jsonc(path):
    """A tsconfig's JSON with comments and trailing commas (None when unreadable)"""
    try:
        t = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        return None
    t = re.sub(r'("(?:[^"\\\n]|\\.)*")|//[^\n]*|/\*.*?\*/', lambda m: m.group(1) or '', t, flags=re.S)
    t = re.sub(r',(\s*[}\]])', r'\1', t)
    try:
        return json.loads(t)
    except ValueError:
        return None


def config_path(base, path):
    """A reference's or `tsc -b` argument's config file: a directory means its tsconfig.json"""
    p = os.path.normpath(os.path.join(base, path))
    return os.path.join(p, 'tsconfig.json') if os.path.isdir(p) else p


def list_build_projects(filter=None):
    """The solutions `tsc -b` checks, as REPO/path/to/config.json: every tsconfig*.json with `references` that no
    other config of its repo references (a solution's root: Vite's template, a monorepo's root, jest's per-package
    __tests__ configs). Roots sharing a subtree each build it"""
    projects = []
    for repo in list_repos():
        base = os.path.join(SOURCES, repo)
        if not os.path.exists(os.path.join(base, '.tttt-installed')):
            continue
        refs, referenced = {}, set()
        for root, dirs, files in os.walk(base):
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith('.'))
            for f in files:
                path = os.path.join(root, f)
                if re.fullmatch(r'tsconfig.*\.json', f):
                    c = jsonc(path)
                    r = c.get('references') if isinstance(c, dict) else None
                    if isinstance(r, list) and r:
                        refs[path] = [config_path(root, x['path']) for x in r if isinstance(x, dict) and isinstance(x.get('path'), str)]
                        referenced.update(refs[path])
        for path in sorted(refs):
            if path not in referenced:
                projects.append(os.path.relpath(path, SOURCES))
    if filter:
        projects = [p for p in projects if re.search(filter, p)]
    return projects


def check_sources():
    """scripts/sourcescheck.sh: warn (stderr) when the shared checkouts aren't as installed (a stray write into a
    hardlinked dependency file corrupts every repo sharing it)"""
    p = subprocess.run([os.path.join(ROOT, 'scripts/sourcescheck.sh')], capture_output=True, text=True)
    if p.returncode:
        print(f'WARNING: {p.stdout.strip()}', file=sys.stderr)


def cached_projects(build, filter=None):
    """list_build_projects() or list_projects(every_config=True), cached in /tmp by the state of the checkouts: the
    walk over every repo takes ~1 min cold, the key is a stat of each repo's install marker and of the manifest"""
    h = hashlib.sha1(f'{build} {os.path.getmtime(__file__)} {os.path.getmtime(MANIFEST)}'.encode())
    for repo in list_repos():
        marker = os.path.join(SOURCES, repo, '.tttt-installed')
        h.update(f'{repo} {os.path.getmtime(marker) if os.path.exists(marker) else 0}\n'.encode())
    cache = f'/tmp/sourcesweep-projects-{"build" if build else "p"}-{h.hexdigest()[:16]}.json'
    try:
        projects = json.load(open(cache))
    except (OSError, ValueError):
        projects = list_build_projects() if build else list_projects(every_config=True)
        with open(cache + '.tmp', 'w') as f:
            json.dump(projects, f)
        os.replace(cache + '.tmp', cache)
    return [p for p in projects if re.search(filter, p)] if filter else projects


def run_tsc_build(project, timeout):
    """tsgo's `tsc -b --force` on a solution in a sandbox (bwrap: every checkout overlaid, what the build writes
    discarded, ngrx's outDir is outside its repo), from SOURCES like tttt"""
    checkout = os.path.realpath(SOURCES)
    tmp = tempfile.mkdtemp(prefix='buildsweep-')
    up, work = os.path.join(tmp, 'up'), os.path.join(tmp, 'work')
    os.makedirs(up), os.makedirs(work)
    start = time.time()
    try:
        p = subprocess.run([*SANDBOX, '--overlay-src', checkout, '--overlay', up, work, checkout, '--', *TSC, '-b',
                            '--force', project, '--pretty', 'false'], cwd=SOURCES, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout)
        out, status = p.stdout.decode(errors='replace'), p.returncode
    except subprocess.TimeoutExpired:
        return {'project': project, 'ms': 0, 'crash': 'timeout', 'diags': []}
    finally:
        subprocess.run(['rm', '-rf', tmp])
    ms = int((time.time() - start) * 1000)
    if status not in (0, 1, 2):
        return {'project': project, 'ms': ms, 'crash': f'exit {status}: {out[-300:]}', 'diags': []}
    r = {'project': project, 'ms': ms, 'diags': text_diags(out)}
    if out:
        r['text'] = out
    return r


def run_tttt_build(project, timeout, mem_gb):
    """sweep.run_tttt's record for `tttt -b PROJECT` (writing nothing)"""
    r = tttt_run.run([os.path.join(ROOT, 'release/tttt'), '-b', project, '--pretty', 'false', '--no-write',
                      '--threads', '1'], SOURCES, timeout=timeout, mem_gb=mem_gb, log_errors=True)
    text = r.text()
    tail = '\n'.join(l for l in text.split('\n') if l.strip())[-600:] if r.status != 'ok' else ''
    return {'package': project, 'status': r.status, 'wall': r.wall,
            'diags': [[f, l, c, m] for f, l, _, c, m in tscformat.parse(text)], 'tail': tail}


def text_diags(text):
    """The diagnostics of tsc's output, as tttt-oracle lists them"""
    return [[f, l, c, code] for f, l, c, code, _ in tscformat.parse(text)]


def load_oracle(path=ORACLE):
    """tsc's cached verdicts, {project: record}. A record's `diags` aren't stored when its `text` says the same"""
    oracle = {}
    if not os.path.exists(path):
        return oracle
    for r in map(json.loads, gzip.open(path, 'rt')):
        if 'diags' not in r:
            r['diags'] = text_diags(r['text'])
        oracle[r['project']] = r
    return oracle


def save_oracle(oracle, path=ORACLE):
    with gzip.open(path, 'wt') as f:
        for p in sorted(oracle):
            r = oracle[p]
            if r.get('text') and text_diags(r['text']) == r['diags']:
                r = {k: v for k, v in r.items() if k != 'diags'}  # (Half the file's size)
            f.write(json.dumps(r, separators=(',', ':')) + '\n')


def run_tsc(projects, jobs):
    """tsc on the projects (sweep.run_tsc: tsgo's tttt-oracle), grouped by repository. With the CLI's whole output
    (`text`, empty when clean): scripts/clidiff.py's expectation"""
    results = sweep.run_tsc(sorted(projects, key=lambda p: p.split('/')[0]), jobs, extra=('--text',), cwd=SOURCES,
                            progress=True)
    if any('text' not in r and 'crash' not in r for r in results.values()):
        sys.exit('tttt-oracle has no --text: an older build (thirdparty/ensure-typescript.sh rebuilds it)')
    for r in results.values():
        if r.get('text') == '':
            del r['text']  # (Implied by a clean verdict: not stored)
    return results


def root_cause(project, diag, spurious):
    """The key a mismatching diagnostic is ranked under: its code, for a spurious one its message shape, and for a
    file in node_modules the dependency (the same declaration read wrong in 40 projects is one bug)"""
    # (The innermost node_modules: pnpm's are node_modules/.pnpm/NAME@VERSION/node_modules/NAME)
    parts = diag[0].split('/')
    dep = ''
    if 'node_modules' in parts:
        i = len(parts) - 1 - parts[::-1].index('node_modules')
        dep = '/'.join(parts[i:i + (3 if parts[i + 1:i + 2] and parts[i + 1].startswith('@') else 2)])
    where = f' in {dep}' if dep else ''
    return f'+TS{diag[2]}{where} {sweep.shape(diag[3])}' if spurious else f'-TS{diag[2]}{where}'


def runs_of(run, project, timeout, mem_gb, runs):
    """run's record of project, run `runs` times: the first run's, with `varies` ({counts, lines}: each run's
    diagnostic count and the lines not in every run) when the runs' diagnostics differ"""
    records = [run(project, timeout, mem_gb) for _ in range(runs)]
    first = records[0]
    if any(r['status'] != first['status'] for r in records):
        first['tail'] += '\n(runs differ in status: ' + ', '.join(r['status'] for r in records) + ')'
    sets = [collections.Counter(f'{d[0]}:{d[1]} TS{d[2]} {d[3][:160]}' for d in r['diags']) for r in records]
    if any(c != sets[0] for c in sets):
        every = sets[0]
        for c in sets[1:]:
            every = every & c
        lines = sorted(set().union(*sets) - set(every))
        first['varies'] = {'counts': [len(r['diags']) for r in records], 'lines': lines}
    return first


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-f', '--filter', help='regex the project (REPO/path) must match (re.search)')
    ap.add_argument('-j', '--jobs', type=int, default=os.cpu_count())
    ap.add_argument('-n', type=int, default=40, help='rows per table')
    ap.add_argument('-k', '--key', help='list the diagnostics whose root-cause key contains this')
    ap.add_argument('--names', help='list the projects with this verdict')
    ap.add_argument('--last', action='store_true', help="reuse the last run's results")
    ap.add_argument('--tsc', action='store_true', help="regenerate tsc's cached verdicts (all projects, or -f's)")
    ap.add_argument('--tsc-new', action='store_true', help='tsc on the projects without a cached verdict')
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--mem-gb', type=int, default=8)
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--sample', type=int, help='only the projects whose name hashes to 0 modulo this')
    ap.add_argument('--build', action='store_true', help='the solutions, `-b` (list_build_projects), not every `-p`')
    ap.add_argument('--defaults', action='store_true',
                    help="tttt at the CLI's default threads and checkers instead of --threads 1")
    ap.add_argument('--runs', type=int, default=1, help='run each project N times; differing runs: verdict varies')
    args = ap.parse_args()
    check_sources()

    global LAST, ORACLE
    projects = cached_projects(args.build, args.filter)
    if args.build:
        ORACLE = BUILD_ORACLE
        LAST = LAST.replace('.json', '-build.json')
        args.timeout = max(args.timeout, 600)
    if args.defaults:
        LAST = LAST.replace('.json', '-defaults.json')
        if '-j' not in sys.argv and '--jobs' not in sys.argv:
            args.jobs = max(1, (os.cpu_count() or 4) // 4)  # (Each run takes the cores)
    if args.sample:
        projects = [p for p in projects if zlib.crc32(p.encode()) % args.sample == 0]
        LAST = LAST.replace('.json', f'-sample{args.sample}.json')  # (Not the full run's)
    if args.tsc or args.tsc_new:
        oracle = {}
        if args.filter or args.tsc_new:
            oracle = load_oracle(ORACLE)
        if args.tsc_new:
            projects = [p for p in projects if p not in oracle]
        start = time.time()
        if args.build:
            with concurrent.futures.ThreadPoolExecutor(sweep.tsc_jobs(args.jobs)) as pool:
                oracle.update((r['project'], r) for r in pool.map(lambda p: run_tsc_build(p, args.timeout), projects))
        else:
            oracle.update(run_tsc(projects, args.jobs))
        save_oracle(oracle, ORACLE)
        print(f'tsc on {len(projects)} projects in {time.time() - start:.0f}s, written to {ORACLE}')
        return

    oracle = load_oracle(ORACLE)
    missing_oracle = [p for p in projects if p not in oracle]
    if missing_oracle:
        print(f'{len(missing_oracle)} projects have no tsc verdict (run --tsc): {" ".join(missing_oracle[:5])}...',
              file=sys.stderr)
    missing_repos = [r for r in list_repos() if not os.path.exists(os.path.join(SOURCES, r, '.tttt-installed'))]
    if missing_repos:
        print(f'{len(missing_repos)} repos not installed (thirdparty/ensure-sources.sh): {" ".join(missing_repos[:5])}...',
              file=sys.stderr)
    projects = [p for p in projects if p in oracle and 'crash' not in oracle[p]]

    if args.last:
        results = json.load(open(LAST))
        results = [r for r in results if r['package'] in set(projects)]
    else:
        if not args.no_build:
            subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
        start = time.time()
        # The slowest projects first, so they don't end up alone at the end
        order = sorted(projects, key=lambda p: -oracle[p]['ms'])
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            run = run_tttt_build if args.build else \
                lambda p, t, m: sweep.run_tttt(p, t, m, cwd=SOURCES, defaults=args.defaults)
            results = list(pool.map(lambda p: runs_of(run, p, args.timeout, args.mem_gb, args.runs), order))
        results.sort(key=lambda r: r['package'])
        elapsed = time.time() - start
        with open(LAST, 'w') as f:
            json.dump(results, f)

    verdicts = collections.defaultdict(list)
    causes = collections.defaultdict(lambda: {'projects': set(), 'repos': set(), 'diags': 0, 'example': None})
    for r in results:
        p = r['package']
        if r['status'] != 'ok':
            verdicts[sweep.verdict_of_status(r['status'])].append(p)
            continue
        if r.get('varies'):
            verdicts['varies'].append(p)
        spurious, missing = sweep.compare(p, oracle[p], r, SOURCES)
        if (spurious or missing) and args.build and p in KNOWN_BUILD:
            verdicts['known'].append(p)
            continue
        v = 'both' if spurious and missing else 'spurious' if spurious else 'missing' if missing else 'match'
        if not (v == 'match' and r.get('varies')):  # (A project whose runs differ doesn't match, whatever its first did)
            verdicts[v].append(p)
        for d, sp in [(d, True) for d in spurious] + [(d, False) for d in missing]:
            k = root_cause(p, d, sp)
            c = causes[k]
            c['projects'].add(p)
            c['repos'].add(p.split('/')[0])
            c['diags'] += 1
            if c['example'] is None:
                c['example'] = f'{d[0]}:{d[1]}' + (f'  {d[3][:100]}' if sp else '')
            if args.key and args.key in k:
                print(f'{p}  {d[0]}:{d[1]} TS{d[2]}' + (f' {d[3][:200]}' if sp else ' (missing)'))
    if args.key:
        return
    if args.names:
        print('\n'.join(verdicts.get(args.names, [])))
        return

    total = len(results)
    tsc_clean = sum(1 for r in results if not oracle[r['package']]['diags'])
    repos = collections.defaultdict(list)
    for r in results:
        repos[r['package'].split('/')[0]].append(r['package'])
    matching = set(verdicts['match'])
    repos_matching = sum(1 for ps in repos.values() if all(p in matching for p in ps))
    print(f'{len(matching)} / {total} projects match ({tsc_clean} clean under tsc), '
          f'{repos_matching} / {len(repos)} repos fully match: ' +
          ', '.join(f'{k} {len(verdicts[k])}' for k in ('spurious', 'missing', 'both', 'crash', 'timeout')) +
          (f', varies {len(verdicts["varies"])} (runs differ: a schedule or checker-count dependence)'
           if args.runs > 1 or verdicts['varies'] else '') +
          (f', killed {len(verdicts["killed"])} (memory or SIGKILL: not crashes)' if verdicts['killed'] else '') +
          (f', known {len(verdicts["known"])}' if verdicts['known'] else ''))
    for p in verdicts['known']:
        print(f'  known: {p}: {KNOWN_BUILD[p]}')
    tttt_wall = sum(r['wall'] for r in results)
    tsc_ms = sum(oracle[r['package']]['ms'] for r in results)
    print(f'tttt {tttt_wall:.0f}s {"at the defaults" if args.defaults else "single-threaded"} in total' + (f' ({elapsed:.0f}s wall)' if not args.last else '') +
          f', tsc {tsc_ms / 1000:.0f}s (in one warm process, libs parsed once)' +
          (f'; {sweep.load_note()}' if not args.last else ''))
    for v in ('crash', 'timeout', 'killed'):
        for p in verdicts[v][:args.n]:
            r = next(r for r in results if r['package'] == p)
            print(f'\n{v.upper()} {p} ({r["status"]}):\n    ' + r['tail'].replace('\n', '\n    ')[-400:])
    for p in verdicts['varies'][:args.n]:
        r = next(r for r in results if r['package'] == p)
        print(f'\nVARIES {p}: diagnostics counts {r["varies"]["counts"]}; not in every run:\n    ' +
              '\n    '.join(r['varies']['lines'][:8]))
    slow = sorted((r for r in results if r['wall'] > 1 and r['wall'] > oracle[r['package']]['ms'] / 1000),
                  key=lambda r: -r['wall'])
    if slow:
        print('\nslower than tsc (perf bugs):\n' + '\n'.join(
            f'  {r["package"]:50} tttt {r["wall"]:6.2f}s  tsc {oracle[r["package"]]["ms"] / 1000:6.2f}s' for r in slow[:args.n]))
    print(f'\nroot causes by repos, then projects (+ spurious, - missing):')
    print(f'  {"repos":>5} {"projs":>5} {"diags":>6}  key / example')
    for k, c in sorted(causes.items(), key=lambda kc: (-len(kc[1]['repos']), -len(kc[1]['projects']), -kc[1]['diags']))[:args.n]:
        print(f'  {len(c["repos"]):5} {len(c["projects"]):5} {c["diags"]:6}  {k[:110]}\n{"":22}{c["example"][:130]}')
    print(f'\nresults saved for --last in {LAST}')


if __name__ == '__main__':
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    sys.exit(main())
