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

## Stage B: outer type parameters: done
The resolve walk keeps the type parameter lists a scope is inside of (`Scope.generics`, an `OuterGenerics` chain with
the owner's name for display) and records them on every class, interface and type alias (`outer_generics`).
`Ty.declared.type_params` is outer ++ local with `outer_count`/`outer` (`ty_local_type_params()` is what type
arguments are written for: `lower_type_args()` passes the outer ones as themselves, and so do `class_base()` and the
deferred reference path); a local class's static side has the outer ones only and its value is the reference applied
to them (`ty_type_of_symbol()`), so the function's mapper instantiates statics and construct signatures (whose own
type parameters are the local ones). A local alias's body is lowered generic. Display: `f<number>.I`, `X.m<B, C>.Y`
(`out_outer_type_args()`, a method is qualified by its class), a static-side reference prints `typeof C`, and a type
alias declared in a block isn't printed by name when printing for a place (`shows_alias()`: tsc's
isTypeSymbolAccessible). Suite +18 (localTypes1/3/5, genericTypeAliases, declarationEmitInferredTypeAlias*,
typeArgumentInferenceWithClassExpression1/3...), nothing lost. Probes `probe_outer_type_params*.ts` match tsc except:
- tsc clones a generic method's type parameters whenever the signature is instantiated, so `new Outer<string>().m`
  returns `Outer<string>.Outer.m<U>.In`; tttt only clones constrained/defaulted ones and prints `Outer<string>.In`.
- Not done: tsc's extra outer parameters of a context-sensitive function expression (its contextual signature's type
  parameters), JSDoc `@template`/`@typedef` scopes, prototype-assignment declarations.
- The rest of the `name<args>.Local` tests are mixins (stage C).

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
