#!/usr/bin/env python3
"""tsgo against tttt on one project, safely: the (file, line, code) diagnostics each side alone reports, and whether
the whole CLI output is byte-identical.

    scripts/projdiff.py thirdparty/sources/REPO/tsconfig.json     # a tsconfig or its directory
    scripts/projdiff.py DIR --copy                                # on reduce.py's symlink-free scratch copy of DIR
    scripts/projdiff.py P -- --strict                             # extra options for both sides
    scripts/projdiff.py P --redact [--limit N]                    # private code: output safe to paste

tsgo: tttt-oracle (test/oracle: `tsc -p P --noEmit --pretty false --singleThreaded` in process, build info hidden
and never written: a plain `built/tsc -p` on a composite or incremental config writes a .tsbuildinfo into the shared
checkout, and the next run reads it and skips checking). tttt: release/tttt `--noEmit --pretty false` at its
defaults (threads and checkers: what users run) and at `--checkers 1 --threads 1`, inside a bwrap sandbox whose
root is read-only (tttt_run's limits and verdicts: a timeout or memory kill says so). A difference between the two
tttt runs is flagged as K-dependence (a tttt bug whatever tsgo says: scripts/kdiff.py).

--copy first runs `scripts/reduce.py --project DIR --no-reduce` (the program's files copied as regular files into
/tmp/reduce-*, a flattened tsconfig) and diffs there: what a reduction would start from. The copy stays (its path
is printed; `scripts/reduce.py --from COPY` continues on it).

--redact prints nothing from the project: no path, identifier, string literal or line content (package names under
node_modules and lib file names are kept). For each diagnostic only one side reports: the side, the code, tsgo's
message template (diagnosticMessages.json) with each argument reduced to its skeleton (`'{ … }'`, `'… | undefined'`,
`'(2 params) => …'`: keywords and punctuation only) and its chain lines alike; the syntactic shape at the position
(`tttt --node-shapes`: the node kinds from the top-level statement down, e.g. `const > initializer > call > argument 1
> object literal > property value > arrow function`, with flags for JSX, destructuring, explicit type arguments,
as/satisfies, spread, optional chains); for each type argument, where the types it names are declared (found by name
in the program's files: `node_modules/<package>`, `lib.*.d.ts`, `project source` / `project .d.ts`) and the
declaration's shape (interface, class, alias: union of N, intersection of N, branded intersection, mapped,
conditional, function with N params, generic with N params...); the file's extension, size class and whether it
looks like a test. Then the same position and code with other text (the first differing line, both templates), the
counts by code and side, and the 10 most frequent (code, shape) pairs. Everything is computed locally; the names
looked up never reach the output.

Exit 0 when tsgo and tttt (defaults) agree by (file, line, code) and the two tttt runs agree, else 1.
"""
import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tscformat  # noqa: E402
import tttt_run  # noqa: E402

REDACT = False  # --redact: nothing from the project is printed
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORACLE = os.path.join(ROOT, 'thirdparty/typescript/built/tttt-oracle')
TTTT = os.path.join(ROOT, 'release/tttt')


def sandbox():
    return ['bwrap', '--ro-bind', '/', '/', '--bind', '/tmp', '/tmp', '--dev', '/dev', '--proc', '/proc',
            '--die-with-parent', '--'] if shutil.which('bwrap') else []


def tsgo(project, cwd, extra, timeout):
    """(output text, exit status) of tsc -p PROJECT --noEmit as tttt-oracle runs it"""
    p = subprocess.run([ORACLE, '--text', *extra], input=project + '\n', cwd=cwd, capture_output=True, text=True,
                       timeout=timeout)
    line = p.stdout.splitlines()[0] if p.stdout.strip() else ''
    if not line:
        sys.exit('projdiff: tttt-oracle gave nothing' + ('' if REDACT else f' on {project}:\n{p.stderr[-1000:]}'))
    r = json.loads(line)
    return r.get('text', ''), r.get('status')


def tttt(project, cwd, extra, timeout, mem_gb, k1):
    args = ['--checkers', '1', '--threads', '1'] if k1 else []
    r = tttt_run.run([*sandbox(), TTTT, '-p', project, '--noEmit', '--pretty', 'false', *args, *extra], cwd,
                     timeout=timeout, mem_gb=mem_gb, threads=1 if k1 else None, stderr=subprocess.PIPE)
    return re.sub(r'\x1b\[[0-9;]*m', '', r.text()), r.code, r.status, (r.err or b'').decode(errors='replace')


def keys(text):
    return collections.Counter((f, l, c) for f, l, _, c, _ in tscformat.parse(text))


def lines_of(text):
    return {(f, l, c): f'{f}({l}): TS{c} {m[:160]}' for f, l, _, c, m in tscformat.parse(text)}


def show(title, only, text):
    if not only:
        return
    named = lines_of(text)
    print(f'{title} ({sum(only.values())}):')
    for k in sorted(only, key=lambda k: (k[0], k[1], k[2]))[:60]:
        print(f'  {named.get(k, k)}' + (f'  x{only[k]}' if only[k] > 1 else ''))
    if len(only) > 60:
        print(f'  ... {len(only) - 60} more')


# --redact: what the differences are, without the code (for private projects). Nothing from the source is printed: no
# path, identifier, string literal or line content. Messages are tsgo's templates (diagnosticMessages.json) with each
# argument replaced by its skeleton (keywords and punctuation kept, every name `…`); positions by the syntactic shape
# tttt --node-shapes prints there (a fixed vocabulary); the types a message names by where their declarations are
# (a package name, a lib file or "project") and what kind of declaration it is, found by name in the program's files
MESSAGES = os.path.join(ROOT, 'thirdparty/typescript/tsc/internal/diagnostics/diagnosticMessages.json')
KEYWORDS = {'any', 'unknown', 'never', 'void', 'undefined', 'null', 'string', 'number', 'boolean', 'bigint', 'symbol',
            'object', 'true', 'false', 'this', 'readonly', 'keyof', 'typeof', 'unique', 'infer', 'extends', 'new',
            'abstract', 'asserts', 'is', 'in', 'out', 'const', 'Function', 'Object', 'Array', 'ReadonlyArray',
            'Promise', 'PromiseLike', 'Record', 'Partial', 'Required', 'Readonly', 'Pick', 'Omit', 'Exclude', 'Extract',
            'NonNullable', 'ReturnType', 'Parameters', 'Awaited', 'InstanceType', 'Map', 'Set', 'Date', 'RegExp', 'Error',
            'Iterable', 'IterableIterator', 'AsyncIterable', 'Generator', 'AsyncGenerator', 'NoInfer', 'Uppercase',
            'Lowercase', 'Capitalize', 'Uncapitalize', 'ThisType', 'ArrayLike', 'Symbol', 'String', 'Number', 'Boolean',
            'BigInt', 'JSX', 'Element', 'IntrinsicElements'}
IDENT = r'[A-Za-z_$\u0080-￿][\w$\u0080-￿]*'
KNOWN_ARGS = {'any', 'unknown', 'never', 'void', 'undefined', 'null', 'string', 'number', 'boolean', 'bigint', 'symbol',
              'object', 'true', 'false', 'this', 'super', 'Object', 'Function'}


def load_templates():
    """{code: [(compiled regex, template)]} and the templates by their text before the first placeholder (chain lines
    don't print their codes)"""
    by_code, all_ = collections.defaultdict(list), []
    try:
        data = json.load(open(MESSAGES))
    except OSError:
        return by_code, all_
    for text, info in data.items():
        parts = re.split(r'(\{\d+\})', text)
        pattern = ''.join(f'(?P<a{p[1:-1]}_{i}>.*?)' if re.fullmatch(r'\{\d+\}', p) else re.escape(p)
                          for i, p in enumerate(parts))
        entry = (re.compile('^' + pattern + '$', re.S), text)
        by_code[info['code']].append(entry)
        all_.append(entry)
    return by_code, all_


def split_top(text, sep):
    """text split at `sep` outside brackets, quotes and template literals"""
    out, depth, cur, quote, i = [], 0, '', None, 0
    while i < len(text):
        c = text[i]
        if quote:
            cur += c
            if c == '\\' and i + 1 < len(text):
                cur += text[i + 1]
                i += 1
            elif c == quote:
                quote = None
        elif c in '"`':
            quote = c
            cur += c
        elif c in '([{<':
            depth += 1
            cur += c
        elif c in ')]}>' and not (c == '>' and i and text[i - 1] == '='):
            depth -= 1
            cur += c
        elif depth == 0 and text.startswith(sep, i):
            out.append(cur)
            cur = ''
            i += len(sep)
            continue
        else:
            cur += c
        i += 1
    out.append(cur)
    return [p.strip() for p in out]


def skeleton(t, depth=0):
    """A type text (or a name, a path...) with every name, string and number replaced by `…`: its structure only"""
    t = t.strip()
    if not t:
        return ''
    if depth > 3:
        return '…'
    if t in KNOWN_ARGS:
        return t
    if re.fullmatch(r'\d{1,3}', t):
        return t
    for sep in (' | ', ' & '):
        parts = split_top(t, sep)
        if len(parts) > 1:
            keep = [skeleton(p, depth + 1) for p in parts]
            out = []
            for k in keep:  # (runs of names collapsed: `… | undefined` whatever the union's other members)
                if not (out and out[-1] == '…' and k == '…'):
                    out.append(k)
            return sep.join(out) + (f'  [{len(parts)} members]' if depth == 0 and len(parts) > 2 else '')
    m = re.match(r'^(?:new\s*)?(<[^>]*>\s*)?\((.*)\)\s*=>\s*(.*)$', t, re.S)
    if m and split_top(m.group(2) + ')', ')')[0] == m.group(2).strip():
        params = [p for p in split_top(m.group(2), ',') if p]
        return (('new ' if t.startswith('new') else '') + ('<…>' if m.group(1) else '') +
                f'({len(params)} params) => ' + skeleton(m.group(3), depth + 1))
    if t.startswith('{') and t.endswith('}'):
        inner = t[1:-1].strip().rstrip(';')
        members = [p for p in split_top(inner, ';') if p] if inner else []
        if inner and re.match(r'^(?:[-+]?readonly\s+)?\[\s*' + IDENT + r'\s+in\b', inner):
            return '{ [… in …]: … }'
        return '{}' if not members else f'{{ … }}  [{len(members)} members]' if depth == 0 else '{ … }'
    if t.startswith('[') and t.endswith(']'):
        return '[…]'
    if t.endswith('[]'):
        return skeleton(t[:-2], depth + 1).split('  [')[0] + '[]'
    if t.startswith('"') or t.startswith("'"):
        return '"…"'
    if t.startswith('`'):
        return '`…`'
    if re.fullmatch(r'-?\d+(\.\d+)?n?', t):
        return '0'
    for kw in ('typeof ', 'keyof ', 'readonly ', 'unique '):
        if t.startswith(kw):
            return kw + skeleton(t[len(kw):], depth + 1)
    m = re.match(r'^(' + IDENT + r'(?:\.' + IDENT + r')*)<(.*)>$', t, re.S)
    if m:
        args = split_top(m.group(2), ',')
        head = m.group(1) if m.group(1) in KEYWORDS else '…'
        return f'{head}<{", ".join(skeleton(a, depth + 1).split("  [")[0] for a in args)}>'
    if re.fullmatch(IDENT + r'(\.' + IDENT + r')*', t):
        return t if t in KEYWORDS else '…'
    if ' extends ' in t and ' ? ' in t:
        return '… extends … ? … : …'
    if t.startswith('import('):
        return 'import("…")…'
    return '…'


def template(code, text, templates, chain=False):
    """(template with its arguments as skeletons, the raw arguments) of a message"""
    by_code, all_ = templates
    for rx, tmpl in (all_ if chain else by_code.get(code, [])):
        m = rx.match(text)
        if not m:
            continue
        args = {}
        for name, value in m.groupdict().items():
            args[int(name[1:].split('_')[0])] = value
        return re.sub(r'\{(\d+)\}', lambda g: skeleton(args.get(int(g.group(1)), '')).split('  [')[0], tmpl), \
            [(args[k], is_type_slot(tmpl[:tmpl.find('{%d}' % k)])) for k in sorted(args)]
    # No template: quoted texts replaced, and any word no message template has
    words = template_words(templates)
    out = re.sub(r"'([^']*)'", lambda g: "'" + skeleton(g.group(1)).split('  [')[0] + "'", text)
    out = re.sub(IDENT, lambda g: g.group(0) if g.group(0) in words or g.group(0) in KEYWORDS else '…', out)
    return '(no template) ' + re.sub(r'"[^"]*"', '"…"', out), \
        [(m.group(1), is_type_slot(text[:m.start() + 1])) for m in re.finditer(r"'([^']*)'", text)]


def is_type_slot(before):
    """Whether a message argument after this text is a type (`type '{0}'`, `Type '{0}'`, `types '{0}' and '{1}'`)"""
    return bool(re.search(r"\btypes?\s+'$|\btypes?\s+'[^']*'\s+and\s+'$|\btype\s+of\s+'$", before, re.I))


_words = None


def template_words(templates):
    global _words
    if _words is None:
        _words = {w for _, t in templates[1] for w in re.findall(IDENT, re.sub(r'\{\d+\}', ' ', t))}
    return _words


def type_names(arg):
    """The names of declared types an argument mentions: each dotted reference's last part, outside string literals,
    not a property name (`a:` / `a?:`) and not a keyword"""
    arg = re.sub(r'"(?:\\.|[^"\\])*"|`[^`]*`', '""', arg)
    names = []
    for m in re.finditer(r'(?<![\w$.])(' + IDENT + r'(?:\.' + IDENT + r')*)(?!\s*\??:)', arg):
        name = m.group(1).split('.')[-1]
        if name not in KNOWN_ARGS and name not in ('typeof', 'keyof', 'readonly', 'unique', 'infer', 'extends', 'new',
                                                   'is', 'asserts', 'in', 'import') and name not in names:
            names.append(name)
    return names


DECL = re.compile(r'(?:^|[\s;{}(])(?:export\s+)?(?:declare\s+)?(?:default\s+)?(?:abstract\s+)?'
                  r'(interface|type|class|enum|namespace)\s+(' + IDENT + r')', re.M)


def declaration_index(files, wanted):
    """{name: [(origin, shape)]} of the type-ish declarations of the names wanted, over the program's files"""
    index = collections.defaultdict(list)
    if not wanted:
        return index
    for path in files:
        try:
            text = open(path, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        if not any(w in text for w in wanted):
            continue
        for m in DECL.finditer(text):
            if m.group(2) in wanted and is_declaration(m.group(1), text, m.end()):
                entry = (origin_of(path), declaration_shape(m.group(1), text, m.end()))
                if entry not in index[m.group(2)]:
                    index[m.group(2)].append(entry)
    return index


def is_declaration(kind, text, at):
    """Whether `kind name` at text[:at] declares (`type the parameter` in a comment doesn't)"""
    end = generic_count(text, at)[1]
    after = text[end:end + 40].lstrip()
    if kind == 'type':
        return after.startswith('=')
    return after[:1] in ('{', '') or re.match(r'(extends|implements)\b', after) is not None


def origin_of(path):
    base = os.path.basename(path)
    if re.fullmatch(r'lib(\.[\w.-]+)?\.d\.ts', base) and ('/thirdparty/lib/' in path or '/typescript/' in path or
                                                          '/node_modules/' not in path):
        return base
    if '/node_modules/' in path:
        rest = path.rsplit('/node_modules/', 1)[1].split('/')
        return 'node_modules/' + ('/'.join(rest[:2]) if rest[0].startswith('@') else rest[0])
    return 'project ' + ('.d.ts' if path.endswith('.d.ts') else 'source')


def generic_count(text, at):
    """How many type parameters a declaration's `<...>` at text[at:] has (0 if none), and the end of the list"""
    i = at
    while i < len(text) and text[i] in ' \t':
        i += 1
    if i >= len(text) or text[i] != '<':
        return 0, at
    depth, j = 0, i
    while j < len(text):
        depth += text[j] == '<'
        depth -= text[j] == '>' and text[j - 1] != '='
        if depth == 0:
            break
        j += 1
    return len([p for p in split_top(text[i + 1:j], ',') if p]), j + 1


def declaration_shape(kind, text, at):
    n, end = generic_count(text, at)
    generic = f', generic with {n} params' if n else ''
    if kind in ('interface', 'class', 'enum', 'namespace'):
        head = text[end:end + 400].split('{', 1)[0]
        ext = ' extending' if kind != 'enum' and re.search(r'\bextends\b', head) else ''
        return kind + ext + generic
    # A type alias: its right-hand side to the end of the declaration
    eq = text.find('=', end)
    if eq < 0 or eq - end > 200:
        return 'alias' + generic
    rhs, depth, i = '', 0, eq + 1
    while i < len(text) and i - eq < 20000:
        c = text[i]
        if c in '([{<':
            depth += 1
        elif c in ')]}>' and not (c == '>' and text[i - 1] == '='):
            depth -= 1
        if depth < 0 or (depth == 0 and c == ';'):
            break
        if depth == 0 and c == '\n' and re.match(r'\s*(export|type|interface|declare|const|function|class|let|var|'
                                                  r'import|namespace|enum|//|/\*|$)', text[i + 1:i + 40]):
            break
        rhs += c
        i += 1
    rhs = rhs.strip().lstrip('|&').strip()
    unions, inters = split_top(rhs, ' | '), split_top(rhs, ' & ')
    if len(split_top(rhs, '|')) > 1 and len(unions) <= 1:
        unions = split_top(rhs, '|')
    if len(split_top(rhs, '&')) > 1 and len(inters) <= 1:
        inters = split_top(rhs, '&')
    if ' extends ' in rhs and '?' in rhs and len(unions) <= 1 and re.search(r'\bextends\b[^?]*\?', rhs):
        shape = 'conditional'
    elif len(unions) > 1:
        shape = f'union of {len(unions)}'
    elif len(inters) > 1:
        branded = any(re.search(r'__brand|__type|\bbrand\b|unique symbol|__tag|_brand', p) for p in inters)
        shape = f'{"branded " if branded else ""}intersection of {len(inters)}'
    elif re.match(r'^\{\s*(?:[-+]?readonly\s+)?\[\s*' + IDENT + r'\s+in\b', rhs):
        shape = 'mapped'
    elif re.match(r'^(?:new\s*)?(<[^>]*>\s*)?\(', rhs) and '=>' in rhs:
        m = re.match(r'^(?:new\s*)?(?:<[^>]*>\s*)?\((.*?)\)\s*=>', rhs, re.S)
        shape = f'function with {len([p for p in split_top(m.group(1), ",") if p]) if m else "?"} params'
    elif rhs.startswith('{'):
        shape = 'object type'
    elif rhs.startswith('`'):
        shape = 'template literal'
    elif rhs.startswith('keyof') or rhs.startswith('typeof'):
        shape = rhs.split()[0]
    elif re.match(r'^' + IDENT + r'(\.' + IDENT + r')*\s*\[', rhs):
        shape = 'indexed access'
    elif re.match(r'^' + IDENT + r'(\.' + IDENT + r')*\s*<', rhs):
        shape = 'instantiation of a generic'
    elif re.match(r'^' + IDENT + r'(\.' + IDENT + r')*$', rhs):
        shape = 'alias of a name' if rhs not in KNOWN_ARGS else 'alias of a keyword'
    else:
        shape = 'other'
    return 'alias, ' + shape + generic


FLAGS = (('JSX', lambda c: 'JSX' in c), ('destructuring', lambda c: 'pattern' in c or 'destructuring' in c),
         ('explicit type arguments', lambda c: 'explicit type arguments' in c),
         ('as/satisfies', lambda c: re.search(r'(^| > )(as|satisfies|<T> assertion|JSDoc cast)( > |$)', c)),
         ('spread', lambda c: 'spread' in c), ('optional chain', lambda c: 'optional ' in c))


def size_class(path):
    try:
        n = sum(1 for _ in open(path, 'rb'))
    except OSError:
        return 'unreadable'
    return next(f'{name} (<{lim} lines)' for lim, name in ((100, 'tiny'), (500, 'small'), (2000, 'medium'),
                                                           (10000, 'large'), (10 ** 12, 'huge')) if n < lim)


def is_test(path):
    return bool(re.search(r'(^|[/._-])(tests?|specs?|__tests__|__mocks__|e2e|fixtures?|stories)([/._-]|$)', path,
                          re.I))


def file_kind(path):
    for ext in ('.d.ts', '.d.mts', '.d.cts', '.tsx', '.ts', '.mts', '.cts', '.jsx', '.js', '.mjs', '.cjs', '.json'):
        if path.endswith(ext):
            return ext
    return 'other'


def program_files(project, cwd, extra):
    r = tttt_run.run([*sandbox(), TTTT, '-p', project, '--listFilesOnly', '--noEmit', '--pretty', 'false', *extra],
                     cwd, timeout=600, mem_gb=16, stderr=subprocess.PIPE)
    return [os.path.join(cwd, line) for line in r.text().splitlines()
            if line and not tscformat.DIAG.match(line) and os.path.isfile(os.path.join(cwd, line))]


def node_shapes(project, cwd, extra, positions):
    import tempfile
    with tempfile.NamedTemporaryFile('w', prefix='projdiff-shapes-', suffix='.txt', dir='/tmp') as f:
        for path, line, col in positions:
            f.write(f'{path}\t{line}\t{col}\n')
        f.flush()
        r = tttt_run.run([*sandbox(), TTTT, '-p', project, '--node-shapes', f.name, '--noEmit', *extra], cwd,
                         timeout=600, mem_gb=16, stderr=subprocess.PIPE)
    shapes = {}
    for line in r.text().splitlines():
        where, _, shape = line.partition('\t')
        shapes[where] = shape
    return shapes


def redacted_report(project, cwd, extra, g_text, d_text, only_g, only_d, limit):
    templates = load_templates()
    diags = []  # (side, file, line, col, code, head, chain lines)
    for side, text, only in (('tsgo-only', g_text, only_g), ('tttt-only', d_text, only_d)):
        left = collections.Counter(only)
        for block in tscformat.blocks(text):
            head, *chain = block.split('\n')
            f, l, c, code, msg = tscformat.parse(head)[0]
            if left[(f, l, code)] > 0:
                left[(f, l, code)] -= 1
                diags.append((side, f, l, c, code, msg, [x.strip() for x in chain]))
    shapes = node_shapes(project, cwd, extra, sorted({(f, l, c) for _, f, l, c, *_ in diags if f}))
    rendered, wanted = [], set()
    for side, f, l, c, code, msg, chain in diags:
        tmpl, args = template(code, msg, templates)
        chain_t = [template(0, x, templates, chain=True)[0] for x in chain]
        names = [type_names(a) if typed else [] for a, typed in args]
        args = [a for a, _ in args]
        wanted.update(n for ns in names for n in ns)
        rendered.append((side, f, l, c, code, tmpl, chain_t, args, names))
    index = declaration_index(program_files(project, cwd, extra), wanted) if wanted else {}
    print(f'redacted: {len(diags)} differing diagnostics (tsgo-only {sum(only_g.values())}, '
          f'tttt-only {sum(only_d.values())}); names, paths and source text left out')
    pairs, by_code = collections.Counter(), collections.Counter()
    for n, (side, f, l, c, code, tmpl, chain_t, args, names) in enumerate(rendered):
        shape = shapes.get(f'{f}:{l}:{c}', '(no file)' if not f else '(unknown)')
        labels = shape.split(' > ')
        short = ' > '.join(labels[-3:])
        pairs[(code, short)] += 1
        by_code[(code, side)] += 1
        if n >= limit:
            continue
        path = os.path.join(cwd, f) if f else ''
        print(f'\n#{n + 1} {side} TS{code}: {tmpl}')
        for x in chain_t[:4]:
            print(f'    | {x}')
        if len(chain_t) > 4:
            print(f'    | ... {len(chain_t) - 4} more chain lines')
        flags = [name for name, test in FLAGS if test(shape)]
        print(f'  at: {shape}' + (f'  [{", ".join(flags)}]' if flags else ''))
        for i, (a, ns) in enumerate(zip(args, names)):
            found = ['; '.join(f'{o}: {s}' for o, s in index[name][:2]) + (' (+more)' if len(index[name]) > 2 else '')
                     for name in ns if index.get(name)]
            if found:  # (Names with no declaration found: type parameters, locals, re-exports, property names)
                print(f'  arg {i} {skeleton(a).split("  [")[0]}: ' + ' / '.join(found[:4]) +
                      (f' / ... {len(found) - 4} more' if len(found) > 4 else '') +
                      (f' ({len(ns) - len(found)} other names undeclared)' if len(ns) > len(found) else ''))
        if path:
            origin = origin_of(path)
            print(f'  file: {file_kind(path)}, {size_class(path)}' + (', test file' if is_test(f) else '') +
                  ('' if origin.startswith('project') else f', in {origin}'))
    if len(rendered) > limit:
        print(f'\n... {len(rendered) - limit} more (--limit)')
    # The same (file, line, code) on both sides, other text: the first differing line of each, as templates
    g_blocks, d_blocks = collections.defaultdict(list), collections.defaultdict(list)
    for text, into in ((g_text, g_blocks), (d_text, d_blocks)):
        for block in tscformat.blocks(text):
            f, l, c, code, _ = tscformat.parse(block.split('\n')[0])[0]
            into[(f, l, code)].append((c, block))
    differ = []
    for key, gs in g_blocks.items():
        for (c, gb), (_, db) in zip(gs, d_blocks.get(key, [])):
            if gb != db:
                differ.append((key, c, gb.split('\n'), db.split('\n')))
    if differ:
        shapes2 = node_shapes(project, cwd, extra, sorted({(f, l, c) for (f, l, _), c, _, _ in differ if f}))
        print(f'\nsame position and code, other text: {len(differ)}')
        text_kinds = collections.Counter()
        for n, ((f, l, code), c, ga, da) in enumerate(differ):
            i = next((i for i, (x, y) in enumerate(zip(ga, da)) if x != y), min(len(ga), len(da)))
            part = 'header' if i == 0 else f'chain line {i}' if i < len(ga) and i < len(da) else 'chain length'
            text_kinds[(code, part)] += 1
            if n >= limit:
                continue
            tg = template(code, tscformat.parse(ga[0])[0][4], templates)[0] if i == 0 else \
                template(0, ga[i].strip(), templates, chain=True)[0] if i < len(ga) else '(end)'
            td = template(code, tscformat.parse(da[0])[0][4], templates)[0] if i == 0 else \
                template(0, da[i].strip(), templates, chain=True)[0] if i < len(da) else '(end)'
            print(f'#t{n + 1} TS{code} {part}: tsgo: {tg}' + (f'\n      tttt: {td}' if td != tg else
                                                             '  (tttt: the same template, other names or types)'))
            print(f'  at: {shapes2.get(f"{f}:{l}:{c}", "(unknown)")}')
        for (code, part), k in text_kinds.most_common(10):
            print(f'  {k:5}  TS{code}  {part}')
    if not by_code:
        return
    print('\nby code and side:')
    for code in sorted({c for c, _ in by_code}):
        print(f'  TS{code}: tsgo-only {by_code[(code, "tsgo-only")]}, tttt-only {by_code[(code, "tttt-only")]}')
    print('most frequent (code, shape):')
    for (code, short), k in pairs.most_common(10):
        print(f'  {k:5}  TS{code}  {short}')


def make_copy(target):
    project, config = (os.path.dirname(target), os.path.basename(target)) if os.path.isfile(target) else (target, 'tsconfig.json')
    p = subprocess.run([sys.executable, os.path.join(ROOT, 'scripts/reduce.py'), '--project', project, '--config', config,
                        '--keep', '.', '--no-reduce'], capture_output=True, text=True)
    m = re.search(r'^copy: (/tmp/reduce-\S+)', p.stdout, re.M)
    if not m:
        sys.exit(f'projdiff: reduce.py made no copy:\n{p.stdout[-2000:]}{p.stderr[-2000:]}')
    copy = m.group(1)
    meta = json.load(open(os.path.join(copy, 'reduce.json')))
    if not REDACT:
        print(f'copy: {copy} (rm -r {copy} when done)')
    return os.path.join(copy, meta['config'])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('project', help='a tsconfig.json or its directory')
    ap.add_argument('extra', nargs='*', help='after --: options for both sides')
    ap.add_argument('--copy', action='store_true', help="diff on reduce.py's scratch copy of the project")
    ap.add_argument('--timeout', type=int, default=900)
    ap.add_argument('--mem-gb', type=int, default=16, help="tttt's address space beyond its threads' (tttt_run)")
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--redact', action='store_true', help='print no path, name or source text (see the header)')
    ap.add_argument('--limit', type=int, default=200, help='--redact: the differing diagnostics shown one by one')
    args = ap.parse_args()
    global REDACT
    REDACT = args.redact
    if not args.no_build:
        subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')], check=True, stdout=subprocess.DEVNULL)
    target = os.path.abspath(args.project)
    if args.copy:
        target = make_copy(target)
    cwd = target if os.path.isdir(target) else os.path.dirname(target)
    project = '.' if os.path.isdir(target) else os.path.basename(target)
    g_text, g_status = tsgo(project, cwd, args.extra, args.timeout)
    d_text, d_code, d_status, d_err = tttt(project, cwd, args.extra, args.timeout, args.mem_gb, False)
    k_text, k_code, k_status, k_err = tttt(project, cwd, args.extra, args.timeout, args.mem_gb, True)
    bad = False
    for name, status, err in (('tttt', d_status, d_err), ('tttt --checkers 1', k_status, k_err)):
        if status != 'ok':
            bad = True
            print(f'{name}: {status}' + (' (a resource limit, not a crash: --timeout / --mem-gb)'
                                         if tttt_run.resource_status(status) else '') +
                  ('\n  ' + '\n  '.join(err.strip().splitlines()[-5:]) if err.strip() and not REDACT else ''))
    g, d, k = keys(g_text), keys(d_text), keys(k_text)
    if REDACT:
        redacted_report(project, cwd, args.extra, g_text, d_text, g - d, d - g, args.limit)
    else:
        show('tsgo only', g - d, g_text)
        show('tttt only', d - g, d_text)
    if d_status == k_status == 'ok' and (d_text, d_code) != (k_text, k_code):
        bad = True
        print('K-DEPENDENT: tttt at its defaults and at --checkers 1 differ (a tttt bug: scripts/kdiff.py)')
        if REDACT:
            print(f'  defaults only: {sum((d - k).values())}, --checkers 1 only: {sum((k - d).values())}')
        else:
            show('  defaults only', d - k, d_text)
            show('  --checkers 1 only', k - d, k_text)
        if d == k:
            print('  (same (file, line, code): the texts or exit codes differ)')
    same = d_text == g_text and d_code == g_status
    agree = g == d
    print(f'tsgo {sum(g.values())} diagnostics (exit {g_status}), tttt {sum(d.values())} (exit {d_code}, '
          f'--checkers 1: {sum(k.values())}, exit {k_code}): ' +
          ('byte-identical' if same else 'same (file, line, code), other text' if agree else 'DIFFER'))
    if agree and not same and REDACT:
        a, b = g_text.splitlines(), d_text.splitlines()
        print(f'  first differing line: {next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b))) + 1}'
              f' of {len(a)}' + ('' if d_code == g_status else f'; exit codes: tsgo {g_status}, tttt {d_code}'))
    elif agree and not same:
        a, b = g_text.splitlines(), d_text.splitlines()
        first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
        print(f'  first differing line {first + 1}:\n    tsgo: {a[first] if first < len(a) else "(end)"}\n'
              f'    tttt: {b[first] if first < len(b) else "(end)"}' + ('' if d_code == g_status else
                                                                         f'\n  exit codes: tsgo {g_status}, tttt {d_code}'))
    sys.exit(0 if agree and not bad else 1)


if __name__ == '__main__':
    main()
