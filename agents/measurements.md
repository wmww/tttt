# Measurements
The latest of each kind of measurement. **Replace a section when re-measuring** (keep its date and commit); don't
append history, git has it. Older per-session sections (oracle tallies, plan stage logs, superseded perf tables) were
condensed on 2026-10-20 into "Past conclusions" below; the full log is `git show a9c5e07da:agents/measurements.md`.

## Machine and caveats
AMD EPYC-Turin, 4 vCPUs = 2 cores x 2 SMT threads (KVM), 31 GB; Ubuntu 26.04, GCC 15.2, node 22.22. The oracle (tsc)
is tsgo 7.1.0-dev fed0bf24 (`thirdparty/typescript`, plan 54; every cache since 2026-10-09). `scripts/bench.sh` compares against the pinned tsgo only (since 2026-10-12). Wall times drift by ~15% between days (a shared machine) and anything running alongside skews
them: compare walls only within one session. Callgrind instruction counts (`valgrind --tool=callgrind ./release/tttt
-q --keep-going -p DIR`) are the stable measure across sessions. Neither side is clean on webpack, drizzle, vue, effect and
vscode (exit 2): the same (file, line, code) on both (realworld.py), so equal work (and the same
text since 2026-10-18 but webpack's TS5115 name order, `scripts/clidiff.py --corpora`). **The 12-core machine** (the human's
Framework laptop, Ryzen AI 9 HX 370, 12 cores / 24 threads) is measured only when the human says a session runs
there (`nproc` 24): its section below; it is the one to read for core scaling.

## How to measure (notes that still hold)
- **Instructions**: `scripts/instrcount.sh` (callgrind, K1 T1, pgo/tttt since 2026-10-19; saved in `test/instrcount.txt`,
  vscode 75.49 G there). Callgrind counts run ~3% above `perf stat -e instructions:u`: don't compare across tools. At
  K1 the counts include recycling's warm-ups (type-fest, drizzle, vscode recycle at 256 MB): compare with
  `--checker-memory off` for the checker's own work. instrcount gives valgrind a 64 MB main stack (type-fest's
  IntRange needs ~16 MB). A worktree with symlinked corpora can measure some corpora differently (nest +8% once):
  measure in the main tree.
- **bun**: `npm pack @oven/bun-linux-x64@1.4.3` (canary: `@oven/bun-linux-x64@1.4.3-canary.20261019.1`, see below),
  `scripts/bench.sh --bun PATH`. Run `bun check` from an empty directory (`perfsweep.py --bun` does): in a corpus whose
  package.json has a `check` script, `bun check` runs the script. Bun's output is its own format and not tsc's.
- **tsc-rs** (pingdotgg/ts-rust, a Rust port of tsgo at our pin fed0bf24): `scripts/bench.sh --tscrs PATH`.
- **Peaks**: `/usr/bin/time` (no Python parent). `--time` prints RSS after parse / after names, arena and types.
  Recycling's peaks wobble +-10 MB with the sawtooth's phase.
- **perfsweep**: tsgo's side is cached cold (`test/perfsweep/tsgo.jsonl.gz`, incremental projects with their own
  `--tsBuildInfoFile`); its walls right after `oracles.py`'s other oracles are inflated: rerun alone. A project whose
  `.tsbuildinfo` is committed makes tsc skip checking (webcmd).

## The thrashers with the flag (2026-10-21, this box)
Coordinator, pgo/tttt at ee9501eb8, quiet box, best of 3, wall s / CPU s / peak MB, plain -> `TTTT_SHARED_GRAPH=1`:

| project | defaults | K12 T12 |
|---|---|---|
| dokploy/apps/dokploy | 1.87 / 13.1 / 1900 -> 0.86 / 6.3 / 1700 | 1.85 / 18.7 / 2700 -> 0.84 / 8.1 / 1800 |
| monkeytype/frontend | 0.72 / 5.2 / 800 -> 0.31 / 2.1 / 600 | 0.71 / 7.0 / 1000 -> 0.29 / 2.8 / 700 |

2 of 9 dokploy flag K12 runs took 2.42 s / 23 CPU s: the lockstep recycle sg found (plan 86 "Stage 4f: the perf
accounting"). These are the only projects measured where the flag wins big (recycling thrash in plain).

## Stage 2 session 2 (2026-10-22, worker k1; this box, 4 vCPUs shared with other agents)
Single-threaded checker speed, base ee9501eb8 -> 859a7a6a2 (plan 86 stage 2). **Callgrind K1 instructions, PGO + LTO
builds of both** (`pgobuild.sh` without ensure-corpora): vscode 77.79 -> 73.69 G (-5.3%), excalidraw 6.51 -> 5.80
(-10.9%), drizzle 5.60 -> 5.28 (-5.7%), effect 8.14 -> 7.68 (-5.6%), zod 4.22 -> 3.99 (-5.4%), tsc 3.87 -> 3.74
(-3.3%), nest 1.007 -> 0.993 (-1.4%). Release (-O3) vscode at 934908c76: 91.56 -> 88.62 G.
**Wall at K1**, best of N interleaved base/new, s: quiet (load 5-6): tsc 0.50 / 0.50 (7 runs), drizzle 0.86 / 0.82
(7); at abf151c0d (four commits earlier) vscode 12.87 / 12.00 (3, load ~5). Under load 19-21 (relative only): zod
0.92 / 1.01, effect 2.36 / 2.25, excalidraw 1.48 / 1.39, vscode 20.59 / 17.98. Needs the laptop's numbers.
What gave what (release, callgrind, against the commit before): a type parameter's instantiation out of
instantiate_structured()'s frame (drizzle -2.5%); allocation-free temporaries in call resolution, relations,
inference, members (zod -2.0%; the malloc family was 5.6% of vscode's instructions, ~3% after, most of the rest the
loader's paths: push_path_to_absolute, relative_path_of, module_resolver_resolve_shared); a name filter for member
tables of at most 8 props (excalidraw -2.5%); subtype reduction's discriminating key remembered per type
(excalidraw -5.8%); node links, intern tables and relation caches at most 5/8 full (-0.3 to -0.6%); template text
appended in runs (excalidraw -1.3%); hot functions' rare cases (with a local buffer: a frame and a stack protector
check for every call) out of line (apparent_type_inner, find_prop_in, apparent_type_of_mapped_at,
resolved_base_constraint: -0.2 to -0.9%). **Declined**: the distribution's hardening flags (-fno-stack-protector etc.:
-4 to -5.5% instructions, 0-3% wall, a security-posture change: kept); LazySlots as two levels (-0.1% instructions,
D1 read misses up 2.07 -> 2.13% on tsc); splitting is_related_to(), recursive_related() or check_related() at their
caches (instructions up or flat). The parser is ~12% of vscode's instructions with no hot site (74M child Parser
creations for 138 MB of source, the top site 10%): structural.

## The 12-core box at 5f2cd699: the flag's laptop bench (2026-10-21, run by an agent on the human's laptop)
Plan 90 section 3 item 2's ask. Same machine as below (on AC, `balanced`), pgo/tttt at 5f2cd699 (plan 86 stage 4f
done). `scripts/bench.sh --runs 3 --checkers 24 --bun bun-canary --tscrs tsc-rs-0.7.0`, run twice back to back: plain,
then with `TTTT_SHARED_GRAPH=1` ("flag"; it also reran bun/tsc-rs/tsgo, which agree with the first pass within
noise). bun canary `1.4.3-canary.20261019.1` (the same as below). Best wall s / CPU s (median run) / peak MB:

| corpus | tttt K12 | tttt K12 flag | K24 | K24 flag | K1 | K1 flag | bun | bun single | tsc-rs --fast | tsgo |
|---|---|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.22 / 1.1 / 438 | 0.23 / 1.2 / 403 | 0.24 / 1.5 / 500 | 0.25 / 1.5 / 431 | 0.53 / 0.5 / 228 | 0.54 / 0.5 / 230 | 0.29 / 1.6 / 282 | 1.01 / 1.2 / 163 | 0.51 / 1.4 / 328 | 0.61 / 3.1 / 498 |
| type-fest | 0.23 / 1.3 / 680 | 0.31 / 1.7 / 761 | 0.22 / 2.0 / 863 | 0.31 / 2.4 / 791 | 0.72 / 0.7 / 432 | 0.77 / 0.8 / 567 | 0.98 / 6.0 / 980 | 3.62 / 4.1 / 783 | 1.78 / 5.1 / 3287 | 4.46 / 19.6 / 5473 |
| valibot | 0.14 / 1.3 / 387 | 0.17 / 1.4 / 444 | 0.14 / 1.9 / 447 | 0.16 / 2.0 / 472 | 0.44 / 0.4 / 255 | 0.47 / 0.5 / 264 | 0.14 / 2.4 / 263 | 1.19 / 1.2 / 158 | 0.23 / 2.2 / 644 | 0.71 / 4.7 / 810 |
| bulletproof-react | 0.13 / 0.4 / 277 | 0.11 / 0.4 / 282 | 0.10 / 1.1 / 493 | 0.09 / 0.8 / 396 | 0.18 / 0.2 / 145 | 0.18 / 0.2 / 149 | 0.18 / 0.9 / 241 | 0.35 / 0.3 / 153 | 0.27 / 0.7 / 306 | 0.43 / 2.5 / 448 |
| nestjs-boilerplate | 0.08 / 0.4 / 265 | 0.08 / 0.5 / 274 | 0.09 / 0.6 / 349 | 0.08 / 0.6 / 355 | 0.18 / 0.2 / 131 | 0.17 / 0.2 / 133 | 0.06 / 0.6 / 195 | 0.27 / 0.3 / 117 | 0.12 / 0.6 / 240 | 0.12 / 1.2 / 258 |
| webpack | 0.29 / 2.9 / 631 | 0.31 / 2.9 / 739 | 0.28 / 4.1 / 741 | 0.31 / 4.0 / 767 | 1.16 / 1.2 / 533 | 1.22 / 1.2 / 541 | 0.48 / 4.8 / 502 | 1.82 / 1.8 / 336 | 0.50 / 4.9 / 1209 | 1.18 / 7.6 / 1160 |
| zod | 0.23 / 2.2 / 556 | 0.22 / 2.0 / 662 | 0.23 / 3.4 / 755 | 0.24 / 3.0 / 697 | 0.68 / 0.7 / 388 | 0.67 / 0.7 / 442 | 0.22 / 3.2 / 386 | 1.83 / 1.8 / 210 | 0.30 / 2.5 / 864 | 1.08 / 7.4 / 1340 |
| xstate | 0.13 / 1.2 / 420 | 0.14 / 1.1 / 382 | 0.16 / 2.2 / 574 | 0.15 / 1.6 / 458 | 0.33 / 0.3 / 194 | 0.34 / 0.4 / 199 | 0.13 / 1.4 / 237 | 0.73 / 1.0 / 110 | 0.22 / 1.0 / 322 | 0.41 / 3.1 / 465 |
| drizzle | 0.32 / 3.1 / 670 | 0.28 / 2.6 / 715 | 0.35 / 5.8 / 941 | 0.24 / 3.6 / 802 | 0.84 / 0.8 / 393 | 0.83 / 0.8 / 479 | 0.27 / 4.5 / 440 | 1.74 / 2.4 / 270 | 0.55 / 5.7 / 1684 | 1.42 / 8.7 / 1633 |
| vue | 0.17 / 1.6 / 471 | 0.35 / 2.1 / 526 | 0.18 / 2.5 / 515 | 0.44 / 3.0 / 560 | 0.68 / 0.7 / 305 | 0.71 / 0.7 / 307 | 0.22 / 2.7 / 315 | 1.32 / 1.4 / 180 | 0.31 / 2.7 / 679 | 0.89 / 5.2 / 745 |
| effect | 0.26 / 2.8 / 634 | 0.34 / 3.4 / 856 | 0.25 / 4.1 / 751 | 0.33 / 4.8 / 901 | 1.27 / 1.3 / 509 | 1.35 / 1.3 / 622 | 0.55 / 6.2 / 513 | 2.69 / 2.8 / 336 | 0.88 / 9.1 / 1580 | 1.41 / 11.1 / 1537 |
| vscode | 2.45 / 27.4 / 1581 | 2.45 / 25.8 / 2924 | 2.03 / 36.7 / 1990 | 2.15 / 36.1 / 3012 | 12.44 / 12.5 / 1400 | 12.41 / 12.3 / 2400 | 1.74 / 36.4 / 2241 | 15.62 / 16.6 / 1664 | 3.20 / 33.5 / 6376 | 8.24 / 57.1 / 7889 |
| excalidraw | 0.22 / 2.4 / 544 | 0.20 / 1.9 / 575 | 0.24 / 3.5 / 712 | 0.21 / 2.8 / 646 | 0.84 / 0.8 / 370 | 0.87 / 0.9 / 374 | 0.26 / 3.2 / 379 | 1.36 / 1.5 / 256 | 0.76 / 9.4 / 948 | 1.47 / 11.9 / 1830 |
| tanstack-router | 0.20 / 1.6 / 455 | 0.22 / 1.8 / 512 | 0.22 / 2.7 / 618 | 0.22 / 2.3 / 576 | 0.61 / 0.6 / 309 | 0.65 / 0.6 / 317 | 0.24 / 3.3 / 367 | 1.14 / 1.2 / 187 | 0.48 / 2.4 / 525 | 1.21 / 6.9 / 892 |
| TS6 -b | 0.58 / 2.9 / 539 | 0.59 / 3.0 / 548 | - | - | - | - | - | - | - | 1.38 / 7.5 / 562 |

- **The flag does not win wall at the defaults.** K12: drizzle 0.32 -> 0.28 and excalidraw 0.22 -> 0.20 gain, vscode
  ties (2.45), zod/xstate/tanstack/webpack/TypeScript6 tie within 0.02; it loses on **vue 0.17 -> 0.35 (2x, medians
  0.17 / 0.37; K24 0.18 -> 0.44)**, type-fest 0.23 -> 0.31, effect 0.26 -> 0.34, valibot 0.14 -> 0.17. K1 is unchanged
  on vue (0.68 / 0.71), so vue's loss is contention or waiting between workers, not extra work: uninvestigated.
- At K24 the flag helps where K24 used to hurt: drizzle 0.35 -> 0.24 (CPU 5.8 -> 3.6), xstate CPU 2.2 -> 1.6,
  excalidraw 3.5 -> 2.8 s CPU; vscode K24 2.03 plain / 2.15 flag (K24 now beats K12 on vscode in both).
- **Memory**: vscode 1581 -> 2924 MB at K12 with the flag (1.3x bun's 2241, inside the ~3x budget), K1 1400 -> 2400;
  effect 634 -> 856, type-fest 432 -> 567 at K1. Smaller corpora +5-20%.
- K1 cost of the flag: +3-7% on type-fest, effect, tanstack, webpack; flat elsewhere (vscode 12.44 / 12.41).
- **bun canary still has the best vscode wall** (1.74 against tttt's 2.45 K12 / 2.03 K24); at K12 plain tttt leads bun on 8 of
  14, ties valibot/zod/xstate, loses nest (0.08 / 0.06), drizzle (0.32 / 0.27; the flag's K24 0.24 wins) and vscode. Single-threaded tttt is fastest everywhere (vscode 12.4 against bun 15.6 and tsc-rs 16.1).
- Raw outputs were scratch (not kept); rerun the command above to reproduce.

## The config glob next to the load pool (2026-10-21, worker fe, this box: 16 vCPUs / 8 cores, load 10-25)
Since 0eca94ef9 the pool parses while the include glob walks; the glob (vscode, T16) went from 28-30 ms to 68-81 ms.
`--time` now prints the main thread's CPU time per phase: the glob's is 30-32 ms alone and 55-59 ms next to the
pool, so the extra wall is on-CPU, not waiting. The sampler (CPU mode against wall mode, 10-12 runs each) shows no
lock: the pool's and the resolver's locks are ~3% of its wall, and every function of the glob slows about
uniformly (glob_matches 1.7x, getdents 1.3x, hash lookups 1.3x, brk/morecore 2.8x): the SMT sibling and all-core
clocks. Giving the main thread a core (only 4 or 7 of the 16 workers started until the glob ends) brought the glob
back to 37-39 ms, but config + libs + load+parse stayed at 218-226 ms best against 202-207 with every worker:
what the glob gains, the pool's parse loses. A 64 MB M_TOP_PAD for the main arena gave 61-62 against 67-70 ms
(noise-sized). Nothing changed; the glob's own 250 M instructions (glob_matches(), path strings) are what is left.

## The 12-core box at efb91998 (2026-10-20, run by an agent on the human's laptop)
AMD Ryzen AI 9 HX 370 (12 cores / 24 threads), on AC, `balanced` profile, GCC 16.2. Answers 2026-10-20's ask.
**Costliest file first, A/B of the same pgo/tttt** (`TTTT_NO_COSTLIEST_FIRST=1`, 3 runs each at the default K12,
wall s / CPU s): TypeScript6 0.24-0.27 / 1.1 on, 0.21-0.26 / 1.1 off; type-fest 0.21-0.25 on and off; drizzle 0.33-0.34 /
3.2 on, 0.32-0.33 / 3.1 off; vscode 2.41-2.52 / 25.9 on, 2.46-2.49 / 25.9 off. **A wash on 12 cores**: the 4-vCPU
box's -6% on TypeScript6 doesn't show (its wall is 0.24 s here, the front end is most of it). vscode's front end at
K12: libs 40 ms, load+parse files 259 ms (loader waited 14 ms; it parsed 83 ms itself, 744 of 11035 files; workers
busy 5.6 of 7.2 s), resolve names 203 ms (declare 71, link 51, walk 44), typecheck 1852 ms: **~0.5 s of vscode's
2.5 s is the front end**, as plan 86 stage 3 assumed.

**Full bench**: `scripts/bench.sh --runs 3 --checkers 24 --bun bun-canary --tscrs tsc-rs-0.7.0`, bun canary
`1.4.3-canary.20261019.1` (1.4.4-canary.1+6bb5dc195); best wall s / CPU s (median run) / peak MB:
| corpus | tttt K12 | tttt K24 | tttt K1 | bun canary | bun canary single | tsc-rs | tsc-rs --fast | tsgo |
|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.24 / 1.13 / 426 | 0.21 / 1.48 / 518 | 0.44 / 0.44 / 225 | 0.26 / 1.76 / 284 | 0.69 / 1.07 / 168 | 0.42 / 1.22 / 335 | 0.41 / 1.24 / 329 | 0.57 / 2.97 / 498 |
| type-fest | 0.23 / 1.32 / 625 | 0.23 / 1.93 / 859 | 0.66 / 0.66 / 430 | 0.94 / 6.34 / 1011 | 4.56 / 4.57 / 788 | 2.45 / 5.49 / 3134 | 1.48 / 4.63 / 3287 | 4.01 / 17.78 / 5452 |
| valibot | 0.14 / 1.15 / 403 | 0.15 / 2.00 / 466 | 0.38 / 0.38 / 253 | 0.15 / 2.41 / 267 | 1.19 / 1.29 / 157 | 0.35 / 1.49 / 486 | 0.21 / 2.20 / 646 | 0.69 / 4.54 / 809 |
| bulletproof-react | 0.13 / 0.43 / 281 | 0.11 / 1.13 / 487 | 0.19 / 0.18 / 142 | 0.17 / 0.93 / 241 | 0.51 / 0.55 / 151 | 0.22 / 0.67 / 311 | 0.24 / 0.67 / 306 | 0.39 / 2.55 / 450 |
| nestjs-boilerplate | 0.09 / 0.54 / 279 | 0.10 / 0.78 / 359 | 0.16 / 0.17 / 135 | 0.06 / 0.63 / 196 | 0.17 / 0.29 / 120 | 0.12 / 0.54 / 220 | 0.11 / 0.56 / 237 | 0.13 / 1.31 / 257 |
| webpack | 0.30 / 2.90 / 625 | 0.31 / 4.60 / 738 | 1.00 / 0.99 / 528 | 0.45 / 5.90 / 505 | 2.19 / 2.25 / 333 | 0.64 / 2.90 / 841 | 0.45 / 5.13 / 1209 | 1.19 / 7.76 / 1166 |
| zod | 0.20 / 1.89 / 536 | 0.22 / 3.49 / 754 | 0.55 / 0.55 / 393 | 0.25 / 3.66 / 394 | 1.03 / 1.03 / 217 | 0.53 / 2.03 / 792 | 0.34 / 2.54 / 866 | 1.06 / 7.31 / 1346 |
| xstate | 0.14 / 1.22 / 408 | 0.17 / 2.37 / 569 | 0.29 / 0.28 / 192 | 0.14 / 1.49 / 239 | 0.52 / 0.60 / 111 | 0.22 / 1.17 / 324 | 0.26 / 1.14 / 323 | 0.41 / 2.74 / 473 |
| drizzle | 0.35 / 3.49 / 691 | 0.37 / 6.74 / 962 | 0.74 / 0.74 / 398 | 0.30 / 5.23 / 439 | 1.98 / 2.02 / 273 | 0.70 / 2.90 / 999 | 0.53 / 6.89 / 1684 | 1.51 / 9.96 / 1594 |
| vue | 0.18 / 1.68 / 475 | 0.19 / 2.69 / 567 | 0.58 / 0.58 / 308 | 0.23 / 3.13 / 313 | 0.95 / 1.34 / 179 | 0.51 / 2.03 / 496 | 0.28 / 2.98 / 675 | 0.85 / 4.95 / 736 |
| effect | 0.26 / 2.73 / 611 | 0.29 / 4.68 / 773 | 1.10 / 1.10 / 516 | 0.58 / 8.10 / 515 | 2.70 / 3.30 / 329 | 1.30 / 5.93 / 1261 | 0.81 / 9.54 / 1578 | 1.43 / 10.43 / 1555 |
| vscode | 2.67 / 28.55 / 1595 | 2.47 / 41.40 / 1893 | 10.51 / 10.54 / 1371 | 2.05 / 44.21 / 2250 | 17.09 / 17.22 / 1651 | 5.02 / 21.61 / 5498 | 3.02 / 35.64 / 6372 | 7.81 / 56.75 / 7839 |
| excalidraw | 0.25 / 2.47 / 556 | 0.25 / 4.12 / 726 | 0.73 / 0.73 / 367 | 0.24 / 3.52 / 382 | 1.65 / 1.75 / 255 | 0.75 / 2.96 / 614 | 0.88 / 11.05 / 948 | 1.47 / 10.34 / 1795 |
| tanstack-router | 0.19 / 1.64 / 447 | 0.19 / 2.31 / 580 | 0.53 / 0.53 / 307 | 0.22 / 2.78 / 375 | 1.03 / 1.65 / 186 | 0.51 / 1.79 / 536 | 0.42 / 1.91 / 528 | 1.21 / 6.60 / 984 |

- Against 2026-10-19's canary table: tttt K1 is unchanged (vscode 10.51 against 10.61), the canary's numbers are the
  same within noise. tttt K12's CPU reads 5-20% higher on some corpora (vscode 28.6 against 25.1 s, drizzle 3.5
  against 2.9) but the A/B above, minutes earlier, measured vscode at 25.4-26.6 s and drizzle 3.1 s: run-to-run frequency
  noise on a laptop, not a code change (K1 flat). Walls equal within 0.05 s except vscode (2.67 here, 2.41-2.52 in
  the A/B).
- **bun canary still beats tttt's default wall on vscode (2.05 against 2.47-2.67), drizzle (0.30 / 0.35), nest (0.06
  / 0.09) and ties excalidraw, xstate, valibot**; tttt wins the other 8 and every K1 (1.5-7x), and uses less CPU
  everywhere at the defaults (vscode 28.6 against 44.2 s). Nothing landed since 2026-10-19 that should change this:
  plan 86 stage 4 is the lever.
- bun's "single-threaded" CPU exceeds its wall on several corpora (TypeScript6 0.69 s wall / 1.07 s CPU, vue,
  tanstack) and its best and median differ widely: it isn't strictly one thread. Compare medians for bun single.
- tsc-rs and tsgo: unchanged picture (tsc-rs --fast is the closest tsgo-exact competitor, vscode 3.02 s at 35.6 s CPU
  and 6.4 GB).

## Costliest file first and the front end's split (2026-10-20, todo entry 4; this box, 4 vCPUs, release build)
Each checker now starts with its share's biggest file (`deal_files()`'s `share_first`; `TTTT_NO_COSTLIEST_FIRST=1`
turns it off). Best of 5 at the defaults (K4 T4), wall / CPU s / peak MB, before -> after: TypeScript6/src/compiler
0.31 / 0.92 / 235 -> 0.29 / 0.90 / 262, type-fest 0.49 / 1.42 / 413 -> 0.48 / 1.40 / 413; webpack, zod, xstate,
drizzle, vue, effect, excalidraw, tanstack-router, valibot, nest and vscode (6.13 -> 6.07) are within noise.
Output byte-identical (checkerdiff K4, threadcheck); every oracle unchanged but kdiff 2 -> 4: sequelize's TS7022,
which depends on the checker count in tsgo too (tsgo_divergences.md), now shows at K16. What the timeline (`TTTT_FILE_TIMES=1`, now with each file's
end time per checker) shows:
- TypeScript6: checker.ts alone is the critical path, ~65 ms materializing its deferred bodies (serial, the file's
  lock) and 195 ms checking it cold, while the other three finish at ~190 ms. `--no-defer-bodies` is 0.27 s: the
  biggest file's body parse sits on the critical path when it is checked first. Splitting one file's materialization,
  or parsing the costliest file's bodies up front in the loader, are the levers left; both are stage 3 material.
- type-fest: size predicts nothing. union-to-tuple.ts is 1.4 KB and 180 ms at K1, the next are int-range.ts (2.3 KB,
  68 ms) and array-reverse.ts. bun's cost model (identifiers in expressions + type nodes) wouldn't see them either:
  their cost is type-level evaluation. So no second cost model was tried.
- A first version let the costliest file set its segment's recycle warm-up base (`segment_base`): excalidraw and
  xstate got 5-10% faster but excalidraw's peak rose 34 MB, because checker 0 held 2x its first file's 23 MB before
  recycling. That gain was fewer recycles, not ordering. The costliest file no longer sets the base.

The front end (`--time`, `TTTT_LOAD_POOL_STATS=1`, `TTTT_RESOLVE_TIMES=1`), ms:
| corpus, threads | libs | load+parse | resolve | of which declare + link (serial) / walk | check |
|---|---|---|---|---|---|
| vscode T4 | 34 | 454 | 318 | 138 / 130 | 5226 |
| vscode T1 | 10 | 866 | 551 | 135 / 365 | 11873 |
| webpack T4 | 15 | 85 | 85 | | 433 |
| webpack T1 | 9 | 231 | 165 | | 857 |
| effect T4 | 13 | 35 | 48 | | 512 |
| effect T1 | 10 | 100 | 89 | | 1187 |
On vscode at T4 the load pool's workers are busy 1779 of 1953 ms, so load is CPU-bound here (1953 / 4 = 488 ms).
The loader's own work is 279 ms, 201 ms of it parsing 3075 of 11035 files itself, because they weren't preparsed
yet when it needed their imports. On 12 cores the parse CPU is ~163 ms, so **the loader's serial 279 ms is the
front end's floor, then declare + link's 138 ms**. Stage 3's order: (1) the import pre-scan, so that the loader
never parses; (2) parallel declare and link; (3) the parser itself, which is CPU and so helps 4-core boxes most.
webpack and effect show the same shape at a smaller scale (their pools are saturated at T4).

## PGO + LTO shipped (2026-10-19, todo entry 1; plan 86 stage 1)
`scripts/pgobuild.sh` (pgo/tttt) is what bench.sh, instrcount.sh and perfsweep.py measure from now on. Callgrind
(`scripts/instrcount.sh`, re-saved: compare against it, not older release numbers) against release -O3:
tsc 4.530 -> 3.893 G (-14.1%, untrained), type-fest -12.9%, valibot -15.1%, react -12.7%, nest -11.5%, webpack
-14.6%, zod -16.2%, xstate -15.2%, drizzle -16.5%, vue -15.5%, effect -16.4%, vscode 89.06 -> 75.52 G (-15.2%,
untrained), excalidraw -16.9%, tanstack-router -15.9%. Wall K1-ish, one run, release -> pgo: vscode 12.37 -> 10.93 s,
tsc 0.67 -> 0.54, vue 0.79 -> 0.62, zod 0.78 -> 0.74. `scripts/bench.sh --runs 3` on the 4-vCPU box (pgo/tttt;
best wall s / CPU s / peak MB; drizzle's defaults above its K1 is the 4-core box's known checker split):
| corpus | tttt defaults | tttt K1 | tsgo defaults | tsgo single |
|---|---|---|---|---|
| TypeScript6 | 0.480 / 0.94 / 232 | 0.670 / 0.78 / 221 | 1.780 / 3.33 / 510 | 2.300 / 2.71 / 417 |
| type-fest | 0.860 / 1.51 / 425 | 0.890 / 0.90 / 425 | 12.130 / 22.06 / 5718 | 11.700 / 15.38 / 4334 |
| valibot | 0.580 / 0.92 / 169 | 0.680 / 0.69 / 251 | 2.540 / 5.14 / 832 | 2.860 / 3.91 / 652 |
| bulletproof-react | 0.210 / 0.41 / 160 | 0.250 / 0.25 / 139 | 1.420 / 2.52 / 501 | 1.140 / 1.35 / 290 |
| nestjs-boilerplate | 0.190 / 0.28 / 147 | 0.230 / 0.25 / 130 | 0.590 / 0.91 / 232 | 0.600 / 0.72 / 192 |
| webpack | 1.220 / 2.27 / 400 | 1.780 / 1.77 / 522 | 4.090 / 7.79 / 1167 | 5.420 / 6.03 / 955 |
| zod | 0.650 / 1.50 / 275 | 0.690 / 0.70 / 388 | 2.800 / 7.69 / 1419 | 3.140 / 4.59 / 962 |
| xstate | 0.280 / 0.75 / 185 | 0.340 / 0.36 / 190 | 1.010 / 2.76 / 460 | 1.130 / 1.54 / 338 |
| drizzle | 1.280 / 2.24 / 361 | 0.930 / 0.94 / 393 | 5.010 / 9.73 / 1683 | 3.390 / 4.42 / 880 |
| vue | 0.810 / 1.27 / 221 | 1.060 / 1.05 / 300 | 2.450 / 5.73 / 850 | 2.310 / 3.40 / 675 |
| effect | 0.810 / 2.25 / 331 | 1.610 / 1.81 / 511 | 5.760 / 11.13 / 1592 | 4.300 / 6.49 / 1241 |
| vscode | 6.100 / 21.56 / 1235 | 13.150 / 15.60 / 1363 | 18.030 / 64.25 / 8072 | 28.700 / 37.25 / 6628 |
| excalidraw | 0.520 / 1.86 / 273 | 0.860 / 0.85 / 361 | 2.970 / 10.78 / 1975 | 2.770 / 3.80 / 899 |
| tanstack-router | 0.300 / 1.09 / 282 | 0.610 / 0.64 / 302 | 2.040 / 6.91 / 977 | 2.590 / 3.52 / 673 |
| `-b` TypeScript6/src | 1.040 / 3.49 / 325 | - | 2.050 / 6.41 / 742 | 3.000 / 3.95 / 512 |

## bun canary with oven-sh/bun#45800 (2026-10-19, the human's laptop)
The bun developer claimed a 1.5-2x speedup in `bun check` over 1.4.3. It is oven-sh/bun PR #45800 ("`bun check`: 1.4x
to 7.8x faster on large projects"), merged 2026-10-19 00:42 UTC, and it is in `@oven/bun-linux-x64@1.4.3-canary.20261019.1`
(`--revision` 1.4.4-canary.1+6bb5dc195). The PR's ideas: one heavy-first file queue with no step barriers and results
shared in small commits; union order from type contents rather than ids (our plan 86 stage 4 question); per-member
union property position sets; memoized inference and cached conditional/mapped parts; 20-byte expressions; labelled
tuples sharing members; a parallel loader. Same box as the section below, best of 3, wall s / CPU s / peak MB:
| corpus | tttt K12 | bun 1.4.3 | bun canary | 1.4.3 / canary wall | tttt K1 | bun 1.4.3 single | bun canary single | 1.4.3 / canary single |
|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.25 / 1.20 / 439 | 0.36 / 2.07 / 350 | 0.27 / 1.54 / 280 | 1.33 | 0.45 / 0.44 / 225 | 1.08 / 1.07 / 214 | 0.99 / 0.98 / 162 | 1.09 |
| type-fest | 0.21 / 1.25 / 635 | 0.96 / 5.80 / 824 | 0.95 / 5.60 / 1020 | 1.01 | 0.65 / 0.64 / 428 | 3.74 / 3.73 / 1010 | 3.06 / 3.05 / 784 | 1.22 |
| valibot | 0.13 / 1.13 / 399 | 0.23 / 2.90 / 277 | 0.14 / 2.26 / 268 | 1.64 | 0.39 / 0.39 / 255 | 1.10 / 1.09 / 277 | 1.18 / 1.18 / 158 | 0.93 |
| bulletproof-react | 0.13 / 0.42 / 278 | 0.28 / 1.48 / 367 | 0.18 / 0.92 / 239 | 1.56 | 0.19 / 0.18 / 143 | 0.53 / 0.52 / 175 | 0.43 / 0.42 / 155 | 1.23 |
| nestjs-boilerplate | 0.09 / 0.52 / 274 | 0.11 / 0.68 / 235 | 0.06 / 0.61 / 196 | 1.83 | 0.19 / 0.19 / 135 | 0.39 / 0.38 / 154 | 0.21 / 0.21 / 119 | 1.86 |
| webpack | 0.31 / 2.83 / 606 | 0.57 / 5.60 / 526 | 0.40 / 4.35 / 515 | 1.42 | 1.00 / 0.99 / 527 | 1.96 / 1.95 / 374 | 1.71 / 1.70 / 335 | 1.15 |
| zod | 0.21 / 1.94 / 598 | 0.36 / 3.79 / 508 | 0.21 / 2.85 / 391 | 1.71 | 0.56 / 0.55 / 393 | 1.74 / 1.74 / 389 | 1.05 / 1.04 / 212 | 1.66 |
| xstate | 0.14 / 1.07 / 383 | 0.19 / 1.31 / 245 | 0.15 / 1.51 / 241 | 1.27 | 0.36 / 0.34 / 194 | 1.02 / 1.00 / 175 | 0.71 / 0.71 / 111 | 1.44 |
| drizzle | 0.30 / 2.94 / 703 | 0.46 / 5.50 / 613 | 0.26 / 4.21 / 444 | 1.77 | 0.74 / 0.73 / 398 | 1.46 / 1.45 / 376 | 1.61 / 1.60 / 274 | 0.91 |
| vue | 0.17 / 1.57 / 458 | 0.26 / 3.12 / 352 | 0.24 / 2.63 / 312 | 1.08 | 0.57 / 0.56 / 306 | 1.69 / 1.68 / 280 | 1.37 / 1.35 / 179 | 1.23 |
| effect | 0.24 / 2.53 / 616 | 0.49 / 8.01 / 503 | 0.53 / 6.41 / 520 | 0.92 | 1.12 / 1.11 / 518 | 3.13 / 3.12 / 375 | 3.28 / 3.27 / 329 | 0.95 |
| vscode | 2.44 / 25.09 / 1590 | 2.66 / 51.52 / 2128 | 2.00 / 42.95 / 2246 | 1.33 | 10.61 / 10.53 / 1369 | 19.27 / 19.21 / 1412 | 14.11 / 14.08 / 1654 | 1.37 |
| excalidraw | 0.25 / 2.47 / 558 | 0.36 / 4.75 / 413 | 0.22 / 2.90 / 380 | 1.64 | 0.72 / 0.71 / 365 | 1.56 / 1.55 / 357 | 1.69 / 1.68 / 254 | 0.92 |
| tanstack-router | 0.18 / 1.36 / 439 | 0.44 / 3.97 / 477 | 0.23 / 2.62 / 377 | 1.91 | 0.54 / 0.53 / 305 | 1.59 / 1.57 / 325 | 1.10 / 1.09 / 189 | 1.45 |
- **The claim holds at the defaults, partly.** The median speedup is 1.5x (0.92 on effect, 1.01 on type-fest, 1.33
  on vscode, up to 1.9 on tanstack and nest). Single-threaded it is 0.9-1.9x, with a median of 1.2.
- **The canary beats tttt's wall at the defaults on 4 / 14**: vscode 2.00 against 2.44, drizzle 0.26 against 0.30,
  excalidraw 0.22 against 0.25 and nest 0.06 against 0.09, with zod tied. tttt still uses less CPU everywhere (vscode
  25 against 43 s) and is 1.1-2.9x faster single-threaded (vscode 10.6 against 14.1 s). The canary's peak is below
  tttt's on 12 / 14 at the defaults; only type-fest and vscode are not.
- **bun's output isn't tsc's.** It printed 21 errors on drizzle against tsc's 41 (1.4.3: 18), 0 on vscode against 10,
  and 3 on effect against 5. bun targets 7.0.2. The difference has not been investigated.

## The 12-core box with PGO + LTO (2026-10-19, run by an agent on the human's laptop)
AMD Ryzen AI 9 HX 370 (12 cores / 24 threads), 62 GB, GCC 16.2, idle (load ~1). `scripts/bench.sh --runs 3
--checkers 24 --bun bun-1.4.3 --tscrs tsc-rs-0.7.0` with pgo/tttt; best wall s / CPU s (user + sys, median run) /
peak MB. tttt's default is now K12 (`default_checker_cap()`), K24 the old default; tsc-rs at its defaults is 4
checkers:
| corpus | tttt K12 | tttt K24 | tttt K1 | bun | bun single | tsc-rs | tsc-rs --fast | tsc-rs single | tsgo | tsgo single |
|---|---|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.20 / 1.02 / 419 | 0.22 / 1.45 / 518 | 0.45 / 0.45 / 225 | 0.37 / 2.15 / 350 | 1.06 / 1.08 / 213 | 0.45 / 1.22 / 334 | 0.46 / 1.24 / 328 | 0.74 / 0.74 / 280 | 0.58 / 2.77 / 498 | 1.21 / 1.84 / 426 |
| type-fest | 0.23 / 1.41 / 697 | 0.24 / 1.94 / 924 | 0.67 / 0.69 / 429 | 1.12 / 5.99 / 757 | 3.43 / 4.74 / 1011 | 2.49 / 5.35 / 3138 | 1.56 / 5.63 / 3285 | 3.18 / 3.52 / 2302 | 3.93 / 19.33 / 5529 | 5.91 / 13.85 / 4399 |
| valibot | 0.14 / 1.28 / 368 | 0.14 / 1.88 / 465 | 0.40 / 0.39 / 254 | 0.22 / 2.90 / 280 | 1.56 / 1.71 / 277 | 0.39 / 1.48 / 487 | 0.21 / 2.14 / 646 | 1.07 / 1.06 / 402 | 0.67 / 4.44 / 822 | 1.60 / 3.16 / 633 |
| bulletproof-react | 0.13 / 0.43 / 272 | 0.11 / 1.11 / 493 | 0.19 / 0.19 / 142 | 0.28 / 1.45 / 376 | 0.42 / 0.50 / 179 | 0.21 / 0.66 / 311 | 0.24 / 0.67 / 304 | 0.37 / 0.38 / 235 | 0.40 / 2.42 / 448 | 0.59 / 1.30 / 288 |
| nestjs-boilerplate | 0.09 / 0.52 / 275 | 0.10 / 0.75 / 364 | 0.20 / 0.19 / 132 | 0.10 / 0.62 / 233 | 0.27 / 0.30 / 154 | 0.10 / 0.46 / 225 | 0.10 / 0.46 / 238 | 0.24 / 0.24 / 188 | 0.13 / 1.30 / 260 | 0.31 / 0.65 / 209 |
| webpack | 0.27 / 2.68 / 592 | 0.29 / 4.08 / 726 | 1.00 / 1.01 / 529 | 0.59 / 5.74 / 518 | 2.56 / 2.82 / 377 | 0.82 / 2.95 / 841 | 0.43 / 4.84 / 1205 | 1.79 / 1.83 / 716 | 1.14 / 6.86 / 1177 | 2.54 / 4.19 / 961 |
| zod | 0.23 / 2.14 / 579 | 0.23 / 3.18 / 807 | 0.56 / 0.55 / 392 | 0.37 / 4.29 / 506 | 1.56 / 1.74 / 389 | 0.66 / 2.26 / 791 | 0.30 / 2.40 / 866 | 1.39 / 1.40 / 587 | 1.02 / 6.80 / 1362 | 2.08 / 4.20 / 957 |
| xstate | 0.14 / 1.05 / 406 | 0.17 / 2.24 / 598 | 0.30 / 0.37 / 196 | 0.19 / 1.45 / 245 | 0.66 / 1.07 / 175 | 0.29 / 1.15 / 322 | 0.24 / 0.98 / 322 | 0.55 / 0.59 / 261 | 0.41 / 2.76 / 468 | 0.81 / 1.57 / 362 |
| drizzle | 0.34 / 3.31 / 691 | 0.37 / 6.57 / 986 | 0.75 / 0.75 / 398 | 0.49 / 6.43 / 600 | 2.07 / 2.07 / 377 | 0.66 / 2.78 / 1002 | 0.54 / 6.61 / 1681 | 1.30 / 1.59 / 595 | 1.44 / 9.76 / 1612 | 2.05 / 3.60 / 925 |
| vue | 0.16 / 1.49 / 461 | 0.18 / 2.77 / 533 | 0.59 / 0.59 / 305 | 0.26 / 2.84 / 355 | 1.11 / 1.15 / 288 | 0.47 / 1.67 / 497 | 0.34 / 3.11 / 675 | 1.26 / 1.53 / 417 | 0.87 / 5.09 / 738 | 1.80 / 2.83 / 665 |
| effect | 0.30 / 3.04 / 639 | 0.29 / 4.58 / 760 | 1.13 / 1.12 / 516 | 0.54 / 9.87 / 503 | 3.24 / 4.03 / 373 | 1.22 / 5.09 / 1261 | 0.82 / 8.68 / 1578 | 3.32 / 4.39 / 1049 | 1.43 / 10.60 / 1510 | 3.15 / 5.15 / 1297 |
| vscode | 2.59 / 27.34 / 1581 | 2.42 / 40.52 / 1909 | 10.41 / 10.37 / 1369 | 2.77 / 54.39 / 2138 | 18.30 / 19.73 / 1419 | 5.01 / 22.51 / 5494 | 3.40 / 35.75 / 6378 | 13.99 / 13.95 / 4925 | 7.88 / 60.84 / 7940 | 19.41 / 32.76 / 6533 |
| excalidraw | 0.26 / 2.49 / 557 | 0.27 / 3.81 / 708 | 0.72 / 0.73 / 365 | 0.38 / 5.55 / 419 | 1.90 / 1.91 / 358 | 0.79 / 3.40 / 612 | 0.78 / 10.80 / 946 | 1.92 / 1.90 / 474 | 1.43 / 11.66 / 1799 | 2.13 / 4.33 / 917 |
| tanstack-router | 0.17 / 1.39 / 428 | 0.19 / 2.44 / 603 | 0.53 / 0.52 / 308 | 0.44 / 4.06 / 490 | 1.15 / 1.80 / 324 | 0.50 / 1.77 / 538 | 0.44 / 2.15 / 531 | 1.44 / 1.44 / 420 | 1.18 / 6.74 / 980 | 2.11 / 3.76 / 692 |
| `-b` TypeScript6/src | 0.51 / 3.90 / 634 | - | - | - | - | - | - | - | 1.31 / 6.96 / 558 | 2.54 / 4.27 / 524 |
- **tttt has the best wall time on all 14 corpora at the defaults.** The closest is vscode at 2.59 s against bun's 2.77
  (the earlier release build tied there at 2.69-2.81). Everywhere else the lead over bun is 1.1x (nest) to 4.9x
  (type-fest), and over tsc-rs `--fast` 1.1x to 6.8x. Single-threaded, vscode takes 10.4 s against tsc-rs's 14.0 and
  bun's 18.3, and tttt is fastest everywhere.
- **CPU at the defaults: tttt uses 0.30-0.84x bun's** (vscode 27.3 against 54.4, drizzle 3.31 against 6.43; the old
  K24 default spent 6.4 s there). tsc-rs at 4 checkers uses less CPU on 3 / 14: nest 0.46 against 0.52, drizzle 2.78
  against 3.31, vscode 22.5 against 27.3. tttt's K12 still costs 2.3-4.4x its own K1 CPU (vscode 2.6x, drizzle 4.4x).
- **K12 rather than K24 (the physical-core policy) is confirmed.** K24 is 7% faster on vscode (2.42 against 2.59) and
  15% on bulletproof (0.11 against 0.13). On the other corpora it is the same or slower, while CPU rises 40-160% and
  peak memory 15-80%. Keep the cap.
- **Memory is still the weak axis.** Bun's peak is below tttt's at the defaults on 10 / 14 (tttt is lower on
  type-fest, bulletproof, vscode 1581 against 2138, and tanstack-router), the same as before PGO. xstate is 406
  against 245, excalidraw 557 against 419, effect 639 against 503. Single-threaded, tttt's peak is at or below bun's
  on 6 / 14. tsc-rs's peak at its defaults is 0.8-4.5x tttt's (below it on TypeScript6 and nest; type-fest 3138,
  vscode 5494).

## supabase studio and bun on the 12-core box (2026-10-19, a conversation with the human)
supabase `apps/studio` (b3757692, 12.7k files / 2.0M lines with dependencies, its own tsconfig; `pnpm install
--ignore-scripts`; not in the source sweep), median of 5, wall / CPU / peak MB. Its tsconfig is incremental: tsgo
runs with `--incremental false` (a leftover `.tsbuildinfo` made it a fake 1.35 s):
| | defaults | single-threaded |
|---|---|---|
| tttt | 2.27 s / 22.6 / 1671 | 5.07 s / 5.0 / 1043 |
| bun 1.4.3 | **2.02 s** / 28.9 / 1357 | 9.76 s / 9.8 / 1045 |
| tsgo | 5.39 s / 35.0 / 4317 | 9.60 s / 17.6 / 2869 |
**tttt loses to bun at the defaults here**: a recycle thrasher (each of 12 checkers recycles 8-14 times, rebuilding
React and friends): K12 `--checker-memory off` 1.30 s / 12.8 / 3154, `--checker-memory 1024` 1.65 s / 16.4 / 1980,
K4 off 1.83 s / 9.8 / 2331. perf_leads.md's snapshot recycle is aimed at exactly this. Output: tttt had tsgo's 10
plus 7 spurious TS2322 (4 fixed with ttttBindingDefaultGenericOptional, 3 in triage.md); bun reports 1 error (none
of the 9 TS7031). Bun on vscode the same day: 2.48-2.93 s over one session (same 1.4.3 binary that measured 2.92 /
3.01 on 2026-10-16 / 18: machine state, not bun; `npm view @oven/bun-linux-x64 dist-tags`: latest 1.4.3, the
1.4.4 canary no faster), tttt K12 2.69-2.81, K24 2.47-2.67: **a tie on vscode at the defaults**, tttt at ~57% of
bun's CPU.

## Many-core accounting on the 12-core box (2026-10-19, a conversation with the human; no code change)
vscode `-p`, release build, this laptop (12 cores / 24 threads, idle): wall / check phase / **sum of per-file check
times** (`TTTT_FILE_TIMES=1`) / recycles / peak:
| configuration | wall | check | per-file sum | recycles | peak |
|---|---|---|---|---|---|
| K1, `--threads 1` | 12.1 s | 10.5 s | 8.8 s | 8 | 1.4 GB |
| K1, `--threads 1`, **11 other cores spinning** (pure ALU loops, package at 2.3 GHz) | 19.1 s | 17.2 s | 14.2 s | 8 | |
| K12 defaults (24 threads) | 2.68 s | 2.06 s | 20.8 s | 171 | 1.46 GB |
| K12 `--checker-memory off` | 2.63 s | 1.90 s | 19.9 s | 0 | 5.0 GB |
| K12 `--threads 12 --checker-memory off` | 2.37 s | 1.75 s | 17.1 s | 0 | 5.0 GB |
| K6 `--threads 12` | 3.77 s | 3.19 s | 16.0 s | 150 | 1.37 GB |
| K4 `--threads 12` | 4.91 s | 4.31 s | 14.6 s | 119 | 1.27 GB |
- **All-core clocks alone slow a checker 1.62x** (the spinning-cores row). 8.8 x 1.62 x 1.19 (plan 72's K12
  duplication) = 17.0 s against the 17.1 measured without recycling: duplication ~1.2x, recycling 5-8% of check
  time (for 3.5 GB), the rest is clocks. The checker-side ceiling on vscode here: zero duplication and recycling
  would make the check ~14.2 / 12 = 1.2 s against 2.06, wall ~1.8 s against 2.68 (**1.5x**); the front end (libs
  0.04 + load/parse 0.33 + resolve 0.25 s at the defaults) is at its serial floor (loader ~0.3, declare/link 0.15).
  drizzle: per-file sum K1 0.69 s, K12 2.8-3.2 s (duplication 1.4x serialized), wall 0.39 -> floor ~0.15.
- Plan 85's census measured at `--threads 1` (serialized checkers), which hides clocks and memory contention: price
  many-core work by per-file sums under real parallel runs, with the spinning-cores row as the floor.
- Minor page faults: K1 215k, K12 401k (recycling) / 374k (off); kernel time 2.4 s of 24 s CPU at K12.
- Hardware level (drizzle K1, cachegrind): 6.4 G instructions, LL misses 19.3 M (3 per 1000 instructions, ~1.5 s of
  DRAM latency hidden into 0.85 s), I1 miss rate 2.1% (134 M), D1 2.5%. IPC from callgrind counts / cycles ~1.5 at
  boost. **PGO + LTO** (`-Db_lto=true -Db_pgo=generate`, trained on drizzle, zod, effect, excalidraw, webpack at K1
  and the defaults, then `use`; `-Wno-missing-profile` and `-Wno-error` at link for a GCC stringop-overflow false
  positive at `ty.h:96`): **-8-9% wall everywhere**, untrained corpora included (vscode K1 11.83 -> 10.86 s, defaults
  2.68 -> 2.48; tsc 0.51 -> 0.47; drizzle 0.85 -> 0.78). LTO alone 1-2%. Shipped 2026-10-19 as `scripts/pgobuild.sh`
  (the section above).

## The 12-core machine: tttt, tsgo, bun, tsc-rs (2026-10-18, 2d1ea533)
The human's laptop, release build (before PGO), tttt at 24 threads / K24 (the old default), tsgo 4 checkers, bun every
core, tsc-rs 0.7.0 4 checkers (`--fast`: one per core). The per-corpus table is superseded by "The 12-core box with
PGO + LTO" above (git history has it). Geometric means against tsgo at its defaults (wall / CPU / RSS): **tttt 0.26 /
0.41 / 0.56**, bun 0.41 / 0.60 / 0.41, tsc-rs 0.59 / 0.31 / 0.64, tsc-rs `--fast` 0.47 / 0.44 / 0.77; single-threaded
tttt 0.66 / 0.10 / 0.30, tsc-rs 1.38 / 0.21 / 0.51, bun 1.64 / 0.25 / 0.31, tsgo 1.88 / 0.59 / 0.74 (tsgo's CPU above
its wall single-threaded: GC threads). Output: tsc-rs byte-identical to tsgo on 13 / 14 (effect: its built-in Effect
plugin diagnostics); tttt on 11 / 14 (vue 2 spurious TS7006; webpack and drizzle same (file, line, code), other
elaboration text); bun differs on the five corpora with errors and reports none on vscode.

## Checkers per physical core (2026-10-18, todo entry 1, 99676b23 + the policy)
**Oversubscribed** (`--threads 24 --checkers K` on these 4 vCPUs): wall means nothing, CPU (user + sys) and peak are
the many-core costs without the 12-core box's SMT and clock effects (which inflate its CPU further: drizzle K24 6.37
s there vs 3.81 here). Median of 3, CPU s / peak MB (K1 at 24 threads parses in parallel and has 23 materialize-ahead
helpers, so it isn't `--singleThreaded`):
| project | K1 | K4 | K8 | K12 | K24 | K24 / K12 cpu, MB |
|---|---|---|---|---|---|---|
| TypeScript6 | 0.64 / 363 | 0.93 / 333 | 0.95 / 385 | 1.01 / 434 | 1.07 / 440 | 1.06, 1.01 |
| type-fest | 0.90 / 527 | 1.41 / 462 | 1.49 / 672 | 1.52 / 721 | 1.59 / 918 | 1.05, 1.27 |
| valibot | 0.52 / 396 | 0.93 / 246 | 1.01 / 291 | 1.10 / 330 | 1.19 / 409 | 1.08, 1.24 |
| bulletproof-react | 0.24 / 213 | 0.40 / 296 | 0.48 / 351 | 0.55 / 370 | 0.60 / 437 | 1.09, 1.18 |
| nestjs-boilerplate | 0.26 / 220 | 0.30 / 257 | 0.32 / 274 | 0.34 / 288 | 0.36 / 314 | 1.06, 1.09 |
| webpack | 1.44 / 621 | 2.21 / 514 | 2.48 / 552 | 2.67 / 609 | 2.86 / 712 | 1.07, 1.17 |
| zod | 0.75 / 489 | 1.46 / 407 | 1.74 / 468 | 1.95 / 542 | 2.07 / 670 | 1.06, 1.24 |
| xstate | 0.39 / 323 | 0.74 / 310 | 0.91 / 314 | 1.05 / 393 | 1.41 / 540 | 1.34, 1.37 |
| drizzle | 1.00 / 544 | 2.26 / 507 | 2.89 / 592 | 3.05 / 689 | 3.81 / 894 | 1.25, 1.30 |
| vue | 0.82 / 465 | 1.32 / 322 | 1.43 / 361 | 1.53 / 472 | 1.77 / 505 | 1.16, 1.07 |
| effect | 1.60 / 636 | 2.35 / 490 | 2.47 / 562 | 2.64 / 618 | 2.86 / 775 | 1.08, 1.25 |
| excalidraw | 1.03 / 519 | 2.06 / 428 | 2.21 / 488 | 2.33 / 545 | 2.54 / 645 | 1.09, 1.18 |
| tanstack-router | 0.72 / 424 | 1.23 / 341 | 1.36 / 362 | 1.44 / 394 | 1.53 / 524 | 1.06, 1.33 |
| vscode | 15.52 / 1596 | 23.43 / 1291 | 25.55 / 1487 | 26.89 / 1538 | 28.64 / 1832 | 1.07, 1.19 |
| dokploy/apps/dokploy | 3.57 / 891 | 11.39 / 1348 | 21.46 / 1919 | 28.06 / 2744 | 48.06 / 4809 | 1.71, 1.75 |
| monkeytype/frontend | 0.86 / 504 | 5.01 / 643 | 8.07 / 856 | 9.57 / 1083 | 13.89 / 1719 | 1.45, 1.59 |
- On the corpora CPU grows slowly with K (vscode +15% K4 -> K12), the thrashers almost linearly: each dokploy checker
  rebuilds a ~170 MB warm state (`--time`: every checker holds 170-226 MB at most and recycles 2-4 times at any K),
  ~1.5-2 CPU s each. That is plan 85's subject; the policy only stops paying it twice per core.
- Budgets at K12 (`--checker-memory M`, so no 16 MB start): 1152 (96 each) vs the default's growing start: monkeytype
  CPU 4.93 vs 9.57 (peak 1219 vs 1083), drizzle 2.61 vs 3.05 (775 vs 689), vscode 24.4 vs 26.9 (**2707 vs 1538**);
  dokploy at 200 each 21.2 vs 28.1 CPU at the same peak. Plan 81's frontier again: not changed.
**Real threads** (`--threads 4`, this box: 2 cores x 2 SMT), wall s / CPU s / peak MB:
| project | K2 | K3 | K4 | K2 / K4 wall |
|---|---|---|---|---|
| TypeScript6 | 0.30 / 0.75 / 226 | 0.30 / 0.86 / 233 | 0.31 / 0.90 / 230 | 0.97 |
| type-fest | 0.49 / 0.96 / 335 | 0.46 / 1.25 / 300 | 0.49 / 1.39 / 422 | 1.00 |
| valibot | 0.31 / 0.66 / 140 | 0.28 / 0.84 / 150 | 0.26 / 0.92 / 164 | 1.19 |
| webpack | 0.69 / 1.59 / 378 | 0.66 / 1.98 / 383 | 0.60 / 2.21 / 420 | 1.15 |
| zod | 0.46 / 0.98 / 238 | 0.41 / 1.25 / 258 | 0.38 / 1.44 / 282 | 1.21 |
| drizzle | 0.75 / 1.55 / 272 | 0.65 / 1.93 / 284 | 0.60 / 2.27 / 370 | 1.25 |
| effect | 0.75 / 1.70 / 338 | 0.65 / 2.07 / 363 | 0.61 / 2.31 / 333 | 1.23 |
| excalidraw | 0.66 / 1.48 / 255 | 0.59 / 1.82 / 286 | 0.54 / 2.03 / 262 | 1.22 |
| tanstack-router | 0.39 / 0.82 / 196 | 0.36 / 1.03 / 220 | 0.31 / 1.16 / 268 | 1.26 |
| vscode | 7.70 / 16.58 / 1127 | 6.86 / 20.53 / 1175 | 6.11 / 23.05 / 1168 | 1.26 |
| dokploy/apps/dokploy | 3.62 / 7.54 / 929 | 2.62 / 7.32 / 1160 | 3.00 / 11.19 / 1214 | 1.21 |
| monkeytype/frontend | 1.16 / 2.36 / 443 | 1.31 / 3.86 / 433 | 1.31 / 4.92 / 521 | 0.89 |
  (react, nest, xstate, vue within 1.14.) The SMT sibling buys 15-26% wall on 2 cores: vscode K2 would tie bun
  (7.70 vs 7.73, plan 67 "Where it stands").
- **Policy shipped** (`default_checker_cap()`, main.c): without `--checkers`, the checkers are at most the physical
  cores (distinct package/core ids among the affinity's CPUs in sysfs), but every thread up to 4. Here: 4 as before
  (`--threads 24` now 4 too: oversubscribing takes `--checkers`). 12 cores / 24 threads: 12, where K12 -> K24 bought
  0-11% wall (vscode 2.81 vs 2.50 s, bun 2.92, 2026-10-16) for the CPU and peak above. Confirmed there on 2026-10-19 ("The 12-core box with PGO + LTO": K24 costs +40-160% CPU for 0-15% wall).

## Plan 85's census: what the checkers re-derive (2026-10-18, todo entry 2, 6ae5f65a)
**Work vs inflation** (CPU s, best of 2): `--threads 1` runs every checker on one thread, so its CPU is the work
done; `--threads 4` K4 is this box's default (2 cores x 2 SMT). `off` is `--checker-memory off` (no recycling):
| project | K1 | K4 | K4 off | K12 | K12 off | threads 4: K4 | K4 off |
|---|---|---|---|---|---|---|---|
| drizzle | 0.93 | 1.46 | 1.02 | 1.59 | 1.30 | 2.31 | 1.70 |
| zod | 0.68 | 0.92 | 0.72 | 1.07 | 0.89 | 1.43 | 1.15 |
| effect | 1.40 | 1.51 | 1.39 | 1.52 | 1.44 | 2.30 | 2.22 |
| type-fest | 0.85 | 0.93 | 0.86 | 0.97 | 0.87 | 1.39 | 1.27 |
| excalidraw | 0.91 | 1.30 | 1.04 | 1.38 | 1.21 | 2.03 | 1.63 |
| monkeytype/frontend | 0.77 | 2.89 | 1.26 | 4.22 | 2.25 | 4.89 | 2.02 |
| dokploy/apps/dokploy | 3.20 (off 2.08) | 5.89 | 3.30 | 12.06 | 6.30 | 11.19 | 5.22 |
- At this box's default (real K4) the extra CPU over serial K1 is **mostly SMT/contention inflation** (the same work
  on 4 threads: drizzle 0.85 of 1.38 s, effect 0.79 of 0.90, monkeytype 2.0 of 4.1), then **recycling** (K4 - K4 off:
  drizzle 0.44, monkeytype 1.63, dokploy 2.59), and little **duplication** between checkers (K4 off - K1: 0.0-0.13 on
  the corpora, monkeytype 0.49). At K12 duplication catches up with recycling (drizzle 0.37 vs 0.29, dokploy K12 off
  3x K1 off).
- **The declared world is not what is re-derived.** A census build timed the outermost resolutions of declaration
  files' declarations (`ty_declared_type_of`, `ty_type_of_symbol`, `resolve_declared_members`, `signature_resolve`,
  `signature_return_type` and `prop_type` of uninstantiated declarations whose first declaration is in a `.d.ts`,
  lib included; reverted, method only) at `--threads 1`: K1 -> K12 typecheck ms, those frames' ms, their share of
  the extra: drizzle 792 -> 1452, 7 -> 23 (2.4%); zod 616 -> 1019, 6 -> 28 (5.4%); monkeytype 619 -> 4142, 31 ->
  219 (5.3%); dokploy 2691 -> 11773, 148 -> 680 (5.9%); vscode 11851 -> 14104, 145 -> 487 (15%, lib.dom); effect ~14%.
  TY_DECLARED is 1-5% of the types made; what multiplies is instantiations (dokploy K1 -> K12: 996k -> 5.3M types,
  anonymous 429k -> 2.3M, references 145k -> 860k, unions 133k -> 684k, conditionals 73k -> 439k). Callgrind of
  monkeytype at `--threads 1`, K1 5.9 G vs K12 39.2 G instructions: every hot function x6.6 (union_with_origin,
  ty_instantiate_mapped, intern_slot, recursive_related...), the same profile scaled. The rebuilt graph is the
  project's own `.ts` code instantiating its libraries (dokploy's trpc router of zod schemas, drizzle's own sources),
  which no up-front layer can hold. Plan 85 closed on this (vision.md).

## Plan 86 gate G2: `--shared-graph` on threads (2026-10-21; history: the lock, waits and redo it measures are deleted, see "Plan 86 stage 4f: the end state"; b97b6f4ac, this box: 16 vCPUs shared with other agents)
Release build (not PGO), `-q --keep-going --noEmit`, best of 3 by wall; load 4.8-6.8 (other agents). Columns: wall s,
CPU s (user+sys), peak RSS MB. K1 = `--threads 1 --checkers 1`; def = no flags; defK / shK = `--threads K --checkers K`
without / with `--shared-graph` (which turns recycling off).
| corpus | K1 | def | def4 | sh4 | def12 | sh12 |
|---|---|---|---|---|---|---|
| TypeScript6 | 0.74 .73 221 | 0.22 .84 354 | 0.22 .67 253 | 0.23 .72 264 | 0.21 .99 335 | 0.24 .94 320 |
| type-fest | 0.90 .90 427 | 0.44 1.56 590 | 0.38 1.02 414 | 0.44 1.19 638 | 0.26 1.41 566 | 0.39 1.68 693 |
| valibot | 0.48 .47 251 | 0.14 .92 296 | 0.19 .68 152 | 0.20 .70 298 | 0.12 1.04 296 | 0.13 .96 367 |
| bulletproof-react | 0.18 .18 141 | 0.13 .38 226 | 0.09 .30 197 | 0.08 .24 178 | 0.15 .56 320 | 0.09 .39 250 |
| nest | 0.19 .18 128 | 0.12 .37 212 | 0.08 .25 162 | 0.08 .25 160 | 0.08 .40 232 | 0.08 .37 237 |
| webpack | 1.35 1.34 542 | 0.40 2.73 546 | 0.48 1.82 437 | 0.48 1.77 613 | 0.32 2.85 560 | 0.30 2.43 665 |
| zod | 0.72 .72 377 | 0.21 1.46 449 | 0.27 1.03 268 | 0.27 .96 494 | 0.20 1.90 440 | 0.19 1.38 564 |
| xstate | 0.34 .33 188 | 0.12 .75 310 | 0.15 .54 199 | 0.14 .43 234 | 0.12 1.06 335 | 0.11 .66 285 |
| drizzle | 0.96 .95 394 | 0.31 2.13 494 | 0.44 1.65 357 | 0.48 1.21 556 | 0.28 2.73 575 | 0.43 1.60 631 |
| vue | 0.77 .77 301 | 0.16 1.18 370 | 0.24 .91 216 | 0.24 .91 349 | 0.15 1.44 383 | 0.16 1.36 409 |
| effect | 1.43 1.43 573 | 0.26 2.05 569 | 0.44 1.71 391 | 0.49 1.83 764 | 0.24 2.38 580 | 0.28 2.56 826 |
| excalidraw | 0.93 .93 365 | 0.24 1.76 425 | 0.38 1.44 270 | 0.30 1.12 417 | 0.22 2.10 445 | 0.18 1.62 492 |
| tanstack-router | 0.64 .63 300 | 0.17 1.04 367 | 0.22 .84 240 | 0.23 .83 371 | 0.16 1.27 391 | 0.16 1.15 424 |
| vscode | 13.26 13.25 1407 | 2.45 18.35 1410 | 4.19 16.11 1158 | 4.08 15.52 4384 | 2.21 22.44 1469 | 2.14 21.12 4467 |
- **CPU**: the shared graph saves CPU at K12 on 10 of 14 (drizzle 1.60 against 2.73, zod 1.38 / 1.90, excalidraw
  1.62 / 2.10, xstate 0.66 / 1.06, vscode 21.1 / 22.4); more on type-fest (1.68 / 1.41) and effect (2.56 / 2.38).
- **Wall**: equal or better at K12 on 11 of 14 (excalidraw 0.18 / 0.22, webpack 0.30 / 0.32, vscode 2.14 / 2.21); worse
  on drizzle (0.43 / 0.28: the variance lock, one measurement at a time, and a third of drizzle's work is under
  measurements; 3.0 s of its K12 workers' time is waiting, 0.7 s without the lock), type-fest (0.39 / 0.26) and effect
  (0.28 / 0.24).
- **Memory**: peak RSS is higher everywhere, since nothing is recycled with the flag: vscode 4.4-4.6 GB against 1.4 at
  the defaults (K1 1.4 with recycling, 4.1 without), effect 826 / 580 MB.
- **Work** (callgrind Ir in program_typecheck and the worker threads, `--checker-memory off`, K12 / K1 with the flag):
  zod 4.808 / 4.384 G = 1.097, drizzle 5.796 / 5.276 = 1.099, excalidraw 7.418 / 6.544 = 1.134 (the G2 target, 1.15,
  holds). Before memo publication at the computation's end (7b20a147d): 1.26, 2.63, 1.17.
- **Per-file check-time sums** (TTTT_FILE_TIMES, best of 3; wall per file, waits included): drizzle K1 801 ms, sh12
  3918, def12 2289; excalidraw 805 / 1400 / 1814; zod 661 / 1575 / 1699. At 12 threads every file runs ~2x slower
  per instruction (def12 is 2.2-2.9x K1 for 1.4-1.9x the work); drizzle's sh12 is mostly waiting (2968 ms summed).
- **Determinism** (K12 T12 against K1's output): 20 runs each on drizzle, zod, effect, excalidraw, nest all K1's;
  drizzle 300 loaded runs (an effect K12 loop beside it) all K1's; vscode 5 / 5 (2.16-2.26 s, 4.56-4.58 GB, no redo),
  TypeScript6's compiler 5 / 5; all 13 other `-p` corpora at K4 and K12 twice each K1's output.

## Plan 86 stage 4f: the end state (2026-10-22, wt-d c60fa6257 on 22d3ce85a; plan 86 "Stage 4f: done")
`TTTT_SHARED_GRAPH=1`, this box (16 vCPUs shared, load 5-18). No waits, no redos (deleted). 20-run K12 tables 20/20 on
drizzle, zod, effect, excalidraw, nest; 300 loaded drizzle runs 0 differ, 100 loaded effect 1 (Schema.tst.ts(2936,39));
vscode 5/5; seeds drizzle 191/200 (the query.ts(139,3) TS2322), effect 300/300, zod, excalidraw, nest 100/100; TSan 28
runs (vscode aside) one group, fixed. Work (cgtc.sh) K12/K1 drizzle 1.305 (6.714 / 5.145 G), zod 1.285 (5.602 / 4.360),
effect 1.134 (9.324 / 8.220); against plain K1 1.36x, 1.35x. Wall best of 7, K12 T12 shared / defaults: drizzle 0.239
/ 0.309 s, zod 0.211 / 0.241, effect 0.355 / 0.336. kdiff with the flag: 8 / 3814 (plain 3). Before wt-perf's PERF 1/2,
the flag's K1 cost was drizzle +14.6%, zod +12.9%, nest +10.4%, effect +10.2% instructions over plain (callgrind,
typecheck only; by function in plan 86's D progress). Default path over the stage (plain release, db9ca5304 to the head
before the deletions): drizzle -1.6%, zod, nest, effect within 0.1%, tsc +0.8% (other tracks' commits included).

## Plan 86 4f PERF: the flag's K1 overhead (2026-10-22, wt-perf)
Callgrind whole process, K1 T1 `--checker-memory off`, `TTTT_SHARED_GRAPH=1` over the plain path (base 3fb334159):
drizzle +11.66% -> +4.06%, zod +11.30% -> +4.25%, nest +2.19% -> +0.71%. By group (drizzle / zod, % of plain): Overlay
and word slow paths 4.09/3.97 -> 1.92/1.93 (claims as a stack), shared intern tables + shard mutexes 5.13/4.80 ->
0.02/0.03 (nothing published or probed while one checker is alone on the graph), memo taint hit test 0.75/0.66 and
first-touch stamps 0.81/1.03 unchanged. K12 work (cgtc.sh) -1 to -2%; K12 wall within noise (plan 86 "PERF" note).

## Plan 86 4e item 1: the variance lock removed, first-touch stamps on under the flag (2026-10-22, wt-design4f)
Release build, this box (16 vCPUs shared, load 7-16 during the runs). `--shared-graph` now turns stamps on; the lock in
claim_and_measure() is gone (not just off). Base = de94e1179 (lock on, stamps off), built in the same scratch.
- **Determinism** (K12 T12 against K1's output, `scripts/sharedgraph/`): 20-run tables drizzle 20, zod 20, excalidraw
  20, nest 20, effect 19 (one run added `internal/fiber.ts(293,21) TS7006`); 300 loaded drizzle runs (`t3loop.sh`, an
  effect K12 loop beside it) all K1's (redos 0-8 a run, median 2: without the lock redos are common again); vscode 5 /
  5. Effect's TS7006 is not the lock's: effect 60 more runs give 1 diff with this build, 1 with base + stamps, 1 with
  base as it is (`test/Schema/TestUtils.ts(45,20) TS7006` there): a lost contextual parameter type, ~1.5% of loaded
  effect runs with or without the lock (the stage 4f design's subject, not chased).
- **checkerdiff / threadcheck equivalent** (`TTTT_SHARED_GRAPH=1`, pretty, `--keep-going --checker-memory off`; every
  corpus K1 against K4 twice, K4 at T4 three times, K12 at T12): all 16 corpora identical.
- **Work** (`cgtc.sh`, callgrind Ir in the typecheck and worker threads): drizzle flag K1 5.319 G (base 5.284: the
  stamps, +0.66%), K12 5.940 (base 5.972) = **1.117x** K1; zod K1 4.428 (base 4.390, +0.86%), K12 4.859 (base 4.807)
  = **1.097x**. Default path (no flag, callgrind whole process): nest, drizzle, zod identical to base to the 1e-3 G.
- **Wall** (best / median of 7, interleaved, load ~8): drizzle defaults 0.292 / 0.301 s, flag 0.253 / 0.269, base flag
  (lock) 0.417 / 0.439: the lock's +54% is gone and the flag now beats the defaults. Effect defaults 0.267 / 0.271,
  flag 0.293 / 0.297, base flag 0.281 / 0.286: effect is still +10% over the defaults (it was +17% at G2).

## Oracle totals (2026-10-22: the merger's final run of the third coordinator session, 8a2bb4b38)
The merger's full `scripts/oracles.py` at ai-main 8a2bb4b38; box load ~20, so perfsweep's times are inflated. Suite 13972 /
14006 (dts 1888 / 1910; `--no-defer-bodies` 13972; `--checker-memory 0`: 0 of 17643 differ), every corpus 0/0 but vue
0/2, DT 8595 / 8595, `$ExpectType` 68640 / 68640, flagflip 18 / 3 / 2, source sweep `--sample 4` 1272 / 1274, `-b` sweep
411 / 416, dtsdiff 1328 / 1350 projects (105145 / 106222 files, +7 as tsgo `--singleThreaded`), clidiff 3824 / 3831
(text 1: apollo vite; diagnostics 6), kdiff 3 / 3814 and recycle 3 / 3814 (rxjs, sequelize x2: tsgo-shared), perfsweep
worst types ratio 1.40 (lit tsserver-plugin), median check ratio 0.29 under load. With `TTTT_SHARED_GRAPH=1`:
checkerdiff drizzle/zod identical, TSan drizzle T4 + T12 clean.
**Instructions** (PGO, `test/instrcount.txt` saved at 9f979e91c, now at a recycle-free K1: the memory default alone is
-11.9% drizzle / -6.2% zod of these), session start -> end: nest 0.942 -> 0.932 G (-1.1%), drizzle 5.548 -> 4.601
(-17.1%), zod 4.204 -> 3.731 (-11.3%), effect 8.069 -> 7.449 (-7.7%), tsc 3.865 -> 3.709 (-4.0%); against the previous
save: vscode 76.85 -> 68.45 (-10.9%), excalidraw -11.0%, tanstack-router -7.5%, xstate -6.1%, type-fest -5.4%, vue
-5.2%, valibot -5.1%, webpack -3.3%, react -2.5%.

## Oracle totals (2026-10-23: the merger's last full `scripts/oracles.py` runs, 2026-10-21/22, plain at 9032ae214 and 8d693aeb5)
Suite 13962 / 13996 (dts 1887 / 1909; `scripts/passcount.py`), `--no-defer-bodies` and `--checker-memory 0` unchanged,
every corpus 0/0 but vue 0/2, DT 8595 / 8595, `$ExpectType` 68640 / 68640, flagflip 18 / 3 / 2, source sweep `--sample
4` 1272 / 1274 (1271 at 9032ae214: CONDP's angular-cli regression, fixed at the cause by cff135035; full: 5235 / 5242
on 2026-10-20), `-b` sweep 411 / 416, clidiff 3824 / 3831, dtsdiff 1327 / 1350 projects (+7 as tsgo
`--singleThreaded`; the history dependence fixed: vscode 9929 / 9929, vitest/ui a tsgo check-order artifact), kdiff 3
and recycle 3 (rxjs/packages/rxjs, sequelize, sequelize/packages/core: tsgo-shared), perfsweep worst types ratio 1.40
(lit tsserver-plugin). **With `TTTT_SHARED_GRAPH=1`** (the merger's double run at 9032ae214): the same everywhere but
kdiff 16 / 3814 at that commit (flag-only: mikro-orm x3 (C), trpc x3 and dokploy (V), folo renderer, trpc/examples/nuxt
(W), renovate, react-navigation x2, folo ssr, redux-toolkit), 8 / 3814 at the end state (plan 86 "Stage 4f: done");
the release criterion wants them equal. **The final double run at 8d693aeb5** (the end state, code-identical to 5f2cd6996):
plain exactly as above; the flagged run identical on every line but two: the effect corpus 21 / 0 spurious once (the
Schema.tst.ts(2936,39) cascade, ~1% of loaded runs, residue C+V) and kdiff 8 / 3814 (plain's 3 plus folo/apps/desktop/
layer/renderer, mikro-orm x3, trpc/examples/nuxt). Beside it: TSan drizzle+effect T12 clean 3 / 3 runs (pair union
empty), fingerprint 5 runs each zod 0, drizzle 1 (the union-order tie-break), effect 0, output K1's in all 15. Instructions: `test/instrcount.txt`. `scripts/oracles.py` reprints all
of these (20-30 minutes when the box is loaded: a generous background timeout); compare against its output, not this
paragraph, once it's stale.

## Past conclusions
Condensed from the per-session sections (2026-09-28 .. 2026-10-19); `git log -S'<heading>' -- agents/measurements.md`
finds the full section. The `###` headings are the names other notes cite.

### Plan 81 planning (2026-10-17, ad4e1701)
Census at one checker without recycling (`scripts/arenasites.sh -p P --checker-memory 100000`), arena MB / types:
valibot 137 / 224k, webpack 182 / 320k, zod 276 / 369k, excalidraw 140 / 151k, effect 359 / 533k, vscode 1482 /
2.39M; the big sites are `ty_alloc` (~108 B per type, vscode 278 MB), instantiated Props (56 B, vscode 135), Members
(`builder_finish`, 149), object literal Prop+PropDecl (152 B, vscode 184, excalidraw 28), intern keys (173), mappers
(~95; effect ~60). **Priced density items** (arena MB saved at K1, valibot / webpack / zod / excalidraw / effect /
vscode): instantiated Prop 56 -> 40 B (6.5, 6.6, 18, 3.2, 8.9, 39); instantiated Signature 160 -> ~64 B (1.7, 3.3,
8.6, 1.4, 8, 17); compact object literal PropDecl (2.9, 4.5, 1.3, 9.7, 2, 64); namespace export props without a
PropDecl (effect 25-42: shared members since stage 2); mappers sharing stable lists (a content-checked cache since stage 2); instantiated props_by_name from the
source's permutation (3.3, 5.4, 9.5, 3.5, 5.8, 30); intern keys -20% (3.6, 4.6, 5, 4, 6.5, 35); per-kind Ty layouts
-15% (3.4, 5.4, 6, 2.5, 8.6, 42). All eight: 18-24% of the arena, ~10-13% of the peak; most touch hundreds of
readers. bun's memory *falls* from one thread to two while its CPU rises 15-50% (it re-derives rather than keeps);
duplication across K4 without recycling is +27-49% of K1's arena, so a shared frozen declared layer saves less than
the budget does.

### Plan 81 stage 1 (2026-10-17)
The several-checker budget: 16 MB each, doubling after a segment of fewer than 16 files ("16 b16", shipped as
`program_set_checker_memory_shared()`, only without `--checker-memory`). Corpora sum at 4 checkers: 96 MB each 11.3 s
/ 7184 MB, 16 b16 12.7 / 4865 (9 / 14 at or below bun's peak). The source sweep's 174 biggest: +14% wall, -22.5%
summed peak, 96% faster than bun. **No policy met the gate** (10 / 14 below bun, tail within +10%): the corpora want
low budgets, the tail wants few recycles. Other frontier points: 16 g4 b32 +8% / -19.5%, 24 b32 +10% / -18%.

### Plan 81 stage 2 (2026-10-17)
`import * as ns` shares the module namespace type's members (effect: 3068 namespace types, 42 MB) and mapper lists go
through a content-checked cache (effect copied lists 37 -> 8 MB); effect's K1 arena 359 -> 279 MB. The tail barely
moved (wall -0.4%, summed peak -0.7%). **What a recycle costs is the rebuild, not the recycle**: `checker_recycled()`
is ~1 ms; the next segment re-deriving a shared graph (react-select's tests rebuild csstype/React, ~20 MB, ~120 ms)
is the cost. Tried, not merged: the warm-up guard as the most any one file of the segment added (tail -5.8% wall,
+6% peak; drizzle and excalidraw above bun).

### The K1 budget (2026-10-17)
K1 stays at 256 MB: 128 saves vscode ~250 MB (1450 -> 1202) and the 15 corpora -15% peak for +2% wall, but the
sweep's 169 biggest pay +6% (folo renderer 3.02 -> 4.90 s). vscode's K1 peak is the checker's sawtooth (~1050 MB after
each recycle, ~1450 before the next). LazyType slot blocks (memo pages ~60% used, ~7 MB a checker) merged; boxing
ClassItem's FunctionImpl declined (methods are the biggest variant; exact sizes ~7 MB).

### The checkers' budget (2026-10-17)
Floor 128 -> 96 MB each at the defaults (vscode +1.7% wall for -200 MB; 80 costs the sweep +5%); since replaced by
plan 81 stage 1's growing start. The tail is dokploy-like: one file grows a fresh checker by ~70 MB (a drizzle schema
behind tRPC) in ~100 ms; drizzle re-derives its graph across many files (db.ts, session.ts, select.ts: +20-27 MB each).
**Dropped**: a warm-up guard on the largest single-file growth (same frontier; type-fest's checkers grew to 767 MB); a
time-based guard would make cuts timing-dependent. `program_set_checker_memory_cap()` (1536 MB together, 64 each at
least) binds only past 16 checkers.

### Plan 79, a copying checker recycle (2026-10-17; declined)
Survivors from declaration-level roots at each recycle (`scripts/survivors.sh`): vscode 35% of the arena at 64 MB
(10-27 MB a recycle), dokploy 37-57%, drizzle 13-20%, type-fest 3-5%. Re-derivation per recycle is ~14-36 ms on
vscode; a precise copy of ~20 MB (~1.5M objects, intern tables rehashed) costs about as much, and at K1 the peak is
already the program's. Declined.

### Plan 80 (2026-10-17)
Source text mapped (MAP_PRIVATE) and released at recycles, ResolvedModule cache dropped: vscode after names 1029 ->
843 MB, peak K1 1584 -> 1447, wall +1.3-1.9% (re-faulting text). Mapping without releasing was *worse* than read()
(1640 MB); releasing per file cost +2.7% (TLB shootdowns), per 256 MB segment +0.9%.

### Plan 78 stage 3 (2026-10-17)
Deferred bodies against eager (`--no-defer-bodies`) on vscode: K1 T4 +1.1%, defaults +2.0%, K1 T1 13.47 vs 13.08 s
(+3%). Huge pages for materialization slabs took stage 2's +4% back to that (page faults 636k -> 307k at T1);
carrying owner-made materializations across recycles kept their chunks (+150 MB peak), so cross-made ones use a lasting
small-page slab. Materializing costs ~1.69 s single-threaded on vscode against the eager parse and walk's ~1.38,
re-materializing 0.23 s (single-body re-materialization of 2026-10-18 took the defaults' 0.80 s CPU to 0.16).
Stage 2 (drop materializations with the last checker incarnation) took vscode K1 2490 -> 1574 MB; stage 1's upper
bound (every body skipped) was vscode -1263 MB after names.

### Plan 75 (2026-10-16)
Compact AST **no-go**: vscode's AST 1367 MB holds 557 MB of zero words, but diffuse (no site past 50 MB; sparse
optional fields and empty vectors, not union padding); exact-size Expr variants reach ~165 MB at most, under the
400 MB gate. Other estimates: 12-byte spans ~60 MB, 8-byte spans ~120 MB (~570 `.buf` sites), 32-bit node references
~40 MB. A lone checker keeps 256 MB (128 costs drizzle +12%).

### Plan 63 (2026-10-14 .. 16)
Stage 2: per-file AST arenas (`ast_owns()` by address; frees of arena memory are no-ops after the parse) and huge
pages. Madvising every arena chunk on a long-running, fragmented machine made vscode **9.39 s with 13.9k compaction
stalls** against 6.17 s without: chunks are now madvised only while a `/proc/buddyinfo` budget lasts
(`huge_pages_take()`), with `MADV_POPULATE_WRITE` prefaulting. Oracle: `compact_stall` and `thp_fault_alloc` in
`/proc/vmstat` around a vscode run. vscode 8.14 s / 2779 MB -> 5.91 / 2727 at the defaults (the glibc re-exec gone).

### Plan 67 stage 2 items 2-3 (2026-10-15)
vscode K1 against bun 1.4.3 at one thread then: program after names 2191 MB vs bun ~1300; check on top ~360 MB vs
~80; peak 2549 vs ~1400. The gap was the AST representation (782 MB of zero words) plus the recycling budget; the
struct diet was declared done (< 120 MB left). Since then deferred bodies, plan 80 and the budgets brought vscode K1
to ~1370 MB (the 12-core table above).

### The 12-core machine: checkers per K (2026-10-16, f760ecdc)
Release build, K24 default then. K4 -> K12 scaled 1.2-2.0x; **past 12 almost nothing** (K12 -> K24 0-11% on vscode,
webpack, effect; 0 elsewhere) while each checker past 4 added ~170 MB on vscode: the origin of the physical-core cap
and the 1.5 GB total budget cap. A fresh checker on a one-line esnext+dom program holds a 7 MB arena (13k types): the
libraries aren't the floor. Plan 72's lines there: vscode load phase 389 ms (loader waited 6 ms, workers 59% busy),
declare + link 130 ms (5% of wall), webpack's 45 ms (15%: its serial declare, since fixed to 18 ms).

### Memory breakdown (2026-10-12, e043e9e2)
Before plans 60-80: vscode K1 5220 MB (AST 2.17 GB, binder ~0.6, checker arena 1.82 GB, LazyMemo 222 MB, intern
tables 202 MB). Today's per-corpus peaks are the bench tables above. The tuple fix (targets share element type
parameters) took type-fest's K1 arena 1906 -> 695 MB, types 4.78M -> 217k.

### Perf at breadth (2026-10-12)
Plan 59 stage 2 drained every project above 1.5x tsgo's types: types ratio median 0.76 -> 0.68, worst 16.4 -> 1.55
(date-fns docs, feathers typebox, nitro, rxjs, nuxt...); peak RSS ratio worst 2.48 -> 0.95; total types 96.0M ->
71.3M against tsgo's 127.9M. A types ratio above ~1.5 is a missing cache or a runaway.

### Perf session (2026-10-06 and 2026-10-07)
Compare relation cache sizes (`tttt --time --checkers 1`) with tsc's `--extendedDiagnostics`: vue's strict-subtype
228k pairs against tsc's 15k led to three tsc shortcuts (same-index members of union pairs, an intersection's key
property in subtype reduction, object-to-primitive) worth vue -13.5%, drizzle -17.5%. On 2026-10-07 the four
checkers redid ~24% of K1's type work on vscode; work volume at K1 was close to tsc's (vscode types 2.57M vs 2.89M,
assignable 960k vs 1008k). Sharing types between checkers was judged not a lever (plan 85's census confirmed it
2026-10-18).

### Smaller results worth remembering
- Checker work stealing (2026-10-17, plan 72): -5.2% summed wall on the sweep's 285 biggest, peaks flat.
- Gated runs (options/syntax diagnostics) parse only deferred bodies for syntax errors instead of a full restart
  (2026-10-17): gated sweep projects -25% wall.
- Plan 72 stage 1 (2026-10-16): the loader's serial work, vscode load T4 940 -> 805 ms, `newfstatat` 194k -> 43k.
  Load discovery (a pre-scan) stays unjustified: the loader waits 6-8 ms.
- Plan 62 (2026-10-13/14): output independent of K; tsgo's import-graph FENNEL partitioner duplicated 2-21% more
  types than contiguous runs. Below 64 KB of checked source more checkers buy nothing (one per 64 KB).
- Declaration emit's cost (plan 64, 2026-10-15): +1.3-4.4% over `--noEmit --declaration` at K1; maps add ~0-4%.
- Plan 67 stage 1 (2026-10-15): recycling at 256 MB took vscode K1 4190 -> 2575 MB for +2.4% wall.
- Parallel name resolution (plan 45, 2026-10-05): vscode resolve 593 -> 283 ms; declare and link stay serial.
- Refreshes (plan 77 and its successors): new TypeScript repos match >= 96% on day one, the checker generalizes;
  JavaScript (checkJs) is where new code finds bugs. The `-p` sweep only walked `tsconfig.json` until 2026-10-19.
- Instructions history: drizzle +50% in 2026-10-17 went unnoticed two sessions (an intersection target's properties
  not seeing the intersection as `this`: variance measurements related ever longer intersections). Investigate any
  flagged instrcount rise before committing.

### Plan 77, fresh repositories (2026-10-17)
How a refresh is run (repos.txt's "Plan 77" comment: GitHub topics, 300-3000 stars, the first 3 per topic with a root
lockfile): append rows to `test/sourcesweep/repos.txt`, `thirdparty/ensure-sources.sh NAMES`, `scripts/sourcesweep.py
--tsc-new`, `scripts/dtsdiff.py --tsc -f RE`, then both with `-f '^(names)(/|$)'`; record day one before any fix.
Plan 77's 28 repos: 195 / 203 projects on day one (96.1%), two checker root causes, 203 / 203 after.
