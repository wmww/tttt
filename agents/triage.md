# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-29 (triage session, todo entry 10 of the sixth window; suite 11962 -> 12040, 0 lost). Fixed: the two
log_error tests (the suite's checker log_error output is empty), TS2724/TS2614 (tsc's errorNoModuleMemberSymbol),
TS2511 for `new` of a union with an abstract member, class instances in subtype reduction (isTypeDerivedFrom),
`o[77] = v` expandos, two stale hacks (TS2358 in JS, TS7053 on callables), `error_ctx_scratch()`, and through
three Opus sub-agents: escaped identifier names (`name_cooked()`, 20+ tests), `export =`/`export default` of entity
names as aliases, augmentations of `export =` modules and of `export default` aliases, the Node16 ESM namespace import's
synthetic default, and nine small checker bugs (narrowingMutualSubtypes, mappedTypeGenericWithKnownKeys, JS TS2565,
returnTagTypeGuard, rest-tuple union signatures, noUncheckedIndexedAccess, classExtendsNull2, varianceReferences,
generatorImplicitAny). Their perf cost was bisected and gated (tsc corpus +0.6% instructions for the session). hacks.md 985 -> 523 lines. Gone on their own: xstate's checker-count dependence
(checkerdiff: identical at K=1 and 4), derivedClassSuperProperties, `<T extends any>` display, blockScopedBindingUsedBeforeDef,
asyncFunctionNoReturnType, jsdocCallbackAndType, unionTypeWithIndexAndTuple. Routed: circularity to plan 39 (a
planning stub), the rest to `bugs/*.md` (missing_checks.md has a new "Elaboration" section) and plan 26 "Leftovers".

- (instantiation limit session) TS2589 location for a call: `const r1 = a.get()` whose return type runs out of tail
  recursions is reported by tsc at the declaration name `r1` (4,7), tttt at `a.get()` (probe
  `test/probes/instantiation_limit/call_location.ts`); a property access (`a.x`) is at the access in both.
- (vue dts-test session) Overload failure fallback: tsc's pickLongestCandidateSignature takes the first candidate with
  enough parameters (getLongestCandidateIndex over all overloads), `resolve_with_signatures` the last unfit one; the
  failed call's type differs (probe `plan19/vue/overload_first_check_skips_generic_functions.ts`, types only).
- SkipGenericFunctions for generic functions nested in literal arguments (`nested_generic`) isn't done: leaving them out
  of the first round lost zod `pick([z.object, ...])` and xstate inferences, because tttt marks literals nested_generic
  where tsc has no contextual signature to skip for (see `infer_type_arguments`).
- Relating nested indexed-access sources (`T[a][b]...[k]`, react-hook-form Path/FieldPathValue) steps one index
  constraint per relation and recomputes each variant ~12 times (Maybe results inside aren't cached): 4% of the react
  corpus. tsc computes the constraint of the whole chain in one getConstraintOfType; see hacks.md (Relations).
- Symbols: a property of `Readonly<LooseRequired<T>> & {...}` (vue's DefineProps) lists its declaration once where tsc
  lists it twice (the intersection modifiers type `T & Required<T>` of LooseRequired); `this` of a ThisType marker
  prints no Symbol line (probes in `plan19/vue/`).
- (drizzle type-tests session) The union-of-tuples rest argument probe `plan19/drizzle/rest_tuple_union_context.ts`
  failed only through the CLI (`tttt file.ts`, `./run.sh`), never in the test harness, before its fix: the harness
  types a call argument with its contextual type some other way. Find out why the harness and the CLI differ there
  (the harness may hide more CLI-only bugs).
- Each checker (`--checkers 4`) measures variances and builds relation caches on its own, and a TS2589 count is charged
  to whichever node first needs them; tsc pays once. Harmless since drizzle's runaways are fixed, but a TS2589 near
  the 5M limit could depend on the checker count.

- (perf session 2026-09-29) New per-checker caches assume the computation is a pure function of the type ids:
  `ty_union` of two types (KEY_UNION_PAIR), `ty_intersection` without an alias (KEY_INTERSECTION_OF), flow's
  `assignment_reduced_type` (KEY_ASSIGNMENT_REDUCED). tsc caches the same things, and the suite and corpora didn't
  move, but a result computed while something it reads is still in flight (constraints in
  `remove_constrained_type_variables`, members for empty-object checks) is pinned. If an order-dependent union or
  narrowing bug shows up, disable these first.
- (skipTrue session) An array literal `[]` whose contextual type is a deferred conditional type of tuple branches
  (`const x: ConditionalRequired<[options?: O], IsNotNever<K<T>>> = []`) is `never[]`, not `[]`, so TS2322; tsc takes
  the tuple-likeness from the conditional's constraint (both branches) and reports nothing.
- `??`, `||` and optional chains call `ty_non_nullish_part()` directly, which filters null/undefined even without
  strictNullChecks (tsc's getNonNullableType is the identity there). Test-neutral today; switch them to
  `ty_non_nullable()` if a non-strict test shows it (2026-09-29 NonNullable port leftover).
- Members of an ambient non-const enum without initializer (`declare namespace n { enum E { A, B } }`) get 0, 1...
  in `ty_resolve_enum()` (its TODO); tsc makes them computed (unknown values), which is what TS4126 "One value of
  'E.A' is the string ..., and the other is assumed to be an unknown numeric value" reports (enumAssignmentCompat6,
  which also needs `import("f")` qualification in the header). Changes what number literals such members accept.
- vue computes an assignability explanation it never shows: `(TSMethodSignature | TSPropertySignature) & { _ownerScope:
  TypeScope }` to `TSType` goes through `ty_explain_unrelated()` (an argument check via `report_unfit_argument()`,
  compiler-sfc resolveType.ts) although vue has no diagnostics. Since discriminated_union_target() sees intersection
  sources' properties (tsc's findMatchingDiscriminantType), that explanation costs +0.2% instructions on vue. Find which
  caller explains a failure whose diagnostic is dropped (speculative checking?) and skip the explanation there.
