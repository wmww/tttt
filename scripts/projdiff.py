#!/usr/bin/env python3
"""tsgo against tttt on one project, safely: the (file, line, code) diagnostics each side alone reports, and whether
the whole CLI output is byte-identical.

    scripts/projdiff.py thirdparty/sources/REPO/tsconfig.json     # a tsconfig or its directory
    scripts/projdiff.py DIR --copy                                # on reduce.py's symlink-free scratch copy of DIR
    scripts/projdiff.py P -- --strict                             # extra options for both sides

tsgo: tttt-oracle (test/oracle: `tsc -p P --noEmit --pretty false --singleThreaded` in process, build info hidden
and never written: a plain `built/tsc -p` on a composite or incremental config writes a .tsbuildinfo into the shared
checkout, and the next run reads it and skips checking). tttt: release/tttt `--noEmit --pretty false` at its
defaults (threads and checkers: what users run) and at `--checkers 1 --threads 1`, inside a bwrap sandbox whose
root is read-only (tttt_run's limits and verdicts: a timeout or memory kill says so). A difference between the two
tttt runs is flagged as K-dependence (a tttt bug whatever tsgo says: scripts/kdiff.py).

--copy first runs `scripts/reduce.py --project DIR --no-reduce` (the program's files copied as regular files into
/tmp/reduce-*, a flattened tsconfig) and diffs there: what a reduction would start from. The copy stays (its path
is printed; `scripts/reduce.py --from COPY` continues on it).

Exit 0 when tsgo and tttt (defaults) agree by (file, line, code) and the two tttt runs agree, else 1.
"""
import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tscformat  # noqa: E402
import tttt_run  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORACLE = os.path.join(ROOT, 'thirdparty/typescript/built/tttt-oracle')
TTTT = os.path.join(ROOT, 'release/tttt')


def sandbox():
    return ['bwrap', '--ro-bind', '/', '/', '--bind', '/tmp', '/tmp', '--dev', '/dev', '--proc', '/proc',
            '--die-with-parent', '--'] if shutil.which('bwrap') else []


def tsgo(project, cwd, extra, timeout):
    """(output text, exit status) of tsc -p PROJECT --noEmit as tttt-oracle runs it"""
    p = subprocess.run([ORACLE, '--text', *extra], input=project + '\n', cwd=cwd, capture_output=True, text=True,
                       timeout=timeout)
    line = p.stdout.splitlines()[0] if p.stdout.strip() else ''
    if not line:
        sys.exit(f'projdiff: tttt-oracle gave nothing on {project}:\n{p.stderr[-1000:]}')
    r = json.loads(line)
    return r.get('text', ''), r.get('status')


def tttt(project, cwd, extra, timeout, mem_gb, k1):
    args = ['--checkers', '1', '--threads', '1'] if k1 else []
    r = tttt_run.run([*sandbox(), TTTT, '-p', project, '--noEmit', '--pretty', 'false', *args, *extra], cwd,
                     timeout=timeout, mem_gb=mem_gb, threads=1 if k1 else None, stderr=subprocess.PIPE)
    return re.sub(r'\x1b\[[0-9;]*m', '', r.text()), r.code, r.status, (r.err or b'').decode(errors='replace')


def keys(text):
    return collections.Counter((f, l, c) for f, l, _, c, _ in tscformat.parse(text))


def lines_of(text):
    return {(f, l, c): f'{f}({l}): TS{c} {m[:160]}' for f, l, _, c, m in tscformat.parse(text)}


def show(title, only, text):
    if not only:
        return
    named = lines_of(text)
    print(f'{title} ({sum(only.values())}):')
    for k in sorted(only, key=lambda k: (k[0], k[1], k[2]))[:60]:
        print(f'  {named.get(k, k)}' + (f'  x{only[k]}' if only[k] > 1 else ''))
    if len(only) > 60:
        print(f'  ... {len(only) - 60} more')


def make_copy(target):
    project, config = (os.path.dirname(target), os.path.basename(target)) if os.path.isfile(target) else (target, 'tsconfig.json')
    p = subprocess.run([sys.executable, os.path.join(ROOT, 'scripts/reduce.py'), '--project', project, '--config', config,
                        '--keep', '.', '--no-reduce'], capture_output=True, text=True)
    m = re.search(r'^copy: (/tmp/reduce-\S+)', p.stdout, re.M)
    if not m:
        sys.exit(f'projdiff: reduce.py made no copy:\n{p.stdout[-2000:]}{p.stderr[-2000:]}')
    copy = m.group(1)
    meta = json.load(open(os.path.join(copy, 'reduce.json')))
    print(f'copy: {copy} (rm -r {copy} when done)')
    return os.path.join(copy, meta['config'])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('project', help='a tsconfig.json or its directory')
    ap.add_argument('extra', nargs='*', help='after --: options for both sides')
    ap.add_argument('--copy', action='store_true', help="diff on reduce.py's scratch copy of the project")
    ap.add_argument('--timeout', type=int, default=900)
    ap.add_argument('--mem-gb', type=int, default=16, help="tttt's address space beyond its threads' (tttt_run)")
    ap.add_argument('--no-build', action='store_true')
    args = ap.parse_args()
    if not args.no_build:
        subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')], check=True, stdout=subprocess.DEVNULL)
    target = os.path.abspath(args.project)
    if args.copy:
        target = make_copy(target)
    cwd = target if os.path.isdir(target) else os.path.dirname(target)
    project = '.' if os.path.isdir(target) else os.path.basename(target)
    g_text, g_status = tsgo(project, cwd, args.extra, args.timeout)
    d_text, d_code, d_status, d_err = tttt(project, cwd, args.extra, args.timeout, args.mem_gb, False)
    k_text, k_code, k_status, k_err = tttt(project, cwd, args.extra, args.timeout, args.mem_gb, True)
    bad = False
    for name, status, err in (('tttt', d_status, d_err), ('tttt --checkers 1', k_status, k_err)):
        if status != 'ok':
            bad = True
            print(f'{name}: {status}' + (' (a resource limit, not a crash: --timeout / --mem-gb)'
                                         if tttt_run.resource_status(status) else '') +
                  ('\n  ' + '\n  '.join(err.strip().splitlines()[-5:]) if err.strip() else ''))
    g, d, k = keys(g_text), keys(d_text), keys(k_text)
    show('tsgo only', g - d, g_text)
    show('tttt only', d - g, d_text)
    if d_status == k_status == 'ok' and (d_text, d_code) != (k_text, k_code):
        bad = True
        print('K-DEPENDENT: tttt at its defaults and at --checkers 1 differ (a tttt bug: scripts/kdiff.py)')
        show('  defaults only', d - k, d_text)
        show('  --checkers 1 only', k - d, k_text)
        if d == k:
            print('  (same (file, line, code): the texts or exit codes differ)')
    same = d_text == g_text and d_code == g_status
    agree = g == d
    print(f'tsgo {sum(g.values())} diagnostics (exit {g_status}), tttt {sum(d.values())} (exit {d_code}, '
          f'--checkers 1: {sum(k.values())}, exit {k_code}): ' +
          ('byte-identical' if same else 'same (file, line, code), other text' if agree else 'DIFFER'))
    if agree and not same:
        a, b = g_text.splitlines(), d_text.splitlines()
        first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
        print(f'  first differing line {first + 1}:\n    tsgo: {a[first] if first < len(a) else "(end)"}\n'
              f'    tttt: {b[first] if first < len(b) else "(end)"}' + ('' if d_code == g_status else
                                                                         f'\n  exit codes: tsgo {g_status}, tttt {d_code}'))
    sys.exit(0 if agree and not bad else 1)


if __name__ == '__main__':
    main()
