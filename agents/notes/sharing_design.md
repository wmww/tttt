# Sharing the type graph the way bun does: is it viable for tttt? (worker sg2, 2026-10-22)

**Closed (2026-10-23)**: the laptop A/B (measurements.md "The 12-core box at 48cd9835") answered the recommendation
below with a no: the best layout wins only on drizzle (-14% wall) and loses on zod (+2%), effect (+11%) and vue
(+25%), the IPC gap to plain is 4-11%, and content order moves 3 drizzle elaboration chains away from tsc's text.
Stages 1-3 are not built (vision.md "Decisions"). The rest of this note is the record.

The question: is bun's sharing model (notes/bun_concurrency.md) incompatible with something tttt wants, or is it
just work? Short answer: it's work, and less of it than the parked flag's history suggests. bun's one design choice
tttt lacked, an **id-free deep-nesting order**, costs tttt ~2 suite tests and ~20-35 real-code diagnostics. Turned on
under `--shared-graph`, it makes the flag's output deterministic in every run measured (75 of 75 on drizzle, zod,
excalidraw, effect, nest, outside the known effect residue). It doesn't make variance measurement *by itself*
shareable at drizzle's scale: what still taints drizzle's measurements is cycles, members in flight and relation-cache
hits concluded under cuts. Those are bun's "order-dependent notes" too, and bun handles them by rollback.
Recommendation at the end: **go for stages 1 and 2 behind the laptop's confirmation; stage 3 is no-go for now.**

All numbers are from this box (16 vCPUs shared with other agents, no performance counters), callgrind for
instructions, per-file thread CPU (`TTTT_ROLLBACK_STATS` `RB` lines) for time. The tools are env-gated in the tree
(commits 2bb219ffc, b5419fe7a on wt-sg2): `TTTT_ORDER_SEED` (now also the conditional-branch order and unique symbols'
names), `TTTT_NESTING_ORDER=id|count|seed|content`, `TTTT_DEEP_HIT_NO_TAINT`, `TTTT_ROLLBACK_STATS`, a
`-DTTTT_TAINT_SITES` build (every taint event's site in the census' VC lines), `TTTT_GRAPH_PAD`, `TTTT_GRAPH_BATCH`,
`-DTTTT_SHARD_PACKED` (the old shard layout).

## 1. Where type-id order is observable (the audit)
Every decision that compares type ids or creation order, with what bun replaced it with and what permuting it costs
(seeds 1-3; suite dumps of 14,029 tests, the 14 `-p` corpora at K1, DefinitelyTyped's 68,640 `$ExpectType`):

| site | decides | bun's replacement | observable when permuted / replaced |
|---|---|---|---|
| order.c `cmp_ids()`: ty_compare's final fallback, string twins, conditional roots without a node, instantiation-expression symbols | union member order | content order (height, last syntax, content hash) | 1 suite test (dynamicImportsDeclaration: `import()`'s `{ default: ... }` objects have no node to order by; a content order keyed by the import expression would fix it); corpora 0; expecttypes 0 |
| relate.c conditional-constraint branches "in id order" (raw ids, now ty_compare_ids()) | which branch is related first | content order | 0 |
| checker.c unique symbols' escaped name `__@name@<type id>` | undeclared properties' order among equal names | content | 0 |
| relate.c `is_deeply_nested_as()` "newer id" (relations, inference's expanding test) | deep-nesting cuts | height, then push order on the stacks (relate.rs `is_at_least_as_new`) | ids permuted: 7 tests, drizzle 39k lines; push order alone: 7 tests, 71 lines; **bun's height + push order (`TTTT_NESTING_ORDER=content`): see below** |
| not observable | | | flow.c key_entry_cmp (groups equal literals), id-keyed caches and intern keys, prop_cmp_name (atom address, lookups), rehash loops, first-touch stamps (equal to creation order at K1) |

**The parity cost of going id-free like bun** is the content order's, everything else being unobservable:
- suite: 2 of 13,993 passing tests lost: `inferFromNestedSameShapeTuple` (inference's expanding test: the inner
  tuple's object is lower than the outer by height, newer by id) and tttt's own `ttttDeferredReferenceNormalizedInRelation`;
- corpora at K1: drizzle 3 TS2416 elaborations one return-type level deeper (mysql2/session.ts(281,17),
  planetscale-serverless/session.ts(198,11), singlestore/session.ts(277,17); 12 lines), webpack one spurious TS2345
  (tooling/generate-types.js(397,35));
- flagflip: 10 missing + 10 spurious, 5 valibot sites in the sloppy and nosnc cells (TS2554 reported as TS2344:
  nullableAsync.test-d.ts:35-36, nullishAsync.test-d.ts:30/48, optionalAsync.test-d.ts:32, undefinedableAsync.test-d.ts:38,
  getDefault.test-d.ts:79);
- source sweep (the fixed quarter, 1,303 projects): 0; expecttypes: 0; type counts within 0.2%.
- The prototype differs from bun in one way that may matter for the residue: it takes the first push on *this* stack;
  bun takes the first on either stack (a level's target before its source). Full sweep and DT sweep not run.

## 2. What the content order does to variances (1b)
Variance census at K12, one thread, no flag (`scripts/variancecensus.py`'s split, values compared with K1's too):

| | drizzle id -> content | zod | effect | excalidraw |
|---|---|---|---|---|
| subjects whose values differ between checkers or from K1 | ~30 -> **1** | 0 -> 0 | 0 -> 0 | 0 -> 0 |
| (b) clean in some checkers, tainted in others | 7 -> 22 | 1 -> 2 | 1 -> 1 | 0 -> 0 |
| outermost measurements' types tainted | 97% -> 84% | 87% -> 76% | 15% -> 11% | 10% -> 8% |

The content order makes variance *values* pure functions of the symbol in practice. Under ids, drizzle's Pg/Gel
builders come out `09` (unreliable) after deep cuts in one checker and `01` in K1. Under content the single exception
is drizzle's `MakeColumnConfig` (`010901` in one checker): it was tainted by a relation-cache hit (CACHE_DEEP) that an
earlier deep cut concluded. It does not make them *clean*: what taints the outermost measurements under content, by
share of their types (`-DTTTT_TAINT_SITES`, the first non-internal event):

| | drizzle | zod | excalidraw |
|---|---|---|---|
| clean | 15.8% | 23.6% | 92% |
| only reads of its own group's claims (nested measurements reading the outer claim) | 2.4% | 67.3% | 1% |
| only CACHE_DEEP hits | 32.2% | 9.1% | 6.9% |
| group reads + CACHE_DEEP hits | 16.7% | 0 | 0 |
| other: cycles (lazy_type.c), members/signatures in flight | 33% | 0 | 0 |

So a shared variance table with bun's claim/wait rule, keyed by the outermost subject, sharing a group when its only
events are its own claims, covers zod's measurements (91%) and excalidraw's (93%). That rule is K-independent by
construction, because the outermost subject's group is a function of the subject. On drizzle it covers 18%, or 67% if
CACHE_DEEP hits are accepted the way bun appears to (bun shares unless a diagnostic or an order-dependent variance was
used). Accepting them is exactly where MakeColumnConfig breaks: one known disagreement in 667 subjects. **The "87% of
drizzle's excess with no rollback" target is not reached by the order alone.** The remaining third (cycles, in-flight
members) is tsc's own order dependence, which bun rolls back.

Under the flag (shared graph, not one checker per history), entry points move. drizzle's `View` family is measured
nested inside another measurement and comes out `09` against K1's `01`, with no cuts at all: the "enclosing claim"
order dependence of tsc's semantics, independent of ids.

## 3. Rollback rate, bun-style (deliverable 2)
`TTTT_ROLLBACK_STATS=1` under `TTTT_SHARED_GRAPH=1` at K12. A file is discarded when it closes a cycle key that an
earlier file in program order also closes, hits a limit (instantiation depth or count, relation complexity or depth),
or measured an outermost variance tainted to a value that differs from K1's. Cost is weighted by the file's K1 check CPU:

| corpus | files | limit hits | cycle discards | variance contradictions | recheck share of K1 work |
|---|---|---|---|---|---|
| drizzle (stamps / content) | 519 | 0 | 4 / 3 files | 27 / 21 files | **8.1% / 10.3%** |
| zod | 334 | 0 | 0 | 0 | **0** |
| excalidraw | 650 | 0 | 0 | 0 | **0** |
| effect | 958 | 0 | 0 | 4-5 files | **2.9-3.6%** |
| vscode | 9,929 | 0 | 0 | 0 | **0** |

Cycles and limits barely matter: at most 8 files close a cycle, no corpus hits a limit. The whole cost is variance
contradictions from entry-point-dependent nested measurements (drizzle's View/ViewBuilder/PreparedQuery/
RelationalQueryBuilder family). Approximations: the trusted closer of a cycle is the earliest file in program order
among this run's closers, and bun's "only importers can reach it" exception isn't modelled.

## 4. Layout and batching (deliverable 3)
What bun does that the parked flag doesn't, checked against the flag's code:
- **Per-writer regions for published objects: already true.** Types and computed values are allocated in the
  computing worker's own arena, ids come in per-checker blocks, and the id directory's chunks are one checker's.
- What is written where others read hot: the shared tables (drizzle K12 inserts ~545k type identities, ~300k links,
  ~250k memo entries, ~140k relation results), 916k SharedWord CAS publications into other workers' objects, the
  variance slots.

Changes, measured as check CPU summed over files (median of 12, flag + content order, this box loaded 15-23):

| | drizzle | zod | effect |
|---|---|---|---|
| plain K12 | 1.99 s | 1.33 | 2.06 |
| today's flag (stamps, packed shards) | 1.85 | 1.53 | 2.52 |
| flag + content order, packed shards | 1.88 | 1.45 | 2.40 |
| **(a') + shard lines split** (slots pointer apart from lock and length) | **1.77** | **1.38** | **2.28** |
| (a') + every slot its own line (`TTTT_GRAPH_PAD=1`) | 1.94 | 1.54 | 2.48 |
| (a') + (b') memo entries and relation results batched to file end, by shard (`TTTT_GRAPH_BATCH=1`) | 1.91 | 1.46 | 2.37 |

- **(a') works**: -5% / -4% / -4%. Every lookup read the slots pointer from the line every insert writes (the shard
  mutex). It is now the flag's layout (b5419fe7a).
- **Padding loses** (4x table memory, more misses).
- **(b') batching loses**: +4..+11% instructions, because results published later are computed by more workers. The
  part that could be batched safely is plain values; batching SharedWords is the design (B) rejected in
  notes/shared_graph.md, because private objects escape before the worker learns it lost.
- The content order's first prototype kept heights in a per-thread table and cost drizzle +27% check CPU under the
  flag. Heights now live in the Ty (16 bits taken from `kind`), and the flag with content order is cheaper than with
  stamps (fewer poisoned words: 25.4k -> 16.6k; tainted variance groups 272 -> 242).
- **Per-instruction premium** of flag K12 over plain K12 (check CPU / callgrind instructions): drizzle 1.38x today ->
  1.29x with content + (a'); zod 1.26x -> 1.14x; effect 1.16x -> 1.06x. Total check CPU against plain K12: drizzle
  -11%, zod +4%, effect +10% (today's flag: -7%, +15%, +22%). Cachegrind can't see coherence: write misses
  (drizzle D1mw 25 M flag vs 39 M plain) don't move with (a'). The laptop must confirm with real counters.
- **Determinism** (20 runs each, K12 T12, against K1 of the same order; load 14-28):

| | drizzle | zod | effect |
|---|---|---|---|
| plain K12 | 20/20 | 20/20 | 20/20 |
| today's flag (stamps) | **12/20** (residue C: 33 lines, the MySqlQueryResultKind/TReturning chain) | 20/20 | 20/20 |
| flag + content | 20/20 | 20/20 | 20/20 |
| flag + content + (a'), two rounds | 40/40 | 40/40 | 39/40 (Schema.tst.ts(2936,39), the known effect cascade) |
| today's flag + (a') (stamps) | 13/20 | 20/20 | 19/20 |
| flag + content + (a') + (b') | 20/20 | 20/20 | 20/20 |
| flag + content + padding | 20/20 | 20/20 | 20/20 |

  Earlier: flag + content 15/15 on drizzle, zod, excalidraw, effect, nest. TSan (T4 and T12, 5 corpora, flag +
  content): one group, `ty_reduced_slow()`'s KEY_REDUCED_UNION slot write against a reader (excalidraw T4; not the
  content order's; triage.md).
- **Today's flag regressed** from the parked note's 300/300 drizzle runs to 12/20. Bisected (25 runs per step):
  5f2cd6996 20/20, the first bad commit d42a3c0e0 "hop_whitespace_slow(): out of line". A parser change can't touch
  the checker's semantics: it moves timing, and the residue C race was latent. Under content order C doesn't appear,
  which says it was a stamp/id-order history effect.

## 5. The staged path

### Stage 1: content order by default (no sharing)
`TTTT_NESTING_ORDER=content` becomes the default deep-nesting test: heights in the Ty (computed at creation would be
cleaner than the lazy store), push numbers on both relation stacks as bun has them, inference's stacks too. The parity
exceptions above are recorded as known-failing: the 2 suite tests, drizzle's 3 TS2416 chains, webpack's TS2345,
valibot's 5 flagflip sites. Run first: full sweep, DefinitelyTyped sweep, full source sweep.
- Gain alone: none at K1 (+1.6% drizzle instructions in the prototype: its push search is quadratic; bun-style push
  numbers make it ~0). It removes the one id-dependent input to memoized relation and variance results, the
  precondition for stages 2 and 3, and makes plain K12 variance values agree across checkers (drizzle ~30 -> 1 subject).
- Risk: low-medium (a parity regression class: a deeper or shallower cut somewhere in real code; the oracles above
  bound it). Size: 1 worker-session plus the sweeps.

### Stage 2: the flag on by default for K > 1, the plain path at K = 1
Publishing clean values only, as today, with the content order instead of stamps and the (a') layout.
- **K1**: the flag's K1 cost is +4% instructions (measurements.md "Shared graph K1 overhead": drizzle +4.06%, zod
  +4.25%, nest +0.71%). A K1 bypass is trivial: `graph_new()` only when the checker count is > 1 (program.c, the
  `my shared_graph && !my graph` test). With the content order there are no stamps to keep consistent, so K1's plain
  path decides deep nesting the same way the flag does.
- Expected gain (this box's check CPU, flag + content + (a') against plain K12): drizzle -11%, excalidraw a win (the
  laptop: 0.22 -> 0.20 s wall with today's flag), zod +4%, effect +10%, vscode tie. The laptop's today's-flag numbers
  are drizzle -26% instructions / -9% cycles / wall 0.32 -> 0.28, vue 2x slower (contention, uninvestigated), type-fest
  and effect slower. With the premium down from 1.38x to 1.29x on drizzle, and from 1.16x to 1.06x on effect, the
  expectation is drizzle -15..-20% cycles, effect about even, zod even. **vue must be understood first**: it is the
  case where the flag loses 2x for no extra work.
- Determinism with content: 160 of 161 runs on drizzle/zod/effect plus 15/15 on five corpora; the one miss is effect's known residue.
  Before a default flip: the effect cascade (C+V family) closed, seeded schedules (`scripts/sharedgraph/seeds.sh`) and
  300-run loops with content, the full oracles flagged and plain identical, the KEY_REDUCED_UNION race fixed.
- Ceiling for this stage (notes/duplication.md, notes/shared_graph.md): the flag removes ~25% of drizzle's K12
  instructions today. The rest of drizzle's excess is tainted variance groups (60%), which stage 2 doesn't share.
- Risk: medium (the residues' history: each converted site exposed a class of per-worker state). Size: 2-3
  worker-sessions (vue, the effect residue, the race, the gates).

### Stage 3: bun-style rollback to share the tainted outermost measurements
Publish an outermost variance group whatever its taint. Record every consumer file, compare at file end with the
first-published value, and on contradiction discard the file's diagnostics and recheck it in a fresh worker (the flag
already hands a shadowed worker over to a fresh one at a file boundary: graph_fresh_worker()).
- Price: the recheck rates of section 3, drizzle 8-10% of its work, effect 3-4%, zod/excalidraw/vscode 0. Plus
  consumer tracking: a value read by another worker's computation that published something itself must taint that
  too, or the rollback cascades.
- Gain: drizzle's tainted groups are 84% of its outermost measurement types under content (60% of the flag's K12
  excess); zod's 76%, mostly group-internal reads that a group rule shares without rollback. Net on drizzle K12 work:
  1.30x -> ~1.12x of K1 minus the 8-10% rechecked, i.e. ~1.2x. On zod the group rule alone gets most of it.
- Risk: high (it's bun's commit protocol on top of an object graph whose results live in the objects: tttt's
  results aren't in side tables a commit could withhold). Size: 4-8 worker-sessions. **Do zod's part (the group rule,
  no rollback) inside stage 2 instead.**

### Not recommended: warm prefix + fork()
A serial warm-up (libs, then the top generic hubs by fan-in) followed by fork() of K children with copy-on-write
graphs. No locks, no publication, today's determinism. What it can cover: the types every K12 checker makes are 6%
(drizzle), 14% (effect), 19% (excalidraw), 11% (vscode) of a checker's types, but only 3-5% of its instructions
(excalidraw 10-13%); lib instantiations are a minor share everywhere (notes/duplication.md). drizzle's duplicated work
is the dialects' variance closures, each entered by a different checker. A prefix that measured them all would do
~1.6 G instructions serially, a third of drizzle's K1 work, while drizzle's whole K12 check is 0.29 s wall on the
laptop. Amdahl: a prefix of the 3-5% common work is ~0.02-0.03 s serial on drizzle, saving at most ~4% of each
child's work. On vscode it's ~0.3-0.5 s of lib checking against a 2 s run, saving ≤ 8-12% per checker: net about
zero. COW faults on the warm graph's lazily written fields would cost on top. Not worth a process model change.

## Recommendation
- **Go for stage 1** (a session, low risk, the precondition). Its parity cost is listed per file.
- **Stage 2 goes once the laptop shows**, on drizzle, zod, effect and vue at K12, plain vs today's flag vs flag +
  content (+ the (a') layout):
  - the per-instruction premium down to ~1.1-1.3x (cycles/instructions; L3 and other-L2 fills against plain);
  - drizzle's cycles below plain's by ≥ 15%;
  - zod and effect within 5% of plain;
  - vue no longer 2x.
- **Stage 3: no-go for now.** Revisit only if stage 2 ships and drizzle-like projects are the human's priority.
- If the laptop shows the premium unchanged by (a') and content, the sharing model is too expensive per instruction
  on this hardware whatever the design. bun pays the same: its CPU at the defaults is 1.6-3x its single-thread time.

## The laptop A/B (prompt)
On the 12-core laptop, at wt-sg2's head (b5419fe7a or later), two release builds: `meson setup rel --buildtype
release && ninja -C rel tttt`, and `meson setup relpacked --buildtype release -Dc_args=-DTTTT_SHARD_PACKED && ninja
-C relpacked tttt`. AC power, `balanced`, nothing else running. For drizzle, zod, effect and vue (`-p` as in
scripts/sharedgraph/paths.sh), each `perf stat -r 7 -e cycles:u,instructions:u,cycles:k,L1-dcache-load-misses,
LLC-load-misses` plus the L3/other-L2 fill events used in measurements.md's drizzle run, all with
`--threads 12 --checkers 12 --checker-memory off --noEmit -p P`:
1. plain: `rel/tttt`;
2. today's flag: `TTTT_SHARED_GRAPH=1 relpacked/tttt`;
3. flag + content: `TTTT_SHARED_GRAPH=1 TTTT_FIRST_TOUCH_STAMPS=0 TTTT_NESTING_ORDER=content relpacked/tttt`;
4. flag + content + (a'): same environment, `rel/tttt`;
5. flag + content + (a') + (b'): 4 with `TTTT_GRAPH_BATCH=1`.

Also `perf c2c record` on drizzle for 2 and 4 (the top contended lines with their symbols), and wall best-of-7 for
each. Report per corpus: instructions, cycles, IPC, fills, wall, against 1. The decision numbers are the IPC gap
between 1 and 4, and vue's wall in 2 vs 4.
