#!/usr/bin/env bash
# Prints what tsc says about a file: ./tscbaseline.sh path/to/file.ts [types|symbols|errors.txt] (default: types)
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )"
thirdparty/ensure-tsc.sh
TEST_DIR=$(mktemp -d /tmp/tscbaseline.XXXXXX)
trap 'rm -rf "$TEST_DIR"' EXIT
mkdir -p "$TEST_DIR/tests/cases/conformance" "$TEST_DIR/tests/cases/compiler"
cp "$1" "$TEST_DIR/tests/cases/conformance/"
NODE_PATH=$PWD/thirdparty/tsc/node_modules/ node ./test/diffapp/tsc_custom_baseline.js "$TEST_DIR" </dev/null >/dev/null
NAME=$(basename "$1"); NAME=${NAME%.*}
cat "$TEST_DIR/tests/baselines/local/$NAME"*".${2:-types}" 2>/dev/null || echo "(no .${2:-types} baseline: no errors?)"
