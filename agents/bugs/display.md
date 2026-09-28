# Display bugs (types/symbols baselines)
What tttt prints differently from tsc, where the underlying type is right (or nearly). `./displaydiff.py` ranks the
mechanisms across the suite (`other` is mostly type-core bugs the types baseline shows first); these are the known
rules. Plan 21 (closed) did the bulk. Re-probed 2026-09-27 (triage) unless noted.

## Aliases
- `shows_alias()` (ty_display.c) hides type aliases declared in a block/function body when printing for a place.
  tsc's isDeclarationVisible also hides non-exported aliases inside namespaces in some cases; unverified.
- Non-strict `[]` argument prints as `any[]` in TS2345 where tsc says `undefined[]`
  (taggedTemplateStringsWithOverloadResolution1; plain calls too?).
tsc's rule, as far as it is understood (getTypeAliasInstantiation, getConditionalType, instantiateMappedType,
getIndexedAccessType): a type gets a *new* alias name only when it is the body of an alias declaration (`type X =
Foo<...>`) and that body instantiation created it: an object type (not a homomorphic mapped type over a non-union), a
union made by distributing a conditional or an indexed access, a deferred conditional/indexed access. A deferred
conditional keeps its root alias; a resolved one is whatever its branch is. Both halves are in `lower.c` (the
`renames` logic of TYPE_IDENTIFIER) and `instantiate.c` (TY_CONDITIONAL, `ty_realias`). Fixed in triage 2026-09-27:
distributed conditionals (`Awaited<string | undefined>`, `keyof Omit<any, "a">`), homomorphic `Prettify<X> =
{...} & {}`, and (not an alias) the order of a generic spread (`{ a } & T & { b }`). Still wrong (probes in `test/probes/plan19/valibot/`):
- keyof_enum_object.ts: `type V = EnumValues<typeof specialEnum>` (a mapped type indexed by `keyof`, resolving to all
  the enum's members) prints `V`, tsc `specialEnum`. Probably: a union that is exactly an enum's members is the enum
  type in tsc (same interned union), and the alias doesn't win.
- symbol_key_string_index.ts: `E[K[number] & string]` stays deferred where tsc resolves it to the property type.
  Evaluation, not display.
- optional_mapped_index_signature.ts: an error message prints `C` (alias) where tsc prints the structure.
- typeVariableConstraintIntersections: T01..T73 print the alias where tsc prints the reduced type (tsc reduces `K &
  "c"` by K's constraint, getReducedType; that is the real bug, `bugs/type_core.md`).
- An imported alias (`import { Simplify } from ...; type X = Simplify<A>`): tsc passes the import's target alias as
  the new alias (getTypeFromTypeAliasReference's `resolveTypeReferenceName(node, SymbolFlags.Alias)` branch). Not
  looked at in tttt.

## Structure
- **Deferred tuple access**: `p[1]` with `p: [string, ...T]` prints `T[number]`, tsc keeps `[string, ...T][1]`
  (variadic_tuple_iterate.ts). tttt resolves the element access through the variadic part eagerly.
- **union_signatures_with_intersection.ts**: union member order and `(A | B) & C` vs `(A & C) | (B & C)` of signature
  types; `T & readonly [...]` where tsc prints T's constraint.
- **Numeric-named properties**: tsc prints a property declared with a numeric literal name as written (`1.1`,
  `9.671406556917009e+24`) and a string-literal one quoted (`{ "1": string }`); tttt quotes by the name's shape
  (objectTypeWithNumericProperty, binaryIntegerLiteral, octalIntegerLiteral,
  emitAccessExpressionOfCastedObjectLiteralExpressionInArrowFunctionES5). A computed negative number name prints
  `[-1]` (computedPropertyNamesDeclarationEmit6): `Prop.name_is_value` doesn't tell a number name from a string one.
- The same for object literals: `{ "0": 0 }` prints `{ 0: number; }`, tsc `{ "0": number; }` (probe: `var m:
  MyMap<number> = { "0": 0 }`, types baseline).
- `{ 2n: 1 }` is `{ 2: number }` here, `{}` in tsc (plus TS1539, `bugs/missing_checks.md`).
- `A & ({} | undefined)` for a truthiness-narrowed generic (typePredicatesOptionalChaining1,
  assertionFunctionWildcardImport2). Lone surrogates `"\u{D800}"` print `""` (unicodeExtendedEscapesInStrings10/11,
  InTemplates10/11: types only).
- `this` of the contextual signature in a function expression's type (`data : (this: Readonly<...> & Instance) =>
  ...`): vueLikeDataAndPropsInference(2), json.stringify.
- tttt instantiates member signatures of generic classes/interfaces that can't reference the mapped type parameters;
  tsc keeps them (getObjectTypeInstantiation filters with isTypeParameterPossiblyReferenced). Display compensates
  (`mentions_mapped()`, hacks.md); doing it in `instantiate.c` would be correct and cheaper. Measure on valibot/type-fest.

## Names and specifiers
- **`import("…")` specifiers**: tsc prints some specifiers as absolute paths the harness then strips (`/.src/a` -> `a`),
  so the text matches but the `^^^` underline is 6 longer (e.g. `exports` inside its own CommonJS file,
  intersectionsAndEmptyObjects). **45 tests fail on this alone** (`displaydiff.py`'s `underlines/order only`, the
  single biggest one-kind bucket; ambient modules are never affected). Probes (2026-09-27, `./tscbaseline.sh`): `a.ts`
  exports class C, `e.ts` returns `new A.C()`, `b.ts` has `export const v = E.mk()`. `v : import("a").C` gets the
  +6 underline iff b.ts (the file the type is printed in) contains an `import X = require(...)`, even an unrelated
  one; `import * as`, `export =` and dynamic `import()` don't trigger it. commonJSImportClassTypeReference (JS
  `require`) fits, intersectionsAndEmptyObjects (only `import('./ex')`, esModuleInterop) doesn't: find the real
  condition in the checker (getSpecifierForModuleSymbol falls back to the full fileName without a resolver host, so
  the question is where the non-prefixed specifier comes from). A tried implementation: `SourceFile.hidden_path_prefix`
  (6 for a test file named relatively) with the underline growing after the specifier in ty_display.c's
  out_symbol_name/out_declared_name, unconditionally: -190 tests, so the condition is essential. moduleSpecifiers' getModuleSpecifiers falling back to the file name. Separately, tsc
  prefers a package entry/`index` that re-exports the symbol and non-relative paths:
  declarationEmitForGlobalishSpecifierSymlink2, symbolLinkDeclarationEmitModuleNames(RootDir),
  bundledDtsLateExportRenaming, declarationEmitTopLevelNodeFromCrossFile2, declarationsForInferredTypeFromOtherFile.
- `export default class {}` prints `typeof default` from other files where tsc names the importing alias (`import {
  default as b }` -> `typeof b`; importHelpersWithImportOrExportDefault, 10 configurations): getNameOfSymbolAsWritten.
- **Union/keyof origins** (~25 tests: stringLiteralTypesInUnionTypes01, partiallyDiscriminantedUnions,
  flowControlTypeGuardThenSwitch, intlNumberFormatES2023, reactTagNameComponentWithPropsNoOOM(2),
  partialOfLargeAPIIsAbleToBeWorkedWith, privateNameStaticMethodAsync, doNotInferUnrelatedTypes, trackedSymbolsNoCrash,
  spreadBooleanRespectsFreshness, literalFreshnessPropagationOnNarrowing). **Decided (plan 21): not now**; a type-core
  feature (unions carry an origin through narrowing and filtering, keyof keeps its operand).

## Underlines (node reuse)
- `() => 'foo' as const`: tsc's underline reuses the `"foo"` (`^^^^^^     `), tttt underlines the whole return type.
- readonly_through_prettify.ts: `| undefined` of optional properties under exactOptional-less strict: tsc's
  underline has gaps (reused nodes), tttt's doesn't.

## Symbols baseline
- `infer X extends C<...>`: references inside an `infer` type parameter's constraint are missing
  (partial_check_overload.ts).
- Mapped properties over an intersection: tsc lists the declarations from every intersection member that has the
  property, tttt the first (mapped_over_intersection_readonly.ts); intersection_with_type_param.ts is the reverse
  (tttt misses a repeated Decl).
- Duplicate class/interface members show as separate symbols (tsc: one merged symbol): the TS2687 tests.
