# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-28 (triage session, todo entry 10 of the fourth window; suite 10516 -> 10543). Fixed: webpack's
checker-count dependence (overload groups keyed by block only; webpack 30 at every K), the baseline walk typing each
file with its own checker, TS2769 candidates printed instantiated, type-predicate narrowing keeping the candidate
(map_type), ~20 `TYF_OBJECT` gates that skipped intersections, explicit type arguments failing a constraint
(candidateForTypeArgumentError, TS2344), TS2375 against union targets, regular object literals (evolving arrays of
objects, the `literal_lacks_prop_of` HACK gone), TS7010/7011/7018 from reportErrorsFromWidening, tsc's line breaks
(`\r`, U+2028/9) and whitespace (NEL, U+200B). Routed: eager contextual instantiation (plan 37 F's return_only probe)
to `bugs/type_core.md`, the zod-without-variance TS2589, intersection property lists, spread identity, and the
smaller leftovers to `bugs/*.md` and hacks.md.

## 2026-09-28, sixth window's corpus session (todo entry 1)
- Distributive conditional base constraint: with `S2<T> = T extends string ? T : number`,
  `g2<T extends unknown[]>(x: [...{ [K in keyof T]: S2<T[K]> }])` widens `g2(['a'])` to `[string]`, tsc `["a"]` (its
  forConstraint constraint of `S2<T[number]>` keeps the true branch with T's implied `string`).
- A corpus's output can depend on the checker count (4 by default): a file can look fine alone and fail in the corpus.
  Reproduce with `--checkers 1` and the suspect file first in `files`.
- xstate's output still depends on the checker count: 5 diagnostics only at `--checkers 1` (actor.test.ts:1060/1499/
  1566, assign.test.ts:20, invoke.test.ts:1257/1276: callbacks typed `AssignArgs<MachineContext...>`), no limit hit:
  some cached inference state (plan 38 territory?). checkerdiff.py should flag it; it's a determinism bug.
- `StateId<U>` to `StateId<T>` (xstate's StateId, no variance measurement) gives TS2589 next to the TS2322 tsc also reports:
  the definitely-assignable checks instantiate deeply nested indexed-access chains. Repro:
  test/probes/xstate/state_id_deep_nesting.ts.
- `interface I<C> { a: StateNode<C, any> }` where StateNode requires `C extends MachineContext`: tsc's TS2344 missing.
- A binding default `{} as any` misses tsc's empty `^^^` types-mask line (bindingDefaultAnyRemovesUndefined.ts types).
- tsc prints a written `<T extends any>` as `extends unknown`.
- ~30 callers of ty_non_nullable still use its stripped-constraint behaviour for type parameters (hacks.md
  "ty_non_nullable"); `x!` and optional-chain roots use ty_non_nullish_part (tsc's getNonNullableType) now.
- (sub-agent C) Elaboration into a `NoInfer` member: against `NoInfer<{ type: 'A' }> | Expr` tsc elaborates to `'"B"'
  is not assignable to '"A"'`, tttt reports the whole argument (best_matching_type doesn't pick the substitution).
- Union-source elaboration: for `Snap<...>` (a union alias) not assignable to `number`, tsc adds a line for the
  union's first member; tttt omits it.
- Missing TS2353 for `const x: Trans<E> = { NEXT: ... }` in a generic function with `Trans<E> = { [K in E['type'] |
  '*']?: ... }`.
- TS2305 where tsc gives TS2724 ("Did you mean 'StateSchema'?") for a missing named import.
- tttt adds TS6500 related info on `NoInfer<{...}>` targets; tsc doesn't.
- xstate meta.test.ts 270/434: elaboration stops one level too high (at `e2:`, tsc goes on to the `meta:` under the
  `@ts-expect-error`). Not reduced.

## 2026-09-28, plan 38 stage 2 (todo entry 4)
- JSDoc nodes and outer type parameters: tsc instantiates a JSDoc object/function type node for *every* outer type
  parameter (isTypeParameterPossiblyReferenced never meets the `@template` tag), so `@template T @callback B` used as
  `B<string>` prints `B<string>` (jsdocCallbackAndType, types only). tttt's lowering collects the node's real free
  parameters, and `ty_anonymous_from_node()` can't enumerate the ones in scope; making JSDoc nodes free_params_unknown
  gained that test but let lib's `then<TResult1>` count as mentioned by a JSDoc-typed T (webpack +2). The fixing side
  is handled narrowly in `assignment_mentions()` (`meets_jsdoc_node_in()`). A real fix records the in-scope
  `@template`s on the lowering context.
- Sub-agents given a worktree share `thirdparty/corpora` through the symlink: a probe written into the corpus
  (xstate's `test/zz_probe.ts`) shows up in every tree's corpus run. Tell sub-agents to keep probes out of it.

## 2026-09-28, suite checker bugs, bucket B (indexed/property access)
- Unicode escapes in identifiers (`x`, `\u{78}`) aren't decoded: names are source spans, so `x` and `x` are
  different names (spurious TS2339/TS2304 in unicodeEscapesInNames01/02, escapedIdentifiers, unicodeEscapesInJsxtags).
  Needs a decoded-name path in the lexer/binder (an interned name next to the span).
- Node16+ ESM `import * as ns` of a CommonJS module: tsc gives ns a synthetic `default` (the module) through
  cloneTypeAsModuleType, displayed `typeof ns` (nodeNextCjsNamespaceImportDefault1/2). Making it an interned object
  literal broke the display of 16 node tests (`{ default: typeof cjs; }` vs `typeof cjs`); needs a module-clone type
  that displays as its alias.
- JS `/** @type {Cb} */ function f(x)` / `const f = ...` with a Cb whose `@return {x is T}` (or `@type {(x) => x is T}`)
  doesn't narrow: the signature control flow resolves has no predicate (returnTagTypeGuard line 58).
- `error_ctx_init()` gives phase 0: a temporary ErrorCtx merged into a checker's must copy `phase` or
  @ts-expect-error/@ts-ignore miss its errors (bit this session once). A `error_ctx_scratch(ErrorCtx const* like)`
  helper would make that impossible to forget.
- dependentDestructuredVariables: `function foo({ value1, test1 = value1.test1, ... })` gives TS7022 on every testN;
  tsc reports the circularity once, at value1 (7022 + 7031).
- moduleAugmentationOfAlias: `declare module './a' { export default interface I {...} }` merging into `export default
  I` (an alias) should make the default export alias+interface (tsc's mergeSymbol), spurious TS2339.
- narrowingMutualSubtypes: `Array.isArray(obj)` with `obj: Record<string, any> | Record<string, any>[]` keeps `any[]`
  in the true branch (tsc: only `Record<string, any>[]`), spurious TS7053.
- mappedTypeGenericWithKnownKeys: `obj.unknownLiteralKey = 3` on `Record<keyof T | "k", V>` should be TS2551 (did you
  mean 'k'), `obj['' as string] = 4` TS2862; ty_check_generic_index_write() skips generic mapped types.
- TS2712 (a dynamic `import()` in ES5 without a global `Promise` value, with TS2468): tsc reports it unconditionally in checkImportCallExpression (createPromiseReturnType); the async-function half (TS2705) is in `infer_return_type()` (ty/function.c).
## 2026-09-28, suite checker bugs, sub-agent C (circularity / noImplicitAny / definite assignment)
- tsc's circularity is one resolution stack (pushTypeResolution): a cycle found at entry i marks every entry from i on
  circular, so the *outer* declarations get TS7022/TS2502 too. tttt has separate in-flight states (LazyType
  self-referential scheme, function return memos, Prop.circular, aliases_in_flight) that only flag the re-entered one.
  Missing because of it: for-of34/35 (TS7022 on `v` when `next()`'s return type is the cycle), circularTypeofWithVarOrFunc
  (TS2502 on `var x: Alias` whose alias is `typeof x`, TS2577), circularGetAccessor (`typeof this.foo` in a declared
  class goes through the accessor LazyType, not the Prop), implicitAnyFromCircularInference, recursiveTypesWithTypeof.
  Worth a plan: one checker-level resolution stack that the lazy states push onto.
- TS7023 that tsc gets from printing a type in an error message while the return type is in flight
  (thisInObjectLiterals, accessorInferredReturnTypeErrorInReturnStatement, trivialSubtypeReductionNoStructuralCheck,
  checkingObjectWithThisInNamePositionNoCrash, conflictingDeclarationsImportFromNamespace1/2): tttt's error types don't
  resolve the in-flight return type. Probably only fixable by printing the way tsc does (resolving members' types).
- TS2313 left: `[P in P]` (P isn't in scope of its own constraint: TS2304 instead), recursiveMappedTypes /
  circularBaseTypes (`keyof` of an alias being resolved), incorrectRecursiveMappedTypeConstraint; the base constraint
  walk (`walk_base_constraint`) doesn't go into conditional types.
- TS2565 for JavaScript expandos read in the container that declares them (`d.q` after `if (b) d.q = false`,
  typeFromPropertyAssignment36, classFieldSuperAccessibleJs1): getFlowTypeOfAccessExpression's second assumeUninitialized
  branch.
- dependentDestructuredVariables: narrowing of destructured variables by each other (tsc's getCandidateDiscriminantPropertyAccess
  on binding elements) isn't implemented: 20 spurious TS2339 and 9 spurious TS7022.
- blockScopedBindingUsedBeforeDef: tsc also reports TS2538 "Type 'any' cannot be used as an index type" for the
  circular `any` key of `let {[a]: a}`.
- generatorImplicitAny `f(yield)` with a generic `f<T>(value: T)`: tsc reports TS7057 (the argument's contextual type
  ends up any); tttt's memo context is T.
- circularReferenceInReturnType: `fn2()(() => res2)` is typed wrong (tsc: res2 any without error, tttt: TS7024 on the
  arrow); context-sensitive argument order.
- restTuplesFromContextualTypes line 56: `(a, b, ...x) => {}` against `(x: number, ...args: T)` should fail (T vs
  `[b: T[0], ...x: T[number][]]`); tttt relates it.
- `export = C.B` / `export default C.B` of a dotted entity name is a value (SYMBOL_DECL_EXPR) in tttt; tsc makes it an
  alias (exportAssignmentIsAlias: isEntityNameExpression) resolved with every meaning, and since a class's statics are
  in its exports the static `B` and a merged namespace's `interface B` are one symbol. Spurious TS2749 in
  exportEqualsProperty(2), exportDefaultProperty2, commonJsImportClassExpression. Needs a dotted SymbolAlias whose
  value half is the expression (value_target) and type half the namespace walk.
- TS2705/TS2468 (async function in ES5 without a global `Promise` value, tsc's createPromiseReturnType): tsc reports it only when the return type of an async function *without* `return x` is computed from its body (noImplicitReturns, a call, a contextual signature...), not for every such function; checking every one of them breaks 9 tests (asyncFunctionWithForStatementNoInitializer, classStaticBlock7...). Needs the error at the lazy return-type computation (asyncFunctionNoReturnType, operationsAvailableOnPromisedType, destructuringControlFlowNoCrash). TS2712 (the same for `import()`) is unconditional in checkImportCallExpression.
- Relations bucket leftovers (sub-agent A, suite-checker-bugs session):
  - compareSignaturesRelated's restIndex (getRestOrAnyTypeAtPosition when either side has a non-array rest) isn't
    ported: `(a, b) => void` against `(...args: readonly [1, "1"] | readonly [2, "2"]) => any` compares per position
    (contextualTupleTypeParameterReadonly). A straight port (`rest_or_any_type_at` in compare_signatures) broke 5 tests
    (genericFunctionInference1, restTuplesFromContextualTypes, ...): generic rest `T` against a params tuple needs care.
  - noUncheckedIndexedAccess on a generic index (`myRecord2[key]`, Key extends keyof with an index signature): tsc's
    IndexedAccess type carries AccessFlags.IncludeUndefined (persistent), whose constraint adds undefined; TY_INDEXED has
    no such flag (noUncheckedIndexedAccess line 99).
  - varianceReferences: an alias with `in out` whose body isn't an object type (TS2637) still uses the annotation, but
    tsc's reportErrors=true first pass falls back to a structural check for an invariant failure (relateVariances);
    tttt's first pass never reports, so `vs12 = vs1` errors.
  - inferConditionalConstraintMappedMember: `{ [K in keyof T]: X } extends { [_ in keyof T]: infer U } ? U : never` has
    constraint X in tsc (K4 variant with template `1` gives number), tttt unknown; why tsc infers there despite the
    generic mapped check type is unclear. Kept out of TS2344 by the deferred-conditional HACK.
  - unionTypeWithIndexAndTuple: `f(["a"])` for `["a"] | I` (I with a number index of any) keeps "a" in tsc; the element
    context is `"a" | any` = any there too, so the mechanism is elsewhere.
  - usingDeclarationsWithIteratorObject (and await variant): `Iterator.from(i)` in lib.esnext.iterator's
    `declare global` resolves `Iterator<T, unknown, undefined>` to the module-local class (with map, filter...) in tttt;
    tsc accepts a global Iterator there. Check how names inside `declare global` of a lib module resolve.
  - mappedTypes6 / mappedTypeErrors2: `-?` template elaboration (tsc's NonNullable<T[P]> display and chain).
  - classExtendsNull2 (TS2417 against static side 'null' with a merged interface), thisConditionalOnMethodReturnOfGenericInstance
    (tsc's circular `method` makes it any), signatureCombiningRestParameters1 (union signature with rest tuples should
    intersect to never).
  - derivedClassSuperProperties fails ERR on ai-main already (TS2376 at `(() => this)()` before super()) though the
    saved baseline had it passing.
- (JS session 2026-09-29) `./fasttest.sh -f` with a long regex alternation crashes with "stack smashing detected" (C's
  report; `-f @file` works): a fixed-size buffer in the harness's regex handling? `./realworld.py` always writes
  `/tmp/realworld.txt`, so concurrent runs (sub-agents in worktrees) overwrite each other's: make it per-corpus or
  per-cwd.
