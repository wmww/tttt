# Perf leads
Numbers are in `measurements.md`; this is what to try next. Profile with callgrind (`valgrind --tool=callgrind
./release/tttt -q files...`, then `callgrind_annotate [--inclusive=yes]`; no `perf` on the dev box). The release build
has no debug info: for line-level annotation (`callgrind_annotate --auto=yes`) build a copy with
`meson setup /tmp/relg --buildtype release -Dc_args=-g` (same code, same instruction counts). Compare
instruction counts across sessions, not wall times. `--time` prints the resident set after each phase and the
checker's arena size and type count. Memory: `scripts/memtrack.sh -p DIR` for the malloc heap (live blocks per phase by
allocating function; massif's extra-heap estimate is wrong for glibc), `scripts/arenasites.sh -p DIR` for the mmapped binder
and checker arenas by call site.

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
- **immutable-js/type-definitions**: 997k types vs tsc's 203k; ~600k anonymous instances spread over hundreds of
  member types (4-parameter mappers, ~8.9k instances each). Member types are lazy (`instantiate_members`), so
  something reads every member of many distinct instantiations: probably structural relations of `this`-returning
  interface instantiations where tsc goes by variance. It's all the check of the one `immutable.d.ts` (merged
  interface + namespace + function declarations): assignable cache 67.8k vs tsc's 23.8k, identity 28.9k vs 8.3k. Find
  the top-level relation queries that grow the caches most (the recipe above).

## Parallelism (from the finished plans 36, 41, 44, 45; numbers in measurements.md)
- **Parallel declare and link** (ex plan 45 stage 3, "maybe never"): vscode's resolve phase was declare 40 + link 28
  ms against a walk of ~200 ms on 4 threads (510 serial; 2026-10-05). Declare writes `Binder.globals`, ambient
  modules and namespace exports shared by files: per-file tables merged in program order (tsc's mergeSymbolTable
  order). Making merged type parameters in declare (tsc makes them in its binder) would also let the files
  `binder_mark_serial_resolves()` walks on the main thread go parallel (18 files / 0.9 MB of vscode). Measure the
  serial remainder against the wall at vscode scale first: below ~10% isn't worth it.
- **Lexical import pre-scan for the load pool** (tsc's preProcessFile: `import`/`export ... from`, `require(`,
  `import(`, `/// <reference`) so a file's children are queued before its full parse. Discovery is a critical path
  for deep `.d.ts` import chains (nest: 4 workers no faster than 2 in the 2026-09-28 prototype), but the landed pool
  measured ~90% busy everywhere on a loaded machine (`TTTT_LOAD_POOL_STATS=1`). Re-measure nest on a quiet machine
  (4 vs 2 workers, "parsed by the loader" count) before building it.
- **Checker balance**: whole-file partitioning is bounded by the biggest file (tsc's checker.ts, zod's
  deep-partial.test.ts). Dealing by content size was tried and reverted (bytes are a poor cost proxy: JS vs
  declarations, and scattering a directory's files duplicates shared types); `deal_files()` cuts program order into 2
  runs per checker. Next: a cost model (per-file check time of a previous run, or type counts). Sharing types
  between checkers is not a lever (2026-10-08, measurements.md "Perf session").
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

## Memory (measurements.md "AST and binder memory" has the table: below tsgo everywhere at one checker)
- **Arena by call site and interned keys by kind**: `scripts/arenasites.sh -p <corpus>`. drizzle at one checker (10-05, 495
  MB arena): types 88 MB (136 bytes each), interned keys ~60 MB (KEY_ANONYMOUS_INSTANCE 15, KEY_CONDITIONAL 13 (node
  pointers and mapper pairs per branch), KEY_CONDITIONAL_INSTANCE 9), instantiated `Prop`s 68 MB (56 bytes),
  `ty_list_copy` 65 MB (union/intersection/reference member lists: the key holds the same ids again), scopes/symbols
  of name resolution ~50, `builder_finish` member arrays 44, `expand_mapped` 24.
- Next steps if memory matters again: a ~40-byte instantiated Prop (no `decl`/`parent`, plan 42); `Signature`'s
  declaration-only fields (two Spans, class_instance, declared_by: 80 bytes) behind a pointer like PropDecl; Ty's
  `anonymous` member could be 88 bytes but `declared` is 96 (the union's size); an interned list whose key *is* its
  member ids (a u32 id array in the type, pointers looked up) would drop `ty_list_copy`'s duplicate.
- **Duplication across checkers**: files are dealt in contiguous runs (deal_files(), 10-05): drizzle 1688 -> 1337
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
