#!/usr/bin/env python3
"""`$ExpectType` on one project, tsc against tttt, and a reducer for the mismatches scripts/expecttypes.py finds.

  scripts/expectprobe.py DIR                         # every assertion of DIR's tsconfig.json: tsc (full check, then
                                               # the types, like expecttypes.py's oracle) and tttt, the mismatches
  scripts/expectprobe.py DIR --reduce FILE TEXT      # delete chunks of FILE (blank-line separated, then single lines) while
                                               # the mismatch output still contains TEXT; FILE is rewritten in place
                                               # (keep a copy), --lines A-B limits it to those lines

To cut a DefinitelyTyped package down: a DIR whose tsconfig.json lists the package's declaration files by absolute path
and a test file of its own (copied from the package's tests) under DIR; only files under DIR are asked about.
"""
import argparse, json, os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def mismatches(d):
    d = os.path.abspath(d)
    subprocess.run([os.path.join(ROOT, 'thirdparty/ensure-typescript.sh')], check=True)
    out = subprocess.run([os.path.join(ROOT, 'thirdparty/typescript/built/tttt-oracle'), '--expect'], input=d + '\n',
                         capture_output=True, text=True, cwd='/').stdout
    asserts = json.loads(out).get('asserts', [])
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        for a in asserts:
            f.write(f'{a[0]}\t{a[1]}\t{a[2]}\t{a[3]}\t{a[4]}\n')
    t = subprocess.run([os.path.join(ROOT, 'release/tttt'), '-q', '--keep-going', '--threads', '1', '--checkers', '1',
                        '--noEmit', '--expect-types', f.name, '-p', d], capture_output=True, text=True, cwd='/',
                       env={**os.environ, 'TTTT_NO_REEXEC': '1'}).stdout
    os.unlink(f.name)
    got = dict(line.split('\t', 1) for line in t.split('\n') if '\t' in line)
    lines = []
    for a in asserts:
        k = f'{a[0]}:{a[1]}:{a[2]}'
        if got.get(k) != a[6]:
            lines.append(f'{k}\n  tsc:  {a[6]}\n  tttt: {got.get(k) or ""}')
    return lines, len(asserts)


def reduce(d, path, text, first, last):
    lines = open(path).read().split('\n')
    last = min(last, len(lines))

    def still(keep):
        open(path, 'w').write('\n'.join(l for i, l in enumerate(lines) if keep[i]))
        return text in '\n'.join(mismatches(d)[0])

    keep = [True] * len(lines)
    if not still(keep):
        sys.exit(f'{text!r} not in the mismatches to begin with')
    for blank_chunks in (True, False):
        chunk = []
        for i in range(first - 1, last):
            if not keep[i]:
                continue
            chunk.append(i)
            if blank_chunks and lines[i].strip() and i + 1 < last:
                continue
            for j in chunk:
                keep[j] = False
            if not still(keep):
                for j in chunk:
                    keep[j] = True
            chunk = []
    still(keep)
    print(f'{sum(keep)} of {len(lines)} lines left in {path}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dir')
    ap.add_argument('--reduce', nargs=2, metavar=('FILE', 'TEXT'))
    ap.add_argument('--lines', default='1-1000000')
    args = ap.parse_args()
    subprocess.run(['ninja', '-C', os.path.join(ROOT, 'release'), 'tttt'], check=True, stdout=subprocess.DEVNULL)
    if args.reduce:
        a, b = map(int, args.lines.split('-'))
        reduce(args.dir, args.reduce[0], args.reduce[1], a, b)
    found, total = mismatches(args.dir)
    print('\n'.join(found))
    print(f'{total} assertions, {len(found)} mismatches')


if __name__ == '__main__':
    main()
