# Plan 32: mixin classes (and the two type-core gaps under them)

Draft by the plan 19 corpus session; rewritten 2026-09-27 by the planning session (todo entry 4). Three stages, one
session each; stage A is done. Stages A and B are type-core gaps that aren't about mixins at all; mixins are just where they
show up together. Do them in order (C needs both).

## What the planning session found
Probes in `test/probes/plan32/` (`./filetest.sh`, `FAST=1`).

1. **Intersections have no resolved members.** `ty_members()` of a `TY_INTERSECTION` is `empty_members`
   (`object_state()` returns null), so each consumer gathers the parts' signatures itself: `resolve_call()` (call.c,
   "An intersection is called through the signatures of all of its members"), `structural_related()`'s HACK in
   relate.c ("the signatures and index signatures of an intersection aren't combined": each kind satisfied by one part),
   `class_base()` (it asks `ty_members()` of the constructor, so `class C extends Mixed1` with
   `Mixed1: typeof M1 & typeof C1` finds no base at all), infer.c's signature inference. None of them applies tsc's
   mixin rule (resolveIntersectionTypeMembers + findMixins/includeMixinType: construct signatures of the parts that
   are mixin constructor types, meaning one signature `new (...args: any[]) => X` without type parameters, are
   dropped and their return types intersected into every other construct signature's return type; if *every*
   constructor part is a mixin, the first one is kept). Duplicate signatures are dropped (appendSignatures, identical
   compare), index infos of the same key are intersected (appendIndexInfo, readonly only if both are).
   `probe_intersection_members.ts`: mixinClassesMembers' 7 spurious TS2339 (`new Mixed1("hello").a`).
2. **Declarations inside generic scopes don't capture the outer type parameters** (tsc's getOuterTypeParameters). This
   is a general soundness leak, not just a mixin one: `function f<T>(v: T) { interface I { x: T } ...; return i }`
   gives `f(3).x: T` (tsc: `number`, the type prints `f<number>.I`). The same for local classes (`Boxed<number>.Box`),
   class expressions (`h<boolean>.(Anonymous class)`), and local type aliases (`g("s").y` is `T`; tsc expands the alias
   `{ y: string; }`). `probe_outer_type_params.ts`. In tsc a class/interface's `typeParameters` are
   `outerTypeParameters ++ localTypeParameters`; written type arguments fill the local ones; a reference inside the scope
   passes the outer parameters as themselves; the declaration's type then gets instantiated with the function's
   mapper like any other type mentioning T, and display prints the outer arguments on the enclosing function's name
   (`Tagged<typeof Base>.C`, and `Tagged<any>.C` for the `prototype` property). Anonymous types (class expressions,
   type literals) are instantiated on the outer parameters they actually mention (getObjectTypeInstantiation,
   isTypeParameterPossiblyReferenced). In tttt the only trace of this is `LowerCtx.generic` ("some type parameter is in
   scope"), which a local declaration's own lowering doesn't even get: `ty_declared_type_of()` lowers an alias with
   `.generic = params.len > 0` from its own parameters only.
   Suite: ~30 tests have a `name<args>.Local` type in their baselines (`grep -lE '[A-Za-z_$]+<[^>]*>\.' *.types`),
   among them localTypes3/5, genericClassExpressionInFunction, baseExpressionTypeParameters, most mixin tests; more
   fail on the leaked `T` without printing a qualified name (not counted).
3. **Mixin classes proper** (class_base(), hacks.md "`class_base()`"). tsc:
   - getBaseTypeVariableOfClass: when the base constructor type is a type variable (or an intersection containing
     one), the class *value's* type is `typeof C & T` (getTypeOfFuncClassEnumModule). Displayed
     `{ new (...args: any[]): C; prototype: Tagged<any>.C; } & T` when `C` isn't accessible by name (local), which
     is what makes `Tagged(Base)` print `{ new (...args: any[]): Tagged<typeof Base>.C; prototype: Tagged<any>.C; } &
     typeof Base` and `new (Tagged(Base))()` print `Tagged<typeof Base>.C & Base` (the intersection mixin rule, 1).
   - The base *instance* type of a class extending `T` comes from the construct signatures of T's apparent type
     (its constraint), and isValidBaseType accepts a type parameter whose constraint is valid: `class Building extends H`
     with `H: abstract new (...a: [Census]) => BT` has base `BT` and default constructor `(...a: [Census])`
     (type-fest's spurious TS2322/TS2554: tttt's class_base() rejects a non-object instance and the class falls back to
     a 0-argument constructor).
   - isConstructorType: a type variable whose constraint is a mixin constructor type is a constructor (TS2545
     "A mixin class must have a constructor with a single rest parameter of type 'any[]'" otherwise).
   - Abstract: `abstract new` signatures carry through; a non-abstract class extending an abstract mixin result gets
     TS2515 naming the intersection (`withBar<typeof Bar>.ExtendedBar & Bar`, needs 2 for the name), TS2797 (a mixin
     class that extends an abstract construct signature must itself be abstract).
   - Afterwards `ty_has_unresolved_base()` (the TS2339/TS7053 gate for classes whose base class_base() can't see) should
     have nothing left to gate for mixins; delete it or narrow it to the truly unresolvable (an `any` base).

## Stage A: intersection members: done (same session as the planning)
`resolve_intersection_members()` (members.c): `TY_INTERSECTION` has a `TyObjectState` (`composite.state`) and
`ty_members()` gives its call signatures (identical ones dropped), construct signatures with the mixin rule (a clone
with the intersected return type) and index infos intersected per key; no properties. Sites that looped over the
parts now just ask `ty_members()`: `resolve_call()`, JSX tags, `ty_applicable_index_info()`, the contextual call
signature, iteration's call signatures, union signatures, signature inference, and relate.c's per-part HACK
(an intersection source is related as a whole; `index_signatures_related()` and `lacks_common_properties()` get the
intersection's properties from `source_props()`). mixinClassesMembers errors, mixinAbstractClasses and override19/20
symbols, declarationEmitClassMixinLocalClassDeclaration; webpack 3981 -> 3977; callgrind neutral (valibot 5.376 ->
5.375 G, type-fest 3.354 -> 3.330 G). Not done: `source_props()` doesn't take the apparent type of type-variable parts.

## Stage B: outer type parameters (one session, maybe two)
The riskiest stage: it touches how every local class/interface/alias is lowered and printed. Prototype first on the
probe, then measure.
- Binder: record each class (declaration and expression), interface and type alias's outer type parameters (the
  type parameter symbols of every enclosing function, method, class, interface, alias, mapped type and `infer`
  scope, in tsc's order: outermost first). Only non-top-level declarations can have any; keep it a pointer to a shared
  list so a file of top-level declarations pays nothing.
- Declared types: `declared.type_params` becomes outer ++ local with a `local_offset` (tsc's localTypeParameters).
  Everything that reads type_params for *written* arguments (lower_type_args, TS2314/TS2315 counts, `class_base()`'s
  written args, display of `C<X>`) must use the local ones; the outer ones default to themselves. The reference a
  bare name makes inside the scope is `C<outer..., local defaults>`, which carries `TYF_COULD_CONTAIN_TYPE_VARIABLES`,
  so instantiation with the function's mapper does the rest.
- Type aliases: tsc instantiates a local alias's body with the outer parameters like any type; tttt should just lower
  the body with the right `LowerCtx.generic` (the alias case in `ty_declared_type_of()`) and make sure an alias with no
  own parameters but outer ones isn't treated as closed (its aliased display is dropped on instantiation: tsc prints
  `{ y: string; }`).
- Class expressions / anonymous types: their instantiations must not be shared across different outer arguments
  (check how `TY_ANONYMOUS` with `anonymous.mapper` handles this today; the leak says it doesn't get instantiated at all).
- Display: `f<number>.I`, `Tagged<any>.C`, `h<boolean>.(Anonymous class)`. The symbol chain printer (symbol_chain.c,
  ty_display.c) prints a local declaration's enclosing function with the outer arguments. tsc: typeToTypeNodeHelper's
  outer type parameter handling in symbolToTypeNode (lookupTypeParameterNodes on the parent chain).
- Validate: `probe_outer_type_params.ts` types clean vs tsc; the ~30 tests above (list them with the grep, compare
  `./resultdiff.py`); localTypes1-5, genericClassExpressionInFunction; no lost components; corpora unchanged; callgrind on
  the tsc corpus (it has no local generic declarations to speak of, so the cost must be ~0).

## Stage C: mixin classes (one session)
- The class value type (`ty_type_of_symbol` for a class, whatever returns `ty_static_type_of()` as a value) becomes
  `typeof C & T` when the base constructor is a type variable (or an intersection with one). Only such classes pay
  (a flag computed from the `extends` expression's type, cached in the class symbol's links). Check what else assumes
  a class value is `TY_DECLARED_CLASS_STATIC` (`typeof C` displays, `instanceof`, static member lookup).
- `class_base()`: the base instance of an extends expression whose type is a type variable comes from the apparent
  type's construct signatures; accept a type-variable instance type (isValidBaseType); the default construct signatures
  come from the base constructor even when the instance type is a type variable. Keep the syntactic class-name path.
- TS2545, TS2515 on mixin results, TS2797, TS2509 (mixinAccessModifiers wants it), private/protected through mixin
  intersections (mixinPrivateAndProtected, mixinAccessModifiers: TS2341/TS2445 on intersection members; see
  `bugs/type_core.md`'s mixin entry and move it here or delete it once done).
- Remove or narrow `ty_has_unresolved_base()`; update hacks.md's `class_base()` entry.
- Validate: `test/probes/plan19/typefest/mixin_abstract.ts` at 0 diffs, type-fest's `abstract-class.ts` 4 gone
  (`./realworld.py --corpus type-fest`), the mixin tests (`grep -lE "new ?\(\.\.\.args: any\[\]\)"` over the test
  cases: mixinClassesAnnotated, mixinClassesAnonymous, mixinClassesMembers, mixinAbstractClasses(.2),
  mixinAccessModifiers, mixinPrivateAndProtected, mixinOverMappedTypeNoCrash, mixingApparentTypeOverrides,
  doubleMixinConditionalTypeBaseClassWorks, mixinIntersectionIsValidbaseType, noCrashOnMixin,
  exportClassExtendingIntersection, override19/20, accessorsOverrideProperty9), no lost components.

## Risks
- Stage B changes type argument counts of every local generic-scope declaration; anything indexing `type_params` by
  written position breaks silently. Grep `declared.type_params` (and `type_params.len`) and classify each use before
  starting.
- Stage C changes the value type of some classes to an intersection; code that pattern-matches `TY_DECLARED_CLASS_STATIC`
  on a class's value type will miss those. They're rare (only mixin classes), so failures are local.
- Stage A: relating intersections by their combined signatures is stricter in some cases and looser in others than
  the per-part HACK; expect a few relation changes (the vision's "stricter relation, latent inference bugs" lesson).
