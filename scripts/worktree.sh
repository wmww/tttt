#!/usr/bin/env bash
# A worktree for a sub-agent: scripts/worktree.sh NAME [BASE] makes /tmp/wt-NAME on branch wt-NAME at BASE (default
# HEAD) with the untracked thirdparty checkouts symlinked in (they are shared: probes go elsewhere, and ensure-*.sh,
# dtsdiff --tsc, flagflip --tsc run only from the main tree). `scripts/worktree.sh --rm NAME` removes it and its branch.
set -euo pipefail
cd "$(dirname "$0")/.."
if test "${1:-}" = --rm; then
    git worktree remove --force "/tmp/wt-$2" 2>/dev/null || true
    git worktree prune
    git branch -D "wt-$2" 2>/dev/null || true
    exit 0
fi
name=$1; base=${2:-HEAD}; dir=/tmp/wt-$name
git worktree add -b "wt-$name" "$dir" "$base" >/dev/null
for d in typescript typescript-gopath go corpora corpora-locks sources node; do
    test -e "thirdparty/$d" && ln -s "$(realpath "thirdparty/$d")" "$dir/thirdparty/$d"
done
echo "$dir"
