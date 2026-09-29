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
