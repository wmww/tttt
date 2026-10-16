# Declaration emit: what still differs from tsgo (plan 64)

State 2026-10-15 (after todo entry 6's fan-out): suite dts component in the headline; `scripts/dtsdiff.py` **1077 / 1233
projects, 95611 / 96726 files** byte-identical (77 of the remaining projects / ~800 files are JS sources, stage 6).
`scripts/dtsdiff.py --last -k BUCKET` lists files, `--show PROJECT FILE` diffs one. Classes by
size on real code, each with an example:

- **JavaScript sources** ("missing file", ~690 files): JS declaration emit is plan 64 stage 6 (not started). crawlee,
  mermaid, astro/benchmark, query/*: `.js`/`.mjs` sources with allowJs.
- **Import specifier choice** (agent A 2026-10-15 ported tsgo 7.1's rules: augmented module names, the import lookup
  in the enclosing location's mode, no container cap, no package copies as containers, existing imports only through
  their realpath, program-wide dependency links by package directory, exports conditions by the target's format;
  `TTTT_DEBUG_CONTAINERS=1` shows the candidates). Left (~4 projects): mcp-sdk/examples
  `import("node_modules/hono/dist/types/hono")` vs tttt's `.../dist/types` (an index ending through a `paths`-like
  bare specifier); babel's `eslint.config.d.ts` (`@eslint/config-helpers` vs `eslint/config`) and react-three-fiber
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
- **Checker types, uninvestigated** (agent C's leftovers, 2026-10-15): corpus:effect `test/Effect/query-repro.test.d.ts`
  (`Equals<Omit<A, ...>, {}> extends true ? void : {...}` resolved to its false branch by tttt, deferred by tsgo);
  excalidraw `App.d.ts` (`callback?: () => void` reused where tsgo prints `(() => void) | undefined`: React's
  `Component<any, AppState>["setState"]` through useContext; a probe with the same class shape in a `.d.ts` matches,
  so the Block-rule guess below may not be it either), `tests/helpers/api.d.ts` (tsgo keeps `declare
  const { h }: Window & typeof globalThis;`); feathers `next?: HookContext` (tsgo names the unresolved NextFunction
  through another unresolved import alias, the unknown-symbol quirk below) and better-auth's similar wrong-looking
  names (`GenericEndpointContext[]`, `AuthContext`).
- **Type literals inside a generic function's block body are instantiated (tsc's isTypeParameterPossiblyReferenced
  Block rule)**: walking up from a type node to a type parameter's declaration, tsc assumes the parameter possibly
  referenced as soon as it passes a Block, so `create = <T>(fn: () => T) => { return (h: { g?: () => any }) => 1 }`
  instantiated (`create(() => 1)`) makes new type literal and function types, whose annotations no longer say the
  same: tsgo prints `g?: (() => any) | undefined` where tttt reuses `g?: () => any` (an expression body keeps the
  identity in both). better-auth test-utils `suites/*.d.ts`, likely excalidraw `App.d.ts` `callback?:` too. tttt
  lowers type nodes with only the type parameters they mention (anonymous_set_free_params, lower.c): needs a "possibly
  referenced through a block" bit there; touches identity of many types, so run every oracle and perfsweep.
- **keyof the wildcard**: tsc's getIndexType returns the wildcard for the wildcard, which defers `Equals<Omit<A, K>,
  {}>` (effect query-repro: tttt resolves it to false). The one-line fix in ty_keyof() makes ramdaToolsNoInfinite2
  report a spurious TS5114: the permissive instantiation of ts-toolbelt's `__ListOf` then recurses 40 iterations, and
  tttt's instantiation depth per level is higher than tsgo's. Needs tsgo's depth accounting first.
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

- **B's second pass leftovers** (2026-10-15): pi durable's `A & (B | C)` printed distributed (`(A & B) | (A & C)`:
  a union origin, checker-side); trpc's `TypeError<'...'>` property of CreateTRPCClientOptions (tsgo reuses the
  annotation, tttt reprints with `| undefined` and double quotes; a two-file probe matches, the real case doesn't);
  arktype bounds `OneCharComparator` and vscode instantiationService `const enum TraceType` locals tsgo keeps
  (painting, not reproduced small).
- **"type text" leftovers** (agent B 2026-10-16 fixed trpc's alias/spread reuse and AsyncIterable args, feathers'
  mapped literals and contravariant aliased intersections, fumadocs' implied infer constraints, ts-mixer's abstract
  constructor parens, tanstack-table's contextual intersection, slate, vitest, nextra, pi keybindings). Left:
  - qwik `FunctionComponent<{...}>` (`{ renderFn(...) }['renderFn']` alias): tsgo keeps the alias on
    `registerSingleton<FunctionComponent<X>>(...)`'s result; a one-file probe doesn't (tsgo prints the signature there
    too), so it's an order effect: an earlier `FunctionComponent<X>` with the same arguments made the instantiation,
    alias included, that later ones find in tsgo's cache. Needs tsgo's instantiation cache keyed without the alias.
  - ui `registry/config.d.ts` `z.ZodEnum<[Base, ...unknown[]]>` (tsgo) vs `[Base, ...Base[]]`: zod 3's
    `Writeable<[BaseName, ...BaseName[]]>`, a homomorphic mapped type over a tuple with a rest element; tsgo's
    `unknown[]` looks like its own quirk, uninvestigated.
  - feathers generators `language: "js" | "ts"` (tsgo prints, no comments, an intersection member repeated) after a
    long `.then()` chain of prompts: checker-side identity, uninvestigated.
  - react-navigation `useNavigationBuilder`/`useNavigationHelpers` (`Readonly<{...}>` expanded vs `NavigationAction`
    in the function member of `NavigationAction | ((state) => NavigationAction)`; a two-file probe matches),
    react-scan `Component<P>` vs `Component<Attributes, {}>` (an inferred `this` type), react-redux (tsgo reuses
    `import("redux").Dispatch` as written without type arguments, tttt prints `Dispatch<UnknownAction>`; a three-file
    probe matches), mockoon `Observable<any>`, react-three-fiber `ReadonlyArray<...>` reuse, headlessui/vueuse long signatures: one file
    each, not looked at. excalidraw `LibraryUnit.d.ts` drops the comment in `LibraryItem["id"] | /** for pending item
    */ null` of a reused node the node builder copies (the "comments of nodes the node builder copies" item below; the
    transformer's own nodes keep theirs). effect's and excalidraw's others: see "Checker types, uninvestigated"
    (effect ExecutionPlan and fiberRuntime fixed 2026-10-16).
  - `export default undefined`: tsgo writes it as an identifier export, tttt as `declare const _default: undefined`.
- **Painting gaps** (dtsdiff "import"; agent A 2026-10-15 fixed inferred predicates, late bound members, readonly enum
  fields, enum-valued keys, every statement of a painted merged name, private constructors' parameter properties,
  painted destructurings' types; agent C 2026-10-16 fixed unresolved-import names reused across files, export lists
  past 64 names, a namespace's export list, value-only `export =` property imports used as types, the first walk
  printing without type parameter names (discord.js's ActivityType), augmentation-kept imports (vue)). What's left in
  the bucket is type text whose names differ (checker-side, the import line follows): yup `MergeObjectTypes` (an alias
  of a distributive conditional expanded), gemini-cli `MessageBusType` (a generic signature `<T extends Message>(type:
  T['type'], ...)` tsgo instantiates), qwik `FunctionComponent`/`_EFFECT_BACK_REF`, effect `test/Effect/query*.test`
  (`Context`), excalidraw `ExcalidrawFreeDrawElement`, react-navigation `useNavigationHelpers` (tsgo names PartialState
  `CommonActions<State>` through the `export * as CommonActions` namespace, and `State_1` renaming), slate-hyperscript.
- **Reused nodes' text, remaining** (agent B 2026-10-15 fixed escapes, comments around type nodes, extends-clause
  parentheses, `typeof f` in unions, empty multi-line tuples, synthesized literals): still open, each small:
  a parenthesized type's inner node's comments (`( /** c */ A)`: tttt keeps one `parenthesized` flag, no node);
  comments on their own line before a list delimiter (emitListItems' emitLeadingComments(previousSibling.End()));
  comments of nodes the node builder copies (tsgo's clones keep positions: `setTextRange`); trpc's `'global-errorFormatter'`
  of an `as const` pseudo type mapped where tsgo doesn't map it (dtsdiff `map: extra at string (message)`,
  `'RATE_LIMIT`); feathers' `language: 'js' | 'ts'` and comments of a spread's properties where tsgo prints the type
  (`"js" | "ts"`, no comments, an intersection member repeated: a spread of an intersection with prompt answers,
  checker-side, see "type text" leftovers).
- **Unknown-symbol naming (tsgo quirk, matched)**: tsgo names an unresolved type through any import alias that also
  resolves to the unknown symbol (`node is isIdentifierChar`, babel-parser with unbuilt workspace deps; a reused
  annotation from another file, `Plugin<Plugin>`, ttttDeclarationUnknownImportRenamed) and paints that import; a
  type printed from its type (not a reused node) may still not paint it (unverified).
- **Declaration maps** (stage 5 done; agent C 2026-10-16 modelled tsgo's per-enclosing-declaration serializedTypes
  cache, TyPrintedTypes: a type printed again in the same scope is an unmapped clone, a signature with parameters a
  new scope; mapped types left out since tttt interns instantiations tsgo doesn't). `scripts/dtsdiff.py -k 'map:'`
  lists the rest, single files: babel-standalone `presets`, babel-traverse `traverse`, babel-types `_skip`, lit
  (`NodeListOf`, `[Symbol`, `'_` constructor), redux-toolkit helpers `K (})`, telegraf `(tg`, xterm `number (start)`.
  Not modelled: the printer flags in tsgo's cache key; a type reprinted in a new signature scope misses mappings
  tsgo has (probe: an object literal with `use: () => p, withArg: (x: number) => p` of an inline conditional).
