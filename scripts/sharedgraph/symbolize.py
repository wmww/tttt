#!/usr/bin/env python3
"""symbolize.py BIN [LOG]: a log with glibc backtrace_symbols_fd() frames (`BIN(+0xOFFSET)[0x...]`, the fingerprint
oracle's) with each frame replaced by `    function@file:line` (addr2line -f -i -s; inlined frames on the same line,
innermost first). Reads stdin without LOG."""
import re, subprocess, sys
binary = sys.argv[1]
text = open(sys.argv[2], errors='replace').read() if len(sys.argv) > 2 else sys.stdin.read()
frame = re.compile(r'^\S*\(\+(0x[0-9a-f]+)\) ?\[0x[0-9a-f]+\]$')
lines = text.split('\n')
offsets = sorted({m.group(1) for l in lines for m in [frame.match(l)] if m})
names = {}
if offsets:
    # (A return address points after the call: one byte back is the call's line)
    out = subprocess.run(['addr2line', '-f', '-i', '-s', '-a', '-e', binary, *[hex(int(o, 16) - 1) for o in offsets]],
                         capture_output=True, text=True).stdout.split('\n')
    cur, parts, i = None, [], 0
    for l in out:
        if re.match(r'^0x[0-9a-f]+$', l):
            if cur is not None: names[cur] = ' < '.join(parts)
            cur, parts = hex(int(l, 16) + 1), []
        elif l:
            parts.append(l)
    if cur is not None: names[cur] = ' < '.join(parts)
    names = {k: re.sub(r'(\S+) < (\S+:\d+)( \(discriminator \d+\))?', r'\1@\2', v) for k, v in names.items()}
for l in lines:
    m = frame.match(l)
    print('    ' + names.get(hex(int(m.group(1), 16)), l) if m else l)
