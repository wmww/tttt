#!/usr/bin/env python3
# The exit tail of a command: start -> last output byte -> exit, per run, with its sys, user, minor faults and peak
# RSS (wait4's rusage). The tail is the time between tttt's last diagnostic and its exit status, which is what the
# detached reaper (main.c exit_detached()) hides: compare a default run against TTTT_SYNC_EXIT=1.
#
#   scripts/exittail.py N cmd...     e.g. scripts/exittail.py 5 fast/tttt --noEmit -p thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json
#   (a project with no output has no last byte: its "tail" is the whole run)
import os, sys, time, select, subprocess, resource
n = int(sys.argv[1]); cmd = sys.argv[2:]
rows = []
for _ in range(n):
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    fds = {p.stdout.fileno(): 'o', p.stderr.fileno(): 'e'}
    last = t0; nbytes = 0
    while fds:
        r, _, _ = select.select(list(fds), [], [])
        for fd in r:
            b = os.read(fd, 1 << 20)
            if not b: del fds[fd]
            else: last = time.monotonic(); nbytes += len(b)
    teof = time.monotonic()
    _, status, ru = os.wait4(p.pid, 0)
    tend = time.monotonic()
    rows.append((tend - t0, last - t0, tend - last, teof - last, ru.ru_stime, ru.ru_utime, ru.ru_minflt, ru.ru_maxrss >> 10))
for r in rows:
    print("wall %.3f last-byte %.3f tail %.1f ms (eof %.1f) sys %.2f user %.2f minflt %d rss %d MB" % (r[0], r[1], r[2]*1000, r[3]*1000, r[4], r[5], r[6], r[7]))
tails = sorted(r[2] for r in rows); walls = sorted(r[0] for r in rows)
print("median tail %.1f ms of wall %.3f (%.1f%%)" % (tails[len(tails)//2]*1000, walls[len(walls)//2], 100*tails[len(tails)//2]/walls[len(walls)//2]))
