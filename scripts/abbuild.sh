#!/usr/bin/env bash
# The "before" binary of an A/B, built out of tree: prints the path of tttt built from REV's tree, never touching the
# caller's release/, index or working tree (nor `git worktree list`: the tree is a `git archive` export).
#
#   scripts/abbuild.sh main                      # /tmp/abbuild-<tree>/release/tttt (baseline_tests next to it)
#   scripts/abbuild.sh HEAD --patch my.diff      # HEAD with a patch applied
#   scripts/abbuild.sh HEAD --revert abc123      # HEAD without commit abc123 ("this tree without my change")
#   scripts/abbuild.sh main --fast               # fastbuild.sh's pipeline there (PGO + BOLT, ~4 min): /tmp/.../fast/tttt
#   scripts/abbuild.sh main --suite-results      # also main's suite results (baseline_tests -s), printed instead:
#                                                # `baseline_tests --summary --results-path THAT` lists what YOUR
#                                                # change gained and lost, not main's unsaved drift
#   scripts/abbuild.sh main --dir                # the export's root (to run scripts from there)
#   scripts/abbuild.sh --clean                   # delete every build not used for a day (--clean all: every one)
#
# Builds are keyed by the tree hash (a --patch/--revert tree is written with a temporary index: a dangling tree
# object, no ref), so equal trees share one build, and a lock (flock on /tmp/abbuild-<tree>.lock) makes two callers
# share it rather than race. Untracked thirdparty checkouts (typescript, corpora, sources...) are symlinked in like
# scripts/worktree.sh does. ~75 MB of source and ~35 MB of build each on /tmp's tmpfs: --clean them.
#
# It is also the answer to "I can't rebuild while a background oracle runs": run the oracle on a frozen copy
# (`scripts/abbuild.sh HEAD --dir`, then the oracle from that directory, or `--bin $(scripts/abbuild.sh HEAD)` where a
# script takes one) and keep editing and rebuilding release/ meanwhile.
set -euo pipefail
ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
cd "$ROOT"
usage() { sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }

if test "${1:-}" = --clean; then
    for d in /tmp/abbuild-*/; do
        test -d "$d" || continue
        d=${d%/}
        if test "${2:-}" = all || test -z "$(find "$d/.used" -mmin -1440 2>/dev/null)"; then
            echo "removing $d" >&2
            ( flock 9; rm -rf "$d" ) 9> "$d.lock"
            rm -f "$d.lock"
        fi
    done
    exit 0
fi

REV=
PATCH=
REVERT=
MODE=release
WANT=bin
while test $# -gt 0; do
    case "$1" in
        --patch) PATCH=$(realpath "$2"); shift 2 ;;
        --revert) REVERT="$2"; shift 2 ;;
        --fast) MODE=fast; shift ;;
        --suite-results) WANT=suite; MODE=release; shift ;;
        --dir) WANT=dir; shift ;;
        -h|--help) usage ;;
        -*) echo "abbuild.sh: unknown option $1" >&2; usage ;;
        *) test -z "$REV" || usage; REV="$1"; shift ;;
    esac
done
test -n "$REV" || usage
COMMIT=$(git rev-parse --verify "$REV^{commit}")
TREE=$(git rev-parse "$COMMIT^{tree}")
if test -n "$PATCH" || test -n "$REVERT"; then
    IDX=$(mktemp /tmp/abbuild-index.XXXXXX)
    trap 'rm -f "$IDX"' EXIT
    GIT_INDEX_FILE=$IDX git read-tree "$TREE"
    if test -n "$PATCH"; then
        GIT_INDEX_FILE=$IDX git apply --cached "$PATCH" || { echo "abbuild.sh: $PATCH doesn't apply to $REV" >&2; exit 1; }
    fi
    if test -n "$REVERT"; then
        R=$(git rev-parse --verify "$REVERT^{commit}")
        git diff --binary "$R^" "$R" | GIT_INDEX_FILE=$IDX git apply --cached -R \
            || { echo "abbuild.sh: $REVERT doesn't revert cleanly on $REV" >&2; exit 1; }
    fi
    TREE=$(GIT_INDEX_FILE=$IDX git write-tree)
fi
D=/tmp/abbuild-$TREE
exec 9> "$D.lock"
flock 9
if ! test -e "$D/.exported"; then
    rm -rf "$D"
    mkdir -p "$D"
    echo "abbuild.sh: exporting $REV (tree ${TREE:0:12}) to $D" >&2
    git archive "$TREE" | tar -x -C "$D"
    for t in typescript typescript-gopath go corpora sources node; do
        test -e "thirdparty/$t" && ln -s "$(realpath "thirdparty/$t")" "$D/thirdparty/$t"
    done
    echo "$REV $COMMIT${PATCH:+ patch $PATCH}${REVERT:+ revert $REVERT}" > "$D/.exported"
fi
touch "$D/.used"
LOG="$D/build-$MODE.log"
if test "$MODE" = fast; then
    ( cd "$D" && scripts/fastbuild.sh ) > "$LOG" 2>&1 || { tail -30 "$LOG" >&2; echo "abbuild.sh: fastbuild failed (log: $LOG)" >&2; exit 1; }
    BIN="$D/fast/tttt"
else
    {
        test -e "$D/release/build.ninja" || meson setup "$D/release" "$D" --buildtype release
        ninja -C "$D/release"
    } > "$LOG" 2>&1 || { tail -30 "$LOG" >&2; echo "abbuild.sh: build failed (log: $LOG)" >&2; exit 1; }
    BIN="$D/release/tttt"
fi
case "$WANT" in
    bin) echo "$BIN" ;;
    dir) echo "$D" ;;
    suite)
        # (The tests and baselines are the export's: an own test added by the caller is "new" against it)
        RES="$D/suite-results.ts"
        TMPRES="$D/suite-results.tmp.ts"
        if ! test -s "$RES"; then
            echo "abbuild.sh: running the suite at $REV" >&2
            cp "$D/test/baseline_results.ts" "$TMPRES"
            ( cd "$D" && ./release/baseline_tests --summary -s --results-path "$TMPRES" ) > "$D/suite.log" 2>&1 \
                || { tail -20 "$D/suite.log" >&2; echo "abbuild.sh: the suite failed (log: $D/suite.log)" >&2; exit 1; }
            mv "$TMPRES" "$RES"
        fi
        echo "$RES" ;;
esac
