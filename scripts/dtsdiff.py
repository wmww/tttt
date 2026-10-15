#!/usr/bin/env python3
"""The declaration emit oracle (plan 64): the `.d.ts` files tsgo writes for real projects against tttt's, byte for
byte. Projects: the pinned `-p` corpora (scripts/realworld.py's, rechecked with --declaration) and every source sweep
project (thirdparty/sources) whose config (extends resolved: tsgo --showConfig) sets `declaration` or `composite`, as
long as tsgo emits at least one file there. Both sides run with the same overrides:

  --noEmit false --declaration --emitDeclarationOnly --declarationMap false --noEmitOnError false
  --outDir T --declarationDir T --tsBuildInfoFile /tmp/...

and both run in a bubblewrap sandbox where only /tmp is writable: a file outside rootDir (which defaults to the
config's directory in TS 6+) has its output written beside its source, whatever outDir says (tsc's
getSourceFilePathInNewDir combines outDir with the absolute path). The first --tsc run without the sandbox wrote 4128
.d.ts files into thirdparty/sources. In the sandbox those fail with TS5033; the manifest lists them (`unwritten`).
tttt is run as the CLI (plan 64 stage 4: it writes declarations where the options only emit those), and what it wrote
under T is compared with what tsgo wrote, by path relative to T.

tsgo's side is cached in test/dts/manifests.jsonl.gz: per project the relative output paths and a sha256 prefix of each,
tsgo's exit code, the files tsgo reported declaration diagnostics in (tsgo doesn't write those), and the pins (the
checkout's commit, the oracle's). Hashes only: tsgo's texts are kept in /tmp/dtsdiff-tsgo-PIN/ (one gzipped JSON per
project) and regenerated on demand for the projects whose mismatches have to be classified or shown.

Mismatching files are bucketed by their first differing line (missing file, extra file, empty output, comment, import,
`export {}`, modifier, member order, type text, other), ranked by projects then files.

  scripts/dtsdiff.py                      # every project (tsgo texts regenerated once into /tmp when missing)
  scripts/dtsdiff.py -f '^corpus:'        # projects matching a regex (corpora are corpus:NAME, sweep ones REPO/path)
  scripts/dtsdiff.py -k 'type text'       # list the files of the buckets whose key contains this
  scripts/dtsdiff.py --names match        # the fully matching projects (or: mismatch, crash, a bucket)
  scripts/dtsdiff.py --show PROJECT [FILE]   # diffs of a project's files (FILE: a substring of the output path)
  scripts/dtsdiff.py --sample 4           # a fixed quarter of the projects (scripts/sourcesweep.py's hash)
  scripts/dtsdiff.py --last ...           # reuse the last run's results
  scripts/dtsdiff.py --tsc [-f ...]       # rediscover the projects and regenerate tsgo's manifests (all, or -f's)
"""
import argparse, collections, concurrent.futures, difflib, gzip, hashlib, json, os, re, resource, shutil, signal
import subprocess, sys, tempfile, time, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'scripts'))
import realworld, sourcesweep, sweep, tscformat  # noqa: E402

TSGO = os.path.join(ROOT, 'thirdparty/typescript/built/tsc')
TTTT = os.path.join(ROOT, 'release/tttt')
MANIFEST = os.path.join(ROOT, 'test/dts/manifests.jsonl.gz')
TREE = os.path.basename(ROOT)
LAST = f'/tmp/dtsdiff-{TREE}.json'
CORPUS_PREFIX = 'corpus:'
# Not zod-root (not clean, and tsgo's emit there takes 9 minutes, writing only outside rootDir) nor webpack (JavaScript
# sources: plan 64 stage 6; 8 minutes of tsgo)
SKIP_CORPORA = ('zod-root', 'webpack')
SANDBOX = ['bwrap', '--ro-bind', '/', '/', '--bind', '/tmp', '/tmp', '--dev', '/dev', '--proc', '/proc', '--']
HASH_LEN = 16  # (hex digits of sha256 kept: equality only)
# Codes of the declaration diagnostics: tsgo doesn't write a file it reports one of them in
DECLARATION_CODES = lambda c: 4000 <= c < 5000 or 9000 <= c < 10000 or c in (2742, 2883, 2527, 2526)


def oracle_pin():
    commit = re.search(r'^TS_COMMIT=(\w+)', open(os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')).read(), re.M)[1]
    return f'tsgo {commit[:8]}'


def repo_commits():
    commits = {}
    for line in open(sourcesweep.MANIFEST):
        if line.strip() and not line.startswith('#'):
            parts = line.split()
            commits[parts[0]] = parts[2] if len(parts) > 2 else ''
    return commits


def project_spec(name):
    """(cwd, -p argument) of a project"""
    if name.startswith(CORPUS_PREFIX):
        return ROOT, realworld.CORPORA[name[len(CORPUS_PREFIX):]]
    return sourcesweep.SOURCES, name


def project_dir(name):
    cwd, p = project_spec(name)
    path = os.path.realpath(os.path.join(cwd, p))
    return path if os.path.isdir(path) else os.path.dirname(path)


def checkout_pin(name):
    if name.startswith(CORPUS_PREFIX):
        return realworld.pin(name[len(CORPUS_PREFIX):]).split()[0]
    return repo_commits().get(name.split('/')[0], '')


def discover(filter=None, jobs=16):
    """The candidate projects: the -p corpora present, and the source sweep projects whose resolved config emits
    declarations"""
    corpora = [CORPUS_PREFIX + c for c in realworld.CORPORA if c not in realworld.BUILD_CORPORA and c not in SKIP_CORPORA and
               os.path.exists(os.path.join(ROOT, realworld.CORPORA[c]))]

    def emits(p):
        r = subprocess.run([TSGO, '-p', p, '--showConfig'], cwd=sourcesweep.SOURCES, capture_output=True, text=True)
        try:
            c = json.loads(r.stdout).get('compilerOptions', {})
        except ValueError:
            return False
        return bool(c.get('declaration') or c.get('composite'))
    sources = sourcesweep.list_projects()
    if filter:
        sources = [p for p in sources if re.search(filter, p)]
        corpora = [p for p in corpora if re.search(filter, p)]
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        flags = list(pool.map(emits, sources))
    return corpora + [p for p, e in zip(sources, flags) if e]


def overrides(out_root, buildinfo):
    out = os.path.join(out_root, *['o'] * 12)  # (Deep: ../ out of rootDir stays inside out_root)
    return out, ['--noEmit', 'false', '--declaration', '--emitDeclarationOnly', '--declarationMap', 'false',
                 '--noEmitOnError', 'false', '--outDir', out, '--declarationDir', out, '--tsBuildInfoFile', buildinfo]


def text_hash(data):
    return hashlib.sha256(data).hexdigest()[:HASH_LEN]


def tsgo_store(name):
    d = f'/tmp/dtsdiff-tsgo-{oracle_pin().split()[1]}'
    return d, os.path.join(d, hashlib.sha1(name.encode()).hexdigest()[:16] + '.json.gz')


def run_tsgo(name, timeout=900, mem_gb=16):
    """tsgo's declaration emit of the project: (manifest record, {relative path: text}). Its texts go to the store"""
    cwd, p = project_spec(name)
    tmp = tempfile.mkdtemp(prefix='dtsdiff-tsgo-run-')
    out, flags = overrides(os.path.join(tmp, 'out'), os.path.join(tmp, 'tsbuildinfo'))
    limit = mem_gb << 30
    start = time.time()
    try:
        proc = subprocess.run([*SANDBOX, TSGO, '-p', p, '--pretty', 'false', *flags], cwd=cwd, capture_output=True, text=True,
                              timeout=timeout, preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
        status, stdout = proc.returncode, proc.stdout
    except subprocess.TimeoutExpired:
        status, stdout = 'timeout', ''
    ms = int((time.time() - start) * 1000)
    texts = {}
    for d, _, files in os.walk(os.path.join(tmp, 'out')):
        for f in files:
            path = os.path.join(d, f)
            texts[os.path.relpath(path, out)] = open(path, 'rb').read().decode('utf-8', errors='surrogateescape')
    shutil.rmtree(tmp)
    unwritten = sorted({os.path.realpath(m) for m in re.findall(r"error TS5033: Could not write file '([^']+)'", stdout)})
    blocked = sorted({os.path.realpath(os.path.join(cwd, f)) for f, _, _, c, _ in tscformat.parse(stdout)
                      if f and DECLARATION_CODES(c)})
    record = {'project': name, 'status': status, 'ms': ms, 'pin': f'{checkout_pin(name)} {oracle_pin()}',
              'files': {r: text_hash(t.encode('utf-8', errors='surrogateescape')) for r, t in sorted(texts.items())},
              'blocked': blocked, 'unwritten': unwritten}
    if status not in (0, 1, 2):
        record['skip'] = f'tsgo exit {status}: {stdout[-300:]}'
    elif not texts:
        record['skip'] = 'no outputs'
    if texts:
        save_texts(name, texts)
    return record, texts


def save_texts(name, texts):
    d, path = tsgo_store(name)
    os.makedirs(d, exist_ok=True)
    with gzip.open(path + '.tmp', 'wt') as f:
        json.dump(texts, f)
    os.replace(path + '.tmp', path)


def tsgo_texts(name, record):
    """tsgo's texts of the project: from the store, else tsgo run again (and checked against the manifest)"""
    _, path = tsgo_store(name)
    if os.path.exists(path):
        return json.load(gzip.open(path, 'rt'))
    fresh, texts = run_tsgo(name)
    if fresh['files'] != record['files']:
        print(f'warning: tsgo now writes other declarations for {name} than its manifest says (rerun --tsc)',
              file=sys.stderr)
    return texts


def run_tttt(name, timeout, mem_gb):
    """tttt as tsc on the project with the overrides, in tsgo's sandbox: {output path relative to the out dir: text}
    (what it wrote, like tsgo's), status"""
    cwd, p = project_spec(name)
    tmp = tempfile.mkdtemp(prefix='dtsdiff-tttt-')
    out_dir, flags = overrides(os.path.join(tmp, 'out'), os.path.join(tmp, 'tsbuildinfo'))
    limit = mem_gb << 30
    start = time.time()
    try:
        proc = subprocess.run([*SANDBOX, TTTT, '-p', p, '--pretty', 'false', '--threads', '1', *flags],
                              cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
                              env={**os.environ, 'TTTT_NO_REEXEC': '1'},
                              preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
        code, out = proc.returncode, proc.stdout
        status = 'ok' if code in (0, 1, 2) else f'exit {code}' if code > 0 else f'signal {-code}'
    except subprocess.TimeoutExpired as e:
        status, out = 'timeout', e.stdout or b''
    wall = time.time() - start
    texts = {}
    for d, _, files in os.walk(os.path.join(tmp, 'out')):
        for f in files:
            path = os.path.join(d, f)
            texts[os.path.relpath(path, out_dir)] = open(path, 'rb').read().decode('utf-8', errors='surrogateescape')
    shutil.rmtree(tmp)
    tail = '' if status == 'ok' else '\n'.join(l for l in out.decode(errors='replace').split('\n') if l.strip())[-600:]
    return texts, status, wall, tail


def is_comment(line):
    s = line.strip()
    return s.startswith(('//', '/*', '*'))


MODIFIERS = re.compile(r'\b(export|declare|default|abstract|readonly|static|public|private|protected|async|override|'
                       r'accessor|const)\s+')


def classify(expected, actual):
    """The bucket of a mismatching file, from its first differing line: (bucket, line number, tsgo's, tttt's)"""
    if actual == '':
        return 'empty output', 1, expected.split('\n', 1)[0], ''
    a, b = expected.split('\n'), actual.split('\n')
    i = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
    x = a[i] if i < len(a) else '<end of file>'
    y = b[i] if i < len(b) else '<end of file>'
    if sorted(a) == sorted(b):
        bucket = 'member order'
    elif x.lstrip().startswith('/// <reference') or y.lstrip().startswith('/// <reference'):
        bucket = 'reference directive'
    elif is_comment(x) or is_comment(y):
        bucket = 'comment'
    elif re.match(r'\s*(import|export \* from|export \{[^}]*\} from)\b', x) or \
            re.match(r'\s*(import|export \* from|export \{[^}]*\} from)\b', y):
        bucket = 'import'
    elif x.strip() == 'export {};' or y.strip() == 'export {};':
        bucket = 'export {}'
    elif x != y and MODIFIERS.sub('', x) == MODIFIERS.sub('', y):
        bucket = 'modifier'
    elif i >= len(a) or i >= len(b):
        bucket = 'missing lines' if i >= len(b) else 'extra lines'
    else:
        head = lambda s: re.split(r'[:=(<]', s, maxsplit=1)[0]
        same_head = head(x) == head(y) and re.search(r'[:=(<]', x) and re.search(r'[:=(<]', y)
        bucket = 'type text' if same_head else 'other'
    return bucket, i + 1, x, y


def compare(name, record, tttt_texts, tsgo_texts_fn):
    """Per file of the project: (relative path, bucket or None, line, tsgo's line, tttt's line)"""
    files = record['files']
    tttt_hashes = {r: text_hash(t.encode('utf-8', errors='surrogateescape')) for r, t in tttt_texts.items()}
    rows, expected = [], None
    for rel, h in sorted(files.items()):
        if rel not in tttt_hashes:
            rows.append((rel, 'missing file', 0, '', ''))
        elif tttt_hashes[rel] == h:
            rows.append((rel, None, 0, '', ''))
        else:
            if expected is None:
                expected = tsgo_texts_fn()
            rows.append((rel, *classify(expected.get(rel, ''), tttt_texts[rel])))
    # (A file tsgo reported a declaration diagnostic in has no output: by its name without the extensions)
    blocked = {os.path.basename(re.sub(r'\.[cm]?tsx?$', '', f)) for f in record.get('blocked', [])}
    for rel in sorted(set(tttt_texts) - set(files)):
        stem = os.path.basename(re.sub(r'\.d\.[cm]?ts$', '', rel))
        rows.append((rel, 'extra file (tsgo: declaration diagnostic)' if stem in blocked else 'extra file', 0, '', ''))
    return rows


def load_manifest():
    if not os.path.exists(MANIFEST):
        return {}
    return {r['project']: r for r in map(json.loads, gzip.open(MANIFEST, 'rt'))}


def write_manifest(records):
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with gzip.open(MANIFEST, 'wt') as f:
        for p in sorted(records):
            f.write(json.dumps(records[p], separators=(',', ':')) + '\n')


def regenerate(args):
    subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')], check=True)
    start = time.time()
    projects = discover(args.filter, args.jobs)
    print(f'{len(projects)} candidate projects ({time.time() - start:.0f}s to discover)', flush=True)
    records = load_manifest() if args.filter else {}
    done = 0
    # (The corpora first: the big ones shouldn't end up alone at the end)
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for future in concurrent.futures.as_completed([pool.submit(run_tsgo, p) for p in projects]):
            record, _ = future.result()
            records[record['project']] = record
            done += 1
            if done % 100 == 0:
                print(f'  tsgo: {done} / {len(projects)} projects, {time.time() - start:.0f}s', flush=True)
    write_manifest(records)
    kept = [r for r in records.values() if 'skip' not in r]
    skipped = collections.Counter(r['skip'].split(':')[0] for r in records.values() if 'skip' in r)
    print(f'tsgo on {len(projects)} projects in {time.time() - start:.0f}s: {len(kept)} emit '
          f'{sum(len(r["files"]) for r in kept)} files; skipped: {dict(skipped)}; written to {MANIFEST}')


def show(args, records):
    name = args.show[0]
    if name not in records:
        sys.exit(f'{name}: not in the manifest')
    record = records[name]
    texts, status, _, tail = run_tttt(name, args.timeout, args.mem_gb)
    if status != 'ok':
        print(f'tttt: {status}\n{tail}')
    expected = tsgo_texts(name, record)
    want = args.show[1] if len(args.show) > 1 else None
    for rel in sorted(set(record['files']) | set(texts)):
        if want and want not in rel:
            continue
        got = texts.get(rel)
        exp = expected.get(rel)
        if got == exp:
            continue
        diff = difflib.unified_diff((exp or '').splitlines(True), (got or '').splitlines(True),
                                    f'tsgo/{rel}' if exp is not None else '/dev/null',
                                    f'tttt/{rel}' if got is not None else '/dev/null')
        sys.stdout.writelines(l if l.endswith('\n') else l + '\n' for l in diff)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-f', '--filter', help='regex the project (corpus:NAME or REPO/path) must match (re.search)')
    ap.add_argument('-j', '--jobs', type=int, default=os.cpu_count())
    ap.add_argument('-n', type=int, default=25, help='rows per table')
    ap.add_argument('-k', '--key', help='list the files of the buckets whose name contains this')
    ap.add_argument('--names', help='list the projects: match, mismatch, crash, or those with files in a bucket')
    ap.add_argument('--show', nargs='+', metavar=('PROJECT', 'FILE'), help="diff one project's files")
    ap.add_argument('--last', action='store_true', help="reuse the last run's results")
    ap.add_argument('--tsc', action='store_true', help="rediscover the projects and regenerate tsgo's manifests")
    ap.add_argument('--sample', type=int, help='only the projects whose name hashes to 0 modulo this')
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--mem-gb', type=int, default=16)
    ap.add_argument('--no-build', action='store_true')
    args = ap.parse_args()
    global LAST

    if args.tsc:
        if subprocess.run([*SANDBOX, 'true']).returncode:
            sys.exit('bwrap (bubblewrap) is needed to keep tsgo from writing into the checkouts')
        return regenerate(args)
    records = load_manifest()
    if not records:
        sys.exit(f'no manifest at {MANIFEST}: run --tsc')
    if not args.no_build and not args.last:
        subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
    if args.show:
        return show(args, records)
    skipped = [p for p, r in records.items() if 'skip' in r]
    projects = sorted(p for p, r in records.items() if 'skip' not in r)
    if args.filter:
        projects = [p for p in projects if re.search(args.filter, p)]
    if args.sample:
        projects = [p for p in projects if zlib.crc32(p.encode()) % args.sample == 0]
        LAST = LAST.replace('.json', f'-sample{args.sample}.json')
    stale = [p for p in projects if records[p]['pin'] != f'{checkout_pin(p)} {oracle_pin()}']
    if stale:
        print(f'STALE cache: {len(stale)} projects pinned otherwise (rerun --tsc): {" ".join(stale[:5])}',
              file=sys.stderr)

    if args.last:
        results = [r for r in json.load(open(LAST)) if r['project'] in set(projects)]
        elapsed = None
    else:
        start = time.time()

        def one(p):
            texts, status, wall, tail = run_tttt(p, args.timeout, args.mem_gb)
            rows = compare(p, records[p], texts, lambda: tsgo_texts(p, records[p])) if status == 'ok' else []
            return {'project': p, 'status': status, 'wall': wall, 'tail': tail, 'rows': rows}
        order = sorted(projects, key=lambda p: -records[p]['ms'])
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            results = sorted(pool.map(one, order), key=lambda r: r['project'])
        elapsed = time.time() - start
        with open(LAST, 'w') as f:
            json.dump(results, f)

    verdicts = collections.defaultdict(list)
    buckets = collections.defaultdict(lambda: {'projects': set(), 'files': 0, 'example': None})
    files_total = files_match = missing = extra = 0
    for r in results:
        p = r['project']
        if r['status'] != 'ok':
            verdicts['crash'].append(p)
            files_total += len(records[p]['files'])
            continue
        bad = [row for row in r['rows'] if row[1]]
        verdicts['mismatch' if bad else 'match'].append(p)
        for rel, bucket, line, x, y in r['rows']:
            if not bucket or not bucket.startswith('extra file'):
                files_total += 1
            if not bucket:
                files_match += 1
                continue
            missing += bucket == 'missing file'
            extra += bucket.startswith('extra file')
            b = buckets[bucket]
            b['projects'].add(p)
            b['files'] += 1
            if b['example'] is None:
                b['example'] = f'{p}: {rel}' + (f':{line}' if line else '')
            if args.key and args.key in bucket and not args.names:
                print(f'{p}  {rel}' + (f':{line}\n    tsgo: {x[:160]}\n    tttt: {y[:160]}' if line else f'  ({bucket})'))
            if args.names and args.names in bucket:
                verdicts[args.names].append(p)
    if args.key and not args.names:
        return
    if args.names:
        print('\n'.join(sorted(set(verdicts.get(args.names, [])))))
        return

    print(f'dts: projects {len(verdicts["match"])} / {len(results)} fully match, files {files_match} / {files_total} '
          f'match; {missing} tsgo writes and tttt not, {extra} tttt writes and tsgo not, {len(verdicts["crash"])} '
          f'crash/timeout ({len(skipped)} discovered projects skipped: tsgo failed or emitted nothing)')
    if elapsed is not None:
        print(f'tttt {sum(r["wall"] for r in results):.0f}s single-threaded in total ({elapsed:.0f}s wall); '
              f'{sweep.load_note()}')
    for p in verdicts['crash'][:args.n]:
        r = next(r for r in results if r['project'] == p)
        print(f'\nCRASH {p} ({r["status"]}):\n    ' + r['tail'].replace('\n', '\n    ')[-400:])
    print('\nbuckets by projects, then files (first differing line):')
    print(f'  {"projs":>5} {"files":>6}  bucket / example')
    for k, b in sorted(buckets.items(), key=lambda kb: (-len(kb[1]['projects']), -kb[1]['files']))[:args.n]:
        print(f'  {len(b["projects"]):5} {b["files"]:6}  {k}\n{"":16}{b["example"][:130]}')
    worst = sorted((r for r in results if r['status'] == 'ok'),
                   key=lambda r: (-sum(1 for row in r['rows'] if row[1]), r['project']))
    print('\nprojects by mismatching files:')
    for r in worst[:args.n]:
        bad = sum(1 for row in r['rows'] if row[1])
        if bad:
            print(f'  {r["project"]:60} {len(r["rows"]) - bad:5} / {len(r["rows"]):5} match')
    print(f'\nresults saved for --last in {LAST}')


if __name__ == '__main__':
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    sys.exit(main())
