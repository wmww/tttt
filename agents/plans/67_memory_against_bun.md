# Plan 67: memory against bun (checker recycling, then the AST)
Written 2026-10-14 by todo entry 5 (planning). It takes over plan 63's question ("is the struct diet the path?") and
plan 62 stage 4 ("what is the duplication across checkers?"). Numbers are this machine's (4 vCPU, GCC 15.2) at
70ffccdc; K1 = `--checkers 1 --threads 1`, T4 = the defaults; MB are `/usr/bin/time` peaks.

## The measurement (what decided the plan)
**Bun checks a file in "a clean state"** (its docs: results don't depend on file order), and its RSS shows it: on
vscode at one thread it is 1.36 GB when loading ends (2.7 s) and stays flat for the whole 17.5 s check; on effect it
*drops* mid-check (344 -> 264 MB); its type-fest peak is lower at 4 threads (577 MB) than at 1 (1035). Bun bounds the
checker's memory and keeps only the program. Bun 1.4.3 (`npm pack @oven/bun-linux-x64@1.4.3`, untar, `bun check -p P
--threads=N`) runs on this machine, so the comparison is like for like now.

**tttt's checker grows linearly** (vscode K1 RSS sampled every 0.1 s: +150-170 MB/s from start to end of the check, no
levelling off): ~190 KB per checked file that is never read again. vscode K1: 1727 MB after load+parse, 2281 after
names, **4261** peak (checker arena 1534 MB, 2.4M types, the rest its malloc tables). At T4 the arena sum is 1.82 GB
(+19% over K1: the duplication across checkers is small next to what each one accumulates).

**Recycling the checker** (the prototype, env `TTTT_RECYCLE_MB=B`, since productised as stage 1: at a file boundary,
once the checker's arena passes B MB, run the unused/declaration pass for the files it checked, drop it, continue
the share in a fresh `checker_new()`): output **byte-identical on every corpus tried, at every budget, even B=0** (a
fresh checker per file: tsc 49 recycles, effect 574, webpack 367, drizzle 281; valgrind clean on a small project with
92). That is plan 62's work paying off: the output depends on no checker's history. A fresh checker's warm-up
(`ty_init_global_types` and the lib/global types a file pulls in again) is ~16 ms on vscode.

| corpus | K1 | K1 B=128 | T4 | T4 B=128 | T4 B=64 | T4 B=32 | bun-1 | bun-4 |
|---|---|---|---|---|---|---|---|---|
| vscode | 13.67 s / 4261 | 13.38 / **2513** | 5.82 / 4900 | 5.98 / **3027** | 6.09 / 2717 | 6.34 / 2535 | 20.3 / 1443 | 7.76 / 1590 |
| type-fest | 1.41 / 734 | 1.67 / 263 | 0.77 / 854 | 0.82 / 560 | 0.82 / 469 | 0.82 / 393 | 3.72 / 1035 | 2.03 / 577 |
| effect | 1.48 / 709 | 1.49 / 454 | 0.73 / 804 | 0.73 / 769 | 0.72 / 575 | 0.71 / 457 | 3.13 / 381 | 1.37 / 317 |
| drizzle | 0.80 / 457 | 0.98 / 290 | 0.50 / 656 | 0.51 / 637 | 0.56 / 468 | 0.70 / 330 | 1.62 / 385 | 1.01 / 340 |
| webpack | 1.35 / 541 | 1.36 / 478 | 0.71 / 641 | 0.71 / 642 | 0.69 / 597 | 0.70 / 490 | 2.29 / 383 | 1.11 / 362 |
| zod | 0.68 / 450 | 0.74 / 274 | 0.37 / 533 | 0.37 / 530 | 0.40 / 420 | 0.43 / 287 | 1.40 / 402 | 0.68 / 320 |
| vue | 0.76 / 338 | 0.76 / 334 | 0.41 / 390 | 0.41 / 390 | - | - | 1.21 / 292 | 0.56 / 228 |
| tsc | 0.57 / 224 | 0.57 / 225 | 0.31 / 277 | 0.30 / 277 | - | - | 0.83 / 219 | 0.46 / 224 |

Warm-up cost varies by project: drizzle's files each pull in a big shared type graph (K1 B=128 +23%, T4 B=32 +40%),
type-fest's too at K1 (+18%); vscode, effect, webpack, zod pay ~0-3%. vscode at B=256: K1 2683, T4 3718.

**What's left after recycling is the program**: vscode's floor is ~2.45 GB (K1, many recycles) against 2.28 GB
resident after names. The malloc heap after names is 1736 MB (`scripts/memtrack.sh --phase resolve_names`), **38%
all-zero words (657 MB)**; the source text ~170 MB, the `ResolvedModule` table 33 MB; the binder arena ~550 MB
(`scripts/arenasites.sh`: Symbol 144, Scope 123 for 1.28M scopes, SymbolDecl 60, symbol tables 50, flow nodes ~80).
Bun's whole vscode run (program included) is 1.44 GB.

### The options, costed
- **Checker recycling** (stage 1): vscode -1.75 GB at K1, -1.9 GB at T4; type-fest -64% K1; one session. Do it.
- **A shared frozen lib/global type layer** (plan 62 stage 4's idea): **no**. With recycling the per-checker memory is
  bounded anyway, the K duplication is +19% of the arena at T4, and a fresh checker's warm-up is milliseconds. It
  would only buy time back on drizzle-like projects, against type ids, interning and LazyMemo being per checker
  (documentation.md "LazyType"). Revisit only if stage 1's budget can't be tuned without a time cost.
- **Plan 63 stage 1b** (Expr 72 -> 64, Binding element fields, Symbol bit fields, ResolvedModule hints): ~-90 MB on
  vscode, small sessions each. Stage 2 (folded into this plan).
- **Binder scopes**: 1.28M scopes for 138 MB of source (one per ~108 bytes); tsgo has locals only on containers and
  blocks that declare something. An audit of which scopes are kept but never declare anything may be worth ~-100 MB.
  Stage 2.
- **Plan 63 stage 2, per-file parse arenas**: a time lever (~11% of parse instructions, the huge-page re-exec), ~-5%
  RSS at most (malloc's headers). Done 2026-10-16 (plan 63, closed: bugs/perf_leads.md "Memory").
- **A compact AST** (exact-size node variants in per-file arenas, 32-bit references, 8-byte spans): the zero words
  bound it at ~-650 MB; realistically -400..-500 MB (vscode K1 with recycling ~2.5 -> ~2.0 GB). It is a rewrite of
  the AST's representation under ~1700 `EXPR_*` sites, ~600 statement-kind sites and every walk: several sessions,
  and tsgo doesn't do it. **Not now.** Insights decides after stages 1-2 whether bun's remaining gap (program memory,
  ~1.4x) is worth it; the competitive picture after stage 1 is the table above with tttt at 0.7-1.9x bun's memory.

## Stage 1: checker recycling (done 2026-10-15, todo entry 1 of the seventeenth window)
`--checker-memory MB` (`program_set_checker_memory()`; default 256 MB split between the checkers, at least 128 MB
each; `0` is a fresh checker per file, `off` never recycles; the library/harness default and `--expect-types` never
recycle). `check_share()` checks a share in segments and runs the after-check pass in the segment's checker
(`check_segment_after()`: unused, declaration diagnostics and emit). `checker_memory()` is the arena plus the
intern/link/relation tables and the type directory. It leaves out the LazyMemo pages, whose count depends on how
the parser threads numbered the slots. `--time` prints each share's recycle count and peak.
**The warm-up guard was dropped**: a share's first files are the default library's declarations, so "N times
what the segment's first file held" never stopped a recycle. type-fest's cost comes from one later file
(`extract-rest-element.ts`, +159 ms in a fresh checker), which no cheap measure predicts.

At the default (MB = `/usr/bin/time` peak; "off" from the same run):

| corpus | K1 off | K1 | recycles | T4 off | T4 | recycles |
|---|---|---|---|---|---|---|
| vscode | 13.18 s / 4190 | 13.50 / **2575** | 9 | 5.80 / 4786 | 6.01 / **2858** | 18 |
| type-fest | 1.42 / 738 | 1.65 / 380 | 2 | 0.78 / 857 | 0.82 / 603 | 4 |
| effect | 1.50 / 705 | 1.49 / 534 | 1 | 0.74 / 797 | 0.74 / 717 | 2 |
| drizzle | 0.80 / 450 | 0.87 / 389 | 1 | 0.50 / 648 | 0.50 / 589 | 2 |
| webpack | 1.33 / 533 | 1.33 / 534 | 0 | 0.70 / 629 | 0.70 / 630 | 0 |
| zod | 0.69 / 443 | 0.72 / 382 | 1 | 0.37 / 521 | 0.37 / 519 | 0 |
| vue, tsc | unchanged | | 0 | unchanged | | 0 |

Targets met on vscode (K1 <= 2.6 GB, T4 <= 3.1 GB, +2.4% / +3.6% wall). No corpus is above its old peak. type-fest
K1 costs +16% (two recycles); every T4 run is within +5%. **Not met**: "type-fest, effect, zod, drizzle at T4 within
1.5x of bun-4": type-fest 1.05x, drizzle 1.7x, zod 1.6x, effect 2.3x. 64 MB per checker gets them closer, but vscode
pays +6% wall and type-fest/drizzle +8-12%. Plain budget alternatives at T4: 64 MB x4 is 2591 MB at +6% on vscode,
128 MB x4 2850 at +3%. Below ~600 MB the program and the libraries are most of the peak, which is stage 2's question.

Validation: corpora byte-identical at `--checker-memory 0` against `off` (`scripts/checkerdiff.py -k 1 -m 0`), the
suite 0 lost (the harness doesn't recycle), valgrind clean on two DT packages and a probe with errors at `0`,
`scripts/oracles.py` all as before but dtsdiff 74466 -> 74462 (the undefinedProperties order, above) and the new
`recycle` oracle at 4 / 3363 (bugs/sourcesweep_checker.md "History dependences recycling exposes").

## Stage 2: the program's memory (one or two sessions)
**The re-measurement (2026-10-15, todo entry 3)**: vscode K1 resident after names **2281 MB = the malloc heap 1736
(the AST, memtrack; 38% zero words) + the binder arena ~505 + libs/text**. The "Scope 123 MB" above was
`scripts/arenasites.sh` counting allocations `scope_release()` popped again (fixed: pops are taken off their site
now). True binder arena by site on vscode: Symbol 162 MB (with SymbolAlias), flow nodes ~110, SymbolDecl 60, symbol
tables 57, Scope **67** (747k kept of 1.43M made), ScopeRare 17.
1. **Binder scopes (done 2026-10-15, todo entry 3)**: scopes and ScopeRares in an arena of their own
   (`binder_scope_arena()`), so flow nodes and symbols made during a walk no longer keep walk-only scopes alive;
   `scope_release()` also requires no locals and no symbol/declaration made meanwhile; new release sites (class
   field, `this`, strict and decorator scopes, `self` scopes). vscode: 747k -> 533k scopes kept, resident after names
   2281 -> **2255 MB**, K1 peak 2642 -> **2610**. What's left is 324k function scopes (87k declare nothing, but
   `FunctionImpl.body.scope` is what the types printer names from) and blocks/loops that declare something. A Scope
   diet (96 -> 64 bytes: `flow` is walk-only, `binder`/`file` derivable) would be ~-17 MB more: not worth its churn.
   **The binder is not the lever**: every binder diet together (Symbol bit fields, decl_cap, scopes) is < 60 MB of
   vscode's 2.25 GB program. The AST is 1.74 GB of it.
2. **The cheap stage 1b items (done 2026-10-15, todo entry 4)**: PortItem 192 -> 120, PortStatement 288 -> 216,
   Statement 136 -> 112, ObjectExprItem 160 -> 152. vscode heap after parse 1690 -> 1623 MB, after names 2255 ->
   **2191**, K1 peak 2610 -> **2549**. What's left of plan 63 stage 1b is < 120 MB together (measurements.md): no
   further diet sessions.
3. **The breakdown (done, measurements.md "Plan 67 stage 2 items 2-3")**: vscode K1 2549 MB against bun-1's ~1.4 GB
   is a program gap (2191 vs ~1300 MB: the AST's representation, 782 MB of zero words) plus a check gap (~360 vs
   ~80 MB: the recycling budget).
   **For insights, the compact AST decision** (the recommendation of this session): do plan 63 stage 2 (per-file parse
   arenas) first in any case: it is a time lever (~11% of parse instructions, the huge-page re-exec gone, `-b`'s
   drops), it takes malloc's ~110 MB of headers and size classes, and it is the prerequisite of variable-size nodes.
   Then a compact AST proper (nodes sized to their variant in the file's arena, no inline unions of the largest
   variant; 8-byte spans) is worth ~-500 MB on vscode (2191 -> ~1.7 GB program, ~1.2x bun's) at several sessions
   of churn under ~2300 AST sites. Below ~1.2x bun the remaining lever is the check's 256 MB budget, not the AST.

### Validation
Byte-identical outputs (suite, oracles, threadcheck, checkerdiff), instructions flat (`scripts/instrcount.sh`, react
and nest are the sensitive ones: plan 63 stage 1 cost them 0.6-0.8%), the K1/T4 table rerun.

## Where it stands (2026-10-18, perf session of the twentieth window's todo entry 8, `scripts/bench.sh --runs 3 --bun`)
The 4-vCPU box. Best wall s / peak MB; defaults = every core for both (tttt 4 checkers starting at 16 MB each, plan
81 stage 1), single = `--singleThreaded` / `--threads=1` (tttt one checker at 256 MB).
| corpus | tttt | bun | tttt single | bun single |
|---|---|---|---|---|
| TypeScript6 compiler | 0.32 / 232 | 0.46 / 215 | 0.56 / 217 | 0.83 / 213 |
| type-fest | 0.52 / **419** | 2.01 / 569 | 0.91 / **417** | 3.72 / 1012 |
| valibot | 0.26 / 161 | 0.46 / 155 | 0.48 / **250** | 1.01 / 278 |
| react | 0.13 / **152** | 0.35 / 296 | 0.18 / **138** | 0.38 / 177 |
| nest | 0.10 / **145** | 0.13 / 178 | 0.19 / **129** | 0.22 / 156 |
| webpack | 0.65 / 402 | 1.10 / 358 | 1.30 / 520 | 2.12 / 380 |
| zod | 0.40 / **275** | 0.71 / 318 | 0.70 / **385** | 1.36 / 389 |
| xstate | 0.21 / 186 | 0.28 / 161 | 0.35 / 191 | 0.59 / 174 |
| drizzle | 0.63 / 368 | 0.99 / 334 | 0.96 / 389 | 1.60 / 376 |
| vue | 0.36 / **196** | 0.55 / 220 | 0.74 / 299 | 1.19 / 286 |
| effect | 0.64 / 327 | 1.37 / 312 | 1.44 / 504 | 3.13 / 375 |
| vscode | 6.36 / **1168** | 7.73 / 1559 | 13.78 / **1361** | 20.14 / 1409 |
| excalidraw | 0.57 / **269** | 0.82 / 277 | 1.00 / 362 | 1.48 / 360 |
| tanstack-router | 0.33 / **266** | 0.73 / 334 | 0.66 / **298** | 1.23 / 325 |
Against bun at the defaults: faster on all 14 (1.2x vscode and nest to 3.9x type-fest), at or below its peak on 8
(within 1.16x on the rest: tsc 1.08, valibot 1.04, webpack 1.12, xstate 1.16, drizzle 1.10, effect 1.05).
Single-threaded at or below bun on 7. tttt's times are 3-5% above the previous table's (2026-10-17) while bun's and
the instruction counts are level; the session's start commit times the same as its end on effect K1 (1.44-1.47 s,
interleaved), so that is the machine's state that day, not a regression. tsgo: tttt uses less memory on all 14 at
both (vscode 8016 / 6628 MB).

## Not doing
- Freeing ASTs or function bodies after their file is checked: other files' checks read declarations and infer
  return types from bodies anywhere, in any later checker.
- The compact AST (plan 75, closed 2026-10-16, measurements.md "Plan 75"): exact-size variants reach ~165 MB of
  vscode's 1.37 GB AST at best (the union padding of non-identifier Exprs), under the 400 MB gate; the rest of the
  557 MB of zero words is sparse optional fields spread over every node kind. Only a bun-style index-based rewrite
  would move this axis, and that is not on the table.
- The shared frozen lib layer: a fresh checker is 7 MB of arena (12-core machine); the K-proportional memory is the
  recycle budget, which the CLI's 1.5 GB cap now bounds (plan 75).
