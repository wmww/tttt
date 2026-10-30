#!/usr/bin/env bash
# Packs a release for npm. Two modes:
#
#   scripts/npmpack.sh TAG BINDIR [OUTDIR]   # v3 on: e.g. scripts/npmpack.sh v3 ~/tttt-release/v3
#   scripts/npmpack.sh TAG BINARY [OUTDIR]   # v2: one Linux x86-64 package, e.g. scripts/npmpack.sh v2 /path/to/v2/tttt
#
# BINDIR mode (v3 on, esbuild's layout): BINDIR/{linux-x64/tttt,linux-arm64/tttt,darwin-arm64/tttt,win32-x64/tttt.exe}
# become four platform packages tttt-ts-PLATFORM (the binary, os/cpu/libc so npm installs only the matching one) and
# the wrapper tttt-ts, whose `bin` is a JS launcher (scripts/npm/launcher.js) that runs the platform package's
# binary, and whose postinstall (scripts/npm/install.js) swaps the binary in for the launcher where it can.
# Writes OUTDIR/*.tgz and OUTDIR/publish.sh, the `npm publish` commands in order (platforms first, the wrapper last).
# The binaries are checked (architecture, glibc <= 2.28 on Linux, macOS minimum <= 13); NPMPACK_FAKE=1 turns those
# checks into warnings, for testing with stand-ins. License files come from TAG if it exists, else from HEAD.
# agents/notes/release.md (npm) has the design and how it was tested (scripts/npm/npmtest.sh).
#
# BINARY mode (published as tttt-ts@2.0.0 on 2026-10-30): the binary itself is the package's `bin` (no JS shim, no
# install scripts), with the licenses, and for releases before the libs were compiled in (418884466) TypeScript's lib
# files at thirdparty/lib next to bin/, where those binaries look for them.
#
# The npm version is TAG's number (v3 -> 3.0.0, v3.1.2 -> 3.1.2). Publishing is the human's (an npm account with 2FA).
set -euo pipefail
if test $# -lt 2; then sed -n '2,21p' "$0"; exit 1; fi
if test -d "$2"; then exec "$(dirname "${BASH_SOURCE[0]}")/npm/npmpack-platforms.sh" "$@"; fi
TAG=$1
BINARY=$(realpath "$2")
OUT=$(realpath -m "${3:-/tmp/npmpack-$TAG}")
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
VERSION=${TAG#v}.0.0
test "$(file -bL "$BINARY" | cut -d, -f1-2)" = "ELF 64-bit LSB pie executable, x86-64" || { echo "$BINARY: not an x86-64 ELF"; exit 1; }
git rev-parse -q --verify "$TAG^{commit}" >/dev/null || { echo "no tag $TAG"; exit 1; }

PKG=$OUT/package
rm -rf "$PKG"
mkdir -p "$PKG/bin" "$PKG/licenses"
install -m755 "$BINARY" "$PKG/bin/tttt"
git archive "$TAG" LICENSE | tar x -C "$PKG"
git show "$TAG:thirdparty/cleat/LICENSE" > "$PKG/licenses/cleat.LICENSE"
git show "$TAG:thirdparty/cleat/stc/LICENSE" > "$PKG/licenses/stc.LICENSE"
FILES='"bin/tttt", "licenses", "LICENSE", "README.md"'
LIBS_NOTE='The binary includes TypeScript'"'"'s standard library declarations, licensed under the Apache License 2.0 (`licenses/typescript.LICENSE`),'
if git cat-file -e "$TAG:src/embedded_libs.py" 2>/dev/null; then
    git show "$TAG:thirdparty/lib/LICENSE.txt" > "$PKG/licenses/typescript.LICENSE"
else
    git archive "$TAG" thirdparty/lib | tar x -C "$PKG"
    FILES='"bin/tttt", "thirdparty/lib", "licenses", "LICENSE", "README.md"'
    LIBS_NOTE='The TypeScript standard library declarations in `thirdparty/lib` are from TypeScript and licensed under the Apache License 2.0 (`thirdparty/lib/LICENSE.txt`). The binary includes'
fi
TS_VERSION=$("$BINARY" --version | sed 's/^Version //')
GLIBC=$(objdump -T "$BINARY" | grep -oE 'GLIBC_[0-9.]+' | sed 's/GLIBC_//' | sort -Vu | tail -1)

cat > "$PKG/package.json" <<EOF
{
  "name": "tttt-ts",
  "version": "$VERSION",
  "description": "A fast TypeScript type checker written in C: a drop-in \`tsc --noEmit\`",
  "keywords": ["typescript", "type checker", "tsc", "typecheck"],
  "homepage": "https://github.com/wmww/tttt",
  "repository": { "type": "git", "url": "git+https://github.com/wmww/tttt.git" },
  "license": "MIT",
  "author": "Sophie Winter",
  "bin": { "tttt": "bin/tttt" },
  "os": ["linux"],
  "cpu": ["x64"],
  "libc": ["glibc"],
  "files": [$FILES]
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

\`tttt --help\` lists the options.

## Platforms

This release is Linux x86-64 only and needs glibc $GLIBC or newer. Other platforms are coming in a later release.

## Licenses

tttt is MIT licensed. $LIBS_NOTE
[cleat](https://codeberg.org/sphi/cleat) and [STC](https://github.com/stclib/STC), both MIT (\`licenses/\`).
EOF
(cd "$OUT" && npm pack ./package 2>&1 | grep -E 'filename|package size|unpacked size|shasum|total files')
echo "$OUT/tttt-ts-$VERSION.tgz"
