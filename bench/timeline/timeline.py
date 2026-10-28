#!/usr/bin/env python3
"""bench/timeline/timeline.c's output as a per-phase table: wall, the main thread's CPU, its run-queue wait and its
blocked time, the other threads' CPU and parallelism, and the top functions of each side.
timeline.py TIMELINE_OUT [--top N] [--phase SUBSTRING] [--inclusive] [--blocked]
--blocked (with TIMELINE_ALL=1): the other threads' blocked time per phase (each one's span less its run and run-queue
time, summed) and their samples blocked in a system call or a futex (S or D, a libc leaf), by the leaf and the first
three of our frames: which lock or mm call the threads sleep in"""
import argparse, bisect, collections, os, subprocess

ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('--top', type=int, default=6)
ap.add_argument('--phase', help='only phases whose name contains this')
ap.add_argument('--inclusive', action='store_true', help='rank functions anywhere on the stack, not leaves')
ap.add_argument('--skip', action='append', default=[], help='leaf-rank the first frame not matching these (e.g. malloc)')
ap.add_argument('--blocked', action='store_true', help='blocked time and blocked stacks of the other threads (TIMELINE_ALL=1)')
ap.add_argument('--state', help='only samples in these states (R running, S sleeping, D disk/uninterruptible); default R', default='R')
args = ap.parse_args()

maps, phases, stats, samples = [], [], collections.defaultdict(list), []
pid = None
for line in open(args.out):
    k = line[0]
    if k == 'M':
        parts = line[2:].split()
        if len(parts) >= 6:
            lo, hi = (int(x, 16) for x in parts[0].split('-'))
            maps.append((lo, hi, int(parts[2], 16), parts[5]))
    elif k == 'B': pid = int(line.split()[2])
    elif k == 'P':
        _, ns, name = line.rstrip('\n').split(' ', 2)
        phases.append((int(ns), name))
    elif k == 'R':
        _, ns, tid, run, wait = line.split()
        stats[int(tid)].append((int(ns), int(run), int(wait)))
    elif k == 'S':
        p = line.split()
        samples.append((int(p[1]), int(p[2]), p[3], [int(x, 16) for x in p[4:]]))

bases = {}
for lo, hi, off, path in maps:
    if off == 0 and path not in bases: bases[path] = lo
symtabs, cache = {}, {}
def symtab(path):
    key = path
    if key not in symtabs:
        syms = []
        # (A stripped library's symbols from its separate debug file, by build id: libc's malloc and futex internals)
        note = subprocess.run(['readelf', '-n', path], capture_output=True, text=True).stdout
        bid = next((l.split()[-1] for l in note.splitlines() if 'Build ID' in l), None)
        if bid and os.path.exists(f'/usr/lib/debug/.build-id/{bid[:2]}/{bid[2:]}.debug'):
            path = f'/usr/lib/debug/.build-id/{bid[:2]}/{bid[2:]}.debug'
        out = subprocess.run(['nm', '-n', '--defined-only', path], capture_output=True, text=True).stdout
        if not out.strip(): out = subprocess.run(['nm', '-n', '-D', '--defined-only', path], capture_output=True, text=True).stdout
        for l in out.splitlines():
            p = l.split()
            if len(p) >= 3 and p[1] in 'tTwW': syms.append((int(p[0], 16), p[2]))
        symtabs[key] = ([a for a, _ in syms], [n for _, n in syms])
    return symtabs[key]
def name(addr):
    if addr in cache: return cache[addr]
    ret = '?'
    for lo, hi, off, path in maps:
        if lo <= addr < hi:
            addrs, names = symtab(path)
            i = bisect.bisect_right(addrs, addr - bases.get(path, lo)) - 1
            ret = names[i] if i >= 0 else path.split('/')[-1]
            if '.so' in path: ret += '@' + path.split('/')[-1].split('.')[0]
            break
    cache[addr] = ret
    return ret

def at(tid, ns):
    """(run, wait) of tid at ns, from its last record at or before ns (0 before its first)"""
    rs = stats[tid]
    i = bisect.bisect_right(rs, (ns, 1 << 62, 0)) - 1
    return (rs[i][1], rs[i][2]) if i >= 0 else (0, 0)

t_begin = phases[0][0]
rows = []
for (a, _), (b, nm) in zip(phases, phases[1:]):
    if args.phase and args.phase not in nm: continue
    wall = (b - a) / 1e6
    main_run = main_wait = 0
    other_run = other_wait = 0
    busy = 0
    for tid in stats:
        r0, w0 = at(tid, a); r1, w1 = at(tid, b)
        r, w = (r1 - r0) / 1e6, (w1 - w0) / 1e6
        if tid == pid: main_run, main_wait = r, w
        else:
            other_run += r; other_wait += w
            if r > 0.05 * wall: busy += 1
    blocked = max(0.0, wall - main_run - main_wait)
    print(f'== {nm}: {(a - t_begin) / 1e6:8.1f} -> {(b - t_begin) / 1e6:8.1f} ms, wall {wall:7.1f} | main run {main_run:7.1f} '
          f'runq {main_wait:6.1f} blocked {blocked:7.1f} | others run {other_run:8.1f} runq {other_wait:7.1f}, '
          f'{busy} threads busy, parallelism {(main_run + other_run) / wall if wall else 0:5.2f}')
    for side, pick in (('main', lambda t: t == pid), ('others', lambda t: t != pid)):
        c = collections.Counter()
        n = 0
        for ns, tid, state, st in samples:
            if not (a <= ns < b) or not pick(tid): continue
            if args.state and state not in args.state: continue
            n += 1
            names = [name(x if i == 0 else x - 1) for i, x in enumerate(st)]
            if args.inclusive:
                for f in set(names): c[f] += 1
            else:
                f = next((f for f in names if not any(s in f for s in args.skip)), names[0] if names else '?')
                if args.skip and names and f != names[0]: f = names[0].split('@')[0] + '<' + f
                c[f] += 1
        if n:
            print(f'   {side:6} {n:5d} samples: ' + ', '.join(f'{f} {100 * k / n:.0f}%' for f, k in c.most_common(args.top)))
    if args.blocked:
        span_blocked = 0.0
        for tid in stats:
            if tid == pid: continue
            rs = [x for x in stats[tid] if a <= x[0] <= b]
            if len(rs) < 2: continue
            span_blocked += max(0.0, (rs[-1][0] - rs[0][0] - (rs[-1][1] - rs[0][1]) - (rs[-1][2] - rs[0][2])) / 1e6)
        c = collections.Counter()
        for ns, tid, state, st in samples:
            if not (a <= ns < b) or tid == pid or state not in 'SD': continue
            names = [name(x if i == 0 else x - 1) for i, x in enumerate(st)]
            if not names or '@' not in names[0]: continue
            ours = [f for f in names[:12] if '@' not in f][:3]
            c[f"{state} {names[0].split('@')[0]} < {' < '.join(ours)}"] += 1
        print(f'   others blocked {span_blocked:.1f} ms (summed); blocked samples by leaf < caller chain:')
        for k, v in c.most_common(args.top): print(f'     {v:5d} {k}')
