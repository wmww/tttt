#!/usr/bin/env bash
# Land a worker's branch on main, for the coordinator (or its merger) in the main tree:
#
#   scripts/land.sh BRANCH [--no-gate] [-p PROJECT_REGEX] [-k]
#
# 1. refuses when the tree isn't on a clean main, or when the branch's commits touch test/baseline_results.ts (workers
#    don't commit it: the coordinator saves it per batch with `scripts/fasttest.sh -s`; the offending commits are
#    listed; in the worktree, `git rebase main -x 'git checkout main -- test/baseline_results.ts &&
#    git commit -q --amend --no-edit --allow-empty'` takes main's copy into each commit)
# 2. rebases the branch's commits onto main on a detached HEAD (the branch itself stays checked out in its worktree,
#    untouched); a conflict aborts the rebase and returns to main
# 3. runs scripts/quickgate.sh (with -p/-k passed through) on the rebased tree unless --no-gate; a failure returns to
#    main with the rebased commit printed, for a look
# 4. fast-forwards main to it, `git pull --rebase`, `git push origin main`, prints the landed commits
#
# Afterwards remove the worktree and branch (`scripts/worktree.sh --rm NAME`), and save the suite's results when the
# batch is done.
set -uo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
BRANCH=
GATE=1
GATE_ARGS=()
while test $# -gt 0; do
    case "$1" in
        --no-gate) GATE=; shift ;;
        -p) GATE_ARGS+=(-p "$2"); shift 2 ;;
        -k) GATE_ARGS+=(-k); shift ;;
        -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) BRANCH="$1"; shift ;;
    esac
done
test -n "$BRANCH" || { echo "usage: scripts/land.sh BRANCH [--no-gate] [-p REGEX] [-k]" >&2; exit 2; }
die() { echo "land: $*" >&2; exit 1; }

test "$(git symbolic-ref --short -q HEAD)" = main || die "not on main (git checkout main first)"
test -z "$(git status --porcelain --untracked-files=no)" || die "the tree has uncommitted changes: $(git status --short --untracked-files=no | head -5 | tr '\n' ' ')"
git rev-parse -q --verify "$BRANCH^{commit}" > /dev/null || die "no branch $BRANCH"
BASE=$(git rev-parse main)
COMMITS=$(git log --reverse --format='%h %s' "main..$BRANCH")
test -n "$COMMITS" || die "$BRANCH has no commits that main doesn't"
TOUCH=$(git log --format='  %h %s' "main..$BRANCH" -- test/baseline_results.ts)
if test -n "$TOUCH"; then
    die "$BRANCH's commits touch test/baseline_results.ts (workers don't commit it; the coordinator saves it per batch):
$TOUCH
in its worktree: git rebase main -x 'git checkout main -- test/baseline_results.ts && git commit -q --amend --no-edit --allow-empty'"
fi
echo "landing $BRANCH onto main ($(git rev-parse --short main)):"
echo "$COMMITS" | sed 's/^/  /'

git checkout -q --detach "$BRANCH" || die "can't check out $BRANCH detached"
back() { git checkout -q main; }
if ! out=$(git rebase -q main 2>&1); then
    echo "$out"
    git rebase --abort
    back
    die "rebasing $BRANCH onto main conflicts (aborted, back on main): rebase it in its worktree"
fi
NEW=$(git rev-parse HEAD)
if test -n "$GATE"; then
    if ! scripts/quickgate.sh "${GATE_ARGS[@]}"; then
        back
        die "the gate failed on the rebased commits ($(git rev-parse --short "$NEW"): git checkout --detach $NEW to look); main is unchanged"
    fi
fi
back
git merge -q --ff-only "$NEW" || die "main moved meanwhile: fast-forward to $NEW failed"
if ! git pull -q --rebase; then
    git rebase --abort 2>/dev/null
    git reset -q --hard "$NEW"
    die "git pull --rebase conflicts with origin (aborted; main is at the landed commits, unpushed)"
fi
git push -q origin main || die "git push failed (main has the landed commits locally)"
echo "landed on main and pushed:"
git log --format='  %h %s' "$BASE..main"
echo "now: scripts/worktree.sh --rm NAME for its worktree and branch ($BRANCH still points at the old commits)"
