# tttt vs bun at breadth (2026-10-23, worker "busy")

tttt at its defaults (fast/tttt at d98cddfbb) against bun canary 1.4.4-canary.20261022.1 at its defaults on the 500
source sweep projects tsgo checks slowest (`test/perfsweep/tsgo.jsonl.gz`, check_s >= 0.58 s; 487 measured, the
corpus-named entries skipped). Per project bun once to warm, then 3 interleaved runs of each, medians. The 16-vCPU
box was loaded all along (load 5-46 during the sweep, median 26; 50-90 during the A/B): walls are relative, and a
16-thread bun gets a bigger share of a loaded box than tttt's 8 checkers. The driver, the analysis and the per-project
JSON were in /tmp/busy-sweep (gone; ~150 lines, easy to redo from this description).

## The answer
- **bun wins on wall on 116 of 487 projects (24%)**, by >10% on 70. Wall ratio tttt/bun: median 0.82, p10 0.51, p90
  1.16. CPU tttt/bun: median 0.65 everywhere, 0.78 on the losers; tttt uses more CPU than bun on 13 losers (all of them
  type-heavy React or library code: formatjs/docs, base-ui, react-select, mantine, tiptap, arktype...).
- **Cores busy on the 116 losers**: tttt's typecheck phase median 4.2 cores (of its 8 checkers); bun's whole run
  (CPU/wall) median 4.2; tttt's whole run 2.9. tttt's front end (config, libs, load+parse, resolve names) is a median
  47% of a loser's wall and runs at **2.1 cores** (load+parse 2.4, resolve names 1.8). So tttt keeps cores as busy as
  bun while checking; the idle cores are in the front end and in single files that pin the check's critical path.
- **The front end is where bun is ahead nearly everywhere**: from `bun check --timing` (bun's "files loaded in" against
  tttt's config+libs+load+resolve, medians of 3): tttt's front end is slower than bun's load on **431 of 487**
  projects (median 1.55x), while tttt's typecheck is faster than bun's check on 429 (median 0.64x). bun's wall is its
  load plus its check (no overlap). Over the 116 losers the front-end deficit is the larger one on 86, the check
  deficit on 27 (3 lose on neither: noise). Summed over all 487: front-end deficits 28.4 s, check surpluses 78.8 s.

## The worst 15 (perfshape.sh medians of 3, then per-file times; class letters as in "How to continue")
| project | tttt/bun wall | where | class |
|---|---|---|---|
| formatjs/docs | 2.8 | K capped at 4 by a 2.2 MB `.d.ts` (fixed below); 4 react-intl components ~180 ms each, bun checks plural.tsx with its 42 imports in 87 ms | (g) cap, (f) |
| base-ui/test, /performance | 2.1-3.2 | mergeProps.ts 177 ms at K1, bun 21 ms for it with its 7 imports; useRenderElement.tsx 7 ms at K1, 177 at K8 (pays the same warm-up) | (f), (c) |
| react-select/docs, react-select | 1.3-1.7 | CustomLoadingIndicator.tsx 155 ms, bun 62 with 55 files; the top 3 files 160-220 ms each at K8 | (f), (c) |
| folo/apps/mobile | 1.8 | NavigationHeader.tsx 188-210 ms, bun 125 with 42 files; top 3 ~240 ms at K8 | (f), (c) |
| mantine-analytics-dashboard | 1.4 | top 3 files ~190 ms at K8, 65-68 ms at K1: a library warm-up every checker pays in its first file | (c) |
| kebo | 1.5 | transactions.routes.ts 633 ms at K8, 133 at K1 (bun 215 alone): the warm-up lands in the costliest file, 92% of the span; bun T1 is 1.9 s against tttt K1 1.2 s | (c) |
| tiptap/demos | 1.3 | ReactMarkViewRenderer.tsx 774-809 ms, bun 580 for it with 260 files | (a), (f) |
| solid/packages/solid | 1.1 | store.ts 417 ms (K1) to 750 (K8, cold), bun 353 | (a), (c) |
| vitest/examples/lit, orca-q | 1.2-1.4 | immutable.d.ts 260 ms, bun 196 | (a) |
| arktype | 1.0-1.3 | one file 34.5 s of a 34.8 s span; bun 34 s too | (a) |
| immutable-js/type-definitions | 1.0-1.3 | one file (immutable.d.ts), noise | (a) |
| nitro, slidev, faker, feathers, dbackup, inbox-zero, ethereumjs, vanilla-extract | 1.2-1.9 | front end 2-3x bun's load (nitro 409-480 ms against 119-161), typecheck 2-3x faster than bun's | (b) |

No loss is (d) (sys 10% of tttt's CPU on the losers, bun's 15%) or (e) (tttt K1 types / tsgo 0.6-0.95 on all 15).
None is "too few checkers": the typecheck-class losers have idle tails behind one file, so more checkers add CPU and
memory for nothing (the K policy stands, notes/duplication.md).

## What the classes mean
- **(b) the front end, 86 of 116 losers**: 3000-6700 files, mostly node_modules declarations. Under load the phases'
  cores busy are unreliable (faker's resolve took 1054 ms wall for 186 ms CPU at load 48), so what blocks the cores
  needs a quiet box or the laptop: the load pool's workers report busy ~2-3 s but on a CPU only ~0.2-0.4 s of it, and
  the resolve walk runs at ~1-2 cores. notes/front_end.md has the vscode timeline; these projects are a different
  shape (many small `.d.ts`, little checking) and have no timeline yet. This is engineering, not a policy.
- **(f) slow shapes**: single files tttt checks 2-8x slower than bun (base-ui's mergeProps.ts 177 against 21 ms,
  react-intl's components, react-select's examples, NavigationHeader.tsx): React/JSX-heavy props merging. A per-file
  cost, found by `TTTT_CHECK_ONLY=FILE TTTT_FILE_TIMES=1 --checkers 1` against `bun check --threads 1 --timing -p P
  FILE`. Not profiled yet: the next thing to do (callgrind on mergeProps.ts alone).
- **(c) warm-up in the critical path**: a file that pulls in a library's big type graph pays for it at K8 in every
  checker that meets it first (kebo 133 -> 633 ms, base-ui 7 -> 177); the costliest-first deal puts such files first,
  but any order pays it once per checker, and the file is indivisible. bun's shared store pays it about once. Only
  sharing removes it (notes/shared_graph.md).
- **(a) one file**: as tsc and bun.

## The fix built: the largest-file cap ignores declaration files
auto_checker_count() caps the checkers at checked bytes / the largest checked file's bytes when that file is >= 1 MB
(the critical-path floor argument, TypeScript6's checker.ts). A `.d.ts` that big checks in milliseconds (lucide-react
2.2 MB 2 ms, @octokit/openapi-types 4.8 MB 0-67 ms, against termdom's 1.2 MB dom.ts 615 ms), so the cap was idling
cores. It now counts implementation files only. K changes on 7 of the 487 projects (formatjs/docs 4 -> 8, cheerio
2 -> 8, octokit 2 -> 8, changed-files 4 -> 8, lexical-esm-astro-react 4 -> 8, camoufox-ts 5 -> 8, mermaid/e2e 6 -> 8)
and on none of the 17 corpora (TypeScript6 stays at 4). perfshape medians of 7 (base -> new): formatjs/docs wall
0.703 -> 0.624 s (span 579 -> 502 ms), cheerio 0.267 -> 0.187 (span 155 -> 80), octokit 0.156 -> 0.142 (span 86 ->
55), lexical's span pinned by one file both ways (106 -> 109 ms, walls overlap). CPU +4..17%, peak RSS +10..32%
(+15 to +48 MB). Small: it is the worst project's class, not the biggest.

## How to redo it
1. bun: `npm pack @oven/bun-linux-x64@1.4.4-canary.20261022.1` into scratch, `package/bin/bun`.
2. Sample: the 500 projects with the largest tsgo `check_s` in `test/perfsweep/tsgo.jsonl.gz`.
3. Per project: bun once (its first run is slow), then 3x (tttt `--time` with TTTT_SYNC_EXIT=1, bun `check -p P
   --noEmit`), from an empty directory, an incremental project's build info into it; medians. ~45 min.
4. The split: `bun check --timing` prints "N files loaded in X ms, M checked in Y ms"; compare X with tttt's phases
   before typecheck and Y with typecheck.
5. `scripts/perfshape.sh -p P --bun B` on the worst; `bun check --threads 1 --timing -p P FILE` for one file's cost.
