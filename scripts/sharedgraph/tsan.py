import re,sys
s=open(sys.argv[1],errors='replace').read()
reps=s.split('WARNING: ThreadSanitizer')[1:]
groups={}
for r in reps:
    m=re.search(r'#0 (\S+) (\S+)', r)
    if not m: continue
    pv=r.find('Previous')
    m2=re.search(r'#0 (\S+) (\S+)', r[pv:]) if pv>=0 else None
    k=(m.group(1)+'@'+m.group(2).split('/')[-1], (m2.group(1)+'@'+m2.group(2).split('/')[-1]) if m2 else '?')
    groups.setdefault(k,[0,r])[0]+=1
for k,(n,r) in sorted(groups.items(), key=lambda x:-x[1][0]):
    print(n, k[0], '<-prev-', k[1])
