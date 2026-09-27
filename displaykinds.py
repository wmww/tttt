"""Heuristic mechanisms for a mismatch between how tsc and tttt print a type, shared by ./displaydiff.py and
./errtextdiff.py"""
import re

IDENT = r'[A-Za-z_$][\w$]*'
LITERAL = re.compile(r'^("[^"]*"|-?\d[\d.e+-]*|true|false)(\s*\|\s*("[^"]*"|-?\d[\d.e+-]*|true|false))*$')
PRIMITIVE = {'string', 'number', 'boolean', 'bigint'}

def type_kinds(tsc, ours):
    ks = []
    if sorted(re.split(r'[\s|]+', tsc)) == sorted(re.split(r'[\s|]+', ours)): return ['union-order']
    if ours == 'any': return ['we-any']
    if tsc == 'any': return ['tsc-any']
    if 'import("' in ours and 'import("' not in tsc: ks.append('import()-qualification')
    if 'import("' in tsc and 'import("' not in ours: ks.append('tsc-import()')
    if re.search(r'\w_\d\b', tsc) and not re.search(r'\w_\d\b', ours): ks.append('T_1-rename')
    if re.search(r'\bArray<', tsc) and not re.search(r'\bArray<', ours): ks.append('Array<>-reuse')
    if 'typeof ' in tsc and 'typeof ' not in ours: ks.append('tsc-typeof')
    if 'typeof ' in ours and 'typeof ' not in tsc: ks.append('our-typeof')
    if 'prototype: ;' in ours: ks.append('class-expression')
    if tsc.replace(' | undefined', '') == ours.replace(' | undefined', ''): ks.append('undefined')
    if LITERAL.match(tsc) and ours in PRIMITIVE: ks.append('literal-widened')
    if LITERAL.match(ours) and tsc in PRIMITIVE: ks.append('literal-kept')
    if '...' in tsc and '...' not in ours: ks.append('truncation')
    if re.fullmatch(IDENT + r'(\.' + IDENT + r')*(<.*>)?', tsc) and ours.startswith(('{', '(', '[')): ks.append('alias-or-name-lost')
    if re.fullmatch(IDENT + r'(\.' + IDENT + r')*(<.*>)?', ours) and tsc.startswith(('{', '(', '[')): ks.append('structure-lost')
    if tsc.startswith('{ (') and not ours.startswith('{ ('): ks.append('overloads')
    if re.sub(r'\b' + IDENT + r'\??:', 'P:', tsc) == re.sub(r'\b' + IDENT + r'\??:', 'P:', ours): ks.append('names-only')
    if tsc.replace('readonly ', '') == ours.replace('readonly ', ''): ks.append('readonly')
    return ks or ['other']
