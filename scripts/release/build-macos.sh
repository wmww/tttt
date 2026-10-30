#!/usr/bin/env bash
# A release binary for macOS arm64 (agents/notes/release.md "macOS"): Apple clang LTO + PGO (scripts/pgobuild.sh in
# dist-pgo/, trained like the fast build) with a minimum macOS of $MINOS (default 13.0), checked and copied to OUTDIR
# with a build-info.txt. Run on the Mac, from a clean checkout of the release commit (src, thirdparty, meson.build
# unmodified: refused otherwise), with the PGO training corpora in place (thirdparty/ensure-corpora.sh).
#
#   scripts/release/build-macos.sh OUTDIR [COMMIT]   # OUTDIR/tttt, OUTDIR/build-info.txt
#
# COMMIT (default HEAD) is the release commit: HEAD may carry later script commits, but its src, thirdparty and
# meson.build must equal COMMIT's (refused otherwise).
#
# Also builds dist-release/ (plain -O3, same minimum) and requires the PGO binary's output to be byte-identical to it
# on the training corpora and TypeScript6 at the defaults and at one checker. What it doesn't do (by hand, notes):
# the suite and corpora against Linux, threadcheck/checkerdiff, the quarantine smoke test.
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/../.."
OUT=${1:?usage: $0 OUTDIR [COMMIT]}
# shellcheck source=scripts/release/lib.sh
. scripts/release/lib.sh
COMMIT=$(release_commit "${2:-HEAD}")
MINOS=${MINOS:-13.0}
export PATH=/opt/homebrew/bin:$PATH
test "$(uname -s)/$(uname -m)" = Darwin/arm64 || fail "run this on macOS arm64"
# -Werror=unguarded-availability: any API newer than $MINOS used without an availability check is a compile error
FLAGS="-mmacosx-version-min=$MINOS -Werror=unguarded-availability"
BIN=$(PGO_DIR=dist-pgo PGO_CFLAGS="$FLAGS" PGO_LDFLAGS="-mmacosx-version-min=$MINOS" scripts/pgobuild.sh --force)
if test ! -e dist-release/build.ninja; then
    meson setup dist-release --buildtype release "-Dc_args=['-mmacosx-version-min=$MINOS','-Werror=unguarded-availability']" \
        "-Dc_link_args=['-mmacosx-version-min=$MINOS']" >&2
fi
ninja -C dist-release tttt baseline_tests >&2

# The minimum OS in the load command, the libraries (system ones only), no weak imports (a symbol newer than the
# minimum would be one), the linker's ad-hoc signature
MIN=$(vtool -show-build "$BIN" | awk '$1 == "minos" { print $2 }')
test "$MIN" = "$MINOS" || fail "minos is $MIN, not $MINOS"
LIBS=$(otool -L "$BIN" | tail -n +2 | awk '{ print $1 }')
! echo "$LIBS" | grep -v -E '^(/usr/lib/|/System/)' || fail "non-system libraries above"
WEAK=$(nm -m "$BIN" | grep -E 'weak external .*\(from ' || true)
test -z "$WEAK" || fail "weak imports (newer than the minimum?): $WEAK"
codesign -v "$BIN" || fail "signature"

# PGO against -O3: identical output
same_output "$BIN" dist-release/tttt

mkdir -p "$OUT"
cp "$BIN" "$OUT/tttt"
SHA=$(shasum -a 256 "$OUT/tttt" | cut -d' ' -f1)
cat > "$OUT/build-info.txt" <<INFO
tttt $(git describe --tags --always "$COMMIT" 2>/dev/null || echo "$COMMIT") for macOS arm64 (darwin-arm64)
commit: $COMMIT (built at $(git rev-parse HEAD): src, thirdparty and meson.build identical)
compiler: $(clang --version | head -1) ($(xcrun --show-sdk-path), SDK $(xcrun --show-sdk-version))
minimum macOS: $MIN (LC_BUILD_VERSION minos; sdk $(vtool -show-build "$BIN" | awk '$1 == "sdk" { print $2 }'))
build: scripts/release/build-macos.sh: scripts/pgobuild.sh (LTO + clang PGO, trained on drizzle, zod, effect, excalidraw, webpack), meson --buildtype release -Db_lto=true
flags: $FLAGS; linker -mmacosx-version-min=$MINOS; ad-hoc signature (ld64), not notarized
libraries: $(echo $LIBS)
built: $(date -u +%Y-%m-%dT%H:%M:%SZ) on macOS $(sw_vers -productVersion) ($(sw_vers -buildVersion))
sha256: $SHA
INFO
cat "$OUT/build-info.txt" >&2
echo "$OUT/tttt"
