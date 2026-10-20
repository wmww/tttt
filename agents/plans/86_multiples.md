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
Sessions: **design** (one), **prototype + gate** (one or two), then a build-out window if go. If the gate says no,
the fallback is stage 3 (front end) plus perf_leads.md's snapshot recycle, which goes after the thrashers (supabase
studio: `--checker-memory off` takes 2.27 -> 1.30 s). Stage 3's cheapest piece, starting the costliest file first
(tsc and type-fest are bound by their biggest file), doesn't depend on stage 4 and can go in any perf slot.

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

## Stage 1: ship PGO + LTO (one session) — done 2026-10-19
Shipped as `scripts/pgobuild.sh` (`pgo/tttt`, profile regenerated from the fixed training set below whenever a
source, the compiler or the script changes, ~3 min on 4 cores; `release/` stays plain -O3 for the edit-test loop).
`bench.sh`, `instrcount.sh` and `perfsweep.py` measure it. Callgrind: -11.5% (nest) to -16.9% (excalidraw), the
untrained tsc -14.1% and vscode -15.2%; output byte-identical on every corpus and DT 8595 / 8595. The human's 12-core
rerun is still owed. The original notes:
Measured 2026-10-19 in a scratch build: **-8-9% wall on every corpus, trained or not** (vscode K1 11.83 -> 10.86 s,
defaults 2.68 -> 2.48; tsc 0.51 -> 0.47; drizzle 0.85 -> 0.78; zod 0.64 -> 0.58). LTO alone 1-2%.
- Recipe that worked: `meson setup B --buildtype release -Db_lto=true -Db_pgo=generate`, run `tttt -p` on drizzle,
  zod, effect, excalidraw, webpack at `--threads 1 --checkers 1` and at the defaults, `meson configure B
  -Db_pgo=use -Dc_args=-Wno-missing-profile`. No `-Wno-error` needed any more: the LTO link's
  `-Werror=stringop-overflow` "writing 128 bytes into a region of size 0" in ty_buf_push() (inlined into
  checker.c's intersection(), the cross-product loop) was GCC's: with PGO it lost the invariant that `cap` is the inline
  array's length when `data` is it (no TyBuf is ever copied by value), so `cap *= 2` could be a malloc(0). Fixed by
  sizing that malloc from the inline array (2026-10-19, instructions unchanged); a PGO+LTO build of the training
  corpora zod and drizzle is warning-free and prints the same as release. A stale profile is
  `-Werror=coverage-mismatch` per object, not a warning: the build script must retrain (or pass
  `-Wno-error=coverage-mismatch`) after any source change, `ty.h` touches every object.
- Decide how the profile ships: regenerated by the build script from a fixed training set (profiles must match the
  compiler; sccache caches objects by flags, fine), or a checked-in `.gcda` set per compiler version. The training
  set must not be the measure: keep vscode and tsc out of it and report them as the untrained check.
- `scripts/` compile the release build; whatever they do must still abort on compile errors and must not slow the
  edit-test loop (train once, reuse; `-Db_pgo=use` with a stale profile is a `coverage-mismatch` error under `-Werror`, see above).
- Validation: every oracle unchanged (PGO changes no semantics; keep `-Werror` at link, a new LTO warning is to be
  read, not silenced); `scripts/bench.sh` and `scripts/instrcount.sh`
  re-saved as the new baseline (instruction counts change with inlining decisions); measurements.md's 12-core
  table re-run by the human.

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

## Stage 4: a shared concurrent type graph (its own window, 10-20 sessions, with a gate)
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
    by id only for a binary search, which can't be observed. Other `->id` uses are hash keys.
- **The design session also answers** (insights 2026-10-20): what bun #45800's "shared results in small commits"
  shares between its workers, and how it keeps the output deterministic (read the PR; bun's output isn't tsc's,
  so its rules aren't ours, but its mechanism is a data point). It also lists every lazily written field on
  `Ty` and in `Checker` with whether it is per-type (it must publish once) or per-query (it stays scratch); this
  census is what the 10-20 session estimate rests on, so re-estimate from it. And it decides the memory story:
  no recycling with a shared graph (K1 holds vscode in 1.4 GB) against a shared graph plus per-worker scratch
  recycling.
- Then a design session pricing: the table (lock-free open addressing, resize), the publish-once fields, what
  `Checker.links` becomes (per-symbol state shared, per-expression state not), how diagnostics stay K-independent
  (they do today by sorting; "whoever asks first" slots for inferred return types match tsc and tsgo's own
  K-dependence), and a prototype on drizzle at K12 against today's 1.4x duplication, measured by per-file sums.
- Go/no-go for the build-out: drizzle's per-file sum at K12 within 1.15x of K1's, K1 within 3% wall, kdiff and
  checkerdiff byte-identical, peak at the defaults no higher than K1's today.

## Validation of the whole plan
The 12-core table in measurements.md re-run by the human after each stage (`scripts/bench.sh --runs 3 --bun PATH
--tscrs PATH`, the CPU column included), per-file sums at K1 / K1-with-spinning-cores / K12 on vscode and drizzle
after stages 2-4, every oracle through `scripts/oracles.py`, and vision.md's "Perf and memory" bullet rewritten with
the geometric means. The plan is done when vscode at the defaults is under ~1.2 s on that box with the output
unchanged, or when a stage's gate says the rest can't pay and this file says why.
