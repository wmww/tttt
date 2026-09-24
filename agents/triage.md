# Triage
Uninvestigated items. Triage sessions drain this. Last drained 2026-09-22 (triage after plan 10): items now owned by
plans 11-15 or `bugs/` are gone from here; see those files.

## Category-sized (worth a plan or a whole session)
- **JS files: JSDoc types, CommonJS**. Since the triage after plan 06 `.js` files load only with `allowJs`, semantic
  diagnostics in them only with `checkJs`/`@ts-check` (`SourceFile.unchecked`, `error_ctx_remove_unchecked`), so ~900
  `@allowJs` tests now mostly fail *quietly*: they need JSDoc types (`@param`, `@type`, `@typedef`), `module.exports`
  / `require` as declarations, expando assignments (`f.x = 1`, `this.x = 1`, prototype assignments), TS2306 on
  CommonJS files that aren't modules. Needs its own plan; JSDoc is big. `check_access()` still lets JS files through.
- **Syntax gaps after plan 14** (done 2026-09-22, plan deleted: 746 tests still have a spurious syntax error, from
  1242): JSX (whole feature, needs its own plan: ~100 tests in `failstats.py --syntax`, more behind `@jsx`), recovery
  fidelity shapes (`''`, `'};'`, `'{'`, `'];'`), among them parameter-list recovery after `a = yield =>` (tsc: "','
  expected" and the function survives; tttt loses the function: FunctionDeclaration10_es6 & ~27 more) and malformed
  import types (nodeModulesImport*DeclarationEmitErrors). Plan 14 leftovers: the import attributes' type against the
  global `ImportAttributes` (TS2322), `new.target`'s symbol in anonymous functions (`(Anonymous function)`) and an
  error type as a computed key (should act like any: invalidNewTarget types), TS2339/TS2488 on destructuring `null`
  (`const {a} = null`, not only `using`), TS1432 top-level `for await`. Older: TS1161 for an unterminated regex
  (`parse_regex` just fails), TS1263/1264 misplaced `let x!`, `const c;` in ambient files, `override`/TS4112+,
  overloads of `*()`-style nameless members, TS2303 "Circular definition of import alias".
- **Parser recovery fidelity now matters more**: the harness typechecks next to parse errors like tsc (since the
  triage after plan 06). parserGreaterThanTokenAmbiguity12-19 (`1 >> = 2` parses as an assignment and gets TS2364)
  and restParamModifier (TS1014 on top of tsc's error) regressed honestly. tsc's baseline for a file with syntax
  errors includes its type errors, so recovery has to produce the same AST shape.
- **Substitution types** don't exist: in the true branch of `T[K] extends unknown[] ? (...args: T[K]) => void : never`
  tsc knows `T[K]` is an array (getSubstitutionType / getImpliedConstraint); tttt reports TS2370 there (@types/node
  events.d.ts x4 in the corpus, hidden by skipLibCheck since plan 10) and presumably mis-relates such types
  elsewhere. Category-sized-ish: needs a Ty kind or a flag on indexed/type-param references made in a conditional's
  true branch.
- `this` leftovers (plan 10 done, deleted): TS2526 for `this` in static property initializers (`static y =
  <this>undefined`) and a few other contexts (thisTypeErrors, 7 missing); TS7041 (`this` captured by a top-level arrow);
  TS7017 (`typeof globalThis` has no index signature); TS2417 (class static side); `ThisType<T>` arguments aren't
  instantiated with the inference in progress (Vue-style `defineComponent` options); awaitUnionPromise lost a types
  component to union order when the `this` type parameters moved type ids (union printing order follows ids).
- **Target-gated diagnostics** now possible (`compiler_options()->target` exists): TS2705/TS2468 (async needs
  `Promise`), TS1375/TS1378 (top-level await), TS2802 (iteration needs `downlevelIteration`), TS1056 (accessors, ES3).
  TS18028 is in plan 13, the "Cannot find name" flavours (TS2583/TS2584/TS2580/TS2582/TS2550) in plan 15.

- **Evolving arrays** (tsc's autoArrayType: `let x = []` / `x = []` on an auto-typed `let x;`, then `x.push(v)`,
  `x[i] = v` grow the element type; needs FLOW_ARRAY_MUTATION nodes in the builder). Auto types (`let x;`) landed at
  the end of plan 08, the array half is `any[]` (hacks.md "Narrowing"). controlFlowArrays and friends.

## Checker gaps (each verified still open by probe)
- `super` legality errors are all missing (2026-09-23 failstats): TS2335 35 in 16 tests, TS2660 35 in 11, TS2337 33 in
  9, TS2466 8, TS2336 3. The resolve walk now decides where `super` is legal (`Scope.no_super`, the super-call check
  in `expr_lookup_names`), reporting there is cheap. Checking `super(...)` against the base constructor is the other
  half (hacks.md).
- `compoundExponentiationAssignmentLHSIsValue`: `x **= v` with a non-reference LHS reports TS2362 where tsc says TS2364
  only (and `**=` has parse gaps there). Spotted by plan 13, not investigated.
- `expandoFunctionSymbolProperty`: `f[symb] = ...` on a function declaration isn't an expando property (only `f.x =`
  is?), so `f` misses `[symb]` (TS2741). Found by plan 09.
- `silentNeverPropagation` (lost to plan 10 stage 2 when object literal methods became context sensitive, like in
  tsc): `convert(createModule({ a: 12 }, { foo() { return true } }))` infers the inner TActions from the return type
  context `ModuleWithState<{a}> & {foo(): boolean}` and fixes it before the method's own candidate is seen. tsc only
  fixes what a contextual *signature's parameters* mention (the method has none).
- **Context-free type of return-sensitive functions** (lost `badInferenceLowerPriorityThanGoodInference` in plan 11
  stage 0, when `() => ({ a: 1, b: x => {} })` became context sensitive like in tsc): tsc's first round types such an
  argument (no sensitive parameters, sensitive through its returned expression) as a return-only signature whose
  return type is checked with `SkipContextSensitive` (nested sensitive functions are non-inferrable `anyFunctionType`,
  nothing memoised: `links.contextFreeType` in checkFunctionExpressionOrObjectLiteralMethod), and infers from that.
  tttt skips the whole argument in pass 1, so `a`'s candidate arrives only in pass 2, after `b`'s `x` is typed with
  `unknown`. Needs a "skip context sensitive" check mode (no memos, sensitive functions -> a non-inferrable marker).
- Non-strict display: `get(k: string): T | undefined` prints `(k: string) => T` in the types baseline without
  strictNullChecks; tsc keeps the written `| undefined` (probe: any interface method returning `T | undefined`).
- Late bound index signatures (TS 5.x): `[k]: T` in a class/interface with an entity-name key of type string/number/
  symbol is an index signature in tsc (getIndexInfosOfIndexSymbol); tttt drops the member (hacks.md "Computed member
  names"). overrideLateBindableIndexSignature1 and friends.
- An array literal that is a destructuring assignment target (`[a, b] = m`) types as an array, tsc makes it a tuple of
  the targets' types (checkArrayLiteral inDestructuringPattern); a binding pattern isn't the contextual type of its
  initializer (`var [, , [, b]] = [3, 5, [0, 1]]` gets TS2461 at ES5, declarationEmitDestructuringArrayPattern5).
  Same root (tsc's getTypeFromBindingPattern + checkObjectLiteral's contextualTypeHasPattern): an object literal
  initializer of a pattern gets the pattern's defaulted properties as optional ones (`function f({ x, y = 0 } = { x: 0 })`
  takes `{ x: number; y?: number }`; `literalTypes2` f6 prints `{ c1?: false; ... }`), and pattern properties the
  literal lacks are TS2525. Since plan 12's excess property checks, `f({ x: 1, y: 1 })` is a spurious TS2353
  (lost destructuringWithLiteralInitializers).
- Exposed by plan 12's excess property checks (each a spurious TS2353 over a pre-existing wrong type):
  mappedTypeMultiInference (`K` of `...cssSets: { [P in K]?: Style }[]` inferred from the first rest argument only),
  typeParameterConstModifiersWithIntersection (`const TConfig extends Config<T1>` inferred as its constraint instead of
  the literal, through an intersection parameter `{ produceThing: T1 } & TConfig`), mappedTypeNotMistakenlyHomomorphic
  (`keyof Gen<ABC.A>` misses `a`: the intersection with the conflicting-discriminant member isn't reduced before
  `keyof`), contextualTypeShouldBeLiteral (hacks.md: contextual types of union members are reduced, `string | "done"` is
  `string`, so the discriminant `value` widens and the wrong member is picked), bigintPropertyName (`{ 2n: x }` is
  named `2n`, tsc names it `2`), genericMappedTypeAsClause (now: `{ bFoo: 'bar' }` is TS2741 where tsc elaborates to
  TS2322 on the property, plan 16; `{ a, b }` against the generic `MappedModel<T>` is TS2322 where tsc says TS2353, the
  excess check doesn't look at the resolved members of a generic mapped type; TS2312 missing).
- Probe leftovers of plan 12 (`test/probes/plan12/`): semver.ts prints the parameter as `"<" | "<=" | "="` where tsc
  reuses the annotation `Comparator["operator"]` (indexed access annotation not reused by the types printer);
  enum_messages.ts prints `(p: E)` for `p: E.x` (same, annotation reuse of an enum member reference).
- Spurious TS7022 on `const can = composite && o?.e && !out` when the type of `e` mentions an unresolved name (`Map`
  without lib es2015): some circularity through error types. Probe: test/probes/plan08/alias_optional_chain.ts minus
  its `@target`.
- Intersection reduction of conflicting discriminants (`has_conflicting_discriminants`) runs at construction and
  only for non-generic object members; tsc does it lazily in getReducedType, so a generic intersection instantiated
  later isn't re-checked.
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
- Optional chaining has no semantics beyond stripping null/undefined from the receiver (`a?.b`, `a?.[i]`, `f?.()`):
  the result doesn't get `| undefined`, and the rest of the chain isn't short-circuited.
- `check_with()` (ty/expr.c) computes an expression with a contextual type without going through the LazyType, so it
  has no cycle detection (`let [x = x] = []` used to recurse forever; `lower_binding_access` avoids it, other
  contextual self-references can still overflow the stack).
- `super.x` is `any`: `super` has no target symbol and no type (`super.throw()` isn't a never call).
- Union *origins* aren't tracked: tsc remembers the aliases a union was written with and narrowing/filtering keeps
  them (`FooBase[]` where we print `string | false`: spreadBooleanRespectsFreshness, literalFreshnessPropagationOnNarrowing).
  Plan 12 gets the literals right but not this.
- Found by plan 12's research: `[F.a, fa]` prints `(F)[]` instead of `F[]`; `type AB = "a" | "b"` declared before
  `r ? "b" : "a"` prints `"b" | "a"` (tsc `"a" | "b"`: alias bodies not resolved in statement order?); a getter-only
  object literal property isn't `readonly`; auto-typed `let x;` (type from later assignments) isn't implemented.
- Type predicates: TS1228 (predicate in a constructor type, accessor, parameter type), TS2775/TS2776 (assertion
  callee without explicit types), TS1225 (no such parameter), `x is T` where the
  function returns a non-boolean (TS2322 on return).
- Interface method overloads print per declaration in the types/symbols baselines (`getFalseType(fresh?: boolean):
  number; getFalseType(): number;` shows `(fresh?: boolean) => number` and `() => number` on the two lines, tsc the
  merged `{ (...): number; (): number; }` with both declarations; probe `test/probes/plan08/contextual_overloads.ts`).
- The symbols of mapped-type properties (`Mutable<A>`'s `flags`: tsc `Symbol(flags, Decl(...))`, we `Symbol(A.flags)`)
  and of union properties (tsc lists every member's declaration, we print `Symbol(flags)`); probe
  `test/probes/plan08/mutable_union.ts`.
- T_1-style renaming of clashing type parameter names is missing in display: overload lists (`{ <A>(...): ...; <A_1>(...):
  ... }`) and a generic signature shown inside a scope that has a type parameter of the same name (`createMCache :
  <T_1>() => MCache<T_1>` inside `function per<T>`: the types components of probes plan11/memoizeOne.ts and
  nestedReturnContext.ts).
- tsc elaborates a lambda argument that doesn't fit at its return expression (TS2322 inside the body,
  genericCallWithGenericSignatureArguments2), we report TS2345 on the whole argument. Same family as the object
  literal elaboration below.
- TS2403 ("Subsequent variable declarations must have the same type") doesn't exist (`lower.c` TODO); visible in
  contextualSignatureInstantiation now that failed calls return `unknown` like tsc.
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
- Found at the end of plan 08, not investigated: TS7005 for `declare var foo;` without a type
  (implicitAnyDeclareVariablesWithoutTypeAndInit); `u += 1` with `u: undefined` is TS18048 in tsc (checkNonNullType on
  `+` operands when neither is string-like), TS2365 here (plan 15's operand checks).
- Types baseline display, found at the end of plan 08: the expression of `class C extends Base<V[]>` prints as
  `typeof Base` (tsc: the instance type `Base<V[]>`); a distributed intersection keeps its origin for display in tsc
  (`LeftHandSideExpression & BindableStaticNameExpression`, tttt prints the distributed union); the second overload of
  a function whose overloads share a type parameter name prints `<T_1 ...>` in tsc.

## Parser
- `parse_label` should refuse reserved words (`default:`/`case x:` are fine today only because of parse order).
- A leading `&`/`|` in a type becomes an `any`/`never` operand (`parse_type`), which every consumer has to know to skip.
- No tokenizer + backtracking with fail cache. Fine for speed in principle (`parse_fail`/
  `hop_start_if_ends_on_word_break` are ~5% of the corpus profile), but parse-error fidelity against tsc is what the
  errors baseline now measures.

## Harness and tools
- `./release/baseline_tests -f enumLiteralsSubtypeReduction -d` hangs (100 s timeout) while the same run without `-d`
  takes 0.3 s: probably the diff of a huge output through a pipe that nobody drains. Seen in plan 12.
- `filetest.sh` always uses `/tmp/testfile`: two agents running it at once collide (a parallel planning agent had to
  use a private copy). Take the directory from `mktemp -d` or an env var.
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

- `tttt` only works when run from the repo root: the lib is looked up as `thirdparty/lib/esnext.d.ts` relative to
  the cwd (`cd corpus && tttt -p .` fails with "File 'thirdparty/lib/esnext.d.ts' not found" and checks nothing,
  exit 0). Resolve it relative to the executable (or an install prefix) like tsc does with its `lib` directory.

- `@ts-expect-error` now reports TS2578 when unused, which exposes checks tttt misses: `promiseTry` (errors
  component lost: `Promise.try(cb, ...args)` with `U extends unknown[]` inferred from both the callback's parameters and
  the rest arguments gets no TS2554 arity error in tttt) and the corpora (valibot 167 TS2578, eg TS2352 on
  `dataset as SuccessDataset<TOutput>` and writes to readonly `schema['~run']`; type-fest 35, eg `EmptyObject`
  assignments, abstract members missing in `class X extends withBar(Bar) {}`). Each is a missing check, `grep -A3
  TS2578 /tmp/realworld.txt` after `./realworld.py --corpus X` lists them.

## Perf (see `measurements.md` for numbers)
- After plan 20, valibot is 1.19 s vs tsgo 0.88 (single-threaded 1.72). What's left is inference (`infer_from` 20%
  self; half of all time under `infer_from_signature_lists` -> `infer_from_returns`, i.e. walking schema objects'
  `~run`/`~standard` members) and `ty_conditional_resolve`'s `infer X` walks. The next lever is tsc's alias shortcut:
  two instantiations of one alias (`InferObjectInput<X>` vs `InferObjectInput<TEntries>`) infer (and relate) by their
  alias type arguments with the alias's variances. tttt only does it for deferred targets (`infer_from`, TODO) since
  there are no alias variances (`get_variances` is for declared types). Covariant-only for all aliases measured
  ~10% on valibot; it needs variances to be correct, and would help relations too: worth a plan.
- type-fest's check (0.6 s) is ~90% `test-d/opaque.ts` alone, which runs into TS2589 at line 68 (`Tagged`/
  `UnwrapTagged` over `TagContainer`) plus four TS7022 that tsc doesn't report: a correctness bug with a perf symptom.
- `./bench.sh`'s "stdlib only" section prints ~0 ms for every phase (seen after plan 17): probably checks nothing.
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
- type-fest `GreaterThan<2, 1>` is `false` (`test-d/greater-than-or-equal.ts:8`, and a probe importing
  `source/greater-than.d.ts`): the nested `[...] extends infer R extends [boolean, ...] ? ... : never` chain (two
  `infer R` in one declaration, the inner shadowing the outer) picks a wrong branch. It used to show as TS2589 in the
  corpus run, before plan 20's cache kept the instantiation count down.
- `for (x in o)` with a non-declaration target: TS2405 ("left-hand side of a 'for...in' statement must be of type
  'string' or 'any'") is missing, tttt reports TS2322 instead (`test/probes/plan14/using_identifier.ts`, line 10).
- Plan 18 leftovers (2026-09-23): stringMappingReduction lost its errors component (it passed while `Lowercase<T>` was
  `any`): `EMap[C]` in the true branch of `C extends Keys ? EMap[C] : ...` needs substitution types (C & Keys).
  mappedTypeAsClauseRelationships misses TS2208 ("This type parameter might need an `extends X` constraint", related
  info on TS2322 against a type parameter's mapped type) and elaborates one line tsc doesn't for `+?` with a remapping
  `as`. mappedTypeAsClauses: union `origin` display (`P & ("a" | "b")`), `keyof { [P in K]: X }` stays deferred (see
  hacks.md). templateLiteralTypes1/3/4: see failstats (TS1125/TS1121 for escapes in template literal types, TS2590 not
  reported, `${symbol}` not rejected). numericStringLiteralTypes: spurious TS2370 on `...values: UnwrapContainers<T>`
  (a homomorphic mapped type over a generic tuple as a rest parameter type).
