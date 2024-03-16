import os
import re

symbols = set()

token_re = re.compile(r'([a-zA-Z_]\w+)|(//[^\n]*\n)|(/\*[^*]*\*)|("[^"\n]*")|' + r"('[^'\n]*')", re.DOTALL)

def traverse_dir(d, cb):
    for i in os.listdir(d):
        p = d + '/' + i
        if os.path.isdir(p):
            traverse_dir(p, cb)
        elif p.endswith('.c') or p.endswith('.h'):
            cb(p)

def add_symbols_in_source(p):
    with open(p, 'r') as f:
        text = f.read()
        tokens = [j for i, j in enumerate(token_re.split(text)) if i % 6 == 1 and j is not None]
        for t in tokens:
            symbols.add(t)

traverse_dir('src', add_symbols_in_source)

def sym_is_type(s):
    return bool(re.findall(r'^.*[A-Z][a-z][^A-Z_]*$', s))

def sym_is_allcaps(s):
    return bool(re.findall(r'^[^a-z]+$', s))

def sym_is_function(s):
    return bool(re.findall(r'^(vec_|arc_|box_|hmap_|hmap_(str_)?|vec_(vec_)?)?[^_]*[a-z0-9_]+$', s))

def sym_to_function(s):
    r = ''
    for c in s:
        if c != c.lower():
            if r and r[-1] != '_':
                r += '_'
            r += c.lower()
        else:
            r += c
    return r

def sym_to_type(s):
    r = ''
    caps = True
    for c in s:
        if c == '_':
            caps = True
        elif caps:
            r += c.upper()
            caps = False
        else:
            r += c
    return r

ignore = set(['c_SV', 'c_JOIN'])
replacements = {}
problems = []

for s in symbols:
    if sym_is_allcaps(s) or s in ignore:
        modified = s
    elif sym_is_type(s):
        modified = sym_to_type(s)
    elif sym_is_function(s):
        modified = sym_to_function(s)
    else:
        problems.append(s)
        modified = s
    if modified != s:
        replacements[s] = modified
    else:
        print('not modifying:', s)

for k, v in replacements.items():
    print(k, '=>', v)

if problems:
    print('problem symbols (add to ignore):', problems)
    exit(1)

def replace_symbols_in_source(p):
    with open(p, 'r') as f:
        text = f.read()
    for k, v in replacements.items():
        text = re.sub(r'\b' + k + r'\b', v, text)
    with open(p, 'w') as f:
        f.write(text)

traverse_dir('src', replace_symbols_in_source)
