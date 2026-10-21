# Sourced by the other scripts: ROOT (the repo), OUT (the work directory: ref.X and every output; SG_OUT, default
# /tmp/tttt-sharedgraph), BIN_DEFAULT (release/tttt) and P[corpus] (corpus -> tsconfig). The scripts cd to OUT, so the
# file paths in tttt's output (relative to the cwd) match ref.X's
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT=${SG_OUT:-/tmp/tttt-sharedgraph}
mkdir -p "$OUT/cg"
BIN_DEFAULT=$ROOT/release/tttt
C=$ROOT/thirdparty/corpora
declare -A P=(
 [drizzle]=$C/drizzle/drizzle-orm/tsconfig.corpus.json
 [zod]=$C/zod/packages/zod/tsconfig.test.json
 [excalidraw]=$C/excalidraw/tsconfig.corpus.json
 [effect]=$C/effect/packages/effect/tsconfig.corpus.json
 [nest]=$C/nestjs-boilerplate/tsconfig.corpus.json
 [vscode]=$C/vscode/src/tsconfig.corpus.json
)
# A binary argument: an absolute path, one relative to the caller's directory, or release/tttt when empty
sg_bin() { if test -z "$1"; then echo "$BIN_DEFAULT"; else realpath "$1"; fi; }
