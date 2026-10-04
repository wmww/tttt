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
