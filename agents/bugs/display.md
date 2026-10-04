# Display bugs (types/symbols baselines)
What tttt prints differently from tsc, where the underlying type is right (or nearly). `./displaydiff.py` ranks the
mechanisms across the suite (`other` is mostly type-core bugs the types baseline shows first); these are the known
rules. Plan 21 (closed) did the bulk. Re-probed 2026-09-27 (triage) unless noted.

## Aliases
- `shows_alias()` (ty_display.c) hides type aliases declared in a block/function body when printing for a place.
  tsc's isDeclarationVisible also hides non-exported aliases inside namespaces in some cases; unverified.
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
- A union containing an intersection with a nullish member prints without parentheses: `T | undefined & {}` where
  tsc prints `T | (undefined & {})` (without strictNullChecks, where it isn't reduced).
- Union order of literal keys follows type ids: the rest of a generic `this` is `Omit<this, "getter" | "setter" |
  "method" | "publicProp">` in tttt, `"publicProp" | ...` first in tsc (the pattern's key literal is older there);
  destructuringUnspreadableIntoRest fails on this alone.
- **Deferred tuple access**: `p[1]` with `p: [string, ...T]` prints `T[number]`, tsc keeps `[string, ...T][1]`
  (variadic_tuple_iterate.ts). tttt resolves the element access through the variadic part eagerly.
- **union_signatures_with_intersection.ts**: union member order and `(A | B) & C` vs `(A & C) | (B & C)` of signature
  types; `T & readonly [...]` where tsc prints T's constraint.
- **Numeric-named properties**: tsc prints a property declared with a numeric literal name as written (`1.1`,
  `9.671406556917009e+24`) and a string-literal one quoted (`{ "1": string }`); the suite's cases pass now. A computed negative number name prints
  `[-1]` (computedPropertyNamesDeclarationEmit6): `Prop.name_is_value` doesn't tell a number name from a string one.
- The same for object literals: `{ "0": 0 }` prints `{ 0: number; }`, tsc `{ "0": number; }` (probe: `var m:
  MyMap<number> = { "0": 0 }`, types baseline).
- `{ 2n: 1 }` is `{ 2: number }` here, `{}` in tsc (plus TS1539, `bugs/missing_checks.md`).
- `this` of the contextual signature in a function expression's type (`data : (this: Readonly<...> & Instance) =>
  ...`): json.stringify.
- tttt instantiates member signatures of generic classes/interfaces that can't reference the mapped type parameters;
  tsc keeps them (getObjectTypeInstantiation filters with isTypeParameterPossiblyReferenced). Display compensates
  (`mentions_mapped()`, hacks.md); doing it in `instantiate.c` would be correct and cheaper. Measure on valibot/type-fest.

- **Order dependence of `Array<T>` vs `T[]`**: an annotation's display depended on when the global Array type was
  first built (moot since plan 37 D builds Array at checker start), but the mechanism is still there for other global
  generics: if `X<T>` vs another spelling flips with `-f` vs a full run or with `--checkers`, find what the printer keys on.

- tttt interns every empty `{}` literal as one type, so an earlier `{}` in a file changes union member order compared
  with tsc (`test/probes/plan37/evolving/objects.ts` puts the affected case first).
- A conditional type as a constraint prints parenthesized: `<Expected extends (Extends<A, Expected>) extends true ?
  ...>`, tsc without the parentheses (expect-type).
- reactDefaultPropsInferenceSuccess: the TS6500 related info prints `keyof Props` where tsc prints `"when" | "error"`.

- **Type creation order is union order.** tsc sorts union members by type id, so tttt has to make types in tsc's order.
  Known rules now ported: the class check makes the class's value type first, type parameters are made in
  checkTypeParameter order (constraint references first), type literals when the check walk meets them, annotations
  before initializers, `{}` is one early type (emptyTypeLiteralType). C's unspecified argument evaluation order is a
  trap: `union_of(me, make_a(), make_b())` makes b first under GCC. `./argorder.py` lists such calls (the remaining
  ones are relation-only or deliberate).

## Names and specifiers
- enumAssignmentCompat6 (passes errors): in f.ts, a function-local `enum DiagnosticCategory` shadows the file's exported
  one; tsc prints the exported one as `import("f").DiagnosticCategory` in the types baseline (not accessible by name
  there), tttt as `DiagnosticCategory`.
- **`import("…")` specifiers**: resolved (2026-09-29). The +6 underline is a file that an import reached before the
  harness's root list did (tsc then names it by its absolute `/.src/` path, which the harness strips from the text but
  not the underline): `SourceFile.loaded_by_import`. The reuse gaps of a renamed `import("m").A` shift with it.
  Still: tsc prefers a package entry/`index` that re-exports the symbol and non-relative paths:
  declarationEmitForGlobalishSpecifierSymlink2, symbolLinkDeclarationEmitModuleNames(RootDir),
  bundledDtsLateExportRenaming, declarationEmitTopLevelNodeFromCrossFile2, declarationsForInferredTypeFromOtherFile.
- **`import * as ns` of a CommonJS file from an ES module file (node16+)** is a clone of the module in tsc
  (resolveESModuleSymbol, cloneTypeAsModuleType): its type is `typeof ns` by that alias, and the symbol chain doesn't
  name the module through it (`typeof m26` via `import m26 = require`, not an earlier `import * as
  m4`). tttt's namespace import is the module itself. Tried making the chain skip such imports
  (symbol_chain.c candidate_list): the import lines then print `typeof import("index")`, since the type itself must
  be a per-alias clone. Node16 tests (*PackageExports*, *PatternExports*: those
  also print `typeof cjsi.cjs.mjs` where tsc's DFS goes through the `cjs` export whose exports table is a distinct
  object, then `mjs`).

- JS corpus leftovers (probes in `test/probes/plan19/webpack/`): a `require("m").X` alias that resolves to nothing
  prints `error` in tsc (tttt `any`); tsc lists a same-named `@typedef` as a second declaration of the alias symbol;
  `module.exports = f` of a function declaration prints `typeof f` (require_property_value_only.ts); a JS `const S =
  Symbol()` read through a `@type {typeof S}` const prints `symbol` in tsc (jsdoc_accessor_type.ts).

## Underlines (node reuse)
- A binding default `{} as any` misses tsc's empty `^^^` types-mask line (bindingDefaultAnyRemovesUndefined types).
- `() => 'foo' as const`: tsc's underline reuses the `"foo"` (`^^^^^^     `), tttt underlines the whole return type.
- readonly_through_prettify.ts: `| undefined` of optional properties under exactOptional-less strict: tsc's
  underline has gaps (reused nodes), tttt's doesn't.

## Symbols baseline
- An augmentation of an `export =` module (`declare module "./file1" {}`): tsc's symbol at its name prints
  `Symbol("./file1", ...)`, tttt names it through an alias in scope (`Symbol(x, ...)`): augmentExportEquals4-6.
- A module's own exports reached through an alias of the module (`import * as self from "./self"`) are named
  `self.x` at their declarations (baseline_test_case_module_alias_text): functions, variables, type aliases,
  classes and interfaces ask (namespaces and enum members use their full chain). An `export default class C`
  reached through the alias is `self.default` in tsc, `C` here (its local symbol isn't the `default` export), and its
  members `self.default.p` (defaultDeclarationEmitShadowedNamedCorrectly).
- `infer X extends C<...>`: references inside an `infer` type parameter's constraint are missing
  (partial_check_overload.ts).
- Mapped properties over an intersection: tsc lists the declarations from every intersection member that has the
  property, tttt the first (mapped_over_intersection_readonly.ts); intersection_with_type_param.ts is the reverse
  (tttt misses a repeated Decl). vue's DefineProps is one: a property of `Readonly<LooseRequired<T>> & {...}` lists
  its declaration once, tsc twice (the modifiers type of LooseRequired is `T & Required<T>`, both members have it).
  Also there: `this` of a ThisType marker prints no Symbol line (probes in plan19/vue/).
- `Pick<T, "a">` with T generic: the mapped property expanded from the key's lower bound has no declaration
  (`p.a : Symbol(a)`, tsc `Symbol(a, Decl(...))` from T's constraint).
- Duplicate class/interface members show as separate symbols (tsc: one merged symbol): the TS2687 tests.
- **Spread doesn't keep the property's identity**: tsc's getSpreadSymbol returns the same symbol when readonly-ness
  doesn't change, so `{ ...x }.a` with `x: N.A` shows `Symbol(N.A.a)` and reuse carets in the types baseline; tttt
  `Symbol(a)`. Setting `.reuse` in `spread_props_of` (ty/expr.c) did it but lost 14 tests: `spread_into`, excess
  property checks and JSX spread attributes read the builder's other fields (and object literal widening may rebuild
  the properties).
- A union property's symbol lists every member's declarations in tsc (`(A[] | B[]).filter` is `Symbol(Array.filter,
  Decl x4)`, tttt x2: unionOfArraysFilterCall).
- A local type merged with an exported value of the same name lists both declarations (importElisionExportNonExportAndDefault).
