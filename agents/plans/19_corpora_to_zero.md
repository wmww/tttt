# Plan 19: the corpora to zero (loop, like plan 08)

**Status 2026-09-28 (after the perf session)**: tsc 0, valibot 0, react 0, nest 0, **webpack 118** (checkJs; causes
in plan 26 "Stage 3 leftovers"), **type-fest 4** (all abstract-class: plan 32's mixin classes), **zod 13** (below).
The history of R1-R21 (identity relation, homomorphic mapped modifiers, const type parameters, skip-sensitive
inference, the permissive instantiation, ...) is in git (this file before the 2026-09-27 triage) and in the commits.

## The loop
`./realworld.py` (tsc's `src/compiler`, must stay 0) and `./realworld.py --corpus X` for valibot, type-fest, react,
nest, webpack, zod (`thirdparty/ensure-corpora.sh` pins them; all are clean under tsc 5.8). Every diagnostic is a
tttt bug. Rank by root cause, not by code: sample the biggest groups, reduce each to a probe in
`test/probes/plan19/<corpus>/`, fix the tsc rule behind it. Record the counts in the commit message and
`measurements.md`; the suite and the other corpora are the regression checks.

Corpus notes worth keeping:
- expect-type's `toEqualTypeOf` (valibot, zod test-d files) checks tttt's identity relation: a jump there is real.
- nest: nestjs-i18n's shipped `.d.ts` imports `Path`/`PathValue`/`IfAnyOrNever` from a module that doesn't export
  them (hidden by skipLibCheck), so those are error types; a regression there would look like an i18n bug. A clean
  corpus finds only spurious diagnostics: decorator checks (TS1238/TS1241/TS1270) are a suite item.
- webpack: tsc is clean only with ensure-corpora.sh's pins (watchpack 2.5.1, `node_modules/webpack -> ..`); ~70 s
  wall for tttt at the time of pinning, worth a perf look. `/// <reference lib>` works in program files, the harness
  still ignores it (TODO in `program_handle_statement`).
- zod: a pnpm workspace, root devDependencies from `corpora-locks/zod.package.json`; `tsconfig.test.json` (not
  tsconfig.json, whose noUnusedLocals only biome satisfies upstream). The heaviest corpus (9.1 G instructions, 1.3 GB:
  deep-partial.test.ts alone is 745 MB, `bugs/perf_leads.md`).

## What is left
### zod (13)
- **exactOptionalPropertyTypes' missing type** (recursive-types.test.ts 17, 35; probe `zod/exact_optional_mapped.ts`):
  a homomorphic `{ [K in keyof T]: T[K] }` over an optional property reads `X | undefined` in tttt, `X | missing` in
  tsc, and the missing is dropped against an index signature (getNonMissingTypeOfSymbol). tttt has no missing type
  (type-fest's jsonify:57 was approximated with `?: never` padding). The fix is tsc's missingType: an undefined of
  its own identity that optional reads add under exactOptionalPropertyTypes and removeMissingType strips. Medium;
  touches access(), union construction and display. Also covers: `'a' in x` narrowing `x.a` to non-undefined when
  its type contains missing (tsc narrowTypeByBinaryExpression's containsMissingType case: `x: {a: number} | {a?:
  never}` gives `number` in the true branch, tttt `number | undefined`).
- **ProtoOf inference** (classic/schemas.ts 2850-2858, 4; probe `zod/proto_of_infer.ts`): `$constructor<T>(name,
  (inst, def) => ..., proto?: ProtoOf<T>)` against a `$constructor<ZodInstanceOf>` annotation. A non-arrow function
  without an explicit `this` parameter is context sensitive (tsc's hasContextSensitiveParameters: implicit `this`),
  so the proto literal is non-inferrable in the first pass, and tsc's createReverseMappedType requires
  isPartiallyInferableType: no candidate, T comes from the return type. tttt builds the reverse mapped type anyway.
  Adding the check to `reverse_mapped_type_uncached` fixes the probe but loses reverseMappedPartiallyInferableTypes
  and contextualComputedNonBindablePropertyType: with no candidate, tsc's second pass keeps the argument's contextual
  type uninstantiated and instantiates contextual *signatures* with the fixing mapper as parameters are assigned;
  tttt instantiates the whole contextual type with T -> unknown. Fix both together: `context_mapper` in
  `infer_type_arguments` (`ty/call.c`) and the parameter assignment in `assign_contextual_signature`.
- detached-methods.test.ts (3): vitest's `MockReturnType` of a method picked by key from an overloaded interface.
- v3 generics.test.ts:20: inside a generic function, `keyof` of a generic intersection of mapped types
  (`addQuestionMarks<baseObjectOutputType<{ nested: TData }>, any>`) is expanded into a union of conditionals where
  tsc keeps it deferred (getIndexType / shouldDeferIndexType), and `data.nested` is `any`.
- in-out.test.ts:59, v3 types.ts 2778/2795: TS7053 on `{ [k in keyof T]?: true } & Record<Exclude<keyof Mask,
  keyof T>, never>` indexed by string. Unsampled.

### type-fest (4)
All abstract-class: plan 32 stage C (mixin classes).

### webpack (118)
JSDoc; plan 26 "Stage 3 leftovers".

### Seen on the way, not corpus-visible now
- `g(() => 1)` with `g<T>(t: T): T` infers `() => number` (tsc `() => 1`: a return literal isn't widened when the
  contextual return type is a type parameter).
- Display: `X<string | number>` for a distributive alias over a union keeps the alias (tsc only aliases a distributed
  union for a new alias symbol, probe `distributed_union_alias.ts`); `P<U>` where tsc prints the indexed access it
  resolves to (`indexed_prop_is_indexed.ts`); a recursive `RD<J>` prints 15 levels deep where tsc stops at 10 with
  `any` (`typefest/rd_2589_min.ts`).
- Padded properties (`a?: never` from widening) get symbol declarations only from the sibling that has the property;
  tsc lists both (`typefest/exact_missing_padding.ts`). Goes away with the missing type.
- Suite: spurious TS7053 where tttt lacks the rule, not the check: index signatures keyed by tagged strings
  (indexSignatures1), `in` narrowing (inKeywordTypeguard, narrowingMutualSubtypes).

## Lessons and tooling
- Reducing a corpus type bug: inline the library's types into one file (`/tmp/x/a.ts`), then delete and rename until
  it flips. A `log_info` of check/extends/result in `ty_conditional`, or of the operands where something turns
  unknown, finds the step fast.
- Multi-file probes against a corpus: put them in `/tmp/x/` with relative imports into `thirdparty/corpora/...`, run
  `./release/tttt -q <options>` and `node thirdparty/tsc/lib/tsc.js --noEmit <options>` (filetest.sh copies the file
  away, so relative imports break).
- `/tmp/realworld.txt` is shared: parallel agents overwrite each other's; copy it right after a run.
- A diagnostic hidden behind `any`/the error type surfaces when its cover is fixed: counts can go up after a real fix
  (expect-type mismatches were all silent until call arity was rechecked; zod's DeepPartial behind the getter fix).
- Estimates by grouping codes were often off by 5x (R2: ~190 estimated, 890 fixed): rank by sampled root cause.
