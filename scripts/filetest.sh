#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )/.."

# Set up the directory needed to run the test
# (A directory of its own: several agents may run this at once)
TEST_DIR=$(mktemp -d /tmp/testfile.XXXXXX)
trap 'rm -rf "$TEST_DIR"' EXIT
mkdir -p "$TEST_DIR/tests/cases/conformance" "$TEST_DIR/tests/cases/compiler"
echo "const tsCommit = '';" >"$TEST_DIR/baseline_results.ts"

# Put the test file in place
TEST_FILE="$1"
shift
cp "$TEST_FILE" "$TEST_DIR/tests/cases/conformance/"

# Get tsc's (tsgo's) baselines, and put them where the tttt runner expects them (baselines/reference/SUITE beside tests)
echo "Getting baselines from tsc..."
scripts/tsgoprobe.sh "$TEST_DIR" >/dev/null
mv "$TEST_DIR/baselines/local" "$TEST_DIR/baselines/reference"

# Run the test (FAST=1: release build without valgrind, much quicker to iterate with)
RUNNER=scripts/test.sh
if test -n "${FAST:-}"; then RUNNER=scripts/fasttest.sh; fi
"$RUNNER" --dir "$TEST_DIR/tests" --results-path "$TEST_DIR/baseline_results.ts" "$@"
