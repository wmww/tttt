#!/usr/bin/env bash
# A fast integrity check of the shared source sweep checkouts (thirdparty/sources): the sha256 of a fixed sample of
# ~400 node_modules/@types/*/index.d.ts files across the repos (test/sourcescheck.txt), recomputed and compared.
# ensure-sources.sh hardlinks identical dependency files across repos (sources/.dedupe.db), so one stray write (a
# probe or a reducer writing through a symlink) corrupts every repo sharing that file, and the oracles then report
# tttt and tsgo "differences" that are the checkout's. sourcesweep.py and dtsdiff.py run it first and warn.
#
#   scripts/sourcescheck.sh          # a second or two: "sourcescheck: OK (N files)", else the changed files, exit 1
#   scripts/sourcescheck.sh --save   # after thirdparty/ensure-sources.sh installed or re-pinned repos: a new sample
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
LIST=$PWD/test/sourcescheck.txt
SRC=thirdparty/sources
if test "${1:-}" = --save; then
    tmp=$(mktemp)
    # (Distinct files only, by inode: the hardlinked copies are one file; a stable sample by the path's hash)
    for f in $SRC/*/node_modules/@types/*/index.d.ts $SRC/*/*/node_modules/@types/*/index.d.ts; do
        test -f "$f" && echo "$(stat -L -c %i "$f") $(echo "$f" | sha1sum | cut -c1-8) $f"
    done | sort -k1,1 -u | sort -k2 | awk 'NR <= 400 {print $3}' > "$tmp"
    xargs -d '\n' sha256sum < "$tmp" | sed "s|  $SRC/|  |" | sort -k2 > "$LIST"
    rm -f "$tmp"
    echo "sourcescheck: saved $(wc -l < "$LIST") files to test/sourcescheck.txt"
    exit 0
fi
test -s "$LIST" || { echo "sourcescheck: no $LIST (scripts/sourcescheck.sh --save)"; exit 0; }
test -d "$SRC" || { echo "sourcescheck: no $SRC"; exit 0; }
# (A repo not installed here is no corruption: only the files whose repo exists)
bad=$(cd "$SRC" && while read -r sum path; do
    repo=${path%%/*}
    test -d "$repo" || continue
    if ! test -f "$path"; then echo "missing $path"
    elif test "$(sha256sum < "$path" | cut -d' ' -f1)" != "$sum"; then echo "changed $path"; fi
done < "$LIST")
n=$(wc -l < "$LIST")
if test -z "$bad"; then echo "sourcescheck: OK ($n files)"; exit 0; fi
echo "sourcescheck: thirdparty/sources is NOT as installed ($(echo "$bad" | wc -l) of $n sampled files):"
echo "$bad" | head -20 | sed 's/^/  /'
echo "  (a shared checkout was written into: oracle results on these repos are suspect; reinstall with"
echo "  thirdparty/ensure-sources.sh from the main tree, then scripts/sourcescheck.sh --save)"
exit 1
