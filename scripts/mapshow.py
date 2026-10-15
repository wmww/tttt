#!/usr/bin/env python3
"""Decodes a .d.ts.map (plan 64 stage 5) against its .d.ts and sources: one line per mapping, the generated text from
it to the next one and the source text at its position.

  scripts/mapshow.py OUT.d.ts.map                 one map (its .d.ts beside it, sources resolved from the map)
  scripts/mapshow.py EXPECTED.d.ts.map OURS.d.ts.map   both, side by side from the first differing mapping (-a: all)
  scripts/mapshow.py -s EXPECTED OURS               the mappings only one side has (N=60 of them)
"""
import json
import os
import sys

B64 = {c: i for i, c in enumerate('ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/')}


def decode(mappings):
    """[(gen_line, gen_col, source_index, source_line, source_col)] (source fields None for a generated-only one)"""
    out = []
    state = [0, 0, 0, 0]  # source, line, col, name
    for line, group in enumerate(mappings.split(';')):
        col = 0
        for seg in group.split(','):
            if not seg:
                continue
            values, value, shift = [], 0, 0
            for ch in seg:
                d = B64[ch]
                value += (d & 31) << shift
                if d & 32:
                    shift += 5
                else:
                    values.append(-(value >> 1) if value & 1 else value >> 1)
                    value, shift = 0, 0
            col += values[0]
            if len(values) >= 4:
                for i in range(3):
                    state[i] += values[i + 1]
                out.append((line, col, state[0], state[1], state[2]))
            else:
                out.append((line, col, None, None, None))
    return out


def utf16_slice(line, start, end=None):
    # (Columns are UTF-16 units)
    units = line.encode('utf-16-le')
    return units[start * 2:None if end is None else end * 2].decode('utf-16-le', errors='replace')


def load(path):
    m = json.load(open(path))
    base = os.path.dirname(path)
    dts = os.path.join(base, m['file'])
    gen = open(dts, encoding='utf-8-sig').read().split('\n') if os.path.exists(dts) else []
    root = m.get('sourceRoot') or ''
    sources = []
    for s in m['sources']:
        p = os.path.normpath(os.path.join(base, root, s))
        sources.append(open(p, encoding='utf-8-sig').read().split('\n') if os.path.exists(p) else [])
    return m, decode(m['mappings']), gen, sources


def rows(m, maps, gen, sources):
    res = []
    for i, (gl, gc, si, sl, sc) in enumerate(maps):
        nxt = maps[i + 1] if i + 1 < len(maps) and maps[i + 1][0] == gl else None
        text = utf16_slice(gen[gl], gc, nxt[1] if nxt else None) if gl < len(gen) else '?'
        if si is None:
            src = '(generated)'
        else:
            lines = sources[si] if si < len(sources) else []
            src = f'{sl + 1}:{sc + 1} ' + (repr(utf16_slice(lines[sl], sc, sc + 16)) if sl < len(lines) else '?')
        res.append((f'{gl + 1}:{gc + 1}', repr(text), src, (gl, gc, si, sl, sc)))
    return res


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    show_all = '-a' in sys.argv
    if len(args) == 1:
        m, maps, gen, sources = load(args[0])
        print('file', m['file'], 'sourceRoot', repr(m.get('sourceRoot')), 'sources', m['sources'])
        for g, text, src, _ in rows(m, maps, gen, sources):
            print(f'{g:>8} {text:<40} {src}')
        return
    a, b = load(args[0]), load(args[1])
    for key in ('file', 'sourceRoot', 'sources', 'names'):
        if a[0].get(key) != b[0].get(key):
            print(f'{key}: expected {a[0].get(key)!r} ours {b[0].get(key)!r}')
    ra, rb = rows(*a), rows(*b)
    if '-s' in sys.argv:
        # Set differences: the mappings only one side has, in generated order
        ka = {r[3]: r for r in ra}
        kb = {r[3]: r for r in rb}
        diff = sorted([(k, '-', ka[k]) for k in ka.keys() - kb.keys()] + [(k, '+', kb[k]) for k in kb.keys() - ka.keys()])
        print(f'{len(ka) - len(ka.keys() - kb.keys())} common, {len(ka.keys() - kb.keys())} only expected (-), '
              f'{len(kb.keys() - ka.keys())} only ours (+)')
        for k, mark, r in diff[:int(os.environ.get('N', 60))]:
            print(f'{mark} {r[0]:>7} {r[1]:<30} {r[2]}')
        return
    first = next((i for i in range(min(len(ra), len(rb))) if ra[i][3] != rb[i][3]), min(len(ra), len(rb)))
    if first == len(ra) == len(rb):
        print('identical mappings')
        return
    start = 0 if show_all else max(0, first - 3)
    print(f'first difference at mapping {first} of {len(ra)} / {len(rb)}')
    for i in range(start, max(len(ra), len(rb)) if show_all else min(max(len(ra), len(rb)), first + 25)):
        x = ra[i] if i < len(ra) else ('', '', '', None)
        y = rb[i] if i < len(rb) else ('', '', '', None)
        mark = ' ' if x[3] == y[3] else '!'
        print(f'{mark} {x[0]:>7} {x[1]:<28} {x[2]:<30} | {y[0]:>7} {y[1]:<28} {y[2]}')


if __name__ == '__main__':
    main()
