#!/usr/bin/env python3
"""A safe repro reducer: a divergence between tsgo and tttt on a real project, cut down to a test/cases file.

  scripts/reduce.py --project DIR [--config tsconfig.json] --keep TS7022 --tttt-has   # spurious in tttt
  scripts/reduce.py --project DIR --keep "TS2345.*'foo'"            # --differ (default): either side has it alone
  scripts/reduce.py --from /tmp/reduce-XXXX --files 'node_modules/*' --keep TS7022 --tttt-has   # then the packages

It copies the files of the project's program (tsgo's --listFilesOnly, its bundled libs aside) into a fresh
/tmp/reduce-*/ as regular files: the project's own files at their place under the repository root (git's toplevel,
--repo), the packages into the node_modules where resolution finds them (a symlinked package, pnpm's and workspace
links, is copied where the link was; a package only reached from inside .pnpm goes to the root's node_modules), each
package.json on the way, and a generated tsconfig (tsgo's --showConfig of the original: `extends` flattened, its root
files listed, incremental/composite/references dropped) where the original config was. Both compilers then run on the
copy only, in a bwrap sandbox whose one writable directory is the copy (tsgo `--noEmit --pretty false`, release/tttt
`--threads 1 --noEmit --pretty false` plus --tttt-args). The predicate: a diagnostic line (`file(line): TSxxxx
message`) that one side has and the other doesn't, by (file, line, code), matches --keep (--differ, the default:
either side; --tsgo-has: tsgo's alone; --tttt-has: tttt's alone). The copy must reproduce it first (exit 1 otherwise,
with the copy's differences: a package the layout lost, a version conflict it printed).

The reducible files are the project's own (under DIR, not in node_modules) unless --files GLOBs (fnmatch against the
path relative to the copy root, repeatable) say otherwise. Whole files first, chunks of files at a time (halving down
to one): `export {};`, then those deleted (and out of the config's files); then each file left by bracket-balanced
ranges (a block whole, else unwrapped) and line halving, repeated while either shrinks it. A long single line stays
whole. --from continues on an earlier copy in place: reduce the project's files first, then the packages with
`--files 'node_modules/*'` (kebo's TS7022: 353 own files to 18 lines in ~360 runs, then 2519 package files to 4 files
and 16 lines in ~210, a few minutes each).

The end: the files left, the differing lines, and `case.ts` in the copy: a test/cases file with the generated config's
options as `// @` headers and every program file as a `// @filename:` section, paths relative to the copy root (the
package.json files above them cut to the fields resolution reads); over 400 lines only the reduced files are inlined
and the others listed. Check it with `FAST=1 scripts/filetest.sh` before copying it into test/cases.

Safety (a reducer once followed a symlink and destroyed 690 shared files): every write and delete goes through
`guard()`, which asserts the real directory is under the copy root and that the path is no symlink; the copy is checked
to contain no symlink before anything runs (and --from takes only /tmp/reduce-* directories); the compilers can't write
outside it. The copy stays for inspection and --from: remove it with `rm -r /tmp/reduce-XXXX` (the path is printed).
"""
import argparse, concurrent.futures, fnmatch, json, os, re, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tscformat

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSGO = os.path.join(ROOT, 'thirdparty/typescript/built/tsc')
TTTT = os.path.join(ROOT, 'release/tttt')
SCRATCH = None  # the copy root, a real path
CODE_EXT = ('.ts', '.tsx', '.mts', '.cts', '.js', '.jsx', '.mjs', '.cjs')


def guard(path):
    """`path` (absolute) may be written or deleted: its directory's real path is under the copy root and it is no
    symlink. Every write goes through here"""
    assert SCRATCH and os.path.isabs(path), path
    real_dir = os.path.realpath(os.path.dirname(path))
    if not (real_dir == SCRATCH or real_dir.startswith(SCRATCH + '/')):
        sys.exit(f'refusing to write {path}: its real directory {real_dir} is outside {SCRATCH}')
    if os.path.islink(path):
        sys.exit(f'refusing to write {path}: a symlink')
    return path


def write(path, data):
    d = os.path.dirname(path)
    if not os.path.isdir(d):
        guard(d)
        os.makedirs(d)
    with open(guard(path), 'w' if isinstance(data, str) else 'wb') as f:
        f.write(data)


def remove(path):
    os.unlink(guard(path))


def check_no_symlinks():
    for dp, dn, fn in os.walk(SCRATCH):
        for n in dn + fn:
            p = os.path.join(dp, n)
            if os.path.islink(p) or not os.path.realpath(p).startswith(SCRATCH + '/'):
                sys.exit(f'the copy has a symlink or escapes the root: {p}')


def run(args, cwd, timeout=600):
    sandbox = ['bwrap', '--ro-bind', '/', '/', '--bind', SCRATCH, SCRATCH, '--dev', '/dev', '--proc', '/proc',
               '--die-with-parent', '--'] if shutil.which('bwrap') else []
    try:
        r = subprocess.run(sandbox + args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        return r.stdout.decode(errors='replace')
    except subprocess.TimeoutExpired:
        return 'TIMEOUT'


def package_root(path):
    """(package root, name) of a file inside node_modules: the directory after the last node_modules segment"""
    parts = path.split('/')
    i = max(j for j, p in enumerate(parts) if p == 'node_modules')
    n = 2 if parts[i + 1].startswith('@') else 1
    return '/'.join(parts[:i + 1 + n]), '/'.join(parts[i + 1:i + 1 + n])


def package_jsons(path, top):
    """The package.json files in the directories from path's up to top (both included)"""
    ret, d = [], os.path.dirname(path)
    while True:
        if os.path.isfile(os.path.join(d, 'package.json')):
            ret.append(os.path.join(d, 'package.json'))
        if d == top or len(d) <= len(top) or d == '/':
            return ret
        d = os.path.dirname(d)


def layout(project, config, files, repo):
    """{original real path: [copy-relative paths]} for the program's files and the package.json files they need"""
    placed = {}

    def place(orig, rel):
        placed.setdefault(orig, [])
        if rel not in placed[orig]:
            placed[orig].append(rel)

    taken = {}  # copy-relative path -> original, to report conflicts
    def place_checked(orig, rel):
        if taken.setdefault(rel, orig) != orig:
            print(f'conflict: {rel} is both {taken[rel]} and {orig} (the first kept)', file=sys.stderr)
            return
        place(orig, rel)

    in_repo = lambda p: p.startswith(repo + '/')
    in_place = [f for f in files if in_repo(f) and '/node_modules/' not in f[len(repo):] + '/']
    rest = set(files) - set(in_place)
    for f in in_place:
        place_checked(f, os.path.relpath(f, repo))
        for pj in package_jsons(f, repo):
            place_checked(pj, os.path.relpath(pj, repo))
    # Links in the node_modules directories on the way from each in-place file up to the root: where resolution looks
    dirs = set()
    for f in in_place + [config]:
        d = os.path.dirname(f)
        while in_repo(d) or d == repo:
            dirs.add(d)
            if d == repo:
                break
            d = os.path.dirname(d)
    remaining = set(rest)
    for d in sorted(dirs):
        nm = os.path.join(d, 'node_modules')
        if not os.path.isdir(nm):
            continue
        entries = []
        for e in os.listdir(nm):
            if e.startswith('@') and os.path.isdir(os.path.join(nm, e)) and not os.path.islink(os.path.join(nm, e)):
                entries += [f'{e}/{s}' for s in os.listdir(os.path.join(nm, e))]
            elif not e.startswith('.'):
                entries.append(e)
        for e in entries:
            target = os.path.realpath(os.path.join(nm, e))
            under = [f for f in files if f.startswith(target + '/')]
            for f in under:
                place_checked(f, os.path.relpath(os.path.join(nm, e, os.path.relpath(f, target)), repo))
                remaining.discard(f)
                for pj in package_jsons(f, target):
                    place_checked(pj, os.path.relpath(os.path.join(nm, e, os.path.relpath(pj, target)), repo))
    # What no link reaches: a package only another package depends on (inside .pnpm), or outside the repository:
    # flattened into the root's node_modules
    for f in sorted(remaining):
        if '/node_modules/' not in f:
            sys.exit(f'{f}: outside {repo} and in no package; pass a --repo above it')
        root, name = package_root(f)
        place_checked(f, os.path.join('node_modules', name, os.path.relpath(f, root)))
        for pj in package_jsons(f, root):
            place_checked(pj, os.path.join('node_modules', name, os.path.relpath(pj, root)))
    return placed


def generated_config(project, config):
    out = subprocess.run([TSGO, '-p', config, '--showConfig'], cwd=project, capture_output=True, text=True)
    if out.returncode:
        sys.exit(f'tsgo --showConfig failed:\n{out.stdout}{out.stderr}')
    cfg = json.loads(out.stdout)
    opts = cfg.setdefault('compilerOptions', {})
    for k in ('incremental', 'composite', 'tsBuildInfoFile', 'emitDeclarationOnly', 'declarationMap', 'noEmit'):
        opts.pop(k, None)
    for k in ('references', 'include', 'exclude', 'watchOptions'):
        cfg.pop(k, None)
    return cfg


def diags(text):
    """{(file, line, code): line text} of a --pretty false output"""
    return {(f, l, c): f'{f}({l}): TS{c} {m}' for f, l, _, c, m in tscformat.parse(text)}


class Probe:
    def __init__(self, args, cwd, config_rel):
        self.args, self.cwd, self.config = args, cwd, config_rel
        self.runs = 0
        self.tsgo_baseline = None

    def outputs(self):
        cfg = os.path.join(self.cwd, self.config)
        with concurrent.futures.ThreadPoolExecutor(2) as ex:
            a = ex.submit(run, [TSGO, '-p', cfg, '--noEmit', '--pretty', 'false'], self.cwd)
            b = ex.submit(run, [TTTT, '--pretty', 'false', '--threads', '1', '--noEmit', '-p', cfg,
                                *self.args.tttt_args.split()], self.cwd)
            return a.result(), b.result()

    def differing(self):
        tsgo, tttt = self.outputs()
        self.runs += 1
        a, b = diags(tsgo), diags(tttt)
        return ([a[k] for k in sorted(set(a) - set(b))], [b[k] for k in sorted(set(b) - set(a))], tsgo, tttt)

    def holds(self):
        only_tsgo, only_tttt, tsgo, tttt = self.differing()
        if 'TIMEOUT' in (tsgo, tttt):
            return False
        # (--tsgo-clean: a cut that makes tsgo report more than the copy did at first, an unresolved name the cut
        # left, is no repro)
        if self.args.tsgo_clean:
            count = len(diags(tsgo))
            if self.tsgo_baseline is None:
                self.tsgo_baseline = count
            if count > self.tsgo_baseline:
                return False
        lines = (only_tsgo if self.args.mode != 'tttt' else []) + (only_tttt if self.args.mode != 'tsgo' else [])
        return any(re.search(self.args.keep, l) for l in lines)


def ddmin(items, test):
    """The items left after removing chunks (half of them, then halving down to one at a time, repeated at one
    while that still removes some) as long as test(the items left) holds"""
    n = max(1, len(items) // 2)
    while True:
        changed, i = False, 0
        while i < len(items):
            trial = items[:i] + items[i + n:]
            if test(trial):
                items, changed = trial, True
            else:
                i += n
        if n > 1:
            n //= 2
        elif not changed or not items:
            return items


class Files:
    """The reducible files' states on disk: 'text' (text_of's lines), 'stub' (`export {};`) or 'gone'"""
    def __init__(self, text_of, config):
        self.text_of, self.state, self.config = text_of, {p: 'text' for p in text_of}, config

    def set(self, p, state):
        if self.state[p] == state:
            return
        if state == 'gone':
            remove(p)
        else:
            write(p, 'export {};\n' if state == 'stub' else '\n'.join(self.text_of[p]))
        self.state[p] = state
        self.config.write(gone={q for q, s in self.state.items() if s == 'gone'})


class Config:
    """The generated tsconfig in the copy, its `files` without the deleted ones (a missing root is TS6053, which gates
    the check)"""
    def __init__(self, path, cfg):
        self.path, self.cfg, self.roots = path, cfg, list(cfg.get('files', []))
        self.written = None

    def write(self, gone=()):
        d = os.path.dirname(self.path)
        files = [f for f in self.roots if os.path.normpath(os.path.join(d, f)) not in gone
                 and os.path.exists(os.path.join(d, f))]
        if files != self.written:
            write(self.path, json.dumps({**self.cfg, 'files': files}, indent=2) + '\n')
            self.written = files


def balanced(lines):
    """(i, j) of each line i that opens more brackets than it closes and the line j where they are closed again
    (naive: strings and comments count too)"""
    ret = []
    for i, l in enumerate(lines):
        depth = sum(l.count(c) for c in '({[') - sum(l.count(c) for c in ')}]')
        j = i
        while depth > 0 and j + 1 < len(lines):
            j += 1
            depth += sum(lines[j].count(c) for c in '({[') - sum(lines[j].count(c) for c in ')}]')
        if j > i and depth <= 0:
            ret.append((i, j))
    return ret


def reduce_lines(lines, test):
    """Bracket-balanced ranges (each removed whole, else its first and last lines alone: the block unwrapped), then
    ddmin on lines, until neither removes anything"""
    while True:
        before = len(lines)
        changed = True
        while changed:
            changed = False
            for i, j in balanced(lines):
                for trial in (lines[:i] + lines[j + 1:], lines[:i] + lines[i + 1:j] + lines[j + 1:]):
                    if test(trial):
                        lines, changed = trial, True
                        break
                if changed:
                    break
        lines = ddmin(lines, test)
        if len(lines) == before:
            return lines


CASE_LINES = 400  # (the program's files are all inlined up to this many lines, else only the reduced ones)
PACKAGE_FIELDS = ('name', 'version', 'type', 'main', 'module', 'types', 'typings', 'exports', 'imports', 'typesVersions')


def case_file(cfg, program, reduced):
    """A test/cases file: the generated config's options as headers, then each program file (paths relative to the copy
    root, where the harness resolves node_modules from) and the package.json files above them, cut to the fields
    resolution reads"""
    out = []
    skip = {'outDir', 'rootDir', 'declaration', 'sourceMap', 'removeComments', 'noEmitOnError', 'declarationDir'}
    for k, v in cfg.get('compilerOptions', {}).items():
        if k in skip:
            continue
        if isinstance(v, list):
            if all(isinstance(x, str) for x in v):
                out.append(f'// @{k}: {", ".join(v)}')
            continue
        if isinstance(v, dict):
            out.append(f'// ({k} is an object: not expressible as a header) {json.dumps(v)}')
            continue
        out.append(f'// @{k}: {str(v).lower() if isinstance(v, bool) else v}')
    texts = {p: [l for l in open(p).read().split('\n') if l.strip()] for p in program}
    inline = program if sum(map(len, texts.values())) <= CASE_LINES else [p for p in program if p in reduced]
    left_out = [os.path.relpath(p, SCRATCH) for p in program if p not in inline]
    if left_out:
        out.append(f'// (over {CASE_LINES} lines: {len(left_out)} unreduced program files left out: '
                   f'{", ".join(left_out[:6])}{" ..." if len(left_out) > 6 else ""})')
    pjs = sorted({pj for p in inline for pj in package_jsons(p, SCRATCH)})
    for pj in pjs:
        try:
            fields = {k: v for k, v in json.load(open(pj)).items() if k in PACKAGE_FIELDS}
        except ValueError:
            continue
        out.append(f'// @filename: {os.path.relpath(pj, SCRATCH)}')
        out.append(json.dumps(fields))
    for p in inline:
        out.append(f'// @filename: {os.path.relpath(p, SCRATCH)}')
        out += texts[p]
    return '\n'.join(out) + '\n', len(inline), left_out


def main():
    global SCRATCH
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--project', help='the project directory')
    ap.add_argument('--from', dest='from_copy', metavar='COPY',
                    help='continue on an earlier run\'s /tmp/reduce-* copy (in place) instead of a new copy of --project, '
                         'e.g. with --files naming the dependencies to cut down next')
    ap.add_argument('--config', default='tsconfig.json', help="the config, relative to --project")
    ap.add_argument('--keep', required=True, help='regex a differing diagnostic line must match (re.search)')
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--differ', dest='mode', action='store_const', const='either', help='either side (default)')
    g.add_argument('--tsgo-has', dest='mode', action='store_const', const='tsgo', help='tsgo reports it, tttt not')
    g.add_argument('--tttt-has', dest='mode', action='store_const', const='tttt', help='tttt reports it, tsgo not')
    ap.add_argument('--files', action='append', help='fnmatch globs (relative to the copy root) of the reducible files')
    ap.add_argument('--repo', help='the root the copy mirrors (default: the git toplevel of --project)')
    ap.add_argument('--tttt-args', default='', help='extra tttt flags, e.g. "--checkers 1"')
    ap.add_argument('--no-reduce', action='store_true', help='copy and check the predicate only')
    ap.add_argument('--tsgo-clean', action='store_true',
                    help='tsgo must report no more than the copy did at first (for a spurious tttt error: no cut leaves broken code)')
    args = ap.parse_args()
    args.mode = args.mode or 'either'
    if bool(args.project) == bool(args.from_copy):
        ap.error('one of --project and --from')

    subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
    if args.from_copy:
        SCRATCH = os.path.realpath(args.from_copy)
        if not re.fullmatch(r'/tmp/reduce-[^/]+', SCRATCH):
            sys.exit(f'--from wants a /tmp/reduce-* copy, not {SCRATCH}')
        check_no_symlinks()
        meta = json.load(open(os.path.join(SCRATCH, 'reduce.json')))
        cfg = meta['cfg']
        print(f'continuing on {SCRATCH}', flush=True)
    else:
        project = os.path.realpath(args.project)
        config = os.path.realpath(os.path.join(project, args.config))
        repo = os.path.realpath(args.repo) if args.repo else subprocess.run(
            ['git', '-C', project, 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip() or project
        repo = os.path.realpath(repo)
        if not (project + '/').startswith(repo + '/'):
            sys.exit(f'{project} is not under {repo}')

        listed = subprocess.run([TSGO, '-p', config, '--listFilesOnly'], cwd=project, capture_output=True, text=True).stdout
        # (tsgo's libs are bundled: `bundled:/libs/lib.es5.d.ts`)
        files = sorted({os.path.realpath(l) for l in listed.split('\n') if l.strip() and not l.startswith('bundled:')})
        if not files:
            sys.exit(f'tsgo --listFilesOnly listed nothing:\n{listed}')
        cfg = generated_config(project, config)
        placed = layout(project, config, files, repo)

        SCRATCH = os.path.realpath(tempfile.mkdtemp(prefix='reduce-', dir='/tmp'))
        for orig, rels in placed.items():
            data = open(orig, 'rb').read()
            for rel in rels:
                write(os.path.join(SCRATCH, rel), data)
        meta = {'project': os.path.relpath(project, repo), 'config': os.path.relpath(config, repo)}
        roots = [os.path.normpath(os.path.join(os.path.dirname(config), f)) for f in cfg.get('files', [])]
        cfg['files'] = [os.path.relpath(f, os.path.dirname(config)) for f in roots if os.path.realpath(f) in placed]
        meta['cfg'] = cfg
        write(os.path.join(SCRATCH, 'reduce.json'), json.dumps(meta, indent=2) + '\n')
        check_no_symlinks()
        copied = sum(len(r) for r in placed.values())
        print(f'copy: {SCRATCH} ({copied} files, {len(files)} in the program)', flush=True)

    prel = meta['project']
    cwd = os.path.normpath(os.path.join(SCRATCH, prel))
    config_copy = os.path.join(SCRATCH, meta['config'])
    config_writer = Config(config_copy, cfg)
    config_writer.write()
    probe = Probe(args, cwd, os.path.relpath(config_copy, cwd))
    only_tsgo, only_tttt, tsgo, tttt = probe.differing()
    if not probe.holds():
        print(f'the copy does not reproduce --keep {args.keep!r}; its differences:', file=sys.stderr)
        print('\n'.join([f'  tsgo only: {l}' for l in only_tsgo[:15]] + [f'  tttt only: {l}' for l in only_tttt[:15]]),
              file=sys.stderr)
        sys.exit(1)
    print(f'reproduces: {len(only_tsgo)} lines tsgo only, {len(only_tttt)} tttt only', flush=True)
    if args.no_reduce:
        return

    own = lambda rel: (prel == '.' or rel.startswith(prel + '/')) and '/node_modules/' not in '/' + rel \
        and rel.endswith(CODE_EXT)
    in_copy = sorted(os.path.relpath(os.path.join(dp, f), SCRATCH) for dp, _, fn in os.walk(SCRATCH) for f in fn
                     if os.path.join(dp, f) != os.path.join(SCRATCH, 'case.ts'))
    if args.files:
        reducible = [r for r in in_copy if any(fnmatch.fnmatch(r, g) for g in args.files) and r.endswith(CODE_EXT)]
    else:
        reducible = [r for r in in_copy if own(r)]
    reducible = [os.path.join(SCRATCH, r) for r in reducible]
    print(f'{len(reducible)} reducible files', flush=True)
    text_of = {p: open(p).read().split('\n') for p in reducible}

    # Whole files first: `export {};`, then out of the program, chunks of files at a time
    files = Files(text_of, config_writer)

    def keep_text(kept):
        for p in reducible:
            if files.state[p] != 'gone':
                files.set(p, 'text' if p in kept else 'stub')
        return probe.holds()
    texts = ddmin(list(reducible), keep_text)
    keep_text(set(texts))

    def keep_stubs(kept):
        for p in reducible:
            if files.state[p] != 'text':
                files.set(p, 'stub' if p in kept else 'gone')
        return probe.holds()
    stubs = ddmin([p for p in reducible if files.state[p] == 'stub'], keep_stubs)
    keep_stubs(set(stubs))
    for p in reducible:
        if files.state[p] == 'gone':
            del text_of[p]
        elif files.state[p] == 'stub':
            text_of[p] = ['export {};']
    print(f'{len(texts)} files kept whole, {len(stubs)} as `export {{}};`, {len(reducible) - len(texts) - len(stubs)} '
          f'deleted ({probe.runs} runs)', flush=True)
    for p in sorted(texts, key=lambda p: -len(text_of[p])):
        lines = text_of[p]

        def keep_lines(kept):
            write(p, '\n'.join(kept) + '\n')
            return probe.holds()
        text_of[p] = reduce_lines(lines, keep_lines)
        keep_lines(text_of[p])
        print(f'  {os.path.relpath(p, SCRATCH)}: {len(lines)} -> {len(text_of[p])} lines ({probe.runs} runs)', flush=True)

    only_tsgo, only_tttt, tsgo, tttt = probe.differing()
    assert probe.holds()
    # (The dependency files the reduced program still loads)
    listed = run([TSGO, '-p', os.path.join(cwd, probe.config), '--listFilesOnly'], cwd)
    program = sorted(l for l in map(str.strip, listed.split('\n')) if l.startswith(SCRATCH + '/'))
    case, inlined, left_out = case_file(cfg, program, text_of)
    write(os.path.join(SCRATCH, 'case.ts'), case)
    stubs = [p for p in text_of if text_of[p] == ['export {};']]
    print(f'\nleft ({probe.runs} runs): {len(stubs)} files as `export {{}};`' + (', and:' if len(stubs) < len(text_of) else ''))
    for p in sorted(set(text_of) - set(stubs)):
        print(f'  {os.path.relpath(p, cwd)}: {len(text_of[p])} lines')
    print('differing:\n' + '\n'.join([f'  tsgo only: {l}' for l in only_tsgo] + [f'  tttt only: {l}' for l in only_tttt]))
    print(f'case: {os.path.join(SCRATCH, "case.ts")} ({inlined} files' +
          (f'; {len(left_out)} program files left out, reduce them with --from {SCRATCH} --files GLOB)' if left_out else ')'))
    print(f'remove the copy with: rm -r {SCRATCH}')


if __name__ == '__main__':
    main()
