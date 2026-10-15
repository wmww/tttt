#!/usr/bin/env python3
"""tttt as a drop-in tsc (plan 53 stage 1): `tttt -p P --pretty false` against `tsc -p P --noEmit --pretty false` on
real projects, byte for byte, exit codes included. Projects are the source sweep's (thirdparty/sources) and the -p
corpora; tsc is the pinned tsgo 7.1 (thirdparty/typescript/built/tsc).

  scripts/clidiff.py --sample 8              # every 8th source sweep project (by name), tsc's slow ones skipped
  scripts/clidiff.py -f '^vite/' -v          # print the first differing lines of each mismatch
  scripts/clidiff.py --corpora               # the -p corpora (all clean under tsc: tttt must print nothing)
  scripts/clidiff.py --pretty ...            # compare the pretty format (colours, code frames, summary) instead
  scripts/clidiff.py --emit                  # `tttt -p P` against `tsc -p P` as they are, emitting (plan 64 stage 4)

--emit: the projects whose config (tsgo --showConfig) only emits declarations (emitDeclarationOnly with declaration
or composite), where tttt is a drop-in `tsc`: both run in a bubblewrap sandbox with an overlay over the checkout
(writes land in a /tmp upper directory, the checkout is untouched), and stdout, the exit code and the written files
(.tsbuildinfo aside: tttt writes none) must be identical. Verdicts as above, then by the written files (--verbose lists
them): `declarationMap` (only tsgo's .d.ts.map files and their trailers differ, plan 64 stage 5), `missing files` (tsgo
wrote more: JavaScript sources' declarations, plan 64 stage 6), `file text` (dtsdiff.py's business).

Exit codes: tttt follows tsc 7 (1 with any diagnostic under --noEmit, 2 when the program has no file). Projects tsc
takes more than --max-ms for (the source sweep's cached
timing) are skipped. Verdicts: match, `exit code`, `order` (the same lines in another order), `text` (the same
diagnostics by file, line, column and code, other text) and `diagnostics` (a checker difference, the source sweep's
business: -v shows those too). tsc writes .tsbuildinfo files for incremental projects even with --noEmit: new ones are deleted."""
import argparse, concurrent.futures, gzip, json, os, re, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import realworld  # noqa: E402
import tscformat  # noqa: E402
import sourcesweep  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = os.path.join(ROOT, 'thirdparty/sources')
TSC = [os.path.join(ROOT, 'thirdparty/typescript/built/tsc')]


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
                if not f.endswith('.tsbuildinfo') and os.path.isfile(path) and not os.path.islink(path):
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
        # (tttt writes no declaration maps yet, plan 64 stage 5: without them and their trailers, do the rest match?)
        unmapped = {f: re.sub(rb'\n//# sourceMappingURL=[^\n]*$', b'\n', t) for f, t in tsc_files.items()
                    if not re.search(r'\.d\.[cm]?ts\.map$', f)}
        # (Files tsgo writes and tttt doesn't are JavaScript sources' mostly, plan 64 stage 6; other text is
        # dtsdiff.py's business)
        text = any(f in unmapped and unmapped[f] != t for f, t in files.items()) or any(f not in unmapped for f in files)
        verdict = 'declarationMap' if unmapped == files else 'file text' if text else 'missing files'
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-f', help='regex on the source sweep project names')
    ap.add_argument('--sample', type=int, default=1, help='every Nth project')
    ap.add_argument('--corpora', action='store_true', help='the -p corpora instead of the source sweep')
    ap.add_argument('--pretty', action='store_true')
    ap.add_argument('--emit', action='store_true', help='emitDeclarationOnly projects, emitting (see above)')
    ap.add_argument('--max-ms', type=int, default=20000, help="skip projects tsc took longer for (source sweep cache)")
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('-j', '--jobs', type=int, default=4)
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)

    if args.corpora:
        jobs = []
        for name, path in realworld.CORPORA.items():
            if name in realworld.BUILD_CORPORA or name == 'zod-root':
                continue
            real = os.path.realpath(os.path.join(ROOT, path))
            checkout = os.path.realpath(os.path.join(ROOT, *path.split('/')[:3]))
            jobs.append((name, os.path.dirname(real) if real.endswith('.json') else real, real, checkout))
    else:
        oracle = {r['project']: r for r in map(json.loads, gzip.open(sourcesweep.ORACLE, 'rt'))}
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
                a, b = tsc_out.split('\n'), out.split('\n')
                shown = 0
                for i in range(max(len(a), len(b))):
                    if i >= len(a) or i >= len(b) or a[i] != b[i]:
                        print(f'    tsc:  {a[i] if i < len(a) else "(end)"}'[:240])
                        print(f'    tttt: {b[i] if i < len(b) else "(end)"}'[:240], flush=True)
                        shown += 1
                        if shown == 3:
                            break
    # (tsc --noEmit still writes the build info of incremental projects)
    repos = sorted({os.path.join(SOURCES, j[2].split('/')[0]) for j in jobs if j[1] == SOURCES})
    if repos:
        subprocess.run(['find', *repos, '-name', 'node_modules', '-prune', '-o', '-name', '*.tsbuildinfo', '-newer',
                        marker, '-exec', 'rm', '-f', '{}', '+'])
    os.unlink(marker)
    with_output = sum(1 for r in results if r[1] == 'match' and r[2])
    print(f'{len(results)} projects: ' + ', '.join(f'{v} {n}' for v, n in sorted(counts.items())) +
          f' ({with_output} matching with diagnostics), {time.time() - start:.0f}s')


if __name__ == '__main__':
    main()
