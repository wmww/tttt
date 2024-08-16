#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )"

DESIRED_TS=v5.5.4

if test ! -e TypeScript; then
    # If we don't have TypeScript, clone it (shallow clone at the tag we want)
    printf "\x1b[1;33mTypeScript repo not found, cloning $DESIRED_TS...\x1b[0m\n"
    git clone --depth 1 --branch $DESIRED_TS https://github.com/microsoft/TypeScript.git
else
    # If we do have TypeScript, check to make sure it's the right version
    ACTUAL_TS="$(git -C TypeScript describe --exact-match --tags)"
    if test $ACTUAL_TS != $DESIRED_TS; then
        printf "\x1b[1;31mYou should have TypeScript $DESIRED_TS, but $ACTUAL_TS is checked out. "
        printf "Please check out the right version or just delete the TypeScript directory (it will be re-cloned)\x1b[0m\n"
        exit 1
    fi
fi
