#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )/.."

# Configure if needed
if test ! -e release; then
    meson setup release --buildtype release
fi
ninja -C release

# We need a copy of the TypeScript repo to run tests
thirdparty/ensure-tsc.sh

./release/baseline_tests "$@"
