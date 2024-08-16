#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )"

# Configure if needed
if test ! -e build; then
    meson setup build
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
    if test -e "$1"; then
        TEST_PATH="$1"
    else
        TEST_PATH="$(find test/TypeScript/tests/cases/ -name "$1.ts" | head -n 1)"
        if test -z "$TEST_PATH"; then
            echo "Could not find test $1"
            exit 1
        fi
    fi
fi

#NODE_PATH="$PWD/test/TypeScript/node_modules" node test/diffapp/tsc_custom_baseline.js /tmp/diffapp test/diffapp/defaultCustomTest.ts

if lsof -i :3030 >/dev/null; then
    if test -z ${TEST_PATH:-}; then
        echo "diffapp already running"
    else
        echo "Sending $TEST_PATH to existing diffapp instance"
        curl -X POST "http://localhost:3030/setCode?token=bash&serial=$RANDOM" --data-binary "@$TEST_PATH"
    fi
else
    node test/diffapp/server.js ${TEST_PATH:-}
fi
