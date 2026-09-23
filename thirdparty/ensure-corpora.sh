#!/usr/bin/env bash
# Fetches the second-oracle corpora (plan 17) into thirdparty/corpora/ (not committed, like thirdparty/tsc): modern,
# type-heavy TypeScript projects that are clean under tsc 5.8 with their own tsconfig. Each is a pinned commit plus
# its devDependencies from the lockfile committed in corpora-locks/ (only their .d.ts files matter; install scripts
# are skipped). `./realworld.py --corpus NAME` and `./bench.sh` use them.
#
#   thirdparty/ensure-corpora.sh            # fetch whatever is missing
#   thirdparty/ensure-corpora.sh --relock   # regenerate the lockfiles from package.json (then commit them)
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )"

RELOCK=0
if test "${1:-}" = --relock; then RELOCK=1; fi

# NAME URL COMMIT PROJECT_DIR (the directory with the tsconfig.json and package.json, relative to the checkout)
CORPORA=(
    # type-fest v4.26.1: type-level utilities (template literal types, key remapping, deep recursion) + tsd tests
    "type-fest https://github.com/sindresorhus/type-fest.git 2b98d9f2ac8921b79ed07ceeef278a5406edbe05 ."
    # valibot v1.0.0, the library package: schema library with inference chains + vitest tests (expectTypeOf)
    "valibot https://github.com/fabian-hiller/valibot.git 15e430e97267711ffd9f0633bc81bb49ce60a247 library"
)

mkdir -p corpora
for ENTRY in "${CORPORA[@]}"; do
    read -r NAME URL COMMIT DIR <<<"$ENTRY"
    if test ! -e "corpora/$NAME"; then
        printf "\x1b[34mCloning corpus %s at %s...\x1b[0m\n" "$NAME" "$COMMIT"
        rm -rf "corpora/$NAME-tmp"
        git init -q "corpora/$NAME-tmp"
        git -C "corpora/$NAME-tmp" fetch -q --depth 1 "$URL" "$COMMIT"
        git -C "corpora/$NAME-tmp" -c advice.detachedHead=false checkout -q FETCH_HEAD
        mv "corpora/$NAME-tmp" "corpora/$NAME"
    elif test "$(git -C "corpora/$NAME" rev-parse HEAD)" != "$COMMIT"; then
        printf "\x1b[1;31mthirdparty/corpora/%s is not at %s, delete it and rerun\x1b[0m\n" "$NAME" "$COMMIT"
        exit 1
    fi
    PROJECT="corpora/$NAME/$DIR"
    LOCK="corpora-locks/$NAME.package-lock.json"
    if test $RELOCK = 1; then
        rm -rf "$PROJECT/node_modules" "$PROJECT/package-lock.json"
        (cd "$PROJECT" && npm install --package-lock-only --ignore-scripts --legacy-peer-deps --no-audit --no-fund)
        cp "$PROJECT/package-lock.json" "$LOCK"
    fi
    if test ! -e "$PROJECT/node_modules"; then
        printf "\x1b[34mInstalling the dependencies of corpus %s...\x1b[0m\n" "$NAME"
        cp "$LOCK" "$PROJECT/package-lock.json"
        (cd "$PROJECT" && npm ci --ignore-scripts --legacy-peer-deps --no-audit --no-fund > /dev/null)
    fi
done
