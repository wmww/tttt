#!/usr/bin/env python3
"""tttt's raw CLI output at --checkers 1 against --checkers K on the sweeps' projects (plan 62).

tsgo's output doesn't depend on its checker count (measured 2026-10-13 at 1/4/16 on the source sweep; its one
exception, the `x?: undefined` property order of widened object literals, went with the 7.1.0-beta), so any
project listed here is a tttt bug: an evaluation-order dependence (another checker sees the files in another order), a
once-per-program diagnostic emulated per checker, or a tie the stable type order breaks by type id. The suite has its
own form (`baseline_tests --checkers K --dump DIR` against a K=1 dump) and the corpora `scripts/checkerdiff.py -k K`.

  scripts/kdiff.py                 # source sweep, K=16 (~7 min on 4 cores)
  scripts/kdiff.py --dt            # DefinitelyTyped (~4.5 min)
  scripts/kdiff.py -k 4 -f trpc    # some projects, K=4
  scripts/kdiff.py -k 1 -m 0       # one checker against a fresh checker per file (plan 67)

(-k 1 without -m compares one checker with itself: refused.) A run that times out, is killed (SIGKILL: the OOM
killer under load) or hits its address-space limit (--mem-gb, scaled to the threads by tttt_run) is no difference:
it is counted and listed apart, and so is the same crash at both counts (a crash at one count only differs).

At -m 0 a project whose files each pull a big shared type graph is slow, not stuck: dokploy/apps/dokploy (994
files, most importing the tRPC router: 165k types for one .tsx alone) takes ~270 s against 5 s, flat per file
(measured 2026-10-20), so it times out under load at the default --timeout.
"""
import argparse, concurrent.futures, json, os, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sweep, sourcesweep, tttt_run

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(project, cwd, k, timeout, memory='off', mem_gb=8, budget_gb=None):
    """(output, exit code, tttt_run status)"""
    r = tttt_run.run([os.path.join(ROOT, 'release/tttt'), '--pretty', 'false', '--threads', '1', '--checkers', str(k),
                      '--checker-memory', memory, '--noEmit', '-p', project], cwd, timeout=timeout, mem_gb=mem_gb,
                     budget_gb=budget_gb)
    return r.text(), r.code, r.status


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-k', type=int, default=16, help='checker count to compare with 1 (default 16)')
    ap.add_argument('-m', '--memory', default='off',
                    help="the K run's --checker-memory (0: a fresh checker per file, plan 67; with -k 1 the stronger "
                         "order-independence oracle)")
    ap.add_argument('--dt', action='store_true', help='the DefinitelyTyped sweep instead of the source sweep')
    ap.add_argument('-f', '--filter', help='regex the project must match (re.search)')
    ap.add_argument('-j', '--jobs', type=int,
                    help="projects at once (default: the cores for --dt; else as many runs of --mem-gb as fit in 80%% of "
                         "MemAvailable, tttt_run.jobs_for(), and the runs share that budget whatever -j is: K runs "
                         "without recycling take GBs each, openclaw's 4-7)")
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--mem-gb', type=int, default=8, help="each run's address space beyond the threads' (tttt_run)")
    args = ap.parse_args()
    if args.k == 1 and args.memory == 'off':
        ap.error('-k 1 compares one checker with itself: give -k K > 1, or -m 0 (a fresh checker per file)')
    subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
    if args.dt:
        jobs = [(p, sweep.DT) for p in sweep.list_packages(args.filter)]
    else:
        jobs = [(p, sourcesweep.SOURCES) for p in sourcesweep.list_projects(args.filter)]
    budget_gb = None if args.dt else args.mem_gb
    if args.jobs is None:
        args.jobs = os.cpu_count() if args.dt else tttt_run.jobs_for(args.mem_gb)

    def one(job):
        project, cwd = job
        return (project, run(project, cwd, 1, args.timeout, mem_gb=args.mem_gb, budget_gb=budget_gb),
                run(project, cwd, args.k, args.timeout, args.memory, args.mem_gb, budget_gb))

    differ, timeouts, crashes = 0, [], []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        for project, a, b in ex.map(one, jobs):
            if a[:2] == b[:2] and a[2] == 'ok':
                continue
            if tttt_run.resource_status(a[2]) or tttt_run.resource_status(b[2]):
                # (Not a difference: a fresh checker per file re-derives each file's dependencies, dokploy/apps/dokploy
                # takes minutes at -m 0 against seconds at K1; a memory kill under load isn't one either)
                timeouts.append(f'{project} ({a[2]}/{b[2]})')
                continue
            if a[2] == b[2] != 'ok' and a[:2] == b[:2]:
                # (The same crash at both counts: the sweeps' business, not a K-dependence)
                crashes.append(f'{project} ({a[2]})')
                continue
            differ += 1
            la, lb = set(a[0].split('\n')), set(b[0].split('\n'))
            print(json.dumps({'project': project, 'exit': [a[1], b[1]], 'only1': sorted(la - lb)[:8],
                              f'only{args.k}': sorted(lb - la)[:8]}), flush=True)
    memory = f' (--checker-memory {args.memory})' if args.memory != 'off' else ''
    print(f'{differ} / {len(jobs)} projects differ between 1 and {args.k} checkers{memory}' +
          (f', {len(timeouts)} timed out or killed: {", ".join(timeouts)}' if timeouts else '') +
          (f', {len(crashes)} crashed alike: {", ".join(crashes)}' if crashes else ''))
    sys.exit(1 if differ else 0)


if __name__ == '__main__':
    main()
