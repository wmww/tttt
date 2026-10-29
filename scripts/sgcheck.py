#!/usr/bin/env python3
"""The shared graph's correctness gate: `TTTT_SHARED_GRAPH=1` against plain tttt, at several checker counts and two
deals, on a list of projects (agents/notes/shared_graph.md). tsgo's output doesn't depend on its checker count and
plain tttt's doesn't either (scripts/kdiff.py, checkerdiff.py), so every flagged run must print exactly what one plain
checker prints: any run that differs is a shared-graph bug (a published value that depends on history, a race, a
nesting-order dependence), however rarely it shows.

Per project: one plain reference run (`--threads 1 --checkers 1`, TTTT_SHARED_GRAPH=0), then RUNS flagged runs per (K, deal), each
`--threads K --checkers K`, compared byte for byte (stdout and exit code) with the reference. Deals:
  def   the scheduler's own (program_set_checkers()'s byte runs, costliest file first)
  rr    program order round robin: file i to checker i % K (TTTT_DUP_DEAL, TTTT_NO_COSTLIEST_FIRST=1); the deal under
        which openbot/apps/auth-api's private-name race showed in every run (hub_regime.md)
At K1 the two deals are one, and since 2026-10-29 one checker has no graph (program_typecheck()): K1 compares plain
with plain, so it is no longer in the default list. A flagged run that differs is attributed: plain at the same K and deal is run once, and
when it differs too the difference is plain tttt's (a kdiff bug, `plain too`), not the flag's (`plain
exit N`/`plain signal N` when plain crashes there).

  scripts/sgcheck.py                        # the default list (scripts/sharedgraph/sgcheck_projects.txt), K 4,8,16, 3 runs
  scripts/sgcheck.py -f 'auth-api|drizzle' -n 10 -k 8
  scripts/sgcheck.py --projects FILE --deals rr
  scripts/sgcheck.py -f 'dokploy/apps/dokploy' -k 16 --deals rr -n 50 --load 3   # under load: 3 plain zod loops beside
  scripts/sgcheck.py -f zod -k 8 --force-poison 30   # the taint rules' oracles (src/ty/overlay.c): must not move,
  scripts/sgcheck.py -f dokploy/apps/dokploy -k 16 --deals rr --force-shadow   # and do (agents/bugs/shared_graph.md)

Projects are source sweep projects (REPO/path, under thirdparty/sources) or `-p` corpus names (realworld.py's), read
through their physical paths. Runs go through tttt_run (address-space limit, timeout, the memory budget: each run takes
--mem-gb of it); -j runs at once (default 4: concurrent runs are load, and load is what shows races). Differing
outputs are kept in a scratch directory printed at the end (deleted when nothing differs). Exit 1 when any flagged run
differs from the reference or crashes. ~25 min for the default list at 3 runs on a quiet 16-vCPU box.
"""
import argparse, concurrent.futures, difflib, os, re, shutil, subprocess, sys, tempfile, threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tttt_run
from realworld import CORPORA, BUILD_CORPORA

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = os.path.realpath(os.path.join(ROOT, 'thirdparty/sources'))
DEFAULT_LIST = os.path.join(ROOT, 'scripts/sharedgraph/sgcheck_projects.txt')


def project_args(project):
    """(cwd, -p argument), physical paths (a worktree's thirdparty/ is a symlink)"""
    if project in CORPORA:
        path = os.path.realpath(os.path.join(ROOT, CORPORA[project]))
        return (os.path.dirname(path), os.path.basename(path)) if path.endswith('.json') else (path, '.')
    return SOURCES, project


def read_list(path):
    with open(path) as f:
        return [l.split('#')[0].strip() for l in f if l.split('#')[0].strip()]


class Checker:
    def __init__(self, args, out_dir):
        self.args = args
        self.out_dir = out_dir
        self.lock = threading.Lock()

    def run(self, project, k, deal, flag, deal_file=None, graph_file=None):
        cwd, p = project_args(project)
        env = dict(os.environ)
        env['TTTT_SHARED_GRAPH'] = '0'  # plain (the default is the graph since 2026-10-29)
        if flag:
            env['TTTT_SHARED_GRAPH'] = '1'
            # (The taint rules' oracles, src/ty/overlay.c: a taint event forced at every N-th clean word computation, a
            # clean loser kept like a tainted one; neither may move the output)
            if self.args.force_poison:
                env['TTTT_GRAPH_FORCE_POISON'] = str(self.args.force_poison)
            if self.args.force_shadow:
                env['TTTT_GRAPH_FORCE_SHADOW'] = '1'
        if deal == 'rr' and k > 1:
            env['TTTT_DUP_DEAL'] = deal_file
            env['TTTT_NO_COSTLIEST_FIRST'] = '1'
        if graph_file:
            env['TTTT_DUP_GRAPH'] = graph_file
        r = tttt_run.run([self.args.bin, '--threads', str(k), '--checkers', str(k), '--noEmit', '--pretty', 'false',
                          '-p', p], cwd, timeout=self.args.timeout, mem_gb=self.args.mem_gb, env=env,
                         budget_gb=self.args.mem_gb)
        return r

    def keep(self, project, name, text):
        path = os.path.join(self.out_dir, project.replace('/', '__') + '.' + name)
        with open(path, 'w') as f:
            f.write(text)
        return path

    def project(self, project):
        """{(k, deal): [verdict per run]}, notes"""
        with tempfile.TemporaryDirectory(prefix='sgcheck-deal-') as tmp:
            graph = os.path.join(tmp, 'graph')
            ref = self.run(project, 1, 'def', False, graph_file=graph)
            if ref.status != 'ok':
                return None, f'reference {ref.status}'
            ref_key = (ref.code, ref.out)
            try:
                files = sum(1 for _ in open(graph))
            except OSError:
                files = 0
            deal_files = {}
            for k in self.args.k:
                deal_files[k] = os.path.join(tmp, f'deal{k}')
                with open(deal_files[k], 'w') as f:
                    f.write(''.join(f'{i % k}\n' for i in range(files)))
            results, notes, plain_seen = {}, [], {}
            for k in self.args.k:
                for deal in self.args.deals:
                    if k == 1 and deal != self.args.deals[0]:
                        continue
                    verdicts = []
                    for i in range(self.args.runs):
                        r = self.run(project, k, deal, True, deal_files[k])
                        if tttt_run.resource_status(r.status):
                            verdicts.append(r.status)
                            continue
                        if r.status != 'ok':
                            verdicts.append('crash')
                            notes.append(f'K{k} {deal} run {i + 1}: {r.status}, kept '
                                         f'{self.keep(project, f"K{k}.{deal}.{i + 1}.crash", r.text())}')
                            continue
                        if (r.code, r.out) == ref_key:
                            verdicts.append('same')
                            continue
                        if (k, deal) not in plain_seen:
                            plain = self.run(project, k, deal, False, deal_files[k])
                            plain_seen[(k, deal)] = ('same' if (plain.code, plain.out) == ref_key else 'plain too') \
                                if plain.status == 'ok' else f'plain {plain.status}'
                        verdicts.append('diff' if plain_seen[(k, deal)] == 'same' else plain_seen[(k, deal)])
                        a, b = ref.text().splitlines(), r.text().splitlines()
                        first = next((l for l in difflib.unified_diff(a, b, lineterm='', n=0)
                                      if l[:1] in '+-' and not l.startswith(('+++', '---'))), f'exit {ref.code} -> {r.code}')
                        lines = sum(1 for l in difflib.unified_diff(a, b, lineterm='', n=0)
                                    if l[:1] in '+-' and not l.startswith(('+++', '---')))
                        notes.append(f'K{k} {deal} run {i + 1}: {verdicts[-1]}, {lines} lines, first {first[:220]}')
                        self.keep(project, f'K{k}.{deal}.{i + 1}.out', r.text())
                    results[(k, deal)] = verdicts
            if any(v != 'same' for vs in results.values() for v in vs):
                self.keep(project, 'ref', ref.text())
            return results, notes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--projects', default=DEFAULT_LIST, help='a file of projects, one per line (# comments)')
    ap.add_argument('-f', '--filter', help='regex the project must match (re.search)')
    ap.add_argument('-k', default='4,8,16', help='checker counts (default 4,8,16; K1 has no graph)')
    ap.add_argument('--deals', default='def,rr', help='deals, among def and rr (default both)')
    ap.add_argument('-n', '--runs', type=int, default=3, help='flagged runs per (K, deal) (default 3)')
    ap.add_argument('-j', '--jobs', type=int, default=4, help='projects at once (default 4)')
    ap.add_argument('--bin', default=os.path.join(ROOT, 'release/tttt'))
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--mem-gb', type=float, default=8, help="each run's address space beyond the threads' (tttt_run)")
    ap.add_argument('--load', type=int, default=0, metavar='N',
                    help='N loops of plain runs of --load-corpus beside the checks (default threads, niced): the landing '
                         "gate's load, under which dokploy's one race showed (agents/bugs/shared_graph.md)")
    ap.add_argument('--load-corpus', default='zod', help='the corpus the --load loops check (default zod)')
    ap.add_argument('--force-poison', type=int, default=0, metavar='N',
                    help='flagged runs with TTTT_GRAPH_FORCE_POISON=N (a forced taint event every N-th clean word computation)')
    ap.add_argument('--force-shadow', action='store_true', help='flagged runs with TTTT_GRAPH_FORCE_SHADOW=1')
    ap.add_argument('-v', '--verbose', action='store_true', help='a line for every project, not only the differing ones')
    args = ap.parse_args()
    args.k = [int(x) for x in args.k.split(',')]
    args.deals = args.deals.split(',')
    if not set(args.deals) <= {'def', 'rr'}:
        ap.error('--deals: def and rr')
    if args.bin == os.path.join(ROOT, 'release/tttt'):
        subprocess_ok = os.system(f'ninja -C {os.path.join(ROOT, "release")} tttt > /dev/null') == 0
        if not subprocess_ok:
            sys.exit('release build failed')
    projects = read_list(args.projects)
    if args.filter:
        projects = [p for p in projects if re.search(args.filter, p)]
    projects = [p for p in projects if p not in BUILD_CORPORA]
    out_dir = tempfile.mkdtemp(prefix='sgcheck-')
    checker = Checker(args, out_dir)
    load_done = threading.Event()
    load_threads = []
    def load_loop():
        cwd, p = project_args(args.load_corpus)
        env = dict(os.environ, TTTT_SHARED_GRAPH='0')
        while not load_done.is_set():
            subprocess.run(['nice', '-n', '10', args.bin, '--noEmit', '-p', p], cwd=cwd, env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=args.timeout)
    for _ in range(args.load):
        t = threading.Thread(target=load_loop, daemon=True)
        t.start()
        load_threads.append(t)
    bad, total_runs, bad_runs = [], 0, 0
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        futures = {ex.submit(checker.project, p): p for p in projects}
        for fut in concurrent.futures.as_completed(futures):
            p = futures[fut]
            results, notes = fut.result()
            if results is None:
                print(f'{p}: {notes}', flush=True)
                continue
            cells = []
            differs = False
            for (k, deal), vs in results.items():
                total_runs += len(vs)
                n = sum(v not in ('same',) and not tttt_run.resource_status(v) for v in vs)
                bad_runs += n
                differs |= n > 0
                extra = ''.join(f' {v}' for v in sorted(set(vs) - {'same'}))
                cells.append(f'K{k}{"" if k == 1 else " " + deal} {n}/{len(vs)}{extra}')
            if differs:
                bad.append(p)
            if differs or args.verbose:
                print(f'{p}: {", ".join(cells)}', flush=True)
                for n in notes:
                    print(f'    {n}', flush=True)
    load_done.set()
    for t in load_threads:
        t.join()
    print(f'\n{len(projects)} projects, {total_runs} flagged runs: {bad_runs} differ from plain K1'
          f'{" (" + str(len(bad)) + " projects: " + " ".join(bad) + ")" if bad else ""}')
    if bad:
        print(f'outputs kept in {out_dir}')
    else:
        shutil.rmtree(out_dir)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
