#!/usr/bin/env bash
# Fetches the source sweep's repositories (plan 52, scripts/sourcesweep.py) into thirdparty/sources/ (not committed, like
# thirdparty/corpora): each repo of test/sourcesweep/repos.txt at its pinned commit, its dependencies installed with
# its own package manager and lockfile (scripts off), then node_modules pruned to what a type checker reads and
# hardlinked to identical files of the other repos (sources/.dedupe.db, content hashes). Idempotent:
# a repo with a .tttt-installed marker is left alone. Needs the network for whatever is missing.
#
#   thirdparty/ensure-sources.sh              # fetch and install whatever is missing (4 at a time)
#   thirdparty/ensure-sources.sh NAME...      # only these repos
#   thirdparty/ensure-sources.sh --dedupe     # also dedupe the repos installed already (after deleting the index)
#
# The installs run under a pinned node 24 (thirdparty/node/, fetched here; the system node is too old for recent pnpm
# and many `engines` fields), with corepack picking each repo's `packageManager`. Only the installs: tsc and tttt's
# oracles run under the system node. A failed install is reported and leaves the repo without a marker (drop it from
# the manifest with a comment, don't patch it).
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )"
exec python3 -I - "$@" <<'EOF'
import concurrent.futures, hashlib, json, os, shutil, sqlite3, subprocess, sys, threading, time, urllib.request

NODE_VERSION = 'v24.21.0'
NODE_SHA256 = 'fd8e59d5a511510f6a298afb548f18c7d2b1be404d8b4a27d94fbe49f56cb2d6'
HERE = os.getcwd()
SOURCES = os.path.join(HERE, 'sources')
CACHE = os.path.join(SOURCES, '.cache')
NODE_BIN = os.path.join(HERE, 'node', f'node-{NODE_VERSION}-linux-x64', 'bin')
MANIFEST = os.path.join(HERE, '..', 'test', 'sourcesweep', 'repos.txt')
# What tsc and tttt read from node_modules; the rest (maps, binaries, docs, native addons) is ~half the bytes
KEEP = ('.ts', '.tsx', '.mts', '.cts', '.js', '.jsx', '.mjs', '.cjs', '.json')


def ensure_node():
    if os.path.exists(os.path.join(NODE_BIN, 'node')):
        return
    os.makedirs(os.path.dirname(os.path.dirname(NODE_BIN)), exist_ok=True)
    name = f'node-{NODE_VERSION}-linux-x64.tar.xz'
    tarball = os.path.join(os.path.dirname(os.path.dirname(NODE_BIN)), name)
    print(f'fetching node {NODE_VERSION}', flush=True)
    urllib.request.urlretrieve(f'https://nodejs.org/dist/{NODE_VERSION}/{name}', tarball)
    if hashlib.sha256(open(tarball, 'rb').read()).hexdigest() != NODE_SHA256:
        sys.exit(f'{name}: sha256 mismatch')
    subprocess.run(['tar', 'xf', tarball, '-C', os.path.dirname(tarball)], check=True)
    os.remove(tarball)


def fetch(name, url, commit):
    d = os.path.join(SOURCES, name)
    if os.path.exists(d):
        head = subprocess.run(['git', '-C', d, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
        if head == commit:
            return d
        print(f'{name}: at {head[:12]}, want {commit[:12]}: refetching', flush=True)
        shutil.rmtree(d)
    tmp = d + '-tmp'
    shutil.rmtree(tmp, ignore_errors=True)
    subprocess.run(['git', 'init', '-q', tmp], check=True)
    subprocess.run(['git', '-C', tmp, 'fetch', '-q', '--depth', '1', url, commit], check=True)
    subprocess.run(['git', '-C', tmp, '-c', 'advice.detachedHead=false', 'checkout', '-q', 'FETCH_HEAD'], check=True)
    os.rename(tmp, d)
    return d


def install_command(d):
    pj = json.load(open(os.path.join(d, 'package.json')))
    pm = pj.get('packageManager', '') or ''
    has = lambda f: os.path.exists(os.path.join(d, f))
    if pm.startswith('pnpm') or (not pm and has('pnpm-lock.yaml')):
        if not pm:
            # Without a packageManager field: an exact `engines.pnpm`, else the major that writes the lockfile's format
            engine = (pj.get('engines') or {}).get('pnpm', '')
            lock = open(os.path.join(d, 'pnpm-lock.yaml')).readline() if has('pnpm-lock.yaml') else ''
            pm = (f'pnpm@{engine}' if engine[:1].isdigit() else 'pnpm@8' if "'6." in lock else
                  'pnpm@7' if lock.startswith('lockfileVersion: 5') else 'pnpm@10')
        return ['corepack', pm.split('+')[0], 'install', '--ignore-scripts',
                f'--store-dir={CACHE}/pnpm-store', '--frozen-lockfile', '--config.engine-strict=false']
    if pm.startswith('yarn') or (not pm and has('yarn.lock')):
        # A berry lockfile (its `__metadata` entry) without a packageManager field: yarn 1's flags would fail
        if not pm and '__metadata:' in open(os.path.join(d, 'yarn.lock')).read(4096):
            pm = 'yarn@4'
        if not pm or pm.startswith('yarn@1'):
            return ['corepack', pm.split('+')[0] if pm else 'yarn@1', 'install', '--ignore-scripts', '--ignore-engines',
                    '--frozen-lockfile', f'--cache-folder={CACHE}/yarn1']
        return ['corepack', 'yarn', 'install', '--mode=skip-build']
    if has('package-lock.json'):
        return [os.path.join(NODE_BIN, 'npm'), 'ci', '--ignore-scripts', '--legacy-peer-deps', '--no-audit', '--no-fund',
                f'--cache={CACHE}/npm']
    return None


def install(name, d):
    cmd = install_command(d)
    if cmd is None:
        return name, 'no lockfile', 0, ''
    env = {**os.environ, 'PATH': NODE_BIN + ':' + os.environ['PATH'], 'COREPACK_ENABLE_DOWNLOAD_PROMPT': '0',
           'COREPACK_ENABLE_STRICT': '0', 'COREPACK_HOME': f'{CACHE}/corepack', 'CI': '1',
           'YARN_NODE_LINKER': 'node-modules', 'YARN_ENABLE_SCRIPTS': 'false', 'YARN_ENABLE_IMMUTABLE_INSTALLS': 'true',
           'YARN_ENABLE_GLOBAL_CACHE': 'true', 'YARN_GLOBAL_FOLDER': f'{CACHE}/berry', 'YARN_ENABLE_TELEMETRY': '0',
           'HUSKY': '0', 'PUPPETEER_SKIP_DOWNLOAD': '1', 'ELECTRON_SKIP_BINARY_DOWNLOAD': '1',
           'PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD': '1', 'CYPRESS_INSTALL_BINARY': '0'}
    start = time.time()
    try:
        p = subprocess.run(cmd, cwd=d, env=env, capture_output=True, text=True, timeout=1800)
        rc, tail = p.returncode, (p.stdout + p.stderr)[-800:]
    except subprocess.TimeoutExpired:
        rc, tail = 'timeout', ''
    if rc == 0:
        prune(d)
        with DEDUPE_LOCK:
            dedupe(d)
        with open(os.path.join(d, '.tttt-installed'), 'w') as f:
            f.write(' '.join(cmd[:3]) + '\n')
    return name, ' '.join(cmd[:3]), rc, f'{time.time() - start:.0f}s' if rc == 0 else tail


def prune(d):
    for root, dirs, files in os.walk(d):
        dirs[:] = [x for x in dirs if x != '.git']
        if '/node_modules' in root:
            for f in files:
                if not f.endswith(KEEP):
                    p = os.path.join(root, f)
                    if not os.path.islink(p):
                        os.remove(p)


DEDUPE_LOCK = threading.Lock()


def dedupe(d):
    """Hardlinks the repo's node_modules files to identical ones of the repos installed before (an index of content
    hashes in sources/.dedupe.db): most dependencies are the same versions everywhere, ~half the disk"""
    db = sqlite3.connect(os.path.join(SOURCES, '.dedupe.db'))
    db.execute('CREATE TABLE IF NOT EXISTS f (size INTEGER, hash BLOB, path TEXT, PRIMARY KEY (size, hash))')
    for root, dirs, files in os.walk(d):
        dirs[:] = [x for x in dirs if x != '.git']
        if '/node_modules' not in root:
            continue
        for name in files:
            p = os.path.join(root, name)
            st = os.lstat(p)
            if not os.path.isfile(p) or os.path.islink(p) or st.st_size == 0:
                continue
            with open(p, 'rb') as f:
                h = hashlib.blake2b(f.read(), digest_size=16).digest()
            row = db.execute('SELECT path FROM f WHERE size = ? AND hash = ?', (st.st_size, h)).fetchone()
            if row:
                try:
                    other = os.lstat(row[0])
                except FileNotFoundError:
                    other = None
                if other and other.st_size == st.st_size:
                    if (other.st_dev, other.st_ino) != (st.st_dev, st.st_ino):
                        os.link(row[0], p + '.tttt-link')
                        os.replace(p + '.tttt-link', p)
                    continue
            db.execute('INSERT OR REPLACE INTO f VALUES (?, ?, ?)', (st.st_size, h, p))
    db.commit()
    db.close()


def main():
    args = sys.argv[1:]
    reindex = '--dedupe' in args
    only = {a for a in args if not a.startswith('--')}
    repos = []
    for line in open(MANIFEST):
        if line.strip() and not line.startswith('#'):
            name, url, commit = line.split()[:3]
            if not only or name in only:
                repos.append((name, url, commit))
    os.makedirs(SOURCES, exist_ok=True)
    if reindex:
        for r in repos:
            if os.path.exists(os.path.join(SOURCES, r[0], '.tttt-installed')):
                dedupe(os.path.join(SOURCES, r[0]))
    todo = [r for r in repos if not os.path.exists(os.path.join(SOURCES, r[0], '.tttt-installed'))]
    if todo:
        ensure_node()
    failed = []

    def one(r):
        name, url, commit = r
        try:
            d = fetch(name, url, commit)
        except subprocess.CalledProcessError as e:
            return name, 'fetch', 1, str(e)
        return install(name, d)
    # In batches: the package managers' caches are only needed while installing (the installed files are copies or
    # hardlinks), and pnpm's store keeps every file it ever fetched, unpruned (25 GB after 50 repos)
    for batch in range(0, len(todo), 20):
        with concurrent.futures.ThreadPoolExecutor(4) as ex:
            for name, cmd, rc, info in ex.map(one, todo[batch:batch + 20]):
                print(f'{name:28} {cmd:28} ' + ('ok ' + info if rc == 0 else f'FAILED ({rc})\n    ' + info.replace('\n', '\n    ')), flush=True)
                if rc != 0:
                    failed.append(name)
        shutil.rmtree(CACHE, ignore_errors=True)
    if failed:
        sys.exit(f'{len(failed)} failed: {" ".join(failed)}')


main()
EOF
