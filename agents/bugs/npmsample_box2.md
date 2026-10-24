# Random npm projects: divergences found by the second box (seeds 20261100-20261199)
Same conventions as agents/bugs/npmsample.md (that file has the first seeds of this box too); a separate file so the two
boxes' appends never conflict. One section per seed, one bullet per repro, status tags as in agents/bugs/README.md.

## Seed 20261106
- npm_rabbitmq-pub-sub_TS2339 (rabbitmq-pub-sub@0.2.5): `import * as Promise from "bluebird"` (@types/bluebird: `declare class Bluebird<R>` with statics, merged namespace, `export = Bluebird`) under commonjs/bundler, esModuleInterop defaulting on: tsgo reports TS2339 "Property 'resolve' does not exist on type 'typeof Bluebird'" for `Promise.resolve(...)` (a namespace import of an `export =` class gets a synthetic namespace object without the class's statics), tttt resolves the static (-20 diagnostics in the project: TS2339, TS2322, TS7006 downstream). Same cause as serverless-openapi (det3): the synthetic-default spread of the `export =` class keeps its static methods, which tsc's isSpreadableProperty drops. The test passes since ecee294e1. [fixed ecee294e1]
- npm__overlastic_vue2_TS2339 (@overlastic/vue2@0.9.0, vue 2.6 types): tttt extra TS2339 "Property '$visible' does not exist on type 'Vue'" (and `$overlay`) for `this.$visible` in `methods` of `Vue.extend({computed: {$visible: {get(){}}}, methods: {...}})`; tsgo infers the ThisType from the computed accessors. The repro is the reducer's multi-file cut (the vue types with unresolved names; a cut of my own to one file stopped reproducing). Guess: the ThisType<CombinedVueInstance<...>> intersection, with Computed inferred from `Accessors<Computed>` through the overload, loses the `$`-prefixed key. Fixed: the cause was overload resolution, not ThisType. `this` from an earlier candidate's contextual signature (the second overload's DefaultMethods `(this: V)`) outlived it when the settled first overload gave none (assign_contextual_signature_as). [fixed corF 2026-10-24]
- npm__overlastic_vue2_TS2339_strictmode.tsx (same project, playgrounds/react): `paths` maps `react` to a script file `global.react.d.ts` holding `declare module 'react' {...}`; tsgo reports TS2339 on `<React.StrictMode>` ('StrictMode' does not exist on type 'typeof import("react")'), the repro shows tttt reporting the opening tag but missing the closing tag's (2,37); in the project tttt reports neither. Guess: the property access on a JSX closing-tag name isn't checked, and the opening one differs by the paths-mapped ambient module resolving. Fixed: the closing tag's name is now checked (jsx_element_typecheck); in the project the second vue2 fix made the whole output byte-identical, and no separate paths divergence showed on a fresh clone. [fixed corF 2026-10-24]
- Unreduced: 0061 @point0/compiler@0.4.2 (packages/compiler, ../core/src/point0.ts, a 22k-line file) (claimed: cor10): tttt-only 18 diagnostics, tsgo 1: TS2345 "Argument of type '{ input: void | InputRawUnknown; point: AnyPoint; error: undefined; output: undefined; }' is not assignable to parameter of type '{ input: Inp...'" (lines 17648, 17768-17791), TS2349 "This expression is not callable" (19142), TS2322 "Type 'void | InputRaw' is not assignable to type 'InputRaw'" (20532), more TS2345 at 20499-20723; reduce.py got nowhere in 5 minutes on the whole file. Looks like one generic-conditional bug (`TSchema extends InputSchema ? InputRaw<TSchema> : undefined`-style inputs). Not a flip (tsgo already has an error). Fixed by cor10, two causes: (1) 17 of the 18: `{ input = {} }: { input: InputRaw | undefined | void }` kept void
  (tsc's getNonUndefinedType removes void with undefined; non_undefined_type in lower.c), repro
  npm__point0_compiler_TS2345; (2) the TS2349: `typeof input === 'function'` intersected `EmptyObject & {...}` with
  Function because `Record<never, never>` counted as an empty anonymous object (tsc's isEmptyAnonymousObjectType wants
  ObjectFlags.Anonymous; is_empty_anonymous_object in ty/flow.c), repro npm__point0_compiler_TS2349. The project is
  byte-identical with tsgo now. [fixed 2026-10-24]
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
  the errors and types components. Flip: no. The errors part was already fixed on main (the test's errors pass at 659bb9b75, after ed0e4d132, most likely by 00c607932's JSX closing-tag names). The types part: `import("../src/types/index.js")` where tsgo prints `import("../src/types")`. Inside a signature with parameters or type parameters, tsc's enterNewScope makes a fake scope the enclosing declaration, so the import specifier around the name no longer gives the originalModuleSpecifier (ESM usage, a `.js` ending), though the chain still finds imports through it. tttt applied the usage everywhere in the printed type (TyDisplay.usage_in_fake_scope now). And the containers' sortByBestName compares specifiers in that usage mode (`./types/index.js` ties `./types/shared.js`), where tttt's parent_specifier used none. Repro ttttImportUsageModeSpecifierScope. (claimed: corB) [fixed f7d042e0b]
- npm__bumblebee032_ng-feedback_TS2345 (@bumblebee032/ng-feedback 2.0.97, feedback-dialog.component.ts(249) 3 extra
  TS2345/TS18046 "Argument of type 'unknown' is not assignable to parameter of type 'Rectangle'"): rxjs
  `mouseDown.pipe(mergeMap((e: MouseEvent) => ... mouseMove.pipe(map(...), takeUntil(mouseUp))))` on `Observable<Event>`
  then `.subscribe(rec => this.drawRectangle(rec))`: tttt infers `unknown` for the pipe's output. Guess: inference
  through a callback whose parameter annotation (MouseEvent) is narrower than the source (Event) under bivariance, or an
  overload choice of `pipe`. The cut-down case is non-strict and fails only the types component in the harness (the
  errors component agrees there: a mismatch of the shape of the project, not reproduced as a diagnostic). Flip: no. Cause: the outer `pipe` call fails on its argument (the callback's `MouseEvent` against the stream's `unknown`), and tsc's failure fallback (inferSignatureInstantiationForOverloadFailure, under SkipGenericFunctions) still infers from the inner `mergeMap(...)` call because getResolvedSignature returns its cached signature; tttt's infer_type_arguments skipped generic calls under skip_generic even when resolved. Test redone by hand from rxjs 6.3.3's shapes. [fixed f29924ac6]
- Unreduced, non-flip (reduce.py drops project references): berget-ai/r8s packages/eneo (tsconfig.bench.json, composite
  with `references` to packages/core): both report TS6059 "not under rootDir", tsgo at ../core/src/index.ts(1) naming
  core/src/jsx-runtime.ts, tttt at src/index.tsx(1) naming core/src/index.ts. The same area as the TS6059 entries in
  agents/bugs/harness.md (which root file/import gets the program error). With the plain tsconfig.json tsgo also reports
  TS6305 at src/index.tsx(449) (output not built) that tttt doesn't. (claimed: fixE) Cause: (1) the implicit jsx-runtime import of a referenced project's unbuilt source said nothing (tsc's resolveExternalModule reports TS6305 at the first JSX tag, getJsxNamespaceContainerForImplicitImport); (2) a synthesized import (jsx runtime, importHelpers) has no location: tsc's createDiagnosticExplainingFile reports at the first written import instead, and its chain line is `Imported via "SPEC" from file ... to import 'jsx' and 'jsxs' factory functions`. The seed's bench config extends tsconfig.json, so it has no references (core's sources come in through paths). Scenarios own/reference-jsx-runtime-unbuilt, own/rootdir-synthetic-jsx-import-location. [fixed 6ccb6f365]
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
  or its optional-member `undefined` is removed by the assignment's read type (getTypeOfExpression of the target under `CheckMode`); tttt keeps it. Cause: a compound assignment target is written too (tsc: AccessFlags.Writing for any assignment target), so a union key gives the intersection of the keys' write types, `number`; tttt read it as the union. [fixed 16c3cbbd5]

## Seed 20261112
- No divergence: 63 accepted, 62 byte-identical, 63/63 same (file, line, code), clean agreement 63/63, no K-dependence. The one text-only difference (0088-coinset-liquid:
  a TS7016 "There are types at ..." message naming a path under node_modules/.pnpm) is not a divergence here. No repros. (Set up without `--prefer-large`: the flag
  arrived after this seed's setup had started.)

## Seed 20261113
- No divergence (set up with --prefer-large): 56 accepted, 56 byte-identical modulo lib paths, 56/56 same (file, line, code),
  clean agreement 56/56, no K-dependence, no flips. No repros.

## Seed 20261114 (setup with --prefer-large)
- 68 accepted, 67 byte-identical, 67/68 same (file, line, code), clean agreement 67/68, no K-dependence (no varies).
- **npm_payload-storage-cloudinary_TS2322** (seed 20261114, payload-storage-cloudinary@1.3.0, nick-vogel/payload-storage-cloudinary at 2086612 src/index.ts(377); tsgo 0, tttt 1) **[flip]**
  (test: test/cases/npm_payload-storage-cloudinary_TS2322.ts, 12 lines, found without reduce.py from the message): `colls.map(c => ({...c, forceSelect: {...c.forceSelect, ...f()}}))`
  assigned to `Coll[]`, where `forceSelect?: SelectType` = `SelectExcludeType | SelectIncludeType` (recursive index-signature objects, `[k]: false | SelectExcludeType` /
  `true | SelectIncludeType`) and `f(): Record<string, true>`: tttt reports the spread's type `{[x: string]: true} | {[x: string]: boolean | SelectExcludeType} | {...}` not assignable to
  `SelectType | undefined`. Guess: spreading a union with an index-signature member into another object produces the cross-product, and tttt's member `{[x:string]: boolean | SelectExcludeType}`
  (the exclude member merged with `true`) is kept where tsgo's spread (getSpreadType over unions, index-signature union of the right side) yields a type that is assignable, e.g. it
  drops or keeps the string index differently; compare the two printed spread types. (claimed: fixF) Cause: the `undefined` member of
  `forceSelect`'s union, spread first, left tttt's alternative as tsc's emptyObjectType, so the later `...Record<string, true>` copied its
  index signature in whole (`{ [x: string]: true }`); tsc's getSpreadType makes a new object for null/undefined (only the other primitives
  return the left side), so getUnionIndexInfos keeps none and that alternative is `{}`. spread_acc_type (ty/expr.c) now pushes a non-empty
  part for it, and after a generic left side intersects it (`T & undefined` never for `T extends object`, the alternative dropped). Repro
  ttttSpreadNullableUnionMemberIndex. [flip] [fixed PENDING]

## Seed 20261115 (--prefer-large)
53 accepted, 52 byte-identical, clean agreement 52/53, no K-dependence.
- **FLIP, unreduced**, 0004-youra-h-soldy (@soldy-ui/webc 0.3.0-alpha.2, https://github.com/youra-h/soldy at a399d8d3,
  packages/ui/webc/tsconfig.bench.json, candidate i=4 of test/npmsample/20261115.candidates.jsonl; setup.jsonl has its
  install): tsgo 0 errors, tttt 203, all TS2883, e.g. `../../setup/content/descriptors/components/button.descriptor.ts(12)`
  "The inferred type of 'ButtonDescriptor' cannot be named without a reference to 'TReadyPlugin' from
  '../../../../../node_modules/@soldy-ui/plugins/src'" (the file `export const ButtonDescriptor = defineDescriptor(() =>
  defineComponent({...}))`; `declaration: true` from tsconfig.base.json). The workspace is npm-linked: node_modules/@soldy-ui/*
  are symlinks to packages/*, and tsconfig `paths` maps `@soldy-ui/plugins` to `../../plugins/src` (the package has no
  dist, so only paths resolves it). tttt names TReadyPlugin's file by the symlink path (node_modules/@soldy-ui/plugins/src)
  and finds no specifier; tsgo names it. Not reduced: reduce.py's copy turns symlinks into regular files and then does NOT
  reproduce (tsgo and tttt agree on the copy), so the trigger is the symlink/realpath handling in module-specifier
  generation (getSymbolChain/containers: the symlink-cache candidate, `TTTT_DEBUG_CONTAINERS=1`), and a scratch project
  with a symlinked workspace package, paths, a deep re-export and a declaration-emitting inferred type did not reproduce it
  (the real call goes through `defineDescriptor`/`defineComponent` generics). The clone is kept on the second box at
  thirdparty/npmsample/20261115/0004-youra-h-soldy; `scripts/projdiff.py <clone>/packages/ui/webc/tsconfig.bench.json`
  shows it. Fixed: module_specifiers.c's normalize() dropped a `paths` pattern's leading `..`s, so `../../plugins/src` never matched and the only name left was the node_modules link's relative path; test ttttDeclarationPathsPatternParentDirs. [fixed corF 2026-10-24] [flip]
- soldy TS2883, second try with `// @link:` (test/cases-style, root /tsconfig.json with `paths` and `declaration`, a workspace
  package /packages/plugins linked at /node_modules/@s/plugins with `exports`/`types` into an unbuilt dist, a setup package
  depending on it via package.json `workspace:*`, TReadyPlugin deep re-exported, imported in one file and reaching the
  exported descriptor's inferred type as `{ plugins: [{ ctor: typeof TReadyPlugin }] }` through a generic `defineDescriptor`,
  with and without noEmit): tsgo and tttt agree (no error) in every variant, so no repro was committed. What the real project
  has that these lack: webc's tsconfig sits at packages/ui/webc with the checked files in ../../setup (outside it), and
  `defineComponent`'s generics carry TReadyPlugin through more layers (plugin bundles). The clone stays. (The missing
  ingredient was a `paths` pattern starting with `../`, see the entry above.) [fixed corF 2026-10-24] [flip]
