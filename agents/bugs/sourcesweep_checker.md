# Source sweep checker leftovers
What the source sweep passes left, by date. (2026-10-21, a quarter sample: 1272 / 1274, the two
left being es-toolkit's tsc order artifact and monkeytype's TS2589 count, both won't-fix below.) `scripts/sourcesweep.py -f '^REPO/'` re-checks one repo. For a +TS2589,
a gdb breakpoint on `ty_instantiation_limit_reached` in `debug/tttt` (`--checkers 1 --threads 1`)
prints `me->instantiation_depth` / `instantiation_count`: depth 100 is a runaway recursion, count 5M an exponential (or
missing cache) blow-up. (Pruned 2026-10-12: entries for repos that fully match now are gone: date-fns, styled-components,
dokploy, chatbox, angular-components, react-navigation, telegraf, commitlint, astro, react-router, rxjs, angular-cli; again
2026-10-16 against a full sweep: 7 projects left: pixijs, better-auth, es-toolkit, knip, arktype, monkeytype, redux-toolkit.)

## Older leftovers (2026-10-08..09), still mismatching
- tsc's TS2322 message truncation (`PromiseLike<...>` elision in long union-of-signature types) differs from tttt's. [text]

## Source sweep 2026-10-12 leftovers (A)
The list A (config, module resolution, references, parser, perf) at the end of that pass; everything else of it
was fixed (lexical, apollo-server, karakeep, postiz-app, astro, hyper, commitlint, jest, mcp-sdk, radix-primitives match; second round: react-router, shiki, lit, typedoc match too).
- **redux-toolkit**: matches the sweep since the 2026-10-17 never-reduction work. The display probe that went with it
  (`get<EN extends keyof Defs>(n: EN): Res<Defs[EN] & { type: "query" }> | undefined` printed `Res<ED & { type:
  "query"; }> | undefined` by tsc, `Res<never> | undefined` by tttt) is printing only: tsc's printer reduces every type
  (getReducedType in typeToTypeNode), so the `ED & ...` it prints probably comes from reusing the annotation's syntax (only
  with the `| undefined`, only for a generic method) rather than from a different type (not confirmed). Not pursued. [text]
- **Content mappers with --runExternalCode**: tttt validates `contentMappers` and reports TS18068 without the flag
  (like tsgo), but with it tsgo resolves each mapper package (TS18075) and runs it; tttt does neither. [open]

## Source sweep 2026-10-14 leftovers (A, relations list)
Fixed that round: vencord (TS2589 at the template, intersection any-before-unions, never-reduction against generic
references), svelte x2 (JS default type arguments, hoisted constraint against a union), react-window (peer
dependencies in package ids, landed 2026-10-12). Left:
- (2026-10-16) monkeytype: see the 2026-10-16 section at the end (arktype and redux-toolkit match since). [dup]
- Display `(A | L1 | P1) & V` with `V extends A | L1 | L2`: fixed 2026-10-18 (a union whose intersection
  members put a known object next to a type variable is now marked as possibly reducing, TYX_CONTAINS_INTERSECTIONS). [fixed 2026-10-18]

## Source sweep 2026-10-12 leftovers (B)
Fixed that round: mikro-orm 1/24 -> 23/24 (NoInfer inference through a union target's leftover, reverse comparable
check before normalization, intersection typesDefinitelyUnrelated), crawlee (Partial<C> over `C extends X<P>`, vi.fn
reverse mapping of callables), base-ui TS2740 (Pick<T, K | "a"> keys), folo (untyped call args contextual any),
msw/discord.js (`...([] | [A])` spread), apollo QueryManager (SkipGenericFunctions in conditional args), super-productivity (TS2347 on tagged templates). Left:
- better-auth +TS2339 `signUpEmail`: fixed 2026-10-18 : `betterAuth(config)` with `config` typed by an
  unresolved import inferred `Options` from the named error type through `Options & {}`; tsc remakes the source as an
  intersection there, which is the plain error type (`Auth<any>`). lynx-store.ts:65 -TS2345 x2: the recursive
  resolveCall quirk below. [fixed 364284218]

## Source sweep 2026-10-12 leftovers (D)
Probes are under /tmp/d-probes (gone after a reboot; the reductions are quoted here).
- Display differences seen on the way: tsc prints `Nullable<Vals<K>>` (`{ [K in keyof T]: T[K] | null } & {}` over a
  mapped type) as `{ [K_1 in keyof Vals<K>]: ... }`, tttt as `Vals<K> extends infer T ? {...} : never`; the vitepress
  TS2345 elaboration prints `Partial<T>[K] & null` where tsc normalizes to `T[K] & null`; isolatedDeclarations on
  `k(c = new Set<string>()) { return c }` (TS9011 + TS9039) matches since (rechecked 2026-10-21, methods, functions,
  object literal methods). [text]
- stripInternal on interface/type-literal members, enum members, parameter properties and trailing `// @internal`:
  matches tsgo's CLI byte for byte (rechecked 2026-10-21; tsgo keeps a parameter property's
  `/** @internal */` parameter and drops only its field, as tttt does). [fixed 2026-10-21]

## Source sweep 2026-10-12 leftovers (C)
Fixed this round: arktype's ark/docs (indexed-access `T & P` reduction, union base classes), react-hook-form's
`__typetest__`, rxjs (TS2314 typeof import, `[...Mapped]`), nuxt/pinia (TS2310), openapi-typescript (cross-file
default exports), inbox-zero (namespace suggestions), tiptap `this.parent`, the perf fix for react. Left:
- **pixijs** `@webgpu/types` against lib.dom: fixed 2026-10-20, the binder's cross-file merge unit is the
  file's symbol now (`declare_file_conflict()`). [fixed a75a9bb49]

## Source sweep round 2026-10-12 (B, second task)
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
  `never` argument tsc leaves out. (2026-10-21: a guessed reduction of each, `interface Box<O> { get<EO = O>(x: EO):
  EO }` on `Box<any>`, `make<EO = O>(): E2<EO>` with `E2<A, B = Cfg<A>>`, RTK's PayloadAction shape, matches tsgo's
  types baseline: the original shapes are lost, nothing in the sweep shows it.) [fixed probe 2026-10-21]

## A missing-diagnostic quirk from the source sweep (ss-d round 2)
(The other one, property access on a union of `E & null`-like members, fixed 2026-10-18.)
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
above still differs.) [design]

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
the suggestion site in resolveNameHelper: the quickest way to see tsc's real order. [note]

## Source sweep 2026-10-14 (B)
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
  (2026-10-18: still reproduces with the reduction above. tttt has neither tsc's callResolutionStack nor
  InferenceFlagsNoConstraintChecks, and the re-entry itself happens only because checkExpressionCached hides the
  arrow's in-flight return type (resolutionStart); tttt's lazies answer in flight instead. Plan-sized for 2 diagnostics.) [dup]
- mermaid create-types-from-json-schema.mts:129 +TS2345 (ajv's `UncheckedPropertiesSchema<T>` measuring 8 in tttt, 9 in
  tsgo: `scripts/variancetrace.py --block UncheckedPropertiesSchema`) is gone from the sweep (2026-10-16); the
  variance difference itself wasn't rechecked (variance_accuracy.md). [fixed 2026-10-16]
- **es-toolkit/tests/types `compat.spec-d.ts:34` -TS2344** (flattenDeep vs lodash's, `Flat<T>` against es-toolkit's
  inline conditional): tsc's own order artifact. tsc reports it only with the whole spec file checked; the same line
  alone, or with the flatMap lines before it, is clean in tsc and tttt alike. [tsgo]
- tsc's TS2694 `Namespace '_default' has no exported member 'Config'` for `import('dompurify').Config`: fixed
  2026-10-21 (ttttImportTypeExportEqualsOtherExports): an import type looked a name up in the module's
  own exports next to any `export =`; tsgo does that only for a JavaScript `module.exports =` (its typedefs), and only
  when the assigned value has none of the name. [fixed 2026-10-21]

## History dependences recycling exposes (2026-10-15)
`scripts/kdiff.py -k 1 -m 0` (a fresh checker per file against one that never recycles; `scripts/oracles.py`'s
`recycle`) lists 4 / 3363 source sweep projects, where K=16 lists 1. In each, the never-recycling run is the one that
matches tsc's cached (`--checkers 1`) output. The default budget (256 MB) doesn't recycle any of them, so the sweeps don't show them:
- **inbox-zero/apps/web**: the `x?: undefined` member order of widened object literals (tsgo's per-checker
  undefinedProperties cache). **Fixed 2026-10-21** by a program-wide history-independent choice; since the 7.1.0-beta
  (2026-10-23) tsgo keys the cache by the widening context's property, which tttt copies (no choice, no recheck).
  The base differed in 10 lines between `--checkers 1`, 16 and `-m 0`; now none, and vscode's 4 recordings `.d.ts`
  match at the default budget. tsgo's output for inbox-zero is the same at 1 and 16 checkers (its split keeps the
  files together); where tsgo's own history shows (dts_emit.md "Member order") tttt now gives one answer whatever the split. [fixed 2026-10-21]
- **sequelize, sequelize/packages/core**: TS7022 on `models` (sequelize-typescript.ts:335, `readonly models = new
  ModelSetView<Dialect>(this, this.#models)`) appears with a fresh checker per file (and then TS2322 :1053 / TS2488
  :1080 go, models being any). **tsgo does the same**: `tsc -p packages/core --checkers 4` (or 16, 64, 256) prints
  exactly that, `--checkers 1` (the cached oracle) doesn't. The cycle (gdb on members.c's `prop->circular = true`):
  checking `this` against ModelSetView's `sequelize: SequelizeTypeScript<Dialect>` parameter simplifies a conditional
  type, whose definitely-assignable check measures some variance, which relates SequelizeTypeScript structurally and
  reads `models` while its initializer is being typed. A checker that measured those variances in an earlier file
  never gets there. tsgo-history-dependent: accepted, no fix wanted (matching it would mean matching which files each
  of tsgo's checkers sees first). TTTT_DEBUG_CYCLE doesn't print class property cycles (it follows LazyTypes only). [tsgo]
- lucide: matched in the 2026-10-16 `-k 1 -m 0` run (5 / 3363: inbox-zero, the two sequelize, and dokploy / folo
  TIMEOUT on a loaded machine, a fresh checker per file being slower). [fixed 2026-10-16]

## Source sweep 2026-10-15 (D, checker diagnostics on real code)
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
  Verdict (2026-10-18): plan-sized, not attempted. tttt caches members while they resolve (one round), tsc
  re-resolves them from scratch on re-entry, and *which* instantiation is the one at depth 100 decides what becomes the
  error type for good; an approximation (members_reentered_unguarded() already reports the TS5115) can't place it
  without replaying tsc's whole re-entrant order. Same family, seen on the way (no oracle has it): arktype's
  `type({ name: "string", "child?": ["this", "=>", (child: { name: string; child?: { name: string } }) => child] })`
  (an annotated callback) is TS5115 in tsc ('finalizeDistillation', '_distill', 'inferredDefaultKeyOf',
  'unwrapInput': the instantiation stack repeats an 8-long distill cycle to 100), nothing in tttt. Both are "tttt's
  instantiation depth doesn't follow tsc's instantiationStack". A tsgo with a stack print at the depth limit
  (instantiateTypeWithAlias, `c.instantiationStack`) shows tsc's side in seconds.
  Re-entrancy (notes/reentrancy.md, 2026-10-20): tsc re-resolves one type's members 33 deep on `r1.ts` (`scripts/reentrytrace.py`'s MEMBERS
  event); members re-entry happens in 45 of a quarter of the source sweep's projects and on zod/webpack/vue, harmless
  except where the depth limit is reached. Verdict there: won't fix without tsc's member resolution order (stage E). [tsgo]
- better-auth,
  es-toolkit, knip, pixijs: unchanged, not looked at again (see
  the entries above). svelte-query's +TS2554 matched in this round's runs (order-dependent, entry above). [note]

## Source sweep 2026-10-16 (A)
Fixed: arktype type.test.ts and fn.test.ts, tanstack-table, nextra, base-ui, and tiptap/demos' +TS2345 x9 along the way (see git log). 7 projects left in the
full sweep: pixijs (binder merge unit), better-auth (lynx-store), es-toolkit (entries above; knip fixed since), and:
- **monkeytype SearchableAutoSetting.tsx:56/59**: tsc reports TS2589 (instantiation excessively deep) at
  `name={props.key}` of `<form.Field>` (the line the @ts-expect-error covers); tttt doesn't hit the limit and reports
  the validators mismatch instead. tanstack form-core's DeepKeys/DeepKeysAndValues over `{ [x: string]: Config[T] }`
  with T generic. Matching tsc's TS2589 there means matching its instantiation count/depth on a big recursive type:
  not attempted. Probe: a tsconfig extending frontend's with `"types": []`, `"paths": {"*": [frontend node_modules]}`,
  SearchableAutoSetting's imports made absolute (@monkeytype/schemas -> packages/schemas/src).
  Verdict (2026-10-18, a tsgo printing depth and count at the limit): tsc stops on the *count*, 5M
  instantiations in that one statement at depth 5-7 (`NonNullable<NonNullable<...>>` chains of DeepKeysAndValues), not
  on depth. Matching that means matching tsc's instantiation count to the unit on an exponential type: won't fix. [tsgo]
- **redux-toolkit module.ts:656 / react/module.ts:216**: no progress. Seen on the way: with
  `interface Api<Defs extends Record<string, QD | MD>> { util: { get<EN extends keyof Defs>(n: EN): Res<Defs[EN] &
  { type: "query" }> | undefined } }` (QD/MD discriminated by a string enum), tsc prints `c.util.get`'s return as
  `Res<ED & { type: "query"; }> | undefined` for `c: Api<Record<string, ED>>` where tttt prints `Res<never>`; without
  the `util` wrapper and the `| undefined`, both print `Res<never>`. Possibly the same eager never-reduction the
  entry above suspects. [fixed oracle 2026-10-23]

## Fresh-repositories leftovers (2026-10-17)
- **An import of a missing `export =` property used as a namespace (+TS2503)**: `import type { nope } from "m"` where
  `m` is `export = v` and `v`'s type has no `nope`, then `nope.X`: tsc reports only TS2305 at the import (the alias is
  unknownSymbol, every meaning, silent from there), tttt also TS2503 at `nope`. The existing-property case (skip the
  import, find the UMD global: ethereumjs's `debug.Debugger`) is fixed in symbol_has_meaning(); telling the two apart
  needs the property's existence, which the scope pass doesn't know (lower.c export_equals_property_type() decides it
  later). Only in programs that already have an error at the import. Repro: test/cases/ttttImportExportEqualsPropertyUmdNamespace.ts
  with the `{ dbg: ... }` intersection dropped and the import renamed. (2026-10-18: the TS2503 comes from
  the scope pass's not-found report (scope_report_not_found / its deferred event); fixing it needs a diagnostic the
  checker confirms later, "only if the property exists", which nothing in tttt does yet. Left.) [open]

## clidiff `diagnostics` verdicts, 2026-10-18 
Fixed: rocket (+TS7031: a callback parameter asked for before its call statement is checked, through a JS
this-property read elsewhere first; generalized since to every function expression's context root,
FunctionImpl.context_root and ty_settle_function_context()),
tiptap/demos (+TS2590: a JSX tag's intersection member took its apparent union's intrinsic signatures; the tag's type
`NoInfer<T> | (NoInfer<T> & string)` itself matches tsc's). Looked at again, unchanged (entries above):
better-auth x2, es-toolkit (tsc's own order artifact), monkeytype TS2589, pixijs (binder merge unit). arktype this.test.ts
and knip's TS7022 were fixed after (2026-10-18).

## 2026-10-19: every tsconfig*.json 
The `-p` sweep now also checks every non-`tsconfig.json` config no other config extends (1428 new projects). Day one:
**1389 / 1428 match (97.3%)**, 306 of them clean under tsc. Fixed the same day: `bigint / any` (ethereumjs), the
include walk's config-directory-first order (ethereumjs TS6307), imported-class narrowing (angular-cli; classes only,
hacks.md), no TS5097 for a side-effect import (angular-cli), an empty string option not overriding an extended one
(qwik TS5051), include-processor diagnostics in plain JS (mermaid TS6059). Left in this list's repos:
- monkeytype/frontend (+TS2578/+TS2322 SearchableAutoSetting.tsx): the 2026-10-16 entry above. [dup]

## Every-tsconfig sweep, A's and B's repos (2026-10-19)
Fixed: TS2308 between two `export *`s of unresolved imports (atproto's six tsconfig.build.json and lex-schema, with
the TS2322/TS2339 that followed), skip-context-sensitive through `&&` (atproto oauth-provider-ui's SignUpWizard). Those
11 repos: 762 -> 768 / 778. Left (sourcesweep_build.md has the better-auth lynx-store and hono request.ts mechanisms):
- **better-auth e2e/smoke/.../cloudflare tsconfig.skip-lib-check-false.json**: fixed 2026-10-20 : the TS2430s
  were the cross-file merge unit, the TS2610 `extends globalThis.Event` (a class is no globalThis property). Its TS5115
  (zod schemas.d.cts:790, tsc's too) is also reported at miniflare index.d.ts:550 at `--threads` > 1: which checker
  first instantiates the type decides the site (the sweep runs `--threads 1`): a kdiff-style item, not looked into. [fixed 2026-10-20]
- babel tsconfig.tstyche.json TS6307 (index-browser.ts vs package.ts) and astro packages/astro/tsconfig.test.json
  (+TS6305/-TS7016) fixed 2026-10-20 : import calls/types load after a file's declarations (tsc's order of
  file.imports); an unbuilt reference's `.js` source is TS7016. [fixed 2026-10-20]
- **discordx** (two @types/node versions in one program): 9 TS2344 left in @types/node 25 http2.d.ts:2398ff (tsc finds
  v25's Http2ServerRequest unrelated to v26's through `addListener`'s return type, tttt relates them; the TS2300 cluster
  is fixed). Garbage-in program, not pursued. [tsgo]


## Every-tsconfig sweep, round 2 (2026-10-20)
Fixed: ghostfolio (a callable with its own `toString(value)` isn't assumed to extend Function: lodash's `_`),
babel jsx (evolving array pushes typed with the contextual type any, tsc's getContextFreeTypeOfExpression), crawlee
(conditional union members with an error apparent type skipped), react-navigation stack (object-literal padding
still needs a member with the property itself), astro cloudflare (an interface property merged with a later class
accessor is typed by the accessor). better-auth's cloudflare skip-lib-check-false matched after the day's merges.
Unchanged verdicts: es-toolkit (tsc order artifact), monkeytype (TS2589 count), better-auth
lynx-store, hono request.ts (entries above). Seen then, matching now (rechecked 2026-10-21): `declare abstract class C { get x() }` then
`interface C { readonly x }` is TS2687 in both; an interface `opt?: T` merged with `get opt(): T` reads `T` in both.

## Fresh repositories, third rule (2026-10-22, cor3)
Refresh 4 (repos.txt "Refresh 4": application code, 40..400 stars). Fixed: hermes-ide's TS7022 (a for-of variable
evaluated isolated from the loops in progress), youtube-enhancer's TS2322 (a generic type literal instantiation's
literal property conflict left unreduced), and two tuple bugs found on the way (`[...Conditional]` treated as an array
rest element; a spread instantiated to `undefined`). Left:
- **pi-bolt, TypeBox (+TS7053 x87, +TS2353 x31, +TS2345; 4 projects)**: `Type.Record(Type.Enum(["off", "minimal",
  ...]), V)` is `{ off: V }` in tttt, every key in tsc. Repro: test/cases/ttttConditionalConstraintDeepCut.ts (known
  failing, standalone, no TypeBox). Mechanism:
  - TypeBox's TBroadenFilter tests `TCompare<Type, Left> extends 2 | 0`; TCompare's default parameter
    `Extends = [TExtends<{}, L, R>, TExtends<{}, R, L>]` is a tuple, not a generic type, so getConditionalType decides
    the alias eagerly for generic L, R: permissive check (`[any, any]`, true), then the restrictive check
    `[TExtendsLeft<{}, Ca1<X>, Ca1<Y>>, ...]` against `[TExtendsTrueLike, TExtendsTrueLike]` (tsgo: false, deferred;
    tttt: Maybe, taken as true, so TCompare<X, Y> extends 0 for all X, Y and Broaden keeps only the first literal).
  - The Maybe comes from src/ty/relate.c conditional_source_related(), the comment "HACK: tsc relates the default
    constraint with RecursionFlags.Source; pushing the target too (a deep-nesting cut-off tsc doesn't have) saves
    react-hook-form's Path/FieldPathValue chains 10% of the react corpus": the default constraint's branches go through
    plain is_related_to(), which pushes the target as well. TExtendsLeft's chain relates a conditional per level
    against the same `TExtendsTrue<TProperties>` (a member of the TExtendsTrueLike union target), so after 3 levels the
    target stack is "expanding", and with the TExtendsRight levels the source too: expanding == 3, Maybe
    (TTTT_DEBUG_RELATE_NESTED prints `nested D (S/T)`, source/target stack depths, since this round: tsgo's stays at
    target depth 2-3 there, tttt's climbs to 5+).
  - Fix that works for the repro: relate the default constraint with is_related_to_side(..., 2), or keep the hack and
    redo a TERNARY_MAYBE concluded while my deep_cuts grew, source-only, with a RelationState flag so nested
    constraints don't retry again (nested retries compound; truncate the maybe stack to before the first attempt). Both
    leave the suite with one loss: largeTupleTypes (TS2799 at its `lengthRange` signature).
  - The largeTupleTypes cost: source-only, tttt relates `Tuple<I, E>` to `readonly any[]` down its
    GrowExp/GrowExpRev chain (union members, then indexed accesses' constraints: the chain doesn't pass through
    conditional_source_related(), whose 10-deep check never fires) to the depth-100 backstop, building
    `[...A, ...P[0]]` tuples past 10000 elements; tsgo's same chain stops near depth 22 (Maybe). Suspects for tsgo's
    stop: (1) getRecursionIdentity of the indexed accesses `[...A, ...P[0]][N]`: tsgo walks to the object type and
    uses the deferred tuple reference's node (or the tuple target), so every level shares one identity and the source
    goes expanding at 3, where tttt's recursion_identity() for TY_INDEXED returns the object type itself (a new type
    per level); (2) the constraint tsgo computes for those indexed accesses (getBaseConstraint's depth/circularity
    limits may stop before tttt's constraint_of_type() does). Before either, three real tuple bugs on that path were
    fixed (TS2624 for `[...Conditional]`, `...undefined` spreads, `[][0]` TS2493). Then drop the HACK and measure
    react's instructions (scripts/instrcount.sh --bin against a fastbuild of the branch point).
  - How it was found: tsgo built with an overlay (agents/notes/instrumenting_tsgo.md) printing getConditionalType's
    permissive/restrictive results for check types containing "TExtends" and every recursiveTypeRelatedTo result
    (source/target stack lengths, expandingFlags) while that restrictive check runs. Build with a private GOCACHE: the
    shared one was being trimmed by another worker's `--env` mid-build. [fixed f38a8b493]
- **bekuto3d (+TS2304 MaybeRefOrGetter x2, +TS7006)**: unplugin-auto-import's auto-imports.d.ts declares Vue's types
  globally with `declare global { export type { MaybeRefOrGetter, ... } from 'vue' }`. tsc binds the re-export inside
  the global augmentation as global names (TS2666 at the export, silenced there by `// @ts-nocheck`); tttt doesn't
  declare them: TS2304 at every use. Binder/declare territory (fe3). Repro:
  test/cases/ttttGlobalAugmentationReExport.ts (known failing). [fixed 4a482aba3]

## Fresh repositories, fourth rule: bun lockfiles (2026-10-22, cor6)
- **Eager parameter types (was kebo/apps/mobile's +TS7022/TS7024/TS2502, fixed 2026-10-23 corF)**: kebo's cycle was
  generic_function_argument() resolving a generic function argument's signature (typing its parameters) before
  looking for a contextual signature; tsc reads only the declared type parameters there
  (test/cases/ttttGenericArgumentParamsUntypedWithoutContext.ts). The eager-parameters design below stays relevant.
  **Same root as openclaw's TS2554 'Expected 2-0 arguments' (det, 2026-10-23)**: a re-entry of a function's signature
  while its parameters were being typed read the half-built record (min_args set, param_count still 0), which is no
  signature tsc ever has, and what it left behind depended on which entry point a checker reached first (openclaw core
  72-149 errors by checker count, run-to-run variance under work stealing). Now (members.c sig_resolving_record(),
  function_signature_params()) the record is filled only once every parameter is typed, and a re-entry gets all of
  them: those typed so far, the one in flight as any (tsc asks a parameter's type only for an argument or a rest, so a
  call that passes none sees no cycle: functionWithDefaultParameterWithNoStatements16), the rest typed then. Still
  eager, not tsc's lazy parameters: a re-entry that passes an argument for the in-flight parameter gets any where tsc
  would report a circularity (TS2502/TS7022), and the parameters after it are typed inside the cycle rather than when
  asked. The full design (the remaining kebo cause, if it is this): SigParam.type as a lazily typed slot per parameter
  (getTypeOfSymbol on the parameter, its own cycle detection through ty_binding_type's resolving stack), the record's
  shape (count, min_args, optional, rest, this) derived from the node up front, so signature_resolve never types a
  parameter nobody asked for. Every reader of `params[i].type` would go through an accessor; measure first. [design]
- **hono/tsconfig.build.json, hono/tsconfig.spec.json (+TS2769 at src/request.ts:214)**: in the full sweep on this
  branch and with main's release binary too (not from the refresh), not in the `--sample 4` quarter. Uninvestigated. [dup]
- **better-drizzle (+TS2502 'transaction' at src/types/plugins.ts:742,2, default checkers only)**: after c0a04350a, tsc's
  TS2502 at 467 is right at every checker count, but the default (K>1) run also reports the second `transaction`
  (742, `PluginInterceptContext`-like context) where `--checkers 1` doesn't: which cycle is entered first depends on
  the files a checker saw (scripts/kdiff.py territory, plan 62). Fixed by acb8dfe7f (an any/unknown target property
  no longer asks for the source property's type, tsc's isPropertySymbolTypeRelated): the same at K1-K16. Remaining: TS2578 at
  tests/types/order-by-relations.ts:24,26 (uninvestigated). [open]

## Refresh 6 leftovers (2026-10-23, corG: tanstack-ai, node-pg-migrate, portabase)
- **tanstack-ai/packages/ai-harness -TS2615** (`Type of property 'agents' circularly references itself in mapped type
  'Pick<SessionImpl<HarnessDefinition<any, any, any, ?, readonly AnyTool[], any>>, PublicSessionMember>'`): a tsgo
  order artifact. tsgo reports it at 1 site at `--checkers 1` (session/handle.ts:500, `new Proxy(session.agents, ...)`)
  and at 4 at its default checker count (define.ts:465, base.ts:668, handle.ts:500, requests.ts:398): its output depends
  on which files a checker saw first. The `?` is a variance marker: an instrumented tsgo (a stack dump at
  getTypeOfMappedSymbol's TS2615) shows the cycle inside inferTypeArguments of the `new Proxy(...)` call: a relation
  reaches getVariances (of an alias over HarnessDefinition, `SessionWithMetadata`'s TMetadataSchema is the `?`), whose
  marker instantiation relates Pick<SessionImpl<...>>'s mapped `agents` (`AgentHandles<THarness>`, a mapped type over
  `HarnessAgentsOf<THarness>`) while that same `agents` symbol's type is on the resolution stack (stack: sym agents,
  TAgent, HarnessAgentsOf<THarness>, sym agents), 180+ relater frames deep. tttt's relation takes another path and
  never re-enters. Not pursued: matching it means matching tsgo's relation order and caching frame by frame on a 10k
  line project for a diagnostic tsgo itself doesn't report consistently. [tsgo]
- **portabase -TS2322** (components/ui/form.tsx:227, not in corG's brief): tsgo says `useForm<TypeOf<Z>>({...})`
  returns `UseFormReturn<output<Z>, any, TFieldValues>`, react-hook-form's third type parameter defaulting to
  `TFieldValues` left uninstantiated (the default of a partially given type argument list); tttt instantiates it.
  Not investigated further. [open]
- **node-pg-migrate docs TS2741 text**: `import * as typescript from 'typescript'` in a .d.mts of a CommonJS .d.cts
  is a clone module type that tsgo prints `typeof import("<path>/lib/version")`; tttt prints the namespace symbol's
  name, `typeof typescript` (also under node16-nodenext: make_gather_target's synthetic-default namespace symbol). [text]

## pothos's TS7023: the variance measurement re-entering an own return type (2026-10-23, corF)
-TS7023 at pothos/packages/plugin-smart-subscriptions/src/cache-node.ts:41 (`managerForField`), only in
pothos/tsconfig.eslint.json, where packages/core/src is in the program. The known-failing
test/cases/ttttVarianceReentersOwnReturnType.ts reproduces it in 12 lines. tsgo checks the method body before asking
for its return type. `new FSM(this)` infers from `this` to CacheNode<T>, which needs CacheNode's variances. The
outermost measurement compares CacheNode<sub> with CacheNode<super>, and through FSM's `cacheNode` gets back to
CacheNode's members. managerForField's return type is then asked for below the variance barrier (reentrytrace: REENTER
3 variance x2, RETINFLIGHT variance, NESTEDCALL x2). That nested resolution checks `return this.fieldManager`, whose
assignment narrowing re-resolves the call (nested). One more level down, the return type is pushed again above the
barrier and the cycle is found: TS7023, `() => any`. tttt resolves the call once (`NESTEDCALL 1`), never re-enters
the return type below the variance barrier, and gets FSM<Types>. Small details flip tsgo's outcome:
- `x!: Types` as CacheNode's first property: no TS7023. [note]
- Explicit `new FSM<Types>(this)`: none. [note]
- In the multi-file form, the order of the files decides it. [note]
That makes this the "Outstanding stages" C/D model of agents/notes/reentrancy.md (nested call results superseding,
return contexts pulled), plus return types re-entering below the variance barrier, not a local fix. Left for that
rework. While reducing it I found and fixed scripts/reentrytrace.py's tsgo overlay: `Symbol.Name` is a method now. [design]
