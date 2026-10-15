#!/usr/bin/env python3
"""Flag flips (plan 46 stage 1): the pinned corpora are clean under tsc with their own flags, so they can only show
spurious diagnostics. Rechecked with a stricter (or looser) flag on the command line they turn into thousands of real
tsc diagnostics on real code, which tttt must report too. This runs every corpus x flag cell through tttt and compares
(file, line, code) multisets with tsc's, which is cached under test/flagflip/CORPUS/FLAG.json.gz keyed by the corpus
pin (generate with --tsc: minutes per cell on the big corpora). Whatever tttt reports on the corpus at its default
flags is subtracted (effect's known spurious ones). Mismatches are ranked by root cause across cells: code and message
shape, and a missing code that tttt reports as another code on the same line as "TSa->TSb".

  scripts/flagflip.py                    # every cached cell
  scripts/flagflip.py -c vscode -F nuia  # cells whose corpus / flag key match (re.fullmatch, comma separated alternatives)
  scripts/flagflip.py --tsc -c 'tsc|zod' # generate tsc's cache for these cells (skips cached ones unless --force)
  scripts/flagflip.py -k TS2412          # list the diagnostics whose root-cause key contains this
  scripts/flagflip.py --cells            # per cell: tsc's count, missing, spurious
  scripts/flagflip.py --last ...         # reuse the last run's tttt output
"""
import argparse, collections, concurrent.futures, gzip, json, os, re, resource, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from realworld import CORPORA, BUILD_CORPORA, pin  # noqa: E402
from sweep import lib_key  # noqa: E402
import tscformat  # noqa: E402

CACHE = os.path.join(ROOT, 'test/flagflip')
TREE = os.path.basename(ROOT)
LAST = f'/tmp/flagflip-{TREE}.json'
# tsc -b doesn't take compiler options, and zod-root isn't clean under tsc: the rest. tsc-src and typescript-eslint
# are covered by tsc's compiler and the other corpora
FLIP_CORPORA = [c for c in CORPORA if c not in BUILD_CORPORA and c != 'zod-root']
# key -> command line flags. A flag the corpus' tsconfig already sets is a no-op there (tsc's output is the default's)
FLAGS = {
    'nuia': ['--noUncheckedIndexedAccess'],
    'eopt': ['--exactOptionalPropertyTypes'],
    'npafis': ['--noPropertyAccessFromIndexSignature'],
    'nio': ['--noImplicitOverride'],
    'nir': ['--noImplicitReturns'],
    'nfcis': ['--noFallthroughCasesInSwitch'],
    'unused': ['--noUnusedLocals', '--noUnusedParameters'],
    # The non-strict modes no corpus runs: the strict family off, each spelled out (a tsconfig's explicit
    # strictNullChecks would survive --strict false), and the options that need strictNullChecks (TS5052, which stops
    # tsc before checking). Not alwaysStrict: 7.0 removed `false` (TS5108, which stops tsc too)
    'sloppy': ['--exactOptionalPropertyTypes', 'false', '--strict', 'false', '--strictNullChecks', 'false', '--noImplicitAny', 'false', '--strictFunctionTypes',
               'false', '--strictBindCallApply', 'false', '--strictPropertyInitialization', 'false', '--noImplicitThis',
               'false', '--useUnknownInCatchVariables', 'false', '--strictBuiltinIteratorReturn', 'false'],
    'nosnc': ['--strictNullChecks', 'false', '--exactOptionalPropertyTypes', 'false', '--strictPropertyInitialization',
              'false'],
    'nonia': ['--noImplicitAny', 'false'],
    'nosft': ['--strictFunctionTypes', 'false'],
    # (No es5 cell: 7.0 removed the target)
    'es2015': ['--target', 'es2015'],
}
TSC_DIAG = re.compile(r'(.*)\((\d+),\d+\): error TS(\d+): (.*)')


def shape(message):
    """A message with its quoted parts elided (not the apostrophe of "can't")"""
    return re.sub(r"(?<!\w)'[^']*'(?!\w)", "'…'", re.sub(r'"[^"]*"', '"…"', message))[:120]


def corpus_cwd(corpus):
    """The directory to run from and the project argument, like realworld.py: through the real path, so a corpus behind
    a worktree's symlink doesn't load its files twice"""
    cwd = ROOT
    real = os.path.realpath(os.path.join(ROOT, CORPORA[corpus]))
    parts = CORPORA[corpus].split('/')
    for i in range(1, len(parts) + 1):
        prefix = '/'.join(parts[:i])
        if os.path.islink(os.path.join(ROOT, prefix)):
            target = os.path.realpath(os.path.join(ROOT, prefix))
            if target.endswith('/' + prefix):
                cwd = target[:-len(prefix) - 1]
            break
    return cwd, os.path.relpath(real, cwd)


def cache_path(corpus, flag):
    return os.path.join(CACHE, corpus, flag + '.json.gz')


def run_tsc(corpus, flag):
    cwd, project = corpus_cwd(corpus)
    start = time.time()
    # (tsgo 7.1's CLI, single-threaded: the cells run several at once)
    tsc = [os.path.join(ROOT, 'thirdparty/typescript/built/tsc'), '--singleThreaded', '-p', project]
    # An incremental (or composite) project writes its .tsbuildinfo even with --noEmit, and the next cell would reuse
    # its diagnostics (tsc's es2015 cell got es5's TS2802s): each run gets a build info file of its own
    config = json.loads(subprocess.run([*tsc, '--showConfig'], cwd=cwd, capture_output=True, text=True).stdout or '{}')
    options = config.get('compilerOptions', {})
    with tempfile.TemporaryDirectory() as tmp:
        extra = ['--tsBuildInfoFile', os.path.join(tmp, 'tsbuildinfo')] if options.get('incremental') or options.get('composite') else []
        proc = subprocess.run([*tsc, '--noEmit', '--pretty', 'false', *extra, *FLAGS[flag]], cwd=cwd,
                              capture_output=True, text=True)
    diags = []
    for line in proc.stdout.split('\n'):
        m = TSC_DIAG.match(line)
        if m:
            diags.append([m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)[:200]])
        elif line.startswith('error TS'):
            m = re.match(r'error TS(\d+): (.*)', line)
            diags.append(['', 0, int(m.group(1)), m.group(2)[:200]])
    if proc.returncode not in (0, 1, 2) or 'FATAL' in proc.stderr:
        print(f'tsc failed on {corpus} {flag}: exit {proc.returncode}\n{proc.stderr[-500:]}')
        return None
    result = {'pin': pin(corpus), 'flags': FLAGS[flag], 'ms': int((time.time() - start) * 1000), 'diags': diags}
    os.makedirs(os.path.dirname(cache_path(corpus, flag)), exist_ok=True)
    with gzip.open(cache_path(corpus, flag), 'wt') as f:
        json.dump(result, f, separators=(',', ':'))
        f.write('\n')
    print(f'tsc {corpus:10} {flag:8} {len(diags):6} diagnostics in {result["ms"] / 1000:.0f}s', flush=True)
    return result


def run_tttt(corpus, flags, timeout=600, mem_gb=12):
    cwd, project = corpus_cwd(corpus)
    limit = mem_gb << 30
    try:
        proc = subprocess.run([os.path.join(ROOT, 'release/tttt'), '--pretty', 'false', '--noEmit', '-p', project,
                               *flags], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
                              preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
        out, code = proc.stdout, proc.returncode
        status = 'ok' if code in (0, 1, 2) else f'exit {code}' if code > 0 else f'signal {-code}'
    except subprocess.TimeoutExpired as e:
        out, status = e.stdout or b'', 'timeout'
    text = re.sub(r'\x1b\[[0-9;]*m', '', out.decode(errors='replace'))
    # (Located ones only, like tsc's side)
    diags = [[f, l, c, m[:200]] for f, l, _, c, m in tscformat.parse(text) if f]
    if status == 'ok' and ('panic' in text.lower() or re.search(r'\.c:\d+: error:', text)):
        status = 'log_error'
    tail = '\n'.join(l for l in text.split('\n') if l.strip())[-600:] if status != 'ok' else ''
    return {'status': status, 'diags': diags, 'tail': tail}


def key3(d):
    return (*lib_key(d[0], d[1]), d[2])


def compare(tsc, tttt, known):
    """(spurious, missing, wrong code) of one cell, by (file, line, code) multiset; tttt's diagnostics at the
    corpus' default flags (`known`) don't count as spurious"""
    want = collections.Counter(key3(d) for d in tsc)
    have = collections.Counter(key3(d) for d in tttt)
    known = collections.Counter(key3(d) for d in known)
    spurious = []
    remaining = want.copy()
    for d in tttt:
        k = key3(d)
        if remaining[k] > 0:
            remaining[k] -= 1
        elif known[k] > 0:
            known[k] -= 1
        else:
            spurious.append(d)
    messages = {key3(d): d[3] for d in tsc}
    missing = [[*k, messages[k]] for k, n in (want - have).items() for _ in range(n)]
    # A missing code with a spurious one on the same line: reported under another code
    by_line = collections.defaultdict(list)
    for d in spurious:
        by_line[(d[0], d[1])].append(d)
    wrong, rest = [], []
    for d in missing:
        other = by_line.get((d[0], d[1]))
        if other:
            o = other.pop()
            wrong.append((d, o))
            spurious.remove(o)
        else:
            rest.append(d)
    return spurious, rest, wrong


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-c', '--corpus', default='.*', help='corpus regex (fullmatch)')
    ap.add_argument('-F', '--flag', default='.*', help='flag key regex (fullmatch): ' + ' '.join(FLAGS))
    ap.add_argument('-j', '--jobs', type=int, default=4)
    ap.add_argument('-n', type=int, default=40, help='rows per table')
    ap.add_argument('-k', '--key', help='list the diagnostics whose root-cause key contains this')
    ap.add_argument('--cells', action='store_true', help='per cell counts')
    ap.add_argument('--tsc', action='store_true', help="generate tsc's cache for the selected cells")
    ap.add_argument('--force', action='store_true', help='with --tsc: regenerate cached cells too')
    ap.add_argument('--last', action='store_true', help="reuse the last run's tttt output")
    ap.add_argument('--no-build', action='store_true')
    args = ap.parse_args()

    corpora = [c for c in FLIP_CORPORA if re.fullmatch(args.corpus, c)]
    flags = [f for f in FLAGS if re.fullmatch(args.flag, f)]
    if any(c != 'tsc' for c in corpora):
        subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-corpora.sh'), *(CORPORA[c].split('/')[2] for c in corpora)], check=True)
    cells = [(c, f) for c in corpora for f in flags]

    if args.tsc:
        pins = {c: pin(c) for c in corpora}
        todo = []
        for c, f in cells:
            p = cache_path(c, f)
            if args.force or not os.path.exists(p) or json.load(gzip.open(p, 'rt'))['pin'] != pins[c]:
                todo.append((c, f))
        # The big ones first, so the pool's tail is short
        order = ['vscode', 'effect', 'tsc', 'vue', 'webpack', 'drizzle', 'zod', 'xstate']
        todo.sort(key=lambda cf: order.index(cf[0]) if cf[0] in order else len(order))
        start = time.time()
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            list(pool.map(lambda cf: run_tsc(*cf), todo))
        print(f'{len(todo)} cells in {time.time() - start:.0f}s')
        return

    oracle = {}
    for c, f in cells:
        p = cache_path(c, f)
        if os.path.exists(p):
            oracle[(c, f)] = json.load(gzip.open(p, 'rt'))
    stale = [cf for cf in oracle if oracle[cf]['pin'] != pin(cf[0])]
    for cf in stale:
        print(f'stale tsc cache for {cf[0]} {cf[1]} (the corpus pin moved): run --tsc')
        del oracle[cf]
    cells = [cf for cf in cells if cf in oracle]
    if not cells:
        print('no cached cells selected (run --tsc)')
        return

    if args.last:
        last = json.load(open(LAST))
        results = {tuple(k.split(' ')): v for k, v in last.items()}
    else:
        if not args.no_build:
            subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True,
                           stdout=subprocess.DEVNULL)
        runs = sorted({(c, 'default') for c, _ in cells} | set(cells))
        # tttt runs 4 threads itself: two at a time
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            outs = pool.map(lambda cf: run_tttt(cf[0], [] if cf[1] == 'default' else FLAGS[cf[1]]), runs)
            results = dict(zip(runs, outs))
        with open(LAST, 'w') as f:
            json.dump({' '.join(k): v for k, v in results.items()}, f)

    causes = collections.defaultdict(lambda: {'cells': set(), 'diags': 0, 'example': None})
    rows = []
    for c, f in cells:
        r, base = results.get((c, f)), results.get((c, 'default'))
        if r is None or base is None:
            continue
        tsc = oracle[(c, f)]['diags']
        if r['status'] != 'ok':
            print(f'{r["status"].upper()} {c} {f}:\n    ' + r['tail'].replace('\n', '\n    ')[-400:])
        spurious, missing, wrong = compare(tsc, r['diags'], base['diags'])
        rows.append((c, f, len(tsc), len(missing), len(spurious), len(wrong), r['status']))
        items = [(f'+TS{d[2]} {shape(d[3])}', d) for d in spurious] + \
                [(f'-TS{d[2]} {shape(d[3])}', d) for d in missing] + \
                [(f'TS{d[2]}->TS{o[2]}', d) for d, o in wrong]
        for k, d in items:
            cause = causes[k]
            cause['cells'].add(f'{c}/{f}')
            cause['diags'] += 1
            if cause['example'] is None:
                cause['example'] = f'{c}/{f} {d[0]}:{d[1]}'
            if args.key and args.key in k:
                print(f'{c}/{f}  {d[0]}:{d[1]} {k[:40]}  {d[3][:160]}')
    if args.key:
        return

    if args.cells:
        print(f'{"corpus":10} {"flag":8} {"tsc":>6} {"miss":>6} {"spur":>6} {"code":>6}')
        for c, f, n, m, s, w, st in rows:
            print(f'{c:10} {f:8} {n:6} {m:6} {s:6} {w:6}' + ('' if st == 'ok' else '  ' + st))
    tot = [sum(r[i] for r in rows) for i in (2, 3, 4, 5)]
    print(f'{len(rows)} cells: tsc {tot[0]} diagnostics, tttt missing {tot[1]}, spurious {tot[2]}, other code {tot[3]}')
    print(f'\nroot causes (+ spurious, - missing, a->b: tsc reports a where tttt reports b):')
    print(f'  {"cells":>5} {"diags":>6}  key / example')
    for k, cause in sorted(causes.items(), key=lambda kc: (-kc[1]['diags'], -len(kc[1]['cells'])))[:args.n]:
        print(f'  {len(cause["cells"]):5} {cause["diags"]:6}  {k[:110]}\n{"":16}{cause["example"][:130]}')


if __name__ == '__main__':
    sys.exit(main())
