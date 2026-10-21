# Plan 86: multiples on many-core cold runs

Written 2026-10-19 from a conversation with the human (git 318b2a58's notes; measurements.md "Many-core accounting on
the 12-core box"). The human asked how to get **multiples, not percentages** on multi-core cold runs of large
projects. The answer: no single architectural bet gives them; three programs of 1.3-1.7x each compound to **2.5-3x
on vscode** (2.68 s -> ~1.0 s on the 12-core box), in a specific order. This plan is the order and the gates. **The
next insights session schedules it**: stages 1-3 fit perf sessions in any window; stage 4 is its own window.

## Order (insights of 2026-10-20: supersedes the stage order below)
Stage 4 comes next, ahead of stage 2's remaining sessions, which are demoted to opportunistic perf slots. The reasons:
- The human asked (2026-10-19) for the big many-core wins first. Stage 2 session 1 found a flat profile and took
  ~2%. Its 1.5-2x target rests on a 42% instruction-footprint share that cachegrind can't confirm without hardware
  counters, so it is the weakest lever we have.
- bun canary (oven-sh/bun#45800) beats tttt's wall at the defaults on vscode, drizzle, excalidraw and nest, while
  using 1.7x tttt's CPU. It keeps ~20 of 24 threads busy; tttt keeps ~10 of its 12 checkers busy, and more checkers
  don't help (K24 is +40-160% CPU for 0-7% wall) because each one rebuilds the same instantiations and recycles. Only
  a shared graph makes the SMT half of the machine useful, and it is also the only lever on memory at the defaults
  (bun is lower on 10 / 14).
- The gate already said go (below). What's left before the build-out is a design and a prototype, with a go/no-go.
Sessions: **design** (done 2026-10-20: go, staged, ~25 sessions; "The design" under stage 4), then the **taint
prototype** (4-0, done 2026-10-20: G0 says go with three conditions, "Gate G0 result"), then K1 refactors (4a-4c) up to gate G1, before anything is shared. If a gate says no,
the fallback is stage 3 (front end) plus perf_leads.md's snapshot recycle, which goes after the thrashers (supabase
studio: `--checker-memory off` takes 2.27 -> 1.30 s). Stage 3's cheapest piece, starting the costliest file first, is
done (2026-10-20: TypeScript6 -6% at K4, type-fest -2%, the rest flat on 4 cores).
The twenty-fifth window (insights of 2026-10-20) is 4a, three sessions: plan 88 stage A (one barrier stack) is 4a's
substrate for per-checker in-flight records, and plan 88 stage B (barrier termination instead of "once" flags) is
decided inside 4a session 2. Session 1 writes the overlay record's shape and API into "The design" for the rest.

## The budget (vscode `-p`, the 12-core box, 2026-10-19)
| part of the 2.68 s wall | today | floor | what moves it |
|---|---|---|---|
| front end: libs 0.04, load+parse 0.33, resolve 0.25 | 0.62 s | ~0.25 s | stage 3 |
| check: inherent per-file work at all-core clocks (14.2 s / 12) | 1.18 s | shrinks only with single-threaded speed | stages 1, 2 |
| check: duplication (~1.2x), recycling (5-8%), contention, imbalance | 0.88 s | ~0.1 s | stage 4 (and 2's allocation cuts) |

Facts the plan rests on (all measured, the commands are in measurements.md):
- **All-core clocks cost 1.62x per checker before tttt does anything**: a single checker's per-file time sum goes
  from 8.8 to 14.2 s when the other 11 cores merely spin. No design removes it. 8.8 x 1.62 x 1.19 (plan 72's K12
  duplication) = 17.0 s against 17.1 s measured at K12 without recycling: the model closes.
- Plan 85's census measured at `--threads 1` (serialized checkers), which hides clocks and memory contention.
  **Price many-core work by per-file time sums under real parallel runs** (`TTTT_FILE_TIMES=1`), with the
  spinning-cores row as the floor. Report wall / CPU / peak as the window rules say.
- The checker is stall-bound, not instruction-bound: ~1.5 IPC at boost, 3 last-level misses per 1000 instructions
  (drizzle: ~1.5 s of DRAM latency hidden in 0.85 s), I1 miss rate 2.1%. Function profiles are flat (nothing above
  6% since 2026-10-16), so the single-threaded levers are structural (layout, allocation volume), not hot spots.
- A shared declared layer (plan 85) is 2-6% of the re-derived work; what multiplies is instantiations of the
  project's own generics (measurements.md "Plan 85's census"). A shared store only pays if it holds those.
- Beyond ~3x the only known route is warm state across runs (daemon, incremental cache), parked by the human (plan
  76). Not this plan.

Expected compounding on vscode here: 2.68 -> ~2.5 (stage 1) -> ~1.6 (stage 2 at 1.6x) -> ~1.4 (stage 3) -> ~1.0
(stage 4). drizzle 0.39 -> ~0.15 (its duplication is 1.4x serialized, 4x in per-file time at K12, so stage 4 is
worth more there). The thrashers (dokploy, monkeytype) are a different problem: perf_leads.md "Shared warm state"
(a snapshot-style recycle); they are not this plan's measure.

## Stage 1: ship PGO + LTO — done 2026-10-19
`scripts/pgobuild.sh` (`pgo/tttt`, retrained from a fixed set, drizzle zod effect excalidraw webpack at K1 and the
defaults, whenever a source, the compiler or the script changes; vscode and tsc stay out of it as the untrained check).
Callgrind -11.5% (nest) to -16.9% (excalidraw), vscode -15.2%; -8-9% wall; output byte-identical. Pitfalls: a stale
profile is `-Werror=coverage-mismatch` per object (the script retrains after any change, `ty.h` touches everything),
and a new LTO warning is to be read, not silenced (ty_buf_push()'s malloc(0) was one). The human's 12-core rerun is
owed (report.md).

## Stage 2: the single-threaded memory-system program (opportunistic since 2026-10-20; was 8-12 sessions)
Target: **1.5-2x on single-threaded check time**, measured as instructions (`scripts/instrcount.sh`), cachegrind
misses (`valgrind --tool=cachegrind --cache-sim=yes` on drizzle and tsc at K1: LL misses per 1000 instructions, I1
miss rate) and wall at K1 and at the defaults. Each session picks the largest measured source of misses, not a
function. Candidate levers, to be ranked by cachegrind per function (`cg_annotate`) in the first session:
- **Type layout**: a `Ty` next to what reading it touches (members, signatures, type arguments, order key), hot
  fields in the first line, cold ones (display, declaration-emit, diagnostics state) out of line. Plan 63's struct
  diet shrank sizes; this is about adjacency and the number of lines a relation or instantiation touches.
- **Memo and intern tables**: `LazySlots` is a three-level sparse table (three dependent loads per memo read);
  `InternTable` probes with `u64` keys of ids and pointers (a random line per lookup, two for the entry it points
  at). Candidates: a flat per-file slot array (slots are already handed out per file), keys that carry the hash,
  entries that carry the first key word so a miss costs one line.
- **Allocation volume**: K1 vscode faults 215k pages; at K12 400k and 2.4 s of kernel time. Arena chunk reuse
  across recycles (no munmap/re-fault), fewer short-lived builders (`scripts/arenasites.sh` ranks allocating
  sites), and every byte not allocated is bandwidth the other 11 cores keep: this is also the lever on the ~1.2x
  contention at K12.
- **Instruction footprint**: I1 2.1% on drizzle. PGO (stage 1) takes most of what layout can give; what's left is
  code size on hot paths (relate.c at the inline-unit-growth limit, giant switch functions).
- The parser's structural costs (perf_leads.md "Time": a `Parser` copy per attempt, identifiers rescanned, malloc
  per node) belong to stage 3 but are measured the same way.
Rules: K1 changes measured by instructions and cachegrind before wall; output byte-identical (`scripts/oracles.py`);
no change that trades memory for time past plan 81's frontier.

### The ranking (session 1, 2026-10-19; `scripts/cgrank.py`)
Cachegrind on pgo/tttt at K1 (simulated I1 32 KB, D1 48 KB, LL 32 MB). drizzle: 5.56 G instructions, I1 miss rate
1.63%, D1 2.7%, 2.26 LL misses per 1000 instructions; tsc: 3.92 G, 1.55%, 2.4%, 1.55. `/usr/bin/time` at K1: drizzle
1.2 s user for 5.56 G instructions, about 1.2 instructions per cycle. Without hardware counters
(perf_event_paranoid 4) cachegrind's misses go through a model, `cgrank.py`'s stall estimate (D1mr*12 + DLmr*150 +
I1mr*10 + DLmw*20 cycles): drizzle 2.15 G cycles, tsc 1.40 G, so about half of the cycles. That fits the measured
IPC. Shares of the estimate:
1. **Instruction footprint, about 42% on both** (drizzle 0.90 G, tsc 0.61 G). Spread out: the top function,
   instantiate_inner, has 7% of drizzle's I1 misses. The rest is relate.c, members, the flow walk and the call
   resolver. GCC 15's `-fipa-reorder-for-locality` on top of PGO+LTO changed nothing (I1 misses within 1%), so these
   are capacity misses: the hot code doesn't fit in 32 KB, and function order doesn't matter. Only smaller hot paths
   would help: outlining cold arms of the giant switch functions (instantiate_inner, is_related_to's,
   structured_related_members) and cutting duplicated inlined bodies. The real cost is uncertain: the uop cache and
   the next-line prefetcher hide part of what cachegrind counts. Without counters, wall time is the only judge, so
   this is a large lever but an unverified one.
2. **The hash tables, about 10% on drizzle and 5% on tsc**: Checker.types and .links (drizzle 443k and 256k entries,
   16 + 8 MB) and node_links (tsc: 16 MB, ty_link_get is tsc's top line). Each lookup's first probe is a random
   line in a table the size of the LL. The rest is not a cost: the key compare at a hit costs little because the
   caller reads the same block anyway. intern_grow's rehash and its memset are sequential, so the prefetcher hides
   them on real hardware. The get-miss → compute → intern_slot re-probe was measured: it was already cached.
   intern_lookup()/intern_insert() (this session) save its hash and probe, drizzle -0.5% instructions, nothing in
   misses. What's left is the footprint: 16-byte entries at 37-75% load. Candidates: 8-byte entries (a hash tag plus
   a 48-bit pointer; rehashing then needs the hash bits from somewhere: the tag can't hold the index past 2^19
   entries), splitting the hottest kinds (KEY_ANONYMOUS_INSTANCE, KEY_CONDITIONAL_INSTANCE: 40% of drizzle's
   lookups) into a table of their own, or fewer lookups (a memo on the Ty for the commonest kind).
3. **Ty header reads in hot predicates, about 5%**: `ty->flags` in ty_instantiate()'s inline test (drizzle 700k D1
   misses), ty_get_prop, union member walks. These are pointer chases over the type graph. **Taken for tsc**:
   `x.kind !== SyntaxKind.X` filtered the 400-member SyntaxKind union every time (is_not_unit_comparable_to was tsc's
   top D1 function, 8% of its misses). It is memoized now (KEY_FLOW_NOT_UNIT, flow.c without_unit()): tsc -2.3%
   instructions, -7% D1 read misses. Other big-union walks (narrow_by_equality's assume-true side, discriminant
   narrowing) are the same shape.
4. **First writes to fresh memory, 8% of the estimate but mostly an artifact**: 70% of drizzle's LL misses are
   writes, and memset (intern_grow 86%) plus memcpy (intern_slot's key copy, builder_finish, ty_list_copy,
   ast_realloc) are 36% of those. On real hardware the kernel zeroes a page just before the first write, so the
   lines are already cached. What's real is the page fault (drizzle 0.1 s sys out of 1.3 s): the allocation-volume
   lever above.
5. **Cold source text, 1.3% on drizzle and 2.6% on tsc**: memchr in source_file_make() reads every file's text for
   the first time. Fusing it into the scanner is stage 3's business.

Next sessions, in order: (a) the instruction footprint, by outlining the cold arms of instantiate_inner and the
relation switch, judged by I1 misses in cachegrind and by wall at K1, since hardware counters are unavailable; (b)
the remaining big-union walks; (c) the intern table footprint.

## Stage 3: the front end to its parallel floor (4-6 sessions)
0.62 s -> ~0.25 s on vscode at the defaults. After stages 1-2 it is a quarter of the wall. Plan 72 measured the
pieces; its "not now" on parallel declare/link was at 8% of today's wall.
**Measured order (2026-10-20, measurements.md "Costliest file first and the front end's split")**: the loader's
own serial work is 279 ms on vscode (201 ms of it parsing files the pool hadn't reached), declare + link 138 ms
serial, the walk parallel. So the pre-scan goes first, then declare/link, then the parser. Costliest-first is done
(`share_first`); the critical file's deferred-body materialization (~65 ms for checker.ts) is what's left of
TypeScript6's tail, so the costliest file's bodies are a candidate for the loader's pool too.
- **The loader off the critical path**: a lexical import pre-scan (`import`/`export ... from`, `require(`,
  `import(`, `/// <reference`) so workers queue a file's children before its full parse, and the loader thread's own
  work (plan 72 stage 1 cut it to ~0.3 s) measured again on the 12-core box with `TTTT_LOAD_POOL_STATS=1`.
- **Parallel declare and link**: per-file tables merged in program order (tsc's mergeSymbolTable order), merged type
  parameters made in declare so `binder_mark_serial_resolves()`'s files go parallel too (perf_leads.md
  "Parallelism"). 150 ms serial on vscode.
- **The parser**: a real token per position and node arenas (perf_leads.md "Time"); 0.9 s single-threaded on
  vscode's 138 MB, ~150 MB/s. It parallelizes already, so this is CPU (and bandwidth) more than wall.
Validation: `scripts/threadcheck.sh`, scenarios, the source sweep (name resolution order is observable: merged
declarations, TS2300/TS2451 order), `tttt --time`'s phase lines on vscode, webpack, effect at `--threads 1` and the
defaults.

**Stage 3 session 1 (2026-10-21, worker "frontend", a box at load 25-45: counts and instructions, not walls)**:
- A main-thread wall sampler (`bench/sampler/`: LD_PRELOAD, `timer_create` on the main tid, `report.py` with
  `--under/--exclude/--callers`) stands in for perf, which this box forbids. It showed the loader waiting a quarter of
  its time on the resolver's single mutex, stat()ing every queued path, freeing the workers' resolutions (their malloc
  arenas' locks) and the config glob stat()ing every entry. Fixed: resolver caches sharded 64 ways, shared (boxed,
  uncopied) resolutions (`module_resolver_resolve_shared()`), sizes stat()ed by the workers, roots queued under one
  lock, `d_type` in the glob. vscode T12 load+parse ~335 -> ~225 ms in the one quiet window.
- The loader's own parses were a scheduling problem, not discovery: `TTTT_LOAD_POOL_INLINE=1` showed nearly all of them
  were roots queued hundreds of ms earlier and passed over by biggest-first. Workers now take files by rank (the
  index path from the loader's first wants: preorder, the order the loader takes them), files >= 128 KB still first:
  vscode T12 inline parses ~1340 -> 350-500. **The pre-scan is not built**: what is left inline is deep `.d.ts` chains
  found just in time and workers starved by the load. Decide it on a quiet 12-core box with `TTTT_LOAD_POOL_INLINE=1`
  (how many inline parses are of files queued < 1 ms before, i.e. found just in time).
- Parallel declare done (`program_declare_files()`, `declares_in_isolation()`): 10604 of vscode's 11015 files.
  Left: **link** (~50-60 ms on vscode at 12 cores: `binder_link()`'s alias resolution is order-dependent: TS2303
  cycles, placeholders by index), the 411 serial files (libs), and the walk's serial files.

## Stage 4: a shared concurrent type graph (~25 sessions over several windows, gates G0-G2: "The design" below)
The paste's "declarations first, bodies second" idea, built where the duplication actually is: **one hash-consed
intern table for every kind of type, grown concurrently by all checkers**, instantiations included; once-published
lazy fields (resolved members, signatures, base constraints, variances) with a publish-once protocol; relation
caches, inference contexts and expression memos per worker as scratch. Worth ~0.25-0.45 s on vscode (duplication +
recycling + the L3 share of twelve copies of the same types), 1.4x on drizzle, and memory at the defaults falls
K-fold, which is the memory target the 12-core numbers miss (vision.md). What breaks today's design: recycling
(a checker's arena goes with its types; a shared graph needs a different memory story, possibly none: K1 holds
vscode in 1.4 GB), and determinism.
- **Gate session first** (one session, no design before it): instrument `ty_compare_tie()` and count, on every
  corpus and the sweep's biggest projects, how often a union's order is decided by the type-id fallback rather than
  tsgo's keys (names, declaration positions, symbols, fresh twins). If it is near zero and the remaining cases can
  be given a structural key, ids assigned by arrival order are harmless and the design is open. If not, stop here
  and write the number into this plan.
- **Gate result (2026-10-19): go, with two conditions.** Instrument: `TTTT_ORDER_STATS=1` counts ty_compare_tie()'s
  type-id fallbacks by site and kind (anonymous objects by origin); `TTTT_ORDER_SEED=N` permutes every non-intrinsic
  id fallback (a bijective hash), so whether the id order is observable is a diff of the oracles' output.
  - Rate: ties are ~0.1-1.4M a corpus at K1 (vscode 880k, excalidraw 1.19M, typescript-eslint 1.42M). The id
    fallback decides 0.003% (tsc 39) to 0.6% (zod 450) of them. Most are intrinsics (any vs auto any, never vs
    silent never), and every checker makes those first in a fixed order, as a shared graph would, so they're exempt.
    Non-intrinsic: vscode ~510, excalidraw ~810, webpack ~560 (515 references), effect ~180, the rest under 200.
  - The one big class was the alternatives of a spread of a union (`{ ...u }`: anonymous objects with the literal's
    symbol, which tsgo also orders by id, its cross product's creation order): ~2600 of vscode's, 3243 of
    excalidraw's. It now has a structural key (`spread_alternative`, in ty_compare_tie()), which also fixed
    noImplicitAnyUnionNormalizedObjectLiteral1 (tttt's widened twins had come in another id order than tsgo's).
  - Observability under three seeds: no change in the output of any corpus (vscode, tsc-src, typescript-eslint
    too), DT 8595 / 8595, `$ExpectType` 68640 / 68640, source sweep 5225 / 5242 (seed 1). The suite moves one test,
    dynamicImportsDeclaration (`{ default: typeof import("./case0.js") } | ...`: the synthetic default objects,
    cached per module, ordered by first use like tsgo). Before the spread key 22 tests moved, one in its
    diagnostics (strictOptionalProperties1: TS2322 became TS2412), and webpack's output moved while intrinsics
    were still permuted.
  - Condition 1: ids come from one monotone counter (an atomic), so the types one worker makes in one operation keep
    their relative order. The residual classes get structural keys or a reason they can't be observed: the
    synthetic default (the target module's program order, a guess against tsgo's first use), a literal's
    fresh/regular/widened twins (derivation stage), cloned type parameters sharing one symbol (vscode 55, webpack
    18), `anon-other` (excalidraw 800, vscode 141, not classified yet), webpack's 515 references.
  - Condition 2, not about unions: is_deeply_nested_as() (relate.c, tsc's isDeeplyNestedType) counts a recursion
    identity only when each type is newer (a higher id) than the one before it. A type another worker interned
    earlier is "older" where a private checker would have just made it, so the recursion cutoff would depend on the
    schedule. The design session must replace "newer" with something schedule-independent (a per-worker creation
    stamp for the types this relation made, say) and show it holds on the suite. flow.c's key_entry_cmp() sorts
    by id only for a binary search, which can't be observed. Other `->id` uses are hash keys. Answered below
    (measurement 2): observable under random ids; first-touch stamps if global ids prove observable.

### The design (session of 2026-10-20): go, staged, ~25 sessions
Inputs: two read-only censuses and bun's design (`86_stage4_census.md`, the appendix), and two measurements made
here (scratch builds, reverted).

**Measurement 1: what duplication costs, and what a warm start buys.** Each checker silently checks a seed first:
the first W% of checked source in program order, minus its own share's files. The table gives the per-file check-time
sums of the real checks, serialized (`--threads 1`, so this is work, not SMT or clock inflation), no recycling
(`--checker-memory off`), no stealing (s):
| corpus | K1 | K4 cold | W 10 | W 25 | W 50 | W 100 | K12 cold | W 10 | W 25 | W 50 | W 100 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| drizzle | 1.14 | 1.54 | 1.51 | 1.52 | 1.21 | 0.94 | 1.91 | 1.75 | 1.72 | 1.16 | 0.85 |
| excalidraw | 1.18 | 1.45 | 1.41 | 1.28 | 1.11 | 1.07 | 1.70 | 1.46 | 1.30 | 1.17 | 1.09 |
| zod | 0.94 | 1.05 | 1.08 | 1.05 | 0.99 | 0.93 | 1.32 | 1.28 | 1.23 | 0.97 | 0.92 |
- **The duplication is real and grows with K**: at K12, check work is 1.40x (zod), 1.44x (excalidraw) and 1.68x
  (drizzle) K1's. That is more than plan 85's whole-CPU census showed (its front end diluted it). A perfectly shared
  graph brings the sum back to about K1 (W 100 is K1 minus the first-use costs that someone has to pay once).
- **A frozen seed doesn't get it**: a 10-25% program-order prefix removes little, and only 50% removes most of it. Each
  region of the program builds types the others need too. So the "fork every checker from one frozen K1 prefix" design
  (no concurrency at all) is dead: half the check would have to run serially first. Only concurrent sharing pays.
- Real K4 runs (4 threads, same box) say the same through noise: drizzle cold 2.9 s of per-file sum, W 100 1.4-1.9, K1
  1.1-1.2.

**Measurement 2: gate condition 2 (is_deeply_nested_as()'s "newer id" test).** The rule (tsgo's isDeeplyNestedType)
counts a stack entry only if its id is at least the last counted one's. Instrumented: on the suite it decides the
answer **2689 times** (the result with and without the test differs), on the corpora 5 (nest) to 6926 (vue) times.
With ids permuted at random for this test alone, the suite moves 3 tests (complexRecursiveCollections,
inferFromNestedSameShapeTuple, ttttDeferredReferenceNormalizedInRelation) and **zod, drizzle and effect change their
output**. With the test dropped, the suite moves 8. So id order is observable here, unlike in union ties.
- But a random permutation is not what a shared graph gives. Under sharing, a type's id says when *some* worker
  first made it, and type arguments are always made before what holds them. Today's private ids already depend on
  history: the checker count, recycling and timing-dependent stealing all change it, and kdiff (K16 against K1: 2
  projects of 5242, both shared with tsgo), the recycle oracle (4) and every corpus stay byte-identical. Expect a shared
  graph's global order to behave like another history, not like a permutation. That is unproven.
- **The replacement, if global ids turn out to be observable: per-worker first-touch stamps.** Each worker keeps a
  dense `u32` array indexed by global id and stamps a type the first time it creates it or finds it in the intern
  table. Only the kinds with a recursion identity need stamps: references, anonymous instantiations, conditional,
  indexed and mapped types. is_deeply_nested_as() compares the stamps. At K1 the stamps equal today's ids exactly, so
  the rule is unchanged where we can test it, and under sharing each worker sees a history of its own. Cost: one load
  per intern hit of those kinds, measured at K1 before anything is shared. bun replaced the id test with a type's
  height and then push order, and lost some cases as false TS2322s on recursive generics (part C §4). Don't copy that.

**The architecture: a shared graph under a private overlay.**
- **Shared**: the interned types, signatures, mappers, Members, Props and type parameters; one global atom table
  (atoms are compared by pointer, so this is a prerequisite); global ids (an atomic counter handing out blocks of 1024
  ids, so condition 1 holds: a worker's creations stay in order) and a global id -> `Ty` directory (a chunked array
  with CAS-allocated chunks) so `LazyMemo`s keep 4-byte ids; the 85 SHARE kinds of `links`/`types` and the 7 SHARE
  `node_links` kinds; declaration-level LazyType memos (class fields and accessors, module-level bindings); and the lib
  singletons (intrinsics, `array_target`, `global_types`, `iteration`, `disposable_targets`, `unknown_signature`,
  `tuple_params`, `typeof_type`), all made before the workers start so their ids are fixed.
- **Per worker (the overlay)**: everything that is in flight (every LazyState IN_FLIGHT, the LazyMemo flags,
  KEY_SIMPLIFIED's placeholder, KEY_BASE_CONSTRAINT's sentinels, the variance claims, early and partial members); every
  *tainted* result (below); the 40 SCRATCH kinds; relation caches; expression memos; flow, inference and contextual
  state; the stacks; `errs`. A read goes to the shared value when it is done (one acquire load, the same fast path
  as today's `state == DONE` test), then to the overlay, then computes with an in-flight record in the overlay.
- **The publish rule** (both censuses reached it independently): a result is published only if its computation was
  *clean*. That means it popped no cycle, read no in-flight state, overlay value or TY_CIRCULAR, moved no limit
  counter (`instantiation_limit_hits`, `relation_complexity_hits`, `relation_depth_hits`, the constraint depth 50, the
  conditional epoch), ran with `flow_uncached == 0`, reported nothing at the current node, and ran in the typecheck
  phase. Mechanism: a per-worker taint counter that every tainting event bumps; a computation records it at start and
  compares at the end, so an inner tainted result taints everything around it for free. A tainted result goes to the
  overlay, which is exactly what that worker would have computed alone today. Publication is CAS, first one wins. A
  loser drops its own value and uses the winner's; its types stay in the graph unreferenced, as dead slots, like bun's.
- **Why the output stays byte-identical**: a worker sees either its own computation (overlay, as today) or a clean
  result that any worker would compute the same way, whoever computed it first. That is the same class of histories
  K, recycling and stealing already produce. What is new: (a) the history is now timing-dependent within one run, so
  any residual history dependence becomes run-to-run flakiness, not K-dependence; (b) mixed derivations: a shared
  declaration type derived from worker A's expression memos while B's walk derives the same body's expressions again
  in its scratch (census B §4). Both are measured, not argued: kdiff, checkerdiff and threadcheck repeated 20 times.
- **Diagnostics**: a clean computation that reports at a fixed span (TS7022, TS2502, TS2577, TS2456, TS2313, TS2705 /
  TS2468, TS7031, TS4109 with a span, report_iterator_methods) stores what it reported on the memo, the way
  `CallResolutionRare.errs` does today. The owning file's walk reports it, so who published can't matter. Anything
  that reports at the current node taints. Merging, sorting and `once_key` don't change.
- **Identity purity** (census A §4, B rows marked H): `from_type_node` (tsgo keeps whoever made an instance first, so
  first publisher wins is tsgo's own semantics), the conditional instance key that reads whether the root's branch is
  done, TYX_CONSTRAINED_TYPE_VARIABLE OR'd into an existing intersection, `empty_symbol` on a structurally interned
  object, `rest_binding` written after interning, `conditional.outer`/`root_aliased` written after interning,
  `ty_pattern_clone()`'s memcpy sharing one TyObjectState, `TyParamState.item`, KEY_INSTANTIATION_QUERY /
  UNDEFINED_PROPERTY / EXTENDED_CONTAINERS. Each moves into its constructor or key, or gets a written reason why it is
  history-like.
- **The table**: sharded open addressing (64 shards by hash, as bun has). Lookups take no lock: they read an atomically
  swapped table pointer, and entries are written key first, value last with release. An insert locks its shard. On a
  resize, readers may still be on the old array, so it stays allocated until the check ends: at most 2x the table
  footprint, which the memory budget has to price. Keys live in the arena and never move (`intern_key_list()`'s TyLists
  point into them). At K1, a lock that is never contended costs ~20 cycles per insert, and lookups don't change: measure
  it.
- **Memory**: the shared graph is never recycled. Each worker allocates from its own chunks of one shared arena, freed
  at the program's end. The shared graph is about K1's size (vscode K1 peaks at 1.4 GB). Overlays recycle when a worker's
  scratch exceeds its budget, which is cheap now that the expensive graph stays. Deferred-body materializations that a
  shared type points into are pinned until the end: plan 78's vscode win is at stake, so measure the peak. Expected at
  the defaults: about K1's peak plus K small overlays, against today's K full graphs.

**Re-estimate from the census: ~25 sessions** (the earlier 10-20 had no census): ~25 IN_FLIGHT sites and 16 HARD keys
to move into overlays, ~40 publication stores in ~22 functions, 6 mutations of published types to move into
constructors, ~10 identity fixes, two global prerequisites (atoms, ids), the diagnostics-as-data conversions, the
table, the arena and overlay recycling, then determinism hardening and perf. Every stage before 4e is a refactor at K1
that holds every oracle. So a no-go at G1 or G2 costs the sessions spent, and leaves code that is better structured,
not a half-built feature.

**Stages and gates** (each holds every oracle at K1, kdiff, checkerdiff and recycle; K1 instructions within 1% per
stage, 3% in total):
- **4-0, taint prototype (one session; done 2026-10-20, "Gate G0 result" below)**: add the taint counter and its events at K1, and mark every
  lazy store and every SHARE / HARD link write clean or tainted. Nothing is shared and the output doesn't change.
  Report, per kind, on drizzle, excalidraw, zod, effect and vscode: the clean share of entries and of compute time. For
  time, sample with `TTTT_FILE_TIMES`-style clocks around the outermost computations or count instantiations. Then
  **cleandiff**: fingerprint every clean result and compare two histories of the same program, K1 in program order
  against K1 with the share order reversed (or `--checker-memory 0`). The fingerprint is a structural hash with ids
  replaced by the referents' hashes, memoized per type; printing is too slow. Clean results whose key is shared must
  have equal fingerprints. **Gate G0**: go if ANONYMOUS_INSTANCE, CONDITIONAL_INSTANCE, members and props are mostly clean (>= 90%
  of their entries and time) and cleandiff finds no mismatch, or only mismatches with a named mechanism to fix. No-go
  otherwise, with the numbers: then the fallback (Order).
- **4a, in-flight state into overlays** (3-5 sessions): every IN_FLIGHT, LazyMemo flag and sentinel becomes a
  per-checker record; the shared-looking field is only ever written done. Still K1, still private checkers. Variance
  claims first (G0's condition 2), and G0's condition 3 (the 46 lenient-only drizzle prop types) before 4d.
- **4b, identity purity and diagnostics as data** (2-3 sessions). G0's M1 (the distributed twin) and M2 (the alias in
  the instance key) and its residue go here; cleandiff on the five corpora is the test.
- **4c, global atoms, global ids with the id directory, first-touch stamps** (2 sessions): stamps behind a flag, and at
  K1 the output is unchanged by construction.
- **Gate G1**: every oracle holds through 4a-4c at K1 and today's K, and K1 is within 3%.
- **4d, the shared graph behind `--shared-graph`** (3-4 sessions): the table, arena chunks, publish/CAS, overlays
  layered on it, workers on one graph.
- **Gate G2 (the old go/no-go, made measurable)**: drizzle, excalidraw and zod, per-file sums serialized at K12 within
  1.15x of K1 (today 1.40-1.68x); real K4 wall and CPU better than today's defaults on every corpus; kdiff, checkerdiff
  and threadcheck byte-identical in 20 repeated runs on the corpora and the source sweep's `--sample 4`; peak at the
  defaults no higher than K1's +25%. On the 12-core box (the human's command): per-file sums at K12 against the
  spinning-cores floor (1.62x K1 there), not against K1.
- **4e, build-out** (4-6 sessions): the rest of the census shared, overlay recycling, pinning, perf, the default on.
- **4f, determinism hardening at breadth** (3-5 sessions): the sweeps repeated, any run-to-run flake root-caused.

### Gate G0 result (2026-10-20): go, with three conditions
The prototype: `src/ty/taint.{h,c}` and `TAINT_*` hooks in 12 files, compiled only with `-DTTTT_TAINT`
(`scripts/taintstats.sh` builds it in /tmp/tttt-taint; the normal build is unchanged: suite, oracles and instructions
the same). One checker on one thread, no recycling, no deferred bodies. Intern-table results are found by their hooks
(the missed lookup starts a frame, the key's put waits for its value); members, prop types, signature returns,
constraints, declared types, function returns, base constraints, simplified, variances, free params and the
declaration LazyTypes are explicit. Expression LazyTypes are counted too, but they are per worker anyway. Cleandiff:
`--cleandiff` runs `--checker-memory 0` (a fresh checker, so a history of its own, for every file) and compares
structural fingerprints of every clean result whose key another checker computed first.

**The rule matters more than anything else.** *Strict* (taint everything that ends while `flow_uncached > 0`) leaves
only 39% (drizzle), 63% (excalidraw), 76% (zod), 73% (effect) and 43% (vscode) of the exclusive time clean; on
drizzle, conditional instances drop to 25%. That would be a no-go. The *G0 rule* (`--lenient`): `flow_uncached` only
taints expressions, because types read flow only through declarations, and declarations are flow-isolated while they
evaluate. Done-ness peeks (TAINT_PEEK) don't count. Variances read while they are being measured do taint:
`--variances-clean` gave drizzle 92 variance mismatches. Under the G0 rule (entries clean / time clean, %):
| corpus | ANONYMOUS_INSTANCE | CONDITIONAL_INSTANCE | members | prop types | all kinds |
|---|---|---|---|---|---|
| drizzle | 100.0 / 100.0 | 100.0 / 99.7 | 100.0 / 100.0 | 100.0 / 100.0 | 99.9 / 97.3 |
| excalidraw | 100.0 / 100.0 | 99.9 / 98.4 | 100.0 / 100.0 | 100.0 / 100.0 | 99.7 / 98.8 |
| zod | 100.0 / 100.0 | 98.9 / 90.2 | 99.9 / 99.6 | 99.7 / 95.8 | 99.3 / 84.4 |
| effect | 100.0 / 100.0 | 99.9 / 99.8 | 99.9 / 99.5 | 100.0 / 100.0 | 99.8 / 98.5 |
| vscode | 100.0 / 100.0 | 100.0 / 100.0 | 100.0 / 100.0 | 100.0 / 99.9 | 99.3 / 98.8 |
Zod's tainted time is almost all variance measurements in flight (relate.c's get_variances: a few expensive calls
reach a recursive measurement). The other first causes: in-flight intern slots, ty_function_return_in_flight(),
in-flight members (members.c). Cycles and limits are rare (effect: 21 cycles). Time is rdtsc around each computation
less its inner ones, under 3-7x instrumentation overhead, so the time shares are approximate; entry counts are exact.

**Cleandiff.** The first run found 5-1000 mismatches per kind. Most were fingerprint artifacts, now fixed in taint.c:
synthetic type parameters (no symbol: a mapped type's key stand-in) need their constraint; the KEY_REGULAR_UNION /
KEY_WIDENED_OBJECTS keys hold a type's address, which a fresh checker reuses; a synthetic prop's type is in the
fingerprint only if resolved, which is history (such members are now counted as unfingerprinted: 2.7% of effect's).
What is left, of 10^5-10^6 compared per kind (G0 rule):
- **M1, the distributed twin made on demand** (named, fix in 4b). instantiate_conditional() keeps an identity mapping
  in the restricted mapper only if the type parameter already has a distributed twin (`TyParamState.distributed`,
  made by ty_distributed_type_param() on first need). Whether it exists yet is history, so two histories build
  different mappers and instances for one key. `--twin` taints that test, which removes every CONDITIONAL_INSTANCE
  mismatch on drizzle (62) and zod (5), 58 of effect's 61, zod's 3 signature returns, and vscode's 142 TK_SIMPLIFIED.
  Fix: make the test independent of history (create the twin with the parameter when it is a distributive check
  type, or key the decision on something static). Then check the twin's tsgo semantics (plan 57) still hold.
- **M2, an instance's alias isn't in its key** (named, fix in 4b). `type SessionMeta = Record<string, unknown>` and
  `type MetaObject = Record<string, unknown>` share one KEY_ANONYMOUS_INSTANCE entry, named by whichever alias
  instantiated it first (vscode: 327 / 1.19M; excalidraw's Viewport). tsgo puts the alias in the instantiation key
  (getObjectTypeInstantiation: type list key + alias key). Do the same, or argue it can't print differently.
- **Residue, not named yet**: drizzle 12 anonymous instances (flags: TYF_COULD_CONTAIN_TYPE_VARIABLES set in one history
  only, free params unknown vs known), vscode 45 members, effect 3 conditional instances + 1 symbol declared type + 1
  declaration, excalidraw 3 prop types + 1 param constraint + 1 base constraint, vscode 2 union pairs + 2 keyof props.
  At most 0.03% of any kind.
- **Lenient-only residue (condition 3)**: 46 drizzle prop types (createIterator, values, mapToDriverValue) mismatch
  under the G0 rule but are tainted under strict, so they end inside a `flow_uncached` region. One history has
  variance-marker (`sub-T`/`super-T`) instantiations in their mappers. It may be another history mechanism that just
  happens to sit inside a loop, or a real counterexample to "types don't read flow". This decides whether the G0 rule is
  sound.

**Verdict: go.** The four hot kinds are 98.9-100% clean in entries and 90.2-100% in time on every corpus (zod's
conditional instances are the low point), and cleandiff's mismatches are two named mechanisms plus a residue of at
most 0.03% per kind. Conditions:
1. M1 and M2 get fixed in 4b, with the residue classified (`taintstats.sh --cleandiff --print-types --kind K` and the
   field traces in `--raw`), and cleandiff rerun on the five corpora after each 4a-4c stage: it must not grow.
2. The variance measurement's in-flight reads stay tainted. Recursive measurements are where zod's tainted time is, so
   4a moves variance claims into the overlay first.
3. The 46 lenient-only drizzle prop types get root-caused before 4d. If flow can reach a type, the G0 rule needs an
   exception for it (an end-condition event at that site), and the clean shares above get measured again.
Not measured: the share of these results another checker actually reuses (the warm measurement above says it's most of
the duplication, not which kinds). Cleandiff compares a fresh checker per file against the first one, not two
interleaved shared histories, so the mixed derivation of census B §4 is still untested.

### 4b's named mechanisms (session 3, 2026-10-21)
- **M1 fixed**: instantiate_conditional() keeps every identity-mapped type parameter in the restricted mapper (outside
  the identity instance), tsgo's newTypeMapper of all outer parameters, instead of only those whose distributed twin
  already existed. Types: drizzle equal, zod -1, effect +4; oracles held.
- **M2 was a fingerprint artifact**: ty_realias() already keys on (type, alias, alias arguments), like tsgo's
  getObjectTypeInstantiation; taint.c's fp_key() read its key as type id pairs and dropped the alias. Fixed there.
  Likewise vscode's 142 TK_SIMPLIFIED: the fingerprint ignored `writing`.
- **Cleandiff residue** (--lenient, after M1 and the two fingerprint fixes; before: drizzle CI 62 / prop 62 / AI 16,
  zod CI 5 / sigret 3, effect CI 61 / sigret 3 / members 2 / AI 2 / symbol 1 / lazy 1, excalidraw AI 5 / prop 3 /
  base 1 / param 1, vscode AI 327 / simplified 142 / members 45 / CI 5 / union pair 2 / keyof 2 / prop 1 / sigret 1):
  drizzle prop types 46 (G0's condition 3, the lenient-only ones); zod CI 3, signature returns 3; effect CI 3,
  signature returns 3, AI 2, symbol declared 1, lazy decl (class accessor) 1; excalidraw prop types 3, AI 1, base
  constraint 1, param constraint 1; vscode members 45, CI 5, union pairs 2, AI 2, keyof props 2, prop 1, signature
  return 1. Not classified yet; at most 0.03% of a kind.

### 4a's overlay: record shape and API (session 1, 2026-10-20)
Every 4a session follows this, so the sessions don't each invent a shape. The rule: **a field another worker could
read holds only finished values; anything about a computation in progress lives in a record owned by the computing
checker.** At K1 nothing is shared, so 4a is a pure refactor (byte-identical output, instructions flat).
- **The barrier stack** (plan 88 stage A, done): `Checker.barriers` (`Barrier { resolving_len, outer_start, kind }`,
  ty.h), pushed by resolve_call() (BARRIER_CALL for a first resolution, BARRIER_NESTED_CALL for one re-entered in
  flight, which doesn't move the barrier) and the outermost measure_variances() (BARRIER_VARIANCE, a restart included).
  `resolving_start` is the innermost call barrier's position, cached; `ty_barrier_outer_call_start()` is what a call's
  return type is resolved with. `ty_resolving_reenters(me, kind, key)` is the one place that decides "in flight only
  below the barrier, resolve again": each kind keeps its current rule there (aliases see the variance barrier, the
  rest the call barriers), so plan 88 stage B changes one function, not five sites.
- **Records live on the C stack of the computation that owns them**, linked into the checker: the `VarianceClaim`
  pattern (relate.c, done). `get_variances()` and `get_alias_variances()` look in the links (only finished
  `Variances`, no state field any more), then in `Checker.variance_claims` (every measurement in flight, the ones a
  restart set aside included), then claim and measure. The claim holds `circular` and `done` (the first measurement to
  finish, a restart's or a nested one's); `publish_variances()` writes the links once. The variance stack holds claim
  pointers. Lifetime is the C frame, so recycling and errors need no cleanup.
- **For kinds that push a `Resolving` entry** (declarations, return types, props, aliases, bindings, base constraints,
  bases, base constructors...): the entry *is* the record's anchor. Session 2 adds `void* record` to `Resolving` (the
  per-kind payload: `reevaluating`, `hits`, `circular`, a placeholder, a sentinel's meaning) and
  `ty_in_flight_record(me, kind, key)`, a scan of the whole resolving stack (not just above the barrier). Kinds with
  no push of their own (KEY_SIMPLIFIED, members, mapped `unexpanded`, deferred reference args, param defaults) get one,
  or a claim list like the variances' when the payload is bigger than a flag.
- **The shared field's state byte stays as a hint at K1**: IN_FLIGHT means "someone has it in flight, ask the
  overlay", never a value. Nothing else about the computation may be read from or written to the shared object
  (`Prop.circular`, `TyDeferredRef.circular`, `fallback_circular`, `FunctionReturn.reevaluating`,
  `LazyMemo.reevaluating`/`hits`, KEY_SIMPLIFIED's self placeholder, KEY_BASE_CONSTRAINT's sentinels,
  KEY_TYPE_VARIABLE_REDUCTION's `1`, early members). The fast path (`state == DONE`) is unchanged, and the scan only runs
  on an in-flight hit, which is rare (G0: in-flight reads are a few percent of tainted entries). In 4d the hint becomes
  owner-tagged (a worker id, or a per-worker bit) so a foreign IN_FLIGHT means "compute privately", not "cycle".
- **Publication** is one function per kind (`publish_variances()` is the model): it writes the value and DONE
  together, and is where 4d puts the CAS and the taint test. A computation that finishes after another one published
  (a nested re-entry, a restart) keeps the first value, as today.
- Session 1's cost: K1 instructions in `test/instrcount.txt`'s session-1 commit, against b7ad0eac's counts.
- **Session 2 (2026-10-21): done as below; what changed from the shape above.** `Resolving` got a `void* record` and
  `ty_in_flight_record()`, then lost them again: its only payloads were the `reevaluating` flags, and plan 88 stage B
  (done in the same session) removed those. What stays for the kinds that push an entry is `ty_in_flight_entry()` (whole
  stack, barriers ignored): props mark their own entry circular through it (Prop.circular is gone), base constraints
  ask it whether they're in flight (KEY_BASE_CONSTRAINT has no in-flight sentinel any more). Kinds with no entry use
  `Claim` headers on per-kind lists, `Checker.claims[ClaimKind]` (`ty_claim_push/pop/find()`): CLAIM_VARIANCES
  (session 1's VarianceClaim, rehomed), CLAIM_DEFERRED_ARGS (TyDeferredRef.circular gone), CLAIM_PARAM_DEFAULT
  (fallback_circular written only with the default), CLAIM_SIMPLIFIED (no self placeholder in the links),
  CLAIM_PRIMITIVE_CONSTRAINT (no `1` in KEY_TYPE_VARIABLE_REDUCTION), CLAIM_MEMBERS (early and partial members: every
  `state->members` write while IN_FLIGHT now goes into the MembersClaim), CLAIM_PREDICATE (predicate_done no longer set
  before computing, predicate_in_flight gone), CLAIM_MENTIONS (FunctionMentions.in_flight gone). Byte-identical output
  on the suite, the five corpora, vscode, kdiff-style checkerdiff/threadcheck, and taint cleandiff counts identical on
  drizzle and zod; K1 instructions nest -0.02%, drizzle +0.16%, zod +0.07% (callgrind, release build).
- **Publish functions** (where 4d's CAS and taint test go): publish_variances() (relate.c), publish_base_constraint()
  (relate.c), publish_reference_args() (lower.c), publish_param_default(), publish_members(),
  publish_signature_predicate() (members.c). The rest still publish inline: prop_type_slow() (`prop->type` +
  type_state), signature_return_type(), function_return_type() (KEY_FUNCTION_RETURN), ty_declared_type_of()
  (SymbolLinks), declared_param_constraint(), ty_mapped_template(), ty_cond_branch_type(), ty_non_augmenting_base(),
  ty_reduced_slow(), ty_simplified() and primitive_constraint_of() (an intern insert after the claim pops).
- **Left after session 2** (each a shared field written while the computation runs, or after it published):
  - Writes after publication: **done in session 3** (2026-10-21). Return types publish once
    (publish_function_return(), the nested type stays like tsc's; a later circular outer inference goes into the
    checker's scratch KEY_RETURN_LATE_CIRCULAR, read by ty_function_return_circular() for TS7023); an alias publishes
    declared_type and circular together, first to finish (tsc's typeParameters aren't unset by a later failure,
    whose TS2456 is reported where it happens); promise_missing is never cleared (reported on every non-speculative
    read, the merge drops duplicates); `unexpanded` is written only if unset. Instrumented, the late cases fire 13x
    (return types) and 1x (aliases) in the suite and 0-1x per corpus, never with a circular outer pop: nothing moves.
    ty_members_resolving()'s "outer overwrites nested" stays as it is, on purpose: the nested resolution's members only
    ever went into the MembersClaim (session 2), the shared state is written once, and tsc's setStructuredTypeMembers is
    last-writer too (resolveStructuredTypeMembers has no "if unset"), so the outer result is the faithful one.
  - The hint-only IN_FLIGHT states (no payload, a fixed answer on re-entry: they need 4d's owner tag, not a record):
    TyCondBranch.state (error type), TyObjectState.reduced_state (itself), base_state (null),
    SymbolLinks.base_variable_state (null), TyParamState.constraint_state (circular: none), MappedInfo.template_state
    (recomputed, not stored), ImpliedConstraint.check_state (skipped), Signature.state / ret_state, Prop.type_state,
    LazyMemo.evaluation_started, SymbolLinks.declared_state, TyDeferredRef.state, the IN_FLIGHT members_state
    (including ty_mapped_info_unexpanded()'s toggle back to NOT_STARTED).
  - LazyMemo.pattern_retried (a destructuring pattern found circular once is evaluated again later: history on the
    memo, a tainted result rather than in-flight state) and LazyMemo.hits / is_self_referential (only ever on
    expression memos, which stay per worker).
  - Intern entries inserted before their value (the instance kinds' null slots, TAINT_IN_FLIGHT_SLOT): the shared
    table's "key first, value last" publish covers them.
  - Per-expression scratch with in-flight markers, per worker by design: CallResolution.in_flight, KEY_FLOW_EXHAUSTIVE's
    2, KEY_BINDING_COMPUTED_KEY's `{}`.

### Track S: `--shared-graph` sequentially (2026-10-21, ahead of 4d)
`--shared-graph` (src/ty/graph.{h,c}, APPROACH.md): K checkers run one after another (`--threads 1`, no recycling)
on one graph: atoms, ids, the id table and the lib singletons handed to the current checker; `types`, `links`,
`intersection_props` and `node_links` with a shared table each, looked up after the private one on a miss
(`InternTable.shared`, `ty_link_get()`); declaration memos in a shared store by slot (`graph_memo_take()`); a share's
private entries published at its end (`graph_publish()`): identity kinds always, memo kinds when clean, scratch never
(`graph_kind_class()`). Ty-resident lazy fields (members, prop types, signatures...) are shared as they are. The cheap
taint (G0's lenient rule as a counter) builds only with `-DTTTT_GRAPH_TAINT`.

**Work** (callgrind instructions inside program_typecheck, as a multiple of the normal build's K1; base K4/K12 from
4eadfcc74, the rest at e3b90d42c + track S):
| corpus | base K4 | base K12 | shared K1 | shared K4 | shared K12 | shared K12, taint on |
|---|---|---|---|---|---|---|
| drizzle | 1.435 | 1.923 | 0.996 | 1.056 | 1.084 | 1.151 |
| zod | 1.192 | 1.510 | 0.996 | 1.059 | 1.081 | 1.246 |
| excalidraw | 1.180 | 1.377 | 0.989 | 1.057 | 0.9892 | 1.128 |
What K12 still does twice is relations (relation caches are per checker: check_related +0.7 G of zod's taint-on K12
excess) and, with taint on, variance measurements: tainted variances are withheld and every checker measures them again
(zod's get_variances 0.33 -> 0.89 G). Wall per-file sums (loaded machine, min of 2) agree: drizzle K12 1107 ms base,
769 shared, K1 841.

**Correctness**: without taint (or with every tainted kind published, `TTTT_GRAPH_PUBLISH_TAINTED=all`), byte-identical
to K1 at K4 and K12 on all 14 `-p` corpora (vscode included). With
taint, drizzle differs (K4: 298 lines against 308, K12: 288): two TS2416 elaborations go. Only KEY_VARIANCES does it
(`TTTT_GRAPH_PUBLISH_TAINTED=52` restores it). Mechanism: MySqlUpdateBase and MySqlUpdateWithout (and the Pg/Gel/
SingleStore twins) are a variance cycle; from a cold checker the restart-from-the-smallest-symbol rule measures
MySqlUpdateBase nested in MySqlUpdateWithout's measurement, [1 0 1 1 1], tainted (it read a claim in flight), and
every private checker at K12 gets the same. A later checker of the shared graph measures it again warm: its marker
instances are shared with their members resolved, so the measurement doesn't enter MySqlUpdateWithout, finds itself
circular directly and gets [17 16 17 17 16] (VARIANCE_UNRELIABLE everywhere). So a tainted result is not "what a
private checker would compute": recomputed in a warm graph it can be what *no* cold history computes. For 4d: a
variance cycle's results must be published as a group with their cold values (first finisher wins, which is what
publishing them gives), not withheld; the publish rule needs a "cycle-internal in-flight read" exception for
variances, the case G0's `--variances-clean` probed. The other tainted kinds (conditional instances, simplified,
base constraints, declaration memos) changed nothing withheld.

**The share** (`TTTT_GRAPH_STATS=1`, K12): shared hits 1.18 M drizzle, 1.02 M zod, 1.85 M excalidraw lookups
(literals, SymbolLinks, references, anonymous and conditional instances, intersection props, union pairs); taint marks
only ~0.03-0.1% of entries (292 / 658 / 412 withheld).

**Memory** (peak RSS, `--threads 1`): K12 shared 524 / 461 / 411 MB against K1 473 / 429 / 373 and base K12 877 /
661 / 580 (drizzle / zod / excalidraw); vscode K12 4.35 GB against K1 4.11, base K12 4.83.

**Cost at K1**: flag on, -0.4% (the last checker doesn't publish; publication is ~2.5% of a checker's work: reading
every private block's kind). Flag off: flat (the taint is out of the default build).

**What isn't shared, and what 4b/4d must do with it**: relation caches (the biggest remaining duplication;
KEY_GENERIC_RELATION numbers relations per checker), every scratch kind, the hand-overs (atoms, ids, id table,
singletons: sequential only, 4c's global atoms and ids), Ty-resident results (shared as computed, tainted or not:
`graph: tainted members ... (on an earlier checker's type: N)` counts the ones another checker then reads: 2-6),
identity records with tainted contents (SymbolLinks, FunctionReturn: withholding them would split a symbol's declared
type), and peeks at memo done-ness (`lazy_type_state_peek()`) that don't see a shared memo. Locks/CAS would go in
`graph_publish()`'s per-entry insert (to become per computation) and `intern_shared_find()`/`graph_link_get()`'s probes.

### Stage 4c: global atoms, global ids, first-touch stamps (2026-10-21, track S's worktree)
- **Atoms** (src/ty/atom_table.{h,c}): one table per program that every checker shares. It has 64 shards by the hash's top
  bits. Lookups take no lock: each shard's slot array is swapped whole when it grows (the old one lives until the table
  goes), and a slot is written atom first and hash last (release/acquire). A miss locks its shard. Atoms are allocated
  in 64 KB blocks per shard. Atoms are only made by checkers (the binder and parser don't intern), so loading is
  untouched. Atom addresses only order lookup tables (props_by_name, index_atoms_by_name): no output depends on them.
- **Ids** (lazy_type.h's TypeDirectory): one atomic counter per program hands out blocks of one id chunk (65536, not
  1024: a chunk then belongs to one checker and is freed when it is dropped, so recycling's memory is unchanged). A
  checker's ids stay in its creation order (gate condition 1). The program's directory holds every chunk.
  LazySlots.types stays the read path as a copy of the directory's pointers (own chunks, plus the others' via
  lazy_slots_sync() under --shared-graph), so memo reads cost what they did. 4d's concurrent readers need either the
  directory on the read path (one more load) or a fill when a chunk is null. The lib singletons are still made on
  first use and handed over under --shared-graph: making iteration/disposable/global types before the workers start
  can report TS2318s a lazy run doesn't, so 4d needs them published once (CAS, first wins) rather than made eagerly.
- **First-touch stamps** (`--first-touch-stamps`, TTTT_FIRST_TOUCH_STAMPS=1; Checker.stamps, ty_stamp(),
  is_deeply_nested_stamped()): each checker has a dense stamp per type, taken when it makes the type, when it first
  finds a reference/anonymous instance/conditional/indexed type in the shared tables, or else at the first comparison.
- **Gates**: byte-identical to K1 at K1, K4 and K16 (16 threads, recycling at the defaults) and with --shared-graph at
  K4/K12, on all 14 `-p` corpora but vscode (run on drizzle, zod and effect with stamps on and off). The suite is
  unchanged, stamps on or off. K1 instructions (callgrind, whole process) against ai-main be0af5b37:
  atoms + ids nest +0.1%, drizzle +0.2%, zod +0.3%; + the stamps' test in ty_alloc() (off) +0.09 / +0.27 / +0.35%;
  stamps on on top of that drizzle +0.69%, zod +0.84% (a chunked store per type made, a load per comparison).
- **Id order observability** (the design's "another history" hope): at one checker the stamps are the id order (0
  decisions differ, K1 and K16). Under --shared-graph, where ids say when *some* checker made a type, ids and stamps
  disagree on 0.06-0.6% of deep-nesting decisions (drizzle 461 / 289k at K12, zod 187 / 372k, effect 2878 / 486k), and
  the output is byte-identical with either. So on these corpora a shared monotone counter behaves like another history:
  the decisions it changes aren't observable. Stamps are the safe choice for 4d at their cost; ids alone held the gate
  here, so 4d can start without them and keep `--first-touch-stamps` as the oracle for a divergence.

## Validation of the whole plan
The 12-core table in measurements.md re-run by the human after each stage (`scripts/bench.sh --runs 3 --bun PATH
--tscrs PATH`, the CPU column included), per-file sums at K1 / K1-with-spinning-cores / K12 on vscode and drizzle
after stages 2-4, every oracle through `scripts/oracles.py`, and vision.md's "Perf and memory" bullet rewritten with
the geometric means. The plan is done when vscode at the defaults is under ~1.2 s on that box with the output
unchanged, or when a stage's gate says the rest can't pay and this file says why.
