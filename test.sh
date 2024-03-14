#!/usr/bin/env bash
set -euo pipefail

rm -f vgcore.*
if test ! -e build; then
    meson setup build
fi
ninja -C build
valgrind --leak-check=full --show-leak-kinds=all --exit-on-first-error=yes --error-exitcode=1 --quiet ./build/baseline_tests "$@"
