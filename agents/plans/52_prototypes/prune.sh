#!/usr/bin/env bash
# Prototype: delete what neither tsc nor tttt reads from node_modules (keep TS/JS/JSON), then hardlink identical files
set -eu
for r in "$@"; do
  find "$r" -path '*/node_modules/*' -type f ! -regex '.*\.\([cm]?[jt]sx?\|json\)$' -delete
done
