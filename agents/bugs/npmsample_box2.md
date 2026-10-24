# Random npm projects: divergences found by the second box (seeds 20261100-20261199)
Same conventions as agents/bugs/npmsample.md (that file has the first seeds of this box too); a separate file so the two
boxes' appends never conflict. One section per seed, one bullet per repro, status tags as in agents/bugs/README.md.

## Seed 20261106
- npm_rabbitmq-pub-sub_TS2339 (rabbitmq-pub-sub@0.2.5): `import * as Promise from "bluebird"` (@types/bluebird: `declare class Bluebird<R>` with statics, merged namespace, `export = Bluebird`) under commonjs/bundler, esModuleInterop defaulting on: tsgo reports TS2339 "Property 'resolve' does not exist on type 'typeof Bluebird'" for `Promise.resolve(...)` (a namespace import of an `export =` class gets a synthetic namespace object without the class's statics), tttt resolves the static (-20 diagnostics in the project: TS2339, TS2322, TS7006 downstream). Same cause as serverless-openapi (det3): the synthetic-default spread of the `export =` class keeps its static methods, which tsc's isSpreadableProperty drops. The test passes since ecee294e1. [fixed ecee294e1]
- npm__overlastic_vue2_TS2339 (@overlastic/vue2@0.9.0, vue 2.6 types): tttt extra TS2339 "Property '$visible' does not exist on type 'Vue'" (and `$overlay`) for `this.$visible` in `methods` of `Vue.extend({computed: {$visible: {get(){}}}, methods: {...}})`; tsgo infers the ThisType from the computed accessors. The repro is the reducer's multi-file cut (the vue types with unresolved names; a cut of my own to one file stopped reproducing). Guess: the ThisType<CombinedVueInstance<...>> intersection, with Computed inferred from `Accessors<Computed>` through the overload, loses the `$`-prefixed key. Fixed: the cause was overload resolution, not ThisType. `this` from an earlier candidate's contextual signature (the second overload's DefaultMethods `(this: V)`) outlived it when the settled first overload gave none (assign_contextual_signature_as). [fixed corF 2026-10-24]
- npm__overlastic_vue2_TS2339_strictmode.tsx (same project, playgrounds/react): `paths` maps `react` to a script file `global.react.d.ts` holding `declare module 'react' {...}`; tsgo reports TS2339 on `<React.StrictMode>` ('StrictMode' does not exist on type 'typeof import("react")'), the repro shows tttt reporting the opening tag but missing the closing tag's (2,37); in the project tttt reports neither. Guess: the property access on a JSX closing-tag name isn't checked, and the opening one differs by the paths-mapped ambient module resolving. Fixed: the closing tag's name is now checked (jsx_element_typecheck); in the project the second vue2 fix made the whole output byte-identical, and no separate paths divergence showed on a fresh clone. [fixed corF 2026-10-24]
- Unreduced: 0061 @point0/compiler@0.4.2 (packages/compiler, ../core/src/point0.ts, a 22k-line file): tttt-only 18 diagnostics, tsgo 1: TS2345 "Argument of type '{ input: void | InputRawUnknown; point: AnyPoint; error: undefined; output: undefined; }' is not assignable to parameter of type '{ input: Inp...'" (lines 17648, 17768-17791), TS2349 "This expression is not callable" (19142), TS2322 "Type 'void | InputRaw' is not assignable to type 'InputRaw'" (20532), more TS2345 at 20499-20723; reduce.py got nowhere in 5 minutes on the whole file. Looks like one generic-conditional bug (`TSchema extends InputSchema ? InputRaw<TSchema> : undefined`-style inputs). Not a flip (tsgo already has an error). [open]
- Unreduced: 0044 ozone-item-edit (taktik/ozone-components): the default include picks up binary `.ts` files (MPEG-TS video chunks, packages/ozone-material/ozone-video-player/test/data/3/.../chunk0000N.ts, 710 KB); both sides report syntax errors there but tsgo only TS1128+TS1434 at line 1 per file (42 diagnostics in all) and tttt 1943 (TS1127 Invalid character, TS1128, TS1434, TS1002...). Not a flip. Guess: the scanner's recovery on an invalid-UTF-8/binary file (tsgo stops after its first error: possibly it bails when the text isn't valid UTF-8 or contains NUL). A test file can't hold the bytes. Fixed: tttt found the binary marker (TS1490) like tsgo but skipped only to the next NUL byte (strlen) and went on scanning; it now skips to the end of the file. The real chunks give tsgo's output byte for byte. [fixed corF 2026-10-24]
- Known, not new: 0097 better-auth react-store.ts TS2345 (sourcesweep_checker.md, `listenKeys(store as any, ...)`). 

## Seed 20261107
- **K-dependence (priority 1)**, 0093-LeCarbonator-discordjs-choice-selectmenu-builder (tsconfig.bench.json; tsgo 6
  errors): two default runs agree, `--threads 1` differs. Defaults: an extra TS2310 "Type 'APIGuild' recursively
  references itself as a base type" at node_modules/discord-api-types/payloads/v10/guild.d.ts(74); --threads 1: an
  extra TS2344 at payloads/v10/invite.d.ts(10) instead. Neither is tsgo's. The same discord-api-types base cycle as
  npm_dmptranscripts_TS2344 (agents/bugs/npmsample.md, `APIPartialGuild extends ... Pick<APIGuild, ...>` while
  APIGuild extends APIPartialGuild): which file a checker reaches first decides whether the cycle shows as TS2310 or
  as a short member set (TS2344), so that fix likely fixes both; check this project's K-dependence with it. The clone is
  kept on the second box. To reproduce on another box: discordjs-choice-selectmenu-builder@2.1.0,
  https://github.com/LeCarbonator/discordjs-choice-selectmenu-builder at 63ad5355, candidate i=93 of
  test/npmsample/20261107.candidates.jsonl (setup.jsonl has its install and bench-config changes); `scripts/projdiff.py`
  on the clone shows both runs. Fixed by cor10's base-cycle change (members.c, "An interface's base cycle is found by its bases alone"), verified on a scratch install of discord-api-types 0.33.5 at K1..16, the defaults and --checker-memory 0. [fixed 2026-10-24]
- **npm_adonis5-gigya_TS2308** (seed 20261107, adonis5-gigya@2.0.0, renzosunico/adonis5-gigya adonis-typings/index.ts; tsgo 65, tttt
  67, +2 TS2308) (no flip): `declare module 'ioc' { export * from 'ga'; export * from 'gb' }` where both packages export a type
  alias `RequestParams` (one generic): tttt reports "has already exported a member named" TS2308, tsgo nothing. Guess: tsgo
  reports the export-star conflict only for a module file / non-ambient module (or when the two aliases are identical in kind),
  tttt checks it inside an ambient module declaration too. 15 lines. Cause (det3): tsc reports `export *` conflicts where getExportsOfModule computes the exports. For a module file that's checkExternalModuleExports when the file is checked. For an ambient module it's only when something asks for its exports (an import from it, an `export =` in it), and with one checker in program order the declaring file's diagnostics are already collected when a later file asks (tsgo at several checkers drops a later file's report too, the owner rule). tttt reported every conflict in name resolution. Now an ambient module's conflicts are kept only with an `export =` or a module name resolving to it in a file no later than the `export *`'s (SymbolModule.first_ref_order_plus1). Test: also ttttAmbientExportStarConflictAsked. [fixed det3]
- **npm_ml-component_TS2551** (seed 20261107, ml-component@1.0.8, menglimu/ml-components src/components/BaseInfo/index.tsx; tsgo 15,
  tttt 23, +8 TS2339/TS2551) (no flip): `Vue.extend({ data() { return { data_: {} } }, methods: { m() { this.data_ } } })`
  with Vue 2's types (reduced to 176 lines, mostly vue/types): tttt reports "Property 'data_' does not exist on type 'Vue'"
  where tsgo infers `this` from `data()`'s return. Guess: the `ThisType<CombinedVueInstance<...>>` of `Vue.extend`'s overload is
  resolved against a different overload (or its contextual `this` lost) when `data` is a method with no parameters under
  `strictNullChecks: false`/`noImplicitAny: false`. The vue2 fix covers it (the method signature's `this` from a later overload, members.c). [fixed corF 2026-10-24]
- **vuejs-core TS7006** (seed 20261107, vuejs/core packages/compiler-sfc/src/style/pluginScoped.ts(190), tsgo 5, tttt 3, 2
  tsgo-only TS7006 "Parameter 'ss' implicitly has an 'any' type") (no flip) UNREDUCED: `n.nodes[0].each(ss => { selector.insertAfter(last, ss); last = ss })`
  with `let last: selectorParser.Selector['nodes'][0] = n`; reduce.py kept all 209 files whole in 15 minutes, and the file's
  functions plus postcss-selector-parser's d.ts alone in one test file don't reproduce it (neither side reports it). Clone deleted;
  rerun from seed 20261107's records if wanted. [open]
- The batch's first compare crashed on the varies project (fixed: npmsample.py's NameError) after deleting the
  identical clones, so 20261107.results.jsonl has only the 5 kept projects; the other 52 of 57 were identical.

## Seed 20261108
- No divergence: 57 accepted of 57 set up, 56 byte-identical, 57/57 same (file, line, code), clean agreement 57/57, no
  K-dependence. The one text-only difference (unbywyd-postcss-color-extractor src/index.ts(108) TS5115: tttt's column is
  11, tsgo's 26) is not a divergence here. No repros.

## Seed 20261109
54 accepted, 50 byte-identical, 51 same (file, line, code), clean agreement 54/54, no K-dependence, no flips.
- npm__react-above_modal_TS2339 (@react-above/modal 0.3.9, demo/index.tsx(14,15)): tsgo reports 4 TS2339 "Property
  'Body'/'Surface' does not exist on type 'ModalFC'" that tttt misses (`createModal({theme})` returns a type whose
  members come from a conditional/mapped type over `TModal extends ModalFC`; tttt resolves it to a type that has them).
  Guess: constraint/instantiation of the generic return type `CreateModal<TModal, TOptions>` differs. The case fails on
  the errors and types components. Flip: no. [open]
- npm__bumblebee032_ng-feedback_TS2345 (@bumblebee032/ng-feedback 2.0.97, feedback-dialog.component.ts(249) 3 extra
  TS2345/TS18046 "Argument of type 'unknown' is not assignable to parameter of type 'Rectangle'"): rxjs
  `mouseDown.pipe(mergeMap((e: MouseEvent) => ... mouseMove.pipe(map(...), takeUntil(mouseUp))))` on `Observable<Event>`
  then `.subscribe(rec => this.drawRectangle(rec))`: tttt infers `unknown` for the pipe's output. Guess: inference
  through a callback whose parameter annotation (MouseEvent) is narrower than the source (Event) under bivariance, or an
  overload choice of `pipe`. The cut-down case is non-strict and fails only the types component in the harness (the
  errors component agrees there: a mismatch of the shape of the project, not reproduced as a diagnostic). Flip: no. [open]
- Unreduced, non-flip (reduce.py drops project references): berget-ai/r8s packages/eneo (tsconfig.bench.json, composite
  with `references` to packages/core): both report TS6059 "not under rootDir", tsgo at ../core/src/index.ts(1) naming
  core/src/jsx-runtime.ts, tttt at src/index.tsx(1) naming core/src/index.ts. The same area as the TS6059 entries in
  agents/bugs/harness.md (which root file/import gets the program error). With the plain tsconfig.json tsgo also reports
  TS6305 at src/index.tsx(449) (output not built) that tttt doesn't. [open]
- Odd: scripts/test.sh (valgrind, debug build) on the react-above case ends with "264 bytes possibly lost" in
  worker_thread_join (load_pool.c:215) via program_drop, with --exit-on-first-error: a leak-check report, not a memory
  error in the checker; not investigated. [triage]

## Seed 20261110
- 59 accepted, 57 byte-identical, 57/59 same (file, line, code), clean agreement 58/59, no K-dependence (no varies).
- **npm_ensnode_ensnode-sdk_TS2769** (seed 20261110, @ensnode/ensnode-sdk@1.16.0, namehash/ensnode at 566cab2 packages/ensnode-sdk/src/shared/currencies.ts(206); tsgo 0,
  tttt 1) **[flip]** (claimed: corB) (test: test/cases/npm__ensnode_ensnode-sdk_TS2769.ts, 22 lines): `prices.reduce((acc, price) => ({amount: acc.amount + price.amount, currency}), {amount: 0n, currency: firstPrice.currency}) as PriceType`
  with `prices: [PriceType, PriceType, ...PriceType[]]`, `<const PriceType extends Price>` (Price a union of interfaces with literal `currency`): tttt reports
  TS2769 "No overload matches this call". Guess: reduce's overload choice with the initial value's `currency` (a `const`-type-parameter-constrained literal) is
  widened or compared against the `U` inference differently (tsgo picks the `U` overload and then the assertion; tttt fails every overload). Root cause: `as PriceType` gives the call a contextual type, so the return mapper instantiates `initial: U` to the `const` PriceType, and tttt took the argument for a const context (`{ readonly amount: 0n; ... }`, which then failed against the callback's bigint). tsc's isConstContext asks getContextualType, which is the raw `U`. The first-pass argument checks (ty_check_literal_argument_const_given) now decide const-ness from the parameter type as written. Repro ttttConstContextFromRawParameter. [fixed 427b48792]
- **npm_elysia_TS2307** (seed 20261110, elysia@1.4.30, elysiajs/elysia at e037eca example/video.ts(1); no flip, tsgo's TS2345 on line 4 is the knock-on): the package's own
  package.json (name `elysia`, `exports` pointing at a `./dist/index.d.ts` that doesn't exist, nothing built) and a dev dependency `node_modules/elysia` (another
  version): tsgo resolves `import 'elysia'` to node_modules after the self-reference fails, tttt reports TS2307. tttt's self-name resolution returned
  "not found" definitively instead of falling through to the node_modules lookup when the exports target is missing. 8 lines. The same cause as augu/utils: the test passes since 667f403ca (det3). [fixed 667f403ca]

## Seed 20261111
- 52 accepted, 51 byte-identical, 51/52 same (file, line, code), clean agreement 51/52, no K-dependence (no varies).
- **npm__framelia_dashboard-server_TS2532** (seed 20261111, @framelia/dashboard-server@0.0.5, hungify/framelia at cb6fb0e packages/dashboard-server/src/model.ts(35); tsgo 0, tttt 1) (claimed: fixE) **[flip]**
  (test: test/cases/npm__framelia_dashboard-server_TS2532.ts, 7 lines): `s[c.status] += 1` with `noUncheckedIndexedAccess`, `s: Record<"queued"|"running", number> & {"masked-pass"?: number; total: number}`
  and `c.status: "queued"|"running"|"masked-pass"`: tttt reports TS2532 "Object is possibly 'undefined'" for the compound assignment target, tsgo nothing. Guess: tsgo
  doesn't add `| undefined` for the read of an element-access write target (compound assignment reads) when the key is a union over an intersection with an optional member,
  or its optional-member `undefined` is removed by the assignment's read type (getTypeOfExpression of the target under `CheckMode`); tttt keeps it. [open]
