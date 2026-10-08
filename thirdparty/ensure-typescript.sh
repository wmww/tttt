#!/usr/bin/env bash
# The TypeScript 7 oracle (plan 54): microsoft/TypeScript `main` at the commit a `typescript@7.1.0-dev` nightly was
# built from (npm's gitHead), with tsgo's Go sources under tsc/ and the native test suite in tsc/testdata. Not
# committed, like thirdparty/tsc. Also fetches the Go toolchain go.mod asks for (thirdparty/go, sha256-checked) and
# builds tsgo's CLI from the same commit into typescript/built/tsc, so every oracle runs one pinned compiler.
#
#   thirdparty/ensure-typescript.sh          # fetch and build whatever is missing
#   thirdparty/ensure-typescript.sh --env    # print the Go environment the scripts use (eval it)
#
# Go reads its module cache and build cache from thirdparty/typescript-gopath (not ~/go). Building against the
# checkout without changing it: `go build -overlay` (tttt's own Go files are mapped into the tree at build time).
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )"

# typescript@7.1.0-dev.20261008.1 (npm `next` on 2026-10-08)
TS_COMMIT=fed0bf24149fb1ed36039212648bafdafc1ea10e
TS_VERSION="7.1.0-dev.20261008.1 (${TS_COMMIT:0:8})"
GO_VERSION=go1.27.2
GO_SHA256=ecbadb99091a3f46e31f5f934b068b1864eafa7995211b39eaddf76996045fe5

EOL="\x1b[0m\n"
BLUE="\x1b[34m"
RED="\x1b[1;31m"

HERE="$PWD"
GOENV="export PATH=\"$HERE/go/bin:\$PATH\" GOPATH=\"$HERE/typescript-gopath\" GOCACHE=\"$HERE/typescript-gopath/cache\" GOTOOLCHAIN=local GOFLAGS= GOWORK="
if test "${1:-}" = --env; then
    echo "$GOENV"
    exit 0
fi

if test ! -e typescript; then
    printf "${BLUE}Fetching TypeScript $TS_VERSION...$EOL"
    mkdir typescript
    git -C typescript init --quiet
    git -C typescript remote add origin https://github.com/microsoft/TypeScript.git
    git -C typescript fetch --quiet --depth 1 origin $TS_COMMIT
    git -C typescript -c advice.detachedHead=false checkout --quiet FETCH_HEAD
else
    ACTUAL_TS="$(git -C typescript rev-parse HEAD)"
    if test "$ACTUAL_TS" != $TS_COMMIT; then
        printf "${RED}thirdparty/typescript has $ACTUAL_TS but it needs $TS_COMMIT: rm -rf thirdparty/typescript and rerun$EOL"
        exit 1
    fi
fi

if test "$(go/bin/go version 2>/dev/null | cut -d' ' -f3)" != $GO_VERSION; then
    printf "${BLUE}Fetching $GO_VERSION...$EOL"
    rm -rf go
    TMP="$(mktemp -d)"
    curl -sSL "https://go.dev/dl/$GO_VERSION.linux-amd64.tar.gz" -o "$TMP/go.tar.gz"
    echo "$GO_SHA256  $TMP/go.tar.gz" | sha256sum --check --quiet
    tar -C . -xzf "$TMP/go.tar.gz"
    rm -rf "$TMP"
fi

eval "$GOENV"
if test ! -e typescript/built/tsc; then
    printf "${BLUE}Building tsgo $TS_VERSION...$EOL"
    (cd typescript/tsc && go build -o ../built/tsc ./cmd/tsc)
fi
