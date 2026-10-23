#!/usr/bin/env python3
"""Random npm projects as a correctness measure (agents/notes/npm_sampling.md, the human's method): a seed picks
~2000 package names from all of npm, the TypeScript ones with a GitHub repository are cloned, installed (scripts
never run) and given a tsconfig.bench.json tsgo 7.1 accepts, the first N whose trial tsgo run looks sane are checked
by tsgo and tttt, and every difference is a tttt bug (or a formatting-only one, not counted).

  scripts/npmsample.py --seed S [-N 50]       # sample, setup, compare, clean: the whole batch
  scripts/npmsample.py sample --seed S [-n 2000]   # test/npmsample/S.candidates.jsonl (TS packages, GitHub repos)
  scripts/npmsample.py setup --seed S [-N 50]      # clone + install + bench config + tsgo trial, resumable
  scripts/npmsample.py compare --seed S [--last] [-k CODE] [--no-threads1]
  scripts/npmsample.py clean --seed S [--all] [--except NAME...]   # delete the clones (diverging ones kept without --all)

Files: test/npmsample/S.candidates.jsonl (sample order), S.setup.jsonl (one line per candidate looked at: accepted or
the skip reason, package@version, the package manager, every config change), S.tsv (name, config path relative to
thirdparty/npmsample/S: the first N accepted in sample order), S.results.jsonl (per project: tsgo's and tttt's counts
and exit codes, missing/extra codes and an example, whether it persists at --threads 1, the tttt commit). The clones
live in thirdparty/npmsample/S/<i>-<owner>-<repo> (untracked, ~8-10 GB per 50) and are deleted by `clean` (run at
the end of a batch unless --keep) except the diverging ones, kept until their repro is in test/cases (then
`clean --all`). tsgo's output is cached per batch in thirdparty/npmsample/S/tsc.jsonl, the last comparison in
thirdparty/npmsample/S/compare.json (--last, -k).

Installs run under thirdparty/node's pinned node 24 (corepack for pnpm and yarn) and bun, scripts off, caches in
thirdparty/npmsample/.cache (the pnpm store is deleted after each setup run), nothing shared with thirdparty/sources.
tsgo runs through tttt-oracle (never writes build info), tttt through tttt_run.py in a read-only bwrap sandbox.
"""
import argparse, collections, concurrent.futures, json, os, random, re, shutil, subprocess, sys, tarfile, threading, time
import urllib.error, urllib.parse, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tscformat  # noqa: E402
import tttt_run  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NS = os.path.realpath(os.path.join(ROOT, 'thirdparty/npmsample'))  # (tsgo prints real paths)
CACHE = os.path.join(NS, '.cache')
TESTDIR = os.path.join(ROOT, 'test/npmsample')
NODE_BIN = os.path.join(ROOT, 'thirdparty/node/node-v24.21.0-linux-x64/bin')
BUN = os.path.join(ROOT, 'thirdparty/node/bun-1.4.3/package/bin/bun')
TS6_VERSION = '6.0.3'
TS6 = os.path.join(CACHE, 'ts6/node_modules/typescript/bin/tsc')
TSGO = os.path.join(ROOT, 'thirdparty/typescript/built/tsc')
ORACLE = os.path.join(ROOT, 'thirdparty/typescript/built/tttt-oracle')
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
    env = {**os.environ, 'GIT_TERMINAL_PROMPT': '0', 'GIT_ASKPASS': '/bin/false', 'GIT_CONFIG_COUNT': '3',
           'GIT_CONFIG_KEY_0': 'url.https://github.com/.insteadOf', 'GIT_CONFIG_VALUE_0': 'git://github.com/',
           'GIT_CONFIG_KEY_1': 'url.https://github.com/.insteadOf', 'GIT_CONFIG_VALUE_1': 'ssh://git@github.com/',
           'GIT_CONFIG_KEY_2': 'url.https://github.com/.insteadOf', 'GIT_CONFIG_VALUE_2': 'git@github.com:'}
    return env


def install_env(d):
    shims = os.path.join(CACHE, 'shims')
    if not os.path.exists(os.path.join(shims, 'yarn')):
        os.makedirs(shims, exist_ok=True)
        with open(os.path.join(shims, 'yarn'), 'w') as f:
            f.write('#!/bin/sh\nexec corepack yarn "$@"\n')
        os.chmod(os.path.join(shims, 'yarn'), 0o755)
    env = {**git_env(), 'PATH': NODE_BIN + ':' + shims + ':' + os.environ['PATH'],
           'COREPACK_ENABLE_DOWNLOAD_PROMPT': '0', 'COREPACK_ENABLE_STRICT': '0', 'COREPACK_HOME': f'{CACHE}/corepack',
           'CI': '1', 'YARN_NODE_LINKER': 'node-modules', 'YARN_ENABLE_SCRIPTS': 'false', 'YARN_ENABLE_GLOBAL_CACHE': 'true',
           'YARN_GLOBAL_FOLDER': f'{CACHE}/berry', 'YARN_ENABLE_TELEMETRY': '0', 'HUSKY': '0',
           'PUPPETEER_SKIP_DOWNLOAD': '1', 'ELECTRON_SKIP_BINARY_DOWNLOAD': '1', 'PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD': '1',
           'CYPRESS_INSTALL_BINARY': '0', 'npm_config_ignore_scripts': 'true', 'npm_config_engine_strict': 'false',
           'NPM_TOKEN': os.environ.get('NPM_TOKEN', '')}
    # (A project .npmrc's ${VAR}s fail the install when unset: empty)
    for rc in (os.path.join(d, '.npmrc'), os.path.join(d, '.yarnrc.yml')):
        if os.path.exists(rc):
            for var in re.findall(r'\$\{(\w+)', open(rc, errors='replace').read()):
                env.setdefault(var, '')
    return env


def install_commands(d):
    """The install commands to try in order (the lockfile's, then its non-frozen fallback), or [] without package.json"""
    if not os.path.exists(os.path.join(d, 'package.json')):
        return []
    try:
        pj = json.load(open(os.path.join(d, 'package.json')))
    except (ValueError, UnicodeDecodeError):
        pj = {}
    pm = pj.get('packageManager', '') if isinstance(pj.get('packageManager'), str) else ''
    has = lambda f: os.path.exists(os.path.join(d, f))  # noqa: E731
    npm = [os.path.join(NODE_BIN, 'npm'), '--ignore-scripts', '--legacy-peer-deps', '--no-audit', '--no-fund',
           '--engine-strict=false', f'--cache={CACHE}/npm']
    if pm.startswith('pnpm') or (not pm and has('pnpm-lock.yaml')):
        if not pm:
            engine = ((pj.get('engines') or {}) if isinstance(pj.get('engines'), dict) else {}).get('pnpm', '')
            lock = open(os.path.join(d, 'pnpm-lock.yaml')).readline() if has('pnpm-lock.yaml') else ''
            pm = (f'pnpm@{engine}' if isinstance(engine, str) and engine[:1].isdigit() else
                  'pnpm@8' if "'6." in lock else 'pnpm@7' if lock.startswith('lockfileVersion: 5') else 'pnpm@10')
        base = ['corepack', pm.split('+')[0], 'install', '--ignore-scripts', f'--store-dir={CACHE}/pnpm-store',
                '--config.engine-strict=false']
        return [base + ['--frozen-lockfile'], base + ['--no-frozen-lockfile']]
    if pm.startswith('yarn') or (not pm and has('yarn.lock')):
        if not pm and has('yarn.lock') and '__metadata:' in open(os.path.join(d, 'yarn.lock'), errors='replace').read(4096):
            pm = 'yarn@4'
        if not pm or pm.startswith('yarn@1'):
            base = ['corepack', pm.split('+')[0] if pm else 'yarn@1', 'install', '--ignore-scripts', '--ignore-engines',
                    '--non-interactive', f'--cache-folder={CACHE}/yarn1']
            return [base + ['--frozen-lockfile'], base]
        return [['corepack', 'yarn', 'install', '--mode=skip-build'],
                ['env', 'YARN_ENABLE_IMMUTABLE_INSTALLS=false', 'corepack', 'yarn', 'install', '--mode=skip-build']]
    if pm.startswith('bun') or (not has('package-lock.json') and (has('bun.lock') or has('bun.lockb'))):
        base = [BUN, 'install', '--ignore-scripts', f'--cache-dir={CACHE}/bun']
        return [base + ['--frozen-lockfile'], base]
    if has('package-lock.json') or has('npm-shrinkwrap.json'):
        return [npm[:1] + ['ci'] + npm[1:], npm[:1] + ['install'] + npm[1:], npm[:1] + ['install', '--force'] + npm[1:]]
    return [npm[:1] + ['install'] + npm[1:], npm[:1] + ['install', '--force'] + npm[1:]]


def install_dir(repo, pkg):
    """Where to install: the nearest directory from pkg up to the repo root with a lockfile, else the root when it
    declares workspaces, else pkg"""
    d = pkg
    while True:
        if any(os.path.exists(os.path.join(d, f)) for f in LOCKFILES):
            return d
        if d == repo:
            break
        d = os.path.dirname(d)
    try:
        if 'workspaces' in json.load(open(os.path.join(repo, 'package.json'))):
            return repo
    except (OSError, ValueError, UnicodeDecodeError):
        pass
    return pkg if os.path.exists(os.path.join(pkg, 'package.json')) else repo


def install(repo, pkg):
    """(package manager command shown, None) on success, else (shown, failure tail)"""
    d = install_dir(repo, pkg)
    cmds = install_commands(d)
    if not cmds:
        return 'none', None
    env = install_env(d)
    tail = ''
    for cmd in cmds:
        shown = ' '.join(os.path.basename(a) if i == 0 else a for i, a in enumerate(cmd[:3]))
        try:
            p = subprocess.run(cmd, cwd=d, env=env, capture_output=True, text=True, timeout=1200,
                               stdin=subprocess.DEVNULL)
            if p.returncode == 0:
                return f'{shown} ({os.path.relpath(d, repo)})', None
            tail = (p.stdout + p.stderr)[-600:]
        except subprocess.TimeoutExpired:
            tail = 'timeout'
    return shown, tail


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
    p = subprocess.run([os.path.join(NODE_BIN, 'node'), TS6, '-p', os.path.basename(config), '--showConfig'],
                       cwd=os.path.dirname(config), capture_output=True, text=True, timeout=120)
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
        p = subprocess.run([ORACLE, '--text'], input=project + '\n', cwd=cwd, capture_output=True, text=True,
                           timeout=timeout)
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
    p = subprocess.run([TSGO, '-p', os.path.basename(bench), '--listFilesOnly'], cwd=os.path.dirname(bench),
                       capture_output=True, text=True, timeout=600)
    return sum(1 for line in p.stdout.splitlines()
               if line.startswith(repo + '/') and '/node_modules/' not in line)


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


def clone(url, repo):
    env = git_env()
    run = lambda *a, **kw: subprocess.run(['git', *a], env=env, capture_output=True, text=True, timeout=900,  # noqa: E731
                                          stdin=subprocess.DEVNULL, **kw)
    p = run('clone', '-q', '--depth', '1', '--single-branch', '--filter=blob:none', '--no-checkout', url, repo)
    if p.returncode == 0:
        p = run('-C', repo, 'sparse-checkout', 'set', '--no-cone', '/*', *[f'!*.{e}' for e in SPARSE_EXCLUDE])
    if p.returncode == 0:
        p = run('-C', repo, 'checkout', '-q')
    return p


def setup_one(seed, c):
    rec = {k: c[k] for k in ('i', 'name', 'version', 'repo', 'directory')}
    repo = os.path.join(NS, str(seed), repo_dirname(c))
    marker = os.path.join(repo, '.npmsample-installed')
    if not os.path.exists(marker):
        if os.path.exists(repo):
            shutil.rmtree(repo)
        p = clone(c['repo'], repo)
        if p.returncode != 0:
            shutil.rmtree(repo, ignore_errors=True)
            return {**rec, 'status': 'skip', 'reason': 'clone: ' + (p.stderr.strip().splitlines() or [''])[-1][:200]}
    rec['commit'] = subprocess.run(['git', '-C', repo, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    pkg = os.path.normpath(os.path.join(repo, c['directory'])) if c['directory'] else repo
    if not os.path.isdir(pkg) or not pkg.startswith(repo):
        return {**rec, 'status': 'skip', 'reason': 'repository.directory gone'}
    config = next((p for p in (os.path.join(pkg, 'tsconfig.json'), os.path.join(repo, 'tsconfig.json'))
                   if os.path.exists(p)), None)
    if not config:
        return {**rec, 'status': 'skip', 'reason': 'no tsconfig.json'}
    if count_ts(pkg) < 3:
        return {**rec, 'status': 'skip', 'reason': 'fewer than 3 .ts files'}
    if not os.path.exists(marker):
        pm, fail = install(repo, pkg)
        if fail is not None:
            return {**rec, 'status': 'skip', 'pm': pm, 'reason': 'install: ' + ' '.join(fail.split())[-300:]}
        prune(repo)
        with open(marker, 'w') as f:
            f.write(pm + '\n')
    rec['pm'] = open(marker).read().strip()
    bench, changes = make_bench(config, repo)
    rec['changes'] = changes
    if bench is None:
        return {**rec, 'status': 'skip', 'reason': changes}
    rec['config'] = os.path.relpath(bench, os.path.join(NS, str(seed)))
    text, status = tsgo_run('tsconfig.bench.json', os.path.dirname(bench))
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
    if own < 1 or len(diags) > 100 or codes[2307] > 5:
        return {**rec, 'status': 'reject', 'reason': f'trial: {own} own files, {len(diags)} errors, '
                                                     f'{codes[2307]} TS2307'}
    return {**rec, 'status': 'accepted'}


def ensure_ts6():
    if not os.path.exists(TS6):
        d = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(TS6))))
        os.makedirs(d, exist_ok=True)
        subprocess.run([os.path.join(NODE_BIN, 'npm'), 'install', '--ignore-scripts', '--no-audit', '--no-fund',
                        f'--cache={CACHE}/npm', f'typescript@{TS6_VERSION}'], cwd=d, check=True,
                       env={**os.environ, 'PATH': NODE_BIN + ':' + os.environ['PATH']}, stdout=subprocess.DEVNULL)


def write_tsv(seed, n):
    recs = sorted(read_jsonl(files(seed, 'setup.jsonl')), key=lambda r: r['i'])
    acc = [r for r in recs if r['status'] == 'accepted'][:n]
    with open(files(seed, 'tsv'), 'w') as f:
        for r in acc:
            f.write(f'{repo_dirname(r)}\t{r["config"]}\n')
    return acc


def cmd_setup(seed, n, jobs):
    cands = read_jsonl(files(seed, 'candidates.jsonl'))
    if not cands:
        sys.exit(f'no candidates: run `scripts/npmsample.py sample --seed {seed}` first')
    ensure_ts6()
    log = files(seed, 'setup.jsonl')
    done = {r['i']: r for r in read_jsonl(log)}
    accepted = sum(1 for r in done.values() if r['status'] == 'accepted')
    todo = [c for c in cands if c['i'] not in done]
    os.makedirs(os.path.join(NS, str(seed)), exist_ok=True)
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
            r = setup_one(seed, c)
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
    if accepted < n and todo:
        with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
            futs = [ex.submit(one, c) for c in todo]
            for c, fu in zip(todo, futs):
                r = fu.result()
                if r is None:
                    continue
                tag = r['status'] if r['status'] != 'skip' and r['status'] != 'reject' else r['status'] + ': ' + str(r['reason'])[:150]
                if r['status'] == 'accepted':
                    accepted += 1
                    tag += f' ({accepted}/{n}, tsgo {r["tsgo"]["errors"]} errors)'
                print(f'  {c["i"]:4} {c["name"][:40]:40} {tag}', flush=True)
                if accepted >= n:
                    stop.set()
                    for f2 in futs:
                        f2.cancel()
                    break
    # (The package managers' caches only matter while installing: the installed files are copies or hardlinks)
    for d in ('pnpm-store', 'npm', 'yarn1', 'bun', 'berry'):
        shutil.rmtree(os.path.join(CACHE, d), ignore_errors=True)
    acc = write_tsv(seed, n)
    print(f'setup: {len(acc)}/{n} accepted -> {os.path.relpath(files(seed, "tsv"), ROOT)}')
    return len(acc) >= n


# --------------------------------------------------------------------------------------------------------- compare

LIBPATH = re.compile(r'(?:bundled:///libs/|\S*/node_modules/typescript/lib/|\S*?/built/local/)(lib\.[\w.-]*d\.ts)')


def normalize(text):
    return sorted(LIBPATH.sub(r'LIB/\1', line) for line in text.replace('\r', '').split('\n') if line.strip())


def sandbox():
    return ['bwrap', '--ro-bind', '/', '/', '--bind', '/tmp', '/tmp', '--dev', '/dev', '--proc', '/proc',
            '--die-with-parent', '--'] if shutil.which('bwrap') else []


def tttt_check(bench, threads=None):
    extra = ['--threads', str(threads)] if threads else []
    r = tttt_run.run([*sandbox(), TTTT, '-p', os.path.basename(bench), '--noEmit', '--pretty', 'false', *extra],
                     os.path.dirname(bench), timeout=900, mem_gb=16, threads=threads, stderr=subprocess.PIPE)
    return re.sub(r'\x1b\[[0-9;]*m', '', r.text()), r.code, r.status


def keys(text):
    return collections.Counter((f, l, c) for f, l, _, c, _ in tscformat.parse(text))


def head_commit():
    return subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip()


def compare_one(seed, name, config, tsc_cache, lock, threads1):
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
    g, d = keys(g_text), keys(d_text)
    miss, extra = g - d, d - g
    r = {'name': name, 'config': config, 'tsgo_exit': g_status, 'tsgo_n': sum(g.values()), 'tttt_exit': d_code,
         'tttt_status': d_status, 'tttt_n': sum(d.values()), 'missing': sum(miss.values()), 'extra': sum(extra.values()),
         'codes_missing': sorted({k[2] for k in miss}), 'codes_extra': sorted({k[2] for k in extra})}
    same = normalize(g_text) == normalize(d_text) and g_status == d_code
    r['verdict'] = ('crash' if d_status != 'ok' else 'same' if same else 'text' if not miss and not extra else 'differ')
    lines = {(f, l, c): f'{f}({l}): TS{c} {m[:200]}' for f, l, _, c, m in tscformat.parse(g_text + '\n' + d_text)}
    if miss or extra:
        k = sorted(extra or miss)[0]
        r['example'] = ('tttt only: ' if extra else 'tsgo only: ') + lines.get(k, str(k))
    elif not same:
        a, b = normalize(g_text), normalize(d_text)
        first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
        r['example'] = f'tsgo: {a[first] if first < len(a) else "(end)"} / tttt: {b[first] if first < len(b) else "(end)"}'
        if g_status != d_code:
            r['example'] += f' (exit {g_status} vs {d_code})'
    if r['verdict'] != 'same' and threads1:
        t_text, t_code, t_status = tttt_check(bench, threads=1)
        r['threads1_same'] = normalize(t_text) == normalize(d_text) and t_code == d_code
    r['_g'] = g_text
    r['_d'] = d_text
    return r


def cmd_compare(seed, jobs, threads1, last, code):
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
            results = list(ex.map(lambda row: compare_one(seed, row[0], row[1], tsc_cache, lock, threads1), rows))
        json.dump(results, open(last_path, 'w'))
        commit = head_commit()
        with open(files(seed, 'results.jsonl'), 'w') as f:
            for r in results:
                if r.get('verdict') != 'gone':
                    f.write(json.dumps({**{k: v for k, v in r.items() if not k.startswith('_')}, 'tttt_commit': commit}) + '\n')
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
    print(f'{"project":44} {"tsgo":>5} {"tttt":>5} {"miss":>5} {"extra":>5}  t1  verdict  codes / example')
    for r in results:
        if r['verdict'] == 'same':
            continue
        codes = ' '.join([f'-{c}' for c in r['codes_missing']] + [f'+{c}' for c in r['codes_extra']])
        t1 = {True: 'same', False: 'DIFF', None: '-'}[r.get('threads1_same')]
        print(f'{r["name"][:44]:44} {r["tsgo_n"]:5} {r["tttt_n"]:5} {r["missing"]:5} {r["extra"]:5}  {t1:4} '
              f'{r["verdict"]:7}  {codes}\n{"":46}{r.get("example", r.get("tttt_status", ""))[:200]}')
    n = len(results)
    same = sum(r['verdict'] == 'same' for r in results)
    fmt = sum(r['verdict'] in ('same', 'text') for r in results)
    clean = sum((r['tsgo_n'] == 0) == (r['tttt_n'] == 0) and r['tttt_status'] == 'ok' for r in results)
    match = sum(r['verdict'] in ('same', 'text') or (r['missing'] == r['extra'] == 0) for r in results)
    print(f'{n} projects: {same} byte-identical modulo lib paths ({100 * same / max(n, 1):.0f}%), {match} same '
          f'(file, line, code) ({fmt} counting text-only), clean agreement {clean}/{n} ({100 * clean / max(n, 1):.0f}%)')


# ----------------------------------------------------------------------------------------------------------- clean

def cmd_clean(seed, everything, keep):
    base = os.path.join(NS, str(seed))
    if not os.path.isdir(base):
        return
    diverging = set()
    last_path = os.path.join(base, 'compare.json')
    if os.path.exists(last_path) and not everything:
        diverging = {r['name'] for r in json.load(open(last_path)) if r.get('verdict') not in ('same', 'gone')}
    diverging |= set(keep)
    removed = 0
    for d in sorted(os.listdir(base)):
        p = os.path.join(base, d)
        if os.path.isdir(p) and re.match(r'^\d{4}-', d) and d not in diverging:
            shutil.rmtree(p)
            removed += 1
    if everything:
        for f in ('tsc.jsonl', 'compare.json'):
            if os.path.exists(os.path.join(base, f)):
                os.remove(os.path.join(base, f))
        if not os.listdir(base):
            os.rmdir(base)
    print(f'clean: {removed} clones deleted' + (f', kept {len(diverging)}: {" ".join(sorted(diverging))}'
                                                 if diverging else ''))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', nargs='?', choices=('sample', 'setup', 'compare', 'clean'))
    ap.add_argument('--seed', type=int, required=True)
    ap.add_argument('-n', type=int, default=2000, help='names to sample')
    ap.add_argument('-N', type=int, default=50, help='projects to accept')
    ap.add_argument('-j', type=int, default=4, help='candidates set up / projects compared at once')
    ap.add_argument('--last', action='store_true')
    ap.add_argument('-k', metavar='CODE', help="compare: list each project's instances of a code")
    ap.add_argument('--no-threads1', action='store_true', help='compare: skip the --threads 1 rerun of differences')
    ap.add_argument('--keep', action='store_true', help="the whole batch: don't clean at the end")
    ap.add_argument('--all', action='store_true', help='clean: the diverging clones too')
    ap.add_argument('--except', dest='keep_names', nargs='*', default=[], help='clean: also keep these clones')
    args = ap.parse_args()
    c = args.command
    if c == 'sample' or (c is None and not os.path.exists(files(args.seed, 'candidates.jsonl'))):
        cmd_sample(args.seed, args.n)
    if c in ('setup', None):
        if not cmd_setup(args.seed, args.N, args.j) and c is None:
            sys.exit('setup: fewer than N accepted (sample more names with -n, then rerun)')
    if c in ('compare', None):
        cmd_compare(args.seed, args.j, not args.no_threads1, args.last, args.k)
    if c == 'clean' or (c is None and not args.keep):
        cmd_clean(args.seed, args.all, args.keep_names)


if __name__ == '__main__':
    main()
