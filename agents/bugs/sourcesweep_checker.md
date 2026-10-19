# Source sweep checker leftovers (plan 52 stage 2 onward)
What the source sweep fan-outs left, by session. `scripts/sourcesweep.py -f '^REPO/'` re-checks one repo. For a +TS2589,
a gdb breakpoint on `ty_instantiation_limit_reached` in `debug/tttt` (`--checkers 1 --threads 1`)
prints `me->instantiation_depth` / `instantiation_count`: depth 100 is a runaway recursion, count 5M an exponential (or
missing cache) blow-up. (Pruned 2026-10-12: entries for repos that fully match now are gone: date-fns, styled-components,
dokploy, chatbox, angular-components, react-navigation, telegraf, commitlint, astro, react-router, rxjs, angular-cli; again
2026-10-16 against a full sweep: 7 projects left: pixijs, better-auth, es-toolkit, knip, arktype, monkeytype, redux-toolkit.)

## Older leftovers (2026-10-08..09), still mismatching
- tsc's TS2322 message truncation (`PromiseLike<...>` elision in long union-of-signature types) differs from tttt's.

## Source sweep fan-out 2026-10-12 leftovers (agent A)
Agent A's list (config, module resolution, references, parser, perf) at the end of that session; everything else of it
was fixed (lexical, apollo-server, karakeep, postiz-app, astro, hyper, commitlint, jest, mcp-sdk, radix-primitives match; second round: react-router, shiki, lit, typedoc match too).
- **redux-toolkit**: matches the sweep since the 2026-10-17 never-reduction work. The display probe that went with it
  (`get<EN extends keyof Defs>(n: EN): Res<Defs[EN] & { type: "query" }> | undefined` printed `Res<ED & { type:
  "query"; }> | undefined` by tsc, `Res<never> | undefined` by tttt) is printing only: tsc's printer reduces every type
  (getReducedType in typeToTypeNode), so the `ED & ...` it prints probably comes from reusing the annotation's syntax (only
  with the `| undefined`, only for a generic method) rather than from a different type (not confirmed). Not pursued.
- **Content mappers with --runExternalCode**: tttt validates `contentMappers` and reports TS18068 without the flag
  (like tsgo), but with it tsgo resolves each mapper package (TS18075) and runs it; tttt does neither.

## Source sweep fan-out 2026-10-14 leftovers (agent A, relations list)
Fixed that round: vencord (TS2589 at the template, intersection any-before-unions, never-reduction against generic
references), svelte x2 (JS default type arguments, hoisted constraint against a union), react-window (peer
dependencies in package ids, landed by agent D). Left:
- (2026-10-16, agent A) monkeytype: see the 2026-10-16 section at the end (arktype and redux-toolkit match since).
- Display `(A | L1 | P1) & V` with `V extends A | L1 | L2`: fixed 2026-10-18 (agent B, a union whose intersection
  members put a known object next to a type variable is now marked as possibly reducing, TYX_CONTAINS_INTERSECTIONS).

## Source sweep fan-out 2026-10-12 leftovers (agent B)
Fixed that round: mikro-orm 1/24 -> 23/24 (NoInfer inference through a union target's leftover, reverse comparable
check before normalization, intersection typesDefinitelyUnrelated), crawlee (Partial<C> over `C extends X<P>`, vi.fn
reverse mapping of callables), base-ui TS2740 (Pick<T, K | "a"> keys), folo (untyped call args contextual any),
msw/discord.js (`...([] | [A])` spread), apollo QueryManager (SkipGenericFunctions in conditional args), super-productivity (TS2347 on tagged templates). Left:
- better-auth +TS2339 `signUpEmail`: fixed 2026-10-18 (agent B): `betterAuth(config)` with `config` typed by an
  unresolved import inferred `Options` from the named error type through `Options & {}`; tsc remakes the source as an
  intersection there, which is the plain error type (`Auth<any>`). lynx-store.ts:65 -TS2345 x2: the recursive
  resolveCall quirk below.

## Source sweep fan-out 2026-10-12 leftovers (agent D)
Probes are under /tmp/d-probes (gone after a reboot; the reductions are quoted here).
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

## Source sweep round 2026-10-12 (agent B, second task)
Fixed: vitest configs (starlight, feathers, react-scan, react-window: augmentations of a deduplicated package copy),
mermaid (discriminated relation reading target property types the source lacks), noUnusedLocals of a local merged
with an export (graphql-js, renovate), pi (widened object literals normalized), es-toolkit without (never candidates),
soybean-admin (implicit jsx-runtime import only for .tsx/JS), apollo ObservableStream (never-reduction with a
generic type literal member), ember runloop (IIFE parameters inside a call argument), vueuse/scripts (null for
string config options), tsx cache.ts (no flow narrowing of a non-variable module export). Its leftovers (monkeytype
storybook's TS2307s, tsx's TS7022) match since; pixijs is the @webgpu/types merge unit above.

## Source sweep checker leftovers of agent ss-b (round 2)
Its mikro-orm/trpc +TS2698, react-navigation TS2349, discord.js and redux-toolkit combineSlices findings match since.
- Display: a method's instantiated type parameter defaults print uninstantiated (`<EO = O>` for `<EO = any>`), a
  default type argument is left out (`E<EO>` for `E<EO, Cfg<EO>>`), and `PayloadAction<..., never>` prints a defaulted
  `never` argument tsc leaves out.

## A missing-diagnostic quirk from the source sweep (plan 52 drain, ss-d round 2)
(The other one, property access on a union of `E & null`-like members, fixed 2026-10-18 by agent B.)
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
symbol): plan-sized for one repo; only worth it if more show up. (2026-10-16: docmost itself matches now, the probe
above still differs.)

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
bodies in an order this model doesn't produce. (2026-10-16: angular-components and jest both match the sweep now; the
model's gaps above remain.) `/tmp/ss-d-tsc` had a tsc copy logging `COUNT n name file:line` at
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
  (2026-10-18, agent B: still reproduces with the reduction above. tttt has neither tsc's callResolutionStack nor
  InferenceFlagsNoConstraintChecks, and the re-entry itself happens only because checkExpressionCached hides the
  arrow's in-flight return type (resolutionStart); tttt's lazies answer in flight instead. Plan-sized for 2 diagnostics.)
- mermaid create-types-from-json-schema.mts:129 +TS2345 (ajv's `UncheckedPropertiesSchema<T>` measuring 8 in tttt, 9 in
  tsgo: `scripts/variancetrace.py --block UncheckedPropertiesSchema`) is gone from the sweep (2026-10-16); the
  variance difference itself wasn't rechecked (variance_accuracy.md).
- **es-toolkit/tests/types `compat.spec-d.ts:34` -TS2344** (flattenDeep vs lodash's, `Flat<T>` against es-toolkit's
  inline conditional): tsc's own order artifact. tsc reports it only with the whole spec file checked; the same line
  alone, or with the flatMap lines before it, is clean in tsc and tttt alike.
- tttt misses tsc's TS2694 `Namespace '_default' has no exported member 'Config'` for
  `import('<abs path>/node_modules/dompurify').Config` (a CJS package's default namespace) in a probe; not looked at.

## History dependences recycling exposes (plan 67 stage 1, 2026-10-15)
`scripts/kdiff.py -k 1 -m 0` (a fresh checker per file against one that never recycles; `scripts/oracles.py`'s
`recycle`) lists 4 / 3363 source sweep projects, where K=16 lists 1. In each, the never-recycling run is the one that
matches tsc's cached (`--checkers 1`) output. The default budget (256 MB) doesn't recycle any of them, so the sweeps don't show them:
- **inbox-zero/apps/web**: the `x?: undefined` member order of widened object literals (tsgo's per-checker
  undefinedProperties cache, `widen_object_literal()`'s KEY_UNDEFINED_PROPERTY). tsgo depends on its history here too.
  The same mechanism changes 4 vscode `.d.ts` files at the default budget (dtsdiff 74466 -> 74462:
  `terminal/test/browser/xterm/recordings/rich/windows11_pwsh7_*.d.ts`, `data?:`/`id?: undefined` swapped). Accepted:
  only a checker's history can reproduce it. (2026-10-16: it is also the one K=16 difference of `scripts/kdiff.py`,
  while tsgo's output for inbox-zero/apps/web is identical at `--checkers 1` and 16: tsgo's history there doesn't
  follow the checker split the way tttt's does.)
- **sequelize, sequelize/packages/core**: TS7022 on `models` (sequelize-typescript.ts:335, `readonly models = new
  ModelSetView<Dialect>(this, this.#models)`) appears with a fresh checker per file (and then TS2322 :1053 / TS2488
  :1080 go, models being any). **tsgo does the same**: `tsc -p packages/core --checkers 4` (or 16, 64, 256) prints
  exactly that, `--checkers 1` (the cached oracle) doesn't. The cycle (gdb on members.c's `prop->circular = true`):
  checking `this` against ModelSetView's `sequelize: SequelizeTypeScript<Dialect>` parameter simplifies a conditional
  type, whose definitely-assignable check measures some variance, which relates SequelizeTypeScript structurally and
  reads `models` while its initializer is being typed. A checker that measured those variances in an earlier file
  never gets there. tsgo-history-dependent: accepted, no fix wanted (matching it would mean matching which files each
  of tsgo's checkers sees first). TTTT_DEBUG_CYCLE doesn't print class property cycles (it follows LazyTypes only).
- lucide: matched in the 2026-10-16 `-k 1 -m 0` run (5 / 3363: inbox-zero, the two sequelize, and dokploy / folo
  TIMEOUT on a loaded machine, a fresh checker per file being slower).

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
  Verdict (2026-10-18, agent B): plan-sized, not attempted. tttt caches members while they resolve (one round), tsc
  re-resolves them from scratch on re-entry, and *which* instantiation is the one at depth 100 decides what becomes the
  error type for good; an approximation (members_reentered_unguarded() already reports the TS5115) can't place it
  without replaying tsc's whole re-entrant order. Same family, seen on the way (no oracle has it): arktype's
  `type({ name: "string", "child?": ["this", "=>", (child: { name: string; child?: { name: string } }) => child] })`
  (an annotated callback) is TS5115 in tsc ('finalizeDistillation', '_distill', 'inferredDefaultKeyOf',
  'unwrapInput': the instantiation stack repeats an 8-long distill cycle to 100), nothing in tttt. Both are "tttt's
  instantiation depth doesn't follow tsc's instantiationStack". A tsgo with a stack print at the depth limit
  (instantiateTypeWithAlias, `c.instantiationStack`) shows tsc's side in seconds.
- better-auth,
  es-toolkit, knip, pixijs: unchanged, not looked at again (see
  the entries above). svelte-query's +TS2554 matched in this round's runs (order-dependent, entry above).

## Source sweep fan-out 2026-10-16 (agent A)
Fixed: arktype type.test.ts and fn.test.ts, tanstack-table, nextra, base-ui, and tiptap/demos' +TS2345 x9 along the way (see git log). 7 projects left in the
full sweep: pixijs (binder merge unit), better-auth (lynx-store), es-toolkit (entries above; knip fixed since), and:
- **monkeytype SearchableAutoSetting.tsx:56/59**: tsc reports TS2589 (instantiation excessively deep) at
  `name={props.key}` of `<form.Field>` (the line the @ts-expect-error covers); tttt doesn't hit the limit and reports
  the validators mismatch instead. tanstack form-core's DeepKeys/DeepKeysAndValues over `{ [x: string]: Config[T] }`
  with T generic. Matching tsc's TS2589 there means matching its instantiation count/depth on a big recursive type:
  not attempted. Probe: a tsconfig extending frontend's with `"types": []`, `"paths": {"*": [frontend node_modules]}`,
  SearchableAutoSetting's imports made absolute (@monkeytype/schemas -> packages/schemas/src).
  Verdict (2026-10-18, agent B, a tsgo printing depth and count at the limit): tsc stops on the *count*, 5M
  instantiations in that one statement at depth 5-7 (`NonNullable<NonNullable<...>>` chains of DeepKeysAndValues), not
  on depth. Matching that means matching tsc's instantiation count to the unit on an exponential type: won't fix.
- **redux-toolkit module.ts:656 / react/module.ts:216**: no progress. Seen on the way: with
  `interface Api<Defs extends Record<string, QD | MD>> { util: { get<EN extends keyof Defs>(n: EN): Res<Defs[EN] &
  { type: "query" }> | undefined } }` (QD/MD discriminated by a string enum), tsc prints `c.util.get`'s return as
  `Res<ED & { type: "query"; }> | undefined` for `c: Api<Record<string, ED>>` where tttt prints `Res<never>`; without
  the `util` wrapper and the `| undefined`, both print `Res<never>`. Possibly the same eager never-reduction the
  entry above suspects.

## Plan 77 leftovers (2026-10-17, fresh repositories)
- **An import of a missing `export =` property used as a namespace (+TS2503)**: `import type { nope } from "m"` where
  `m` is `export = v` and `v`'s type has no `nope`, then `nope.X`: tsc reports only TS2305 at the import (the alias is
  unknownSymbol, every meaning, silent from there), tttt also TS2503 at `nope`. The existing-property case (skip the
  import, find the UMD global: ethereumjs's `debug.Debugger`) is fixed in symbol_has_meaning(); telling the two apart
  needs the property's existence, which the scope pass doesn't know (lower.c export_equals_property_type() decides it
  later). Only in programs that already have an error at the import. Repro: test/cases/ttttImportExportEqualsPropertyUmdNamespace.ts
  with the `{ dbg: ... }` intersection dropped and the import renamed. (2026-10-18, agent B: the TS2503 comes from
  the scope pass's not-found report (scope_report_not_found / its deferred event); fixing it needs a diagnostic the
  checker confirms later, "only if the property exists", which nothing in tttt does yet. Left.)

## clidiff `diagnostics` verdicts, 2026-10-18 (agent C)
Fixed: rocket (+TS7031: a callback parameter asked for before its call statement is checked, through a JS
this-property read elsewhere first; `InitializedBindingRare.settled_by`. Only statement calls are settled that way:
callbacks of calls in initializers, returns or nested arguments still miss tsc's on-demand contextual typing),
tiptap/demos (+TS2590: a JSX tag's intersection member took its apparent union's intrinsic signatures; the tag's type
`NoInfer<T> | (NoInfer<T> & string)` itself matches tsc's). Looked at again, unchanged (entries above):
better-auth x2, es-toolkit (tsc's own order artifact), monkeytype TS2589, pixijs (binder merge unit). arktype this.test.ts
and knip's TS7022 were fixed after (2026-10-18, agent B).

## 2026-10-19: every tsconfig*.json (agent C)
The `-p` sweep now also checks every non-`tsconfig.json` config no other config extends (1428 new projects). Day one:
**1389 / 1428 match (97.3%)**, 306 of them clean under tsc. Fixed the same day: `bigint / any` (ethereumjs), the
include walk's config-directory-first order (ethereumjs TS6307), imported-class narrowing (angular-cli; classes only,
hacks.md), no TS5097 for a side-effect import (angular-cli), an empty string option not overriding an extended one
(qwik TS5051), include-processor diagnostics in plain JS (mermaid TS6059). Left in this list's repos:
- **crawlee** packages/basic-crawler/tsconfig.build.json: -TS2339 at throttling_request_manager.ts:1064
  (`manager.extendRequestProcessingTimeSecs?.(...)` where `manager` is `this.#resolvedInner!` or an awaited
  `#selectManagerOrThrow()`: tsc says a member of that union lacks the method; tttt finds it). Uninvestigated.
- monkeytype/frontend (+TS2578/+TS2322 SearchableAutoSetting.tsx): the 2026-10-16 entry above.

## Every-tsconfig sweep, agent A's and B's repos (2026-10-19, agent A)
Fixed: TS2308 between two `export *`s of unresolved imports (atproto's six tsconfig.build.json and lex-schema, with
the TS2322/TS2339 that followed), skip-context-sensitive through `&&` (atproto oauth-provider-ui's SignUpWizard). Those
11 repos: 762 -> 768 / 778. Left (sourcesweep_build.md has the better-auth lynx-store and hono request.ts mechanisms):
- **atproto lex-schema -TS2394 x2** (record.ts:97, typed-object.ts:82, `build()` overloads against an implementation
  returning `$typed(input, this.$type)`): with LexMap the error type (its module unresolved; plain `any` too), tsc's
  apparent type of `InferOutput<TShape> & { $type: TType }` is `any` (getApparentTypeOfIntersectionType maps members
  through their apparent types), so `Simplify<...>` has only a string index signature: neither return type relates to
  the other. tttt lists the parts' properties ($type) in expand_generic_homomorphic(); fixing that alone (tried) isn't
  enough: the reverse relation passes through relate.c's generic-mapped-target HACK fallback (member by member). Probe:
  `type Simplify<T> = { [K in keyof T]: T[K] } & NonNullable<unknown>`, class `C<const TType extends string, const
  TShape extends Validator<any>>` with `build(i: Omit<InferOutput<TShape>, '$type'>): $Typed<InferOutput<TShape>,
  TType>` and implementation `build(i: Record<string, unknown>) { return $typed(i, this.$type) }`.
- **astro integrations/cloudflare -TS18048 x2** (prerender-response.ts:139): tsc types `transportResponse.body.getReader()`
  as `ReadableStreamDefaultReader<any>` (Cloudflare workers types merged with the DOM lib), so `chunk = next.value`
  assigns any and the variable keeps its declared `| undefined`; tttt agrees on that rule (probe), so its reader isn't
  `<any>`: a global interface merging difference. Not pursued.
- **better-auth e2e/smoke/.../cloudflare tsconfig.skip-lib-check-false.json**: +TS2430 x4 in @types/node's
  web-globals (TextDecoder, TextEncoder, URL, URLSearchParams `incorrectly extends` node:util's/node:url's) and +TS2610
  in tinybench: the same workers-types + @types/node global merging, here under skipLibCheck false. Not pursued.
- babel (+TS2322 x7 babel-parser jsx/index.ts:544 etc. in tsconfig.tstyche.json, TS6307 index-browser.ts/package.ts),
  astro packages/astro/tsconfig.test.json (+TS6305/-TS7016 test-utils.ts:9: a reference's output under
  scripts/node_modules/.cache): not looked at.
