#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )"

TSC_VERSION=v5.4.2

EOL="\x1b[0m\n"
BLUE="\x1b[34m"
RED="\x1b[1;31m"

if test ! -e tsc; then
    printf "${BLUE}Cloning TypeScript $TSC_VERSION...$EOL"
    git clone --depth 1 --branch $TSC_VERSION https://github.com/microsoft/TypeScript.git tsc
else
    # If we do have TypeScript, check to make sure it's the right version
    ACTUAL_TS="$(git -C tsc describe --exact-match --tags)"
    if test $ACTUAL_TS != $TSC_VERSION; then
        printf "${RED}thirdparty/tsc has TypeScript $ACTUAL_TS but it needs $TSC_VERSION, run thirdparty/refresh.sh to fix$EOL"
        exit 1
    fi
fi
