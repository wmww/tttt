#!/usr/bin/env bash
# perfshape.sh -p PROJECT [--bun PATH] [--runs N] [--bin TTTT] [--no-tsgo]: a redacted shape report of one project, to
# paste where tttt is slow on code that can't be shared: numbers only, no paths, no names. tttt at its defaults
# (--time, TTTT_FILE_TIMES) N times (default 3, the median run by wall reported), interleaved with bun's `bun check`
# when --bun is given; then tttt at --checkers 1 --threads 1 once, and tsgo --singleThreaded --extendedDiagnostics
# once when thirdparty/typescript/built/tsc exists. Every tool runs from an empty directory (a package.json "check"
# script would make `bun check` run that instead), with --noEmit (incremental projects: tsgo and bun get a build info
# file of their own in it). TTTT is fast/tttt, else pgo/tttt, else release/tttt: build one first (scripts/fastbuild.sh).
#
# The headline: wall, CPU and cores busy per phase; the checkers' span against the sum of per-file check times (how
# many checkers were busy on average, their idle tails, the critical path: the costliest file's share of the span);
# checkers and threads chosen against the cores.
cd "$(dirname "$0")/.." || exit 1
exec python3 -I - "$PWD" "$@" <<'EOF'
import argparse, os, re, resource, statistics, subprocess, sys, tempfile, time, json

root = sys.argv[1]
ap = argparse.ArgumentParser(prog='perfshape.sh')
ap.add_argument('-p', required=True, help='tsconfig.json or its directory')
ap.add_argument('--bun', help="bun's executable: also `bun check` at its defaults")
ap.add_argument('--runs', type=int, default=3)
ap.add_argument('--bin', help='the tttt to measure')
ap.add_argument('--no-tsgo', action='store_true')
ap.add_argument('--timeout', type=int, default=600)
args = ap.parse_args(sys.argv[2:])
project = os.path.abspath(args.p)
tttt = args.bin or next((os.path.join(root, b) for b in ('fast/tttt', 'pgo/tttt', 'release/tttt')
                         if os.path.exists(os.path.join(root, b))), None)
if not tttt:
    sys.exit('perfshape.sh: no tttt build (scripts/fastbuild.sh)')
tsgo = os.path.join(root, 'thirdparty/typescript/built/tsc')
tsgo = tsgo if os.path.exists(tsgo) and not args.no_tsgo else None
lib_dir = os.path.join(root, 'thirdparty/lib')
cores = os.cpu_count()


def run(cmd, cwd, env=None):
    """(stdout, stderr, status, wall s, CPU s (user + sys), sys s, peak RSS MB)"""
    start = time.time()
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        proc = subprocess.Popen(cmd, cwd=cwd, stdout=out, stderr=err, env={**os.environ, **(env or {})})
        _, code, usage = os.wait4(proc.pid, 0)
        wall = time.time() - start
        proc.returncode = 0
        out.seek(0)
        err.seek(0)
        return (out.read().decode(errors='replace'), err.read().decode(errors='replace'), os.waitstatus_to_exitcode(code),
                wall, usage.ru_utime + usage.ru_stime, usage.ru_stime, usage.ru_maxrss / 1024)


work = tempfile.mkdtemp(prefix='perfshape-')
build_info = []
if tsgo:
    shown = subprocess.run([tsgo, '-p', project, '--showConfig'], cwd=work, capture_output=True, text=True).stdout
    try:
        o = json.loads(shown).get('compilerOptions', {})
        if o.get('incremental') is True or o.get('composite') is True:
            build_info = ['--tsBuildInfoFile', os.path.join(work, 'x.tsbuildinfo')]
    except ValueError:
        pass


def tttt_run(single=False, files=True):
    extra = ['--checkers', '1', '--threads', '1'] if single else []
    return run([tttt, '-p', project, '--noEmit', '--pretty', 'false', '--time', *extra, *(['--listFiles'] if files else [])],
               work, {'TTTT_FILE_TIMES': '1'} if files else {})


runs, buns = [], []
for i in range(args.runs):
    runs.append(tttt_run())
    if args.bun:
        buns.append(run([args.bun, 'check', '-p', project, '--noEmit', '--pretty', 'false', *build_info], work))
single = tttt_run(single=True, files=False)
tsgo_run = run([tsgo, '-p', project, '--noEmit', '--singleThreaded', '--extendedDiagnostics', '--pretty', 'false',
                *build_info], work) if tsgo else None
subprocess.run(['rm', '-rf', '--', work])

med = lambda rs: sorted(rs, key=lambda r: r[3])[len(rs) // 2]
out, err, status, wall, cpu, sys_s, rss = med(runs)
p = lambda x: f'{100 * x:.0f}%'

# Files: --listFiles' lines (bundled libraries: thirdparty/lib)
groups = {'lib': [0, 0], 'declaration (node_modules)': [0, 0], 'declaration (project)': [0, 0],
          'source (node_modules)': [0, 0], 'source (project)': [0, 0]}
for line in out.splitlines():
    if line.startswith('bundled:///libs/lib.'):
        path, g = os.path.join(lib_dir, line[len('bundled:///libs/lib.'):]), 'lib'
    elif line.startswith('/') and os.path.isfile(line):
        path = line
        nm = '/node_modules/' in line
        g = ('declaration' if re.search(r'\.d\.[cm]?ts$|\.d\.[^/]*\.ts$', line) else 'source') + (' (node_modules)' if nm else ' (project)')
    else:
        continue
    try:
        groups[g][1] += os.path.getsize(path)
        groups[g][0] += 1
    except OSError:
        pass

phases = []
for m in re.finditer(r'^time: ([a-z+ ]+?) ([\d.]+) ms \(resident (\d+) MB, main thread on a CPU ([\d.]+) ms, (\d+) faults, '
                     r'sys ([\d.]+) ms(?:, process CPU ([\d.]+) ms)?\)', err, re.M):
    phases.append((m.group(1), float(m.group(2)), int(m.group(3)), float(m.group(4)), int(m.group(5)), float(m.group(6)),
                   float(m.group(7)) if m.group(7) else None))
checkers = re.search(r'^time: checkers: (\d+) \((\d+) KB of checked source(?:, (\d+) threads)?', err, re.M)
types = sum(int(t) for t in re.findall(r'^time: checker \d+: [\d.]+ ms, arena \d+ MB, (\d+) types', err, re.M))
recycles = sum(int(t) for t in re.findall(r'^time: checker \d+ recycled (\d+) times', err, re.M))
taken = sum(int(t) for t in re.findall(r'^time: checker \d+: .* (\d+) files taken from others', err, re.M))

files = [(float(m.group(1)), int(m.group(3)), float(m.group(4))) for m in
         re.finditer(r'^file time: +([\d.]+) ms check( taken)? \(checker (\d+), cpu ([\d.]+) ms', err, re.M)]
after = [(float(m.group(1)), int(m.group(2))) for m in
         re.finditer(r'^file time: +([\d.]+) ms unused and declaration \(checker (\d+)\)', err, re.M)]
finish = {int(m.group(1)): float(m.group(2)) for m in re.finditer(r'^file time: checker (\d+) finished at ([\d.]+) ms', err, re.M)}
started = re.search(r'^file time: checkers started ([\d.]+) ms', err, re.M)

print(f'perfshape (tttt {os.path.basename(os.path.dirname(tttt))}, {cores} cores, {args.runs} runs, median by wall; '
      f'exit {status})')
total_files = sum(n for n, _ in groups.values())
print(f'files: {total_files}, {sum(b for _, b in groups.values()) >> 10} KB; checked source '
      f'{checkers.group(2) if checkers else "?"} KB')
for g, (n, b) in groups.items():
    if n:
        print(f'  {g:28} {n:6} files {b >> 10:8} KB')
k = int(checkers.group(1)) if checkers else 0
threads = int(checkers.group(3)) if checkers and checkers.group(3) else None
print(f'checkers {k}, threads {threads if threads is not None else "?"} of {cores} cores; recycles {recycles}, '
      f'files stolen {taken}; types {types} (summed over the checkers)')
print(f'wall {wall:.3f} s, CPU {cpu:.3f} s (cores busy {cpu / wall:.2f}), sys {p(sys_s / cpu if cpu else 0)} of CPU, '
      f'peak RSS {rss:.0f} MB (walls of the runs: {", ".join(f"{r[3]:.3f}" for r in runs)})')
print(f'  {"phase":18} {"wall ms":>8} {"share":>6} {"CPU ms":>8} {"cores":>6} {"main ms":>8} {"sys ms":>7} {"faults":>7} {"RSS MB":>7}')
total = sum(ph[1] for ph in phases)
for name, w, res, main, faults, s, pc in phases:
    print(f'  {name:18} {w:8.1f} {p(w / total if total else 0):>6} {pc if pc is not None else float("nan"):8.1f} '
          f'{(pc / w if pc is not None and w else float("nan")):6.2f} {main:8.1f} {s:7.1f} {faults:7} {res:7}')
print(f'  (process start and exit, outside the phases: {1000 * wall - total:.0f} ms)')
if files:
    fsum = sum(f[0] for f in files)
    fcpu = sum(f[2] for f in files)
    asum = sum(ms for ms, _ in after)
    span = max(finish.values()) if finish else 0
    tc = next((ph for ph in phases if ph[0] == 'typecheck'), None)
    busy = {c: sum(f[0] for f in files if f[1] == c) + sum(ms for ms, cc in after if cc == c) for c in finish}
    print(f'typecheck: wall {tc[1]:.1f} ms, CPU {tc[6] or 0:.1f} ms (cores busy {(tc[6] or 0) / tc[1] if tc[1] else 0:.2f}); '
          f'checkers started at {float(started.group(1)) if started else 0:.1f} ms, span {span:.1f} ms (to the last '
          f'checker\'s finish), {tc[1] - span:.1f} ms outside it')
    print(f'  per-file checks: wall sum {fsum:.1f} ms, CPU sum {fcpu:.1f} ms (wall / CPU {fsum / fcpu if fcpu else 0:.2f}: '
          f'above 1 is waiting for a core or blocking); unused/declaration {asum:.1f} ms; over the span: '
          f'{(fsum + asum) / span if span else 0:.2f} of {k} checkers busy (wall), {fcpu / span if span else 0:.2f} on a core')
    if finish:
        tails = sorted(span - f for f in finish.values())
        gaps = sorted(finish[c] - busy[c] for c in finish)
        print(f'  idle tails (span - a checker\'s finish) ms: median {statistics.median(tails):.1f}, max {tails[-1]:.1f}, '
              f'sum {sum(tails):.1f} ({p(sum(tails) / (k * span) if span and k else 0)} of checkers x span); '
              f'between files (finish - own file walls) ms: median {statistics.median(gaps):.1f}, max {gaps[-1]:.1f}')
    top = sorted(files, key=lambda f: -f[2])[:3]
    print(f'  costliest files by CPU: {", ".join(f"{f[2]:.1f}" for f in top)} ms = {", ".join(p(f[2] / fcpu) for f in top)} '
          f'of the CPU sum; the costliest\'s wall {top[0][0]:.1f} ms is {p(top[0][0] / span if span else 0)} of the span '
          f'(the critical path floor)')
    cpus = sorted((f[2] for f in files), reverse=True)
    print(f'  files checked {len(files)}; CPU median {statistics.median(cpus):.2f} ms, p90 {cpus[len(cpus) // 10]:.1f} ms')
so, se, ss, sw, sc, ssys, srss = single
st = re.search(r'^time: typecheck ([\d.]+) ms', se, re.M)
stypes = sum(int(t) for t in re.findall(r"^time: checker \d+: [\d.]+ ms, arena \d+ MB, (\d+) types", se, re.M))
sfront = sum(float(m.group(2)) for m in re.finditer(r'^time: ([a-z+ ]+?) ([\d.]+) ms', se, re.M) if m.group(1) not in ('typecheck',))
print(f'single-threaded (--checkers 1 --threads 1): wall {sw:.3f} s, CPU {sc:.3f} s, typecheck {float(st.group(1)) if st else 0:.1f} ms, '
      f'other phases {sfront:.1f} ms, peak RSS {srss:.0f} MB, types {stypes}; '
      f'defaults / single: wall {wall / sw:.2f}, CPU {cpu / sc:.2f}')
if buns:
    bo, be, bstatus, bw, bc, bsys, brss = med(buns)
    print(f'bun check: wall {bw:.3f} s, CPU {bc:.3f} s (cores busy {bc / bw:.2f}), sys {p(bsys / bc if bc else 0)} of CPU, '
          f'peak RSS {brss:.0f} MB, exit {bstatus} (walls: {", ".join(f"{r[3]:.3f}" for r in buns)}); '
          f'tttt / bun: wall {wall / bw:.2f}, CPU {cpu / bc:.2f}, RSS {rss / brss:.2f}')
if tsgo_run:
    to, te, tstatus, tw, tcpu, tsys, trss = tsgo_run
    stat = lambda name: m.group(1) if (m := re.search(rf'^{name}:\s+([\d.]+)', to, re.M)) else '?'
    print(f'tsgo --singleThreaded: wall {tw:.3f} s, parse {stat("Parse time")} s, bind {stat("Bind time")} s, '
          f'check {stat("Check time")} s, types {stat("Types")} (tttt K1 / tsgo types {stypes / int(stat("Types")) if stat("Types") != "?" and int(stat("Types")) else 0:.2f})')
EOF
