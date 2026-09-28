#!/usr/bin/env python3
"""Regenerates unicode_identifiers.c from tsc's identifier tables (thirdparty/tsc/src/compiler/scanner.ts).
Run from the repository root: python3 src/unicode_identifiers.py"""
import re
src = open('thirdparty/tsc/src/compiler/scanner.ts').read()
tables = {}
for name in ['ES5IdentifierStart', 'ES5IdentifierPart', 'ESNextIdentifierStart', 'ESNextIdentifierPart']:
    m = re.search(r'^const unicode%s = \[([^\]]*)\];' % name, src, re.M)
    tables[name] = [int(x) for x in m.group(1).split(',')]
lines = ["// Generated from tsc's unicode*Identifier* tables (src/compiler/scanner.ts) by unicode_identifiers.py: the",
         "// code point ranges (inclusive pairs) that can start or continue an identifier. Regenerate rather than edit",
         '#include "unicode_identifiers.h"', '']
for name, values in tables.items():
    lines.append('static u32 const %s[] = {' % name)
    for i in range(0, len(values), 16):
        lines.append('    ' + ', '.join(str(v) for v in values[i:i + 16]) + ',')
    lines.append('};')
    lines.append('')
lines.append('''// tsc's lookupInUnicodeMap
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

bool unicode_is_identifier_start(u32 code, bool esnext) {
    return esnext ? IN_MAP(code, ESNextIdentifierStart) : IN_MAP(code, ES5IdentifierStart);
}

bool unicode_is_identifier_part(u32 code, bool esnext) {
    return esnext ? IN_MAP(code, ESNextIdentifierPart) : IN_MAP(code, ES5IdentifierPart);
}''')
open('src/unicode_identifiers.c', 'w').write('\n'.join(lines) + '\n')
