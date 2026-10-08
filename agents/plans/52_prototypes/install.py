#!/usr/bin/env python3
# Prototype: install a repo's dependencies with its own package manager, scripts off, shared caches
import json, os, subprocess, sys, time, concurrent.futures
SS = '/home/ai/ss'
def install(d):
    pj = json.load(open(os.path.join(d, 'package.json')))
    pm = pj.get('packageManager', '')
    has = lambda f: os.path.exists(os.path.join(d, f))
    env = {**os.environ, 'PATH': SS + '/node/node-v24.21.0-linux-x64/bin:' + os.environ['PATH'], 'COREPACK_ENABLE_DOWNLOAD_PROMPT': '0', 'COREPACK_ENABLE_STRICT': '0', 'CI': '1',
           'YARN_NODE_LINKER': 'node-modules', 'YARN_ENABLE_SCRIPTS': 'false', 'YARN_ENABLE_IMMUTABLE_INSTALLS': 'false',
           'YARN_ENABLE_GLOBAL_CACHE': 'true', 'HUSKY': '0', 'PUPPETEER_SKIP_DOWNLOAD': '1', 'ELECTRON_SKIP_BINARY_DOWNLOAD': '1'}
    if pm.startswith('pnpm') or (not pm and has('pnpm-lock.yaml')):
        cmd = ['corepack', 'pnpm', 'install', '--ignore-scripts', f'--store-dir={SS}/pnpm-store', '--frozen-lockfile']
        if not pm: cmd[1] = 'pnpm@10'
    elif pm.startswith('yarn') or has('yarn.lock'):
        cmd = ['corepack', 'yarn', 'install', '--ignore-scripts', '--ignore-engines'] if not pm or pm.startswith('yarn@1') else ['corepack', 'yarn', 'install', '--mode=skip-build']
        if not pm: cmd[1] = 'yarn@1'
    elif has('package-lock.json'):
        cmd = [SS + '/node/node-v24.21.0-linux-x64/bin/npm', 'ci', '--ignore-scripts', '--legacy-peer-deps', '--no-audit', '--no-fund']
    else:
        cmd = ['npm', 'install', '--ignore-scripts', '--legacy-peer-deps', '--no-audit', '--no-fund']
    t = time.time()
    p = subprocess.run(cmd, cwd=d, env=env, capture_output=True, text=True, timeout=1800)
    return d, ' '.join(cmd[:3]), p.returncode, time.time() - t, (p.stdout + p.stderr)[-500:]
dirs = sys.argv[1:]
with concurrent.futures.ThreadPoolExecutor(4) as ex:
    for d, cmd, rc, t, tail in ex.map(install, dirs):
        print(f'{os.path.basename(d):14} {cmd:22} rc={rc} {t:5.0f}s', flush=True)
        if rc: print('   ', tail.replace('\n', '\n    '))
