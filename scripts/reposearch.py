#!/usr/bin/env python3
"""Candidate repositories for a source sweep refresh (vision.md "Refresh the breadth oracles"): GitHub's repository
search per topic (unauthenticated: ~10 searches a minute, it sleeps between them), stars 300..3000, pushed after
2025-06-01, size < 200 MB, sorted by stars; the first PER of each topic not in test/sourcesweep/repos.txt with a
root package.json, a root lockfile and a root tsconfig.json (local `extends` followed) that matches the mode:

  scripts/reposearch.py dts TOPIC,TOPIC... PER       # language TypeScript, declaration/composite/emitDeclarationOnly
  scripts/reposearch.py checkjs TOPIC,TOPIC... PER   # language JavaScript, checkJs
  scripts/reposearch.py build TOPIC,TOPIC... PER     # language TypeScript, a solution: `references` (tsc -b)
  scripts/reposearch.py decorators TOPIC,TOPIC... PER  # language TypeScript, experimentalDecorators
  scripts/reposearch.py app TOPIC,TOPIC... PER       # application code (refresh 4): stars 40..400, pushed in the last
      year, no deno.json; a root tsconfig with a less covered setting (strict off or partial, allowJs/checkJs, paths/
      baseUrl, moduleResolution node/node10, jsx react/preserve, experimentalDecorators, isolatedModules/
      verbatimModuleSyntax, references); prints the matched settings too
  scripts/reposearch.py appbun TOPIC,TOPIC... PER    # app's rule, but only repos whose one lockfile is bun's
      (bun.lock/bun.lockb: refresh 5; ensure-sources.sh installs them with a pinned bun)
  scripts/reposearch.py ts7 TOPIC,TOPIC... PER [PAGES]  # TypeScript 7 adopters (refresh 6): stars 20..5000, pushed
      since 2026-07-01, any root lockfile (bun's too), no deno.json, a root tsconfig.json, and a root package.json
      depending on `@typescript/native-preview` or on `typescript` 7 (the people who'd use tttt); PAGES of 100
      search results per topic (default 1)
  scripts/reposearch.py large SEED N               # random large repositories (refresh 8): no topic, language
      TypeScript, stars 100..20000, pushed since 2026-07-01, from several unrelated search pages (size buckets x sort
      by updated / by stars x random page offsets, picked by SEED); kept if GitHub's languages API says TypeScript is
      the main language with >= 5 MB of it (~150k lines), a root package.json, any root lockfile and no deno.json,
      not in repos.txt nor the corpora; N of them, in shuffled order. Verify the lines after cloning. (The languages
      API is the unauthenticated core limit, 60 an hour: it sleeps when it runs out)
  scripts/reposearch.py genericapp TOPIC,TOPIC... PER [PAGES]  # application monorepos on heavy generic libraries
      (refresh 7): stars 50..5000, pushed since 2026-06-01, any root lockfile, no deno.json; the root package.json
      and its workspaces' (package.json `workspaces`, pnpm-workspace.yaml) depend on at least two of the GENERIC
      families; an application (an apps/ directory, or a private root package with workspaces, or a private root
      package depending on an app framework); some tsconfig*.json of the tree says `"strict": true`. Lists the tree
      with a blobless shallow clone into a temporary directory (no API quota); prints the families matched, the
      workspace packages, the TypeScript files (not .d.ts) and the tsconfig*.json files (refresh 7 preferred large ones;
      REPOSEARCH_MIN_TS=N skips trees with fewer TypeScript files)

Prints `topic owner/name stars default-branch`; pin each with `git ls-remote URL HEAD` and append it to repos.txt
under a comment saying the rule (refresh 2, 2026-10-18, is the example). Files are read from raw.githubusercontent.com.
"""
import fnmatch, json, os, posixpath, re, shutil, subprocess, sys, tempfile, time, urllib.request, urllib.error
existing = set()
for l in open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'test/sourcesweep/repos.txt')):
    l = l.strip().lstrip('#')
    if not l: continue
    p = l.split()
    if len(p) >= 2 and p[1].startswith('https://github.com/'):
        existing.add(p[1][len('https://github.com/'):].removesuffix('.git').lower())
def get(url, js=True):
    for i in range(5):
        try:
            r = urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'tttt'}), timeout=30)
            d = r.read().decode('utf-8', 'replace')
            return json.loads(d) if js else d
        except urllib.error.HTTPError as e:
            if e.code == 404: return None
            if e.code in (403, 429):
                reset = e.headers.get('X-RateLimit-Reset')
                time.sleep(max(65, min(3700, int(reset) - time.time() + 5)) if reset and e.headers.get('X-RateLimit-Remaining') == '0' else 65)
                continue
            raise
    return None
def strip(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    return re.sub(r'(?m)^\s*//.*$', '', t)
def raw(full, path): return get(f'https://raw.githubusercontent.com/{full}/HEAD/{path}', js=False)
def config_text(full, path, depth=0):
    t = raw(full, path)
    if t is None or depth > 3: return t or ''
    m = re.search(r'"extends"\s*:\s*"(\.[^"]+)"', strip(t))
    if m:
        p = posixpath.normpath(posixpath.join(posixpath.dirname(path), m.group(1)))
        if not p.endswith('.json'): p += '.json'
        t += '\n' + config_text(full, p, depth + 1)
    return t
GENERIC = {'effect': r'^(effect|@effect/.*)$', 'zod': r'^zod$', 'drizzle': r'^drizzle-orm$', 'kysely': r'^kysely$',
           'hono': r'^hono$', 'trpc': r'^@trpc/.*$', 'tanstack': r'^@tanstack/.*$', 'arktype': r'^arktype$',
           'valibot': r'^valibot$', 'ts-pattern': r'^ts-pattern$', 'mikro-orm': r'^@mikro-orm/.*$',
           'typebox': r'^(typebox|@sinclair/typebox)$'}
FRAMEWORKS = r'^(next|react|react-dom|vite|solid-js|svelte|@sveltejs/kit|nuxt|astro|expo|react-native|electron|express|' \
             r'fastify|hono|@remix-run/.*|@tanstack/react-start|@tanstack/solid-start|vue|@nestjs/core|elysia)$'
def tree_paths(full, branch):
    d = tempfile.mkdtemp(prefix='reposearch-')
    try:
        subprocess.run(['git', 'clone', '-q', '--depth', '1', '--filter=blob:none', '--no-checkout', '-b', branch,
                        f'https://github.com/{full}.git', d], check=True, capture_output=True, timeout=300)
        out = subprocess.run(['git', '-C', d, 'ls-tree', '-r', '--name-only', 'HEAD'], capture_output=True, text=True,
                             check=True).stdout
        return out.split('\n')
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return None
    finally:
        shutil.rmtree(d, ignore_errors=True)
def deps_of(pj):
    try: j = json.loads(pj)
    except ValueError: return {}, set()
    names = set()
    for k in ('dependencies', 'devDependencies', 'peerDependencies'):
        if isinstance(j.get(k), dict): names |= set(j[k])
    return j, names
def genericapp(full, branch, pj):
    root, names = deps_of(pj)
    globs = root.get('workspaces', [])
    if isinstance(globs, dict): globs = globs.get('packages', [])
    pw = raw(full, 'pnpm-workspace.yaml')
    if pw:
        m = re.search(r'(?ms)^packages:\s*\n((?:\s*-.*\n?)+)', pw)
        if m: globs = list(globs) + [x.strip().lstrip('-').strip().strip('\'"') for x in m.group(1).splitlines() if x.strip()]
    paths = tree_paths(full, branch)
    if paths is None: return None
    ts = sum(1 for p in paths if re.search(r'\.(ts|tsx|mts|cts)$', p) and not p.endswith('.d.ts') and '/node_modules/' not in p)
    if ts < int(os.environ.get('REPOSEARCH_MIN_TS', 0)): return None
    pjs = [p for p in paths if p.endswith('/package.json') and '/node_modules/' not in p and
           any(fnmatch.fnmatch(p[:-len('/package.json')], g.rstrip('/')) for g in globs if not g.startswith('!'))]
    private = bool(root.get('private'))
    framework = any(re.match(FRAMEWORKS, n) for n in names)
    for p in pjs[:60]:
        t = raw(full, p)
        if t is None: continue
        _, n = deps_of(t)
        names |= n
        framework = framework or any(re.match(FRAMEWORKS, x) for x in n)
    fams = [f for f, rx in GENERIC.items() if any(re.match(rx, n) for n in names)]
    if len(fams) < 2: return None
    app = any(p.startswith('apps/') for p in paths) or (private and pjs and framework) or (private and framework)
    if not app: return None
    configs = [p for p in paths if re.search(r'(^|/)[^/]*tsconfig[^/]*\.json$', p) and '/node_modules/' not in p]
    if not any(re.search(r'"strict"\s*:\s*true', raw(full, c) or '') for c in sorted(configs, key=len)[:25]):
        return None
    return fams + [f'{len(pjs)}ws', f'{ts}tsfiles', f'{len(configs)}tsconfigs']
mode = sys.argv[1]  # dts | checkjs | build | decorators
if mode == 'large':
    import random
    rnd = random.Random(int(sys.argv[2]))
    want = int(sys.argv[3])
    corpora = open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'thirdparty/ensure-corpora.sh')).read()
    existing |= {m.lower() for m in re.findall(r'github\.com/([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?\b', corpora)}
    pool = {}
    buckets = ('15000..30000', '30000..60000', '60000..120000', '120000..300000', '300000..900000')
    for size in buckets:
        for sort in ('updated', 'stars'):
            for page in rnd.sample(range(1, 11), 2):
                q = f'language:TypeScript+stars:100..20000+pushed:>2026-07-01+size:{size}'
                res = get(f'https://api.github.com/search/repositories?q={q}&sort={sort}&order=desc&per_page=100&page={page}')
                time.sleep(7)
                for r in (res or {}).get('items', []):
                    pool[r['full_name']] = r
                print(f'# search size:{size} sort:{sort} page:{page}: pool {len(pool)}', file=sys.stderr, flush=True)
    cands = sorted(pool)
    rnd.shuffle(cands)
    n = 0
    for full in cands:
        r = pool[full]
        if full.lower() in existing or r['fork'] or r['archived'] or r['language'] != 'TypeScript': continue
        if full.split('/')[1].lower() in ('openclaw', 'opencode', 'osis'): continue
        if raw(full, 'package.json') is None: continue
        if raw(full, 'deno.json') is not None or raw(full, 'deno.jsonc') is not None: continue
        if not any(raw(full, f) is not None for f in ('pnpm-lock.yaml', 'yarn.lock', 'package-lock.json', 'bun.lock', 'bun.lockb')): continue
        langs = get(f'https://api.github.com/repos/{full}/languages') or {}
        ts = langs.get('TypeScript', 0)
        if ts < 5_000_000 or ts < max(langs.values() or [0]): continue
        print(full, r['stargazers_count'], r['size'], ts, r['default_branch'], flush=True)
        n += 1
        if n >= want: break
    sys.exit(0)
topics = sys.argv[2].split(',')
per = int(sys.argv[3])
picked = []
for topic in topics:
    lang = 'JavaScript' if mode == 'checkjs' else 'TypeScript'
    q = f'topic:{topic}+language:{lang}+stars:300..3000+pushed:>2025-06-01+size:<200000'
    if mode in ('app', 'appbun'): q = f'topic:{topic}+language:TypeScript+stars:40..400+pushed:>2025-10-22+size:<200000'
    if mode == 'ts7': q = f'topic:{topic}+language:TypeScript+stars:20..5000+pushed:>2026-07-01+size:<200000'
    if mode == 'genericapp': q = f'topic:{topic}+language:TypeScript+stars:50..5000+pushed:>2026-06-01+size:<200000'
    items = []
    for page in range(1, (int(sys.argv[4]) if len(sys.argv) > 4 else 1) + 1):
        res = get(f'https://api.github.com/search/repositories?q={q}&sort=stars&order=desc&per_page=100&page={page}')
        time.sleep(7)
        items += (res or {}).get('items', [])
        if len((res or {}).get('items', [])) < 100: break
    n = 0
    for r in items:
        full = r['full_name']
        if full.lower() in existing or r['fork'] or r['archived']: continue
        pj = raw(full, 'package.json')
        if pj is None: continue
        if mode in ('ts7', 'genericapp') and (raw(full, 'deno.json') is not None or raw(full, 'deno.jsonc') is not None):
            continue
        if mode == 'ts7':
            if not re.search(r'"@typescript/native-preview"\s*:|"typescript"\s*:\s*"[\^~>=]*7\.', pj): continue
        other = any(raw(full, f) is not None for f in ('pnpm-lock.yaml', 'yarn.lock', 'package-lock.json'))
        if mode in ('ts7', 'genericapp'):
            if not other and raw(full, 'bun.lock') is None and raw(full, 'bun.lockb') is None: continue
        elif mode == 'appbun':
            if other or (raw(full, 'bun.lockb') is None and raw(full, 'bun.lock') is None): continue
        elif not other: continue
        if mode in ('app', 'appbun') and (raw(full, 'deno.json') is not None or raw(full, 'deno.jsonc') is not None): continue
        if mode == 'genericapp':
            feats = genericapp(full, r['default_branch'], pj)
            if not feats: continue
            existing.add(full.lower())
            print(topic, full, r['stargazers_count'], r['default_branch'], *feats, flush=True)
            n += 1
            if n >= per: break
            continue
        c = strip(config_text(full, 'tsconfig.json'))
        if not c: continue
        if mode == 'dts':
            ok = re.search(r'"(declaration|composite|emitDeclarationOnly)"\s*:\s*true', c)
        elif mode == 'build':
            ok = re.search(r'"references"\s*:\s*\[\s*\{', c)
        elif mode == 'decorators':
            ok = re.search(r'"experimentalDecorators"\s*:\s*true', c)
        elif mode == 'ts7':
            ok = True
        elif mode in ('app', 'appbun'):
            feats = [k for k, rx in (
                ('nonstrict', r'"strict"\s*:\s*false'), ('allowJs', r'"(allowJs|checkJs)"\s*:\s*true'),
                ('paths', r'"(paths|baseUrl)"\s*:'), ('node10', r'"moduleResolution"\s*:\s*"node(10)?"'),
                ('jsx', r'"jsx"\s*:\s*"(react|preserve)"'), ('decorators', r'"experimentalDecorators"\s*:\s*true'),
                ('isolated', r'"(isolatedModules|verbatimModuleSyntax)"\s*:\s*true'),
                ('references', r'"references"\s*:\s*\[\s*\{')) if re.search(rx, c, re.I)]
            if not re.search(r'"strict"\s*:', c): feats.insert(0, 'nostrict')
            ok = bool(feats)
        else:
            ok = re.search(r'"checkJs"\s*:\s*true', c)
        if not ok: continue
        existing.add(full.lower())
        print(topic, full, r['stargazers_count'], r['default_branch'], *(feats if mode in ('app', 'appbun') else ()), flush=True)
        n += 1
        if n >= per: break
