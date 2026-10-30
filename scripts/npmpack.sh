#!/usr/bin/env bash
# Packs a release's Linux x86-64 binary as the npm package `tttt-ts` (published as tttt-ts@2.0.0 on 2026-10-30, from
# the v2 release binary). The package is the binary as `bin` (npm links node_modules/.bin/tttt straight to it: no JS
# shim, no install scripts), the licenses, and for releases before the libs were compiled in (418884466) TypeScript's
# lib files at thirdparty/lib next to bin/, where those binaries look for them.
#
#   scripts/npmpack.sh TAG BINARY [OUTDIR]   # e.g. scripts/npmpack.sh v2 /path/to/v2/tttt: OUTDIR/tttt-ts-2.0.0.tgz
#
# The npm version is TAG's number as a major (v2 -> 2.0.0). Publishing is the human's (a separate npm account with
# 2FA): hand them `npm publish OUTDIR/tttt-ts-X.0.0.tgz --otp CODE`. Only Linux x86-64 so far: other platforms need
# per-platform packages under one wrapper (agents/todo.md).
set -euo pipefail
if test $# -lt 2; then sed -n '2,11p' "$0"; exit 1; fi
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
