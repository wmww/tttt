# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-07 (triage session, todo entry 10 of the tenth window; suite 13299 -> 13303, 0 lost, every
corpus unchanged, sweep 8588 -> 8589, flagflip 51 / 47 / 11 -> 51 / 45 / 10, `$ExpectType` 68619 flat). Four Opus
sub-agents (type core + corpus cells, missing checks, display/text, JS/modules) took every item: 44 fixes, ~12 items
gone by earlier work, the rest written up into bugs/*.md, plan 26 Leftovers and hacks.md. Fresh mutation runs: seed 4
(`/tmp/tc-mutate4`, 26 -> 21) and seed 5 (`/tmp/tb-mutate`, 29 -> 25), leftovers in missing_checks.md.

- valibot under the commit's tsc with `--ignoreDeprecations 6.0` (without it tsc skips semantic diagnostics after the
  TS5107): 26 TS2554 "Expected 1 arguments, but got 0" on `expectTypeOf<...>().toEqualTypeOf<...>()` in
  `src/methods/partial/partial.test-d.ts` and others, i.e. tsc 6 finds types unequal that tttt finds equal (before and
  after stable ordering). Missing-diagnostic lead for plan 48 stage 5 (valibot re-pin), 2026-10-07.
- typeofImportDefer (plan 48 stage 3, agent B): `typeof import.defer("./a").Foo` in a type: tsc's parseImportType recovery says '(' expected at the `.`, TS1141 over `.defer`, ')' expected at `(`, then TS2339 'Foo' on '"./a"'; tttt says TS2304 'import'. One test, quirky recovery.
- Namespace import of an ES module with a `default` (plan 48 stage 3, agent C): under esModuleInterop (6.0's
  default) tsc's resolveESModuleSymbol clones it too (cloneTypeAsModuleType), so `ns` no longer names the module:
  `import("./a")`'s type is `typeof import("./a")`, not `typeof ns` (importAttributes9, verbatimModuleSyntaxRestrictionsCJS,
  probably transformNestedGeneratorsWithTry's Bluebird). Tried making namespace_import_type() (lower.c) return a
  module_clone copy for that case too and dropping symbol_chain.c's container_lookup restriction: +2 but 14 lost
  (symbols naming `self.default` through the object type in baseline_test_traversal.c, `export { ns }` re-exports,
  esModuleInteropDefaultImports...). Wants a real clone symbol (hacks.md, namespace_import_is_clone), plan-sized.
- legacyNodeModulesExportsSpecifierGenerationConditions (agent C): in the synthetic-default object of `await
  import("inner")`, tsc doesn't reuse the `() => ` of `export const x: () => Thing` (Thing inaccessible from index.ts)
  where ty_display.c's "module's variable" annotation path reuses it partially; tsc's tryReuseExistingTypeNode seems to
  give up whole there. Find the rule before touching display_annotated().
- `fx<>` (instantiationExpressions, agent C): the parser drops an empty type argument list (parse_expr_rest, `import<>`
  excepted), so the types baseline misses tsc's `>fx<> :` line. Keeping the node would need the checker to ignore it
  like tsc does (TS1099 only).
- T_1 renames (agent C; pre-5.8 failures): computedTypesKeyofNoIndexSignatureType wants `K_1`..`K_9` because tsc's
  instantiateMappedType clones the key type parameter (ty_mapped_info_instantiate() doesn't);
  inferredReturnTypeIncorrectReuse1 (`fn_1`) and jsFileFunctionOverloads (`T_1`) rename where tttt sees no shadowing,
  coAndContraVariantInferences3 `P_1`. One mechanism worth a look: what tsc's typeParameterShadowsOtherTypeParameterInScope
  finds for an instantiated signature's clone.
- Plan 48 stage 3 agent D leftovers (types component), 2026-10-07:
  - thislessFunctionsNotContextSensitive1: a *failed* generic call's non-context-sensitive function argument
    (`F({ b: function () { return 123; } }, wrongArg)`) gets `() => number`; tsc keeps `() => 123` (its own signature is
    the contextual return type, as for a successful call: set_contextual_return / ty_late_contextual_return only see
    resolved calls). Also TS2820's target prints the alias `ExtractFields<...> | undefined`.
  - instantiateContextualTypes: `Promise.resolve().then(() => cond ? 'A' : 'B')` under a contextual `Promise<DooDad>`
    infers TResult as the alias DooDad; tsc infers the alias-less union `"ELSE" | "SOMETHING"` (failed under 5.8 too).
  - nestedGenericSpreadInference: tsc 6 removed SingleSignatureType's outerTypeParameters (getOrCreateTypeFromSignature)
    and isTupleOfSelf; `wrap(<T>(x: T) => x)` is `{ x: (x: unknown) => unknown }` in tsc 6, tttt keeps it generic.
  - destructuringParameterDeclaration-like strictness corners are done; jsxNamespacePrefixInName(React): `<a attr:=...>`
    recovery types `a` undefined in tsc, any in tttt (parser recovery).
  - Merged generic interfaces (9e63c0cb) instantiate every anonymous type of their members with the owner's type
    parameters like tsc (isTypeParameterPossiblyReferenced is true for multi-declaration type parameters): +3.8% zod
    instructions (ZodType<...> is merged, 53 member types now instantiated per schema type), +1% tsc. An identity-only
    cost (the instances are structurally equal); a relation fast path for two instances of one root whose syntactic
    free parameters map alike would win some of it back.
- Plan 48 stage 3 agent B leftovers (errors component, all pre-6.0 or display/elaboration, not new 6.0 checks):
  bigintPropertyName (bigint literal property names: 6.0 parses them, binds them as no member (`{1n: 1}` is `{}`), TS1539; parser + binder work);
  inlineJsxFactoryDeclarationsLocalTypes ('Its return type' must qualify `import("renderer2").predom.JSX.Element` like tsc's getTypeNamesForErrorDisplay; same 5.8 baseline, passed before the switch);
  complexRecursiveCollections (TS2430 elaborates `map(...).filter(...).filter` where tsc's first failing property is `map(...).size`: property comparison order);
  requireOfJsonFileWithoutResolveJsonModule (6.0 resolves the .json: tsc's JSON parser recovery on non-JSON text gives TS1005/TS1136 and an object type `{ contents: any; Not: any; read: any; }`, tttt gives TS1328);
  arityAndOrderCompatibility01, styledComponentsInstantiaionLimitNotReached, bigintWithLib, conditionalTypes1/2 (elaboration chain depth/order);
  parser recovery (plan 27): labeledStatementDeclarationListInLoopNoCrash4, jsxAndTypeAssertion, tsxAttributeInvalidNames (one missing EOF TS1005 each), parser0_004152, jsxInvalidEsprimaTestSuite, thisTypeInFunctionsNegative, varianceAnnotationsWithCircularlyReferencesError, reachabilityChecksNoCrash1, importAttributes10/11, decoratorOnUsing, scannerUnexpectedNullCharacter1.
