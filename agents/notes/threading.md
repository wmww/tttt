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
   **Measured 2026-10-29 ("The census" below): it dies; the dynamic counter loses to the better mode nearly
   everywhere.** The original step: **Measurement first (one day, no src changes)**: from the duplication census (`TTTT_DUP_CENSUS`, hub's hubtrace),
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

## The census (2026-10-29, 663af2717 + the root census build 7f519a118, worker census): share on second touch dies
The plan's step 1, measured before any prototype. **Verdict: don't build it.** The rule that can actually be built
(a per-declaration touch counter: the first checker to touch a declaration keeps its own derivations private, later
ones publish and consult) loses to the better of plain and `--shared-graph` on 36 of 42 hub projects and 6 of 13
corpora (more than 3% worse), and beats it on none. Even the oracle rule (knowing in advance which declarations two
checkers will reach) only ties on the corpora (median 1.00 against the better mode). The corpora don't route most
types private: 42-72% of their type copies are rooted at declarations two or more checkers reach, depending on the
rule. Their duplication sits in the same generic core every checker uses (zod's schemas, effect's and xstate's
generics, lib), not in pairs and clusters of files.

**Method.** `scripts/sharedgraph/rootcensus.py` (the `-DTTTT_ROOT_CENSUS` build, ty/dupcensus.c's root census; the
normal build is unchanged). Every run is at `--threads 1`, so CPU measures work, not SMT siblings or the load. The
flag's concurrency factor (x0.99-1.6, shared_graph.md "The premium, attributed") comes on top of both the flagged and
the routed runs. Three configurations: K1, the default K (8 on all but vscode 16, tsc 4 and the three
one-checker-rerun projects, left out) with the default deal, and K8 with sgcheck's round-robin deal. The typecheck
phase's process CPU is best of 3 (vscode 2), plain and `TTTT_SHARED_GRAPH=1`, main's release build.
- *Root declaration* of a type made: a declaration is (file, line where its span starts). The four rules:
  - **outer**: the outermost declaration on the resolution stack (`ty_resolving_push` entries that have a
    declaration span) when the type was allocated;
  - **inner**: the innermost one;
  - in both, when the stack holds none, the type's *home*: its alias, the declaration it instantiates, a function's
    span, a type literal's node (keyed by address), an object literal's expression;
  - **home**: the home always;
  - **closure**: home, with a union or intersection hot only when every member that isn't a literal or intrinsic has
    a hot home.
  Types with no root under the rule (literals, rootless unions) stay private. A declaration is *hot* when two or more
  checkers touched it (resolved it at any depth, or made a type rooted there). *Static* shares every hot-rooted copy;
  *dynamic* also keeps the first toucher's copies private (first by monotonic time).
- How much the rule matters: inner catches the most duplication on hubs (89% of duplicated copies, median), outer
  loses ~15 points of hot share, and home is blind to literals (i18n: 10% hot by home, 86% by stack). Outer against
  inner moves the hubs' routed/plain by ~0.1 and the corpora's by ~0.02.
- Attribution I couldn't make: CPU split at the outermost resolution leaves the files' own checking (expressions,
  relations, members resolved outside any resolution entry) unrooted. That is 35% of check CPU on hubs and 64% on the
  corpora, so the model uses types made as the unit of work instead.
- *The model.* plain_K = P1 + a (T - T1), where T is the copies made at K and T1 the types made at K1, and `a` is the
  CPU per duplicated copy, fitted from the two measured plains. The flag is P1 x tau, with tau = flag K / plain K1
  measured: the whole premium, machinery, misses and lost work together. Routed = P1 + a (private copies + shared
  distinct - T1) + (tau - 1) P1 x shared distinct / D: the tax on the shared share of the work, and the private
  duplicates kept. All shared gives the flag back, none gives plain.

| | plain K/K1 | tau | flag/plain | inner static: hot, routed/plain, /best of two | inner dynamic | closure static | closure dynamic |
|---|---|---|---|---|---|---|---|
| hubs (42), default K | 2.25 | 1.55 | 0.736 | 0.83, 0.717, 0.969 (21 beat by >3%, 12 lose) | 0.65, 0.845, **1.104** (0 beat, 36 lose) | 0.77, 0.769, 1.018 | 0.65, 0.844, 1.117 |
| corpora (13), default K | 1.25 | 1.31 | 1.061 | 0.58, 0.998, 1.004 (1, 4) | 0.42, 1.011, **1.028** (0 beat, 6 lose) | 0.72, 1.017, 1.033 | 0.64, 1.025, 1.056 |
| hubs, K8 round robin | 2.55 | 1.62 | 0.658 | 0.84, 0.649, 0.959 (22, 15) | 0.69, 0.780, 1.113 (0, 32) | 0.78, 0.742, 1.035 | 0.65, 0.803, 1.148 |
| corpora, K8 round robin | 1.38 | 1.35 | 1.021 | 0.56, 0.957, 0.991 (5, 3) | 0.48, 0.973, 1.008 (2, 4) | 0.75, 0.993, 1.027 | 0.62, 1.004, 1.039 |

(Medians. "hot" is the share of all copies routed shared; "/best of two" is routed / min(plain, flag).) Per project
(default K, routed/plain inner static/dynamic): i18n 0.60/0.70 against flag 0.49; dokploy/apps/dokploy 0.55/0.64
against 0.64; zod 1.03/1.04 against 1.09; react 0.79/0.88 against 0.79; drizzle 0.93/0.98 against 0.93; effect
1.04/1.05 against 1.15; xstate 1.00/1.03 against 1.03; vscode (K16) 1.00/1.01 against 1.09, with 35% of copies hot.
The full table is `rootcensus.py report` on the run's JSON (not kept).

**Why it loses.** routed - flag = a x (private duplicates kept) - (tau - 1) P1 (1 - sigma), where sigma is the shared
share of distinct types. On hubs the dynamic counter keeps the first toucher's copy of every hot declaration private,
which is a second copy of the whole hub: that costs more than the tax it saves. On the corpora sigma is 0.4-0.7, so the
saving is at most (1 - sigma)(tau - 1) = 10-15% of P1, and it goes to the private duplicates and to the "hot but made
by one checker" copies (15% of all copies on the corpora: instantiations of a hot generic that only one checker makes,
which pay the tax for nothing).

**Where it flips.** Routing beats plain only where the hot share's duplication exceeds tau. With the premium at 1.15
instead of the measured 1.31-1.35, the corpora's dynamic routed/plain would be 0.96 (6 of 13 better, none worse), but
at that tau the flag itself is 0.92-0.95 on them, so the better mode is still the flag. Routing would win only if it
were nearly free on the corpora, meaning hot shares under ~0.2. The corpora measure 0.4-0.7 under every rule, and
vscode, the most private, measures 0.35.

**What the numbers can't tell.**
- Types made stand for work, both per copy (`a`) and per distinct type for the tax.
- A hot-rooted type that references private types couldn't be published as it stands (the private-record rule). That
  lowers the routed sharing further, so this is a bound in routing's favour.
- Reads of shared types from the files' own private code pay the cold-read tax too. The model charges the tax only on
  the shared share, again in routing's favour.
- One thread only: the concurrency factor, the races (dokploy) and the wall's idle tail are outside the model.

**So:** step 1 of the plan closes negative. The per-checker model stays the default and `--shared-graph` an option
(hub_regime.md "The decision"). Of the plan's other directions, options 1 and 4 (single-thread work, the front end, the
idle tail) are unaffected. If anything reopens the mixed mode, it is a lower per-read premium (tau), not a smarter
routing rule: at tau 1.15 the flag alone already ties or wins the corpora.

## Standing rules that bound the work
No machine-specific tuning and no page policy that reads the kernel's state (vision.md); determinism outranks speed
(every mode byte-identical to one checker at any K); perf judged by mechanism or breadth on the dev box, laptop runs
only when a decision hinges on them; vscode-shaped projects must not regress while other shapes get faster; batch
instruction counts (vscode included) at every save, since per-landing gates let ~0.6% drift through per day.
