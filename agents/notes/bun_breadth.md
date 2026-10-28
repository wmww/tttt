# tttt vs bun at breadth

## 2026-10-28 (worker perf): each side's own scaling, and where tttt's goes
`scripts/bunsweep.py` (the driver this note asked for): the 300 source-sweep projects tsgo checks slowest, four runs
interleaved per round (tttt defaults = K8 on this 16-vCPU / 8-core box, tttt `--threads 1 --checkers 1`, bun defaults,
bun `--threads=1`), 3 rounds, the best of each; tttt fast/tttt at 5137f5cc7 (before the rerun's early stop), bun
1.4.4-canary.20261027.1. The box was shared (load 5-23, median 10): walls are relative; CPU sums and the serial
`--threads 1` runs carry the structure. (A first run named projects through the worktree's symlinked
thirdparty/sources: workspace packages then load twice on both sides, arktype 0.2 s -> 54 s; redone with physical
paths. A median of 3 was moved by stalled rounds of other workers' jobs, pinia/packages/nuxt 0.40x; best-of-3 since.)

**Headline.** tttt ST / MT median 3.16 (p10 1.74, min 0.81): no faster than ST on 7 projects, all reruns; under
1.5x on 23. bun ST / MT median 3.33 (p10 1.80). tttt MT / bun MT median 0.76, bun faster on 65 of 300 (by > 25% on 26);
tttt ST / bun ST median 0.70. So tttt scales about as well as bun everywhere (in every regime below, bun's median
scaling is within 0.2 of tttt's) and wins on its faster single thread; the losers are where tttt scales worse
than bun or bun's single thread is unusually fast.

**Where tttt's speed-up goes** (ms of MT wall over ST / 8 cores, summed over the 300: 51.9 s of 66.6 s of MT wall):
- **The check phase** (median 3.3x over ST): typecheck CPU MT / ST 1.66 = **duplication 1.22** (K8 / K1 work at
  `--threads 1`, no contention: median 1.22, p90 2.24, >= 2 on 41 projects) x **per-instruction slowdown ~1.36** (SMT
  siblings, the materialize helpers, other workers' load: unmeasurable here, the laptop's counters must split it), at
  **6.3 of 8 cores busy** (the idle tail behind the costliest file). bun's CPU MT / ST is 1.90 at 7.3 busy: the same
  shape, more cores.
- **The costliest file** bounds the per-file model: at K8 it exceeds K1 / 8 on 188 of 300 projects; the bound K1 /
  max(K1 / 8, costliest file) has median 6.4, p10 2.7; < 4 on 68 projects, < 2 on 21. Idle tail summed 12.8 s.
  The costliest file is mostly the same at K1 and K8 (199 of 300) and costs about the same (median 1.04x): it is a
  heavy file, not a warm-up the deal created. Often a checked declaration file: immutable.d.ts (300-400 ms; the
  critical path of 7 of the 300: lucide/packages/solid, vitest/test/browser, vitest/examples/lit, angular-components,
  ng-bootstrap, quill, immutable-js), solid-js's store.d.ts, @types/react, @babel/types, typescript-eslint's
  helpers.d.ts; or a type-level module (solid's store.ts, telegraf's composer.ts, qwik's jsx-generated.ts).
- **Duplication, two regimes** (`TTTT_DUP_CENSUS=1 --threads 1 --checkers 8`, types summed / distinct, the share of a
  checker's types every checker makes): *spread* (vscode 1.39x / 6%, effect 1.40x / 5%, kebo 1.15x / 1%, react-select
  1.46x / 0.3%, drizzle 2.16x / 5%: pairs and clusters, notes/duplication.md) and *hub* (dokploy 4.13x / 80%, openbot
  i18n 4.19x / 47%, monkeytype backend 3.22x / 21%, bulletproof-react 2.07x / 31%, zod 1.65x / 24%): one type graph
  nearly every file reaches (a tRPC AppRouter, an i18n key union, a schema library's core), rebuilt by every checker,
  mostly anonymous object types, `.d.ts` references, unions and intersections. Duplication's summed wall share 9.2 s.
  41 projects have K8 / K1 >= 2; their median scaling is 1.94 for tttt and 2.09 for bun: **bun scales as badly on
  hubs** (dokploy 1.95x), because the hub is a serial chain whoever builds it. The parked shared graph on dokploy:
  CPU -10..-30%, peak RSS -40%, wall unchanged (all 8 checkers start on hub files at once, none has published). What
  would lower the wall is building the hub itself in parallel (its routers on several threads), which is neither
  model's today.
- **The front end** (9.5 s summed over ST / 8): front / bun's load median 1.15 (1.55 on 2026-10-23), slower on 230;
  cores busy 5.9 median over the whole front end. load+parse at 4.1 cores: the loader waits on a worker's parse 51%
  of the phase (p90 85%: the import graph's depth), workers are on a CPU 31% of the time they hold a file (here the
  run queue of a loaded box; notes/front_end.md has the quiet-box reasons). resolve at 2.7 cores: main thread on a
  CPU 69% (declare merge, link, the walk's serial files). One fixed: a 2.6 MB barrel `.d.ts` with ~1900 namespaces
  re-exporting (lucide-react's dynamic.d.ts, in 34 of the source repos) scanned the file's statements per name,
  64 ms on one declare thread (ff16b7a48).
- **The rerun** (9 projects, 4.0 s): every project with MT <= ST. The parallel pass is 1.0 s of it, which the early
  stop (15a98e4b7) removes; the rest is a K1 check (notes/one_checker_rerun.md "Fresh eyes").

**Verdicts, the worst scalers** (tttt ST/MT, bun ST/MT):

| project | tttt | bun | dominant | mechanism | fixable | generality | gain on the sweep if removed |
|---|---|---|---|---|---|---|---|
| crawlee/website/versioned_docs x3 | 0.81-0.91 | 2.5-2.8 | rerun | TS5115 shown, the parallel pass then a K1 check | parallel pass: fixed (early stop); K1 check: only by order-independent limits or tsgo-default's oracle | 9 / 300 rerun (32 / 5166 source-sweep configs) | 1.0 s parallel passes (1.5% of MT wall); the K1 checks' excess over K1/8 ~3 s more |
| better-drizzle | 0.81 | 2.02 | rerun | TS2502 foreign circularity | as above | as above | as above |
| sunmao-ui | 0.90 | 1.70 | rerun | TS2590 | as above | as above | as above |
| hop-protocol | 1.00 | 3.49 | rerun | TS7023 implicit any across files | as above | as above | as above |
| monkeytype/backend | 0.96 | 1.59 | dup + tail | hub (types 3.2x; routes/index.ts 244 ms at K8 vs 71 at K1) | not in the per-checker model; sharing doesn't lower the serial hub | 41 / 300 with K8/K1 >= 2 | dup's wall share 9.2 s (14%), of which the hubs' serial part stays |
| dokploy/apps/dokploy | 1.03 (2x on quiet runs) | 1.95 | dup | tRPC AppRouter ~500 ms per checker, 80% of each checker's types in every checker | as above; shared graph: CPU and RSS, not wall | as above | as above |
| immutable-js/type-definitions | 0.99 | 0.99 | one file | immutable.d.ts 380 ms: base-interface compatibility through generic signature inference (relate.c, infer_from; tsgo 3.4 s, bun 0.19 s) | per-file model: no; bun's 2x on this file is a lead (unread) | immutable.d.ts critical in 7; 21 projects bounded < 2x by one file, 68 < 4x | idle tail 12.8 s (19%) |
| telegraf | 1.18 | 1.10 | one file | composer.ts 545-645 ms | no (bun the same) | as above | as above |
| openbot/packages/i18n | 1.28 | 2.55 | dup | hub of literal unions / intersections (types 4.2x, 47% in every checker) | as dokploy | as above | as above |

What next, in order: (1) the laptop's counters on the check phase's 1.36x per-instruction slowdown (the one bucket not
explained by mechanism here); (2) the hub regime is the input for the redesign question: sharing helps its CPU and
memory, its wall only if the hub's own construction parallelizes; (3) what bun does to check immutable.d.ts 2x
faster than tttt (and 17x faster than tsgo); (4) the front end's loader waits (import-graph depth: an import
pre-scan was bounded at ~15 ms on vscode, notes/front_end.md).

## 2026-10-23 (worker "busy")
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
- **(f) slow shapes**: single files tttt checked 2-8x slower than bun (base-ui's mergeProps.ts 177 against 21 ms,
  react-intl's components, react-select's examples, NavigationHeader.tsx). **One mechanism, fixed (wt-reactfile)**: a
  call through a union of React's per-element functions (`ComponentPropsWithRef<ElementType>` getters) has an
  intersection of ~187 prop interfaces (~290 properties each) as its parameter, and reducing it
  (never_reduced_prop_known()) looked every property up in every later member, ~9M binary searches. Now grouped by name
  first (never_reduced_prop_grouped()), tsc's linear getPropertiesOfUnionOrIntersectionType. K1 CPU: mergeProps.ts
  252 -> ~0 ms (in a cut-down project: typecheck 300 -> 12 ms, tsgo's check 148 ms), CustomLoadingIndicator.tsx 236
  -> 2.5, NavigationHeader.tsx 321 -> 41, react-intl's plural.tsx 405 -> ~7. 12 source sweep projects have the
  187-member intersection (base-ui x2, react-select x2, formatjs/docs, folo x2, tiptap/demos, react-navigation,
  lexical-devtools, hop-protocol, sunmao-ui). `scripts/scaleprobe.py -k callunion` is the synthetic shape.
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

## How to redo it (now: scripts/bunsweep.py --bun PATH; the steps below are what it does)
1. bun: `npm pack @oven/bun-linux-x64@1.4.4-canary.20261022.1` into scratch, `package/bin/bun`.
2. Sample: the 500 projects with the largest tsgo `check_s` in `test/perfsweep/tsgo.jsonl.gz`.
3. Per project: bun once (its first run is slow), then 3x (tttt `--time` with TTTT_SYNC_EXIT=1, bun `check -p P
   --noEmit`), from an empty directory, an incremental project's build info into it; medians. ~45 min.
4. The split: `bun check --timing` prints "N files loaded in X ms, M checked in Y ms"; compare X with tttt's phases
   before typecheck and Y with typecheck.
5. `scripts/perfshape.sh -p P --bun B` on the worst; `bun check --threads 1 --timing -p P FILE` for one file's cost.
