#!/usr/bin/env python3
"""Regenerates lib_names.c from tsgo's LibMap (thirdparty/typescript/tsc/internal/tsoptions/declarations_generated.go).
Run from the repository root: python3 src/lib_names.py"""
import re
src = open('thirdparty/typescript/tsc/internal/tsoptions/declarations_generated.go').read()
start = src.index('var LibMap = ')
body = src[start:src.index('})', start)]
entries = re.findall(r'\{Key: "([^"]*)", Value: "([^"]*)"\}', body)
lines = ["// Generated from tsgo's LibMap (internal/tsoptions/declarations_generated.go) by lib_names.py: the names `lib`",
         "// takes and the files they mean. Regenerate rather than edit",
         '#include "lib_names.h"', '', 'char const* const lib_names[] = {']
lines += ['    "%s",' % k for k, _ in entries]
lines += ['};', 'u32 const lib_name_count = %d;' % len(entries), '',
          'char const* const lib_aliases[][2] = {']
aliases = [(k, v[4:-5]) for k, v in entries if v != 'lib.%s.d.ts' % k]
lines += ['    { "%s", "%s" },' % a for a in aliases]
lines += ['};', 'u32 const lib_alias_count = %d;' % len(aliases), '']
open('src/lib_names.c', 'w').write('\n'.join(lines))
