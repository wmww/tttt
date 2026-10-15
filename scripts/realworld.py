#!/usr/bin/env python3
"""Runs tttt on a real project (tsc's own compiler sources by default, ~146k lines, no errors under tsc) and
summarises what it reports: exit status, wall time, peak RSS, diagnostics by code, and syntax errors grouped by
the token they point at. tsc reports nothing for this corpus, so every diagnostic is a tttt bug.

  scripts/realworld.py                 # tttt -p thirdparty/corpora/TypeScript6/src/compiler (its tsconfig.json)
  scripts/realworld.py path/to/entry.ts --options '--strict' -n 40
  scripts/realworld.py --code TS1005   # list the places a code is reported
  scripts/realworld.py --corpus valibot   # a second-oracle corpus from thirdparty/ensure-corpora.sh (type-fest, valibot, react, nest, webpack, zod, zod-root, xstate, drizzle, vue, effect, vscode, excalidraw, tanstack-router; tsc-src and typescript-eslint: -b on a solution config)
"""
import argparse, collections, json, os, re, resource, subprocess, sys, time
import sweep, tscformat

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The corpora besides tsc's compiler (plan 17): modern, type-heavy code, clean under tsc 5.8 as pinned (plan 48 stage 5 re-pins them for 6.0), fetched by
# thirdparty/ensure-corpora.sh
CORPORA = {
    'tsc': 'thirdparty/corpora/TypeScript6/src/compiler',
    # TypeScript's whole src: a solution config of 13 composite projects, checked with -b (plan 41: references read as
    # sources). Clean, like tsc
    'tsc-src': 'thirdparty/corpora/TypeScript6/src',
    'type-fest': 'thirdparty/corpora/type-fest',
    'valibot': 'thirdparty/corpora/valibot/library/tsconfig.corpus.json',
    'react': 'thirdparty/corpora/bulletproof-react/apps/react-vite/tsconfig.corpus.json',
    'nest': 'thirdparty/corpora/nestjs-boilerplate/tsconfig.corpus.json',
    'webpack': 'thirdparty/corpora/webpack',
    'zod': 'thirdparty/corpora/zod/packages/zod/tsconfig.test.json',
    # zod's root tsconfig: every package (docs excluded), nodenext with customConditions; not clean under tsc either
    # (53 at 0b216ef: missing bench/fixture deps, unused locals), tttt must match tsc's list (plan 19)
    'zod-root': 'thirdparty/corpora/zod',
    # xstate's core package and its type tests: generic call inference with callbacks (plan 38)
    'xstate': 'thirdparty/corpora/xstate/packages/core/tsconfig.corpus.json',
    # drizzle-orm and its type tests: SQL builders through deep conditional/mapped chains, noUncheckedIndexedAccess
    'drizzle': 'thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json',
    # vue core's packages and dts-test: component option overloads, props inference, JSX
    'vue': 'thirdparty/corpora/vue/tsconfig.corpus.json',
    # Effect v3's core package, its tests and tstyche type tests: HKTs, generators, dual/pipe overloads, Schema
    'effect': 'thirdparty/corpora/effect/packages/effect/tsconfig.corpus.json',
    # VS Code's src: the large application (3.41M lines), decorator-based DI, browser/node/electron in one program
    'vscode': 'thirdparty/corpora/vscode/src/tsconfig.corpus.json',
    # excalidraw: the JSX-heavy React 19 application (whiteboard editor, its packages and tests)
    'excalidraw': 'thirdparty/corpora/excalidraw/tsconfig.corpus.json',
    # TanStack Router's react-router and its type tests: route-tree inference, template-literal path unions, Register
    'tanstack-router': 'thirdparty/corpora/tanstack-router/tsconfig.corpus.json',
    # typescript-eslint's root solution config, checked with -b (plan 41 stage 3): 38 composite projects, workspace
    # packages importing each other through node_modules symlinks into dist/*.d.ts, `${configDir}`, nested solutions
    'typescript-eslint': 'thirdparty/corpora/typescript-eslint',
}
# The corpora checked with -b (a solution config of project references) rather than -p
BUILD_CORPORA = ('tsc-src', 'typescript-eslint')

# tsc's (tsgo 7.1's) diagnostics on each corpus, what tttt must report there: one JSON line per corpus (--tsc writes it)
TSC_CACHE = os.path.join(ROOT, 'test/corpora.tsc.jsonl')
TSGO = os.path.join(ROOT, 'thirdparty/typescript/built/tsc')


def pin(corpus):
    """What a cache of tsc's diagnostics on the corpus is valid for: its checkout's commit and the oracle's
    (thirdparty/ensure-typescript.sh)"""
    d = os.path.join(ROOT, CORPORA[corpus])
    while not os.path.exists(os.path.join(d, '.git')):
        d = os.path.dirname(d)
    commit = subprocess.run(['git', '-C', d, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    oracle = re.search(r'^TS_COMMIT=(\w+)', open(os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')).read(), re.M)[1]
    return f'{commit} tsgo {oracle[:8]}'


def run_tsc(corpus, cwd, options):
    """tsc (tsgo) on the corpus as tttt checks it, from the same directory. `-b` emits (a referenced project may not
    disable it): its outputs, whatever the build wrote under the checkout after it started, are deleted after"""
    subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')], check=True)
    build = options.startswith('-b ')
    checkout = os.path.join(ROOT, *CORPORA[corpus].split('/')[:3])
    marker = time.time()
    time.sleep(0.01)
    # (Bounded: tsgo 7.0.2 ran away on zod-root, > 18 GB)
    limit = 20 << 30
    try:
        p = subprocess.run([TSGO, *options.split(), '--pretty', 'false', *([] if build else ['--noEmit'])], cwd=cwd,
                           capture_output=True, text=True, timeout=1800,
                           preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
        out, status = p.stdout, p.returncode
    except subprocess.TimeoutExpired as e:
        out, status = (e.stdout or b'').decode(errors='replace'), 'timeout'
    if status not in (0, 1, 2):
        print(f'tsc on {corpus}: exit {status}: {out[-500:]}')
    if build:
        for d, dirs, files in os.walk(checkout, topdown=False):
            if '/node_modules' in d:
                continue
            for f in files:
                path = os.path.join(d, f)
                if not os.path.islink(path) and os.path.getmtime(path) > marker:
                    os.remove(path)
            if os.path.getmtime(d) > marker and not os.listdir(d) and os.path.realpath(d) != os.path.realpath(checkout):
                os.rmdir(d)
    diags = [[f, l, col, c] for f, l, col, c, _ in tscformat.parse(out)]
    return {'corpus': corpus, 'pin': pin(corpus), 'status': status, 'diags': diags}


def load_tsc_cache():
    if not os.path.exists(TSC_CACHE):
        return {}
    return {r['corpus']: r for r in map(json.loads, open(TSC_CACHE))}


KEYWORDS = set('''else in of as is asserts new function catch export import type default case satisfies this void
typeof instanceof get set static abstract declare namespace let const var for do while return throw class extends
yield await async keyof infer readonly unique enum super delete if try finally switch break continue'''.split())

def source_line(path, lineno):
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            for i, line in enumerate(f, 1):
                if i == lineno:
                    return line.rstrip('\n')
    except OSError:
        pass
    return ''


def main():
    ap = argparse.ArgumentParser()
    # Without entry files the corpus' own tsconfig says everything: files, target/lib es2020, strict, nodenext,
    # types: ["node"]
    ap.add_argument('entry', nargs='*', default=[])
    ap.add_argument('--corpus', choices=CORPORA, default='tsc', help='which project to check (default: tsc)')
    ap.add_argument('--options', help='options passed to tttt (the project, or compiler options when entry files are '
                    'given); overrides --corpus')
    ap.add_argument('-n', type=int, default=25, help='rows per table')
    ap.add_argument('--code', help='list every place this code is reported')
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--mem-gb', type=int, default=12)
    ap.add_argument('--tsc', action='store_true', help="regenerate tsc's cached diagnostics for the corpus (test/corpora.tsc.jsonl)")
    args = ap.parse_args()
    corpus_run = args.options is None

    cwd = ROOT
    if args.options is None:
        # Through its real path: in a worktree thirdparty/corpora is a symlink, and a project reached through one
        # loads its own files twice (webpack's node_modules/webpack -> .. realpaths to the other copy; tsc too). Run
        # from the tree the link leads into, so paths print short (thirdparty/corpora/...) rather than as ../../home/...
        flag = '-b ' if args.corpus in BUILD_CORPORA else '-p '
        real = os.path.realpath(os.path.join(ROOT, CORPORA[args.corpus]))
        parts = CORPORA[args.corpus].split('/')
        for i in range(1, len(parts) + 1):
            prefix = '/'.join(parts[:i])
            if os.path.islink(os.path.join(ROOT, prefix)):
                target = os.path.realpath(os.path.join(ROOT, prefix))
                if target.endswith('/' + prefix):
                    cwd = target[:-len(prefix) - 1]
                break
        args.options = flag + os.path.relpath(real, cwd)
        subprocess.run(['thirdparty/ensure-corpora.sh', CORPORA[args.corpus].split('/')[2]], check=True)
    if args.tsc:
        cache = load_tsc_cache()
        cache[args.corpus] = run_tsc(args.corpus, cwd, args.options)
        with open(TSC_CACHE, 'w') as f:
            for c in sorted(cache):
                f.write(json.dumps(cache[c], separators=(',', ':')) + '\n')
        print(f"tsc: {len(cache[args.corpus]['diags'])} diagnostics, exit {cache[args.corpus]['status']}, written to {TSC_CACHE}")
        return
    if not args.no_build:
        subprocess.run(['ninja', '-C', 'release', 'tttt'], check=True, stdout=subprocess.DEVNULL)
    limit = args.mem_gb << 30
    start = time.time()
    try:
        opts = args.options.split()
        # (`-b` must be the first argument, like tsc's TS6369)
        # (-b emits, tsc's too: tttt writes nothing into the checkout. -p is tsc --noEmit's oracle)
        cmd = [os.path.join(ROOT, 'release/tttt'), *(opts + ['--pretty', 'false', '--no-write'] if opts[:1] == ['-b'] else ['--pretty', 'false', '--noEmit'] + opts), *args.entry]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=args.timeout, cwd=cwd,
                              preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
        status = 'exit ' + str(proc.returncode) if proc.returncode >= 0 else 'KILLED BY SIGNAL ' + str(-proc.returncode)
        out = proc.stdout
    except subprocess.TimeoutExpired as e:
        status, out = 'TIMEOUT', e.stdout or b''
    wall = time.time() - start
    rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss // 1024
    text = re.sub(r'\x1b\[[0-9;]*m', '', out.decode(errors='replace'))
    # per tree and corpus: concurrent runs (sub-agents in worktrees, several corpora) don't overwrite each other
    tree = os.path.basename(ROOT)
    outpath = f'/tmp/realworld-{tree}-{args.corpus}.txt'
    with open(outpath, 'w') as f:
        f.write(text)
    lines = text.split('\n')

    codes, tokens, example, files = collections.Counter(), collections.Counter(), {}, collections.Counter()
    for path, lineno, col, code, message in tscformat.parse(text):
        code = f'TS{code}'
        codes[code] += 1
        files[path] += 1
        src = source_line(os.path.join(cwd, path), lineno) if path else ''
        if args.code == code:
            print(f'{path}:{lineno} {message}\n    {src.strip()[:160]}')
        if code.startswith('TS1') and len(code) == 6:
            # (The token at the column: an identifier, else a character)
            m = re.match(r'[A-Za-z_$][\w$]*|\S', src[col - 1:])
            tok = m.group(0) if m else ''
            tok = re.sub(r'[A-Za-z_$][\w$]*', lambda w: w.group(0) if w.group(0) in KEYWORDS else 'id', tok)[:14]
            key = (code, tok)
            tokens[key] += 1
            example.setdefault(key, f'{path.split("/")[-1]}:{lineno}  {src.strip()[:100]}')
    if args.code:
        return

    print(f'{status}, wall {wall:.2f}s, peak RSS {rss} MB, {sum(codes.values())} diagnostics in {len(files)} files '
          f'(full output: {outpath})')
    for l in lines:
        if l.startswith('  time:') or 'panic' in l.lower() or 'invalid pointer' in l or re.search(r'\.c:\d+: error:', l):
            print('  ' + l.strip()[:200])
    # Against tsc's cached diagnostics on the corpus, by (file, line, code) like the sweeps
    tsc = load_tsc_cache().get(args.corpus) if corpus_run and not args.entry else None
    if tsc:
        spurious, missing = sweep.compare(args.corpus, tsc, {'diags': [[f, l, c, m] for f, l, _, c, m in
                                                                     tscformat.parse(text)]}, cwd)
        stale = '' if tsc['pin'] == pin(args.corpus) else ' (STALE cache: rerun with --tsc)'
        print(f'against tsc: {len(spurious)} spurious, {len(missing)} missing ({len(tsc["diags"])} expected){stale}')
        for f, l, c, m in spurious[:args.n]:
            print(f'  + {f}:{l} TS{c} {m[:120]}')
        for f, l, c in missing[:args.n]:
            print(f'  - {f}:{l} TS{c}')
    elif corpus_run and not args.entry:
        print('against tsc: no cached diagnostics (run with --tsc)')
    print('\nby code:')
    for code, n in codes.most_common(args.n):
        print(f'  {n:6}  {code}')
    print('\nsyntax errors by the token they point at:')
    for key, n in tokens.most_common(args.n):
        print(f'  {n:6}  {key[0]} {key[1]!r:18} {example[key]}')

if __name__ == '__main__':
    sys.exit(main())
