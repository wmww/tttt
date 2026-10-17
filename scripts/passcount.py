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
# (dts, the declaration emit, where a test has it: part of a full pass since plan 64 stage 4)
# ("did not run" tests are saved with undefined components: failing, like the suite counts them)
tests = re.findall(r"'([^']*)': \{\s*errors: (true|false|undefined),\s*symbols: (true|false|undefined),"
                   r"\s*types: (true|false|undefined),(?:\s*dts: (true|false),)?", text)
if match:
    tests = [t for t in tests if match.search(t[0])]
e = [t[1] == 'true' for t in tests]
s = [t[2] == 'true' for t in tests]
t = [t[3] == 'true' for t in tests]
d = [t[4] != 'false' for t in tests]
dts = [t[4] == 'true' for t in tests if t[4]]
print(f'full {sum(a and b and c and x for a, b, c, x in zip(e, s, t, d))} / {len(e)}  errors {sum(e)}  symbols {sum(s)}  '
      f'types {sum(t)}  dts {sum(dts)} / {len(dts)}')
