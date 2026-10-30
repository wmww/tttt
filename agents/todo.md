# Todo
The live list of directions and ideas, roughly in order (vision.md "Direction" says why). Each entry: what, why, what
is known, where the numbers are. Delete an entry when it lands, leaving its numbers in measurements.md; add new ones
where they fit the order. Uninvestigated items go to triage.md, known bugs to `bugs/*.md`.

Starting point: measurements.md "Oracle totals (2026-10-23 evening)" (37d832827), `python3 scripts/bugcount.py` (the
known correctness bugs and every oracle's last cache in seconds) and the laptop's latest section (at 3a77c827: the
kernel round; the counters at 2989c0f0). `test/instrcount.txt` is fast/tttt (BOLT) at a recycle-free K1, so a
jump against it needs attribution (policy, build or code) before it is celebrated.

## Always first: the re-pin to TypeScript 7.1.0 stable
When `npm view typescript dist-tags` shows `latest` 7.1.x (7.1.0 stable expected after 2026-11-24), do this ahead of
everything. The oracle is the 7.1.0-beta (tag `v7.1-beta` = 3e923636, re-pinned from the 2026-10-08 nightly on
2026-10-23 by this recipe: the beta added 157 suite tests, 4 were lost and fixed, 19 new ones fail and are written up;
measurements.md "Oracle totals (2026-10-23, the 7.1.0-beta re-pin)"). The beta's one new feature tttt lacks is the
content mapper `outputExtensions` (12 suite configurations, bugs/binder_modules.md): worth doing when content
mappers matter.
1. `thirdparty/ensure-typescript.sh`: `TS_COMMIT` = the release's `gitHead` (`npm view typescript@7.1.0 gitHead`),
   `TS_VERSION` its version, the Go version from the new `tsc/go.mod` (`GO_VERSION` + sha256). `rm -rf
   thirdparty/typescript` and rerun (~90 s cold; `test/oracle` may need small API fixes if tsgo's internals moved).
2. `thirdparty/refresh.sh`: libs from the new tree; regenerate `src/lib_features.py`, `src/lib_names.py`,
   `src/unicode_identifiers.py`, `src/regex_tables.py` and diff them.
3. `scripts/fasttest.sh --summary`: lost tests are 7.1 changes after the dev pin; bucket them (failstats), fix the
   cheap ones, write up the rest. New tests count too. Then one parallel pass over the new failures (disjoint test
   lists per worker, 0 lost at merge).
4. Every cache regenerated with tsgo (each script's `--tsc`): `realworld.py --tsc --corpus X` for every corpus,
   `sweep.py --tsc` (and `--tsc --flags nuia`, `--flags eopt`), `expecttypes.py --tsc`, `sourcesweep.py --tsc`,
   `flagflip.py --tsc --force`, mutate's /tmp cache deleted. Commit between them (sweep ~10 min, source sweep ~12).
   In a worktree the `-b` corpora (tsc-src, typescript-eslint) go through a bwrap overlay of the shared corpora (tsgo
   -b writes into the checkout), and dtsdiff/flagflip `--tsc` only from the main tree (the beta re-pin's report).
5. `scripts/oracles.py`, `scripts/scenarios.py`, `scripts/clidiff.py --sample 4`: before/after numbers into
   measurements.md; the version in CLAUDE.md's and documentation.md's oracle sentence and vision.md's state.
Done when `grep -rn OLDCOMMIT scripts src test thirdparty/*.sh` is empty, no oracle in `oracles.py` is STALE, and the
suite's lost list is fixed or written up. tsgo's `.tsbuildinfo` `version` string would matter only if a buildinfo is
ever written (it isn't).

The re-pin above is direction 6 (vision.md "Direction"). The human's priorities for the rest: tttt reports exactly
where tsgo reports (presence is absolute; text exactness may be compromised and written up), then distinctly faster
than bun canary on real projects, judged on their private set (perf work needs a mechanism argument or a breadth
check, not the bench, and no machine-specific tuning: vision.md "Decisions"); memory where cheap; easy, measured
wins first; land fast and gate in batches.

## Now (2026-10-29 box time, 2026-10-31 wall): the shared graph is the default; correctness and safe perf wins
- **The shared graph is the default at K > 1** (vision.md Decisions; 7c91df2df; plain = `TTTT_SHARED_GRAPH=0`, one
  checker never has a graph). The default-on gate and the forced-taint oracles found five publication leaks this week,
  all fixed by mechanism (308bfacae, 9990e5259, e598f8660, fea36a257, 1e1801c88) plus one forced-knob artifact
  (4b87954cb); the rule now reads "a record's values may be private, the identities it mints never are"
  (notes/shared_graph.md). quickgate -k runs sgcheck and `--force-poison 30`. Worker `fwc` builds the forbidden-write
  check (a publish-time ownership walk in the GRAPH_CHECKS build, race3's design) so an identity leak fails on first
  sight; then the OWN machinery (KEY_SIGNATURE_INSTANCE_OWN, sig_res_is_own) comes out if the oracles stay at zero. The
  natural dokploy K16 rr difference (seen once) stays [open] with the signature clones as its likely mechanism.
- **Perf, safe wins only (the human, 2026-10-31)**: no threading redesign, no big trade-offs; moderate refactors for a
  purpose are fine. Done: tail (the one-file .d.ts shape -13%), fe3 (front-end instructions -5..-22% at T1), fe4 (the
  front end's threads: whole runs -3..-7% at the defaults on five of six shapes). Next perf worker, when a slot is free:
  the loader's own per-file work (~100 ms CPU on vscode/openclaw: program_add_made_source, the files map, include
  reasons), after a fresh timeline (front_end.md "Thread-level pass" resume); lower value: nitro's module-refs split,
  link's augmentations step.
- **Correctness** (worker `cor15`): the JSDoc `{@link}` TS6133 fix, seeds 20261120/21, the next bun root-cause groups;
  triage.md has vue-query's 4 spurious diagnostics (unwritten).
- **The libs' fixed cost, if tiny-project latency ever matters** (measurements.md "The standard library in the
  binary"): per run, parsing and binding the default libs is ~13-18 ms CPU / ~8-10 ms wall of a ~37 ms one-file run
  and <=3% of a real project's CPU (off the critical path at the defaults); not worth a build-time AST. The options,
  if it is: (a) a serialized arena of the parsed and bound libs mapped at startup (needs position-independent AST
  pointers or relocation, and couples the file to the AST and Symbol layouts: a format to keep in step with every node
  change; the binder's lib symbols are shared, mutable merge targets for user `declare global`s, so they'd need
  copy-on-merge), (b) generated C initializers of the AST (the same layout coupling, a slow compile, no binding), (c)
  in-process sharing of the parsed lib files between the programs of one `tttt -b` run (what tsgo does: its build
  host's parseCache), the cheap one: each `-b` program parses its libs again today, and the embedded texts are already
  shared. The bigger fixed cost on a tiny file is the checker's first resolution of the lib's global types (~22 ms
  of typecheck on a 4-line file), which none of these removes.
- **Release v3: every platform from one pipeline** (v2 shipped 2026-10-29: GitHub tarballs of binary + thirdparty/lib,
  and npm `tttt-ts@2.0.0`, Linux x86-64 only, glibc >= 2.38 because it was built on Arch). The human plans the
  build for the next release: Linux x86-64 and arm64 built against an old glibc (e.g. a glibc 2.28 container with a
  current GCC toolset; the binary only binds symbol versions, so no perf cost: the C23 `__isoc23_strtol` family,
  `fmod@2.38` and the 2.34 pthread move are the new symbols, none hot), macOS arm64, Windows later. npm: today one
  package whose `bin` is the ELF itself (scripts/npmpack.sh); for several platforms the esbuild layout (a wrapper with
  per-platform `optionalDependencies` carrying os/cpu/libc), keeping the native binary as the bin or a JS shim's
  node start-up (tens of ms) dominates small runs. The libs are compiled in since 418884466, so a package is just the binary.
  Publishing is the human's (a 2FA npm account): the `tttt-ts@0.0.0-stage` placeholder npm's staged publishing left
  is worth deprecating. Keep the oracles green and the headline saved (`bugcount.py --save` per full run).

- **macOS arm64 follow-ups** (agents/notes/macos.md; the port landed 2026-10-29, output identical to Linux on the
  suite and six corpora). Open: (1) memory on macOS: peak footprint is 1.1-1.9x bun's on five of six corpora there,
  and macOS's libmalloc is untuned (no mallopt) and unmeasured against an alternative; (2) physical_cores() counts
  efficiency cores like performance ones (hw.physicalcpu): on real M-series hardware a checker on an E core may be
  the critical path, hw.perflevel0.physicalcpu is the alternative, untested (the VM has no E cores); (3) the exit
  teardown runs in process (no clone/pidfd reaper): measure its share of wall on vscode on real hardware; (4) the
  Linux-only tooling (valgrind, perf, bwrap: instrcount, clidiff, dtsdiff, bench.sh) has no macOS counterpart; a
  `bench.sh` that runs on both would be the cheapest next step.

## Done (2026-10-28/29): the hub regime, the shared graph's finish line
- **The shared graph at the laptop's 24 threads** (the next point on the break-even curve, hub_regime.md): flag
  against plain at the defaults and at `--threads 24 --checkers 24` on zod, drizzle, effect, vscode, dokploy/apps/
  dokploy and openbot/apps/i18n (physical paths), best of 5 interleaved, wall, user, RSS; `TTTT_SHARED_GRAPH=1` is the
  flag. If the corpora are at or under 1.0 there, the flag is a default-on candidate pending residue C; else the
  per-read cost comes first.
- **Fewer shared reads per unit of work** (the only lever left on the premium): the content nesting order is done
  (the walk order replaced the stamps at b2b3cdcd2, residue C closed, -0.3..-1.9% instructions: sharing_design.md §6), and variance groups
  measured once (zod 287 at K1 against 1086 at K8 under the flag: lost work, +3..18% instructions; the 10-22 closure
  of variance sharing was about K-independence, which the shared graph's taint rules now give a frame for).
- **The hub regime diagnosis (done 2026-10-29: hub_regime.md)** (the input to the redesign question; a diagnosis track with a written verdict, no
  build-out): on dokploy/apps/dokploy, monkeytype/backend, zod and openbot/i18n, what the hub is (which declarations
  nearly every file reaches: `TTTT_DUP_CENSUS`, arenasites), how long building it serially takes at K1 (the wall
  floor of the per-checker model at any K, and of the parked shared graph), what share of each checker's types it
  is, and whether bun's threads build it concurrently (notes/bun_concurrency.md plus its source) or pay it once
  serially too. Verdict: can the hub be built in parallel, at what granularity, and what it would need from the
  type graph (notes/sharing_design.md has the costs already measured). Numbers: notes/bun_breadth.md "2026-10-28".
- **Bad speculation, twice bun's** (the laptop, 2026-10-27, footprint.md "The laptop's counters"): 5-8% of cycles
  everywhere, 2x bun's on the one-file `.d.ts` shapes (imm 110 M against 51 M cycles). Unexamined: branchy code on
  data-dependent type kinds is the guess (tsc's flags tests, tttt's kind switches). A diagnosis: `perf record -e
  branch-misses` on the laptop by function and line, then whether the top sites are kind dispatch, hash probing or
  relation recursion; a general lever if it is dispatch.
- **immutable.d.ts**: bun 0.19 s, tttt 0.38, tsgo 3.4 s; the critical path of 7 sweep projects. What bun does there
  is an unread lead (a checked `.d.ts` is the idle tail's usual shape: 188 of 300 projects have one file over 1/K).
- **The front end's loader waits**: load+parse waits on a worker's parse 51% of the phase, resolve at 2.7 cores
  (bun_breadth.md "2026-10-28"); 9.5 s of the 300's 51.9 s loss.
- **For the laptop**: `perf stat -r 5 -e cycles:u,instructions:u` at `--threads 1 --checkers 8` against the default
  threads with `--checkers 8` on dokploy/apps/dokploy, monkeytype/backend, solid/packages/solid, kebo and
  vitest/test/browser: splits the 1.36x per-instruction slowdown (SMT, helpers, or this box's load). And
  notes/footprint.md's `perf stat -M PipelineL1` on the immutable.d.ts and store.d.ts one-file projects (data stalls
  against front-end stalls: orders the representation work).
- **The physical-core checker cap stays until duplication is fixed** (the human, 2026-10-29): its removal (ad0215612,
  byte-identical, built by ref) is a tuning question only while every checker rebuilds the declared world (drizzle
  K8 -> K16 on this box: +50% user CPU, +22% RSS); no A/B. It goes with the duplication work, when one checker per
  thread is simply right. The commit is in git history (branch wt-ref-cap deleted; `git show ad0215612`).
- **Correctness, continuous** (cor's queue): sourcesweep_checker.md `[open]` (starlight's TS2589 is reentrancy stage
  E), then type_core.md's one-line probes, then bun cases. The content mappers' `--runExternalCode` is a feature
  decision for the human (it runs external code).
- **Cleanup leftovers** (ref's report): the shared graph + taint removal when the Decisions allow it (inventory in
  vision.md); the 235 `plan N` comment tags (cosmetic, a Sonnet task); the `-b` per-builder checker split went with
  the cap removal (wt-ref-cap).

## Earlier threads: the human's projects (2026-10-23)
- **openclaw** (correctness only, out of every perf measurement): 36 / 49 configs match in the last sweep, **13 time
  out** (its sub-configs under load: are they slow in tttt or only in the sweep's limits? `scripts/projdiff.py` on one,
  `TTTT_FILE_TIMES=1`), and 4 are K-dependent in kdiff (openclaw/packages, packages/plugin-sdk, src/agents/sessions,
  src/config): history-dependent results, gate each fix with kdiff K1/K16. The det track's layers A/B (c97742da3,
  1a6cf92cc) fixed the signature re-entry flake; the TS2554 'Expected 2-0 arguments' and the constraint check under
  an outer relation (bugs/type_core.md) are the written-up rest.
- **opencode** (correctness only; repos.txt `# perf: no opencode`): 8-27 varying errors at the defaults, 10 at K1
  before e01e80779 fixed identity after normalization. Left: **its history-dependent alias re-entry below call
  barriers** (tsc re-enters an alias in flight below a call's barrier, tttt reads what the first entry point left:
  notes/reentrancy.md experiment 1, which once gained mikro-orm TS2456/TS7022). tsgo's verdicts aren't cached yet
  (composite packages: tsgo -p writes `.tsbuildinfo` into the checkout, so cache it through tttt-oracle or projdiff's
  sandbox). Its gate: kdiff K1/K16 on opencode, mikro-orm, drizzle, effect, zod.
- **osis** (the human's sphi/osis ts/, repos.txt at 99d3b589c, a8dc6e431): not installed nor cached; its tsconfig
  lists the generated build/test.ts (run ts/test.sh's extra/collectTests.js step at install, or both sides report
  TS6053). `ensure-sources.sh` then `sourcesweep.py --tsc-new` from the main tree.
- **Refresh 6's leftovers** (423 / 429, measurements.md "fifth rule"): pothos (plugin-with-input TS7031,
  plugin-add-graphql TS2345, plugin-smart-subscriptions -TS7023: a variance measurement re-entering an own return
  type, known-failing ttttVarianceReentersOwnReturnType), node-pg-migrate/docs (TS7's `typeof ts` typings), portabase
  (auth.ts:690 `never`, form.tsx:227 -TS2322), tanstack-ai/packages/ai-harness (-TS2615).
- **Refresh 7, stopped at six repos** (cor9, repos.txt "Refresh 7": application monorepos on generic libraries,
  `scripts/reposearch.py genericapp`): openbot, comp, thunderbolt, meridian, afilmory and parabol installed and
  cached (74 / 79 projects, 4 / 6 repos at --threads 1; the leftovers are bugs/sourcesweep_checker.md "Refresh 7").
  The other 38 picks are commented out in repos.txt (skyroc failed, four interrupted, 33 pending). Resuming needs
  **ensure-sources.sh ported to `scripts/installsandbox.py`** (agents/notes/install_security.md: every install
  sandboxed with its own cache root, not the sampler's `.cache/pm`, `--refuse` for yarnrc/pnpm-workspace hooks, the
  dedupe step kept; test on skyroc), then the installs at -j 1, `--tsc-new` and the day-one table per repo (a repo
  is clean only if every config matches) at --threads 1 and at the defaults. Dropped for now (2026-10-23, the npm
  workflow first).
- **Instruction counts**: today's fixes drift ~+0.8% on drizzle against `test/instrcount.txt` (a6af311ed). Attribute
  it (`scripts/instrcount.sh --vs REV` per batch boundary, abbuild.sh) before `--save`; the escaped-keyword cost was
  already taken back (9a5647e3b).
- **The shared sources' integrity**: at 10:51-10:57 a worker's reducer wrote through a symlink into
  `thirdparty/sources` (it stubbed 690 hardlinked dependency files, so every repo sharing them changed; restored in place before the full
  sweep). Since then `scripts/sourcescheck.sh` runs first in sourcesweep.py and dtsdiff.py, `scripts/reduce.py` copies
  without symlinks into a bwrap sandbox, and the worker rules (coordinator.md) forbid symlinks into `thirdparty/` and
  `rm` of computed paths. Run `sourcescheck.sh --save` after every install or re-pin.

## For the human
The prompts were given in conversation at the fourth coordinator's end; redo them from here, each with the exact
command (measurements.md "How to measure": sys with `TTTT_SYNC_EXIT=1`):
- **(a) The remaining kernel time: done 2026-10-27** (measurements.md "The 12-core box at df95ec2f"): kernel 4% of
  user on vscode, 8% on drizzle; `clear_pages` 30% of kernel cycles, faulting ~10%, spinlocks 4.7%, TLB shootdowns
  3.7%, no futex/mmap. Zeroing fresh pages is ~2.4% of user cycles; reusing arena memory is the only lever left.
- **(b) The exit tail under THP `always`**: last output byte to exit with `TTTT_SYNC_EXIT=1` against the reaper's
  default (this box, THP `madvise`, measured 7-66 ms; fewer pages to free on the laptop may make it smaller).
- **(c) Thread placement: withdrawn** (2026-10-23, the human): a rule that pins work to a kind of core would be
  hill-climbing whichever machine runs the bench. Scheduling policy only on arguments that hold on any box (core and
  thread counts, bytes, file sizes); the OS places threads. `physical_cores()` (main.c) stays: a per-core cap is such
  an argument.
- **(d) The recycle-budget sweep**: superseded by the available-memory pool (1e71cfc86, the human's requirement). Still
  useful: vscode and drizzle with `TTTT_MEM_AVAILABLE_MB` 4096 / 3072 / 2048, wall against peak RSS, to see what the
  pool costs on a smaller machine.
- **(e) The bench on main after the reaper** (`scripts/bench.sh --runs 3 --checkers 12 --bun ...`), the arena
  alignment (97839044a) included.
- **Keep or revert the relation HACK's removal** (f38a8b493, one commit): react 0.09 -> 0.11 s at the defaults and
  type-fest single 0.61 -> 0.68 on the laptop (bbe37c5f), react +36% / type-fest +7% instructions; it is tsc's own
  relation work (tsgo 84.6k relations on react, tttt 93.4k, and form.tsx is half of tsgo's react check too) and fixed
  pi-bolt's TypeBox chain. Kept unless the human says otherwise.
- MADV_POPULATE_WRITE counts as minor faults on this box's kernel but not in the laptop's `perf stat page-faults`
  (6.7k there against ~72k here on drizzle): compare faults like with like.

## 1. Page-fault leftovers: closed 2026-10-28
The huge-page machinery is removed (vision.md "Decisions", the laptop at df95ec2f). What is left of kernel time is
page zeroing (2.4% of user cycles): touching less fresh memory (reusing recycled arena chunks, measured at most
0.6% CPU in notes/memory.md) is the remaining lever, small.

## 2. The bun breadth sweep's remaining classes
Done 2026-10-23 (notes/bun_breadth.md: 487 projects, bun wins on wall on 116, median ratio 0.82; fixed on the way:
React's 187-member props intersection 9d63a2dd8, the largest-file cap ignoring `.d.ts` c082b54c5). Left:
- **(b) the front end, 86 of the 116 losers** (3000-6700 files, mostly node_modules declarations; tttt's front end
  slower than bun's load on 431 of 487, median 1.55x, at ~2 cores busy). Needs a timeline (`bench/timeline/`) on a
  quiet box or the laptop on one of nitro, slidev, faker: the load pool's workers report busy far beyond their CPU.
  Engineering, not policy.
- **(c) a library warm-up every checker pays** in its first file (kebo 133 -> 633 ms at K8): only sharing removes it,
  and sharing is closed (vision.md "Decisions"); a note for the human, not work.
- **(f) slow shapes** beyond the fixed one: rerun perfshape on the worst 15 after 9d63a2dd8 to see what is left.
Rerun the sweep after a front-end change (~45 min, the recipe in the note); warm bun once per project.

## 3. Memory: three levers, cheapest first
vscode 5.5 GB at the defaults against bun's 2.2 (K1 4.3); the kernel round added +4..15% (vision.md "Decisions").
- **The budget default landed** as a process-wide pool, 80% of the memory available at startup (1e71cfc86): no
  change on a free box, bounded on a busy one or in a cgroup. Its doubts (the recycle pick by arena size, not
  materialized bodies; overshoot by one file per checker) are in the commit.
- **Per-file release and chunk reuse: priced and declined** (9f238a1c0, notes/memory.md): vscode 76% of the arena
  droppable at file end (zod 82%) but it is instantiated type structure; the memo/scratch region reaches 13.5% (under
  the 15% bar); reusing recycled chunks is at most 0.6% CPU. The note lists what would cut the peak next: a kept
  region for what non-checked declarations derive, checker counts by measured scaling, dropping materialized bodies
  no other file reaches.
- **Phase-boundary trims**: a trim burst when no checker runs. Small.

## 4. Kernel round three: answered 2026-10-27
The laptop's profile (For the human, a) shows no round left: 4-8% kernel of user, led by page zeroing. Closed.

## 5. Correctness long tail (continuous)
New real-code sightings first; every unfixed divergence gets a known-failing test in `test/cases`. Grep `bugs/` for a
repository's name before investigating it. **No semantics commit lands without a repro or the full source sweep, and
a relation change runs kdiff on the affected project at K1 and K8/16** (better-drizzle's 742 was K>1 only). Hacks
are suspects (vision.md "Decisions"). Open:
- **Lazy parameter typing in signature resolution: the structural item.** tsc resolves a returned function's
  parameter types lazily, tttt eagerly in signature_resolve_slow(): kebo's mobx-state-tree cycle (bugs/
  sourcesweep_checker.md "fourth rule"; reduce.py cut it to 18 lines plus 4 MST `.d.ts`, ccc313017's message),
  openclaw's TS2554 'Expected 2-0 arguments' (same entry), triage.md's missed TS2502 through `ReturnType<typeof h>`.
  The det track made re-entry read a whole record (1a6cf92cc); the design that is left is in the kebo entry. Design
  first, gated by kdiff.
- **bun's cases** (`scripts/buncases.py`, 1278 / 2411 match; as suite tests `bun*`, 980 / 2332 pass): 530 of 532
  keys no bugs entry names. Largest: -TS2322 252, -TS2509 184, -TS2339 177, +TS5114 146, +TS2322 127, +TS2339 127,
  +TS2842 126, +TS7022 112 (`python3 scripts/bugcount.py -v`); the 7 crashes first, then the missing TS7022 family
  (notes/reentrancy.md's test list for stages C/D), parser recovery. Write up per root cause so the unexplained count
  means something.
- **mcp-linker's 1.73x types** (triage.md): typed i18next's `t()` under an object contextual type, a base constraint
  chain tsgo cuts at the recursion-identity limit and tttt doesn't (localized, not fixed).
- **The indexed-access recursion identity** (triage.md): tsc's rule, reverted for youtube-enhancer's runaway (1.16M
  types against tsgo's 211k); needs a cut-down repro from src/validation/constraints.ts first.
- **The source sweep's other 7** (es-toolkit, monkeytype, hono x2, better-auth, discordx, sunmao): each a confirmed
  tsgo artifact or a written-up design (bugs/sourcesweep_checker.md, bugs/sourcesweep_build.md). **hono's TS2769**:
  tsgo keeps a this-instantiated method type apart from the declared one, tttt merges them (bugs/missing_checks.md;
  known-failing test ttttThisInstantiatedMethodErased).
- **A fifth refresh rule** when the human reports new divergences (the recipe: measurements.md "Fresh repositories,
  third rule" and "fourth rule"); the app-code, low-star and bun slices found what the old oracles couldn't.
- **The missing TS2536** on an indexed access of a recursive conditional alias inside its own declaration (triage.md).
- **The seed-62 spurious getter circularities** (recursiveTypeInference `_zod?` line 28, noCircularitySelfReferentialGetter2
  drop-as): not the expression-memo mechanism; unexplained. Instrumented tsgo (notes/instrumenting_tsgo.md,
  `TSGO_PUSH`/`TSGO_CYCLE`) against `TTTT_DEBUG_CYCLE=1` is the method.
- **The loop-pass K-dependence hole** (triage.md, `ty_flow_partial_reports()`) and the partial pass's leftovers (same).
- **The error pass's first check of a callback argument** (TS2339), **the binding-element family**:
  bugs/missing_checks.md.
- Mutation seeds yield less each time; parser-recovery mutants (58-77 per seed, `scripts/mutate.py`,
  `failstats.py --syntax`) are untouched.
- **A shape-matrix probe for ordinary code** (corH, 2026-10-23; for when the fresh-repo tables run dry): the
  human's spurious TS2322/TS2345/TS2353/TS2344/TS2339/TS2719/TS2488 come from everyday shapes that the oracles have
  run out of (bun, DT, source sweep and mutate.py -n 8000 showed none left in those codes). Generate strict files from
  matrices of everyday blocks, one statement per line so a difference is one (line, code) that names what was
  combined: ~90 declared types (primitives and literal unions, interfaces and their Partial/Pick/Omit/Readonly,
  discriminated unions with optional members, index signatures and Record, arrays, readonly and named/optional/rest
  tuples, function types and overload sets, classes with private members, enums, branded strings, template literals,
  Promise/Map/Set/Iterable/Generator, mapped and conditional utilities) against each other (`const t: T = s;`) and
  against ~40 fresh literals (object literals with excess, discriminant and spread members, conditional and `??`
  literals, getters, `as const`, `satisfies`); generic, overloaded, const-type-parameter and rest/tuple functions
  against argument lists; for-of, spread and destructuring over every type; property/element/optional reads under
  `in`, `typeof`, discriminant and truthiness narrowing. Oracle: scripts/tsgoprobe.sh baselines, reusing
  mutate.py's run_tsc/run_tttt/parse_errors and its errors.txt comparison, cached by content hash. Bucket by
  (missing|spurious, code), print the statement. A first draft fitted in ~250 lines of Python.

## 6. Small projects
react is bound by one file: `src/components/ui/form/form.tsx` (react-hook-form's Path/PathImpl with zodResolver) is
~37-44 ms of a ~94 ms K1 typecheck. **tsc pays the same** (measurements.md "The 12-core box at 48cd9835" part C: about
half of tsgo's check time and 74% of its types; tttt does it 5x faster), so it bounds react at every checker count;
react's relation count against tsgo's is in "Smaller ideas". nest is start-up bound (the front end is 2/3 of its
55 ms; kernel 27.5% of user cycles after the kernel round, against 6-9% on the big corpora). The rest is per-checker
duplication (each checker's arena is its repeated work, 3-6 MB on nest, 12-33 MB on react: notes/front_end.md
"Done"). Gates: RSS at many threads next to the wall; threadcheck/checkerdiff byte-identical.

## Smaller ideas
- **arm64 leftovers** (notes/arm64.md, 2026-10-29): a 16 KB-page Linux kernel is untested (64 KB passed every check;
  macOS's 16 KB pages are covered by the mac port); GCC 15's ext-dce miscompile of the LTO + PGO
  build is worked around with `-fno-ext-dce`, not reduced or reported upstream (recheck when GCC moves: drop the flag
  only if fast/tttt stays byte-identical to release on arm64); the arm box's source checkouts stopped at 216 / 378
  repos. Its bench (measurements.md "arm64 Linux"): tttt leads bun canary on vscode by 25% at the defaults.
- **Code footprint**: still ~50% frontend-bound after BOLT and huge text (op cache hit 63%, icache misses 23% of
  fetches at 2989c0f0). The noinline splits simulated I1 misses -4.5..-6%, but on the laptop vscode K1's icache misses
  per instruction rose 1.4% and the cycles' -1.7% came from fewer instructions: the working set is what is left.
  `scripts/inlinedup.py` and the inlining ledger (notes/code_layout.md) list the next copies. Lower priority since
  BOLT and the huge text page didn't move the human's private set: a change here needs a mechanism argument and
  the laptop's counters, not the bench.
- **The small-program floor's leftovers** (fe4's leads on nest, laptop, before round three: link 10 ms, walk 7 ms):
  link's time is mostly AST arena madvise/munmap page faulting, not linking (pre-fault or reuse arenas; a parallel
  link per import-cycle level only if link is real work after that); the load pool's finish-file lock is its top
  blocked wait; the walk includes thread start-up on the main thread. The laptop's nest timeline at bbe37c5f: link
  3.0 ms, walk 6.4, 44.7 ms before typecheck.
- **react's relation work against tsgo** (93.4k relations past the cache against tsgo's 84.6k since the HACK's
  removal): small unions related without caching (tsc's skipCaching, prototyped at -12% assignable cache entries) and
  the indexed-access constraint chains tttt relates inside UseFormResetField's variance measurement and tsgo doesn't
  (notes/checker_speed.md, with the trace-alignment method).
- **A snapshot-style recycle for the thrashers** (dokploy, monkeytype, supabase studio): bugs/perf_leads.md "Shared
  warm state at the defaults". Recycling costs vscode 15-20% of check time at small budgets (measurements.md "Memory
  budget sweep on vscode"), so it is the memory direction's lever once speed allows.
- **`-b` shares parsed declaration files across its programs**: every `-b` program parses and binds the default lib
  and each reference output itself (typescript-eslint's 56 programs take at least 80-130 ms each, `TTTT_BUILD_TIMES`);
  tsgo parses each `.d.ts`/`.json` once per build (`parseCache`). Measure the floor's split on a trivial program
  first; then share ASTs and binder scopes read-only between programs where nothing is program-specific
  (`impliedNodeFormat`, language version, arena ownership), or at least the lib files. Gate: typescript-eslint and the
  sweep's biggest solutions (atproto, astro, jest).
- **Source sweep flag flips**: flagflip's cells (`--strict` where the tsconfig isn't, noUncheckedIndexedAccess,
  exactOptionalPropertyTypes) on the source sweep's matching projects, cached per flag like `sweep.py --flags`; the
  missing direction there is unmeasured.

## Parked (the human decides)
- **CLI modes** (parked 2026-10-16): `--init`, `--showConfig`, `--explainFiles` are each a day or less and
  oracle-able byte for byte; skipping unchanged `.d.ts` writes is small; a `-b` buildinfo (as tttt's own file, never
  tsgo's version string) would let `-b` skip up-to-date projects; watch: W1 (inotify, full rerun) is UX parity, W2
  (affected files by declaration-emit signatures, fresh checkers) is what would beat `tsgo -w` on body edits; full
  `--incremental` and an LSP were judged not worth it. tttt's full check beat tsgo's incremental rebuild on hub edits
  almost everywhere and lost on body edits on 12 of 19 projects (vscode 6.2 vs 5.1 s on the 4-vCPU box). The
  measurements and sizing: `git show af936072c:agents/plans/76_cli_modes.md`.
