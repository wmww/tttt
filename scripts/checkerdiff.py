#!/usr/bin/env python3
"""Compares tttt's diagnostics with --checkers 1 and --checkers K (plan 36) on the pinned corpora.

tsgo's output doesn't depend on its checker count, so tttt's mustn't either (plan 62): any diagnostic (header, source
excerpt, message chain) that differs is a bug: an evaluation-order dependence, a memoised report, a tie the stable type
order breaks by type id. The one exception tsgo shares is widened object literals' `x?: undefined` property order
(tsgo's per-checker undefinedProperties cache; no corpus shows it). Also runs K twice and checks the outputs are
byte-identical.

  scripts/checkerdiff.py                   # every corpus, K=4
  scripts/checkerdiff.py -k 2 webpack zod  # some corpora, K=2
  scripts/checkerdiff.py --file x.ts       # one file (or a -p directory) instead of the corpora
  scripts/checkerdiff.py -k 1 -m 0         # a fresh checker per file against one that never recycles

thirdparty/ensure-corpora.sh runs only when a corpus it checks is missing; TTTT_NO_ENSURE=1 errors out then instead
(worktrees, whose corpora are the shared checkouts). The environment goes through to tttt (TTTT_SHARED_GRAPH=1...).
"""
import argparse
import collections
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from realworld import CORPORA

# tsc's pretty format (with the related information and code frames): `file:line:col - error TSxxxx: message`
HEADER = re.compile(r'^(\S.*?:\d+:\d+ - )?error TS\d+: ')


def run(options, checkers, timeout, memory='off'):
    build = list(options[:2]) if options[:1] == ['-b'] else []  # (-b first: tsc's TS6369)
    proc = subprocess.run(['./release/tttt', *build, '--pretty', 'true', '--keep-going', '--checkers', str(checkers),
                           '--checker-memory', memory, *(['--no-write'] if build else ['--noEmit']), *options[len(build):]],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    return re.sub(r'\x1b\[[0-9;]*m', '', proc.stdout.decode(errors='replace'))


def diagnostics(text):
    """The diagnostics of a pretty output, each the text of its header up to the next header (or the summary)"""
    out, current = [], None
    for line in text.split('\n'):
        if re.match(r'^Found \d+ errors?\b', line):
            if current is not None:
                out.append('\n'.join(current).strip())
            current = None
        elif HEADER.match(line):
            if current is not None:
                out.append('\n'.join(current).strip())
            current = [line]
        elif current is not None:
            current.append(line)
    if current is not None:
        out.append('\n'.join(current).strip())
    return out


def compare(name, options, k, timeout, examples, memory='off'):
    one = run(options, 1, timeout)
    many = run(options, k, timeout, memory)
    again = run(options, k, timeout, memory)
    d1, dk = diagnostics(one), diagnostics(many)
    c1, ck = collections.Counter(d1), collections.Counter(dk)
    lost, gained = c1 - ck, ck - c1
    exact = sum((lost + gained).values())
    print(f'{name}: {len(d1)} diagnostics with 1 checker, {len(dk)} with {k}; differing: {exact}; '
          f'{k} checkers twice: {"identical" if many == again else "DIFFERENT"}')
    for title, which in (('only with 1 checker', lost), (f'only with {k} checkers', gained)):
        for diag, _ in list(which.items())[:examples]:
            print(f'  {title}:\n    ' + diag.split('\n')[0])
    return exact == 0 and many == again


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('corpora', nargs='*', help='corpora (default: all of them but zod-root)')
    ap.add_argument('-k', type=int, default=4, help='checker count to compare with 1 (default 4)')
    ap.add_argument('--file', help='a file or -p project instead of the corpora')
    ap.add_argument('-m', '--memory', default='off',
                    help='--checker-memory of the K runs (default off; 0: a fresh checker per file, plan 67) against '
                         'one checker that never recycles')
    ap.add_argument('--examples', type=int, default=5)
    ap.add_argument('--timeout', type=int, default=300)
    args = ap.parse_args()
    subprocess.run(['ninja', '-C', 'release', 'tttt'], check=True, stdout=subprocess.DEVNULL)
    ok = True
    if args.file:
        options = ['-p', args.file] if os.path.isdir(args.file) or args.file.endswith('.json') else [args.file]
        ok = compare(args.file, options, args.k, args.timeout, args.examples, args.memory)
    else:
        names = args.corpora or [c for c in CORPORA if c != 'zod-root']
        missing = [n for n in names if not os.path.exists(CORPORA[n])]
        if missing and os.environ.get('TTTT_NO_ENSURE'):
            sys.exit('missing corpora (TTTT_NO_ENSURE set): ' + ' '.join(missing))
        if missing:
            subprocess.run(['thirdparty/ensure-corpora.sh'], check=True, stdout=subprocess.DEVNULL)
        for name in names:
            path = os.path.relpath(os.path.realpath(CORPORA[name]))
            ok = compare(name, ['-p', path], args.k, args.timeout, args.examples, args.memory) and ok
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
