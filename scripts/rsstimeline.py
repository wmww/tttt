#!/usr/bin/env python3
"""RSS over time by kind of mapping, and what the peak is made of.

    scripts/rsstimeline.py [-i MS] [--bin B] [--every N] -- TTTT-ARGS...

Runs tttt (default: scripts/fastbuild.sh --measured) with TTTT_VMA_NAMES=1 (prelude.c vma_name(): its anonymous
mappings are named `[anon:tttt-KIND]`: checker arenas, other arenas (binder...), big tables (intern, node links,
relations), the AST and materializations, read source text, stacks) and --time, samples /proc/PID/smaps every MS
milliseconds (default 25), and prints the timeline (every N-th sample, phases from --time placed on it) and the
breakdown at the peak sample. Unnamed anonymous memory is malloc's thread arenas and whatever else; [heap] is malloc's
main heap; *.ts mappings are mapped source text. Sampling smaps takes the target's mmap lock: the run is a little
slower than a plain one, so take walls from a plain run.
"""
import argparse, os, re, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument('-i', type=float, default=25, help='sample interval, ms')
ap.add_argument('--bin', default=None)
ap.add_argument('--every', type=int, default=0, help='print every N-th sample (default: ~40 lines)')
ap.add_argument('args', nargs=argparse.REMAINDER)
a = ap.parse_args()
args = a.args[1:] if a.args and a.args[0] == '--' else a.args
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
binary = a.bin or subprocess.check_output([os.path.join(root, 'scripts/fastbuild.sh'), '--measured'], cwd=root, text=True).strip().splitlines()[-1]
if not os.path.isabs(binary): binary = os.path.join(root, binary)

def kind(name):
    if name.startswith('[anon:tttt-'): return name[11:-1]
    if name == '[heap]': return 'heap'
    if name == '': return 'anon'
    if name.startswith('[stack'): return 'stack'
    if name.startswith('['): return 'other'
    if name.endswith('.ts') or name.endswith('.tsx') or name.endswith('.mts') or name.endswith('.cts') or name.endswith('.js'): return 'mapped-text'
    if name == binary or name.endswith('/tttt'): return 'binary'
    return 'files'

def sample(pid):
    out = {}
    try:
        with open(f'/proc/{pid}/smaps') as f:
            name = None
            for line in f:
                c = line[0]
                if c.isdigit() or ('a' <= c <= 'f'):
                    parts = line.split(None, 5)
                    name = kind(parts[5].strip() if len(parts) > 5 else '')
                elif line.startswith('Rss:'):
                    out[name] = out.get(name, 0) + int(line.split()[1])
    except (FileNotFoundError, ProcessLookupError):
        return None
    return out

env = dict(os.environ, TTTT_VMA_NAMES='1', TTTT_SYNC_EXIT='1')
t0 = time.monotonic()
proc = subprocess.Popen([binary, '--time'] + args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env, text=True)
samples = []
phases = []
import threading
def read_err():
    for line in proc.stderr:
        if line.startswith('time:'):
            m = re.match(r'time: (.*?) ([\d.]+) ms', line)
            if m: phases.append(((time.monotonic() - t0) * 1e3, m.group(1), line.strip()))
threading.Thread(target=read_err, daemon=True).start()
while proc.poll() is None:
    s = sample(proc.pid)
    if s is None: break
    samples.append(((time.monotonic() - t0) * 1e3, s))
    time.sleep(a.i / 1e3)
proc.wait()
time.sleep(0.05)
if not samples:
    sys.exit('no samples')
kinds = sorted({k for _, s in samples for k in s}, key=lambda k: -max(s.get(k, 0) for _, s in samples))
total = lambda s: sum(s.values())
peak_t, peak = max(samples, key=lambda x: total(x[1]))
every = a.every or max(1, len(samples) // 40)
mb = lambda kb: kb / 1024
print('ms      total  ' + ' '.join(f'{k[:9]:>9}' for k in kinds))
pi = 0
for i, (t, s) in enumerate(samples):
    while pi < len(phases) and phases[pi][0] <= t:
        print(f'  -- {phases[pi][0]:7.0f} ms: {phases[pi][2][:110]}')
        pi += 1
    if i % every == 0 or t == peak_t:
        print(f'{t:6.0f} {mb(total(s)):6.0f}  ' + ' '.join(f'{mb(s.get(k, 0)):9.0f}' for k in kinds) + ('   <- peak' if t == peak_t else ''))
for p in phases[pi:]:
    print(f'  -- {p[0]:7.0f} ms: {p[2][:110]}')
print(f'\npeak {mb(total(peak)):.0f} MB at {peak_t:.0f} ms of {samples[-1][0]:.0f} ({len(samples)} samples), by kind:')
for k in kinds:
    print(f'  {k:12} {mb(peak.get(k, 0)):8.0f} MB')
