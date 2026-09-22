# Triage
Uninvestigated items. Triage sessions drain this. Everything here was re-checked on 2026-09-21 (triage after plan
06); items fixed by plans 04-06 or verified fixed by probe are gone.

## Category-sized (worth a plan or a whole session)
- **JS files: JSDoc types, CommonJS**. Since the triage after plan 06 `.js` files load only with `allowJs`, semantic
  diagnostics in them only with `checkJs`/`@ts-check` (`SourceFile.unchecked`, `error_ctx_remove_unchecked`), so ~900
  `@allowJs` tests now mostly fail *quietly*: they need JSDoc types (`@param`, `@type`, `@typedef`), `module.exports`
  / `require` as declarations, expando assignments (`f.x = 1`, `this.x = 1`, prototype assignments), TS2306 on
  CommonJS files that aren't modules. Needs its own plan; JSDoc is big. `check_access()` still lets JS files through.
- **Spurious TS2304 for `import`, `export`, `using`, `const`, `module`, `exports`, `with`, `of`** (thousands of
  diagnostics): parser recovery turns keywords it can't place into identifier expressions; `using` declarations and
  the import/export forms below don't parse. `./failstats.py --syntax` has the shapes. Remaining syntax gaps (test
  suite only, the corpus has none): `for await (... of ...)`, `export { a as default } from`, `import { default as
  b }`, `import d, * as ns from`, `export import`, `new.target`, `**=`, shebang line, labeled `continue`/`break`
  target validation, `const c;` in ambient files, JSX (whole feature, needs its own plan), `accessor` fields,
  `override`/TS4112+, overloads of `*()`-style nameless members (error recovery), TS1161 for an unterminated regex
  (`parse_regex` just fails), TS6188/6189 misplaced numeric separators, TS1263/1264 misplaced `let x!`, `export { x
  as "y" }` string export names / `import cjs = require` in `.d.cts` (TS1003/TS1005 in nodeModulesPackagePatternExports*),
  TS2303 "Circular definition of import alias".
- **Parser recovery fidelity now matters more**: the harness typechecks next to parse errors like tsc (since the
  triage after plan 06). parserGreaterThanTokenAmbiguity12-19 (`1 >> = 2` parses as an assignment and gets TS2364)
  and restParamModifier (TS1014 on top of tsc's error) regressed honestly. tsc's baseline for a file with syntax
  errors includes its type errors, so recovery has to produce the same AST shape.
- **Contextual signatures of nested function expressions in generic calls**: `foo("", function () { return x =>
  x.length }, p => undefined)` with `fun: () => (x: T) => U` checks the inner lambda against the *uninstantiated*
  `(x: T) => U` (TS2345 "Type 'string' is not assignable to type 'T'"). tsc instantiates contextual signatures with
  the inference context's nonFixingMapper (instantiateContextualType, ContextFlags.Signature). generatorTypeCheck45/46
  lost their errors component to this once generators were typed (plan 09).
- `expandoFunctionSymbolProperty`: `f[symb] = ...` on a function declaration isn't an expando property (only `f.x =`
  is?), so `f` misses `[symb]` (TS2741). Found by plan 09.
- `asyncYieldStarContextualType`: `promise.then(mapper)` with a generic `mapper` is higher-order inference (plan 11).
- Late bound index signatures (TS 5.x): `[k]: T` in a class/interface with an entity-name key of type string/number/
  symbol is an index signature in tsc (getIndexInfosOfIndexSymbol); tttt drops the member (hacks.md "Computed member
  names"). overrideLateBindableIndexSignature1 and friends.
- An array literal that is a destructuring assignment target (`[a, b] = m`) types as an array, tsc makes it a tuple of
  the targets' types (checkArrayLiteral inDestructuringPattern); a binding pattern isn't the contextual type of its
  initializer (`var [, , [, b]] = [3, 5, [0, 1]]` gets TS2461 at ES5, declarationEmitDestructuringArrayPattern5).
- **Member accessibility is not checked at all**: `private`/`protected` members and constructors are ordinary ones
  (no TS2341 in 36 tests, TS2445 22, TS2673/TS2674 8). `new C()` on a private constructor should be an error whose
  result is `any`: typesWithPrivateConstructor/typesWithProtectedConstructor lost their symbols component when
  `Object` members started resolving (`c.constructor` now finds `Object.constructor`, tsc has `any`), an honest loss.
  Also `this` parameters: no contextual `this` from a contextual signature's `this` (getContextualThisParameterType).
- **Writes to readonly properties aren't checked** (TS2540, see the count in the commit that added this line; readonly
  mapped types only became readonly with plan 08 session 3, so the flags are now there to check).
- Spurious TS7022 on `const can = composite && o?.e && !out` when the type of `e` mentions an unresolved name (`Map`
  without lib es2015): some circularity through error types. Probe: test/probes/plan08/alias_optional_chain.ts minus
  its `@target`.
- **Substitution types** don't exist: in the true branch of `T[K] extends unknown[] ? (...args: T[K]) => void : never`
  tsc knows `T[K]` is an array (getSubstitutionType / getImpliedConstraint); tttt reports TS2370 there (@types/node
  events.d.ts x4 in the corpus) and presumably mis-relates such types elsewhere. Category-sized-ish: needs a Ty kind
  or a flag on indexed/type-param references made in a conditional's true branch.
- **`this` types**: `plans/10_this_types.md`.
- **Target-gated diagnostics** now possible (`compiler_options()->target` exists): TS18028 (private identifiers <
  ES2015), TS2705/TS2468 (async needs `Promise`), TS1375/TS1378 (top-level await), TS2802 (iteration needs
  `downlevelIteration`), TS1056 (accessors, ES3), TS2583/TS2584/TS2580/TS2582/TS2581 "Cannot find name" flavours
  picked by name (`Map`/`Promise` with an old lib: 116; `require`/`process`: 88; `describe`/`it`: 24; `$`; `console`
  /`document`): a table in `identifier_expr_lookup_names`. Check counts with `./failstats.py --code`.

## Checker gaps (each verified still open by probe)
- genericClassExpressionInFunction: `s.genericVar = 12` on an instance of a class extending a generic class expression
  gives "not assignable to type 'V'" (the base class's type parameter is not instantiated), exposed when property
  assignments started being checked (plan 08).
- Enum members aliasing another member's value (`FirstX = A`) should have the *same* literal type (tsc prints
  `SyntaxKind.A`), and the symbols baseline names an enum-member initializer reference `SyntaxKind.A` (probe
  `test/probes/plan08/table1.ts`). Matters for `SyntaxKind.FirstX` comparisons and mapped keys.
- Types baseline: an annotated parameter prints tsc's reused annotation (`ReturnType<typeof mk>["a"]`), we print the
  resolved type; `type R = ReturnType<typeof f>` prints `R` instead of the resolved object (probe
  `returntype_index.ts`). And `[]` in `outputs || []` under strict is `undefined[]` where tsc says `never[]`.
- An array literal contextually typed by an array binding pattern should be a tuple (`let [a = 1, b] = [undefined,
  "s"]` gives `a: string | number`, tsc `number` and `[undefined, string]`); an array literal is never a tuple
  without contextual typing (correctOrderOfPromiseMethod); object rest (`{ a, ...rest }`) gives the whole object
  instead of `getRestType()`.
- Parameter properties (`constructor(public x: number, private q: string)`): the member exists on the instance, but
  the symbols baseline prints `Symbol(q, ...)` instead of `Symbol(P.q, ...)`.
- `type Phantom<T> = {}`: `Phantom<string>` and `Phantom<number>` are one `Ty` printing as `Phantom<T>`, because
  instantiating a node that mentions no type parameter returns the node's type, alias arguments and all.
- tsc prints string-named enum members as `(typeof E)["b c"]`; we print `E.b c`. Members of ambient non-const enums
  without initializer should be computed (no value); `ty_resolve_enum` auto-increments.
- Intersection reduction: conflicting discriminants reduce to `never` since plan 08 (`has_conflicting_discriminants`,
  at construction and only for non-generic object members; tsc does it lazily in getReducedType, so a generic
  intersection instantiated later isn't re-checked). Conflicting privates don't reduce, and TS2339's "The intersection
  'A & B' was reduced to 'never' because..." message doesn't exist (intersectionWithConflictingPrivates).
- Optional chaining has no semantics beyond stripping null/undefined from the receiver (`a?.b`, `a?.[i]`, `f?.()`):
  the result doesn't get `| undefined`, and the rest of the chain isn't short-circuited.
- `check_with()` (ty/expr.c) computes an expression with a contextual type without going through the LazyType, so it
  has no cycle detection (`let [x = x] = []` used to recurse forever; `lower_binding_access` avoids it, other
  contextual self-references can still overflow the stack).
- `super.x` is `any`: `super` has no target symbol and no type (`super.throw()` isn't a never call).
- No fresh/regular literal types (plan 11 sketch): `[x]` with `x: string | false` gives `(string | boolean)[]`
  (spreadBooleanRespectsFreshness); a union filtered to the members of an alias prints `string | false` where tsc
  says `FooBase`.
- Type predicates: TS1228 (predicate in a constructor type, accessor, parameter type), TS2775/TS2776 (assertion
  callee without explicit types), TS1225 (no such parameter), `x is T` where the
  function returns a non-boolean (TS2322 on return).
- Interface method overloads print per declaration in the types/symbols baselines (`getFalseType(fresh?: boolean):
  number; getFalseType(): number;` shows `(fresh?: boolean) => number` and `() => number` on the two lines, tsc the
  merged `{ (...): number; (): number; }` with both declarations; probe `test/probes/plan08/contextual_overloads.ts`).
- The symbols of mapped-type properties (`Mutable<A>`'s `flags`: tsc `Symbol(flags, Decl(...))`, we `Symbol(A.flags)`)
  and of union properties (tsc lists every member's declaration, we print `Symbol(flags)`); probe
  `test/probes/plan08/mutable_union.ts`.
- Bigint literal types (`type A = 1n`) don't parse in type position (TS1005).
- A type parameter's constraint prints uninstantiated in instantiated signatures (`<S extends T>` where tsc prints
  `<S extends number | null>` for `Array<number | null>.filter`); T_1-style renaming of clashing type parameter names
  in overload lists is missing.
- Destructured parameters without a type annotation don't get the type the pattern implies (`({ a = 1 }) => a` is
  `{ a?: number }` in tsc, `any` here), and computed keys in patterns miss their symbol lines (probe
  `binding_pattern_display.ts`).
- `unknown` where tsc infers a type (~20 TS18046 in the corpus; suite: intraExpressionInferences,
  inferFromGenericFunctionReturnTypes2, mappedTypeRecursiveInference, reverseMappedUnionInference, promiseChaining).
- Reverse mapped types (plan 08) are eager objects, no arrays/tuples, cut at depth 4 (`reverse_mapped_type`); tsc's are
  lazy. Contextual types from a generic mapped type substitute the whole constraint for K, not the property's key.
- Permissive instantiation for conditional types is missing (`ty_conditional_resolve` defers where tsc picks the
  false branch); a flat `TyMapper` can't say "every type parameter -> any".
- Circular aliases: `type N<T, K extends string> = T | { [P in K]: N<T, K> }[K]` is `any` + TS2589-ish in tsc
  (recursivelyExpandingUnionNoStackoverflow), here it prints `N<number, "M">`. TS2456 is only a TODO in
  `ty_declared_type_of`.
- A homomorphic mapped type over a `satisfies`/const object doesn't resolve its keys (the `computedOptions` mapped
  type in `utilities.ts` prints with a huge `keyof "a" & keyof "b"` key, ~25 corpus TS2339).
- Module augmentation / `export =` merges don't add members (moduleAugmentationsImports3/4, augmentExportEquals6_1,
  umd-augmentation-3/4, moduleAugmentationDoes*MergeOfReexport). `aliasUsageIn*` (4 tests): the static side of a
  class reached through `import x = require()` vs another import reports a missing property.
- Class expressions print as `''` in messages (`Property 'x' does not exist on type ''`), tsc says `(Anonymous class)`.
- TS2550 instead of TS2339 for members of newer libs (doYouNeedToChangeYourTargetLibrary*: ~80 diagnostics), TS2551.
- Missing TS2339 is ~500 in 230 tests: the gates in `check_access()` (JS, callables, mapped, unresolved bases),
  bracket access (TS7053), and whatever is typed `any` by failure.
- Contextual signature of a type with several call signatures (tsc `getIntersectedSignatures`;
  signatureCombiningRestParameters2). Arrow functions in an object literal whose contextual type is an interface with
  optional/overloaded methods don't get their parameters typed: **852 TS7006 in the corpus**, the biggest category
  (`const checker: TypeChecker = { getFalseType: (fresh?) => ... }`).
- Corpus TS2345 317 / TS2769 103 / TS2322 92: `visitNode(node, visitor, isExpression)` resolves to the overload
  without the predicate (`test/probes/plan05/visitNodeInference.ts`); `CompilerOptions` -> `Pick<CompilerOptions,
  "target" | "module">` (20); literal widening in an object literal that has a contextual type; `unknown` -> `Symbol`.
  TS2349 78, TS2558 38, TS2344 30, TS2364 19, TS2392 13: not looked at. `./realworld.py` for the current numbers.
- A return type annotation that is an error (TS2314) isn't `any`, so TS2355 is reported on top
  (genericRecursiveImplicitConstructorErrors3). tsc's `errorType` is any-like everywhere. A failed type-argument-arity
  call returns `any` where tsc still resolves a signature.
- `lower_identifier_internal` no longer treats a bare `C` inside generic class `C<T>` as `C<T>` (only `this` is);
  tsc reports TS2314 for the bare name, nothing here does.
- Object literal elaboration: tsc reports a mismatching property of an object *literal* at the property (TS2322 +
  related TS6500), we report the whole assignment with a chain (`elaborateError`).
- TS18050 ("The value 'null' cannot be used here", 680 missing in 38 tests) is what tsc says for null/undefined
  operands; `expr.c` operator checks use `type_extends_strict_nulls` + TS2365 instead.
- `unionRelationshipCheckPasses`: `{ foo?: number | undefined }` -> `{ foo?: undefined } | { foo: number }` needs
  tsc's discriminated-union decomposition of the source (`typeRelatedToDiscriminatedType`).
- Optional parameters print as `x?: T` in signatures; tsc prints `x?: T | undefined` under strictNullChecks (the
  annotation is reused and `| undefined` added synthetically: `TyDisplay.reused` can express that now).
- Signatures don't print their `this` parameter (`method(this: C, q: string)` prints `(q: string) => void`; tsc
  prints it). Found by plan 07's probes.
- Union member order: tsc's order is type id order, literal types are made when the type node is first resolved;
  we lower annotations eagerly in a different order (`stringLiteralsWithEqualityChecks03`, `y: "p" | "q" | 1` as
  `1 | "p" | "q"`, `typeof x`'s union built on first use). Worth a failstats pass over diffs that differ only by
  union order.
- `propertyAndAccessorWithSameName`: tsc's symbol merging when a property and accessor clash (TS2300) isn't mirrored.
  `type A = {}` clashing with `interface A` (noSymbolForMergeCrash): the traversal prints the merged declaration's
  type, tsc the alias's own.
- TS2694 on an import type names namespaces inside it by bare name where tsc qualifies.
- A type that prints structurally has no size limit: `typeof globalThis` without its name was 23 MB of error text,
  and the harness then deadlocked writing it to `diff` (`run_diff_subprocess` writes all of stdin before reading).
  tsc truncates (`...`, the 160 character default of `noErrorTruncation`).
- Checking the stdlib itself (`tttt --no-lib --keep-going thirdparty/lib/esnext.full.d.ts`) gave 32 name errors and
  143 type errors at last count; tsc gives none. Cheap regression target for lib-related bugs.
- `error.c` "Error added with invalid span" is ~70 lines per suite run (TS2511 on a lib class, names found in a `_`
  of some lib). `grep build/failstats/stderr.txt`. Also one "Declaration B acquired multiple times".
- Plan 04 stage 1 leftovers: ~35 tests lost their symbols component then: `declare global` in odd places, `export {
  x }` next to an exported `x`, moduleAssignmentCompat1-4 (`Symbol(A)` without a Decl for a non-instantiated
  namespace used as a value). Declaration order in the symbols baseline when an import merges with a function
  (`test/probes/plan04/aliasMergesWithLocalValue.ts`: tsc binds function declarations first).
- Module resolution leftovers (`hacks.md` "Module resolution"): json modules as types (~35 tests), resolution-mode
  attributes (~20), package ids / duplicate packages, `tryLoadInputFileForPath` (8), TS2846 (import of a `.d.ts` by
  name, 7), TS2877/TS2876 (`rewriteRelativeImportExtensions`), Windows paths. `selfReferentialDefaultNoStackOverflow`
  wants TS7022 on `export default { mixins: [DefaultSpinner] }` importing itself.
- Diagnostics without a file that depend on emit paths aren't produced: TS5055 (38 baselines, "would overwrite
  input file"), TS5056, TS6054 (unsupported extension, 7), TS6082/TS5053/TS5069 (outFile/sourceMap/declaration
  combinations), TS5101/TS5107 deprecation warnings under `ignoreDeprecations`. `program_validate_options()` has the
  ones that don't. With a tsconfig tsc puts option diagnostics on the option's span in the json; we skip them.
- TS2688 for a type reference directive / `types` entry that resolves for us but not for tsc (typingsLookup3,
  tripleSlashTypesReferenceWithMissingExports, resolutionModeTripleSlash4): our resolution finds a file tsc rejects.

## Parser
- `parse_label` should refuse reserved words (`default:`/`case x:` are fine today only because of parse order).
- A leading `&`/`|` in a type becomes an `any`/`never` operand (`parse_type`), which every consumer has to know to skip.
- No tokenizer + backtracking with fail cache. Fine for speed in principle (`parse_fail`/
  `hop_start_if_ends_on_word_break` are ~5% of the corpus profile), but parse-error fidelity against tsc is what the
  errors baseline now measures.

## Harness and tools
- `@libFiles` is ignored (`hacks.md`): 99 of its 100 uses are `react.d.ts,lib.d.ts`, so it's worth nothing until JSX
  exists. `@baselineFile`, `@fullEmitPaths` too. Bad option values are ignored (tsc's harness throws).
- `filetest.sh`/diffapp with a multi-config test file: `run_baseline_test_case()` gets an empty configuration there.
  `./filetest.sh` on a multi-file probe shows the comment lines before the first `// @filename` as a file of their
  own in the errors baseline, so the errors component fails although the diagnostics match.
- Harness runs a test twice when the first run leaves net allocations (lib cache fill), ~170 times per suite run;
  "### TEST" markers can repeat, `failstats.py` dedupes. `failstats.py` counts by test *name*; a handful of names
  exist in both `compiler/` and `conformative/`, so its pass counts can differ from the harness by one or two.
- `./release/tttt file.ts` without `-q` prints the parsed AST before typechecking; `display_expr` prints `?` for
  expression types there. Is that dump worth keeping at all? `display.c`'s syntax printer is only needed for debug
  output and written type *names* now: `DISPLAY_TYPE_MAX_DEPTH`, `BriefType` may be dead; `DisplayUnderlineTarget` /
  `underline_display()` is (types underlines are `TyDisplay.underline` since plan 07), delete it.
- `tttt` finds `thirdparty/lib/` relative to the cwd. Should be relative to the executable / an install prefix.
- Nodes aren't cloned any more: collapse `owned_symbol`/`symbol_ref` (`GenericItem`, `InferredType`, mapped keys)
  and `key.expr.owned`/`ref` into one pointer each; drop `LAZY_TYPE_FUNCTION` (`hacks.md`).
- Idea: cache call resolutions per `CallExpr` in the checker (each call resolves twice today); variances for type
  aliases (inference and relations both want them).

## Perf (see `measurements.md` for numbers)
- Corpus profile after plan 05: `is_simple_related` + `check_related` 24% (3.9 M `ty_is_comparable_either_way` from
  discriminant/equality narrowing: tsc has a key property map for unions of 10+ members, `narrow_by_discriminant()`
  asks every member every query), `ty_enum_of_member` -> `ty_declared_type_of` -> `intern_slot` 9% (cache the enum
  type on the member literal), `narrowed_type` 8%, `ty_union_subtype_reduced` 2.5%.
- Name resolution is 77 ms on the corpus (plan 04 wanted 40): cache misses more than work; every identifier walks the
  scope chain (locals + container exports per level, value then type meaning on failure), declare + resolve are two
  walks of the AST. Ideas: one table probe per scope for all meanings, skip empty block scopes, merge the walks.
- `find_prop`'s intersection memo caches whatever depth it was computed at (`depth > 8` gives "not found"), harmless
  so far. `ty_intersection` distributes over non-primitive unions with no size limit (tsc: TS2590 at 100k).
- Peak RSS ~120 bytes per source byte: AST node size / per-node allocations / `DEBUG_ALLOCATOR` (always defined in
  `prelude.h`, even in release) are the suspects. Measure with it off. `program_destroy` is skipped in the CLI
  (release) already; long term, arenas.
- No `perf` on the dev box; callgrind works (`valgrind --tool=callgrind ./release/tttt -q files...`, then
  `callgrind_annotate [--inclusive=yes]`). Worth a `./profile.sh` wrapper if more sessions need it.
