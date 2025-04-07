#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )"

# We need a copy of the TypeScript repo to run tests
thirdparty/ensure-tsc.sh

# Set up the directory needed to run the test
TEST_DIR=/tmp/testfile
rm -rf "$TEST_DIR"
mkdir "$TEST_DIR"
mkdir -p "$TEST_DIR/tests/cases/conformance"
mkdir -p "$TEST_DIR/tests/cases/compiler"
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

# Run the test
./test.sh --dir "$TEST_DIR/tests" --results-path "$TEST_DIR/baseline_results.ts" "$@"

# Clean up
#rm -rf "$TEST_DIR"
