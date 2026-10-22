# Realistic affinity: local search on import-closure sets (what the resolver knows), scored on the true type sets (README.md)
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from model import *
def load_graph(path):
    rows = []
    for line in open(path):
        parts = line.split()
        rows.append((int(parts[0]), int(parts[1]), parts[2] == '1', parts[3], [int(x) for x in parts[4:]]))
    return rows
def closures(rows, depth=None):
    n = len(rows); out = []
    imp = [r[4] for r in rows]
    for i in range(n):
        seen = {i}; frontier = [i]; d = 0
        while frontier and (depth is None or d < depth):
            nxt = []
            for f in frontier:
                for g in imp[f]:
                    if g not in seen: seen.add(g); nxt.append(g)
            frontier = nxt; d += 1
        out.append(np.array(sorted(seen), np.int64))
    return out
if __name__ == '__main__':
    # python3 graph_ls.py DUMP GRAPH K...: the deal against local search on import closures (depth: all, 2), scored on
    # the dump's true type sets
    dump, graph = sys.argv[1], sys.argv[2]
    Ks = [int(k) for k in sys.argv[3:]] or [12]
    recs = load(dump, [1])
    rows = load_graph(graph)
    assert len(rows) == len(recs), (len(rows), len(recs))
    uniq = len(np.unique(np.concatenate([r[2] for r in recs])))
    w = np.array([max(r[1], 1) for r in rows], np.int64)
    for depth in [None, 2, 1]:
        cl = closures(rows, depth)
        print(f"closure depth {depth}: mean size {np.mean([len(c) for c in cl]):.0f}", flush=True)
        for K in Ks:
            a = deal(recs, K)
            per = cost(recs, a, K); l = loads(recs, a, K)
            print(f"  K{K} deal x{sum(per)/uniq:.3f} load {max(l)/(sum(l)/K):.2f}", flush=True)
            b = local_search(recs, a, K, slack=1.1, dense=cl, weights=w)
            per = cost(recs, b, K); l = loads(recs, b, K)
            print(f"  K{K} closure local search: x{sum(per)/uniq:.3f} max {max(per)} load {max(l)/(sum(l)/K):.2f}", flush=True)
