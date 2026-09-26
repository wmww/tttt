#!/usr/bin/env python3
"""JS/JSDoc accounting (plan 26): classifies the failing JS tests (a .js/.jsx/.cjs/.mjs unit, @allowJs or @checkJs)
by the features they need, from their sources and tsc's baselines. Run ./failstats.py first (reads its dump).

  ./jsstats.py                  counts per category, category combinations, cumulative "could pass" upper bounds
  ./jsstats.py 1cjs,2expando    the failing tests whose category set is exactly that

Categories: 1cjs (module.exports/exports/require), 2expando (f.x=, this.x=, prototype, @class), 3jsdoc (@type/@param/
@returns/@typedef/@template/@property/casts), 3bjsdoc_adv (the other tags), 4lenient (spurious TS2554/TS7006/TS7009/
TS2683/...), 5syntax (TS8xxx/TS1xxx), 6emit (TS5055 & co., out of scope), 7ts (only a .ts unit differs), 0other.
Regex-based: in a 40-test sample the primary category was right ~85% of the time; the cumulative numbers are upper
bounds (incidental bugs cut them by a third or so)."""
import csv, os, re, sys, collections
os.chdir(os.path.dirname(os.path.abspath(__file__)))
REF='thirdparty/tsc/tests/baselines/reference'
DUMP='build/failstats/dump'
JSDETECT = re.compile(r'@(allowjs|checkjs)\s*:\s*true|@filename:\s*\S+\.(js|jsx|cjs|mjs)\b', re.I)
src={}
for root in ['thirdparty/tsc/tests/cases/compiler','thirdparty/tsc/tests/cases/conformance']:
    for d,_,fs in os.walk(root):
        for f in fs:
            b,ext=os.path.splitext(f)
            if ext in('.ts','.tsx') and b not in src:
                src[b]=os.path.join(d,f)
names={b for b,p in src.items() if JSDETECT.search(open(p,encoding='utf-8',errors='replace').read())}
JSEXT=('.js','.jsx','.cjs','.mjs')
def units(path):
    text=open(path,encoding='utf-8',errors='replace').read()
    opts={}; out=[]; cur=None; buf=[]
    for line in text.split('\n'):
        m=re.match(r'\s*//\s*@(\w+)\s*:\s*(.*?)\s*$',line)
        if m:
            k=m.group(1).lower()
            if k=='filename':
                if cur is not None or buf: out.append((cur or os.path.basename(path),'\n'.join(buf)))
                cur=m.group(2); buf=[]; continue
            opts[k]=m.group(2); continue
        buf.append(line)
    out.append((cur or os.path.basename(path),'\n'.join(buf)))
    return opts,out
def read(p):
    try: return open(p,encoding='utf-8',errors='replace').read()
    except FileNotFoundError: return None
ERRLINE=re.compile(r'^(?:(\S.*?)\(\d+,\d+\): )?error (TS\d+): ')
def errs(t):
    if t is None: return []
    res=[]
    for l in t.split('\n'):
        if l.startswith('==== '): break
        m=ERRLINE.match(l)
        if m: res.append((m.group(1) or '',m.group(2),l))
    return res
def sections(t):
    d=collections.defaultdict(list); cur=''
    if t is None: return d
    for l in t.split('\n'):
        m=re.match(r'^=== (.*) ===$',l)
        if m: cur=m.group(1); continue
        d[cur].append(l)
    return d
def isjs(f): return f.lower().endswith(JSEXT)
def secdiff(a,b):
    """files whose section differs"""
    sa,sb=sections(a),sections(b)
    bad=set()
    for k in set(sa)|set(sb):
        if collections.Counter(sa.get(k,[]))!=collections.Counter(sb.get(k,[])): bad.add(k)
    return bad
FEAT={
 'cjs': [r'\bmodule\.exports\b', r'(?<![.\w])exports\.\w+\s*=', r'(?<![.\w])exports\s*=', r'\brequire\s*\(', r'\bObject\.defineProperty\s*\(\s*(exports|module)'],
 'this_assign': [r'\bthis\.\w+\s*=[^=]', r'\bthis\[[^\]]+\]\s*=[^=]'],
 'prototype': [r'\.prototype\.\w+\s*=[^=]', r'\.prototype\s*=[^=]', r'\.prototype\[[^\]]+\]\s*=[^=]'],
 'defineProperty': [r'Object\.defineProperty\s*\('],
}
TAGS=['type','param','returns','return','typedef','template','callback','enum','import','overload','satisfies','this','extends','augments','implements','class','constructor','property','prop','readonly','private','protected','public','override','deprecated','see','link','abstract','static','arg','argument','yields','throws','constant','const','memberof','namespace','function','lends','exports','module','inheritdoc','default','async']
def jsdoc_tags(text):
    tags=collections.Counter()
    for c in re.findall(r'/\*\*.*?\*/',text,re.S):
        for t in re.findall(r'@(\w+)',c): tags[t.lower()]+=1
    casts=len(re.findall(r'/\*\*\s*@type\s*\{[^}]*\}\s*\*/\s*\(',text))
    return tags,casts
def expando(text):
    ids=set(re.findall(r'\bfunction\s*\*?\s*(\w+)',text))|set(re.findall(r'\b(?:const|let|var)\s+(\w+)\s*=',text))|set(re.findall(r'\bclass\s+(\w+)',text))|{'window','globalThis'}
    hits=[]
    for m in re.finditer(r'^\s*(\w+)((?:\.\w+|\[[^\]=]+\])+)\s*=[^=>]',text,re.M):
        if m.group(1) in ids and 'prototype' not in m.group(2) and m.group(1) not in('module','exports','this'): hits.append(m.group(0).strip())
    return hits
rows={r['name']:r for r in csv.DictReader(open('build/failstats/tests.tsv'),delimiter='\t')}
out=[]
for name,r in rows.items():
    b=name.split('(')[0]
    if b not in names: continue
    passed = r['errors']=='1' and r['symbols']=='1' and r['types']=='1'
    p=src.get(b)
    if not p: continue
    opts,us=units(p)
    js=[(f,t) for f,t in us if isjs(f)]
    jstext='\n'.join(t for f,t in js)
    feats=set()
    for k,pats in FEAT.items():
        if any(re.search(pp,jstext) for pp in pats): feats.add(k)
    if expando(jstext): feats.add('expando')
    tags,casts=jsdoc_tags(jstext)
    if casts: tags['cast']+=casts
    if re.search(r'@(class|constructor)\b',jstext): feats.add('ctor_tag')
    # diffs
    ref_e=errs(read(f'{REF}/{name}.errors.txt')); got_e=errs(read(f'{DUMP}/{name}.errors.txt'))
    ce=collections.Counter(x[2] for x in ref_e); cg=collections.Counter(x[2] for x in got_e)
    missing=[l for l in (ce-cg).elements()]; spurious=[l for l in (cg-ce).elements()]
    def fileof(l):
        m=ERRLINE.match(l); return m.group(1) or '' if m else ''
    errfiles=set(fileof(l) for l in missing+spurious)
    symfiles=secdiff(read(f'{REF}/{name}.symbols'),read(f'{DUMP}/{name}.symbols')) if r['symbols']=='0' else set()
    typfiles=secdiff(read(f'{REF}/{name}.types'),read(f'{DUMP}/{name}.types')) if r['types']=='0' else set()
    allf=errfiles|symfiles|typfiles
    tsbad=[f for f in allf if f and not isjs(f)]
    jsbad=[f for f in allf if f and isjs(f)]
    codes_m=[ERRLINE.match(l).group(2) for l in missing]; codes_s=[ERRLINE.match(l).group(2) for l in spurious]
    out.append(dict(name=name,passed=passed,e=r['errors'],s=r['symbols'],t=r['types'],feats=feats,tags=tags,
        miss=codes_m,spur=codes_s,tsbad=tsbad,jsbad=jsbad,globalerr='' in errfiles,opts=opts,src=p,njs=len(js),nunits=len(us)))
print(len(out),'JS tests;',sum(o['passed'] for o in out),'pass;',sum(not o['passed'] for o in out),'failing')

F=[o for o in out if not o['passed']]
BASIC={'type','param','returns','return','typedef','template','cast','property','prop','arg','argument'}
ADV={'import','callback','enum','overload','satisfies','this','extends','augments','implements','override','private','public','protected','readonly','constructor','class','namespace','function','yields','lends'}
LENIENT={'TS2554','TS7009','TS2683','TS7006','TS7019','TS7034','TS7005','TS7031','TS2348'}
EMIT={'TS5055','TS5056','TS6054','TS6504','TS5053','TS5069'}
def cats(o):
    c=set()
    f=o['feats']
    if 'cjs' in f or {'TS2580','TS2306'}&set(o['spur']): c.add('1cjs')
    if f&{'this_assign','prototype','expando','ctor_tag','defineProperty'}: c.add('2expando')
    t=set(o['tags'])
    if t&BASIC: c.add('3jsdoc')
    if t&ADV: c.add('3bjsdoc_adv')
    if set(o['spur'])&LENIENT: c.add('4lenient')
    if any(x.startswith('TS8') for x in o['miss']+o['spur']) or any(re.match(r'TS1\d\d\d$',x) for x in o['miss']+o['spur']): c.add('5syntax')
    if set(o['miss']+o['spur'])&EMIT: c.add('6emit')
    if not o['jsbad'] and o['tsbad'] and not o['globalerr']: c.add('7ts')
    if not c: c.add('0other')
    return c
for o in F: o['cats']=cats(o)
if True:
    inv=collections.Counter(); only=collections.Counter()
    for o in F:
        for c in o['cats']: inv[c]+=1
        if len(o['cats'])==1: only[next(iter(o['cats']))]+=1
    print('involved',sorted(inv.items())); print('only',sorted(only.items()))
    combos=collections.Counter(tuple(sorted(o['cats'])) for o in F)
    for k,v in combos.most_common(30): print(v,k)
    def passes(S): return sum(1 for o in F if o['cats']<=S)
    for S in [{'1cjs'},{'2expando'},{'1cjs','2expando'},{'1cjs','2expando','3jsdoc'},{'1cjs','2expando','3jsdoc','4lenient'},{'1cjs','2expando','3jsdoc','4lenient','5syntax'},{'1cjs','2expando','3jsdoc','3bjsdoc_adv','4lenient','5syntax'},{'1cjs','2expando','3jsdoc','3bjsdoc_adv','4lenient','5syntax','6emit'}]:
        print(sorted(S),passes(S))
    if len(sys.argv)>1:
        for o in F:
            if o['cats']==set(sys.argv[1].split(',')): print(o['name'],o['e'],o['s'],o['t'],o['miss'][:5],o['spur'][:5],dict(o['tags']))
