#!/usr/bin/env python3
"""Regenerates regex_tables.c from tsgo's Unicode property tables for regular expressions
(thirdparty/typescript/tsc/internal/scanner/unicodeproperties.go). Run from the repository root:
python3 src/regex_tables.py"""
import re
src = open('thirdparty/typescript/tsc/internal/scanner/unicodeproperties.go').read()


def names(start):
    """The strings of the collections.NewSetFromItems(...) call that follows `start`, in source order"""
    body = src[src.index(start) + len(start):]
    return re.findall(r'"([^"]*)"', body[:body.index(')')])


tables = [
    ('regex_binary_properties', names('var binaryUnicodeProperties = ')),
    ('regex_binary_properties_of_strings', names('var binaryUnicodePropertiesOfStrings = ')),
    ('regex_general_category_values', names('"General_Category": collections.NewSetFromItems(')),
    ('regex_script_values', names('var scriptValues = ')),
]
lines = ["// Generated from tsgo's Unicode property tables for regular expressions (internal/scanner/unicodeproperties.go)",
         "// by regex_tables.py, in tsgo's order (which spelling suggestions depend on). Regenerate rather than edit",
         '#include "regex_check.h"', '']
for name, values in tables:
    lines.append('csview const %s[] = {' % name)
    for v in values:
        lines.append('    { "%s", %d },' % (v, len(v)))
    lines.append('};')
    lines.append('isize const %s_count = %d;' % (name, len(values)))
    lines.append('')
open('src/regex_tables.c', 'w').write('\n'.join(lines))
