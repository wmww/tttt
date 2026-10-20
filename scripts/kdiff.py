#!/usr/bin/env python3
"""tttt's raw CLI output at --checkers 1 against --checkers K on the sweeps' projects (plan 62).

tsgo's output doesn't depend on its checker count (measured 2026-10-13 at 1/4/16 on the source sweep; its one
exception is the `x?: undefined` property order of widened object literals, the undefinedProperties cache), so any
project listed here is a tttt bug: an evaluation-order dependence (another checker sees the files in another order), a
once-per-program diagnostic emulated per checker, or a tie the stable type order breaks by type id. The suite has its
own form (`baseline_tests --checkers K --dump DIR` against a K=1 dump) and the corpora `scripts/checkerdiff.py -k K`.

  scripts/kdiff.py                 # source sweep, K=16 (~7 min on 4 cores)
  scripts/kdiff.py --dt            # DefinitelyTyped (~4.5 min)
  scripts/kdiff.py -k 4 -f trpc    # some projects, K=4
  scripts/kdiff.py -k 1 -m 0       # one checker against a fresh checker per file (plan 67)

At -m 0 a project whose files each pull a big shared type graph is slow, not stuck: dokploy/apps/dokploy (994
files, most importing the tRPC router: 165k types for one .tsx alone) takes ~270 s against 5 s, flat per file
(measured 2026-10-20), so it times out under load at the default --timeout.
"""
import argparse, concurrent.futures, json, os, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sweep, sourcesweep

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(project, cwd, k, timeout, memory='off'):
    try:
        r = subprocess.run([os.path.join(ROOT, 'release/tttt'), '--pretty', 'false', '--threads', '1', '--checkers', str(k),
                            '--checker-memory', memory, '--noEmit', '-p', project], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        return r.stdout.decode(errors='replace'), r.returncode
    except subprocess.TimeoutExpired:
        return 'TIMEOUT', -1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-k', type=int, default=16, help='checker count to compare with 1 (default 16)')
    ap.add_argument('-m', '--memory', default='off',
                    help="the K run's --checker-memory (0: a fresh checker per file, plan 67; with -k 1 the stronger "
                         "order-independence oracle)")
    ap.add_argument('--dt', action='store_true', help='the DefinitelyTyped sweep instead of the source sweep')
    ap.add_argument('-f', '--filter', help='regex the project must match (re.search)')
    ap.add_argument('-j', '--jobs', type=int, default=os.cpu_count())
    ap.add_argument('--timeout', type=int, default=300)
    args = ap.parse_args()
    subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
    if args.dt:
        jobs = [(p, sweep.DT) for p in sweep.list_packages(args.filter)]
    else:
        jobs = [(p, sourcesweep.SOURCES) for p in sourcesweep.list_projects(args.filter)]

    def one(job):
        project, cwd = job
        return project, run(project, cwd, 1, args.timeout), run(project, cwd, args.k, args.timeout, args.memory)

    differ, timeouts = 0, []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        for project, a, b in ex.map(one, jobs):
            if a == b:
                continue
            if 'TIMEOUT' in (a[0], b[0]):
                # (Not a difference: a fresh checker per file re-derives each file's dependencies, dokploy/apps/dokploy
                # takes minutes at -m 0 against seconds at K1)
                timeouts.append(project)
                continue
            differ += 1
            la, lb = set(a[0].split('\n')), set(b[0].split('\n'))
            print(json.dumps({'project': project, 'exit': [a[1], b[1]], 'only1': sorted(la - lb)[:8],
                              f'only{args.k}': sorted(lb - la)[:8]}), flush=True)
    memory = f' (--checker-memory {args.memory})' if args.memory != 'off' else ''
    print(f'{differ} / {len(jobs)} projects differ between 1 and {args.k} checkers{memory}' +
          (f', {len(timeouts)} timed out: {", ".join(timeouts)}' if timeouts else ''))
    sys.exit(1 if differ else 0)


if __name__ == '__main__':
    main()
