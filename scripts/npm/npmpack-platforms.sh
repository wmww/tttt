#!/usr/bin/env bash
# scripts/npmpack.sh's BINDIR mode (v3 on): the four platform packages and the tttt-ts wrapper. Usage and design are in
# scripts/npmpack.sh's header and agents/notes/release.md (npm).
#
#   scripts/npm/npmpack-platforms.sh TAG BINDIR [OUTDIR]
set -euo pipefail
if test $# -lt 2; then sed -n '2,5p' "$0"; exit 1; fi
TAG=$1
BINDIR=$(realpath "$2")
OUT=$(realpath -m "${3:-/tmp/npmpack-$TAG}")
cd "$( dirname "${BASH_SOURCE[0]}" )/../.."
NPMDIR=scripts/npm
case "$TAG" in
    v[0-9]*.[0-9]*.[0-9]*) VERSION=${TAG#v} ;;
    v[0-9]*) VERSION=${TAG#v}.0.0 ;;
    *) echo "TAG must look like v3 or v3.1.2"; exit 1 ;;
esac
REV=$TAG
if ! git rev-parse -q --verify "$TAG^{commit}" >/dev/null; then
    echo "warning: no tag $TAG, license files from HEAD"
    REV=HEAD
fi
FAKE=${NPMPACK_FAKE:-0}
problem() {
    if test "$FAKE" = 1; then echo "warning (NPMPACK_FAKE): $*"; else echo "error: $*"; exit 1; fi
}

# The platform packages' names: PREFIX + platform. Scoped under the publisher's user (v3 on): npm's spam filter
# refused the unscoped tttt-ts-win32-x64 after three new unscoped siblings went up in minutes, and a scope keeps the
# names from being squatted. Keep it in step with launcher.js and install.js (their PREFIX).
PREFIX=@wmww/tttt-ts-
# platform  npm-os  npm-cpu  binary-path-in-package  `file` pattern  description
PLATFORMS=(
    "linux-x64    linux  x64   bin/tttt   ELF.64-bit.LSB.*x86-64       Linux x64 (glibc 2.28 or newer)"
    "linux-arm64  linux  arm64 bin/tttt   ELF.64-bit.LSB.*ARM.aarch64  Linux arm64 (glibc 2.28 or newer)"
    "darwin-arm64 darwin arm64 bin/tttt   Mach-O.64-bit.*arm64         macOS arm64 (13 or newer)"
    "win32-x64    win32  x64   tttt.exe   PE32\\+.executable.*x86-64    Windows x64 (10 or newer)"
)

# The newest glibc symbol version an ELF binds.
max_glibc() { readelf --dyn-syms -W "$1" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sed 's/GLIBC_//' | sort -Vu | tail -1 || true; }
# A Mach-O's minimum macOS (LC_BUILD_VERSION or LC_VERSION_MIN_MACOSX), as X.Y.
macos_min() {
    python3 -I - "$1" <<'EOF' || true
import struct, sys
d = open(sys.argv[1], 'rb').read()
if d[:4] != b'\xcf\xfa\xed\xfe': sys.exit(0)
ncmds, = struct.unpack_from('<I', d, 16)
off = 32
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', d, off)
    if cmd in (0x32, 0x24):
        v, = struct.unpack_from('<I', d, off + (12 if cmd == 0x32 else 8))
        print(f'{v >> 16}.{(v >> 8) & 0xff}')
        break
    off += size
EOF
}
version_le() { test "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -1)" = "$1"; }

rm -rf "$OUT"
mkdir -p "$OUT"
LICENSES=$OUT/licenses
mkdir -p "$LICENSES"
git show "$REV:LICENSE" > "$OUT/LICENSE"
git show "$REV:thirdparty/cleat/LICENSE" > "$LICENSES/cleat.LICENSE"
git show "$REV:thirdparty/cleat/stc/LICENSE" > "$LICENSES/stc.LICENSE"
git show "$REV:thirdparty/lib/LICENSE.txt" > "$LICENSES/typescript.LICENSE"
# (TypeScript's third-party notices, which Apache-2.0 4(d) asks a redistribution of its files to carry; in the tree since v3)
git show "$REV:thirdparty/lib/NOTICE.txt" > "$LICENSES/typescript.NOTICE" 2>/dev/null || rm -f "$LICENSES/typescript.NOTICE"
REPO='"homepage": "https://github.com/wmww/tttt",
  "repository": { "type": "git", "url": "git+https://github.com/wmww/tttt.git" },
  "license": "MIT",
  "author": "Sophie Winter"'
TS_VERSION=

OPTIONAL=
for line in "${PLATFORMS[@]}"; do
    read -r PLAT OS CPU SUB PATTERN DESC <<<"$line"
    NAME=$PREFIX$PLAT
    SRC=$BINDIR/$PLAT/$(basename "$SUB")
    test -f "$SRC" || { echo "error: missing $SRC"; exit 1; }
    KIND=$(file -bL "$SRC")
    grep -qE "$PATTERN" <<<"$KIND" || problem "$SRC is not a $PLAT binary: $KIND"
    case $PLAT in
        linux-*)
            G=$(max_glibc "$SRC")
            if test -n "$G" && ! version_le "$G" 2.28; then problem "$SRC needs glibc $G (> 2.28)"; fi ;;
        darwin-*)
            M=$(macos_min "$SRC")
            if test -n "$M" && ! version_le "$M" 13.0; then problem "$SRC needs macOS $M (> 13)"; fi ;;
    esac
    if test "$PLAT" = linux-x64 && test "$(uname -sm)" = "Linux x86_64"; then
        TS_VERSION=$("$SRC" --version | sed 's/^Version //')
    fi
    PKG=$OUT/tttt-ts-$PLAT
    mkdir -p "$PKG/$(dirname "$SUB")"
    install -m755 "$SRC" "$PKG/$SUB"
    cp "$OUT/LICENSE" "$PKG/"
    cp -r "$LICENSES" "$PKG/"
    LIBC=
    test "$OS" = linux && LIBC='
  "libc": ["glibc"],'
    cat > "$PKG/package.json" <<EOF
{
  "name": "$NAME",
  "version": "$VERSION",
  "description": "The tttt binary for $DESC: installed by tttt-ts, use that package",
  $REPO,
  "os": ["$OS"],
  "cpu": ["$CPU"],$LIBC
  "preferUnplugged": true,
  "files": ["$SUB", "licenses", "LICENSE", "README.md"]
}
EOF
    cat > "$PKG/README.md" <<EOF
# $NAME

The [tttt](https://www.npmjs.com/package/tttt-ts) type checker's native binary for $DESC. Don't install this
package directly: install \`tttt-ts\`, which depends on the package for each platform and lets npm pick this one.

tttt is MIT licensed. The binary includes TypeScript's standard library declarations, licensed under the Apache
License 2.0 (\`licenses/typescript.LICENSE\`, its notices in \`licenses/typescript.NOTICE\`), [cleat](https://codeberg.org/sphi/cleat) and
[STC](https://github.com/stclib/STC), both MIT (\`licenses/\`).
EOF
    OPTIONAL+="${OPTIONAL:+,
}    \"$NAME\": \"$VERSION\""
done
test -n "$TS_VERSION" || TS_VERSION=$(git show "$REV:thirdparty/ensure-typescript.sh" | grep -oE 'v[0-9]+\.[0-9]+[-a-z0-9.]*' | head -1)

PKG=$OUT/tttt-ts
mkdir -p "$PKG/bin"
install -m755 "$NPMDIR/launcher.js" "$PKG/bin/tttt"
install -m644 "$NPMDIR/install.js" "$PKG/install.js"
cp "$OUT/LICENSE" "$PKG/"
cat > "$PKG/package.json" <<EOF
{
  "name": "tttt-ts",
  "version": "$VERSION",
  "description": "A fast TypeScript type checker written in C: a drop-in \`tsc --noEmit\`",
  "keywords": ["typescript", "type checker", "tsc", "typecheck"],
  $REPO,
  "bin": { "tttt": "bin/tttt" },
  "scripts": { "postinstall": "node install.js" },
  "engines": { "node": ">=16" },
  "optionalDependencies": {
$OPTIONAL
  },
  "files": ["bin/tttt", "install.js", "LICENSE", "README.md"]
}
EOF
cat > "$PKG/README.md" <<EOF
# tttt: the Tess TypeScript Type checker

A high speed [TypeScript](https://www.typescriptlang.org/) type checker written in C.

tttt is meant as a drop-in \`tsc\` for checking: the same command lines and tsconfigs. Every run behaves like
\`tsc --noEmit\`, except that a project whose config has \`emitDeclarationOnly\` gets its \`.d.ts\` and \`.d.ts.map\` files
written like \`tsc\` does. It matches TypeScript $TS_VERSION.

\`\`\`bash
npm install --save-dev tttt-ts
npx tttt -p path/to/project   # or run from your project directory without arguments
\`\`\`

\`tttt --help\` lists the options. It exits 0 when there are no errors, 2 when there are, like \`tsc\`.

## Platforms

Linux x64 and arm64 (glibc 2.28 or newer: not musl/Alpine), macOS arm64 (13 or newer) and Windows x64 (10 or newer).
The binary comes in a platform package (\`${PREFIX}linux-x64\`, ...) that npm installs as an optional dependency, the
one matching your machine only, so don't install with \`--omit=optional\`. On other platforms tttt can be built from
source (https://github.com/wmww/tttt); \`TTTT_BINARY=/path/to/tttt\` makes this package's \`tttt\` run that build.

\`tttt\` is a small JavaScript launcher that runs the platform's binary. Where the package's install script may run
(Linux and macOS, with npm, and with Yarn 1), it replaces the launcher with the binary itself, so \`tttt\` starts
no Node.js process. npm 12 and pnpm 10 block dependencies' install scripts by default: allow tttt-ts's
(\`npm approve-scripts tttt-ts\`, or \`--allow-scripts=tttt-ts\`) to save Node.js's start-up (tens of milliseconds) on
every run, or leave it: the launcher works the same. On Windows \`tttt\` always goes through the launcher (npm's
\`.cmd\` shim runs it with Node.js).

## Licenses

tttt is MIT licensed. The platform packages' binaries include TypeScript's standard library declarations, licensed
under the Apache License 2.0 (with TypeScript's NOTICE), [cleat](https://codeberg.org/sphi/cleat) and [STC](https://github.com/stclib/STC),
both MIT (their \`licenses/\` directory).
EOF
rm -rf "$LICENSES" "$OUT/LICENSE"

{
    echo "# Publish tttt-ts $VERSION: the platform packages first, the wrapper last (a wrapper whose optional"
    echo "# dependencies don't exist yet installs without a binary). Each asks for a 2FA code (--otp CODE)."
    echo "set -e"
} > "$OUT/publish.sh"
for d in "$OUT"/tttt-ts-* "$OUT/tttt-ts"; do
    (cd "$OUT" && npm pack "./$(basename "$d")" --pack-destination "$OUT" >/dev/null 2>&1)
    # (npm pack names a scoped package's tarball SCOPE-NAME-VERSION.tgz)
    NAME=$(node -p "require('$d/package.json').name")
    TGZ=$OUT/$(sed 's/^@//; s|/|-|' <<<"$NAME")-$VERSION.tgz
    test -f "$TGZ" || { echo "error: npm pack $d made no $TGZ"; exit 1; }
    echo "npm publish $TGZ --access public" >> "$OUT/publish.sh"
    printf '%-28s %8s KB\n' "$(basename "$TGZ")" "$(( $(stat -c %s "$TGZ") / 1024 ))"
done
echo "$OUT/publish.sh:"
grep '^npm' "$OUT/publish.sh"
