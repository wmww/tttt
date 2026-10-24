# Random npm projects found on the remote box "box3" (seeds 20261200-20261299)
Same conventions as agents/bugs/npmsample.md (one bullet per repro, root-cause guess, `[flip]` before the status tag
for flips); a separate file so the three boxes never conflict.

- Seed 20261200, auguwu/utils (@augu/utils@1.5.6, the repo's package.json is named `@noelware/utils`): spurious
  TS2307 on `import ... from '@noelware/utils'` in the package's own scripts. The package imports its own name, its
  `exports` target (dist/) isn't built, and a published copy of the same package sits in node_modules. tsgo's
  loadModuleFromSelfNameReference result is continueSearching when the export target doesn't resolve, so the
  lookup goes on into node_modules; tttt's load_from_self_name gets SEARCH_STOP from load_from_exports (which
  turns an unresolved target into a stop) and resolve_non_relative returns it. Repro
  test/cases/npm__augu_utils_TS2307.ts. Fixed (det3): load_from_self_name() now turns load_from_exports()' SEARCH_STOP into SEARCH_CONTINUE, as tsc's loadModuleFromSelfNameReference returns loadModuleFromExports' continueSearching (the node_modules walk already did). [fixed det3]
- Seed 20261201, fireproof-storage/fireproof (@fireproof/core-types-protocols-cloud@0.24.19): spurious TS18048
  "'error' is possibly 'undefined'" after `const { success, data, error } = S.safeParse(x); if (!success || !data)
  throw ... error.message` (zod 4's SafeParseResult: `{success: true, data: T, error?: never} | {success: false,
  data?: never, error}`). Either test alone agrees with tsgo. With the `||`, the false-then-true path (success
  true, then `!data` with data never falsy there) should leave nothing, so error is the error member's. Guess:
  narrowing a destructured sibling by two successive discriminant references (the binding-element parent narrowing,
  getCandidateDiscriminantPropertyAccess) doesn't compose across `||` in tttt. Repro
  test/cases/npm__fireproof_core-types-protocols-cloud_TS18048.ts. (claimed: fixE) Cause: tttt asked whether `data` is a discriminant of the computed type (the success member alone after `!success`), tsc of the declared union while the computed type is a subset of it (getDiscriminantPropertyAccess); the pattern and parameter pseudo-references missed that. [fixed ada65342c]
- Seed 20261201, abrevior/apollo-federation-file-upload (@abrevior/apollo-federation-upload@4.2.0): spurious TS7016
  for `import ... from 'graphql-upload/GraphQLUpload.mjs'` (a JS-only package reached through `exports`), plus tsgo's
  20 errors in the package's own .mjs files (TS2554, TS2694) missing. allowJs + maxNodeModuleJsDepth 10 and a `paths`
  `"*": ["./*"]` (the bench config's rewrite of baseUrl "."): tsgo loads the .mjs into the program, and tttt treats it
  as an untyped JS module. Without the paths tttt agrees, so the guess is that a node_modules result reached after a
  failed paths attempt loses its external-library/JS-depth handling. Repro
  test/cases/npm__abrevior_apollo-federation-upload_TS7016.ts (the missing errors in the package's JS should follow
  from the same cause; not reproduced separately). Fixed: not paths. tttt ignored every number in a tsconfig's compilerOptions (no JSON_NUMBER case in tsconfig.c), so maxNodeModuleJsDepth stayed 0 from a config; the harness reads it from a header and was fine. [fixed corF 2026-10-24]
- Seed 20261202, ragokan/bunicorn (@bunicorn/server@0.0.30): spurious TS6196 "'T' is declared but never used" for
  `serve<T>(options: Omit<ServeOptions & Serve<T>, "fetch">)`, where `Serve` is a namespace (TS2709). tsgo's
  checkTypeReferenceNode still checks the type arguments of a reference that fails to resolve, which marks T
  referenced. tttt skips them. Repro test/cases/npm__bunicorn_server_TS6196.ts. Fixed (det3): several_types.c's TS2709 return now looks up the type arguments first. [fixed det3]
- Seed 20261201, octanejs/octane packages/sanity-icons (@octanejs/sanity-icons@0.1.16): spurious TS7022 on `const
  root = candidate.getRootNode()` inside a do-while loop that reassigns the declared `let candidate: Element |
  undefined` from root (`candidate = root.nodeType === 11 ? (root as ShadowRoot).host : undefined`). It is the
  project's only error, so a flip. An earlier `contains(candidate)` read in the loop body is needed. Guess: the
  loop's flow type of `candidate` at that earlier reference evaluates the back-edge assignment, which needs root's
  type while root is being resolved. tsgo doesn't hit the circularity there (the declared type stands in for the
  assignment, or the reference's flow is cached differently). Reduced by hand from the reduce.py copy: --tsgo-clean
  kept all 149 package files whole, since deleting one breaks tsgo's imports. Repro
  test/cases/npm__octanejs_sanity-icons_TS7022.ts. Cause: tttt checked a do-while's condition before its body (the
  parser makes it a for loop); tsc's checkDoStatement checks the body first, so `contains(candidate)` puts the loop in
  flight and root's initializer (getQuickTypeOfExpression, outside checkExpressionCached) gets its partial type. Repro
  of the rule: ttttDoWhileBodyBeforeCondition. [flip] [fixed 7a62acf9f]
- Seed 20261205, akveo/nebular (@nbholleweger/eva-icons@0.0.5): an Angular schematics template checked as TypeScript,
  `export class <%= classify(className) %>Component {`. tsgo parses the `<` after `class` as the type parameter
  list of a nameless class (TS1139 at the `%`, then TS1005 '>' expected). tttt reports TS1005 '{' expected and TS1110
  and treats `classify` as a name. A parser recovery difference: a class heritage/body is expected right after
  `class` when no name follows, where tsgo allows type parameters. Also -TS1005 ',' expected in the .spec.ts
  template, probably the same recovery in an import list. Repro test/cases/npm__nbholleweger_eva-icons_TS1110.ts. Fixed: parse_generic_decl backed out of a class's `<` when no type parameter came first, so the class went on without one. tsc's parseTypeParameters commits: a token that can't start a type parameter is TS1139, skipped unless an enclosing list takes it, and `(`, `{`, extends or implements end the list (test ttttClassTypeParamListRecovery). The .spec.ts template's -TS1005 ',' isn't in the repro and is still unchecked. [fixed ebe1c162c]
- Seed 20261205, yuschick/stylelint-plugin-defensive-css (stylelint-plugin-defensive-css@2.9.7): both sides report one
  TS5115 ('Container', 'Diff', postcss-selector-parser 7), tsgo at src/rules/require-pure-selectors/utils.ts:107 and
  tttt at :24. This is the placement mechanism of rw_vue_TS7006 in agents/bugs/npmsample.md: tsgo's depth-100
  chain against tttt's members_reentered_unguarded. No new repro. [design]
- Seed 20261205, whats-good/graphql-ts (@whatsgood/graphql-ts@0.1.2): TS5115 placement. tsgo reports three, at
  InputFieldsMap.ts:19 ('InternalType', 'Array', 'ExternalTypeOf'), InterfaceType.ts:70 and OutputField.ts:63.
  tttt reports them at packages/core/src/types/core.ts:70/77/82 ('Array', 'ExternalTypeOf'). It's the same
  instantiation-depth placement class as above (tsgo's depth-100 point against tttt's re-entry probe), and its
  places also depend on tsgo's checker count (tsgo_divergences.md). No repro yet. [design]
- Seed 20261206, hikerpig/gatsby-project-kb (@gatsby-project-kb/transformer-wiki-references@0.3.1): spurious TS2339
  "Property 'addEventListener' does not exist on type 'never'". `const isClient = typeof window === 'object'; if
  (isClient) return; window.addEventListener(...)` narrows `window` (a `declare var`) through the aliased condition.
  tsgo's aliased-condition narrowing applies only to a constant reference (isConstantReference: a const variable,
  a readonly property, an unassigned parameter), and a var isn't one. tttt narrows the var too. Repro
  test/cases/npm__gatsby-project-kb_transformer-wiki-references_TS2339.ts (also a plain `declare var w`). Cause: tttt's constant_reference_type took any unassigned symbol as constant; tsc's isConstantReference takes a const, or an unassigned parameter, catch variable or local (non-exported, non-global) `let`, or a named function expression's name. Repro of the rule: ttttAliasedConditionConstantReference. [fixed 2d7dcb490]
- Seed 20261207, VKCOM/vk-mini-apps-router (@vkontakte/vk-mini-apps-router@1.8.6): missing TS2322 "Type 'Params' is
  not assignable to type 'Params<T>'" (@remix-run/router's `Params<Key extends string = string> = { readonly [key in
  Key]: string | undefined }`) on `return cond ? previousRecord.params : undefined` in a `<T extends string>`
  function returning `Params<T> | undefined`. tsgo accepts the same code put in one file, so the layout matters:
  Params comes through the package's `export type { Params } from "./utils"` and the record interface through
  `export *`. Guess: the alias's variance for Key (a mapped type's key) is measured in a different state or order.
  Repro test/cases/npm__vkontakte_vk-mini-apps-router_TS2322.ts. Cause (det3): no variance at all. An import's bare
  reference to a generic alias has alias type arguments `[]` (getTypeFromTypeAliasReference), and tsgo relates two
  instantiations of one alias by its variances only when `len(source.alias.typeArguments) != 0`, so `Params` (imported,
  bare) to `Params<T>` goes structurally and fails against the generic mapped target; in one file `Params` has `[string]`
  and the variances accept it. alias_variance_applies() had TS 6's rule (`[]` truthy, 13d21d1b7). Also fixes coop
  server's missing TS2416 (PluginSignalAdapter.ts:112). [fixed a236facd4]
- Seed 20261208, taiyakihitotsu/foxp (@taiyakihitotsu/foxp@0.5.11): tsgo 0 errors, tttt 97 (TS2344, TS2345, TS7006).
  foxp is a type-level Lisp over @taiyakihitotsu/cion (a type-level evaluator built on template-literal inference and
  Peano/bit arithmetic). tttt takes `_MergePreStr`'s `countArgs<X> & countArgs<Y> extends never ? false` branch, so
  `countArgs` (cion.Lisp of `(count (split ...))` over regex-found argument lists) evaluates to never or another
  number where tsgo gets 0-4. Then the `{ assoc: false }` premap fails `Record<string, string>` (TS2344), and
  resolveLambdaPre<S> is `string | false`. Guess: a template-literal inference or string-recursion step in cion's
  evaluator, or an instantiation depth limit, that differs. Repro test/cases/npm__taiyakihitotsu_foxp_TS2344.ts
  (532 lines: reduce.py with --tsgo-clean, own files then the package's). [flip] [fixed 939dbc038] Fixed (corB): conditional.c branch_type_with() composed the branch mapper before the `infer` mapper, so an inner resolution's own inferred `restTape` replaced the outer one held in cion's _TapeEval Stack argument; tsc's combinedMapper is combineTypeMappers(context.mapper, mapper). Now ty_compose_mappers(infer, branch->mapper); repro ttttConditionalOwnInferInBranchArgs.
  Same rank key (TS7006 "Parameter implicitly has an any type"), seed 20261302: 0019-firecmsco-firecms#packages/firecms_core (tsgo 85, tttt 87), src/form/PropertyFieldBinding.tsx(102) `fieldProps`; not reduced (rank said repro). Possibly a different cause: check first when picked up. [open]
- Seed 20261209, rs-pro/rocket-slate (@rocket-slate/mentions@0.2.26): missing TS2615 "Type of property
  'defaultProps' circularly references itself in mapped type 'ForwardRefExoticBase<...>'" on `styled(Toolbar)` with
  @types/styled-components (ts3.7) and @types/react 16. tsgo reports it twice at the same span, for the two
  `theme` forms. tttt has the check (members.c, the expanded_from containsError path) but doesn't reach the
  circularity here. Repro test/cases/npm__rocket-slate_mentions_TS2615.ts (reduce.py, 8 files).
  corF (2026-10-24, tsgotrace.py 2615 --suite): tsgo's cycle runs through
  - `styled(Toolbar)`'s return type, StyledComponentInnerComponent<typeof Toolbar>, whose conditional type infers from
    `StyledComponent<"div", ...>` to `StyledComponent<infer I, ...> | StyledComponent<infer I, ...>`;
  - inferFromMatchingTypes' identity check of each pair: the references' variances are unreliable, so tsc falls back
    to structure, propertiesIdenticalTo, compareSignaturesIdentical of `withComponent`, and then the type of
    ForwardRefExoticBase's mapped `defaultProps`;
  - that type instantiates a mapped type, and getReducedType -> somePropertyReducesToNever -> getPropertiesOfType ->
    getReducedApparentType asks the base constraint of a distributive conditional, StyledComponentInnerComponent
    again, whose inference does the same identity check and reaches the same `defaultProps` in flight: TS2615
    (twice, once per `theme` form).
  tttt makes the same identity check and falls back to structure too (unreliable variances, `objects_identical`),
  and never asks for `defaultProps` while it is in flight. The likely reason, not confirmed by instrumentation, is
  that its mapped-type instantiation doesn't walk the reduced type's properties eagerly (no somePropertyReducesToNever
  over the instantiated members).
  It is a resolution-order circularity of the kind of tanstack-ai's TS2615 (sourcesweep_checker.md, [tsgo]): what
  tsgo resolves eagerly and in which order decides it, and a lazy evaluation terminates. Matching it means emulating
  getReducedType's eager property walk on mapped instantiations in tsc's order. Not done. [design]
- Seed 20261209, tseijp/voxelizer (voxel-three@0.3.0): spurious TS2589 "excessively deep" in glre's TypeScript
  sources (`new Proxy(...) as unknown as X<T>` over glre's recursive node types), and a TS2590 "union type too
  complex" in voxel-three. tsgo hits neither limit. Not a placement difference: tsgo reports no depth error at all,
  so tttt's instantiation goes deeper or caches less. Repro test/cases/npm_voxel-three_TS2589.ts (reduce.py, 6
  files; the TS2590 is not reproduced separately). Cause (det3): no reverse mapped property cache. Struct<T>'s homomorphic mapped type, inferred from X<T> (XImpl: `_X<T>` and ~760 swizzle properties of a few types), made ~25 reverse mapped candidates whose comparison (getCommonSupertype) inferred each property's type anew: 5M instantiations in one expression, the count limit. tsc's inferReverseMappedType caches by (source, mapped type, constraint); infer.c ty_reverse_mapped_prop_type() now does too. The TS2590 in voxel-three couldn't be checked (the clone is gone, and a new setup needs 25 GB free); it is probably the same cause. [fixed 9b29c2b00]
- Large seed 20261400, amcharts/amcharts5@master (root tsconfig.json): +35 errors (TS2339 on never, TS2322,
  TS2345) from `$object.entries(o): Array<[Keyof<O>, O[Keyof<O>]]>` with `Keyof<T> = Extract<keyof T, string>` over
  a number-indexed `{ [id: number]: IParent }`. tsgo types the element `[never, IParent]` (the indexed access through
  the conditional index keeps the index signature's type) and tttt `[never, never]`. Guess: instantiating
  `O[Extract<keyof O, string>]` reduces the index to never first and then indexes, where tsgo keeps the
  index-signature lookup. Repro test/cases/npm_amcharts_amcharts5_TS2339.ts (by hand, 18 lines). (claimed: fixF) Cause: never_index_read()'s
  HACK made any non-mapped anonymous object indexed by never `never`; tsc's getPropertyTypeForIndexType reads the index
  signature never is assignable to. Now a type literal as written (no instantiation) goes to ty_access(); instantiated
  ones keep the hack (xstate's TS5115, hacks.md). Repro ttttNeverIndexTypeLiteral. [fixed 95ffcd29e]
- Large seed 20261400, sixb-ai/sixb@main packages/core: missing TS2339 `item.account` on DatasetWriteValue. A type
  parameter inferred from `readValues: async () => (async function* () { try { yield {...} } finally {} })()`
  (Promise<Iterable<T> | AsyncIterable<T>>) next to a context-sensitive method that uses T. tsgo gets no inference
  from the generator and falls back to the constraint; tttt infers the yielded object. Repro
  test/cases/npm_sixb-ai_sixb_TS2339.ts (reduce.py, then by hand). (claimed: cor10) Cause (instrumented tsgo): tsgo 7.1's inferFromTypes takes two references to one generic type through invokeOnce, stack and all, so `Promise<IteratorResult<X>>` inside `Promise<AsyncGenerator<X>>` against `Promise<AsyncIterable<T>>` is two Promises deep on both sides: a circularity, no inference. tttt went by type argument off the stack (TS 6's rule); now through the same once-per-pair walk (infer_from in ty/infer.c). The test's errors and symbols pass; its types still differ on the generator's TNext (`any` where tsgo has `unknown`: the IIFE's contextual type is an async arrow's `X | PromiseLike<X>`, whose PromiseLike member gives tsgo no contextual next type), filed in triage.md. [fixed 4998d6376]
- Seed 20261211, pixpilot/credit-calculator (@pixpilot/cost-calculator@0.1.0): spurious TS6053 "File
  '@pixpilot/dev-config/typescript' not found" for a tsconfig `extends` of a package subpath whose `exports` lists
  `"./*"` (a pattern) before the exact `"./typescript"`. Exact keys win over patterns whatever their order (tsgo
  resolves it), and tttt apparently takes the pattern first. The lost base options then cost tsgo's TS2883 and add a
  zod TS5115 on the project. Repro test/cases/npm__pixpilot_cost-calculator_TS6053.ts. (claimed: fixF) Cause: tsconfig.c
  resolve_extends_exports() took the first matching key in order; now an exact key first, else the pattern with the
  longest prefix (tsc's comparePatternKeys). Repro ttttExtendsExportsExactBeforePattern. [fixed eea01cfd2]
- Seed 20261212, journeyWorker/lute packages/player (@lute-lang/player@0.48.1): tttt aborts (exit 134) at
  ty/lower.c alias_instance(), `check(count <= c_arraylen(stack_key))`: the alias-instance intern key is a fixed
  32-word stack array (2 + the argument count), and xstate 6's `Next_MachineConfig` has 36 type parameters. tsgo
  reports 2 errors there. Fix: a heap (or arena) key when the arguments outrun the array, as other intern keys do.
  Repro test/cases/npm__lute-lang_player_crash.ts (a 32-parameter alias). Fixed (fixF): heap keys past the stack arrays in
  alias_instance, ty_alias_body_mapped_instance and ty_realias (the last two silently skipped realiasing past 36 arguments), and
  the 32-entry caps on type parameters (MergedTypeParams, now 63: the arity memo's 6 bits), lowered type arguments
  (lower_type_args/ty_alias_args_filled: arena past MAX_TYPE_ARGS) and explicit call/JSX/instantiation-expression type arguments
  (truncated before). Repro of the rest: ttttManyTypeArguments (40 parameters). [crash] [fixed e0ed0046b]
- Seed 20261212, cfdude/mcp-namecheap (mcp-namecheap@1.0.0): spurious TS2883 "The inferred type of '(Missing)'
  cannot be named without a reference to 'Server'..." in a `// @ts-nocheck` file under declaration: true. tsgo
  reports no diagnostics for a nocheck file, declaration diagnostics included, and tttt's declaration walk reports
  them anyway. A second oddity: the anonymous `export default function` is named '(Missing)'. Repro
  test/cases/npm_mcp-namecheap_TS2883.ts (reduce.py, then by hand). (claimed: fixF) Cause: not the nocheck (tsgo reports
  nothing without it either): the function has no name and tsgo's FunctionDeclaration pushes no error fallback node, so
  the symbol tracker's errorLocation is nil and ReportLikelyUnsafeImportRequiredError reports nothing; the type is
  emitted as `any` (encounteredError). tttt's error_name_node() fell back to the declaration; now no location there
  outside classes and export assignments, and the printed type is `any`. Repro
  ttttDeclarationAnonymousDefaultUnsafeImport. [flip] [fixed 2d30c3eeb]
- Large seed 20261400, ChatbotXIO/ChatbotX@main apps/builder: two spurious TS7006 "Parameter 'template' implicitly
  has an 'any' type" on `fkMatcher: (template) => ...` in the second branch of a conditional initializer of a
  destructuring annotated `{ fkMatcher: (template: TemplateCandidate) => boolean }`, inside a useMemo callback.
  (tsgo's other difference there, a TS5115 in zod 4's IsOptionalIn, is the paseo [design] family; reduce.py deleted
  that file and the TS7006 stayed, so the two are independent.) The own file reduced to 19 lines, with no imports
  (it becomes a global script, so names may bind to globals of the program's packages: React 19.2 / @types/react
  19.2.14 among 16k program files). Standalone, with local declarations, it agrees with tsgo. The package pass over
  53k files didn't finish in 50 minutes, so there's no test. The own file, for a fixer rebuilding the dependency
  set from the clone's lockfile:
  `function RuleBuilder({}: RuleBuilderProps) { const templateOptions = useMemo(() => { const { list }: {
  fkMatcher: (template: TemplateCandidate) => boolean } = channel === "messenger" ? {} : { fkMatcher: (template) =>
  template.integrationId === selectedIntegrationId }; return list }, []) }`. [open]
- Large seed 20261401, rocicorp/mono@main packages/zero-cache: tsgo 0 errors, tttt 42 (TS2339, TS2322, TS2345,
  TS2349, TS2532, TS2722). An array destructuring of a tuple union, `const [tag] = change; switch (tag) { case
  'begin': change[2]... }`, narrows `change` by the destructured tuple discriminant in tsgo (the binding-element
  parent narrowing, for array binding patterns too), and tttt narrows only `tag`. Repro
  test/cases/npm_rocicorp_mono_TS2339.ts (by hand, 18 lines). [flip] Fixed (corF): destructured_alias_name (src/ty/flow.c, tsc getCandidateDiscriminantPropertyAccess for a destructured const) accepted only object patterns; now any top-level pattern, the element name from pattern_element_name (array index, numeric object keys too). Repro ttttArrayDestructuredDiscriminant. [fixed 54c72943d]
- Large seed 20261401, matrix-org/matrix-js-sdk@develop: spurious TS2322 "Type 'unknown' is not assignable to type
  'SecretsBundleJson'". The wasm package's `class SecretsBundle { to_json(): unknown }` gets an overload
  `to_json(): Promise<SecretsBundleJson>` from the project's module augmentation (`declare module ... { interface
  SecretsBundle { ... } }`). tsgo resolves the call to the augmentation's signature (the later declaration's
  overloads first), and tttt takes the class's. Repro test/cases/npm_matrix-org_matrix-js-sdk_TS2322.ts (by
  hand). (claimed: fixF) Cause: reorder_candidates() groups signatures by (merged symbol, declaration) and tries a later
  group first, but a class's own methods had no group (only interface members did), so the class's overloads stayed
  first. Now a class merged with an interface gives its methods the group of its declaration (members.c). Repro
  ttttClassInterfaceMergedOverloadOrder. [flip] [fixed 6a3029342]
- Large seed 20261401, bytecodealliance/jco@39b2b63e packages/jco-std (tsconfig.bench.json): tsgo 0 errors, tttt 1,
  TS18046 "'stream.params' is of type 'unknown'" at src/wasi/0.2.x/node/24.x.x/zlib/host-worker.ts:88, after
  `if (!("params" in stream)) throw ...`. Cause: `stream = factories[algorithm]()` is `Deflate | BrotliCompress | ...`
  in tsgo but subtype-reduced to `BrotliCompress` in tttt: @types/node declares each zlib stream as a class merged with an
  interface, and removeSubtypes keeps two class instance types unless one derives from the other; tttt's declared type
  of a class+interface symbol is an interface, so expr.c's is_class_instance() didn't apply the rule. It now asks the
  symbol's class flag. Then `in` narrowed `BrotliCompress` to `BrotliCompress & Record<"params", unknown>`. Test
  npm_bytecodealliance_jco_TS18046 (self-contained). [flip] [fixed f1a53b501]
- Large seed 20261402, cellajs/cella@main backend and frontend: spurious TS2322/TS2345 "'{}' is not assignable to
  'string'". `Object.entries(o)` with `o: Partial<Record<keyof T, string>>` (T a type parameter) infers U for
  `{ [s: string]: U }` as `Record<keyof T, string>[string] | undefined` in tsgo (an index signature read off the
  generic mapped type) and as `unknown` in tttt. Repro test/cases/npm_cellajs_cella_TS2322.ts (by hand). [flip]
  (claimed: fixF) Cause: expand_generic_intersection_homomorphic() (members.c) took a generic modifiers type's index
  signatures only from a homomorphic mapped part; Record<keyof T, string> isn't one. Now a lone mapped modifiers type's
  are taken too. Repro ttttMappedOverGenericRecordIndex. [fixed b405aca7f]
- Large seed 20261402, marimo-team/marimohub@main (commit 2139c26c5), packages/core and packages/api
  (tsconfig.json; api reaches core's file): spurious TS2322 at packages/core/src/services/runtime/SandboxProvisioner.ts:852
  "Type 'Promise<unknown> | Promise<ReturnType<T[string]>>' is not assignable to type 'Promise<void>'", in `await
  all({ async reachable() {...}, async load() { ... this.$.reachable ... } })` (better-all's `all`, a `this`-typed
  task object). tsgo 0, tttt 1 each, so a flip. Not K-dependent. Repro test/cases/npm_marimo-team_marimohub_TS2322.ts (by hand from reduce.py's cut). Fixed (corF): mentions_type_variables (src/ty/conditional.c, the permissive/restrictive check's type-variable walk) asked signatures for return types still to be inferred from a body, fixing `load`'s contextual this with T unresolved; such a return type now counts as mentioning the function's outer type parameters instead. [flip] [fixed 914a16434]
- Large seed 20261402, deepagent-ltd/deepagent-code@dev (commit 87403bac1), packages/app (tsconfig.json): spurious
  TS2322 at src/utils/persist.ts:643 and :644 "Type 'T | Accessor<(readonly unknown[] & Exclude<T,
  NotWrappable>)[number]>' is not assignable to type 'T'", in the returned tuple `[state, setState, init, ...]` of a
  solid-js store helper (solid-js/store's Store/SetStoreFunction, @solid-primitives/storage makePersisted). tsgo 0,
  tttt 2, so a flip. Not K-dependent. Root cause (corB, reduce.py then by hand): PersistedState<[T, SetStoreFunction<T>]>'s first branch `S extends Signal<infer T>` must be definitely false. tsgo infers the `infer T` with NoConstraints, so Part<T>'s deferred `[T] extends [readonly unknown[]] ? (item: T[number]) => boolean : never` gives nothing and the T is void (from Setter's return). The permissive check then fails and the second branch is taken. tttt inferred through the conditional's constraint (T[number] -> wildcard), the permissive check passed, and the type stayed deferred. infer.c now skips an instantiable source's apparent type under a conditional's inference (inf->always_strict). Test test/cases/npm__deepagent-code_TS2322.ts. [flip] [fixed 8424e77d3]
- Large seed 20261402, mui/mui-x@master (commit 215513c6c), packages/x-date-pickers (tsconfig.json): spurious TS2740 at
  src/DateCalendar/PickersFadeTransitionGroup.tsx:54 "Type '{ className: string; ownerState: {...}; children: ... }'
  is missing the following properties from type ...: about, accessKey, ... and 274 more", and TS2322 at
  PickersSlideTransition.tsx:157 (props with `childFactory` to a styled TransitionGroup). tsgo 0, tttt 2, so a
  flip. Not K-dependent. (claimed: cor10) Re-created at 215513c6 (private seed 20269007; reduce.py's copy didn't
  reproduce). The props are `Pick<TransitionGroupProps<"div">, ...>`, a union whose second member is `{ component } &
  { [prop: string]: any }`: tsc's union property `about` is optional (the member with an applicable index signature
  takes part without making it required); tttt's modifiers_prop() (ty/members.c) looked for that index signature only
  on object members, not intersections, and made the attributes required. Repro
  test/cases/ttttPickUnionIntersectionIndexOptional.ts. [flip] [fixed 3606770d3]
- Large seed 20261402, latitude-dev/latitude-llm@development (commit 4914acc6c), apps/web (tsconfig.json): spurious
  TS2877 at src/domains/annotations/annotations.functions.ts:7 "This import uses a '.ts' extension to resolve to an
  input TypeScript file, but will not be rewritten during emit because it is not a relative path". tsgo 4 errors,
  tttt 5, not a flip. Same as akash console-api's tsup.config.ts:1 (npmsample.md): a workspace package's .ts through its node_modules link; repro test/cases/ttttTsExtensionImportOfLinkedPackage.ts. Fixed (corF): report_unsafe_rewrite (src/source_file.c) looked for node_modules in the imported file's real path; now the file's found_searching_node_modules flag, as emit_source_may_be_emitted and tsc's IsSourceFileFromExternalLibrary. [fixed ee1a75319]
- Large seed 20261401, Bitterbot-AI/bitterbot-desktop@main (commit 733b74195), root tsconfig.json (declaration
  emit): missing TS5088 at src/telegram/bot.ts:471 "The inferred type of 'createTelegramWebhookCallback' references
  a type with a cyclic structure which cannot be trivially serialized". tsgo 1, tttt 0, so a flip. Not
  K-dependent. (claimed: fixF) Cause: grammy's `HttpAdapter = (req: { on: (...) => typeof req }) => ...`; printed for
  `Parameters<...>` in bot.ts's declaration, `typeof req` isn't accessible (req is a parameter of a function in another
  file) and expands into the object it's in, a cycle: TS5088. ty_display.c name_accessible() only refused a parameter
  printed outside its function in the same file. Repro test/cases/npm_Bitterbot-AI_bitterbot-desktop_TS5088.ts.
  [flip] [fixed d36868a8f]
- Large seed 20261401, akan-team/akanjs@main (commit 1324d30dc), pkgs/akanjs (tsconfig.json): tsgo reports TS2322 at
  service/service.test.ts:672 and :675 ("Type '(value: string) => string' is not assignable to type '(value:
  unknown) => string | null'"). tttt doesn't, and instead reports TS2344 at constant/constant.test.ts:182 ("Type
  'false' does not satisfy the constraint 'true'", a type-level test assertion) and TS2352 at ui/Load/View.tsx:46,
  :56 ("Conversion of type 'ServerView<T, Full>[`${T}Obj`]' to type 'Full' may be a mistake"). 3 against 3, not a
  flip. No repro. (claimed: corF) [open]
- Seed 20261216, liruifengv/starlight-head (starlight-head@0.0.1): an extra TS2589 at @astrojs/starlight 0.21.3's
  schema.ts:113, where tsgo also reports a depth diagnostic (TS5114 "Instantiations of type 'ZodType' appear
  infinitely circular"), which tttt reports too. An extra limit error next to tsgo's own, in the depth-limit family
  (paseo's write-up in npmsample.md). No repro. [design]
- Seed 20261217, eds2002/react-native-screen-transitions (react-native-screen-transitions@4.0.0, commit 80e7a54d6,
  packages/react-native-screen-transitions): tsgo 0 errors, tttt 15 (TS7022, TS7024, TS7006). `export const { A,
  useOptional, ... }: ReturnType<typeof createX> = createX((props) => { const parent = useOptional(); ... })`, where
  createX is an instantiation expression `createProvider(...)<P, V>`. The binding pattern's annotation types the
  bindings, so reading one inside the initializer's callback isn't circular, and tttt treats it as circular.
  Repro test/cases/npm_react-native-screen-transitions_TS7022.ts (by hand, 11 lines). (claimed: fixF) Cause: an
  element of an annotated pattern asks whether the initializer can be undefined (tsc's getBindingElementTypeFromParentType
  via getTypeOfInitializer = getTypeOfExpression, quick type first); tttt checked the whole call, callback included.
  Now ty_quick_expr_type() first (lower.c binding_assigned_type). Repro ttttAnnotatedPatternQuickInitializer. [flip]
  [fixed 602d18e4c]
- Seed 20261217, lambda-direct/datalayer (@lambda-team/datalayer@1.0.11, commit cc98e7158, root tsconfig.json):
  missing TS2345 at src/builders/highLvlBuilders/insertRequestBuilder.ts:63 "Argument of type '{ [name in keyof
  ExtractModel<TTable>]: AbstractColumn<ColumnType<{}>, true, false>; }' is not assignable to parameter of type '{
  [x: string]: Column<ColumnType<{}>, true, false>; }'" (a generic mapped type against an index signature).
  tsgo 32, tttt 31, not a flip. Not K-dependent. (claimed: cor10) Re-created at cc98e715 (private seed 20269011),
  reduced with reduce.py. tsgo infers T of `values<T>(columns: { [name in keyof T]: Column })` from the source's keys
  `Names<TTable>` (a `{...}[keyof TTable]`): inferFromTypes restarts with its apparent type `string | number | symbol`,
  and `string` to `keyof T` gives T `{ [x: string]: {} }`, so the AbstractColumn template fails. tttt's keyof-target
  branch in infer_from() dropped a generic non-keyof source; it now restarts from the apparent type (ty/infer.c).
  Repro test/cases/npm_lambda-direct_datalayer_TS2345.ts; the project is byte-identical with tsgo. [fixed ae6edb381]
- Seed 20261217, googlechrome/lighthouse (tybrs-lighthouse@1.0.0, commit 1a4ffa418, root tsconfig.json, checkJs):
  +7 in JavaScript: TS7008 "Member '_frameNavigations' implicitly has an 'any[]' type" at
  core/gather/driver/network-monitor.js:60 and :89 (a `this._frameNavigations = []` member), TS2339 "Property
  'level' does not exist on type '{ fingerprint: any[]; tags: any; }'" at core/lib/sentry.js:131 and :141 (a
  JS object literal later given properties), and TS7022. tsgo 57, tttt 64, not a flip. Two causes, both fixed: (1) `this.x =
  []` in a method of a class whose body declares x (a `/** @type {T[]} */ x = []` field): tsc types the member by the
  field and never widens the assignment; tttt's ty_js_assignment_implicit_any() reported TS7008 there (now 0 when the
  class body has the member). Test npm_lighthouse_TS7008. (2) `sentryDelegate.captureException = async (err, opts =
  {}) => { opts.fingerprint = ...; opts.tags = ... }`: tttt made the parameter's `{}` default an expando container, so
  opts was `{ fingerprint; tags }` instead of the contextual `{ level?, tags?, extra? }` (TS2339 x3, TS7022 via `tags`);
  tsc's getInitializerSymbol takes variable declarations only (scope.c symbol_is_expando_host() skips parameters). Test
  npm_lighthouse_TS2339. Byte-identical with tsgo after both. [fixed 7636bc66e]
- Seed 20261217, izumiz-dev/react-flow-chart (@izumiz-dev/react-flow-chart@0.1.7, commit c7a97a7e0): tsgo's TS2615
  in @types/styled-components index.d.ts:196 (the defaultProps ForwardRefExoticBase circularity, like
  rocket-slate's npm__rocket-slate_mentions_TS2615) is missing, and tttt reports TS2313 "Type parameter 'P' has a
  circular constraint" at lib.es5.d.ts:1584 instead. Probably the same root as rocket-slate's entry. Not a flip.
  fixF (2026-10-25, stopped at the release wind-down, no fix): reproduced on a sandboxed clone (projdiff: tsgo 13, tttt
  12; tsgo-only TS2615 x2 at @types/styled-components index.d.ts:196 on `ForwardRefExoticBase<Omit<Omit<any, any> &
  Partial<Pick<any, any>>, "theme"> & ...>`, tttt-only TS2313 at lib.es5.d.ts:1584, Pick's `[P in K]`). reduce.py
  --keep TS2313 --tttt-has cut the project's own files to one 3-line file, `import styled from 'styled-components';
  export const PortsGroupDefault = styled.div<IPortsGroupDefaultProps>``;` with IPortsGroupDefaultProps unresolved (an
  error/any type argument: hence the `any`s in tsgo's mapped type), re-exported through two index.ts files; the
  node_modules pass (@types/react, @types/styled-components, csstype, prop-types) was stopped unfinished. The TS2313
  comes from members.c mapped_key_circular(): ty_mapped_info_instantiate() found Pick's instantiated key constraint
  in flight (RESOLVING_BASE_CONSTRAINT), i.e. tttt meets the same resolution-order cycle as tsgo but closes it at a
  different place: at the mapped key's base constraint where tsgo closes it at ForwardRefExoticBase's `defaultProps`
  (TS2615). So (a) and (b) look like one mechanism, rocket-slate's [design] entry above (corF's trace there: the
  identity check in StyledComponentInnerComponent's inference falling back to structure, getReducedType's eager
  property walk). Not confirmed with tsgotrace.py 2313/2615 on this project; next step for whoever takes it: rerun
  reduce.py (--keep TS2313 --tttt-has) including node_modules and compare tsgotrace.py 2615 --funcs
  getResolvedBaseConstraint with TTTT_DEBUG_CYCLE=1 on the cut. [design]
- Seed 20261216, sanity-io/comlink (@sanity/comlink@4.0.3, packages/comlink, tsconfig.json): tsgo 0 errors, tttt 2
  TS2589 "Type instantiation is excessively deep and possibly infinite" at src/connection.ts:583 and
  src/node.ts:628 (xstate 5 machine setups). tsgo reports no depth diagnostic anywhere on the project, so this is a
  tttt-only limit and not a placement difference. Not K-dependent. reduce.py had cut the own files to 6 when corB
  claimed it. No repro from this box. [flip] [open]
- Large seed 20261403, akash-network/console@main (commit aa01c1469), apps/deploy-web (tsconfig.json): +58 tttt
  errors (tsgo 94, tttt 152, so not a flip). Many have `unknown`/`{}` where tsgo has a type: TS7006 "Parameter
  'stripe' implicitly has an 'any' type" at src/components/billing-usage/AddCreditsWalletButton/AddCreditsWalletButton.tsx:94
  (`stripePromise.then(async stripe => ...)`), TS18046 "'_response' is of type 'unknown'" at
  src/queries/useTemplateQuery.tsx:84, TS2322 "Type '{} | null' is not assignable to type 'ProviderSummary | null'"
  at src/components/providers/ProvidersExplorer/ProvidersExplorer.tsx:82, TS2740 "Type '{}' is missing ... from type
  'Blob'" at TransactionInvoiceButton.tsx:47. It looks like one upstream inference or resolution loss (Promise
  `then` callbacks, react-query results) rather than 58 causes. Not K-dependent. No repro. Re-checked at
  7a1056d63 (after cor10's 2b43a7cd4; seed 20261490, the same commit aa01c1469 re-set up): still 94 against 152
  with the same first lines, so it isn't console-web's DI-container TS7022 cascade. fixE, 2026-10-25 (stopped for
  the release wind-down, clone deleted; re-create it from 20261403.setup.jsonl with installsandbox.py, make_bench
  changes "baseUrl -> null, paths rebased", "downlevelIteration -> null"): projdiff at main 1676eb513 still 94 / 152,
  the 58 tttt-only are 23 TS7006, 18 TS2347 ("Untyped function calls may not accept type arguments": a callee that is
  any), 4 TS7031, 3 TS2345, 3 TS18048, 2 TS18046, TS2740, TS2739, TS2352, TS2322. In file order the first are
  src/services/app-di-container/app-di-container.ts(102) and browser-di-container.ts(38): TS7006 'config' in
  `request: [config => ({ ...config, baseURL: getBaseUrl() })]` passed to `container.applyAxiosInterceptors(...)`
  inside a factory of `const container = createContainer({ ..., providerProxy: () => { ... container.X ... } })`,
  and RootContainerProvider.tsx(14) TS2345 on `Partial<Factories & { api: ... }>`. So `container` (and the
  `services` built from it with createChildContainer) loses its type in tttt while its own initializer's factories
  are inferred: a self-referential initializer through deferred factory functions, the re-entry model's area
  (agents/notes/reentrancy.md; compare cor10's 2b43a7cd4 for console-web's DI case). The stripe/react-query errors
  are downstream (services any). Next step: reduce createContainer/createChildContainer (src/services/container/
  createContainer.ts) with one self-referencing factory, then scripts/reentrytrace.py. [open]
- Large seed 20261403, akash-network/console@main, apps/api: spurious TS2877 at tsup.config.ts:1 ("This import uses
  a '.ts' extension ... not a relative path"), the same as latitude-llm's apps/web entry above. Not a flip. Re-checked at 7a1056d63 (seed 20261490, the same commit aa01c1469): byte-identical. [fixed ee1a75319]
- Seed 20261218, wakaztahir/solid-staggered-grid (solid-staggered-grid@1.0.22, commit 4bf293019, root tsconfig.json):
  missing TS2322 at lib/src/StaggeredGridItem.tsx:89 "Type '{ component: "div" | T; } & IntrinsicElements[T] & {
  children: Element; }' is not assignable to type 'IntrinsicAttributes & DynamicProps<"div" | T, ...>'" (solid-js's
  `<Dynamic component={...}>` with a generic element type T). tsgo 13, tttt 12, not a flip. Not K-dependent. No
  repro. [open]
- Large seed 20261404, cloudflare/cloudflare-os@main (commit 835de9e87), packages/workshop-backend (tsconfig.json):
  VARIES. At the defaults (both runs) tttt reports TS5114 "Instantiations of type 'MaybeCallableProvider' appear
  infinitely circular" at src/overseer.ts:4779. At --threads 1 it doesn't, and tsgo never does (tsgo 9, tttt 10
  at the defaults). packages/gatekeeper-google of the same repo has the same tttt-only TS5114 at
  src/gmail-hooks.ts:66 (tsgo 1, tttt 2), where the runs
  agree. A depth limit reached only when the checker count or file split differs: det3's area. The clone was kept
  on box3 until the box went away. No repro. [open]
- Large seed 20261404, vvedantb/eva@main (commit fd387003e), apps/web (tsconfig.json): tsgo 0 errors, tttt 215
  TS7006, starting with "Parameter 'localStore' implicitly has an 'any' type" at
  src/lib/components/ChangelogDialog.tsx:37 (`useMutation(api.changelog.dismissChangelog).withOptimisticUpdate((localStore)
  => ...)`) and NotificationToastStream.tsx:58/:64. Every callback typed through convex's react hooks loses its
  contextual type: convex's generated `api` (FilterApi/ApiFromModules over the convex/ modules) or the hooks' types
  probably resolve differently. Not K-dependent. No repro. [flip] [open]
- Large seed 20261404, CodeWithCJ/SparkyFitness@main (commit 0ec259faf), SparkyFitnessMobile, shared and
  SparkyFitnessServer: zod 4's TS5115 ('$ZodTypeInternals', '$InferZodRecordInput', 'IsOptionalIn'). tttt reports it
  at shared/src/schemas/database/FoodAssistant.zod.ts:46 on all three configs. tsgo reports it only on the server, at
  models/foodAssistantRepository.ts:109, so Mobile and shared flip (0 against 1). On the server tttt also has three
  TS2578 "Unused '@ts-expect-error'" at services/exerciseService.ts:1900-1904, which the depth limit's error type
  plausibly explains. The paseo depth-limit family ([design] in npmsample.md). No repro. [design]
