# Declaration emit: what still differs from tsgo (plan 64, closed)

**Plan 64 closed 2026-10-16** (todo entry 5 of the eighteenth window). Its decisions that still hold: no synthetic AST
(the writer prints text from the source AST and `Ty`s, documentation.md "Declaration emit"); one walk with two sinks
(`declaration_emit.c`'s diagnostics walk, the writer a second pass in the checker that checked the file); tttt emits
only where the options emit declarations alone (`emitDeclarationOnly`), never JavaScript; no `.tsbuildinfo` (plan 76
decides incremental). Validation met: suite dts in the headline (1834 / 1859, JavaScript tests included since this
entry: 351 / 352), dtsdiff >= 90% of projects (96.8%), clidiff `--emit`; `dtsdiff.py --kdiff 16` differs on 3 / 292
of a quarter sample by tsgo's own per-checker undefinedProperties history (below, "Member order").

State 2026-10-18 (todo entry 6, four agents): `scripts/dtsdiff.py` **1211 / 1233 projects, 96675 / 96726 files**
byte-identical (was 1193 / 96627); clidiff `--emit` 403 / 410. What the pass taught: tsc keys object instantiations
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
  2026-10-18), so it's the per-checker KEY_EXTENDED_CONTAINERS memo below with tttt's file distribution; babel's `eslint.config.d.ts` (`@eslint/config-helpers` vs `eslint/config`) and react-three-fiber
  (`three` vs `three/src/Three.Core.js`), uninvestigated. corpus:react's `@testing-library/react` vs `/dom` is a worktree
  artifact (the right one from the real path). tsgo's checker-wide memo of a successful import lookup
  (KEY_EXTENDED_CONTAINERS, filled from inside an import declaration) makes names depend on what a checker printed
  first, as in tsgo; tttt's checkers don't split files like tsgo's.
- **Member order** (agent C 2026-10-16 fixed arktype's filtering `as` clauses, JS classes' `this.x` members, the
  isDeclarationContainedBy offset quirk of mixins): tsgo sorts every resolved object type's properties by
  compareSymbols (getNamedMembers), tttt sorts at Members build (builder_sort_stable). What's left:
  - `?: undefined` members of normalized object literal unions (postiz-app `*.provider.d.ts`, slate
    `test/interfaces/Text/decorations/*`, excalidraw `actionElementLink.d.ts`; likely react-navigation's
    `getShadowStyle` `?: never` too): tsgo's getUndefinedProperty caches one symbol per name per checker, declared
    where the first property it was made for is, so the order depends on which file that checker checked first (tsgo
    runs 4 checkers). tttt emulates the cache (KEY_UNDEFINED_PROPERTY) but its checking order/distribution differs.
    Plan-sized: needs tsgo's file-to-checker assignment and check order.
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
- **react-three-fiber `__mocks__/react-native.d.ts`** (uninvestigated): `render()` returns `React.createElement('view',
  props)` with `props` the rest of `this.props` (`Readonly<Omit<ViewProps, 'children'> & {...}>`); tsgo reuses each
  property's annotation (`accessibilityActions?: ReadonlyArray<AccessibilityActionInfo> | undefined`, `'none' |
  'polite' | 'assertive'` in single quotes, `style?: StyleProp<ViewStyle> | undefined`), tttt prints the types (the
  alias `AccessibilityActionInfo` lost, literal unions sorted, `style` without `| undefined`): the rest of a
  homomorphic mapped type over interface properties should keep their declarations for reuse.
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
    member order, the undefinedProperties cache of "Member order" above.
  - feathers generators `language: "js" | "ts"` (tsgo prints, no comments, an intersection member repeated) after a
    long `.then()` chain of prompts: checker-side identity. `initializeBaseContext()` (commons.ts, a generic arrow
    returning `{ ...ctx, dependencyVersions: { ...pkg.devDependencies, ...ctx.dependencyVersions, '@feathersjs/cli':
    version } }` through `.then`s) is applied twice: tsgo's result keeps two distinct `{ '@feathersjs/cli': any }`
    intersection members (instantiations of the literal under two mappers), tttt makes one (it interns them), and
    tsgo's spread type no longer reuses the properties' nodes. Not reduced (agent B 2026-10-18).
  - react-scan `Component<P>` vs `Component<Attributes, {}>`: a checker bug, bugs/expando_instantiation.md (a
    function with expandos is a TY_DECLARED namespace, never instantiated). The `Readonly<{...}>` reprinted where tsc
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
- **lit ssr-dom-shim `labels = [] as unknown as NodeListOf<...>`** in a class expression printed as a literal: tsgo
  maps the asserted type (pseudo type of the field initializer); tttt's class-literal members don't use pseudo_of_field.
  It needs a path like the object literal's (ty_display's declared_item()/pseudo_of_object_member branch), but a class
  field's PropDecl only has its `lazy` type, no way back to its ClassItem. Same text, only mappings differ.
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
- **babel-parser placeholders/plugin-utils `comments: N.Comment[]` member order**: in the mixins' flattened
  instance types tsgo splits BaseParser's `declare` fields (`options` ... `comments` in one place, `locData` and the
  methods after it elsewhere); tttt places `comments` with `locData`. Same with `--singleThreaded`. Unexplained (the
  "Member order" item above: compareSymbols of each resolved level).

## 2026-10-18 worktree agent D leftovers
- **sequelize/packages/core `readonly models: any`** (tsgo) vs tttt's `ModelSetView<Dialect>`: tsgo itself prints
  `ModelSetView<Dialect>` with `--checkers 1` and `any` with its default 4: a circularity that depends on which checker
  checks what first (the TS7022 that differs by checker count in tsgo too, CLAUDE.md's kdiff note). Not worth
  emulating without tsgo's file-to-checker assignment (the "Member order" item's plan).
- A type literal the node builder reuses twice in one declaration (a parameter, then the return type `x`): a member's
  trailing comment is written the first time only in tttt, both times in tsgo (probe: `memo((x: { a: number, /** b */
  b: 1 }) => x)`).


## Refresh 2 (2026-10-18, todo entry 7): vest
- **`typeof ns.x` for a spread namespace's const export**: `export const allRules = { ...numberRules } as const` where
  numberRules re-exports `export const isEven = (v: string | number): boolean => ...`: tsgo writes `readonly isEven:
  typeof numberRules.isEven` (its pseudochecker path in serializeTypeForDeclaration, the property's declaration being
  the variable), tttt the function type. Functions are `typeof` on both sides. Probe: three files, `rules/isEven.ts`,
  `rules/numberRules.ts` re-exporting it, `all.ts` spreading `import * as numberRules`.
- **An alias reused uninstantiated in a mapped member**: vest's lazy.d.ts, `[K in keyof TRules]: (...args:
  DropFirst<Parameters<TRules[K]>>) => ...` instantiated per key: tttt writes the annotation's text
  (`DropFirst<Parameters<TRules[K]>>`, K and TRules out of scope), tsgo the instantiation (named, oddly,
  `arrayRules.isEmpty<Parameters<typeof arrayRules.equals>>`: a different symbol of the same module). Not reduced.
