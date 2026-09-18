#!/usr/bin/env python3
"""Builds build/bench/corpus: code that tttt can check today, for ./bench.sh.

There's no real project tttt gets through yet, so this takes every baseline test that fully passes (per
test/baseline_results.ts), is a single script file (no // @directives, imports or exports) and expects no errors,
wraps it in a namespace so the tests don't collide, and keeps those both tttt and tsc still accept. The survivors are
written out COPIES times under different namespace names to get to a size where startup doesn't dominate.

Usage: bench/make_corpus.py [COPIES=20]
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = f'{ROOT}/build/bench/corpus'
COPIES = int(sys.argv[1]) if len(sys.argv) > 1 else 20


def passing_tests():
    text = open(f'{ROOT}/test/baseline_results.ts').read()
    return set(re.findall(r"'([^']+)': \{\s*errors: true,\s*symbols: true,\s*types: true,", text))


def candidates():
    passing = passing_tests()
    for sub in ('compiler', 'conformance'):
        for d, _, files in os.walk(f'{ROOT}/thirdparty/tsc/tests/cases/{sub}'):
            for f in sorted(files):
                name = f[:-3]
                if not f.endswith('.ts') or name not in passing:
                    continue
                if os.path.exists(f'{ROOT}/thirdparty/tsc/tests/baselines/reference/{name}.errors.txt'):
                    continue
                src = open(f'{d}/{f}', encoding='utf-8', errors='replace').read().lstrip('﻿')
                if re.search(r'^\s*//\s*@\w+\s*:', src, re.M) or re.search(r'^\s*(import|export)\b', src, re.M):
                    continue
                if re.search(r'\b(declare\s+global|module\s+["\'])', src):
                    continue
                yield name, src


def wrap(src, ns):
    return f'namespace {ns} {{\n{src}\n}}\n'


def accepted(cmd, path):
    return subprocess.run(cmd + [path], cwd=ROOT, capture_output=True).returncode == 0


def main():
    subprocess.run(['rm', '-rf', OUT]); os.makedirs(OUT)
    probe = f'{ROOT}/build/bench/probe.ts'
    kept = []
    for name, src in candidates():
        open(probe, 'w').write(wrap(src, 'Probe'))
        if accepted([f'{ROOT}/release/tttt', '-q'], probe):
            kept.append((name, src))
    # tsc in one go, then drop whatever files it complains about
    for i, (name, src) in enumerate(kept):
        open(f'{OUT}/t{i}.ts', 'w').write(wrap(src, f'T{i}_0'))
    out = subprocess.run(['node', f'{ROOT}/thirdparty/tsc/lib/tsc.js', '--noEmit', '--strict', '--target', 'esnext'] +
                         [f'{OUT}/t{i}.ts' for i in range(len(kept))], cwd=ROOT, capture_output=True, text=True).stdout
    bad = {int(m) for m in re.findall(r'corpus/t(\d+)\.ts\(', out)}
    lines = 0
    for i, (name, src) in enumerate(kept):
        path = f'{OUT}/t{i}.ts'
        if i in bad:
            os.remove(path)
            continue
        text = ''.join(wrap(src, f'T{i}_{c}') for c in range(COPIES))
        open(path, 'w').write(text)
        lines += text.count('\n')
    os.remove(probe)
    print(f'{len(kept) - len(bad)} tests x {COPIES} copies, {lines} lines in build/bench/corpus '
          f'({len(bad)} dropped because tsc rejected them once wrapped)')


if __name__ == '__main__':
    main()
