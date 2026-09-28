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

# NAME URL COMMIT PROJECT_DIR [LINK] (PROJECT_DIR: the directory with the package.json the dependencies are installed
# for, relative to the checkout; LINK: node_modules/NAME is a symlink to LINK, relative to node_modules, as a project
# that imports its own package name gets from `yarn link` or a workspace). If corpora-locks/NAME.package.json exists, it
# replaces PROJECT_DIR/package.json before installing (a monorepo whose package manager npm can't read). The files under
# corpora-locks/NAME.overlay/ are copied into the checkout (a tsconfig of our own, when upstream's doesn't fit)
CORPORA=(
    # type-fest v4.26.1: type-level utilities (template literal types, key remapping, deep recursion) + tsd tests
    "type-fest https://github.com/sindresorhus/type-fest.git 2b98d9f2ac8921b79ed07ceeef278a5406edbe05 ."
    # valibot v1.0.0, the library package: schema library with inference chains + vitest tests (expectTypeOf)
    "valibot https://github.com/fabian-hiller/valibot.git 15e430e97267711ffd9f0633bc81bb49ce60a247 library"
    # bulletproof-react (May 2026), the Vite app: a React 18 application (react-query, react-router, radix,
    # react-hook-form + zod, tailwind) with vitest/testing-library tests; the lock is npm's resolution of its
    # package.json (upstream ships a yarn.lock)
    "bulletproof-react https://github.com/alan2207/bulletproof-react.git 9506629ed003a561c6627735480cce4994244bb4 apps/react-vite"
    # nestjs-boilerplate (Aug 2026, plan 31): a NestJS 11 server application (TypeORM + Mongoose, class-validator,
    # passport, swagger; experimentalDecorators + emitDecoratorMetadata, CommonJS, @types/node) with jest e2e tests;
    # the lock is upstream's
    "nestjs-boilerplate https://github.com/brocoders/nestjs-boilerplate.git 9620f159eefe38f47747d02ab162852367c5472c ."
    # webpack (May 2026, plan 33): the last commit before its move to TypeScript 6. JavaScript checked with checkJs and
    # JSDoc types (lib/, tooling/, examples' configs), CommonJS, @types/node. Its examples require("webpack"). The lock
    # is npm's resolution of its package.json with watchpack held at yarn.lock's 2.5.1 (2.5.2 fails tsc; --relock
    # loses that, redo it by hand)
    "webpack https://github.com/webpack/webpack.git 45a1bab632b09773938e5bde99b2614ee78f2284 . .."
    # zod v4.6.5 (Sep 2026): the schema library's package (classic, mini, core, v3) and its vitest tests (expectTypeOf,
    # heavy inference), checked through packages/zod/tsconfig.test.json (tsconfig.json has noUnusedLocals, which only
    # biome satisfies); exactOptionalPropertyTypes, nodenext, customConditions. A pnpm workspace: the dependencies are the
    # root's devDependencies (without `zod: workspace:*`, plus the @types/node vitest hoists), from
    # corpora-locks/zod.package.json
    "zod https://github.com/colinhacks/zod.git 59bbc03e10c636b9eb3c393dfeb552819774ec21 . ../packages/zod"
    # xstate v5.28 (Mar 2026, todo entry 1 of the sixth window): the last commit before its move to TypeScript 6. The
    # core package (state machines, setup()/createMachine()/assign() configs typed through generic call inference
    # with callbacks) and its vitest tests (types.test.ts, setup.types.test.ts: @ts-expect-error on inference
    # results), checked through packages/core/tsconfig.corpus.json from the overlay (upstream checks every package
    # through the root tsconfig, lib es2019 + ESNext.Promise, which lacks Promise.withResolvers under tsc 5.8: ours
    # has lib es2024 and src + test only). A pnpm workspace: the dependencies are core's devDependencies (without
    # @scion-scxml/test-framework) + vitest and @types/node, from corpora-locks/xstate.package.json
    "xstate https://github.com/statelyai/xstate.git f210b35ceed100615ed3acafa120e29db303cef2 ."
)

mkdir -p corpora
for ENTRY in "${CORPORA[@]}"; do
    read -r NAME URL COMMIT DIR LINK <<<"$ENTRY"
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
    if test -e "corpora-locks/$NAME.package.json"; then cp "corpora-locks/$NAME.package.json" "$PROJECT/package.json"; fi
    if test -d "corpora-locks/$NAME.overlay"; then cp -r "corpora-locks/$NAME.overlay/." "corpora/$NAME/"; fi
    if test $RELOCK = 1; then
        rm -rf "$PROJECT/node_modules" "$PROJECT/package-lock.json"
        (cd "$PROJECT" && npm install --package-lock-only --ignore-scripts --legacy-peer-deps --no-audit --no-fund)
        cp "$PROJECT/package-lock.json" "$LOCK"
    fi
    if test ! -e "$PROJECT/node_modules"; then
        printf "\x1b[34mInstalling the dependencies of corpus %s...\x1b[0m\n" "$NAME"
        cp "$LOCK" "$PROJECT/package-lock.json"
        (cd "$PROJECT" && npm ci --ignore-scripts --legacy-peer-deps --no-audit --no-fund > /dev/null)
        if test -n "$LINK"; then ln -s "$LINK" "$PROJECT/node_modules/$NAME"; fi
    fi
done
