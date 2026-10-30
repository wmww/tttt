"""One way for every oracle to run tttt (or tsgo) on a project: an address-space limit scaled to the threads, a
timeout, and a verdict that tells a resource kill from a crash.

    import tttt_run
    r = tttt_run.run([TTTT, '--noEmit', '-p', P], cwd, timeout=300, mem_gb=8, threads=1)
    r.out, r.err, r.code, r.status, r.wall

`status`:
    ok          exit 0-2 (tsc 7.1's codes: 2 with diagnostics; `ok_codes` to change)
    timeout     still running after `timeout` s (killed then)
    killed      SIGKILL from outside: the kernel's OOM killer or a person, under load (not tttt's doing)
    memory      the address-space limit was hit: tttt's "Couldn't reserve address space", "couldn't start a loader
                thread" (a stack not mapped), an "out of memory" panic; raise mem_gb (it's virtual memory reserved,
                not used)
    exit N / signal N   a crash (panic, assertion, segfault)
    log_error   exit ok but a cleat `file.c:N: error:` / `panic:` line in the output (`log_errors=True`)

`resource(status)` is True for timeout, killed and memory: an oracle reports those apart from crashes and from
differences (a memory kill once read as a K-dependence in kdiff.py, and an AST reservation failure as a crash in a new
oracle that had copied run_tttt with a 4 GB limit).

The binary is frozen: an argument naming a tttt build (`.../{release,debug,pgo,bolt,fast}/tttt`) is replaced by a
copy taken at its first run in this process (/tmp/tttt-frozen-PID-*, deleted at exit), so a rebuild while a long
oracle runs (you keep editing) never swaps the binary under it halfway: every project is checked by the build the run
started with. (For a whole tree frozen, scripts and all: run the oracle from `scripts/abbuild.sh HEAD --dir`.)

The limit: RLIMIT_AS = mem_gb + THREAD_GB per thread (threads: the --threads given, else the machine's cores, the
CLI's default). tttt reserves address space per thread (AST slabs, arenas, stacks; MAP_NORESERVE, so nothing is
used until touched): VmPeak measured 2026-10-23 on zod's test config 1 GB at --threads 1, 2 at 4, 3 at 8, 5 at 16,
types/lodash 0 / 1 / 2 / 3. mem_gb is then what the project itself may take.

The memory budget: RLIMIT_AS bounds one run, not the sum of a driver's parallel runs (2026-10-28: kdiff.py at -j 16,
openclaw's configs at K16 without recycling taking 4-7 GB each, filled the 60 GB box; the openclaw line of
agents/notes/one_checker_rerun.md). A run given `budget_gb` (what it may really use, typically its mem_gb) takes that much from a process-wide
budget of BUDGET_FRACTION of MemAvailable (read at the first such run) and waits while the budget is spent, so a
ThreadPoolExecutor of any size never has more than the budget's worth running; a run larger than the whole budget
still runs, alone. `jobs_for(gb)` is the number of such runs that fit, for a driver's default -j. Drivers whose runs
are small (--checkers 1 on DefinitelyTyped packages) don't pass it and keep os.cpu_count() jobs.
"""
import atexit
import dataclasses
import os
import re
import resource
import shutil
import signal
import subprocess
import sys
import threading
import time

THREAD_GB = 0.5
OK_CODES = (0, 1, 2)
# (tttt's panics when an mmap or malloc fails, a thread's stack not mapped included: "couldn't start a loader thread"
# at 1 GB on zod; Go's and Python's own)
MEMORY_TEXT = re.compile(r"Couldn't reserve address space|couldn't start a \w+ thread|Cannot allocate memory|"
                         r"[Oo]ut of memory|mmap failed|MemoryError|std::bad_alloc|fatal error: runtime: out of memory")
LOG_ERROR = re.compile(r'\.c:\d+: (error|panic):')


BUDGET_FRACTION = 0.8


def mem_available():
    """MemAvailable in bytes (/proc/meminfo), None if unknown"""
    try:
        with open('/proc/meminfo') as f:
            for line in f:
                if line.startswith('MemAvailable:'):
                    return int(line.split()[1]) * 1024
    except OSError:
        pass
    return None


def jobs_for(gb, fraction=BUDGET_FRACTION):
    """How many runs of `gb` GB each fit in fraction of MemAvailable (at least 1, at most the cores; the cores when
    MemAvailable is unknown)"""
    cores = os.cpu_count() or 1
    avail = mem_available()
    if not avail or not gb:
        return cores
    return max(1, min(cores, int(avail * fraction // (gb * (1 << 30)))))


class _Budget:
    def __init__(self):
        self.cond = threading.Condition()
        self.total = None
        self.used = 0

    def acquire(self, n):
        with self.cond:
            if self.total is None:
                avail = mem_available()
                self.total = int(avail * BUDGET_FRACTION) if avail else 1 << 62
            # (One larger than the whole budget runs alone rather than never)
            self.cond.wait_for(lambda: self.used == 0 or self.used + n <= self.total)
            self.used += n

    def release(self, n):
        with self.cond:
            self.used -= n
            self.cond.notify_all()


_budget = _Budget()


def limit_bytes(mem_gb, threads=None):
    # (macOS refuses an RLIMIT_AS below the address space a process already reserves, and enforces none: no limit)
    if sys.platform == 'darwin':
        return None
    threads = threads or os.cpu_count() or 1
    return int((mem_gb + THREAD_GB * threads) * (1 << 30))


def maxrss_kb(usage):
    """A getrusage() result's peak resident set in KB (Linux reports KB, macOS bytes)"""
    return usage.ru_maxrss // 1024 if sys.platform == 'darwin' else usage.ru_maxrss


def threads_of(cmd):
    """The --threads of a tttt command line, else None (the cores)"""
    for i, a in enumerate(cmd[:-1]):
        if a == '--threads':
            try:
                return int(cmd[i + 1])
            except ValueError:
                return None
    return None


BUILD_DIRS = ('release', 'debug', 'pgo', 'bolt', 'fast')
_frozen = {}
_frozen_lock = threading.Lock()


def frozen(path):
    """A copy of the tttt binary at path, taken once per process (see the module's comment)"""
    real = os.path.realpath(path)
    with _frozen_lock:
        if real not in _frozen:
            copy = f'/tmp/tttt-frozen-{os.getpid()}-{len(_frozen)}'
            shutil.copy2(real, copy + '.tmp')
            os.rename(copy + '.tmp', copy)
            if not _frozen:
                atexit.register(lambda: [os.path.exists(c) and os.remove(c) for c in _frozen.values()])
            _frozen[real] = copy
        return _frozen[real]


def freeze(cmd, cwd):
    """cmd with its tttt builds frozen (a relative path is the run's cwd's, like subprocess resolves it)"""
    def one(a):
        path = os.path.join(cwd or '.', a)
        if os.path.basename(a) == 'tttt' and os.path.basename(os.path.dirname(os.path.abspath(path))) in BUILD_DIRS \
                and os.path.isfile(path):
            return frozen(path)
        return a
    return [one(a) for a in cmd]


@dataclasses.dataclass
class Run:
    out: bytes
    err: bytes  # None when stderr went to out
    code: int  # None after a timeout
    status: str
    wall: float

    def text(self):
        return self.out.decode(errors='replace')


def resource_status(status):
    return status in ('timeout', 'killed', 'memory')


def run(cmd, cwd, timeout=None, mem_gb=8, threads=None, env=None, stderr=subprocess.STDOUT, ok_codes=OK_CODES,
        log_errors=False, stdin=None, budget_gb=None):
    """Runs cmd (stdout piped, stderr merged into it unless stderr=PIPE), the address space limited to
    limit_bytes(mem_gb, threads) (threads: from cmd's --threads when not given; mem_gb None: no limit); with
    budget_gb, once that much of the process-wide memory budget is free (the module's comment)"""
    limit = limit_bytes(mem_gb, threads or threads_of(cmd)) if mem_gb else None
    cmd = freeze(cmd, cwd)
    budget = int(budget_gb * (1 << 30)) if budget_gb else 0
    if budget:
        _budget.acquire(budget)
    try:
        return _run(cmd, cwd, timeout, limit, env, stderr, ok_codes, log_errors, stdin)
    finally:
        if budget:
            _budget.release(budget)


def _run(cmd, cwd, timeout, limit, env, stderr, ok_codes, log_errors, stdin):
    start = time.time()
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=stderr, timeout=timeout, env=env, input=stdin,
                           preexec_fn=(lambda: resource.setrlimit(resource.RLIMIT_AS, (limit, limit))) if limit else None)
        out, err, code = p.stdout, p.stderr, p.returncode
        status = status_of(code, out + (err or b''), ok_codes, log_errors)
    except subprocess.TimeoutExpired as e:
        out, err, code, status = e.stdout or b'', e.stderr if stderr == subprocess.PIPE else None, None, 'timeout'
    if isinstance(out, str):
        out = out.encode()
    return Run(out, err, code, status, time.time() - start)


def status_of(code, output=b'', ok_codes=OK_CODES, log_errors=False):
    """The verdict of an exit code and the process's output (bytes or str)"""
    text = output.decode(errors='replace') if isinstance(output, bytes) else output
    if code in ok_codes:
        return 'log_error' if log_errors and LOG_ERROR.search(text) else 'ok'
    if MEMORY_TEXT.search(text[-4000:]):
        return 'memory'
    if code == -signal.SIGKILL:
        return 'killed'
    return f'exit {code}' if code > 0 else f'signal {-code}'
