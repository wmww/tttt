# Source sweep checker leftovers (plan 52 stage 2 onward)
What the source sweep fan-outs left, by session. `scripts/sourcesweep.py -f '^REPO/'` re-checks one repo. For a +TS2589,
a gdb breakpoint on `ty_instantiation_limit_reached` in `debug/tttt` (`TTTT_NO_REEXEC=1`, `--checkers 1 --threads 1`)
prints `me->instantiation_depth` / `instantiation_count`: depth 100 is a runaway recursion, count 5M an exponential (or
missing cache) blow-up. (Pruned 2026-10-12: entries for repos that fully match now are gone: date-fns, styled-components,
dokploy, chatbox, angular-components, react-navigation, telegraf, commitlint, astro, react-router, rxjs, angular-cli.)

## Older leftovers (2026-10-08..09), still mismatching
- svelte `deriveds.js:352`, `runtime.js:672` (`unknown` to `T`/`V`), `sources.js:90` (`Value<unknown>` to
  `Source<V>`), `Identifier.js:50` (`name += ...` string to the runes union), `builders.js:376` (`Value` to an
  estree union): JS/JSDoc, not reduced.
- +TS7006 renovate `migration.ts:201` (under @ts-expect-error'd element accesses): contextual typing miss, not reduced.
- tsc's TS2322 message truncation (`PromiseLike<...>` elision in long union-of-signature types) differs from tttt's.
- tanstack-table examples (`spans.reduce((total, span) => ...)` with `spans: unknown` from an unresolved Playwright
  call): tsgo's untyped call gives the callbacks contextual any, tttt TS7006 (a plain `unknown.reduce(...)` probe is
  fine: the difference is in how `spans` becomes unknown).

## Source sweep fan-out 2026-10-12 leftovers (agent A)
Agent A's list (config, module resolution, references, parser, perf) at the end of that session; everything else of it
was fixed (lexical, apollo-server, karakeep, postiz-app, astro, hyper, commitlint, jest, mcp-sdk, radix-primitives match; second round: react-router, shiki, lit, typedoc match too).
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
- **redux-toolkit query/core/module.ts:182/185/205 (+TS2344 x3) and :656, react/module.ts:216 (+TS2322)**, all
  `Definitions[EndpointName] & { type: "query" }` against `QueryDefinition<any, ...>`. tsc accepts it because its
  getReducedType looks through type variables: getPropertiesOfUnionOrIntersectionType takes each member's apparent
  type, so `D & { type: "query" }` with D's constraint a union of definitions whose `type` is `DefinitionType.x`
  (an enum literal, disjoint from the string "query") is never. Self-contained reduction (tsc: only T5 errors; tttt
  both): `enum DT { query = "query", mutation = "mutation" }`, `QD = Base & { type: DT.query }`, `MD = Base & { type:
  DT.mutation }`, `type T2<D extends QD | MD> = Q<D & { type: "query" }>` with `Q<D extends QD>`. A patch that does
  this in never_reduced_prop() (checker.c: a type-variable member contributes its apparent type when that's a known
  object or a union of them, compared lazily) fixed every probe and changed no oracle, but cost +0.3% instructions
  (zod, valibot, effect) and did NOT fix the real file, where the result is order-dependent: with it, module.ts alone matches
  tsc, with src/query/react/namedHooks.ts checked first it doesn't (a backtrace showed the apparent type of
  `Definitions[EndpointName]` coming back unknown during an indexed-access check in another file). Not committed;
  the patch (described above) was left in /tmp/a-probes/keep/red_lazy.patch, which may be gone. Next: find what
  namedHooks.ts's checking caches.
- **knip language-server/src/server.js:277 (-TS7022 on `this.fileCache = undefined`)**: a JS this-property whose
  type comes from `this.fileCache = { filePath: relPath, file }`, `file` from `this.session.describeFile(...)` narrowed
  across `if (this.fileCache?.filePath === relPath)` in the same method: tsc meets its own declared type through that
  flow narrowing, tttt doesn't. Not reduced.
- **react-navigation +TS2339 `in`** (the entry above, unchanged).
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
  probe but makes the suite runaway-recurse (segfault). Plan 61 (plans/61_constraint_functions.md) covers it.
- mikro-orm/tests +TS2615 x3 (`toJSON` circularly references itself in a mapped type): reproduces in a project
  extending `mikro-orm/tests/tsconfig.json` with only `import type { Loaded } from '@mikro-orm/core'; import type { Book }
  from '<abs>/tests/entities/index.js'; declare function createEntity<T>(): T; createEntity<Loaded<Book>>().toObject(['id']);`
  (BaseEntity's `toObject<Entity extends this, Ignored extends EntityKey<Entity>>(ignoreFields: Ignored[])`, EntityDTO's
  `as`-clause mapped types with CleanKeys). Not reduced further. +TS2769 EntityManager.mongo.test.ts:1268 and -TS5088
  x9 / -TS2883 x2 (declaration diagnostics): not looked at.
- better-auth +TS2339 `signUpEmail` on `InferAPI<Prettify<{ [x: string]: any }>>` (get-migration-schema.test.ts:293
  and 15 sites in other test files once that file is the only root): reproduces with a tsconfig extending the
  package's (composite/incremental/emitDeclarationOnly turned off, its references by absolute path) whose only file is
  get-migration-schema.test.ts; a hand-written `declare const auth: Auth<BetterAuthOptions>; auth.api.signUpEmail` or
  `betterAuth(config)` probe doesn't. tsctypeat: tsc has `auth: Auth<BetterAuthOptions>`, `config: BetterAuthOptions`.
  A copy of the file outside the package (modules unresolved) shows another divergence: tsc infers `Auth<any>` (api
  any) from the error-typed config, tttt `Auth<BetterAuthOptions>` (api `FilteredAPI<Record<string, Endpoint>>`) and
  reports TS2722 there. Not reduced further. lynx-store.ts:65 -TS2345 x2 not looked at.
- apollo-client docs `@types/react/index.d.ts:4146` +TS2694 `React.React` / -TS2749 (`React.Component` in `declare
  global { namespace JSX }`): near agent A's b5fcc5fc (TS2749 for value-only members of an `export =` namespace), not
  looked at further.
- sequelize model-definition.ts:730 +TS2578: tsc's TS2367 (`key === 'column'` with `keyof IndexOptions`) that the
  `@ts-expect-error` hides is missing in tttt only in the real project (a copy whose `@sequelize/utils` doesn't resolve
  reports it, so does a probe with IndexOptions alone); connection-options.ts:50 -TS2339: tsc reports TS2339 twice
  there, once on `RawConnectionOptions<Dialect>` and once on its constraint `string | (object & { url?: string })`
  (the binding pattern's contextual type makes the initializer's `options` substitute constraints); tttt only the first.
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

## Source sweep fan-out 2026-10-12 leftovers (agent C)
Fixed this round: arktype's ark/docs (indexed-access `T & P` reduction, union base classes), react-hook-form's
`__typetest__`, rxjs (TS2314 typeof import, `[...Mapped]`), nuxt/pinia (TS2310), openapi-typescript (cross-file
default exports), inbox-zero (namespace suggestions), tiptap `this.parent`, the perf fix for react. Left:
- **pixijs** `@webgpu/types` (+TS2451/-TS2709 x5): TS 7's lib.dom declares `namespace GPUBufferUsage` (and friends),
  `@webgpu/types` declares `interface GPUBufferUsage` + `declare var GPUBufferUsage`, pixijs `declare const`. tsc merges
  cross-file per *file symbol*: webgpu's file-local symbol (interface + var) conflicts as a whole with the lib namespace
  (TS2300 on both webgpu declarations, the interface isn't merged), so webgpu's `: GPUBufferUsage` resolves to the
  namespace (TS2709) and pixijs's const conflicts with the namespace (TS2451; lookupOrIssueError keeps the first
  diagnostic at a node). tttt's binder merges declaration by declaration (declare_symbol): the interface merges into
  the lib symbol, only the var is rejected. Architectural (the binder's cross-file merge unit); rare (conflicts only).
- **soybean-admin** root: +TS2503 x17 `Cannot find namespace 'JSX'` in naive-ui's .d.ts (`() => JSX.Element`), no
  skipLibCheck. tsc's full program doesn't report them, but a probe project with only Close.d.ts (types []) does, and
  no file in tsc's program declares a global JSX that I could find. tttt's program has one file more:
  `vue/jsx-runtime/index.d.ts` (the jsxImportSource implicit import; tsc's list doesn't have it). Lead: how tsc
  resolves `JSX` there (maybe through the jsx-runtime/jsxImportSource namespace for a file without JSX). Also
  -TS2502 in bumpp's index.d.mts:331 (2 projects), not looked at.
- **tiptap** demos +TS2345 x9 `VueNodeViewRenderer.test.ts:58,69,122...`: `ReturnType<typeof defineComponent>`
  (vue's overloaded defineComponent). tsc: `typeof defineComponent extends (...args: any) => infer R ? R : 2` is 2
  (definitely false), tttt defers it; underneath, tttt finds `DefineSetupFnComponent<any, any, any, any, PublicProps>`
  assignable to `DefineComponent<unknown, ...>` (an intersection of a constructor type, ComponentOptionsBase and
  PublicProps) where tsc doesn't. Probe: /tmp-style project importing `defineComponent, DefineSetupFnComponent,
  PublicProps` from tiptap's vue `dist/vue.d.mts` by absolute path; a hand-written two-overload copy doesn't reproduce.
- **arktype** root (not compared in the sweep before this round: it now is, 4 left): fn.test.ts:286 +TS2769,
  this.test.ts:190 +TS2322 (exactOptionalPropertyTypes, a morph `(child) => child` vs `Morph<{ child?: ... }>`),
  type.test.ts:138 +TS2740 / :140 +TS2769. Not reduced.
- For agent D (mermaid is theirs now): mermaid's `renderHelpers.ts:150` +TS18048 (`insertNode`'s inferred return type
  keeps `undefined`) only reproduces in the full project with one checker: a probe with only renderHelpers.ts, or a
  file importing insertNode, matches tsc, and the files before it in program order don't reproduce it alone either.
  Order-dependent; a delta-debugging bisect over the 765 project files was started and stopped.

### Agent D, second round (agent C's tail list)
- **mermaid** (+TS18048 x7 `nodeEl`/`newEl`, +TS2345, +TS7053 x2): all downstream of `node.shape` being `any` in the
  full project (`ShapeID = keyof typeof shapes`, and `shapes` is built from handlers whose parameters mention
  `Node`, whose `shape` is `ShapeID`). tsc itself resolves `ShapeID` to `string` mid-circularity (the Record has 176
  literal keys), and a probe importing the types alone gets the same as tsc in tttt: order-dependent circularity.
- **tanstack-table preact/react `selection` +TS18046 x3**: JSX overload resolution. Reduction (10 lines):
  `interface Sub { <TS, TSel>(p: { source: SS<TS>; selector: (s: TS) => TSel }): null; <TSel>(p: { selector: (s:
  State) => TSel }): null }` and `<S source={anyValue} selector={(selection) => selection['1']} />`: tsc picks the
  second (non-generic) overload although it has no `source`, tttt the first with TS = unknown. The call form with an
  object literal agrees with tttt (the excess `source` fails the second overload there). Something in tsc's
  checkApplicableSignatureForJsxCallLikeElement (subtype pass and JSX attributes' excess checking) not found yet.
- **tanstack-table table-core `rowModelSlots.test.ts:368` +TS2322**: AggregationFnListItem/AggregationFnRef relation
  over the large inferred feature type; not reduced.
- **react-select `Async.tsx:33`, `AsyncCreatable.tsx:40` +TS2345 (2 projects each)**: `useStateManager(useAsync(props))`
  infers Option as `Option | (Option & undefined) | (Option & MultiValue<Option>) | ...`: inference to
  `StateManagerProps<O, M, G> & AdditionalProps` (a naked type parameter in an intersection target; tsc's
  inferFromMatchingTypes for intersections) from useAsync's Omit/Pick result. A PropsValue-only probe infers O like tsc.
- **dexie samples/dexie-cloud-todo-app +TS2502 `todoLists` (and TS2615 changeOwner)**: `todoLists!: DexieCloudTable<
  TodoList, 'id'>` where InsertType maps over TodoList's keys asking which are functions, and TodoList's methods' inferred
  return types read `db.todoLists`: tttt evaluates that during the property declaration's check, tsc lazily.
- **reselect +TS2536**: fixed by agent C's dc0ff92a (the same base-constraint change I had made; mine dropped).
- tsx: matched at the start of the round. unocss -TS5115 is agent A's; its lib.dom TS2344 is the lib-checking triage entry.
