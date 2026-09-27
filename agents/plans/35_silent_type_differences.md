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

## Queue
TS tests by the kind of their first differing line after stage 1 (`./displaydiff.py --fine`): underlines/order 105
(display, not this plan: 45 of them are the `import()` +6 underline, `bugs/display.md`), **we-any/declaration 95**,
line-missing 73, shape-other/declaration 58, union-order 57 (display), signature-differs/declaration 48,
object-differs/declaration 46, union-differs/declaration 44. Mechanisms seen in them, roughly by size:
1. **Class property types inferred from the constructor / static block** under noImplicitAny (`x;` or `accessor x;`
   assigned in the constructor: tsc 4.0's getWidenedTypeForAssignmentDeclaration for class members):
   classPropInitializationInferenceWithElementAccess, classAttributeInferenceTemplate, classStaticBlockUseBeforeDef1
   (2 configs), classStaticBlockUseBeforeDef4, controlFlowAutoAccessor1, classAccessorInitializationInference*. A real
   feature, ~7 tests; check it on the corpora (a wrong `any` there hides errors).
2. **Tagged template calls** resolve like calls (overloads, return type): taggedTemplateStringsWithOverloadResolution2
   (+ES6), taggedTemplateStringsWithManyCallAndMemberExpressions (+ES6), importHelpers, taggedTemplateWithoutDeclaredHelper
   (`TemplateStringsArray`), parenthesizedContexualTyping3. ~7 tests.
3. **Out-of-range tuple index / empty-object index** is `undefined` (TS2493 recovery): bestCommonTypeOfTuple(2),
   castingTuple, tupleLengthCheck, noImplicitAnyIndexing(Suppressed), augmentedTypeBracketAccessIndexSignature. ~7.
4. `export as namespace X` of a module whose X is not a value is errorType (`React : error`): jsxNamespaceImplicit*
   (3), reactTransitiveImportHasValidDeclaration, verbatimModuleSyntaxDeclarationFile. ~5.
5. `new C<T>()` with too few/many type arguments still instantiates (`D<number, unknown>`, `C<number>`):
   constructorInvocationWithTooFewTypeArgs, instantiateGenericClassWithWrongNumberOfTypeArguments,
   callGenericFunctionWithIncorrectNumberOfTypeArguments. ~3.
6. Default import of a CommonJS/`export =` module under allowSyntheticDefaultImports/esModuleInterop (`typeof
   Namespace`): allowSyntheticDefaultImports1/2, esModuleInteropEnablesSyntheticDefaultImports,
   nodeNextImportModeImplicitIndexResolution2; `import type * as types` (implementsClause, extendsClause). ~6.
7. Generators: IIFE generators' yields (generatorTypeCheck27/29/30), `Generator<never...>` vs number
   (generatorTypeCheck58), IterableIterator's TNext (generatorReturnTypeFallback.4).
8. Numeric property names: `1.1` prints `"1.1"` (objectTypeWithNumericProperty) and the reverse (numericIndexingResults);
   duplicate numeric index signatures kept (multipleNumericIndexers); octal keys (octalIntegerLiteral(ES6)).
9. Duplicate overloads/members merged into one signature list where tsc keeps the first (duplicatePropertyNames,
   overloadsWithinClasses).
10. Union/keyof origins and alias names lost (keyofIntersection, doubleUnderscoreMappedTypes, typeInferenceLiteralUnion):
   plan 21 decided "not now" (display.md).
line-missing (73) is mostly parser recovery spans (`>1 + : any` vs `>1 +      : any`: tttt's span of a recovered
expression keeps trailing whitespace) — plan 27's territory; a quick look at how recovered binary expressions end
their span may fix a dozen.

## Validation
`./displaydiff.py` fixed-by-one counts for the worked buckets go down, suite up, no corpus regressions. A fix to an
inference rule that shows up here should be checked on zod and valibot (inference-heavy) for instruction counts too.
