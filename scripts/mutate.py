#!/usr/bin/env python3
"""Mutation testing (plan 46 stage 2): missing and wrong diagnostics on code tsc complains about.

Takes single-file suite tests that pass all three components, applies one small source mutation to each (operators in
OPS, tagged in the mutant's name), has tsc (tsgo's test runner, scripts/tsgoprobe.sh) baseline the mutants (one process per core, cached) and tttt check
them, and buckets the errors-component mismatches by root-cause key, like failstats.

  scripts/mutate.py                      2000 mutants, seed 1: generate, tsc (cached), tttt, report
  scripts/mutate.py -n 400 --seed 7      another sample
  scripts/mutate.py --tests multi,js     mutants of multi-file (@filename) and JS (@allowJs) tests instead
  scripts/mutate.py --last               re-report the last run without running anything
  scripts/mutate.py --recheck            rerun tttt on the last run's mutants (after a fix: the mutants are generated from
                                   the tests passing in test/baseline_results.ts, so a new -s changes a seed's set)
  scripts/mutate.py -k 'missing TS2551'  the mutants behind one key (prefix match), with the mutated line
  scripts/mutate.py --show mut0042_member-typo   the mutation and tttt's errors diff for one mutant
  scripts/mutate.py --syntax             include the mutants whose tsc baseline gained a syntax error (parser recovery:
                                   separated by default)

The mutants live in /tmp/mutate/KEY/t/tests/cases/conformance, tsc's baselines in KEY/t/baselines/reference/conformance (KEY hashes their contents and the tsgo commit, so a
rerun after a tttt change reuses tsc's baselines; MUTATE_DIR overrides /tmp/mutate, eg for parallel worktrees); `log.json` there says which test, operator, line and text. Keys:
`missing TSx` / `spurious TSx` (the first unmatched diagnostic by (line, code)), `TSa->TSb` (a missing and a spurious
code on the same line), `position TSx` (same line and code, other column), `text TSx` (same code and position, other
message: mostly type printing), `elaboration TSx` (same first line, the chain or related info differs), `render` (same summary lines, the annotated source differs), `crash`.
"""
import argparse, collections, hashlib, json, os, random, re, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import suite  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSGO = os.path.join(ROOT, 'thirdparty/typescript')
BASE = os.environ.get('MUTATE_DIR', '/tmp/mutate')
ANSI = re.compile(r'\x1b\][^\x1b]*\x1b\\|\x1b\[[0-9;]*m')
ERR_LINE = re.compile(r'^(?:(.*?)\((\d+),(\d+)\): )?error (TS\d+): (.*)$')

# Regex operators: (pattern, replacement), one match picked at random. Matches on comment lines are skipped
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
    'never-const': (r'\bconst (\w+) = ', r'const \1: never = '),
    'null-arg': (r'(?<!function )(?<!\w )\b(\w+)\((\w+)(?=[,)])(?![^\n]*\)\s*[:{]\s*$)', r'\1(null'),
    'undefined-init': (r'(\blet \w+(?::[^=;]+)?) = [^;\n]+;', r'\1 = undefined;'),
    'extends-object': (r'\bextends (\w+)', r'extends Object'),
    'method-rename': (r'\{\n(\s+)(\w+)\(\)', r'{\n\1\2X()'),
    'drop-type-arg': (r'<(\w+(?:\[\])?), (\w+(?:\[\])?)>', r'<\1>'),
    'drop-modifier': (r'\b(override|readonly|abstract) (?=\w)', ''),
    'literal-type': (r'(:\s*)"(\w*)"(?=\s*[;,)|}\n])', r'\1"\2X"'),
    'bool-literal-type': (r'(:\s*)true\b', r'\1false'),
    'drop-nonnull': (r'(\w)!(?=[.;),\[])', r'\1'),
    'drop-as': (r' as (?!const\b)\w+(?:<[^<>]*>)?(?:\[\])?', ''),
}


LINE_OPS = ['delete-stmt', 'delete-stmt', 'delete-member', 'swap-stmts']  # repeated: drawn more often


def line_ops(src, rng, op):
    """Statement-level operators, returning (op, line number, before, mutated source) or None"""
    lines = src.splitlines(keepends=True)
    def code(i):
        s = lines[i].strip()
        return s and not s.startswith(('//', '/*', '*')) and s.endswith(';') and not s.startswith(('import ', 'export '))
    if op == 'delete-stmt':
        cands = [i for i in range(len(lines)) if code(i)]
        if not cands:
            return None
        i = rng.choice(cands)
        return op, i + 1, lines[i].strip(), ''.join(lines[:i] + lines[i + 1:])
    if op == 'swap-stmts':
        indent = lambda i: len(lines[i]) - len(lines[i].lstrip())
        cands = [i for i in range(len(lines) - 1) if code(i) and code(i + 1) and indent(i) == indent(i + 1)]
        if not cands:
            return None
        i = rng.choice(cands)
        body = lambda l: l.rstrip('\r\n')
        a, b = body(lines[i]), body(lines[i + 1])
        lines[i], lines[i + 1] = b + lines[i][len(a):], a + lines[i + 1][len(b):]
        return op, i + 1, a, ''.join(lines)
    # delete-member: a one-line member of a class that implements or extends something, or of an object type
    cands = []
    depth_class = None
    for i, l in enumerate(lines):
        if re.search(r'\bclass \w+[^{]*\b(implements|extends)\b[^{]*\{\s*$', l):
            depth_class = len(l) - len(l.lstrip())
            continue
        if depth_class is not None:
            if l.strip() == '}' and len(l) - len(l.lstrip()) == depth_class:
                depth_class = None
            elif re.match(r'\s+(?:public |private |protected |static |readonly )*\w+\??\s*[(:].*[;}]\s*$', l) and \
                    l.count('{') == l.count('}'):
                cands.append(i)
    if not cands:
        return None
    i = rng.choice(cands)
    return op, i + 1, lines[i].strip(), ''.join(lines[:i] + lines[i + 1:])


def suite_tests(kinds=()):
    """Passing suite tests under 6000 bytes: single-file TypeScript ones, plus multi-file (`@filename`) and JS
    (`@allowJs`) ones if `kinds` says so (the only ones with `--tests multi` / `--tests js`)"""
    res = open(f'{ROOT}/test/baseline_results.ts').read()
    passing = set(re.findall(r"'([^']+)': \{\s*errors: true,\s*symbols: true,\s*types: true", res))
    paths = {}
    for p in suite.test_files(own=False):
        f = os.path.basename(p)
        if f.endswith('.ts') and f[:-3] in passing:
            paths[f[:-3]] = p
    out = {}
    for n, p in sorted(paths.items()):
        if os.path.getsize(p) >= 6000:
            continue
        src = open(p, errors='replace', newline='').read()
        if '@noTypesAndSymbols' in src:
            continue
        kind = 'js' if '@allowjs' in src.lower() else 'multi' if '@filename' in src.lower() else 'single'
        if kind in (kinds or ('single',)):
            out[n] = src
    return out


def generate(n, seed, kinds=()):
    """Deterministic mutants: {name: (source, log entry)}"""
    rng = random.Random(seed)
    tests = suite_tests(kinds)
    names = sorted(tests)
    ops = sorted(OPS) + LINE_OPS
    mutants, seen = {}, set()
    while len(mutants) < n:
        t = rng.choice(names)
        src = tests[t]
        op = rng.choice(ops)
        if op in LINE_OPS:
            r = line_ops(src, rng, op)
            if not r:
                continue
            op, line, before, mutated = r
            after = ''
        else:
            pat, rep = OPS[op]
            ms = [m for m in re.finditer(pat, src)
                  if '//' not in src[src.rfind('\n', 0, m.start()) + 1:m.start()]]
            if not ms:
                continue
            m = rng.choice(ms)
            mutated = src[:m.start()] + m.expand(rep) + src[m.end():]
            line, before, after = src[:m.start()].count('\n') + 1, m.group(0)[:80], m.expand(rep)[:80]
        if mutated == src or (t, mutated) in seen:
            continue
        seen.add((t, mutated))
        name = f'mut{len(mutants):04d}_{op}'
        mutants[name] = (mutated, {'test': t, 'op': op, 'line': line, 'before': before, 'after': after})
    return mutants


def run_tsc(work, names, retry=True):
    """tsc's (tsgo's) baselines for the mutants, one warm probe process per core (scripts/tsgoprobe.sh), merged into
    work/t/baselines/reference/conformance. A shard's probe can die midway (2026-10-20: a quarter of 8000 mutants had no
    baselines, which read as "tsc reports nothing": hundreds of spurious keys): the mutants left without any baseline
    are run once more"""
    jobs = min(os.cpu_count() or 1, 4, len(names))
    procs = []
    for j in range(jobs):
        shard = f'{work}/tsc{j}'
        shutil.rmtree(shard, ignore_errors=True)
        os.makedirs(f'{shard}/tests/cases/conformance')
        for name in names[j::jobs]:
            shutil.copy(f'{work}/t/tests/cases/conformance/{name}.ts', f'{shard}/tests/cases/conformance/')
        procs.append(subprocess.Popen([f'{ROOT}/scripts/tsgoprobe.sh', shard], stdin=subprocess.DEVNULL,
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    for p in procs:
        p.wait()
    ref = f'{work}/t/baselines/reference/conformance'
    os.makedirs(ref, exist_ok=True)
    # (An empty file is a baseline the dying probe didn't finish writing, never a real one: 2026-10-23, an empty
    # .errors.txt read as "tsc reports nothing" and made a mutant of 30 expected errors look spurious. Rerun as missing)
    truncated = set()
    for j in range(jobs):
        local = f'{work}/tsc{j}/baselines/local/conformance'
        for f in os.listdir(local) if os.path.isdir(local) else []:
            if f.endswith(('.errors.txt', '.types', '.symbols')):
                if os.path.getsize(f'{local}/{f}') == 0:
                    truncated.add(f.split('.')[0])
                    continue
                shutil.move(f'{local}/{f}', f'{ref}/{f}')
        shutil.rmtree(f'{work}/tsc{j}')
    for name in truncated:
        for ext in ('.errors.txt', '.types', '.symbols'):
            if os.path.exists(f'{ref}/{name}{ext}'):
                os.remove(f'{ref}/{name}{ext}')
    have = {f.split('.')[0] for f in os.listdir(ref)}
    missing = [n for n in names if n not in have]
    if missing:
        print(f'tsc: {len(missing)} mutants without baselines' + (', running them again' if retry else ''), file=sys.stderr)
        if retry:
            run_tsc(work, missing, retry=False)


def run_tttt(work):
    """tttt over the mutants with --dump; a crash takes its test out and reruns the rest. Returns the crashed names"""
    subprocess.run(['ninja', '-C', 'release'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    dump = f'{work}/dump'
    shutil.rmtree(dump, ignore_errors=True)
    os.makedirs(dump)
    names = sorted(f[:-3] for f in os.listdir(f'{work}/t/tests/cases/conformance'))
    results = f'{work}/results.ts'
    crashed = []
    todo = names
    while todo:
        open(f'{work}/names.txt', 'w').write('\n'.join(todo) + '\n')
        open(results, 'w').write("const tsCommit = '';\n")
        with open(f'{work}/stderr.txt', 'wb') as err:
            try:
                code = subprocess.run(['./release/baseline_tests', '--dir', f'{work}/t/tests', '--results-path', results,
                                       '--summary', '--dump', dump, '-f', f'@{work}/names.txt'], cwd=ROOT,
                                      stdout=subprocess.DEVNULL, stderr=err, timeout=1200).returncode
            except subprocess.TimeoutExpired:
                code = -1
        if code == 0:
            break
        stderr = open(f'{work}/stderr.txt', errors='replace').read()
        started = [l[9:].split('(')[0] for l in stderr.splitlines() if l.startswith('### TEST ')]
        if not started:
            print(f'harness failed with {code} before any test', file=sys.stderr)
            break
        bad = started[-1]
        crashed.append(bad)
        done = {f.split('.')[0].split('(')[0] for f in os.listdir(dump)}
        todo = [t for t in todo if t != bad and t not in done]
    return crashed


def parse_errors(text):
    out = []
    for line in (text or '').splitlines():
        if line.startswith('===='):
            break
        m = ERR_LINE.match(line)
        if m:
            out.append([int(m[2] or 0), int(m[3] or 0), m[4], m[5], '', m[1] or ''])
        elif out and line.startswith(' '):
            out[-1][4] += line.strip() + '\n'  # elaboration chain and related information
    return [tuple(e) for e in out]


def read(path):
    try:
        return open(path, encoding='utf-8', errors='replace', newline='').read()
    except FileNotFoundError:
        return ''


def classify(ours_text, exp_text):
    """(key, detail) for the first difference between tttt's and tsc's errors.txt, None if equal"""
    if ours_text == exp_text:
        return None
    ours, exp = parse_errors(ours_text), parse_errors(exp_text)
    # (file, line, code): multi-file tests number lines per file
    o_lc = collections.Counter((e[5], e[0], e[2]) for e in ours)
    e_lc = collections.Counter((e[5], e[0], e[2]) for e in exp)
    missing = sorted((e_lc - o_lc).elements())
    spurious = sorted((o_lc - e_lc).elements())
    if missing or spurious:
        first = min(missing + spurious)
        where = first[:2]
        m = [c for f, l, c in missing if (f, l) == where]
        s = [c for f, l, c in spurious if (f, l) == where]
        if m and s:
            return f'{m[0]}->{s[0]}', where[1]
        if first in missing:
            return f'missing {first[2]}', where[1]
        return f'spurious {first[2]}', where[1]
    o_pos = collections.Counter((e[5], *e[:3]) for e in ours)
    e_pos = collections.Counter((e[5], *e[:3]) for e in exp)
    diff = sorted(((e_pos - o_pos) + (o_pos - e_pos)).elements())
    if diff:
        return f'position {diff[0][3]}', diff[0][1]
    for o, e in zip(sorted(ours), sorted(exp)):
        if o[3] != e[3]:
            return f'text {e[2]}', e[0]
    for o, e in zip(sorted(ours), sorted(exp)):
        if o != e:
            return f'elaboration {e[2]}', e[0]
    return 'render', 0


def has_new_syntax_error(exp_text, orig_name):
    """Whether tsc's baseline of the mutant has a TS1xxx code the original test's baseline doesn't"""
    orig = {e[2] for e in parse_errors(read(suite.ref(orig_name, '.errors.txt')))}
    return any(e[2].startswith('TS1') and e[2] not in orig for e in parse_errors(exp_text))


def analyse(work):
    log = json.load(open(f'{work}/log.json'))
    crashed = json.load(open(f'{work}/crashed.json')) if os.path.exists(f'{work}/crashed.json') else []
    ref, dump = f'{work}/t/baselines/reference/conformance', f'{work}/dump'
    rows = []  # (name, test name incl. configuration, key, line, syntax?)
    for f in sorted(os.listdir(dump)):
        if not f.endswith('.errors.txt'):
            continue
        tname = f[:-len('.errors.txt')]
        name = tname.split('(')[0]
        exp = read(f'{ref}/{f}')
        r = classify(read(f'{dump}/{f}'), exp)
        if r:
            rows.append((name, tname, r[0], r[1], has_new_syntax_error(exp, log[name]['test'])))
    for name in crashed:
        rows.append((name, name, 'crash', 0, False))
    return log, rows


def productive(work, log):
    """The mutants whose tsc diagnostics (codes) differ from the original test's: the ones that test anything"""
    ref, out = f'{work}/t/baselines/reference/conformance', set()
    for f in os.listdir(ref):
        if f.endswith('.errors.txt') or f.endswith('.types'):
            name = f.split('.')[0].split('(')[0]
            orig = f.replace(name, log[name]['test'], 1)
            if not f.endswith('.errors.txt'):
                # no errors.txt: productive only if the original had one
                f, orig = f[:-6] + '.errors.txt', orig[:-6] + '.errors.txt'
            codes = lambda t: collections.Counter(e[2] for e in parse_errors(t))
            if codes(read(f'{ref}/{f}')) != codes(read(suite.ref(orig[:-len('.errors.txt')], '.errors.txt'))):
                out.add(name)
    return out


def report(work, args):
    log, rows = analyse(work)
    total = len(log)
    rows = [r for r in rows if args.syntax or not r[4]]
    syntax = sum(1 for r in analyse(work)[1] if r[4]) if not args.syntax else 0
    # One row per mutant: the first configuration that differs
    per_mutant = {}
    for r in rows:
        per_mutant.setdefault(r[0], r)
    if args.k is not None:
        for name, r in sorted(per_mutant.items()):
            if r[2].startswith(args.k):
                e = log[name]
                print(f'{r[1]}  {e["test"]}:{e["line"]}  {e["before"]!r} -> {e["after"]!r}  (diff at line {r[3]})')
        return
    by_key = collections.Counter(r[2] for r in per_mutant.values())
    by_op = collections.Counter(log[n]['op'] for n in per_mutant)
    all_ops = collections.Counter(e['op'] for e in log.values())
    prod = productive(work, log)
    prod_ops = collections.Counter(log[n]['op'] for n in prod)
    print(f'{total} mutants of {len(set(e["test"] for e in log.values()))} tests: {len(per_mutant)} differ from tsc in '
          f'the errors component, of {len(prod)} that change tsc\'s codes' + (f' ({syntax} more where tsc reports a new syntax error: --syntax)' if syntax else ''))
    print('\nBy key (first difference per mutant; -k KEY lists them):')
    for key, c in by_key.most_common(args.n):
        ex = next(n for n, r in sorted(per_mutant.items()) if r[2] == key)
        print(f'  {c:5}  {key:28} eg {ex}  ({log[ex]["test"]})')
    if len(by_key) > args.n:
        print(f'  ... {len(by_key) - args.n} more keys')
    print('\nBy operator (differing / changing tsc\'s codes / mutants):')
    for op, c in sorted(all_ops.items(), key=lambda x: (-by_op[x[0]], -prod_ops[x[0]])):
        print(f'  {by_op[op]:5} / {prod_ops[op]:5} / {c:<5} {op}')


def show(work, name):
    log = json.load(open(f'{work}/log.json'))
    e = log[name]
    print(f'{name}: {e["test"]} line {e["line"]}: {e["before"]!r} -> {e["after"]!r}')
    print(f'source: {work}/t/tests/cases/conformance/{name}.ts')
    ref, dump = f'{work}/t/baselines/reference/conformance', f'{work}/dump'
    for f in sorted(os.listdir(dump)):
        if f.startswith(name) and f.endswith('.errors.txt'):
            ours, exp = read(f'{dump}/{f}'), read(f'{ref}/{f}')
            print(f'--- {f}: {classify(ours, exp)}')
            o, x = parse_errors(ours), parse_errors(exp)
            for d in sorted(set(x) - set(o)):
                print(f'  + tsc   ({d[5] + ':' if d[5] else ''}{d[0]},{d[1]}) {d[2]}: {d[3][:200]}' + ''.join('\n          ' + l for l in d[4].splitlines()))
            for d in sorted(set(o) - set(x)):
                print(f'  - tttt  ({d[5] + ':' if d[5] else ''}{d[0]},{d[1]}) {d[2]}: {d[3][:200]}' + ''.join('\n          ' + l for l in d[4].splitlines()))
            if set(o) == set(x) and ours != exp:
                import difflib
                for l in list(difflib.unified_diff(exp.splitlines(), ours.splitlines(), 'tsc', 'tttt', lineterm='', n=1))[:40]:
                    print('  ' + l)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-n', '--count', type=int, default=2000)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--last', action='store_true')
    ap.add_argument('--recheck', action='store_true')
    ap.add_argument('-k')
    ap.add_argument('--show')
    ap.add_argument('--syntax', action='store_true')
    ap.add_argument('--rows', dest='n', type=int, default=40)
    ap.add_argument('--tests', default='single',
                    help='which passing tests to mutate, comma separated: single (one TypeScript file), multi '
                         '(@filename), js (@allowJs)')
    args = ap.parse_args()
    os.makedirs(BASE, exist_ok=True)
    last = f'{BASE}/last'
    if args.recheck:
        work = os.path.realpath(last)
        json.dump(run_tttt(work), open(f'{work}/crashed.json', 'w'))
        return report(work, args)
    if args.last or args.k is not None or args.show:
        work = os.path.realpath(last)
        if not os.path.exists(work):
            sys.exit('no previous run')
        return show(work, args.show) if args.show else report(work, args)

    mutants = generate(args.count, args.seed, tuple(args.tests.split(',')))
    commit = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=TSGO, capture_output=True, text=True).stdout.strip()
    h = hashlib.sha1(commit.encode())
    for name, (src, _) in sorted(mutants.items()):
        h.update(name.encode() + b'\0' + src.encode() + b'\0')
    work = f'{BASE}/{h.hexdigest()[:12]}'
    cases = f'{work}/t/tests/cases/conformance'
    if not os.path.exists(f'{work}/tsc_done'):
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(cases)
        os.makedirs(f'{work}/t/tests/cases/compiler')
        for name, (src, _) in mutants.items():
            open(f'{cases}/{name}.ts', 'w', newline='').write(src)
        json.dump({n: e for n, (_, e) in mutants.items()}, open(f'{work}/log.json', 'w'), indent=1)
        print(f'tsc: baselining {len(mutants)} mutants in {work}', file=sys.stderr)
        run_tsc(work, sorted(mutants))
        open(f'{work}/tsc_done', 'w').close()
    if not os.path.lexists(f'{work}/t/tests/lib'):
        os.symlink(f'{suite.TESTS}/lib', f'{work}/t/tests/lib')
    print('tttt: checking', file=sys.stderr)
    crashed = run_tttt(work)
    json.dump(crashed, open(f'{work}/crashed.json', 'w'))
    if os.path.islink(last) or os.path.exists(last):
        os.remove(last)
    os.symlink(work, last)
    report(work, args)


if __name__ == '__main__':
    main()
