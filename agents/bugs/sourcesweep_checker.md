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
  tests/types/order-by-relations.ts:24,26, closed by 2236c911d (intersection targets' combined-properties check uses isGenericObjectType); projdiff 2026-10-24: tsconfig.json and tsconfig.build.json byte-identical, K1-K16 identical. [fixed 2236c911d]

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
  Cause: when no candidate is checked because the explicit type arguments fail their constraints, tsc fills the
  missing type arguments with their defaults as written (getTypeArgumentsFromNodes); tttt instantiated them.
  Repro ttttTypeArgumentConstraintFailureDefaults. [fixed 5e8c02f34]
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

## opencode leftovers (2026-10-23, corB and det2)
anomalyco/opencode at 4232b8bc (repos.txt, `# perf: no opencode`). After det2's alias re-entry series (aliases
re-enter below call barriers, type arguments before the reference, contextual return inference not on the stack)
packages/core is clean under every mode (20 default runs identical, K1, `--checker-memory 0`, K16) and
`scripts/sourcesweep.py -f '^opencode/'` gives 60 / 62; the earlier app/desktop/web/storybook/ai/ui
TS2769/TS2339/TS2740/TS5088 mismatches are fixed (rechecked by corB at 2f81c6966: 60 / 62, kdiff 0 / 3 on
core/sdk/tui). The two left are node-build.test.ts:49 (core/tsconfig.tests.json, whose references aren't built:
TS6305 everywhere, so the core imports are any) and starlight's schema.ts:119. The typewriter and alias-printing items
below were seen in the root project (`opencode`, about 60k lines of tsc output: React's and solid's JSX together),
which the sweep's cache no longer lists (its 62 configs are the packages'), so nothing measures them now:
- **The return type of a failed generic call** (-TS2322 at core/test/effect/layer-node/node-build.test.ts:49,
  tsconfig.tests.json). Both checkers fail the argument check of `layer: Implementation &
  CheckLayer<NoInfer<Implementation>>` with Implementation = Layer.Any, but they return different types afterwards.
  tsgo returns `Provider<never, unknown, Tag<"global">>` (LayerOutput<Any>). tttt returns `Provider<Result, any, ...>`,
  through the overload-failure re-inference in call.c (make_candidate with the skip flags), so the next node's
  `deps: [a]` check doesn't fail. Probe (2026-10-23):
  - A /tmp tsconfig extends packages/core/tsconfig.tests.json, with `rootDir: "/"`, `references` to core's
    tsconfig.json, a node_modules symlink to core's, and a copy of packages/util/src/effect/layer-node.ts as ln.ts.
  - zz.ts:
    ```
    const makeGlobalNode = tags({ location: ["global"], global: [] }).make("global")
    class Result extends Context.Service<Result, { readonly value: string }>()("t") {}
    declare const eff: Effect.Effect<{ readonly value: string }, any, any>
    declare const dep: any
    const r = makeGlobalNode({ service: Result, layer: Layer.effect(Result, eff), deps: [dep] })
    ```
    tsgo types r as Provider<never, unknown, ...>, tttt as Provider<Result, any, ...>.
  - Each ingredient matters. With `deps: []` the deps check fails instead and both return Provider<Result>. With the
    layer as a variable of type `Layer<Result, any, any>`, inference succeeds in both.
  - A line-by-line reduction of ln.ts with the real effect types stalled at 245 lines. Self-contained mimics, even with
    effect's overloaded `Layer.effect`, `Exclude<R, Scope>`, Identity/TagInput/DistributiveOmit/CheckDependencies and
    const type parameters, infer the Layer and don't fail at all.
  - So the inference that yields Layer.Any depends on something in effect's Layer or Context.Service types that hasn't
    been isolated (Pipeable/Unify members, Context.Key variance), and the return difference comes after it.
  - 2026-10-24 at 10941157b: still, projdiff core/tsconfig.tests.json tsgo 5315 / tttt 5312: -TS2322 node-build.test.ts:49 (and the
    two -TS2488 below). [open]
- **opencode/packages/core/tsconfig.tests.json: -TS2488 at test/session-owned.test.ts:197 and :845** ("Type 'never'
  must have a '[Symbol.iterator]()' method", tsgo only), found by the 2026-10-24 at 10941157b recheck, uninvestigated. Cause: an overload failure's fallback inference (SkipGenericFunctions) inferred from an argument that is a call returning a generic function, `e.pipe(Effect.satisfiesServicesType<never>(), Effect.setContext(Context.empty()))`; tsc's checkExpression makes it anyFunctionType there (instantiateTypeWithSingleGenericCallSignature), so the failed pipe's C stays never and `yield* never` is TS2488. Repro test/cases/ttttOverloadFailureSkipsGenericCallResult.ts. [fixed 9ba61e39e]
- **JSX children elaboration under a generic target** (typewriter.tsx:52, root project). tsgo reports TS2746
  (several children for `(IntrinsicAttributes & DynamicProps<"p" | T, ...>)["children"]`); tttt took the elementwise
  path (is_iterable_like of the deferred indexed access) and reported TS2322. In the root project, JSX is React's while
  the components are solid's. 2026-10-24 at 10941157b: tttt reports the TS2746 at typewriter.tsx(52,6) on the root project
  (opencode/tsconfig.json). Fixed by b5dd411e6 (children aren't elaborated against a generic props type's `P["children"]`). [fixed b5dd411e6]
- **starlight schema.ts:119**: tsgo TS5114 ("Instantiations of type 'ZodType' appear infinitely circular"), tttt
  TS2589 next to it. This is the instantiation-limit family (bugs/tsgo_divergences.md, MEMBERS nesting). Cut down
  (scripts/reduce.py, then by hand) to test/cases/ttttRecursiveBaseArgumentLimit.ts, which is known failing:
  `declare class ZodUnion<T extends readonly ZodType[]> extends ZodType<T[number]["_input"]> {}` and `type B =
  ZodUnion<[B, ...B[]]>`.
  - tsgo reports TS5114 at the alias. The constraint check of the type argument relates B to ZodType<any>, which needs
    B's `_input`, whose base's argument B["_input"] resolves B's members again from the start (no in-flight state)
    until the stack is 100 deep.
  - tttt doesn't relate there (no error at the alias). At a use (`b._input`) it reports twice. First the TS5114 that
    members_reentered_unguarded() synthesizes (hacks.md). Then a real TS2589 from the instantiated property's re-entry
    (prop_type_slow, up to 200 levels), whose stack of indexed accesses names nothing.
  - Two divergences: where the alias's constraint check goes, and a second limit hit after the synthesized one. The
    first is the members stage E of agents/notes/reentrancy.md.
  - 2026-10-24 at 10941157b: still, projdiff opencode/packages/web tsgo 44 / tttt 45: +TS2589 at schema.ts:119 (the TS5114 matches). [open]
- A union member a non-generic alias names prints as the alias in tttt's error heads (`S` for `S | undefined`, `type S
  = readonly {...}[]`) and expanded in tsgo's. 2026-10-24 at 10941157b: the root project opencode/tsconfig.json has the same (file, line,
  code) as tsgo but for starlight's +TS2589 (17337 / 17338); by text, 363 tsgo-only and 281 tttt-only distinct
  diagnostics of ~17.3k (union member order, truncation, chains...); the alias instance wasn't isolated among them. [open]

## Refresh 8: random large repositories (2026-10-23, cor10)
Day one at 57c517455 (measurements.md "Fresh repositories, seventh rule"); checkouts in thirdparty/sources/REPO, commands
from there; tsgo's verdicts in test/sourcesweep/tsc.jsonl.gz.
- **A raw NUL byte inside a regex literal ends it**: 0sec/packages/cli/src/tui/agents-panel-model.ts:41
  `value.replace(/[<NUL>-<US><DEL>]+/g, " ")` (control bytes written literally) -> spurious TS1161/TS1127/TS1135 at
  (41,...), and since the program then has syntax errors, every semantic diagnostic is suppressed (0sec/packages/cli
  and 0sec/packages/core: ~4000 missing, -TS2307 880, -TS2591 2141...; abacusai-bot/apps/host has the same TS1127/TS1161
  plus 354 -TS6305 behind it). parse_regex (src/parser.c) tests `!c` for the end of text; tsc's scanner only stops at
  the real end (a NUL is an ordinary character in a regex body). Fixed: parse_regex asks at_eof; 0sec 12 / 12 and
  abacusai-bot/apps/host match (repro test/cases/ttttRegexNulByte.ts). [fixed 2026-10-23]
- **appflare: +TS5115 moves with the checker count** (to det3): tsgo (singleThreaded) reports one TS5115 ("Instantiations
  of the following types appear infinitely circular: 'IsOptionalIn', '$ZodTypeInternals', '$InferZodRecordInput'", zod
  4) at packages/schema/src/config-patch.ts(124,28). `release/tttt --noEmit --pretty false -p appflare/packages/schema`:
  `--checkers 1 --threads 1` matches; `--checkers 2 --threads 2` config-patch.ts(131,12) instead; `--checkers 4
  --threads 4` artifact.ts(144,30) + config-patch.ts(131,12); the defaults artifact.ts(144,30), (172,42),
  config-patch.ts(131,12), wrangler-config-inline.ts(174,9). At K1 `TTTT_CHECK_ONLY=artifact.ts` gives
  artifact.ts(144,30) and `=wrangler-config-inline.ts` (174,9): which file a checker saw first decides the site. 10
  configs (apps/docs, apps/installer, apps/manager, apps/manager/screenshots, apps/sandbox, packages/cli, packages/pack,
  packages/schema, packages/schema/tsconfig.build.json, scripts) match at K1 and fail at the defaults. Unchecked:
  whether tsgo moves it too at its own default checkers. 2026-10-24 at 10941157b: `sourcesweep.py -f '^appflare/'` 15 / 15 match at
  the defaults, `kdiff.py -f '^appflare/'` 0 / 11 differ (K1 vs K16): a limit hit with several checkers reruns the program
  in one checker (a75f0080c; tsgo itself moves the site with K). [fixed a75f0080c]
- **abacusai-bot/apps/web: +TS2322 on motion components' props** (to corB): 23 spurious, e.g.
  src/components/notch-controls.tsx:38 (`Type '{ ref?: Ref<HTMLDivElement> | undefined; variants?: Variants | undefined;
  ... 338 more ...}' is not assignable ...`), src/features/chat/kit/message-actions.tsx:1249, permissions/notch-list.tsx:216
  (`IntrinsicAttributes & Omit<HTMLMotionProps<"div">, "ref"> & RefAttributes<HT...`); plus one +TS2551. Guess: a
  spread of motion's HTMLMotionProps against an Omit/intersection target (relation of a big mapped/intersection). Fixed by
  corB's union-inference fix (landed as 24e649c77, f88e06da7 on its branch); projdiff 2026-10-24: 1529 diagnostics, byte-identical. [fixed 24e649c77]
- **crowd-dev/services/libs/connectors: TS7022 on the wrong declarations** (refresh 8). Two causes, neither order
  dependent (K1, K16 and TTTT_CHECK_ONLY agree):
  - tttt's spurious TS7022 on `nodes` (prWalk.ts:198, syncs/discussions.ts:124, issueComments.ts:86, issues.ts:33):
    `const { pageInfo, nodes } = data...` with data circular through pageInfo gave every name of the pattern the
    circular marker. [fixed 7b564dfa0]
  - tsgo's TS7022 on `log` (syncs/pullRequestComments.ts:32, pullRequestReviewComments.ts:50, :86), missing in tttt:
    `do { const log = ctx.child({ cursor }); const connection: string | null = fetchPage(ctx, log); cursor =
    connection; } while (cursor)` with `fetchPage<T>(c: any, log: T)` (a generic call: no quick type). tsgo reaches
    log again through cursor's back edge, connection's assignment narrowing and fetchPage's argument, below the call
    barrier: `scripts/reentrytrace.py` on that 10-line file shows tsgo's REENTER 0 at the fetchPage line and
    NESTEDCALL at both calls, tttt re-entering ten times and dropping the nested results. The re-entry model's stages
    C/D (agents/notes/reentrancy.md): the nested call's result kept. [design]
- **mulmoclaude: +TS2345 `unknown` to `string`** at server/utils/validateEntryList.ts:40 (configs mulmoclaude/server,
  mulmoclaude/test): `hasStringProp` is unresolved there (its package isn't built: TS2305 on both sides), so `item:
  unknown` isn't narrowed and `item[spec.echoProp]` (a generic key) was TS18046 and then `unknown`; tsc's
  checkNonNullExpression makes it the error type whatever the key. Fixed in generic_element_access (src/ty/expr.c),
  repro test/cases/ttttUnknownGenericElementAccess.ts. [fixed 2026-10-23]
- **deepmonkey-studio/apps/web: +TS2339 on `never`** at src/delivery/r3StateFrame.ts:211 (`clip.axis`, `clip.inverted`)
  after `if (clip.kind !== "axis") fail(...)` inside a switch case in a for loop, `fail(): never`, `let clip:
  ClipState`. The assignment's right side, evaluated for the loop's back edge while the loop is at its partial type
  ({kind: "off"}), narrowed clip to never; tttt made an incomplete never silent only at the condition, and the label
  after the `if` (joining the unreachable `fail()` with it) turned it back into a real never. tsc's newFlowType makes
  every incomplete never the silent never: new_flow_type (src/ty/flow.c), repro
  test/cases/ttttLoopNeverCallSilentNever.ts. [fixed 2026-10-23]
- **ark (packages/solid, templates/solid-start): +TS2322** at packages/solid/src/components/factory.tsx:32 (`Type 'string'
  is not assignable to type 'keyof { [P in keyof HTMLArkProps<T> as Exclude<P, "asChild">]: HTMLArkProps<T>[P]; }'`):
  solid-js's splitProps of what an earlier splitProps left. apparent_mapped_type_keys (src/ty/relate.c) skipped a
  generic mapped part of the modifiers type (`Omit<E[T], K>`), which has the properties of the lower bound of its keys
  like tsc's getPropertiesOfType; plus the printer's parentheses around a mapped type printed as a conditional in a
  check type or `keyof` operand. Repro test/cases/ttttKeyofRemappedGenericOmit.ts. [fixed 2026-10-23]
- **papra/apps/papra-client: +TS7006** at src/modules/tags/components/tag.component.tsx:28 (`Parameter 'e' implicitly
  has an 'any' type`): a contextual type tsgo finds for a Solid JSX event handler. Fixed by corF's JSX children/spread
  order fix (landed as 734367af7, 4d6af3e00 on its branch); projdiff 2026-10-24: 7 diagnostics, byte-identical. [fixed 734367af7]
- **genkit/js/testapps/dev-ui-gallery: -TS7022/-TS7024** at src/genkit.ts:78, src/main/middleware.ts:91 and :108: tsgo
  finds a circularity tttt resolves. corF's reduce.py run (2026-10-24) keeps all three in 3 files, with every
  package cut away (genkit, generateMiddleware, annotateSchema and z are all undeclared then):
  `export const ai = genkit({ plugins: [failTool.plugin()] })` in genkit.ts, and in middleware.ts
  `export const failTool = generateMiddleware({ toolName: annotateSchema(z.describe(), { enum: lazyToolNames(() => ai) }) })`,
  where `lazyToolNames(getAi: () => { registry?: unknown }): string[]` is not generic. tsgo types the arguments of a
  call whose callee is unresolved (resolveErrorCall checks them), so typing `failTool` reaches `() => ai`'s return
  type while `ai` is being resolved, and the cycle closes. tttt never types those arguments while resolving the
  declaration's type (likely its error-callee path, or a quick non-generic return type for `lazyToolNames(...)`), so
  it has no cycle. Next step: those 3 files as a test/cases repro (an undeclared callee alone may reproduce it). Not continued: the npm workflow comes
  first. Fixed: the cause was the order, not the unresolved callees. tsc infers a function argument's return type during its contextual check, before relating it, and tttt did that only for inferring calls; check_arg() now does it for every call (test/cases/ttttArgumentReturnReentersCycle.ts). [fixed corF 2026-10-24]
- **foldkit/repos/effect/packages/tools/bundle/tsconfig.src.json: 11 missing TS18046/TS7006/TS2322/TS2345** at src/Cli.ts:75,81 and src/Reporter.ts:195-205: `fixtures`, from the `Fixtures` service whose `make` is `Effect.promise(() => Glob.glob(...)).pipe(Effect.map(Array.sort(Order.String)), ...)` (`glob` unresolved, so an Effect of unknown), read as any instead of unknown. Stable at every K and file order. Missing at a015c5c48, matching from 9ba61e39e on (under SkipGenericFunctions, a generic call argument returning a generic function is skipped by the inference: `Array.sort(Order.String)`); the opencode TS2488 repro covers it. [fixed 9ba61e39e]
- **discordx: -TS2344 x6 in @types/node@25.9.5's http2.d.ts:2400-2482** (the root tsconfig includes node_modules: @types/node 25.9.5 and 26.1.2 both declare `declare module "node:http2"`, 3147 diagnostics, mostly TS2300/TS2451). Cut down to two files each declaring the same ambient module with `class Req { on(l: () => void): this; }`, `class Resp<R extends Req = Req> { readonly req: R; }`, `function f<RQ extends typeof Req = typeof Req, RS extends typeof Resp<InstanceType<RQ>> = typeof Resp>(o: Opts<RQ, RS>): void` and a matching `interface Opts`. In the second file tsgo reports TS2344 on the type argument `InstanceType<RQ>` of `typeof Resp<...>` ("import("m").Req ... Two different types with this name exist", via `on`'s `this`), so that instantiation expression stays `typeof Resp` (tsc's getInstantiatedSignatures keeps a signature whose type arguments fail) and the error moves to `RS` in `Opts<RQ, RS>`. tttt reports the default `typeof Resp` instead. Every name resolves to the first file's symbols on both sides (the symbols baseline matches), so the second Req comes from the duplicate class declaration's own `this` type. Not fixed: conflicting duplicate classes in a program that already has thousands of errors; next step is tsgotrace.py TS2344 on the cut-down case. (claimed: cor10) [open]
- **foldkit/repos/effect/packages/effect: +TS2379 at src/Stream.ts:5141** (claimed: cor10) [open]
- **foldkit/examples/kanban: -TS2769 at src/view/column.ts:132** (claimed: cor10): `pipe(model.columns, ..., Array.findFirst(({ id }) => id === draggedId))` with `model.columns` any. effect's first findFirst overload is tried first and leaves A `unknown`, so the destructured parameter takes its pattern's type `{ id: any }` for good (assignParameterType). That type is an unwidened object literal in tsc, so the predicate overload's `Card` isn't assignable to it (propertiesRelatedTo's isObjectLiteralType target: `Property 'sortKey' does not exist`). tttt made it a synthetic, non-literal object. Now the fallback is a copy marked KEY_PATTERN_OBJECT_LITERAL, which properties_related() treats as a literal target; repro test/cases/ttttPatternFallbackObjectLiteralTarget.ts. [fixed 2026-10-24]
- **es-toolkit/tests/types: -TS2344 at compat.spec-d.ts:34** (claimed: cor10): expect-type's `toEqualTypeOf` (identity of `<T>() => T extends (L & T) | T ? ...`) of es-toolkit's `flattenDeep`, whose return is a written conditional `T extends string ? T : ...`, against lodash's `Flat<T>`. tsgo 7.1's check type of a written distributive conditional is the distributed twin, an instantiation onto the same parameter has the original, so `var v: <T>() => T extends string ? 1 : 2` declared twice is TS2403; and identity is cached for the pair in either order (getRelationKey), so the first direction compared answers both. tttt kept the original as the check type and keyed identity by direction. Now identity_structured() tells a conditional as written (or instantiated with every outer parameter to itself) from an instance, and recursive_related() keys identity by the pair in id order; repro test/cases/ttttConditionalIdentityWrittenVsInstantiated.ts. [fixed d06134025]

## Refresh 7: application monorepos on generic libraries (2026-10-23, cor9)
Six repos installed before the installs were stopped (repos.txt "Refresh 7"; the rest pending a sandboxed
ensure-sources.sh). At --threads 1: 74 / 79 projects, 4 / 6 repos; checkouts in thirdparty/sources/REPO.
- **comp: +TS2604 on a generic template-literal JSX tag** (comp/apps/app, apps/portal, packages/ui-ish configs: 3
  projects): node_modules/@trycompai/design-system/src/components/atoms/heading.tsx:48, `const Component = as ??
  (\`h${level}\` as \`h${T}\`); <Component .../>` with `T extends '1' | ... | '6'`. tsc's
  getUninstantiatedJsxSignaturesOfType (jsx.go) takes getApparentType of the tag type: an instantiable type's base
  constraint, here "h1" | ... | "h6", whose members each give an intrinsic signature. tttt finds no signature for
  `h${T}` (a type parameter T extends "h1" | "h2" works). Repro test/cases/ttttJsxTemplateLiteralTag.tsx. Fixed:
  jsx_apparent_type() (call.c) takes a template literal tag's base constraint, also for union members. [fixed corF 2026-10-24]
- **parabol: -TS7006 on kysely expression-builder callbacks** (packages/embedder, packages/server: 19 missing), e.g.
  packages/embedder/EmbeddingsJobQueueStream.ts:47 `.where('id', '=', (eb) => eb...)` and
  server/graphql/public/mutations/helpers/movePageToNewTeam.ts:19-36: tsgo reports the parameter implicitly any
  (the contextual type there gives `eb` none), tttt types it from context. Root cause: kysely's `where<RE, VE extends OperandValueExpressionOrList<DB, TB, RE>>(..., rhs: VE)` with the database type an error (any). Its constraint is a union with V in it, and tsc's contextual signature of a type parameter goes through getApparentType, the base constraint, which for a union whose member has no constraint is none (unknown): no contextual signature. tttt's contextual_signature took the declared constraint and found the factory's signature. It now takes ty_base_constraint. Repro ttttContextualTypeParamBaseConstraint. [fixed 3feee5ef8]
- **parabol/packages/embedder: +TS2322 and +TS2344 at mergeAsyncIterators.ts:52**: `AsyncIterableIterator<{[P in
  keyof T]: [ParseInt<\`${P}\`>, Result<T[P]>]}[number]>` with T a tuple of async iterators: tttt says "Type 'P' is
  not assignable to type 'string | number | bigint | boolean | null | undefined'" (the template literal's placeholder)
  and "Type 'T[P]' does not satisfy the constraint 'AsyncIterator<any, any, any>'": a homomorphic mapped type over a
  tuple, whose P tsc keeps to the numeric-string keys (and T[P] to the element types). Fixed: lower.c's
  mapped_key_flow_constraint() didn't count `[]` as a tuple (`T extends X[] | []`); test
  ttttMappedKeyEmptyTupleConstraint. [fixed corF 2026-10-24]

## hono tsconfig.build.json: spurious TS2769 at src/request.ts:214 (2026-10-24, corB)
`parseBody(this, options)` in HonoRequest's `parseBody` method: tttt reports TS2769 (`HonoRequest<P, I>` not
assignable to `HonoRequest<"/", {}>`, I bivariant [4 3] in both checkers, `I` vs `{}` fails both ways). tsgo doesn't
(projdiff.py confirms against tsgo 7.1; tsgotrace shows no TS2769 made there). The variance check can only pass in tsc
while HonoRequest's variances are in flight (getVariances' emptyArray: relateVariances skips to structural), so tsgo
most likely resolves this call during HonoRequest's measurement (the `parseBody` member's return type is inferred from
the body) and keeps that signature. tttt errors even with request.ts as the only root (a /tmp tsconfig extending the
build config, `files: [src/request.ts]`), and hand-made mimics (a class whose method returns `f(this)`, its variance
measured first through a wrapper type) agree with tsgo in both orders, so the order that matters wasn't found. A
`scripts/reduce.py --tsgo-clean` run (`--project thirdparty/sources/hono --config tsconfig.build.json --keep TS2769
--tttt-has --tsgo-clean`) was stopped for the npm priority. Rechecked (det3, 2026-10-24): not history-dependent
in either checker. tttt reports it at --checkers 1, 16 and --checker-memory 0, and with TTTT_CHECK_ONLY=src/request.ts.
tsgo doesn't report it with request.ts as the only root either, and tsgotrace shows the call's first resolution
inside checkFunctionOrConstructorSymbol (isImplementationCompatibleWithOverload -> getReturnTypeFromBody), not under a
variance measurement. reltrace pins the pair: `HonoRequest<P, I> & this -> HonoRequest<"/", {}>` passes in tsgo,
because `valid`'s two method types are both instantiations of one symbol (signaturesRelatedTo then pairs them with type
parameters erased). In tttt the source side is the declared method type itself, so same_signature_origin() fails and
the generic signatures are compared unerased. That is exactly bugs/missing_checks.md "This-instantiated members"
(2026-10-22, with test/cases/ttttThisInstantiatedMethodErased.ts failing). A relation-side shortcut was tried and
reverted: "declared type == instantiation of itself" in same_signature_origin(), even limited to methods that possibly
reference `this`, fixes hono and that test. But a class's declared type is a TY_DECLARED in tttt, not a reference, so
`HonoRequest<I> -> HonoRequest<{}>` goes structural instead of through variances, and with `valid` now erased it passes
where tsgo reports TS2345 (the test's method with an annotated implementation). The members-level design in
missing_checks.md, together with variances for a declared type against its instantiations, is the fix. Fixed (det3, 2026-10-24) with a five-line change instead (ty_function_free_params() counts an overload's implementation; agents/notes/this_instantiated_members.md): build and spec configs byte-identical. [fixed det3]
