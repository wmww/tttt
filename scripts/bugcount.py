#!/usr/bin/env python3
"""The known-correctness-bugs headline: how many root causes are open (from the status tags of agents/bugs/*.md and
agents/triage.md, agents/bugs/README.md), next to the symptoms the oracles count (the saved suite results and each
breadth oracle's `--last` cache), and the oracle root causes no entry explains yet.

  scripts/bugcount.py            # headline, symptom table, unexplained keys (seconds: caches only)
  scripts/bugcount.py --save     # also append a dated line to agents/measurements.md "Known correctness bugs"
  scripts/bugcount.py --run      # first rerun the cheap oracles: the suite (fasttest.sh -s) and buncases.py
  scripts/bugcount.py --list TAG # the entries with that tag (open, design, text, tsgo, fixed, dup, note, harness,
                                 # untagged), file:line and their first words
  scripts/bugcount.py -v         # the unexplained keys in full, with an instance each

Each oracle's cache is /tmp/NAME-TREE.json of this tree; when there is none, the newest of any worktree's (its tree
and age are printed: another worktree's run measures that worktree's binary). The unexplained part: a mismatching
diagnostic of the DT sweep or the source sweep is explained when an entry that is not `[fixed]` mentions its code
(`TS2322`) and its file's basename or its project's repository (or package) name; one of bun's cases when an entry
mentions its code and the case id. A root-cause key none of whose diagnostics is explained is unexplained; one
explained only by `[fixed]` entries is listed as possibly regressed.
"""
import argparse, collections, concurrent.futures, contextlib, datetime, glob, importlib.util, io, json, os, re
import subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TREE = os.path.basename(ROOT)
RESULTS = os.path.join(ROOT, 'test/baseline_results.ts')
MEASUREMENTS = os.path.join(ROOT, 'agents/measurements.md')
SECTION = '## Known correctness bugs'
TAG = re.compile(r'\[(open|design|tsgo|text|harness|note|dup|fixed)(?: ([^\]]+))?\]\s*$')
COUNTED = ('open', 'design')
HUMAN = ('openclaw', 'opencode', 'osis')  # the human's projects, when the source sweep has them
PREFERRED = [TREE, 'tttt', 'wt-gate']  # whose caches count: this tree's, main's, the merger's (--tree overrides)


def entry_files():
    files = sorted(glob.glob(os.path.join(ROOT, 'agents/bugs/*.md')))
    files = [f for f in files if os.path.basename(f) not in ('perf_leads.md', 'README.md')]
    return files + [os.path.join(ROOT, 'agents/triage.md')]


def indent(line):
    return len(line) - len(line.lstrip())


def parse(path):
    """(entries, untagged): an entry is a top-level bullet, a sub-bullet or a paragraph whose last line ends with a
    status tag (a tag on a line that ends several blocks belongs to the innermost); every top-level bullet needs one
    unless one of its sub-bullets has one (a container)"""
    lines = open(path).read().split('\n')
    blocks, fence = [], False
    for i, l in enumerate(lines):
        if l.startswith('```'):
            fence = not fence
            continue
        if fence or not l.strip() or l.startswith('#'):
            continue
        bullet = re.match(r'\s*[-*] ', l) is not None
        if not bullet and indent(l):  # (a bullet's continuation)
            continue
        if not bullet and blocks and not blocks[-1]['bullet'] and blocks[-1]['start'] <= i + 1 <= blocks[-1]['end']:
            continue  # (the paragraph's next line)
        j = i
        while j + 1 < len(lines):
            nx = lines[j + 1]
            if not nx.strip() or nx.startswith('#') or nx.startswith('```'):
                break
            if (indent(nx) <= indent(l)) if bullet else re.match(r'\s*[-*] ', nx):
                break
            j += 1
        blocks.append({'start': i + 1, 'end': j + 1, 'bullet': bullet, 'top': bullet and indent(l) == 0,
                       'text': lines[i:j + 1], 'file': os.path.relpath(path, ROOT), 'tag': None, 'ref': None})
    for b in blocks:
        b['words'] = ' '.join(x.strip() for x in b['text'])
        b['section'] = max([n for n, l in enumerate(lines, 1) if l.startswith('#') and n < b['start']], default=0)
    for n, l in enumerate(lines, 1):
        m = TAG.search(l)
        if not m:
            continue
        owners = [b for b in blocks if b['end'] == n]
        if owners:
            b = max(owners, key=lambda b: b['start'])
            b['tag'], b['ref'] = m.group(1), m.group(2)
    entries = [b for b in blocks if b['tag']]
    # What an entry mentions, for the unexplained keys: its own text, its section's heading and untagged prose
    for e in entries:
        prose = [b['words'] for b in blocks if b['section'] == e['section'] and not b['bullet'] and not b['tag']]
        e['context'] = ' '.join([lines[e['section'] - 1] if e['section'] else '', e['words']] + prose)
    untagged = [b for b in blocks if b['top'] and not b['tag'] and not any(
        c['tag'] for c in blocks if c is not b and b['start'] < c['start'] <= b['end'])]
    return entries, untagged


def all_entries():
    entries, untagged = [], []
    for f in entry_files():
        e, u = parse(f)
        entries += e
        untagged += u
    return entries, untagged


# ---- the oracles' caches ----------------------------------------------------------------------------------------

def find_cache(prefix, variants=(), trees=None):
    """The cache to read: the full run's of the first tree in TREES that has one (this tree, main's, the merger's
    wt-gate), then each variant's (e.g. '-sample4') the same way; a file under half the size of the largest of its
    kind is a filtered (-f) run and skipped. Returns (path, tree, age in hours) or None"""
    trees = trees or PREFERRED
    for suffix in ('',) + tuple(variants):
        sizes = [os.path.getsize(p) for p in glob.glob(f'/tmp/{prefix}-*{suffix}.json')]
        ok = [(tree, f'/tmp/{prefix}-{tree}{suffix}.json') for tree in trees]
        ok = [(t, p) for t, p in ok if os.path.exists(p) and os.path.getsize(p) >= 0.5 * max(sizes)]
        if ok:
            # (This tree's own run first, else the newest of the others)
            tree, p = ok[0] if ok[0][0] == trees[0] else max(ok, key=lambda tp: os.path.getmtime(tp[1]))
            return p, tree + suffix, (time.time() - os.path.getmtime(p)) / 3600
    return None


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(ROOT, 'scripts', name + '.py'))
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.join(ROOT, 'scripts'))
    spec.loader.exec_module(m)
    return m


def run_last(name, cache):
    """One oracle's `--last` report on CACHE, in this (worker) process: stdout, and every (key, project, diag,
    spurious) its root_cause() saw. The scripts' set-up steps that fetch or build are skipped, and the project
    listings that walk the checkouts are replaced by the cache's own projects"""
    m = load_script(name)
    m.LAST = cache
    real = subprocess.run

    def quiet_run(cmd, *a, **k):
        s = cmd if isinstance(cmd, str) else ' '.join(map(str, cmd))
        if 'ensure-' in s or 'sweep_links' in s or s.startswith('ninja'):
            return subprocess.CompletedProcess(cmd, 0, '', '')
        return real(cmd, *a, **k)
    m.subprocess.run = quiet_run
    cached = lambda key: sorted({r[key] for r in json.load(open(cache))})
    if name == 'sweep':
        m.list_packages = lambda *a, **k: cached('package')
    elif name == 'sourcesweep':
        m.list_projects = lambda *a, **k: cached('package')
        m.list_repos = lambda *a, **k: []
    elif name == 'expecttypes':
        m.packages_with_assertions = lambda *a, **k: cached('package')
    seen = []
    if hasattr(m, 'root_cause'):
        inner = m.root_cause

        def recording(project, diag, spurious):
            k = inner(project, diag, spurious)
            seen.append((k, project, list(diag[:3]), spurious))
            return k
        m.root_cause = recording
    sys.argv = [name, '--last', '-n', '100000']
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            m.main()
    except SystemExit:
        pass
    finally:
        m.subprocess.run = real
    return out.getvalue(), seen


def clidiff_counts(cache):
    rows = json.load(open(cache))
    return collections.Counter(r['verdict'] for r in rows), len(rows)


ORACLES = {
    # name: (cache prefix, fallback variants, how to read it)
    'buncases': ('buncases', ()),
    'sweep': ('sweep', ()),
    'sourcesweep': ('sourcesweep', ('-sample4',)),
    'expecttypes': ('expecttypes', ()),
    'dtsdiff': ('dtsdiff', ()),
    'flagflip': ('flagflip', ()),
}


def gather_oracles():
    found = {n: find_cache(p, v) for n, (p, v) in ORACLES.items()}
    out = {}
    with concurrent.futures.ProcessPoolExecutor(len(found)) as pool:
        futures = {n: pool.submit(run_last, n, c[0]) for n, c in found.items() if c}
        for n, c in found.items():
            if not c:
                out[n] = None
                continue
            try:
                text, seen = futures[n].result()
            except Exception as e:  # noqa: BLE001 (a cache of another format: report, don't die)
                text, seen = f'error: {e!r}', []
            out[n] = {'cache': c[0], 'tree': c[1], 'age': c[2], 'text': text, 'seen': seen}
    c = find_cache('clidiff')
    out['clidiff'] = None if not c else {'cache': c[0], 'tree': c[1], 'age': c[2], 'counts': clidiff_counts(c[0])}
    return out


# ---- the suite --------------------------------------------------------------------------------------------------

def suite_results():
    text = open(RESULTS).read()
    rows = re.findall(r"'([^']*)': \{\s*errors: (true|false|undefined),\s*symbols: (true|false|undefined),"
                      r"\s*types: (true|false|undefined),(?:\s*dts: (true|false),)?", text)
    origin = collections.defaultdict(lambda: [0, 0])
    failing_own = []
    for name, e, s, t, d in rows:
        kind = 'tttt' if name.startswith('tttt') else 'bun' if name.startswith('bun') else 'tsc'
        ok = e == s == t == 'true' and d != 'false'
        origin[kind][0] += ok
        origin[kind][1] += 1
        if not ok and kind == 'tttt':
            failing_own.append(name)
    return origin, failing_own


# ---- the report -------------------------------------------------------------------------------------------------

def stale_note(o):
    if o is None:
        return None
    tree = '' if o['tree'] == TREE else f", {o['tree']}'s"
    return f"({o['age']:.0f} h old{tree})" if o['age'] >= 1 or tree else ''


def first(pattern, text, default=None):
    m = re.search(pattern, text, re.M)
    return m if m else default


def key_rows(text):
    """The rows of a root-cause table: (key, count) from lines `  N  ONLY  DIAGS  key`"""
    rows = re.findall(r'^\s+(\d+)\s+(\d+)\s+(\d+)  ([-+]TS\d+.*)$', text, re.M)
    return [(k, int(n)) for n, _, _, k in rows]


def explained(entries, code, names):
    """Which tags of the entries mention code and one of names"""
    tags = set()
    for e in entries:
        w = e['context']
        if f'TS{code}' in w and any(n and n in w for n in names):
            tags.add(e['tag'])
    return tags


def unexplained(entries, oracle, name):
    """Root-cause keys of one oracle none of whose diagnostics an entry explains: [(key, instances, example,
    fixed-only)]"""
    by_key = collections.defaultdict(list)
    for k, project, diag, sp in oracle['seen']:
        by_key[k].append((project, diag, sp))
    out = []
    memo = {}
    for k, inst in by_key.items():
        tags = set()
        for project, diag, sp in inst:
            if name == 'buncases':
                names = (project,)
            else:
                repo = project.split('/')[1] if project.startswith('types/') else project.split('/')[0]
                names = (os.path.basename(diag[0]), repo)
            key = (diag[2], names)
            if key not in memo:
                memo[key] = explained(entries, diag[2], names)
            tags |= memo[key]
        live = tags - {'fixed'}
        if not live:
            p, d, _ = inst[0]
            out.append((k, len(inst), f'{p}  {d[0]}:{d[1]}', 'fixed' in tags))
    return sorted(out, key=lambda r: -r[1]), len(by_key)


def human_state(oracle):
    """The human's projects in the source sweep: per repo, projects matching / total and the mismatching ones"""
    rows = json.load(open(oracle['cache']))
    bad = collections.defaultdict(set)
    for k, project, diag, sp in oracle['seen']:
        bad[project].add(k)
    timeouts = set(re.findall(r'^(?:TIMEOUT|CRASH) (\S+)', oracle['text'], re.M))
    out = []
    for h in HUMAN:
        projects = sorted(r['package'] for r in rows if r['package'] == h or r['package'].startswith(h + '/'))
        if not projects:
            out.append(f'{h}: not in the sweep')
            continue
        wrong = [p for p in projects if p in bad or p in timeouts]
        out.append(f'{h}: {len(projects) - len(wrong)} / {len(projects)} projects match' +
                   (f' ({len([p for p in wrong if p in timeouts])} timed out)' if timeouts & set(wrong) else ''))
    return out


def report(args):
    entries, untagged = all_entries()
    tags = collections.Counter(e['tag'] for e in entries)
    in_triage = collections.Counter(e['tag'] for e in entries if e['file'].endswith('triage.md'))
    n_open = tags['open'] + tags['design']
    headline = (f"known correctness bugs: {n_open} root causes open ({tags['design']} of them designs), "
                f"{tags['text']} text-only, {tags['tsgo']} argued tsgo artifacts excluded")
    lines = [headline,
             f"  (entries: {sum(tags.values())} tagged; also {tags['fixed']} fixed, {tags['dup']} duplicates, "
             f"{tags['note']} notes, {tags['harness']} harness/tooling; triage.md's share of the open: "
             f"{in_triage['open'] + in_triage['design']}; agents/bugs/README.md has the tags)"]
    if untagged:
        lines.append(f'  {len(untagged)} untagged bullets (tag them):')
        lines += [f"    {u['file']}:{u['start']}  {u['words'][:90]}" for u in untagged[:20]]

    t0 = time.time()
    oracles = gather_oracles()
    origin, failing_own = suite_results()
    mentioned = lambda name: any(name.split('(')[0] in e['context'] for e in entries
                                 if e['tag'] in COUNTED + ('tsgo', 'text', 'dup'))
    with_entry = [n for n in failing_own if mentioned(n)]
    sym = []  # (label, value, source)
    tsc, own, bun = origin['tsc'], origin['tttt'], origin['bun']
    sym.append(('suite: tsc tests failing', f'{tsc[1] - tsc[0]} / {tsc[1]}', 'test/baseline_results.ts'))
    sym.append(('suite: tttt own tests failing', f'{own[1] - own[0]} / {own[1]} ({len(with_entry)} with a bugs '
                f'entry: symptoms of counted causes)', 'test/baseline_results.ts'))
    sym.append(('suite: bun cases failing', f'{bun[1] - bun[0]} / {bun[1]}', 'test/baseline_results.ts'))
    saved = {'suite_tsc': tsc[1] - tsc[0], 'suite_own': own[1] - own[0], 'suite_bun': bun[1] - bun[0]}

    def missing(name, how):
        sym.append((name, 'stale: no cache', f'run {how}'))

    o = oracles['buncases']
    if o and o['text'].startswith(tuple('0123456789')):
        m = first(r'(\d+) cases .*?match (\d+) .*?spurious (\d+), missing (\d+), both (\d+), crash (\d+), timeout (\d+)',
                  o['text'])
        keys = len(key_rows(o['text']))
        bad = int(m.group(1)) - int(m.group(2))
        sym.append(('bun cases mismatching (file, line, code)', f'{bad} / {m.group(1)} ({m.group(6)} crash), '
                    f'{keys} root-cause keys', f"scripts/buncases.py --last {stale_note(o)}".rstrip()))
        saved['bun'] = bad
    else:
        missing('bun cases', 'scripts/buncases.py')
    o = oracles['sweep']
    if o and (m := first(r'^(\d+) packages \(\d+ clean under tsc\): match (\d+)', o['text'])):
        sym.append(('DT sweep packages mismatching', f'{int(m.group(1)) - int(m.group(2))} / {m.group(1)}, '
                    f'{len(key_rows(o["text"]))} keys', f'scripts/sweep.py --last {stale_note(o)}'.rstrip()))
        saved['dt'] = int(m.group(1)) - int(m.group(2))
    else:
        missing('DT sweep', 'scripts/sweep.py')
    o = oracles['sourcesweep']
    if o and (m := first(r'^(\d+) / (\d+) projects match.*?(\d+) / (\d+) repos', o['text'])):
        bad = int(m.group(2)) - int(m.group(1))
        sample = ' (a quarter sample)' if 'sample' in o['cache'] else ''
        sym.append(('source sweep projects mismatching' + sample,
                    f'{bad} / {m.group(2)} ({int(m.group(4)) - int(m.group(3))} repos), {len(key_rows(o["text"]))} keys',
                    f'scripts/sourcesweep.py --last {stale_note(o)}'.rstrip()))
        saved['source'] = bad
        for h in human_state(o):
            sym.append(('  the human\'s ' + h.split(':')[0], h.split(': ', 1)[1], 'source sweep'))
    else:
        missing('source sweep', 'scripts/sourcesweep.py')
    o = oracles['expecttypes']
    if o and (m := first(r'^(\d+) assertions in \d+ packages: (\d+) match', o['text'])):
        sym.append(('$ExpectType assertions mismatching', f'{int(m.group(1)) - int(m.group(2))} / {m.group(1)}',
                    f'scripts/expecttypes.py --last {stale_note(o)}'.rstrip()))
        saved['expect'] = int(m.group(1)) - int(m.group(2))
    else:
        missing('$ExpectType', 'scripts/expecttypes.py')
    o = oracles['dtsdiff']
    if o and (m := first(r'projects (\d+) / (\d+) fully match', o['text'])):
        sym.append(('dtsdiff projects mismatching', f'{int(m.group(2)) - int(m.group(1))} / {m.group(2)}',
                    f'scripts/dtsdiff.py --last {stale_note(o)}'.rstrip()))
        saved['dts'] = int(m.group(2)) - int(m.group(1))
    else:
        missing('dtsdiff', 'scripts/dtsdiff.py')
    o = oracles['flagflip']
    if o and (m := first(r'^(\d+) cells: .*missing (\d+), spurious (\d+), other code (\d+)', o['text'])):
        cells = len(re.findall(r'^\s+(\d+)\s+(\d+)  \S', o['text'].split('root causes', 1)[-1], re.M))
        sym.append(('flagflip diagnostics missing / spurious / other code', f'{m.group(2)} / {m.group(3)} / '
                    f'{m.group(4)} in {m.group(1)} cells, {cells} keys', f'scripts/flagflip.py --last {stale_note(o)}'.rstrip()))
        saved['flagflip'] = int(m.group(2)) + int(m.group(3)) + int(m.group(4))
    else:
        missing('flagflip', 'scripts/flagflip.py')
    o = oracles['clidiff']
    if o:
        counts, total = o['counts']
        other = total - counts['match'] - counts['text']
        sym.append(('clidiff projects: text / other mismatches', f"{counts['text']} / {other} of {total}",
                    f'scripts/clidiff.py --last {stale_note(o)}'.rstrip()))
        saved['clidiff_text'] = counts['text']
    else:
        missing('clidiff', 'scripts/clidiff.py')

    lines.append('')
    lines.append('symptoms (each oracle\'s own unit; a cache from another tree measures that tree\'s binary):')
    w = max(len(s[0]) for s in sym)
    lines += [f'  {a:{w}}  {b}   [{c}]' for a, b, c in sym]
    if failing_own:
        lines.append(f'  own tests failing: ' + ', '.join(n + ('' if n in with_entry else ' (no entry)')
                                                       for n in failing_own))

    lines.append('')
    lines.append('unexplained (oracle root-cause keys no non-fixed entry mentions: code + file basename or repo; '
                 'bun: code + case id):')
    for name, label in (('sweep', 'DT sweep'), ('sourcesweep', 'source sweep'), ('buncases', 'bun cases')):
        o = oracles[name]
        if not o:
            lines.append(f'  {label}: no cache')
            continue
        rows, nkeys = unexplained(entries, o, name)
        saved[f'unexplained_{name}'] = len(rows)
        regressed = [r for r in rows if r[3]]
        lines.append(f'  {label}: {len(rows)} of {nkeys} keys unexplained' +
                     (f' ({len(regressed)} mentioned only by [fixed] entries: regressed?)' if regressed else ''))
        for k, n, ex, fixed_only in rows[:None if args.verbose else 8]:
            lines.append(f'    {n:5}  {k[:90]}{"  [fixed only]" if fixed_only else ""}' +
                         (f'\n           {ex[:110]}' if args.verbose else ''))
        if not args.verbose and len(rows) > 8:
            lines.append(f'    ... {len(rows) - 8} more (-v)')
    lines.append(f'\n(oracle caches read in {time.time() - t0:.0f} s)')
    return headline, tags, saved, lines


def save(headline, tags, saved):
    commit = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short=9', 'HEAD'], capture_output=True,
                            text=True).stdout.strip()
    date = datetime.date.today().isoformat()
    parts = [f"{k} {v}" for k, v in saved.items()]
    line = (f"- {date} {commit}: {tags['open'] + tags['design']} open ({tags['design']} designs), {tags['text']} "
            f"text-only, {tags['tsgo']} tsgo excluded; symptoms: " + ', '.join(parts))
    text = open(MEASUREMENTS).read()
    if SECTION not in text:
        intro = (f'{SECTION}\n`scripts/bugcount.py --save`, one line per run (per batch): the open root causes from the '
                 'tags of agents/bugs/*.md and triage.md (agents/bugs/README.md), then the symptom counts it read '
                 '(suite_*: failing tests by origin; bun: bun cases mismatching; dt, source: projects mismatching; '
                 'expect: $ExpectType assertions; dts: dtsdiff projects; flagflip: diagnostics; clidiff_text: '
                 'projects; unexplained_*: the root-cause keys of each oracle no entry mentions). The oracle numbers are those of '
                 'whichever tree last ran each oracle (bugcount.py prints which).\n')
        anchor = text.find('\n## ', text.find('## How to measure'))
        text = text[:anchor + 1] + intro + line + '\n\n' + text[anchor + 1:]
    else:
        start = text.index(SECTION)
        end = text.find('\n\n', start)
        text = text[:end] + '\n' + line + text[end:]
    open(MEASUREMENTS, 'w').write(text)
    print(f'saved to {os.path.relpath(MEASUREMENTS, ROOT)}: {line}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--save', action='store_true', help='append the dated line to agents/measurements.md')
    ap.add_argument('--run', action='store_true', help='rerun the suite (fasttest.sh -s) and buncases.py first')
    ap.add_argument('--list', metavar='TAG', help="the entries with this tag ('untagged' for the missing ones)")
    ap.add_argument('--tree', action='append', help="read this worktree's caches (/tmp/NAME-TREE.json); repeatable, "
                    "in order of preference (default: this tree, tttt, wt-gate)")
    ap.add_argument('-v', '--verbose', action='store_true', help='every unexplained key, with an instance')
    args = ap.parse_args()
    if args.tree:
        PREFERRED[:] = args.tree
    if args.list:
        entries, untagged = all_entries()
        rows = untagged if args.list == 'untagged' else [e for e in entries if e['tag'] == args.list]
        for e in rows:
            ref = f" [{e['ref']}]" if e.get('ref') else ''
            print(f"{e['file']}:{e['start']}{ref}  {e['words'][:110]}")
        print(f'{len(rows)} entries')
        return
    if args.run:
        subprocess.run([os.path.join(ROOT, 'scripts/fasttest.sh'), '-s'], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([sys.executable, os.path.join(ROOT, 'scripts/buncases.py')], check=True,
                       stdout=subprocess.DEVNULL)
    headline, tags, saved, lines = report(args)
    print('\n'.join(lines))
    if args.save:
        save(headline, tags, saved)


if __name__ == '__main__':
    sys.exit(main())
