#!/usr/bin/env python3
"""Regenerates regex_tables.c from tsc's Unicode property tables for regular expressions (the ones after
utf16EncodeAsString in thirdparty/tsc/src/compiler/scanner.ts). Run from the repository root: python3 src/regex_tables.py"""
import re
src = open('thirdparty/tsc/src/compiler/scanner.ts').read()

def names(pattern):
    m = re.search(pattern, src, re.M)
    return re.findall(r'"([^"]*)"', m.group(1))

tables = [
    ('regex_binary_properties', names(r'^const binaryUnicodeProperties = new Set\(\[([^\]]*)\]\);')),
    ('regex_binary_properties_of_strings', names(r'^const binaryUnicodePropertiesOfStrings = new Set\(\[([^\]]*)\]\);')),
    ('regex_general_category_values', names(r'^    General_Category: new Set\(\[([^\]]*)\]\),')),
    ('regex_script_values', names(r'^    Script: new Set\(\[([^\]]*)\]\),')),
]
lines = ["// Generated from tsc's Unicode property tables for regular expressions (src/compiler/scanner.ts) by",
         '// regex_tables.py, in tsc\'s order (which spelling suggestions depend on). Regenerate rather than edit',
         '#include "regex_check.h"', '']
for name, values in tables:
    lines.append('csview const %s[] = {' % name)
    for v in values:
        lines.append('    { "%s", %d },' % (v, len(v)))
    lines.append('};')
    lines.append('isize const %s_count = %d;' % (name, len(values)))
    lines.append('')
open('src/regex_tables.c', 'w').write('\n'.join(lines))
