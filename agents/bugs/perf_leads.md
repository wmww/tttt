# Perf leads
Numbers are in `measurements.md`; this is what to try next. Profile with callgrind (`valgrind --tool=callgrind
./release/tttt -q files...`, then `callgrind_annotate [--inclusive=yes]`; no `perf` on the dev box). The release build
has no debug info: for line-level annotation (`callgrind_annotate --auto=yes`) build a copy with
`meson setup /tmp/relg --buildtype release -Dc_args=-g` (same code, same instruction counts). Compare
instruction counts across sessions, not wall times. `--time` prints the resident set after each phase and the
checker's arena size and type count. Memory: `scripts/memtrack.sh -p DIR` for the malloc heap (live blocks per phase by
allocating function; massif's extra-heap estimate is wrong for glibc), `scripts/arenasites.sh -p DIR` for the mmapped binder
and checker arenas by call site.

## Immediate distributive constraints (plan 61 stage 2, 2026-10-13)
tanstack-router 5.337 -> 5.401 G (+1.1%), xstate +0.3%: from base constraints and getSimplifiedTypeOrConstraint taking
the check type's one-step constraint (tsc's) instead of its base constraint, so a conditional's constraint goes
through more instantiations. react +0.5% from `conditional_source_related()` relating the immediate distributive
constraint only (the base one, tried first, often succeeded cheaper). Not profiled: callgrind tanstack-router at
8a2dfab5 vs after, inclusive counts of `distributive_constraint_uncached`/`ty_instantiate` under `base_constraint`.

## Checking the default lib (2026-10-12)
Lib files are checked unless skipLibCheck/skipDefaultLibCheck (vision.md). An empty esnext+dom+dom.iterable project:
0.16 G -> 0.43 G instructions, ~42 ms check (tsgo --singleThreaded: ~156 ms, 31.5k types vs tttt's 13.4k); quill
and pinia/nuxt +8% total. lib.dom.d.ts is ~32 ms of it. The callgrind split: interface heritage relations
(`HTMLElement extends Element...`, ty_check_heritage -> structured_related_members) ~127M, `ty_indexed_access_index_ok`
68M (the `K extends keyof XEventMap` / `XEventMap[K]` of every addEventListener overload), `ty_keyof` 49M with 50M in
msort (keyof of the big tag/event maps sorted again and again?). Cheap leads if small projects ever matter.

## Perf at breadth (plan 59): `scripts/perfsweep.py` ranks the outliers
Stage 2 (2026-10-12) drained every project above 1.5x tsgo's types (measurements.md "Perf at breadth": worst 16.4 ->
1.55, total 96.0M -> 71.3M types against tsgo's 127.9M, worst RSS ratio 2.48 -> 0.95). The mechanisms, for the next
outlier: instantiations keyed by the whole mapper where tsgo keys by the outer type parameters the declaration
possibly references (overloads, object literals, declared methods, `typeof f<T>`/imported `typeof ns.x`); eager work
tsgo defers (single-member intersection properties, erased signatures, the canonical signature); a missing recursion
identity (deep nesting never stopping); messages printing whole types where tsgo truncates (rxjs). What's left is
below per agent; the new top of the ranking is small projects (1-3k types, lightweight-charts plugins ~1.4) and
karakeep/packages/plugins 1.40. Recipe: `scripts/perfsweep.py -f '^PROJECT$'` reruns one, `TTTT_FILE_TIMES=1` finds
the file, then which type kinds grow (`scripts/arenasites.sh`); instrumented tsgo builds (overlay outside the shared
tree) answer "does tsgo make these too".

## Plan 59 stage 2 leftovers (agent D)
Fixed (2026-10-12): dokploy x4 (`typeof f<T>` / `typeof ns.x` free params, 2.3 -> 0.69), arktype/ark/docs (this-only
references share thisless base signatures, 1.51 -> 0.92), linkwarden/apps/mobile (union flattening skips repeated
types: IconSymbol.tsx 1.54 s -> 0.14 s, RSS 517 -> 239 MB), mcp-sdk/packages/core-internal (declaration printing
memoizes module specifiers and accessible chains: 9.5 -> 1.3 G instructions on a schemas.ts probe). Round 2
(full `perfsweep.py -j 2` at 8ba67cad, /tmp/perfsweep-d-round2.json): types median 0.70, p90 0.98, 155 projects
above 1; the worst are rxjs 2.58, quill 1.81, immutable-js 1.77, nitro/examples/hono 1.71, date-fns docs 1.65 and
then sass's immutable.d.ts (vitest/examples/lit, angular-components/tools/extract-tokens, vitest/test/browser, vite's
two, all ~1.6: owned by agents A-C); below them karakeep/packages/plugins 1.43 and projects of 1-15k types whose
excess is a few hundred to a few thousand types (lightweight-charts plugins, solid-table-devtools, apollo codegen,
repomix/website/server: hono's MiddlewareHandler like nitro/examples/hono). Left:
- **Intersection property types are eager in find_prop_in()** (tsc's synthetic property type resolves on first
  getTypeOfSymbol; intersection_props_related() is lazy for >2 parts since bc73dda3). Not a tsgo gap where it was
  found: tiptap's generic `<Tag {...rest} ref=...>` intersects 119 `Ref<X> | undefined` unions (TS2590 under a
  `// @ts-ignore`), and a probe of packages/react/src/ReactMarkViewRenderer.tsx costs tsgo the same (511k types,
  5.7 s check; tttt 518k, 5.3 s). Only worth doing if a project shows find_prop making intersections tsgo doesn't.
- **karakeep/packages/plugins** (types ratio 1.43, round-2 perfsweep): ratelimit-redis/src/index.ts, node-redis v5's
  multi-command types. 19.6k `CommandSignature` instances, all from variance probes of WithCommands/WithModules/...
  (sub-T/super-T for each of the 6-7 parameters, times ~750 commands: the mapped type's keys are concrete, so every
  property's template is instantiated per marker). tsgo: 44k types for the probe vs tttt's 71k, but tttt checks it in
  0.39 s vs 1.37 s. Whether tsgo relates those instantiations by the alias's variance without instantiating the
  property types needs an instrumented tsgo; low priority.
- **Single-file probes are order-dependent**: a probe of dokploy/packages/server/src/lib/auth.ts alone gets TS2589 at
  (513,23) from tsgo (not from the whole project, where neither reports it); tttt reported it too before 892750cc and
  doesn't now. Not chased: tsgo's instantiation count depends on what was checked before.
- **Worktrees load symlinked sources twice** (the "Pathologies" entry below): arktype and arktype/ark/docs show ~40
  spurious TS2339/TS2300/TS7006 in a worktree (files under both `thirdparty/sources/...` and `../../home/...`), none in
  the main tree. Compare source sweep runs of the same worktree only.
- Wall times of this session are meaningless (load 20+ on 4 cores): remeasure linkwarden/apps/mobile and
  mcp-sdk/packages/core-internal check-time ratios on a quiet machine (`scripts/perfsweep.py -f`); their types ratios
  (0.87, 0.83) were never the problem.
### Plan 59 stage 2 leftovers (agent A)
Fixed (types ratio before -> after): date-fns/pkgs/docs 16.4 -> 1.6 (overloads' type parameters made a method type's
free parameters unknown), feathers/packages/typebox 7.0 -> 1.1 (object literal instances keyed by the whole mapper),
lit ssr-react 2.4 -> 0.65 and the small React projects (every JSX.IntrinsicElements member's property types resolved
when relating to an intersection), mcp-sdk/packages/codemod 2.2 -> 0.9 (declared method types keyed by the whole
mapper), nitro/examples/hono 1.76 -> 0.77 (all overloads resolved up front to look for a `this` type). How each was
found: `A_VAR`-style logging of the types each variance measurement makes (next_type_id before/after in
measure_variances(), and the same in a patched tsgo's getVariancesWorker built with `go build -overlay` into /tmp, not
through ensure-typescript.sh, whose built/ is shared), then a probe of the costliest measurement with a log of the
same-named property pairs that relate as distinct types (`sourceProp != targetProp` in tsgo).
- **date-fns/pkgs/docs** fixed in round 2 (1.56 -> 0.94): erasing an unresolved instantiated signature resolved it first.
- **A getter resolved inside its own circularity**: a correctness bug, not only perf. Object literals with a getter are
  still instantiated by the whole mapper (56a730f2): with the identity instance (what tsgo does) zod's
  recursive-types.test.ts gets a spurious TS2344. Round 2 found it isn't about the circularity marker (no
  ty_resolving_cycle() is hit there): the literal's mentions are first asked while its getter is lazy (unknown, keyed
  by the whole mapper) and later answered (identity), so the same literal has instances of both kinds, and which ones
  meet in a relation decides the verdict. Making the early "unknown" permanent passes the corpus test but not this
  probe, which **fails on ai-main already** (tsgo is clean): `/tmp/a-probes/zod-getter-probe.ts` content, a function
  with `const TaskListNodeSchema = z.strictObject({ type: z.literal("taskList"), get content() { return
  z.array(z.tuple([TaskListNodeSchema, z.union([TaskListNodeSchema])])).min(1); } })`, `type T = z.infer<typeof
  TaskListNodeSchema>`, a recursive `_T = { type: "taskList"; content: [_T, _T][] }` and `const y: _T = x as T` (spurious
  TS2322 "Target requires 2 element(s) but source may have fewer"; run it with a tsconfig whose `paths` maps "zod/v4"
  to the zod corpus's src/v4/index.ts). tsgo never asks the getter's type during the strictObject call, nor while the
  variable is in flight; tttt resolves the inner `TaskListNodeSchema` inside the getter to a type whose content is
  `ZodObject<any, $strict>`-like at the second level. Next step: find where the `any` comes from (it isn't a
  declaration cycle; maybe the reentrant "evaluate again below the call's barrier" in lazy_type_evaluate_inner()).
- **angular-components/tools/extract-tokens 1.62** (1.01M vs 627k): all of it is checking immutable@5's immutable.d.ts,
  the immutable-js/type-definitions cause (agent B's).
- **karakeep/packages/plugins 1.43, found but not committed** (round 2): the RedisClientType alias variances (26k types)
  come from infer_from()'s alias shortcut, which tsgo runs through invokeOnce (inferFromAliasTypeArguments): there the
  pair `RedisClientType<_M...>` (node-redis's `duplicate()` return, reached through the client's methods during the
  return-type inference of `createClient()`) is cut short by isDeeplyNestedType on both sides, so tsgo never measures
  that alias. Routing tttt's alias branch through the same invokeOnce (visited, deeply nested on both stacks, pushed on
  the object stack) takes karakeep 83.4k -> 71.9k types (1.43 -> 1.23). But: (1) tttt's `visited` is keyed by the pair
  alone where tsgo's InferenceKey also has priority, contravariant and bivariant: with alias pairs in it, excalidraw's
  delta.ts gets a spurious TS2345 (`(partial: Partial<X>) => Partial<X>` to `(partial: Partial<T>) => Partial<T>` infers
  from the parameter and then skips the return); keying alias pairs (or all pairs) like tsgo fixes that. (2) It costs
  instructions: everything keyed like tsgo effect +0.4%, valibot +0.6%, zod +0.4%; a separate tsgo-keyed table for alias
  pairs effect +0.3%; only the deeply-nested check on both stacks (no visited, no push) still effect +0.18% (the
  recursion identities of every alias pair at depth >= 1: ty_is_deeply_nested + ty_recursion_entry ~13M). Not
  committed for that; a cheaper recursion-identity check (memoized per type?) would make it pay. A patch is easy to redo:
  wrap the alias loop of infer_from() in the conditional branch's visited/deeply-nested/object-stack code.
  What's left after that: RedisClientPoolType's RESP (#3) variance measurement costs tttt 26k types and tsgo 300.
  Inside it tttt relates RedisClientPool<..sub..> to RedisClientPool<..super..> structurally (RedisClientPool #3 is "in
  out (unreliable)" on both sides, so both fall back to members), and its `multi()` returns RedisClientMultiCommandType
  intersections whose members are related pairwise (`RedisClientMultiCommand<[]>` to `WithCommands<[], ..., super-T,
  any>`, WithCommands to WithModules/WithFunctions/WithScripts...), instantiating CommandSignature over ~750 commands per
  marker. tsgo makes 300 types there: compare the nested relations of that one measurement with an instrumented tsgo.
  Also: alias_variance_applies() (relate.c) tests TYF_OBJECT where tsgo tests TypeFlagsObject|Conditional, which no
  intersection or union has: check whether tttt's intersections carry TYF_OBJECT (excluding them didn't change this
  case).

## Compare relation counts with tsc first (2026-10-06 perf session)
`tttt --time --checkers 1 -p X` prints each relation cache's size; `tsc -p X --noEmit --extendedDiagnostics` prints
tsc's (Assignability / Subtype / Strict subtype / Identity cache size, both count failures and nested pairs). A
corpus where tttt's count is several times tsc's is missing one of tsc's shortcuts: that is how vue's 228k strict
subtype pairs (tsc 15k) led to three ports worth -13% (measurements.md "Perf session"). To find the source, wrap
`ty_is_related()` with a temporary log of each top-level query that grows a cache by more than ~100 entries (current
node span, both types), and compare one such query with a patched tsc (`isRelatedTo` wrapped to print both types and
the result, memory note "instrument a tsc copy"); `TTTT_DEBUG_RELATE_NESTED=1` on a probe shows tttt's pairs. Left
after that session: tanstack-router's assignable 236k vs tsc's 174k (looked at briefly: the biggest growers are
variance probes of zod's classes from link.test.tsx, `Omit<UseLinkPropsOptions...>` against React's anchor props, and
expect-type's `KeyType` against a ~300-literal key union per assertion in link.test-d.tsx, all of which tsc relates
the same way as far as its code says; not compared with a patched tsc), vue's identity 18k vs 28k and type-fest/react
well below tsc (fine). On the pinned corpora tttt makes fewer types than tsc; on the source sweep a multiple of
tsc's is how this session's pathologies showed (next section).

## The source sweep finds pathologies (2026-10-08 perf session)
Rank `scripts/sourcesweep.py` projects by tttt/tsc time (median 0.075; read /tmp/sourcesweep-tttt.json against
test/sourcesweep/tsc.jsonl.gz, only projects tsc takes >= 2 s on), skip the ones tsc checks degraded (TS6305 unbuilt
references, thousands of TS2307/TS7031: tsc does less work there, babel-traverse, rxjs, better-auth), then
`TTTT_FILE_TIMES=1` for the file, a single-file `-p` probe (a temporary tsconfig with `files` next to the project's,
since paths and node_modules resolve from there; delete it after), and compare `--time`'s type count with tsc's
`--extendedDiagnostics`. **Type counts are a signal here**: every pathology found had tttt making 5-12x tsc's types.
Fixed that session: `typeof` of a top-level name made a literal generic (karakeep, 55 s), keyof of 70 unions
quadratic (headlessui), top-level object literal types instantiated per mapper (mcp-sdk, 5.5x types); later (agent ta) function expression
types keyed by the whole mapper (docmost/apps/server 2.19M types -> 153k, typecheck 2.7 s -> 0.4 s: now by what the
resolved signature mentions, `ty_function_type_mentions()`). Open:
- immutable-js/type-definitions: fixed by plan 59 stage 2 (canonical signatures, 997k -> 292k types, tsgo 564k).

## Parallelism (from the finished plans 36, 41, 44, 45; numbers in measurements.md)
- **Parallel declare and link** (ex plan 45 stage 3, "maybe never"): vscode's resolve phase was declare 40 + link 28
  ms against a walk of ~200 ms on 4 threads (510 serial; 2026-10-05). Declare writes `Binder.globals`, ambient
  modules and namespace exports shared by files: per-file tables merged in program order (tsc's mergeSymbolTable
  order). Making merged type parameters in declare (tsc makes them in its binder) would also let the files
  `binder_mark_serial_resolves()` walks on the main thread go parallel (18 files / 0.9 MB of vscode). Measure the
  serial remainder against the wall at vscode scale first: below ~10% isn't worth it. Plan 72 (2026-10-15) measured
  it: vscode 150 ms, webpack 48, ~8% / ~16% at 12 cores: "not now", after the loader's serial work (plan 72 stage 1).
- **Lexical import pre-scan for the load pool** (tsc's preProcessFile: `import`/`export ... from`, `require(`,
  `import(`, `/// <reference`) so a file's children are queued before its full parse. Discovery is a critical path
  for deep `.d.ts` import chains (nest: 4 workers no faster than 2 in the 2026-09-28 prototype), but the landed pool
  measured ~90% busy everywhere on a loaded machine (`TTTT_LOAD_POOL_STATS=1`). Re-measure nest on a quiet machine
  (4 vs 2 workers, "parsed by the loader" count) before building it.
- **Checker balance**: whole-file partitioning is bounded by the biggest file (tsc's checker.ts, zod's
  deep-partial.test.ts). Dealing by content size was tried and reverted (bytes are a poor cost proxy: JS vs
  declarations, and scattering a directory's files duplicates shared types); `deal_files()` cuts program order into 2
  runs per checker. Next: a cost model (per-file check time of a previous run, or type counts). Sharing types
  between checkers is not a lever (2026-10-08, measurements.md "Perf session"). tsgo 7.1's import-graph FENNEL
  partitioner (compiler/checkerpool.go), prototyped with bytes for node counts, duplicated 2-21% more types than the
  contiguous runs (2026-10-13, plan 62).
- **`-b` over many projects** (ex plan 41 stage 2): per-program name resolution of referenced projects' sources is
  the biggest phase of `-b` on tsc-src and typescript-eslint (summed over programs). Not done: a cross-program file
  cache (parsing is no longer the cost after `skip_typed_body()`), sharing resolved reference sources between
  programs (resolve writes into the AST, so only with the lib program's freeze-and-overlay treatment).
- **A lib cache** (parse the default libs once per process, or an mmapped pre-parsed lib, shared by `-b`'s
  programs): measured not worth it on the sweep (2026-10-05: median package 41 ms, startup ~6 ms). Revisit only for a
  watch/LSP mode or if `-b` profiles show lib loading.

## Leftovers of the 2026-10-05 perf session (todo entry 8)
- **react** (+3.7% from a4aa6c6b's distributive constraints of `T & {}`): `base_constraint()` is memoized at depth 0
  now (1.695 -> 1.682 G). The rest is first computations: ~120k instructions each through `ty_simplified` of nested
  indexed accesses (react-hook-form's Path chains). Reading the memo at depth > 0 too gave 1.648 but made the display
  of `Compute<A[K]>` (computedTypesKeyofNoIndexSignatureType) expand without end: something in
  `display_mapped` -> `expand_generic_homomorphic` -> `ty_apparent_type` relies on the depth cutoff of base
  constraints to stop. tsc caches every base constraint with a circularity stack instead of a depth cutoff; porting
  that (plan 39's stack) would allow the all-depth memo.
- **vue** (+3.4% from 9444083d): mostly inherent. Widened component-option literals are no longer subtypes of vue's
  weak options types, so ~630 multi-overload calls (`h(Comp, ...)` in vue's tests) fail tsc's subtype round and redo
  every overload in the assignable round, as tsc does. 7e159aa6 took a quarter back (arguments checked and related one
  at a time, tsc's getSignatureApplicabilityError). The rest is re-inference per overload round; only cheaper
  inference/argument checking in general would help (e.g. reusing a non-context-sensitive argument's type across
  candidates, beyond tsc, careful with freshness and contextual literal types).
- **Variance measurement order changes its cost** (triage 2026-10-05): deferring JSX element checks made react -5%
  with identical checks and identical variance results: react-hook-form's aliases (`FieldPathValue`, `DeepPartial`,
  `Omit`...) measured earlier went deeper into the recursive `Path<T>` chains (164 extra marker relations, ~80M
  instructions of instantiation/interning under `measure_variances`). Some depth limit or cache makes a measurement's
  cost depend on what was measured before; finding which could make measurements cheaper in every order.

## Where the time goes now (perf session 2026-09-29, after the fixes of that session)
Measure with `scripts/instrcount.sh` (all corpora, or `--bin` another build); bisect regressions by building old commits in
a worktree (`git worktree add /tmp/wt-perf <commit>`, symlink `thirdparty/{typescript,typescript-gopath,go,corpora}`, `meson setup`
once, then `git checkout -f X && ninja`) and running `scripts/instrcount.sh --bin`. Keyed caches in `my links`
(`intern_get`/`intern_slot` with a `KEY_*`) are the cheap fix for "the same computation again": the session's wins
were mostly tsc caches tttt lacked (two-member unions, intersections by members, assignment-reduced types).
- **drizzle** (15.7 G): variance measurements are 40% (207 of them, ~30 M instructions each, nested:
  `*SelectBuilder` -> `Create*SelectFromBuilderMode` -> `*SelectBase`). Compared with tsc (`VARIANCES=1
  scripts/tscinstcount.js`): the same flags, and tsc spends 100k-470k instantiations on the same ones. Not a tttt bug; making
  each measurement cheaper is instantiation speed in general (`instantiate_inner` 19% self, `intern_get` 10%).
- **vue** (8.2 G): `first_unfit_argument_skipping` (call.c) 13%: the conditional skipTrue hack (todo entry 5's
  plan-sized hacks). `ty_union_subtype_reduced` 3.5%.
- **valibot** (5.4 G): `identity_structured` 53% inclusive: expect-type's identity comparisons of schema types in
  `*.test-d.ts`, most of it `prop_type` resolving instantiated property types (inherent) and nested relations.
  `instantiate_inner` 12.6% self.
- **react** (1.56 G): react-hook-form's Path/FieldPathValue chains (hacks.md "Relations", relating whole chains as
  tsc does); `lower_node` of big literal unions. Relating a nested indexed-access source (`T[a][b]...[k]`) steps one
  index constraint per relation and recomputes each variant ~12 times (Maybe results inside aren't cached), about 4%
  of the react corpus; tsc computes the constraint of the whole chain in one getConstraintOfType.
- **tsc corpus** (4.6 G): flow is the biggest block: `type_at_flow_node` ~4% self, `narrowed_type` misses (type
  predicates like `isIdentifier(node)` over the `Node` union: 70k instructions each, cached per (type, candidate)),
  `narrow_by_equality`; then `check_related`/`is_simple_related` setup.
- **webpack** (3.7 G, +12% over f0dd10eb): flat profile; the growth is the JS features added since (expandos,
  CommonJS, JSDoc), not one regression.

## Memory (plans/67_memory_against_bun.md first: checker recycling, then the program; plans/75_memory_endgame.md the compact AST; measurements.md "Memory breakdown" the corpus table)
- **The AST's allocation** (plan 63, closed 2026-10-16; its measurements are in measurements.md "Plan 63"): the AST is
  in per-file arenas (`ast_alloc.h`). What is left of it:
  - **The drop is still a walk**: `block_drop()` frees the AST's malloc'd parts (cooked string literals and template
    texts in `cstr`s, `Value` strings, what resolve attaches: `AccessSite`s, rare structs made after the parse,
    ModuleRef rares written by module resolution) and is a no-op for the rest. An O(ranges) drop (-b frees 13
    programs in 119 ms on tsc-src, the test harness one per test) needs those in the arena too (`csview`s into it) or
    tracked per file; then `drop_source_files()` is `ast_arena_drop()` per file.
  - Lists are still grown by doubling and shrunk (`list_push`/`shrink_list`): the outgrown buffers are reused by
    later allocations (3% of vscode's 1.32 GB is still free when its file ends). Lists built on a per-parser scratch
    stack and copied out exact would remove the churn, not much memory.
  - **Expr 72 -> 40** (plan 75's census, vscode after names: 8.19M Exprs = 590 MB; 2.59M identifiers, 1.14M
    literals, 0.31M implicit any, 4.46M boxed variants using 8 of the union's 40 bytes): box IdentifierExpr and Value
    (146 `.identifier` and 78 `.literal` sites) and the union is a pointer. -262 MB of Expr slots, but each identifier
    box needs its name span too (identifier_expr_name() reads the Expr around it; Exprs move while vectors grow, so no
    back pointer): +56 B x 2.59M, literals +~16 B: **net ~-100 MB** (~-140 MB if the box drops the rare union, 21k
    users, to a side table). Plus one pointer chase per identifier in the checker: measure instructions first. A
    session of its own if memory against bun becomes the priority again.
  - Struct diet leftovers (plan 63 stage 1b, < 120 MB together, not scheduled): **Expr 72 -> 64** (the union is 40
    because of IdentifierExpr: `symbol` into the union when it differs from `target`, with a 2-bit kind and a per-file
    side table for the union's other users; 54 `identifier_symbol()` readers, every union writer through a migrating
    setter; a session of its own), **Binding element-only fields** behind a `BindingElement*` (37k of 700k bindings,
    ~-18 MB, ~110 readers), **Symbol** 152 -> 144 (bit fields; `is_assigned*` are written by resolve threads and must
    stay bytes), the **ResolvedModule** cache's failure hints behind a pointer (~-10 MB). PropertyExpr 128 -> 112
    needs its name span derived from the source text (lower.c's synthesized accesses name keys from elsewhere). Not
    worth it (measured): BinaryOpExpr, Statement, ClassItem. With the arena, sizes no longer round to malloc classes:
    every 8 bytes saved now counts.
  - Huge pages come from a budget read once from `/proc/buddyinfo` (`huge_pages_take()`): other processes taking
    blocks meanwhile, or the kernel's background compaction freeing more, aren't seen. The malloc heap (source text,
    checker intern tables and LazyMemo pages) gets none since the glibc re-exec went: the intern tables could move to
    mmapped, madvised memory if a profile shows their faults.
- Checker-side levers left by plan 60 stage 1 (2026-10-12, vscode numbers; the plan was deleted when plan 63 took over
  its AST and binder stages):
  - Where the checker's memory was at plan 60's start (e043e9e2; LazyMemo, CallResolution, PropDecl, Members and the
    interned lists shrank since):
    - **Checker malloc** (outside the arena, per checker, so 4x at K4): `lazy_type_evaluate_inner`'s LazyMemo pages
      **222 MB** (36k zeroed pages of 24-byte memos), the intern tables **202 MB** (3 tables, 16-byte entries, load
      0.375-0.75, a grow holds old and new at once), relation caches.
    - **Checker arena** 1.82 GB, diffuse: types 290 MB (2.56M, ~113 B), intern keys 244 MB (KEY_STRUCTURAL 75: object
      literal keys of 23.5 words; KEY_CALL_RESOLUTION 27), object literal Props with a full PropDecl
      (`ty_object_literal_alternative`) **212 MB**, instantiated Props (`instantiate_members`, 56 B) 129+29 MB,
      `CallResolution` (112 B) **127 MB**, `builder_finish` member arrays 104+50+40, `ty_list_copy` duplicate member lists
      44, plain signatures 44 + instantiated signature copies 31 (Signature 176 B, its spans copied into each instance).
      effect adds `namespace_export_prop` 49 MB (a 176-byte Prop+PropDecl per namespace export, only `symbol`,
      `name_span`, `ctx` used); drizzle `expand_mapped` + its `prop_decl_rare_mut` 27 MB.
  - **The rest of `links`** (2.6M entries on vscode, 4M slots + 24-byte blocks): its kinds hand out slots that callers keep
    across further interning (`void const** slot = intern_slot(...)`, ~50 sites), which an inline-key table can't. A kind
    moves to node links once every use of it is a plain get/set: worth it for the next big ones (KEY_FLOW_CALL 400k and
    KEY_FLOW_EXPR 245k are three words: a second inline table with two-word subjects, or pack (node, id)).
  - **8-byte intern entries** (hash fragment + compressed block pointer): growing has to rehash from the key blocks, a
    cache miss per entry (vscode ~12M over the run). Not worth it.
  - **1.5x growth for intern tables**: +1.2% zod instructions (longer probes, more rehashes) for a few MB either way; only
    node links grow by half. Peak RSS moves +-30 MB with where the last grow lands (vscode was 4866 vs 4892 between two
    growth policies): compare corpora as a set, never one.
  - **Instantiated Prop 56 -> 48** (no `decl` pointer): mapped type props get their PropDecl after the Prop
    (`expand_mapped()`'s `prop->decl = arena_new(...)` paths), so it can't be found next to the Prop by a flag. Would need a
    side lookup or allocating mapped props declared up front: ~-25 MB vscode.
  - **Signature's declaration fields behind a pointer**: saves 72 bytes per instantiation but costs 8 per declared one,
    vscode ~-11 MB. Not worth the churn alone.
  - **A compact declaration for object literal props** (1.2M on vscode, Prop 56 + PropDecl 96): PropDecl's sources
    (`node`/`lazy`/`function`/`symbol`) as one tagged pointer would take PropDecl to ~72, but `decl->node` & co have
    ~250 readers. -30 MB vscode. Same for `namespace_export_prop()` (effect).
  - Unexplained: `/proc` VmRSS sampled at the end of typecheck reads ~116 MB below `/usr/bin/time`'s peak on vscode, and
    strace shows no allocation after typecheck. Probably the kernel's per-CPU RSS counters lagging; use `/usr/bin/time`.
- 2026-10-12: tuple targets share their element type parameters (`Checker.tuple_params`) and tuple element props a
  read-only `ty_prop_decl_value_name`: type-fest 2.06 -> 0.80 GB. The rest of the old candidate list (a ~40-byte
  instantiated Prop, the `Signature` split, interned lists keyed by their ids) is plan 60 stage 1.
- **Recycling's cost is redone shared work** (2026-10-15 perf session): type-fest's +21% instructions at K1 (2
  recycles) is mostly one file, `extract-rest-element.ts` (0.3 ms warm, 157 ms in a fresh checker: a `TupleOf<999>`
  split an earlier file already did; tsgo takes 1.1 s on that file alone). drizzle's single recycle at ~90% of its
  share costs +14% instructions for -61 MB. Tried a tail rule (no recycle when the share's remaining bytes are under a
  quarter of what the segment checked, the checker allowed 1.25x its budget then): drizzle K1 -8% wall, but at the
  defaults (T4) no time gained and +60..90 MB peaks (drizzle, effect, type-fest): dropped. Nothing cheap predicts
  which files share a big type graph; a shared frozen type layer is the only real fix (plan 67 "options", rejected).
- **Duplication across checkers** (plan 67, 2026-10-14: small, +19% of vscode's arena at T4; each checker's own
  linear growth is the problem, which recycling bounds; no shared lib layer): files are dealt in contiguous runs (deal_files(), 10-05): drizzle 1688 -> 1337
  MB at K=4. One run per checker would be 1277 MB and cut valibot's *total* checker time 28% (837 -> 600 ms), but
  balances worse with bytes as the cost proxy. A better cost model (type counts or times of a previous run) or shared
  lib/global types between checkers would take the rest.
- **AST and binder diet done** (plan 43, 2026-10-05; numbers in measurements.md): every corpus is below tsgo's peak
  RSS at one checker now. Tools: `scripts/memtrack.sh -p DIR` (live malloc heap per phase by allocating function, with the
  share of all-zero words and Spans: 66% zero before, 45% after) and `scripts/arenasites.sh`. The pattern that worked: rare
  fields behind one owned side struct (`*_rare()` reads a shared all-null static, `*_rare_mut()` allocates on first
  write), absent optional children as null pointers, spans of one file packed as u32 offsets, bools as bit fields,
  walk-only scopes popped off the binder arena (hacks.md has the rules each relies on). Levers left, each a few MB:
  `Span` 24 -> 16 (pointer + u32 length + u32 file index: ~650 `.sv` uses; shrinks every node, `Prop`, `Signature`,
  `SymbolDecl`), `ModuleRef` 184 bytes (three failure-only strings), `ObjectExprItem` 224, `TypeNode` 72 (literal
  `Value` + span is the biggest variant; TypeNodes are copied by value everywhere, so a box needs an owner audit), the
  function parameter scope as a flag on the function scope (~4 MB on webpack, walks depend on meeting it),
  `LazyMemo` 24 bytes per AST node slot (~10 MB on webpack, dense pages). Node arenas (lever 7) stay a parser *time*
  lever: malloc/free/realloc are ~12% of nest's instructions, and `list_push`/`shrink_list` cost ~0.7% that lists
  built in a scratch stack and copied out exact would take back.
- Type counts on the pinned corpora are not the problem: tsc makes 718k types on type-fest (tttt 578k), 589k on zod
  (tttt 361k), 332k on valibot (tttt 385k). On sweep projects they were (section above).

## Time
- **Scaling probes** (2026-10-14 perf session, `scripts/scaleprobe.py [-k KIND] [-n N]`): one declaration or block
  with N members/statements/elements, timed against tsgo `--singleThreaded` (8k and 32k by default, ~3 min). Fixed that session (all were quadratic in tttt, linear in tsgo): array literal element contexts, statement
  reachability (tsgo's lastFlowNode memo), object literal / interface / class member duplicate and overload checks
  (name indexes: `src/name_index.h`, LiteralBuilder/MembersBuilder/MemberTables lazy indexes, ClassDecl namesake
  chains). Linear on both or better than tsgo: unions, switch, if/else chains, `+` and `&&` chains (tsgo takes 73 s
  on 8k `&&`), enums, namespaces, type literals, exports, overloads, tuples, JSX children, JSDoc functions, locals,
  calls, type alias chains. Superlinear on both: a chain of N classes each extending the previous (1000: tttt 16 s /
  4 GB, tsgo 365 s / 20 GB), CommonJS `module.exports.cN = ...` (32k: 13 s on both). Rerun after big
  checker changes; add a kind (the KINDS table) when a real project shows a slow file (`TTTT_FILE_TIMES=1`).
  **`--emit`** (2026-10-15 perf session: emitDeclarationOnly + declarationMap): six quadratic paths in the declaration
  walk and the map encoder fixed (one long line's map columns, object literal member lookup, class overload
  implementations, namespace export stripping, function properties, merged overload entries; e.g. 32k-member union
  of objects 57 -> 0.09 s, object literal 3.4 -> 0.05 s). Every kind is now linear with emit except what is
  superlinear on both sides anyway (tuple, union, switch, params, aliases, cjs). JS kinds: tsgo only (plan 64 stage 6).
- `jsdoc_before_ex()` walked back to the line start from every JSDoc host (minified lines: 3.9% of webpack), fixed
  2026-10-14; what's left of JSDoc in webpack is parsing the comments themselves (`parse_jsdoc_comment` ~2.3%).
- **Variance measurements** match tsc's reporting now (09-28: only where the marker occurs in the source, at tsc's
  four sites; Unmeasurable separate). Probe: zod with its `in`/`out` annotations stripped (sed over a /tmp copy of
  `packages/zod/src`, `.configs/` and `vitest.config.ts` alongside) costs 4.65 G vs 4.33 annotated (was 7.8). To compare
  variances with tsc, (tsc 6 era: patched a copy of `_tsc.js`; now a test/oracle overlay of tsgo's checker, after `links.variances = variances;` in
  getVariancesWorker print `symbol.escapedName` and the flags; tsc's Unmeasurable is 8, Unreliable 16, tttt's
  `TTTT_DUMP_VARIANCES` the other way round). ~25 of ~250 zod variances still differ in reliability, both ways
  (ordering of cached comparisons; `ZodTransform`, `ZodTuple`: tsc unreliable, tttt not).
- **Parser** (13% of the tsc corpus, ~25% of webpack, half of nest): no single function stands out. What's left is
  structural: a `Parser` copy per attempt, identifiers scanned again by each alternative that fails, one malloc per
  node. A real token (scan the identifier or punctuator once per position) and node arenas.
- **Name resolution** 78 ms on webpack, 89 ms on the tsc corpus, ~1 IPC (cache misses walking the AST): every
  identifier walks the scope chain (locals + container exports per level, value then type meaning on failure),
  `scope_check_strict_identifier` compares each name with 9 reserved words, `scope_declares_symbol` rehashes the name
  per scope in the loops of `unused_mark_referenced`/`check_static_type_parameter_reference`/`expr.c` (hash once, or
  keep the hash in `Symbol`). Declare + resolve are two AST walks.
- **Flow** (~12% of webpack): `type_at_flow_node` follows tsc; each variable read asks once now (TS2454's check
  reused the walk until 09-28).
- **Checker hot spots** (tsc corpus): `check_related` setup, `intern_slot`, `checker_atom` (`ty_member_name`
  atomizes member names of resolved declarations: atomize at bind time instead; the override checks' quadratic
  use of it is gone).
- **zod** (4.4 G): flat after the props_by_name fix (`intern_get` 4%, `instantiate_inner` 3%, `arena_alloc` 3%);
  generic call resolution is ~46% inclusive. valibot: `infer_from_returns` over schema objects' `~run`/`~standard`
  members and `ty_conditional_resolve`'s `infer X` walks.
- `find_prop`'s intersection memo caches whatever depth it was computed at (`depth > 8` gives "not found").
  `ty_intersection` distributes over non-primitive unions with no size limit (tsc: TS2590 at 100k).
- **Stack depth**: the parser and checker recurse once per nesting level; binderBinaryExpressionStress needs 4-8 MB
  with GCC 15 -O3 (passes at the default 8 MB). Logical chains (`&&`, `??`, `||`) still recurse per operand in
  `expr_lookup_names_logical`, the checker, `expr_compile_test_info_inner` and `expr_drop`. Threads get smaller
  stacks: matters for parallelism.
- The suite: the slowest tests left are the React-typings JSX ones (~1 s each: react16.d.ts per test).
- **Thread startup per program** (plan 36 stage 3): `baseline_tests --checkers 4 --threads 4` takes 38 s against 24 s
  at `--threads 1` (same output): ~1 ms per program for starting and joining 3 threads (8 MB stacks, glibc arenas).
  Irrelevant for one CLI run; a persistent pool (stage 1's load pool) fixes it for a watch/LSP mode or a threaded
  harness. With fewer checked files than checkers, the empty checkers still build their global types (~0.2 ms each).

- **valibot +2.5% instructions** since the corpus session (todo entry 8): generic homomorphic mapped types get index
  signatures from their constraint, eagerly instantiated. tsc's addMemberForKeyTypeWorker instantiates index
  values eagerly too (plan 42 planning), so the difference is *whether* the members get resolved: look for the caller
  that asks a generic mapped type's members where tsc relates/infers through the mapped type itself.

- **Tuples' `this` members, type-fest +0.2%, xstate +0.26%** (`retarget_this_props()`, members.c): a tuple's
  members re-instantiate the few Array members that mention `this` (sort, reverse, fill, copyWithin, every) with the
  tuple as this-argument. Only 3-5 props per tuple (2-8k tuples per corpus), but the cost stays when they're given the
  array's own mapper, so it comes from their being distinct Props downstream (lazy prop types resolved again,
  pointer-equal shortcuts lost), not from building them. A lookup-time substitution (share the array's props, apply
  `this` when one of those is read off a tuple) would take it back; tsc pays the full per-reference instantiation.

## Pathologies
- **webpack loaded twice: 89 s** (tsc: 11 s on the same program). Run from a worktree whose `thirdparty/corpora` is a
  symlink (`cd /tmp/wt; tttt -p thirdparty/corpora/webpack`), the root files keep the symlinked path and
  `node_modules/webpack -> ..` realpaths to the other copy: two webpacks, 130 diagnostics (tsc gets 24 of its own).
  realworld.py now passes the real path, but some relation between the two copies' structurally-identical types
  runs away in tttt; a callgrind of that run would show which (probably the relation budget/recursion identity).

## Base constraints of expect-type's conditional constraints
In valibot's `*.test-d.ts`, `ty_base_constraint` of expect-type's `Expected` (constrained by
`StrictEqualUsingTSInternalIdenticalToOperator<Input, Expected> extends true ? unknown : MismatchInfo<...>`) took
5-10 ms per type parameter (about 250M instructions over the corpus, 5%) when constrained type variable reduction asked
for it; `primitive_constraint_of` (checker.c) now skips deferred declared constraints because of it. Worth a look: tsc
computes the same constraint without it showing up.

## A later resolution lowers more (2026-10-09)
9dee2f20 (skip the source property's type against an any/unknown target property, tsgo's isPropertySymbolTypeRelated)
cost type-fest +0.8% because the skipped property types got resolved later by costlier paths (+128k lower_node calls
from ty_cond_branch_type / tuple_element_type); limited to getters in d361bdba. Why a later resolution lowers more (a
memo that only fills on the early path?) isn't known: worth finding, it may cost elsewhere too.

## Plan 59 stage 2 leftovers (agent B)
- **rxjs/packages/rxjs: fixed** (types ratio 2.62 -> 0.45, RSS 1943 -> 477 MB). It was not a missing cache: three spec
  files' `expectTypeOf(x).toEqualTypeOf<Observable<...>>()` mismatches print expect-type's MismatchInfo constraint, a
  mapped type over Observable's ~110 methods (each a DeepBrand of its overloads). tsgo truncates the message at ~160
  characters and never resolves the rest of the properties (with `--noErrorTruncation`, tsgo makes 2.34M types on the
  one-assertion probe, against 200k without it). tttt printed all of it. Now `display_over()` in ty_display.c (hacks.md).
  Its property order still differs (tttt puts the unique-symbol keys first), so the elaboration names `[buffer]` where
  tsgo names `drop`: the union order of `keyof A | keyof B`.
- **immutable-js/type-definitions** (1.77, handed to agent C, who found the same file under sass in quill, vite, vitest,
  soybean-admin and lucide): all of it is the interface heritage checks (OrderedMap -> Map: 717k tttt types against
  92k for tsgo). Nested relation counts are similar (97k vs 87k), but tttt makes ~1.8x the anonymous instances (606k
  vs 336k) and type references (185k vs 106k), with fewer signature instantiations than tsgo (tsgo: 412k
  instantiateSignature, 76k cloned type params). 233k of the 606k are made in `signature_resolve`'s eager
  instantiation of every parameter type. tsgo instantiates parameter symbols lazily. Lead: lazy SigParam types.
  Instrumented-tsgo recipe: patched copies of checker.go/relater.go built with `go build -overlay X.json -o /tmp/...
  ./cmd/tsc` from `thirdparty/typescript/tsc`, so built/ isn't touched.
## Plan 59 stage 2 leftovers (agent C)
- **Lazy parameter types of instantiated signatures** (tsc's instantiateSymbol: an instantiated signature's parameter
  symbols get their types when asked for). `signature_resolve()` instantiates every parameter type of an instantiated
  signature eagerly, so signatures that are only erased (overloads compared pairwise), counted or base-instantiated
  still make their parameter types: immutable.d.ts's `merge<KC, VC>(...collections: Array<Iterable<[KC, VC]>>)` made
  an `Iterable<[KC', VC']>` (and tuple, and Array) for every clone of KC/VC, 11.7k against tsgo's 0.7k. Prototyped
  (2026-10-12, /tmp/c-probes/full3.diff while it lasts): `SigParam.type` renamed to `lazy_type` plus a `pending`
  mapper composed with each further instantiation (`ty_compose_mappers()`), read through `ty_param_type(me, param)`
  (~90 read sites, the compiler finds them all once the field is renamed); rest parameters stay eager unless they are
  arrays (`ty_signature_has_rest()` and friends read the shape without a checker); infer.c's `base_signature_mapper()`
  became a cached base signature (tsc's getBaseSignature) so that its parameter types stay lazy too. Suite, corpora,
  sweep, $ExpectType and source sweep sample 4 were all unchanged. Not landed: on top of the canonical signature fix
  it only took immutable.d.ts from 292k to 254k types, while composing mappers cost valibot +0.25% and zod +0.2%
  instructions (`ty_compose_mappers()` ~2M, extra arena and intern traffic). Worth it with a cheaper composition (tsc's
  composite mapper is lazy: maps through inner then outer on lookup, no up-front instantiation of inner's targets).
- **immutable.d.ts after the canonical signature fix**: 292k types against tsgo's 595k (the whole sweep's immutable
  projects are at types ratio 0.43-0.57 now, from 1.3-1.8). Nothing left there.
- (2026-10-14, agent C) TS2574's check of generic variadic tuple elements (type.c, tsgo's checkTupleType ->
  isArrayLikeType, the same relation tsgo runs) costs type-fest +0.84% instructions (19.08 -> 19.25 G). 60 checks;
  three `...MergeDeepArrayOrTupleElements<PickRestType<D>, PickRestType<S>, Options>` (merge-deep.d.ts:167/193/219) take
  ~22 of the 25 ms: relating that conditional to `readonly any[]` goes through its default constraint into the whole
  MergeDeepOrReturn machinery (resolve_conditional_instance/branch_type_with dominate the callgrind delta). Worth a look
  if tsgo is much cheaper there: relate.c conditional_source_related() relates the branches before the distributive
  constraint (tsgo: distributive first) and again after it when that fails.
