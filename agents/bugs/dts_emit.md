# Declaration emit: what still differs from tsgo (plan 64, closed)

**Plan 64 closed 2026-10-16** (todo entry 5 of the eighteenth window). Its decisions that still hold: no synthetic AST
(the writer prints text from the source AST and `Ty`s, documentation.md "Declaration emit"); one walk with two sinks
(`declaration_emit.c`'s diagnostics walk, the writer a second pass in the checker that checked the file); tttt emits
only where the options emit declarations alone (`emitDeclarationOnly`), never JavaScript; no `.tsbuildinfo` (plan 76
decides incremental). Validation met: suite dts in the headline (1834 / 1859, JavaScript tests included since this
entry: 351 / 352), dtsdiff >= 90% of projects (96.8%), clidiff `--emit`; `dtsdiff.py --kdiff 16` differs on 3 / 292
of a quarter sample by tsgo's own per-checker undefinedProperties history (below, "Member order").

State 2026-10-19 (agent A, after refresh 2's projects): `scripts/dtsdiff.py` **1246 / 1274 projects, 99736 / 100826
files** in a worktree (1028 of the misses are corpus:effect's and corpus:tsc's `.d.ts.map` sources, a worktree
artifact; was 1242 / 99652 that morning). 2026-10-18 (todo entry 6, four agents): 1211 / 1233, 96675 / 96726; clidiff `--emit` 403 / 410. What the pass taught: tsc keys object instantiations
on the alias they're made for (an import's alias names an unaliased instance, `type N = Readonly<X>` is a type of its
own), checks type-parameter references syntactically (the Block rule; merged class/interface owners by node), and
makes a mapped tuple's error rest element unknown. **Most of what's left is tsgo's checker split** (its weighted
FENNEL file-to-checker assignment, compiler/checkerpool.go): the member order bucket (undefinedProperties) and single
files that match `tsgo --checkers 1` (babel-parser's predicate, sequelize's `models`, mcp-sdk's hono specifier);
triage.md has the tooling question.
`scripts/dtsdiff.py --last -k BUCKET` lists files, `--show PROJECT FILE` diffs one. Classes by
size on real code, each with an example:

- **JavaScript sources**: the suite's last one is jsDeclarationsTypeReferences4 (`export { thing }` inside a
  namespace, TS1194: the specifier's `source_symbol` is never resolved, `collect_exported_locals()` can't paint the
  local; TS has it too). Guesses from one or two tests each are in hacks.md ("JavaScript declaration emit guesses").
  JavaScript declaration diagnostics are reported like TypeScript's since this entry (no file written then); left
  on real code: query's `root.tsdown.config.d.ts.map` (17 projects, a JSDoc type literal member's mapped end) and
  create-t3-app's `prettier.config.d.mts.map` were fixed by todo entry 5's agent, as were the missing TypeScript files
  (mermaid, sequelize, zigbee2mqtt: none left).
- **Import specifier choice** (agent A 2026-10-15 ported tsgo 7.1's rules: augmented module names, the import lookup
  in the enclosing location's mode, no container cap, no package copies as containers, existing imports only through
  their realpath, program-wide dependency links by package directory, exports conditions by the target's format;
  `TTTT_DEBUG_CONTAINERS=1` shows the candidates). Left (~4 projects): mcp-sdk/examples
  `import("node_modules/hono/dist/types/hono")` vs tttt's `.../dist/types`: matches at `--checkers 1` (agent C
  2026-10-18), so it's the per-checker KEY_EXTENDED_CONTAINERS memo below with tttt's file distribution; babel's `eslint.config.d.ts` and react-three-fiber's `three`
  specifiers matched in 2026-10-19 runs. corpus:react's `@testing-library/react` vs `/dom` is a worktree
  artifact (the right one from the real path). tsgo's checker-wide memo of a successful import lookup
  (KEY_EXTENDED_CONTAINERS, filled from inside an import declaration) makes names depend on what a checker printed
  first, as in tsgo; tttt's checkers don't split files like tsgo's.
- **Member order** (agent C 2026-10-16 fixed arktype's filtering `as` clauses, JS classes' `this.x` members, the
  isDeclarationContainedBy offset quirk of mixins): tsgo sorts every resolved object type's properties by
  compareSymbols (getNamedMembers), tttt sorts at Members build (builder_sort_stable). What's left:
  - `?: undefined` members of normalized object literal unions (postiz-app `*.provider.d.ts`, slate
    `test/interfaces/Text/decorations/*`, excalidraw `actionElementLink.d.ts`, ui `registry/config.d.ts:241`; likely
    react-navigation's `getShadowStyle` `?: never` too): tsgo's getUndefinedProperty caches one symbol per name per
    checker, declared where the first property it was made for is, so the order depends on which files that checker
    met first (4 checkers, FENNEL's split; a gated project's emit walk, last file first under `--singleThreaded`).
    **tttt's choice is history-independent since 2026-10-21** (worker "filler", `undefined_choice()` in members.c,
    ty.h's UndefinedProps): one program-wide entry per name, the candidate first in declaration order among those some
    file's check meets, else among those only the emit walk does; prints of a choice a later candidate replaced are
    recorded per file and those files checked again at the end of program_typecheck()
    (`recheck_stale_undefined_props()`, `TTTT_DEBUG_UNDEFINED_PROPS=1` lists them, `=all` rechecks every file that
    printed one: its own oracle). vscode at the defaults 9929 / 9929 (was 9925: recycling reset the old per-checker
    first-need cache, the terminal xterm recordings), byte-identical at K1, `-m 0`, K4, K12 and `--shared-graph` K1/K12;
    a quarter sample (`--sample 4`) as before. The full run (2026-10-21, after the merge) lost hoppscotch-backend
    and vitest/packages/ui against the first-need emulation: candidates are now ordered by where their widening
    context starts first (`WideningCtx.first_pos`: tsc's getPropertiesOfContext takes the *last* sibling with the name,
    so an earlier context's candidate can sit after a later one's; hoppscotch's mock-server example, a big literal of
    nested arrays, printed `grantTypeInfo?` first), which brought hoppscotch back: dtsdiff 1327 / 1350, files 105143.
    vitest/packages/ui `client/composables/explorer/{state,search}.d.ts` stays lost: a `computed(() => ...)` arrow
    whose returns `{ matcher }` / `{ matcher, error }` / `{ matcher }` (state.ts:58-73) call createSafeFilter(), a
    function declared after it whose own return union (`{ matcher, error }` at :83) tsgo widens first, while checking
    the arrow's body before widening the arrow's returns, so tsgo's `error?` is :83's and sorts last; tttt's is the
    arrow's (:68). What's left is where tsgo's first need isn't declaration order: a
    literal union in a deferred function body (an arrow's) declared before a top-level one tsgo checks first (filetest
    shape: `const run = (): number => { const v = [{ x: 1 }, { y: 1 }]; return v.length; };` then `export const b =
    [{ y: 1, z: 1 }, { x: 1 }];`: tsgo `x?` last, tttt first), and tsgo's own split (postiz, slate tests, ui, whose
    `--singleThreaded` differs from its default). Whether a check met a candidate deferred or not depends on history
    (a warm checker skips what an earlier deferred check cached), so that order isn't emulated. tsgo's baseline harness
    is history-dependent here too (a two-file test with the costlier file second: its `.d.ts` differs from tsgo's CLI):
    no `test/cases` repro is possible, the harness's one checker in program order meets candidates in about
    declaration order.
  - qwik mcp `page.d.ts`: union order, checker-side. `inc ? { ...signal, value } : signal` with `signal` a parameter's
    rest binding (`({ value, ...signal }: T)`) is `{ name; hookType } | { name; hookType; value }` in tsgo (the rest
    type made first) and the other way in tttt (filetest repro; a declared `signal` matches). tttt's rest type
    (object_rest_type, rest_literal) gets a later id than the spread's object.
  - trpc next-prisma `context.d.ts`, ui `registry-health/directory.d.ts`: uninvestigated.
- **Checker types, uninvestigated** (agent C's leftovers, 2026-10-15; effect's query-repro fixed 2026-10-18 by agent
  D's permissive wildcard fixes): excalidraw `tests/helpers/api.d.ts` (tsgo keeps `declare
  const { h }: Window & typeof globalThis;`); feathers `next?: HookContext` (tsgo names the unresolved NextFunction
  through another unresolved import alias, the unknown-symbol quirk below).
- **isTypeParameterPossiblyReferenced leftovers** (agent A 2026-10-18 did the Block rule and merged owners by node:
  `TypeFlow.scope`, a function body's Block or a class/interface declaration's owner, set by the resolve walk; the
  order-dependent `LowerCtx.merged_owner` is gone). Not modelled: the `this` type of a class around a method body
  (tsc's Block rule makes it possibly referenced; the Block marker carries only OuterGenerics, which a class without
  type parameters has none of, and the deferred body walk doesn't see the class's marker), and a context-sensitive
  function's inferred signature type parameters (tsc's getOuterTypeParameters adds them).
- **`typeof this` in an object literal, types baseline** (agent C 2026-10-16 fixed the declaration side: headlessui's
  TS5088 matches). Printing a cyclic object literal whose methods mention `typeof this`, tsgo expands one level more
  than tttt: its `this` there is the fresh object literal type (getContextualThisParameterType without a contextual
  type: getWidenedType of the literal's own, fresh, type), a different type from the variable's regular one, so the
  printer's visited check lets it through once. tttt's contextual this is the regular type. Checker-side (the type of
  `this` in object literal methods); no suite test shows it.
- **Content-mapped files' outputs**: tsgo names a `.vue` file's declaration output `x.d.vue.ts`
  (ChangeToDeclarationExtension with the content mapper's extensions) and emits the supplemental module
  (`component.vue.0.d.ts`, `/// <reference path>` to it); tttt writes `x.vue.d.ts` and no supplemental one
  (`contentMapperSupplementalModule`, suite dts component).
- **Worktree artifacts**: dtsdiff run in a worktree with symlinked corpora differs in `.d.ts.map` `sources` and in
  TS2883 "cannot be named without a reference to '/home/ai/tttt/...'" (excalidraw-app/collab): not bugs.

- **B's second pass leftovers** (2026-10-15): trpc's `TypeError<'...'>` property of CreateTRPCClientOptions (tsgo reuses the
  annotation, tttt reprints with `| undefined` and double quotes; a two-file probe matches, the real case doesn't);
  arktype bounds `OneCharComparator` and vscode instantiationService `const enum TraceType` locals tsgo keeps
  (painting, not reproduced small).
- **"type text" leftovers** (agent B 2026-10-16 fixed trpc's alias/spread reuse and AsyncIterable args, feathers'
  mapped literals and contravariant aliased intersections, fumadocs' implied infer constraints, ts-mixer's abstract
  constructor parens, tanstack-table's contextual intersection, slate, vitest, nextra, pi keybindings). Left:
  - ui `registry/config.d.ts` (its ZodEnum fixed 2026-10-18): left is `parseRegistryBaseParts`'s `error?: undefined`
    member order, tsgo's undefinedProperties split of "Member order" above.
  - feathers generators `language: "js" | "ts"` (tsgo prints, no comments, an intersection member repeated) after a
    long `.then()` chain of prompts: checker-side identity. `initializeBaseContext()` (commons.ts, a generic arrow
    returning `{ ...ctx, dependencyVersions: { ...pkg.devDependencies, ...ctx.dependencyVersions, '@feathersjs/cli':
    version } }` through `.then`s) is applied twice: tsgo's result keeps two distinct `{ '@feathersjs/cli': any }`
    intersection members (instantiations of the literal under two mappers), tttt makes one (it interns them), and
    tsgo's spread type no longer reuses the properties' nodes. Not reduced (agent B 2026-10-18).
  - react-scan `Component<P>` vs `Component<Attributes, {}>`: fixed (a local function with expandos is an anonymous
    type now, instantiated: is_local_expando_function()). The `Readonly<{...}>` reprinted where tsc
    no longer reuses an alias body's annotation: hacks.md "An alias body's homomorphic mapped instance".
    react-three-fiber `ReadonlyArray<...>` reuse: see agent A's note. mockoon
    `deploy.service.d.ts` `Observable<any>` (tttt `Observable<DeployInstance>`): only under the root `mockoon`
    config (`scripts/tsctypeat.sh` says `instance: any` in the `tap` after `switchMap` there, DeployInstance under
    packages/app's); `forkJoin([any, any])` is `Observable<[any]>` in both and an rxjs probe with a local `post<T>`
    matches, so it's in Angular's `HttpClient.post<T>` overloads or the unresolved services, not reduced. vueuse
    `.test/mount.d.ts`: `defineComponent({ components: Comp, ... })` where tsgo prints Comp's `DefineComponent<...>`
    expanded (its constructor type & ComponentOptionsBase...) and tttt keeps the alias: alias lost somewhere in Vue's
    inference, not reduced. headlessui's use-controllable matched in 2026-10-18 runs (an order effect, may come back). effect's and excalidraw's others: see "Checker types, uninvestigated"
    (effect ExecutionPlan and fiberRuntime fixed 2026-10-16).
  - `export default undefined`: tsgo writes it as an identifier export, tttt as `declare const _default: undefined`.
- **Fixed 2026-10-20 (agent A)**: react-three-fiber's react-native mock: the rest of a mapped type over a mapped type
  (`Readonly<Omit<ViewProps, 'children'>>`) reuses the annotations of the properties the inner one maps
  (lower.c's mapped_prop_annotation(), ttttRestOfNestedMappedReusesAnnotation). The "map: its .d.ts differs" bucket is
  the maps of files whose .d.ts are in other buckets (babel-parser, mcp-sdk, slate). telegraf's map and vscode's
  modifier unchanged (entries below and above).
- **Painting gaps** (dtsdiff "import"; agent A 2026-10-15 fixed inferred predicates, late bound members, readonly enum
  fields, enum-valued keys, every statement of a painted merged name, private constructors' parameter properties,
  painted destructurings' types; agent C 2026-10-16 fixed unresolved-import names reused across files, export lists
  past 64 names, a namespace's export list, value-only `export =` property imports used as types, the first walk
  printing without type parameter names (discord.js's ActivityType), augmentation-kept imports (vue)). What's left in
  the bucket is type text whose names differ (checker-side, the import line follows; agent D 2026-10-18 fixed yup's
  imported distributive alias, gemini-cli's `f?: T` generic argument instantiation, effect's late-painted merged const):
  excalidraw `ExcalidrawFreeDrawElement`, slate-hyperscript.
- **Reused nodes' text, remaining** (agent B 2026-10-15 fixed escapes, comments around type nodes, extends-clause
  parentheses, `typeof f` in unions, empty multi-line tuples, synthesized literals): still open, each small:
  a parenthesized type's inner node's comments (`( /** c */ A)`: tttt keeps one `parenthesized` flag, no node);
  comments on their own line before a list delimiter (emitListItems' emitLeadingComments(previousSibling.End()));
  comments of nodes the node builder copies (tsgo's clones keep positions: `setTextRange`; a union's or intersection's members' done 2026-10-18 when in the file being written, display_node_list()); trpc's `'global-errorFormatter'`
  of an `as const` pseudo type mapped where tsgo doesn't map it (dtsdiff `map: extra at string (message)`,
  `'RATE_LIMIT`); feathers' `language: 'js' | 'ts'` and comments of a spread's properties where tsgo prints the type
  (`"js" | "ts"`, no comments, an intersection member repeated: a spread of an intersection with prompt answers,
  checker-side, see "type text" leftovers).
- **Unknown-symbol naming (tsgo quirk, matched)**: tsgo names an unresolved type through any import alias that also
  resolves to the unknown symbol (`node is isIdentifierChar`, babel-parser with unbuilt workspace deps; a reused
  annotation from another file, `Plugin<Plugin>`, ttttDeclarationUnknownImportRenamed; a reused `typeof X` too,
  ttttDeclarationUnknownImportTypeof) and paints that import; a
  type printed from its type (not a reused node) may still not paint it (unverified).
- **Declaration maps** (stage 5 done; agent C 2026-10-16 modelled tsgo's per-enclosing-declaration serializedTypes
  cache, TyPrintedTypes: a type printed again in the same scope is an unmapped clone, a signature with parameters a
  new scope; mapped types left out since tttt interns instantiations tsgo doesn't). `scripts/dtsdiff.py -k 'map:'`
  lists the rest, single files: babel-types `_skip`, lit (`NodeListOf`, `[Symbol`, `'_` constructor), redux-toolkit
  helpers `K (})`, telegraf `(tg`. Agent B 2026-10-18: the cache is per *enclosing declaration*, which a class's
  property declarations share (the class) and a function's parameters share (Walk.class_printed_types,
  printed_next); a keyword annotation's file is its signature's (TyDisplay.node_file); a class literal's methods
  tsgo instantiates for `this` have fresh type parameters (method_this_instantiated(): not thisless, or more than
  one declaration, and some declaration possibly references `this`, e.g. an implementation without a return type).
  Not modelled: the printer flags in tsgo's cache key; a type reprinted in a new signature scope misses mappings
  tsgo has (probe: an object literal with `use: () => p, withArg: (x: number) => p` of an inline conditional).

## 2026-10-16 worktree agent (wt-a): JS maps / missing files / small buckets, leftovers
- **telegraf `as (tg.MessageEntity & { type: SelectedTypes; ... })[]`**: tsgo reuses the node with only the local alias
  reference replaced (partial reuse); tttt prints from the type (same text, no mappings).
- Suite JS dts quirks left as tsgo bugs: jsdocTemplateTagDefault (`= ` empty default), jsdocReadonlyDeclarations
  (`readonly constructor`), jsdocAccessibilityTagsDeclarations (`c;`).

## 2026-10-18 worktree agent B: babel-parser and friends
- **babel-parser `maybeAsyncOrAccessorProp(...): boolean`** (tsgo) vs tttt's `prop is typeof prop & {...}`: a tsgo
  checker-distribution quirk, not emulated. `tsc --singleThreaded` prints the predicate like tttt (so do
  `--checkers 1/2` on the probe below; 3+ print `boolean`). In a checker that resolves `new A().m` before checking
  A's file, the signature's parameters are instantiated copies, `typeof prop` in the reused predicate resolves to the
  original parameter but the fake scope has the copy (trackExistingEntityName's getSymbolIfSameReference fails), the
  reuse fails and reuseTypeNode's fallback is getTypeFromTypeNode(predicate) = boolean. Probe: types.ts with two
  interfaces, a.ts `class A { m(prop: OP | OM): prop is typeof prop & { key: string } }` (a union parameter
  matters), c.ts `export const v = new A().m;` with `--checkers 3`. tsgo single-threaded also differs from its
  default on babel-parser's `parseVarStatement`/`parseExportSpecifier` (`any` by default, as tttt).

## 2026-10-18 worktree agent D leftovers
- **sequelize/packages/core `readonly models: any`** (tsgo) vs tttt's `ModelSetView<Dialect>`: tsgo itself prints
  `ModelSetView<Dialect>` with `--checkers 1` and `any` with its default 4: a circularity that depends on which checker
  checks what first (the TS7022 that differs by checker count in tsgo too, CLAUDE.md's kdiff note). Not worth
  emulating without tsgo's file-to-checker assignment (the "Member order" item's plan).
- A type literal the node builder reuses twice in one declaration (a parameter, then the return type `x`): a member's
  trailing comment is written the first time only in tttt, both times in tsgo (probe: `memo((x: { a: number, /** b */
  b: 1 }) => x)`).


## Refresh 2 leftovers (agent A, 2026-10-19, todo entry 6 of the window)
Fixed: vest's `typeof ns.x` (a spread namespace's alias export is the alias: spread_alias), the checkJs projects'
maps and types (rocket 315 / 316, eslint-plugin-jsdoc 336 / 338, markdown-magic and its packages all match), lit's
class literal fields (pseudo types), babel and react-three-fiber specifiers (match now, not by a fix of this pass).
Left:
- **vest lazy.d.ts, a tsgo bug**: `[K in keyof TRules]: (...args: DropFirst<Parameters<TRules[K]>>) => ...` instantiated
  per key; tsgo names the instantiated alias by a wrong symbol (`arrayRules.isEmpty<Parameters<typeof
  arrayRules.equals>>`, `isEmpty` being another export of the module), tttt writes the annotation's text with K and
  TRules out of scope. tttt's is wrong too: it should print the instantiation (`DropFirst<Parameters<typeof
  arrayRules.equals>>`), which still won't match tsgo. Not worth emulating tsgo's symbol mix-up.
- **eslint-plugin-jsdoc**: 5 files match `tsgo --singleThreaded` (checker split); its maps, rocket's `{string=}` and
  markdown-magic's all match since agent A's second pass (2026-10-19).
- **`/** @type {{a: number}} */ export default { a: 1 }`**: tsgo maps the member to the literal's property when the tag's
  type and the literal's agree (with an extra optional member it maps the tag, like tttt).
- **telegraf** partial reuse (below, unchanged: tsgo reuses the asserted node with only a local alias reference
  replaced, a reuse mode tttt's printer doesn't have; not cheap), **vueuse `.test/mount.d.ts`** mismatches again (also at this
  pass's base commit) after matching in the morning's full run: an order or environment effect.

## 2026-10-20 agent C (real-code fan-out)
Fixed: ts-proto's `| //` then `/** doc */` union members (lazy indentation, ttttDeclarationUnionMemberLineThenJsdoc),
snaps-cli's `builtIns?: any` (a return-mapped contextual type that is any falls back to the parameter type),
effect matcher's template literal map (types mapped at their starts), headlessui-vue's `$slots: Slots` (an alias
body's twin turns back only if tsc's instance could contain type variables), react-three-fiber's class-expression
static order (own statics first). dtsdiff in a worktree: 1326 / 1350. Verdicts:
- **vscode terminal recordings `readonly id?: undefined`** (the "modifier" bucket, 4 files): tsgo's getUndefinedProperty
  copies the first property it was made for (createSymbolWithType keeps CheckFlags.Readonly), cached per name per
  checker; tttt's K1 meets a readonly `id` first, tsgo (and `--singleThreaded`, whose hashes don't match tttt either)
  a mutable one. `TTTT_CHECK_ONLY` on the file gives yet another order. The undefinedProperties history of "Member
  order" above; feathers generators' connection/index.d.ts and postiz orchestrator's autopost.service.d.ts are it too.
  (vscode's matched at the defaults since the program-wide choice, 2026-10-21; the others not rechecked)
- headlessui-vue: `--singleThreaded` writes what tsgo's default does (cached); its last file matches since the twin fix.
- Left as before: babel-parser, mcp-sdk (checker split), slate test / postiz / ui (undefinedProperties), feathers
  `language`, mockoon, vest (tsgo bug), telegraf's partial reuse map (react-three-fiber's mock fixed since).
