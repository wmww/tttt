import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from model import *
from graph_ls import load_graph, closures
name, K = sys.argv[1], int(sys.argv[2])
recs = load(f'../{name}.dump', [1])
assert [r[0] for r in recs] == list(range(len(recs)))
rows = load_graph(f'../{name}.graph')
uniq = len(np.unique(np.concatenate([r[2] for r in recs])))
a = deal(recs, K)
o = local_search(recs, a, K, slack=1.1)
w = np.array([max(r[1], 1) for r in rows], np.int64)
g = local_search(recs, a, K, slack=1.1, dense=closures(rows, 1), weights=w)
for label, x in (("deal", a), ("oracle", o), ("graph1", g)):
    per = cost(recs, x, K)
    print(f"{name} K{K} {label}: predicted x{sum(per)/uniq:.3f}")
    open(f'../deal.{name}.K{K}.{label}', 'w').write("\n".join(map(str, x)) + "\n")
