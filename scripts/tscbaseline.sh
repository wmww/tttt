#!/usr/bin/env bash
# Prints what tsc (tsgo, the pinned 7.1) says about a file: scripts/tscbaseline.sh path/to/file.ts [types|symbols|errors.txt] (default: types)
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
TEST_DIR=$(mktemp -d /tmp/tscbaseline.XXXXXX)
trap 'rm -rf "$TEST_DIR"' EXIT
mkdir -p "$TEST_DIR/tests/cases/conformance"
cp "$1" "$TEST_DIR/tests/cases/conformance/"
scripts/tsgoprobe.sh "$TEST_DIR" >/dev/null
NAME=$(basename "$1"); NAME=${NAME%.*}
cat "$TEST_DIR/baselines/local/conformance/$NAME"*".${2:-types}" 2>/dev/null || echo "(no .${2:-types} baseline: no errors?)"
