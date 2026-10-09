#!/usr/bin/env python3
"""Regenerates unicode_identifiers.c from tsgo's identifier tables (Go range tables in
thirdparty/typescript/tsc/internal/stringutil/identifier_parts_generated.go). Run from the repository root:
python3 src/unicode_identifiers.py"""
import re
src = open('thirdparty/typescript/tsc/internal/stringutil/identifier_parts_generated.go').read()


def ranges(name):
    """A Go unicode.RangeTable ({lo, hi, stride} entries) as inclusive pairs of code points"""
    body = src[src.index('var unicode%s = ' % name):]
    out = []
    for lo, hi, stride in re.findall(r'\{0x([0-9A-F]+), 0x([0-9A-F]+), (\d+)\}', body[:body.index('\n}\n')]):
        for c in range(int(lo, 16), int(hi, 16) + 1, int(stride)):
            if out and out[-1][1] == c - 1:
                out[-1][1] = c
            else:
                out.append([c, c])
    return [v for r in out for v in r]


tables = {name: ranges(name) for name in ['ESNextIdentifierStart', 'ESNextIdentifierPart']}
lines = ["// Generated from tsgo's unicodeESNextIdentifier* tables (internal/stringutil) by unicode_identifiers.py: the",
         "// code point ranges (inclusive pairs) that can start or continue an identifier. Regenerate rather than edit",
         '#include "unicode_identifiers.h"', '']
for name, values in tables.items():
    lines.append('static u32 const %s[] = {' % name)
    for i in range(0, len(values), 16):
        lines.append('    ' + ', '.join(str(v) for v in values[i:i + 16]) + ',')
    lines.append('};')
    lines.append('')
lines.append('''// tsc's lookupInUnicodeMap (tsgo: unicode.Is)
static bool in_map(u32 code, u32 const* map, u32 len) {
    if (code < map[0]) return false;
    u32 lo = 0, hi = len / 2; // Over the ranges
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (code < map[mid * 2]) hi = mid;
        else if (code > map[mid * 2 + 1]) lo = mid + 1;
        else return true;
    }
    return false;
}

#define IN_MAP(code, map) in_map(code, map, sizeof(map) / sizeof(map[0]))

bool unicode_is_identifier_start(u32 code) {
    return IN_MAP(code, ESNextIdentifierStart);
}

bool unicode_is_identifier_part(u32 code) {
    return IN_MAP(code, ESNextIdentifierPart);
}''')
open('src/unicode_identifiers.c', 'w').write('\n'.join(lines) + '\n')
