#!/usr/bin/env python3
"""The root census: would "share on second touch" pay? (agents/notes/threading.md "The census")

Per project, a -DTTTT_ROOT_CENSUS build (built here into /tmp/tttt-rootcensus) run with TTTT_DUP_CENSUS=1 at K1, at
the defaults and at K8 with sgcheck's program-order round robin deal: every type each checker made, its structural
fingerprint and its root declaration under four rules (outer / inner: the outermost / innermost declaration on the
resolution stack when it was made, else its home; home: the declaration it is an alias or instance of, a type
literal's node; closure: home, a union or intersection by its members), and which checkers touched each declaration
(resolved it, or made a type rooted there) and which one first. A declaration two or more checkers touched is hot.
Then the plain binary's (--bin, default main's release build through abbuild.sh) typecheck-phase process CPU, plain
and `TTTT_SHARED_GRAPH=1`, at K1 and both modes, best of --rounds interleaved.

The model (types as the unit of duplicated work): plain at K is P1 + a (T - T1) (T copies made, T1 at K1: a is the CPU
of a duplicated copy, from the two measured plains); the flag is P1 x tau (tau measured: flag K / plain K1, the whole
premium); routing keeps the private copies R (cold, rootless, literals, and with the dynamic counter the first
toucher's) and shares the rest, S copies of S_d distinct types: routed = P1 + a (R + S_d - T1) + (tau - 1) P1 S_d / D.
All shared gives the flag back, none plain.

    scripts/sharedgraph/rootcensus.py run [-f REGEX] [--out F] [--rounds N]   (appends JSON lines, resumable)
    scripts/sharedgraph/rootcensus.py report [--out F] [--tau 1.15,1.3]
"""
import argparse, json, os, re, statistics, subprocess, sys, tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import tttt_run
from sgcheck import project_args, read_list, DEFAULT_LIST, ROOT

CENSUS_BUILD = '/tmp/tttt-rootcensus'
RULES = ('outer', 'inner', 'home', 'closure')
TIME_RE = re.compile(r'^time: typecheck ([\d.]+) ms .*process CPU ([\d.]+) ms', re.M)


def build_census():
    if not os.path.exists(os.path.join(CENSUS_BUILD, 'build.ninja')):
        subprocess.run(['meson', 'setup', CENSUS_BUILD, '--buildtype', 'release', '-Dc_args=-g -DTTTT_ROOT_CENSUS'],
                       cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['ninja', '-C', CENSUS_BUILD, 'tttt'], check=True, stdout=subprocess.DEVNULL)
    return os.path.join(CENSUS_BUILD, 'tttt')


def mode_args(mode, deal_file, kdef):
    """One thread always: the CPU is then work, not SMT siblings and the load (the flag's concurrency factor,
    shared_graph.md "The premium, attributed", comes on top of the routed and the flagged runs alike)"""
    if mode == 'k1':
        return ['--threads', '1', '--checkers', '1'], {}
    if mode == 'rr8':
        return ['--threads', '1', '--checkers', '8'], {'TTTT_DUP_DEAL': deal_file, 'TTTT_NO_COSTLIEST_FIRST': '1'}
    return ['--threads', '1', '--checkers', str(kdef)], {}


def parse_census(text):
    rec = {}
    m = re.search(r'== root census: (\d+) checkers, (\d+) types, (\d+) distinct', text)
    if not m:
        return None
    rec['k'], rec['T'], rec['D'] = map(int, m.groups())
    for m in re.finditer(r'^RC types (\w+) ([\d ]+)$', text, re.M):
        v = list(map(int, m.group(2).split()))
        rec[f'types_{m.group(1)}'] = dict(none=v[0], hot=v[1], cold=v[2], neutral=v[3], dup_none=v[4], dup_hot=v[5],
                                          dup_cold=v[6], dup_neutral=v[7], hot_distinct=v[8], dup=v[9], distinct=v[10])
    for m in re.finditer(r'^RC dyn (\w+) (\d+) (\d+) (\d+)$', text, re.M):
        rec[f'dyn_{m.group(1)}'] = dict(private=int(m.group(2)), shared=int(m.group(3)), shared_distinct=int(m.group(4)))
    m = re.search(r'^RC cpu (\d+) (\d+) (\d+)$', text, re.M)
    if m:
        rec['cpu'] = dict(zip(('hot', 'cold', 'none'), map(int, m.groups())))
    return rec


def default_checkers(args, project):
    """The checker count the defaults choose (on this box)"""
    cwd, p = project_args(project)
    r = tttt_run.run([args.bin, '--time', '--noEmit', '--pretty', 'false', '-p', p], cwd, timeout=args.timeout,
                     mem_gb=args.mem_gb, env=dict(os.environ, TTTT_SYNC_EXIT='1'), stderr=subprocess.PIPE,
                     budget_gb=args.mem_gb)
    return len(set(re.findall(r'^time: checker (\d+): ', (r.err or b'').decode(errors='replace'), re.M))) or 1


def run_one(args, binary, project, mode, deal_file, census=False, flag=False, graph=None, kdef=8):
    cwd, p = project_args(project)
    margs, env_add = mode_args(mode, deal_file, kdef)
    env = dict(os.environ)
    env.pop('TTTT_SHARED_GRAPH', None)
    env.update(env_add)
    env['TTTT_SYNC_EXIT'] = '1'
    if census:
        env['TTTT_DUP_CENSUS'] = '1'
    if flag:
        env['TTTT_SHARED_GRAPH'] = '1'
    if graph:
        env['TTTT_DUP_GRAPH'] = graph
    r = tttt_run.run([binary, '--time', '--noEmit', '--pretty', 'false', *margs, '-p', p], cwd,
                     timeout=args.timeout, mem_gb=args.mem_gb, env=env, stderr=subprocess.PIPE,
                     budget_gb=args.mem_gb)
    err = (r.err or b'').decode(errors='replace')
    if r.status != 'ok':
        return None, r.status
    if census:
        return parse_census(err), 'ok'
    m = TIME_RE.search(err)
    return (float(m.group(2)) if m else None), 'ok'


def run(args):
    census_bin = build_census()
    projects = [p for p in read_list(args.projects) if re.search(args.filter, p)]
    done = set()
    if os.path.exists(args.out):
        done = {json.loads(l)['project'] for l in open(args.out)}
    with tempfile.TemporaryDirectory(prefix='rootcensus-') as tmp, open(args.out, 'a') as out:
        for project in projects:
            if project in done:
                continue
            rec = dict(project=project, census={}, cpu={}, status={})
            kdef = rec['kdef'] = default_checkers(args, project)
            graph = os.path.join(tmp, 'graph')
            c, st = run_one(args, census_bin, project, 'k1', None, census=True, graph=graph)
            rec['status']['census k1'] = st
            rec['census']['k1'] = c
            try:
                files = sum(1 for _ in open(graph))
            except OSError:
                files = 0
            deal = os.path.join(tmp, 'deal8')
            with open(deal, 'w') as f:
                f.write(''.join(f'{i % 8}\n' for i in range(files)))
            for mode in ('def', 'rr8'):
                c, st = run_one(args, census_bin, project, mode, deal, census=True, kdef=kdef)
                rec['status'][f'census {mode}'] = st
                rec['census'][mode] = c
            runs = {}
            for rnd in range(args.rounds):
                for mode, flag in (('k1', False), ('def', False), ('def', True), ('rr8', False), ('rr8', True)):
                    cpu, st = run_one(args, args.bin, project, mode, deal, flag=flag, kdef=kdef)
                    key = f'{mode}{"+flag" if flag else ""}'
                    if cpu is not None:
                        runs.setdefault(key, []).append(cpu)
                    else:
                        rec['status'][key] = st
            rec['cpu'] = {k: min(v) for k, v in runs.items()}
            rec['cpu_runs'] = runs
            rec['load'] = float(open('/proc/loadavg').read().split()[0])
            out.write(json.dumps(rec) + '\n')
            out.flush()
            print(f'{project}: done ({rec["status"] or "ok"})', flush=True)


def model(rec, mode, rule, dynamic, tau=None):
    """routed/plain, flag/plain (measured) and tau, None when a number is missing"""
    c, c1, cpu = rec['census'].get(mode), rec['census'].get('k1'), rec['cpu']
    if not c or not c1 or 'k1' not in cpu or mode not in cpu or f'{mode}+flag' not in cpu:
        return None
    p1, pk, fk = cpu['k1'], cpu[mode], cpu[f'{mode}+flag']
    t, t1, d = c['T'], c1['T'], c['D']
    a = (pk - p1) / (t - t1) if t - t1 > 0.01 * t1 else 0.0
    if tau is None:
        tau = fk / p1
    if dynamic:
        dy = c[f'dyn_{rule}']
        s, s_d = dy['shared'], dy['shared_distinct']
    else:
        ty = c[f'types_{rule}']
        s, s_d = ty['hot'], ty['hot_distinct']
    routed = p1 + a * (t - s + s_d - t1) + (tau - 1) * p1 * s_d / d
    return dict(routed=routed / pk, flag=fk / pk, tau=tau, dup=pk / p1, types_dup=t / t1, hot=s / t,
                fk=fk, pk=pk, p1=p1)


def is_corpus(project):
    from realworld import CORPORA
    return project in CORPORA


def report(args):
    recs = [json.loads(l) for l in open(args.out)]
    taus = [None] + [float(x) for x in args.tau.split(',')]
    for mode in ('def', 'rr8'):
        print(f'\n## mode {mode}: K, types K/K1, check CPU plain K/K1, flag/plain (measured), tau = flag/plainK1; '
              f'hot share of copies and routed/plain at the measured tau (s static, d dynamic)')
        print(f'| project | K | types K/K1 | CPU K/K1 | flag/plain | tau | ' +
              ' | '.join(f'{r} hot s/d | {r} routed s/d' for r in ('outer', 'inner', 'closure')) + ' |')
        print('|' + '---|' * (6 + 6))
        for rec in recs:
            m0 = model(rec, mode, 'inner', False)
            if not m0:
                print(f'| {rec["project"]} | (missing: {rec["status"]}) |')
                continue
            cells = []
            for rule in ('outer', 'inner', 'closure'):
                s, d = model(rec, mode, rule, False), model(rec, mode, rule, True)
                cells.append(f'{s["hot"]:.2f}/{d["hot"]:.2f}')
                cells.append(f'{s["routed"]:.2f}/{d["routed"]:.2f}')
            print(f'| {rec["project"]} | {rec["census"][mode]["k"]} | {m0["types_dup"]:.2f} | {m0["dup"]:.2f} | '
                  f'{m0["flag"]:.2f} | {m0["tau"]:.2f} | ' + ' | '.join(cells) + ' |')
        for group, pred in (('hubs', lambda p: not is_corpus(p)), ('corpora', is_corpus), ('all', lambda p: True)):
            sel = [r for r in recs if pred(r['project']) and model(r, mode, 'inner', False)]
            if not sel:
                continue
            print(f'\n{group} ({len(sel)}), medians: flag/plain {statistics.median(model(r, mode, "inner", False)["flag"] for r in sel):.3f}; '
                  f'types K/K1 {statistics.median(model(r, mode, "inner", False)["types_dup"] for r in sel):.2f}')
            for rule in RULES:
                for dyn in (False, True):
                    parts = []
                    for tau in taus:
                        vals = [model(r, mode, rule, dyn, tau)['routed'] for r in sel]
                        better = sum(v < 0.95 for v in vals)
                        worse = sum(v > 1.05 for v in vals)
                        parts.append(f'tau {"meas" if tau is None else tau}: {statistics.median(vals):.3f} '
                                     f'({better} better, {worse} worse)')
                    hot = statistics.median(model(r, mode, rule, dyn)['hot'] for r in sel)
                    print(f'  {rule:8} {"dyn" if dyn else "static"}: hot share {hot:.2f}; routed/plain ' + '; '.join(parts))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=('run', 'report'))
    ap.add_argument('-f', '--filter', default='')
    ap.add_argument('--projects', default=DEFAULT_LIST)
    ap.add_argument('--out', default='/tmp/rootcensus.jsonl')
    ap.add_argument('--bin', help='the plain binary timed (default: main\'s release build via abbuild.sh)')
    ap.add_argument('--rounds', type=int, default=3)
    ap.add_argument('--timeout', type=int, default=900)
    ap.add_argument('--mem-gb', type=float, default=16)
    ap.add_argument('--tau', default='1.15,1.3')
    args = ap.parse_args()
    if args.cmd == 'run':
        if not args.bin:
            args.bin = subprocess.run([os.path.join(ROOT, 'scripts', 'abbuild.sh'), 'main'], capture_output=True,
                                      text=True, check=True).stdout.strip().split('\n')[-1]
        run(args)
    else:
        report(args)


if __name__ == '__main__':
    main()
