#!/usr/bin/env python3
"""Re-entrancy census, tttt against an instrumented tsgo (plan 88): what is asked for again while in flight.

    scripts/reentrytrace.py PROJECT [PROJECT...]   # each side's events per (event, file:line), and where they differ
    scripts/reentrytrace.py FILE.ts                # a single file (strict, like scripts/run.sh)
    scripts/reentrytrace.py --tsgo-only P...       # only tsgo's events (the census: -j N over many projects)
    scripts/reentrytrace.py --trace P              # also tsgo's whole pushTypeResolution trace, in /tmp/tsgo-reentry

tsgo 7.1's CLI is built once with patched copies of internal/checker/{checker,relater}.go (go build -overlay, nothing
is written into the shared checkout; GOCACHE: --gocache DIR, else the shared one) into /tmp/tsgo-reentry/tsc. Both
sides run single-threaded (tttt --checkers 1) and print `TTTT EVENT file:line`:

    REENTER K B     pushTypeResolution's entity K (tsc's TypeSystemPropertyName: 0 a symbol's type, 1 a base
                    constructor, 2 an alias, 3 a return type, 4 a base constraint, 6 base types...) is in flight below
                    the resolution barrier B (call: getResolvedSignature's resolutionStart, variance: the outermost
                    getVariancesWorker's): not a cycle, it is resolved again, nested, and the first to finish wins
    RETINFLIGHT B   isResolvingReturnTypeOfSignature: a return type in flight below barrier B, which tsc answers
                    "not resolving" (so a contextual return type re-enters it); tttt answers "resolving"
    NESTEDSIG       (tsgo) getResolvedSignature's nested resolution of the same call finished first and replaced the
                    outer one's result
    NESTEDCALL      a call asked for again while it is being resolved, resolved again without a barrier
    MEMBERS         (tsgo) resolveStructuredTypeMembers of a type whose members are being resolved (tsc has no
                    in-flight state there; tttt has, never re-enters)
    ONCE K call     (tttt) a declaration or return type re-entered below the barrier a second time: tttt says
                    circular (lazy_type_evaluate_inner's `reevaluating`), tsc would re-enter again

tsgo's lines are at its currentNode (the statement or expression being checked), tttt's at Checker.current_node:
lines can differ by a statement. Logs land in /tmp/tsgo-reentry.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import subprocess
import sys

ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), '..'))
OUT = '/tmp/tsgo-reentry'
TSC_DIR = os.path.realpath(os.path.join(ROOT, 'thirdparty/typescript/tsc'))

DEBUG_GO = r'''package checker

import (
	"fmt"
	"os"

	"github.com/microsoft/TypeScript/tsc/internal/ast"
	"github.com/microsoft/TypeScript/tsc/internal/scanner"
)

var ttttReentry = os.Getenv("TSGO_REENTRY") != ""
var ttttResTrace = os.Getenv("TSGO_RESTRACE") != ""
var ttttBarrier = "none"
var ttttMembers = map[*Type]int{}

func ttttEntity(e TypeSystemEntity) string {
	switch v := e.(type) {
	case *ast.Symbol:
		return "sym:" + v.Name
	case *Type:
		if v.symbol != nil {
			return fmt.Sprintf("type#%d:%s", v.id, v.symbol.Name)
		}
		return fmt.Sprintf("type#%d", v.id)
	case *Signature:
		if v.declaration != nil {
			return fmt.Sprintf("sig@%d", v.declaration.Pos())
		}
		return "sig"
	case *ast.Node:
		return fmt.Sprintf("node@%d", v.Pos())
	}
	return "?"
}

func (c *Checker) ttttLog(what string, node *ast.Node) {
	if node == nil {
		node = c.currentNode
	}
	loc := ""
	if node != nil {
		if sf := ast.GetSourceFileOfNode(node); sf != nil {
			line := scanner.GetECMALineOfPosition(sf, scanner.SkipTrivia(sf.Text(), node.Pos()))
			loc = fmt.Sprintf(" %s:%d", sf.FileName(), line+1)
		}
	}
	fmt.Fprintf(os.Stderr, "TTTT %s%s\n", what, loc)
}
'''

CHECKER_PATCHES = [
    ('''		c.resolutionStart = len(c.typeResolutions)
	}
	links.resolvedSignature = c.resolvingSignature
	result := c.resolveSignature(node, candidatesOutArray, checkMode)
	c.resolutionStart = saveResolutionStart''', '''		c.resolutionStart = len(c.typeResolutions)
	}
	saveBarrier := ttttBarrier
	if cached == nil {
		ttttBarrier = "call"
	} else if ttttReentry && cached == c.resolvingSignature {
		c.ttttLog("NESTEDCALL", node)
	}
	links.resolvedSignature = c.resolvingSignature
	result := c.resolveSignature(node, candidatesOutArray, checkMode)
	ttttBarrier = saveBarrier
	c.resolutionStart = saveResolutionStart'''),
    ('''		if links.resolvedSignature != c.resolvingSignature {
			result = links.resolvedSignature
		}''', '''		if links.resolvedSignature != c.resolvingSignature {
			if ttttReentry && links.resolvedSignature != result {
				c.ttttLog("NESTEDSIG", node)
			}
			result = links.resolvedSignature
		}'''),
    ('''func (c *Checker) pushTypeResolution(target TypeSystemEntity, propertyName TypeSystemPropertyName) bool {
	resolutionCycleStartIndex := c.findResolutionCycleStartIndex(target, propertyName)''', '''func (c *Checker) pushTypeResolution(target TypeSystemEntity, propertyName TypeSystemPropertyName) bool {
	resolutionCycleStartIndex := c.findResolutionCycleStartIndex(target, propertyName)
	if ttttResTrace {
		c.ttttLog(fmt.Sprintf("PUSH %d %s len=%d start=%d cycle=%d barrier=%s", propertyName, ttttEntity(target), len(c.typeResolutions), c.resolutionStart, resolutionCycleStartIndex, ttttBarrier), nil)
	}
	if ttttReentry && resolutionCycleStartIndex < 0 {
		for i := c.resolutionStart - 1; i >= 0; i-- {
			if c.typeResolutions[i].target == target && c.typeResolutions[i].propertyName == propertyName {
				c.ttttLog(fmt.Sprintf("REENTER %d %s", propertyName, ttttBarrier), nil)
				break
			}
		}
	}'''),
    ('''func (c *Checker) popTypeResolution() bool {''', '''func (c *Checker) popTypeResolution() bool {
	if ttttResTrace {
		c.ttttLog(fmt.Sprintf("POP %v len=%d", c.typeResolutions[len(c.typeResolutions)-1].result, len(c.typeResolutions)), nil)
	}'''),
    ('''func (c *Checker) resolveStructuredTypeMembers(t *Type) *StructuredType {
	if t.objectFlags&ObjectFlagsMembersResolved == 0 {''', '''func (c *Checker) resolveStructuredTypeMembers(t *Type) *StructuredType {
	if t.objectFlags&ObjectFlagsMembersResolved == 0 {
		if ttttReentry {
			if ttttMembers[t] > 0 {
				c.ttttLog(fmt.Sprintf("MEMBERS depth=%d", ttttMembers[t]), nil)
			}
			ttttMembers[t]++
			defer func() { ttttMembers[t]-- }()
		}'''),
]

RELATER_PATCHES = [
    ('''	return signature.resolvedReturnType == nil && c.findResolutionCycleStartIndex(signature, TypeSystemPropertyNameResolvedReturnType) >= 0
}''', '''	r := signature.resolvedReturnType == nil && c.findResolutionCycleStartIndex(signature, TypeSystemPropertyNameResolvedReturnType) >= 0
	if ttttReentry && !r && signature.resolvedReturnType == nil {
		for i := c.resolutionStart - 1; i >= 0; i-- {
			if c.typeResolutions[i].target == signature && c.typeResolutions[i].propertyName == TypeSystemPropertyNameResolvedReturnType {
				c.ttttLog("RETINFLIGHT "+ttttBarrier, nil)
				break
			}
		}
	}
	return r
}'''),
    ('''			if len(c.varianceStack) == 0 {
				c.resolutionStart = len(c.typeResolutions)
			}''', '''			saveBarrier := ttttBarrier
			if len(c.varianceStack) == 0 {
				c.resolutionStart = len(c.typeResolutions)
				ttttBarrier = "variance"
			}
			defer func() { ttttBarrier = saveBarrier }()'''),
]


def build(gocache):
    exe = os.path.join(OUT, 'tsc')
    sources = {name: os.path.join(TSC_DIR, 'internal/checker', name) for name in ('checker.go', 'relater.go')}
    if os.path.exists(exe) and os.path.getmtime(exe) > max([os.path.getmtime(__file__)] + [os.path.getmtime(p) for p in sources.values()]):
        return exe
    os.makedirs(OUT, exist_ok=True)
    replace = {}
    for name, patches in (('checker.go', CHECKER_PATCHES), ('relater.go', RELATER_PATCHES)):
        src = open(sources[name]).read()
        for old, new in patches:
            if src.count(old) != 1:
                sys.exit(f'reentrytrace: {name} changed, a patch no longer applies:\n{old}')
            src = src.replace(old, new)
        open(os.path.join(OUT, name), 'w').write(src)
        replace[sources[name]] = os.path.join(OUT, name)
    open(os.path.join(OUT, 'tttt_reentry.go'), 'w').write(DEBUG_GO)
    replace[os.path.join(TSC_DIR, 'internal/checker/tttt_reentry.go')] = os.path.join(OUT, 'tttt_reentry.go')
    with open(os.path.join(OUT, 'overlay.json'), 'w') as f:
        json.dump({'Replace': replace}, f)
    env_line = subprocess.check_output([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh'), '--env'], text=True)
    if gocache:
        env_line += f'\nexport GOCACHE={gocache}'
    print('reentrytrace: building the instrumented tsgo (~3 min)...', file=sys.stderr)
    subprocess.run(['bash', '-c', f'{env_line}\ngo build -overlay {OUT}/overlay.json -o {exe} ./cmd/tsc'],
                   cwd=TSC_DIR, check=True)
    return exe


def events(stderr):
    """(event, location) -> count. Paths through the corpora/sources symlinks are made relative to them"""
    ret = collections.Counter()
    for line in stderr.splitlines():
        if not line.startswith('TTTT ') or line.startswith(('TTTT PUSH', 'TTTT POP')):
            continue
        parts = line.split()
        loc = parts[-1] if ':' in parts[-1] and '/' in parts[-1] else ''
        event = ' '.join(parts[1:-1] if loc else parts[1:])
        event = re.sub(r'^MEMBERS depth=\d+', 'MEMBERS', event)
        loc = re.sub(r'^.*/thirdparty/(corpora|sources)/', '', loc)
        ret[(event, loc)] += 1
    return ret


def run(cmd, env, cwd, trace_path=None):
    p = subprocess.run(cmd, env=dict(os.environ, **env), cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                       text=True, errors='replace', timeout=900)
    if trace_path:
        with open(trace_path, 'w') as f:
            f.write(p.stderr)
    return events(p.stderr)


def target_args(target):
    path = os.path.abspath(target)
    if os.path.isfile(path) and path.endswith(('.ts', '.tsx', '.mts', '.cts')):
        return [path, '--strict', '--target', 'esnext'], os.path.dirname(path)
    return ['-p', path], ROOT


def tsgo_events(exe, target, trace=False):
    args, cwd = target_args(target)
    env = {'TSGO_REENTRY': '1'}
    if trace:
        env['TSGO_RESTRACE'] = '1'
    trace_path = os.path.join(OUT, 'tsgo.trace') if trace else None
    return run([exe] + args + ['--noEmit', '--singleThreaded', '--incremental', 'false'], env, cwd, trace_path)


def tttt_events(target):
    args, cwd = target_args(target)
    return run([os.path.join(ROOT, 'release/tttt')] + args + ['--noEmit', '--checkers', '1'], {'TTTT_DEBUG_REENTRY': '1'}, cwd)


def summary(counter):
    by_event = collections.Counter()
    for (event, _), n in counter.items():
        by_event[event] += n
    return ', '.join(f'{e} {n}' for e, n in sorted(by_event.items())) or 'none'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('targets', nargs='+', help='tsconfig.json files, their directories, or single .ts files')
    ap.add_argument('--tsgo-only', action='store_true', help="only tsgo's events (no tttt run)")
    ap.add_argument('--trace', action='store_true', help="write tsgo's pushTypeResolution trace to /tmp/tsgo-reentry/tsgo.trace")
    ap.add_argument('-j', '--jobs', type=int, default=1)
    ap.add_argument('-n', type=int, default=30, help='rows of differing locations per target')
    ap.add_argument('--gocache', help='GOCACHE for the build (default: the shared one in thirdparty/typescript-gopath)')
    args = ap.parse_args()
    exe = build(args.gocache)
    if not args.tsgo_only:
        subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)

    def one(target):
        g = tsgo_events(exe, target, args.trace)
        t = collections.Counter() if args.tsgo_only else tttt_events(target)
        return target, g, t

    total_g, total_t = collections.Counter(), collections.Counter()
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for target, g, t in pool.map(one, args.targets):
            total_g.update(g)
            total_t.update(t)
            print(f'{target}\n  tsgo: {summary(g)}')
            if args.tsgo_only:
                continue
            print(f'  tttt: {summary(t)}')
            differ = sorted(set(g) | set(t), key=lambda k: (k[1], k[0]))
            differ = [k for k in differ if g[k] != t[k]]
            for event, loc in differ[:args.n]:
                print(f'    {loc or "?"}  {event}: tsgo {g[(event, loc)]} tttt {t[(event, loc)]}')
            if len(differ) > args.n:
                print(f'    ... {len(differ) - args.n} more')
    if len(args.targets) > 1:
        print(f'all: tsgo {summary(total_g)}' + ('' if args.tsgo_only else f'\n     tttt {summary(total_t)}'))


if __name__ == '__main__':
    main()
