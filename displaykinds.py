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

# --fine: split the coarse catch-alls (other, we-any, tsc-any, line-extra) by the annotated node and the shape of
# the difference, for plan 35 (tttt computes another type and no diagnostic shows it)
COARSE = {'other', 'we-any', 'tsc-any', 'line-extra'}
LIT_NODE = re.compile(r'^(-?\d[\w.]*|"[^"]*"|\'[^\']*\'|`[^`]*`|true|false|null|undefined|/.*/\w*)$')

def node_kind(lhs, source):
    e = lhs[1:].strip()
    if e == 'this': return 'this'
    if e.startswith('super'): return 'super'
    if LIT_NODE.match(e): return 'literal'
    if re.match(r'^(async\s+)?function\b|^(async\s+)?(\(.*?\)|' + IDENT + r')\s*(:[^=]*)?=>', e): return 'function'
    if e.startswith('class') : return 'class'
    if e.startswith('new '): return 'new'
    if e.startswith('{'): return 'object'
    if e.startswith('['): return 'array'
    if e.startswith('<') or ' as ' in e or ' satisfies ' in e: return 'assertion'
    if e.startswith('await ') or e.startswith('yield'): return 'await/yield'
    if re.fullmatch(IDENT, e):
        n = re.escape(e)
        if re.search(r'\b(const|let|var|function\*?|class|interface|type|enum|namespace|module|import|as)\s+' + n + r'\b', source) \
           or re.search(r'[(,]\s*(\.\.\.)?' + n + r'\s*[?:=,)]', source) or re.search(r'^\s*(\w+\s+)*' + n + r'\s*[?!]?\s*[(:=;]', source) \
           or re.search(r'\{[^}]*\b' + n + r'\b[^}]*\}\s*[=:]', source):
            return 'declaration'
        return 'identifier'
    if re.fullmatch(r'[\w$.?!]+', e) and '.' in e: return 'property-access'
    if e.endswith(')') and '(' in e and not e.startswith('('): return 'call'
    if e.endswith(']') and '[' in e: return 'element-access'
    if e.startswith('('): return 'parenthesized'
    if e.endswith('`'): return 'template'
    if re.search(r' [=!]==? | [-+*/%&|^]=? |&&|\|\||\?\?| \? | instanceof | in ', e): return 'binary/conditional'
    return 'expression-other'

def split_union(t):
    parts, depth, cur = [], 0, ''
    for c in t:
        if c in '<({[': depth += 1
        elif c in '>)}]': depth -= 1
        if c == '|' and depth == 0: parts.append(cur.strip()); cur = ''
        else: cur += c
    parts.append(cur.strip())
    return [p for p in parts if p]

HEAD = re.compile(r'^(' + IDENT + r'(?:\.' + IDENT + r')*)<(.*)>$')
def shape(tsc, ours):
    if ours == 'any': return 'we-any'
    if tsc == 'any': return 'tsc-any'
    if tsc == 'unknown' or ours == 'unknown': return 'unknown-vs-type'
    if tsc == 'never' or ours == 'never': return 'never-vs-type'
    if ours == 'any[]' and tsc.endswith('[]'): return 'we-any[]'
    if tsc == 'undefined[]' or tsc == 'never[]': return 'tsc-' + tsc
    tu, ou = split_union(tsc), split_union(ours)
    if len(tu) > 1 or len(ou) > 1:
        if set(ou) < set(tu): return 'union-fewer-members'
        if set(tu) < set(ou): return 'union-more-members'
        return 'union-differs'
    th, oh = HEAD.match(tsc), HEAD.match(ours)
    if th and oh and th.group(1) == oh.group(1): return 'type-args-differ'
    if tsc.startswith('typeof ') and ours.startswith('typeof '): return 'typeof-other-name'
    if re.fullmatch(r'[A-Z]\w{0,2}', tsc) and not re.fullmatch(r'[A-Z]\w{0,2}', ours): return 'tsc-type-param'
    if re.fullmatch(r'[A-Z]\w{0,2}', ours) and not re.fullmatch(r'[A-Z]\w{0,2}', tsc): return 'our-type-param'
    if re.search(r'\bany\b', ours) and not re.search(r'\bany\b', tsc): return 'nested-we-any'
    if re.search(r'\bany\b', tsc) and not re.search(r'\bany\b', ours): return 'nested-tsc-any'
    if '=>' in tsc and '=>' in ours: return 'signature-differs'
    if tsc.startswith('{') and ours.startswith('{'): return 'object-differs'
    return 'shape-other'

def fine_kind(kind, lhs, source, tsc, ours):
    if kind not in COARSE: return kind
    if kind == 'line-extra': return 'line-extra/' + node_kind(lhs, source)
    return shape(tsc, ours) + '/' + node_kind(lhs, source)
