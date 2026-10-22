# The pinned corpora (reference)
What each `scripts/realworld.py --corpus X` corpus is and what it exercises; `thirdparty/ensure-corpora.sh` and
`corpora-locks/*.overlay` pin them. Every corpus is 0 / 0 against tsgo 7.1 but vue 0 / 2 (bugs/sourcesweep_checker.md;
measurements.md has the latest). This file replaced the corpora-to-zero work ("the corpora to zero", complete 2026-10-15); its
per-corpus history (counts at pinning, the fixes, by cause) is in git (`git log -- agents/plans/19_corpora_to_zero.md`)
and the probes in `test/probes/plan19/<corpus>/`.

## The corpora
- **tsc** (TypeScript 6's `src/compiler`, 193k lines) and **tsc-src** (its `src`, `-b`): the first corpus.
- **valibot**, **type-fest** (peak RSS on the IntRange tests), **zod** (`tsconfig.test.json`, a pnpm workspace whose
  root devDependencies come from `corpora-locks/zod.package.json`; deep-partial.test.ts is its heaviest file),
  **zod-root** (zod's root tsconfig: **not clean under tsc**, compare sets with `comm`, don't count).
- **react** (`baseUrl: null`: a JSON `null` in compilerOptions unsets baseUrl and the booleans), **nest**
  (`tsconfig.corpus.json`, `strict: false`; nestjs-i18n's `.d.ts` imports names its module doesn't export, hidden by
  skipLibCheck, so those are error types there).
- **webpack** (JavaScript with JSDoc, checkJs; tsc is clean only with ensure-corpora.sh's pins: watchpack 2.5.1,
  `node_modules/webpack -> ..`; tsgo 7.1 itself isn't clean on it: TS 7's reduced JS support).
- **xstate** (core at f210b35 + vitest tests, lib es2024): the inference corpus (setup().types, spawn, assign).
- **drizzle** (drizzle-orm 15454dbe src + type-tests, noUncheckedIndexedAccess, noPropertyAccessFromIndexSignature,
  devDependencies at the lock's versions: npm's resolution of the ranges isn't clean): query-builder chains returning
  `this & {...}`, `Expect<Equal<>>`; the instantiation-volume corpus.
- **vue** (core b5f85183, every package's src and `__tests__` plus dts-test): reactivity types, `this` contexts.
- **effect** (v3 e730f59d, `packages/effect` src + test + dtslint tstyche in one program, exactOptionalPropertyTypes,
  NodeNext): dual APIs, `pipe` with generic calls as arguments, generators as do-notation, Schema classes.
- **vscode** (156e400b `src/tsconfig.corpus.json`, 1.39M lines; nodenext, experimentalDecorators,
  useDefineForClassFields off): the scale corpus.
- **excalidraw** (d080833f, React 19 .tsx; `lib-dom-5.9-shim.d.ts` for 5.9's `Uint8Array<ArrayBuffer>`): the JSX app.
- **tanstack-router** (837917a4 react-router src + `*.test-d.tsx`, against router-core's built declarations from
  `corpora-locks/tanstack-router.postinstall.sh`): inference over route trees, template-literal unions, augmentation.
- **typescript-eslint** (`-b` on a solution config).

## Probing a corpus
- `scripts/tsctypeat.sh TSCONFIG FILE LINE NAME` prints tsgo's type of an identifier inside the corpus, where a
  reduction doesn't reproduce.
- A probe project: a tsconfig in /tmp that `extends` the corpus's config with `"files": [probe.ts]`, importing from
  the corpus by absolute path (plus absolute `baseUrl`/`paths`/`typeRoots` if the corpus uses them); it checks in
  about a second. tttt doesn't type check a file whose imports don't resolve. `filetest.sh` copies the file away, so
  multi-file probes run `./release/tttt` and `thirdparty/typescript/built/tsc --noEmit` directly.
- Reducing: inline the library's types into one file, then delete and rename until it flips; a `log_info` where a
  type turns unknown finds the step fast.
- expect-type's `toEqualTypeOf` (valibot, zod test-d files) checks the identity relation: a jump there is real.
- A diagnostic hidden behind `any` or the error type surfaces when its cover is fixed: counts can go up after a real
  fix. Estimates by grouping codes were often off by 5x: rank by sampled root cause.

## Seen on the way
The corpora-to-zero work's "seen on the way, not corpus-visible" list was re-probed 2026-10-19: all match tsgo now but drizzle's
neon-http TS7023 shape, moved to bugs/type_core.md ("TS7023 through a handler's inferred return").
