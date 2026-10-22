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
            if e.code in (403, 429): time.sleep(65); continue
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
topics = sys.argv[2].split(',')
per = int(sys.argv[3])
picked = []
for topic in topics:
    lang = 'JavaScript' if mode == 'checkjs' else 'TypeScript'
    q = f'topic:{topic}+language:{lang}+stars:300..3000+pushed:>2025-06-01+size:<200000'
    if mode in ('app', 'appbun'): q = f'topic:{topic}+language:TypeScript+stars:40..400+pushed:>2025-10-22+size:<200000'
    res = get(f'https://api.github.com/search/repositories?q={q}&sort=stars&order=desc&per_page=100')
    time.sleep(7)
    n = 0
    for r in (res or {}).get('items', []):
        full = r['full_name']
        if full.lower() in existing or r['fork'] or r['archived']: continue
        if raw(full, 'package.json') is None: continue
        other = any(raw(full, f) is not None for f in ('pnpm-lock.yaml', 'yarn.lock', 'package-lock.json'))
        if mode == 'appbun':
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
