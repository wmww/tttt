# Plan 42: where tttt is eager and tsc is lazy
Planned 2026-10-05 (todo entry 7, one session with three prototype sub-agents). Prototypes in `42_prototypes/` (those left), all
applying to 02ca5d29.

## Decision: go, as four small independent stages. No shared groundwork, no big port
The stub expected one root cause ("an instantiated object type whose members are resolved per name on demand").
That isn't what the prototypes found:
- **tttt already has tsc's member laziness.** `ty_members()` resolves a type's members on first use, `prop_type()`
  instantiates/lowers each property's type on first use (tsc's getTypeOfInstantiatedSymbol), signatures and return
  types resolve lazily, mapped type properties instantiate the template per key when asked (getTypeOfMappedSymbol).
  Building the *list* of instantiated Props eagerly is what tsc does too (createInstantiatedSymbolTable).
- The eager sites are separate mechanisms, each a session or less, each 0-lost in its prototype, each removing a
  hack. Together they are worth ~+6 tests: **the value is deleting workarounds and unblocking plan 39's leftovers,
  not the pass count.** Perf is neutral (within ±1.2% callgrind per corpus).
- **Not the memory fix** (see Memory below): drizzle's 1.8 GB is four checkers each rebuilding the same types.
- **Not laziness sites** after all: index signature values (tsc's getIndexInfosOfSymbol and instantiateIndexInfo are
  eager too: the `builder_add_interface_member` and valibot perf_leads notes are about other things); contextual
  instantiation (`bugs/type_core.md` "Contextual types are instantiated eagerly") is push-down vs pull contextual
  typing plus tsc's leaf-wise instantiateContextualType, a plan 38 topic, not member laziness.
- **Plan 38's "resolvingSignature" premise was wrong** (stage 3 below): tsc doesn't return any for a call re-entered
  in flight. It resolves it again, with no barrier for the nested resolution. Drop "any on re-entry" for good.

Rule from here (vision.md's "stop patching" still holds): a fix that needs one of these sites to be lazy is done by
doing the stage, not another workaround.

## Stage 1: lazy reverse mapped types. Done 2026-10-05
Suite 12728 -> 12729 (+reverseMappedTypeRecursiveInference, +mappedTypeRecursiveInference symbols), 0 lost; every
corpus byte-identical, threadcheck/checkerdiff identical, callgrind within 0.1%. Object reverse mapped types are a
`TY_ANONYMOUS` flavour (`anonymous.is_reverse`, `ty_reverse_of()`; the pointer shares `merged_value`'s slot to keep
`Ty` at 136 bytes) whose members resolve in `resolve_reverse_mapped_members()` and whose property types are inferred in
`prop_type()`. Limited constraints (`keyof T & X`) take numeric/unique symbol keys; the reverse property's own key is
its name's string (tsc's reverse symbol has no valueDeclaration). The canonical `T[K]` -> T form instantiates the root
when other parameters are mapped too. Probes in `test/probes/plan42/`. Arrays/tuples stay eager (hacks.md).
- Follow-up (0.5 session, optional): delete the `infer_from_objects` homomorphic shortcut entirely (tsc always
  builds the reverse type): costs complicatedIndexesOfIntersectionsAreInferencable and twiceNestedKeyofIndexInference
  in the prototype; fix those first. mappedTypeInferenceErrors (`bar: unknown`) stays unexplained: needs an
  instrumented tsc run (memory note "instrument a tsc copy").

## Stage 2: lazy intersection reduction (~1 session). Prototype `stage2_reduced_intersections.patch`
Measured: suite +2 (intersectionWithConflictingPrivates, typeGuardsWithInstanceOf), 0 lost; corpora unchanged,
zod-root byte-identical; callgrind zod -0.8%, valibot +0.2%, drizzle +0.4%, tsc +0.65%, vue +1.1%. Not run:
threadcheck/checkerdiff (run them).
- Intersections stay `TY_INTERSECTION` when made. `ty_reduced()` (tsc's getReducedType): never-or-itself memoized in
  a `TyObjectState.reduced_state` byte; unions map their members (`KEY_REDUCED_UNION`), only when flagged
  `TYX_CONTAINS_INTERSECTIONS`. Inline fast path in ty.h. `never_reduced_prop()` replaces
  `has_conflicting_discriminants` and reads a property's type only when a second constituent has it.
- Asked at tsc's sites: display (unless `TyDisplay.no_reduction`), `is_related_to` both sides and `check_related`'s
  fast path (failure adds the TS18031/18032 elaboration "The intersection 'A & B' was reduced to 'never' because..."),
  `apparent_type`, `find_prop_in`, `access()`, `ty_keyof`, infer.c's object targets, conditional distribution,
  `instantiate_mapped_over_list`, property access on never (TS2339 with the elaboration), `member_refs`,
  `target_prop_of`, flow's `is_discriminant_property`, the harness's `collect_prop_symbol`, class bases
  (`ty_class_never_base()`: TS2509) and interface bases under the RESOLVING_BASES push.
- Deleted: the barrier, `members_judgeable`, `prop_judgeable`, `ty_node_reaches_alias_in_flight`, `reduced_later`.
  Three hacks.md entries go (has_conflicting_discriminants, plan 39 stage 3 item 5, the barrier).
- Kept (HACKs to record): members with `TYF_COULD_CONTAIN_TYPE_VARIABLES` still aren't judged (judging them like tsc
  cost valibot **+29%**: understand why tsc doesn't pay that before trying again); `reduction_forces_members()` gates
  the `instantiate_mapped_over_list` site (reducing there made zod's recursive getters TS2615 on `pipe`). The
  sub-agent blamed eager mapped members, but mapped props' types *are* lazy (`expand_mapped`); find what actually
  asks the in-flight getter (TTTT_DEBUG_CYCLE) before building anything for it.
- Risk to check: `check_related` reducing before its fast path makes `any` vs a never intersection false where tsc's
  isTypeRelatedTo says true; other `flags & TYF_NEVER` tests on unreduced intersections (grep them).
- circularBaseTypes' `Y` TS2310 comes out (plan 39 stage 3 item 2's Y); its `K` needs stage 3.

## Stage 3: mapped info without expansion, quick types, re-entry semantics (~1 session). Prototype `stage3_reentry.patch`
The prototype is behind environment variables (`getenv("TTTT_...")`, marked PROTOTYPE): land the default branches,
delete the switches. Measured with the default ("barrier on every resolution", as HEAD): 0 lost, +underscoreTest1,
+incorrectRecursiveMappedTypeConstraint symbols/types, recursiveMappedTypes' six TS2456/TS2313 (test still fails on
lines 21, 73, 79); tsc/zod/type-fest/vue 0, xstate 8 (compare the file:line set, not the count). Not run:
instrcount, threadcheck, zod-root.
- `ty_mapped_info_unexpanded()`: a mapped type's `MappedInfo` before expansion, used by `homomorphic_variable()` and
  `ty_keyof()` of a mapped type (tsc's getHomomorphicTypeVariable / getIndexTypeForMappedType read the declaration
  only). With it, plan 39 stage 3 item 2 lands: `declared_param_constraint()` lowers under the walk's push, and
  tsc's eager key-constraint check of getTypeFromMappedTypeNode runs at every mapped node in `lower()`;
  mappedTypeWithAny stays passing.
- Quick type for class field initializers (tsc's getQuickTypeOfExpression in checkDeclarationInitializer:
  `class_item_field_evaluate_type` lacks what variables have): implicitAnyFromCircularInference's `s = foo(this)`.
- A function argument's return type isn't asked again while in flight (tsc's ContextChecked: once per node), and a
  declaration re-entered below a barrier evaluates again, keeping the inner result (res1/res3 of
  circularReferenceInReturnType).
- Plan 38 "not done 1" and plan 39 "Stage 3" 1 get rewritten with the corrected premise (table in the session's
  commit message / this plan): tsc resolves re-entered calls again; resolvingSignature only reaches callers through
  SkipGenericFunctions (silentNeverType) and argument contextual types while in flight.

## Stage 4 (optional, 1-2 sessions): the first-resolution-only barrier and return types after resolution
- **Return type read while the call is in flight** (3 tests: simpleRecursionWithBaseCase1's fn5,
  types.asyncGenerators.es2018.2, witness): `resolved_return_type` takes the chosen signature's return type inside
  the call's in-flight window; tsc asks getReturnTypeOfSignature after the call is resolved and cached. Needs the
  chosen signature in `CallResolution` (which plan 38 "not done 2", `ty_call_effects` using getResolvedSignature,
  wants too: do both).
- **Barrier only on a call's first resolution** (tsc's `resolutionStart` rule; the prototype's
  `resolving_nested_call`): +circularReferenceInReturnType, 0 lost in the suite, but **zod 0 -> 6** (4 TS7023, 2
  TS2344): tttt types zod's recursive getters during `z.object(...)`'s resolution (`props_related_impl` typing the
  source accessor prop; `definitely_unrelated`'s `mentions_type_variables` walk over object literals, tttt's stand-in
  for permissive instantiation, which can't see an object literal's free type parameters: `free_params_unknown`).
  tsc first asks a getter's type in the deferred accessor check, after every call resolved. Go only once that is
  found and fixed; otherwise leave the every-resolution barrier (hacks.md `resolve_call`) as the documented cost.
- Eager predicate inference during instantiation (`inference_mapper` -> `signature_predicate` ->
  `ty_infer_type_predicate` checks a body; tsc reads predicates lazily off instantiated signatures):
  recursiveGenericSignatureInstantiation. Small; do it whenever.

## Memory (the stub's question): not laziness
drizzle, 2026-10-05 (loaded machine; RSS is still meaningful): `--checkers 1` peaks at **896 MB** (single-threaded
tsgo: 1054), `--checkers 4` at 1725 MB (multi-threaded tsgo: 1869). The four checkers make 1.66M types between them
vs 683k for one (sum of arenas 1239 MB vs 539): drizzle's files share deep generic types and each checker
instantiates them again. Other corpora grow far less with K: zod 591 -> 716 MB, vue 614 -> 701, valibot 456 -> 528,
type-fest 535 -> 552. **tttt is at or under tsgo's memory in both like-for-like comparisons**; the vision's "memory
is the weak number" compared 4-checker tttt with 1-thread tsgo.
- One checker's arena (539 MB on drizzle): `ty_alloc` 99, instantiated Props 100 (`instantiate_members`, 80 bytes
  each, 1.3M), intern keys 76, `ty_list_copy` 65, member arrays ~45, instantiated Signatures 15.
- Of the instantiated Props, only **38% (drizzle), 18% (zod), 27% (valibot)** ever have their type asked. Lazy
  per-name Props (beyond tsc, which builds them all) would save ≤ ~65 MB per checker on drizzle but every
  enumeration (relations, keyof, spreads, printing) would materialise them anyway, and Prop pointers are identities.
  Cheaper with most of the gain: a compact instantiated Prop (name, flags, parent, target, mapper, type, state: ~48
  bytes; `decl`/`write_type`/`mapped_type` follow `target`), already in `bugs/perf_leads.md`.
- The real lever for drizzle's peak is cross-checker duplication: file-to-checker assignment by affinity (plan 36
  "Later": round robin scatters files sharing types) or sharing the global/lib types. A perf session item, not this
  plan.

## Validation (every stage)
`./fasttest.sh --summary` 0 lost; every corpus unchanged or better (`./realworld.py --corpus X`, xstate/webpack/
drizzle by file:line set; zod-root diffed against the previous build: `./release/tttt -q --keep-going -p
thirdparty/corpora/zod | sort`); `./threadcheck.sh` and `./checkerdiff.py` identical; `./instrcount.sh` within ~1% per
corpus (stage 2's vue +1.1% is the ceiling: look for it before accepting more). Each stage removes its hacks.md
entries and adds what it keeps.
