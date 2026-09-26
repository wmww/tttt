#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )"

# We need a copy of the TypeScript repo to run tests
thirdparty/ensure-tsc.sh

# Set up the directory needed to run the test
# (A directory of its own: several agents may run this at once)
TEST_DIR=$(mktemp -d /tmp/testfile.XXXXXX)
trap 'rm -rf "$TEST_DIR"' EXIT
mkdir -p "$TEST_DIR/tests/cases/conformance"
mkdir -p "$TEST_DIR/tests/cases/compiler"
# (/.lib/react16.d.ts and friends: @libFiles and /// references to /.lib)
cp -r thirdparty/tsc/tests/lib "$TEST_DIR/tests/lib"
echo "const tsCommit = '';" >"$TEST_DIR/baseline_results.ts"

# Put the test file in place
TEST_FILE="$1"
shift
cp "$TEST_FILE" "$TEST_DIR/tests/cases/conformance/"

# Get the tsc results
echo "Getting baselines from tsc..."
NODE_PATH=$PWD/thirdparty/tsc/node_modules/ node ./test/diffapp/tsc_custom_baseline.js "$TEST_DIR" </dev/null >/dev/null

# Put the output files where the tttt runner expects them
mv "$TEST_DIR/tests/baselines/local" "$TEST_DIR/tests/baselines/reference"

# Run the test (FAST=1: release build without valgrind, much quicker to iterate with)
RUNNER=./test.sh
if test -n "${FAST:-}"; then RUNNER=./fasttest.sh; fi
"$RUNNER" --dir "$TEST_DIR/tests" --results-path "$TEST_DIR/baseline_results.ts" "$@"
