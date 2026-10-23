#!/usr/bin/env python3
"""bun's differential check cases (test/buncases/differential-cases.json: github.com/oven-sh/bun test/cli/check at
5c6d4e24c, 2026-10-23, MIT): 2418 small projects (a tsconfig.json and a few files, each ending in a `sentinel` error)
on which bun's `bun check`, a TypeScript 7 reimplementation like tttt, once disagreed with tsc. Each case is written to
a scratch tree (/tmp/buncases-TREE-SIDE/ID/) and checked the way bun's runner (differential.ts) checks it: in the
case's directory, `tsc [-b] --pretty false --singleThreaded` (tsgo 7.1, cached in test/buncases/tsc.jsonl.gz keyed by
case id with the case's content hash; --tsc regenerates the stale or missing ones) against `tttt [-b] --pretty false
--no-write`, at tttt's defaults and at --checkers 1 and 4 (tsgo's output doesn't depend on its count: a difference is
a K-dependence bug, listed apart). The printed lines are compared whole with the scratch root stripped, as bun does
(`identical`), and as (file, line, code) multisets (`match`: the same diagnostics, other text, bucketed by the first
differing part), else spurious / missing / both, crash, timeout; mismatches are ranked by root cause across cases.

  scripts/buncases.py                  # every case (~1 minute)
  scripts/buncases.py -f '^01-'        # the cases whose id matches
  scripts/buncases.py -k TS2322        # list the cases and diagnostics whose root-cause key contains this
  scripts/buncases.py --names missing  # the ids of a verdict (identical, match, spurious, missing, both, crash,
                                       # timeout, kdep, skipped)
  scripts/buncases.py --show ID        # the case's files, tsc's and tttt's output
  scripts/buncases.py --last ...       # reuse the last run's results without running tttt again

A case whose tsconfig wants `types` none of its files provide (a node_modules/@types/X of its own) is skipped: it was
written for bun's tree. bun's `theirs` (what 7.1 reported where bun's installed tsc differed, 22 cases) isn't used:
the cache is the pinned tsgo's, and the headline says how many of those it agrees with.
"""
import argparse, collections, concurrent.futures, gzip, hashlib, json, os, re, resource, shutil, subprocess, sys, time
import tscformat, tttt_run
from sweep import tsc_jobs, shape, load_note

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CASES = os.path.join(ROOT, 'test/buncases/differential-cases.json')
ORACLE = os.path.join(ROOT, 'test/buncases/tsc.jsonl.gz')
TSC = os.path.join(ROOT, 'thirdparty/typescript/built/tsc')
TREE = os.path.basename(ROOT)
LAST = f'/tmp/buncases-{TREE}.json'
# tttt's runs: its defaults (what users run; on projects this small one checker), and fixed checker counts
TTTT_RUNS = {'default': [], 'k1': ['--checkers', '1'], 'k4': ['--checkers', '4']}


def case_hash(case):
    return hashlib.sha1(json.dumps(case, sort_keys=True).encode()).hexdigest()[:16]


def skip_reason(case):
    """Why a case can't be checked here, or None"""
    files = case['files']
    for path in files:
        if path.startswith('/') or '..' in path.split('/'):
            return f'path outside the case: {path}'
    m = re.search(r'"types"\s*:\s*\[([^\]]*)\]', files.get('tsconfig.json', ''))
    if m:
        for name in re.findall(r'"([^"]+)"', m.group(1)):
            mangled = name[1:].replace('/', '__') if name.startswith('@') else name
            if not any(p.startswith((f'node_modules/@types/{mangled}/', f'node_modules/{name}/')) or
                       re.search(rf'(^|/)node_modules/(@types/{re.escape(mangled)}|{re.escape(name)})/', p) for p in files):
                return f'types: {name} is not among its files'
    return None


def scratch(side):
    return os.path.realpath(f'/tmp/buncases-{TREE}-{side}')


def write_case(root, name, case):
    """The case's files under root/name (a fresh directory), each checked to stay inside it"""
    base = os.path.join(root, name)
    if os.path.lexists(base):
        shutil.rmtree(base)
    for path, text in case['files'].items():
        full = os.path.join(base, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        if not os.path.realpath(full).startswith(root + '/'):
            raise RuntimeError(f'{name}: {path} leaves the scratch tree')
        with open(full, 'w') as f:
            f.write(text)
    return base


def fresh_root(side):
    root = scratch(side)
    if not root.startswith('/tmp/buncases-'):
        raise RuntimeError(f'refusing to use {root}')
    if os.path.lexists(root):
        shutil.rmtree(root)
    os.makedirs(root)
    return root


def lines_of(cmd, cwd, root, timeout, crash_above, mem_gb=None):
    """bun's linesOf: the non-blank stdout lines with root stripped (case-insensitively), plus a line with the exit
    status after a crash"""
    # (tttt_run: mem_gb beyond the threads' reservations; killed and memory apart from crashes)
    r = tttt_run.run(cmd, cwd, timeout=timeout, mem_gb=mem_gb, env={**os.environ, 'NO_COLOR': '1'},
                     stderr=subprocess.PIPE, ok_codes=range(0, crash_above + 1))
    out, err, code, status = r.text(), (r.err or b'').decode(errors='replace'), r.code, r.status
    start = time.time() - r.wall
    prefix = re.compile(re.escape(root + '/'), re.I)
    lines = [prefix.sub('', l) for l in re.split(r'\r?\n', out) if l.strip()]
    ret = {'lines': lines, 'exit': code, 'status': status, 'ms': int((time.time() - start) * 1000)}
    # tttt's panics and log_errors (cleat's `file.c:N: message` on stderr): where a crash happened, and the checks
    # that should never fire
    err = re.sub(r'\x1b\[[0-9;]*m', '', err)
    sites = re.findall(r'(\S+\.c:\d+: [^\n]{0,80})', err)
    if sites:
        ret['sites'] = sites[:5]
    return ret


def run_tsc_case(name, case, root, timeout):
    base = write_case(root, name, case)
    r = lines_of([TSC, *(['-b'] if case.get('build') else []), '--pretty', 'false', '--singleThreaded'],
                 base, root, timeout, 5)
    shutil.rmtree(base)
    return {'id': name, 'hash': case_hash(case), **r}


def run_tttt_case(name, case, root, timeout, mem_gb):
    out = {'id': name}
    for run, flags in TTTT_RUNS.items():
        # A fresh copy each run: nothing one leaves behind reaches the next
        base = write_case(root, name, case)
        out[run] = lines_of([os.path.join(ROOT, 'release/tttt'), *(['-b'] if case.get('build') else []),
                             '--pretty', 'false', '--no-write', *flags], base, root, timeout, 5, mem_gb)
    shutil.rmtree(base)
    return out


def diags(lines):
    return [(f, l, c, m) for f, l, _, c, m in tscformat.parse('\n'.join(lines))]


def compare(want_lines, got_lines):
    """Spurious and missing (file, line, code, message) by multiset"""
    want, got = diags(want_lines), diags(got_lines)
    remaining = collections.Counter((f, l, c) for f, l, c, _ in want)
    spurious = []
    for d in got:
        if remaining[d[:3]] > 0:
            remaining[d[:3]] -= 1
        else:
            spurious.append(d)
    remaining = collections.Counter((f, l, c) for f, l, c, _ in got)
    missing = []
    for d in want:
        if remaining[d[:3]] > 0:
            remaining[d[:3]] -= 1
        else:
            missing.append(d)
    return spurious, missing


def text_cause(want_lines, got_lines):
    """The same diagnostics at (file, line, code), other text: the first differing part"""
    if sorted(want_lines) == sorted(got_lines):
        return 'order of lines'
    wb, gb = tscformat.blocks('\n'.join(want_lines)), tscformat.blocks('\n'.join(got_lines))
    if len(wb) != len(gb):
        return 'lines outside diagnostics'
    key = lambda b: tscformat.parse(b.split('\n')[0])[0]
    for w, g in zip(sorted(wb, key=lambda b: key(b)[:2] + key(b)[3:4]), sorted(gb, key=lambda b: key(b)[:2] + key(b)[3:4])):
        if w == g:
            continue
        (wf, wl, wc, wcode, wm), (gf, gl, gc, gcode, gm) = key(w), key(g)
        if wc != gc:
            return f'TS{wcode} column'
        if wm != gm:
            return f'TS{wcode} header {shape(wm)[:80]}'
        if w.split('\n')[0] != g.split('\n')[0]:
            return f'TS{wcode} header (category)'
        return f'TS{wcode} chain {shape(w.split(chr(10))[1].strip() if chr(10) in w else "")[:80]}'
    return 'order of diagnostics'


OWN_CASES = os.path.join(ROOT, 'test/cases')


def test_text(case):
    """The case as a suite test (test/cases/bunID.ts): its files as `// @filename:` units, the tsconfig.json first (the
    harness applies it like tsc's: options, roots), or why it can't be one"""
    if case.get('build'):
        return None, 'tsc -b'
    files = sorted(case['files'].items(), key=lambda kv: kv[0] != 'tsconfig.json')
    if any('\r' in t for _, t in files):
        return None, 'a carriage return'
    if any(re.search(r'(?m)^\s*//\s*@\w+\s*:', t) for _, t in files):
        return None, 'a line the harness reads as a directive'
    out = []
    for path, text in files:
        out.append(f'// @filename: {path}\n' + text + ('' if text.endswith('\n') or not text else '\n'))
    return ''.join(out), None


# Cases tsgo's suite runner can't run as tests (2026-10-23: it hung on one of them, wrote no baselines for the rest;
# Windows paths like `C:/x.d.ts`, quotes in baseUrl, references to files outside the test)
HARNESS_UNFIT = {'35-C7', '37-C15', '37-C21a', '39-C21a', '39-C21b', '42-P3b', '42-P6c', '42-P8'}
# A test whose tsgo .types and .symbols would take more than this is compared on its errors alone (@noTypesAndSymbols):
# a few deep cases make tens of megabytes of them
TYPES_LIMIT = 50_000
OWN_REF = os.path.join(ROOT, 'test/baselines')


# A case tsgo's CLI took longer than this on (the cache's `ms`, a loaded box) stays out of the suite: the 18 past it
# (templates of thousands of literals, deep nesting) would take seconds of every run
SLOW_MS = 1000


def write_tests(cases, names, oracle):
    """Each case a suite test; returns the ones that can't be, with the reason. A test keeps @noTypesAndSymbols when
    its file had it, and gets it when its current baselines are past TYPES_LIMIT"""
    unfit = {}
    for n in names:
        text, why = test_text(cases[n])
        if not why and n in HARNESS_UNFIT:
            why = "tsgo's suite runner can't run it"
        if not why and oracle.get(n, {}).get('ms', 0) > SLOW_MS:
            why = f'slower than {SLOW_MS} ms under tsgo'
        if why:
            unfit[n] = why
            continue
        path = os.path.join(OWN_CASES, f'bun{n}.ts')
        quiet = os.path.exists(path) and open(path).read().startswith('// @noTypesAndSymbols: true')
        quiet |= sum(os.path.getsize(os.path.join(OWN_REF, f'bun{n}{ext}')) for ext in ('.types', '.symbols')
                     if os.path.exists(os.path.join(OWN_REF, f'bun{n}{ext}'))) > TYPES_LIMIT
        with open(path, 'w') as f:
            f.write(('// @noTypesAndSymbols: true\n' if quiet else '') + text)
    return unfit


def root_cause(case, diag, spurious):
    """The key a mismatching diagnostic is ranked under: its sign, code and message shape (quoted parts elided)"""
    return f'{"+" if spurious else "-"}TS{diag[2]} {shape(diag[3])}'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-f', '--filter', help='regex the case id must match (re.search)')
    ap.add_argument('-j', '--jobs', type=int, default=os.cpu_count())
    ap.add_argument('-n', type=int, default=30, help='rows per table')
    ap.add_argument('-k', '--key', help='list the diagnostics whose root-cause key contains this')
    ap.add_argument('--names', help='list the case ids with this verdict')
    ap.add_argument('--show', help="print a case's files and both outputs")
    ap.add_argument('--last', action='store_true', help="reuse the last run's results")
    ap.add_argument('--tsc', action='store_true', help="regenerate tsc's cached outputs that are stale or missing")
    ap.add_argument('--tsc-all', action='store_true', help="with --tsc, regenerate every selected case's")
    ap.add_argument('--timeout', type=int, default=60)
    ap.add_argument('--mem-gb', type=int, default=64, help="tttt's address space limit (it reserves arenas per thread: 4 GB fails at the default threads)")
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--write-tests', action='store_true',
                    help='write the (selected) cases as suite tests test/cases/bunID.ts (then scripts/owntests.sh them)')
    args = ap.parse_args()

    cases = json.load(open(CASES))
    names = [n for n in cases if not args.filter or re.search(args.filter, n)]
    oracle = {}
    if os.path.exists(ORACLE):
        oracle = {r['id']: r for r in map(json.loads, gzip.open(ORACLE, 'rt'))}
    skipped = {n: skip_reason(cases[n]) for n in names}
    skipped = {n: r for n, r in skipped.items() if r}
    names = [n for n in names if n not in skipped]

    if args.write_tests:
        unfit = write_tests(cases, names, oracle)
        print(f'{len(names) - len(unfit)} tests written to test/cases; not as tests: ' +
              ', '.join(f'{why} {k}' for why, k in collections.Counter(unfit.values()).most_common()))
        return

    if args.tsc:
        subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')], check=True)
        todo = [n for n in names if args.tsc_all or n not in oracle or oracle[n]['hash'] != case_hash(cases[n])]
        root = fresh_root('tsc')
        start = time.time()
        with concurrent.futures.ThreadPoolExecutor(tsc_jobs(args.jobs, 1)) as pool:
            for r in pool.map(lambda n: run_tsc_case(n, cases[n], root, args.timeout * 3), todo):
                oracle[r['id']] = r
        shutil.rmtree(root)
        with gzip.open(ORACLE, 'wt') as f:
            for n in sorted(oracle):
                if n in cases:
                    f.write(json.dumps(oracle[n], separators=(',', ':')) + '\n')
        print(f'tsc on {len(todo)} cases in {time.time() - start:.0f}s, written to {ORACLE}')
        return

    if args.show:
        n = args.show
        for path, text in cases[n]['files'].items():
            print(f'=== {path}\n{text.rstrip()}' if len(text) < 4000 else f'=== {path} ({len(text)} bytes)\n{text[:2000]}...')
        print('=== tsc' + (' (-b)' if cases[n].get('build') else '') + f' exit {oracle.get(n, {}).get("exit")}')
        print('\n'.join(oracle.get(n, {}).get('lines', ['(not cached)'])))
        if 'theirs' in cases[n]:
            print("=== bun's theirs\n" + '\n'.join(cases[n]['theirs']))
        r = run_tttt_case(n, cases[n], fresh_root('tttt'), args.timeout, args.mem_gb)
        shutil.rmtree(scratch('tttt'))
        for run in TTTT_RUNS:
            print(f'=== tttt {run} ({r[run]["status"]}, exit {r[run]["exit"]})')
            print('\n'.join(r[run]['lines']))
        return

    stale = [n for n in names if n not in oracle or oracle[n]['hash'] != case_hash(cases[n])]
    if stale:
        print(f'{len(stale)} cases have no current tsc output (run --tsc): {" ".join(stale[:5])}...')
    names = [n for n in names if n not in stale]
    tsc_crash = [n for n in names if oracle[n]['status'] != 'ok']
    names = [n for n in names if n not in tsc_crash]

    if args.last:
        results = {r['id']: r for r in json.load(open(LAST))}
        names = [n for n in names if n in results]
    else:
        if not args.no_build:
            subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
        root = fresh_root('tttt')
        start = time.time()
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            results = {r['id']: r for r in pool.map(
                lambda n: run_tttt_case(n, cases[n], root, args.timeout, args.mem_gb), names)}
        shutil.rmtree(root)
        elapsed = time.time() - start
        with open(LAST, 'w') as f:
            json.dump(list(results.values()), f)

    verdicts = collections.defaultdict(list)
    causes = collections.defaultdict(lambda: {'cases': set(), 'diags': 0, 'example': None})
    texts = collections.defaultdict(list)
    only = collections.Counter()
    kdep = []
    for n in names:
        want, r = oracle[n]['lines'], results[n]
        got = r['default']['lines']
        runs = {run: r[run]['lines'] + ([r[run]['status']] if r[run]['status'] != 'ok' else []) for run in TTTT_RUNS}
        if len({tuple(v) for v in runs.values()}) > 1 and not any(tttt_run.resource_status(r[run]['status']) for run in TTTT_RUNS):
            kdep.append(n)
            verdicts['kdep'].append(n)
        if r['default']['status'] != 'ok':
            s = r['default']['status']
            verdicts[s if s == 'timeout' else 'killed' if tttt_run.resource_status(s) else 'crash'].append(n)
            continue
        if got == want:
            verdicts['identical'].append(n)
            continue
        spurious, missing = compare(want, got)
        if not spurious and not missing:
            verdicts['match'].append(n)
            k = text_cause(want, got)
            texts[k].append(n)
            if args.key and args.key in k:
                print(f'{n}  text: {k}')
            continue
        verdicts['both' if spurious and missing else 'spurious' if spurious else 'missing'].append(n)
        keys = set()
        for d, sp in [(d, True) for d in spurious] + [(d, False) for d in missing]:
            k = root_cause(n, d, sp)
            keys.add(k)
            c = causes[k]
            c['cases'].add(n)
            c['diags'] += 1
            if c['example'] is None:
                c['example'] = f'{n}  {d[0]}:{d[1]}'
            if args.key and args.key in k:
                print(f'{n}  {d[0]}:{d[1]} {k[0]}TS{d[2]} {d[3][:200]}')
        if len(keys) == 1:
            only[keys.pop()] += 1
    if args.key:
        return
    if args.names:
        print('\n'.join(skipped if args.names == 'skipped' else tsc_crash if args.names == 'tsc-crash'
                        else verdicts.get(args.names, [])))
        return

    total = len(names)
    same = len(verdicts['identical']) + len(verdicts['match'])
    print(f'{total} cases ({len(skipped)} skipped, {len(tsc_crash)} tsc crashes, {len(stale)} uncached): '
          f'(file, line, code) match {same} ({100 * same / max(total, 1):.1f}%), of them byte-identical '
          f'{len(verdicts["identical"])}; ' +
          ', '.join(f'{k} {len(verdicts[k])}' for k in ('spurious', 'missing', 'both', 'crash', 'timeout')) +
          (f', killed {len(verdicts["killed"])} (memory or SIGKILL)' if verdicts['killed'] else '') +
          f'; K-dependent {len(kdep)}')
    theirs = [n for n in names if 'theirs' in cases[n]]
    if theirs:
        agree = sum(1 for n in theirs if oracle[n]['lines'] == cases[n]['theirs'])
        print(f"tsc agrees with bun's recorded 7.1 output on {agree} of {len(theirs)} cases that carry one")
    if not args.last:
        print(f'tttt {elapsed:.0f}s wall for {total} cases x {len(TTTT_RUNS)} runs; {load_note()}')
    sites = collections.defaultdict(list)
    for v in ('crash', 'timeout', 'killed'):
        for n in verdicts[v]:
            d = results[n]['default']
            sites[f'{v} ' + (d.get('sites') or [d['status']])[-1]].append(n)
    logged = collections.defaultdict(list)
    for n in names:
        if results[n]['default']['status'] == 'ok':
            for site in {shape(s) for s in results[n]['default'].get('sites', [])}:
                logged[site].append(n)
    for title, table in (('crashes and timeouts by site', sites), ('log_error and other stderr sites', logged)):
        if table:
            print(f'\n{title}:')
            for k, ns in sorted(table.items(), key=lambda kv: -len(kv[1]))[:args.n]:
                print(f'  {len(ns):5}  {k[:110]}\n{"":9}{" ".join(ns[:6])}')
    if kdep:
        print('\nK-dependent (tttt default / --checkers 1 / --checkers 4 differ): ' + ' '.join(kdep[:args.n]))
    print(f'\nroot causes by cases (+ spurious, - missing; "only": cases it is the only mismatch of):')
    print(f'  {"cases":>5} {"only":>5} {"diags":>6}  key / example')
    for k, c in sorted(causes.items(), key=lambda kc: (-len(kc[1]['cases']), -kc[1]['diags']))[:args.n]:
        print(f'  {len(c["cases"]):5} {only[k]:5} {c["diags"]:6}  {k[:110]}\n{"":21}{c["example"][:130]}')
    if texts:
        print(f'\ntext-only differences (same (file, line, code)), by first differing part:')
        for k, ns in sorted(texts.items(), key=lambda kv: -len(kv[1]))[:args.n]:
            print(f'  {len(ns):5}  {k[:110]}\n{"":9}{" ".join(ns[:4])}')
    print(f'\nresults saved for --last in {LAST}')


if __name__ == '__main__':
    sys.exit(main())
