import sys,subprocess,collections
import os; BIN = os.environ.get('MEMTRACK_BIN', 'release/tttt')
fn=sys.argv[1]; mode=sys.argv[2] if len(sys.argv)>2 else 'f0'
rows=[l.split() for l in open(fn)]
addrs=sorted({a for r in rows for a in r[2:5] if a!='0'})
out=subprocess.run(['addr2line','-f','-e',BIN]+['0x'+a for a in addrs],capture_output=True,text=True).stdout.split('\n')
sym={a:out[2*i] for i,a in enumerate(addrs)}
S=lambda a: sym.get(a,'?') if a!='0' else '-'
tot=collections.Counter(); chunk=collections.Counter(); cnt=collections.Counter()
T=[0,0,0]
for r in rows:
    s=int(r[0]); u=int(r[1]); c=max(u+8,32)
    if mode=='f0': k=S(r[2])
    elif mode=='f01': k=S(r[2])+' < '+S(r[3])
    elif mode=='size': k=r[0]
    elif mode=='sizef': k=r[0]+' '+S(r[2])
    tot[k]+=s; chunk[k]+=c; cnt[k]+=1; T[0]+=s;T[1]+=c;T[2]+=1
print('live allocs %d, requested %.1f MB, chunks %.1f MB'%(T[2],T[0]/1e6,T[1]/1e6))
for k,v in chunk.most_common(int(sys.argv[3]) if len(sys.argv)>3 else 40):
    print('%7.2f MB chunk %7.2f MB req %8d n  %s'%(v/1e6,tot[k]/1e6,cnt[k],k))
