# Plan 81: the defaults' memory against bun (density measured and under its gate; the lever is the budget)
Planned 2026-10-17 (todo entry 2 of the twentieth window); the numbers are measurements.md "Plan 81 planning".

## What the planning session found
- **Density is under its gate.** Eight priced items (instantiated Prop 56 -> 40 B and Signature 160 -> ~64 B,
  compact object-literal and namespace-export PropDecls, mappers sharing their lists, instantiations' props_by_name
  from the source's permutation, intern keys, per-kind Ty layouts) save 18-24% of the K1 arena on the mid-size
  corpora (effect 24%, valibot 18%), ~10-13% of the defaults' peak, and most touch hundreds of readers. The gate was
  25%.
- **The premise was wrong: per-type cost already matches bun's at one thread.** tttt K1 and bun T1 are close on
  valibot (276 / 285 MB) and excalidraw (392 / 369); effect (556 / 380) is the outlier, and its two worst items are
  ones a session can fix (namespace export props 42 MB, mappers ~60 MB).
- **The defaults' gap is a policy, not density or duplication.** bun's peak *falls* from one thread to two
  (valibot 285 -> 150 MB) while its CPU time rises 15-50% (+70-110% at four): with more than one thread it
  re-derives types rather than keeping them. tttt keeps 4 x 96 MB. The duplication across checkers (+27-49% of K1's
  arena at 96 MB) is smaller than what the budget moves, so the shared frozen layer stays rejected.
- **The budget frontier is far from used up on the corpora.** At 4 x 32 MB tttt is at or below bun on 5 of the 8
  measured corpora (valibot 214, drizzle 312, zod 287, vscode 1168 vs 1552) and within 1.18x on the others, at
  +0-13% wall, and still faster than bun everywhere. At 16 MB it matches bun on all four mid-size corpora. Entry 7
  of the nineteenth window turned 64 MB down because of the source sweep's tail (+9% wall, p90 +34%), measured
  against tttt's own 128 MB, not against bun. Bun was never measured on that tail.

## Stage 1: the budget policy at more than one checker (one session)
K1's 256 MB stays as it is. The vision's "~3% wall per memory change" rule is what stops this. The human's target
says memory first ("less memory than every competitor in most cases"), speed after ("as fast as possible"), so this
stage proposes a different bound at the defaults: **faster than bun everywhere**, on top of byte-identical output.
The next insights session confirms the trade or reverts it (vision.md "Memory target" records the question).
1. Get bun (`npm pack @oven/bun-linux-x64@1.4.3` into its own /tmp directory; bench.sh's header). Add bun to
   `scripts/perfsweep.py --defaults-only` (a `--bun PATH` column: wall, peak, exit code) so the tail can be compared
   with it. bun's diagnostics don't matter here, only its wall and RSS.
2. Measure the frontier at 96 / 64 / 48 / 32 / 24 MB: the 15 `-p` corpora (`--corpora --defaults-only
   --defaults-args "--checker-memory MB"`, best of 2) and the sweep's 169 projects whose K1 arena is over 64 MB
   (`--min-arena 64`, one run each), each next to bun at its defaults.
3. Pick the policy. A constant per-checker budget is the simple version. The warm-up guard (`check_share()`: recycle
   only past 2x the segment's first file) is what keeps dokploy-like shared graphs from thrashing, so it stays. If one
   constant can't serve both the corpora and the tail, try the guard's multiple (2x -> 1.5x / 3x) before anything
   new. Nothing that depends on timing (the output and the cuts must stay K- and timing-independent).
4. **Gate**: at the defaults, below bun's peak on at least 10 of the 14 bench corpora; faster than bun on every
   corpus and on at least 90% of the tail's projects; tail total wall within +10% of today. Wherever it lands, write
   the frontier table into measurements.md and the chosen budget's reasoning into `CHECKER_MEMORY_FLOOR`'s comment
   (program.c). Oracles: `scripts/kdiff.py` and the recycle oracle (`-k 1 -m 0`), `scripts/checkerdiff.py`,
   `scripts/oracles.py`. Then `scripts/bench.sh --runs 3 --bun`, and refresh plan 67's "Where it stands" table.

## Stage 2: make a recycle cheaper to pay for (one session, after stage 1)
At a low budget each recycle re-derives the warm state, so bytes per derived type now buy wall time. The two
items that are a session each and hit effect, the worst corpus:
1. **Namespace export props** (`namespace_export_prop()`, effect 42 MB at K1, zod 7, vscode 18). A namespace
   object's property is Prop + a full 96 B PropDecl just to hold `symbol`. Back these with a decl-less Prop (the
   symbol in a field `prop_decl_none()` props don't use, or a 16 B decl kind), or resolve them lazily per name.
   tsgo's module type uses the export table itself.
2. **Mappers** (`ty_mapper_new()`, effect ~60 MB of ~990k mappers, vscode ~95). Both lists are copied on every
   mapper. `sources` is nearly always a declaration's stable type-parameter list, and `targets` often an interned
   list. Copy only the lists that aren't stable (callers say which, or copy into a single allocation with the header
   when not), and look at `inference_mapper_of()`'s 27 MB on effect: a fresh mapper per changed result.
Measure recycles and wall at stage 1's budget on effect, zod, drizzle and the tail's top 20. Items that move neither
don't merge.

## Stage 3 (only if stage 2 shows recycles still cost on the tail)
Instantiated Prop and Signature compaction and the object literal PropDecl (vscode 184 MB, excalidraw 28), from the
priced list in measurements.md. Each touches hundreds of readers, so plan each one as its own session, with an
accessor migration first (`prop_parent()`, `prop_decl()`).

## Rules
Output byte-identical at every K and budget (kdiff, checkerdiff, recycle). K1 wall: the old ~3% rule. Defaults:
stage 1's gate.
