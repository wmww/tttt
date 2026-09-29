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

## For the insights session
- The hacks.md consolidation named hacks whose fixes are plan-sized rather than triage-sized (each has its entry in
  hacks.md): instantiation-limit poisoning (signatures/members/variances memoized past
  `ty_instantiation_limit_reached`), `check_assignable` letting `any` through to `never` (25 tests depend on it:
  flow typing yields `any` where tsc has `never`), conditional skipTrue (sharing of conditional instantiations; zod
  perf and `first_unfit_argument`), `ty_non_nullable` -> `ty_non_nullish_part` for ~24 callers with `NonNullable<T>`
  printing, `chain_object_type` (TS18048 on `a?.b.c` needs an `optional` marker type).
