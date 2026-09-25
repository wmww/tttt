#!/usr/bin/env python3
"""Regenerates lib_features.c from tsc's getScriptTargetFeatures (thirdparty/tsc/src/compiler/utilities.ts).
Run from the repository root: python3 src/lib_features.py"""
import re
src = open('thirdparty/tsc/src/compiler/utilities.ts').read()
start = src.index('export const getScriptTargetFeatures')
txt = src[start:]
out = []
for m in re.finditer(r'^        (\w+): new Map\(Object\.entries\(\{(.*?)^        \}\)\),', txt, re.M | re.S):
    t, body = m.group(1), m.group(2)
    for lm in re.finditer(r'^            (\w+): (\[(.*?)\]|emptyArray)', body, re.M | re.S):
        props = re.findall(r'"([^"]*)"', lm.group(3) or '')
        out.append((t, lm.group(1), props))
lines = ["// Generated from tsc's getScriptTargetFeatures (src/compiler/utilities.ts) by lib_features.py: which lib adds",
         "// which member of which global type. Regenerate rather than edit",
         '#include "lib_features.h"', '', 'static LibFeature const lib_features[] = {']
for t, lib, props in out:
    members = '(char const* const[]){ %s }' % ', '.join('"%s"' % p for p in props) if props else 'nullptr'
    lines.append('    { "%s", "%s", %s, %d },' % (t, lib, members, len(props)))
lines.append('};')
lines.append('''
char const* lib_feature_of_global(csview name) {
    for (u32 i = 0; i < sizeof(lib_features) / sizeof(lib_features[0]); i++) {
        if (csview_equals(name, lib_features[i].type)) return lib_features[i].lib;
    }
    return nullptr;
}

char const* lib_feature_of_member(csview type, csview member) {
    for (u32 i = 0; i < sizeof(lib_features) / sizeof(lib_features[0]); i++) {
        if (!csview_equals(type, lib_features[i].type)) continue;
        for (u32 j = 0; j < lib_features[i].member_count; j++) {
            if (csview_equals(member, lib_features[i].members[j])) return lib_features[i].lib;
        }
    }
    return nullptr;
}''')
open('src/lib_features.c', 'w').write('\n'.join(lines) + '\n')
