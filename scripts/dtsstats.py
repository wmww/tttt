#!/usr/bin/env python3
"""The suite's dts component (declaration emit, plan 64): why tests fail it, bucketed by their first differing line.

  scripts/dtsstats.py [-f REGEX] [-k BUCKET] [--names] [--last]

Runs `baseline_tests --dts` (release build) and buckets each failing test by the first line of its diff, with words
replaced by `w` (`-` lines are what tttt wrote, `+` what tsgo did). `-k` lists the tests of the buckets that start
with BUCKET (`--names`: just names, for `-f @file`), `--last` reuses the previous run's output (/tmp/dtsstats.txt).
"""
import argparse, collections, os, re, subprocess, sys

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
ap = argparse.ArgumentParser()
ap.add_argument('-f', default=None)
ap.add_argument('-k', default=None)
ap.add_argument('--names', action='store_true')
ap.add_argument('--last', action='store_true')
ap.add_argument('--js', action='store_true')
args = ap.parse_args()
OUT = '/tmp/dtsstats-js.txt' if args.js else '/tmp/dtsstats.txt'
if not args.last:
    subprocess.run(['ninja', '-C', 'release'], check=True, stdout=subprocess.DEVNULL)
    cmd = ['./release/baseline_tests', '--dts-js' if args.js else '--dts'] + (['-f', args.f] if args.f else [])
    text = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout.decode('utf-8', 'replace')
    open(OUT, 'w').write(re.sub(r'\x1b\[[0-9;]*m', '', text))
text = open(OUT, errors='replace').read()

buckets = collections.defaultdict(list)
seen = set()
# Each failing test prints its diff, then `NAME: dts did not match:`
for m in re.finditer(r'--- /dev/fd/\d[^\n]*\n(.*?)^(\S+): dts did not match:', text, re.M | re.S):
    diff, name = m.group(1), m.group(2)
    if name in seen:
        continue
    seen.add(name)
    first = None
    for line in diff.split('\n'):
        if line.startswith(('-tttt dts:', '+++', '---')) or (line.startswith('+') and line.endswith('.js:')):
            continue
        if line.startswith(('-', '+')):
            first = line
            break
    if first is None:
        first = '(whitespace)'
    key = first[0] + re.sub(r'[A-Za-z_$][\w$]*', 'w', first[1:].strip())[:48]
    buckets[key].append((name, first))

total = sum(len(v) for v in buckets.values())
if args.k is not None:
    for key, items in sorted(buckets.items(), key=lambda kv: -len(kv[1])):
        if not key.startswith(args.k):
            continue
        for name, first in items:
            print(name if args.names else f'{name}: {first}')
    sys.exit(0)
summary = re.search(r'with JavaScript inputs: (\d+) / (\d+)' if args.js else r'dts \(declaration emit.*?\): (\d+) / (\d+)', text)
if summary:
    print(f'dts {summary.group(1)} / {summary.group(2)}, {total} failing ({len(buckets)} buckets; `-` tttt wrote, `+` tsgo did)')
for key, items in sorted(buckets.items(), key=lambda kv: -len(kv[1]))[:60]:
    print(f'{len(items):5}  {key!r:56}  {items[0][0]}: {items[0][1][:90]}')
