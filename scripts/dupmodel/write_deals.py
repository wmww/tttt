# Writes TTTT_DUP_DEAL files for K checkers (README.md): the current deal, the oracle (local search on the true type
# sets: the bound on what assignment can save) and graph1 (local search on direct imports), each with its predicted
# types. python3 write_deals.py DUMP GRAPH K OUTPREFIX  ->  OUTPREFIX.deal, OUTPREFIX.oracle, OUTPREFIX.graph1
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from model import *
from graph_ls import load_graph, closures
dump, graph, K, prefix = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
recs = load(dump, [1])
assert [r[0] for r in recs] == list(range(len(recs))), "the dump must have one record per checked file"
rows = load_graph(graph)
uniq = len(np.unique(np.concatenate([r[2] for r in recs])))
a = deal(recs, K)
o = local_search(recs, a, K, slack=1.1)
w = np.array([max(r[1], 1) for r in rows], np.int64)
g = local_search(recs, a, K, slack=1.1, dense=closures(rows, 1), weights=w)
for label, x in (("deal", a), ("oracle", o), ("graph1", g)):
    per = cost(recs, x, K)
    print(f"K{K} {label}: predicted x{sum(per)/uniq:.3f}")
    open(f"{prefix}.{label}", "w").write("\n".join(map(str, x)) + "\n")
