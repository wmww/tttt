#!/usr/bin/env python3
"""Census of how C structs are used across src/, from clang's AST (exact, not grep):
every read/write/address-of of each field, pointer equality comparisons of `T *`, and `T *` cast to an integer (hash
or intern keys), by file and by function. Read-only: clang -fsyntax-only on each src/**/*.c, nothing built.

  scripts/fieldcensus.py Prop [PropDecl ...]          per-field counts, identity compares, key casts
  scripts/fieldcensus.py Prop -F decl [-v]            one field's sites by function (-v: every site)
  scripts/fieldcensus.py Prop --eq [-v]               the T* == / != sites by function
  scripts/fieldcensus.py Prop --keys [-v]             the T* -> integer casts by function
Written for the type graph design (agents/notes/type_graph_design.md)."""
import argparse, collections, concurrent.futures, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLAGS = ['-fsyntax-only', '-I.', '-Isrc', '-Ithirdparty/cleat/include', '-Ithirdparty/cleat/stc/include',
         '-D_FILE_OFFSET_BITS=64', '-std=c2x', '-pthread', '-Wno-everything', '-fno-color-diagnostics',
         '-Xclang', '-ast-dump']
LOC = re.compile(r'(?:([\w./+-]+\.(?:c|h|inc)):(\d+):(\d+)|line:(\d+):(\d+)|col:(\d+))')
NODE = re.compile(r'^([| `-]*)(\w+)')

def parse(path, structs):
    out = subprocess.run(['clang', *FLAGS, path], cwd=ROOT, capture_output=True, text=True, errors='replace').stdout
    fields = {}  # FieldDecl addr -> (struct, field)
    events = []  # (kind, struct, field_or_'', file, line, col, func, parent_kind, extra)
    f, l, c = '', 0, 0
    stack = []  # (depth, kind, line_text, pos)
    record = None; record_depth = -1
    func = ''
    binops = {}
    for text in out.splitlines():
        m = NODE.match(text)
        if not m: continue
        depth = len(m.group(1)) // 2
        kind = m.group(2)
        first = None
        for g in LOC.finditer(re.sub(r"'[^']*'", '', text)):
            if g.group(1): f, l, c = g.group(1), int(g.group(2)), int(g.group(3))
            elif g.group(4): l, c = int(g.group(4)), int(g.group(5))
            else: c = int(g.group(6))
            if first is None: first = (f, l, c)
        if first is None: first = (f, l, c)
        while stack and stack[-1][0] >= depth: stack.pop()
        parent = stack[-1] if stack else None
        if depth == 1 and kind == 'FunctionDecl':
            fm = re.search(r"> (?:line:\d+:\d+|col:\d+|[\w./+-]+:\d+:\d+)(?: [a-z]+)* (\w+) '", text)
            func = fm.group(1) if fm else '?'
        if record and depth <= record_depth: record = None
        if kind == 'RecordDecl' and not record:
            rm = re.search(r'struct (\w+) definition', text)
            record = rm.group(1) if rm and rm.group(1) in structs else None
            record_depth = depth
        elif kind == 'FieldDecl' and record and depth > record_depth:
            fm = re.match(r'.*?FieldDecl (0x[0-9a-f]+) .*? (\w+) \'', text)
            if fm: fields[fm.group(1)] = (record, fm.group(2))
        loc = first
        insrc = loc[0].startswith(('src/', './src/'))
        if kind == 'MemberExpr' and insrc:
            mm = re.search(r'(?:->|\.)(\w+) (0x[0-9a-f]+)', text)
            if mm and mm.group(2) in fields:
                st, fd = fields[mm.group(2)]
                role = 'read'
                if parent:
                    pk, ptext = parent[1], parent[2]
                    if pk == 'BinaryOperator' and "'='" in ptext and parent[3] == 0: role = 'write'
                    elif pk == 'CompoundAssignOperator' and parent[3] == 0: role = 'write'
                    elif pk == 'UnaryOperator' and "'&'" in ptext: role = 'addr'
                    elif pk == 'UnaryOperator' and ("'++'" in ptext or "'--'" in ptext): role = 'write'
                events.append(('field', st, fd, loc[0], loc[1], loc[2], func, role))
        if parent and parent[1] == 'BinaryOperator' and re.search(r"'(==|!=)'", parent[2]) and insrc:
            tm = re.search(r"'((?:const )?(\w+) \*)'", text)
            pl = parent[4]
            ops = binops.setdefault(pl, [func])
            ops.append(tm.group(2) if tm and tm.group(2) in structs and '<NullToPointer>' not in text else None)
        if parent and parent[1] == 'CStyleCastExpr' and re.search(r"'(uintptr_t|u64|uint64_t|unsigned long)'", parent[2]) and insrc:
            tm = re.search(r"'(?:const )?(\w+) \*'", text)
            if tm and tm.group(1) in structs:
                pl = parent[4]
                events.append(('key', tm.group(1), '', pl[0], pl[1], pl[2], func, ''))
        index = 0
        if parent is not None:
            index = parent[5]
            parent[5] += 1
        stack.append([depth, kind, text, index, loc, 0])
    for pl, ops in binops.items():
        if len(ops) == 3 and ops[1] and ops[1] == ops[2]:
            events.append(('eq', ops[1], '', pl[0], pl[1], pl[2], ops[0], ''))
    return events

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('structs', nargs='+')
    ap.add_argument('-F', '--field')
    ap.add_argument('--eq', action='store_true')
    ap.add_argument('--keys', action='store_true')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    files = []
    for d, _, fs in os.walk(os.path.join(ROOT, 'src')):
        files += [os.path.relpath(os.path.join(d, x), ROOT) for x in fs if x.endswith('.c')]
    structs = set(a.structs)
    # (clang's text dump attributes some locations to the last file printed: a function's own file is where it's defined)
    deffile = {}
    for d, _, fs in os.walk(os.path.join(ROOT, 'src')):
        for x in fs:
            if not x.endswith(('.c', '.h', '.inc')): continue
            path = os.path.join(d, x)
            for line in open(path, errors='replace'):
                m = re.match(r'^[A-Za-z_][\w\s\*]*?\b(\w+)\(.*[^;]\s*$', line)
                if m and not line.startswith(('if', 'for', 'while', 'return', 'typedef', '#')): deffile.setdefault(m.group(1), os.path.relpath(path, os.path.join(ROOT, 'src')))
    seen = set(); ev = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        for res in ex.map(lambda p: parse(p, structs), sorted(files)):
            for e in res:
                k = (e[0], e[1], e[2], e[3].lstrip('./'), e[4], e[5])
                if k in seen: continue
                seen.add(k)
                e = list(e); e[3] = 'src/' + deffile.get(e[6], e[3].lstrip('./').replace('src/', '')); ev.append(tuple(e))
    def show(sel, title):
        byfunc = collections.Counter((e[3].lstrip('./').replace('src/', ''), e[6]) for e in sel)
        byfile = collections.Counter(e[3].lstrip('./').replace('src/', '') for e in sel)
        print(f'{title}: {len(sel)} sites in {len(byfunc)} functions; by file: ' + ', '.join(f'{f} {n}' for f, n in byfile.most_common()))
        for (f, fn), n in byfunc.most_common(None if a.v else 40):
            print(f'  {n:4} {f} {fn}')
        if a.v:
            for e in sorted(sel, key=lambda e: (e[3], e[4])): print(f'    {e[3]}:{e[4]}:{e[5]} {e[6]} {e[7]}')
    if a.field:
        show([e for e in ev if e[0] == 'field' and e[2] == a.field], f'{a.field}')
        return
    if a.eq:
        show([e for e in ev if e[0] == 'eq'], 'pointer equality'); return
    if a.keys:
        show([e for e in ev if e[0] == 'key'], 'pointer as integer'); return
    for st in a.structs:
        fe = [e for e in ev if e[0] == 'field' and e[1] == st]
        cnt = collections.Counter((e[2], e[7]) for e in fe)
        names = sorted({e[2] for e in fe}, key=lambda n: -sum(v for (k, r), v in cnt.items() if k == n))
        print(f'struct {st}: {len(fe)} field accesses')
        for n in names:
            fns = len({(e[3], e[6]) for e in fe if e[2] == n})
            print(f'  {n:22} read {cnt[(n, "read")]:5} write {cnt[(n, "write")]:4} addr {cnt[(n, "addr")]:4}   in {fns} functions')
        eq = [e for e in ev if e[0] == 'eq' and e[1] == st]
        ky = [e for e in ev if e[0] == 'key' and e[1] == st]
        print(f'  {st} * == / !=: {len(eq)} sites in {len({(e[3], e[6]) for e in eq})} functions; {st} * cast to an integer (keys, hashes): {len(ky)} sites in {len({(e[3], e[6]) for e in ky})} functions')

main()
