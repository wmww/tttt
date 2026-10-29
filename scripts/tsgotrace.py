#!/usr/bin/env python3
"""Where does tsgo report this? tsgo 7.1 instrumented through a Go overlay (agents/notes/instrumenting_tsgo.md):
the Go stack at the creation of every diagnostic with a given code, and whether it reached a diagnostics collection.

    scripts/tsgotrace.py 2339 PROJECT              # a tsconfig.json or its directory, through tttt-oracle (what
                                                   # `tsc -p P --noEmit` reports: build info hidden, single-threaded)
    scripts/tsgotrace.py 2339 FILE.ts [-- --strict --target esnext]
                                                   # one file (a generated tsconfig: files [FILE], the options given)
    scripts/tsgotrace.py 2339 --suite FILE.ts      # one suite-style test (@option headers) through tsgo's own
                                                   # compiler test runner (scripts/tsgoprobe.sh's): the harness can
                                                   # differ from the CLI on order-dependent cycles
    scripts/tsgotrace.py 2339,2322 P --funcs getTypeOfSymbol,checkSourceElement [--max 20]
                                                   # also a stack at each entry of the named functions (any function
                                                   # of internal/checker; the first --max calls of each)
    scripts/tsgotrace.py --check                   # only apply the patches and build (scripts/overlaycheck.sh)

For each matching diagnostic: its code and location, `kept` (added to a diagnostics collection: reported, unless
another filter drops it), `duplicate` (an equal one was there) or `not added` (an elaboration chain or related
information, a parser or program diagnostic, which go elsewhere, or a speculative one discarded), its message
arguments, and the stack from the diagnostic's creation down to the checker's entry point. Then tsgo's own output
lines with that code, to match them up.

Patches: internal/ast/diagnostic.go (NewDiagnostic and DiagnosticsCollection.Add wrapped), and with --funcs the
named functions' files. The shared checkout is never written: the patched copies and the overlay map live in
/tmp/tsgotrace-<tsgo commit>-<patch hash>/ with the binaries, reused while the patch set and test/oracle are the same
(the first build ~1-3 min; Go's build cache is the shared one, capped by ensure-typescript.sh).
"""
import argparse
import ast
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), '..'))
TSC_DIR = os.path.realpath(os.path.join(ROOT, 'thirdparty/typescript/tsc'))
ORACLE_DIR = os.path.join(ROOT, 'test/oracle')

HOOK_AST = r'''package ast

import (
	"fmt"
	"os"
	"runtime"
	"strconv"
	"strings"

	"github.com/microsoft/TypeScript/tsc/internal/core"
	"github.com/microsoft/TypeScript/tsc/internal/diagnostics"
)

var ttttTraceCodes = func() map[int32]bool {
	ret := map[int32]bool{}
	for _, s := range strings.Split(os.Getenv("TSGO_TRACE_CODE"), ",") {
		if n, err := strconv.Atoi(strings.TrimPrefix(strings.TrimSpace(s), "TS")); err == nil {
			ret[int32(n)] = true
		}
	}
	return ret
}()

// TtttTraceStack prints the caller's stack, `skip` frames above it left out, as `TSGOTRACE   func file:line` lines
func TtttTraceStack(skip int) {
	pcs := make([]uintptr, 80)
	n := runtime.Callers(skip+2, pcs)
	frames := runtime.CallersFrames(pcs[:n])
	var b strings.Builder
	for {
		f, more := frames.Next()
		name := f.Function
		if i := strings.LastIndex(name, "/"); i >= 0 {
			name = name[i+1:]
		}
		if strings.HasPrefix(name, "runtime.") || strings.HasPrefix(name, "testing.") {
			break
		}
		file := f.File
		if i := strings.LastIndex(file, "/internal/"); i >= 0 {
			file = file[i+len("/internal/"):]
		}
		fmt.Fprintf(&b, "TSGOTRACE   %s %s:%d\n", name, file, f.Line)
		if !more {
			break
		}
	}
	os.Stderr.WriteString(b.String())
}

func ttttDiagWhere(d *Diagnostic) string {
	name := "-"
	if d.file != nil {
		name = string(d.file.FileName())
	}
	return fmt.Sprintf("%s|%d|%d", name, d.loc.Pos(), d.loc.End())
}

func NewDiagnostic(file *SourceFile, loc core.TextRange, message *diagnostics.Message, args ...any) *Diagnostic {
	d := ttttNewDiagnosticOrig(file, loc, message, args...)
	if ttttTraceCodes[d.code] {
		fmt.Fprintf(os.Stderr, "TSGOTRACE new %p %d %s %s\n", d, d.code, ttttDiagWhere(d), strconv.Quote(strings.Join(d.messageArgs, "\x1f")))
		TtttTraceStack(1)
	}
	return d
}

func (c *DiagnosticsCollection) Add(diagnostic *Diagnostic) *Diagnostic {
	r := c.ttttAddOrig(diagnostic)
	if ttttTraceCodes[diagnostic.code] {
		fmt.Fprintf(os.Stderr, "TSGOTRACE added %p %v\n", diagnostic, r != diagnostic)
	}
	return r
}
'''

HOOK_CHECKER = r'''package checker

import (
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync"

	"github.com/microsoft/TypeScript/tsc/internal/ast"
)

var ttttTraceFuncs = func() map[string]bool {
	ret := map[string]bool{}
	for _, s := range strings.Split(os.Getenv("TSGO_TRACE_FUNCS"), ",") {
		if s != "" {
			ret[s] = true
		}
	}
	return ret
}()
var ttttTraceMax = func() int {
	n, err := strconv.Atoi(os.Getenv("TSGO_TRACE_MAX"))
	if err != nil {
		return 50
	}
	return n
}()
var ttttTraceMu sync.Mutex
var ttttTraceCount = map[string]int{}

func ttttFuncHook(name string, node *ast.Node) {
	if !ttttTraceFuncs[name] {
		return
	}
	ttttTraceMu.Lock()
	defer ttttTraceMu.Unlock()
	ttttTraceCount[name]++
	if ttttTraceCount[name] > ttttTraceMax {
		return
	}
	where := "(no current node)|-1|-1"
	if node != nil {
		if sf := ast.GetSourceFileOfNode(node); sf != nil {
			where = fmt.Sprintf("%s|%d|%d", string(sf.FileName()), node.Pos(), node.End())
		}
	}
	fmt.Fprintf(os.Stderr, "TSGOTRACE func %s %d %s\n", name, ttttTraceCount[name], where)
	ast.TtttTraceStack(1)
}
'''


def patch_ast(src):
    for old, new in (('func NewDiagnostic(', 'func ttttNewDiagnosticOrig('),
                     ('func (c *DiagnosticsCollection) Add(', 'func (c *DiagnosticsCollection) ttttAddOrig(')):
        if src.count(old) != 1:
            sys.exit(f'tsgotrace: internal/ast/diagnostic.go changed, `{old}` is no longer there once')
        src = src.replace(old, new)
    return src


def patch_funcs(funcs):
    """{original path: patched text} for the files of internal/checker defining the functions"""
    checker = os.path.join(TSC_DIR, 'internal/checker')
    texts = {}
    found = set()
    for name in sorted(os.listdir(checker)):
        if not name.endswith('.go') or name.endswith('_test.go'):
            continue
        path = os.path.join(checker, name)
        lines = open(path).read().split('\n')
        changed = False
        i = 0
        while i < len(lines):
            m = re.match(r'func (\((\w+) \*(\w+)\) )?(\w+)[\[(]', lines[i])
            if m and m.group(4) in funcs:
                j = i
                while not lines[j].endswith('{'):
                    j += 1
                node = f'{m.group(2)}.currentNode' if m.group(3) == 'Checker' else 'nil'
                lines.insert(j + 1, f'\tttttFuncHook("{m.group(4)}", {node})')
                found.add(m.group(4))
                changed = True
                i = j + 1
            i += 1
        if changed:
            texts[path] = '\n'.join(lines)
    missing = set(funcs) - found
    if missing:
        sys.exit(f'tsgotrace: no function {", ".join(sorted(missing))} in internal/checker')
    return texts


def oracle_overlay():
    """test/oracle's own overlay (ensure-typescript.sh's OVERLAY list), for the oracle and the probe"""
    text = open(os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')).read()
    body = re.search(r'\nOVERLAY=\((.*?)\n\)', text, re.S).group(1)
    words = body.split()
    return {os.path.join(TSC_DIR, words[i + 1]): os.path.realpath(os.path.join(ORACLE_DIR, words[i]))
            for i in range(0, len(words), 2)}


def go_env():
    return subprocess.check_output([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh'), '--env'], text=True)


def tsgo_commit():
    return subprocess.check_output(['git', '-C', TSC_DIR, 'rev-parse', 'HEAD'], text=True).strip()


def prepare(funcs, out=None):
    """Writes the patched files and overlay.json into the build directory, returned"""
    replace = dict(oracle_overlay())
    texts = {}
    diag = os.path.join(TSC_DIR, 'internal/ast/diagnostic.go')
    texts[diag] = patch_ast(open(diag).read())
    texts[os.path.join(TSC_DIR, 'internal/ast/tttt_trace.go')] = HOOK_AST
    texts[os.path.join(TSC_DIR, 'internal/checker/tttt_trace.go')] = HOOK_CHECKER
    texts.update(patch_funcs(funcs))
    h = hashlib.sha256()
    for k in sorted(texts):
        h.update(k.encode() + b'\0' + texts[k].encode() + b'\0')
    for k in sorted(replace):
        h.update(k.encode() + b'\0' + open(replace[k], 'rb').read())
    if out is None:
        out = f'/tmp/tsgotrace-{tsgo_commit()[:8]}-{h.hexdigest()[:12]}'
    os.makedirs(out, exist_ok=True)
    for i, (orig, text) in enumerate(sorted(texts.items())):
        copy = os.path.join(out, f'{i}-{os.path.basename(orig)}')
        with open(copy, 'w') as f:
            f.write(text)
        replace[orig] = copy
    with open(os.path.join(out, 'overlay.json'), 'w') as f:
        json.dump({'Replace': replace}, f, indent=1)
    return out


def build(out, what):
    """what: tsc, oracle or probe; returns the binary"""
    exe = os.path.join(out, {'tsc': 'tsc', 'oracle': 'tttt-oracle', 'probe': 'tttt-probe.test'}[what])
    if os.path.exists(exe):
        return exe
    cmd = {'tsc': f'go build -overlay {out}/overlay.json -o {exe}.tmp ./cmd/tsc',
           'oracle': f'go build -overlay {out}/overlay.json -o {exe}.tmp ./cmd/tttt-oracle',
           'probe': f'go test -overlay {out}/overlay.json -c -o {exe}.tmp ./internal/testrunner'}[what]
    print(f'tsgotrace: building the instrumented {what} into {out} (~1-3 min)...', file=sys.stderr)
    p = subprocess.run(['bash', '-c', f'{go_env()}\n{cmd}'], cwd=TSC_DIR)
    if p.returncode:
        sys.exit(f'tsgotrace: the build failed (overlay: {out}/overlay.json)')
    os.rename(exe + '.tmp', exe)
    return exe


# The suite harness's files are virtual (/.src/NAME): their texts, the test file split at its @filename headers
SUITE_UNITS = {}


def suite_units(target):
    name, units = os.path.basename(target), {}
    cur = []
    for line in open(target, encoding='utf-8', errors='replace').read().split('\n'):
        m = re.match(r'\s*//\s*@(\w+)\s*:\s*(.*?)\s*$', line)
        if m and m.group(1).lower() == 'filename':
            if cur and any(l.strip() for l in cur):
                units[name] = '\n'.join(cur)
            name, cur = m.group(2), []
        elif not m:
            cur.append(line)
    units[name] = '\n'.join(cur)
    for n, text in units.items():
        SUITE_UNITS['/.src/' + n.lstrip('/')] = text


def line_col(path, pos, cache={}):
    if path in SUITE_UNITS:
        cache[path] = SUITE_UNITS[path]
    if path not in cache:
        try:
            cache[path] = open(path, encoding='utf-8', errors='replace').read()
        except OSError:
            cache[path] = None
    text = cache[path]
    if text is None or pos < 0:
        return f'{path} @{pos}'
    before = text[:pos]
    return f'{path}:{before.count(chr(10)) + 1}:{pos - (before.rfind(chr(10)) + 1) + 1}'


def where(field):
    if field.count('|') < 2:
        return '(no node)'  # (a function whose first argument isn't a node: getBaseConstructorTypeOfClass(t))
    path, pos, end = field.rsplit('|', 2)
    return line_col(path, int(pos)) if path != '-' else '(no file)'


def report(stderr, codes):
    """The trace lines of stderr, grouped per diagnostic / function entry"""
    events = []
    added = {}
    cur = None
    for line in stderr.splitlines():
        if not line.startswith('TSGOTRACE '):
            continue
        rest = line[len('TSGOTRACE '):]
        if rest.startswith('  '):
            if cur is not None:
                cur['stack'].append(rest.strip())
            continue
        parts = rest.split(' ', 4)
        if parts[0] == 'new':
            cur = {'kind': 'new', 'id': parts[1], 'code': parts[2], 'where': where(parts[3]),
                   'args': ast.literal_eval(parts[4]).split('\x1f') if len(parts) > 4 else [], 'stack': []}
            events.append(cur)
        elif parts[0] == 'added':
            added[parts[1]] = parts[2] == 'true'
            cur = None
        elif parts[0] == 'func':
            cur = {'kind': 'func', 'name': parts[1], 'n': parts[2], 'where': where(parts[3]), 'stack': []}
            events.append(cur)
    ndiag = 0
    for e in events:
        if e['kind'] == 'new':
            ndiag += 1
            status = ('duplicate' if added[e['id']] else 'kept') if e['id'] in added else 'not added'
            args = ', '.join(repr(a) for a in e['args'] if a)
            print(f"TS{e['code']} {e['where']}  [{status}]{'  args: ' + args if args else ''}")
        else:
            print(f"call {e['name']} #{e['n']} at {e['where']}")
        stack = e['stack']
        # (Below the checker's entry point it is the program's driver, the same every time)
        last = max((i for i, f in enumerate(stack) if f.startswith('checker.')), default=len(stack) - 1)
        for frame in stack[:last + 1]:
            print(f'    {frame}')
        if last + 1 < len(stack):
            print(f'    ... {len(stack) - last - 1} frames of the driver ({stack[last + 1].split()[0]} ...)')
        print()
    print(f'{ndiag} diagnostics with code {",".join(codes)} made; '
          f'{sum(1 for e in events if e["kind"] == "new" and added.get(e["id"]) is False)} kept')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('code', nargs='?', help='diagnostic code(s), comma separated (2339 or TS2339)')
    ap.add_argument('target', nargs='?', help='a tsconfig.json, its directory, or a .ts file')
    ap.add_argument('extra', nargs='*', help='after --: options for a single file')
    ap.add_argument('--suite', action='store_true', help="run TARGET (a suite-style test file) through tsgo's test runner")
    ap.add_argument('--funcs', default='', help='checker functions to print a stack at (comma separated)')
    ap.add_argument('--max', type=int, default=50, help='stacks per --funcs function (default 50)')
    ap.add_argument('--check', action='store_true', help='only apply the patches and build the instrumented tsc')
    ap.add_argument('--raw', action='store_true', help="print tsgo's stderr as it is")
    args = ap.parse_args()
    funcs = [f for f in args.funcs.split(',') if f]
    if args.check:
        with tempfile.TemporaryDirectory(prefix='tsgotrace-check-') as d:
            out = prepare(funcs, d)
            p = subprocess.run(['bash', '-c', f'{go_env()}\ngo build -overlay {out}/overlay.json -o /dev/null ./cmd/tsc '
                                f'./cmd/tttt-oracle && go vet -overlay {out}/overlay.json ./internal/ast >/dev/null'],
                               cwd=TSC_DIR)
        print('tsgotrace: overlay OK' if p.returncode == 0 else 'tsgotrace: overlay BROKEN')
        sys.exit(p.returncode)
    if not args.code or not args.target:
        ap.error('CODE and TARGET are needed')
    subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')], check=True, stdout=subprocess.DEVNULL)
    codes = [c.strip().removeprefix('TS') for c in args.code.split(',')]
    out = prepare(funcs)
    env = dict(os.environ, TSGO_TRACE_CODE=','.join(codes), TSGO_TRACE_FUNCS=','.join(funcs), TSGO_TRACE_MAX=str(args.max))
    target = os.path.abspath(args.target)
    scratch = tempfile.mkdtemp(prefix='tsgotrace-run-')
    try:
        if args.suite:
            exe = build(out, 'probe')
            suite_units(target)
            cases = os.path.join(scratch, 'tests/cases/compiler')
            os.makedirs(cases)
            shutil.copy(target, cases)
            os.symlink(os.path.join(TSC_DIR, 'testdata/tests/lib'), os.path.join(scratch, 'tests/lib'))
            p = subprocess.run([exe, '-test.run', '^TestTtttProbe$', '-test.timeout', '0'], cwd=os.path.join(TSC_DIR, 'internal/testrunner'),
                               env=dict(env, TTTT_TESTDATA=scratch), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, errors='replace', timeout=900)
            stdout = ''
            base = os.path.splitext(os.path.basename(target))[0]
            local = os.path.join(scratch, 'baselines/local/compiler')
            for f in sorted(os.listdir(local)) if os.path.isdir(local) else []:
                if f.startswith(base) and f.endswith('.errors.txt'):
                    stdout += open(os.path.join(local, f)).read()
        else:
            exe = build(out, 'oracle')
            if os.path.isfile(target) and not target.endswith('.json'):
                # (tttt-oracle takes projects: a generated one, options as given after --)
                opts = {}
                rest = list(args.extra)
                while rest:
                    k = rest.pop(0).lstrip('-')
                    v = rest.pop(0) if rest and not rest[0].startswith('--') else True
                    opts[k] = {'true': True, 'false': False}.get(v, v) if isinstance(v, str) else v
                with open(os.path.join(scratch, 'tsconfig.json'), 'w') as f:
                    json.dump({'compilerOptions': opts, 'files': [target]}, f)
                project = scratch
            else:
                project = target
            p = subprocess.run([exe, '--text'], input=project + '\n', stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, errors='replace', env=env, cwd=ROOT, timeout=1800)
            try:
                stdout = json.loads(p.stdout.splitlines()[0]).get('text', '') if p.stdout.strip() else ''
            except (json.JSONDecodeError, IndexError):
                stdout = p.stdout
        if args.raw:
            sys.stderr.write(p.stderr)
        else:
            other = [l for l in p.stderr.splitlines() if l and not l.startswith('TSGOTRACE ')]
            if other:
                print('tsgo stderr:\n  ' + '\n  '.join(other[:20]) + '\n')
        report(p.stderr, codes)
        mine = [l for l in stdout.splitlines() if any(f'TS{c}:' in l or f'TS{c} ' in l for c in codes)]
        print(f"tsgo's output with {','.join('TS' + c for c in codes)} ({len(mine)}):")
        for l in mine[:50]:
            print('  ' + l)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == '__main__':
    main()
