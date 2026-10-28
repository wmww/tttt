# The one-checker rerun on history-dependent diagnostics
det3, 2026-10-24. Mechanism: documentation.md ("A history-dependent report reruns the checking in one checker"). Code:
program_typecheck() in program.c (`history_dependent_matters()`, `typecheck_pass_drop()`, `goto again`), Error.history_foreign
and Error.history_entry_dependent (error.c), and resolving_mark_cycle() in ty/checker.c.

## What it is
tttt checks a program with K checkers at once. Some diagnostics depend on what the checker saw before:
- **The limits** (TS2589, TS5114, TS5115, TS2590, TS2321, TS2859): where an instantiation or relation limit trips, and
  what the error type it caches silences afterwards.
- **The circularities** (TS2615, TS7022, TS7023, TS7024, TS2456, TS2502): which declaration of a cycle is found in
  flight.

tsgo has the same dependence: its output moves with `--checkers`. With tttt's dynamic deal (work stealing,
memory-pool recycles), it would also be a run-to-run flake at the defaults.

So when the parallel pass reports such a diagnostic, and the history visibly mattered (rules below), program_typecheck()
drops the checkers and their diagnostics and checks every file again in one fresh checker, in program order. The
output is then tsgo `--singleThreaded`'s, the oracle, at any K and on every run.

`--no-rerun` (or `TTTT_NO_RERUN=1`) skips the rerun and keeps the parallel pass's output. That is faster, but such a
diagnostic's site, and the errors its error type silences, may then depend on the checker count and the schedule.
`--rerun` is the default and wins over the environment variable.

## The trigger (2026-10-24)
Only diagnostics the output shows count. Ignored are diagnostics in an unchecked or excluded file (skipLibCheck's
.d.ts) and under a `@ts-expect-error`/`@ts-ignore`. Of the shown ones, the program reruns for:
1. **Any limit.** Where one trips depends on what its checker instantiated before, also inside the file it checks
   itself. Its error type then silences diagnostics in later files. cloudflare-os@835de9e87 packages/workshop-backend
   reports a tttt-only TS5114 at every K but 1, with 1 to 12 more errors depending on the deal. appflare's TS5115 trips
   in every checker at the first site its own files reach.
2. **A circularity reported in another file than the one its checker was checking, or after a cycle through another
   file's declarations** (Error.history_foreign). When the cycle is found, resolving_mark_cycle() looks up each
   resolution entry's file (resolving_entry_file(); RESOLVING_DECLARATION entries carry Resolving.key_type). Example:
   bun 92-R16b's TS2615 pair across two files.
3. **An implicit-any circularity (TS7022, TS7023, TS7024)** when both:
   - its cycle is more than one function's return type (Error.history_entry_dependent); and
   - its file is reached through imports, transitively, by a file before it in program order (reached_from_earlier();
     a script counts as reached).

   sequelize's `models` field (`readonly models = new ModelSetView<Dialect>(this, ...)`, TS7022) has every cycle entry
   in its own file. The cycle forms only where no earlier file resolved the class first: 3 of 15 runs of the sequelize
   root differed at the defaults without this rule. nest's samples (TS7023) are methods inferring their own return
   type: found at that method from any entry.
4. **Anything after a checker recycled, or a diagnostic without a file.**

## What it buys (each comes back on a revert or with --no-rerun)
- **appflare** (10 configs): TS5115 at up to five sites at K16 against K1's one or two (bugs/tsgo_divergences.md,
  "owner rule").
- **cloudflare-os workshop-backend** (box3's npm sample, 2026-10-24): TS5114 plus the errors its error type changes.
  Byte-identical to --threads 1 at the defaults and at K2 to K16 with the rerun; main before this commit differed at
  K2 and K4.
- **openclaw**: kdiff -k 16 0 / 160. 6 configs were unmeasurable on memory at -j 2: the root, extensions,
  extensions/codex, src/gateway, test and ui. (At kdiff's old default -j 16 these K16 runs, 4-7 GB each without
  recycling, filled the 60 GB box on 2026-10-28: kdiff's -j and its runs now come from tttt_run's memory budget,
  jobs_for() and budget_gb, scripts/tttt_run.py.)
- **sequelize and sequelize/packages/core**: TS7022 (rule 3): 15/15 runs identical to K1 at the defaults.
- **bun 92-R16b**: TS2615 across two files (rule 2). buncases K-dependent: 0.
- **sunmao-ui**: TS2590 from several checkers (rule 1).
- **opencode** (configs where the memory pool recycles, rule 4): kdiff identical.
- APIGuild (discord-api-types' base cycle, TS2310) is not among the trigger codes; the base-cycle work fixes it.

## Cost
On the source sweep's 5166 configs at the defaults, 2026-10-24:

| trigger | configs that rerun |
|---|---|
| any reported history code (d22d93ae to 27cd0d2cf) | 111 |
| only shown ones (27cd0d2cf) | 87 |
| (d)+(B): foreign, two checkers' limits, any TS7022 family (05ca65eb4) | 72 of those 87 |
| this one | 29 of those 87 |

Of the 87, the parallel pass's output was already byte-identical to --checkers 1 in 75 without any rerun. The 12 it
changed (appflare x10, sunmao-ui, sequelize/packages/core at K16) all rerun under the current rule. All 87 are
identical to --checkers 1 at the defaults (twice), K4 and K16.

Wall on the 87, best of 3 per config, on a quiet box (load 7-12) right after each other: main (05ca65eb4) 28.4 s
with the rerun against 10.6 s with --no-rerun, so 17.8 s of rerun. This commit: 24.8 s against 11.4 s, 13.4 s of
rerun. --checkers 1 alone is 25-26 s. The 29 configs that still rerun are mostly limits (appflare, sunmao-ui,
cloudflare-os-like cases), which rule 1 keeps on purpose. Earlier sessions under load 25-45 gave the same ratios,
less reliably.

A rerunning config costs its parallel pass plus a --checkers 1 check: about 2.7x its --no-rerun wall, and more than
--checkers 1 alone. Examples: opencode 11.3 s against 2.8 s with --no-rerun and 6.6 s at --checkers 1; appflare/apps/
manager 5.4 s against 0.5 and 1.3 s. That is what the trigger rules avoid.

## History
- **7ca8d73ea** (2026-10-23): the rerun on limits, for appflare's K-dependent TS5115.
- **1ee362e69**: also on circularities, for sequelize and bun 92-R16b.
- **27cd0d2cf** (2026-10-24): only shown diagnostics. type-fest's test-d (`// @ts-expect-error` over CrashIfAny<any>,
  TS2589) and effect's fiberRuntime.ts:1398 (`// @ts-expect-error` over a TS2590) reran every time for nothing:
  +495% and +306% wall.
- **a50e6ab60**: `--no-rerun` / `TTTT_NO_RERUN=1`.
- **05ca65eb4**: (d)+(B), the safe version.
- **This commit**: limits always, the TS7022 family by cycle shape and import order.

## Fresh eyes (perf, 2026-10-28)
**Measured** (`--time` prints `time: rerun in one checker after a parallel pass of X ms at K checkers[, stopped early]
(RULE, TSn in FILE)`; the study script ran tttt at the defaults, at `--checkers 1` and with `--no-rerun` on the 136
source-sweep configs whose tsgo output has a history-dependent code, box loaded 10-20): 32 rerun at the defaults
(limit TS5115 25, TS2590 1; implicit any TS7022/7023 3; foreign TS2502/TS7023 3). All 136 at the defaults are
byte-identical to `--checkers 1`. Without the rerun 19 of the 32 would have been identical anyway; the 13 that need it
are appflare x11 (TS5115 at several checkers' first sites), better-auth's cloudflare fixture (TS5115) and sunmao-ui
(TS2590). On the bun breadth sweep's 300 slowest projects the rerun is the dominant loss of every project whose
default run is no faster than its single-threaded one (sunmao-ui 0.88x, hop-protocol 1.01x, better-auth 1.18x):
a rerunning project paid its whole parallel pass plus a K1 check.

**Built: the early stop** (15a98e4b7). history_dependent_matters() is monotone in the diagnostics reported (an error
in a checker's context stays there until the pass ends; the rules read only it, static program data and whether a
checker recycled), so the first diagnostic that fires a rule makes the rerun certain. Each checker polls its own new
diagnostics once history_reported is set, after every statement and file, and sets Program.rerun_due; every checker
then drops what it is checking at its next statement (typecheck_stopping() in statements_typecheck() and
check_file()'s deferred checks), skips its after-check pass and declaration walks, and the rerun starts. The output
is the rerun's by construction; a stop whose diagnostics fire no rule at the end (not expected) reruns anyway.
`TTTT_DEBUG_RERUN_STOP=N` forces a stop after the pass's N-th file, mid-file for the others: byte-identical to
`--checkers 1` on the 13 corpora at N = 1, 20, 200. 31 of the 32 stopped early; appflare/apps/manager's parallel pass
236 -> 6-27 ms (typecheck 1.9 -> 1.2 s). A rerunning project now costs about its K1 check plus the time to its first
firing diagnostic: still slower than K1 alone by that much, never faster.

**Verdicts per rule** (keep / narrow / drop):
- **Limits: keep.** Where one trips depends on what its checker instantiated before: the depth and count limits
  count work the caches short-cut, and cached instantiations come from any earlier file, not only the tripping file's
  import closure (global augmentations, lib types every file instantiates). Its error type then silences diagnostics
  in arbitrary later files. 11 of 25 TS5115 configs and the TS2590 one differ without the rerun; nothing tells the 14
  that don't apart without knowing K1's site (cloudflare-os workshop-backend: one checker's limit, still not K1's).
- **Foreign circularities: keep.** better-drizzle and hexo were identical without it, but bun 92-R16b is the flake it
  exists for (16-19 of 60 runs at K4 before); it costs little now that the pass stops at the report.
- **Implicit-any circularities across files: keep.** hop-protocol and better-auth's drizzle adapter were identical
  without it; sequelize differed in 3 of 15 runs.
- **Recycles: keep** (none measured: the pool default recycles nothing on a free box).

**Verdicts per design:**
- **Recheck only the files that can depend on history: drop.** K1's history for a file is every earlier file in
  program order; which earlier work warms the caches a limit counts is not its import closure, and where K1 trips
  first is unknowable without the K1 prefix. Non-history diagnostics are history-independent by the K-stability
  invariant, but the silencing reaches any later file. No narrower recheck gives K1's output by construction.
- **Start the rerun early on a spare thread: subsumed** by the early stop: the pass now ends at the first firing
  statement (median ~30 ms of parallel pass on the 32), so there is little left to overlap.
- **Continue the rerun from a checker that has been checking a program-order prefix** (checker 0 dealt files 0..m in
  order, no stealing into it, its after-check pass deferred): saves at most that prefix, ~1/K of K1, for a deal
  change on the scheduler. Not built.
- **A parallel rerun at a fixed virtual K with tsgo's own association**: tsgo's CLI checks with 4 checkers whose files
  come from a FENNEL partition of the import graph (internal/compiler/checkerpool.go), so tsgo's *default* output for
  these programs is that partition's, not `--singleThreaded`'s. A rerun replaying tsgo's association at 4 checkers
  would be deterministic, about 4x faster than K1, and equal to what users of `tsc` see, but not to the oracle the
  sweeps use (`--singleThreaded`). The human's call; it means porting the partitioner exactly.
- The real fix stays order-independent limits and circularities (agents/notes/reentrancy.md stages C-E).

## Cheaper designs (not built)
- **Recheck only the files that can depend on history** (the reporting checkers' files, or the tripping file's import
  closure), keeping the other checkers' results. tsgo K1's history includes every earlier file in program order, and
  a file outside the closure can still warm caches that move where a limit trips. It needs the set of declarations
  the cached types came from; the taint prototype (agents/notes/shared_graph.md) tracks something close.
- **Start the rerun early, on a spare thread**, at the first shown history diagnostic. This overlaps at most the
  parallel pass: the floor is still the --checkers 1 wall, and peak memory doubles.
- **A deterministic deal** (agents/notes/deterministic_deal.md): run-to-run stable, but not tsgo K1's output, and
  measured at 7-20% wall on every project.
