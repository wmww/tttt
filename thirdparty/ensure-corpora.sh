#!/usr/bin/env bash
# Fetches the second-oracle corpora (plan 17) into thirdparty/corpora/ (not committed, like thirdparty/typescript): modern,
# type-heavy TypeScript projects that are clean under tsc 5.8 with their own tsconfig. Each is a pinned commit plus
# its devDependencies from the lockfile committed in corpora-locks/ (only their .d.ts files matter; install scripts
# are skipped). `scripts/realworld.py --corpus NAME` and `scripts/bench.sh` use them.
#
#   thirdparty/ensure-corpora.sh            # fetch whatever is missing
#   thirdparty/ensure-corpora.sh --relock   # regenerate the lockfiles from package.json (then commit them)
#   thirdparty/ensure-corpora.sh NAME...    # only these corpora (also with --relock)
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )"

RELOCK=0
if test "${1:-}" = --relock; then RELOCK=1; shift; fi
ONLY=" $* "

# NAME URL COMMIT PROJECT_DIR [LINK] (PROJECT_DIR: the directory with the package.json the dependencies are installed
# for, relative to the checkout; LINK: node_modules/NAME is a symlink to LINK, relative to node_modules, as a project
# that imports its own package name gets from `yarn link` or a workspace; `workspace:GLOB` instead links every package
# directory GLOB matches, relative to the checkout, under its package.json name, as a workspace install does; `alias:A=T`
# links node_modules/A to T instead). If corpora-locks/NAME.package.json exists, it
# replaces PROJECT_DIR/package.json before installing (a monorepo whose package manager npm can't read). The files under
# corpora-locks/NAME.overlay/ are copied into the checkout (a tsconfig of our own, when upstream's doesn't fit), and
# corpora-locks/NAME.postinstall.sh runs in the checkout after installing (generated files a build would make)
CORPORA=(
    # type-fest v5.10.0 (Sep 2026 head, plan 48 stage 5): type-level utilities (template literal types, key remapping,
    # deep recursion: IntRange and Replace near the instantiation-depth limits) + tsd tests. Upstream is on TypeScript
    # ^5.9.2 (its CI hasn't moved); clean under tsc 6 and tsgo 7 with its own tsconfig
    "type-fest https://github.com/sindresorhus/type-fest.git e9f614f191aa039e4aefa2d41d62c2a3fd070cfd ."
    # valibot v1.5.0 (Sep 2026 head, plan 48 stage 5), the library package: schema library with inference chains +
    # vitest tests (expectTypeOf). Upstream is on TypeScript ^5.9.3 (its CI hasn't moved): checked through
    # library/tsconfig.corpus.json from the overlay, upstream's with moduleResolution bundler instead of node10
    # (deprecated in 6, removed in 7) and minus 7 type tests tsc 6 fails (keyof's union-to-tuple order under stable
    # type ordering; partial/required/record's toEqualTypeOf on optional properties, a 6.0 change: clean under 5.9.3)
    "valibot https://github.com/fabian-hiller/valibot.git d4da61a0ac696f8421f97328f329a560b67143a9 library"
    # bulletproof-react (May 2026, still upstream's head in Oct 2026, on TypeScript ^5.4.5), the Vite app: a React 18
    # application (react-query, react-router, radix, react-hook-form + zod, tailwind) with vitest/testing-library tests;
    # the lock is npm's resolution of its package.json (upstream ships a yarn.lock). Checked through
    # apps/react-vite/tsconfig.corpus.json from the overlay: upstream's with `"baseUrl": null` (6.0 deprecates baseUrl,
    # 7.0 removes it; its `paths` are relative to the tsconfig either way), clean under tsc 6 and tsgo 7
    "bulletproof-react https://github.com/alan2207/bulletproof-react.git 9506629ed003a561c6627735480cce4994244bb4 apps/react-vite"
    # nestjs-boilerplate (Aug 2026, plan 31, still upstream's head in Oct 2026, on TypeScript 5.9.3): a NestJS 11 server
    # application (TypeORM + Mongoose, class-validator, passport, swagger; experimentalDecorators +
    # emitDecoratorMetadata, CommonJS, @types/node) with jest e2e tests; the lock is upstream's. Checked through
    # tsconfig.corpus.json from the overlay: upstream's (strictNullChecks + noImplicitAny, strictBindCallApply off) with
    # `"strict": false` as 5.x defaulted (6.0's strict default finds 2 TS2322 under strictFunctionTypes and 2 TS18046
    # under useUnknownInCatchVariables) and incremental off (tsc would write a tsbuildinfo)
    "nestjs-boilerplate https://github.com/brocoders/nestjs-boilerplate.git 9620f159eefe38f47747d02ab162852367c5472c ."
    # webpack v5.111.1+ (upstream's main on 2026-10-07, plan 48 stage 5; on TypeScript ^6.0.3, checked by its CI with
    # tsc 6): JavaScript checked with checkJs, strict and JSDoc types (lib/ incl. its hand-written CSS/HTML/JS
    # syntax parsers built on lib/util/SourceProcessor.js's generic JSDoc classes, tooling/, setup/, examples'
    # configs, the minified generated schemas/**/*.check.js validators, required without a .d.ts), CommonJS,
    # @types/node. Its examples require("webpack"). Upstream's tsconfig.json as is: 0 under tsc 6; tsgo 7 is not clean
    # (~600: TS 7's reduced JS support, e.g. TS2749 for JSDoc type references to values) and can't be made so short of
    # dropping the JS. The lock is npm's resolution of its package.json (upstream ships a yarn.lock)
    "webpack https://github.com/webpack/webpack.git 86d31e06c4e3ed3c63596ee313549d16d9e76600 . .."
    # zod (main's head on 2026-10-07, 4.6.5+, plan 48 stage 5): the schema library's package (classic, mini, core, v3)
    # and its vitest tests (expectTypeOf, heavy inference), checked through packages/zod/tsconfig.test.json
    # (tsconfig.json has noUnusedLocals, which only biome satisfies); exactOptionalPropertyTypes, nodenext,
    # customConditions. Upstream is still on TypeScript ~5.5.4 (its CI hasn't moved), but needs no overlay: tsc 6 and
    # tsgo 7.0.2 are clean on it as is. The root tsconfig (realworld's zod-root) isn't clean under tsc 6 (53: missing
    # bench/fixture dependencies, unused locals) and tsgo 7.0.2 runs away on it (>18 GB). A pnpm workspace: the
    # dependencies are the root's devDependencies (without `zod: workspace:*`, plus the @types/node vitest hoists),
    # from corpora-locks/zod.package.json
    "zod https://github.com/colinhacks/zod.git 0b216ef674e297ebe41d8bf902262e56f8755822 . ../packages/zod"
    # xstate (Oct 2026, plan 48 stage 5): main's head, on TypeScript 6 (`@typescript/typescript6` ^6.0.2, CI also runs
    # tsgo 7). The core package (state machines, setup()/createMachine()/assign() configs typed through generic call
    # inference with callbacks) and its vitest tests (types.test.ts, setup.types.test.ts: @ts-expect-error on inference
    # results), checked through packages/core/tsconfig.corpus.json from the overlay: upstream's root tsconfig (lib
    # es2024, strict, nodenext, vitest/globals) restricted to core's src + test (the root checks every framework
    # package). A pnpm workspace: the dependencies are core's devDependencies (without @scion-scxml/test-framework) +
    # vitest and @types/node, from corpora-locks/xstate.package.json
    "xstate https://github.com/statelyai/xstate.git 38dcaffb20ec7f3cbb10e6161d8f0ebacca33701 ."
    # drizzle-orm (Sep 2026, todo entry 1 of the seventh window; checked again Oct 2026 for plan 48 stage 5: still
    # main's head, upstream still on TypeScript 5.6.3): the SQL query builder package (pg, mysql, sqlite, singlestore,
    # gel dialects: column builders, select/join result types through deep conditional and mapped chains) and its
    # type-tests (Expect<Equal<...>>), checked through drizzle-orm/tsconfig.corpus.json from the overlay (upstream's
    # type-tests/tsconfig.json minus src/prisma, which needs a generated @prisma/client; `"baseUrl": null` with
    # ./-relative paths `~/*` and `type-tests/*`, as 6.0 deprecates baseUrl). noUncheckedIndexedAccess, exactOptional
    # off, noPropertyAccessFromIndexSignature. Clean under tsc 6; tsgo 7.0.2 reports 16 of its own (8 TS2769/TS2578
    # pairs in type-tests/*/insert.ts and generated-columns.ts: it reports a failed overload on the object literal's
    # line rather than the call's, so the `@ts-expect-error` above the call goes unused). A pnpm workspace:
    # the dependencies are drizzle-orm's devDependencies at pnpm-lock.yaml's versions, from
    # corpora-locks/drizzle.package.json (npm floats the transitive ones)
    "drizzle https://github.com/drizzle-team/drizzle-orm.git 15454dbe49d827c6081f3d0231e2e7985e517295 drizzle-orm"
    # vue core v3.5 (Oct 2026, plan 48 stage 5): main's head, on TypeScript ~6.0.3 (upstream dropped baseUrl). Every
    # package's src and __tests__ (compiler, reactivity, runtime-core's component option overloads, runtime-dom's JSX
    # types) plus the dts-test type tests (defineComponent, props inference, JSX), checked through tsconfig.corpus.json
    # from the overlay (upstream's root tsconfig: paths to the package sources, not composite, no isolatedDeclarations,
    # no noUnusedLocals: dts-test has one unused local; dts-test's built.test-d.ts needs a build). tsc 6 is clean;
    # tsgo 7.0.2 is not (10: TS2769/TS2578 pairs in dts-test where it reports overload failures elsewhere, a TS2322,
    # a TS2345 in vModel.ts; the same at the previous pin, upstream doesn't run tsgo). A pnpm workspace: the
    # dependencies are every importer's (packages-private excluded; typescript the root's) at pnpm-lock.yaml's
    # versions (its second YAML document), from corpora-locks/vue.package.json
    "vue https://github.com/vuejs/core.git 4ab865a848a1da3d10fb674f857e5fff13094644 ."
    # typescript-eslint v8.71+ (main's head on 2026-10-07, plan 48 stage 5; on TypeScript 6: `typescript` is pnpm's
    # override npm:@typescript/typescript6@6.0.2, with stableTypeOrdering in the base config, CI also runs tsgo 7), the
    # second project-references corpus (`tttt -b` at the root solution config: 38 composite projects, nested solution
    # configs, `${configDir}` in the shared base config, NodeNext; workspace packages import each other through
    # node_modules symlinks whose package.json `types`/`exports` point at dist/*.d.ts, mapped back to the sources).
    # Clean under tsc 6 and tsgo 7 as is. A pnpm workspace: the dependencies are every importer's at pnpm-lock.yaml's
    # versions (its second YAML document; flat, no conflicts; aliases as npm: specs), in upstream's root package.json
    # without `workspaces` (integration-tests imports its packageManager), from
    # corpora-locks/typescript-eslint.package.json. The postinstall moves the hoisted @types/esquery under its only
    # dependent, as pnpm's layout has it. The overlay has the two generated files the build needs:
    # packages/types/src/generated/ast-spec.ts (ast-spec's tsc -b, then api-extractor's rollup, with an
    # overrideTsconfig since its bundled TS 5.9 rejects stableTypeOrdering, then copy-ast-spec.mts's enum transform,
    # unprettified) and packages/website/data/recent-blog-posts.json (the website's recent-blog-posts plugin)
    "typescript-eslint https://github.com/typescript-eslint/typescript-eslint.git 706298716de5edb95ff58a1ba8806f88f87423e7 . workspace:packages/*"
    # Effect v3.22.2 (Oct 2026, todo entry 2 of the ninth window; still the v3 branch's head at plan 48 stage 5):
    # upstream on TypeScript 5.8.3. main is Effect 4 on TypeScript 7 (tsgo), which tsc 6 doesn't check cleanly. The
    # core `effect` package's src, its vitest tests (through @effect/vitest's sources) and its tstyche type tests
    # (dtslint/*.tst.ts): HKT encodings, generators as do-notation, `pipe` and dual APIs with many overloads, variance
    # annotations, branded types, Schema. Checked through packages/effect/tsconfig.corpus.json from the overlay
    # (upstream's tsconfig.src/test.json and dtslint's, one program, noEmit, noUnusedLocals off as in dtslint's; the
    # base's 6.0-deprecated downlevelIteration and esModuleInterop=false unset with null). tsc 6 is clean; tsgo 7.0.2
    # is not (3: a TS2345 in Schema.ts, TS2769 x2 in @effect/vitest; upstream doesn't run tsgo on v3). A pnpm
    # workspace: the dependencies are the type dependencies of effect, @effect/vitest and the root at pnpm-lock.yaml's
    # versions, from corpora-locks/effect.package.json; the workspace packages are linked into the root's node_modules
    "effect https://github.com/Effect-TS/effect.git e730f59d29db5f1e312be6476a95d07bcad45788 . workspace:packages/*"
    # VS Code (main's head on 2026-10-07, plan 48 stage 5; upstream builds with @typescript/typescript6 ^6.0.2 and
    # checks with tsgo 7): the large application (10.1k files, 3.4M lines in src/ with monaco.d.ts and vscode.d.ts,
    # more than twice the Mar 2025 pin's): the workbench, editor, chat/agent host, platform services with
    # decorator-based dependency injection (experimentalDecorators), browser + node + electron code in one program,
    # nodenext, noImplicitOverride, useDefineForClassFields off. Checked through src/tsconfig.corpus.json from the
    # overlay: upstream's src/tsconfig.json (the extensions are separate projects, left out) minus the webview service
    # worker (a .js file). tsgo 7.1 reports 10 errors on it (resolveAsync's distributive conditional return type,
    # TS2345/TS2322/TS2416), tttt the same ones. The lock is upstream's;
    # corpora-locks/vscode.postinstall.sh fetches the electron.d.ts its `paths` map `electron` to
    "vscode https://github.com/microsoft/vscode.git 2afa73ea129cad2710fc5c6363bebc4012648070 ."
    # excalidraw (Oct 2026, plan 48 stage 5): the JSX-heavy application (whiteboard editor's React 19 components,
    # hooks, jotai atoms, the element/math/common/fractional-indexing/laser-pointer packages, the collab app,
    # vitest/testing-library tests), upstream's head, still on TypeScript 5.9.3. Checked through tsconfig.corpus.json
    # from the overlay: upstream's root tsconfig (paths to the package sources, moduleResolution bundler, jsx
    # react-jsx) with `"baseUrl": null` (its paths are all "./"-relative, so dropping baseUrl changes nothing; tsc 6
    # reports TS5101 for it, 7.0 removes the option). A yarn workspace: the dependencies are the root's, excalidraw-app's
    # and every package's (lint, format, coverage tools and the in-tree @excalidraw packages dropped), from
    # corpora-locks/excalidraw.package.json
    "excalidraw https://github.com/excalidraw/excalidraw.git 53973c3a423fbd75a4ce68107786b4fcb90e4968 ."
    # TanStack Router (Oct 2026, plan 48 stage 5): main's head, on TypeScript 6 (`@typescript/typescript6` ^6.0.2, CI
    # also runs tsgo 7 and 5.6-5.9). The inference-heavy corpus: react-router's src and tests, mostly *.test-d.tsx type
    # tests (expectTypeOf on route trees: path/search/params/loader inference through createRoute()/
    # createFileRoute() chains, Link/navigate `to` as template-literal unions over the whole tree, Register module
    # augmentation), checked through tsconfig.corpus.json from the overlay (react-router's own tsconfig, `types:
    # ["node"]`, limited to src + tests) against router-core's and history's declarations as upstream does
    # (corpora-locks/tanstack-router.postinstall.sh builds them with the oracle's tsc through their own tsconfigs:
    # checking router-core's sources inside react-router's program sees react-router's augmentations of its interfaces
    # and fails). moduleResolution bundler, noUncheckedIndexedAccess, checkJs. A pnpm workspace: the dependencies are
    # the type dependencies of react-router, router-core and the root at pnpm-lock.yaml's versions, from
    # corpora-locks/tanstack-router.package.json
    "tanstack-router https://github.com/TanStack/router.git 30fc108a5ebbc4eadd48439282bf1c27d55fbb94 . workspace:packages/*"
    # TypeScript 6's own sources (commit 4d4f005c, branch tsgo-port, 2026-06-29: v6.0.3 plus stableTypeOrdering on by
    # default, what tsgo 7.0.2 pinned; the test oracle until plan 54 moved it to tsgo 7.1): realworld's `tsc`
    # (src/compiler, 193k lines) and `tsc-src` (src, a solution config of 13 composite projects, -b). The biggest pure-TS
    # project we have, the first corpus and bench.sh's. Clean under tsc 6 and tsgo 7.1; the lock is upstream's
    "TypeScript6 https://github.com/microsoft/TypeScript.git 4d4f005c8541e0255a9d8791205fdce326e462bc ."
    # DefinitelyTyped (Oct 2026, plan 44): the breadth sweep's packages (scripts/sweep.py), every types/NAME with its own
    # tsconfig and tests. DT's pnpm workspace links each @types/X to types/X: node_modules/@types -> ../types does the
    # same for the latest version of each. The npm dependencies are the few non-@types ones that many packages need
    # (undici-types for node, csstype for react...), from corpora-locks/DefinitelyTyped.package.json; packages that
    # need others fail under tsc and are left out of the sweep
    "DefinitelyTyped https://github.com/DefinitelyTyped/DefinitelyTyped.git ac3977d854a671215b8d3e2d5e4ced9c345bf00b . alias:@types=../types"
)

# Copies SRC to DST unless DST already says the same, through a temporary file renamed into place: scripts run this
# before every corpus run while other workers' tttt and tsc read the same checkouts, and a plain `cp` truncates the
# file first (a drizzle run once read an empty tsconfig.corpus.json mid-copy: no `extends`, no `lib`, no skipLibCheck,
# 6410 diagnostics instead of 41)
install_same() {
    if cmp -s "$1" "$2"; then return 0; fi
    mkdir -p "$(dirname "$2")"
    cp "$1" "$2.tmp.$$" && mv -f "$2.tmp.$$" "$2"
}

mkdir -p corpora
for ENTRY in "${CORPORA[@]}"; do
    read -r NAME URL COMMIT DIR LINK <<<"$ENTRY"
    if test "$ONLY" != "  " && test "${ONLY#* $NAME }" = "$ONLY"; then continue; fi
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
    if test -e "corpora-locks/$NAME.package.json"; then install_same "corpora-locks/$NAME.package.json" "$PROJECT/package.json"; fi
    if test -d "corpora-locks/$NAME.overlay"; then
        (cd "corpora-locks/$NAME.overlay" && find . -type f) | while read -r FILE; do
            install_same "corpora-locks/$NAME.overlay/$FILE" "corpora/$NAME/$FILE"
        done
    fi
    if test $RELOCK = 1; then
        rm -rf "$PROJECT/node_modules" "$PROJECT/package-lock.json"
        (cd "$PROJECT" && npm install --package-lock-only --ignore-scripts --legacy-peer-deps --no-audit --no-fund)
        cp "$PROJECT/package-lock.json" "$LOCK"
    fi
    if test ! -e "$PROJECT/node_modules"; then
        printf "\x1b[34mInstalling the dependencies of corpus %s...\x1b[0m\n" "$NAME"
        cp "$LOCK" "$PROJECT/package-lock.json"
        (cd "$PROJECT" && npm ci --ignore-scripts --legacy-peer-deps --no-audit --no-fund > /dev/null)
        if test "${LINK#workspace:}" != "$LINK"; then
            for PACKAGE in corpora/$NAME/${LINK#workspace:}; do
                if test ! -e "$PACKAGE/package.json"; then continue; fi
                PACKAGE_NAME=$(node -p "require('./$PACKAGE/package.json').name")
                rm -rf "$PROJECT/node_modules/$PACKAGE_NAME"
                mkdir -p "$(dirname "$PROJECT/node_modules/$PACKAGE_NAME")"
                ln -s "$(realpath --relative-to="$(dirname "$PROJECT/node_modules/$PACKAGE_NAME")" "$PACKAGE")" "$PROJECT/node_modules/$PACKAGE_NAME"
            done
        elif test "${LINK#alias:}" != "$LINK"; then
            ALIAS=${LINK#alias:}
            rm -rf "$PROJECT/node_modules/${ALIAS%%=*}"
            mkdir -p "$(dirname "$PROJECT/node_modules/${ALIAS%%=*}")"
            ln -s "${ALIAS#*=}" "$PROJECT/node_modules/${ALIAS%%=*}"
        elif test -n "$LINK"; then ln -s "$LINK" "$PROJECT/node_modules/$NAME"; fi
        if test -e "corpora-locks/$NAME.postinstall.sh"; then (cd "corpora/$NAME" && "$OLDPWD/corpora-locks/$NAME.postinstall.sh"); fi
    fi
done
