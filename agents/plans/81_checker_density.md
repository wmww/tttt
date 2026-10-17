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

## Stage 1: done (2026-10-17, todo entry 3; measurements.md "Plan 81 stage 1")
Several checkers now start at 16 MB each (`program_set_checker_memory_shared()`, the CLI's default unless
`--checker-memory` is given), and a checker whose segment ends after fewer than `CHECKER_MEMORY_BACKOFF_FILES` (16)
files doubles its budget up to the split default (96 MB at 4). K1 unchanged. Gate missed on both sides by a little,
no policy met both: corpora 9 / 14 at or below bun's peak (was 4; valibot 158 vs 151, tsc 220 vs 215, effect, webpack,
xstate above), every corpus faster than bun (+12% summed corpora wall); the tail +14% wall (gate +10%), 96% faster
than bun, -22.5% summed peak. The insights decides whether that trade stands (vision.md "Memory target"); reverting
is `SHARED_CHECKER_MEMORY_MB` (main.c) at 96. The tail's cost is recycles rebuilding a shared graph the segment's
first file doesn't show (react-select's tests: 0.28 -> 0.81 s, bun 0.25): that is what stage 2 has to make cheaper,
and its measure is the tail's wall at today's policy (`scripts/perfsweep.py --defaults-only --min-arena 64 --bun
PATH`, against the stage's table).

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
Measure recycles and wall at stage 1's budget on effect, zod, drizzle and the tail's top 20 (react-select's tests,
mikro-orm, folo's renderer, hop-protocol, monkeytype: measurements.md "Plan 81 stage 1"); a recycle's fixed cost
(the global and library types rebuilt, `check_segment_after()`) is a candidate too. Items that move neither
don't merge.

## Stage 3 (only if stage 2 shows recycles still cost on the tail)
Instantiated Prop and Signature compaction and the object literal PropDecl (vscode 184 MB, excalidraw 28), from the
priced list in measurements.md. Each touches hundreds of readers, so plan each one as its own session, with an
accessor migration first (`prop_parent()`, `prop_decl()`).

## Rules
Output byte-identical at every K and budget (kdiff, checkerdiff, recycle). K1 wall: the old ~3% rule. Defaults:
stage 1's gate.
