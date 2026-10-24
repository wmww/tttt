#!/usr/bin/env python3
"""Every type relation tsgo checks, next to tttt's for the same input, so the first place they part is a diff away.

    scripts/reltrace.py FILE.ts [--filter TEXT] [-- --strict --target esnext]   # one file (a generated tsconfig)
    scripts/reltrace.py PROJECT [--filter TEXT]                                 # a tsconfig.json or its directory
    scripts/reltrace.py --check                                                 # only patch and compile (overlaycheck.sh)

tsgo 7.1 runs through tttt-oracle (single-threaded, what `tsc -p P --noEmit` reports, build info hidden: never a plain
built/tsc -p on a shared checkout), built with a patched copy of internal/checker/relater.go (go build -overlay, the
shared thirdparty/typescript checkout untouched) into /tmp/reltrace-<tsgo commit>-<patch hash>/, once. tttt runs as
release/tttt --checkers 1 --noEmit with TTTT_DEBUG_RELATE=1 TTTT_DEBUG_RELATE_NESTED=1. Both logs are normalized to

    nested D (S/T): 'SOURCE' -> 'TARGET': R [deep]   one structured comparison (tsc's recursiveTypeRelatedTo) as it
                                                     finishes (so children come before their parent): D its depth in
                                                     this relation, S/T the deep-nesting stacks' depths, R T(rue),
                                                     F(alse), M(aybe), U(nknown); `deep`: both sides deeply nested
    maybe D: 'SOURCE' -> 'TARGET'                    answered Maybe by the maybe stack (in flight further up)
    relation KIND: 'SOURCE' -> 'TARGET': yes|no      a top-level query (tsc's checkTypeRelatedTo; tttt's
                                                     ty_is_related: both after their fast paths, which aren't logged)

and written to OUT/tsgo.log and OUT/tttt.log (OUT a fresh /tmp/reltrace-run-* directory, printed; delete it when
done), with `--filter TEXT` keeping only the lines that contain TEXT. Then the first lines where the two differ
(difflib over the logs, depths left out: tttt counts some steps tsc takes outside recursiveTypeRelatedTo, so they are
often off by one where the comparisons agree; a unit type against a union is logged by tttt alone); `diff
OUT/tsgo.log OUT/tttt.log` for the rest. Both sides print types with their own
typeToString, so a line can differ in printing alone (union order, `T_1`): look at the shape, not the first byte.
The orders agree as long as both check the same things in the same order: deferred bodies, a different
checking order, or a cached result on one side (tsc's relation cache answers before recursiveTypeRelatedTo, and so
does tttt's) shift them. A tiny input (one function) keeps that noise down.
"""
import argparse
import difflib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tsgotrace  # noqa: E402 (oracle_overlay, go_env, tsgo_commit)
import tttt_run  # noqa: E402

ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), '..'))
TSC_DIR = os.path.realpath(os.path.join(ROOT, 'thirdparty/typescript/tsc'))

HOOK = r'''package checker

import (
	"fmt"
	"os"
)

var ttttRelTrace = os.Getenv("TSGO_RELTRACE") != ""
var ttttRelDepth = map[*Relater]int{}

func ttttTernary(t Ternary) string {
	switch t {
	case TernaryTrue:
		return "T"
	case TernaryFalse:
		return "F"
	case TernaryMaybe:
		return "M"
	}
	return "U"
}

func (c *Checker) ttttRelationName(r *Relation) string {
	switch r {
	case c.assignableRelation:
		return "assignable"
	case c.comparableRelation:
		return "comparable"
	case c.subtypeRelation:
		return "subtype"
	case c.strictSubtypeRelation:
		return "strictSubtype"
	case c.identityRelation:
		return "identity"
	}
	return "other"
}

func ttttRelLog(format string, args ...any) {
	fmt.Fprintf(os.Stderr, "RELTRACE "+format+"\n", args...)
}
'''

PATCHES = [
    # A pair on the maybe stack
    ('''	if r.maybeKeysSet.Has(id) {
		return TernaryMaybe
	}''', '''	if r.maybeKeysSet.Has(id) {
		if ttttRelTrace {
			ttttRelLog("maybe %d: '%s' -> '%s'", ttttRelDepth[r]+1, r.c.TypeToString(source), r.c.TypeToString(target))
		}
		return TernaryMaybe
	}'''),
    # Entering a structured comparison
    ('''	maybeStart := len(r.maybeKeys)
	r.maybeKeys = append(r.maybeKeys, id)''', '''	maybeStart := len(r.maybeKeys)
	ttttRelDepth[r]++
	r.maybeKeys = append(r.maybeKeys, id)'''),
    # Leaving it, the stacks still holding it
    ('''	propagatingVarianceFlags := r.c.reliabilityFlags
	r.c.reliabilityFlags |= saveReliabilityFlags
''', '''	propagatingVarianceFlags := r.c.reliabilityFlags
	r.c.reliabilityFlags |= saveReliabilityFlags
	if ttttRelTrace {
		deep := ""
		if r.expandingFlags == ExpandingFlagsBoth {
			deep = " deep"
		}
		ttttRelLog("nested %d (%d/%d): '%s' -> '%s': %s%s", ttttRelDepth[r], len(r.sourceStack), len(r.targetStack),
			r.c.TypeToString(source), r.c.TypeToString(target), ttttTernary(result), deep)
	}
	ttttRelDepth[r]--
'''),
    # A top-level query
    ('''	c.putRelater(r)
	return result != TernaryFalse
}''', '''	if ttttRelTrace {
		yes := "yes"
		if result == TernaryFalse {
			yes = "no"
		}
		ttttRelLog("relation %s: '%s' -> '%s': %s", c.ttttRelationName(relation), c.TypeToString(source), c.TypeToString(target), yes)
	}
	c.putRelater(r)
	return result != TernaryFalse
}'''),
]


def prepare(out=None):
    relater = os.path.join(TSC_DIR, 'internal/checker/relater.go')
    src = open(relater).read()
    for old, new in PATCHES:
        if src.count(old) != 1:
            sys.exit(f'reltrace: relater.go changed, a patch no longer applies:\n{old}')
        src = src.replace(old, new)
    replace = dict(tsgotrace.oracle_overlay())
    h = hashlib.sha256((src + HOOK).encode())
    for k in sorted(replace):
        h.update(k.encode() + b'\0' + open(replace[k], 'rb').read())
    if out is None:
        out = f'/tmp/reltrace-{tsgotrace.tsgo_commit()[:8]}-{h.hexdigest()[:12]}'
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, 'relater.go'), 'w').write(src)
    open(os.path.join(out, 'tttt_reltrace.go'), 'w').write(HOOK)
    replace[relater] = os.path.join(out, 'relater.go')
    replace[os.path.join(TSC_DIR, 'internal/checker/tttt_reltrace.go')] = os.path.join(out, 'tttt_reltrace.go')
    with open(os.path.join(out, 'overlay.json'), 'w') as f:
        json.dump({'Replace': replace}, f, indent=1)
    return out


def build(out):
    exe = os.path.join(out, 'tttt-oracle')
    if os.path.exists(exe):
        return exe
    print(f'reltrace: building the instrumented tttt-oracle into {out} (~1-3 min)...', file=sys.stderr)
    p = subprocess.run(['bash', '-c', f'{tsgotrace.go_env()}\ngo build -overlay {out}/overlay.json -o {exe}.tmp ./cmd/tttt-oracle'],
                       cwd=TSC_DIR)
    if p.returncode:
        sys.exit(f'reltrace: the build failed (overlay: {out}/overlay.json)')
    os.rename(exe + '.tmp', exe)
    return exe


TTTT_RELATIONS = ['assignable', 'comparable', 'subtype', 'strictSubtype', 'identity']
TTTT_TERNARY = {'0': 'F', '1': 'U', '3': 'M', '7': 'T'}
ANSI = re.compile(r'\x1b\[[0-9;]*m|\x1b\]8;;[^\x1b]*\x1b\\')


def normalize_tttt(line):
    line = ANSI.sub('', line)
    m = re.match(r'^\S+:\d+: info: (.*)$', line)
    if not m:
        return None
    body = m[1]
    m = re.match(r"^nested (\d+) \((\d+)/(\d+)\): (.*): (\d+)( \(unreliable\))?( \(deeply nested\))?$", body)
    if m:
        return f"nested {m[1]} ({m[2]}/{m[3]}): {m[4]}: {TTTT_TERNARY.get(m[5], m[5])}{' deep' if m[7] else ''}"
    m = re.match(r"^on the maybe stack (\d+): (.*)$", body)
    if m:
        return f'maybe {m[1]}: {m[2]}'
    m = re.match(r"^relation (\d+): '(.*)' #\d+ -> '(.*)' #\d+: (yes|no)$", body)
    if m:
        kind = TTTT_RELATIONS[int(m[1])] if int(m[1]) < len(TTTT_RELATIONS) else m[1]
        return f"relation {kind}: '{m[2]}' -> '{m[3]}': {m[4]}"
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('target', nargs='?', help='a .ts file, a tsconfig.json or its directory')
    ap.add_argument('extra', nargs='*', help='after --: compiler options for a single file')
    ap.add_argument('--filter', help='keep only the lines containing this text')
    ap.add_argument('--context', type=int, default=40, help='lines of the first differences to print (default 40)')
    ap.add_argument('--check', action='store_true', help='only apply the patches and compile')
    args = ap.parse_args()
    if args.check:
        with tempfile.TemporaryDirectory(prefix='reltrace-check-') as d:
            out = prepare(d)
            p = subprocess.run(['bash', '-c', f'{tsgotrace.go_env()}\ngo build -overlay {out}/overlay.json -o /dev/null ./cmd/tttt-oracle'],
                               cwd=TSC_DIR)
        print('reltrace: overlay OK' if p.returncode == 0 else 'reltrace: overlay BROKEN')
        sys.exit(p.returncode)
    if not args.target:
        ap.error('TARGET is needed')
    subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')], check=True, stdout=subprocess.DEVNULL)
    exe = build(prepare())
    subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
    run_dir = tempfile.mkdtemp(prefix='reltrace-run-')
    target = os.path.abspath(args.target)
    if os.path.isfile(target) and not target.endswith('.json'):
        opts, rest = {}, list(args.extra)
        while rest:
            k = rest.pop(0).lstrip('-')
            v = rest.pop(0) if rest and not rest[0].startswith('--') else 'true'
            opts[k] = {'true': True, 'false': False}.get(v, v)
        project = os.path.join(run_dir, 'project')
        os.makedirs(project)
        with open(os.path.join(project, 'tsconfig.json'), 'w') as f:
            json.dump({'compilerOptions': opts, 'files': [target]}, f)
    else:
        project = target
    p = subprocess.run([exe, '--text'], input=project + '\n', stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                       errors='replace', env=dict(os.environ, TSGO_RELTRACE='1'), cwd=ROOT, timeout=1800)
    tsgo = [l[len('RELTRACE '):] for l in p.stderr.splitlines() if l.startswith('RELTRACE ')]
    env = dict(os.environ, TTTT_DEBUG_RELATE='1', TTTT_DEBUG_RELATE_NESTED='1')
    r = tttt_run.run([os.path.join(ROOT, 'release/tttt'), '-p', project, '--noEmit', '--checkers', '1', '--threads', '1'],
                     ROOT, timeout=1800, env=env)
    if r.status != 'ok':
        print(f'reltrace: tttt {r.status}', file=sys.stderr)
    tttt = [n for n in (normalize_tttt(l) for l in r.out.decode(errors='replace').splitlines()) if n is not None]
    if args.filter:
        tsgo = [l for l in tsgo if args.filter in l]
        tttt = [l for l in tttt if args.filter in l]
    for side, lines in (('tsgo', tsgo), ('tttt', tttt)):
        with open(os.path.join(run_dir, f'{side}.log'), 'w') as f:
            f.write('\n'.join(lines) + '\n')
    print(f'tsgo {len(tsgo)} lines, tttt {len(tttt)}: {run_dir}/tsgo.log and tttt.log (rm -r {run_dir} when done)')
    # (The depths left out of the comparison: tttt counts some steps tsc takes without recursiveTypeRelatedTo, a
    # union member against a union, so they are often off by one where the comparisons agree)
    def key(l):
        return re.sub(r'^(nested|maybe) \d+( \(\d+/\d+\))?:', r'\1:', l)
    shown = 0
    for line in difflib.unified_diff([key(l) for l in tsgo], [key(l) for l in tttt], 'tsgo', 'tttt', n=2, lineterm=''):
        print(line[:300])
        shown += 1
        if shown >= args.context:
            print('...')
            break
    if not shown:
        print('the logs are identical')
    if os.path.exists(os.path.join(run_dir, 'project')):
        shutil.rmtree(os.path.join(run_dir, 'project'))


if __name__ == '__main__':
    main()
