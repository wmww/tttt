# Plan 02 - type core redesign

Outcome of the planning session for brief 01 (2026-09-18, on top of 6805d87). Verdict: **the assessment in
`vision.md` is confirmed, and it's worse than the skim suggested.** Replace the type representation, staged in place,
keeping the suite runnable after every session.

## 1. Evidence

Read: `type.h/.c`, `several_types.*`, `generic.c`, `lazy_type.*`, `type_pair_cache.h`, `declaration.h`, `interface*.h`,
`callable_type.h`, the stdlib sharing in `scope.c`/`baseline_test_case.c`. Measured with callgrind (no `perf` on this
box) and small `./filetest.sh` probes (kept nowhere; they're four lines each and reproduced below).

- **No identity, so everything is a deep copy.** `Type` is a by-value tagged tree. `identifier_type_evaluate_type` →
  `generic_degenericize_type` clones the *whole declared type* of a generic and substitutes into the clone
  (`replace_named_types_with`), per reference, uncached (`identifier_type_clone` even throws away the memo). 112
  `type_clone` / 157 `type_drop` / 22 `type_refcopy` call sites; ownership is a manual discipline ("sometimes owned by
  the AST", `TYPE_REF`, `owned_*` vs `*_ref` field pairs in 5 structs).
- **The relation cache doesn't cache.** `TypeCtx` (which owns `extends_results`) is created and destroyed *per
  top-level `type_extends` call*. So the string-keyed cache only breaks cycles within one query; nothing is remembered
  between two assignments of the same types. Its key is a full rendering of both types, built at every cached level.
- **Recursive generics explode.** `varianceProblingAndZeroOrderIndexSignatureRelationsAlign` (30 lines, tsc/tsgo: not
  measurable above startup): 6.2 G instructions, 0.68 s, 222 MB, 15 364 depth-limit hits. Inclusive: 58% instantiating
  (`lazy_type_evaluate` → clone + substitute), 85% inside `display_callable` (cache keys), ~50% dropping what was just
  cloned. Every structural level instantiates fresh, bigger trees, and no two of them are ever recognised as the same
  type, so the only terminator is the depth limit (which answers "assignable").
- **Even on the easy bench corpus, relations are half the checker**: the non-recursive entry `type_extends` is 11.3%
  of all instructions inclusive, `program_typecheck` 22% (callgrind's inclusive numbers for the recursive
  `type_extends_internal` are inflated, ignore them). malloc/free/memcpy are ~25% self time across the run;
  `program_destroy` is 11.7%. (Parse 34%, resolve names 32% - half of that in `hmap_arc_declaration` lookups, see
  triage.)
- **Unions are quadratic**: `type_composite` runs pairwise `type_extends` (each with a fresh `TypeCtx`) per added
  member. A 1500-literal union assigned three times costs 0.16 s. No dedup by identity, no literal fast path.
- **Printing loses what tsc keeps.** `a = b` with `b: List<string>` reports `Type '{ head: string; tail: ... }'` where
  tsc says `Type 'List<string>'`; `f(al)` with `al: Alias` (`type Alias = string | number`) prints `string | number`
  where tsc prints `Alias`; `type Alias = ...` line prints the structure instead of `Alias`. All three need identity
  (reference = target + args; alias symbol carried on the type). No error elaboration chain either (`Type 'string' is
  not assignable to type 'number'.` second line) - that needs the relation to be re-runnable in a reporting mode.
- **Checker state lives in the AST, and the lib AST is shared between test programs.** `LazyType` memos are embedded
  in AST nodes; the harness parses+binds each lib set once (`baseline_test_cache_get_program_with_libs`) and shares its
  declarations copy-on-write with all ~11k test programs. Works today only because evaluated types are self-contained
  heap trees owned by the memo. Any arena/interned design breaks this unless memos move out of the AST - which is
  also exactly what multiple checkers over one AST (threading) need. This was not in the brief and shapes the plan.

Things that are fine and stay: the phase structure, LazyType's *mechanism* (memo + in-flight cycle detection,
dispatch by tag + `container_of`), `Value` for literals, `Declaration` for now (plan 03 revisits symbols).

## 2. Design

Two separate things that are one struct today:

- **`TypeNode`** - syntax. What the parser produces for a type annotation; owned by the AST, immutable after name
  resolution, never cloned, never created by the checker. This is today's `Type` struct minus `TYPE_LAZY`/`TYPE_REF`
  and minus every `has_cache/cached` field. (It keeps the name `Type` until the final stage, see migration.)
- **`Ty`** - semantic type (rename to `Type` at the very end). Immutable once published, arena-allocated, owned by a
  `Checker`, referred to by `Ty const*`, carries a `u32 id` assigned in creation order. Pointer equality *is* type
  identity. Pointers rather than bare 32-bit ids: simpler C, debuggable, no table indirection on the hot path; the id
  is for ordering, cache keys and hashing. (If memory ever matters, handles can come later; nothing here depends on it.)

### Checker
```c
typedef struct Checker {
    Arena arena;              // all Ty, Signature, Prop, mapper, member-table memory. Freed wholesale.
    u32 next_type_id;
    Ty const* intrinsics[...];// any, unknown, never, void, undefined, null, string, number, bigint, symbol, object,
                              // true, false, boolean (= union true|false, like tsc), error-any, silent-never...
    // intern tables (open addressing, key = small struct of ids, value = Ty*)
    InternTable literals;     // (kind, Value) -> Ty
    InternTable unions, intersections;     // sorted id list -> Ty
    InternTable references;   // (target id, arg ids) -> Ty     (interfaces, classes, Array, tuples)
    InternTable aliases;      // (alias decl, arg ids) -> Ty    (instantiated type aliases)
    InternTable deferred;     // (kind, operand ids, flags) -> Ty  keyof / T[K] / conditional(root,mapper) / template
    InternTable tuple_targets;// (arity, per-element flags, readonly, names?) -> generic tuple target
    // side tables ("links"): ALL checker state that is about an AST node or Declaration lives here, not in the AST
    LazySlots lazy;           // per source file: slot index -> {state, Ty const*}
    HMapDeclLinks decl_links; // Declaration const* -> {declared_type, value_type, type params, variances, ...}
    // relation state
    RelationCache assignable, comparable, subtype, strict_subtype, identity; // u64 (src id << 32 | tgt id) -> result
    ... instantiation depth/count, error reporting state
} Checker;
```
One checker per `Program` for now. New-core functions take `Checker* me` explicitly. Legacy entry points that have
no context parameter (`type_extends(a, b)` and friends, 58 call sites) get it from a `thread_local Checker*
current_checker` set by `program_typecheck`; record that in `hacks.md` and shrink it as signatures get ported.

**LazyType becomes a slot.** `LazyType` in the AST shrinks to `{tag, u32 slot}`; the parser/binder numbers slots per
`SourceFile`. `lazy_type_get(checker, lazy)` indexes `checker->lazy[file][slot]` → `{not started | in flight | in
flight, self referential | done, Ty const*}`. The cycle-detection logic is unchanged. Slot storage for a file is
allocated on first touch (the DOM lib is big and most tests touch none of it). This makes the AST read-only during
checking, fixes lib sharing between tests by construction (each test's checker re-derives the few lib types it
touches; type ids therefore don't depend on which tests ran before), and is the threading story (§4).

### Ty
```c
struct Ty {
    u32 id; u16 kind; u16 /*spare*/; u32 flags;   // flags: tsc-like TypeFlags + ObjectFlags-ish cached facts, most
                                                  // importantly COULD_CONTAIN_TYPE_VARIABLES and IS_FRESH_LITERAL
    Declaration const* alias; TyList alias_args;  // how to print it if it came from an alias (tsc aliasSymbol)
    union { ...per kind... };
};
```
Kinds and their identity:
| kind | identity / interning | notes |
|---|---|---|
| intrinsic | singleton per checker | `boolean` is `true \| false` union with an intrinsic-looking print, as tsc |
| literal (string/number/bigint/bool, enum member) | by `Value` (+ enum member decl) | fresh vs regular literal: two Ty per value linked to each other, as tsc (needed for widening; can start with regular only) |
| unique symbol | by declaration | |
| type parameter | by declaration (`GenericItem`/`infer`/mapped key), plus synthesized ones (`this`, markers) | constraint/default resolved lazily via decl links |
| declared object (interface / class instance / class static side / enum object / namespace value) | by `Declaration` (merged decls = one type) | members resolved lazily: props, call/construct signatures, index infos, base types. Generic ones own `type_params` and a `this` type param, and are their own "target" |
| reference | interned by (target, arg ids) | `List<string>`, `Array<T>`, tuples (target = tuple target). Members resolved lazily = target members through a mapper. **This is what stops the recursive-generic explosion**: `List<string>` reached twice is the same pointer, so the relation cache and the in-flight ("maybe") set recognise it |
| anonymous object (type literal, object literal expr, function type/expr, mapped type, instantiated anonymous) | by AST node for the uninstantiated form (side table node → Ty); instantiated form interned by (target, ids of the mapped *outer* type params it actually uses) | members lazy. A function type is an anonymous object with one call signature, as in tsc - `TYPE_CALLABLE` vs `TYPE_INTERFACE`-with-callable-member stops being two cases everywhere |
| union / intersection | sorted by id, deduped, interned by id list | construction does the cheap reductions only (flatten, `never`/`any`/`unknown` absorption, literal-vs-primitive subsumption by flags). Subtype reduction is a separate explicit operation used where tsc uses it. Large literal unions: O(n log n) |
| keyof, indexed access, conditional, template literal, string mapping, substitution | interned by operand ids (conditional: by root node + mapper arg ids) | *deferred* only while an operand is generic; otherwise the constructor resolves eagerly and returns the result, so no `has_cache/cached` fields |

`Signature` (not a Ty): type params, params (`Prop`-like: name, type lazy, optional, rest), return type (lazy, from
annotation or body), min arg count, flags (construct, abstract), declaration, and for instantiated signatures
`target + mapper`. `Prop`: name (interned string or unique symbol Ty), flags (optional, readonly, method, accessor
bits), declarations, parent, type (lazy; for instantiated props `target + mapper`). `Prop` is deliberately the seed of
plan 03's persistent symbol; plan 03 decides whether `Declaration` and `Prop` become one thing. Member tables are
small sorted arrays or hash maps by interned name - executor's choice, measure.

### Instantiation
`TyMapper` = arena-allocated {sources[], targets[]} with simple (1 param), array, composed and merged forms (needed
for inference later). `instantiate(checker, ty, mapper)`:
- returns `ty` untouched unless `COULD_CONTAIN_TYPE_VARIABLES`;
- type parameter → mapped target; reference → re-intern with instantiated args; alias'd types keep alias + mapped
  alias args; union/intersection/deferred → rebuild through the normal constructors (which may now resolve);
- anonymous object / mapped / conditional → lazily instantiated object `{target, mapper}` interned as in the table;
- counts depth and total instantiations per top-level check; past tsc's limits (100 deep / 5M) report TS2589 and
  return error-any. That replaces the "return `true` at depth 100" hack.
No clone-and-substitute anywhere; `replace_named_types_with` and `generic_degenericize_type` are deleted.
Conditional-type `infer` results are a mapper too (today: `VecTypeRefinement` with borrowed pointers).

### Relations
`is_related(checker, source, target, relation, report?)`, structure following tsc's `isTypeRelatedTo` →
`recursiveTypeRelatedTo` → `structuredTypeRelatedTo`, because the *observable* results (which errors, which
elaboration lines, where TS2589/"excessively deep" appears) are the oracle:
- fast paths on identity and flags before any cache lookup (`isSimpleTypeRelatedTo`);
- persistent per-relation cache keyed by `(src id, tgt id)`; entries: succeeded / failed (+ reported bit). Pairs
  involving in-scope type parameters or intersection state need tsc's key postfix: widen the key to 128 bits rather
  than building strings;
- recursion: in-flight "maybe" set + source/target stacks + `is_deeply_nested` (same recursion identity ≥ 3 deep →
  assume related, *maybe* result not cached as definitive), exactly the mechanism tsc uses to check the repro instantly;
- **variances** per generic declared type / alias, computed with marker type arguments and stored in decl links;
  reference-vs-reference with the same target compares arguments by variance first. Do this in the same stage as
  references or recursive generics stay slow even with caching;
- error reporting is a second run of the same function with `report` set, producing the elaboration chain. Wire the
  parameter through from the start even if the first port only fills the top line.

### Printing
A `ty_display` that works from `Ty` only, in tsc's order of preference: alias name (+ alias args) → reference name +
args (`T[]`, `readonly T[]`, tuple syntax for Array/tuple targets) → declared name → structural. Union members print
in id order with tsc's `formatUnionTypes` rules (`null`/`undefined` last, `true|false` → `boolean`, enum member
collapsing) - read that function in `thirdparty/tsc/src/compiler/checker.ts` rather than guessing. Id order matches
tsc's only if creation order does; tsc creates intrinsics in a fixed order (copy it: see `createIntrinsicType` calls
near the top of `createTypeChecker`) and everything else lazily in check order, which the LazyType-driven design
already approximates. Expect a residue of order mismatches; they're then bugs about *when* we create a type, which is
at least fixable - today it isn't. The existing `display.c` stays for `TypeNode` (declarations, `parsed`/`underline`
modes used by the test traversal); the semantic printer needs the underline mode too (the `^^^` rows in `.types`).

### Lowering
`ty_from_node(checker, TypeNode const*)`: the only producer of `Ty` from syntax. Identifier → decl links: alias
(instantiate alias body with args, cached in `aliases`), interface/class (reference or the declared type itself),
type parameter, enum; type literal / function type → anonymous object keyed by node; the rest via constructors.
Self-referential aliases resolve through the lazy slot of the alias declaration plus deferred member resolution of
object types (object members are never resolved at construction time, that's what breaks the cycles legally;
`type T = T[]`-style illegal cycles still end at the in-flight check → TS2456).

## 3. Threading (not built now, not precluded)
- Parse: per file, no shared mutable state except slot/node counters, which are per `SourceFile`.
- Bind/resolve names: still mutates the AST (`decl_ref`) and the global scope; making it parallel is plan 03's
  problem (per-file symbol tables + a merge step). Nothing here makes it harder.
- Check: after binding the AST is read-only. All checker state is in `Checker` (arena, intern tables, caches, lazy
  slots, decl links), so N checkers over one AST (tsgo's model) is "make N `Checker`s and partition files"; no locks.
  Cost is duplicated lib types per checker, as in tsgo. Type ids are per checker, so printed union order can depend
  on file → checker assignment; tests run one checker, and a deterministic assignment keeps CLI output stable.
  A shared concurrent intern table is possible later but is not the default direction.

## 4. Migration: staged, in place, suite runnable every session

A rewrite-then-switch would leave the tree red for 3+ sessions with no oracle. Instead use one piece of throwaway
scaffolding: a legacy tag **`TYPE_NEW { Ty const* }`** in the old `Type` struct, so old code can carry new types around,
and **`ty_from_legacy(checker, Type const*)`**, which lowers *any* old tree (it's `ty_from_node` plus `TYPE_LAZY` →
slot result, `TYPE_REF` → target, `TYPE_NEW` → payload). Old APIs are then re-implemented one at a time as "lower the
arguments, call the new core, wrap the result in `TYPE_NEW`". Where old code still switches on the tag of a result,
give it a one-layer `ty_to_legacy` (children stay `TYPE_NEW`); expect to need it less than feared, and delete it at
the end. Pass counts may dip mid-migration; a stage's floor is stated below. Baseline today: **1084 full, errors 4396,
symbols 5939, types 2146**; bench 0.600 s / 790 MB; repro test 0.68 s.

Each stage: `./fasttest.sh --summary`, `./failstats.py` (watch `log_error` totals), valgrind (`./test.sh -f`) on
~50 tests touching the changed area, update `measurements.md` if numbers moved, update `hacks.md`/`documentation.md`.

**Stage 1 - DONE (2026-09-18).** `src/ty/` is ~2.2k lines; results in `measurements.md`, shortcuts in the "New type
core" section of `hacks.md`, overview in `documentation.md`. What later stages should know:
- The design held. Deviations from §2: one `InternTable` type with `u64[]` keys for everything instead of a table per
  kind; `Checker.links` is that same table rather than an `HMapDeclLinks`; no `LazySlots` yet (stage 2); conditional
  types are interned by operand ids, not root + mapper; enums are a nominal `TY_DECLARED_ENUM` placeholder; no
  fresh literal types; `TyDisplay` has no underline mode (the legacy one underlines a type's whole rendering, the
  traversal can do that from the string length).
- There is no Declaration -> AST node link in the legacy code. Stage 1 gets by with `LazyType.origin` +
  `container_of` (`legacy_entry_interface_decl`), and reads class/enum/namespace members off the legacy structural
  type. Stage 2 should give declarations a real link to their nodes when it touches `LazyType` anyway; stage 4a
  needs it for classes and enums.
- **Must do in stage 3 before relations are trusted: deferred type references.** `type Json = string | Json[]` hits
  the alias in-flight check while lowering `Json[]`, so the union's member is `any[]` (the alias prints fine, which
  hides it). tsc makes array/tuple/reference nodes inside a declaration that's being resolved *deferred*: identity by
  (node, mapper), type arguments resolved on first use. Same laziness as `Prop`, applied to reference arguments.
- Forcing the legacy checker to resolve type operators it never resolved itself is unsafe (stack overflow in
  `recursiveConditionalTypes`), so lowering only asks it for expression types. Every conditional type stays deferred
  until stage 3 gives it relations + `infer` inference; `deeplyNestedConditionalTypes` hits the instantiation depth
  limit until then.
- `intern_slot()` pointers are invalidated by the next insert into the same table; this already caused one
  use-after-free. Stage 3's relation caches should use get/put, not slots.
- Things that turned out to matter for printing parity: namespace qualification needs symbol accessibility (plan 03,
  3.5k nodes differ only by that), `{ new(): T }` parses as a method named `new` (parser bug, triage).

**Stage 1 as planned - core + lowering + printer, shadow mode (new code ~1.5k lines, nothing switched).**
`src/ty/`: arena, `Checker`, `Ty`, intern tables, intrinsics, literals, unions/intersections, type params, declared
object types + references + anonymous objects with lazy members, `Signature`, `Prop`, `TyMapper` + `instantiate`,
`ty_from_node`, `ty_display`. Decl links keyed by `Declaration*`. Hook: a `--shadow-types` flag (and harness env var)
that, after typecheck, lowers every type annotation and every declaration's declared type, prints it, and
`log_error`s when it differs from the old display of the same node for non-generic, alias-free cases. *Validate:*
suite numbers unchanged (flag off); with the flag on over the suite: no crashes, valgrind clean on a sample, mismatch
count recorded in `measurements.md` and the top causes either fixed or written down; a unit-ish probe (can be a
`filetest` with the flag) shows `List<string>` lowered twice is one id, 1500-literal union builds in < 5 ms, and
lowering everything in the repro test terminates immediately.

**Stage 2 - DONE (2026-09-18).** Pass counts identical (1084 / 4396 / 5939 / 2146), suite wall time unchanged
(8.3 s), valgrind clean on ~390 tests, bench typecheck phase within noise, RSS +6%. What later stages should know:
- `LazyType` is `{tag, slot, inline memo}`; memos are `LazyMemo`s in `Checker.lazy` (`LazySlots`), still holding the
  legacy `Type`. Flip `LazyMemo.evaluated` to `Ty const*` in stage 4. The tag never changes after bind any more, so
  `LazyType.origin` is gone.
- Deviation: slots are numbered by a process-wide counter, not per `SourceFile` (Declarations, Scopes and identifier
  types made during checking belong to no file). Check-time clones keep an inline memo (`LAZY_SLOT_INLINE`). See
  `hacks.md`.
- One `Checker` per `Program`, made by `program_make_current()` (not only `program_typecheck`: the harness traversal
  evaluates types of programs with parse errors, which are never typechecked). Name resolution evaluates no lazies
  (it would panic now). The shadow pass uses the program's checker.
- Not done: `has_cache/cached` in legacy type operators still write to the (lib) AST, so the harness "run twice if
  net allocations" workaround stays (it's also needed for the lib program cache itself). No Declaration -> node link.

**Stage 2 as planned - checker state out of the AST.** `LazyType` → `{tag, slot}` + checker slots holding the *legacy* `Type`
for now (arena'd or heap, executor's call) and later a `Ty*`; `thread_local current_checker`; one `Checker` per
`Program`, created in `program_typecheck` (or earlier if name resolution evaluates lazies - check). Lib ASTs are no
longer written to during tests; delete the "run test twice if net allocations" harness workaround if it falls out.
*Validate:* pass counts identical to before the stage (this is a pure refactor); valgrind clean; suite wall time not
worse than +20%.

**Stage 3 - DONE (2026-09-18).** 1100 full / errors 4553 / symbols 5939 / types 2149, suite 5.6 s, the depth-limit
`log_error`s are gone, repro test at startup cost, bench typecheck +2% (noise). Details in `measurements.md`, shortcuts
in `hacks.md` ("Relations bridge" and the `relate.c` entries). What later stages should know:
- `src/ty/relate.c` (assignable, comparable, strict-subtype and a strict-nulls variant; per-relation id-keyed caches
  on the checker, maybe-stack, deep-nesting check, measured variances with marker type parameters, reporting as a
  second run) and `src/ty/conditional.c` (conditional resolution, `infer` inference; distribution is in
  `instantiate.c`). `TTTT_DEBUG_RELATE=1` logs every top-level relation query, `TTTT_DUMP_VARIANCES=1` the variances.
- **The bridge was the hard part, not the relation.** Legacy callers pass temporaries that get freed, and stage 1
  keyed anonymous types by node address. `LowerCtx.transient` + `ty_structural_from_legacy` + `legacy_stable_view`
  handle that (see hacks.md). Stage 4 should shrink the set of places that pass temporaries quickly; every
  `type_clone`d type that reaches a relation costs an eager structural lowering.
- The legacy relation is *not* fully deleted: `legacy_infer_extends` stays private in `type.c` for legacy conditional
  types with `infer` (their results must be legacy types; building a `ty_to_legacy` was judged worse). It lost its
  string cache (`type_pair_cache.h` and the `hash_key` display mode are deleted) and immediately went exponential
  (`inferTypes1`: 75 s, 9 GB) until it got a coarse in-progress list. It dies when type operators evaluate in the new
  core (4b). `TYPE_NEW` still doesn't exist; nothing needed it yet.
- Done as planned in stage 1's notes: deferred array/tuple references (`type Json = string | Json[]`). Also needed
  and not planned: lazy conditional branches (`TyCondBranch`, for recursive conditional aliases), lazy signature
  return types, mapped type expansion to properties, inherited class members, sorted property tables.
- A correct relation exposes the legacy checker: 41 test components regressed because weak inference / missing
  contextual typing / missing narrowing used to be forgiven. Two blanket mitigations went in (`inference_gave_up` ->
  `any`, `any` source always assignable); the rest is for 4b and later plans. Expect the same effect again whenever
  a legacy approximation is replaced by the real rule.
- The head line of assignability errors is still rendered by the legacy display. Switching it to `ty_display` needs
  expression types to be widened/fresh like tsc's first (4b).

**Stage 3 as planned - relations on the new core.** Port `type_extends`, `type_overlaps` (→ comparable), `type_composite`
simplification and `check_assignable_or_error_internal` to `is_related` over `Ty` (lower both sides). Persistent
caches, maybe-set, deep-nesting check, variances, TS2589. Elaboration chain for the common cases (property, argument,
union member) since the lowering now knows `List<string>`'s name. Delete `type_pair_cache.h` and the `hash_key` display
mode. *Validate:* repro test < 50 ms over startup and zero "Max type recursion depth" in `failstats`; errors-component
passes ≥ 4396, full ≥ 1084; bench typecheck phase not slower.

**Stage 4a - DONE (2026-09-18).** 1115 full / errors 4590 / symbols 5974 / types 2183 (from 1100 / 4553 / 5939 / 2149),
suite 4.3 s, `log_error` lines 7.6k -> 4.6k, bench typecheck phase unchanged (~118 ms), valgrind clean on ~650 tests.
Details in `measurements.md`, shortcuts in `hacks.md` ("Legacy code consuming the new core"). What 4b should know:
- **`TYPE_NEW` exists now and the scaffolding held.** Classes (instance + static side), interfaces, enums, enum members
  and every identifier type that names one evaluate to `Ty` (`ty_declaration_is_evaluated_by_new_core` is the
  switch). Legacy code consumes them through `ty/legacy_view.c`: `type_resolve()` of a `TYPE_NEW` is a memoised one-layer
  legacy *view* with `TYPE_NEW` children. It took ~250 lines plus a `TYPE_NEW` case in ~15 switches, and the three flips
  (enums, classes, interfaces) each cost a handful of fixes, not a long tail. Flipping type aliases is the obvious next
  step and the same pattern (`identifier_type_evaluate_type` -> `ty_from_identifier_type`); then `generic_degenericize_type`
  has no type-side callers left. Expect deferred types (`keyof T`...) viewed as `any` to matter there.
- The new core builds class members itself from the `ClassDecl` (own items, overloads, accessors, inherited instance
  and static members, construct signatures incl. inherited default constructors), enums are unions of member literal
  types with real values (`ty/enums.c`, a constant evaluator over `Expr`), and `Declaration.nodes` links a Declaration
  to the AST nodes it was made from. `Prop`/`Signature` carry declaration spans (the symbols baseline reads them
  through the view). Instantiation expressions (`Bar<T>` as a value) exist in the new core because `new Bar<T>()` needs
  them.
- Not done from the 4a brief: a `this` type parameter, accessibility/nominal class rules, namespaces (still the legacy
  structural value type), function declarations (still `LAZY_TYPE_FUNCTION` -> legacy callable, lowered by node).
- The legacy call path is now the weakest link: parameter types declared in interfaces/classes are `TYPE_NEW`, legacy
  inference only understands a bare type parameter, and everything else becomes `any`. 4b should start with calls
  (`type_call_evaluate_type`/`type_typecheck_call`/`generic_infer_callable_types` on `Signature`s, `infer_from` in
  `conditional.c` is a usable seed) and property access (`index_type_evaluate_type*` on `ty_members`), then expressions.
- Exposed and fixed on the way: bare member names in enum initializers didn't resolve (`B = A << 1`); optional
  properties were read as `T & undefined`; atoms clustered in their hash table (+70% typecheck time on the bench until
  the hash got a finalizer - `hmap_arc_declaration` lookups may have the same problem, see triage).

**Stage 4b1 - DONE (2026-09-18).** 1120 full / errors 4648 / symbols 5979 / types 2186 (from 1115 / 4590 / 5974 / 2183),
suite 4.5 s, `log_error` lines 4.6k -> 2.2k, bench typecheck unchanged (118 ms), valgrind clean on 233 tests. Details in
`measurements.md`, shortcuts in `hacks.md` ("Calls, inference and property access on the new core"), overview in
`documentation.md`. What 4b2/4b3 should know:
- **The `TYPE_NEW` seam for calls held, no merge with 4b3 needed.** `ty/call.c` (resolution, overloads, arity, spread and
  rest, TS2769 reporting), `ty/infer.c` (one inference walker shared with conditional types: co/contravariant
  candidates, priorities, top-level tracking, tsc's result rules) and `ty/access.c` (property/element access through
  apparent types) replace `type_call_evaluate_type`, `type_typecheck_call`, `generic_infer_callable_types`,
  `collect_generic_candidates`, `interface_select_callable_overload` and `index_type_evaluate_type`. The
  `inference_gave_up -> any` mitigation is gone with them. The legacy `index_type_evaluate_type_internal` and
  `type_all_keys` stay for the legacy type operators until 4b2; `index_type_typecheck` (TS2339) is still legacy too:
  making it honest is cheap now (`ty_access` returning null = no such property) but floods until narrowing and
  apparent types of everything are right, so it was left alone.
- What replaced the blanket mitigation is one narrow one: object/array literal arguments only need to be *comparable*
  (`Arg.widened_without_context`), because the legacy checker widens their literals with no contextual type. 39 error
  components were lost anyway, nearly all for missing contextual typing (callback parameters, literal freshness,
  tuple context) - 4b3 should expect them back and treat `Arg.context_sensitive`, `InferParam.fixed` and
  `infer_type_arguments(..., fixed)` as the hooks for the second inference pass.
- Bugs found in earlier stages on the way: `infer X` parameters that got no candidate were never replaced, so
  `Awaited<unknown>`-like conditionals stayed deferred forever; inherited call signatures replaced instead of joined
  the type's own; `this` parameters counted as arguments; structural (transient) object types were always flagged as
  containing type variables, which kept `keyof {a, b}` deferred; a class merged with a namespace had no construct
  signatures; instantiated generic signatures expose uninstantiated constraints (`instantiate_outer` works around it,
  the real fix is fresh type parameters per instantiated signature in `signature_resolve`).
- 4b2 note: `ty_access` and `try_resolve_indexed` (checker.c) now overlap. When indexed access *types* move to the new
  core, make `try_resolve_indexed` call `ty_access` for the non-generic case instead of keeping two lookups.

**Stage 4b - split in three (insights, 2026-09-18).** 4a showed flips are cheap when done one kind of thing at a
time, and that the pain is wherever a *weak legacy algorithm* meets precise `Ty` inputs. So 4b is three sessions, each
leaving the suite runnable and pass counts at or above the previous session's:
- **4b1 - calls and property access.** Call resolution on `Signature`s (overload selection, arity, spread/rest,
  optional params, `new`), property/element access on `ty_members` (`index_type_evaluate_type*`, `type_all_keys`),
  and generic inference. **Deviation from "keep its current feature level": don't port `generic_infer_callable_types`.**
  It only understands bare type parameters; porting it means writing the same code twice. Write inference on `Ty`
  the way tsc observably does it: walk source/target structurally (`infer_from` in `conditional.c` is the seed; share
  it), collect candidates per type parameter with covariant/contravariant positions, pick union-of-candidates /
  common supertype, fall back to the constraint then `unknown`, check the result against the constraint, literal
  widening unless the constraint is primitive-ish. Out of scope for 4b1: the second pass for context-sensitive
  arguments (needs contextual typing in 4b3; leave the seam: inference takes an "already inferred" mapper), return
  type inference from contextual type, priorities beyond what tests demand. Legacy expression code keeps calling in
  through `TYPE_NEW` at the seam: arguments are lowered, the result comes back as `TYPE_NEW`. Remove the
  `inference_gave_up -> any` mitigation if the numbers allow. *Validate:* the 13 components lost in 4a are back;
  errors >= 4590, full >= 1115; failstats "not implemented: inference" `log_error`s gone.
- **4b2 - type aliases and type operators.** `identifier_type_evaluate_type` -> `ty_from_identifier_type` for aliases,
  so every written type evaluates in the new core: `keyof`, indexed access, conditional, mapped, `typeof`. Deletes
  `legacy_infer_extends`, the `has_cache/cached` AST writes (and with them, check whether the harness "run twice"
  workaround can go), the type-side callers of `generic_degenericize_type`/`replace_named_types_with`. Deferred
  types can no longer be viewed as `any`: the legacy view needs an honest answer for them (usually: the constraint).
  *Validate:* `grep legacy_infer_extends src` empty; types >= previous; no AST writes during checking (valgrind on the
  3 tests named in hacks.md).
- **4b3 - expressions, statements, bindings, functions, namespaces.** The rest of "Stage 4 as planned" below. Shape it
  for what comes next rather than transliterating: one `check_expr(expr, contextual Ty)` entry point (contextual typing
  of function expressions, object/array literals and the context-sensitive inference pass hang off it), fresh vs
  regular literal types with widening at declarations (then the error head line can move to `ty_display` and the
  `is_simple_related` enum hack can go), and a single function through which every identifier *reference* gets its type
  (plan 03's narrowing replaces its body). Harness fidelity runs before this session so `CompilerOptions`
  (`strictNullChecks` etc.) exists as a struct the new expression code can read instead of ad-hoc bools. The test
  traversal switches to `ty_display`. *Validate:* `LowerCtx.transient` and `ty_structural_from_legacy` have no callers;
  all counts >= previous.

If 4b1 finds that calls can't be cut from expressions at a `TYPE_NEW` seam without more scaffolding than it's worth,
merge 4b1 into 4b3 and say so; 4b2 is independent of both.

**Stage 4 as planned - type evaluation produces `Ty`.** Flip `LazyType` slots and every `*_evaluate_type`
(`expr.c`, `statement.c`, `binding.c`, `function_impl.c`, `class_decl.c`, `enum_decl.c`, `interface_decl.c`,
`scope_evaluate_value_interface`, `merged_type_evaluate_type`) to return `Ty const*`, file by file, using `TYPE_NEW`
at the seams. Port `index_type_evaluate_type*`, `type_call_evaluate_type`/`type_typecheck_call`, `type_all_keys`,
`type_abstract_literals` (→ widening via fresh/regular literals), `type_remove_null_and_undefined`, generic call
inference (`generic_infer_callable_types`; keep its current feature level, real inference is its own later plan).
Probably two sessions: (4a) declarations/classes/interfaces/enums/functions, (4b) expressions/statements/calls.
The test traversal (`baseline_test_traversal.c`) switches to `ty_display`. *Validate:* types-component passes ≥ 2146
and expected to rise (alias/reference names); full ≥ 1084.

**Stage 5 - delete the old world.** Remove `TYPE_NEW`, `TYPE_LAZY`, `TYPE_REF`, `ty_to_legacy`, `type_clone`/`type_drop`
for semantic use, `replace_named_types_with`, `generic_degenericize_type`, `cached` fields, `owned_*`/`*_ref` pairs
that only existed for clones, `Interface.namespace_scope` hack. Rename `Type` → `TypeNode`, `Ty` → `Type` (mechanical,
do it last so earlier diffs stay readable). Rewrite the Types/LazyType/Memory sections of `documentation.md`, prune
`hacks.md`, rerun `./bench.sh`. *Validate:* `grep -c type_clone src` near zero (TypeNode is never cloned); bench
typecheck phase and peak RSS better than today's 113 ms / 790 MB; all pass counts ≥ baseline; valgrind clean on 1000
tests.

If a stage turns out to need the next one's work to be testable, merge them rather than faking it; if the scaffolding
(`TYPE_NEW` round trips) fights back harder than a stage is worth, say so in the commit and let the next insights
session decide on a harder cut-over. Stages 1 and 2 are independent and could swap.

## 5. Deliberately out of scope
Control flow / narrowing, persistent symbols and namespace merging (plan 03, which should start from `Prop` and decl
links), real generic inference, mapped/template-literal type *features* beyond what exists today, freshness/excess
property checks beyond carrying the flag, multi-checker threading, arena-allocating the AST.
