#!/usr/bin/env python3
"""Variance measurements, tttt against an instrumented tsgo (2026-10-12, the knex types blow-up).

    scripts/variancetrace.py PROJECT                 # every measured variance that differs, by symbol name
    scripts/variancetrace.py PROJECT --block NAME    # the relations of NAME's stored measurement, side by side files

Builds tsgo 7.1's CLI with a patched copy of internal/checker/relater.go (go build -overlay, nothing is written into
the shared thirdparty/typescript checkout) into /tmp/tsgo-vartrace/tsc, once. Its TSGO_DEBUG_VAR=1 log and tttt's
TTTT_DEBUG_VARIANCE=1 log (both single-threaded) have the same format:

    VAR> Name depth=N                      a measurement starts (N: the variance stack's depth)
    VAR< Name [v...] stored=B types=N      it ends (N: the types made inside, nested measurements too): tsgo's VarianceFlags (0 in out, 1 out, 2 in, 3 bi, 4 independent,
                                           +8 unmeasurable, +16 unreliable); stored=false when a restart got there first
    VAR circular Name restart=Other        a measurement in flight was asked for
    REL> S -> T / REL< S -> T = R flags=F  a structural comparison inside a measurement (R: T, F, U(nknown), M(aybe);
                                           F: r unreliable, m unmeasurable)
    PROP name = R                          one property of it
    CACHE S -> T = R / MAYBE S -> T        answered by the relation cache / the maybe stack

Markers print as SUB/SUPER/OTHER, type parameters with their ids (`TResult@205`), anonymous types as `#...`. Indentation
is the source stack depth (tttt's REL< is one less). Logs and extracted blocks land in /tmp/tsgo-vartrace.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), '..'))
OUT = '/tmp/tsgo-vartrace'
TSC_DIR = os.path.realpath(os.path.join(ROOT, 'thirdparty/typescript/tsc'))

DEBUG_GO = r'''package checker

import (
	"fmt"
	"os"
	"strings"
)

var ttttDebugVar = os.Getenv("TSGO_DEBUG_VAR") != ""

func (c *Checker) ttttDesc(t *Type, d int) string {
	if t == nil {
		return "nil"
	}
	if t == c.markerSuperType {
		return "SUPER"
	}
	if t == c.markerSubType {
		return "SUB"
	}
	if t == c.markerOtherType {
		return "OTHER"
	}
	if d > 3 {
		return "…"
	}
	var b strings.Builder
	if t.flags&TypeFlagsUnionOrIntersection != 0 {
		sep := " | "
		if t.flags&TypeFlagsIntersection != 0 {
			sep = " & "
		}
		for i, m := range t.Types() {
			if i > 6 {
				b.WriteString(sep + "…")
				break
			}
			if i > 0 {
				b.WriteString(sep)
			}
			b.WriteString(c.ttttDesc(m, d+1))
		}
		return "(" + b.String() + ")"
	}
	if t.alias != nil && t.alias.symbol != nil {
		b.WriteString(t.alias.symbol.Name)
		if len(t.alias.typeArguments) > 0 {
			b.WriteString("<")
			for i, a := range t.alias.typeArguments {
				if i > 0 {
					b.WriteString(", ")
				}
				b.WriteString(c.ttttDesc(a, d+1))
			}
			b.WriteString(">")
		}
		return b.String()
	}
	if t.symbol != nil {
		b.WriteString(t.symbol.Name)
		if t.flags&TypeFlagsTypeParameter != 0 {
			fmt.Fprintf(&b, "@%d", t.id)
		}
	} else {
		fmt.Fprintf(&b, "#%x", uint64(t.flags))
	}
	if t.objectFlags&ObjectFlagsReference != 0 {
		args := t.AsTypeReference().resolvedTypeArguments
		if len(args) > 0 {
			b.WriteString("<")
			for i, a := range args {
				if i > 0 {
					b.WriteString(", ")
				}
				b.WriteString(c.ttttDesc(a, d+1))
			}
			b.WriteString(">")
		}
	}
	return b.String()
}

func (c *Checker) ttttLog(format string, args ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
}

func ttttVarianceInts(v []VarianceFlags) []int {
	r := make([]int, len(v))
	for i, x := range v {
		r[i] = int(x)
	}
	return r
}
'''

IND = 'strings.Repeat(" ", len(r.sourceStack))'
PATCHES = [
    ('''			c.varianceStack = append(c.varianceStack, VarianceStackEntry{symbol, typeParameters})
''', '''			c.varianceStack = append(c.varianceStack, VarianceStackEntry{symbol, typeParameters})
			if ttttDebugVar {
				c.ttttLog("VAR> %s depth=%d", symbol.Name, len(c.varianceStack))
			}
			ttttTypes := c.TypeCount
'''),
    ('''			// Store the results unless a restarted computation has already stored them.
			if len(links.variances) == 0 {
''', '''			// Store the results unless a restarted computation has already stored them.
			if ttttDebugVar {
				c.ttttLog("VAR< %s %v stored=%v types=%d", symbol.Name, ttttVarianceInts(variances), len(links.variances) == 0, c.TypeCount-ttttTypes)
			}
			if len(links.variances) == 0 {
'''),
    ('''			if minIndex > stackIndex {
				saveVarianceStack''', '''			if ttttDebugVar {
				c.ttttLog("VAR circular %s restart=%s", symbol.Name, c.varianceStack[minIndex].symbol.Name)
			}
			if minIndex > stackIndex {
				saveVarianceStack'''),
    ('''		result = r.structuredTypeRelatedTo(source, target, reportErrors, intersectionState)
	}
	propagatingVarianceFlags := r.c.reliabilityFlags
''', '''		if ttttDebugVar && len(r.c.varianceStack) > 0 {
			r.c.ttttLog("%sREL> %s -> %s", ''' + IND + ''', r.c.ttttDesc(source, 0), r.c.ttttDesc(target, 0))
		}
		result = r.structuredTypeRelatedTo(source, target, reportErrors, intersectionState)
	}
	propagatingVarianceFlags := r.c.reliabilityFlags
	if ttttDebugVar && len(r.c.varianceStack) > 0 {
		r.c.ttttLog("%sREL< %s -> %s = %d flags=%d", ''' + IND + ''', r.c.ttttDesc(source, 0), r.c.ttttDesc(target, 0), result, propagatingVarianceFlags)
	}
'''),
    ('''				related := r.propertyRelatedTo(source, target, sourceProp, targetProp, r.c.getNonMissingTypeOfSymbol, reportErrors, intersectionState, r.relation == r.c.comparableRelation)
				if related == TernaryFalse {''', '''				related := r.propertyRelatedTo(source, target, sourceProp, targetProp, r.c.getNonMissingTypeOfSymbol, reportErrors, intersectionState, r.relation == r.c.comparableRelation)
				if ttttDebugVar && len(r.c.varianceStack) > 0 {
					r.c.ttttLog("%sPROP %s = %d", ''' + IND + ''', name, related)
				}
				if related == TernaryFalse {'''),
    ('''			r.c.reliabilityFlags |= entry & (RelationComparisonResultReportsUnmeasurable | RelationComparisonResultReportsUnreliable)
			if reportErrors && entry&RelationComparisonResultOverflow != 0 {''', '''			r.c.reliabilityFlags |= entry & (RelationComparisonResultReportsUnmeasurable | RelationComparisonResultReportsUnreliable)
			if ttttDebugVar && len(r.c.varianceStack) > 0 {
				r.c.ttttLog("%sCACHE %s -> %s = %d", ''' + IND + ''', r.c.ttttDesc(source, 0), r.c.ttttDesc(target, 0), entry)
			}
			if reportErrors && entry&RelationComparisonResultOverflow != 0 {'''),
    ('''	if r.maybeKeysSet.Has(id) {
		return TernaryMaybe
	}''', '''	if r.maybeKeysSet.Has(id) {
		if ttttDebugVar && len(r.c.varianceStack) > 0 {
			r.c.ttttLog("%sMAYBE %s -> %s", ''' + IND + ''', r.c.ttttDesc(source, 0), r.c.ttttDesc(target, 0))
		}
		return TernaryMaybe
	}'''),
]


def build():
    exe = os.path.join(OUT, 'tsc')
    relater = os.path.join(TSC_DIR, 'internal/checker/relater.go')
    if os.path.exists(exe) and os.path.getmtime(exe) > max(os.path.getmtime(__file__), os.path.getmtime(relater)):
        return exe
    os.makedirs(OUT, exist_ok=True)
    src = open(relater).read()
    for old, new in PATCHES:
        if src.count(old) != 1:
            sys.exit(f'variancetrace: relater.go changed, a patch no longer applies:\n{old}')
        src = src.replace(old, new)
    open(os.path.join(OUT, 'relater.go'), 'w').write(src)
    open(os.path.join(OUT, 'tttt_debug.go'), 'w').write(DEBUG_GO)
    with open(os.path.join(OUT, 'overlay.json'), 'w') as f:
        f.write('{"Replace": {"%s": "%s", "%s": "%s"}}\n' % (
            relater, os.path.join(OUT, 'relater.go'),
            os.path.join(TSC_DIR, 'internal/checker/tttt_debug.go'), os.path.join(OUT, 'tttt_debug.go')))
    env_line = subprocess.check_output([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh'), '--env'], text=True)
    print('variancetrace: building the instrumented tsgo (~2 min)...', file=sys.stderr)
    # (Its own build cache: the shared GOCACHE of ensure-typescript.sh can miss entries a plain go build then needs,
    # "could not import ..."; delete OUT/gocache after use, it's ~1-2 GB of tmpfs)
    env_line += f'\nexport GOCACHE={OUT}/gocache\n'
    subprocess.run(['bash', '-c', f'{env_line}\ngo build -overlay {OUT}/overlay.json -o {exe} ./cmd/tsc'],
                   cwd=TSC_DIR, check=True)
    return exe


TERNARY = {'-1': 'T', '0': 'F', '1': 'U', '3': 'M'}


def flags(n):
    n = int(n)
    return ('r' if n & 16 else '') + ('m' if n & 8 else '')


def normalize_tsgo(line):
    m = re.match(r'^(\s*)(REL<.*) = (-?\d+) flags=(\d+)$', line)
    if m:
        return f'{m[1]}{m[2]} = {TERNARY[m[3]]} flags={flags(m[4])}'
    m = re.match(r'^(\s*)(PROP .*) = (-?\d+)$', line)
    if m:
        return f'{m[1]}{m[2]} = {TERNARY[m[3]]}'
    m = re.match(r'^(\s*)(CACHE .*) = (\d+)$', line)
    if m:
        return f'{m[1]}{m[2]} = {"T" if int(m[3]) & 1 else "F"}'
    return line


def read_log(path, tsgo):
    out = []
    for line in open(path, errors='replace'):
        line = line.rstrip('\n')
        if not re.match(r'^\s*(REL|PROP|CACHE|MAYBE|VAR)', line):
            continue
        line = normalize_tsgo(line) if tsgo else re.sub(r'  @[0-9,]+$', '', line)
        out.append(re.sub(r'#[0-9a-f.]+', '#', line))
    return out


def stored(lines):
    """name -> the variances of its stored measurements"""
    ret = {}
    for line in lines:
        m = re.match(r'^VAR< (\S+) \[(.*)\] stored=true', line)
        if m:
            ret.setdefault(m[1], set()).add(m[2])
    return ret


def block(lines, name):
    stack = []
    for i, line in enumerate(lines):
        if line.startswith('VAR> '):
            stack.append((line.split()[1], i))
        elif line.startswith('VAR< '):
            n, start = stack.pop()
            if n == name and 'stored=true' in line:
                return lines[start:i + 1]
    return []


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('project', help='a tsconfig.json or its directory')
    ap.add_argument('--block', metavar='NAME', help="write NAME's stored measurement of both sides to files")
    args = ap.parse_args()
    exe = build()
    subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
    project = os.path.abspath(args.project)
    logs = {}
    for side in ('tsgo', 'tttt'):
        path = os.path.join(OUT, f'{side}.log')
        with open(path, 'w') as err:
            if side == 'tsgo':
                cmd = [exe, '-p', project, '--noEmit', '--singleThreaded', '--tsBuildInfoFile', os.path.join(OUT, 'x.tsbuildinfo')]
                env = dict(os.environ, TSGO_DEBUG_VAR='1')
            else:
                cmd = [os.path.join(ROOT, 'release/tttt'), '-p', project, '--noEmit', '--checkers', '1']
                env = dict(os.environ, TTTT_DEBUG_VARIANCE='1')
            subprocess.run(cmd, env=env, stdout=subprocess.DEVNULL, stderr=err)
        logs[side] = read_log(path, side == 'tsgo')
    g, t = stored(logs['tsgo']), stored(logs['tttt'])
    common = sorted(set(g) & set(t))
    differ = [n for n in common if g[n] != t[n]]
    print(f'{len(common)} measured by both: {len(common) - len(differ)} agree, {len(differ)} differ '
          f'(tsgo only {len(set(g) - set(t))}, tttt only {len(set(t) - set(g))}); logs in {OUT}')
    for n in differ:
        print(f'  {n}: tsgo {" / ".join(sorted(g[n]))}  tttt {" / ".join(sorted(t[n]))}')
    if args.block:
        for side in ('tsgo', 'tttt'):
            path = os.path.join(OUT, f'{args.block}.{side}')
            with open(path, 'w') as f:
                f.write('\n'.join(block(logs[side], args.block)) + '\n')
            print(f'{args.block}, {side}: {path}')


if __name__ == '__main__':
    main()
