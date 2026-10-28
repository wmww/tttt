#!/usr/bin/env python3
"""The hub regime's probe (agents/notes/hub_regime.md): how a file's type graph was built, as a tree of resolutions
with their CPU time and types, and how parallel its construction could be.

  scripts/hubtrace.py trace PROJECT FILESUBSTR [--out PREFIX]   # FILE checked alone (fresh checker), traced twice
  scripts/hubtrace.py tree PREFIX [--depth D] [--min MS]          # the heaviest resolutions under the file
  scripts/hubtrace.py dag PREFIX [ROOTSUFFIX] [-P 8 16]           # critical paths: member and file granularity

`trace` builds tttt with -DTTTT_HUB_TRACE into /tmp/tttt-hubtrace (once per source change; the ordinary build has
none of it) and runs it with TTTT_CHECK_ONLY=FILESUBSTR (checked alone, the rest of the program loaded) and
TTTT_HUB_TRACE: once with uses edges (PREFIX.u.hub; their hash lookups on every memo hit inflate CPU times) and once
without (PREFIX.t.hub, TTTT_HUB_NO_USES=1: the times; node ids are the same in both runs).

The trace (ty/checker.c hub_trace_*): every resolution (each ty_resolving_push()..pop: a declaration's type, a
function's inferred return, a property, an alias, a signature's return, bases, base constraints...) and every checked
file is a node; the resolutions started while one is open are its children. `N id parent depth kind incl_ns self_ns
incl_types self_types file line`, `F id 0 checker incl_ns self_ns types self_types path`, and `U reader owner`: a memo
hit (a declaration's LazyType, a property's type, a signature's or function's return) by `reader` on what `owner`
computed. Self is what a node did outside its children: relations, instantiations, members, call resolution, which
this granularity can't split.

`dag`: node weight = self cost, dependencies = children + uses edges (close order is topological). The critical path
is a bound for building the graph with every resolution a task and perfect claim/wait on a shared store; a list
schedule on P threads (longest critical path first) is what P threads could reach. The file-granular version groups
the nodes by the file that declares them (an unnamed node goes with its ancestor's), collapses the files' strongly
connected components and gives the same numbers: what a shared store reaches when each file's declarations are built
by whoever checks that file, files scheduled bottom-up (bun's model, the parked shared graph with a program-order deal).
Uses edges cover only the four memos named above, so the bound is optimistic where siblings share instantiations,
variances or relation results (on dokploy a router's closure was within 10% of its cost checked alone, in types).
"""
import argparse, collections, heapq, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = '/tmp/tttt-hubtrace'
KINDS = ['decl', 'fnret', 'argret', 'prop', 'alias', 'binding', 'pattern', 'sigret', 'bases', 'basecons', 'basector',
         'mappedc', 'typeargs']


def short(p):
    p = re.sub(r'^.*?/thirdparty/(sources|corpora)/', '', p)
    return re.sub(r'node_modules/\.pnpm/[^/]+/node_modules/', 'nm:', p)


def build():
    if not os.path.exists(os.path.join(BUILD, 'build.ninja')):
        subprocess.run(['meson', 'setup', BUILD, '--buildtype', 'release', '-Dc_args=-g -DTTTT_HUB_TRACE'], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['ninja', '-C', BUILD, 'tttt'], check=True, stdout=subprocess.DEVNULL)


def trace(args):
    build()
    prefix = args.out or os.path.join('/tmp', 'hubtrace-' + re.sub(r'\W', '_', os.path.basename(args.file)))
    base = [os.path.join(BUILD, 'tttt'), '-p', os.path.realpath(args.project), '--noEmit', '--threads', '1',
            '--checkers', '1']
    for suffix, extra in (('.u.hub', {}), ('.t.hub', {'TTTT_HUB_NO_USES': '1'})):
        env = dict(os.environ, TTTT_CHECK_ONLY=args.file, TTTT_HUB_TRACE=prefix + suffix, **extra)
        subprocess.run(base, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(prefix)


class Trace:
    def __init__(self, prefix):
        self.nodes, self.order = {}, []
        self.children, self.uses = collections.defaultdict(list), collections.defaultdict(set)
        files = [(prefix + '.u.hub', 'U'), (prefix + '.t.hub', 'NF')] if os.path.exists(prefix + '.t.hub') else \
            [(prefix, 'UNF')]
        for path, kinds in files:
            for line in open(path):
                f = line.rstrip('\n').split(' ')
                if f[0] not in kinds:
                    continue
                if f[0] == 'U':
                    self.uses[int(f[1])].add(int(f[2]))
                elif f[0] == 'N':
                    i, par, _, kind, incl, self_, ty, sty = map(int, f[1:9])
                    self.nodes[i] = dict(par=par, kind=KINDS[kind], incl=incl / 1e6, self=self_ / 1e6, types=ty,
                                         stypes=sty, where=f"{short(' '.join(f[9:-1]))}:{f[-1]}")
                    self.children[par].append(i)
                    self.order.append(i)
                elif f[0] == 'F':
                    i = int(f[1])
                    incl, self_, ty, sty = map(int, f[4:8])
                    self.nodes[i] = dict(par=0, kind='FILE', incl=incl / 1e6, self=self_ / 1e6, types=ty, stypes=sty,
                                         where=short(' '.join(f[8:])))
                    self.order.append(i)

    def find(self, suffix):
        ids = [i for i, n in self.nodes.items() if (n['kind'] == 'FILE' if not suffix else n['where'].endswith(suffix))]
        if not ids:
            sys.exit(f'no node ends with {suffix}')
        return max(ids, key=lambda i: self.nodes[i]['incl'])

    def deps(self, i):
        return set(self.children[i]) | self.uses[i]

    def closure(self, root):
        out, stack = set(), [root]
        while stack:
            x = stack.pop()
            if x not in out:
                out.add(x)
                stack.extend(self.deps(x))
        return out


def schedule(weight, deps, cp, threads):
    """List schedule of a DAG (weight, deps by node) on `threads`, longest critical path first: the makespan"""
    indeg = {k: len(deps[k]) for k in weight}
    rdeps = collections.defaultdict(list)
    for k in weight:
        for d in deps[k]:
            rdeps[d].append(k)
    ready = [(-cp[k], k) for k in weight if not indeg[k]]
    heapq.heapify(ready)
    running, t, free = [], 0.0, threads
    while ready or running:
        while ready and free:
            _, k = heapq.heappop(ready)
            heapq.heappush(running, (t + weight[k], k))
            free -= 1
        t, k = heapq.heappop(running)
        free += 1
        for r in rdeps[k]:
            indeg[r] -= 1
            if not indeg[r]:
                heapq.heappush(ready, (-cp[r], r))
    return t


def tree(args):
    tr = Trace(args.prefix)
    def show(i, indent):
        n = tr.nodes[i]
        print(f"{'  ' * indent}{n['kind']:8} incl {n['incl']:8.1f} ms self {n['self']:7.1f} types {n['types']:7} "
              f"{n['where']}  [{len(tr.children[i])} children]")
        if indent < args.depth:
            for c in sorted(tr.children[i], key=lambda c: -tr.nodes[c]['incl']):
                if tr.nodes[c]['incl'] >= args.min:
                    show(c, indent + 1)
    show(tr.find(args.root), 0)


def dag(args):
    tr = Trace(args.prefix)
    root = tr.find(args.root)
    cl = tr.closure(root)
    order = [i for i in tr.order if i in cl]  # close order: dependencies first
    for key, unit in (('self', 'ms'), ('stypes', 'types')):
        weight = {i: tr.nodes[i][key] for i in cl}
        deps = {i: [d for d in tr.deps(i) if d in cl] for i in cl}
        cp = {}
        for i in order:
            cp[i] = weight[i] + max((cp[d] for d in deps[i]), default=0)
        total = sum(weight.values())
        line = ', '.join(f'{p} threads {schedule(weight, deps, cp, p):.0f} (x{total / schedule(weight, deps, cp, p):.1f})'
                         for p in args.P)
        print(f"[{unit}] {tr.nodes[root]['where']}: serial {total:.0f}; member-granular critical path {cp[root]:.0f} "
              f"(x{total / cp[root]:.1f}); {line}")
        if unit == 'ms':
            x = root
            while True:
                n = tr.nodes[x]
                print(f"    {n['kind']:8} self {n['self']:6.1f} cp {cp[x]:7.1f} incl {n['incl']:7.1f} {n['where']}")
                if not deps[x]:
                    break
                x = max(deps[x], key=lambda d: cp[d])
        # File granularity
        def file_of(i):
            while True:
                n = tr.nodes[i]
                if n['kind'] == 'FILE':
                    return n['where']
                w = n['where'].rsplit(':', 1)[0]
                if w != '-':
                    return w
                i = n['par']
        group = {i: file_of(i) for i in cl}
        fw, fe = collections.Counter(), collections.defaultdict(set)
        for i in cl:
            fw[group[i]] += weight[i]
            fe[group[i]].update(group[d] for d in deps[i] if group[d] != group[i])
        comp = scc(list(fw), fe)
        cw, ce, members = collections.Counter(), collections.defaultdict(set), collections.defaultdict(list)
        for v in fw:
            cw[comp[v]] += fw[v]
            members[comp[v]].append(v)
            ce[comp[v]].update(comp[w] for w in fe[v] if comp[w] != comp[v])
        ccp = {}
        for k in sorted(cw):  # (scc() numbers components dependencies first)
            ccp[k] = cw[k] + max((ccp[d] for d in ce[k]), default=0)
        r = comp[group[root]]
        big = max(cw, key=lambda k: cw[k])
        line = ', '.join(f'{p} threads {schedule(cw, ce, ccp, p):.0f}' for p in args.P)
        print(f"     file-granular: {len(fw)} files in {len(cw)} components (largest {len(members[big])} files, "
              f"{cw[big]:.0f}); critical path {ccp[r]:.0f} (x{total / ccp[r]:.1f}); {line}")


def scc(vertices, edges):
    """Tarjan, iterative: component numbers in reverse topological order (a component after those it reaches)"""
    index, low, stack, on, comp, counter, count = {}, {}, [], set(), {}, [0], [0]
    for v0 in vertices:
        if v0 in index:
            continue
        work = [(v0, iter(edges[v0]))]
        index[v0] = low[v0] = counter[0]; counter[0] += 1; stack.append(v0); on.add(v0)
        while work:
            u, it = work[-1]
            for w in it:
                if w not in index:
                    index[w] = low[w] = counter[0]; counter[0] += 1; stack.append(w); on.add(w)
                    work.append((w, iter(edges[w])))
                    break
                if w in on:
                    low[u] = min(low[u], index[w])
            else:
                work.pop()
                if work:
                    low[work[-1][0]] = min(low[work[-1][0]], low[u])
                if low[u] == index[u]:
                    while True:
                        w = stack.pop(); on.discard(w); comp[w] = count[0]
                        if w == u:
                            break
                    count[0] += 1
    return comp


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    t = sub.add_parser('trace'); t.add_argument('project'); t.add_argument('file'); t.add_argument('--out')
    t.set_defaults(fn=trace)
    t = sub.add_parser('tree'); t.add_argument('prefix'); t.add_argument('root', nargs='?', default='')
    t.add_argument('--depth', type=int, default=3); t.add_argument('--min', type=float, default=5)
    t.set_defaults(fn=tree)
    t = sub.add_parser('dag'); t.add_argument('prefix'); t.add_argument('root', nargs='?', default='')
    t.add_argument('-P', type=int, nargs='*', default=[8, 16]); t.set_defaults(fn=dag)
    args = ap.parse_args()
    args.fn(args)


if __name__ == '__main__':
    main()
