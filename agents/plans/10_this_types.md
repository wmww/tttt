# Plan 10: polymorphic `this` types

## Why
tttt has no `this` type. Inside a class, `this` is the class's instance type (`ty_identifier_reference_type`,
`THIS_CLASS_INSTANCE`); a `this` type node lowers to the same (`lower_identifier`); in an interface `this` resolves
to nothing (`Symbol(this)` with no declaration). So:
- **Suite:** ~242 types baselines fail *only* on `this` lines, 969 have some (measured 2026-09-22 by comparing
  types dumps; `>this : this` appears 5148 times in tsc's baselines). By name, `*this*`/`fluent*` tests pass fully
  44 of 289 (conformance/types/thisType has 30).
- **Corpus:** fluent calls lose their receiver: `perFileCache.set(path, x).get(path)` (resolutionCache.ts 869: `Map.set`
  returns `this`) gives the error type, which cascades into TS7022 x3 + TS7006 x3; also `xs.slice().sort()`-style
  chains (plan 08 counted TS7022 13, some of which are plan 09's). Small in the corpus, big in the suite, and every
  lib collection method that returns `this` is wrong today.

Probe: `test/probes/plan09/this_basic.ts` (class `self()`/`chain(): this`, a derived class calling it, `Map.set().get()`,
an interface `clone(): this`). It fails on all three components.

## The model (tsc's, observed)
- Every class, and every interface that mentions `this` anywhere including through its bases (tsc's
  `isThislessInterface`), has a `this` type parameter (`thisType`, printed `this`). Its constraint is the declared
  type. `this` type nodes in the members, and the `this` expression in instance members, are that parameter.
- A type reference carries an optional extra type argument for `this` (`getTypeWithThisArgument`). Members of a
  reference `C<A>` are the declared members instantiated with `T -> A, this -> (the this-argument, or the reference
  itself)`; for a non-generic class `C` that's `this -> C` (tsc: a class's declared type is a reference to itself).
  So `d.chain()` with `d: D` is `D`, and `m.set(...)` with `m: Map<K, V>` is `Map<K, V>`.
- Inherited members are taken from the base *with this-argument = the derived type's this parameter*, then
  instantiated with the derived type's mapper: that's how `D` sees `C.chain(): this` as returning `D`.
- The apparent type of a type parameter whose constraint is a reference is that reference with this-argument = the
  type parameter (`getApparentType`: `objectFlags & Reference && t !== type ? getTypeWithThisArgument(t, type)`);
  intersections get this-argument = the intersection (`getApparentTypeOfIntersectionType`). This is what makes
  `this.chain()` inside the class `this`, not `C`.
- Relations: `this` relates to its constraint (so `this` -> `C` works, `C` -> `this` is TS2322 "'C' is not assignable
  to type 'this'"); references with the same target compare their arguments by variance and the this-argument
  covariantly (tsc: arguments beyond the variances array are covariant).
- Perf rule from tsc: when the mapper only maps `this` (`createInstantiatedSymbolTable(..., mappingThisOnly)`),
  members that don't mention `this` (`isThislessType` / `isThislessFunctionLikeDeclaration` /
  `isThislessVariableLikeDeclaration`, syntactic) are *not* instantiated, they're shared. Without this, every
  non-generic class would instantiate all its members on first use. Adopt it.

## Progress
**Stage 1 done (2026-09-22)**: suite 3935 -> 4107, corpus 109 -> 95, typecheck +2%. What it is: `documentation.md`
"`this` types"; what it leaves out: `hacks.md` "`this` types (plan 10 stage 1)". Fixed on the way: return-type
inference from a contextual type that mentions an outer type parameter (`const y: MAC<T> = createMAC()` was
`unknown`), TS2739 vs TS2740 cutoff, signatures whose return type is being inferred are `any` in relations (tsc's
isResolvingReturnTypeOfSignature). Deviation from the plan: no relation rule for this-arguments (a reference with
one is compared structurally), nothing needed it yet. Stage 1's item 6 (flow): `this instanceof D` and `this is T`
predicates on methods work (typeGuardFunctionOfFormThis passes); `this is T` as a property type doesn't parse.

**Stage 2 mostly done (2026-09-22)**: suite 4107 -> 4110 (types +41), corpus unchanged (95, no spurious TS2683 under
the corpus' strict mode). Contextual `this` from a contextual signature's `this` parameter, the object literal
(`ThisType<T>` through enclosing literals, else the contextual type, else the literal's type) or the object of
`o.m = function`, all under noImplicitThis like tsc; TS2683; top-level `this` (`typeof globalThis` in scripts),
TS2331 in namespaces; non-arrow functions without a `this` parameter are context sensitive (won back
generatorTypeCheck45/46); object literal accessors are typed lazily (their bodies can ask for the literal's type).
Signatures already printed and checked their `this` parameters (TS2684), the notes saying otherwise were stale.
Left: TS7041, TS7017, the ThisType argument isn't instantiated with the inference in progress; lost
silentNeverPropagation (triage.md). Stage 3 not started.

## Stage 1: the `this` type parameter and this-arguments
1. `Ty.declared` gets a `this_param` (a `TY_TYPE_PARAM` flagged as a this type, printing `this`) for classes and
   non-thisless interfaces, made in `make_declared`. The binder needs a type-meaning `this` for interfaces
   (`scope_get_this` only knows classes); `this` in a type literal/at top level is TS2526 (can wait).
2. `TY_REFERENCE` gets an optional this-argument, part of its interning key (`ty_reference` in ty/checker.c).
   Non-generic targets get references too once a this-argument is given (today `ty_reference` returns the target).
   Everything that reads `reference.args` keeps working; display ignores the this-argument.
3. Members (`ty/members.c`): today `resolve_declared_members` builds the declared type's members directly and
   `resolve_members_inner(TY_REFERENCE)` instantiates them with `type_params -> args`. Split "declared members"
   (raw, mentioning `this_param`) from "resolved members" (instantiated with `this -> self` for the declared type,
   `this -> this-arg or the reference` for references), with the mapping-this-only sharing rule above.
   `builder_add_inherited` takes the base with this-argument = the derived type's `this_param`.
4. Lowering: a `this` type node is the enclosing class/interface's `this_param`; the `this` expression in instance
   members is too (`ty_identifier_reference_type`), which means property access on `this` goes through the
   apparent type rule above (`ty_apparent_type`, relate.c). `collect_free_params` knows `this` now (it gives up on
   it today, `free_params_unknown`) and `LowerCtx.generic` must be set for members of a type with a `this_param`.
5. Display: `this` in the types baseline, `this` in signatures (`() => this`), no symbols-baseline line for a `this`
   type node (the probe shows tsc prints none).
6. Flow: `this` is a reference for narrowing already? Check `this instanceof D` and `this is T` predicates
   (`isFoo(): this is Foo`) against tsc on the thisType tests.

Check: `this_basic.ts` passes, the corpus resolutionCache.ts cascade is gone, `./fasttest.sh --summary` gains a
large block of types components. **Losses are expected to be real and informative** (anything that compared a class
instance type by identity will now see `this` or a reference-with-this-arg): read `./resultdiff.py --lost` rather
than accepting a net number.

## Stage 2: contextual `this` and the value side
tsc's `checkThisExpression` / `tryGetThisTypeAt` / `getContextualThisParameterType`:
- `this` in object literal methods: `ThisType<T>` marker in the contextual type, else the contextual type, else the
  literal's own type (under `noImplicitThis`). Today these are `THIS_ANY`.
- `this` in a function expression assigned to a property or passed where a signature declares `this: T`.
- `this` at top level (`typeof globalThis` in scripts, `undefined` in modules; today `undefined` everywhere).
- TS2683 ("'this' implicitly has type 'any'"), TS2684 (a call's receiver not assignable to the signature's `this`,
  see `hacks.md` "`signature_resolve`: a `this` parameter is dropped"), signatures printing their `this` parameter
  (triage).

## Stage 3 (if cheap): checks that need `this`
Class/interface `extends`/`implements` checks compare `typeWithThis` to `getTypeWithThisArgument(base, thisType)`
(tsc checkClassLikeDeclaration ~46272); `this` in static members / constructor parameter defaults (TS2331,
TS2332?, check tsc's baselines for the codes); `polymorphic this` in index signatures and in `ThisType`.

## Risks
- The change touches the identity of every class type's members. Budget: stage 1 is one to two sessions. If the
  "declared vs resolved members" split turns out to fight the lazy-member design (`TyObjectState`), stop and write
  down why before patching around it.
- Printing: a reference with a this-argument must never print differently from the one without, and must not
  appear as a separate union member (`C | C` with two ids). Interning by (target, args, this-arg) makes them distinct
  `Ty`s by design, as in tsc; tsc's printing and subtype reduction live with it, so check a union of the two.
- Perf: `./bench.sh` after stage 1. The this-only sharing rule is the mitigation; measure before optimising further.

## Validation
- `this_basic.ts` passes; a types-baseline count of tests whose only diff is `this` lines (redo the measurement:
  compare tttt's and tsc's `.types` dumps, count tests where every differing line mentions `this`) goes from ~242 to a
  small explained remainder.
- Corpus: nothing `this`-related left in `./realworld.py`.
- `./fasttest.sh -s`, `measurements.md`, remove the `this` entries from `hacks.md` (`lower_identifier`,
  `builder_add_class_items` "no `this` type parameter", relate.c gaps) and triage.md.
