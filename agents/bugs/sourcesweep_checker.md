# Source sweep checker leftovers (plan 52 stage 2 onward)
What the source sweep fan-outs left, by session. `scripts/sourcesweep.py -f '^REPO/'` re-checks one repo. For a +TS2589,
a gdb breakpoint on `ty_instantiation_limit_reached` in `debug/tttt` (`TTTT_NO_REEXEC=1`, `--checkers 1 --threads 1`)
prints `me->instantiation_depth` / `instantiation_count`: depth 100 is a runaway recursion, count 5M an exponential (or
missing cache) blow-up. (Pruned 2026-10-12: entries for repos that fully match now are gone: date-fns, styled-components,
dokploy, chatbox, angular-components, react-navigation, telegraf, commitlint, astro, react-router, rxjs, angular-cli.)

## Older leftovers (2026-10-08..09), still mismatching
- tsc's TS2322 message truncation (`PromiseLike<...>` elision in long union-of-signature types) differs from tttt's.

## Source sweep fan-out 2026-10-12 leftovers (agent A)
Agent A's list (config, module resolution, references, parser, perf) at the end of that session; everything else of it
was fixed (lexical, apollo-server, karakeep, postiz-app, astro, hyper, commitlint, jest, mcp-sdk, radix-primitives match; second round: react-router, shiki, lit, typedoc match too).
- **redux-toolkit query/core/module.ts:656, react/module.ts:216 (+TS2322)**: `Api<BaseQuery, EndpointDefinitions,
  ReducerPath, TagTypes, keyof ApiModules<...>>` to `Api<any, EndpointDefinitions, any, any, unique symbol>`. tttt's
  chain ends in `util.getRunningQueryThunk()`: the target's result is `InfiniteQueryActionCreatorResult<never> |
  QueryActionCreatorResult<never>`, i.e. tttt reduced the target's `Definitions[EndpointName] & { type: "query" }`
  (Definitions = EndpointDefinitions, a Record of the definition union whose `type` is a DefinitionType member) to
  never, where the source keeps `EndpointDefinition<any, ...> & { type: "query" }` unreduced. Either tsc doesn't
  reduce that instance (getReducedType is only asked in some places) or it compares the two references by variance
  before reaching it. Not reduced further. (The TS2344 x3 there went with never-reduction through type variables'
  constraints, 2026-10-14.)
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

## Source sweep fan-out 2026-10-14 leftovers (agent A, relations list)
Fixed that round: vencord (TS2589 at the template, intersection any-before-unions, never-reduction against generic
references), svelte x2 (JS default type arguments, hoisted constraint against a union), react-window (peer
dependencies in package ids, landed by agent D). Left:
- base-ui test/performance lists 10 more files than tsc: a second vitest 5.0.2 pnpm copy whose package id (peers
  included) equals the first's: tttt dedups the file but still loads the duplicate's own imports (tsgo's redirect
  doesn't parse them). No diagnostic difference (its CheckboxRoot TS2322s were rest types' union order, fixed
  2026-10-16).
- **arktype** (this.test.ts:190), **redux-toolkit** (module.ts:656, react/module.ts:216:
  see agent A 2026-10-12 above), **monkeytype** SearchableAutoSetting.tsx: not looked at this round.
- Display: `(A | L1 | P1) & V` with `V extends A | L1 | L2` (L1 and L2 sharing a `type` discriminant) prints its
  origin, tsc `(A & V) | (L1 & V)`: tsc's getReducedType drops `P1 & V` through V's apparent type (the
  never_reduced_prop() type-variable member gap of the redux-toolkit entry).

## Source sweep fan-out 2026-10-12 leftovers (agent B)
Fixed that round: mikro-orm 1/24 -> 23/24 (NoInfer inference through a union target's leftover, reverse comparable
check before normalization, intersection typesDefinitelyUnrelated), crawlee (Partial<C> over `C extends X<P>`, vi.fn
reverse mapping of callables), base-ui TS2740 (Pick<T, K | "a"> keys), folo (untyped call args contextual any),
msw/discord.js (`...([] | [A])` spread), apollo QueryManager (SkipGenericFunctions in conditional args), super-productivity (TS2347 on tagged templates). Left:
- better-auth +TS2339 `signUpEmail` on `InferAPI<Prettify<{ [x: string]: any }>>` (get-migration-schema.test.ts:293
  and 15 sites in other test files once that file is the only root): reproduces with a tsconfig extending the
  package's (composite/incremental/emitDeclarationOnly turned off, its references by absolute path) whose only file is
  get-migration-schema.test.ts; a hand-written `declare const auth: Auth<BetterAuthOptions>; auth.api.signUpEmail` or
  `betterAuth(config)` probe doesn't. tsctypeat: tsc has `auth: Auth<BetterAuthOptions>`, `config: BetterAuthOptions`.
  A copy of the file outside the package (modules unresolved) shows another divergence: tsc infers `Auth<any>` (api
  any) from the error-typed config, tttt `Auth<BetterAuthOptions>` (api `FilteredAPI<Record<string, Endpoint>>`) and
  reports TS2722 there. Not reduced further. lynx-store.ts:65 -TS2345 x2 not looked at.
- sequelize model-definition.ts:730 +TS2578: tsc's TS2367 (`key === 'column'` with `keyof IndexOptions`) that the
  `@ts-expect-error` hides is missing in tttt only in the real project (a copy whose `@sequelize/utils` doesn't resolve
  reports it, so does a probe with IndexOptions alone). (data-types.ts:2353 fixed 2026-10-15: object to generic mapped
  type relation.)
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
- **monkeytype frontend `SearchableAutoSetting.tsx:56/59`**: tanstack-form `<form.Field name={props.key} validators=...>`
  over a zod Config type: tsc's error is at `name` (the @ts-expect-error), tttt's at `validators`. Not reduced.
  storybook `theming/index.d.ts:12687` TS2344 (`ForwardedProps` vs `keyof LibraryManagedAttributes<...>`): not looked at.
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
- **tiptap** demos +TS2345 x9 `VueNodeViewRenderer.test.ts:58,69,122...`: `ReturnType<typeof defineComponent>`
  (vue's overloaded defineComponent). tsc: `typeof defineComponent extends (...args: any) => infer R ? R : 2` is 2
  (definitely false), tttt defers it; underneath, tttt finds `DefineSetupFnComponent<any, any, any, any, PublicProps>`
  assignable to `DefineComponent<unknown, ...>` (an intersection of a constructor type, ComponentOptionsBase and
  PublicProps) where tsc doesn't. Probe: /tmp-style project importing `defineComponent, DefineSetupFnComponent,
  PublicProps` from tiptap's vue `dist/vue.d.mts` by absolute path; a hand-written two-overload copy doesn't reproduce.
- **arktype** root: this.test.ts:190 +TS2322 (exactOptionalPropertyTypes, a morph `(child) => child` vs
  `Morph<{ child?: ... }>`). (Fixed 2026-10-16: type.test.ts:138/140, the conditional types' mention walk gave up past
  16 bound type parameters; fn.test.ts:286, inference from a lone readonly rest tuple parameter.)
- For agent D (mermaid is theirs now): mermaid's `renderHelpers.ts:150` +TS18048 (`insertNode`'s inferred return type
  keeps `undefined`) only reproduces in the full project with one checker: a probe with only renderHelpers.ts, or a
  file importing insertNode, matches tsc, and the files before it in program order don't reproduce it alone either.
  Order-dependent; a delta-debugging bisect over the 765 project files was started and stopped.

### Agent D, second round (agent C's tail list)
- **mermaid** (+TS18048 x7 `nodeEl`/`newEl`, +TS2345, +TS7053 x2): all downstream of `node.shape` being `any` in the
  full project (`ShapeID = keyof typeof shapes`, and `shapes` is built from handlers whose parameters mention
  `Node`, whose `shape` is `ShapeID`). tsc itself resolves `ShapeID` to `string` mid-circularity (the Record has 176
  literal keys), and a probe importing the types alone gets the same as tsc in tttt: order-dependent circularity.
- **dexie samples/dexie-cloud-todo-app +TS2502 `todoLists` (and TS2615 changeOwner)**: `todoLists!: DexieCloudTable<
  TodoList, 'id'>` where InsertType maps over TodoList's keys asking which are functions, and TodoList's methods' inferred
  return types read `db.todoLists`: tttt evaluates that during the property declaration's check, tsc lazily.
- **reselect +TS2536**: fixed by agent C's dc0ff92a (the same base-constraint change I had made; mine dropped).
- tsx: matched at the start of the round. unocss -TS5115 is agent A's; its lib.dom TS2344 is the lib-checking triage entry.

## Source sweep round 2026-10-12 (agent B, second task)
Fixed: vitest configs (starlight, feathers, react-scan, react-window: augmentations of a deduplicated package copy),
mermaid (discriminated relation reading target property types the source lacks), noUnusedLocals of a local merged
with an export (graphql-js, renovate), pi (widened object literals normalized), es-toolkit without (never candidates),
soybean-admin (implicit jsx-runtime import only for .tsx/JS), apollo ObservableStream (never-reduction with a
generic type literal member), ember runloop (IIFE parameters inside a call argument), vueuse/scripts (null for
string config options), tsx cache.ts (no flow narrowing of a non-variable module export). Left:
- **monkeytype storybook +TS2307 x8 in @sentry/browser `feedbackAsync.d.ts`**: an unresolvable `import("@sentry-internal/
  feedback/build/npm/types/core/types")` nested in a *type argument of an import type*
  (`import("@sentry/core").IntegrationFn<... & { attachTo(o?: import("...").X) }>`). tsc's checkImportType doesn't
  check type arguments and only resolves them lazily (a type literal's members), so the nested import is never
  resolved; tttt's name-resolution walk reports its TS2307 (import_type_lookup_names passes `errs` into a found
  import type's type arguments). Reduced: three packages under node_modules (pkga's index.d.ts `export declare const
  x: import("pkgc").F<import("pkgc").I & { a(o?: import("pkgb/deep/x").T): void }>`, pkgb with an exports map
  without `./deep/x`).
- **tsx `esm/api/register.ts:151` +TS7022**: two `const unregister`s in one arrow, the first in an `if` block
  (`(async () => {}) as NU`), the second at function level (an expando host, `async () => {}`). tsc's binder, like
  tttt's, binds the first block's `unregister.unregister = unregister` (inside a nested `if`) as an expando member of
  the *outer* const (lookupSymbolForName searches the container's locals). tttt then reports TS7022 on that member;
  tsc doesn't (its member type is computed lazily, and its initializer resolves to the inner const). Reduced:
  `const register = () => { if (cond) { const u = (async () => {}) as NU; if (ns) { u.unregister = u; } return u; }
  const u = async () => {}; if (ns) { u.unregister = u; } return u; }` with `NU = (() => Promise<void>) & {
  unregister: () => Promise<void> }`. Not found which evaluation is circular (TTTT_DEBUG_CYCLE prints nothing: a
  member type cycle).
- **tiptap/demos `VueNodeViewRenderer.test.ts` +TS2345 x9**: `ReturnType<typeof defineComponent>` (overloaded,
  generic) prints as `DefineComponent<unknown, unknown, ...>` in tttt's message and isn't a Component<NodeViewProps>;
  not reduced.
- pixijs/feathers "flakiness" (2026-10-12, agent C): not reproduced. Both are byte-identical over 5 runs at
  `--checkers` 1 and 4 and over repeated `sourcesweep.py -f` runs; the runs that matched were probably of other
  commits. feathers' mismatch (+TS6196/+TS2339/+TS2353 in docs/vite.config.ts: `import type { Plugin } from "vite"`
  of vite's CJS `export = module` (any) found lib.dom's global `Plugin`) is fixed; pixijs is the @webgpu/types merge
  unit above. Run outside sourcesweep.py from a worktree (`tttt -p thirdparty/sources/feathers`), feathers shows ~19
  extra TS2307: the symlinked-sources path issue of perf_leads.md, not flakiness.

## Source sweep checker leftovers of agent ss-b (round 2)
Unreduced or order-dependent findings on the +TS2339/+TS7006/+TS2349/+TS2698 keys, for a future session.

- **+TS2698 `{ ...where }` with `where: FilterQuery<Entity>`** (mikro-orm: 7 in each of ~20 projects, trpc's heyapi
  `client.gen.ts:248` `{ ..._config, ...options }` in 12). tsc's isValidSpreadType takes the base constraint of each
  union member; mikro-orm's `FilterQuery<T> = ObjectQuery<T> | NonNullable<ExpandScalar<Primary<T>>> | ...`. A cut-down
  copy of those aliases (`/tmp` probe sp1.ts in the session) is clean in both, so it depends on the real
  ObjectQuery/Primary. Reduce from `mikro-orm/packages/core/src/EntityManager.ts:287`.
- **react-navigation `example/__typechecks__/common.check.tsx:1501` TS2349** (`expectTypeOf(navigation.setParams)
  .parameter(0).toBeUndefined()` not callable) only in the root project: checking common.check.tsx alone (a mirror
  project with only that file) matches tsc, so it depends on what's checked before it.
- **discord.js root `REST.ts:128` and `structures/__tests__/types/channels.test-d.ts:8`**: only in the broken root
  project (no jsx setting, everything included).
- **redux-toolkit `combineSlices.ts:120` +TS2344 `Sl` vs `AnySliceLike`** (relations, agent A's key).
- Display: a method's instantiated type parameter defaults print uninstantiated (`<EO = O>` for `<EO = any>`), a
  default type argument is left out (`E<EO>` for `E<EO, Cfg<EO>>`), and `PayloadAction<..., never>` prints a defaulted
  `never` argument tsc leaves out.

## Two missing-diagnostic quirks from the source sweep (plan 52 drain, ss-d round 2)
### `import * as X` of an `export =` module under esModuleInterop is a *clone* of the target (docmost, -TS2322)
tsc's resolveESModuleSymbol: a namespace import (or `import()`) of an `export =` whose type has call/construct
signatures (or a `default`) under esModuleInterop resolves to `cloneTypeAsModuleType(symbol, ...)`: a new transient
symbol with the same flags and declarations. Its value type is the synthetic module type (no signatures: `new X()` is
TS2351, tttt has that), and its *type* meaning is the clone's own declared interface type, a distinct type from the
original interface with its own `this` type, printed `import("<path>")`. So the original and the clone don't relate
when the interface returns `this` somewhere. Repro (`/tmp/ss-d-probes/p14` shape):

```ts
// node_modules/jz/index.d.ts
interface JSZip { file(path: string): JSZip.JSZipObject | null; file(path: string, data: string): this;
  folder(name: string): JSZip | null; new (): this; (): JSZip; prototype: JSZip; }
declare namespace JSZip { interface JSZipObject { name: string } }
declare var JSZip: JSZip;
export = JSZip;
// a.ts, module commonjs (esModuleInterop is on by default in 6.0), strict false
import * as JSZip from 'jz';
export function f(zip: JSZip) {
  const stack: { folder: JSZip }[] = [{ folder: zip }];
  const { folder } = stack.pop();
  stack.push({ folder: folder.folder("x") });   // tsc: TS2322 Type 'import(".../jz/index")' is not assignable to type 'JSZip'
}
```
Fixing it means a second declared type for the same declarations, keyed by the import (tttt keys declared types by
symbol): plan-sized for one repo; only worth it if more show up.

### Property access on a union member `E & null` with E constrained to the error type (react-navigation, -TS2339)
`T extends Unresolved` narrowed by `=== undefined` is `T & ({} | null)`; tsc reports TS2339 on it ("does not exist on
type 'T & null'"). Probing tsc (`function f<E extends Unresolved>(a: E & {}, b: E & null, c: E & { c: 3 },
d: (E & {}) | (E & null))`): only `d.x` is an error, `a.x`, `b.x`, `c.x` alone are not. So a lone intersection with
an error-constrained member is any-like but a union of them isn't (getUnionOrIntersectionProperty skips members whose
apparent type is the error type, and the union's property needs every member). tttt's check_access treats any union
member with a type parameter as opaque. Needs reading tsc's getPropertyOfType for unions of intersections first; the
lone-intersection case must stay silent.

## Spelling suggestions counted in check order: the deferred bodies are done, lazy type resolution isn't
tsc gives at most 10 "Did you mean" suggestions per checker (`suggestionCount` / `maximumSuggestionCount` in
`resolveNameHelper`'s lazy diagnostic), and the count increases for every name it fails to resolve, suggestion or not,
in the order the *checker* resolves names. tttt counts at the resolve shards' merge (`resolve_shard_merge`): since
the plan-52 drain it counts the top level first and then the bodies tsc defers (`checkNodeDeferred`: function
expressions, arrows, object literal methods and accessors, class expressions) breadth first (`ResolveShard.body_parent`,
`suggestion_count_order()`), which fixed jest's test files (`test(...)` calls at the top level get "Did you mean
'Test'?" before the `expect`s in their callbacks spend the budget).

Still different (angular-components `src/cdk/testing/testbed/task-state-zone-interceptor.ts`, -TS2552 in 3 projects):
tsc resolves type references lazily, when a type is first needed. There, line 36's initializer calls
`this._getTaskStateFromInternalZoneState(...)`, whose signature's parameter type `HasTaskState` (line 55) is resolved
then, before the method at line 48 is checked: tsc suggests 'TaskState' at 33, 43 and 55 but not at 48. A binder-side
order can't know that; it would need counting at the checker's first resolution of each name (tsc's actual model).
Also not covered: arrow bodies whose return expressions inference checks early (getReturnTypeFromBody of a
context-sensitive argument), and JavaScript files (resolved serially, counted in walk order).

Since 4e156fbf: only the *body* is deferred (signatures count in place), and a syntactically contextual function
expression without a return annotation counts its return expressions and call-statement callees early (tsc infers
its return type on the spot). Contextual is a syntactic guess; arguments of a call to an unresolved name are not
contextual. Still off: jest-reporters' GitHubActionsReporter.test.ts, where tsc's order mixes nested `describe`
bodies in an order this model doesn't produce. `/tmp/ss-d-tsc` had a tsc copy logging `COUNT n name file:line` at
the suggestion site in resolveNameHelper: the quickest way to see tsc's real order.

## Source sweep fan-out 2026-10-14 (agent B)
Fixed: adonis (`this.x = v` context keeps an optional property's undefined), docmost (forceTuple reaches nested
array literals), vueuse (generic rest read as `T[i]` in signature matching), tsx (assignment declarations union
literal-reduced; unbraced `if` branch no block scope container), renovate (missing element access narrowed), es-toolkit
(`Record<string, T>[never]`), sequelize's +TS2578 (union-key write types without missing), solid (skipped generic call
typed against its rest element), tanstack-table preact/react (JSX attributes needn't have optional target props in the
subtype pass). Left:
- **better-auth react-store.ts:67 / lynx-store.ts:65 -TS2345** (`listenKeys(store as any, keys, emitChange)` inside
  `useCallback((onChange) => { ...; return listenKeys(...) }, deps)`): a tsc quirk. `useCallback<T extends Function>
  (callback: T)` infers T as the arrow's own type, so the arrow's contextual signature is its own; computing its return
  type from the deferred check, checkExpressionCached hides the pushed return-type resolution (resolutionStart), the
  inner call's inferTypeArguments asks getContextualReturnType, which recomputes the body, and the inner call is
  resolved *recursively*: resolveCall's `recursiveResolution` sets InferenceFlagsNoConstraintChecks, so the
  contra-candidate (the outer SomeStore) wins over the covariant `any` without the constraint check, the candidate
  fails, and that recursive resolution's TS2345 is reported (instrumented tsgo, a RECURSIVE log in resolveCall).
  Reduced (tsc: TS2345 on `keys`, tttt none): `type SV<S> = S extends { get(): infer V } ? V : any; interface RA<V =
  any> { get(): V } interface MS<V = any> { get(): V; setKey(k: keyof V, v: any): void } declare function lk<S extends
  { setKey: (key: any, value: any) => void }>($store: S, keys: S extends { setKey: (key: infer Key, value: never) =>
  unknown } ? readonly Key[] : never, listener: (value: SV<S>) => void): () => void; type SK<T> = T extends { setKey:
  (k: infer K, v: any) => unknown } ? K : never; declare function use3<T>(callback: T): T; export function a<O extends
  RA | MS>(store: O, keys: SK<O>[]) { const emitChange = (value: SV<O>) => {}; use3(() => lk(store as any, keys,
  emitChange)); }`. Mirroring it: when a call's contextual type comes through the return of a function whose
  contextual signature is its own and still in flight, resolve it once more without contextual type and without
  constraint checks (single generic candidate) and keep that pass's errors. Not done (2 diagnostics, one repo).
- **mermaid create-types-from-json-schema.mts:129 +TS2345** (`removeRequired(mermaidConfigSchema)`,
  `JSONSchemaType<MermaidConfig>` to `JSONSchemaType<Record<string, any>>`, ajv): a variance difference.
  `UncheckedPropertiesSchema<T>` (`{ [K in keyof T]-?: (UncheckedJSONSchemaType<T[K], false> & Nullable<T[K]>) | {
  $ref: string } }`) measures 9 (covariant + unmeasurable) in tsgo, 8 (invariant + unmeasurable) in tttt
  (scripts/variancetrace.py --block UncheckedPropertiesSchema: tsgo's measurement meets itself, `VAR circular ...
  restart`, Unknown). Only matters where a property type is the error type: MermaidConfig's `dompurifyConfig?: Config`
  has `USE_PROFILES?: false | UseProfilesConfig` with DOM types unresolved (lib without DOM). Reduction: ajv's
  `dist/types/json-schema.d.ts` cut to 40 lines (a ddmin of it keeps UncheckedJSONSchemaType, UncheckedProperties
  Schema, UncheckedRequiredMembers, Nullable, Known, StrictNullChecksWrapper), and `interface C1 { u?: false | XX |
  undefined }; declare const s3: JSONSchemaType<{ d?: C1 }>; function rr(schema: JSONSchemaType<Record<string, any>>)
  {} rr(s3)` (tsc fine, tttt TS2345; with `C1` directly both report). Variance accuracy (variance_accuracy.md).
  Display seen there: tsc names `JSONSchemaType<C1>` (the outer alias of an alias whose body is an instantiation of
  another alias through a resolved conditional), tttt `UncheckedJSONSchemaType<C1, false>`.
- **es-toolkit/tests/types `compat.spec-d.ts:34` -TS2344** (flattenDeep vs lodash's, `Flat<T>` against es-toolkit's
  inline conditional): tsc's own order artifact. tsc reports it only with the whole spec file checked; the same line
  alone, or with the flatMap lines before it, is clean in tsc and tttt alike.
- tttt misses tsc's TS2694 `Namespace '_default' has no exported member 'Config'` for
  `import('<abs path>/node_modules/dompurify').Config` (a CJS package's default namespace) in a probe; not looked at.

## History dependences recycling exposes (plan 67 stage 1, 2026-10-15)
`scripts/kdiff.py -k 1 -m 0` (a fresh checker per file against one that never recycles; `scripts/oracles.py`'s
`recycle`) lists 4 / 3363 source sweep projects, where K=16 lists 1. In each, the never-recycling run is the one that
matches tsc. The default budget (256 MB) doesn't recycle any of them, so the sweeps don't show them:
- **inbox-zero/apps/web**: the `x?: undefined` member order of widened object literals (tsgo's per-checker
  undefinedProperties cache, `widen_object_literal()`'s KEY_UNDEFINED_PROPERTY). tsgo depends on its history here too.
  The same mechanism changes 4 vscode `.d.ts` files at the default budget (dtsdiff 74466 -> 74462:
  `terminal/test/browser/xterm/recordings/rich/windows11_pwsh7_*.d.ts`, `data?:`/`id?: undefined` swapped). Accepted:
  only a checker's history can reproduce it.
- **sequelize, sequelize/packages/core**: TS7022 on `models` (sequelize-typescript.ts:335) is lost with a fresh checker
  per file. That's circularity that depends on what an earlier file resolved first. Reduce it with `TTTT_CHECK_ONLY` /
  `TTTT_DEBUG_CYCLE=1`: does tsgo report it on that file alone?
- **lucide**: the elaboration line `Type 'RenderedElement' is not assignable to type 'ReactNode'` is missing with a
  fresh checker per file (a relation cached or a type made earlier changes the elaboration).

## Fan-out 2026-10-15 (agent D, checker diagnostics on real code)
Fixed: DT's last mismatch (ember/v2: imports resolved by tsc's global-type initialization before the ambient modules
exist), mikro-orm x2 (method free parameters with literal defaults), react-select x3 (optional intersection properties'
read types). Left:
- **vue corpus 0 / 2** (`compiler-sfc/src/style/pluginScoped.ts:190,235` -TS7006 on `n.nodes[0].each(ss => ...)`):
  postcss-selector-parser's `Selector = _Selector<Selector>` / `interface _Selector<S> extends Container<string,
  Diff<Node, S>>` / `interface Pseudo extends Container<string, Selector>`. tsc's resolveStructuredTypeMembers has no
  in-flight state: resolving `_Selector<Selector>`'s bases evaluates `Diff<Node, Selector>`, which relates Pseudo to
  Selector, which resolves `_Selector<Selector>`'s members again, until the instantiation stack is 100 deep: TS5115
  (both report it, at line 107) and the instantiation that hit the limit is the error type for good, here making
  `Pseudo.nodes` `any[]` (so `ss` is implicitly any). tttt's members_reentered_unguarded() reports the TS5115 but
  doesn't reproduce which instantiation ends up as the error type. Standalone: the d.ts alone gives tsc's TS5115 at
  `interface Root extends Container<undefined, Selector>` (its name, line 338), tttt nothing (`r1.ts` shape: `Diff`,
  `NodeTypes { selector; root; pseudo; tag }`, `Node = NodeTypes[keyof NodeTypes]`, Base/Container/_Selector/Root/
  Pseudo/Tag, 20 lines, `export {}`). Matching it means making the second round's error type land where tsc's does:
  plan-sized and fragile; only worth it with a faithful in-flight-free member resolution.
- arktype (this.test.ts:190 +TS2322), better-auth,
  es-toolkit, knip, monkeytype, pixijs, redux-toolkit: unchanged, not looked at again (see
  the entries above). svelte-query's +TS2554 matched in this round's runs (order-dependent, entry above).
- The tiptap/demos +TS2345 x9 (VueNodeViewRenderer.test.ts, entries above) is unchanged.
