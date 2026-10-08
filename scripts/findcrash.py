#!/usr/bin/env python3
"""Finds the tests that crash (or hang, or run away with memory) the release test runner, by bisecting on test name
prefixes. Usage: scripts/findcrash.py [prefix-regex-start]. Build first (scripts/fasttest.sh does)."""
import subprocess, sys, os, glob, string

def names():
    out = set()
    for path in glob.glob('thirdparty/tsc/tests/cases/**/*.ts*', recursive=True):
        out.add(os.path.splitext(os.path.basename(path))[0])
    return sorted(out, key=str.lower)

def crashes(regex):
    cmd = f"ulimit -v 8000000; timeout 120 ./release/baseline_tests --summary -f '{regex}' >/dev/null 2>&1"
    return subprocess.run(['bash', '-c', cmd]).returncode not in (0, 1)

def search(prefix, candidates):
    if not crashes('^' + prefix + '.*'):
        return
    exact = [n for n in candidates if n.lower() == prefix.lower()]
    longer = [n for n in candidates if len(n) > len(prefix)]
    if len(candidates) <= 8:
        for n in candidates:
            if crashes('^' + n + '$'):
                print(n, flush=True)
        return
    for n in exact:
        if crashes('^' + n + '$'):
            print(n, flush=True)
    for c in sorted({n[len(prefix)].lower() for n in longer}):
        esc = '\\' + c if c in '.$^()[]{}+*?|\\' else c
        search(prefix + esc, [n for n in longer if n[len(prefix)].lower() == c])

all_names = names()
start = sys.argv[1] if len(sys.argv) > 1 else ''
search(start, [n for n in all_names if n.lower().startswith(start.lower())])
