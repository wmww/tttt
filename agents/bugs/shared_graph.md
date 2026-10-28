# --shared-graph (TTTT_SHARED_GRAPH=1): flagged output against plain one checker

Flag-only: the default path has none of these (tsgo's output and plain tttt's are checker-count independent), so the
open ones are tagged `[note]` and don't count in the headline; they block turning the flag on by default. The gate is
`scripts/sgcheck.py` (flagged runs at K 1/4/8/16, the scheduler's deal and a program-order round robin, against plain
K1 byte for byte). Census 2026-10-29 (K8, both deals, 3 runs each, the 43 hub projects and 15 corpora of
scripts/sharedgraph/sgcheck_projects.txt, load 8-15): main at ab39dce20 12 of 348 runs differ in 5 projects
(auth-api 3, folo renderer 2, better-drizzle 5, drizzle 1, effect 1); wt-sg after b9a8bc824 6 runs in 3 projects
(better-drizzle 4, drizzle 1, effect 1), after c76f0abbe better-drizzle 0/21.

- **openbot/apps/auth-api: private names raced** (hub_regime.md's 75 TS2345s): ty_private_name() returned its own
  `#x` atom when another checker's was published first; the class's members carried the winner's, so lookups missed
  them (TS18014, TS6133, `unknown`/`{}` through Effect chains). 10/10 runs before, 0/60 after. [fixed b9a8bc824]
- **No one-checker rerun under the flag**: a history-dependent TS2589 stayed whatever the schedule hit
  (better-drizzle src/shared/client/factory.ts(77,19)). The flag reruns now, in a fresh graph. [fixed c76f0abbe]
- **TSan groups**: the could-contain-type-variables memo in xflags bits (fetch_or against plain reads, five groups)
  and KEY_PROP_WRITE_TYPE's null "none" written into another checker's published block (intern_set()), fixed by
  04d2384d9; ty_reduced_slow()'s second KEY_REDUCED_UNION store into a possibly published block, fixed by 0fe39de79.
  TSan clean on drizzle, effect, excalidraw, zod at T4/T8 after. [fixed 04d2384d9]
- **drizzle residue C** (sharing_design.md §4): the Gel/MySql query builders' `execute` chains (TS2430/TS2416 at
  src/gel-core/query-builders/delete.ts(98,18), insert.ts(198,18), update.ts(280,18), mysql2/session.ts(281,17))
  missing or elaborated differently (`Promise<unknown>`). 5/10 K8 runs with the scheduler's deal, 0/30 with the round
  robin; 0/128 seeded schedules (scripts/sharedgraph/seeds.sh, K8, rates 16 and 2): it needs true concurrency. **0/10
  under `TTTT_NESTING_ORDER=content TTTT_FIRST_TOUCH_STAMPS=0`** (the reference K1 run with the same order): the
  mechanism is the first-touch stamps standing in for type ids in is_deeply_nested_as(). A worker stamps a type when
  it first touches it, but a published composite (members, an instantiation) brings many types at once, which plain
  creates in its own order at its own time: the "newer" test, and with it a deep-nesting cut, follows the schedule.
  The cut is a taint event, but what it decides (a relation verdict reported in this file's diagnostics) is the
  worker's own history, not plain K1's. The fingerprint oracle also shows a KEY_UNION_PAIR published from inside a
  variance measurement in expo-sqlite/query.ts (definitely_unrelated()'s permissive check -> compare_signatures ->
  infer_from_type_args -> get_variances) whose cloned `TResult1`'s mapper targets `any` where K1's targets `unknown`
  (9/12 runs, output unaffected there). Fix: the content order (sharing_design.md stage 1; its parity cost is listed
  there), a decision for the coordinator; stamps can't be made a function of the node. [note]
  - The content order's parity cost, measured 2026-10-29 (plain K1 with and without `TTTT_NESTING_ORDER=content
    TTTT_FIRST_TOUCH_STAMPS=0` on sgcheck's 58 projects): 55 identical; drizzle chain text only (12 lines: the
    mysql2/session.ts TS2416 elaboration one return level deeper, `with(...).update(...).set(...).where(...).$dynamic()`);
    but two diagnostics appear: webpack tooling/generate-types.js(397,35) TS2345 (`string | number | boolean | Type`
    to `Type`, sharing_design.md §1 had it) and openscience/backend/cli test/tool/bash-output-receipts.test.ts(52,49)
    TS7006 (`file` implicitly any in a `.map((file) => file.path)` callback). So the content order is not yet only
    text: making it the flag's order was stopped there. Tried (one time-boxed attempt, not kept): bun's "first push on
    either stack" (each relation push numbered, a level's target before its source, source_seqs/target_seqs in
    RelationState; inference's two stacks in lockstep) in place of the prototype's this-stack-only push order: the
    same three projects differ, the same two diagnostics appear. So the push-order detail isn't it; the height
    comparison itself (or where bun cuts that tttt doesn't model) decides those two. Next, if wanted: reltrace or
    TTTT_DEBUG_RELATE_NESTED on webpack's generate-types.js(397,35) under both orders, the first relation whose
    verdict parts. [note]
  - Traced (`TTTT_CHECK_ONLY=tooling/generate-types.js TTTT_DEBUG_RELATE_NESTED=1`, both orders): the call is
    `flatten(args.map((arg) => [...]))` with `flatten<T>(iterable: Iterable<Iterable<T>>): T[]` (JSDoc). Inferring T
    from `(string | boolean | Type)[][]` walks the target's `Iterable` -> `Iterator` -> `next` -> `IteratorResult`
    chain twice, `Iterable<Iterable<T>>` then `Iterable<T>`. Under ids the inner instantiations are made later
    (newer), so inference's expanding test (max 2) cuts the second walk and T comes out `any` (the id log relates
    against `IteratorResult<any, any>`), which is tsc's answer: no error. Under the content order the inner ones are
    *lower* (`Iterator<T>` is lower than `Iterator<Iterable<T>>`: T is a leaf), so going inward never counts as newer,
    nothing is cut, T is inferred as `string | boolean | Type` and the `.map((item) => typeParametersMap.get(item))`
    after it reports TS2345. The mechanism: the height says "smaller", where tsc's creation order says "made later",
    so recursion that unwraps a type (the descent tsc's deep-nesting test exists for, in inference) is never cut. Not
    bounded: any content key must order an unwrapped instantiation after its wrapper, which height can't. openscience's
    TS7006 (a `.map((file) => ...)` callback's parameter) is probably the same cut through inference, not traced.
    [note]
- **A signature's instance shared while its signature's resolution was private** (effect's Schema.ts
  `X.pipe(lowercased(...))` cascade: TS2507 "never is not a constructor", Schema.tst.ts(2892,39) TS2345, 2-3 of 10 K8
  runs; folo/apps/desktop/layer/renderer's residue W: `GeneralSettings[K]` read as never, TS2345 at
  data-control.tsx(36,77) and CustomIntegrationSection.tsx(18,3), 4/4 at K16 with the scheduler's deal).
  ty_signature_instantiation() published (signature, args) -> instance as an identity whose mapper's sources were the
  signature's type parameters as *this* worker resolved them; a resolution kept privately (POISONed or shadowed) has
  this worker's own clones, so another worker's instance lookup got a mapper that never matched its type parameters.
  Found on folo by elimination (TTTT_GRAPH_NOSHARE over identity kinds; poisoning every shared word, not sharing memo
  kinds or relations didn't remove it). Taint rules: not a missed taint event but an identity built from a private
  record, which identities never check ("Clean, precisely" in notes/shared_graph.md covers computed values only).
  After: effect 0/28, folo 0/16. [fixed 720b23760]
- **mikro-orm/packages/seeder at K16, round robin**: packages/core/src/entity/Reference.ts(131,44) TS2322 "Type
  'object' is not assignable to type 'T'", 2/2 runs at K16 rr, 0/10 at K4/K8 and K16 with the scheduler's deal
  (shared_graph.md's residue C listed mikro-orm). 0/4 under `TTTT_NESTING_ORDER=content TTTT_FIRST_TOUCH_STAMPS=0`
  against 2/4 without (2026-10-29), and 2/4 still after 720b23760: residue C's family (the stamps), with drizzle's.
  Repro: `scripts/sgcheck.py -f 'mikro-orm/packages/seeder' -k 16 --deals rr -n 4`. [note]

After 720b23760 (2026-10-29, the default list at K 4/8/16, both deals, 2 runs, 696 runs): 3 differ, all drizzle's C
(K8 and K16 with the scheduler's deal); mikro-orm seeder's shows at a lower rate (0/4 there, 2/4 in a targeted run).

Final state on wt-sg rebased on fc64a1ea3 (2026-10-29, the default list at K 4/8/16, both deals, 2 runs, 696 runs):
9 differ in 4 projects: drizzle 2 (K8 def, C), effect 3, folo renderer 2 (K16 def), mikro-orm seeder 2 (K16 rr).
TSan clean at T8 on drizzle, effect, excalidraw, zod, vue (with the lazily made instance properties).

## Audit: identities made of private records (2026-10-29)
The rule (notes/shared_graph.md "Identities are published whatever their taint"): a published identity's fields must
be a function of its key, the AST and other published identities, never of a record this worker resolved privately.
Every constructor that publishes through intern_set()/intern_deferred()/ty_link_publish() and reads a resolved record
(signature_resolve, sig_res, ty_members, prop types, constraints, declared types, free params, variances, clones) before
publishing was listed (a scan of the publishing functions' bodies) and read:

| kind (site) | reads | verdict |
|---|---|---|
| KEY_SIGNATURE_INSTANCE (infer.c ty_signature_instantiation) | the signature's SigResolved type parameters, as its mapper's sources | **was the bug**: OWN kind when the published record isn't the one held (720b23760) |
| KEY_SIGNATURE_INSTANCE (relate.c ty_erased_signature, one-step erasure) | the target's SigResolved type parameters | same, OWN kind (720b23760) |
| KEY_SIGNATURE_TYPE (members.c ty_signature_type) | nothing: one member, the signature (the key) | safe |
| KEY_ANONYMOUS_INSTANCE (instantiate.c instantiate_anonymous) | ty_function_free_params() (AST: the declared type parameters a signature's annotations mention, identities by symbol) for which mapper pairs to keep; the pairs are the key | safe: the free params are a syntactic function |
| KEY_ANONYMOUS_INSTANCE (expr.c ty_apply_type_arguments_mapper_of; instantiate.c realias/alias-body twins) | ty_members() only to decide whether to make one; fields are the key's | safe |
| KEY_STRUCTURAL (members.c object literals) | prop types of reused props; the reused Prop is in the key | safe (a private Prop makes a private key) |
| KEY_SYNTHETIC_PROP_PARTS, KEY_PROP_MAPPED_TYPE (members.c) | members of the parts / the mapped template | safe: part of a Prop made inside a members computation, which is published only if clean (else the Prop, and so the key, is private) |
| KEY_KEYOF (checker.c ty_keyof), KEY_REFERENCE (ty_reference_with_this: tuple optional elements) | members only for a shortcut / argument normalization before keying | safe: the key is built from what was read |
| KEY_MARKER, KEY_CHECK_MARKER (relate.c) | another marker's identity (intern_set's winner) as the constraint | safe |
| KEY_RESTRICTIVE_PARAM (conditional.c) | writes a constraint init, reads nothing | safe |
| KEY_INSTANTIATION_QUERY, KEY_CLONE_CLASS (lower.c) | the key's type / the declaration | safe |
| KEY_CONDITIONAL, KEY_DEFERRED_REFERENCE, KEY_FUNCTION_RETURN, the plain constructors (literal, union, intersection, indexed, substitution, template, string mapping, tuple target, unique symbol), the AST/symbol roots | key components, AST, root outer params (AST) | safe |

Not covered by construction: an identity whose constructor calls something that reads a private record further down
than the scan saw (a helper). A debug check would wrap each identity constructor in a taint frame and log identities
published after a private read; not built.

### The debug check (TTTT_GRAPH_CHECKS, a `-DTTTT_GRAPH_CHECKS` build)
`meson setup B --buildtype release -Dc_args='-g -DTTTT_GRAPH_CHECKS'`, then `TTTT_GRAPH_CHECKS=1` (=2 panics) with the
flag: every identity lookup records the worker's taint counter; publishing an identity after a taint event since its
miss is reported (`graph check: identity of kind K published ...`, the address for `addr2line -f -i -e B/tttt`). The
constructors that read resolved records open a frame (GRAPH_CHECK_FRAME() in ty_signature_instantiation,
ty_erased_signature, instantiate_anonymous, ty_apply_type_arguments_mapper_of) and bind their own lookup to it
(GRAPH_CHECK_BIND()): the window starts at the frame, and a *hit* on another worker's published identity after a taint
event since the frame is reported too (`found`): the reader's side, which is how folo's bug happened (the instance was
published clean; the reader held its own resolution). GRAPH_CHECK_FIELDS_DONE() moves the window past an identity's own
lazy parts (a signature instance's resolution). Default build: the macros are empty, checker.c's hooks compiled out
(instructions drizzle/zod +0.01%).
- With 720b23760 reverted locally it fires on folo: `found` at ty_signature_instantiation (infer.c). Today, on sgcheck's
  list at K8 both deals (116 runs), two sites report:
  - **instantiate_anonymous (instantiate.c), KEY_ANONYMOUS_INSTANCE published**, in 106 of 116 runs: the window from
    the miss to the publication includes instantiating the target's alias arguments (a nested instantiation that meets
    a taint event) and literal_instance_is_itself() (reads the literal's members). The alias arguments are part of the
    published value, so a history-dependent nested result would be published with it; no output shows it (the census
    is clean outside residue C). Over-approximate or a display-level residue: not resolved. [note]
  - **ty_erased_signature (relate.c), `found` after a peek** (nest platform-fastify and microservices tests, 4 runs):
    the one-step erasure is chosen when `sig` isn't resolved yet in this worker (a done-ness peek), and it shares its
    key (sig, any...) with ty_signature_instantiation(sig, anys), which the resolved path makes. So which object a
    worker gets for that key depends on whether some worker had resolved `sig` (history, also at K1, but the flag
    publishes the other path's). A distinct key for the one-step erasure removes the sharing, not the peek. [note]
