# Plan 19: the corpora to zero (loop, like plan 08)

**Status 2026-09-29 (seventh window's corpus session, todo entry 1)**: tsc 0, valibot 0, react 0, nest 0, type-fest 0,
zod 0, zod-root = tsc's own 55, webpack 6, xstate 19, and two new corpora: **vue** (107 at pinning) and **drizzle** (693
at pinning, after an OOM fix), see their sections. The history of R1-R21 (identity relation, homomorphic mapped
modifiers, const type parameters, skip-sensitive inference, the permissive instantiation, ...) and of the zod and
type-fest items is in git (this file before the corpus sessions) and in the commits.

## The loop
`./realworld.py` (tsc's `src/compiler`, must stay 0) and `./realworld.py --corpus X` for valibot, type-fest, react,
nest, webpack, zod, xstate, drizzle, vue (`thirdparty/ensure-corpora.sh` pins them; all are clean under tsc 5.8). Every diagnostic is a
tttt bug. Rank by root cause, not by code: sample the biggest groups, reduce each to a probe in
`test/probes/plan19/<corpus>/`, fix the tsc rule behind it. Record the counts in the commit message and
`measurements.md`; the suite and the other corpora are the regression checks.

Corpus notes worth keeping:
- `./tsctypeat.js TSCONFIG FILE LINE|* NAME` prints what tsc says an identifier's type is inside the corpus, where a
  reduction doesn't reproduce (webpack's `any`s from JSDoc fixing were found that way).
- Grep `./filetest.sh` output for diffs only with `-d` (`FAST=1 ./filetest.sh f.ts -d`): without it no diff is
  printed and "no `^[-+]` lines" means nothing.
- `./tscinstcount.js TSCONFIG FILE` prints what tsc's TS2589 counter (instantiationCount, reset per checked node)
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
### webpack (9)
Fixed 2026-09-28 (probes in `test/probes/plan19/webpack/`, one commit each): JSDoc `[T?]` optional tuple elements,
overload groups of merged global functions (node's setTimeout before DOM's), parameter defaults removing undefined
(contextual/JSDoc-typed parameters, closures), JSDoc casts as narrowable references, a declaration's `@type` tag
signature, lazy conditional branches (TS2456), `require("m").X` value-only aliases, generic array-like spreads,
conditional-vs-conditional with `infer`, inference from optional parameters, getter `@type`, JSDoc through comments.
Sixth window's corpus session (30 -> 18): unique symbols kept in declaration unions, any/unknown defaults removing
undefined, `{}` initializers with expandos unchecked, keyof `keyof T`, string-index-only element access, the whole base
constraint for TS2731, description backticks, constructor-declared JS members by flow type, `@this` types unchecked, exports through a
`module.exports` alias not read-only.
Left, every one reduced:
- **PackFileCacheStrategy.js 931, 942, 1032, 1047, 1087** (TS2322, LazyFunction and `Promise<PackContainer | Pack>`
  chains): plan 38 stage 1 fixed the reduced probe `contextual_return_default_leak.ts` (tsc's `B` now) and four of
  the nine (IdleFileCachePlugin:121, PackFileCacheStrategy 1239/1367/1438), but not these: another cause, reduce again.
- `@template` on a variable statement (`@template T` + `@type {Record<string, (items: T[]) => X<T>>}`,
  DefaultStatsFactoryPlugin:2733, TS2304 x2): plan 26 "Stage 3 leftovers".
- The module-federation example's TS2322: the arrow's return type comes from its variable's `@type` in tsc (plan 26
  leftovers, "TS6210 related info").
- cleverMerge.js:355 `newFn[DYNAMIC_INFO] = ...`: late-bound expandos (plan 26 leftovers).


### xstate (19)
Pinned 2026-09-28 (sixth window, todo entry 1) as the inference-heavy corpus: xstate core at f210b35 (last commit
before TypeScript 6) with its vitest tests, through our `corpora-locks/xstate.overlay/packages/core/tsconfig.corpus.json`
(lib es2024, src + test). tsc 5.8: 0 in 5.9 s. tttt went 394 -> 61 the same session (three sub-agents + main tree):
unknown-like unions, instantiation expressions of functions, nested generic calls skipped in the first pass, the
target-side isDeeplyNestedType(10) bail-out for conditionals (a TS2589 blowup whose memoized error types poisoned the
test files and made the output checker-count dependent), `extends any`, x! as NonNullable<T>, speculative memos...
Plan 38 stage 1 took it to 43, stage 2 to 19; plan 38 "Stage 3" lists the 19 left. Before stage 1, by cause (sub-agent
reports in the commit messages; repros in `plans/38_prototypes/xstate_*.ts`):
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
- Determinism: 5 diagnostics only at `--checkers 1` (triage).

### drizzle (693 at pinning)
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
so TS2589 x59 and 257 G -> 15 G instructions. Left in type-tests (48 at that point): `Expect<Equal<>>` mismatches
(TS2344 x31: `Omit<any, any>` for select results, `.$dynamic()` query builders inferred as their constraint, pg/with.ts
subquery selections) and TS2345 on set operators / `.where()` of dynamic builders.

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

### vue (107 at pinning)
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
`./realworld.py --corpus zod-root` is zod's root tsconfig (all packages, noUnusedLocals, customConditions). It is
**not clean under tsc**: 55 diagnostics of its own (26 TS2307 for bench deps the workspace doesn't install, 14 TS6133,
8 TS6196, ...), so compare, don't count: `node thirdparty/tsc/lib/tsc.js --noEmit --pretty false -p
thirdparty/corpora/zod` and diff `file:line code` sets with `comm`. tttt's set now equals tsc's. The arktype (bench)
ones were three causes: interfaces extending a type parameter (`DynamicBase<t> extends t`, Members.bases), `const`
type parameters behind conditional/mapped types (ty_is_const_type_variable), and `infer X extends keyof $ & string`
constraints not instantiated (ty_conditional_resolve); execa's `result.stderr` was an instantiation expression in an
alias (`typeof CommonResult<false, O>`) left uninstantiated. Probes in `test/probes/plan19/zod/ark_*.ts` and
`instantiation_expression_in_alias.ts`.

### HACKs the zod fixes left (hacks.md "Inference" and "Mapped types")
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
  `./release/tttt -q <options>` and `node thirdparty/tsc/lib/tsc.js --noEmit <options>` (filetest.sh copies the file
  away, so relative imports break).
- A diagnostic hidden behind `any`/the error type surfaces when its cover is fixed: counts can go up after a real fix
  (expect-type mismatches were all silent until call arity was rechecked; zod's DeepPartial behind the getter fix).
- Estimates by grouping codes were often off by 5x (R2: ~190 estimated, 890 fixed): rank by sampled root cause.
