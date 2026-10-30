#!/usr/bin/env python3
"""Installing an untrusted repository's dependencies without running any of its code, or its dependencies' code, and
running our own tools on it, in a bubblewrap sandbox (agents/notes/install_security.md).

    import installsandbox as sb
    r = sb.install(repo_dir, install_dir, cache_dir)   # {'pm', 'ok', 'tail', 'changes', 'refused', 'argv', 'env'}
    p = sb.run(cmd, cwd, rw=[...], ro=[...], network=False)   # our own tools (tsc 6 --showConfig, tsgo, tttt)

    scripts/installsandbox.py install DIR [--root REPO] --cache DIR [--dry-run]   # --dry-run: the plan, argv and env
    scripts/installsandbox.py run [--rw DIR]... [--ro DIR]... [--net] [--cwd DIR] -- CMD...

The sandbox (sandbox_argv): bwrap with / read-only, a private /dev, /proc and /tmp, an empty tmpfs over $HOME (~/.ssh,
~/.claude, ~/.config, ~/.gitconfig and every checkout under it invisible) and over /run (no D-Bus or systemd user
sockets), then only the directories named bound back: `rw` read-write (the repository, the caches), `ro` read-only
(node, bun, tsgo, tsc 6). Every namespace unshared but the network's when network=True (installs fetch; our tools run
without). The environment is built from nothing (never os.environ): PATH, HOME (the empty tmpfs), LANG, the
package managers' settings below; no tokens.

What may execute during an install: the package manager we pick and nothing else.
- The manager is pinned here, never the repository's: its family from the lockfile (or packageManager), its version
  from ALLOWED by major (packageManager's major when allowed, else the lockfile format's), fetched by corepack (which
  verifies the registry's signatures) with an explicit `corepack NAME@VERSION`, so packageManager, engines, yarnPath
  and .yarn/releases never choose what runs.
- npm: --ignore-scripts. pnpm: --ignore-scripts --config.ignore-scripts=true --ignore-pnpmfile (no .pnpmfile.cjs, no
  pnpmfile setting), --config.manage-package-manager-versions=false and --config.pm-on-fail=ignore (pnpm 9.7+
  switches to packageManager's version by itself otherwise); a pnpm-workspace.yaml with configDependencies, pnpmfile or hooks is refused. yarn 1:
  --ignore-scripts, YARN_IGNORE_PATH=1 (no .yarnrc yarn-path). yarn berry: --mode=skip-build, YARN_ENABLE_SCRIPTS=false,
  YARN_IGNORE_PATH=1, YARN_PLUGINS empty, and the keys that load or run code (yarnPath, plugins, afterInstall and other
  *Hook/*Command keys of plugins) stripped from the repo's .yarnrc.yml first (`changes`: the copy is ours, a clone; set
  strip=False to refuse such a repo instead). bun: --ignore-scripts (bun's own pinned binary).
- The registry is forced to registry.npmjs.org (--registry, YARN_NPM_REGISTRY_SERVER, NPM_CONFIG_REGISTRY), against
  .npmrc/.yarnrc mirrors; a lockfile's own resolved URLs stay what they are.
- The fallbacks (`npm install`, `--no-frozen-lockfile`, non-immutable yarn) re-resolve, under the same rules.
Nothing from node_modules is ever run by us: no npx, no bins, no `yarn <script>`.
"""
import argparse, json, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
THIRD = os.path.realpath(os.path.join(ROOT, 'thirdparty'))
NODE_DIR = os.path.realpath(os.path.join(THIRD, 'node'))
NODE_BIN = os.path.join(NODE_DIR, 'node-v24.21.0-linux-' + {'aarch64': 'arm64'}.get(os.uname().machine, 'x64') + '/bin')
BUN = os.path.join(NODE_DIR, 'bun-1.4.3/package/bin/bun')
REGISTRY = 'https://registry.npmjs.org'
HOME = os.path.realpath(os.path.expanduser('~'))
# The package-manager versions an install may use, by major (corepack downloads them, signature-checked). Berry 2 and
# 3 lockfiles install with 4 (non-immutably when it rewrites them)
ALLOWED = {
    'pnpm': {7: '7.33.7', 8: '8.15.9', 9: '9.15.9', 10: '10.30.3', 11: '11.24.0', 12: '12.15.0'},
    'yarn': {1: '1.22.22', 2: '4.11.0', 3: '4.11.0', 4: '4.11.0'},
}
LOCKFILES = ('pnpm-lock.yaml', 'yarn.lock', 'bun.lock', 'bun.lockb', 'package-lock.json', 'npm-shrinkwrap.json')
# .yarnrc.yml keys that load or run code: the repo's yarn bundle, plugins, and the hooks plugins read
YARNRC_CODE_KEYS = re.compile(r'^(yarnPath|plugins|afterInstall|\w+Hook|\w+Command)\s*:', re.M)
PNPM_WORKSPACE_REFUSE = re.compile(r'^(configDependencies|pnpmfile|globalPnpmfile|hooks)\s*:', re.M)


def sandbox_argv(rw=(), ro=(), network=False, cwd=None):
    """The bwrap command prefix: / read-only, $HOME and /run empty, rw/ro directories bound back (real paths)"""
    argv = ['bwrap', '--die-with-parent', '--new-session', '--unshare-all'] + (['--share-net'] if network else []) + [
        '--ro-bind', '/', '/', '--dev', '/dev', '--proc', '/proc', '--tmpfs', '/tmp', '--tmpfs', '/run',
        '--tmpfs', HOME]
    if network:
        # (DNS: resolv.conf is often a symlink into /run)
        rc = os.path.realpath('/etc/resolv.conf')
        if rc.startswith('/run/') and os.path.exists(rc):
            argv += ['--ro-bind', rc, rc]
    for d in ro:
        d = os.path.realpath(d)
        argv += ['--ro-bind', d, d]
    for d in rw:
        d = os.path.realpath(d)
        argv += ['--bind', d, d]
    if cwd:
        argv += ['--chdir', os.path.realpath(cwd)]
    return argv + ['--']


def clean_env(extra=None):
    env = {'PATH': f'{NODE_BIN}:/usr/local/bin:/usr/bin:/bin', 'HOME': HOME, 'LANG': 'C.UTF-8', 'TERM': 'dumb'}
    env.update(extra or {})
    return env


def run(cmd, cwd, rw=(), ro=(), network=False, env=None, timeout=None, **kw):
    """cmd in the sandbox (subprocess.run's result; capture_output and text by default)"""
    argv = sandbox_argv(rw, ro, network, cwd) + list(cmd)
    kw.setdefault('capture_output', True)
    kw.setdefault('text', True)
    return subprocess.run(argv, env=clean_env(env), timeout=timeout, stdin=subprocess.DEVNULL, **kw)


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


def _major(spec):
    m = re.match(r'^\D*(\d+)', spec or '')
    return int(m.group(1)) if m else None


def plan(d):
    """(family, version, commands, notes) for installing d; family None without package.json"""
    if not os.path.exists(os.path.join(d, 'package.json')):
        return None, None, [], ['no package.json']
    try:
        pj = json.load(open(os.path.join(d, 'package.json')))
    except (ValueError, UnicodeDecodeError):
        pj = {}
    pm = pj.get('packageManager') if isinstance(pj.get('packageManager'), str) else ''
    has = lambda f: os.path.exists(os.path.join(d, f))  # noqa: E731
    notes = [f'packageManager {pm} (version not used)'] if pm else []
    reg = f'--registry={REGISTRY}'
    if pm.startswith('pnpm') or (not pm.split('@')[0] in ('yarn', 'npm', 'bun') and has('pnpm-lock.yaml')):
        lock = open(os.path.join(d, 'pnpm-lock.yaml')).readline() if has('pnpm-lock.yaml') else ''
        major = _major(pm.split('@', 1)[1]) if '@' in pm else None
        if major not in ALLOWED['pnpm']:
            major = 7 if lock.startswith('lockfileVersion: 5') else 8 if "'6." in lock or '"6.' in lock else 10
        v = ALLOWED['pnpm'][major]
        base = ['corepack', f'pnpm@{v}', 'install', '--ignore-scripts', '--config.ignore-scripts=true',
                '--ignore-pnpmfile', reg, '--config.engine-strict=false',
                # (pnpm 9.7+ would fetch and run packageManager's version itself; 11 refuses another one)
                '--config.manage-package-manager-versions=false', '--config.pm-on-fail=ignore', '--store-dir={CACHE}/pnpm-store']
        return 'pnpm', v, [base + ['--frozen-lockfile'], base + ['--no-frozen-lockfile']], notes
    if pm.startswith('yarn') or (not pm and has('yarn.lock')):
        lock = open(os.path.join(d, 'yarn.lock'), errors='replace').read(4096) if has('yarn.lock') else ''
        major = _major(pm.split('@', 1)[1]) if '@' in pm else None
        if major not in ALLOWED['yarn']:
            major = 4 if '__metadata:' in lock else 1
        v = ALLOWED['yarn'][major]
        if major == 1:
            base = ['corepack', f'yarn@{v}', 'install', '--ignore-scripts', '--ignore-engines', '--non-interactive',
                    '--cache-folder={CACHE}/yarn1', reg]
            return 'yarn1', v, [base + ['--frozen-lockfile'], base], notes
        base = ['corepack', f'yarn@{v}', 'install', '--mode=skip-build']
        return 'berry', v, [base, ['env', 'YARN_ENABLE_IMMUTABLE_INSTALLS=false'] + base], notes
    if pm.startswith('bun') or (not has('package-lock.json') and (has('bun.lock') or has('bun.lockb'))):
        base = [BUN, 'install', '--ignore-scripts', reg, '--cache-dir={CACHE}/bun']
        return 'bun', '1.4.3', [base + ['--frozen-lockfile'], base], notes
    npm = [os.path.join(NODE_BIN, 'npm')]
    flags = ['--ignore-scripts', '--legacy-peer-deps', '--no-audit', '--no-fund', '--engine-strict=false', reg,
             '--cache={CACHE}/npm']
    first = [npm + ['ci'] + flags] if has('package-lock.json') or has('npm-shrinkwrap.json') else []
    return 'npm', 'bundled', first + [npm + ['install'] + flags, npm + ['install', '--force'] + flags], notes


def pm_env(cache, corepack_home):
    return {'COREPACK_HOME': corepack_home, 'COREPACK_ENABLE_DOWNLOAD_PROMPT': '0', 'COREPACK_ENABLE_STRICT': '0',
            'COREPACK_ENABLE_AUTO_PIN': '0', 'COREPACK_ENABLE_PROJECT_SPEC': '0', 'CI': '1',
            'YARN_IGNORE_PATH': '1', 'YARN_PLUGINS': '', 'YARN_ENABLE_SCRIPTS': 'false',
            'YARN_NODE_LINKER': 'node-modules', 'YARN_ENABLE_GLOBAL_CACHE': 'true', 'YARN_GLOBAL_FOLDER': f'{cache}/berry',
            'YARN_ENABLE_TELEMETRY': '0', 'YARN_NPM_REGISTRY_SERVER': REGISTRY, 'YARN_ENABLE_IMMUTABLE_INSTALLS': 'true',
            'NPM_CONFIG_REGISTRY': REGISTRY, 'npm_config_ignore_scripts': 'true', 'npm_config_engine_strict': 'false',
            'npm_config_update_notifier': 'false', 'BUN_CONFIG_REGISTRY': REGISTRY, 'HUSKY': '0',
            'GIT_TERMINAL_PROMPT': '0', 'GIT_CONFIG_COUNT': '3',
            'GIT_CONFIG_KEY_0': 'url.https://github.com/.insteadOf', 'GIT_CONFIG_VALUE_0': 'git://github.com/',
            'GIT_CONFIG_KEY_1': 'url.https://github.com/.insteadOf', 'GIT_CONFIG_VALUE_1': 'ssh://git@github.com/',
            'GIT_CONFIG_KEY_2': 'url.https://github.com/.insteadOf', 'GIT_CONFIG_VALUE_2': 'git@github.com:'}


# Berry's own settings (yarn 4): once a repo's plugins are stripped, the settings they declared are unknown to yarn
# and fail the install, so then only these stay
BERRY_CORE_KEYS = {
    'cacheFolder', 'cacheMigrationMode', 'changesetBaseRefs', 'changesetIgnorePatterns', 'checksumBehavior',
    'cloneConcurrency', 'compressionLevel', 'constraintsPath', 'defaultLanguageName', 'defaultProtocol',
    'defaultSemverRangePrefix', 'deferredVersionFolder', 'enableColors', 'enableConstraintsChecks',
    'enableGlobalCache', 'enableHardenedMode', 'enableHyperlinks', 'enableImmutableCache', 'enableImmutableInstalls',
    'enableInlineBuilds', 'enableInlineHunks', 'enableMessageNames', 'enableMirror', 'enableNetwork',
    'enableOfflineMode', 'enableProgressBars', 'enableScripts', 'enableStrictSsl', 'enableTelemetry',
    'enableTimers', 'enableTransparentWorkspaces', 'globalFolder', 'httpProxy', 'httpRetry', 'httpTimeout',
    'httpsCaFilePath', 'httpsCertFilePath', 'httpsKeyFilePath', 'httpsProxy', 'immutablePatterns',
    'initFields', 'initScope', 'injectEnvironmentFiles', 'installStatePath', 'logFilters', 'networkConcurrency',
    'networkSettings', 'nmHoistingLimits', 'nmMode', 'nmSelfReferences', 'nodeLinker', 'npmAlwaysAuth',
    'npmAuditExcludePackages', 'npmAuditIgnoreAdvisories', 'npmAuditRegistry', 'npmAuthIdent', 'npmAuthToken',
    'npmMinimalAgeGate', 'npmPreapprovedPackages', 'npmPublishAccess', 'npmPublishProvenance', 'npmPublishRegistry',
    'npmRegistries', 'npmRegistryServer', 'npmScopes', 'packageExtensions', 'patchFolder', 'pnpDataPath',
    'pnpEnableEsmLoader', 'pnpEnableInlining', 'pnpFallbackMode', 'pnpIgnorePatterns', 'pnpMode', 'pnpShebang',
    'pnpUnpluggedFolder', 'preferDeferredVersions', 'preferInteractive', 'preferReuse', 'preferTruncatedLines',
    'progressBarStyle', 'rcFilename', 'supportedArchitectures', 'taskPoolConcurrency', 'telemetryInterval',
    'telemetryUserId', 'tsEnableAutoTypes', 'unsafeHttpWhitelist', 'virtualFolder', 'winLinkType',
    'workerPoolMode'}


def strip_yarnrc(path):
    """Removes the top-level keys of a .yarnrc.yml that load or run code (with their indented values), and with the
    plugins every key berry itself doesn't know: the keys removed"""
    text = open(path, errors='replace').read()
    no_plugins = re.search(r'^plugins\s*:', text, re.M) is not None
    out, removed, skipping = [], [], False
    for line in text.split('\n'):
        if skipping and (line.startswith((' ', '\t', '-')) or not line.strip()):
            continue
        skipping = False
        m = YARNRC_CODE_KEYS.match(line)
        k = re.match(r'^"?([\w-]+)"?\s*:', line)
        if m or (no_plugins and k and k.group(1) not in BERRY_CORE_KEYS):
            removed.append(m.group(1) if m else k.group(1))
            skipping = True
            continue
        out.append(line)
    if removed:
        with open(path, 'w') as f:
            f.write('\n'.join(out))
    return removed


def prepare(repo, d, strip=True):
    """Makes the tree safe to install or says why it isn't: (changes, refusal or None)"""
    changes = []
    for dirpath in sorted({repo, d}):
        y = os.path.join(dirpath, '.yarnrc.yml')
        if os.path.exists(y) and YARNRC_CODE_KEYS.search(open(y, errors='replace').read()):
            if not strip:
                return changes, f'{os.path.relpath(y, repo)} loads code'
            changes.append(f'stripped {", ".join(strip_yarnrc(y))} from {os.path.relpath(y, repo)}')
        w = os.path.join(dirpath, 'pnpm-workspace.yaml')
        if os.path.exists(w) and PNPM_WORKSPACE_REFUSE.search(open(w, errors='replace').read()):
            return changes, f'{os.path.relpath(w, repo)} has configDependencies/pnpmfile/hooks'
    return changes, None


def install(repo, pkg, cache, timeout=1200, strip=True, dry_run=False, corepack_home=None):
    """Installs the dependencies of the package at pkg (in repo) in the sandbox; the record of what ran. cache: the
    package managers' caches (one per batch: concurrent batches never share a store a cleanup deletes); corepack_home:
    the managers corepack fetched (default cache/corepack; shared, read-only to an install once fetched)"""
    repo, pkg, cache = os.path.realpath(repo), os.path.realpath(pkg), os.path.realpath(cache)
    corepack_home = os.path.realpath(corepack_home or os.path.join(cache, 'corepack'))
    d = install_dir(repo, pkg)
    family, version, cmds, notes = plan(d)
    rec = {'pm': family and f'{family}@{version}', 'dir': os.path.relpath(d, repo), 'notes': notes, 'changes': [],
           'refused': None, 'ok': False, 'tail': '', 'commands': []}
    if not cmds:
        rec['ok'] = True
        return rec
    if not dry_run:
        rec['changes'], rec['refused'] = prepare(repo, d, strip)
        if rec['refused']:
            return rec
    os.makedirs(cache, exist_ok=True)
    os.makedirs(corepack_home, exist_ok=True)
    env = pm_env(cache, corepack_home)
    for cmd in cmds:
        cmd = [a.replace('{CACHE}', cache) for a in cmd]
        rec['commands'].append(' '.join(cmd))
        # (A package manager corepack fetched before is read-only to every later install: one install can't change
        # the manager the next one runs)
        name = family if family == 'pnpm' else 'yarn'
        fetched = os.path.join(corepack_home, 'v1', name, version) if family in ('pnpm', 'yarn1', 'berry') else None
        ro = [NODE_DIR] + ([fetched] if fetched and os.path.isdir(fetched) else [])
        argv = sandbox_argv(rw=[repo, cache, corepack_home], ro=[], network=True, cwd=d)[:-1] + \
            sum((['--ro-bind', x, x] for x in ro), []) + ['--'] + cmd
        if dry_run:
            rec['argv'], rec['env'] = argv, clean_env(env)
            return rec
        try:
            p = subprocess.run(argv, env=clean_env(env), capture_output=True, text=True, timeout=timeout,
                               stdin=subprocess.DEVNULL)
            if p.returncode == 0:
                rec['ok'], rec['tail'] = True, ''
                return rec
            rec['tail'] = ' '.join((p.stdout + p.stderr)[-600:].split())
        except subprocess.TimeoutExpired:
            rec['tail'] = 'timeout'
    return rec


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('install')
    a.add_argument('dir')
    a.add_argument('--root', help='the repository root (default: dir)')
    a.add_argument('--cache', required=True)
    a.add_argument('--dry-run', action='store_true')
    a.add_argument('--refuse', action='store_true', help='refuse a .yarnrc.yml that loads code instead of stripping it')
    r = sub.add_parser('run')
    r.add_argument('--rw', action='append', default=[])
    r.add_argument('--ro', action='append', default=[])
    r.add_argument('--net', action='store_true')
    r.add_argument('--cwd', default='.')
    r.add_argument('command', nargs=argparse.REMAINDER)
    args = ap.parse_args()
    if args.cmd == 'install':
        rec = install(args.root or args.dir, args.dir, args.cache, strip=not args.refuse, dry_run=args.dry_run)
        if args.dry_run and 'argv' in rec:
            print('argv:', ' '.join(rec['argv']))
            print('env:', ' '.join(f'{k}={v}' for k, v in rec['env'].items()))
        print(json.dumps({k: v for k, v in rec.items() if k not in ('argv', 'env')}, indent=1))
        sys.exit(0 if rec['ok'] or args.dry_run else 1)
    cmd = args.command[1:] if args.command[:1] == ['--'] else args.command
    p = run(cmd, args.cwd, rw=args.rw, ro=args.ro, network=args.net, capture_output=False)
    sys.exit(p.returncode)


if __name__ == '__main__':
    main()
