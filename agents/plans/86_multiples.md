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

**Stage 3: the link step, a design (2026-10-21, worker "frontend"; not implemented)**. vscode at T12 (`TTTT_RESOLVE_TIMES=1`
prints link's steps now): link ~60 ms on a quiet run = module refs ~15 (`binder_module_of_ref()` per ref, 124k refs),
type/resolved exports ~5, **aliases ~40-70** (`binder_link()`'s loop over `Binder.symbols`: `symbol_resolve_alias()`
on each of ~200k aliases, then the alias checks: TS2440/TS2484/TS2865 conflicts, `export_value`, ambient const enum,
isolatedModules). Callgrind: binder_link 160 M instructions of which resolve_alias 117 M; ~1 instruction per ns, so it
is cache misses (every alias, its module's export table entry, the target), not compute.
- *Order-dependent*: (1) TS2303: `symbol_resolve_alias()` marks every alias on `alias_stack` from the re-entered one to
  the innermost CIRCULAR, and each reports when it finishes, so which aliases of a cycle report depends on where the walk
  entered it (tsc's findResolutionCycleStartIndex: the same rule, entered in tsc's order); (2) placeholders
  (`binder_new_placeholder()`) are appended to `Binder.symbols` in resolution order, which later loops (`binder_resolve_
  remaining_aliases()`) and anything iterating symbols see; (3) reports are appended in resolution order (TS2305/TS2614
  /TS2306/TS1192... in `resolve_alias_target()`, the checks in the loop), and same-position ties keep that order;
  (4) `module_export_equals()` and the namespace-import interop build symbols lazily (`export_equals_clone`, gather
  targets), first resolver wins. Targets themselves are order-independent (a pure function of the tables, which are
  complete after module_resolved_exports()) except through (1).
- *Parallel resolution* (per-alias locks or CAS on `alias->state`, threads resolving disjoint aliases and recursing into
  shared ones) would need a deterministic cycle rule independent of entry (not tsc's: TS2303 positions would change)
  and deferred, re-ordered reports and placeholders. High risk, every alias-related baseline at stake.
- *Two-phase* (the cleaner one): phase A on threads, read-only, computes each alias's first step (the module, the
  export table entry or `export=` it names, or "needs the general path"); phase B, serial in `Binder.symbols` order,
  runs today's recursion but takes phase A's step instead of looking it up, so order, cycles, placeholders and reports
  are exactly today's. The risk is replicating `resolve_alias_target()`'s first step (250 lines: default/namespace
  interop, `export =`, entity names, CommonJS, shorthand ambient modules, type-only, wrong-context items...) without
  writes; the fallback "general path" keeps any doubtful case serial. Gain: phase B still touches each alias and its
  target (the misses it can't avoid); measured bound: the hash lookups and table walks are ~40% of resolve_alias's
  instructions, so perhaps 40-70 -> 25-40 ms. **Not worth it now**: < 1.5% of vscode's 2.5 s wall at 12 cores, for the
  riskiest code in the binder. Revisit if link becomes the front end's floor after the loader.
- Module refs (~15 ms) are per-ref lookups that only write the ref (`program_link_module_refs()` runs them on threads
  unless a pattern has augmentations, whose lookup sets a parent table).

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
**Variances are never withheld now (2026-10-21, get_variances())**: tsc measures a type's variances once per program,
whoever asks first; the cheap taint no longer marks them. Tried first and rejected: a group rule where only the
cycle's own claim reads are exempt. The outermost measurements' groups were still tainted, by reads of tainted
results (signature returns, declaration memos: 88 and 394 reads on drizzle K4) whose taint came from a handful of
in-flight declarations and return types outside the measurement (lazy_type.c's IN_FLIGHT, function_return_type()'s),
so every one of drizzle's MySql/Pg/SQLite/Gel/SingleStore groups stayed withheld and the TS2416s still went. With
variances published: drizzle K4 and K12 identical to K1, and zod, effect, excalidraw, nest, vscode at K12 too (a
-DTTTT_GRAPH_TAINT build, --threads 1). For 4d: what a variance cycle measures is published by the checker that
measured it, as one group when the outermost measurement (claim_and_measure()) ends, first group to finish wins: a
group must not be mixed from two workers' measurements (different entries into one cycle can measure its members
differently). Not built yet: the prototype publishes per checker, which is per group already.

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

### Stage 4d: the shared graph under real concurrency (design, 2026-10-21)
Track S showed the ceiling (K12 1.08-1.10x K1, byte-identical) with checkers run one after another and publication
batched at each share's end. Under real threads that batching is wrong, not just slow: every checker starts at once
and shares nothing until the first share ends, and a key worker B made privately can meet the same key A published
later through A's types (two `string[]`s in one union). So 4d publishes **identities at creation and computed values
at the computation's end**, and makes every field another worker can read safe to read while it is written.
- **The tables** (types, links, intersection_props, node_links): sharded open addressing (64 shards by hash), reads
  lock-free (the shard's slot array swapped whole when it grows, entries written block/value first and hash/key last
  with release, read with acquire), inserts locked per shard, replaced arrays kept until the program ends. The
  atom table (4c) is the model. Blocks live in the inserting worker's arena, which is never recycled with the flag.
- **Identity kinds** (graph_kind_class() GRAPH_IDENTITY: constructors, per-symbol/per-node roots like SymbolLinks)
  have no private table with the flag: lookup shared; on a miss the worker builds the value and `intern_publish()`
  inserts it under the shard lock, or returns the entry another worker inserted meanwhile (the loser's value is
  dropped before it escapes: its Ty stays in the arena and the id directory as a dead slot). This needs the
  constructors' `*slot = ret` (132 intern_slot sites, ~30 ty_link_set sites) turned into `ret = intern_set(me, table,
  slot, ret)`, a plain store without the flag. Interning is hash-consing, so two workers computing the same memo
  through published identities get the same pointers.
- **Memo kinds** keep a private table (tainted or scratch results stay there) and publish at the computation's end
  with first-wins CAS (the publish_X() functions: publish_variances, publish_base_constraint, publish_reference_args,
  publish_param_default, publish_members, publish_signature_predicate, publish_function_return, and the inline
  stores listed under 4a). A loser keeps reading the winner's value from then on (lookup order shared, then private:
  the reverse of track S, which only worked because nothing was published during a share). Scratch kinds never leave
  the worker.
- **Ty-resident lazy states** (members_state, Prop.type_state, Signature.state/ret_state, constraint/fallback state,
  TyCondBranch.state, reduced_state, base_state, template_state, declared_state, deferred args...): the IN_FLIGHT hint
  becomes owner-tagged: `IN_FLIGHT | worker << 2` set by CAS from NOT_STARTED, DONE stored with release after the
  value. A foreign IN_FLIGHT is never a cycle: the reader waits for DONE (or NOT_STARTED: an owner that abandons)
  unless waiting would close a wait cycle (A waits on B's field while B waits on A's: a global wait-for graph under one
  mutex, consulted only on a foreign in-flight hit, which G0 says is rare), and then computes privately into its
  overlay (a per-worker map by field address, read on the slow path only). Without the flag the worker tag is 0 and
  the states are today's values, so the fast path (`state == DONE`, a plain load on x86) and the K1 behaviour stay.
  Every `== LAZY_STATE_IN_FLIGHT` test becomes `lazy_in_flight()` (40 sites).
- **Other writes to published objects** (census A §4): `order_key` (racy-benign word), Members.rare attached late
  (CAS), TYX_CONSTRAINED_TYPE_VARIABLE (atomic OR, or into the constructor), empty_symbol / rest_binding /
  conditional.outer (into the constructors), tuple_params (published by pointer swap, grown under a lock).
- **Steps 5 and 6 done ahead (2026-10-21, worker "inflight")**. Writes to published objects: an object rest's
  rest_binding and import()'s empty_symbol (OBJECT_EMPTY_SYMBOL, in the key) and a conditional root's `outer`
  (ty_conditional() takes a TyRootSpec) are set by their constructors; order_key relaxed atomics; Members.rare attached
  by CAS (members_rare_published()), by_name_order first-wins, contains_error an atomic set-only flag; the late
  TYX_CONSTRAINED_TYPE_VARIABLE an atomic OR (history-like: which path made the intersection first, K1's order; its
  word's other readers race only formally, a byte of its own would settle it); root_aliased and TyParamState.item
  first-wins CAS (item: never late on the suite or corpora, the constructor sets it); pattern clones keep sharing
  their state on purpose (the protocol is per state) with their own order_key. from_type_node needs nothing: it is
  set at construction and first-wins by interning, tsgo's own semantics (whoever instantiates first). Singletons:
  TySingletons (ty.h), each checker's field a cache, ty_singleton_publish() / ty_singletons_sync(), tuple_params by
  pointer swap. **Hook for the graph worker**: TyGraph holds a TySingletons (ty_singletons_new()); graph_new() sets
  `first->singletons` and calls ty_singletons_sync(first), graph_adopt() the same for each checker; held_take() /
  held_give() drop those fields. Tested with that patch in a scratch build: K4/K12 sequential byte-identical to K1
  on drizzle, zod, effect, excalidraw, nest, vscode.
- **Ids and singletons**: ids from 4c's blocks; the read path adds a fill-on-null of LazySlots.types from the program
  directory (a worker meets another's id only through shared types: the null check sits in the slow path of
  lazy_slots_type(), which needs one). The lib singletons (array targets, global_types, iteration, disposable targets,
  unknown_signature) are published once: each worker's field is a cache of a graph slot set by CAS, first wins, so
  lazy TS2318 reports stay where they are today.
- **Declaration memos**: the shared store by slot, published with CAS when the evaluation ends; a worker evaluating a
  declaration another worker has in flight evaluates it privately (memos are values of identities, so they converge).
- **Variances**: published as a group at the outermost measurement's end (the cycle's cold values, track S's finding),
  never withheld.
- **Taint**: off. Track S's output was byte-identical without it and changed with it; the compiled-in
  -DTTTT_GRAPH_TAINT build stays the instrument. If a run-to-run difference shows up, taint is the first suspect to
  re-enable for that kind only.
- **Per worker, unshared**: relation caches (the biggest remaining duplication: zod's +0.69 G of +0.91 G at K12, 4e's
  first item: definite results keyed by global ids could be shared), KEY_GENERIC_RELATION numbering, every scratch kind,
  expression memos, flow, inference and contextual state, the resolving/barrier/claim stacks, `errs`.
- **Gates**: K1 with the flag byte-identical and within 1%; K4/K12 on real threads byte-identical to K1 in 20 repeated
  runs on drizzle, zod, effect, excalidraw (and the rest of the corpora once), per-file sums and callgrind at K12 vs K1,
  wall/CPU at K4 vs the defaults, peak RSS at the defaults. Without the flag: byte-identical and instruction-flat.
- **Order of work** (one commit each): the concurrent shared tables; `intern_set()` at every identity site and
  immediate publication of identities (still at one thread: output must not move); the owner-tagged states and the
  wait protocol (`lazy_claim()` / `lazy_publish()` helpers per state); memo publication at computation end; the late
  writes; singletons; then threads on, and the measurements.
- **Status** (update at each step). Coordinator's adjustment: a foreign in-flight state means *compute privately into
  the overlay* (no waiting); count how often it happens (TTTT_GRAPH_STATS) and add the wait protocol later only if
  that says it matters. Step 1 done: src/ty/shared_table.{h,c} (SharedTable for word keys, SharedLinks for node links,
  first insert wins) replaces the graph's InternTables and LinkTable; the declaration memo store makes its pages by CAS
  and writes a memo flags-last under a mutex. Still --threads 1, publication at share end: K4/K12 byte-identical on
  drizzle, zod, effect, excalidraw.
  Step 2 done: identities publish at creation. `intern_set(me, table, slot, value)` (ty.h; `*slot = value` without the
  flag) and `ty_link_publish()` (node links) at every identity site (39 intern sites, the SymbolLinks / function type /
  FunctionReturn / fresh literal / decl group / class member symbols links); with the flag, `intern_publish()` inserts
  the new block into the shared table (its hash kept in the word before the block) and, if another checker's is there,
  the private entry takes the winner's value, which the constructor returns. Constructors set the value only once it
  is final: intern_deferred()'s callers fill the fresh type first, instantiate_anonymous() decides
  literal_instance_is_itself() first, signature instantiations resolve first. Lookup stays private first: a private
  identity entry holds the winner, a memo entry its own value. At one thread nothing is ever lost and the output
  doesn't move.
  Step 3 (threads on, waiting): the flag no longer forces one thread. Every Ty-resident lazy state (members, the raw
  members, mapped `unexpanded`, prop types, signature resolution and return, constraints, defaults, mapped templates,
  conditional branches, deferred args, reduced intersections, equivalent bases, class base variables, declared types,
  inferred return types, implied constraints: 18 claim sites) is claimed by CAS with the worker's number in the bits
  above the state (LAZY_CLAIM()), published with a release store after its value (LAZY_DONE()), and read through a
  snapshot (LAZY_SNAP()): another worker's in-flight state is waited for (lazy_wait_slow(): 64 pauses, then sleeps
  doubling from 2 us to 1 ms, a global wait-for graph under one mutex; a worker whose wait would close a cycle and that
  has the highest number in it treats the field as its own cycle, counted). Copies of objects with such states
  (`*copy = *original` of signatures, props, mapped infos) drop another worker's in-flight marks (LAZY_COPIED()).
  Without the flag the worker number is 0 and the values are LazyState's. Also: the lib singletons are the inflight
  worker's TySingletons (graph_adopt()), every checker's id table is told of each new chunk (lazy_slots_share()), the
  first checker's pre-graph entries are published when the graph is made (the intrinsics' literals and typeof's union:
  without that every worker made its own `"string"` and comparisons said "no overlap"), identities found by intern_slot()
  are shared but memo kinds' slots stay private (callers write them), and a dozen lazily written fields became
  first-wins CAS or atomics (class static types, distributed twins, predicates, evolving arrays' final arrays, implied
  extends, a type parameter's circularity, `unexpanded`, ty_erased_signature()'s read of the mapper before the state).
  The lib warm-up argument for waiting (all K workers start at once on Array, String, Promise...) holds: waits are
  ~100-1200 per run, fallbacks 0 on drizzle, zod, effect, excalidraw.
  **Not deterministic yet**: K12 on 12 threads gives K1's output in 24 of 32 runs (drizzle 7/8, zod 6/8 and one
  segfault, effect 6/8, excalidraw 5/8); the differing runs add or drop a few diagnostics (members of a type missing
  properties, a constraint not satisfied, a TS2589). Mechanism (main suspect, not yet proven): results computed while
  their computation read something in flight (early members, a re-entered conditional branch, a variance claim: G0's
  tainted results, ~0.1% of entries) are published into the shared Ty-resident field like clean ones. At K1 and in
  track S every checker computed each such result in its own history; on threads, whether the tainted value (one
  checker's in-flight view) or a clean one is what everyone reads depends on timing. Also known: the conditional
  instance key reads whether the root's branch is lowered yet (instantiate_branch(): two instances of one type when
  two workers race; keying by the node always cost drizzle +10% at K1). Next: the cheap taint on in shared mode and
  tainted Ty-resident results kept in a per-worker overlay (slow path only), then rerun the 20-run gate.
  Default path (callgrind, flag off) against the step 2 tree: nest +0.0%, drizzle +0.37%, zod +0.35%.
  **The segfault and most of the non-determinism were one bug** (2026-10-21, after step 3): shared_table_find() and
  shared_links_get() probed, and when the probe stopped at an empty slot they looked at that slot *again*: another
  worker's insert landing there in between made the lookup return that other key's entry (a KEY_STRUCTURAL block for a
  KEY_TYPE_PARAM lookup, a Ty where a conditional instance's entry array belongs). Now the probe's own view decides.
  Effect at K12 on 12 threads crashed 7/30 before, 0/40 after (K32 on 32 threads 5/20 -> 0/30); the 8-run table went
  from zod 6/8, drizzle 7/8, effect 7/8 + a crash, excalidraw 3/8 to zod 8/8, drizzle 6/8, effect 8/8, excalidraw 8/8.
  Also fixed on the way (each a real race, none the segfault): side table entries describing a property
  (KEY_PROP_WRITE_TYPE behind Prop.write_type_known, KEY_SYNTHETIC_PROP_PARTS, KEY_PROP_MAPPED_TYPE) are identities
  now, published before the property is reachable (a memo kind, only the checker that made the property found them
  until its share ended); instantiate_branch() copies a branch field by field (the struct copy read the type another
  worker was writing); clone_type_params() stores the mapper atomically (ty_erased_signature() reads it); the branch
  and signature state peeks in cond_branch_may_be_never()/_check() and resolve_call_inner() are LAZY_LOAD();
  instantiate_anonymous() adopts the instance a nested instantiation published instead of overwriting the published
  block with null; an alias in flight on another worker that this one doesn't wait for (a broken wait cycle) is a
  cycle, not a re-entry (ty_declared_type_of() looped forever: the one hang seen, 1 in ~60 runs). Tried and reverted:
  KEY_ALIAS_MARKER / KEY_CHECK_MARKER_REF as identities (whether an alias instance is a marker is history: excalidraw
  differed in every K12 run). Wait-cycle fallbacks do happen at K32 (0 to 1000 a run): each treats another worker's
  field as its own cycle, which is K1's semantics only approximately; not yet a measured source of differences.
  Remaining: drizzle's 2/8 (two TS2589 in mysql-core/db.ts, or two TS2345 `never` params in tidb-serverless/session.ts:
  instantiation-depth and contextual results that depend on what a worker computed first), the taint experiment's
  subject. TSan on effect at K12 and K32 (btsan) reports only formal races now: ty_param_constraint() reading
  `circular` plainly next to its CAS, SymbolLinks.base_cycle's flag RMW (two workers' bits can be lost: a real one,
  benign so far), Prop's write_type_known bitfield next to its neighbours, MappedInfo's struct copy.
  **Resume notes of 4d (historical; the current ones are at the top of Stage 4e).** The scripts are now in scripts/sharedgraph/ (README.md; outputs in $SG_OUT, default /tmp/tttt-sharedgraph): `paths.sh` (corpus -> tsconfig), `ref.X`
  (K1 output: `release/tttt --threads 1 --checkers 1 --checker-memory off --noEmit --pretty false -p P`), `rep.sh BIN N
  corpus...` (N runs at --shared-graph K12 T12 against ref.X: same/diff/crash, differing outputs kept as diffout.X.I),
  `crashrate.sh N VAR=VAL...` (effect crash count, env passed through), `hunt.sh BIN CORPUS N` (gdb runs of a -g build, each
  crash's stacks kept as crash.X.I), `hang.sh BIN CORPUS N SECS` (a run past SECS interrupted under gdb, all threads'
  stacks), `tsan.py ERR` (TSan reports grouped by both sites). Builds (scratch, deleted; rebuild with meson setup as named): `bdbg` (debugoptimized
  -DTTTT_DEBUG_IDS: id lookups that miss abort, intern tables used off their checker abort, type-valued kinds hold
  types, identities never publish null), `btsan` (-Db_sanitize=thread), `basan` (address), `bpoison`
  (-DTTTT_TY_POISON). Tools in the tree: TTTT_GRAPH_NOSHARE=k,k / =all-but:k,k (kinds kept private: sharing nothing
  never crashed in 80 runs, which is how the bisection started), TTTT_NO_SHARE_END_PUBLISH=1 (no memo publication),
  TTTT_GRAPH_LOUD_FALLBACKS=1 (each broken wait cycle on stderr), TTTT_DEBUG_INTERN_SET=1, graph_fallback_hook() (a
  breakpoint: scratch's `fb.gdb` prints each broken cycle's stack).
  **The taint experiment** (coordinator's step 2, after the auto array fix, which removed the `push(x)` against never
  family): `bgtaint` (release -DTTTT_GRAPH_TAINT, scratch) at K12 T12, 20 runs per corpus, rep3.sh with stats:
  without withholding 70/80 runs K1's output (excalidraw 17, drizzle 17, effect 19, zod 17), with
  TTTT_GRAPH_WITHHOLD=1 76/80 (18, 20, 19, 19). **Every differing run, either way, broke wait cycles**
  (TTTT_GRAPH_LOUD_FALLBACKS: 1 to 63 fallbacks); runs without a fallback were all K1's output, and with withholding
  most runs with fallbacks were too (drizzle 12 of 12). Per kind, the tainted counts of K1-identical runs are steady
  (zod: members 117, prop types 539, signature returns 1393, function returns ~1387, conditional instances ~279;
  effect: members 122, base constraints 90...): the results every history taints, K1's too. Differing runs add a burst
  in some kind where the cycle broke (drizzle prop types 326/339 against ~10; zod union nodes 396 or declared types 63;
  excalidraw everything). So the differences aren't the steady tainted results but the cycles workers enter from the
  other side than K1: A computing X waits for B's Y while B computing Y waits for A's X, and the breaker (the higher
  number) takes X as its own cycle, which K1 only does if it entered Y first. Broken cycles seen (fb.gdb):
  ty_reduced_slow() of two intersections (never_reduced_prop() -> prop type -> an indexed access -> the other's
  reduction), ty_members_resolving() under property access. Withholding the Ty-resident results tainted by the break
  hides most of it; what's left is the breaker's own file, checked in its broken-cycle history (its diagnostics), and
  results of kinds not withheld. Overlay work scoped by this: only results tainted *by a fallback* need keeping from
  the others (the steady ones are K1's), and the breaker's own diagnostics need its file rechecked or its cycle
  entered K1's way. Cheaper directions to weigh first: fewer cycles to break (ty_reduced()'s in-flight guard is
  tttt's, tsc's getReducedType recomputes on re-entry; the lib warm-up cycles), or a breaker that waits for the owner
  to finish its whole top-level computation by yielding its own claims (abort and redo, not partial).
  **The coordinator's levers** (release, K12 T12, 20 runs a corpus, rep3.sh + summ.sh; fallbacks = broken wait cycles
  summed over the 20 runs): before them 74/80 K1's output, 1088 fallbacks. Lever 1, ty_graph_warm_up() (the first
  checker resolves the global types and the members and property types of ~30 hot lib interfaces before the workers
  start; TTTT_GRAPH_WARM_UP=0 skips it): 73/80, 405 fallbacks. Lever 2, the yielder is the worker whose file is latest
  in program order (Checker.file_order, ties by number; TTTT_GRAPH_YIELD=worker for the old rule): 76/80 (excalidraw
  17, drizzle 19, effect 20, zod 20), 183 fallbacks (zod 0). The differing runs all had fallbacks (excalidraw's 3:
  contextual parameters lost, 1 to 164 added lines; drizzle's 1: a TS2589); 15 runs with fallbacks were K1's output.
  Lever 3 doesn't apply: tsgo's getReducedType sets ObjectFlagsIsNeverIntersectionComputed before computing, so a
  re-entered intersection is itself there too, the same guard as tttt's reduced_state (its union case recomputes, and
  so does tttt's KEY_REDUCED_UNION path); the cycle is real in tsgo's order as well, only private to one checker.
  **Lever 4, the redo** (coordinator's design, the abort variant): a worker that would break a wait cycle gives its
  file up instead. check_file_or_give_up() (program.c) arms a jmp_buf (Checker.redo_jmp) around each file;
  lazy_wait_slow() longjmps there when this worker is the yielder. The file's diagnostics so far are dropped, the checker
  is retired (graph_retire(): kept until the program ends, the graph holds what it made; Program.retired) and a fresh
  checker (a new worker number, graph_adopt() under a lock now, lazy_slots_share() registration sequentially consistent
  with chunk broadcasts) checks the file again; the files before it in the segment get their after-check pass from the
  old one first. The retired worker's in-flight fields are nobody's: whoever meets one (lazy_wait_slow()) resets it to
  not started, so the other side claims and finishes them in K1's shape. At most MAX_FILE_REDOS (3) redos a file, then
  the old fallback; none past 60 workers; TTTT_GRAPH_REDO=0 turns it off; TTTT_GRAPH_STATS counts the redos. Nothing
  computed after the break is published, since nothing is computed after it. Not handled: a break while the thread
  materializes deferred bodies (none seen; the AST thread state would be left set), malloc'd temporaries of the
  abandoned stack leak. **20 runs a corpus: 79/80** (excalidraw 20, drizzle 19, effect 20, zod 20), 29 redos in all,
  every run that broke a cycle K1's output; the one differing drizzle run had no broken cycle (a TS2589 in
  mysql-core/query-builders/query-builder.ts: instantiation depth against what is cached already, another mechanism).
  vscode: 2 runs, both K1's output, no redos, 2.3-2.6 s wall at K12 against K1's 13.5 s, 4.6 GB peak (K1 4.1 GB).
  **Step 4 done (2026-10-21, worker "shared2"): memo kinds published when their value is there**, not at the share's
  end (which on threads is the program's end: before it, K12 shared almost no memo and drizzle did 2.6x K1's work).
  intern_put() logs a memo kind's new private entry (Checker.graph_memo_log, with instantiation_limit_hits then);
  graph_memo_flush() inserts the logged entries that have their value into the shared table (first wins, a loser's
  entry takes the winner's value), every 64 puts (the limit doubles while unfilled entries pile up), at each file's
  end and at the share's; the share-end graph_publish() is gone with it (60 ms a checker on drizzle). Not published:
  an entry made before an instantiation limit ran out (TS2589: cut short in that worker's history). intern_slot() of a
  memo kind fills a new private entry from the shared table (callers find it done). Node link memos (ty_link_set())
  and declaration memos (graph_memo_done()) go at once. All of it only with a second checker (Checker.graph_memo_on).
  TTTT_GRAPH_MEMO_EAGER=0 (share-end publication), =nodecl (declaration memos at the end), TTTT_GRAPH_MEMO_LATE=k,k
  (kinds held back: the bisection tool), TTTT_DEBUG_MEMO_PUBLISH=1 (published values written again later: 2-23 a run,
  KEY_REVERSE_MAPPED and intersection props whose nested request published first; those two stores are release now).
  **Variances needed their own protocol**: published entry by entry, two workers measured one type at once and the
  first read spread (drizzle's View [0 0 0] next to K1's [1 1 1], a TS2344 in 2-4 runs of 20); kept private, every
  worker measured them again in the now warm graph (excalidraw TS2345 in 20 of 20, track S's warm-measurement effect).
  So on threads a type's or alias's variances have a shared slot (graph_variance_slot(): LAZY_CLAIM()ed by the
  measuring worker, LAZY_SNAP()-waited for by the others, done as one group when the claimer's outermost measurement
  ends, graph_variances_publish()), tsc's one measurement per program; variances_known() counts another worker's
  measurement in flight (marker instances are compared structurally then, as K1 would). This makes wait cycles, so
  redos are common now (drizzle 1-5 a run). Waiting also changed: after the 64 pauses, 50 sched_yield()s before the
  sleeps (TTTT_GRAPH_WAIT_YIELDS=N): with every memo shared the workers ran in a convoy, a waiter that slept fell in
  behind the owner at its next field (drizzle K12: 89k waits, 1.59 s mean wall; yielding, 660 and 0.85 s).
  Work (callgrind Ir inside program_typecheck and the worker threads, --checker-memory off), base = 36c99661f:
  | corpus | flag K1 base | flag K1 now | K12 base | K12 now | K12 now / K1 |
  |---|---|---|---|---|---|
  | zod | 4.384 G | 4.392 | 5.527 (1.26x) | 4.805 | 1.096 |
  | drizzle | 5.283 | 5.294 | 13.869 (2.63x) | 5.853 | 1.108 |
  | excalidraw | 6.544 | 6.548 | 7.653 (1.17x) | 7.372 | 1.126 |
  Shared hits (TTTT_GRAPH_STATS) drizzle 1.5 M -> 1.2-8 M (with the waiting), zod 1.0 -> 0.9-1.3 M, excalidraw 1.0 ->
  1.9 M, effect 1.6 -> 1.8-2.2 M. Wall at K12 T12 (load 4, mean of 5): drizzle 0.25 s against 0.76 (min 0.30), effect
  0.27 / 0.29, zod 0.16 / 0.18, excalidraw 0.196 / 0.200. Default path (flag off, whole-process callgrind): nest +0.05%,
  drizzle +0.24%, zod +0.12% before moving the log hook into intern_put()'s --shared-graph branch, typecheck-only
  drizzle +0.007% after. 20-run table at K12 T12: drizzle 19 (one run: TS2769/TS2578 missing errors, redos in it),
  zod 20, effect 20, excalidraw 20; drizzle over 70 runs in all 66-67, the differences TS2589s, TS2578 (`@ts-expect-error`
  lines left unused: a type too permissive) and once TS2322/TS2430/TS2416 in singlestore (variance-shaped). Task 3's
  subject (what instantiation limits and the redo leave history-dependent) is the next suspect for all of them.
  **Task 3 (the drizzle residue) found: concurrent variance measurements, not the limits or the redo.** Repro loop:
  drizzle K12 T12 next to an effect K12 loop for load, 100 runs (~4 min, scratch `t3loop.sh`: verdict, redos, limit
  hits, redone files, differing codes; TTTT_GRAPH_STATS now prints per-worker instantiation limit hits and each redone
  file). K1 has no instantiation limit hit at all, and no differing run had one (the TS2589 of before step 4 is gone).
  Differing runs ~2% (6 of ~400), redos in every one but at the same rate as in identical runs (most runs redo 1-6
  files). Tried and not it: memo entries made during a measurement kept private, every lazy state done during the
  abandoned measurement reset at the redo, the marker types per worker (TTTT_GRAPH_NOSHARE=53,55,56,57), no redo
  (fallback: 7%). What fixes it: one worker measuring at a time (graph_variance_lock(), owner-tagged so the wait-for
  graph sees it, taken by the outermost measurement): 0 differing in 300 loaded runs, and the redos mostly go (0-4 a
  run). Cost: drizzle's K12 wall 0.27 -> 0.44 s (load 10; zod, effect, excalidraw within noise): measure_variances is a
  third of drizzle's K1 typecheck inclusive (1.88 G of 5.28 G), now on one worker at a time. Not understood: which
  shared result one measurement takes from another running at once (both claim different subjects; a cycle between
  them is a wait cycle, so they're unrelated measurements reading each other's lazy results). Narrowing the lock
  (per strongly connected group of subjects, or only while another measurement is in flight on a type this one
  reaches) is 4e work; TTTT_GRAPH_VARIANCE_LOCK=0 is the old behaviour. tsgo's dependence on checker count (task 3's
  other question) was not measured: its checkers share nothing, so its variance is per checker.
  **Task 2 done: a conditional instance is keyed by its root and mapper** (ty_conditional(): key[2]'s root id, then
  the mapper's pairs) instead of by its branches, whose form depends on whether the root's branch was lowered yet
  (instantiate_branch()); roots keep the branch key. The instances' branches are made as before, so nothing lowers
  again. K1 default path: drizzle -0.12%, zod -0.04%, nest -0.01% instructions (shorter keys), one type fewer on
  drizzle (171 989), suite and corpora unchanged; K12 T12 20 runs each on drizzle, zod, effect, excalidraw, nest: K1's.

  **Gate G2 (task 4, b97b6f4ac; the table is measurements.md "Plan 86 gate G2")**: work holds (callgrind K12 / K1
  with the flag: zod 1.097, drizzle 1.099, excalidraw 1.134, target 1.15) and so does determinism (every `-p` corpus
  at K4 and K12 K1's output; 20 / 20 on the five, drizzle 300 / 300 under load, vscode 5 / 5, TypeScript6 5 / 5). CPU at
  K12 falls on 10 of 14 corpora (drizzle -41%, zod -27%, excalidraw -23%), wall is equal or better on 11 of 14 but
  worse on drizzle (+54%: the variance lock), type-fest (+50%) and effect (+17%). Memory fails: nothing is recycled,
  vscode peaks at 4.4-4.6 GB against 1.4 at the defaults. Per-file wall sums at 12 threads are 2-5x K1's (12 threads
  on this box run each file ~2x slower per instruction, and drizzle's are mostly waiting), so they don't measure work
  here. Next: 4e below.

### Stage 4e: making `--shared-graph` the default (plan, 2026-10-21; ~9-13 sessions)
**Superseded by "Stage 4f: by construction" below (2026-10-22): its design replaces the waiting, the redo and the owner-tagged states; the build-out steps S0, S1, P1-P3 and D are there.** **Resume from here (2026-10-21, worker "shared2").** Built: 4d complete (shared tables, identities at creation, memo
kinds published when their value is there, owner-tagged lazy states with waiting, wait-cycle redo, variance slots,
conditional instances keyed by root and mapper, KEY_NODE published at creation). 4e: item 2 (shared relation cache),
item 3 (the graph-wide recycle) and item 4 (the redo's gaps) done. Open: item 1, the global variance lock (one worker
measures at a time; without it ~2-3% of loaded drizzle K12 runs differ, mechanism unknown, findings below; costs
drizzle +54% K12 wall) and item 5, default on, which waits for item 1 and the full oracle set with the flag (run once
at b3a67c3e5: identical but for the kdiff and dtsdiff items since fixed or explained). (Plain-path dtsdiff on vscode, KEY_UNDEFINED_PROPERTY's
first-need declaration reset by recycling: fixed 2026-10-21, the choice is program-wide and history-independent,
members.c's undefined_choice(); KEY_UNDEFINED_PROPERTY is now only each checker's name -> program entry link.) Also
open: the effect TS2769 seen once in 43 recycle storms. Knobs: TTTT_SHARED_GRAPH=1 (the flag for
scripts), TTTT_GRAPH_BUDGET_MB (graph-wide recycle budget, default 800; `--checker-memory 0` every file, `off` never),
TTTT_GRAPH_VARIANCE_LOCK=0 (lock off), TTTT_GRAPH_SHARE_RELATIONS=0, TTTT_GRAPH_MEMO_EAGER=0|nodecl,
TTTT_GRAPH_MEMO_LATE=k,k, TTTT_GRAPH_WAIT_YIELDS=N, TTTT_GRAPH_REDO=0, TTTT_GRAPH_STATS=1 (shares, waits, redos and
redone files, limit hits, relation pairs and shared hits, graph-wide recycles), TTTT_DEBUG_UNSHARED=1 (identities left
private at a share's end), TTTT_DEBUG_MEMO_PUBLISH=1, TTTT_GRAPH_LOUD_FALLBACKS=1. The stress loop: drizzle at K12 T12
beside an effect K12 loop for load, 100-150 runs (~4-6 min), each run's verdict against K1's output, redos, redone
files and limit hits (`scripts/sharedgraph/t3loop.sh BIN N ENV...` against `ref.X`, the K1 outputs `scripts/sharedgraph/refs.sh`
writes; scripts/sharedgraph/README.md lists the rest); `rep3.sh BIN N "VAR=VAL" corpus...`
for 20-run tables; `cgtc.sh BIN corpus K` for callgrind work in the typecheck and worker threads.

In order; each item its own commit, gated like 4d (K1 byte-identical and flat with the flag, K4/K12 K1's output on
every `-p` corpus, 20-run tables on the four, no default-path cost until the flag flips).
1. **Narrow the variance lock (1-2 sessions).** The lock serializes all measurements (drizzle +54% wall at K12). First
   find what one measurement takes from another in flight: two workers' TTTT_DEBUG_VARIANCE logs (worker-tagged, as in
   task 3's scratch patch) in differing lock-off runs, and TTTT_DEBUG_RELATE_NESTED around the measurement whose
   result differs; the candidates are Ty-resident results finished under another worker's measurement (members and
   prop types of instances whose relation used an Unknown variance) and variances_known()'s foreign-in-flight answer.
   Then either fix that read (keep such results private to the measuring worker: a measurement-scoped overlay) or lock
   per connected group (a worker that would wait on another's slot joins its lock instead of claiming its own).
   Gate: drizzle 300 loaded runs K1's output with drizzle's K12 wall back at or below the defaults'.
   **First session's findings (2026-10-21, no fix yet).** Lock off, drizzle K12 next to an effect loop: ~2-3% of runs
   differ (13 of ~700), always *missing* errors (unused `@ts-expect-error` in type-tests, a TS2416/TS2322 group in
   singlestore): some type became too permissive. Ruled out, each over 100-150 loaded runs: KEY_NODE duplicates (fixed
   anyway, 536614091), variances_known() answering "known" for another worker's measurement in flight (only done
   ones: still 2/150), memo entries made while measuring kept private, per-worker marker types, resetting what the
   abandoned measurement finished at a redo. The published variances themselves vary run to run with the lock on too
   (the VARIANCE_UNRELIABLE bit, 1 against 9, on ~40 drizzle types: whether a measurement went through something
   unreliable depends on what the worker's relation cache already held) and the output doesn't move, so the final
   variances aren't the channel. Overlapping measurement pairs (start/end times per worker) show no pair specific to
   the differing runs. Remaining suspects: a relation-dependent result computed inside one worker's measurement with
   its own subject in flight (an Unknown variance read as related: inferred return types, declaration memos,
   overload results of class members reached through marker instances) that another worker's measurement or check
   races to compute first; under K1 the order is fixed. Tools for the next try: a worker-tagged TTTT_DEBUG_VARIANCE,
   a per-slot dump at publication (both scratch patches, easy to redo), and the 100-run loaded loop (`t3loop.sh`).
   Also ruled out (coordinator's hypothesis, one round of 150 loaded runs): memos built on a relation decided by
   assumption. A per-worker counter bumped at every maybe-stack / deeply-nested / depth-limit Maybe and every Unknown
   from unmeasured variances; intern memo entries and declaration memos whose computation saw it move kept private
   (110k of 450k intern memo entries and 45 of 4900 declaration memos withheld on drizzle): still 2 of 150 differ.
   Not testable that way: Ty-resident results (members, prop and signature return types), which have no private home.
   **Third session (worker "varlock", 2026-10-21): the residue was two mechanisms, neither a variance-sharing leak.**
   (a) *The redo dropped other files' errors* (most of it: every TS2578 case). check_file_or_give_up() popped the
   checker's error vector back to its length before the file, but errors are kept in span order, so the tail it
   popped held the errors of files the same checker had checked earlier whose paths sort after the given-up file
   (geldb/insert.ts given up: mysql/insert.ts, sqlite/with.ts lost theirs), while the given-up check's own errors
   stayed. Fixed (4bdbb37de): a file that may be given up reports into an ErrorCtx of its own, merged when checked.
   How it was found: rechecking every file in the finished graph with a fresh checker (and with the original one)
   always gave K1's errors, and the relations both checks asked agreed: the loss was in the bookkeeping, and every
   affected file had been checked by the worker that later redid a type-tests file (a redo never seen in identical
   runs). The lock "fixed" it only by making late redos rare. Likely also the coordinator's effect TS2769 under
   TTTT_GRAPH_BUDGET_MB=1 with the lock on (every recycle replays warm-up, more redos).
   (b) *Deep-nesting decisions by type id* (the singlestore/mysql TS2416/TS2322/TS2430 group, ~0.5% of lock-off runs
   after (a)): one worker's measurements all came out bivariant + unreliable ([19 19 ...] in tsgo's numbering:
   SingleStoreSession, SingleStoreSelectBase, ..., even Awaited) from some point on. Its relation cache held
   `super-T -> sub-T = true`: inside SingleStoreSelectWithout's measurement (Omit<SingleStoreSelectKind<...>>) the
   pair was reached in a frame both sides of which were deeply nested (expanding == 3, Maybe), the outermost pair
   concluded Maybe and the maybe stack's keys went into the cache as succeeded (tsc's rule), poisoning every later
   measurement of that worker. is_deeply_nested_as() counts occurrences with *increasing type ids*; under the shared
   graph ids come from per-worker blocks, so the order is the schedule's. With TTTT_FIRST_TOUCH_STAMPS=1 (4c's
   per-checker stamps) and the lock off: 500 / 500 loaded drizzle K12 runs K1's output (without stamps 2 of ~400).
   So stage 4c's "ids are another history, unobservable" is false under concurrent measurement: stamps (or a
   deterministic id order) are needed, at their K1 cost (+0.7-0.8% instructions measured in 4c), or the lock.
   Lock off after (a): 150 + 300 + 400 + 500 runs, the only differences (b)'s. Not done: drizzle/effect wall with lock
   off + stamps against the defaults, zod/effect/excalidraw/nest repeat tables with that combination.
   **Done (2026-10-22, wt-design4f): the lock deleted, stamps on whenever the graph is shared**
   (program_set_shared_graph() sets first_touch_stamps; graph_variance_lock() and its field gone). Gates (numbers in
   measurements.md "Plan 86 4e item 1"): 20-run tables drizzle/zod/excalidraw/nest 20/20, effect 19/20; 300 loaded
   drizzle runs K1's; vscode 5/5; every corpus K1 = K4 = K4 T4 = K12 T12 with the flag; work K12/K1 drizzle 1.117,
   zod 1.097; drizzle K12 wall 0.25 s against the defaults' 0.29 (the lock's 0.42). What remains: effect's ~1.5% of
   loaded runs adding a TS7006 (fiber.ts or Schema/TestUtils.ts: a contextual parameter type lost), present with the
   lock too and with base + stamps, no redo or broken cycle in the differing runs; effect's wall +10% over the
   defaults; redos are back to 0-8 a drizzle run. Stage 4f's design is what should make the residue moot.
   Tooling left in the tree: TTTT_DEBUG_VARIANCE is worker-tagged and buffered per thread (vlog(), printed when
   typechecking ends: stderr writes per line hid the races), with VARWAIT / VARSLOT lines for each foreign variance slot
   met; TTTT_DEBUG_DISAGREE=1 prints, at the end, memo entries (conditional instances, intersection properties) a
   checker computed differently from the value another published first (normal runs: none that print differently).
   The recheck-in-the-finished-graph and per-file trace comparison (TTTT_SHADOW) were scratch patches, not kept: the
   recipe is to check every file again with one fresh checker after the workers join and diff its errors per file
   against the first check's (by span.file over the share's whole ErrorCtx, not the tail).
   **Stage 4f gates (worker "tooling", 2026-10-21; scripts/sharedgraph/README.md has the usage).** Regression gates
   that find violations of the publish rule, not the correctness argument. (1) `tsan.sh`: TSan on every `-p` corpus
   at T4/T12 under the flag, one line per run, exit 1 on any report. First run (de94e1179): every output K1's, no run
   clean, 57 groups (inventory with stacks and guesses: `$SG_OUT/tsan-groups-de94e1179.txt`, default
   /tmp/tttt-sharedgraph, not committed). Categories: A, one lazily written field accessed both atomically and plainly
   (TyParamState.circular in ty_param_constraint(), the biggest at 20 of 28 runs; TyParamState.item, signature
   predicates, reference args, SymbolLinks.base_cycle); B, bitfield read-modify-writes on published objects
   (Prop.write_type_known against every read of its neighbours; LazyMemo.done of declaration memos, which
   graph_memo_take() reads without the lock, fences only); C, fresh objects reached by another worker before their
   initializing stores are visible to it (ty_alloc()'s fields, TyObjectState, fresh-literal twins, order keys, the type
   directory's chunk slots: a publication path without a release/acquire pair); D, structs copied while written
   (ty_mapped_info_instantiate()'s `*mapped = *source` against template_ and the unexpanded members being built); E,
   data mutated in place after publication (ty_function_type_mentions() into members and signatures,
   ty_function_type() still initializing a type another worker resolves the members of); F, four trivial ones, fixed
   (1519287bb). (2) The recheck, `TTTT_GRAPH_RECHECK=1|private` (program.c graph_recheck(), rep3.sh counts it): every
   file checked again by one fresh checker after the join, in the finished graph or privately, per-file errors
   compared, the first differing file printed with both lists. (3) `fingerprint.sh` (a `-DTTTT_GRAPH_FINGERPRINT`
   build, src/ty/graph_debug.c): a private K1 check in the same process records every result's structural fingerprint
   by key (in-process because fingerprints hash AST and symbol addresses), then each value the K12 workers compute and
   publish is compared. 20 runs each (de94e1179): differences in every run of zod (9), excalidraw (35-56), drizzle
   (7-656), effect (9-1062), nest 0-2; 113 distinct, all with K1's output but one effect run. The repeatable ones: zod
   `$replace<Meta, S>`'s base constraint published as its check type's constraint; methods' property types published
   as an anonymous instance of the function type where K1 keeps the type (KEY_FUNCTION_TYPE_MENTIONS /
   literal_instance_is_itself(), TSan's category E); drizzle's array builder variances; effect inferred returns with
   `any` (aggregation.test.ts's generator: the TS2769/TS2375 miss). (4) `seeds.sh` (`TTTT_GRAPH_SEED=S`,
   src/ty/graph_sched.c): one token, handed on in a seeded order at claims, identity publications, memo flushes, file
   starts and wait rounds, so a schedule is its seed and replays byte for byte (checked: drizzle 397, 320, effect 473).
   About K1 wall per run (zod 2.3 s, drizzle 4 s). 500 seeds each: drizzle 451 / 500 K1's output (all 49 the
   singlestore/mysql TS2416/TS2322/TS2430 group, (b) above), with `TTTT_FIRST_TOUCH_STAMPS=1` 500 / 500; effect 488 /
   500 (fiber.ts:293 TS7006, a contextual parameter lost, and aggregation.test.ts TS2375), with stamps 494 / 500, all
   fiber.ts TS7006 (replay: `TTTT_FIRST_TOUCH_STAMPS=1 TTTT_GRAPH_SEED=18`; the recheck in the graph doesn't show it:
   the worker's history). The schedules the seeds explore are far from what the free-running threads produce (the
   free runs never showed fiber.ts): run both. Combined (FP_MODE=1 with _ONLY/_STACKS/_TRACE on a failing seed,
   README "fingerprint + seed"): effect seed 18 names the declaration memo of the parameter `id` at
   src/internal/fiber.ts:293 published as `any` (K1: FiberId) through the same stack K1 computed it by, so the
   contextual type was missing on the worker at that moment (the symptom key; its cause is upstream); drizzle seed 379
   without stamps names View's variances [11 11 11] against K1's [1 1 1], mechanism (b). The oracle now covers every
   published lazy kind (README "Fingerprint coverage"), clean results only, and converted sites call SW_FP() beside
   sw_finish(). With stamps on (the graph's default since 3f4c7550c): 300 seeds each on zod, excalidraw and nest all K1's
   output.
2. **Shared relation cache (2 sessions)**, as designed below. Gate as there; excalidraw's K12 pairs within 5% of K1.
   **Done (2026-10-21).** recursive_related(): after the private cache and the maybe stack, a pair the private cache
   doesn't have is looked up in the graph's table for its relation kind (graph_relation_get(), SharedLinks keyed like
   the private cache); SUCCEEDED returns true, FAILED false unless reporting (then computed again, as a private
   failure is). Published (graph_relation_put(), first wins): the maybe stack's keys when the outermost pair concludes
   and failures, only without CACHE_UNRELIABLE / UNMEASURABLE / INDEX_INFERRED, never a generic relation key (bit 61),
   never from inside a variance measurement; overflowed top-level pairs stay private. TTTT_GRAPH_SHARE_RELATIONS=0
   turns it off; TTTT_GRAPH_STATS prints pairs computed (Checker.relation_pairs) and shared puts/hits. Pairs computed
   at K12 / K1: excalidraw 169k / 168k (287k before), zod 237k / 234k (269k), drizzle 259k / 247k (278k), effect 266k /
   263k (289k). Callgrind K12: excalidraw 7.40 -> 7.21 G (1.10x K1), zod and drizzle within noise. Wall at K12 (load 4.5,
   mean of 5) unchanged within noise. 20/20 K12 runs on drizzle, zod, effect, excalidraw, nest; sequential K4/K12 K1's.
   Default path +0.05-0.1% (the pair counter and the flag tests).
3. **Memory: per-worker private state recycled, the shared graph kept (3-4 sessions).** With the flag nothing is
   freed: a worker's arena holds both what it published and its private state (relation caches, scratch kinds,
   expression memos, flow and inference results, private memo entries). Split each checker's arena into a published
   part (what intern_set()/graph_memo_flush()/LAZY_DONE publish: blocks other workers can point to) and a private
   part freed at the same budget points as today's recycling (the private tables and caches reset, the published
   ones kept); a recycled worker keeps its worker number and the graph. Needs: allocation sites told which arena
   (identities and Ty-resident results to the published one, scratch to the private one; the census in
   86_stage4_census.md lists the kinds), and no private pointer from a published block (TTTT_DEBUG_IDS-style check).
   Target: vscode K12 peak within ~1.3x of today's defaults (1.4 GB), drizzle/effect within 1.2x.
   **Measured first (2026-10-21): the private part can't reach the target.** vscode at K12 with the flag peaks at
   4.56 GB: ~0.89 GB before the typecheck (AST, binder), then ~1.1 GB of materialized deferred bodies (`--no-defer-bodies`
   parses them all up front: resolve ends at 2.00 GB instead of 0.89; with the flag nothing is recycled, so every
   materialized body stays, `defer: dropped 0`), the checkers' arenas 1.42 GB (one K1 history's worth of types:
   K1 without recycling has 1.40 GB), and ~1.1 GB of tables, lazy pages and the rest (checker_memory() sums to 1.67 GB
   with the arenas). The defaults peak at 1.28-1.49 GB because a recycle drops all of it: types, bodies and tables. With
   a shared graph the types are published (every worker may point at them) and published types point into
   materialized bodies (function expressions', object literals', local declarations' nodes: KEY_NODE identities, decl
   pointers), so neither can go while the graph lives. Recycling only the private part (relation caches, scratch kinds,
   expression memos, private tables) frees at most ~1 GB: ~3.5 GB, 2.5x the target. **What the target needs is a
   graph-wide recycle**: when the graph's memory passes the budget, every worker stops at its next file boundary (nothing
   in flight there), the last to arrive drops every checker, the graph and its tables (deferred bodies are released
   with the checkers' holds), a new graph and checkers are made, and the workers go on, warming the lib up again
   (ty_graph_warm_up()). Today's per-checker recycle semantics for the whole graph at once; the barrier costs up to the
   slowest worker's current file per recycle. Estimate 2-3 sessions (the barrier with stealing and finished shares,
   graph teardown/rebuild, the id directory's chunks, retired checkers dropped with it, the recycle oracle).
   **Built (2026-10-21): the graph-wide recycle** (program.c graph_epoch_due() / graph_epoch_arrive() /
   graph_epoch_leave() / graph_epoch_rebuild()). After each file a worker publishes its checker_memory(); when the sum
   passes the budget, every worker runs its segment's after-check pass and parks at its next file boundary; the last
   to arrive (or the last worker leaving its share, so finished shares never hold the barrier) drops every checker
   (deferred bodies go with their holds), the redo's retired ones and the graph, resets the type directory's readers,
   makes a fresh graph and checkers (errs, recycle counts and peaks carried over), runs ty_graph_warm_up() and wakes
   the others, who make their new checker current. Budget 800 MB (GRAPH_BUDGET_MB; TTTT_GRAPH_BUDGET_MB overrides,
   `--checker-memory 0` recycles after every file, `off` never): vscode recycles twice, none of the other 13 `-p`
   corpora, dokploy or monkeytype do (300 MB would recycle type-fest, zod, drizzle, effect once). vscode K12 T12 5 runs
   each, interleaved: with the recycle 2.73-2.78 GB peak, without 4.56-4.58 GB, wall the same within noise (load 5:
   2.34-2.40 s against 2.27-2.29; load 17: 4.10-4.16 against 3.98-4.20), all 10 K1's output; the defaults without the
   flag 1.55 GB. Budgets on vscode: 1000 MB 1 recycle 3.14 GB +2% wall, 600 3 recycles 2.36 GB +4%, 400 5 recycles
   1.92 GB +6.5%. Recycle oracle (`TTTT_SHARED_GRAPH=1 scripts/kdiff.py -k 1 -m 0`: a graph recycled after every file
   against one never recycled, checkers one after another): 4 / 3814 differ, inbox-zero/apps/web, rxjs/packages/rxjs,
   sequelize and sequelize/packages/core, the plain recycle oracle's four. Storms on threads (K12 T12, budget 1 MB:
   13-72 recycles a run): drizzle, zod, excalidraw, nest K1's output; effect 1 of 43 runs lost a TS2769 (stream.ts),
   at 20 MB 0 of 10 on each of the four: the shared graph's residual concurrency, met far more often from a cold graph.
   **Where the flag already wins on memory, the thrashers**: K12 T12 (load 3.5) dokploy/apps/dokploy 0.77 s, 4.2 s CPU,
   1.22 GB against the defaults' 3.40 s, 34.5 s, 2.98 GB (K1: 3.77 s, 0.81 GB); monkeytype/frontend 0.30 s, 2.1 s, 564 MB
   against 0.89 s, 8.9 s, 1056 MB (K1 0.79 s, 395 MB); both 5 / 5 K1's output. vscode 2.12 s / 21.5 / 4.56 GB against
   2.20 / 22.5 / 1.49; drizzle 0.41 / 1.54 / 629 MB against 0.28 / 2.65 / 622; effect 0.28 / 2.62 / 840 against 0.24 /
   2.39 / 561.
4. **The redo's gaps (1 session).** A break while the thread materializes deferred bodies (none seen) leaves the AST
   thread state set; malloc'd temporaries of the abandoned stack leak (bounded by redos: drizzle 0-4 a run); a retired
   checker's memory stays to the end (fold into item 3: a retired worker's private part freed at once).
5. **Default on (2-3 sessions).** Flip the flag where it wins: threads > 1 and a program big enough (the per-checker
   source bytes rule, or the sh12 / def12 table above). Before flipping: the whole oracle set (sweep, source sweep,
   kdiff -k 1 vs 16, expecttypes, flagflip, clidiff, dtsdiff, scenarios -b) with the flag, since only the suite and the
   corpora ran with it so far; checker recycling's oracle (kdiff -m 0) replaced by item 3's; type-fest and effect,
   where the flag costs CPU today, looked at (likely relation duplication, item 2).
6. **Open questions**: whether tsgo's own output depends on checker count where tttt's shared graph now does what K1
   does (task 3's other half, not measured); whether TTTT_GRAPH_WAIT_YIELDS' 50 suits a quiet many-core box (tuned on
   a loaded 16-vCPU one).

### 4e's first item: a shared relation cache (measured and designed 2026-10-21, not built)
**Measurement** (`agents/plans/86_relation_share_probe.patch`, a scratch patch to relate.c's recursive_related():
`TTTT_RS=1` keeps a program-wide map of every clean definitive cache put, keyed by relation, intersection-state bits
and the global type-id pair, and counts, at `--shared-graph --checkers 12 --threads 1`, the pairs a checker computes
that an earlier checker already concluded, with everything computed beneath them; default build, so variances are
always published). Relation pairs computed (recursive_related() past its cache and maybe-stack checks, a
machine-independent proxy for relation work; the box was at load 20+, cycle counts were noise):
| corpus | K1 pairs | K12 pairs | K12 pairs under an already-concluded pair (heads + beneath) | share of K12 | values disagreeing |
|---|---|---|---|---|---|
| excalidraw | 168 386 | 284 148 | 47 336 + 67 760 = 115 096 | 40% | 0 of 114 414 duplicate puts |
| zod | 267 712 | 276 635 | 10 904 + 19 371 = 30 275 | 11% | 0 of 29 742 |
| drizzle | 283 800 | 278 724 | 14 485 + 12 695 = 27 180 | 10% | 0 of 26 872 |
| effect | 275 527 | 286 778 | 11 534 + 9 182 = 20 716 | 7% | 0 of 20 252 |
| nest | 7 107 | 8 002 | 481 + 419 = 900 | 11% | 0 of 899 |
On excalidraw the recomputed-but-concluded work is the whole K12 excess (115k of 116k extra pairs); elsewhere K12
does about K1's relation work already (zod +3%, drizzle -2%: the shared graph changes histories) and a shared cache
would take 7-11% off. Track S's "check_related +0.69 G of zod's +0.91 G" was taint-on, where withheld variances were
measured again by every checker; with variances always published that duplication is gone. Two workers that conclude
the same pair never disagreed (0 in ~186k duplicate definitive puts).
**tsc's semantics** (relater.go): one relation map per relation kind for the whole program, entries Succeeded /
Failed plus ReportsUnreliable / ReportsUnmeasurable and Overflow. A failure goes "straight into the global cache (when
something is false under assumptions it will also be false without assumptions)"; successes under Maybe assumptions
are recorded only when the outermost pair concludes (resetMaybeStack), never a Ternary.Unknown. A cached failure is
computed again when errors are reported (reportErrors, unless it was an overflow), so the cache never decides an
elaboration. Cached flags are re-reported into the variance measurement in progress (reliabilityFlags). So a tsgo
result is reused by every later comparison whatever its context: sharing definitive results across workers is tsc's
own single-checker semantics, provided only definitive, context-free entries are shared.
**Design**:
- Per relation kind, a shared table keyed like the private one (the global type-id pair; the intersection-state bits
  62-63), values SUCCEEDED or FAILED only. Same concurrent structure as 4d's intern tables (sharded open addressing,
  lock-free reads, insert-if-absent; the value byte written before the key with release). First wins; a loser's value
  is equal (measured).
- Published at the private puts in recursive_related(): FAILED at a pair's failure (and fails_common_properties()),
  SUCCEEDED for each maybe-stack key when the outermost pair concludes. Not published: anything with
  CACHE_UNRELIABLE / CACHE_UNMEASURABLE (they must reach the measuring worker's variance reports as flags; a flagged
  entry stays private, so a shared hit never hides an unreliability), CACHE_OVERFLOW (depends on the top-level
  relation's budget), CACHE_INDEX_INFERRED (depends on the IntersectionState twin), and generic relation keys (bit 61:
  KEY_GENERIC_RELATION numbers them per checker; 1-2% of pairs. Sharing them needs the numbering table shared, keyed by
  the key words: a later step if worth it). Results computed while a variance measurement is in flight are already
  marked through the flags.
- Lookup: private cache first (its in-progress and flagged entries are this worker's), then the maybe stack as now,
  then the shared table: a shared SUCCEEDED returns true; a shared FAILED returns false unless `report`, which
  computes again privately exactly as a private CACHE_FAILED does today (elaboration and errors stay per worker, never
  read from or written to the shared table). The maybe stack, relation budgets, depth and deeply-nested stacks, and
  `lines` stay per worker.
- Gates: K1 with the flag byte-identical and flat; K4/K12 byte-identical to K1 on the corpora; the probe's
  disagreement counter stays 0; excalidraw's K12 relation pairs within ~5% of K1's.
- Cost at K1: one more probe per private miss that reaches computation (~280k per corpus): a few ms.

### Stage 4f: by construction (design, 2026-10-22)
The ruling (plan 89 section 3): the shared graph's determinism must follow from its design, and testing is only a
regression gate. This section is the design the build-out implements. It replaces 4d's claimed-and-waited lazy states.
**Item 1 of 4e is done first** (66a06b563: the variance lock is deleted, stamps are on under the flag; numbers in
measurements.md "Plan 86 4e item 1"). **The residue it leaves**, for the fingerprint oracle to target: about 1.5% of
loaded effect K12 runs add a `TS7006 Parameter 'x' implicitly has an 'any' type` (`src/internal/fiber.ts(293,21)`
`interruptAsFork: (id) =>`, or `test/Schema/TestUtils.ts(45,20)`). The same rate shows with the lock and without the
stamps, and the differing runs have no wait, redo or broken cycle. A contextual parameter type is lost, so a
contextual signature (a signature return or a members result read while another worker had it in flight) came out
different. A second residue for the build-out to verify (worker "filler", 2026-10-21): one vscode K12 `--shared-graph` run of
10 with `TTTT_DEBUG_UNDEFINED_PROPS=all` (program.c's recheck_stale_undefined_props(), private checkers after the
workers join) added TS6133/TS6138 in two files the recheck didn't check (chatEditingNotebookCellEntry.ts,
notebookModifiedCellDecorator.ts), not reproduced since: unused-tracking (reference marks, the deferred bodies' undo of
unused registrations when a private checker drops a materialization) seems to interact with the shared graph; the S0/P
workers should check it (hacks.md, the undefined_choice() entry).

**The decision: write-once shared words, private overlays, no waiting.** It takes B's invariant with A's storage.
- Every lazily computed value reachable from a shared object (the 18 sites below, variance slots, declaration memos)
  lives in one **shared word** on the object. A word goes null -> final once, by CAS with release, and is read with
  acquire. Final is either a clean value or POISON. Nothing else on a shared object is ever written after
  publication: no in-flight marks, no owner tags, no bits set later.
- **What is in flight lives only in the computing worker**: a per-worker ClaimSet keyed by the word's address (claim,
  re-entry and cycle detection, early members: today's MembersClaim, Resolving and VarianceClaim records, found by
  address instead of by the field's state) and a per-worker Overlay (word address -> value) for results that are not
  published. **No worker reads, waits for, or learns of another worker's in-flight state.** A worker that finds a word
  null computes. It never waits, so no wait cycle can form.
- At a computation's end: if it is **clean**, the worker CASes null -> value. If the CAS wins, the value is published.
  If it loses to another worker's clean value, the worker adopts the winner's value and drops its own, which nothing
  has referenced (proof below). If it loses to POISON, the value goes to the worker's Overlay. If the computation is
  **tainted**, the worker CASes null -> POISON and the value goes to its Overlay.
- **Intern-table memo kinds keep a private-first table** (the lookup is private table, then shared), so private
  priority comes for free there. A tainted entry stays private and is never inserted into the shared table; nothing
  needs poisoning. Only the shared-first words need POISON.
- **Reading a word**: the fast path is `v = acquire(word); if (v > POISON && !my shadowed) return v;` (POISON is 1,
  NONE is 2 for "computed: no value", so null still means not computed). The slow path is the Overlay (a hit is a
  taint event, see "clean" below), then the ClaimSet (my own in-flight: a cycle, as today), then compute. Without the
  flag there is one worker, so a tainted result can be published into the word as it is today (it is the only
  history), and the Overlay and ClaimSet are never touched: the default path keeps today's semantics. Its cost is
  measured below.
- **POISON** is final: nobody overwrites it, and a recycle drops the whole graph. A worker that reads it computes
  privately, without waiting, and keeps the result in its Overlay forever, clean or not. Each worker then has its own
  history for that key, exactly the default path's K private checkers. Measured share of POISON (G0 rule, taint build
  `-DTTTT_GRAPH_TAINT` at K12): intern memo kinds tainted 74 of 0.81 M published on effect, 44 of 0.89 M on drizzle,
  684 of 0.48 M on zod (0.01-0.14%). The Ty-resident kinds per G0's table: members and prop types 99.7-100% clean on
  every corpus. The sharing lost is under 0.2% of entries. The main exception is zod's tainted time, which is
  variances, and those have their own rule.

**"Clean", precisely** (one per-worker counter, `Checker.taint`, not thread-local; a frame records it at the start and
compares at the end, so taint propagates outward for free):
1. The computation read nothing in flight to this worker: no ClaimSet hit (its own cycle, early members, a variance
   claim of an enclosing measurement it isn't part of), no in-flight intern slot, no TY_CIRCULAR, no
   `ty_resolving_cycle()`.
2. It read no private value: every Overlay hit and every private-table hit of a memo kind whose entry is marked
   tainted is an event. There are no clean-but-unpublished values: a clean result is published or adopted at its
   end. So "depends on a private, not-yet-published value" can't arise for clean values, and every private value is
   tainted by definition. A shadowed word (below) is in the Overlay, so reading it is an event too.
3. It moved no limit counter (instantiation depth or count, relation depth or complexity, constraint depth 50, the
   conditional epoch). No relation it asked for ended at a **deeply-nested cut** or a depth limit
   (is_deeply_nested_as() answering Maybe at a level where the stamps decided, the relation's overflow): bun's
   `keeps_relations_for_this_task`, and the mechanism of 4e item 1's (b). Ordinary maybe-stack assumptions resolved by
   the top-level pair's end don't taint. They are tsc's context-free rule, the same one the shared relation cache
   already relies on. A private relation-cache entry made under a deeply-nested cut is marked, and hitting it later is
   an event.
4. It reported nothing at the current node (diagnostics as data on the memo otherwise, as `CallResolutionRare.errs`
   does). It ended with `flow_uncached == 0` if it is an expression kind (the G0 rule: declarations are flow-isolated).
   It ran in the typecheck phase.
5. Done-ness peeks (TAINT_PEEK) don't count (G0's lenient rule, cleandiff-checked). A done-ness peek doesn't count only when it cannot change the
   result. A peek that selects between results (ty_function_type_mentions()'s trimming of an instantiation's mapper
   is the example: divergence 5) is a read of in-flight state and must be an event, or be replaced by computing the
   value.

**Why it is deterministic by construction.** A clean value is a function of published clean values and identities.
Identities are hash-consed and pure (4b), and history-like ones carry a written reason. So any worker computing that
key cleanly computes the same value, and whoever publishes first doesn't matter. A worker's view is its own history
(everything it computed: tainted values in its Overlay, clean ones published or adopted), plus clean values computed
elsewhere, which it would have computed the same way. This is the view of a sequential checker that had computed
those keys earlier: the class of histories that checker recycling, stealing and K already produce (kdiff, the recycle
oracle). Nothing in flight elsewhere is ever seen, so a cycle is always entered from this worker's own stack, exactly
as one sequential checker enters it. This is what the redo emulated.

**Identity of lost-race objects (the rule).** A loser's objects never escape:
- Clean means nothing read the result before its end, because every read of a value under construction goes through
  the ClaimSet and is an event (rule 1).
- A computation may store its fresh objects only in its result, or in entries keyed by those fresh objects
  (KEY_PROP_WRITE_TYPE, KEY_SYNTHETIC_PROP_PARTS, KEY_PROP_MAPPED_TYPE, KEY_SIGNATURE_INSTANCE/TYPE of its own
  signatures, KEY_STRUCTURAL with its `reuse` prop). After a loss these are orphans that no key reachable by anyone
  names.
- Nested computations publish or adopt at their own end, before the outer one goes on, so an outer result only ever
  points at winners.
- Tainted results reach only the worker's Overlay and private tables: anything that reads them is tainted by rule 2,
  so no published value can point at a private object.

The census of pointer-identity uses (class 3 back-pointers: instantiate_members' `target`, builder_add_inherited, the
mapped `mapped_from`, FoundProp.prop, KEY_ARRAY_THIS_PROPS, the 4R root-prop comparisons of private/protected checks)
only meets winners or the worker's own private objects. The enforcement is a **debug-build publish check**, not a
release cost. TTTT_DEBUG_PUBLISH tags each arena chunk's allocations with (worker, computation serial). A tainted or
lost computation's allocation range, minus its nested published sub-ranges, is recorded as private. Each publication
walks the value one level deep (Members' props, signatures and index infos; a SigResolved's params; Prop.target) and
aborts with both stacks on a reference into a private range, or into a range not yet published that isn't the
publishing computation's. The release path pays nothing. Forwarding a loser's objects to the winner's was rejected:
once a tainted value's objects escape, there is nothing left to forward.

**Shadowed words (the one place priority needs care).** A worker can end a tainted computation of word w after
another worker published a clean value there (both were computing w at once). Its own tainted value has escaped
into its private state, so it must keep reading its own value, but the fast path would return the shared one.
Then the worker records w in its Overlay and sets `my shadowed`, and its fast paths take the slow path (Overlay first)
until its current file ends. At that file boundary nothing is in flight, so it retires its private state: a fresh
checker on the same graph and worker number, today's graph_epoch machinery for one worker. The next file starts in
the recycle class of history. Cost: one byte test on each fast path, measured below. Expected frequency: a race
between a clean and a tainted computation of one key, which needs a counter (TTTT_GRAPH_STATS) in step S0.

**Variances (the exception, by kind).** A variance group (the subjects one outermost measurement claims) is one
computation:
- Reads of the group's own claims are internal: restart-from-min (measured_variances()) makes the result independent
  of the entry point. Reads of anything else in flight on the worker taint it.
- Results are pure data, with no identity, so a loser can compare. Two workers measuring one group at once both
  measure; the first CAS of each slot wins. A loser whose values are equal adopts; one whose values differ (the
  VARIANCE_UNRELIABLE bit is known to differ run to run) is shadowed for those slots, like a tainted word.
- A tainted group publishes its slots as POISON. Each worker that needs them measures them in its own history: the
  default path's semantics, at the default path's cost.
- Track S's warning (withheld variances measured again in a warm graph changed excalidraw's output) concerned
  withholding *clean-by-group* results, so the group rule doesn't withhold them. Gate: the excalidraw 20-run table
  with the variance group rule on.

**The 18 lazy sites** (4d step 3), each as a shared word:
| site | file: function | word on | value | notes |
|---|---|---|---|---|
| members | members.c ty_members_resolving(), early members | TyObjectState | `Members const*` | early members live in the ClaimSet record (they already live in the MembersClaim) |
| raw members | members.c declared_raw_members() | TyObjectState (raw) | `Members const*` | Members.rare is written before publication (no late CAS) |
| mapped unexpanded | members.c ty_mapped_info_unexpanded() | TyObjectState.unexpanded | `Members const*` | |
| equivalent base | members.c ty_non_augmenting_base() | TyObjectState | `Ty const*`, NONE for none | base_state byte deleted |
| reduced | checker.c ty_reduced_slow() | TyObjectState | REDUCED_* as a small tagged value | reduced_state byte deleted |
| prop type | members.c prop_type() | Prop.type | `Ty const*` | write_type_known moves into the KEY_PROP_WRITE_TYPE entry's presence (TSan B) |
| signature resolution | members.c signature_resolve_slow() | Signature.res | `SigResolved const*` (params, counts, type params, this, ret if a class's) | readers go through `SigResolved const* r = signature_resolve(me, sig)`, ~500 reads in ~15 files (step S1) |
| signature return | members.c signature_return_type() | SigResolved or Signature | `Ty const*` | its own word: inferred returns are lazier |
| predicate | members.c signature_predicate() | Signature | `TyPredicate const*`, NONE | predicate_done/_writing bytes deleted |
| param constraint | members.c declared_param_constraint() | TyParamState | `Ty const*`, NONE | `circular` becomes a word as well, or part of the value record (TSan A) |
| param default | members.c ty_param_default() | TyParamState | record {type, circular} | fallback_circular folded in |
| mapped template | members.c ty_mapped_template() | MappedInfo | `Ty const*` | MappedInfo copies copy only immutable fields plus the word's acquire-loaded value (TSan D) |
| class base variable | members.c ty_class_base_type_variable() | SymbolLinks | `Ty const*`, NONE | |
| cond branch | checker.c ty_cond_branch_type() | TyCondBranch | `Ty const*` | instantiate_branch() copies field by field already |
| declared type | lower.c ty_declared_type_of() | SymbolLinks | `Ty const*` | |
| reference args | lower.c ty_reference_args() | deferred record | `TyList const*` (boxed) | |
| implied constraint | lower.c conditional_flow_type() | implied record | `Ty const*` | |
| function return | function.c function_return_type() | memo | `Ty const*` | KEY_FUNCTION_RETURN stays an intern kind |
Plus declaration memos (lazy_type.c, graph.c: the shared memo store's slot becomes a word to an immutable LazyMemo
copy, so the bitfield `done` read without the lock goes: TSan B), variance slots (relate.c, graph.c: as above), and
the singletons (already first-wins CAS).

**TSan inventory (the tooling worker's categories, 55 groups on de94e1179) and what removes each:**
- A, mixed atomic and plain access of one lazily written field (TyParamState.circular, item, predicates, reference
  args, base_cycle): removed by construction. A word is a typed `SharedWord` (a struct around an `_Atomic` pointer)
  accessed only through sw_load() and sw_publish(), so a plain access doesn't compile. SymbolLinks.base_cycle becomes
  a word or a byte of its own.
- B, bitfield RMW on published objects (Prop.write_type_known, LazyMemo.done): removed. Nothing on a shared object is
  written after publication except words; flags move into the word's value record or into a side entry.
- C, fresh objects reached before their stores are visible (ty_alloc fields, TyObjectState, literal twins, order
  keys, the type directory's chunk slots): removed where the path goes through a word or a shared table. Those are
  the only cross-worker hand-offs: release at the CAS or insert, acquire at the read. Order keys get computed at
  construction or become a word. The id directory's chunk slots get atomic stores and loads (lazy_slots_add_type /
  lazy_slots_type). Any hand-off path TSan still finds is a bug against the rule, not a formal race to waive.
- D, structs copied while another worker writes them (MappedInfo): removed. The copied struct's mutable part is a
  word, read with sw_load().
- E, data mutated in place after publication (ty_function_type_mentions() into members and signatures,
  ty_function_type() initializing a visible type): forbidden by the rule. It is computed before publication or
  becomes a word of its own (the mention set as a value).
- The two "x86 saves us" fast paths: gone, replaced by the single acquire load of the word. Measured below.

**What gets deleted, and why the rule makes it unnecessary:**
- The wait protocol (lazy_wait_slow(), LAZY_SNAP's wait, the global wait-for graph and its mutex, wait yields and
  sleeps, graph_fallback_hook): nobody waits for another worker, so there is no wait.
- The redo (check_file_or_give_up(), Checker.redo_jmp, graph_retire(), MAX_FILE_REDOS, the retired checkers and
  their leaks, the given-up file's ErrorCtx): it existed to break wait cycles.
- The owner-tagged states (LAZY_CLAIM/LAZY_DONE/LAZY_COPIED, `IN_FLIGHT | worker << 2`, the 40 lazy_in_flight
  sites): in-flight is per-worker state.
- The variance lock: already gone. The lib warm-up (ty_graph_warm_up()) is no longer needed for correctness; it was
  there to cut wait cycles. It stays only if measuring shows it saves duplicated work at the start (12 workers
  computing Array's members at once all publish-or-adopt) and goes otherwise.
- TTTT_GRAPH_WITHHOLD and the -DTTTT_GRAPH_TAINT build: the cheap taint is always on with the flag.
- The plain-load hacks.md entry.

**Measured costs** (callgrind whole process, release build, K1 default path, against de94e1179; throwaway probes in
/tmp, deleted):
| probe | nest | drizzle | zod |
|---|---|---|---|
| base | 1.144 G | 6.719 G | 4.991 G |
| per-worker shadow byte test on four fast paths (ty_members, prop_type, signature_resolve, signature_return_type) | +0.09% | +0.49% | +0.44% |
| + acquire instead of the plain loads | +0.17% | +0.63% | +0.54% |
| B's hot path: per-worker dense table by type id for members, plus a hash probe by Prop for prop types (two kinds only) | +0.26% | +2.96% | +1.94% |
| the cheap taint compiled in (-DTTTT_GRAPH_TAINT, includes TAINT_READ bitmap tests on fast paths, which the hybrid drops) | +0.09% | +1.00% | +0.70% |
- **The hybrid's default-path cost**, estimated at +0.5-1.0% on drizzle and zod, under 0.2% on nest. Of that, about
  0.5% is the shadow test plus acquire. The word design removes the state load (one load instead of state and
  value), which should give some of it back. The taint frames come in at slow-path boundaries only (no
  TAINT_READ on fast paths: private reads are slow-path Overlay hits), well under the taint build's 0.7-1.0%. Step
  S0 measures the real number. The budget: K1 default path within 1% in total. Over that, the coordinator decides
  (proposal: the shadow test only with the flag, through a per-checker fast-path function pointer, is not worth its
  indirect call; the alternative is dropping the per-file shadow and making a shadowed word end the worker's file at
  once with a fresh checker for the rest).
- **B costs 4-6x as much on the hot path** for two kinds, and needs every kind keyed by table. Memory: a dense
  per-worker array by global id is K copies of the id space.
- **The sharing lost**: per-computation publication is what 4d step 4 already does (work K12/K1 1.10-1.12, track S's
  per-share 1.08-1.10 as the reference). POISON adds under 0.2% of entries (above). Work K12/K1 after 4e item 1:
  drizzle 1.117, zod 1.097, effect 1.078.
- **Effect's +10% wall** at K12 against the defaults is not waits or redos: TTTT_GRAPH_STATS shows 12-120 waits
  (0.7-2 ms in all) and no redo, and work is 1.078x K1. Types are K1's (553k vs 552k), CPU 2.97 s against def12's 2.63.
  So it is per-instruction cost on a shared, unrecycled graph: claim CASes write to shared cache lines at every
  computation's start and end, and the RSS is 826 against 561 MB. The hybrid writes a shared line once per value, at
  publication, so this should shrink, but it isn't proven: measure at S0 and at D. Drizzle's waits (170-680, 0.55-0.65 s
  summed) and redos (2-4) go with D.

**Build-out** (each step one commit or a few, default path byte-identical, every gate below green at its end; owners
in parallel only where the file sets are disjoint):
- **S0, the primitives (one worker, first; ~1 session).** `SharedWord`, sw_load/sw_publish/POISON/NONE, the per-worker
  ClaimSet and Overlay (new `src/ty/overlay.{h,c}`), `my shadowed` and the file-boundary retire, the always-on cheap
  taint in `Checker` (taint.h's GRAPH branch made runtime, intern hooks in checker.c's intern_put/intern_get behind
  `my graph`), the deeply-nested-cut event (relate.c is_deeply_nested_as() call sites only), TTTT_GRAPH_STATS
  counters (POISON, adopts, shadows), TTTT_DEBUG_PUBLISH. Files: ty.h (lazy macros), taint.h, overlay.{h,c},
  checker.c (intern hooks), relate.c (one call site). Measure the default path here.
  **S0 done (2026-10-22, wt-design4f).** As built:
  - `src/ty/overlay.{h,c}`: SharedWord, sw_get() (fast path, in ty.h), sw_look(), sw_claim(), sw_finish(). The
    pattern of a site is in overlay.h's header comment.
  - ClaimSet and Overlay are one per-worker map by the word's address (value 0 = in flight here).
  - Without the flag the word is written whatever the taint, and in flight is a LazyState byte next to it (only
    NOT_STARTED / IN_FLIGHT), so every site keeps one byte for the default path.
  - `Checker.graph_shadowed`: program.c hands over to graph_fresh_worker() at the next file boundary, the same
    helper the redo's give_up_file() uses. TTTT_GRAPH_FORCE_SHADOW=1 treats every clean loser as shadowed, to test
    that path: 210 retirements in 10 effect runs, output K1's.
  - The counter: TAINT_EVENT() is a cold out-of-line bump of the thread-local `graph_taint_now`, always compiled.
    Inline, it changed hot callers' inlining (+0.37% drizzle). Not in `Checker`, because the event sites don't all
    have `me`.
  - The intern hooks (graph_taint_miss/put) and the global mark set stay `-DTTTT_GRAPH_TAINT`-only: turning
    publication of intern memo kinds into publish-only-clean is P3's. So S0 changes no memo kind's publication.
  - Rule 3: relation_deep_cut() at relation_deeply_nested() and ty_is_deeply_nested() returning true (a taint event,
    `Checker.deep_cuts`). `Checker.relation_deep_seen` is saved and restored per pair, like variance_reports. Pairs
    concluded around a cut are cached CACHE_DEEP; a hit is an event, and they are never put in the shared relation
    cache.
  - TTTT_DEBUG_PUBLISH=1: every claimed computation's arena range is recorded at its end (private if tainted or
    lost). sw_debug_check_ref(ref, what) aborts on a referent whose innermost recorded range is private. Nothing
    calls it yet: the example's value is a type, an identity. P1 calls it from the walks of Members, Props and
    SigResolved.
  - Worked example: ty_non_augmenting_base() (TyObjectState.equivalent_base a SharedWord, NONE for none).

  Gates:
  - Default path callgrind against ai-main db9ca5304: nest +0.0%, drizzle -0.06%, zod +0.04%. The suite is
    unchanged.
  - Every corpus with the flag: plain K1 = K1 = K4 = K4 T4 x2 = K12 T12 x2.
  - 20-run K12 tables drizzle, zod, effect, excalidraw and nest 20/20 each.
  - Seeded scheduler (TTTT_GRAPH_SEED): effect and drizzle 100/100 seeds, and seed 18 (the TS7006) passes, also on
    base at db9ca5304.
  - TSan on effect and drizzle T12: no report in overlay.c or the example.
  Counters per K12 run (means of 20):

  | corpus | words published | adopted | poisoned | after poison | shadowed | deep cuts |
  |---|---|---|---|---|---|---|
  | drizzle | 18.7k | 9 | 0.6 | 1.8 | 0 | 17k |
  | effect | 47.4k | 133 | 0 | 0 | 0 | 25k |
  | zod | 40.1k | 123 | 0 | 0 | 0 | 5.4k |
  | excalidraw | 5.0k | 13 | 0 | 0 | 0 | 1.8k |

  Rule 3's cost to sharing, measured with the taint build: tainted intern memo entries go from 0.005-0.14% to
  0.19-0.28% (drizzle 1150 of 443k, effect 721 of 385k, zod 650 of 233k).

  **The fingerprint oracle's five divergent publications (the tooling worker's 20-run list), by owner:**
  1. zod `$replace` base constraint and KEY_DISTRIBUTIVE_CONSTRAINT: P3 (memo and node-link kinds published only
     when clean).
  2. Method property types published as an instance of the function type (literal_instance_is_itself,
     ty_function_type_mentions(): TSan E): P1.
  3. drizzle PgArrayBuilder/GelArrayBuilder variances: P3 (variance groups).
  4. KEY_UNION_PAIR of Promise unions: P3 (a memo kind; if it is identity purity instead, the owner of checker.c, P2).
  5. effect inferred returns with `any` (the TS7006/TS2769 residue): P2 (function.c's function returns).
  Standing gates for P1-P3: `seeds.sh` 100 seeds on effect and drizzle, seed 18 on effect.
- **S1, SigResolved (one worker, alone, mechanical, Sonnet-able with an Opus review; ~1 session).** Signature's
  resolved fields move into an immutable record behind `Signature.res`, and every reader takes it from
  signature_resolve(). Touches call.c, relate.c, infer.c, members.c, expr.c, check.c, ty_display.c, decl_emit and
  the rest that read `params`, `param_count`, `min_args`, `type_params`, `this_type`. Still published as today; no
  semantic change. It goes alone because it touches every file.
  **S1 done** (ff5038d11, 23a1ac999, 21ec4186b; byte-identical on every gate, callgrind +0.04-0.12%). What P1 needs:
  - The record (`SigResolved`, 56 B: type_params, params, this_type, param_count, min_args, and the *effective*
    target and mapper) is allocated at the claim and filled in place in the old order. `r->target`/`r->mapper` start
    as Signature's made ones, and the JSDoc `@type` tag (target) and clone_type_params() (mapper) replace them later.
    Signature.target/mapper are as made and never written after. Readers use sig_target()/sig_mapper(), or the plain
    fields where `!sig->function` guards (a JSDoc target is only a function's) and in ty_erased_signature().
    Unresolved signatures point at `&sig_resolved_none`: **every maker must set `res`** (9 makers and expr.c's
    context_checked_none sentinel; a miss is a null deref).
  - **Re-entries that see the partial record** (probed: 3 in the suite, 2 on the corpora at K1): a binding's assigned
    type reaching ty_quick_expr_type() (flow.c, reads type_params of a signature resolving its params, then its
    return), and applies_to_signature() via ty_instantiation_keeps_signature() (ty/expr.c, type_params). P1's private
    fill must keep showing them what is resolved so far (the ClaimSet record).
  - **sig_resolved_mut()**, a fresh signature's maker changing its own record after resolving it:
    ty_signature_instantiation() (infer.c) and ty_erased_signature()'s any-instantiation (relate.c) clear
    type_params; ty_union_signature() (relate.c) and decorator_call()'s mutator (call.c) set this_type.
    ty_signature_with_type_params() (infer.c) makes a new record. All before publication; P1 can build these
    records whole instead.
  - **signature_copied()** at the four `*copy = *sig` sites: ty_signature_with_type_params() (infer.c),
    ty_implementation_compatible_with_overload()'s two copies (relate.c), the mixin construct-signature clone in
    members.c's intersection members. A copy of a signature in flight here gets a snapshot of the record; one that
    restarts (in flight elsewhere) gets none.
  - Still in place on Signature, each its own word in the table above: ret/ret_state (including a class signature's
    ret, set by signature_resolve_slow()) and predicate/predicate_done/predicate_writing.
- **Then three in parallel:**
  - **P1, members.c** (members, raw, unexpanded, equivalent base, prop types and write types, signature resolution
    and return and predicate, param constraint, default and circularity, mapped templates and MappedInfo copies, class
    base variable, Members.rare, ty_function_type_mentions()). One worker, the biggest share, ~2 sessions.
    **P1 progress (wt-s1).** Done, as shared words with the default path's byte next to each:
    - **Prop types**: `Prop.type` (a Ty, NONE for null) with `type_flight`. prop_type()'s re-entry and cycle answers
      are as before, on SW_LOOK_MINE. Peeks go through prop_type_peek() (sw_peek) or prop_type_known() (published
      only, for the fingerprint oracle and fresh props). prop_copy_into() copies the known value, and a copy of one in
      flight here stays in flight (sw_copy_in_flight()), as the copied state byte did. **write_type_known is gone**: a
      KEY_PROP_WRITE_TYPE entry's presence is it (intern_has(), checker.c). TSan B for Prop is removed.
    - **Signature resolution**: `Signature.res` (a SigResolved) with `res_flight`. The record is filled privately and
      published once, complete. While it is in flight, this worker's re-entries find it on Checker.sig_resolving (a
      stack of frames) through sig_res()'s and signature_resolve()'s slow paths. ty_quick_expr_type() and
      applies_to_signature() (S1's two partial-record re-entries) still see their worker's partial record and never
      another's. sig_resolved_mut() is gone: an instantiation of its own type parameters loses them inside its
      resolution, and ty_union_signature() and the decorator mutator build a new record. A copy of a signature in
      flight here gets a snapshot record, published as resolved (before, the copy's state stayed in flight forever:
      the one semantic difference, no oracle shows it). sig_res(), sig_target() and sig_mapper() take `me`.
    - Helpers in ty.h: sw_look_fast()/sw_claim_fast()/sw_finish_fast(), sw_look/claim/finish with the default path
      inline (out of line they cost ~1% of drizzle), and sw_copy_in_flight() in overlay.c.
    - Gates (K12 T12 with the flag unless said): the default path is byte-identical (suite; drizzle, zod, effect, nest
      and excalidraw at K1 and the defaults); the 20-run tables are 20/20 on drizzle, zod, effect, excalidraw and nest;
      seeds 100/100 on effect and drizzle; recheck 0 files differ on drizzle. Counters per run (published / adopted /
      poisoned / shadowed): drizzle 534k / 426k / 0.9k / 0, effect 503k / 260k / 20k / 0, zod 398k / 245k / 6.7k / 0,
      excalidraw 136k / 88k / 2.9k / 2 in 20 runs, nest 13k / 7.7k / 60 / 1 in 20 runs. The adoptions are 12 workers
      racing on the same lib members at the start.
    - **Cost**: callgrind K1 default path against 2732ae1d3: drizzle +0.45%, zod +0.68%, nest +0.09%. That is an
      acquire load plus the shadow-byte test per read: gcc doesn't merge repeated atomic loads in one function as it
      did the plain loads. The record is hoisted into locals at ~60 sites, the slow paths are out of line, and the
      default path is inline. prop_type_slow's +17 instructions a call (480k calls on drizzle) are the claim and
      finish word pointers kept live across the computation (spilled around its calls) and the graph test made three
      times; the checks are ordered cheapest first. The coordinator accepted this for the P steps, with the recovery at
      step D: (1) the default-path state bytes (type_flight, res_flight and the others) go once the plain path is
      retired, and (2) the per-read shadow test can be replaced by the design's alternative (a shadowed worker ends its
      file at once and a fresh worker rechecks it) if the natural shadow count stays near zero. So far it is 0 on
      drizzle, effect and zod, 2 on excalidraw and 1 on nest in 20 runs each.
    - Fingerprint: divergence (2), method property types (effect Match.ts tagStartsWith, excalidraw App.tsx clearHover,
      drizzle select.ts createIterator, dialect.ts mapToDriverValue), persists. It isn't prop_type()'s word: it comes
      through ty_function_type_mentions()'s instantiation identity, a later group. The fingerprint build segfaulted on
      effect in graph_debug's describe() of KEY reference args (fixed on ai-main by 04c1f100b).
    - **Signature return and predicate (held, wip 5ffd63edc)**: Signature.ret and .predicate are words, the class
      signature's return in SigResolved.class_ret. Gated except excalidraw: 20/20 runs differ, because App.tsx's
      `perform` returns lose their contextual return type. A worker that no longer waits for another's in-flight
      signature return infers the function's return itself, context-free, and that counts as clean. Lands after
      wt-ctx (the contextual signature as a function of the node).
    - **Type parameter constraint, default and circularity**: TyParamState.constraint (a Ty, NONE if unconstrained),
      .fallback (a Ty, NONE, or the ty_param_default_circular_marker: fallback_circular folded in) and .circular
      (PARAM_NOT_CIRCULAR/PARAM_CIRCULAR, no in-flight state: a re-entry computes it again, the first answer stays,
      sw_finish() with a graph) are words. TSan A for `circular` is gone, and TyParamState.item is read with acquire
      (set_param_item()'s CAS). Fresh parameters' constraints go through ty_param_constraint_init() (checker.c,
      lower.c, relate.c, conditional.c). A reverse mapped property's `asserted` drop, which prop_type() wrote into a
      published PropDecl (TSan E), is a word of its own (PropDeclRare.asserted_dropped), read by prop_decl_asserted().
    - Fingerprint divergence (1), zod `$replace<Meta, S>`'s distributive constraint, persists with the constraint word:
      it isn't declared_param_constraint().
    - **Effect seed 37** fails (the TS7006 `interruptAsFork: (id)` residue) with this group and not without it. The
      cause is the seeded scheduler, not the conversion: LAZY_CLAIM() was a scheduling point and sw_claim() isn't, so
      every converted site removes points and a seed's schedule shifts. With graph_sched_point() back in sw_claim()
      seed 37 passes 2/2. 1d083898f fails seed 152 of seeds 101-200 (TS2375) and this group passes all of 101-200.
      Proposal for the scheduler's owner: sw_claim() calls graph_sched_point(), so converted sites keep the seeded
      coverage the claims had.
    - **Per-worker stores read inside word computations (the coordinator's audit, members.c):** signature_resolve_slow()
      reads KEY_CONTEXT_CHECKED (ty_function_assigned_contextual_signature(): type parameters from a generic
      contextual signature, a JavaScript function's minimum arguments) and KEY_CONTEXT_THIS. compute_signature_predicate()
      reads KEY_CONTEXT_CHECKED. ty_class_base_type_variable() and class_base() read ty_expr_type() of the extends
      expression (the expression's type as this worker has it). members' resolution reads the worker's own in-flight
      stacks (MembersClaim's early members, bases_frames, members_in_flight, eager_probe_depth); those are rule 1's
      in-flight reads, counted by the cycle machinery. Making the two contextual reads taint events doesn't help (a
      private recompute is context-free all the same: P2's measurement), and that fix is wt-ctx's.
    - **Members, raw members, unexpanded members, mapped templates, the class base variable**: TyObjectState.members
      (Members, never null) with members_flight, declared raw members (the same TyObjectState of Ty.declared.raw),
      TyObjectState.unexpanded (no in-flight state of its own: settled first-wins without a graph, sw_finish() with one,
      word_settle()), MappedInfo.template_ (MappedInfo copied field by field, so the copy never reads the word: TSan D)
      and SymbolLinks.base_variable are words. ty_members()'s fast path is the acquire load and the shadow test (the
      x86 plain-load hack is gone). Early members stay in the MembersClaim (this worker's own); every in-flight test is
      members_mine_in_flight(). ty_mapped_info_unexpanded() claims the members word and abandons it (sw_abandon()).
      ty_members_done() and the peeks (conditional.c, instantiate.c, the eager-base probe) go through
      ty_members_peek(). Fresh types' members go through ty_members_init() (relate.c).
    - **ty_function_type_mentions()** (conditional.c), rule 5: its peeks selected between results (known mentions,
      a trimmed instantiation mapper; unknown, the whole mapper: divergence 5). The members and the signature's
      resolution are computed now instead of peeked (the default path's output doesn't move: suite, five corpora). An
      object literal's property-type peek is a taint event. The return type's peek stays: inferring it there changes the
      default path (a suite test, and excalidraw's output, when the return type was forced). It becomes a taint event
      with the held return-type commit, whose line it is (the gates ran with it as one).
    - **SymbolLinks.base_cycle** (P3's hack): REENTERED and IN_CYCLE are set by a declared type's raw members
      resolution, CONSTRUCTOR by class_base(), which more than that resolution calls (the static side, the base
      instance, a mixin's check). REENTERED and IN_CYCLE can move into the raw Members (MembersRare, set before
      publication, read through declared_raw_members()); CONSTRUCTOR wants a word of its own (class_base()'s cycle
      result per class). Not done in this session.
  - **P2, checker.c + lower.c + function.c + instantiate.c** (reduced, cond branches, declared types, reference args,
    implied constraints, function returns, ty_alloc and the directory's chunk slots (lazy_type.c's lazy_slots_*),
    order keys, literal twins: TSan C). ~1-2 sessions.
    **P2 progress (wt-p2).** Done: function returns (FunctionReturn's word, a Ty tagged FR_CIRCULAR/FR_PROMISE_MISSING)
    and declared types (SymbolLinks.declared, a Ty tagged SYMBOL_DECLARED_CIRCULAR; links->circular folded in).
    Findings:
    - **Identities must publish whatever their taint.** An interface's, a class's and a type parameter's declared type
      is a fresh TY_DECLARED made of identities. Poisoned, every worker would make its own interface: forcing every
      declared type private (a throwaway knob) gave 62k differing lines on effect. ty_declared_type_of() treats those
      branches as clean (`identity`), and the same knob then keeps K1's output. Any other site whose value is a fresh
      identity rather than hash-consed (P1: members' Props and Signatures are values, not this) needs the same review.
    - **An argument's return inferred under its call's inference is in flight** (rule 1: it reads the outer
      inference). Published, it was the TS7006 residue under seeds (fiber.ts `interruptAsFork: (id) =>`): effect 2 of
      100 seeds, 0 with the event. The event is at function_return_type()'s keep_outer; at the reads of the outer
      inference (call.c's outer_inference_mapper() & co., expr.c) it would poison less (effect's poisoned words 2.9k ->
      20k with it, summed K12 types unchanged).
      **Follow-up (step D or later):** the narrower event, at the reads of the outer inference (call.c
      outer_inference_mapper(), the OuterInference reads at its other uses, expr.c's two `my outer_inference` tests in
      check_with()/ty_check_expr()) instead of at keep_outer. Poisoned words per K12 run with the wholesale event
      (all words, means of 20): drizzle 681, zod 5753, effect 19985, excalidraw 2788, nest 54; before it (function
      returns alone): drizzle 107, zod 1426, effect 2943, excalidraw 311, nest 2. The gate: effect seeds 100/100.
    - **Divergence 5 (Pretty.ts) is a peek that decides an identity**, not the function return's publication:
      ty_instantiate() of a function expression's anonymous type trims the mapper to what ty_function_type_mentions()
      says it mentions, and that answers "unknown" (keep every source) unless the members, the signature and the
      inferred return are already done: done-ness peeks (TAINT_PEEK) that the lenient rule doesn't count. The same
      printed type then has two identities (mapper of 3 sources in K1, of 1 in K12). Rule 5 needs an exception: a peek
      whose answer changes an identity (an instantiation's key) is an event when it fails, or the trimming must not
      depend on done-ness. Owner: whoever takes ty_function_type_mentions() (P1's divergence 2 is the same function).
    - The enum's members are finished from inside the enum's resolution (ty_set_declared_type(), sw_finish() without
      a claim, with the enum's start taint: a clean prefix is clean). The member's own claim, if any, sees
      sw_mine_in_flight() false and takes the value, as the nested re-entries do.
    - Also converted: reduced intersections (TyObjectState.reduced, REDUCED_ITSELF/NEVER), deferred references'
      arguments (TyDeferredRef.args: the array, the count immutable in arg_count), the type directory's slots
      (release/acquire). The category C TSan groups on drizzle/effect/excalidraw/react all reach the type through a
      declaration or expression memo (P3's LazyMemo publication), not through the slots.
    - **Per-worker side data read or left behind by a publishing computation (the audit of P2's files, 2026-10-22).**
      A published result is adopted without the side data its computation left in the computing worker's scratch
      links, and a computation that reads another computation's side data depends on whether this worker did that one.
      Two kinds, with opposite answers:
      1. *Self-produced* (the computation itself writes it, a later report of the same worker reads it): an event at
         the write (TY_WORKER_STATE()), so the result stays private and every worker that needs it recomputes it and
         has the data. Done: KEY_RETURN/YIELD/ELEMENT_WIDENED_ANY (function.c, lower.c: the implicit-any widening
         reports), KEY_JS_IMPLICIT_ANY (lower.c), KEY_BINDING_ACCESS_KEY (TS2538's key), ty_mark_member_referenced()
         (checker.c: unused.c's report_class_members() reads each checker's own marks; the likely cause of the vscode
         TS6133/TS6138 residue above), a recorded KEY_CIRCULAR_BINDING hit, the KEY_BINDING_COMPUTED_KEY in-flight
         marker. Not needed: KEY_CHECKED_ONCE, KEY_DEFERRED_BODY and the reports themselves (diagnostics merge
         program-wide with duplicates dropped); KEY_RETURN_LATE_CIRCULAR (only after a MINE event); KEY_REST_SYMBOL (the
         type carries anonymous.rest_binding too); KEY_SYNTHETIC_ACCESS (a per-worker Expr with its own slots).
      2. *Produced by another computation* (the contextual-typing links: KEY_CONTEXT_CHECKED, _PARAM*, _RETURN, _THIS,
         _IIFE_RETURN, _RETURN_SELF, _FREE, KEY_YIELD_CONTEXT, KEY_PATTERN_COMPUTED; the enclosing call's resolution
         assigns them): an event does **not** work. Measured: events at every read and write kept excalidraw, drizzle,
         zod and nest 20/20 but made effect 0/20 and 24/100 seeds (`circular.ts(806,21) TS7006 'id'`). The private
         recompute runs in a history that never resolved the enclosing call, so it is context-less, and it records
         `context_checked_none` for good. Rules 2 and 5 make every read an event, absent or present, so no placement
         helps. **The by-construction answer is (b)**: the contextual signature as a function of the node, resolved on
         demand from its position (tsc's getContextualSignature / contextuallyCheckFunctionExpressionOrObjectLiteralMethod:
         a fresh checker gets the same one), which also recovers the sharing. This is what P1's signature-return word
         needs (excalidraw's `perform` widened in 20/20) and very likely the fiber.ts/circular.ts TS7006 residue.
         expr.c/call.c, its own item.
      Stack-scoped per-worker state read by nested computations (candidates, not yet shown to matter): lower.c's
      contextual_patterns (read by expr.c identifier flow), function.c's return_inference_of/_skipped and
      return_context, Checker.for_constraint_root (its conditional instances are keyed without it: an identity whose
      resolution path differs by context, but whose key is the same).
    - **Known identity-history holes.** Conditional instances: instantiate_branch() decided an instance's branch
      representation by whether the root's branch was lowered yet, and the instance (an identity, keyed by root and
      mapper) is published at creation, so whoever made it first fixed it for every worker. The two history-free
      choices change output (always from the node: a spurious TS2344 at type-fest object-merge.d.ts(173,48); always
      lowering the root first: 3 suite tests). Option B (the instance keeps the root's branch; the choice is made at
      pick time, the not-lowered case a taint event; patch kept out of the tree) is byte-identical on the suite and 12
      corpora at K1 but not landed: with the flag cgtc K12/K1 work drizzle 1.191 -> 1.263, zod 1.217 -> 1.274, K1 with
      the flag +1.9% (the poisoned words' Overlay), and zod differs in 5/5 K12 runs (a TS2345 at function.test.ts
      (257,38)): the two paths aren't equivalent, and a worker's view of "the root is lowered" mixes its history with
      others' publications. **The fix is to make the two paths agree** (why lowering from the node differs from
      instantiating the root's lowered type: type-fest object-merge.d.ts(173,48), ttttTemplatePlaceholderCurrentNode,
      distributiveConditionalTypeConstraints, recursiveConditionalCrash4); then the choice and the hole vanish. P3's
      fingerprint divergence (1) (zod `$replace`'s distributive constraint) starts at a conditional branch: probably
      this.
      tsc has only the node path (getConditionalTypeInstantiation re-evaluates getConditionalType from root.node with
      the combined mapper, cached by type-argument ids), so tttt's "instantiate the root's lowered type" path is an
      optimization whose non-equivalence is a fidelity bug: the type-fest TS2344 under the node path is tttt's node
      path differing from tsc, the 3 suite tests failing with the root lowered first are the lowered path's. Branches
      as landed (A): TyCondBranch.lowered is the word, conditional.c's branch_type()/branch_type_with() compute fresh
      (their peeks deleted: no output change), instantiate_branch() keeps the creation-time choice through
      ty_cond_branch_known() (the hole), relate.c's cond_branch_may_be_never/_check() peek it (selecting, no event yet).
      **Proven: P3's fingerprint divergence (1)** (zod registries.ts `$replace<Meta, S>`'s distributive constraint,
      KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT: K1 a 4-member union, K12 an aliased 2-member one) is this hole: with
      instantiate_branch() forced to the node path (a throwaway knob) `fingerprint.sh 2 zod` has 0 differences (9 per
      run otherwise). The paths-agree fix is its fix.
  - **P3, relate.c + graph.c + lazy_type.c + shared_table.c** (variance groups without waiting, declaration memos as
    words, memo kinds publishing only clean entries in graph_memo_flush(), the shared relation cache's deeply-nested
    rule). ~1-2 sessions. The tooling worker's hooks in graph.c and shared_table.c are its neighbours: rebase often.
    **P3 progress (2026-10-22, wt-p3).** Built:
    - *Intern memo kinds publish only clean entries* (graph.c MemoTaint, runtime whenever the checker shares memos,
      `graph_memo_on`). A computation starts at its key's private and shared miss (graph_memo_miss() from
      intern_shared_find(), a per-checker direct-mapped table; a pushed-out miss leaves its start in a floor bucket,
      so it is never taken for clean) and ends when its value is there (the entries put since the last event are
      pending: an event finds the ones still without a value and taints them, graph_memo_sweep()). Tainted entries
      stay private and a hit of one is an event (InternTable.taint, null without a graph: one load and branch per
      hit). Node-link memos through ty_link_set_memo()/ty_link_get_memo(): KEY_BASE_CONSTRAINT, KEY_FUNCTION_FREE_PARAMS
      (KEY_IMPLIED_CONSTRAINT is P2's word). The -DTTTT_GRAPH_TAINT build's hooks are the older duplicate (step D).
    - *Which memo kinds are values*: every GRAPH_MEMO kind but four, checked producer by producer (2026-10-22). The
      identity-valued ones, whose value is a fresh object no other key hash-conses and which publish whatever their
      taint, like identities: KEY_MAPPED_KEY_PARAM (a synthetic stand-in type parameter in a fresh mapper),
      KEY_EVOLVING_ARRAY (ty_alloc'd, this kind is its only hash-consing), KEY_REVERSE_MAPPED (a fresh reverse mapped
      object at the top level) and KEY_UNION_NODE (a lowered node's type; fresh only for a stray `infer X`), and,
      found by react-navigation's kdiff after the rebase onto P2's words, the conditional instances
      (KEY_CONDITIONAL_INSTANCE, _FOR_CONSTRAINT: an arena record of an interned result, but their inference can make
      fresh nested reverse mapped objects; withheld, react-navigation's TS2344/TS2345 at K16 T1; not proven which of
      the two, fresh objects or a clean value that differs, so fingerprint (1)'s _FOR_CONSTRAINT entry is to watch).
      Values: alias instances, regular unions, widened
      objects and regular object literals (KEY_STRUCTURAL identities), keyof properties, simplified, normalized
      intersections, mapped filtering, union access, inferred true branches, distribution-dependent,
      distributive constraints, type variable reductions, definitely-related, canonical and base signatures (interned
      instances; the base signature's mapper holds interned types and is keyed by nothing), variances, alias markers,
      iteration types, widened nullables, intersection prop contexts, union pairs, intersections, union key names and
      members, flow not-unit, reduced unions, function type mentions, array this-props, the name orders, thisless
      signatures, and the intersection properties table (FoundProp records of interned types and existing Props).
    - *Relation cache*: a pair whose comparison saw any taint event (not only a deep cut) is cached CACHE_DEEP:
      private, a hit an event, never shared (fails_common_properties()'s failures too). relation_shares() checked:
      generic relation keys stay out, nothing CACHE_DEEP/UNRELIABLE/UNMEASURABLE/INDEX_INFERRED or measured under a
      variance measurement is published.
    - *Variance groups without waiting*: VarianceSlot.value is a SharedWord (no state, no LAZY_SNAP/LAZY_CLAIM, no
      graph_variances_publish() of states). The group is the slots one outermost measurement found empty; it is
      clean if every taint event in it was internal: the running measurement reading its own subject in flight
      (always Ternary Unknown), or a read of a group-mate's not-yet-published values (Variances.pending). Reading
      an enclosing measurement's claim taints it (the View case of seed 379: a nested measurement's Unknown-driven
      unreliable result). Clean: each slot CAS'd, an equal loser adopts the winner's, a differing one keeps its own
      (marked tainted; none seen: 0 differing in every run); tainted: POISON and private. A subject found poisoned is
      measured privately, its entry tainted. variances_known() reads only published values. A clean group rewinds
      graph_taint_now to its start (hacks.md).
    - *Declaration memos*: the shared store's slot is a SharedWord to an immutable LazyMemo copy (no lock, no `done`
      read beside a writer: TSan B); published only if the evaluation was clean (lazy_type.c: drizzle 670 of 4977
      tainted).
    - *SymbolLinks.base_cycle*: ty_interface_in_base_cycle()'s walk is a word (interface_cycle); the bits members'
      resolution sets are atomic ORs, still written on a shared record (hacks.md: they belong in P1's members value).
    Numbers (K12, 20 runs each): drizzle, zod, effect, excalidraw, nest 20/20 K1's output; drizzle seeds 100/100;
    effect seeds 98/100 (seeds 20, 94: the TS7006 residue at fiber.ts(293,21), divergence (5); base passes those
    two, schedules shift with any code change); drizzle seeds with TTTT_FIRST_TOUCH_STAMPS=0 27/100 (TS2589s and
    the TS2416 group): the stamps are still needed, deep-nesting by schedule-ordered ids changes a worker's own
    history, which the taint rule only keeps private. Memo entries tainted: drizzle ~8k of 400k, zod 2.5-4k of 190k,
    effect 3.5k of 185k. Variance groups tainted: drizzle ~240 of 390 (~500 slots poisoned, ~165 published), zod
    ~70 of 137, effect ~55 of 418. The first non-internal event of a tainted group (drizzle, zod): a hit of a
    CACHE_DEEP relation entry 105/23, a hit of a tainted memo entry 76/39, a deep cut 36/6, a poisoned subject
    measured inside 11/10, an enclosing claim read 8/0.
    **Cost, the open question**: default path flat (callgrind drizzle +0.10%, zod +0.18%, nest +0.09%), but with the
    flag K12 work (cgtc.sh) drizzle 5.90 -> 6.44 G (K12/K1 1.106 -> 1.187), zod 4.875 -> 5.478 G (1.096 -> 1.217);
    relation pairs at K12 drizzle 257k -> 352k, zod 238k -> 394k (K1 247k, 234k), almost all of it the poisoned
    variance groups measured again by every worker (zod without relation sharing 270k -> 407k). ~0.1 G is the
    hooks' own (graph_memo_miss(), the log), trimmed since (graph_memo_note_miss()'s inline kind filter, the log
    only while memos are shared): K1 with the flag drizzle +0.59% against ai-main.
    **Decision (coordinator, 2026-10-22): option 1, the variance group rule as it is.** The options were (1) accept
    the duplicated measurements (correct by the rule; the cost is work, not waits), (2) a per-group variance lock
    (rejected: it brings waiting back, and wait cycles with it), (3) a history-independent deep-nesting order so that
    deep cuts stop tainting (rejected for now, see the follow-up below). Narrowing "results made inside a clean group,
    tainted only by its internal events, are internal for later groups" was rejected: such a result is the same only
    when computed inside that group's measurement, and another history can compute the pair outside it, so it isn't
    by construction. Gates changed with it: K12 work within 1.25x K1 on every corpus; the binding criterion is drizzle,
    effect and zod K12 wall at or below today's defaults (best-of-7, load stated; the human's bench decides).
    Stamps stay on under the flag: drizzle seeds with them off are 27/100, the taint rule only keeps a schedule-ordered
    deep-nesting decision private, the worker's own history still makes it.
    **After the decision (same session):** the variance slots are used whenever memos are shared, threads or not
    (the memo log no longer carries variances): at T1 K16 the per-entry rule withheld some members of clean groups
    and react-navigation's three projects differed (kdiff). KEY_ALIAS_MARKER became a scratch kind: it says which
    alias instances this checker's own measurements made, and shared, it told a checker that never measured the
    alias that an instance it met was a marker (react-navigation's spurious TS2322s at K16 T1; the four kdiff
    projects: react-navigation x3 now K1's, trpc/examples/nuxt still differs, not mine, see above).
    TTTT_GRAPH_RECHECK=1 on drizzle: 3 of 519 files differ in the graph at K12 (elaboration chains of the TS2416
    group: `with(...).update(...).set(...)` against `...$dynamic()`), but base (ai-main 6b2063a69) shows the same 3
    at --checkers 1 --threads 1 with the flag: a fresh checker reading the finished graph elaborates differently,
    pre-existing; at K12 base hid it because every worker took the one published measurement.
    Final numbers of the session (against ai-main 6b2063a69): default path callgrind drizzle +0.07%, zod +0.18%,
    nest +0.09% (try_resolve_indexed() pinned inline: gcc had stopped inlining it into indexed(), +0.3% on drizzle);
    K1 with the flag (cgtc.sh) drizzle +0.39%, zod +0.43%; K12/K1 work drizzle 1.197, zod 1.224, effect 1.084 (gate
    1.25); wall best-of-15 interleaved at load 15-27, K12 T12 shared against the defaults: drizzle 0.240 s against
    0.322, zod 0.204 against 0.222, effect 0.564 against 0.412 (base: 0.563, effect's known +10-35% under the flag,
    not this step's; base drizzle 0.257, zod 0.187). 20-run tables 20/20 on the five; checkerdiff K12 and threadcheck
    T12 identical; seeds drizzle 100/100, effect 99/100 (seed 18, the TS7006 residue: P2's outer-inference event
    is to be rechecked once merged). Fingerprint (2 runs zod, drizzle): (3) the PgArrayBuilder/GelArrayBuilder
    variances gone; (1) now starts at a conditional branch [185] in registries.ts (P2's word) with the memo kinds
    downstream of it; (4) KEY_UNION_PAIR of the Promise unions in query.ts still differs although clean by the
    counter: two members that print alike, most likely their order by per-worker ids (P2's order keys), not a taint.
    TSan T12 drizzle + effect (9 groups): none of A's base_cycle or B's LazyMemo.done any more; the two in graph.c
    fixed after the run (graph_memo_flush()'s debug knob initialized racily, graph_adopt() rewriting the first
    checker's graph_memo_on on a redo's fresh worker); the rest are members.c/ty.h (P1: param constraints, mapped
    templates and MappedInfo copies, sig_target, write types) and conditional.c's prop-type peek. SymbolLinks'
    interface_cycle word has no SW_FP() point: no TK_ kind for it in fingerprint.inc (a boolean of a walk over
    declarations).
    **Audit of per-worker state read by publishing computations** (relate.c, graph.c, lazy_type.c; the coordinator's
    generalization of P1's contextual-signature finding):
    - relate.c ty_is_related_within(): inference inside a relation reads the relation in progress (`my relating`:
      its maybe stack, depth, expanding): now a taint event.
    - relate.c tuples_related()'s extra index-signature comparison while `my base_constructors_in_flight` (a class's
      `extends` expression is evaluated): decided by in-flight state, now an event.
    - relate.c variances_known() answering true for this worker's own claim in flight: an event now (internal to
      the group for the running measurement's own subject), like the get_variances() reads.
    - relate.c structured_related_members()'s `marker` test reads `variance_depth` and variances_known(): marker
      references exist only inside measurements, whose relations aren't published; the claim reads are events.
    - relate.c `for_constraint_root`: part of the conditional instance's key (KEY_CONDITIONAL_INSTANCE_FOR_CONSTRAINT),
      reset for nested conditionals (checker.c): no leak found.
    - relate.c relation state (maybe stack, deep-nesting stacks, budgets, `variance_reports`, the generic relation
      numbering): the maybe stack is tsc's context-free rule; deep cuts and limits are events; the generic numbering
      is never shared; variance reports only flag measurements.
    - the private relation cache: entries made under any event are CACHE_DEEP (hit = event); clean ones are this
      worker's clean results, fine.
    - lazy_type.c: a declaration's LazyMemo state (evaluation_started: an event; pattern_retried: the retry is a
      TAINT_CYCLE event); expression memos (LAZY_TYPE_EXPR) are per checker and never shared, but a declaration
      evaluation that reads an expression's cached type computed earlier under another contextual type or flow state
      reads private state: not an event today (rule 4's flow isolation covers flow, not contextual typing). Left for
      P2/D, which own expr.c: the expression memo's `context` check decides whether it is reused.
    - graph.c: only bookkeeping of its own (the miss table, the log).
 If is_deeply_nested_as()
    ordered types by something every history agrees on (a canonical key instead of first-touch stamps or ids), a deep
    cut would stop being a taint event, and most of the variance groups' and relations' taint (the cascade above)
    would go. Risk: tsc's isDeeplyNestedType compares type ids along the stack, so id order is tsc's semantics there;
    another order changes which relations get cut and so tttt's TS2589 tail against tsgo.
    Not done: divergence (1) (zod `$replace` distributive constraint) and the trpc/examples/nuxt kdiff are not memo,
    relation, variance or declaration memo publications (they stay with every one of those kept private): an
    unconverted Ty-resident value, P1/P2's.
  - **CTX, contextual signatures as a function of the node (wt-ctx, 2026-10-22).** A function expression's contextual
    signature was a side effect of checking its enclosing call (KEY_CONTEXT_* links, per worker): a worker that needed
    its inferred return (a published type holding its function type: excalidraw's `register({ perform })` actions under
    P1's held signature-return conversion, wt-s1 5ffd63edc, 0/20) computed it context-free and published it as clean.
    Now the resolve walk records each function expression's context root (FunctionImpl.context_root: an expression
    statement's/throw's/export default's expression, a condition, a switch needle; a variable's or parameter default's
    initializer; a return expression of a named function), and ty_settle_function_context() (ty/expr.c) checks that
    root as the walk would, outermost first, when something asks for the parameters (lower.c settle_parameter_context)
    or the inferred return (function.c function_return_type's compute path) of a function nothing has checked.
    - **No resolution memo to extend**: KEY_CALL_RESOLUTION is GRAPH_SCRATCH, so a worker never adopts another's call
      resolution; it resolves the call itself. Deterministic because the root is checked from the outermost node, as
      the walk would (a call is resolved once: resolving an inner call before its outer one would differ).
    - **Diagnostics of an early settle** go where any out-of-order check's go: compute-time reports carry their own
      spans into the checker's ErrorCtx, and a call's resolution keeps its diagnostics on CallResolutionRare for the
      walk's call_typecheck() to report once. The CLI sorts by file and position and drops identical ones
      (error_ctx_sort_and_dedupe()), so a report made early is the walk's report, in the walk's place. What is reported
      at the current node is at the root (the settle enters a node there, the asker's instantiation count restored).
    - **A root in another file** (a JS assignment declaration read from elsewhere; under the graph a root of a file
      another worker checks): the same checker-wide ErrorCtx (error_ctx_remove_unchecked() drops unchecked files');
      bounded to that one root: one statement's expression, one initializer, or the enclosing function's return
      inference and one return expression, plus the enclosing functions' own roots (depth 8), never a file's statements.
    - **Flow isolation**: ty_flow_isolate() around the settle (no uncached mode, no loop in progress), no outer
      inference, inferential or argument-return depth, no return context: what a declaration's evaluation has.
    - Default path: rocket's settled_by generalized; ttttContextRootSettled (5 TS7006/TS7031 of nested callbacks,
      callbacks in initializers, returns and conditions) passes; suite otherwise unchanged, 14 corpora, checkerdiff and
      threadcheck unchanged; callgrind drizzle +0.03%, zod +0.04%, nest +0.07%.
    - Flag gates (wt-s1 5ffd63edc + CTX): rep3 20 runs excalidraw, effect, drizzle, zod 20/20; seeds effect 100/100,
      drizzle 100/100 (seed 37 passes with and without CTX on 5ffd63edc). Fingerprint 5 effect runs: 50 distinct
      differences (53 without CTX), none an inferred return or a parameter: tagStartsWith/addCases/is are variable
      declaration memos and property types of the same printed type, divergence (2)'s identity; the rest is the
      `[any, ...any[]]` tuple constraint family (the 978-difference runs) and conditional instances. On ai-main HEAD
      (with P3's clean-only declaration memos) + 5ffd63edc excalidraw is already 6/6 without CTX: P3 hides the case,
      CTX removes the mechanism.
    - Not covered (hacks.md): class field initializers, for-in/of iterables, decorators, computed keys have no root.
- **D, the deletions (one worker, last; ~1 session).** program.c, graph.c, ty.h: everything in the list above, then
  the gates. A mechanism that can't be deleted at D means a site isn't converted.

**Gates** (regression gates, not the proof):
- the default path byte-identical on every oracle and within 1% K1 instructions in total;
- TSan clean with the flag on every `-p` corpus at T4 and T12 (zero groups);
- the 20-run tables and 300 loaded drizzle runs identical to K1, plus 100 loaded effect runs (the TS7006 residue);
- the fingerprint-at-publish oracle silent;
- both full oracle runs identical with and without the flag;
- K12 work within 1.15x K1;
- drizzle and effect K12 wall at or below the defaults, with the load stated;
- the recheck tool (every file checked again by a fresh checker after the join) agreeing per file.

**Rejected: B, immutable types with per-worker lazy tables published at quiescent points.** (1) The hot path: a
per-worker table probe for every lazy read costs 4-6x the hybrid's measured overhead, two kinds already +2-3% on
drizzle and zod, and needs Props given ids or hash probes. (2) Identity: per-file publication lets a worker use its
own objects (declared Props, Signatures) for a whole file before learning it lost. By then they have escaped into its
published values' back-pointers (instantiate_members' `target`, builder_add_inherited), and the private/protected
checks compare root props across types (relate.c modifiers_related(), check.c synthetic_access(), checker.c
has_conflicting_private(): the census's 4R sites). Repairing that needs a transitive drop of everything that
references a loser at each publication, or bun's discard-and-recheck of the file. (3) What B gets right, the
hybrid keeps: no in-flight state is ever visible to another worker. Also rejected: **A as plan 89 wrote it** (claimed
and waited fields, tainted values reset to not-started). Waiting makes the wait cycles the redo exists for, and a
breaker's history is no sequential checker's. Resetting a tainted word lets another worker publish a clean value
that the tainter, which keeps its own, then disagrees with on every later read.

## Validation of the whole plan
The 12-core table in measurements.md re-run by the human after each stage (`scripts/bench.sh --runs 3 --bun PATH
--tscrs PATH`, the CPU column included), per-file sums at K1 / K1-with-spinning-cores / K12 on vscode and drizzle
after stages 2-4, every oracle through `scripts/oracles.py`, and vision.md's "Perf and memory" bullet rewritten with
the geometric means. The plan is done when vscode at the defaults is under ~1.2 s on that box with the output
unchanged, or when a stage's gate says the rest can't pay and this file says why.
