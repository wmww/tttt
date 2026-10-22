# Cross-checker duplication and scheduling (2026-10-22, worker "dup")

K checkers each build their own type graph. This measures what they build twice and whether assigning files
differently (affinity) avoids it. Verdict: **not worth building**. Assignment can save ~1-2% of the check phase on
vscode, effect and excalidraw and ~9% on drizzle; block stealing does nothing; `auto_checker_count()` stays as it is.
The ceiling for a shared graph is in notes/shared_graph.md "The ceiling, measured". Tools: `TTTT_DUP_*`
(ty/dupcensus.c, CLAUDE.md) and scripts/dupmodel/ (README.md: how to reproduce everything here).

## The census (`--threads 1`: the pure deal; types summed over checkers / distinct; instructions whole process / K1)
| corpus | types K12 / K16 | extras made by every checker (K12) | instr K4 / K12 / K16 | arena K1 -> K12 |
|---|---|---|---|---|
| drizzle | 2.47 / 2.85 | 6% | 1.35 / 1.74 / 1.91 | 328 -> 712 MB |
| effect | 1.47 / 1.54 | 14% | 1.07 / 1.12 / 1.13 | 342 -> 465 MB |
| excalidraw | 2.54 / 2.85 | 19% | 1.16 / 1.34 / 1.38 | 160 -> 344 MB |
| vscode | 1.49 / 1.58 | 11% | 1.04 / 1.09 / 1.11 (check phase) | 1718 -> 2369 MB |
- Type fingerprints are history-stable (distinct types at K12 within 0.1% of K1), so these are exact counts.
- Relation caches duplicate alike: drizzle 1.75x, excalidraw 1.96x, effect 1.16x, vscode 1.26x at K12.
- What is duplicated: vscode literals (397k extra copies at K12) and type-node / object-literal anonymous types
  (205k); drizzle its own conditional and indexed instances; effect anonymous types and its own type parameters.
  Lib instantiations are a minor share everywhere.
- Types are a poor proxy for work: vscode's 1.49x types are 1.09x instructions (the duplicated types are cheap).
- At the defaults (8 checkers on this box, with stealing) drizzle is already at 2.44x types, as high as K12 without
  stealing; stealing at 12 threads takes drizzle 2.47 -> 2.70-2.85x, vscode 1.49 -> 1.50-1.51x.
- Defaults' memory (8 checkers): drizzle arena 720 MB, effect 455, excalidraw 302 (K1: 328 / 342 / 160).

## Avoidable vs inherent (K12, assignments forced with TTTT_DUP_DEAL and measured)
| corpus | best possible: types | best possible: instructions | import graph: instructions |
|---|---|---|---|
| drizzle | 2.47 -> 1.78 | -51% of the extra (-22% of total) | -22% of the extra (-9%) |
| excalidraw | 2.54 -> 2.15 | -26% (-7%) | -3% (-1%) |
| effect | 1.47 -> 1.35 | +4% (worse) | ~0% |
| vscode | 1.49 -> 1.34 | not measured | types 1.42, arena -4% (2369 -> 2272 MB) |
- "Best possible" is local search on the files' true type sets (the dump), loads within 1.1x the mean bytes: a bound,
  not buildable. "Import graph" is the same search on direct imports, what the resolver knows. Longer closures are
  useless (almost every file reaches a hub). Like tsgo's FENNEL partitioner (measurements.md), the graph barely helps.
- So half to three quarters of K12's extra work is inherent: no assignment removes it.

## Per-checker work at the defaults' schedule (instructions in check_share, G; threads = K, stealing on)
| corpus | K1 | K4 | K8 | K12 | K16 | K24 |
|---|---|---|---|---|---|---|
| vscode total | 70.9 | 73.8 | 75.8 | 77.2 | 78.5 | 80.5 |
| vscode longest / mean | | 19.2 / 18.4 | 11.6 / 9.5 | 8.8 / 6.4 | 7.3 / 4.9 | 5.2 / 3.4 |
| drizzle total / longest | 4.51 | 6.59 / 1.81 | 8.50 / 1.22 | 9.71 / 0.93 | 10.59 / 0.86 | 12.04 / 0.67 |
| excalidraw total / longest | 5.59 | 6.68 / 1.73 | 7.42 / 1.04 | 7.90 / 0.71 | 8.29 / 0.71 | 9.02 / 0.71 |
| effect total / longest | 7.30 | 7.87 / 2.00 | 8.19 / 1.05 | 8.38 / 0.75 | 8.53 / 0.73 | 8.86 / 0.58 |
- Valgrind serializes threads, so the longest-checker numbers are approximate; natively vscode's 24 checkers finish
  within 0.6% of each other (stealing balances), and the critical path is ~total/K with a floor at the slowest file
  (vscode's chatListRenderer.test.ts, ~1 s under load; excalidraw is pinned at 0.71 G from K12 by one file).
- Block stealing (TTTT_STEAL_BLOCK=1: a thief takes a contiguous block from the middle of its victim's unclaimed
  files): total work and types within +-1-4% either way, the longest checker mixed (effect -10% at K16/K24, +7% at
  K12). A non-result, left behind its env var.

## K policy
No change. Past K8-K12, drizzle and excalidraw add 15-25% total work for a critical path that barely shrinks
(excalidraw flat from K12; drizzle -28% K12 -> K24). The rule already exceeds the physical cores only for vscode,
where K12 -> K24 costs +4.3% instructions: its CPU seconds are SMT accounting. The laptop should confirm vscode's wall
and CPU at K12 / K16 / K24 and drizzle's wall at K8 / K12.
