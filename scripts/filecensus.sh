#!/bin/bash
# The per-file release census (agents/notes/memory.md): at the end of checking (and at each recycle), which of each
# checker's arena bytes, node links and memos could have been dropped when the file they were made for was done.
#   scripts/filecensus.sh [--top N] -p thirdparty/corpora/zod/packages/zod/tsconfig.test.json [--checkers 1 --threads 1 ...]
# Builds a -g release copy with -DTTTT_SURVIVORS in /tmp/tttt-filecensus and runs it with TTTT_FILE_CENSUS=1
# (ty/survivors.c survivors_file_census()). Raw lines stay in /tmp/tttt-filecensus/out.txt
set -e
cd "$(dirname "$0")/.."
TOP=30
ARGS=()
while test $# -gt 0; do
    if test "$1" = --top; then TOP=$2; shift 2; else ARGS+=("$1"); shift; fi
done
B=/tmp/tttt-filecensus
if test ! -e $B/build.ninja; then
    meson setup $B --buildtype release -Dc_args='-g -DTTTT_SURVIVORS' > /dev/null
fi
ninja -C $B tttt > /dev/null
TTTT_FILE_CENSUS=1 $B/tttt --noEmit --keep-going "${ARGS[@]}" 2>&1 > /dev/null | grep '^fcensus-' > $B/out.txt || true
python3 scripts/filecensus_an.py $B/out.txt $B/tttt "$TOP"
