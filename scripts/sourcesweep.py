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
  scripts/sourcesweep.py --build [...]    # the solutions instead: `tsc -b` against `tttt -b` (list_build_projects)
"""
import argparse, collections, concurrent.futures, gzip, json, os, re, resource, signal, subprocess, sys, tempfile, threading, time, zlib

import sweep, tscformat

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = os.path.join(ROOT, 'thirdparty/sources')
MANIFEST = os.path.join(ROOT, 'test/sourcesweep/repos.txt')
ORACLE = os.path.join(ROOT, 'test/sourcesweep/tsc.jsonl.gz')
LAST = f'/tmp/sourcesweep-{os.path.basename(ROOT)}.json'
BUILD_ORACLE = os.path.join(ROOT, 'test/sourcesweep/tsc-build.jsonl.gz')
TSC = [os.path.join(ROOT, 'thirdparty/typescript/built/tsc')]
SANDBOX = ['bwrap', '--ro-bind', '/', '/', '--bind', '/tmp', '/tmp', '--dev', '/dev', '--proc', '/proc']  # (clidiff's)
# Directories that hold no project of the repo's own: outputs, dependencies, tooling state
SKIP_DIRS = {'node_modules', 'dist', 'build'}


def list_repos():
    return [line.split()[0] for line in open(MANIFEST) if line.strip() and not line.startswith('#')]


def list_projects(filter=None, every_config=False):
    """Every directory with a tsconfig.json in every installed repo, as REPO/path (outside node_modules, outputs and
    dot-directories). every_config (the -p sweep's): also every other `tsconfig*.json` and `*.tsconfig.json` there, as
    REPO/path/tsconfig.build.json, except the bases another config of the repo extends (`tsconfig.base.json`: nobody
    checks one by itself, and without `include` it would take its whole directory). tsconfig.json keeps its directory
    name, so the cached verdicts stay valid"""
    projects = []
    for repo in list_repos():
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
    limit = mem_gb << 30
    start = time.time()
    try:
        proc = subprocess.run([os.path.join(ROOT, 'release/tttt'), '-b', project, '--pretty', 'false', '--no-write',
                               '--threads', '1'], cwd=SOURCES, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=timeout, preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
        out, code = proc.stdout, proc.returncode
        status = 'ok' if code in (0, 1, 2) else f'exit {code}' if code > 0 else f'signal {-code}'
    except subprocess.TimeoutExpired as e:
        out, status = e.stdout or b'', 'timeout'
    text = out.decode(errors='replace')
    if status == 'ok' and re.search(r'\.c:\d+: (error|panic):', text):
        status = 'log_error'
    tail = '\n'.join(l for l in text.split('\n') if l.strip())[-600:] if status != 'ok' else ''
    return {'package': project, 'status': status, 'wall': time.time() - start,
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
    args = ap.parse_args()

    global LAST, ORACLE
    projects = list_build_projects(args.filter) if args.build else list_projects(args.filter, every_config=True)
    if args.build:
        ORACLE = BUILD_ORACLE
        LAST = LAST.replace('.json', '-build.json')
        args.timeout = max(args.timeout, 600)
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
            with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
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
            run = run_tttt_build if args.build else lambda p, t, m: sweep.run_tttt(p, t, m, cwd=SOURCES)
            results = list(pool.map(lambda p: run(p, args.timeout, args.mem_gb), order))
        results.sort(key=lambda r: r['package'])
        elapsed = time.time() - start
        with open(LAST, 'w') as f:
            json.dump(results, f)

    verdicts = collections.defaultdict(list)
    causes = collections.defaultdict(lambda: {'projects': set(), 'repos': set(), 'diags': 0, 'example': None})
    for r in results:
        p = r['package']
        if r['status'] != 'ok':
            verdicts['timeout' if r['status'] == 'timeout' else 'crash'].append(p)
            continue
        spurious, missing = sweep.compare(p, oracle[p], r, SOURCES)
        verdicts['both' if spurious and missing else 'spurious' if spurious else 'missing' if missing else 'match'].append(p)
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
          ', '.join(f'{k} {len(verdicts[k])}' for k in ('spurious', 'missing', 'both', 'crash', 'timeout')))
    tttt_wall = sum(r['wall'] for r in results)
    tsc_ms = sum(oracle[r['package']]['ms'] for r in results)
    print(f'tttt {tttt_wall:.0f}s single-threaded in total' + (f' ({elapsed:.0f}s wall)' if not args.last else '') +
          f', tsc {tsc_ms / 1000:.0f}s (in one warm process, libs parsed once)' +
          (f'; {sweep.load_note()}' if not args.last else ''))
    for v in ('crash', 'timeout'):
        for p in verdicts[v][:args.n]:
            r = next(r for r in results if r['package'] == p)
            print(f'\n{v.upper()} {p} ({r["status"]}):\n    ' + r['tail'].replace('\n', '\n    ')[-400:])
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
