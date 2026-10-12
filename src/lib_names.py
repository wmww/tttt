#!/usr/bin/env python3
"""Regenerates lib_names.c from tsgo's LibMap and option declarations
(thirdparty/typescript/tsc/internal/tsoptions/declarations_generated.go).
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
def option_names(var):
    s = src.index('var %s = ' % var)
    return re.findall(r'^\t\tName:\s+"([^"]*)"', src[s:src.index('\n}\n', s)], re.M)
common, compiler, build = option_names('commonOptionsWithBuild'), option_names('optionsForCompiler'), option_names('OptionsForBuild')
def name_list(var, names):
    out = ['char const* const %s[] = {' % var]
    line = '   '
    for n in names:
        if len(line) + len(n) + 4 > 120:
            out.append(line)
            line = '   '
        line += ' "%s",' % n
    return out + [line, '};', 'u32 const %s_count = %d;' % (var, len(names)), '']
lines += name_list('tsc_option_names', common + compiler)
lines += ['u32 const tsc_common_options_with_build = %d;' % len(common), '']
lines += name_list('tsc_build_only_option_names', ['build'] + build)
# Each option's kind (tsgo's CommandLineOptionType: b boolean, s string, n number, e enum, l list, L listOrElement,
# o object), in tsc_option_names' order, and the kind of a list option's elements (commandLineOptionElements)
KINDS = {'Boolean': 'b', 'String': 's', 'Number': 'n', 'Enum': 'e', 'List': 'l', 'ListOrElement': 'L', 'Object': 'o'}
def option_kinds(var):
    s = src.index('var %s = ' % var)
    return re.findall(r'^\t\tName:\s+"([^"]*)",\n\t\tKind:\s+CommandLineOptionType(\w+)', src[s:src.index('\n}\n', s)], re.M)
kinds = option_kinds('commonOptionsWithBuild') + option_kinds('optionsForCompiler')
assert [n for n, _ in kinds] == common + compiler
lines += ['char const tsc_option_kinds[] = "%s";' % ''.join(KINDS[k] for _, k in kinds), '']
s = src.index('var commandLineOptionElements = ')
elements = re.findall(r'^\t"(\w+)": \{\n\t\tName:\s+"[^"]*",\n\t\tKind:\s+CommandLineOptionType(\w+)', src[s:src.index('\n}\n', s)], re.M)
lines += ['char const* const tsc_option_elements[][2] = {']
lines += ['    { "%s", "%s" },' % (n, KINDS[k]) for n, k in elements]
lines += ['};', 'u32 const tsc_option_elements_count = %d;' % len(elements), '']
open('src/lib_names.c', 'w').write('\n'.join(lines))
