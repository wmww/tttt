#!/usr/bin/env bash
set -euo pipefail

rm -f vgcore.*
if test ! -e build; then
    meson setup build
fi
ninja -C build
./build/baseline_tests "$@"
