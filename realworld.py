#!/usr/bin/env python3
"""Runs tttt on a real project (tsc's own compiler sources by default, ~146k lines, no errors under tsc) and
summarises what it reports: exit status, wall time, peak RSS, diagnostics by code, and syntax errors grouped by
the token they point at. tsc reports nothing for this corpus, so every diagnostic is a tttt bug.

  ./realworld.py                 # tttt -p thirdparty/tsc/src/compiler (its tsconfig.json)
  ./realworld.py path/to/entry.ts --options '--strict' -n 40
  ./realworld.py --code TS1005   # list the places a code is reported
  ./realworld.py --corpus valibot   # a second-oracle corpus from thirdparty/ensure-corpora.sh (type-fest, valibot, react, nest, webpack, zod, zod-root, xstate, drizzle, vue, effect, vscode, excalidraw, tanstack-router; tsc-src and typescript-eslint: -b on a solution config)
"""
import argparse, collections, os, re, resource, subprocess, sys, time

ROOT = os.path.dirname(os.path.abspath(__file__))

# The corpora besides tsc's compiler (plan 17): modern, type-heavy code, clean under tsc 5.8 as pinned (plan 48 stage 5 re-pins them for 6.0), fetched by
# thirdparty/ensure-corpora.sh
CORPORA = {
    'tsc': 'thirdparty/tsc/src/compiler',
    # TypeScript's whole src: a solution config of 13 composite projects, checked with -b (plan 41: references read as
    # sources). Clean, like tsc
    'tsc-src': 'thirdparty/tsc/src',
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

KEYWORDS = set('''else in of as is asserts new function catch export import type default case satisfies this void
typeof instanceof get set static abstract declare namespace let const var for do while return throw class extends
yield await async keyof infer readonly unique enum super delete if try finally switch break continue'''.split())

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
    args = ap.parse_args()

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
        if args.corpus not in ('tsc', 'tsc-src'):
            subprocess.run(['thirdparty/ensure-corpora.sh', CORPORA[args.corpus].split('/')[2]], check=True)
    if not args.no_build:
        subprocess.run(['ninja', '-C', 'release', 'tttt'], check=True, stdout=subprocess.DEVNULL)
    limit = args.mem_gb << 30
    start = time.time()
    try:
        proc = subprocess.run([os.path.join(ROOT, 'release/tttt'), '-q', '--keep-going', '--all-errors', *args.options.split(), *args.entry], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
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
    tree = os.path.basename(os.path.dirname(os.path.abspath(__file__)))
    outpath = f'/tmp/realworld-{tree}-{args.corpus}.txt'
    with open(outpath, 'w') as f:
        f.write(text)
    lines = text.split('\n')

    codes, tokens, example, files = collections.Counter(), collections.Counter(), {}, collections.Counter()
    for i, line in enumerate(lines):
        m = re.match(r'(\S+):(\d+) \[(TS\d+)\] (.*)', line)
        if not m:
            continue
        path, lineno, code, message = m.groups()
        codes[code] += 1
        files[path] += 1
        src, underline = (lines[i + 2], lines[i + 3]) if i + 3 < len(lines) else ('', '')
        if args.code == code:
            print(f'{path}:{lineno} {message}\n    {src.strip()[:160]}')
        if code.startswith('TS1') and len(code) == 6:
            s, e = underline.find('-'), underline.rfind('-') + 1
            tok = src[s:e] if s >= 0 else ''
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
    print('\nby code:')
    for code, n in codes.most_common(args.n):
        print(f'  {n:6}  {code}')
    print('\nsyntax errors by the token they point at:')
    for key, n in tokens.most_common(args.n):
        print(f'  {n:6}  {key[0]} {key[1]!r:18} {example[key]}')

if __name__ == '__main__':
    sys.exit(main())
