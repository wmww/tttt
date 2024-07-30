#!/usr/bin/env bash
set -euo pipefail

rm -f vgcore.*
if test ! -e build-release; then
    meson setup build-release --buildtype release
fi
ninja -C build-release
./build-release/baseline_tests "$@"
