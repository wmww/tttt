#!/usr/bin/env bash
# The fastest tttt, always at fast/tttt (a relative symlink to the pipeline's binary). This script is the single
# definition of "the fastest build": when the pipeline changes (BOLT in or out, a new flag, huge pages), this script
# changes and the path doesn't. Prints the path on stdout, everything else goes to stderr; idempotent through the
# underlying builds' stamps.
#
#   scripts/fastbuild.sh             # build (only what changed) and print fast/tttt
#   scripts/fastbuild.sh --force     # retrain every stage anyway
#   scripts/fastbuild.sh --measured  # the binary the measuring scripts (bench.sh, instrcount.sh, perfsweep.py,
#                                    # cgrank.py) use by default: MEASURED below
#
# Today's pipeline: scripts/pgobuild.sh (LTO + PGO, pgo/tttt) then scripts/boltbuild.sh (BOLT's layout, bolt/tttt);
# agents/notes/code_layout.md says why. The measuring scripts still measure pgo/tttt until the laptop's counters
# confirm BOLT: flipping MEASURED to fast makes them measure fast/tttt.
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
MEASURED=fast # flipped 2026-10-22: the laptop confirmed BOLT (measurements.md "BOLT and interning A/B": -3% K1, -2..-4% defaults)
FORCE=
case "${1:-}" in
    --force) FORCE=--force ;;
    --measured)
        if test $MEASURED = pgo; then scripts/pgobuild.sh; exit; fi ;;
    '') ;;
    *) echo "usage: $0 [--force|--measured]" >&2; exit 2 ;;
esac

test -z "$FORCE" || scripts/pgobuild.sh --force > /dev/null
# (macOS and Windows: BOLT rewrites ELF only, so the pipeline ends at pgo/tttt, clang's LTO + PGO: agents/notes/macos.md,
# windows.md. Windows: fast/tttt.exe, a copy, MSYS2's ln makes no symlink without Developer Mode)
case "$(uname -s)" in
    MINGW*|MSYS*)
        BIN=$(scripts/pgobuild.sh $FORCE)
        mkdir -p fast
        cp -f "$BIN" fast/tttt.exe.new && mv -f fast/tttt.exe.new fast/tttt.exe
        echo fast/tttt.exe
        exit 0 ;;
esac
if test "$(uname -s)" = Darwin; then BIN=$(scripts/pgobuild.sh $FORCE); else BIN=$(scripts/boltbuild.sh $FORCE); fi
mkdir -p fast
# (Replaced atomically: a run of fast/tttt in flight keeps its binary)
ln -sfn "../$BIN" fast/tttt.new
# (-T is GNU's; BSD mv's -h doesn't follow a symlink to a directory either)
if test "$(uname -s)" = Darwin; then mv -fh fast/tttt.new fast/tttt; else mv -T fast/tttt.new fast/tttt; fi
echo fast/tttt
