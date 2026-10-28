# Threading: why tttt's lead erodes with threads, and the plan to unify the modes (2026-10-30)

The coordinator's synthesis of the 2026-10-28..30 measurements (hub_regime.md, shared_graph.md "The premium,
attributed", bun_breadth.md "2026-10-28", footprint.md, intern_tables.md, sharing_design.md §6, measurements.md's
sections at 15f15fdeb and 2c389b213) and the human's conclusions. Read this before touching the parallel model.

## The pattern
tttt wins single-threaded (half bun's instructions on the corpora at the same per-instruction profile; 1.15x on
vscode, ~2x on hub-shaped projects) and the lead shrinks as both get more threads (the 42 hub projects: tttt wall /
bun wall 0.63 at 1 thread, 0.97 at 16 on the 8-core box; vscode on the human's 24-thread laptop 1.15x -> 0.97x).

## The root: per-checker ownership of the type graph (a real trade-off, not a hack)
Each tttt checker builds and caches its own types, memos and relation results privately. Single-threaded that is the
best layout: warm local caches, no atomics, no indirection, no cross-core traffic. With K checkers the same privacy
means each rebuilds whatever declarations its files reach: types made grow ~1 + 0.25 K on hub-shaped projects
(one type graph nearly every file reaches: a tRPC router, an i18n catalog, a zod schema tree), 1.2-1.8x at eight
checkers on ordinary projects. Bun's design is the opposite trade: one shared store, nothing rebuilt, but every shared
read costs a cache miss (and a synchronization step) on the first touch, single-threaded too. Privacy is a tax bun pays
at K=1 and we don't; duplication is a tax we pay at K>1 and bun doesn't. Which is bigger depends on the project's
shape: below ~2.5x duplication privacy wins at any K we run; above it sharing wins and the gap widens with every core.
Not all of tttt's single-threaded lead is the trade: half of it is instruction efficiency (the same algorithm done
tighter) that bun would not recover by giving up sharing; the contingent part is the sharing tax, measured at 1.15-1.3x
per shared read when tttt pays it itself. Two policies also lean to single-threaded speed for the same reason: the
checker count capped at the physical cores (more checkers = more duplication) and the memory pool recycling checkers
when the duplicated graphs exceed the budget; bun has neither problem, which is why it keeps more cores busy on vscode
at the same CPU (16.4 against 21.6 of 24).

## Two side causes, independent of ownership
- The front end is 26% of the multi-threaded wall at the median and speeds up only 2.5-3.5x from 1 to 16 threads while
  the check scales 6-7x; tttt has no lead over bun there (1.15x bun's load after 10-29's fixes), so as K grows the
  fixed part is a larger share of a shrinking wall (Amdahl).
- The idle tail behind one indivisible file (usually a checked `.d.ts`) exceeds its 1/K share on 188 of 300 projects.
  Bun has the same tail but its shared store lets other threads' work warm that file's graph.

## What the shared graph is now (`--shared-graph`, off by default)
Correct: byte-identical to one plain checker on every run we can produce (the walk order for deep nesting, variance
frames, the private-record rule, the race fixes; `scripts/sgcheck.py` is its gate, a `-DTTTT_GRAPH_CHECKS` build its
debug check). Perf: removes the duplication (types made K8/K1 1.00-1.06) and pays the first-read tax everywhere: wins
hubs 10-60% (more at 24 threads), loses ordinary projects 5-20% (react +43%, zod +27%, effect +16%, vscode +6% at the
defaults). The tax is half the shared-table probes (now 8-byte slots with private caching of hits) and half reading
types another checker made, cold by nature. A leaner type representation lowers it only by the density factor on the
read paths (footprint and the tax connect through cache lines per read, not bytes per type) and can't remove it; the
one step taken (instance props made lazily) cut bytes that were never read and bought no cycles. No core-count
threshold and no import-graph predictor separates the winners from the losers (hub_regime.md).

## The human's framing (2026-10-30)
Long term there will not be two maintained modes. Four ways forward were weighed: (1) single-threaded optimizations,
helping everything; (2) optimizations to sharing, helping scaling; (3) routing between the two modes intelligently,
ideally at a granularity finer than the whole project, so every project gets faster than either mode today; (4) other.

## The plan (the coordinator's recommendation, 2026-10-30; to be confirmed by the human)
1. **Option 3 first, as a measured prototype: "share on second touch" at declaration granularity.** The shared graph
   is already a mixed mode (published clean values, private tainted ones behind overlays, readers falling back to
   computing privately), and the uniqueness argument that makes it byte-identical doesn't care *why* a value stayed
   private. Routing is one more input to the publish/consult decision, so correctness by construction is kept. The
   rule: the binder's symbols are already shared read-only, so a per-declaration "touched by N checkers" counter is a
   near-free write on a line both checkers read anyway; a checker that finds the count at >= 1 publishes the types it
   derives from that declaration and consults the shared tables for them; everything else stays private and warm, no
   probe, no cold read. Hub projects are exactly the declarations every checker touches (80% of each checker's types),
   so they'd be shared in full; the corpora's duplication is pairs and clusters, so most of their types never pay the
   tax. Expected: the hub wins kept, the corpora from +5..+20% to roughly neutral, one mode replacing two. It does not
   make ordinary projects much faster than plain (only 1.2-1.8x duplication there to remove).
   **Measurement first (one day, no src changes)**: from the duplication census (`TTTT_DUP_CENSUS`, hub's hubtrace),
   per project the share of each checker's types and instructions rooted at declarations reached by two or more
   checkers, and what the flag's premium would have been if only those were shared. High on hubs and low on corpora:
   build the prototype (about a week inside the existing graph code); corpora also routing most types as hot: the
   idea dies cheaply.
2. **Option 1 as the continuous background**: memory stalls on `.d.ts`-shaped files (31-43% memory-bound against bun's
   28%), bad speculation at twice bun's (5-8% of cycles, unexamined), per-file costs from the perfsweep's types ratio.
   Single-digit percents each, compounding, never wrong.
3. **Option 4 next to it**: the front end and the idle tail, mode-independent Amdahl work with clear mechanisms; where
   a vscode-shaped project actually gets faster.
4. **Option 2 alone is dropped**: fewer shared reads is what option 3 does by construction; the races on true hubs
   (dokploy's 1.6x) are 7 projects.
Beyond all of this, the only new architecture is building a hub's members in parallel at member granularity
(hub_regime.md: ~-50% on dokploy's check for ~2% of projects, weeks, after a shared store is on). Parked.

## Standing rules that bound the work
No machine-specific tuning and no page policy that reads the kernel's state (vision.md); determinism outranks speed
(every mode byte-identical to one checker at any K); perf judged by mechanism or breadth on the dev box, laptop runs
only when a decision hinges on them; vscode-shaped projects must not regress while other shapes get faster; batch
instruction counts (vscode included) at every save, since per-landing gates let ~0.6% drift through per day.
