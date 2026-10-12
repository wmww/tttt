# Source sweep checker leftovers (plan 52 stage 2, agent ss-c, 2026-10-08)
Keys +TS2589, +TS7022/7023 and friends, what was left after the fixes of that session. `scripts/sourcesweep.py -f '^REPO/'`
re-checks one repo; tsc's per-node TS2589 budget needs an instrumented tsgo now (triage.md, plan 54 stage 2:
`tscinstcount.js` went with tsc 6). A gdb breakpoint on `ty_instantiation_limit_reached` in `debug/tttt` (`TTTT_NO_REEXEC=1`, `--checkers 1
--threads 1`) prints `me->instantiation_depth` / `instantiation_count`: depth 100 is a runaway recursion, count 5M an
exponential (or missing cache) blow-up.

## +TS2589: tttt runs into the 5M instantiation count where tsc counts thousands
- date-fns/pkgs/docs `src/db.ts:10` `Typesaurus.Schema<typeof db>`: tsc 13214 instantiations at that node, tttt 5M.
  The stack at the limit is variance measurements nested ~8 deep (measure_variances inside relations inside
  measure_variances), with definitely_related's restrictive instantiations (conditional.c) and distributive
  constraints below. Suspects: variances measured again where tsc's cache hits (an alias's variance being measured
  while it is measured: tsc's `inVarianceComputation` / VarianceFlags.Unmeasurable path), or the restrictive
  instantiation of whole types per definitely_related() call (tsc caches getRestrictiveInstantiation on the type).
- styled-components `packages/styled-components/src/test/overriding.test.tsx:99,127,154`
  (`styled(Child).attrs({...})`): tsc ~7.9k, tttt 5M. Stack: mapped-type member resolution under a relation
  (expand_mapped -> keyof -> ty_mapped_info_instantiate, repeated). Since the active-mapper cache (efbf3b82) it is
  reported 3 times per call (callee, call, tagged template) instead of once.
- Not looked at: dokploy `packages/server/src/lib/auth.ts:513` (4 projects, better-auth types), rxjs
  `multicast.spec.ts:41`, `zip.spec.ts:75`, arktype `ark/type/match.ts:57` (and mikro-orm's copy of arktype in
  node_modules).

## +TS7022 chatbox `packages/chatbox-core/src/application/session/message-tree.ts:323` (`rest`), not investigated
`const { [forkMessageId]: _removed, ...rest } = resultHash ?? {}` inside a loop that assigns `resultHash = ...rest...`.
TTTT_DEBUG_CYCLE shows the pattern `{ [forkMessageId]: _removed, ...rest }` reaching itself (no expression between).
A cut-down with `Record<string, { n: number }> | undefined` for `Session['messageForksHash']` doesn't reproduce: the
real type (zod-inferred Session) matters. Next step: copy message-tree.ts with chatbox's node_modules into a probe
project and cut it down.

## +TS7023 tiptap `packages/core/src/Extendable.test.ts` (11 lines, `addAttributes`)
Reduced (tsc: only TS2339 on `parent`; tttt adds TS7023 on addAttributes):
```ts
interface ExtensionConfig<O = any> { name?: string; addOptions?: (this: { parent: any }) => O; [key: string]: any }
interface NodeConfig<O = any> { name?: string; group?: string; addOptions?: (this: { parent: any }) => O }
declare class Ext<Options = any> {
  extend<EO = Options, EC = ExtensionConfig<EO> | NodeConfig<EO>>(config?: Partial<EC>): Ext<EO>
}
declare const x: Ext;
export const e = x.extend({ addAttributes() { return { ...this.parent?.(), bar: {} } } });
```
The TS7023 comes from Checker.print_probe (missing_property_type() prints the object type the way tsc's eager
TS2339 message would): in the deferred body check `this` is `Partial<{ addAttributes: () => ... }>`, whose method's
return type is in flight, so printing marks it circular. tsc had already resolved that return type during the call
(its `this` there was the uninstantiated contextual type, so `this.parent` was the error type and the return type
any), and prints `() => any`. Likely fix: resolve the method's return type during argument checking like tsc (or make
the print probe skip a return type whose first evaluation is the current deferred check).

## Spelling suggestions run out in a different order (angular-components `task-state-zone-interceptor.ts`, 3 projects)
tsc counts every unresolved name toward its 10 spelling suggestions in *check* order, which is lazy: a method's
parameter types are resolved when a call in the constructor first needs them, so line 55's `HasTaskState` gets its
"Did you mean 'TaskState'?" (TS2552) before line 48's names use up the budget. tttt resolves names in the binder, in
source order (suggestion_count_order() approximates some of tsc's order), so line 55 is TS2304. Architectural; only
worth it if more cases turn up.

## Agent tc leftovers (2026-10-08, triage entry 10)
Fixed this round: mikro-orm `{ ...where }` TS2698 (intersection base constraints in isValidSpreadType), the
query/react-navigation `expectTypeOf` +TS2344 (void-optional rest tuples, arity before instantiation, instantiation
expression defaults, union effects signatures), svelte's JSDoc-typed class field context and destructuring defaults.
solid `component.type-tests.ts:100` already matched at this base. Left:
- svelte `deriveds.js:352`, `runtime.js:672` (`unknown` to `T`/`V`), `sources.js:90` (`Value<unknown>` to
  `Source<V>`), `Identifier.js:50` (`name += ...` string to the runes union), `builders.js:376` (`Value` to an
  estree union): JS/JSDoc, not reduced.
- react-navigation `Link.native.tsx:15` +TS2339 `'in' does not exist on type 'Props<ParamList, RouteName>'` (32
  diags, 3 projects; its +TS2578 unused `@ts-expect-error` follows). Partly reduced (agent tc, second round): with
  useLinkProps.tsx's types and core's `NavigatorScreenParams`/`ParamListForNavigator` imported,
  `function L<PL extends {}>(p: (WithIn<NoInfer<PL>> | { in?: undefined }) & { style?: string }) { p.in }` errors in
  tttt only (`WithIn` = useLinkProps' `LinkPropsWithIn`). tsc's apparent type of the member `WithIn<NoInfer<PL>> & {...}`
  is never (getApparentTypeOfIntersectionType: the members' apparent types, WithIn's base constraint is never), so
  createUnionOrIntersectionProperty skips it; tttt's is `{ style? }` because its apparent type of `WithIn<NoInfer<PL>>`
  is unknown. Underneath: tsc's base constraint of `ForParams<Exclude<PL[keyof PL], undefined>>`
  (`LinkPropsWithInForParams`) is never (C2 = `[Params] extends [NavigatorScreenParams<infer Navigator>] ? ...` is
  deferred, its inferred true type takes Navigator = its implied constraint `{}`, and `ParamListForNavigator<{}>
  extends infer NPL extends {} ? LST<NPL> | WithIn<NPL> : never` then resolves to never), while tttt's keeps
  `{ in: Extract<keyof NestedParamList, string>; ... } | LST<NestedParamList>` with NestedParamList free:
  inferred_true_branch() (relate.c) on the nested conditional doesn't resolve it. A self-contained copy of those
  definitions (stub NavigatorLike/NSP) did not reproduce; the real core types are needed (probe dir layout: tsconfig
  extending react-navigation's with `"types": []`, a p.ts importing by absolute path; /tmp/tc-probes/tools has
  tsctype.js/tscbase.js, node scripts printing tsc's type and getBaseConstraintOfType at a span).
- dokploy's react-hook-form `Control<...>` mismatches: not looked at.

## Source sweep after the TS 7.1 fan-out (agent B, 2026-10-09: 3069 -> 3119 / 3363)
Fixed that session: chai's assert via an export= property (TS2775), discriminant narrowing of `x!.k` with x null,
TS2875's position, union signature return reduction (Promise | PromiseLike `then`), free type parameter limits
(Playwright's Unboxed), TS2862 with a generic union index, `import x = require` resolving module members (mongoose
TS2303), TS2348 for unions with construct signatures only, destructuring typeof globalThis (TS2339). Left, by key:
- +TS2339 tiptap `Extendable.test.ts` (11): `this.parent` in an object literal method passed to `.extend()` of a union
  of classes (`[Extension, Node, Mark].forEach(E => E.create(...).extend({...}))`): the `this` type from a union
  signature's contextual type.
- +TS2339 telegraf `markup.ts:121-135`: `B[] | B[][]` narrowed by `is2D()` and filtered; button's type wrong.
- +TS7006 renovate `migration.ts:201` (under @ts-expect-error'd element accesses), react-hook-form
  `validateField.ts:67`, angular-cli `registry.ts:659` (ajv addKeyword validate), ember runloop: contextual typing
  misses, not reduced.
  `validateField.ts:67`, ember runloop: contextual typing misses, not reduced (renovate and ember matched by
  2026-10-12).
- -TS2307/-TS7006/-TS2339 commitlint (6 projects): `@commitlint/test` through project references + paths: module
  resolution (agent A's area).
- +TS6305 (astro & others, 1445), +TS6059 react-router rootDir, ±TS6310 react-navigation: project reference / config
  handling (agent A's area).
- arktype (+TS2345/2322/2339/7006/2589/2769 across ~60 projects): deep type-level library; nothing reduced yet.
- tsc's TS2322 message truncation (`PromiseLike<...>` elision in long union-of-signature types) differs from tttt's.

## Left by the 2026-10-09 triage's real-world agent (cut off before it reported)
- tanstack-table examples (`spans.reduce((total, span) => ...)` with `spans: unknown` from an unresolved Playwright
  call): tsgo's untyped call gives the callbacks contextual any, tttt TS7006 (a plain `unknown.reduce(...)` probe is
  fine: the difference is in how `spans` becomes unknown).

## Source sweep fan-out 2026-10-12 leftovers (agent A)
Agent A's list (config, module resolution, references, parser, perf) at the end of that session; everything else of it
was fixed (lexical, apollo-server, karakeep, postiz-app, astro, hyper, commitlint, jest, mcp-sdk, radix-primitives match).
- **-TS5115 (12 projects: zod 4's `schemas.d.cts:804` in crawlee/unocss/knip, better-auth `db/to-zod.ts:40`, vitepress
  `postcss/isolateStyles.ts:15`)**: tsgo hits its depth-100 instantiation limit where tttt has no runaway at all. Single
  file reproduction (tsgo reports TS5115 at `Root`, tttt nothing):
  ```ts
  type Diff<T, U> = T extends U ? never : T;
  interface Base<P extends Container | undefined = Container | undefined> { parent: P; }
  interface Container<C extends Node = Node> extends Base { first: C; }
  interface Root extends Container<Selector> { type: "root" }
  interface _Selector<S> extends Container<Diff<Node, S>> { type: "selector" }
  type Selector = _Selector<Selector>;
  export type Node = Root | Selector;
  ```
  tsgo's stack (instrumented build): checkTypeArgumentConstraints (Selector against `C extends Node`) -> relation ->
  isWeakType -> resolveTypeReferenceMembers(_Selector<Selector>) -> resolveObjectTypeMembers instantiates the base
  `Container<Diff<Node, S>>` eagerly -> getConditionalType's isTypeAssignableTo(Root, Selector) -> isWeakType again on the
  same reference whose members are still being resolved -> ... zod's is the same shape (propertiesRelatedTo ->
  resolveTypeReferenceMembers -> base instantiation -> conditional -> relation). tttt never instantiates a reference's
  base types (lazy mapper-chained props, members.c `instantiate_members`), so it can't re-enter. Matching it would mean
  copying tsgo's eager base instantiation (or detecting "this base's instantiation needs this reference's members"):
  plan-sized and arguably copying a tsgo runaway; not done.
- **trpc examples/nuxt -TS2416 in vite 8.3's `dist/node/index.d.ts:1512`** (`MinimalPluginContext$1 extends
  BasicMinimalPluginContext implements MinimalPluginContext`): not looked at; the project mixes several vite/rollup/
  @types/node copies.
- **Checker keys in these repos, not looked at**: react-router `react-router-dev/vite/route-chunks.ts:296` (+TS2322
  `Set<unknown>` to `Set<NodePath<Identifier>>`, 2 projects, 24 diags), typedoc `scripts/generate_site_plugins.js:293`
  (+TS2769), shiki `transformers/render-whitespace.ts:54` (+TS2322 assigning to `line.children` of an
  `Element | Root` union), knip `src/plugins/postcss/index.ts:36` (+TS2345 `string | never[]`) and
  `language-server/src/server.js:277` (-TS7022), lit `reactive-element/src/decorators/query.ts:134-143` (+TS2352,
  +TS2345, +TS7006) and `lit-html/src/private-ssr-support.ts:55` (+TS2653), redux-toolkit `combineSlices.ts:120`
  (+TS2344), `query/core/module.ts:656` (+TS2322), `query/core/buildThunks.ts:384` (+TS2339), react-navigation's +TS2339
  `in` (the entry above).
- **Content mappers with --runExternalCode**: tttt validates `contentMappers` and reports TS18068 without the flag
  (like tsgo), but with it tsgo resolves each mapper package (TS18075) and runs it; tttt does neither.
- Perf: knip/packages/knip/test (3.4 s at the session start), postiz-app (36 s) and tabby (1.5 s) were checking that tsc
  gates (options diagnostics hidden by spurious syntax errors) or load noise; now 0.46 s, 5.4 s and 0.27 s (tsc 0.41,
  25.8, 0.46).

## Source sweep fan-out 2026-10-12 leftovers (agent B)
Fixed that round: mikro-orm 1/24 -> 23/24 (NoInfer inference through a union target's leftover, reverse comparable
check before normalization, intersection typesDefinitelyUnrelated), crawlee (Partial<C> over `C extends X<P>`, vi.fn
reverse mapping of callables), base-ui TS2740 (Pick<T, K | "a"> keys), folo (untyped call args contextual any),
msw/discord.js (`...([] | [A])` spread), apollo QueryManager (SkipGenericFunctions in conditional args), super-productivity (TS2347 on tagged templates). Left:
- mikro-orm/tests +TS2345 x13 and GHx6 +TS2322 x3: `raw()` (`<R = RawQueryFragment & symbol>`) as `$in` inside
  `em.find(Author2, { books: { $in: raw(qb) } })`: its return-type inference sees extra members `readonly unknown[] |
  readonly ExpandScalar<{}>[]`. Reduced (self-contained): `type EP<T> = T extends (infer U)[] ? NonNullable<U> :
  NonNullable<T>; type EQ<T> = T extends object ? { $in?: readonly T[] } : number; type MU<T> = [T] extends [object] ?
  T extends string ? T : { [K in keyof T]: T[K] } : T; declare function g<E extends object>(w: { b?:
  EQ<EP<MU<E>["books"]>> }): void; g({ b: { $in: r() } })` with `declare function r<R = "dflt">(): R`: tsc's
  contextual type for `$in` is `readonly {}[] | readonly NonNullable<E["books"]>[]` (still mentioning E, so the outer
  silent-never mapper makes it non-inferrable: R = "dflt"), tttt's `readonly {}[]`. Cause: base_constraint()'s
  distributive constraint uses the check type's *base* constraint (hacks.md "Constraint functions are
  approximations"); switching that call to the immediate constraint (`distributive_constraint_as(.., true)`) fixes the
  probe but makes the suite runaway-recurse (segfault). Needs the constraint-function rewrite hacks.md describes.
- mikro-orm/tests +TS2615 x3 (`toJSON` circular in a mapped type, EntityFactory.test.ts:52), +TS2769
  EntityManager.mongo.test.ts:1268, -TS5088 x9 / -TS2883 x2 (declaration diagnostics): not looked at.
- better-auth get-migration-schema.test.ts:293 +TS2339 x4: `auth.api` is `InferAPI<Prettify<{ [x: string]: any }>>`
  where tsc keeps `signUpEmail`; `Auth<BetterAuthOptions>.api = InferAPI<ReturnType<typeof router<Options>>["endpoints"]>`
  through `OverrideMerge<typeof endpoints, PluginEndpoint>` (`{ [K in keyof Base as K extends keyof Override ? never :
  K]: Base[K] } & Override`) with a `{ ...baseEndpoints, ...pluginEndpoints }` that spreads a Record<string, Endpoint>.
  Not reduced; better-auth lynx-store.ts:65 -TS2345 x2 not looked at.
- apollo-client: ObservableQuery.ts:2237 +TS2684 (`this` of a WatchQueryOptions method), LocalState.ts:501 +TS2352
  (`DeepOmit<Partial<NoInfer<TVariables>> & {...}>`), +TS2865/+TS2694/-TS2749 in node_modules (ast-types, @types/react
  `React.React`): not looked at.
- sequelize model-hooks.ts:185 +TS2345 (`[...HookParameters<ModelHooks<...>[HookName]>]` to a union of tuples), +TS2578
  model-definition.ts:747, -TS2339 connection-options.ts:50: not looked at.
- msw body-stream.node.test.ts:11 and qwik slot.e2e.tsx:19 -TS2428 (a `declare module 'vitest' { interface
  Matchers<T = any> }` augmentation of an interface vitest re-exports from a chunk): a standalone copy of the
  re-export chain (even the `config.d.CU_b-wJj.js` chunk name) reports TS2428 like tsc; something about the real
  setup (types: vitest/globals?) differs. Not found.
- folo renderer +TS2883 x7: declaration diagnostics tsc doesn't report, hidden until the TS2345s there were fixed
  (tttt's CLI drops them while other errors exist).

## Source sweep fan-out 2026-10-12 leftovers (agent D)
Probes are under /tmp/d-probes (gone after a reboot; the reductions are quoted here).
- **socket.io `broadcast-operator.ts:329` -TS2339 (`args.push`)**: `...args: AllButLast<EventParams<E, Ev>>` with `Ev
  extends EventNamesWithError<E>` (a recursive IfAny/Last/Parameters conditional over `E[K]`). tsc's apparent type of
  `Parameters<E[Ev]>` has no `length` (assignable to `unknown[]`, but no properties: likely a circular base constraint),
  tttt's is `any[] | unknown[]`. Self-contained reduction: the typed-events helpers copied into one file with
  `m<Ev extends EventNamesWithError<E>>(ev: Ev, a: AllButLast<EventParams<E, Ev>>, p: EventParams<E, Ev>) { p.length }`.
  Order-dependent in tsc too: with that method checked first, a sibling `n<Ev extends EventNames<E>>` errors as well.
- **query svelte-query `queryOptions.test-d.ts:90` +TS2554 and `createQueries.svelte.ts:303,321` -TS2345**: tsc's
  result depends on check order (`new QueryClient().fetchQuery(options)` is TS2345 in tsc when checked first in a probe
  project, clean in the real project). Variance/relation caching; not pursued.
- **nuqs e2e/tanstack-router `repro-1501.tsx:16`** (+TS2322/+TS2339 on never): `Route.useSearch({ select: search =>
  search.folder })` inside a component that `export const Route = createFileRoute(...)({ component: Page })` uses:
  a circularity (the route's type needs Page's). Not reduced.
- **graphql-yoga benchmark `server.ts:684` +TS2769**: `handleMaybePromise(() => parse(), r => { if (isResponse(r))
  return { response: r }; let rr = r; return handleMaybePromise(() => it(), () => ({ requestParserResult: rr })) })`
  (overloads with MaybePromise / MaybePromiseLike). tsc infers TOutput `{ response } | { requestParserResult }`, tttt
  the common supertype of its candidates, only `{ requestParserResult }`. Reduced to one file (17 lines, the two
  overloads, `declare function parse(): Promise<P | Response> | P | Response`). How tsc combines the candidates there
  (priority, return-type inference of the nested call) not found yet.
- **nextra-theme-docs `layout-props.test-d.ts:18` +TS2344**: `expectTypeOf<Actual>().toEqualTypeOf<Expected>` (an
  instantiation expression; tsc reports TS2344 there too when it fails). tttt's `z.input<typeof z.strictObject({
  ...LayoutPropsSchema.shape, ... })>` has a top-level `[x: string]: any` index signature (zod v4); not reduced.
- **monkeytype frontend `SearchableAutoSetting.tsx:56/59`**: tanstack-form `<form.Field name={props.key} validators=...>`
  over a zod Config type: tsc's error is at `name` (the @ts-expect-error), tttt's at `validators`. Not reduced.
  storybook `theming/index.d.ts:12687` TS2344 (`ForwardedProps` vs `keyof LibraryManagedAttributes<...>`): not looked at.
- **angularfire test/functions -TS2552 in lib.esnext.intl.d.ts** (and DT react-intl-redux): tsc checks lib files
  without skipLibCheck/skipDefaultLibCheck; @types/node's `/// <reference lib="esnext.intl">` without es2021.intl
  leaves `DateTimeRangeFormatPart` unresolved there. tttt never checks stdlib files (`is_stdlib` skipped in program.c).
  Checking them costs time on every project; only partial lib sets ever produce errors. A policy decision.
- **es-toolkit tests/types `compat.spec-d.ts:34` -TS2344** (flatMapDepth vs lodash) and root `without.spec.ts:6`
  +TS2345: not looked at.
- **svelte** (JS/JSDoc, see the agent tc entry above): not looked at this round.
- Display differences seen on the way: tsc prints `Nullable<Vals<K>>` (`{ [K in keyof T]: T[K] | null } & {}` over a
  mapped type) as `{ [K_1 in keyof Vals<K>]: ... }`, tttt as `Vals<K> extends infer T ? {...} : never`; the vitepress
  TS2345 elaboration prints `Partial<T>[K] & null` where tsc normalizes to `T[K] & null`; isolatedDeclarations on
  `k(c = new Set<string>()) { return c }` is TS9039 in tsc, TS9008 in tttt.
- stripInternal is only applied to statements and class members (not interface/type-literal members, enum members,
  parameter properties), and a trailing `// @internal` on the previous code line isn't seen.
