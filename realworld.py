#!/usr/bin/env python3
"""Runs tttt on a real project (tsc's own compiler sources by default, ~146k lines, no errors under tsc) and
summarises what it reports: exit status, wall time, peak RSS, diagnostics by code, and syntax errors grouped by
the token they point at. tsc reports nothing for this corpus, so every diagnostic is a tttt bug.

  ./realworld.py                 # tttt -p thirdparty/tsc/src/compiler (its tsconfig.json)
  ./realworld.py path/to/entry.ts --options '--strict' -n 40
  ./realworld.py --code TS1005   # list the places a code is reported
  ./realworld.py --corpus valibot   # a second-oracle corpus from thirdparty/ensure-corpora.sh (type-fest, valibot, react)
"""
import argparse, collections, re, resource, subprocess, sys, time

# The corpora besides tsc's compiler (plan 17): modern, type-heavy code, clean under tsc 5.8, fetched by
# thirdparty/ensure-corpora.sh
CORPORA = {
    'tsc': 'thirdparty/tsc/src/compiler',
    'type-fest': 'thirdparty/corpora/type-fest',
    'valibot': 'thirdparty/corpora/valibot/library',
    'react': 'thirdparty/corpora/bulletproof-react/apps/react-vite',
}

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

    if args.options is None:
        args.options = '-p ' + CORPORA[args.corpus]
        if args.corpus != 'tsc':
            subprocess.run(['thirdparty/ensure-corpora.sh'], check=True)
    if not args.no_build:
        subprocess.run(['ninja', '-C', 'release', 'tttt'], check=True, stdout=subprocess.DEVNULL)
    limit = args.mem_gb << 30
    start = time.time()
    try:
        proc = subprocess.run(['./release/tttt', '-q', '--keep-going', '--all-errors', *args.options.split(), *args.entry], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=args.timeout,
                              preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
        status = 'exit ' + str(proc.returncode) if proc.returncode >= 0 else 'KILLED BY SIGNAL ' + str(-proc.returncode)
        out = proc.stdout
    except subprocess.TimeoutExpired as e:
        status, out = 'TIMEOUT', e.stdout or b''
    wall = time.time() - start
    rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss // 1024
    text = re.sub(r'\x1b\[[0-9;]*m', '', out.decode(errors='replace'))
    with open('/tmp/realworld.txt', 'w') as f:
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
          f'(full output: /tmp/realworld.txt)')
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
