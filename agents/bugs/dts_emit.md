# Declaration emit: what still differs from tsgo (closed)

**Declaration emit closed 2026-10-16.** Its decisions that still hold: no synthetic AST
(the writer prints text from the source AST and `Ty`s, documentation.md "Declaration emit"); one walk with two sinks
(`declaration_emit.c`'s diagnostics walk, the writer a second pass in the checker that checked the file); tttt emits
only where the options emit declarations alone (`emitDeclarationOnly`), never JavaScript; no `.tsbuildinfo` (the CLI modes (parked: todo.md)
decides incremental). Validation met: suite dts in the headline (1834 / 1859, JavaScript tests included since this
entry: 351 / 352), dtsdiff >= 90% of projects (96.8%), clidiff `--emit`; `dtsdiff.py --kdiff 16` differs on 3 / 292
of a quarter sample by tsgo's own per-checker undefinedProperties history (below, "Member order": gone in the 7.1.0-beta).

State 2026-10-19 (after refresh 2's projects): `scripts/dtsdiff.py` **1246 / 1274 projects, 99736 / 100826
files** in a worktree (1028 of the misses are corpus:effect's and corpus:tsc's `.d.ts.map` sources, a worktree
artifact; was 1242 / 99652 that morning). 2026-10-18 : 1211 / 1233, 96675 / 96726; clidiff `--emit` 403 / 410. What the pass taught: tsc keys object instantiations
on the alias they're made for (an import's alias names an unaliased instance, `type N = Readonly<X>` is a type of its
own), checks type-parameter references syntactically (the Block rule; merged class/interface owners by node), and
makes a mapped tuple's error rest element unknown. **Most of what's left is tsgo's checker split** (its weighted
FENNEL file-to-checker assignment, compiler/checkerpool.go): the member order bucket (undefinedProperties, until the beta) and single
files that match `tsgo --checkers 1` (babel-parser's predicate, sequelize's `models`, mcp-sdk's hono specifier);
triage.md has the tooling question.
`scripts/dtsdiff.py --last -k BUCKET` lists files, `--show PROJECT FILE` diffs one. Classes by
size on real code, each with an example:

- **JavaScript sources**: the suite's last one is jsDeclarationsTypeReferences4 (`export { thing }` inside a
  namespace, TS1194: the specifier's `source_symbol` is never resolved, `collect_exported_locals()` can't paint the
  local; TS has it too). Guesses from one or two tests each are in hacks.md ("JavaScript declaration emit guesses").
  JavaScript declaration diagnostics are reported like TypeScript's since this entry (no file written then); left
  on real code: query's `root.tsdown.config.d.ts.map` (17 projects, a JSDoc type literal member's mapped end) and
  create-t3-app's `prettier.config.d.mts.map` were fixed on 2026-10-18, as were the missing TypeScript files
  (mermaid, sequelize, zigbee2mqtt: none left). [open]
- **Import specifier choice** (2026-10-15 ported tsgo 7.1's rules: augmented module names, the import lookup
  in the enclosing location's mode, no container cap, no package copies as containers, existing imports only through
  their realpath, program-wide dependency links by package directory, exports conditions by the target's format;
  `TTTT_DEBUG_CONTAINERS=1` shows the candidates). Left (~4 projects): mcp-sdk/examples
  `import("node_modules/hono/dist/types/hono")` vs tttt's `.../dist/types`: matches at `--checkers 1` (2026-10-18), so it's the per-checker KEY_EXTENDED_CONTAINERS memo below with tttt's file distribution; babel's `eslint.config.d.ts` and react-three-fiber's `three`
  specifiers matched in 2026-10-19 runs. corpus:react's `@testing-library/react` vs `/dom` is a worktree
  artifact (the right one from the real path). tsgo's checker-wide memo of a successful import lookup
  (KEY_EXTENDED_CONTAINERS, filled from inside an import declaration) makes names depend on what a checker printed
  first, as in tsgo; tttt's checkers don't split files like tsgo's. [tsgo]
- **Member order** (2026-10-16 fixed arktype's filtering `as` clauses, JS classes' `this.x` members, the
  isDeclarationContainedBy offset quirk of mixins): tsgo sorts every resolved object type's properties by
  compareSymbols (getNamedMembers), tttt sorts at Members build (builder_sort_stable). Closed 2026-10-23: the `?:
  undefined` members of normalized object literal unions (postiz, slate tests, excalidraw's actionElementLink, ui,
  vitest ui, vscode's terminal recordings, hoppscotch), qwik mcp `page.d.ts`, trpc next-prisma and ui
  registry-health all matched in a full dtsdiff run at 68adca3bc. [fixed oracle 2026-10-23]
- **Left at 2026-10-23** (full run from a worktree after corC's fixes: 1497 / 1503 projects, every corpus matching):
  - feathers/packages/generators `app/index.d.ts` (and feathers): `generate`'s chain applies pinion's
    `initializeBaseContext()` twice (strictNullChecks off). The two `{ '@feathersjs/cli': any }` members now match
    (corF 2026-10-23: an object literal's type is instantiated by every outer type parameter, like tsc's
    getObjectTypeInstantiation; test/cases/ttttObjectLiteralInstantiatedByOuterTypeParams.ts). Left: its `language`
    is `"js" | "ts"`, printed, in tsgo, where tttt reuses the `'js' | 'ts'` annotation. The intersection property
    has several declarations in tsgo (Base's and the two literal instances'), so declaration emit has no single
    valueDeclaration annotation to reuse. tttt still picks one. A cut-down with the same spreads matches, so it needs
    the real chain (pinion's loadJSON types and the `.then`s). [open]
  - kebo `models/root-store.d.ts`: mobx-state-tree's `IOptionalIType<IModelType<{...}>>` is `IOptionalIType<any,
    [undefined]>` in tttt (the eager signature design, checker). [dup]
  - babel-parser (one map), sequelize/packages/core `models`: tsgo's checker split (matches tsgo --singleThreaded or
    differs between tsgo's own runs); mcp-sdk/examples' hono specifier: as below. [tsgo]
- **Checker types, uninvestigated** (2026-10-15 leftovers; effect's query-repro fixed 2026-10-18 by the permissive wildcard
  fixes): excalidraw `tests/helpers/api.d.ts` (tsgo keeps `declare
  const { h }: Window & typeof globalThis;`); feathers `next?: HookContext` (tsgo names the unresolved NextFunction
  through another unresolved import alias, the unknown-symbol quirk below). [open]
- **isTypeParameterPossiblyReferenced leftovers** (2026-10-18 did the Block rule and merged owners by node:
  `TypeFlow.scope`, a function body's Block or a class/interface declaration's owner, set by the resolve walk; the
  order-dependent `LowerCtx.merged_owner` is gone). Not modelled: the `this` type of a class around a method body
  (tsc's Block rule makes it possibly referenced; the Block marker carries only OuterGenerics, which a class without
  type parameters has none of, and the deferred body walk doesn't see the class's marker), and a context-sensitive
  function's inferred signature type parameters (tsc's getOuterTypeParameters adds them). [open]
- **`typeof this` in an object literal, types baseline** (2026-10-16 fixed the declaration side: headlessui's
  TS5088 matches). Printing a cyclic object literal whose methods mention `typeof this`, tsgo expands one level more
  than tttt: its `this` there is the fresh object literal type (getContextualThisParameterType without a contextual
  type: getWidenedType of the literal's own, fresh, type), a different type from the variable's regular one, so the
  printer's visited check lets it through once. tttt's contextual this is the regular type. Checker-side (the type of
  `this` in object literal methods); no suite test shows it. [text]
- **Content-mapped files' outputs**: tsgo names a `.vue` file's declaration output `x.d.vue.ts`
  (ChangeToDeclarationExtension with the content mapper's extensions) and emits the supplemental module
  (`component.vue.0.d.ts`, `/// <reference path>` to it); tttt writes `x.vue.d.ts` and no supplemental one
  (`contentMapperSupplementalModule`, suite dts component). [open]
- **Worktree artifacts**: dtsdiff run in a worktree with symlinked corpora differs in `.d.ts.map` `sources` and in
  TS2883 "cannot be named without a reference to '/home/ai/tttt/...'" (excalidraw-app/collab): not bugs. [harness]

- **B's second pass leftovers** (2026-10-15): trpc's `TypeError<'...'>` property of CreateTRPCClientOptions (tsgo reuses the
  annotation, tttt reprints with `| undefined` and double quotes; a two-file probe matches, the real case doesn't);
  arktype bounds `OneCharComparator` and vscode instantiationService `const enum TraceType` locals tsgo keeps
  (painting, not reproduced small). [fixed oracle 2026-10-23]
- **"type text" leftovers** (2026-10-16 fixed trpc's alias/spread reuse and AsyncIterable args, feathers'
  mapped literals and contravariant aliased intersections, fumadocs' implied infer constraints, ts-mixer's abstract
  constructor parens, tanstack-table's contextual intersection, slate, vitest, nextra, pi keybindings). Left:
  - ui `registry/config.d.ts` (its ZodEnum fixed 2026-10-18): left is `parseRegistryBaseParts`'s `error?: undefined`
    member order, tsgo's undefinedProperties split of "Member order" above (the beta keys it by property: recheck). [fixed oracle 2026-10-23]
  - feathers generators `language: "js" | "ts"` (tsgo prints, no comments, an intersection member repeated) after a
    long `.then()` chain of prompts: checker-side identity. `initializeBaseContext()` (commons.ts, a generic arrow
    returning `{ ...ctx, dependencyVersions: { ...pkg.devDependencies, ...ctx.dependencyVersions, '@feathersjs/cli':
    version } }` through `.then`s) is applied twice: tsgo's result keeps two distinct `{ '@feathersjs/cli': any }`
    intersection members (instantiations of the literal under two mappers), tttt makes one (it interns them), and
    tsgo's spread type no longer reuses the properties' nodes. Not reduced (2026-10-18). [dup]
  - react-scan `Component<P>` vs `Component<Attributes, {}>`: fixed (a local function with expandos is an anonymous
    type now, instantiated: is_local_expando_function()). The `Readonly<{...}>` reprinted where tsc
    no longer reuses an alias body's annotation: hacks.md "An alias body's homomorphic mapped instance".
    react-three-fiber `ReadonlyArray<...>` reuse: see the 2026-10-15 note above. (mockoon's `Observable<any>` fixed
    2026-10-21: ttttContravariantInferenceOfUnresolvedName.) vueuse
    `.test/mount.d.ts`: `defineComponent({ components: Comp, ... })` where tsgo prints Comp's `DefineComponent<...>`
    expanded (its constructor type & ComponentOptionsBase...) and tttt keeps the alias: alias lost somewhere in Vue's
    inference, not reduced. headlessui's use-controllable matched in 2026-10-18 runs (an order effect, may come back). effect's and excalidraw's others: see "Checker types, uninvestigated"
    (effect ExecutionPlan and fiberRuntime fixed 2026-10-16). [fixed oracle 2026-10-23]
  - `export default undefined`: tsgo writes it as an identifier export, tttt as `declare const _default: undefined`. [text]
- **Fixed 2026-10-20 **: react-three-fiber's react-native mock: the rest of a mapped type over a mapped type
  (`Readonly<Omit<ViewProps, 'children'>>`) reuses the annotations of the properties the inner one maps
  (lower.c's mapped_prop_annotation(), ttttRestOfNestedMappedReusesAnnotation). The "map: its .d.ts differs" bucket is
  the maps of files whose .d.ts are in other buckets (babel-parser, mcp-sdk, slate). telegraf's map and vscode's
  modifier unchanged (entries below and above). [fixed 2026-10-20]
- **Painting gaps** (dtsdiff "import"; 2026-10-15 fixed inferred predicates, late bound members, readonly enum
  fields, enum-valued keys, every statement of a painted merged name, private constructors' parameter properties,
  painted destructurings' types; 2026-10-16 fixed unresolved-import names reused across files, export lists
  past 64 names, a namespace's export list, value-only `export =` property imports used as types, the first walk
  printing without type parameter names (discord.js's ActivityType), augmentation-kept imports (vue)). What's left in
  the bucket is type text whose names differ (checker-side, the import line follows; 2026-10-18 fixed yup's
  imported distributive alias, gemini-cli's `f?: T` generic argument instantiation, effect's late-painted merged const):
  excalidraw `ExcalidrawFreeDrawElement`, slate-hyperscript. [fixed oracle 2026-10-23]
- **Reused nodes' text, remaining** (2026-10-15 fixed escapes, comments around type nodes, extends-clause
  parentheses, `typeof f` in unions, empty multi-line tuples, synthesized literals): still open, each small:
  a parenthesized type's inner node's comments (`( /** c */ A)`: tttt keeps one `parenthesized` flag, no node);
  comments on their own line before a list delimiter (emitListItems' emitLeadingComments(previousSibling.End()));
  comments of nodes the node builder copies (tsgo's clones keep positions: `setTextRange`; a union's or intersection's members' done 2026-10-18 when in the file being written, display_node_list()); trpc's `'global-errorFormatter'`
  of an `as const` pseudo type mapped where tsgo doesn't map it (dtsdiff `map: extra at string (message)`,
  `'RATE_LIMIT`); feathers' `language: 'js' | 'ts'` and comments of a spread's properties where tsgo prints the type
  (`"js" | "ts"`, no comments, an intersection member repeated: a spread of an intersection with prompt answers,
  checker-side, see "type text" leftovers). [text]
- **Unknown-symbol naming (tsgo quirk, matched)**: tsgo names an unresolved type through any import alias that also
  resolves to the unknown symbol (`node is isIdentifierChar`, babel-parser with unbuilt workspace deps; a reused
  annotation from another file, `Plugin<Plugin>`, ttttDeclarationUnknownImportRenamed; a reused `typeof X` too,
  ttttDeclarationUnknownImportTypeof) and paints that import; a
  type printed from its type (not a reused node) may still not paint it (unverified). [fixed 2026-10-16]
- **Declaration maps** (stage 5 done; 2026-10-16 modelled tsgo's per-enclosing-declaration serializedTypes
  cache, TyPrintedTypes: a type printed again in the same scope is an unmapped clone, a signature with parameters a
  new scope; mapped types left out since tttt interns instantiations tsgo doesn't). `scripts/dtsdiff.py -k 'map:'`
  lists the rest, single files: babel-types `_skip`, lit (`NodeListOf`, `[Symbol`, `'_` constructor), redux-toolkit
  helpers `K (})`, telegraf `(tg`. 2026-10-18: the cache is per *enclosing declaration*, which a class's
  property declarations share (the class) and a function's parameters share (Walk.class_printed_types,
  printed_next); a keyword annotation's file is its signature's (TyDisplay.node_file); a class literal's methods
  tsgo instantiates for `this` have fresh type parameters (method_this_instantiated(): not thisless, or more than
  one declaration, and some declaration possibly references `this`, e.g. an implementation without a return type).
  Not modelled: the printer flags in tsgo's cache key; a type reprinted in a new signature scope misses mappings
  tsgo has (probe: an object literal with `use: () => p, withArg: (x: number) => p` of an inline conditional). [text]

## 2026-10-16: JS maps / missing files / small buckets, leftovers
- **telegraf `as (tg.MessageEntity & { type: SelectedTypes; ... })[]`**: tsgo reuses the node with only the local alias
  reference replaced (partial reuse); tttt prints from the type (same text, no mappings). [text]
- Suite JS dts quirks left as tsgo bugs: jsdocTemplateTagDefault (`= ` empty default), jsdocReadonlyDeclarations
  (`readonly constructor`), jsdocAccessibilityTagsDeclarations (`c;`). [tsgo]

## 2026-10-18: babel-parser and friends
- **babel-parser `maybeAsyncOrAccessorProp(...): boolean`** (tsgo) vs tttt's `prop is typeof prop & {...}`: a tsgo
  checker-distribution quirk, not emulated. `tsc --singleThreaded` prints the predicate like tttt (so do
  `--checkers 1/2` on the probe below; 3+ print `boolean`). In a checker that resolves `new A().m` before checking
  A's file, the signature's parameters are instantiated copies, `typeof prop` in the reused predicate resolves to the
  original parameter but the fake scope has the copy (trackExistingEntityName's getSymbolIfSameReference fails), the
  reuse fails and reuseTypeNode's fallback is getTypeFromTypeNode(predicate) = boolean. Probe: types.ts with two
  interfaces, a.ts `class A { m(prop: OP | OM): prop is typeof prop & { key: string } }` (a union parameter
  matters), c.ts `export const v = new A().m;` with `--checkers 3`. tsgo single-threaded also differs from its
  default on babel-parser's `parseVarStatement`/`parseExportSpecifier` (`any` by default, as tttt). [tsgo]

## 2026-10-18 leftovers (second list)
- **sequelize/packages/core `readonly models: any`** (tsgo) vs tttt's `ModelSetView<Dialect>`: tsgo itself prints
  `ModelSetView<Dialect>` with `--checkers 1` and `any` with its default 4: a circularity that depends on which checker
  checks what first (the TS7022 that differs by checker count in tsgo too, CLAUDE.md's kdiff note). Not worth
  emulating without tsgo's file-to-checker assignment (the "Member order" item's plan). [tsgo]
- A type literal the node builder reuses twice in one declaration (a parameter, then the return type `x`): a member's
  trailing comment is written the first time only in tttt, both times in tsgo (probe: `memo((x: { a: number, /** b */
  b: 1 }) => x)`). [text]


## Refresh 2 leftovers (2026-10-19)
Fixed: vest's `typeof ns.x` (a spread namespace's alias export is the alias: spread_alias), the checkJs projects'
maps and types (rocket 315 / 316, eslint-plugin-jsdoc 336 / 338, markdown-magic and its packages all match), lit's
class literal fields (pseudo types), babel and react-three-fiber specifiers (match now, not by a fix of this pass).
Left:
- **vest lazy.d.ts, a tsgo bug**: `[K in keyof TRules]: (...args: DropFirst<Parameters<TRules[K]>>) => ...` instantiated
  per key; tsgo names the instantiated alias by a wrong symbol (`arrayRules.isEmpty<Parameters<typeof
  arrayRules.equals>>`, `isEmpty` being another export of the module), tttt writes the annotation's text with K and
  TRules out of scope. tttt's is wrong too: it should print the instantiation (`DropFirst<Parameters<typeof
  arrayRules.equals>>`), which still won't match tsgo. Not worth emulating tsgo's symbol mix-up. [fixed oracle 2026-10-23]
- **eslint-plugin-jsdoc**: 5 files match `tsgo --singleThreaded` (checker split); its maps, rocket's `{string=}` and
  markdown-magic's all match since the second pass (2026-10-19). [tsgo]
- **`/** @type {{a: number}} */ export default { a: 1 }`**: tsgo maps the member to the literal's property when the tag's
  type and the literal's agree (with an extra optional member it maps the tag, like tttt). [text]
- **telegraf** partial reuse (below, unchanged: tsgo reuses the asserted node with only a local alias reference
  replaced, a reuse mode tttt's printer doesn't have; not cheap), **vueuse `.test/mount.d.ts`** mismatches again (also at this
  pass's base commit) after matching in the morning's full run: an order or environment effect. [dup]

## 2026-10-20 (real-code pass)
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
  (The 7.1.0-beta copies the widening context's property instead: recheck after dtsdiff --tsc) [fixed oracle 2026-10-23]
- headlessui-vue: `--singleThreaded` writes what tsgo's default does (cached); its last file matches since the twin fix. [fixed 2026-10-20]
- Left as before: babel-parser, mcp-sdk (checker split), slate test / postiz / ui (undefinedProperties), feathers
  `language`, vest (tsgo bug), telegraf's partial reuse map (react-three-fiber's mock fixed since). [note]
