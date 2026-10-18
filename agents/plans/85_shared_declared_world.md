# Plan 85: stop re-deriving the declared world (planning, with a go/no-go gate)
Written 2026-10-18 by the insights after the twentieth window, as a stub: **the first session is a planning session**
that measures, prototypes and either writes the real stages here or closes the plan with numbers.

## Why now
vision.md item 22 said the human's 12-core numbers would decide whether sharing the declared world is worth a plan.
They arrived (measurements.md "The 12-core machine: tttt, tsgo, bun, tsc-rs", 2026-10-18) and they say the question
is real:
- **CPU at the defaults is 3-7x tttt's own single-threaded CPU** (24 checkers): vscode 43.3 s vs 12.2, effect 4.4 vs
  1.3, zod 3.6 vs 0.63, valibot 2.3 vs 0.43, drizzle **6.4 vs 0.85**. tsc-rs at 4 checkers pays 1.3-1.5x (vscode 22 s,
  the least CPU of any tool on 11 / 14), tsgo ~2x, bun ~3x. tttt's single-threaded lead (2-2.5x less CPU than bun and
  tsc-rs) is spent at the defaults: absolute CPU there is level with bun's and up to 2x tsc-rs'.
- **Memory at the defaults misses the target on 12 cores**: bun lowest on 10 / 14, tttt 1.2-1.8x bun's. The 4-vCPU
  box's 8 / 14 doesn't generalize to the machines people run.
- On this 4-vCPU box: drizzle K4 2.1 s CPU against K1's 0.89 (19 recycles in 0.5 s of checking), monkeytype 4.6 s
  against 0.58, dokploy's K4 slower than K1 (bugs/perf_leads.md "Shared warm state at the defaults").

Both columns are the same cost: K checkers each building, dropping (recycling at a 16 MB start) and rebuilding the
declaration-level types of the same `.d.ts` world (lib, react, drizzle, zod schemas, prisma). Five budget policies
were prototyped and declined (perf_leads.md); every policy only moves along one frontier (memory for CPU). The
previous rejections of a shared layer (plans 67, 81) were on **memory** grounds at 4 checkers, and plan 79's copying
recycle cost as much as re-deriving because it copied the graph per recycle. Neither priced the CPU column, nor a
layer built once and read by every checker.

## What the planning session does
1. **The re-derivation census** (the measure perf_leads.md asks for): per declaration (symbol of a `.d.ts` or
   declared export), how many times a declaration-level type (declared type, type of symbol, resolved members,
   signatures, alias instantiations with the declaration's own type parameters) is created across all checker
   incarnations at the defaults (oversubscribe here: `--threads 24 --checkers 24` measures CPU-seconds and RSS even on
   4 vCPUs) against K1 without recycling. Report what share of the defaults' extra CPU it is, on drizzle, effect,
   zod, vscode, monkeytype, dokploy. If it is under ~50% of the extra CPU, the cost is elsewhere (relations,
   instantiations of user types): say so and stop.
2. **Designs, priced against tttt's actual invariants** (documentation.md "Typecheck", hacks.md "Parallelism
   contract"). At least: (a) a frozen base built once before the checkers start (a pre-pass checker resolving the
   declared world of declaration files, then read-only: interning lookups fall through to it, its lazy caches either
   complete or never filled after freezing); (b) a base grown on demand under a lock or per-checker overlay promoted
   into the base; (c) recycling that keeps a frozen prefix (the declarations-only part of the arena survives, user
   types don't). What breaks determinism (type ids order union members: `ty/order.c`; the output must be
   byte-identical at every K), what is mutable after creation (member resolution, relation caches, variance), and how
   tsgo/tsc-rs/bun get away with not doing it (they don't recycle at 16 MB; tsc-rs keeps 4 checkers).
3. **Prototype the most promising design far enough to measure** the census' biggest corpus and one thrasher: CPU at
   oversubscribed K24 and K4, peak RSS, K1 unchanged.

## Gate
Go if the prototype (or a defensible extrapolation from it) cuts the defaults' CPU by >= 30% on the mid-size corpora
and the thrashers with peak RSS no higher than today's defaults, K1 within ~3% wall, and a determinism story that
doesn't depend on which checker made a type first. Otherwise close the plan with the numbers, and the next insights
turns to the policy alone (fewer checkers, larger budgets: todo's many-core defaults entry).

## Validation of the eventual stages
`scripts/oracles.py` (kdiff and checkerdiff byte-identical), `scripts/threadcheck.sh`, `scripts/bench.sh --bun` here,
and a 12-core command for the human (`scripts/bench.sh --runs 3 --bun --tscrs PATH`, CPU column included).
