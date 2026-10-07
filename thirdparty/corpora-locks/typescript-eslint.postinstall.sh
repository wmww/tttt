#!/usr/bin/env bash
# npm hoists @phenomnomnominal/tsquery's @types/esquery to the root, where packages/website's `import esquery from
# 'esquery'` resolves to it and its own `declare module 'esquery'` (typings/esquery.d.ts, forked from it) merges in:
# TS2322 'typeof query & typeof query'. pnpm keeps it private to tsquery: do the same. Run by ensure-corpora.sh in the
# checkout after installing.
set -euo pipefail
mkdir -p node_modules/@phenomnomnominal/tsquery/node_modules/@types
mv node_modules/@types/esquery node_modules/@phenomnomnominal/tsquery/node_modules/@types/esquery
