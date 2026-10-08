import json, subprocess, sys, time
projects = open(sys.argv[1]).read().split()
out = open(sys.argv[2], 'w')
jobs = 4
procs = []
for i in range(jobs):
    p = subprocess.Popen(['node', '--max-old-space-size=8192', '/home/ai/tttt/test/sweep_tsc.js'], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, text=True)
    p.stdin.write('\n'.join(projects[i::jobs]) + '\n'); p.stdin.close(); procs.append(p)
import threading
def pump(p):
    for line in p.stdout: out.write(line); out.flush()
ts = [threading.Thread(target=pump, args=(p,)) for p in procs]
[t.start() for t in ts]; [t.join() for t in ts]
print([p.wait() for p in procs])
