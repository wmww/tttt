import os, re, random, sys, json
random.seed(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
N = int(sys.argv[1])
ROOT = '/home/ai/tttt'
res = open(ROOT + '/test/baseline_results.ts').read()
passing = set(re.findall(r"'([^']+)': \{\s*errors: true,\s*symbols: true,\s*types: true", res))
paths = {}
for d, _, fs in os.walk(ROOT + '/thirdparty/tsc/tests/cases'):
    for f in fs:
        if f.endswith('.ts') and f[:-3] in passing:
            paths[f[:-3]] = os.path.join(d, f)
OPS = {
    'drop-await': (r'\bawait ', ''),
    'return-void': (r'\breturn ([^;\n]+);', 'return;'),
    'num-to-str': (r'(?<![\w.$"\'])(\d+)(?![\w.])', r'"\1"'),
    'str-to-num': (r'"([a-z]\w*)"', '42'),
    'member-typo': (r'\.([a-z]\w+)\b(?!\s*[:=(])', r'.\1X'),
    'ann-string-number': (r':\s*string\b', ': number'),
    'ann-number-string': (r':\s*number\b', ': string'),
    'drop-optional': (r'(\w)\?:', r'\1:'),
    'add-optional': (r'(\n\s+\w+):(?!:)', r'\1?:'),
    'swap-args': (r'\((\w+), (\w+)\)', r'(\2, \1)'),
    'drop-arg': (r'\((\w+), (\w+)\)', r'(\1)'),
    'add-arg': (r'(\w)\((\w+)\)', r'\1(\2, 1)'),
    'drop-prop': (r'\{ *(\w+): [^,{}]+, ', '{ '),
    'readonly-assign': (r'\bconst (\w+) = ', r'const \1: never = '),
    'null-arg': (r'(\w)\((\w+)', r'\1(null'),
    'undefined-init': (r'(\blet \w+(?::[^=;]+)?) = [^;\n]+;', r'\1 = undefined;'),
    'extends-swap': (r'\bextends (\w+)', r'extends Object'),
    'implements-drop': (r'\{\n(\s+)(\w+)\(\)', r'{\n\1\2X()'),
}
names = sorted(n for n, p in paths.items() if '@filename' not in open(p, errors='replace').read().lower() and os.path.getsize(p) < 6000)
out = '/tmp/mut/t/tests/cases/conformance'
os.makedirs(out, exist_ok=True)
made = 0
log = {}
while made < N:
    n = random.choice(names)
    src = open(paths[n], errors='replace').read()
    op = random.choice(list(OPS))
    pat, rep = OPS[op]
    ms = [m for m in re.finditer(pat, src) if not src[:m.start()].rstrip().endswith('//') and '//' not in src[src.rfind('\n', 0, m.start()):m.start()]]
    if not ms:
        continue
    m = random.choice(ms)
    mutated = src[:m.start()] + m.expand(rep) + src[m.end():]
    name = f'mut{made:03d}_{op}'
    open(f'{out}/{name}.ts', 'w').write(mutated)
    log[name] = {'test': n, 'op': op, 'line': src[:m.start()].count('\n') + 1, 'before': m.group(0)[:80], 'after': m.expand(rep)[:80]}
    made += 1
json.dump(log, open('/tmp/mut/log.json', 'w'), indent=1)
print(made)
