#!/usr/bin/env python3
"""Pass totals of a saved baseline results file (default test/baseline_results.ts; save with scripts/fasttest.sh -s).

  scripts/passcount.py [file] [--match REGEX]   only tests whose name contains a match (plan 05 tracks
                                          'controlFlow|narrow|typeGuard|discriminat|definiteAssign|unreachable|assertion')
"""
import re, sys
args = sys.argv[1:]
match = None
if '--match' in args:
    i = args.index('--match')
    match = re.compile(args[i + 1], re.I)
    del args[i:i + 2]
text = open(args[0] if args else 'test/baseline_results.ts').read()
tests = re.findall(r"'([^']*)': \{\s*errors: (true|false),\s*symbols: (true|false),\s*types: (true|false)", text)
if match:
    tests = [t for t in tests if match.search(t[0])]
e = [t[1] == 'true' for t in tests]
s = [t[2] == 'true' for t in tests]
t = [t[3] == 'true' for t in tests]
print(f'full {sum(a and b and c for a, b, c in zip(e, s, t))} / {len(e)}  errors {sum(e)}  symbols {sum(s)}  types {sum(t)}')
