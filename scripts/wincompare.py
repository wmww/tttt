#!/usr/bin/env python3
"""The corpora on Windows (agents/notes/windows.md): tsgo and tttt on each, from the repository root like
scripts/realworld.py, their whole stdout and exit code compared byte for byte; and tsgo's against its Linux output cached
in test/corpora.tsc.jsonl (what tsgo itself prints differently there; the checkout's own path aside, though a message
truncated by length counts its characters). Runs where realworld.py can't (no `resource`, no
bwrap): any Python 3, on Windows or Linux.

  scripts/wincompare.py [-c CORPUS]... [--tttt PATH] [--tsc PATH] [--checkers K] [-v]

-c limits it to those corpora (default: every -p corpus present). -v prints the first differing lines."""
import argparse
import ast
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = '.exe' if os.name == 'nt' or sys.platform in ('msys', 'cygwin') else ''


def corpora():
    """realworld.py's CORPORA and BUILD_CORPORA, read without importing it (it needs Linux-only modules)"""
    text = open(os.path.join(ROOT, 'scripts/realworld.py'), encoding='utf-8').read()
    table = ast.literal_eval(re.search(r'^CORPORA = (\{.*?^\})', text, re.S | re.M).group(1))
    build = ast.literal_eval(re.search(r'^BUILD_CORPORA = (\(.*?\))', text, re.M).group(1))
    return table, build


def run(cmd):
    start = time.time()
    p = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return p.returncode, p.stdout, p.stderr, time.time() - start


def rooted(text):
    """The checkout's absolute path (/home/x/tttt/, C:/tttt/) made one placeholder: tsgo's messages name files by it"""
    return re.sub(rb'[^\s"\'(]*?/thirdparty/corpora/', b'<ROOT>/thirdparty/corpora/', text)


def first_difference(a, b):
    la, lb = a.splitlines(), b.splitlines()
    for i in range(max(len(la), len(lb))):
        x = la[i] if i < len(la) else b'<end>'
        y = lb[i] if i < len(lb) else b'<end>'
        if x != y:
            return i + 1, x, y
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-c', '--corpus', action='append')
    ap.add_argument('--tttt', default=os.path.join(ROOT, 'release', 'tttt' + EXE))
    ap.add_argument('--tsc', default=os.path.join(ROOT, 'thirdparty', 'typescript', 'built', 'tsc' + EXE))
    ap.add_argument('--checkers', help='tttt --checkers K as well (K-dependence)')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    table, build = corpora()
    cache = {}
    cache_path = os.path.join(ROOT, 'test/corpora.tsc.jsonl')
    if os.path.exists(cache_path):
        cache = {r['corpus']: r for r in map(json.loads, open(cache_path, encoding='utf-8'))}
    names = args.corpus or [c for c in table if c not in build]
    same = 0
    for name in names:
        path = table[name]
        if not os.path.exists(os.path.join(ROOT, path)):
            print(f'{name:16} missing')
            continue
        options = ['-p', path, '--pretty', 'false', '--noEmit']
        ts = run([args.tsc] + options)
        tt = run([args.tttt] + options)
        runs = [('tttt', tt)]
        if args.checkers:
            runs.append((f'tttt K{args.checkers}', run([args.tttt] + options + ['--checkers', args.checkers])))
        verdicts = []
        for label, r in runs:
            ok = r[0] == ts[0] and r[1] == ts[1]
            verdicts.append(f'{label} {"SAME" if ok else "DIFF"} (exit {r[0]}, {r[3]:.2f} s)')
            if not ok and args.verbose:
                d = first_difference(ts[1], r[1])
                if d:
                    print(f'  {name} line {d[0]}:\n    tsgo: {d[1][:300]!r}\n    {label}: {d[2][:300]!r}')
                if r[2]:
                    print(f'  {label} stderr: {r[2][-500:]!r}')
        linux = cache.get(name)
        if linux is not None:
            ok = rooted(linux['text'].encode()) == rooted(ts[1]) and linux['status'] == ts[0]
            verdicts.append('tsgo as on Linux' if ok else 'tsgo differs from Linux')
            if not ok and args.verbose:
                d = first_difference(rooted(linux['text'].encode()), rooted(ts[1]))
                if d:
                    print(f'  {name} vs Linux line {d[0]}:\n    linux: {d[1][:300]!r}\n    here:  {d[2][:300]!r}')
        same += not any(v.startswith('tttt') and ' DIFF ' in v for v in verdicts)
        print(f'{name:16} tsgo exit {ts[0]} ({ts[3]:.2f} s, {len(ts[1].splitlines())} lines); ' + '; '.join(verdicts),
              flush=True)
    print(f'{same} / {len(names)} identical')


if __name__ == '__main__':
    main()
