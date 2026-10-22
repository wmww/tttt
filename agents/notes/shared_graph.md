# The shared type graph (parked)
`--shared-graph` / `TTTT_SHARED_GRAPH=1`, off by default: one type graph shared by every checker on real threads
(`src/ty/graph.{h,c}`, `overlay.{h,c}`, `shared_table.c`, `taint.h`; documentation.md "--shared-graph"; the oracles in
`scripts/sharedgraph/README.md`). Built 2026-10-20..22, **parked 2026-10-21/22**: it is correct enough to measure and
too expensive per instruction to win. This note keeps what was learned; the full design log (2240 lines, every stage,
the census of lazily written fields, bun's design read from its source) is git history:
`git show af936072c:agents/plans/86_multiples.md` and `git show af936072c:agents/plans/86_stage4_census.md`.

## Why it was built
At the defaults on 12 cores each checker re-derives the instantiations of the project's own generics (vscode ~1.2x
duplicated work; drizzle 1.4x serialized, 4x per-file time at K12), and recycling adds 5-8%. A shared declared layer
(lib and `.d.ts` declarations only) was measured first and rejected: those are 2-6% of the re-derived work
(measurements.md "What the checkers re-derive"). Only a graph holding instantiations could remove it, and it was also
the one lever on memory at the defaults. bun canary (oven-sh/bun#45800) shares its whole graph and keeps ~20 of 24
threads busy at 2.2 GB on vscode: the existence proof that sharing can pay.

## The design that was built ("by construction", 2026-10-22)
- **Write-once shared words.** Every lazily computed value reachable from a shared object (members, raw members, prop
  types, signature resolution and return, predicates, constraints, defaults, mapped templates, conditional branches,
  declared types, reference args, inferred returns, declaration memos, variance slots: 18 sites plus memos) is one
  `SharedWord`: null -> clean value | NONE | POISON, by CAS with release, read with acquire. Nothing else on a shared
  object is written after publication.
- **In-flight state lives only in the computing worker** (a ClaimSet keyed by the word's address, a private Overlay
  for unpublished values). No worker reads, waits for or learns of another's in-flight state, so no wait cycle and no
  redo exist.
- At a computation's end: clean -> CAS the value (a loser adopts the winner's, its own objects being unreferenced);
  tainted -> CAS POISON and keep the value privately. A POISONed key is computed privately by each worker that needs
  it (the default path's semantics). Intern-table memo kinds keep a private-first table, so a tainted entry just
  stays private.
- **"Clean"**, precisely (one per-worker counter, frames compare it): read nothing in flight to this worker (own
  cycle, early members, an enclosing variance claim, TY_CIRCULAR); read no private value (every Overlay hit is an
  event); moved no limit counter and met no deeply-nested cut or depth limit in a relation; reported nothing at the
  current node; `flow_uncached == 0` for expression kinds; ran in the typecheck phase. A done-ness peek that selects
  between results counts as a read.
- **Why deterministic**: a clean value is a function of published clean values and hash-consed identities, so any
  worker computing that key cleanly computes the same value. A worker's view is a history a sequential checker could
  have had (the class recycling, stealing and K already produce).
- **Shadowed words**: a worker whose tainted value lost to another's clean one keeps reading its own (Overlay first)
  until its file ends, then hands over to a fresh checker on the same graph (`graph_fresh_worker()`).
- **Variances**: a group (the subjects of one outermost measurement) is one computation; reads of its own claims are
  internal; equal results adopt, differing ones shadow; a tainted group publishes POISON.
- **Rejected**: (A) claimed-and-waited fields with tainted values reset: waiting creates the wait cycles a redo must
  break, and a breaker's history is no sequential checker's; this was built (4d/4e: owner-tagged states, a wait-for
  graph, a per-file redo, a global variance lock costing drizzle +54% wall) and deleted. (B) immutable types with
  per-worker lazy tables published at quiescent points: a table probe on every lazy read costs 4-6x the words' fast
  path (+2-3% on drizzle and zod for two kinds alone), and per-file publication lets a worker's own objects escape
  into back-pointers (instantiate_members' `target`, the private/protected root-prop comparisons) before it learns
  it lost.
- Earlier gates that still describe the code: union order's type-id fallback decides 0.003-0.6% of ties and is not
  observable under seeded permutation once spread alternatives got a structural key (`TTTT_ORDER_STATS`,
  `TTTT_ORDER_SEED`); is_deeply_nested_as()'s "newer id" test needs first-touch stamps under the flag; the taint
  prototype (G0) found members and prop types 99.7-100% clean, intern memo kinds tainted 0.01-0.14%.

## Where it ended (2026-10-22)
Numbers: measurements.md "Shared graph end state", "Shared graph K1 overhead", "Shared graph gate G2", "Oracle totals
with and without the shared graph".
- Default path: unchanged output, no measurable instruction cost (state bytes beside each word, < 0.1%).
- With the flag at K12: 20/20 run tables on drizzle, zod, effect, excalidraw, nest; 300 loaded drizzle runs identical;
  effect ~1% of loaded runs differ; TSan clean on 14 corpora; kdiff 8 / 3814 against plain's 3; K1 cost +4%
  instructions; work K12/K1 drizzle 1.305, zod 1.285, effect 1.134.
- **Open residues** (the release criterion wanted them closed and plain/flagged oracles identical): **C**, conditional
  and alias instances published as identities whatever their taint while carrying resolved state (mikro-orm x3, the
  drizzle seeds' TS2322 at singlestore-core/query-builders/query.ts(139,3)); proposed fix: the instance object an
  identity published at creation, its resolution a word published only when clean. **V**, variance groups' side state
  (results computed at variance_depth > 0, marker instances) an adopter lacks; no project shows it now. **W**,
  trpc/examples/nuxt and folo's renderer (mechanism open). Effect's Schema.tst.ts(2936,39) TS2345 cascade (~1% loaded,
  0-15% at load 40+, bimodal fingerprint, the C+V family). Each converted site had exposed a class of per-worker state
  the taint counter couldn't see; the fix each time was tsc's shape (derive from the node, not from history).
- The laptop bench (measurements.md "The 12-core box at 5f2cd699"): no wall win at the defaults; vue 2x slower
  (contention, K1 unchanged), type-fest, effect, valibot slower; drizzle and excalidraw faster; memory vscode 1581 ->
  2924 MB at K12.

## The perf accounting (2026-10-21, 16-vCPU box, callgrind; why it's parked)
Work in G instructions (typecheck and worker threads):

| corpus | plain K1 | flag K1 | plain K12 | flag K12 T1 | flag K12 T12 | flag K12 / flag K1 |
|---|---|---|---|---|---|---|
| drizzle | 4.88 | 5.15 | 10.54 | 6.58 | 6.64 | 1.29 |
| zod | 4.13 | 4.36 | 6.34 | 5.57 | 5.64 | 1.29 |
| effect | 7.86 | 8.20 | 9.02 | 9.44 | 9.32 | 1.14 |
| vscode | - | 77.36 | - | - | 84.38 | 1.09 |

The flag K12 excess over flag K1, by bucket:
- (a) recomputation after POISON: words 0.7-3% of CPU; **variance groups** 60% of drizzle's excess (1.81 -> 2.68 G
  under measurements; 266 of 397 groups tainted, each worker re-measures), 48% of zod's, 5% of effect's. The first
  taint event of a tainted group is mostly a deep-nesting cut (relation_deep_cut(), or a CACHE_DEEP or tainted memo
  hit that came from one). By tsc's own semantics the cut depends on type-id order, i.e. the worker's history: the
  taint is correct, not conservative.
- (b) races: under 1% of instructions (T1 vs T12); adopted words' cycles drizzle 4%, zod 2.3%, the rest under 1%,
  three quarters in the first ~60 ms. The start-of-run lockstep is a dokploy-shape problem (one huge central type).
- (c) relations outside variance measurement: small. The shared relation cache doesn't pay as built
  (`TTTT_GRAPH_SHARE_RELATIONS=0`: zod 5.617 vs 5.636 G, CPU -2..-5%), although sharing definitive results is tsc's
  own semantics (a probe found 0 disagreements in ~186k duplicate puts; it would take 7-11% of K12 relation pairs,
  40% on excalidraw).
- (d) the sharing machinery itself: 43-61% of the excess (drizzle 0.72 G, zod 0.52, effect 0.75, vscode 4.27):
  ~150-300 instructions per shared probe after a private miss, per identity publication (a mutex insert), per memo
  flush.
- (e) zeroed fresh tables (memset) 0.11-0.69 G; private intern work.

**Per instruction** (best per-file check-time sum / instructions, ns): drizzle K1 0.141 plain / 0.139 flag, K12 0.193
plain / **0.314 flag**; zod 0.161 / 0.153, 0.239 / **0.381**; effect 0.163 / 0.155, 0.264 / **0.325**. A flag
instruction at K12 costs 1.2-1.6x a plain K12 one (2.0-2.4x a K1 one; plain K12 is 1.4-1.6x from all-core clocks).
At K4 the premium is 1.19x on separate cores, 1.14x on SMT siblings. Cachegrind gives the flag *fewer* LL misses than
plain (drizzle 17.1 M vs 24.2 M) and context switches are ~1000 a run, so it is neither working set nor sleeping:
inferred to be lines written by one core and read by others (shared tables, published objects, CAS'd words). Not
measured: this VM has no performance counters. Effect's critical file Schema.ts: 469 M instructions plain, 549 M flag,
117 ms plain vs 162-184 ms flag at K12. Net: drizzle's flag per-file sum equals plain's on 37% fewer instructions;
zod and effect lose.

**Verdict**: the design shares well (races < 1-4%, recomputation a few percent but variances); it doesn't make
sharing cheap. Even a perfect 1.0x work ratio gives drizzle only -20% per-file sum and effect a loss. The flag wins
only where plain recycling thrashes: dokploy and monkeytype run 2.2-2.5x faster (measurements.md "The thrashers with
the flag").

## Before reopening it
1. **Hardware counters on the laptop** (the human's 12-core box has them): `perf stat -e
   cycles,instructions,cache-misses,LLC-load-misses,...` and `perf c2c record` on drizzle K12, flag against plain.
   Candidates if it is cross-core traffic: publication writes into lines other cores read (words inside
   TyObjectState and Signatures beside hot read-only fields; shared-table slots written beside probed ones), the
   private-miss shared probe on every first use, the per-chunk type directory. Layout changes are low risk.
2. **A design for variance groups cut by deep nesting** (48-60% of drizzle's and zod's excess): graph-wide stamps
   (the first toucher's order: nondeterministic, 4676 of 1.36 M dokploy decisions would change; high risk), or
   measuring a tainted group once and publishing it keyed by the stamps of the types it cut on (medium-high risk).
   Without them drizzle and zod K12 work would be ~1.12x.
3. **Cheaper machinery** (halving (d) takes drizzle/zod 1.29x -> ~1.2x): cache a shared identity in the private
   table on first hit, lock-free insert-if-absent for identities, batched memo flushes; drop or gate the shared
   relation cache. Smaller flag-only items left: a single look-and-claim call at the members.c sites (~2% of the
   flag's K1 cost), retiring shadowed workers at once (-0.2..-0.6%), a narrower outer-inference taint event
   (function_return_type()'s keep_outer, hacks.md), object literals' and JS function expressions' identity keyed
   syntactically.
4. The residues C, V, W, and both full oracle runs identical plain and flagged, before any default flip.
5. Policy ideas that were never built: **adaptive** (plain by default, the flag when plain thrashes: a checker
   recycling after fewer than CHECKER_MEMORY_BACKOFF_FILES files repeatedly, or a first segment's warm-up over its
   budget share; switching is a recycle into a fresh shared graph at a file boundary); the graph budget (800 MB, at
   dokploy's working set): don't recycle when under a quarter of the files remain, or double after a short epoch.

## What bun does differently (read from its source at 6bb5dc19, 2026-10-20)
One content-keyed hash-consed store for every type, signature and mapper, created into directly with global ids from
per-thread id ranges (a LOCAL bit for records that must not escape); every tsgo "link" in shared tables behind a
task-local buffer, committed in numbered commits under one mutex, first entry per key wins; union order and
isDeeplyNestedType's id test replaced by a content order (costing tsgo parity in edge cases, accepted); determinism
from a large body of program-order rules, discard-and-recheck of files and a serial last round (where its known bugs
are). Checkers are recycled after 1/128 of the total cost. Its CPU at default threads is 1.6-3.0x its single-thread
time (vscode 43 CPU s vs 14 single): sharing costs it too, it just has more cores busy.

## Tools left behind
`TTTT_GRAPH_STATS=1`, `TTTT_GRAPH_WORK=1` (cycles per word computation by outcome), `TTTT_GRAPH_LEAD=1` /
`TTTT_GRAPH_NO_LEAD=1`, `TTTT_GRAPH_SHARE_RELATIONS=0`, `TTTT_GRAPH_BUDGET_MB` (graph-wide recycle, default 800),
`TTTT_DEBUG_PUBLISH` (debug build: a publication referencing a private range aborts), `scripts/sharedgraph/` (20-run
tables, seeds, fingerprint-at-publish, TSan, the recheck), `scripts/taintstats.sh`, `scripts/kdiff.py` with the flag.
