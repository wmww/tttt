#!/usr/bin/env python3
"""The front end's output identity at breadth: two tttt binaries' `--listFilesOnly` (every program file, in order,
and the syntax and options diagnostics) byte for byte, with the exit code, on every source sweep config (the -p
sweep's list) and the -p corpora, from the directory the sweeps run in and (--cwd-too) from / with an absolute -p.
For loader, resolver, glob and parser-speed changes: a wrong file breaks a project silently.

    scripts/listcheck.py OLD NEW [-f regex] [-j 6] [--cwd-too] [--threads N]

OLD and NEW are tttt binaries (`scripts/abbuild.sh REV` prints one, release/tttt is this tree's). Prints each
differing project with its first differing line; exit 1 on any difference."""
import argparse, concurrent.futures, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sourcesweep, tttt_run

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORPORA = ['TypeScript6/src/compiler/tsconfig.json', 'nestjs-boilerplate/tsconfig.corpus.json', 'zod/packages/zod/tsconfig.test.json',
           'drizzle/drizzle-orm/tsconfig.corpus.json', 'vscode/src/tsconfig.corpus.json', 'effect/packages/effect/tsconfig.corpus.json',
           'xstate/packages/core/tsconfig.corpus.json', 'excalidraw/tsconfig.corpus.json', 'webpack/tsconfig.json',
           'bulletproof-react/apps/react-vite/tsconfig.corpus.json', 'vue/tsconfig.corpus.json', 'type-fest/tsconfig.json',
           'tanstack-router/tsconfig.corpus.json', 'valibot/library/tsconfig.corpus.json']


def one(binary, cwd, config, threads):
    cmd = [binary, '--noEmit', '--listFilesOnly', '-p', config] + (['--threads', str(threads)] if threads else [])
    r = tttt_run.run(cmd, cwd, timeout=300, mem_gb=16)
    return f'{r.status} {r.code}\n' + r.out.decode('utf-8', 'replace')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('old')
    ap.add_argument('new')
    ap.add_argument('-f', '--filter')
    ap.add_argument('-j', '--jobs', type=int, default=6)
    ap.add_argument('--cwd-too', action='store_true', help='also from / with an absolute -p')
    ap.add_argument('--threads', type=int, help='--threads for both (default: the CLI default)')
    args = ap.parse_args()
    jobs = [(sourcesweep.SOURCES, p) for p in sourcesweep.list_projects(args.filter, every_config=True)]
    corpora = os.path.join(ROOT, 'thirdparty', 'corpora')
    jobs += [(corpora, c) for c in CORPORA if (not args.filter or re.search(args.filter, c)) and os.path.exists(os.path.join(corpora, c))]
    if args.cwd_too:
        jobs += [('/', os.path.join(base, p)) for base, p in jobs]
    old, new = os.path.abspath(args.old), os.path.abspath(args.new)
    diffs = 0
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(lambda j: (one(old, j[0], j[1], args.threads), one(new, j[0], j[1], args.threads)), j): j for j in jobs}
        for f in concurrent.futures.as_completed(futures):
            cwd, p = futures[f]
            a, b = f.result()
            if a == b:
                continue
            diffs += 1
            al, bl = a.splitlines(), b.splitlines()
            i = next((i for i in range(min(len(al), len(bl))) if al[i] != bl[i]), min(len(al), len(bl)))
            print(f'DIFF {p} (cwd {cwd}) line {i + 1}:\n  old: {al[i] if i < len(al) else "<end>"}\n  new: {bl[i] if i < len(bl) else "<end>"}', flush=True)
    print(f'{len(jobs)} runs, {diffs} differing')
    return 1 if diffs else 0


if __name__ == '__main__':
    sys.exit(main())
