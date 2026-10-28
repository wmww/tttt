#!/usr/bin/env python3
"""tttt against bun's `bun check`, each at its defaults and single-threaded, at breadth (agents/notes/bun_breadth.md):
the source sweep projects tsgo checks slowest (test/perfsweep/tsgo.jsonl.gz's check_s; the `# perf: no` repos left
out). Per project bun once to warm the page cache and itself, then --runs rounds of four interleaved runs: tttt at its
defaults (MT), tttt --threads 1 --checkers 1 (ST), bun at its defaults, bun --threads=1; medians.

The headline is each side's own scaling, ST wall / MT wall (tttt's worst first), next to tttt MT / bun MT. tttt's
runs record `--time`'s phases (config, libs, load+parse, resolve names: the front end; typecheck; the rest of the wall
is start-up, output and exit) with the process CPU per phase (cores busy), TTTT_FILE_TIMES=1's per-file check CPU
(summed: at MT against ST it is the duplicated work across checkers) and the costliest file, the checkers' finishing
spread (the idle tail behind the critical path), the one-checker rerun (`time: rerun`), recycles and sys time. bun's
`--timing` line ("N files loaded in X ms, M checked in Y ms") splits its load from its check.

  scripts/bunsweep.py --bun PATH [-n 500] [-f REGEX] [--runs 3]   # measure (resumable: the results file below)
  scripts/bunsweep.py --last [-k 20] [-f REGEX]                   # reprint the tables
  scripts/bunsweep.py --bun PATH --redo -f REGEX                  # remeasure the matching projects
  scripts/bunsweep.py --last --show PROJECT                       # one project's four runs in detail

bun: `npm pack @oven/bun-linux-x64@canary` into a scratch dir, `package/bin/bun`. Run it on a quiet box (the load is
recorded per project); about 4-8 s per project. Results in /tmp/bunsweep-TREE.json. tttt is fast/tttt
(scripts/fastbuild.sh --measured) unless --bin, copied at the start (tttt_run.frozen).
"""
import argparse, concurrent.futures, json, os, re, resource, signal, statistics, subprocess, sys, tempfile, time

import perfsweep, sourcesweep, tttt_run

ROOT = perfsweep.ROOT
LAST = f'/tmp/bunsweep-{os.path.basename(ROOT)}.json'
FRONT = ('config', 'load+parse libs', 'load+parse files', 'resolve names')


def run(cmd, cwd, timeout, limit, env=None):
    """(output, status, wall s, peak RSS KB, user s, sys s); env given: tttt's TTTT_SYNC_EXIT=1 run, its wall to the
    last output byte (perfsweep.run's reasoning)"""
    start = time.time()
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                            preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit)))
    out, last = [], [start]

    def read():
        chunks = []
        while chunk := proc.stdout.read1(1 << 20):
            chunks.append(chunk)
            last[0] = time.time()
        out.append(b''.join(chunks))
    reader = concurrent.futures.ThreadPoolExecutor(1).submit(read)
    status = None
    while status is None:
        pid, code, usage = os.wait4(proc.pid, os.WNOHANG)
        if pid:
            status = os.waitstatus_to_exitcode(code)
            break
        if time.time() - start > timeout:
            proc.kill()
            _, code, usage = os.wait4(proc.pid, 0)
            status = 'timeout'
            break
        time.sleep(0.002)
    proc.returncode = 0
    reader.result()
    wall = (last[0] if env and status != 'timeout' else time.time()) - start
    return (out[0] if out else b'').decode(errors='replace'), status, wall, usage.ru_maxrss, usage.ru_utime, usage.ru_stime


def run_tttt(tttt, project, single, timeout, mem_gb):
    cwd, p = perfsweep.project_args(project)
    cmd = [tttt, '-p', os.path.join(cwd, p), '--noEmit', '--pretty', 'false', '--time',
           *(['--threads', '1', '--checkers', '1'] if single else [])]
    env = {**os.environ, 'TTTT_SYNC_EXIT': '1', 'TTTT_FILE_TIMES': '1'}
    text, status, wall, rss, user, sys_ = run(cmd, cwd, timeout, tttt_run.limit_bytes(mem_gb, tttt_run.threads_of(cmd)), env)
    phases, phase_cpu = {}, {}
    for m in re.finditer(r'^time: ([a-z+ ]+?) ([\d.]+) ms \(.*?process CPU ([\d.]+) ms\)', text, re.M):
        phases[m.group(1)] = float(m.group(2))
        phase_cpu[m.group(1)] = float(m.group(3))
    checkers = [float(m) for m in re.findall(r'^time: checker \d+: ([\d.]+) ms', text, re.M)]
    k = re.search(r'^time: checkers: (\d+) \((\d+) KB of checked source, (\d+) threads\)', text, re.M)
    rerun = re.search(r'^time: rerun in one checker after a parallel pass of ([\d.]+) ms at \d+ checkers \(([^,]+), (TS\d+)', text, re.M)
    files = [(float(m.group(1)), float(m.group(2)), m.group(3)) for m in re.finditer(
        r'^file time: +([\d.]+) ms check(?: taken)? \(checker \d+, cpu ([\d.]+) ms.*?\) (.*)$', text, re.M)]
    top = sorted(files, key=lambda f: -f[0])[:5]
    root = os.path.realpath(os.path.join(cwd, p))
    root = root if os.path.isdir(root) else os.path.dirname(root)
    name = lambda f: os.path.relpath(os.path.realpath(os.path.join(cwd, f)), root)
    return {'status': status if status in (0, 1, 2) else str(status), 'wall': wall, 'user': user, 'sys': sys_,
            'cpu': user + sys_, 'rss_kb': rss, 'phases': phases, 'phase_cpu': phase_cpu, 'checker_ms': checkers,
            'checkers': int(k.group(1)) if k else 0, 'checked_kb': int(k.group(2)) if k else 0,
            'threads': int(k.group(3)) if k else 0,
            'recycles': sum(int(m) for m in re.findall(r'^time: checker \d+ recycled (\d+) times', text, re.M)),
            'rerun': [float(rerun.group(1)), rerun.group(2), rerun.group(3)] if rerun else None,
            'file_cpu_ms': sum(f[1] for f in files), 'file_wall_ms': sum(f[0] for f in files), 'files': len(files),
            'top_files': [[round(f[0], 1), round(f[1], 1), name(f[2])] for f in top]}


def run_bun(bun, project, single, timeout, mem_gb):
    cwd, p = perfsweep.project_args(project)
    # (From an empty directory: a package.json "check" script would make `bun check` run that instead)
    with tempfile.TemporaryDirectory(prefix='bunsweep-') as tmp:
        build_info = ['--tsBuildInfoFile', os.path.join(tmp, 'x.tsbuildinfo')] if incremental(cwd, p) else []
        text, status, wall, rss, user, sys_ = run([bun, 'check', '-p', os.path.join(cwd, p), '--noEmit', '--pretty',
                                                   'false', '--timing', *(['--threads=1'] if single else []), *build_info],
                                                  tmp, timeout, mem_gb << 30)
    m = re.search(r'(\d+) files loaded in ([\d.]+)ms, (\d+) checked in ([\d.]+)ms', text)
    return {'status': status if status in (0, 1, 2) else str(status), 'wall': wall, 'user': user, 'sys': sys_,
            'cpu': user + sys_, 'rss_kb': rss,
            'files': int(m.group(1)) if m else 0, 'load_ms': float(m.group(2)) if m else None,
            'checked': int(m.group(3)) if m else 0, 'check_ms': float(m.group(4)) if m else None}


_incremental = {}


def incremental(cwd, p):
    key = (cwd, p)
    if key not in _incremental:
        _incremental[key] = perfsweep.incremental(cwd, p)
    return _incremental[key]


def med(xs):
    xs = [x for x in xs if x is not None]
    return statistics.median(xs) if xs else None


SIDES = ('mt', 'st', 'bun', 'bun1')


def summarize(project, r):
    """The medians of a project's runs: None when a run failed"""
    if any(not r.get(side) or any(x['status'] not in (0, 1, 2) for x in r[side]) for side in SIDES):
        return None
    mt, st, b, b1 = (r[side] for side in SIDES)
    front = lambda runs: med([sum(x['phases'].get(f, 0) for f in FRONT) for x in runs])
    front_cpu = lambda runs: med([sum(x['phase_cpu'].get(f, 0) for f in FRONT) for x in runs])
    check = lambda runs: med([x['phases'].get('typecheck', 0) for x in runs])
    s = {'project': project, 'load': r.get('load'),
         'mt': med([x['wall'] for x in mt]), 'st': med([x['wall'] for x in st]),
         'bun': med([x['wall'] for x in b]), 'bun1': med([x['wall'] for x in b1]),
         'mt_cpu': med([x['cpu'] for x in mt]), 'st_cpu': med([x['cpu'] for x in st]),
         'mt_sys': med([x['sys'] for x in mt]), 'st_sys': med([x['sys'] for x in st]),
         'bun_cpu': med([x['cpu'] for x in b]), 'bun1_cpu': med([x['cpu'] for x in b1]),
         'mt_rss': med([x['rss_kb'] for x in mt]) / 1024, 'bun_rss': med([x['rss_kb'] for x in b]) / 1024,
         'front_mt': front(mt), 'front_st': front(st), 'front_cpu_mt': front_cpu(mt),
         'check_mt': check(mt), 'check_st': check(st),
         'check_cpu_mt': med([x['phase_cpu'].get('typecheck', 0) for x in mt]),
         'file_cpu_mt': med([x['file_cpu_ms'] for x in mt]), 'file_cpu_st': med([x['file_cpu_ms'] for x in st]),
         'tail': med([x['phases'].get('typecheck', 0) - min(x['checker_ms']) if x['checker_ms'] else 0 for x in mt]),
         'top_mt': mt[0]['top_files'][:1], 'top_st': st[0]['top_files'][:1],
         'rerun': next((x['rerun'] for x in mt if x['rerun']), None),
         'recycles': max(x['recycles'] for x in mt), 'checkers': mt[0]['checkers'], 'threads': mt[0]['threads'],
         'bun_load_mt': med([x['load_ms'] for x in b]), 'bun_check_mt': med([x['check_ms'] for x in b]),
         'bun_load_st': med([x['load_ms'] for x in b1]), 'bun_check_st': med([x['check_ms'] for x in b1]),
         'files': b[0]['files']}
    s['rest_mt'] = s['mt'] * 1000 - s['front_mt'] - s['check_mt']
    return s


def report(results, k):
    rows = [s for p, r in results.items() if (s := summarize(p, r))]
    failed = [p for p, r in results.items() if not summarize(p, r)]
    if not rows:
        print('nothing measured')
        return
    q = lambda xs, f: xs[min(len(xs) - 1, int(f * len(xs)))]
    dist = lambda xs: (f'median {statistics.median(xs):.2f} p10 {q(xs, .1):.2f} p90 {q(xs, .9):.2f} '
                       f'min {xs[0]:.2f} max {xs[-1]:.2f}')
    scal = sorted(s['st'] / s['mt'] for s in rows)
    bscal = sorted(s['bun1'] / s['bun'] for s in rows)
    vs = sorted(s['mt'] / s['bun'] for s in rows)
    vs1 = sorted(s['st'] / s['bun1'] for s in rows)
    print(f'{len(rows)} projects ({len(failed)} with a failed run)')
    print(f'  tttt ST / MT (its own speed-up): {dist(scal)}; MT no faster than ST on {sum(1 for x in scal if x <= 1)}, '
          f'under 1.5x on {sum(1 for x in scal if x < 1.5)}')
    print(f'  bun  ST / MT:                    {dist(bscal)}')
    print(f'  tttt MT / bun MT: {dist(vs)}; bun faster on {sum(1 for x in vs if x > 1)} (> 25%: {sum(1 for x in vs if x > 1.25)})')
    print(f'  tttt ST / bun ST: {dist(vs1)}; bun faster on {sum(1 for x in vs1 if x > 1)}')
    dup = sorted(s['file_cpu_mt'] / s['file_cpu_st'] for s in rows if s['file_cpu_st'] > 50)
    if dup:
        print(f'  per-file check CPU summed, MT / ST (duplication + SMT + cold caches; projects over 50 ms): {dist(dup)}')
    fr = sorted(s['front_st'] / s['front_mt'] for s in rows if s['front_mt'])
    ch = sorted(s['check_st'] / s['check_mt'] for s in rows if s['check_mt'] > 20)
    print(f'  front end ST / MT: {dist(fr)}; typecheck ST / MT (over 20 ms): {dist(ch)}')
    print(f'  reruns at MT: {sum(1 for s in rows if s["rerun"])}; recycles at MT: {sum(1 for s in rows if s["recycles"])}')
    print(f'\nthe {k} worst tttt scalers (ms; scal = ST / MT wall; front/check = MT phases with cores busy, the ST phase '
          f'after the slash; dup = per-file check CPU MT / ST; tail = MT typecheck after the first checker finished):')
    print(f'  {"project":40} {"tST":>6} {"tMT":>6} {"scal":>5} {"bST":>6} {"bMT":>6} {"bscal":>5} {"t/b":>5} '
          f'{"front":>11} {"fcor":>4} {"check":>11} {"ccor":>4} {"dup":>4} {"tail":>5} {"rest":>4} {"sys":>5} {"K":>2}')
    for s in sorted(rows, key=lambda s: s['st'] / s['mt'])[:k]:
        print(f'  {s["project"][:40]:40} {s["st"] * 1000:6.0f} {s["mt"] * 1000:6.0f} {s["st"] / s["mt"]:5.2f} '
              f'{s["bun1"] * 1000:6.0f} {s["bun"] * 1000:6.0f} {s["bun1"] / s["bun"]:5.2f} {s["mt"] / s["bun"]:5.2f} '
              f'{s["front_mt"]:5.0f}/{s["front_st"]:<5.0f} {s["front_cpu_mt"] / s["front_mt"] if s["front_mt"] else 0:4.1f} '
              f'{s["check_mt"]:5.0f}/{s["check_st"]:<5.0f} {s["check_cpu_mt"] / s["check_mt"] if s["check_mt"] else 0:4.1f} '
              f'{s["file_cpu_mt"] / s["file_cpu_st"] if s["file_cpu_st"] else 0:4.1f} {s["tail"]:5.0f} {s["rest_mt"]:4.0f} '
              f'{s["mt_sys"] * 1000:5.0f} {s["checkers"]:2}'
              f'{" rerun " + s["rerun"][2] if s["rerun"] else ""}{" recycled" if s["recycles"] else ""}')
    print(f'\nthe {k} worst against bun at both defaults:')
    print(f'  {"project":40} {"t/b":>5} {"tMT":>6} {"bMT":>6} {"front":>6} {"bload":>6} {"check":>6} {"bchk":>6} {"scal":>5} {"bscal":>5}')
    for s in sorted(rows, key=lambda s: -s['mt'] / s['bun'])[:k]:
        print(f'  {s["project"][:40]:40} {s["mt"] / s["bun"]:5.2f} {s["mt"] * 1000:6.0f} {s["bun"] * 1000:6.0f} '
              f'{s["front_mt"]:6.0f} {s["bun_load_mt"] or 0:6.0f} {s["check_mt"]:6.0f} {s["bun_check_mt"] or 0:6.0f} '
              f'{s["st"] / s["mt"]:5.2f} {s["bun1"] / s["bun"]:5.2f}')
    for p in failed:
        r = results[p]
        print(f'  FAILED {p}: ' + ' '.join(f'{side} {[x["status"] for x in r.get(side, [])]}' for side in SIDES))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--bun', help="bun's executable")
    ap.add_argument('--bin', help='the tttt to measure (default: scripts/fastbuild.sh --measured)')
    ap.add_argument('-n', type=int, default=500, help='the n projects tsgo checks slowest')
    ap.add_argument('-f', '--filter', help='regex the project must match (re.search)')
    ap.add_argument('-k', type=int, default=20, help='rows per table')
    ap.add_argument('--runs', type=int, default=3)
    ap.add_argument('--last', action='store_true')
    ap.add_argument('--show', help="one project's summary and runs")
    ap.add_argument('--redo', action='store_true', help='remeasure projects already in the results')
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--mem-gb', type=int, default=16)
    ap.add_argument('--results', default=LAST, help='the results file')
    args = ap.parse_args()
    results = json.load(open(args.results)) if os.path.exists(args.results) else {}
    if args.show:
        print(json.dumps(summarize(args.show, results[args.show]), indent=1))
        for side in SIDES:
            for x in results[args.show][side]:
                print(side, json.dumps({k: v for k, v in x.items() if k not in ('phase_cpu',)}))
        return
    if not args.last:
        if not args.bun:
            sys.exit('--bun PATH is needed to measure')
        tttt = args.bin or os.path.join(ROOT, subprocess.run(
            [os.path.join(ROOT, 'scripts/fastbuild.sh'), '--measured'], check=True, stdout=subprocess.PIPE,
            text=True).stdout.strip())
        tttt = tttt_run.frozen(tttt)
        cache = perfsweep.load_cache()
        excluded = sourcesweep.no_perf_repos()
        projects = [p for p in cache if p.split('/')[0] not in excluded and '/' in p or p in os.listdir(sourcesweep.SOURCES)]
        projects = [p for p in projects if p.split('/')[0] not in excluded and cache[p].get('check_s')
                    and cache[p]['status'] in (0, 1, 2) and os.path.exists(os.path.join(sourcesweep.SOURCES, p))]
        projects = sorted(projects, key=lambda p: -cache[p]['check_s'])[:args.n]
        if args.filter:
            projects = [p for p in projects if re.search(args.filter, p)]
        todo = [p for p in projects if args.redo or p not in results]
        start = time.time()
        for i, p in enumerate(todo):
            run_bun(args.bun, p, False, args.timeout, args.mem_gb)  # (warm)
            r = {side: [] for side in SIDES}
            r['load'] = os.getloadavg()[0]
            for _ in range(args.runs):
                r['mt'].append(run_tttt(tttt, p, False, args.timeout, args.mem_gb))
                r['bun'].append(run_bun(args.bun, p, False, args.timeout, args.mem_gb))
                r['st'].append(run_tttt(tttt, p, True, args.timeout, args.mem_gb))
                r['bun1'].append(run_bun(args.bun, p, True, args.timeout, args.mem_gb))
            r['load'] = max(r['load'], os.getloadavg()[0])
            results[p] = r
            if i % 5 == 4 or i == len(todo) - 1:
                with open(args.results + '.tmp', 'w') as f:
                    json.dump(results, f)
                os.replace(args.results + '.tmp', args.results)
            print(f'{i + 1} / {len(todo)} {p} ({time.time() - start:.0f} s, load {os.getloadavg()[0]:.1f})',
                  file=sys.stderr, flush=True)
    if args.filter:
        results = {p: r for p, r in results.items() if re.search(args.filter, p)}
    report(results, args.k)
    if not args.last:
        print(f'\n{sourcesweep.sweep.load_note()} at the end of the run; results in {args.results}')


if __name__ == '__main__':
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    sys.exit(main())
