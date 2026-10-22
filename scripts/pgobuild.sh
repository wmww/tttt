#!/usr/bin/env bash
# The shipped build (plan 86 stage 1): tttt with LTO and GCC's profile-guided optimization, in pgo/ (pgo/tttt).
# `release/` stays a plain -O3 build for the edit-test loop; the perf tools (bench.sh, instrcount.sh, perfsweep.py)
# measure this one. Prints the binary's path on stdout, everything else goes to stderr.
#
#   scripts/pgobuild.sh            # (re)train only when the sources, the compiler or this script changed
#   scripts/pgobuild.sh --force    # retrain anyway
#   PGO_DIR=pgo-x PGO_CFLAGS='-fno-plt' PGO_LDFLAGS='-Wl,-z,now' scripts/pgobuild.sh   # an experiment beside pgo/
#                                  # (extra compiler and linker flags, space-separated; scripts/boltbuild.sh passes
#                                  # them through and takes BOLT_DIR)
#
# The profile is regenerated here, never checked in: a .gcda set only matches the exact sources and compiler that
# wrote it (a stale one is -Werror=coverage-mismatch per object). Training set: drizzle, zod, effect, excalidraw and
# webpack, `-p --noEmit` at one thread and one checker and at the defaults. vscode and tsc are kept out of it: they're
# the untrained check that PGO isn't fitted to its measure. Takes ~2 min on 4 cores (two LTO builds and the training
# runs); a build with a fresh stamp is a no-op.
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
B=${PGO_DIR:-pgo}
FORCE=
test "${1:-}" != --force || FORCE=1

# The stamp: every source the build reads, the compiler, the build files and this script (the training set)
STAMP=$( { gcc --version | head -1
           find src thirdparty/cleat -type f \( -name '*.c' -o -name '*.h' -o -name meson.build \) | LC_ALL=C sort | xargs cat
           cat meson.build thirdparty/meson.build "$0"; echo "${PGO_CFLAGS:-}|${PGO_LDFLAGS:-}"; } | sha256sum | cut -d' ' -f1)
if test -z "$FORCE" && test -x $B/tttt && test "$(cat $B/.stamp 2>/dev/null)" = "$STAMP"; then
    echo $B/tttt
    exit 0
fi
# One build at a time, and none while a tttt of this build runs: a training run orphaned by a killed build (an
# instrcount.sh under `timeout`) writes its .gcda files at exit, over the next build's ("src_ty_expr.c.gcda is
# corrupted")
mkdir -p $B
exec 9> $B/.lock
flock 9
rm -f $B/.stamp
EXE=$(realpath $B/tttt 2>/dev/null || true)
while test -n "$EXE"; do
    BUSY=
    for P in $(pgrep -x tttt || true); do
        case "$(readlink /proc/$P/exe 2>/dev/null || true)" in "$EXE"|"$EXE (deleted)") BUSY=$P ;; esac
    done
    test -n "$BUSY" || break
    echo "pgobuild: waiting for $B/tttt (pid $BUSY) to exit" >&2
    sleep 1
done

# For scripts/boltbuild.sh, which relays this binary out: --emit-relocs keeps the static relocations in it (not
# loaded) so llvm-bolt can move functions and blocks, and -fno-reorder-blocks-and-partition keeps GCC from splitting
# each function's cold blocks into a `.cold` fragment of its own: BOLT can't attribute a profile across the split (its
# CFG discontinuity metric at 60% on the hottest functions; the bolted binary had 5-10% more I1 misses than this one
# instead of 17% fewer). Neutral on this binary itself under cachegrind (agents/notes/code_layout.md)
# -fno-plt with -z now: libc calls (memcpy, malloc...: ~100 M per training run) load their target from the GOT instead
# of jumping through a PLT stub (-0.2% instructions, the stubs' lines out of the I-cache; binding every symbol at load
# costs microseconds)
# (Meson's array syntax: '-Dc_args=a,b' would be one argument)
array() { local a=() x; for x in "$@"; do a+=("'$x'"); done; (IFS=,; echo "[${a[*]}]"); }
# shellcheck disable=SC2086
LINK=$(array -Wl,--emit-relocs -Wl,-z,now ${PGO_LDFLAGS:-})
# shellcheck disable=SC2086
CARGS=(-fno-reorder-blocks-and-partition -fno-plt ${PGO_CFLAGS:-})
if test ! -e $B/build.ninja; then
    meson setup $B --buildtype release -Db_lto=true -Db_pgo=generate "-Dc_args=$(array "${CARGS[@]}")" "-Dc_link_args=$LINK" >&2
else
    meson configure $B -Db_pgo=generate "-Dc_args=$(array "${CARGS[@]}")" "-Dc_link_args=$LINK" >&2
fi
find $B -name '*.gcda' -delete
ninja -C $B tttt >&2

thirdparty/ensure-corpora.sh >&2
TRAIN=(
    thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json
    thirdparty/corpora/zod/packages/zod/tsconfig.test.json
    thirdparty/corpora/effect/packages/effect/tsconfig.corpus.json
    thirdparty/corpora/excalidraw/tsconfig.corpus.json
    thirdparty/corpora/webpack
)
# (A training run dies with this script)
TRAINING=
trap 'test -z "$TRAINING" || kill $TRAINING 2>/dev/null || true' EXIT
trap 'exit 143' TERM INT
train() {
    "$@" > /dev/null &
    TRAINING=$!
    # (Their exit codes don't matter: webpack has errors under tsc too)
    wait $TRAINING || true
    TRAINING=
}
for P in "${TRAIN[@]}"; do
    echo "pgobuild: training on $P" >&2
    train $B/tttt --threads 1 --checkers 1 --noEmit -p "$P"
    train $B/tttt --noEmit -p "$P"
done

# Functions the training never reached have no profile: that's -Wmissing-profile, which -Werror would make fatal
meson configure $B -Db_pgo=use "-Dc_args=$(array -Wno-missing-profile "${CARGS[@]}")" >&2
ninja -C $B tttt >&2
echo "$STAMP" > $B/.stamp
echo $B/tttt
