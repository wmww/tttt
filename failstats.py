#!/usr/bin/env python3
"""Aggregate *why* baseline tests fail, across the whole suite.

Builds the release harness, runs it with --dump (tttt's rendered errors/symbols/types for every test land in
build/failstats/dump), compares against tsc's baselines and prints a one-screen report. A full per-test dump goes to
build/failstats/report.txt and build/failstats/tests.tsv (gitignored).

  ./failstats.py                 whole suite
  ./failstats.py -f 'async.*'    only tests matching the regex (same as the harness's -f)
  ./failstats.py --no-run        reuse the dump from the last run
  ./failstats.py --code TS2322   list tests where that code is missing/spurious (after a run)
  ./failstats.py --jsx           only tests with JSX (.tsx files, .ts ones with a .tsx unit or @jsx)
  ./failstats.py --syntax        where the parser first rejects valid syntax, by shape (after a run)
  ./failstats.py -n 25           rows per table
"""
import argparse, collections, os, re, shutil, subprocess, sys
from concurrent.futures import ProcessPoolExecutor

ROOT = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(ROOT, 'thirdparty/tsc/tests/baselines/reference')
OUT = os.path.join(ROOT, 'build/failstats')
DUMP = os.path.join(OUT, 'dump')

ANSI = re.compile(r'\x1b\][^\x1b]*\x1b\\|\x1b\[[0-9;]*m')
ERR_LINE = re.compile(r'^(?:(.*?)\((\d+),(\d+)\): )?error (TS\d+): (.*)$')


def read(path):
    try:
        with open(path, encoding='utf-8', errors='replace', newline='') as f:
            return f.read()
    except FileNotFoundError:
        return None


def parse_errors(text):
    """The summary lines at the top of an .errors.txt, as (file, line, col, code, message)"""
    out = []
    for line in (text or '').splitlines():
        if line.startswith('===='):
            break
        m = ERR_LINE.match(line)
        if m:
            out.append((m[1] or '', int(m[2] or 0), int(m[3] or 0), m[4], m[5]))
    return out


def line_ratio(ours, expected):
    # Multiset overlap of lines rather than a real diff: difflib is quadratic and some baselines are 50k lines
    a, b = collections.Counter(ours.splitlines()), collections.Counter(expected.splitlines())
    total = sum(a.values()) + sum(b.values())
    return 2 * sum((a & b).values()) / total if total else 1.0


def analyse(name):
    """Runs in a worker. Returns a dict describing one test that ran"""
    r = {'name': name}
    ours_err, exp_err = read(f'{DUMP}/{name}.errors.txt') or '', read(f'{REF}/{name}.errors.txt') or ''
    r['errors'] = ours_err == exp_err
    ours, exp = collections.Counter(parse_errors(ours_err)), collections.Counter(parse_errors(exp_err))
    ours_pos = collections.Counter(e[:4] for e in ours.elements())
    exp_pos = collections.Counter(e[:4] for e in exp.elements())
    r['missing'] = [e[3] for e in (exp_pos - ours_pos).elements()]
    r['spurious'] = [e[3] for e in (ours_pos - exp_pos).elements()]
    r['expected_errors'], r['our_errors'] = sum(exp.values()), sum(ours.values())
    # Same codes at the same places, so whatever differs is message text (mostly type printing) or related info
    r['text_only'] = not r['errors'] and not r['missing'] and not r['spurious']
    # Right codes on the right lines but at least one column is off
    r['column_only'] = not r['errors'] and not r['text_only'] and \
        collections.Counter((e[0], e[1], e[3]) for e in ours.elements()) == \
        collections.Counter((e[0], e[1], e[3]) for e in exp.elements())
    for comp in ('symbols', 'types'):
        o, e = read(f'{DUMP}/{name}.{comp}') or '', read(f'{REF}/{name}.{comp}') or ''
        r[comp] = o == e
        r[comp + '_ratio'] = 1.0 if o == e else line_ratio(o, e)
    return r


KEYWORDS = set("""async await abstract declare get set static readonly accessor using import export type enum namespace
module class function yield new super this in of as is satisfies keyof typeof infer unique asserts override public
private protected default from require global out const let var for if return do while switch case try catch finally
throw break continue with void delete interface extends implements""".split())


def syntax_gaps(n):
    """Histogram of the source text where tttt reports its first syntax error (TS1xxx) in tests where tsc reports no
    syntax error there. Identifiers are collapsed to `id` so that the rows are syntax shapes"""
    shapes, example = collections.Counter(), {}
    for f in sorted(os.listdir(DUMP)):
        if not f.endswith('.errors.txt'):
            continue
        name = f[:-len('.errors.txt')]
        expected = {e[:4] for e in parse_errors(read(f'{REF}/{f}'))}
        text = read(f'{DUMP}/{f}')
        first = next((e for e in parse_errors(text) if e[3].startswith('TS1') and e[:4] not in expected), None)
        if not first:
            continue
        # Find that file's section in the rendered output and pull out the offending line
        m = re.search(r'^==== ' + re.escape(first[0]) + r' \(\d+ errors\) ====\r?\n', text, re.M)
        if not m:
            continue
        source = [l[4:] for l in text[m.end():].splitlines() if l.startswith('    ') and not l[4:].lstrip().startswith('~')]
        # (squiggle-only lines are dropped above, so indices are source lines again)
        line = source[first[1] - 1] if first[1] - 1 < len(source) else ''
        snippet = line[first[2] - 1:][:16].strip()
        shape = re.sub(r'[A-Za-z_$][\w$]*', lambda w: w[0] if w[0] in KEYWORDS else 'id', snippet)
        shape = re.sub(r'"[^"]*"?|\'[^\']*\'?', '"s"', shape)
        shapes[shape] += 1
        example.setdefault(shape, f'{name}: {line.strip()[:60]}')
    print(f'{sum(shapes.values())} tests where tttt reports a syntax error tsc does not; text at the first one:')
    for shape, count in shapes.most_common(n):
        print(f'  {count:5}  {shape!r:22} eg {example[shape]}')


def normalise_log(line):
    line = ANSI.sub('', line).strip()
    line = re.sub(r"'[^']*'", "'_'", line)
    line = re.sub(r'(declaration|type|name|identifier) [A-Za-z_$][\w$.]*$', r'\1 _', line)
    return line


def parse_stderr(text):
    """Log lines the checker emitted, per test. Returns {test: Counter(normalised line)}"""
    logs, current = collections.defaultdict(collections.Counter), '<before first test>'
    for line in text.splitlines():
        if line.startswith('### TEST '):
            current = line[9:]
        elif line.strip():
            logs[current][normalise_log(line)] += 1
    return logs


def run_harness(filter_re):
    subprocess.run(['ninja', '-C', 'release'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL) \
        if os.path.exists(f'{ROOT}/release') else \
        subprocess.run(['./fasttest.sh', '-f', '^$', '--summary'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    shutil.rmtree(DUMP, ignore_errors=True)
    os.makedirs(DUMP)
    # A list of names is run in chunks: the harness's regex engine silently matches nothing past a few kB of pattern
    filters = [filter_re] if not isinstance(filter_re, list) else \
        ['(' + '|'.join(filter_re[i:i + 30]) + ')' for i in range(0, len(filter_re), 30)]
    code = 0
    with open(f'{OUT}/stderr.txt', 'wb') as err:
        for f in filters:
            cmd = ['./release/baseline_tests', '--summary', '--dump', DUMP] + (['-f', f] if f else [])
            code = code or subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=err).returncode
    return code


def table(title, counter, n, tests_by_key=None, width=100):
    lines = [title]
    for key, count in counter.most_common(n):
        extra = f'  in {len(tests_by_key[key])} tests' if tests_by_key else ''
        lines.append(f'  {count:6}{extra}  {str(key)[:width]}')
    if len(counter) > n:
        lines.append(f'  ... {len(counter) - n} more rows, {sum(counter.values())} total')
    return lines


# The names of the tests that contain JSX, as regexes (for -f): .tsx test files, and .ts ones with a .tsx/.jsx unit or `@jsx`
def jsx_filter():
    unit = re.compile(r'@filename:\s*\S+\.(tsx|jsx)\b|@jsx:', re.I)
    names = []
    for root, _, files in os.walk('thirdparty/tsc/tests/cases'):
        if 'fourslash' in root: continue
        for f in files:
            if f.endswith('.tsx') or (f.endswith('.ts') and unit.search(open(os.path.join(root, f), errors='replace').read())):
                names.append(re.escape(f.rsplit('.', 1)[0]))
    return sorted(names)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-f', '--filter')
    ap.add_argument('--no-run', action='store_true')
    ap.add_argument('--code')
    ap.add_argument('--syntax', action='store_true')
    ap.add_argument('-n', type=int, default=15)
    ap.add_argument('--jsx', action='store_true', help='only tests with JSX: .tsx files and .ts files with a .tsx unit or @jsx')
    args = ap.parse_args()
    if args.jsx:
        args.filter = jsx_filter()
    os.makedirs(OUT, exist_ok=True)

    if args.syntax:
        if not args.no_run:
            run_harness(args.filter)
        return syntax_gaps(args.n)

    exit_code = 0
    if not args.no_run and not args.code:
        exit_code = run_harness(args.filter)
    stderr = read(f'{OUT}/stderr.txt') or ''
    logs = parse_stderr(stderr)

    # Every test the harness considered announces itself on stderr, those that then ran have a dumped .errors.txt
    # (deduplicated: the harness reruns a test when the first run left allocations behind, eg a newly cached lib)
    considered = list(dict.fromkeys(l[9:] for l in stderr.splitlines() if l.startswith('### TEST ')))
    ran = [t for t in considered if os.path.exists(f'{DUMP}/{t}.errors.txt')]
    crashed_in = considered[-1] if exit_code != 0 and considered else None

    with ProcessPoolExecutor() as pool:
        results = list(pool.map(analyse, ran, chunksize=64))

    if args.code:
        for r in results:
            m, s = r['missing'].count(args.code), r['spurious'].count(args.code)
            if m or s:
                print(f'{r["name"]}: {m} missing, {s} spurious (of {len(r["missing"])} / {len(r["spurious"])})')
        return

    rep = []
    commit = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    rep.append(f'failstats @ {commit}' + (' (JSX tests)' if args.jsx else f' filter={args.filter}' if args.filter else ''))
    if crashed_in:
        rep.append(f'!!! HARNESS DIED (exit {exit_code}) while running {crashed_in}; everything after it is missing. '
                   f'See build/failstats/stderr.txt')

    # Tests that never ran: no .symbols/.types baseline although the test doesn't say @noTypesAndSymbols (tsc doesn't
    # produce them either, eg when the harness refuses the options). A test file with several configurations
    # ("@target: es5,es2015") is one test per configuration, named name(target=es5)
    ran_set = set(ran)
    not_run = [c for c in considered if c not in ran_set]
    files = {c.split('(')[0] for c in considered}
    errors_only = sum(1 for t in ran if not os.path.exists(f'{DUMP}/{t}.types'))
    rep.append(f'{len(files)} test files, {len(considered)} tests (configurations), {len(ran)} ran ({errors_only} of '
               f'them errors only: @noTypesAndSymbols), {len(not_run)} did not (no types/symbols baselines)')

    n = len(results) or 1
    full = sum(1 for r in results if r['errors'] and r['symbols'] and r['types'])
    rep.append(f'pass: all {full}  errors {sum(r["errors"] for r in results)}  symbols '
               f'{sum(r["symbols"] for r in results)}  types {sum(r["types"] for r in results)}  (of {len(results)})')

    # Checker log output (log_error & co: constructs tttt knows it can't handle)
    log_counts, log_tests = collections.Counter(), collections.defaultdict(set)
    for test, c in logs.items():
        for line, count in c.items():
            log_counts[line] += count
            log_tests[line].add(test)
    rep.append('')
    rep.append(f'checker log_error output: {sum(log_counts.values())} lines across {len(logs)} tests')
    rep += table('  top messages:', log_counts, args.n, log_tests)

    # Errors component
    fails = [r for r in results if not r['errors']]
    rep.append('')
    rep.append(f'errors component: {len(fails)} failing')
    cats = collections.Counter()
    for r in fails:
        if r['text_only']: cats['same codes+positions, message text differs'] += 1
        elif r['column_only']: cats['same codes+lines, column differs'] += 1
        elif r['expected_errors'] == 0: cats['tsc reports nothing, tttt reports errors (all spurious)'] += 1
        elif r['our_errors'] == 0: cats['tsc reports errors, tttt reports nothing'] += 1
        elif not r['spurious']: cats['only missing errors'] += 1
        elif not r['missing']: cats['only spurious errors'] += 1
        else: cats['both missing and spurious'] += 1
    rep += table('  by shape:', cats, 10)
    syntax = [r for r in fails if any(c.startswith('TS1') and len(c) == 6 for c in r['spurious'])]
    rep.append(f'  {len(syntax)} tests have a spurious TS1xxx, ie the parser rejects syntax tsc accepts (typechecking is '
               f'skipped for those, so their types/symbols fail too); {sum(1 for r in syntax if r["expected_errors"] == 0)} '
               f'of them expect no errors at all')
    missing, spurious, alone = collections.Counter(), collections.Counter(), collections.Counter()
    missing_t, spurious_t = collections.defaultdict(set), collections.defaultdict(set)
    for r in fails:
        for c in r['missing']: missing[c] += 1; missing_t[c].add(r['name'])
        for c in r['spurious']: spurious[c] += 1; spurious_t[c].add(r['name'])
        codes = set(r['missing']) | set(r['spurious'])
        if len(codes) == 1:
            kind = 'misplaced' if r['missing'] and r['spurious'] else 'missing' if r['missing'] else 'spurious'
            alone[f'{kind} {next(iter(codes))}'] += 1
    rep += table('  missing diagnostics (tsc has, tttt lacks) by code:', missing, args.n, missing_t)
    rep += table('  spurious diagnostics (tttt has, tsc lacks) by code:', spurious, args.n, spurious_t)
    rep += table('  tests whose errors diff involves a single code (would pass errors if fixed alone):', alone, args.n)

    for comp in ('symbols', 'types'):
        f = [r for r in results if not r[comp]]
        buckets = collections.Counter()
        for r in f:
            x = r[comp + '_ratio']
            buckets['>=95%' if x >= .95 else '80-95%' if x >= .8 else '50-80%' if x >= .5 else '<50%'] += 1
        mean = sum(r[comp + '_ratio'] for r in results) / n
        rep.append('')
        rep.append(f'{comp} component: {len(f)} failing, mean line match {mean:.1%}; failing tests by line match: ' +
                   ', '.join(f'{k}: {buckets[k]}' for k in ('>=95%', '80-95%', '50-80%', '<50%')))
        near = sorted((r for r in f if r[comp + '_ratio'] >= .95), key=lambda r: -r[comp + '_ratio'])
        rep.append(f'  nearly passing (full list in report.txt): ' + ' '.join(r['name'] for r in near[:args.n]))
        r_full = [f'{r[comp + "_ratio"]:.3f} {r["name"]}' for r in near]
        with open(f'{OUT}/near_{comp}.txt', 'w') as fh:
            fh.write('\n'.join(r_full) + '\n')

    text = '\n'.join(rep)
    print(text)
    with open(f'{OUT}/report.txt', 'w') as fh:
        fh.write(text + '\n\nnot run:\n' + '\n'.join(sorted(not_run)) + '\n')
    with open(f'{OUT}/tests.tsv', 'w') as fh:
        fh.write('name\terrors\tsymbols\ttypes\tsymbols_ratio\ttypes_ratio\tmissing\tspurious\tlog_lines\n')
        for r in results:
            fh.write('\t'.join([r['name'], str(int(r['errors'])), str(int(r['symbols'])), str(int(r['types'])),
                                f'{r["symbols_ratio"]:.3f}', f'{r["types_ratio"]:.3f}',
                                ','.join(sorted(r['missing'])), ','.join(sorted(r['spurious'])),
                                str(sum(logs.get(r['name'], {}).values()))]) + '\n')
    print(f'\nfull data: build/failstats/report.txt, tests.tsv, near_symbols.txt, near_types.txt, stderr.txt')


if __name__ == '__main__':
    main()
