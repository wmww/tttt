#!/usr/bin/env python3
"""tttt against tsgo and `bun check` on Windows (agents/notes/windows.md "Benchmark"): each tool on each corpus,
interleaved, a warm-up then the median of N runs of wall, CPU (user + kernel, GetProcessTimes) and peak working set
(GetProcessMemoryInfo's PeakWorkingSetSize). Runs from the directory holding the corpora (bun from an empty one: in a
corpus with a package.json `check` script, `bun check` runs the script).

  scripts/winbench.py --corpora DIR --tttt EXE --tsc EXE [--bun EXE] [-n 5] [-c CORPUS]... [--single]

--single adds the single-threaded runs (tttt and tsgo --singleThreaded, bun --threads=1). Windows only (ctypes)."""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import statistics
import subprocess
import tempfile
import time

# The corpora bench.sh and the macOS table use: (name, project relative to the corpora directory, extra options)
REAL_ARGS = ['--noEmit', '--composite', 'false', '--incremental', 'false', '--emitDeclarationOnly', 'false']
CORPORA = [
    ('tsc-compiler', 'TypeScript6/src/compiler', REAL_ARGS),
    ('zod', 'zod/packages/zod/tsconfig.test.json', ['--noEmit']),
    ('drizzle', 'drizzle/drizzle-orm/tsconfig.corpus.json', ['--noEmit']),
    ('effect', 'effect/packages/effect/tsconfig.corpus.json', ['--noEmit']),
    ('excalidraw', 'excalidraw/tsconfig.corpus.json', ['--noEmit']),
    ('webpack', 'webpack', ['--noEmit']),
    ('vscode', 'vscode/src/tsconfig.corpus.json', ['--noEmit']),
]


class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
    _fields_ = [('cb', wt.DWORD), ('PageFaultCount', wt.DWORD), ('PeakWorkingSetSize', ctypes.c_size_t),
                ('WorkingSetSize', ctypes.c_size_t), ('QuotaPeakPagedPoolUsage', ctypes.c_size_t),
                ('QuotaPagedPoolUsage', ctypes.c_size_t), ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t),
                ('QuotaNonPagedPoolUsage', ctypes.c_size_t), ('PagefileUsage', ctypes.c_size_t),
                ('PeakPagefileUsage', ctypes.c_size_t)]


def measure(cmd, cwd):
    """(wall s, CPU s, peak working set MB, exit code) of one run"""
    start = time.perf_counter()
    p = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    handle = int(p._handle)
    # (The handle stays valid after the exit until Popen closes it: the times and counters are read before wait())
    ctypes.windll.kernel32.WaitForSingleObject(handle, 0xFFFFFFFF)
    wall = time.perf_counter() - start
    times = [wt.FILETIME() for _ in range(4)]
    ctypes.windll.kernel32.GetProcessTimes(handle, *[ctypes.byref(t) for t in times])
    ticks = lambda t: (t.dwHighDateTime << 32 | t.dwLowDateTime) / 1e7
    cpu = ticks(times[2]) + ticks(times[3])
    counters = PROCESS_MEMORY_COUNTERS(cb=ctypes.sizeof(PROCESS_MEMORY_COUNTERS))
    ctypes.windll.psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
    code = p.wait()
    return wall, cpu, counters.PeakWorkingSetSize / 2**20, code


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--corpora', required=True)
    ap.add_argument('--tttt', required=True)
    ap.add_argument('--tsc', required=True)
    ap.add_argument('--bun')
    ap.add_argument('-n', type=int, default=5)
    ap.add_argument('-c', '--corpus', action='append')
    ap.add_argument('--single', action='store_true')
    args = ap.parse_args()
    empty = tempfile.mkdtemp(prefix='winbench-')
    corpora = os.path.abspath(args.corpora)
    tools = [('tttt', lambda p, o: ([args.tttt, '-p', p] + o, corpora)),
             ('tsgo', lambda p, o: ([args.tsc, '-p', p] + o, corpora))]
    if args.bun:
        tools.append(('bun', lambda p, o: ([args.bun, 'check', '-p', os.path.join(corpora, p)] + o, empty)))
    if args.single:
        tools += [('tttt 1T', lambda p, o: ([args.tttt, '-p', p, '--singleThreaded'] + o, corpora)),
                  ('tsgo 1T', lambda p, o: ([args.tsc, '-p', p, '--singleThreaded'] + o, corpora))]
        if args.bun:
            tools.append(('bun 1T', lambda p, o: ([args.bun, 'check', '--threads=1', '-p', os.path.join(corpora, p)] + o, empty)))
    print('corpus | ' + ' | '.join(name for name, _ in tools))
    print('|'.join(['---'] * (len(tools) + 1)))
    for name, project, options in CORPORA:
        if args.corpus and name not in args.corpus:
            continue
        if not os.path.exists(os.path.join(corpora, project)):
            continue
        results = {tool: [] for tool, _ in tools}
        for i in range(args.n + 1):
            for tool, make in tools:
                cmd, cwd = make(project, options)
                r = measure(cmd, cwd)
                if i:
                    results[tool].append(r)
        cells = []
        for tool, _ in tools:
            rs = results[tool]
            wall = statistics.median(r[0] for r in rs)
            cpu = statistics.median(r[1] for r in rs)
            peak = max(r[2] for r in rs)
            codes = sorted({r[3] for r in rs})
            cells.append(f'{wall:.3f} / {cpu:.2f} / {peak:.0f}' + ('' if codes in ([0], [1], [2]) else f' (exit {codes})'))
        print(f'{name} | ' + ' | '.join(cells), flush=True)


if __name__ == '__main__':
    main()
