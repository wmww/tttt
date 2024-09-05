#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )"

# Clean up any old Valgrind artifacts
rm -f vgcore.*

# Configure if needed
if test ! -e debug; then
    meson setup debug --buildtype debug
fi
ninja -C debug

valgrind --leak-check=full --show-leak-kinds=all --quiet debug/tttt "$@"
