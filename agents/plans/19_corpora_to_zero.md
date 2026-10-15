# Plan 19: the corpora to zero (loop, like plan 08)

**Status 2026-10-09 (tsgo 7.1 oracle, plan 54)**: spurious / missing against tsgo's own diagnostics: every corpus 0 / 0
but webpack 0 / 5, drizzle 3, vue 4, effect 3 / 0 (measurements.md has the latest). The per-corpus sections below were
written against tsc 6 (4d4f005c) and earlier pins: their counts are history, their mechanisms still apply.
bugs/tsgo_divergences.md has what the tsc 6 era left in the corpus configs.

## Re-pin configs (2026-10-07; `thirdparty/ensure-corpora.sh` and the overlays have the details)
- Pin unchanged: **nest** (`tsconfig.corpus.json` with `strict: false`), **react** (`baseUrl: null`: a JSON `null` in
  compilerOptions unsets baseUrl and the booleans; other string/list options still ignore `null`), **valibot**
  (d4da61a0; tsc 6 skips semantic diagnostics after TS5107 without `--ignoreDeprecations 6.0`), **effect** (v3 head
  e730f59d, TS 5.8.3 upstream; main is Effect 4 on tsgo; the overlay unsets `downlevelIteration` and
  `esModuleInterop: false` with `null`).
- Re-pinned to TS-6-era heads: **webpack** 86d31e06, **xstate** 38dcaffb (overlay = upstream's root tsconfig limited
  to core), **vue** 4ab865a8, **tanstack-router** 30fc108a (react-router's tsconfig now says `types: ["node"]`),
  **excalidraw** 53973c3a, **type-fest** e9f614f1 (peak RSS 2.3 GB on the IntRange tests), **zod** 0b216ef6
  (tsconfig.test.json, no overlay), **typescript-eslint** 70629871 (`-b`), **vscode** 2afa73ea (src/tsconfig.corpus.json
  = upstream's minus the webview service worker; ~10.1k files / 3.41M lines, tsc 6 needs >8 GB heap).

**Status 2026-10-06**: excalidraw and tanstack-router pinned and drained to 0 the same session (see their sections).
**Status 2026-10-05**: effect pinned (6300 -> 63, see its section); vscode pinned (129 -> 0); the rest as below. **Status 2026-09-29 (seventh
window's corpus session, todo entry 1)**: tsc 0, valibot 0, react 0, nest 0, type-fest 0,
zod 0, zod-root = tsc's own 55, webpack 6 (0 since 2026-10-05), xstate 19, and two new corpora pinned and worked down the same day: **vue 0**
(107 at pinning) and **drizzle 8** (693 at pinning, after an OOM fix), see their sections. The history of R1-R21 (identity relation, homomorphic mapped
modifiers, const type parameters, skip-sensitive inference, the permissive instantiation, ...) and of the zod and
type-fest items is in git (this file before the corpus sessions) and in the commits.

## The loop
`scripts/realworld.py` (tsc's `src/compiler`, must stay 0) and `scripts/realworld.py --corpus X` for valibot, type-fest, react,
nest, webpack, zod, xstate, drizzle, vue, effect, vscode, excalidraw, tanstack-router (`thirdparty/ensure-corpora.sh` pins them; all are clean under tsc 5.8). Every diagnostic is a
tttt bug. Rank by root cause, not by code: sample the biggest groups, reduce each to a probe in
`test/probes/plan19/<corpus>/`, fix the tsc rule behind it. Record the counts in the commit message and
`measurements.md`; the suite and the other corpora are the regression checks.

Corpus notes worth keeping:
- `scripts/tsctypeat.sh TSCONFIG FILE LINE|* NAME` prints what tsc says an identifier's type is inside the corpus, where a
  reduction doesn't reproduce (webpack's `any`s from JSDoc fixing were found that way).
- Grep `scripts/filetest.sh` output for diffs only with `-d` (`FAST=1 scripts/filetest.sh f.ts -d`): without it no diff is
  printed and "no `^[-+]` lines" means nothing.
- (Deleted with tsc 6, triage.md: port as an overlay if needed) `scripts/tscinstcount.js TSCONFIG FILE` printed what tsc's TS2589 counter (instantiationCount, reset per checked node)
  reaches per node of a file, and `VARIANCES=1` each variance measurement's cost: the budget a TS2589 runaway in tttt
  should be measured against. A probe project for a corpus: a tsconfig outside it that `extends` the corpus's with
  absolute `baseUrl`/`paths` (plus `"*": ["node_modules/*"]`) and `typeRoots`, `files: [p.ts]`: tttt doesn't type check
  a file whose imports don't resolve.
- expect-type's `toEqualTypeOf` (valibot, zod test-d files) checks tttt's identity relation: a jump there is real.
- nest: nestjs-i18n's shipped `.d.ts` imports `Path`/`PathValue`/`IfAnyOrNever` from a module that doesn't export
  them (hidden by skipLibCheck), so those are error types; a regression there would look like an i18n bug. A clean
  corpus finds only spurious diagnostics: decorator checks (TS1238/TS1241/TS1270) are a suite item.
- webpack: tsc is clean only with ensure-corpora.sh's pins (watchpack 2.5.1, `node_modules/webpack -> ..`); ~70 s
  wall for tttt at the time of pinning, worth a perf look. `/// <reference lib>` works in program files, the harness
  still ignores it (TODO in `program_handle_statement`).
- zod: a pnpm workspace, root devDependencies from `corpora-locks/zod.package.json`; `tsconfig.test.json` (not
  tsconfig.json, whose noUnusedLocals only biome satisfies upstream). The heaviest corpus (9.1 G instructions, 1.3 GB:
  deep-partial.test.ts alone is 745 MB, `bugs/perf_leads.md`).

## What is left
### webpack (0)
Fixed 2026-09-28 (probes in `test/probes/plan19/webpack/`, one commit each): JSDoc `[T?]` optional tuple elements,
overload groups of merged global functions (node's setTimeout before DOM's), parameter defaults removing undefined
(contextual/JSDoc-typed parameters, closures), JSDoc casts as narrowable references, a declaration's `@type` tag
signature, lazy conditional branches (TS2456), `require("m").X` value-only aliases, generic array-like spreads,
conditional-vs-conditional with `infer`, inference from optional parameters, getter `@type`, JSDoc through comments.
Sixth window's corpus session (30 -> 18): unique symbols kept in declaration unions, any/unknown defaults removing
undefined, `{}` initializers with expandos unchecked, keyof `keyof T`, string-index-only element access, the whole base
constraint for TS2731, description backticks, constructor-declared JS members by flow type, `@this` types unchecked, exports through a
`module.exports` alias not read-only.
**Zero since 2026-10-05.** The last six: the five PackFileCacheStrategy.js TS2322 (`this.lazy =
SerializerMiddleware.unMemoizeLazy(this.lazy)`) were infer_to_union inferring the whole source rather than the source
less its matched members to several naked type variables (`Promise<A> | A` to `T | U | Promise<T | U>`); cleverMerge's
`newFn[DYNAMIC_INFO] = ...` was annotated_expando_type looking a late-bound expando up by name instead of by its key's
type. The `@template`-on-a-variable-statement (DefaultStatsFactoryPlugin) and module-federation example diagnostics
listed here before were gone by then (fixed along the way by other work). Plan 26's leftovers still list those
mechanisms in general.


### xstate (0 since 2026-10-05)
Pinned 2026-09-28 (sixth window, todo entry 1) as the inference-heavy corpus: xstate core at f210b35 (last commit
before TypeScript 6) with its vitest tests, through our `corpora-locks/xstate.overlay/packages/core/tsconfig.corpus.json`
(lib es2024, src + test). tsc 5.8: 0 in 5.9 s. tttt went 394 -> 61 the same session (three sub-agents + main tree):
unknown-like unions, instantiation expressions of functions, nested generic calls skipped in the first pass, the
target-side isDeeplyNestedType(10) bail-out for conditionals (a TS2589 blowup whose memoized error types poisoned the
test files and made the output checker-count dependent), `extends any`, x! as NonNullable<T>, speculative memos...
Plan 38 stage 1 took it to 43, stage 2 to 19 (xstate is 0 since). Before stage 1, by cause (sub-agent
reports in the commit messages; the `xstate_*.ts` repros were in plan 38's prototype directory, git history):
- **Plan 38, most of it**: defaults leaking into the second pass's contextual type (setup.types 37-132, TS7006/7031),
  the return mapper applied to the whole parameter type (types 2006-2038), the contextual type instantiated whole
  instead of leaf-wise (`assign({ prop: ({ spawn }) => ... })`, 7 TS7031), fixing through the parameter walk
  (actorLogic x7, predictableExec, system), `params` unknown in provided actions (types 3035/3848), spawnChild's
  `{ id: 'ok1' }` widened (types 870), `spawn('child')` over both logics (types 1662), setup().extend() guards/delays
  (setup.types 2787-2951, not reproduced standalone), dieHard.test.ts, StateMachine.ts:393.
- Elaboration: meta.test 270/434 stops one level high (triage).
- Not investigated: invoke `event.output` unknown x4 / `Snapshot<unknown>` context x3, actor 869, actions
  3951/4152/4181 (DoNotInfer leak?), actions 4224/4272 (GetConcreteByKey), emit 49/52 (TS2353), spawn.ts:132
  (deferred conditional rest parameter).
- Determinism: 5 diagnostics only at `--checkers 1` then; none since (checkerdiff K=2/3/4/7 clean, 2026-10-05).

2026-10-05 (vscode window, agent B), 7 -> 2: types 2634 (expand_mapped's keyed-intersection path skipped any
intersection with TYF_COULD_CONTAIN_TYPE_VARIABLES, which every type literal with a `typeof` has), types 1662
(never-reduced intersections had the same flag misuse, and tsc's isGenericReducibleType was missing: an indexed access
of `(A | B) & { src: TSrc }` stays deferred until TSrc is known), setup.types 2787/2821/2822 (an overload failure's
fallback inference skipped nested generic calls that were already resolved; HACK, hacks.md
`overload_failure_inference`). Left, both investigated, probes in the commit messages' style below:
- types 870: fixed 2026-10-05 (e8 agent D): a generic rest parameter's arguments (`check_rest_arg()`) were checked
  against `SpawnArguments<..., TActor>[i]` uninstantiated, whose constraint widened 'ok1'; now instantiated with the
  return mapper like the other arguments (tsc's instantiateContextualType). Probe `test/probes/plan19/xstate/
  spawn_child/` (corpus-based, `tttt -p` the directory: a standalone stand-in didn't reproduce).
- setup.types 2951: fixed 2026-10-05 (e8 agent D): a round that skips generic functions now relates a literal with a
  skipped nested generic call as its first inference pass typed it (`first_pass_typed_arg()`), and that inference
  doesn't re-check it with the calls instantiated, so a naked `const TConfig` stays at its constraint and the excess
  property fails the candidate like tsc's chooseOverload. Probe `test/probes/plan19/xstate/`. (The call's type in the
  types baseline is still the inferred one, tsc's the constraint: the `overload_failure_inference` hack resolves
  raise() for real in the failure fallback.)

### drizzle (0; 693 at pinning)
Pinned 2026-09-29 (seventh window, todo entry 1): drizzle-orm at 15454dbe, the package's src and type-tests through
`corpora-locks/drizzle.overlay/drizzle-orm/tsconfig.corpus.json` (upstream's type-tests tsconfig minus src/prisma,
which needs a generated @prisma/client); devDependencies at pnpm-lock.yaml's versions (npm's own resolution of the
ranges gave tsc 40 errors). noUncheckedIndexedAccess, noPropertyAccessFromIndexSignature, paths `~/*`. tsc 5.8: 0 in
12.7 s. tttt first ran out of memory (27 GB after 7 minutes): isDeeplyNestedType didn't look through intersections, so
methods returning `this & {...}` grew intersections without bound (probe `drizzle/this_intersection_nesting.ts`).
Then 693 in 115 files: type-tests 265 (TS2344 213 = `Expect<Equal<>>` mismatches, TS2589 67), src 428 (TS2345, TS2339,
TS7053, TS2322, TS2352, TS18048, TS2862...). The heaviest corpus by far: 265 G instructions (perf_leads.md).
type-tests (sub-agent, 2026-09-29): a method's `this` through an alias of a class reference (`.notNull().references()`
lost notNull), a base class's defaults instantiated with the arguments before them (`db.query.users`), contextual
types for arguments of a union-of-tuples rest parameter, and object literal types instantiated by mappers their
members don't mention staying themselves: every generic query-builder step had made equal copies of the tables'
columns literals and everything built on them, 5M instantiations for one `.rightJoin()` (tsc: 37k at most per node),
so TS2589 x59 and 257 G -> 15 G instructions. Then an inferred type argument checked against its constraint with
itself as `this`, and an instantiable type's apparent type with itself as its class constraint's `this`
(`qb.leftJoin(a).leftJoin(b)` on `T extends PgSelect`). Left in type-tests (8, after the src pass's alias fix
below took the 16 of mysql/singlestore select.ts + set-operators.ts, whose `$dynamic()` builders measured
`MySqlSelectBase` invariant through `MySqlSelectPrepare<this>`; `VARIANCES=1 scripts/tscinstcount.js` and
`TTTT_DUMP_VARIANCES=1` compare variance measurements if that comes back):
- mysql/generated-columns.ts 102/122 (TS2345): `drizzle(pool, { schema: { users }, mode })` with an `any` client.
- pg/with.ts 215-230 (6): `WithSubqueryWithSelection` of a `$with(...).as(select ... innerJoin)` selection, the mapped
  `{ [K in ... as ...] }` over the joins tuple.

Both gone by 2026-10-05 (the last 4, pg/with.ts 217/230, were keyof of a concrete tuple kept deferred because the
tuple had TYF_COULD_CONTAIN_TYPE_VARIABLES: only a generic tuple defers, tsc's shouldDeferIndexType). Seen on the way,
not corpus-visible: keyof `{ [K in keyof Tuple as Tuple[K]['table']['_']['name']]: ... }` is string in tttt and
unknown in tsc (the non-element keys such as 'length' name `number['table']`). Probing drizzle quickly: a tsconfig in
/tmp that `extends` the corpus's tsconfig.corpus.json with `"files": [probe.ts]`, importing from the corpus by
absolute path, checks in about a second.

`src/` went 428 -> 14 the same day (probes `test/probes/plan19/drizzle/`, one commit each, with the type-tests agent's
fixes merged in between): keyof/Partial skipping private members, a type assertion in a mutable location keeping
unique symbols (`Table.Symbol.X` keys, the biggest cause: ~180), TS2612 skipping late-bound members, a homomorphic
mapped type over the wildcard being the wildcard (Equal<> deferred, view.ts), array literal tuple context only for
declarations homomorphic over a type parameter, arrays not relating to generic mapped types, base-class defaults
instantiated, the array-method union fallback through interfaces extending Array, valid spread of an indexed access
without base constraint, comparable conditional sources through either branch, unconstrained type parameters
comparable to deferred types, `object` without inferable index, no alias variances for type references; then
(second pass, to 0): a conditional resolving to another alias's conditional keeps that alias (Assume, not
MySqlSelectPrepare: db.ts TS2394), a symbol reading an intersection's string index (select.types.ts TS2344), a
function argument's return type not re-decided while in flight (neon-http TS7023). Seen, not corpus-visible: the
non-generic variant of the last one still reports TS7023 where tsc doesn't (`proxy_recursive_wrap2.ts`, `wrap2`:
the inferred type predicate of `get` evaluates `wrap2(target)` while wrap2's return type is in flight; how tsc's
ordering avoids the cycle wasn't worked out).

### effect (3; 6300 at pinning)
Pinned 2026-10-05 (ninth window, todo entry 2): Effect-TS/effect's v3 branch head e730f59d (Effect 3.22.2, still on
TypeScript 5.8; main is Effect 4 on TS 7), the core `effect` package's src (188k lines), test (95k, through
@effect/vitest's sources) and dtslint tstyche type tests (14k), through `corpora-locks/effect.overlay/packages/effect/
tsconfig.corpus.json` (src + test + dtslint in one program, noEmit, noUnusedLocals off as in dtslint's config).
exactOptionalPropertyTypes, NodeNext, self-references through `node_modules/effect -> packages/effect`. tsc 5.8.3: 0
in 16.8 s, 1.8 GB. The largest library corpus (299k lines; 11.1 G instructions at one checker). What it exercises:
dual APIs (`dual(2, ...)` + overloads), `pipe` with generic functions and generic calls as arguments, generators as
do-notation (`Effect.gen`, `yield*` of YieldWrap), Schema classes (`class A extends Schema.Class<A>("A")({...})`),
variance through `_A: Covariant<A>` structs, tstyche's `expect<>().type.toBe<>()`.

The pinning session took it 6300 -> 63 (probes `test/probes/plan19/effect/`, one commit each): generic calls returning
generic functions instantiated in context (`e.pipe(Effect.provideService(tag, svc))`, 5260 of the 6300), class
bases on a conditional over the class itself (`[Self] extends [never]`: ty_is_generic_object resolved A's members),
deferred interface references with defaults decided syntactically (`type AST = Union | ...` with `Union<M extends AST =
AST>`, TS2456), generic rest parameters: union-of-tuples and tuple-with-variadic rest types (fast-check's
`asyncProperty`, `STM.gen(this, function*)`, `catchTag(...tags: K, f)`) get per-position contextual types,
literals that have room in them, and their context-sensitive functions wait out the skip-sensitive round; array-like
type variables spread into tuples stay variadic; literals with generic functions in them skipped in a candidate's
first check. Also `readonly ಠ_ಠ` (a modifier before a non-ASCII name).

**Determinism**: 7 of the 63 differed between `--checkers 1` and 4 at one point; gone by 2026-10-05 (13 left, K=2/3/4/7
identical modulo quoted text, `scripts/checkerdiff.py`): the inference fixes since removed whatever was order-dependent.

2026-10-05, second pass (63 -> 31, probes in `test/probes/plan19/effect/`, one commit each): a generic call as an
argument infers from the raw parameter type, not one instantiated with the outer call's return mapper (Cause.test 8,
Stream/constructors 4); a conditional's branches and `||`/`??`/`&&`/comma operands get the contextual type as written
(Sink racing/zipping 6); `x[key]` with a non-literal key is a constraint position (vitest internal.ts 6); the base
constraint of a union keeps member intersections that have nothing to replace (LayerMap 402/403); a lone spread of an
`any` argument is any for a generic rest parameter (Schema 944/1004/1005, layer 1161).

Third pass: a generic call argument already resolved (under an earlier overload's full check) isn't skipped by a later
candidate's first inference (tsc's getResolvedSignature cache beats SkipGenericFunctions), so `tap(acquireRelease(...),
(q) => ...)` infers `Queue<A>` from it before typing the callback (stream.ts 5, probe
`overload_generic_call_then_callback.ts`; the earlier attempt, skipping the sensitive arguments instead, was the wrong
model). An inferred type predicate is only asked for while the function's return type isn't known to be other than
boolean (tsc's `!signature.resolvedReturnType || boolean`; a function expression that has a contextual signature has
its return type decided first, which tsc does at its contextual check): relating `dual<A, B>(arity, body)`'s body
against DataFirst typed the returned `core.map(...)` without context (core-effect, metric, fiberRuntime: 4, probe
`generic_arrow_predicate_poisons_return.ts`). The noImplicitAny check of `a[0]` only strips null/undefined from what
may be nullish (tsc's checkNonNullType), so a conditional whose constraint is any stays any instead of `NonNullable<..>`
(Schema 4596/4629: 4, probe `element_access_generic_any_constraint.ts`). A context-sensitive function skipped in the first round is tsc's
anyFunctionType, which doesn't fit a parameter the skipping inference made `never` (fast-check's `letrec` overload on
`T extends Record<string, unknown> ? ... : never`; Schema 9195/9373, probe `skipped_function_never_param.ts`). Inferring from a type parameter whose apparent type isn't an object (a union
constraint) starts over from the apparent type (Micro 2730: `failCause(cause)` with `cause: EB extends MicroCause<E>`,
probe `infer_from_param_with_union_constraint.ts`; its types still differ: tsc prints `cause` as its constraint there,
getNarrowableTypeForReference's substitution with a contextual type that has no generic types). A call resolved while
an outer call infers isn't inferential itself (tsc's chooseOverload has its own argCheckMode): `Struct({ k: isSymbol(v)
? .. : Literal(v) })` checks `v` against Literal's parameter as its narrowed constraint (Schema 4110, probe
`nested_call_not_inferential.ts`). A function first checked under a call's resolution with a contextual type that has
no signature (`zero: Z`) keeps no contextual signature, and sees its own type as context only once the call is resolved
(traversing 444 `Effect.mergeAll(() => 1, ...)`, probe `function_context_none_until_resolved.ts`; hacks.md
KEY_CONTEXT_NONE). The arguments of a variadic rest parameter are checked with the inferring call as the outer
inference (return mapper, return type computed then): `M.tag("A", () => "a")` under `[first: P, ...values: P[], f: Fn]`
returns into `Ret` = `"a" | "b"` (Match 887/888, probe `variadic_rest_function_return_mapper.ts`). Inference from a generic mapped source's template
uses the instantiation's fresh key parameter, the one the index signature relation uses (SchemaUserland 12: `F[P]`
from `Readonly<F>` related to `ReadonlyRecord<string, F[P]>`, probe `generic_mapped_fresh_key_param.ts`).

Fourth pass (2026-10-06, 7 -> 3, probes in `test/probes/plan19/effect/` and `vue/`): a non-sensitive function
argument's contextual type gets the return mapper only at its top level (instantiateInstantiableTypes), and the outer
inference mapper a nested call instantiates its contextual type with checks constraints (getInferredType on the
clone): query.test 51 (`function_argument_return_mapper_top_level.ts`, vue `outer_inference_constraint.ts`). Generic
calls returning functions among a variadic rest's arguments wait for the others' inferences (SkipGenericFunctions):
Effect.tst 736 `catchTag("A", fn(function*(e) {...}))` (`rest_generic_call_waits.ts`). A generic mapped type spread into
a tuple is a variadic element, not a rest array: Schema.ts 9010 (`variadic_generic_mapped_element.ts`). TS2590 reported
by intersect_unions(), with intersection targets' property types made lazily for 3+ parts as tsc defers them:
fiberRuntime 1397 (`union_call_cross_product_too_complex.ts`).

Done (2026-10-15): the 3 TS2578 (dtslint TaggedClass 6, TaggedError 31, TaggedRequest 28, `class _A extends
Schema.TaggedClass<_A>()(...)` where tsc reports TS2310 + TS2506): the base constructor resolution entry, bases
re-entered across a call's resolution barrier, and the tuple-to-tuple number index relation while a base expression is
evaluated (`ttttBaseExpressionTupleIndexCycle`).

### vscode (0; 129 at pinning)
Pinned 2026-10-05 (ninth window, todo entry 5) as the large application: VS Code at 156e400b (2025-03-28, the last
commit on TypeScript 5.8 nightlies), upstream's `src/tsconfig.json` (4.5k files, 1.39M lines incl. monaco.d.ts and
vscode.d.ts; nodenext, experimentalDecorators, noImplicitOverride, useDefineForClassFields off, browser + node +
electron code in one program; extensions are separate projects, left out), npm ci of upstream's lock. tsc 5.8.2: 0
in 38 s. tttt first stopped at 7 parse errors (an arrow function's type parameters committing it, `<T>(x => y)`; a
first parameter that didn't commit followed by something other than `,`/`)`, `(readonly T[]) => {` in a return type),
then 129, 0 the same session (main tree + four sub-agents, one commit per cause, list in git log): `this: void` not
checked (38 TS2684), `readonly` dropped at the start of a tuple element (13), destructuring assignments to `this.x`
in a constructor not initializing the field (16 TS2564), the annotated-property contextual type of `this.p = v`,
an any contextual type not inferring any, closure variables starting at their declared type, flow through async
IIFEs, `a.b!` of an only-nullish reference, `??=` as a read, mapped types over generic mapped types / union-constrained
T, constructor return contextual types, async `void` contextual return types, deferred `T[keyof T & string]`, the
boolean filter of the return mapper, union inference with one member left, union index signatures for an any key,
never-reduced members of union property lookups, optional-chain return inference, `any & X` lowering, for-of
widening, predicate methods not thisless, constructor overloads erased with the class's type parameters. Several
moved effect and xstate too. Scale numbers in measurements.md ("vscode").

### excalidraw (0; 9 at pinning)
Pinned 2026-10-06 (tenth window, todo entry 7) as the JSX-heavy application: excalidraw at d080833f (Dec 2025, the
commit that moved it to TypeScript 5.9.3), 149k lines of React 19 .tsx/.ts (the editor, element/math/common packages,
the collab app, vitest/testing-library tests), through `corpora-locks/excalidraw.overlay/tsconfig.corpus.json`
(upstream's root tsconfig + `lib-dom-5.9-shim.d.ts`: TS 5.9's lib types `TextEncoder.encode()` as
`Uint8Array<ArrayBuffer>`, which excalidraw relies on; the shim's overload merges in first). Yarn workspace: lint,
format and coverage tools dropped (npm's lock of @isaacs/cliui doesn't `npm ci`). tsc 5.8: 0 in 8 s. tttt: 9, drained
by a sub-agent (probes `test/probes/plan19/excalidraw/`): side-effect imports of a `.d.ts` (TS2846 only under
noUncheckedSideEffectImports), contextual types for arguments of a union-of-arrays rest parameter generic in an outer
type parameter, the discriminated-union relation (optional target properties take undefined; intersection sources),
alias variances after a distributed union target fails (`Mutable<E>` to `Mutable<El>`), Pick's modifiers from a
generic modifiers type's apparent type, spread operands reduced first (never-reducing union members). Flag flips
(26 cells with tanstack-router, 5757 tsc diagnostics) found: an eopt write keeping the missing type of a non-optional
property (`r.current = cb`), TS2339 on a union that reduces to never (sloppy), tsc's per-union TS2488 cache order
(triage).

### tanstack-router (0; 4 at pinning)
Pinned 2026-10-06 (same session) as the inference-heavy library: TanStack Router at 837917a4 (Mar 2026, the last
commit before TypeScript 6; its CI checked src with 5.8), react-router's src and tests (mostly `*.test-d.tsx`
expectTypeOf over route trees: path/search/params/loader inference through createRoute()/createFileRoute() chains,
`to` as template-literal unions over the whole tree, Register augmentation) through
`corpora-locks/tanstack-router.overlay/tsconfig.corpus.json`, against router-core's and history's declarations built
by `corpora-locks/tanstack-router.postinstall.sh` (tsc 5.8 `emitDeclarationOnly`), as upstream checks against its
dist: router-core's sources inside react-router's program fail under tsc (react-router augments their interfaces).
Three runtime tests need 5.9's lib.dom (`textContent: string`) and are excluded. tsc 5.8: 0 in 17.5 s (41k lines +
101k of .d.ts; tttt 0.43 s at one checker). Drained by a sub-agent (probes `test/probes/plan19/tanstack-router/`):
optional discriminants with their undefined, a homomorphic mapped type inferring nothing from a source that isn't
reverse mappable (`window.scrollTo = vi.fn()`), inference from identical intersection members, conditional sources against weak intersection members.
Flag flips found the base constraint of a union with an unconstrained type variable (none, tsc's
computeBaseConstraint: NavigateFn's `TFrom extends RoutePaths<...> | string` widens '/posts').

### vue (0; 107 at pinning)
Pinned 2026-09-29 (same session): vue core at b5f85183 (the last commit before TypeScript 6), every package's src and
__tests__ plus packages-private/dts-test, through `corpora-locks/vue.overlay/tsconfig.corpus.json` (the root tsconfig
without composite/isolatedDeclarations/noUnusedLocals; dts-test's built.test-d.ts needs a build, excluded). tsc 5.8: 0
in 11.6 s. Pinning found a parser bug (`return` + line break + `let x`) and a crash (an alias with 17 type arguments).

`packages/` (88 of the 107) went to 0 the same day (probes `test/probes/plan19/vue/`, one commit each): optional
fields' initializers against `T | undefined`, TS2564 through `this[Enum.X] = v`, contextual `any` kept (literals
asserted/annotated any, untyped call arguments: `this` is any), default constructors of `extends Array<T>` taking the
written type arguments, TS2405 by the object's keys, identical union members inferred between (`InjectionKey<T> |
string`), a generic index distributed over an intersection in inference (`LooseRequired<Readonly<P> & ...>`), an
unannotated `this` parameter's contextual this type, variadic rest arguments inferred before the second pass
(puppeteer's `evaluate`), overloads relating a context-sensitive literal in the skip round (ReadableStream), TS2540
through Function's `name`, and the immediate distributive constraint (reactive.ts `toReactive`). Left over from those:
- reduce_intersection_target() doesn't infer between identical members (tsc's inferFromMatchingTypes does:
  `symbol & IC<number>` to `symbol & IC<T>`, probe `infer_identical_members.ts` `d`); porting it made zod's
  recursive-types.test.ts:474 TS2589. Two hacks.md entries (skip-round literals, distributive constraints).

### zod-root (tsc 55, tttt 55: the same set)
`scripts/realworld.py --corpus zod-root` is zod's root tsconfig (all packages, noUnusedLocals, customConditions). It is
**not clean under tsc**: 55 diagnostics of its own (26 TS2307 for bench deps the workspace doesn't install, 14 TS6133,
8 TS6196, ...), so compare, don't count: `thirdparty/typescript/built/tsc --noEmit --pretty false -p
thirdparty/corpora/zod` and diff `file:line code` sets with `comm`. tttt's set now equals tsc's. The arktype (bench)
ones were three causes: interfaces extending a type parameter (`DynamicBase<t> extends t`, Members.bases), `const`
type parameters behind conditional/mapped types (ty_is_const_type_variable), and `infer X extends keyof $ & string`
constraints not instantiated (ty_conditional_resolve); execa's `result.stderr` was an instantiation expression in an
alias (`typeof CommonResult<false, O>`) left uninstantiated. Probes in `test/probes/plan19/zod/ark_*.ts` and
`instantiation_expression_in_alias.ts`.

### HACKs the zod fixes left (hacks.md "Calls and inference" and "Type core")
- Uninferred type parameters stay generic in the second pass's contextual type only where they'd be `unknown`
  (contextual types don't look through type parameters to constraints like getApparentTypeOfContextualType).

### Seen on the way, not corpus-visible now
- `g(() => 1)` with `g<T>(t: T): T` infers `() => number` (tsc `() => 1`: a return literal isn't widened when the
  contextual return type is a type parameter).
- Display: `X<string | number>` for a distributive alias over a union keeps the alias (tsc only aliases a distributed
  union for a new alias symbol, probe `distributed_union_alias.ts`); `P<U>` where tsc prints the indexed access it
  resolves to (`indexed_prop_is_indexed.ts`); a recursive `RD<J>` prints 15 levels deep where tsc stops at 10 with
  `any` (`typefest/rd_2589_min.ts`).
- Padded properties (`a?: never` from widening) get symbol declarations only from the sibling that has the property;
  tsc lists both (`typefest/exact_missing_padding.ts`). Goes away with the missing type.
- Suite: spurious TS7053 where tttt lacks the rule, not the check: index signatures keyed by tagged strings
  (indexSignatures1), `in` narrowing (inKeywordTypeguard, narrowingMutualSubtypes).

## Lessons and tooling
- Reducing a corpus type bug: inline the library's types into one file (`/tmp/x/a.ts`), then delete and rename until
  it flips. A `log_info` of check/extends/result in `ty_conditional`, or of the operands where something turns
  unknown, finds the step fast.
- Multi-file probes against a corpus: put them in `/tmp/x/` with relative imports into `thirdparty/corpora/...`, run
  `./release/tttt -q <options>` and `thirdparty/typescript/built/tsc --noEmit <options>` (filetest.sh copies the file
  away, so relative imports break).
- A diagnostic hidden behind `any`/the error type surfaces when its cover is fixed: counts can go up after a real fix
  (expect-type mismatches were all silent until call arity was rechecked; zod's DeepPartial behind the getter fix).
- Estimates by grouping codes were often off by 5x (R2: ~190 estimated, 890 fixed): rank by sampled root cause.
