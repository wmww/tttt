# Plan 19: the modern corpora to zero (loop, like plan 08)

`./realworld.py --corpus valibot` and `--corpus type-fest` (tsc 5.8 reports 0 on both; tsgo only 2 TS2554 in valibot
under its different moduleResolution). Work them down root cause by root cause, suite and the tsc corpus
(`./realworld.py`, must stay 0) as regression checks. Rank by root cause, not by code: sample the biggest groups,
reduce each to a probe in `test/probes/plan19/`, fix the tsc rule behind it. Each session: record both counts in the
commit message and `measurements.md`.

## Stage 0: module augmentation of an aliased export (do first, it's cheap and it's most of valibot's count)
vitest augments `@vitest/expect`'s `ExpectStatic` (`declare module '@vitest/expect' { interface ExpectStatic {...} }`,
`node_modules/vitest/dist/chunks/global.*.d.ts`), and `@vitest/expect` exports it through an alias (`interface
ExpectStatic ...; export { type ExpectStatic }`). tttt puts the augmentation's interface next to the alias in the
exports table (TS2484 in the reduced case) instead of merging it into the alias' target, so `expect` loses its call
signature: valibot's ~1800 TS2349 + ~600 TS2339 in `*.test.ts` are this. Known in `hacks.md` (Modules, stage 3:
crossFileOverloadModifierConsistency). Reduced case (tsc: no errors; tttt: TS2484 + `s(1)` not callable):
```
// node_modules/lib/index.d.ts
interface S { <T>(x: T): T; any: () => any; }
export { type S };
// aug.d.ts
import { S } from 'lib';
declare module 'lib' { interface S { extra: number } }
// main.ts
import { S } from 'lib';
declare const s: S;
s(1); s.any(); s.extra;
```
tsc merges augmentation declarations into the *resolved* target symbol of the export (mergeModuleAugmentation ->
resolveSymbol). In tttt augmentations declare in the declare phase, before aliases are linked, so the merge has to
happen in or after `binder_link` (and must stay a per-file contribution for the parallelism contract, `hacks.md`).

## Known root causes after plan 17 (re-rank after plan 18)
- valibot `src/` (library code, not tests), plan 17 sample: most of it was `@ts-expect-error` lines (fixed in plan 17);
  what remained: an object literal returned from an overload implementation (`bytes.ts:104`: `kind: string` instead
  of `'validation'` against `BytesAction<...>` with `reference: typeof bytes`), `DeepPickN` (valibot's own mapped/
  conditional path picking, `partialCheck` tests, TS1360 on `satisfies`).
- type-fest `test-d/`: ~500 evaluation errors of utility types (`expectType<ArraySlice<[0,1,2,3],1>>([1,2,3])`: the
  utility evaluates to the wrong type). Most go through template literal arithmetic (plan 18). tsd's `expectType` /
  `expectError` are plain functions; `expectError` checks nothing at type level.
- tttt only works when run from the repo root (lib path relative to cwd): triage.md.
