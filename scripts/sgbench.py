#!/usr/bin/env python3
"""The shared graph's perf table: `TTTT_SHARED_GRAPH=1` against plain tttt on scripts/sgcheck.py's project list (the
hub projects and the -p corpora, agents/notes/hub_regime.md), at several modes, interleaved rounds, best of N.

Each round runs, per project and mode, plain and flagged once each (their order alternating by round), one run at a
time, from an empty scratch directory (-p by absolute physical path), `TTTT_SYNC_EXIT=1`, `--time` for the typecheck
phase's wall and process CPU, /usr/bin/time for the whole run's wall, CPU and peak RSS, the load average at the start
of each run recorded. Modes: a number N is `--threads N --checkers N`, `def` the defaults. Every run is one JSON line
in OUT (appended, so an interrupted run resumes: `--resume`); the report takes the best of the rounds per (project,
mode, side) for each measure separately and prints flag/plain per project, the medians per set (hubs: the source
sweep projects, corpora: realworld.py's), and the counts of projects whose flag wall is >5% worse, within 5%, >5%
better. Whether flag and plain printed the same thing is a column too (`out`: same / DIFF; sgcheck.py is the gate).

  nice -n 15 scripts/sgbench.py --modes 8,16,def -n 5 --out /tmp/sgbench.jsonl
  scripts/sgbench.py --report /tmp/sgbench.jsonl [--measure check_wall]
  scripts/sgbench.py -f 'drizzle|i18n' -n 3 --bin fast/tttt
"""
import argparse, hashlib, json, os, re, statistics, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tttt_run
from realworld import CORPORA, BUILD_CORPORA
from sgcheck import project_args, read_list, DEFAULT_LIST, ROOT

TIME_RE = re.compile(r'^time: typecheck ([\d.]+) ms .*process CPU ([\d.]+) ms', re.M)
MEASURES = ('check_wall', 'check_cpu', 'wall', 'cpu', 'rss')


def one(args, project, mode, flag, scratch):
    cwd, p = project_args(project)
    path = os.path.realpath(os.path.join(cwd, p))
    env = dict(os.environ)
    env.pop('TTTT_SHARED_GRAPH', None)
    env['TTTT_SYNC_EXIT'] = '1'
    if flag:
        env['TTTT_SHARED_GRAPH'] = '1'
    tfile = os.path.join(scratch, 'time')
    mode_args = [] if mode == 'def' else ['--threads', mode, '--checkers', mode]
    load = float(open('/proc/loadavg').read().split()[0])
    r = tttt_run.run(['/usr/bin/time', '-f', '%e %U %S %M', '-o', tfile, args.bin, '--time', '--noEmit', '--pretty',
                      'false', *mode_args, '-p', path], scratch, timeout=args.timeout, mem_gb=args.mem_gb, env=env,
                     stderr=tttt_run.subprocess.PIPE)
    rec = dict(project=project, mode=mode, flag=flag, status=r.status, code=r.code, load=load,
               out=hashlib.sha1(r.out).hexdigest()[:12])
    err = (r.err or b'').decode(errors='replace')
    rec['checkers'] = len(set(re.findall(r'^time: checker (\d+): ', err, re.M)))
    m = TIME_RE.search(err)
    if m:
        rec['check_wall'], rec['check_cpu'] = float(m.group(1)) / 1000, float(m.group(2)) / 1000
    try:
        e, u, s, mx = open(tfile).read().split('\n')[-2].split()
        rec.update(wall=float(e), cpu=float(u) + float(s), rss=int(mx) / 1024)
    except (OSError, ValueError, IndexError):
        pass
    return rec


def bench(args, projects, modes):
    done = set()
    if args.resume and os.path.exists(args.out):
        for l in open(args.out):
            r = json.loads(l)
            done.add((r['round'], r['project'], r['mode'], r['flag']))
    with tempfile.TemporaryDirectory(prefix='sgbench-') as scratch, open(args.out, 'a') as out:
        for rnd in range(1, args.rounds + 1):
            for project in projects:
                for mode in modes:
                    for flag in ((False, True) if rnd % 2 else (True, False)):
                        if (rnd, project, mode, flag) in done:
                            continue
                        rec = one(args, project, mode, flag, scratch)
                        rec['round'] = rnd
                        out.write(json.dumps(rec) + '\n')
                        out.flush()
                        if rec['status'] != 'ok':
                            print(f'round {rnd} {project} {mode} {"flag" if flag else "plain"}: {rec["status"]}',
                                  flush=True)
            print(f'round {rnd} done', flush=True)


def report(path, measure):
    runs = [json.loads(l) for l in open(path)]
    best, outs, loads = {}, {}, [r['load'] for r in runs]
    for r in runs:
        if r['status'] != 'ok':
            continue
        key = (r['project'], r['mode'], r['flag'])
        outs.setdefault((r['project'], r['mode']), set()).add(r['out'])
        b = best.setdefault(key, {})
        for m in MEASURES:
            if m in r:
                b[m] = min(b.get(m, r[m]), r[m])
    projects = list(dict.fromkeys(r['project'] for r in runs))
    modes = list(dict.fromkeys(r['mode'] for r in runs))
    rounds = max(r['round'] for r in runs)
    print(f'{len(runs)} runs, {rounds} rounds, load at run start median {statistics.median(loads):.1f} '
          f'(p90 {sorted(loads)[int(len(loads) * 0.9)]:.1f}, max {max(loads):.1f})')

    def ratio(p, mode, m):
        a, b = best.get((p, mode, True), {}).get(m), best.get((p, mode, False), {}).get(m)
        return a / b if a and b else None

    print('\n| set | mode | median check CPU | median check wall | median wall | median peak RSS | wall >5% worse | '
          'within 5% | >5% better |')
    print('|---|---|---|---|---|---|---|---|---|')
    for name, members in (('hubs', [p for p in projects if p not in CORPORA]),
                          ('corpora', [p for p in projects if p in CORPORA]), ('all', projects)):
        for mode in modes:
            cells = []
            for m in ('check_cpu', 'check_wall', 'wall', 'rss'):
                rs = [x for x in (ratio(p, mode, m) for p in members) if x]
                cells.append(f'{statistics.median(rs):.2f}' if rs else '-')
            ws = [x for x in (ratio(p, mode, measure) for p in members) if x]
            print(f'| {name} ({len(members)}) | {mode} | {" | ".join(cells)} | {sum(x > 1.05 for x in ws)} | '
                  f'{sum(0.95 <= x <= 1.05 for x in ws)} | {sum(x < 0.95 for x in ws)} |')
    print(f'\n(counts by {measure}; ratios flag/plain of the best of {rounds})\n')
    head = ' | '.join(f'{mode} CPU / check wall / wall / RSS' for mode in modes)
    print(f'| project | {head} | plain {measure} s ({" / ".join(modes)}) | out |')
    print('|---|' + '---|' * (len(modes) + 2))
    for p in projects:
        cells = []
        for mode in modes:
            cells.append(' / '.join(f'{ratio(p, mode, m):.2f}' if ratio(p, mode, m) else '-'
                                    for m in ('check_cpu', 'check_wall', 'wall', 'rss')))
        plain = ' / '.join(f'{best.get((p, mode, False), {}).get(measure, 0):.2f}' for mode in modes)
        same = all(len(outs.get((p, mode), ())) == 1 for mode in modes)
        print(f'| {p} | {" | ".join(cells)} | {plain} | {"same" if same else "DIFF"} |')
    bad = [r for r in runs if r['status'] != 'ok']
    if bad:
        print(f'\n{len(bad)} runs not ok: ' + ', '.join(f'{r["project"]} {r["mode"]} {r["flag"]} {r["status"]}'
                                                      for r in bad[:20]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--projects', default=DEFAULT_LIST, help="a file of projects (default sgcheck.py's list)")
    ap.add_argument('-f', '--filter', help='regex the project must match (re.search)')
    ap.add_argument('--modes', default='8,16,def', help='N (--threads N --checkers N) or def, comma separated')
    ap.add_argument('-n', '--rounds', type=int, default=5)
    ap.add_argument('--out', default=None, help='the runs file (JSON lines; default a new one under /tmp)')
    ap.add_argument('--resume', action='store_true', help='skip the (round, project, mode, side) already in --out')
    ap.add_argument('--report', metavar='FILE', help='only print the report of a runs file')
    ap.add_argument('--measure', default='check_wall', choices=MEASURES, help='what the counts are of')
    ap.add_argument('--bin', default=os.path.join(ROOT, 'release/tttt'))
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--mem-gb', type=float, default=16)
    args = ap.parse_args()
    if args.report:
        report(args.report, args.measure)
        return
    if args.bin == os.path.join(ROOT, 'release/tttt') and os.system(f'ninja -C {ROOT}/release tttt > /dev/null'):
        sys.exit('release build failed')
    projects = [p for p in read_list(args.projects) if p not in BUILD_CORPORA]
    if args.filter:
        projects = [p for p in projects if re.search(args.filter, p)]
    if not args.out:
        args.out = tempfile.mkstemp(prefix='sgbench-', suffix='.jsonl')[1]
    print(f'runs into {args.out}', flush=True)
    bench(args, projects, args.modes.split(','))
    report(args.out, args.measure)


if __name__ == '__main__':
    main()
