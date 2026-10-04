# Plan 35: silent type differences (the types component's "other" and "any" buckets)
Written by the insights of 2026-09-27 (third of the day). Short on purpose: stage 1 builds the tool and rewrites
this plan's "Queue" with what it finds.

## Why
The types component is the one that fails most (2797 tests, against 2372 for errors). `./displaydiff.py` names the
display mechanisms (union order, `typeof`, `import()`...), and plan 21 worked those. What remains in the tests that
pass errors is mostly **not display**: tttt computes a different type and no diagnostic shows it. Fixed by one kind
(TS + JS): `other` 394 + 35, `we-any` 127 + 39 (tttt says `any` where tsc has a type), `tsc-any` 101 + 14 (tsc says
`any`, usually error recovery), `line-extra` 118 + 6 (tttt annotates something tsc doesn't). Examples in `other`:
`objectFreeze` infers `boolean` for a `false` return, `genericCombinators2` infers `Collection<number, Date>` where
tsc falls back to `any`, `localTypes3` is plan 32 stage B. These are checker semantics bugs that a clean corpus can't
see and the errors component doesn't catch: the same class of bug as a wrong inference that later produces a
spurious error in a real project. No plan or session has owned them; the lesson of plans 27/34 applies (a bucket gets
worked once a tool names its mechanisms).

## Stage 1 as planned: the tool, then the first fixes (one session)
- Split `other` and `we-any` in `displaydiff.py` (or a sibling script) by what can be told mechanically: the AST kind
  of the annotated node (call result, property access, parameter, literal, `this`...), the shape of the difference
  (literal vs widened, type arguments differ, `any` vs a type, a member missing/extra, generic vs instantiated,
  `T` vs its constraint), and whether the node is inside a generic function (plan 32 B). The goal is buckets of the
  form "call results whose inferred type arguments differ: N tests", not a perfect classification.
- `tsc-any` and `line-extra`: check how many are one mechanism each (tsc's error-recovery `any`/`errorType` for
  unresolved names and bad calls; which nodes tsc's type writer skips).
- Write the ranked queue below (tests fixed alone per mechanism), then fix from the top for the rest of the session.
- Record the numbers in `measurements.md` (replace the displaydiff line).

## Stage 1 (done, 2026-09-27): suite 9427 -> 9666
`./displaydiff.py --fine` names each catch-all line `<shape>/<annotated node>` (`displaykinds.fine_kind`: shape =
we-any, tsc-any, union-fewer/more-members, type-args-differ, signature-differs, object-differs, nested-we-any...;
node = declaration, identifier, call, literal, property-access...), and ranks the tests by the kind of their **first**
differing line (a wrong root type spreads over lines of other kinds, so "fixed alone" undercounts). `--shape` drops
the node. `-k KIND --names` lists the tests fixed alone and the tests whose first line is that kind.

Fixed (mostly error recovery and type-writer rules, not inference): shorthand ambient modules are any; tsc's writer
skips a number/string literal under `export default`; a non-instantiated namespace's name has no type line;
invalid assignment targets are errorType; wrong type-argument counts are errorType (TS only); `import type`/`export
type` names show the declared type; `import X = N` of an uninstantiated N is errorType; IIFE parameters are typed by
the arguments. we-any 127 -> 93, tsc-any 102 -> 32, line-extra 118 -> 20 (TS tests fixed alone); `other` (393 + 39)
barely moved: it is many small mechanisms.

## Stage 2 (done, 2026-09-28): suite 9833 -> 9951
The queue below from stage 1, worked top to bottom, and what the buckets showed on the way:
- Done: (1) TS fields typed by the constructor / static blocks' flow (`field_type_from_assignments()`); (2) tagged
  templates resolve as calls (`CallInfo.tagged_template`, see hacks.md), which exposed two general bugs: non-strict
  null/undefined weren't subtypes of anything (relate.c `is_simple_related`) and inferred type arguments didn't widen
  null/undefined; (3) past-the-end tuple elements are undefined; (4) `export as namespace` of no value is errorType;
  (5) wrong type-argument counts instantiate tsc's longest candidate; (6) synthetic default imports of declaration
  files, node_modules files get their package.json format; (7) IIFE generators yield into the call's context;
  (8) property names print like tsc (`1.1` unquoted, `"1"` quoted when string-named); (9) `m() {} m(x) {}` has one
  signature. Beyond the queue: heritage names are property accesses to the type writer (+21), `(null)` has a line,
  signature parameter initializers are walked, namespaces with `;` are instantiated, `catch {}` blocks are walked,
  **class field initializers are contextually typed by their annotation** (a real bug: spurious TS7006 on
  `f: (a: number) => number = (a) => a`), missing destructured properties with defaults are undefined.
- we-any/declaration (first differing line) 95 -> 50; line-missing 73 -> 72 (the heritage/null/catch fixes moved tests
  into other buckets as much as out); `./displaydiff.py` fixed-by-one: other 383 + 54, we-any 65 + 41.

## Stage 3 (done, 2026-09-29, todo entry 9): suite 11472 -> 11962
Four Opus sub-agents, each owning the failing tests whose **first** differing line was in one family
(`./displaydiff.py --fine --first-tsv`): A any/error recovery, B inference/signatures, C objects/unions/literals, D
display/type writer. ~150 commits, 0 lost, corpora unchanged, instructions +0.3-1.2%. Tests with errors passing and
types failing 968 -> 467. Mostly tsc error-recovery rules (errorType for unresolved names, element accesses, bad
spreads), binder-style member symbol merging (`ClassItem.symbol_first`, duplicate interface properties), index
signatures from computed names, accessor pairs with setters (write types), property-name printing, union order from
tsc's early `""`/`0`/`0n` literals, `NonNullable<T>` for `||`/`??`, `removeNullableByIntersection`, higher-order
inference for generic functions in literal arguments, reverse mapping for Readonly/Partial-like mapped types,
noUncheckedIndexedAccess's missingType, `import x = N.C` and instantiation expression lines, `typeof f` for
self-referencing functions. Split by first-line kind duplicated little (accessor pairs C/D, non-array rest targets and
`!` in optional chains B/C).

## Queue (after stage 3)
What the agents left, roughly by size (their test names are examples):
1. **Union order** (~24 first-line, 18 alone): creation order of types; no general fix found (arrayFrom,
   controlFlowCaching, spreadUnion, subtypesOfTypeParameter). Plan 21 territory.
2. **node16 `import * as` of a CommonJS module**: the *PackageExports*/*PatternExports* chains (27 tests) were a
   circular import alias keeping its target (fixed 2026-10-04). What's left really is tsc's per-import clone
   (resolveESModuleSymbol -> cloneTypeAsModuleType, members through getTypeWithSyntheticDefaultImportType's spread:
   readonly exports become new symbols without a parent): nodeModulesTypesVersionPackageExports x3 (`mod.x` of a
   cloned import is `Symbol(x)`, not `Symbol(mod.x)`), the `typeof m4`-vs-`m26` naming in bugs/display.md, maybe
   nodeModulesResolveJsonModule. A per-import module type is a refactor of how `import * as` resolves; small payoff.
2. **node16 `import * as` of a CommonJS module** (~24: nodeModules1, nodeModulesAllowJs1, *PackageExports*): tsc
   clones the module symbol per import (prints `typeof m4`); needs a per-import type, not a naming tweak. Notes in
   `bugs/display.md`. (The *PackageExports* symbols chains were circular aliases, fixed 2026-10-04; the types
   component's `typeof` clones remain.)
3. **Line-missing one-offs** (~50): alias vs structure for recursive conditional/mapped types
   (recursiveConditionalCrash1-3), imports/exports inside functions (moduleElementsInWrongContext2/3), missing
   identifier recovery (`for ()`, reservedWords3, JSX namespaced names).
4. **Underlines** (~40): annotation reuse on instantiated overloaded interface methods (ipromise2-4,
   twoMergedInterfacesWithDifferingOverloads) and inside mapped types.
5. **One type per expression** (~5): tsc re-checks arguments outside inference mode, or has a separate rest-element
   check (voidReturnIndexUnionInference, narrowingDestructuring, callChain.3, mappedTypeIndexedAccessConstraint).
6. **Inference**: variadic middle rest parameters (`...args: [unknown, ...T, unknown]`, typeParameterConstModifiers:
   the parameter shape can't represent it), reverse-mapped `?`/`-?` keeping undefined (mappedTypesArraysTuples),
   deep cases (multiSignatureTypeInference, nestedGenericSpreadInference, reverseMappedTypeRecursiveInference).
7. **Spread of a union with generic members** (spreadObjectOrFalsy, spreadInvalidArgumentType): `SpreadAcc` can't keep
   them, a refactor.
8. `yield` outside a generator's body (FunctionDeclaration6/7/10_es6, generatorTypeCheck57-59); merged/duplicate
   declarations (defaultNamedExportWithType1, typeAliasesForObjectTypes); scanner `\u` escapes in property names
   (scannerUnicodeEscapeInKeyword2); enum members with numeric names use the source text; declaration visibility
   (privacyInterface: depends on declaration emit); JS tests (~25) belong to plan 26.
9. tsc artefacts not worth mimicking: awaitedType x2 (first alias a union was created with), fixingTypeParametersRepeatedly1.
10. Left by fan-out agent A of 2026-10-04 (types: semantics/inference), each investigated far enough to name the
    mechanism:
    - **Generic function nested in a returned literal**: tsc's instantiateTypeWithSingleGenericCallSignature runs for
      any identifier with a contextual signature inside an inferring call (`map("", () => ({ x: identity }))`
      instantiates identity with the context's *fixing* mapper); tttt only does it for argument-level
      `generic_function`/`nested_generic` Args (call.c). inferentialTypingWithFunctionTypeNested,
      nestedGenericSpreadInference.
    - **Circular returns are any in tsc**: `let x = { foo: () => x }` gives foo `() => any` (x's type is being
      resolved when the arrow's return is asked); tttt resolves the return later and unrolls x
      (genericCallOnMemberReturningClosedOverObject; the opposite of plan 42's direction).
    - A spread namespace member declared by `export const f = (o: T) => {}` prints its parameters syntactically in tsc
      (typeFromExpression); tttt's spread props don't keep the declaration (declarationEmitResolveTypesIfNotReusable).
    - A reverse mapped optional property prints `a?: number | undefined` in tsc (its type includes the undefined),
      `a?: number` in tttt.
    - An import's non-string module specifier (`import * from Zero from "./0"`) is an expression tsc types (`Zero :
      any`); tttt's parser drops it (invalidSyntaxNamespaceImportWith{AMD,Commonjs,System}).
    - An ambient module nested in a script's ambient module is an augmentation to tsc (isModuleAugmentationExternal):
      unmerged and uninstantiated it's any; tttt declares a global ambient module (privacyGloImportParseErrors).
    - `x = 10 as T` parameter initializers print the asserted annotation syntactically (isolatedDeclarationsAddUndefined).
    - Merged interfaces with differently named type parameters keep each declaration's own (TS2428,
      twoGenericInterfacesDifferingByTypeParameterName); tttt maps the second's to the first's.
    - **Reuse needs accessibility, which declaration emit's painting changes**: tsc's trackExistingEntityName only
      reuses a name that isSymbolAccessible (shouldComputeAliasesToMakeVisible false); a non-exported alias inside a
      non-exported namespace of a module isn't, so tsc prints its structure (instantiateContextualTypes,
      declarationEmitInlinedDistributiveConditional, inlineMappedTypeModifierDeclarationEmit,
      mappedTypeGenericInstantiationPreservesHomomorphism, deepKeysIndexing). But with `@declaration` the emit runs
      first and its late painting sets `isVisible` on what it emitted, so the same name is reused afterwards
      (privacyFunctionParameterDeclFile & co.). Adding symbol_type_accessible_at() to name_accessible() in ty_display.c
      was +5 -4 for that reason; doing it right needs the declaration walk's painted set kept for the types baseline.
    - **Naming from a block scope**: TyDisplay.enclosing is the file/namespace scope, so a name shadowed by a local
      where the type is printed isn't noticed: tsc qualifies with `import("f")` (enumAssignmentCompat6,
      expandoFunctionBlockShadowing, multipleExportDefault3, exportAssignmentOfExportNamespaceWithDefault), and a
      reused `typeof b` of a function local in its own return annotation falls back (functionVariableInReturnTypeAnnotation).
      Needs the innermost scope at the printed position (the resolve walk could record it per types-baseline node).
    - CommonJS naming (plan 26 territory): `typeof mod1."d"` for an element-access export with expando members
      (moduleExportsElementAccessAssignment: tsc's createEntityNameFromSymbolChain keeps the written quotes, but
      typeFromPropertyAssignment39's `foo["baz"]` prints `foo.baz`, rule unknown), `typeof x.F` for a JS class
      reached through a require (lateBoundAssignmentDeclarationSupport6: the value-meaning chain misses
      `module.exports.F = F`), jsDeclarationsFunctionsCjs, moduleExportAliasImported, moduleExportPropertyAssignmentDefault,
      jsdocTypeReferenceToImportOfFunctionExpression.
    - Unexplained so far: typeArgumentInferenceWithObjectLiteral (`x => x` gets E1.X: when T is fixed tsc has only
      non-widened candidates), inferConditionalConstraintMappedMember (tsc: never), wellKnownSymbolExpando (tsc: no
      late-bound expando for `f[Symbol.iterator] = ...` in a TS file), typeGuardRedundancy (tsc keeps both signatures
      of `x.substr | x.toFixed` in a conditional's subtype reduction), fakeInfinity2/3 (`() => Infinity`, not the alias).

## Validation
`./displaydiff.py` fixed-by-one counts for the worked buckets go down, suite up, no corpus regressions. A fix to an
inference rule that shows up here should be checked on zod and valibot (inference-heavy) for instruction counts too.
