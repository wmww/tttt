#!/usr/bin/env python3
"""tttt as a drop-in tsc (plan 53): `tttt -p P --noEmit --pretty false` against tsc's whole output on real projects,
byte for byte, exit codes included: the message text (elaboration chains, printed types) the other oracles don't
compare (they match by file, line and code). Projects: every -p/-b corpus (scripts/realworld.py's, `corpus:NAME`) and
every source sweep project (thirdparty/sources). tsc's output is cached: the source sweep's in its own cache
(test/sourcesweep/tsc.jsonl.gz, a record's `text`, absent when clean), the corpora's in test/corpora.tsc.jsonl
(`text`), both written by tsgo's tttt-oracle / CLI (`--singleThreaded` or not prints the same, checked on the corpora).

  scripts/clidiff.py                         # every corpus and source sweep project, from the caches (~5 min at -j 4)
  scripts/clidiff.py --corpora               # the corpora only (~1 min)
  scripts/clidiff.py -f '^vite/' -v          # projects matching a regex, the first differing lines of each mismatch
  scripts/clidiff.py -k chain                # the `text` mismatches' diagnostics whose kind starts so (see below)
  scripts/clidiff.py --last ...              # reuse the last run (-k, --names VERDICT)
  scripts/clidiff.py --tsc [-f ...]          # regenerate the stale expectations (no text or another pin; -f: all its)
  scripts/clidiff.py --live [--sample 8]     # against tsc run now (the source sweep's projects)
  scripts/clidiff.py --live --pretty ...     # live, the pretty format (colours, code frames, summary)
  scripts/clidiff.py --emit                  # live, `tttt -p P` against `tsc -p P` as they are, emitting (plan 64 stage 4)

Verdicts: match, `exit code`, `order` (the same lines in another order), `text` (the same diagnostics by file, line,
column and code, other text), `diagnostics` (a checker difference, the sweeps' business: -v shows those too), crash.
The `text` mismatches' differing diagnostics, paired in (file, line, column, code) order, are bucketed by their first
differing part (scripts/errtextdiff.py's kinds): `header:CODE MECHANISM` (the message; the mechanism compares the first
differing quoted piece: union order, import(), alias...; `wording` outside quotes) or `chain:...` (the elaboration
lines: missing, extra, shallower, deeper, or the first differing line's template).

--emit: the projects whose config (tsgo --showConfig) only emits declarations (emitDeclarationOnly with declaration
or composite), where tttt is a drop-in `tsc`: both run in a bubblewrap sandbox with an overlay over the checkout
(writes land in a /tmp upper directory, the checkout is untouched), and stdout, the exit code and the written files
(.tsbuildinfo aside: tttt writes none) must be identical. Verdicts as above, then by the written files (--verbose lists
them): `file text` (a .d.ts differs: dtsdiff.py's business), `declarationMap` (only .d.ts.map files differ: dtsdiff.py's
too), `missing files` (tsgo wrote more: JavaScript sources' declarations, plan 64 stage 6).

Exit codes: tttt follows tsc 7 (1 with any diagnostic under --noEmit, 2 when the program has no file). Live, projects tsc
takes more than --max-ms for (the source sweep's cached timing) are skipped, and tsc writes .tsbuildinfo files for
incremental projects even with --noEmit (and reads them: checking nothing): new ones are deleted."""
import argparse, collections, concurrent.futures, gzip, json, os, re, resource, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import realworld  # noqa: E402
import tscformat  # noqa: E402
import sourcesweep  # noqa: E402
import errtextdiff  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = os.path.join(ROOT, 'thirdparty/sources')
TSC = [os.path.join(ROOT, 'thirdparty/typescript/built/tsc')]
LAST = f'/tmp/clidiff-{os.path.basename(ROOT)}.json'
CORPUS = 'corpus:'


def run(cmd, cwd, timeout):
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout,
                           env={**os.environ, 'NO_COLOR': ''})
        return p.stdout.decode(errors='replace'), p.returncode
    except subprocess.TimeoutExpired:
        return None, 'timeout'


# tsgo's own nondeterminism, normalized on both sides: TS2460 names the export that "is" the local through
# findInMap over a Go map (reportNonExportedMember), so with several candidates (e.g. every unresolved export is the
# same unknownSymbol) it picks a random one per run; tttt takes the first in declaration order.
NONDETERMINISTIC = re.compile(r"(error TS2460: .* locally, but it is exported as )'[^']*'\.")


def normalize(out):
    return out if out is None else NONDETERMINISTIC.sub(r"\1'...'.", out)


SANDBOX = ['bwrap', '--ro-bind', '/', '/', '--bind', '/tmp', '/tmp', '--dev', '/dev', '--proc', '/proc']


def emitted(cmd, cwd, checkout, timeout):
    """cmd run in the sandbox, its writes under checkout captured: (stdout, exit code, {relative path: bytes})"""
    tmp = tempfile.mkdtemp(prefix='clidiff-emit-')
    up, work = os.path.join(tmp, 'up'), os.path.join(tmp, 'work')
    os.makedirs(up), os.makedirs(work)
    try:
        out, code = run([*SANDBOX, '--overlay-src', checkout, '--overlay', up, work, checkout, '--', *cmd], cwd, timeout)
        files = {}
        for d, _, names in os.walk(up):
            for f in names:
                path = os.path.join(d, f)
                # (A config's tsBuildInfoFile may be named anything ending so: better-auth's `.cache/ts/tsbuildinfo`)
                if not f.endswith('tsbuildinfo') and os.path.isfile(path) and not os.path.islink(path):
                    files[os.path.relpath(path, up)] = open(path, 'rb').read()
        return out, code, files
    finally:
        subprocess.run(['rm', '-rf', tmp])


def compare_emit(cwd, project, checkout, args):
    pretty = ['--pretty', 'true' if args.pretty else 'false']
    tsc_out, tsc_code, tsc_files = emitted(TSC + ['-p', project, *pretty], cwd, checkout, args.timeout)
    out, code, files = emitted([os.path.join(ROOT, 'release/tttt'), '-p', project, *pretty], cwd, checkout, args.timeout)
    if tsc_out is None or out is None:
        return 'timeout', tsc_out, out, tsc_code, code
    verdict, *_ = compare_output(normalize(tsc_out), normalize(out), tsc_code, code)
    if verdict == 'match' and files != tsc_files:
        # (Files tsgo writes and tttt doesn't are JavaScript sources' mostly, plan 64 stage 6; other text is
        # dtsdiff.py's business, the declaration maps too)
        is_map = lambda f: re.search(r'\.d\.[cm]?ts\.map$', f)
        text = any(f in tsc_files and tsc_files[f] != t and not is_map(f) for f, t in files.items()) or \
            any(f not in tsc_files for f in files)
        maps = any(f in tsc_files and tsc_files[f] != t and is_map(f) for f, t in files.items())
        verdict = 'file text' if text else 'declarationMap' if maps else 'missing files'
        if args.verbose:
            differ = sorted(f for f in set(files) | set(tsc_files) if files.get(f) != tsc_files.get(f))
            print(f'    {project}: {len(differ)} files differ: {" ".join(differ[:6])}', flush=True)
    return verdict, tsc_out, out, tsc_code, code


def emits_declarations_only(cwd, project):
    r = subprocess.run(TSC + ['-p', project, '--showConfig'], cwd=cwd, capture_output=True, text=True)
    try:
        c = json.loads(r.stdout).get('compilerOptions', {})
    except ValueError:
        return False
    return bool(c.get('emitDeclarationOnly') and (c.get('declaration') or c.get('composite')) and not c.get('noEmit'))


def compare_output(tsc_out, out, want_code, code):
    if out == tsc_out:
        verdict = 'match' if code == want_code else 'exit code'
    elif sorted(out.split('\n')) == sorted(tsc_out.split('\n')):
        verdict = 'order'  # (the same lines in another order: the CLI's sorting)
    elif sorted(d[:4] for d in tscformat.parse(out)) == sorted(d[:4] for d in tscformat.parse(tsc_out)):
        verdict = 'text'  # (the same diagnostics, other messages or chains)
    else:
        verdict = 'diagnostics'  # (a checker difference: the source sweep's business)
    return verdict, tsc_out, out, want_code, code


def compare(cwd, project, args):
    pretty = ['--pretty', 'true' if args.pretty else 'false']
    tsc_out, tsc_code = run(TSC + ['-p', project, '--noEmit', *pretty], cwd, args.timeout)
    out, code = run([os.path.join(ROOT, 'release/tttt'), '-p', project, '--noEmit', *pretty], cwd, args.timeout)
    if tsc_out is None or out is None:
        return 'timeout', tsc_out, out, tsc_code, code
    return compare_output(normalize(tsc_out), normalize(out), tsc_code, code)


def expectations(filter=None, corpora_only=False):
    """{project: (cwd, tttt command, tsc's record or None)}: the corpora (pin-checked) and the source sweep projects"""
    jobs = {}
    cache = realworld.load_tsc_cache()
    for c in realworld.CORPORA:
        name = CORPUS + c
        if filter and not re.search(filter, name) or not os.path.exists(os.path.join(ROOT, realworld.CORPORA[c])):
            continue
        cwd, options = realworld.invocation(c)
        r = cache.get(c)
        jobs[name] = (cwd, realworld.tttt_command(options), r if r and 'text' in r and r['pin'] == realworld.pin(c) else None)
    if corpora_only:
        return jobs
    oracle = sourcesweep.load_oracle()
    for p in sourcesweep.list_projects(filter):
        r = oracle.get(p)
        if r and 'crash' in r:
            continue
        if r and 'text' not in r:
            r = dict(r, text='') if not r['diags'] and r['status'] == 0 else None  # (Clean: not stored)
        # (One thread, as the source sweep runs it: -j counts processes. The corpora at the default threads)
        jobs[p] = (SOURCES, [os.path.join(ROOT, 'release/tttt'), '--pretty', 'false', '--noEmit', '--threads', '1',
                             '-p', p], r)
    return jobs


def regenerate(args):
    jobs = expectations(args.f, args.corpora)
    stale = [p for p, (_, _, r) in jobs.items() if r is None or args.f]
    corpora = [p[len(CORPUS):] for p in stale if p.startswith(CORPUS)]
    for c in corpora:
        start = time.time()
        subprocess.run([os.path.join(ROOT, 'scripts/realworld.py'), '--corpus', c, '--tsc'], check=True)
        print(f'  {c}: {time.time() - start:.0f}s', flush=True)
    sources = [p for p in stale if not p.startswith(CORPUS)]
    if sources:
        oracle = sourcesweep.load_oracle()
        start = time.time()
        oracle.update(sourcesweep.run_tsc(sources, args.jobs))
        sourcesweep.save_oracle(oracle)
        print(f'tsc on {len(sources)} source sweep projects in {time.time() - start:.0f}s, written to {sourcesweep.ORACLE}')


def run_tttt(cwd, cmd, timeout, mem_gb):
    limit = mem_gb << 30
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
        code = p.returncode
        return p.stdout.decode(errors='replace'), code if code in (0, 1, 2) else f'crash: exit {code}' if code > 0 \
            else f'crash: signal {-code}'
    except subprocess.TimeoutExpired as e:
        return (e.stdout or b'').decode(errors='replace'), 'crash: timeout'


def split_diag(block):
    """A diagnostic's text as errtextdiff's dict: the header message and the chain lines"""
    lines = block.split('\n')
    m = tscformat.DIAG.match(lines[0])
    return {'code': 'TS' + m.group(5), 'header': m.group(6), 'chain': lines[1:], 'related': [], 'squiggles': []}


def text_kinds(tsc_out, out):
    """The differing diagnostics of a `text` mismatch: [(kind, tsc's block, tttt's block)], paired in (file, line,
    column, code) order"""
    key = lambda b: tscformat.parse(b.split('\n')[0])[0][:4]
    a, b = sorted(tscformat.blocks(tsc_out), key=key), sorted(tscformat.blocks(out), key=key)
    rows = []
    for x, y in zip(a, b):
        if x != y:
            rows.append((errtextdiff.diag_kind(split_diag(x), split_diag(y)) or 'other', x, y))
    return rows


def first_lines(tsc_out, out, n=3):
    a, b = tsc_out.split('\n'), out.split('\n')
    shown = []
    for i in range(max(len(a), len(b))):
        if i >= len(a) or i >= len(b) or a[i] != b[i]:
            shown.append(f'    tsc:  {a[i] if i < len(a) else "(end)"}'[:240])
            shown.append(f'    tttt: {b[i] if i < len(b) else "(end)"}'[:240])
            if len(shown) == 2 * n:
                break
    return '\n'.join(shown)


def cached(args):
    jobs = expectations(args.f, args.corpora)
    stale = sorted(p for p, (_, _, r) in jobs.items() if r is None)
    if stale:
        print(f'{len(stale)} projects without a current expectation (run --tsc): {" ".join(stale[:5])}', file=sys.stderr)
    jobs = {p: j for p, j in jobs.items() if j[2] is not None}
    last = LAST if not args.f and not args.corpora else LAST.replace('.json', '-subset.json')
    if args.last:
        results = [r for r in json.load(open(last)) if r['project'] in jobs]
        elapsed = None
    else:
        start = time.time()

        def one(p):
            cwd, cmd, r = jobs[p]
            out, code = run_tttt(cwd, cmd, args.timeout, args.mem_gb)
            tsc_out, tsc_code = normalize(r['text']), r['status']
            if str(code).startswith('crash'):
                verdict = 'crash'
            else:
                verdict = compare_output(tsc_out, normalize(out), tsc_code, code)[0]
            row = {'project': p, 'verdict': verdict, 'tsc_code': tsc_code, 'code': code,
                   'lines': sum(1 for l in tsc_out.split('\n') if l)}
            if verdict != 'match':
                row['first'] = first_lines(tsc_out, out)
            if verdict == 'text':
                row['kinds'] = text_kinds(tsc_out, normalize(out))
            if verdict == 'crash':
                row['first'] = out[-400:]
            return row
        # (The corpora and the slow projects first: none ends up alone at the end)
        order = sorted(jobs, key=lambda p: (not p.startswith(CORPUS), -len(jobs[p][2].get('diags', []))))
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            results = sorted(pool.map(one, order), key=lambda r: r['project'])
        elapsed = time.time() - start
        with open(last, 'w') as f:
            json.dump(results, f)

    if args.names:
        print('\n'.join(r['project'] for r in results if r['verdict'] == args.names))
        return
    kinds = collections.defaultdict(lambda: {'diags': 0, 'projects': set(), 'example': None})
    for r in results:
        for k, x, y in r.get('kinds', []):
            k = k if args.k else re.sub(r':TS\d+ ', ': ', k) if k.startswith('header') else k
            kd = kinds[k]
            kd['diags'] += 1
            kd['projects'].add(r['project'])
            if kd['example'] is None:
                kd['example'] = (r['project'], x, y)
            if args.k and k.startswith(args.k):
                print(f'{r["project"]}  [{k}]\n    tsc:  ' + x.replace('\n', '\n          ')[:600] +
                      '\n    tttt: ' + y.replace('\n', '\n          ')[:600])
    if args.k:
        return
    counts = collections.Counter(r['verdict'] for r in results)
    for r in results:
        if r['verdict'] in ('match', 'text') or r['verdict'] == 'diagnostics' and not args.verbose:
            continue
        print(f'{r["verdict"]:11} {r["project"]}  (tsc {r["lines"]} lines, exit {r["tsc_code"]}; tttt exit {r["code"]})')
        print(r['first'])
    texts = [r for r in results if r['verdict'] == 'text']
    if texts:
        print(f'\n`text` mismatches ({len(texts)} projects), differing diagnostics by kind (diagnostics, projects, '
              f'first project):')
        for k, kd in sorted(kinds.items(), key=lambda kv: (-len(kv[1]['projects']), -kv[1]['diags']))[:args.n]:
            print(f'  {kd["diags"]:6} {len(kd["projects"]):4}  {k[:110]}\n{"":15}{kd["example"][0]}')
        if args.verbose:
            for r in texts:
                print(f'text        {r["project"]}\n{r["first"]}')
    with_output = sum(1 for r in results if r['verdict'] == 'match' and r['lines'])
    print(f'\nclidiff: {len(results)} projects: ' + ', '.join(f'{v} {counts[v]}' for v in
          ('match', 'text', 'order', 'exit code', 'diagnostics', 'crash')) +
          f' ({with_output} matching with diagnostics, {len(stale)} without expectation)' +
          (f', {elapsed:.0f}s' if elapsed is not None else ''))
    corpora = [r for r in results if r['project'].startswith(CORPUS)]
    print('corpora: ' + ', '.join(f'{r["project"][len(CORPUS):]} {r["verdict"]}' for r in corpora
                                  if r['verdict'] != 'match') + f' ({sum(r["verdict"] == "match" for r in corpora)} / '
          f'{len(corpora)} match)')


def live(args):
    if args.corpora:
        jobs = []
        for name, path in realworld.CORPORA.items():
            if name in realworld.BUILD_CORPORA or name == 'zod-root':
                continue
            real = os.path.realpath(os.path.join(ROOT, path))
            checkout = os.path.realpath(os.path.join(ROOT, *path.split('/')[:3]))
            jobs.append((name, os.path.dirname(real) if real.endswith('.json') else real, real, checkout))
    else:
        oracle = sourcesweep.load_oracle()
        projects = [p for p in sourcesweep.list_projects(args.f) if p in oracle and oracle[p]['ms'] <= args.max_ms]
        projects = sorted(projects)[::args.sample]
        jobs = [(p, os.path.realpath(SOURCES), p, os.path.realpath(os.path.join(SOURCES, p.split('/')[0]))) for p in projects]
    if args.emit:
        with concurrent.futures.ThreadPoolExecutor(16) as pool:
            keep = list(pool.map(lambda j: emits_declarations_only(j[1], j[2]), jobs))
        jobs = [j for j, k in zip(jobs, keep) if k]
        print(f'{len(jobs)} projects only emit declarations', flush=True)

    start = time.time()
    marker = f'/tmp/clidiff-{os.getpid()}.start'
    open(marker, 'w').close()
    results, counts = [], {}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        work = (lambda j: (j[0], *compare_emit(j[1], j[2], j[3], args))) if args.emit else \
            (lambda j: (j[0], *compare(j[1], j[2], args)))
        futures = [pool.submit(work, j) for j in jobs]
        for f in concurrent.futures.as_completed(futures):
            name, verdict, tsc_out, out, want_code, code = r = f.result()
            results.append(r)
            counts[verdict] = counts.get(verdict, 0) + 1
            if verdict == 'match':
                continue
            lines = sum(1 for l in (tsc_out or '').split('\n') if l)
            print(f'{verdict:11} {name}  (tsc {lines} lines, exit {want_code}; tttt exit {code})', flush=True)
            if (args.verbose or verdict != 'diagnostics') and tsc_out is not None and out is not None:
                print(first_lines(tsc_out, out), flush=True)
    # (tsc --noEmit still writes the build info of incremental projects)
    repos = sorted({os.path.join(SOURCES, j[2].split('/')[0]) for j in jobs if j[1] == SOURCES})
    if repos:
        subprocess.run(['find', *repos, '-name', 'node_modules', '-prune', '-o', '-name', '*tsbuildinfo*', '-newer',
                        marker, '-exec', 'rm', '-f', '{}', '+'])
    os.unlink(marker)
    with_output = sum(1 for r in results if r[1] == 'match' and r[2])
    print(f'{len(results)} projects: ' + ', '.join(f'{v} {n}' for v, n in sorted(counts.items())) +
          f' ({with_output} matching with diagnostics), {time.time() - start:.0f}s')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-f', help='regex on the project names (corpora: corpus:NAME)')
    ap.add_argument('--corpora', action='store_true', help='the corpora only')
    ap.add_argument('-k', help='list the `text` mismatches whose kind starts with this')
    ap.add_argument('--names', help='list the projects with this verdict')
    ap.add_argument('--last', action='store_true', help="reuse the last run's results")
    ap.add_argument('-n', type=int, default=30, help='rows per table')
    ap.add_argument('--tsc', action='store_true', help="regenerate tsc's stale cached output (-f: all its projects)")
    ap.add_argument('--live', action='store_true', help='against tsc run now rather than the caches')
    ap.add_argument('--sample', type=int, default=1, help='live: every Nth project')
    ap.add_argument('--pretty', action='store_true', help='live: the pretty format')
    ap.add_argument('--emit', action='store_true', help='live: emitDeclarationOnly projects, emitting (see above)')
    ap.add_argument('--max-ms', type=int, default=20000, help="live: skip projects tsc took longer for")
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--mem-gb', type=int, default=12)
    ap.add_argument('-j', '--jobs', type=int, default=4)
    ap.add_argument('-v', '--verbose', action='store_true')
    ap.add_argument('--no-build', action='store_true')
    args = ap.parse_args()
    if args.tsc:
        return regenerate(args)
    if not args.no_build and not args.last:
        subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
    if args.live or args.emit or args.pretty:
        return live(args)
    return cached(args)


if __name__ == '__main__':
    main()
