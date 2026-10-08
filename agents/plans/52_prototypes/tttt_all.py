import json, sys, concurrent.futures
sys.path.insert(0, '/home/ai/tttt')
import sweep
projects = open(sys.argv[1]).read().split()
import os; os.chdir('/home/ai/ss/repos')
import subprocess
orig = subprocess.run
def run(args, cwd=None, **kw): return orig(args, cwd='/home/ai/ss/repos', **kw)
sweep.subprocess.run = run
with concurrent.futures.ThreadPoolExecutor(4) as ex:
    res = list(ex.map(lambda p: sweep.run_tttt(p, 120, 8), projects))
json.dump(res, open(sys.argv[2], 'w'))
