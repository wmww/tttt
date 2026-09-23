# Plan 19: the modern corpora to zero (loop, like plan 08)

`./realworld.py --corpus valibot` and `--corpus type-fest` (tsc 5.8 reports 0 on both; tsgo only 2 TS2554 in valibot
under its different moduleResolution). Work them down root cause by root cause, suite and the tsc corpus
(`./realworld.py`, must stay 0) as regression checks. Rank by root cause, not by code: sample the biggest groups,
reduce each to a probe in `test/probes/plan19/`, fix the tsc rule behind it. Each session: record both counts in the
commit message and `measurements.md`.

## Stage 0: module augmentation of an aliased export: done
`merge_augmentations` (`scope.c`); valibot 4473 -> 2169 (probe `test/probes/plan19/augment_alias.ts`).

## Known root causes after stage 0 (re-rank after plan 18)
- valibot after stage 0 (2169): TS2344 1166, most of them on expect-type's `toEqualTypeOf<X>()` (the argument checked
  against expect-type's mismatch type: `{ message: string; lang: ...; abortEarly: ... }` 227, `{ message: undefined;
  "~types": ... }` 138, the `"Expected: ${PrintType<...>}"` template 46): some relation/evaluation of expect-type's
  `StrictEqualUsingTSInternalIdenticalToOperator`/`DeepBrand` path, sample one before assuming plan 18 fixes it.
  TS2345 489: `Omit<SetIssue, ...>` not assignable to `Omit<SetIssue | StringIssue, ...>` (a homomorphic-ish mapped
  type over a union: distributes? relates?), object literals with `kind: string` (the `bytes.ts:104` return context
  below), TS2578 185, TS2322 141, TS1360 86 (`satisfies`).
- valibot `src/` (library code, not tests), plan 17 sample: most of it was `@ts-expect-error` lines (fixed in plan 17);
  what remained: an object literal returned from an overload implementation (`bytes.ts:104`: `kind: string` instead
  of `'validation'` against `BytesAction<...>` with `reference: typeof bytes`), `DeepPickN` (valibot's own mapped/
  conditional path picking, `partialCheck` tests, TS1360 on `satisfies`).
- type-fest `test-d/`: ~500 evaluation errors of utility types (`expectType<ArraySlice<[0,1,2,3],1>>([1,2,3])`: the
  utility evaluates to the wrong type). Most go through template literal arithmetic (plan 18). tsd's `expectType` /
  `expectError` are plain functions; `expectError` checks nothing at type level.
- tttt only works when run from the repo root (lib path relative to cwd): triage.md.
