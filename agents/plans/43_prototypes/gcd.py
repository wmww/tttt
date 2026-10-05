import sys,subprocess,collections,math
rows=[l.split() for l in open(sys.argv[1])]
addrs=sorted({r[0] for r in rows if r[0]!='0'})
out=subprocess.run(['addr2line','-f','-e','release/tttt']+['0x'+a for a in addrs],capture_output=True,text=True).stdout.split('\n')
sym={a:out[2*i] for i,a in enumerate(addrs)}
# per (function, gcd class) : group sizes by gcd with most frequent size
by=collections.defaultdict(collections.Counter)
for a,s,sp,z in rows: by[sym.get(a,'?')][int(s)]+=1
tot={k:sum(s*n for s,n in c.items()) for k,c in by.items()}
for k in sorted(tot,key=lambda k:-tot[k])[:30]:
    c=by[k]; g=0
    for s in c: g=math.gcd(g,s)
    top=', '.join('%d×%d'%(s,n) for s,n in c.most_common(6))
    print('%6.2f MB %-40s gcd %d  %s'%(tot[k]/1e6,k[:40],g,top))
