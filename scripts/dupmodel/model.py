# Offline model of file-to-checker assignments (README.md): per-file key sets (TTTT_DUP_DUMP), an assignment's cost =
# the sum over checkers of the union of their files' sets (sampled 1/4).
#   python3 model.py DUMP [K...]      (DOMAINS=1: types only, the history-stable domain; unset: every domain)
import sys, numpy as np
def load(path, domains=None):
    data = open(path, 'rb').read()
    recs = []
    off = 0
    while off < len(data):
        at, n = np.frombuffer(data, np.uint32, 2, off); off += 8
        b = int(np.frombuffer(data, np.uint64, 1, off)[0]); off += 8
        keys = np.frombuffer(data, np.uint64, int(n), off); off += 8 * int(n)
        if domains: keys = keys[np.isin(keys >> np.uint64(56), np.array(domains, np.uint64))]
        keys = np.unique(keys)
        recs.append((int(at), b, keys))
    recs.sort(key=lambda r: r[0])
    return recs

def deal(recs, K):
    runs = K * 2
    total = sum(r[1] for r in recs)
    before = 0; libs = 0; out = []
    for at, b, _ in recs:
        if b == 0:
            out.append(libs % K); libs += 1; continue
        out.append(int((before + b // 2) * runs // max(total, 1) % K)); before += b
    return out

def cost(recs, assign, K):
    per = []
    for c in range(K):
        sets = [r[2] for r, a in zip(recs, assign) if a == c]
        per.append(len(np.unique(np.concatenate(sets))) if sets else 0)
    return per

def loads(recs, assign, K):
    l = [0] * K
    for r, a in zip(recs, assign): l[a] += r[1]
    return l

def greedy(recs, K, slack=1.10):
    # Biggest files first; each to the checker whose union it overlaps most (fewest new keys), within a byte cap
    total = sum(r[1] for r in recs); cap = total / K * slack
    order = sorted(range(len(recs)), key=lambda i: -len(recs[i][2]))
    unions = [set() for _ in range(K)]; load = [0] * K; assign = [0] * len(recs)
    for i in order:
        keys = recs[i][2]
        best, bestnew = None, None
        for c in range(K):
            if load[c] + recs[i][1] > cap and load[c] > 0: continue
            new = len(keys) - sum(1 for k in keys.tolist() if k in unions[c]) if len(unions[c]) else len(keys)
            if bestnew is None or new < bestnew or (new == bestnew and load[c] < load[best]): best, bestnew = c, new
        if best is None: best = min(range(K), key=lambda c: load[c])
        assign[i] = best; load[best] += recs[i][1]; unions[best].update(keys.tolist())
    return assign

if __name__ == '__main__':
    import os
    doms = [int(d) for d in os.environ.get('DOMAINS', '').split(',') if d]
    recs = load(sys.argv[1], doms)
    allk = np.concatenate([r[2] for r in recs])
    uniq, counts = np.unique(allk, return_counts=True)
    F = len(recs)
    print(f"files {F}, sum of per-file sets {len(allk)}, union {len(uniq)} (x{len(allk)/len(uniq):.1f})")
    for frac in (0.9, 0.5, 0.25, 0.1):
        print(f"  keys in >= {frac:.0%} of files: {int((counts >= frac * F).sum())}")
    for K in [int(k) for k in sys.argv[2:]] or [4, 12, 16]:
        a = deal(recs, K); per = cost(recs, a, K); l = loads(recs, a, K)
        print(f"K{K} deal: total {sum(per)} x{sum(per)/len(uniq):.3f}, max checker {max(per)}, load max/avg {max(l)/(sum(l)/K):.2f}")
        g = greedy(recs, K); per = cost(recs, g, K); l = loads(recs, g, K)
        print(f"K{K} greedy(oracle sets): total {sum(per)} x{sum(per)/len(uniq):.3f}, max checker {max(per)}, load max/avg {max(l)/(sum(l)/K):.2f}")

def local_search(recs, assign, K, slack=1.10, passes=6, dense=None, weights=None):
    # Moves files between checkers while that lowers the total (sum of union sizes), loads within slack of the mean
    if dense is None:
        allk = np.concatenate([r[2] for r in recs])
        uniq = np.unique(allk)
        dense = [np.searchsorted(uniq, r[2]) for r in recs]
        U = len(uniq)
    else:
        U = max(int(d.max()) for d in dense if len(d)) + 1
    cnt = np.zeros((K, U), np.int32)
    load = np.zeros(K)
    assign = list(assign)
    for i, d in enumerate(dense):
        np.add.at(cnt[assign[i]], d, 1); load[assign[i]] += recs[i][1]
    cap = load.sum() / K * slack
    for p in range(passes):
        moved = 0
        for i in np.random.default_rng(p).permutation(len(recs)):
            d = dense[i]
            if not len(d): continue
            a = assign[i]
            if weights is None:
                removed = int((cnt[a, d] == 1).sum())
                added = (cnt[:, d] == 0).sum(axis=1)
            else:
                w = weights[d]
                removed = int(((cnt[a, d] == 1) * w).sum())
                added = ((cnt[:, d] == 0) * w).sum(axis=1)
            added[a] = removed
            delta = added - removed
            ok = load + recs[i][1] <= cap
            ok[a] = True
            delta[~ok] = 1 << 30
            b = int(np.argmin(delta))
            if b != a and delta[b] < 0:
                np.subtract.at(cnt[a], d, 1); np.add.at(cnt[b], d, 1)
                load[a] -= recs[i][1]; load[b] += recs[i][1]
                assign[i] = b; moved += 1
        if not moved: break
    return assign
