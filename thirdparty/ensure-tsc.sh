#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )"

# TypeScript commit 4d4f005c (branch tsgo-port, 2026-06-29): v6.0.3 plus stableTypeOrdering on by default plus three
# one-line fixes, the commit tsgo 7.0.2's _submodules/TypeScript pins (plan 48). It has no lib/ LKG: every tool uses
# built/local/tsc.js and typescript.js, which this script builds.
TSC_COMMIT=4d4f005c8541e0255a9d8791205fdce326e462bc
TSC_VERSION="6.0.3+stableTypeOrdering (${TSC_COMMIT:0:8})"

EOL="\x1b[0m\n"
BLUE="\x1b[34m"
RED="\x1b[1;31m"

if test ! -e tsc; then
    printf "${BLUE}Fetching TypeScript $TSC_VERSION...$EOL"
    mkdir tsc
    git -C tsc init --quiet
    git -C tsc remote add origin https://github.com/microsoft/TypeScript.git
    git -C tsc fetch --quiet --depth 1 origin $TSC_COMMIT
    git -C tsc checkout --quiet FETCH_HEAD
else
    # If we do have TypeScript, check to make sure it's the right commit
    ACTUAL_TS="$(git -C tsc rev-parse HEAD)"
    if test "$ACTUAL_TS" != $TSC_COMMIT; then
        printf "${RED}thirdparty/tsc has TypeScript $ACTUAL_TS but it needs $TSC_COMMIT, run thirdparty/refresh.sh to fix$EOL"
        exit 1
    fi
fi

# Build the TypeScript compiler and test infra if needed
if test ! -e tsc/built/local/run.js || test ! -e tsc/built/local/tsc.js; then
    (
        printf "\x1b[1;33mInstalling Hereby with npm...\x1b[0m\n"
        cd tsc
        npm install hereby
        printf "\x1b[1;33mBuilding compiler and tests...\x1b[0m\n"
        node ./node_modules/.bin/hereby tests
        node ./node_modules/.bin/hereby local
    )
fi
