#!/usr/bin/env python3
"""Regenerates unicode_case_tables.c from tsgo's JavaScript case mapping tables (specialCasingMappings and the Cased /
Case_Ignorable range tables in thirdparty/typescript/tsc/internal/stringutil/js_case_generated.go). Run from the
repository root: python3 src/unicode_case.py"""
import re
src = open('thirdparty/typescript/tsc/internal/stringutil/js_case_generated.go').read()


def go_string(s):
    """The code points of a Go string literal made of \\u/\\U escapes"""
    return [int(h, 16) for h in re.findall(r'\\[uU]([0-9A-Fa-f]+)', s)]


def utf8(cps):
    return ''.join(chr(c) for c in cps).encode('utf-8')


def ranges(name):
    """A Go unicode.RangeTable ({lo, hi, stride} entries) as inclusive pairs of code points"""
    body = src[src.index('var %s = ' % name):]
    out = []
    for lo, hi, stride in re.findall(r'\{0x([0-9A-F]+), 0x([0-9A-F]+), (\d+)\}', body[:body.index('\n}\n')]):
        for c in range(int(lo, 16), int(hi, 16) + 1, int(stride)):
            if out and out[-1][1] == c - 1:
                out[-1][1] = c
            else:
                out.append([c, c])
    return [v for r in out for v in r]


pool = bytearray()
offsets = {}


def intern(b):
    if b not in offsets:
        offsets[b] = len(pool)
        pool.extend(b)
    return offsets[b], len(b)


entries = []
body = src[src.index('var specialCasingMappings'):]
body = body[:body.index('\n}\n')]
for m in re.finditer(r'0x([0-9A-F]+):\s*\{lower: "([^"]*)", upper: "([^"]*)"(?:, conditionalLower: "([^"]*)")?, '
                     r'condition: specialCasingCondition(\w+)\}', body):
    cp = int(m.group(1), 16)
    lower = intern(utf8(go_string(m.group(2))))
    upper = intern(utf8(go_string(m.group(3))))
    cond = intern(utf8(go_string(m.group(4) or '')))
    entries.append((cp, lower, upper, cond, 1 if m.group(5) == 'FinalSigma' else 0))
assert len(entries) == body.count('condition:'), 'unparsed entries'
entries.sort()

lines = ["// Generated from tsgo's js_case_generated.go (internal/stringutil, Unicode 15.1) by unicode_case.py: JavaScript's",
         "// default case mappings and the Cased / Case_Ignorable code points for Final_Sigma. Regenerate rather than edit",
         '#include "unicode_case.h"', '']
lines.append('static char const case_pool[] = { // UTF-8')
for i in range(0, len(pool), 16):
    lines.append('    ' + ', '.join('(char)0x%02x' % b for b in pool[i:i + 16]) + ',')
lines.append('};')
lines.append('')
lines.append('CaseMapping const unicode_case_mappings[] = {')
for cp, (lo, ll), (uo, ul), (co, cl), sigma in entries:
    lines.append('    { %d, %d, %d, %d, %d, %d, %d, %d },' % (cp, lo, ll, uo, ul, co, cl, sigma))
lines.append('};')
lines.append('u32 const unicode_case_mappings_count = %d;' % len(entries))
lines.append('char const* const unicode_case_pool = case_pool;')
lines.append('')
for name, cname in [('unicodeCasedRanges', 'unicode_cased'), ('unicodeCaseIgnorableRanges', 'unicode_case_ignorable')]:
    values = ranges(name)
    lines.append('u32 const %s_ranges[] = {' % cname)
    for i in range(0, len(values), 16):
        lines.append('    ' + ', '.join(str(v) for v in values[i:i + 16]) + ',')
    lines.append('};')
    lines.append('u32 const %s_ranges_len = %d;' % (cname, len(values)))
    lines.append('')
open('src/unicode_case_tables.c', 'w').write('\n'.join(lines))
