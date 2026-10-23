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

Prints `topic owner/name stars default-branch`; pin each with `git ls-remote URL HEAD` and append it to repos.txt
under a comment saying the rule (refresh 2, 2026-10-18, is the example). Files are read from raw.githubusercontent.com.
"""
import json, os, posixpath, re, sys, time, urllib.request, urllib.error
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
        if mode == 'ts7':
            if not re.search(r'"@typescript/native-preview"\s*:|"typescript"\s*:\s*"[\^~>=]*7\.', pj): continue
            if raw(full, 'deno.json') is not None or raw(full, 'deno.jsonc') is not None: continue
        other = any(raw(full, f) is not None for f in ('pnpm-lock.yaml', 'yarn.lock', 'package-lock.json'))
        if mode == 'ts7':
            if not other and raw(full, 'bun.lock') is None and raw(full, 'bun.lockb') is None: continue
        elif mode == 'appbun':
            if other or (raw(full, 'bun.lockb') is None and raw(full, 'bun.lock') is None): continue
        elif not other: continue
        if mode in ('app', 'appbun') and (raw(full, 'deno.json') is not None or raw(full, 'deno.jsonc') is not None): continue
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
