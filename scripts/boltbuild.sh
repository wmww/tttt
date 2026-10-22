#!/usr/bin/env bash
# The shipped build relaid out by BOLT: scripts/pgobuild.sh's pgo/tttt (LTO + PGO, linked with --emit-relocs) through
# llvm-bolt's instrumentation mode, into bolt/tttt. The same code in another order: hot blocks and functions packed
# together, cold blocks split out of their functions, identical functions folded. Aimed at the front end (the
# instruction footprint is tttt's stall, agents/notes/code_layout.md). Prints the binary's path on stdout, everything
# else goes to stderr.
#
#   scripts/boltbuild.sh            # rebuild only when pgo/tttt, llvm-bolt or this script changed
#   scripts/boltbuild.sh --force    # retrain anyway
#   BOLT_DIR=bolt-x BOLT_EXTRA=-plt=hot PGO_DIR=... scripts/boltbuild.sh   # an experiment (pgobuild.sh's knobs)
#   BOLT=llvm-bolt-21 scripts/boltbuild.sh   # (llvm-bolt, merge-fdata from PATH by default, or the BOLT's directory)
#
# No perf needed: an instrumented copy of pgo/tttt counts every edge and writes a profile per process at exit. The
# training set is pgobuild.sh's (vscode and tsc stay out as the untrained check), at one thread and checker and at the
# defaults. The profiles are deleted once merged.
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
B=${BOLT_DIR:-bolt}
FORCE=
test "${1:-}" != --force || FORCE=1
LLVM_BOLT=${BOLT:-llvm-bolt}
if ! command -v "$LLVM_BOLT" >/dev/null 2>&1; then
    echo "boltbuild: $LLVM_BOLT not found: install llvm-bolt (Debian/Ubuntu: apt install llvm-bolt), or BOLT=path/to/llvm-bolt" >&2
    exit 1
fi
MERGE_FDATA=$(dirname "$(command -v "$LLVM_BOLT")")/merge-fdata
command -v "$MERGE_FDATA" > /dev/null || MERGE_FDATA=merge-fdata

PGO=$(scripts/pgobuild.sh)
mkdir -p $B
exec 9> $B/.lock
flock 9
STAMP=$( { sha256sum "$PGO"; "$LLVM_BOLT" --version | head -3; cat "$0"; echo "${BOLT_EXTRA:-}"; } | sha256sum | cut -d' ' -f1)
if test -z "$FORCE" && test -x $B/tttt && test "$(cat $B/.stamp 2>/dev/null)" = "$STAMP"; then
    echo $B/tttt
    exit 0
fi
rm -f $B/.stamp
# (grep without -q: it reads all of its input, so readelf and nm never die of SIGPIPE, which pipefail reports as a failure)
readelf -S "$PGO" | grep '\.rela\.text' > /dev/null || { echo "boltbuild: $PGO has no static relocations (--emit-relocs)" >&2; exit 1; }
# (GCC's hot/cold partitioning breaks BOLT's profile of the functions it splits: pgobuild.sh turns it off)
! nm "$PGO" | grep '\.cold$' > /dev/null || echo "boltbuild: $PGO has .cold fragments (-freorder-blocks-and-partition)" >&2

PROF=$B/prof
rm -rf "$PROF"
mkdir -p "$PROF"
"$LLVM_BOLT" "$PGO" -o $B/tttt.inst -instrument -instrumentation-file="$(realpath $PROF)/tttt" \
    -instrumentation-file-append-pid >&2

TRAIN=(
    thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json
    thirdparty/corpora/zod/packages/zod/tsconfig.test.json
    thirdparty/corpora/effect/packages/effect/tsconfig.corpus.json
    thirdparty/corpora/excalidraw/tsconfig.corpus.json
    thirdparty/corpora/webpack
)
TRAINING=
trap 'test -z "$TRAINING" || kill $TRAINING 2>/dev/null || true' EXIT
trap 'exit 143' TERM INT
train() {
    "$@" > /dev/null &
    TRAINING=$!
    wait $TRAINING || true
    TRAINING=
}
for P in "${TRAIN[@]}"; do
    echo "boltbuild: training on $P" >&2
    train $B/tttt.inst --threads 1 --checkers 1 --noEmit -p "$P"
    train $B/tttt.inst --noEmit -p "$P"
done
"$MERGE_FDATA" "$PROF"/tttt.*.fdata -o $B/tttt.fdata >&2
rm -rf "$PROF" $B/tttt.inst

# (No -hugify: with THP in madvise mode its runtime only madvises the file-backed hot text, which takes 2 MB pages
# only with CONFIG_READ_ONLY_THP_FOR_FS, unset on this box's kernel)
"$LLVM_BOLT" "$PGO" -o $B/tttt.new -data=$B/tttt.fdata \
    -reorder-blocks=ext-tsp -reorder-functions=cdsort -split-functions -split-all-cold -split-eh \
    -icf=all -use-gnu-stack -dyno-stats ${BOLT_EXTRA:-} >&2
mv $B/tttt.new $B/tttt
echo "$STAMP" > $B/.stamp
echo $B/tttt
