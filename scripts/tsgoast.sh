#!/usr/bin/env bash
# Where tsgo's parser puts the nodes and errors for a file: its parse tree (kind, [pos,end), text; trivia included in
# pos as in tsgo; a node tsgo flags ThisNodeHasError says so) and its parse diagnostics, through `go test -overlay` (scripts/tsgoast_test.go mapped into
# internal/parser; the shared checkout is never written). The script kind follows the extension (.ts/.tsx/.js/.jsx).
#   scripts/tsgoast.sh FILE.tsx      # ~10 s the first time (compiles), ~2 s after
#   scripts/tsgoast.sh --check       # only compile it (scripts/overlaycheck.sh)
set -uo pipefail
ROOT=$(realpath "$( dirname "${BASH_SOURCE[0]}" )/..")
CHECK=0
if test "${1:-}" = --check; then CHECK=1; else SRC=$(realpath "${1:?usage: tsgoast.sh FILE | --check}") || exit 1; fi
TSC=$(realpath "$ROOT/thirdparty/typescript/tsc") || exit 1
eval "$("$ROOT/thirdparty/ensure-typescript.sh" --env)"
DIR=$(mktemp -d /tmp/tsgoast-XXXXXX)
trap 'rm -rf "$DIR"' EXIT
printf '{"Replace":{"%s":"%s"}}\n' "$TSC/internal/parser/tttt_ast_test.go" "$ROOT/scripts/tsgoast_test.go" > "$DIR/overlay.json"
cd "$TSC" || exit 1
if test "$CHECK" = 1; then exec go test -vet=off -overlay "$DIR/overlay.json" -c -o /dev/null ./internal/parser; fi
TTTT_SRC="$SRC" go test -v -vet=off -count=1 -overlay "$DIR/overlay.json" -run '^TestTtttAst$' ./internal/parser | grep -v -E '^(ok|PASS$|=== RUN|--- PASS)'
