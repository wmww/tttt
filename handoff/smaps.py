import sys, re, collections
maps=[]; cur=None
for l in open(sys.argv[1]):
    m=re.match(r'^([0-9a-f]+)-([0-9a-f]+) (\S+) \S+ \S+ \S+\s*(.*)$', l)
    if m:
        cur=dict(start=int(m[1],16), size=int(m[2],16)-int(m[1],16), perm=m[3], path=m[4], kv={}); maps.append(cur); continue
    k,_,v=l.partition(':')
    if v.strip().endswith('kB'): cur['kv'][k]=int(v.split()[0])
def kind(m):
    p=m['path']; s=m['size']>>10
    if p.startswith('/') : return 'file '+('lib' if 'thirdparty/lib' in p else 'tttt' if p.endswith('tttt') else 'so' if '.so' in p else 'source')
    if p: return p
    return 'anon %s' % m['perm']
agg=collections.defaultdict(lambda:[0,0,0,0])
rows=[]
for m in maps:
    rss=m['kv'].get('Rss',0); huge=m['kv'].get('AnonHugePages',0)
    k=kind(m)
    agg[k][0]+=1; agg[k][1]+=rss; agg[k][2]+=huge; agg[k][3]+=m['size']>>10
    rows.append((rss-huge, m['size']>>10, rss, huge, k, hex(m['start'])))
for k,v in sorted(agg.items(), key=lambda x:-x[1][1]): print("%-20s n %4d rss %8d KB huge %8d KB small-pages %7d (4K pages) vsize %d MB" % (k,v[0],v[1],v[2],(v[1]-v[2])//4, v[3]>>10))
print("biggest 4K-page rss mappings:")
for r in sorted(rows, reverse=True)[:int(sys.argv[2]) if len(sys.argv)>2 else 25]: print("  small %7d KB size %8d KB rss %7d huge %7d %s %s" % r)
