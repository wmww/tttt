# Shared by scripts/release/build-*.sh (sourced; the repository root is the current directory)

fail() { echo "${0##*/}: $*" >&2; exit 1; }

# release_commit COMMIT: the release commit's full hash; HEAD may carry later script commits, but its src, thirdparty
# and meson.build must be the commit's, and the work tree's must be HEAD's (refused otherwise)
release_commit() {
    local c
    c=$(git rev-parse --verify "$1^{commit}")
    if test -n "$(git status --porcelain -- src thirdparty meson.build | grep -v '^??' || true)"; then
        fail "src, thirdparty or meson.build differ from HEAD: release builds are from a commit"
    fi
    git diff --quiet "$c" HEAD -- src thirdparty meson.build || fail "HEAD's src, thirdparty or meson.build differ from $c's"
    echo "$c"
}

# same_output A B: the two binaries' whole stdout and exit code identical on the PGO training corpora and TypeScript6,
# at the defaults and at one checker (the PGO build against the plain -O3 one)
same_output() {
    local P A X Y
    for P in thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json thirdparty/corpora/zod/packages/zod/tsconfig.test.json \
             thirdparty/corpora/effect/packages/effect/tsconfig.corpus.json thirdparty/corpora/excalidraw/tsconfig.corpus.json \
             thirdparty/corpora/webpack thirdparty/corpora/TypeScript6/src/compiler; do
        test -e "$P" || { echo "${0##*/}: $P missing, not compared" >&2; continue; }
        for A in "" "--checkers 1"; do
            # shellcheck disable=SC2086
            X=$("$1" $A --noEmit --pretty false -p "$P"; echo "exit $?")
            # shellcheck disable=SC2086
            Y=$("$2" $A --noEmit --pretty false -p "$P"; echo "exit $?")
            test "$X" = "$Y" || fail "$1 and $2 differ on $P $A"
            echo "${0##*/}: same output on $P $A ($(echo "$X" | wc -l | tr -d ' ') lines, $(echo "$X" | tail -1))" >&2
        done
    done
}
