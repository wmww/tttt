#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the project root
cd "$( dirname "${BASH_SOURCE[0]}" )/.."

DESIRED_TS=v5.4.2
#DESIRED_TS=v5.5.4

# Make sure we have TypeScript
if test ! -e TypeScript; then
    printf "\x1b[1;33mTypeScript repo not found, cloning $DESIRED_TS...\x1b[0m\n"
    git clone --depth 1 --branch $DESIRED_TS https://github.com/microsoft/TypeScript.git
else
    ACTUAL_TS="$(git -C TypeScript describe --exact-match --tags)"
    if test $ACTUAL_TS != $DESIRED_TS; then
        printf "\x1b[1;31mYou should have TypeScript $DESIRED_TS, but $ACTUAL_TS is checked out. "
        printf "Please check out the right version or just delete the TypeScript directory (it will be re-cloned)\x1b[0m\n"
        exit 1
    fi
fi
