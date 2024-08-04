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

valgrind --leak-check=full --show-leak-kinds=all --quiet ./build/tttt "$@"
