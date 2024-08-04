#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )"

# Clean up any old Valgrind artifacts
rm -f vgcore.*

# Configure if needed
if test ! -e build; then
    meson setup build
fi
ninja -C build

# We need a copy of the TypeScript repo to run tests
test/clone_typescript_if_needed.sh

valgrind --leak-check=full --show-leak-kinds=all --exit-on-first-error=yes --error-exitcode=1 --quiet ./build/baseline_tests "$@"
