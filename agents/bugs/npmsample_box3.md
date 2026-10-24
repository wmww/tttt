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
  index-signature lookup. Repro test/cases/npm_amcharts_amcharts5_TS2339.ts (by hand, 18 lines). [open]
- Large seed 20261400, sixb-ai/sixb@main packages/core: missing TS2339 `item.account` on DatasetWriteValue. A type
  parameter inferred from `readValues: async () => (async function* () { try { yield {...} } finally {} })()`
  (Promise<Iterable<T> | AsyncIterable<T>>) next to a context-sensitive method that uses T. tsgo gets no inference
  from the generator and falls back to the constraint; tttt infers the yielded object. Repro
  test/cases/npm_sixb-ai_sixb_TS2339.ts (reduce.py, then by hand). [open]
- Seed 20261211, pixpilot/credit-calculator (@pixpilot/cost-calculator@0.1.0): spurious TS6053 "File
  '@pixpilot/dev-config/typescript' not found" for a tsconfig `extends` of a package subpath whose `exports` lists
  `"./*"` (a pattern) before the exact `"./typescript"`. Exact keys win over patterns whatever their order (tsgo
  resolves it), and tttt apparently takes the pattern first. The lost base options then cost tsgo's TS2883 and add a
  zod TS5115 on the project. Repro test/cases/npm__pixpilot_cost-calculator_TS6053.ts. [open]
- Seed 20261212, journeyWorker/lute packages/player (@lute-lang/player@0.48.1): tttt aborts (exit 134) at
  ty/lower.c alias_instance(), `check(count <= c_arraylen(stack_key))`: the alias-instance intern key is a fixed
  32-word stack array (2 + the argument count), and xstate 6's `Next_MachineConfig` has 36 type parameters. tsgo
  reports 2 errors there. Fix: a heap (or arena) key when the arguments outrun the array, as other intern keys do.
  Repro test/cases/npm__lute-lang_player_crash.ts (a 32-parameter alias). [flip] [open]
