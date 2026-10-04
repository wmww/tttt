# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-29 (triage session, todo entry 10 of the seventh window; suite 12318 -> 12354, 0 lost, corpora
unchanged). Four Opus sub-agents took every item: fixed ~25 (the harness now drops diagnostics its type walk provokes,
which hid a TS2318 generator check; `declare global` names resolve to the merged global first (Iterator.from); pattern
ambient module augmentations; tuple members with the tuple as `this`; computed ambient enum members; TS2589 at the
declaration; tsc's overload fallback; Pick's K inference; destructuring accessibility; DOS drive paths; file names in
messages; TS2395/TS2307/TS2748/TS1376 leftovers; `@augments` as the base; TS7031 widening...), routed the rest to
`bugs/*.md`, plan 38 (nested_generic) and plan 39 (TS7023 re-entrance). The drizzle "CLI-only" probe was not
reproducible. Stale entries (tests that pass now) were pruned from `bugs/*.md`.

- (triage 2026-09-29, group A) `test/probes/triage/pick_key_inference.ts`: tsc's TS2353 excess-property error on an
  object literal against `Pick<S, keyof S>` is missing (predates the Pick K inference fix).
- (plan 40 stage 1, 2026-10-04) `export function f3() { return; return 1; }`: tsc's types baseline says
  `f3 : () => number`, tttt `() => 1 | undefined` (the unreachable second return, or the bare return's undefined).
- (plan 40 stage 1) `isolatedDeclarationErrorsAugmentation` also fails its types component, unrelated to TS9026.
- (plan 40 stage 2, 2026-10-04) Cross-test state dependence: `importAssertionNonstring` alone prints `{ field: any[] }`
  (wrong; tsc `string[]`) and in the full suite `string[]`, in the build before stage 2 too; with `--checkers 4
  --threads 4` the full suite now gives `any[]` there (`./release/baseline_tests --summary --dump DIR` 1 vs 4 checkers:
  7 differing files, 6 before). Something an earlier test leaves cached (the lib program's types?) decides an array
  literal's element type in an import attribute.
- (2026-10-04) `types: ["node"]` with `lib: ["es2020"]` and no skipLibCheck: tttt reports TS2502 "'crypto' is
  referenced directly or indirectly in its own type annotation" at @types/node/crypto.d.ts:4466 (`var crypto: typeof
  globalThis extends { crypto: infer T; ... } ? T : ...`); tsc is clean. A file with just `process;` under
  `{"strict": true, "types": ["node"], "lib": ["es2020"], "module": "nodenext"}` (with node_modules pointing at
  thirdparty/tsc/node_modules) shows it. tsc-src doesn't see it because of skipLibCheck.
- (plan 41) CLI phase gating differs from tsc's: tsc -p reports semantic diagnostics only when there are no syntactic
  and no options/global ones (`emitFilesAndReportErrors`), tsc -b goes on to semantic ones anyway; tttt reports name
  errors after parse errors and skips typechecking after either. Scenarios `when-project-references-composite-project-
  with-noEmit` (a TS2307 tsc doesn't report) and `in-bad-ref-branch-...` (a TS6133 tsc does).
- (plan 41) Plan 40 gaps from the scenarios: no declaration diagnostics at all with `--noCheck` (tsc still reports
  TS4094: `tsbuild/noCheck/*/dts-errors`); TS4023 missing for `export const api = ky.extend({})` where ky's
  `KyInstance` is a non-exported type alias of a package (`tsbuild/declarationEmit/*/reports-dts-generation-errors`).
- (plan 41) Config diagnostics tttt lacks: TS1005 & co. for tsconfig JSON syntax errors; a missing `extends` (TS5083,
  tsc goes on with the rest); `--composite null` on the command line (tttt takes `null` for a file name: TS6231).
  TS6377 (two projects writing one tsbuildinfo) is out of scope.
- (plan 41) Checker gaps the scenarios show, not reference-specific: TS2613 (the "did you mean `import { local }`"
  variant of TS1192); no TS1192 for a default import of an ESM `.d.ts` package under bundler + esnext when both sides
  are ES modules (tsc's canHaveSyntheticDefault `usageMode === targetMode === ESNext`); TS1479 for a CommonJS
  `.tsx` file's implicit `solid-js/jsx-runtime` import of an ESM package (`tsc/composite/synthetic-jsx-import-...`).
- (fan-out C, 2026-10-04) **Evaluation order changes answers.** tsc's checkReturnStatement asks for the function's
  return type at the first `return` (so every return expression's literals are older than what the statements in
  between make: narrowingInCaseClauseAfterCaseClauseWithReturn, `"Not A, B, C or D" | "D"`). Doing the same in
  tttt (`ty_function_return_type(ctx.function)` at the top of STATEMENT_RETURN_EXPR in statement_typecheck_inner,
  unannotated functions only) passed the suite (+1) but produced corpus false positives: tsc
  `transformers/module/esnextAnd2015.ts:251` TS18048 (`importRequireStatements` possibly undefined: flow narrowing of a
  captured variable computed early gives another answer) and zod 2x TS2344 (`_TaskListNodeSchema`/`_Tree` against a
  constraint with `never` members, recursive-types.test.ts:474, mini/tests/index.test.ts:359). Both are order
  dependence bugs (plan 42 territory); find why before retrying the port.
- (fan-out 2026-10-04, for insights) The symbols baseline harness (`src/test/baseline_test_traversal.c`,
  `baseline_test_case.c`) grew ~650 lines this session rebuilding tsc's symbol identity that tttt's checker doesn't
  have: members aren't Symbols, so merges (computed names, type parameter + member, namespace locals + exports, union
  property declarations by (declaration, type, mapper)) are recomputed for display (hacks.md). It's display-only and
  passes 290 symbols components, but it is a parallel model of tsc's binder. Decide whether member Symbols (which plan
  42's lazily resolved members may want anyway) should replace it, or whether it's an acceptable harness cost.
