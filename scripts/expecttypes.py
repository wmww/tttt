#!/usr/bin/env python3
"""`$ExpectType` at breadth (plan 44 stage 3 item 2): a *types* oracle over DefinitelyTyped. Every package with
`// $ExpectType` assertions in its tests is checked by tttt, which prints the type of each asserted node; that is
compared with what tsc prints for the same node (not with the comment: DT's CI runs a newer TypeScript). tsc's
side is cached in test/sweep/DefinitelyTyped.expect.jsonl.gz (`--tsc` regenerates it, ~2-10 minutes on 4 cores): the node
DefinitelyTyped's `expect` lint rule picks for each assertion (test/oracle's tttt-oracle --expect, tsgo 7.1), its span and kind, and
`typeToString(getTypeAtLocation(node), undefined, NoTruncation)` read after checking every file but the default libs
(tttt's order: union member order is creation order, and lazy getTypeAtLocation without the check made ~100
assertions differ by order alone; DT's own comments come from that lazier order). tttt gets the spans (`--expect-types FILE`) and
prints its own type for each.

Mismatches are bucketed with displaydiff's kinds (union order, import() qualification, any where tsc has a type...)
and ranked by assertions and packages.

  scripts/expecttypes.py                 # every package with assertions (~30 s on 4 cores)
  scripts/expecttypes.py -f '^react'     # the packages whose name matches
  scripts/expecttypes.py -k 'union'      # list the mismatches whose kind contains this
  scripts/expecttypes.py --last ...      # reuse the last run
"""
import argparse, collections, concurrent.futures, gzip, json, os, re, resource, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import sweep  # noqa: E402

DT = sweep.DT
ORACLE = os.path.join(ROOT, 'test/sweep/DefinitelyTyped.expect.jsonl.gz')
LAST = f'/tmp/expecttypes-{os.path.basename(ROOT)}.json'


def packages_with_assertions(filter=None):
    """The sweep's packages that have a `$ExpectType` in a file of their own (a versioned directory's files belong
    to it, not to the package above it)"""
    out = subprocess.run(['grep', '-rlE', '--include=*.ts', '--include=*.tsx', r'\$ExpectType', 'types'], cwd=DT,
                         stdout=subprocess.PIPE, text=True).stdout.split()
    owners = set()
    for f in out:
        parts = f.split('/')
        owners.add('/'.join(parts[:3]) if len(parts) > 3 and re.fullmatch(r'v\d+(\.\d+)?', parts[2]) else '/'.join(parts[:2]))
    return [p for p in sweep.list_packages(filter) if p in owners]


def run_tttt(package, asserts, timeout, mem_gb):
    """tttt's type for each asserted node: `--expect-types` reads `file line col endLine endCol` lines and prints
    `file:line:col<TAB>type` for each node it finds with that span"""
    limit = mem_gb << 30
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as spans:
        for f, l, c, el, ec, *_ in asserts:
            spans.write(f'{f}\t{l}\t{c}\t{el}\t{ec}\n')
    start = time.time()
    try:
        proc = subprocess.run([os.path.join(ROOT, 'release/tttt'), '-q', '--keep-going', '--threads', '1',
                               '--checkers', '1', '--noEmit', '--expect-types', spans.name, '-p', package], cwd=DT,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout,
                              env={**os.environ, 'TTTT_NO_REEXEC': '1'},
                              preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
        code = proc.returncode
        out, err = proc.stdout, proc.stderr
        status = 'ok' if code in (0, 1, 2) else f'exit {code}' if code > 0 else f'signal {-code}'
    except subprocess.TimeoutExpired as e:
        out, err, status = e.stdout or b'', e.stderr or b'', 'timeout'
    os.unlink(spans.name)
    types = {}
    for line in out.decode(errors='replace').split('\n'):
        m = re.match(r'(\S+):(\d+):(\d+)\t(.*)$', line)
        if m:
            types[f'{m.group(1)}:{m.group(2)}:{m.group(3)}'] = m.group(4)
    tail = '\n'.join(l for l in err.decode(errors='replace').split('\n') if l.strip())[-600:] if status != 'ok' else ''
    return {'package': package, 'status': status, 'wall': time.time() - start, 'types': types, 'tail': tail}


# The node kinds tttt's --expect-types finds (expressions); assertions on declarations (a type alias, a parameter, a
# binding element...) aren't compared yet
DECLARATION_KINDS = {'TypeAliasDeclaration', 'BindingElement', 'Parameter', 'PropertyAssignment', 'TypeReference',
                     'ReturnStatement', 'MethodDeclaration', 'ImportDeclaration', 'ImportEqualsDeclaration',
                     'VariableDeclaration', 'VariableStatement', 'FunctionDeclaration', 'ClassDeclaration',
                     'InterfaceDeclaration', 'PropertyDeclaration', 'ExportAssignment', 'ShorthandPropertyAssignment',
                     'IfStatement', 'ForOfStatement', 'ForStatement', 'Block', 'ExpressionWithTypeArguments',
                     'TypeQuery', 'Decorator', 'JsxAttribute', 'ModuleDeclaration', 'EnumDeclaration'}
TOKENS = re.compile(r'"(?:[^"\\]|\\.)*"|`[^`]*`|\w+|\S')


def classify(tsc, tttt, node_kind):
    """The kind of difference between tsc's text and tttt's, in displaydiff's terms where they apply"""
    if tttt is None:
        return f'node not found: {node_kind}'
    if tttt == 'any' and tsc != 'any':
        return 'we-any'
    if tsc == 'any':
        return 'tsc-any'
    split = lambda s: sorted(re.split(r'\s*\|\s*', s))
    if split(tsc) == split(tttt):
        return 'union order'
    if re.sub(r'import\("[^"]*"\)\.', '', tsc) == re.sub(r'import\("[^"]*"\)\.', '', tttt):
        return 'import() qualification'
    norm = lambda s: re.sub(r'\b(\w+)\.', '', s)
    if norm(tsc) == norm(tttt):
        return 'qualification'
    if re.sub(r'_\d+\b', '', tsc) == re.sub(r'_\d+\b', '', tttt):
        return 'T_1 renaming'
    if tttt == 'error' or tttt == 'unknown':
        return f'we-{tttt}'
    if tttt.startswith('typeof ') != tsc.startswith('typeof '):
        return 'typeof reuse'
    if sorted(TOKENS.findall(tsc)) == sorted(TOKENS.findall(tttt)):
        return 'order (same tokens)'
    if len(tsc) > 200 or len(tttt) > 200:
        return 'other (long)'
    return 'other'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-f', '--filter', help='regex the package name (types/NAME) must match (re.search)')
    ap.add_argument('-j', '--jobs', type=int, default=os.cpu_count())
    ap.add_argument('-n', type=int, default=30, help='rows per table')
    ap.add_argument('-k', '--key', help='list the mismatches whose kind contains this')
    ap.add_argument('--names', action='store_true', help='with -k: only the package names')
    ap.add_argument('--last', action='store_true', help="reuse the last run's results")
    ap.add_argument('--tsc', action='store_true', help="regenerate tsc's cached types (all packages, or -f's)")
    ap.add_argument('--timeout', type=int, default=60)
    ap.add_argument('--mem-gb', type=int, default=4)
    ap.add_argument('--no-build', action='store_true')
    args = ap.parse_args()

    subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-corpora.sh')], check=True)
    subprocess.run([sys.executable, os.path.join(ROOT, 'test/sweep_links.py'), DT], check=True)
    packages = packages_with_assertions(args.filter)

    if args.tsc:
        oracle = {}
        if os.path.exists(ORACLE):
            oracle = {r['project']: r for r in map(json.loads, gzip.open(ORACLE, 'rt'))}
        start = time.time()
        oracle.update(sweep.run_tsc(packages, args.jobs, ['--expect']))
        with gzip.open(ORACLE, 'wt') as f:
            for p in sorted(oracle):
                f.write(json.dumps(oracle[p], separators=(',', ':')) + '\n')
        print(f'tsc on {len(packages)} packages in {time.time() - start:.0f}s, written to {ORACLE}')
        return

    oracle = {r['project']: r for r in map(json.loads, gzip.open(ORACLE, 'rt'))}
    for r in oracle.values():
        r['asserts'] = [a for a in r.get('asserts', []) if a[5] not in DECLARATION_KINDS]
    packages = [p for p in packages if p in oracle and oracle[p]['asserts']]
    if args.last:
        wanted = set(packages)
        results = [r for r in json.load(open(LAST)) if r['package'] in wanted]
    else:
        if not args.no_build:
            subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
        start = time.time()
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            results = list(pool.map(lambda p: run_tttt(p, oracle[p]['asserts'], args.timeout, args.mem_gb), packages))
        print(f'tttt on {len(packages)} packages in {time.time() - start:.0f}s')
        with open(LAST, 'w') as f:
            json.dump(results, f)

    kinds = collections.defaultdict(lambda: {'asserts': 0, 'packages': set(), 'example': None})
    total = matched = 0
    clean_packages = 0
    failed = []
    for r in results:
        p = r['package']
        if r['status'] != 'ok':
            failed.append(r)
            continue
        ok = True
        for a in oracle[p]['asserts']:
            total += 1
            f, l, c, el, ec, node_kind, want = a[:7]
            got = r['types'].get(f'{f}:{l}:{c}')
            if got == want:
                matched += 1
                continue
            ok = False
            k = classify(want, got, node_kind)
            kinds[k]['asserts'] += 1
            kinds[k]['packages'].add(p)
            if kinds[k]['example'] is None:
                kinds[k]['example'] = f'{f}:{l}:{c} {node_kind}\n      tsc:  {want[:150]}\n      tttt: {(got or "")[:150]}'
            if args.key and args.key in k:
                if not args.names:
                    print(f'{f}:{l}:{c} {node_kind} [{k}]\n    tsc:  {want[:300]}\n    tttt: {got}'[:1200])
        if args.key and args.names and any(args.key in classify(a[6], r['types'].get(f'{a[0]}:{a[1]}:{a[2]}'), a[5])
                                           for a in oracle[p]['asserts'] if a[6] != r['types'].get(f'{a[0]}:{a[1]}:{a[2]}')):
            print(p)
        clean_packages += ok
    if args.key:
        return
    print(f'{total} assertions in {len(results)} packages: {matched} match ({100 * matched / max(total, 1):.1f}%), '
          f'{clean_packages} packages all match, {len(failed)} crash/timeout')
    for r in failed[:args.n]:
        print(f'\n{r["status"].upper()} {r["package"]}:\n    ' + r['tail'].replace('\n', '\n    ')[-400:])
    print(f'\n  {"asserts":>7} {"pkgs":>5}  kind / first example')
    for k, v in sorted(kinds.items(), key=lambda kv: -kv[1]['asserts'])[:args.n]:
        print(f'  {v["asserts"]:7} {len(v["packages"]):5}  {k}\n      {v["example"]}')
    print(f'\nresults saved for --last in {LAST}')


if __name__ == '__main__':
    sys.exit(main())
