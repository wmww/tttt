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

- kirka@6.1.0 (tsconfig.bench.json), src/result/api/debug.ts:2, TS7006: a destructured parameter default `{ ok = (v) => v, err = (e) => e } = {} as { ok?(v: T): string; err?(e: E): string }` gets its arrow functions contextually typed from the `as` type in tsgo (no error); tttt reports the parameters implicitly any. Test npm_kirka_TS7006. (claimed: cor10) Cause: tttt gave the initializer what the pattern implies as its contextual type eagerly, which checked the defaults' arrow functions without context first (their parameters any, TS7006); tsc asks getContextualTypeForInitializerExpression only on demand, and a type assertion's operand takes the asserted type as context, never the outer one. Fixed in initializer_ignores_context (src/binding.c), repro test/cases/ttttAssertedInitializerDefaults.ts. [flip] [fixed 234bae0b7]
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
  from test/npmsample/20261033.setup.jsonl. Same key as npm_paseo-agent-todos_TS5115, whose write-up (seed 20261045)
  shows tsgo's depth-100 instantiation limit reached through eager instantiation that tttt doesn't do. Here the chain is
  ts-toolbelt's own recursion (`___ListOf`) checked in the declaration file. The mechanism is not traced, so it may be
  a different route to the same limit; it is depth-limit territory either way. [design]

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
- @big-whale-labs/seal-hub-memorabilia-token-contracts@0.0.4 (tsconfig.bench.json, node_modules/ganache/dist/ganache.d.ts:43 and :7789/:7801 in the full program): tsgo reports TS2344 "Type 'G' does not satisfy the constraint 'unknown[]'" on ganache's `ExclusiveGroupOptionPairs<C, G extends unknown[]>` use with `G extends ExclusiveGroup<C>` (indexed access of `C["exclusiveGroups"]` through a `number & keyof` index); tttt omits it (tsgo 13, tttt 9; `test/cases/npm_BigWhaleLabs-seal-hub-memorabilia-token-contracts_TS2344.ts`). Constraint of an indexed-access type parameter G not resolved to unknown[]. Root cause: G's constraint `ExclusiveGroups<C>[number]` goes through `C["exclusiveGroups"]`'s constraint `ExclusiveGroup[] | undefined`. tsc's getIndexedAccessTypeOrUndefined finds no index signature on that union (undefined has none), so there is no constraint and G isn't an array. tttt's access skips nullable union members, which is how an expression reads them, so the constraint was the element type. The constraint path now uses ty_access_for_constraint (ACCESS_NULLABLE_FAILS). Repro ttttOptionalPropIndexedConstraint. [fixed 2de37e36a]
## Seed 20261040

- ag-sockets@14.0.4 (Agamnentzar/ag-sockets, tsconfig.bench.json, src/serverSocket.ts:149, reduced to a 20-line case): tsgo reports TS2345 when a `import * as ws from 'ws'` (an `export =` class merged with a namespace, @types/ws) is used as a type, `socket: ws`, and a `Server<T extends WebSocket>` callback's `client: T` is passed to it (the type is the module's namespace type whose `on(): this` returns are incompatible with `this`); tttt reports nothing (tsgo 19, tttt 18 errors). `test/cases/npm_ag-sockets_TS2345.ts`. Mechanism (det3): with esModuleInterop, resolveESModuleSymbol makes the namespace import's alias target a clone of the class symbol (cloneTypeAsModuleType), and `ws` in a type position is getDeclaredTypeOfSymbol(clone): a second class type, with its own `this`, printed `ws`. The callback's `client: T` defaults to the original WebSocket (printed `import("ws")`). Relating the two fails on `on(): this`. tttt resolves the type `ws` to the original class instance (printed `import("ws")` there too), so both sides are one type. A fix means a twin declared class type for the clone, with its own this-type, made wherever a type reference goes through such a namespace import. Fixed (det3): lower.c namespace_import_class() makes that twin, keyed by the import alias (make_declared() of the same class symbol: its own members lower `this` through the canonical declared type, so they keep the original's this), printed through the import or by its bare name (ty_clone_class_import()). Also fixes the type baseline's `socket: ws`. [fixed 366b63436]
  Variance probe (corF, 2026-10-24, after 366b63436): det3's doubt was that the twin shares the original class's variances (keyed by the class symbol) where tsc measures the clone apart. Two generic `export =` classes merged with a namespace, imported via `import * as ns` and `import C = require`, with relations ns<A>/ns<B>, ns<A>/C<B>, ns<A>/import("m")<B> and conditional `extends` checks. One class used T in a property, a callback, a method parameter, a conditional and `on(): this`; the other only through `this` (`on(f: (self: this) => T)`, `this extends C<infer U>`). tsgo and tttt agree on every relation (tscbaseline.sh vs filetest.sh, the probe passes): no divergence found, nothing changed.
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
## Seed 20261042

- mlok@0.1.5 (smolijar/mlok, tsconfig.bench.json, src/index.spec.ts:77, reduced to a 20-line self-contained case): tttt reports TS2322 "Type 'string' is not assignable to type '"foo"'" on `const foo: 'foo' = mlokNode<Req>({}).override({ getHeaders: () => 'foo' } as const).getHeaders()`; tsgo accepts (the recursive conditional `Mlok<..., O>` intersection with a `as const` override should give a return type whose call yields `"foo"`; tttt picks the base `string`). tsgo 24, tttt 25 errors. `test/cases/npm_mlok_TS2322.ts`. Cause (det3): the arrow's unit return type. tsc computes it lazily, after the call inferred O from the argument itself, so its contextual signature is its own and getReturnTypeFromBody keeps "foo". tttt has that through call anchors, but record_anchors_in() didn't go through a const assertion (whose operand has the assertion's contextual type), so `id({ f: () => 'foo' } as const)` widened to string. [fixed det3]

## Seed 20261045

- **npm_paseo-agent-todos_TS5115** (paseo-agent-todos@0.1.1, tsconfig.bench.json, client/todos-surface.tsx:6, 13-file case with zod's v4 core `.d.cts` reduced in node_modules): tsgo reports TS5115 ('$ZodTypeInternals', '$InferZodRecordInput', 'IsOptionalIn') from `useQuery({queryFn: ...})` over zod types; tttt reports nothing (tsgo's instantiation depth limit is not reached). Same key as ergolabs-ergo-sdk-js's TS5115.
  corF's analysis (2026-10-24, tsgotrace.py 5115 --suite on the test): tsgo's cycle is resolveStructuredTypeMembers ->
  resolveObjectTypeMembers instantiating a reference's *base types* with their type arguments -> a base argument that
  is a conditional type, `$ZodTypeInternals<$InferZodRecordOutput<K, V>, $InferZodRecordInput<K, V>>` (the base of
  $ZodRecordInternals<ZodString, ZodJSONSchema>), resolved eagerly by getConditionalTypeInstantiation ->
  getConditionalType -> isTypeAssignableTo([ZodJSONSchema], [OptionalInSchema]) -> getUnmatchedProperty -> members of
  ZodJSONSchema -> ZodUnion<[ZodRecord<ZodString, ZodJSONSchema>]> -> `optin: IsOptionalIn<T[number]>` ->
  isTypeAssignableTo(ZodRecord<ZodString, ZodJSONSchema>, OptionalInSchema) -> getUnmatchedProperty -> members of that
  ZodRecord (no in-flight state in tsc: resolved again) -> its bases instantiated again -> ... until the instantiation
  depth hits 100: TS5115 naming the three types.
  - tttt never evaluates $InferZodRecordInput at all. It resolves a reference's members from the raw members
    instantiated lazily, not by instantiating its bases, and no property on the way depends on the Input argument:
    `optin` is what OptionalInSchema asks for, and it isn't typed by I. There is no members re-entry
    (ty_members_resolving()'s SW_LOOK_MINE is never hit) and no relation of the cyclic pair.
  - The cycle exists only because of tsc's eager base instantiation. A lazy evaluation of the same types terminates
    with a sensible answer, so it is a tsgo artifact of the vue kind, not a missed infinite type.
  - Emulating it means instantiating every base type argument of every reference whose members are resolved (tsc's
    resolveObjectTypeMembers). That is what probe_eager_bases()/members_reentered_unguarded() approximate for
    MembersRare.eager_bases, and only for bases whose instantiation "resolves something". Extending the probe to
    conditional base arguments would emulate this case, at the cost of evaluating conditional types tttt otherwise
    never needs, in every program, for a diagnostic that marks no real error. Not done.
  [flip] [design]


## Seed 20261044

- fui-fancyui@0.6.12 (tsconfig.bench.json), src/components/atoms/AlignedLabel/AlignedLabel.style.tsx:8, TS7053 (65x, plus TS7006): tttt types the destructured `$align` of a styled-components interpolation function (`styled.label<Props>`) as any, so indexing `Record<TTextAlignLRC,...>` errors; tsgo infers the prop type. Test npm_fui-fancyui_TS7053. (claimed: cor10) Cause: the parameter's type is `ExecutionContext & NoInfer<Props>`, and `NoInfer` of an object stays a substitution type (tsc's isNoInferTargetType); tttt's type facts took the intersection as generic with no base constraint, so it could be nullish and the destructuring read gave the error type. A substitution type now has the facts of its base intersection, and one of a non-generic type doesn't make an intersection generic (type_facts, ty/flow.c); repro test/cases/ttttNoInferIntersectionDestructure.ts. [fixed 2026-10-24]

## Seed 20261300 (large)

- lklynet/aurral at 4f5e4757 (root project, tsconfig.bench.json; tsgo 9, tttt 10): spurious TS2345 at
  frontend/src/pages/useDiscoverData.ts(87,51), `queryClient.setQueryData(discoveryQueryKey, updater)` with an untagged
  `QueryKey` and `updater: (current: DiscoverData | undefined) => DiscoverData | undefined` (@tanstack/query-core
  5.104.5). tttt inferred `TInferredQueryFnData = DiscoverData` through `NoInfer<TInferredQueryFnData> | undefined`:
  after the union's matched `undefined` came off, the leftover `NoInfer<T>` target went to infer_from_reduced(), whose
  getActualTypeVariable step stripped the NoInfer (tsc's inferFromTypes asks isNoInferType right before it). With
  `TInferred` its default `unknown`, the updater's `current` parameter fits like tsgo's. Repro
  test/cases/ttttNoInferUnionRemainder.ts (recreated from a sandboxed install; projdiff byte-identical after the fix).
  [fixed 2de37e36a]
- Zoo-Code-Org/Zoo-Code@main webview-ui (tsconfig.bench.json; a pnpm monorepo installed at the root), tsgo 0 errors,
  tttt 2: TS2322 at src/components/common/MarkdownBlock.tsx:405 (`'() => (tree: never) => void'` not assignable to
  `Pluggable`) and TS2345 in src/utils/__tests__/markdown.spec.ts:12, both from a remark plugin typed `(tree:
  Parameters<typeof visit>[0]) => void` (unist-util-visit 5.1.0). Parameters<> relates the overloaded generic `visit`,
  its signatures erased, to `(...args: P) => any` instantiated with the inferred P. At a rest parameter of the generic P,
  tsc's isInstantiatedGenericParameter holds, because tryGetTypeAtPosition gives `P[1]`. So the two `visitor` parameters
  relate as types (BuildVisitor's alias variance: true) and not signature against signature as callbacks (false:
  `parent: never`). tttt's is_instantiated_generic_param read the any that param_type_at() gives for an element of a
  non-tuple rest, so Parameters<> came out never. Test npm_Zoo-Code-Org_Zoo-Code_TS2322; repro of the rule:
  ttttRestOfInferredParametersNotCallback. (claimed: fixF) [flip] [fixed 5f3c08628]

## Named projects (the human, 2026-10-25)

n8n-io/n8n at 3c7a553dc, one sandboxed pnpm install (the repo wants pnpm >= 12.4.2: installsandbox.py's ALLOWED got
12.15.0), the workspace packages unbuilt (tsgo itself reports 15.5k TS2307 in packages/cli). Not flips, not
K-dependent.
- n8n (packages/cli, bench: tsBuildInfoFile -> null): tsgo 28064 / tttt 28071 at f81ef487c.
  - 60 TS2345 in tsgo that tttt reported as TS2741/TS2739 at the same sites (agent-config.service.ts:68,
    `composeJsonConfig(entity, ...)` with `type ProjectAgent = Agent & { projectId: string }`). tsc's
    getApparentTypeOfIntersectionType instantiates a class (or generic or non-thisless interface) member with `this`, a
    new intersection without the alias, so the missing property's line names `Agent & { projectId: string; }`, which
    doesn't repeat the head's `ProjectAgent`, and the head stays (reportRelationError's chainArgsMatch). tttt's line
    named the alias and dropped the head. relate.c apparent_intersection() now drops the alias for such members.
    Test test/cases/rw_n8n_TS2345.ts. [fixed dc6856bd0]
  - +TS7031 x7 (tttt only): agent-execution.service.test.ts:1911 ('allowed', x2), agent-background-job.service.test.ts:380
    ('expected', x5). vitest's `it.each<Row>(rows)(name, ({ a, b }) => ...)` picks the overload whose callback is `(...args:
    T) => ...` with T the (object) row type; tsc's tryGetTypeAtPosition takes `T[0]` with getIndexedAccessType, which is
    unknown without an access node, so the destructured parameter falls back to its pattern's type silently. tttt's
    signature_type_at() gave nothing, and the parameter was typed as uncontextual (TS7031 / TS7006). Test
    test/cases/rw_n8n_TS7031.ts. [fixed a4d3ab492]
  - Text only, after both fixes (projdiff: same (file, line, code), 28 lines):
    - TS2339 x4 at a destructuring of an aliased intersection (`const { id } = entity`, agent-config.service.ts:182,
      agent-task.service.ts:535, agent-tools.service.ts:1100, :1219): tsgo names the apparent type `Agent & { projectId:
      string; }` (getIndexedAccessType reports on getReducedApparentType), tttt named the alias `ProjectAgent`.
      check.c report_no_property() takes ty_apparent_intersection_for_display() for a binding element. Test
      test/cases/rw_n8n_TS2339.ts. [fixed 17d84c588]
    - TS2353 x9 with the EventEmitter truncation (as in nodes-base, below). [fixed b8aecd599]
    - One TS2322 elaboration at promotion-binding-preflight.service.test.ts:83: `{ name: "w", nodes, ...overrides }` with
      `nodes: INode[]` (an unresolved module's type) and `overrides: Partial<Pick<WorkflowEntity, ...>>` (WorkflowEntity
      unresolved too). tsgo's head prints the spread's properties as their left declarations say (`nodes: INode[]`,
      `name: string`) while its chain line prints them as the spread made them (`string | any` is any:
      `{ name: any; nodes: any; settings?: any; }`); tttt prints `{ name: string; nodes: any; ... }` in both. Cut down
      (6 lines): `import type { Entity, INode } from "missing"; type Content = Pick<Entity, "name" | "nodes" | "settings">;
      function make(nodes: INode[], o: Partial<Content> = {}): { content: Content } { return { content: { name: "w",
      nodes, ...o } }; }` (test/cases/rw_n8n_TS2322.ts, known failing). A printer question, looked at 2026-10-25 and left: the
      spread's properties are `any` (getSpreadType's union `string | any`), but they keep the left declarations, so in the
      head (the fresh literal) tsgo's addPropertyToElementList prints them through serializeTypeForDeclaration of the
      declaration (`nodes` shorthand -> the parameter's `INode[]`, `name: "w"` -> `string`), while the chain's source is
      the regular type, whose transient members (getRegularTypeOfObjectLiteral) have no declarations to reuse, so `any`.
      Emulating it means tttt's pseudo-type reuse accepting an error-typed declaration for an `any` property and
      dropping it for the regular type; rare (error types plus a spread), not a clean rule. [text]
- n8n-nodes-base (packages/nodes-base, bench: composite -> false, declaration true; tsBuildInfoFile -> null): tsgo 7138 /
  tttt 7138, same (file, line, code), 8 text-only lines:
  - TS2684 x3 (Airtop agent.utils.ts:15, run.operation.ts:175, methods/index.ts:46): tsgo prints `body?: IDataObject`
    in the 'this' context's signature, tttt `body?: any`. `body: IDataObject = {}` with IDataObject from the unbuilt
    n8n-workflow: tttt added undefined to the initialized parameter's type, which the named error type absorbs into
    any, and the printer's declared type (undefined taken out) was any. tsc's getTypeOfSymbol of an initialized parameter
    has no undefined, so the name stays (a `?` parameter's does print any in tsgo). members.c leaves such a parameter's
    error type alone. Test test/cases/rw_n8n_nodes_base_TS2684.ts. [fixed 17d84c588]
  - TS2353 x5 (FormTriggerV2.node.test.ts:308, 437, 540, 597, 638): where the long target type is truncated, tsgo
    `{ [EventEmitter.captureRejectionSymbol]?: ...; ... 105 more ...`, tttt names `off?: ...` instead. An
    instrumented tsgo (approximateLength printed per member of createTypeNodesFromResolvedType) gave 164 after that
    first member where tttt counted 128: the unique-symbol name. tsgo's addPropertyToElementList counts the escaped
    name, "\xFE@" + name + "@" + a 15-digit symbol id (BlockIdOffset 2^48 plus a count), and the computed name, printed by
    createExpressionFromSymbolChain, adds one more than each part's length (`EventEmitter.captureRejectionSymbol`: 13 +
    23). ty_display.c prop_symbol_name_length() counted tsc 6's `__@name@` with six digits and the name's expression
    nothing; now both like tsgo (approx_add_expression_text()). Test test/cases/rw_n8n_nodes_base_TS2353.ts; nodes-base is
    byte-identical with tsgo now, cli's 9 too. [fixed b8aecd599]

Seed 20269002 (fixF): vue (vuejs/core head, packages/vue) tsgo 5 / tttt 3, only the known pluginScoped.ts TS7006
[design]; firecrawl and immich-server set up by hand (npmsample's trial rejected them on TS2307 counts from unbuilt
workspace packages). Not flips, not K-dependent.
- firecrawl (firecrawl/firecrawl e1dc7a43, apps/api): tsgo 63 / tttt 69. +TS2345 at src/controllers/v2/types.ts(974)
  and 5 +TS2353 at 979-1004: `liftCheckPromptInjection(applyScrapeOptionsDefaults(obj))` with obj a `T extends
  ScrapeOptionsBase | undefined`. The inferred `T & { skipTlsVerification: boolean }` failed the constraint check
  against the weak `{ formats?: unknown[]; checkPromptInjection?: unknown }`, so the inference fell back to the
  constraint. relate.c intersection_apparent_is_union() returned true for any member whose apparent type is a union.
  tsc's getApparentTypeOfIntersectionType intersects the members' apparent types, and `undefined & { skip }` is never,
  so the source is `Base & { skip }`, compared property by property. Now the members' apparent types are intersected
  first. Test rw_firecrawl_TS2345; repro ttttIntersectionNullableConstraintMember. [fixed d70cb46ad]
- immich-server (immich-app/immich d474ec96, server/): tsgo 12 / tttt 13. +TS1272 at src/services/job.service.ts(86):
  `@OnEvent(...) async onJobRun(...[queueName, job]: ArgsOf<'JobRun'>)` with `import { ArgsOf } from
  'src/repositories/event.repository.js'` (a type-only export) under isolatedModules + emitDecoratorMetadata. tsc checks
  a rest parameter by getParameterTypeNodeForDecoratorCheck: GetRestParameterElementType gives `T[]`'s T or a type
  reference's first type argument (here the literal `'JobRun'`), else nothing (a parenthesized type, `readonly T[]`).
  tttt's check_decorator_metadata_signature checked the rest parameter's whole annotation. Test
  rw_immich-server_TS1272; repro ttttDecoratorMetadataRestParameter. [fixed 0470ef693]

morluto/rea at its default branch head (corF, 2026-10-25), one sandboxed npm install (private seed 20269004, records
not committed), a bench config next to each of its 4 tsconfigs. Not K-dependent.
- rea (tsconfig.json, .build, .reverse-tools-mutation: tsgo 0 / tttt 1 each; .frida-verification 0 / 0): +TS2345 at
  src/contracts/toolOutputSchemaPrimitives.ts:29, `z.strictObject({ ...evidenceSchema.shape, normalized_result: schema
  }).superRefine(validate)` in a generic `<Schema extends z.ZodType>` arrow. zod v4's object output is a mapped type
  with `as T[k] extends OptionalOutSchema ? never : k`; with one generic shape member its `as` clause is generic, which
  makes the mapped type generic (isGenericMappedType), but tsc's resolveMappedTypeMembers still gives it the properties
  of the keys whose names resolve. tttt's mapped members gave up on all of them, so `provider` was missing. Members.c
  now marks it generic and goes on key by key. A flip (tsgo clean). Test test/cases/rw_rea_TS2345.ts. [fixed fc76fdebc]
- paperclip-ui / paperclip-server (corB, seed 20269003): neither name is on npm unscoped; the match is paperclipai/paperclip's
  @paperclipai/ui (ui/) and @paperclipai/server (server/) at ee29e2850, one pnpm install, workspace packages unbuilt. `rea`
  resolves to stagas/rea 0.0.1 (2022, "a fancy wrapper over connect.router"), no tsconfig: nothing to check, probably not
  the project meant. ui: tsgo 14 / tttt 14, byte-identical. server (checked with NPMSAMPLE_ANY_TRIAL=1, 73 TS2307 from
  the unbuilt packages): tsgo 354 / tttt 355, not a flip, not K-dependent.
  - +TS2339 at src/services/tool-access.ts:2712, `revivedGrantMutation.previous` in `if (revivedGrantMutation)` where the
    variable is still its initializer's null. tsgo's narrowed type there is the silent never (incomplete flow: the walk
    meets a loop junction whose shared entry its back edge left incomplete), so the access is any-like and silent; a
    probe reference earlier in the branch makes tsgo report it too. tttt's check pass (ty_identifier_flow_issue) ran the
    reference's flow query a second time for the unassigned-variable issue, which cached that junction complete, and the
    later reference got a plain never. The issue is now remembered from the type's own query. Repro
    test/cases/ttttFlowIssueSingleQuery.ts (hand-cut, 15 lines; reduce.py on the 6481-file program was too slow and was
    stopped). Its types component still differs (tsgo prints `r.previous : error`, tttt `never`). [fixed 63514446a]

## Seed 20261301

- forgesteel@14.217.0 (tsconfig.bench.json), src/components/pages/library/library-print/library-print-page.tsx:19, TS2345: `useState<Element>(() => { switch (kind!) { case ...all members: return ...; } })` where `kind` is a destructured `useParams<{kind: K}>()` (`Partial`, so `K | undefined`): tsgo treats the switch over `kind!` as exhaustive (end unreachable, no implicit `undefined` return), tttt reports the callback as returning `Title | undefined`. Probably switch exhaustiveness through a non-null assertion on the switch expression. Test npm_forgesteel_TS2345. Cause: tttt's is_exhaustive_switch() typed the switch expression with its `!` (and `satisfies`) stripped, `K | undefined`, which no case list covers; tsc's computeExhaustiveSwitchStatement checks node.Expression() as written, the non-null K. Repro ttttExhaustiveSwitchNonNull. [flip] [fixed 93c7ee0a0]
- vinext@1.3.0 (packages/vinext/tsconfig.bench.json), src/server/app-page-element-builder.ts:824, TS2345: inside `for (const mod of mods)` with `mods: readonly (TModule | null | undefined)[]`, `TModule extends Record<string, unknown> & {default?: C | null}`, `if (mod?.default) map.set(k, mod.default)` into a `Map<_, NonNullable<TModule["default"]>>`: tsgo takes the narrowed `mod.default` as assignable to `NonNullable<TModule["default"]>`, tttt reports `C` not assignable to it. Test npm_vinext_TS2345. Fixed (corF): through_non_number_index (src/ty/relate.c, the NoIndexSignatures check of the write constraint of T[K]) looked the key up with ty_get_prop, which misses properties declared by an intersection's members; now ty_property_of_type (tsc getPropertyOfType). Repro ttttWriteConstraintIntersectionProperty. [fixed 17755b64d]
## Seed 20261302

- lumiverse-backend@1.2.5 (prolix-oc/Lumiverse, tsconfig.bench.json), src/services/global-addons.service.ts:108, TS2322 (tsgo 0, tttt 1): `return finish<T>(...)` in a `void` function, where `finish<T>(...): T` has no inference candidate for T; tsgo gives the call `void` (the return type as contextual inference), tttt gives `unknown` and reports "Type 'unknown' is not assignable to type 'void'". Test npm_lumiverse_TS2322. Cause: tttt gave a function's return expressions no contextual type when the annotated (or anchored signature's) return type is `void` (function_impl_enter_return_context, the ANCHOR_RETURN anchor); tsc's getContextualTypeForReturnExpression has no such exception, so the call's return-type inference gets `void`. Repro ttttVoidReturnContextInference. [flip] [fixed f733aeca2]
- @shepai/cli@1.247.0 (shep-ai/shep, tsconfig.bench.json), tests/unit/electron/adapters/electron-desktop-notifier.test.ts:79, TS2322 (tsgo 0, tttt 1): `deps.createNotification = vi.fn(() => ({ show: vi.fn(), on: vi.fn((event: string, listener: (...args: unknown[]) => void) => {}) }))` assigned to a `(opts) => NotificationLike` whose `on?(event: 'failed', listener: (event: unknown, error: string) => void)`: tttt compares the mock's `on` parameters and fails on listener parameter `args`/`error` (unknown vs string); tsgo accepts (the vi.fn generic is inferred against the contextual type). Self-contained mock approximation, test npm__shepai_cli_TS2322. (claimed: cor10) Re-created at the recorded commit a7291505 (private seed 20269005): tttt reports it at a06a01025, the batch's build, and matches tsgo from 5f3c08628 on (bisected: a parameter at a generic rest of an instantiated signature isn't compared as a callback, the Zoo-Code flip), which landed before this record. The finder's test npm__shepai_cli_TS2322.ts was a hand-made approximation that passes on main and before 5f3c08628 alike (.noerrors baselines), so it documents nothing; not replaced by a reduction (the box was to stay quiet for perf measurements): Zoo-Code's repro covers the mechanism. [flip] [fixed 5f3c08628]
- software-mansion/react-native-reanimated docs/docs-worklets (tsconfig.bench.json), docusaurus.config.js:13, TS2578 (tsgo 28, tttt 27): `// @ts-expect-error` above `const { X } = require('./src/x.ts')` (checkJs): tsgo finds nothing to suppress ("Unused '@ts-expect-error' directive"), tttt has an error there that the directive swallows (likely a .ts-extension require diagnostic). Test npm_react-native-reanimated_TS2578 (a short stand-in; the real error text on that line was not inspected). [open]
- @agnes/host@0.0.0 (AgnesAI-Labs/agnes-harness packages/host, tsconfig.bench.json; also packages/daemon via ../host), src/computer-use/windows-driver-backend.ts:435, TS2769 (tsgo 77, tttt 83): `const M: ReadonlyMap<string, readonly string[]> = new Map([['a', ['--x']], ['b', []]] as const)`: tsgo accepts the `as const` tuple of readonly tuples for `new Map(iterable)`, tttt reports "No overload matches this call". 4-line test npm__agnes_host_TS2769. The TS7006 extras in host (6 in all) are not looked at. Cause: the candidates for V were the inner array literals `readonly ["--new-window"]` and `readonly []`, which tsc unions (unionObjectAndArrayLiteralCandidates); tttt registered nested array literals only for an argument that is an array literal itself (not through `as const`) and never the empty tuple (its own target, ty_is_tuple false), so the common supertype picked one. call.c infer_from_arg()/add_nested_array_literals(). Repro ttttConstArrayLiteralCandidates. [fixed 4d209f060]


## Seed 20261304
- lightdash@3.52.0 (lightdash/lightdash, packages/common/tsconfig.bench.json) src/utils/item.ts:119 TS2367 (test/cases/npm_lightdash_TS2367.ts): `type === DimensionType.BOOLEAN || type === TableCalculationType.BOOLEAN` on `DimensionType | MetricType | TableCalculationType` (MetricType an empty enum); tttt reports no overlap between MetricType and TableCalculationType after the first narrowing, tsgo is clean (tsgo 0 errors, tttt 2). [flip] Fixed (corF): is_simple_related_enum (src/ty/relate.c) made members of different enums with the same value comparable, which tsc does not (isEnumTypeRelatedTo only); now only when exactly one side is an enum literal. Repro ttttEnumMembersOfOtherEnumNotComparable. [fixed b15969459]
- @pentest-harness/cordis@4.0.1 (S1N6H/pentest-harness, packages/{llm/llm,client/ui-primitives,sandbox/sandbox-windows-acl}/tsconfig.bench.json) vendor/cordis/src/context.ts:139 TS2313+TS2536, registry.ts:22 TS2456 (test/cases/npm__pentest-harness_cordis_TS2313.ts): `InjectKey = keyof {[K in keyof Context & string as Context[K] extends {[sym]: any} ? K : never]: any}` used as the constraint of a method type parameter inside Context itself; tttt reports the circularity, tsgo resolves it. Not reproducible via reduce.py's copy (order-dependent); hand-cut. (claimed: cor10) Cause: resolving `Context[K] extends { [config]: any } ? K : never` for K = "intercept" asks whether the conditional is definitely false, and tttt's walk for the type variables to map to the wildcard (tsc's permissive instantiation) went into the overloaded method's signatures, lowering `K extends InjectKey` while InjectKey was being resolved; the free-parameter shortcut skipped overloaded method types (anonymous.function is null there) and a signature whose implementation has an unannotated body. tsgo's permissive instantiation is lazy. Now an overloaded method type's free parameters are its overloads' (every type parameter in scope for one whose implementation's body may mention any), without resolving a signature (overloads_free_params in ty/conditional.c); the finder's test passes. Order-dependent in the project; a single file with the alias first and the class with an implementation reproduces it too. [fixed 3ae8c8783]
- @akashnetwork/console-api@3.32.0 (akash-network/console, apps/api/tsconfig.bench.json; deploy-web likely the same family, 318 extra) src/core/repositories/base.repository.ts:80 TS2345 (+TS2322/TS2352/TS2877) (test/cases/npm_akashnetwork_console-api_TS2345.ts, shaped after drizzle-orm's PgTableWithColumns<any>): `Partial<Output>` (Output extends an empty interface) passed to `Partial<T["$inferSelect"]>` with T extends PgTableWithColumns<any>; tttt rejects via keyof, tsgo accepts. (claimed: cor10) Re-created at the recorded commit aa01c146 (private seed 20269006): the batch's build 273d8ff92 reports the 9 diagnostics at base.repository.ts, main doesn't; bisected to 17755b64d (writing T[K] through an intersection constraint finds the property any member declares, vinext's TS2345). The finder's test npm_akashnetwork_console-api_TS2345.ts reproduces it on 273d8ff92 and passes since 17755b64d: kept as the regression test. Left in apps/api: a spurious TS2877 at tsup.config.ts:1 (a non-relative `.ts` import), like latitude-llm's in npmsample_box3.md. [fixed 17755b64d] (the TS2877 remainder: fixed by ee1a75319, the latitude-llm case)
- @akashnetwork/console-web@3.31.0 (akash-network/console, apps/deploy-web/tsconfig.bench.json) src/services/app-di-container/app-di-container.ts:2 TS7022 + TS7006 (test/cases/npm_akashnetwork_console-web_TS7022.ts; 318 extra errors in the project, cascade of this container pattern): `const container = createContainer({ a: () => ... container.b(... config => ...) ...})` with `DIContainer<T> = {[K in keyof T]: ReturnType<T[K]>}`; tttt reports the self-reference circular and leaves `config` implicit any, tsgo infers it. The test keeps the reduction's unresolved names (noisy). (claimed: cor10) Cause (tsgotrace/reentrytrace): tsgo checks a call's callee behind the barrier of its first resolution (getResolvedSignature moves resolutionStart, resolveCallExpression then checks the expression), so each `container.x(...)` in a factory re-enters `container` (twice: the second factory's reference comes inside the first re-entry). tttt checked the callee before resolve_call()'s barrier, and the second reference found a cycle. check_call() now checks the callee of a first resolution behind a barrier (expr.c); the noisy test was replaced by a clean cut-down that fails on main. [fixed 2b43a7cd4]
- @agnes/host@0.0.0 (AgnesAI-Labs/agnes-harness packages/host, tsconfig.bench.json; also packages/daemon via ../host), src/computer-use/windows-driver-backend.ts:435, TS2769 (tsgo 77, tttt 83): `const M: ReadonlyMap<string, readonly string[]> = new Map([['a', ['--x']], ['b', []]] as const)`: tsgo accepts the `as const` tuple of readonly tuples for `new Map(iterable)`, tttt reports "No overload matches this call". 4-line test npm__agnes_host_TS2769. The TS7006 extras in host (6 in all) are not looked at. [open]

## Seed 20261303 (large)
- toeverything/blocksuite packages/affine (tsconfig.bench.json), packages/affine/fragments/outline/src/outline-viewer.ts:312: tttt reports TS6133 for a `private accessor _x` that is only written (`this._x = false`); tsgo counts the write as a use, no error. Repro `npm_blocksuite_TS6133`. Cause: tsc's markPropertyAsReferenced skips a write-only access unless the symbol has SetAccessor, and the binder gives `accessor x` SymbolFlags.Accessor; tttt's mark_member_referenced() only exempted get/set accessors (PROP_ACCESSOR). An auto accessor (class_auto_accessor) is exempt now. [fixed 9f267b50c]
- artsy/metaphysics . (tsconfig.bench.json; strict, noImplicitAny false), src/schema/v2/itinerary/itineraryStop.ts:85 (TS2322), previewSavedSearch/suggestedFilters.ts:47 (TS2551), :51 (TS2345): `export const ShowType = new GraphQLObjectType<any, Ctx>({ fields: () => ({ self: { type: ShowType } }) })` is `any` in tsgo (the initializer re-enters its own type through the fields thunk) but `GraphQLObjectType<any, Ctx>` in tttt, so uses like `ShowType.name` are typed in tttt and tsgo has none of the follow-on errors. Repro `npm_artsy_metaphysics_TS2741` (TS2741 on a probe assignment). The re-entry model's stage C (agents/notes/reentrancy.md "Outstanding stages"): with noImplicitAny on, tsgo reports TS7022 on ShowType and TS7023 on `fields`; scripts/reentrytrace.py shows both sides re-enter ShowType once below the `new Obj(...)` call's barrier and resolve the call nested (NESTEDCALL 1, REENTER 0 call 1), but tsgo keeps the nested resolution's result (resolvingSignature: any) where tttt drops it and keeps the outer Obj<any, Ctx>; TTTT_DEBUG_CYCLE shows the cycle (binding -> expression -> binding). Same mechanism as the bun TS7022 list there and crowd-dev's connectors. (fixE, 2026-10-25) [design]

## Seed 20261305 (large)
- @react-doctor/evals (millionco/react-doctor packages/evals, tsconfig.bench.json), tests/vercel-sandbox.test.ts:18/19, TS2322 (tsgo 0, tttt 2): `fn(async () => ({stdout: async () => "output"}))` with `Awaited<ReturnType<T>>` as the parameter of a later method: tttt keeps the literal `"output"` where tsgo widens the inferred return type, so `mockResolvedValue({stdout: async () => ""})` fails. Repro test/cases/npm__react-doctor_evals_TS2322.ts. Cause: the inner `async () => "output"` has no contextual signature while the call resolves (its context is a property of `any`), so its return type is computed later, when its context is its own signature (T inferred as the outer arrow); tsc's getReturnTypeFromBody then takes the unwidened return type as the context and, for an async function, getPromisedTypeOfPromise of it (nothing for a literal), so it widens. tttt's infer_return_type() (src/ty/function.c) used the literal itself as the context (CONTEXT_OWN) without the promise step. [flip] [fixed COMMIT]
- Unreduced (reduce.py stopped at the budget wind-down; clones deleted, reclone at the commit): SII-Holos/synergy @ a283cf3 (https://github.com/SII-Holos/synergy; bun install). apps/web (tsconfig.bench.json): src/components/provider/ProviderConnectionFlow.tsx:503 TS2322 `Type '(k2: Part<never, never>, k3: any, ...) => void' is not assignable...` from `onChange={setFormStore.bind(null, "value")}` on a TextField (tsgo 0, tttt 1). packages/harness and packages/local-runtime (tsconfig.bench.json; tsgo 0, tttt 28 each): first ../util/src/features.ts:43 TS7022 'FeaturePlan' circular initializer, then TS2456/2502/2538/7006/7024/7031/2339/2345/2322 cascades. [flip] [open]
- Doezer/Questarr @ de563f0 (https://github.com/Doezer/Questarr; root, tsconfig.bench.json): client/src/components/CompactGameCard.tsx:361 TS7006 `Parameter 'e' implicitly has an 'any' type` for `onClick={(e) => ...}` on a wouter `<Link>` (tsgo 0, tttt 2): contextual typing of the JSX attribute is lost. [flip] [open]
- freelensapp/freelens @ 088d085 packages/core (tsconfig.bench.json; tsgo 13, tttt 25): src/main/get-metrics.injectable.ts:76 TS18046, plus +TS2339 +TS2783, tsgo-only TS2345 (3 missing). Unreduced. [open]
- ChatbotXIO/ChatbotX @ a4ab684 apps/builder (tsgo 4, tttt 5): src/features/ads/components/conversion-events-view.tsx:255 TS7006 `template`; tsgo-only TS5115 (infinitely circular instantiations). Unreduced. [open]
