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

## Queue (after stage 2)
TS tests by the kind of their first differing line: underlines/order 106 (display), line-missing 72, shape-other/
declaration 59, union-order 56 (display), we-any/declaration 50, signature-differs/declaration 50, union-differs/
declaration 44, our-typeof 42, object-differs/declaration 38. Mechanisms seen, roughly by size:
1. **Non-widening null/undefined** (~25 non-strict tests, 11 fixed by it alone: bestCommonTypeOfTuple(2),
   tupleLengthCheck, castingTuple, indexerWithTuple, noImplicitAnyIndexing...): tsc has undefinedWideningType /
   nullWideningType for the `undefined`/`null` expressions, `void`, `[]`; only those widen to any. tttt widens every
   null/undefined (`widen_nullable()` HACK, and now inference too). Needs two more intrinsics (non-strict only,
   printing the same) that `widen_nullable` alone widens, union/intersection construction treating both as nullable,
   and the `== ty_intrinsic(TY_UNDEFINED)` comparisons (~47 in ty/) checked. Medium, risky; measure first.
2. **Decorators contextually typed as calls** (esDecorators-contextualTypes, legacyDecorators-contextualTypes: `t`,
   `c : ClassDecoratorContext<...>`): a decorator expression is checked like a call with the decorated thing's
   arguments (tsc's getEffectiveCallArguments for Decorator). A feature, ~2 big tests plus whatever the corpora use.
3. **BigInt literals are doubles** (`Value.bigint`): parseBigInt, localesObjectArgument print rounded values.
4. **Dynamic import of CommonJS under node16+** is `{ default: typeof import(...) }` (nodeModulesDeclarationEmit
   DynamicImportWithPackageExports, nodeModulesSynchronousCallErrors, esModuleIntersectionCrash).
5. JS `Outer.Inner` vs `Inner` naming (typeFromPropertyAssignment13/14/16), `Array<T>` written in JSDoc prints `T[]`.
6. Parser-side line-missing: `interface\nI` ASI (asiPreventsParsingAsInterface01-04), `export * from Aaa` in a namespace,
   `-(3` recovery spans, `for ()` missing expressions.
7. Generators: TNext from the yields' contextual types (generatorReturnTypeFallback.4, needs a parent-context lookup),
   a `yield` in a class static initializer still counts for the generator (generatorTypeCheck58).
8. From stage 1, not done: union/keyof origins and alias names (display.md says not now).

## Validation
`./displaydiff.py` fixed-by-one counts for the worked buckets go down, suite up, no corpus regressions. A fix to an
inference rule that shows up here should be checked on zod and valibot (inference-heavy) for instruction counts too.
