#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )/.."

# Configure if needed
if test ! -e release; then
    meson setup release --buildtype release
fi
ninja -C release

# The test suite is TypeScript 7.1's native tree (thirdparty/typescript/tsc/testdata)
thirdparty/ensure-typescript.sh

./release/baseline_tests "$@"
