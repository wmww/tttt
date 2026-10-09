#!/usr/bin/env bash
# Builds the declarations react-router imports its workspace dependencies through (node_modules/@tanstack/X ->
# packages/X, whose package.json `exports` point at dist/esm/*.d.ts), as upstream's vite-plugin-dts build does: the
# oracle's tsc (thirdparty/typescript/built/tsc, ensure-typescript.sh) on history's and router-core's sources. Run by
# ensure-corpora.sh in the checkout after installing.
set -euo pipefail
../../ensure-typescript.sh
TSC=../../typescript/built/tsc
$TSC -p tsconfig.dts-history.json
$TSC -p tsconfig.dts-router-core.json
