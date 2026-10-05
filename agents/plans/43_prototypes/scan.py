import sys,subprocess,collections
rows=[l.split() for l in open(sys.argv[1])]
addrs=sorted({r[0] for r in rows if r[0]!='0'})
out=subprocess.run(['addr2line','-f','-e','release/tttt']+['0x'+a for a in addrs],capture_output=True,text=True).stdout.split('\n')
sym={a:out[2*i] for i,a in enumerate(addrs)}
B=collections.Counter();SP=collections.Counter();Z=collections.Counter()
for a,s,sp,z in rows:
    k=sym.get(a,'?'); s=int(s); B[k]+=s; SP[k]+=int(sp)*24; Z[k]+=int(z)*8
tb=sum(B.values()); print('total %.1f MB, nonnull Span bytes %.1f MB (%.0f%%), zero words %.1f MB (%.0f%%)'%(tb/1e6,sum(SP.values())/1e6,100*sum(SP.values())/tb,sum(Z.values())/1e6,100*sum(Z.values())/tb))
for k,v in B.most_common(25): print('%7.2f MB  spans %4.0f%%  zero %4.0f%%  %s'%(v/1e6,100*SP[k]/v,100*Z[k]/v,k))
