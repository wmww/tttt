#!/usr/bin/env bash
# The TypeScript 7 oracle (plan 54): microsoft/TypeScript `main` at the commit a `typescript@7.1.0` prerelease was
# built from (npm's gitHead), with tsgo's Go sources under tsc/ and the native test suite in tsc/testdata. Not
# committed. Also fetches the Go toolchain go.mod asks for (thirdparty/go, sha256-checked) and
# builds tsgo's CLI from the same commit into typescript/built/tsc, so every oracle runs one pinned compiler.
#
#   thirdparty/ensure-typescript.sh          # fetch and build whatever is missing
#   thirdparty/ensure-typescript.sh --env    # print the Go environment the scripts use (eval it)
#
# Go reads its module cache and build cache from thirdparty/typescript-gopath (not ~/go). Building against the
# checkout without changing it: `go build -overlay` (tttt's own Go files in test/oracle are mapped into the tree at
# build time, see test/oracle/README.md): built/tttt-oracle and built/tttt-probe.test, rebuilt when test/oracle changes.
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )"

# typescript@7.1.0-beta (npm `beta`, git tag v7.1-beta, 2026-10-19)
TS_COMMIT=3e9236361fa97a9e6c685f4833ac457ae2d8c50a
TS_VERSION="7.1.0-beta (${TS_COMMIT:0:8})"
GO_VERSION=go1.27.2
GO_SHA256=ecbadb99091a3f46e31f5f934b068b1864eafa7995211b39eaddf76996045fe5

EOL="\x1b[0m\n"
BLUE="\x1b[34m"
RED="\x1b[1;31m"

HERE="$PWD"
mkdir -p "$HERE/typescript-gopath/tmp"
# (GOTMPDIR: Go's $WORK dirs go beside the cache, not under /tmp, whose quota a build hit on 2026-10-23)
GOENV="export PATH=\"$HERE/go/bin:\$PATH\" GOPATH=\"$HERE/typescript-gopath\" GOCACHE=\"$HERE/typescript-gopath/cache\" GOTMPDIR=\"$HERE/typescript-gopath/tmp\" GOTOOLCHAIN=local GOFLAGS= GOWORK="

# Every instrumented tsgo build (an overlay patching internal/checker) adds hundreds of MB to the build cache, and Go
# only trims entries unused for 5 days: it reached 50 GB and filled the disk (2026-10-14). Cap it at 4 GB.
if test -d typescript-gopath/cache && test "$(du -sm typescript-gopath/cache | cut -f1)" -gt 4096; then
    echo "ensure-typescript: Go build cache above 4 GB, cleaning it" >&2
    (eval "$GOENV" && go clean -cache) >&2
fi

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

# tttt's overlay (test/oracle): FILE -> where it goes in the checkout
OVERLAY=(
    testdata.go internal/repo/tttt_testdata.go
    probe_test.go internal/testrunner/tttt_probe_test.go
    tttt-oracle/main.go cmd/tttt-oracle/main.go
    tttt-test-mapper/main.go cmd/tttt-test-mapper/main.go
    tttt-test-mapper/stdio.go internal/testutil/contentmappertest/tttt_stdio.go
)
ORACLE_DIR="$HERE/../test/oracle"
# Rebuilt when the overlay's content changes (not its mtime: every new worktree's checkout is newer than the tools,
# and a rebuild from a worktree would point overlay.json at it)
ORACLE_HASH="$(cd "$ORACLE_DIR" && find . -name "*.go" | sort | xargs cat | sha256sum | cut -d' ' -f1)"
# The built tools are shared by every worktree (typescript/ is a symlink there), so only the main tree rebuilds them:
# a worktree at another commit has another test/oracle hash, and rebuilding from it flipped the shared tools back and
# forth between two worktrees' versions under running oracles (2026-10-23). A worktree whose hash differs gets main's
# tools and a warning; rebase onto main to match.
IN_WORKTREE=
if test "$(git -C "$HERE/.." rev-parse --git-dir 2>/dev/null)" != "$(git -C "$HERE/.." rev-parse --git-common-dir 2>/dev/null)"; then IN_WORKTREE=1; fi
if test -n "$IN_WORKTREE" && test -e typescript/built/tttt-oracle && test -e typescript/built/tttt-probe.test && test -e typescript/built/tttt-test-mapper; then
    if test "$(cat typescript/built/tttt-oracle.hash 2>/dev/null)" != "$ORACLE_HASH"; then
        printf "${BLUE}ensure-typescript: this worktree's test/oracle differs from the shared oracle tools' (built by the main tree); using main's. Rebase onto main to match.$EOL" >&2
    fi
elif test "$(cat typescript/built/tttt-oracle.hash 2>/dev/null)" != "$ORACLE_HASH" -o ! -e typescript/built/tttt-oracle \
        -o ! -e typescript/built/tttt-probe.test -o ! -e typescript/built/tttt-test-mapper; then
    printf "${BLUE}Building tttt's tsgo oracle tools (test/oracle)...$EOL"
    # (Go matches overlay paths against real paths: in a worktree, typescript/ is a symlink to the main tree's)
    {
        printf '{"Replace": {'
        for ((i = 0; i < ${#OVERLAY[@]}; i += 2)); do
            test $i = 0 || printf ', '
            printf '"%s": "%s"' "$(realpath "$HERE/typescript/tsc")/${OVERLAY[i+1]}" "$(realpath "$ORACLE_DIR/${OVERLAY[i]}")"
        done
        printf '}}\n'
    } > typescript/built/overlay.json
    (cd "$(realpath typescript/tsc)" && go build -overlay ../built/overlay.json -o ../built/tttt-oracle ./cmd/tttt-oracle &&
        go test -overlay ../built/overlay.json -c -o ../built/tttt-probe.test ./internal/testrunner &&
        go build -overlay ../built/overlay.json -o ../built/tttt-test-mapper ./cmd/tttt-test-mapper) &&
        echo "$ORACLE_HASH" > typescript/built/tttt-oracle.hash
fi
