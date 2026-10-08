# Plan 52: the source sweep (real TypeScript repositories at breadth)
The source-code analogue of plan 44: tttt on 174 real TypeScript repositories, every `tsconfig.json` in each checked
with `-p`, against tsc 4d4f005c's cached diagnostics, mismatches ranked by root cause across repositories. Planned on
2026-10-08 from a 22-repository prototype; **stage 1 done the same day** (`./sourcesweep.py`, CLAUDE.md has the usage).

## Why
The pinned corpora are at 0 and go quiet once drained, and DefinitelyTyped covers declarations plus small tests. Every
new real project found tens to hundreds of bugs on its first day. The prototype confirms it: of 655 projects in 22
repos, **344 match tsc; 139 differ only in checker diagnostics, 83 only in config/program-level ones, 89 in both**
(~2700 missing and ~2600 spurious checker diagnostics, ~30k spurious config ones, mostly one TS6059 bug). Two real bugs
were found while building the prototype (fixed in the same commit):
- `tttt -p` with no `target` loaded an ES5 + few-extras lib list (`main.c` `default_libs`, a pre-6.0 leftover) instead of
  tsc 6's default target ES2025's full lib. Every corpus sets a target, so nothing noticed.
- `sweep.py` dropped diagnostics without a file (`[TS2688] ...`, options diagnostics) when parsing tttt's output, while
  tsc's oracle has them as `('', 0)`: always "missing". The DT sweep is unchanged by the fix (8594 / 8595).

## Stages
### Stage 1 (done 2026-10-08): the tool, the oracle, first numbers
- **Manifest** `test/sourcesweep/repos.txt`: the prototype's 21 (ky dropped: no lockfile) + 154 from GitHub search
  (`language:TypeScript stars:>3000 pushed:>2025-10-01`, size < 300 MB, a lockfile; libraries, frameworks, apps; the
  pinned corpora left out), minus cmdk (its lockfile is a format its own pnpm 8.8 rejects): **174 repos, 3363 projects**
  (3359 compared: tsc itself crashes on 4).
- **`thirdparty/ensure-sources.sh`**: depth-1 fetch at the commit, install under a pinned node 24 (`thirdparty/node`,
  sha256 in the script) with corepack and the repo's own package manager (frozen lockfile, scripts off; pnpm's version
  from `packageManager`, else an exact `engines.pnpm`, else the lockfile format's major; engines ignored), prune
  node_modules to TS/JS/JSON, then hardlink each file to an identical one of an earlier repo (content-hash index in
  `sources/.dedupe.db`). Batches of 20 with the package managers' caches deleted between them (pnpm's store reached 25 GB
  after 50 repos). ~1.5 h from scratch on this machine (yarn berry repos take up to 20 min), **~40 GB** on disk
  (~230 MB a repo). A `.tttt-installed` marker per repo makes it idempotent.
- **tsc oracle** `test/sourcesweep/tsc.jsonl.gz` (3.3 MB): `test/sweep_tsc.js`, projects handed to 3-4 node processes
  one at a time; **~58 min** wall for all (tsc's CPU total 10088 s). `--tsc-new` adds the projects of new repos.
- **tttt**: ~5 min wall on 4 cores (1112 s single-threaded in total), `--sample 4` (a fixed quarter, ~1 min) is
  `./oracles.py`'s.
- **Fixed on the way** (each a mechanism, 0 lost everywhere): 10 crashes (an instantiation expression's target read as
  an anonymous type: lexical, discord.js, folo, karakeep); module augmentation merged through an export specifier
  (vitest's `Assertion`/`File`, ~40 repos) and the TS2440 that fix first caused; parser: `a++ as T`, `asserts v` before
  a line break, JSDoc `typeof f<T>`, `@import import ...`, a JSDoc type before a parenthesized JS expression; test-runner
  names include `beforeEach` (TS2593). Sample 528 -> 554 / 822.
- **First numbers**: 2154 -> **2241 / 3359 projects match (746 clean under tsc), 47 / 174 repos fully match**; spurious
  520, missing 367, both 231, crash 0, timeout 0.

#### What stage 1 learned about the ranking
- The missing direction was mostly one config cascade (tsc reading referenced projects' unbuilt outputs under `-p`):
  fixed in stage 2. The spurious checker keys are real checker bugs, ranked by repos.
- Rank by repos/projects, not diagnostics: one broken project can carry thousands.

### Stage 2 (2026-10-08, first drain session): config level done, checker keys fanned out
Main session on the config/program level, four sub-agents (A relations, B access/context/narrowing, C circularity and
names, D the missing direction) on the checker keys, two rounds each. **2251 -> 2997 / 3359**, repos fully matching
49 -> 71, tttt's total time 1677 -> 811 s single-threaded; spurious 512 -> 277, missing 367 -> 33, both 229 -> 52. What the config level turned out to be:
- **`-p` reads referenced projects through their built outputs** (tsc's CLI: TS6305 when unbuilt, the module `any`),
  `-b` keeps reading sources (plan 41's decision now only for `-b`). This alone: 2251 -> 2573 and tttt's total time
  halved (no more checking referenced sources).
- Files found searching node_modules are never emitted (tsc's sourceFilesFoundSearchingNodeModules, not a path
  heuristic: workspace links leave node_modules); include patterns may name hidden directories (`.vitepress`); the
  default `exclude` is outDir/declarationDir; nested include base paths walked once (tsc's getBasePaths); `extends`
  through the package's own name; `"lib": []`; options diagnostics on configs without inputs; TS6059's location is the
  file's first import (also for a root file); program diagnostics in unchecked files dropped.
- Front end: NUL bytes inside templates and comments; `f<T> satisfies X`; grammar errors of unchecked files dropped
  before the CLI prints parse errors; the TS2875 jsx-runtime error at the first element the checker asks about, and
  function expression bodies deferred from their first check (tsc's checkNodeDeferred order).
- **The oracle**: `sourcesweep.py` runs tttt with `--keep-going=options` (syntactic diagnostics gate semantic ones like
  the oracle; options diagnostics don't, deliberately), and `sweep_tsc.js` adds declaration diagnostics when there are
  syntactic errors and tsc would emit (90 projects regenerated).
- **Never run plain `tsc -p` in `thirdparty/sources`**: it emits into the shared checkouts (a probing run wrote ~1250
  files into astro/mockoon/headlessui/nest/apollo/knip, which then resolved imports into the new dist/). Use `--noEmit`.

Left at the config level: TS5023 (unknown options in `compilerOptions`, tttt has no option table: unocss only),
tsc's include-pattern order of root files (agents/bugs/include_order.md, blocked by an order-dependent union reduction
in vue), the root-file TS6305's "file is in the program because" chain, a `/// <reference path>` to an unbuilt source
(TS6053 instead of TS6305). Checker leftovers are in the agents' bug files (agents/bugs/sourcesweep_checker.md,
conditional-walk-infers-returns.md, suggestion-count-check-order.md and others) and triage.md.

Where a second drain would start (end of stage 2, by repos / projects / diagnostics): +TS2322 31/51/165, +TS2345
27/74/277, +TS7006 26/52/215, +TS2339 21/39/196, +TS2578 15/24/52, +TS2769 11/17/21, +TS2344 10/13/54 (many are
`expectTypeOf` assertions failing on other wrong types), +TS2352 7/52/53, +TS2589 6/10/20; missing -TS2339 5/11/43,
-TS2552 5/10/16 (suggestion order, bugs/suggestion-count-check-order.md). The per-key returns are falling (a round of
four agents moved ~70 projects in its second round against ~200 in the first): worth another fan-out only together
with stage 3's flag flips, or one agent alongside other work.

### Stage 3 (later): the missing direction and `-b`
- Flag flips (plan 46 cells) on the projects that match: `--strict` where the tsconfig isn't, `noUncheckedIndexedAccess`,
  `exactOptionalPropertyTypes`, cached per flag like `./sweep.py --flags`.
- Solution configs (`files: []` + `references`) checked with `-b`: tsc's oracle then needs `tsc -b` to emit into a
  scratch copy of the tree (hardlinked copy, outputs are new files). Only if stage 2 shows `-p` on each leaf misses
  what `-b` would find.
- Mining upstream's own `tsc` invocations from package.json scripts, if the every-tsconfig policy turns out to be noisy.

## Risks
- Disk: ~40 GB used, ~54 GB free after stage 1. Keep checkouts under `thirdparty/sources`, not /tmp.
- Upstream drift doesn't matter (pinned commits), but a dependency registry outage does: the first fetch needs the
  network; after that only the checkouts are read. Don't re-fetch in `./sourcesweep.py`'s normal run.
- Noise from broken projects (shadcn-ui's root alone: 74k TS17004, jsx not set): fine for comparison, but a project
  with > ~5000 tsc diagnostics costs time and can dominate a key's diag count; rank by repos/projects, not diags.
