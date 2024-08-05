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

#NODE_PATH="$PWD/test/TypeScript/node_modules" node test/diffapp/tsc_custom_baseline.js
node test/diffapp/server.js
