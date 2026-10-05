#!/usr/bin/env python3
"""Versioned dependencies for the breadth sweep (plan 44 stage 3 item 1): what pnpm's workspace install does for
DefinitelyTyped, as symlinks. The checkout's root node_modules/@types points at types/ (the latest of everything); a
package whose package.json wants an older major of a dependency (`"@types/react": "^16"`) gets
types/X/node_modules/@types/react -> types/react/v16, the highest version directory the range accepts (semver, against
the `version` of each directory's package.json). A versioned package (types/X/vN, a package of its own) gets a link for
every @types dependency, itself included (`workspace:.`), since resolving from inside types/X/vN would otherwise walk up
into types/X/node_modules, the latest X's links.

Idempotent: only missing or wrong links are written, and symlinks under types/**/node_modules/@types that aren't wanted
are removed (nothing else lives there: DefinitelyTyped ignores node_modules and isn't installed). `--check` only says
whether anything would change. `--affected` prints the packages whose programs the links can change (those with links
and every package depending on one of them, transitively), for `./sweep.py --tsc -f`.

  test/sweep_links.py [DT_DIR] [--check | --affected]
"""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def parse_version(text):
    m = re.fullmatch(r'v?(\d+)(?:\.(\d+))?(?:\.(\d+))?', text.strip())
    return tuple(int(g) for g in m.groups() if g is not None) if m else None


def comparators(part):
    """One space-separated (AND) semver range as a list of (op, version tuple padded to 3) pairs"""
    out = []
    part = re.sub(r'([<>=~^]+)\s+', r'\1', part.strip())
    hyphen = re.fullmatch(r'(\S+)\s+-\s+(\S+)', part)
    if hyphen:
        return [('>=', pad(parse_partial(hyphen.group(1))[0])), ('<=', pad(parse_partial(hyphen.group(2))[0], 9999999))]
    for token in part.split():
        m = re.fullmatch(r'(\^|~|>=|<=|>|<|=)?(.*)', token)
        op, rest = m.group(1) or '', m.group(2)
        if rest in ('*', 'x', 'X', ''):
            continue
        version, given = parse_partial(rest)
        if version is None:
            return None
        if op == '^':
            lower = pad(version)
            first = next((i for i, v in enumerate(lower) if v != 0), 2 if given >= 3 else given - 1)
            upper = list(lower[:first + 1]) + [0] * (2 - first)
            upper[first] += 1
            out += [('>=', lower), ('<', tuple(upper))]
        elif op == '~':
            lower = pad(version)
            upper = (lower[0] + 1, 0, 0) if given == 1 else (lower[0], lower[1] + 1, 0)
            out += [('>=', lower), ('<', upper)]
        elif op in ('', '='):
            if given == 3:
                out.append(('=', pad(version)))
            else:  # `16`, `1.2`: an X-range
                lower = pad(version)
                upper = list(lower)
                upper[given - 1] += 1
                out += [('>=', lower), ('<', tuple(upper))]
        elif op in ('<', '>='):
            out.append((op, pad(version)))
        elif op == '<=':
            out.append(('<', bump(version, given)) if given < 3 else ('<=', pad(version)))
        elif op == '>':
            out.append(('>=', bump(version, given)) if given < 3 else ('>', pad(version)))
    return out


def parse_partial(text):
    parts = re.split(r'\.', text)
    numbers = []
    for p in parts:
        if p in ('x', 'X', '*'):
            break
        if not p.isdigit():
            return None, 0
        numbers.append(int(p))
    return (tuple(numbers), len(numbers)) if numbers else (None, 0)


def pad(version, fill=0):
    return tuple(version) + (fill,) * (3 - len(version))


def bump(version, given):
    v = list(pad(version))
    v[given - 1] += 1
    for i in range(given, 3):
        v[i] = 0
    return tuple(v)


def satisfies(version, rng):
    for part in rng.split('||'):
        cs = comparators(part)
        if cs is None:
            continue
        ok = True
        for op, v in cs:
            ok = ok and {'=': version == v, '<': version < v, '<=': version <= v, '>': version > v, '>=': version >= v}[op]
        if ok:
            return True
    return False


def scan(dt):
    """Every package directory (types/X, types/X/vN): its package.json"""
    packages = {}
    types = os.path.join(dt, 'types')
    for name in sorted(os.listdir(types)):
        base = os.path.join(types, name)
        candidates = [base] + [os.path.join(base, d) for d in sorted(os.listdir(base)) if re.fullmatch(r'v\d+(\.\d+)?', d)] \
            if os.path.isdir(base) else []
        for d in candidates:
            pj = os.path.join(d, 'package.json')
            if not os.path.isfile(pj):
                continue
            try:
                j = json.load(open(pj))
            except ValueError:
                continue
            packages[os.path.relpath(d, dt)] = j
    return packages


def wanted_links(dt, packages):
    """{link path relative to dt: target directory relative to dt}"""
    versions = {}  # @types/NAME -> [(version tuple, dir)]
    for d, j in packages.items():
        v = parse_version(j.get('version', '')) if j.get('name', '').startswith('@types/') else None
        if v:
            versions.setdefault(j['name'], []).append((pad(v), d))
    links = {}
    for d, j in packages.items():
        versioned = d.count('/') == 2
        deps = {}
        for section in ('peerDependencies', 'devDependencies', 'dependencies'):  # (later ones win, like npm's precedence)
            deps.update({k: v for k, v in (j.get(section) or {}).items() if k.startswith('@types/')})
        for dep, rng in deps.items():
            if rng.startswith('workspace:'):
                target = d if rng == 'workspace:.' and dep == j.get('name') else None
            else:
                fits = [(v, t) for v, t in versions.get(dep, []) if satisfies(v, rng)]
                target = max(fits)[1] if fits else None
            if target is None:
                continue
            latest = 'types/' + dep[len('@types/'):]
            if versioned or target != latest:
                links[os.path.join(d, 'node_modules', dep)] = target
    return links


def existing_links(dt):
    out = {}
    types = os.path.join(dt, 'types')
    for name in os.listdir(types):
        for sub in ('', *[s for s in os.listdir(os.path.join(types, name)) if re.fullmatch(r'v\d+(\.\d+)?', s)]) \
                if os.path.isdir(os.path.join(types, name)) else ():
            scope = os.path.join(types, name, sub, 'node_modules', '@types')
            if not os.path.isdir(scope):
                continue
            for entry in os.listdir(scope):
                path = os.path.join(scope, entry)
                if os.path.islink(path):
                    out[os.path.relpath(path, dt)] = os.path.normpath(os.path.relpath(
                        os.path.join(os.path.dirname(path), os.readlink(path)), dt))
    return out


def affected(packages, links):
    """Packages whose program the links may change: those with links, and their dependents (transitively)"""
    by_name = {}
    for d, j in packages.items():
        if d.count('/') == 1 and j.get('name'):
            by_name[j['name']] = d
    dependents = {}
    for d, j in packages.items():
        for section in ('dependencies', 'devDependencies', 'peerDependencies'):
            for dep in (j.get(section) or {}):
                if dep in by_name and by_name[dep] != d:
                    dependents.setdefault(by_name[dep], set()).add(d)
    todo = sorted({l.split('/node_modules/')[0] for l in links} | {d for d in packages if d.count('/') == 2})
    seen = set(todo)
    while todo:
        d = todo.pop()
        # (A versioned directory is depended on through its links, which are all in `links` already)
        for user in dependents.get(d, ()):
            if user not in seen:
                seen.add(user)
                todo.append(user)
    return sorted(seen)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    dt = args[0] if args else os.path.join(ROOT, 'thirdparty/corpora/DefinitelyTyped')
    packages = scan(dt)
    want = wanted_links(dt, packages)
    if '--affected' in sys.argv:
        print('\n'.join(affected(packages, want)))
        return
    have = existing_links(dt)
    stale = [l for l in have if want.get(l) != have[l]]
    missing = [l for l in want if have.get(l) != want[l]]
    if '--check' in sys.argv:
        print(f'{len(want)} links wanted, {len(missing)} to write, {len(stale)} to remove')
        sys.exit(1 if missing or stale else 0)
    for l in stale:
        os.unlink(os.path.join(dt, l))
    for l in missing:
        path = os.path.join(dt, l)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        os.symlink(os.path.relpath(os.path.join(dt, want[l]), os.path.dirname(path)), path)
    if missing or stale:
        print(f'sweep_links: {len(missing)} links written, {len(stale)} removed ({len(want)} in all)')


if __name__ == '__main__':
    main()
