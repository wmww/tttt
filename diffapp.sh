#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )"

# Configure if needed
if test ! -e debug; then
    meson setup debug --buildtype debug
fi

# We need a copy of the TypeScript repo to run tests
test/clone_typescript_if_needed.sh

# Build the TypeScript compiler and test infra if needed
if test ! -e test/TypeScript/built/local/run.js; then
    (
        printf "\x1b[1;33mInstalling Hereby with npm...\x1b[0m\n"
        cd test/TypeScript
        npm install hereby
        printf "\x1b[1;33mBuilding compiler and tests...\x1b[0m\n"
        node ./node_modules/.bin/hereby tests
    )
fi

if test $# -gt 0; then
    # If we're given an argument
    if test "$1" == "-h" -o "$1" == "--help"; then
        echo "Run with no argument, the path to a TypeScript source file or the name of a tsc test."
        echo "If an instance of diffapp is already running the given source will be sent to it."
        exit 0
    elif test -e "$1"; then
        # Argument is a path, assume it's a source file
        TEST_PATH="$1"
    else
        # Argument is not a valid path, assume it's a test name and search for the test
        TEST_PATH="$(find test/TypeScript/tests/cases/ -name "$1.ts" | head -n 1)"
        if test -z "$TEST_PATH"; then
            echo "Could not find test $1"
            exit 1
        fi
    fi
fi

if lsof -i :3030 >/dev/null; then
    # If an instance of diffapp is already running
    if test -z ${TEST_PATH:-}; then
        echo "diffapp already running"
    else
        # If a test is specified, request the locally running instance of diffapp load it's source
        echo "Sending $TEST_PATH to existing diffapp instance"
        curl -Ss -X POST "http://localhost:3030/setCode?token=bash&serial=$RANDOM" --data-binary "@$TEST_PATH" >/dev/null
    fi
else
    # Start diffapp
    node test/diffapp/server.js ${TEST_PATH:-}
fi

# This is how you can run the tsc test runner manually, useful for debugging it
# mkdir -p /tmp/diffapp/tests/cases/conformance
# touch /tmp/diffapp/tests/cases/conformance/customTest.ts
# NODE_PATH="$PWD/test/TypeScript/node_modules" node test/diffapp/tsc_custom_baseline.js /tmp/diffapp
