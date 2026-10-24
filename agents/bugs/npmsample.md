# Random npm projects (scripts/npmsample.py) divergences
Found by the npm sample (agents/notes/npm_sampling.md); per seed the table is in agents/measurements.md "Random npm
projects". Repros are test/cases/npm_*.ts (failing until fixed; registry test/npmsample/repros.tsv); text-only
differences (same file, line, code) aren't divergences for the npm workflow and get no repro (agents/notes/npm_sampling.md).

- Seed 20261023, JohnDeved/zooqle-api-ts: `let [, a]: string[] | number[] = s.match(r)` (a union with null) read the
  element through ty_access_reading instead of leaving the declared type (missing TS2345/TS2339/TS2322). Repro
  test/cases/ttttDestructuredNullableUnionInitial.ts. [fixed 68bd2051d]
- Seed 20261023, vijay8856/react-native-find-background-location-vd: an unknown compiler option set to null was
  TS5023 (tsgo's onPropertySet skips null values). Repro test/cases/ttttConfigUnknownOptionNull.ts. [fixed 8ec4e47a4]
- Seed 20261023, AkilSaji/firelab: spurious TS2538 "Type '{}' cannot be used as an index type" indexing with a
  truthiness-narrowed generic indexed access: `const cap = CAP_FOR[method]` (CAP_FOR: `Partial<Record<"x" | "y",
  keyof Caps>>`, method: M extends "x" | "y") then `if (cap) caps[cap]`. tsgo types the narrowed cap
  `NonNullable<Partial<Record<"x" | "y", "a">>[M]>` and `caps[cap]` `Caps[NonNullable<...>]`, no error; tttt's index
  check sees '{}' (the `& {}` of NonNullable without the indexed access's constraint, probably in the TS2538
  isTypeAssignableTo(index, keyof object) path). Repro test/cases/npm_firelab_TS2538.ts (7 lines). [flip] [fixed 1f9c5ae2d]
- Seed 20261023, overture-stack/arranger (modules/sqon): `SqonBuilder.or([study, site])` with `as const` objects
  against `SqonNode | SqonNode[]` (SqonNode a union with zod v4's `z.infer` of a looseObject): tsgo reports TS2345 on
  the whole argument, tttt elaborates into the array literal and reports TS2322 on the element `site` (the same line;
  2 missing, 3 extra in the project). A union target containing the array type: tsgo's elaborateError doesn't descend
  into the array literal here. Repro test/cases/npm__overture-stack_sqon_TS2345.ts (13 files, zod cut to ~40 lines).
  Root cause: findMostOverlappyType's key overlap. tttt counted a tuple's "0" for the array member through its numeric
  index signature; keyof's intersection doesn't (numeric index key: number), so the object member with a string index
  signature is the best match, its [0] is unknown and nothing is elaborated (ttttBestMatchNumericIndexKeys). Fixed:
  index_key_includes_name in relate.c. The repro's errors component passes; its types component still differs on
  an import() specifier inside zod (`../core/core.cjs` where tsgo prints `../core/core.d.cts`), a separate [text]
  bug. [fixed 1096fcc71]
- Seed 20261023, alex-korneyko/ngx-summernote-mod: the TS2769 elaboration of `combineLatest(requests).subscribe(
  (remotePaths: string[]) => ...)` (rxjs 6.5.3, `const requests = []` an evolving array pushed
  `Observable<string | false>` in a for-of) prints `(value: unknown[]) => void` where tsgo prints
  `(value: (string | false)[]) => void`: tttt infers unknown for the last combineLatest overload's T (the evolving
  array's type or the overload's inference). Same (file, line, code). Line 350 of
  projects/ngx-summernote/src/lib/ngx-summernote.directive.ts at commit of the setup record; a cut-down
  evolving-array probe didn't reproduce; the clone is deleted. [text]
- Seed 20261023, axiona/uri: TS2416 for a class `implements CallbackType<ValueType, Type, ...>` (an alias of an
  intersection, node_modules/@axiona/validator/validatable/callback.d.ts(42)): tsgo names the base type expanded
  ('Readonly<Message<MessageType> & Value<Type | ValueType> & ...> & Validation<...> & Argument<...>', truncated),
  tttt by the alias ('CallbackType<ValueType, Type, MessageType, Arguments, Boolean>'). The clone is deleted. [text]
- **npm__uzm_katex-woff2_TS2577** (seed 20261024, @uzm/katex-woff2@0.1.0, KaTeX/KaTeX src/strict.ts:10): `type
  StrictFunction = (...) => Exclude<Strict, StrictFunction>` with StrictFunction a member of Strict. The distributed
  Exclude compares StrictFunction with itself, and tttt's callable_definitely_related shortcut asked for its return type
  (the Exclude being resolved): TS2577. tsc's isTypeAssignableTo answers identity first. Fixed by taking the true
  branch for identical check and extends types (ty_conditional_resolve). [flip] [fixed 389887243]
- **npm_react-smooth-scroll-hook_TS7016** (seed 20261027, react-smooth-scroll-hook@1.3.4, ron0115/react-smooth-scroll-hook
  tsconfig.bench.json): `paths: { "*": ["./src/*", "./node_modules/*"] }` under bundler resolution. tsgo resolves
  `react` through the paths candidate ./node_modules/react to the untyped index.js (TypeScript and JavaScript extensions
  tried together at each location for bundler/node16/nodenext) and reports TS7016, then TS2503 for `React.` namespaces;
  tttt goes on to @types/react (as if TypeScript extensions were tried everywhere before JavaScript, node10's two
  passes). Missing 3 TS7016 + 1 TS2503 there; not a flip (both have errors). Guess: the module resolver's extension
  passes for paths mappings (src/ module resolution). The clone is deleted. Fixed in resolve_uncached
  (src/module_resolution.c): a `paths` mapping is tried with every extension at once before the two-pass package
  lookup, and a result found through paths has a package id only when found as a file (tsgo's paths loader is
  nodeLoadModuleByRelativeName), so TS7016 has no `npm i --save-dev @types/...` chain there. [fixed 2026-10-23]
- **npm__veto-party_baum__package_manager__npm_TS2339** (seed 20261024, @veto-party/baum__package_manager__npm@0.2.0-pre-54,
  veto-party/baum; tsgo 5, tttt 106). `type IConfigMapStructure = typeof VariableFeature.makeInstance extends () =>
  IFeature<any, any, infer Structure> ? (Structure extends Record<infer Key, infer Value> ? Map<Key, Value> : never) :
  never`, with VariableFeature a class over json-schema-to-ts's FromSchema of a `patternProperties` schema. tsgo
  infers Structure as the whole FromSchema result `{ [x: string]: { [x: string]: {} } }`; tttt as `{ [x: string]: {}
  }` (IFeature's From appears in `verifyObject(): element is ToDefinitionStructure<Path, From>` and in
  `ingestObject(): IngestResult<From>[]`: likely a candidate from the predicate through ToDefinitionStructure<any,
  ...> chosen or combined differently). FromSchema itself agrees on both sides; a self-contained mimic of the two
  methods infers `{ [x: string]: {} }` in both, so the difference needs the real chain (597-line repro, reduce.py plus
  hand fixes for the names the cut left). Every one of the 101 extra diagnostics looks downstream of this (property
  reads on `{} & { source: string }`, `unknown` entries): one root cause until a fix shows otherwise. Root cause: inferFromIndexTypes between two mapped types
  (the source `KeyValuePair<"variable", F>` resolved, the target `KeyValuePair<any, S>`) infers at HomomorphicMappedType
  priority, below the naked S beside it, so S gets the whole `{ variable: F }` from the predicate (and F from
  `ingestObject`, the common supertype F); tttt inferred F's index value at full priority. Repro
  ttttMappedIndexInferencePriority. The npm test's errors pass; its types still differ on two other lines (`this.schema`
  of a generic T printed as T where tsgo prints its constraint `Record<string, any> | {}`, and `keyof []` printed
  expanded by tsgo). (claimed: corB) [fixed 8c8643128]
- **npm__markw65_prettier-plugin-monkeyc_TS7034** (seed 20261031, @markw65/prettier-plugin-monkeyc@1.0.69,
  markw65/prettier-plugin-monkeyc; tsgo 13, tttt 15). src/printer.ts:74 `let rest, ...; ({ print: x, ..., ...rest } =
  estree)`: an auto-typed `let` (no initializer, no annotation) whose only assignment is an object rest element of a
  destructuring assignment. tsgo's control flow takes the assigned type from the rest element (getAssignedType of a
  rest property), tttt reports TS7034 at the declaration and TS7005 at the use. The array rest form `[first,
  ...others] = [1, 2, 3]` works. Guess: tttt's flow assignment for object-pattern targets skips the spread/rest
  element. tsc's getAssignedType has no case for a spread assignment: the rest gets the error type, so the auto-typed
  `let` is any from there on; tttt's pattern_path_type returned nothing for an object rest, which left the auto type
  (unassigned: TS7034). Fixed in pattern_path_type (src/ty/flow.c). [fixed 2026-10-23]
- Seed 20261029, vobsjs/vobs (@vobs/theme): `x?. (args)` (whitespace between `?.` and `(`) was TS1003 Identifier
  expected, and the syntax error hid the program's other diagnostics. Repro test/cases/npm__vobs_theme_TS1003.ts.
  [fixed daf488efd]
- Seed 20261029, vobsjs/vobs (@vobs/theme), behind the parser bug: `JSX.IntrinsicElements` an intersection of mapped
  types over HTMLElementTagNameMap and SVGElementTagNameMap had no tags (569 spurious TS2339 'div' does not exist).
  Repro test/cases/npm__vobs_theme_TS2339.tsx. [fixed ef9b44945]
- Seed 20261029, ionic-team/stencil-component-starter: spurious TS2320 on @vitest/expect's `interface Assertion<T>`
  (13 of them; tsgo is clean). @vitest/expect declares Assertion and `export type { Assertion }`, vitest augments it
  (`declare module "@vitest/expect"`) and re-exports it (`export { Assertion } from '@vitest/expect'`), and
  @stencil/vitest augments it through the re-export (`declare module 'vitest' { interface Assertion extends
  CustomMatchers }`). tsgo's mergeSymbol merges each augmentation into a clone (cloneSymbol of the resolved target, a
  clone of a clone the second time), and getMergedSymbol of the original declaration is only the first clone, so the
  interface @vitest/expect declares never has CustomMatchers as a base: no conflict with JestAssertion's
  jest.Matchers members. tttt's aug_merge_symbol/aug_merge_fixup (scope.c) put the second augmentation's base into
  what the declaration means. Symbols differ too (tsgo's import from 'outer' has 2 declarations). Repro
  test/cases/npm_My-first-stencil-project-001_TS2320.ts (inner/outer/aug, 30 lines). Also seed 20261028's
  0115-ionic-team-stencil-component-starter (tsgo 0, tttt 13 TS2320) and seed 20261032's
  0150-anchalmehta567-recipe-planner (tsgo 36, tttt 52: +16 TS2320, not a flip), the same augmentation. Errors fixed:
  the declaration's heritage is checked as the first clone (aug_one_level, hacks.md); the repro's SYM/TYP still differ
  (which clone a name means: tsc's getMergedSymbol applied once, twice or more depending on the path). (claimed: corB)
  [flip] [fixed cf4ba416a]
- Seed 20261029, nirholas/free-crypto-news: spurious TS2345 on unified's `.use(rehypeHighlight.default, { detect:
  true, ignoreMissing: true })` (`use<P extends unknown[]>(plugin: Plugin<P, Input, Output>, ...parameters: P |
  [boolean])`): tsgo's inference takes P from the arguments (no excess property check against Options), tttt the
  plugin's parameters `[options?: Readonly<Options> | null | undefined]`. Cut-down probes without unified's Plugin
  conditional return type and CallableInstance base didn't reproduce. Repro test/cases/npm__nirholas_crypto-news_TS2345.ts
  (11 files from reduce.py). (claimed: cor10) Cause: tttt's infer_to_union took two tuples of one arity as closely
  matched (one tuple target), so `[{ detect, ignoreMissing }]` was used up by `[boolean]` and reached P only at the
  naked-type-variable priority, below the plugin's contravariant candidate. tsc's isTypeCloselyMatchedBy needs a
  symbol, which a tuple target lacks: the argument tuple goes to P directly, and the covariant inference wins. Fixed in
  is_closely_matched (src/ty/infer.c). [fixed 2026-10-24]
- Seed 20261028, tamagui/tamagui (@tamagui/codemod-flat-values@3.1.3, code/core/codemod-flat-values): spurious
  TS2339 "Property 'replaceWithText' does not exist on type 'never'" and TS2349 at src/transition.ts:302 (tsgo 0
  errors, tttt 2). `let node: A | B | null = null`, assigned in a loop after a type-predicate `continue`
  (`if (!isJsxAttribute(attribute)) continue; node = attribute`), then read through an optional chain (`node?.m()`),
  then `if (!node) return`, then `() => node!.replaceWithText(..)` in a closure past the last assignment. Without the
  optional-chain read it matches. Guess: the closure's narrowing past the last assignment (tsc 5.4's
  isPastLastAssignment) goes through the optional chain's flow condition and narrows to never. Repro
  test/cases/npm__tamagui_codemod-flat-values_TS2339.ts (13 lines). (claimed: cor10) Root cause: `node!` there is an
  incomplete never in tsgo too (the K2 query, initial type = declared because of the `!`, reaches its loop label while
  that loop is in progress through the optional chain's branches): the silent never. tttt's flow made it silent as well,
  but the walk's property access check (src/expr.c, EXPR_PROPERTY) and resolve_call_inner (src/ty/call.c) reported on
  it; tsc's checkPropertyAccessExpressionOrQualifiedName and resolveCallExpression return early for silentNeverType.
  Errors fixed (the flip: tsgo 0, tttt 0 now) [flip] [fixed 2026-10-23]; left: the repro's types baseline, where tsgo's
  type writer asks again after the loop caches are complete and prints `node!` as JsxAttribute while tttt prints the
  memoized never (a harness-only difference: tttt memoizes the first, incomplete, evaluation). [text]

## Real-world flips from other sources
- **rw_foldkit_TS6133** (source sweep, foldkit packages/foldkit/tsconfig.build.json and tsconfig.docs.json; tsgo 0,
  tttt 1): `import { Record } from 'effect'` (a namespace, `export * as Record`) used only as `Readonly<Record<K, V>>`
  in types. tsgo's getTypeFromTypeAliasReference, for a generic alias reference that isn't another alias's body, also
  resolves the name with SymbolFlags.Alias (the import alias it would print the instantiation by), which marks the
  import referenced; tttt's name resolution found only the global Record and reported TS6133. Fixed: the reference
  remembers such an import alias (IdentifierTypeRare.args_alias) and lowering marks it under tsc's condition
  (src/ty/lower.c). (claimed: cor10) [flip] [fixed 2026-10-23]

## Seed 20261100 (second box)
- npm_ciceksepeti_cui-listbox_TS7006 (@ciceksepeti/cui-listbox, cactus-ui packages/listbox): tsgo reports TS7006 on the unannotated second parameter of a generic arrow passed to a generic `forwardRef(render: (props: P, ref: Ref<T>) => ...)` when the arrow's first parameter type is erroneous (here `Poly<C, {}>` with a value used as a type, in the real project an unresolved import); tttt gives the parameter a contextual type and reports nothing. Not a flip (29 vs 23 errors). Not about the erroneous sibling (det3): a function expression with its own type parameters isn't context sensitive (hasContextSensitiveParameters), so tsc's getContextuallyTypedParameterType gives its unannotated parameters nothing, and they're TS7006 with `{ a: C }` as the first parameter's type too. tttt's assign_contextual_signature_as() typed them anyway. A generic generator that yields a context sensitive expression is still context sensitive (hasContextSensitiveYieldExpression doesn't look at type parameters, bun06-C9), and ty_function_is_context_sensitive() now says so. [fixed det3]
- decentraland-builder-assets: text-only; tsconfig `"diagnostics": true` makes tsgo print timing stats (`Bind time: ...`), tttt ignores the option; same 6 errors and exit code. [text]
  element. [open]
- **npm_dmptranscripts_TS2344** (seed 20261031, dmptranscripts@1.0.0, Deneyap-Mezunlar/dmptranscripts; tsgo 16,
  tttt 18). discord-api-types 0.33.5's payloads/v10 and v9 guild.d.ts: `interface APIPartialGuild extends
  Omit<APIUnavailableGuild, 'unavailable'>, Pick<APIGuild, 'welcome_screen'> { name; icon; ... }` and `interface
  APIGuild extends APIPartialGuild { ... welcome_screen?: ... }`; invite.d.ts's `Pick<APIGuild, 'id' | 'name' | 'icon'
  | ...>` then gets TS2344 in tttt ("'icon' is not assignable to type 'keyof APIGuild'"). A base type that reads its
  own derived interface through Pick (keyof APIGuild while APIGuild's bases resolve): tttt's keyof APIGuild lacks the
  members inherited from APIPartialGuild, likely a member set cached while the base list was in flight. tsgo gives no
  error at all (no TS2310 either). Cut-down: 2 files, 10 lines. (claimed: cor10) Cause: Pick<APIGuild, 'welcome_screen'>
  reads APIGuild's modifiers while APIPartialGuild's members are resolved; APIGuild then inherited nothing from the
  in-flight APIPartialGuild (no early members) and kept that. tsc's resolveObjectTypeMembers sets the members table
  (declared members, then each base's added into it) before it adds the inherited ones, so a reader meanwhile sees
  what is there so far. Fixed in resolve_declared_members (src/ty/members.c): the members so far are published as
  early members before each base is added, except after a re-entry while the bases were lowered and for a mapped base
  over this type's own keys (`interface D extends Renamed<D>`, bun87-R18b: tsc got `keyof D` in isValidBaseType, from
  D's members without bases). [fixed 2026-10-24]
  - The other order, the Pick<APIGuild, ...> use before the declarations are checked: tttt reports TS2310 on APIGuild,
    tsgo nothing. base_is_dynamic() asks for the base's members while APIGuild's bases are lowered (inside its
    RESOLVING_BASES window), which reaches Pick and APIGuild's members there; tsc's getBaseTypes never resolves a base's
    members (hasBaseType walks bases only). Moving it out of the window broke ttttCircularBaseDefaultArgument (the A/B
    interface cycle is found through that re-entry): the cycle check needs a bases-only walk instead. Repro: put
    `export type First = Pick<G2, 'id'>;` above the declarations of test/cases/ttttBaseReadsDerivedThroughPick.ts.
    Fixed with the thread dependence it caused (npmsample_box2.md, seed 20261107): an interface's bases decide
    `dynamic` after the window, with a bases-only cycle check (has_base_type) inside it; the members so far are early
    members then too; and a type's object-level members made from its raw members while those are partial aren't
    kept (ty_members_resolving()). [fixed 2026-10-24]

## Seed 20261103 (second box)
- npm_task-definition-ts_TS2339 (task-definition-ts@1.0.18, config tsconfig.bench.json, allowJs+checkJs, no include): tttt's default wildcard include adds `dist/index.mjs` although `dist/index.d.mts` sits beside it, and checks it (TS2339/TS7006 x6); tsgo drops the .mjs (same-basename extension priority: a declaration file beats the JS file), as tttt already does for `index.cjs` next to `index.d.cts`. Guess: the priority removal in tttt's wildcard expansion knows .d.cts/.cjs but not .d.mts/.mjs. Fixed: tsgo's LEGACY BEHAVIOR keeps only a `.js`/`.jsx` next to its `.d.ts`; tttt kept every JS file next to any declaration (has_higher_priority_sibling). [flip] [fixed corF 2026-10-24]
- **npm__skastr0_quartz-engine_TS2719** (seed 20261025, @skastr0/quartz-engine@0.2.3, skastr0/quartz
  apps/opencode-plugin/src/server.ts:22): a generic arrow's return type annotation `V extends undefined ?
  Record<never, never> : { readonly [P in K]?: V }`, with an `as` to the same conditional type written again in its
  body. tsgo finds the two deferred conditional types unrelated: its elaboration ends at "Type 'V' is not assignable
  to type 'V | undefined'", two distinct V's, which suggests one side's V is a substituted or instantiated copy.
  tttt relates them. Guess: tsgo relates a deferred conditional to another only when the check and extends types are
  identical and both branches are related both ways, through instantiations its constraint machinery makes distinct.
  The fix needs the tsgo trace (scripts/tsgotrace.py 2719). Root cause: tsgo 7.1's getMappedType maps a distributed
  type parameter like its original, and a mapper that doesn't map it gives the original, not the distributed twin. So
  mappedTypeRelatedTo's template instantiation (the key mapper) turns the source's distributed V into V, against the
  target's distributed V: unrelated, TS5113 in the chain. tttt's ty_instantiate_mapped returned the twin unchanged, and
  report_missing_constraint didn't see the twin as the source (TS2208 tsgo doesn't add). Repro
  ttttDistributedParamMappedTemplate. (claimed: corB) [flip] [fixed f840ca64f]
- **npm__tera-ui_tooltip_TS7031** (seed 20261025, @tera-ui/tooltip@1.3.3, hieumau12/tera-ui
  packages/utilities/shared-utils/src/object.ts:9): `(k: string, {[k]: old, ...others}) => ...`, an unannotated
  parameter's binding pattern with a computed non-literal key. tsc's getTypeFromObjectBindingPattern leaves that
  element out of the implied type, and a rest element adds an `any` index signature. The element then indexes the
  implied type by string: `any` with a rest element (no TS7031), TS2537 "has no matching index signature" without
  one. tttt reports TS7031 for the element in both cases, and misses the TS2537. (claimed: cor10) Fixed in
  binding_add_implicit_any_type_errors (src/binding.c): such an element (its key's type not usable as a property name,
  tsc's isTypeUsableAsPropertyName) isn't reported; repro test/cases/ttttComputedBindingKeyImplicitAny.ts. [fixed 2026-10-24]
  - Seen on the way: with a unique symbol key (`{ [s]: a }` with `declare const s: unique symbol`) tsgo prints the
    implied parameter type with the symbol's internal name (`{ "\uFFFD@s@281474976710687": any; }`), tttt `{ [s]: any; }`:
    types baseline only. [text]
- **npm__nest-excalibur_common-api_TS2345** (seed 20261025, @nest-excalibur/common-api@0.1.0,
  src/api/api-principal/services/principal.service.ts:55): typeorm's `repository.update(id, record)` with a
  `DeepPartial<Entity>` record, where the parameter is `QueryDeepPartialEntity<Entity>`. Each mapped property is a
  deferred conditional type over `Entity[P]` with `infer U`. tsgo relates them only if their extends types are
  identical, and two `infer U`s are distinct type parameters, so the source falls back to its branches and fails:
  TS2345. tttt finds them assignable. Likely related to npm__skastr0_quartz-engine_TS2719, where tttt also relates
  two deferred conditionals that tsgo doesn't. Not the same root cause: no distributed type parameter is involved
  (it still differs after quartz's fix); tttt's relation reaches deeply-nested Maybe results through
  `U[P][string]`-like indexed accesses where tsgo's fails first (TTTT_DEBUG_RELATE_NESTED). Root cause: an indexed
  access (or type parameter) without a constraint relates as `unknown` by a plain is_related_to, which pushed the
  target too (tsc: RecursionFlags.Source only); along `T[P][string]` -> `T[keyof T][string]` -> unknown the target
  conditional was pushed at each step, became deeply nested, and the Maybe passed. Repro
  ttttUnconstrainedSourcePushesSourceOnly. [fixed 888a8a4f5]
- **npm__mui-treasury_component-number-input_TS2353** (seed 20261031, @mui-treasury/component-number-input@0.0.1-alpha.3,
  siriwatknp/mui-treasury-next; tsgo 75, tttt 81). Six `<TextField {...args} InputProps={{ ..., disableUnderline:
  true }}/>` in packages/style-textfield-{ant,bootstrap,standard} stories, each under a `{/* @ts-ignore */}` line.
  TextFieldProps is a union of variants (standard's InputProps has disableUnderline, outlined's doesn't) and the spread
  makes the attributes a union. tsgo reports TS2322 for the whole attributes type at the tag name (suppressed there by
  the ts-ignore); tttt elaborates into the attribute and reports TS2353 at the property line, which the ts-ignore
  doesn't cover. Without the ts-ignore both report, at different lines and codes. Hand-cut repro (reduce.py didn't
  converge in an hour on MUI's types). Not the elaboration (det3): elaborateElement relates InputProps to the
  union's `Partial<StdInput> | Partial<OutInput> | undefined` and finds it assignable, in both. It's the excess
  property report inside the relation. tsgo's hasExcessProperties takes the JSX message and keeps the error node (the tag
  name) when the error node is JSX, also for an object literal nested in an attribute's value. tttt tells "the error
  node is JSX" by the outermost source being JSX attributes, and with a spread of a union that source is a union of
  them. relate.c's is_jsx_attributes_source() now accepts the union. [fixed det3]
- Seed 20261033, DevExpress/devextreme-reactive: `return <P params={...params} />;` in an arrow inside JSX children.
  tsgo reports TS1109 at the `...`, ends the attribute list there and the element without `/>`, and then its
  parseList recovery (abortParsingListOrMoveToNextToken: isInSomeParsingContext, where the enclosing JSX children
  context takes any token) unwinds out of the statement list, arrow and JSX expression back to the children, so
  `...params} />;` and the next `}` are JSX text (TS1381, TS1382). tttt's parse_jsx_attributes stops too, but the
  rest is parsed on in the arrow body (`/>;` a regex: TS1161, then TS1005). The parser has no general unwinding to an
  enclosing context that would take the token. Repro test/cases/npm__devexpress_dx-react-grid-material-ui_TS1381.tsx.
  Fixed: parse_jsx_children enters a PARSING_JSX_CHILDREN list context that takes every token, so list_abort_or_skip
  unwinds to the children from any list inside them, as in tsc. That made a speculative arrow function in a ternary
  (bun98-J1, jsxTernaryWithObjectInAttribute) succeed through the new aborts, so tsc's
  typeHasArrowFunctionBlockingParseError went in too (type_blocks_arrow_function: a return type that is a function
  type missing its parameter list ends a non-committed arrow). [fixed 5b926eff3]
- Seed 20261033, ionic-team/stencil-component-starter again (another npm package of the same template repo): the
  TS2320 flip above. [dup]

## Seed 20261102 (second box)
- **npm_skilltrace_TS2322** (seed 20261102, skilltrace@0.3.3, hideya/skilltrace tsconfig.bench.json; tsgo 0, tttt 1) [flip]:
  spurious TS2322 at a generic helper returning a mapped type built from drizzle-orm's `ExtractTablesWithRelations<...>[T["_"]["name"]]["columns"]`
  (`{ [K in keyof ({} & ({ [K in keyof ...]?: boolean } ...))]: ...}` not assignable). Reduced to 40 files / 380 lines (drizzle's .d.ts
  cut down, `~/` paths made relative); not cut by hand further. Guess: assignability of a deferred mapped type over an intersection
  with a conditional (`undefined extends Record<...> ? ...`) to another mapped type that tsgo relates via the template/modifiers.
  Root cause (det3): `findFirst`'s TSelection is inferred from the return context as `Omit<C, "where">`, but tttt
  failed its constraint check and fell back to the constraint. The relation descends to drizzle's FindTableByDBName:
  `keyof { [K in keyof ETR as ETR[K]["dbName"] extends N ? K : never]: ... }` against the same shape over another
  ETR object. That is the same alias and arguments, but `type Schema = ExtractTablesWithRelations<...>` makes a twin
  (ty_alias_body_mapped_instance()), so the `as` types aren't identical and mappedTypeRelatedTo fails, in tsgo too.
  tsgo then relates the source to the target's keys: its `as` type and the apparent keys
  (structuredTypeRelatedTo's generic-mapped `keyof` rule, under isGenericMappedType, whose `as` clause alone makes it
  generic). tttt had that rule only for a generic constraint. Also found: in tttt alone the TS2322 came and went with
  the root files (the test's 40 files as `files` vs `include: ["app"]`), at one checker. Which twin a reference
  meets first decided whether the `as` types were identical. [fixed det3]
- **npm__xneog_dsh-anonymous-user-id_TS2344** (seed 20261102, @xneog/dsh-anonymous-user-id@0.1.0, gomes007-alt/xneog-harness
  vendor/schemastery/src/index.ts:85; tsgo 89, tttt 91, +2 TS2344): `Dict<TypeS<X>, TypeS<Y>>` / `Dict<TypeT<X>, TypeT<Y>>` where
  `Y extends Schema<any, string>` and `From<Y>` is a distributive conditional with a `X extends string|number|boolean ? Schema<X>` branch:
  tttt checks the constraint `string | symbol` against `Y | ...` / `(string|number|boolean) & Y` instead of the conditional's
  constraint with Y's own constraint substituted (Schema<any,string> is not a primitive, so that branch is never). Repro is the
  vendored file's types, 15 lines. Root cause: From's body starts with a leading `|`, which makes the conditional's check type a union node of one type (`| X`). tttt's is_naked_type_param_node looked at the node and found it not distributive, so `From<Y>`'s constraint wasn't the distributive one with Y's constraint substituted. tsc's isDistributive looks at the type, a type parameter. Repro ttttLeadingBarConditionalDistributes. [fixed 22fdb6b1c]
## Seed 20261035

- kirka@6.1.0 (tsconfig.bench.json), src/result/api/debug.ts:2, TS7006: a destructured parameter default `{ ok = (v) => v, err = (e) => e } = {} as { ok?(v: T): string; err?(e: E): string }` gets its arrow functions contextually typed from the `as` type in tsgo (no error); tttt reports the parameters implicitly any. Test npm_kirka_TS7006. (claimed: cor10) Cause: tttt gave the initializer what the pattern implies as its contextual type eagerly, which checked the defaults' arrow functions without context first (their parameters any, TS7006); tsc asks getContextualTypeForInitializerExpression only on demand, and a type assertion's operand takes the asserted type as context, never the outer one. Fixed in initializer_ignores_context (src/binding.c), repro test/cases/ttttAssertedInitializerDefaults.ts. [flip] [fixed 2026-10-24]
  - The same with a non-generic call initializer (`const { a = (v) => v } = mk()` with `mk(): X`): tsc never asks the call for its contextual type (no inference), tttt still checks the defaults without context first (TS7006). Needs initializer_ignores_context to know the call's signatures aren't generic, or the pattern's type computed on demand. [open]
- slotsmith@1.10.0 (tsconfig.bench.json), src/data-table/__tests__/integrations/shared.tsx:6, TS7006: `validate: (name) => ...` in an array literal typed `DataTableColumnDef<Employee>[]` (tanstack ColumnDef & a conditional IsAny type over a mapped-by-DeepKeys union with `edit?:` method-signature members) is contextually typed in tsgo, implicit any in tttt. Test npm_slotsmith_TS7006 (9 files of the reduction). Fixed: discriminate_by_items() returned a union contextual type of more than 64 members undiscriminated (the mapped DeepKeys union of the recursive Employee has more), tsc has no such limit. [flip] [fixed corF 2026-10-24]
- better-auth@1.7.12 (packages/better-auth/tsconfig.bench.json), src/plugins/organization/adapter.ts:234: tttt reports TS2322 and TS2352 (a cast between `InferOrganization<O>` mapped types differing in the `isClientSide` argument is "neither type sufficiently overlaps") where tsgo reports TS2345 (tsgo 3 errors, tttt 5; not a flip). NO repro yet: reduce.py cut the 4 own files to ~120 lines (db/field.ts, db/index.ts, plugins/organization/{schema,adapter}.ts keep the TS2352 at adapter.ts:21), but the package reduction (zod, better-call, @better-auth/core under packages/better-auth/node_modules) produced syntax-broken files, and a hand-inlined copy of the own files alone does not reproduce. Repro and fix (det3): reinstalled at the seed's commit d32b5b271 in a sandbox, then cut by hand to test/cases/npm_better-auth_TS2352.ts. The alias InferAdditionalFieldsOutput<N, O, isClientSide> has a bivariant isClientSide (false vs true are not comparable), but its N is a mapped type's key in the conditional's extends type. tsc relates those extends types by identity through mappedTypeRelatedTo, which instantiates the source constraint with the reporting mapper, so N is unreliable and the variance check falls back to structure, where the branches are comparable. tttt's mapped_identical() didn't report it. All four tttt-only diagnostics go (adapter.ts 234/560/768 TS2352 and crud-members.ts:350 TS2322); what's left is the known lynx/react-store -TS2345. [fixed det3]

## Seed 20261104 (second box)
- npm_znicholasbrown-graphql-tools_TS2345 (znicholasbrown-graphql-tools@4.0.6, packages/testing/utils.ts): spreading `...args: Parameters<TSync | TAsync>` into a call of `executeSync: TSync` (TSync extends (...args: TArgs) => any, TAsync likewise, TArgs extends any[]) gives tttt TS2345 "Argument of type 'any[]' is not assignable to parameter of type 'TArgs'"; tsgo accepts it. Guess: the spread of Parameters<union of type parameters> is resolved to its constraint `any[]` and compared against TArgs, where tsgo keeps the deferred conditional/rest type and relates it through the union's constraint. Root cause: `Parameters<TSync | TAsync>` distributes into `Parameters<TAsync> | Parameters<TSync>`, a union of generic members; mutable_array_or_tuple_union (tsc's getMutableArrayOrTupleType for a lone spread) gave up on generic members, so the spread became `any[]`. tsc keeps a member whose base constraint is a mutable array or tuple and wraps another array-like one as `[...Y]`. Repros: this test and ttttSpreadUnionOfGenericArrays. [flip] [fixed e1165d76b]
  test/cases/npm__tamagui_codemod-flat-values_TS2339.ts (13 lines). A duplicate of the tamagui case above. [dup] [fixed abcb33b0d]
- Seed 20261032, NGHQ/types-momentum (types-momentum@4.6.12): spurious TS2322 "Type 'DayStreak[]' is not assignable to
  type 'boolean[]'" at src/methods/dates.ts:74 (tsgo 0 errors, tttt 1): `const mutable: boolean[] = [...runningStreak]`
  with `runningStreak: FixedLengthArray<[DayStreak x7]>`, `Pick<T, Exclude<keyof T, number | mutators>> & {
  [Symbol.iterator]: () => IterableIterator<...> }`. That type is array-like (assignable to `readonly any[]`) without a
  number index: tsgo's checkArrayLiteral takes the spread as a variadic element, normalized to a rest element of its
  number index type or the error type, so `[...x]` is any[]; tttt iterated it. Repro
  test/cases/npm_types-momentum_TS2322.ts. [flip] [fixed 83a82b68e]
- Seed 20261033, ergolabs/ergo-sdk-js (@ergolabs/cardano-dex-sdk@0.1.19), project level, no reduced test: with
  skipLibCheck off, `import * as R from "ramda"` alone (src/wallet/boxSelector.ts) makes tsgo report TS2589 "Type
  instantiation is excessively deep and possibly infinite" and TS5114 "Instantiations of type '___ListOf' appear
  infinitely circular" at node_modules/@types/ramda/index.d.ts(644,103) (`flip`'s `_.F.Curry<(...args:
  _.T.Merge<[P[1], P[0]], P>) => _.F.Return<F>>`) and TS2589 at node_modules/ts-toolbelt/out/index.d.ts(5286,12);
  tttt reports neither (tsgo 9, tttt 6 diagnostics). The depth/circularity limit is reached in checking the
  declaration file, not by a use. reduce.py on the node_modules files timed out after 50 minutes (ts-toolbelt's
  single index.d.ts) and the partial cut no longer reproduced; the clone is deleted, the project is reproducible
  from test/npmsample/20261033.setup.jsonl. [open]

## Seed 20261036

- Seed 20261036, theodo/serverless-openapi (serverless-openapi@1.1.2), tsconfig.bench.json, src/parse.ts:50, TS2339: tsgo reports "Property 'bundle' does not exist on type 'typeof $RefParser'" for `import * as $RefParser` of an `export =` class (json-schema-ref-parser) with a static `bundle`; tttt resolves the static (the reduced test then reports TS2554 on the call instead). Test npm_serverless-openapi_TS2339. The reduced test is faithful: tsgo's TS2339 against tttt's TS2554, a call that
  resolved because tttt kept the static. Cause: with a synthetic default, the namespace import's type is tsc's
  getSpreadType of the `export =` value into `{ default }`, which drops a class's methods and accessors and private
  or protected members (isSpreadableProperty). lower.c's with_synthetic_default() copied every property; it now
  filters with ty_is_spreadable_prop(). [fixed det3]

## Seed 20261038

- Seed 20261038, @0xsonic/sdk@0.2.6 (sahilpabale/sonic-sdk), tsconfig.bench.json, src/components/Connect.tsx:69 (reduced: User.tsx:2), TS2590: tsgo reports "Expression produces a union type that is too complex to represent" for a JSX `<Text>` of @chakra-ui/react (system props types); tttt reports nothing (complexity limit not reached or counted differently). Test npm__0xsonic_sdk_TS2590. Two causes, both needed (also for iwatakeshi's below): (1) tttt's intersect_unions took undefined out of `(As<any> | undefined) & (As | undefined)` by rebuilding each union from its members, so As<any> and As (an imported alias referenced without arguments, its own instantiation) became one union and the intersection collapsed; tsc's filterType gives back the named union of the origin, and As<any> & As is a 709-member cross product. (2) tsc's somePropertyReducesToNever (getReducedType in inference) makes the intersection's property of each name two members have, and createUnionOrIntersectionProperty intersects two property types at once: 709 x 143 is TS2590; tttt's never_reduced_prop only intersected literal-like types. Repro ttttReducedIntersectionPropCrossProduct. [flip] [fixed 2ed1714f0]
## Seed 20261037

- @iwatakeshi/chakra-prose@0.0.2 (tsconfig.bench.json, src/stories/Page.tsx:27, reduced to :6): tsgo reports TS2590 "Expression produces a union type that is too complex to represent" on a `<Box p={10}>` JSX element with chakra-ui children (14-file case, `test/cases/npm__iwatakeshi_chakra-prose_TS2590.tsx`); tttt does not (tsgo 2, tttt 1 errors). Probably the union-size limit counted differently in JSX attribute/props checking. Same root cause as 0xsonic's above (the undefined-free intersection of As<any> and As, and the eager intersection of two-member properties in getReducedType). [fixed 2ed1714f0]
- @big-whale-labs/seal-hub-memorabilia-token-contracts@0.0.4 (tsconfig.bench.json, node_modules/ganache/dist/ganache.d.ts:43 and :7789/:7801 in the full program): tsgo reports TS2344 "Type 'G' does not satisfy the constraint 'unknown[]'" on ganache's `ExclusiveGroupOptionPairs<C, G extends unknown[]>` use with `G extends ExclusiveGroup<C>` (indexed access of `C["exclusiveGroups"]` through a `number & keyof` index); tttt omits it (tsgo 13, tttt 9; `test/cases/npm_BigWhaleLabs-seal-hub-memorabilia-token-contracts_TS2344.ts`). Constraint of an indexed-access type parameter G not resolved to unknown[]. [open]
## Seed 20261040

- ag-sockets@14.0.4 (Agamnentzar/ag-sockets, tsconfig.bench.json, src/serverSocket.ts:149, reduced to a 20-line case): tsgo reports TS2345 when a `import * as ws from 'ws'` (an `export =` class merged with a namespace, @types/ws) is used as a type, `socket: ws`, and a `Server<T extends WebSocket>` callback's `client: T` is passed to it (the type is the module's namespace type whose `on(): this` returns are incompatible with `this`); tttt reports nothing (tsgo 19, tttt 18 errors). `test/cases/npm_ag-sockets_TS2345.ts`. [open]
- **rw_vue_TS7006** (the vue corpus, tsconfig.corpus.json, packages/compiler-sfc/src/style/pluginScoped.ts:190 and
  :235; the corpus' long-tolerated "0/2 missing", also seen by the second box): tsgo's two TS7006 'ss' in
  `n.nodes[0].each(ss => ...)`. reduce.py cut it to the file's `selector.each(n => { if (n.type === 'pseudo')
  n.nodes[0].each(ss => {}) })` and 30 lines of postcss-selector-parser.d.ts (`type Selector = _Selector<Selector>`,
  `interface _Selector<S> extends Container<string, Diff<Node, S>>`, `type Diff<T, U> = T extends U ? never : T`).
  Both sides report TS5115 ('Container', 'Diff'). tsgo reaches its instantiation depth limit in a chain
  getConditionalType (Diff) -> isTypeAssignableTo -> isWeakType -> resolveStructuredTypeMembers of a reference ->
  resolveObjectTypeMembers instantiating its bases' type arguments -> Diff again (tsgotrace.py 5115), and the
  errorType it returns at depth 100 ends up in Pseudo's members for good: `Pseudo.nodes` is `any[]`, so
  `n.nodes[0].each` is any and the callback is checked untyped (resolveErrorCall: TS7006). tttt's TS5115 comes from
  another path (probe_eager_bases -> a reference's members re-entered, members_reentered_unguarded, reported as the
  limit), never a 100-deep chain, so no member of Pseudo becomes the error type and `ss` gets `Pseudo | Root`. Matching
  it means tttt's instantiation sequence reaching tsgo's depth-100 point on the same type (the order members, bases
  and conditional types are instantiated in), not a local rule. Repro test/cases/rw_vue_TS7006.ts (fails). [design]
