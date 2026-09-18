#!/usr/bin/env python3
"""Pass totals of a saved baseline results file (default test/baseline_results.ts; save with ./fasttest.sh -s)."""
import re, sys
text = open(sys.argv[1] if len(sys.argv) > 1 else 'test/baseline_results.ts').read()
e = [m == 'true' for m in re.findall(r'errors: (true|false)', text)]
s = [m == 'true' for m in re.findall(r'symbols: (true|false)', text)]
t = [m == 'true' for m in re.findall(r'types: (true|false)', text)]
print(f'full {sum(a and b and c for a, b, c in zip(e, s, t))} / {len(e)}  errors {sum(e)}  symbols {sum(s)}  types {sum(t)}')
