#!/usr/bin/env python3
"""Compares tttt's diagnostics with --checkers 1 and --checkers K (plan 36) on the pinned corpora.

With K checkers (files dealt round robin) the diagnostics may only differ from one checker's inside quoted types: ties
that the stable type order breaks by type id (per checker), and widened object literals' `x?: undefined` properties,
which take their position from the first property of that name the checker saw (tsc's undefinedProperties cache, one
per program in tsc; seen in webpack's JSON schemas). This runs each corpus both ways (--all-errors) and reports:
  exact     diagnostics (header, source excerpt, message chain) that differ at all
  modulo    what still differs once every quoted '...' part of a message is blanked: codes, positions and the
            unquoted text. Anything here is a determinism bug (a memoised report, an evaluation-order dependence)
Also runs K twice and checks the outputs are byte-identical.

  scripts/checkerdiff.py                   # every corpus, K=4
  scripts/checkerdiff.py -k 2 webpack zod  # some corpora, K=2
  scripts/checkerdiff.py --file x.ts       # one file (or a -p directory) instead of the corpora
"""
import argparse
import collections
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from realworld import CORPORA

HEADER = re.compile(r'^(\S+:\d+ )?\[TS\d+\] ')


def run(options, checkers, timeout):
    proc = subprocess.run(['./release/tttt', '-q', '--keep-going', '--all-errors', '--checkers', str(checkers), *options],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    return re.sub(r'\x1b\[[0-9;]*m', '', proc.stdout.decode(errors='replace'))


def diagnostics(text):
    """The diagnostics of an --all-errors output, each the text of its header up to the next header"""
    out, current = [], None
    for line in text.split('\n'):
        if HEADER.match(line):
            if current is not None:
                out.append('\n'.join(current).strip())
            current = [line]
        elif current is not None:
            current.append(line)
    if current is not None:
        out.append('\n'.join(current).strip())
    return out


def normalize(diag):
    return re.sub(r"'[^'\n]*'", "'…'", diag)


def compare(name, options, k, timeout, examples):
    one = run(options, 1, timeout)
    many = run(options, k, timeout)
    again = run(options, k, timeout)
    d1, dk = diagnostics(one), diagnostics(many)
    c1, ck = collections.Counter(d1), collections.Counter(dk)
    n1 = collections.Counter(normalize(d) for d in d1)
    nk = collections.Counter(normalize(d) for d in dk)
    exact = sum(((c1 - ck) + (ck - c1)).values())
    lost, gained = n1 - nk, nk - n1
    modulo = sum((lost + gained).values())
    print(f'{name}: {len(d1)} diagnostics with 1 checker, {len(dk)} with {k}; differing exactly: {exact}, '
          f'modulo quoted text: {modulo}; {k} checkers twice: {"identical" if many == again else "DIFFERENT"}')
    for title, which in (('only with 1 checker', lost), (f'only with {k} checkers', gained)):
        for diag, _ in list(which.items())[:examples]:
            print(f'  {title}:\n    ' + diag.split('\n')[0])
    return modulo == 0 and many == again


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('corpora', nargs='*', help='corpora (default: all of them but zod-root)')
    ap.add_argument('-k', type=int, default=4, help='checker count to compare with 1 (default 4)')
    ap.add_argument('--file', help='a file or -p project instead of the corpora')
    ap.add_argument('--examples', type=int, default=5)
    ap.add_argument('--timeout', type=int, default=300)
    args = ap.parse_args()
    subprocess.run(['ninja', '-C', 'release', 'tttt'], check=True, stdout=subprocess.DEVNULL)
    ok = True
    if args.file:
        options = ['-p', args.file] if os.path.isdir(args.file) or args.file.endswith('.json') else [args.file]
        ok = compare(args.file, options, args.k, args.timeout, args.examples)
    else:
        subprocess.run(['thirdparty/ensure-corpora.sh'], check=True, stdout=subprocess.DEVNULL)
        for name in args.corpora or [c for c in CORPORA if c != 'zod-root']:
            path = os.path.relpath(os.path.realpath(CORPORA[name]))
            ok = compare(name, ['-p', path], args.k, args.timeout, args.examples) and ok
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
