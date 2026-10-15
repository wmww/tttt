#!/usr/bin/env python3
"""Scaling probes: one synthetic declaration, block or expression with N members/statements/elements, checked by tttt
(--checkers 1) and tsgo (--singleThreaded) at growing N. A time that grows 4x per doubling of N where tsgo's grows 2x is
a quadratic path (agents/bugs/perf_leads.md "Scaling probes" lists what was found and fixed).

  scripts/scaleprobe.py                       # every kind at 8000 and 32000
  scripts/scaleprobe.py -k obj -k classmem -n 16000 -n 64000
  scripts/scaleprobe.py --list
  scripts/scaleprobe.py --emit                # with declaration emit (+ declarationMap) instead of --noEmit
"""
import argparse, os, resource, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSGO = os.path.join(ROOT, 'thirdparty/typescript/built/tsc')

# kind: (extension, generator of the file's lines)
KINDS = {
    'obj': ('ts', lambda n: ['export const o = {', *[f'  k{i}: {i},' for i in range(n)], '};']),
    'objctx': ('ts', lambda n: ['type R = Record<string, number>;', 'export const o: R = {',
                                *[f'  k{i}: {i},' for i in range(n)], '};']),
    'array': ('ts', lambda n: ['type P = { a: number; b: string };', 'export const xs: P[] = [',
                               *[f'  {{ a: {i}, b: "{i}" }},' for i in range(n)], '];']),
    'tuple': ('ts', lambda n: ['export const t = [' + ', '.join(map(str, range(n))) + '] as const;']),
    'union': ('ts', lambda n: ['export type U = ' + ' | '.join(f'"s{i}"' for i in range(n)) + ';',
                               'declare const u: U;', 'export const v: U = u;']),
    'unionobj': ('ts', lambda n: ['export type U = ' + ' | '.join(f'{{ k: "s{i}"; v{i}: number }}' for i in range(n)) + ';',
                                  'export function f(u: U) { if (u.k === "s1") return u.v1; return 0; }']),
    'switch': ('ts', lambda n: ['type U = ' + ' | '.join(f'"s{i}"' for i in range(n)) + ';',
                                'export function f(u: U) { switch (u) {', *[f'  case "s{i}": return {i};' for i in range(n)], '} }']),
    'ifelse': ('ts', lambda n: ['export function f(x: number | string | undefined) { let r = 0;',
                                *[f'  if (x === {i}) r = {i}; else' for i in range(n)], '  r = -1; return r; }']),
    'stmts': ('ts', lambda n: ['export function f() { let a = 0;', *[f'  a = a + {i};' for i in range(n)], '  return a; }']),
    'locals': ('ts', lambda n: ['export function f() {', *[f'  let v{i} = {i}; v{i}++;' for i in range(n)], '  return 0; }']),
    'calls': ('ts', lambda n: ['declare function g(x: number): number;', 'export function f() {',
                               *[f'  g({i});' for i in range(n)], '}']),
    'narrow': ('ts', lambda n: ['export function f(x: unknown) { let n = 0;',
                                *['  if (typeof x === "number") n += x;' for _ in range(n)], '  return n; }']),
    'concat': ('ts', lambda n: ['export const s = ""' + ''.join(f' + "a{i}"' for i in range(n)) + ';']),
    'andchain': ('ts', lambda n: ['declare const x: number;', 'export const b = ' + ' && '.join(f'x !== {i}' for i in range(n)) + ';']),
    'template': ('ts', lambda n: ['declare const x: number;', 'export const t = `' + 'a${x}' * n + '`;']),
    'enum': ('ts', lambda n: ['export enum E {', *[f'  M{i},' for i in range(n)], '}']),
    'iface': ('ts', lambda n: ['export interface I {', *[f'  m{i}: number;' for i in range(n)], '}',
                               'declare const a: I; export const b: I = a;']),
    'typelit': ('ts', lambda n: ['export type T = {', *[f'  m{i}: number;' for i in range(n)], '};',
                                 'declare const a: T; export const b: T = a;']),
    'classmem': ('ts', lambda n: ['export class C {', *[f'  m{i}(): number {{ return {i}; }}' if i % 2 else f'  p{i} = {i};'
                                                         for i in range(n)], '}']),
    'ns': ('ts', lambda n: ['export namespace NS {', *[f'  export const c{i} = {i};' for i in range(n)], '}',
                            'export const z = NS.c0;']),
    'exports': ('ts', lambda n: [f'export const c{i} = {i};' for i in range(n)]),
    'funcs': ('ts', lambda n: [f'export function f{i}(x: number) {{ return x + {i}; }}' for i in range(n)]),
    'overloads': ('ts', lambda n: ['export function f(x: "a0"): 0;', *[f'export function f(x: "a{i}"): {i};' for i in range(1, n)],
                                   'export function f(x: string): number { return 0; }', f'f("a{n - 1}");']),
    'params': ('ts', lambda n: ['export function f(' + ', '.join(f'p{i}: number' for i in range(n)) + ') { return p0; }']),
    'manyaliases': ('ts', lambda n: [f'export type A{i} = {{ v: {i} }};' for i in range(n)]),
    'aliases': ('ts', lambda n: ['export type A0 = { v: 0 };', *[f'export type A{i} = A{i - 1} | {{ v: {i} }};' for i in range(1, n)]]),
    'jsx': ('tsx', lambda n: ['declare namespace JSX { interface IntrinsicElements { div: any } interface Element {} }',
                              'export const e = <div>', *[f'  <div>{{{i}}}</div>' for i in range(n)], '</div>;']),
    'comments': ('ts', lambda n: ['export interface I {', *[f'  /** Member {i}. */\n  m{i}: number;' for i in range(n)], '}']),
    'inferfn': ('ts', lambda n: ['export function f(x: number) { return {', *[f'  k{i}: x + {i},' for i in range(n)], '}; }']),
    'jsdoc': ('js', lambda n: [f'/** @param {{number}} x @returns {{number}} */ function f{i}(x) {{ return x + {i}; }}' for i in range(n)]),
    'cjs': ('js', lambda n: [f'module.exports.c{i} = {i};' for i in range(n)]),
}
# Superlinear on both sides: only at small N
SMALL = {'aliases': 4, 'cjs': 4}


def run(cmd, cwd, timeout):
    start = time.time()
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_AS, (8 << 30, 8 << 30)))
    try:
        _, status, usage = os.wait4(proc.pid, 0) if timeout is None else wait(proc, timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        os.wait4(proc.pid, 0)
        return None, None
    return time.time() - start, usage.ru_maxrss >> 10


def wait(proc, timeout):
    deadline = time.time() + timeout
    while True:
        pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
        if pid:
            return pid, status, usage
        if time.time() > deadline:
            raise subprocess.TimeoutExpired(proc.args, timeout)
        time.sleep(0.005)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-k', '--kind', action='append', help='a kind (repeatable; default: all)')
    parser.add_argument('-n', action='append', type=int, help='sizes (repeatable; default 8000 and 32000)')
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--no-tsgo', action='store_true')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--emit', action='store_true', help='emit declarations and maps (the JS kinds: tsgo only)')
    args = parser.parse_args()
    if args.list:
        print(' '.join(KINDS))
        return
    subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release')], check=True, stdout=subprocess.DEVNULL)
    sizes = args.n or [8000, 32000]
    for kind in args.kind or KINDS:
        ext, gen = KINDS[kind]
        for n in sizes:
            n = n // SMALL.get(kind, 1) if not args.n else n
            with tempfile.TemporaryDirectory(prefix='scaleprobe-') as tmp:
                with open(os.path.join(tmp, f'a.{ext}'), 'w') as f:
                    f.write('\n'.join(gen(n)) + '\n')
                with open(os.path.join(tmp, 'tsconfig.json'), 'w') as f:
                    emit = ('"declaration":true,"emitDeclarationOnly":true,"declarationMap":true,"outDir":"out"' if args.emit
                            else '"noEmit":true')
                    f.write(f'{{"compilerOptions":{{"strict":true,"lib":["es2020"],{emit},"allowJs":true,'
                            f'"checkJs":true,"jsx":"preserve"}},"files":["a.{ext}"]}}')
                t, m = run([os.path.join(ROOT, 'release/tttt'), '-p', '.', '--checkers', '1', *([] if args.emit else ['--noEmit'])],
                           tmp, args.timeout)
                line = f'{kind:12} {n:7} | tttt ' + (f'{t:7.2f} s {m:6} MB' if t is not None else '  timeout')
                if not args.no_tsgo:
                    t, m = run([TSGO, '-p', '.', '--singleThreaded'], tmp, args.timeout)
                    line += ' | tsgo ' + (f'{t:7.2f} s {m:6} MB' if t is not None else '  timeout')
                print(line, flush=True)


if __name__ == '__main__':
    main()
