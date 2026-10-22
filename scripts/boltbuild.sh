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

# The instrumentation runtime llvm-bolt links into the instrumented copy. Arch's llvm-bolt package compiles it with GCC
# and -fno-plt, so its memset calls go through the GOT, which BOLT's JITLink rejects ("unsupported edge kind
# RequestGOTAndTransformToPCRel32GOTLoadRelaxable"): such a runtime is rebuilt from LLVM's source at llvm-bolt's
# version (as LLVM's CMakeLists.txt compiles it, plus -fplt), once, into $B/rt-VERSION
RT_ARGS=()
RT_DEFAULT=$(dirname "$(realpath "$(command -v "$LLVM_BOLT")")")/../lib/libbolt_rt_instr.a
if test -f "$RT_DEFAULT" && readelf -r "$RT_DEFAULT" | grep GOTPCREL > /dev/null; then
    VER=$("$LLVM_BOLT" --version | sed -n 's/.*LLVM version \([0-9][0-9.]*\).*/\1/p')
    RT=$B/rt-$VER
    if ! test -f $RT/libbolt_rt_instr.a; then
        echo "boltbuild: $RT_DEFAULT calls through the GOT, rebuilding it from llvm-project llvmorg-$VER" >&2
        rm -rf $RT.tmp
        mkdir -p $RT.tmp
        for F in instr.cpp common.h sys_x86_64.h; do
            curl -sfL -o $RT.tmp/$F "https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-$VER/bolt/runtime/$F" ||
                { echo "boltbuild: can't fetch bolt/runtime/$F at llvmorg-$VER" >&2; exit 1; }
        done
        echo '#define HAVE_ELF_H' > $RT.tmp/config.h
        g++ -std=c++17 -O3 -DNDEBUG -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fPIC -fplt \
            -fomit-frame-pointer -mno-sse -mgeneral-regs-only -I$RT.tmp -c $RT.tmp/instr.cpp -o $RT.tmp/instr.cpp.o
        ar rcs $RT.tmp/libbolt_rt_instr.a $RT.tmp/instr.cpp.o
        rm -rf $RT
        mv $RT.tmp $RT
    fi
    RT_ARGS=(-runtime-instrumentation-lib="$(realpath $RT/libbolt_rt_instr.a)")
fi

PROF=$B/prof
rm -rf "$PROF"
mkdir -p "$PROF"
"$LLVM_BOLT" "$PGO" -o $B/tttt.inst -instrument -instrumentation-file="$(realpath $PROF)/tttt" \
    -instrumentation-file-append-pid "${RT_ARGS[@]}" >&2

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
