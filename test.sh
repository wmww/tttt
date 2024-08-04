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

# Make sure we have TypeScript
if test ! -e TypeScript; then
    echo "TypeScript repo not found, cloning..."
    git clone --depth 1 --branch v5.4.2 https://github.com/microsoft/TypeScript.git
fi

valgrind --leak-check=full --show-leak-kinds=all --exit-on-first-error=yes --error-exitcode=1 --quiet ./build/baseline_tests "$@"
