# Random npm projects: divergences found by the second box (seeds 20261100-20261199)
Same conventions as agents/bugs/npmsample.md (that file has the first seeds of this box too); a separate file so the two
boxes' appends never conflict. One section per seed, one bullet per repro, status tags as in agents/bugs/README.md.

## Seed 20261106
- npm_rabbitmq-pub-sub_TS2339 (rabbitmq-pub-sub@0.2.5): `import * as Promise from "bluebird"` (@types/bluebird: `declare class Bluebird<R>` with statics, merged namespace, `export = Bluebird`) under commonjs/bundler, esModuleInterop defaulting on: tsgo reports TS2339 "Property 'resolve' does not exist on type 'typeof Bluebird'" for `Promise.resolve(...)` (a namespace import of an `export =` class gets a synthetic namespace object without the class's statics), tttt resolves the static (-20 diagnostics in the project: TS2339, TS2322, TS7006 downstream). Same cause as serverless-openapi (det3): the synthetic-default spread of the `export =` class keeps its static methods, which tsc's isSpreadableProperty drops. The test passes since ecee294e1. [fixed ecee294e1]
- npm__overlastic_vue2_TS2339 (@overlastic/vue2@0.9.0, vue 2.6 types): tttt extra TS2339 "Property '$visible' does not exist on type 'Vue'" (and `$overlay`) for `this.$visible` in `methods` of `Vue.extend({computed: {$visible: {get(){}}}, methods: {...}})`; tsgo infers the ThisType from the computed accessors. The repro is the reducer's multi-file cut (the vue types with unresolved names; a cut of my own to one file stopped reproducing). Guess: the ThisType<CombinedVueInstance<...>> intersection, with Computed inferred from `Accessors<Computed>` through the overload, loses the `$`-prefixed key. [open]
- npm__overlastic_vue2_TS2339_strictmode.tsx (same project, playgrounds/react): `paths` maps `react` to a script file `global.react.d.ts` holding `declare module 'react' {...}`; tsgo reports TS2339 on `<React.StrictMode>` ('StrictMode' does not exist on type 'typeof import("react")'), the repro shows tttt reporting the opening tag but missing the closing tag's (2,37); in the project tttt reports neither. Guess: the property access on a JSX closing-tag name isn't checked, and the opening one differs by the paths-mapped ambient module resolving. [open]
- Unreduced: 0061 @point0/compiler@0.4.2 (packages/compiler, ../core/src/point0.ts, a 22k-line file): tttt-only 18 diagnostics, tsgo 1: TS2345 "Argument of type '{ input: void | InputRawUnknown; point: AnyPoint; error: undefined; output: undefined; }' is not assignable to parameter of type '{ input: Inp...'" (lines 17648, 17768-17791), TS2349 "This expression is not callable" (19142), TS2322 "Type 'void | InputRaw' is not assignable to type 'InputRaw'" (20532), more TS2345 at 20499-20723; reduce.py got nowhere in 5 minutes on the whole file. Looks like one generic-conditional bug (`TSchema extends InputSchema ? InputRaw<TSchema> : undefined`-style inputs). Not a flip (tsgo already has an error). [open]
- Unreduced: 0044 ozone-item-edit (taktik/ozone-components): the default include picks up binary `.ts` files (MPEG-TS video chunks, packages/ozone-material/ozone-video-player/test/data/3/.../chunk0000N.ts, 710 KB); both sides report syntax errors there but tsgo only TS1128+TS1434 at line 1 per file (42 diagnostics in all) and tttt 1943 (TS1127 Invalid character, TS1128, TS1434, TS1002...). Not a flip. Guess: the scanner's recovery on an invalid-UTF-8/binary file (tsgo stops after its first error: possibly it bails when the text isn't valid UTF-8 or contains NUL). A test file can't hold the bytes. [open]
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
  tttt checks it inside an ambient module declaration too. 15 lines. [open]
- **npm_ml-component_TS2551** (seed 20261107, ml-component@1.0.8, menglimu/ml-components src/components/BaseInfo/index.tsx; tsgo 15,
  tttt 23, +8 TS2339/TS2551) (no flip): `Vue.extend({ data() { return { data_: {} } }, methods: { m() { this.data_ } } })`
  with Vue 2's types (reduced to 176 lines, mostly vue/types): tttt reports "Property 'data_' does not exist on type 'Vue'"
  where tsgo infers `this` from `data()`'s return. Guess: the `ThisType<CombinedVueInstance<...>>` of `Vue.extend`'s overload is
  resolved against a different overload (or its contextual `this` lost) when `data` is a method with no parameters under
  `strictNullChecks: false`/`noImplicitAny: false`. [open]
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
