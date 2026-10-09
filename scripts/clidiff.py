#!/usr/bin/env python3
"""tttt as a drop-in tsc (plan 53 stage 1): `tttt -p P --pretty false` against `tsc -p P --noEmit --pretty false` on
real projects, byte for byte, exit codes included. Projects are the source sweep's (thirdparty/sources) and the -p
corpora; tsc is the pinned tsgo 7.1 (thirdparty/typescript/built/tsc).

  scripts/clidiff.py --sample 8              # every 8th source sweep project (by name), tsc's slow ones skipped
  scripts/clidiff.py -f '^vite/' -v          # print the first differing lines of each mismatch
  scripts/clidiff.py --corpora               # the -p corpora (all clean under tsc: tttt must print nothing)
  scripts/clidiff.py --pretty ...            # compare the pretty format (colours, code frames, summary) instead

Exit codes: tttt follows tsc 7 (1 with any diagnostic under --noEmit, 2 when the program has no file). Projects tsc
takes more than --max-ms for (the source sweep's cached
timing) are skipped. Verdicts: match, `exit code`, `order` (the same lines in another order), `text` (the same
diagnostics by file, line, column and code, other text) and `diagnostics` (a checker difference, the source sweep's
business: -v shows those too). tsc writes .tsbuildinfo files for incremental projects even with --noEmit: new ones are deleted."""
import argparse, concurrent.futures, gzip, json, os, re, subprocess, sys, time

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


def compare(cwd, project, args):
    pretty = ['--pretty', 'true' if args.pretty else 'false']
    tsc_out, tsc_code = run(TSC + ['-p', project, '--noEmit', *pretty], cwd, args.timeout)
    out, code = run([os.path.join(ROOT, 'release/tttt'), '-p', project, *pretty], cwd, args.timeout)
    if tsc_out is None or out is None:
        return 'timeout', tsc_out, out, tsc_code, code
    tsc_out, out = normalize(tsc_out), normalize(out)
    want_code = tsc_code
    if out == tsc_out:
        verdict = 'match' if code == want_code else 'exit code'
    elif sorted(out.split('\n')) == sorted(tsc_out.split('\n')):
        verdict = 'order'  # (the same lines in another order: the CLI's sorting)
    elif sorted(d[:4] for d in tscformat.parse(out)) == sorted(d[:4] for d in tscformat.parse(tsc_out)):
        verdict = 'text'  # (the same diagnostics, other messages or chains)
    else:
        verdict = 'diagnostics'  # (a checker difference: the source sweep's business)
    return verdict, tsc_out, out, want_code, code


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-f', help='regex on the source sweep project names')
    ap.add_argument('--sample', type=int, default=1, help='every Nth project')
    ap.add_argument('--corpora', action='store_true', help='the -p corpora instead of the source sweep')
    ap.add_argument('--pretty', action='store_true')
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
            jobs.append((name, os.path.dirname(real) if real.endswith('.json') else real, real))
    else:
        oracle = {r['project']: r for r in map(json.loads, gzip.open(sourcesweep.ORACLE, 'rt'))}
        projects = [p for p in sourcesweep.list_projects(args.f) if p in oracle and oracle[p]['ms'] <= args.max_ms]
        projects = sorted(projects)[::args.sample]
        jobs = [(p, SOURCES, p) for p in projects]

    start = time.time()
    marker = f'/tmp/clidiff-{os.getpid()}.start'
    open(marker, 'w').close()
    results, counts = [], {}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(lambda j: (j[0], *compare(j[1], j[2], args)), j) for j in jobs]
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
