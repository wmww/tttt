# Declaration emit: what still differs from tsgo (plan 64)

State 2026-10-15 (after todo entry 7): suite dts 1345 / 1408; `scripts/dtsdiff.py` 922 / 1233 projects, ~74300 / 76294
files byte-identical. `scripts/dtsdiff.py --last -k BUCKET` lists files, `--show PROJECT FILE` diffs one. Classes by
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
- **Member order** (31 files left of ~170; the zod-style mapped/intersection and spread cases are fixed): tsgo sorts
  every resolved object type's properties by compareSymbols (getNamedMembers), tttt sorts at Members build
  (builder_sort_stable). What's left:
  - `?: undefined` members of normalized object literal unions (postiz-app `*.provider.d.ts`, slate
    `test/interfaces/Text/decorations/*`, excalidraw `actionElementLink.d.ts`): tsgo's getUndefinedProperty caches one
    symbol per name per checker, declared where the first property it was made for is, so the order depends on which
    file that checker checked first (tsgo runs 4 checkers). tttt emulates the cache (KEY_UNDEFINED_PROPERTY) but its
    checking order/distribution differs. Plan-sized: needs tsgo's file-to-checker assignment and check order.
  - mermaid radar `styles.d.ts` (a JS class instance type: constructor `this.x` assignments vs methods), trpc
    next-prisma `context.d.ts`, ui `registry-health/directory.d.ts`: uninvestigated.
- **Checker types, uninvestigated** (agent C's leftovers, 2026-10-15): corpus:effect `test/Effect/query-repro.test.d.ts`
  (`Equals<Omit<A, ...>, {}> extends true ? void : {...}` resolved to its false branch by tttt, deferred by tsgo);
  excalidraw `MainMenu.d.ts` (`Trigger: typeof import(...).default` where tsgo expands it), `App.d.ts` (`callback?:
  () => void` reused where tsgo prints `(() => void) | undefined`), `tests/helpers/api.d.ts` (tsgo keeps `declare
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
- **Worktree artifacts**: dtsdiff run in a worktree with symlinked corpora differs in `.d.ts.map` `sources` and in
  TS2883 "cannot be named without a reference to '/home/ai/tttt/...'" (excalidraw-app/collab): not bugs.

- **Painting gaps** ("import", 40 projects / 64 files after agent A's four fixes: inferred predicates, late bound
  members, readonly enum fields, enum-valued keys): corpus:vscode's 13 left (`SymbolKind`, `URI`, `RefCountedDisposable`,
  `IReference` dropped: each a name printed from a type without `d.tracker->track_symbol()`), qwik `EffectProperty`,
  gemini-cli `SettingScope`, xstate `AnyEventObject`, react-navigation `PartialState`/`EventMapBase`, yup
  `MergeObjectTypes`, typedoc `IntrinsicElements`, effect `fiberRuntime.d.ts` (`import type * as Either`), excalidraw
  `actionProperties.d.ts`. `scripts/dtsdiff.py --last -k import` lists them; reduce one to a probe and find the display
  path that prints without tracking (the four fixed ones were all of that kind).
- **B's second pass leftovers** (2026-10-15): pi durable's `A & (B | C)` printed distributed (`(A & B) | (A & C)`:
  a union origin, checker-side); trpc's `TypeError<'...'>` property of CreateTRPCClientOptions (tsgo reuses the
  annotation, tttt reprints with `| undefined` and double quotes; a two-file probe matches, the real case doesn't);
  graphql-js `class GraphQLEnumType/* <T> */  implements` (tsgo copies a plain comment after a class name, two
  spaces); arktype bounds `OneCharComparator` and vscode instantiationService `const enum TraceType` locals tsgo keeps
  (painting, not reproduced small); dtsdiff `map: extra at infer (ctx)` (trpc ×9: an `infer T extends object` tsgo
  prints anew, renamed `T_1`, tttt reuses and maps), `map: missing at AbstractTable ({)` (sequelize), the other
  single-file map buckets.
- **Painting gaps** (dtsdiff "import"; agent A 2026-10-15 fixed inferred predicates, late bound members, readonly enum
  fields, enum-valued keys, every statement of a painted merged name, private constructors' parameter properties,
  painted destructurings' types; vscode and hexo/graphql-codegen now match). Left, one or two files each:
  react-navigation `useNavigationHelpers` (tsgo names PartialState `CommonActions<State>`: a chain through the
  `export * as CommonActions` namespace, quirk to understand first), gemini-cli `MessageBusType`, qwik
  `FunctionComponent`/`_EFFECT_BACK_REF`, yup `MergeObjectTypes`, typedoc `IntrinsicElements`, effect
  `test/Effect/query*.test` (`Context`), excalidraw `ExcalidrawFreeDrawElement`, apollo-client `core/index`, fp-ts
  `index`, better-auth test-utils, reselect, slate-hyperscript, shadcn templates, vitest `logger`/`console`; and the
  other way, discord.js structures keeps `ActivityType` where tsgo drops it. `scripts/dtsdiff.py --last -k import`;
  each is a name printed (or written) without `track_symbol()`, or a statement the late pass doesn't walk.
- **Reused nodes' text, remaining** (agent B 2026-10-15 fixed escapes, comments around type nodes, extends-clause
  parentheses, `typeof f` in unions, empty multi-line tuples, synthesized literals): still open, each small:
  a parenthesized type's inner node's comments (`( /** c */ A)`: tttt keeps one `parenthesized` flag, no node);
  comments on their own line before a list delimiter (emitListItems' emitLeadingComments(previousSibling.End()));
  comments of nodes the node builder copies (tsgo's clones keep positions: `setTextRange`); trpc's `'global-errorFormatter'`
  of an `as const` pseudo type mapped where tsgo doesn't map it (dtsdiff `map: extra at string (message)`,
  `'RATE_LIMIT`); feathers' `language: 'js' | 'ts'` and comments of a spread's properties where tsgo prints the type
  (`"js" | "ts"`, no comments, an intersection member repeated: a spread of an intersection with prompt answers,
  checker-side).
- **Unknown-symbol naming (tsgo quirk, matched)**: tsgo names an unresolved type through any import alias that also
  resolves to the unknown symbol (`node is isIdentifierChar`, babel-parser with unbuilt workspace deps) and paints
  that import; tttt prints the same name but doesn't paint the import.
- **Declaration maps** (stage 5 done): 551 of 20435 dtsdiff maps differ, 271 of them because their `.d.ts` does;
  `scripts/dtsdiff.py -k 'map:'` lists the rest by first differing mapping. Mostly tttt reusing a node tsgo prints
  anew (it then maps what tsgo doesn't: trpc's `infer T extends object` in inferred type arguments, mixin constructor
  types, `PromiseLike<void>` in testClientResource), `-0` literal types, singletons.
