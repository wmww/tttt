import json, sys, collections
sys.path.insert(0, '/home/ai/tttt')
import sweep
tsc = {r['project']: r for r in map(json.loads, open('/home/ai/ss/tsc.jsonl'))}
res = json.load(open(sys.argv[1]))
key = sys.argv[2] if len(sys.argv) > 2 else None
verd = collections.defaultdict(list); causes = collections.defaultdict(lambda: [set(), set(), 0, None])
for r in res:
    p = r['package']
    if r['status'] != 'ok': verd[r['status']].append(p); print('CRASH', p, r['status'], r['tail'][-300:]); continue
    sp, mi = sweep.compare(p, tsc[p], r)
    verd['both' if sp and mi else 'spurious' if sp else 'missing' if mi else 'match'].append(p)
    for d, s in [(d, 1) for d in sp] + [(d, 0) for d in mi]:
        dep = sweep.dependency(d[0]) if 'node_modules/' in d[0] else ''
        dep = dep[dep.index('node_modules'):] if dep else ''
        k = (f'+TS{d[2]} ' + (f'in {dep} ' if dep else '') + sweep.shape(d[3])) if s else f'-TS{d[2]}' + (f' in {dep}' if dep else '')
        c = causes[k]; c[0].add(p); c[1].add(p.split('/')[0]); c[2] += 1
        if c[3] is None: c[3] = f'{p}  {d[0]}:{d[1]}' + (f' {d[3][:100]}' if s else '')
        if key and key in k: print(p, d[0], d[1], d[2], d[3][:200] if s else '(missing)')
if key: sys.exit()
print({k: len(v) for k, v in verd.items()})
print(f'{"repos":>5} {"projs":>5} {"diags":>6}')
for k, c in sorted(causes.items(), key=lambda kc: (-len(kc[1][1]), -len(kc[1][0]), -kc[1][2]))[:60]:
    print(f'{len(c[1]):5} {len(c[0]):5} {c[2]:6}  {k[:110]}\n{"":19}{c[3][:150]}')
