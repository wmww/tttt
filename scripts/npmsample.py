#!/usr/bin/env python3
"""Random npm projects as a correctness measure (agents/notes/npm_sampling.md, the human's method): a seed picks
~2000 package names from all of npm, the TypeScript ones with a GitHub repository are cloned, installed (scripts
never run) and given a tsconfig.bench.json tsgo 7.1 accepts, the first N whose trial tsgo run looks sane are checked
by tsgo and tttt, and every difference is a tttt bug (or a formatting-only one, not counted).

  scripts/npmsample.py --seed S [-N 50]       # sample, setup, compare, clean: the whole batch
  scripts/npmsample.py sample --seed S [-n 2000]   # test/npmsample/S.candidates.jsonl (TS packages, GitHub repos)
  scripts/npmsample.py stats --seed S              # setup's timing summary from S.setup.jsonl (t_* fields)
  scripts/npmsample.py setup --seed S [-N 50]      # clone + install + bench config + tsgo trial, resumable
        [--prefer-large] [--skip-monorepos]        # the trees listed first (precheck(): skips before any checkout),
                                                   # largest first; workspaces skipped (opt-in, see npm_sampling.md)
  scripts/npmsample.py sample --large --seed S [-n 60]   # large mode: random large GitHub repositories (reposearch.py
  scripts/npmsample.py setup --large --seed S [-N 50] [--per-repo 3] [--min-ts-files 200]  # large), per repo up to K
                                                   # workspace packages as projects (name DIR#package); one install
  scripts/npmsample.py compare --seed S [--last] [-k CODE] [--keep]   # tttt at the defaults twice and --threads 1:
                                                   # any two differing is verdict `varies`, priority 1; matches deleted
  scripts/npmsample.py rank [--min N] [--json]     # every seed's results by (side, code, message skeleton): projects, seeds,
                                                   # examples, status (open/repro/fixed); records' `diff` lists feed it
  scripts/npmsample.py clean --seed S [--all] [--except NAME...]   # delete the clones compare matched (--all: all)

Files: test/npmsample/S.candidates.jsonl (sample order), S.setup.jsonl (one line per candidate looked at: accepted or
the skip reason, package@version, the package manager, every config change), S.tsv (name, config path relative to
thirdparty/npmsample/S: the first N accepted in sample order), S.results.jsonl (per project: tsgo's and tttt's counts
and exit codes, missing/extra codes and an example, whether it persists at --threads 1, the tttt commit). The clones
live in thirdparty/npmsample/S/<i>-<owner>-<repo> (untracked, ~8-10 GB per 50) and are deleted by `clean` (run at
the end of a batch unless --keep) except the diverging ones, kept until their repro is in test/cases (then
`clean --all`). tsgo's output is cached per batch in thirdparty/npmsample/S/tsc.jsonl, the last comparison in
thirdparty/npmsample/S/compare.json (--last, -k).

Everything that touches a clone runs in scripts/installsandbox.py's bwrap sandbox (agents/notes/install_security.md):
git, the install (our pinned package manager, no scripts, no pnpmfile, no yarnPath/plugins: those are stripped from
the clone's .yarnrc.yml and logged in the setup record), tsc 6's --showConfig, tsgo and tttt (read-only, no network).
Nothing from node_modules is ever run. Caches in thirdparty/npmsample/.cache/pm (the package managers' deleted after
each setup run).
"""
import argparse, collections, concurrent.futures, fnmatch, glob, json, os, random, re, shutil, subprocess, sys, tarfile, threading, time
import urllib.error, urllib.parse, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import installsandbox as sb  # noqa: E402
import tscformat  # noqa: E402
import tttt_run  # noqa: E402
import sweep  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NS = os.path.realpath(os.path.join(ROOT, 'thirdparty/npmsample'))  # (tsgo prints real paths)
CACHE = os.path.join(NS, '.cache')
TESTDIR = os.path.join(ROOT, 'test/npmsample')
NODE_BIN = sb.NODE_BIN
BUN = sb.BUN
TS6_VERSION = '6.0.3'
TS6_DIR = os.path.join(CACHE, 'ts6')
TS6 = os.path.join(TS6_DIR, 'node_modules/typescript/bin/tsc')
BUILT = os.path.realpath(os.path.join(ROOT, 'thirdparty/typescript/built'))
TSGO = os.path.join(BUILT, 'tsc')
ORACLE = os.path.join(BUILT, 'tttt-oracle')
PM_CACHE = os.path.join(CACHE, 'pm')
COREPACK = os.path.join(PM_CACHE, 'corepack')  # (the managers corepack fetched, verified: shared, kept)


def seed_cache(seed):
    """The package managers' caches of one batch (deleted at its end: concurrent batches never share one)"""
    return os.path.join(PM_CACHE, 'seeds', str(seed))
TTTT = os.path.join(ROOT, 'release/tttt')
REGISTRY = 'https://registry.npmjs.org/'
LOCKFILES = ('pnpm-lock.yaml', 'yarn.lock', 'bun.lock', 'bun.lockb', 'package-lock.json', 'npm-shrinkwrap.json')
KEEP = ('.ts', '.tsx', '.mts', '.cts', '.js', '.jsx', '.mjs', '.cjs', '.json')
TS_SOURCE = ('.ts', '.tsx', '.mts', '.cts')
# Options tsgo 7.1 removed (TS5102 or unknown to it) that TypeScript 6's --showConfig still prints: set to null
REMOVED = ('downlevelIteration', 'outFile', 'out', 'importsNotUsedAsValues', 'preserveValueImports', 'keyofStringsOnly',
           'charset', 'noImplicitUseStrict', 'suppressExcessPropertyErrors', 'suppressImplicitAnyIndexErrors',
           'noStrictGenericChecks', 'prepend', 'baseUrl')
FALSE_REMOVED = ('alwaysStrict', 'esModuleInterop', 'allowSyntheticDefaultImports')
MIN_FREE_GB = 25


def files(seed, kind):
    return os.path.join(TESTDIR, f'{seed}.{kind}')


def read_jsonl(path):
    return [json.loads(line) for line in open(path)] if os.path.exists(path) else []


# ---------------------------------------------------------------------------------------------------------- sample

def ensure_names():
    path = os.path.join(CACHE, 'all-the-package-names.json')
    if not os.path.exists(path):
        os.makedirs(CACHE, exist_ok=True)
        meta = json.load(urllib.request.urlopen(REGISTRY + 'all-the-package-names/latest', timeout=60))
        tgz = path + '.tgz'
        print(f'fetching all-the-package-names {meta["version"]}', flush=True)
        urllib.request.urlretrieve(meta['dist']['tarball'], tgz)
        with tarfile.open(tgz) as t:
            data = t.extractfile('package/names.json').read()
        with open(path + '.tmp', 'wb') as f:
            f.write(data)
        os.rename(path + '.tmp', path)
        os.remove(tgz)
    return json.load(open(path))


class Pacer:
    """Request starts spaced by `gap` s across threads; a 429 pauses every thread (the registry's limit is a sliding
    window: bursts above ~6 at once get 429 for a while, and retrying into it extends it)"""
    def __init__(self, gap):
        self.gap, self.next, self.pause, self.lock = gap, 0.0, 15.0, threading.Lock()

    def wait(self):
        with self.lock:
            now = time.time()
            t = max(now, self.next)
            self.next = t + self.gap
        time.sleep(max(0.0, t - now))

    def throttled(self, retry_after):
        with self.lock:
            p = max(self.pause, float(retry_after or 0))
            self.next = max(self.next, time.time() + p)
            self.pause = min(300.0, self.pause * 1.5)

    def ok(self):
        self.pause = max(15.0, self.pause * 0.9)


PACER = Pacer(0.2)


def lookup(name):
    """The registry's latest manifest of name, None when unpublished (404), {'_error': ...} after many failures"""
    url = REGISTRY + urllib.parse.quote(name, safe='@') + '/latest'
    for attempt in range(12):
        PACER.wait()
        try:
            m = json.load(urllib.request.urlopen(url, timeout=60))
            PACER.ok()
            return m
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            if e.code == 429:
                ra = e.headers.get('Retry-After')
                PACER.throttled(ra if ra and ra.isdigit() else 0)
                continue
            if e.code not in (500, 502, 503, 504):
                return {'_error': e.code}
        except (urllib.error.URLError, TimeoutError, ConnectionError, json.JSONDecodeError, OSError):
            pass
        time.sleep(min(30, 2 ** attempt))
    return {'_error': 'retries'}


def has_types_export(x):
    if isinstance(x, dict):
        return 'types' in x or any(has_types_export(v) for v in x.values())
    if isinstance(x, list):
        return any(has_types_export(v) for v in x)
    return False


def looks_typescript(m):
    deps = {**(m.get('dependencies') or {}), **(m.get('devDependencies') or {})} if isinstance(
        m.get('dependencies') or {}, dict) and isinstance(m.get('devDependencies') or {}, dict) else {}
    return bool(m.get('types') or m.get('typings') or has_types_export(m.get('exports')) or 'typescript' in deps)


def github_repo(m):
    """(https://github.com/owner/repo, directory) of a manifest's repository field, else None"""
    r = m.get('repository')
    directory = ''
    if isinstance(r, dict):
        directory = r.get('directory') or ''
        r = r.get('url') or ''
    if not isinstance(r, str) or not r:
        return None
    r = r.strip()
    m2 = (re.match(r'^github:([\w.-]+)/([\w.-]+?)(?:\.git)?(?:#.*)?$', r) or
          re.match(r'^([\w.-]+)/([\w.-]+?)(?:\.git)?(?:#.*)?$', r) or
          re.match(r'^(?:git\+)?(?:https?|git|ssh|git\+ssh)://(?:[^@/]+@)?(?:www\.)?github\.com[:/]([\w.-]+)/([\w.-]+?)'
                   r'(?:\.git)?(?:[/#?].*)?$', r) or
          re.match(r'^(?:git@)?github\.com:([\w.-]+)/([\w.-]+?)(?:\.git)?(?:#.*)?$', r))
    if not m2:
        return None
    owner, repo = m2.group(1), m2.group(2)
    if not directory:
        t = re.search(r'github\.com/[\w.-]+/[\w.-]+/tree/[^/]+/(.+?)/?$', r)
        directory = t.group(1) if t else ''
    return f'https://github.com/{owner}/{repo}', directory.strip('/').removeprefix('./')


def cmd_sample_large(seed, n):
    """--large: the candidates are scripts/reposearch.py large's (random large TypeScript repositories from GitHub's
    search, SEED picks the pages; GITHUB_TOKEN is used when set), one per repository with the repository root as its
    directory"""
    out = files(seed, 'candidates.jsonl')
    print(f'sample --large: reposearch.py large {seed} {n} (7 s between searches)', flush=True)
    p = subprocess.run([sys.executable, os.path.join(ROOT, 'scripts/reposearch.py'), 'large', str(seed), str(n)],
                       stdout=subprocess.PIPE, text=True, timeout=6 * 3600)
    if p.returncode:
        sys.exit(f'reposearch.py failed ({p.returncode})')
    cands = []
    for line in p.stdout.splitlines():
        f = line.split()
        if len(f) != 5 or '/' not in f[0]:
            continue
        cands.append({'i': len(cands), 'name': f[0], 'version': f[4], 'repo': f'https://github.com/{f[0]}',
                      'directory': '', 'large': True, 'stars': int(f[1]), 'size_kb': int(f[2]), 'ts_bytes': int(f[3])})
    os.makedirs(TESTDIR, exist_ok=True)
    with open(out, 'w') as fh:
        for c in cands:
            fh.write(json.dumps(c) + '\n')
    print(f'sample --large: {len(cands)} candidate repos -> {os.path.relpath(out, ROOT)}')


def cmd_sample(seed, n):
    out = files(seed, 'candidates.jsonl')
    names = ensure_names()
    picks = random.Random(seed).sample(names, n)
    cache_path = os.path.join(CACHE, f'lookups-{seed}.jsonl')
    cache = {r['name']: r['m'] for r in read_jsonl(cache_path) if not (r['m'] and '_error' in r['m'])}
    todo = [x for x in picks if x not in cache]
    lock = threading.Lock()
    print(f'sample: {n} names, {len(todo)} to look up', flush=True)
    with open(cache_path, 'a') as f, concurrent.futures.ThreadPoolExecutor(6) as ex:
        def one(name):
            m = lookup(name)
            if m and '_error' not in m:
                m = {k: m.get(k) for k in ('name', 'version', 'types', 'typings', 'exports', 'dependencies',
                                            'devDependencies', 'repository')}
            with lock:
                cache[name] = m
                f.write(json.dumps({'name': name, 'm': m}) + '\n')
                f.flush()
        for i, _ in enumerate(ex.map(one, todo)):
            if i % 200 == 199:
                print(f'  {i + 1}/{len(todo)}', flush=True)
    seen, cands, counts = set(), [], collections.Counter()
    for name in picks:
        m = cache.get(name)
        if m is None:
            counts['404'] += 1
            continue
        if '_error' in m:
            counts['error'] += 1
            continue
        if not looks_typescript(m):
            counts['not typescript'] += 1
            continue
        gh = github_repo(m)
        if not gh:
            counts['no github repository'] += 1
            continue
        key = gh[0].lower()
        if key in seen:
            counts['duplicate repo'] += 1
            continue
        seen.add(key)
        cands.append({'i': len(cands), 'name': name, 'version': m.get('version'), 'repo': gh[0], 'directory': gh[1]})
    os.makedirs(TESTDIR, exist_ok=True)
    with open(out, 'w') as f:
        for c in cands:
            f.write(json.dumps(c) + '\n')
    print(f'sample: {len(cands)} candidate repos ({", ".join(f"{k} {v}" for k, v in counts.most_common())}) -> '
          f'{os.path.relpath(out, ROOT)}')


# ----------------------------------------------------------------------------------------------------------- setup

def git_env():
    env = {**sb.clean_env(), 'GIT_TERMINAL_PROMPT': '0', 'GIT_ASKPASS': '/bin/false', 'GIT_CONFIG_COUNT': '3',
           'GIT_CONFIG_KEY_0': 'url.https://github.com/.insteadOf', 'GIT_CONFIG_VALUE_0': 'git://github.com/',
           'GIT_CONFIG_KEY_1': 'url.https://github.com/.insteadOf', 'GIT_CONFIG_VALUE_1': 'ssh://git@github.com/',
           'GIT_CONFIG_KEY_2': 'url.https://github.com/.insteadOf', 'GIT_CONFIG_VALUE_2': 'git@github.com:'}
    return env


def prune(d):
    """node_modules cut to what a type checker reads (ensure-sources.sh's)"""
    for root, dirs, fs in os.walk(d):
        dirs[:] = [x for x in dirs if x != '.git']
        if '/node_modules' in root:
            for f in fs:
                if not f.endswith(KEEP):
                    p = os.path.join(root, f)
                    if not os.path.islink(p):
                        try:
                            os.remove(p)
                        except OSError:
                            pass


def strip_jsonc(text):
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith('//', i):
            while i < n and text[i] != '\n':
                i += 1
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return re.sub(r',(\s*[}\]])', r'\1', ''.join(out))


def load_jsonc(path):
    try:
        return json.loads(strip_jsonc(open(path, encoding='utf-8-sig').read()))
    except (OSError, ValueError):
        return None


def own_config_chain(config, repo):
    """The config and the configs it extends that are the repository's own (not in node_modules)"""
    chain, todo = [], [config]
    while todo:
        c = todo.pop()
        if c in chain or not os.path.exists(c) or '/node_modules/' in c or not c.startswith(repo):
            continue
        chain.append(c)
        j = load_jsonc(c) or {}
        ext = j.get('extends')
        for e in ([ext] if isinstance(ext, str) else ext if isinstance(ext, list) else []):
            if isinstance(e, str) and e.startswith('.'):
                p = os.path.normpath(os.path.join(os.path.dirname(c), e))
                todo.append(p if p.endswith('.json') or os.path.exists(p) and not os.path.isdir(p) else
                            os.path.join(p, 'tsconfig.json') if os.path.isdir(p) else p + '.json')
    return chain


def remove_key(text, key):
    """text (JSONC) without the property `"key": value` (and its comma), or None when it has none"""
    m = re.search(r'"' + re.escape(key) + r'"\s*:\s*', text)
    if not m:
        return None
    i, depth, n = m.end(), 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == '\\' else 1
            i += 1
            if depth == 0:
                break
            continue
        if c in '[{':
            depth += 1
        elif c in ']}':
            if depth == 0:
                break
            depth -= 1
            if depth == 0:
                i += 1
                break
        elif c in ',\n' and depth == 0:
            break
        i += 1
    end = i
    rest = re.match(r'\s*,', text[end:])
    if rest:
        end += rest.end()
    else:
        before = re.search(r',\s*$', text[:m.start()])
        if before:
            return text[:before.start()] + text[end:]
    return text[:m.start()] + text[end:]


def show_config(config):
    """(TypeScript 6's --showConfig JSON or None, its error text)"""
    p = sb.run([os.path.join(NODE_BIN, 'node'), TS6, '-p', os.path.basename(config), '--showConfig'],
               os.path.dirname(config), ro=[REPO_OF(config), TS6_DIR, sb.NODE_DIR], timeout=120)
    try:
        return json.loads(p.stdout), ''
    except ValueError:
        return None, (p.stdout + p.stderr).strip()


def solution_target(config, cfg):
    """For a solution-style config (files: [] plus references): the reference to check instead (the first app, lib,
    build or src one, else the first), else None"""
    if cfg.get('files') != [] or cfg.get('include') or not cfg.get('references'):
        return None
    refs = [r.get('path', '') for r in cfg['references'] if isinstance(r, dict)]
    pick = next((r for r in refs if re.search(r'app|lib|build|src', r)), refs[0] if refs else None)
    if pick is None:
        return None
    p = os.path.normpath(os.path.join(os.path.dirname(config), pick))
    return os.path.join(p, 'tsconfig.json') if os.path.isdir(p) else p


def bench_overrides(config, opts):
    """compilerOptions for tsconfig.bench.json (which extends config) and the changes made, as text"""
    o, changes = {'noEmit': True, 'incremental': False}, []
    low = lambda k: str(opts.get(k, '')).lower()  # noqa: E731
    mr, module, target = low('moduleResolution'), low('module'), low('target')
    if mr in ('node', 'node10', 'classic'):
        o['moduleResolution'] = 'bundler'
        if module in ('', 'commonjs', 'amd', 'umd', 'system', 'none'):
            o['module'] = 'commonjs'
        changes.append(f'moduleResolution {mr} -> bundler' + (f', module {module or "(unset)"} -> commonjs'
                                                              if 'module' in o else ''))
    elif module in ('amd', 'umd', 'system', 'none'):
        o['module'] = 'commonjs'
        changes.append(f'module {module} -> commonjs')
    if target in ('es3', 'es5'):
        o['target'] = 'es2015'
        changes.append(f'target {target} -> es2015')
        if 'lib' not in opts:
            o['lib'] = ['es5', 'dom', 'webworker.importscripts', 'scripthost']
            changes.append('lib pinned to the es5 default')
    if 'baseUrl' in opts:
        cdir = os.path.dirname(config)
        base = os.path.normpath(os.path.join(cdir, opts['baseUrl']))

        def rel(p):
            r = os.path.relpath(os.path.normpath(os.path.join(base, p)), cdir)
            r = r + ('/' if p.endswith('/') and not r.endswith('/') else '')
            return r if r.startswith('../') else './' + r
        if isinstance(opts.get('paths'), dict):
            o['paths'] = {k: [rel(v) for v in vs] for k, vs in opts['paths'].items() if isinstance(vs, list)}
            changes.append('baseUrl -> null, paths rebased')
        else:
            o['paths'] = {'*': [rel('*')]}
            changes.append(f'baseUrl {opts["baseUrl"]} -> paths "*"')
    for k in REMOVED:
        if k in opts:
            o[k] = None
            if k != 'baseUrl':
                changes.append(f'{k} -> null')
    for k in FALSE_REMOVED:
        if opts.get(k) is False:
            o[k] = None
            changes.append(f'{k} false -> null')
    if opts.get('composite'):
        o['composite'] = False
        o['declaration'] = True
        changes.append('composite -> false, declaration true')
    if opts.get('declarationMap') and not opts.get('declaration') and 'declaration' not in o:
        o['declaration'] = True
        changes.append('declaration true (declarationMap)')
    for k in ('emitDeclarationOnly', 'tsBuildInfoFile'):
        if k in opts:
            o[k] = None
            changes.append(f'{k} -> null')
    return o, changes


def tsgo_run(project, cwd, timeout=900):
    """(output text, exit status) of tsc -p PROJECT --noEmit --pretty false as tttt-oracle runs it"""
    try:
        p = subprocess.run(sb.sandbox_argv(ro=[REPO_OF(cwd), BUILT], cwd=cwd) + [ORACLE, '--text'],
                           input=project + '\n', env=sb.clean_env(), capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return '', 'timeout'
    line = p.stdout.splitlines()[0] if p.stdout.strip() else ''
    if not line:
        return p.stderr[-2000:], 'crash'
    r = json.loads(line)
    return r.get('text', ''), r.get('status')


def option_errors(text):
    """The option and config errors (tsgo checks nothing when there's one): no file, or a tsconfig"""
    return [(c, m) for f, _, _, c, m in tscformat.parse(text)
            if (not f or f.endswith('.json')) and (5000 <= c < 7000 or c in (18003,))]


def make_bench(config, repo):
    """(bench config path, changes) or (None, skip reason)"""
    changes = []
    for _ in range(4):
        cfg, err = show_config(config)
        if cfg is not None:
            target = solution_target(config, cfg)
            if target:
                if not os.path.exists(target):
                    return None, f'solution reference {os.path.relpath(target, repo)} missing'
                changes.append(f'solution config: checking {os.path.relpath(target, repo)}')
                config = target
                continue
            break
        unknown = re.findall(r"error TS5023: Unknown compiler option '([^']+)'", err)
        fixed = False
        for k in unknown:
            for c in own_config_chain(config, repo):
                t = open(c, encoding='utf-8-sig').read()
                t2 = remove_key(t, k)
                if t2 is not None:
                    open(c, 'w').write(t2)
                    changes.append(f'unknown option {k} deleted from {os.path.relpath(c, repo)}')
                    fixed = True
        if not fixed:
            return None, 'showConfig: ' + ' | '.join(err.splitlines()[:3])[:300]
    else:
        return None, 'showConfig: too many rounds'
    overrides, more = bench_overrides(config, cfg.get('compilerOptions') or {})
    changes += more
    bench = os.path.join(os.path.dirname(config), 'tsconfig.bench.json')
    for _ in range(3):
        with open(bench, 'w') as f:
            json.dump({'extends': './' + os.path.basename(config), 'compilerOptions': overrides}, f, indent=2)
            f.write('\n')
        text, status = tsgo_run('tsconfig.bench.json', os.path.dirname(bench))
        errs = option_errors(text)
        if not errs:
            return bench, changes
        fixed = False
        for code, msg in errs:
            m = re.match(r"Option '(\w+)'(?:=\S+)? has been removed", msg) or \
                re.match(r"Unknown compiler option '(\w+)'", msg)
            if m and code != 5023 and overrides.get(m.group(1), 0) is not None:
                overrides[m.group(1)] = None
                changes.append(f'{m.group(1)} -> null (TS{code})')
                fixed = True
            elif m and code == 5023:
                overrides.pop(m.group(1), None)
                for c in own_config_chain(config, repo):
                    t2 = remove_key(open(c, encoding='utf-8-sig').read(), m.group(1))
                    if t2 is not None:
                        open(c, 'w').write(t2)
                        changes.append(f'unknown option {m.group(1)} deleted from {os.path.relpath(c, repo)}')
                        fixed = True
        if not fixed:
            break
    return bench, changes  # (the trial reports the option errors left)


def count_ts(d):
    n = 0
    for root, dirs, fs in os.walk(d):
        dirs[:] = [x for x in dirs if x not in ('node_modules', '.git')]
        n += sum(1 for f in fs if f.endswith(TS_SOURCE) and not re.search(r'\.d\.[mc]?ts$', f) and '.d.' not in f)
    return n


def own_files(bench, repo):
    p = sb.run([TSGO, '-p', os.path.basename(bench), '--listFilesOnly'], os.path.dirname(bench), ro=[repo, BUILT],
               timeout=600)
    return sum(1 for line in p.stdout.splitlines()
               if line.startswith(repo + '/') and '/node_modules/' not in line)


def REPO_OF(path):  # noqa: N802
    """The clone a path is in (thirdparty/npmsample/SEED/NNNN-owner-repo): what a sandboxed tool may read"""
    rel = os.path.relpath(os.path.realpath(path), NS).split(os.sep)
    if len(rel) < 2 or rel[0].startswith('..'):
        raise ValueError(f'{path} is not in a clone')
    return os.path.join(NS, rel[0], rel[1])


def repo_dirname(c):
    owner, name = c['repo'].split('/')[-2:]
    return f'{c["i"]:04d}-{owner}-{name}'


# Never checked out: fonts, images, media, archives, binaries (a font repository's checkout was 12 GB). The clone is
# blobless, so only the files checked out are fetched
SPARSE_EXCLUDE = ('woff', 'woff2', 'ttf', 'otf', 'eot', 'png', 'jpg', 'jpeg', 'gif', 'webp', 'avif', 'ico', 'bmp', 'tif',
                  'tiff', 'psd', 'ai', 'sketch', 'fig', 'heic', 'mp3', 'mp4', 'm4a', 'wav', 'webm', 'mov', 'ogg', 'flac',
                  'avi', 'mkv', 'zip', 'gz', 'tgz', 'tar', '7z', 'rar', 'xz', 'bz2', 'jar', 'pdf', 'wasm', 'node', 'so',
                  'dylib', 'dll', 'exe', 'bin', 'dat', 'db', 'sqlite', 'pbf', 'mbtiles', 'glb', 'gltf', 'fbx', 'obj',
                  'blend', 'parquet', 'npy', 'onnx', 'pt', 'h5', 'csv', 'tsv', 'geojson', 'map')


def git_run(repo, *a, network=True):
    env = git_env()
    return sb.run(['git', *a], repo, rw=[repo], network=network,
                  env={k: v for k, v in env.items() if k.startswith('GIT_')}, timeout=900)


def clone_tree(url, repo):
    """The repository's commit and trees only (no blobs, no checkout): what precheck() reads"""
    env = git_env()
    parent = os.path.dirname(repo)
    return sb.run(['git', 'clone', '-q', '--depth', '1', '--single-branch', '--filter=blob:none', '--no-checkout', url,
                   repo], parent, rw=[parent], network=True, env={k: v for k, v in env.items() if k.startswith('GIT_')},
                  timeout=900)


def checkout(repo):
    """After clone_tree(): the sparse checkout (fonts, images, media left out), which fetches the blobs"""
    p = git_run(repo, '-C', repo, 'sparse-checkout', 'set', '--no-cone', '/*', *[f'!*.{e}' for e in SPARSE_EXCLUDE])
    if p.returncode == 0:
        p = git_run(repo, '-C', repo, 'checkout', '-q')
    return p


def is_ts_source(path):
    f = path.rsplit('/', 1)[-1]
    return f.endswith(TS_SOURCE) and '.d.' not in f


def precheck(repo, directory, min_ts=0, large=False):
    """Before any checkout or install, from the tree listing alone (and the root package.json, one blob): a skip
    reason (what setup_one() would find after the checkout: no tsconfig.json, fewer than 3 .ts files, the
    package directory gone), or None; and what the listing says (ts files under the package directory, monorepo)"""
    p = git_run(repo, '-C', repo, 'ls-tree', '-r', '--name-only', '-z', 'HEAD', network=False)
    if p.returncode != 0:
        return 'ls-tree: ' + (p.stderr.strip().splitlines() or [''])[-1][:200], {}
    paths = [x for x in p.stdout.split('\0') if x]
    d = os.path.normpath(directory).strip('/') if directory else ''
    if d == '.':
        d = ''
    prefix = d + '/' if d else ''
    if d.startswith('..') or (d and not any(x.startswith(prefix) for x in paths)):
        return 'repository.directory gone', {}
    names = set(paths)
    info = {'ts_files': sum(1 for x in paths if x.startswith(prefix) and is_ts_source(x) and '/node_modules/' not in
                            '/' + x)}
    info['monorepo'] = 'pnpm-workspace.yaml' in names or 'lerna.json' in names or 'nx.json' in names
    if 'package.json' in names and not info['monorepo']:
        q = git_run(repo, '-C', repo, 'show', 'HEAD:package.json')
        try:
            info['monorepo'] = q.returncode == 0 and 'workspaces' in json.loads(q.stdout)
        except (ValueError, TypeError):
            pass
    if large:
        # (A large repository: the projects are its workspace packages, found after the checkout; here only that
        # some tsconfig*.json exists and the tree is big enough)
        if not any(re.fullmatch(r'tsconfig[^/]*\.json', x.rsplit('/', 1)[-1]) and '/node_modules/' not in '/' + x
                   for x in paths):
            return 'no tsconfig*.json', info
    elif prefix + 'tsconfig.json' not in names and 'tsconfig.json' not in names:
        return 'no tsconfig.json', info
    if info['ts_files'] < max(3, min_ts):
        return (f'fewer than {min_ts} .ts files (--min-ts-files)' if min_ts > 3 else 'fewer than 3 .ts files'), info
    return None, info


def clone(url, repo):
    p = clone_tree(url, repo)
    return checkout(repo) if p.returncode == 0 else p


def list_one(seed, c, skip_monorepos=False, large=False, min_ts=0):
    """The first stage of a candidate's setup: its trees cloned (no blobs, no checkout) and precheck()ed. A skip
    record, or None and what the listing says (setup_one() then checks out the same clone)"""
    rec = {k: c[k] for k in ('i', 'name', 'version', 'repo', 'directory')}
    repo = os.path.join(NS, str(seed), repo_dirname(c))
    if os.path.exists(os.path.join(repo, '.npmsample-installed')):
        return None, {}
    if os.path.exists(repo):
        shutil.rmtree(repo)
    t0 = time.time()
    p = clone_tree(c['repo'], repo)
    t_clone = time.time() - t0
    if p.returncode != 0:
        shutil.rmtree(repo, ignore_errors=True)
        return {**rec, 'status': 'skip', 'reason': 'clone: ' + (p.stderr.strip().splitlines() or [''])[-1][:200],
                't_clone': t_clone}, {}
    t0 = time.time()
    reason, info = precheck(repo, c['directory'], min_ts, large)
    info = {**info, 't_clone': t_clone, 't_precheck': time.time() - t0}
    if not reason and skip_monorepos and info.get('monorepo'):
        reason = 'monorepo (--skip-monorepos)'
    if reason:
        shutil.rmtree(repo, ignore_errors=True)
        return {**rec, **info, 'status': 'skip', 'reason': reason, 'precheck': True}, info
    return None, info


def setup_one(seed, c, listed=None, skip_monorepos=False, large=False, min_ts=0, per_repo=3):
    """`listed`: list_one()'s result if it ran already. The record carries the wall-clock seconds of its steps
    (t_clone: clone and checkout, t_precheck, t_install, t_trial: the bench config and tsgo's run) and t_total"""
    pre = listed is not None
    if not pre:
        listed = list_one(seed, c, skip_monorepos, large, min_ts)
    t0 = time.time()
    t = {}
    r = setup_one_steps(seed, c, listed, t, per_repo if large else 0)
    # (list_one's steps are in the record already: its skip record, or the info merged into it; a listing done
    # before this call (--prefer-large) is not in this call's wall time)
    t['t_total'] = time.time() - t0 + (sum(listed[1].get(k, 0.0) for k in ('t_clone', 't_precheck')) if pre else 0.0)
    for k in ('t_clone', 't_precheck', 't_install', 't_trial'):
        t[k] = t.get(k, r.get(k, 0.0))
    return {**r, **{k: round(v, 3) for k, v in t.items()}}


def setup_one_steps(seed, c, listed, t, per_repo=0):
    rec = {k: c[k] for k in ('i', 'name', 'version', 'repo', 'directory')}
    repo = os.path.join(NS, str(seed), repo_dirname(c))
    marker = os.path.join(repo, '.npmsample-installed')
    skip, info = listed
    if skip:
        return skip
    rec.update(info)
    if not os.path.exists(marker):
        t0 = time.time()
        p = checkout(repo)
        rec['t_clone'] = rec.get('t_clone', 0.0) + time.time() - t0
        if p.returncode != 0:
            shutil.rmtree(repo, ignore_errors=True)
            return {**rec, 'status': 'skip', 'reason': 'checkout: ' + (p.stderr.strip().splitlines() or [''])[-1][:200]}
    rec['commit'] = sb.run(['git', '-C', repo, 'rev-parse', 'HEAD'], repo, ro=[repo]).stdout.strip()
    pkg = os.path.normpath(os.path.join(repo, c['directory'])) if c['directory'] else repo
    if not os.path.isdir(pkg) or not pkg.startswith(repo):
        return {**rec, 'status': 'skip', 'reason': 'repository.directory gone'}
    if per_repo:
        projs = large_projects(repo)
        if not projs:
            return {**rec, 'status': 'skip', 'reason': 'no workspace package or root with a tsconfig and 3 .ts files'}
        rec['workspaces'] = len(projs)
    config = None if per_repo else next((p for p in (os.path.join(pkg, 'tsconfig.json'), os.path.join(repo, 'tsconfig.json'))
                   if os.path.exists(p)), None)
    if not config and not per_repo:
        return {**rec, 'status': 'skip', 'reason': 'no tsconfig.json'}
    if not per_repo and count_ts(pkg) < 3:
        return {**rec, 'status': 'skip', 'reason': 'fewer than 3 .ts files'}
    if not os.path.exists(marker):
        # (A cache per install, deleted after it: a batch's shared caches reached 7-9 GB, more than its clones; pnpm's
        # store files stay in node_modules as hardlinks)
        cache = os.path.join(seed_cache(seed), repo_dirname(c))
        t0 = time.time()
        r = sb.install(repo, pkg, cache, corepack_home=COREPACK)
        t['t_install'] = time.time() - t0
        shutil.rmtree(cache, ignore_errors=True)
        rec['install'] = {k: r[k] for k in ('pm', 'dir', 'notes', 'changes', 'commands')}
        if r['refused']:
            return {**rec, 'status': 'skip', 'pm': r['pm'], 'reason': 'install refused: ' + r['refused']}
        if not r['ok']:
            return {**rec, 'status': 'skip', 'pm': r['pm'], 'reason': 'install: ' + r['tail'][-300:]}
        prune(repo)
        with open(marker, 'w') as f:
            f.write(json.dumps(rec['install']) + '\n')
    rec['pm'] = json.loads(open(marker).read())['pm'] if open(marker).read().startswith('{') else open(marker).read().strip()
    t0 = time.time()
    if per_repo:
        return large_trials(seed, c, repo, projs, per_repo, rec, t)
    bench, changes = make_bench(config, repo)
    rec['changes'] = changes
    if bench is None:
        return {**rec, 'status': 'skip', 'reason': changes}
    rec['config'] = os.path.relpath(bench, os.path.join(NS, str(seed)))
    text, status = tsgo_run('tsconfig.bench.json', os.path.dirname(bench))
    t['t_trial'] = time.time() - t0
    diags = tscformat.parse(text)
    codes = collections.Counter(d[3] for d in diags)
    own = own_files(bench, repo)
    rec['tsgo'] = {'exit': status, 'errors': len(diags), 'ts2307': codes[2307], 'own': own}
    left = option_errors(text)
    if status not in (0, 1, 2):
        return {**rec, 'status': 'reject', 'reason': f'tsgo {status}'}
    if left:
        return {**rec, 'status': 'reject', 'reason': 'option errors left: ' +
                '; '.join(f'TS{c} {m[:80]}' for c, m in left[:3])}
    # (NPMSAMPLE_ANY_TRIAL=1: a project asked for by name is checked whatever the trial says, unresolved packages and
    # all: both compilers see the same tree)
    if (own < 1 or len(diags) > 100 or codes[2307] > 5) and not os.environ.get('NPMSAMPLE_ANY_TRIAL'):
        return {**rec, 'status': 'reject', 'reason': f'trial: {own} own files, {len(diags)} errors, '
                                                     f'{codes[2307]} TS2307'}
    return {**rec, 'status': 'accepted'}


def workspace_globs(repo):
    """(include, exclude) globs of the repository's workspaces: package.json `workspaces` (list or {packages}) and
    pnpm-workspace.yaml `packages`"""
    globs = []
    try:
        w = json.load(open(os.path.join(repo, 'package.json'), encoding='utf-8-sig')).get('workspaces', [])
        globs += w.get('packages', []) if isinstance(w, dict) else w
    except (OSError, ValueError, AttributeError):
        pass
    try:
        m = re.search(r'(?ms)^packages:\s*\n((?:[ \t]*-.*\n?|[ \t]*#.*\n?)+)', open(os.path.join(repo, 'pnpm-workspace.yaml')).read())
        if m:
            globs += [x.split('#')[0].strip().lstrip('-').strip().strip('\'"') for x in m.group(1).splitlines()
                      if x.strip() and not x.strip().startswith('#')]
    except OSError:
        pass
    globs = [g.removeprefix('./') for g in globs if isinstance(g, str) and g]
    return [g for g in globs if not g.startswith('!')], [g[1:] for g in globs if g.startswith('!')]


def package_config(d):
    """The tsconfig a package directory is checked with: tsconfig.json, else the first tsconfig*.json that is not a
    base/shared one"""
    if os.path.exists(os.path.join(d, 'tsconfig.json')):
        return os.path.join(d, 'tsconfig.json')
    names = sorted(f for f in os.listdir(d) if re.fullmatch(r'tsconfig[^/]*\.json', f))
    names = [f for f in names if not re.search(r'base|shared|common', f)] or names
    return os.path.join(d, names[0]) if names else None


def large_projects(repo):
    """[(directory, config, ts files)] of a --large repository, biggest first: the workspace packages with a
    tsconfig*.json and 3 .ts files, the root itself last (first when it declares no workspaces)"""
    inc, exc = workspace_globs(repo)
    dirs = set()
    for g in inc:
        for p in glob.glob(os.path.join(repo, g.rstrip('/')), recursive=True):
            rel = os.path.relpath(p, repo)
            if os.path.isdir(p) and 'node_modules' not in rel.split(os.sep) and rel != '.' and \
                    not any(fnmatch.fnmatch(rel, e.rstrip('/')) for e in exc):
                dirs.add(p)
    out = []
    for d in sorted(dirs):
        cfg = package_config(d)
        n = count_ts(d) if cfg else 0
        if cfg and n >= 3:
            out.append((os.path.relpath(d, repo), cfg, n))
    out.sort(key=lambda x: -x[2])
    root = package_config(repo)
    if root and count_ts(repo) >= 3:
        out = out + [('.', root, count_ts(repo))] if dirs else [('.', root, count_ts(repo))]
    return out


def large_trials(seed, c, repo, projs, per_repo, rec, t):
    """--large: the repository (installed once) trial-run per sub-project, biggest first, until per_repo are accepted
    (or 4*per_repo+4 tried); the same trial as one project (<=100 errors, <=5 TS2307, >=1 own file)"""
    subs, used, t0 = [], set(), time.time()
    for d, config, n in projs:
        if sum(s['status'] == 'accepted' for s in subs) >= per_repo or len(subs) >= 4 * per_repo + 4:
            break
        s = {'name': f'{c["name"]}#{d}', 'dir': d, 'ts_files': n}
        subs.append(s)
        bench, changes = make_bench(config, repo)
        s['changes'] = changes
        if bench is None:
            s.update(status='skip', reason=changes)
            continue
        if bench in used:
            s.update(status='skip', reason='same project as another sub-project (solution reference)')
            continue
        used.add(bench)
        s['config'] = os.path.relpath(bench, os.path.join(NS, str(seed)))
        text, status = tsgo_run('tsconfig.bench.json', os.path.dirname(bench))
        diags = tscformat.parse(text)
        codes = collections.Counter(x[3] for x in diags)
        own = own_files(bench, repo)
        s['tsgo'] = {'exit': status, 'errors': len(diags), 'ts2307': codes[2307], 'own': own}
        left = option_errors(text)
        if status not in (0, 1, 2):
            s.update(status='reject', reason=f'tsgo {status}')
        elif left:
            s.update(status='reject', reason='option errors left: ' + '; '.join(f'TS{x} {m[:80]}' for x, m in left[:3]))
        elif own < 1 or len(diags) > 100 or codes[2307] > 5:
            s.update(status='reject', reason=f'trial: {own} own files, {len(diags)} errors, {codes[2307]} TS2307')
        else:
            s['status'] = 'accepted'
    t['t_trial'] = time.time() - t0
    ok = [s for s in subs if s['status'] == 'accepted']
    return {**rec, 'subs': subs, 'status': 'accepted' if ok else 'reject',
            **({} if ok else {'reason': 'no sub-project passed the trial: ' + '; '.join(
                f'{s["dir"]}: {s["reason"][:60]}' for s in subs[:3])})}


def accepted_of(r):
    """How many projects a setup record accepted (a --large record: its accepted sub-projects)"""
    if 'subs' in r:
        return sum(s['status'] == 'accepted' for s in r['subs'])
    return int(r['status'] == 'accepted')


def ensure_ts6():
    if not os.path.exists(TS6):
        d = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(TS6))))
        os.makedirs(d, exist_ok=True)
        npm_cache = os.path.join(CACHE, 'ts6-npm-cache')
        os.makedirs(npm_cache, exist_ok=True)
        p = sb.run([os.path.join(NODE_BIN, 'npm'), 'install', '--ignore-scripts', '--no-audit', '--no-fund',
                    f'--registry={sb.REGISTRY}', f'--cache={npm_cache}', f'typescript@{TS6_VERSION}'], d,
                   rw=[d, npm_cache], ro=[sb.NODE_DIR], network=True)
        shutil.rmtree(npm_cache, ignore_errors=True)
        if p.returncode:
            sys.exit(f'typescript@{TS6_VERSION} install failed:\n{p.stdout[-1000:]}{p.stderr[-1000:]}')


def write_tsv(seed, n):
    recs = sorted(read_jsonl(files(seed, 'setup.jsonl')), key=lambda r: r['i'])
    # (Every accepted project, the overshoot too: installs running when the Nth is accepted finish, and compare
    # covers them rather than leaving uncompared clones)
    acc = [r for r in recs if r['status'] == 'accepted']
    with open(files(seed, 'tsv'), 'w') as f:
        for r in acc:
            if 'subs' in r:
                for s in r['subs']:
                    if s['status'] == 'accepted':
                        f.write(f'{repo_dirname(r)}#{s["dir"]}\t{s["config"]}\n')
            else:
                f.write(f'{repo_dirname(r)}\t{r["config"]}\n')
    return acc


def outcome(r):
    if r['status'] == 'accepted':
        return 'accepted'
    if r['status'] == 'reject':
        return 'rejected by trial'
    if str(r.get('reason', '')).startswith('install'):
        return 'install failed'
    return 'skipped'


def timing_summary(recs, what='candidates'):
    """Time spent by outcome and the five longest candidates, from setup records with a t_total (older records
    have none: counted, not summarized)"""
    timed = [r for r in recs if 't_total' in r]
    out = [f'timing: {len(timed)} of {len(recs)} {what} timed' +
           (f' ({len(recs) - len(timed)} older records lack the fields, skipped)' if len(timed) < len(recs) else '')]
    if not timed:
        return '\n'.join(out)
    steps = ('t_clone', 't_precheck', 't_install', 't_trial')
    out.append(f'  {"outcome":18} {"n":>4} {"total s":>9} {"mean s":>8}  ' + ' '.join(f'{k[2:]:>9}' for k in steps))
    for o in ('accepted', 'rejected by trial', 'install failed', 'skipped'):
        rs = [r for r in timed if outcome(r) == o]
        if rs:
            tot = sum(r['t_total'] for r in rs)
            out.append(f'  {o:18} {len(rs):4} {tot:9.0f} {tot / len(rs):8.1f}  ' +
                       ' '.join(f'{sum(r.get(k, 0.0) for r in rs):9.0f}' for k in steps))
    tot = sum(r['t_total'] for r in timed)
    out.append(f'  {"all":18} {len(timed):4} {tot:9.0f} {tot / len(timed):8.1f}  (summed over candidates, run in parallel)')
    out.append('  longest:')
    for r in sorted(timed, key=lambda r: -r['t_total'])[:5]:
        why = r.get('reason') or ''
        out.append(f'    {r["t_total"]:7.0f}s {r["name"][:36]:36} {outcome(r):17} ts_files {r.get("ts_files", "?")}' +
                   (f'  {str(why)[:50]}' if r['status'] != 'accepted' else ''))
    return '\n'.join(out)


def cmd_stats(seed):
    recs = read_jsonl(files(seed, 'setup.jsonl'))
    if not recs:
        sys.exit(f'stats: no setup records for seed {seed}')
    print(timing_summary(recs))


def cmd_setup(seed, n, jobs, prefer_large=False, skip_monorepos=False, large=False, min_ts=0, per_repo=3):
    cands = read_jsonl(files(seed, 'candidates.jsonl'))
    if not cands:
        sys.exit(f'no candidates: run `scripts/npmsample.py sample --seed {seed}` first')
    ensure_ts6()
    log = files(seed, 'setup.jsonl')
    done = {r['i']: r for r in read_jsonl(log)}
    large = large or any(c.get('large') for c in cands)
    accepted = sum(accepted_of(r) for r in done.values())
    todo = [c for c in cands if c['i'] not in done]
    os.makedirs(os.path.join(NS, str(seed)), exist_ok=True)
    # (clean refuses to run while this file names a live process)
    pid_file = os.path.join(NS, str(seed), '.setup.pid')
    other = setup_running(seed)
    if other and other != os.getpid():
        sys.exit(f'setup: another setup of seed {seed} is running (pid {other})')
    with open(pid_file, 'w') as f:
        f.write(str(os.getpid()))
    print(f'setup: {accepted}/{n} accepted, {len(done)} candidates done, {len(todo)} left', flush=True)
    lock = threading.Lock()
    stop = threading.Event()

    def one(c):
        if stop.is_set():
            return None
        free = shutil.disk_usage(NS).free / 2 ** 30
        if free < MIN_FREE_GB:
            stop.set()
            print(f'setup: stopping, {free:.0f} GB free (< {MIN_FREE_GB})', flush=True)
            return None
        try:
            r = setup_one(seed, c, listed.get(c['i']), skip_monorepos, large, min_ts, per_repo)
        except Exception as e:  # noqa: BLE001 (one candidate's surprise is its skip reason)
            r = {k: c[k] for k in ('i', 'name', 'version', 'repo', 'directory')}
            r.update(status='skip', reason=f'setup error: {type(e).__name__}: {str(e)[:200]}')
        if r['status'] != 'accepted':
            # (Never needed again: the record says why)
            shutil.rmtree(os.path.join(NS, str(seed), repo_dirname(c)), ignore_errors=True)
        with lock:
            with open(log, 'a') as f:
                f.write(json.dumps(r) + '\n')
        return r
    listed = {}

    def record(r):
        with lock:
            with open(log, 'a') as f:
                f.write(json.dumps(r) + '\n')

    def list_chunk(chunk):
        # (--prefer-large: the chunk's trees listed first, cheaply, its skips recorded; the rest set up largest
        # first by their .ts file count. Never by what tsgo says: clean projects and erroring ones alike)
        with concurrent.futures.ThreadPoolExecutor(jobs * 2) as ex:
            got = list(ex.map(lambda c: list_one(seed, c, skip_monorepos, large, min_ts), chunk))
        kept = []
        for c, (skip, info) in zip(chunk, got):
            if skip:
                record(skip)
                print(f'  {c["i"]:4} {c["name"][:40]:40} skip: {skip["reason"][:150]}', flush=True)
            else:
                listed[c['i']] = (None, info)
                kept.append(c)
        return sorted(kept, key=lambda c: -listed[c['i']][1].get('ts_files', 0))

    while accepted < n and todo and not stop.is_set():
        if prefer_large:
            size = max(20, 4 * (n - accepted))
            batch, todo = list_chunk(todo[:size]), todo[size:]
        else:
            batch, todo = todo, []
        with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
            futs = [ex.submit(one, c) for c in batch]
            for c, fu in zip(batch, futs):
                r = fu.result()
                if r is None:
                    continue
                tag = r['status'] if r['status'] != 'skip' and r['status'] != 'reject' else r['status'] + ': ' + str(r['reason'])[:150]
                if r['status'] == 'accepted':
                    accepted += accepted_of(r)
                    tag += f' ({accepted}/{n}, ' + (f'{accepted_of(r)} of {len(r["subs"])} sub-projects tried' if 'subs' in r
                                                    else f'tsgo {r["tsgo"]["errors"]} errors') + ')'
                print(f'  {c["i"]:4} {c["name"][:40]:40} {tag}', flush=True)
                if accepted >= n:
                    stop.set()
                    for f2 in futs:
                        f2.cancel()
                    break
    # (Listed by --prefer-large but never set up, the batch being full: their tree-only clones go, no record says
    # anything about them, a resume lists them again)
    recorded = {r['i'] for r in read_jsonl(log)}
    for c in cands:
        if c['i'] in listed and c['i'] not in recorded:
            shutil.rmtree(os.path.join(NS, str(seed), repo_dirname(c)), ignore_errors=True)
    # (The package managers' caches only matter while installing: the installed files are copies or hardlinks)
    shutil.rmtree(seed_cache(seed), ignore_errors=True)
    os.remove(pid_file)
    acc = write_tsv(seed, n)
    print(f'setup: {len(acc)}/{n} accepted -> {os.path.relpath(files(seed, "tsv"), ROOT)}')
    print(timing_summary([r for r in read_jsonl(log) if r['i'] not in done], 'candidates of this run'))
    return len(acc) >= n


# --------------------------------------------------------------------------------------------------------- compare

LIBPATH = re.compile(r'(?:bundled:///libs/|\S*/node_modules/typescript/lib/|\S*?/built/local/)(lib\.[\w.-]*d\.ts)')


def normalize(text):
    return sorted(LIBPATH.sub(r'LIB/\1', line) for line in text.replace('\r', '').split('\n') if line.strip())


def tttt_check(bench, threads=None):
    extra = ['--threads', str(threads)] if threads else []
    binary = tttt_run.frozen(TTTT)
    # (The standard library is in the binary)
    argv = sb.sandbox_argv(ro=[REPO_OF(bench), binary], cwd=os.path.dirname(bench))
    r = tttt_run.run([*argv, binary, '-p', os.path.basename(bench), '--noEmit', '--pretty', 'false', *extra],
                     None, timeout=900, mem_gb=16, threads=threads, env=sb.clean_env(), stderr=subprocess.PIPE)
    return re.sub(r'\x1b\[[0-9;]*m', '', r.text()), r.code, r.status


def keys(text):
    return collections.Counter((f, l, c) for f, l, _, c, _ in tscformat.parse(text))


def head_commit():
    return subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip()


def diff_entries(g_text, d_text, miss, extra, limit=30):
    """The diagnostics only one side reports, as {side, code, key, file, line}: key is the message skeleton (quoted
    parts elided, sweep.shape), so a deleted clone still leaves its fingerprint for `rank`"""
    msgs = {}
    for side, text in (('tsgo', g_text), ('tttt', d_text)):
        for f, l, _, c, m in tscformat.parse(text):
            msgs.setdefault((side, f, l, c), m)
    out = []
    for side, ks in (('tttt', extra), ('tsgo', miss)):
        for f, l, c in sorted(ks):
            out.append({'side': side, 'code': c, 'key': sweep.shape(msgs.get((side, f, l, c), '')),
                        'file': os.path.basename(f), 'line': l})
    return out[:limit]


def compare_one(seed, name, config, tsc_cache, lock, delete_same):
    base = os.path.join(NS, str(seed))
    bench = os.path.join(base, config)
    if not os.path.exists(bench):
        return {'name': name, 'verdict': 'gone'}
    with lock:
        cached = tsc_cache.get(name)
    if cached is None:
        g_text, g_status = tsgo_run('tsconfig.bench.json', os.path.dirname(bench))
        cached = {'name': name, 'text': g_text, 'status': g_status}
        with lock:
            tsc_cache[name] = cached
            with open(os.path.join(base, 'tsc.jsonl'), 'a') as f:
                f.write(json.dumps(cached) + '\n')
    g_text, g_status = cached['text'], cached['status']
    d_text, d_code, d_status = tttt_check(bench)
    # (Races and checker-count dependence outrank everything: the defaults twice and --threads 1 each time)
    runs = [(d_text, d_code, d_status), tttt_check(bench), tttt_check(bench, threads=1)]
    g, d = keys(g_text), keys(d_text)
    miss, extra = g - d, d - g
    r = {'name': name, 'config': config, 'tsgo_exit': g_status, 'tsgo_n': sum(g.values()), 'tttt_exit': d_code,
         'tttt_status': d_status, 'tttt_n': sum(d.values()), 'missing': sum(miss.values()), 'extra': sum(extra.values()),
         'codes_missing': sorted({k[2] for k in miss}), 'codes_extra': sorted({k[2] for k in extra})}
    same = normalize(g_text) == normalize(d_text) and g_status == d_code
    r['verdict'] = ('crash' if d_status != 'ok' else 'same' if same else 'text' if not miss and not extra else 'differ')
    shapes = {(tuple(normalize(t)), c, st) for t, c, st in runs}
    r['runs_agree'] = len(shapes) == 1
    r['threads1_same'] = (normalize(runs[2][0]), runs[2][1]) == (normalize(d_text), d_code)
    if not r['runs_agree']:
        r['verdict'] = 'varies'
        a = runs[0]
        b = next(x for x in runs[1:] if (normalize(x[0]), x[1], x[2]) != (normalize(a[0]), a[1], a[2]))
        ka, kb = keys(a[0]), keys(b[0])
        r['varies'] = {'defaults_twice_same': (normalize(runs[1][0]), runs[1][1]) == (normalize(d_text), d_code),
                       'only_first': sorted(map(list, ka - kb))[:5], 'only_other': sorted(map(list, kb - ka))[:5],
                       'statuses': [x[2] for x in runs], 'exits': [x[1] for x in runs]}
    lines = {(f, l, c): f'{f}({l}): TS{c} {m[:200]}' for f, l, _, c, m in tscformat.parse(g_text + '\n' + d_text)}
    r['diff'] = diff_entries(g_text, d_text, miss, extra)
    if not r['diff']:
        del r['diff']
    if miss or extra:
        k = sorted(extra or miss)[0]
        r['example'] = ('tttt only: ' if extra else 'tsgo only: ') + lines.get(k, str(k))
    elif not same:
        a, b = normalize(g_text), normalize(d_text)
        first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
        r['example'] = f'tsgo: {a[first] if first < len(a) else "(end)"} / tttt: {b[first] if first < len(b) else "(end)"}'
        if g_status != d_code:
            r['example'] += f' (exit {g_status} vs {d_code})'
    if r['verdict'] == 'same' and delete_same and '#' not in name:  # (a --large clone is shared: `clean` decides)
        # (A matching project is never needed again: deleted at once)
        shutil.rmtree(REPO_OF(bench), ignore_errors=True)
    r['_g'] = g_text
    r['_d'] = d_text
    return r


def cmd_compare(seed, jobs, delete_same, last, code):
    base = os.path.join(NS, str(seed))
    last_path = os.path.join(base, 'compare.json')
    if last:
        results = json.load(open(last_path))
    else:
        subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
        rows = [line.rstrip('\n').split('\t') for line in open(files(seed, 'tsv'))]
        tsc_cache = {r['name']: r for r in read_jsonl(os.path.join(base, 'tsc.jsonl'))}
        lock = threading.Lock()
        with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
            results = list(ex.map(lambda row: compare_one(seed, row[0], row[1], tsc_cache, lock, delete_same), rows))
        # (Merged by project with the earlier rounds' records: a batch resumed after the disk guard compares only what
        # is left, and a clone deleted as matching comes back as `gone`, which never replaces its record)
        commit = head_commit()
        fresh = {r['name']: r for r in results if r.get('verdict') != 'gone'}
        earlier = json.load(open(last_path)) if os.path.exists(last_path) else []
        results = [r for r in earlier if r['name'] not in fresh] + list(fresh.values())
        results.sort(key=lambda r: r['name'])
        json.dump(results, open(last_path, 'w'))
        rows = {r['name']: r for r in read_jsonl(files(seed, 'results.jsonl'))}
        rows.update({n: {**{k: v for k, v in r.items() if not k.startswith('_')}, 'tttt_commit': commit}
                     for n, r in fresh.items()})
        with open(files(seed, 'results.jsonl'), 'w') as f:
            for name in sorted(rows):
                f.write(json.dumps(rows[name]) + '\n')
    if code:
        want = int(code.removeprefix('TS'))
        for r in results:
            if '_g' not in r:
                continue
            g, d = keys(r['_g']), keys(r['_d'])
            lines = {(f, l, c): f'{f}({l}): TS{c} {m[:200]}' for f, l, _, c, m in tscformat.parse(r['_g'] + '\n' + r['_d'])}
            for side, ks in (('tsgo only', g - d), ('tttt only', d - g)):
                for k in sorted(ks):
                    if k[2] == want:
                        print(f'{r["name"]}: {side}: {lines.get(k, k)}')
        return
    results = [r for r in results if r.get('verdict') != 'gone']
    for r in results:
        if r.get('verdict') == 'varies':
            print(f'PRIORITY 1: {r["name"]}: tttt runs differ (defaults, defaults, --threads 1): {json.dumps(r["varies"])}')
    print(f'{"project":44} {"tsgo":>5} {"tttt":>5} {"miss":>5} {"extra":>5}  t1  verdict  codes / example')
    for r in results:
        if r['verdict'] == 'same':
            continue
        codes = ' '.join([f'-{c}' for c in r['codes_missing']] + [f'+{c}' for c in r['codes_extra']])
        t1 = {True: 'same', False: 'DIFF', None: '-'}[r.get('threads1_same')]
        t1 = t1 if r.get('runs_agree', True) else 'VARY'
        print(f'{r["name"][:44]:44} {r["tsgo_n"]:5} {r["tttt_n"]:5} {r["missing"]:5} {r["extra"]:5}  {t1:4} '
              f'{r["verdict"]:7}  {codes}\n{"":46}{r.get("example", r.get("tttt_status", ""))[:200]}')
    n = len(results)
    same = sum(r['verdict'] == 'same' for r in results)
    fmt = sum(r['verdict'] in ('same', 'text') for r in results)
    clean = sum((r['tsgo_n'] == 0) == (r['tttt_n'] == 0) and r['tttt_status'] == 'ok' for r in results)
    match = sum(r['verdict'] in ('same', 'text') or (r['missing'] == r['extra'] == 0) for r in results)
    print(f'{n} projects: {same} byte-identical modulo lib paths ({100 * same / max(n, 1):.0f}%), {match} same '
          f'(file, line, code) ({fmt} counting text-only), clean agreement {clean}/{n} ({100 * clean / max(n, 1):.0f}%)')


# ------------------------------------------------------------------------------------------------------------ rank

def project_owner_repo(name):
    """'0017-JohnDeved-zooqle-api-ts' -> 'johndeved-zooqle-api-ts' (the clone directory without its index)"""
    return name.split('#')[0].split('-', 1)[-1].lower()


def bug_bullets():
    """(text, fixed) of every bullet in agents/bugs/npmsample*.md"""
    out = []
    d = os.path.join(ROOT, 'agents/bugs')
    for fn in sorted(os.listdir(d)):
        if not (fn.startswith('npmsample') and fn.endswith('.md')):
            continue
        for b in re.split(r'\n(?=- )', open(os.path.join(d, fn)).read()):
            if b.startswith('- '):
                t = b.rstrip()
                out.append((t, bool(re.search(r'\[fixed[^\]]*\]\s*$', t))))
    return out


def rank_data(min_projects=1):
    """Every results record of every seed: {(side, code, key): {projects, seeds, ...}} with key None for the coarse
    fallback of records that have only codes_extra/codes_missing; each with a status (fixed, repro, open)"""
    agg = {}
    for path in sorted(glob.glob(os.path.join(ROOT, 'test/npmsample/*.results.jsonl'))):
        seed = os.path.basename(path).split('.')[0]
        for r in read_jsonl(path):
            if r.get('verdict') not in ('differ', 'crash', 'varies', 'text') and not r.get('diff'):
                continue
            proj = f'{seed}/{r["name"]}'
            entries = {(e['side'], e['code'], e['key']) for e in r.get('diff', [])}
            if not r.get('diff'):
                entries = {('tttt', c, None) for c in r.get('codes_extra', [])} | \
                          {('tsgo', c, None) for c in r.get('codes_missing', [])}
            for k in entries:
                a = agg.setdefault(k, {'projects': set(), 'seeds': set()})
                a['projects'].add(proj)
                a['seeds'].add(seed)
    repros = []
    tsv = os.path.join(ROOT, 'test/npmsample/repros.tsv')
    if os.path.exists(tsv):
        for line in open(tsv):
            f = line.rstrip('\n').split('\t')
            if len(f) >= 4:
                tail = re.sub(r'\.git$', '', f[3]).rstrip('/').split('github.com/')[-1]
                repros.append((f[0], tail.replace('/', '-').lower()))
    bullets = bug_bullets()
    out = []
    for (side, code, key), a in agg.items():
        if len(a['projects']) < min_projects:
            continue
        status, repo = 'open', set()
        for proj in sorted(a['projects']):
            name = proj.split('/', 1)[1]
            orr = project_owner_repo(name)
            if any(orr == rr for _, rr in repros):
                repo.add(proj)
            owner_repo = orr.replace('-', '[-/]', 1)
            for text, fixed in bullets:
                if fixed and re.search(owner_repo, text, re.I) and f'TS{code}' in text:
                    status = 'fixed'
        if status != 'fixed' and repo:
            status = 'repro'
        out.append({'side': side, 'code': code, 'key': key, 'projects': sorted(a['projects']),
                    'n_projects': len(a['projects']), 'n_seeds': len(a['seeds']), 'status': status})
    out.sort(key=lambda e: (-e['n_projects'], -e['n_seeds'], e['code'], e['side'], e['key'] or ''))
    return out


def cmd_rank(min_projects, as_json):
    rows = rank_data(min_projects)
    if as_json:
        print(json.dumps(rows, indent=1))
        return
    print(f'{"proj":>4} {"seeds":>5} {"status":6} {"side":4} {"code":>5}  key / examples')
    for e in rows:
        ex = ', '.join(p.split('/', 1)[1] for p in e['projects'][:3])
        print(f'{e["n_projects"]:4} {e["n_seeds"]:5} {e["status"]:6} {e["side"]:4} TS{e["code"]:<5} '
              f'{e["key"] if e["key"] is not None else "(older record: code only)"}\n{"":29}{ex}')


# ----------------------------------------------------------------------------------------------------------- clean

def setup_running(seed):
    """The pid of a live `setup` of this seed (its pid file in the seed directory), else None"""
    try:
        pid = int(open(os.path.join(NS, str(seed), '.setup.pid')).read())
        os.kill(pid, 0)
        return pid
    except (OSError, ValueError):
        return None


def cmd_clean(seed, everything, keep):
    """Deletes the clones `compare` matched (those it compared and found the same), or with --all every clone; a clone
    never compared (setup still running, an interrupted compare, a spare beyond N) stays without --all"""
    pid = setup_running(seed)
    if pid:
        sys.exit(f'clean: a setup of seed {seed} is running (pid {pid}); refusing')
    # (The batch's package-manager caches: corepack's managers and ts6 stay)
    shutil.rmtree(seed_cache(seed), ignore_errors=True)
    base = os.path.join(NS, str(seed))
    if not os.path.isdir(base):
        return
    last_path = os.path.join(base, 'compare.json')
    compared = {r['name']: r.get('verdict') for r in json.load(open(last_path))} if os.path.exists(last_path) else {}
    # (--large: a clone holds several projects, named DIR#package; it counts as `same` when all of them are)
    group = collections.defaultdict(list)
    if os.path.exists(files(seed, 'tsv')):
        for line in open(files(seed, 'tsv')):
            nm = line.split('\t')[0]
            if '#' in nm:
                group[nm.split('#')[0]].append(nm)
    for d, nms in group.items():
        vs = [compared.get(x) for x in nms]
        compared[d] = 'same' if all(v == 'same' for v in vs) else next((v or 'not compared' for v in vs if v != 'same'), None)
    removed, kept = 0, []
    for d in sorted(os.listdir(base)):
        p = os.path.join(base, d)
        if not (os.path.isdir(p) and re.match(r'^\d{4}-', d)):
            continue
        # (--except holds back a clone under --all too: a kept divergence whose repro is still being cut)
        if d not in keep and (everything or compared.get(d) == 'same'):
            shutil.rmtree(p)
            removed += 1
        else:
            kept.append(f'{d} ({compared.get(d, "not compared")})')
    if everything and not kept:
        for f in ('tsc.jsonl', 'compare.json'):
            if os.path.exists(os.path.join(base, f)):
                os.remove(os.path.join(base, f))
        if not os.listdir(base):
            os.rmdir(base)
    print(f'clean: {removed} clones deleted' + (f', kept {len(kept)}: {" ".join(kept)}' if kept else ''))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', nargs='?', choices=('sample', 'setup', 'compare', 'clean', 'stats', 'rank'))
    ap.add_argument('--seed', type=int)
    ap.add_argument('--min', type=int, default=1, help='rank: only keys in at least this many projects')
    ap.add_argument('--json', action='store_true', help='rank: JSON instead of the table')
    ap.add_argument('-n', type=int, default=None, help='names to sample (2000; --large: repositories, 60)')
    ap.add_argument('--large', action='store_true', help='sample/setup: large-project mode (npm_sampling.md "Large '
                    'mode"): GitHub\'s large TypeScript repositories instead of npm packages; setup takes up to '
                    '--per-repo workspace packages of each as projects')
    ap.add_argument('--per-repo', type=int, default=3, help='setup --large: sub-projects accepted per repository')
    ap.add_argument('--min-ts-files', type=int, default=None, help='setup: skip a repository with fewer .ts sources '
                    'in its tree (default 200 with --large, else none)')
    ap.add_argument('-N', type=int, default=50, help='projects to accept')
    ap.add_argument('-j', type=int, default=4, help='candidates set up / projects compared at once')
    ap.add_argument('--last', action='store_true')
    ap.add_argument('-k', metavar='CODE', help="compare: list each project's instances of a code")
    ap.add_argument('--keep', action='store_true', help="compare and the whole batch: keep the clones (no deleting "
                    "matching projects, no clean)")
    ap.add_argument('--all', action='store_true', help='clean: the diverging clones too')
    ap.add_argument('--except', dest='keep_names', nargs='*', default=[], help='clean: also keep these clones')
    ap.add_argument('--prefer-large', action='store_true', help='setup: list the next candidates\' trees first and '
                    'set up the ones with the most .ts files first (bigger projects, never filtered on tsgo\'s verdict)')
    ap.add_argument('--skip-monorepos', action='store_true', help='setup: skip a candidate whose repository is a '
                    'workspace (package.json workspaces, pnpm-workspace.yaml, lerna.json, nx.json) before installing '
                    '(npm_sampling.md: it cuts the trial rejects, and a third of the accepted projects too)')
    args = ap.parse_args()
    c = args.command
    if c == 'rank':
        return cmd_rank(args.min, args.json)
    if args.seed is None:
        ap.error('--seed is required')
    if c in ('setup', 'compare', None) and not args.last:
        # (The pinned tsgo: a stale checkout fails here instead of filling tsc.jsonl with another version's output)
        subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')], check=True, stdout=subprocess.DEVNULL)
    if c == 'stats':
        return cmd_stats(args.seed)
    if c == 'sample' or (c is None and not os.path.exists(files(args.seed, 'candidates.jsonl'))):
        if args.large:
            cmd_sample_large(args.seed, args.n or 60)
        else:
            cmd_sample(args.seed, args.n or 2000)
    if c in ('setup', None):
        if not cmd_setup(args.seed, args.N, args.j, args.prefer_large, args.skip_monorepos, args.large,
                     args.min_ts_files if args.min_ts_files is not None else 200 if args.large else 0, args.per_repo) and c is None:
            sys.exit('setup: fewer than N accepted (sample more names with -n, then rerun)')
    if c in ('compare', None):
        cmd_compare(args.seed, args.j, not args.keep, args.last, args.k)
    if c == 'clean' or (c is None and not args.keep):
        cmd_clean(args.seed, args.all, args.keep_names)


if __name__ == '__main__':
    main()
