#!/usr/bin/env python3
"""tsan.py LOG... [--brief]: ThreadSanitizer reports grouped by the unordered pair (access, previous access), each site the innermost
frame outside TSan's own (function@file:line). One line per group: count, kind, site <-prev- previous site; then the
first report of each group's two stacks (top 6 frames) unless --brief. With several logs, the count is summed and the
logs a group occurs in are listed (by basename)."""
import os, re, sys

FRAME = re.compile(r'#(\d+) (\S+) (\S+)')

def frames(block):
    out = []
    for m in FRAME.finditer(block):
        fn, loc = m.group(2), m.group(3)
        if loc.startswith('(') or '/sanitizer_common/' in loc or '/tsan/' in loc:
            continue
        out.append(fn + '@' + loc.split('/')[-1].rsplit(':', 1)[0] if loc.count(':') >= 2 else fn + '@' + loc.split('/')[-1])
    return out

def parse(text):
    for r in text.split('WARNING: ThreadSanitizer: ')[1:]:
        r = r.split('SUMMARY: ThreadSanitizer')[0]
        kind = r.split('(')[0].split('\n')[0].strip()
        pv = r.find('Previous ')
        if pv < 0:
            pv = r.find('Mutex ')
        cur = frames(r[:pv] if pv >= 0 else r)
        prev = frames(r[pv:].split('\n\n')[0]) if pv >= 0 else []
        yield kind, cur, prev, r

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    brief = '--brief' in sys.argv
    groups = {}
    for path in args:
        for kind, cur, prev, r in parse(open(path, errors='replace').read()):
            a, b = cur[0] if cur else '?', prev[0] if prev else '?'
            if b < a:  # one race whichever side TSan saw second
                a, b, cur, prev = b, a, prev, cur
            k = (kind, a, b)
            g = groups.setdefault(k, [0, set(), cur, prev])
            g[0] += 1
            g[1].add(os.path.basename(path))
    for (kind, a, b), (n, logs, cur, prev) in sorted(groups.items(), key=lambda x: -x[1][0]):
        where = ' [' + ' '.join(sorted(logs)) + ']' if len(args) > 1 else ''
        print(f'{n} {kind}: {a} <-prev- {b}{where}')
        if not brief:
            print('    now:  ' + ' < '.join(cur[:6]))
            print('    prev: ' + ' < '.join(prev[:6]))
    if not groups:
        print('clean')

main()
