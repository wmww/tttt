#!/usr/bin/env bash
# VS Code's src/tsconfig.base.json maps `electron` to .build/typings/electron.d.ts, which upstream's postinstall
# (build/npm/electronTypes.ts) downloads from the Electron release named in .npmrc, checked against
# build/checksums/electron.txt. Same here, without running upstream's build scripts.
set -euo pipefail
VERSION=$(sed -n 's/^target="\(.*\)"$/\1/p' .npmrc)
EXPECTED=$(sed -n 's/^\([0-9a-f]\{64\}\) \*electron\.d\.ts$/\1/p' build/checksums/electron.txt)
mkdir -p .build/typings
curl -sSfL -o .build/typings/electron.d.ts "https://github.com/electron/electron/releases/download/v$VERSION/electron.d.ts"
echo "$EXPECTED  .build/typings/electron.d.ts" | sha256sum -c --quiet
