# Plan 86 stage 4: the census (appendix)

Written 2026-10-20 by the stage 4 design session's research agents (read-only, tttt at 80f33c08f). The design and the
verdict are in plan 86 "Stage 4"; this file is the reference the build-out works through: every lazily written field
of a `Ty` (part A), every `Checker` table, KEY_* kind and LazyType tag (part B), and what bun check (oven-sh/bun#45800)
does (part C). Line numbers drift: grep the function names. Paths under /tmp were the agents' scratch and are gone
(the instrument they used is `scripts/arenasites.sh`). Legends differ: in part A **S** is per-worker scratch; in part
B's KEY_* table **S** is SHARE, **C** sCratch, **H** HARD. Delete a row once the build-out has decided it.

## Part A: Census: what is written into a Ty (and what hangs off it) after it is published

Read-only research for plan 86 stage 4 (shared concurrent type graph), tttt at 80f33c08f. "Published" = reachable by
another caller: interned in `Checker.types`/`links`, stored in a memo, or returned. Writes into a Ty that is still being
built by its constructor (ty_alloc + field init before `*slot = ret`) are **not** listed except where noted; there are
many such casts (`(Ty*)make_type_param(..)`, `fresh->anonymous.state->members = ...` in ty_object_literal_alternative,
relate.c's resolve_global_type, variance markers, restrictive_type_param, clone_type_params' clone_mapper, infer.c's
renamed params, expr.c:238's NON_INFERRABLE return-only type...). They are fine under any protocol as long as
publication is the last store (release).

Legend: **P** = pure (same value whoever computes it first), **P*** = pure *except* when an instantiation/complexity
limit cut the computation short (see section 3), **C** = context-dependent, **S** = must be per-worker scratch.
Hotness: H (relations/instantiation/property access inner loops), M, R (rare).

### 1. Lazily written fields on Ty and its satellites

### 1.1 TyObjectState (declared.state, declared.raw, reference.state, anonymous.state, composite.state of intersections)

| field | writer(s) | caches | class | diagnostics while computing | hot |
|---|---|---|---|---|---|
| `members` + `members_state` | members.c `ty_members_resolving()` (NOT_STARTED path; also the IN_FLIGHT path that publishes `resolve_members()` of a reference whose target's early members exist), `declared_raw_members()` (for `declared.raw`) | resolved Members (props, signatures, index infos, mapped info) | **C** for TY_DECLARED/TY_REFERENCE (and anything reaching them); **P*** for plain anonymous instantiations and intersections | yes: `members_reentered()` -> `ty_resolving_cycle_across_calls(RESOLVING_BASES)` (TS2310/TS2506 reported by the walk later), `members_reentered_unguarded()` -> `ty_instantiation_limit_reached_on()` TS5114/TS5115/TS2589 **at current_node**, anything lowering reports (lower.c: TS2456, TS2749, TS2315, TS2694, TS7022, TS4109), TS2590/TS2799 at current_node from union/tuple construction. Deduped today only because each checker resolves once and identical (span, code, text) diagnostics merge in `error_ctx_sort_and_dedupe()` | H read (inline fast path, "drizzle asks 17M times"), M compute |
| `members` while IN_FLIGHT ("early members") | `early_declared_members()` (bases frame snapshot or own members), `early_static_members()`, class static side partial in `builder_add_class_static_side()` (members.c ~2160), interface index-signature pass in `resolve_declared_members()` (~3105), namespace/merged-class partial (~2385) | a partial member table visible only while the real one is being built (tsc's early-bound symbols) | **S** (it is in-flight state: another worker must never read it as final) | via ty_resolving_cycle_across_calls | R |
| `unexpanded` | `resolve_members()`, `ty_mapped_info_unexpanded()` (which also toggles `members_state` NOT_STARTED->IN_FLIGHT->NOT_STARTED on a published type!) | a mapped type's members before `expand_mapped()` | P* (value); the transient IN_FLIGHT toggle is **S** | lowering diagnostics | M |
| `equivalent_base` + `base_state` | members.c `ty_non_augmenting_base()` | the base an interface/class that adds nothing is equivalent to | P (IN_FLIGHT re-entry returns null without caching) | no (instantiation only: P*) | M (5 callers, relations/printing) |
| `reduced_state` | checker.c `ty_reduced_slow()` | whether an intersection reduces to never | P, except re-entry while IN_FLIGHT answers "itself" | no | M-H (38 callers: property lookup, relations, keyof) |

### 1.2 Ty header and variants

| field | writer | caches | class | diag | hot |
|---|---|---|---|---|---|
| `order_key` | order.c `ty_order_key()` (`((Ty*)t)->order_key = k`) | ty_compare()'s prefix (rank, name/position) | P (compute_key reads only immutable fields/symbols, no checker). Racy-benign 8-byte store; but see `rest_binding` below which changes its input after creation | no | H (every union sort) |
| `evolving_array.final_array` | flow.c `final_array_type()` | finalized array of an evolving array | P* (subtype reduction goes through the relation cache: complexity limits) | no | R |
| `xflags |= TYX_CONSTRAINED_TYPE_VARIABLE` | checker.c `intersection()` after `intern_composite()` (may hit an existing intersection) | IsConstrainedTypeVariable | **Fixed (CONDP, 2026-10-22): set at creation only, intersections made without constraint reduction keyed apart (tsgo's getIntersectionKey '*'), the late write deleted.** Was **C/history**: set only when *this path* built it (`out == 2 && !inherited`); an identical intersection first made by another path lacks it until this path runs. Not lazy: a mutation of a published type. Needs an atomic OR or (better) computing it in the constructor | no | M |
| `anonymous.empty_symbol` | lower.c `ty_dynamic_import_module_type()` (2 sites) on the result of `with_synthetic_default()` = `ty_object_literal(OBJECT_SYNTHETIC)`, which is **structurally interned** (KEY_STRUCTURAL) | tsc's anonymousSymbol (isEmptyAnonymousObjectType) | **history**: mutates a shared interned type; another `{default: X}` synthetic object of the same shape gains the flag. Make it part of the key | no | R |
| `anonymous.module_clone` | lower.c `namespace_import_type()` | KEY_MODULE_CLONE companion | P (type is made fresh per alias by namespace_import_type_uncached; check) | no | R |
| `anonymous.rest_binding` + `rest_of_binding` | lower.c `rest_literal()` after `ty_object_literal_of(.., symbol)` (interned by shape + symbol) | union order key of an object rest | P per symbol, but written after interning; changes `order_key`'s input (if an order_key was computed in between it is stale) | no | R |
| `conditional.outer` | lower.c ~1298 (`collect_outer_params` caller) | outer params keying instances | P; comment says "a root is never shared between checkers" - with a shared graph it is: must move into construction | no | R |
| `conditional.root_aliased` | lower.c TYPE_TERNARY lowering (~4324) | root as named by its alias | first alias that lowers the root (syntactic, so P in practice) | no | R |
| `conditional.branches[i]` (TyCondBranch: `type`, `state`) | checker.c `ty_cond_branch_type()` via `ty_conditional_branch()` | a lazily lowered/instantiated branch | **C**: IN_FLIGHT returns TY_ERROR; uses `ty_instantiate_tail()` (tail count, depth: TS2589 -> error type memoized) | instantiation-limit diagnostics at current_node | M-H (35 callers, conditional resolution, relations, inference) |
| | also *read* by instantiate.c `instantiate_branch()`: whether the root's branch is DONE changes the **intern key** of the instance (`cond_branch_key()`: node+composed mapper vs source+mapper). Identity depends on history | | **history-identity** | | |
| `reference.deferred` (TyDeferredRef: `args`, `state`, `circular`) | lower.c `ty_reference_args()` | a deferred array/tuple/generic reference's arguments | **C**: IN_FLIGHT sets `circular` and returns error args; result reported TS4109/TS4110 at the node or **at current_node** (array/tuple nodes without span) | yes | H read (every arg read of a deferred ref), R compute |
| `reference.from_type_node` | set at creation from `Checker.creating_from_type_node` | recursion identity | **creation-context** ("the instantiation cache keeps it from whoever made it first"): schedule-dependent under sharing | | |

### 1.3 TyParamState (`param.state`)

| field | writer | caches | class | diag | hot |
|---|---|---|---|---|---|
| `constraint` + `constraint_state` | members.c `declared_param_constraint()` (clone path: instantiate the original's; declared path: lower the node, any->unknown, implied infer constraint) | declared constraint | P* for clones; **C** for declared: IN_FLIGHT = circular ("T extends T"), lowering runs under the base constraint's resolving push | lowering diagnostics (TS2456 etc.); TS2313 itself is reported by the walk | H (54 callers: relations, instantiation, inference) |
| `circular` (0/1/2) | `ty_param_constraint_is_circular()` | TS2313 circularity | **C**: depends on `ty_has_non_circular_base_constraint` (base-constraint sentinel, resolving stack); already refuses to cache while `ty_base_constraint_in_flight` | no | H |
| `fallback`, `fallback_state`, `fallback_circular` | `ty_param_default()` | default type | **C**: re-entry while IN_FLIGHT sets `fallback_circular` (TS2716) and the result becomes null | lowering diagnostics | M (29 callers) |
| `distributed` | checker.c `ty_distributed_type_param()` | the distributed twin (fresh type param) | P, but identity-creating: CAS-publish, the loser's twin must not have escaped | no | R |
| `item` | lower.c `ty_type_params_of()`, `merged_local_type_params()` (`if (!item) item = i`) | the GenericItem (constraint/default source) | **history**: the first Generic lowered (merged declarations); constraint/default lookups partly compensate by scanning symbol->decls | no | M |
| `is_infer` | lower.c TYPE_INFERRED (`= true`) | | P (idempotent; ty_type_param already initialises it) | no | R |
| `clone_mapper` | clone sites (members.c `clone_type_params`, infer.c ~486) | | pre-publication only | | |

Type parameters themselves are per-symbol (`KEY_TYPE_PARAM` in `links`), not in `types`: sharing types forces sharing these.

### 1.4 Prop (`Members.props[]`; one Prop may be shared by several Members, see the merged-statics code ~2405)

| field | writer | caches | class | diag | hot |
|---|---|---|---|---|---|
| `type` + `type_state` | members.c `prop_type()` / `prop_type_slow()` | the property's type | **C** in general: IN_FLIGHT returns TY_CIRCULAR + `ty_resolving_cycle(RESOLVING_PROP)`, or re-instantiates (thread-local `prop_reentry_depth`), or re-asks the symbol's type below a call barrier. Sources: `lazy_type_get()` (declaration memos in **per-checker LazySlots**: cycles, flow isolation), `ty_type_of_symbol`, `ty_function_type`, object-literal accessors (`ty_object_literal_accessor_type`: function bodies, `lazy_getter`, flow_uncached is **not** honoured here), reverse-mapped inference, mapped templates; P* for plain instantiated props (`instantiated_prop_type`) and node-lowered props | yes: TS2615 **at current_node** (mapped), TS2502 at the annotation; TS7022 via declaration cycles | H (fast path inline; every relation/property access) |
| `circular` | `prop_type_slow()` (IN_FLIGHT, resolving pop) | cycle seen | **S** (per-computation) | | |
| `write_type_known` (+ link KEY_PROP_WRITE_TYPE) | `prop_write_type()` | setter type | P* (value lives in the checker's `links`: must move onto the Prop or into a shared map) | lowering | M |
| `decl->asserted = nullptr` | `prop_type_slow()` reverse-mapped branch | display: reuse of the source annotation | P (function of the computed type), but a PropDecl mutation after publication | no | R |
| `PropDeclRare` (`prop_decl_rare_mut`) | all builder-time (members.c 700-790, 1600-1960, 4250-4350, 4730) | | pre-publication (but `early` member tables publish Props mid-build: the merged-statics path copies before mutating) | | |

### 1.5 Signature (`Members.call/construct[]`, `KEY_SIGNATURE_*`)

| field | writer | caches | class | diag | hot |
|---|---|---|---|---|---|
| `state`, `type_params`, `params`, `param_count`, `min_args`, `this_type`, **`mapper`** (overwritten by `clone_type_params()`), `target` (JSDoc `@type` target), `ret`/`ret_state` for class construct sigs | members.c `signature_resolve()` | resolved signature | P* for instantiated (`target`) and type-node signatures (fresh clones: identity-creating, CAS); **C** for function expressions: `ty_function_assigned_contextual_signature()`, `KEY_CONTEXT_THIS` link, `ty_iife_effective_arg_count`, untyped-JS rule asks the contextual signature | TS7031 (`report_renamed_bodiless_elements`) at the binding; lowering diagnostics | H (every call, signature relation) |
| `ret` + `ret_state` | `signature_return_type()` | return type | P* for `target`/type-node; **C** for function-backed: `ty_function_return_type()` (body inference: KEY_FUNCTION_RETURN memo, deferred bodies, flow isolation, `outer_inference`, `lazy_getter`); IN_FLIGHT -> TY_CIRCULAR + resolving cycle; pop failure -> any | TS2577 at the annotation, TS7023/TS7010/TS7011 via function.c, TS2705/TS2468 (`report_promise_missing`) | H |
| `predicate`, `predicate_done`, `predicate_in_flight` | `signature_predicate()` | type predicate | P* for annotated; **C** for inferred (`ty_infer_type_predicate` from the body; depends on whether the return type is known yet). `predicate_in_flight` is **S** (re-lowering on re-entry to find TS2456) | lowering | M (25 callers, flow narrowing) |

### 1.6 MappedInfo, Members, MembersRare

| field | writer | caches | class | diag | hot |
|---|---|---|---|---|---|
| `MappedInfo.template_` + `template_state` | members.c `ty_mapped_template()` (`(MappedInfo*)mapped`) | template type | P* (re-entry recomputes *without* storing: good shape for sharing) | lowering, instantiation limits | M-H (24 callers, mapped members, relations) |
| `MembersRare.by_name_order` / `by_name_order_none` | members.c `members_by_name_order()` (also allocates `rare` via `members_rare_mut()` on a published Members) | permutation for instantiated props_by_name | P | no | M |
| `MembersRare.contains_error` | `prop_type_slow()` circular mapped path | mapped type prints in mapped syntax | **C** (cycle-driven, printing-visible) | TS2615 | R |
| `Members.*`, other MembersRare fields, IndexInfo, ReverseMapped, TupleShape, TyMapper, TyPredicate | builder-time only | | immutable | | |

### 2. Type-keyed lazy state that lives in `Checker.links`/`types` instead of on the Ty

These are lazy fields in all but location; a shared graph has to decide each (shared map, or per-worker scratch).
Grouped by class (ty_internal.h KEY_*):
- **P / P* (share):** KEY_FRESH_LITERAL, KEY_REGULAR_UNION, KEY_WIDENED_OBJECTS, KEY_REGULAR_OBJECT_LITERAL (twins:
  identity-creating), KEY_KEYOF_PROPERTIES, KEY_NORMALIZED_INTERSECTION, KEY_UNION_ACCESS, KEY_REDUCED_UNION,
  KEY_UNION_PAIR, KEY_INTERSECTION_OF, KEY_WIDENED_NULLABLE, KEY_RESTRICTIVE_PARAM, KEY_TYPE_VARIABLE_REDUCTION,
  KEY_DEFINITELY_RELATED, KEY_CANONICAL_SIGNATURE, KEY_BASE_SIGNATURE, KEY_IMPLEMENTATION_SIGNATURE,
  KEY_SIGNATURE_INSTANCE, KEY_SIGNATURE_TYPE, KEY_MAPPED_KEY_PARAM, KEY_MAPPED_FILTERING, KEY_PROP_MAPPED_TYPE,
  KEY_PROP_WRITE_TYPE, KEY_SYNTHETIC_PROP_PARTS, KEY_THISLESS_SIGNATURES, KEY_ARRAY_THIS_PROPS, KEY_TUPLE_NAME_ORDER,
  KEY_INDEX_NAME_ORDER, KEY_DECIMAL_ORDER, KEY_ITERATION_TYPES, KEY_UNION_KEY_NAME/MEMBER, KEY_FLOW_DISCRIMINANT,
  KEY_FLOW_NARROWED, KEY_FLOW_NOT_UNIT, KEY_ASSIGNMENT_REDUCED, KEY_EVOLVING_ARRAY, KEY_FUNCTION_TYPE_MENTIONS,
  KEY_MODULE_CLONE, KEY_REST_SYMBOL, KEY_INFERRED_TRUE_BRANCH, KEY_DISTRIBUTION_DEPENDENT, `intersection_props`
  (FoundProp per (intersection, name)).
- **C (hard):** KEY_BASE_CONSTRAINT (relate.c `resolved_base_constraint()`: in-flight sentinel, circular sentinel set from
  the resolving stack, and **`deep` (ConstraintStack length >= 50 / identity seen) memoizes "none"** - the value depends
  on the caller's constraint stack), KEY_SIMPLIFIED ("itself while in flight"), KEY_DISTRIBUTIVE_CONSTRAINT
  (`conditional_constraint_depth`), KEY_VARIANCES / KEY_ALIAS_VARIANCES (relate.c `measure_variances()`/
  `measured_variances()`: in-flight -> Ternary Unknown, `circular`, UNRELIABLE/UNMEASURABLE from what the measurement
  went through; the "restart from the smallest symbol" rule already makes the circular case order-independent, which is
  the right template for the others), KEY_CONDITIONAL_INSTANCE(_FOR_CONSTRAINT) (entries carry a **per-checker
  `instantiation_epoch`** cut-short mark), KEY_REVERSE_MAPPED (`reverse_mapped_depth` HACK), KEY_GENERIC_RELATION.
- **S (per worker):** RelationCache[], KEY_ITERATOR_REPORTED and other report-once flags (KEY_EMIT_HELPERS, the
  `*_reported` bools on Checker), active mappers, `mapper_lists`, all per-expression/per-call keys (KEY_CONTEXT_*,
  KEY_LITERAL_*, KEY_CALL_RESOLUTION, KEY_FLOW_*, KEY_JSX_ATTRIBUTE, KEY_AUTO_FLOW_ISSUE...).
- Per-symbol/per-node (Checker.links design question, not Ty): KEY_TYPE_PARAM, KEY_SYMBOL_LINKS, KEY_GENERIC_PARAMS,
  KEY_ALIAS_INSTANCE, KEY_NODE, KEY_FUNCTION, KEY_FUNCTION_RETURN, KEY_UNION_NODE, KEY_DEFERRED_REFERENCE,
  KEY_STRUCTURAL... Types reachable from shared Tys come out of these, so they must be shared too.

### 3. The cross-cutting context dependencies

1. **In-flight is per call stack, not per type.** Every LazyState above uses IN_FLIGHT to mean "*my* stack re-entered
   this": the answer is a cycle value (TY_CIRCULAR, TY_ERROR, null, error args, partial members, Ternary Unknown) plus
   `ty_resolving_cycle()` marking the resolving stack. Under sharing, worker B finding A's IN_FLIGHT must not take the
   cycle path. IN_FLIGHT has to become (owner worker, state): owner re-entry = cycle as today; foreign = wait or compute
   privately. Waiting can deadlock (A in flight on X needs Y, B in flight on Y needs X: a cross-worker cycle that a single
   checker would have seen as one cycle with one answer). Computing privately and publishing the first DONE is safe only
   for P fields. ~25 IN_FLIGHT sites (sections 1, 2).
2. **Instantiation and complexity limits.** `ty_instantiate()` depth 100 / tail 1000 / `instantiation_count`, relation
   depth/complexity (TS2589, TS2859, TS2321), union size (TS2590), tuple size (TS2799/2800), constraint depth 50. Any
   lazy field computed through them (P* above: practically every one) memoizes an error type if the *caller's* stack was
   already deep. tttt already detects this (`instantiation_limit_hits` deltas in check.c, `cut_short` +
   `instantiation_epoch` in instantiate.c `instantiate_conditional`, active-mapper cache skip in `ty_instantiate`): the
   protocol can generalize it - **publish only if no limit was hit during the computation, else keep the result in the
   worker's scratch overlay**. tsgo has the same dependence per checker, so this is about not making it schedule-dependent.
3. **Diagnostics emitted inside a lazy computation.** Today: computed once per checker, so reported once per checker;
   merged by `error_ctx_sort_and_dedupe()` (identical span/code/text collapse); `Error.once_key` is used only by helpers.c
   (TS2354 family). Under sharing, only the winner reports. Two kinds:
   - span-fixed (at a declaration/annotation/node: TS2502, TS2577, TS4109 with a span, TS7031, lowering's TS2456/2749/
     2315/2694/7022, TS2468/2705): could be recorded with the published value and replayed by each worker on first read
     (then dedupe merges) - or reported by the walk instead of the computation.
   - **at current_node** (TS2589/TS5114/TS5115 via `ty_instantiation_limit_reached*`, TS2615, TS2590, TS2799/2800, TS4109/
     4110 without span): the location is the asker's. These must taint the result (don't publish, same as item 2).
   tsgo itself is K-dependent here (each checker reports at its own first asker); tttt's oracles already tolerate that
   (kdiff lists tsgo-shared ones), but a shared graph would make it timing-dependent rather than partition-dependent.
4. **History/creation context baked into identity:** `from_type_node`, the conditional intern key reading root branch
   states, `TYX_CONSTRAINED_TYPE_VARIABLE` set on hit, `empty_symbol` on a structurally interned object,
   `TyParamState.item` (first Generic), `rest_binding` after interning. Today each is deterministic per checker
   history (and recycling is claimed not to change output: `--checker-memory 0` oracle). Under sharing they're
   schedule-dependent; each needs to move into the key/constructor or be shown unobservable (the gate's ORDER_SEED method).
5. **flow_uncached / lazy_getter.** Expression memos honour `flow_uncached`, lazy Ty fields do not: `prop_type()` of an
   object-literal accessor and `signature_return_type()` of a function computed while `flow_uncached` is set are
   memoized anyway (function.c isolates declarations with `ty_flow_isolate()`, which is the reason it mostly works).

### 4. Non-lazy mutations of published types
- (Fixed) checker.c `intersection()`: `xflags |= TYX_CONSTRAINED_TYPE_VARIABLE` on an interned (possibly pre-existing) intersection.
- lower.c `ty_dynamic_import_module_type()`: `anonymous.empty_symbol = true` on a KEY_STRUCTURAL-interned object.
- lower.c `rest_literal()`: `anonymous.rest_binding/rest_of_binding` after `ty_object_literal_of()` interned it.
- lower.c `namespace_import_type()`: `anonymous.module_clone = true` (fresh in practice).
- lower.c conditional lowering: `conditional.outer` and `root_aliased` after the root is interned.
- members.c `ty_mapped_info_unexpanded()`: `members_state` toggled through IN_FLIGHT and back to NOT_STARTED.
- members.c `prop_type_slow()`: `decl->asserted = nullptr`; `members_rare_mut(expanded)->contains_error = true`.
- members.c `members_by_name_order()`: allocates/attaches `Members.rare` to a published Members.
- members.c `clone_type_params()` (inside signature_resolve): overwrites `sig->mapper` of a published Signature.
- lower.c `ty_pattern_clone()`/`ty_pattern_unclone()`: `memcpy` of a whole Ty **shares the `state` pointer** (TyObjectState)
  between the original and the clone - two types, one mutable state.

### 5. Checker-specific pointers and arena objects reachable from a Ty
- **Atoms are per checker** (`Checker.atoms`, `checker_atom()`): `Prop.name`, `param.name`, `literal.string`,
  `declared.name`, `template_literal.texts`, `SigParam.name`, `TyPredicate.param_name`, `IndexInfo.param_name`;
  `props_by_name` is sorted by atom *address* and property lookup is atom pointer equality; `index_atoms`,
  KEY_UNIQUE_SYMBOL_NAME, KEY_PRIVATE_NAME too. A shared graph needs one global atom table first.
- **Ids**: `Ty.id` from `Checker.next_type_id`; ids are intern-key material (KEY_ANONYMOUS_INSTANCE, conditional keys,
  `ty_id_pair` everywhere) and `LazyMemo` stores ids resolved through the checker's id table
  (`lazy_slots_add_type()`): needs a global id->Ty directory (atomic counter + chunked array).
- **LazySlots** (`Checker.lazy`, `current_checker` thread-local): `PropDecl.lazy` resolves through them; declaration
  types (and so every Prop typed by a LazyType) are per-checker memos today.
- Per-checker singletons referenced by types: `intrinsics[]`, `typeof_type`, `array_target`, `readonly_array_target`,
  `auto_array`, `tuple_params` (tuple targets' element params, *replaced by a longer copy*), `global_types[]`,
  `unknown_signature`, `iteration` state, `disposable_targets`. Must be made once, shared.
- Arena objects hanging off Tys: TyObjectState, TyParamState, TyCondBranch arrays, TyDeferredRef, TyConditionalOuter,
  TupleShape, TyMapper (+ `mapper_lists` cache), Members/MembersRare/Prop/PropDecl/PropDeclRare/decl_spans,
  Signature/SigParam/TyPredicate, IndexInfo/IndexComponent, MappedInfo, ReverseMapped, Variances, FoundProp,
  FunctionReturn, alias_args/composite.types/reference.args lists that **point into intern-table key storage**
  (`intern_key_list()`): a concurrent table must never move or free key storage on resize.
- Static sentinels shared already (`ty_prop_decl_none`, `empty_members`, `base_constraint_in_flight`, `access_none`).

### 6. Classification

| class | sites (function) |
|---|---|
| **publish-once, pure** | `ty_order_key`; `members_by_name_order`; `ty_reduced_slow` (DONE value); `ty_non_augmenting_base`; `ty_distributed_type_param` (CAS identity); `TyParamState.is_infer`; `module_clone`; `root_aliased`; `prop decl->asserted`; twin links (FRESH_LITERAL, REGULAR_*, WIDENED_*), KEY_KEYOF_PROPERTIES, KEY_UNION_PAIR/INTERSECTION_OF, signature-type/instance links, `intersection_props` |
| **publish-once, pure modulo limits (P*: needs the taint rule)** | `ty_members_resolving` for anonymous instantiations/intersections; `resolve_members`'s `unexpanded`; `ty_mapped_template`; `prop_type_slow` for instantiated and node-lowered props; `prop_write_type`; `signature_resolve` for instantiated/type-node signatures; `signature_return_type` for target/type-node; `signature_predicate` annotated; `declared_param_constraint`/`ty_param_default` clone paths; `final_array_type`; KEY_REDUCED_UNION, KEY_UNION_ACCESS, KEY_NORMALIZED_INTERSECTION, KEY_ITERATION_TYPES, KEY_WIDENED_NULLABLE... |
| **publish-once but context-dependent (the hard ones)** | `ty_members_resolving`/`declared_raw_members` for TY_DECLARED/TY_REFERENCE (base cycles, eager-base probes reporting at current_node, members_in_flight stack); `prop_type_slow` for LazyType/symbol/accessor/reverse-mapped/mapped (TS2615 at current_node); `signature_resolve` for function expressions (contextual signature, KEY_CONTEXT_THIS); `signature_return_type` for function-backed (body inference); `signature_predicate` inferred; `declared_param_constraint`/`ty_param_constraint_is_circular`/`ty_param_default` declared paths; `ty_reference_args` (TS4109/4110); `ty_cond_branch_type`; KEY_BASE_CONSTRAINT (depth-50 memo); KEY_SIMPLIFIED; KEY_DISTRIBUTIVE_CONSTRAINT; KEY_VARIANCES/ALIAS_VARIANCES; KEY_CONDITIONAL_INSTANCE epoch marks; history/creation fields (`from_type_node`, TYX_CONSTRAINED_TYPE_VARIABLE, `empty_symbol`, `rest_binding`, `TyParamState.item`, conditional key from branch state) |
| **must move to per-worker scratch** | every IN_FLIGHT marker (owner-tagged), early/partial members (`early_declared_members`, `early_static_members`, the three builder partials), `ty_mapped_info_unexpanded`'s toggle, `Prop.circular`, `TyDeferredRef.circular`, `fallback_circular` (as a cycle flag), `Variances.circular`, `predicate_in_flight`, `prop_reentry_depth` (already TLS), RelationCache, report-once links/flags, active mappers, per-expression/per-call links, resolving / members_in_flight / variance stacks |

**Site count.** Ty-resident lazy publication points: ~40 stores in ~22 functions (members 8 incl. early-member
publishers, unexpanded 2, base 1, reduced 1, order_key 1, final_array 1, param state ~10, cond branch 1, deferred ref 1,
prop type/write/asserted/contains_error 4, signature resolve/ret/predicate 3, mapped template 1, by_name_order 1,
plus 6 non-lazy mutations to move into constructors). Type-keyed link kinds that act as lazy fields: ~45 KEY_* kinds
(~35 shareable, ~8 hard, the rest scratch), each read/written through 1-3 helper sites, but most go through
`intern_get`/`intern_slot`/`ty_link_get`/`ty_link_set`, so a shared-map wrapper covers them at once. Realistic estimate:
**~20-25 distinct protocol implementations** (one per lazy-state machine; the hard ~12 each need a decision on cycles,
limits and diagnostics) plus 2 global prerequisites (atoms, id directory) and a links split.

## Part B: Stage 4 census: what in a `Checker` could be shared (plan 86 stage 4)

Written 2026-10-20 for the stage 4 design session. Sources: src/ty/ty.h (`struct Checker`, `SymbolLinks`),
src/ty/ty_internal.h (KEY_*), src/lazy_type.{h,c}, src/error.{h,c}, src/program.c (program_typecheck, check_share),
src/ty/{checker,instantiate,relate,call,function,lower,members,flow,expr,iterate,helpers,symbol_chain}.c.
Scripts used (in /tmp/stage4/scripts): keymap.py (which table / which functions use each key), ctx.py (comment and
line at each use), table.py (the classification below, with counts).

Classes:
- **SHARE**: a pure fact about a type, symbol, declaration or type node; the same whoever computes it, in whatever
  order (given the computation saw no in-flight state and hit no limit, see "taint" below).
- **SCRATCH**: per expression, per query, per file check, flow generation, inference / contextual state, report-once
  flags: stays per worker.
- **HARD**: would be shared, but the stored value depends on who computed it first or on the computing context
  (in-flight sentinels visible as answers, cycle outcomes that depend on the entry point, results cut short by
  limits, first-creator flags that display reads, diagnostics reported as a side effect).

Tables: T = `Checker.types` (InternTable), L = `Checker.links` (InternTable), N = `Checker.node_links` (LinkTable via
ty_link_get()/ty_link_set()), L* = `links` through `current_checker` (symbol_chain.c), - = unused.

### 1. KEY_* kinds (143) and node_links kinds (23 of them)

Counts: **SHARE 85, SCRATCH 40, HARD 16, dead 2** (KEY_LITERAL_METHOD_OVERLOADS and KEY_IMPLEMENTATION_SIGNATURE are
declared but never written; delete them). The node_links kinds are the N rows: FRESH_LITERAL, SYMBOL_LINKS,
CALL_RESOLUTION, BASE_CONSTRAINT, IMPLIED_CONSTRAINT, CHECK_MARKER_REF, CONTEXT_PARAM, CONTEXT_RETURN, LITERAL_CONTEXT,
LITERAL_ARG_OF, DECL_GROUP, FUNCTION, DEFERRED_BODY, LITERAL_ANCHOR, FUNCTION_ANCHOR, FUNCTION_RETURN, CONTEXT_CHECKED,
FLOW_REACHABLE, FLOW_CALL, FLOW_CALL_IN_EXPR, FUNCTION_FREE_PARAMS, PSEUDO, CLASS_MEMBER_SYMBOLS (23: 7 SHARE-ish
declaration facts, 3 HARD, 13 SCRATCH).

Precedent: survivors.c's kind_fate() (plan 79) already sorts the kinds into ROOT / IDENTITY / CACHE / EXPR for a
copying recycle. Its ROOT+IDENTITY+CACHE set is close to SHARE+HARD here; the difference is concurrency: survivors.c
lists KEY_SIMPLIFIED, KEY_BASE_CONSTRAINT, KEY_VARIANCES as CACHE (fine to copy once done), but their in-flight
sentinels make them HARD for a graph other threads read while it grows.

| # | KEY | table | class | what it keys/holds, why |
|---|---|---|---|---|
| 1 | KEY_LITERAL | T | **S** | literal type by (value, enum); pure hash-consing |
| 2 | KEY_UNION | T | **S** | union by (alias, alias args, member pointers, origin); member order uses ty_compare (id fallback: gate cond. 1) |
| 3 | KEY_INTERSECTION | T | **S** | same as union |
| 4 | KEY_REFERENCE | T | **H** | generic reference (target, args, this_arg); Ty.reference.from_type_node is set from Checker.creating_from_type_node by whoever creates it FIRST (relate.c recursion identity reads it) |
| 5 | KEY_TUPLE_TARGET | T | **S** | tuple target per shape |
| 6 | KEY_KEYOF | L | **S** | deferred keyof T |
| 7 | KEY_INDEXED | T | **S** | deferred T[K] (intern_deferred) |
| 8 | KEY_CONDITIONAL | T | **S** | conditional type (root, mapper words) |
| 9 | KEY_SUBSTITUTION | T | **S** | substitution type (base, constraint) |
| 10 | KEY_ANONYMOUS_INSTANCE | T | **S** | instantiated anonymous {target, mapper}; instantiate_anonymous(), ty_realias(): biggest duplicated table (drizzle 229k) |
| 11 | KEY_UNIQUE_SYMBOL | T | **S** | unique symbol per UniqueSymbolDecl key |
| 12 | KEY_FRESH_LITERAL | N | **S** | regular literal -> fresh twin (ty_fresh_literal) |
| 13 | KEY_REGULAR_UNION | T | **S** | ty_regular() of a union |
| 14 | KEY_WIDENED_OBJECTS | L | **S** | ty_widened_objects() |
| 15 | KEY_REGULAR_OBJECT_LITERAL | L | **S** | ty_regular_object_literal() |
| 16 | KEY_TEMPLATE_LITERAL | T | **S** | template literal type (make_template; template.c can hit the instantiation limit: taint) |
| 17 | KEY_STRING_MAPPING | T | **S** | Uppercase<T> & co |
| 18 | KEY_MAPPED_KEY_PARAM | L | **S** | MappedInfo -> mapper to constrained key stand-in (relate.c) |
| 19 | KEY_INSTANTIATION_QUERY | L | **H** | type id -> the `typeof f<T>` TypeNode that FIRST lowered to it (lower_node); ty_display prints from it: display depends on first lowerer |
| 20 | KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT | T | **H** | as below, under Checker.for_constraint_root |
| 21 | KEY_CONDITIONAL_INSTANCE | T | **H** | instantiate_conditional(): entry[2] = instantiation_epoch+1 when cut short by TS2589 limits (per current node): a cut-short entry is context (current node, instantiation_count) dependent; clean entries are SHARE |
| 22 | KEY_TYPE_PARAM | L | **S** | Symbol -> type parameter Ty |
| 23 | KEY_SYMBOL_LINKS | N | **H** | SymbolLinks: declared_state (LAZY in-flight), declared/static type, circular (TS2456 reported when it finishes), base_cycle, base_variable_state. DONE+acyclic values are SHARE; in-flight/circular are per-computation |
| 24 | KEY_GENERIC_PARAMS | L | **S** | Generic -> TyList |
| 25 | KEY_ALIAS_INSTANCE | L | **S** | (alias, args) -> Ty |
| 26 | KEY_NODE | L | **S** | TYPE_INTERFACE/TYPE_CALLABLE node or overload set -> anonymous Ty (node address: deferred-body lifetime, see risks) |
| 27 | KEY_NAMESPACE | L | **S** | namespace value type |
| 28 | KEY_TYPE_ONLY_EXPORT | L | **S** | (namespace, export) -> Prop |
| 29 | KEY_MERGED_VALUE | L | **S** | merged value type |
| 30 | KEY_LITERAL_METHOD_OVERLOADS | - | **DEAD** | declared, never used (survivors.c doesn't list it either) |
| 31 | KEY_DEFERRED_OUTER | L | **S** | TyConditionalOuter of a deferred node |
| 32 | KEY_CALL_RESOLUTION | N | **C** | CallResolution per call: in_flight, ret, sig, CallResolutionRare.errs (diagnostics replayed by call_typecheck); per expression |
| 33 | KEY_DEFERRED_REFERENCE | L | **S** | deferred array/tuple/identifier node + mapper pairs -> Ty |
| 34 | KEY_STRUCTURAL | L | **S** | object literal shape -> Ty (ty_object_literal_alternative) |
| 35 | KEY_KEYOF_PROPERTIES | L | **S** | keyof from props; ty_keyof() caches only once members are done (pinned pattern) |
| 36 | KEY_SIMPLIFIED | L | **H** | ty_simplified(): slot holds the type ITSELF while in flight (tsc's circularConstraintType): another worker would read the placeholder as the answer |
| 37 | KEY_NORMALIZED_INTERSECTION | L | **S** | normalized_intersection() |
| 38 | KEY_MAPPED_FILTERING | L | **S** | relation-derived bit per declared mapped type |
| 39 | KEY_PROP_MAPPED_TYPE | L | **S** | Prop -> mapped type it belongs to (side table of Members of shared types; drizzle 121k) |
| 40 | KEY_PROP_WRITE_TYPE | L | **S** | Prop -> write type |
| 41 | KEY_UNION_ACCESS | L | **S** | access() of a union at depth 0 only (depth cutoff would be context) |
| 42 | KEY_BASE_CONSTRAINT | N | **H** | resolved_base_constraint(): in_flight / circular / none sentinels, ty_resolving barrier, 'first answer stays' |
| 43 | KEY_INFERRED_TRUE_BRANCH | L | **S** | resolvedInferredTrueType |
| 44 | KEY_DISTRIBUTION_DEPENDENT | L | **S** | per root conditional |
| 45 | KEY_DISTRIBUTIVE_CONSTRAINT | L | **S** | reads base constraints: taint if one was circular/in flight |
| 46 | KEY_IMPLIED_CONSTRAINT | N | **S** | TernaryType node -> TyImplied |
| 47 | KEY_RESTRICTIVE_PARAM | L | **S** | unconstrained stand-in per param |
| 48 | KEY_TYPE_VARIABLE_REDUCTION | L | **H** | primitive_constraint_of() writes 1 (= none) while in flight ('re-entered while its base constraint is computed: none'); the (var, prim) entries are SHARE |
| 49 | KEY_DEFINITELY_RELATED | L | **S** | definitely (un)assignable memo; relation-derived |
| 50 | KEY_CANONICAL_SIGNATURE | L | **S** | signature -> canonical instantiation |
| 51 | KEY_BASE_SIGNATURE | L | **S** | signature -> base-constraint mapper |
| 52 | KEY_VARIANCES | L | **H** | Variances with LAZY_STATE_IN_FLIGHT, circular restart from the smallest symbol (measured_variances), UNRELIABLE/UNMEASURABLE marks from the relation in progress |
| 53 | KEY_MARKER | L | **S** | marker instantiation per (declared, index, which) |
| 54 | KEY_ALIAS_VARIANCES | L | **H** | as KEY_VARIANCES for aliases |
| 55 | KEY_ALIAS_MARKER | L | **S** | marks marker instantiations |
| 56 | KEY_CHECK_MARKER | L | **S** | super-T/sub-T markers of variance annotation checks |
| 57 | KEY_CHECK_MARKER_REF | N | **S** | marks references used by those checks |
| 58 | KEY_GENERIC_RELATION | L | **C** | relation key words -> a number from Checker.generic_relation_keys, used as RelationCache key: lives and dies with the relation caches |
| 59 | KEY_CONTEXT_PARAM | N | **C** | unannotated parameter's type from contextual signature (first contextual check) |
| 60 | KEY_CONTEXT_PARAM_UNWIDENED | L | **C** | same, before widening |
| 61 | KEY_CONTEXT_PARAM_PATTERN | L | **C** | pattern fallback flag |
| 62 | KEY_CONTEXT_RETURN | N | **C** | function expression's contextual return type |
| 63 | KEY_CONTEXT_RETURN_SELF | L | **C** | return inferred with own signature as context (TS7024) |
| 64 | KEY_CONTEXT_IIFE_RETURN | L | **C** | IIFE call's contextual type |
| 65 | KEY_CONTEXT_THIS | L | **C** | contextual this |
| 66 | KEY_YIELD_CONTEXT | L | **C** | yield's contextual type |
| 67 | KEY_ACCESS_USES_CONTEXT | L | **C** | per access expr flag |
| 68 | KEY_LITERAL_CONTEXT | N | **C** | object literal's contextual type (for `this`) |
| 69 | KEY_PATTERN_COMPUTED | L | **C** | implied pattern type flag (type id key, but only for pattern clones made per check) |
| 70 | KEY_BINDING_COMPUTED_KEY | L | **C** | computed name type, in-flight marker `{}` |
| 71 | KEY_BINDING_ACCESS_KEY | L | **C** | key an element was read with (TS2538) |
| 72 | KEY_LITERAL_INFERENCE | L | **C** | serial of the OuterInference (per worker counter) |
| 73 | KEY_LITERAL_PARENT | L | **C** | literal's parent literal |
| 74 | KEY_LITERAL_ARG_OF | N | **C** | literal's call |
| 75 | KEY_DECL_GROUP | N | **S** | FunctionImpl -> TyDeclGroup |
| 76 | KEY_DECL_GROUP_OF | L | **S** | (symbol, block) -> TyDeclGroup |
| 77 | KEY_JSX_ATTRIBUTE | L | **C** | 'last check' attribute type |
| 78 | KEY_FUNCTION | N | **S** | FunctionImpl -> function type object (signature read off the AST; its return type is KEY_FUNCTION_RETURN) |
| 79 | KEY_ITERATION_TYPES | L | **S** | (type, which) -> iteration types (reports are KEY_ITERATOR_REPORTED's) |
| 80 | KEY_UNIQUE_SYMBOL_NAME | L | **S** | escaped name atom -> unique symbol |
| 81 | KEY_PRIVATE_NAME | L | **S** | (class, #x) -> atom |
| 82 | KEY_PRIVATE_NAME_CLASS | L | **S** | atom -> class |
| 83 | KEY_WIDENED_NULLABLE | L | **S** | ty_widened_nullable |
| 84 | KEY_DEFERRED_BODY | N | **C** | FunctionImpl queued in Checker.deferred |
| 85 | KEY_CHECKED_ONCE | L | **C** | ty_first_check() report-once (class_decl.c, jsx.c) |
| 86 | KEY_REFERENCED_MEMBER | L | **C** | noUnusedLocals marks (per file check) |
| 87 | KEY_LITERAL_ANCHOR | N | **C** | ContextAnchor of an object literal |
| 88 | KEY_QUICK_CALLEE_CHECKED | L | **C** | report-once flag: check_assigned_callee() reports TS18048/TS2722 into my errs once per assignment |
| 89 | KEY_FUNCTION_ANCHOR | N | **C** | ContextAnchor of a function expression |
| 90 | KEY_INTERSECTION_PROP_CONTEXT | L | **S** | (intersection id, key id) -> distributed contextual prop type: pure in type ids |
| 91 | KEY_UNDEFINED_PROPERTY | L | **H** | name -> the Prop the FIRST normalized-in optional undefined property was made of: identity chosen by first writer (decl emit / display) |
| 92 | KEY_SYNTHETIC_PROP_PARTS | L | **S** | side table of synthetic intersection props |
| 93 | KEY_JS_IMPLICIT_ANY | L | **S** | JS assignment declaration widened to any: published with the symbol's type |
| 94 | KEY_RETURN_WIDENED_ANY | L | **H** | published with KEY_FUNCTION_RETURN (inherits its hazards) |
| 95 | KEY_FUNCTION_RETURN | N | **H** | FunctionReturn: state IN_FLIGHT/reevaluating (resolving barrier), circular, promise_missing (a pending TS2705 reported on the next non-speculative read); for function expressions also depends on the first contextual signature |
| 96 | KEY_CONTEXT_CHECKED | N | **C** | contextual signature decided by the first check (tsc's ContextChecked) |
| 97 | KEY_IMPLEMENTATION_SIGNATURE | - | **DEAD** | only named in survivors.c |
| 98 | KEY_RETURN_REPORTABLE | L | **C** | widening reportability decided with the return type and the contextual signature then |
| 99 | KEY_FLOW_REACHABLE | N | **C** | FlowNode reachability |
| 100 | KEY_FLOW_REFERENCE | L | **C** | reference key ids (per worker numbering, Checker.flow_key_count) |
| 101 | KEY_FLOW_EXPR | L | **C** | flow's expression types, by generation while loops run |
| 102 | KEY_FLOW_CONTEXT_FREE_EXPR | L | **C** | same, context-free |
| 103 | KEY_FLOW_CALL | N | **C** | CallEffects of a statement call |
| 104 | KEY_FLOW_CALL_IN_EXPR | N | **C** | CallEffects of an expression call |
| 105 | KEY_FLOW_DISCRIMINANT | L | **S** | (union, name) -> discriminant bit; types only |
| 106 | KEY_UNION_PAIR | L | **S** | ty_union of two types |
| 107 | KEY_INTERSECTION_OF | L | **S** | intersection without alias |
| 108 | KEY_ASSIGNMENT_REDUCED | L | **S** | (declared, assigned) -> reduced |
| 109 | KEY_REVERSE_MAPPED | L | **S** | top level only (depth cutoff below would be context) |
| 110 | KEY_UNION_KEY_NAME | L | **S** | union -> key property |
| 111 | KEY_UNION_KEY_MEMBER | L | **S** | (union, literal) -> member |
| 112 | KEY_FLOW_NARROWED | L | **S** | narrowed_type memo, types only |
| 113 | KEY_FLOW_NOT_UNIT | L | **S** | union without a unit |
| 114 | KEY_FLOW_EXHAUSTIVE | L | **C** | per switch, value 2 = being worked out |
| 115 | KEY_EVOLVING_ARRAY | L | **S** | evolving array type constructor |
| 116 | KEY_AUTO_FLOW_ISSUE | L | **C** | per identifier expression |
| 117 | KEY_FLOW_LOOP | L | **C** | (loop label, reference key) -> type at top of loop |
| 118 | KEY_SYNTHETIC_ACCESS | L | **C** | synthetic Expr made per owner (has its own LazyType slot) |
| 119 | KEY_REDUCED_UNION | L | **S** | ty_reduced |
| 120 | KEY_SIGNATURE_INSTANCE | L | **S** | (signature, args) -> instantiation |
| 121 | KEY_SIGNATURE_TYPE | L | **S** | signature -> anonymous type |
| 122 | KEY_CONTEXT_FREE | L | **C** | return-only type of a function expression (contextual machinery) |
| 123 | KEY_FUNCTION_FREE_PARAMS | N | **S** | free params of a function's annotations |
| 124 | KEY_FUNCTION_TYPE_MENTIONS | L | **S** | per function type |
| 125 | KEY_CIRCULAR_BINDING | L | **H** | binding / export default found circular (TS2502/TS7022): entry-point dependent; checker.circular_bindings gates lookups |
| 126 | KEY_ELEMENT_WIDENED_ANY | L | **S** | published with the binding element's type |
| 127 | KEY_ARRAY_THIS_PROPS | L | **S** | Array target -> this props |
| 128 | KEY_TUPLE_NAME_ORDER | L | **S** | ordering tables |
| 129 | KEY_INDEX_NAME_ORDER | L | **S** | ordering tables |
| 130 | KEY_DECIMAL_ORDER | L | **S** | ordering tables |
| 131 | KEY_YIELD_WIDENED_ANY | L | **H** | published with KEY_FUNCTION_RETURN |
| 132 | KEY_ITERATOR_REPORTED | L | **C** | report-once flag (TS2767/TS2490 once per iterable type) |
| 133 | KEY_UNION_NODE | L | **S** | (union/generic reference TypeNode, ctx.generic) -> lowered type |
| 134 | KEY_EMIT_HELPERS | L | **C** | report-once per (file, helper): Error.once_key/once_order already makes it partition-independent |
| 135 | KEY_NAMESPACE_IMPORT | L | **S** | namespace import -> module type |
| 136 | KEY_MODULE_CLONE | L | **S** | module type -> alias (identity pair of the above) |
| 137 | KEY_REST_SYMBOL | L | **S** | rest type -> binding symbol (identity pair) |
| 138 | KEY_THISLESS_SIGNATURES | L | **S** | raw Members -> ThislessSignatures |
| 139 | KEY_ACCESSIBLE_CHAIN | L* | **S** | (scope, symbol, meaning) -> chain (symbol_chain.c via current_checker->links; cached only outside a search) |
| 140 | KEY_PSEUDO | N | **S** | declaration emit's Pseudo per AST item (syntactic) |
| 141 | KEY_EXTENDED_CONTAINERS | L* | **H** | (symbol, file) -> modules: filled only once some are found (tsc's extendedContainersByFile): first asker decides what later display sees |
| 142 | KEY_MODULE_DISPLAY_NAME | L | **S** | (module, file, mode, usage) -> import() name |
| 143 | KEY_CLASS_MEMBER_SYMBOLS | N | **S** | ClassDecl -> member symbols |

Notes on the table:
- "SHARE" assumes the publish rule of section 6 (publish only untainted results). Several SHARE rows are
  relation-derived (DEFINITELY_RELATED, MAPPED_FILTERING, DISTRIBUTIVE_CONSTRAINT, FLOW_DISCRIMINANT...) or read
  members/constraints that may be in flight; they are pure only when nothing on the way was in flight or cut short.
  The code already has this discipline in places: ty_keyof() caches KEY_KEYOF_PROPERTIES only `if
  (ty_members_done(operand))`; access() caches unions only at depth 0; reverse_mapped_type() only at the top level;
  instantiate_conditional() tags cut-short entries with the epoch; ty_instantiate_mapped() doesn't keep an active-mapper
  result when `instantiation_limit_hits` moved; ty_check_expr()/check_with() don't write memos under `flow_uncached` or
  (for context-using references) under `inferential_depth`. Stage 4 has to make that discipline uniform.
- The type-constructor keys (LITERAL ... STRING_MAPPING, ANONYMOUS_INSTANCE, CONDITIONAL_INSTANCE, ALIAS_INSTANCE,
  DEFERRED_REFERENCE, SIGNATURE_INSTANCE, UNION_PAIR, INTERSECTION_OF...) are where the duplication is (plan 85's
  census: instantiations of the project's own generics). Union/intersection keys hold member *pointers* (and alias
  pointers), not ids, so they need no id translation; the order of members does depend on ids at ties (gate
  condition 1).
- Many SHARE values are side tables of shared objects (PROP_MAPPED_TYPE, PROP_WRITE_TYPE, SYNTHETIC_PROP_PARTS,
  THISLESS_SIGNATURES, MODULE_CLONE, REST_SYMBOL keyed by `Prop*`, `Members*`, `Ty*`). They must be published together
  with (before) the object they describe, or be moved into it: a side table entry missing for a shared Prop is
  a wrong answer, not a cache miss (prop_type_slow() reads KEY_PROP_MAPPED_TYPE to know what a prop *is*).
- KEY_JS_IMPLICIT_ANY, KEY_ELEMENT_WIDENED_ANY, KEY_RETURN_WIDENED_ANY, KEY_YIELD_WIDENED_ANY record "this widened to
  any" for the walk to report later: they are diagnostics-as-data and must be published atomically with the type they
  describe.

### 2. The other `Checker` fields

| field(s) | what | class |
|---|---|---|
| program, binder | read-only program | SHARE (already shared) |
| enum_member_initializer, suggestion_access, jsx_element, current_node, const_enum_object_ok, type_only_site_depth, within_unreachable, return_context, defer_checks, checking_file, file_order, display_of/display_as, reporting_candidate, lazy_getter(+_nested), contextual_patterns[16] | walk / reporting context | SCRATCH |
| errs | where ty_error_at() reports | SCRATCH (per worker sink; merged by program_typecheck) |
| painted_kept* | declaration walk's painted symbols | SCRATCH (per file) |
| check_ms, files_taken, recycles, peak_memory | stats | SCRATCH |
| held_files, cross_files, held*, held_count | deferred body materializations this incarnation reads (plan 78) | SCRATCH, but **HARD interaction**: shared types/keys point into materialized bodies (KEY_NODE by TypeNode address, function types by FunctionImpl, KEY_UNION_NODE, LazySlot memos); a shared graph pins every materialization any published type touches (deferred_bodies_release() can't drop it) |
| deferred, deferred_count | tsc's checkNodeDeferred queue | SCRATCH |
| lazy (LazySlots) | memos of every LazyType + the id -> Ty table | split, see section 3 |
| arena | types, keys, memos | becomes: shared arena(s) for published types + per-worker scratch arena |
| next_type_id | id counter | SHARE: one atomic counter (gate condition 1), or per-worker private id space for scratch types |
| intrinsics[], typeof_type | made first, in tsc's order | SHARE (create once before workers start: ids stay low and fixed) |
| atom_constructor/prototype/bind, index_atoms, index_atoms_by_name | atoms the checker makes | SHARE (immutable after creation; index_atoms grows by realloc: needs publish-by-replacement like tuple_params) |
| atoms (InternTable) | checker_atom(): content -> Atom | SHARE: atoms are compared by pointer (`mid_name == name` in ty_get_prop()), so a shared graph *requires* a shared atom table |
| types, links (InternTable) | section 1 | split by kind: the tables become a shared table (SHARE rows) + a per-worker table (SCRATCH rows) + per-worker overlay for HARD/tainted entries |
| intersection_props (InternTable) | (intersection id, name) -> FoundProp (find_prop_in) | SHARE with taint (it reads members that may be in flight); drizzle ~250k entries, the biggest untagged table |
| node_links (LinkTable) | 23 kinds above | split like links |
| call_errors | ErrorCtx of call resolutions, owned until checker_drop | SCRATCH (goes with KEY_CALL_RESOLUTION) |
| array_target, readonly_array_target, auto_array, unknown_signature | lib types made on first use | SHARE (publish once; better: made before workers start) |
| tuple_params | element type params shared by tuple targets, replaced by a longer copy | SHARE: already publish-by-replacement (a target keeps its old list valid); needs an atomic pointer swap and a lock for growth |
| global_types[14], global_types_resolved[14] | String, Number... looked up on first use (relate.c resolve_global_type) | SHARE, publish once (or resolve all before workers start: ids then don't depend on first use) |
| iteration | lib iteration interfaces (iterate.c) | SHARE publish once |
| disposable_targets[2], disposable_looked_up[2] | `Disposable \| null \| undefined` unions | SHARE publish once |
| awaited_reported, iterable_iterator_reported[2], iterable_reported | TS2318 report-once flags | SCRATCH (each worker reports once; error_ctx_merge() drops repeated global diagnostics) |
| instantiation_depth, instantiation_stack[100], instantiation_tail_count, instantiating_tail, instantiation_count, instantiation_epoch | tsc's per-node instantiation limits | SCRATCH, and the source of taint (TS2589) |
| instantiation_limit_hits, relation_complexity_hits, relation_depth_hits | limit counters (check.c compares before/after) | SCRATCH; their deltas are the taint signal |
| mapper_lists[1024] | recently kept mapper lists by their types (mapper_list()) | SCRATCH cache (could be shared; it's a hint table, lossy) |
| active_mappers* | tsc's activeTypeMappers caches, cleared when the outermost instantiation returns | SCRATCH |
| for_constraint_root | distributive constraint in progress | SCRATCH (and it selects KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT) |
| resolving*, resolving_start, base_constructors_in_flight | tsc's resolutionTargets stack (ty_resolving_push/pop/cycle) | SCRATCH: **the** in-flight record; with sharing it must become the only place in-flight lives (section 6) |
| members_in_flight*, eager_probe_depth/floor/reported, bases_frames | members resolution in progress | SCRATCH (but they guard Ty-level in-flight states, see below) |
| resolving_nested_call, resolving_call_start, resolving_call, outer_inference, active_inferences, inference_serial, inferential_depth, argument_return_depth, return_inference_of, return_inference_skipped | call resolution / inference | SCRATCH |
| creating_from_type_node | flags what ty_reference_with_this() creates | SCRATCH, but makes KEY_REFERENCE HARD |
| circular_bindings | a KEY_CIRCULAR_BINDING exists (lookup skip) | SCRATCH |
| relations[TY_RELATION_COUNT] (RelationCache u64 -> u8), relating | relation results by (source id, target id), the relation in progress | SCRATCH first. Definite results (not maybe / not unreliable / not depth-limited) are pure and a big shared win later (drizzle K1: assignable 83.5k entries), but KEY_GENERIC_RELATION numbering is per worker and goes with them |
| enum_evaluating | enum member being evaluated | SCRATCH |
| conditional_constraint_depth, reverse_mapped_depth | depth guards | SCRATCH (depth-dependent results must not publish) |
| flow_* (flow_inline_level, last_reachability_flow/_reachability, flow_substitutes_constraints, flow_uncached, flow_incomplete_seen, flow_key_count, flow_expr_generation(s), flow_loops*, flow_loop_start, flow_shared*, flow_analysis_disabled) | control flow analysis | SCRATCH |
| generic_relation_keys | KEY_GENERIC_RELATION counter | SCRATCH |
| variance_depth, variance_stack[64], variance_stack_len, variance_reports | variance measurement in progress | SCRATCH; makes KEY_VARIANCES HARD |

Thread-local globals outside Checker that join the census: function.c `inferred_promise_missing`,
`return_type_speculative`; expr.c `collected_returns`; members.c `prop_reentry_depth`; ty_display.c
`node_fell_back`, `pattern_comments`, `pattern_indent` (all SCRATCH, already thread_local). order.c's
`id_fallbacks`/`tie_calls` are stats.

Ty-level lazily written state (not in Checker, but the same census question; for the design session): TyObjectState
(`members_state`/`members`, `base_state`/equivalent base), TyParamState (`constraint_state`, `fallback_state`),
deferred reference/tuple `state` (ty.h ~491/498), Prop `type_state`, Signature `state`/`ret_state`, MappedInfo
`template_state`, SymbolLinks `declared_state`/`base_variable_state`. All are LazyState with an IN_FLIGHT value read
by the same thread as a cycle (ty_members_resolving(): early members, `members_reentered()`, `empty_members`), i.e.
HARD in exactly the KEY_BASE_CONSTRAINT way.

### 3. LazySlots and LazyType tags

How it works today (src/lazy_type.{h,c}): every LazyType in the AST has a process-wide `slot` (lazy_slot_new(), per
thread blocks of 256 slots so a memo page is one file's). Each checker's `LazySlots` maps slot -> `LazyMemo` (12
bytes: `evaluated` u32 type id, `context` u32 type id, flags evaluation_started / is_self_referential / reevaluating /
done / pattern_retried, `hits`). Type ids are resolved through `LazySlots.types`, a two-level id -> `Ty*` table that
ty_alloc() fills for *every* type the checker makes (`lazy_slots_add_type(&my lazy, ret)`), read via
`current_lazy_slots` (thread_local). Ids are dense per checker (0..next_type_id), which is what makes 4-byte memos
possible (vscode: 10M memos).

Tags:
| tag | where | level | class |
|---|---|---|---|
| LAZY_TYPE_EVALUATED | implicit any; also `PropertyExpr.name_slot` memos (ty_access_key_type(): fresh string literal of a name) | none / pure | SHARE-able (trivial) |
| LAZY_TYPE_EXPR | every Expr (expr_lookup_names), class `extends` expression | expression: memo + the contextual type it was computed with (check_with(), lazy_type_set()) | SCRATCH. The class heritage expression is declaration-level in effect (the class's base) |
| LAZY_TYPE_INITIALIZED_BINDING | variables, parameters, properties' bindings (binding.c) | declaration for module/namespace/class-level variables and annotated or defaulted parameters of declarations; contextual for unannotated parameters of function expressions (they read KEY_CONTEXT_PARAM); local for variables in bodies | SHARE for top-level/exported declarations (HARD: in-flight/circular state, TS7022 decided by the memo being TY_CIRCULAR, reported by binding.c's walk); SCRATCH for function-expression parameters and body locals |
| LAZY_TYPE_FOR_EACH_LOOP_VAR | for-in/of variable (statement.c) | local | SCRATCH |
| LAZY_TYPE_CLASS_ITEM_FIELD | class property (class_decl.c) | declaration | SHARE (HARD: cycles) |
| LAZY_TYPE_CLASS_ITEM_ACCESSOR | class accessor | declaration | SHARE (HARD: cycles) |

Note lazy_type_evaluate_inner(): only non-EXPR, non-INITIALIZED_BINDING tags are flow-isolated (ty_flow_isolate());
an initialized binding's evaluation sees the loops in progress, so even a "declaration" binding's result can depend on
the flow state of whoever asked first if it's a local; module-level bindings have no enclosing loop.

The LazyMemo carries in-flight state (`evaluation_started && !done`, `reevaluating`, `hits`, `is_self_referential`,
`pattern_retried`) in the same word as the result. With sharing, that state cannot live in a shared memo: another
worker seeing `evaluation_started` would take it as a cycle (TY_CIRCULAR, a spurious TS7022) or reevaluate. In-flight
must live on the worker's resolving stack (or a per-worker overlay memo); the shared memo is written once, `done` only.

What shared ids mean for LazyMemo:
- With one atomic id counter, ids stay unique but a worker's ids are no longer dense: the id -> Ty table must become
  one shared table (chunked, chunks allocated with CAS, entry written before the Ty is published, read without locks).
  Every worker's memo resolution goes through it: one more shared, cold-ish structure on the memo-read path (today's
  per-checker table is hot in L1/L2).
- If scratch types (fresh object literals of expressions, inference temporaries, contextual instantiations, flow's
  evolving arrays...) are also made in the shared graph, ids and memory grow with no recycle. The alternative is two id
  spaces: shared types from the atomic counter, worker-private types from a per-worker counter with a tag bit; a
  LazyMemo id resolves through the shared or the private table by the bit. Invariant needed: nothing shared points at a
  private type (a published result that mentions a private type must be either re-made in the shared graph or not
  published) - the old->young rule of a generational heap.
- The memo stores ids, not pointers, so a shared memo holding a shared type's id is valid for every worker. A scratch
  memo holding a private id is only valid for its owner; recycling a worker drops its private ids and memos only.
- `LazyMemo.context` is compared against `ctx.contextual->id` (check_with()); with shared ids that comparison still
  works.
- lazy_slots_drop() today frees everything per checker; with sharing only the scratch part goes at a recycle.

### 4. Diagnostics produced during lazy computations

Where they come from: deep code reports with ty_error_at()/ty_error_at_current_node() (instantiate.c) into `Checker.errs`
(checker_errs[c] during program_typecheck, the program's own ErrorCtx otherwise; dropped unless `errs->phase ==
ERROR_PHASE_TYPECHECK`). Examples: TS7022 from lower.c (assignment declarations 1617, export default 1703) and
expr.c 2295, TS2589 (ty_instantiation_limit_reached*, at the *current node*), TS2321/TS2859 relation limits, TS2705/TS2468
(function.c report_promise_missing(), a pending diagnostic kept in FunctionReturn.promise_missing), TS18048/TS2722
(flow.c check_assigned_callee()), TS2767/TS2490 (iterate.c report_iterator_methods()), TS2318 global types, TS2354/
TS2343 (helpers.c). Walk-level checks get an explicit `ErrorCtx* errs`; some declaration outcomes are *stored* and
reported by the owning file's walk (binding.c:588 TS7022 when the binding memo is TY_CIRCULAR, SymbolLinks.circular ->
TS2456, KEY_*_WIDENED_ANY -> widening errors, CallResolutionRare.errs replayed by call_typecheck()).

How exactly-once and K-independence hold today:
1. Every checker that computes something reports its deep diagnostics into its own errs; the checker that checks file
   F always computes everything F's walk needs, so F's diagnostics are always reported at least once.
2. program_typecheck() merges every checker's ErrorCtx; error_ctx_merge() drops global diagnostics already present;
   error_ctx_sort_and_dedupe() (tsc's sortAndDeduplicateDiagnostics) sorts by span/code/head and keeps one per
   (span, code, head message), choosing deterministically among chains. So duplicates from several checkers collapse.
3. Diagnostics tsc reports once per program from whichever file asks first carry Error.once_key; each checker reports
   them once, error_ctx_keep_first_once() keeps the one with the lowest once_order (Checker.file_order, the checked
   file's index) = what a single checker would report. Only helpers.c uses it today.
4. Call resolutions are resolved once per checker and their diagnostics are data (CallResolutionRare.errs, registered
   in Checker.call_errors for ownership) that the walk reports where the call is: the report doesn't depend on where
   the resolution happened (inside another call's inference, an overload round).
5. Report-once flags (KEY_QUICK_CALLEE_CHECKED, KEY_ITERATOR_REPORTED, KEY_CHECKED_ONCE, awaited_reported...) are per
   checker, so each checker reports once and 2. collapses them.
The residual K-dependence (kdiff.py) is where the *content or location* depends on the first asker (TS2589 at the
current node, cycle membership decided by the entry point), shared with tsgo.

What breaks if worker A computes a declaration's type that B's file "should" report:
- Plain deep diagnostics (location = the declaration, context-free text): nothing breaks. A reports into its errs, B
  finds the memo and reports nothing, the merge has it once. Mechanism 1 becomes "the publisher reports" instead of
  "everyone reports".
- **Phase**: if A computes it outside ERROR_PHASE_TYPECHECK (the harness prints types with errs = walk_errs; anything
  after program_typecheck reassigns errs to the program's), the report is dropped and the published memo hides it from
  B forever. Today the frozen guard (log_error on a report into the program's ErrorCtx) catches it at K=1 only because
  the same checker checked first. Rule: publish only from a worker in the typecheck phase, or keep a memo's diagnostics
  as data.
- **Location from context** (ty_error_at_current_node: TS2589, TS2321, TS2859; eager probe runaways): the location is
  A's current node, possibly in A's file; B's walk, which at K=1 might have been the one to hit it, reports nothing.
  Today: K-dependent; with sharing: schedule-dependent (run to run). The cut-short result itself (errorType) would also be
  published. Rule: a result whose computation moved instantiation_limit_hits / relation_*_hits is tainted, stays private
  (instantiate_conditional() already does this per epoch).
- **Cycle outcomes**: which declarations end up TY_CIRCULAR (and report TS7022/TS2502/TS7023/TS2456/TS2313) depends on
  where the cycle was entered. Today each checker enters from its own file order; with sharing A's entry point decides
  for B and, worse, A's in-flight memo seen by B looks like a cycle that doesn't exist on B's stack. Rule: in-flight is
  per worker (B computes privately or waits); results that were part of a cycle (ty_resolving_pop() returned circular,
  or that read a TY_CIRCULAR) stay private.
- **Pending diagnostics in memos** (FunctionReturn.promise_missing cleared by the first non-speculative reader,
  KEY_ITERATOR_REPORTED-style): with a shared memo the first reader anywhere reports; fine by mechanism 2 as long as the
  clear and the report are atomic and the reader is in the typecheck phase.
- **Contextual first-check decisions** stay per worker (KEY_CONTEXT_CHECKED etc.), but a shared declaration type derived
  through them (`export const h: H = (x) => ...`'s binding type, a function declaration's inferred return type, which
  checks its return expressions and resolves their calls) was derived by A's expression memos, while B's walk of the
  same body re-derives them in its scratch. Today one checker is self-consistent (the walk reuses the memos the
  declaration computation filled). With sharing, a declaration type and the expression types of its own initializer
  can come from two different derivations; if any of them is order-dependent, B can report against a type A computed
  differently. This is the new class of divergence the prototype must measure (kdiff/checkerdiff byte-identical gate).
- once_key is unaffected (it is a per-diagnostic rule, partition-independent by construction).

The general fix the code already points at: **diagnostics as data on the memo** (like CallResolutionRare.errs and the
*_WIDENED_ANY keys), replayed by the owning file's walk; then the publisher's identity can't matter. Candidates to
convert: TS7022 in lower.c (1617, 1703), TS2705/TS2468, TS2456, TS2313, report_iterator_methods.

### 5. Table sizes on drizzle (existing instrument)

No env var prints per-table entry counts. `tttt --time` prints per checker the arena size, type count and relation cache
sizes. The existing compile-time instrument scripts/arenasites.sh (a -DTTTT_ARENA_SITES build in
/tmp/tttt-arenasites, counts every intern_put() by key word 0 & 255) gives entries per KEY_ kind for `types` and
`links` (and, untagged, `intersection_props`); it does **not** count node_links, atoms or LazySlots memos. Ran on
drizzle (`-p .../drizzle-orm/tsconfig.corpus.json --noEmit`, `--checkers 1 --threads 1`; the checker recycled once at
the default 256 MB budget, so counts are cumulative over 2 incarnations; output kept in
/tmp/stage4/arenasites_drizzle.txt):

`checker 0: 1492 ms, arena 114 MB, 171853 types` (last incarnation); relation caches assignable 83505, subtype 1975,
strict subtype 84, identity 1931.

| kind | entries | key MB | class |
|---|---|---|---|
| KEY_ANONYMOUS_INSTANCE | 228,832 | 11.4 | SHARE |
| KEY_CONDITIONAL_INSTANCE | 172,898 | 6.0 | HARD (only cut-short entries) |
| KEY_PROP_MAPPED_TYPE | 121,246 | 2.8 | SHARE (side table) |
| KEY_CONDITIONAL | 64,712 | 5.6 | SHARE |
| KEY_SIMPLIFIED | 54,979 | 1.7 | HARD (in-flight placeholder) |
| KEY_REFERENCE | 52,810 | 2.8 | HARD (from_type_node flag) |
| KEY_INDEXED | 49,158 | 1.5 | SHARE |
| KEY_INTERSECTION_OF | 46,694 | 1.8 | SHARE |
| KEY_INTERSECTION | 44,979 | 2.4 | SHARE |
| KEY_UNION | 44,236 | 3.6 | SHARE |
| KEY_UNION_PAIR | 25,680 | 0.8 | SHARE |
| KEY_SIGNATURE_INSTANCE | 16,630 | 0.6 | SHARE |
| KEY_UNION_NODE | 13,277 | 0.3 | SHARE |
| KEY_TYPE_VARIABLE_REDUCTION | 12,173 | 0.4 | HARD (in-flight subkey) |
| KEY_DEFINITELY_RELATED | 11,444 | 0.4 | SHARE |
| KEY_DEFERRED_REFERENCE | 10,357 | 0.6 | SHARE |
| KEY_KEYOF_PROPERTIES / KEYOF | 9,895 / 8,910 | 0.4 | SHARE |
| KEY_GENERIC_RELATION | 8,097 | 0.8 | SCRATCH |
| everything else | < 8k each | | |
| intersection_props (untagged, spread over all 256 bins at ~990 each) | ~250k | ~6 | SHARE (taint) |

Totals by class (types+links, small kinds corrected for the ~990/bin intersection_props noise): SHARE ~727k entries /
34.5 MB of keys, HARD ~292k / 11 MB (of which 173k are conditional instances that are HARD only when cut short and 53k
references that are HARD only by one flag), SCRATCH ~14k / 1.1 MB. I.e. in `types`/`links` almost everything is
shareable in kind; the per-expression state lives in LazySlots and node_links, which this instrument doesn't see
(no instrument exists; not built, per the brief).

### 6. What the census implies for the design

Today already tolerates arbitrary checker histories: check_share() steals files by timing (program.c: "which ones it
checks also on timing"), recycling restarts checkers at arbitrary points, and checkerdiff/threadcheck/kdiff hold the
output byte-identical (residuals shared with tsgo). So "first computed by a checker that had seen other files first"
is already validated for every per-checker cache. What sharing adds is new:
1. **Concurrency**: another thread's in-flight state becomes visible (LazyMemo started, KEY_SIMPLIFIED's placeholder,
   KEY_BASE_CONSTRAINT/VARIANCES/FUNCTION_RETURN/SymbolLinks in-flight, primitive_constraint_of()'s `1`, every Ty
   LazyState). Waiting on another worker's in-flight value can deadlock exactly where the type graph has a real cycle
   (A holds X needing Y, B holds Y needing X); treating it as a cycle invents TS7022s. So: in-flight is per worker; a
   worker meeting another's in-flight entry computes privately (duplicate) and publish-if-absent decides.
2. **Finer mixing**: a single computation can now consume sub-results derived under other workers' contexts.

Proposed publish rule (to be priced by the design session): a result is published to the shared graph only if its
computation was *clean*: no ty_resolving cycle popped through it (ty_resolving_pop() false), it read no TY_CIRCULAR /
in-flight placeholder / private value, `instantiation_limit_hits`, `relation_complexity_hits`, `relation_depth_hits`
didn't move, `flow_uncached` was 0, no inferential/argument-return context it depends on, and the worker is in the
typecheck phase. Otherwise it goes into the worker's private overlay (today's tables, recycled as today). The
existing "pinned"/"cut short"/"depth 0 only" special cases become instances of this one taint bit carried on the
resolving stack. Publication is publish-if-absent (CAS on the slot); a loser discards its value (its diagnostics are
already in its errs, collapsed by the merge).

### Summary table

| | SHARE | SCRATCH | HARD | notes |
|---|---|---|---|---|
| KEY_* kinds (143) | 85 | 40 | 16 | +2 dead |
| of which node_links kinds (23) | 7 | 13 | 3 | SYMBOL_LINKS, BASE_CONSTRAINT, FUNCTION_RETURN are HARD |
| other Checker fields (~110 incl. stacks, flags, stats) | ~20 (intrinsics, global/lib types, atoms, tuple_params, id counter, intern tables' SHARE part) | ~85 (walk, flow, inference, limits, relations, stacks, stats) | ~5 (creating_from_type_node via KEY_REFERENCE, held/cross deferred-body lifetimes, report-once TS2318 flags if shared, variance_stack/resolving as the in-flight carriers) | |
| LazyType tags (5 + name_slot) | CLASS_ITEM_FIELD/ACCESSOR, module-level INITIALIZED_BINDING, name_slot | EXPR, FOR_EACH_LOOP_VAR, body-local and contextual INITIALIZED_BINDING | all declaration tags (cycle state in the memo) | memo resolves ids through a per-checker id table: needs a shared one |
| drizzle entries (types+links) | ~727k (+~250k intersection_props) | ~14k | ~292k (mostly conditional instances, HARD only when cut short) | node_links, atoms, LazySlots: no instrument |

Top risks:
1. **In-flight state is stored in the memos themselves** (LazyMemo flags, KEY_SIMPLIFIED placeholder,
   KEY_BASE_CONSTRAINT sentinels, KEY_TYPE_VARIABLE_REDUCTION `1`, Variances IN_FLIGHT, FunctionReturn, SymbolLinks,
   every Ty LazyState incl. members). All must move to per-worker records before anything is shared; this touches the
   cycle semantics that took many sessions to match tsc (barriers, reevaluation, early members).
2. **Cycle and limit outcomes are entry-point dependent**: shared, they become schedule-dependent (nondeterministic
   output run to run, not just K-dependent). Needs the taint/private-overlay rule; measure how much it leaves
   unshared (if cycles/limits touch the hot generics, the win shrinks: drizzle hits TS2589-class limits).
3. **Deep diagnostics' exactly-once now rests on the publisher**: phase gating (ty_error_at drops outside the typecheck
   phase) and current-node locations break it. Convert the stored-outcome diagnostics to data on memos.
4. **Lifetimes**: shared types point into deferred-body materializations (KEY_NODE, function types, FunctionImpl keys),
   Props/Members side tables (PROP_MAPPED_TYPE etc. must publish with their owners), and the LazySlots id table; no
   recycling of the shared part means pinning materializations (vscode re-materializes 19% today) and an unbounded
   shared arena.
5. **Id order and first-creator facts**: gate conditions 1 and 2 (union order ties, is_deeply_nested_as "newer" by id)
   plus first-creator display facts: Ty.reference.from_type_node, KEY_INSTANTIATION_QUERY, KEY_UNDEFINED_PROPERTY,
   KEY_EXTENDED_CONTAINERS, global/lib types made on first use (resolve them before workers start).
6. **Declaration vs body consistency**: a shared declaration type (inferred return, initializer type) derived from A's
   expression memos while B's walk re-derives the same expressions in scratch: new mixed-derivation divergences that
   no per-checker test exercises today; kdiff/checkerdiff at K12 are the gate.
7. Cost side: memo reads go through a shared id -> Ty table (today a hot per-checker table), and every intern lookup
   becomes a concurrent-table probe; LazySlots and node_links volume (where the expression state lives) is unmeasured.

## Part C: bun check (oven-sh/bun#45800): how checker threads share work

Sources actually read: the PR description (GitHub API, `/tmp/stage4/body.md`); the files list (418 files, +44,911 lines);
and these files at the merge commit `6bb5dc1956505dd1113455f58d4757c924f3ad54` (copies in `/tmp/stage4/src/`):
`src/sema/check/commit.rs` (new, 817 lines, the design document for the commits), `src/sema/table.rs`,
`src/sema/local.rs`, `src/sema/types.rs`, `src/sema/check/task.rs`, `src/sema/check/mod.rs` (the `Program` tables),
`src/sema/check/relate.rs` (isDeeplyNestedType), `src/sema/check/links.rs`, `signature_links.rs`, `check_order.rs`,
`sink.rs`, `infer_memo.rs`, `src/sema/driver/lib.rs` (the scheduler), `src/sema/util.rs` and `util/memory.rs`
(the concurrent containers). `gh` isn't installed, so everything came from api.github.com and raw.githubusercontent.com.
I say so where something is my inference.

Short version: bun shares **the whole type graph**. One global interner holds every published type, signature, mapper
and component list, keyed by content. Tasks create into it directly with global ids from per-thread id ranges.
Records a task must not share stay task-local under a `LOCAL` id bit. Every tsgo "link" (per-node, per-symbol and
per-type results, relations, instantiations, members, constraints...) lives in a shared table with a task-local
buffer in front of it. A task's buffered entries reach the shared tables in **commits**: one global mutex, numbered,
first entry per key wins. A large body of rules (snapshots, invalidation, rechecks, a serial "last round") keeps the
output equal to tsgo `--singleThreaded` in program order. Union order and isDeeplyNestedType's id test are replaced by
a content-based order, which costs some tsc-compatibility in edge cases.

### 1. What is shared, and how types are identified

**Type records: one shared hash-consed store.** `types.rs` header: "Types, signatures and type mappers, hash-consed:
equal ones have equal ids." `TypeStore` is "the shared store: the published types, signatures, type mappers and lists
of index components. A lookup is lock-free and writes nothing. Tasks add records at any time." Its rules (doc of
`TypeStore`): "A record consists of its interning key and flags computed from the key, so it does not depend on which
task created it. Anything that changes the behaviour of a type is in its interning key, or is computed from the key."
A type is like tttt's: an origin (declaration, node, mapper), not its members. Members are resolved on demand into a
table (see 3).

**Ids are global, with no translation.** `TypeId(u32)`. A published record gets its id when it is created
(`Interned::reserve`/`place`). A task reserves a **range of ids under a lock** (`IDS_RESERVED_AT_A_TIME`: 1024 types,
256 sigs, 512 mappers, 32 component lists; type ranges also split into 6 pools by "class": type params, unions,
intersections, references..., so tables keyed by type get pages from a single thread and a single sort of type). The
`Interned` doc says: "An id is final as soon as it's assigned, and doesn't reflect when its record was created. So
nothing may depend on id order, which is why types are ordered by `Types::content_order`." `TypeId` deliberately has
no `Ord`. `arrival_order()` is the only way to get a number, and it is for set ops (sort/dedup/binary search).

**Task-local records.** `local.rs`: "A type, signature, mapper, component list, atom or handle that a task has created
has `LOCAL` set in its id [bit 30], and the rest of the id counts from 0 within the task." A task that publishes
(`Sharing::Publishes`, used by committing tasks per `commit.rs` "The records are shared already") publishes every
record **at creation**, unless the record is **bound**: it mentions a node or symbol of a file that nothing imports
(`OwnStore::add_unimported_file`: "From now on every record that references `file` is bound"). Bound records are
never published: no other task can ever ask for them. Tables reject `LOCAL` keys from the shared part (`MaybeLocal`).
`intern_record` searches only the index that can hold the key: the own index if bound, else the published one.

**Race on publishing.** `Interned::place`: "The index lookup and insert run under the shard's lock, so if two threads
race on the same key, one adds its id and the other thread finds it. The losing thread's record stays in its slot,
and nothing refers to it." So both may build the record, first publish wins, and the loser's id slot is a dead hole.
The index is `GrowingPlaces` (64 shards): lock-free lookup, insert locks one shard. On resize, old slot arrays stay
allocated until `forget_older`, because a reader may still use them (`Newest`, util/memory.rs).

**Facts on types.** `Summary` (flags, member_flags, object flags, facts bits, `Rank`) sits beside each published type
in a parallel atomic array. "Facts learned about a task-local type aren't published with it." A fact's known bit and
value go in one store (`mod fact`). Only facts that are "a function of id's interning key and the program" go there.

**Everything else is in `Program` tables** (check/mod.rs ~lines 230-490), nearly all `Buffered`: `expr_types`,
`symbol_types`, `declared_types`, `fn_return_types`, `shapes` and `members` (resolved members by TypeId),
`resolved_return_types`, `instantiations: ByKey<(TypeId, MapperId), TypeId>`, `composed` mappers, `relations:
ByKey<relate::Key, u8>`, `constraints`, `type_param_constraints`, `union_properties`, `conditionals`, `awaited_types`,
all the `circular_*` markers, and so on. **Relation caches are shared** like everything else (with exceptions, see 3).
Variances go in a separate `serial_variances` map with claims (see 2).

Table shapes (`table.rs`): `ByNode`/`ById` are dense arrays of atomic cells indexed by node number (all files
concatenated, `Bases`) or by type id, zero meaning "not computed", "written once. A read is one load". Arrays come from
zeroed OS pages, so untouched pages are never resident. `ByKey` is a sharded hash map (`PUBLISHED_SHARDS` = 64).
Policies: `Frozen` (CAS, first value wins: loader and type store), `Buffered` (task-local half plus shared half, filled
by commits), `FileLocal` (task-local, dropped at the end of the file, "only a pure cache... a function of entries that
are published").

**Per-checker caches that are not shared.** `links.rs`/`signature_links.rs` cache derived parts of a conditional,
mapped or signature (checkType, templateType, resolvedReturnType...) per checker: "None of it is shared, and no id in
it outlives the task". They are only cached when the underlying table entry is final. `relations_of_this_task` and
`constraints_of_this_task` hold depth-dependent results (see 3).

### 2. "Small commits"

Read in `commit.rs` (header and `Commits::store`, `Checker::commit`, `rest`):

- **What.** A task writes its results into its own half of each `Buffered` table, and the key goes to a `Log`. At a
  commit, "on its own thread it stores what it has logged since its last commit in the shared part". Records need no
  rewriting because they were published at creation.
- **When.** "After every file, and from time to time within a file while nothing is in progress in it": at
  `rest_between_statements` (query stack, contextual stack and inference contexts empty, not measuring variances) once
  `KEYS_PER_COMMIT = 1024` keys are logged. "The number changes how long a check takes, never its result."
- **How.** "One commit at a time. They store under one mutex and are numbered from 1 in that order. The first entry for
  a key stays." `store` sets `begun = n`, stores the record-keyed entries in log order "each with a release", then the
  node- and symbol-keyed entries with commit number `n` in a parallel `stamps` array, then sets `stored = n`. Only then
  do entries stamped `n` count for anyone. So it is a single-writer publish with atomic cells and a global commit
  counter: no RCU or epochs for reclamation, but **per-file snapshot visibility by commit number**:
  - "A file does not change under a statement": a task notes the last fully stored commit the first time it looks at
    a file (`View::snapshot_of`) and ignores higher-stamped node entries of that file until its next rest.
  - "The file that a task visits does not change during the visit" (`View::pin`).
  - Record-keyed entries (by type id etc.) have no snapshot: they are visible as soon as stored. That is why ordering
    within a commit matters: "whoever finds an entry finds what was stored before it".
- **Writing at once.** An entry under a record skips the commit and goes straight to the shared table
  (`Task::may_write_at_once`, `ByKey::put_at_once` uses `add_if_absent`) when neither key nor value is bound, and
  nothing since its evaluation began was held back (`Task::hold_back` counts reads that "hold only while the stack of
  the checker is as it is"). This stops once anything happens that may get the task thrown away (until the next
  commit). Some tables (`tables_written_at_once`) are written at once.
- **Concurrent duplicate work.** For tables, both tasks compute and the **first committed entry wins**. The later
  commit's entry is dropped (`store_in_commit` "unless there is an entry"). Diagnostics follow their query's entry:
  "If the entry loses, so does the diagnostic" (`drop_diagnostics_of_losers`, sink.rs: the first task that commits a
  diagnostic under the query reports). For **variances only**, there is waiting: `Claims` (a mutex-protected map plus a
  condvar). The first thread claims a symbol and measures, and others wait and take the result. The result is shared
  at once at the end of the measurement, not at commit, because "its result is a few flags... no id is in it". A
  failed claim lets another thread try once more, then everyone measures for themselves. The doc includes a deadlock
  argument.
- **Validation.** A commit can be **invalid** (`Program::is_invalid`): its order-dependent variances conflict with the
  ones in `serial_variances`, or its `markNodeAssignments` walks cross another task's unseen walk. Then everything
  since the last commit is thrown away, and the file is rechecked at once on any thread.

### 3. Lazy state, in-flight work, circularity

- Lazily resolved state is ordinary `Buffered` table entries: `shapes`/`members` by TypeId, `constraints`,
  `type_param_constraints`, `resolved_return_types`, `instantiations`, `declared_types`... An in-flight computation is
  **never visible to another thread**. The query stack (`Checker::stack`, `Provisional` results valid while a frame
  has a serial) is per task. So two threads can resolve the same members concurrently, and the first commit wins.
  There is no cross-thread in-flight marker except the variance claims.
- **Circularity across workers** is the main complication. tsc's answer depends on where a cycle is entered. bun
  believes a task about a "ring" (a cycle that closed) only if every earlier file in program order is "checked for
  good" (`Task::close_ring`), or, with `is_reachable_only_by_importers`, the earlier files of its own import cycle.
  Otherwise the task is **discarded** at its next statement, and the driver rechecks the file when a commit resolves
  that cycle (`Program::is_cycle_resolved`, the `Ring` records with sorted member-query hashes) or when the earlier
  files are done. "A ring is seen whole or not at all": a task that missed one member of a committed ring loses its
  work (`lose_what_has_missed_a_ring`). Once anything is discarded, one worker switches from heavy files to the first
  unchecked files, so cycles everything uses get resolved early.
- **Depth/limit dependence stays task-local**: "a base constraint that was cut short, a comparison that holds because
  two types inside it counted as deeply nested, and what follows from either (`constraints_of_this_task`,
  `relations_of_this_task`)". In relate.rs, a comparison that hit a deeply-nested cut sets
  `keeps_relations_for_this_task`. "A task that gives up at a limit is not believed" (`note_gave_up`): it is discarded,
  and its file waits for the last round.
- **Last round.** "One task that checks the files that are still discarded, in program order, while nothing else
  runs... It commits once, when it ends." This is the serial fallback that bounds how wrong the parallel phase may be.

### 4. Determinism, union order, isDeeplyNestedType

- **Goal**: "the output doesn't depend on thread timing" (PR title section). All the rules above exist "to keep the
  report a function of the program". One thread checks in program order, like `tsc --singleThreaded`. On several
  threads, where tsc's own output depends on file order, bun "can print any of the answers tsc gives for those files"
  (example in the PR: 8 or 14 errors at 8 threads). Known remaining nondeterminism: import cycles where the caller
  comes before the erroring file, and about 1 run in 400 missing a group of TS7031 on a 24,800-error JS project.
- **Union order and every id tie-break**: `Types::content_order` replaces id comparison in "the final tie-break of
  `CompareTypes`, and the swap of an identity comparison in `getRelationKey`". It uses a per-type `Rank` computed at
  creation from the interning key: `height` (1 + the max height of the types it mentions, so a type sorts after its
  parts), `place` (the last file/node it mentions, a proxy for "tsgo created it later"), and `content` (a hash with
  every id replaced by its content). Then `compare_contents` does a structural walk, and ids break any remaining tie
  "if [equal keys] do [reach this point]". The PR lists where this follows 7.1's content-ordering (bigint literals by
  value, mapped instantiations by type args, reverse mapped types...). Also "Unions made from other unions merge their
  sorted members instead of sorting again".
- **isDeeplyNestedType** (`relate.rs is_deeply_nested_type`/`is_created_no_earlier`, `types.rs nesting_order`):
  tsgo's `t.id >= last.id` becomes **height** comparison, and at equal height **push order on the relater's
  source/target stacks** ("tsgo creates an instantiation when a comparison first reaches it, and the target's before
  the source's, so the type that was pushed first is the older one"). Hash comparison was rejected: it "would count
  only about half of them". `links.rs`/relate.rs also have special cases (`holds_object_literals`) where lazy member
  resolution changes which type is "newer".
- **The known cost** (PR "known issues"): generated recursive generic aliases stepping through literal rings get a
  **false TS2322** (108 of 312 generated variants that tsc accepts), and **a wrong assignment can be accepted**, both
  because "tsc cuts such a comparison off depending on which instantiation it created first. Types are ordered by
  content here, so that order isn't known. A first fix was reverted: it made correct code ... fail with TS2321 or
  TS2589, up to 1,000 times slower. Seen in generated programs only."
- **Diagnostics**: each belongs to the task or to a query entry. Commits move them to per-file buffers (`Sink`), and
  `Program::finish_file` reads them per file after the last task ends (sink.rs header). Once-per-file errors (TS2879,
  TS2354, TS2343, TS2807) are placed by a syntax walk that replays checkSourceFile's order (`check_order.rs`) instead of
  where a thread first asked. "A task only reports in its own file and in later files"
  (`is_left_to_an_earlier_file`), and `View::hides` ignores later files' entries in a file where an error was found.
  Mutex-protected unions (`properties_referenced_before`) handle order-independent sets.

### 5. Memory

- `Session` arena: "Nothing frees it before the session ends" (`impl Alloc for &Session`). Published records, their
  lists and the shared tables live there, and the shared graph is never freed during a check. The record lists are
  allocated "in the arena of the thread that built it", and an `ArenaBox` "frees the block when it is dropped, on any
  thread".
- Task-local state (`OwnStore`, table buffers, `FileLocal` tables, per-checker links) is "on the regular heap, and the
  thread that runs the task frees it when the task ends" (local.rs). `Task::begin`/`drop_everything` clear it.
  **Checkers are recycled**: "A checker is replaced after it has checked 1/MIN_TASKS [128] of the total cost, because
  its private tables grow with every file" (driver/lib.rs).
- Losing records from publish races stay as dead slots. Unused reserved id ranges are returned to `spare_ids` at task
  end (`take_back_ids`).
- PR text: "Tuples that differ only in their labels share their members, which was 61% of all stored properties on
  vscode". Compact AST (20-byte expression, 24-byte symbol, 4-byte parent link). "Lookups about the file being checked
  use a private array instead of the shared hash tables" (`KEEPS_THE_VISITED_FILE`: expression types of the visited
  file never leave the task).

### 6. Work distribution (driver/lib.rs around lines 4200-4330)

- Cost per file = identifiers in expressions + type nodes + 1 ("text size is a poor predictor: on storybook... 1.8
  times as long... this estimate... 1.1").
- One queue, no barriers. **Heavy files** (cost >= total/128, when workers > 1) go first, largest first, one per run.
  Then light files in program order, in **runs of consecutive files** sized `cost_left / (workers * 4)`, which shrink
  toward the end ("threads that work far apart ask for different types at any one time"). There is no work stealing
  beyond the shared queue.
- One worker can take light files first (`skips_heavy_files`) so that early files get "checked for good" and
  discarded files can be rechecked. The rechecks of discarded files go first. Then comes the serial last round.
- Project references: idle threads join projects still being built.

### 7. Numbers and limitations (PR text)

- 8 threads, cold, against 1.4.3: mui 10.28 -> 1.31 s (11.2 -> 1.3 GB), supabase 2.84 -> 1.27 s (1.0 -> 1.2 GB), t3code
  4.59 -> 1.62 s, mikro-orm 3.30 -> 1.82 s (0.9 -> 1.1 GB), vscode 4.09 -> 2.82 s (1.6 -> 1.9 GB). Memory went up on 3 of
  5. 1.4.3 had "steps" with all-thread barriers and merges, about 8,000 lines deleted.
- Regressions: bun's own `src/js` is slower on threads (8 threads 0.11 -> 0.20 s), because `interface Array<T> extends
  ClassWithIntrinsics<Array<T>>` puts most files into a cycle and serializes "a chain of 27 files". Adversarial
  projects make rechecks cost 2.1-2.8x wall (3-5x CPU). One of seven projects uses 15-26% more peak memory.
- Correctness: the false TS2322 / wrongly accepted assignments above (content order instead of id order). A lost
  TS2321 when two files overflow. Run-to-run variance in import cycles. A rare missing TS7031 group.
- Our own measurements (agents/measurements.md, 2026-10-19 canary) show canary's total CPU at default threads is 1.6-3.0x
  its single-thread time (vscode 42.95 CPU s against 14.08 s single, drizzle 4.21 against 1.60, TypeScript6 1.54 against
  0.98). So parallel overhead is large (duplicate
  work and rechecks), even though wall time is good. My inference: much of that CPU is spent on the duplicated
  "both compute, first commit wins" work and on discarded tasks.

### What tttt might take from this (my inference)

1. Publish-at-creation, content-keyed records with **per-thread id ranges** (one reservation lock, ranges per type
   class) and a local bit for records that must not escape. This gives one interner with no id translation.
2. Remove id order from semantics before sharing (a content `Rank`: height/place/hash then structural compare). Expect
   to lose tsgo parity on isDeeplyNestedType edge cases, which bun accepted ("generated programs only").
3. Separate **publish-once records** (lock-free reads, sharded locked inserts, first wins) from **link/cache entries**
   (task-buffered, committed under one mutex with commit-number stamps for node-keyed entries). Keep depth- and
   limit-dependent results task-local.
4. Determinism comes from a large set of program-order rules plus discard/recheck and a serial last round. That is
   the most complex part (commit.rs's header alone is about 85 lines of rules), and it is where bun still has known bugs. A
   design that only shares **order-independent** results would need much less of this.
