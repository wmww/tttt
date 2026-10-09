#!/usr/bin/env bash
# What tsc (tsgo 7.1) says an identifier's type is inside a real project (a corpus), where a reduced probe may not reproduce:
#   scripts/tsctypeat.sh thirdparty/corpora/webpack/tsconfig.json lib/MultiCompiler.js 629 watchings
#   scripts/tsctypeat.sh thirdparty/corpora/webpack/tsconfig.json lib/MultiCompiler.js '*' watching   (every line)
# The file is relative to the tsconfig's directory. Prints `line name : type` for each occurrence (test/oracle's
# tttt-oracle --type-at, one checker).
set -euo pipefail
if test $# != 4; then echo "usage: scripts/tsctypeat.sh TSCONFIG FILE LINE|* IDENTIFIER" >&2; exit 2; fi
ROOT="$(cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd)"
"$ROOT/thirdparty/ensure-typescript.sh" > /dev/null
exec "$ROOT/thirdparty/typescript/built/tttt-oracle" --type-at "$@"
