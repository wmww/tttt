# Plan 81: checker density (what a type costs us, against tsgo and bun)
Written by the insights of 2026-10-17 (end of the nineteenth window). **A planning session first** (todo entry 2):
measure, decide, and rewrite this file into stages or close it with the numbers.

## Why
The memory window (plans 78, 80, the 96 MB floor) took the *program* to bun's level: vscode is tied with bun at the
defaults (1560 / 1552 MB) and below it single-threaded (1388 / 1414). What's left is the *checker*: at the defaults
tttt is above bun on 11 of 14 corpora, the mid-size ones 1.5-2x (effect 605 vs 315, valibot 314 vs 158, webpack 621
vs 365, excalidraw 469 vs 277). Those programs are small after names (effect 185 MB) and the rest is K checkers' state.

One number says where to look. effect at `--checkers 1`: tsgo makes **942k types** in its whole run, tttt held
**206k types in a 152 MB arena** after its one recycle (~740 B per type, everything included) and recycled at 256 MB.
bun, a tsgo port (so presumably about tsgo's type count; unverified), peaks at 377 MB single-threaded *in total*. If tttt's total type count
is near tsgo's (perfsweep's types ratios say it usually is), each type costs several times what bun's does: the
session's first number is the whole run's types and bytes without recycling. Density buys both axes: the same work in fewer bytes means fewer
recycles (speed) or a lower budget (memory) at every K, where the budget alone (todo entries 7-8) only moves along the
frontier.

## What the planning session measures (one session, no product code)
1. **Census, by kind, at one checker without recycling** (`--checker-memory 0` is the opposite; use a large budget) on
   effect, valibot, webpack, excalidraw, zod and vscode: `scripts/arenasites.sh` (arena bytes by allocating site),
   `scripts/memtrack.sh` (the checker's malloc: LazyMemo pages, intern tables, relation caches), `--time`'s table
   sizes. Group into: types proper (Ty variants), members (Prop/PropDecl/Members arrays), signatures, intern keys,
   links/node links, call resolutions, memos, relation caches, flow caches, other. perf_leads.md "Memory" has the
   2026-10-12 vscode census (object literal Props 212 MB, CallResolution 127, LazyMemo 222, intern tables 202, ...):
   redo it, much changed since.
2. **Per-type comparisons** against tsgo (`--extendedDiagnostics`: Types, Symbols, Memory used; tsgo's Go structs are
   readable in `thirdparty/typescript/tsc/internal/checker/types.go`) for the same corpora: which of our structures are
   bigger than theirs per item, which do we have that tsgo doesn't (or keeps per-node where we keep per-type).
3. **Duplication across K** at 4 checkers (each checker re-derives lib and declared types): how much of 4 x 96 MB is
   the same declared world four times (survivors.sh's census, plan 79, has a version of this).
4. **Candidate items with estimated savings each** (on effect/valibot/webpack at the defaults and vscode), cost in
   instructions, and how many readers each touches. Known leads to price: compact object literal PropDecl (~-30 MB
   vscode), instantiated Prop without `decl` (~-25), Signature declaration fields behind a pointer (~-11),
   `namespace_export_prop` (effect 49 MB in 2026-10), LazyMemo page sparseness, intern table load factors, KEY_FLOW_*
   links into node links.

## Gate
Go if a set of items, each a session or less, cuts the arena-plus-checker-malloc bytes per checked file by **>= 25%**
on the mid-size corpora (which then becomes a lower default budget at equal recycles, or fewer recycles at equal
budget, whichever the budget sweep of todo entry 7 shows is better). Below that: write the numbers into
measurements.md, close the plan, and say in vision.md that the defaults' gap to bun on mid-size programs is
structural (type-graph duplication across checkers) and what would close it (a shared frozen declared layer, which
plan 67 rejected; revisit the rejection with these numbers).

## Rules (vision.md "Memory target")
Each item costs at most ~3% wall at K1 and at the defaults; output byte-identical at every K and budget (kdiff,
checkerdiff, recycle oracles). Measure peaks as `/usr/bin/time` at K1 and the defaults, every corpus, next to bun
(`scripts/bench.sh --bun`).
