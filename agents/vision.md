# Vision

## Goal
A complete TypeScript typechecker in C, **distinctly faster than bun canary's `bun check`** (the fastest competitor:
measurements.md "The 12-core box at 3a77c827"), with typescript-go 7.1 (tsgo) as the oracle for every diagnostic,
type and emitted declaration. Restated by the human on 2026-10-22: speed first; memory is secondary until we are
faster; correctness is never traded. Completeness first in the sense that nothing is built on a foundation that can't
be fast or can't be complete. The suite's baselines are the first oracle, not the goal; real projects that are clean
under tsc are the second.

## State (2026-10-28 evening: three parallel tracks after the release; main at 8642f349c, 48 commits since 5137f5cc7)
- **Correctness**: 15 real-code fixes landed today (cor), each with a `test/cases` repro: every `[open]` npmsample flip with a
  named repository is fixed (react-doctor, reanimated, kirka, Questarr, freelens x3, ChatbotX, synergy x2, firecms,
  eva, akanjs written up), appflare's root project (two apps augmenting one interface through two re-export paths:
  tsc's clone chain and TS2717), effect's HttpApiEndpoint TS2322 (a declared type's non-augmenting base has no
  this-argument), four isEmptyAnonymousObjectType variants made one. `scripts/bugcount.py`: 298 open root causes (309
  this morning), 206 fixed. Batch oracles on the first 12 commits: suite 0 lost, corpora 0/0, DT 8595, `$ExpectType`
  68640, bun cases 1532 with 0 K-dependent, dts and perfsweep unchanged; the sampled sweep's two moved projects were
  appflare's old root divergence (fixed since), not a regression.
- **Cleanup** (ref): 24 commits, ~500 net lines removed, 9 newly passing tests, instructions within 0.03%: six
  semantics fixes that fell out of hack removals (tsgo 7's pragma parser, `no-default-lib`, the module test, JSDoc
  `unique symbol`, TS2417 on mixin bases, TS6133 on destructured private methods), dead functions/fields/helpers,
  and the huge-page machinery removed (below). hacks.md: 208 entries triaged, ~190 are designs or measured policies.
- **Perf** (perf; notes/bun_breadth.md "2026-10-28", notes/one_checker_rerun.md "Fresh eyes"): on the 300 slowest
  sweep projects, tttt ST/MT median 3.16 against bun's 3.33 on this 8-core box; tttt MT/bun MT 0.76. Where the
  speedup goes (51.9 s of loss over 300 projects): the idle tail behind one file 12.8 s, the front end 9.5 s,
  duplication 9.2 s (the "hub" regime: 41 projects where every checker rebuilds one type graph nearly every file
  reaches, K8 doing >= 2x K1's work), the rerun 4.0 s (its parallel passes now stop early: 15a98e4b7). The parked
  shared graph saves CPU on hubs but not wall: the hub is built serially by whoever builds it.
- **Pending the laptop**: the per-instruction slowdown at K8 (check CPU 1.66x ST of which duplication explains 1.22x)
  and footprint.md's PipelineL1 split. The physical-core checker cap stays until duplication is fixed (the human,
  2026-10-29: a tuning job made irrelevant by that work; its removal ad0215612 is in history, no A/B).

## State (2026-10-25: v1 released; full oracles at 5de65a46b, quick oracles at 45397f75f and 86bcd7ba4)
- **Released: tag `v1` = 438727981** (the human's choice, 2026-10-25: the tree they tested; the README updated in
  c7ad9918a). Main past the tag holds the memory refactor (45397f75f: huge pages off by default, the pool reads free
  memory now), perf3's four output-identical wins (2fd330892, 862617c2e, aac3864d6, b509189b3) and the docs, all
  gated clean (quick oracles at 86bcd7ba4: suite 15656, 0 lost, corpora 0/0): the next release's content.
- **Target: TypeScript 7.1.0-beta** (tag `v7.1-beta` = 3e923636, `thirdparty/typescript`); every cache is tsgo 7.1's.
- **The rc** is the commit this section lands in: main after the 2026-10-24/25 random-npm correctness sweep (three
  boxes, ~2000 small packages and ~500 large-repository sub-projects compared; 95.7% byte-identical with tsgo; every
  flip found was fixed but the depth-limit designs and the five filed last, below), the perf regressions since
  d22d93ae fixed (measurements.md: three per-file 4-12x blowups, the one-checker rerun's phantom triggers and its
  final trigger rule, `--no-rerun`), and the two machine-state dependences fixed (huge pages off by default, the
  memory pool reading free memory now: notes/kernel_time.md, notes/one_checker_rerun.md).
- **Known correctness bugs (`scripts/bugcount.py`): 309 root causes open (27 of them designs), 82 text-only, 38
  argued tsgo artifacts excluded.** The rise from 162 (10-23) is the sweeps' write-ups, most of them bun cases and
  missing checks from tsc's suite; the open real-world items are agents/bugs/npmsample*.md's `[open]` bullets (the
  five unreduced flips of large seeds 20261305/20261404: synergy x3, Questarr, eva, SparkyFitness's zod TS5115
  family; akash deploy-web's +58 (the re-entry area); akanjs's TS2344/TS2322; effect HttpApiEndpoint; artsy,
  rocket-slate, react-flow-chart, vue pluginScoped, paseo, starlight as [design]: tsgo's depth limit and
  resolution-order circularities).
- **Suite** (`scripts/passcount.py`): **15656 / 16816** configurations, 0 lost against 10-23, 159 gained today;
  `--no-defer-bodies` the same, `--checker-memory 0` 0 of 20542 differ.
- **Real code** (the full run at 5de65a46b): corpora 0 / 0 everywhere (vue's 0 / 2 is the design item), DT 8595 /
  8595, `$ExpectType` 68640 / 68640, source sweep quarter sample 1666 / 1671 (every miss known), `-b` sweep 420 /
  426 (4 missing, 2 known), bun cases 1532 / 2411 (63.5%, 0 crashes, K-dependent 0), flagflip 17 / 2 / 4, clidiff
  5130 / 5174 (text 34, diagnostics 10), dtsdiff 1497 / 1503, perfsweep median check ratio 0.20 against tsgo
  single-threaded, kdiff 1 / 5166 (rxjs's known elaboration text), recycle 1 / 5166 (rxjs).
- **Determinism**: output is byte-identical across thread and checker counts on every known case (the one-checker
  rerun fires on 29 / 5166 sweep configs; its rules and cost: notes/one_checker_rerun.md). `--no-rerun` trades that
  for speed on those projects.
- **Speed**: since d22d93ae, +1.3..1.7% instructions on every corpus (measurements.md: attributed; two of the four
  contributors already recovered), the per-file regressions back to their old times, big projects within noise of
  d22d93ae (vscode 4.07 s both at the defaults here). Huge pages are opt-in (`--huge-pages`, +1..5% without).

## Direction (2026-10-28; the items and their numbers are in todo.md)
- **Multithreading is the perf problem (the human, 2026-10-28)**: on the projects where tttt loses to bun by a lot,
  tttt single-threaded still beats bun single-threaded (often ~2x), while some of them have tttt at the defaults no
  faster than its K1 and bun's parallel run >10x its single-threaded one on the same machine. The answer is not a
  heuristic that disables threads: tttt's parallel run must beat its own K1 on every project by a wide margin, and
  on slow projects by a much wider one. Many strategies failed (sharing, deals, scheduling: "Decisions"); a major
  redesign may be needed; C is not the blocker. Slow projects are clues, not targets: every finding is stated as a
  mechanism that applies everywhere and checked at breadth before it counts. The 2026-10-28 accounting (above) says
  the hub regime is where the gap is; the open question is whether the hub itself can be built in parallel
  (notes/bun_breadth.md "2026-10-28", todo.md "Now").
- **After the release (the human, 2026-10-25)**: one correctness agent at a time, fixing the known real-world items
  (agents/bugs/npmsample*.md `[open]`, then sourcesweep_checker.md, then bun cases when the real-world list is
  empty); no finders (the remote box is gone; notes/npm_workflow.md "Remote finder box" recreates one), no perf work
  beyond the gate. The coordinator lands and reports as before.
The human's priorities: diagnostic presence matches tsgo everywhere (absolute), then speed (distinctly faster than
bun canary on real projects, judged on their private set), memory where cheap, exactness of text last; easy,
measured wins first; land fast and gate in batches.
0. **For the human**: the laptop asks (remaining kernel cycles, the exit tail under THP `always`, the bench on main);
   keep or revert the relation HACK's removal.
1. **The human's projects, correctness**: openclaw (13 configs time out, 4 K-dependent), opencode (its
   history-dependent alias re-entry below call barriers), osis (cache it in the sweep).
2. **Lazy parameter typing in signature resolution**, the structural item behind kebo's MST cycle, openclaw's TS2554
   and pothos's TS7023 (bugs/sourcesweep_checker.md, notes/reentrancy.md).
3. **bun's cases**: 1278 / 2411 match, 530 of 532 keys not written up; the largest buckets (-TS2322, -TS2509, -TS2339, +TS5114,
   +TS2842, +TS7022) and the 7 crashes first.
4. **The front end at breadth**: bun loads faster on 431 of 487 projects; the 86 front-end losers need a timeline on
   a quiet box (many small `.d.ts`, ~2 cores busy).
5. **Memory** (the pool is in; the next levers in notes/memory.md), kernel round three from the laptop's profile.
6. **Re-pin to 7.1.0 stable** when it ships.

## Decisions (each with its reason; the narrative is in git history of this file)
- **The shared graph's premium is attributed; no switch rescues it** (2026-10-29, worker premium; notes/shared_graph.md
  "The premium, attributed", hub_regime.md "The break-even curve" and the predictor section). The +15% CPU at K8 is
  not coherence: the flag's instructions are at or below plain's at K8 (it removes the duplication: types made K8/K1
  1.00-1.06 against plain's 1.4-4.8), and its cost is a cache miss per piece of shared state a checker first reads
  (half the shared-table probes, half reading types another checker made), present on one thread. Races add 1.6x on
  dokploy only. Cuts landed (shared hits cached privately, no probes for unpublished kinds): -1..-3%. The curve bends
  the right way with threads (hubs median wall 1.11/1.05/0.90 at N4/8/16; corpora 1.26/1.15/1.10, crossing only
  around 32-64 threads), no core-count threshold wins everywhere, and no import-graph predictor separates winners
  from losers (the type-level fan-in of an `as const` catalog isn't in the import graph). What is left: the laptop
  at its 24 threads (the curve's next point), fewer shared reads per unit of work (variance groups shared; the
  content order replaced the first-touch stamps at b2b3cdcd2, closing residue C), or accepting the flag as a per-project option.
- **The leaner type graph is a memory project, not the route to speed** (2026-10-29, the laptop at 2c389b213:
  measurements.md "The 12-core box at 2c389b213"): step 1 (instance props made on first use) cut zod's arena 20% and
  RSS 12% but cycles only 1.6%, because the bytes it saved were written once and never read: the stalls are random
  reads of the objects that are used. Step 2 is not started (its stop rule); steps 2-4 are re-estimated at 0 to -2%
  cycles for 9-14 days (type_graph_design.md §4) and would be gated on read misses, not arena bytes. The intern tables
  at 8 bytes a slot are kept (cycles -1..-6% on all five shapes, RSS -4..-13%). The threading speedup therefore goes
  through the shared graph's premium directly: attribute the +15% CPU to instructions, coherence or lost work, then
  cut the largest bucket (worker premium).
- **The threading speedup and the leaner type graph that enables it are the priority** (the human, 2026-10-29, after
  the laptop's counters: footprint.md "The laptop's counters"). Order: the shared-members design (notes/
  type_graph_design.md, in progress), its stages landed output-identical and measured on the laptop's `.d.ts`
  shapes (immutable.d.ts, store.d.ts: the whole single-threaded gap there is memory stalls; the corpora, where tttt
  already runs half bun's instructions and cycles, won't show much), the shared graph made correct at every K and
  deal (its auth-api residue first), then its premium re-measured and the default decided. Other tracks (bun cases,
  the loader) finish their current item and yield workers; the box is kept quiet for measurements when asked.
- **Keep the parked shared graph and the TTTT_TAINT prototype** (2026-10-28, the human) until tttt either uses the
  shared graph, is confident in a different multithreading strategy, or understands why it can't get multithreading
  gains near bun's. The long-term wish is a clean tree of only code and tooling in use, no disabled features; the
  removal inventory (3,282 + 858 lines, 260 + 103 hook sites, 1,031 lines of tooling) is in ref's 2026-10-28 report
  and todo.md.
- **No perf machinery below ~1% for its complexity, nor anything that only showed on the laptop** (2026-10-28, the
  human): the huge text page (-1.2% at the defaults on the laptop, the private set unmoved) and the whole opt-in
  `--huge-pages` path (the laptop at df95ec2f: faults -3.6x, wall and user cycles within noise, kernel 4-8% of user,
  zeroing fresh pages 2.4% of user and not avoided by huge pages) are removed (24820df46, 4f3fa414e). Kept: the
  memory pool reading free memory (the alternative is an OOM kill or an arbitrary cap), the AST's address-space
  reservations (structural: pointer ownership in one load, not a perf trick), MADV_POPULATE_WRITE pre-faulting. The
  physical-core checker cap is judged a hack by the human; its removal (ad0215612, in history) waits for the
  duplication fix that makes it moot rather than an A/B (2026-10-29). notes/portability.md lists what still depends on the box.
- **Structural work is good** (2026-10-24, the human, approving the "this-instantiated members" project after hono's
  TS2769 turned out to be that design gap): a fix that gives a structural solution instead of a point fix is welcome
  even when the triggering case isn't a flip, as long as it lands in gated stages (quickgate --vs-main incl. instrcount
  on the class-heavy corpora, threadcheck/checkerdiff, $ExpectType, DT sweep, a sampled source sweep) and stops when
  a stage opens up; agents/notes/this_instantiated_members.md is the design and progress log (det3).
- **The sweep is biased heavily toward big projects** (2026-10-24, the human: "its time to heavily bias towards big
  projects"). After ~1900 random npm projects (95.7% identical, 20 flips found and all fixed), small packages return
  mostly clean batches, while the large-repo refreshes showed 9 of 20 repos diverging. `scripts/npmsample.py sample
  --large` draws random large TypeScript repositories from GitHub search (reposearch.py's size buckets) and `setup
  --large` accepts up to a few workspace sub-projects per repo (installed once, sandboxed) with a minimum TypeScript
  file count; finders run that mode from now on (seeds 20261300+ here, 20261400+ the remote box); the npm registry
  sampling stays available for breadth.
- **The npm workflow is the only priority; races and thread-count dependence outrank even it** (2026-10-23 late, the
  human). Order: (1) any race or thread/checker-count dependence found anywhere (det3 owns them); (2) flip cases from
  the random-npm sweep (tsgo and tttt disagree on whether a project has ANY error), each a minimal `test/cases/npm_*`
  repro with `// origin:` and `// flip:` lines and a row in test/npmsample/repros.tsv, fixed at the root cause;
  (3) finding more (sampling, setup, compare, repros) while flips are scarce; clones are deleted as processed.
  Not worked until saturation: non-flip divergences, text differences, bun cases, the refresh sweeps, other work.
  Tooling only if it speeds the pipeline up significantly. Perf must not regress (quickgate --vs-main per fix,
  instrcount within +0.3%); landings are batched rather than immediate when that gives throughput. All untrusted
  code runs sandboxed (installsandbox.py) or is reviewed; the pipeline brief is the coordinator's npmflow.md.
- **Random repositories and all their dependencies are untrusted; installs run sandboxed and execute nothing of
  theirs** (2026-10-23, the human, after a review found two yarn-berry repos had run their own plugins/bundle during
  `npmsample.py` installs; both benign, host clean: agents/notes/install_security.md). Distinguish dependencies of
  known-good software (the pinned corpora) from literally random packages and the repos found by sampling and
  searching, which get the strict treatment: bwrap with an empty $HOME and a scrubbed env, pnpm `--ignore-pnpmfile`,
  yarn with YARN_IGNORE_PATH=1 and no plugins (a .yarnrc.yml with yarnPath/plugins/hooks is refused or rewritten:
  removing build steps is a valid alternative to reviewing them), pinned package-manager majors, forced registry;
  nothing from node_modules is ever executed (only our own tsc 6, tsgo and tttt run, in bwrap). `scripts/
  installsandbox.py` (worker npm) is the one implementation; ensure-sources.sh uses it too.
- **Random npm projects are the correctness measure; bun cases are deprioritized** (2026-10-23, the human): their
  method (agents/notes/npm_sampling.md: sample npm names with a recorded seed, clone the repos, bench config, tsgo
  7.1 against tttt) found 6 of 50 random projects diverging and 1 of 50 flipping clean/has-errors on that day's main,
  while the sweep repos showed nothing. `scripts/npmsample.py` runs it (worker npm); the divergences it files get
  fixers ahead of the remaining bun cases. Checked projects are NOT kept: a fix leaves a `test/cases` repro (or shows
  an existing test covers it), after which the project adds nothing; a matching project is deleted at once.
  What stays in git is the seed, the list of what was checked and the per-project counts.
- **Run-to-run and thread-count stability outrank the deal's load balance** (2026-10-23, the human: "if you discover
  any more race conditions or thread count-dependence I'd def work on that, as that will cause us a lot more trouble
  until its fixed (workers getting confused and such)"). bun 92-R16b showed that with work stealing an order-dependent
  diagnostic (a circularity on mutually recursive mapped types; also TS5115/TS2589, sequelize's TS7022) is a
  run-to-run flake when its two files split across checkers (16-19 of 60 runs at K4), while tsgo's static
  association is stable per K (though itself K-dependent: sequelize). A deterministic deal was prototyped and
  withdrawn the same evening (corH, agents/notes/deterministic_deal.md): no stealing costs +7..+21% wall at the
  defaults, fixed round-robin sequences +15..+48%, fresh-checker blocks +26% and +25% RSS; user time is unchanged,
  the loss is the idle tail, and bytes don't predict a file's cost. What is done instead: when a parallel run reports
  a history-sensitive diagnostic (an instantiation limit: TS2589/TS5114/TS5115/TS2590), the program is rechecked in
  one checker in program order and that output is used (det3): K-stable and identical to tsgo at K1, paid only by
  projects that already fail. The order-independence of the diagnostics themselves (reentrancy.md stages C-E) remains
  the real fix. Written up in
  bugs/tsgo_divergences.md.
- **Land fast, gate in batches** (2026-10-23, the human): a fix lands on `scripts/quickgate.sh` (the suite against
  the main it started from, three corpora, instructions against its base: `--vs-main`) through `scripts/land.sh`;
  the full oracles (`scripts/oracles.py`, the full source sweep, kdiff) run per batch of landings in the merger's
  tree, and a regression they find is bisected and reverted rather than every fix waiting ~20 minutes for them. Each
  batch saves `test/baseline_results.ts` and `scripts/bugcount.py --save` (the headline per landing).
- **TypeScript 7.0 compatibility is shelved** (2026-10-23, the human): a build-time 7.0.2 mode was sized at 20-30
  worker-days for everything (75 local, 15 medium, 3 structural changes, ~8 unemulable history-dependent behaviours)
  and a second oracle, baselines and gates to be trustworthy; on real code 7.0.2 and the beta differ on 0 / 1075 DT
  packages and 10 / 15 corpora are identical. tttt ships 7.1 semantics; notes/ts70_compat.md has the table and the
  cheap option if it reopens.
- **opencode is a correctness project only** (2026-10-23, the human): github.com/anomalyco/opencode (clean under tsgo
  7.1; 8-27 varying errors under tttt at the defaults, 10 at one checker, 12 with a fresh checker per file) joins the
  source sweep for correctness and is excluded from every perf measurement, like openclaw.
- **openclaw is a correctness project only** (2026-10-23, the human): github.com/openclaw/openclaw (clean under tsgo
  7.1, spurious and run-to-run varying errors under tttt at e6b76af2e) joins the source sweep for correctness and is
  excluded from every perf measurement (perfsweep, bench, instrcount, the bun breadth sweep); repos.txt marks it.
- **Diagnostic presence before exactness** (2026-10-23, the human): tttt must never report where tsgo doesn't nor
  stay silent where tsgo reports; that is absolute. Exact message text, elaboration chains, related info and printed
  types remain the goal, but where exactness would cost perf or great complexity it may be compromised and written up
  (bugs/display.md, errtextdiff's buckets) instead. Rank divergences: missing, spurious, wrong code or position, then
  text and type printing; report match rates separately for the (file, line, code) set and for byte-identical text.
- **No machine-specific tuning** (2026-10-23, the human): no pinning of work to kinds of cores, no `-march`, no
  per-microarchitecture rules; the shipped build's PGO and BOLT profiles are instrumentation counts of the training
  corpora (input-dependent, not machine-dependent) and the huge-page paths fall back at runtime. Defaults are derived
  from arguments that hold on any Linux x86 box (core counts, bytes, file sizes, RAM); the laptop confirms a
  mechanism, it does not suggest a rule. The hybrid-CPU placement experiment is withdrawn (todo.md).
- **Perf work is judged by mechanism or breadth, not by the bench** (2026-10-23): the human's private project set is
  the judge, and before the kernel round it moved 0.98 over a stretch of bench wins (BOLT -3.2% K1 wall, the huge
  text page -4.6%, the parser's -14%, the front-end round), which were fitted to the 15 bench corpora. The kernel
  round won >10% on most private projects because its argument was a mechanism (shootdowns and faults at the
  defaults, present in every multi-threaded run), not a corpus. A perf change lands on a mechanism argument or a
  breadth check (perfsweep, the bun breadth sweep, `scripts/perfshape.sh` reports the human can paste), and its
  claim is stated as "where else this applies".
- **The kernel and OS layer is a bottleneck this VM can't see** (2026-10-23): the shared 16-vCPU KVM box has THP
  `madvise`, other agents' shootdowns in `/proc/interrupts` and no perf; on the laptop (THP `always`, idle) the
  same changes cut faults -79..-96% where this box saw -8..-27%. Kernel cycles, faults, placement and anything
  cycles-level are measured on the laptop with `perf stat` / `perf record -e cycles:k`, and sys is compared with
  `TTTT_SYNC_EXIT=1` (the reaper hides the teardown's sys otherwise).
- **Sharing the type graph is closed** (2026-10-23; notes/sharing_design.md, measurements.md "The 12-core box at
  48cd9835"): bun's model is work, not incompatible (content nesting order makes the shared graph deterministic,
  160 / 161 loaded runs), but on the laptop at K12 it wins only on drizzle (-14% wall) and loses on zod (+2%),
  effect (+11%) and vue (+25%): the IPC gap of shared lines (plain against the best layout: drizzle 1.40 -> 1.24,
  zod -7%, effect -4%, vue -9%; 4-11%) outweighs the instructions saved everywhere but drizzle-shaped projects, and
  content order moves 3 of drizzle's elaboration chains away from tsc's text. Stages 1-3 are not built; the flag
  stays in the tree, parked. The per-checker model is at the hardware's ceiling for tsc's algorithm; the absolute
  wall is what matters and it leads bun.
- **A semantics commit lands only with a repro or the full source sweep, gated at several checker counts**
  (2026-10-23): the indexed-access recursion identity (tsc's rule, landed without a repro "needed only in combination
  with" the removed HACK) regressed youtube-enhancer, outside the quarter sample (1.16M types against tsgo's 211k, a
  spurious TS2589); the full run caught it and it was reverted (triage.md). c0a04350a (a generic mapped target no
  longer passing an intersection source by its members) was right at K1 and added a TS2502 to better-drizzle only at
  K>1 (kdiff caught it; cor7's acb8dfe7f fixed it): a relation change runs kdiff on the affected project at K1 and
  K8/16, not only the K1 gates.
- **Documented hacks are suspects when a divergence appears** (2026-10-23): two produced wrong answers in a week, the
  deep-nesting target push (pi-bolt) and the generic-mapped member fallback (better-drizzle's 4.23x types runaway
  behind tsc's TS2502). Read hacks.md for the code path before hypothesising.
- **The conditional_source_related HACK's removal is accepted at react +36%, type-fest +7% instructions**
  (2026-10-23, f38a8b493): tttt now does the relation work tsc does on react-hook-form's Path/PathImpl (tsgo 84.6k
  relations / 133k types on react, tttt 93.4k / 89.6k; the old 21.8k / 39.5k was the wrong answer's price) and
  pi-bolt's TypeBox chain is fixed. Price a hack's removal against tsc's work, not the hack's speed; a one-commit
  revert if the human decides otherwise.
- **No persistent thread pool for now** (2026-10-23, fe4): ~1 ms on nest for 1-2 days of work and a 58-thread-local
  audit; the design and the audit are in notes/front_end.md. **The loader not blocking on libs** was built (fe6,
  b6f8a4650, with a `TTTT_DEFER_LIBS=all` oracle against `TTTT_NO_DEFER_LIBS=1`): nest libs + load 41 -> 37 ms on
  this box, the laptop to confirm.
- **Directory listings in CachedFs landed at the +0.5% instruction bar** (2026-10-23; nest +0.45%, syscalls down by
  thousands): nil on the laptop's wall either way (bbe37c5f), kept.
- **The shared type graph is parked** (2026-10-21/22): built correct by construction and in the tree behind
  `TTTT_SHARED_GRAPH=1`, but at K12 each of its instructions costs 1.2-1.6x plain's, which outweighs the duplicated
  work it saves on ordinary projects, and its output still differs under load on effect (~1%). It wins 2.2-2.5x only
  on recycle thrashers (dokploy, monkeytype). The sharing study (above) answered the determinism and part of the
  premium and the laptop's counters closed it (notes/shared_graph.md). The earlier lock/wait/redo design was deleted, not narrowed: waiting
  creates the cycles a redo must break, and the redo's history is no sequential checker's.
- **The shared graph's ceiling is measured** (2026-10-22, notes/shared_graph.md "The ceiling, measured"): at most
  12% off a vscode checker at K24, below its own 1.2-1.6x per-instruction overhead: off the table for vscode; only
  drizzle-shaped projects (63%) would justify it. The perf-c2c measurement planned for it is withdrawn. The laptop
  measured drizzle at K12 with the flag: -26% instructions but only -9% cycles, wall 0.300 -> 0.273 s.
- **Cross-checker duplication is mostly inherent; no scheduler** (2026-10-22/23, notes/duplication.md): check-phase
  instructions K12/K1 vscode 1.09x (K24 1.14x), effect 1.15x, excalidraw 1.41x, drizzle 2.15x, half to three quarters
  inherent. Import-graph affinity, block stealing and dealing by a per-file cost estimate (bun's identifier count) were
  all measured negative or within 1-2% on vscode: not built (a cost estimate predicts no better than bytes).
  Stealing already ends at the costliest file.
- **Variance sharing is closed** (2026-10-22, `TTTT_VARIANCE_CENSUS`, `scripts/variancecensus.py`): a shared
  variance isn't K-independent by construction, the clean share is under 1% of K12 instructions, and drizzle's
  duplicated variance work is tsc's own per-closure cost (tsgo pays it too); the "25x blow-up" seen
  on the way was a census counter summing type ids across id chunks.
- **Bun's concurrency is not adoptable whole** (2026-10-22, notes/bun_concurrency.md): bun publishes selectively in
  batches and keeps its output deterministic by rollback, which needs id-free type ordering (priced since: 2-3 suite
  tests, ~20-35 real-code diagnostics, the sharing study above).
  What applied: the chunked parse of big declaration files (landed), a persistent pool (designed, declined for now),
  link in parallel per import-cycle level (30-40 ms on vscode; after link's page faults).
  Its type-store shape was studied as a memo (notes/sharing_design.md): the staged path above.
- **GCC, BOLT and huge text** (2026-10-22, the laptop at 8e3f4fb0 and 277be4bc): the shipped build is GCC LTO + PGO
  relaid out by llvm-bolt (-3.2% K1 wall) with the hot text on a 2 MB huge page (-4.6% K1 wall). clang compiles but
  costs +1.4% cycles at K1 and +2..7% instructions, with a 3.67 MB hot window against GCC's 2.47: not the production
  compiler. `-fno-peel-loops` took -1% cycles for +1.2..3.7% instructions: not taken.
- **Names are not interned per program** (2026-10-22, wt-fe2 built it, notes/front_end.md): on the laptop vscode got
  +1.8% wall, +2.8% cycles; cycles rose 5x more than instructions fell. Declined.
- **The parser gets targeted fixes, not a rewrite** (2026-10-22): tttt already parses in 2/3 of tsgo's instructions;
  first-word statement dispatch, skipper tables, quick tests and lists built once took vscode's parse -14%
  (notes/front_end.md "The parser"). Dispatch shortcuts land with a `VERIFY_GATES` build that checks the fast path
  against the slow one.
- **Checker constant-factor mining is at its floor** (2026-10-22, notes/checker_speed.md): sites now give 0.1-0.5%
  each; stop mining. Lazy message formatting (2.6% on drizzle) waits on hacks.md's "Two circularity diagnostics come
  from printing a type for its side effects" being fixed tsc's way.
- **Every unfixed divergence gets a known-failing test** in `test/cases` (2026-10-22; the results file records
  pass/fail per component): the real-code oracles are down to confirmed tsgo artifacts and written-up designs.
- **Declined with write-ups** (2026-10-22): a heuristic reconstructing tsc's deferred-body check order and arrayFrom
  (bugs/missing_checks.md), import-graph affinity and block stealing (notes/duplication.md), spinning on the pool
  lock, skipping display for suppressed errors, a member_refs memo (notes/checker_speed.md).
- **No shared declared layer** (2026-10-18): what checkers re-derive is instantiations of the project's own code; lib
  and `.d.ts` declarations are 2-6% of it (measurements.md "What the checkers re-derive").
- **Defaults** (2026-10-22/23): `--threads` every core; checkers one per 16 KB of checked source (was 64 KB: nest and
  react get ~8 checkers, nest -20% wall at +13% RSS; level with K12 on the laptop, ahead of K4 on nest, xstate and
  valibot), capped at ceil(checked bytes / the largest checked file) only when that file is >= 1 MB (10398e204,
  8d4518ecc: TypeScript6's 3.15 MB checker.ts 8 -> 4, where the laptop measured K4 -10% wall and -32% CPU; type-fest,
  tanstack-router and react are not bounded by one file's bytes: type-fest's K4 was 0.28 s against K12's 0.18), up to the physical cores, past them one per 4 MB up to the threads (only vscode gets 24); the checker
  memory default a process-wide pool of 80% of the memory available at startup (1e71cfc86; it was a quarter of RAM;
  zero recycles on everything measured on a free box, `--checker-memory MB` the explicit override); a stolen file starts a fresh checker only
  when the thief holds half its budget. A wall gain must be weighed against CPU: K12 -> K24 on vscode bought 0.09 s
  for 12 CPU s.
- **Speed over memory, but memory is now worth cutting cheaply** (the human, 2026-10-22/23: up to ~3x bun's peak was
  fine for a clear speed win; at vscode 5.5 GB against bun's 2.2 it is now "a lot"). The kernel round's +4..15% RSS
  was accepted for its walls. Three levers, in order of price: (1) the budget default (since 1e71cfc86 the
  available-memory pool, the human's requirement; on this box 1 GB was 1.39 s / 3.1 GB against the default's 1.20 s /
  5.15 GB); (2) per-file release, dropping a checker's per-file results
  (expression, flow, node memos, per-file relation work) at the file's end as bun does (most of its 2.2 GB on vscode):
  priced and declined on 2026-10-23 (notes/memory.md): 76-82% of an arena is droppable at file end but it is
  instantiated type structure, a memo/scratch-only region reaches 13.5% on vscode (under the 15% bar), and reusing
  recycled chunks saves at most 0.6% CPU; (3)
  phase-boundary trims (small: a trim burst when no checker runs). ~2 GB of vscode's peak is budget-independent.
  Closed on the way: the compact AST (2026-10-16: vscode's zero words are diffuse, exact-size variants reach ~165 MB
  at most), the copying recycle (2026-10-17: costs about what re-derivation does), further struct diets (< 120 MB
  left), freeing ASTs after their file (other files' checks read them).
- **Output is independent of checker count, threads, budget and schedule**, byte for byte (`scripts/kdiff.py`,
  `checkerdiff.py`, `threadcheck.sh`, the recycle oracle): tsgo's is. A change that makes a flake rarer without a
  named mechanism is not a fix. Anything that could be history must be derived from the node or declaration, as tsc
  does, not from what a checker happened to compute first.
- **Unambiguous tsgo bugs are not emulated** (2026-10-23, the human), unless longstanding or of a kind projects might
  rely on. First case: tsgo 7.1-beta's `-b` build-wide file-system cache never sees writes, so a reference's outputs
  probed before it was built stay missing (astro's, b4run's and atproto's TS2307 under `tsc -b`); a deterministic
  simulation of its scheduler and cache was landed (7c2777878) and reverted the same day. bugs/sourcesweep_build.md
  records the mechanism and the upstream state; those `-b` sweep misses are expected until tsgo fixes it.
- **Correctness by construction over catching races**, and over fidelity to tsgo *artifacts* (a runaway's model, a
  cache collision): emulating those is special-casing in a respectable form. The suite's last 12 are off the table
  (2026-10-15): each is argued in bugs/tsgo_divergences.md or a mechanism needed by one test; work them only if real
  code shows the mechanism. A test may be declared out of reach only with evidence that tsgo's output depends on
  something tttt shouldn't reproduce ("tsgo's check order" alone is not such evidence).
- **The oracle is tsc as it ships** (tsgo 7.1): no dual-version modes, delete what the oracle deleted, re-pin when a
  release lands. The oracle tools are tsgo built from source with a Go overlay (`test/oracle`), so instrumenting tsgo
  stays one `go build` away.
- **The sweep is a training set; the measure is fresh large repositories, per repo** (2026-10-23, the human: "over half
  of the random reasonably large projects I test, clean under tsgo 7.1, have incorrect errors under tttt, even
  single-threaded"). Every repository in the source sweep has had its divergences fixed, so its 99.5% says nothing
  about the next project. The honest number is the day-one match rate of fresh LARGE repositories counted per repo
  (a repo is clean only if every config matches; refresh 6 was 24 / 29 repos on day one), measured at one checker
  AND at the defaults. Refreshes 7 (generic-library app monorepos) and 8 (random large repos, no rule) started on
  2026-10-23; keep adding fresh large repos until the per-repo day-one rate stays above ~95%.
- **Real projects are the second oracle, and oracles must look both ways.** A rising pass count with flat corpus
  counts is test-chasing; a clean corpus only finds spurious diagnostics, flag flips and mutations find missing ones.
  Rank by root cause across projects, not by code (one mapped type with an `as` clause was 810 TS7006); prefer fixes
  that move whole categories. Saturated oracles measure nothing: refresh the breadth oracles with repositories chosen
  by a new rule every so often (day-one match rates 96.1, 97.1, 98.1%; the recipe is measurements.md "Fresh
  repositories"), and say what each oracle's projection doesn't compare (message text was a hidden axis once).
- **Perf is priced by instructions and types, not wall, on the shared box**: callgrind counts (`scripts/instrcount.sh`
  at every landing: creep is silent otherwise, a +50% on drizzle once went unnoticed), tsgo's type and instantiation
  counts as a machine-independent oracle (`scripts/perfsweep.py`: a ratio above ~1.5 is a missing cache or a
  runaway), many-core work by per-file time sums under real parallel runs with the spinning-cores floor (1.62x), never
  serialized CPU. Walls and every cycles-level claim come from the human's laptop (cachegrind's icache proxy
  over-predicted `-fno-peel-loops` and under-predicted BOLT). The shipped build is `scripts/fastbuild.sh` (State).
- **The parser's error recovery needs no rewrite** (2026-09-29: one list/commit-point framework).
- **JS lowers into the same `Ty` machinery**: no parallel JSDoc type world.
- **Default lib files are checked like tsgo checks them** (unless skipLibCheck/skipDefaultLibCheck): ~40 ms per
  esnext+dom program, and it found a real checker bug.
- **`-b` has one model** (2026-10-20): a referenced project is read through its emitted declarations, like `tsc -b`;
  its leftovers are `-p` checker bugs (bugs/sourcesweep_build.md).
- **Out of scope**: JS emit (2026-10-12), a cache across runs (2026-10-21), the CLI modes above (parked 2026-10-16;
  the measurements for when they reopen are in todo.md "Parked").
- **How changes land**: staging in place works (the type core migration ran 8 steps with the suite runnable
  throughout); don't transliterate weak algorithms onto a new foundation, write them the way tsc observably behaves;
  when relations get stricter, expect latent inference bugs a lenient relation hid; when a checker feature "doesn't
  work" on lib types, look at what the lib *parsed* to first. A drop-in must survive its inputs changing mid-run
  (`text_sigbus()`).

## Built and holding
Hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks), tsc-shaped binder with
persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity relation, stable type
ordering, mapper instantiation, tsc's inference flow (return mappers included) and ContextChecked/context anchors,
one circularity stack with resolution barriers, conditional/mapped/indexed/template/substitution types, deferred
references, flow graph + narrowing, module resolution + tsconfig with tsgo's two-phase file collection, iteration
protocol, generators, `this` types, mixins, decorators, union origins, elaboration, JSX, tsc's type printer and
symbol chains, the pseudochecker, JSDoc + CommonJS + expandos as 7 has them, parallel loading / chunked parsing of
big declaration files / declaring / resolving / checking with the output independent of the checker count, work
stealing, checker recycling, deferred function bodies, per-file AST arenas, mapped source text, declaration
diagnostics, the declaration writer with comments and maps for TypeScript and JavaScript, project references / `-b`
through emitted declarations, 7.1's content mappers, tsc's laziness and deferred checks, 7.1's language, in-flight
state as per-checker records, one atom table and one type-id space per program, the shared graph behind its flag.
~150k lines of C.

## Absent or approximated
Pseudochecker gaps (bugs/display.md "Pseudochecker gaps"), namespace-import clones (cloneTypeAsModuleType), member
identity (members aren't Symbols), tsc's re-entrancy beyond the declaration and return-type sites
(notes/reentrancy.md), errors from a loop's intermediate flow types beyond the partial pass's (its K-dependence hole:
triage.md), the CLI modes above.

## Architecture assessment
A clean, direct-style checker: hand-written backtracking parser straight from `char*` (no tokenizer, a fail cache for
the ambiguous cases), an AST of plain structs in per-file arenas, a binder (persistent symbols and scopes, merging
like tsc's), then checking driven by `LazyType` (memoised, cycle-detecting per-node types) over the interned `Ty`
core. All checker state is in `Checker`; the AST is read-only while checking. What's left to watch:
- **Parser**: unbounded recursion depth (binary chains need ~8 MB of stack; tsc walks them iteratively); speculative
  attempts copy a `Parser` (child parsers on vscode 73M -> 41M after the round), no token stream: cheaper than tsgo's
  already, so targeted fixes. The debug build's suite aborts at a parser check (triage.md).
- **Binder**: declare runs in parallel for files that declare only into their own tables (96% of vscode's files);
  link is serial (alias resolution's order is observable; bun links in parallel per import-cycle level, worth it
  only once link is real work after its page faults: notes/bun_concurrency.md, notes/front_end.md); the resolve walk shards per file, big declaration files in chunks
  (hacks.md "Parallelism contract").
- **Threading**: any new checker-wide memo that *reports* must be tagged `Error.once_key`, and anything written to
  AST/binder state during checking is a race (the frozen guards log it; TSan on the corpora is the net:
  documentation.md "Typecheck"). Under `--shared-graph` a new lazily written field must be a `SharedWord`
  (notes/shared_graph.md) or it is a race only TSan and the 20-run tables find.

## Risks
- Exact-match oracle: `types`/`symbols` baselines require tsc's type printing (alias preservation, union order,
  origins, truncation). Type ids still break union order's last ties (fresh/regular literals, same-symbol anonymous
  types, conditional roots), so creation order still matters a little.
- Perf: checks accumulate cost silently between perf rounds; deep generic code is where tsc's caches and shortcuts
  matter (every past check-up's wins were tsc caches tttt lacked).
- **Competition**: tttt leads bun canary 1.4.4 at the defaults on every bench corpus (vscode 1.75 against 1.93 s,
  ~10%) at 2.5x its peak memory, but the human's private set still has projects where bun wins (on wall, never on
  total CPU): the lead is not yet "distinctly faster" everywhere, and the breadth sweep hasn't measured how many
  projects that is. tsc-rs (a Rust tsgo port at our pin, `scripts/bench.sh --tscrs`) spends the least CPU at few
  checkers. Our margin is single-threaded efficiency, which many cores spend on all-core clocks, SMT, idle cores
  behind one dominating file and duplicated work.
- tsgo is order dependent in places tttt isn't; tsgo 7.1's ordering rules are what `ty/order.c` must port, not patch.
- The per-fix instruction gate (quickgate's drizzle/zod budget, 2026-10-25 at +0.3%, flagged at 2%) lets drift
  accumulate: ~250 commits added +1.3..1.7% on every corpus between d22d93ae and main, next to three per-file 4-12x
  regressions no corpus showed. The human (2026-10-25): bisect the drift (perf2), and "if you find anything a tighter
  gate would have caught that would have been worth catching, tighten it. but feel free to loosen it again if it causes
  more trouble than its worth". A per-file oracle for the gate (the sweep's biggest configs' hot files, as
  scripts/perfab did) is the structural answer to the per-file class.
