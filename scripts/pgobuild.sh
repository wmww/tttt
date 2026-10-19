#!/usr/bin/env bash
# The shipped build (plan 86 stage 1): tttt with LTO and GCC's profile-guided optimization, in pgo/ (pgo/tttt).
# `release/` stays a plain -O3 build for the edit-test loop; the perf tools (bench.sh, instrcount.sh, perfsweep.py)
# measure this one. Prints the binary's path on stdout, everything else goes to stderr.
#
#   scripts/pgobuild.sh            # (re)train only when the sources, the compiler or this script changed
#   scripts/pgobuild.sh --force    # retrain anyway
#
# The profile is regenerated here, never checked in: a .gcda set only matches the exact sources and compiler that
# wrote it (a stale one is -Werror=coverage-mismatch per object). Training set: drizzle, zod, effect, excalidraw and
# webpack, `-p --noEmit` at one thread and one checker and at the defaults. vscode and tsc are kept out of it: they're
# the untrained check that PGO isn't fitted to its measure. Takes ~2 min on 4 cores (two LTO builds and the training
# runs); a build with a fresh stamp is a no-op.
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
B=pgo
FORCE=
test "${1:-}" != --force || FORCE=1

# The stamp: every source the build reads, the compiler, the build files and this script (the training set)
STAMP=$( { gcc --version | head -1
           find src thirdparty/cleat -type f \( -name '*.c' -o -name '*.h' -o -name meson.build \) | LC_ALL=C sort | xargs cat
           cat meson.build thirdparty/meson.build "$0"; } | sha256sum | cut -d' ' -f1)
if test -z "$FORCE" && test -x $B/tttt && test "$(cat $B/.stamp 2>/dev/null)" = "$STAMP"; then
    echo $B/tttt
    exit 0
fi
rm -f $B/.stamp

if test ! -e $B/build.ninja; then
    meson setup $B --buildtype release -Db_lto=true -Db_pgo=generate >&2
else
    meson configure $B -Db_pgo=generate -Dc_args= >&2
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
for P in "${TRAIN[@]}"; do
    echo "pgobuild: training on $P" >&2
    # (Their exit codes don't matter: webpack has errors under tsc too)
    $B/tttt --threads 1 --checkers 1 --noEmit -p "$P" > /dev/null || true
    $B/tttt --noEmit -p "$P" > /dev/null || true
done

# Functions the training never reached have no profile: that's -Wmissing-profile, which -Werror would make fatal
meson configure $B -Db_pgo=use -Dc_args=-Wno-missing-profile >&2
ninja -C $B tttt >&2
echo "$STAMP" > $B/.stamp
echo $B/tttt
