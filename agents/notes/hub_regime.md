# The hub regime: can the hub be built in parallel? (worker hub, 2026-10-29)

The question (vision.md "Direction", bun_breadth.md "2026-10-28"): on projects where one type graph is reached by
nearly every file, every checker rebuilds it and the parked shared graph saves CPU but not wall, because whoever builds
the hub builds it serially. Can the hub itself be built in parallel, and what would that need? Diagnosis only: numbers
and a verdict, nothing built but the probe.

**Short answer.** It depends on which hub. Two regimes, measured: (1) **hub-file clusters** (35 of the 42 projects
with K8/K1 work >= 2): a handful of aggregating files (an i18n catalog index, a ts-rest router, an mcp-sdk schema
index) reach a graph whose members are declared in many files; a shared store with files checked bottom-up builds it
in parallel for free (file-granular critical path x5.6 on openbot/i18n), which is what bun gets and what the parked
shared graph already gets (openbot/i18n check phase -51%). (2) **true hubs** (7 of 42: dokploy x4, openbio,
karakeep/trpc, portabase): most files reach the whole graph through one root, the file-level structure is one big
strongly connected component (drizzle schema files referencing each other), and only member-granular parallelism
(several threads resolving one declaration's members) helps: x8.7 critical path, x5.1 on 8 threads on dokploy. Neither
bun nor tttt does that; bun flattens on dokploy exactly where tttt does. The decision for the human is at the end.

## Tools
- `scripts/hubtrace.py trace PROJECT FILE` (a `-DTTTT_HUB_TRACE` build in /tmp/tttt-hubtrace; ty/checker.c
  `hub_trace_*`): the file checked alone in a fresh checker, every resolution (ty_resolving_push..pop) a node with its
  inclusive/self thread CPU and types, memo hits (declaration LazyType, property type, signature/function return) as
  uses edges. `tree` prints the heaviest resolutions, `dag` the critical path and list schedules at member granularity
  (every resolution a task, perfect claim/wait on a shared store) and at file granularity (nodes grouped by declaring
  file, strongly connected components collapsed: a shared store, each file's declarations built by whoever checks it).
- Validation of the uses edges (dokploy, types): a router's closure in the DAG against the router checked alone in a
  probe file (reduce.py copy): application 103k / 97k alone, domain 101k / 96k, docker 102k / 95k, ai 109k / 97k. The
  missed sharing (instantiation, relation, variance caches) is small there; the closure slightly over-counts.
- Limits: a node's *self* (relations, instantiations, members, call resolution inside one resolution) can't be split
  at this granularity. zod's files are 65-70% self, so its shape is unknown below statement level.
- Cold per-file costs: `TTTT_FILE_TIMES=1 --checker-memory 0 TTTT_CHECK_ONLY=a,b,...` (a fresh checker per file).
- Box: 16 vCPU / 8 cores, shared with two workers; fast/tttt at e0281d1f4 + the probe (no change to the normal build);
  bun 1.4.4-canary.20261027.1. Walls are best of 3 interleaved at load ~5 (scaling) and ~10-15 (shared graph A/B).

## Per project (K1 = `--threads 1 --checkers 1`; walls in s, check phases in ms)
| | dokploy/apps/dokploy | monkeytype/backend | zod (corpus, tsconfig.test) | openbot/packages/i18n |
|---|---|---|---|---|
| tttt K1 wall / check | 1.807 / 1285 | 0.310 / 198 | 0.589 / 516 | 0.423 / 353 |
| tttt defaults (K8) wall / check | 0.686 / 536 | 0.179 / 153 | 0.138 / 119 | 0.316 / 296 |
| bun check phase, threads 1 / 8 / 16 | 1882 / 545 / 532 | 467 / 206 / 220 | 902 / 147 / 128 | 996 / 443 / 349 |
| bun wall at 16 | 0.672 | 0.261 | 0.164 | 0.410 |
| the hub | tRPC `appRouter` (server/api/root.ts:66) through utils/api.ts; its core is the tRPC context: better-auth's `auth` (lib/auth.ts:555) over the drizzle schema and drizzle-zod schemas, ~95k types every router needs | the ts-rest contract (packages/contracts/src/index.ts:19, 97k types, 99 ms alone), reached by routes/index.ts and the controllers | none: zod's generic core reached by every test file through members and instantiations, no root declaration | the `Messages` type of en (messages/en/index.ts:63, 66k types) and each locale's catalog checked against it, reached by app-catalogs.ts, lazy-catalogs.ts and each locale's index.ts |
| the first file that reaches it, alone (fast/tttt CPU) | show-billing.tsx 310 ms, 169k types (39% of K1's 437k) | routes/index.ts 149 ms, 141k types (K1: 57 ms) | classic/schemas.ts 51 ms (60k types); to-json-schema.test.ts 42 | app-catalogs.ts 345 ms, 172k types (K1: 11 ms, all built by earlier files) |
| as a share of the K8 check phase | 58% | 97% | 43% | 100%+ |
| types every K8 checker makes (census, `--threads 1`) | 80% (1.84 GB arena, x4.14 types) | 21% (89% also another's; x3.22) | 23% (x1.64) | 47% (x4.19; literals and intersections) |
| cold cost of a sampled file: median / max | 146 / 316 ms (42% of files reach half the max) | 5.7 / 130 (4%) | 0.9 / 28 | 0.8 / 222 (5%) |
| member-granular critical path (traced ms) / list schedule on 8 | x8.7 (51 of 445) / x5.1 | x3.5 (67 of 235) / x2.8 | x1.4-1.8 (self-bound) | x7.5 (58 of 432) / x4.9 |
| file-granular critical path / on 8 | x1.4 (a 100-file component: 50 schema, 22 services, 16 routers) / x1.3 | x1.0 (a 47-file component of controllers) | - | x5.6 (842 files, no cycle) / x4.4 |
| parked shared graph, K8 check (`TTTT_SHARED_GRAPH=1`) | 543 -> 638 (+17%); with a program-order round-robin deal 727 -> 641 (-12%, vs the shared graph's own deal -19%) | 152 -> 145 | 122 -> 157 (+29%) | 301 -> 148 (-51%) |

Shapes, in words:
- **dokploy**: a short thin chain (api.ts -> appRouter -> createTRPCRouter -> the context's return type -> auth's
  config, ~10 nodes of ~0 self) above a wide fan: auth's config has 536 children, appRouter 841 (the ~45 routers, 1-9k
  types each on top of the core). The critical path is the chain plus the costliest leaf: drizzle-zod's first
  `createInsertSchema(domains, ...)` (schema/domain.ts:77, 43 ms of self over 20574 tiny children: instantiation work
  no resolution boundary splits) and auth.ts:66's 18 ms. Each router alone needs ~55% of the hub (the core). With all
  8 checkers on services files at once under the shared graph's round-robin deal, all 8 still build the core
  concurrently (8 files of ~200 ms each, done within 4 ms of each other): duplicate, don't wait, entered at the same
  place.
- **monkeytype/backend**: the critical path is the file's own check (33 ms self), `initServer().router(contract, {...})`
  (routes/index.ts:48, 12 ms self) and the users contract (13 ms self): relation and inference work inside single
  resolutions. Member parallelism buys x3.5, files none.
- **openbot/i18n**: a fan of locale catalogs over the en `Messages` type, each locale file its own member: the textbook
  case for file-granular sharing, measured.
- **zod**: not hub-bound. The costliest cold file is 51 ms against a 119 ms K8 check; its problem is duplication CPU
  (the shared graph's premium makes sharing a loss there).

## At breadth: the 42 projects with K8/K1 work >= 2 (of bun_breadth's 300)
One cold run per project (`--checker-memory 0`, a fresh checker per file) over the 10 files with the most types at K8
plus 40 random ones, next to K1 and K8 check phases (fast/tttt, under load; per-file times are thread CPU):
- **The costliest cold file is a median 53% of the K8 check phase** (p10 30%, p90 78%): the per-checker model's floor
  and, without parallel construction, the shared graph's.
- **True hubs, 7** (>= 30% of the sampled files cost at least half the costliest): dokploy apps/dokploy, apps/api,
  packages/server, apps/schedules (median file 22-146 ms cold), openbio, karakeep/packages/trpc, portabase.
- **Hub-file clusters, 35**: median file cold 0.2-25 ms, the costliest is one of a few aggregating files
  (app-catalogs.ts, routes index.ts, mcp-sdk's specTypeSchema.ts in 6 projects, immutable.d.ts, EntityManager.ts...).
- bun's scaling on them (bunsweep-300, a loaded box, older build): true hubs bun ST/MT 2.04 against tttt 1.96;
  clusters bun 2.88 against tttt 2.34. **bun scales like tttt on true hubs and better on clusters.**
- A model of the summed K8 check phase over the 42 (9650 ms measured; "shared" = the rest spread over 8 checkers with
  no duplication, the shared graph's per-instruction premium ignored): hub built serially by whoever reaches it first
  6756 (-30%), the hub at x3 3603 (-63%), at x5 2973 (-69%).

## What bun does (notes/bun_concurrency.md, from its source; measured here)
bun has one shared store, tasks of consecutive files in program order, and **duplicate, don't wait**: a task that finds
a result missing computes it itself; record-keyed results are written immediately when nothing they depend on awaits a
commit, the rest at per-file commits or every 1024 keys at an idle moment; first entry wins. Only variance measurement
claims and waits. So two threads never split one declaration's members cooperatively; they race through the same
graph, and the hub gets built in parallel only when they enter it at different members. Measured: on i18n bun's check
keeps improving to 16 threads (996 -> 443 -> 349 ms; its tasks on the locale files build the members, the catalog file
finds them published), on monkeytype/backend it scales 2.3x against tttt's 1.3x; on dokploy it flattens from 8 to 16
threads (545 -> 532 ms), within 1% of tttt's 534: every task enters through the same root (the client files through
appRouter, the services files through drizzle's `db`), so they duplicate in the same order. The human's laptop
observation (bun >10x its single thread) doesn't reproduce on these four here: bun's check-phase scaling is 3.5x, 2.3x,
7.0x and 2.9x, and tttt's defaults are at or under bun's wall on all four (dokploy 0.686 against 0.672 s).

## Verdict (a decision for the human)
1. **Member-granular parallel construction, for the true hubs: don't build now.** It's the only thing that lowers the
   wall there (dokploy's check phase 534 -> ~185 ms modelled, about -50% wall; 7 of 300 projects). What it would need:
   the shared store first (the parked graph: clean-only publication keeps output K-independent), then speculative
   helpers at a fan node (an object literal's properties, a namespace import's exports, a router record): idle
   checkers resolve the siblings in another order and publish clean results, the requesting checker never waits and
   computes whatever isn't published yet. Waiting is what the deleted lock/wait/redo design died of (cycles a redo must
   break); helpers that only publish clean values keep tsc's per-checker resolution stack and circularity semantics.
   Costs: the shared graph's 1.2-1.6x per-instruction premium on the whole run (the blocker already), races lost to
   the requester are wasted CPU, and the bound falls where a fan's cost sits in one node's self (drizzle-zod's 43 ms
   instantiation, zod's 70% self). Weeks of work for 2% of projects, behind the premium.
2. **The hub-file clusters need nothing new but sharing without its premium.** File-granular sharing builds their hub
   in parallel (i18n -51% measured with the parked graph). So the hub question reduces to the shared graph's
   question, which is closed on its premium (zod +29%, dokploy +17% here; the laptop's 4-11% IPC gap). Reopening it
   means bun's layout (dense id-indexed tables, per-task id ranges, batched commits: bun_concurrency.md 6.3), not a
   hub mechanism.
3. **What to measure next, cheap, before any build**: (a) the parked shared graph with the program-order round-robin
   deal (`TTTT_DUP_DEAL` of i % K, `TTTT_NO_COSTLIEST_FIRST=1`) at K8 over the 42 and the corpora: dokploy -12%, i18n
   is already -51% with the default deal; if the clusters win at breadth and the rest lose only the premium, the
   premium is the one number to buy down. (b) The premium on the laptop's counters at these projects. (c) Only if
   both are good: a speculative-helper prototype on dokploy (start the K checkers on the router and schema files in
   reverse program order, the client files after), against the x5.1 bound.

## The shared graph with a program-order deal, at breadth (2026-10-29)
`TTTT_SHARED_GRAPH=1 TTTT_DUP_DEAL=<i % 8> TTTT_NO_COSTLIEST_FIRST=1` ("sg-rr") against plain defaults, both
`--threads 8 --checkers 8`, fast/tttt at f936fad40, best of 3 interleaved, on the 42 hub projects and the 15 `-p`
corpora. Load 11-28 (two other workers): walls are noisy (a plain dokploy/apps/api run took 1.06 s in one pass and
0.32 s in another), so the CPU of the check phase is the steadier number. A second pass added the deal alone
(plain-rr) and the shared graph with its own deal (sg-def) to split the losses.

| set | sg-rr wins wall > 5% | loses > 5% | median wall | median check CPU | median peak RSS | second pass, median check CPU: sg-rr / plain-rr / sg-def |
|---|---|---|---|---|---|---|
| 41 hub projects | 20 | 10 | 0.96 | 0.95 | 0.69 | 0.92 / 1.03 / 0.94 |
| 13 corpora | 1 (excalidraw -10%) | 11 | 1.22 | 1.15 | 0.87 | 1.17 / 1.06 / 1.15 |

- Wins (sg-rr, wall): openbot i18n -49%, team-client -35%, monkeytype/frontend -21%, openbot auth-api -19%,
  monkeytype/frontend/__tests__ -18%, openbot mobile -16%, folo mobile -15%, karakeep trpc -14%, openbio -13%,
  dokploy schedules -12%, karakeep api, immutable-js tests, monkeytype/backend -11%, dokploy/apps/dokploy -10%; the
  rest -5..-9%. Losses: mikro-orm seeder +36%, nest platform-fastify test +26%, vite __tests_dts__ +24%, five small
  mcp-sdk projects +6..+12% (70-100 ms runs), nest microservices +9%. Corpora: xstate +76%, react +34%,
  tanstack-router +26%, effect +25%, type-fest +24%, zod-root +24%, valibot +22%, vscode +17%, tsc +8%, zod +8%,
  drizzle +6%.
- **The losses are the premium, not the deal**: on the corpora the shared graph costs +15% CPU with either deal
  (sg-def 1.15, sg-rr 1.17) while the deal alone costs +6%; on the hubs the deal alone is neutral (1.03) and the
  shared graph saves 6-8% CPU with either deal. The program-order deal adds little over the shared graph's own on most
  hubs (medians 0.92 against 0.94); where it matters it is the dokploy family (dokploy -7..-10% against sg-def -1%).
- Memory: the shared graph's peak RSS is a median -31% on the hubs (dokploy 2461 -> 1428 MB, i18n 1028 -> 467).
- **Not wins**: webpack (-73%), vue (-74%) and better-drizzle are runs where plain does the one-checker rerun (TS2502
  / limits: notes/one_checker_rerun.md) and the shared graph doesn't; they are excluded above. Output differs from
  plain on better-drizzle (2 of 3 runs: it needed the rerun) and **openbot/apps/auth-api (3 of 3)**, a residue not in
  sharing_design.md §4's list (with the program-order deal only: 75 spurious lines, TS2345 in
  scripts/cloud-authorization-e2e.ts, an Effect's error channel `unknown` where plain infers `never`; clean under the
  shared graph's own deal in the one run checked); every other project was byte-identical in all runs. (2026-10-29, worker sg: auth-api's was a private-name race,
  fixed; better-drizzle's "plain too" was TTTT_DUP_DEAL panicking in plain's one-checker rerun, and the flag's own
  difference its missing rerun, both fixed: agents/bugs/shared_graph.md)
- The premium's attribution (2026-10-29, notes/shared_graph.md "The premium, attributed"): not coherence. At K8 the
  corpora's +12-19% is about half machinery instructions (+13-15% of plain's) and half cache misses of reading one
  shared graph (x1.08-1.24 CPU per instruction already at `--threads 1 --checkers 8`), with variance re-measurement next
  and cross-core effects last (x0.99-1.12); dokploy's x1.6 at T8 is races (564-584k words computed by a loser).
- Verdict: file-granular sharing wins on about half the hub projects (by 5-49%, the i18n-style clusters most) and
  loses on 11 of 13 corpora by the premium (+15% CPU median). It would pay only with the premium bought down, as
  the first half of this note concluded; a per-project switch is a heuristic the human has ruled out.

## The lead's erosion with threads (2026-10-29, the decisive measurement)
The human's framing: tttt is ~2x faster than bun single-threaded and much less so with threads. Both sides at
N = 1, 2, 4, 8, 16 (tttt `--threads N --checkers N`, bun `--threads=N`), best of 3 interleaved, fast/tttt at
f936fad40, bun 1.4.4-canary.20261027.1, `TTTT_FILE_TIMES=1` on every tttt run; the 42 hub projects (load median 16)
and the 15 corpora (load median 9). Medians over the projects without a one-checker rerun (vue, webpack and
better-drizzle rerun at K1 and erode 2.7-4.1x for that reason alone). "types K/K1": types made summed over the
checkers' files against K1's (duplication); "file CPU": the same for the files' check CPU; busy: the checkers' summed
spans over K x the check phase (1 - idle).

| | N1 | N2 | N4 | N8 | N16 |
|---|---|---|---|---|---|
| hubs (41): wall lead tttt / bun | 0.63 | 0.60 | 0.69 | 0.71 | **0.97** |
| check-phase lead (tttt typecheck / bun check) | 0.54 | 0.57 | 0.63 | 0.69 | **0.93** |
| front-end lead (tttt's four front phases / bun's load) | 1.06 | 0.94 | 1.01 | 1.03 | 1.13 |
| tttt types K/K1 / file CPU K/K1 | 1.00 / 1.00 | 1.47 / 1.35 | 2.23 / 1.96 | 3.25 / 3.00 | **4.76 / 4.98** |
| process CPU N/1: tttt / bun | 1 / 1 | 1.28 / 1.20 | 1.72 / 1.45 | 2.46 / 1.85 | **3.80 / 2.43** |
| tttt busy | 1.00 | 0.99 | 0.95 | 0.85 | 0.77 |
| peak RSS tttt / bun | 1.42 | 1.57 | 1.79 | 2.11 | 2.43 |
| corpora (13): wall lead | 0.63 | 0.63 | 0.67 | 0.68 | 0.67 |
| check-phase lead | 0.57 | 0.60 | 0.64 | 0.67 | 0.68 |
| tttt types K/K1 / file CPU K/K1 | 1 / 1 | 1.17 / 1.06 | 1.45 / 1.22 | 1.91 / 1.58 | 2.49 / 2.30 |
| process CPU N/1: tttt / bun | 1 / 1 | 1.08 / 1.05 | 1.22 / 1.15 | 1.55 / 1.46 | 2.19 / 1.90 |

The lead erodes from N = 1 to 16 on 35 of the 42 hub projects (median x1.42 of the ratio, p90 x1.97; tttt behind bun
at 16 on 18 of them, by up to 49% without the rerun: vite __tests_dts__ 1.49, inbox-zero 1.47, dokploy/apps/dokploy
1.44, dokploy api, schedules, server 1.25-1.28, openscience cli 1.26; better-drizzle 1.94 by its rerun) and on 6 of the 15 corpora (zod-root x1.81, zod x1.41, drizzle x1.31,
vscode x1.14, three by the rerun); on the other corpora it holds or grows (effect 0.57 -> 0.43, type-fest 0.26 ->
0.20, excalidraw, react, nest). On this 8-core box the erosion is entirely past the physical cores on the corpora and
mostly from N = 8 to 16 on the hubs; the laptop's 24 threads would extend the N8 -> N16 trend.

Verdict per cause:
- **Duplicated work grows linearly with K, and it is the erosion.** Hub projects make 1.47 / 2.23 / 3.25 / 4.76x K1's
  types at K = 2 / 4 / 8 / 16 (about 1 + 0.25 K), and the files' check CPU follows (4.98x at 16); bun's whole-process
  CPU grows 2.43x over the same range, so tttt's CPU grows 1.56x more than bun's: that alone accounts for most of the
  check-phase lead going 0.54 -> 0.93 (x1.72). The corpora duplicate half as much (2.49x types at 16) and their lead
  barely moves. The per-checker warm-up is paid K times; bun's shared store pays it about once (bun duplicates too,
  "duplicate, don't wait", hence its 2.43x).
- **The front end is not it.** tttt's front phases scale like bun's load at every N (lead 0.94-1.13, a serial share of
  15-18% of the wall at 16 on both sides): tttt has no lead there to lose, and none is lost. All of tttt's 2x is in
  the check phase.
- **Memory bandwidth / K arenas: no per-instruction slowdown visible at this resolution.** File CPU per type made at
  K16 is within 5% of K1's on both sets (1.05 hubs, 0.92 corpora), though tttt's peak RSS grows to 2.4x bun's on the
  hubs. (Thread CPU under SMT and load can't separate instruction counts; the laptop's counters would, and the
  per-file type counter wraps on recycled checkers, so 3 projects were left out of that ratio.)
- **The idle tail: secondary.** Busy falls to 0.77 at 16 on both sets (0.85 at 8): the costliest file and the
  K-times warm-up landing in each checker's first file. It costs ~1.3x at 16 on both sets, so it erodes the hubs no
  more than the corpora; bun's own idle isn't visible from outside.

**So the redesign question has its answer in one number: tttt's per-checker duplication, ~1 + 0.25 K on hub projects
(~1 + 0.1 K on the corpora), is what eats the single-thread lead.** Only a shared store removes it (the parked graph
cuts the hubs' CPU 6-8% at K8 and their RSS 31%, but pays a +15% per-instruction premium elsewhere, above); building the
hub in parallel (the first half of this note) is the second step and matters only for the 7 true hubs.
