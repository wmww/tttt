# Plan 32: mixin classes (draft, needs a planning session)

Written 2026-09-27 by the plan 19 corpus session. Not scheduled: the next insights session decides.

## Why
A class that extends a value whose type is a type parameter (`class X extends Ctor {}` with `Ctor: T`, `T extends
new (...args: any[]) => object`) is how TypeScript does mixins. tttt approximates its base with `class_base()`
(`ty/members.c`, hacks.md "`class_base()`"): the return type of the first construct signature of the base expression's
type, no intersection with the type variable, no static side, and `ty_has_unresolved_base()` gates TS2339 on such
classes. What tsc does instead:
- getBaseTypeVariableOfClass: the static side of a mixin class is `typeof X & T` (getTypeOfFuncClassEnumModule), so a
  function returning the class returns `{ new (...args: any[]): X; prototype: X } & T` (displayed like that).
- resolveIntersectionTypeMembers: of an intersection with mixin constructor types (a single construct signature with a
  single `...args: any[]` rest parameter, isMixinConstructorType), the mixin signatures are dropped and their return
  types intersected into the other construct signatures' returns. `new (withBar(Bar))()` is
  `withBar<typeof Bar>.ExtendedBar & Bar`.
- The instance type of the mixin class itself has T's constraint's instance type as its base (getBaseTypes through
  the construct signatures of the base constructor type), and a class declared inside a generic function is
  instantiated with the function's type arguments (`withBar<typeof Bar>.ExtendedBar` in displays).
- TS2515 etc. for non-abstract classes extending an abstract mixin result; `abstract new` signatures carry through.

## Evidence
- type-fest `test-d/abstract-class.ts`: 4 of type-fest's 19 (probe `test/probes/plan19/typefest/mixin_abstract.ts`:
  spurious TS2322/TS2554 on `class Building extends HousingType {}` with `HousingType: AbstractClass<BT, [Census]>`,
  missing TS2515 on `class Wrong extends withBar(Bar) {}`, wrong `typeof ExtendedBar` everywhere).
- Suite (`build/failstats/tests.tsv`, 2026-09-27): mixinClassesAnnotated, mixinClassesMembers, mixinPrivateAndProtected,
  mixinOverMappedTypeNoCrash fail all three components; mixinClassesAnonymous, declarationEmitMixinPrivateProtected,
  mixingApparentTypeOverrides fail some. triage.md's mixinAccessModifiers / mixinPrivateAndProtected entry (mixin
  intersections aren't reduced) belongs here too.

## What the planning session should decide
- Where the static side of a class gets `& T` (the class symbol's type, only when the base is a type variable) without
  every class paying for it.
- How construct signatures of intersections are resolved (members.c's intersection members) and whether that's a
  small change or needs the intersection member resolution restructured.
- Whether classes declared inside generic functions already get instantiated per call (the display
  `withBar<typeof Bar>.ExtendedBar` suggests tsc treats them like any type declared in a generic scope), and what
  plan 30's substitution work means for it.
- Removing the `ty_has_unresolved_base()` TS2339 gate for mixins afterwards (hacks.md).

Validation: the probe and type-fest's abstract-class.ts at 0, the mixin suite tests above, no lost components.
